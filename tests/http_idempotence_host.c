#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "zam_program.h"
#include "cJSON.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static zam_program_state_t state=ZAM_PROGRAM_IDLE;
static zam_program_result_t mock_result=ZAM_PROGRAM_MEMORY_ERROR;
static int unavailable,run_calls,force_error,read_error;
static size_t registered;
static int timeout_mode;static int timer_calls;static int64_t fake_time;
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *h){(void)h;return r->content_type?strlen(r->content_type):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *h,char *out,size_t n){(void)h;snprintf(out,n,"%s",r->content_type);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){if(read_error)return -1;size_t left=r->content_len-r->position;if(n>left)n=left;if(n>7)n=7;memcpy(out,r->body+r->position,n);r->position+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=s;return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *a,const char *b){(void)r;(void)a;(void)b;return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *r){(void)h;(void)r;registered++;return ESP_OK;}
int64_t esp_timer_get_time(void){if(fake_time)return fake_time;if(timeout_mode){timer_calls++;return timeout_mode==2 && timer_calls<3?0:(int64_t)timer_calls*2100000;}struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
esp_err_t zam_program_run(const char *path){if(!path || path[0]!='/' || strstr(path,"..") || !strncmp(path,"/web/",5))return ESP_ERR_INVALID_ARG;pthread_mutex_lock(&lock);run_calls++;esp_err_t e=force_error?ESP_FAIL:unavailable||state!=ZAM_PROGRAM_IDLE?ESP_ERR_INVALID_STATE:ESP_OK;if(e==ESP_OK)state=ZAM_PROGRAM_STARTING;pthread_mutex_unlock(&lock);return e;}
esp_err_t zam_program_stop(void){pthread_mutex_lock(&lock);esp_err_t e=unavailable||state==ZAM_PROGRAM_IDLE||state==ZAM_PROGRAM_CLEANUP?ESP_ERR_INVALID_STATE:ESP_OK;if(e==ESP_OK)state=ZAM_PROGRAM_STOPPING;pthread_mutex_unlock(&lock);return e;}
esp_err_t zam_program_get_status(zam_program_status_t *s){pthread_mutex_lock(&lock);if(unavailable){pthread_mutex_unlock(&lock);return ESP_ERR_INVALID_STATE;}memset(s,0,sizeof(*s));s->state=state;s->last_result=mock_result;s->lua_peak=123;s->output_produced=99;s->output_dropped=11;pthread_mutex_unlock(&lock);return ESP_OK;}
esp_err_t zam_program_autorun_status(zam_autorun_status_t *s){memset(s,0,sizeof(*s));s->state=ZAM_AUTORUN_IDLE;return ESP_OK;}
#include "../components/zam_http/zam_http_program.c"
static httpd_req_t request(const char *body,const char *type){httpd_req_t r={0};r.body=body;r.content_len=body?strlen(body):0;r.content_type=type;return r;}
static void code(httpd_req_t *r,const char *prefix){assert(r->status && !strncmp(r->status,prefix,3));cJSON *o=cJSON_Parse(r->response);assert(o);cJSON_Delete(o);}
static pthread_barrier_t barrier;static int outcomes[2];static int same_id;
static const char *id1="00000000-0000-4000-8000-000000000001";
static const char *id2="00000000-0000-4000-8000-000000000002";
static httpd_req_t runreq(const char *path,const char *id){char body[512];snprintf(body,sizeof(body),"{\"path\":\"%s\",\"request_id\":\"%s\"}",path,id);httpd_req_t r=request(strdup(body),"application/json");run_handler(&r);free((void *)r.body);return r;}
static void clear_registry(void){memset(run_entries,0,sizeof(run_entries));state=ZAM_PROGRAM_IDLE;unavailable=0;force_error=0;run_calls=0;fake_time=1000000;}
static void *race(void *arg){int i=(int)(intptr_t)arg;pthread_barrier_wait(&barrier);httpd_req_t r=runreq("/loop.lua",same_id?id1:(i?id2:id1));outcomes[i]=atoi(r.status);return NULL;}
int main(void){
 clear_registry();unavailable=1;assert(zam_http_register_program((void *)1)==ESP_OK && registered==0);unavailable=0;assert(zam_http_register_program((void *)1)==ESP_OK && registered==3);
 httpd_req_t r=request(NULL,"application/json");run_handler(&r);code(&r,"400");
 char huge[514];memset(huge,'x',513);huge[513]=0;r=request(huge,"application/json");run_handler(&r);code(&r,"413");
 r=request("{}","text/plain");run_handler(&r);code(&r,"415");
 const char *bad[]={"{}","{\"path\":\"/a\"}","{\"path\":2,\"request_id\":\"x\"}","{\"path\":\"/a\",\"request_id\":\"x\"}","{\"path\":\"/a\",\"path\":\"/b\"}","{bad}"};
 for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++){r=request(bad[i],"application/json");run_handler(&r);code(&r,"400");}
 r=runreq("/hello.lua","00000000-0000-4000-8000-000000000001x");code(&r,"400");
 r=runreq("/hello.lua",id1);code(&r,"202");assert(run_calls==1);
 for(int st=ZAM_PROGRAM_STARTING;st<=ZAM_PROGRAM_CLEANUP;st++){state=(zam_program_state_t)st;r=runreq("/hello.lua",id1);code(&r,"202");assert(strstr(r.response,"\"duplicate\":true") && run_calls==1);}
 state=ZAM_PROGRAM_IDLE;r=runreq("/hello.lua",id1);code(&r,"202");assert(run_calls==1);
 r=runreq("/other.lua",id1);code(&r,"409");assert(run_calls==1);
 r=runreq("/../bad",id2);code(&r,"400");assert(run_calls==1);r=runreq("/../bad",id2);code(&r,"400");assert(run_calls==1);
 clear_registry();force_error=1;r=runreq("/x.lua",id1);code(&r,"500");force_error=0;r=runreq("/x.lua",id1);code(&r,"500");assert(run_calls==1);
 clear_registry();run_outcome_t prev;assert(reserve_run(id1,"/x.lua",fake_time,&prev)>=0);r=runreq("/x.lua",id1);code(&r,"409");assert(run_calls==0);
 clear_registry();for(int i=0;i<RUN_IDS_CAPACITY;i++){char id[37];snprintf(id,sizeof(id),"00000000-0000-4000-8000-%012x",i);r=runreq("/x.lua",id);code(&r,i?"409":"202");}
 r=runreq("/new.lua","ffffffff-ffff-4fff-8fff-ffffffffffff");code(&r,"503");assert(run_calls==RUN_IDS_CAPACITY);fake_time+=RUN_ID_TTL_US;state=ZAM_PROGRAM_IDLE;r=runreq("/new.lua","ffffffff-ffff-4fff-8fff-ffffffffffff");code(&r,"202");assert(run_calls==RUN_IDS_CAPACITY+1);
 for(same_id=0;same_id<=1;same_id++){clear_registry();pthread_t a,b;pthread_barrier_init(&barrier,NULL,3);pthread_create(&a,NULL,race,(void *)0);pthread_create(&b,NULL,race,(void *)1);pthread_barrier_wait(&barrier);pthread_join(a,NULL);pthread_join(b,NULL);assert(run_calls==(same_id?1:2));assert((outcomes[0]==202||outcomes[1]==202));pthread_barrier_destroy(&barrier);}
 clear_registry();r=runreq("/x.lua",id1);code(&r,"202");clear_registry();r=runreq("/x.lua",id1);code(&r,"202");assert(run_calls==1);
 r=request(NULL,NULL);status_handler(&r);code(&r,"200");r=request(NULL,NULL);stop_handler(&r);code(&r,"202");
 printf("HTTP idempotence: duplicates, conflict, in-flight, capacity, TTL, errors and concurrency PASS; registry %zu bytes\n",sizeof(run_entries));return 0;
}
