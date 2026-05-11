# Import Strategy — rustc tests/ui

This file documents the **prioritised plan** for adapting tests from
[rust-lang/rust](https://github.com/rust-lang/rust)/tests/ui. The
plan is intentionally batched and conservative: rustc's `tests/ui`
holds ~20,354 .rs files across 320 top-level categories — wholesale
import is neither feasible nor useful. The strategy below picks
batches that maximise Logos compiler-coverage per unit of porting
effort and that exercise areas where Logos and Rust have substantial
semantic overlap.

For the import workflow (per-file headers, manifest rows, mechanical
porting) see [README.md](README.md). For the per-batch commit pin
see [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).

## Pinned upstream

First batch will pin

  rust-lang/rust commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`
  (2026-05-10, master HEAD; PR #156405).

Local clone for browsing: `/home/victor/cxx/rust`. Note that
`git describe --tags --abbrev=0` on this commit returns
`1.0.0-beta` — that's an artefact of rust-lang/rust's tag scheme
(release-versioned tags live on stable branches, not master), not
the actual age of the source. The pin is the commit SHA.

## Volume — categorisation

### Permanently skipped — semantic mismatch with Logos design

Logos design choices that deliberately diverge from Rust. Tests
exercising the Rust-specific mechanic don't translate. For the
per-category rationale ("what is `unboxed-closures`, anyway?") see
[WHY-WE-SKIP.md](WHY-WE-SKIP.md).

| Category | Files | Reason |
|---|---|---|
| `async-await` | 461 | fibers, no async colouring — model mismatch |
| `proc-macro` | 360 | `#[fn_macro]` / `#[token_macro]` replace; not API-compatible |
| `specialization` | 113 | not in Logos by design |
| `unboxed-closures` | 96 | legacy Rust pre-stable; current Logos closures sufficient |
| `transmute`, `transmutability`, `unsafe-binders`, `unsafe-fields` | small | out of scope (Logos has `unsafe { }` for the bounded use-cases) |

### Permanently skipped — not language

Tests that exercise rustc's build infrastructure or target backends,
not the language surface.

| Category | Files | Reason |
|---|---|---|
| `bootstrap`, `compiletest-self-test`, `argfile`, `command`, `rustdoc*` | small | build-infra, not language |
| `wasm`, `sanitize-*`, `target_modifiers`, `unwind-abis`, `windows-subsystem` | small | target / platform backends |
| `nightly-features`, `rust-2018/21/24` | many | edition / unstable surface, not stable language |
| `feature-gates` (most) | 273 | rustc-internal feature flags |
| `single-use-lifetime` | small | rustc-specific lint, not a language rule |

Permanent-skip total: ~1500+ files we never touch.

### Deferred — implement first, import later

These are **real language gaps** in Logos that we plan to close.
Tests for them stay in the rustc source until the corresponding
Logos feature lands; at that point the relevant rustc tests
become a regression suite for the new feature.

| Category | Files | Planned Logos work |
|---|---|---|
| `try-block`, `try-trait` | small | `?` operator + `Try` trait pair — big-but-bounded language gap |
| `asm` | 116 | `asm!` intrinsic — Logos has a stub; full integration TBD |
| `simd` | small | SIMD intrinsics — useful, doable on top of MLIR vectors |
| `generic-associated-types` | 163 | GATs (type-level fns) — needs the trait resolver to handle assoc-type families with type-param substitution |
| `higher-ranked` | 108 | HRTB (`for<'a> Fn(&'a T)` and friends) — same trait-resolver work |
| `associated-types` (subset) | 295 | most basic assoc-types already work in Logos; the GAT-adjacent subset waits for the resolver upgrade |
| `nll` | 370 | non-lexical lifetimes — overlaps borrowck; the harder cases need the new borrow-check infrastructure |
| `regions` (advanced) | 219 | named lifetimes beyond what Logos's current pass handles |
| `coherence` (subset) | 196 | coherence rules under blanket impls + assoc types — same resolver work |
| `coroutine` | 160 | generators / coroutines — separate from fibers; will be added when concrete use-case lands |
| `repr` | small | `#[repr(...)]` memory-layout control — Logos may add for ABI-compatibility |

**Datalog engine is the implementation vehicle for the bottom block
of this table.** Per `~/.claude/plans/persistent-roadmap-v2.md`-style
ordering: HRTB / GAT / blanket-impl-coherence pressure justifies the
~1k LOC Datalog-derived (datafrog-clone) trait resolver. It is built
during Track 3 alongside test-import — when the imported tests reveal
the resolver-shape pressure, that's the trigger. The NLL / regions
backlog uses the same fixpoint engine once the trait resolver lands.

When each feature lands, its rustc tests join the import flow as
an "expected-to-pass" batch + regression suite. Until then, NOT
imported — would just be noise.

## Volume — what is in scope

Categorisation by Logos coverage and porting cost:

### Tier 1 — Core language (highest ROI)

These map most directly to Logos features we already ship. Errors
caught here strengthen the compiler we have today.

| Category | Files | Notes |
|---|---|---|
| `parser` | 884 | syntax edge-cases; large but cherry-pick top-level only first |
| `borrowck` | 468 | ownership / borrow check |
| `traits` | 1168 | massive — pick by sub-directory |
| `trait-bounds` | 41 | small, focused |
| `generics` | 98 | type-param inference |
| `typeck` | 257 | type checking |
| `type-inference` | 22 | inference edge cases |
| `binding` | 108 | let / pattern binding |
| `pattern` | 322 | match patterns |
| `match` | 100 | match expressions |
| `closures` | 297 | closure capture, types |
| `coercion` | 87 | implicit conversions |
| `cast` | 73 | `as` casts |
| `structs`, `enum`, `union` (combined) | ~220 | basic compound types |
| `methods` | 112 | method resolution |
| `inference` | 94 | (general) type inference |

Tier 1 total ≈ 4500 files. First few batches focus here.

### Tier 2 — Surrounding features (good coverage, more porting work)

| Category | Files | Notes |
|---|---|---|
| `regions` | 219 | named lifetimes; Logos has partial support |
| `lifetimes` | 183 | overlap with regions |
| `derives` | 92 | `#[derive(...)]` shape — needs `#[fn_macro]` style adaptation |
| `attributes` | 185 | most apply; some Rust-specific (`#[repr]`, `#[lang]`) skipped |
| `error-codes` | 252 | many can stay as fail tests once message-wording is loose |
| `macros` | 480 | only those translatable to `#[fn_macro]` / `#[token_macro]` |
| `imports`, `resolve`, `use`, `modules` | ~590 | module / use rules; some Rust-edition-specific |
| `iterators` | 64 | needs IntoIterator parity (done) |
| `consts` | 782 | constants — many trivially port, some `const fn` skip |
| `lint` | 682 | most lints not in Logos; pick a few |

Tier 2 total ≈ 4000 files. Batched after Tier 1.

### Tier 3 — Polish / advanced (lower priority)

| Category | Files | Notes |
|---|---|---|
| `mir` | 214 | post-typeck IR; partly applies |
| `impl-trait` | 496 | `impl Trait` in arg / ret — Logos supports some |
| `associated-type-bounds` | 100 | trait bounds on assoc types — basics already work |
| `hygiene` | 102 | macro hygiene — applies once we mature `gensym` use |
| `privacy` | 187 | `pub` rules |
| `feature-gates` | 273 | mostly rustc-internal — skip most |

Tier 3 total ≈ 1400 files. Done as needed.

(`coherence`, `nll`, `regions` advanced, `associated-types` GAT-adjacent
slice, `generic-associated-types`, `higher-ranked` — moved to the
**Deferred** block above, since they wait on the Datalog trait
resolver.)

### Tier 4 — Catch-all / opportunistic

`issues` (460) — historical bug reports. Pick when triaging Logos
regressions; otherwise skip.

`suggestions` (413) — diagnostic-quality tests; nice-to-have once
Logos diagnostics polish lands.

## Per-batch shape

A typical batch is one category (or a sub-directory of a large
category) imported in one commit:

* **Batch size**: 30–60 tests, completed in a single session.
* **Commit**: pinned rustc SHA, all tests in this batch share it.
* **Manifest update**: one row per test in
  [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).
* **Test runner**: pass tests → `tests/imported/ui/<cat>/<name>.{logos,expected}`;
  fail tests → same path under `tests/imported/ui/<cat>/` (the
  CMake glob recognises both regardless of subdirectory).

Tests carrying `//~ ERROR <msg>` annotations port to Logos **fail**
tests; the `.expected` file holds a substring of the expected diag
(Logos test runner does substring match, not exact stderr diff).

Tests with no `ERROR` annotations port to Logos **pass** tests with
`exit: 0` unless the original returns a specific value.

## Proposed batch order

Each batch ends with a green ctest run.

1. **Batch 1 — parser top-level** (~40 tests, focused mostly on
   syntax edge cases we already cover: precedence, struct-literal
   restrictions, brace/semicolon edge cases). Calibration of the
   workflow; some failures expected and triaged into bug tickets.

2. **Batch 2 — basic borrowck** (~40 tests, move/copy/reference
   rules; the bedrock of Logos ownership tracking).

3. **Batch 3 — generics + type inference** (~50 tests). Includes
   verifying our `<T, const N: i64>` slice-3.6 fix against rustc's
   const-generic suite.

4. **Batch 4 — patterns + match** (~40 tests).

5. **Batch 5 — closures** (~40 tests). Logos has closures + impl
   on `&T` (from slice ~5).

6. **Batch 6 — coercion + cast** (~30 tests).

7. **Batch 7 — methods + traits/object-safe** (~50 tests).
   Picks single sub-folders out of `traits/` rather than the whole.

8. ...

After every two-three batches: pause to triage the failures —
each "test fails because Logos doesn't implement X" becomes a
ticket on the gap-grind list (the third leg of roadmap-v2 Track 3).
The import + gap-grind feedback loop is the whole point.

## Gap-grind link

When a test fails because of a Logos feature gap, the workflow:

1. Add the test to the import batch as a `pass`/`fail` test in the
   manifest **expected to currently fail**.
2. Open a ticket in the gap registry (TBD location — likely
   `docs/baghunt/` or a new `docs/track3-gaps/`).
3. When the gap closes, the imported test starts passing — that's
   the regression test for the gap closure.

This is exactly the Track 3 promise from
[memory: roadmap 2026-05-10](../../README.md): "test-import + gap-grind
generates the trait-system pressure that justifies Datalog" (when we
need it). Each batch reveals a slice of gap surface.
