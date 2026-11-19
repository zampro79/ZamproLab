#!/usr/bin/env python3
"""Verify preparation, offline reuse, tampering and refusal to replace files."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
from unittest import mock

root = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("prepare_lua", root / "tools/prepare_lua.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(prefix="zam-lua-acquire-") as temp:
    module.ROOT = Path(temp)
    module.CACHE = module.ROOT / ".deps"
    module.DEST = module.CACHE / "lua-5.5.1"
    module.MANIFEST = module.ROOT / "manifest.cmake"
    payload = io.BytesIO()
    entries = []
    with tarfile.open(fileobj=payload, mode="w:gz") as tar:
        for i in range(61):
            name = f"file{i}.c"
            data = f"source {i}\n".encode()
            info = tarfile.TarInfo(f"lua-5.5.1/src/{name}")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
            entries.append(f'"{name};{hashlib.sha256(data).hexdigest()}"')
    module.MANIFEST.write_text("\n".join(entries))
    content = payload.getvalue()
    module.ARCHIVE_SHA256 = hashlib.sha256(content).hexdigest()
    with mock.patch.object(module.urllib.request, "urlopen", return_value=io.BytesIO(content)):
        module.main()
    with mock.patch.object(module.urllib.request, "urlopen", side_effect=AssertionError("network requested")):
        module.main()
    victim = module.DEST / "src/file0.c"
    victim.write_text("local change")
    try:
        module.main()
        raise AssertionError("tampered sources accepted")
    except ValueError:
        assert victim.read_text() == "local change"
    module.CACHE = module.ROOT / "other-cache"
    module.DEST = module.CACHE / "lua-5.5.1"
    module.ARCHIVE_SHA256 = "0" * 64
    with mock.patch.object(module.urllib.request, "urlopen", return_value=io.BytesIO(content)):
        try:
            module.main()
            raise AssertionError("tampered archive accepted")
        except ValueError:
            assert not module.DEST.exists()
    print("Lua acquisition: verified install, offline reuse and tamper preservation PASS")
