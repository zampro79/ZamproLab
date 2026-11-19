#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "zam_files.h"
#include "mbedtls/sha256.h"
#ifdef ZAM_EXPERIMENTAL_LUA
#include "zam_program.h"
#endif
static esp_err_t fail(httpd_req_t *r,int e) {
 const char *status="500 Internal Server Error";
 if(e==EINVAL||e==ENAMETOOLONG)status="400 Bad Request";
 else if(e==ENOENT)status="404 Not Found";
 else if(e==EEXIST||e==EBUSY||e==ENOTEMPTY||e==EISDIR||e==ENOTDIR||e==E2BIG)status="409 Conflict";
 else if(e==EFBIG)status="413 Payload Too Large";
 else if(e==ENOSPC)status="507 Insufficient Storage";
 httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Connection","close");httpd_resp_sendstr(r,"{\"error\":\"file operation failed\"}");return ESP_FAIL;
}
static int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int query_path(httpd_req_t *r,char *out,size_t cap) {
 char q[ZAM_FILES_PATH_MAX*3+16];size_t n=httpd_req_get_url_query_len(r);if(!n||n>=sizeof(q)||httpd_req_get_url_query_str(r,q,sizeof(q))!=ESP_OK){errno=EINVAL;return -1;}
 if(strncmp(q,"path=",5)||strchr(q,'&')){errno=EINVAL;return -1;}size_t j=0;
 for(size_t i=5;q[i];i++){unsigned char c=q[i];if(c=='%'){if(!q[i+1]||!q[i+2]){errno=EINVAL;return -1;}int a=hex(q[++i]),b=hex(q[++i]);if(a<0||b<0){errno=EINVAL;return -1;}c=(unsigned char)((a<<4)|b);}if(!c||j+1>=cap){errno=EINVAL;return -1;}out[j++]=(char)c;}
 out[j]=0;return 0;
}
static esp_err_t json_send(httpd_req_t *r,cJSON *o){char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!s)return fail(r,ENOMEM);httpd_resp_set_type(r,"application/json");esp_err_t e=httpd_resp_sendstr(r,s);free(s);return e;}
static cJSON *body_json(httpd_req_t *r){if(r->content_len<=0||r->content_len>512){errno=EINVAL;return NULL;}char b[513];int n=0;while(n<r->content_len){int got=httpd_req_recv(r,b+n,r->content_len-n);if(got<=0){errno=EINVAL;return NULL;}n+=got;}if(memchr(b,0,n)){errno=EINVAL;return NULL;}b[n]=0; if(strstr(b,"\\u") || strstr(b,"\\U")){errno=EINVAL;return NULL;}const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(b,&end,1);if(!o)errno=EINVAL;return o;}
static esp_err_t list_handler(httpd_req_t *r) {
 char path[ZAM_FILES_PATH_MAX+1];if(query_path(r,path,sizeof(path)))return fail(r,errno);if(zam_files_lock())return fail(r,errno);
 DIR *d=zam_files_open_dir(path);if(!d){int e=errno;zam_files_unlock();return fail(r,e);}cJSON *o=cJSON_CreateObject(),*array=o?cJSON_AddArrayToObject(o,"entries"):NULL;bool ok=array && cJSON_AddStringToObject(o,"path",path);struct dirent *entry;unsigned count=0;
 while(ok){errno=0;entry=readdir(d);if(!entry){if(errno)ok=false;break;}if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||!strncmp(entry->d_name,".zam",4))continue;
  char child[ZAM_FILES_PATH_MAX+1];int n=snprintf(child,sizeof(child),"%s%s%s",path,strcmp(path,"/")?"/":"",entry->d_name);if(n<0||(size_t)n>=sizeof(child)){errno=ENAMETOOLONG;ok=false;break;}
  struct stat st;if(zam_files_stat(child,&st)){ok=false;break;}if(++count>128){errno=EFBIG;ok=false;break;}cJSON *item=cJSON_CreateObject();if(!item){ok=false;break;}
  ok=cJSON_AddStringToObject(item,"name",entry->d_name)&&cJSON_AddStringToObject(item,"type",S_ISDIR(st.st_mode)?"directory":"file")&&(S_ISDIR(st.st_mode)||cJSON_AddNumberToObject(item,"size",st.st_size));if(ok)ok=cJSON_AddItemToArray(array,item);if(!ok)cJSON_Delete(item);
 }
 int error=ok?0:errno;if(closedir(d) && !error){error=errno?errno:EIO;ok=false;}zam_files_unlock();if(!ok){cJSON_Delete(o);return fail(r,error?error:ENOMEM);}return json_send(r,o);
}
/* Caller holds zam_files lock. Hash and rewind the same open file. */
static int file_etag(int fd, char tag[67]) {
 mbedtls_sha256_context ctx; unsigned char digest[32],buf[1024]; ssize_t n;
 mbedtls_sha256_init(&ctx);
 if(mbedtls_sha256_starts(&ctx,0)!=0){mbedtls_sha256_free(&ctx);errno=EIO;return -1;}
 while((n=read(fd,buf,sizeof(buf)))>0) if(mbedtls_sha256_update(&ctx,buf,n)!=0){mbedtls_sha256_free(&ctx);errno=EIO;return -1;}
 if(n<0 || mbedtls_sha256_finish(&ctx,digest)!=0 || lseek(fd,0,SEEK_SET)<0){mbedtls_sha256_free(&ctx);errno=EIO;return -1;}
 mbedtls_sha256_free(&ctx);tag[0]='"';for(int i=0;i<32;i++)snprintf(tag+1+i*2,3,"%02x",digest[i]);tag[65]='"';tag[66]=0;return 0;
}
static esp_err_t precondition_failed(httpd_req_t *r){httpd_resp_set_status(r,"412 Precondition Failed");httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Connection","close");httpd_resp_sendstr(r,"{\"error\":\"precondition failed\"}");return ESP_FAIL;}
static esp_err_t precondition_required(httpd_req_t *r){httpd_resp_set_status(r,"428 Precondition Required");httpd_resp_set_type(r,"application/json");httpd_resp_sendstr(r,"{\"error\":\"precondition required\"}");return ESP_FAIL;}
static esp_err_t content_get(httpd_req_t *r){char path[ZAM_FILES_PATH_MAX+1];if(query_path(r,path,sizeof(path)))return fail(r,errno);if(zam_files_lock())return fail(r,errno);struct stat st;int fd=zam_files_open_read(path,&st);if(fd<0){int e=errno;zam_files_unlock();return fail(r,e);}char etag[67];if(file_etag(fd,etag)){int e=errno;close(fd);zam_files_unlock();return fail(r,e);}httpd_resp_set_hdr(r,"ETag",etag);httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_type(r,"application/octet-stream");char b[2048];ssize_t n;esp_err_t err=ESP_OK;while((n=read(fd,b,sizeof(b)))>0){err=httpd_resp_send_chunk(r,b,n);if(err!=ESP_OK)break;}if(n<0)err=ESP_FAIL;close(fd);zam_files_unlock();if(err==ESP_OK)err=httpd_resp_send_chunk(r,NULL,0);return err;}
static esp_err_t content_put_inner(httpd_req_t *r){char path[ZAM_FILES_PATH_MAX+1];if(query_path(r,path,sizeof(path)))return fail(r,errno);if(!httpd_req_get_hdr_value_len(r,"Content-Length"))return fail(r,EINVAL);
 char match[80]={0},none[8]={0};size_t ml=httpd_req_get_hdr_value_len(r,"If-Match"),nl=httpd_req_get_hdr_value_len(r,"If-None-Match");
 if((ml && nl)||ml>=sizeof(match)||nl>=sizeof(none))return fail(r,EINVAL);
 if((ml && httpd_req_get_hdr_value_str(r,"If-Match",match,sizeof(match))!=ESP_OK)||(nl && httpd_req_get_hdr_value_str(r,"If-None-Match",none,sizeof(none))!=ESP_OK))return fail(r,EINVAL);
 if(nl && strcmp(none,"*"))return fail(r,EINVAL);
 if(ml){if(ml!=66||match[0]!='"'||match[65]!='"')return fail(r,EINVAL);for(int i=1;i<65;i++)if(!((match[i]>='0'&&match[i]<='9')||(match[i]>='a'&&match[i]<='f')))return fail(r,EINVAL);}
 if(zam_files_lock())return fail(r,errno);
 if(ml||nl){struct stat st;int fd=zam_files_open_read(path,&st);if(fd<0){int e=errno;if(e==ENOENT && ml){zam_files_unlock();return precondition_failed(r);}if(e!=ENOENT){zam_files_unlock();return fail(r,e);}}else {if(nl){close(fd);zam_files_unlock();return precondition_failed(r);}char current[67];if(file_etag(fd,current)){int e=errno;close(fd);zam_files_unlock();return fail(r,e);}close(fd);if(strcmp(match,current)){zam_files_unlock();return precondition_failed(r);}}}
 zam_files_upload_t u;if(zam_files_upload_begin(&u,path,r->content_len)){int e=errno;zam_files_unlock();return fail(r,e);}char b[1024];size_t left=r->content_len;int e=0;while(left){size_t want=left<sizeof(b)?left:sizeof(b);int n=httpd_req_recv(r,b,want);if(n<=0){e=EINVAL;break;}if(zam_files_upload_write(&u,b,n)){e=errno;break;}left-=n;}if(!e && zam_files_upload_finish(&u))e=errno;if(e)zam_files_upload_abort(&u);bool existed=u.existed;char result_tag[67]={0};if(!e){struct stat st;int saved=zam_files_open_read(path,&st);if(saved>=0){if(file_etag(saved,result_tag))result_tag[0]=0;close(saved);}}zam_files_unlock();if(e)return fail(r,e);if(result_tag[0])httpd_resp_set_hdr(r,"ETag",result_tag);httpd_resp_set_status(r,existed?"200 OK":"201 Created");httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");}
/* Only Autorun paths reserve against the Program Manager. No critical lock
 * is held while a request, filesystem operation or network transfer runs. */
static bool autorun_path(const char *p){return !strcmp(p,"/autorun") || !strncmp(p,"/autorun/",9);}
static int reserve_mutation(bool needed){
#ifdef ZAM_EXPERIMENTAL_LUA
 if(needed && zam_program_autorun_mutation_begin()!=ESP_OK){errno=EBUSY;return -1;}
#else
 (void)needed;
#endif
 return 0;
}
static void release_mutation(bool needed){
#ifdef ZAM_EXPERIMENTAL_LUA
 if(needed)zam_program_autorun_mutation_end();
#else
 (void)needed;
#endif
}
static esp_err_t content_put(httpd_req_t *r){char p[ZAM_FILES_PATH_MAX+1];if(query_path(r,p,sizeof(p)))return fail(r,errno);bool reserve=autorun_path(p);if(reserve_mutation(reserve))return fail(r,errno);esp_err_t rc=content_put_inner(r);release_mutation(reserve);return rc;}
static esp_err_t delete_handler(httpd_req_t *r){
 char p[ZAM_FILES_PATH_MAX+1],match[80]={0};if(query_path(r,p,sizeof(p)))return fail(r,errno);
 size_t ml=httpd_req_get_hdr_value_len(r,"If-Match");
 if(ml){if(ml!=66 || ml>=sizeof(match) || httpd_req_get_hdr_value_str(r,"If-Match",match,sizeof(match))!=ESP_OK || match[0]!='"' || match[65]!='"')return fail(r,EINVAL);for(int i=1;i<65;i++)if(!((match[i]>='0'&&match[i]<='9')||(match[i]>='a'&&match[i]<='f')))return fail(r,EINVAL);}
 bool reserve=autorun_path(p);if(reserve_mutation(reserve))return fail(r,errno);
 if(zam_files_lock()){int e=errno;release_mutation(reserve);return fail(r,e);}
 if(!ml){struct stat info;if(zam_files_stat(p,&info)){int e=errno;zam_files_unlock();release_mutation(reserve);return fail(r,e);}if(S_ISREG(info.st_mode)){zam_files_unlock();release_mutation(reserve);return precondition_required(r);}}
 if(ml){struct stat st;int fd=zam_files_open_read(p,&st);if(fd<0){int e=errno;zam_files_unlock();release_mutation(reserve);return e==ENOENT?precondition_failed(r):fail(r,e);}char tag[67];if(file_etag(fd,tag)){int e=errno;close(fd);zam_files_unlock();release_mutation(reserve);return fail(r,e);}close(fd);if(strcmp(match,tag)){zam_files_unlock();release_mutation(reserve);return precondition_failed(r);}}
 int rc=zam_files_delete(p),e=errno;zam_files_unlock();release_mutation(reserve);if(rc)return fail(r,e);httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
static esp_err_t json_operation(httpd_req_t *r){
 cJSON *o=body_json(r);if(!o)return fail(r,errno);bool rename_op=r->user_ctx!=NULL;
 cJSON *a=cJSON_GetObjectItemCaseSensitive(o,rename_op?"from":"path"),*b=rename_op?cJSON_GetObjectItemCaseSensitive(o,"to"):NULL;
 if(!cJSON_IsString(a)||(rename_op&&!cJSON_IsString(b))){cJSON_Delete(o);return fail(r,EINVAL);}
 bool reserve=autorun_path(a->valuestring)||(rename_op&&autorun_path(b->valuestring));
 if(reserve_mutation(reserve)){int e=errno;cJSON_Delete(o);return fail(r,e);}
 if(zam_files_lock()){int e=errno;release_mutation(reserve);cJSON_Delete(o);return fail(r,e);}
 int rc=rename_op?zam_files_rename(a->valuestring,b->valuestring):zam_files_mkdir(a->valuestring);int e=errno;
 zam_files_unlock();release_mutation(reserve);cJSON_Delete(o);if(rc)return fail(r,e);httpd_resp_set_status(r,rename_op?"200 OK":"201 Created");httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
#ifdef ZAM_EXPERIMENTAL_LUA
/* Copy a stable UserFS snapshot into /autorun. The source and destination are
 * checked and copied while the existing filesystem gate is held. */
static esp_err_t autorun_copy(httpd_req_t *r){
 cJSON *o=body_json(r);if(!o)return fail(r,errno);
 cJSON *source=cJSON_GetObjectItemCaseSensitive(o,"source");
 if(!cJSON_IsString(source)||!source->valuestring){cJSON_Delete(o);return fail(r,EINVAL);}
 const char *p=source->valuestring,*name=strrchr(p,'/');char physical[256],dest[ZAM_FILES_PATH_MAX+1];
 if(zam_files_validate(p,false,physical,sizeof(physical))||autorun_path(p)||!name||strlen(++name)<5||strcmp(name+strlen(name)-4,".lua")||snprintf(dest,sizeof(dest),"/autorun/%s",name)>=(int)sizeof(dest)){cJSON_Delete(o);return fail(r,EINVAL);}
 char match[80]={0},none[8]={0};size_t ml=httpd_req_get_hdr_value_len(r,"If-Match"),nl=httpd_req_get_hdr_value_len(r,"If-None-Match");
 if((ml&&nl)||(!ml&&!nl)||ml>=sizeof(match)||nl>=sizeof(none)||(ml&&httpd_req_get_hdr_value_str(r,"If-Match",match,sizeof(match))!=ESP_OK)||(nl&&httpd_req_get_hdr_value_str(r,"If-None-Match",none,sizeof(none))!=ESP_OK)||(nl&&strcmp(none,"*"))){cJSON_Delete(o);return fail(r,EINVAL);}
 if(ml){if(ml!=66||match[0]!='"'||match[65]!='"'){cJSON_Delete(o);return fail(r,EINVAL);}for(int i=1;i<65;i++)if(!((match[i]>='0'&&match[i]<='9')||(match[i]>='a'&&match[i]<='f'))){cJSON_Delete(o);return fail(r,EINVAL);}}
 if(reserve_mutation(true)){int e=errno;cJSON_Delete(o);return fail(r,e);}
 if(zam_files_lock()){int e=errno;release_mutation(true);cJSON_Delete(o);return fail(r,e);}
 int e=0,fd=-1;bool precondition=false;struct stat st;char source_tag[67],dest_tag[67];zam_files_upload_t up={.fd=-1};
 fd=zam_files_open_read(p,&st);if(fd<0){e=errno;goto done;}
 if(st.st_size<0 || st.st_size>CONFIG_ZAM_LUA_SOURCE_LIMIT){e=EFBIG;goto done;}
 if(file_etag(fd,source_tag)){e=errno;goto done;}
 if(zam_files_stat("/autorun",&st)){if(errno!=ENOENT||zam_files_mkdir("/autorun")){e=errno;goto done;}}
 else if(!S_ISDIR(st.st_mode)){e=ENOTDIR;goto done;}
 int prior=zam_files_open_read(dest,&st);
 if(prior<0){
  if(errno!=ENOENT){e=errno;goto done;}if(ml){precondition=true;goto done;}
  /* Never add an entry that makes the next bounded Autorun scan fail. */
  DIR *dir=zam_files_open_dir("/autorun");if(!dir){e=errno;goto done;}
  unsigned entries=0,scripts=0;struct dirent *ent;
  for(;;){errno=0;ent=readdir(dir);if(!ent){if(errno)e=errno;break;}if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,".."))continue;
   size_t n=strlen(ent->d_name);entries++;if(n>ZAM_AUTORUN_NAME_MAX||ent->d_type==DT_UNKNOWN){e=EINVAL;break;}
   if(ent->d_type==DT_REG&&n>=4&&!strcmp(ent->d_name+n-4,".lua"))scripts++;
  }
  if(closedir(dir)&&!e)e=errno;
  if(!e&&(entries>=ZAM_AUTORUN_MAX_ENTRIES||scripts>=ZAM_AUTORUN_MAX_SCRIPTS))e=E2BIG;
  if(e)goto done;
 }
 else {if(nl){close(prior);precondition=true;goto done;}if(file_etag(prior,dest_tag)){e=errno;close(prior);goto done;}close(prior);if(strcmp(match,dest_tag)){precondition=true;goto done;}}
 if(fstat(fd,&st)){e=errno;goto done;}
 if(zam_files_upload_begin(&up,dest,(size_t)st.st_size)){e=errno;goto done;}
 {char buf[1024];ssize_t n;size_t copied=0;while((n=read(fd,buf,sizeof(buf)))>0){if(zam_files_upload_write(&up,buf,(size_t)n)){e=errno;break;}copied+=(size_t)n;}if(n<0)e=errno;if(!e && copied!=(size_t)st.st_size)e=EIO;}
 if(e){zam_files_upload_abort(&up);goto done;}
 if(zam_files_upload_finish(&up)){e=errno;goto done;}
 done:
 if(fd>=0){close(fd);}
 zam_files_unlock();release_mutation(true);cJSON_Delete(o);
 if(precondition)return precondition_failed(r);
 if(e)return fail(r,e);
 httpd_resp_set_hdr(r,"ETag",source_tag);httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
#endif
esp_err_t zam_http_register_files(httpd_handle_t server){static const httpd_uri_t routes[]={
 {.uri="/api/files",.method=HTTP_GET,.handler=list_handler}, {.uri="/api/files",.method=HTTP_DELETE,.handler=delete_handler},
 {.uri="/api/files/content",.method=HTTP_GET,.handler=content_get}, {.uri="/api/files/content",.method=HTTP_PUT,.handler=content_put},
 {.uri="/api/files/mkdir",.method=HTTP_POST,.handler=json_operation}, {.uri="/api/files/rename",.method=HTTP_POST,.handler=json_operation,.user_ctx=(void *)1}};
 for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t e=httpd_register_uri_handler(server,&routes[i]);if(e!=ESP_OK)return e;}
#ifdef ZAM_EXPERIMENTAL_LUA
 static const httpd_uri_t copy={.uri="/api/autorun/copy",.method=HTTP_POST,.handler=autorun_copy};return httpd_register_uri_handler(server,&copy);
#else
 return ESP_OK;
#endif
}
