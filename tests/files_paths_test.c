#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "zam_files.h"
int main(void){char out[256];
 assert(!zam_files_validate("/",1,out,sizeof(out))&&!strcmp(out,"/user"));
 assert(!zam_files_validate("/projects/main.lua",0,out,sizeof(out))&&!strcmp(out,"/user/projects/main.lua"));
 const char *bad[]={"/","foo","/../web","/a/../b","/web/x","/user/x","/a//b","/a/","/%2e%2e","/a\\b","/.zamupload-abc","/a/..hidden","/x?y"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(zam_files_validate(bad[i],0,out,sizeof(out))<0);
 char longpath[230];memset(longpath,'a',sizeof(longpath));longpath[0]='/';longpath[sizeof(longpath)-1]=0;assert(zam_files_validate(longpath,0,out,sizeof(out))<0);
 puts("File path validation passed");return 0;}
