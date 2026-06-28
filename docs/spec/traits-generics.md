# Traits & Generics

Scope: trait-system and generics rules (domains `trait`, `generic`) of the Logos language specification, extracted from the compiler implementation layers — grammar (`tools/peg_gen`), sema (`src/compiler/sema*`), monomorphization (`src/compiler/mono*`), and codegen (`src/compiler/mlir_gen*`). Each `### ` heading is a stable, linkable rule id; never rename one.

## Trait System

## Trait Definitions

### `trait.def.copy-not-a-supertrait` — Copy is excluded from a trait's supertrait list

When recording a trait's declared supertraits, `Copy` is omitted from the supertrait set (it is an auto/marker bound, not a vtable-bearing supertrait).

**Divergence:** A: Copy treated specially, excluded from supertrait closure.

**Evidence:** `src/compiler/sema_decl.cpp#L1621-L1625`

### `trait.def.duplicate` — Trait uniqueness per package

Two traits with the same name in the same package are an error. A user trait colliding with an already-registered trait of the same bare name from a different package is a distinct trait: the incumbent keeps the bare slot and the newcomer registers only under `pkg::Name`; lookup probes `cur_package_::Name` first.

**Evidence:** `src/compiler/sema_collect.cpp#L2661-L2677`

### `trait.def.modifiers` — Trait definition modifiers and supertraits

`[pub] [auto|unsafe] trait NAME [<params>] [: super + super ...] { items }` defines a trait; the supertrait list after `:` is a `+`-separated list of trait bounds. `auto` and `unsafe` are mutually-exclusive leading modifiers. Generic params, supertraits, both, or neither may be present.

```logos
trait Foo: Bar + Baz { }
auto trait Send { }
unsafe trait Sync { }
pub trait Iterator<T> { }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L819-L876`, `tools/peg_gen/grammars/logos.peg#L828-L829`

### `trait.def.vtable-layout-supertrait-closure` — Trait vtable layout spans the supertrait closure with ordered upcast targets

A trait object's vtable layout is the method order over the trait's supertrait closure, together with an ordered list of upcast target supertraits; this single layout is the canonical slot order used for dispatch and upcasting.

**Evidence:** `src/compiler/sema_decl.cpp#L1626-L1639`

## Trait Methods

### `expr.method.typevar-trait-method-dispatch` — Method on a bounded type parameter resolves via its trait bounds

If the receiver (after peeling one reference/raw-pointer layer) is a type parameter `T` (or, conditionally, an associated-type projection), the method is resolved against the traits in `T`'s declared bounds; the concrete impl is selected later during monomorphization.

**Evidence:** `src/compiler/sema_expr.cpp#L7320-L7340`, `src/compiler/sema_expr.cpp#L7367-L7375`, `src/compiler/sema_expr.cpp#L7439-L7443`

### `trait.method.default-body` — Trait method default body

A trait method that provides a body has a default implementation; impls may omit it and inherit the default.

**Evidence:** `src/compiler/sema_collect.cpp#L2632-L2636`

### `trait.method.disambiguating-mangle` — Same-name same-sig trait methods coexist via trait-qualified mangling

When two distinct traits define a method with identical name and signature on the same target type, both coexist: the colliding methods are re-keyed under a trait-qualified base `<target>__<trait>__<method>`; non-colliding methods keep the plain base.

**Evidence:** `src/compiler/sema_collect.cpp#L4731-L4835`

### `trait.method.inherent-preferred` — Inherent method preferred over same-name trait method

When an inherent method and a trait method share name and signature on a type, the inherent method keeps the plain base (found by concrete-receiver dispatch) and only the trait method is trait-qualified; a `T: Trait`-bounded dispatch resolves to the qualified trait method.

**Evidence:** `src/compiler/sema_collect.cpp#L4776-L4777`, `src/compiler/sema_collect.cpp#L4787-L4801`

### `trait.method.multi-trait-ambiguity` — Method provided by multiple traits is ambiguous

If a method name `m` on type `S` is provided by more than one trait, the plain unqualified call `s.m(...)` is an error; the call must be disambiguated via a trait-bounded generic context or an explicit trait-qualified call.

**Divergence:** A1: collision removes the plain base from the registry; Rust resolves by receiver/inference where unambiguous

**Evidence:** `src/compiler/sema_expr.cpp#L8683-L8700`

### `trait.method.overload-mangling` — Method symbol resolution under overload mangling

A method reference <struct>__<method> resolves to the actual mangled symbol, which may be package-qualified ([pkg.]Base__method) and may carry an overload-mangling suffix __f__<sig> or __g__<sig>; the bare convention name is the fallback when no mangled match exists.

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L228-L274`

### `trait.method.self-receiver` — Trait method self-receiver inference

A trait method's first parameter is the `self` receiver iff it lacks an explicit type (`self`/`&self`/`&mut self`) or is named `self`. For an untyped `&self`/`&mut self`, the receiver type is synthesized as `Self` / `&Self` / `&mut Self` (mut taken from the param's mut marker).

**Evidence:** `src/compiler/sema_collect.cpp#L2563-L2589`

### `trait.method.signatures` — Trait method declarations

A trait item is a method declaration (`[unsafe] fn NAME [<params>] (params) [-> T] [where ...] (block | ';')`) — body-bearing alts give a default impl, `;`-terminated alts are required methods — or an associated type/const. Method names may be `new`/`null` keywords. A `where` clause may follow the signature (before block or `;`); on a default body it gates per-impl default synthesis (skip the default when the bound fails for the impl's concrete type).

```logos
fn next(self) -> Option<Item>;
fn max(self) -> Item where Item: Ord { ... }
fn new() -> Self;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L885-L963`, `tools/peg_gen/grammars/logos.peg#L933-L951`

### `trait.method.trait-typearg-distinct` — Distinct trait type-args mangle distinctly

Two impls of the same trait with different concrete type-arguments (e.g. `impl Trait<u64> for X` vs `impl Trait<u8> for X`) for a same-name+signature method are treated as distinct and mangled with the trait type-arg suffix (`X__Trait$u64__m` vs `X__Trait$u8__m`).

**Evidence:** `src/compiler/sema_collect.cpp#L4742-L4749`, `src/compiler/sema_collect.cpp#L4778-L4782`, `src/compiler/sema_collect.cpp#L4808-L4821`

### `trait.method.where-self-sized` — where Self: Sized excludes method from vtable

A trait method with a `where Self: Sized` bound is flagged requires-sized-self: it is excluded from the trait's vtable and thus does not affect object safety. Other where-bounds on trait type-params are recorded for per-impl default-synthesis gating.

**Evidence:** `src/compiler/sema_collect.cpp#L2599-L2631`

## Associated Items (general)

### `trait.assoc.eq-clause-satisfaction` — Associated-type equality clauses must hold

A bound `Trait<Assoc = Type>` holds for a concrete type only if each expected associated type equals the impl's actual `type Assoc = ...` resolution after substitution.

**Evidence:** `src/compiler/sema_impl.hpp#L3258-L3266`

### `trait.assoc.equality-bound-satisfaction` — Associated-type equality bound satisfaction

A bound of form `T: Trait<Assoc = X>` is satisfied iff the impl of `Trait` for the concrete type defines `type Assoc = Y` with Y type-equal to X. The impl's binding is looked up first for the concrete name, then for the base (template) name. If no direct impl provides Assoc, a matching blanket impl whose bound-trait and extra-bounds are all satisfied supplies its `type Assoc = ...`, with the blanket target type-var substituted by the concrete type and recursively resolved before the equality check.

```logos
K: Primitive => K: HasPrim<P = K::Prim = i32>
```

**Evidence:** `src/compiler/sema.cpp#L3358-L3420`

### `trait.assoc.type-and-const` — Associated type and const declarations in traits

Trait associated items: `type NAME [<params>] [= T] ;` (optional default and optional bound list `: B + B`) declares an associated type; `const NAME : T [= expr] ;` declares an associated const, optionally with a default value.

```logos
type Item;
type Item: Ord = i32;
const N: usize = 0;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L952-L963`

### `trait.assoc.typearg-suffixed-lookup` — Associated-type lookup prefers trait-arg-suffixed key

When resolving an associated type of `Trait` for a target type, if the current impl context fixes the trait's type-arguments, the lookup first tries a key suffixed with those arguments — so two `Trait<T>` impls for one type at distinct `T` register and resolve their associated types independently — falling back to the unsuffixed `Trait::target::aname` key.

**Evidence:** `src/compiler/sema.cpp#L3015-L3029`

## Associated Types & GATs

### `trait.assoc-type.completeness-with-default` — Associated-type completeness with trait default fallback

A non-blanket trait impl must provide every associated type the trait declares; if the impl omits one, the trait's declared default associated type is used, and only the absence of both impl definition and trait default is an error. Blanket impls skip this check (assoc types are per-instantiation).

**Evidence:** `src/compiler/sema_collect.cpp#L3617-L3642`

### `trait.assoc-type.copied-into-impl` — Associated type bindings recorded per impl under a trait-arg-suffixed key

Associated-type bindings of a trait impl are recorded under a key prefixed by `trait_name + trait-arg-suffix + "::" + target + "::"`; blanket impls use the synthetic `$blanket$Trait$Bound$Target` name. The trait-argument suffix disambiguates two `Trait<T>` impls of the same target type (distinct concrete T) so neither erases the other.

**Evidence:** `src/compiler/sema_decl.cpp#L2563-L2585`

### `trait.assoc-type.default-and-gat` — Associated type defaults and GATs

An associated type may carry its own type params (GAT, e.g. `type Item<T>`), trait-bound constraints, and a default RHS (`type Item = i32;`); an impl that omits the assoc type falls back to the declared default.

**Evidence:** `src/compiler/sema_collect.cpp#L2491-L2520`

### `trait.assoc-type.dual-impl-ambiguous-projection` — Ambiguous bare associated-type projection across generic-trait impls

When two impls of a generic trait Trait<T> for one target at distinct T each declare the same associated type, the bare projection `X::Assoc` becomes ambiguous and must be written `<X as Trait<T>>::Assoc`; the unsuffixed projection key is first-impl-wins and is erased once a second distinct-args impl appears so a bare lookup fails.

**Divergence:** G156-1: Rust requires fully-qualified `<X as Trait<T>>::Assoc` for ambiguous projections; Logos matches by erasing the ambiguous bare key.

**Evidence:** `src/compiler/sema_collect.cpp#L3235-L3248`, `src/compiler/sema_collect.cpp#L3281-L3295`

### `trait.assoc-type.dual-trait-typearg-key` — Associated-type lookup keyed by trait type-args when ambiguous

Associated-type resolution for two distinct `Trait<T>` impls of one target type is disambiguated by a `$G<n>$<a1>$..` suffix encoding the concrete trait type-args, preferring the suffixed key when args are known from the current impl context and falling back to the plain key for single/non-generic-trait impls.

**Related:** `mono.mangle.type-args-recursive`

**Evidence:** `src/compiler/sema_impl.hpp#L4282-L4300`

### `trait.assoc-type.duplicate` — Duplicate associated type in impl rejected

An impl block must define each associated type at most once for a given (trait, trait-type-args, target, name); a second definition with the same key is an error.

**Evidence:** `src/compiler/sema_collect.cpp#L3247-L3252`

### `trait.assoc-type.gat-and-default` — Associated types may be generic (GAT) and have a default

A trait associated type may carry its own type-params (`type Item<T>` — a GAT) with bounds, and may declare a default (`type Item = i32;`) used by impls that omit it.

**Evidence:** `src/compiler/sema_impl.hpp#L2618-L2624`

### `trait.assoc-type.gat-arity-match` — Impl GAT arity must match trait declaration

The number of type parameters on an impl's associated type definition must equal the count the trait declares for that associated type.

**Evidence:** `src/compiler/sema_collect.cpp#L3261-L3273`

### `trait.assoc-type.gat-no-shadow-impl-param` — GAT params must not shadow impl type params

A generic associated type's own type parameters (`type Item<T> = ...`) must not share a name with any of the enclosing impl's type parameters.

**Evidence:** `src/compiler/sema_collect.cpp#L3253-L3260`

### `trait.assoc-type.stamp-bound-targs-on-return` — Associated-type return projection carries the bound's concrete trait type-args

If a dispatched trait method's substituted return type is an associated-type projection, its trait name is rewritten to the trait-name suffixed with the bound's concrete trait type-args (`Trait<...>` -> mangled args suffix), so projections from two distinct `impl Trait<T>` for one type, and the caller's declared `-> P::Item`, resolve to the same impl.

**Uncertainty:** Disambiguation among multiple Trait<T> impls; tied to G156-1 trait-type-arg mangling (still narrow per memory).

**Evidence:** `src/compiler/sema_expr.cpp#L7671-L7694`

## Associated Constants

### `trait.assoc-const.accessor-for-generic-projection` — Associated const in concrete trait impl emits a zero-arg accessor

An associated const in a concrete (non-generic, non-blanket) trait impl emits a zero-argument accessor function `Target__kassoc_<name>` returning the const value, so a generic projection `T::<name>` (lowered as `T__kassoc_<name>()` and rewritten by mono once T is substituted) has a concrete function to call. Generic/blanket-target associated consts are not given accessors; concrete `Target::<name>` reads use the direct value path.

**Evidence:** `src/compiler/sema_decl.cpp#L2260-L2295`

### `trait.assoc-const.completeness-with-default` — Associated-constant completeness with trait default fallback

A trait impl must provide every associated constant the trait declares; if omitted, the trait's default value is projected into the impl, and only the absence of both impl value and trait default is an error.

**Evidence:** `src/compiler/sema_collect.cpp#L3643-L3663`

### `trait.assoc-const.default` — Associated const declaration and default

An associated const declares a type, and may provide a default value (`const X: i32 = 42;`); an impl that omits the const falls back to the recorded default.

**Evidence:** `src/compiler/sema_collect.cpp#L2523-L2542`

### `trait.assoc-const.default-projected-to-impl` — Associated const default projected into omitting impls

A trait associated const may declare a default (`const X: i32 = 42;`); each `impl Trait for T` that omits the const inherits the default so `T::CONST` resolves to it.

**Evidence:** `src/compiler/sema_impl.hpp#L2625-L2633`

### `trait.assoc-const.generic-projection` — Associated const projection on a bound type parameter

For a path `T::C` where `T` is a type parameter with bound `T: Tr` and `C` is an associated const declared by `Tr` (or by any transitive supertrait of `Tr`), the expression resolves to a per-impl accessor that yields the const's value; its type is the declared associated-const type, defaulting to i64 when undeclared.

**Uncertainty:** i64 default for an associated const with no declared type is implementation-driven; Rust requires an explicit type.

**Evidence:** `src/compiler/sema_expr.cpp#L11540-L11567`

### `trait.assoc-const.inherent-allowed` — Inherent associated constants permitted

An inherent impl (no trait) may declare associated constants `const C: T = ...;`, registered under the target type.

**Evidence:** `src/compiler/sema_collect.cpp#L3296-L3308`

### `trait.assoc-const.supertrait-closure` — Associated const lookup closes over supertraits

Resolution of an associated const on a bound type parameter searches the transitive supertrait closure of every stated bound, deduplicated, so a const declared on a supertrait of a stated bound is found.

**Evidence:** `src/compiler/sema_expr.cpp#L11544-L11565`

### `trait.assoc-const.type-match` — Impl associated-constant type must match trait declaration

An impl's associated constant must have a type equal to the type the trait declares for that constant.

**Evidence:** `src/compiler/sema_collect.cpp#L3313-L3326`

## Supertraits

### `trait.supertrait.declared-bounds` — Trait may declare supertrait bounds

A trait declaration `trait Foo: Display + Into<i32>` records supertrait bounds (trait name + type-args) that every impl of Foo must also satisfy.

**Evidence:** `src/compiler/sema_impl.hpp#L2644`

### `trait.supertrait.impl-completeness` — Implementing a trait requires implementing its supertraits

For an `impl Trait for T`, an impl of each of Trait's supertraits for T must also exist (transitively); missing supertrait impls are an error. Order of definitions across files does not matter (deferred check). The impl's own canonically-captured trait is used (not a same-named trait).

**Evidence:** `src/compiler/sema_collect.cpp#L4954-L4972`, `src/compiler/sema_collect.cpp#L5013-L5014`

### `trait.supertrait.impl-required` — supertrait impls must be satisfied

For every `impl Trait for T`, the impls of all of Trait's supertraits for T must also be present (checked order-independently after all impls are collected).

**Evidence:** `src/compiler/sema_collect.cpp#L670-L672`

### `trait.supertrait.known` — Supertrait must name a known trait

Every supertrait listed on a trait must refer to a known trait (except the `Copy` marker); otherwise it is an "unknown supertrait" error.

**Evidence:** `src/compiler/sema_collect.cpp#L4928-L4937`, `src/compiler/sema_collect.cpp#L4930`

### `trait.supertrait.via-blanket` — Supertrait satisfied by a blanket impl

A supertrait requirement `T: Super` is satisfied if a blanket `impl<U: Bound> Super for U` exists and T implements `Bound` (and all extra bounds), directly or via another blanket.

**Evidence:** `src/compiler/sema_collect.cpp#L4973-L4991`

### `trait.supertrait.via-self-bound` — Supertrait satisfied by impl's own type-param bound

For a blanket `impl<T: Super> Child for T {}`, the supertrait requirement on T is discharged by the impl's own where-clause bound on T, where the bound trait directly is or transitively reaches the required supertrait via its own supertrait chain.

**Evidence:** `src/compiler/sema_collect.cpp#L4992-L5012`

## Trait Implementations

### `trait.impl.blanket-and-semantics` — Blanket impl bounds combine conjunctively; empty bounds = impl-for-all

A blanket impl is satisfied iff ALL of its bounds hold (primary bound first, then extra bounds, conjoined). A blanket impl with empty primary bound and no extras is an unconditional impl-for-all (always satisfied).

**Evidence:** `src/compiler/mono_clone.cpp#L4926-L4935`

### `trait.impl.blanket-default-method` — Blanket impl inherits trait default methods

For a blanket impl `impl<T: Bound> Trait for T {}`, each non-overridden trait default method is synthesized as a generic fn with Self = the blanket type variable and a blanket-impl entry is recorded so dispatch surfaces it on any concrete receiver satisfying Bound.

**Evidence:** `src/compiler/sema_collect.cpp#L3569-L3600`

### `trait.impl.blanket-detection` — An impl whose target is one of its own type parameters is a blanket impl

A trait impl whose target type equals one of the impl's own type parameters (`impl<T: B> Trait for T`) is a blanket impl. The parameter's first bound becomes the impl's bound trait, and its associated-type equalities together with any additional bounds (and their associated-type equality clauses) are recorded, so the blanket can be instantiated at call sites on any concrete type satisfying those bounds.

**Evidence:** `src/compiler/sema_collect.cpp#L3114-L3141`, `src/compiler/sema_collect.cpp#L3213-L3225`, `src/compiler/sema_decl.cpp#L1968-L2006`

### `trait.impl.blanket-keyed-by-bound` — distinct blanket impls keyed by bound trait

Blanket impl methods register under a synthetic key incorporating the implemented trait, the bound trait, and the target typevar (`$blanket$Trait$Bound$T`), so two blankets of the same trait over different bounds (e.g. `impl<X: Primitive> Tr for X` vs `impl<X: PodRef> Tr for X`) register separately and do not collide with `T::method` lookups on unrelated generics.

**Evidence:** `src/compiler/sema_collect.cpp#L3162-L3173`, `src/compiler/sema_collect.cpp#L3226-L3231`

### `trait.impl.blanket-method-target` — Blanket-impl methods are lowered under a synthetic target name

Methods of a blanket impl are lowered under a synthetic target key `$blanket$<Trait>$<BoundTrait>$<target>` so they do not collide with `T::method` for unrelated generic `T` in the program.

**Evidence:** `src/compiler/sema_decl.cpp#L2121-L2126`

### `trait.impl.default-method-visibility` — Default trait methods inherit trait visibility

A default trait method registered into an impl is marked public (inheriting trait accessibility); all newly-registered overloads of that method are marked pub, not only the first.

**Evidence:** `src/compiler/sema_collect.cpp#L3601-L3608`

### `trait.impl.default-on-scalar-self` — Self bound to scalar primitive for default-method bodies

For `impl Trait for <scalar primitive>` (i8..i128/u8..u128/usize/isize/f32/f64/bool/char), Self resolves to the primitive so inherited default method bodies using `&Self` typecheck; this binding is restricted to scalar kinds and excludes `str` and enum targets, whose defaults keep Self as a type variable.

**Evidence:** `src/compiler/sema_collect.cpp#L3545-L3567`

### `trait.impl.distinct-trait-args-coexist` — Multiple impls of one trait at distinct args coexist

Multiple impls of the same trait name for one Self type at distinct concrete trait type-args (e.g. `From<i8> for i32` and `From<i16> for i32`) are distinct impls; their methods are mangled with the impl's concrete trait type-args so they coexist, and coherence/duplicate-detection keys on `Trait[args]::Target`.

**Evidence:** `src/compiler/sema_impl.hpp#L2950-L2963`, `src/compiler/sema_impl.hpp#L2990-L2993`

### `trait.impl.foreign-private-trait-error` — impl of a foreign private trait is an error

In `impl Trait for T`, if `Trait` resolves to a trait that is not accessible from the impl site (not pub, or module-only and outside its module), the impl is rejected (privacy error). The check fires at the impl site that introduces the foreign trait name.

**Divergence:** Note: §4 module/package visibility model; trait must be pub-accessible to be implemented across package boundaries.

**Evidence:** `src/compiler/sema_collect.cpp#L2685-L2699`

### `trait.impl.method-completeness` — Trait impl must provide every required method

A trait impl must supply an implementation for every trait method lacking a default; a missing non-default method is an error. Methods with a default that are not overridden are auto-registered from the default body.

**Evidence:** `src/compiler/sema_collect.cpp#L3363-L3373`, `src/compiler/sema_collect.cpp#L3502-L3503`, `src/compiler/sema_collect.cpp#L3610-L3613`

### `trait.impl.method-signature-match` — Impl method signature matched against trait by arity and non-receiver param types

An impl method satisfies a trait method when arities agree and each non-receiver parameter type is equal, where a trait parameter that is a type variable or associated-type projection (possibly under &/&mut/*) is treated as polymorphic and matches any concrete impl type; the receiver (param 0) is always skipped.

**Evidence:** `src/compiler/sema_collect.cpp#L3389-L3458`

### `trait.impl.method-template-attach` — Generic-impl methods attach to a matching spec or struct template, not free functions

For an impl carrying its own type parameters, methods are attached to a template the monomorphizer clones: a partial/full struct specialization is preferred when the impl target's type-args pattern-match the spec's patterns (typevar↔typevar positions agree, concrete positions equal); otherwise the base struct template (preferring one in the impl's own package over a same-named struct from another package) is used.

**Evidence:** `src/compiler/sema_decl.cpp#L2076-L2120`

### `trait.impl.method-unsafe-parity` — Impl method unsafe-ness must match trait method

An impl method's `unsafe` qualifier must match the trait method's: a safe trait method cannot be implemented unsafe and vice versa.

**Evidence:** `src/compiler/sema_collect.cpp#L3495-L3501`

### `trait.impl.method-visibility-inherits-trait` — trait-impl methods inherit trait accessibility

Methods in a trait impl take their accessibility from the trait: if the trait is reachable, its impl methods are public (forced is_pub). `pub fn` is disallowed inside trait/trait-impl blocks. Methods of an inherent impl (no trait) keep their explicit pub/private status.

**Evidence:** `src/compiler/sema_collect.cpp#L3175-L3212`

### `trait.impl.negative-impl` — Negative impl asserts a type does not implement a trait

`impl !Trait for X {}` is a negative impl declaring that X does not implement Trait.

**Evidence:** `src/compiler/sema_impl.hpp#L2663`

### `trait.impl.no-standalone-unsafe` — Standalone (inherent) impl cannot be unsafe

`unsafe impl` with no trait (a standalone inherent impl) is an error.

**Evidence:** `src/compiler/sema_collect.cpp#L3707-L3709`

### `trait.impl.params-from-impl-header` — impl-level generic params come from the impl header

An impl's own generic parameters are taken from `impl<...>`: for a trait impl `impl<T> Trait for U<T>` from the impl-type-params position, and for an inherent impl `impl<T> U<T>` from the standalone type-params position. These params (and their lifetime params and outlives bounds) are in scope for the target type, trait args, and all method signatures.

**Evidence:** `src/compiler/sema_collect.cpp#L2710-L2743`, `src/compiler/sema_collect.cpp#L2748-L2778`

### `trait.impl.ref-target-key` — Reference-type impl lookup key is structure-aware

An `impl Trait for &T` / `&mut T` is keyed by structure: prefix `$ref_`/`$mut_ref_` followed by the struct name when the pointee is a (possibly zoned) struct, otherwise followed by the full ref's type string; `&&i32` and `&i32` thus resolve to distinct keys (no `&`-stripping collision).

**Evidence:** `src/compiler/mono_clone.cpp#L4967-L4984`, `src/compiler/mono_clone.cpp#L5011-L5018`

### `trait.impl.self-binding` — Self binds to the impl target type

Within an impl, `Self` denotes the target type: for a concrete specialization (`impl U<i32>`) the fully resolved type with its type args; for a generic impl (`impl<T> U<T>`) the type with TypeVar args (including the type's lifetime params); for a blanket impl (`impl<T:B> Trait for T`) the TypeVar `T`; for a primitive target the primitive type.

**Evidence:** `src/compiler/sema_collect.cpp#L2972-L3063`

### `trait.impl.self-param-defaults-to-self-ref` — self receiver parameter type defaults to a reference to Self

A method receiver parameter with no written type (`self` / `&self` / `&mut self`) has parameter type a reference to Self, mutable iff the receiver is declared mut.

**Uncertainty:** Derived from the visibility-promotion re-walk of params; the primary signature construction is in collect_fn.

**Evidence:** `src/compiler/sema_collect.cpp#L3195-L3204`

### `trait.impl.self-seeding` — Self is seeded to the impl target for shapes lower_fn's name lookup cannot resolve

Within an impl's methods, `Self` denotes the impl target type. The compiler explicitly binds `Self` (over any stale prior binding) for targets whose mangled name is not resolvable by ordinary name lookup: `str` and other unsized self-types → `UnsizedSlice<u8>`/the resolved unsized type; tuple, fn-pointer, and reference targets → that exact type; a concrete-type-arg target with no impl parameters; and blanket impls on a type variable. Sized plain struct/datatype/primitive targets fall through to name lookup.

**Evidence:** `src/compiler/sema_decl.cpp#L2127-L2171`

### `trait.impl.str-is-byte-slice` — str impls are keyed under &[u8]'s wire form

For impl lookup, the concrete type `&[u8]` is the canonical wire form of `str`; a concrete name of `&[u8]` is renamed to `str` before bound checking so impls registered for `str` apply.

**Uncertainty:** Mirrors a legacy enum bound check; direction (str↔&[u8]) is normative for lookup keys only.

**Evidence:** `src/compiler/mono_clone.cpp#L5024-L5028`

### `trait.impl.target-alias-unfold` — impl target unfolds transparent type aliases

When the impl target names a non-generic, non-lifetime-parameterized type alias, the alias is unfolded so the impl's methods register under the aliased concrete type's name (struct/datatype name, or its mangled concrete name when the alias carries type args; slice type string for a slice alias).

**Evidence:** `src/compiler/sema_collect.cpp#L2935-L2955`

### `trait.impl.target-fnptr-erased` — impl for fn-pointer covers all fn-ptrs of an arity

`impl<A,B,C> Trait for fn(A,B)->C` is permitted; because fn-pointers are type-erased to a uniform pointer at the Logos ABI, the impl covers every fn-pointer of the given arity and its methods are collected non-generically (one shared codegen, keyed by arity).

**Divergence:** Logos additive behavior: fn-ptr impls are arity-keyed and non-generic due to fn-ptr type erasure (no per-signature monomorphization).

**Evidence:** `src/compiler/sema_collect.cpp#L2928-L2934`, `src/compiler/sema_collect.cpp#L2963-L2967`

### `trait.impl.target-ref` — impl for reference types

`impl Trait for &T` / `&mut T` is permitted; `&[T]`/`&mut [T]` canonicalize to the fat-pointer slice form and register under the same `$slice$<elem>` key as `impl Trait for [T]` (binding Self to the unsized-slice type); a generic ref-blanket `impl<T> Trait for &T`/`&mut T` keys under a fixed `$ref_$T`/`$mut_ref_$T` sentinel, restricted by coherence to one such impl per trait/ref-shape.

**Divergence:** Note: receiver-shape mangling is a Logos dispatch-implementation detail; observable rule is which reference forms are valid impl targets and that &[T] ≡ [T] for dispatch.

**Evidence:** `src/compiler/sema_collect.cpp#L2818-L2863`

### `trait.impl.target-tuple` — impl for tuple types

`impl Trait for (A, B, ...)` is permitted; the empty tuple `()` is treated as the unit/void target. A variadic form `impl<A...> Trait for (A...)` covers tuples of any arity; otherwise the impl is keyed by arity (with element types when monomorphic).

**Evidence:** `src/compiler/sema_collect.cpp#L2887-L2927`

### `trait.impl.target-unsized` — impl for bare unsized self-types

`impl Trait for [T]`, `impl Trait for dyn Foo`, and `impl Trait for str` are permitted: the bare unsized slice / dyn-trait / str self-type is resolved in unsized-OK context, binding Self to UnsizedSlice / UnsizedDyn (and `str` to UnsizedSlice<u8>) so `&Self` canonicalizes to the corresponding fat pointer.

**Evidence:** `src/compiler/sema_collect.cpp#L2788-L2817`, `src/compiler/sema_collect.cpp#L3037-L3056`

### `trait.impl.trait-arg-resolution` — Impl trait type-args bind the trait's type parameters by position

For `impl Trait<A1,A2,...> for U`, the trait arguments are resolved positionally and each binds the corresponding declared trait type parameter (Self-substitution scope), skipping lifetime arguments. The impl's own resolved trait args (not a coherence-map last-wins entry) determine the `Trait$G..$Args` keying of associated-type impls in monomorphization.

**Evidence:** `src/compiler/sema_decl.cpp#L2009-L2043`

### `trait.impl.trait-type-args-bind` — trait type args bind the trait's parameters

For `impl Trait<X> for U`, the trait's positional type arguments are resolved and bound to the trait's declared type parameters (e.g. `impl Into<i32> for C` binds the `Into` parameter to `i32`), making them available in method signatures. Lifetime arguments at trait position (`impl Trait<'a>`) are collected separately and not treated as type args.

**Divergence:** Note: Logos does not track regions structurally for trait dispatch; trait-position lifetime args are skipped from type-arg resolution.

**Evidence:** `src/compiler/sema_collect.cpp#L3076-L3110`

### `trait.impl.type-code-from-genos-spec` — type_code from a genos-specialization decl propagates through a trait impl with matching args

If a genos specialization `#[type_code=N] genos Trait<Args>;` exists and a type implements `Trait<Args>` (same resolved trait type-args), the implementing struct inherits type_code N, provided the trait itself did not already supply one.

**Evidence:** `src/compiler/sema_decl.cpp#L2044-L2069`

### `trait.impl.type-code-inherit` — Implementing a type-coded trait propagates its type_code to the target struct

If a trait carries `#[type_code=N]` and a type implements it (`impl Trait for S`), the target struct S inherits type_code N unless S already has one. The trait-declared code overrides a hash-derived default. For a generic-instantiation target (no concrete struct found, mangled name contains `$G`), the code is recorded as an instantiation annotation so monomorphization applies it to the cloned concrete struct, and the canonical and mangled names are registered for sema-time `type_code_of` queries.

**Evidence:** `src/compiler/sema_decl.cpp#L1912-L1967`

### `trait.impl.unknown-trait-error` — impl of an undeclared trait is an error

`impl Trait for T` requires `Trait` to be a declared trait, except for the built-in marker traits `Copy` and `Drop`, which are always implementable by name without a visible trait declaration; any other unknown trait name is an error.

**Divergence:** Copy and Drop are treated as compiler built-in marker traits resolvable by name alone (not requiring import/dependency-graph visibility).

**Evidence:** `src/compiler/sema_collect.cpp#L3064-L3072`

### `trait.impl.unsafe-trait-parity` — unsafe trait requires unsafe impl and vice versa

Implementing an unsafe trait requires `unsafe impl`; using `unsafe impl` for a safe non-auto trait is an error.

**Evidence:** `src/compiler/sema_collect.cpp#L3664-L3670`

### `trait.impl.variadic-pack-param` — Variadic trait type-param pack absorbs trailing impl params

If a trait declares a variadic type parameter `A...` used at a method parameter position, an impl may expose any number of concrete parameters from that position onward; each post-pack impl parameter type must equal the corresponding trait-instantiation type-arg (trait_type_args[k - pack_pos]), and the count of post-pack impl params must equal the number of pack instantiation args.

**Divergence:** Fn-family variadic type packs are a Logos extension (no stable Rust equivalent).

**Evidence:** `src/compiler/sema_collect.cpp#L3408-L3492`

### `trait.impl.where-outlives-bounds` — where-clause outlives bounds augment impl params

Lifetime-outlives bounds (`'a: 'b`) and type-outlives bounds (`T: 'a`) written in an impl's where-clause are collected in addition to those in the `impl<...>` header and attached to the matching impl type parameter.

**Evidence:** `src/compiler/sema_collect.cpp#L2748-L2778`

## Impl Methods

### `trait.impl-method.self-binding-by-target-shape` — Self binding for default-method synthesis follows the impl target shape

When synthesising a default method, `Self` is bound to: the impl's structured target pattern when the target has a shaped (non-typevar, non-constvar) type argument; otherwise a generic struct/datatype over the impl's type-parameters as type variables when the impl has type parameters; otherwise the bare struct/datatype type; for a primitive target, the primitive type; and for a blanket impl, a type variable named for the target.

**Evidence:** `src/compiler/sema_decl.cpp#L2379-L2433`

### `trait.impl-method.visibility-inherits-trait` — Trait-impl methods inherit the trait's visibility

A method defined in a trait-impl block (`impl Trait for T`) is always callable wherever the trait is reachable; it is treated as public regardless of declaration. The `pub` modifier is not permitted on trait-impl methods. Inherent-impl methods (no trait) keep their explicit `pub`/private distinction.

**Evidence:** `src/compiler/sema_decl.cpp#L2202-L2208`, `src/compiler/sema_decl.cpp#L2442`

## Default Methods

### `trait.default-method.conditional-on-param-bounds` — Conditional default methods gated by per-method param bounds

A per-method `where` bound whose subject is a trait type-parameter (e.g. `where Item: Ord`) gates default-method synthesis per impl: when an impl substitutes the type-parameter, the bound is rewritten under the substitution and checked; if unsatisfied, the default method is not synthesized for that impl (the method is simply unavailable).

**Evidence:** `src/compiler/sema_impl.hpp#L2600-L2613`

### `trait.default-method.conditional-where-gate` — Default-method per-method where-clause gates synthesis

A trait default method carrying a per-method where-bound `where P: Trait2` is synthesised for a concrete impl only if the impl's concrete trait-argument bound to trait-parameter P satisfies `Trait2`. If the concrete argument is fully concrete and does not implement `Trait2`, default synthesis is silently skipped and the method is unavailable for that impl (conditional-default semantics).

**Evidence:** `src/compiler/sema_decl.cpp#L2317-L2378`

### `trait.default-method.synthesised-when-not-overridden` — Unoverridden trait default methods are synthesised into the impl

For each trait method that has a default body and is not overridden by the impl, the default body is lowered into the impl (with `Self` bound to the impl's target type), so the method is available on the implementing type.

**Evidence:** `src/compiler/sema_decl.cpp#L2304-L2316`

### `trait.default-method.where-bounds-carried-to-mono` — Deferred where-bounds carried as type-expression bounds for mono re-gating

When a default method's where-bounds are deferred, each is carried on the synthesised function as a (subject-type, trait-name) bound where the subject is the impl's trait-argument expressed in the impl's generic terms; monomorphization substitutes the subject with concrete clone arguments and re-gates, rejecting clones whose substituted argument does not implement the required trait.

**Evidence:** `src/compiler/sema_decl.cpp#L2443-L2472`, `src/compiler/sema_decl.cpp#L2494-L2530`

### `trait.default-method.where-gate-deferred-when-typevar` — Where-gate deferred to mono when concrete arg mentions a type variable

If the trait-argument bound to the gated parameter still mentions any type variable (recursively: under references, type args, tuple/closure positions) or an error type, the where-clause gate is undecidable at sema and deferred to monomorphization; the default template is synthesised and re-checked per concrete instantiation. Blanket impls (Self is a type variable) always defer.

**Uncertainty:** Recursion set (pointee/elem/type_args/tuple/closure) read directly; exhaustiveness over all type kinds assumed from the listed traversal.

**Evidence:** `src/compiler/sema_decl.cpp#L2330-L2354`, `src/compiler/sema_decl.cpp#L2366-L2376`

## Coherence

### `trait.coherence.no-overlapping-impl` — Coherence: no two impls of same trait+args for same target

Two non-generic, non-negative impls of the same trait (canonical name) with the same trait type-arguments for the same target type conflict and are an error. The coherence key includes the trait's spelled-out type-args (so `From<i8>` and `From<i16>` for one target do not collide) and uses the canonical scope-resolved trait name (so distinct same-name traits do not collide). Generic impls (with impl type/lifetime params) and negative impls are exempt.

**Evidence:** `src/compiler/sema_collect.cpp#L3729-L3773`

## The Copy Trait

### `trait.copy.auto-derive-conditions` — Auto-Copy for all-Copy fieldless-Drop structs

A struct is automatically Copy (never move-only) iff every field type is Copy and it has no `impl Drop`. Manual `impl Copy` entries take precedence (computed after supertrait-impl checking).

**Evidence:** `src/compiler/sema_impl.hpp#L2051-L2054`, `src/compiler/sema_impl.hpp#L1930-L1930`

### `trait.copy.conditional` — Conditional Copy via Copy-bounded impl params

A generic `impl<P: Copy> Copy for Type<P>` registers conditional Copy: the instance is Copy iff each target type-arg position bound to a Copy-bounded impl parameter is itself Copy. A bound-less param or non-generic target registers Copy unconditionally.

**Evidence:** `src/compiler/sema_collect.cpp#L3678-L3705`

### `trait.copy.conditional-copy-impl` — Conditional Copy depends on Copy of bounded type-args

For a conditional `impl<P: Copy> Copy for Wrapper<P>`, an instance `Wrapper<A>` is Copy iff every type-arg position bound to a Copy-bounded impl parameter is itself Copy (evaluated recursively); otherwise the instance is move-only and a move is not a bitwise copy.

**Evidence:** `src/compiler/sema_impl.hpp#L1934-L1941`

### `trait.copy.register` — impl Copy registers target as a Copy type

`impl Copy for T` registers T as a Copy type (affecting move semantics); `unsafe impl Copy` is an error because Copy is a safe built-in trait.

**Evidence:** `src/compiler/sema_collect.cpp#L3673-L3677`, `src/compiler/sema_collect.cpp#L3702-L3703`

## Built-in / Marker Traits

### `trait.builtin.copy-handle-kinds` — Copy is built-in for bitwise-copyable handle kinds

A `Copy` bound is satisfied without an explicit `impl Copy` for the bitwise-copyable handle kinds: shared reference `&T` (incl. `&dyn Trait`), raw pointer `*const/*mut T`, slice `&[T]`, fn pointer, and trait-object fat pointer. `&mut T` is an exclusive move-only borrow and is NOT Copy (falls through to impl lookup).

**Evidence:** `src/compiler/sema_collect.cpp#L884-L898`

### `trait.builtin.sized-noop` — `Sized` is a builtin no-op bound

`Sized` is a compiler-builtin marker auto-implemented for every size-known concrete type; a `T: Sized` bound is admitted as a no-op and accepted without trait lookup (matching Rust's implicit `Sized`). This positive-bound path is a no-op; the `?Sized` opt-out is handled elsewhere and IS supported (see `generic.bound.relaxed-only-sized`).

**Related:** `generic.bound.relaxed-only-sized`

**Evidence:** `src/compiler/sema_impl.hpp#L1623-L1628`, `src/compiler/sema_collect.cpp#L879-L883`

## Closure Trait Impls

### `trait.closure.fn-family-auto-impl` — Closure types automatically satisfy Fn/FnMut/FnOnce

Every closure type (canonical name beginning with `|`, i.e. `|T1,...| -> R`) is treated as implementing Fn, FnMut, and FnOnce without any explicit `impl`; the trait engine answers satisfies(Fn-family, closure) = true.

**Divergence:** A10

**Evidence:** `src/compiler/mono_clone.cpp#L4943-L4948`

## Trait Bounds (bound)

### `trait.bound.arity` — Trait-bound type-argument arity

A trait bound `T: Tr<A...>` must supply type arguments matching Tr's type-parameter arity: if Tr's last param is variadic, |A| >= |params|-1; else |A| == |params|. A trait with no type params accepts no args. Mismatch is an error at the definition site.

**Evidence:** `src/compiler/sema_impl.hpp#L1637-L1651`

### `trait.bound.auto-trait-structural` — Auto-trait bounds checked structurally, not via impls

When a method type-param bound names an auto trait, satisfaction is decided by structural auto-trait analysis of the concrete type (is_auto_satisfied) rather than by impl lookup.

**Evidence:** `src/compiler/mono_clone.cpp#L5179-L5187`

### `trait.bound.fn-family-intrinsic` — Fn-family bound satisfied by callable shapes intrinsically

A parenthesized Fn/FnMut/FnOnce bound is satisfied intrinsically (no registered impl needed) by any fn-value kind, Closure, TypeVar (deferred to outer mono pass), or Struct/ZonedStruct (struct-with-Fn-impl bridge); any other kind fails the bound.

**Divergence:** A10

**Evidence:** `src/compiler/mono_clone.cpp#L5072-L5081`, `src/compiler/mono_clone.cpp#L5154-L5178`

### `trait.bound.generic-arg-recursion` — Generic instantiation bound satisfaction recurses into type-args

For a generic struct/enum instantiation, satisfying `Concrete<A..>: Trait` requires not only that a matching impl exists by bare name, but also that for each impl type-param, after unifying the impl target pattern against Concrete, every bound on that param holds for the unified argument (recursively). Non-generic / argument-free concrete types are accepted on the bare-name impl check alone.

**Evidence:** `src/compiler/mono_clone.cpp#L4986-L5089`, `src/compiler/mono_clone.cpp#L5036-L5088`

### `trait.bound.unknown-trait` — Trait bound naming an unknown trait is rejected

Every recorded type-parameter trait bound `T: Tr` must name a known trait; an unknown `Tr` is an error at the definition site. Exceptions: the Fn-family bounds (`Fn`/`FnMut`/`FnOnce`) and `Sized` are compiler-builtin and require no user-space trait declaration.

```logos
fn f<T: Nonexistent>() {} // error: unknown trait
```

**Evidence:** `src/compiler/sema_impl.hpp#L1612-L1636`

### `trait.bound.where-clause-gate` — Method where-bounds gate method instantiation

A method with a where-bound `Subject: Trait` is only instantiated for a concrete substitution if the substituted Subject satisfies Trait; if the substituted Subject still contains a TypeVar the check is deferred to an outer mono pass (not a failure). Unsatisfied where-bounds suppress synthesis of that method.

**Evidence:** `src/compiler/mono_clone.cpp#L5091-L5110`

## Trait Bounds (bounds)

### `trait.bounds.dyn-object-self-and-super` — Trait object satisfies its own trait and supertraits

A `dyn Trait` (TraitObject / UnsizedDyn) value satisfies a `T: Bound` bound iff the trait object's trait equals Bound or transitively reaches it through supertraits.

**Evidence:** `src/compiler/sema_collect.cpp#L1273-L1296`

### `trait.bounds.fn-family-by-callable` — Fn-family bounds satisfied by callable types

An `F: Fn*(args)->R` bound is satisfied by any closure or fn-pointer type; by `&F`/`&mut F` whose pointee is a closure, fn-ptr, or Fn-bounded TypeVar (autoderef-invoke); and by a concrete fn-pointer matching an arity-keyed `$fnptr$N` impl. Arity/arg/ret compatibility is enforced at the call site.

**Evidence:** `src/compiler/sema_collect.cpp#L1245-L1272`

### `trait.bounds.generic-struct-base-key` — Generic-struct impl keyed by base struct name

A generic impl `impl<T: X> Trait for GenericStruct<T>` registers under the base name; a bound on a concrete instantiation of that struct is satisfied if a generic impl for its base struct exists (with type-args matching), the impl's own param bounds being validated recursively at monomorphization.

**Evidence:** `src/compiler/sema_collect.cpp#L1181-L1191`

### `trait.bounds.parametrized-type-args-match` — Parametrized bound requires a type-arg-matching impl

For a bound `T: Trait<Args>`, a name-keyed impl hit proves only that SOME Trait impl exists; satisfaction additionally requires that some impl for this Self (enumerated via multi-valued impls registry) has matching trait type-args after substituting the impl's params from the concrete Self. An empty bound type-arg list imposes no constraint. Enumeration covers the concrete, unwrapped, and bare struct/enum names; abstract (TypeVar-bearing) sides defer.

**Evidence:** `src/compiler/sema_collect.cpp#L936-L1007`

### `trait.bounds.partialeq-via-eq` — PartialEq/PartialOrd satisfied by Eq/Ord impls

A `T: PartialEq` bound is satisfied by an existing Eq impl, and `T: PartialOrd` by an Ord impl (alias resolution over concrete and unwrapped names).

**Divergence:** Logos Eq/Ord carry the methods Rust puts on PartialEq/PartialOrd; full split pending.

**Evidence:** `src/compiler/sema_collect.cpp#L1110-L1131`

### `trait.bounds.ref-subject-impl-key` — where &T: Trait satisfied only by reference-Self impl

A `where &T: Trait` bound (ref-subject) is satisfied only by an `impl Trait for &Concrete` / `&mut Concrete` (keyed `$ref_<C>` / `$mut_ref_<C>`), NOT by `impl Trait for Concrete`; absence is an error naming parameter `&<name>`.

**Evidence:** `src/compiler/sema_collect.cpp#L899-L911`

### `trait.bounds.region-mismatch-error` — Found impl with incompatible regions is an error

When a direct/alias impl is found and its type-args match, satisfaction holds only if the region check passes; on region failure the bound is rejected with a diagnostic citing incompatible trait-arg lifetimes (and HRTB `for<...>` binders if present).

**Related:** `region.bounds.universal-lifetime-position`, `region.bounds.impl-tie-injectivity`, `region.bounds.hrtb-outlives-unsat`

**Evidence:** `src/compiler/sema_collect.cpp#L1102-L1154`

### `trait.bounds.tuple-impl-satisfaction` — Tuple impls keyed by arity, with per-element recursion

A tuple satisfies a bound if a variadic tuple impl (`$tuple$variadic`) exists (any arity), or an arity-specific impl (`$tuple$N`) exists AND every non-TypeVar element itself satisfies the trait (element checked via direct impl, nested tuple-arity, or auto-trait short-circuit). TypeVar elements defer to mono.

**Evidence:** `src/compiler/sema_collect.cpp#L1192-L1244`

### `trait.bounds.unsatisfied-error` — Unsatisfiable bound emits a diagnostic

When no satisfaction path applies (direct/alias impl, blanket, generic-struct, tuple, Fn-family, dyn, or reference-Self mangled key), the bound is rejected: `'<target>': type '<C>' does not implement trait '<Trait>' required by parameter '<name>'`.

**Evidence:** `src/compiler/sema_collect.cpp#L1297-L1318`

## Higher-Ranked Trait Bounds

### `trait.hrtb.universal-bijective` — HRTB bound satisfaction requires universal-position, bijective lifetime mapping

When a trait bound carries lifetime type-args, the bound's lifetimes must align with the impl's lifetime params: a bound lifetime that is empty/'static must match exactly or map to a universally-quantified impl lifetime; a named bound lifetime requires the impl side be universal and the impl-lt→bound-lt map be 1-1 (injective). An impl outlives constraint `'a: 'b` where both sides map to bound-side binder (skolem) lifetimes is unsatisfiable and rejected.

**Uncertainty:** Region soundness rule; full statement deferred to sema_collect region_ok per comment.

**Evidence:** `src/compiler/mono_clone.cpp#L5189-L5280`

## Trait Resolution

### `trait.resolve.auto-impl-for-all` — Auto/marker trait holds for all types

An auto-trait T (e.g. marker traits Copy/Send/Sync) holds unconditionally for every type, subject to negative carve-outs.

**Evidence:** `src/compiler/trait_engine.hpp#L49-L55`, `src/compiler/trait_engine.hpp#L83`

### `trait.resolve.blanket-and-bounds` — Blanket impl with conjunctive bounds

A blanket impl `impl<S> T for S where S: B1 + ... + Bn` makes T hold for any type S iff S satisfies ALL bounds B1..Bn (AND-conjunction). An empty bound set degenerates to an unconditional impl-for-all.

**Evidence:** `src/compiler/trait_engine.hpp#L39-L47`, `src/compiler/trait_engine.hpp#L78-L82`

### `trait.resolve.blanket-conjunction` — Blanket impl bounds are an AND-conjunction

A blanket impl blanket(T←{Tb1..Tbn}) derives satisfies(T,X) only if every bound trait Tbi satisfies satisfies(Tbi,X) for the same type X. An empty bound set {} is an unconditional impl-for-all-types of T.

**Evidence:** `src/compiler/trait_engine.cpp#L104-L121`

### `trait.resolve.blanket-first-match` — First fully-satisfied blanket wins

When multiple blanket impls target the same trait T, the first one (in declaration/registration order) whose bounds are all satisfied is selected; remaining candidate blankets are not considered.

**Uncertainty:** Coherence is deferred; overlapping blankets are resolved by order rather than rejected here.

**Evidence:** `src/compiler/trait_engine.cpp#L108-L121`

### `trait.resolve.cycle-terminates-no-impl` — Cyclic blanket bounds resolve to no-impl on the cyclic path

If resolving satisfies(T,X) recursively re-enters the same query (T,X) through a blanket-bound chain, that recursive path yields no impl rather than diverging, allowing outer rules to try alternatives; resolution always terminates.

**Evidence:** `src/compiler/trait_engine.cpp#L89-L96`, `src/compiler/trait_engine.cpp#L14-L17`

### `trait.resolve.derivation-modes` — Trait satisfaction derivation modes

satisfies(T, X) holds iff at least one derivation succeeds: (D) a direct impl fact impls(T,X); (B) a blanket impl blanket(T←{Tb...}) whose every bound Tb satisfies satisfies(Tb,X); (A) an auto impl auto(T) with no negative carve-out; or (S) a shape-auto impl shape_auto(T,S) whose predicate S(X) matches with no negative carve-out.

**Evidence:** `src/compiler/trait_engine.cpp#L98-L148`, `src/compiler/trait_engine.cpp#L151-L153`

### `trait.resolve.derived-impl-distinct-identity` — Derived auto/shape impls get a fresh stable impl identity per (trait,type)

Auto and shape-auto derivations produce a fresh impl identity the first time a given (T,X) pair is queried; that identity is memoized so subsequent queries of the same pair compare equal.

**Evidence:** `src/compiler/trait_engine.cpp#L125-L145`

### `trait.resolve.direct-impl-fact` — Direct impl fact

A declaration `impl T for X` makes X satisfy trait T directly (a direct impl fact keyed by (T, X)).

**Evidence:** `src/compiler/trait_engine.hpp#L31-L37`, `src/compiler/trait_engine.hpp#L76`

### `trait.resolve.direct-impl-idempotent` — Duplicate direct impls collapse to one impl identity

Registering a direct impl for an already-implemented (T,X) pair does not create a new impl; the original impl identity is returned, so impls(T,X) names a single impl.

**Evidence:** `src/compiler/trait_engine.cpp#L28-L37`

### `trait.resolve.fact-monotonic-invalidation` — Adding a fact may flip previous negative results

Adding any impl fact (direct, blanket, auto, shape-auto, or negative) invalidates all previously cached resolution results, because a prior 'no impl' may become satisfiable (or vice versa).

**Evidence:** `src/compiler/trait_engine.cpp#L34`, `src/compiler/trait_engine.cpp#L48`, `src/compiler/trait_engine.cpp#L55`, `src/compiler/trait_engine.cpp#L62`, `src/compiler/trait_engine.cpp#L68`

### `trait.resolve.impl-id-dispatch-selection` — Resolution selects a concrete impl for dispatch

resolve(T, X) yields the impl identity through which a call on X is dispatched, or NO_IMPL when no direct or derived impl exists; satisfaction and dispatch-selection are the same predicate (resolve = NO_IMPL iff not satisfies).

**Evidence:** `src/compiler/trait_engine.hpp#L101-L103`, `src/compiler/trait_engine.hpp#L28-L29`

### `trait.resolve.memoization-stable-result` — Resolution result per (trait,type) is memoized and stable

satisfies(T,X) is a deterministic function of the current fact set: results (including negative/no-impl outcomes) are memoized per (T,X) pair and re-queries return the same answer until the fact set changes.

**Evidence:** `src/compiler/trait_engine.cpp#L85-L87`, `src/compiler/trait_engine.cpp#L99-L148`

### `trait.resolve.negative-overrides` — Negative carve-out beats auto/shape facts

A negative fact `X does NOT implement T` overrides (beats) any auto-impl or shape-auto-impl fact for (T, X); satisfies(T, X) is then false even if an auto/shape rule would otherwise derive it.

**Evidence:** `src/compiler/trait_engine.hpp#L87-L90`

### `trait.resolve.negative-priority` — Negative impls beat all derivations

A negative fact !impls(T,X) makes satisfies(T,X) false unconditionally; it is checked before and overrides direct, blanket, auto and shape-auto derivations.

**Evidence:** `src/compiler/trait_engine.cpp#L82-L83`, `src/compiler/trait_engine.cpp#L66-L70`

### `trait.resolve.priority-order` — Fixed derivation priority order

Resolution tries derivations in strict order: negative carve-out (reject), then direct, then blanket, then auto, then shape-auto. The first kind that succeeds determines the result; later kinds are not consulted.

**Uncertainty:** Phase-1 simple priority ordering; coherence/overlap checks are stated to live elsewhere.

**Evidence:** `src/compiler/trait_engine.cpp#L98-L148`

### `trait.resolve.recursive-bound-cycle-terminates` — Circular bound resolution terminates as unsatisfied

Recursive bound resolution is cycle-guarded: a query (T, X) that re-asks itself through a circular blanket bound resolves that inner query as not-satisfied (NO_IMPL) rather than diverging, and the outer rule then decides.

**Evidence:** `src/compiler/trait_engine.hpp#L145-L148`

### `trait.resolve.satisfies-fixpoint` — Satisfaction by fixpoint closure

satisfies(T, X) is decided by the least fixpoint over {direct, blanket, auto, shape-auto} facts minus negative facts: X satisfies T iff a direct fact (T,X) exists, or some applicable blanket/auto/shape rule derives it (transitively through bound resolution).

**Evidence:** `src/compiler/trait_engine.hpp#L92-L99`, `src/compiler/trait_engine.hpp#L153-L156`

### `trait.resolve.shape-auto-predicate-on-typename` — Shape-auto impls match by a predicate over the type name

A shape-auto impl applies to type X iff its shape predicate evaluates true on X's type name; a shape-auto impl with no predicate never matches.

**Evidence:** `src/compiler/trait_engine.cpp#L136-L145`

### `trait.resolve.shape-conditioned-auto` — Shape-conditioned auto-impl

A shape-conditioned auto-impl makes trait T hold for every type whose structural shape matches a predicate S (e.g. `Fn`-family for every closure type), subject to negative carve-outs.

**Evidence:** `src/compiler/trait_engine.hpp#L57-L69`, `src/compiler/trait_engine.hpp#L84-L85`

## Trait Satisfaction

### `trait.satisfy.blanket-recursive` — Recursive impl satisfaction with blanket chains

A type satisfies a trait if a direct impl exists (by primary or alternate mangled key), or a blanket impl `impl<T: B> Trait for T` applies whose bound trait B (and all extra bounds) are themselves recursively satisfied by the type. A cycle-guard set prevents infinite recursion; each blanket candidate uses a per-attempt copy of the seen-set so a failed sibling does not poison later candidates. An unbounded blanket (`impl<T> Trait for T`) trivially satisfies any type.

**Evidence:** `src/compiler/sema_collect.cpp#L764-L806`

### `trait.satisfy.ref-self-mangling` — Reference-Self impls keyed by $ref_/$mut_ref_ mangling

An `impl Trait for &T` / `&mut T` registers under a mangled key (`$ref_`/`$mut_ref_` prefix); a query whether `&T` impls Trait matches both the full-string pointee form (`$ref_&i32`) and the bare-name pointee form (`$ref_Foo`).

**Evidence:** `src/compiler/sema_collect.cpp#L776-L788`

## Blanket Impls

### `trait.blanket.auto-ref-receiver` — Auto-ref of receiver for &self/&mut self blanket method

When a dispatched blanket method's self parameter is `&self`/`&mut self` but the receiver is a value (not already a ref/ptr), the receiver's address is taken (mutably for `&mut self`).

**Evidence:** `src/compiler/sema_expr.cpp#L6385-L6401`

### `trait.blanket.method-dispatch-unique-or-error` — Blanket-impl method dispatch requires a unique applicable blanket

For `impl<T: Bound> Trait for T { fn m .. }`, a method call on a receiver dispatches via the blanket only when exactly one registered blanket applies to the receiver type; two or more viable overlapping blankets is an ambiguity error. Value blankets (`impl<T> Trait for T`) reach primitive receivers as well as struct receivers.

**Evidence:** `src/compiler/sema_impl.hpp#L3885-L3897`

### `trait.blanket.overlap-ambiguity` — Two applicable distinct blanket impls are ambiguous

If two or more distinct blanket impls of the same method both apply to the receiver, the method call is an ambiguity error naming both impls.

**Evidence:** `src/compiler/sema_expr.cpp#L6336-L6351`

### `trait.blanket.recursive-impl-gating` — Blanket method dispatch gated by recursive bound satisfaction

A blanket impl provides a method for a receiver type only if the receiver satisfies the blanket's primary bound trait (checked recursively, including supertraits) and all extra bounds, AND every associated-type-equality clause on the primary and extra bounds is satisfied for the receiver.

**Evidence:** `src/compiler/sema_expr.cpp#L6296-L6335`

### `trait.blanket.recursive-satisfaction` — Recursive trait satisfaction through blanket-impl chains

A concrete type satisfies a trait if it implements it directly or transitively through any chain of blanket impls whose bounds are recursively satisfied. Cycle detection prevents infinite recursion through cyclic blanket chains, and each candidate is checked with an independent visited set so a failed candidate does not poison sibling candidates. Associated-type-equality clauses are NOT validated by this satisfaction check.

**Evidence:** `src/compiler/sema_impl.hpp#L3579-L3596`

### `trait.blanket.type-param-inference-by-name` — Blanket type-params inferred by name from receiver, args, and return hint

For a dispatched blanket method, the blanket's target type-param (and `Self`) bind to the receiver's concrete type (unwrapped from ref/ptr); remaining type-params appearing only in argument or return position are inferred by unifying parameter types with argument types and, when present, the return type with the call-site return-type hint. Binding is by name, not position.

**Evidence:** `src/compiler/sema_expr.cpp#L6362-L6384`

### `trait.blanket.unbound-param-bail` — Blanket dispatch bails when a type-param is uninferable

If any of a dispatched blanket method's type-params cannot be inferred (e.g. the destination of `x.into()` with no expected-type annotation), the blanket impl does not dispatch; the normal 'cannot resolve' / type-annotation-needed diagnostic fires instead.

**Evidence:** `src/compiler/sema_expr.cpp#L6402-L6417`

## Blanket Impls (synthesis)

### `trait.blanket-impl.synthetic-target-name` — Blanket impl methods lowered under a synthetic target name

Blanket-impl methods and defaults are lowered under the synthetic target `$blanket$Trait$Bound$Target` with `Self` bound to the blanket type variable, producing an LIR template that mono clones per concrete receiver; this avoids collision with `T::method` of any other generic T.

**Evidence:** `src/compiler/sema_decl.cpp#L2308-L2315`, `src/compiler/sema_decl.cpp#L2381-L2382`, `src/compiler/sema_decl.cpp#L2566-L2569`

## Overload Selection

### `trait.overload.candidate-visibility-filter` — Call candidates filtered by package visibility; explicit pkg:: overrides imports

Function-name resolution collects all overloads (concrete and generic) then filters to those visible from the call site: own package, wildcard-imported packages, or empty-package (extern/prelude). A `use pkg from <module>` import deliberately excludes other modules' same-pkg fns. An explicit `pkg::fn(...)` qualifier restricts to that package only with no empty-fallback. Otherwise, if filtering leaves nothing and no deliberate `from`-exclusion occurred, all candidates are returned (synthetic-phase robustness).

**Evidence:** `src/compiler/sema.cpp#L1638-L1700`

### `trait.overload.generic-arity-and-package` — Generic function selection: package-qualifier filter, arity match, own-package preference

Generic-function lookup among an overload set keeps only candidates passing the package-qualifier filter; with an arg count, a candidate matches if its arity equals n (or n>=arity when vararg). An arity match in the current package wins immediately; else the first matching-arity candidate; else a wrong-arity fallback.

**Evidence:** `src/compiler/sema.cpp#L1578-L1608`

### `trait.overload.generic-select-by-arg-shape` — Generic overload selection by substituted-param scoring

When a base name carries multiple generic overloads of equal arity (e.g. `Pin<&T>::new` vs `Pin<&mut T>::new` vs `Pin<Box<T>>::new`), the overload is selected by unifying each candidate's params against the actual arguments and scoring the SUBSTITUTED params: exact match scores higher than coercion-compatible, and any incompatible param rejects the candidate. With fewer than two overloads the first-arity-match (first-wins) behavior is retained.

**Evidence:** `src/compiler/sema_impl.hpp#L3773-L3789`

## Method Lookup

### `trait.lookup.exact-signature-fnitem-coerce` — Exact-signature lookup matches params by equality, with FnItem to FnPtr coercion

Exact-signature function lookup requires equal vararg-ness and param arity, and each param either types_equal or (arg-side FnItem vs candidate FnPtr) compatible. This mirrors the FnItem to FnPtr coercion on the otherwise exact-match path.

**Evidence:** `src/compiler/sema.cpp#L1610-L1636`

## Operator-Trait Overloading

### `trait.binop.enum-eq-impl` — == / != on same-named enums requires structural Eq impl for payload enums

== / != between two enums of the same name route to the enum's eq/ne impl (a 2-param candidate keyed `EnumName__eq`, concrete or generic) when one exists, auto-borrowing operands; a payload-less (C-like) enum without an impl falls through to discriminant comparison, while a payload-carrying enum with no Eq/PartialEq impl is rejected.

**Evidence:** `src/compiler/sema_expr.cpp#L2114-L2192`

### `trait.binop.operator-method-autoref` — Operator-method operands auto-borrowed to match by-ref formals

When the resolved operator-overload method takes an operand by reference (&self / &other), the corresponding value operand is auto-borrowed (addr_of_temp) to match; by-value method formals receive the operand by value unchanged.

**Evidence:** `src/compiler/sema_expr.cpp#L1956-L1988`

### `trait.binop.partial-ord-derive` — Relational ops derive from partial_cmp when direct method absent

For a struct LHS with relational op {<,<=,>,>=}, if the direct lt/le/gt/ge method is not implemented but partial_cmp is, the comparison derives as a.partial_cmp(&b) followed by is_lt/is_le/is_gt/is_ge; when partial_cmp returns Option<Ordering> it routes through cmp_opt_is_<op> (None => false), and when it returns Ordering directly it calls Ordering::is_<op>.

**Divergence:** Mirrors Rust's default PartialOrd lt/le/gt/ge bodies.

**Evidence:** `src/compiler/sema_expr.cpp#L1990-L2055`

### `trait.binop.struct-operator-overload` — Operator overloading on struct LHS desugars to trait method

When the left operand is a struct, the operator desugars to the corresponding trait method: + Add::add, - Sub::sub, * Mul::mul, / Div::div, % Rem::rem, & BitAnd::bitand, | BitOr::bitor, ^ BitXor::bitxor, << Shl::shl, >> Shr::shr, == Eq::eq, != Eq::ne, < Ord::lt, <= Ord::le, > Ord::gt, >= Ord::ge.

**Evidence:** `src/compiler/sema_expr.cpp#L1930-L1958`

### `trait.binop.tuple-eq-impl` — Tuple == / != routes to Eq impl only for non-primitive tuples

== / != between two tuples of equal arity routes to the tuple's Eq eq/ne impl (keyed concrete `$tuple$N$...`, then arity `$tuple$N`, then variadic `$tuple$variadic`) ONLY when at least one field is non-primitive; an all-primitive tuple falls through to per-field value comparison and never requires the Eq trait. Operands are auto-borrowed to &Tuple.

**Divergence:** Primitive-tuple fast path avoids requiring f64:Eq (f64 is PartialEq-only, Rust parity).

**Evidence:** `src/compiler/sema_expr.cpp#L1812-L1928`

### `trait.binop.typevar-eq-bound` — == / != on bounded type variable dispatches to Eq method

== / != where the left operand is a type variable whose bounds (transitively, through supertraits) provide an `eq` method desugar to an auto-ref'd eq/ne method call dispatched after monomorphization; if more than one trait in scope provides `eq`, the call is tagged with trait Eq for disambiguation. Absent an eq-providing bound, falls through to the generic operator check.

**Evidence:** `src/compiler/sema_expr.cpp#L2060-L2112`

## Deref

### `trait.deref.multi-impl-target-match` — Deref impl selected by strict self-type match among multiple impls

When a type carries several Deref impls distinguished by self type-args (e.g. Pin<&T>/Pin<&mut T>/Pin<Box<T>>), the impl whose target pattern strictly unifies-substitutes-and-equals the receiver type is selected; a non-matching impl is used only as a loose fallback.

**Evidence:** `src/compiler/sema_expr.cpp#L123-L158`

## Method Dispatch (bound-provided)

### `trait.method-dispatch.bound-provides-method` — Type-parameter method dispatch via trait bound

A method call `t.m(args)` where `t : T` (T a type parameter) resolves `m` only if some in-scope bound `T: Trait` (or supertrait reference) declares a method named `m`; the chosen trait/method drive the call. If no in-scope bound on T provides `m` and no Deref/DerefMut bound applies, it is an error: "type parameter '<T>' has no trait bound providing method '<m>'".

**Evidence:** `src/compiler/sema_expr.cpp#L7613-L7635`, `src/compiler/sema_expr.cpp#L7822-L7826`

### `trait.method-dispatch.self-subst-from-bound` — Substitute Self and trait type-params from the receiver bound

When dispatching a trait method on receiver of type T, the substitution binds `Self := T` and binds each trait type-parameter to the matching concrete type-argument from the in-scope bound `T: Trait<A0,A1,...>` (positional pairing, type_params[i] := type_args[i] when present). The method return type is computed by applying this substitution.

**Evidence:** `src/compiler/sema_expr.cpp#L7617-L7636`, `src/compiler/sema_expr.cpp#L7670`

## Trait Dispatch

### `trait.dispatch.ambiguous-method-error` — Method matching multiple bound traits is ambiguous

When a method name `m` is provided by two distinct traits reachable from a type parameter's bounds (e.g. `trait Foo: A + B` where both A and B define `m`), the call is an error: method `m` is ambiguous (matches both traits). All supertrait siblings are searched so the ambiguity is detected rather than silently resolving to one.

**Evidence:** `src/compiler/sema_expr.cpp#L7411-L7421`

### `trait.dispatch.assoc-type-nondefault-gate` — Associated-type receiver intercepted only for a non-default method

A method call whose receiver is an associated-type projection `G::R` is dispatched via the assoc-type's declared bounds (`type R: HasId`) only when those bounds supply a NON-default method of that name; if the only provider is a default method (e.g. via a blanket impl), dispatch defers to the ordinary path.

**Evidence:** `src/compiler/sema_expr.cpp#L7341-L7366`, `src/compiler/sema_expr.cpp#L7444-L7458`

### `trait.dispatch.blanket-bound-transitive` — Blanket extension trait holds transitively on a bounded type param

If `impl<U: B> Ext for U {}` exists and the receiver type parameter `T` satisfies bound `B` (directly or via a supertrait of one of its bounds) and all the blanket impl's extra bounds, then `T: Ext` holds and `Ext`'s methods (including defaults) are searched for the call.

**Evidence:** `src/compiler/sema_expr.cpp#L7460-L7485`

### `trait.dispatch.dstref-unsafe` — Method call through &DstStruct requires unsafe unless self-describing

On a `DstRef` receiver of struct S, a method call dispatches `S__<m>` (concrete signature then generic). It requires an `unsafe` context unless S is `#[self_describing]` (tail length recovered in-band), in which case it is safe. No matching method is an error.

**Evidence:** `src/compiler/sema_expr.cpp#L6752-L6794`

### `trait.dispatch.dyn-inherent-first` — Inherent impls on dyn override vtable dispatch

On a `dyn Trait` (or `&dyn Trait` / `&mut dyn Trait`) receiver, an inherent `impl Trait for dyn Foo` method (mangled `$dyn$Foo__<m>`) is tried before vtable dispatch; if found and applicable it is called directly rather than virtually.

**Evidence:** `src/compiler/sema_expr.cpp#L6801-L6856`

### `trait.dispatch.dyn-return-subst` — Self and trait args substituted in dyn return type

The return type of a virtual `dyn Trait` method is computed by substituting `Self`→receiver type plus the owning trait's type/const params from the TraitObject's trait args (read from the peeled `dyn` type, not from a `&dyn` reference whose own type_args are empty), so trait params do not leak unsubstituted to the call site.

**Evidence:** `src/compiler/sema_expr.cpp#L6979-L7001`

### `trait.dispatch.dyn-vtable-supertrait` — Virtual dispatch over the flattened supertrait vtable

Failing inherent resolution, a `dyn Trait` method call dispatches virtually: the method index is its slot in the supertrait-closure vtable layout (supertrait methods occupy real slots). Arg count must equal method params − 1, an unsafe method requires `unsafe`, and arguments are coerced to formal parameter types (arg-to-dyn, implicit reborrow, int widening) with type/variance/int-fit checks. Unknown method is an error.

**Related:** `coerce.arg.dyn-reborrow-widen`

**Evidence:** `src/compiler/sema_expr.cpp#L6857-L6916`, `src/compiler/sema_expr.cpp#L7003-L7010`

### `trait.dispatch.slice-impl` — Method dispatch on slice/str impls

A method call on a slice receiver resolves user `impl Trait for [T]` / `impl Trait for str` methods via sentinel keys, in order: concrete `$slice$<elem>__<m>`, generic blanket `$slice$T__<m>`, and (for `Slice<u8>`) `str__<m>`. A `&[U]` argument whose formal parameter wants a flat `Slice` is reborrowed by stripping its `&`/`&mut` wrapper. If the receiver is a slice but no key matches, it is an error.

**Evidence:** `src/compiler/sema_expr.cpp#L6518-L6613`

### `trait.dispatch.supertrait-dag-search` — Bounded-type method search walks the supertrait DAG

Method lookup on a bounded type parameter searches each bound trait and, transitively, its supertraits (depth-first, with cycle/diamond guarding). Substitutions compose along supertrait references: a supertrait reference's type-args (written in the sub-trait's namespace incl. Self) are resolved through the current substitution and bound to the supertrait's formal params.

**Evidence:** `src/compiler/sema_expr.cpp#L7400-L7437`, `src/compiler/sema_expr.cpp#L7386-L7398`

### `trait.dispatch.tagged-ptr` — Tag-based dispatch on &tagged<TS> Trait

On a `TaggedPtr` receiver carrying tag-system TS and trait T, a method call resolves against T's declared methods (T must exist, method must exist), checks arg count = method param count − 1, requires `unsafe` if the method is unsafe, substitutes `Self`→receiver type in the return type, and emits a tag-dispatched method call (runtime type_code read via TS).

**Evidence:** `src/compiler/sema_expr.cpp#L6699-L6743`

### `trait.dispatch.tuple-impl` — Method dispatch on tuple impls with autoref/autoderef

On a (possibly `&`/`&mut`-wrapped) tuple receiver of arity N, a method call resolves user `impl Trait for (T1..TN)` via keys concrete `$tuple$N$<t1>..$<tN>__<m>` then generic blanket `$tuple$N__<m>`, trying receiver shapes {Self, &Self, &mut Self}. The receiver is auto-referenced (mut per formal) when the formal `self` is a reference but the receiver is a value, or auto-dereferenced when the formal is by-value but the receiver is a reference. No match yields nullopt (falls through to standard diagnostics).

**Evidence:** `src/compiler/sema_expr.cpp#L7018-L7113`

## Static Dispatch

### `trait.static-dispatch.trait-qualified-self-inference` — `Trait::static_method(args)` infers Self from hint or unique bounded type-param

A trait-qualified static call `Trait::method(args)` inside a generic fn resolves Self by: (a) the let-annotation/return hint's concrete type if it implements Trait (keyed on bare struct base or concrete-spec name), emitting `<Type>__<method>` directly; else (b) the unique in-scope type-param whose transitive bound-closure includes Trait, emitting `<param>__<method>` for mono to retarget. Ambiguity (>1 candidate) leaves it unresolved.

**Evidence:** `src/compiler/sema_expr.cpp#L13433-L13525`

### `trait.static-dispatch.via-type-param-bound` — Generic static dispatch `T::method()` through type-param trait bounds

`T::method()` where T is a type-param resolves the static method (first param not Self) by searching the transitive supertrait closure of T's bounds. Single-dispatch traits route through the uniform generic-call resolver (turbofish/arg-infer/return-hint); multi-param traits (`Sum<Item>`) emit empty type-args and an abstract `T__method` symbol that monomorphization retargets to the concrete impl.

**Evidence:** `src/compiler/sema_expr.cpp#L13363-L13432`, `src/compiler/sema_expr.cpp#L13016-L13074`

## Method Mangling

### `trait.method-mangling.bound-targ-suffix` — Trait tag folds concrete bound type-args into a $G<n>$ suffix

When the receiver bound carries concrete trait type-args (`T: MyTrait<u64>`), the trait tag becomes `Trait` + `$G<n>$<args>` suffix, so mono resolves the args-qualified symbol `<Concrete>__MyTrait$G1$u64__method`, distinct from a sibling `impl MyTrait<u8>`.

**Evidence:** `src/compiler/sema_expr.cpp#L7778-L7792`

### `trait.method-mangling.trait-qualified-symbol` — Trait-qualified method symbol when a method name is provided by a trait

When at least one trait declares a method of the given name, the call is tagged with the chosen trait so monomorphization may resolve the trait-qualified symbol `<Concrete>__<Trait>__<method>`; mono falls back to the plain name when no qualified symbol exists. This disambiguates multi-trait collisions and cases where a same-named inherent method occupies the plain symbol.

**Evidence:** `src/compiler/sema_expr.cpp#L7753-L7793`

## Method Type-Argument Propagation

### `trait.method-typeargs.propagate-trait-and-method-params` — Propagate trait-level then method-level type-args to the call

The lowered method call carries type-args in order: first each owning-trait type-parameter (bound from the Self-substitution), then each method-level type-parameter inferred by unifying substituted formal param types against actual argument types. Unbound positions are left null.

**Evidence:** `src/compiler/sema_expr.cpp#L7713-L7746`

## Mangling (collision)

### `trait.mangle.trait-qualified-on-collision` — Trait-qualified method mangling on name collision

When two distinct traits define a same-named method with the same signature on one type, the colliding methods re-key under `<target>__<trait>__<method>`; a plain base `<target>__<method>` with more than one such entry is ambiguous for concrete-receiver dispatch and requires disambiguation.

**Evidence:** `src/compiler/sema_impl.hpp#L2856-L2862`

## Trait Visibility

### `trait.vis.placeholder-carries-pub` — predeclared trait carries its visibility

A trait name predeclared during name-collection carries its declared `pub` visibility, so a cross-package reference resolving before the trait's body is collected sees the correct public/private status.

**Evidence:** `src/compiler/sema_collect.cpp#L447-L475`

## Trait Objects (object)

### `trait.object.object-safety-required` — Trait objects require object-safe traits

A trait used as a trait object (`&dyn`/`*dyn`/`Box<dyn>`) must be object-safe (dyn-compatible, Rust E0038); a non-object-safe trait used this way is an error reported once per offending trait.

**Evidence:** `src/compiler/sema_impl.hpp#L3186-L3191`

## Object Safety

### `trait.object-safety.method-generic-not-dispatchable` — method-level generic methods are not dispatchable through dyn

A trait method whose every implementation carries method-level type parameters (e.g. `fn fold<Acc>(...)`) is not callable through `&dyn Trait`: it occupies a non-dispatchable (empty) vtable slot rather than a method pointer (Rust object-safety rule).

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L909-L931`

### `trait.object-safety.sized-self-method-excluded` — where Self: Sized method excluded from vtable

A trait method with `where Self: Sized` is excluded from the trait's vtable and ignored for object-safety determination.

**Evidence:** `src/compiler/sema_impl.hpp#L2598-L2599`

## Dynamic Dispatch (dyn / vtables)

### `trait.dyn.blanket-impl-vtable-synthesis` — blanket impl supplies dyn vtable for concrete types

When no explicit (trait, type) vtable is registered but the trait has a blanket impl `impl<T> Trait for T`, the concrete type's vtable methods are the blanket instantiations named `<type>__<method>` (possibly package-qualified / `__g__`/`__f__`-suffixed), enabling `&Concrete as &dyn BlanketTrait`.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L1089-L1133`

### `trait.dyn.blanket-vtable-synthesis` — Blanket-impl concrete vtable synthesis

When a concrete type reaches &dyn Trait only via a blanket impl (impl<T> Trait for T), a concrete <Type>__<method> vtable is synthesized on demand from the trait's slot-ordered method names.

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L415-L421`

### `trait.dyn.drop-full-concrete` — drop_in_place runs the full concrete drop

drop_in_place for a dynamically-dispatched value runs the FULL drop of the concrete type (recursive field drops), matching the static drop of that type.

**Related:** `trait.dyn.vtable-drop-slot0`

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L429-L432`

### `trait.dyn.fat-pointer-vtable-dispatch` — &dyn Trait dispatch loads self=data_ptr and method from vtable

A method call on a `&dyn Trait` receiver is dispatched indirectly: the receiver is a fat pointer {data_ptr, vtable_ptr}; the call loads data_ptr as `self`, loads the method function pointer from the vtable, and calls it indirectly with data_ptr followed by the user args.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L1481-L1483`, `src/compiler/mlir_gen_dyn.cpp#L1555-L1574`, `src/compiler/mlir_gen_dyn.cpp#L1576-L1607`

### `trait.dyn.object-safety` — Object-safety (dyn-compatibility) constraints (E0038)

A trait may be used as `dyn Trait` only if every method has a vtable slot. A method is rejected if it: is generic (`fn f<T>`); has no `self` receiver (associated fn); returns `Self` by value; returns `impl Trait` (opaque); takes `Self` by value as a parameter; or takes `impl Trait` as a parameter. A method with a `where Self: Sized` bound is excluded from the vtable and so never affects object-safety. A trait owning a generic associated type (GAT) is also not object-safe. The diagnostic is emitted once per trait.

**Evidence:** `src/compiler/sema.cpp#L3031-L3130`

### `trait.dyn.supertrait-vtable-slots` — Supertrait pointer slots and upcast layout

A dyn Trait vtable carries, after its method slots, a pointer slot per transitive supertrait; an upcast &dyn Sub -> &dyn Super selects the corresponding super-vtable pointer. A trait with no supertraits has no extra slots (unchanged vtable layout).

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L422-L427`

### `trait.dyn.upcast-via-stored-super-vtable` — trait upcast recovers super-vtable from a stored slot

An upcast `&dyn Sub` → `&dyn Super` recovers Super's vtable for the concrete type by loading the super-vtable slot stored at index `3 + |methods| + idx(Super)` of Sub's vtable; each transitive supertrait has exactly one such slot.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L1149-L1161`

### `trait.dyn.vtable-drop-slot0` — vtable slot 0 is drop_in_place glue

Every dyn-Trait vtable's slot 0 is a drop_in_place glue function that runs the concrete type's full Drop; for a non-droppable type it is a no-op.

**Related:** `trait.dyn.drop-full-concrete`

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L429-L436`

### `trait.dyn.vtable-layout-order` — dyn-Trait vtable slot ordering

A dyn-Trait vtable is laid out by post-order DFS over the trait's transitive supertrait graph (deepest deduped ancestors first, root trait's own methods last); each method's position is its vtable slot index. Transitive supertraits (every visited trait except the root, same deepest-first order) each get one stored super-vtable-pointer slot after the methods; the `Copy` marker contributes no vtable.

**Evidence:** `src/compiler/sema_collect.cpp#L4903-L4921`

### `trait.dyn.vtable-static` — dyn-Trait vtables are static, one per (Trait, concrete type)

Each &dyn Trait coercion of a given concrete type uses a single static vtable global ([N x ptr] of method addresses) shared across all coercions; coercion takes the address of that static vtable rather than allocating/filling a fresh vtable per coercion.

**Evidence:** `src/compiler/mlir_gen_impl.hpp#L401-L414`

## Formatting Traits

### `trait.fmt.trait-dispatcher-fn` — Formatting-trait free-fn dispatchers

Format macro lowering dispatches via free-fn wrappers bound at sema-time through a generic trait bound (rather than a `.method()` dot-call): Display=`fmt_display`, Debug=`fmt_debug`, and hex/oct/bin/exp variants named identically to their method names.

**Evidence:** `src/compiler/sema_fmt.cpp#L25-L41`

### `trait.fmt.trait-method-names` — Formatting-trait method names

Each formatting trait maps to a method name used by macro lowering: Display=`fmt`, Debug=`dbg`, LowerHex=`fmt_lower_hex`, UpperHex=`fmt_upper_hex`, Octal=`fmt_octal`, Binary=`fmt_binary`, LowerExp=`fmt_lower_exp`, UpperExp=`fmt_upper_exp`.

**Evidence:** `src/compiler/sema_fmt.cpp#L11-L23`

## Tag-Dispatch Tables (tagdispatch)

### `trait.tagdispatch.dispatch-table-layout` — Tag-dispatch table maps per-type type_code to fn_ptr per (tag_system, trait, method)

Tag-dispatch (an alternative to vtable-based dyn dispatch) is backed by a per-(tag_system, trait, method) dispatch table mapping a per-concrete-type integer `type_code` (logically u64) to the implementing method `fn_ptr`. A `type_code == 0` entry is the unset sentinel meaning 'no impl registered' and is skipped at table build / treated as no-impl at lookup.

**Divergence:** Logos addition: tag-dispatch dispatch model has no Rust analogue (Rust uses fat-pointer vtables only).

**Uncertainty:** type_code allocation policy (which integers map to which types) is defined elsewhere; only the dispatch-table consumption is observable here.

**Related:** `trait.tagdispatch.type-code-keyed`

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L181-L186`, `src/compiler/mlir_gen_dyn.cpp#L302-L321`

### `trait.tagdispatch.registration-uniqueness` — At most one impl per (tag_system, trait, type_code)

Each (tag_system, trait, type_code) registration is unique program-wide: registering the same triple from two separately-compiled units is a hard error (detected as a multiply-defined link symbol). Multiple methods of one trait for one type share a single registration (deduplicated per triple, not per method).

**Divergence:** Logos addition (tag-dispatch); analogous to Rust's coherence/orphan-style uniqueness but enforced at link time.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L188-L214`, `src/compiler/mlir_gen_dyn.cpp#L324-L355`

### `trait.tagdispatch.registry-lookup-api` — Per-triple public dispatch-lookup function

For each (tag_system, trait, method) triple with at least one tier, a public lookup function `type_code -> fn_ptr` is exposed, checking tier-1 (with an in-range guard against the 256 bound) and falling back to tier-2, returning null when no table has the entry. This enables reflective / deferred invocation of trait methods by type_code.

**Divergence:** Logos addition: runtime trait-method registry by type_code has no Rust analogue.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L538-L645`

### `trait.tagdispatch.startup-table-init` — Dispatch tables are populated at program startup

Dispatch tables are zero-initialized statically and filled at program startup before user code runs (one initializer per tag system, invoked from main's prologue). Method bodies observe fully-populated tables; the dispatch tables are not const-folded per call site.

**Divergence:** Logos addition (tag-dispatch).

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L184-L186`, `src/compiler/mlir_gen_dyn.cpp#L434-L530`, `src/compiler/mlir_gen_dyn.cpp#L532-L536`

### `trait.tagdispatch.tier-boundary-256` — Tag dispatch is two-tier with a type-code boundary of 256

Tag dispatch tables are split into a dense tier-1 array of 256 slots indexed directly by type_code, and a tier-2 sparse lookup function. When both exist, dispatch selects tier-1 iff type_code < 256 (unsigned), else calls the tier-2 lookup(type_code); a missing tier resolves to a null function pointer. At least one tier must exist for the call to be emitted.

**Divergence:** Logos addition: tiered type-code dispatch table; no Rust analogue.

**Uncertainty:** Comment text says 'type_code < 223' but the emitted boundary constant is kTier1Size=256; the code is authoritative.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L1287`, `src/compiler/mlir_gen_dyn.cpp#L1366-L1370`, `src/compiler/mlir_gen_dyn.cpp#L1375-L1442`

### `trait.tagdispatch.tier2-binary-search-sorted` — Tier-2 dispatch requires sorted, gap-free codes

Tier-2 dispatch tables list only registrations whose callee is defined; the (type_code, fn) entries are sorted ascending by type_code with no zero/placeholder gaps, and resolution performs an unsigned binary search over type_code returning the paired fn or null on miss.

**Divergence:** Logos addition (tag-dispatch).

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L82-L128`, `src/compiler/mlir_gen_dyn.cpp#L378-L391`

### `trait.tagdispatch.two-tier-codespace` — type_code space is split into two dispatch tiers

The type_code key space is partitioned at 256: codes in [1,255] dispatch via a dense direct-indexed table of fixed size 256 (tier-1, O(1) index); codes >= 256 dispatch via a sorted (type_code, fn) pair table searched by binary search (tier-2, O(log n)). A lookup that hits neither tier yields null (no matching impl).

**Divergence:** Logos addition (tag-dispatch).

**Uncertainty:** Comment at L307 mentions 1-127 inline / 128-255 zone tier-1 sub-ranges, but the dispatch split here only observes the <256 vs >=256 boundary.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L181-L186`, `src/compiler/mlir_gen_dyn.cpp#L239-L240`, `src/compiler/mlir_gen_dyn.cpp#L316-L320`, `src/compiler/mlir_gen_dyn.cpp#L604-L645`

### `trait.tagdispatch.type-code-keyed` — Tag-based dispatch keys on a runtime type-code read from the receiver

A `#[tag_dispatch]`-style trait call resolves the target method at runtime by (1) reading an integer `type_code` from the receiver value via the trait's TagSystem `read_tag(self=null, obj_ptr) -> i64`, then (2) indexing a per-(tag_system, trait, method) dispatch structure by that type_code to obtain the method function pointer, then (3) calling it indirectly with the receiver pointer as `self: *const u8` followed by the user args. The TagSystem is a stateless unit struct (self passed as null).

**Divergence:** Logos addition: runtime type-code/TagSystem dispatch has no direct Rust analogue (Rust uses vtables only).

**Uncertainty:** TagSystem read_tag encoding variants (legacy 2-byte / vlen / TOM inline header) are noted in comments but not enforced in this unit.

**Related:** `trait.tagdispatch.dispatch-table-layout`

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L1308-L1356`, `src/compiler/mlir_gen_dyn.cpp#L1360-L1364`, `src/compiler/mlir_gen_dyn.cpp#L1444-L1474`

## Tag-Dispatch Tables (tag-dispatch)

### `trait.tag-dispatch.entry-emission` — Tag-dispatch entries emitted for concrete impls of tag-dispatched traits

When a trait carries `#[tag_dispatch(TS)]`, the impl is non-generic, and the concrete target datatype has a known nonzero type_code, one dispatch entry (tag-system, trait, method, fn-symbol, type-name, type-code) is emitted for each trait method that is either explicitly overridden or has a default body. The type_code is taken from the struct's annotation-applied code, else an explicit `#[type_code=N]`, else computed from a 56-bit hash of the type's canonical name with values below 128 biased into [128, ...).

**Divergence:** Logos addition: trait tag-dispatch table for zoned/data types has no Rust analogue.

**Evidence:** `src/compiler/sema_decl.cpp#L2590-L2679`

## Unsafe Trait Methods

### `trait.unsafe-method.requires-unsafe-context` — Calling an unsafe method requires an unsafe context

A call to a method marked unsafe outside an unsafe context is an error: "call to unsafe method '<name>' requires unsafe context".

**Evidence:** `src/compiler/sema_expr.cpp#L8025-L8028`

## auto

### `trait.auto.aggregate-structural` — Tuples, structs, and enums auto-satisfy a trait iff all components do

A tuple/struct/enum auto-satisfies an auto-trait (Send/Sync) iff every component type satisfies it: tuple elements, (substituted) struct fields, and every enum-variant payload type. Auto-trait membership is structural over the substituted constituent types.

**Evidence:** `src/compiler/mono_clone.cpp#L237-L289`

### `trait.auto.array-element` — Array satisfies auto trait iff element does

An array type [T; N] satisfies auto trait A iff its element type T satisfies A; an array with no element type vacuously satisfies.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L221-L222`

### `trait.auto.closure-over-captures` — Closure auto-trait membership is computed over capture types

A closure type's auto-trait (Send/Sync) membership is determined by its captured values' types, not its parameter types. By-reference captures enter as `&T`/`&mut T` (so the reference auto-trait rules apply: `&T: Send` iff `T: Sync`); owned (move) captures enter as `T`; narrow captures use the captured field's type. Since closure types intern by signature, the recorded capture set is the union across all same-signature literals, so the type is `!Send`/`!Sync` if any such literal captures a `!Send`/`!Sync` value.

**Evidence:** `src/compiler/sema_expr.cpp#L14900-L14925`

### `trait.auto.closure-via-captures` — Closure auto trait follows its captured types

A closure type satisfies auto trait A iff every captured value's type satisfies A (by-ref captures recorded as &/&mut so reference rules apply). A closure type with no recorded capture set (e.g. a bare dyn Fn annotation without + Send) does not satisfy. Auto-trait satisfaction is computed over captures, not parameter types.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L246-L252`

### `trait.auto.conservative-false` — Other types conservatively fail auto traits

Any type kind not otherwise handled (e.g. trait objects) is conservatively treated as not satisfying any auto trait.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L254-L256`

### `trait.auto.cycle-guard` — Recursive types terminate as satisfied

Auto-trait checking memoizes on key (type, trait); revisiting an in-progress (type, trait) pair during recursion is treated as satisfied (true), so a recursive type does not loop and is not rejected merely for self-reference.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L32-L35`

### `trait.auto.empty-body` — Auto trait must have empty body

An `auto trait` must declare no members; a body containing any FN, associated type, or associated const is an error. Visibility modifiers, type params, and supertraits do not count as members.

**Evidence:** `src/compiler/sema_collect.cpp#L2445-L2461`

### `trait.auto.enum-all-payloads` — Enum satisfies auto trait iff all variant payloads do

Absent an overriding impl, an enum satisfies auto trait A iff every payload type of every variant satisfies A. An unknown enum is leniently treated as satisfying.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L202-L218`

### `trait.auto.explicit-impl-override` — Explicit impl overrides structural check for struct/enum

For struct/enum (and zoned struct) types, an explicit auto-trait impl is consulted first by candidate key (mangled concrete name, then full type string, then base name): a positive impl accepts and a negative impl rejects, short-circuiting the structural field/variant walk.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L37-L85`, `src/compiler/sema_auto_trait.cpp#L168-L170`, `src/compiler/sema_auto_trait.cpp#L203-L205`

### `trait.auto.explicit-impl-overrides` — An explicit impl of an auto-trait short-circuits the structural check

If a type has an explicit impl of the auto-trait (matched by mangled concrete name, type_str form, or unmangled base name), the type satisfies the trait unconditionally, bypassing the structural field/variant walk.

**Evidence:** `src/compiler/mono_clone.cpp#L199-L201`, `src/compiler/mono_clone.cpp#L242-L248`, `src/compiler/mono_clone.cpp#L275-L277`

### `trait.auto.generic-impl-bound-check` — Generic positive impl honours its type-param auto-trait bounds

When a positive auto-trait impl has impl type parameters and a generic target, each impl type param is substituted with the corresponding query type argument; for every bound on that param that names an auto trait, the substituted argument must itself satisfy that auto trait, else the impl is rejected.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L56-L84`

### `trait.auto.mut-ref-send-sync` — &mut T: Send iff T:Send, Sync iff T:Sync

For a mutable reference &mut T, Send(&mut T) = Send(T) and Sync(&mut T) = Sync(T).

**Evidence:** `src/compiler/sema_auto_trait.cpp#L125-L130`

### `trait.auto.mutref-delegates-same-trait` — &mut T: Send iff T: Send, Sync iff T: Sync

&mut T auto-satisfies the queried trait iff T satisfies that same trait (Send→T:Send, Sync→T:Sync). Matches Rust.

**Evidence:** `src/compiler/mono_clone.cpp#L226-L230`

### `trait.auto.phantompinned-not-unpin` — PhantomPinned and #[pinned] types are !Unpin

The lang-item logos.lang.marker.PhantomPinned does not satisfy Unpin; likewise a #[pinned] (arena-resident) struct does not satisfy Unpin. These structural opt-outs propagate via the all-fields rule.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L164-L167`, `src/compiler/sema_auto_trait.cpp#L176`

### `trait.auto.pointer-always-unpin` — Pointers/references are always Unpin

Raw pointers, &T, and &mut T are Unpin regardless of the pointee's pin-ness, unless an explicit negative Unpin impl exists for that exact pointer type; references never carry a negative Unpin impl and are unconditionally Unpin.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L101-L112`, `src/compiler/sema_auto_trait.cpp#L121`, `src/compiler/sema_auto_trait.cpp#L126`

### `trait.auto.raw-pointer-not-send-sync` — Raw pointers are !Send/!Sync absent an explicit impl

A raw pointer type (*const T / *mut T) does not satisfy a Send/Sync-shaped auto trait unless an explicit positive impl exists for that exact pointer type; a matching explicit impl is honoured (positive accepts, negative rejects).

**Evidence:** `src/compiler/sema_auto_trait.cpp#L107-L117`

### `trait.auto.raw-ptr-not-send-sync` — Raw pointers are !Send and !Sync absent an explicit impl

A raw pointer type *const T / *mut T does NOT auto-satisfy Send or Sync; it satisfies them only if an explicit (unsafe) impl is present for that pointer type. Matches Rust.

**Evidence:** `src/compiler/mono_clone.cpp#L216-L220`

### `trait.auto.recursive-structural-satisfaction` — Auto-trait satisfaction is recursive/structural

A type satisfies an auto trait (Send/Sync) iff all of its constituent fields recursively satisfy it; the first non-satisfying field is reported as the offender.

**Evidence:** `src/compiler/sema_impl.hpp#L3617-L3629`

### `trait.auto.ref-send-iff-pointee-sync` — &T: Send/Sync iff T: Sync

&T auto-satisfies Send iff T: Sync, and Sync iff T: Sync — i.e. the auto-trait obligation on &T is delegated to T: Sync for both Send and Sync. Matches Rust.

**Evidence:** `src/compiler/mono_clone.cpp#L222-L224`

### `trait.auto.scalars-and-fnptr` — Scalars and function pointers satisfy all auto traits

Every scalar type (bool, unit/void, iN/uN incl. i24/u24/i56/u56/i128/u128, f32/f64, integer/float literal types), function items, and function pointers satisfy every auto trait unconditionally.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L89-L99`

### `trait.auto.scalars-fn-always-satisfy` — Scalars and function types unconditionally auto-satisfy Send/Sync

Primitive scalar types (bool, all integer widths, f32/f64, char), integer/float literals, and function-item / function-pointer types unconditionally satisfy auto-traits (Send/Sync).

**Evidence:** `src/compiler/mono_clone.cpp#L204-L214`

### `trait.auto.shared-ref-via-sync` — &T auto-trait reduces to T: Sync

For a shared reference &T, both Send and Sync are satisfied iff T: Sync. (Send(&T) = Sync(T), Sync(&T) = Sync(T).)

**Evidence:** `src/compiler/sema_auto_trait.cpp#L120-L122`

### `trait.auto.slice-send-iff-elem-sync` — Slice auto-trait delegates to element Sync; array preserves the trait

[T] (slice) auto-satisfies the queried trait iff T: Sync (slice behaves like &-borrowed element storage). [T; N] (array) auto-satisfies the queried trait iff T satisfies that same trait.

**Uncertainty:** Slice→Sync delegation for BOTH Send and Sync queries is inferred from the literal "Sync" argument regardless of trait_name; Rust treats [T] like &-storage.

**Evidence:** `src/compiler/mono_clone.cpp#L232-L235`

### `trait.auto.slice-via-sync` — Slice auto-trait reduces to element Sync

A slice type [T] (a shared-reference-shaped view) satisfies Send and Sync iff its element type T satisfies Sync (not the queried trait); an empty-element slice vacuously satisfies.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L224-L227`

### `trait.auto.struct-all-fields` — Struct satisfies auto trait iff all fields do

Absent an overriding impl, a struct/zoned-struct satisfies auto trait A iff every field type satisfies A, with generic field TypeVars substituted by the struct's concrete type arguments. An unknown struct (no struct/datatype info) is leniently treated as satisfying.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L171-L198`

### `trait.auto.structural-satisfaction` — Auto traits are satisfied structurally

An auto trait (e.g. Send, Sync, Unpin) is satisfied by a concrete type via structural recursion over its composition (its field/element types) rather than by an explicit impl, except where an explicit (possibly negative) impl overrides. On failure the offending field (name + type) is reported when known, otherwise that the type is not inherently the trait. The error type and the unit/never-shaped Error kind vacuously satisfy every auto trait.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L24-L30`, `src/compiler/sema_auto_trait.cpp#L87-L257`, `src/compiler/sema_collect.cpp#L912-L933`

### `trait.auto.tuple-all-elements` — Tuple satisfies auto trait iff all elements do

A tuple type satisfies auto trait A iff every element type satisfies A.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L230-L233`

### `trait.auto.typevar-via-bounds` — Type parameter satisfies an auto trait iff bounded by it

A type variable T satisfies auto trait A iff A appears in T's declared bound list in the current generic context; otherwise it does not.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L133-L140`

### `trait.auto.unpin-default-true` — Unpin is satisfied by default

Unpin holds for all types except those that (transitively) contain PhantomPinned, are #[pinned], or carry an explicit negative Unpin impl.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L101-L112`, `src/compiler/sema_auto_trait.cpp#L164-L176`

### `trait.auto.unsafecell-not-sync` — UnsafeCell<T> is !Sync; Send follows T

The lang-item logos.lang.cell.UnsafeCell<T> never satisfies Sync; it satisfies Send iff its wrapped T satisfies Send (no arg => not Send). Recognition is by qualified name to avoid clashing with same-named user types.

**Evidence:** `src/compiler/sema_auto_trait.cpp#L145-L160`

## Generics

## Generic Parameters

### `generic.param.bounds-and-const` — type-parameter and const-parameter forms

A type parameter is `NAME [: bound + bound + ...]`; a const generic parameter is `const NAME : TYPE`. Either may be marked variadic with `...`. Bounds are joined with `+`.

**Divergence:** Variadic type/const parameters (`...`) are a Logos extension.

**Evidence:** `src/compiler/sema_render.cpp#L1052-L1099`

### `generic.param.const-generic` — Const generic parameters

A type parameter list may contain const parameters `const N: T`; each carries a const value-type T and may be marked variadic.

```logos
fn f<const N: usize>() -> [i32; N] {}
```

**Evidence:** `src/compiler/sema.cpp#L4070-L4080`, `src/compiler/sema.cpp#L4147-L4158`

### `generic.param.default-type-arg` — Default type arguments

A type parameter may declare a default `<T = Default>` (or `<T: Bound = Default>`); the default type is recorded and substituted at use sites when the argument is omitted.

**Evidence:** `src/compiler/sema.cpp#L4105-L4112`, `src/compiler/sema.cpp#L4180-L4187`

### `generic.param.implicit-sized` — Type parameters are implicitly Sized

Every type parameter carries an implicit `Sized` bound by default; it is cleared only by an explicit `?Sized` relaxed bound.

**Evidence:** `src/compiler/sema.cpp#L3938-L3946`

### `generic.param.lexical-shadowing` — Type/const params shadow outer same-named params

Pushing a scope of type parameters shadows any outer binding of the same name (type, bounds, and `?Sized` relaxation), and popping restores the prior binding exactly; this permits e.g. a method `<T>` to shadow an enclosing trait/impl `<T>`.

**Evidence:** `src/compiler/sema_impl.hpp#L3268-L3331`

### `generic.param.sibling-in-scope` — Sibling type-params visible to bound argument resolution

When resolving a type-parameter's bound arguments, all sibling type-param names in the same list are in scope as type variables (so `where F: FnOnce(T, T) -> bool` resolves T).

**Evidence:** `src/compiler/sema.cpp#L4055-L4066`, `src/compiler/sema.cpp#L4131-L4142`

### `generic.param.unused-warn` — unused function type-parameter warns

A function type-parameter that appears nowhere in the function's signature is a warning.

**Evidence:** `src/compiler/sema_collect.cpp#L552-L554`

### `generic.param.variadic-last` — Variadic type parameter must be last

A variadic type parameter `T...` must be the final entry in the type-parameter list; a non-final variadic param is an error "variadic type parameter must be last".

**Divergence:** Variadic type/const parameters are a Logos addition not present in Rust.

**Evidence:** `src/compiler/sema.cpp#L4188-L4190`

## Generic Functions

### `generic.fn.cross-pkg-coexist` — Cross-package same-name generics coexist

Generic functions are keyed by package-qualified mangled symbol; same base+signature in different packages produce distinct symbols and coexist; a duplicate error fires only on an exact symbol-name match within a package.

**Evidence:** `src/compiler/sema_collect.cpp#L4837-L4856`

### `generic.fn.lifetime-param-unique` — Lifetime parameters of a fn must be unique

A function's lifetime parameter names must be pairwise distinct; a duplicate lifetime parameter is ill-formed.

**Evidence:** `src/compiler/sema_decl.cpp#L463-L467`

### `generic.fn.type-param-unique` — Type parameters of a fn must be unique

A function's type parameter names must be pairwise distinct; a duplicate type parameter is ill-formed.

**Evidence:** `src/compiler/sema_decl.cpp#L521-L524`

## Generic Bounds (bound)

### `generic.bound.assoc-eq` — Associated-type equality constraints in bounds

A trait bound may bind associated types by equality, `Trait<Assoc = Ty>`; each `Assoc = Ty` is recorded as an associated-type equality on the bound.

```logos
fn f<I: Iterator<Item = i32>>(i: I) {}
```

**Evidence:** `src/compiler/sema.cpp#L4025-L4033`

### `generic.bound.fn-family-paren-form` — Fn-family parenthesized bound syntax

The traits Fn, FnMut, FnOnce admit a parenthesized bound form `Fn(P1, ..., Pn) -> R`; the parenthesized list supplies the argument types and `-> R` the return type (both optional), distinct from the `<...>` type-argument list.

```logos
fn call<F: FnOnce(i32, i32) -> bool>(f: F) {}
```

**Evidence:** `src/compiler/sema.cpp#L3969-L3992`

### `generic.bound.hrtb-binder` — Higher-ranked trait bound binders

A trait bound may carry a `for<'a, 'b, ...>` higher-ranked lifetime binder; the bound lifetime names are recorded on the bound.

```logos
fn f<F: for<'a> Fn(&'a i32)>(f: F) {}
```

**Evidence:** `src/compiler/sema.cpp#L3994-L4017`

### `generic.bound.lifetime-arg-not-structural` — Lifetime args in trait bounds are recorded but not dispatched on

A lifetime argument at a trait bound's type-argument position (e.g. `Foo<'a>`) is captured for record only; regions are not tracked structurally for bound dispatch.

**Divergence:** Logos does not track regions structurally for bound dispatch; lifetime bound-args carry no dispatch significance.

**Evidence:** `src/compiler/sema.cpp#L4034-L4041`

### `generic.bound.lifetime-outlives-clause` — Lifetime outlives bounds in generic param list

A lifetime parameter may carry outlives bounds `'long: 'a + 'b + 'c`, which desugar to the set of pairwise constraints {('long,'a), ('long,'b), ('long,'c)} meaning 'long outlives each listed shorter lifetime.

**Uncertainty:** Encoding read here; enforcement of the outlives relation is elsewhere.

**Evidence:** `src/compiler/sema.cpp#L3324-L3351`

### `generic.bound.relaxed-not-propagated` — Relaxed markers are consumed, never positive bounds

A relaxed `?Trait` marker is removed from a type parameter's bound set during finalization; it is never carried forward as a positive bound to monomorphization or bound-checking.

**Evidence:** `src/compiler/sema.cpp#L3937-L3954`

### `generic.bound.relaxed-only-sized` — Only ?Sized is a permitted relaxed bound

A relaxed bound `?Trait` on a type parameter is permitted only when Trait = Sized. `?Sized` clears the parameter's implicit Sized bound; any other `?Trait` is a hard error "relaxed bound '?T' is not permitted (only `?Sized` is supported)".

```logos
fn f<T: ?Sized>(x: &T) {}
fn g<T: ?Clone>() {}  // error
```

**Evidence:** `src/compiler/sema.cpp#L3944-L3954`, `src/compiler/sema.cpp#L3957-L3968`

### `generic.bound.type-outlives` — Type-outlives bounds

A type parameter may carry type-outlives bounds `T: 'a (+ 'b)*`; the outlived lifetime names are recorded on the parameter.

```logos
fn f<T: 'static>(x: T) {}
```

**Evidence:** `src/compiler/sema.cpp#L4098-L4102`

### `generic.bound.well-formed` — trait bounds are validated at the declaration site

Trait bounds written on generic declarations are validated where written: each bound must name a known trait and supply the correct number of trait arguments.

**Evidence:** `src/compiler/sema_collect.cpp#L548-L550`

## Generic Bounds (bounds)

### `generic.bounds.defer-typevar-bearing` — TypeVar-bearing subjects defer bound checking to mono

A concrete-arg type-expression that mentions a TypeVar anywhere (`&T`, `[T;0]`, `&[T]`, `EnumPair<T>`, `(T,U)`, closure param/ret) has undecidable trait satisfaction here and is deferred to monomorphization, where the substituted form is re-checked. AssocType and CfgSlotType kinds are likewise deferred.

**Evidence:** `src/compiler/sema_collect.cpp#L838-L866`

### `generic.bounds.no-empty-params` — Bound checking is a no-op for non-generic targets

When a target has no type parameters, bound checking is skipped entirely.

**Evidence:** `src/compiler/sema_collect.cpp#L808-L811`

### `generic.bounds.relaxed-only-sized` — Only ?Sized is a permitted relaxed bound

A relaxed bound (`?Trait`) is permitted only for `Sized`; any other relaxed bound is an error. Seeing `?Sized` clears the parameter's implicit `Sized` requirement, and the relaxed entry is removed so downstream code sees only positive bounds.

**Evidence:** `src/compiler/sema_impl.hpp#L3248-L3256`, `src/compiler/sema_impl.hpp#L3310-L3316`

### `generic.bounds.relaxed-sized-substitution-check` — ?Sized param substituted where Sized required errors

When a type parameter carrying `?Sized` is passed as a type-argument to a callee whose corresponding parameter requires `Sized`, the same unsized-substitution diagnostic is emitted as for an explicit unsized substitution.

**Evidence:** `src/compiler/sema_impl.hpp#L2996-L3001`

### `generic.bounds.substitute-call-args` — Call's type-args substituted into parametrized bounds

Before checking a parametrized bound `I: Iterator<T>`, the call's mapping of type-params to concrete args is substituted into the bound's type-args, so the bound is checked against the concrete value of T (e.g. turbofish T=i32) rather than the bare TypeVar.

**Evidence:** `src/compiler/sema_collect.cpp#L815-L825`, `src/compiler/sema_collect.cpp#L947-L955`

### `generic.bounds.variadic-tail-param` — Variadic tail parameter absorbs extra type args

If the last type parameter is variadic, type args beyond the non-variadic count are all checked against that final (variadic) parameter; otherwise excess args are ignored once parameters are exhausted.

**Divergence:** A6

**Evidence:** `src/compiler/sema_collect.cpp#L812-L833`

## Where-Clauses

### `generic.where.concrete-subject-obligation` — where-clause with concrete subject is an obligation, not a param

A `where <ConcreteType>: Trait` clause (subject names a known type, e.g. `where i32: Show`) is a trivially-checked obligation and does not introduce a new type parameter; only a genuinely-undeclared type-param name in a where clause is added as a parameter.

**Evidence:** `src/compiler/sema.cpp#L4260-L4279`

### `generic.where.merged-with-inline` — where-clause bounds merge into parameter bounds

Bounds from a `where T: Trait, U: Trait2` clause are merged onto the corresponding type parameters; an inline `<T, F: Bound>` and the equivalent `where`-clause form are semantically identical, and sibling type-params are in scope when resolving where-clause bound arguments.

**Evidence:** `src/compiler/sema.cpp#L4195-L4295`

### `generic.where.projection-subject-skipped` — where-clause projection subject is parsed but not enforced

A `where C::Item<T>: Bound` clause whose subject is an associated-type projection is accepted but not yet enforced (parse-and-skip).

**Uncertainty:** Statement reflects current parse-and-skip behavior; full projection-bound checking is noted as a separate unimplemented feature.

**Evidence:** `src/compiler/sema.cpp#L4218-L4227`

### `generic.where.ref-subject` — where-clause with reference subject

A `where &T: Trait` / `where &mut T: Trait` clause records its bounds on the underlying type-param T, flagged as applying only to a matching (shared/mut) reference receiver.

**Evidence:** `src/compiler/sema.cpp#L4210-L4259`

## Type-Argument Arity

### `generic.arity.exact-or-variadic-tail` — Type-argument arity matching

A generic instantiation with parameter list P and argument list A is well-formed iff: if P's last parameter is a variadic pack (`T...`), |A| >= |P|-1 (the pack absorbs >=0 trailing args); otherwise |A| == |P|. Mismatch is an error stating the expected and actual counts.

```logos
Vec<i32>      // P={T}, ok
Tuple<A,B,...> // last param variadic, >=2 args
```

**Evidence:** `src/compiler/sema_impl.hpp#L1337-L1366`

### `generic.arity.type-args-on-non-generic` — Type-args on a non-generic target rejected

Supplying >=1 type argument to a target whose type-parameter list is empty (non-generic) is an error: `<context> '<name>': not generic — cannot accept N type arg(s)`. (Diagnosed only after full type-parameter info is available, never during forward-declaration prepass.)

```logos
struct S {} ; let x: S<i32>; // error: not generic
```

**Evidence:** `src/compiler/sema_impl.hpp#L1341-L1352`

## Type-Parameter Shadowing

### `generic.shadow.type-param-shadows-type-warn` — Type-param shadowing a type/trait warned

A fn type-parameter whose name shadows an existing struct/datatype/enum/trait is warned because it currently breaks fn-name resolution at use sites.

**Uncertainty:** Stated as a current implementation limitation rather than a designed rule.

**Evidence:** `src/compiler/sema_collect.cpp#L4619-L4630`

## Generic Lints

### `generic.lint.unused-type-param` — Unused type parameter warning (functions)

A function's declared type parameter that does not appear in its signature (parameter types, return type, or any trait-bound type-args) is warned as unused. Exemptions: variadic packs; const-generic params; names equal to `_` or beginning with `_`; and any param that itself carries a trait bound (the bound counts as a use). Lifetime parameters get the analogous unused-lifetime warning.

```logos
fn f<T>(x: i32) {} // warn: type parameter 'T' is unused
fn f<_T>(x: i32) {} // ok
```

**Evidence:** `src/compiler/sema_impl.hpp#L1555-L1604`

## Type Inference

### `generic.infer.array-len-const` — Const-generic array length inferred from concrete length

Unifying a formal array `[T; N]` (N a const-generic length parameter) against a concrete `[U; M]` binds N → IntLit(M) (if N not already bound and M > 0), in addition to unifying element types.

**Evidence:** `src/compiler/sema_expr.cpp#L3683-L3698`

### `generic.infer.bind-first-wins` — Type-param unification binds first occurrence

Unifying a formal type-parameter (TypeVar or ConstVar) against an actual type T records the binding param→T only if the parameter is not already bound; subsequent occurrences do not overwrite. A const-generic parameter at type-argument position (ConstVar) is bound the same way as a type-generic parameter.

**Related:** `generic.infer.literal-default`

**Evidence:** `src/compiler/sema_expr.cpp#L3619-L3637`

### `generic.infer.fn-bound-propagation` — Fn-family bound drives inference of its signature params

When a type-parameter F with an Fn-family bound `F: Fn*(X..)->Y` is bound to an actual closure/fn-ptr, the actual callable's parameter types and return type are unified against the bound's fn_params/fn_ret, so type-params X/Y that appear only inside F's bound are inferred from the actual callable's signature.

**Evidence:** `src/compiler/sema_expr.cpp#L3867-L3901`

### `generic.infer.literal-default` — Literal types default before type-param binding

During unification an actual operand of type IntLit defaults to i32 and FloatLit defaults to f64 before it is used to bind any type parameter. A variadic pack element of literal type is likewise widened (IntLit→i32, FloatLit→f64) before being recorded as a pack type-arg.

**Related:** `generic.infer.bind-first-wins`

**Evidence:** `src/compiler/sema_expr.cpp#L3612-L3617`, `src/compiler/sema_expr.cpp#L3971-L3976`

### `generic.infer.never-fallback` — Unbound type-param falls back to ! for diverging callees

If a non-variadic type-parameter remains unbound after inference, it is an error (ambiguous) UNLESS the callee's body is statically known to always diverge (panic/loop/never-returning tail), in which case the parameter falls back to the never type `!`. The discriminator is the callee body, not the surrounding callsite divergence: `fn f<T>()->T { return 0; }` errors as ambiguous while `fn f<T>()->T { panic(); }` resolves T = `!`.

**Divergence:** A7 — abort-only panic; `!`-fallback for diverging bodies follows Rust-2024 inference.

**Evidence:** `src/compiler/sema_expr.cpp#L3946-L3966`

### `generic.infer.no-bind-infer-hole` — Inference holes never bind a type-param

Unifying a type-parameter against an inference hole `_` (InferredType) leaves the parameter unbound; the hole is resolved by other arguments or later uses, never pinned to the literal `_`.

**Evidence:** `src/compiler/sema_expr.cpp#L3629-L3634`

### `generic.infer.no-bind-self` — Self is never bound by unification

A formal type-parameter named `Self` is never bound during unification (it is resolved by the impl/receiver, not inferred from arguments).

**Evidence:** `src/compiler/sema_expr.cpp#L3628`

### `generic.infer.ptr-ref-cross` — Pointer/reference families cross-unify on pointee

A formal Ptr unifies against an actual Ptr, Ref, or MutRef by recursing on pointee. A formal Ref or MutRef unifies against an actual Ref, MutRef, or Ptr by recursing on pointee. Reference/pointer mutability and kind do not block unification; only the pointee is matched.

**Uncertainty:** Mutability compatibility is enforced elsewhere (arg type-check / B6); unify itself is mutability-agnostic.

**Evidence:** `src/compiler/sema_expr.cpp#L3641-L3653`

### `generic.infer.ref-unsize-pointee` — ?Sized inference through reference to slice/dyn

When a formal `&T`/`&mut T` is unified against an actual slice value (Kind::Slice → `&[U]`/`*const [U]`) or trait object (`&dyn Trait`), the actual is treated as a reference whose pointee is the unsized form UnsizedSlice<U> (resp. UnsizedDyn<Trait>), so a `T: ?Sized` formal pointee binds to that unsized type; substitution later canonicalises Ref/MutRef/Ptr-of-unsized back to the original Slice/TraitObject (same ABI).

**Evidence:** `src/compiler/sema_expr.cpp#L3654-L3681`

### `generic.infer.return-type-hint` — Return-type hint participates in inference

When an expected return type is in scope (e.g. from a let-binding annotation), the function's (partially substituted) return type is unified against the expected type, inferring type-params that appear only in return position. The expected-type hint may legitimately bind a parameter even when overload pre-selection found no argument binding for it.

**Evidence:** `src/compiler/sema_expr.cpp#L3800-L3811`, `src/compiler/sema_expr.cpp#L3936-L3944`, `src/compiler/sema_expr.cpp#L4061-L4067`

### `generic.infer.structural-recursion` — Unification recurses structurally on matching constructors

Unification of two types with the same constructor recurses into components: Ptr/Ref/MutRef into pointee; Array/Slice into element; Struct (resp. Enum) into positional type-args when struct (resp. enum) names match; Tuple into positional elements; Fn-item/Fn-ptr/Closure into positional parameter types and return type. Mismatched constructors bind nothing.

**Evidence:** `src/compiler/sema_expr.cpp#L3640-L3756`

### `generic.infer.trait-bound-impl-args` — Trait-bound type-args inferred via the bound impl

When a type-parameter I is bound to a concrete struct and carries a non-Fn trait bound `I: Trait<A..>`, inference looks up I's impl of Trait, unifies the impl's target pattern against I's actual type, substitutes the impl's recorded trait type-args, and unifies those against the bound's args A.. — so a type-param appearing only inside another param's trait bound becomes deducible.

**Evidence:** `src/compiler/sema_expr.cpp#L3903-L3934`

### `generic.infer.variadic-pack` — Variadic type-pack collects one element per trailing arg

For a function with a trailing variadic type-parameter, each value argument beyond the fixed parameters contributes one element to the type pack (with IntLit/FloatLit defaulted to i32/f64). The pack length is recorded for `sizeof...` / `[T; sizeof...(P)]` resolution under a symbolic key, and a single-type-param variadic also binds that param to the tuple of pack elements.

**Evidence:** `src/compiler/sema_expr.cpp#L3968-L3978`, `src/compiler/sema_expr.cpp#L4123-L4147`

## Generic Calls

### `expr.call.generic-inference-deferred-in-generic-context` — Generic-call inference is deferred inside a generic context

If any argument is a pack expansion or has a TypeVar/AssocType type (partially-substituted context), call inference is deferred to monomorphization: the generic overload is selected and the call shape is preserved (callee type-vars and substituted return type) rather than pinning a concrete instantiation.

**Evidence:** `src/compiler/sema_expr.cpp#L3435-L3462`, `src/compiler/sema_expr.cpp#L3586-L3601`

### `expr.call.generic-inference-from-args` — Type arguments of a generic call are inferred from argument types

When a callee is generic and the call is not in a generic/pack-expansion context, type arguments are inferred from the actual argument types; if not all can be inferred it is an error directing the user to explicit `f::<T>(...)` syntax.

**Evidence:** `src/compiler/sema_expr.cpp#L3413-L3457`

### `generic.call.antiquot-pack-type-arg` — Type-arg antiquote pack splices a reflected type list

An antiquote pack `$v...` in a generic call's type arguments splices a runtime-produced list of types (e.g. a struct's field types) into the callee's type args; it is carried as a marker TypeVar `__splicepack$v` that flows like a variadic pack and is expanded during monomorphization by chasing the variable to its type-list producer.

**Divergence:** Logos metaprog reflection extension (no Rust analogue)

**Evidence:** `src/compiler/sema_expr.cpp#L5985-L6000`

### `generic.call.callee-resolution-order` — Generic call callee resolution precedence

For a call `f::<TARGS>(args)` with n value args, the callee resolves in order: (1) a generic function overload matching name `f` and arity n; (2) if none, a single non-generic candidate named `f`. If exactly one of these is found it is the callee; otherwise fall through to alternative interpretations (struct ctor / enum variant) before erroring.

**Evidence:** `src/compiler/sema_expr.cpp#L5854-L5861`

### `generic.call.impl-target-pattern-unify` — Impl-level type-params bound by target-pattern unification

For a method on `impl<...> Trait for Foo<Pat..>`, the impl-level type-parameters are bound by structurally unifying the recorded impl-target pattern against the receiver-positional type-arguments (e.g. pattern `Vec<T>` vs concrete `Vec<i32>` ⇒ T=i32), not positionally; method-level type-arguments are then layered positionally on top. Positional binding is used when no target pattern is recorded.

**Evidence:** `src/compiler/sema_expr.cpp#L4085-L4122`, `src/compiler/sema_expr.cpp#L4033-L4052`

### `generic.call.pub-access-check` — Callee visibility check

Once a generic-call callee is resolved, its visibility (pub / module-only) is enforced relative to the calling package and module.

**Evidence:** `src/compiler/sema_expr.cpp#L5962`, `src/compiler/sema_expr.cpp#L6110`

### `generic.call.sized-enforcement` — Sized bound enforced at type-argument substitution

Every type-parameter carries an implicit `Sized` bound unless declared `?Sized`. Passing an unsized type-argument (UnsizedSlice/UnsizedDyn), or a `?Sized` outer type-parameter, at a Sized-required parameter position is an error ("requires `Sized`"), reported before trait-bound checking.

**Evidence:** `src/compiler/sema_expr.cpp#L4150-L4184`

### `generic.call.tuple-struct-arity` — Tuple-struct constructor arity check

A tuple-struct constructor call is an error if the number of arguments differs from the number of struct fields.

**Evidence:** `src/compiler/sema_expr.cpp#L5880-L5884`

### `generic.call.tuple-struct-field-type-check` — Tuple-struct constructor field type checking

For each tuple-struct constructor argument, the corresponding field type (after substitution) is the expected type: integer literals are widened to it, and a non-TypeVar non-error field type that is incompatible with the argument type is an error.

**Evidence:** `src/compiler/sema_expr.cpp#L5893-L5907`

### `generic.call.turbofish-arity` — Type-argument count validation

A generic call supplying more type-arguments than the function has type-parameters is an error. With a variadic type-param, fewer than the non-variadic count is an error. Supplying fewer than the full count (partial turbofish), or interior `_` placeholders, is permitted: the explicit head is pre-bound and the remaining/`_` positions are inferred from argument types and the return-type hint; a still-uninferable position is an error directing the user to turbofish.

**Evidence:** `src/compiler/sema_expr.cpp#L4001-L4082`

### `generic.call.turbofish-tuple-struct-ctor` — Turbofish on a tuple-struct constructor

If callee resolution finds no function but `f` names a tuple struct, `f::<TARGS>(args)` constructs that struct: the explicit turbofish TARGS pin the leading struct type-params positionally, any remaining type-params are inferred by unifying each field type with its argument type, and the result is a struct literal of `f` with fields '0','1',… . Argument count must equal the struct's field count.

**Evidence:** `src/compiler/sema_expr.cpp#L5862-L5920`

### `generic.call.undefined-callee-error` — Undefined-function diagnostic gated by metaprog mode

If a call's callee resolves to nothing (no fn, struct ctor, or variant), it is an error `call to undefined function 'f'`, EXCEPT in metaprog mode where the call silently lowers with `<error>` type so a not-yet-emitted derive-synthesized function can resolve in a later sema pass.

**Evidence:** `src/compiler/sema_expr.cpp#L5954-L5960`

### `generic.call.underscore-type-arg-inference` — `_` turbofish argument is an inference hole

An explicit type argument written `_` becomes an inference hole (TypeVar `_`) and is inferred from the value arguments during call finishing rather than pinned.

**Evidence:** `src/compiler/sema_expr.cpp#L5982-L6003`

### `generic.call.unsized-targ-for-relaxed-param` — ?Sized type-param relaxes unsized turbofish argument

When resolving the i-th explicit turbofish type argument, a bare unsized type (`[T]`, `dyn Trait`) is accepted iff the i-th target type-param was declared `?Sized` (implicit_sized=false); otherwise the unsized-by-value diagnostic applies.

**Evidence:** `src/compiler/sema_expr.cpp#L5964-L6007`, `src/compiler/sema_expr.cpp#L6112-L6127`

## Generic References (value-position / turbofish)

### `generic.ref.bounds-check-when-concrete` — Generic-ref bound check deferred when TARGS contain TypeVars

Type-param bound checking on a generic-ref value runs eagerly only when all type arguments are concrete; if any type argument is a TypeVar the real bound check is deferred to monomorphization.

**Evidence:** `src/compiler/sema_expr.cpp#L6141-L6150`

### `generic.ref.no-variadic-packs` — Variadic type packs forbidden in generic-ref value

A generic-ref value of a function whose last type-param is a variadic pack is an error; variadic type packs are not supported in value position.

**Evidence:** `src/compiler/sema_expr.cpp#L6129-L6134`

### `generic.ref.sized-enforcement` — Sized enforcement at generic-ref substitution

Substituting a generic-ref type argument of unsized kind (`[T]` slice or `dyn`) into a type-param that requires `Sized` (implicit_sized true) is an error, suggesting `T: ?Sized` to relax the bound.

**Evidence:** `src/compiler/sema_expr.cpp#L6152-L6168`

### `generic.ref.turbofish-no-payload-variant` — Turbofish on a no-payload enum variant in value position

In value position, `V::<TARGS>` where `V` is a no-payload variant of `Option`/`Result` (or a use-aliased variant) constructs that variant, pinning the enum's type-args from the turbofish when the turbofish arity matches the enum's type-param count.

**Evidence:** `src/compiler/sema_expr.cpp#L6071-L6109`

### `generic.ref.type-arg-arity` — Generic-ref requires exact type-arg arity

A generic-ref value must supply exactly as many type arguments as the function has type-params; a mismatch is an error.

**Evidence:** `src/compiler/sema_expr.cpp#L6135-L6139`

### `generic.ref.value-position-fn-pointer` — `IDENT::<TARGS>` as a value yields a fn-pointer literal

`f::<TARGS>` in expression (non-call) position evaluates to a function-pointer value whose type is the callee signature with TARGS substituted (params and return). TARGS containing TypeVars are deferred: the node carries (base, type_args) and is mangled/substituted at monomorphization time.

**Evidence:** `src/compiler/sema_expr.cpp#L6043-L6182`

## Generic Method Calls

### `expr.method.typeparam-inference-from-args` — Method type params inferred by unifying params with arg types

Absent turbofish, a trait method's type parameters are inferred by unifying each substituted formal parameter type (seeded with Self → receiver type and any supertrait-derived bindings) against the corresponding argument's type.

**Evidence:** `src/compiler/sema_expr.cpp#L7498-L7524`

### `generic.method.arg-inference` — Method type args inferred from args

Method-level type arguments not bound by turbofish or receiver are inferred from the actual argument expressions (param offset skips `self`), seeding the substitution context.

**Evidence:** `src/compiler/sema_expr.cpp#L8861-L8865`

### `generic.method.bounds-check` — Method type-arg bounds enforced

Inferred/explicit method type arguments must satisfy their type parameters' bounds.

**Evidence:** `src/compiler/sema_expr.cpp#L8973`

### `generic.method.inference-complete` — All method type args must be inferred

Every method type parameter must be bound (by turbofish, receiver, or inference); failure to bind all is an error.

**Evidence:** `src/compiler/sema_expr.cpp#L8967-L8972`

### `generic.method.recv-formal-unify` — Impl params inferred from receiver formal

Impl/method type parameters appearing only in the receiver formal (e.g. `impl<T> Pin<&T> { fn get_ref(&self) -> &T }`) are inferred by unifying the receiver formal against the actual receiver type, peeling one ref/ptr layer on either side to match shapes.

**Evidence:** `src/compiler/sema_expr.cpp#L8838-L8860`

### `generic.method.recv-typeargs-bind` — Receiver type-args seed substitution

The receiver's struct/zoned-struct or enum type arguments are bound positionally to the receiving type's type parameters and used as the substitution context for checking and inferring method-level type arguments.

**Evidence:** `src/compiler/sema_expr.cpp#L8750-L8776`

### `generic.method.turbofish-wins` — Method turbofish overrides inference

Explicit method-level type arguments `recv.m::<T...>(args)` bind the method-level type parameters (the tail of the method's type-param list that is not shared with the receiving type) positionally; inference runs only for the remaining unbound positions.

**Evidence:** `src/compiler/sema_expr.cpp#L8784-L8837`

## Method Substitution

### `generic.method-subst.owning-trait-params-from-impl` — Owning-trait type-params bound from the receiver's impl

For a method belonging to a trait, the trait's type-parameters (e.g. `Iterator<Item>`) are bound from the receiver type's `impl Trait for Recv` trait-type-args (positional, only filling names not already bound by the receiver's own type-args), so Fn-family bound argument types resolve concretely for closure-formal hints.

**Evidence:** `src/compiler/sema_expr.cpp#L7895-L7918`

### `generic.method-subst.recv-typeargs` — Receiver nominal type-args bound into method formal substitution

When the receiver is a generic Struct/ZonedStruct or Enum carrying type-args, the nominal type's declared type-parameters are bound positionally to those args; this substitution is applied to the method's formal parameter types when computing argument type hints.

**Evidence:** `src/compiler/sema_expr.cpp#L7877-L7894`, `src/compiler/sema_expr.cpp#L7938-L7941`

## Method Type-Arguments

### `generic.method-typeargs.turbofish-binds-method-params` — Turbofish binds method-level type-params in order, skipping already-bound

An explicit turbofish on a method call (`it.fold::<i32>(..)`) provides the method-level type-arguments in order; each is bound to the next method type-parameter not already bound by the receiver substitution (struct-inherited params bound from the receiver are skipped).

**Evidence:** `src/compiler/sema_expr.cpp#L7919-L7936`

## Overload Resolution

### `generic.overload.receiver-autoref-autoderef` — Method receiver autoref/autoderef in overload matching

For the receiver argument of a method overload, a by-value actual matching a `&self`/`&mut self` formal pointee (autoref) ranks as an exact match (score 2); a reference actual matching a by-value `self` formal pointee (autoderef) ranks one below (score 1). The receiver is unified through these autoref/autoderef shapes so unification does not see Ref-vs-Struct and bind nothing.

**Evidence:** `src/compiler/sema_expr.cpp#L3783-L3797`, `src/compiler/sema_expr.cpp#L3821-L3835`

### `generic.overload.score-select` — Generic overload selection by argument fit score

Among ≥2 generic overloads of a name, each arity-compatible candidate is scored by per-argument fit after substituting inferred bindings: exact type match = 2, compatible (incl. autoderef receiver / general type-compatibility) = 1; any incompatible argument disqualifies the candidate. A candidate whose fixed type-params cannot all be bound is rejected (unless its body always diverges, or a return-type hint is present). The highest-scoring candidate wins; ties are broken in favour of a candidate defined in the current package.

**Evidence:** `src/compiler/sema_expr.cpp#L3759-L3845`

## Generic Enum Literals

### `generic.enum-lit.bounds-check` — Resolved type-args checked against type-param bounds

After resolving the enum's type arguments, each is checked against the corresponding type parameter's trait bounds; unresolved type parameters (no binding) yield an error type.

**Evidence:** `src/compiler/sema_expr.cpp#L12502-L12513`

### `generic.enum-lit.dyn-hint-preference` — Trait-object type-arg preferred over concrete payload

When the hint pins a type-param to a (possibly Box/ref-wrapped) trait-object type while the payload argument is a concrete type compatible with it, the enum's type-arg is taken as the trait-object (dyn) type, while the payload expression stays concrete; the concrete payload is unsize-fattened into the dyn slot at codegen.

**Evidence:** `src/compiler/sema_expr.cpp#L12428-L12455`

### `generic.enum-lit.hint-ref-ptr-preference` — Hint reference/pointer kind overrides inferred pointee

When the context hint for type-param T is `&U`/`&mut U` and inference produced bare `U`, the hint's reference type is used for T; when the hint is `*const U`/`*mut U` over the same pointee as an inferred reference/pointer, the hint's raw-pointer type is used (preserving the annotated repr, e.g. tagged `Option<*const T>` rather than niche `Option<&T>`).

**Evidence:** `src/compiler/sema_expr.cpp#L12464-L12501`

### `generic.enum-lit.intlit-payload-pin` — Integer/float-literal payload type-param resolution

When a variant payload whose formal is a type-param T receives an unresolved integer- or float-literal argument, T is bound to the type the surrounding hint pins for T (widening the literal accordingly) if available; otherwise T defaults to i32 for an integer literal and f64 for a float literal.

**Evidence:** `src/compiler/sema_expr.cpp#L12407-L12427`

### `generic.enum-lit.structural-unify` — Structural unification of non-TypeVar payload formals

A variant payload formal that is not a bare TypeVar but mentions the enum's type parameters (e.g. `Pair<T>`) is unified against the actual argument's type to extract nested type-param bindings.

**Evidence:** `src/compiler/sema_expr.cpp#L12456-L12462`

### `generic.enum-lit.turbofish-first` — Explicit turbofish type-args bind before inference

Explicit turbofish type arguments on an enum literal (`E::<A>::V`) are applied to `E`'s type parameters before any payload-derived inference, so a payload-less variant (`None`/unit variant) still receives the user-given type-args.

**Evidence:** `src/compiler/sema_expr.cpp#L12386-L12406`

## Generic Enum Methods

### `generic.enum-method.instantiate-on-typeargs` — Generic-enum method dispatch instantiates the method template

For a receiver of generic Enum type (`Enum<...>` with type-args) whose method is a generic template, dispatch routes through the generic-call path with type-args = receiver enum type-args (struct-level prefix) followed by inferred or turbofish-supplied method-level type-args; the receiver's enum type-params are pre-seeded into the substitution before inference.

**Evidence:** `src/compiler/sema_expr.cpp#L8004-L8088`

## Impl-Method Parameter Split

### `trait.impl-method.method-vs-impl-param-split` — Impl-level type params dropped from method TYPE_PARAMS, kept as IMPL_TYPE_PARAMS

When an impl-block method is attached to a generic struct template, impl-level type parameters are removed from the method's own type-parameter list (mono re-injects them at struct instantiation) while method-level type parameters are retained; the impl-level parameters with their bounds are preserved separately so monomorphization can gate instantiation on bound satisfaction.

**Evidence:** `src/compiler/sema_decl.cpp#L2216-L2240`, `src/compiler/sema_decl.cpp#L2478-L2530`

## Const Generics

### `generic.const.binds-as-const-var` — Const-generic parameters bind as const-vars

A const-generic type parameter `const N: T` binds in scope as a ConstVar carrying its declared const type; a non-const type parameter binds as a type variable.

**Evidence:** `src/compiler/sema_impl.hpp#L3297-L3304`

## Specialization

### `generic.spec.bare-ident-resolution` — Bare-ident spec param: known type is concrete, else fresh TypeVar

A bare-IDENT type-param in a specialization pattern that names a known type resolves to that CONCRETE type (a specialization leg); otherwise it is treated as a fresh unbounded TypeVar scoped over the fn signature and body.

**Evidence:** `src/compiler/sema_collect.cpp#L4454-L4464`

### `generic.spec.bounded-param-is-typevar` — Bounded spec type-param stays a TypeVar

A type-param carrying trait bounds in a specialization pattern is always a TypeVar (never coerced to a concrete leg), with its declared trait bounds recorded.

**Evidence:** `src/compiler/sema_collect.cpp#L4437-L4453`

### `generic.spec.lifetime-param-skip` — Lifetime params ignored in spec patterns

Lifetime parameters in a specialization fn's type-param list contribute no spec pattern (deferred to the borrow checker).

**Evidence:** `src/compiler/sema_collect.cpp#L4431`

### `generic.spec.method-shadows-impl-param-warn` — Method type-param shadowing impl param is a silent specialization

When a method's bare-IDENT type-param has the same name as an enclosing impl-block type-param, the method is silently treated as a specialization on the impl's param; the compiler emits a warning advising a rename.

**Divergence:** Rust treats the method param as a fresh shadowing generic; Logos reinterprets it as a specialization leg (warned).

**Evidence:** `src/compiler/sema_collect.cpp#L4551-L4586`

### `generic.spec.most-specific-match` — Partial-spec selection picks the most specific matching pattern

Among struct specializations sharing a base name with arity equal to the supplied type-args, a spec is a candidate iff every pattern position matches the corresponding type-arg (TypeVar binds any type but must bind consistently within one spec; concrete kinds must match structurally; Struct/ZonedStruct match by name). The selected spec is the one whose per-position specificity scores are lexicographically greatest (specificity: 0 for a TypeVar, 100 for a concrete leaf, 1+inner for Ptr/Array). No candidate ⇒ no specialization.

**Uncertainty:** Specificity ordering inferred from specificity_sema constants and lexicographic vector compare.

**Evidence:** `src/compiler/sema.cpp#L6506-L6563`

### `generic.spec.partial-pattern-typevars` — Partial specialization keeps unbound params as type variables

In a struct specialization's pattern list, each slot that does not resolve to a known type stays a free type variable (e.g. `Map<Bitmap, V>` keeps `V`). The concrete spec name derives from `concrete_struct_name(make_generic_struct(name, patterns))`, so both full and partial specs are registered and later matched by best-fit at lookup. Pattern type variables are scoped only during collection and removed afterward.

**Divergence:** A6: partial specialization of user structs is a Logos addition.

**Evidence:** `src/compiler/sema_collect.cpp#L3810-L3833`, `src/compiler/sema_collect.cpp#L3858-L3860`

### `generic.spec.pattern-list` — Specialization function pattern parameters

A specialization fn's type-parameter list is interpreted as a list of type PATTERNS (not plain generic params): each entry is resolved into a SPEC_PATTERNS type that can be a structured type (e.g. `*T`, `[T; N]`), a TypeVar (bounded or unbounded type-param), or a concrete known type. The patterns drive dispatch to the matching specialization leg.

**Uncertainty:** Logos has a specialization mechanism with no direct stable-Rust analogue (Rust specialization is unstable).

**Evidence:** `src/compiler/sema_collect.cpp#L4414-L4474`

### `generic.spec.struct-pattern-classification` — Struct specialization detection

A `struct Name<...>` decl is a specialization (not a fresh generic base) iff (a) some type-param slot is a structured pattern (`*T`, `[T;N]`), OR (b) some slot is a concrete/known type name (primitive, alias, struct, datatype, or enum). A bare type-param name is treated as a concrete user-type spec only when a base of the same name is already registered in the current package AND the name is in the pre-scanned decl-name set; otherwise the decl is registered as a generic base. Specializations are not added to `structs_`; they are lowered directly.

**Divergence:** A6: Logos supports user struct specialization (`struct Map<Bitmap, V> {...}`), which Rust lacks for structs.

**Uncertainty:** Order-independence relies on pass0_decl_names_; classification of a name colliding across modules is gated on a same-name base existing.

**Evidence:** `src/compiler/sema_collect.cpp#L3974-L3976`, `src/compiler/sema_collect.cpp#L4258-L4304`

## Generic Fields

### `generic.field.spec-overrides-base` — Field type comes from the matching specialization when one exists

For a field of an instantiated generic struct, if a partial/full specialization matches the type-args, the field type is taken from that specialization's field list (and a field absent there resolves to nothing); only when no specialization matches is the field looked up on the base template and substituted.

**Related:** `generic.spec.most-specific-match`, `generic.field.subst`

**Evidence:** `src/compiler/sema.cpp#L6586-L6603`

### `generic.field.subst` — Generic struct field types are substituted with the instance's type and lifetime args

For a non-specialized instantiated generic struct, each base field type is substituted by mapping the struct's (non-variadic) type parameters to the supplied type-args positionally and its lifetime parameters to the supplied lifetime-args positionally; `&'z T` fields thus resolve to the caller's lifetime.

**Related:** `generic.field.variadic-expansion`

**Evidence:** `src/compiler/sema.cpp#L6644-L6655`

### `generic.field.variadic-expansion` — Variadic field `name_N` selects the Nth element of the variadic type-arg pack

A variadic struct field declared `name: A...` expands to fields `name_0, name_1, …`; field `name_<idx>` whose declared type is the variadic type parameter resolves to the type-arg at (start-of-pack + idx), where start-of-pack is the count of preceding non-variadic type parameters. Out-of-range or non-TypeVar variadic field types fall back to the raw declared type.

**Divergence:** A6 — variadic type/field packs are Logos-only.

**Related:** `generic.field.subst`

**Evidence:** `src/compiler/sema.cpp#L6578-L6582`, `src/compiler/sema.cpp#L6606-L6631`

## Variance

### `generic.variance.adt-by-table` — ADT variance is per-parameter from the computed variance table

For a struct/zoned-struct/enum `D<A0..,'L0..>`, the variance contribution of each type argument `Ai` (resp. lifetime arg) is the meet over arguments of `compose(ambient, declared_variance(D,#i))` recursed into `Ai`, where `declared_variance(D,#i)` (`@i` for lifetimes) comes from the variance table keyed by `pkg.Name`; a parameter absent from the table defaults to covariant (Co).

**Evidence:** `src/compiler/sema.cpp#L8087-L8132`

### `generic.variance.compose` — Variance composition under nesting

For a parameter P used at variance `inner` inside Wrapper<P>, where the Wrapper field occupies a position of variance `outer` in the enclosing type, P's effective variance there is outer ∘ inner: BiVar absorbs (BiVar ∘ x = x ∘ BiVar = BiVar); Inv dominates the non-BiVar cases (Inv ∘ x = x ∘ Inv = Inv); otherwise the result is Contra iff exactly one of outer/inner is Contra (sign-flip rule: Co∘Co=Co, Co∘Contra=Contra, Contra∘Co=Contra, Contra∘Contra=Co).

**Related:** `generic.variance.four-kinds`, `generic.variance.meet`

**Evidence:** `include/logos/compiler/variance.hpp#L40-L46`

### `generic.variance.dyn-trait` — Trait objects: covariant in lifetime bound, invariant in type args

`dyn Trait<A...> + 'a` is covariant in its lifetime bound `'a` (the erased object's storage must outlive `'a`) and invariant in each type argument `Ai` (ambient composed with Inv); auto-trait bounds (Send/Sync) are set-membership and contribute nothing to variance.

**Evidence:** `src/compiler/sema.cpp#L8144-L8163`

### `generic.variance.fixpoint` — ADT variances computed by monotone fixpoint over fields

Variances of all struct/datatype/enum parameters are computed by a fixpoint iteration: each parameter is seeded BiVar, then on each round its variance is set to the meet over all field types (enum: over all variant payload types) of `variance_in_type(field, param, ambient=Co)`; iteration repeats until no entry changes, bounded at 32 rounds.

**Uncertainty:** The 32-round cap is an implementation safety bound; the language semantics is the least fixpoint.

**Evidence:** `src/compiler/sema.cpp#L8171-L8261`

### `generic.variance.fn-contravariant-params` — Function types are contravariant in parameters, covariant in return

A function item or function pointer `fn(P0..)->R` is contravariant in each parameter type `Pi` (ambient composed with Contra) and covariant in the return type `R`.

**Evidence:** `src/compiler/sema.cpp#L8134-L8143`

### `generic.variance.four-kinds` — Generic-parameter variance kinds

Each type/lifetime parameter of a struct/enum has a variance in {BiVar, Co, Contra, Inv}: Co (covariant) preserves the subtype direction (Foo<Sub> <: Foo<Super>, &'long T <: &'short T when 'long: 'short); Contra (contravariant) reverses it (fn-argument position); Inv (invariant) requires both directions (mutable-reference content); BiVar (bivariant) places no constraint, used when the parameter appears only in phantom/absent positions.

**Related:** `generic.variance.meet`, `generic.variance.compose`

**Evidence:** `include/logos/compiler/variance.hpp#L31`

### `generic.variance.meet` — Variance meet (combine multiple uses of a parameter)

When a parameter is used in several positions, its variance is the meet (most-restrictive demand): BiVar ∧ x = x; x ∧ x = x; Co ∧ Contra = Inv; Inv ∧ x = Inv. The lattice ordering is BiVar < {Co, Contra} < Inv.

**Related:** `generic.variance.four-kinds`

**Evidence:** `include/logos/compiler/variance.hpp#L33-L38`

### `generic.variance.mutref-invariant-pointee` — Mutable reference is covariant in lifetime, invariant in pointee

`&'a mut T` is covariant in its lifetime `'a`, but invariant in its pointee `T` (recurses with ambient composed with Inv).

**Evidence:** `src/compiler/sema.cpp#L8062-L8071`

### `generic.variance.raw-ptr` — Raw pointers: *const covariant, *mut invariant

`*const T` is covariant in pointee `T`; `*mut T` is invariant in pointee `T` (ambient composed with Co or Inv respectively). Matches Rust.

**Evidence:** `src/compiler/sema.cpp#L8072-L8076`

### `generic.variance.ref-covariant` — Shared reference is covariant in lifetime and pointee

`&'a T` is covariant in its lifetime `'a` (contributes ambient) and covariant in its pointee `T` (recurses with unchanged ambient).

**Evidence:** `src/compiler/sema.cpp#L8054-L8061`

### `generic.variance.tuple-array-slice-covariant` — Tuples, arrays, and slices are covariant in element types

A tuple is covariant in each element type (meet over elements, unchanged ambient); `[T; N]` and `[T]` are covariant in element type `T` (recurse with unchanged ambient).

**Evidence:** `src/compiler/sema.cpp#L8077-L8086`

### `generic.variance.type-param-occurrence` — Variance of a parameter is the meet over its occurrences

The variance of a type/lifetime parameter `p` in a type `T` is `variance_in_type(T,p,ambient=Co)`, computed structurally: a leaf occurrence of `p` contributes the ambient variance; a parameter that does not occur is bivariant (BiVar). The overall variance combines occurrences via the meet operator (variance_meet); an unmentioned parameter stays BiVar (unconstrained).

**Uncertainty:** variance_meet/variance_compose lattice (BiVar top, Co/Contra, Inv bottom) defined elsewhere; semantics inferred from usage.

**Evidence:** `src/compiler/sema.cpp#L8047-L8053`, `src/compiler/sema.cpp#L8165-L8166`

### `generic.variance.unknown-type-bivariant` — Type kinds not contributing the parameter are bivariant

A type that does not mention the target parameter (or whose kind is not variance-relevant) contributes BiVar (the identity for meet, i.e. no constraint).

**Evidence:** `src/compiler/sema.cpp#L8047`, `src/compiler/sema.cpp#L8053`, `src/compiler/sema.cpp#L8164-L8166`

### `generic.variance.unsafecell-invariant` — UnsafeCell<T> is invariant in T

`logos.lang.cell::UnsafeCell<T>` (the interior-mutability lang item, recognised by qualified name) is invariant in each type argument (ambient composed with Inv), overriding the table-driven ADT rule.

**Evidence:** `src/compiler/sema.cpp#L8090-L8104`

## Variadic Packs

### `expr.pack.sizeof-and-expand` — Variadic pack size and expansion

`P...(N)` yields the length of variadic pack `P` (sizeof-pack), and `P...` in expression position expands the pack `P`.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L2737-L2738`, `tools/peg_gen/grammars/logos.peg#L2774`
