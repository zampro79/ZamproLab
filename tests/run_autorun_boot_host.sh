#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-autorun-boot.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=gnu11 -Wall -Wextra -Werror -DZAM_EXPERIMENTAL_LUA -I"$root/tests/autorun_boot_stubs" -I"$root/tests/program_stubs" -I"$root/components/zam_storage/include" -I"$root/components/zam_program/include" "$root/tests/autorun_boot_host.c" -o "$bin"
"$bin"
