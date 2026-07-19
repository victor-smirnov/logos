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

## pdtbuf_dump — PackedDataTypeBuffer cross-check reference (PdtBuf rungs P0+P1+P2+P3)

Generates `tests/pdtbuf_fixtures.hex`: **228 cases** over
`PackedDataTypeBufferT<BigInt, true, C, DTOrdering::{SUM,MAX}>` for C in
{1,2,3,4}, in four flavours.

**Marker-1 (88 cases, rung P0)** — row counts {0,1,2,3,5,8,15,16,17,31,32}, at
or below `IndexSpan` = 32, so HEAD stays on its scan/bisection paths. Each
case carries the corpus plus (query → answer) triples for `sum`, `find_fw_ge`
and `find_fw_gt`, and — for MAX cases — a record of HEAD's `sum(column,row)`
used as *divergence evidence* (see below).

**Marker-2 (40 cases, rung P1)** — row counts {33, 64, 65, 100, 200, 1023,
1024, 1025, 2000, 32769}, i.e. above the span threshold, where HEAD
materializes its index. The case dumps HEAD's index **cells, level by level**,
walking `so.index()` until `has_index()` stops (the index is HEAD's own
recursive self-type, `using IndexType = MyType` —
`packed_datatype_buffer.hpp:65`), and `pdtbuf_core.logos` asserts exact
numeric identity against our levels, cell for cell. 32769 rows reaches **three
index levels** (1025 → 33 → 2).

    2 <columns> <rows> <seed>
    <n_levels>
      <level_rows> <cells ...>       x n_levels, cells ROW-MAJOR

The **corpus is not dumped** for marker-2 cases: it is a pure function of
(seed, columns, rows) through the same LCG, in the same column-major fill
order, that the Logos gate runs — so both sides regenerate byte-identical data
and a 32769-row case costs a few KB of index cells instead of a megabyte of
corpus. Self-checked before emission from the *definition*: level 1 cell (c,k)
is the **partial** sum of rows `[k*32, min((k+1)*32, rows))` — not a prefix —
and level L+1 is that same rule applied to level L's cells; the level count is
recomputed independently; and HEAD's own `so.check()` (which for SUM buffers
walks the index and re-derives every span sum, `so.hpp` `check_sum` :1649) is
called on every case.

**Marker-3 (80 cases, rung P2)** — the start-relative and **backward** search
families, at row counts {0, 1, 5, 31, 32, 33, 64, 65, 100, 1025}, i.e. on both
sides of the span threshold so HEAD is exercised on its scan path *and* its
index path, in two corpus **modes**: `0` = LCG values mod 23, `1` = a
leading-zero **plateau** corpus (a prefix plateau is the only shape under which
a rebase/complement target is attained by more than one row index, which is
where bug #27 lives). The corpus is regenerated from `(mode, seed, columns,
rows)` as for marker 2.

    3 <columns> <rows> <seed> <mode>
    <n_q>
      <dir> <op> <col> <start> <val> <idx> <prefix>              x n_q
    <n_div>
      <dir> <op> <col> <start> <val> <status> <idx> <prefix>     x n_div

`dir` 0 = `findGE/GTForward(column, start, val)` (`start` in `[0, rows]`),
1 = `findGE/GTBackward(column, start, val)` (`start` in `[0, rows)`, inclusive),
2 = `findGE/GTBackward(column, val)`; `op` 0 = GE, 1 = GT.

The two blocks are the whole oracle discipline of this port in miniature.
`<n_q>` holds the queries where HEAD **agrees** with an implementation derived
from the definition — cross-check fixtures the Logos gate must match.
`<n_div>` holds the ones where it does not: the gate asserts the
**disagreement** and matches the independent model instead. Every emitted
answer is self-checked against a naive walk before it is classified, and the
harness prints a per-case divergence breakdown to stderr.

**Third recorded divergence (P2, upstream bug candidate #27).** Every `<n_div>`
entry — across all four column counts, both corpora, every row count, on both
the scan and the index path — is the **non-strict** operation at **`val == 0`**,
exactly the prefix-plateau case the derivation predicted before the harness was
first run. Three observable faces, one root (a delegated forward search
resolving a prefix plateau at the wrong end, with nothing clamping its answer
back into the caller's range):

- `find_ge_fw_sum(column, start, val)` (`so.hpp:1394-1400`) answers with a row
  **below** `start`, and `FindResult::sub_prefix` then subtracts a larger
  prefix from a smaller one: for a true answer of `(2, 0)` HEAD returns
  `(1, 18446744073709551614)`.
- `find_ge_bw_sum(column, start, val)` (`:1445-1460`) answers with a row
  **above** `start` — outside the range it was asked to walk — with the same
  underflowed prefix.
- both backward forms then call `sum_sum(column, res.local_pos() + 1)` with no
  bound check; when the delegated answer is `size`, that trips
  `MEMORIA_ASSERT(idx <= size())` (`:1601`). The assertion macro
  (`core/tools/assert.hpp:38`) **catches its own throw and calls
  `std::terminate()`**, so the process dies and the query cannot be observed at
  all — the harness identifies these independently (`terminates_head`) and
  reports them unexecuted with `status = 1`.

The Logos port clamps the delegated answer back into the walk's range at both
sites (the semantics admit exactly one answer: a zero demand is met by the
first row examined, having accumulated nothing) and guards the complement
subtraction before evaluating it. Transcribing HEAD's unclamped shape into the
port aborts the gate: exit 132 for either face.

**Marker-4 (20 cases, rung P3)** — **mutation traces**. A scripted, fully
deterministic op sequence (insert at the front / straddling a span, update
inside one span / across spans, remove, split off a suffix) driven through
HEAD's *own* two-phase surface — `make_update_state()` → `prepare_*` →
`commit_*` (`so.hpp:533-1060`) plus `split_to` — at row counts {5, 33, 65,
100, 1025}, i.e. below the span threshold, above it, and deep enough for two
index levels.

    4 <columns> <rows> <seed>
    <n_steps>
      <op> <a> <b>                       op 0=insert 1=update 2=remove 3=split
      <n_in> <in values ...>             row-major inputs the step consumed
      <post_rows> <values_ok> <index_ok>
      <n_probe> (<row> <col> <val>) x n_probe
      <n_levels> (<level_rows> <cells ...>) x n_levels

The corpus is regenerated from `(seed, columns, rows)` as for markers 2 and 3;
only the *inserted* values are dumped, because the Logos gate must replay the
identical script. Rows are probed exhaustively at or below 128 and every 31st
above, so a 1025-row case stays a few KB while still covering every span.

Two self-checks travel with every step, and the second is the interesting one:

- `values_ok` — HEAD's post-state against a naive `std::vector` model of the
  operation, written from its definition;
- `index_ok` — HEAD's index cells, level by level, against span sums
  **recomputed from HEAD's own post-state rows**, plus a coverage check that
  the index spans every live row. This is what would catch a mutation that
  forgets to reindex: the values can be perfectly right while every cached
  span sum is stale.

**Result — and a null result is reported as a result.** Across all four column
counts, all five row counts and all eight steps: **0 divergences**. HEAD's
FSE/SUM mutation paths agree with the definition on both rows and index cells.
So **no new upstream bug is claimed at P3.** The catalogued defects in this
neighbourhood are real but not observable here: `prepare_remove`
(dispatcher base, `:189`) returns SUCCESS unconditionally for every
FIXED-size buffer, which leaves `do_prepare_remove` (`:614`) — and the
`for (size_t column = 0; column < 1; column++)` loop inside it that would
price only column 0 — **dead code**; and `split_to` (`:533`) genuinely has no
prepare and no budget (it grows and fills `other`, then `commit_remove`s from
`self`, so an OOM between the two tears both nodes), but a single-threaded
harness holding a 256 KB block cannot provoke it. One suspicion of mine did
not survive checking and is therefore **not** recorded as a bug:
`do_commit_update_fxd_sum` (`:1025`) does call `reindex()` after forwarding to
the `_max` arm.

The Logos gate (`check_mut_fixture_case`, exit codes 246..255) replays each
script step for step and requires row parity unconditionally, index-cell
parity wherever HEAD reports itself coherent, and — whatever HEAD did — that
its own post-state satisfies `prepared_bytes == consumed_bytes`, `check()`,
model equivalence and index-vs-full-rebuild parity.

**Role.** Unlike `ssrle_dump`, this harness is NOT an authority. The C++
PDTBuffer/FQTree family is bug-dense (23 catalogued during the port recon) and
the FQTree half was deleted upstream having never been covered by a test
suite. Correctness for the Logos port is decided by the independent
materialized model + algebraic laws inside `pdtbuf_core.logos`; these fixtures
exist so that any disagreement with HEAD becomes visible and has to be
explained. The harness self-checks every emitted answer against a naive
implementation derived from the operation's *definition* before writing it
(208/208 cases, 0 failed self-checks at the time of generation — the marker-3
disagreements are *classified*, not counted as self-check failures).

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

**Second recorded divergence (P1, upstream bug candidate #26).** HEAD's
`find_ge_fw_sum` / `find_gt_fw_sum`
(`packed_datatype_buffer_so.hpp:1501-1514`, `:1541-1554`) treat an index
*miss* as the final answer — `res.set_local_pos(this->size()); return res;` —
returning "not found over the whole buffer" without examining a single row.
That is correct only for an index whose size is exactly `div_up(size, 32)`;
against an index covering fewer spans — precisely the state HEAD's own
`create_index()` leaves behind before `insert_from_fn` fills it — it reports
not-found over rows it never read. The Logos port derives the residual
instead ("resume the scan at the first row the index does not cover"), which
subsumes HEAD's answer rather than special-casing it. Transcribing HEAD's
short-circuit into the port fails the gate at exit 119.

## Known edges

Known codec edge (preserved, not fixed, on both sides): for Bps=7,
`max_pattern_length()` = 59/7 = 8, but LEN_BITS=3 caps the encodable
pattern length at 7 — a plen-8 run encodes corruptly in C++ itself. The
corpus stays within the encodable range.
