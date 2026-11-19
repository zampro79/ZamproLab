#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dir=$(mktemp -d "${TMPDIR:-/tmp}/zam-web-stream.XXXXXX")
trap 'rm -rf "$dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pthread -I"$root/components/zam_http" "$root/tests/web_stream_host.c" "$root/components/zam_http/zam_web_stream.c" -o "$dir/stream"
"$dir/stream"
cc -std=c11 -Wall -Wextra -Werror -I"$root/tests/web_http_stubs" -I"$root/tests/storage_stubs" -I"$root/components/zam_http" -I"$root/components/zam_storage/include" "$root/tests/web_http_host.c" "$root/components/zam_http/zam_http_web.c" "$root/components/zam_http/zam_web_stream.c" -o "$dir/http"
"$dir/http"
cc -std=c11 -Wall -Wextra -Werror -pthread -I"$root/tests/storage_stubs" -I"$root/components/zam_storage/include" "$root/tests/web_storage_host.c" -o "$dir/storage"
"$dir/storage"
