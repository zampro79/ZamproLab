#include <string.h>
#include "esp_log.h"
#include "zam_config.h"
#include "zam_console.h"
#include "zam_system.h"
#include "zam_wifi.h"
#include "zam_http.h"
#include "zam_storage.h"
#include "zam_rgb.h"
#include "esp_heap_caps.h"
#ifdef ZAM_EXPERIMENTAL_LUA
#include "zam_program.h"
#endif

void app_main(void)
{
    zam_system_log_startup();
    esp_err_t err = zam_config_init();
    if (err != ESP_OK) {
        ESP_LOGE("ZamproLab", "NVS initialization failed: %s; configuration unavailable, no erase performed", esp_err_to_name(err));
    } else {
        char enabled[ZAM_DEVICE_NAME_MAX + 1];
        char name[ZAM_DEVICE_NAME_MAX + 1];
        err = zam_config_get("boot_message", enabled, sizeof(enabled));
        if (err == ESP_OK && !strcmp(enabled, "on")) {
            err = zam_config_get("device_name", name, sizeof(name));
            if (err == ESP_OK) ESP_LOGI("ZamproLab", "Device: %s", name);
        }
        if (err != ESP_OK) ESP_LOGE("ZamproLab", "Configuration read failed: %s", esp_err_to_name(err));
    }
    err = zam_console_start();
#ifdef ZAM_EXPERIMENTAL_LUA
    bool console_ready = err == ESP_OK;
#endif
    if (err != ESP_OK) ESP_LOGE("ZamproLab", "Console initialization failed: %s", esp_err_to_name(err));
    ESP_LOGI("ZamproLab", "Before storage: heap=%zu internal=%zu largest=%zu minimum=%zu",
             heap_caps_get_free_size(MALLOC_CAP_8BIT),heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    err=zam_storage_init();
    if (err==ESP_OK) err=zam_storage_mount();
    if (err!=ESP_OK) ESP_LOGW("ZamproLab", "Storage mount incomplete: %s",esp_err_to_name(err));
#ifdef ZAM_EXPERIMENTAL_LUA
    zam_storage_info_t user_info,web_info;
    bool storage_ready = zam_storage_get_info(ZAM_STORAGE_USER,&user_info)==ESP_OK && user_info.state==ZAM_STORAGE_MOUNTED &&
                         zam_storage_get_info(ZAM_STORAGE_WEB,&web_info)==ESP_OK && web_info.state==ZAM_STORAGE_MOUNTED;
#endif
    ESP_LOGI("ZamproLab", "After storage: heap=%zu internal=%zu largest=%zu minimum=%zu",
             heap_caps_get_free_size(MALLOC_CAP_8BIT),heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    err=zam_rgb_init();
    if(err!=ESP_OK && err!=ESP_ERR_NOT_SUPPORTED) ESP_LOGW("ZamproLab","RGB indicator unavailable: %s",esp_err_to_name(err));
    err = zam_wifi_init();
#ifdef ZAM_EXPERIMENTAL_LUA
    bool wifi_ready = err == ESP_OK;
#endif
    if (err != ESP_OK) ESP_LOGE("ZamproLab", "Wi-Fi initialization failed: %s", esp_err_to_name(err));
#ifdef ZAM_EXPERIMENTAL_LUA
    err=zam_program_init();
    bool autorun_prepared = false;
    if(err==ESP_OK && console_ready && wifi_ready && storage_ready){
        esp_err_t prep=zam_program_autorun_prepare();
        autorun_prepared=prep==ESP_OK;
        if(prep!=ESP_OK)ESP_LOGW("ZamproLab","Autorun preparation failed: %s",esp_err_to_name(prep));
    }
    if(err==ESP_OK)err=zam_program_register_console();
    if(err!=ESP_OK){
        if(autorun_prepared){zam_program_autorun_abort_prepared();autorun_prepared=false;}
        ESP_LOGE("ZamproLab","Experimental Lua init failed: %s",esp_err_to_name(err));
    }
#endif
    ESP_LOGI("ZamproLab", "Before HTTP: heap=%zu internal=%zu largest=%zu minimum=%zu",
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    err=zam_http_init();
    if (err==ESP_OK) err=zam_http_start();
    if (err!=ESP_OK) {
#ifdef ZAM_EXPERIMENTAL_LUA
        if(autorun_prepared)zam_program_autorun_abort_prepared();
#endif
        ESP_LOGE("ZamproLab", "HTTP initialization failed: %s",esp_err_to_name(err));
    } else {
#ifdef ZAM_EXPERIMENTAL_LUA
        if(autorun_prepared){
            esp_err_t launch=zam_program_autorun_launch();
            if(launch!=ESP_OK)ESP_LOGW("ZamproLab","Autorun launch failed: %s",esp_err_to_name(launch));
        }
#endif
#ifdef ZAM_EXPERIMENTAL_LUA
        zam_program_indicator_ready();
#else
        if(zam_rgb_available()) (void)zam_rgb_set(0,4,0);
#endif
        ESP_LOGI("ZamproLab", "After HTTP: heap=%zu internal=%zu largest=%zu minimum=%zu",
                  heap_caps_get_free_size(MALLOC_CAP_8BIT),heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    }
}
