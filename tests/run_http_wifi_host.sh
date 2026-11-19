#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF 5.5.3}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/zam-http-wifi.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -DCONFIG_LWIP_IPV6=1 -std=gnu11 -Wall -Wextra -Werror -I"$root/tests/http_wifi_stubs" -I"$root/components/zam_wifi/include" -I"$IDF_PATH/components/json/cJSON" "$root/tests/http_wifi_host.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$bin"
"$bin"
