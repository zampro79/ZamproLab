#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef struct { size_t size; const char *label; } esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_LITTLEFS 0x83
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label);
esp_err_t esp_partition_read(const esp_partition_t *part,size_t off,void *dst,size_t size);
