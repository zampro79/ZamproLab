#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "zam_output_ring.h"
#include "cJSON.h"
#include "../components/zam_http/zam_http_output.c"
static zam_output_ring_t ring;
size_t httpd_req_get_url_query_len(httpd_req_t *r){return strlen(r->body);}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *s,size_t n){if(strlen(r->body)>=n)return ESP_FAIL;strcpy(s,r->body);return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=s;return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *s,const char *v){(void)r;(void)s;(void)v;return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u){(void)h;(void)u;return ESP_OK;}
uint32_t esp_random(void){return 42;}
esp_err_t zam_program_get_status(zam_program_status_t *s){(void)s;return ESP_OK;}
esp_err_t zam_program_output_read(uint64_t c,void *d,size_t n,zam_program_output_t *o){zam_output_snapshot_t x;if(!zam_output_copy(&ring,c,d,n,&x))return ESP_FAIL;*o=(zam_program_output_t){x.run_id,x.oldest,x.end,x.next,x.lost,x.copied,x.exhausted};return ESP_OK;}
static httpd_req_t request(const char *q){httpd_req_t r={.body=q};assert(output_handler(&r)==ESP_OK);return r;}
static void bad(const char *q,const char *status){httpd_req_t r=request(q);assert(r.status && !strcmp(r.status,status));}
static void *concurrent(void *unused){(void)unused;for(int i=0;i<1000;i++){httpd_req_t r=request("boot_id=42&execution_id=1&cursor=0&limit=256");assert(!r.status);cJSON *j=cJSON_Parse(r.response);assert(j);assert(!strcmp(cJSON_GetObjectItem(j,"lost")->valuestring,"952"));cJSON_Delete(j);}return NULL;}
int main(void){output_boot=42;
 bad("","400 Bad Request");bad("execution_id=0&cursor=0","400 Bad Request");bad("boot_id=42&execution_id=x&cursor=0","400 Bad Request");bad("boot_id=42&execution_id=0&cursor=01","400 Bad Request");bad("boot_id=42&execution_id=0&cursor=0&limit=257","400 Bad Request");bad("boot_id=42&boot_id=42&execution_id=0&cursor=0","400 Bad Request");bad("boot_id=42&execution_id=0&cursor=1","409 Conflict");
 httpd_req_t empty=request("boot_id=0&execution_id=0&cursor=0");cJSON *j=cJSON_Parse(empty.response);assert(j && !strcmp(cJSON_GetObjectItem(j,"data")->valuestring,""));cJSON_Delete(j);
 zam_output_begin(&ring);unsigned char input[3000];for(size_t i=0;i<sizeof(input);i++)input[i]=(unsigned char)i;zam_output_write(&ring,input,sizeof(input));
 httpd_req_t r=request("boot_id=42&execution_id=0&cursor=0&limit=256");j=cJSON_Parse(r.response);assert(j);assert(cJSON_IsTrue(cJSON_GetObjectItem(j,"changed")));assert(!strcmp(cJSON_GetObjectItem(j,"lost")->valuestring,"952"));assert(!strcmp(cJSON_GetObjectItem(j,"cursor")->valuestring,"952"));assert(!strcmp(cJSON_GetObjectItem(j,"next_cursor")->valuestring,"1208"));
 unsigned char decoded[300];size_t len=0;const char *b64=cJSON_GetObjectItem(j,"data")->valuestring;assert(!mbedtls_base64_decode(decoded,sizeof(decoded),&len,(const unsigned char *)b64,strlen(b64)));assert(len==256 && !memcmp(decoded,input+952,len));cJSON_Delete(j);
 zam_output_ring_t before=ring;pthread_t a,b;assert(!pthread_create(&a,NULL,concurrent,NULL));assert(!pthread_create(&b,NULL,concurrent,NULL));pthread_join(a,NULL);pthread_join(b,NULL);assert(!memcmp(&before,&ring,sizeof(ring)));
 r=request("boot_id=2&execution_id=0&cursor=999999");j=cJSON_Parse(r.response);assert(j && cJSON_IsTrue(cJSON_GetObjectItem(j,"reset")));cJSON_Delete(j);puts("HTTP output host tests passed");}
