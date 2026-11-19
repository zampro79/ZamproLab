#!/usr/bin/env python3
"""Bundle canonical Lua examples and shared libraries into existing web assets."""
import base64
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
stage = pathlib.Path(sys.argv[2])
names = ('00_welcome.lua', '01_hello.lua', '02_countdown.lua', '03_semaforo.lua',
         '04_led_blink.lua', '05_led_sos.lua', '06_button.lua',
         '07_i2c_scanner.lua')
deps = {}
def read(path):
    data = path.read_bytes()
    text = data.decode('utf-8')
    if text.encode('utf-8') != data or '\r' in text:
        raise ValueError(f'{path}: expected UTF-8 and LF line endings')
    return text
samples = {name: read(root / 'samples' / 'lua' / name) for name in names}
libs = {name: read(root / 'samples' / 'lua' / 'lib' / (name + '.lua'))
        for name in ()}
for name, dependencies in deps.items():
    expanded = ''.join('local ' + lib + ' = (function()\n' + libs[lib] +
                       '\nend)()\n' for lib in dependencies) + samples[name]
    if len(expanded.encode('utf-8')) > 32768:
        raise ValueError(f'{name}: expanded sample exceeds Lua source limit')
prefix = 'window.ZamSampleLibraries=Object.freeze(' + json.dumps(libs, ensure_ascii=True) + ');\n'
(stage / 'highlight.js').write_bytes(prefix.encode('ascii') + (root / 'web' / 'highlight.js').read_bytes())
prefix = ('window.ZamSamples=Object.freeze(Object.fromEntries(Object.entries(' +
          json.dumps(samples, ensure_ascii=True) + ').map(([n,s])=>[n,((' +
          json.dumps(deps, ensure_ascii=True) + ')[n]||[]).map(k=>"local "+k+" = (function()\\n"+'
          'window.ZamSampleLibraries[k]+"\\nend)()\\n").join("")+s])));\n')
(stage / 'terminal.js').write_bytes(prefix.encode('ascii') + (root / 'web' / 'terminal.js').read_bytes())

# Preserve the firmware's existing /settings.js route while splitting source.
(stage / 'settings.js').write_bytes((root / 'web' / 'settings.js').read_bytes() + b'\n' + (root / 'web' / 'file_manager.js').read_bytes())

# Inline images into existing routes; the HTTP firmware has no image routes.
def data_uri(name):
    return 'data:image/png;base64,' + base64.b64encode((root / 'web' / 'assets' / name).read_bytes()).decode('ascii')
for name, marker, image in (('index.html', '{{ZAM_LOGO_DATA}}', 'logo.png'),
                            ('style.css', '{{ZAM_BANNER_DATA}}', 'banner.png')):
    text = read(root / 'web' / name)
    if text.count(marker) != 1:
        raise ValueError(f'{name}: expected exactly one {marker} marker')
    (stage / name).write_text(text.replace(marker, data_uri(image)), encoding='utf-8')
