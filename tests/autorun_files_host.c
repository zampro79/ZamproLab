#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "zam_files.h"
static char root[128];static atomic_int occupied,hold_write,write_entered,hold_delete,delete_entered,hold_rename,rename_entered;static pthread_mutex_t files_gate=PTHREAD_MUTEX_INITIALIZER;static int inject_write,inject_space,inject_delete,inject_read,inject_send;
#include "../components/zam_http/zam_http_files.c"
size_t httpd_req_get_url_query_len(httpd_req_t*r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t*r,char*b,size_t n){snprintf(b,n,"%s",r->query);return ESP_OK;}
static const char*hdr(httpd_req_t*r,const char*n){return !strcmp(n,"If-Match")?r->match:!strcmp(n,"If-None-Match")?r->none:NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t*r,const char*n){if(!strcmp(n,"Content-Length"))return r->body?1:0;const char*x=hdr(r,n);return x?strlen(x):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*r,const char*n,char*b,size_t z){const char*x=hdr(r,n);if(!x)return ESP_FAIL;snprintf(b,z,"%s",x);return ESP_OK;}
int httpd_req_recv(httpd_req_t*r,char*b,size_t n){size_t left=r->content_len-r->position;if(n>left)n=left;memcpy(b,r->body+r->position,n);r->position+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=s;return 0;}
esp_err_t httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
esp_err_t httpd_resp_set_hdr(httpd_req_t*r,const char*n,const char*v){if(!strcmp(n,"ETag"))snprintf(r->etag,sizeof(r->etag),"%s",v);return 0;}
esp_err_t httpd_resp_sendstr(httpd_req_t*r,const char*s){if(inject_send){snprintf(r->response,sizeof(r->response),"%.8s",s);return ESP_FAIL;}snprintf(r->response,sizeof(r->response),"%s",s);return 0;}
esp_err_t httpd_resp_send_chunk(httpd_req_t*r,const char*s,size_t n){(void)r;(void)s;(void)n;return 0;}
esp_err_t httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t*u){(void)s;(void)u;return 0;}
esp_err_t zam_program_autorun_mutation_begin(void){return atomic_exchange(&occupied,1)?ESP_ERR_INVALID_STATE:ESP_OK;}
void zam_program_autorun_mutation_end(void){assert(atomic_exchange(&occupied,0));}
int zam_files_lock(void){if(pthread_mutex_trylock(&files_gate)){errno=EBUSY;return -1;}return 0;}void zam_files_unlock(void){assert(!pthread_mutex_unlock(&files_gate));}
int zam_files_validate(const char*p,bool root_ok,char*out,size_t n){(void)root_ok;if(!p||*p!='/'||strstr(p,"..")||strchr(p,' ')||strlen(p)>192){errno=EINVAL;return -1;}snprintf(out,n,"%s%s",root,p);return 0;}
int zam_files_stat(const char*p,struct stat*s){char x[256];return zam_files_validate(p,true,x,sizeof(x))?-1:stat(x,s);}
int zam_files_open_read(const char*p,struct stat*s){char x[256];if(inject_read&&!strcmp(p,"/test.lua")){errno=EIO;return -1;}if(zam_files_validate(p,false,x,sizeof(x))||stat(x,s))return -1;if(!S_ISREG(s->st_mode)){errno=EISDIR;return -1;}return open(x,O_RDONLY);}
DIR*zam_files_open_dir(const char*p){char x[256];return zam_files_validate(p,true,x,sizeof(x))?NULL:opendir(x);}
int zam_files_mkdir(const char*p){char x[256];return zam_files_validate(p,false,x,sizeof(x))?-1:mkdir(x,0700);}
int zam_files_delete(const char*p){char x[256];struct stat st;if(atomic_load(&hold_delete)){atomic_store(&delete_entered,1);while(atomic_load(&hold_delete))usleep(1000);}if(inject_delete){errno=EIO;return -1;}if(zam_files_validate(p,false,x,sizeof(x))||stat(x,&st))return -1;return S_ISDIR(st.st_mode)?rmdir(x):unlink(x);}
int zam_files_rename(const char*a,const char*b){char x[256],y[256];if(atomic_load(&hold_rename)){atomic_store(&rename_entered,1);while(atomic_load(&hold_rename))usleep(1000);}if(zam_files_validate(a,false,x,sizeof(x))||zam_files_validate(b,false,y,sizeof(y)))return -1;struct stat st;if(!stat(y,&st)){errno=EEXIST;return -1;}if(errno!=ENOENT)return -1;return rename(x,y);}
int zam_files_upload_begin(zam_files_upload_t*u,const char*p,size_t n){(void)n;if(inject_space){errno=ENOSPC;return -1;}if(zam_files_validate(p,false,u->target,sizeof(u->target)))return -1;struct stat existing;u->existed=stat(u->target,&existing)==0;snprintf(u->temp,sizeof(u->temp),"%s/.temp",root);u->fd=open(u->temp,O_CREAT|O_EXCL|O_WRONLY,0600);u->active=u->fd>=0;return u->active?0:-1;}
int zam_files_upload_write(zam_files_upload_t*u,const void*p,size_t n){if(atomic_load(&hold_write)){atomic_store(&write_entered,1);while(atomic_load(&hold_write))usleep(1000);}if(inject_write){errno=EIO;return -1;}return write(u->fd,p,n)==(ssize_t)n?0:-1;}
void zam_files_upload_abort(zam_files_upload_t*u){if(u->active){close(u->fd);unlink(u->temp);u->active=false;}}
int zam_files_upload_finish(zam_files_upload_t*u){close(u->fd);u->active=false;return rename(u->temp,u->target);}
static httpd_req_t request(const char*body,const char*match,const char*none){return (httpd_req_t){.body=body,.content_len=body?strlen(body):0,.match=match,.none=none};}
static void*copy_thread(void*arg){autorun_copy((httpd_req_t*)arg);return NULL;}
static void write_source(const char*s){char x[256];snprintf(x,sizeof(x),"%s/test.lua",root);FILE*f=fopen(x,"wb");assert(f);fputs(s,f);fclose(f);}
int main(void){strcpy(root,"/tmp/zam-autorun-api-XXXXXX");assert(mkdtemp(root));write_source("return 1\n");
 httpd_req_t r=request("{\"source\":\"/test.lua\"}",NULL,"*");assert(autorun_copy(&r)==ESP_OK);assert(!strcmp(r.response,"{\"ok\":true}")&&strlen(r.etag)==66);char old[80];strcpy(old,r.etag);assert(!occupied);
 r=request("{\"source\":\"/test.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"412",3));
 write_source("return 2\n");inject_write=1;r=request("{\"source\":\"/test.lua\"}",old,NULL);autorun_copy(&r);assert(r.status&&!strncmp(r.status,"500",3));inject_write=0;struct stat st;int fd=zam_files_open_read("/autorun/test.lua",&st);assert(fd>=0);char tag[67];assert(!file_etag(fd,tag));close(fd);assert(!strcmp(tag,old));
 inject_space=1;r=request("{\"source\":\"/test.lua\"}",old,NULL);autorun_copy(&r);assert(r.status&&!strncmp(r.status,"507",3));inject_space=0;
 r=request("{\"source\":\"/test.lua\"}",old,NULL);assert(autorun_copy(&r)==ESP_OK);assert(strcmp(r.etag,old));char updated[80];strcpy(updated,r.etag);
 occupied=1;r=request("{\"source\":\"/test.lua\"}",updated,NULL);autorun_copy(&r);assert(r.status&&!strncmp(r.status,"409",3));occupied=0;
 r=request(NULL,old,NULL);r.query="path=%2Fautorun%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"412",3));
 inject_delete=1;r=request(NULL,updated,NULL);r.query="path=%2Fautorun%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"500",3));inject_delete=0;
 r=request(NULL,updated,NULL);r.query="path=%2Fautorun%2Ftest.lua";assert(delete_handler(&r)==ESP_OK);assert(zam_files_stat("/autorun/test.lua",&st)<0&&zam_files_stat("/test.lua",&st)==0);
 inject_read=1;r=request("{\"source\":\"/test.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"500",3));inject_read=0;
 atomic_store(&hold_write,1);atomic_store(&write_entered,0);httpd_req_t thread_request=request("{\"source\":\"/test.lua\"}",NULL,"*");pthread_t th;assert(!pthread_create(&th,NULL,copy_thread,&thread_request));for(int k=0;k<1000&&!atomic_load(&write_entered);k++)usleep(1000);assert(atomic_load(&write_entered));r=request("{\"source\":\"/test.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"409",3));r=request(NULL,updated,NULL);r.query="path=%2Fautorun%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"409",3));atomic_store(&hold_write,0);pthread_join(th,NULL);assert(!occupied);assert(zam_files_delete("/autorun/test.lua")==0);
 r=request("{\"source\":\"/missing.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"404",3));
 r=request("{\"source\":\"/../evil.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"400",3));
 r=request("{\"source\":\"/test.txt\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"400",3));
 write_source("return 1\n");r=request("{\"source\":\"/test.lua\"}",NULL,"*");assert(autorun_copy(&r)==ESP_OK);strcpy(updated,r.etag);
 occupied=1;r=request(NULL,updated,NULL);r.query="path=%2Fautorun%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"409",3));occupied=0;
 r=request(NULL,NULL,NULL);r.query="path=%2Fautorun%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"428",3));
 /* Ordinary files require the same conditional delete as Autorun files. */
 r=request(NULL,NULL,NULL);r.query="path=%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"428",3));assert(zam_files_stat("/test.lua",&st)==0);
 fd=zam_files_open_read("/test.lua",&st);assert(fd>=0);assert(!file_etag(fd,tag));close(fd);char original_tag[67];strcpy(original_tag,tag);
 write_source("changed concurrently\n");r=request(NULL,original_tag,NULL);r.query="path=%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"412",3));assert(zam_files_stat("/test.lua",&st)==0);
 fd=zam_files_open_read("/test.lua",&st);assert(fd>=0);assert(!file_etag(fd,tag));close(fd);r=request(NULL,tag,NULL);r.query="path=%2Ftest.lua";assert(delete_handler(&r)==ESP_OK);assert(zam_files_stat("/test.lua",&st)<0);
 r=request(NULL,tag,NULL);r.query="path=%2Ftest.lua";delete_handler(&r);assert(r.status&&!strncmp(r.status,"412",3));write_source("return 1\n");
 char huge[256];snprintf(huge,sizeof(huge),"%s/big.lua",root);int big=open(huge,O_CREAT|O_WRONLY,0600);assert(big>=0);assert(!ftruncate(big,32769));close(big);
 r=request("{\"source\":\"/big.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"413",3));

 char blank[256];snprintf(blank,sizeof(blank),"%s/empty.lua",root);int empty=open(blank,O_CREAT|O_WRONLY,0600);assert(empty>=0);close(empty);
 r=request("{\"source\":\"/empty.lua\"}",NULL,"*");assert(autorun_copy(&r)==ESP_OK);assert(zam_files_stat("/autorun/empty.lua",&st)==0&&st.st_size==0);assert(zam_files_delete("/autorun/empty.lua")==0);
 for(int i=0;i<31;i++){char x[256];snprintf(x,sizeof(x),"%s/autorun/n%02d.lua",root,i);int f=open(x,O_CREAT|O_WRONLY,0600);assert(f>=0);close(f);}
 char fresh[256];snprintf(fresh,sizeof(fresh),"%s/new.lua",root);int f=open(fresh,O_CREAT|O_WRONLY,0600);assert(f>=0);close(f);
 r=request("{\"source\":\"/new.lua\"}",NULL,"*");autorun_copy(&r);assert(r.status&&!strncmp(r.status,"409",3));
 printf("Autorun API mock: create, conflict, replace, failed write/space, busy, stale and conditional delete PASS\n");return 0;}
