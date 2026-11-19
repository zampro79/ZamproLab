#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ZAM_SYSTEM_NAME "ZamproLab"
typedef struct {
    const char *name, *version, *target, *idf_version;
    int64_t uptime_ms;
    uint32_t flash_size;
    unsigned cores, revision;
} zam_system_info_t;
typedef struct {
    size_t heap_free, heap_internal_free, heap_internal_largest_block;
    size_t heap_minimum_free, heap_external_free;
} zam_system_memory_t;
esp_err_t zam_system_get_info(zam_system_info_t *info);
esp_err_t zam_system_get_memory(zam_system_memory_t *memory);
void zam_system_log_startup(void);
esp_err_t zam_system_register_commands(void);
