#!/usr/bin/env python3
"""Build offscreen YUV diagnostics. Never starts a VM or modifies a device.

The translator mode requires the separately patched, pinned gfxstream build.
It exercises real native YUV images below the guest encoder boundary and does
not advertise GL_EXT_YUV_target to an unqualified guest.
"""

import argparse
import hashlib
import json
import platform
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = "b46f0d4b4545def09bdf30651d4bf848b2ba0c34"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def translator_flags(build, compiler):
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in entries if item["file"].endswith("/host/gl/color_buffer_gl.cpp"))
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    if Path(arguments[0]).resolve() != compiler.resolve():
        raise ValueError("Renderer and probe must use the same pinned NDK compiler")
    flags = []
    index = 1
    while index < len(arguments):
        value = arguments[index]
        index += 1
        if value in ("-o", "-MF", "-MT", "-MQ"):
            index += 1
            continue
        if value in ("-c", "-MD", "-MMD", "-MP") or value == entry["file"]:
            continue
        flags.append(value)
    if "-DPLB_GFXSTREAM_NATIVE_YUV_EXPERIMENT=1" not in flags:
        raise ValueError("Use the separate native YUV experimental renderer build")
    source = Path(entry["file"]).resolve().parents[2]
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != REVISION:
        raise ValueError("Unrecognized gfxstream source revision")
    return source, flags


def build(args):
    sdk = args.sdk.resolve()
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    toolchain = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
    compiler = toolchain / "bin/clang++"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    mode = "translator" if args.renderer_build else "host"
    executable = output / f"{mode}-yuv-probe"
    manifest = output / f"{mode}-yuv-probe.json"
    # A failed build must not leave an older executable looking current.
    executable.unlink(missing_ok=True)
    manifest.unlink(missing_ok=True)
    source = ROOT / "tools/egl_yuv_probe.cpp"
    command = [str(compiler)]
    metadata = {"mode": mode, "source_sha256": digest(source), "vulkan_enabled": False}
    if args.renderer_build:
        if not args.display_backend_dir:
            raise ValueError("Translator mode also requires --display-backend-dir")
        renderer = args.renderer_build.resolve()
        renderer_source, flags = translator_flags(renderer, compiler)
        command += flags
        command += ["-DPLB_YUV_TRANSLATOR=1", "-I", str(renderer_source / "host/vulkan")]
        command += ["-L", str(renderer), "-L", str(args.display_backend_dir.resolve())]
        command += ["-lgfxstream_backend", "-lplb_owned_display_backend", "-Wl,-rpath,$ORIGIN"]
        metadata["gfxstream_revision"] = REVISION
        metadata["renderer_sha256"] = digest(renderer / "libgfxstream_backend.so")
        metadata["overlay_sha256"] = digest(
            ROOT / "native/owned_display/source-overlays/gfxstream-native-yuv-experiment.patch"
        )
        metadata["guest_extension_advertised"] = False
    else:
        command += [
            "--target=aarch64-linux-android35",
            "--sysroot=" + str(toolchain / "sysroot"),
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
        ]
    command += ["-static-libstdc++", "-I", str(ROOT / "native"), "-I", str(ROOT / "tools")]
    command += [str(source), "-lEGL", "-lGLESv3", "-landroid", "-llog", "-ldl"]
    command += ["-Wl,--no-undefined", "-o", str(executable)]
    try:
        subprocess.run(command, cwd=ROOT, check=True)
        metadata["probe_sha256"] = digest(executable)
        manifest.write_text(json.dumps(metadata, indent=2) + "\n")
    except BaseException:
        executable.unlink(missing_ok=True)
        manifest.unlink(missing_ok=True)
        raise
    print("PLB_YUV_PROBE_BUILD_PASS mode=" + mode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renderer-build", type=Path)
    parser.add_argument("--display-backend-dir", type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
