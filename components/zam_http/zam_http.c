#include "zam_http.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "zam_system.h"
#include "zam_wifi.h"
#include "zam_storage.h"
#include "zam_files.h"
esp_err_t zam_http_register_files(httpd_handle_t server);
esp_err_t zam_http_register_wifi(httpd_handle_t server);
#ifdef ZAM_EXPERIMENTAL_LUA
esp_err_t zam_http_register_program(httpd_handle_t server);
esp_err_t zam_http_register_output(httpd_handle_t server);
#endif

static const char *TAG="zam_http";
static httpd_handle_t server;
static SemaphoreHandle_t mutex;

/* SSIDs are arbitrary bytes. Replace invalid UTF-8 sequences so every JSON
 * response remains UTF-8, preserving valid multi-byte sequences. */
static void utf8_sanitize(const char *src, char *dst, size_t cap)
{
    const unsigned char *s=(const unsigned char *)src; size_t w=0;
    while (*s && w+1<cap) {
        unsigned n=0; unsigned char c=*s;
        if (c<0x80) n=1;
        else if (c>=0xc2 && c<=0xdf && s[1] && (s[1]&0xc0)==0x80) n=2;
        else if (c>=0xe0 && c<=0xef && s[1] && s[2] && (s[1]&0xc0)==0x80 && (s[2]&0xc0)==0x80 && !(c==0xe0 && s[1]<0xa0) && !(c==0xed && s[1]>=0xa0)) n=3;
        else if (c>=0xf0 && c<=0xf4 && s[1] && s[2] && s[3] && (s[1]&0xc0)==0x80 && (s[2]&0xc0)==0x80 && (s[3]&0xc0)==0x80 && !(c==0xf0 && s[1]<0x90) && !(c==0xf4 && s[1]>=0x90)) n=4;
        if (n) { if (w+n>=cap) break; memcpy(dst+w,s,n); w+=n; s+=n; }
        else { if (w+3>=cap) break; memcpy(dst+w,"\xef\xbf\xbd",3); w+=3; s++; }
    }
    dst[w]=0;
}
static esp_err_t internal_error(httpd_req_t *req)
{
    esp_err_t err=httpd_resp_set_status(req,"500 Internal Server Error");
    if (err==ESP_OK) err=httpd_resp_set_type(req,"application/json");
    return err==ESP_OK ? httpd_resp_sendstr(req,"{\"error\":\"internal error\"}") : err;
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root)
{
    if (!root) return internal_error(req);
    char *body=cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!body) return internal_error(req);
    esp_err_t err=httpd_resp_set_type(req,"application/json");
    if (err==ESP_OK) err=httpd_resp_sendstr(req,body);
    free(body); return err;
}
esp_err_t zam_http_web_root(httpd_req_t *req);
esp_err_t zam_http_web_asset(httpd_req_t *req);
static esp_err_t health_handler(httpd_req_t *req)
{
    esp_err_t err=httpd_resp_set_type(req,"application/json");
    return err==ESP_OK ? httpd_resp_sendstr(req,"{\"status\":\"ok\",\"service\":\"zamprolab\",\"http\":\"running\"}") : err;
}
static esp_err_t system_handler(httpd_req_t *req)
{
    zam_system_info_t info;
    if (zam_system_get_info(&info)!=ESP_OK) return internal_error(req);
    cJSON *o=cJSON_CreateObject(); if (!o) return internal_error(req);
    cJSON *chip=cJSON_AddObjectToObject(o,"chip");
    if (!chip || !cJSON_AddStringToObject(o,"name",info.name) || !cJSON_AddStringToObject(o,"version",info.version) ||
        !cJSON_AddStringToObject(o,"target",info.target) || !cJSON_AddStringToObject(o,"idf_version",info.idf_version) ||
        !cJSON_AddNumberToObject(o,"uptime_ms",(double)info.uptime_ms) || !cJSON_AddNumberToObject(o,"flash_size",info.flash_size) ||
        !cJSON_AddNumberToObject(chip,"cores",info.cores) || !cJSON_AddNumberToObject(chip,"revision",info.revision)) {
        cJSON_Delete(o); return internal_error(req);
    }
    return send_json(req,o);
}
static esp_err_t memory_handler(httpd_req_t *req)
{
    zam_system_memory_t m; if (zam_system_get_memory(&m)!=ESP_OK) return internal_error(req);
    cJSON *o=cJSON_CreateObject(); if (!o) return internal_error(req);
    if (!cJSON_AddNumberToObject(o,"heap_free",m.heap_free) || !cJSON_AddNumberToObject(o,"heap_internal_free",m.heap_internal_free) ||
        !cJSON_AddNumberToObject(o,"heap_internal_largest_block",m.heap_internal_largest_block) ||
        !cJSON_AddNumberToObject(o,"heap_minimum_free",m.heap_minimum_free) || !cJSON_AddNumberToObject(o,"heap_external_free",m.heap_external_free)) {
        cJSON_Delete(o); return internal_error(req);
    }
    return send_json(req,o);
}
static const char *mode_name(zam_wifi_mode_t m)
{
    switch(m) { case ZAM_WIFI_AP:return "ap"; case ZAM_WIFI_STA:return "sta"; case ZAM_WIFI_APSTA:return "apsta"; case ZAM_WIFI_AUTO:return "auto"; default:return "unknown"; }
}
static esp_err_t wifi_handler(httpd_req_t *req)
{
    zam_wifi_status_t st; if (zam_wifi_status(&st)!=ESP_OK) return internal_error(req);
    char ap[97],sta[97]; utf8_sanitize(st.ap_ssid,ap,sizeof(ap)); utf8_sanitize(st.sta_ssid,sta,sizeof(sta));
    cJSON *o=cJSON_CreateObject(); if (!o) return internal_error(req);
    if (!cJSON_AddStringToObject(o,"configured_mode",mode_name(st.configured)) || !cJSON_AddStringToObject(o,"effective_mode",mode_name(st.effective)) ||
        !cJSON_AddBoolToObject(o,"sta_connected",st.connected) || !cJSON_AddBoolToObject(o,"sta_paused",st.paused) ||
        !cJSON_AddStringToObject(o,"sta_ssid",sta) || !cJSON_AddBoolToObject(o,"sta_ssid_valid_utf8",strcmp(sta,st.sta_ssid)==0) || !cJSON_AddStringToObject(o,"sta_ip",st.sta_ip) ||
        !cJSON_AddStringToObject(o,"ap_ssid",ap) || !cJSON_AddStringToObject(o,"ap_ip",st.ap_ip) ||
        !cJSON_AddNumberToObject(o,"ap_clients",st.clients) || !cJSON_AddBoolToObject(o,"fallback",st.fallback) || !cJSON_AddNumberToObject(o,"last_error",st.last_error) || !cJSON_AddNumberToObject(o,"disconnect_reason",st.disconnect_reason)) {
        cJSON_Delete(o); return internal_error(req);
    }
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return send_json(req,o);
}
static esp_err_t client_error(httpd_req_t *req,httpd_err_code_t error)
{
    bool method=error==HTTPD_405_METHOD_NOT_ALLOWED;
    const char *allow="GET";
    if (!strncmp(req->uri,"/api/wifi/settings",18)) allow="POST";
    else if (!strncmp(req->uri,"/api/wifi/scan",14)) allow="GET, POST";
#ifdef ZAM_EXPERIMENTAL_LUA
    if (!strncmp(req->uri,"/api/program/",13))
        allow=(!strncmp(req->uri,"/api/program/status",19) || !strncmp(req->uri,"/api/program/output",19))?"GET":"POST";
#endif
    esp_err_t err=httpd_resp_set_status(req,method ? "405 Method Not Allowed" : "404 Not Found");
    if (err==ESP_OK) err=httpd_resp_set_type(req,"application/json");
    if (err==ESP_OK && method) err=httpd_resp_set_hdr(req,"Allow",allow);
    if (err==ESP_OK) err=httpd_resp_set_hdr(req,"Connection","close");
    if (err==ESP_OK) httpd_resp_sendstr(req,method ? "{\"error\":\"method not allowed\"}" : "{\"error\":\"not found\"}");
    /* Close even if the request body was not consumed. */
    return ESP_FAIL;
}
static const httpd_uri_t endpoints[]={
    {.uri="/",.method=HTTP_GET,.handler=zam_http_web_root},
    {.uri="/style.css",.method=HTTP_GET,.handler=zam_http_web_asset,.user_ctx=(void *)"/web/style.css"},
    {.uri="/app.js",.method=HTTP_GET,.handler=zam_http_web_asset,.user_ctx=(void *)"/web/app.js"},
    {.uri="/terminal.js",.method=HTTP_GET,.handler=zam_http_web_asset,.user_ctx=(void *)"/web/terminal.js"},
    {.uri="/settings.js",.method=HTTP_GET,.handler=zam_http_web_asset,.user_ctx=(void *)"/web/settings.js"},
    {.uri="/highlight.js",.method=HTTP_GET,.handler=zam_http_web_asset,.user_ctx=(void *)"/web/highlight.js"},
    {.uri="/api/health",.method=HTTP_GET,.handler=health_handler},
    {.uri="/api/system",.method=HTTP_GET,.handler=system_handler},
    {.uri="/api/memory",.method=HTTP_GET,.handler=memory_handler},
    {.uri="/api/wifi/status",.method=HTTP_GET,.handler=wifi_handler},
};
esp_err_t zam_http_init(void)
{
    if (mutex) return ESP_OK;
    mutex=xSemaphoreCreateMutex(); if (!mutex || zam_files_init()) return ESP_ERR_NO_MEM; return ESP_OK;
}
esp_err_t zam_http_start(void)
{
    if (!mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (server) { xSemaphoreGive(mutex); return ESP_OK; }
    /* IDF's URI dispatcher logs the entire unknown URI at WARN, including
     * query strings supplied by clients. Suppress those URI-bearing logs. */
    esp_log_level_set("httpd_uri",ESP_LOG_ERROR);
    httpd_handle_t instance=NULL; httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers=25; config.stack_size=8192; config.lru_purge_enable=true;
    esp_err_t err=httpd_start(&instance,&config);
    for (size_t i=0;err==ESP_OK && i<sizeof(endpoints)/sizeof(endpoints[0]);i++) err=httpd_register_uri_handler(instance,&endpoints[i]);
    if (err==ESP_OK) err=zam_http_register_files(instance);
    if (err==ESP_OK) err=zam_http_register_wifi(instance);
#ifdef ZAM_EXPERIMENTAL_LUA
    if (err==ESP_OK) err=zam_http_register_program(instance);
    if (err==ESP_OK) err=zam_http_register_output(instance);
#endif
    if (err==ESP_OK) err=httpd_register_err_handler(instance,HTTPD_404_NOT_FOUND,client_error);
    if (err==ESP_OK) err=httpd_register_err_handler(instance,HTTPD_405_METHOD_NOT_ALLOWED,client_error);
    if (err==ESP_OK) server=instance;
    else if (instance) httpd_stop(instance);
    xSemaphoreGive(mutex);
    if (err==ESP_OK) ESP_LOGI(TAG,"HTTP server listening on port 80; ZAM_WEBFS_STREAM_65536_V1");
    return err;
}
esp_err_t zam_http_stop(void)
{
    if (!mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex,portMAX_DELAY);
    esp_err_t err=server ? httpd_stop(server) : ESP_OK;
    if (err==ESP_OK) server=NULL;
    xSemaphoreGive(mutex); return err;
}
bool zam_http_is_running(void)
{
    if (!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY); bool running=server!=NULL; xSemaphoreGive(mutex); return running;
}
