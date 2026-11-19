#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#define ZAM_WIFI_SCAN_MAX 20
typedef enum { ZAM_WIFI_AP = 1, ZAM_WIFI_STA, ZAM_WIFI_APSTA, ZAM_WIFI_AUTO } zam_wifi_mode_t;
typedef struct {
    zam_wifi_mode_t configured, effective;
    bool connected, fallback, paused, scanning;
    char sta_ssid[33], ap_ssid[33], sta_ip[16], ap_ip[16];
    unsigned clients, scan_count;
    esp_err_t last_error, scan_error;
    uint16_t disconnect_reason;
    uint32_t scan_generation;
} zam_wifi_status_t;
typedef struct { char ssid[33]; int8_t rssi; uint8_t channel; wifi_auth_mode_t auth; } zam_wifi_scan_item_t;
/* One volatile record. IDs must strictly increase until reboot; no wrap.
 * Never replay an unconfirmed request after reboot: deduplication is volatile.
 * A later serial operation marks the retained result as superseded. */
typedef enum { ZAM_WIFI_PASSWORD_KEEP, ZAM_WIFI_PASSWORD_REPLACE, ZAM_WIFI_PASSWORD_OPEN } zam_wifi_password_action_t;
typedef enum { ZAM_WIFI_PERSIST_PENDING, ZAM_WIFI_PERSIST_SAVED, ZAM_WIFI_PERSIST_FAILED } zam_wifi_persist_t;
typedef enum { ZAM_WIFI_APPLY_PENDING, ZAM_WIFI_APPLY_APPLIED, ZAM_WIFI_APPLY_FAILED, ZAM_WIFI_APPLY_NOT_ATTEMPTED } zam_wifi_apply_t;
typedef enum { ZAM_WIFI_CONNECTION_NA, ZAM_WIFI_CONNECTION_CONNECTING, ZAM_WIFI_CONNECTION_CONNECTED, ZAM_WIFI_CONNECTION_NOT_CONNECTED } zam_wifi_connection_t;
typedef struct {
    uint32_t id;
    bool enabled, confirm_sta_only;
    const char *ssid;
    zam_wifi_password_action_t password_action;
    const char *password;
} zam_wifi_sta_request_t;
typedef struct {
    uint32_t id;
    bool accepted, superseded;
    zam_wifi_persist_t persistence;
    zam_wifi_apply_t application;
    zam_wifi_connection_t connection;
    esp_err_t error;
    uint16_t disconnect_reason;
} zam_wifi_operation_t;
/* Accepted means queued, not saved. Any reused or older ID is rejected until reboot. */
esp_err_t zam_wifi_configure_sta(const zam_wifi_sta_request_t *request);
esp_err_t zam_wifi_operation(uint32_t id, zam_wifi_operation_t *out);
/* Mutating calls enqueue work, not completion. Inspect status and logs for outcome. */
esp_err_t zam_wifi_init(void);
esp_err_t zam_wifi_status(zam_wifi_status_t *status);
esp_err_t zam_wifi_set_mode(zam_wifi_mode_t mode);
esp_err_t zam_wifi_set_ap(const char *ssid, const char *password);
esp_err_t zam_wifi_set_sta(const char *ssid, const char *password);
esp_err_t zam_wifi_connect(void);
esp_err_t zam_wifi_disconnect(void);
/* The ID is assigned at acceptance, including before the worker starts.
 * Results of superseded IDs return ESP_ERR_NOT_FOUND. After timeout scans are
 * unavailable until the late completion is drained; reboot if it never arrives.
 * SSIDs retain raw bytes, empty SSIDs are hidden, and duplicates are retained. */
esp_err_t zam_wifi_scan_start(uint32_t *id);
esp_err_t zam_wifi_scan_get(uint32_t id, zam_wifi_scan_item_t *items, size_t capacity, size_t *count, bool *pending);
esp_err_t zam_wifi_scan(void);
esp_err_t zam_wifi_scan_results(zam_wifi_scan_item_t *items, size_t capacity, size_t *count, uint32_t *generation);
