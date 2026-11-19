import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_build', ROOT / 'tools/package_build.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageBuildTests(unittest.TestCase):
    def test_idf_version_strict_normalization(self):
        for value in ('5.5.3', 'v5.5.3'):
            with self.subTest(value=value):
                self.assertEqual(module.normalize_idf_version(value), '5.5.3')
        for value in ('5.5.2', '5.5.4', 'v5.5.3-dirty', '5.5.3-dirty',
                      '5.5', '', 'V5.5.3', '5.5.3 ', 'vv5.5.3', None, 553):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    module.normalize_idf_version(value)

    def test_version_and_source_snapshot(self):
        self.assertRegex(module.version(), r'^\d+\.\d+\.\d+')
        state = module.snapshot()
        self.assertRegex(state['commit'], r'^[0-9a-f]{40}$')
        self.assertIsInstance(state['dirty'], bool)
        self.assertRegex(state['source_sha256'], r'^[0-9a-f]{64}$')

    def test_sha_and_missing_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'input'
            p.write_bytes(b'abc')
            self.assertEqual(module.sha(p), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            with self.assertRaises(FileNotFoundError):
                module.sha(Path(d) / 'missing')

    def test_rejects_unsupported_and_existing_destinations(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            with self.assertRaises(ValueError):
                module.package('bad-target', 'ON', p / 'build', p / 'output')
            with self.assertRaises(ValueError):
                module.package('esp32', 'bad', p / 'build', p / 'output')
            with self.assertRaises(ValueError):
                module.package('esp32', 'ON', p, p / 'output')
            self.assertFalse((p / 'output' / 'release-manifest.json').exists())

    def test_initial_and_update_flash_instructions(self):
        m={'target':'esp32s3','hardware_profile':'s3-generic-4mb','flash_size':'4MB','flash_frequency':'80m','flash_mode':'dio','artifacts':{'bootloader':{'offset':0}}}
        text=module.flashing_instructions(m)
        self.assertIn('--flash_freq 80m',text)
        initial,update=text.split('UPDATE:',1)
        self.assertIn('0x2d0000 userfs-empty.bin',initial)
        command=next(line for line in update.splitlines() if line.startswith('esptool.py'))
        self.assertNotIn('userfs-empty',command)
        self.assertNotIn('0x2d0000',command)
        self.assertIn('0x210000',command)
        self.assertIn('installed bootloader is compatible with the candidate Factory',update)
        self.assertIn('if uncertain, stop',update)

    def test_verified_local_dependency_fingerprints(self):
        if not (ROOT / 'managed_components/joltwallet__littlefs/CHECKSUMS.json').exists():
            self.skipTest('LittleFS cache unavailable')
        old = os.environ.get('LUA_DIR')
        os.environ['LUA_DIR'] = str(ROOT / '.deps/lua-5.5.1')
        try:
            if not (Path(os.environ['LUA_DIR']) / 'src/lua.h').exists():
                self.skipTest('Lua cache unavailable')
            result = module.dependency_snapshot('ON')
            self.assertRegex(result['littlefs_checksums_sha256'], r'^[0-9a-f]{64}$')
            self.assertRegex(result['lua_source_manifest_sha256'], r'^[0-9a-f]{64}$')
        finally:
            if old is None:
                del os.environ['LUA_DIR']
            else:
                os.environ['LUA_DIR'] = old


if __name__ == '__main__':
    unittest.main()
