#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_err.h"

typedef enum { ZAM_STORAGE_WEB, ZAM_STORAGE_USER, ZAM_STORAGE_COUNT } zam_storage_volume_t;
typedef enum {
    ZAM_STORAGE_NOT_INITIALIZED, ZAM_STORAGE_MOUNTED, ZAM_STORAGE_MISSING,
    ZAM_STORAGE_EMPTY, ZAM_STORAGE_INVALID_OR_CORRUPT, ZAM_STORAGE_MOUNT_ERROR,
    ZAM_STORAGE_ACCESS_ERROR, ZAM_STORAGE_UNMOUNTED
} zam_storage_state_t;
typedef struct {
    zam_storage_state_t state;
    esp_err_t error;
    size_t total, used, free_bytes;
} zam_storage_info_t;
/* init is called once during boot; mount and unmount are serialized. */
esp_err_t zam_storage_init(void);
esp_err_t zam_storage_mount(void);
esp_err_t zam_storage_unmount(void);
esp_err_t zam_storage_get_info(zam_storage_volume_t volume, zam_storage_info_t *info);
/* Open only approved immutable WebFS files; close each successful open. */
#define ZAM_WEB_ASSET_LIMIT (64U * 1024U)
esp_err_t zam_storage_web_open(const char *path, FILE **file, size_t *size);
esp_err_t zam_storage_web_close(FILE *file);
/* Diagnostic probe uses a caller-supplied 32-hex token and O_EXCL creation. */
esp_err_t zam_storage_probe_create(const char *token);
esp_err_t zam_storage_probe_check(const char *token);
esp_err_t zam_storage_probe_delete(const char *token);
