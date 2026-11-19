#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_http_server.h"
#include "zam_wifi.h"
static const char *header(httpd_req_t *r,const char *k){if(!strcasecmp(k,"Host"))return r->host;if(!strcasecmp(k,"Origin"))return r->origin;if(!strcasecmp(k,"Content-Type"))return r->type;return NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *k){const char *s=header(r,k);return s?strlen(s):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *k,char *out,size_t n){const char *s=header(r,k);if(!s||strlen(s)>=n)return ESP_FAIL;strcpy(out,s);return ESP_OK;}
static bool read_fail,time_out;
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){if(read_fail)return -1;size_t left=r->content_len-r->pos;if(!left)return 0;if(n>left)n=left;if(n>5)n=5;memcpy(out,r->body+r->pos,n);r->pos+=n;return (int)n;}
int httpd_req_to_sockfd(httpd_req_t *r){(void)r;return 4;}
size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t n){if(!r->query||strlen(r->query)>=n)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=s;return 0;}esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *a,const char *b){(void)r;(void)a;(void)b;return 0;}esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return 0;}static int registered;esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *r){(void)h;(void)r;registered++;return 0;}
int64_t esp_timer_get_time(void){static int n;if(time_out)return (int64_t)++n*3000000;return 0;}uint32_t esp_random(void){static uint32_t n=1;return n++;}
static int saves,scan_err,scan_calls;static bool busy;static zam_wifi_operation_t op;static zam_wifi_password_action_t last_action;static bool last_enabled;static char last_pass[64];
esp_err_t zam_wifi_configure_sta(const zam_wifi_sta_request_t *r){if(busy)return ESP_ERR_INVALID_STATE;if(r->enabled&&(!r->ssid||strlen(r->ssid)>32||!strlen(r->ssid)))return ESP_ERR_INVALID_ARG;if(r->enabled&&r->password_action==ZAM_WIFI_PASSWORD_REPLACE&&strlen(r->password)<8)return ESP_ERR_INVALID_ARG;saves++;last_enabled=r->enabled;last_action=r->password_action;if(r->password)snprintf(last_pass,sizeof(last_pass),"%s",r->password);op=(zam_wifi_operation_t){.id=r->id,.accepted=true,.persistence=ZAM_WIFI_PERSIST_PENDING,.application=ZAM_WIFI_APPLY_PENDING,.connection=ZAM_WIFI_CONNECTION_NA};return 0;}
esp_err_t zam_wifi_operation(uint32_t id,zam_wifi_operation_t *o){if(id!=op.id)return ESP_ERR_NOT_FOUND;*o=op;return 0;}
esp_err_t zam_wifi_scan_start(uint32_t *id){scan_calls++;if(busy)return ESP_ERR_INVALID_STATE;*id=7;return 0;}
esp_err_t zam_wifi_scan_get(uint32_t id,zam_wifi_scan_item_t *o,size_t cap,size_t *count,bool *pending){if(id!=7)return ESP_ERR_NOT_FOUND;*pending=false;*count=cap<3?cap:3;memset(o,0,*count*sizeof(*o));if(*count>0){strcpy(o[0].ssid,"open");o[0].auth=0;}if(*count>1)o[1].ssid[0]=(char)0xff;if(*count>2){o[2].ssid[0]=0;o[2].auth=3;}return scan_err;}
static const char *local_ip="192.168.4.1";
static int local_family=AF_INET,local_port=80,short_address,failed_getsockname,native_v6;
static int fake_getsockname(int fd,struct sockaddr *s,socklen_t *n){
 (void)fd;if(failed_getsockname)return -1;struct sockaddr_storage storage={0};size_t size;
 if(local_family==AF_INET){struct sockaddr_in *a=(struct sockaddr_in *)&storage;a->sin_family=AF_INET;a->sin_port=htons(local_port);inet_pton(AF_INET,local_ip,&a->sin_addr);size=sizeof(*a);}
 else {struct sockaddr_in6 *a=(struct sockaddr_in6 *)&storage;a->sin6_family=AF_INET6;a->sin6_port=htons(local_port);char text[64];snprintf(text,sizeof(text),native_v6?"2001:db8::1":"::ffff:%s",local_ip);inet_pton(AF_INET6,text,&a->sin6_addr);size=sizeof(*a);}
 if(short_address)size=2;
 if(size>*n)size=*n;
 memcpy(s,&storage,size);*n=(socklen_t)size;return 0;
}
static int fake_getsockopt(int fd,int l,int o,void *v,socklen_t *n){(void)fd;(void)l;(void)o;memset(v,0,*n);return 0;}
static int fake_setsockopt(int fd,int l,int o,const void *v,socklen_t n){(void)fd;(void)l;(void)o;(void)v;(void)n;return 0;}
#define getsockname fake_getsockname
#define getsockopt fake_getsockopt
#define setsockopt fake_setsockopt
#include "../components/zam_http/zam_http_wifi.c"
#undef getsockname
#undef getsockopt
#undef setsockopt
static httpd_req_t request(const char *body){return (httpd_req_t){.body=body,.content_len=body?strlen(body):0,.host="192.168.4.1",.origin="http://192.168.4.1",.type="application/json"};}
static void check(httpd_req_t *r,int status){assert(r->status&&atoi(r->status)==status);assert(cJSON_Parse(r->response));assert(!strstr(r->response,"secretpass"));}
static void origin_matrix(void){
 struct test_case {const char *host,*origin;int port;bool accepted;} cases[]={
  {"192.168.4.1","http://192.168.4.1",80,true},
  {"192.168.4.1:80","http://192.168.4.1",80,true},
  {"192.168.4.1","http://192.168.4.1:80",80,true},
  {"192.168.4.1:80","http://192.168.4.1:80",80,true},
  {"192.168.4.1:8080","http://192.168.4.1:8080",8080,true},
  {"192.168.4.2","http://192.168.4.1",80,false},
  {"192.168.4.1","http://192.168.4.2",80,false},
  {"192.168.4.1:81","http://192.168.4.1",80,false},
  {"192.168.4.1","http://192.168.4.1:81",80,false},
  {"192.168.4.1","https://192.168.4.1",80,false},
  {NULL,"http://192.168.4.1",80,false},
  {"192.168.4.1",NULL,80,false},
  {"192.168.4.1:8080","http://192.168.4.1",8080,false},
  {"192.168.4.1","http://192.168.4.1:8080",8080,false},
  {"192.168.4.1.evil","http://192.168.4.1.evil",80,false},
 };
 char body[160];snprintf(body,sizeof(body),"{\"boot_id\":\"%s\",\"operation_id\":1,\"enabled\":false}",boot_id);
 for(int family=0;family<2;family++)for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  local_family=family?AF_INET6:AF_INET;local_port=cases[i].port;
  httpd_req_t r=request(NULL);r.host=cases[i].host;r.origin=cases[i].origin;
  int before=scan_calls;scan_start_handler(&r);check(&r,cases[i].accepted?202:403);assert(scan_calls==before+(int)cases[i].accepted);
  r=request(body);r.host=cases[i].host;r.origin=cases[i].origin;
  before=saves;next_id=1;settings_handler(&r);check(&r,cases[i].accepted?202:403);assert(saves==before+(int)cases[i].accepted);
 }
 local_family=AF_INET6;local_port=80;native_v6=1;
 httpd_req_t r=request(NULL);int before=scan_calls;scan_start_handler(&r);check(&r,403);assert(scan_calls==before);
 r=request(body);before=saves;next_id=1;settings_handler(&r);check(&r,403);assert(saves==before);native_v6=0;
 for(int family=0;family<2;family++){local_family=family?AF_INET6:AF_INET;short_address=1;r=request(NULL);before=scan_calls;scan_start_handler(&r);check(&r,403);assert(scan_calls==before);r=request(body);before=saves;settings_handler(&r);check(&r,403);assert(saves==before);short_address=0;}
 failed_getsockname=1;r=request(NULL);before=scan_calls;scan_start_handler(&r);check(&r,403);assert(scan_calls==before);r=request(body);before=saves;settings_handler(&r);check(&r,403);assert(saves==before);failed_getsockname=0;
 local_family=AF_INET;local_port=80;saves=0;scan_calls=0;next_id=1;last_id=0;memset(&op,0,sizeof(op));
}
int main(void){assert(zam_http_register_wifi((void*)1)==0&&registered==4);origin_matrix();char good[512];snprintf(good,sizeof(good),"{\"boot_id\":\"%s\",\"operation_id\":1,\"enabled\":true,\"ssid\":\"abc\",\"password_action\":\"replace\",\"password\":\"secretpass\",\"confirm_sta_only\":false}",boot_id);
 httpd_req_t r=request(good);r.origin="http://attacker";settings_handler(&r);check(&r,403);r=request(good);r.host="attacker";settings_handler(&r);check(&r,403);r=request(good);r.origin=NULL;settings_handler(&r);check(&r,403);
 local_ip="10.0.0.8";r=request("{}");r.host="10.0.0.8";r.origin="http://10.0.0.8";settings_handler(&r);check(&r,400);local_ip="192.168.4.1";
 r=request("{\"boot_id\":\"old-boot\",\"operation_id\":1,\"enabled\":false}");settings_handler(&r);check(&r,409);
 r=request("{}");settings_handler(&r);check(&r,400);r=request(good);r.type="text/plain";settings_handler(&r);check(&r,415);r=request(good);r.content_len=513;settings_handler(&r);check(&r,413);r=request(good);read_fail=true;settings_handler(&r);check(&r,400);read_fail=false;r=request(good);time_out=true;settings_handler(&r);check(&r,408);time_out=false;
 r=request(good);settings_handler(&r);check(&r,202);assert(saves==1&&last_enabled&&last_action==ZAM_WIFI_PASSWORD_REPLACE&&!strcmp(last_pass,"secretpass"));r=request(good);settings_handler(&r);check(&r,409);assert(saves==1);
 char body[512];snprintf(body,sizeof(body),"{\"boot_id\":\"%s\",\"operation_id\":2,\"enabled\":true,\"ssid\":\"abc\",\"password_action\":\"keep\"}",boot_id);r=request(body);busy=true;settings_handler(&r);check(&r,409);busy=false;r=request(body);settings_handler(&r);check(&r,202);assert(last_action==ZAM_WIFI_PASSWORD_KEEP);
 snprintf(body,sizeof(body),"{\"boot_id\":\"%s\",\"operation_id\":3,\"enabled\":true,\"ssid\":\"abc\",\"password_action\":\"open\"}",boot_id);r=request(body);settings_handler(&r);check(&r,202);assert(last_action==ZAM_WIFI_PASSWORD_OPEN);
 snprintf(body,sizeof(body),"{\"boot_id\":\"%s\",\"operation_id\":4,\"enabled\":false}",boot_id);r=request(body);settings_handler(&r);check(&r,202);assert(!last_enabled);r=request(body);settings_handler(&r);check(&r,409);
 snprintf(body,sizeof(body),"{\"boot_id\":\"%s\",\"operation_id\":5,\"enabled\":false,\"enabled\":true}",boot_id);r=request(body);settings_handler(&r);check(&r,400);r=request("{bad}");settings_handler(&r);check(&r,400);r=request("{\"password\":\"x\\u0000y\"}");settings_handler(&r);check(&r,400);
 r=request(NULL);r.query="id=4";operation_handler(&r);check(&r,200);assert(strstr(r.response,"pending"));op.persistence=ZAM_WIFI_PERSIST_FAILED;op.application=ZAM_WIFI_APPLY_NOT_ATTEMPTED;r=request(NULL);r.query="id=4";operation_handler(&r);check(&r,200);assert(strstr(r.response,"failed")&&strstr(r.response,"not_attempted"));op.persistence=ZAM_WIFI_PERSIST_SAVED;op.application=ZAM_WIFI_APPLY_FAILED;r=request(NULL);r.query="id=4";operation_handler(&r);check(&r,200);assert(strstr(r.response,"saved")&&strstr(r.response,"failed"));r=request(NULL);r.query="id=3";operation_handler(&r);check(&r,404);
 r=request(NULL);scan_start_handler(&r);check(&r,202);r=request(NULL);r.query="id=7";scan_get_handler(&r);check(&r,200);assert(strstr(r.response,"Non-UTF-8")&&strstr(r.response,"\"hidden\":true"));scan_err=ESP_ERR_TIMEOUT;r=request(NULL);r.query="id=7";scan_get_handler(&r);check(&r,200);assert(strstr(r.response,"263"));r=request(NULL);r.query="id=8";scan_get_handler(&r);check(&r,404);busy=true;r=request(NULL);scan_start_handler(&r);check(&r,409);
 puts("Wi-Fi HTTP host tests PASS");return 0;}
