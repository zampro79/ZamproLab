#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/zam-lua-128k-core.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
printf '#pragma once\n#define CONFIG_ESP_CONSOLE_UART_NUM 0\n#define CONFIG_ZAM_LUA_HEAP_LIMIT 131072\n#define CONFIG_ZAM_LUA_SOURCE_LIMIT 32768\n' > "$tmp/sdkconfig.h"
sed 's/m.lua_peak<=49152/m.lua_peak<=131072/' "$root/tests/lua_core_host.c" > "$tmp/lua_core_host.c"
make -C "$LUA_DIR" -j4 linux >/dev/null
cc -std=gnu11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32=1 -DCONFIG_ZAM_LUA_PSRAM_EXPERIMENT=1 -I"$tmp" -I"$root/tests/psram_stubs" -I"$root/tests/program_stubs" -I"$root/components/zam_rgb/include" -I"$root/components/zam_files/include" -I"$root/components/zam_runtime_lua/include" -I"$root/components/zam_runtime_lua" -I"$LUA_DIR/src" "$tmp/lua_core_host.c" "$root/tests/lua_psram_heap_mock.c" "$root/components/zam_runtime_lua/zam_runtime_lua.c" "$root/components/zam_runtime_lua/zam_lua_allocator.c" "$root/components/zam_runtime_lua/zam_lua_bindings.c" "$root/components/zam_runtime_lua/zam_lua_modules.c" "$root/components/zam_runtime_lua/zam_lua_gpio.c" "$root/components/zam_runtime_lua/zam_lua_i2c.c" "$LUA_DIR/src/liblua.a" -lm -ldl "$root/tests/rgb_disabled_host.c" -o "$tmp/test"
ZAM_REPO_ROOT="$root" "$tmp/test"
