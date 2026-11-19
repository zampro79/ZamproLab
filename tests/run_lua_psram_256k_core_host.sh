#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/zam-lua-256k-core.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
printf '#pragma once\n#define CONFIG_ESP_CONSOLE_UART_NUM 0\n#define CONFIG_ZAM_LUA_HEAP_LIMIT 262144\n#define CONFIG_ZAM_LUA_SOURCE_LIMIT 32768\n' > "$tmp/sdkconfig.h"
python3 - "$root/tests/lua_core_host.c" "$tmp/lua_core_host.c" <<'PY'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
s=s.replace('m.lua_peak<=49152','m.lua_peak<=262144')
old='write_script("while true do end");stop_after=10;assert(run(&m)==ZAM_LUA_STOPPED);stop_after=0;'
new='write_script("local a={} for i=1,200 do a[i]=string.rep(\'x\',700) end while true do end");stop_after=100;assert(run(&m)==ZAM_LUA_STOPPED && m.lua_peak>131072);stop_after=0;'
assert old in s
Path(sys.argv[2]).write_text(s.replace(old,new,1))
PY
make -C "$LUA_DIR" -j4 linux >/dev/null
cc -std=gnu11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32=1 -DCONFIG_ZAM_LUA_PSRAM_EXPERIMENT=1 -I"$tmp" -I"$root/tests/psram_stubs" -I"$root/tests/program_stubs" -I"$root/components/zam_rgb/include" -I"$root/components/zam_files/include" -I"$root/components/zam_runtime_lua/include" -I"$root/components/zam_runtime_lua" -I"$LUA_DIR/src" "$tmp/lua_core_host.c" "$root/tests/lua_psram_heap_mock.c" "$root/components/zam_runtime_lua/zam_runtime_lua.c" "$root/components/zam_runtime_lua/zam_lua_allocator.c" "$root/components/zam_runtime_lua/zam_lua_bindings.c" "$root/components/zam_runtime_lua/zam_lua_modules.c" "$root/components/zam_runtime_lua/zam_lua_gpio.c" "$root/components/zam_runtime_lua/zam_lua_i2c.c" "$LUA_DIR/src/liblua.a" -lm -ldl "$root/tests/rgb_disabled_host.c" -o "$tmp/test"
ZAM_REPO_ROOT="$root" "$tmp/test"
