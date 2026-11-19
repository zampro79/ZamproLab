/* Build deterministic, explicitly provisioned LittleFS images with the same
 * LittleFS sources as the ESP-IDF component. Never writes a device. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lfs.h"

#define BLOCK_SIZE 4096u
static int block_read(const struct lfs_config *c,lfs_block_t block,lfs_off_t off,void *buffer,lfs_size_t size)
{
    FILE *f=c->context;
    if (block>=c->block_count || off+size>c->block_size || fseek(f,(long)(block*c->block_size+off),SEEK_SET) || fread(buffer,1,size,f)!=size) return LFS_ERR_IO;
    return 0;
}
static int block_prog(const struct lfs_config *c,lfs_block_t block,lfs_off_t off,const void *buffer,lfs_size_t size)
{
    FILE *f=c->context;
    if (block>=c->block_count || off+size>c->block_size || fseek(f,(long)(block*c->block_size+off),SEEK_SET) || fwrite(buffer,1,size,f)!=size) return LFS_ERR_IO;
    return 0;
}
static int block_erase(const struct lfs_config *c,lfs_block_t block)
{
    FILE *f=c->context; unsigned char blank[BLOCK_SIZE]; memset(blank,0xff,sizeof(blank));
    if (block>=c->block_count || fseek(f,(long)(block*c->block_size),SEEK_SET) || fwrite(blank,1,sizeof(blank),f)!=sizeof(blank)) return LFS_ERR_IO;
    return 0;
}
static int block_sync(const struct lfs_config *c) { return fflush(c->context) ? LFS_ERR_IO : 0; }
int main(int argc,char **argv)
{
    if (argc!=3 && argc<6) { fprintf(stderr,"usage: %s IMAGE SIZE_BYTES [INDEX_HTML STYLE_CSS APP_JS TERMINAL_JS HIGHLIGHT_JS]\n",argv[0]); return 2; }
    char *end=NULL; errno=0; unsigned long size=strtoul(argv[2],&end,0);
    if (errno || !end || *end || size<BLOCK_SIZE*4 || size%BLOCK_SIZE || size>4*1024*1024UL) { fputs("invalid image size\n",stderr);return 2; }
    FILE *f=fopen(argv[1],"wb+");if (!f) { perror("image");return 1; }
    unsigned char blank[BLOCK_SIZE];memset(blank,0xff,sizeof(blank));
    for (unsigned long off=0;off<size;off+=sizeof(blank)) if (fwrite(blank,1,sizeof(blank),f)!=sizeof(blank)) { fclose(f);return 1; }
    struct lfs_config cfg={.context=f,.read=block_read,.prog=block_prog,.erase=block_erase,.sync=block_sync,
        .read_size=128,.prog_size=128,.block_size=BLOCK_SIZE,.block_count=(lfs_size_t)(size/BLOCK_SIZE),
        .block_cycles=512,.cache_size=512,.lookahead_size=128};
    lfs_t fs; int err=lfs_format(&fs,&cfg);if (!err) err=lfs_mount(&fs,&cfg);
    if (err) { fprintf(stderr,"LittleFS format/mount: %d\n",err);fclose(f);return 1; }
    for (int i=3;i<argc && !err;i++) {
        const char *name=strrchr(argv[i],'/'); name=name ? name+1 : argv[i];
        FILE *in=fopen(argv[i],"rb"); if (!in) { perror("index");lfs_unmount(&fs);fclose(f);return 1; }
        lfs_file_t file;err=lfs_file_open(&fs,&file,name,LFS_O_WRONLY|LFS_O_CREAT|LFS_O_EXCL);
        if (!err) {
            unsigned char buf[1024];size_t n;
            while ((n=fread(buf,1,sizeof(buf),in))>0) { if (lfs_file_write(&fs,&file,buf,n)!=(lfs_ssize_t)n) {err=LFS_ERR_IO;break;} }
            if (ferror(in)) err=LFS_ERR_IO;
            int close_err=lfs_file_close(&fs,&file);if (!err) err=close_err;
        }
        fclose(in);
    }
    int unmount_err=lfs_unmount(&fs);if (!err) err=unmount_err;
    if (fclose(f)) err=LFS_ERR_IO;
    if (err) {fprintf(stderr,"LittleFS image error: %d\n",err);return 1;}
    return 0;
}
