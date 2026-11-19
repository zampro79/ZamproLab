#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF 5.5.3}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/zam-http-program.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=gnu11 -Wall -Wextra -Werror -pthread -I"$root/tests/http_program_stubs" -I"$root/tests/program_stubs" -I"$root/components/zam_program/include" -I"$IDF_PATH/components/json/cJSON" "$root/tests/http_program_host.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$bin"
"$bin"
