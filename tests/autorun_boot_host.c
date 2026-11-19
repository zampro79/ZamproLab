#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "zam_storage.h"
#include "zam_program.h"
static int fail_console,fail_wifi,fail_user,fail_web,fail_program,fail_reg,fail_http_init,fail_http_start;
static int prepared,launched,aborted,http_started,stop_requested,stop_during_start,console_registered,scan_calls;
const char *esp_err_to_name(esp_err_t e){(void)e;return "mock";}
void zam_system_log_startup(void){}
esp_err_t zam_config_init(void){return ESP_OK;}
esp_err_t zam_config_get(const char *key,char *v,size_t n){(void)key;snprintf(v,n,"off");return ESP_OK;}
esp_err_t zam_console_start(void){return fail_console?ESP_FAIL:ESP_OK;}
esp_err_t zam_storage_init(void){return ESP_OK;}
esp_err_t zam_storage_mount(void){return ESP_OK;}
esp_err_t zam_storage_get_info(zam_storage_volume_t v,zam_storage_info_t *i){memset(i,0,sizeof(*i));i->state=(v==ZAM_STORAGE_USER?fail_user:fail_web)?ZAM_STORAGE_MISSING:ZAM_STORAGE_MOUNTED;return ESP_OK;}
esp_err_t zam_rgb_init(void){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t zam_wifi_init(void){return fail_wifi?ESP_FAIL:ESP_OK;}
esp_err_t zam_program_init(void){return fail_program?ESP_FAIL:ESP_OK;}
esp_err_t zam_program_register_console(void){console_registered++;return fail_reg?ESP_FAIL:ESP_OK;}
esp_err_t zam_program_autorun_prepare(void){assert(!http_started);prepared++;return ESP_OK;}
void zam_program_autorun_abort_prepared(void){aborted++;}
esp_err_t zam_program_autorun_launch(void){assert(prepared==1 && http_started && !aborted);launched++;if(!stop_requested)scan_calls++;return ESP_OK;}
void zam_program_indicator_ready(void){}
esp_err_t zam_http_init(void){return fail_http_init?ESP_FAIL:ESP_OK;}
esp_err_t zam_http_start(void){if(fail_http_start)return ESP_FAIL;http_started=1;if(stop_during_start){assert(prepared==1);stop_requested=1;}return ESP_OK;}
#include "../main/main.c"
static void reset(void){fail_console=fail_wifi=fail_user=fail_web=fail_program=fail_reg=fail_http_init=fail_http_start=0;prepared=launched=aborted=http_started=stop_requested=stop_during_start=console_registered=scan_calls=0;}
int main(void){
 reset();app_main();assert(prepared==1 && launched==1 && scan_calls==1 && !aborted);
 reset();stop_during_start=1;app_main();assert(prepared==1 && launched==1 && stop_requested && !scan_calls);
 for(int i=0;i<8;i++){reset();switch(i){case 0:fail_console=1;break;case 1:fail_wifi=1;break;case 2:fail_user=1;break;case 3:fail_web=1;break;case 4:fail_program=1;break;case 5:fail_reg=1;break;case 6:fail_http_init=1;break;default:fail_http_start=1;break;}app_main();assert(!launched);assert(!scan_calls);if(i>=5)assert(aborted==1);else assert(!prepared);}
 reset();app_main();assert(prepared==1 && launched==1 && scan_calls==1);
 puts("Boot readiness, one request, early STOP, failures and simulated reboot PASS");return 0;
}
