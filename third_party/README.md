# Dependency license inventory

`components.tsv` records the resolved runtime artifacts, including transitive AndroidX/Material dependencies and libxposed service/interface, plus the compile-only libxposed API 102. The build fails if this inventory no longer matches Gradle resolution. Full texts are packaged in the app license viewer.

- AndroidX / Material / libxposed / Kotlin and related runtime artifacts: Apache-2.0, exact Maven POM versions recorded in the inventory.
- Vendored `@xterm/xterm` 5.5.0: MIT; full upstream text in `xterm-LICENSE.txt` and pinned npm provenance in `xterm-provenance.json`.
- Own native console bridge and guest user-space init: project MIT license.

No Android kernel, crosvm binary, Debian image, stock Terminal APK, BusyBox binary, or downloaded OS asset is distributed. JUnit/build tools are not shipped in the APK.

The developer-only `tools/crosvm_preallocated_ram.patch` modifies the pinned
AOSP crosvm source under its [BSD license](crosvm-BSD-LICENSE.txt). The Rust
FD-adoption and LEND-chunk helper files are original MIT-licensed Bridge code.
No crosvm, captured host library or new guest-kernel binary is redistributed.
