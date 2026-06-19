# Self-contained build + package + runtime image for the Logos toolchain.
#
# From a clean checkout, this builds logosc + the stdlib with the distribution's
# STOCK LLVM/MLIR 20 (no Jenny fork, no apt.llvm.org), packages a .deb, and
# produces a minimal runtime image with logosc installed and ready to compile
# Logos programs. Build context = repo root:
#
#   docker build -f docker/Dockerfile -t logos .
#   docker run --rm logos --version
#
# To extract just the .deb:
#   docker build -f docker/Dockerfile --target deb -o type=local,dest=dist .

# ── Stage 1: build logosc + stdlib, package as .deb ──────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang-20 lld-20 llvm-20-dev libmlir-20-dev mlir-20-tools \
        g++-14 \
        cmake ninja-build pkg-config dpkg-dev \
        liburing-dev zlib1g-dev libzstd-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-20   100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    && cmake --build build -j"$(nproc)" \
    && cd build && cpack -G DEB

# ── Stage 2: the .deb as a build artifact ────────────────────────────────────
FROM scratch AS deb
COPY --from=builder /src/build/logos_*.deb /

# ── Stage 3: minimal runtime with logosc installed from the .deb ─────────────
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
COPY --from=builder /src/build/logos_*.deb /tmp/
# `gcc` + liburing-dev give the final-link step (cc + -luring) a Logos program
# needs; the .deb's own Depends pull libllvm/libmlir/libstdc++/…
RUN apt-get update && apt-get install -y --no-install-recommends \
        /tmp/logos_*.deb gcc liburing-dev \
    && rm -rf /var/lib/apt/lists/* /tmp/logos_*.deb
# In-image smoke: compile, link and run a trivial program.
RUN printf 'package smoke;\nfn main() -> i32 { return 0; }\n' > /tmp/s.logos \
    && logosc /tmp/s.logos -o /tmp/s.o \
    && cc /tmp/s.o /usr/lib/logos/lib*.a -lpthread -lm -luring -o /tmp/s \
    && /tmp/s && echo "logos toolchain OK"
ENTRYPOINT ["logosc"]
