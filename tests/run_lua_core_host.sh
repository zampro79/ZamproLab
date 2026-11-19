#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-lua-core.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
make -C "$LUA_DIR" -j4 linux >/dev/null
cc -std=gnu11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32=1 -I"$root/tests/program_stubs" -I"$root/components/zam_rgb/include" -I"$root/components/zam_files/include" -I"$root/components/zam_runtime_lua/include" -I"$root/components/zam_runtime_lua" -I"$LUA_DIR/src" "$root/tests/lua_core_host.c" "$root/components/zam_runtime_lua/zam_runtime_lua.c" "$root/components/zam_runtime_lua/zam_lua_allocator.c" "$root/components/zam_runtime_lua/zam_lua_bindings.c" "$root/components/zam_runtime_lua/zam_lua_modules.c" "$root/components/zam_runtime_lua/zam_lua_gpio.c" "$root/components/zam_runtime_lua/zam_lua_i2c.c" "$LUA_DIR/src/liblua.a" -lm -ldl "$root/tests/rgb_disabled_host.c" -o "$bin"
ZAM_REPO_ROOT="$root" "$bin"
