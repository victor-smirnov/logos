# bufdrop-2026-09-11 — THE LANDING ROUND

The pricing round (`e3be929e5`) priced `BufReader`/`BufWriter` freeing on drop
at zero in every column and recommended candidate C.  This round wrote its own
counter-examples, found the recommended arm sound, and found that **the class it
was priced against was one member short**.

## 1. THE CLASS, ENUMERATED BY PROPERTY — AND WHY IT IS NOT WHAT THE SWEEP SAID

The leak sweep's class was `buf_reader_wrap`/`buf_writer_wrap`: **2 types, 9
records, 6 fixtures**, and it was defined by *which fixtures valgrind caught*.

The property is: **a stdlib struct whose value-returning constructor heap-
allocates a buffer it stores, which exposes an explicit release function, and
which declares no `impl Drop`.**  Enumerated mechanically over every
`stdlib/**/*.logos` struct with a raw-pointer field and an allocating
constructor — 9 candidates, split by the property that decides whether a `Drop`
impl can do anything at all:

| | ctor returns | drop glue exists? | members |
|---|---|---|---|
| **THE CLASS** | `T` by value | yes | `BufWriter`, `BufReader`, **`BTreeMap`** |
| not the class | `*mut T` | **no** — a raw pointer has no glue to hook | `Chan`, `FutureSlot`, `Scheduler`, `SpscRing`, `ThreadPool` |
| not the class | passes a non-owned pointer through | n/a — not an owner | `BtssRemRet` |

Control side, same shape but *with* `impl Drop` (13): `Arc Box BtvecBuf HashMap
LineReader MemoryStore Mutex PdtHolder Rc RwLock String Vec VecDeque`.

**`BTreeMap` is the third member, and it leaks.**  Measured on the base binary,
128 bytes in 2 blocks (init cap 8 × `sizeof::<i64>()`, twice — `keys` and
`vals`).  Its one leaking corpus fixture, `deem_btreemap_source`, is filed in
`unrowed_backlog.ledger` under **"10 singles with no group yet"** — the sweep
had looked for the group and concluded there wasn't one.  The property found it.

⚠ `tools/dlog` could not be used for this enumeration and the reason is
structural, not a preference: its extractor (`cxx_facts.cpp`) is a clang
LibTooling pass over **C++ translation units**, and this class lives in `.logos`
stdlib sources, which nothing in `tools/dlog` parses.  The enumeration above is
over declarations, cross-referenced against `impl Drop` and against the
constructor's *return form*, and its discriminator — value vs `*mut T` — is
exactly the kind a grep on the type name cannot see.

## 2. COUNTER-EXAMPLES — MY OWN, IN SHAPES THE PRICING PHASE DID NOT USE

The pricing phase's 12 shapes were all *use sites constructing a concrete
`BufWriter<Sink>`*.  Seven new shapes, each run on both binaries:

| | shape | why it is new | base | armed |
|---|---|---|---|---|
| N1 | `BufWriter<BufWriter<FdWriter>>`, neither closed | the pricing phase nested only **BufReaders**, which never flush; this is the drop-ORDER question — the outer's `Drop::drop` must flush into the inner while that field is still alive | **rc 20** (nothing arrived) | rc 0 ✅ |
| N2 | `close()` **twice**, then scope exit | three release opportunities on one buffer, not two | rc 0 | rc 0 |
| N3 | direct write via `buf_writer_inner_mut`, then drop | output **ORDERING**: C's drop-flush lands after the direct write | **rc 31** (buffered byte discarded) | rc 0 ✅ |
| N4 | early-return path out of a scope with a live `BufWriter` | the loop shape only exercised the fallthrough exit | rc 0 | rc 0 |
| N5 | a sink whose `write` always fails, with and without `close` | drop must not retry through a freed buffer after a failed `close` | rc 0 | rc 0 |
| N6 | **user generic `fn f<W>(bw: &BufWriter<W>)` with no bound** | candidate C moves `W: Write` onto the struct — this is the API cost probe | rc 0 | **rc 0** |
| N7 | **user `struct Holder<W> { inner: BufWriter<W> }`** | same question at a declaration site | rc 0 | **rc 0** |

N1 and N3 fail on base *precisely because base never flushes on drop* — they are
candidate C's positive tests, written before the arm was built.

**N6/N7 are the round's second finding.**  In Rust, `struct BufWriter<W: Write>`
makes both of those programs errors: a use of the type must satisfy its
declared bound.  Logos accepts both.  So the struct bound candidate C adds buys
exactly one thing — the E0367 permission for `impl<W: Write> Drop` — and costs
**nothing** at use sites, because Logos does not enforce a struct's bound at its
use sites at all.  That is a divergence from Rust in a mechanism no registry
covers; it is reported, not exploited, and it is what makes C's API cost zero.

## 3. BOTH DIVERGENCE REGISTRIES, RE-GREPPED BY CONSTRUCT (rule 17)

  * `docs/DIVERGENCES.md` — 17 LETTER rows, enumerated: `A1 … A17`.  One hit for
    the construct grep, and it is `~~B3~~` (a struck-through `Box<[T]>` row)
    matching on the word *buffer*.  Nothing about io/buffered, nothing about
    `BTreeMap`.
  * `docs/spec/*.md` — **3854** `### \`clause.id\`` clauses (the prompt says
    3851; I report what I counted), **497** of them in `divergences.md`.
    `grep -rinE 'bufreader|bufwriter|buf_reader|buf_writer|io/buffered'` over
    the whole of `docs/spec/` exits **1** — zero hits, rc read without a pipe.

Neither registry covers these types.  The standing rule therefore settles it and
this is not an escalation.

⚠ **The Rust claim rests on my reading.**  `/home/logos/cxx/rust` has **no
`library/` tree** (verified: `library/std/src/io/buffered/` does not exist) and
there is no rustc binary on this box.  The claim used — Rust's `BufWriter` flushes
and frees on drop, its `BufReader` frees on drop via `Box<[u8]>`, and Rust puts
the `Write` bound on the `BufWriter` struct declaration — is stated from
knowledge, not from a file in this tree, and the round record says so.

## 4. WHAT LANDED

Two files, one structural change stated at three sites: **the release function
nulls what it freed, and `Drop` performs the same guarded release.**
`BufWriter::close` already did the nulling half; `btreemap_free` did not, and a
`Drop` added on top of it as it stood would have double-freed at
`tests/logos/pass/btreemap_basic.logos:82`.

## 5. DECLINED, BY NAME, WITH THE NUMBER

  * **Element drops in `BTreeMap`** — `btreemap_free` deallocs the two arrays
    and never drops the K/V elements, and `remove` shifts elements and drops
    nothing either, so elements are dropped **nowhere** in this type.  A
    `BTreeMap<String, String>` leaks its elements before and after this landing.
    Dropping them only on the drop path, while `remove` still discards silently,
    is a double-drop waiting at the first `remove`-then-drop program.  Different
    defect, different evidence; **0 of the 11 records this round closed are
    element leaks** — all 11 are the backing arrays.
  * **The 5 `*mut T`-returning types** (`Chan`, `FutureSlot`, `Scheduler`,
    `SpscRing`, `ThreadPool`) — an `impl Drop` on them is **inert**: a raw
    pointer has no drop glue for it to hook.  The zero here is structural, not
    measured-small, and it is why the class is 3 and not 8.
  * **Candidate A (free-only, no flush)** — declined against C on **one number**:
    the pricing round's S12 and this round's N1/N3.  A dropped `BufWriter` with
    3 buffered bytes DISCARDS them under A and DELIVERS them under C, and Rust
    delivers.  A is not cheaper in any column measured — both price rc 0 on
    `stdlib-cost`, both leave one differing `run_oracle` row
    (`cast-region-to-uint`, which prints a stack address and whose sha differs
    between every run), and both are ABI-PRESERVING.

## 6. TWO INSTRUMENT HOLES

  * **`btreemap_drop_frees_storage` cannot ratchet its own leak direction.**
    `BTreeMap` runs no element destructors, so there is no destructor COUNT to
    take, and the program exits 42 whether or not the arrays are released.  The
    pair pins the **double-free** direction by exit code (a second release
    aborts) and the leak direction only by the valgrind number recorded here.
    This is the prompt's own warning — "a leak reads as a clean exit" — holding
    in a case where the counter it prescribes does not exist.  The
    `BufWriter` pair does not have this hole: `flushes=0` vs `1` vs `2` is a real
    count and the pass half is RED on the base binary.
  * **A full `cmake --build` races on the stdlib layer after a stdlib edit.**
    `cmake --build build-copy -j32` fanned the *same* `liblogos-std.a` rule out
    to **17 concurrent dependents** through the fixed path
    `/tmp/logos_emit_logos-std/`, and died with
    `objcopy: error: the input file '/tmp/logos_emit_logos-std/logos-std.imp.raw' is empty`
    and `carries no readable .pkgi member after ar — the package index did not
    reach the archive; 27 package(s) would have been lost silently`.
    Building `--target stdlib_layers` first, then the full tree, is clean — the
    layer is then up to date and the fan-out has nothing to do.  This is latent,
    pre-existing, and only reachable after a stdlib source change, which is why
    no round has hit it; it is NOT caused by this change.  The failure mode is
    loud (rc≠0), not silent.

## 7. EVERY ORACLE, WITH ITS rc

| oracle | rc | number |
|---|---|---|
| `soundness_queue_gate.sh` (with `LOGOS_LIB_DIR`) | **0** | `# TOTAL 82`, 82 rows by direct listing — **unchanged; this landing closed no queue row** |
| `probe-log-lint.py` | — | 271 records, every site symbol resolves (unchanged) |
| `build_hash.py` | — | `260d74a881ec778e 43` → `d10ce02056b41dc8 43` (a stdlib landing moves it, as recorded) |
| `stdlib-cost.sh` | **0** | all four layers compile under 'nothing armed' |
| `cmake --build build` (full) | **0** | layer built first to dodge the race in §6 |
| `test-levels.sh L1` | **0** | 807/807, 12 684 generated cases, 143 gates |
| `logos_00_population_pin_lint` | **0** | corpus 3039→**3043**, glob 191 (unmoved), nonglob 2848→**2852**; re-derived by direct listing, partition closes |
| `logos_00_census_pin` | **0** | ALL 9595→**9599**, NOIMPORTED 5123→**5127**, TIERCOMMIT **143 unmoved** |
| `run_oracle.py` | **0** | 6665 fixtures compiled+linked+RUN; **6661 pre-existing rows joined, exactly 1 differs**, and it is `cast-region-to-uint` — whose sha also differs between the pricing round's OWN control and candC tables (`5ded1140` vs `3f9c37d7` vs `46eb5b01`), which is the independent proof that it is run-to-run noise. Diffed BOTH ways: nothing only-in-base. |
| `abi-check.sh` | see §8 | 3 symbols added, 0 removed |
| `test-levels.sh L4 bc` | see §8 | |

## 8. THE CLOSED SET, DIFFED BOTH WAYS

11 valgrind loss records over 8 fixtures, every one measured on both binaries:

| fixture | base | armed |
|---|---|---|
| `bufio_generic_reader` | 1 rec, 5 b | **0** |
| `http_parse_stream` | 2 recs, 96 b | **0** |
| `http_framing_readers` | 3 recs, 80 b | **0** |
| `http_chunked_reader` | 1 rec, 16 b | **0** |
| `http_chunked_body_helper` | 1 rec, 64 b | **0** |
| `http_serialize_stream` | 1 rec, 64 b | **0** |
| `deem_btreemap_source` | 2 recs, 128 b | **0** |
| `b1_btreemap_scope` (hand) | 2 recs, 128 b | **0** |
| **the other direction — must stay clean** | | |
| `bufio_basic` | 0 | **0** |
| `adv3_generic_field_method_call` | 0 (exits 0, not 42) | **0** |
| `btreemap_basic` (calls `btreemap_free` at :82) | 0 | **0** |
| `b2_btreemap_free_then_scope` (hand) | 0 | **0** |

Every exit code identical on both binaries.  **No `Invalid free` anywhere, on
either binary, in any program.**

## 9. THE CONTROL REVERT, ON THE FOUR NEW FIXTURES

Run against the unmodified `build/` binary BEFORE it was rebuilt:

| new fixture | base binary | armed binary |
|---|---|---|
| `bufwriter_drop_flushes_buffer` | **rc 3**, `stdout: got= flushes=0`, **64 b lost** | rc 42, `got=abc flushes=1`, 0 |
| `bufwriter_close_then_drop_flushes_once` | rc 42, `got=abc flushes=1`, 0 | rc 42, `got=abc flushes=1`, 0 |
| `btreemap_drop_frees_storage` | rc 42, **128 b lost** | rc 42, 0 |
| `btreemap_free_then_drop_frees_once` | rc 42, 0 | rc 42, 0 |

The first row is the ratchet: the destructor COUNT separates the two binaries
(`flushes=0` vs `flushes=1`) where the exit code alone would not have.  The
second is the control twin that says the fix was not bought with a double
release.  Rows three and four are the pair whose leak direction the exit code
cannot see — stated in the fixture's own header, and the reason §6 records it as
an instrument hole rather than leaving it implied.
