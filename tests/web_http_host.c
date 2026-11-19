#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "zam_storage.h"
esp_err_t zam_http_web_root(httpd_req_t*);
static size_t len, announced, sent, calls, fails;static unsigned opens,closes,terminators;static int opening,closefail;
esp_err_t zam_storage_web_open(const char *p,FILE **f,size_t *n){assert(!strcmp(p,"/web/index.html"));if(opening)return opening;*f=tmpfile();assert(*f);opens++;for(size_t i=0;i<len;i++)assert(fputc((unsigned char)i,*f)!=EOF);rewind(*f);*n=announced;return 0;}
esp_err_t zam_storage_web_close(FILE *f){closes++;int ret=fclose(f);return ret || closefail ? ESP_FAIL : ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;assert(!strcmp(s,"text/html; charset=utf-8"));return 0;}
esp_err_t httpd_resp_set_status(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
esp_err_t httpd_resp_sendstr(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
esp_err_t httpd_resp_send_err(httpd_req_t*r,int n,const char*s){(void)r;(void)s;assert(n==404);return 0;}
esp_err_t httpd_resp_send_chunk(httpd_req_t*r,const char*b,ssize_t n){(void)r;if(!b){assert(n==0);terminators++;return 0;}assert(n>0 && n<=2048);if(fails && ++calls>=fails)return ESP_FAIL;for(ssize_t i=0;i<n;i++)assert((unsigned char)b[i]==(unsigned char)(sent+i));sent+=(size_t)n;return 0;}
static void clear(void){len=announced=sent=calls=fails=0;opens=closes=terminators=0;opening=closefail=0;}
int main(void){httpd_req_t r={0};size_t sizes[]={0,1,16384,16385,32768,65535,65536};for(size_t i=0;i<7;i++){clear();len=announced=sizes[i];assert(zam_http_web_root(&r)==0);assert(sent==len && opens==1 && closes==1 && terminators==1);}clear();len=100;announced=200;assert(zam_http_web_root(&r)==ESP_FAIL);assert(opens==1 && closes==1 && !terminators);clear();len=announced=65536;fails=3;assert(zam_http_web_root(&r)==ESP_FAIL);assert(opens==1 && closes==1 && !terminators);clear();len=announced=1;closefail=1;assert(zam_http_web_root(&r)==ESP_FAIL);assert(opens==1 && closes==1 && !terminators);clear();opening=ESP_ERR_NOT_FOUND;assert(!zam_http_web_root(&r));assert(!opens && !closes);puts("HTTP stream, status, close and abort: PASS");}
