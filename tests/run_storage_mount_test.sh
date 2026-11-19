#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/zam-storage-test.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I"$root/tests/storage_stubs" -I"$root/components/zam_storage/include" \
  "$root/components/zam_storage/zam_storage.c" "$root/tests/storage_mount_test.c" -o "$bin"
"$bin"
printf 'Storage mount failure and no-format cases passed\n'
