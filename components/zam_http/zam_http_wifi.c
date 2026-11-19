#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "cJSON.h"
#include "zam_wifi.h"

#define BODY_MAX 512
#define BODY_DEADLINE_US 2000000LL
static char boot_id[17];
static uint32_t next_id=1, last_id;

static void wipe(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
static esp_err_t fail(httpd_req_t *r,const char *status,const char *code)
{
    char out[128]; snprintf(out,sizeof(out),"{\"error\":\"%s\"}",code);
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Connection","close");
    httpd_resp_sendstr(r,out);return ESP_FAIL;
}
static esp_err_t send_json(httpd_req_t *r,cJSON *o,const char *status)
{
    if(!o)return fail(r,"500 Internal Server Error","out_of_memory");
    char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);
    if(!s)return fail(r,"500 Internal Server Error","out_of_memory");
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");esp_err_t e=httpd_resp_sendstr(r,s);free(s);return e;
}
/* Compare the literal IPv4 Host and Origin with the receiving socket address.
 * DNS names are deliberately unsupported: a hostile name can rebind to us. */
static bool same_origin(httpd_req_t *r)
{
    char host[64],origin[96],ip[INET_ADDRSTRLEN],expected[80];
    size_t h=httpd_req_get_hdr_value_len(r,"Host"),o=httpd_req_get_hdr_value_len(r,"Origin");
    if(!h||h>=sizeof(host)||!o||o>=sizeof(origin))return false;
    if(httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK || httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK)return false;
    struct sockaddr_storage local={0};socklen_t n=sizeof(local);
    if(getsockname(httpd_req_to_sockfd(r),(struct sockaddr *)&local,&n)!=0)return false;
    struct in_addr addr;unsigned port;
    if(local.ss_family==AF_INET){
        if(n<sizeof(struct sockaddr_in))return false;
        const struct sockaddr_in *v4=(const struct sockaddr_in *)&local;
        addr=v4->sin_addr;port=ntohs(v4->sin_port);
#if defined(CONFIG_LWIP_IPV6) && CONFIG_LWIP_IPV6
    }else if(local.ss_family==AF_INET6){
        if(n<sizeof(struct sockaddr_in6))return false;
        const struct sockaddr_in6 *v6=(const struct sockaddr_in6 *)&local;
        static const unsigned char prefix[12]={0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if(memcmp(v6->sin6_addr.s6_addr,prefix,sizeof(prefix))!=0)return false;
        memcpy(&addr,v6->sin6_addr.s6_addr+12,sizeof(addr));port=ntohs(v6->sin6_port);
#endif
    }else return false;
    if(!port || !inet_ntop(AF_INET,&addr,ip,sizeof(ip)))return false;
    snprintf(expected,sizeof(expected),"%s:%u",ip,port);
    if(strcmp(host,expected) && !(port==80 && !strcmp(host,ip)))return false;
    snprintf(expected,sizeof(expected),"http://%s:%u",ip,port);
    if(!strcmp(origin,expected))return true;
    if(port==80){snprintf(expected,sizeof(expected),"http://%s",ip);return !strcmp(origin,expected);}
    return false;
}
static bool json_type(httpd_req_t *r)
{
    char h[96];size_t n=httpd_req_get_hdr_value_len(r,"Content-Type");
    if(!n||n>=sizeof(h)||httpd_req_get_hdr_value_str(r,"Content-Type",h,sizeof(h))!=ESP_OK)return false;
    char *semi=strchr(h,';');if(semi)*semi=0;n=strlen(h);while(n && (h[n-1]==' '||h[n-1]=='\t'))h[--n]=0;
    return !strcasecmp(h,"application/json");
}
static bool utf8(const unsigned char *s)
{
    while(*s){unsigned char c=*s++;if(c<0x80)continue;unsigned n;uint32_t v;
        if(c>=0xc2&&c<=0xdf){n=1;v=c&31;}else if(c>=0xe0&&c<=0xef){n=2;v=c&15;}else if(c>=0xf0&&c<=0xf4){n=3;v=c&7;}else return false;
        for(unsigned i=0;i<n;i++){if(!*s||(*s&0xc0)!=0x80)return false;v=(v<<6)|(*s++&63);}
        if((n==2&&v<0x800)||(n==3&&v<0x10000)||(v>=0xd800&&v<=0xdfff)||v>0x10ffff)return false;
    }return true;
}
static cJSON *read_body(httpd_req_t *r,char body[BODY_MAX+1],const char **status,const char **error)
{
    if(r->content_len<=0){*status="400 Bad Request";*error="body_required";return NULL;}
    if(r->content_len>BODY_MAX){*status="413 Payload Too Large";*error="body_too_large";return NULL;}
    if(!json_type(r)){*status="415 Unsupported Media Type";*error="json_required";return NULL;}
    size_t n=0;int64_t deadline=esp_timer_get_time()+BODY_DEADLINE_US;
    while(n<r->content_len){if(esp_timer_get_time()>=deadline){*status="408 Request Timeout";*error="body_timeout";return NULL;}
        int got=httpd_req_recv(r,body+n,r->content_len-n);
        if(esp_timer_get_time()>=deadline){*status="408 Request Timeout";*error="body_timeout";return NULL;}
        if(got==HTTPD_SOCK_ERR_TIMEOUT)continue;
        if(got<=0){*status="400 Bad Request";*error="incomplete_body";return NULL;}n+=(size_t)got;}
    body[n]=0;
    /* No escaped Unicode: blocks escaped NUL and leaves UTF-8 as sent by browser. */
    if(memchr(body,0,n)||strstr(body,"\\u")||strstr(body,"\\U")||!utf8((unsigned char *)body)){*status="400 Bad Request";*error="invalid_json";return NULL;}
    const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(body,&end,1);
    if(!cJSON_IsObject(o)){cJSON_Delete(o);*status="400 Bad Request";*error="invalid_json";return NULL;}
    return o;
}
static bool keys(cJSON *o,const char *const *allowed,size_t n)
{
    for(cJSON *a=o->child;a;a=a->next){bool found=false;
        for(size_t i=0;i<n;i++)if(a->string&&!strcmp(a->string,allowed[i]))found=true;
        if(!found)return false;
        for(cJSON *b=a->next;b;b=b->next)if(b->string&&!strcmp(a->string,b->string))return false;
    }return true;
}
static bool u32(cJSON *o,const char *key,uint32_t *out)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<1||v->valuedouble>UINT32_MAX||floor(v->valuedouble)!=v->valuedouble)return false;
    *out=(uint32_t)v->valuedouble;return true;
}
static void delete_secrets(cJSON *o)
{
    if(o){for(cJSON *p=o->child;p;p=p->next)if(p->string&&!strcmp(p->string,"password")&&cJSON_IsString(p)&&p->valuestring)wipe(p->valuestring,strlen(p->valuestring));cJSON_Delete(o);}
}
static esp_err_t settings_handler(httpd_req_t *r)
{
    if(!same_origin(r))return fail(r,"403 Forbidden","origin_forbidden");
    char body[BODY_MAX+1]={0};const char *status=NULL,*error=NULL;
    int fd=httpd_req_to_sockfd(r);struct timeval old_timeout,short_timeout={.tv_sec=1,.tv_usec=0};socklen_t optlen=sizeof(old_timeout);
    if(getsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&old_timeout,&optlen)!=0 || setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&short_timeout,sizeof(short_timeout))!=0)
        return fail(r,"503 Service Unavailable","socket_unavailable");
    cJSON *o=read_body(r,body,&status,&error);
    int restored=setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&old_timeout,sizeof(old_timeout));
    if(restored!=0){delete_secrets(o);wipe(body,sizeof(body));return fail(r,"503 Service Unavailable","socket_unavailable");}
    if(!o){wipe(body,sizeof(body));return fail(r,status,error);}
    const char *const allowed[]={"boot_id","operation_id","enabled","ssid","password_action","password","confirm_sta_only"};
    cJSON *boot=cJSON_GetObjectItemCaseSensitive(o,"boot_id"),*enabled=cJSON_GetObjectItemCaseSensitive(o,"enabled"),*confirm=cJSON_GetObjectItemCaseSensitive(o,"confirm_sta_only"),*ssid=cJSON_GetObjectItemCaseSensitive(o,"ssid"),*action=cJSON_GetObjectItemCaseSensitive(o,"password_action"),*pass=cJSON_GetObjectItemCaseSensitive(o,"password");
    uint32_t id=0;zam_wifi_sta_request_t req={0};bool valid=keys(o,allowed,7)&&cJSON_IsString(boot)&&cJSON_IsBool(enabled)&&(!confirm||cJSON_IsBool(confirm))&&u32(o,"operation_id",&id);
    if(valid && strcmp(boot->valuestring,boot_id)){status="409 Conflict";error="boot_changed";valid=false;}
    if(valid && id!=next_id){status="409 Conflict";error="stale_id";valid=false;}
    if(valid){req.id=id;req.enabled=cJSON_IsTrue(enabled);req.confirm_sta_only=confirm&&cJSON_IsTrue(confirm);
        if(req.enabled){valid=cJSON_IsString(ssid)&&cJSON_IsString(action)&&ssid->valuestring&&action->valuestring;
            if(valid){req.ssid=ssid->valuestring;
                if(!strcmp(action->valuestring,"keep")){req.password_action=ZAM_WIFI_PASSWORD_KEEP;valid=!pass;}
                else if(!strcmp(action->valuestring,"open")){req.password_action=ZAM_WIFI_PASSWORD_OPEN;valid=!pass;}
                else if(!strcmp(action->valuestring,"replace")){req.password_action=ZAM_WIFI_PASSWORD_REPLACE;valid=cJSON_IsString(pass)&&pass->valuestring;req.password=valid?pass->valuestring:NULL;}
                else valid=false;}}
        else valid=!ssid&&!action&&!pass;
    }
    esp_err_t e=valid?zam_wifi_configure_sta(&req):ESP_ERR_INVALID_ARG;
    delete_secrets(o);wipe(body,sizeof(body));
    if(!valid)return fail(r,status?status:"400 Bad Request",error?error:"invalid_request");
    if(e==ESP_ERR_INVALID_ARG)return fail(r,"400 Bad Request","invalid_credentials");
    if(e==ESP_ERR_INVALID_STATE)return fail(r,"409 Conflict","wifi_busy_or_confirmation_required");
    if(e!=ESP_OK)return fail(r,"503 Service Unavailable","wifi_unavailable");
    last_id=id;next_id=id==UINT32_MAX?0:id+1;
    cJSON *reply=cJSON_CreateObject();if(!reply||!cJSON_AddBoolToObject(reply,"accepted",true)||!cJSON_AddStringToObject(reply,"boot_id",boot_id)||!cJSON_AddNumberToObject(reply,"operation_id",id)){cJSON_Delete(reply);return fail(r,"500 Internal Server Error","out_of_memory");}
    return send_json(r,reply,"202 Accepted");
}
static const char *persist_name(zam_wifi_persist_t s){switch(s){case ZAM_WIFI_PERSIST_PENDING:return "pending";case ZAM_WIFI_PERSIST_SAVED:return "saved";case ZAM_WIFI_PERSIST_FAILED:return "failed";default:return "unknown";}}
static const char *apply_name(zam_wifi_apply_t s){switch(s){case ZAM_WIFI_APPLY_PENDING:return "pending";case ZAM_WIFI_APPLY_APPLIED:return "applied";case ZAM_WIFI_APPLY_FAILED:return "failed";case ZAM_WIFI_APPLY_NOT_ATTEMPTED:return "not_attempted";default:return "unknown";}}
static const char *connect_name(zam_wifi_connection_t s){switch(s){case ZAM_WIFI_CONNECTION_NA:return "not_applicable";case ZAM_WIFI_CONNECTION_CONNECTING:return "connecting";case ZAM_WIFI_CONNECTION_CONNECTED:return "connected";case ZAM_WIFI_CONNECTION_NOT_CONNECTED:return "not_connected";default:return "unknown";}}
static bool query_id(httpd_req_t *r,uint32_t *id)
{
    char q[32];size_t n=httpd_req_get_url_query_len(r);if(!n||n>=sizeof(q)||httpd_req_get_url_query_str(r,q,sizeof(q))!=ESP_OK||strncmp(q,"id=",3)||!q[3])return false;
    uint64_t v=0;for(char *p=q+3;*p;p++){if(*p<'0'||*p>'9')return false;v=v*10+(*p-'0');if(v>UINT32_MAX)return false;}if(!v)return false;*id=(uint32_t)v;return true;
}
static esp_err_t operation_handler(httpd_req_t *r)
{
    uint32_t id=last_id;bool requested=httpd_req_get_url_query_len(r)>0;
    if(requested&&!query_id(r,&id))return fail(r,"400 Bad Request","invalid_id");
    zam_wifi_operation_t op;bool found=id&&zam_wifi_operation(id,&op)==ESP_OK;
    if(requested&&!found)return fail(r,"404 Not Found","operation_unknown");
    cJSON *o=cJSON_CreateObject();if(!o||!cJSON_AddStringToObject(o,"boot_id",boot_id)||!cJSON_AddNumberToObject(o,"next_id",next_id)||!cJSON_AddBoolToObject(o,"found",found)){cJSON_Delete(o);return fail(r,"500 Internal Server Error","out_of_memory");}
    if(found&&(!cJSON_AddNumberToObject(o,"operation_id",id)||!cJSON_AddBoolToObject(o,"accepted",op.accepted)||!cJSON_AddBoolToObject(o,"superseded",op.superseded)||!cJSON_AddStringToObject(o,"persistence",persist_name(op.persistence))||!cJSON_AddStringToObject(o,"application",apply_name(op.application))||!cJSON_AddStringToObject(o,"connection",connect_name(op.connection))||!cJSON_AddNumberToObject(o,"error",op.error)||!cJSON_AddNumberToObject(o,"disconnect_reason",op.disconnect_reason))){cJSON_Delete(o);return fail(r,"500 Internal Server Error","out_of_memory");}
    return send_json(r,o,"200 OK");
}
static esp_err_t scan_start_handler(httpd_req_t *r)
{
    if(!same_origin(r))return fail(r,"403 Forbidden","origin_forbidden");
    if(r->content_len)return fail(r,"400 Bad Request","body_not_allowed");
    uint32_t id;esp_err_t e=zam_wifi_scan_start(&id);
    if(e==ESP_ERR_INVALID_STATE)return fail(r,"409 Conflict","wifi_busy_or_scanner_unavailable");
    if(e!=ESP_OK)return fail(r,"503 Service Unavailable","scan_unavailable");
    cJSON *o=cJSON_CreateObject();if(!o||!cJSON_AddBoolToObject(o,"accepted",true)||!cJSON_AddStringToObject(o,"boot_id",boot_id)||!cJSON_AddNumberToObject(o,"scan_id",id)){cJSON_Delete(o);return fail(r,"500 Internal Server Error","out_of_memory");}
    return send_json(r,o,"202 Accepted");
}
static esp_err_t scan_get_handler(httpd_req_t *r)
{
    uint32_t id;if(!query_id(r,&id))return fail(r,"400 Bad Request","invalid_id");
    zam_wifi_scan_item_t records[ZAM_WIFI_SCAN_MAX];size_t count=0;bool pending=false;
    esp_err_t e=zam_wifi_scan_get(id,records,ZAM_WIFI_SCAN_MAX,&count,&pending);
    if(e==ESP_ERR_NOT_FOUND)return fail(r,"404 Not Found","scan_unknown");
    if(e==ESP_ERR_INVALID_STATE)return fail(r,"503 Service Unavailable","scan_unavailable");
    cJSON *o=cJSON_CreateObject(),*a=o?cJSON_AddArrayToObject(o,"networks"):NULL;
    if(!a||!cJSON_AddStringToObject(o,"boot_id",boot_id)||!cJSON_AddNumberToObject(o,"scan_id",id)||!cJSON_AddBoolToObject(o,"pending",pending)||!cJSON_AddNumberToObject(o,"error",e)){cJSON_Delete(o);return fail(r,"500 Internal Server Error","out_of_memory");}
    for(size_t i=0;i<count;i++){bool valid=utf8((unsigned char *)records[i].ssid);bool hidden=!records[i].ssid[0];cJSON *v=cJSON_CreateObject();
        if(!v||!cJSON_AddStringToObject(v,"ssid",valid?records[i].ssid:"[Non-UTF-8 SSID]")||!cJSON_AddBoolToObject(v,"selectable",valid&&!hidden)||!cJSON_AddBoolToObject(v,"hidden",hidden)||!cJSON_AddNumberToObject(v,"rssi",records[i].rssi)||!cJSON_AddNumberToObject(v,"auth",records[i].auth)||!cJSON_AddNumberToObject(v,"channel",records[i].channel)){cJSON_Delete(v);cJSON_Delete(o);return fail(r,"500 Internal Server Error","out_of_memory");}cJSON_AddItemToArray(a,v);}
    return send_json(r,o,"200 OK");
}
esp_err_t zam_http_register_wifi(httpd_handle_t server)
{
    if(!boot_id[0])snprintf(boot_id,sizeof(boot_id),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    static const httpd_uri_t routes[]={
        {.uri="/api/wifi/settings",.method=HTTP_POST,.handler=settings_handler},
        {.uri="/api/wifi/operation",.method=HTTP_GET,.handler=operation_handler},
        {.uri="/api/wifi/scan",.method=HTTP_POST,.handler=scan_start_handler},
        {.uri="/api/wifi/scan",.method=HTTP_GET,.handler=scan_get_handler}};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t e=httpd_register_uri_handler(server,&routes[i]);if(e!=ESP_OK)return e;}return ESP_OK;
}
