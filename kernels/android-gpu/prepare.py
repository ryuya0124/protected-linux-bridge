#!/usr/bin/env python3
"""Extract pinned guest sources on the builder's case-sensitive Linux volume."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


source = Path("/work/kernel_common-3ec022196c4e9d5c1434599cdda63f622dd6f586")
archive = Path("/inputs/archive.tar.gz")
base = Path("/inputs/base.config")
if digest(archive) != "13ab707b031ace652b6d7d111dfe4f877e186e1bc0c80925ea19c34dc0c3e531":
    raise ValueError("Source archive identity mismatch")
if digest(base) != "4c065ae7247d878dc34438e61230fb4c3e4a9ce286cd585bf49db03dd7e69726":
    raise ValueError("Original guest config identity mismatch")
if source.exists():
    raise ValueError("Expected this invocation's empty Linux build volume")
with tarfile.open(archive, "r:gz") as bundle:
    for member in bundle:
        if member.name != source.name and not member.name.startswith(source.name + "/"):
            raise ValueError("Unexpected archive root")
        bundle.extract(member, path=source.parent, filter="data")
# The two files have different semantics and must coexist, including objects.
lower = source / "net/netfilter/xt_hl.c"
upper = source / "net/netfilter/xt_HL.c"
if not lower.is_file() or not upper.is_file() or lower.samefile(upper):
    raise ValueError("The guest kernel build requires a case-sensitive filesystem")
shutil.copyfile(base, "/work/base.config")
command = ["patch", "-p1", "-d", str(source), "-i", "/recipe/guest-bound-dma-sync.patch"]
subprocess.run(command + ["--dry-run"], check=True)
subprocess.run(command, check=True)
