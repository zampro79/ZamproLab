#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_WIFI_CONN 0x3001
#define ESP_ERR_WIFI_NOT_CONNECT 0x3002
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define ESP_MAC_BASE 0
#define ESP_LOGI(tag,fmt,...) do { if (0) printf(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_DISCONNECTED 1
#define WIFI_EVENT_SCAN_DONE 2
#define IP_EVENT_STA_LOST_IP 1
#define IP_EVENT_STA_GOT_IP 2
#define WIFI_MODE_AP 1
#define WIFI_MODE_STA 2
#define WIFI_MODE_APSTA 3
#define WIFI_IF_AP 1
#define WIFI_IF_STA 2
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((void)(p),192U),168U,4U,1U
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(n) (n)
typedef int wifi_auth_mode_t;
typedef int wifi_mode_t;
typedef int esp_event_base_t;
typedef struct { uint32_t status; uint8_t number,scan_id; } wifi_event_sta_scan_done_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { int addr; } ip_addr_t;
typedef struct { struct {ip_addr_t ip;} ip_info; } ip_event_got_ip_t;
typedef struct { ip_addr_t ip; } esp_netif_ip_info_t;
typedef struct { int dummy; } esp_netif_t;
typedef struct { int dummy; } wifi_init_config_t;
typedef struct { int num; } wifi_sta_list_t;
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { unsigned char ssid[33]; int8_t rssi; uint8_t primary; wifi_auth_mode_t authmode; } wifi_ap_record_t;
typedef struct { struct {unsigned char ssid[32], password[64]; int ssid_len,channel,max_connection,authmode;} ap; struct {unsigned char ssid[32],password[64]; struct {int authmode;} threshold;} sta; } wifi_config_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
size_t strlcpy(char *,const char *,size_t);
size_t heap_caps_get_free_size(int); size_t heap_caps_get_largest_free_block(int); size_t heap_caps_get_minimum_free_size(int);
const char *esp_err_to_name(esp_err_t); int64_t esp_timer_get_time(void);
QueueHandle_t xQueueCreate(unsigned,size_t); int xQueueSend(QueueHandle_t,const void *,unsigned); int xQueueReceive(QueueHandle_t,void *,unsigned); int xQueueOverwrite(QueueHandle_t,const void *); void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void); int xSemaphoreTake(SemaphoreHandle_t,unsigned); int xSemaphoreGive(SemaphoreHandle_t); void vSemaphoreDelete(SemaphoreHandle_t);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,int,void *);
esp_err_t esp_read_mac(uint8_t *,int); esp_err_t esp_netif_init(void); esp_err_t esp_event_loop_create_default(void); esp_err_t esp_event_loop_delete_default(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void); esp_netif_t *esp_netif_create_default_wifi_sta(void); void esp_netif_destroy_default_wifi(esp_netif_t *); esp_err_t esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *);
esp_err_t esp_event_handler_register(int,int,void (*)(void *,int,int32_t,void *),void *); esp_err_t esp_event_handler_unregister(int,int,void (*)(void *,int,int32_t,void *));
esp_err_t esp_wifi_init(const wifi_init_config_t *); esp_err_t esp_wifi_deinit(void); esp_err_t esp_wifi_set_storage(int); esp_err_t esp_wifi_set_mode(wifi_mode_t); esp_err_t esp_wifi_set_config(int,const wifi_config_t *); esp_err_t esp_wifi_start(void); esp_err_t esp_wifi_stop(void); esp_err_t esp_wifi_connect(void); esp_err_t esp_wifi_disconnect(void); esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *); esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *,bool); esp_err_t esp_wifi_scan_stop(void); esp_err_t esp_wifi_scan_get_ap_records(uint16_t *,wifi_ap_record_t *); esp_err_t esp_wifi_clear_ap_list(void);
