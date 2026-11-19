#!/usr/bin/env python3
"""Exercise real LittleFS size inspection and legacy Factory rejection."""
import importlib.util
from pathlib import Path
import struct,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('packager',ROOT/'tools/package_build.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Compatibility(unittest.TestCase):
 def test_boundaries_and_legacy_factory(self):
  with tempfile.TemporaryDirectory() as t:
   base=Path(t);build=base/'build';build.mkdir()
   for d in ('bootloader','partition_table','storage-images'):(build/d).mkdir()
   c=ROOT/'managed_components/joltwallet__littlefs/src/littlefs';gen=base/'gen'
   subprocess.run(['cc','-std=c11','-I'+str(c),str(ROOT/'tools/make_littlefs_image.c'),str(c/'lfs.c'),str(c/'lfs_util.c'),'-o',str(gen)],check=True)
   inp=base/'files';inp.mkdir(); names=('index.html','style.css','app.js','terminal.js','highlight.js','settings.js')
   paths=[inp/n for n in names]
   for p in paths:p.write_bytes(b'x')
   part=lambda t,s,o,n,l:struct.pack('<HBBII16sI',0x50aa,t,s,o,n,l.encode(),0)
   table=part(0,0,0x10000,0x200000,'factory')+part(1,0x83,0x210000,0xc0000,'webfs')+part(1,2,0x9000,0x6000,'nvs')+part(1,1,0xf000,0x1000,'phy_init')+part(1,0x83,0x2d0000,0x130000,'userfs')
   (build/'partition_table/partition-table.bin').write_bytes(table.ljust(0xc00,b'\xff'));(build/'bootloader/bootloader.bin').write_bytes(b'\xe9x')
   (build/'storage-images/userfs-empty.bin').write_bytes((b'littlefs').ljust(0x130000,b'\xff'))
   factory=build/'zamprolab.bin';web=build/'storage-images/webfs.bin'
   files={'bootloader':(build/'bootloader/bootloader.bin','bootloader.bin',0x1000),'partition_table':(build/'partition_table/partition-table.bin','partition-table.bin',0x8000),'factory':(factory,'zamprolab-factory.bin',0x10000),'webfs':(web,'zamprolab-webfs.bin',0x210000),'userfs_empty':(build/'storage-images/userfs-empty.bin','userfs-empty.bin',0x2d0000)}
   for n in (16384,16385,32768,65536,65537):
    paths[0].write_bytes(b'x'*n);subprocess.run([gen,web,'786432',*paths],check=True);self.assertEqual(m.inspect_webfs(web),n)
    factory.write_bytes(b'\xe9legacy')
    if n<=16384:m.validate_artifacts(files,'esp32',build)
    else:
     with self.assertRaises(ValueError):m.validate_artifacts(files,'esp32',build)
    factory.write_bytes(b'\xe9ZAM_WEBFS_STREAM_65536_V1')
    if n<=65536:m.validate_artifacts(files,'esp32',build)
    else:
     with self.assertRaises(ValueError):m.validate_artifacts(files,'esp32',build)
   web.write_bytes(b'\0'*786432)
   with self.assertRaises(subprocess.CalledProcessError):m.inspect_webfs(web)
if __name__=='__main__':unittest.main()
