"""Serve the same combined Settings asset that WebFS packaging produces."""
import os
import base64
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def asset_bytes(path):
    data = path.read_bytes()
    for name, marker, image in (('index.html', b'{{ZAM_LOGO_DATA}}', 'logo.png'), ('style.css', b'{{ZAM_BANNER_DATA}}', 'banner.png')):
        if path == ROOT / 'web' / name:
            data = data.replace(marker, b'data:image/png;base64,' + base64.b64encode((ROOT / 'web' / 'assets' / image).read_bytes()))
    if path == ROOT / 'web' / 'index.html' and os.environ.get('ZAM_TEST_PAD_HTML') == '1':
        data += b'<!--' + b'x' * 4096 + b'-->'

    if path == ROOT / 'web' / 'settings.js':
        data += b'\n' + (ROOT / 'web' / 'file_manager.js').read_bytes()
    return data
