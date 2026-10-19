"""Synthetic CPU fixtures test the evidence checker; they are not GPU results."""

import base64
import json
from pathlib import Path
import re
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_guest_gpu_payload import commands
from verify_guest_gpu_frame import verify, png_samples, WIDTH, HEIGHT

NONCE = "0123456789abcdef0123456789abcdef"


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def fixture_png(nonce=NONCE, phase=1, channels=4, filtering=0):
    # Independent raster construction uses screen coordinates, unlike GLSL's
    # bottom-origin gl_FragCoord. A nonce stripe must survive both readback paths.
    raw = bytearray()
    previous = bytes(WIDTH * channels)
    for y in range(HEIGHT):
        row = bytearray()
        for x in range(WIDTH):
            if y < HEIGHT // 2:
                rgb = ((255, 0, 0) if phase else (0, 0, 255)) if x < WIDTH // 2 else (255, 255, 0)
            else:
                rgb = ((0, 0, 255) if phase else (255, 0, 0)) if x < WIDTH // 2 else (0, 255, 0)
            if abs((x + 0.5) / WIDTH - 0.5) < 0.08 and abs((y + 0.5) / HEIGHT - 0.5) < 0.08:
                rgb = (255, 255, 255)
            if y >= HEIGHT - 64:
                value = int(nonce[x * 32 // WIDTH], 16) * 17
                rgb = (value, 255 - value, 127)
            row.extend(rgb + ((255,) if channels == 4 else ()))
        encoded = bytearray()
        for index, value in enumerate(row):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filtering == 1:
                predicted = left
            elif filtering == 2:
                predicted = up
            elif filtering == 3:
                predicted = (left + up) // 2
            elif filtering == 4:
                estimate = left + up - upper_left
                a, b, c = abs(estimate - left), abs(estimate - up), abs(estimate - upper_left)
                predicted = left if a <= b and a <= c else up if b <= c else upper_left
            else:
                predicted = 0
            encoded.append((value - predicted) & 255)
        raw.append(filtering)
        raw.extend(encoded)
        previous = row
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6 if channels == 4 else 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def fixture_log(**changes):
    result = dict(
        probe="plb-gles-v1",
        nonce=NONCE,
        vendor="Google (Qualcomm)",
        renderer="Android Emulator OpenGL ES Translator (Adreno (TM) 840)",
        version="OpenGL ES 3.0",
        width=WIDTH,
        height=HEIGHT,
        frame=22,
        readback_ok=True,
        phase0_readback_ok=True,
        phase1_readback_ok=True,
        reason="two_phase_readback",
    )
    result.update(changes)
    return "PLBGpuProbe: PLB_GPU_PROBE_RESULT " + json.dumps(result) + "\n"


class GuestGpuEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.png = fixture_png()

    def test_two_phase_result_and_full_nonce_bound_screenshot(self):
        result = verify(fixture_log(), self.png, NONCE)
        self.assertTrue(result["screenshot_pattern_verified"])
        self.assertFalse(result["android_home_gpu_verified"])

    def test_nonce_in_log_and_complete_texture_must_match(self):
        other = NONCE[:-1] + "e"
        for log, png in (
            (fixture_log(nonce=other), self.png),
            (fixture_log(), fixture_png(nonce=other)),
        ):
            with self.assertRaises(ValueError):
                verify(log, png, NONCE)

    def test_software_missing_metadata_duplicate_and_failed_readback(self):
        for changes in (
            dict(renderer="SwiftShader Adreno"),
            dict(renderer="llvmpipe"),
            dict(renderer=""),
            dict(phase0_readback_ok=False),
            dict(phase1_readback_ok=False),
            dict(readback_ok="true"),
            dict(frame=21),
            dict(width=800),
            dict(reason="gl_error"),
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                verify(fixture_log(**changes), self.png, NONCE)
        with self.assertRaises(ValueError):
            verify(fixture_log() * 2, self.png, NONCE)

    def test_stale_first_phase_cannot_pass_final_screenshot(self):
        with self.assertRaises(ValueError):
            verify(fixture_log(), fixture_png(phase=0), NONCE)

    def test_actual_png_crc_truncation_and_trailing_bytes(self):
        damaged = bytearray(self.png)
        damaged[60] ^= 1
        for data in (bytes(damaged), self.png[:-1], self.png + b"tail", b"root denied"):
            with self.subTest(size=len(data)), self.assertRaises((ValueError, zlib.error)):
                png_samples(data)

    def test_rgb_rgba_and_all_five_png_filters(self):
        for filtering in range(5):
            with self.subTest(filtering=filtering):
                verify(
                    fixture_log(),
                    fixture_png(channels=3 if filtering % 2 else 4, filtering=filtering),
                    NONCE,
                )

    def test_decompression_beyond_geometry_bound_is_rejected(self):
        header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
        oversized = b"\0" * (HEIGHT * (WIDTH * 4 + 1) + 1)
        png = (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(oversized))
            + chunk(b"IEND", b"")
        )
        with self.assertRaises(ValueError):
            png_samples(png)


class GuestGpuPayloadTests(unittest.TestCase):
    def test_chunk_boundaries_reconstruct_exact_payload_and_keep_guest_identity_checks(self):
        data = b"PK\x03\x04" + bytes(range(256)) * 127
        payload = commands(data)
        decoded = bytearray()
        for part in payload["chunks"]:
            # The base64 alphabet stays literal even inside shlex's outer quoting.
            block = re.search(r"[A-Za-z0-9+/=]{100,}", part).group(0)
            decoded.extend(base64.b64decode(block))
            self.assertLess(len(part.replace("@NONCE@", NONCE)), 8192)
        self.assertEqual(bytes(decoded), data)
        for part in [payload["begin"], *payload["chunks"], payload["finish"], payload["cleanup"]]:
            for expected in ("vsoc_arm64_only", "cutf_cvm", "16373615", "id -u"):
                self.assertIn(expected, part)
        self.assertIn("sha256sum", payload["finish"])
        self.assertNotIn("rm -rf", payload["cleanup"])

    def test_unknown_payloads_and_unbounded_apks_rejected(self):
        for data in (b"", b"not an apk" * 200, b"PK\x03\x04" + b"x" * 65536):
            with self.assertRaises(ValueError):
                commands(data)
