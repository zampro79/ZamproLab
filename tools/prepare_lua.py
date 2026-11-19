#!/usr/bin/env python3
"""Acquire the official Lua release into the project-local, ignored dependency cache."""
import hashlib
import os
from pathlib import Path
import re
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / ".deps"
DEST = CACHE / "lua-5.5.1"
URL = "https://www.lua.org/ftp/lua-5.5.1.tar.gz"
ARCHIVE_SHA256 = "1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce"
MANIFEST = ROOT / "components/zam_runtime_lua/lua-5.5.1-sha256.cmake"


def verify_sources(directory):
    entries = re.findall(r'"([^";]+);([0-9a-f]{64})"', MANIFEST.read_text())
    if len(entries) != 61:
        raise ValueError("Invalid Lua source checksum manifest")
    for name, digest in entries:
        path = directory / "src" / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Lua source mismatch: {name}")


def main():
    CACHE.mkdir(exist_ok=True)
    if DEST.exists() or DEST.is_symlink():
        if DEST.is_symlink() or not DEST.is_dir():
            raise ValueError(f"Refusing to replace existing path: {DEST}")
        verify_sources(DEST)
        print("Lua 5.5.1 already present and verified.")
        return
    with tempfile.TemporaryDirectory(prefix="lua-5.5.1-", dir=CACHE) as tmp:
        tmpdir = Path(tmp)
        archive = tmpdir / "lua.tar.gz"
        digest = hashlib.sha256()
        total = 0
        with urllib.request.urlopen(URL, timeout=60) as response, archive.open("wb") as output:
            while True:
                chunk = response.read(65536)
                if not chunk:
                    break
                total += len(chunk)
                if total > 2 * 1024 * 1024:
                    raise ValueError("Lua archive exceeds the expected size limit")
                digest.update(chunk)
                output.write(chunk)
        if digest.hexdigest() != ARCHIVE_SHA256:
            raise ValueError("Official Lua archive SHA-256 mismatch")
        with tarfile.open(archive, "r:gz") as tar:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if not parts or parts[0] != "lua-5.5.1" or ".." in parts or not (member.isfile() or member.isdir()):
                    raise ValueError("Unexpected path or link in Lua archive")
            tar.extractall(tmpdir)
        source = tmpdir / "lua-5.5.1"
        verify_sources(source)
        # Create exclusively: never replace a directory created by another user/process.
        DEST.mkdir()
        for child in source.iterdir():
            os.rename(child, DEST / child.name)
    print("Lua 5.5.1 downloaded and verified.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"Lua preparation failed: {exc}", file=sys.stderr)
        sys.exit(1)
