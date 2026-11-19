#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "zam_program.h"
#include "zam_files.h"
#include "zam_runtime_lua.h"
struct fake_queue{pthread_mutex_t mutex;pthread_cond_t cond;char item[ZAM_FILES_PATH_MAX+1];int occupied;};
extern atomic_int test_rgb_enabled,test_rgb_last,test_rgb_fail;
static atomic_int gate_open,entered,running,executions,allow_cleanup;
static const char *autorun_dir;
static atomic_int scan_entered,scan_hold,sequence_count,scan_error,between_hold,between_entered,cleanup_entered;
void zam_autorun_test_event(void){if(atomic_load(&between_hold)){atomic_store(&between_entered,1);while(atomic_load(&between_hold)){struct timespec t={0,1000000};nanosleep(&t,NULL);}}}
static char sequence_paths[64][ZAM_FILES_PATH_MAX+1];
int zam_files_lock(void){return 0;}
void zam_files_unlock(void){}
DIR *zam_files_open_dir(const char *p){(void)p;atomic_store(&scan_entered,1);while(atomic_load(&scan_hold)) {struct timespec t={0,1000000};nanosleep(&t,NULL);}if(atomic_load(&scan_error)){errno=EIO;return NULL;}if(!autorun_dir){errno=ENOENT;return NULL;}return opendir(autorun_dir);}

static pthread_barrier_t output_first,output_second;
static void pause_ms(long ms){struct timespec ts={.tv_sec=ms/1000,.tv_nsec=(ms%1000)*1000000};nanosleep(&ts,NULL);}
size_t strlcpy(char *dst,const char *src,size_t n){size_t l=strlen(src);if(n){size_t c=l<n-1?l:n-1;memcpy(dst,src,c);dst[c]=0;}return l;}
QueueHandle_t xQueueCreate(unsigned count,unsigned item_size){assert(count==1 && item_size==ZAM_FILES_PATH_MAX+1);struct fake_queue *q=calloc(1,sizeof(*q));pthread_mutex_init(&q->mutex,NULL);pthread_cond_init(&q->cond,NULL);return q;}
int xQueueSend(QueueHandle_t q,const void *item,uint32_t ticks){(void)ticks;pthread_mutex_lock(&q->mutex);int ok=!q->occupied;if(ok){memcpy(q->item,item,sizeof(q->item));q->occupied=1;pthread_cond_signal(&q->cond);}pthread_mutex_unlock(&q->mutex);return ok;}
int xQueueReceive(QueueHandle_t q,void *item,uint32_t ticks){(void)ticks;pthread_mutex_lock(&q->mutex);while(!q->occupied)pthread_cond_wait(&q->cond,&q->mutex);memcpy(item,q->item,sizeof(q->item));q->occupied=0;pthread_mutex_unlock(&q->mutex);return 1;}
void vQueueDelete(QueueHandle_t q){free(q);}
struct start{void (*fn)(void *);void *arg;};
static void *launch(void *v){struct start *s=v;void(*f)(void *)=s->fn;void *a=s->arg;free(s);f(a);return NULL;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle){(void)name;(void)stack;(void)priority;struct start *s=malloc(sizeof(*s));*s=(struct start){fn,arg};pthread_t t;int err=pthread_create(&t,NULL,launch,s);if(err){free(s);return 0;}pthread_detach(t);if(handle)*handle=(void *)1;return 1;}
void vTaskDelay(unsigned ticks){pause_ms(ticks);}
void vTaskDelete(TaskHandle_t task){(void)task;}
size_t uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 1000;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int zam_files_validate(const char *p,bool root,char *out,size_t cap){(void)root;if(!p||p[0]!='/'||!strcmp(p,"/")||strstr(p,".."))return -1;return snprintf(out,cap,"/user%s",p)>=(int)cap?-1:0;}
zam_lua_result_t zam_runtime_lua_execute(const char *p,const zam_lua_callbacks_t *cb,zam_lua_metrics_t *m){memset(m,0,sizeof(*m));atomic_fetch_add(&executions,1);
 if(!strncmp(p,"/autorun/",9)){
 int i=atomic_fetch_add(&sequence_count,1);if(i<64)strlcpy(sequence_paths[i],p,sizeof(sequence_paths[i]));
 if(strstr(p,"race_error") || strstr(p,"race_cleanup") || strstr(p,"race_ok")){
    cb->running(cb->context);cb->cleanup(cb->context);atomic_store(&cleanup_entered,1);
    while(!atomic_load(&allow_cleanup))pause_ms(1);
    return strstr(p,"race_cleanup")?ZAM_LUA_INTERNAL:strstr(p,"race_error")?ZAM_LUA_RUNTIME:ZAM_LUA_OK;
 }
 if(strstr(p,"syntax")){cb->cleanup(cb->context);return ZAM_LUA_SYNTAX;}
 if(strstr(p,"memory")){cb->cleanup(cb->context);return ZAM_LUA_MEMORY;}
 if(strstr(p,"error")){cb->cleanup(cb->context);return ZAM_LUA_RUNTIME;}
 if(strstr(p,"cleanup")){cb->running(cb->context);cb->cleanup(cb->context);atomic_store(&cleanup_entered,1);while(!atomic_load(&allow_cleanup))pause_ms(1);return ZAM_LUA_OK;}
 if(strstr(p,"wait")){atomic_store(&entered,1);cb->running(cb->context);while(!cb->stop_requested(cb->context))pause_ms(1);while(!atomic_load(&allow_cleanup))pause_ms(1);cb->cleanup(cb->context);return ZAM_LUA_STOPPED;}
 cb->running(cb->context);cb->cleanup(cb->context);return ZAM_LUA_OK;
 }
 if(!strcmp(p,"/handshake.lua")){cb->output(cb->context,"abc",3);pthread_barrier_wait(&output_first);pthread_barrier_wait(&output_second);cb->output(cb->context,"def",3);cb->cleanup(cb->context);return ZAM_LUA_OK;}
 if(!strcmp(p,"/error.lua")){char msg[]="ERR!";cb->output(cb->context,msg,4);memset(msg,'?',4);cb->cleanup(cb->context);return ZAM_LUA_RUNTIME;}
 if(!strcmp(p,"/hello.lua")){char msg[]="OK!";cb->output(cb->context,msg,3);memset(msg,'?',3);cb->cleanup(cb->context);return ZAM_LUA_OK;}atomic_store(&entered,1);while(!atomic_load(&gate_open)&&!cb->stop_requested(cb->context))pause_ms(1);if(cb->stop_requested(cb->context)){while(!atomic_load(&allow_cleanup))pause_ms(1);cb->cleanup(cb->context);return ZAM_LUA_STOPPED;}cb->running(cb->context);atomic_store(&running,1);for(int i=0;i<10000;i++)cb->output(cb->context,"abcdefghijklmnopqrstuvwxyz",26);while(!cb->stop_requested(cb->context))pause_ms(1);while(!atomic_load(&allow_cleanup))pause_ms(1);cb->cleanup(cb->context);return ZAM_LUA_STOPPED;}
static void wait_for(atomic_int *value){for(int i=0;i<3000&&!atomic_load(value);i++)pause_ms(1);assert(atomic_load(value));}
static void wait_state(zam_program_state_t target){zam_program_status_t s;for(int i=0;i<3000;i++){assert(zam_program_get_status(&s)==ESP_OK);if(s.state==target)return;pause_ms(1);}assert(!"timeout waiting for state");}
static pthread_barrier_t barrier;static int answers[2];
static void *run_race(void *p){int i=(int)(intptr_t)p;pthread_barrier_wait(&barrier);answers[i]=zam_program_run("/test.lua");return NULL;}
static void *stop_race(void *p){int i=(int)(intptr_t)p;pthread_barrier_wait(&barrier);answers[i]=zam_program_stop();return NULL;}
static void *mutation_race(void *p){int i=(int)(intptr_t)p;pthread_barrier_wait(&barrier);answers[i]=i?zam_program_autorun_prepare():zam_program_autorun_mutation_begin();pthread_barrier_wait(&barrier);if(answers[i]==ESP_OK){if(i)zam_program_autorun_abort_prepared();else zam_program_autorun_mutation_end();}return NULL;}
static void wait_autorun(zam_autorun_state_t target){zam_autorun_status_t s;for(int i=0;i<3000;i++){assert(zam_program_autorun_status(&s)==ESP_OK);if(s.state==target&&!zam_program_autorun_busy())return;pause_ms(1);}assert(!"timeout waiting for autorun");}
static void touch_file(const char *root,const char *name){char path[512];snprintf(path,sizeof(path),"%s/%s",root,name);FILE *f=fopen(path,"w");assert(f);fputs("return",f);fclose(f);}
static atomic_int reader_done;static atomic_size_t consumed;
static void *read_concurrently(void *arg){(void)arg;char data[31];zam_program_output_t initial;assert(zam_program_output_read(0,NULL,0,&initial)==ESP_OK);uint64_t cursor=initial.end;while(!atomic_load(&reader_done)){zam_program_output_t out;assert(zam_program_output_read(cursor,data,sizeof(data),&out)==ESP_OK);for(size_t i=0;i<out.copied;i++)assert(data[i]>='a' && data[i]<='z');atomic_fetch_add(&consumed,out.copied);cursor=out.next;if(!out.copied)pause_ms(1);}return NULL;}
int main(void){atomic_store(&test_rgb_enabled,1);assert(zam_program_init()==ESP_OK);zam_program_indicator_ready();assert(atomic_load(&test_rgb_last)==(4<<8));assert(zam_program_stop()==ESP_ERR_INVALID_STATE);assert(zam_program_run("/../bad")==ESP_ERR_INVALID_ARG);
 pthread_t a,b;pthread_barrier_init(&barrier,NULL,3);pthread_create(&a,NULL,run_race,(void *)0);pthread_create(&b,NULL,run_race,(void *)1);pthread_barrier_wait(&barrier);pthread_join(a,NULL);pthread_join(b,NULL);assert((answers[0]==ESP_OK)+(answers[1]==ESP_OK)==1);wait_for(&entered);wait_state(ZAM_PROGRAM_STARTING);assert(atomic_load(&test_rgb_last)==4);zam_program_indicator_ready();assert(atomic_load(&test_rgb_last)==4);
 assert(zam_program_run("/second.lua")==ESP_ERR_INVALID_STATE);pthread_barrier_destroy(&barrier);pthread_barrier_init(&barrier,NULL,3);pthread_create(&a,NULL,stop_race,(void *)0);pthread_create(&b,NULL,stop_race,(void *)1);pthread_barrier_wait(&barrier);pthread_join(a,NULL);pthread_join(b,NULL);assert(answers[0]==ESP_OK && answers[1]==ESP_OK);assert(zam_program_run("/third.lua")==ESP_ERR_INVALID_STATE);atomic_store(&allow_cleanup,1);wait_state(ZAM_PROGRAM_IDLE);zam_program_status_t st;assert(zam_program_get_status(&st)==ESP_OK&&st.last_result==ZAM_PROGRAM_STOPPED);assert(atomic_load(&test_rgb_last)==(4<<8));assert(atomic_load(&executions)==1);
 atomic_store(&gate_open,1);assert(zam_program_run("/test.lua")==ESP_OK);wait_for(&running);wait_state(ZAM_PROGRAM_RUNNING);assert(zam_program_stop()==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);assert(zam_program_get_status(&st)==ESP_OK&&st.output_produced==260000&&st.output_dropped==260000-2048);assert(atomic_load(&executions)==2);
 char data[2050];zam_program_output_t out;
 assert(zam_program_output_read(0,NULL,1,&out)==ESP_ERR_INVALID_ARG);
 assert(zam_program_output_read(0,data,1000,&out)==ESP_OK && out.copied==1000 && out.lost==260000-2048);
 for(size_t i=0;i<out.copied;i++)assert(data[i]=="abcdefghijklmnopqrstuvwxyz"[(i+260000-2048)%26]);
 uint64_t old_cursor=out.next;
 assert(zam_program_run("/error.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);assert(zam_program_get_status(&st)==ESP_OK&&st.last_result==ZAM_PROGRAM_RUNTIME_ERROR);assert(atomic_load(&test_rgb_last)==(4<<16));zam_program_indicator_ready();assert(atomic_load(&test_rgb_last)==(4<<16));
 assert(zam_program_output_read(old_cursor,data,sizeof(data),&out)==ESP_OK && out.copied==4 && out.lost==1048 && !memcmp(data,"ERR!",4));
 assert(zam_program_run("/hello.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);assert(zam_program_get_status(&st)==ESP_OK&&st.last_result==ZAM_PROGRAM_OK);assert(atomic_load(&test_rgb_last)==(4<<8));
 assert(zam_program_output_read(out.next,data,sizeof(data),&out)==ESP_OK&&out.copied==3&&!memcmp(data,"OK!",3));
 atomic_store(&test_rgb_fail,1);assert(zam_program_run("/hello.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);assert(zam_program_get_status(&st)==ESP_OK && st.last_result==ZAM_PROGRAM_OK);atomic_store(&test_rgb_fail,0);
 assert(st.output_produced==260010 && st.output_dropped==260000-2048+2048+4+3);
 pthread_t reader;assert(!pthread_create(&reader,NULL,read_concurrently,NULL));
 assert(zam_program_run("/test.lua")==ESP_OK);pause_ms(50);assert(zam_program_stop()==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);atomic_store(&reader_done,1);pthread_join(reader,NULL);
 assert(atomic_load(&consumed)>0);assert(zam_program_get_status(&st)==ESP_OK && st.last_result==ZAM_PROGRAM_STOPPED);
 zam_program_output_t previous,part1,part2;assert(zam_program_output_read(0,NULL,0,&previous)==ESP_OK);
 pthread_barrier_init(&output_first,NULL,2);pthread_barrier_init(&output_second,NULL,2);
 assert(zam_program_run("/handshake.lua")==ESP_OK);pthread_barrier_wait(&output_first);
 assert(zam_program_output_read(previous.end,data,3,&part1)==ESP_OK && part1.copied==3 && !memcmp(data,"abc",3));
 pthread_barrier_wait(&output_second);wait_state(ZAM_PROGRAM_IDLE);
 assert(zam_program_output_read(part1.next,data,3,&part2)==ESP_OK && part2.copied==3 && !memcmp(data,"def",3) && part1.run_id==part2.run_id);
 pthread_barrier_destroy(&output_first);pthread_barrier_destroy(&output_second);
 puts("Program manager RUN/STOP, bounded output, wrap, copy, concurrent read and recovery PASS");
 pthread_barrier_destroy(&barrier);
 for(int i=0;i<100;i++){pthread_barrier_init(&barrier,NULL,3);pthread_create(&a,NULL,mutation_race,(void *)0);pthread_create(&b,NULL,mutation_race,(void *)1);pthread_barrier_wait(&barrier);pthread_barrier_wait(&barrier);pthread_join(a,NULL);pthread_join(b,NULL);assert((answers[0]==ESP_OK)+(answers[1]==ESP_OK)==1);pthread_barrier_destroy(&barrier);}
 assert(zam_program_autorun_mutation_begin()==ESP_OK);
 assert(zam_program_autorun_mutation_begin()==ESP_ERR_INVALID_STATE);
 assert(zam_program_autorun_prepare()==ESP_ERR_INVALID_STATE);
 zam_program_autorun_mutation_end();
 assert(zam_program_autorun_prepare()==ESP_OK);assert(zam_program_run("/hello.lua")==ESP_ERR_INVALID_STATE);
 assert(zam_program_autorun_mutation_begin()==ESP_ERR_INVALID_STATE);
 assert(zam_program_stop()==ESP_OK);assert(zam_program_autorun_launch()==ESP_OK);assert(zam_program_autorun_launch()==ESP_ERR_INVALID_STATE);wait_autorun(ZAM_AUTORUN_CANCELLED);
 assert(zam_program_autorun_prepare()==ESP_OK);zam_program_autorun_abort_prepared();assert(!zam_program_autorun_busy());assert(zam_program_autorun_launch()==ESP_ERR_INVALID_STATE);
 assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);zam_autorun_status_t as;assert(zam_program_autorun_status(&as)==ESP_OK&&as.discovered==0&&as.result==ZAM_PROGRAM_OK);
 char root[]="/tmp/zam-autorun-host-XXXXXX";assert(mkdtemp(root));autorun_dir=root;
 assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.discovered==0);
 touch_file(root,"single.lua");atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);assert(atomic_load(&sequence_count)==1);char one[512];snprintf(one,sizeof(one),"%s/single.lua",root);assert(!unlink(one));
 char linkpath[512];snprintf(linkpath,sizeof(linkpath),"%s/link.lua",root);assert(!symlink("/etc/passwd",linkpath));
 touch_file(root,"b.lua");touch_file(root,"A.lua");touch_file(root,"a.lua");touch_file(root,"ignore.txt");char folder[512];snprintf(folder,sizeof(folder),"%s/folder.lua",root);assert(!mkdir(folder,0700));
 atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);
 assert(zam_program_autorun_status(&as)==ESP_OK&&as.discovered==3&&as.completed==3);assert(atomic_load(&sequence_count)==3);
 assert(!strcmp(sequence_paths[0],"/autorun/A.lua")&&!strcmp(sequence_paths[1],"/autorun/a.lua")&&!strcmp(sequence_paths[2],"/autorun/b.lua"));
 touch_file(root,"B_error.lua");atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(atomic_load(&sequence_count)==2);char mid[512];snprintf(mid,sizeof(mid),"%s/B_error.lua",root);assert(!unlink(mid));
 touch_file(root,"0_error.lua");for(int i=0;i<20;i++){atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(atomic_load(&sequence_count)==1);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==ZAM_PROGRAM_RUNTIME_ERROR);}
 char path[512];snprintf(path,sizeof(path),"%s/0_error.lua",root);assert(!unlink(path));
 const char *bad[] = {"0_syntax.lua","0_memory.lua"};zam_program_result_t expected[]={ZAM_PROGRAM_SYNTAX_ERROR,ZAM_PROGRAM_MEMORY_ERROR};
 for(int i=0;i<2;i++){touch_file(root,bad[i]);atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==expected[i]);assert(atomic_load(&sequence_count)==1);snprintf(path,sizeof(path),"%s/%s",root,bad[i]);assert(!unlink(path));}
 for(int i=0;i<20;i++){atomic_store(&scan_hold,1);atomic_store(&scan_entered,0);assert(zam_program_autorun_start()==ESP_OK);wait_for(&scan_entered);assert(zam_program_run("/hello.lua")==ESP_ERR_INVALID_STATE);assert(zam_program_stop()==ESP_OK);assert(zam_program_autorun_cancel()==ESP_OK);atomic_store(&scan_hold,0);wait_autorun(ZAM_AUTORUN_CANCELLED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==ZAM_PROGRAM_STOPPED);assert(zam_program_run("/hello.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);}
 touch_file(root,"0_wait.lua");atomic_store(&sequence_count,0);atomic_store(&entered,0);atomic_store(&allow_cleanup,0);assert(zam_program_autorun_start()==ESP_OK);wait_for(&entered);assert(zam_program_stop()==ESP_OK);assert(zam_program_run("/hello.lua")==ESP_ERR_INVALID_STATE);atomic_store(&allow_cleanup,1);wait_autorun(ZAM_AUTORUN_CANCELLED);assert(atomic_load(&sequence_count)==1);assert(zam_program_run("/hello.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);
 snprintf(path,sizeof(path),"%s/0_wait.lua",root);assert(!unlink(path));
 atomic_store(&scan_error,1);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);atomic_store(&scan_error,0);
 atomic_store(&between_hold,1);atomic_store(&between_entered,0);atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_for(&between_entered);assert(zam_program_stop()==ESP_OK);assert(zam_program_run("/hello.lua")==ESP_ERR_INVALID_STATE);atomic_store(&between_hold,0);wait_autorun(ZAM_AUTORUN_CANCELLED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==ZAM_PROGRAM_STOPPED);assert(atomic_load(&sequence_count)==1);
 touch_file(root,"0_cleanup.lua");atomic_store(&cleanup_entered,0);atomic_store(&allow_cleanup,0);atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_for(&cleanup_entered);assert(zam_program_stop()==ESP_OK);assert(zam_program_stop()==ESP_OK);atomic_store(&allow_cleanup,1);wait_autorun(ZAM_AUTORUN_CANCELLED);assert(atomic_load(&sequence_count)==1);
 snprintf(path,sizeof(path),"%s/0_cleanup.lua",root);assert(!unlink(path));
 const char *race_names[]={"0_race_ok.lua","0_race_error.lua","0_race_cleanup.lua"};
 zam_program_result_t race_results[]={ZAM_PROGRAM_STOPPED,ZAM_PROGRAM_RUNTIME_ERROR,ZAM_PROGRAM_INTERNAL_ERROR};
 for(int i=0;i<3;i++){
  touch_file(root,race_names[i]);atomic_store(&cleanup_entered,0);atomic_store(&allow_cleanup,0);atomic_store(&sequence_count,0);
  assert(zam_program_autorun_start()==ESP_OK);wait_for(&cleanup_entered);assert(zam_program_run("/hello.lua")==ESP_ERR_INVALID_STATE);
  assert(zam_program_stop()==ESP_OK);assert(zam_program_stop()==ESP_OK);atomic_store(&allow_cleanup,1);
  wait_autorun(i?ZAM_AUTORUN_FAILED:ZAM_AUTORUN_CANCELLED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==race_results[i]);
  assert(!zam_program_autorun_busy()&&atomic_load(&sequence_count)==1);assert(zam_program_run("/hello.lua")==ESP_OK);wait_state(ZAM_PROGRAM_IDLE);
  snprintf(path,sizeof(path),"%s/%s",root,race_names[i]);assert(!unlink(path));
 }
 touch_file(root,"0_race_cleanup.lua");atomic_store(&cleanup_entered,0);atomic_store(&allow_cleanup,1);
 assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(zam_program_autorun_status(&as)==ESP_OK&&as.result==ZAM_PROGRAM_INTERNAL_ERROR);
 snprintf(path,sizeof(path),"%s/0_race_cleanup.lua",root);assert(!unlink(path));
 char longname[80];memset(longname,'x',64);strcpy(longname+64,".lua");touch_file(root,longname);atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(atomic_load(&sequence_count)==0);snprintf(path,sizeof(path),"%s/%s",root,longname);assert(!unlink(path));
 for(int i=0;i<30;i++){atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);assert(atomic_load(&sequence_count)==3);assert(zam_program_get_status(&st)==ESP_OK&&st.lua_live==0);}
 for(int i=0;i<30;i++){char name[30];snprintf(name,sizeof(name),"extra%02d.lua",i);touch_file(root,name);}atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(atomic_load(&sequence_count)==0);
 for(int i=0;i<30;i++){char name[30];snprintf(name,sizeof(name),"extra%02d.lua",i);snprintf(path,sizeof(path),"%s/%s",root,name);assert(!unlink(path));}
 for(int i=0;i<257;i++){char name[30];snprintf(name,sizeof(name),"ignored%03d.txt",i);touch_file(root,name);}
 atomic_store(&sequence_count,0);assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);assert(atomic_load(&sequence_count)==0);
 puts("Autorun bounded scan, ordering, errors, ownership and cancellation PASS");return 0;}
