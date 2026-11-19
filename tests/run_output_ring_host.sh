#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$(mktemp /tmp/zam-output-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I"$root/components/zam_program" "$root/tests/output_ring_host.c" "$root/components/zam_program/zam_output_ring.c" -o "$bin"
"$bin"
