# Build a VM guest kernel only. The Android device is never mounted here.
FROM debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-19 lld-19 llvm-19 gcc make bc bison flex libssl-dev libelf-dev \
    dwarves python3 rsync xz-utils cpio patch ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
