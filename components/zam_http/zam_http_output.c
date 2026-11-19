#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_http_server.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "zam_program.h"

#define OUTPUT_LIMIT 256
static uint64_t output_boot;

typedef struct {uint64_t boot,run,cursor;size_t limit;} output_query_t;
static bool decimal(const char *s,size_t n,uint64_t *out)
{
    if(!n || n>20 || (n>1 && *s=='0'))return false;
    uint64_t value=0;
    for(size_t i=0;i<n;i++){
        if(s[i]<'0'||s[i]>'9')return false;
        unsigned d=(unsigned)(s[i]-'0');if(value>(UINT64_MAX-d)/10)return false;
        value=value*10+d;
    }
    *out=value;return true;
}
static bool parse_query(const char *q,output_query_t *out)
{
    unsigned seen=0;uint64_t v;output_query_t x={.limit=OUTPUT_LIMIT};
    while(*q){
        const char *end=strchr(q,'&');if(!end)end=q+strlen(q);
        const char *eq=memchr(q,'=',(size_t)(end-q));if(!eq || eq==q || !decimal(eq+1,(size_t)(end-eq-1),&v))return false;
        size_t n=(size_t)(eq-q);unsigned bit;
        if(n==7&&!memcmp(q,"boot_id",7)){bit=1;x.boot=v;}
        else if(n==12&&!memcmp(q,"execution_id",12)){bit=2;x.run=v;}
        else if(n==6&&!memcmp(q,"cursor",6)){bit=4;x.cursor=v;}
        else if(n==5&&!memcmp(q,"limit",5)){bit=8;if(!v||v>OUTPUT_LIMIT)return false;x.limit=(size_t)v;}
        else return false;
        if(seen&bit)return false;
        seen|=bit;
        if(!*end)break;
        q=end+1;if(!*q)return false;
    }
    if((seen&7)!=7)return false;
    *out=x;return true;
}
static esp_err_t error_response(httpd_req_t *r,const char *status,const char *message)
{
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,message);
}
static esp_err_t output_handler(httpd_req_t *r)
{
    if(r->content_len)return error_response(r,"400 Bad Request","{\"error\":\"unexpected body\"}");
    size_t n=httpd_req_get_url_query_len(r);char query[128];output_query_t q;
    if(!n || n>=sizeof(query) || httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK || !parse_query(query,&q))
        return error_response(r,"400 Bad Request","{\"error\":\"invalid query\"}");
    zam_program_output_t meta,read;
    if(zam_program_output_read(0,NULL,0,&meta)!=ESP_OK)return error_response(r,"503 Service Unavailable","{\"error\":\"runtime unavailable\"}");
    bool reset=q.boot!=0 && q.boot!=output_boot;
    uint64_t cursor=(q.boot==0 || reset)?meta.oldest:q.cursor;
    if(cursor>meta.end)return error_response(r,"409 Conflict","{\"error\":\"cursor future\"}");
    unsigned char raw[OUTPUT_LIMIT],encoded[4*((OUTPUT_LIMIT+2)/3)+1];size_t encoded_len=0;
    if(zam_program_output_read(cursor,raw,q.limit,&read)!=ESP_OK)return error_response(r,"503 Service Unavailable","{\"error\":\"snapshot unavailable\"}");
    if(mbedtls_base64_encode(encoded,sizeof(encoded),&encoded_len,raw,read.copied)!=0)return error_response(r,"500 Internal Server Error","{\"error\":\"encoding failed\"}");
    encoded[encoded_len]=0;
    char body[800];int length=snprintf(body,sizeof(body),
      "{\"boot_id\":\"%" PRIu64 "\",\"execution_id\":\"%" PRIu64 "\",\"cursor\":\"%" PRIu64 "\",\"next_cursor\":\"%" PRIu64 "\",\"lost\":\"%" PRIu64 "\",\"changed\":%s,\"reset\":%s,\"exhausted\":%s,\"encoding\":\"base64\",\"data\":\"%s\"}",
      output_boot,read.run_id,read.next-read.copied,read.next,read.lost,(q.boot==output_boot && q.run!=read.run_id)?"true":"false",reset?"true":"false",read.exhausted?"true":"false",(char *)encoded);
    if(length<0 || (size_t)length>=sizeof(body))return error_response(r,"500 Internal Server Error","{\"error\":\"response too large\"}");
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,body);
}
esp_err_t zam_http_register_output(httpd_handle_t server)
{
    zam_program_status_t state;if(zam_program_get_status(&state)!=ESP_OK)return ESP_OK;
    output_boot=((uint64_t)esp_random()<<32)|esp_random();if(!output_boot)output_boot=1;
    const httpd_uri_t route={.uri="/api/program/output",.method=HTTP_GET,.handler=output_handler};
    return httpd_register_uri_handler(server,&route);
}
