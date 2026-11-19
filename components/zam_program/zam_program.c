#include "zam_program.h"
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "zam_files.h"
#include "zam_runtime_lua.h"
#include "zam_output_ring.h"
#include "zam_rgb.h"
#define PROGRAM_STACK_BYTES 12288
#define PROGRAM_PRIORITY 4
#ifdef ZAM_AUTORUN_TEST_HOOK
extern void zam_autorun_test_event(void);
#define AUTORUN_TEST_EVENT() zam_autorun_test_event()
#else
#define AUTORUN_TEST_EVENT() ((void)0)
#endif

typedef struct { char path[ZAM_FILES_PATH_MAX+1]; } request_t;
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t requests;
static SemaphoreHandle_t indicator_gate;
static zam_output_ring_t output;
static zam_program_status_t current={.state=ZAM_PROGRAM_IDLE,.last_result=ZAM_PROGRAM_NONE};
static bool stop_flag;
/* Protected by guard; ownership stays reserved across the entire sequence. */
static bool autorun_owned;
static bool autorun_prepared;
static bool autorun_mutation;
static zam_autorun_status_t autorun={.state=ZAM_AUTORUN_IDLE};
static char autorun_names[ZAM_AUTORUN_MAX_SCRIPTS][ZAM_AUTORUN_NAME_MAX+1];
static bool initialized;
static int64_t stop_requested_at;

static bool stop_requested(void *context)
{
    (void)context;portENTER_CRITICAL(&guard);bool stop=stop_flag;portEXIT_CRITICAL(&guard);return stop;
}
static void mark_running(void *context)
{
    (void)context;portENTER_CRITICAL(&guard);
    if(current.state==ZAM_PROGRAM_STARTING)current.state=stop_flag?ZAM_PROGRAM_STOPPING:ZAM_PROGRAM_RUNNING;
    portEXIT_CRITICAL(&guard);
}
static void mark_cleanup(void *context)
{
    (void)context;portENTER_CRITICAL(&guard);current.state=ZAM_PROGRAM_CLEANUP;portEXIT_CRITICAL(&guard);
}
/* Single runtime producer, bounded copy; readers use the same short lock. */
static void emit(void *context,const char *bytes,size_t length)
{
    (void)context;
    portENTER_CRITICAL(&guard);
    current.output_produced=SIZE_MAX-current.output_produced<length?SIZE_MAX:current.output_produced+length;
    size_t lost=zam_output_write(&output,bytes,length);
    current.output_dropped=SIZE_MAX-current.output_dropped<lost?SIZE_MAX:current.output_dropped+lost;
    portEXIT_CRITICAL(&guard);
}
static zam_program_result_t convert(zam_lua_result_t result)
{
    switch(result){
    case ZAM_LUA_OK:return ZAM_PROGRAM_OK;
    case ZAM_LUA_STOPPED:return ZAM_PROGRAM_STOPPED;
    case ZAM_LUA_SYNTAX:return ZAM_PROGRAM_SYNTAX_ERROR;
    case ZAM_LUA_RUNTIME:return ZAM_PROGRAM_RUNTIME_ERROR;
    case ZAM_LUA_MEMORY:return ZAM_PROGRAM_MEMORY_ERROR;
    case ZAM_LUA_LOAD:return ZAM_PROGRAM_LOAD_ERROR;
    default:return ZAM_PROGRAM_INTERNAL_ERROR;
    }
}
/* The startup thread and runtime worker serialize only short RGB operations.
 * The runtime task is the only caller of Lua RGB; no RMT call holds guard. */
static void indicator(uint8_t r,uint8_t g,uint8_t b)
{
    if(indicator_gate && xSemaphoreTake(indicator_gate,pdMS_TO_TICKS(40))==pdTRUE){
        if(zam_rgb_available()) (void)zam_rgb_set(r,g,b);
        xSemaphoreGive(indicator_gate);
    }
}
void zam_program_indicator_ready(void)
{
    if(!indicator_gate || xSemaphoreTake(indicator_gate,pdMS_TO_TICKS(40))!=pdTRUE)return;
    portENTER_CRITICAL(&guard);bool idle=current.state==ZAM_PROGRAM_IDLE && current.last_result==ZAM_PROGRAM_NONE;portEXIT_CRITICAL(&guard);
    if(idle && zam_rgb_available()) (void)zam_rgb_set(0,4,0);
    xSemaphoreGive(indicator_gate);
}
/* A bounded inventory, with the existing UserFS gate held only during scan.
 * The LittleFS VFS reports DT_REG/DT_DIR and does not implement symlinks.
 * Unexpected entry types are skipped, never followed. */
static int scan_autorun(unsigned *found)
{
    *found=0;
    if(zam_files_lock()) return -1;
    DIR *dir=zam_files_open_dir("/autorun");
    if(!dir){int e=errno;zam_files_unlock();if(e==ENOENT)return 0;errno=e;return -1;}
    unsigned seen=0;int rc=0;
    for(;;){
        if(stop_requested(NULL)){rc=1;break;}
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry){if(errno)rc=-1;break;}
        const char *name=entry->d_name;
        if(!strcmp(name,".")||!strcmp(name,".."))continue;
        if(++seen>ZAM_AUTORUN_MAX_ENTRIES){errno=E2BIG;rc=-1;break;}
        size_t n=strlen(name);
        if(n>ZAM_AUTORUN_NAME_MAX){errno=ENAMETOOLONG;rc=-1;break;}
        if(entry->d_type==DT_UNKNOWN){errno=ENOTSUP;rc=-1;break;}
        if(entry->d_type!=DT_REG || n<4 || strcmp(name+n-4,".lua"))continue;
        char path[ZAM_FILES_PATH_MAX+1],physical[256];
        int len=snprintf(path,sizeof(path),"/autorun/%s",name);
        if(len<0 || (size_t)len>=sizeof(path) || zam_files_validate(path,false,physical,sizeof(physical))){errno=EINVAL;rc=-1;break;}
        if(*found>=ZAM_AUTORUN_MAX_SCRIPTS){errno=E2BIG;rc=-1;break;}
        strlcpy(autorun_names[(*found)++],name,sizeof(autorun_names[0]));
    }
    int saved=errno;if(closedir(dir) && !rc){saved=errno;rc=-1;}zam_files_unlock();errno=saved;
    if(rc)return rc;
    for(unsigned i=1;i<*found;i++){
        char name[ZAM_AUTORUN_NAME_MAX+1];strlcpy(name,autorun_names[i],sizeof(name));
        unsigned j=i;while(j && strcmp(autorun_names[j-1],name)>0){strlcpy(autorun_names[j],autorun_names[j-1],sizeof(autorun_names[j]));j--;}
        strlcpy(autorun_names[j],name,sizeof(autorun_names[j]));
    }
    return 0;
}
static zam_program_result_t execute_one(const char *path)
{
    portENTER_CRITICAL(&guard);
    size_t invalidated=zam_output_begin(&output);
    current.output_dropped=SIZE_MAX-current.output_dropped<invalidated?SIZE_MAX:current.output_dropped+invalidated;
    portEXIT_CRITICAL(&guard);
    indicator(0,0,4);
    zam_lua_metrics_t metrics;
    const zam_lua_callbacks_t cb={.stop_requested=stop_requested,.running=mark_running,.cleanup=mark_cleanup,.output=emit,.context=NULL};
    zam_lua_result_t result=zam_runtime_lua_execute(path,&cb,&metrics);
    if(result==ZAM_LUA_OK || result==ZAM_LUA_STOPPED)indicator(0,4,0);else indicator(4,0,0);
    size_t stack=uxTaskGetStackHighWaterMark(NULL);int64_t finished=esp_timer_get_time();
    portENTER_CRITICAL(&guard);
    current.last_result=convert(result);current.lua_peak=metrics.lua_peak;current.lua_live=metrics.lua_live;
    current.heap_before=metrics.heap_before;current.heap_after=metrics.heap_after;current.heap_minimum=metrics.heap_minimum;current.heap_largest=metrics.heap_largest;
    current.stack_free=stack;current.load_us=metrics.load_us;current.run_us=metrics.run_us;current.cleanup_us=metrics.cleanup_us;
    current.stop_us=stop_requested_at && finished>=stop_requested_at?finished-stop_requested_at:0;
    portEXIT_CRITICAL(&guard);
    return convert(result);
}
static void worker(void *unused)
{
    (void)unused;request_t request;
    for(;;){
        if(xQueueReceive(requests,&request,portMAX_DELAY)!=pdTRUE)continue;
        if(request.path[0]){
            (void)execute_one(request.path);
            portENTER_CRITICAL(&guard);stop_flag=false;stop_requested_at=0;current.state=ZAM_PROGRAM_IDLE;portEXIT_CRITICAL(&guard);
            continue;
        }
        unsigned count=0;int scan=scan_autorun(&count);
        portENTER_CRITICAL(&guard);
        autorun.discovered=count;
        if(scan<0){autorun.state=ZAM_AUTORUN_FAILED;autorun.result=ZAM_PROGRAM_LOAD_ERROR;strlcpy(autorun.message,"Directory scan failed",sizeof(autorun.message));}
        else if(stop_flag || scan==1){autorun.state=ZAM_AUTORUN_CANCELLED;autorun.result=ZAM_PROGRAM_STOPPED;}
        else {autorun.state=count?ZAM_AUTORUN_RUNNING:ZAM_AUTORUN_COMPLETED;autorun.result=count?ZAM_PROGRAM_NONE:ZAM_PROGRAM_OK;}
        portEXIT_CRITICAL(&guard);
        for(unsigned i=0;scan==0 && i<count;i++){
            char path[ZAM_FILES_PATH_MAX+1];
            portENTER_CRITICAL(&guard);
            if(stop_flag){autorun.state=ZAM_AUTORUN_CANCELLED;autorun.result=ZAM_PROGRAM_STOPPED;portEXIT_CRITICAL(&guard);break;}
            if(output.run_id==UINT64_MAX){autorun.state=ZAM_AUTORUN_FAILED;autorun.result=ZAM_PROGRAM_INTERNAL_ERROR;strlcpy(autorun.message,"Output run identifier exhausted",sizeof(autorun.message));portEXIT_CRITICAL(&guard);break;}
            strlcpy(autorun.current,autorun_names[i],sizeof(autorun.current));
            current.state=ZAM_PROGRAM_STARTING;
            portEXIT_CRITICAL(&guard);
            snprintf(path,sizeof(path),"/autorun/%s",autorun_names[i]);
            zam_program_result_t result=execute_one(path);
            portENTER_CRITICAL(&guard);
            if(result!=ZAM_PROGRAM_OK && result!=ZAM_PROGRAM_STOPPED){
                autorun.state=ZAM_AUTORUN_FAILED;autorun.result=result;
                strlcpy(autorun.message,"Script execution failed",sizeof(autorun.message));
            } else if(stop_flag || result==ZAM_PROGRAM_STOPPED){
                autorun.state=ZAM_AUTORUN_CANCELLED;autorun.result=ZAM_PROGRAM_STOPPED;
            } else {autorun.result=ZAM_PROGRAM_OK;autorun.completed++;}
            bool done=autorun.state!=ZAM_AUTORUN_RUNNING;
            portEXIT_CRITICAL(&guard);
            if(done)break;
            AUTORUN_TEST_EVENT();
        }
        portENTER_CRITICAL(&guard);
        if(stop_flag && autorun.state!=ZAM_AUTORUN_FAILED){autorun.state=ZAM_AUTORUN_CANCELLED;autorun.result=ZAM_PROGRAM_STOPPED;}
        else if(autorun.state==ZAM_AUTORUN_RUNNING){autorun.state=ZAM_AUTORUN_COMPLETED;autorun.result=ZAM_PROGRAM_OK;}
        autorun_owned=false;stop_flag=false;stop_requested_at=0;current.state=ZAM_PROGRAM_IDLE;
        portEXIT_CRITICAL(&guard);
    }
}
esp_err_t zam_program_init(void)
{
    if(initialized)return ESP_OK;
    if(zam_rgb_available()){indicator_gate=xSemaphoreCreateMutex();if(!indicator_gate)zam_rgb_shutdown();}
    requests=xQueueCreate(1,sizeof(request_t));if(!requests){if(indicator_gate){vSemaphoreDelete(indicator_gate);indicator_gate=NULL;}return ESP_ERR_NO_MEM;}
    if(xTaskCreate(worker,"zam_program",PROGRAM_STACK_BYTES,NULL,PROGRAM_PRIORITY,NULL)!=pdPASS){vQueueDelete(requests);requests=NULL;if(indicator_gate){vSemaphoreDelete(indicator_gate);indicator_gate=NULL;}return ESP_ERR_NO_MEM;}
    initialized=true;return ESP_OK;
}
esp_err_t zam_program_run(const char *path)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    char physical[256];if(zam_files_validate(path,false,physical,sizeof(physical)))return ESP_ERR_INVALID_ARG;
    request_t request;strlcpy(request.path,path,sizeof(request.path));
    portENTER_CRITICAL(&guard);
    bool available=current.state==ZAM_PROGRAM_IDLE && !autorun_owned && output.run_id!=UINT64_MAX;
    if(available){current.state=ZAM_PROGRAM_STARTING;stop_flag=false;stop_requested_at=0;}
    portEXIT_CRITICAL(&guard);
    if(!available)return ESP_ERR_INVALID_STATE;
    if(xQueueSend(requests,&request,0)!=pdTRUE){portENTER_CRITICAL(&guard);current.state=ZAM_PROGRAM_IDLE;stop_flag=false;portEXIT_CRITICAL(&guard);return ESP_FAIL;}
    return ESP_OK;
}
esp_err_t zam_program_stop(void)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&guard);
    bool active=autorun_owned||current.state==ZAM_PROGRAM_STARTING||current.state==ZAM_PROGRAM_RUNNING||current.state==ZAM_PROGRAM_STOPPING;
    if(active&&!stop_flag){stop_flag=true;stop_requested_at=esp_timer_get_time();current.state=ZAM_PROGRAM_STOPPING;}
    portEXIT_CRITICAL(&guard);
    return active?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t zam_program_get_status(zam_program_status_t *status)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    if(!status)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&guard);*status=current;portEXIT_CRITICAL(&guard);return ESP_OK;
}

esp_err_t zam_program_output_read(uint64_t cursor,void *destination,size_t capacity,zam_program_output_t *result)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    if(!result || (!destination && capacity))return ESP_ERR_INVALID_ARG;
    zam_output_snapshot_t snap;
    portENTER_CRITICAL(&guard);
    bool ok=zam_output_copy(&output,cursor,destination,capacity,&snap);
    if(ok)*result=(zam_program_output_t){.run_id=snap.run_id,.oldest=snap.oldest,.end=snap.end,.next=snap.next,.lost=snap.lost,.copied=snap.copied,.exhausted=snap.exhausted};
    portEXIT_CRITICAL(&guard);
    return ok?ESP_OK:ESP_ERR_INVALID_ARG;
}

esp_err_t zam_program_autorun_prepare(void)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&guard);
    bool available=!autorun_owned && !autorun_mutation && current.state==ZAM_PROGRAM_IDLE && output.run_id!=UINT64_MAX;
    if(available){autorun_owned=true;autorun_prepared=true;stop_flag=false;stop_requested_at=0;current.state=ZAM_PROGRAM_STARTING;
        memset(&autorun,0,sizeof(autorun));autorun.state=ZAM_AUTORUN_WAITING;}
    portEXIT_CRITICAL(&guard);
    return available?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t zam_program_autorun_launch(void)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    request_t request={0};
    portENTER_CRITICAL(&guard);
    bool ready=autorun_owned && autorun_prepared;
    if(ready){autorun_prepared=false;autorun.state=ZAM_AUTORUN_SCANNING;}
    portEXIT_CRITICAL(&guard);
    if(!ready)return ESP_ERR_INVALID_STATE;
    if(xQueueSend(requests,&request,0)!=pdTRUE){
        portENTER_CRITICAL(&guard);autorun_owned=false;current.state=ZAM_PROGRAM_IDLE;
        autorun.state=ZAM_AUTORUN_FAILED;autorun.result=ZAM_PROGRAM_INTERNAL_ERROR;
        strlcpy(autorun.message,"Queue unavailable",sizeof(autorun.message));
        stop_flag=false;stop_requested_at=0;portEXIT_CRITICAL(&guard);
        return ESP_FAIL;
    }
    return ESP_OK;
}
void zam_program_autorun_abort_prepared(void)
{
    if(!initialized)return;
    portENTER_CRITICAL(&guard);
    if(autorun_owned && autorun_prepared){autorun_prepared=false;autorun_owned=false;
        autorun.state=stop_flag?ZAM_AUTORUN_CANCELLED:ZAM_AUTORUN_FAILED;
        autorun.result=stop_flag?ZAM_PROGRAM_STOPPED:ZAM_PROGRAM_INTERNAL_ERROR;
        if(!stop_flag)strlcpy(autorun.message,"Boot services unavailable",sizeof(autorun.message));
        stop_flag=false;stop_requested_at=0;current.state=ZAM_PROGRAM_IDLE;}
    portEXIT_CRITICAL(&guard);
}
esp_err_t zam_program_autorun_start(void)
{
    esp_err_t err=zam_program_autorun_prepare();
    return err==ESP_OK?zam_program_autorun_launch():err;
}
esp_err_t zam_program_autorun_cancel(void)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&guard);bool owned=autorun_owned;
    if(owned && !stop_flag){stop_flag=true;stop_requested_at=esp_timer_get_time();current.state=ZAM_PROGRAM_STOPPING;}
    portEXIT_CRITICAL(&guard);return owned?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t zam_program_autorun_status(zam_autorun_status_t *status)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    if(!status)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&guard);*status=autorun;portEXIT_CRITICAL(&guard);return ESP_OK;
}
bool zam_program_autorun_busy(void)
{
    portENTER_CRITICAL(&guard);bool busy=autorun_owned;portEXIT_CRITICAL(&guard);return busy;
}

esp_err_t zam_program_autorun_mutation_begin(void)
{
    if(!initialized)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&guard);
    bool available=!autorun_owned && !autorun_mutation;
    if(available)autorun_mutation=true;
    portEXIT_CRITICAL(&guard);
    return available?ESP_OK:ESP_ERR_INVALID_STATE;
}
void zam_program_autorun_mutation_end(void)
{
    portENTER_CRITICAL(&guard);autorun_mutation=false;portEXIT_CRITICAL(&guard);
}
