#define _GNU_SOURCE
#define main autorun_original_main
#include "autorun_files_host.c"
#undef main
#include <signal.h>
static void *delete_thread(void *p){delete_handler(p);return NULL;}
static void *put_thread(void *p){content_put(p);return NULL;}
static void *rename_thread(void *p){json_operation(p);return NULL;}
static void wait_flag(atomic_int *p){for(int i=0;i<3000&&!atomic_load(p);i++)usleep(1000);assert(atomic_load(p));}
static void tag_of(const char *path,char tag[67]){struct stat st;int fd=zam_files_open_read(path,&st);assert(fd>=0);assert(!file_etag(fd,tag));close(fd);}
static int status(httpd_req_t *r,int n){return r->status&&atoi(r->status)==n;}
static httpd_req_t del(const char *tag){httpd_req_t r=request(NULL,tag,NULL);r.query="path=%2Ftest.lua";return r;}
static httpd_req_t put(const char *tag,const char *body){httpd_req_t r=request(body,tag,NULL);r.query="path=%2Ftest.lua";return r;}
static httpd_req_t ren(const char *from,const char *to){static char b[512];snprintf(b,sizeof(b),"{\"from\":\"%s\",\"to\":\"%s\"}",from,to);httpd_req_t r=request(b,NULL,NULL);r.user_ctx=(void*)1;return r;}
static void no_temp(void){char p[256];snprintf(p,sizeof(p),"%s/.temp",root);assert(access(p,F_OK)<0&&errno==ENOENT);}
int main(void){alarm(20);strcpy(root,"/tmp/zam-race-XXXXXX");assert(mkdtemp(root));char tag[67];struct stat st;pthread_t thread;
 for(int reverse=0;reverse<2;reverse++){
  write_source("old");tag_of("/test.lua",tag);httpd_req_t a=del(tag),b=del(tag);httpd_req_t *first=reverse?&b:&a,*second=reverse?&a:&b;
  atomic_store(&hold_delete,1);atomic_store(&delete_entered,0);assert(!pthread_create(&thread,NULL,delete_thread,first));wait_flag(&delete_entered);delete_handler(second);assert(status(second,409));atomic_store(&hold_delete,0);pthread_join(thread,NULL);assert(!first->status);*second=del(tag);delete_handler(second);assert(status(second,412));assert(zam_files_stat("/test.lua",&st)<0);no_temp();
 }
 write_source("old");tag_of("/test.lua",tag);httpd_req_t p=put(tag,"new"),d=del(tag);atomic_store(&hold_write,1);atomic_store(&write_entered,0);assert(!pthread_create(&thread,NULL,put_thread,&p));wait_flag(&write_entered);delete_handler(&d);assert(status(&d,409));atomic_store(&hold_write,0);pthread_join(thread,NULL);assert(p.status&&atoi(p.status)==200);d=del(tag);delete_handler(&d);assert(status(&d,412));char next[67];tag_of("/test.lua",next);assert(strcmp(tag,next));assert(!strcmp(next,p.etag));int check=zam_files_open_read("/test.lua",&st);char data[4]={0};assert(check>=0&&read(check,data,3)==3&&!strcmp(data,"new"));close(check);no_temp();
 write_source("old");tag_of("/test.lua",tag);d=del(tag);atomic_store(&hold_delete,1);atomic_store(&delete_entered,0);assert(!pthread_create(&thread,NULL,delete_thread,&d));wait_flag(&delete_entered);p=put(tag,"new");content_put(&p);assert(status(&p,409));atomic_store(&hold_delete,0);pthread_join(thread,NULL);assert(!d.status);p=put(tag,"new");content_put(&p);assert(status(&p,412));no_temp();
 write_source("old");tag_of("/test.lua",tag);d=del(tag);atomic_store(&hold_delete,1);atomic_store(&delete_entered,0);assert(!pthread_create(&thread,NULL,delete_thread,&d));wait_flag(&delete_entered);httpd_req_t r=ren("/test.lua","/moved.lua");json_operation(&r);assert(status(&r,409));atomic_store(&hold_delete,0);pthread_join(thread,NULL);assert(!d.status);r=ren("/test.lua","/moved.lua");json_operation(&r);assert(status(&r,404));no_temp();
 write_source("old");tag_of("/test.lua",tag);r=ren("/test.lua","/moved.lua");atomic_store(&hold_rename,1);atomic_store(&rename_entered,0);assert(!pthread_create(&thread,NULL,rename_thread,&r));wait_flag(&rename_entered);d=del(tag);delete_handler(&d);assert(status(&d,409));atomic_store(&hold_rename,0);pthread_join(thread,NULL);assert(!r.status||atoi(r.status)==200);d=del(tag);delete_handler(&d);assert(status(&d,412));assert(zam_files_stat("/moved.lua",&st)==0);write_source("different");r=ren("/test.lua","/moved.lua");json_operation(&r);assert(status(&r,409));assert(zam_files_stat("/test.lua",&st)==0);assert(zam_files_stat("/moved.lua",&st)==0);no_temp();
 /* An identical-content replacement has the same ETag: identity is not tracked. */
 write_source("same");tag_of("/test.lua",tag);assert(zam_files_delete("/test.lua")==0);write_source("same");d=del(tag);assert(delete_handler(&d)==ESP_OK);assert(zam_files_stat("/test.lua",&st)<0);
 /* Autorun reservation blocks direct DELETE and PUT; it is released after STOP. */
 assert(zam_files_mkdir("/autorun")==0);char ap[256];snprintf(ap,sizeof(ap),"%s/autorun/active.lua",root);int af=open(ap,O_CREAT|O_WRONLY,0600);assert(af>=0);assert(write(af,"old",3)==3);close(af);tag_of("/autorun/active.lua",tag);
 atomic_store(&occupied,1);d=request(NULL,tag,NULL);d.query="path=%2Fautorun%2Factive.lua";delete_handler(&d);assert(status(&d,409));p=request("new",tag,NULL);p.query="path=%2Fautorun%2Factive.lua";content_put(&p);assert(status(&p,409));httpd_req_t copy=request("{\"source\":\"/test.lua\"}",tag,NULL);autorun_copy(&copy);assert(status(&copy,409));atomic_store(&occupied,0);
 d=request(NULL,tag,NULL);d.query="path=%2Fautorun%2Factive.lua";assert(delete_handler(&d)==ESP_OK);assert(zam_files_stat("/autorun/active.lua",&st)<0);no_temp();
 puts("DELETE/DELETE, DELETE/PUT and DELETE/rename with nonblocking gate: PASS");return 0;
}
