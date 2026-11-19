import hashlib, importlib.util, json, pathlib, subprocess, tempfile, unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('packager',ROOT/'tools/package_build.py'); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
class SnapshotTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=pathlib.Path(self.temp.name);self.root=self.base/'tree';self.root.mkdir();(self.root/'VERSION').write_text('0.2.0-rc.1\n');(self.root/'source.c').write_text('source\n'); self.patch=patch.object(m,'ROOT',self.root);self.patch.start();self.addCleanup(self.patch.stop)
  self.manifest={'format':1,'project':'ZamproLab','version':'0.2.0-rc.1','distribution':'source-only','source_commit':None,'modified':True,'prepared':'2026-11-15','publication':'source-only-candidate','files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.iterdir()}}
 def write(self): (self.root/'SOURCE_SNAPSHOT.json').write_text(json.dumps(self.manifest))
 def test_valid(self):
  self.write();s=m.snapshot();self.assertIsNone(s['commit']);self.assertTrue(s['dirty']);self.assertEqual(s['source_provenance'],'manifest-unverified')
 def test_missing_malformed_version_and_missing_source(self):
  with self.assertRaises(ValueError):m.snapshot()
  (self.root/'SOURCE_SNAPSHOT.json').write_text('{')
  with self.assertRaises(ValueError):m.snapshot()
  self.manifest['version']='0.0.0';self.write()
  with self.assertRaises(ValueError):m.snapshot()
  self.manifest['version']='0.2.0-rc.1';self.write();(self.root/'source.c').unlink()
  with self.assertRaises(ValueError):m.snapshot()
 def test_modified_extra_and_symlink(self):
  self.write();(self.root/'source.c').write_text('changed')
  with self.assertRaises(ValueError):m.snapshot()
  (self.root/'source.c').write_text('source\n');(self.root/'extra').write_text('extra')
  with self.assertRaises(ValueError):m.snapshot()
  (self.root/'extra').unlink();(self.root/'link').symlink_to(self.root/'source.c')
  with self.assertRaises(ValueError):m.snapshot()
 def test_cache_and_invalid_commit_type(self):
  self.write();cache=self.root/'__pycache__';cache.mkdir();(cache/'x.pyc').write_bytes(b'cache')
  with self.assertRaisesRegex(ValueError,'Python cache'):m.snapshot()
  (cache/'x.pyc').unlink();cache.rmdir();self.manifest['source_commit']=7;self.write()
  with self.assertRaises(ValueError):m.snapshot()
 def test_lua_dir_required(self):
  self.write()
  with patch.dict('os.environ',{'IDF_PATH':'/tmp'},clear=True):
   with self.assertRaisesRegex(ValueError,'LUA_DIR'):m.package('esp32','ON',self.base/'build',self.base/'out')
 def test_parent_repo_not_inherited(self):
  subprocess.run(['git','init','-q',str(self.base)],check=True);self.write()
  with patch.object(m,'git',side_effect=AssertionError('external git used')):self.assertIsNone(m.snapshot()['commit'])
if __name__=='__main__':unittest.main()
