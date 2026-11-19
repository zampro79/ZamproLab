#pragma once
#include <stddef.h>
#define ESP_OK 0
#define ZAM_STORAGE_USER 1
#define ZAM_STORAGE_MOUNTED 1
typedef struct {int state; size_t free_bytes;} zam_storage_info_t;
static inline int zam_storage_get_info(int v,zam_storage_info_t *i){(void)v;i->state=1;i->free_bytes=1000000;return 0;}
