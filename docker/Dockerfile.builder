# Logos build environment — Ubuntu 24.04 + the distribution's STOCK
# LLVM/MLIR 20 toolchain (no apt.llvm.org, no Jenny fork).
#
# This image carries only the build dependencies; the Logos source tree is
# bind-mounted at build time so the heavy apt layer stays cached across
# iterations:
#
#   docker build -t logos-build-env -f docker/Dockerfile.builder docker
#   docker run --rm -v "$PWD":/src -w /src logos-build-env \
#       cmake -S . -B build-stock -G Ninja \
#             -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20
#
# A self-contained image that builds + installs logosc from a clean checkout
# is a separate Dockerfile (TODO: docker/Dockerfile).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        # stock LLVM/MLIR 20 — straight from the Ubuntu 24.04 repos
        clang-20 lld-20 llvm-20-dev libmlir-20-dev mlir-20-tools \
        # C++23 standard library: gcc-14's libstdc++ carries <print> etc.
        # (Ubuntu 24.04 default gcc-13 does not). clang-20 auto-selects the
        # newest installed GCC toolchain, so this just needs to be present.
        # libstdc++ — NOT libc++ — because the stock LLVM/MLIR/sqlite archives
        # are built against libstdc++ and the ABI must match.
        g++-14 \
        # build system
        cmake ninja-build pkg-config \
        # third-party libraries Logos links against
        libsqlite3-dev liburing-dev zlib1g-dev libzstd-dev \
        # misc
        ca-certificates git \
    && rm -rf /var/lib/apt/lists/*

# Make the unsuffixed names resolve to the stock-20 toolchain.
RUN update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-20   100 \
 && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100 \
 && update-alternatives --install /usr/bin/ld.lld  ld.lld  /usr/bin/ld.lld-20  100
