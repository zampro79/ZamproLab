#pragma once
#include <stddef.h>
#include "esp_err.h"

#define ZAM_DEVICE_NAME_MAX 32
esp_err_t zam_config_init(void);
esp_err_t zam_config_get(const char *key, char *value, size_t size);
esp_err_t zam_config_set(const char *key, const char *value);
esp_err_t zam_config_reset(void);

#include <stdint.h>
/* Versioned single NVS blob: updates of credentials and mode are atomic. */
typedef struct {
    uint32_t version;
    uint32_t mode;
    char ap_ssid[33];
    char ap_password[64];
    char sta_ssid[33];
    char sta_password[64];
} zam_config_wifi_t;
esp_err_t zam_config_wifi_load(zam_config_wifi_t *config);
esp_err_t zam_config_wifi_save(const zam_config_wifi_t *config);
