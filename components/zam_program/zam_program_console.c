#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_err.h"
#include "zam_program.h"
static const char *const states[]={"IDLE","STARTING","RUNNING","STOPPING","CLEANUP"};
static const char *const results[]={"none","ok","stopped","syntax_error","runtime_error","memory_error","load_error","internal_error"};
static int lua_command(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"status")){
        zam_program_status_t s;if(zam_program_get_status(&s)!=ESP_OK)return 1;
        printf("[lua] state=%s last=%s peak=%u live=%u stack_free=%u dropped=%u heap_before=%u heap_after=%u min=%u largest=%u load_us=%lld run_us=%lld cleanup_us=%lld stop_us=%lld produced=%u\n",states[s.state],results[s.last_result],(unsigned)s.lua_peak,(unsigned)s.lua_live,(unsigned)s.stack_free,(unsigned)s.output_dropped,(unsigned)s.heap_before,(unsigned)s.heap_after,(unsigned)s.heap_minimum,(unsigned)s.heap_largest,(long long)s.load_us,(long long)s.run_us,(long long)s.cleanup_us,(long long)s.stop_us,(unsigned)s.output_produced);
        return 0;
    }
    esp_err_t err;
    if(argc==3&&!strcmp(argv[1],"run"))err=zam_program_run(argv[2]);
    else if(argc==2&&!strcmp(argv[1],"stop"))err=zam_program_stop();
    else {puts("Usage: lua run <path> | stop | status");return 1;}
    if(err!=ESP_OK){printf("[lua] request rejected: %s\n",esp_err_to_name(err));return 1;}
    puts(argc==3?"[lua] queued":"[lua] stop requested");return 0;
}
esp_err_t zam_program_register_console(void)
{
    const esp_console_cmd_t cmd={.command="lua",.help="Experimental Lua run/stop/status",.func=lua_command};
    return esp_console_cmd_register(&cmd);
}
