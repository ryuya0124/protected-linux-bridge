FROM debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends gcc make bc bison flex libssl-dev libelf-dev xz-utils ca-certificates gnupg dirmngr curl python3 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
