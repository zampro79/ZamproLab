#define _POSIX_C_SOURCE 200809L
#include "zam_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static unsigned web_open_files;
static SemaphoreHandle_t guard;
static const char *const labels[]={"webfs","userfs"};
static const char *const paths[]={"/web","/user"};
static zam_storage_info_t volumes[ZAM_STORAGE_COUNT];
static const esp_partition_t *partitions[ZAM_STORAGE_COUNT];
static void lock(void) { xSemaphoreTake(guard,portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(guard); }

/* Only a completely erased partition is called empty. A failed mount with
 * other contents cannot distinguish corruption from an unrelated format. */
static esp_err_t is_erased(const esp_partition_t *part, bool *erased)
{
    unsigned char bytes[512]; *erased=true;
    for (size_t off=0;off<part->size;off+=sizeof(bytes)) {
        size_t n=part->size-off<sizeof(bytes) ? part->size-off : sizeof(bytes);
        esp_err_t err=esp_partition_read(part,off,bytes,n);
        if (err!=ESP_OK) return err;
        for (size_t i=0;i<n;i++) if (bytes[i]!=0xff) { *erased=false; return ESP_OK; }
        if ((off & 0x3fffU)==0x3e00U) vTaskDelay(1);
    }
    return ESP_OK;
}
static esp_err_t mount_one(zam_storage_volume_t v)
{
    if (volumes[v].state==ZAM_STORAGE_MOUNTED) return ESP_OK;
    partitions[v]=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,labels[v]);
    if (!partitions[v]) { volumes[v].state=ZAM_STORAGE_MISSING; return volumes[v].error=ESP_ERR_NOT_FOUND; }
    volumes[v].total=partitions[v]->size;
    esp_vfs_littlefs_conf_t conf={.base_path=paths[v],.partition_label=labels[v],
        .format_if_mount_failed=false,.read_only=(v==ZAM_STORAGE_WEB),.grow_on_mount=false};
    esp_err_t err=esp_vfs_littlefs_register(&conf);
    if (err==ESP_OK) { volumes[v].state=ZAM_STORAGE_MOUNTED;volumes[v].error=ESP_OK;return ESP_OK; }
    volumes[v].error=err;
    if (err==ESP_FAIL) {
        bool empty=false; esp_err_t read_err=is_erased(partitions[v],&empty);
        if (read_err!=ESP_OK) { volumes[v].state=ZAM_STORAGE_ACCESS_ERROR;volumes[v].error=read_err;return read_err; }
        volumes[v].state=empty ? ZAM_STORAGE_EMPTY : ZAM_STORAGE_INVALID_OR_CORRUPT;
    } else volumes[v].state=ZAM_STORAGE_MOUNT_ERROR;
    return err;
}
esp_err_t zam_storage_init(void)
{
    if (guard) return ESP_OK;
    guard=xSemaphoreCreateMutex();
    return guard ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t zam_storage_mount(void)
{
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); esp_err_t web=mount_one(ZAM_STORAGE_WEB); esp_err_t user=mount_one(ZAM_STORAGE_USER); unlock();
    return web!=ESP_OK ? web : user;
}
esp_err_t zam_storage_unmount(void)
{
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); if (web_open_files) { unlock();return ESP_ERR_INVALID_STATE; }
    esp_err_t result=ESP_OK;
    for (unsigned i=0;i<ZAM_STORAGE_COUNT;i++) if (volumes[i].state==ZAM_STORAGE_MOUNTED) {
        esp_err_t err=esp_vfs_littlefs_unregister(labels[i]);
        if (err==ESP_OK) { volumes[i].state=ZAM_STORAGE_UNMOUNTED;volumes[i].error=ESP_OK; }
        else { volumes[i].error=err;if (result==ESP_OK) result=err; }
    }
    unlock(); return result;
}
esp_err_t zam_storage_get_info(zam_storage_volume_t v,zam_storage_info_t *out)
{
    if (!out || v<0 || v>=ZAM_STORAGE_COUNT) return ESP_ERR_INVALID_ARG;
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); *out=volumes[v];
    if (out->state==ZAM_STORAGE_MOUNTED) {
        esp_err_t err=esp_littlefs_info(labels[v],&out->total,&out->used);
        if (err!=ESP_OK) { out->state=ZAM_STORAGE_ACCESS_ERROR;out->error=err; }
        else out->free_bytes=out->total>=out->used ? out->total-out->used : 0;
    }
    unlock(); return ESP_OK;
}
esp_err_t zam_storage_web_open(const char *path,FILE **file,size_t *size)
{
    if (!path || !file || !size) return ESP_ERR_INVALID_ARG;
    *file=NULL; *size=0;
    if (strcmp(path,"/web/index.html") && strcmp(path,"/web/style.css") && strcmp(path,"/web/app.js") && strcmp(path,"/web/terminal.js") && strcmp(path,"/web/highlight.js") && strcmp(path,"/web/settings.js")) return ESP_ERR_NOT_FOUND;
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock();
    if (volumes[ZAM_STORAGE_WEB].state!=ZAM_STORAGE_MOUNTED) { unlock();return ESP_ERR_INVALID_STATE; }
    FILE *f=fopen(path,"rb");
    if (!f) { esp_err_t err=errno==ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;unlock();return err; }
    struct stat st;
    if (fstat(fileno(f),&st)!=0) { fclose(f);unlock();return ESP_FAIL; }
    if (st.st_size<0 || (size_t)st.st_size>ZAM_WEB_ASSET_LIMIT) { fclose(f);unlock();return ESP_ERR_INVALID_SIZE; }
    *file=f;*size=(size_t)st.st_size;web_open_files++;unlock();return ESP_OK;
}
esp_err_t zam_storage_web_close(FILE *file)
{
    if (!file || !guard) return ESP_ERR_INVALID_ARG;
    lock();int rc=fclose(file);if (web_open_files) web_open_files--;unlock();
    return rc ? ESP_FAIL : ESP_OK;
}
static bool probe_path(const char *token,char *path,size_t size)
{
    if (!token || strlen(token)!=32) return false;
    for (size_t i=0;i<32;i++) if (!((token[i]>='0' && token[i]<='9') || (token[i]>='a' && token[i]<='f'))) return false;
    return snprintf(path,size,"/user/.zamprobe-%s",token)>0;
}
static esp_err_t probe_check_locked(const char *path,const char *token)
{
    char expected[64],actual[64]; int len=snprintf(expected,sizeof(expected),"ZamproLab probe %s\n",token);
    int fd=open(path,O_RDONLY); if (fd<0) return errno==ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    ssize_t n=read(fd,actual,sizeof(actual)); int close_err=close(fd);
    return n==len && !memcmp(actual,expected,(size_t)len) && !close_err ? ESP_OK : ESP_FAIL;
}
esp_err_t zam_storage_probe_create(const char *token)
{
    char path[64]; if (!probe_path(token,path,sizeof(path))) return ESP_ERR_INVALID_ARG;
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); if (volumes[ZAM_STORAGE_USER].state!=ZAM_STORAGE_MOUNTED) { unlock();return ESP_ERR_INVALID_STATE; }
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) { esp_err_t err=errno==EEXIST ? ESP_ERR_INVALID_STATE : ESP_FAIL;unlock();return err; }
    char content[64]; int len=snprintf(content,sizeof(content),"ZamproLab probe %s\n",token);
    ssize_t written=write(fd,content,(size_t)len); int close_err=close(fd);
    if (written!=len || close_err) { unlink(path);unlock();return ESP_FAIL; }
    esp_err_t err=probe_check_locked(path,token);unlock();return err;
}
esp_err_t zam_storage_probe_check(const char *token)
{
    char path[64]; if (!probe_path(token,path,sizeof(path))) return ESP_ERR_INVALID_ARG;
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); esp_err_t err=volumes[ZAM_STORAGE_USER].state==ZAM_STORAGE_MOUNTED ? probe_check_locked(path,token) : ESP_ERR_INVALID_STATE;unlock();return err;
}
esp_err_t zam_storage_probe_delete(const char *token)
{
    char path[64]; if (!probe_path(token,path,sizeof(path))) return ESP_ERR_INVALID_ARG;
    if (!guard) return ESP_ERR_INVALID_STATE;
    lock(); esp_err_t err=volumes[ZAM_STORAGE_USER].state==ZAM_STORAGE_MOUNTED ? probe_check_locked(path,token) : ESP_ERR_INVALID_STATE;
    if (err==ESP_OK && unlink(path)!=0) err=ESP_FAIL;
    unlock();return err;
}
