#define main unused_manager_fixture
#define zam_runtime_lua_execute fake_lua_execute
#include "program_manager_host.c"
#undef zam_runtime_lua_execute
#undef main
#include <fcntl.h>
int zam_files_open_read(const char *path,struct stat *st)
{
    if(!autorun_dir || strncmp(path,"/autorun/",9)) {errno=ENOENT;return -1;}
    char full[512];snprintf(full,sizeof(full),"%s/%s",autorun_dir,path+9);
    if(stat(full,st) || !S_ISREG(st->st_mode))return -1;
    return open(full,O_RDONLY);
}
static void set_script(const char *root,const char *name,const char *text)
{
    char path[512];snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));
}
int main(void)
{
    char root[]="/tmp/zam-autorun-real-lua-XXXXXX";assert(mkdtemp(root));autorun_dir=root;
    assert(zam_program_init()==ESP_OK);
    set_script(root,"01_first.lua","value=42; print('first')");
    set_script(root,"02_second.lua","assert(value==nil); print('second')");
    zam_autorun_status_t as;zam_program_status_t ps;
    for(int i=0;i<30;i++){
        assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_COMPLETED);
        assert(zam_program_autorun_status(&as)==ESP_OK && as.completed==2 && as.result==ZAM_PROGRAM_OK);
        assert(zam_program_get_status(&ps)==ESP_OK && ps.lua_live==0 && ps.last_result==ZAM_PROGRAM_OK);
    }
    atomic_store(&between_hold,1);atomic_store(&between_entered,0);
    assert(zam_program_autorun_start()==ESP_OK);wait_for(&between_entered);
    set_script(root,"02_second.lua","assert(value==nil); print('modified')");
    atomic_store(&between_hold,0);wait_autorun(ZAM_AUTORUN_COMPLETED);
    atomic_store(&between_hold,1);atomic_store(&between_entered,0);
    assert(zam_program_autorun_start()==ESP_OK);wait_for(&between_entered);
    char removed[512];snprintf(removed,sizeof(removed),"%s/02_second.lua",root);assert(!unlink(removed));
    atomic_store(&between_hold,0);wait_autorun(ZAM_AUTORUN_FAILED);
    assert(zam_program_autorun_status(&as)==ESP_OK && as.completed==1 && as.result==ZAM_PROGRAM_LOAD_ERROR);
    set_script(root,"02_second.lua","assert(value==nil)");
    set_script(root,"01_first.lua","error('expected')");
    for(int i=0;i<20;i++){
        assert(zam_program_autorun_start()==ESP_OK);wait_autorun(ZAM_AUTORUN_FAILED);
        assert(zam_program_autorun_status(&as)==ESP_OK && as.completed==0 && as.result==ZAM_PROGRAM_RUNTIME_ERROR);
        assert(zam_program_get_status(&ps)==ESP_OK && ps.lua_live==0);
    }
    set_script(root,"01_first.lua","while true do end");
    for(int i=0;i<20;i++){
        assert(zam_program_autorun_start()==ESP_OK);wait_state(ZAM_PROGRAM_RUNNING);
        assert(zam_program_stop()==ESP_OK);wait_autorun(ZAM_AUTORUN_CANCELLED);
        assert(zam_program_autorun_status(&as)==ESP_OK && as.result==ZAM_PROGRAM_STOPPED);
        assert(zam_program_get_status(&ps)==ESP_OK && ps.lua_live==0);
    }
    puts("Real Lua Autorun: fresh states, 30 sequences, 20 errors, 20 STOP, live zero PASS");
    return 0;
}
