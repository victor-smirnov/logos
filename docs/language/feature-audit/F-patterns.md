# Category F — Patterns (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference local checkout `/home/victor/cxx/reference/src/patterns.md`.

Summary v2: 3 main features — 1 OK (refutability), 2 WARN. Closed since v1: §4.1 canonical refutability predicate (1b7bf7c9), §4.3 chained auto-deref `&&Some(x)` end-to-end (8cc48531 + 96ffd506), §4.5 fn-param struct patterns (59b5d3cc), enum-`&Struct` payload field mis-read (ADV1-A, 955cebbf), slice bindings by-ref `&T` (e87c9b95). Remaining headline gaps (all probed 2026-06-12): 6 grammar-surface parse gaps (`&&pat`, const range bounds, exclusive/half-open char ranges, `S { ref a }` shorthand quals, tuple-index fields `P { 0: a }`, `(..)`); or-pattern binding-consistency unenforced at top-level arm alts (Rust E0408 — soundness-adjacent); Copy payloads under `&` bind by value not `&T`; `let` general-pattern surface still per-shape. New cross-category find: silent `&[i32;N]→&[i64]` elem-type-mismatched coercion (miscompile-class, category B/E).

---

## F.1 Pattern kinds

### Rust nomenclature
Spec enumerates `LiteralPattern`, `IdentifierPattern`, `WildcardPattern`, `RestPattern`, `ReferencePattern`, `StructPattern`, `TupleStructPattern`, `TuplePattern`, `GroupedPattern`, `SlicePattern`, `PathPattern`, `MacroInvocation`, plus `RangePattern`/`OrPattern` at top-alt level (`patterns.syntax`).

### Logos nomenclature
Flat `Code::PAT_*` family, `tools/peg_gen/grammars/logos.peg` (enum block ~:147-303; productions `pattern` :1910, `pat_single` :1966, `pat_single_base` :1996): PAT_VARIANT / PAT_VARIANT_DATA (tuple-struct AND struct-shape variant, `IS_STRUCT_SHAPE` flag), PAT_WILD (bare `_` AND ident binding, disambiguated by NAME), PAT_INT/NEG_INT/BOOL/CHAR/STR/FLOAT/BYTES, PAT_RANGE/PAT_CHAR_RANGE (INCLUSIVE flag; half-open via missing LHS/RHS key), PAT_REF, PAT_TUPLE, PAT_STRUCT, PAT_SLICE, PAT_UNIT, PAT_AT, PAT_FIELD, PAT_REST, PAT_OR, PAT_HERMES_* (Logos addition). Lowering: `SemaChecker::build_pattern` `src/compiler/sema_stmt.cpp:2851`, `build_pattern_impl` :3956, `build_pattern_variant_data` :2922, `build_pattern_or` :3872.

### Match verdict
**WARN — coverage broad, surface naming debt + 6 probed parse gaps.**

AST-naming debt unchanged from v1: PAT_VARIANT_DATA serves both TupleStructPattern and StructPattern(variant); PAT_WILD overloads wildcard+ident (root of `current_pat_mut_names_` side-channel, `sema_impl.hpp:3837`); no PAT_PATH / PAT_GROUP nodes (const-as-pattern is the PAT_WILD-fallback detection `sema_stmt.cpp:4767-4807`; grouped patterns inline to PAT_OR at parse, `logos.peg` "Parenthesised (grouping) pattern" alt — works, probed `n @ (1|2)` analogues green).

Verified working (probes 2026-06-12, all exit 0): tuple rest `(1, .., z)` (P4-pm-20); 1-tuple `(x,)` (4c01c1bc); grouped `(P|Q)`; inclusive+exclusive+half-open int ranges `1..=5`/`lo..hi`/`1..` (3097f848, bc07b92b); nonempty-range validation (`sema_stmt.cpp:4112`, :4380 — `patterns.range.constraint-nonempty` ✓); `e @ 1..=5`, `ref x @ pat` (536d55fb), `mut x`; struct longhand `a: ref ra`; union pattern exactly-one-field + `unsafe` gate (§6.1, `sema_stmt.cpp:4617-4640` — `patterns.struct.constraint-union` ✓); nested payload bindings `Some(Inner::A(x))` (2599c5ee — DIVERGENCES "payload-binding inners pending" note is STALE); dynamic-slice patterns incl. `[f, .., l]`, `[h, t @ ..]` with by-ref `&T` element bindings (`*x` derefs; adv1_slice_reborrow_pattern pins it).

Parse GAPS (each probed → syntax error):
1. `&&pat` (`patterns.ref.ref-ref`) — `pat_single` has only AMP alts; the `AND`-token route exists at *type* position (`logos.peg:1595-1601` DOUBLE_REF_TYPE) but not in patterns. Whitespace `& &pat` works.
2. Range bounds as paths (`patterns.range.bound`/`constraint-bound-path`): `LO..=HI` over module consts — bounds are INTEGER/CHAR_LIT literals only.
3. Char ranges: only `CHAR_LIT DOTDOTEQ CHAR_LIT`; exclusive `'m'..'p'` and half-open char forms absent.
4. Struct-pattern shorthand qualifiers `S { ref a }` / `S { mut b }` (`patterns.struct.binding-shorthand`) — `pat_field` (:1914) has only `IDENT COLON pat_single` / bare `IDENT`. Longhand `a: ref x` works.
5. Tuple-index struct-pattern fields `P { 0: a }` (StructPatternField TUPLE_INDEX; also the `patterns.tuple-struct.namespace` nuance) — absent.
6. `(..)` whole-tuple rest form (`patterns.tuple.rest-syntax`) — absent.

Other deltas: `[1.., _]` ACCEPTED (Rust requires `(1..)` parens, `patterns.range.constraint-slice`) — more-permissive, minor. PAT_FLOAT parses, sema rejects (`sema_stmt.cpp:3959-3967`, IEEE decision pending — Rust allows w/ lint ⇒ GAP-minor, not blessed). PAT_BYTES = `[u8;N]` scrutinee only (`build_pattern_bytes` :3776; dynamic `&[u8]` future). PAT_STR position-limited: whole arm / variant payload / tuple elem ✓, slice elem rejected cleanly (G172-1b, :3970-3982). `&[a, b]` ref-slice pattern rejected ("reference pattern requires reference scrutinee, got '&[i64]'") — Logos Slice kind IS the one-level `&[T]`; surface stays `[a, b]` (B5 note). No MacroInvocation in pattern position. `patterns.ident.constraint` (ref-shadows-const error) unenforced (minor).

### Interactions check
- **Match — OK.** Exhaustiveness: `check_match_exhaustiveness` `sema_stmt.cpp:6991` (guarded arms skipped, uninhabited short-circuit) + `ast_patterns_exhaustive` :7343 (nested variants). §4.2 ✅ — pinned by core_4_2 pass+fail tests.
- **Let — WARN.** `LET_PAT` (`lower_let_pat` :1018) accepts PAT_STRUCT, tuple-struct, fixed-array PAT_SLICE (no rest), single-variant struct-shape enum; everything else → "supports struct patterns only" (:1093). Probed: `let &x = &n;` rejected — Rust allows any irrefutable pattern ⇒ GAP. LET_DESTRUCT handles tuple/nested-tuple.
- **Fn-params — WARN (was GAP).** §4.5 ✅ (59b5d3cc): `pat_param` grammar (`logos.peg:1331-1345`) + collect_fn synth `__pat_param_N` + body-prologue (`sema_decl.cpp:578-614`, :873-915). Probed `fn f(Point { x, y }: Point)` green; rename form too. PAT_SLICE parses, prologue TODO (`sema_decl.cpp:915`). Tuple params (P4-pm-19) work. Other shapes (ref, ident@) absent. Refutable shapes rejected via §4.1 predicate.
- **if let / while let / for — OK.** Nested payload patterns in if-let/while-let landed (ece792fc).
- **Const patterns — GAP (partial).** Scalar (int/bool/char) module consts as whole patterns ✓ (P4-pm-06, :4767-4807; core_4_4 test); str + `[u8;N]` consts via `current_pat_refutable_guards_` channel (top-level positions only). Absent: struct/enum aggregate consts (`patterns.const.structural-equality`), associated/qualified-path consts, consts as range bounds (parse gap #2), metacall-result-as-pattern unexercised. logos-core row "4.4 ✅ PAT_PATH" OVERSTATES — no PAT_PATH node exists; scalar-only.
- **Or-patterns — GAP (soundness-adjacent).** Nested alts in payload/tuple positions probed green (`Some(1|2)`, `(1|2, y)`); binding-consistency enforced in `build_pattern_or` (:3947) and let-else (:1613), but NOT for top-level arm alternations: `match o { Some(x) | None => … }` COMPILES (probe; Rust E0408). Non-binding alt leaves `x` undefined-garbage if used. `patterns.constraints.pattern` type-unification across alts also unchecked.

### Gaps / debt
- Grammar batch: parse gaps 1-6 above (each one-production); `&&pat` needs the AND-token route mirrored from ref_type.
- Top-level arm alternation: route through (or replicate) the `build_pattern_or` same-names+types check.
- PAT_PATH node + structural-equality const translation (`patterns.const.translation`); retire the PAT_WILD switcheroo.
- Split PAT_VARIANT_DATA → PAT_TUPLE_STRUCT/PAT_STRUCT_VARIANT; PAT_WILD → PAT_WILD/PAT_IDENT (naming-only, unblocks side-channel removal).
- Generalize `let`/fn-param onto one `is_irrefutable_pattern`-gated driver (incl. fn-param PAT_SLICE prologue, `let &x`).
- B-pt-03 residual: dynamic `&[u8]` byte-string scrutinee; float-pattern decision.

---

## F.2 Refutability

### Rust nomenclature
`patterns.refutable`; per-kind refutability tags (`patterns.literal.refutable` … `patterns.path.refutable`). `match` any; `let`/params irrefutable; `if let`/`while let` refutable.

### Logos nomenclature & verdict
**OK — ✅ closed since v1 (1b7bf7c9, Wave 9 2026-05-31).** Single canonical predicate `lir_view::is_irrefutable_pattern(PatRef)` at `include/logos/compiler/lir_view.hpp:1754` (recurses Wild/RefBind/RefPat/At/Tuple/Struct/Slice/Or). The two former drifting lambdas are one-line wrappers: `mlir_gen_stmt.cpp:3735-3740`, `mlir_gen_expr.cpp:4119-4126`. v1's third site (let-destruct shape gate, now `sema_stmt.cpp:1093`) is per-shape *lowering dispatch*, not a refutability predicate — documented in the foundation header; the user-facing generality gap is tracked under F.1 "Let — WARN".

Slice refutability matches `patterns.slice.refutable-slice` (single `..`/`ident @ ..` rest = irrefutable for slices; length-refutable otherwise) — predicate Slice arm verified in lir_view.hpp:1786-1799. Refutable sub-patterns inside struct/variant payloads still route through `current_pat_refutable_guards_` (`sema_impl.hpp:3844`) guard synthesis — works (refutable literal fields :4560-4591), conflation noted in v1 stands but is contained.

### Residuals
- No `irrefutable_let_patterns` lint analogue (`if let _ = e` compiles silently) — Rust warns; lint-tier, minor.
- Exhaustiveness of guarded arms = Rust-aligned (guarded arm never counts, unguarded fallback required — core_4_2 piece 2).

---

## F.3 Binding modes / default binding modes

### Rust nomenclature
`patterns.ident.binding` (RFC 2005): default mode move; `&`→`ref`, `&mut`→`ref mut`, nested-references repeat; 2024-edition mode-limitations.

### Match verdict
**WARN — core ergonomics landed (incl. `&mut` + N-deep chains); binding-TYPE divergence for Copy/TypeVar payloads remains.**

✅ Closed since v1:
- **Chained auto-deref §4.3 (8cc48531; disc-test ptr/i64 bug 96ffd506).** `pat_scrut_ref_depth` counts peeled layers (`sema_stmt.cpp:3084-3096`); N-wrap of binding types (:3758-3766, outermost takes strictest mut); codegen `enum_ref_depth` loads + `ref_bind_depth` chained temps. Probed: `match &&o { Some(x) => *(*x) }` → exit 0 (depth-2 extraction); core_4_3 test pins depth-3.
- **`&mut` scrutinee default-`ref mut`** (6da9ef87 — v1 text was stale, this landed 2026-05-24 for concrete payloads): probe `match &mut m { Some(v) => { *v = 7 } }` writes back ✓. Copy payloads under `&mut` also auto-`&mut`-bound (`|| default_mut`, :3750).
- **Slice-element by-ref bindings** (`&[T]` scrutinee → `&T` bindings, `*x` derefs; e87c9b95/a6c71b4c iterator work + ADV1-G `&a[..]` fix).
- **Mutability-match on reference patterns** (`patterns.ref.mut`): `match &n { &mut x => }` → clean "'&mut' requires '&mut' scrutinee" (probed).

Remaining gaps:
- **Copy payloads under shared `&` bind BY VALUE** (gate `is_move_type(binding_types[k]) || default_mut`, `sema_stmt.cpp:3748-3750`; same gate tuple :4312, struct :5441). Probed: `match &Some(3) { Some(y) => want_ref(y) }` → "expected &i64, got i32". Rust binds `&T`. Planned follow-up in-code ("drop the is_move_type gate", :3717-3721). Mitigation: bare `*y` on the by-value binding is leniently accepted, so many ports run.
- **TypeVar payloads never default-ref'd** (self-ref guard :3744-3747, prevents `OptionIter<&mut … T>` mono blow-up) — generic bodies need explicit `ref`; un-Rust, needs the principled guard.
- **Tuple/struct patterns peel ONE `&` level** (:4139-4147, :5423-5427) vs enum's N-level — `&&(T,U)` tuple-pattern ergonomics untested/likely gap.
- **2024-edition mode-limitations unenforced** (`mut`/`ref`/`ref mut`/reference-pattern only in move mode) — diagnostic-tier.
- `&&pat` grammar gap (F.1 #1) caps explicit reference-pattern depth at whitespace forms.

---

## Cross-category gaps

- **NEW — B/E (probed 2026-06-12, miscompile-class):** untyped `[1,2,3,4]` defaults `[i32;4]`; `let s: &[i64] = &arr;` is SILENTLY ACCEPTED → element reads see garbage upper bits (probe: `s[0] as i32 == 1` but `s[0] != 1i64`). Elem-type check missing at unsize-coercion site. Surfaced via slice-pattern probes; patterns themselves exonerated.
- **B5 residual (re-confirmed):** named slice bindings nested in tuple `(2, [a, b])` → clean "undefined variable 'a'/'b'" (probe). Length-discrimination works.
- **ADV1-A closed (955cebbf):** enum-variant `&Struct` payload field access `V(p) => p.x` — was 4 drifting payload-binding loops; unified onto `bind_enum_payload`. Probe green. (ref_enum_niche memo's "pre-existing bug" note now stale.)
- **DIVERGENCES.md staleness:** B4 row still lists 1-tuple `(z,)` (fixed 4c01c1bc), `ref _y @ Pat` (536d55fb); "Recently caught up" says payload-binding inners `Some(Inner(x))` pending (fixed 2599c5ee). logos-core §4.4 row overstates (no PAT_PATH node; scalar consts only; const range bounds don't parse).
- **C (const items) ↔ F:** metacall-result-as-pattern still unexercised (A1 intersection).
- **D (generics) ↔ F:** `patterns.const.generic` non-issue (no assoc-const patterns yet); TypeVar default-ref guard (F.3) is the real generics×patterns friction.

---

## Recommended next moves

1. **Fix the silent `&[i32;N]→&[i64]` coercion** (B/E miscompile, one elem-type equality check at the unsize site). Smallest fix, highest severity.
2. **Or-pattern binding-consistency at top-level arm alternations** — apply the `build_pattern_or` :3947 same-names+types check when lower_match expands arm alts; soundness-adjacent (undefined binding in non-binding alt).
3. **Drop the `is_move_type` gate** (3 sites: :3748, :4312, :5441) so Copy payloads bind `&T` per RFC 2005; pairs with `&T`-operator auto-deref already landed for slice bindings. Then N-level peel for tuple/struct patterns (mirror enum's `pat_scrut_ref_depth`).
4. **Grammar surface batch (6 parse gaps):** `&&pat` (AND-token route), const range bounds, exclusive/half-open char ranges, `S { ref a }` shorthand quals, `P { 0: a }` tuple-index fields, `(..)`.
5. **Generalize `let` + fn-params onto the §4.1 predicate** — one driver for any irrefutable pattern (`let &x`, fn-param PAT_SLICE prologue, ident@/ref shapes); retire the :1093 shape gate.
6. **PAT_PATH node + structural-equality const patterns** — aggregate consts, assoc/qualified paths; unifies with range-bound-paths (move 4's item there).
