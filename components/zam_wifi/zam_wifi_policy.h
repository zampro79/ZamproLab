#pragma once
#include <stdbool.h>
#include "zam_wifi.h"

/* AUTO is logical only: the driver always receives a physical mode. */
static inline zam_wifi_mode_t zam_wifi_resolve_mode(zam_wifi_mode_t configured, bool has_sta_credentials)
{
    return configured==ZAM_WIFI_AUTO ? (has_sta_credentials ? ZAM_WIFI_APSTA : ZAM_WIFI_AP) : configured;
}

/* Web-style operations always preserve an AP and require consent to leave STA-only. */
static inline bool zam_wifi_sta_transition(zam_wifi_mode_t current, bool enabled, bool confirmed, zam_wifi_mode_t *target)
{
    if (!target || current<ZAM_WIFI_AP || current>ZAM_WIFI_AUTO || (current==ZAM_WIFI_STA && !confirmed)) return false;
    *target=enabled ? ZAM_WIFI_APSTA : ZAM_WIFI_AP;
    return true;
}
