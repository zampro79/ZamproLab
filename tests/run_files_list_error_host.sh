#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-autorun-files.XXXXXX)
trap 'rm -f "$bin"' EXIT
cc -pthread -std=gnu11 -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter -DZAM_EXPERIMENTAL_LUA=1 -include "$root/tests/autorun_files_stubs/sdkconfig.h" -I"$root/tests/autorun_files_stubs" -I"$root/components/zam_program/include" -I"$root/components/zam_files/include" -I"$IDF_PATH/components/json/cJSON" "$root/tests/files_list_error_host.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lcrypto -lm -o "$bin"
"$bin"
