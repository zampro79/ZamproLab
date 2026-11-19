#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-files-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I"$root/tests/files_stubs" -I"$root/components/zam_files/include" "$root/tests/files_paths_test.c" "$root/components/zam_files/zam_files.c" -o "$bin"
"$bin"
