#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "zam_storage.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "freertos/semphr.h"

static esp_partition_t web={1024,"webfs"}, user={1024,"userfs"};
static bool missing,nonempty,io_error;
static unsigned web_calls,user_calls;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &web; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned t) { (void)s;(void)t;return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s;return 1; }
void vTaskDelay(unsigned t) { (void)t; }
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label)
{
    assert(type==ESP_PARTITION_TYPE_DATA && subtype==ESP_PARTITION_SUBTYPE_DATA_LITTLEFS);
    if (!strcmp(label,"webfs")) return missing ? NULL : &web;
    return !strcmp(label,"userfs") ? &user : NULL;
}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t off,void *dst,size_t n)
{
    (void)off;assert(p==&web);if (io_error) return ESP_FAIL;
    memset(dst,0xff,n);if (nonempty && n) ((unsigned char *)dst)[0]=0;
    return ESP_OK;
}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c)
{
    assert(!c->format_if_mount_failed && !c->grow_on_mount);
    if (!strcmp(c->partition_label,"webfs")) { assert(c->read_only);web_calls++;return ESP_FAIL; }
    assert(!strcmp(c->partition_label,"userfs") && !c->read_only);user_calls++;return ESP_OK;
}
esp_err_t esp_vfs_littlefs_unregister(const char *name) { assert(!strcmp(name,"userfs"));return ESP_OK; }
esp_err_t esp_littlefs_info(const char *name,size_t *total,size_t *used)
{ assert(!strcmp(name,"userfs"));*total=1024;*used=256;return ESP_OK; }
int main(void)
{
    zam_storage_info_t w,u;
    assert(zam_storage_init()==ESP_OK);
    assert(zam_storage_mount()==ESP_FAIL);
    assert(zam_storage_get_info(ZAM_STORAGE_WEB,&w)==ESP_OK && w.state==ZAM_STORAGE_EMPTY);
    assert(zam_storage_get_info(ZAM_STORAGE_USER,&u)==ESP_OK && u.state==ZAM_STORAGE_MOUNTED && u.free_bytes==768);
    assert(web_calls==1 && user_calls==1);
    nonempty=true;assert(zam_storage_mount()==ESP_FAIL);
    assert(zam_storage_get_info(ZAM_STORAGE_WEB,&w)==ESP_OK && w.state==ZAM_STORAGE_INVALID_OR_CORRUPT && user_calls==1);
    nonempty=false;io_error=true;assert(zam_storage_mount()==ESP_FAIL);
    assert(zam_storage_get_info(ZAM_STORAGE_WEB,&w)==ESP_OK && w.state==ZAM_STORAGE_ACCESS_ERROR);
    io_error=false;missing=true;assert(zam_storage_mount()==ESP_ERR_NOT_FOUND);
    assert(zam_storage_get_info(ZAM_STORAGE_WEB,&w)==ESP_OK && w.state==ZAM_STORAGE_MISSING);
    assert(zam_storage_unmount()==ESP_OK && zam_storage_unmount()==ESP_OK);
    assert(zam_storage_get_info(ZAM_STORAGE_USER,&u)==ESP_OK && u.state==ZAM_STORAGE_UNMOUNTED);
    return 0;
}
