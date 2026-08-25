# B167 — the rustc borrow-check / NLL / lifetime compile-fail import

Upstream `rust-lang/rust` @ `da5114692c9ebe46b869488c5f34f92eb10b98c1`, `src/version` = 1.100.0.

983 `tests/ui/{borrowck,nll,moves,lifetimes,regions,dropck,drop}` compile-fail
tests were ported, one Logos program per upstream error site (plus, where the
classification needed one, an isolating CONTROL that must refuse). Every one of
the 983 reached a shelf; this file is where the shelves that are NOT fixtures
are named, because a skip that is not written down is indistinguishable from a
test that passes.

| shelf | where it lives | count |
|---|---|---|
| PIN — refuses for the upstream reason | `tests/imported/fail/<category>/` | 412 fixtures |
| ADMIT — a borrow-check hole, one row each | `tests/logos/bc_admits.ledger` + `tests/imported/admit/` | 463 rows |
| UNPORTABLE — the program cannot be written in Logos | this file, §1 | 167 rows |
| NOISE — refuses, but for an unrelated reason | this file, §2 | 39 rows |
| NOT CONFIRMED — a PIN row that did not survive the re-check | this file, §3 | 26 rows |
| A16 DIVERGENCE — admitting is CORRECT | this file, §4 | 3 rows |
| COMPILER HANG | this file, §5 | 4 programs |

## §1 — UNPORTABLE, with the missing feature per row

A row here is a rustc test whose *subject* cannot be expressed in Logos today.
It is not a borrow-check verdict of any kind, and it is NEVER a silent skip:
the feature named is what would have to exist for the test to be re-ported.

| upstream | upstream code | missing feature |
|---|---|---|
| `borrowck/alias-liveness/escaping-bounds-2.rs` | E0716 | `syntax error near 'trait'` — GATs (`type Gat<'a: 'b, ...>`) not in the grammar |
| `borrowck/alias-liveness/name-region.rs` | E0309 | (GAT `type Ty2<'a>` + `'static` bound on an associated type) |
| `borrowck/alias-liveness/tait-hidden-erased-unsoundness-2.rs` | hidden-type-captures-lifetime | feature: type_alias_impl_trait + named lifetime bounds |
| `borrowck/alias-liveness/tait-hidden-erased-unsoundness.rs` | hidden type captures lifetime | `#![feature(type_alias_impl_trait)]` — a TAIT (`type Tait<'a> = impl Sized + 'a`) plus `#[define_opaque]` and an `unsafe impl Send` keyed on `'static`; Logos has no opaque type aliases |
| `borrowck/async-trait-proposes-let-binding.rs` | temporary dropped while borrowed | feature: `async fn` (+ edition-2024 tail-temporary scoping) |
| `borrowck/borrowck-closures-slice-patterns.rs` |  | feature: slice patterns with `..` rest + `ref`/`ref mut` bindings ("syntax error near '..'") |
| `borrowck/borrowck-describe-field-generic-param-owned-box.rs` | E0507 | needs `#![no_core]` + `#[lang]` items + `#[repr(transparent)]` + `transmute` |
| `borrowck/borrowck-move-out-of-overloaded-auto-deref.rs` | E0507 | "call to undefined static method 'Rc::new'" |
| `borrowck/borrowck-slice-pattern-element-loan-array.rs` |  | feature: `let [..]` rest patterns ("`..` rest at let-position is currently not supported") |
| `borrowck/borrowck-thread-local-static-borrow-outlives-fn.rs` | E0712 | (`#[thread_local]`; a plain `static` in Logos is genuinely `'static`, so accepting is correct) |
| `borrowck/borrowck-union-move-assign.rs` | E0382 | `unknown generic type 'ManuallyDrop'`; without it: `union fields must be Copy` |
| `borrowck/borrowck-union-move.rs` | E0382 | feature: `ManuallyDrop` (non-Copy union fields unrepresentable) |
| `borrowck/cannot-return-ref-to-temporary-format-args.rs` | E0515 | needs `format_args!` / `core::fmt::Arguments<'a>` |
| `borrowck/cloning-in-async-block-121547.rs` | E0382 | (`async` blocks) |
| `borrowck/closure-upvar-named-lifetime.rs` | E0700/E0597 | `Arc<dyn Fn(Entry<'a, String, String>) + 'a>` returning `impl Fn(..)` — trait objects with a lifetime bound, `impl Trait` in return position, and `HashMap`/`Entry` |
| `borrowck/const-fn-ptr-borrow-annotation.rs` | E0597 | feature: const-item initializer evaluation + `mem::transmute` in const context |
| `borrowck/fn-item-check-trait-ref.rs` |  | feature: implied `T: 'static` bounds from `impl<T> Tr<&'static T> for T` (named lifetimes in impl headers) |
| `borrowck/fn-ptr-lifetime-mismatch-with-impl-trait-arg.rs` | (lifetime) | needs named lifetimes + `impl Trait` in argument position |
| `borrowck/ice-mutability-error-slicing-121807.rs` | E0220 etc. | (`extern "C"` trait fns, `Self::Assoc<'_>`, undeclared lifetime; an ICE regression on malformed input) |
| `borrowck/ice-on-non-ref-sig-ty.rs` | E0521/E0597 | feature: named lifetimes / HRTB in impl signatures |
| `borrowck/implementation-not-general-enough-ice-133252.rs` |  | feature: `async`/`await`, `impl Future`, `Pin` |
| `borrowck/index-mut-help2.rs` | – | p/t24_UNPORTABLE.txt |
| `borrowck/issue-102209.rs` | E0521 | (invariant named lifetimes + `PhantomData` branding) |
| `borrowck/issue-103624.rs` | E0521/E0507 | `async fn` + `.await` + `impl (Fn() -> T) + Send + Sync + 'static` argument-position `impl Trait` |
| `borrowck/issue-51301.rs` | E0507 | features: assoc-type bindings on `dyn Trait<Assoc=T>`, `?` operator, HashMap `Index`/iter of tuple refs |
| `borrowck/issue-81899.rs` | explicit panic in const | feature: const evaluation |
| `borrowck/issue-82126-mismatched-subst-and-hir.rs` | E0107 | feature: `async fn` (permanent skip per STRATEGY.md) |
| `borrowck/issue-88434-minimal-example.rs` | explicit panic in const eval | a `const` initialised by a `const fn` call that PANICS — const evaluation, which Logos does not have (`project_no_const_eval`) |
| `borrowck/issue-88434-removal-index-should-be-less.rs` | (const eval panic) | feature: const evaluation of a `const fn` in a const initializer |
| `borrowck/liberated-region-from-outer-closure.rs` | E0310 | t31_capture_escapes_nested_closure (adjacent probe; refused E0597) |
| `borrowck/moved-into-question-mark.rs` | E0382 | the `?` operator, `std::fs::read_dir`, `io::Result` and `dbg!` — the move is created by `?` desugaring |
| `borrowck/mut-borrow-in-loop-2.rs` | – | p/t33_UNPORTABLE.txt |
| `borrowck/opaque-types-patterns-subtyping-ice-104779.rs` | – | p/t34_UNPORTABLE.txt |
| `borrowck/overwrite-anon-late-param-regions.rs` | lifetime may not live long enough | feature: type_alias_impl_trait + `for<'a,'b>` HRTB |
| `borrowck/trait-impl-argument-difference-ice.rs` | E0220 etc. | (same shape as #23) |
| `borrowck/unconstrained-closure-lifetime-trait-object.rs` | E0310 | (HRTB `for<'a>`, `dyn Any` downcast, `T: 'static`) |
| `drop/drop_elaboration_with_errors.rs` | E0308 | (needs `type Tait = impl Sized` + `#[define_opaque]`) |
| `drop/drop_elaboration_with_errors2.rs` | unconstrained opaque type / conflicting impls | feature: TAIT defining-scope (`type A<T> = impl Sized`) + coherence. `type A = impl Sized;` parses (u00.logos rc 0) but the defining-use machinery does not exist. |
| `drop/drop_elaboration_with_errors3.rs` | (compile_error!) | feature: `async fn`, `impl Trait` return, `compile_error!` |
| `drop/lint-if-let-rescope-with-macro.rs` | if_let_rescope lint | feature: `macro_rules!` + edition-2024 `if let` rescope lint |
| `drop/lint-if-let-rescope.rs` | lint if_let_rescope | feature: edition-gated drop-scope lint |
| `drop/lint-tail-expr-drop-order-borrowck.rs` | tail_expr_drop_order (deny lint) | edition-2024 future-compat LINT; Logos has no edition mechanism and no such lint |
| `drop/lint-tail-expr-drop-order.rs` | tail_expr_drop_order | feature: edition-2024 drop-order lint (no edition/lint machinery) |
| `drop/tail_expr_drop_order-on-coroutine-unwind.rs` | tail_expr_drop_order lint | feature: `async fn` / coroutines |
| `dropck/dropck-after-failed-type-lowering.rs` | E0277 | (GAT `type C<'a>`) |
| `dropck/dropck-eyepatch-implies-unsafe-impl.rs` | E0569 | feature: `#[may_dangle]` / `unsafe impl` |
| `dropck/dropck-eyepatch.rs` | E0597 | feature: #[may_dangle] / dropck_eyepatch |
| `dropck/dropck-only-error-ambiguity.rs` | E0282 | (needs `Cow`/`ToOwned` blanket-impl normalization ambiguity) |
| `dropck/dropck-only-error-async.rs` | E0277 | feature: async fn in traits + assoc-type projection chains |
| `dropck/dropck-only-error-gat.rs` | E0277 | feature: generic associated types (`type Gat<T: Clone>`) with bound projection |
| `dropck/dropck-union.rs` | `v` does not live long enough | `union` itself parses (u04.logos rc 0); missing: `ManuallyDrop`, user `Deref` coercion, `Cell<Option<&C>>` |
| `dropck/reservation.rs` | reservation `Drop` impls unsupported | `#[rustc_reservation_impl]` rustc-internal attribute |
| `lifetimes/closure-lifetime-bounds-10291.rs` | lifetime may not live long enough | (HRTB `Box<dyn for<'z> FnMut(..)->..>`) |
| `lifetimes/closure-must-return-bound-lifetime.rs` | lifetime may not live long enough | feature: HRTB `for<'a>` in an `impl Trait` bound |
| `lifetimes/conflicting-bounds.rs` | E0283 | feature: HRTB `for<'s>` |
| `lifetimes/could-not-resolve-issue-121503.rs` | E0658 | feature: async fn + self: Box<Self, A> custom-allocator receiver |
| `lifetimes/elided-lifetime-in-const-param-type.rs` | E0412 | feature: `for<…>` HRTB binders with const params |
| `lifetimes/elided-lint-in-mod.rs` | hidden lifetime parameters deprecated | rc 0 + "unknown attribute '#[deny]'" |
| `lifetimes/implicit-lifetime-in-assoc-type-projection.rs` | E0495 / E0309 | feature: associated-type equality bound inside a trait-object type (dyn Tr<Assoc = X>) |
| `lifetimes/issue-105227.rs` | E0700 | (`impl Trait` return capturing a lifetime, `use<>` bounds) |
| `lifetimes/issue-105507.rs` | `T` does not live long enough | GAT decl parses (u08.logos rc 0); missing: `for<'a> T::Projected<'a>: MyTrait` where-clauses |
| `lifetimes/issue-105675.rs` | `FnOnce` not general enough | feature: HRTB closure signature inference |
| `lifetimes/issue-34979.rs` | E0283 | (where-clause `&'a (): Foo` on a struct decl) |
| `lifetimes/issue-69314.rs` | implicit elided lifetime not allowed here | `async fn` (permanent skip per STRATEGY) |
| `lifetimes/issue-76168-hr-outlives-3.rs` | expected FnOnce closure | feature: async fn + unboxed_closures + HRTB where-clause |
| `lifetimes/issue-90600-expected-return-static-indirect.rs` | E0597 + E0521 | feature: `RefCell`, `dyn Read`, unsize coercion of a `&RefCell<T>` |
| `lifetimes/issue-91763.rs` | hidden lifetime parameters deprecated | proc-macro aux crate + `elided_lifetimes_in_paths` lint |
| `lifetimes/issue-95023.rs` | E0183/E0229/E0407/E0220 | feature: manual impl Fn(&isize) for T (parenthesized Fn sugar in impl position) |
| `lifetimes/lifetime-bound-will-change-warning.rs` | E0521 borrowed data escapes | feature: trait-object lifetime bounds (`dyn Fn() + 'a` vs `+ 'static`) — without the contrast there is no program |
| `lifetimes/mismatched-lifetime-syntaxes-details/example-from-issue48686.rs` | lint mismatched_lifetime_syntaxes | (no such lint in Logos) |
| `lifetimes/mismatched-lifetime-syntaxes-details/macro.rs` | mismatched_lifetime_syntaxes lint | feature: macro_rules! + a deny-by-attribute lint |
| `lifetimes/mismatched-lifetime-syntaxes-details/mismatched-lifetime-syntaxes.rs` | lint | feature: the `mismatched_lifetime_syntaxes` lint (no lint infrastructure / `#![deny]`) |
| `lifetimes/mismatched-lifetime-syntaxes-details/missing-lifetime-kind.rs` | lint mismatched_lifetime_syntaxes | (rustc lint, no Logos analogue) |
| `lifetimes/mismatched-lifetime-syntaxes-details/not-tied-to-crate.rs` | mismatched_lifetime_syntaxes | feature: lint-level attributes |
| `lifetimes/mismatched-lifetime-syntaxes-details/path-count.rs` | lint mismatched_lifetime_syntaxes | feature: rustc lifetime-syntax lint |
| `lifetimes/mismatched-lifetime-syntaxes-details/static.rs` | mismatched_lifetime_syntaxes (deny lint) | rustc-specific style lint, not a language rule |
| `lifetimes/missing-lifetime-in-alias.rs` | E0106 + does not fulfill required lifetime | feature: GATs (type Bar<'b>) |
| `lifetimes/missing-lifetime-in-assoc-type-4.rs` | missing lifetime in associated type | (GAT + `impl IntoIterator for &S`) |
| `lifetimes/raw/raw-lt-invalid-raw-id.rs` | `_`/`self`/… cannot be a raw lifetime | raw lifetime syntax `'r#ident` does not exist in Logos |
| `lifetimes/raw/use-of-undeclared-raw-lifetimes.rs` | E0261 | feature: raw lifetimes 'r#fn (lexer) |
| `lifetimes/re-empty-in-error.rs` | higher-ranked lifetime error | `syntax error` — `for<'b>` HRTB where-clause not in the grammar |
| `moves/assign-value-after-at-pattern-issue-145564.rs` | E0381 used binding isn't initialized | `syntax error near 'x'` on `let ref mut x @ _v;` |
| `moves/invalid-suggestions-destructuring-assignment-drop.rs` | E0509 | "invalid assignment target: left side is not an assignable place" — feature: destructuring assignment |
| `moves/move-out-of-slice-2.rs` | E0277 size for `[A]` not known | (no `Box<[T]>`) |
| `moves/pin-mut-reborrow-infer-var-issue-107419.rs` | use of moved value | feature: `Pin` (core.pin absent — module_loader: cannot find package 'core.pin') |
| `moves/suggest-clone-when-some-obligation-is-unmet.rs` | — | needs HashMap + custom BuildHasher + Clone obligation diagnostics |
| `nll/capture-mut-ref.rs` | unused_mut | feature: the `unused_mut` lint + `#![deny]` — Logos has neither |
| `nll/closure-requirements/escape-argument-callee.rs` | closure region | feature: HRTB (`for<'a>` in a where clause is a syntax error) |
| `nll/closure-requirements/propagate-approximated-both-lower-bounds.rs` | lifetime may not live long enough | `F: for<'x,'y> FnMut(Cell<&'a &'x u32>, ..)` — HRTB closure bounds over multiple named regions plus `#![feature(rustc_attrs)]` |
| `nll/closure-requirements/propagate-approximated-ref.rs` | — | feature: higher-ranked `for<'x,'y>` closure bounds + `Cell` invariance; region requirements propagated out of a closure |
| `nll/closure-requirements/propagate-approximated-shorter-to-static-comparing-against-free.rs` | E0521/`a` does not live long enough | feature: higher-ranked closure-signature region approximation (no region inference for closure signatures at all) |
| `nll/closure-requirements/propagate-approximated-shorter-to-static-no-bound.rs` | — | feature: HRTB for<'x,'y> closure bounds + Cell |
| `nll/closure-requirements/propagate-approximated-shorter-to-static-wrong-bound.rs` | E0521 | feature: HRTB + Cell |
| `nll/closure-requirements/propagate-approximated-val.rs` | (region) | propagate-approximated-val.rs: needs HRTB closure signatures relating `'x`/`'y` through |
| `nll/closure-requirements/propagate-fail-to-approximate-longer-wrong-bounds.rs` | — | HRTB region approximation |
| `nll/closure-requirements/propagate-from-trait-match.rs` | `T` may not live long enough | a closure whose type parameter is constrained by `T: Trait<'a>`, i.e. region parameters appearing only in the closure's own generics |
| `nll/coroutine-upvar-mutability.rs` | — | feature: coroutines / yield |
| `nll/ice-106874.rs` | FnOnce-not-general-enough | feature: PhantomData + Rc + HRTB closure bounds |
| `nll/issue-50716.rs` | — | `?Sized` associated type + HRTB where-clause |
| `nll/issue-52213.rs` | — | needs named lifetimes ('a vs 'b on the signature) |
| `nll/issue-52533-1.rs` | lifetime may not live long enough (HRTB) | `impl for<'a,'b,'c> FnOnce(&'a Foo<'a,'b,u32>, &'a Foo<'a,'c,u32>) -> &'a Foo<'a,'b,u32>` — HRTB over three regions in argument-position `impl Trait` |
| `nll/issue-54189.rs` | — | feature: impl for<'r> Fn() -> &'r () (HRTB Fn sugar) |
| `nll/issue-54302-cases.rs` | not-general-enough | feature: HRTB |
| `nll/issue-54779-anon-static-lifetime.rs` | — | feature: dyn Trait type params + ?Sized + Formatter + generic default methods |
| `nll/issue-55511.rs` | — | assoc const with a lifetime param, used as a pattern |
| `nll/issue-55850.rs` | E0515 cannot yield value referencing local | (coroutines) |
| `nll/issue-57265-return-type-wf-check.rs` | temporary value dropped while borrowed | feature: `dyn Any` + `downcast_ref` (core.any absent) |
| `nll/issue-57280-1-flipped.rs` | — | feature: associated const of a lifetime-parameterised type used as a match pattern |
| `nll/issue-57362-2.rs` | (no assoc fn) | issue-57362-2.rs: needs a qualified path over a FN-POINTER type — `<fn(&())>::make_g()` |
| `nll/issue-57843.rs` | — | `for<'a>` trait object + closure sig inference |
| `nll/issue-58299.rs` | lifetime may not live long enough | feature: lifetime-parameterised impl selection in a path/range pattern — `A::<'a>::X` is a syntax error ("syntax error near '::'") |
| `nll/issue-61424.rs` | (lint) unused_mut | feature: lint levels / `#![deny(unused_mut)]`; the refusal is a LINT, not borrowck |
| `nll/issue-73159-rpit-static.rs` | captures lifetime that does not appear in bounds | feature: return-position `impl Trait` lifetime capture (RPIT over a borrowed field does not even infer; "impl Trait return: could not infer concrete return type") |
| `nll/issue-75777.rs` | — | feature: async / Future / Pin<Box<dyn Future>> |
| `nll/issue-97997.rs` | (not general enough) | issue-97997.rs: needs `<fn(&u8) as Foo>::ASSOC`, same missing construct plus assoc-const |
| `nll/issue-98589-closures-relate-named-regions.rs` | — | early/late-bound named regions in closures |
| `nll/issue-98693.rs` | `T` may not live long enough (`for<'a> T: 'a`) | `where for<'a> T: 'a` — a higher-ranked outlives bound on a type parameter |
| `nll/local-outlives-static-via-hrtb.rs` | — | feature: HRTB for<'a> G: Outlives<'a> |
| `nll/mir_check_cast_closure.rs` | — | fn-pointer subtyping over named regions |
| `nll/mir_check_cast_reify.rs` | lifetime may not live long enough | `ReifyFnPointer` casts (`foo as fn(&u32) -> &u32`) with an early-bound `'a: 'a` where-clause |
| `nll/missing-universe-cause-issue-114907.rs` | — | feature: HRTB FnOnce(&()) bounds / universes |
| `nll/nll-anon-to-static.rs` | — | `'static` in a return type |
| `nll/normalization-bounds-error.rs` | lifetime may not live long enough | an associated type normalised through `<&'a () as Visitor<'d>>::Value` where the impl carries `'d: 'a` |
| `nll/polonius/subset-relations.rs` | — | named lifetime subset relations |
| `nll/relate_tys/hr-fn-aau-eq-abu.rs` | — | higher-ranked fn types in Cell |
| `nll/relate_tys/impl-fn-ignore-binder-via-bottom.rs` | implementation of `Y` is not general enough | matching an impl for `fn(T)` against `for<'a> fn(&'a ())`, under `-Zno-leak-check` — higher-ranked fn-pointer subtyping |
| `nll/relate_tys/opaque-hrtb.rs` | — | feature: `impl Trait` return position + `for<'a>` HRTB trait bound |
| `nll/relate_tys/trait-hrtb.rs` | — | feature: Box<dyn for<'a> Foo<'a>> |
| `nll/snocat-regression.rs` | `S` does not live long enough | `where for<'a> &'a S: 'a` — a higher-ranked bound on a reference to a type parameter, captured by a closure |
| `nll/ty-outlives/impl-trait-outlives.rs` | E0309 | feature: a lifetime bound on an `impl Trait` return (`impl Deb + 'a` is a syntax error) |
| `nll/ty-outlives/projection-no-regions-fn.rs` | — | `Box<dyn Trait + 'a>` outlives on a projection |
| `nll/ty-outlives/projection-one-region-closure.rs` | `T` may not live long enough | `<T as Anything<'b>>::AssocType` outliving `'a` — associated-type projections in outlives position |
| `nll/ty-outlives/projection-one-region-trait-bound-closure.rs` | — | feature: associated-type outlives bounds (`T::AssocType: 'a`) + closure region propagation |
| `nll/ty-outlives/projection-where-clause-env-wrong-bound.rs` | — | feature: outlives where-clauses on associated-type projections (<T as Tr<'a>>::Output: 'b) |
| `nll/ty-outlives/projection-where-clause-env-wrong-lifetime.rs` | E0309 | feature: HRTB `for<'x> T: MyTrait<'x>` |
| `nll/ty-outlives/ty-param-closure-outlives-from-return-type.rs` | — | type-param outlives propagated out of a closure |
| `nll/ty-outlives/ty-param-closure-outlives-from-where-clause.rs` | `T` may not live long enough | propagating a `T: 'a` obligation out of a closure to its caller, plus `#![feature(rustc_attrs)]` |
| `nll/type-test-universe.rs` | — | `for<'u> T: 'u` |
| `nll/unused-mut-issue-50343.rs` | lint `unused_mut` (denied) | the `unused_mut` LINT under `#![deny]` — a lint, not a borrow error, and Logos has no lint level attributes |
| `nll/user-annotations/adt-nullary-enums.rs` | — | feature: generic-enum turbofish with user substitutions ::<Cell<&'static u32>> + Cell |
| `nll/user-annotations/ascribed-type-wf.rs` | — | lifetime-parameterised impl + assoc type |
| `nll/user-annotations/constant-in-expr-trait-item-3.rs` | — | assoc const with a lifetime param |
| `nll/user-annotations/dump-adt-brace-struct.rs` | `#[rustc_dump_user_args]` dump | `#[rustc_dump_user_args]` — a rustc-internal debugging attribute; the test's whole output is the dump |
| `nll/user-annotations/dump-fn-method.rs` | — | feature: `#[rustc_dump_user_args]` compiler-internal attribute; the `//~ ERROR` lines are debug dumps, not diagnostics |
| `nll/user-annotations/inherent-associated-constants.rs` | — | feature: lifetime turbofish A::<'a>::IC — measured syntax error (p/q3.logos: "unexpected type node code 131") |
| `nll/user-annotations/method-call.rs` | — | turbofish lifetime annotations |
| `nll/user-annotations/method-ufcs-1.rs` | E0597 (via UFCS `<T as Tr>::m`) | `syntax error near '<'` |
| `nll/user-annotations/method-ufcs-inherent-1.rs` | — | feature: lifetime turbofish A::<'a>::new — same measurement |
| `nll/user-annotations/method-ufcs-inherent-2.rs` | lifetime | feature: `A::<'a>::new::<T>(..)` — a path carrying explicit lifetime args plus a method turbofish is a syntax error |
| `nll/user-annotations/normalization-2.rs` | — | qualified paths + normalization of lifetime-bearing projections |
| `nll/user-annotations/normalization-default.rs` | E0597 (default type param = assoc proj) | a DEFAULT type parameter that is an associated projection (`struct MyTuple<T, U = <&'static () as Trait>::Assoc>`) |
| `nll/user-annotations/normalization-infer.rs` | — | feature: associated-type normalization with inference variables; traits/impls declared inside fn bodies |
| `nll/user-annotations/pattern_substs_on_tuple_enum_variant.rs` | E0597 | syntax error near 'Bar' — lifetime arguments in a PATTERN path (`Foo::Bar::<'static>(z)`) are not parsed |
| `nll/user-annotations/pattern_substs_on_tuple_struct.rs` | — | `Foo::<'static>` pattern substs |
| `regions/closure-in-projection-issue-97405.rs` | assoc type may not live long enough | (async blocks + RPIT `impl Iterator`) |
| `regions/forall-wf-ref-reflexive.rs` | T does not live long enough | (HRTB where-clause `for<'a> &'a T: 'a`) |
| `regions/higher-ranked-implied.rs` | mismatched types | feature: higher-ranked fn-pointer types `for<'a,'b> fn(..)` (u23.logos) |
| `regions/lifetime-not-long-enough-suggestion-regression-test-124563.rs` | E0478 + region errors | (assoc types over lifetime-parameterised impls, `impl FnOnce` arg, PhantomData) |
| `regions/regions-assoc-type-region-bound-in-trait-not-met.rs` | the type does not fulfill the required lifetime | lifetime bound on an ASSOCIATED TYPE (`type Value: 'a;`) is a syntax error; `type Value: Clone;` parses (p/t27c.logos), so it is the lifetime bound specifically |
| `regions/regions-assoc-type-static-bound-in-trait-not-met.rs` | does not fulfill required lifetime | feature: lifetime bound on an associated-type DECLARATION (type Value: 'static; is a syntax error; type Value: Copy; parses) |
| `regions/regions-bounded-method-type-parameters-cross-crate.rs` | lifetime may not live long enough | feature: //@ aux-build cross-crate compilation |
| `regions/regions-implied-bounds-projection-gap-hr-1.rs` | E0277 `for<'z> T: Trait2<'y,'z>` not satisfied | feature: HRTB projection |
| `regions/regions-infer-proc-static-upvar.rs` | `x` does not live long enough | `F: Fn() + 'static` — a lifetime bound MIXED with a trait bound is a syntax error (`T: 'static` alone parses, p/pw3.logos). Without the `'static` the program is legal in rustc too, so it cannot be ported |
| `regions/regions-outlives-projection-container-hrtb.rs` | lifetime may not live long enough | feature: HRTB where-clause where for<'a> T: Trait<'a> (syntax error) |
| `regions/regions-ref-in-fn-arg.rs` | E0515 | feature: box patterns box ref x (syntax error) |
| `regions/regions-wf-trait-object.rs` | E0478 | `syntax error near 'trait'` — a LIFETIME supertrait bound (`trait T<'t>: 't`) is not in the grammar |
| `regions/wf-bound-region-in-local-soundness-issue-148854.rs` | the parameter type `T` may not live long enough | feature: `Rc`/`OnceCell`/`PhantomData` + implied `T: 'static` drop bounds |

## §2 — NOISE: logosc refuses, but not for the upstream reason

These programs DO fail to compile, and that is exactly why they are dangerous:
landed as pins they would read as conformance while asserting something else.
The recorded reason is the diagnostic logosc actually produced.

| upstream | upstream code | what logosc actually said |
|---|---|---|
| `borrowck/borrowck-overloaded-call.rs` | E0502 | "call to undefined function 's'" (a struct value is not callable at a call site) |
| `borrowck/borrowck-reborrow-from-shorter-lived-andmut.rs` | (lifetime may not live long enough) | `call to 'copy_borrowed_ptr' arg 1: variance mismatch — expected &'a mut S<'b>, got &mut S` — refused at the CALL for a signature-shape reason, not the reborrow |
| `borrowck/borrowed-mut-pointer-assign-overflow-on.rs` | E0503/E0506 | same `variance mismatch` over-refusal at the call as t16 |
| `borrowck/deref-and-mut-borrow-conflict.rs` | E0499 | "cannot borrow moved value 's'" (Logos has no implicit reborrow, so `MyPtr{p:s}` moves) |
| `borrowck/immut-function-arguments.rs` | E0594 cannot assign | "write through raw pointer requires unsafe context" + "deref-write: '=' left side must be a pointer or mutable reference" — fires identically with `mut y`, so unrelated to the mutability point |
| `borrowck/issue-64453.rs` | E0507 | "const 'SETTINGS': initializer must be a literal expression…" (const-init restriction, not the move) |
| `borrowck/issue-81365-1.rs` | E0506 | `field read: struct 'Container' has no field 'target_field'` — Logos has no auto-deref through a user `Deref` impl, so the test's borrow never forms |
| `borrowck/issue-81365-7.rs` | E0506 | `field read: struct 'Container' has no field 'target_field'` — autoderef through a user `Deref` in a field path is unsupported, so the borrowck point is never reached |
| `borrowck/non-promotable-static-ref.rs` | E0716 | "const 'G': initializer must be a literal expression…" (no `&'static` in Logos; the plain-temp port admits vacuously) |
| `borrowck/span-semicolon-issue-139049.rs` | E0597 | mlir_gen internal malfunction: "& undefined 'l'" — the block-tail temporary form never reaches borrowck |
| `borrowck/suggestions/overloaded-index-not-mut-but-should-be-mut.rs` | E0596 | "method call: 'String' has no method 'push_str'" — `&mut self` methods are not resolvable at all through a `&T` binding |
| `drop/dropck-normalize-errors.rs` | E0277 the trait bound | rc1 `mlir_gen: internal: unknown field type in 'BDecoder'` (not a trait-bound diagnostic; late codegen ICE) |
| `dropck/dropck_no_diverge_on_nonregular_1.rs` | overflow while adding drop-check rules | **logosc HANGS** — no diagnostic, no object, RSS flat at 186 MB (spinning, not allocating), killed at 5m18s; the 4-line reduction times out at 90 s too |
| `dropck/dropck_no_diverge_on_nonregular_2.rs` | overflow while adding drop-check rules | rc124 — **logosc HANGS >90 s, no diagnostic** |
| `dropck/negative.rs` | negative `Drop` impls are not supported | rc 1 — "impl Drop for NonDrop: missing method 'drop'" |
| `lifetimes/issue-79187-2.rs` | FnOnce/Fn not general enough | `type '\ |
| `lifetimes/issue-79187.rs` | implementation of `FnOnce` is not general enough | "'thing': callable '\ |
| `lifetimes/issue-83753-invalid-associated-type-supertrait-hrtb.rs` | E0229 | `syntax error near 'fn' at line 4 col 1` (assoc-item-constraint syntax unparsed) |
| `lifetimes/late-bound-lifetime-parameters-60622.rs` | cannot specify lifetime arguments explicitly | rc 1 — "unexpected type node code 131" |
| `lifetimes/lifetime-elision-return-type-trait.rs` | trait bound not satisfied | `internal: unknown tagged enum 'Result__void__<error>'` (known #91) |
| `moves/assignment-of-clone-call-on-ref-due-to-missing-bound.rs` | E0308 (clone-on-&T yields &T) | `method call: 'Day' has no method 'clone'` |
| `nll/closure-malformed-projection-input-issue-102800.rs` | implementation of `Trait` is not general enough | "let '_g': type mismatch — expected fn(&'a U::Ty) -> void, got fn ITEM<...>(i64) -> void" (assoc-type projection through a lifetime-parameterised impl is not normalised; nothing to do with higher-ranked subtyping) |
| `nll/closure-requirements/thread_scope_incorrect_implied_bound.rs` | E0310 | "call to 'outlives_hr': could not infer all type arguments — use explicit f::<T>(...) syntax" |
| `nll/continue-after-missing-main.rs` | lifetime may not live long enough / does not live long enough | "let '_x': type mismatch — expected AdaptedMatrixProvider<'od, MP>, got AdaptedMatrixProvider<'od, AdaptedMatrixProvider<'od, MP>>" (a Self-substitution bug in the impl, unrelated to the region error) |
| `nll/issue-31567.rs` | E0713 borrow may still be in use when destructor runs | "cannot return reference to local variable 'v': dangling reference" — p/t15c_nodrop.logos (identical, NO `impl Drop`) refuses identically, so the refusal is an unrelated over-refusal of a reborrow through a by-value param, not the drop-order rule |
| `nll/issue-45696-scribble-on-boxed-borrow.rs` | E0713 | "cannot return reference to local variable 's': dangling reference" — NOT the destructor rule: the no-Drop twin, which rustc ACCEPTS, is refused with the identical shape ("cannot return reference to temporary value"), so this is a blanket over-refusal of any reborrow returned through a by-value param |
| `nll/issue-52534.rs` | E0597 | "call to 'foo' arg 1: expected fn(&i64) -> &i64, got \ |
| `nll/issue-54302.rs` | (not general enough) | "impl DeserializeOwned for T: missing impl Deserialize for T (required by supertrait)" — the HRTB supertrait is not matched at all, so the blanket impl itself is rejected; unrelated to the upstream point |
| `nll/issue-57362-1.rs` | E0599 | "method call: receiver is not a struct (got fn(&i64) -> i64)" — Logos refuses ALL method calls on a fn-pointer receiver, not the HRTB mismatch |
| `nll/issue-57642-higher-ranked-subtype.rs` | E0599 | "return type mismatch — expected fn(&'a void) -> void::G, got &void" (upstream error is 'no associated function make_g found for fn pointer') |
| `nll/issue-69114-static-ty.rs` | E0597 | `const 'FOO': initializer must be a literal expression, simple arithmetic over literals, or an explicit metacall` |
| `nll/type-alias-free-regions.rs` | lifetime | a lifetime-parameterised `type A<'a> = &'a i64;` in a USER package makes the STDLIB fail: "struct 'ChainIter': not generic — cannot accept 3 type arg(s)" ×10 + "specialisation 'fn option_unzip<...>' has no generic counterpart" |
| `nll/user-annotations/method-ufcs-2.rs` | (region) | `syntax error near '<' at line 8 col 13` (UFCS `<T as Tr>::m` unsupported) |
| `regions/issue-28848.rs` | lifetime may not live long enough | `variance mismatch — expected &'a i64, got &'b i64` fired inside `Foo::xmute`, which rustc ACCEPTS |
| `regions/regions-close-object-into-object-2.rs` | E0515 + lifetime may not live long enough | "struct literal 'B' field 'r': expected &dyn A, got &dyn A" (and "…got &&dyn A" for `&*v`) — a fat-pointer type-identity failure, unrelated to the region error |
| `regions/regions-close-object-into-object-5.rs` | E0310 / E0515 | `struct literal 'B' field 'r': expected &dyn A, got &&dyn A` (a type error in the reshape, not the point) |
| `regions/regions-early-bound-error-method.rs` | lifetime may not live long enough | "cannot return reference to local variable 'g2': dangling reference" — an unrelated rule: rustc's point is `&'b` vs `&'a`, ours is that the by-value param `g2` is local |
| `regions/regions-escape-via-trait-or-not.rs` | lifetime may not live long enough | rc 1 — "'with': type '&i64' does not implement trait 'Der' required by parameter 'R'" |
| `regions/transitively-redundant-lifetimes.rs` | lint `redundant_lifetimes` | `use of undeclared lifetime name ''a' in outlives clause` on `fn d<'b: 'a>(self: &'b Self)` — an over-refusal, not the lint |

## §3 — PIN rows that did NOT survive the re-check

⚠ THE RE-CHECK IS THE POINT OF THIS SECTION. A refusal for an unrelated reason
is NOISE wearing a pin's name, and the largest single group below is the one
the import brief predicted: **nine** programs whose upstream error is the
`&`-vs-`&mut` rule (E0596) and whose Logos diagnostic is METHOD RESOLUTION —
`'T' has no method 'm'` — because a `&mut self` method is not resolvable at all
through a `&T` binding, so the borrow rule is never reached. Landing them would
have pinned the method-lookup message to an upstream borrow-check test.

| upstream | upstream code | why it was not landed | family logosc produced |
|---|---|---|---|
| `borrowck/borrowck-assign-to-constants.rs` | E0594 | refuses for a DIFFERENT rule than upstream | IMMASSIGN |
| `borrowck/borrowck-borrow-from-stack-variable.rs` | E0499/E0502/E0596 | PIN row but the program is ADMITTED today | — |
| `borrowck/cannot-borrow-index-output-mutably.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/index-mut-help-with-impl.rs` | E0596 | refuses for a DIFFERENT rule than upstream | TYPEMISMATCH |
| `borrowck/issue-111554.rs` | E0596 | FINDING row, refuses today but for an unrelated rule | TYPEMISMATCH |
| `borrowck/issue-115259-suggest-iter-mut.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/issue-62387-suggest-iter-mut-2.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/issue-62387-suggest-iter-mut.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/issue-81365-4.rs` | E0506 | FINDING row, refuses today but for an unrelated rule | METHODRES |
| `borrowck/issue-91206.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/issue-93078.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/mut-borrow-in-loop.rs` | E0499 | refuses for a DIFFERENT rule than upstream | MOVE |
| `borrowck/or-patterns.rs` | E0502 | FINDING row, refuses today but for an unrelated rule | MOVE |
| `borrowck/suggest-mut-iterator.rs` | E0596 | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/suggestions/overloaded-index-without-indexmut.rs` | E0596 cannot borrow `*y` as mutable behind `&` | refuses for a DIFFERENT rule than upstream | METHODRES |
| `borrowck/writing-to-immutable-vec.rs` | E0596 | refuses for a DIFFERENT rule than upstream | IMMASSIGN |
| `drop/drop-conflicting-impls.rs` | E0119 | refuses, but with no ownership/region diagnostic | — |
| `drop/explicit-call-to-supertrait-dtor.rs` | E0040 explicit use of destructor method | refuses for a DIFFERENT rule than upstream | METHODRES |
| `lifetimes/container-lifetime-error-11374.rs` | E0515 | refuses for a DIFFERENT rule than upstream | REGION, TYPEMISMATCH |
| `nll/closure-captures.rs` | E0594 | refuses for a DIFFERENT rule than upstream | IMMASSIGN |
| `nll/constant-thread-locals-issue-47053.rs` | E0594 | refuses for a DIFFERENT rule than upstream | IMMASSIGN |
| `nll/issue-46023.rs` | E0594 cannot assign to `x`, not declared mutable | refuses for a DIFFERENT rule than upstream | IMMASSIGN |
| `nll/issue-55401.rs` | (region/E0515) | PIN row but the program is ADMITTED today | — |
| `nll/user-annotations/constant-in-expr-inherent-2.rs` | E0597 | FINDING row, refuses today but for an unrelated rule | SYNTAX |
| `regions/region-object-lifetime-5.rs` | E0515 | refuses for a DIFFERENT rule than upstream | REGION, TYPEMISMATCH |
| `regions/regions-name-duplicated.rs` | E0403 name `'a` already used | refuses, but with no ownership/region diagnostic | — |

## §4 — expected to ADMIT under divergence A16 (structural auto-Copy)

⚠ THESE ARE NOT HOLES, AND THE ROOT THAT PUT THEM HERE IS REFUTED. The import
briefs for blocks 2 and 3 carried a known root **R7 — "move tracking is gated
on `impl Drop`"** with an instruction to add `impl Drop` and re-check. R7 is
wrong: adding `impl Drop` moves TWO properties at once — the destructor AND the
Copy verdict — and the flip comes from the second. A struct with no `impl Drop`
and all-Copy fields IS Copy in Logos (`docs/DIVERGENCES.md` §A16, canonised
2026-08-24), so a second use of it is a COPY, not a use-after-move, and
admitting is CORRECT. They are therefore neither pins nor ledger rows.

| upstream | slice | recorded class |
|---|---|---|
| `dropck/drop-with-active-borrows-1.rs` | bck_lifereg/s0 #4 | R7 |
| `moves/borrow-closures-instead-of-move.rs` | bck_nllmoves/s3 #1 | R7 |
| `moves/use_of_moved_value_copy_suggestions.rs` | bck_nllmoves/s5 #7 | R7 |

⚠ MEASURED, AND IT DISAGREES WITH THE BRIEF. The brief records "8 in nllmoves,
1 in lifereg". Only **3** rows across all 26 slice reports carry `R7` in their
class column (nllmoves/s3 #1, nllmoves/s5 #7, lifereg/s0 #4) — the three above.
The other nllmoves rows the brief was counting are `NEW N2` in nllmoves/s3, a
different root entirely (`&'static` annotations are not enforced), and the
"R7 check" section of nllmoves/s7 is a THIRD claim: that nine move PINs in that
slice refuse only because their probe carries an `impl Drop` the upstream test
did not have. That claim is about the ports' SHAPE, not about a hole, and it is
measured separately in §6.

## §5 — programs the COMPILER DOES NOT TERMINATE ON

Four ports never returned; each was killed after the sweep's timeout. They are
on no shelf that runs them — a fixture that hangs is a suite that hangs — and
they are named here because a hang is a finding, not a missing row. All four are
the same shape: **dropck over a NON-REGULAR (polymorphically recursive) type**,
where the drop-glue walk instantiates `S<Box<T>>`, `S<Box<Box<T>>>`, … forever.
Sources kept under `/home/logos/sandbox/bck_lifereg/`.

* `bck_lifereg/s4/ports/t05_nonregular_dropck.logos`
* `bck_lifereg/s4/ports/t05b_min_polyrec.logos`
* `bck_lifereg/s5/p/t05.logos`
* `bck_lifereg/s6/ports/dropck_no_diverge_on_nonregular_3.logos`

## §6 — HOW MANY MOVE PINS RIDE ON `impl Drop` (i.e. on A16)

The s7 slice report claims its nine move PINs "are PINs only because of
`impl Drop`". That is a claim about the whole batch, not about nine rows, and a
claim about a whole batch has to be measured over the whole batch.

CONTROL, run over every landed B167 pin whose `.expected` is in the MOVE family
and whose source declares an `impl Drop`: delete exactly the `impl Drop` block —
brace-balanced, nothing else — and recompile.

| | count |
|---|---|
| move-family B167 pins carrying an `impl Drop` | 75 |
| **FLIP to admitted** once the `impl Drop` is deleted | **68** |
| still refuse without it | 7 |

So 68 of 75 — not nine — are refusals that ride on the auto-Copy
OPT-OUT rather than on move tracking as such. They stay pins: the program as
written IS refused, and rustc refuses it too. But the reading "Logos tracks
moves here" is only true for a type that is not auto-Copy, and the `.expected`
of these fixtures does not say so — this measurement is where that is written
down. The seven that hold without `impl Drop` are the ones whose payload is
non-Copy for another reason (a heap-owning field, a `&mut` field, a `TypeVar`).

The full lists are in `/home/logos/sandbox/land/dropprobe.json`.


## §7 — DEDUPE against the 138 hand-written refusals already in the tree

`tests/imported/fail/{closures,move,nll,regions}` already held 138 refusal tests —
renamed ports plus authored batches (`assign-twice-immut` cites
`ui/borrowck/assign-imm-local-twice`; `dropck-multi-source` is marked B87). NAME
matching is known to be blind to that renaming, so it is reported alongside a
CONTENT method rather than instead of one.

**Method 2 — token skeleton.** Comments, the `package` line and all whitespace are
removed; every identifier becomes `ID` and every literal `LIT`. Two programs that
are the same test modulo naming then hash identically. Near-duplicates are read
off a trigram Jaccard over the same skeleton.

| measurement | count |
|---|---|
| existing hand-written refusals scanned | 138 |
| B167 pins scanned | 413 |
| identical BASENAME | 0 (after the one collision was resolved at naming time with a `-b167` suffix) |
| identical TOKEN SKELETON (true duplicate) | 1 |
| trigram Jaccard >= 0.70 | 20 |

The ONE exact duplicate was `regions/regions-bounds.rs`, already imported by an
earlier batch. Its B167 copy was DELETED rather than landed, which is why the
pin count is 412 and not 413. (The pre-existing fixture asserts only the word
`variance`; the deleted copy asserted the full message. Strengthening someone
else's `.expected` was not part of this landing and is left alone.)

The 20 near-duplicates were READ, not counted: every one is a pair of SHORT
programs whose skeletons coincide because a two-`let`-and-a-move body has few
shapes — e.g. six different upstream tests all sit at J≈0.71–0.78 against the
single existing `closures/closure-double-move-into`. None is a re-port of the
same upstream test: the highest pair, J=0.956, is `regions-infer-not-param.rs`
against the already-imported `regions-bounds.rs` — two DIFFERENT upstream tests
that both reduce to `fn f<'a,'b>(x: S<'a>) -> S<'b>` once the names are erased,
which is precisely the case the skeleton method cannot separate and a reader
can. The pairs are listed verbatim in
`/home/logos/sandbox/land/dedupe.txt`.

⚠ ACCEPTED AND SAID SO: skeleton equality cannot see a port that was rewritten
into a different shape for the same rule. The residue this method cannot
exclude is bounded only by reading, and it was not read program-by-program.

