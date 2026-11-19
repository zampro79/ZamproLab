#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_NOT_FOUND 261
#define ESP_ERR_TIMEOUT 263
const char *esp_err_to_name(esp_err_t code);
