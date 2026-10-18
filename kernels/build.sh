#!/bin/sh
# Build only a VM guest kernel. Run in the documented Linux builder container.
set -eu
export LC_ALL=C KBUILD_BUILD_USER=plb KBUILD_BUILD_HOST=guest-builder
export KBUILD_BUILD_TIMESTAMP='Thu Oct 15 00:00:00 UTC 2026' KBUILD_BUILD_VERSION=1
PROBE_CACHE=${1:?Usage: build.sh CACHE_DIRECTORY}
PLB_CONFIG_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$PROBE_CACHE"
printf '%s\n' '5bd9acdc82ec5efc3790ae840e5aaa40dfde7efff261cf6dde902dc570acf98c  linux-6.18.57.tar.xz' | sha256sum -c -
printf '%s\n' '450d4be5d747b77dbf16a3d6640dc449a6975da6912f17c24afb18f75fada77f  microdroid_defconfig' | sha256sum -c -
# Developer signature, checked with pinned official release-key fingerprints.
export GNUPGHOME="$PROBE_CACHE/gnupg"
mkdir -p "$GNUPGHOME"
chmod 700 "$GNUPGHOME"
gpg --batch --auto-key-locate clear,wkd --locate-keys gregkh@kernel.org sashal@kernel.org
xz -cd linux-6.18.57.tar.xz | gpg --batch --status-fd 3 --verify linux-6.18.57.tar.sign - 3>signature-status.txt
python3 - <<'PY'
from pathlib import Path
accepted={'647F28654894E3BD457199BE38DBBDC86092693E','E27E5D8A3403A2EF66873BBCDEA66FF797772CDC'}
lines=Path('signature-status.txt').read_text().splitlines()
valid=[line.split()[2:] for line in lines if line.startswith('[GNUPG:] VALIDSIG ')]
assert len(valid)==1 and (valid[0][0] in accepted or valid[0][-1] in accepted),'Unexpected signing key'
PY
if [ ! -d linux-6.18.57 ]; then tar -xf linux-6.18.57.tar.xz; fi
mkdir -p output
cd linux-6.18.57
# Never run a kernel install target; all outputs stay in this build directory.
cp ../microdroid_defconfig ../output/.config
make ARCH=arm64 O=../output olddefconfig
scripts/kconfig/merge_config.sh -m -O ../output ../output/.config "$PLB_CONFIG_DIR/plb.config"
make ARCH=arm64 O=../output olddefconfig
make ARCH=arm64 O=../output -j4 Image
cp ../output/arch/arm64/boot/Image ../Image-6.18.57-plb1
sha256sum ../Image-6.18.57-plb1
