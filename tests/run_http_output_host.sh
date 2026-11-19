#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF 5.5.3}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-http-output.XXXXXX)
mbedcrypto=${MBEDCRYPTO_LIB:-/usr/lib/x86_64-linux-gnu/libmbedcrypto.so.16}
[ -r "$mbedcrypto" ] || { echo "Set MBEDCRYPTO_LIB to the host mbedcrypto library" >&2; exit 1; }
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=gnu11 -Wall -Wextra -Werror -pthread -I"$root/tests/http_output_stubs" -I"$root/tests/program_stubs" -I"$root/components/zam_program/include" -I"$root/components/zam_program" -I"$IDF_PATH/components/json/cJSON" "$root/tests/http_output_host.c" "$root/components/zam_program/zam_output_ring.c" "$IDF_PATH/components/json/cJSON/cJSON.c" "$mbedcrypto" -lm -o "$bin"
"$bin"
