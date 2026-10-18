#!/usr/bin/env python3
"""Bounded newc reader and guest-only low-RAM property override.

Preserves the original init/vendor archives. Appends a small third cpio with
only the ramdisk build properties. Host Android files and policies are untouched.
"""

import stat
from pathlib import Path

MAX_ARCHIVE = 128 * 1024 * 1024
PROPERTY_PATH = "system/etc/ramdisk/build.prop"


def entries(data):
    if len(data) > MAX_ARCHIVE:
        raise ValueError("Guest ramdisk exceeds bound")
    at = 0
    while at < len(data):
        if data[at] == 0:
            at += 1
            continue
        if at + 110 > len(data) or data[at : at + 6] != b"070701":
            raise ValueError("Expected complete newc archive")
        fields = [int(data[at + 6 + i * 8 : at + 14 + i * 8], 16) for i in range(13)]
        size, namesize = fields[6], fields[11]
        if not 1 < namesize <= 4096 or at + 110 + namesize > len(data):
            raise ValueError("Invalid cpio name")
        encoded = data[at + 110 : at + 110 + namesize]
        if encoded[-1:] != b"\0":
            raise ValueError("Unterminated cpio name")
        name = encoded[:-1].decode("utf-8")
        if name.startswith("/") or ".." in name.split("/"):
            raise ValueError("Unsafe cpio path")
        start = (at + 110 + namesize + 3) & ~3
        if start + size > len(data):
            raise ValueError("Truncated cpio entry")
        yield name, fields[1], data[start : start + size]
        at = (start + size + 3) & ~3


def lowram_archive(original):
    properties = None
    for name, mode, payload in entries(original):
        if name == PROPERTY_PATH:
            if not stat.S_ISREG(mode) or len(payload) > 64 * 1024:
                raise ValueError("Unexpected ramdisk properties")
            properties = payload
    if properties is None:
        raise ValueError("Ramdisk build properties missing")
    properties += (
        b"\n# Protected Linux Bridge guest-only diagnostic profile\n"
        b"ro.config.low_ram=true\nro.sf.lcd_density=160\n"
    )
    extra = bytearray()
    for inode, name, mode, payload in (
        (100001, PROPERTY_PATH, stat.S_IFREG | 0o644, properties),
        (100002, "TRAILER!!!", 0, b""),
    ):
        encoded = name.encode() + b"\0"
        fields = [inode, mode, 0, 0, 1, 0, len(payload), 0, 0, 0, 0, len(encoded), 0]
        extra.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode() + encoded)
        extra.extend(b"\0" * (-len(extra) % 4))
        extra.extend(payload)
        extra.extend(b"\0" * (-len(extra) % 4))
    return original + extra


def write_lowram(original, destination):
    data = lowram_archive(Path(original).read_bytes())
    with Path(destination).open("xb") as output:
        output.write(data)
