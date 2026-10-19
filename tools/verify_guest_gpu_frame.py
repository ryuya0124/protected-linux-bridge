#!/usr/bin/env python3
"""Offline validation of one nonce-bound guest GLES result and its PNG.

Never connects to Android or starts a VM. Renderer metadata, two-phase GL
readback and a matching screenshot are separate required pieces of evidence.
This verifies the test workload, not home-screen acceleration or stability.
"""

import argparse
import json
from pathlib import Path
import re
import struct
import zlib

WIDTH, HEIGHT = 480, 800
MAX_PNG = 2 * 1024 * 1024
SOFTWARE_NAMES = ("swiftshader", "llvmpipe", "softpipe", "lavapipe", "software rasterizer")
SAMPLE_COLORS = (
    (120, 200, (255, 0, 0, 255)),
    (360, 200, (255, 255, 0, 255)),
    (120, 600, (0, 0, 255, 255)),
    (360, 600, (0, 255, 0, 255)),
    (240, 400, (255, 255, 255, 255)),
)


def png_samples(data):
    """CRC-check and decode bounded non-interlaced RGB/RGBA screencap output."""
    if not 45 <= len(data) <= MAX_PNG or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("PNG signature/size")
    offset, chunks, channels = 8, 0, 0
    compressed = bytearray()
    ended = False
    idat_ended = False
    for _ in range(1024):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        if size > MAX_PNG or offset + size + 12 > len(data):
            raise ValueError("PNG chunk size")
        payload = data[offset + 8 : offset + 8 + size]
        checksum = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != checksum:
            raise ValueError("PNG CRC")
        offset += size + 12
        if chunks == 0 and kind != b"IHDR":
            raise ValueError("PNG must start with IHDR")
        chunks += 1
        if kind == b"IHDR":
            if chunks != 1 or size != 13:
                raise ValueError("PNG IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (width, height, depth, compression, filtering, interlace) != (
                WIDTH,
                HEIGHT,
                8,
                0,
                0,
                0,
            ):
                raise ValueError("unsupported PNG geometry/encoding")
            if color not in (2, 6):
                raise ValueError("expected RGB/RGBA PNG")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            if idat_ended:
                raise ValueError("nonconsecutive PNG IDAT")
            compressed.extend(payload)
        elif kind == b"IEND":
            if size or offset != len(data):
                raise ValueError("PNG IEND or trailing data")
            ended = True
            break
        else:
            if not re.fullmatch(b"[a-zA-Z]{4}", kind) or not kind[0] & 32:
                raise ValueError("unsupported critical PNG chunk")
            if compressed:
                idat_ended = True
    if not ended or not channels or not compressed:
        raise ValueError("incomplete PNG")
    expected = HEIGHT * (WIDTH * channels + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("invalid/oversized PNG raster")
    centers = [(x, y) for x, y, _ in SAMPLE_COLORS]
    centers += [((index * 2 + 1) * WIDTH // 64, HEIGHT - HEIGHT // 25) for index in range(32)]
    wanted = {(x + dx, y + dy) for x, y in centers for dx in (-2, 0, 2) for dy in (-2, 0, 2)}
    samples = {}
    previous = bytearray(WIDTH * channels)
    stride = WIDTH * channels
    for y in range(HEIGHT):
        start = y * (stride + 1)
        method = raw[start]
        if method > 4:
            raise ValueError("invalid PNG row filter")
        row = bytearray(raw[start + 1 : start + 1 + stride])
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if method == 1:
                predictor = left
            elif method == 2:
                predictor = up
            elif method == 3:
                predictor = (left + up) // 2
            elif method == 4:
                estimate = left + up - upper_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - upper_left))
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        for x, sample_y in wanted:
            if sample_y == y:
                pixel = tuple(row[x * channels : (x + 1) * channels])
                samples[x, y] = pixel if channels == 4 else pixel + (255,)
        previous = row
    return samples


def verify(log, png, nonce):
    if not re.fullmatch("[0-9a-f]{32}", nonce):
        raise ValueError("nonce format")
    if len(log) > 256 * 1024:
        raise ValueError("log exceeds bound")
    records = []
    for line in log.splitlines():
        marker = "PLB_GPU_PROBE_RESULT "
        if marker not in line:
            continue
        record = json.loads(line.split(marker, 1)[1])
        if not isinstance(record, dict):
            raise ValueError("result must be an object")
        if record.get("nonce") == nonce:
            records.append(record)
    if len(records) != 1:
        raise ValueError("missing/ambiguous nonce-bound probe result")
    result = records[0]
    if result.get("probe") != "plb-gles-v1" or result.get("reason") != "two_phase_readback":
        raise ValueError("probe protocol or failure result")
    for key in ("readback_ok", "phase0_readback_ok", "phase1_readback_ok"):
        if result.get(key) is not True:
            raise ValueError("GLES readback failed or incomplete")
    if tuple(result.get(key) for key in ("width", "height", "frame")) != (WIDTH, HEIGHT, 22):
        raise ValueError("GLES geometry or frame count")
    metadata = [result.get(key) for key in ("vendor", "renderer", "version")]
    if any(not isinstance(item, str) or not item or len(item) > 400 for item in metadata):
        raise ValueError("renderer metadata")
    renderer = " ".join(metadata).lower()
    if any(name in renderer for name in SOFTWARE_NAMES) or "adreno" not in metadata[1].lower():
        raise ValueError("renderer is software or not the expected Adreno path")
    samples = png_samples(png)
    expected_colors = list(SAMPLE_COLORS)
    for index, digit in enumerate(nonce):
        value = int(digit, 16) * 17
        expected_colors.append(
            ((index * 2 + 1) * WIDTH // 64, HEIGHT - HEIGHT // 25, (value, 255 - value, 127, 255))
        )
    for x, y, color in expected_colors:
        for dx in (-2, 0, 2):
            for dy in (-2, 0, 2):
                actual = samples[x + dx, y + dy]
                if any(abs(a - b) > 3 for a, b in zip(actual, color)):
                    raise ValueError("screen does not match the final GLES pattern")
    return {
        "probe": "plb-gles-v1",
        "nonce": nonce,
        "two_phase_readback_verified": True,
        "screenshot_pattern_verified": True,
        "reported_renderer": result["renderer"],
        "android_home_gpu_verified": False,
        "scope": "nonce-bound diagnostic GLES drawing only; not a stability or attestation claim",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--png", type=Path, required=True)
    parser.add_argument("--nonce", required=True)
    args = parser.parse_args()
    try:
        if args.log.stat().st_size > 256 * 1024 or args.png.stat().st_size > MAX_PNG:
            raise ValueError("input file exceeds bound")
        result = verify(args.log.read_text(), args.png.read_bytes(), args.nonce)
    except (ValueError, OSError, zlib.error, RecursionError) as error:
        parser.exit(1, f"Not verified: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
