#include <string.h>
#include "esp_http_server.h"
#include "zam_storage.h"
#include "zam_web_stream.h"
static int web_send(void *ctx,const void *data,size_t size)
{
    return httpd_resp_send_chunk(ctx,data,(ssize_t)size)==ESP_OK ? 0 : -1;
}
static esp_err_t web_handler(httpd_req_t *req,const char *path,const char *mime)
{
    FILE *f=NULL;size_t size=0;esp_err_t err=zam_storage_web_open(path,&f,&size);
    if (err==ESP_ERR_INVALID_STATE) { httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Web filesystem unavailable"); }
    if (err==ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Asset not found");
    if (err!=ESP_OK) { httpd_resp_set_status(req,"500 Internal Server Error");return httpd_resp_sendstr(req,"{\"error\":\"internal error\"}"); }
    err=httpd_resp_set_type(req,mime);
    if (err==ESP_OK && zam_web_stream(f,size,web_send,req)) err=ESP_FAIL;
    if (zam_storage_web_close(f)!=ESP_OK) err=ESP_FAIL;
    if (err==ESP_OK) err=httpd_resp_send_chunk(req,NULL,0);
    return err;
}
esp_err_t zam_http_web_root(httpd_req_t *req)
{
    return web_handler(req,"/web/index.html","text/html; charset=utf-8");
}
esp_err_t zam_http_web_asset(httpd_req_t *req)
{
    const char *path=(const char *)req->user_ctx;
    return web_handler(req,path,!strcmp(path,"/web/style.css") ? "text/css; charset=utf-8" : "text/javascript; charset=utf-8");
}
