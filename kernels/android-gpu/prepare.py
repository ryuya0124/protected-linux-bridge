#!/usr/bin/env python3
"""Extract pinned guest sources on the builder's case-sensitive Linux volume."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import os
import tempfile
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
patch = Path("/recipe/guest-bound-dma-sync.patch")
patched = {
    "drivers/gpu/drm/virtio/virtgpu_drv.h",
    "drivers/gpu/drm/virtio/virtgpu_ioctl.c",
    "drivers/gpu/drm/virtio/virtgpu_vq.c",
}
resume = source.exists()
if resume and os.environ.get("PLB_RESUME_BUILD") != "1":
    raise ValueError("Expected this invocation's empty Linux build volume")
with tempfile.TemporaryDirectory(dir="/work") as directory:
    pristine = Path(directory)
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle:
            if member.name != source.name and not member.name.startswith(source.name + "/"):
                raise ValueError("Unexpected archive root")
            if not resume:
                bundle.extract(member, path=source.parent, filter="data")
                continue
            relative = Path(member.name).relative_to(source.name).as_posix()
            target = Path("/work") / member.name
            if member.isfile():
                if relative in patched:
                    destination = pristine / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(bundle.extractfile(member).read())
                elif (
                    not target.is_file()
                    or target.is_symlink()
                    or digest(target)
                    != hashlib.sha256(bundle.extractfile(member).read()).hexdigest()
                ):
                    raise ValueError("Resumed source differs from the pinned archive: " + relative)
            elif member.issym() and (
                not target.is_symlink()
                or os.path.normpath(target.parent / os.readlink(target))
                != os.path.normpath(target.parent / member.linkname)
            ):
                raise ValueError("Resumed source symlink differs: " + relative)
    if resume:
        subprocess.run(["patch", "-p1", "-d", str(pristine), "-i", str(patch)], check=True)
        for relative in patched:
            if (source / relative).is_symlink() or digest(source / relative) != digest(
                pristine / relative
            ):
                raise ValueError("Resumed driver does not match the guest patch")
# The two files have different semantics and must coexist, including objects.
lower = source / "net/netfilter/xt_hl.c"
upper = source / "net/netfilter/xt_HL.c"
if not lower.is_file() or not upper.is_file() or lower.samefile(upper):
    raise ValueError("The guest kernel build requires a case-sensitive filesystem")
shutil.copyfile(base, "/work/base.config")
command = ["patch", "-p1", "-d", str(source), "-i", "/recipe/guest-bound-dma-sync.patch"]
if not resume:
    subprocess.run(command + ["--dry-run"], check=True)
    subprocess.run(command, check=True)
