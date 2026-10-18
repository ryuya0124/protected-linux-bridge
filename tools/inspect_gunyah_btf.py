#!/usr/bin/env python3
"""Inspect offline kernel type metadata; never loads BPF or touches a device.

Read only whitelisted Gunyah enums/structure member names. BTF has type metadata,
not kernel instruction bodies; this cannot establish the exact executing code.
Do not commit the host's raw BTF or private probe logs.
"""

import argparse
import json
from pathlib import Path
import struct

ENUMS = {"gunyah_error", "gunyah_vcpu_exit", "gunyah_map_flags"}
STRUCTS = {"gunyah_vm", "gunyah_vm_binding", "gunyah_userspace_memory_region"}
MAX_SIZE = 64 * 1024 * 1024


def inspect(data):
    if len(data) < 24 or len(data) > MAX_SIZE:
        raise ValueError("Invalid BTF size")
    magic, version, flags, header, types, types_size, strings, strings_size = struct.unpack_from(
        "<HBB5I", data
    )
    if magic != 0xEB9F or version != 1 or flags or not 24 <= header <= len(data):
        raise ValueError("Unsupported BTF header")
    if header + types + types_size > len(data) or header + strings + strings_size > len(data):
        raise ValueError("BTF section outside file")
    names = data[header + strings : header + strings + strings_size]
    if not names or names[0] or names[-1]:
        raise ValueError("Invalid BTF string table")

    def name(offset):
        if offset >= len(names):
            raise ValueError("BTF string offset outside table")
        end = names.find(b"\0", offset)
        if end < 0:
            raise ValueError("Unterminated BTF string")
        return names[offset:end].decode("utf-8")

    result = {"enums": {}, "structures": {}}
    at, end = header + types, header + types + types_size
    while at < end:
        if end - at < 12:
            raise ValueError("Truncated BTF type")
        offset, info, size = struct.unpack_from("<3I", data, at)
        at += 12
        kind, count, label = info >> 24 & 31, info & 0xFFFF, name(offset)
        lengths = {
            1: 4,
            2: 0,
            3: 12,
            4: 12 * count,
            5: 12 * count,
            6: 8 * count,
            7: 0,
            8: 0,
            9: 0,
            10: 0,
            11: 0,
            12: 0,
            13: 8 * count,
            14: 4,
            15: 12 * count,
            16: 0,
            17: 4,
            18: 0,
            19: 12 * count,
        }
        if kind not in lengths or at + lengths[kind] > end:
            raise ValueError("Unknown or truncated BTF type payload")
        payload = data[at : at + lengths[kind]]
        if kind == 6 and label in ENUMS:
            signed = bool(info >> 31)
            result["enums"][label] = {
                name(offset): value
                for offset, value in struct.iter_unpack("<Ii" if signed else "<II", payload)
            }
        if kind == 4 and label in STRUCTS:
            members = []
            for offset, type_id, bit_offset in struct.iter_unpack("<3I", payload):
                members.append(
                    {
                        "name": name(offset),
                        "bit_offset": bit_offset & 0xFFFFFF if info >> 31 else bit_offset,
                    }
                )
            result["structures"][label] = {"bytes": size, "members": members}
        at += lengths[kind]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("btf", type=Path)
    args = parser.parse_args()
    if args.btf.stat().st_size > MAX_SIZE:
        parser.error("BTF exceeds bound")
    print(json.dumps(inspect(args.btf.read_bytes()), indent=2))


if __name__ == "__main__":
    main()
