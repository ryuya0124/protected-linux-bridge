FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

# Development container only. All mkfs/inspection operands are regular guest
# image files under the explicitly mounted work directory, never Android devices.
RUN apt-get update -qq \
    && apt-get install -y --no-install-recommends e2fsprogs zerofree erofs-utils \
    && rm -rf /var/lib/apt/lists/*
