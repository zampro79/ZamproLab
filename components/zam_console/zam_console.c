#include "zam_console.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_console.h"
#include "zam_config.h"
#include "zam_system.h"
#include "zam_wifi.h"
#include "zam_storage.h"

static int print_value(const char *key)
{
    char value[ZAM_DEVICE_NAME_MAX + 1];
    esp_err_t err = zam_config_get(key, value, sizeof(value));
    if (err != ESP_OK) {
        printf("Cannot read %s: %s\n", key, esp_err_to_name(err));
        return 1;
    }
    printf("%s=%s\n", key, value);
    return 0;
}
static int config_cmd(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "list")) {
        int result = print_value("device_name");
        return print_value("boot_message") || result;
    }
    if (argc == 3 && !strcmp(argv[1], "get")) return print_value(argv[2]);
    esp_err_t err;
    if (argc == 4 && !strcmp(argv[1], "set")) {
        err = zam_config_set(argv[2], argv[3]);
    } else if (argc == 2 && !strcmp(argv[1], "reset")) {
        err = zam_config_reset();
    } else {
        puts("Usage: config list | get <key> | set <key> <value> | reset");
        return 1;
    }
    if (err != ESP_OK) {
        printf("Configuration error: %s (check key and value; NVS must be available)\n", esp_err_to_name(err));
        return 1;
    }
    puts("Configuration saved");
    return 0;
}

static char pending_ssid[33];
static bool pending_ap;
static void wifi_secret_done(esp_err_t result, const char *secret, void *context)
{
    (void)context;
    if (result != ESP_OK) { puts("Wi-Fi credential entry cancelled or too long"); memset(pending_ssid,0,sizeof(pending_ssid)); return; }
    esp_err_t err=pending_ap ? zam_wifi_set_ap(pending_ssid,secret) : zam_wifi_set_sta(pending_ssid,secret);
    memset(pending_ssid,0,sizeof(pending_ssid));
    if (err==ESP_OK) puts("Wi-Fi configuration request accepted; check wifi status");
    else printf("Wi-Fi configuration rejected: %s\n",esp_err_to_name(err));
}
static const char *mode_name(zam_wifi_mode_t mode)
{
    switch(mode) {case ZAM_WIFI_AP:return "ap";case ZAM_WIFI_STA:return "sta";case ZAM_WIFI_APSTA:return "apsta";case ZAM_WIFI_AUTO:return "auto";default:return "unknown";}
}
static const char *wifi_auth_name(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-Personal";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-Personal";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-Personal";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-Personal";
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
    case WIFI_AUTH_OWE: return "OWE";
    default: return "other";
    }
}
static int wifi_cmd(int argc,char **argv)
{
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if (argc==2 && !strcmp(argv[1],"status")) {
        zam_wifi_status_t st; err=zam_wifi_status(&st);
        if (err==ESP_OK) {
            printf("Configured: %s; effective: %s; fallback: %s\n",mode_name(st.configured),mode_name(st.effective),st.fallback?"yes":"no");
            printf("STA: %s%s; SSID: %s; IP: %s; reason: %u\n",st.connected?"connected":"not connected",st.paused?" (paused)":"",st.sta_ssid[0]?st.sta_ssid:"(unset)",st.sta_ip[0]?st.sta_ip:"-",st.disconnect_reason);
            printf("AP: %s; IP: %s; clients: %u\n",st.ap_ssid,st.ap_ip[0]?st.ap_ip:"-",st.clients);
            printf("Last operation: %s; scan: %s; results: %u; generation: %" PRIu32 "\n",esp_err_to_name(st.last_error),st.scanning?"running":esp_err_to_name(st.scan_error),st.scan_count,st.scan_generation);
            return 0;
        }
    } else if (argc==2 && !strcmp(argv[1],"scan")) err=zam_wifi_scan();
    else if (argc==3 && !strcmp(argv[1],"scan") && !strcmp(argv[2],"results")) {
        zam_wifi_scan_item_t records[ZAM_WIFI_SCAN_MAX];size_t count;uint32_t gen;
        err=zam_wifi_scan_results(records,ZAM_WIFI_SCAN_MAX,&count,&gen);
        if (err==ESP_OK) {
            printf("Scan generation %" PRIu32 ": %u result(s)\n",gen,(unsigned)count);
            for (size_t i=0;i<count;i++) printf("%s RSSI %d channel %u auth %s (%d)\n",records[i].ssid,records[i].rssi,records[i].channel,wifi_auth_name(records[i].auth),records[i].auth);
            return 0;
        }
    } else if (argc==3 && !strcmp(argv[1],"mode")) {
        zam_wifi_mode_t mode=!strcmp(argv[2],"ap")?ZAM_WIFI_AP:!strcmp(argv[2],"sta")?ZAM_WIFI_STA:!strcmp(argv[2],"apsta")?ZAM_WIFI_APSTA:!strcmp(argv[2],"auto")?ZAM_WIFI_AUTO:0;
        err=zam_wifi_set_mode(mode);
    } else if (argc==2 && !strcmp(argv[1],"connect")) err=zam_wifi_connect();
    else if (argc==2 && !strcmp(argv[1],"disconnect")) err=zam_wifi_disconnect();
    else if (argc==4 && !strcmp(argv[2],"set") && (!strcmp(argv[1],"sta") || !strcmp(argv[1],"ap"))) {
        if (!argv[3][0] || strlen(argv[3])>32) { puts("SSID must have 1-32 bytes"); return 1; }
        strlcpy(pending_ssid,argv[3],sizeof(pending_ssid)); pending_ap=!strcmp(argv[1],"ap");
        err=zam_console_request_secret(64,wifi_secret_done,NULL);
        if (err==ESP_OK) return 0;
        memset(pending_ssid,0,sizeof(pending_ssid));
    } else {
        puts("Usage: wifi status | scan [results] | mode auto|ap|sta|apsta | sta|ap set <ssid> | connect | disconnect");
        return 1;
    }
    if (err!=ESP_OK) { printf("Wi-Fi request rejected: %s\n",esp_err_to_name(err)); return 1; }
    puts("Wi-Fi request accepted; check wifi status"); return 0;
}

static const char *storage_state(zam_storage_state_t state)
{
    static const char *const names[]={"not initialized","mounted","partition missing","uninitialized/erased",
        "invalid or corrupt","mount error","access error","unmounted"};
    return state>=ZAM_STORAGE_NOT_INITIALIZED && state<=ZAM_STORAGE_UNMOUNTED ? names[state] : "unknown";
}
static int storage_cmd(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"status")) {
        for (int i=0;i<ZAM_STORAGE_COUNT;i++) {
            zam_storage_info_t info; esp_err_t err=zam_storage_get_info(i,&info);
            if (err!=ESP_OK) { printf("Storage unavailable: %s\n",esp_err_to_name(err)); return 1; }
            if (info.state==ZAM_STORAGE_MOUNTED)
                printf("%s: %s; total=%zu used=%zu free=%zu; error=%s\n",
                       i==ZAM_STORAGE_WEB?"webfs":"userfs",storage_state(info.state),
                       info.total,info.used,info.free_bytes,esp_err_to_name(info.error));
            else
                printf("%s: %s; partition=%zu used/free=unavailable; error=%s\n",
                       i==ZAM_STORAGE_WEB?"webfs":"userfs",storage_state(info.state),
                       info.total,esp_err_to_name(info.error));
        }
        return 0;
    }
    if (argc==3) {
        esp_err_t err;
        if (!strcmp(argv[1],"test-create")) err=zam_storage_probe_create(argv[2]);
        else if (!strcmp(argv[1],"test-check")) err=zam_storage_probe_check(argv[2]);
        else if (!strcmp(argv[1],"test-delete")) err=zam_storage_probe_delete(argv[2]);
        else { puts("Unknown storage operation");return 1; }
        printf("Storage diagnostic: %s\n",esp_err_to_name(err));return err==ESP_OK ? 0 : 1;
    }
    puts("Usage: storage status | test-create|test-check|test-delete <32-hex-token>");return 1;
}

#define LINE_CAP 256
#define SECRET_CAP 65
#define HISTORY_CAP 16
#define PROMPT "zamprolab> "
static const char *TAG = "zam_console";
static TaskHandle_t console_task;
static bool dispatching, secret_pending;
static zam_console_secret_cb_t secret_cb;
static void *secret_context;
static size_t secret_capacity;
static char history[HISTORY_CAP][LINE_CAP];
static unsigned history_count;

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

esp_err_t zam_console_request_secret(size_t capacity, zam_console_secret_cb_t cb, void *context)
{
    if (xTaskGetCurrentTaskHandle() != console_task || !dispatching || secret_pending) return ESP_ERR_INVALID_STATE;
    if (!cb || capacity < 1 || capacity > SECRET_CAP) return ESP_ERR_INVALID_ARG;
    secret_cb = cb; secret_context = context; secret_capacity = capacity; secret_pending = true;
    return ESP_OK;
}
static void secret_test_done(esp_err_t result, const char *secret, void *context)
{
    (void)secret; (void)context;
    printf("Secret test: %s\n", result == ESP_OK ? "completed" : result == ESP_ERR_INVALID_SIZE ? "too long" : "cancelled or input error");
}
static int console_cmd(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "secret-test")) { puts("Usage: console secret-test"); return 1; }
    esp_err_t err = zam_console_request_secret(SECRET_CAP, secret_test_done, NULL);
    if (err != ESP_OK) { printf("Secret request failed: %s\n", esp_err_to_name(err)); return 1; }
    return 0;
}
static void redraw(const char *line, size_t len, size_t cursor)
{
    printf("\r\033[2K%s%s", PROMPT, line);
    if (len > cursor) printf("\033[%uD", (unsigned)(len-cursor));
    fflush(stdout);
}
static void console_loop(void *arg)
{
    (void)arg;
    char line[LINE_CAP] = {0}, secret[SECRET_CAP] = {0};
    size_t len=0, cursor=0, secret_len=0;
    unsigned esc=0, hist=0;
    bool secret_mode=false, overflow=false, swallow_lf=false;
    printf("%s", PROMPT); fflush(stdout);
    for (;;) {
        uint8_t c;
        int n=uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, portMAX_DELAY);
        if (n != 1) {
            ESP_LOGE(TAG, "UART read failed");
            if (secret_mode) { wipe(secret, sizeof(secret)); secret_len=0; overflow=false; secret_mode=false; puts(""); secret_cb(ESP_FAIL, NULL, secret_context); printf("%s", PROMPT); fflush(stdout); }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (swallow_lf) { swallow_lf=false; if (c=='\n') continue; }
        if (c=='\r' || c=='\n') {
            if (c=='\r') swallow_lf=true;
            if (secret_mode) {
                esp_err_t result=overflow ? ESP_ERR_INVALID_SIZE : ESP_OK;
                secret[secret_len]=0; puts("");
                secret_cb(result, result==ESP_OK ? secret : NULL, secret_context);
                wipe(secret,sizeof(secret)); secret_len=0; secret_mode=false; overflow=false;
                printf("%s",PROMPT); fflush(stdout); continue;
            }
            puts("");
            if (overflow) puts("Command too long");
            else if (len) {
                line[len]=0;
                if (history_count==HISTORY_CAP) { memmove(history,history+1,(HISTORY_CAP-1)*LINE_CAP); history_count--; }
                memcpy(history[history_count++],line,len+1);
                int ret=0; dispatching=true; esp_err_t err=esp_console_run(line,&ret); dispatching=false;
                if (err==ESP_ERR_NOT_FOUND) puts("Unrecognized command");
                else if (err!=ESP_OK && err!=ESP_ERR_INVALID_ARG) printf("Console error: %s\n",esp_err_to_name(err));
                else if (err==ESP_OK && ret) printf("Command returned error: %d\n",ret);
            }
            wipe(line,sizeof(line));len=cursor=0;esc=0;overflow=false;hist=history_count;
            if (secret_pending) { secret_pending=false; secret_mode=true; secret_len=0; printf("Secret: "); }
            else printf("%s", PROMPT);
            fflush(stdout); continue;
        }
        if (secret_mode) {
            if (c==3) { wipe(secret,sizeof(secret)); secret_len=0; secret_mode=false; overflow=false; puts(""); secret_cb(ESP_ERR_INVALID_STATE,NULL,secret_context); printf("%s",PROMPT); fflush(stdout); }
            else if (c==8 || c==127) { if (!overflow && secret_len) secret[--secret_len]=0; }
            else if (c>=32 && c<=126) { if (secret_len+1>=secret_capacity) overflow=true; else if (!overflow) secret[secret_len++]=c; }
            else overflow=true;
            continue;
        }
        if (overflow) continue;
        if (esc==1) { esc=(c=='[') ? 2 : 0; continue; }
        if (esc==2) {
            esc=0;
            if (c=='A' && hist>0) { hist--; strlcpy(line,history[hist],sizeof(line)); len=cursor=strlen(line); }
            else if (c=='B' && hist<history_count) { hist++; if (hist==history_count) line[0]=0; else strlcpy(line,history[hist],sizeof(line)); len=cursor=strlen(line); }
            else if (c=='C' && cursor<len) cursor++;
            else if (c=='D' && cursor>0) cursor--;
            redraw(line,len,cursor); continue;
        }
        if (c==27) { esc=1; continue; }
        if (c==8 || c==127) { if (cursor) { memmove(line+cursor-1,line+cursor,len-cursor+1); cursor--; len--; redraw(line,len,cursor); } continue; }
        if (c>=32 && c<=126) {
            if (len+1>=sizeof(line)) { overflow=true; continue; }
            memmove(line+cursor+1,line+cursor,len-cursor+1); line[cursor++]=(char)c; len++; redraw(line,len,cursor);
        }
    }
}

esp_err_t zam_console_start(void)
{
    esp_console_config_t config=ESP_CONSOLE_CONFIG_DEFAULT();
    esp_err_t err=esp_console_init(&config);
    if (err!=ESP_OK) return err;
    err=esp_console_register_help_command();
    if (err==ESP_OK) err=zam_system_register_commands();
    if (err==ESP_OK) {
        const esp_console_cmd_t cmd={.command="config",.help="Manage persistent configuration",.func=config_cmd};
        err=esp_console_cmd_register(&cmd);
    }
    if (err==ESP_OK) {
        const esp_console_cmd_t cmd={.command="console",.help="Console diagnostics: secret-test",.func=console_cmd};
        err=esp_console_cmd_register(&cmd);
    }
    if (err==ESP_OK) {
        const esp_console_cmd_t cmd={.command="wifi",.help="Wi-Fi status and control",.func=wifi_cmd};
        err=esp_console_cmd_register(&cmd);
    }
    if (err==ESP_OK) {
        const esp_console_cmd_t cmd={.command="storage",.help="Storage status and controlled probe",.func=storage_cmd};
        err=esp_console_cmd_register(&cmd);
    }
    if (err!=ESP_OK) { esp_console_deinit(); return err; }
    fflush(stdout);
    err=uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,1024,0,0,NULL,0);
    if (err!=ESP_OK) { esp_console_deinit(); return err; }
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    if (xTaskCreate(console_loop,"zam_console",4096,NULL,2,&console_task)!=pdPASS) {
        uart_vfs_dev_use_nonblocking(CONFIG_ESP_CONSOLE_UART_NUM); uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM); esp_console_deinit(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
