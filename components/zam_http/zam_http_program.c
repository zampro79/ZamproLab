#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "zam_program.h"
#include "freertos/FreeRTOS.h"

#define PROGRAM_BODY_MAX 512
#define PROGRAM_BODY_DEADLINE_US 2000000
#define PROGRAM_REJECT_DRAIN_MAX 4096

/* At most 64 distinct requests per two-minute window; unexpired entries are
 * never evicted.  Reservations remain occupied while acceptance is in flight. */
#define RUN_IDS_CAPACITY 64
#define RUN_ID_TTL_US INT64_C(120000000)
#define RUN_PATH_MAX 192
#define RUN_ID_LENGTH 36
typedef enum { RUN_EMPTY, RUN_PENDING, RUN_ACCEPTED, RUN_INVALID, RUN_BUSY, RUN_UNAVAILABLE, RUN_FAILED } run_outcome_t;
typedef struct { char id[RUN_ID_LENGTH+1]; char path[RUN_PATH_MAX+1]; int64_t expires; run_outcome_t outcome; } run_entry_t;
static run_entry_t run_entries[RUN_IDS_CAPACITY];
static portMUX_TYPE run_guard=portMUX_INITIALIZER_UNLOCKED;

static bool valid_request_id(const char *id)
{
    if(!id || strlen(id)!=RUN_ID_LENGTH)return false;
    for(size_t i=0;i<RUN_ID_LENGTH;i++){
        char c=id[i];
        if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if(!((c>='0'&&c<='9') || (c>='a'&&c<='f')))return false;
    }
    return true;
}
/* Return slot for a new reservation, -1 if full, -2 conflict, or -3 duplicate.
 * Copy the prior outcome under lock; no locks are held during program or HTTP. */
static int reserve_run(const char *id,const char *path,int64_t now,run_outcome_t *prior)
{
    int free_slot=-1,answer=-1;
    portENTER_CRITICAL(&run_guard);
    for(int i=0;i<RUN_IDS_CAPACITY;i++){
        run_entry_t *e=&run_entries[i];
        if(e->outcome!=RUN_EMPTY && e->outcome!=RUN_PENDING && now>=e->expires)e->outcome=RUN_EMPTY;
        if(e->outcome==RUN_EMPTY){if(free_slot<0)free_slot=i;continue;}
        if(strcmp(e->id,id)==0){answer=strcmp(e->path,path)?-2:-3;*prior=e->outcome;goto done;}
    }
    if(free_slot>=0){run_entry_t *e=&run_entries[free_slot];memcpy(e->id,id,RUN_ID_LENGTH+1);strcpy(e->path,path);e->expires=0;e->outcome=RUN_PENDING;answer=free_slot;}
 done:
    portEXIT_CRITICAL(&run_guard);return answer;
}
static void complete_run(int slot,run_outcome_t result,int64_t now)
{
    portENTER_CRITICAL(&run_guard);run_entries[slot].expires=now+RUN_ID_TTL_US;run_entries[slot].outcome=result;portEXIT_CRITICAL(&run_guard);
}

static const char *state_name(zam_program_state_t state)
{
    switch(state){
    case ZAM_PROGRAM_IDLE:return "idle";
    case ZAM_PROGRAM_STARTING:return "starting";
    case ZAM_PROGRAM_RUNNING:return "running";
    case ZAM_PROGRAM_STOPPING:return "stopping";
    case ZAM_PROGRAM_CLEANUP:return "cleanup";
    default:return NULL;
    }
}
static const char *result_name(zam_program_result_t result)
{
    switch(result){
    case ZAM_PROGRAM_NONE:return NULL;
    case ZAM_PROGRAM_OK:return "ok";
    case ZAM_PROGRAM_STOPPED:return "stopped";
    case ZAM_PROGRAM_SYNTAX_ERROR:return "syntax_error";
    case ZAM_PROGRAM_RUNTIME_ERROR:return "runtime_error";
    case ZAM_PROGRAM_MEMORY_ERROR:return "memory_error";
    case ZAM_PROGRAM_LOAD_ERROR:return "load_error";
    case ZAM_PROGRAM_INTERNAL_ERROR:return "internal_error";
    default:return NULL;
    }
}
static const char *autorun_state_name(zam_autorun_state_t state)
{
    switch(state){
    case ZAM_AUTORUN_IDLE:return "idle";
    case ZAM_AUTORUN_WAITING:return "waiting";
    case ZAM_AUTORUN_SCANNING:return "scanning";
    case ZAM_AUTORUN_RUNNING:return "running";
    case ZAM_AUTORUN_COMPLETED:return "completed";
    case ZAM_AUTORUN_CANCELLED:return "cancelled";
    case ZAM_AUTORUN_FAILED:return "failed";
    default:return NULL;
    }
}
/* Close rejected requests so unread request bytes cannot contaminate keep-alive. */
static esp_err_t fail(httpd_req_t *req,const char *status,const char *code,const char *message)
{
    char out[192];
    int n=snprintf(out,sizeof(out),"{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",code,message);
    if(n<0 || (size_t)n>=sizeof(out))return ESP_FAIL;
    httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Connection","close");httpd_resp_sendstr(req,out);return ESP_FAIL;
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root,const char *status)
{
    if(!root)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    char *body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!body)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    esp_err_t err=httpd_resp_set_status(req,status);
    if(err==ESP_OK)err=httpd_resp_set_type(req,"application/json");
    if(err==ESP_OK)err=httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if(err==ESP_OK)err=httpd_resp_sendstr(req,body);
    free(body);return err;
}
static esp_err_t response_state(httpd_req_t *req,const char *status,const zam_program_status_t *s)
{
    const char *name=state_name(s->state);if(!name)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    cJSON *o=cJSON_CreateObject();
    if(!o || !cJSON_AddBoolToObject(o,"accepted",true) || !cJSON_AddStringToObject(o,"state",name)){cJSON_Delete(o);return fail(req,"500 Internal Server Error","internal_error","Internal error");}
    return send_json(req,o,status);
}
static bool json_content_type(httpd_req_t *req)
{
    size_t n=httpd_req_get_hdr_value_len(req,"Content-Type");char value[96];
    if(!n || n>=sizeof(value) || httpd_req_get_hdr_value_str(req,"Content-Type",value,sizeof(value))!=ESP_OK)return false;
    char *semicolon=strchr(value,';');if(semicolon)*semicolon=0;
    size_t len=strlen(value);while(len && (value[len-1]==' ' || value[len-1]=='\t'))value[--len]=0;
    return strcasecmp(value,"application/json")==0;
}
static esp_err_t run_handler(httpd_req_t *req)
{
    if(req->content_len>PROGRAM_BODY_MAX){
        /* Drain only small rejected bodies, bounded in bytes and time, so
         * ordinary clients receive 413 instead of a TCP reset on close. */
        if(req->content_len<=PROGRAM_REJECT_DRAIN_MAX){
            char discard[128];size_t left=req->content_len;int64_t deadline=esp_timer_get_time()+PROGRAM_BODY_DEADLINE_US;
            while(left && esp_timer_get_time()<deadline){
                size_t want=left<sizeof(discard)?left:sizeof(discard);
                int n=httpd_req_recv(req,discard,want);if(n<=0)break;left-=(size_t)n;
            }
        }
        return fail(req,"413 Payload Too Large","body_too_large","Request body too large");
    }
    if(!req->content_len)return fail(req,"400 Bad Request","invalid_body","JSON body required");
    if(!json_content_type(req))return fail(req,"415 Unsupported Media Type","unsupported_media_type","Expected application/json");
    char body[PROGRAM_BODY_MAX+1];size_t n=0;int64_t deadline=esp_timer_get_time()+PROGRAM_BODY_DEADLINE_US;
    while(n<req->content_len){
        if(esp_timer_get_time()>=deadline)return fail(req,"408 Request Timeout","body_timeout","Request body timeout");
        int got=httpd_req_recv(req,body+n,req->content_len-n);
        if(esp_timer_get_time()>=deadline)return fail(req,"408 Request Timeout","body_timeout","Request body timeout");
        if(got<=0)return fail(req,"400 Bad Request","invalid_body","Could not read JSON body");
        n+=(size_t)got;
    }
    if(memchr(body,0,n))return fail(req,"400 Bad Request","invalid_body","Invalid JSON body");
    body[n]=0;
    /* UserFS paths are ASCII; reject Unicode escapes including embedded NUL. */
    if(strstr(body,"\\u") || strstr(body,"\\U"))return fail(req,"400 Bad Request","invalid_body","Invalid JSON body");
    const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(body,&end,1);
    if(!cJSON_IsObject(o) || cJSON_GetArraySize(o)!=2){cJSON_Delete(o);return fail(req,"400 Bad Request","invalid_body","Expected path and request_id");}
    cJSON *p=cJSON_GetObjectItemCaseSensitive(o,"path"),*id=cJSON_GetObjectItemCaseSensitive(o,"request_id");
    if(!cJSON_IsString(p) || !p->valuestring || strlen(p->valuestring)>RUN_PATH_MAX){cJSON_Delete(o);return fail(req,"400 Bad Request","invalid_path","Path must be a valid string");}
    if(!cJSON_IsString(id) || !valid_request_id(id->valuestring)){cJSON_Delete(o);return fail(req,"400 Bad Request","invalid_request_id","Invalid request identifier");}
    run_outcome_t previous=RUN_EMPTY;int slot=reserve_run(id->valuestring,p->valuestring,esp_timer_get_time(),&previous);
    if(slot==-2){cJSON_Delete(o);return fail(req,"409 Conflict","request_id_conflict","Request identifier used for another path");}
    if(slot==-1){cJSON_Delete(o);return fail(req,"503 Service Unavailable","request_registry_full","Request registry full");}
    if(slot==-3){
        cJSON_Delete(o);
        if(previous==RUN_ACCEPTED){cJSON *r=cJSON_CreateObject();if(!r || !cJSON_AddBoolToObject(r,"accepted",true) || !cJSON_AddBoolToObject(r,"duplicate",true)){cJSON_Delete(r);return fail(req,"500 Internal Server Error","internal_error","Internal error");}return send_json(req,r,"202 Accepted");}
        if(previous==RUN_PENDING)return fail(req,"409 Conflict","request_pending","Request acceptance in progress");
        if(previous==RUN_INVALID)return fail(req,"400 Bad Request","invalid_path","Invalid UserFS path");
        if(previous==RUN_BUSY)return fail(req,"409 Conflict","program_busy","Program manager busy");
        if(previous==RUN_UNAVAILABLE)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
        return fail(req,"500 Internal Server Error","internal_error","Internal error");
    }
    esp_err_t result=zam_program_run(p->valuestring);cJSON_Delete(o);
    run_outcome_t outcome=RUN_FAILED;
    if(result==ESP_OK)outcome=RUN_ACCEPTED;
    else if(result==ESP_ERR_INVALID_ARG)outcome=RUN_INVALID;
    else if(result==ESP_ERR_INVALID_STATE){zam_program_status_t tmp;outcome=zam_program_get_status(&tmp)==ESP_OK?RUN_BUSY:RUN_UNAVAILABLE;}
    complete_run(slot,outcome,esp_timer_get_time());
    if(outcome==RUN_INVALID)return fail(req,"400 Bad Request","invalid_path","Invalid UserFS path");
    if(outcome==RUN_BUSY)return fail(req,"409 Conflict","program_busy","Program manager busy");
    if(outcome==RUN_UNAVAILABLE)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
    if(outcome!=RUN_ACCEPTED)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    zam_program_status_t st;if(zam_program_get_status(&st)!=ESP_OK)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
    return response_state(req,"202 Accepted",&st);
}
static esp_err_t stop_handler(httpd_req_t *req)
{
    if(req->content_len)return fail(req,"400 Bad Request","invalid_body","STOP body must be empty");
    esp_err_t result=zam_program_stop();zam_program_status_t s;
    if(zam_program_get_status(&s)!=ESP_OK)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
    if(result==ESP_ERR_INVALID_STATE && s.state==ZAM_PROGRAM_IDLE)return response_state(req,"200 OK",&s);
    if(result==ESP_ERR_INVALID_STATE)return fail(req,"409 Conflict","program_busy","Program manager busy");
    if(result!=ESP_OK)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    return response_state(req,"202 Accepted",&s);
}
static esp_err_t status_handler(httpd_req_t *req)
{
    if(req->content_len)return fail(req,"400 Bad Request","invalid_body","STATUS body must be empty");
    zam_program_status_t s;if(zam_program_get_status(&s)!=ESP_OK)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
    zam_autorun_status_t automatic;
    if(zam_program_autorun_status(&automatic)!=ESP_OK)return fail(req,"503 Service Unavailable","program_unavailable","Program manager unavailable");
    const char *auto_state=autorun_state_name(automatic.state);
    const char *auto_result=result_name(automatic.result);
    const char *state=state_name(s.state);const char *result=result_name(s.last_result);
    if(!state || !auto_state || (!auto_result && automatic.result!=ZAM_PROGRAM_NONE) || (!result && s.last_result!=ZAM_PROGRAM_NONE))return fail(req,"500 Internal Server Error","internal_error","Internal error");
    cJSON *o=cJSON_CreateObject();if(!o)return fail(req,"500 Internal Server Error","internal_error","Internal error");
    cJSON *mem=cJSON_AddObjectToObject(o,"last_memory"),*output=cJSON_AddObjectToObject(o,"output"),*timing=cJSON_AddObjectToObject(o,"last_timing_us");
    cJSON *auto_status=cJSON_AddObjectToObject(o,"autorun");
    if(!auto_status || !cJSON_AddStringToObject(auto_status,"state",auto_state) ||
       !cJSON_AddNumberToObject(auto_status,"discovered",automatic.discovered) ||
       !cJSON_AddNumberToObject(auto_status,"completed",automatic.completed) ||
       !(automatic.current[0]?cJSON_AddStringToObject(auto_status,"current",automatic.current):cJSON_AddNullToObject(auto_status,"current")) ||
       !(auto_result?cJSON_AddStringToObject(auto_status,"result",auto_result):cJSON_AddNullToObject(auto_status,"result")) ||
       !cJSON_AddStringToObject(auto_status,"message",automatic.message) ||
       !mem||!output||!timing||!cJSON_AddStringToObject(o,"state",state)||
       !(result?cJSON_AddStringToObject(o,"last_result",result):cJSON_AddNullToObject(o,"last_result"))||
       !cJSON_AddNumberToObject(mem,"live",s.lua_live)||!cJSON_AddNumberToObject(mem,"peak",s.lua_peak)||
       !cJSON_AddNumberToObject(mem,"limit",CONFIG_ZAM_LUA_HEAP_LIMIT)||!cJSON_AddNumberToObject(mem,"stack_free",s.stack_free)||
       !cJSON_AddNumberToObject(output,"produced",s.output_produced)||!cJSON_AddNumberToObject(output,"dropped",s.output_dropped)||
       !cJSON_AddNumberToObject(timing,"load",(double)s.load_us)||!cJSON_AddNumberToObject(timing,"run",(double)s.run_us)||
       !cJSON_AddNumberToObject(timing,"cleanup",(double)s.cleanup_us)||!cJSON_AddNumberToObject(timing,"stop",(double)s.stop_us)){
        cJSON_Delete(o);return fail(req,"500 Internal Server Error","internal_error","Internal error");
    }
    return send_json(req,o,"200 OK");
}
esp_err_t zam_http_register_program(httpd_handle_t server)
{
    zam_program_status_t state;
    if(zam_program_get_status(&state)!=ESP_OK)return ESP_OK;
    static const httpd_uri_t routes[]={
        {.uri="/api/program/run",.method=HTTP_POST,.handler=run_handler},
        {.uri="/api/program/stop",.method=HTTP_POST,.handler=stop_handler},
        {.uri="/api/program/status",.method=HTTP_GET,.handler=status_handler},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        esp_err_t err=httpd_register_uri_handler(server,&routes[i]);if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}
