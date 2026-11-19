#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH}" "${LUA_DIR:?Set LUA_DIR}" "${ZAM_IDF_PYTHON:?Set ZAM_IDF_PYTHON}" "${ZAM_EXTRA_COMPONENTS:?Set ZAM_EXTRA_COMPONENTS}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/zam-profile-negative.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$root" "$tmp" <<'PY'
import os, subprocess, sys
from pathlib import Path
root, tmp = map(Path, sys.argv[1:])
base = root / 'sdkconfig.defaults'
rgb = root / 'config/esp32s3-rgb.defaults'
psram = root / 'config/esp32s3-rgb-psram-experiment.defaults'
lua = root / 'config/esp32s3-lua-psram-phase-b.defaults'
cases = [
    ('classic_allocator', 'esp32', [base], 'CONFIG_ZAM_LUA_PSRAM_EXPERIMENT=y\n', 'Requested Lua PSRAM allocator requires'),
    ('generic_allocator', 'esp32s3', [base], 'CONFIG_ZAM_LUA_PSRAM_EXPERIMENT=y\n', 'Requested Lua PSRAM allocator requires'),
    ('psram_disabled', 'esp32s3', [base, rgb, psram, lua], '# CONFIG_SPIRAM is not set\n', 'Requested Lua PSRAM allocator requires'),
    ('psram_bad_limit', 'esp32s3', [base, rgb, psram, lua], 'CONFIG_ZAM_LUA_HEAP_LIMIT=262145\n', 'Requested Lua heap limit'),
    ('classic_256k', 'esp32', [base], 'CONFIG_ZAM_LUA_HEAP_LIMIT=262144\n', 'Requested Lua heap limit'),
    ('generic_octal', 'esp32s3', [base], 'CONFIG_SPIRAM=y\nCONFIG_SPIRAM_MODE_OCT=y\n', 'Requested octal PSRAM requires'),
    ('psram_wrong_flash', 'esp32s3', [base, rgb, psram, lua], 'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n', 'Lua PSRAM experiment requires 16 MB flash'),
]
for name, target, defaults, extra, expected in cases:
    override=tmp / (name+'.defaults'); override.write_text(extra)
    cmd=['cmake','-S',str(root),'-B',str(tmp/name),'-G','Ninja',f'-DIDF_TARGET={target}',
         f'-DSDKCONFIG={tmp/name}.config','-DZAM_EXPERIMENTAL_LUA=ON',
         '-DEXTRA_COMPONENT_DIRS='+os.environ['ZAM_EXTRA_COMPONENTS'],
         '-DPYTHON='+os.environ['ZAM_IDF_PYTHON'],'-DPYTHON_DEPS_CHECKED=1',
         '-DSDKCONFIG_DEFAULTS='+';'.join(map(str,defaults+[override]))]
    for attempt in range(2):
        result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode==0 or expected not in result.stdout:
            print(name+f' FAILED on attempt {attempt+1}\n'+result.stdout[-6000:],file=sys.stderr);sys.exit(1)
    print(name+' rejected twice: '+expected,flush=True)
PY
