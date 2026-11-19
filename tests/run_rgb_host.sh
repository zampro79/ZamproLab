#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-rgb-host.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
for enabled in 0 1; do
 cc -std=c11 -Wall -Wextra -Werror -DCONFIG_ZAM_RGB_VERIFIED_S3=$enabled -I"$root/tests/rgb_stubs" -I"$root/components/zam_rgb/include" "$root/tests/rgb_host.c" "$root/components/zam_rgb/zam_rgb.c" -o "$bin"
 "$bin"
done
