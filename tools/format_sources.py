#!/usr/bin/env python3
"""Format first-party source only. Tool binaries/caches stay in ignored artifacts.

Expected tools: clang-format 23, google-java-format 1.33.0, ktfmt 0.61,
Black 26.10.1, shfmt 3.12.0 and Prettier 3.6.2. Dependencies and Gradle's
wrapper are upstream source and intentionally excluded.
"""

from pathlib import Path
import argparse
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "artifacts/format-tools"


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", default="java")
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--black", default="black")
    parser.add_argument("--shfmt", default=str(CACHE / "shfmt"))
    parser.add_argument("--prettier", default=str(CACHE / "node_modules/.bin/prettier"))
    args = parser.parse_args()
    for name in ("google-java-format.jar", "ktfmt.jar"):
        if not (CACHE / name).is_file():
            parser.error(f"Install {name} into {CACHE}; see docs/code-style.md")
    # Deliberately enumerate source roots rather than formatting caches or vendor code.
    c = [
        p
        for folder in ("native", "tools", "tests", "kernels")
        for p in (ROOT / folder).rglob("*")
        if p.suffix in (".c", ".h")
    ]
    java = [
        p for folder in ("app/src", "native/clipboard") for p in (ROOT / folder).rglob("*.java")
    ]
    run(args.clang_format, "-i", *c)
    run(args.java, "-jar", CACHE / "google-java-format.jar", "--replace", *java)
    run(
        args.java,
        "-jar",
        CACHE / "ktfmt.jar",
        "--google-style",
        "build.gradle.kts",
        "settings.gradle.kts",
        "app/build.gradle.kts",
    )
    run(args.black, "tools", "tests", "native/clipboard_watch.py")
    run(
        args.shfmt,
        "-w",
        "-i",
        "4",
        "native/plb-network",
        "native/plb-install-gui",
        "native/plb-install-gpu",
        "native/plb-install-integration",
        "kernels/build.sh",
    )
    run(
        args.prettier,
        "--write",
        "app/src/main/assets/web/display.html",
        "app/src/main/assets/web/terminal.html",
        "kernels/provenance.json",
    )
    ET.register_namespace("android", "http://schemas.android.com/apk/res/android")
    ET.register_namespace("tools", "http://schemas.android.com/tools")
    for path in (ROOT / "app/src").rglob("*.xml"):
        tree = ET.parse(path)
        ET.indent(tree, space="  ")
        tree.write(path, encoding="utf-8", xml_declaration=True)
        with path.open("ab") as out:
            out.write(b"\n")


if __name__ == "__main__":
    main()
