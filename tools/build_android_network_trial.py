#!/usr/bin/env python3
"""Embed only the fixed source-built Android guest network experiment.

No caller-supplied payload or guest command is accepted. Each ADB transaction
checks the exact guest identity. Guest files are content-addressed and verified
before execution. This developer tool performs no device I/O itself.
"""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import platform
import shlex
import subprocess

from build_guest_gpu_payload import IDENTITY

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    args = parser.parse_args()
    output = ROOT / "artifacts/android4g-gpu/network-agent"
    source = ROOT / "tools/android_guest_network"
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(compiler),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source / "loopback_relay.c"),
            "-o",
            str(output / "loopback-relay"),
        ],
        check=True,
    )
    payloads = {
        "agent.jar": (output / "agent.jar").read_bytes(),
        "loopback-relay": (output / "loopback-relay").read_bytes(),
        "hev-socks5-tunnel": (ROOT / "app/build/hev/source/bin/hev-socks5-tunnel").read_bytes(),
        "tunnel.yml": (source / "tunnel.yml").read_bytes(),
    }
    if any(not data or len(data) > 512 * 1024 for data in payloads.values()):
        raise ValueError("Guest network payload size")
    identity = hashlib.sha256(
        b"".join(payloads.values()) + (source / "start.sh").read_bytes()
    ).hexdigest()
    directory = "/data/local/tmp/plb-network-" + identity[:20]
    payloads["start.sh"] = (
        (source / "start.sh").read_text().replace("@DIRECTORY@", directory).encode()
    )

    def command(script):
        return "exec:sh -c " + shlex.quote(
            IDENTITY + "umask 077; " + script + "; echo PLB_NETWORK_INSPECT_STEP_OK"
        )

    commands = [
        command(
            "! ip link show plb0 >/dev/null 2>&1; "
            "if test -e " + directory + "; then "
            "test ! -L " + directory + "; test -d " + directory + "; "
            'test "$(stat -c %u:%a ' + directory + ')" = 0:700; '
            "else mkdir " + directory + "; fi"
        )
    ]
    for name, data in payloads.items():
        path = directory + "/" + name
        commands.append(command("test ! -L " + path + "; : > " + path))
        encoded = base64.b64encode(data).decode("ascii")
        for at in range(0, len(encoded), 4096):
            commands.append(
                command("printf %s '" + encoded[at : at + 4096] + "' | base64 -d >> " + path)
            )
        commands.append(
            command(
                '[ "$(sha256sum '
                + path
                + ' | cut -d" " -f1)" = '
                + hashlib.sha256(data).hexdigest()
                + " ]; chmod 700 "
                + path
            )
        )
    launch = command(
        "setsid nohup sh "
        + directory
        + "/start.sh </dev/null >"
        + directory
        + "/supervisor.log 2>&1 & supervisor=$!; sleep 2; "
        "kill -0 $supervisor; test -s " + directory + "/ready; "
        "test -s " + directory + "/agent.log; "
        "grep -q PLB_GUEST_NETWORK_REGISTERED " + directory + "/agent.log; "
        "echo PLB_GUEST_NETWORK_SUPERVISOR:$supervisor"
    )
    commands.append(launch)
    (output / "android_network_inspect.generated.h").write_text(
        "/* Generated fixed source-built guest-only network trial. */\n"
        "static const char *const network_inspect_commands[] = {\n"
        + "\n".join(json.dumps(value) + "," for value in commands)
        + "\n};\n"
        + "static const char network_trial_stop[] = "
        + json.dumps(
            command(
                "if test -s " + directory + "/supervisor.pid; then "
                "read supervisor born guest_boot <" + directory + "/supervisor.pid; "
                'test "$guest_boot" = "$(cat /proc/sys/kernel/random/boot_id)"; '
                "case $supervisor:$born in *[!0-9:]*|:*) exit 40;; esac; "
                "now=\"$(awk '{print $22}' /proc/$supervisor/stat 2>/dev/null || true)\"; "
                'if test -n "$born" && test "$now" = "$born"; then kill -TERM $supervisor; fi; '
                "fi"
            )
        )
        + ";\n"
        + "static const char network_trial_verify[] = "
        + json.dumps(
            command(
                "cmd package resolve-activity --brief -a android.intent.action.VIEW -d https://example.com; "
                "am start -W -a android.intent.action.VIEW -d https://example.com; "
                "echo PLB_GUEST_BROWSER_LAUNCH_OK"
            )
        )
        + ";\n"
        + "static const char network_trial_launch[] = "
        + json.dumps(launch)
        + ";\n"
        + "static const char network_trial_state[] = "
        + json.dumps(
            command(
                "ip addr show plb0 || true; ip route show table main default; "
                "ls -l " + directory + "; cat " + directory + "/start.sh; "
                "dumpsys connectivity | head -22; "
                "for name in supervisor loopback tunnel agent; do echo PLB_LOG:$name; "
                "cat " + directory + "/$name.log || true; done; "
                "command -v curl || true; command -v wget || true"
            )
        )
        + ";\n"
    )
    (output / "trial.json").write_text(
        json.dumps({"directory": directory, "steps": len(commands)}, indent=2) + "\n"
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
            str(source / "trial_runner.c"),
            "-o",
            str(output / "android-network-trial"),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
