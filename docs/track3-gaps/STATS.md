# Track 3 — gap-per-imported-test trend

The headline metric: **how many catalogued gaps does an average imported
rustc test surface?** Phase 1 ran at ~2 gaps/test (every test tripped
something new). The trend should fall as Logos's surface grows.

## Per-batch ledger

Counted *at the time the batch landed*.

- **Gaps** = new catalog entries (any status — Closed, Partial, Open,
  Divergence) emitted between the previous batch's commit and this
  batch's commit. Retroactive fixes that silently un-trim earlier
  imports don't count here (logged in `docs/track3-gaps/*-gaps.md`
  instead).
- **Bugs** = fixes landing in this batch that do *not* show up in
  any gap catalog: silent miscompiles, codegen ABI bugs, parser
  off-by-ones, mono regressions, mangling collisions, stdlib
  inconsistencies, etc. Counted per *root cause* (one logical
  fix = one bug, even if it took multiple commits or touched
  multiple files). Use `—` when the count couldn't be reconstructed
  reliably from the batch's commit/history (early batches were
  import-only and didn't track this).

| Batch | Date | Tests | New gaps | Bugs fixed | gaps / test | Cumulative tests | Cumulative gaps | Cum bugs |
|---|---|---|---|---|---|---|---|---|
| B1   | 2026-05-11 | 11 |  ≈ 18 | — | 1.6 | 11  | 18  | — |
| B2   | 2026-05-11 | 11 |  ≈ 14 | — | 1.3 | 22  | 32  | — |
| B3   | 2026-05-11 | 12 |  ≈ 15 | — | 1.3 | 34  | 47  | — |
| B4   | 2026-05-11 |  5 |  ≈ 11 | — | 2.2 | 39  | 58  | — |
| B5   | 2026-05-11 |  3 |  ≈  5 | — | 1.7 | 42  | 63  | — |
| B6   | 2026-05-11 |  5 |  ≈  9 | — | 1.8 | 47  | 72  | — |
| B7   | 2026-05-11 |  3 |  ≈  4 | — | 1.3 | 50  | 76  | — |
| B8   | 2026-05-11 |  6 |  ≈  6 | — | 1.0 | 56  | 82  | — |
| B9   | 2026-05-11 |  2 |  ≈  5 | — | 2.5 | 58  | 87  | — |
| B10  | 2026-05-11 |  3 |  ≈  3 | — | 1.0 | 61  | 90  | — |
| B11  | 2026-05-11 |  3 |  ≈  3 | — | 1.0 | 64  | 93  | — |
| B12  | 2026-05-11 |  6 |     1 | — | 0.17 | 70  | 94  | — |
| B13  | 2026-05-11 |  4 |     0 | — | 0.00 | 74  | 94  | — |
| B14  | 2026-05-11 |  4 |     1 | — | 0.25 | 78  | 95  | — |
| B15  | 2026-05-11 |  4 |     0 | — | 0.00 | 82  | 95  | — |
| B16  | 2026-05-11 |  3 |     1 | — | 0.33 | 85  | 96  | — |
| B17  | 2026-05-11 |  5 |     0 | — | 0.00 | 90  | 96  | — |
| B18  | 2026-05-11 |  3 |     0 | — | 0.00 | 93  | 96  | — |
| B19  | 2026-05-11 |  5 |     2 | — | 0.40 | 98  | 98  | — |
| B20  | 2026-05-11 | 12 |     6 | — | 0.50 | 110 | 104 | — |
| B21  | 2026-05-11 |  9 |     0 | — | 0.00 | 119 | 104 | — |
| B22  | 2026-05-11 |  4 |     2 | — | 0.50 | 123 | 106 | — |
| B23  | 2026-05-11 |  7 |     0 | — | 0.00 | 130 | 106 | — |
| B24  | 2026-05-11 |  3 |     0 | — | 0.00 | 133 | 106 | — |
| B25  | 2026-05-11 |  2 |     0 | — | 0.00 | 135 | 106 | — |
| B26  | 2026-05-11 |  4 |     0 | — | 0.00 | 139 | 106 | — |
| B27  | 2026-05-11 |  2 |     0 | — | 0.00 | 141 | 106 | — |
| B28  | 2026-05-11 |  1 |     0 | — | 0.00 | 142 | 106 | — |
| B29  | 2026-05-11 |  2 |     0 | — | 0.00 | 144 | 106 | — |
| B30  | 2026-05-11 |  1 |     0 | — | 0.00 | 145 | 106 | — |
| B31  | 2026-05-11 |  1 |     0 | — | 0.00 | 146 | 106 | — |
| B32  | 2026-05-11 |  1 |     0 | — | 0.00 | 147 | 106 | — |
| B33  | 2026-05-11 |  1 |     0 | — | 0.00 | 148 | 106 | — |
| B34  | 2026-05-11 |  2 |     0 | — | 0.00 | 150 | 106 | — |
| B35  | 2026-05-12 |  1 |     0 | — | 0.00 | 151 | 106 | — |
| B36  | 2026-05-12 |  1 |     0 | — | 0.00 | 152 | 106 | — |
| B37  | 2026-05-12 |  0 |     0 | — | 0.00 | 152 | 106 | — |
| B38  | 2026-05-12 |  2 |     0 | — | 0.00 | 154 | 106 | — |
| B39  | 2026-05-12 | 13 |     0 | — | 0.00 | 167 | 106 | — |
| B40  | 2026-05-12 |  5 |     0 | — | 0.00 | 172 | 106 | — |
| B41  | 2026-05-12 |  3 |     0 | — | 0.00 | 175 | 106 | — |
| B42  | 2026-05-12 |  5 |     0 | — | 0.00 | 180 | 106 | — |
| B43  | 2026-05-12 |  2 |     0 | — | 0.00 | 182 | 106 | — |
| B44  | 2026-05-12 |  3 |     0 | — | 0.00 | 185 | 106 | — |
| B45  | 2026-05-12 |  6 |     0 | — | 0.00 | 191 | 106 | — |
| B46  | 2026-05-12 |  3 |     0 | — | 0.00 | 194 | 106 | — |
| B47  | 2026-05-12 |  3 |     0 | — | 0.00 | 197 | 106 | — |
| B48  | 2026-05-12 |  3 |     0 | — | 0.00 | 200 | 106 | — |
| B49  | 2026-05-12 |  5 |     0 | — | 0.00 | 205 | 106 | — |
| B50  | 2026-05-12 |  2 |     0 |  1 | 0.00 | 207 | 106 |  1 |
| B51  | 2026-05-12 |  1 |     0 |  0 | 0.00 | 208 | 106 |  1 |
| B52  | 2026-05-12 |  3 |     0 |  0 | 0.00 | 211 | 106 |  1 |
| B53  | 2026-05-12 |  2 |     0 |  0 | 0.00 | 213 | 106 |  1 |
| B54  | 2026-05-12 |  3 |     0 |  1 | 0.00 | 216 | 106 |  2 |
| B55  | 2026-05-12 | 36 |     0 |  0 | 0.00 | 252 | 106 |  2 |

(Phase-1 gap counts are estimates — pre-batch gap-as-code triage gave
coarse totals only; precise per-batch arrival-order numbers weren't
recorded. Phase-2 onward — from B12 — gap counts are exact. **Bug
counts** start from B50 — earlier batches were import-driven and
incidental bug fixes weren't separated from gap-closure commits;
backfilling reliably isn't possible. From B50 onward each batch's
commit message lists the bugs it touched, and the column counts
root-cause fixes.)

Bugs counted so far:
- **B50** — `slice_index` struct-element ABI: GEP stride was
  `logos_to_mlir(Struct) == ptr_type()` (8B) instead of the
  aggregate's actual `sizeof(Struct)`. Affected `[Struct;N]` array
  destructure (P4-pm-15 close surfaced it). Fix: detect struct/
  ZonedStruct element type and GEP with the LLVM struct type.
- **B54** — duplicate `str_eq` mangling collision: stdlib's
  `std.sys.args` carried a private `fn str_eq(a: str, b: str)`
  that mangled identically to the new `pub fn str_eq` in
  `std.lang.text.string`; mlir-gen rejected "duplicate function
  body for symbol …". Fix: delete the private copy.

## Reading

Phase 1 (B1–B11) baseline: **~1.5 gaps/test**. Many tests surfaced
multiple gaps because Logos's pattern, coercion, fn-family, generics,
and metaprog surfaces were all rough at once.

Phase 2 (B12+): trending toward **~0.2 gaps/test** as the easy/middling
surfaces close. New gaps are mostly narrow codegen issues (e.g.
P4-pm-17 ref-bind deref chain) or specialised patterns (tuple-rest,
exclusive ranges). Single gap can block multiple tests, so total
unblocked-tests-per-gap-closure is also worth eyeballing.

Phase 3 (B29–B35): "arc closure" batches — work was gap-closure-driven,
not import-driven. Each batch lands a slice of the Sprint 5.8 dyn-arc
(C6-cc-09 / C6-cc-08 / C5-cl-04 / C5-cl-08) and re-instates the rustc
test that originally surfaced the gap. Zero net new catalog entries
since every closure here re-fills an "Open" status flipped earlier.
The Sprint 5.8 dyn-arc is fully closed at B35.

Phase 5 (B50–B52): targeted gap-closure run. B50 closed
P4-pm-15 (array destructure Drop case — slice_index struct-element
fast path + move-track temp/RHS) and P3-pg-04 (break-as-expression
codegen — EBlockExpr + SBreak + terminated-block tolerance in
EBlockExpr / EIfExpr branches). B51 partial-closed P4-pm-01
(struct-shape enum variants) — declaration, construction, and
irrefutable struct-shape match patterns land end-to-end, including
shorthand, rename, `..` rest, and missing/duplicate/unknown-field
diagnostics. New IS_STRUCT_SHAPE flag (slot 47, reuses LABEL) on
VARIANT_DEF / ENUM_LIT_DATA / PAT_VARIANT_DATA; SemaVariantInfo
gains `payload_field_names` parallel to `payload_types`. mlir-gen
unchanged — sema resolves names → positions, downstream stays
positional. Still open: refutable inner patterns + let-destructure
with single-variant enum. ctest 1439 → 1442. Catalog flips: 2
Partial → ✅ Closed, 1 Deferred → Partial. B52 fully closed
P4-pm-01 — refutable inner literal patterns (PAT_INT,
PAT_NEG_INT, PAT_BOOL, PAT_CHAR) now lower via synthesized
`__refut_* == <value>` arm guards in BOTH tuple-shape and
struct-shape variant payloads (so `Option::Some(1)` works
alongside `E::V { f: 1 }`); single-variant struct-shape
let-destructure (`let E::V { f } = e;`) lowers as a temp
plus per-binding match-as-expression. Three rustc tests
un-trimmed: `issue-8351-1`, `issue-8351-2`, `issue-11577`.
IS_STRUCT_SHAPE moved into the new `variant` key group
(slot 47, sibling of `mod`) for cleaner slot documentation.
ctest 1442 → 1445. Catalog: P4-pm-01 Partial → ✅ Closed.
B53 closed two P4-pm-01 follow-ups: P4-pm-24 (variant pattern
as tuple-pattern element — sema allow-list extension + per-element
disc-check in mlir-gen tuple-arm dispatch, both stmt and expr
forms) and P4-pm-25 (or-patterns in match arm LHS — sema
fan-out when any alt is a variant pattern, so each alt goes
through the normal single-arm path with its own refutable
guard and payload extraction; mixed-shape `Opaque {with: true, ..}
| Transparent` now works correctly). Two more rustc tests
un-trimmed: `issue-5530`, `issue-114691`. ctest 1445 → 1447.
Catalog: P4-pm-24 Open → ✅ Closed, P4-pm-25 Open → ✅ Closed.
B54 closes the last three Partials in pattern-match-gaps: P4-pm-03
(or-pattern at tuple element via grammar + sema dispatch + mlir-gen
OR-chain — single-alt PAT_OR unwraps so existing tuple-elem
binding/scalar shapes still parse identically), P4-pm-06 (str-typed
const-pattern via synth `__str_<n>` binding + `str_eq(__str_<n>, CONST)`
guard pushed to refutable-guard side channel; new `pub fn str_eq` in
`std.lang.text.string`, parallel private copy in `std.sys.args`
deleted to avoid mangling collision), and P4-pm-07 (`b"…"` at
expression position via new LIT_BYTES AST code that decodes escapes
parity-with-PAT_BYTES and emits `[u8; N]` ArrLit; const-init allow-list
extended). Three more rustc ports un-trimmed: `issue-11940` (str-const
pattern), `issue-72680-g` (or-pat inside 4-element bool tuple), and a
homegrown `byte_string_expr` regression. ctest 1447 → 1449 (plus the
72680-g import → 1450). Catalog: P4-pm-03 / P4-pm-06 / P4-pm-07 all
Partial → ✅ Closed. **All pattern-match-gaps now Closed**, and
the only remaining cross-catalog open work is small follow-ups
documented inline.

Phase 6 (B55): import sweep through under-explored dirs. 36 new
tests across numbers-arithmetic, expr, for-loop-while, mir, tuple,
consts, traits, autoref-autoderef, moves, pattern, recursion,
functions-closures, binop, array-slice-vec. Zero new gaps caught,
zero bug fixes — all imports went in clean modulo the usual
isize→i64 / `assert!`-to-conditional-return / Box-skip adaptations.
Several test candidates skipped on the agent's pass with the same
flavour as Phase 4: features that conflict with Logos design
(unsafe trait markers, Box-heavy iterator chains, struct-variant
enums in the few corners P4-pm-01 doesn't reach via the let-pat
path, vec!-macro tests until that lands). Re-confirms that the
gap surface is genuinely drained: a 36-test sweep produces no
new catalog entries. ctest 1450 → 1484.

Phase 4 (B38–B49): "autonomous bulk-import" run. Single overnight
session through under-imported dirs (binding, match, for-loop-while,
typeck, drop, mir, moves, attributes, inference, tuple, generics,
binop, enum, coercion, closures, issues). 50+ tests added, zero new
gaps catalogued — Phase 2/3 had already drained the easy surface,
so the remaining stalls were variants of known gaps (struct enum
variants, Box/Vec ABI corners, FnMut indirection edge-cases) which
the batch silently skips. ctest 1389 → 1439.

Update this table whenever a numbered batch lands.
