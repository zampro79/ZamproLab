#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-lua-gpio-s3.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
make -C "$LUA_DIR" -j4 linux >/dev/null
cc -std=gnu11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3=1 -I"$root/tests/program_stubs" -I"$root/components/zam_runtime_lua/include" -I"$root/components/zam_runtime_lua" -I"$LUA_DIR/src" "$root/tests/lua_gpio_s3_host.c" "$root/components/zam_runtime_lua/zam_lua_gpio.c" "$LUA_DIR/src/liblua.a" -lm -ldl -o "$bin"
"$bin"
