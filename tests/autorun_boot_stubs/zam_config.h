#pragma once
#include "esp_err.h"
#define ZAM_DEVICE_NAME_MAX 32
esp_err_t zam_config_init(void);
esp_err_t zam_config_get(const char *,char *,size_t);
