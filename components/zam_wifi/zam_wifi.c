#include "zam_wifi.h"
#include "zam_wifi_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "zam_config.h"

#define TAG "zam_wifi"
#define SEC_US 1000000LL
#define SCAN_TIMEOUT_MS 15000
typedef enum { JOB_COMPOSED, JOB_MODE, JOB_AP, JOB_STA, JOB_CONNECT, JOB_DISCONNECT, JOB_SCAN, JOB_LINK_DOWN, JOB_GOT_IP } job_kind_t;
typedef struct { job_kind_t kind; zam_wifi_mode_t mode; char ssid[33]; char *password; uint16_t reason; char ip[16]; bool enabled; zam_wifi_password_action_t password_action; } job_t;
static QueueHandle_t jobs, link_events, scan_events;
static SemaphoreHandle_t guard;
static zam_config_wifi_t cfg;
static zam_wifi_status_t status;
static zam_wifi_scan_item_t scan_items[ZAM_WIFI_SCAN_MAX];
static esp_netif_t *ap_netif, *sta_netif;
static bool started, ever_init;
static int64_t lost_since, next_try;
static unsigned retries;
static bool command_busy, scan_quarantine;
static uint32_t highest_operation_id;
static zam_wifi_operation_t operation;

static void memory_log(const char *phase)
{
    ESP_LOGI(TAG,"%s heap=%zu internal=%zu largest=%zu minimum=%zu",phase,
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}
static void wipe(void *p, size_t n) { volatile unsigned char *v=p; while (n--) *v++=0; }
static void lock(void) { xSemaphoreTake(guard,portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(guard); }
static void report(esp_err_t err) { lock(); status.last_error=err; unlock(); if (err!=ESP_OK) ESP_LOGE(TAG,"operation failed: %s",esp_err_to_name(err)); }
static bool printable(const char *s, size_t min, size_t max)
{
    if (!s) return false;
    size_t n=strnlen(s,max+1);
    if (n<min || n>max) return false;
    for (size_t i=0;i<n;i++) if ((unsigned char)s[i]<32 || (unsigned char)s[i]>126) return false;
    return true;
}
/* SSIDs are byte strings; accept UTF-8 and other non-control bytes. */
static bool valid_ssid(const char *s, bool allow_empty)
{
    if (!s) return false;
    size_t n=strnlen(s,33);
    if (n>32 || (!allow_empty && !n)) return false;
    for (size_t i=0;i<n;i++) if ((unsigned char)s[i]<32 || (unsigned char)s[i]==127) return false;
    return true;
}
static bool valid(const zam_config_wifi_t *c)
{
    return c->version==1 && c->mode>=ZAM_WIFI_AP && c->mode<=ZAM_WIFI_AUTO &&
        valid_ssid(c->ap_ssid,false) && printable(c->ap_password,8,63) &&
        valid_ssid(c->sta_ssid,true) && printable(c->sta_password,0,63) &&
        (!c->sta_password[0] || strlen(c->sta_password)>=8);
}
static esp_err_t enqueue(job_t *job)
{
    if (!jobs) return ESP_ERR_INVALID_STATE;
    lock();
    if (command_busy) { unlock(); return ESP_ERR_INVALID_STATE; }
    command_busy=true;
    if (xQueueSend(jobs,job,0)!=pdTRUE) { command_busy=false; unlock(); return ESP_ERR_NO_MEM; }
    unlock(); return ESP_OK;
}
static void event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; job_t j={0};
    if (base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t done=*(wifi_event_sta_scan_done_t *)data;
        xQueueOverwrite(scan_events,&done);
        return;
    }
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        j.kind=JOB_LINK_DOWN; j.reason=((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        j.kind=JOB_LINK_DOWN;
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        j.kind=JOB_GOT_IP;
        const ip_event_got_ip_t *ev=data;
        snprintf(j.ip,sizeof(j.ip),IPSTR,IP2STR(&ev->ip_info.ip));
    } else return;
    /* Latest connectivity event wins; command traffic cannot fill this queue. */
    xQueueOverwrite(link_events,&j);
}
static esp_err_t driver_config(zam_wifi_mode_t mode)
{
    wifi_config_t ap={0}, sta={0};
    strlcpy((char *)ap.ap.ssid,cfg.ap_ssid,sizeof(ap.ap.ssid));
    /* The IDF SSID array is 32 bytes, without a required terminator. */
    memcpy(ap.ap.ssid,cfg.ap_ssid,strlen(cfg.ap_ssid)); ap.ap.ssid_len=strlen(cfg.ap_ssid);
    strlcpy((char *)ap.ap.password,cfg.ap_password,sizeof(ap.ap.password));
    ap.ap.channel=1; ap.ap.max_connection=4; ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
    memcpy(sta.sta.ssid,cfg.sta_ssid,strlen(cfg.sta_ssid));
    strlcpy((char *)sta.sta.password,cfg.sta_password,sizeof(sta.sta.password));
    sta.sta.threshold.authmode=WIFI_AUTH_OPEN;
    esp_err_t err=ESP_OK;
    if (mode!=ZAM_WIFI_STA) err=esp_wifi_set_config(WIFI_IF_AP,&ap);
    if (err==ESP_OK && mode!=ZAM_WIFI_AP) err=esp_wifi_set_config(WIFI_IF_STA,&sta);
    wipe(&ap,sizeof(ap)); wipe(&sta,sizeof(sta)); return err;
}
static esp_err_t apply_mode(zam_wifi_mode_t mode)
{
    wifi_mode_t target=mode==ZAM_WIFI_AP ? WIFI_MODE_AP : mode==ZAM_WIFI_STA ? WIFI_MODE_STA : WIFI_MODE_APSTA;
    esp_err_t err=esp_wifi_set_mode(target);
    if (err==ESP_OK) {
        lock(); status.effective=mode; if (mode==ZAM_WIFI_AP) {status.connected=false; status.sta_ip[0]=0;} unlock();
    }
    return err;
}
static void update_names(void)
{
    lock(); status.configured=cfg.mode; strlcpy(status.ap_ssid,cfg.ap_ssid,sizeof(status.ap_ssid));
    strlcpy(status.sta_ssid,cfg.sta_ssid,sizeof(status.sta_ssid)); unlock();
}
static esp_err_t save_candidate(zam_config_wifi_t *candidate)
{
    if (!valid(candidate)) return ESP_ERR_INVALID_ARG;
    esp_err_t err=zam_config_wifi_save(candidate);
    if (err==ESP_OK) { cfg=*candidate; update_names(); }
    return err;
}
static void reset_retry(void) { lost_since=esp_timer_get_time(); next_try=lost_since; retries=0; }
static void request_attempt(void)
{
    lock(); bool active=status.configured!=ZAM_WIFI_AP && !status.paused && !status.connected; unlock();
    if (!active || !cfg.sta_ssid[0]) return;
    int64_t now=esp_timer_get_time();
    if (now<next_try) return;
    esp_err_t err=esp_wifi_connect();
    if (err!=ESP_OK && err!=ESP_ERR_WIFI_CONN) report(err);
    unsigned delay=retries<5 ? 1U<<retries : 30U; if (delay>30) delay=30;
    if (retries<5) retries++;
    next_try=now+(int64_t)delay*SEC_US;
}
static void fallback_check(void)
{
    lock(); bool needed=status.configured==ZAM_WIFI_STA && !status.connected && !status.fallback; unlock();
    if (!needed || (cfg.sta_ssid[0] && esp_timer_get_time()-lost_since<30*SEC_US)) return;
    esp_err_t err=apply_mode(ZAM_WIFI_APSTA);
    if (err==ESP_OK) err=driver_config(ZAM_WIFI_AP);
    if (err==ESP_OK) { lock();status.fallback=true;unlock(); ESP_LOGW(TAG,"recovery AP enabled"); }
    else report(err);
}
/* The event has no reliable caller-supplied ID. After timeout, block new scans
 * until the stopped scan's completion event is drained; never attribute a late
 * completion to a newer request. Other Wi-Fi commands remain available. */
static void scan_drain_late(void)
{
    wifi_event_sta_scan_done_t done;
    lock(); bool waiting=scan_quarantine; unlock();
    if (waiting && xQueueReceive(scan_events,&done,0)==pdTRUE) {
        esp_wifi_clear_ap_list();
        lock(); scan_quarantine=false; unlock();
    }
}
static void do_scan(void)
{
    lock(); bool ap_only=status.effective==ZAM_WIFI_AP; unlock();
    esp_err_t err=ap_only ? apply_mode(ZAM_WIFI_APSTA) : ESP_OK;
    bool started_scan=false;
    wifi_scan_config_t opts={.show_hidden=true};
    if (err==ESP_OK) { err=esp_wifi_scan_start(&opts,false); started_scan=err==ESP_OK; }
    wifi_event_sta_scan_done_t done={0};
    if (started_scan) {
        if (xQueueReceive(scan_events,&done,pdMS_TO_TICKS(SCAN_TIMEOUT_MS))!=pdTRUE) {
            lock(); scan_quarantine=true; unlock();
            esp_err_t stop=esp_wifi_scan_stop();
            if (stop!=ESP_OK) ESP_LOGW(TAG,"scan stop failed: %s",esp_err_to_name(stop));
            err=ESP_ERR_TIMEOUT;
        } else if (done.status!=0) err=ESP_FAIL;
    }
    wifi_ap_record_t records[ZAM_WIFI_SCAN_MAX]; uint16_t n=ZAM_WIFI_SCAN_MAX;
    if (err==ESP_OK) err=esp_wifi_scan_get_ap_records(&n,records);
    else if (started_scan && err!=ESP_ERR_TIMEOUT) esp_wifi_clear_ap_list();
    if (ap_only) { esp_err_t restore=apply_mode(ZAM_WIFI_AP); if (restore!=ESP_OK) { ESP_LOGW(TAG,"scan AP mode restore failed: %s",esp_err_to_name(restore)); if (err==ESP_OK) err=restore; } }
    lock(); status.scan_count=0;
    if (err==ESP_OK) {
        if (n>ZAM_WIFI_SCAN_MAX) n=ZAM_WIFI_SCAN_MAX;
        for (unsigned i=0;i<n;i++) {
            memcpy(scan_items[i].ssid,records[i].ssid,32); scan_items[i].ssid[32]=0;
            scan_items[i].rssi=records[i].rssi; scan_items[i].channel=records[i].primary; scan_items[i].auth=records[i].authmode;
        }
        status.scan_count=n;
    }
    status.scan_error=err; status.scanning=false; unlock();
    if (err!=ESP_OK) ESP_LOGW(TAG,"scan failed: %s",esp_err_to_name(err));
    memory_log("after scan");
}
static void work(void *arg)
{
    (void)arg; job_t job; reset_retry(); fallback_check(); request_attempt();
    for (;;) {
        scan_drain_late();
        if (xQueueReceive(link_events,&job,0)==pdTRUE || xQueueReceive(jobs,&job,pdMS_TO_TICKS(250))==pdTRUE) {
            esp_err_t err=ESP_OK;
            if (job.kind==JOB_GOT_IP) {
                lock();if (!status.paused && status.configured!=ZAM_WIFI_AP) {status.connected=true;strlcpy(status.sta_ip,job.ip,sizeof(status.sta_ip)); if (!operation.superseded && operation.application==ZAM_WIFI_APPLY_APPLIED && operation.connection!=ZAM_WIFI_CONNECTION_NA) operation.connection=ZAM_WIFI_CONNECTION_CONNECTED;} unlock(); retries=0;
            } else if (job.kind==JOB_LINK_DOWN) {
                lock();bool was=status.connected;status.connected=false;status.sta_ip[0]=0;status.disconnect_reason=job.reason; if (!operation.superseded && operation.application==ZAM_WIFI_APPLY_APPLIED && operation.connection!=ZAM_WIFI_CONNECTION_NA) { operation.connection=ZAM_WIFI_CONNECTION_NOT_CONNECTED; operation.disconnect_reason=job.reason; } unlock();
                if (was) reset_retry();
            } else if (job.kind==JOB_SCAN) do_scan();
            else if (job.kind==JOB_COMPOSED) {
                zam_config_wifi_t candidate=cfg;
                candidate.mode=job.mode;
                if (job.enabled) {
                    strlcpy(candidate.sta_ssid,job.ssid,sizeof(candidate.sta_ssid));
                    if (job.password_action==ZAM_WIFI_PASSWORD_REPLACE) strlcpy(candidate.sta_password,job.password,sizeof(candidate.sta_password));
                    else if (job.password_action==ZAM_WIFI_PASSWORD_OPEN) candidate.sta_password[0]=0;
                }
                err=save_candidate(&candidate); wipe(&candidate,sizeof(candidate));
                lock(); operation.persistence=err==ESP_OK?ZAM_WIFI_PERSIST_SAVED:ZAM_WIFI_PERSIST_FAILED;
                if (err!=ESP_OK) {operation.application=ZAM_WIFI_APPLY_NOT_ATTEMPTED;operation.error=err;} unlock();
                if (err==ESP_OK) {
                    lock(); status.fallback=false;status.paused=false;status.connected=false;status.sta_ip[0]=0;unlock();
                    err=esp_wifi_disconnect();
                    if (err==ESP_ERR_WIFI_NOT_CONNECT) err=ESP_OK;
                    if (err==ESP_OK) err=apply_mode(job.enabled?ZAM_WIFI_APSTA:ZAM_WIFI_AP);
                    if (err==ESP_OK) err=driver_config(job.enabled?ZAM_WIFI_APSTA:ZAM_WIFI_AP);
                    lock(); operation.application=err==ESP_OK?ZAM_WIFI_APPLY_APPLIED:ZAM_WIFI_APPLY_FAILED;
                    operation.error=err;operation.connection=(err==ESP_OK && job.enabled)?ZAM_WIFI_CONNECTION_CONNECTING:ZAM_WIFI_CONNECTION_NA; unlock();
                    if (err==ESP_OK) reset_retry();
                    else { lock(); status.paused=true; unlock(); }
                }
            }
            else if (job.kind==JOB_DISCONNECT) {
                lock();status.paused=true;status.connected=false;status.sta_ip[0]=0;unlock();
                err=esp_wifi_disconnect(); if (err==ESP_ERR_WIFI_NOT_CONNECT) err=ESP_OK;
            } else if (job.kind==JOB_CONNECT) {
                lock();status.paused=false;bool ap_only=status.configured==ZAM_WIFI_AP;unlock();
                if (ap_only || !cfg.sta_ssid[0]) err=ESP_ERR_INVALID_STATE; else reset_retry();
            } else {
                zam_config_wifi_t candidate=cfg;
                if (job.kind==JOB_MODE) candidate.mode=job.mode;
                else if (job.kind==JOB_AP) {strlcpy(candidate.ap_ssid,job.ssid,sizeof(candidate.ap_ssid));strlcpy(candidate.ap_password,job.password,sizeof(candidate.ap_password));}
                else if (job.kind==JOB_STA) {strlcpy(candidate.sta_ssid,job.ssid,sizeof(candidate.sta_ssid));strlcpy(candidate.sta_password,job.password,sizeof(candidate.sta_password));}
                err=save_candidate(&candidate); wipe(&candidate,sizeof(candidate));
                if (err==ESP_OK && job.kind==JOB_MODE) {
                    lock();status.fallback=false;status.paused=false;status.connected=false;status.sta_ip[0]=0;unlock();
                    esp_wifi_disconnect();
                    zam_wifi_mode_t effective=zam_wifi_resolve_mode(cfg.mode,cfg.sta_ssid[0]!=0);
                    err=apply_mode(effective); if (err==ESP_OK) err=driver_config(effective); reset_retry();
                } else if (err==ESP_OK) {
                    lock(); zam_wifi_mode_t active=status.effective; unlock();
                    if (job.kind==JOB_STA && cfg.mode==ZAM_WIFI_AUTO && active==ZAM_WIFI_AP) {
                        err=apply_mode(ZAM_WIFI_APSTA);
                        if (err==ESP_OK) active=ZAM_WIFI_APSTA;
                    }
                    if (err==ESP_OK) err=driver_config(active);
                    if (job.kind==JOB_STA && active!=ZAM_WIFI_AP) {esp_wifi_disconnect();reset_retry();}
                }
            }
            if (job.kind!=JOB_COMPOSED && job.kind!=JOB_SCAN && job.kind!=JOB_LINK_DOWN && job.kind!=JOB_GOT_IP) { lock(); if (operation.accepted) operation.superseded=true; unlock(); }
            if (err!=ESP_OK) report(err);
            else if (job.kind!=JOB_LINK_DOWN && job.kind!=JOB_GOT_IP && job.kind!=JOB_SCAN) report(ESP_OK);
            if (job.kind!=JOB_LINK_DOWN && job.kind!=JOB_GOT_IP) { lock(); command_busy=false; unlock(); }
            if (job.password) { wipe(job.password,64); free(job.password); }
            wipe(&job,sizeof(job));
        }
        fallback_check(); request_attempt();
    }
}
esp_err_t zam_wifi_init(void)
{
    if (ever_init) return ESP_ERR_INVALID_STATE;
    ever_init=true;
    memory_log("before Wi-Fi");
    bool loop_ready=false, wifi_ready=false, wifi_started=false;
    bool wifi_handler=false, scan_handler=false, got_handler=false, lost_handler=false;
    uint8_t mac[6]; esp_err_t err=esp_read_mac(mac,ESP_MAC_BASE);
    if (err!=ESP_OK) return err;
    memset(&cfg,0,sizeof(cfg)); cfg.version=1; cfg.mode=ZAM_WIFI_AUTO;
    snprintf(cfg.ap_ssid,sizeof(cfg.ap_ssid),"ZamproLab-%02X%02X%02X",mac[3],mac[4],mac[5]);
    strlcpy(cfg.ap_password,"ZamproLab",sizeof(cfg.ap_password));
    zam_config_wifi_t saved={0}; err=zam_config_wifi_load(&saved);
    if (err==ESP_OK && valid(&saved)) cfg=saved;
    else if (err!=ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG,"Wi-Fi configuration unavailable or invalid; using recovery defaults");
    wipe(&saved,sizeof(saved));
    guard=xSemaphoreCreateMutex(); jobs=xQueueCreate(16,sizeof(job_t)); link_events=xQueueCreate(1,sizeof(job_t)); scan_events=xQueueCreate(1,sizeof(wifi_event_sta_scan_done_t));
    if (!guard || !jobs || !link_events || !scan_events) { err=ESP_ERR_NO_MEM; goto fail; }
    update_names();
    err=esp_netif_init(); if (err!=ESP_OK) goto fail;
    err=esp_event_loop_create_default(); if (err!=ESP_OK) goto fail;
    loop_ready=true;
    ap_netif=esp_netif_create_default_wifi_ap(); sta_netif=esp_netif_create_default_wifi_sta();
    if (!ap_netif || !sta_netif) { err=ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT(); err=esp_wifi_init(&init); if (err!=ESP_OK) goto fail;
    wifi_ready=true;
    memory_log("after driver init");
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err!=ESP_OK) goto fail;
    err=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event_cb,NULL); if (err!=ESP_OK) goto fail;
    wifi_handler=true;
    err=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,event_cb,NULL); if (err!=ESP_OK) goto fail;
    scan_handler=true;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_cb,NULL); if (err!=ESP_OK) goto fail;
    got_handler=true;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,event_cb,NULL); if (err!=ESP_OK) goto fail;
    lost_handler=true;
    err=esp_wifi_set_mode(WIFI_MODE_APSTA); if (err!=ESP_OK) goto fail;
    err=driver_config(ZAM_WIFI_APSTA); if (err!=ESP_OK) goto fail;
    err=apply_mode(zam_wifi_resolve_mode(cfg.mode,cfg.sta_ssid[0]!=0)); if (err!=ESP_OK) goto fail;
    err=esp_wifi_start(); if (err!=ESP_OK) goto fail;
    wifi_started=true; started=true;
    memory_log("after Wi-Fi start");
    if (xTaskCreate(work,"zam_wifi",6144,NULL,4,NULL)!=pdPASS) { err=ESP_ERR_NO_MEM; goto fail; }
    return ESP_OK;
fail:
    started=false;
    if (wifi_started) esp_wifi_stop();
    if (lost_handler) esp_event_handler_unregister(IP_EVENT,IP_EVENT_STA_LOST_IP,event_cb);
    if (got_handler) esp_event_handler_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,event_cb);
    if (scan_handler) esp_event_handler_unregister(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,event_cb);
    if (wifi_handler) esp_event_handler_unregister(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event_cb);
    if (wifi_ready) esp_wifi_deinit();
    if (sta_netif) { esp_netif_destroy_default_wifi(sta_netif); sta_netif=NULL; }
    if (ap_netif) { esp_netif_destroy_default_wifi(ap_netif); ap_netif=NULL; }
    if (loop_ready) esp_event_loop_delete_default();
    if (scan_events) { vQueueDelete(scan_events); scan_events=NULL; }
    if (link_events) { vQueueDelete(link_events); link_events=NULL; }
    if (jobs) { vQueueDelete(jobs); jobs=NULL; }
    if (guard) { vSemaphoreDelete(guard); guard=NULL; }
    wipe(&cfg,sizeof(cfg));
    return err;
}
esp_err_t zam_wifi_status(zam_wifi_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    lock(); *out=status; unlock();
    if (out->effective!=ZAM_WIFI_STA) {
        esp_netif_ip_info_t ip; if (esp_netif_get_ip_info(ap_netif,&ip)==ESP_OK) snprintf(out->ap_ip,sizeof(out->ap_ip),IPSTR,IP2STR(&ip.ip));
        wifi_sta_list_t clients; if (esp_wifi_ap_get_sta_list(&clients)==ESP_OK) out->clients=clients.num;
    }
    return ESP_OK;
}
esp_err_t zam_wifi_set_mode(zam_wifi_mode_t mode)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (mode<ZAM_WIFI_AP || mode>ZAM_WIFI_AUTO) return ESP_ERR_INVALID_ARG;
    job_t j={.kind=JOB_MODE,.mode=mode}; return enqueue(&j);
}
static esp_err_t credentials(job_kind_t kind,const char *ssid,const char *password)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!valid_ssid(ssid,false) || !printable(password,kind==JOB_AP ? 8 : 0,63) || (kind==JOB_STA && password[0] && strlen(password)<8)) return ESP_ERR_INVALID_ARG;
    job_t j={.kind=kind}; strlcpy(j.ssid,ssid,sizeof(j.ssid));
    j.password=calloc(1,64); if (!j.password) return ESP_ERR_NO_MEM;
    strlcpy(j.password,password,64);
    esp_err_t err=enqueue(&j);
    if (err!=ESP_OK) { wipe(j.password,64); free(j.password); }
    wipe(&j,sizeof(j)); return err;
}
esp_err_t zam_wifi_set_ap(const char *ssid,const char *password) { return credentials(JOB_AP,ssid,password); }
esp_err_t zam_wifi_set_sta(const char *ssid,const char *password) { return credentials(JOB_STA,ssid,password); }
static esp_err_t simple(job_kind_t kind) { if (!started) return ESP_ERR_INVALID_STATE; job_t j={.kind=kind}; return enqueue(&j); }
esp_err_t zam_wifi_connect(void) { return simple(JOB_CONNECT); }
esp_err_t zam_wifi_disconnect(void) { return simple(JOB_DISCONNECT); }
esp_err_t zam_wifi_scan_start(uint32_t *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    job_t j={.kind=JOB_SCAN};
    lock();
    if (command_busy || scan_quarantine || status.scan_generation==UINT32_MAX) { unlock(); return ESP_ERR_INVALID_STATE; }
    command_busy=true;
    if (xQueueSend(jobs,&j,0)!=pdTRUE) { command_busy=false;unlock();return ESP_ERR_NO_MEM; }
    status.scanning=true; status.scan_count=0;
    *id=++status.scan_generation; status.scan_error=ESP_OK;
    unlock(); return ESP_OK;
}
esp_err_t zam_wifi_scan(void) { uint32_t id; return zam_wifi_scan_start(&id); }
esp_err_t zam_wifi_scan_get(uint32_t id,zam_wifi_scan_item_t *out,size_t capacity,size_t *count,bool *pending)
{
    if (!id || !out || !count || !pending) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    lock();
    if (id!=status.scan_generation) { unlock();return ESP_ERR_NOT_FOUND; }
    *pending=status.scanning;
    size_t n=status.scan_count<capacity ? status.scan_count:capacity;
    memcpy(out,scan_items,n*sizeof(*out)); *count=n;
    esp_err_t err=status.scanning ? ESP_OK : status.scan_error;
    unlock();return err;
}
esp_err_t zam_wifi_scan_results(zam_wifi_scan_item_t *out,size_t capacity,size_t *count,uint32_t *generation)
{
    if (!out || !count || !generation) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    lock(); size_t n=status.scan_count<capacity ? status.scan_count : capacity;
    memcpy(out,scan_items,n*sizeof(*out)); *count=n; *generation=status.scan_generation; esp_err_t err=status.scan_error; unlock(); return err;
}

esp_err_t zam_wifi_configure_sta(const zam_wifi_sta_request_t *request)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!request || !request->id) return ESP_ERR_INVALID_ARG;
    if (request->enabled && (!valid_ssid(request->ssid,false) ||
        (request->password_action<ZAM_WIFI_PASSWORD_KEEP || request->password_action>ZAM_WIFI_PASSWORD_OPEN) ||
        (request->password_action==ZAM_WIFI_PASSWORD_REPLACE && (!printable(request->password,8,63))))) return ESP_ERR_INVALID_ARG;
    job_t job={.kind=JOB_COMPOSED,.enabled=request->enabled,.password_action=request->password_action};
    if (request->enabled) strlcpy(job.ssid,request->ssid,sizeof(job.ssid));
    if (request->enabled && request->password_action==ZAM_WIFI_PASSWORD_REPLACE) {
        job.password=calloc(1,64); if (!job.password) return ESP_ERR_NO_MEM;
        strlcpy(job.password,request->password,64);
    }
    lock();
    esp_err_t err=ESP_OK;
    if (command_busy || request->id<=highest_operation_id) err=ESP_ERR_INVALID_STATE;
    else if (!zam_wifi_sta_transition(status.configured,request->enabled,request->confirm_sta_only,&job.mode)) err=ESP_ERR_INVALID_STATE;
    else if (request->enabled && request->password_action==ZAM_WIFI_PASSWORD_KEEP && !cfg.sta_ssid[0]) err=ESP_ERR_INVALID_STATE;
    else {
        command_busy=true;
        zam_wifi_operation_t previous=operation;
        operation=(zam_wifi_operation_t){.id=request->id,.accepted=true,.persistence=ZAM_WIFI_PERSIST_PENDING,.application=ZAM_WIFI_APPLY_PENDING,.connection=ZAM_WIFI_CONNECTION_NA};
        if (xQueueSend(jobs,&job,0)!=pdTRUE) { command_busy=false;operation=previous;err=ESP_ERR_NO_MEM; }
        else highest_operation_id=request->id;
    }
    unlock();
    if (err!=ESP_OK && job.password) { wipe(job.password,64); free(job.password); }
    wipe(&job,sizeof(job)); return err;
}
esp_err_t zam_wifi_operation(uint32_t id,zam_wifi_operation_t *out)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!id || !out) return ESP_ERR_INVALID_ARG;
    lock(); esp_err_t err=operation.id==id?ESP_OK:ESP_ERR_NOT_FOUND;
    if (err==ESP_OK) *out=operation;
    unlock(); return err;
}
