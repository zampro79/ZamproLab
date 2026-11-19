#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/zam-wifi-worker.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -pthread -std=gnu11 -Wall -Wextra -Werror -I"$root/tests/wifi_stubs" -I"$root/components/zam_wifi/include" -I"$root/components/zam_wifi" -I"$root/components/zam_config/include" "$root/tests/wifi_worker_host.c" -o "$bin"
"$bin"
