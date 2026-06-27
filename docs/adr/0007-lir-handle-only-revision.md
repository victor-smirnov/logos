# ADR 0007 — Handle-only L-IR: revision of ADR 0006's slice plan

Status: Proposed 2026-04-27. Revises ADR 0006 §7 (slice plan) and §8
(non-decisions). All other sections of ADR 0006 stand: single source of
truth is the Writ zone, identity is the zone offset, one zone per
`LProgram`, write-once nodes, inline `Varchar` strings.

## Context

Between 2026-04-26 and 2026-04-27 we attempted ADR 0006's Stage 3g
incrementally and stopped at B.3 + a surgical SLoop migration. The work
exposed three load-bearing facts that ADR 0006's slice plan did not
account for. They invalidate the dual-write / interim-back-pointer
strategy and force a different cutover shape.

### Fact 1 — Sema mutates variant payloads after construction

Sema routinely rewrites variant fields after the builder returned a node:

- `sema_stmt.cpp:558+` patches `tlit->elems[ei]->type` after tuple-lit
  construction (type inference back-fill).
- `sema_stmt.cpp:268` rewrites `SReturn` → `SLet __ret_tmp + drops +
  SReturn(var_ref)` by `std::move`-ing the value out of one variant into
  another.
- Mono substitution (`mono_clone.cpp`) rebuilds `LExprPtr` trees by
  walking the variant and producing a new owning tree.

ADR 0006 §5 ("write-once") was an empirical claim. It is wrong. The
variant tree is *not* write-once today; sema and mono both mutate
post-construction. ADR 0006 §7's 3g.1 assert-equality phase would
therefore fire on these sites and block the slice.

Memory: `feat_lir_mirror_eager_emit_gaps.md` documents the discovery.

### Fact 2 — `LStmt` is reparented after construction

`make_stmt` returns by value; callers move the stmt into a containing
`LBlock`, which is then moved into another stmt (`SBlock`, `SIf`,
`SLoop`), eventually landing inside `LFunction.body`. Each move changes
the stmt's address. Eager-mirror at `make_stmt` time snapshots the stmt
in isolation, before its final children/parents exist. Bulk-emit's
fast-path then sees `mirror_offset_ != 0` and skips re-mirror — the
mirror points at a partial graph that does not match reality.

Naive "make `LirBuilder` write Writ alongside variants" (ADR 0006
slice 3g.1) crashes MLIR-gen on tests 255, 497, 853 — same bug-family
as 89049ed (heap recycle) and a08c40a (addr-cache). Surgical per-stmt
emit works (validated for SLoop, commit 1b36b31) only when the stmt's
construction site is also its final-state site.

### Fact 3 — Single-kind variant drop is illusory

`LExprPtr` (and `LPatternPtr`, `LStmtPtr`) are `unique_ptr` fields of
the variant payloads. Dropping any one variant kind's payload requires
moving its owned children somewhere; the variant *was* the owner.
Tested on `SReturn`: dropping `SReturn::value` cascades to `SLet`
(receives the moved `LExprPtr` at sema_stmt.cpp:268), which cascades to
every other variant containing an `LExprPtr` (~55 fields across
`lir.hpp`). There is no "pilot one kind, then iterate" path. Either
ownership moves out of the variant for *all* kinds at once, or it does
not move at all.

## Decision

Replace ADR 0006 §7's five-slice plan with a three-slice plan that
treats handle-only L-IR as a **non-incremental** cutover for the
construction path, while keeping the ADR's overall direction.

### A. The dual-write window does not exist

Skip ADR 0006 slice 3g.1 entirely. Do not add Writ-emit alongside
variant construction. Reasons:

- Fact 1 makes the assert-equality check a tripwire on every
  type-inference back-fill site. Resolving each one ahead of the cutover
  is the same diff as the cutover itself.
- Fact 2 forces per-kind reasoning about "is this site final-state"
  exactly where dual-write is most tempting (LStmt). The sites where
  surgical eager-emit is safe are already covered by view migration
  (B.3 + SLoop); the sites where it isn't are the same sites that need
  the cutover.

### B. Slices

- **Slice 1 — `LirBuilder` ownership pool (infrastructure-only).**
  Add `LirBuilder::expr_pool_` (or `LProgram::expr_pool_`),
  `vector<unique_ptr<LExpr>>`. Every `expr_*` and `stmt_*` builder
  method moves the constructed node into the pool and returns a
  non-owning `LExpr*` / `LStmt*`. Variant fields that today own
  `LExprPtr` change to raw `LExpr*` pointing into the pool. Mirror's
  fast-path keying does not change (variant pointer identity is
  stable in the pool). All sema/mono/borrow_check callsites that today
  do `std::move(sr->value)` change to copy/rebuild via builder, which
  is mechanical because the new value either replaces the old (drop
  the old pointer) or wraps it (point to it).

  This slice ships standalone. Tests stay green. No view migration in
  this slice. The slice's value is that *after* it lands, "drop
  variants" stops being a 5-day cascade and becomes a per-kind diff.

- **Slice 2 — Builder writes Writ; variants become shells; consumer
  signatures flip to `ExprRef`/`StmtRef`.**
  Combines ADR 0006 slices 3g.2 + 3g.3 + 3g.4 into one diff. Reason:
  Fact 1 means we cannot leave variants partially populated through
  intermediate slices — any variant field still read by sema must
  either be fully populated (current world) or fully gone (handle
  world). Half-populated is a correctness regression hidden behind a
  view abstraction.

  In one commit:
  - `LirBuilder` methods write the zone TinyObjectMap directly and
    return `ExprRef` / `StmtRef` / `PatRef`. The variant struct is
    still constructed (interim) but only as a key into the pool from
    slice 1. Variant payload fields are *not* read after this slice;
    they exist solely so `mirror_table` lookup still works for
    `expr_ref_of`.
  - Sema mutation sites (Fact 1) rebuild via builder instead of
    patching: `tlit->elems[i]->type = T` becomes `b.with_type(elem_ref,
    T)` which constructs a new node, drops the old. Type back-fill
    becomes a builder rebuild, mirroring how mono substitution will
    work in slice 3.
  - All consumer signatures flip from `const lir::LExpr&` to `ExprRef`.
  - `lir_mirror.cpp` (the post-sema bulk-emit pass) is deleted.
  - The interim variant pool from slice 1 stays alive as a holding
    pen for `expr_ref_of` lookup.

- **Slice 3 — Delete variants.**
  `LExpr` / `LStmt` / `Pattern` structs and their `kind` variants are
  removed. The pool from slice 1 is removed. `expr_ref_of` is removed.
  `mirror_table` is removed. Identity is `(arena*, offset)` everywhere.
  Mono substitution rewrites to `view + builder` — walk an `ExprRef`
  with a substitution map, build a new tree via `LirBuilder`. This is
  the same shape as the "rebuild via builder" pattern from slice 2's
  type back-fill, just at function granularity.

  `LFunction::body` becomes `BlockRef` (ADR 0006 slice 3g.5).

### C. Mutation semantics in the new world

ADR 0006 §5 ("write-once after construction") becomes correct *because*
the cutover enforces it. After slice 2, no consumer can mutate a node:
the variant struct is gone (slice 3) or unreadable (slice 2). All
"mutation" is replaced by **rebuild-and-replace**: produce a new node
via builder, write the new offset wherever the old offset was held.

This is a real semantics change and is the technical heart of the
cutover. Cost: every type-inference back-fill site does an extra small
allocation. Benefit: mono substitution, metaprogram splicing, and
freeze-before-codegen all become uniform operations on top of the
rebuild primitive instead of three different mutation strategies.

### D. The pool as a stepping stone, not a destination

Slice 1's pool exists to make slice 2 reviewable. In slice 3 it is
deleted. Anyone reading the codebase between slices 1 and 2 should
treat the pool as scaffolding, not API.

If slice 2 lands within the same week as slice 1, the pool is fine. If
the gap stretches, document the pool as scaffolding in
`lir_builder.hpp` so future-us doesn't ossify it.

## Consequences

- ADR 0006's expected memory and mono-clone wins still hold; they just
  arrive at the end of slice 3 instead of being amortized across five
  slices.
- Reviewability is worse than ADR 0006 imagined. Slice 2 is the largest
  diff in the compiler's history (estimate ~270 expr sites + ~163 stmt
  sites + ~21 pattern sites + every consumer signature). It must land
  in one commit because Fact 1 makes intermediate states incorrect.
  Mitigation: extensive use of `git rebase -i` to split the diff into
  reviewable patches *post hoc*, with a single tree-level test gate at
  the tip.
- The "rebuild via builder" pattern (slice 2 type back-fill, slice 3
  mono substitution) becomes a new compiler idiom. It needs one good
  named helper and one round of style review before it propagates.
- ADR 0006 §7 is superseded. ADR 0006 §1–§6 and §8 still apply.

## Verification strategy

- **Slice 1**: ctest 916+/916+ green. Pool turnover instrumented; no
  leaks under valgrind on a representative compile.
- **Slice 2**: ctest 916+/916+ green at HEAD only — intermediate
  rebase steps are not required to be green. Mono perf benchmark must
  not regress (variants still allocated, but mirror double-write is
  gone, so net wash is acceptable).
- **Slice 3**: ctest 916+/916+ green. Compiler binary size drops
  measurably (variant types and `lir_mirror` gone). Mono perf
  benchmark improves ≥10% (ADR 0006's target).

## Followups

Same as ADR 0006 §"Followups". The mutation-API followup is partially
discharged by §C (rebuild-and-replace is the mutation API).

## Open questions

1. **`SLet`-rewrite in `sema_stmt.cpp:268`**: the current code uses
   `std::move(sr->value)` to thread the LExprPtr through. Under §C this
   becomes "construct new SLet whose value is the existing ExprRef,
   construct new SReturn whose value is var_ref(__ret_tmp)". Verify
   that re-pointing the existing ExprRef doesn't break borrow-check's
   provenance keys (it shouldn't — the Ref is identity-stable).
2. **Mono substitution allocation cost**: rebuilding an entire function
   body in the zone for each clone may grow the zone faster than the
   variant world. Benchmark before slice 3 ships; if regressed, scope a
   per-clone sub-zone in a follow-up ADR.
3. **Metaprogram splicing**: Phase 7 will want to insert builder-built
   subtrees into existing functions. §C makes that natural (the splice
   is a rebuild of the containing block); confirm with the first
   metaprogram that actually does it.
