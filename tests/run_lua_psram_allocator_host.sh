#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-lua-psram.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -DCONFIG_ZAM_LUA_PSRAM_EXPERIMENT=1 -I"$root/tests/psram_stubs" -I"$LUA_DIR/src" -I"$root/components/zam_runtime_lua" -I"$root/components/zam_runtime_lua/include" "$root/tests/lua_psram_allocator_host.c" "$root/components/zam_runtime_lua/zam_lua_allocator.c" -o "$bin"
"$bin"
