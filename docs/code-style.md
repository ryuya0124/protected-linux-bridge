# Source readability

First-party source is kept expanded and consistently formatted. Avoid several statements on one line, compressed control flow, or hand-minified UI/build scripts. Use concrete names, separate responsibilities into functions, and explain invariants at guest/host boundaries and disk promotion/recovery. Formatting is a baseline, not a substitute for understandable logic.

- C/C++: LLVM-based style in `.clang-format`, four spaces, 100 columns, expanded control flow. Current tool: clang-format 23.1.3.
- Java: google-java-format 1.33.0, JDK21.
- Gradle Kotlin: ktfmt 0.61, Google style.
- Python: Black 26.10.1, 100 columns (`pyproject.toml`).
- Shell: shfmt 3.12.0, four spaces. Keep heredocs intact.
- Own HTML/CSS/JS: Prettier 3.6.2.
- Android XML: two spaces; preserve string contents and namespace names.
- C used by Python tests lives in `tests/fixtures/`, so it can be reviewed and formatted as C. Test-only base paths use an explicit substituted placeholder.

Third-party source, bundled minified xterm, dependency licenses, generated Gradle wrapper and downloaded/build artifacts retain their upstream form. Never format source caches inside `artifacts/` or `app/build/` as part of this workflow.

## Reproduce

Install the tools above. Keep the two Java formatter jars in ignored `artifacts/format-tools/`:

- `google-java-format.jar`: [Maven Central 1.33.0 all-deps](https://repo.maven.apache.org/maven2/com/google/googlejavaformat/google-java-format/1.33.0/)
- `ktfmt.jar`: [Maven Central 0.61 with-dependencies](https://repo.maven.apache.org/maven2/com/facebook/ktfmt/0.61/)

Verify the published checksums when downloading. `shfmt` and `node_modules/.bin/prettier` default to that same cache; alternate paths can be passed as flags. Then:

```sh
python3 tools/format_sources.py --java /path/to/jdk21/bin/java \
    --clang-format /path/to/clang-format --black /path/to/black
python3 -m unittest discover -s tests -v
./gradlew :app:testDebugUnitTest :app:lintRelease :app:assembleRelease
```

Do not commit formatter binaries, local paths, SDK caches or signing files. Run tests/build after formatting: changes to shell quoting, raw protocols, initramfs generation or Android resources still need verification.

The terminal page is also kept as readable `assets/web/terminal.html`, with explicit xterm insertion markers. It is no longer a hand-minified Java string. The separately bundled upstream xterm files remain unmodified.

The owned display Rust entry uses `rustfmt --edition 2021`. Android Binder
headers under `third_party/android-binder/` retain their unmodified upstream form.
