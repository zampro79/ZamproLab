#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/zam-wifi-policy.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root/tests/host_stubs" -I"$root/components/zam_wifi/include" \
  -I"$root/components/zam_wifi" "$root/tests/wifi_policy_test.c" -o "$bin"
"$bin"
printf 'Wi-Fi policy cases passed\n'
