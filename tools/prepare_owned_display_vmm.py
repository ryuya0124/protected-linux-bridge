#!/usr/bin/env python3
"""Emit a pinned source overlay for a private Android 17 VMM; never build/run it.

Input checkout is read-only. Apply the overlay only to a disposable AOSP build
tree. This switches the source tree's gpu_display link dependency, so the tree
must never be used to produce/install a host APEX or Android system image.
"""

from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]


def transform(files):
    blueprint = files["Android.bp"]
    start = blueprint.index("rust_binary {")
    end = blueprint.index("\nrust_test {", start)
    module = blueprint[start:end]
    if module.count('    name: "crosvm",\n') != 1:
        raise ValueError("Unexpected VMM build module")
    module = module.replace("rust_binary {", "rust_ffi_shared {", 1)
    module = module.replace('    name: "crosvm",\n', '    name: "libplb_vmm_android17",\n', 1)
    module = module.replace("    prefer_rlib: true,\n", "")
    module = module.replace(
        'crate_name: "crosvm",', 'crate_name: "crosvm",\n    stem: "libplb_vmm_android17",', 1
    )
    # Keep the original binary definition for source provenance, but build only
    # libplb_vmm_android17. No device installation step is emitted.
    blueprint += "\n// Bridge-owned library; not a replacement system crosvm.\n" + module
    display = files["gpu_display/Android.bp"]
    old = '                "libcrosvm_android_display_client",\n'
    if display.count(old) != 1:
        raise ValueError("Unexpected display link graph")
    display = display.replace(old, "")
    anchor = '                "libbinder_ndk",\n'
    if display.count(anchor) != 1:
        raise ValueError("Unexpected display shared libraries")
    display = display.replace(anchor, anchor + '                "libplb_owned_display_backend",\n')
    main = files["src/main.rs"]
    if main.count("fn main() {") != 1 or "plb_crosvm_" in main:
        raise ValueError("Unexpected VMM entry or an older Bridge patch")
    main = main.replace("fn main() {", "#[allow(dead_code)]\nfn main() {", 1)
    main += "\n" + (ROOT / "native/owned_display/entry.rs").read_text()
    return {"Android.bp": blueprint, "src/main.rs": main, "gpu_display/Android.bp": display}


def prepare(checkout, output, backend):
    output = output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()) or output.exists():
        raise ValueError("Choose a new output below this project's artifacts directory")
    pins = json.loads((ROOT / "compatibility/android17-owned-display-inputs.json").read_text())
    files = {}
    for name, expected in pins["files"].items():
        data = (checkout / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Pinned source mismatch: " + name)
        files[name] = data.decode()
    binary = backend.read_bytes()
    if binary[:5] != b"\x7fELF\x02" or len(binary) > 8 * 1024 * 1024:
        raise ValueError("Expected the separately built bounded 64-bit display backend")
    changed = transform(files)
    for name, content in changed.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    library = output / "plb_owned_display"
    library.mkdir()
    (library / "Android.bp").write_bytes(
        (ROOT / "native/owned_display/prebuilt.Android.bp").read_bytes()
    )
    (library / backend.name).write_bytes(binary)
    manifest = {
        "upstream": pins,
        "backend_sha256": hashlib.sha256(binary).hexdigest(),
        "target": "libplb_vmm_android17",
        "full_vmm_built": False,
        "device_tested": False,
        "host_installation_allowed": False,
        "files": {
            name: hashlib.sha256(content.encode()).hexdigest() for name, content in changed.items()
        },
    }
    (output / "owned-display-overlay.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--checkout", type=Path, required=True, help="Pinned external/crosvm source root"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.checkout, args.output, args.backend)
