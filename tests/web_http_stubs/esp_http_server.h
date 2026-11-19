#pragma once
#include <sys/types.h>
#include "esp_err.h"
typedef struct {void *user_ctx;} httpd_req_t;
#define HTTPD_404_NOT_FOUND 404
esp_err_t httpd_resp_send_chunk(httpd_req_t*,const char*,ssize_t);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_sendstr(httpd_req_t*,const char*);
esp_err_t httpd_resp_send_err(httpd_req_t*,int,const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*,const char*);
