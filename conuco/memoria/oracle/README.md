# C++ oracle harnesses

Standalone programs compiled against the REAL C++ Memoria headers
(`~/cxx/memoria`) that generate committed fixture files for the paired-port
gates. No CMake — one `g++` invocation per harness.

## ssrle_dump — SSRLE codec fixtures (increment 1)

Generates `tests/ssrle_fixtures.hex`: a deterministic corpus (Bps 1..8; single
runs at every residual-bit budget boundary, padding at exact 32-unit segment
edges, long + LCG-seeded streams) with the exact u16 unit streams produced by
`SSRLERunTraits<Bps>::write_segments_to`. Consumed by `tests/ssrle_codec.logos`
(byte-identity + size-oracle + roundtrip gate).

Regenerate (from `conuco/memoria/`):

```
g++ -std=c++20 -O1 -DMMA_ICU_CXX_NS=icu_74 \
    -I $HOME/cxx/memoria/core/include \
    -I $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/include \
    oracle/ssrle_dump.cpp -o /tmp/ssrle_dump \
&& /tmp/ssrle_dump > tests/ssrle_fixtures.hex
```

Include-path notes:

- The codec layer (`core/include/memoria/core/ssrle/*.hpp`) is header-only but
  pulls abseil + ICU headers transitively; the vcpkg tree inside the Memoria
  build dir provides both. Adjust the vcpkg path if the build dir moves.
- `-DMMA_ICU_CXX_NS=icu_74` overrides the `icu_60` default in
  `core/config.hpp` to match the installed ICU (`unicode/uvernum.h` →
  `U_ICU_VERSION_MAJOR_NUM`).

The harness self-checks every case (C++ `compute_size` == units written; C++
decode roundtrips to the source runs) and prints per-bps case counts to
stderr; stdout is the fixture stream, `0`-terminated.

Known codec edge (preserved, not fixed, on both sides): for Bps=7,
`max_pattern_length()` = 59/7 = 8, but LEN_BITS=3 caps the encodable
pattern length at 7 — a plen-8 run encodes corruptly in C++ itself. The
corpus stays within the encodable range.
