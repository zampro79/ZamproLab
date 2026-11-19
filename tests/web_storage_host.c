#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static char location[256];
static FILE *test_fopen(const char *p,const char *m) { (void)p;return fopen(location,m); }
#define fopen test_fopen
#include "../components/zam_storage/zam_storage.c"
#undef fopen
static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;static esp_partition_t web={786432,"webfs"},user={1245184,"userfs"};
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &m;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned t){(void)t;return !pthread_mutex_lock(s);}
int xSemaphoreGive(SemaphoreHandle_t s){return !pthread_mutex_unlock(s);}
void vTaskDelay(unsigned t){(void)t;}
const esp_partition_t *esp_partition_find_first(int t,int s,const char*l){(void)t;(void)s;return !strcmp(l,"webfs")?&web:&user;}
esp_err_t esp_partition_read(const esp_partition_t*p,size_t o,void*d,size_t n){(void)p;(void)o;memset(d,255,n);return 0;}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*c){(void)c;return 0;}
esp_err_t esp_vfs_littlefs_unregister(const char*s){(void)s;return 0;}
esp_err_t esp_littlefs_info(const char*s,size_t*a,size_t*b){(void)s;*a=1;*b=0;return 0;}
int main(void){strcpy(location,"/tmp/zam-web-storage-13a-XXXXXX");int fd=mkstemp(location);assert(fd>=0);FILE*f=fdopen(fd,"wb");assert(f);for(int i=0;i<65537;i++)assert(fputc(i,f)!=EOF);assert(!fclose(f));assert(!zam_storage_init());assert(!zam_storage_mount());size_t n=0;FILE*openf=NULL;assert(zam_storage_web_open("/web/../user/x",&openf,&n)==ESP_ERR_NOT_FOUND);assert(zam_storage_web_open("/web/index.html",&openf,&n)==ESP_ERR_INVALID_SIZE && !openf);assert(!truncate(location,65536));assert(!zam_storage_web_open("/web/index.html",&openf,&n) && n==65536);assert(zam_storage_unmount()==ESP_ERR_INVALID_STATE);FILE *others[3]={0};size_t sizes[3];
for(int i=0;i<3;i++)assert(!zam_storage_web_open(i==1?"/web/style.css":"/web/index.html",&others[i],&sizes[i]) && sizes[i]==65536);
assert(!zam_storage_web_close(openf));assert(zam_storage_unmount()==ESP_ERR_INVALID_STATE);
for(int i=0;i<3;i++)assert(!zam_storage_web_close(others[i]));
assert(!truncate(location,0));assert(!zam_storage_web_open("/web/index.html",&openf,&n) && n==0);assert(!zam_storage_web_close(openf));
assert(!unlink(location));assert(zam_storage_web_open("/web/index.html",&openf,&n)==ESP_ERR_NOT_FOUND);assert(!zam_storage_unmount());puts("storage whitelist, bound and active-handle lifecycle: PASS");}
