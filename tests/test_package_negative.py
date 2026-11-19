import importlib.util
import json
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_build', ROOT / 'tools/package_build.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class NegativeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.build = self.base / 'build'
        self.build.mkdir()
        for folder in ('bootloader', 'partition_table', 'storage-images'):
            (self.build / folder).mkdir()
        part = lambda t, s, o, n, label: struct.pack('<HBBII16sI', 0x50aa, t, s, o, n, label.encode(), 0)
        table = part(0, 0, 0x10000, 0x200000, 'factory') + part(1, 0x83, 0x210000, 0xc0000, 'webfs') + part(1, 2, 0x9000, 0x6000, 'nvs') + part(1, 1, 0xf000, 0x1000, 'phy_init') + part(1, 0x83, 0x2d0000, 0x130000, 'userfs')
        (self.build / 'partition_table/partition-table.bin').write_bytes(table.ljust(0xc00, b'\xff'))
        (self.build / 'bootloader/bootloader.bin').write_bytes(b'\xe9' + b'x'*128)
        (self.build / 'zamprolab.bin').write_bytes(b'\xe9' + b'x'*256)
        component=ROOT/'managed_components/joltwallet__littlefs/src/littlefs'
        generator=self.base/'generator'
        subprocess.run(['cc','-std=c11','-I'+str(component),str(ROOT/'tools/make_littlefs_image.c'),str(component/'lfs.c'),str(component/'lfs_util.c'),'-o',str(generator)],check=True)
        inputs=[]
        for name in ('index.html','style.css','app.js','terminal.js','highlight.js','settings.js'):
            path=self.base/name;path.write_bytes(b'asset');inputs.append(path)
        subprocess.run([generator,self.build/'storage-images/webfs.bin','786432',*inputs],check=True)
        (self.build / 'storage-images/userfs-empty.bin').write_bytes((b'\xff'*8+b'littlefs').ljust(0x130000, b'\xff'))
        self.files = {'bootloader': (self.build/'bootloader/bootloader.bin', 'bootloader.bin', 0x1000),
                      'partition_table': (self.build/'partition_table/partition-table.bin', 'partition-table.bin', 0x8000),
                      'factory': (self.build/'zamprolab.bin', 'zamprolab-factory.bin', 0x10000),
                      'webfs': (self.build/'storage-images/webfs.bin', 'zamprolab-webfs.bin', 0x210000),
                      'userfs_empty': (self.build/'storage-images/userfs-empty.bin', 'userfs-empty.bin', 0x2d0000)}

    def prepare_build_metadata(self):
        (self.build/'project_description.json').write_text(json.dumps({'target':'esp32','project_version':'0.1.0-dev'}))
        (self.build/'CMakeCache.txt').write_text('ZAM_EXPERIMENTAL_LUA:BOOL=ON\n')
        (self.build/'sdkconfig').write_text('CONFIG_ESPTOOLPY_FLASHSIZE="4MB"\nCONFIG_PARTITION_TABLE_CUSTOM=y\n')
        elf=b'test elf';(self.build/'zamprolab.elf').write_bytes(elf)
        image=bytearray(256);image[0]=0xe9;image[32:36]=b'\x32\x54\xcd\xab';image[48:58]=b'0.1.0-dev\0';image[176:208]=hashlib.sha256(elf).digest()
        (self.build/'zamprolab.bin').write_bytes(image)

    def test_build_metadata_rejects_mixed_inputs(self):
        self.prepare_build_metadata()
        m.validate_build(self.build,'esp32','ON','0.1.0-dev')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32','OFF','0.1.0-dev')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32','ON','0.2.0')
        (self.build/'zamprolab.elf').write_bytes(b'another build')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32','ON','0.1.0-dev')
        self.prepare_build_metadata()
        (self.build/'sdkconfig').write_text('CONFIG_ESPTOOLPY_FLASHSIZE="16MB"\n')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32','ON','0.1.0-dev')
        self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_valid_fixture(self):
        m.validate_artifacts(self.files, 'esp32', self.build)

    def test_each_missing_or_empty_rejected(self):
        for name in self.files:
            with self.subTest(name=name):
                p = self.files[name][0]; original = p.read_bytes(); p.unlink()
                with self.assertRaises(ValueError): m.validate_artifacts(self.files, 'esp32', self.build)
                p.write_bytes(b'')
                with self.assertRaises(ValueError): m.validate_artifacts(self.files, 'esp32', self.build)
                p.write_bytes(original)
                self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_wrong_sizes_offset_partition_and_duplicates(self):
        bad = dict(self.files); p,n,_ = bad['factory']; bad['factory']=(p,n,0x210000)
        with self.assertRaises(ValueError): m.validate_artifacts(bad, 'esp32', self.build)
        bad = dict(self.files); p,_,off=bad['factory'];bad['factory']=(p,'bootloader.bin',off)
        with self.assertRaises(ValueError): m.validate_artifacts(bad, 'esp32', self.build)
        bad = dict(self.files); bad['factory']=bad['webfs']
        with self.assertRaises(ValueError): m.validate_artifacts(bad, 'esp32', self.build)
        f=self.files['webfs'][0];f.write_bytes(b'littlefs')
        with self.assertRaises(ValueError): m.validate_artifacts(self.files, 'esp32', self.build)
        f.write_bytes((b'\xff'*8+b'littlefs').ljust(0xc0000,b'\xff'))
        f=self.files['partition_table'][0]; data=bytearray(f.read_bytes());struct.pack_into('<I',data,32+4,0x220000);f.write_bytes(data)
        with self.assertRaises(ValueError): m.validate_artifacts(self.files, 'esp32', self.build)
        self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_other_build_and_symlink(self):
        other=self.base/'foreign.bin';other.write_bytes(self.files['factory'][0].read_bytes())
        bad=dict(self.files);_,n,o=bad['factory'];bad['factory']=(other,n,o)
        with self.assertRaises(ValueError):m.validate_artifacts(bad,'esp32',self.build)
        target=self.files['factory'][0];target.unlink();target.symlink_to(other)
        with self.assertRaises(ValueError):m.validate_artifacts(self.files,'esp32',self.build)
        self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_unreadable_file_and_output(self):
        (self.base / 'src').mkdir()
        (self.base / 'src/lua.h').write_text('fixture')
        with patch.object(m, 'sha', side_effect=PermissionError('denied')):
            with self.assertRaises(PermissionError):m.validate_artifacts(self.files,'esp32',self.build)
        with patch.dict(os.environ, {'IDF_PATH': str(self.base), 'LUA_DIR': str(self.base)}), \
             patch.object(m, 'dependency_snapshot', return_value={}), \
             patch.object(Path, 'mkdir', side_effect=PermissionError('denied')):
            with self.assertRaises(PermissionError):m.package('esp32','ON',self.base/'newbuild',self.base/'output')
        self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_invalid_version_and_configuration(self):
        with patch.object(m,'ROOT',self.base):
            with self.assertRaises(FileNotFoundError):m.version()
            (self.base/'VERSION').write_text('bad')
            with self.assertRaises(ValueError):m.version()
        for target,lua in [('unknown','ON'),('esp32','MAYBE')]:
            with self.assertRaises(ValueError):m.package(target,lua,self.base/'newbuild',self.base/'output')
        with self.assertRaises(ValueError):m.package('esp32','ON',self.base/'missing'/'build',self.build)
        self.assertFalse((self.base/'output/release-manifest.json').exists())

    def test_verified_s3_16mb_profile(self):
        self.prepare_build_metadata()
        p=self.build/'project_description.json';d=json.loads(p.read_text());d['target']='esp32s3';p.write_text(json.dumps(d))
        p=self.build/'zamprolab.bin';b=bytearray(p.read_bytes());b[12:14]=(9).to_bytes(2,'little');p.write_bytes(b)
        (self.build/'sdkconfig').write_text('CONFIG_ESPTOOLPY_FLASHSIZE="16MB"\nCONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_ZAM_RGB_VERIFIED_S3=y\n')
        m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True)
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',4,True)
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,False)
        with self.assertRaises(ValueError):m.package('esp32','ON',self.base/'new',self.base/'out','s3-rgb-16mb')

    def test_psram_profile_resolved_configuration(self):
        self.prepare_build_metadata()
        desc=self.build/'project_description.json'; d=json.loads(desc.read_text()); d['target']='esp32s3';desc.write_text(json.dumps(d))
        image=self.build/'zamprolab.bin'; data=bytearray(image.read_bytes());data[12:14]=(9).to_bytes(2,'little');image.write_bytes(data)
        required=['CONFIG_IDF_TARGET="esp32s3"','CONFIG_IDF_TARGET_ESP32S3=y','CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
                  'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y','CONFIG_PARTITION_TABLE_CUSTOM=y',
                  'CONFIG_ZAM_RGB_VERIFIED_S3=y','CONFIG_SPIRAM=y','CONFIG_SPIRAM_MODE_OCT=y','CONFIG_SPIRAM_TYPE_AUTO=y',
                  'CONFIG_SPIRAM_SPEED_80M=y','CONFIG_SPIRAM_SPEED=80','CONFIG_SPIRAM_BOOT_INIT=y',
                  'CONFIG_SPIRAM_MEMTEST=y','CONFIG_SPIRAM_USE_CAPS_ALLOC=y',
                  'CONFIG_ZAM_LUA_SOURCE_LIMIT=32768','CONFIG_ZAM_LUA_PSRAM_EXPERIMENT=y',
                  'CONFIG_ZAM_LUA_HEAP_LIMIT=262144', '# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set',
                  '# CONFIG_SPIRAM_MODE_QUAD is not set', '# CONFIG_SPIRAM_SPEED_40M is not set']
        cfg=self.build/'sdkconfig';cfg.write_text('\n'.join(required)+'\n')
        m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True,True)
        for line in required:
            with self.subTest(missing=line):
                cfg.write_text('\n'.join(x for x in required if x!=line)+'\n')
                with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True,True)
        cfg.write_text('\n'.join(required)+'\nCONFIG_SPIRAM_IGNORE_NOTFOUND=y\n')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True,True)
        cfg.write_text('\n'.join(required)+'\nCONFIG_ZAM_LUA_HEAP_LIMIT=49152\n')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True,True)
        cfg.write_text('\n'.join(required)+'\n')
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','OFF','0.1.0-dev',16,True,True)
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',16,True,False)
        with self.assertRaises(ValueError):m.validate_build(self.build,'esp32s3','ON','0.1.0-dev',4,False,False)
        with self.assertRaises(ValueError):m.package('esp32','ON',self.base/'new',self.base/'out',m.PSRAM_PROFILE)

    def test_psram_manifest_and_reproducible_zip(self):
        dest=self.base/'one'/'package';dest.mkdir(parents=True)
        files={}
        for name,(src,filename,offset) in self.files.items():
            shutil.copyfile(src,dest/filename)
            files[name]={'file':filename,'size':(dest/filename).stat().st_size,'sha256':m.sha(dest/filename),'offset':offset}
        files['bootloader']['offset']=0
        manifest={'project':'ZamproLab','version':'0.1.0-dev','git_commit':'0'*40,'git_dirty':False,
                  'source_snapshot_sha256':'0'*64,'target':'esp32s3','lua_enabled':True,'lua_version':'5.5.1',
                  'dependency_snapshot':{},'esp_idf_version':'5.5.3','flash_size':'16MB','psram_enabled':True,
                  'partition_layout_sha256':'0'*64,'artifacts':files,'hardware_profile':m.PSRAM_PROFILE,
                  'rgb_enabled':True,'flash_mode':'dio','flash_frequency':'80m',
                  'psram_configuration':dict(m.PSRAM_METADATA),'lua_heap_limit':262144,'lua_allocator':'psram'}
        m.validate_manifest(manifest,dest)
        for field,bad in [('target','esp32'),('lua_heap_limit',49152),('lua_allocator','internal'),('psram_enabled',False),('flash_size','4MB')]:
            with self.subTest(field=field):
                changed=dict(manifest);changed[field]=bad
                with self.assertRaises(ValueError):m.validate_manifest(changed,dest)
        changed=json.loads(json.dumps(manifest));changed['artifacts']['webfs']['offset']=0x220000
        with self.assertRaises(ValueError):m.validate_manifest(changed,dest)
        changed=json.loads(json.dumps(manifest));changed['artifacts']['webfs']['sha256']='0'*64
        with self.assertRaises(ValueError):m.validate_manifest(changed,dest)
        changed=json.loads(json.dumps(manifest));changed['artifacts']['webfs']['file']='../escape.bin'
        with self.assertRaises(ValueError):m.validate_manifest(changed,dest)
        for profile,target,flash,rgb,boot in [('classic-4mb','esp32','4MB',False,0x1000),
                                              ('s3-generic-4mb','esp32s3','4MB',False,0),
                                              ('s3-rgb-16mb','esp32s3','16MB',True,0)]:
            legacy=json.loads(json.dumps(manifest))
            for key in ('psram_configuration','lua_heap_limit','lua_allocator'):legacy.pop(key)
            legacy.update(hardware_profile=profile,target=target,flash_size=flash,rgb_enabled=rgb,psram_enabled=False)
            legacy['artifacts']['bootloader']['offset']=boot
            m.validate_manifest(legacy,dest)
        other=self.base/'two'/'package';shutil.copytree(dest,other)
        os.utime(other/'zamprolab-webfs.bin',(1700000000,1700000000))
        a=self.base/'one'/'one.zip';b=self.base/'two'/'two.zip'
        m.write_archive(dest,a);m.write_archive(other,b);self.assertEqual(a.read_bytes(),b.read_bytes())

    def test_oversized_factory_and_webfs(self):
        for name,size in [('factory',0x200001),('webfs',0xc0001)]:
            path=self.files[name][0];old=path.read_bytes()
            with self.subTest(name=name):
                path.write_bytes(old.ljust(size,b'X'))
                with self.assertRaises(ValueError):m.validate_artifacts(self.files,'esp32',self.build)
            path.write_bytes(old)

    def test_oversized_web_asset_rejected_by_storage_builder(self):
        component=ROOT/'managed_components/joltwallet__littlefs'
        if not (component/'include/esp_littlefs.h').is_file():
            self.skipTest('LittleFS cache unavailable')
        fixture=self.base/'source';fixture.mkdir()
        for directory in ('web','samples','tools'):
            shutil.copytree(ROOT/directory,fixture/directory)
        index=fixture/'web/index.html'
        index.write_bytes(index.read_bytes()+b'X'*65537)
        env=dict(os.environ,LITTLEFS_COMPONENT_DIR=str(component))
        result=subprocess.run(['sh',str(fixture/'tools/build_storage_images.sh'),str(self.base/'storage')],
                              text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Web asset exceeds firmware 64 KiB limit',result.stdout)
        self.assertFalse((self.base/'storage/webfs.bin').exists())

    def test_graphics_staging_rejects_missing_and_oversized_assets(self):
        component=ROOT/'managed_components/joltwallet__littlefs'
        if not (component/'include/esp_littlefs.h').is_file():
            self.skipTest('LittleFS cache unavailable')
        fixture=self.base/'graphics-source';fixture.mkdir()
        for directory in ('web','samples','tools'):
            shutil.copytree(ROOT/directory,fixture/directory)
        env=dict(os.environ,LITTLEFS_COMPONENT_DIR=str(component))
        logo=fixture/'web/assets/logo.png'
        original=logo.read_bytes()
        logo.unlink()
        result=subprocess.run(['sh',str(fixture/'tools/build_storage_images.sh'),str(self.base/'missing-graphics')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.base/'missing-graphics/webfs.bin').exists())
        logo.write_bytes(original+b'X'*50000)
        self.assertLess((fixture/'web/index.html').stat().st_size,65536)
        result=subprocess.run(['sh',str(fixture/'tools/build_storage_images.sh'),str(self.base/'oversized-graphics')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Web asset exceeds firmware 64 KiB limit',result.stdout)
        self.assertFalse((self.base/'oversized-graphics/webfs.bin').exists())

    def test_flash_size_and_overlap_validation(self):
        m.validate_artifacts(self.files, 'esp32', self.build, 4)
        m.validate_artifacts(self.files, 'esp32', self.build, 16)
        with self.assertRaises(ValueError):m.validate_artifacts(self.files, 'esp32', self.build, 2)
        f=self.files['partition_table'][0]; data=bytearray(f.read_bytes())
        struct.pack_into('<I',data,4*32+4,0x2c0000);f.write_bytes(data)
        with self.assertRaises(ValueError):m.validate_artifacts(self.files, 'esp32', self.build, 16)

    def test_incomplete_manifest(self):
        with self.assertRaises(ValueError):m.validate_manifest({'project':'ZamproLab'},self.base)
        self.assertFalse((self.base/'release-manifest.json').exists())

    def test_clean_modified_and_untracked_git(self):
        repo=self.base/'repo';repo.mkdir()
        def git(*args):
            subprocess.run(['git','-C',str(repo),*args],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        git('init','-q');git('config','user.name','Test');git('config','user.email','test@example.invalid')
        (repo/'source').write_text('original');git('add','source');git('commit','-qm','fixture')
        with patch.object(m,'ROOT',repo):
            clean=m.snapshot();self.assertFalse(clean['dirty'])
            (repo/'source').write_text('changed');changed=m.snapshot();self.assertTrue(changed['dirty'])
            self.assertNotEqual(clean['source_sha256'],changed['source_sha256'])
            (repo/'source').write_text('original');(repo/'untracked').write_text('new')
            extra=m.snapshot();self.assertTrue(extra['dirty']);self.assertNotEqual(clean['source_sha256'],extra['source_sha256'])
            self.assertEqual(clean['commit'],extra['commit'])


if __name__ == '__main__': unittest.main()
