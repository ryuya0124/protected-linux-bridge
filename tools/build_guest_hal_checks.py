#!/usr/bin/env python3
"""Build fixed guest-only HAL checks. Does not connect to or start a VM/host UI."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/owned_display_probe"
CHECKS = {
    "sensor": ("GuestSensorReadback", "PLB_GUEST_SENSOR_EVENTS_PASS"),
    "audio": ("GuestAudioReadback", "PLB_GUEST_AUDIO_HAL_PASS"),
    "camera": ("GuestCameraReadback", "PLB_GUEST_CAMERA_HAL_PASS"),
}


def run(arguments):
    subprocess.run([str(value) for value in arguments], check=True)


def build(kind, sdk, java, output):
    class_name, pass_marker = CHECKS[kind]
    classes = output / f"guest-{kind}-classes"
    dex = output / f"guest-{kind}-dex"
    classes.mkdir(parents=True, exist_ok=True)
    dex.mkdir(exist_ok=True)
    android = sdk / "platforms/android-37.0/android.jar"
    tools = sdk / "build-tools/36.1.0"
    run(
        [
            java / "javac",
            "--release",
            "17",
            "-cp",
            android,
            "-d",
            classes,
            SOURCE / f"{class_name}.java",
        ]
    )
    run(
        [
            tools / "d8",
            "--release",
            "--min-api",
            "36",
            "--lib",
            android,
            "--output",
            dex,
            *sorted(classes.rglob("*.class")),
        ]
    )
    jar = output / f"guest-{kind}.jar"
    with zipfile.ZipFile(jar, "w") as archive:
        entry = zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0))
        archive.writestr(entry, (dex / "classes.dex").read_bytes())
    digest = hashlib.sha256(jar.read_bytes()).hexdigest()
    payload = base64.b64encode(jar.read_bytes()).decode("ascii")
    destination = f"/data/local/tmp/plb-owned-{kind}-readback.jar"
    # All payloads/paths/classes are built-in. Identity is checked by the native
    # helper before sending this service to the pinned guest, never the host.
    command = (
        "umask 077; printf %s "
        + shlex.quote(payload)
        + " | base64 -d > "
        + destination
        + ' && test "$(sha256sum '
        + destination
        + " | cut -d ' ' -f 1)\" = "
        + digest
        + " && CLASSPATH="
        + destination
        + " app_process -Xmx32m /system/bin "
        + "net.ryuya_dev.protectedlinux.guestprobe."
        + class_name
        + "; result=$?; echo PLB_GUEST_CHECK_PROCESS_EXIT=$result; "
        + "if test $result -ne 0; then logcat -d -t 150 -v brief | "
        + "grep -E 'lmkd|lowmemory|killed|Killed|Audio|audio|pcm|ALSA|FATAL|fatal'; fi; "
        + "rm -f "
        + destination
        + '; exit "$result"'
    )
    if kind == "camera":
        # Lazy guest service, entirely synthetic. Claim only a newly registered
        # test camera; never disable another caller's pre-existing test camera.
        camera_setup = (
            # Android's cmd uses a non-waiting lookup here. Start only the
            # guest's already installed service; this is not a host init write.
            "echo PLB_GUEST_CAMERA_SERVICE_START_REQUEST; "
            "setprop ctl.start virtual_camera; attempt=0; "
            "while test $attempt -lt 30; do "
            "service check virtual_camera | grep -q ': found' && break; "
            "attempt=$((attempt + 1)); sleep 0.1; done; "
            # The guest shell can inherit a PTY that virtual_camera may not
            # receive under SELinux. Supply null stdin and pipe stdout/stderr;
            # keep both host and guest policy/enforcement unchanged.
            "help=$(cmd virtual_camera help </dev/null 2>&1); printf '%s\\n' \"$help\"; "
            "enabled=$(cmd virtual_camera enable_test_camera --camera_id=9987 "
            "--input_fps=10 </dev/null 2>&1); "
            'printf \'%s\\n\' "$enabled"; case "$enabled" in '
            "*'Successfully registered test camera'*) ;; *) "
            "echo PLB_GUEST_CAMERA_SERVICE_STATE=$(getprop init.svc.virtual_camera); "
            "pidof virtual_camera; logcat -d -b crash -t 100 -v brief; "
            "logcat -d -t 250 -v brief | "
            "grep -E 'VirtualCamera|virtual_camera|EGL|egl|Camera|FATAL|fatal|avc: denied'; "
            "exit 31;; esac; "
            "trap 'cleanup=$(cmd virtual_camera disable_test_camera </dev/null 2>&1); "
            'cleanup_status=$?; printf "%s\\n" "$cleanup"; '
            "echo PLB_GUEST_CAMERA_REVOKED=$cleanup_status' EXIT; "
        )
        command = camera_setup + command
    header_name = f"guest_{kind}_check.generated.h"
    (output / header_name).write_text(
        "static const char plb_guest_check_service[] = "
        + json.dumps("exec:sh -c " + shlex.quote(command))
        + ";\n"
        + "static const char plb_guest_check_pass[] = "
        + json.dumps(pass_marker)
        + ";\n"
    )
    compiler = sdk / (
        "ndk/28.2.13676358/toolchains/llvm/prebuilt/darwin-x86_64/bin/"
        "aarch64-linux-android35-clang"
    )
    run(
        [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            output,
            "-I",
            ROOT / "native",
            '-DPLB_GUEST_CHECK_HEADER="' + header_name + '"',
            SOURCE / "guest_hal_check.c",
            "-o",
            output / f"guest-{kind}-check",
        ]
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--java-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    for kind in CHECKS:
        build(
            kind, arguments.sdk.resolve(), arguments.java_bin.resolve(), arguments.output.resolve()
        )
    print("GUEST_HAL_CHECKS_BUILD_PASS")


if __name__ == "__main__":
    main()
