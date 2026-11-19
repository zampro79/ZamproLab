#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
static int calls, fail_at, fail_close, fail_alloc, fail_print;
static struct dirent *fault_readdir(DIR *d);
static int fault_closedir(DIR *d);
#define cJSON_CreateObject fault_create_object
#define cJSON_PrintUnformatted fault_print_json
#define readdir fault_readdir
#define closedir fault_closedir
#define main preexisting_mock_main
#include "autorun_files_host.c"
#undef main
#undef cJSON_CreateObject
#undef cJSON_PrintUnformatted
#undef readdir
#undef closedir
extern cJSON *cJSON_CreateObject(void);
extern char *cJSON_PrintUnformatted(const cJSON *);
cJSON *fault_create_object(void){return fail_alloc?NULL:cJSON_CreateObject();}
char *fault_print_json(const cJSON *o){return fail_print?NULL:cJSON_PrintUnformatted(o);}
static struct dirent *fault_readdir(DIR *d){ if(++calls==fail_at){errno=EIO;return NULL;} return readdir(d); }
static int fault_closedir(DIR *d){int rc=closedir(d);if(fail_close){errno=EIO;return -1;}return rc;}
int main(void){
 strcpy(root,"/tmp/zam-list-error-XXXXXX");assert(mkdtemp(root));
 char path[256];snprintf(path,sizeof(path),"%s/a.lua",root);int a=open(path,O_CREAT|O_WRONLY,0600);assert(a>=0);close(a);
 snprintf(path,sizeof(path),"%s/b.lua",root);int b=open(path,O_CREAT|O_WRONLY,0600);assert(b>=0);close(b);
 for(int mode=0;mode<7;mode++){calls=0;fail_at=mode==0?1:mode==1?4:0;fail_close=mode==2;fail_alloc=mode==3;fail_print=mode==4;inject_send=mode==5;httpd_req_t r=request(NULL,NULL,NULL);r.query="path=%2F";esp_err_t rc=list_handler(&r);
 printf("injected_readdir_errno=EIO http_status=%s return=%d\n",r.status?r.status:"200 default",rc);
 if(mode<5){assert(rc==ESP_FAIL && r.status && !strncmp(r.status,"500",3));assert(!strstr(r.response,"entries"));}else if(mode==5){assert(rc==ESP_FAIL && !r.status);assert(!cJSON_Parse(r.response));}else{assert(rc==ESP_OK && !r.status);assert(strstr(r.response,"entries"));} assert(!zam_files_lock());zam_files_unlock();} inject_send=0;httpd_req_t missing=request(NULL,NULL,NULL);missing.query="path=%2Fmissing";list_handler(&missing);assert(missing.status&&!strncmp(missing.status,"404",3));char empty[256];snprintf(empty,sizeof(empty),"%s/empty",root);assert(!mkdir(empty,0700));httpd_req_t er=request(NULL,NULL,NULL);er.query="path=%2Fempty";assert(list_handler(&er)==ESP_OK);assert(strstr(er.response,"\"entries\":[]"));return 0;
}
