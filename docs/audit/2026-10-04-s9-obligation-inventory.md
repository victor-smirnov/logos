# S9 inventory: trait satisfaction, impl selection, Self, impl keys (2026-10-04)

Read from the code at `2eb554d31` (ADR 0030 S8 closed) for ADR 0030 S9
(C-OBL). Nothing here was executed: every behavioural difference is inferred
from code and is a hypothesis to measure before a row relies on it. Call
counts are grep counts of call sites. Functions are cited by symbol.

## 0. Registries

Sema:
- `impls_`: `ImplKey{DefId trait, std::string target}` — identity trait, SPELLED
  target; single-valued, last insert wins; written once at the end of
  `collect_impl` (+ an `&[u8]` alias for `str`). `impls_all_`: same key, all
  impls (several `Trait<A>` for one Self).
- `has_impl` / `find_impl`: exact-key probes; neither reads `is_negative`.
- `blanket_impls_`: one entry per METHOD of each `impl<T: B…> Tr for T`, plus a
  marker entry for a method-less blanket; `blanket_implements` compares by
  DefId. A blanket is ALSO in `impls_` under its type-variable name
  (`ImplKey{Tr, "T"}`), so `has_impl(Tr, "T")` is true whatever the bounds.
- `assoc_type_impls_` / `assoc_const_impls_`: `AssocKey{trait, targs "$G<n>$…",
  target, name}`; a blanket's assoc types under `$blanket$<Trait>$<RawBound>$<TV>`.
- `coherence_keys_`: `<canonical_trait>[<args>]::<pkg::target|target>`,
  non-generic impls only.
- `copy_types_` / `conditional_copy_`: a separate Copy registry (explicit
  `impl Copy` + the `compute_auto_copy_types` fixpoint), not in `impls_`.

Mono:
- `concrete_impls_`: `set<(identity trait, target spelling)>` from every
  non-blanket impl, negative impls INCLUDED.
- `blanket_impls_` (`BlanketImplInfo`), `assoc_impls_` (`"<bare trait><targs>::<target>::<name>"`
  + a plain key).
- `trait_engine_` (TraitEngine): populated once at the first query and never
  refreshed — impls added later (factory drain) are invisible to it.
  `trait_rules_` is its Datalog shadow (`LOGOS_DL_SHADOW=traits`), logging only.

## 1. Sema satisfaction engines

### 1.1 `check_type_bounds` (15 call sites; quiet wrapper `type_bounds_satisfied_quiet`, 5)

Per type argument, in order: integer-literal variable (defer to
`lit_close_fn_`, probe mode judges the default); open `?iN` (defer to
`infer_close_fn_`, which re-checks only args concrete by then — still-open ones
are SKIPPED); lifetime half; fn-value generality; any TypeVar / AssocType /
CfgSlotType → permissive `continue` (no param-env check of trait bounds exists
in sema; mono decides).

Per bound: `Sized` by spelling → accept; raw pointer + `eq`/`ord` lang item →
accept; Copy builtin kinds (Ref, Ptr, Slice, FnPtr/FnItem, TraitObject — owning
`Box<dyn>` included); `where &T: Tr` → `$ref_…` key only; auto trait →
`is_auto_trait_satisfied`; trait args → `impls_all_` scan with pattern
unification (a TypeVar/ConstVar side matches), a failure disables every
name-keyed accept below; direct hit `impls_[key1|key2]` (SL-sl-02 retries
`PartialEq`/`PartialOrd` as `Eq`/`Ord` here only), a generic direct hit checks
the impl's params (unbound → accept), then HRTB `region_ok`; blanket arm
(identity; ADR 0008 eqs of the blanket's bounds); generic-struct arm: a struct
whose bare name has an impl → accept WITHOUT the impl's bounds (no enum
counterpart: an enum hits key1 and is checked); slice / array / `&T` via
`generic_impl_holds`; tuples: each element must have an impl of the SAME trait
by `type_str` (no blanket, no generic key, no `$ref_`, the impl's real element
bounds unread); Fn family: closure kind + signature shape (the kind error is
emitted even in probe mode); `&F` with a callable pointee; fn value with
`$fnptr$<n>`; `dyn Tr` by supertrait DefId walk; `&T` keys; factory-backed
markers defer. Negative impls count as satisfied; assoc-type equality on the
checked bound itself is not checked.

### 1.2 `sema_has_impl_recursive` (14 call sites)

String-keyed: `impls_[concrete]`, `impls_[alt]`, the `$ref_` / `$mut_ref_`
recompositions, `$ref_$T` recursing on the referent per impl-param bound,
blankets by identity. No builtins at all (Copy, auto, Sized, Fn, tuple, slice,
array, fn pointer), no generic-impl bounds, no trait args, no assoc eqs, no
negative impls.

### 1.3 Direct `impls_` probes (`has_impl(` 37 calls, ~25 more `impls_.find`)

Exact presence, usually × {`concrete_struct_name`, bare name}:
`op_assign_call_`, `lower_place_compound_assign`, `try_index_mut_assign`,
`lower_place_assign`, `index_output_type_` (last-wins), `lower_index_place`,
`lower_index_read`, `deref_target_type_` (last-wins), `emit_generic_deref_call`
(`impls_all_`, skips negatives), `infer_type_args` bound-driven pass
(last-wins), `lower_method_call` closure hints (last-wins), assoc-const trait
choice in `lower_enum_lit` / `lower_enum_lit_data` / `lower_static_call` (scan
every trait), `Trait::f` as a value (count targets), `native_source_spec`,
`lit_select_by_trait_` (12 integer kinds, direct impls only),
`compute_auto_copy_types` (Drop, Copy×Drop E0184, StableLayout × 3 spellings),
`self_describing_dst_ref`, `subst_type_sema` blanket projection (shallow),
`resolve_type_assoc_ref` last resort (every trait), `lower_module_items`,
`lower_fn` (impl lifetime outlives), `lower_impl_block` (trait args/lifetimes,
last-wins), `check_supertrait_impls`.

### 1.4 `check_supertrait_impls` (1)

Per `impls_` entry and supertrait: `has_impl` on the impl's own target
spelling, a blanket via `sema_has_impl_recursive`, or the target is a type
param whose bound reaches the supertrait. Not considered: generic-impl bounds,
builtins, auto traits, SL-sl-02.

### 1.5 `assoc_eqs_satisfied` (4)

`assoc_type_impls_` by concrete then base; blanket fallback by the raw
`bound_trait` spelling; `types_equal`.

### 1.6 Blanket loops (8)

`sema_has_impl_recursive`, `check_type_bounds`, `check_supertrait_impls`,
`assoc_eqs_satisfied` (identity); `subst_type_sema` TypeVar arm (raw spelling,
no supertraits) and concrete arm (raw spelling, shallow `has_impl`, positional
`make_subst`); `viable_blanket_impls` (the only call-site blanket coherence
check, when `report=true`); `lower_method_call` abstract-T arm (raw spelling
closed under supertraits).

### 1.7 Other deciders

`ref_arg_satisfies_dyn` (7; by spelling for TypeVar/upcast, else
`sema_has_impl_recursive` + auto); `types_compatible` TraitObject arm
(permissive); `check_dyn_auto_bounds_at_coercion` (4); `find_generic_func_for_args`
and `find_best_sema_struct_spec` (TypeVar arg: bound NAMES compared, no
supertraits); `resolve_trait_item_` / `probe_method_` / `impl_lookup_keys_`
(symbol presence, impl bounds unread); the for-loop "is an iterator" test
(`<S>__next` exists); implicit Sized at 6 sites; `struct_type_is_copy` /
`is_move_type`; two Drop deciders (`drop_fn_for` by symbol,
`compute_auto_copy_types` by `has_impl`).

## 2. Mono engines and selectors

- `mono_has_impl_recursive` + TraitEngine (10): negative facts (never added),
  direct pair, blankets (AND), auto list (never added), shape predicates on the
  type-NAME string: Fn family = name starts with `|` (closures only); Copy =
  `$ref_`, `&` (not `&mut`), `*const `, `*mut `, `fn(` (FnItem `fn ITEM<` is
  not); any `[`-name for a trait with any `$slice$` fact; Fst/Send/Sync/Unpin
  via `is_auto_satisfied` on a name rebuilt to a type. No trait args, assoc
  eqs, generic-impl bounds, Sized, SL-sl-02.
- `mono_concrete_satisfies_bound` (14 + 4 recursive): auto traits by spelling;
  slice / array keys accept without bounds; `&T` recurses with the SAME trait;
  `cname` = bare base (`$G` stripped) / enum / `ref_target_key` / `$tuple$N` /
  `$fnptr$N` / `type_str` / `str`; tuples unify the impl and check its element
  bounds; then `mono_has_impl_recursive`; generic struct/enum deep check (an
  Fn-family bound accepts any Struct). No trait args, assoc eqs, negatives,
  Sized, SL-sl-02, `dyn Tr: Tr`, concrete-specialization keys.
- `method_bound_ok` (2): where-type bounds, impl param bounds (Fn family
  permissive incl. Struct; auto by a same-NAME trait; HRTB region walk against
  the FIRST impl of that spelling; trait args never compared). A false answer
  silently drops the method.
- `instantiate_enum_templates` bound loop: spelling only, no Fn short-circuit.
- `find_best_struct_spec`: bound-discriminated specs by spelling; ambiguity is
  a mono diagnostic.
- Eager blanket pass / `assoc_eqs_ok` / dyn-coerced pass (mono.cpp): candidates
  are non-generic structs/enums; generic impls' own bounds unchecked;
  `assoc_eqs_ok` falls back to `assoc_impls_` and a raw-spelling blanket scan.
- Selectors: `trait_names_`, `impl_trait_args_match_` (the only mono code that
  compares trait args), `trait_item_symbol_` (7 sites; no check of a generic
  nominal impl's bounds; SL-sl-02 only at the bound-dispatch retry),
  `shape_trait_item_symbol_` (no impl-bound check), `blanket_trait_item_symbol_`,
  `trait_item_assoc_type_`, the `has_trait` / `has_trait_of` intrinsics (raw
  `type_str` names).
- Mono blanket loops (6): eager, `assoc_eqs_ok`, dyn-coerced,
  `populate_trait_engine_`, `trait_item_assoc_type_`, `blanket_trait_item_symbol_`
  (+ TraitEngine's inner loop).

## 3. Auto-trait engines

`SemaChecker::is_auto_trait_satisfied` (7; by DefId; coinductive) and
`Mono::is_auto_satisfied` (4; by spelling). Both return false for `char` /
`usize` / `isize` / `!`. They differ on TypeVar (in-scope bounds vs true),
closures (captures vs false), Unpin on pointers/refs, UnsafeCell / PhantomPinned
/ `#[pinned]` (sema only), generic explicit impls' bounds (sema only), negative
impls (sema only), Fst+Drop (`drop_fn_for` vs a Drop fact), struct lookup
(package-aware vs bare name). Copy: `compute_auto_copy_types` (sema only) —
`check_copy_impls_` runs before it.

## 4. Self derivations (6)

`collect_impl` header (`impl_self_ty`), `collect_impl` defaults (recomputed per
default, Self erased after), `lower_impl_block` seed, `lower_impl_block`
defaults, `lower_fn` (datatype first), struct/trait bodies. They disagree on
struct-vs-datatype priority, lifetime arguments (kept / dropped), `str`
(`UnsizedSlice<u8>` / unbound / `Slice<u8>`), positional `impl_tps` → struct
args in the defaults paths (`impl<U, T> Tr for P<T, U>` gives `P<U, T>` unless
the pattern is shaped), and the default-method where-gate (collect registers
every default, lowering skips one whose gate fails → a SemaFuncInfo with no
body). Mono derives no Self: it unifies `TARGET_TYPEREF` or falls back to the
target spelling.

## 5. Impl key spellings (sema `ImplKey.target`)

`*const T` → `type_str`; `[T]` / `&[T]` → `$slice$T` / `$slice$<elem>`;
`dyn Tr` → `$dyn$<Trait>`; `&S` → `$ref_<bare|concrete_struct_name>`; `&T` →
`$ref_$T`; `&prim` → `$ref_&i32`; `Foo<…>` → bare if generic, else
`concrete_struct_name` for a STRUCT (a concrete enum stays bare); tuples →
`void` / `$tuple$variadic` / `$tuple$N` / `$tuple$N$<t1>$…`; `[E; N]` →
`$array$<elem>$<len|N>`; `fn(..)` → `$fnptr$<arity>`; a name (incl. a blanket
TV); `str` + alias `&[u8]`. Other sema spellings: `$blanket$…`, `$marker$…`,
`trait_targ_suffix`, coherence keys. Mono: `concrete_impls_`, `assoc_impls_`,
`assoc_const_values_`, the trait-engine query names, `ref_target_key`, the
`$blanket$…__` template prefix.

## 6. Consumers

E0277-class diagnostics at every instantiation site; RPIT; unsize/dyn; `?`;
dyn `+Send`/`+Sync`; for-loop iterator; repeat Copy; supertraits; E0184, E0204,
StableLayout, SelfDescribing, E0368/E0594. Method and trait-item resolution,
Index/Deref routing, assoc-const trait choice, projections. Overload / spec
selection, integer-literal solving. Default-method synthesis. Move semantics.
Mono: method-instance existence (`method_bound_ok`, silent drop), the enum
gate, spec selection, blanket enqueue, callee symbols (`trait_item_symbol_`
family; a miss is an ICE in the last round), projections, `has_trait` folding.
Coherence: non-generic keys, array overlap, call-site blanket overlap, mono
ambiguity → no answer; no post-collection overlap pass.

## 7. Differences between engines (inferred — measure before relying)

1. `Vec<NoClone>: Clone` accepted by sema's generic-struct arm, refused by mono
   (`method_bound_ok` drops the method); `Option<NoClone>` is checked by both.
2. `impl Tr for W<i32>` (key `W$G1$i32`): sema finds it, mono strips `$G` and
   does not (unless a generic impl also exists).
3. Tuples: sema checks each element against the same trait by `type_str`
   (refuses `(Vec<i32>, i32): Clone`, `(&i64, i64): Eq`); mono checks the
   impl's element bounds (accepts).
4. Negative impls are positive for `has_impl`, `sema_has_impl_recursive`,
   `check_type_bounds`, `check_supertrait_impls`, `concrete_impls_`,
   TraitEngine; negative impls are not restricted to auto traits.
5. Auto traits: see §3.
6. Fn family: sema checks closure kind and signature; mono's `method_bound_ok`
   accepts any struct; mono's enum gate / spec selection fail fn pointers.
7. Trait args: sema compares (`impls_all_`); mono's gates never do — only its
   selectors.
8. ADR 0008 equality on the checked bound itself: neither side.
9. SL-sl-02: sema direct keys only; mono `trait_item_symbol_` retry only — a
   mono gate `T: PartialEq` on an Eq-only type drops the method.
10. Copy: FnItem (sema yes, mono no); owning `Box<dyn>` Copy on both sides;
    `[T; N]: Copy` refused by both; auto-Copy structs are Copy for moves but
    fail `T: Copy`.
11. An explicit `T: Sized` reaching LIR fails mono's `method_bound_ok`.
12. `dyn Tr: Tr`: sema yes, mono no rule.
13. Slices/arrays: sema checks impl bounds, mono accepts the key.
14. `impl<T: B> Tr for &T`: sema checks B on the referent, mono recurses with Tr.
15. No param-env check of trait bounds in sema; free-fn instantiation in mono
    has no bound gate (only method clones are gated).
16. Blanket matching: identity in some loops, raw spelling in others
    (homonyms answer).
17. Unbounded blankets: sema satisfies every type; mono's eager pass
    instantiates for non-generic structs/enums and dyn targets only.
18. TraitEngine is a one-time snapshot.
19. Last-wins `impls_` readers pick an arbitrary `Trait<A>` impl.
20. The closure Fn-kind error fires in probe mode.
