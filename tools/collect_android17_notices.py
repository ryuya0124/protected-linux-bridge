#!/usr/bin/env python3
"""Collect unmodified notices from the exact private Android 17 source caches.

The inventory is a superset, not a claim that every optional crate/header is
linked. Never copy OS images, binaries, source logs, keys or absolute host paths.
"""

from pathlib import Path
import subprocess
import re

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/android17-vmm"
OUTPUT = ROOT / "third_party/android-gpu4g/ANDROID17_NOTICES.txt"


def notices(tree):
    for path in sorted(tree.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(tree)
        if any(part in {".git", "target", "build", "node_modules"} for part in relative.parts):
            continue
        if path.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT")):
            if path.stat().st_size > 2 * 1024 * 1024:
                raise ValueError("Unexpected notice size")
            yield relative.as_posix(), path.read_text(encoding="utf-8")


def main():
    sources = [
        ("crosvm", BASE / "crosvm-source", "c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8"),
        ("gfxstream", BASE / "gfxstream-source", "b46f0d4b4545def09bdf30651d4bf848b2ba0c34"),
    ]
    content = ["Android 17 source-built runtime notices. Optional/build dependencies included.\n"]
    for name, tree, revision in sources:
        actual = subprocess.check_output(
            ["git", "-C", str(tree), "rev-parse", "HEAD"], text=True
        ).strip()
        if actual != revision:
            raise ValueError("Source revision mismatch: " + name)
        for relative, text in notices(tree):
            content.append(f"\n===== {name}@{revision}/{relative} =====\n\n{text}")
    # Cargo.lock also lists unused platform/feature dependencies absent from a
    # baseline build. Compiler depfiles identify the actual cached source crates.
    registry = ROOT / "artifacts/android4g-gpu/vmm-build/cargo-home/registry/src"
    packages = set()
    for folder in [
        BASE / "build/target/aarch64-linux-android/release/deps",
        BASE / "build/target/release/deps",
    ]:
        for depfile in folder.glob("*.d"):
            packages.update(re.findall(r"registry/src/[^/\s]+/([^/\s]+)/", depfile.read_text()))
    if not packages:
        raise ValueError("Missing compiler dependency inventory")
    for name in sorted(packages):
        trees = list(registry.glob("*/" + name))
        if len(trees) != 1:
            raise ValueError("Missing or ambiguous registry notices: " + name)
        for relative, text in notices(trees[0]):
            content.append(f"\n===== registry/{name}/{relative} =====\n\n{text}")
    OUTPUT.write_text("\n".join(content))
    print("ANDROID17_NOTICES_WRITTEN", OUTPUT.stat().st_size)


if __name__ == "__main__":
    main()
