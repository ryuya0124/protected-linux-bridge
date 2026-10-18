#!/usr/bin/env python3
"""Pinned, static ARM64 Linux guest tun2socks. Never creates an Android TUN."""

from pathlib import Path
import hashlib, json, os, platform, shutil, subprocess, tarfile, urllib.request


def build(root: Path, sdk: Path) -> Path:
    manifest = (root / "third_party/hev/sources.json").read_bytes()
    cache = root / "app/build/hev"
    cache.mkdir(parents=True, exist_ok=True)
    key = hashlib.sha256(manifest + Path(__file__).read_bytes()).hexdigest()
    source = cache / "source"
    marker = cache / "sources.identity"
    if not marker.exists() or marker.read_text() != key:
        if source.exists():
            shutil.rmtree(source)
        source.mkdir()
        for row in json.loads(manifest):
            archive = cache / f"{row['project']}-{row['revision']}.tar.gz"
            if not archive.exists():
                with urllib.request.urlopen(row["url"], timeout=45) as response:
                    data = response.read(8 * 1024 * 1024 + 1)
                if len(data) > 8 * 1024 * 1024:
                    raise RuntimeError("TUN source exceeds bound")
                archive.write_bytes(data)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != row["sha256"]:
                raise RuntimeError("TUN source checksum mismatch: " + row["project"])
            target = source / row["destination"]
            target.mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive) as tar:
                for member in tar.getmembers():
                    parts = Path(member.name).parts[1:]
                    if not parts:
                        continue
                    member.name = str(Path(*parts))
                    if not (target / member.name).resolve().is_relative_to(target.resolve()):
                        raise RuntimeError("Unsafe TUN archive path")
                    tar.extract(member, target, filter="data")
        (source / ".rev-id").write_text(json.loads(manifest)[0]["revision"] + "\n")
        marker.write_text(key)
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    tc = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
    sysroot = root / "app/build/guest/sysroot"
    # Bionic's exported Linux UAPI contains two libc-specific includes. Supply
    # compile-only shims; all libc types/functions still come from guest musl.
    uapi = cache / "uapi"
    if not uapi.exists():
        uapi.mkdir()
        for name in ("linux", "asm-generic"):
            shutil.copytree(tc / "sysroot/usr/include" / name, uapi / name)
        shutil.copytree(tc / "sysroot/usr/include/aarch64-linux-android/asm", uapi / "asm")
        (uapi / "sys").mkdir()
        (uapi / "sys/cdefs.h").write_text("/* C-only musl build. */\n")
        (uapi / "bits").mkdir()
        (uapi / "bits/sockaddr_storage.h").write_text("#include <sys/socket.h>\n")
    builtins = next((tc / "lib/clang").glob("*/lib/linux/libclang_rt.builtins-aarch64-android.a"))
    base = [
        str(tc / "bin/clang"),
        "--target=aarch64-linux-musl",
        "--ld-path=" + str(tc / "bin/ld.lld"),
        "-Wno-unused-command-line-argument",
        "-I" + str(uapi),
        "-isystem",
        str(sysroot / "include"),
    ]
    pre = ["-static", "-nostdlib", str(sysroot / "lib/crt1.o"), str(sysroot / "lib/crti.o")]
    post = ["-L" + str(sysroot / "lib"), "-lc", str(builtins), str(sysroot / "lib/crtn.o")]
    wrapper = cache / "musl-cc"
    wrapper.write_text(
        '#!/usr/bin/env python3\nimport sys,subprocess\na=sys.argv[1:]\nlink=not any(x in a for x in ["-c","-E","-S","-MM","-M"])\nbase='
        + repr(base)
        + "\npre="
        + repr(pre)
        + " if link else []\npost="
        + repr(post)
        + " if link else []\nsys.exit(subprocess.call(base+pre+a+post))\n"
    )
    wrapper.chmod(0o755)
    args = [
        "make",
        "-j4",
        "CC=" + str(wrapper),
        "AR=" + str(tc / "bin/llvm-ar"),
        "STRIP=" + str(tc / "bin/llvm-strip"),
    ]
    with (cache / "build.log").open("w") as log:
        # Upstream's shell loop doesn't propagate a failed sub-build. Check each.
        for part in ("third-part/yaml", "third-part/lwip", "third-part/hev-task-system"):
            subprocess.run(
                args + ["ENABLE_MEMALLOC_SLICE=0", "ENABLE_STACK_OVERFLOW_DETECTOR=1", "static"],
                cwd=source / part,
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
            )
        subprocess.run(
            args + ["exec"], cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True
        )
    return source / "bin/hev-socks5-tunnel"
