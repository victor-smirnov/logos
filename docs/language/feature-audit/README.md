# Logos Feature Audit — Index

**v2 — re-audited 2026-06-12** (v1 generated 2026-05-30). Spec basis: rust-lang/reference (local checkout at `/home/victor/cxx/reference`).

Per-category audits comparing the Logos compiler/stdlib against the Rust Language Reference, feature-by-feature. Each report enumerates feature naming, implementation pointers (file:line), neighbour interactions, gaps/debt, and single-session work items. v2 re-verified every v1 verdict against HEAD (00355c52, 356 commits past v1) with ~100 targeted compile/run probes; v1-missed spec chapters (primary expressions, names/scopes, linkage, runtime, parenthesized types) swept in.

## Table of contents

- [A — Ownership](A-ownership.md) — Move, Copy, Drop/RAII, Borrow, Lifetimes, Reborrow, Variance, temporaries.
- [B — Type system primitives](B-type-system-primitives.md) — primitives, Never, Tuple, Array, Slice, str, raw pointer, fn-item, fn-pointer, Closure, TraitObject, ImplTrait, Inferred `_`, layout/`repr`, coercions, DST.
- [C — Items](C-items.md) — fn, struct, enum, union, const/static, type alias, trait, impl, module, use, extern block, associated items.
- [D — Generics and bounds](D-generics-and-bounds.md) — type params, lifetime params, const params, where-clauses, trait bounds, HRTB, `Sized`/`?Sized`, GATs, variance.
- [E — Expressions and control flow](E-expressions-and-control-flow.md) — let, block, if/if let, match, loops, closure, `?`, async/await, return, field/method/call, operator overloading, range, cast, primary expressions.
- [F — Patterns](F-patterns.md) — pattern kinds, refutability, binding modes.
- [G — Memory and safety](G-memory-and-safety.md) — interior mutability, memory model/atomics, variables, statics.
- [H — Concurrency](H-concurrency.md) — Send/Sync, async fn / async block, Pin/Unpin.
- [I — Modules, names, visibility](I-modules-names-visibility.md) — paths, visibility/privacy, name resolution/preludes/namespaces.
- [J — Macros and metaprogramming](J-macros-and-metaprogramming.md) — `macro_rules!` analogue, procedural macros analogue.
- [K — Unsafe](K-unsafe.md) — `unsafe` surface, UB list.
- [L — Attributes](L-attributes.md) — built-in attributes, `#[cfg]` / `cfg!`.
- [M — Const evaluation](M-const-evaluation.md) — const expressions / `const fn` / `const { }`.
- [N — FFI, linkage, ABI](N-ffi-linkage-abi.md) — `extern "ABI" fn` / blocks, inline assembly, linkage model.
- [O — Other (Panic, Divergence)](O-other-panic-divergence.md) — Panic, Never `!` / divergence.

## Aggregate verdict counts (v2, with v1 for delta)

Counts are per audited feature. v2 totals grew (78 → 122) because the re-sweep added spec surface v1 missed; per-feature comparisons are in the chapters. BLESSED = formally registered DIVERGENCES §A rows (async A4), excluded from OK/WARN/GAP.

| Category | OK | WARN | GAP | BLESSED | Total | v1 (OK/WARN/GAP) |
|----------|----|------|-----|---------|-------|------------------|
| A — Ownership | 5 | 2 | 1 | — | 8 | 4/2/1 |
| B — Type primitives | 14 | 3 | 0 | — | 17 | 10/4/2 |
| C — Items | 8 | 4 | 0 | — | 12 | 6/4/2 |
| D — Generics / bounds | 5 | 4 | 1 | — | 10 | 3/4/1 |
| E — Expressions / CF | 15 | 4 | 1 | 1 | 21 | 6/5/2 |
| F — Patterns | 5 | 7 | 2 | — | 14 | 0/3/0 |
| G — Memory / safety | 4 | 8 | 3 | — | 15 | 0/2/1 |
| H — Concurrency | 3 | 4 | 2 | 1 | 10 | 0/1/1 |
| I — Modules / names | 0 | 3 | 0 | — | 3 | 0/3/0 |
| J — Macros / metaprog | 0 | 2 | 0 | — | 2 | 0/2/0 |
| K — Unsafe | 0 | 2 | 0 | — | 2 | 0/2/0 |
| L — Attributes | 0 | 2 | 0 | — | 2 | 0/2/0 |
| M — Const evaluation | 0 | 1 | 0 | — | 1 | 0/1/0 |
| N — FFI / linkage / ABI | 0 | 2 | 1 | — | 3 | 0/1/1 |
| O — Panic / divergence | 0 | 2 | 0 | — | 2 | 0/2/0 |
| **Total** | **59** | **50** | **11** | **2** | **122** | **29/38/11** |

OK 48% (v1 37%), WARN 41% (49%), GAP 9% (14%). The 37-item `logos-core.md` catalog absorbed v1's Tier-1–4 backlog: of v1's 44 ranked moves, ~34 are landed and verified (region inference, UnsafeCell, auto-traits, object safety, exhaustiveness, definite assignment, union, static split, extern blocks, cfg combinators, all 8 derives, Try-`?`, Range generics, DerefMut autoderef, atomics Ordering, never-macros, FnItem, `_` inference, repr-minimal, slice mutability, …). The v2 sweep's net new content: **6 crash/miscompile-grade findings, 8 soundness holes, and 8 scoreboard-vs-reality corrections** — almost all in surface the catalog never claimed or in residuals its ✅ rows under-stated.

## v1 → v2 corrections (audit errors, both directions)

v1 false-negatives (feature existed at v1 time, reported GAP/WARN): variance machinery B64 (`d1987b9c`), inferred const-param `_` (`2487f0be`), UFCS expr-position `<T as Trait>::m()` (`2e57b1e5`), let-else divergence assertion (`70d4a671`), integer-overflow trap (`b0bc3eb5`). v1 false-positive: plain `&&` in match guards was never a gap (guard slot is a full expr); the real gap is `if let` in guards (still open).

## Top cross-category findings (v2)

Ordered by severity; (cat) = chapters carrying detail.

1. **Statics have no storage class — miscompiles, not diagnostics** (C,G,K,M,N). Every static lowers to const-inline / per-use fresh alloca; no `llvm.mlir.global` is ever emitted. Probe-confirmed: cross-fn `static mut` write→read **segfaults** (S25); single-fn read-before-first-write returns **garbage**; `static ATOMIC` mutation hits an inlined copy (fetch_add in fn A invisible in main); extern statics fold to `CONST_DEF` (reads not unsafe-gated, linkage presumed broken). One fix — real GlobalOp + addressof — closes all five.
2. **Compiler crashes (ICE) on legal-or-near-legal input** (B,O,A): (a) `fn f(x: _)` → logosc SIGSEGV, no diagnostic; (b) `unreachable!()`/`todo!()` in match-arm position → SIGSEGV (if-arm fine; `panic!("…")` in same position types `void` — same root: format-family sema-inline doesn't carry `Never` through the match-arm unifier); (c) `let r = &String::from("x")` (spec temporary-lifetime-extension) → mlir-gen module-verification failure.
3. **Silent wrong-code coercion**: `let s: &[i64] = &[1i32,2,3,4]`-shape accepted — unsize coercion never checks element type; garbage upper-bit reads (F,B).
4. **`#[cfg]`-false items are dropped from collection but still lowered** — two same-named fns gated `cfg(unix)`/`cfg(windows)` (the canonical platform switch) die with "duplicate function body" (L). Also `cfg(true)`/`cfg(false)` literal predicates unrecognized.
5. **Closure auto-trait propagation checks parameter types, not captures** — closure capturing `*mut i32` passes `T: Send`; v1's too-strict conservative `false` became too-permissive (H). logos-core §2.4(a) claim is wrong.
6. **Or-pattern binding consistency unenforced at top-level arm alternations** — `Some(x) | None => x` compiles (Rust E0408); the check exists (`sema_stmt.cpp:3947`) but only fires nested (F).
7. **Enum + trait visibility entirely unenforced cross-package** (`lookup_qualified_<false>` for `enums_`/`traits_`; `SemaTraitInfo` lacks `is_pub`) — non-pub enum constructs/matches from another package silently (I).
8. **Partial-move tracking is one-level**: `o.i.s` move-out then re-read accepted (missed E0382) AND sibling `o.i.t` leaks (whole-field drop suppression, valgrind-confirmed) (A). Related: `impl Copy` + `impl Drop` coexistence accepted (E0184, double-drop hazard).
9. **Atomics: per-variant Ordering is literal-site-only** — the safe `*_ordered` methods pass a runtime VarRef, always fall back to seq_cst (sound on x86, claim in §6.14 over-broad); `AtomicUsize`/`AtomicIsize` still absent (G).
10. **Operator-dispatch debts**: unary `!` unusable (`impl Not` can't bind — sema dispatches `not_`, trait declares `not`; zero coverage); `Eq`/`Ord`↔`PartialEq`/`PartialOrd` inversion persists; `..`/`..=` still construct `RangeI32`/`RangeI64`, not the landed generic `RangeOf<T>` family; `RangeToInclusive` end+1 observable (E).
11. **`where <concrete-type>: Trait` mis-parses** into a phantom type-param named after the concrete type → spurious "could not infer type arg 'i32'" (D). Sibling parse gaps: GAT where-clauses, `&&pat`, const range-bounds `LO..=HI`, exclusive char ranges, `S { ref a }`, `S { 0: a }`, `(..)`, `fn g() -> _` accepted (E0121 absent).
12. **Trait-default assoc consts not inherited** — `impl Tr for S {}` then `S::C` / `Tri::SIDES` unresolved (grammar landed `1b8ff07e`; projection fallback trait→impl missing) (C,M).

## Scoreboard / register corrections needed (tracking-doc hygiene)

`logos-core.md` "37/37 ✅" needs five row-level corrections: §2.4(a) closure-captures claim false (see #5); §6.9 over-states (K10-co-06 canonical repro `[i64; metacall{N}]` still fails — resolver not wired at the 3 type-position CTFE sites); §6.14 method-path Ordering claim false (see #9); §6.2/§6.11 ✅ rows carry live crashes in their deferred residuals (S25; match-arm ICE); §1.4 plan-table row keeps stale "DEFERRED" text vs closed §-body. `DIVERGENCES.md`: §A still lacks the package/`mod` + dotted-path + `use…as` model row (v1 move #41, twice flagged, never executed) and the `dyn Fn*`→`Kind::Closure` collapse + `I24/U24/I56/U56` rows; B4 lists already-fixed items; B6 "pool-UID split" contradicts landed code. `undefined-behavior.md` is stale in 3 entries (transmute "rejected" — no transmute exists; Ordering "discarded" — threaded since `2d145bf4`; overflow "wraps" — traps since `b0bc3eb5`) and its anchor IDs drifted from the spec.

## Recommended next moves (ranked, deduplicated)

### Tier 0 — crashes & miscompiles (fix-the-class, now) — ✅ ALL DONE 2026-06-12

1. ✅ `7ea97718` Static storage class: one `llvm.mlir.global` per `static`/`static mut` + addressof routing — closed S25 segfault, read-before-write garbage, atomic-in-static, `&STATIC` identity (C,G,K,M,N).
2. ✅ `f97332e7` Match-arm `Never`-typing: `__fmt_panic -> !` + divergent blocks type `never_t()` (was `error_t()`) + null-guarded the match/if IntLit-upgrade `.result()` deref (O).
3. ✅ `bf497612` `_`-in-item-signature E0121: one chokepoint in `resolve_type` under an `in_item_signature_` RAII guard set at all signature-collection sites; nested `Vec<_>`/`&_` caught by resolve_type's own recursion (B).
4. ✅ `c9dc5a5a` Temporary-lifetime-extension: EAddrOfTemp spills by-value aggregates; `let r = &[mut] <rvalue>` synthesizes a named temp (scope-end DROP — the old spill leaked); also fixed pre-existing mut-`&Struct`-rebind pointee corruption (A).
5. ✅ `13bccdd1` Slice elem-type check: concrete scalar elems must match exactly in `types_compatible`; literal arrays adopt the annotated/formal elem width (lower_call slice-formal hint joins closure/enum/tuple hints) (F,B).
6. ✅ `a0304a36` cfg gates EVERY walk via shared `cfg_attrs_drop_item` (pass-0 pre-registration + phase collection + lowering); `cfg(true/false)` literals in attribute + `cfg!()` forms (L).

### Tier 1 — soundness — ✅ ALL DONE 2026-06-12

7. ✅ `1b7b0cda` Closure auto-traits walk CAPTURE types (closure_capture_env_ per interned type, union across same-sig literals; by-ref as `&[mut] T`); String/Vec/Box got the missing `unsafe impl Send/Sync` (H).
8. ✅ `a68581ee` Or-pattern E0408 at top-level arm alternations — AST-level check at both eff_arms fan-outs (F).
9. ✅ `2b64fa97` Enum/trait visibility: `is_pub` on SemaEnumInfo/SemaTraitInfo; checked lookup for enums; explicit check at collect_impl for traits (introspective callers stay uncheck) (I).
10. ✅ `a4645cd0` Dotted-path partial-move granularity (B78): full-path moved_fields with overlap relation in borrow_check (raw-ptr hops untracked); path-aware SDrop/gen_drop_value walk (siblings drop, moved leaves suppressed); field reinit lifts suppression. NEW pre-existing finding registered: live-field overwrite (`i.s = new`, no prior move) leaks the old value — field-level drop-before-replace (A).
11. ✅ `f0ff8424` E0184: `impl Copy`+`impl Drop` rejected (post-collect sweep); un-Rust pass/copy_drop retired (A).
12. ✅ `4914c6f0` `dyn Trait + Auto` enforced at let/return/tail-return/field-init coercion sites (H,B).
13. ✅ `658f3760` Extern-static access gating (module_extern_statics_) + `unsafe extern { }` 2024 form (K,N).

Bonus (same session): `_`-hole pinning fix (31eb8759 — `let v: Vec<_> = vec![…]` ICE class), tail-return variance gate (86eebe44), fn-macros in if/while conditions (54011185 — the cfg! residual).

Tier-1.5 (found-by-the-way, 9d2d29e4): **field-level drop-before-replace** — `o.s = new` over a live droppable field dropped the old value (SDerefWrite got a `drop_old` flag mirroring SAssign; plumbed through mono-clone too). Remaining: `&mut`/`*mut`-root field reassign + B8 declared-uninit whole-var leak (both registered).

### Tier 2 — high-impact parity / surface

14. ✅ `3be40f91` CTFE type-position COMPLETE — trait-default assoc-const (17e7f97c) + const PATH folding at array-length/fill/disc via shared ctfe_eval_const (M,C).
15. ✅ `f0bb96bf` `Not` dispatch fix (`not_`→`not`, sema + mono) + `impl Not` test (E).
16. ✅ `1e537517` Comparison operators → PartialOrd `partial_cmp` dispatch (`a<b` ≡ `a.partial_cmp(&b).is_lt()`); direct `lt` still wins (E,C,F).
17. ✅ `308d94e3` Inclusive range VALUE → generic `RangeOfIncl<T>` (real end, no hi+1 overflow/observability); `for` counter loop unaffected (E).
18. ✅ `4250bfa8` where-clause COMPLETE — concrete-type subject = obligation (no phantom param) + GAT/assoc-projection subject (`C::Item<T>: Bound`) parses & accepts (D).
19. Lifetime-elision signature rules + E0106 reject (named-region substrate ready) (D,A).
20. ◑ `use pkg.{a,b}` already works (USE_VARIANTS lowercase desugar); `use … as Alias` is DESIGN-GATED (Logos path-model decision, flagged in DIVERGENCES) — deferred (I,C).
21. ✅ `52791272` `matches!` + `dbg!` builtins (assert family already shipped) (J).
22. `quote_expr!` antiquot Ident-at-type/str-position — unblocks Debug/Default/PartialOrd derive parity in one fix (J).
23. ABI string → MLIR calling-convention threading + ABI tag on `Kind::FnPtr` + extern-fn-ptr type grammar (N,B).
24. ◑ `69356297` `AtomicUsize`/`AtomicIsize` DONE; Ordering const-prop through `*_ordered` wrappers remains (G,H).
25. ✅ `95bbd1e2` Deferred init of non-mut local (`let x: i32; x = 5;`) (G).
26. ✅ `e5c26764`+`375f04d9`+`974f7049` **is_move_type gate DROPPED + depth-N (full RFC-2005)**: under a `&`/`&mut` scrutinee EVERY payload binds by-reference — Copy AND reference payloads (`&T`⟹`&&T`, Rust-exact). `&T` operands auto-deref in arithmetic (`lower_binop`), `as`-casts (`lower_cast`), refutable range-guards (`synth_refutable_inner` via `pat_scrut_by_ref`); depth-N field (`lower_field_read`) + method (`lower_method_call`) autoderef peel extra ref layers so `&&P` field/method access works. Arithmetic stays single-level (Rust's `&i32` impls). By-value Copy uses (`return x`) require `*x`/`**x` — rest_pattern migrated. Raw-ptr payloads participate too (`*T`⟹`&*T`, `8e8782f9`). LAST carve-out removed (`77b9b75e`): bare-TypeVar payloads bind `&T` in generic bodies (the `OptionIter<&mut…T>` blow-up was `Option::take`/`replace` rebuilding `Some(v)` from `&mut T` — fixed to value-level `mem::replace` moves, not inherent to TypeVar binding). RESIDUAL: indexing `&&[T;N]` still a compile-error (array→slice decay codegen, dedicated follow-up). Tests ref_payload_depth2_autoderef, ref_payload_raw_ptr_binding, generic_match_ergonomics_typevar (F).
27. ✅ `b474a15a` Pattern parse batch COMPLETE — `(..)`, `S { ref a }`/`ref mut` (write-back), `&&pat`, `S { 0: a }`; ranges already worked (F).
28. ◑ `5e9203ad` Fully-qualified dotted+`::` path — **Increment 1 DONE**: package-qualified free-fn calls `pkg.path::fn(args)`/`::fn::<T>(args)` (exact-package resolution disambiguates same-named free fns across pkgs, e.g. `logos.lang.mem::replace_ref` vs `logos.lang.ptr::replace`). REMAINING: `pkg.path.Type::method`/`::CONST`/`::Variant` (type-member), type-position (`let x: pkg.Type`), UFCS trait-qualifier `<T as pkg.Trait>::m`. Mechanism: grammar dotted-prefix alts + QUAL_PARTS field + sema `call_pkg_qualifier_` filter (I,E).
29. ✅ `a83e6ed5` Uninhabited-variant arm elision in exhaustiveness (O).
30. Nested `#(…)*` repetition in quote templates (`sema_expr.cpp:14822,15860`) (J).

### Tier 3 — documentation / register hygiene (single pass)

31. `DIVERGENCES.md`: add §A rows (package/path model + `use…as`; `dyn Fn*`→Closure collapse; extra int widths; `Void`); prune stale B4/B6 lines.
32. `undefined-behavior.md`: sync 3 stale entries; re-anchor IDs to spec.
33. `logos-core.md`: correct §2.4(a)/§6.9/§6.14 rows; surface S25 + match-arm-ICE residuals at the scoreboard level; refresh §1.4 plan row.
34. Stale stdlib comments: `cell.logos` header ("no compiler magic"), `marker.logos` Pin note; `enum Never` → `Infallible` rename (O,G).

---

For per-feature detail (Rust-spec citation, exact Logos file:line, probe transcripts, interaction edges), open the corresponding category report linked in the [Table of contents](#table-of-contents).
