# ADR 0006 — Stage 3g: Cutover to Writ-only L-IR

Status: Proposed 2026-04-26. Builds on ADR 0005 (Stage 3f).

## Context

ADR 0005 split the L-IR migration in two. Stage 3f is now closed:
sema_expr / sema_stmt / sema_decl construct every node through
`LirBuilder`, and `LirMirrorEmitter` projects the resulting `lir::LExpr` /
`lir::LStmt` / `Pattern` variant trees into a Writ mirror that
mlir_gen / borrow_check / mono_clone read via `lir_view::ExprRef` /
`StmtRef` / `PatRef`.

The result is a system that holds the same data twice. Every L-IR node
exists as a `std::variant` *and* as a `TinyObjectMap` in the Writ zone,
emitted right after sema and re-emitted (via mono's input mirror) for
each cloned function. Costs:

- ~2× memory and ~2× write bandwidth during sema and mono.
- A non-trivial post-sema pass (`lir_mirror.cpp`, ~400 LoC) whose only
  job is variant→Writ projection.
- Mono carries a `mirror_table` keyed on variant pointer identity, which
  forces every cloned function to be re-mirrored before borrow_check /
  mlir_gen can read it.
- Two source-of-truth questions (which-side-do-I-edit) for every new
  L-IR field; new schema entries require dual-edit (variant struct +
  schema key + mirror writer + view reader).

ADR 0005's Stage 3g is the planned exit: delete the variant types,
collapse `LirBuilder` into a Writ-direct emitter, change consumer
signatures from `const lir::LExpr&` to `lir_view::ExprRef`. This ADR
locks in the decisions that the cutover depends on and slices it.

## Decisions

### 1. Single source of truth: Writ zone. Variants delete.

`lir::LExpr` / `lir::LStmt` / `Pattern` (and their `kind` `std::variant`s,
the per-variant struct fields, `EClosure`, `LMatchArm`, `LBlock`, etc.)
are removed. The Writ mirror — a TinyObjectMap with the
`schema_type_code` set per `lir_schema::expr/stmt/pat::Code` — becomes
the only representation.

`lir::LExprPtr` / `lir::LStmtPtr` / `lir::LPatternPtr` cease to exist as
owning types. Construction APIs return `lir_view::ExprRef` /
`StmtRef` / `PatRef` — the same fat (arena*, offset) view types
consumers already use for reads.

### 2. `LirBuilder` writes the zone directly; mirror emitter is deleted.

Each `LirBuilder` method that today builds a variant
(`make_unique<LExpr>` + `e->kind = lir::EFoo{...}` + `e->type = ty`)
becomes: allocate a TinyObjectMap in the program's Writ zone, set
`schema_type_code` to the corresponding `lir_schema::expr::Code`, write
the relevant `expr_keys` keys, return an `ExprRef` into the new map.

`src/compiler/lir_mirror.{cpp,hpp}` is deleted. Its content moves
field-by-field into `LirBuilder` methods. The `LProgram::mirror_table`
hash (variant-pointer → mirror-offset) is also deleted; identity *is*
the offset after Stage 3g.

Mono's "rebuild input mirror" step (currently invoked at the start of
each clone) ceases to exist — the input is already a Writ mirror.

### 3. Identity = zone offset. No more variant pointers.

Today an `ExprRef` is constructed from an `LExpr*` via
`expr_ref_of(*lexpr)`, which looks the variant pointer up in the
program's `mirror_table`. After Stage 3g the variant doesn't exist, so
the lookup table doesn't either. `ExprRef` becomes the only handle, and
its (arena, offset) pair is the identity used by everything that needs
identity (borrow-check provenance keys, mono's clone-table keys, etc.).

### 4. Storage: one Writ zone per `LProgram`.

`LProgram` already owns `type_pool.zone()` (TypePool's Writ zone after
Phase 2). Stage 3g extends that zone to also host L-IR. No second zone,
no per-function arenas:

- L-IR nodes share the type pool's zone so that `expr_common::TYPE`
  fields can be plain `RelPtr<LogosType>` into the same address space.
- The zone stays `Zone<Mutable>` through the entire pipeline (sema,
  mono, borrow_check, mlir_gen). It is *not* frozen, because mono needs
  to *grow* the zone with cloned function bodies after sema is done. A
  later "freeze before LLVM IR-gen" optimization is possible but is not
  in this ADR.

`LFunction::body` stops being a `std::unique_ptr<LBlock>` and becomes a
`lir_view::BlockRef` (or equivalent). `LFunction` stays a `struct`
(name, sig, body-ref, flags) on the C++ heap; only its body — the IR —
moves into the zone. Function-level metadata (extern flag, generic
params, etc.) is *not* moved into Writ by this ADR.

### 5. Mutability of nodes after construction.

Stage 3g does not introduce node mutation. Sema constructs each node
once via `LirBuilder` and never edits it afterwards (this is already
true today — the variant tree is also write-once in practice). Mono
clones by *re-building* through `LirBuilder`, never by patching an
existing node in place. The TinyObjectMap representation supports
mutation, but consumer code is forbidden from using it.

This keeps Stage 3g a refactor, not a semantics change. A later ADR may
relax this if metaprograms need to splice new nodes into existing IR;
the open question is captured in §"Followups".

### 6. String storage: inline `Varchar` everywhere; no string pool yet.

Stage 3g writes every `std::string` field (`name`, `method`,
`callee`, `struct_name`, `op`, …) as an inline Writ `Varchar` —
copied into the zone byte-for-byte. No interning, no per-zone string
pool.

Reasoning: the natural shape of a string pool is per-`LProgram`, but
mono clones into the *same* program, so a pool would need to dedupe
across function-clone boundaries; that interacts with Phase 7 stdlib
plans (where metaprograms see strings from outside-the-zone Logos
values) and with TypePool's existing string handling. The right time
to decide is when the first metaprogram actually surfaces the cost.
Inline copies are baseline-correct and the migration's goal is parity
with Stage 3f, not new optimizations.

### 7. Slice plan.

Stage 3g executes as five sub-slices, each independently testable
against the existing 916+ ctest suite.

- **3g.1 — Builder writes Writ in addition to variants.** Each
  `LirBuilder` method gets a Writ-emit step that mirrors what
  `LirMirrorEmitter` does today. `LirMirrorEmitter` stays alive but
  becomes redundant; an assert verifies the two writers agree on each
  node's keys, then the assert is removed when 3g.1 is fully landed.
  No consumer change. Tests stay green throughout.

- **3g.2 — Delete `LirMirrorEmitter` and `mirror_table`.** Drop
  `lir_mirror.{cpp,hpp}`, drop the post-sema "emit mirrors" call in
  `sema.cpp`, drop mono's input-mirror rebuild. Mirrors come exclusively
  from `LirBuilder`. `expr_ref_of(LExpr&)` is rewritten to read a
  per-node back-pointer field that 3g.1 writes into the variant when it
  builds the mirror (interim only).

- **3g.3 — Flip residual consumer signatures to `ExprRef`/`StmtRef`.**
  ~10 sites in `borrow_check.cpp`, `mono_clone.cpp`, `mlir_gen*.cpp`
  still take `const lir::LExpr&` / `const lir::LStmt&`. Bodies already
  go through views; signatures change mechanically. After 3g.3 nothing
  in the code dereferences a variant pointer.

- **3g.4 — Delete variant types.** Remove `lir::LExpr`/`LStmt`/`Pattern`
  structs, all `lir::EFoo`/`lir::SFoo` payloads, `EClosure`,
  `LMatchArm`, `LBlock`'s `stmts` vector (replaced by Writ Array of
  StmtRef in zone). `LirBuilder`'s return types become `ExprRef` /
  `StmtRef` / `PatRef`. The interim back-pointer from 3g.2 is gone.
  `LExprPtr` / `LStmtPtr` typedefs are deleted. This is the largest
  diff, but is mechanical: no behavior changes, only types.

- **3g.5 — `LFunction::body` to `BlockRef`.** Last variant-shaped
  field. `std::unique_ptr<LBlock>` becomes a zone-resident block.
  `LFunction` itself stays in C++.

`LirBuilder` constructor signature does not change between sub-slices —
it already takes `LProgram&` and can reach the zone through it.

### 8. What 0006 does *not* decide.

- **A `lir_builder.hpp` API redesign.** The Stage 3f surface
  (`builder().bin_op(...)`, etc.) stays. Names, arg orders, trailing-ty
  convention — all unchanged.
- **Metaprogram-driven mutation of L-IR nodes.** Stage 3g writes
  once-and-done. Mutation is a Phase 7 question.
- **String interning** (see §6).
- **Freeze-before-codegen.** Possibly worthwhile (immutable LLVM-IR-gen
  read path, marginal speedup), separate ADR if pursued.
- **Cross-`LProgram` references.** Single zone per program is the
  invariant; cross-program references aren't a thing in Logos.

## Consequences

- **Memory: a single TinyObjectMap per node** instead of variant +
  TinyObjectMap. Estimate: ~40-50% reduction in L-IR-related
  allocations during sema. Mono clone cost halves on the same axis.
- **No more sync questions.** Adding an L-IR field is one schema-key
  edit + one builder method edit + one view accessor — no variant
  struct, no mirror writer.
- **Consumer code already mostly survives** because Phase 3e routed
  reads through views. The cutover is type-system surgery, not logic
  change.
- **`expr_ref_of` disappears.** Anywhere that looks up identity via a
  variant pointer (e.g. mono clone-table, borrow-check provenance
  table) keys directly off the `(arena, offset)` pair instead.
- **Risk: zone size grows visibly during compilation.** Writ zones
  are bump-allocated; without a freeze step we never reclaim. For
  realistic programs the zone holds the entire L-IR through codegen
  anyway, so the change is "we hold it earlier", not "we hold it
  longer". To be re-checked with benchmarks at end of 3g.

## Verification strategy

- **Per-slice ctest 916+/916+ green.** Standard.
- **3g.1 assert-equality phase.** During the dual-write window, each
  builder method asserts the variant-side mirror and the
  builder-emitted mirror have identical keys and values. Catches drift.
- **3g.4 size check.** After variant deletion, ensure compiler binary
  size drops measurably (proxy for "the variant types are really
  gone").
- **Mono perf check.** Time `cargo build` of a generic-heavy stdlib
  fixture before and after 3g; expect ≥10% improvement, regression
  blocks landing.

## Followups

- **Mutation API.** Phase 7 will need it for metaprogram-driven AST/IR
  rewrites; design under a separate ADR when the first metaprogram
  needs it.
- **String interning** (§6 deferred decision).
- **Optional freeze-before-codegen** ADR if benchmarks justify it.
- **Update `feat_lir_schema_max_keys_blocker.md`** memory: the
  per-namespace renumber landed; `expr_keys` tops out at 51, fits
  MAX_KEYS=52. The blocker is closed.
