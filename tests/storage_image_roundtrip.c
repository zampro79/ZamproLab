/* Exercises file and directory persistence only on a temporary image copy. */
#define main image_generator_main
#include "../tools/make_littlefs_image.c"
#undef main

int main(int argc,char **argv)
{
    if (argc!=3) return 2;
    unsigned long size=strtoul(argv[2],NULL,0);
    FILE *f=fopen(argv[1],"rb+");if (!f || size%BLOCK_SIZE) return 1;
    struct lfs_config cfg={.context=f,.read=block_read,.prog=block_prog,.erase=block_erase,.sync=block_sync,
        .read_size=128,.prog_size=128,.block_size=BLOCK_SIZE,.block_count=(lfs_size_t)(size/BLOCK_SIZE),
        .block_cycles=512,.cache_size=512,.lookahead_size=128};
    const char expected[]="ZamproLab temporary persistence test\n";
    lfs_t fs;lfs_file_t file;int err=lfs_mount(&fs,&cfg);
    if (!err) err=lfs_mkdir(&fs,"/probe");
    if (!err) err=lfs_file_open(&fs,&file,"/probe/check",LFS_O_WRONLY|LFS_O_CREAT|LFS_O_EXCL);
    if (!err) {
        if (lfs_file_write(&fs,&file,expected,sizeof(expected))!=(lfs_ssize_t)sizeof(expected)) err=1;
        if (lfs_file_close(&fs,&file)) err=1;
    }
    if (err) { fclose(f);return 1; }
    if (lfs_unmount(&fs) || fclose(f)) return 1;
    f=fopen(argv[1],"rb+");if (!f) return 1;cfg.context=f;
    err=lfs_mount(&fs,&cfg);
    if (!err) err=lfs_file_open(&fs,&file,"/probe/check",LFS_O_RDONLY);
    if (!err) {
        char got[sizeof(expected)];
        if (lfs_file_read(&fs,&file,got,sizeof(got))!=(lfs_ssize_t)sizeof(got) || memcmp(got,expected,sizeof(got))) err=1;
        if (lfs_file_close(&fs,&file)) err=1;
    }
    if (!err) err=lfs_remove(&fs,"/probe/check");
    if (!err) err=lfs_remove(&fs,"/probe");
    if (lfs_unmount(&fs)) err=1;
    if (fclose(f)) err=1;
    return err ? 1 : 0;
}
