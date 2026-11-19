#!/usr/bin/env python3
"""Check that packaged sample templates and shared drivers reproduce valid Lua."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LUA = pathlib.Path(os.environ.get('LUA_DIR', ROOT / '.deps' / 'lua-5.5.1')) / 'src' / 'luac'


class SampleBundleTest(unittest.TestCase):
    def test_sources_and_size(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            subprocess.run(['python3', str(ROOT / 'tools/bundle_samples.py'), str(ROOT), str(stage)], check=True)
            for name in ('index.html', 'style.css'):
                content = (stage / name).read_bytes()
                self.assertLessEqual(len(content), 65536)
                self.assertIn(b'data:image/png;base64,', content)
                self.assertNotIn(b'{{ZAM_', content)
            highlight = (stage / 'highlight.js').read_text()
            terminal = (stage / 'terminal.js').read_text()
            settings = (stage / 'settings.js').read_bytes()
            self.assertEqual(settings, (ROOT / 'web/settings.js').read_bytes() + b'\n' + (ROOT / 'web/file_manager.js').read_bytes())
            self.assertIn(b'window.ZamFileList=', settings)
            self.assertIn(b'window.ZamFileActions=', settings)
            self.assertIn('window.ZamImportBytes=', terminal)
            self.assertIn('window.ZamImportDirectories=', terminal)
            self.assertLessEqual(len(settings), 65536)
            self.assertLessEqual((ROOT / 'web/app.js').stat().st_size, 65536)
            html = (ROOT / 'web/index.html').read_text()
            self.assertLess(html.index('src="/highlight.js"'), html.index('src="/terminal.js"'))
            self.assertLess(html.index('src="/terminal.js"'), html.index('src="/settings.js"'))
            self.assertLess(html.index('src="/settings.js"'), html.index('src="/app.js"'))
            libraries = json.loads(highlight.split('Object.freeze(', 1)[1].split(');\n', 1)[0])
            templates = json.loads(terminal.split('Object.entries(', 1)[1].split(').map(', 1)[0])
            dependencies = json.loads(terminal.split('=>[n,((', 1)[1].split(')[n]||[]', 1)[0])
            self.assertEqual(len(templates), 8)
            self.assertNotIn("ssd1306", libraries)
            self.assertNotIn("09_ssd1306.lua", templates)
            self.assertNotIn("bme280", libraries)
            self.assertNotIn("08_bme280.lua", templates)
            self.assertNotIn("10_environment_monitor.lua", templates)
            for name, template in templates.items():
                self.assertEqual(template, (ROOT / 'samples/lua' / name).read_text())
                text = ''.join('local ' + lib + ' = (function()\n' + libraries[lib] + '\nend)()\n'
                               for lib in dependencies.get(name, [])) + template
                self.assertLessEqual(len(text.encode()), 32768)
                path = stage / name
                path.write_text(text)
                subprocess.run([str(LUA), '-p', str(path)], check=True)
            self.assertLessEqual(len(highlight.encode()), 65536)
            self.assertLessEqual(len(terminal.encode()), 65536)


if __name__ == '__main__':
    unittest.main()
