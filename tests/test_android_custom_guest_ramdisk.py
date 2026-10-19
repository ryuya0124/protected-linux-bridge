"""A built-in-only guest keeps original init, security properties and binaries."""

import json
from pathlib import Path
import stat
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from android_ramdisk import entries
from build_android4g_probe import custom_ramdisk
from test_android_ramdisk import archive


class CustomGuestRamdiskTest(unittest.TestCase):
    def test_original_members_and_security_settings_remain_byte_identical(self):
        original = b"".join(
            archive(name, data)
            for name, data in (
                ("init", b"original-init"),
                ("lib/modules/modules.load", b"virtio-gpu.ko\n"),
                ("lib/modules/virtio-gpu.ko", b"original-module"),
                ("system/etc/ramdisk/build.prop", b"ro.adb.secure=1\n"),
                ("first_stage_ramdisk/fstab.ranchu", b"avb=vbmeta,logical\n"),
            )
        )
        record = {"kernel_sha256": "0" * 64, "kernel_release": "6.12.74-plb-android-gpu1"}
        result = custom_ramdisk(original, record)
        self.assertEqual(result[: len(original)], original)
        old = {name: (mode, data) for name, mode, data in entries(original)}
        new = {name: (mode, data) for name, mode, data in entries(result)}
        for name in old:
            if name not in ("lib/modules/modules.load", "TRAILER!!!"):
                self.assertEqual(new[name], old[name])
        self.assertEqual(new["lib/modules/modules.load"][1], b"")
        marker = json.loads(new["plb-guest-kernel.json"][1])
        self.assertEqual(marker, {"guest_only": True, **record})
        self.assertEqual(set(new) - set(old), {"plb-guest-kernel.json"})

    def test_missing_or_symlinked_module_list_is_rejected(self):
        for original in (
            archive("init", b"original-init"),
            archive("lib/modules/modules.load", b"elsewhere", stat.S_IFLNK | 0o777),
        ):
            with self.subTest(original=original), self.assertRaises(ValueError):
                custom_ramdisk(original, {})
