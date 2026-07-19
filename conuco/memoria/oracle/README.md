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

## pdtbuf_dump — PackedDataTypeBuffer cross-check reference (PdtBuf rung P0)

Generates `tests/pdtbuf_fixtures.hex`: 88 cases over
`PackedDataTypeBufferT<BigInt, true, C, DTOrdering::{SUM,MAX}>` for C in
{1,2,3,4} and row counts {0,1,2,3,5,8,15,16,17,31,32} (at or below `IndexSpan`
= 32, so HEAD stays on its scan/bisection paths — the ones P0 implements).
Each case carries the corpus plus (query → answer) triples for `sum`,
`find_fw_ge` and `find_fw_gt`, and — for MAX cases — a record of HEAD's
`sum(column,row)` used as *divergence evidence* (see below). Consumed by
`tests/pdtbuf_core.logos`.

**Role.** Unlike `ssrle_dump`, this harness is NOT an authority. The C++
PDTBuffer/FQTree family is bug-dense (23 catalogued during the port recon) and
the FQTree half was deleted upstream having never been covered by a test
suite. Correctness for the Logos port is decided by the independent
materialized model + algebraic laws inside `pdtbuf_core.logos`; these fixtures
exist so that any disagreement with HEAD becomes visible and has to be
explained. The harness self-checks every emitted answer against a naive
implementation derived from the operation's *definition* before writing it
(88/88 cases, 0 failed self-checks at the time of generation).

Regenerate (from `conuco/memoria/`):

```
g++ -std=c++20 -O1 -DMMA_ICU_CXX_NS=icu_74 \
    -I $HOME/cxx/memoria/core/include \
    -I $HOME/cxx/memoria/containers/include \
    -I $HOME/cxx/memoria/containers-api/include \
    -I $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/include \
    oracle/pdtbuf_dump.cpp -o /tmp/pdtbuf_dump \
    $HOME/cxx/memoria/build/Clang_19-Debug/mbt-explicit/libCore.a \
    $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/lib/libfmt.a \
    -licuuc -licudata \
&& /tmp/pdtbuf_dump > tests/pdtbuf_fixtures.hex
```

Include/link notes beyond the ssrle recipe: the packed layer needs
`containers/include` **and** `containers-api/include`
(`memoria/profiles/common/block_operations.hpp` lives in the latter), and it
links real code — `libCore.a` plus the **vcpkg** `libfmt.a` (the system
`-lfmt` is a different ABI and fails to resolve `fmt::v11::vformat`).

**Recorded divergence (not conformed to).** For MAX/UNORDERED buffers the C++
routes `sum(column,row)` to `sum_gen`
(`packed_datatype_buffer_so.hpp:1577`), which sums `[row, size)` — a SUFFIX —
while the SUM arm `sum_sum` (`:1599`) sums `[0, row)` — a PREFIX. Its own
`sum(column,start,end) = sum(end) - sum(start)` (`:889`) is therefore the
NEGATION of the true range sum on any MAX buffer. The Logos port implements
prefix semantics for every ordering (the only reading under which that
identity holds); the fixture's `<n_gen>` block records HEAD's behaviour and
`pdtbuf_core.logos` asserts the divergence explicitly as
`cpp_gen(c,k) == sum_all(c) - sum_prefix(c,k)`. If upstream ever fixes
`sum_gen`, that assertion flips and reports it.

## Known edges

Known codec edge (preserved, not fixed, on both sides): for Bps=7,
`max_pattern_length()` = 59/7 = 8, but LEN_BITS=3 caps the encodable
pattern length at 7 — a plen-8 run encodes corruptly in C++ itself. The
corpus stays within the encodable range.
