#!/usr/bin/env python3
"""Embed one signed, size-bounded, fixed-package diagnostic APK; no device I/O.

Only the selected Cuttlefish guest can receive the generated commands. There is
no caller-supplied remote path, arbitrary executable or general upload API.
"""

import base64
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

PACKAGE = "net.ryuya_dev.protectedlinux.gpuprobe"
CERTIFICATE = "548519a543d68d53e1db07743c8ddedff498ad38c7df2bd4bee51085b161948d"
MAX_APK = 64 * 1024
DIRECTORY = "/data/local/tmp/plb-gpu-probe-@NONCE@"
APK = DIRECTORY + "/probe.apk"
IDENTITY = (
    'set -eu; [ "$(id -u)" = 0 ]; '
    '[ "$(getprop ro.product.device)" = vsoc_arm64_only ]; '
    '[ "$(getprop ro.hardware)" = cutf_cvm ]; '
    '[ "$(getprop ro.build.version.incremental)" = 16373615 ]; '
)


def command(script):
    return "exec:sh -c " + shlex.quote(IDENTITY + script)


def commands(data):
    if not 1024 <= len(data) <= MAX_APK or not data.startswith(b"PK\x03\x04"):
        raise ValueError("diagnostic APK envelope/size")
    encoded = base64.b64encode(data).decode("ascii")
    digest = hashlib.sha256(data).hexdigest()
    begin = command(f"umask 077; mkdir {DIRECTORY}; : > {APK}; echo PLB_GPU_APK_BEGIN_OK")
    chunks = [
        command(
            f"printf %s '{encoded[at:at + 4096]}' | base64 -d >> {APK}; echo PLB_GPU_APK_CHUNK_OK"
        )
        for at in range(0, len(encoded), 4096)
    ]
    finish = command(
        f'[ "$(wc -c < {APK})" = {len(data)} ]; '
        f'[ "$(sha256sum {APK} | cut -d" " -f1)" = {digest} ]; '
        f"pm install --user 0 -r {APK} > {DIRECTORY}/install.log; "
        f"grep -x Success {DIRECTORY}/install.log >/dev/null; "
        f"rm {APK} {DIRECTORY}/install.log; rmdir {DIRECTORY}; "
        "cmd power wakeup; wm dismiss-keyguard; "
        f"am start -W -n {PACKAGE}/.ProbeActivity --es plb_nonce @NONCE@; "
        "echo PLB_GPU_APK_STARTED"
    )
    cleanup = command(f"rm -f {APK} {DIRECTORY}/install.log; rmdir {DIRECTORY}")
    collect = command(f"logcat -d -s PLBGpuProbe:I '*:S' | grep @NONCE@ | tail -2")
    poweroff = command("sync; /system/bin/reboot -p")
    return {
        "poweroff": poweroff,
        "begin": begin,
        "chunks": chunks,
        "finish": finish,
        "cleanup": cleanup,
        "collect": collect,
    }


def build(apk, sdk, destination):
    apk = Path(apk)
    if not 1024 <= apk.stat().st_size <= MAX_APK:
        raise ValueError("diagnostic APK exceeds 64 KiB; build the minimized release variant")
    build_tools = Path(sdk) / "build-tools/36.0.0"
    signature = subprocess.check_output(
        [str(build_tools / "apksigner"), "verify", "--print-certs", str(apk)], text=True
    )
    if "Signer #1 certificate SHA-256 digest: " + CERTIFICATE not in signature:
        raise ValueError("diagnostic APK signing certificate")
    badging = subprocess.check_output(
        [str(build_tools / "aapt"), "dump", "badging", str(apk)], text=True
    )
    if not badging.startswith(f"package: name='{PACKAGE}' "):
        raise ValueError("diagnostic APK package")
    if "application-debuggable" in badging or "uses-permission:" in badging:
        raise ValueError("diagnostic APK must be non-debuggable and request no permissions")
    data = apk.read_bytes()
    payload = commands(data)
    header = ["/* Generated fixed guest-only APK payload. Never a host upload API. */"]
    for name in ("begin", "finish", "cleanup", "collect", "poweroff"):
        header += [f"static const char gpu_probe_{name}[] =", json.dumps(payload[name]) + ";"]
    header += ["static const char *const gpu_probe_chunks[] = {"]
    header += [json.dumps(chunk) + "," for chunk in payload["chunks"]]
    header += ["};"]
    Path(destination).write_text("\n".join(header) + "\n")
    return hashlib.sha256(data).hexdigest()
