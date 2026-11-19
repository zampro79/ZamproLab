#!/bin/sh
set -eu
: "${LUA_DIR:?Set LUA_DIR to verified Lua 5.5.1 source root}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dir=$(mktemp -d /tmp/zam-autorun-real.XXXXXX)
trap 'rm -rf "$dir"' EXIT HUP INT TERM
make -C "$LUA_DIR" -j4 linux >/dev/null
inc="-I$root/tests/program_stubs -I$root/components/zam_rgb/include -I$root/components/zam_files/include -I$root/components/zam_program/include -I$root/components/zam_runtime_lua/include -I$root/components/zam_runtime_lua -I$LUA_DIR/src"
cc -std=gnu11 -Wall -Wextra -Werror $inc -Dmain=unused_lua_fixture -Dzam_files_lock=fixture_files_lock -Dzam_files_unlock=fixture_files_unlock -Dzam_files_open_read=fixture_files_open_read -Desp_timer_get_time=fixture_time -DvTaskDelay=fixture_delay -c "$root/tests/lua_core_host.c" -o "$dir/fixture.o"
cc -std=gnu11 -Wall -Wextra -Werror -pthread -include "$root/tests/program_stubs/host_compat.h" -DCONFIG_IDF_TARGET_ESP32=1 -DZAM_AUTORUN_TEST_HOOK $inc "$root/tests/autorun_lua_integration_host.c" "$root/components/zam_program/zam_program.c" "$root/components/zam_program/zam_output_ring.c" "$root/components/zam_runtime_lua/zam_runtime_lua.c" "$root/components/zam_runtime_lua/zam_lua_allocator.c" "$root/components/zam_runtime_lua/zam_lua_bindings.c" "$root/components/zam_runtime_lua/zam_lua_modules.c" "$root/components/zam_runtime_lua/zam_lua_gpio.c" "$root/components/zam_runtime_lua/zam_lua_i2c.c" "$root/tests/rgb_disabled_host.c" "$dir/fixture.o" "$LUA_DIR/src/liblua.a" -lm -ldl -o "$dir/test"
"$dir/test"
