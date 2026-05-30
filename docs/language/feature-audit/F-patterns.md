# Category F — Patterns (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`).

Summary: 3 features audited — 0 fully OK, 3 WARN (each landed but with surface/edge gaps), 0 completely-GAP. Aggregate state of patterns is "mostly Rust-conformant, with several catch-up edges". Headline gaps: PathPattern (constants-as-patterns) is partial and ad-hoc; default-binding-modes is gated to a narrow subset (only shared `&`, only move-only payloads); GroupedPattern is desugared away rather than represented; let-bindings still require irrefutability shapes hand-listed in sema instead of computed from a uniform `is_irrefutable` walk; `&&` reference-of-reference token form is not in the grammar.

---

## F.1 Pattern kinds

### Rust nomenclature
`Pattern`, `PatternNoTopAlt`, `PatternWithoutRange`. Spec enumerates: `LiteralPattern`, `IdentifierPattern`, `WildcardPattern`, `RestPattern`, `ReferencePattern`, `StructPattern`, `TupleStructPattern`, `TuplePattern`, `GroupedPattern`, `SlicePattern`, `PathPattern`, `MacroInvocation`, plus `RangePattern` and `OrPattern` at the top alt level. Reference: `/home/victor/cxx/reference/src/patterns.md` §patterns.syntax (lines 4–25), §patterns.literal..patterns.or.

### Logos nomenclature
A flat `Code::PAT_*` family in `tools/peg_gen/grammars/logos.peg:147-303`:
- `PAT_VARIANT`, `PAT_VARIANT_DATA` (used for both tuple-struct AND struct-shape variant — disambiguated by `IS_STRUCT_SHAPE`)
- `PAT_WILD` (does double duty: bare `_` wildcard AND a bound identifier — disambiguated by `NAME`)
- `PAT_INT`, `PAT_NEG_INT`, `PAT_BOOL`, `PAT_CHAR`, `PAT_STR`, `PAT_FLOAT`, `PAT_BYTES` (literals)
- `PAT_RANGE`, `PAT_CHAR_RANGE` (range; one node carries inclusive vs exclusive via `INCLUSIVE` flag)
- `PAT_REF` (reference pattern), `PAT_TUPLE`, `PAT_STRUCT`, `PAT_SLICE`, `PAT_UNIT`
- `PAT_AT` (`@`-binding), `PAT_FIELD` (struct-pattern field), `PAT_REST` (`..`)
- `PAT_OR` (or-pattern; the top `pattern` production always wraps in PAT_OR even for a single alt — sema unwraps)
- `PAT_HERMES_*` (Logos addition for `@null`/`@true`/`@42`/`@"…"`/`@[…]`/`@{…}` over Hermes-typed scrutinees — gated to match arms only at `src/compiler/sema_stmt.cpp:4454`)

Grammar entry points: `pattern` (`logos.peg:1779`), `pat_single` (`logos.peg:1835`), `pat_single_base` (`logos.peg:1865`).

Build pipeline: `SemaChecker::build_pattern` and friends at `src/compiler/sema_stmt.cpp:2636-4623` — a large pc-switch that lowers each AST `PAT_*` into a `lir::Pattern` mirror.

### Match verdict
**WARN — rename/normalize needed.** Logos covers most Rust pattern kinds, but the surface naming diverges in three ways:
1. `PAT_VARIANT_DATA` is a single node serving both Rust's `TupleStructPattern` AND `StructPattern` (variant cases). Rust spec separates them. Recommend splitting at the AST level (`PAT_TUPLE_STRUCT` / `PAT_STRUCT_VARIANT`) for parity with the spec's distinction (and the `patterns.tuple-struct.namespace` value-vs-type-namespace nuance — which Logos does not implement).
2. `PAT_WILD` overloads bare `_` and the identifier-binding pattern. Rust spec separates `WildcardPattern` (matches anything, **does not bind**, `patterns.wildcard.no-binding`) from `IdentifierPattern` (binds). Recommend `PAT_IDENT` distinct from `PAT_WILD` — the current overload is the reason `current_pat_mut_names_` and the `IS_REF` side-channels exist (`sema_stmt.cpp:4615-4618`).
3. No `PathPattern` / `GroupedPattern` AST nodes. Const-as-pattern is hand-detected in the `PAT_WILD` fallback at `sema_stmt.cpp:4496-4611` via `module_const_values_` lookup, and grouped patterns are inlined directly into `PAT_OR` at parse time (`logos.peg:1995-1996`). Both are spec-named entities — Logos should represent them.

### Implementation pointer
- Grammar: `tools/peg_gen/grammars/logos.peg:1779` (`pattern`), `:1835` (`pat_single`), `:1865-2007` (`pat_single_base` alternatives).
- AST codes: `tools/peg_gen/grammars/logos.peg:147-303` (`PAT_*` enum values 84-245).
- Lowering: `src/compiler/sema_stmt.cpp:2636` (`build_pattern`), `:2642` (`build_pattern_variant`), `:2707` (`build_pattern_variant_data`), `:3520` (`build_pattern_bytes`), `:3879` (PAT_TUPLE), `:4234` (PAT_STRUCT), `:4382` (PAT_SLICE), `:4180` (PAT_REF inner).
- mlir-gen: `src/compiler/mlir_gen_stmt.cpp:3074-4623` (large `gen_match` arm-emission with kind-specific paths).

### Interactions check
Direct neighbours from the table for "Pattern kinds":

- **Match (most uses) — OK.** All PAT_* feed `match_arm` (`logos.peg:1732-1743`). Exhaustiveness is implemented in `check_match_exhaustiveness` (`sema_stmt.cpp:6608`) and `ast_patterns_exhaustive` (`:6934`); the latter handles nested enum payloads.
- **Let-bindings — WARN.** `LET_DESTRUCT` only supports tuple and struct shapes via the dedicated `lower_let_destruct` path (rejects other refutable shapes at `sema_stmt.cpp:1002-1003`). Full pattern surface in `let` is routed through `LET_PAT` (`logos.peg` enum `LET_PAT = 217`), but the spec wants `let` to accept any irrefutable pattern uniformly. The current code-path bifurcation (`LET_DESTRUCT` vs `LET_PAT` vs `LET_ELSE`) is sema scaffolding, not a spec divergence — consolidating onto one `build_pattern`-driven irrefutability check would track Rust 1:1.
- **Fn-params — WARN.** Grammar supports `(a,b): (T,U)` tuple-destructure (`logos.peg:1224`) and `mut x: T` (`:1214`), but only those two pattern shapes. Rust admits ANY irrefutable pattern as a fn-param (e.g. `fn f(Point { x, y }: Point)`). Struct-pattern, slice-pattern, and reference-pattern fn-params are absent.
- **`if let` / `while let` / `for` — OK.** Wired in grammar: `logos.peg:2112-2122` (if), `:2046-2050` (while), `:1687-1688` (for). Sema reuses `build_pattern` over the scrutinee type — composition works.
- **Refutability rules — WARN.** No single `is_irrefutable` predicate over AST. The closest is `is_irrefutable` in `mlir_gen_stmt.cpp:3521-3578` over LIR (used for exhaustiveness shortcut), but sema's let-destruct path uses a hand-rolled shape check at `sema_stmt.cpp:990-1003`. Single uniform predicate would close gaps like "refutable inner pattern not yet supported in struct-shape variant" (`sema_stmt.cpp:3218-3229`).
- **Binding modes — see F.3.**
- **Move vs borrow — see F.3.**
- **Type inference — partial.** Scrutinee type drives pattern build (`scrut_type` threaded through `build_pattern`). No or-pattern type-unification across alts beyond shape (spec §patterns.constraints.pattern requires unification both of types AND binding modes — not all checked).
- **Const generics (const pattern) — GAP.** Spec `patterns.const` describes constants-as-patterns (`patterns.const.partial-eq`, `patterns.const.structural-equality`, `patterns.const.translation`). Logos's implementation is *ad-hoc*: `sema_stmt.cpp:4496-4611` ctfe-evals a bare-ident pattern when it resolves to a module-const, only for scalar / `str` / `[u8; N]` consts, and only outside nested positions (str case requires `current_pat_refutable_guards_` channel — sub-pattern positions don't have it). Generic associated consts cannot appear as patterns (spec `patterns.const.generic`). Qualified paths in patterns (`<u8 as MaxValue>::MAX`) not supported. Path patterns over no-payload enum variants do work but only when the bare ident shape resolves (`sema_stmt.cpp:4214-4231`).

### Gaps / debt
- Split `PAT_VARIANT_DATA` into `PAT_TUPLE_STRUCT` and `PAT_STRUCT_VARIANT`; split `PAT_WILD` into `PAT_WILD` + `PAT_IDENT`.
- Introduce `PAT_PATH` and `PAT_GROUP` AST nodes to mirror spec `PathPattern` / `GroupedPattern`.
- Generalize constants-as-patterns: route via a `PAT_PATH` build that handles structural-equality types (struct/enum const aggregates, tuple/array consts, refs to such). Today only scalar/`str`/`[u8;N]` consts work.
- Generalize `let` and fn-params to accept any irrefutable pattern via a single `is_irrefutable(Pat)` predicate; retire the LET_DESTRUCT shape-restriction error at `sema_stmt.cpp:1002-1003`.
- B-pt-03 (byte-string scrutinee `&[u8]` rather than `[u8;N]`) and B-pt-06 (float pattern) are explicitly marked TODO at `logos.peg:1973-1978`. Marker `LET_PAT` (`logos.peg` enum 217) exists — needs the full uniform driver.
- No `MacroInvocation` in pattern position (Rust grammar allows; Logos metacall does not appear in `pat_single`).
- Or-pattern unification across alternatives (`patterns.constraints.pattern`): build_pattern_or (sema_stmt.cpp:1475-1486 says alts must bind same names+types) is enforced for `let-else` only — needs general check in any or-pattern position.

---

## F.2 Refutability

### Rust nomenclature
"Refutable" / "irrefutable" pattern; spec §`patterns.refutable` (`/home/victor/cxx/reference/src/patterns.md` lines 116-129). Each pattern kind tags its refutability: `patterns.literal.refutable` (always refutable), `patterns.wildcard.refutable` (always irrefutable), `patterns.ident.refutable`, `patterns.rest.refutable`, `patterns.ref.refutable`, `patterns.struct.refutable`, `patterns.tuple-struct.refutable`, `patterns.tuple.refutable`, `patterns.range.refutable`, `patterns.slice.refutable-array`, `patterns.slice.refutable-slice`, `patterns.path.refutable`. Rule: `match` accepts any; `let` / fn-params require irrefutable; `if let` / `while let` accept refutable.

### Logos nomenclature
Logos uses the same English terms ("refutable", "irrefutable") in code comments and diagnostics: e.g. `sema_stmt.cpp:923`, `:1003` ("other shapes are refutable; use 'match' or 'let-else'"), `:1128`, `:3218-3229`. There is no enum/type called `Refutability`; refutability is checked at the LIR level by an inline recursive lambda `is_irrefutable` (`mlir_gen_stmt.cpp:3521-3578`), and at the AST level by `ast_patterns_exhaustive` (`sema_stmt.cpp:6934`) which handles nested-enum coverage.

### Match verdict
**WARN — semantics present but not factored.** There is no single, canonical `is_refutable(Pat)` predicate. The notion is encoded across at least three sites:
1. `mlir_gen_stmt.cpp:3521-3578` `is_irrefutable` — LIR-level, used for shortcut in arm dispatch.
2. `mlir_gen_expr.cpp:3877-3879` — struct-pattern irrefutability check (every field-sub irrefutable).
3. `sema_stmt.cpp:990-1003` — ad-hoc shape gate at `let` lowering (rejects PAT_SLICE with `..` rest, etc.).
4. Refutable sub-patterns inside struct/variant patterns are routed through a side-channel `current_pat_refutable_guards_` (`sema_impl.hpp:3401`) which converts them into guard predicates — clever, but conflates "pattern is refutable" with "needs predicate emitted".

Recommend hoisting `is_refutable` to a single `bool SemaChecker::is_refutable(TinyMapView pat, TypeRef scrut)` (or LIR-level) used by let/fn-param/let-else/match-shortcut sites uniformly.

### Implementation pointer
- LIR-level: `src/compiler/mlir_gen_stmt.cpp:3521-3578` (`is_irrefutable` lambda).
- AST-level shape gate: `src/compiler/sema_stmt.cpp:990-1003` (`lower_let_destruct` rejection of refutable forms).
- Refutable-sub-pattern guard channel: `src/compiler/sema_impl.hpp:3401` (`current_pat_refutable_guards_`).
- Exhaustiveness driver: `src/compiler/sema_stmt.cpp:6608` (`check_match_exhaustiveness`) and `:6934` (`ast_patterns_exhaustive`).

### Interactions check
- **Match (all refutable allowed) — OK.** All `PAT_*` codes accepted; exhaustiveness check separate.
- **`let` (irrefutable required) — WARN.** Hand-listed acceptance: tuple, struct, tuple-struct, nested-tuple let-destruct, single-variant-enum let-destruct. Many irrefutable shapes that the spec allows (e.g. `let &x = …` over `&T`; `let RefStruct { ref a, mut b }`; `let (_, ..)`) work or partially work, but the gate is ad-hoc. The LET_PAT code (217) is the future "uniform" path — its hookup is partial (one explicit call in stmt dispatch via `lower_let_pat`).
- **`if let` / `while let` (refutable required) — WARN/PARTIAL.** Grammar wires refutable patterns through `if_expr` / `while_stmt`; no explicit "must be refutable" check — irrefutable `if let _ = expr` would compile (Rust warns/errors). No `unreachable_patterns`/`irrefutable_let_patterns` lint analogue grepped.
- **Fn-params (irrefutable) — GAP.** Surface only admits `IDENT`, `mut IDENT`, `(pat,..)`; no general pattern. So the spec's "must be irrefutable" constraint is vacuously satisfied because no refutable pattern can even be written.

### Gaps / debt
- Introduce one canonical `is_refutable(Pat, ScrutTy)` predicate in sema, replacing the three ad-hoc sites.
- Diagnose irrefutable `if let` / `while let` (lint or warning).
- Once fn-params accept any pattern, gate them by `is_refutable == false`.
- Slice-pattern refutability needs `patterns.slice.refutable-slice` rule: "irrefutable only with a single `..` rest or `ident @ ..`" — verify `is_irrefutable` (mlir_gen_stmt.cpp:3554-3556) matches.
- Exhaustiveness coverage of guarded arms (`a if g => …`) — DIVERGENCES.md notes `Logos doesn't yet prove finite-enum coverage of guarded arms` — Rust treats guarded arms as non-exhaustive too, so this is alignment.

---

## F.3 Binding modes / default bindings

### Rust nomenclature
`patterns.ident.binding` (RFC-2005 "match ergonomics"). Default binding mode starts "move"; matching a reference with a non-reference pattern auto-derefs and sets mode to `ref` (or `ref mut` under `&mut`). 2024-edition rules tighten — explicit `mut`/`ref`/`ref mut`/reference patterns only allowed when current mode is "move". Spec §patterns.ident.binding (lines 280-355).

### Logos nomenclature
Logos calls it "default binding modes" in code comments (`sema_stmt.cpp:3463-3512`, "RFC 2005 match ergonomics"). Implementation flags: `default_ref`, `default_mut`, `explicit_ref`, `binding_is_ref`, `binding_is_mut`, `binding_from_wild`. AST flags carried per-pattern node: `IS_REF`, `IS_MUT`. Grammar productions: `KW_REF IDENT` → `PAT_WILD { IS_REF }`, `KW_REF KW_MUT IDENT` → `PAT_WILD { IS_REF, IS_MUT }`, `KW_MUT IDENT` → `PAT_WILD { IS_MUT }` (`logos.peg:1840-1858`).

### Match verdict
**WARN — narrowly implemented; explicit Stage-1 restriction documented.** Default-binding-modes is gated to a SHARED `&` scrutinee under enum/tuple/struct patterns and ONLY auto-refs MOVE-only payloads (`sema_stmt.cpp:3504`). Spec ergonomics are broader: `&mut` should auto-`ref mut`-bind; under deeper nesting auto-deref should chain. Logos's own comment at `sema_stmt.cpp:3474-3483` flags this as a planned follow-up gated by `&T`-arith auto-deref and a self-ref recursion guard.

### Implementation pointer
- Enum/variant payload binding modes: `src/compiler/sema_stmt.cpp:3463-3512`.
- Tuple-pattern auto-deref under `&(T,U)` / `&mut (T,U)`: `sema_stmt.cpp:3881-3895`.
- Struct-pattern auto-deref under shared `&`: `sema_stmt.cpp:5147` (per-field `default_ref` propagation).
- Explicit `ref` / `ref mut` lowering: `sema_stmt.cpp:4189-4206` (PAT_WILD with `IS_REF`).
- Per-binding flag collection in PAT_VARIANT_DATA: `sema_stmt.cpp:3256-3317`.
- `is_move_type` (Copy gate): `sema_stmt.cpp:287, :3475, :3504` and others.

### Interactions check
- **Move/Borrow — WARN.** When the default binding mode is move, Copy/move discrimination is correct (Copy payloads stay by-value, move-only payloads under `&` scrutinee auto-`&`-bound to avoid double-free). The `&mut` case is intentionally NOT auto-ref'd (`sema_stmt.cpp:3478-3483`) — this differs from Rust ergonomics. Tagged as catch-up TODO not blessed divergence (no entry in DIVERGENCES.md §A for binding modes).
- **`&` patterns auto-deref — PARTIAL.** Tuple, struct, and enum-variant patterns auto-deref the outer `&` (covered above). Slice patterns auto-deref `&[T;N]` → `[T;N]` for byte-string (`sema_stmt.cpp:3581-3589`) but the general "non-reference pattern matches reference" recursion is not implemented as a uniform top-down walk; each pattern kind handles its own one-level auto-deref.
- **`mut` binding — PARTIAL.** `PAT_WILD` with `IS_MUT` is recorded in side-channel `current_pat_mut_names_` (`sema_stmt.cpp:4615-4618`) which `bind_pattern_ref` consumes to declare the binding mutable. Works at top level. Inside variant payloads, `binding_is_mut` is collected at `sema_stmt.cpp:3315`. 2024-edition restriction that `mut` is only permitted when default mode is "move" is not enforced (Logos's narrow default-mode means this rarely bites).
- **`ref` / `ref mut` (explicit borrow binding) — OK.** Lowered to a reference-typed binding at `sema_stmt.cpp:4194-4206`. The 2024-edition "only when default mode is move" rule is not enforced.
- **Reference patterns (`&pat`, `&mut pat`) — WARN.** `PAT_REF` lowering at `sema_stmt.cpp:4180-4187`. Works for top-level `&x` over `&T`. The double-amp form `&&` (single token in Rust for `&&pat` over `&&T`) is NOT in the grammar — `logos.peg:1855-1858` only branches `AMP KW_MUT pat_single` / `AMP pat_single`. Rust spec `patterns.ref.ref-ref` explicitly calls this out.
- **Match ergonomics — WARN.** The chain (`patterns.ident.binding.nested-references`: "If the automatically dereferenced value is still a reference, it is dereferenced and this process repeats") is not implemented as a fixed-point loop; auto-deref is one-level per pattern kind. So `match &&Some(3) { Some(x) => … }` likely fails to bind `x` correctly through both `&` layers.

### Gaps / debt
- Extend default-binding-modes to `&mut` scrutinees once the self-ref recursion guard is in place (per Victor's plan-default-binding-modes note at `sema_stmt.cpp:3478-3483`).
- Implement chained auto-deref for `&&T`, `&&&T` etc. (uniform top-down "if scrutinee still a ref and pattern is non-reference, peel and update mode" walk; replace per-kind ad-hoc deref).
- Add `&&` token form to `pat_single` per spec `patterns.ref.ref-ref`.
- Enforce 2024-edition restrictions (explicit `ref`/`ref mut`/`mut` and reference patterns only when default mode is move) — currently silently permitted. Diagnostic-only, no soundness.
- Bring `is_move_type` gate down (Victor's note `sema_stmt.cpp:3474-3476`) so Copy payloads under `&T` also auto-ref-bind — needed for `&T`-operator auto-deref-on-read to feel natural.
- Refutability + binding-modes intersection: when a refutable sub-pattern lives inside a `&` scrutinee variant payload, the synth-refutable-inner path (`sema_stmt.cpp:2870-2920`) needs to honor the by-ref binding for the synth temp.

---

## Cross-category gaps

- **Category B (Type system primitives) ↔ F:** Slice (`[T]`) is DST-only-behind-fat-pointer in Rust; Logos's slice patterns assume the scrutinee is a single Slice type (DIVERGENCES.md B5). When B3 (`Box<?Sized>`) lands, slice patterns over `Box<[T]>` will need consideration — current `PAT_SLICE` accepts `Slice` / `Array` only (`sema_stmt.cpp:4389`).
- **Category C (Items) ↔ F:** Const-as-pattern (PathPattern) crosses into const-item rules (`patterns.const.partial-eq`, `patterns.const.structural-equality`). Logos has §A1 blessed-divergence "no const-eval, use metacall" — but const-as-pattern is required for module consts used in `match` arms (which DO work today via ctfe-eval at sema_stmt.cpp:4499). The intersection of metacall and PathPattern is not exercised: a `metacall { … }` const result spliced into a match arm is unlikely to be recognized as a refutable literal pattern.
- **Category E (Expressions) ↔ F:** Match ergonomics interplay with auto-deref method receivers. The narrow default-binding-modes coverage means `match &mut opt { Some(x) => x.method() }` may resolve methods inconsistently with Rust.
- **Category G (Memory/safety) ↔ F:** `ref mut` binding interacts with interior mutability (`patterns.ident.binding.mode-limitations-binding` 2024-edition rule). Logos has no interior-mut primitives yet; once `Cell`/`RefCell`/`UnsafeCell` land, the binding-mode rules must hold.
- **Category D (Generics) ↔ F:** `patterns.const.generic` — associated consts involving generic parameters cannot be used as patterns (pre-mono ctfe constraint). Logos's mono pipeline (`mono_*.cpp`) does not consider const-pattern positions; should be a non-issue today since Logos doesn't accept generic-associated-const patterns yet.

---

## Recommended next moves

Sized for single-session work items, in priority order (impact × cost):

1. **Hoist `is_irrefutable` to a single canonical predicate** in `SemaChecker` (or LIR view), and replace the 3-4 ad-hoc sites (`sema_stmt.cpp:990-1003`, `mlir_gen_stmt.cpp:3521-3578`, `mlir_gen_expr.cpp:3877-3879`). Use it to drive both `let` acceptance and `if let` / `while let` "warning if irrefutable". Closes refutability factoring debt; enables generalized `let <pattern> = …` (replacing the LET_DESTRUCT shape gate at `sema_stmt.cpp:1002-1003`). Per `feedback_derive_from_foundation`, this is the foundational refutability primitive — every higher-level pattern check should derive from it.

2. **Chained auto-deref for binding modes.** Replace the per-pattern-kind ad-hoc deref (sema_stmt.cpp:3484-3488, :3881-3895, :5147) with a uniform "peel-and-recurse-while-scrutinee-is-ref-and-pattern-is-non-reference" loop. Closes `&&Some(x)` / `&&&pair` ergonomics gap; foundation for the `&mut`-scrutinee extension Victor flagged as Stage-2.

3. **Generalize fn-params to accept any irrefutable pattern.** Grammar: lift `pat_binding`-restricted forms in `logos.peg:1210-1229` to accept full `pat_single`. Sema: synthesize a `__param_N` of declared type + body-prologue `let <pat> = __param_N;` reusing the LET_PAT path. Closes the "fn-params pattern surface" gap and aligns with `patterns.param` use site.

4. **Introduce `PAT_PATH` + general constants-as-pattern**. Implement structural-equality check per `patterns.const.structural-equality`; route module-const and unqualified-enum-variant uniformly. Retires the ad-hoc bare-ident-as-const-or-binding switcheroo at `sema_stmt.cpp:4214-4611`. Qualified paths (`<u8 as MaxValue>::MAX`) and associated-const patterns become tractable.

5. **Split `PAT_VARIANT_DATA` AST node** into `PAT_TUPLE_STRUCT` and `PAT_STRUCT_VARIANT`; split `PAT_WILD` into `PAT_WILD` (true wildcard, no name) and `PAT_IDENT` (binding). Mechanical AST refactor; eliminates the `current_pat_mut_names_` and bare-ident-vs-variant disambiguation kludges. Naming-only — no semantic change.

6. **Grammar: add `&&pat` form** in `pat_single` (spec `patterns.ref.ref-ref`). One-line grammar addition + chained auto-deref (item 2) closes the double-ref pattern story.
