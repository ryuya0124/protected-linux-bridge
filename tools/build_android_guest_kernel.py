#!/usr/bin/env python3
"""Build the separate Android guest DMA-sync kernel; never install or start it."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts"
COMMIT = "3ec022196c4e9d5c1434599cdda63f622dd6f586"
ARCHIVE_SHA = "13ab707b031ace652b6d7d111dfe4f877e186e1bc0c80925ea19c34dc0c3e531"
CONFIG_SHA = "4c065ae7247d878dc34438e61230fb4c3e4a9ce286cd585bf49db03dd7e69726"
FIX = "598c1c3e895590f845e04455d5580ea28ffde666"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def private_output(path):
    path = path.resolve()
    if not path.is_relative_to(ARTIFACTS.resolve()) or path == ARTIFACTS.resolve():
        raise ValueError("Use a dedicated ignored artifacts/ output")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--base-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--builder-image", default="plb-android-gpu-kernel:clang19")
    parser.add_argument(
        "--resume",
        action="store_true",
        help="Verify/reuse only this output's owned failed Linux workspace",
    )
    args = parser.parse_args()
    output = private_output(args.output)
    recipe = ROOT / "kernels/android-gpu"
    if shutil.disk_usage(ROOT).free < 2 * 1024**3:
        parser.error("Retain 2 GiB emergency space; clean owned intermediates after the build")
    output.mkdir(parents=True, mode=0o700, exist_ok=True)
    if digest(args.source_archive) != ARCHIVE_SHA or digest(args.base_config) != CONFIG_SHA:
        parser.error("Use the pinned Android common archive and original guest config")
    if (output / "build.json").exists():
        parser.error("Refuse replacing a completed guest kernel build")
    image = subprocess.check_output(
        ["docker", "image", "inspect", args.builder_image, "--format", "{{.Id}}"], text=True
    ).strip()
    if not image.startswith("sha256:"):
        raise ValueError("Expected a resolved builder image identity")
    name = "plb-guest-dma-sync-build-" + uuid.uuid4().hex[:12]
    volume = name + "-work"
    owner = name
    if args.resume:
        previous = json.loads((output / "builder-instance.json").read_text())
        owner = previous.get("volume_owner", previous["container"])
        volume = previous["volume"]
        if (
            not re.fullmatch(r"plb-guest-dma-sync-build-[0-9a-f]{12}", owner)
            or volume != owner + "-work"
            or previous["image"] != image
        ):
            parser.error("Resumed workspace identity mismatch")
        info = json.loads(
            subprocess.check_output(["docker", "inspect", previous["container"]], text=True)
        )[0]
        labels = json.loads(
            subprocess.check_output(["docker", "volume", "inspect", volume], text=True)
        )[0].get("Labels", {})
        if (
            info["State"]["Status"] != "exited"
            or info["Config"]["Image"] != image
            or labels.get("plb.guest-build") != owner
            or not any(
                m.get("Name") == volume and m.get("Destination") == "/work" for m in info["Mounts"]
            )
        ):
            parser.error("Only this output's stopped, labeled builder may resume")
        subprocess.run(["docker", "rm", previous["container"]], check=True)
    else:
        subprocess.run(
            ["docker", "volume", "create", "--label", "plb.guest-build=" + owner, volume],
            check=True,
        )
    (output / "builder-instance.json").write_text(
        json.dumps(
            {"container": name, "volume": volume, "volume_owner": owner, "image": image}, indent=2
        )
        + "\n"
    )
    command = [
        "docker",
        "run",
        "--name",
        name,
        "--network",
        "none",
        "--read-only",
        "--cpus",
        "4",
        "--memory",
        "4g",
        "-e",
        "PLB_RESUME_BUILD=" + ("1" if args.resume else "0"),
        "--pids-limit",
        "1024",
        "--tmpfs",
        "/tmp:size=256m",
        "-v",
        volume + ":/work",
        "-v",
        str(args.source_archive.resolve()) + ":/inputs/archive.tar.gz:ro",
        "-v",
        str(args.base_config.resolve()) + ":/inputs/base.config:ro",
        "-v",
        str(recipe) + ":/recipe:ro",
        image,
        "sh",
        "-c",
        "python3 /recipe/prepare.py && exec sh /recipe/build.sh",
    ]
    log_path = output / "build.log"
    if log_path.exists():
        log_path.rename(output / ("build.previous-" + uuid.uuid4().hex[:12] + ".log"))
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            while process.poll() is None:
                if (
                    shutil.disk_usage(output).free < 2 * 1024**3
                    or log_path.stat().st_size > 32 * 1024**2
                ):
                    raise RuntimeError("Guest kernel build exceeded disk/log budget")
                time.sleep(1)
        finally:
            if process.poll() is None:
                # Only this invocation's fresh Mac build container, never Docker
                # itself, the Android host or an unrelated VM/container.
                subprocess.run(["docker", "stop", "--time", "5", name], check=False)
                process.wait(timeout=15)
    if process.returncode:
        raise RuntimeError(f"Guest kernel build failed ({process.returncode}); inspect {log_path}")
    # Export only completed outputs; source/object names stay on Linux.
    for relative in (
        "Image-android17-dma-sync",
        "kernel-release.txt",
        "output/.config",
        "output/System.map",
    ):
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["docker", "cp", name + ":/work/" + relative, str(target)], check=True)
    kernel = output / "Image-android17-dma-sync"
    if not 8 * 1024**2 <= kernel.stat().st_size <= 128 * 1024**2:
        raise ValueError("Unexpected guest Image size")
    record = {
        "guest_only": True,
        "source_commit": COMMIT,
        "source_archive_sha256": ARCHIVE_SHA,
        "base_config_sha256": CONFIG_SHA,
        "upstream_fix": FIX,
        "docker_image_id": image,
        "kernel_sha256": digest(kernel),
        "kernel_release": (output / "kernel-release.txt").read_text().strip(),
        "config_sha256": digest(output / "output/.config"),
        "system_map_sha256": digest(output / "output/System.map"),
        "case_sensitive_linux_build": True,
        "resumed_verified_source": args.resume,
        "patch_sha256": digest(recipe / "guest-bound-dma-sync.patch"),
        "recipe_sha256": {p.name: digest(p) for p in recipe.iterdir() if p.is_file()},
        "android_host_modified": False,
        "vm_started_by_build": False,
    }
    (output / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    # Only the invocation-owned stopped container/volume are eligible. No prune.
    subprocess.run(["docker", "rm", name], check=True)
    subprocess.run(["docker", "volume", "rm", volume], check=True)
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
