#!/usr/bin/env python3
"""Build a private, traceable package; never flashes or overwrites output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PSRAM_PROFILE = 's3-rgb-psram-16mb'
PROFILES = {'classic-4mb': ('esp32', 4, False), 's3-generic-4mb': ('esp32s3', 4, False),
            's3-rgb-16mb': ('esp32s3', 16, True), PSRAM_PROFILE: ('esp32s3', 16, True)}
PSRAM_METADATA = {'required_size_bytes': 8 * 1024 * 1024, 'mode': 'octal', 'frequency_mhz': 80}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def version():
    v = (ROOT / 'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?', v):
        raise ValueError('invalid VERSION')
    return v


def normalize_idf_version(value):
    if value not in ('5.5.3', 'v5.5.3'):
        raise ValueError('ESP-IDF version is not 5.5.3')
    return '5.5.3'


def snapshot():
    # A parent Git repository must never be mistaken for this source tree.
    if not (ROOT / '.git').exists():
        return snapshot_from_manifest()
    commit = git('rev-parse', 'HEAD')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('invalid commit')
    status = git('status', '--porcelain=v1', '--untracked-files=all')
    names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-co', '--exclude-standard', '-z']).split(b'\0')
    h = hashlib.sha256()
    for name in sorted(set(x for x in names if x)):
        path = ROOT / os.fsdecode(name)
        if not path.is_file() or path.is_symlink():
            raise ValueError('missing or unsupported source input: ' + os.fsdecode(name))
        h.update(name + b'\0' + bytes.fromhex(sha(path)))
    return {'commit': commit, 'dirty': bool(status), 'source_sha256': h.hexdigest()}



def snapshot_from_manifest():
    manifest_path = ROOT / 'SOURCE_SNAPSHOT.json'
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError('source snapshot manifest required without local .git')
    try:
        m = json.loads(manifest_path.read_text(encoding='utf-8'))
    except (ValueError, UnicodeError) as exc:
        raise ValueError('invalid source snapshot manifest') from exc
    required = {'format', 'project', 'version', 'distribution', 'source_commit',
                'modified', 'prepared', 'publication', 'files'}
    if not isinstance(m, dict) or set(m) != required or m['format'] != 1:
        raise ValueError('unsupported source snapshot manifest')
    if (m['project'] != 'ZamproLab' or m['version'] != version()
            or m['distribution'] != 'source-only' or m['publication'] != 'source-only-candidate'
            or type(m['modified']) is not bool or not isinstance(m['prepared'], str)
            or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', m['prepared'])
            or (m['source_commit'] is not None and (not isinstance(m['source_commit'], str) or not re.fullmatch(r'[0-9a-f]{40}', m['source_commit'])))
            or not isinstance(m['files'], dict) or not m['files']):
        raise ValueError('invalid source snapshot metadata')
    expected = m['files']
    actual = set()
    for path in ROOT.rglob('*'):
        rel = path.relative_to(ROOT).as_posix()
        if rel == 'managed_components' or rel.startswith('managed_components/'):
            continue  # validated independently by dependency_snapshot
        if path.is_symlink():
            raise ValueError('unsafe symlink in source snapshot')
        if path.is_file() and rel != 'SOURCE_SNAPSHOT.json':
            actual.add(rel)
    if actual != set(expected):
        unexpected = actual - set(expected)
        if any('__pycache__' in Path(name).parts or name.endswith('.pyc') for name in unexpected):
            raise ValueError('unlisted Python cache: use PYTHONDONTWRITEBYTECODE=1 or PYTHONPYCACHEPREFIX outside the source tree; inspect and remove only your own cache files')
        raise ValueError('source snapshot file inventory mismatch')
    h = hashlib.sha256()
    for name in sorted(actual):
        digest = expected[name]
        if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest) or sha(ROOT / name) != digest:
            raise ValueError('source snapshot file hash mismatch: ' + name)
        h.update(os.fsencode(name) + b'\0' + bytes.fromhex(digest))
    # This is a manifest assertion, not independent proof of Git provenance.
    return {'commit': m['source_commit'], 'dirty': m['modified'],
            'source_sha256': h.hexdigest(), 'source_provenance': 'manifest-unverified'}

def dependency_snapshot(lua):
    component = ROOT / 'managed_components/joltwallet__littlefs'
    manifest = component / 'CHECKSUMS.json'
    entries = json.loads(manifest.read_text())['files']
    if not entries:
        raise ValueError('empty LittleFS checksum manifest')
    seen = set()
    for entry in entries:
        name = entry['path']
        path = (component / name).resolve()
        if name in seen or component.resolve() not in path.parents or not path.is_file() or sha(path) != entry['hash']:
            raise ValueError('LittleFS source mismatch: ' + name)
        seen.add(name)
    actual = {str(p.relative_to(component)) for p in component.rglob('*') if p.is_file()}
    if actual - seen != {'CHECKSUMS.json', '.component_hash'}:
        raise ValueError('unexpected LittleFS component files')
    result = {'littlefs_checksums_sha256': sha(manifest),
              'littlefs_component_hash': (component / '.component_hash').read_text().strip()}
    if lua == 'ON':
        lua_src = Path(os.environ['LUA_DIR']) / 'src'
        entries = re.findall(r'"([^";]+);([0-9a-f]{64})"', (ROOT / 'components/zam_runtime_lua/lua-5.5.1-sha256.cmake').read_text())
        if len(entries) != 61:
            raise ValueError('unexpected Lua manifest')
        for name, expected in entries:
            if not (lua_src / name).is_file() or sha(lua_src / name) != expected:
                raise ValueError('Lua source mismatch: ' + name)
        result['lua_source_manifest_sha256'] = sha(ROOT / 'components/zam_runtime_lua/lua-5.5.1-sha256.cmake')
    return result


def run(cmd):
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True)


def inspect_webfs(path):
    """Read member sizes from the actual LittleFS image, fail closed."""
    component = ROOT / 'managed_components/joltwallet__littlefs/src/littlefs'
    with tempfile.TemporaryDirectory(prefix='zam-webfs-inspect-') as tmp:
        inspector = Path(tmp) / 'inspect'
        run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(component),
             ROOT / 'tools/inspect_webfs.c', component / 'lfs.c', component / 'lfs_util.c', '-o', inspector])
        return int(subprocess.check_output([inspector, path], text=True).strip())


def validate_webfs_compatibility(webfs, factory):
    if webfs.stat().st_size != 0xc0000:
        raise ValueError('invalid WebFS image size')
    maximum = inspect_webfs(webfs)
    if maximum > 65536:
        raise ValueError('WebFS member exceeds 64 KiB')
    if maximum > 16384 and b'ZAM_WEBFS_STREAM_65536_V1' not in factory.read_bytes():
        raise ValueError('WebFS requires streaming-capable Factory')


def validate_artifacts(files, target, build, flash_mb=4):
    expected = {
        'bootloader': ('bootloader/bootloader.bin', 'bootloader.bin', 0x1000 if target == 'esp32' else 0),
        'partition_table': ('partition_table/partition-table.bin', 'partition-table.bin', 0x8000),
        'factory': ('zamprolab.bin', 'zamprolab-factory.bin', 0x10000),
        'webfs': ('storage-images/webfs.bin', 'zamprolab-webfs.bin', 0x210000),
        'userfs_empty': ('storage-images/userfs-empty.bin', 'userfs-empty.bin', 0x2d0000),
    }
    if set(files) != set(expected) or len({v[1] for v in files.values()}) != len(expected):
        raise ValueError('missing or duplicate artifact')
    for name, (source, filename, offset) in files.items():
        rel, wanted_name, wanted_offset = expected[name]
        if (filename, offset) != (wanted_name, wanted_offset) or source != build / rel:
            raise ValueError('wrong artifact path, name or offset: ' + name)
        if source.is_symlink() or source.resolve() != source or not source.is_file() or not source.stat().st_size:
            raise ValueError('missing, empty or unsafe artifact: ' + name)
        # Actually read every input before creating a destination, including on restricted filesystems.
        sha(source)
    table = files['partition_table'][0].read_bytes()
    if len(table) != 0xc00:
        raise ValueError('invalid partition table size')
    entries = {}
    for pos in range(0, len(table), 32):
        magic, typ, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', table[pos:pos + 32])
        if magic != 0x50aa:
            break
        label = label.split(b'\0', 1)[0].decode('ascii')
        if label in entries:
            raise ValueError('duplicate partition')
        entries[label] = (typ, subtype, offset, size, flags)
    if entries.get('factory') != (0, 0, 0x10000, 0x200000, 0) or entries.get('webfs') != (1, 0x83, 0x210000, 0xc0000, 0):
        raise ValueError('partition table layout mismatch')
    if flash_mb not in (4, 16):
        raise ValueError('unsupported flash size')
    required = {'nvs': (1, 2, 0x9000, 0x6000, 0), 'phy_init': (1, 1, 0xf000, 0x1000, 0),
                'userfs': (1, 0x83, 0x2d0000, 0x130000, 0)}
    if any(entries.get(k) != v for k, v in required.items()):
        raise ValueError('partition table layout mismatch')
    regions = sorted((e[2], e[2] + e[3], name) for name, e in entries.items())
    if any(start < 0x9000 or end > flash_mb * 1024 * 1024 or start >= end for start, end, _ in regions):
        raise ValueError('partition outside flash')
    if any(a[1] > b[0] for a, b in zip(regions, regions[1:])):
        raise ValueError('overlapping partitions')
    if files['userfs_empty'][0].stat().st_size != entries['userfs'][3] or b'littlefs' not in files['userfs_empty'][0].read_bytes()[:4096]:
        raise ValueError('invalid empty UserFS image')
    if files['factory'][0].stat().st_size > 0x200000 or files['webfs'][0].stat().st_size != 0xc0000:
        raise ValueError('artifact exceeds partition or invalid WebFS size')
    if files['bootloader'][0].stat().st_size > 0x8000 - files['bootloader'][2]:
        raise ValueError('bootloader overlaps partition table')
    if files['bootloader'][0].read_bytes()[:1] != b'\xe9' or files['factory'][0].read_bytes()[:1] != b'\xe9':
        raise ValueError('invalid ESP image')
    if b'littlefs' not in files['webfs'][0].read_bytes()[:4096]:
        raise ValueError('invalid WebFS image')
    validate_webfs_compatibility(files['webfs'][0], files['factory'][0])


def validate_manifest(manifest, dest):
    required = {'project', 'version', 'git_commit', 'git_dirty', 'source_snapshot_sha256',
                'target', 'lua_enabled', 'lua_version', 'dependency_snapshot', 'esp_idf_version',
                'flash_size', 'psram_enabled', 'partition_layout_sha256', 'artifacts', 'hardware_profile', 'rgb_enabled', 'flash_mode', 'flash_frequency'}
    if 'source_provenance' in manifest:
        required.add('source_provenance')
        if manifest['source_provenance'] != 'manifest-unverified':
            raise ValueError('invalid snapshot provenance')
    psram = manifest.get('hardware_profile') == PSRAM_PROFILE
    if psram:
        required |= {'psram_configuration', 'lua_heap_limit', 'lua_allocator'}
        if (manifest.get('target') != 'esp32s3' or manifest.get('flash_size') != '16MB'
                or manifest.get('psram_enabled') is not True or manifest.get('rgb_enabled') is not True
                or manifest.get('lua_enabled') is not True or manifest.get('lua_heap_limit') != 262144
                or manifest.get('lua_allocator') != 'psram' or manifest.get('psram_configuration') != PSRAM_METADATA):
            raise ValueError('inconsistent PSRAM profile manifest')
    elif manifest.get('psram_enabled'):
        raise ValueError('PSRAM requires the explicit PSRAM profile')
    if set(manifest) != required or set(manifest['artifacts']) != {'bootloader', 'partition_table', 'factory', 'webfs', 'userfs_empty'}:
        raise ValueError('incomplete manifest')
    profile = manifest['hardware_profile']
    if profile not in PROFILES:
        raise ValueError('unknown manifest hardware profile')
    expected_target, expected_flash, expected_rgb = PROFILES[profile]
    if (manifest['target'] != expected_target or manifest['flash_size'] != f'{expected_flash}MB'
            or manifest['rgb_enabled'] is not expected_rgb):
        raise ValueError('manifest hardware profile mismatch')
    expected_offsets = {'bootloader': 0x1000 if manifest['target'] == 'esp32' else 0,
                        'partition_table': 0x8000, 'factory': 0x10000, 'webfs': 0x210000, 'userfs_empty': 0x2d0000}
    expected_names = {'bootloader': 'bootloader.bin', 'partition_table': 'partition-table.bin',
                      'factory': 'zamprolab-factory.bin', 'webfs': 'zamprolab-webfs.bin', 'userfs_empty': 'userfs-empty.bin'}
    if len({item['file'] for item in manifest['artifacts'].values()}) != len(expected_offsets):
        raise ValueError('duplicate artifact metadata')
    for name, item in manifest['artifacts'].items():
        if set(item) != {'file', 'size', 'sha256', 'offset'} or Path(item['file']).name != item['file']:
            raise ValueError('invalid artifact metadata')
        if item['offset'] != expected_offsets[name] or item['file'] != expected_names[name]:
            raise ValueError('invalid artifact name or offset: ' + name)
        path = dest / item['file']
        if path.is_symlink() or not path.is_file() or path.stat().st_size != item['size'] or sha(path) != item['sha256']:
            raise ValueError('manifest artifact verification failed')
    validate_webfs_compatibility(dest / expected_names['webfs'], dest / expected_names['factory'])


def validate_build(build, target, lua, v, flash_mb=4, rgb=False, psram=False):
    desc = json.loads((build / 'project_description.json').read_text())
    if desc['target'] != target or desc['project_version'] != v:
        raise ValueError('build target or project version mismatch')
    cache = (build / 'CMakeCache.txt').read_text()
    if not re.search(r'^ZAM_EXPERIMENTAL_LUA:BOOL=' + lua + r'$', cache, re.M):
        raise ValueError('Lua configuration mismatch')
    app = build / 'zamprolab.bin'
    raw = app.read_bytes()
    if len(raw) < 80 or raw[32:36] != b'\x32\x54\xcd\xab' or raw[48:80].split(b'\0')[0].decode() != v:
        raise ValueError('embedded esp_app_desc version mismatch')
    if len(raw) < 208 or raw[176:208].hex() != sha(build / 'zamprolab.elf'):
        raise ValueError('factory does not match build ELF')
    chip_id = int.from_bytes(raw[12:14], 'little')
    if chip_id != {'esp32': 0, 'esp32s3': 9}[target]:
        raise ValueError('factory image target mismatch')
    config = (build / 'sdkconfig').read_text()
    if f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_mb}MB"' not in config or 'CONFIG_PARTITION_TABLE_CUSTOM=y' not in config:
        raise ValueError('unsupported flash or partition configuration')
    lines = config.splitlines()
    if ('CONFIG_ZAM_RGB_VERIFIED_S3=y' in lines) != rgb or ('CONFIG_SPIRAM=y' in lines) != psram:
        raise ValueError('RGB or PSRAM configuration mismatch')
    if psram:
        if target != 'esp32s3' or flash_mb != 16 or not rgb or lua != 'ON':
            raise ValueError('PSRAM profile requires S3 RGB, 16MB flash and Lua ON')
        required_config = ('CONFIG_IDF_TARGET="esp32s3"', 'CONFIG_IDF_TARGET_ESP32S3=y', 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
                           'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"', 'CONFIG_ZAM_RGB_VERIFIED_S3=y',
                           'CONFIG_SPIRAM=y', 'CONFIG_SPIRAM_MODE_OCT=y', 'CONFIG_SPIRAM_TYPE_AUTO=y', 'CONFIG_SPIRAM_SPEED_80M=y',
                           'CONFIG_SPIRAM_SPEED=80', 'CONFIG_SPIRAM_BOOT_INIT=y', 'CONFIG_SPIRAM_MEMTEST=y',
                           'CONFIG_SPIRAM_USE_CAPS_ALLOC=y', 'CONFIG_ZAM_LUA_SOURCE_LIMIT=32768',
                           'CONFIG_ZAM_LUA_PSRAM_EXPERIMENT=y', 'CONFIG_ZAM_LUA_HEAP_LIMIT=262144')
        for setting in required_config:
            key = setting.split('=', 1)[0] + '='
            if lines.count(setting) != 1 or sum(line.startswith(key) for line in lines) != 1:
                raise ValueError('PSRAM profile requires resolved ' + setting)
        for option in ('CONFIG_SPIRAM_IGNORE_NOTFOUND', 'CONFIG_SPIRAM_MODE_QUAD', 'CONFIG_SPIRAM_SPEED_40M'):
            if lines.count('# ' + option + ' is not set') != 1 or any(line.startswith(option + '=') for line in lines):
                raise ValueError('PSRAM profile requires disabled ' + option)
    elif 'CONFIG_ZAM_LUA_PSRAM_EXPERIMENT=y' in lines:
        raise ValueError('PSRAM Lua allocator requires the explicit PSRAM profile')
    if flash_mb == 16 and (target != 'esp32s3' or not rgb):
        raise ValueError('16MB supported only for verified S3 RGB profile')
    return desc, config


def flashing_instructions(m):
    target, profile, flash = m['target'], m['hardware_profile'], m['flash_size']
    frequency, mode = m['flash_frequency'], m['flash_mode']
    boot = m['artifacts']['bootloader']['offset']
    psram = m.get('psram_enabled', False)
    memory = '8 MiB octal PSRAM required at 80 MHz; Lua PSRAM allocator, limit 262144 bytes' if psram else 'PSRAM disabled'
    return f"""PRIVATE EVALUATION — NOT FOR PUBLIC DISTRIBUTION
Profile {profile}; target {target}; flash {flash}, {mode.upper()} {frequency}; Lua enabled; {memory}.
Use only on a verified compatible board. The verified S3 RGB profiles reserve GPIO48 for a GRB LED.

Offsets: bootloader.bin {boot:#x}; partition-table.bin 0x8000; zamprolab-factory.bin 0x10000;
zamprolab-webfs.bin 0x210000; userfs-empty.bin 0x2d0000. Verify sizes and SHA-256 in release-manifest.json.

INITIAL INSTALLATION overwrites selected regions and can destroy existing data. Identify chip, flash,
protections and existing layout. Back up data and obtain explicit authority before overwriting.
NVS at 0x9000 and PHY at 0xf000 are not written; on an erased device NVS initializes at boot.
UserFS must be initialized using userfs-empty.bin only after expressly authorizing data loss.

Linux esptool v4 (replace PORT and confirm tool/version; use lower baud if unstable):
esptool.py --chip {target} --port PORT --baud 921600 write_flash --flash_mode {mode} --flash_freq {frequency} --flash_size {flash} {boot:#x} bootloader.bin 0x8000 partition-table.bin 0x10000 zamprolab-factory.bin 0x210000 zamprolab-webfs.bin 0x2d0000 userfs-empty.bin

UPDATE: first verify the installed partition table, target, flash size and Factory/WebFS compatibility.
Verify that the installed bootloader is compatible with the candidate Factory; if uncertain, stop.
Back up and verify NVS and UserFS. Preserve bootloader, table, NVS, PHY and UserFS.
Never select userfs-empty.bin during an update. If layout differs, stop and plan migration.
WebFS assets larger than 16 KiB require the streaming Factory in this exact package.
Do not install this WebFS alone onto an older Factory; install and verify the matching Factory first.
esptool.py --chip {target} --port PORT --baud 921600 write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x10000 zamprolab-factory.bin 0x210000 zamprolab-webfs.bin

Windows Flash Download Tool (not tested): select chip {target}, Develop mode and appropriate UART/USB
load mode and COM port; enter each authorized image and offset above, select compatible SPI settings,
prefer DoNotChgBin, set 921600 or lower, START, confirm completion and restart. For UPDATE select
only Factory and WebFS. Never use ERASE or CombineBin for an update. See
https://docs.espressif.com/projects/esp-test-tools/en/latest/{target}/production_stage/tools/flash_download_tool.html

On first boot connect to AP ZamproLab-XXXXXX (last three MAC bytes); factory default password is
ZamproLab. Open http://192.168.4.1/. Change the default AP password via the administrative
console. Use a trusted network: HTTP has no authentication or TLS.
This package is experimental; check the manifest for source-tree status. Its third-party license inventory
is not complete. No Windows or hardware installation of this package has been validated.
"""


def package(target, lua, build, dest, profile=None):
    profile = profile or ('classic-4mb' if target == 'esp32' else 's3-generic-4mb')
    if profile not in PROFILES or PROFILES[profile][0] != target:
        raise ValueError('profile and target mismatch')
    _, flash_mb, rgb = PROFILES[profile]
    psram = profile == PSRAM_PROFILE
    if psram and lua != 'ON':
        raise ValueError('PSRAM profile requires Lua ON')
    if target not in ('esp32', 'esp32s3') or lua not in ('ON', 'OFF'):
        raise ValueError('unsupported target or Lua configuration')
    if build.exists() or dest.exists() or dest.with_suffix('.zip').exists() or build == dest or build in dest.parents or dest in build.parents:
        raise ValueError('build and destination must be new, separate directories')
    if ROOT == build or ROOT in build.parents or ROOT == dest or ROOT in dest.parents:
        raise ValueError('build and output must be outside source tree')
    if not os.environ.get('IDF_PATH'):
        raise ValueError('IDF_PATH is required; activate ESP-IDF 5.5.3')
    if not os.environ.get('LUA_DIR') or not (Path(os.environ['LUA_DIR']) / 'src' / 'lua.h').is_file():
        raise ValueError('LUA_DIR must point to a verified Lua 5.5.1 source directory containing src/lua.h')
    if not (ROOT / '.git').exists() and (Path(os.environ['LUA_DIR']).resolve() == ROOT or ROOT in Path(os.environ['LUA_DIR']).resolve().parents):
        raise ValueError('for a Gitless snapshot set LUA_DIR to a verified dependency outside the source tree')
    # Child Python tools must not create unlisted __pycache__ entries.
    os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
    if not (ROOT / 'managed_components/joltwallet__littlefs/include/esp_littlefs.h').is_file():
        raise ValueError('LittleFS component unavailable')
    v, before, deps = version(), snapshot(), dependency_snapshot(lua)
    build.mkdir(parents=True)
    cfg = build / 'sdkconfig'
    defaults = [ROOT / 'sdkconfig.defaults']
    if rgb:
        defaults.append(ROOT / 'config/esp32s3-rgb.defaults')
    if psram:
        defaults.extend(ROOT / 'config' / f for f in ('esp32s3-rgb-psram-experiment.defaults',
                        'esp32s3-lua-psram-phase-b.defaults', 'esp32s3-lua-psram-phase-c2-256k.defaults'))
    run(['cmake', '-G', 'Ninja', '-S', ROOT, '-B', build, '-DIDF_TARGET=' + target,
         '-DSDKCONFIG=' + str(cfg), '-DZAM_EXPERIMENTAL_LUA=' + lua,
         '-DPYTHON=' + sys.executable, '-DPYTHON_DEPS_CHECKED=1',
         '-DSDKCONFIG_DEFAULTS=' + ';'.join(map(str, defaults))])
    run(['ninja', '-C', build, '-j', '4'])
    images = build / 'storage-images'
    run([ROOT / 'tools/build_storage_images.sh', images])
    after = snapshot()
    if before != after or version() != v or deps != dependency_snapshot(lua):
        raise ValueError('source state changed while building')
    desc, config = validate_build(build, target, lua, v, flash_mb, rgb, psram)
    app = build / 'zamprolab.bin'
    idf_header = (Path(os.environ['IDF_PATH']) / 'components/esp_common/include/esp_idf_version.h').read_text()
    if not all(re.search(r'#define\s+ESP_IDF_VERSION_' + key + r'\s+' + value + r'\b', idf_header)
               for key, value in [('MAJOR', '5'), ('MINOR', '5'), ('PATCH', '3')]):
        raise ValueError('unsupported IDF')
    flash_args = json.loads((build / 'flasher_args.json').read_text())
    flash_settings = flash_args['flash_settings']
    if flash_settings['flash_size'] != f'{flash_mb}MB' or flash_settings['flash_mode'] != 'dio' or flash_settings['flash_freq'] not in ('40m', '80m'):
        raise ValueError('unexpected flash settings')
    if flash_args['extra_esptool_args']['chip'] != target:
        raise ValueError('flash tool target mismatch')
    expected_offsets = {hex(0x1000 if target == 'esp32' else 0), '0x8000', '0x10000'}
    if set(flash_args['flash_files']) != expected_offsets:
        raise ValueError('unexpected build flash offsets')
    files = {'bootloader': (build / 'bootloader/bootloader.bin', 'bootloader.bin', 0x1000 if target == 'esp32' else 0),
             'partition_table': (build / 'partition_table/partition-table.bin', 'partition-table.bin', 0x8000),
             'factory': (app, 'zamprolab-factory.bin', 0x10000),
             'webfs': (images / 'webfs.bin', 'zamprolab-webfs.bin', 0x210000),
             'userfs_empty': (images / 'userfs-empty.bin', 'userfs-empty.bin', 0x2d0000)}
    validate_artifacts(files, target, build, flash_mb)
    dest.mkdir(parents=True)
    artifacts = {}
    for name, (src, filename, offset) in files.items():
        dst = dest / filename
        shutil.copyfile(src, dst)
        if sha(src) != sha(dst):
            raise ValueError('copied artifact hash mismatch')
        artifacts[name] = {'file': filename, 'size': dst.stat().st_size, 'sha256': sha(dst), 'offset': offset}
    manifest = {'project': 'ZamproLab', 'version': v, 'git_commit': before['commit'],
                'git_dirty': before['dirty'], 'source_snapshot_sha256': before['source_sha256'],
                'target': target, 'lua_enabled': lua == 'ON', 'lua_version': '5.5.1' if lua == 'ON' else None,
                'dependency_snapshot': deps,
                'esp_idf_version': normalize_idf_version(desc['git_revision']), 'flash_size': f'{flash_mb}MB', 'psram_enabled': 'CONFIG_SPIRAM=y' in config,
                'hardware_profile': profile, 'rgb_enabled': rgb,
                'flash_mode': flash_settings['flash_mode'], 'flash_frequency': flash_settings['flash_freq'],
                'partition_layout_sha256': sha(ROOT / 'partitions.csv'), 'artifacts': artifacts}
    if 'source_provenance' in before:
        manifest['source_provenance'] = before['source_provenance']
    if psram:
        manifest.update(psram_configuration=dict(PSRAM_METADATA), lua_heap_limit=262144, lua_allocator='psram')
    validate_manifest(manifest, dest)
    shutil.copyfile(ROOT / 'LICENSE', dest / 'LICENSE')
    shutil.copyfile(ROOT / 'THIRD_PARTY_NOTICES.md', dest / 'THIRD_PARTY_NOTICES.md')
    shutil.copytree(ROOT / 'third_party_licenses', dest / 'third_party_licenses')
    (dest / 'FLASHING.txt').write_text(flashing_instructions(manifest))
    tmp_manifest = dest / '.release-manifest.tmp'
    tmp_manifest.write_text(json.dumps(manifest, indent=2) + '\n')
    tmp_manifest.replace(dest / 'release-manifest.json')
    if psram:
        sums = [(str(p.relative_to(dest)), sha(p)) for p in sorted(dest.rglob('*')) if p.is_file()]
        (dest / 'SHA256SUMS').write_text(''.join(digest + '  ' + name + '\n' for name, digest in sums))
    print('Manifest:', dest / 'release-manifest.json')
    archive = dest.with_suffix('.zip')
    if archive.exists():
        raise ValueError('archive already exists')
    write_archive(dest, archive)
    print('ZIP:', archive, sha(archive))


def write_archive(dest, archive):
    """Archive verified package files with stable timestamps and file modes."""
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for path in sorted(dest.rglob('*')):
            if path.is_symlink():
                raise ValueError('unsafe package symlink')
            if path.is_file():
                info = zipfile.ZipInfo(str(Path(dest.name) / path.relative_to(dest)), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                z.writestr(info, path.read_bytes())
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None: raise ValueError('archive integrity error')
        for path in sorted(dest.rglob('*')):
            if path.is_file() and hashlib.sha256(z.read(str(Path(dest.name) / path.relative_to(dest)))).hexdigest() != sha(path):
                raise ValueError('archive content mismatch')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--target', required=True, choices=['esp32', 'esp32s3'])
    p.add_argument('--lua', required=True, choices=['ON', 'OFF'])
    p.add_argument('--profile', choices=list(PROFILES))
    p.add_argument('--build-dir', required=True, type=lambda x: Path(x).resolve())
    p.add_argument('--output', required=True, type=lambda x: Path(x).resolve())
    args = p.parse_args()
    try:
        package(args.target, args.lua, args.build_dir, args.output, args.profile)
    except (OSError, ValueError, subprocess.CalledProcessError, KeyError) as exc:
        print('Packaging failed:', exc, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
