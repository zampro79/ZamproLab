#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-program-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -DZAM_AUTORUN_TEST_HOOK -std=gnu11 -Wall -Wextra -Werror -pthread -include "$root/tests/program_stubs/host_compat.h" -I"$root/tests/program_stubs" -I"$root/components/zam_rgb/include" -I"$root/components/zam_program/include" -I"$root/components/zam_runtime_lua/include" -I"$root/components/zam_files/include" "$root/tests/program_manager_host.c" "$root/components/zam_program/zam_program.c" "$root/components/zam_program/zam_output_ring.c" "$root/tests/rgb_disabled_host.c" -o "$bin"
"$bin"
