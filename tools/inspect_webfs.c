/* Print the largest WebFS file and reject unknown paths or invalid images. */
#define main image_generator_main
#include "make_littlefs_image.c"
#undef main
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb");if (!f) return 1;
    struct lfs_config cfg={.context=f,.read=block_read,.prog=block_prog,.erase=block_erase,.sync=block_sync,
        .read_size=128,.prog_size=128,.block_size=BLOCK_SIZE,.block_count=786432/BLOCK_SIZE,
        .block_cycles=512,.cache_size=512,.lookahead_size=128};
    lfs_t fs;int err=lfs_mount(&fs,&cfg);if(err){fclose(f);return 1;}
    const char *const names[]={"index.html","style.css","app.js","terminal.js","highlight.js","settings.js"};
    unsigned found=0;size_t max=0;lfs_dir_t dir;struct lfs_info info;
    err=lfs_dir_open(&fs,&dir,"/");
    if(!err) {
        int n;
        while((n=lfs_dir_read(&fs,&dir,&info))>0) {
            if (!strcmp(info.name,".") || !strcmp(info.name,"..")) continue;
            unsigned i;for(i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(info.name,names[i]))break;
            if(i==sizeof(names)/sizeof(*names) || info.type!=LFS_TYPE_REG || (found & (1u<<i))) {err=1;break;}
            found|=1u<<i;if(info.size>max)max=info.size;
            lfs_file_t file;if(lfs_file_open(&fs,&file,info.name,LFS_O_RDONLY)) {err=1;break;}
            char buffer[1024];lfs_size_t total=0; lfs_ssize_t got;
            while((got=lfs_file_read(&fs,&file,buffer,sizeof(buffer)))>0)total+=(lfs_size_t)got;
            if(got<0 || total!=info.size)err=1;
            if(lfs_file_close(&fs,&file))err=1;
            if(err)break;
        }
        if(n<0 || lfs_dir_close(&fs,&dir))err=1;
    }
    if(found!=63)err=1;
    if(lfs_unmount(&fs))err=1;
    if(fclose(f))err=1;
    if(err)return 1;
    printf("%zu\n",max);return 0;
}
