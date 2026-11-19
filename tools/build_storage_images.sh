#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
component=${LITTLEFS_COMPONENT_DIR:-"$root/managed_components/joltwallet__littlefs"}
out=${1:-"$root/build-storage-images"}
if ! grep -q '^#define ESP_LITTLEFS_VERSION_NUMBER "1\.20\.4"' "$component/include/esp_littlefs.h"; then
  printf 'LittleFS 1.20.4 not found in %s\n' "$component" >&2; exit 1
fi
mkdir -p "$out"
bin=$(mktemp "${TMPDIR:-/tmp}/zam-lfs-image.XXXXXX")
verify=$(mktemp "${TMPDIR:-/tmp}/zam-lfs-verify.XXXXXX")
roundtrip=$(mktemp "${TMPDIR:-/tmp}/zam-lfs-roundtrip.XXXXXX")
stage=$(mktemp -d "${TMPDIR:-/tmp}/zam-lfs-stage.XXXXXX")
testimg=$(mktemp "${TMPDIR:-/tmp}/zam-lfs-testimg.XXXXXX")
trap 'rm -f "$bin" "$verify" "$roundtrip" "$testimg"; rm -rf "$stage"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I"$component/src/littlefs" \
  "$root/tools/make_littlefs_image.c" "$component/src/littlefs/lfs.c" "$component/src/littlefs/lfs_util.c" -o "$bin"
python3 "$root/tools/bundle_samples.py" "$root" "$stage"
# Validate final bundled resources against the streaming firmware's 64 KiB limit.
for asset in "$stage/index.html" "$stage/style.css" "$root/web/app.js" "$stage/settings.js" "$stage/terminal.js" "$stage/highlight.js"; do
  if [ "$(wc -c < "$asset")" -gt 65536 ]; then
    printf 'Web asset exceeds firmware 64 KiB limit: %s\n' "$asset" >&2; exit 1
  fi
done
"$bin" "$out/webfs.bin" 786432 "$stage/index.html" "$stage/style.css" "$root/web/app.js" "$stage/settings.js" "$stage/terminal.js" "$stage/highlight.js"
"$bin" "$out/userfs-empty.bin" 1245184
cc -std=c11 -Wall -Wextra -Werror -I"$component/src/littlefs" \
  "$root/tests/verify_storage_images.c" "$component/src/littlefs/lfs.c" "$component/src/littlefs/lfs_util.c" -o "$verify"
"$verify" "$out/webfs.bin" 786432 "$stage/index.html" "$stage/style.css" "$root/web/app.js" "$stage/settings.js" "$stage/terminal.js" "$stage/highlight.js"
"$verify" "$out/userfs-empty.bin" 1245184
cc -std=c11 -Wall -Wextra -Werror -I"$component/src/littlefs" \
  "$root/tests/storage_image_roundtrip.c" "$component/src/littlefs/lfs.c" "$component/src/littlefs/lfs_util.c" -o "$roundtrip"
cp "$out/userfs-empty.bin" "$testimg"
"$roundtrip" "$testimg" 1245184
"$verify" "$testimg" 1245184
printf 'Created images in %s; no device was written.\n' "$out"
