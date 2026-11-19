#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#define ZAM_FILES_PATH_MAX 192
#define ZAM_FILES_LIMIT (512*1024)
typedef struct { int fd; char temp[256], target[256]; bool active, existed; } zam_files_upload_t;
/* Paths are decoded once by the caller; validate() is the sole path policy. */
int zam_files_init(void);
int zam_files_validate(const char *path, bool root_ok, char *physical, size_t capacity);
int zam_files_lock(void);
void zam_files_unlock(void);
int zam_files_stat(const char *path, struct stat *st);
int zam_files_open_read(const char *path, struct stat *st);
DIR *zam_files_open_dir(const char *path);
int zam_files_mkdir(const char *path);
int zam_files_delete(const char *path);
int zam_files_rename(const char *from,const char *to);
int zam_files_upload_begin(zam_files_upload_t *up,const char *path,size_t length);
int zam_files_upload_write(zam_files_upload_t *up,const void *data,size_t size);
int zam_files_upload_finish(zam_files_upload_t *up);
void zam_files_upload_abort(zam_files_upload_t *up);
