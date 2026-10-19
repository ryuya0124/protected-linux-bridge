#!/usr/bin/env python3
"""Build a fixed, read-only guest framework metadata helper; no device I/O."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import platform
import shlex
import subprocess
import io
import zipfile

from build_guest_gpu_payload import IDENTITY

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    args = parser.parse_args()
    output = ROOT / "artifacts/android4g-gpu/network-inspector"
    output.mkdir(parents=True, exist_ok=True)
    apk = (
        ROOT
        / "artifacts/android4g-gpu/surface-probe/build/outputs/apk/release/PlbSurfaceProbe-release.apk"
    )
    build_tools = args.sdk / "build-tools/36.0.0"
    subprocess.run([str(build_tools / "apksigner"), "verify", str(apk)], check=True)
    badging = subprocess.check_output(
        [str(build_tools / "aapt"), "dump", "badging", str(apk)], text=True
    )
    if (
        not badging.startswith("package: name='net.ryuya_dev.protectedlinux.surfaceprobe' ")
        or "application-debuggable" in badging
        or "uses-permission:" in badging
    ):
        raise ValueError("Expected permission-free, non-debuggable inspector payload")
    # APK signing/package checks authenticate the input. Transfer only the DEX
    # archive used by app_process, not Android resources or install an app.
    with zipfile.ZipFile(apk) as source:
        dex = source.read("classes.dex")
    if len(dex) > 60000:
        raise ValueError("Metadata DEX exceeds its explicit size bound")
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as target:
        target.writestr("classes.dex", dex)
    data = archive.getvalue()
    if not 1024 <= len(data) <= 65536:
        raise ValueError("Metadata inspector must fit the bounded APK envelope")
    digest = hashlib.sha256(data).hexdigest()
    directory = "/data/local/tmp/plb-network-inspect-" + digest[:20]
    remote = directory + "/inspector.jar"

    def command(script):
        return "exec:sh -c " + shlex.quote(IDENTITY + script + "; echo PLB_NETWORK_INSPECT_STEP_OK")

    commands = [command("umask 077; mkdir " + directory + "; : > " + remote)]
    encoded = base64.b64encode(data).decode("ascii")
    for at in range(0, len(encoded), 4096):
        commands.append(
            command("printf %s '" + encoded[at : at + 4096] + "' | base64 -d >> " + remote)
        )
    commands.append(
        command(
            '[ "$(sha256sum ' + remote + ' | cut -d" " -f1)" = ' + digest + " ]; "
            "CLASSPATH=" + remote + " app_process -Xmx32m /system/bin "
            "net.ryuya_dev.protectedlinux.surfaceprobe.GuestNetworkInspector; "
            "rm " + remote + "; rmdir " + directory
        )
    )
    (output / "android_network_inspect.generated.h").write_text(
        "/* Generated fixed source-built guest-only metadata payload. */\n"
        "static const char *const network_inspect_commands[] = {\n"
        + "\n".join(json.dumps(value) + "," for value in commands)
        + "\n};\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    subprocess.run(
        [
            str(compiler),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(ROOT / "tools/android_network_inspector.c"),
            "-o",
            str(output / "android-network-inspector"),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
