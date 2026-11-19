#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
extern atomic_int test_rgb_enabled,test_rgb_last,test_rgb_fail;
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "zam_files.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "zam_runtime_lua.h"
static char directory[256],output[4096];
static unsigned checks,stop_after,started;static bool mutate,stop_at_cleanup;
static int modes[64],levels[64],calls,fail_config,fail_low,fail_disable,disable_count;
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level){assert(pin>=0&&pin<64);calls++;if(fail_low){fail_low=0;return ESP_FAIL;}levels[pin]=(int)level;return ESP_OK;}
int gpio_get_level(gpio_num_t pin){assert(pin>=0&&pin<64);calls++;return levels[pin];}
esp_err_t gpio_config(const gpio_config_t *c){assert(c->pin_bit_mask && !(c->pin_bit_mask&(c->pin_bit_mask-1)));int pin=__builtin_ctzll(c->pin_bit_mask);calls++;assert(!c->pull_up_en&&!c->pull_down_en&&!c->intr_type);if(c->mode==GPIO_MODE_OUTPUT)assert(levels[pin]==0);if(c->mode==GPIO_MODE_DISABLE){if(modes[pin]==GPIO_MODE_OUTPUT)assert(levels[pin]==0);disable_count++;}modes[pin]=c->mode;if(c->mode==GPIO_MODE_DISABLE && fail_disable){fail_disable=0;return ESP_FAIL;}if(fail_config && --fail_config==0)return ESP_FAIL;return ESP_OK;}

static int buses,devices,probe_calls,combined_calls,transfer_calls,mock_setup_error,mock_probe_error,mock_transfer_error,mock_release_error,mock_empty_scan;
static unsigned char last_payload[256];static size_t last_length;
const char *esp_err_to_name(esp_err_t code){(void)code;return "mock";}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c,i2c_master_bus_handle_t *out){assert(c->i2c_port==-1 && c->sda_io_num!=c->scl_io_num);if(mock_setup_error)return mock_setup_error;buses++;*out=(void *)1;return ESP_OK;}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t b){assert(b && buses==1 && devices==0);if(mock_release_error)return ESP_FAIL;buses--;return ESP_OK;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t *c,i2c_master_dev_handle_t *out){assert(b && buses==1 && !devices && c->device_address>=8 && c->device_address<=0x77);devices++;*out=(void *)2;return ESP_OK;}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d){assert(d && devices==1);devices--;return ESP_OK;}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t b,uint16_t addr,int timeout){assert(b && addr>=8 && addr<=0x77 && timeout==20);probe_calls++;if(mock_empty_scan)return ESP_ERR_NOT_FOUND;if(mock_probe_error)return mock_probe_error;return addr==0x3c || addr==0x76 ? ESP_OK : ESP_ERR_NOT_FOUND;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *data,size_t n,int timeout){assert(d && n<=256 && timeout==100);transfer_calls++;last_length=n;memcpy(last_payload,data,n);return mock_transfer_error;}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t d,uint8_t *data,size_t n,int timeout){assert(d && n<=256 && timeout==100);transfer_calls++;memset(data,0x60,n);return mock_transfer_error;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t *in,size_t n,uint8_t *out,size_t m,int timeout){combined_calls++;esp_err_t e=i2c_master_transmit(d,in,n,timeout);if(e)return e;return i2c_master_receive(d,out,m,timeout);}

static bool files_locked;
int zam_files_lock(void){assert(!files_locked);files_locked=true;return 0;}
void zam_files_unlock(void){assert(files_locked);files_locked=false;}
int zam_files_open_read(const char *path,struct stat *st){char full[512];if(!path||path[0]!='/'||strstr(path,".."))return -1;snprintf(full,sizeof(full),"%s%s",directory,path);if(stat(full,st))return -1;return open(full,O_RDONLY);}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void vTaskDelay(unsigned ticks){struct timespec ts={.tv_nsec=(long)ticks*1000000};nanosleep(&ts,NULL);}
static void write_script(const char *data){char path[512];snprintf(path,sizeof(path),"%s/test.lua",directory);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,strlen(data),f)==strlen(data));assert(fclose(f)==0);}
static bool stop(void *ctx){(void)ctx;checks++;return stop_after && checks>=stop_after;}
static void running(void *ctx){(void)ctx;started++;if(mutate)write_script("print('new')");}
static void cleanup(void *ctx){(void)ctx;if(stop_at_cleanup)stop_after=checks?checks:1;}
static void emit(void *ctx,const char *p,size_t n){(void)ctx;size_t used=strlen(output);if(n>sizeof(output)-used-1)n=sizeof(output)-used-1;memcpy(output+used,p,n);output[used+n]=0;}
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
extern size_t psram_mock_live_blocks, psram_mock_allocations;
#endif
static zam_lua_result_t run(zam_lua_metrics_t *m){checks=0;output[0]=0;zam_lua_callbacks_t cb={stop,running,cleanup,emit,NULL};zam_lua_result_t result=zam_runtime_lua_execute("/test.lua",&cb,m);assert(m->lua_live==0 && !files_locked);
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
 unsigned live,peak,nonexternal;zam_lua_psram_diagnostics(&live,&peak,&nonexternal);
 assert(live==0 && nonexternal==0 && psram_mock_live_blocks==0 && psram_mock_allocations>0);
#endif
 return result;}
static void write_module(const char *name,const char *content){char path[512];snprintf(path,sizeof(path),"%s/lib/%s.lua",directory,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(content,1,strlen(content),f)==strlen(content));assert(fclose(f)==0);}
int main(void){char templ[]="/tmp/zam-lua-core-test.XXXXXX";char *dir=mkdtemp(templ);assert(dir);strcpy(directory,dir);zam_lua_metrics_t m;
 char moddir[512];snprintf(moddir,sizeof(moddir),"%s/lib",directory);assert(mkdir(moddir,0700)==0);
 write_module("a","module_count=(module_count or 0)+1; return {value=41}");
 write_module("b","local a=require('lib.a'); return {value=a.value+1}");
 write_module("utils","local M={} function M.saluta(nome) return 'Ciao '..nome end return M");
 write_script("local utils,path=require('lib.utils'); assert(path=='/lib/utils.lua'); local u,p=require('lib.utils'); assert(u==utils and p==nil); print(utils.saluta('ZamproLab'))");assert(run(&m)==ZAM_LUA_OK && strstr(output,"Ciao ZamproLab"));
 write_script("local a=require('lib.a'); local b=require('lib.b'); assert(a==require('lib.a') and module_count==1 and b.value==42 and require('string')==string)");assert(run(&m)==ZAM_LUA_OK);
 write_module("a","return {value=8}");write_script("assert(require('lib.a').value==8)");assert(run(&m)==ZAM_LUA_OK);
 write_script("require('lib.missing')");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"lib.missing"));
 write_module("a","local =");write_script("require('lib.a')");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"/lib/a.lua:1"));
 char longsyntax[1024];memset(longsyntax,'x',sizeof(longsyntax)-1);memcpy(longsyntax,"local = ",8);longsyntax[sizeof(longsyntax)-1]=0;write_module("a",longsyntax);assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"/lib/a.lua:1") && !strstr(output,"read failed"));
 write_module("a","error('module test')");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"/lib/a.lua:1"));
 write_module("a","\x1bLua");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"binary"));
 write_module("a","return require('lib.b')");write_module("b","return require('lib.a')");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"circular module dependency"));
 const char *bad_modules[]={"../secret","/web/file","lib..a","lib/a","lib.a.lua/../x","a.","", "lib.a\\0hidden"};
 for(size_t i=0;i<sizeof(bad_modules)/sizeof(bad_modules[0]);i++){char script[256];snprintf(script,sizeof(script),"require('%s')",bad_modules[i]);write_script(script);assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"invalid module name"));}
 write_module("a","local x={} for i=1,1000 do x[i]=string.rep('x',1024) end return x");write_script("require('lib.a')");assert(run(&m)==ZAM_LUA_MEMORY);
 char *compile_oom=malloc(32768);assert(compile_oom);size_t used=(size_t)sprintf(compile_oom,"return {");for(int i=0;i<4000;i++)used+=(size_t)sprintf(compile_oom+used,"%d,",i);assert(used+2<32768);strcpy(compile_oom+used,"}");write_module("a",compile_oom);free(compile_oom);zam_lua_result_t compile_result=run(&m);if(CONFIG_ZAM_LUA_HEAP_LIMIT==49152)assert(compile_result==ZAM_LUA_MEMORY);else assert(compile_result==ZAM_LUA_MEMORY || compile_result==ZAM_LUA_OK);
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT && CONFIG_ZAM_LUA_HEAP_LIMIT == 262144
 /* Exhaust Lua memory while compiling a valid module, then prove a new RUN recovers. */
 char *oom_module=malloc(32768);assert(oom_module);size_t n=(size_t)sprintf(oom_module,"print('MODULE_STARTED');return {");
 for(int i=0;i<4000;i++) { n+=(size_t)sprintf(oom_module+n,"%d,",i); } assert(n+2<32768);strcpy(oom_module+n,"}");write_module("a",oom_module);free(oom_module);
 write_script("local t={} for i=1,400 do t[i]=string.rep('x',512)..i end print('BEFORE_REQUIRE');require('lib.a')");
 assert(run(&m)==ZAM_LUA_MEMORY && strstr(output,"BEFORE_REQUIRE") && !strstr(output,"MODULE_STARTED"));
 write_module("a","return 17");write_script("assert(require('lib.a')==17)");assert(run(&m)==ZAM_LUA_OK);
 printf("PSRAM modules: compile OOM and new RUN recovery PASS\n");
#endif
 char *large=malloc(32770);assert(large);memset(large,'x',32769);large[32769]=0;write_module("a",large);free(large);assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"source limit"));
 write_module("a","");write_script("assert(require('lib.a')==true and require('lib.a')==true)");assert(run(&m)==ZAM_LUA_OK);
 write_module("a","return false");write_script("assert(require('lib.a')==false and require('lib.a')==false)");assert(run(&m)==ZAM_LUA_OK);
 write_module("a","while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_STOPPED);stop_after=0;
 write_module("a","return 9");write_script("assert(require('lib.a')==9)");assert(run(&m)==ZAM_LUA_OK);
 char modulepath[512];for(const char **p=(const char *[]){"a","b","utils",NULL};*p;p++){snprintf(modulepath,sizeof(modulepath),"%s/lib/%s.lua",directory,*p);assert(unlink(modulepath)==0);}assert(rmdir(moddir)==0);
 printf("UserFS modules: cache, nested, errors, traversal, OOM, STOP and recovery PASS\n");
 write_script("assert(zam.rgb == nil)");assert(run(&m)==ZAM_LUA_OK);
 atomic_store(&test_rgb_enabled,1);write_script("zam.rgb.set(1,2,3)");assert(run(&m)==ZAM_LUA_OK && atomic_load(&test_rgb_last)==0x010203);
 const char *bad_rgb[]={"zam.rgb.set(-1,0,0)","zam.rgb.set(256,0,0)","zam.rgb.set(1.0,0,0)","zam.rgb.set(1,2)","zam.rgb.set(1,2,3,4)"};
 for(size_t i=0;i<sizeof(bad_rgb)/sizeof(bad_rgb[0]);i++){write_script(bad_rgb[i]);assert(run(&m)==ZAM_LUA_RUNTIME);}
 atomic_store(&test_rgb_fail,1);write_script("zam.rgb.set(1,2,3)");assert(run(&m)==ZAM_LUA_RUNTIME);atomic_store(&test_rgb_fail,0);atomic_store(&test_rgb_enabled,0);
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true)");assert(run(&m)==ZAM_LUA_OK&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);
 write_script("zam.gpio.mode(19,'input'); assert(zam.gpio.read(19)==false); zam.gpio.mode(19,'output'); zam.gpio.write(19,true); zam.gpio.mode(19,'input')");assert(run(&m)==ZAM_LUA_OK&&modes[19]==GPIO_MODE_DISABLE&&levels[19]==0);
 write_script("zam.gpio.mode(34,'input'); assert(zam.gpio.read(34)==false)");assert(run(&m)==ZAM_LUA_OK&&modes[34]==GPIO_MODE_DISABLE);
 const char *invalid[]={"zam.gpio.mode(6,'output')","zam.gpio.mode(2,'output')","zam.gpio.mode(1,'output')","zam.gpio.mode(12,'output')","zam.gpio.mode(34,'output')","zam.gpio.mode(64,'input')","zam.gpio.mode(-1,'input')","zam.gpio.mode('18','output')","zam.gpio.mode(18.0,'output')","zam.gpio.mode(18,'bad')","zam.gpio.mode(18)","zam.gpio.mode(18,'input',0)","zam.gpio.write(18,true)","zam.gpio.read(18)","zam.gpio.write(18,1)","zam.gpio.write(18,true,1)","zam.gpio.read(18,1)","zam.gpio.read()"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){int before=calls;write_script(invalid[i]);assert(run(&m)==ZAM_LUA_RUNTIME&&calls==before);}
 write_script("zam.gpio.mode(18,'input'); zam.gpio.write(18,true)");assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE);
 write_script("zam.gpio.mode(18,'output'); zam.gpio.read(18)");assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE);
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true); error('expected')");assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true); while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_STOPPED&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);stop_after=0;
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true); local a={} while true do a[#a+1]=string.rep('x',4096) end");assert(run(&m)==ZAM_LUA_MEMORY&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);
 write_script("zam.gpio.mode(18,'output')");fail_config=1;assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true); zam.gpio.mode(19,'output')");fail_config=2;assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0&&modes[19]==GPIO_MODE_DISABLE);
 write_script("zam.gpio.mode(18,'output')");fail_low=1;assert(run(&m)==ZAM_LUA_RUNTIME&&modes[18]==GPIO_MODE_DISABLE);
 levels[34]=1;write_script("zam.gpio.mode(34,'input'); assert(zam.gpio.read(34)==true)");assert(run(&m)==ZAM_LUA_OK);levels[34]=0;
 write_script("local x <close> = setmetatable({}, {__close=function() error('close failed') end}); while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"close failed"));stop_after=0;
 write_script("error('STOP_REQUESTED')");stop_at_cleanup=true;assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"STOP_REQUESTED"));stop_at_cleanup=false;stop_after=0;
 write_script("zam.gpio.mode(26,'output'); zam.gpio.write(26,true)");stop_at_cleanup=true;fail_disable=1;assert(run(&m)==ZAM_LUA_INTERNAL && strstr(output,"Cleanup GPIO fallito"));stop_at_cleanup=false;stop_after=0;
 write_script("zam.gpio.mode(25,'output'); zam.gpio.write(25,true)");fail_disable=1;assert(run(&m)==ZAM_LUA_INTERNAL&&strstr(output,"Cleanup GPIO fallito")&&levels[25]==0);
 write_script("zam.gpio.mode(25,'output')");{int before=calls;assert(run(&m)==ZAM_LUA_RUNTIME&&strstr(output,"errore cleanup")&&calls==before);}
 write_script("zam.gpio.mode(18,'output'); zam.gpio.mode(18,'output'); zam.gpio.write(18,true)");assert(run(&m)==ZAM_LUA_OK&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);
 write_script("zam.gpio.write(18,true)");{int before=calls;assert(run(&m)==ZAM_LUA_RUNTIME&&calls==before);}
 write_script("zam.gpio.mode(18,'output'); zam.gpio.write(18,true); hold=setmetatable({}, {__gc=function() zam.gpio.mode(18,'output'); zam.gpio.write(18,true) end})");{int before=calls;assert(run(&m)==ZAM_LUA_OK&&calls==before+5&&modes[18]==GPIO_MODE_DISABLE&&levels[18]==0);}
 printf("GPIO host: validation, input-only, ownership, stop, errors, OOM and partial failure PASS (releases %d)\n",disable_count);
 write_script("zam.i2c.setup(21,22,100000); local d=zam.i2c.scan(); assert(#d==2 and d[1]==0x3c and d[2]==0x76); zam.i2c.write(0x76,string.char(1,0,2)); assert(zam.i2c.read(0x76,1)==string.char(0x60)); assert(zam.i2c.write_read(0x76,string.char(0xd0),1)==string.char(0x60))");assert(run(&m)==ZAM_LUA_OK && !buses && !devices && probe_calls==112 && combined_calls==1);
 write_script("zam.i2c.setup(21,22,400000); zam.i2c.write(0x76,string.char(1,0,2))");assert(run(&m)==ZAM_LUA_OK && last_length==3 && last_payload[1]==0 && !buses);
 write_script("zam.i2c.setup(21,22,100000); zam.i2c.write(0x76,string.rep('x',256))");assert(run(&m)==ZAM_LUA_OK && last_length==256);
 const char *bad_i2c[]={"zam.i2c.scan()","zam.i2c.setup(21,21,100000)","zam.i2c.setup(2,22,100000)","zam.i2c.setup(21,22,200000)","zam.i2c.setup('21',22,100000)","zam.gpio.mode(21,'output'); zam.i2c.setup(21,22,100000)","zam.i2c.setup(21,22,100000); zam.gpio.mode(21,'output')","zam.i2c.setup(21,22,100000); zam.i2c.write(7,'x')","zam.i2c.setup(21,22,100000); zam.i2c.write(0x76,string.rep('x',257))","zam.i2c.setup(21,22,100000); zam.i2c.read(0x76,0)"};
 for(unsigned i=0;i<sizeof(bad_i2c)/sizeof(bad_i2c[0]);i++){write_script(bad_i2c[i]);assert(run(&m)==ZAM_LUA_RUNTIME && !buses && !devices);}
 mock_empty_scan=1;write_script("zam.i2c.setup(21,22,100000); assert(#zam.i2c.scan()==0)");assert(run(&m)==ZAM_LUA_OK && !buses);mock_empty_scan=0;
 write_script("zam.i2c.setup(21,22,400000); zam.i2c.scan()");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"100 kHz") && !buses);
 write_script("zam.i2c.setup(21,22,100000); zam.i2c.scan()");stop_after=8;assert(run(&m)==ZAM_LUA_STOPPED && !buses);stop_after=0;
 write_script("zam.i2c.setup(21,22,100000); hold=setmetatable({}, {__gc=function() zam.i2c.setup(21,22,100000) end})");assert(run(&m)==ZAM_LUA_OK && !buses);
 mock_setup_error=ESP_ERR_NO_MEM;write_script("zam.i2c.setup(21,22,100000)");assert(run(&m)==ZAM_LUA_RUNTIME && !buses);mock_setup_error=0;
 mock_probe_error=ESP_ERR_TIMEOUT;write_script("zam.i2c.setup(21,22,100000); zam.i2c.scan()");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"Timeout I2C") && !buses);mock_probe_error=0;
 mock_transfer_error=ESP_FAIL;write_script("zam.i2c.setup(21,22,100000); zam.i2c.read(0x76,1)");assert(run(&m)==ZAM_LUA_RUNTIME && !buses && !devices);mock_transfer_error=0;
 write_script("zam.i2c.setup(21,22,100000); error('expected')");assert(run(&m)==ZAM_LUA_RUNTIME && !buses);
 write_script("zam.i2c.setup(21,22,100000); while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_STOPPED && !buses);stop_after=0;
 write_script("zam.i2c.setup(21,22,100000); local a={} while true do a[#a+1]=string.rep('x',4096) end");assert(run(&m)==ZAM_LUA_MEMORY && !buses);
 printf("I2C host: validation, conflict, scan, binary, repeated start, errors, STOP and OOM PASS\n");
 write_script("print('hello')");assert(run(&m)==ZAM_LUA_OK&&strstr(output,"hello"));
 write_script("local =");assert(run(&m)==ZAM_LUA_SYNTAX);
 write_script("error('expected')");assert(run(&m)==ZAM_LUA_RUNTIME);
 write_script("local a={} while true do a[#a+1]=string.rep('x',4096) end");assert(run(&m)==ZAM_LUA_MEMORY&&m.lua_peak<=49152);
 write_script("while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_STOPPED);stop_after=0;
 write_script("print('old')");mutate=true;assert(run(&m)==ZAM_LUA_OK&&strstr(output,"old"));mutate=false;assert(run(&m)==ZAM_LUA_OK&&strstr(output,"new"));
 write_script("assert(io==nil and os==nil and package==nil and debug==nil and load==nil and pcall==nil and xpcall==nil)");assert(run(&m)==ZAM_LUA_OK);
 write_script("print('cycle')");for(int i=0;i<100;i++)assert(run(&m)==ZAM_LUA_OK);
 write_script("error('expected')");for(int i=0;i<50;i++)assert(run(&m)==ZAM_LUA_RUNTIME);
 write_script("while true do end");stop_after=10;for(int i=0;i<50;i++)assert(run(&m)==ZAM_LUA_STOPPED);stop_after=0;
 write_script("local a={} while true do a[#a+1]=string.rep('x',4096) end");for(int i=0;i<20;i++)assert(run(&m)==ZAM_LUA_MEMORY);
 char oversized[32770];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;write_script(oversized);assert(run(&m)==ZAM_LUA_LOAD);
 char path[512];snprintf(path,sizeof(path),"%s/test.lua",directory);
 const char *repo=getenv("ZAM_REPO_ROOT");assert(repo);char libpath[512], assembled[16000];
 snprintf(libpath,sizeof(libpath),"%s/samples/lua/00_welcome.lua",repo);FILE *welcome=fopen(libpath,"rb");assert(welcome);size_t wn=fread(assembled,1,sizeof(assembled)-1,welcome);assert(wn>0&&!ferror(welcome));fclose(welcome);assembled[wn]=0;
 write_script(assembled);atomic_store(&test_rgb_enabled,0);assert(run(&m)==ZAM_LUA_OK && strstr(output,"Welcome to ZamproLab!"));
 atomic_store(&test_rgb_enabled,1);assert(run(&m)==ZAM_LUA_OK && atomic_load(&test_rgb_last)==0x040404);stop_after=8;assert(run(&m)==ZAM_LUA_STOPPED);stop_after=0;atomic_store(&test_rgb_enabled,0);
 mock_release_error=1;write_script("zam.i2c.setup(21,22,100000)");assert(run(&m)==ZAM_LUA_INTERNAL && strstr(output,"Cleanup I2C fallito"));mock_release_error=0;
 write_script("zam.gpio.mode(21,'output')");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"errore cleanup"));
 write_script("zam.i2c.setup(21,22,100000)");assert(run(&m)==ZAM_LUA_RUNTIME && strstr(output,"gia utilizzato"));
 printf("I2C failed cleanup quarantine PASS\n");
 assert(unlink(path)==0);assert(run(&m)==ZAM_LUA_LOAD);assert(rmdir(directory)==0);
 printf("Lua core: 100 normal, 50 errors, 50 STOP, 20 OOM, snapshot and restrictions PASS (runs %u)\n",started);return 0;}
