# Category D — Generics and bounds (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

8 features audited: 3 OK, 4 WARN, 1 GAP. Logos has structurally complete type/lifetime/const-param grammar with where-clauses, trait bounds, HRTB binders, `?Sized`, and a working GAT spine (associated types with their own `type_params`, projection via `T::Item<X>`). Two real divergences: (1) lifetimes parse and outlives-propagate but `region_infer.cpp` does not consume them as scopes — region inference is CFG-driven over locals, not over named-lifetime regions; (2) `?Sized` opt-out flag (`implicit_sized=false`) only flips at struct/enum generic boundaries (`sema.cpp:4654-4675`), and the comment at `sema_collect.cpp:743-747` admits Sized enforcement is partial. Const params are a Logos-specific magic-name shape (`__const_param:N`) rather than a value-namespace identifier — works for monomorphization but blurs the type-vs-value distinction Rust draws (`items.generics.const.namespace`).

---

## 1. Type parameters

**Rust nomenclature:** `TypeParam` — `IDENTIFIER ( ':' Bounds? )? ( '=' Type )?` inside `GenericParams` (`reference/src/items/generics.md` r[items.generics.syntax]).

**Logos nomenclature:** Grammar production `type_param` (`tools/peg_gen/grammars/logos.peg:2863-2897`); AST node `TYPE_PARAM` (slot 90, `grammars/logos.peg:153`); list `type_param_list` (`grammars/logos.peg:2790`); LIR/sema struct `TypeParam` (`include/logos/compiler/sema.hpp:376-396`) with fields `name`, `bounds`, `is_variadic`, `is_const`, `const_type`, `default_type`, `lifetime_outlives`, `implicit_sized`. AST slot `TYPE_PARAMS` (slot 27, `grammars/logos.peg:47`). Reader `read_type_params` (used pervasively, e.g. `src/compiler/sema_decl.cpp:257,438,1136`).

**Match verdict:** OK on name (`type_param` ≈ Rust `TypeParam`, `TypeParam` struct ≈ Rust internal). Logos extends with `is_variadic` (`T...`) — a Logos addition (§A6).

**Implementation pointer:** parse `tools/peg_gen/grammars/logos.peg:2863`; collect `src/compiler/sema.cpp:3414` (the `read_type_params_inner`-style walker, with CONST_PARAM branch). Substitution `src/compiler/mono_subst.cpp` (subst_type_sema).

**Interactions check:**
- Trait bounds — OK. `IDENT COLON trait_bound (PLUS trait_bound)*` (`grammars/logos.peg:2894`); stored in `TypeParam.bounds`.
- Lifetime params — OK. Mixed-order admitted by `type_param` PEG alts; lifetime params are LIFETIME_PARAM siblings inside the same list.
- Const params — OK. Grammar alt `KW_CONST IDENT COLON type_ref` (`grammars/logos.peg:2880`); see §3.
- Where-clauses — OK. `where_clause` parsed on every decl shape (e.g. `grammars/logos.peg:851-863` for fn) and merged into `TypeParam.bounds` at `src/compiler/sema.cpp:3545-3618`.
- `?Sized` opt-out — partial. `implicit_sized` flag exists; enforcement only at struct/enum decl sites — see §7.
- Variance — GAP. No per-parameter variance tracking. `region_infer.cpp` has no variance machinery; `TypeParam` has no `variance` slot in `sema.hpp:376`. Rust `subtyping.md` not enforced.
- Monomorphization — OK. `mono_clone.cpp` clones generic items per `SubstMap`.
- Inference — partial. Turbofish works (`grammars/logos.peg:273` GENERIC_REF). Bidirectional inference exists for fn-args; deep `impl Trait` inference is partial.
- HRTB — OK at trait-bound position, see §6.
- Defaults — OK. `IDENT COLON … ASSIGN type_ref` / `IDENT ASSIGN type_ref` (`grammars/logos.peg:2890-2893`); `default_type` slot used at `sema.cpp:4694`.

**Gaps / debt:**
- No variance computation / `PhantomData` analogue. Lifetime subtyping (`'long <: 'short`) not enforced for type-param positions.
- `is_variadic` is a Logos addition; harmonization not needed but record in §A6.

---

## 2. Lifetime parameters

**Rust nomenclature:** `LifetimeParam -> Lifetime ( ':' LifetimeBounds )?` (`reference/src/items/generics.md` r[items.generics.syntax]). Outlives `'a: 'b` (`reference/src/trait-bounds.md` r[bound.lifetime]).

**Logos nomenclature:** Grammar `lifetime_param` (`grammars/logos.peg:2858-2861`); AST node `LIFETIME_PARAM` (slot 131, `grammars/logos.peg:190`). Outlives carried as `LIFETIME_PARAM.ITEMS = [LIFETIME_PARAM(name)…]`. In LIR: `LFunction.lifetime_params` and `LFunction.lifetime_outlives` (vector of `(long, short)` name pairs) — see `src/compiler/sema_decl.cpp:21-115` (`compute_fn_lifetime_outlives`). Analogous fields on `LStructDef.lifetime_outlives` (`sema_decl.cpp:864-911`), `LEnumDef.lifetime_outlives` (`sema_decl.cpp:971-1020`), `LImplBlock.lifetime_outlives` (`sema_decl.cpp:1354`). Bound subject: `TypeParam.lifetime_outlives` (`sema.hpp:388`).

**Match verdict:** WARN — grammar + storage match Rust spec, but enforcement is partial. `region_infer.cpp` is named-lifetime-blind: the file has zero references to "lifetime" / "outlives" / "'static" (`grep` empty). Outlives data is collected and propagated through mono substitution but is not consumed by region inference at borrow-check time.

**Implementation pointer:** parse `grammars/logos.peg:2858`; collect `src/compiler/sema.cpp:3414` (in `read_type_params_inner`); fn-level outlives `src/compiler/sema_decl.cpp:21-115`.

**Interactions check:**
- Lifetimes (`'a` in types) — partial. References carry `lifetime_args` on TypeRef (`sema.hpp:340`), stored by name. Borrow-checker enforces exclusivity over CFG but does not consume `'a` annotations.
- References — partial as above.
- Outlives — partial. Collected (`sema_decl.cpp:71-115` shows fn/struct/enum/impl read-and-merge of WHERE-clause outlives) but not enforced as a constraint at use sites.
- Elision rules — GAP. `lifetime-elision.md` (input/output position rules) not implemented. Logos accepts elided refs (no `'a` written) because all named lifetimes are erased post-parse for codegen; no test exists for "elided lifetime in output of a multi-input fn must error" (Rust's E0106).
- Structs `struct S<'a>` — OK at parse/collect; `sd.lifetime_params` populated (`sema_decl.cpp:864`). No enforcement of `'a` in field-region propagation.
- Impl blocks `impl<'a> T for S<'a>` — OK at parse; collect at `sema_decl.cpp:1352-1363`.
- HRTB — OK; `hrtb_lt` & `hrtb_binder` productions (`grammars/logos.peg:2801-2806`).
- Trait objects `+ 'a` — partial. Grammar admits a `+ 'a` lifetime in `Bounds` shape; sema parses `lifetime_args` on TraitBound; not enforced as dyn lifetime.
- `'static` — partial. Recognized as a literal lifetime name; no `'static` outlives propagation.
- GATs — partial (see §8).

**Gaps / debt:**
- `region_infer.cpp` operates on CFG locals only; no named-lifetime equivalence classes. Outlives constraints declared in source are dead data at borrow-check time.
- Lifetime elision rules (input → output) unimplemented.
- `'static` is a magic-name token, not a special-cased outlives top element.

---

## 3. Const parameters

**Rust nomenclature:** `ConstParam -> 'const' IDENTIFIER ':' Type ( '=' (BlockExpression | IDENTIFIER | '-'?LiteralExpression) )?` (`reference/src/items/generics.md` r[items.generics.const]). Lives in the value namespace; allowed types restricted to `{u8…u128, i8…i128, usize, isize, char, bool}` (r[items.generics.const.allowed-types]).

**Logos nomenclature:** Grammar `KW_CONST IDENT COLON type_ref` (and antiquot / variadic variants) at `grammars/logos.peg:2876-2881`, AST `CONST_PARAM` (used at `sema.cpp:3414,3491`). Sema flag `TypeParam.is_const = true` (`sema.hpp:380`), backing type in `TypeParam.const_type` (`sema.hpp:381`). At expression-position use the const-param resolves to a magic VarRef name `"__const_param:N"` (`src/compiler/sema_expr.cpp:367-375`); mono detects the prefix and substitutes at clone time (`src/compiler/mono_clone.cpp:363-409`).

**Match verdict:** WARN — feature exists and works for monomorphization, but the lowering deliberately diverges from Rust's "value-namespace name" model. `__const_param:N` is a synthetic identifier, not a real value-namespace binding; this means the parameter name is NOT looked up like a value in user code — the front-end fabricates the lookup via a magic prefix. Net surface behaviour is roughly Rust-shaped (you can write `N` inside the body), but the implementation is a string-keyed back-door, not a proper namespace integration (no `value namespace` per `items.generics.const.namespace`).

Additionally, type restriction (only integer/char/bool allowed in Rust) is NOT enforced — Logos admits a `HermesStatic` const-param type (visible in `mono_clone.cpp:378`), a Logos addition.

**Implementation pointer:** grammar `tools/peg_gen/grammars/logos.peg:2876-2881`; sema `src/compiler/sema.cpp:3414-3494`; expr lowering `src/compiler/sema_expr.cpp:367-375`; mono splice `src/compiler/mono_clone.cpp:363-409`.

**Interactions check:**
- Array types `[T; N]` — OK. Const-generic `N` flows through array-type construction at mono time.
- Const eval — N/A — blessed §A1; const-context arithmetic on a const-param flows via metacall when needed.
- Type parameters — OK (mixed-order allowed; `EitherOrderWorks<const N: bool, U>` would parse).
- Where-clauses — partial. No tests known for `where [(); N]: Sized`-style bounds.
- Inference (`_` const argument) — GAP. Rust `items.generics.const.inferred`: `f::<_>()` and `[0; _]` infer the const. Logos has no `_` inferred-const at call site or in array repeat; turbofish requires a concrete value.
- `#[derive]` interplay — N/A (derives are §A3-replaced).
- Trait bounds — partial. Const generics participate in mono key but no first-class const-trait-bound (`const T: Trait`, which is unstable in Rust anyway).

**Gaps / debt:**
- `__const_param:N` magic-prefix is a Logos-internal hack; Rust uses a value-namespace name. Consider promoting to a proper sema binding so general value-position uses don't depend on string prefix matching.
- No `_` inferred-const argument.
- Type restriction (Rust whitelist) not enforced — accepting `HermesStatic` is a Logos addition.

---

## 4. Where-clauses

**Rust nomenclature:** `WhereClause -> 'where' (WhereClauseItem ',')* WhereClauseItem?` with `LifetimeWhereClauseItem | TypeBoundWhereClauseItem` (`reference/src/items/generics.md` r[items.generics.where.syntax]). Bound subjects may be non-parameters (`String: PartialEq<T>` valid; bounds on associated types valid).

**Logos nomenclature:** AST slot `WHERE` (slot 38, `grammars/logos.peg:58`); grammar production `where_clause` (used pervasively, e.g. `grammars/logos.peg:703,851,899`). Collector: `src/compiler/sema.cpp:3545-3618` (where-clause walker, merges WHERE constraints into `result` `TypeParam` list); reference-subject form (`where &T: Trait`) supported at `sema.cpp:3554-3590` with `on_ref_subject` flag.

**Match verdict:** WARN — surface coverage is real (every decl shape that admits generics also admits where-clauses; lifetime outlives in WHERE flow through `read_lifetime_outlives_from(node, la::WHERE.code)` at `sema_decl.cpp:74,867,974`). But Rust's "bound on arbitrary type" (`String: PartialEq<T>` example, `reference/src/items/generics.md`) is partial: the where-walker at `sema.cpp:3553` skips items whose code is not TYPE_PARAM, and only the reference-subject special-case at `sema.cpp:3559-3591` handles a `&T` subject. A bound on a concrete type like `String: PartialEq<T>` will be silently dropped.

**Implementation pointer:** where parsing `tools/peg_gen/grammars/logos.peg:2790-2897` (under TYPE_PARAMS); where-clause grammar rule not pretty-printed but used as `where_clause` in 30+ alts; sema merge `src/compiler/sema.cpp:3545-3618`.

**Interactions check:**
- Generics — OK (merge into TypeParam bounds list).
- Trait bounds — OK for type-param subjects; partial for arbitrary-type subjects.
- Lifetime bounds — OK at outlives shape; merged via `read_lifetime_outlives_from`.
- Assoc-type equality — OK at trait-bound level (`bound_arg <- IDENT ASSIGN type_ref => ASSOC_EQ_BIND`, `grammars/logos.peg:2847`); stored in `TraitBound.assoc_eqs`.
- `Self: Sized` (opt-in object method exclusion) — OK. Scanned at `sema_collect.cpp:2006-2024` to exclude that method from the dyn vtable.
- Method/impl decls — OK; every alt admits WHERE.
- HRTB — OK. `hrtb_binder` admitted as a prefix (`grammars/logos.peg:2813-2823`).
- GATs — partial. Where-clauses on assoc types (`type Item: Bound;`) read at `sema_collect.cpp:1931-1940`.

**Gaps / debt:**
- WHERE subject types other than `T` / `&T` / `&mut T` silently dropped (not a hard error). Add a diagnostic.
- No `Self: Trait` bound in arbitrary impl-decl WHERE position (only at trait-decl level via supertraits).

---

## 5. Trait bounds

**Rust nomenclature:** `Bounds -> Bound ( '+' Bound )* '+'?`, `Bound -> Lifetime | TraitBound | UseBound`; `TraitBound -> ('?' | ForLifetimes)? TypePath` (`reference/src/trait-bounds.md` r[bound.syntax]).

**Logos nomenclature:** Grammar `trait_bound` (`grammars/logos.peg:2811-2840`); AST `TRAIT_BOUND` (slot present, code `la::TRAIT_BOUND`); sema struct `TraitBound` (`include/logos/compiler/sema.hpp:338-372`) with rich field set: `trait_name`, `type_args`, `lifetime_args`, `hrtb_binders`, `assoc_eqs`, `fn_params`/`fn_ret`/`is_fn_family` (parenthesized `Fn(args) -> R` form), `is_relaxed` (the `?Sized` form), `on_ref_subject`/`is_ref_mut` (for the G158-6 `&T: Trait` shape).

**Match verdict:** OK on the core surface — `+`-combined bounds, HRTB binder, `?Sized` opt-out, associated-type equality `Trait<Item = U>`, parenthesized fn-family, all present. WARN on `UseBound`: Logos has no `use<…>` precise-capturing bound (`reference/src/trait-bounds.md` r[bound.use]) — `grammars/logos.peg:2811-2840` shows no `KW_USE` alt; corresponds to RPIT precise capturing, which Logos does not implement.

**Implementation pointer:** grammar `tools/peg_gen/grammars/logos.peg:2811-2840`; collect/merge `src/compiler/sema.cpp:3282-3414`; method dispatch via bounds at `sema_collect.cpp:1910-1944`; trait engine `src/compiler/trait_engine.cpp`.

**Interactions check:**
- Generics — OK.
- Where-clauses — OK (with the caveat in §4 about arbitrary-type subjects).
- `dyn Trait` — OK. Trait-object emission consumes the bound list; supertrait vtable handled (per `ref_dyn_supertrait_vtable.md` index entry).
- Supertraits — OK at `sema_collect.cpp:1903-1916`.
- HRTB — OK. `hrtb_binders` flag set per-bound at `sema.cpp:3351-3355`.
- Auto-traits — partial. `KW_AUTO trait_kw` admitted by grammar (`grammars/logos.peg:783-799`); `sema_auto_trait.cpp` handles propagation. Send/Sync per Cat H not in scope of this audit but present.
- Lifetime bounds — partial (see §2).
- Associated-type bounds (`T: Trait<Item = U>`) — OK; `assoc_eqs` populated.

**Gaps / debt:**
- `use<…>` bound absent (gates RPIT precise-capturing).
- Arbitrary-type subject bound silently dropped (§4 cross-ref).
- No diagnostic for malformed `+`-chains.

---

## 6. HRTB `for<'a>`

**Rust nomenclature:** `ForLifetimes -> 'for' GenericParams`, used either before a TraitBound (`for<'a> Fn(&'a T)`) or before a WHERE-clause type subject (`for<'a> T: Trait<'a>`). Spec: `reference/src/trait-bounds.md` r[bound.higher-ranked].

**Logos nomenclature:** Grammar `hrtb_binder <- KW_FOR LT hrtb_lt (COMMA hrtb_lt)* COMMA? GT_TYPE` (`grammars/logos.peg:2803-2806`); each binder lifetime wraps as `LIFETIME_PARAM` (`grammars/logos.peg:2801`). Carried on `TraitBound.hrtb_binders` (`sema.hpp:350`). Skolemization at impl resolution: `src/compiler/sema_collect.cpp:875-936` (B85: "HRTB skolemization — if the impl declares a where-clause"). AST slot `HRTB_BINDERS` (slot 41, reuses IMPL_TYPE_PARAMS slot per `grammars/logos.peg:73`).

**Match verdict:** OK. Both bound-prefix and trait-prefix HRTB forms are admitted by `trait_bound` alts (each fn-family + `IDENT<…>` alt has an `hrtb_binder` variant). Skolemization is implemented for impl-resolution (B85).

**Implementation pointer:** parse `tools/peg_gen/grammars/logos.peg:2801-2824`; sema collect `src/compiler/sema.cpp:3349-3355`; impl HRTB satisfaction `src/compiler/sema_collect.cpp:817-936`.

**Interactions check:**
- Lifetimes — partial. HRTB binders are recorded but region-inference is lifetime-blind (§2). Bounds-as-types satisfaction works via name matching, not via the spec's "for all 'a" universal.
- Function pointers `for<'a> fn(&'a T)` — partial. `fn_ptr_type` (grammar) does not admit a `for<'a>` prefix — grep of `grammars/logos.peg` shows no `KW_FOR.*fn_ptr` alt. Function-pointer-position HRTB is GAP.
- Trait bounds — OK at type-param / where-clause positions.
- Closure traits `for<'a> Fn(&'a T) -> &'a U` — partial. The `hrtb_binder IDENT LPAREN closure_type_args RPAREN ARROW type_ref` alt at `grammars/logos.peg:2815-2816` accepts the syntax; effective binder-vs-outer-lifetime distinction depends on region inference being aware.
- Variance — N/A — no variance machinery (§1).

**Gaps / debt:**
- No HRTB on bare function-pointer types.
- Binders are recorded by name; soundness via universal quantification is by skolemization at impl-match only, not at use-site type-checking. Sufficient for most parametric callbacks; insufficient for advanced cases (`Box<dyn for<'a> Fn(&'a T)>` round-trips).

---

## 7. `Sized` / `?Sized`

**Rust nomenclature:** Default implicit bound on every type parameter. `?Sized` opts out. `reference/src/special-types-and-traits.md` §Sized; `reference/src/trait-bounds.md` r[bound.sized] ("`?` is only used to relax the implicit `Sized` trait bound for type parameters or associated types").

**Logos nomenclature:** `TypeParam.implicit_sized = true` default (`sema.hpp:395`); the relaxed-bound walker clears it at `src/compiler/sema.cpp:3286-3294` ("`?Sized` clears the implicit Sized bound; any other relaxed name" is rejected at `sema.cpp:3294-3303`). Grammar `trait_bound <- QUESTION IDENT => RELAXED:true` (`grammars/logos.peg:2811-2812`). Sized-enforcement at struct/enum generic args: `sema.cpp:4654-4721`. Sema_collect comment is honest: "M7-mt-03: `Sized` is a compiler-builtin marker. Logos has no unsized types yet, so every concrete type satisfies it; the bound is admitted as a no-op" (`sema_collect.cpp:743-747`).

**Match verdict:** WARN — opt-out flag exists end-to-end, but enforcement is partial:
- Sized-enforcement runs only at the struct/enum decl's generic-instantiation site (`sema.cpp:4700-4721`).
- The internal comment at `sema_collect.cpp:743-747` flags that `T: Sized` is admitted as a no-op everywhere else.
- Functions / methods / impls / let-bindings do NOT enforce that a non-`?Sized` parameter is bound to a Sized type at call/use sites.
- Stdlib `T: ?Sized` propagation through `.hm0` header load is broken per the index entry `project_box_unsized_customdst.md` — "front-gate: `?Sized` on a stdlib struct param is LOST through `.hm0` load".

**Implementation pointer:** flag `include/logos/compiler/sema.hpp:389-395`; relaxed-bound parse `tools/peg_gen/grammars/logos.peg:2811-2812`; clear logic `src/compiler/sema.cpp:3286-3294`; struct/enum enforcement `src/compiler/sema.cpp:4654-4721`; no-op admission `src/compiler/sema_collect.cpp:743-747`.

**Interactions check:**
- Generics (implicit bound) — OK at declaration; enforcement partial (per above).
- DST — OK at the bare-type level (`UnsizedSlice` / `UnsizedDyn`, B2/B3 done) but `?Sized` propagation through stdlib decl boundaries is broken (P1 in `project_box_unsized_customdst.md`).
- Trait objects — OK. Bare `dyn Trait` resolves to UnsizedDyn (`sema_collect.cpp:2193-2198`).
- References (fat vs thin) — OK in mlir-gen; fat ptr representation for `&[T]` / `&dyn`.
- `Box<T: ?Sized>` — OK for `Box<[T]>` (B3 done 2026-05-29) and `Box<dyn>`; per-shape, not by truly generic `?Sized`.
- Method receivers (`self: Box<Self>` with `Self: ?Sized`) — partial. Works for `dyn`/`[T]` ad-hoc.
- Struct (last-field-unsized rule) — OK after B2 (custom-DST tail-slice done 2026-05-29).

**Gaps / debt:**
- Generic `T: ?Sized` propagation through stdlib `.hm0` decl boundary — known P1.
- Sized-bound enforcement at function call sites missing — Logos accepts `fn f<T>(x: T)` and a caller passing an unsized arg would not be caught at the call site (only at the next generic-decl boundary).

---

## 8. Generic associated types (GATs)

**Rust nomenclature:** Associated type with its own generic parameters: `trait Iter { type Item<'a>; }`; projection `T::Item<X>`; `reference/src/items/associated-items.md` §Type Aliases.

**Logos nomenclature:** Grammar `ASSOC_TYPE_DEF` (`grammars/logos.peg:884-887`, slot 119); collect picks up the assoc type's own `type_params` at `src/compiler/sema_collect.cpp:1923-1944`; sema struct `SemaAssocTypeInfo` carries `type_params` plus `bounds`. Projection: `ASSOC_TYPE_REF` (`grammars/logos.peg:1318-1323`), e.g. `simple_type COLONCOLON IDENT LT type_arg_list GT_TYPE`. Qualified-path projection `<T as Trait>::Item` via `qualified_assoc_type` (`grammars/logos.peg:1315-1316`). TypeRef kind `Kind::AssocType` (`sema.hpp:66`); fields `assoc_base`, `assoc_type_name`, `gat_args` (`sema.hpp:328-330`). Impl: `ASSOC_TYPE_IMPL` (`grammars/logos.peg:981-984`); resolution `src/compiler/sema_decl.cpp:1146-1152` ("Phase 6 (GAT projection)") and `sema_collect.cpp:2457` ("Phase 6: scope the impl's trait name so `Self::Item<X>` inside ..."). LIR: `LTraitDef.assoc_types` (`lir.hpp:904`), `LImplBlock.assoc_types` (`lir.hpp:941`).

**Match verdict:** OK on structure — assoc types carry their own type_params (genuine GAT), projection syntax matches Rust (`T::Item<X>` and `<T as Trait>::Item`), impl side carries `TYPE_PARAMS` per assoc impl. WARN on completeness:
- Lifetime-GATs (`type Item<'a>;`) parse but lifetimes have no semantic enforcement (§2 cross-ref).
- Where-clauses on assoc-type defs in Rust shape `type Item where Self: Sized;` not seen in the grammar (bounds on assoc-types are `COLON trait_bound (PLUS trait_bound)*` only, `grammars/logos.peg:884`).
- Object-safety bars certain GAT shapes (Rust rejects GATs in object-safe traits); not enforced in Logos.

**Implementation pointer:** parse `tools/peg_gen/grammars/logos.peg:884-887,981-984,1315-1323`; collect `src/compiler/sema_collect.cpp:1923-1944,2457-2648`; project `src/compiler/sema_decl.cpp:1112-1152,1834`; mono substitution via `gat_args` field.

**Interactions check:**
- Trait associated types — OK (basic non-GAT is the degenerate GAT-with-zero-params).
- Generics — OK; substitution via `gat_args`.
- Lifetimes (lifetime-GATs) — partial; parse OK, no enforcement.
- Where-clauses — partial; only bounds-after-colon form, no WHERE on assoc-type itself.
- HRTB — partial; `for<'a> T::Item<'a>: Trait` not exercised; depends on §6.
- Trait object compatibility — GAP. Object-safety check for "trait with GAT cannot be made into an object" not present (`grep` for object-safety-GAT check in sema empty).

**Gaps / debt:**
- Object-safety rejection of GAT-carrying traits.
- WHERE-clause on assoc-type defs (`type Item where …;`).
- Lifetime-GAT semantic enforcement gated on §2 region inference work.

---

## Cross-category gaps

- **Region inference is named-lifetime-blind** (§2). Cross-cuts Cat A (Borrow / Lifetimes / Variance). `region_infer.cpp` operates on CFG locals only; named-lifetime constraints declared in source (`<'a, 'b>`, `'a: 'b`, `T: 'a`) are dead data at borrow-check time. Soundness gap whenever a borrow-checker corner case depends on a declared outlives. Cross-ref the index: no row in DIVERGENCES §A or §B explicitly covers this — likely deserves a §B row.
- **Variance** (§1). Cross-cuts Cat A (Variance & Subtyping) and Cat F (Patterns binding modes). No `PhantomData`, no per-param variance computation. Lifetime/type subtyping (`reference/src/subtyping.md`) is essentially absent.
- **`?Sized` propagation through stdlib `.hm0`** (§7). Cross-cuts Cat C (Items / Module loading) — already catalogued in `project_box_unsized_customdst.md` index entry.
- **`use<…>` precise-capturing bound** (§5). Cross-cuts Cat B (`impl Trait` return type capture rules). Gated on Rust RPIT capture model.
- **Inferred const `_`** (§3). Cross-cuts Cat B (Inferred type `_`) and Cat E (Cast / turbofish path syntax).

## Recommended next moves

Sized in single-session scope:

1. **Where-clause arbitrary-type subject diagnostic.** Today `where String: PartialEq<T>` is silently dropped at `sema.cpp:3553` (`if (code_of(constraint) != la::TYPE_PARAM) continue;`). Emit a diagnostic — either implement the constraint (add to a side ledger of "well-formedness must hold") or hard-error "bounds on non-parameter types not supported". 1 session.
2. **HRTB on bare `fn(...)` type.** Grammar add: `fn_ptr_type` should accept `hrtb_binder?` prefix per Rust `for<'a> fn(&'a T)`. The sema side can ignore binders at first (matches today's stance for trait-position HRTB). 0.5 session.
3. **Object-safety GAT rejection.** A trait with an associated type carrying type_params should error when used in `dyn` position. Cross-ref `sema_collect.cpp:2631` (the dyn-vtable construction site). 0.5 session.
4. **Sized enforcement at fn call sites.** The Sized-enforcement loop at `sema.cpp:4700-4721` only fires for struct/enum gen-arg substitution. Mirror it at function-call generic substitution + at let-binding when the let-type is a `TypeVar` whose source param is `!?Sized`. 1 session.
5. **`_` inferred const at call site.** `f::<_>()` and `[0; _]` should parse + infer through unification. Grammar already admits `_` as type — extend to const-arg position; mono inserts the inferred literal. 1 session.

Larger, multi-session:

6. **Region inference over named lifetimes.** `region_infer.cpp` rewrite to consume `LFunction.lifetime_params` + `lifetime_outlives` and build a region graph indexed by named region (in addition to per-CFG block region). Closes the §2 WARN + the variance gap's prerequisite. Multi-session.
7. **`__const_param:N` → proper value-namespace binding.** Eliminate the magic-prefix hack in `sema_expr.cpp:367-375` + `mono_clone.cpp:363-409`. Make const-params real value bindings in the symbol table. Surface-equivalent today, but cleans up a recurring source of debt and matches `items.generics.const.namespace` exactly. Multi-session.
