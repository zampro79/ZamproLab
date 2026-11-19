#include "zam_config.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

static bool ready;
static bool valid_key(const char *key)
{
    return key && (!strcmp(key, "device_name") || !strcmp(key, "boot_message"));
}

esp_err_t zam_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    ready = err == ESP_OK;
    return err;
}

esp_err_t zam_config_get(const char *key, char *value, size_t size)
{
    if (!valid_key(key) || !value || !size) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    const char *fallback = !strcmp(key, "device_name") ? "ZamproLab" : "on";
    size_t capacity = size;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("zamprolab", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, key, value, &size);
        nvs_close(handle);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (strlen(fallback) + 1 > capacity) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(value, fallback, strlen(fallback) + 1);
        return ESP_OK;
    }
    return err;
}

esp_err_t zam_config_set(const char *key, const char *value)
{
    if (!valid_key(key) || !value) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(value);
    if (!strcmp(key, "device_name")) {
        if (!len || len > ZAM_DEVICE_NAME_MAX) return ESP_ERR_INVALID_ARG;
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = value[i];
            if (c < 32 || c > 126) return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(value, "on") && strcmp(value, "off")) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("zamprolab", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t zam_config_reset(void)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("zamprolab", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    const char *keys[] = {"device_name", "boot_message"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        err = nvs_erase_key(handle, keys[i]);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) break;
        err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t zam_config_wifi_load(zam_config_wifi_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("zamprolab", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(*config);
    err = nvs_get_blob(handle, "wifi_v1", config, &size);
    nvs_close(handle);
    if (err == ESP_OK && size != sizeof(*config)) return ESP_ERR_NVS_INVALID_LENGTH;
    return err;
}

esp_err_t zam_config_wifi_save(const zam_config_wifi_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("zamprolab", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "wifi_v1", config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
