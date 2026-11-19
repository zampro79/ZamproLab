#include "zam_files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "zam_storage.h"
static SemaphoreHandle_t gate;
int zam_files_init(void) { if (!gate) gate=xSemaphoreCreateMutex(); if (!gate) { errno=ENOMEM; return -1; } return 0; }
int zam_files_lock(void) { if (!gate || xSemaphoreTake(gate,0)!=pdTRUE) { errno=EBUSY;return -1; } return 0; }
void zam_files_unlock(void) { xSemaphoreGive(gate); }
int zam_files_validate(const char *p,bool root_ok,char *out,size_t cap)
{
 if (!p || !out || p[0]!='/' || strlen(p)>ZAM_FILES_PATH_MAX || (!root_ok && !strcmp(p,"/"))) {errno=EINVAL;return -1;}
 if (!strcmp(p,"/web") || !strncmp(p,"/web/",5) || !strcmp(p,"/user") || !strncmp(p,"/user/",6)) {errno=EINVAL;return -1;}
 const char *part=p+1; size_t n=0;
 for (const unsigned char *s=(const unsigned char *)part;;s++) {
  unsigned char c=*s;
  if (!c || c=='/') {
   if ((!n && c) || n>63 || (n==1 && part[0]=='.') || (n>=2 && part[0]=='.' && part[1]=='.') || (n>=4 && !strncmp(part,".zam",4))) {errno=EINVAL;return -1;}
   if (!c) break;
   part=(const char *)s+1;n=0;continue;
  }
  if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')) {errno=EINVAL;return -1;}
  n++;
 }
 if (strlen(p)>1 && p[strlen(p)-1]=='/') {errno=EINVAL;return -1;}
 if (snprintf(out,cap,"/user%s",!strcmp(p,"/")?"":p)>=(int)cap) {errno=ENAMETOOLONG;return -1;}
 return 0;
}
int zam_files_stat(const char *p,struct stat *st) { char path[256];if(zam_files_validate(p,true,path,sizeof(path)))return -1;return stat(path,st); }
int zam_files_open_read(const char *p,struct stat *st) { char path[256]; if(zam_files_validate(p,false,path,sizeof(path)))return -1; if(stat(path,st))return -1; if(!S_ISREG(st->st_mode)){errno=EISDIR;return -1;} return open(path,O_RDONLY); }
DIR *zam_files_open_dir(const char *p) { char path[256]; if(zam_files_validate(p,true,path,sizeof(path)))return NULL;return opendir(path); }
int zam_files_mkdir(const char *p) { char path[256];if(zam_files_validate(p,false,path,sizeof(path)))return -1;return mkdir(path,0770); }
int zam_files_delete(const char *p) { char path[256];struct stat st;if(zam_files_validate(p,false,path,sizeof(path)))return -1;if(stat(path,&st))return -1;return S_ISDIR(st.st_mode)?rmdir(path):unlink(path); }
int zam_files_rename(const char *from,const char *to) {
 char a[256],b[256];struct stat st;if(zam_files_validate(from,false,a,sizeof(a))||zam_files_validate(to,false,b,sizeof(b)))return -1;
 size_t n=strlen(a);if(!strncmp(a,b,n)&&b[n]=='/'){errno=EINVAL;return -1;}
 if(!stat(b,&st)){errno=EEXIST;return -1;}if(errno!=ENOENT)return -1;return rename(a,b);
}
int zam_files_upload_begin(zam_files_upload_t *u,const char *p,size_t length) {
 memset(u,0,sizeof(*u));u->fd=-1;if(length>ZAM_FILES_LIMIT){errno=EFBIG;return -1;}
 if(zam_files_validate(p,false,u->target,sizeof(u->target)))return -1;
 zam_storage_info_t info;if(zam_storage_get_info(ZAM_STORAGE_USER,&info)!=ESP_OK || info.state!=ZAM_STORAGE_MOUNTED){errno=EIO;return -1;}
 if(length+8192>info.free_bytes){errno=ENOSPC;return -1;}
 struct stat st;if(!stat(u->target,&st)){if(!S_ISREG(st.st_mode)){errno=EISDIR;return -1;}u->existed=true;}else if(errno!=ENOENT)return -1;
 for(int i=0;i<8;i++){snprintf(u->temp,sizeof(u->temp),"/user/.zamupload-%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());u->fd=open(u->temp,O_WRONLY|O_CREAT|O_EXCL,0600);if(u->fd>=0){u->active=true;return 0;}if(errno!=EEXIST)return -1;}
 errno=EEXIST;return -1;
}
int zam_files_upload_write(zam_files_upload_t *u,const void *data,size_t n){const char *p=data;while(n){ssize_t written=write(u->fd,p,n);if(written<=0)return -1;p+=written;n-=written;}return 0;}
void zam_files_upload_abort(zam_files_upload_t *u){int saved=errno;if(u->fd>=0){close(u->fd);u->fd=-1;}if(u->active){unlink(u->temp);u->active=false;}errno=saved;}
int zam_files_upload_finish(zam_files_upload_t *u){if(fsync(u->fd)){zam_files_upload_abort(u);return -1;}if(close(u->fd)){u->fd=-1;zam_files_upload_abort(u);return -1;}u->fd=-1;if(rename(u->temp,u->target)){zam_files_upload_abort(u);return -1;}u->active=false;return 0;}
