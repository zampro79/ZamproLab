/* Verify provisioning images using the exact LittleFS version used to build them. */
#define main image_generator_main
#include "../tools/make_littlefs_image.c"
#undef main

int main(int argc,char **argv)
{
    if (argc!=3 && argc<6) return 2;
    unsigned long size=strtoul(argv[2],NULL,0);
    FILE *f=fopen(argv[1],"rb");if (!f || size%BLOCK_SIZE) return 1;
    struct lfs_config cfg={.context=f,.read=block_read,.prog=block_prog,.erase=block_erase,.sync=block_sync,
        .read_size=128,.prog_size=128,.block_size=BLOCK_SIZE,.block_count=(lfs_size_t)(size/BLOCK_SIZE),
        .block_cycles=512,.cache_size=512,.lookahead_size=128};
    lfs_t fs;int err=lfs_mount(&fs,&cfg);
    if (err) { fclose(f);return 1; }
    if (argc>=6) for (int i=3;i<argc && !err;i++) {
        const char *name=strrchr(argv[i],'/'); name=name ? name+1 : argv[i];
        FILE *source=fopen(argv[i],"rb");lfs_file_t file;
        if (!source || lfs_file_open(&fs,&file,name,LFS_O_RDONLY)) err=1;
        else {
            unsigned char expected[512],actual[512];size_t n;
            while ((n=fread(expected,1,sizeof(expected),source))>0) {
                lfs_ssize_t got=lfs_file_read(&fs,&file,actual,n);
                if (got!=(lfs_ssize_t)n || memcmp(expected,actual,n)) { err=1;break; }
            }
            if (ferror(source) || lfs_file_read(&fs,&file,actual,1)!=0) err=1;
            if (lfs_file_close(&fs,&file)) err=1;
        }
        if (source) fclose(source);
    } else {
        lfs_dir_t dir;struct lfs_info info;
        if (lfs_dir_open(&fs,&dir,"/")) err=1;
        else {
            int n;
            while ((n=lfs_dir_read(&fs,&dir,&info))>0) if (strcmp(info.name,".") && strcmp(info.name,"..")) err=1;
            if (n<0 || lfs_dir_close(&fs,&dir)) err=1;
        }
    }
    if (lfs_unmount(&fs)) err=1;
    fclose(f);return err ? 1 : 0;
}
