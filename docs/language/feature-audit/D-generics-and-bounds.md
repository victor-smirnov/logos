# Category D — Generics and bounds (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local `/home/victor/cxx/reference`)

9 features: 5 OK, 3 WARN, 1 GAP (v1: 3 OK, 4 WARN, 1 GAP over 8). The two v1 structural divergences are closed: region inference now consumes named lifetimes (`region_infer.cpp:37-143` allocates a RegionId per declared lifetime, ingests `fn.lifetime_outlives`, special-cases `'static`; `borrow_check.cpp:1432` consults `outlives_named` at return sites — 4c38aed4/239bd7b7/5bccc7fc), and Sized enforcement runs at fn-call type-arg substitution (f895f1f3). v1 audit errors corrected: variance machinery existed pre-v1 (B64 d1987b9c, 2026-05-12) and inferred-const `_` worked pre-v1 (2487f0be) — both were wrongly reported GAP. Remaining real divergences: signature-level lifetime-elision rules (E0106 shape accepted), `where <concrete>: Trait` mis-parsed as a phantom type-param, `use<…>` bound absent, `__const_param:N` magic prefix.

---

## 1. Type parameters — OK

**Rust:** `TypeParam -> IDENTIFIER (':' Bounds?)? ('=' Type)?` (r[items.generics.syntax]).

**Logos:** grammar `type_param` (`tools/peg_gen/grammars/logos.peg:3027-3053`, incl. `CONST_PARAM` + variadic alts), `type_param_list` (`logos.peg:2954`); struct `TypeParam` (`include/logos/compiler/sema.hpp:444`) — `name/bounds/is_variadic/is_const/const_type/default_type/lifetime_outlives/implicit_sized`. Reader: the type-param walker at `sema.cpp:~3600-3990` (bounds, defaults in TYPE slot, variadic-must-be-last check, where-merge).

Interactions: bounds/where/defaults/turbofish/mono all OK; multi-impl selection by self-type shape landed 8c10eb4e (generic overloads, Deref `impls_all_`, mono `__g__` keys); generic-struct move classes fixed in adversarial sweep #2 (00355c52). `is_variadic` = §A6 Logos addition.

**Gap (new, minor):** param-order rule unenforced — `fn f<T, 'a>` accepted (probe compiles+runs); Rust requires lifetimes before type/const params.

---

## 2. Lifetime parameters — OK (was WARN)

**Rust:** `LifetimeParam -> Lifetime (':' LifetimeBounds)?`; outlives `'a: 'b` (r[bound.lifetime]).

**Logos:** grammar `lifetime_param` (`logos.peg:3022`); LIR `LFunction.lifetime_params` + `lifetime_outlives` (collected `sema_decl.cpp` compute_fn_lifetime_outlives; analogues on LStructDef/LEnumDef/LImplBlock).

**✅ closed (4c38aed4 + 239bd7b7 + 5bccc7fc, logos-core §2.1):** `region_infer.cpp` is no longer lifetime-blind — fresh RegionId per declared lifetime (`region_infer.cpp:41-50`), `outlives_pairs_` ingested as point-independent constraints (`:50-62`), `outlives_named(longer, shorter)` with transitive closure + `'static`-top special case (`:102-143`); `borrow_check.cpp:1432` consults it at return-value sites; default trait-object lifetime rule applied (`&dyn Trait` returns tracked like `&T`; owned `Box<dyn>` = `'static`, 239bd7b7). Verification: `tests/logos/fail/core_2_1_dyn_ref_outlives_local.logos`.

**Residual GAP — lifetime elision rules** (`lifetime-elision.md`): signature-level input/output elision (E0106) not implemented. Probe: `fn pick(x: &i32, y: &i32) -> &i32 { x }` compiles (Rust rejects — elided output with 2 input lifetimes). Logos region-infers from the body instead — accepts-more divergence, not in DIVERGENCES §A → GAP. Const/static elision (`&str` → `'static`) OK by construction; trait-object default lifetime OK (above).

---

## 3. Const parameters — WARN

**Rust:** `ConstParam -> 'const' IDENTIFIER ':' Type ('=' …)?`; value namespace; type whitelist {ints, usize/isize, char, bool} (r[items.generics.const]).

**Logos:** grammar `KW_CONST IDENT COLON type_ref` alts (`logos.peg:3040-3045`, `CONST_PARAM` code 133); `TypeParam.is_const/const_type`; expr-position use lowers to magic VarRef `"__const_param:N"` (`sema_expr.cpp:423-431`), mono splices at clone (`mono_clone.cpp:455-473`).

**WARN persists:** `__const_param:N` magic prefix is still the lowering (not a value-namespace binding per r[items.generics.const.namespace]). Type whitelist still unenforced — probe `fn f<const N: f64>()` compiles (decl-only). `WritStatic` const-param type = §A6 addition.

**v1 correction — inferred const `_` is NOT a gap:** `g::<_>([1,2,3])` with `g<const N: i64>(x: [i64; N])` compiles+runs (probe, exit 0); landed pre-v1 (2487f0be, 2026-05-04). Uninferable cases get a clean "could not infer type arg 'N' — supply via turbofish" diagnostic. Float literal as const-ARG (`f::<1.5>`) is a parse error — moot while the whitelist question stands.

---

## 4. Where-clauses — WARN

**Rust:** `WhereClauseItem = LifetimeWhereClauseItem | TypeBoundWhereClauseItem`; subjects may be non-parameters (`where String: PartialEq<T>`) (r[items.generics.where]).

**Logos:** `where_clause <- KW_WHERE where_pred (COMMA where_pred)*` (`logos.peg:1224`); `where_pred <- ref_type COLON trait_bound… / type_param` (`logos.peg:1233` — G158-6 `&T`-subject special case); sema merge in the type-param walker (`sema.cpp:3923-3990`): `&T`/`&mut T` subjects → `on_ref_subject` bounds on the underlying param; `where T: ?Sized` honored (relaxed-bound finalization runs post-merge, `sema.cpp:3913-3914`).

**WARN persists, sharpened:** arbitrary concrete-type subject is now MIS-PARSED, not silently dropped — `where i32: Speak` falls into `type_param`'s IDENT alt, registering a phantom type-param named `i32` ("type param in where clause not in param list — add it", `sema.cpp:3975-3981`); call sites then fail with spurious "could not infer type arg 'i32'" (probe). Wrong diagnostic for a legal Rust form.

Trait default-method where-bounds DO travel as type-expression bounds: `LFunction.where_type_bounds` (`lir.hpp:791`), produced `sema_decl.cpp:2071` (subject = impl trait-arg, e.g. `&T` for `impl Iterator<&T>`), re-gated at mono (`mono_clone.cpp:4793`) — the §8.5 iterator gate (e87c9b95). This is impl-trait-arg-shaped only, not general subjects.

`Self: Sized` method exclusion from dyn vtable: OK (consumed by object-safety, `sema.cpp:2828-2920`, f3f163f6).

---

## 5. Trait bounds — WARN

**Rust:** `Bound -> Lifetime | TraitBound | UseBound`; `TraitBound -> ('?' | ForLifetimes)? TypePath` (r[bound.syntax]).

**Logos:** grammar `trait_bound` (`logos.peg:2975+`); struct `TraitBound` (`sema.hpp:406`) — `trait_name/type_args/lifetime_args/hrtb_binders/assoc_eqs/fn_params/fn_ret/is_fn_family/is_relaxed/on_ref_subject/is_ref_mut`.

Core surface OK: `+`-chains, HRTB binder, `?Sized`, `Trait<Item = U>` assoc-eq, parenthesized fn-family, supertraits, auto-traits (`sema_auto_trait.cpp`; dyn+auto enforcement core 2.4). New since v1: `impl Trait for &T` reference-Self bounds + dispatch (208ee9d3); trait type-arg soundness in bound checking hardened (bcacd8d6 + a1934121).

**GAP persists:** `use<…>` precise-capturing bound absent (no `KW_USE` alt in `trait_bound`; only module `use_decl` at `logos.peg:485-498`). Gates RPIT precise capture (Cat B cross-ref).

---

## 6. HRTB `for<'a>` — OK (was OK-with-gaps)

**Rust:** `ForLifetimes -> 'for' GenericParams` before TraitBound or where-subject (r[bound.higher-ranked]).

**Logos:** `hrtb_binder <- KW_FOR LT hrtb_lt … GT_TYPE` (`logos.peg:2965-2967`); `TraitBound.hrtb_binders`; skolemization at impl resolution (B85, `sema_collect.cpp`).

**✅ closed (ff12df64, logos-core §3.1 + Wave 9 8111af9b):** HRTB instantiation subtyping — fresh universal at binder, unified with caller actual, subtyping consumes the §2 region machinery. ~61 hrtb-* tests (123 files), incl. fail-side `hrtb_conflate_regions`, pass-side `hrtb_two_independent_binders`, `hrtb_tied_bound_indep_impl`, `core_3_1_hrtb_closure_arg`.

**✅ closed — HRTB on bare fn-ptr types:** `fn_ptr_type` now admits `hrtb_binder KW_FN …` alts (`logos.peg:1639-1641`); test `hrtb_fn_ptr_type`. `&dyn for<'a> …` forms also in grammar (`logos.peg:1492-1510`); test `hrtb_dyn_trait_type`.

---

## 7. `Sized` / `?Sized` — OK (was WARN)

**Rust:** implicit `Sized` bound per type param; `?Sized` opt-out (r[bound.sized], special-types-and-traits.md).

**Logos:** `TypeParam.implicit_sized = true` default (`sema.hpp:463`); `?Sized` clears it (`sema.cpp:3668`, post-where finalization `:5560`).

**✅ closed (f895f1f3, logos-core §3.2):** enforcement is end-to-end —
- fn-call type-arg substitution rejects unsized args for `implicit_sized` params with a targeted "requires `Sized` (add `T: ?Sized`)" diagnostic (`sema_expr.cpp:3773-3782`; fail test `core_3_2_qsized_required` — `null_ptr::<[u8]>()`);
- `?Sized` params flip `unsized_ok_` for the matching positions (`sema.cpp:5067-5118`, `sema_expr.cpp:5440-5614`);
- struct/enum gen-arg sites unchanged (already in v1).

**✅ closed — `?Sized` through stdlib `.hm0`:** generic templates ship as ASTs (stash_template, `emit_module.cpp:450-474`) and B3 stage-2b proves the path — `Arc`/`Rc` inherent methods relaxed to `impl<T: ?Sized>` run from the precompiled stdlib (30f1aafc/f1c65cc7, DIVERGENCES §B-record 2026-06-02). B2 custom-DST + B3 `Box<[T]>`/`Box<dyn>` done (DIVERGENCES ~~B2~~/~~B3~~).

Residual (cosmetic): trait-bound satisfaction still treats an explicit `T: Sized` bound as a no-op (`sema_collect.cpp:820-824`) — sound (enforcement happens at substitution sites), comment text stale.

---

## 8. Generic associated types (GATs) — WARN

**Rust:** assoc type with own generics; projection `T::Item<X>`, `<T as Trait>::Item` (associated-items.md).

**Logos:** `ASSOC_TYPE_DEF` (code 119, `logos.peg:931-937`, own TYPE_PARAMS slot); collect reads GAT params (`sema_collect.cpp:2415`); `assoc_type_ref` / `qualified_assoc_type` (`logos.peg:1433-1437`); `Kind::AssocType` + `gat_args`. Structure + projection + mono multi-step Rust-conformant (gat_basic/bounds/projection/dispatch tests; Writ fabric `Datatype::View<S>` uses them in anger).

**✅ closed — GAT × object-safety (5bccc7fc, logos-core §3.3):** `check_trait_object_safe` (`sema.cpp:2837`, called at unsize coercion `:5520`) rejects GAT-carrying traits: "not object-safe … has a generic associated type `Item<T>` — GAT instantiation needs a concrete impl" (`sema.cpp:2854-2870`); fail test `core_3_3_gat_dyn_rejected`. Generic-method + `impl Trait`-param dyn-incompatibility also covered (`:2874-2920`, P2-15 f3f163f6).

**WARN remains:**
- `type Item<T> where T: Copy;` — parse error (probe: "syntax error near '>'"); assoc-type defs take only `COLON trait_bound+` bounds, no WHERE.
- GAT-instantiation equality in generic bodies — known rough edge (fabric.logos:84 workaround).
- Lifetime-GAT (`type Item<'a>`) semantics ride the §2 region machinery only at fn boundaries; projection-position lifetime equality unverified.

---

## 9. Variance & subtyping — OK (NEW; v1 wrongly reported absent)

**Rust:** subtyping.md — per-param variance from field composition; `&T` Co, `&mut T` Co-in-lt/Inv-in-T, `*T` Inv, fn Contra-in-args/Co-in-ret, `UnsafeCell` Inv.

**Logos:** B64 (d1987b9c, 2026-05-12 — pre-v1; v1 audit error): `Variance{BiVar,Co,Contra,Inv}` + meet/compose (`include/logos/compiler/variance.hpp`, `DefVarianceTable` at `:63`); `compute_variances()` fixed-point over structs/datatypes/enums (`sema.cpp:6132-6135`, `variance_in_type` `:7357+`, hardcoded built-in kinds incl. UnsafeCell Inv-in-T `:7415`); variance-aware `subtype(sub, sup, adj, vars)` (`include/logos/compiler/subtype.hpp`) consumed at let-init coercion (`sema_stmt.cpp:1920`) and return-coercion sites. Trait-object type-arg invariance enforced (core 2.3, fail test `core_2_3_traitobj_variance_typearg`).

Scope note: subtype relation fires at coercion sites, not as a pervasive inference relation — adequate for the Rust surface exercised; deep variance×HRTB interplay untested. No `PhantomData`; unused-param warning suggests `_` instead (probe output) — divergent mechanism, records as minor GAP if PhantomData-dependent patterns import.

---

## Cross-category gaps

- **Lifetime-elision signature rules** (§2). E0106 shape accepted; body-based region inference covers soundness (dangling rejected) but signatures aren't abstraction boundaries. Cat A cross-ref.
- **`where <concrete-type>: Trait` phantom-param mis-parse** (§4). Legal Rust form → wrong diagnostic. Grammar-level fix (where_pred needs a general-type alt + sema ledger).
- **`use<…>` bound** (§5) — gates Cat B RPIT precise capture.
- **`__const_param:N` magic prefix** (§3) — value-namespace integration debt.
- **GAT where-clause + generic-body GAT equality** (§8).

## Recommended next moves

1. **where_pred general-subject alt + diagnostic/ledger** — stop the phantom-param mis-parse; either enforce as well-formedness bound or hard-error "bounds on non-parameter types unsupported". 1 session.
2. **GAT `where`-clause on assoc-type defs** — grammar alt + route into existing where-merge. 0.5 session.
3. **Lifetime-elision signature rules** — implement the 3 elision rules + E0106 reject; aligns signatures with Rust abstraction boundaries; region inference already has the named-region substrate. 1 session.
4. **Const-param type whitelist + value-namespace binding** — enforce {int,char,bool} (+ blessed WritStatic), retire `__const_param:` prefix. 1-2 sessions.
5. **Generic-param order rule** (lifetimes first) — trivial parser/sema check. 0.25 session.
