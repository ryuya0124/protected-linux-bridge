#!/usr/bin/env python3
"""Pinned, separately licensed guest-only zerofree. No host package installation."""

from pathlib import Path
import hashlib, io, tarfile, urllib.request, shutil

FILES = {
    "zerofree_1.1.1-1+b2_arm64.deb": "1b96d1b2b3ff430dc0804634397d166fed6fdcd3e7af9bb8bb5cedc330492e12",
    "zerofree_1.1.1.orig.tar.gz": "956bc861b55ba0a2b7593c58d32339dab1a0e7da6ea2b813d27c80f08b723867",
    "zerofree_1.1.1-1.debian.tar.xz": "8048fe2c7f921ae3e838f7c78efcc2634b6235e5832d78f976c853217a4c5bf3",
}


def fetch(root: Path, out: Path) -> Path:
    cache = root / "app/build/storage-runtime"
    cache.mkdir(parents=True, exist_ok=True)
    for name, sha in FILES.items():
        dest = cache / name
        if not dest.exists():
            local = root / "artifacts/storage-tools" / name
            if local.is_file():
                shutil.copyfile(local, dest)
            else:
                with urllib.request.urlopen(
                    "https://deb.debian.org/debian/pool/main/z/zerofree/" + name, timeout=45
                ) as r:
                    dest.write_bytes(r.read(200000))
        if hashlib.sha256(dest.read_bytes()).hexdigest() != sha:
            raise RuntimeError("Storage dependency checksum mismatch: " + name)
    # Parse ar and read only one bounded regular file; never extract archive paths.
    data = (cache / "zerofree_1.1.1-1+b2_arm64.deb").read_bytes()
    if data[:8] != b"!<arch>\n":
        raise RuntimeError("Bad Debian ar")
    at = 8
    payload = None
    while at + 60 <= len(data):
        head = data[at : at + 60]
        size = int(head[48:58])
        name = head[:16].decode().strip().rstrip("/")
        at += 60
        if name.startswith("data.tar"):
            payload = data[at : at + size]
        at += size + size % 2
    if payload is None:
        raise RuntimeError("Missing Debian data archive")
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:*") as tar:
        members = [
            m
            for m in tar
            if m.name in ("./usr/sbin/zerofree", "usr/sbin/zerofree")
            and m.isfile()
            and 0 < m.size < 100000
        ]
        if len(members) != 1:
            raise RuntimeError("Ambiguous zerofree")
        binary = tar.extractfile(members[0]).read()
    if binary[:4] != b"\x7fELF" or int.from_bytes(binary[18:20], "little") != 183:
        raise RuntimeError("Not ARM64 ELF")
    target = cache / "plb-zerofree"
    target.write_bytes(binary)
    # Distribute complete corresponding source alongside the separate executable in APK.
    sources = out.parent / "storage-sources"
    sources.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        if not name.endswith(".deb"):
            # AAPT transparently gunzips *.gz assets and strips that suffix.
            # Keep the pinned original bytes and checksum by adding a packaging suffix.
            target_name = name + ".bin" if name.endswith(".gz") else name
            old = sources / name
            if name.endswith(".gz") and old.is_file():
                old.unlink()
            shutil.copyfile(cache / name, sources / target_name)
    return target
