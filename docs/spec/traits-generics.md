# Traits and Generics

Scope: trait definitions, implementations, resolution, dispatch, auto traits, generic parameters/inference, specialization, and variance — 421 rules extracted from the grammar (`tools/peg_gen/grammars/logos.peg`), semantic analysis (`src/compiler/sema*`), monomorphization (`src/compiler/mono*`), and codegen (`src/compiler/mlir_gen*`) layers.

Rule ids are permanent addresses and are preserved verbatim from the extraction artifacts, including unnormalized group spellings (`bound`/`bounds`, `dispatch`/`method-dispatch`/`tagdispatch`/`tag-dispatch`, `mangle`/`mangling`/`method-mangling`, `blanket`/`blanket-impl`, `object`/`object-safety`); adjacent sections cover the paired spellings.

## Trait definitions (`def`)

### `trait.def.copy-not-a-supertrait` — Copy is excluded from a trait's supertrait list

When recording a trait's declared supertraits, `Copy` is omitted from the supertrait set (it is an auto/marker bound, not a vtable-bearing supertrait).

> **Divergence (vs Rust):** A: Copy treated specially, excluded from supertrait closure.

Source: `src/compiler/sema_decl.cpp#L1665-L1669`

### `trait.def.duplicate` — Trait uniqueness per package

Two traits with the same name in the same package are an error. A user trait colliding with an already-registered trait of the same bare name from a different package is a distinct trait: the incumbent keeps the bare slot and the newcomer registers only under `pkg::Name`; lookup probes `cur_package_::Name` first.

Source: `src/compiler/sema_collect.cpp#L2661-L2677`

### `trait.def.local-shadowing-resolution` — Trait lowering resolves same-name collisions via package-qualified scope

When a user-defined trait's bare name collides with an already-registered trait of the same name (e.g. an imported/prelude trait), lowering resolves THIS definition's own info via scope-aware lookup (current package first), and uses that resolved (possibly package-qualified) registry key when computing the vtable/method-order layout — so a def is never bound to the wrong same-named trait's methods or vtable slot order. The trait's LIR name key itself stays the bare name; impls/vtables remain bare-keyed and are disambiguated by target type.

> **Uncertainty:** The collision/registration mechanics of find_trait_iter_scoped live outside this slice; only the consuming behavior at trait-lowering is directly evidenced here.

Source: `src/compiler/sema_decl.cpp#L1625-L1637`

### `trait.def.modifiers` — Trait definition modifiers and supertraits

`[pub] [auto|unsafe] trait NAME [<params>] [: super + super ...] { items }` defines a trait; the supertrait list after `:` is a `+`-separated list of trait bounds. `auto` and `unsafe` are mutually-exclusive leading modifiers. Generic params, supertraits, both, or neither may be present.

```logos
trait Foo: Bar + Baz { }

auto trait Send { }

unsafe trait Sync { }

pub trait Iterator<T> { }
```

Source: `tools/peg_gen/grammars/logos.peg#L819-L876` · `tools/peg_gen/grammars/logos.peg#L828-L829`

### `trait.def.vtable-layout-supertrait-closure` — Trait vtable layout spans the supertrait closure with ordered upcast targets

A trait object's vtable layout is the method order over the trait's supertrait closure, together with an ordered list of upcast target supertraits; this single layout is the canonical slot order used for dispatch and upcasting.

Source: `src/compiler/sema_decl.cpp#L1670-L1682`

## Trait visibility (`vis`)

### `trait.vis.placeholder-carries-pub` — predeclared trait carries its visibility

A trait name predeclared during name-collection carries its declared `pub` visibility, so a cross-package reference resolving before the trait's body is collected sees the correct public/private status.

Source: `src/compiler/sema_collect.cpp#L468-L496`

## Trait methods and method calls (`method`)

### `expr.method.typeparam-inference-from-args` — Method type params inferred by unifying params with arg types

Absent turbofish, a trait method's type parameters are inferred by unifying each substituted formal parameter type (seeded with Self → receiver type and any supertrait-derived bindings) against the corresponding argument's type.

Source: `src/compiler/sema_expr.cpp#L7498-L7524`

### `expr.method.typevar-trait-method-dispatch` — Method on a bounded type parameter resolves via its trait bounds

If the receiver (after peeling one reference/raw-pointer layer) is a type parameter `T` (or, conditionally, an associated-type projection), the method is resolved against the traits in `T`'s declared bounds; the concrete impl is selected later during monomorphization.

Source: `src/compiler/sema_expr.cpp#L7320-L7340` · `src/compiler/sema_expr.cpp#L7367-L7375` · `src/compiler/sema_expr.cpp#L7439-L7443`

### `generic.method.arg-inference` — Method type args inferred from args

Method-level type arguments not bound by turbofish or receiver are inferred from the actual argument expressions (param offset skips `self`), seeding the substitution context.

Source: `src/compiler/sema_expr.cpp#L8861-L8865`

### `generic.method.bounds-check` — Method type-arg bounds enforced

Inferred/explicit method type arguments must satisfy their type parameters' bounds.

Source: `src/compiler/sema_expr.cpp#L8973`

### `generic.method.inference-complete` — All method type args must be inferred

Every method type parameter must be bound (by turbofish, receiver, or inference); failure to bind all is an error.

Source: `src/compiler/sema_expr.cpp#L8967-L8972`

### `generic.method.recv-formal-unify` — Impl params inferred from receiver formal

Impl/method type parameters appearing only in the receiver formal (e.g. `impl<T> Pin<&T> { fn get_ref(&self) -> &T }`) are inferred by unifying the receiver formal against the actual receiver type, peeling one ref/ptr layer on either side to match shapes.

Source: `src/compiler/sema_expr.cpp#L8838-L8860`

### `generic.method.recv-typeargs-bind` — Receiver type-args seed substitution

The receiver's struct/zoned-struct or enum type arguments are bound positionally to the receiving type's type parameters and used as the substitution context for checking and inferring method-level type arguments.

Source: `src/compiler/sema_expr.cpp#L8750-L8776`

### `generic.method.turbofish-wins` — Method turbofish overrides inference

Explicit method-level type arguments `recv.m::<T...>(args)` bind the method-level type parameters (the tail of the method's type-param list that is not shared with the receiving type) positionally; inference runs only for the remaining unbound positions.

Source: `src/compiler/sema_expr.cpp#L8784-L8837`

### `trait.method.ambiguous-bound-method` — Ambiguous method across sibling trait bounds

If a bounded receiver's set of bounds (including supertraits, searched without early-exit on first match) yields a method of the sought name from two different traits, the call is rejected: "method `'<name>'` is ambiguous for type parameter `'<T>'` (matches traits `'<A>'` and `'<B>')"`.

Source: `src/compiler/sema_expr.cpp#L7458-L7467`

### `trait.method.arg-count-mismatch` — Bounded-generic method call arg-count check

A bounded-generic trait-method call's explicit argument count must equal the chosen trait method's declared parameter count minus one (for `self`); otherwise: "method call `'<name>'`: expected `<n>` args, got `<m>"`.

Source: `src/compiler/sema_expr.cpp#L7539-L7544`

### `trait.method.arg-type-mismatch` — Post-coercion argument type mismatch diagnostic

After coercion, if an argument's type still does not match the method's substituted parameter type (and neither side is `Error`/`TypeVar`/`AssocType`, which defer to monomorphization), the call is rejected: "method `'<name>'` arg `<i>`: expected `<expected>`, got `<got>"`.

Source: `src/compiler/sema_expr.cpp#L7578-L7587`

### `trait.method.assoc-type-projection-dispatch` — Associated-type projection method dispatch (Gap-3)

A method call on an associated-type projection receiver `G::R` dispatches via `R`'s declared bounds (`type R: HasId` on the owning trait) exactly like a bounded TypeVar, but only when at least one bound supplies a non-default method of that name; if the only provider is a default method (typically reached via a blanket `impl<T> Tr for T`), dispatch defers to the pre-existing blanket-impl path instead.

> **Uncertainty:** The non-default-only gating is a targeted fix for a prior mono crash on blanket-supplied defaults; exact boundary conditions are inferred from the accompanying comment, not independently derived.

Source: `src/compiler/sema_expr.cpp#L7378-L7413` · `src/compiler/sema_expr.cpp#L7495-L7505`

### `trait.method.blanket-extension-bound` — Blanket-impl-derived transitive bound for method dispatch

For a bounded generic `T: B` (directly, or via a supertrait of one of T's bounds), a blanket impl `impl<U: B> Ext for U {}` is treated as establishing `T: Ext` transitively for method-dispatch purposes, making Ext's methods resolvable on `T` — including calls inside the blanket impl's own default-method bodies, where Self is the blanket type-var.

Source: `src/compiler/sema_expr.cpp#L7507-L7532`

### `trait.method.bounded-typevar-dispatch` — Method dispatch on a bounded generic type-param

A method call on a receiver whose type is a bounded generic type-param `T` (or a raw/ref/mut-ref thereof) resolves the callee's signature against a trait method declared by one of `T`'s bounds (searched depth-first over the bound trait and its supertraits); the concrete impl is resolved later, at monomorphization.

Source: `src/compiler/sema_expr.cpp#L7367-L7414` · `src/compiler/sema_expr.cpp#L7452-L7490`

### `trait.method.default-body` — Trait method default body

A trait method that provides a body has a default implementation; impls may omit it and inherit the default.

Source: `src/compiler/sema_collect.cpp#L2632-L2636`

### `trait.method.disambiguating-mangle` — Colliding same-name/signature trait methods coexist via trait-qualified mangling

When two distinct traits (or a trait and an inherent impl) define a method with identical name and parameter signature on the same target type, the flat `<target>__<method>` naming cannot hold both: on first collision, the pre-existing entry is re-keyed to a trait-qualified base `<target>__<trait>[$<typeargs>]__<method>` and the new method is registered under its own trait-qualified base; the trait's concrete type arguments are folded into the base so `impl Trait<u64> for X` and `impl Trait<u8> for X` mangle distinctly. Non-colliding methods keep the plain `<target>__<method>` base unchanged.

Source: `src/compiler/sema_collect.cpp#L4943-L5047` · `src/compiler/sema_collect.cpp#L4731-L4835`

### `trait.method.inherent-preferred` — Inherent method preferred over same-name trait method

When an inherent (non-trait) method and a trait method share name and signature on the same type, the inherent method is preferred: it keeps the plain `<target>__<method>` base so concrete-receiver dispatch finds it directly, and only the trait method is re-keyed to its trait-qualified base. A `T: Trait`-bounded dispatch resolves through the trait-qualified name, never the inherent method.

Source: `src/compiler/sema_collect.cpp#L4978-L4979` · `src/compiler/sema_collect.cpp#L4999-L5013`

### `trait.method.multi-trait-ambiguity` — Method provided by multiple traits is ambiguous

If a method name `m` on type `S` is provided by more than one trait, the plain unqualified call `s.m(...)` is an error; the call must be disambiguated via a trait-bounded generic context or an explicit trait-qualified call.

> **Divergence (vs Rust):** A1: collision removes the plain base from the registry; Rust resolves by receiver/inference where unambiguous

Source: `src/compiler/sema_expr.cpp#L8683-L8700`

### `trait.method.overload-mangling` — Method symbol resolution under overload mangling

A method reference `<struct>__<method>` resolves to the actual mangled symbol, which may be package-qualified ([pkg.]Base__method) and may carry an overload-mangling suffix `__f__<sig>` or `__g__<sig>`; the bare convention name is the fallback when no mangled match exists.

Source: `src/compiler/mlir_gen_impl.hpp#L238-L281`

### `trait.method.self-receiver` — Trait method self-receiver inference

A trait method's first parameter is the `self` receiver iff it lacks an explicit type (`self`/`&self`/`&mut self`) or is named `self`. For an untyped `&self`/`&mut self`, the receiver type is synthesized as `Self` / `&Self` / `&mut Self` (mut taken from the param's mut marker).

Source: `src/compiler/sema_collect.cpp#L2563-L2589`

### `trait.method.signatures` — Trait method declarations

A trait item is a method declaration (`[unsafe] fn NAME [<params>] (params) [-> T] [where ...] (block | ';')`) — body-bearing alts give a default impl, `;`-terminated alts are required methods — or an associated type/const. Method names may be `new`/`null` keywords. A `where` clause may follow the signature (before block or `;`); on a default body it gates per-impl default synthesis (skip the default when the bound fails for the impl's concrete type).

```logos
fn next(self) -> Option<Item>;

fn max(self) -> Item where Item: Ord { ... }

fn new() -> Self;
```

Source: `tools/peg_gen/grammars/logos.peg#L885-L963` · `tools/peg_gen/grammars/logos.peg#L933-L951`

### `trait.method.supertrait-dag-search` — Supertrait DAG search for bounded-receiver method lookup

Method lookup on a bounded receiver searches the directly-named bound trait's own methods first; if not found, it recurses into all of that trait's supertraits (a cycle-guarded DAG walk via a visited-set), composing a substitution that maps `Self` and each supertrait's formal type-params to concrete types resolved through the enclosing bound's substitution.

Source: `src/compiler/sema_expr.cpp#L7447-L7484`

### `trait.method.trait-typearg-distinct` — Distinct trait type-args disambiguate identically-named methods

Two impls of the same trait NAME but with different concrete trait type-arguments on the same target (e.g. `impl Trait<u64> for X` vs `impl Trait<u8> for X`) providing a same-name+signature method are NOT merged as a collision; each is mangled with its own trait-type-arg suffix (`X__Trait$u64__m` vs `X__Trait$u8__m`) so both coexist distinctly, and re-keying an existing entry uses ITS OWN trait type-args (not the new method's) to avoid re-colliding the two.

Source: `src/compiler/sema_collect.cpp#L4954-L4960` · `src/compiler/sema_collect.cpp#L4990-L4994` · `src/compiler/sema_collect.cpp#L5024-L5025` · `src/compiler/sema_collect.cpp#L4742-L4749` · `src/compiler/sema_collect.cpp#L4778-L4782` · `src/compiler/sema_collect.cpp#L4808-L4821`

### `trait.method.turbofish-overrides-inference` — Turbofish overrides inference for bounded-generic method type-params

For a bounded-generic-dispatched trait method with its own method-level type-params, explicit turbofish type args supplied at the call site bind directly to those type-params, taking priority over inference (which otherwise unifies argument-expression types against the method's substituted parameter types).

Source: `src/compiler/sema_expr.cpp#L7551-L7571`

### `trait.method.unsafe-requires-unsafe-context` — Unsafe trait method requires unsafe context

Calling a trait method marked `unsafe` through bounded-generic dispatch requires an enclosing `unsafe` context; otherwise: "call to unsafe method `'<name>'` requires unsafe context".

Source: `src/compiler/sema_expr.cpp#L7535-L7537`

### `trait.method.where-self-sized` — where Self: Sized excludes method from vtable

A trait method with a `where Self: Sized` bound is flagged requires-sized-self: it is excluded from the trait's vtable and thus does not affect object safety. Other where-bounds on trait type-params are recorded for per-impl default-synthesis gating.

Source: `src/compiler/sema_collect.cpp#L2599-L2631`

## Unsafe methods (`unsafe-method`)

### `trait.unsafe-method.requires-unsafe-context` — Calling an unsafe method requires an unsafe context

A call to a method marked unsafe outside an unsafe context is an error: "call to unsafe method `'<name>'` requires unsafe context".

Source: `src/compiler/sema_expr.cpp#L8025-L8028`

## Default methods (`default-method`)

### `trait.default-method.conditional-on-param-bounds` — Conditional default methods gated by per-method param bounds

A per-method `where` bound whose subject is a trait type-parameter (e.g. `where Item: Ord` on `fn max()` in `Iterator<Item>`) gates default-method synthesis per impl: when an impl substitutes the type-parameter with a concrete type, the bound is rewritten under the substitution and checked; if unsatisfied, the default method is not synthesized for that impl (the method is simply unavailable), matching Rust's conditional-default-method semantics.

Source: `src/compiler/sema_impl.hpp#L2624-L2637`

### `trait.default-method.conditional-where-gate` — Per-method where-clause gates conditional default synthesis

A trait default method with per-method where-bounds (`param_name: trait_name` pairs referring to the trait's own type params) is synthesized for a given non-blanket impl only if, for every bound whose trait-param maps to a fully concrete impl trait-argument, that concrete argument satisfies the bound trait (via the recursive impls_ probe `sema_has_impl_recursive`). Any bound found unsatisfied silently skips synthesis of the default for this impl.

Source: `src/compiler/sema_decl.cpp#L2361-L2422`

### `trait.default-method.synthesised-when-not-overridden` — Trait default methods copied into an impl unless explicitly overridden

For each method `m` of the implemented trait with a default body (m.has_default), if the impl block did not explicitly define it (mangled name `lower_target__m.name` absent from `overridden`), the compiler lowers `m.default_ast` under `lower_target` as an impl method, giving the impl a concrete copy of the default.

Source: `src/compiler/sema_decl.cpp#L2352-L2360` · `src/compiler/sema_decl.cpp#L2484-L2486`

### `trait.default-method.where-bounds-carried-to-mono` — Deferred where-bounds re-expressed for mono's method_bound_ok gate

For a non-blanket impl, every per-method where-bound of a synthesized default is re-expressed as a (subject-type, trait-name) pair — subject = the impl's own trait-argument at that trait-param's index — and attached to the emitted function's WHERE_TYPE_BOUNDS. Additionally, on the struct-template path, if that subject is itself a bare TypeVar matching one of the impl's own type params, the bound trait is appended (deduplicated) to that IMPL_TYPE_PARAMS entry's bound list, so mono's method_bound_ok can reject a clone whose substituted concrete argument fails the bound.

Source: `src/compiler/sema_decl.cpp#L2487-L2516` · `src/compiler/sema_decl.cpp#L2538-L2574`

### `trait.default-method.where-gate-deferred-when-typevar` — Where-bound gate defers to mono when the trait-arg still mentions a TypeVar

A trait-argument that recursively mentions a TypeVar or Error anywhere in its structure (through pointee, elem, type_args, tuple_elems, closure_params, or closure_ret) is not decidable at sema; the where-bound gate is deferred (default synthesized regardless) rather than treated as unsatisfied. Blanket impls (Self = TypeVar) and any generic-impl trait-argument mentioning a TypeVar always take this deferred path.

Source: `src/compiler/sema_decl.cpp#L2374-L2421`

## Associated items (`assoc`)

### `trait.assoc.eq-clause-satisfaction` — Associated-type equality clauses must hold

A bound `Trait<Assoc = Type>` holds for a concrete type only if each expected associated type equals the impl's actual `type Assoc = ...` resolution after substitution.

Source: `src/compiler/sema_impl.hpp#L3282-L3290`

### `trait.assoc.equality-bound-satisfaction` — Associated-type equality bound satisfaction

A bound of form `T: Trait<Assoc = X>` is satisfied iff the impl of `Trait` for the concrete type defines `type Assoc = Y` with Y type-equal to X. The impl's binding is looked up first for the concrete name, then for the base (template) name. If no direct impl provides Assoc, a matching blanket impl whose bound-trait and extra-bounds are all satisfied supplies its `type Assoc = ...`, with the blanket target type-var substituted by the concrete type and recursively resolved before the equality check.

```logos
K: Primitive => K: HasPrim<P = K::Prim = i32>
```

Source: `src/compiler/sema.cpp#L3358-L3420`

### `trait.assoc.gat-own-params` — Associated type definitions carry independent GAT params

An associated-type implementation (`type Item<T> = ...` inside an `impl`) records its own generic type parameters (from the GAT declaration) separately from the enclosing impl's type parameters; both parameter lists are retained for substitution at each projection use-site.

Source: `src/compiler/sema_impl.hpp#L2988-L2995`

### `trait.assoc.self-projection-during-collect` — Self::Item resolves through the impl's trait before impl registration

Inside a trait definition, `Self::Item` resolves via the trait currently being defined; inside an impl's method signatures during signature collection, `Self::Item<X>` resolves via the impl's trait even though the impl itself is not yet present in the impl registry at that point.

Source: `src/compiler/sema_impl.hpp#L3006-L3013`

### `trait.assoc.type-and-const` — Associated type and const declarations in traits

Trait associated items: `type NAME [<params>] [= T] ;` (optional default and optional bound list `: B + B`) declares an associated type; `const NAME : T [= expr] ;` declares an associated const, optionally with a default value.

```logos
type Item;

type Item: Ord = i32;

const N: usize = 0;
```

Source: `tools/peg_gen/grammars/logos.peg#L952-L963`

### `trait.assoc.typearg-suffixed-lookup` — Associated-type lookup prefers trait-arg-suffixed key

When resolving an associated type of `Trait` for a target type, if the current impl context fixes the trait's type-arguments, the lookup first tries a key suffixed with those arguments — so two `Trait<T>` impls for one type at distinct `T` register and resolve their associated types independently — falling back to the unsuffixed `Trait::target::aname` key.

Source: `src/compiler/sema.cpp#L3015-L3029`

## Associated types (`assoc-type`)

### `trait.assoc-type.bound-requalify-projection` — Associated-type return projection re-qualified to the dispatch bound's trait args

If a trait method's substituted return type is an associated-type projection (`Self::Assoc`-style) whose trait qualifier was resolved bare (no type arguments) at the trait declaration, and the receiver's matching bound specifies concrete trait type arguments, the projection's trait qualifier is rewritten to the type-argument-qualified trait name, so the returned associated type resolves to the same concrete impl selected by the bound (disambiguating between multiple `impl Trait<X>` for one receiver type).

Source: `src/compiler/sema_expr.cpp#L7718-L7741`

### `trait.assoc-type.completeness-with-default` — Associated-type completeness with trait default fallback

A non-blanket trait impl must provide every associated type the trait declares; if the impl omits one, the trait's declared default associated type is used, and only the absence of both impl definition and trait default is an error. Blanket impls skip this check (assoc types are per-instantiation).

Source: `src/compiler/sema_collect.cpp#L3617-L3642`

### `trait.assoc-type.copied-into-impl` — Associated-type bindings copied into the impl's ASSOC_TYPES from a suffix-disambiguated key

After lowering an impl, entries in assoc_type_impls_ whose key starts with `trait_name<trait_targ_suffix(impl_trait_args)>::stored_target::` are copied into the impl's ASSOC_TYPES array (name = key suffix after the prefix, type = entry.type). stored_target is the synthetic blanket name for blanket impls, else the plain target; the trait-arg suffix disambiguates multiple `Trait<T>` impls of the same target (empty suffix for a non-generic trait leaves the bare prefix unchanged).

Source: `src/compiler/sema_decl.cpp#L2607-L2629`

### `trait.assoc-type.default-and-gat` — Associated type defaults and GATs

An associated type may carry its own type params (GAT, e.g. `type Item<T>`), trait-bound constraints, and a default RHS (`type Item = i32;`); an impl that omits the assoc type falls back to the declared default.

Source: `src/compiler/sema_collect.cpp#L2491-L2520`

### `trait.assoc-type.dual-impl-ambiguous-projection` — Ambiguous bare associated-type projection across generic-trait impls

When two impls of a generic trait `Trait<T>` for one target at distinct T each declare the same associated type, the bare projection `X::Assoc` becomes ambiguous and must be written `<X as Trait<T>>::Assoc`; the unsuffixed projection key is first-impl-wins and is erased once a second distinct-args impl appears so a bare lookup fails.

> **Divergence (vs Rust):** G156-1: Rust requires fully-qualified `<X as Trait<T>>::Assoc` for ambiguous projections; Logos matches by erasing the ambiguous bare key.

Source: `src/compiler/sema_collect.cpp#L3235-L3248` · `src/compiler/sema_collect.cpp#L3281-L3295`

### `trait.assoc-type.dual-trait-typearg-key` — Trait-type-arg-suffixed keying disambiguates two impls of the same trait with different type-args

When a type has two impls of the SAME trait differing only in trait type-arguments (`impl Trait<A> for X`, `impl Trait<B> for X`), each impl's mangled method base carries a `$G<n>$<arg1>$..` suffix on the trait name (`X__Trait$G1$u64__m`, `$G` chosen so bare_fn_name's plain-`$`-as-pkg-separator stripping leaves it intact) so collect/lower/dispatch key on (trait, type-args) rather than trait name alone. An AssocType's trait_name may itself carry this suffix; `strip_trait_targ_suffix` recovers the bare trait name by truncating at the first `$`. `find_assoc_type_entry` looks up the suffixed key first (when the current impl context supplies concrete trait args via current_impl_trait_args_) then falls back to the plain key; null if neither exists.

Source: `src/compiler/sema_impl.hpp#L4336-L4354`

### `trait.assoc-type.duplicate` — Duplicate associated type in impl rejected

An impl block must define each associated type at most once for a given (trait, trait-type-args, target, name); a second definition with the same key is an error.

Source: `src/compiler/sema_collect.cpp#L3247-L3252`

### `trait.assoc-type.gat-and-default` — Associated types may be generic (GAT) and have a default

A trait associated type may carry its own type-params (`type Item<T>` — a GAT) with bounds, and may declare a default (`type Item = i32;`) used by impls that omit it.

Source: `src/compiler/sema_impl.hpp#L2642-L2647`

### `trait.assoc-type.gat-arity-match` — Impl GAT arity must match trait declaration

The number of type parameters on an impl's associated type definition must equal the count the trait declares for that associated type.

Source: `src/compiler/sema_collect.cpp#L3261-L3273`

### `trait.assoc-type.gat-no-shadow-impl-param` — GAT params must not shadow impl type params

A generic associated type's own type parameters (`type Item<T> = ...`) must not share a name with any of the enclosing impl's type parameters.

Source: `src/compiler/sema_collect.cpp#L3253-L3260`

### `trait.assoc-type.stamp-bound-targs-on-return` — Associated-type return projection carries the bound's concrete trait type-args

If a dispatched trait method's substituted return type is an associated-type projection, its trait name is rewritten to the trait-name suffixed with the bound's concrete trait type-args (`Trait<...>` -> mangled args suffix), so projections from two distinct `impl Trait<T>` for one type, and the caller's declared `-> P::Item`, resolve to the same impl.

> **Uncertainty:** Disambiguation among multiple `Trait<T>` impls; tied to G156-1 trait-type-arg mangling (still narrow per memory).

Source: `src/compiler/sema_expr.cpp#L7671-L7694`

## Associated consts (`assoc-const`)

### `trait.assoc-const.accessor-for-generic-projection` — Zero-arg accessor synthesized for concrete-impl associated consts

For a concrete trait impl (non-empty trait_name, no impl-level type params, not a struct-template target), an ASSOC_CONST_IMPL item additionally emits a pub zero-arg accessor function `Target__kassoc_<name>` (METHOD_BASE `kassoc_<name>`) whose body returns the const's value, expression-retyped to its declared TYPE if present. This lets a generic projection `T::<name>`, lowered elsewhere to a `T__kassoc_<name>()` call, resolve to `Target__kassoc_<name>` once mono substitutes T := Target. Blanket-impl and generic-target associated consts get no accessor here.

Source: `src/compiler/sema_decl.cpp#L2304-L2339`

### `trait.assoc-const.completeness-with-default` — Associated-constant completeness with trait default fallback

A trait impl must provide every associated constant the trait declares; if omitted, the trait's default value is projected into the impl, and only the absence of both impl value and trait default is an error.

Source: `src/compiler/sema_collect.cpp#L3643-L3663`

### `trait.assoc-const.default` — Associated const declaration and default

An associated const declares a type, and may provide a default value (`const X: i32 = 42;`); an impl that omits the const falls back to the recorded default.

Source: `src/compiler/sema_collect.cpp#L2523-L2542`

### `trait.assoc-const.default-projected-to-impl` — Associated const default projected into omitting impls

A trait associated const may declare a default (`const X: i32 = 42;`); each `impl Trait for T` that omits the const inherits the default so `T::CONST` resolves to it.

Source: `src/compiler/sema_impl.hpp#L2649-L2657`

### `trait.assoc-const.generic-projection` — Associated const projection on a bound type parameter

For a path `T::C` where `T` is a type parameter with bound `T: Tr` and `C` is an associated const declared by `Tr` (or by any transitive supertrait of `Tr`), the expression resolves to a per-impl accessor that yields the const's value; its type is the declared associated-const type, defaulting to i64 when undeclared.

> **Uncertainty:** i64 default for an associated const with no declared type is implementation-driven; Rust requires an explicit type.

Source: `src/compiler/sema_expr.cpp#L11540-L11567`

### `trait.assoc-const.inherent-allowed` — Inherent associated constants permitted

An inherent impl (no trait) may declare associated constants `const C: T = ...;`, registered under the target type.

Source: `src/compiler/sema_collect.cpp#L3296-L3308`

### `trait.assoc-const.supertrait-closure` — Associated const lookup closes over supertraits

Resolution of an associated const on a bound type parameter searches the transitive supertrait closure of every stated bound, deduplicated, so a const declared on a supertrait of a stated bound is found.

Source: `src/compiler/sema_expr.cpp#L11544-L11565`

### `trait.assoc-const.type-match` — Impl associated-constant type must match trait declaration

An impl's associated constant must have a type equal to the type the trait declares for that constant.

Source: `src/compiler/sema_collect.cpp#L3313-L3326`

## Supertraits (`supertrait`)

### `trait.supertrait.blanket-satisfies` — Blanket impl of a supertrait discharges the supertrait-impl obligation transitively

The requirement `impl Super for Type` is satisfied if a blanket impl `impl<T: Bound> Super for T` (optionally with extra where-bounds) exists and `Type` implements `Bound` (directly or transitively via another blanket impl) and every extra bound on the blanket impl.

Source: `src/compiler/sema_collect.cpp#L5189-L5207`

### `trait.supertrait.copy-marker-no-vtable` — Copy as a supertrait contributes no vtable slot

`Copy` is a marker supertrait: it is skipped both when building a trait's dyn-vtable layout (no method/upcast slot) and when checking supertrait-impl completeness.

Source: `src/compiler/sema_collect.cpp#L5129` · `src/compiler/sema_collect.cpp#L5146` · `src/compiler/sema_collect.cpp#L5184`

### `trait.supertrait.declared-bounds` — Trait may declare supertrait bounds

A trait declaration `trait Foo: Display + Into<i32>` records supertrait bounds (trait name + type-args) that every impl of Foo must also satisfy.

Source: `src/compiler/sema_impl.hpp#L2668`

### `trait.supertrait.impl-completeness` — Implementing a trait requires implementing its supertraits

For every `impl Trait for Type`, and for every known supertrait `Super` of `Trait` (transitively, `Copy` exempt as a marker), an `impl Super for Type` must exist — directly, via a satisfying blanket impl (`impl<T: Bound> Super for T` where Type implements Bound directly or through another blanket), or via a self-bound on Type as a blanket impl's type parameter — otherwise it is a compile error "impl {Trait} for {Type}: missing impl {Super} for {Type} (required by supertrait)". The check is deferred until after collection, so definition order within or across files does not matter; the supertrait chain is resolved from the impl's own canonically-captured trait, not a same-named shadowing trait.

See also: `trait.supertrait.blanket-satisfies`, `trait.supertrait.self-bound-satisfies`.

Source: `src/compiler/sema_collect.cpp#L5170-L5188` · `src/compiler/sema_collect.cpp#L5175-L5182` · `src/compiler/sema_collect.cpp#L5194-L5217` · `src/compiler/sema_collect.cpp#L5229-L5230`

### `trait.supertrait.impl-required` — supertrait impls must be satisfied

For every `impl Trait for T`, the impls of all of Trait's supertraits for T must also be present (checked order-independently after all impls are collected, since `impl Foo for T` and `impl Super for T` may appear in either file order).

> **Uncertainty:** check_supertrait_impls() body is defined outside this unit; only its invocation site is evidenced here.

Source: `src/compiler/sema_collect.cpp#L691-L693`

### `trait.supertrait.known` — Supertrait must name a known trait

Every supertrait listed on a trait must refer to a known trait (except the `Copy` marker); otherwise it is an "unknown supertrait" error.

Source: `src/compiler/sema_collect.cpp#L4928-L4937` · `src/compiler/sema_collect.cpp#L4930`

### `trait.supertrait.self-bound-satisfies` — A blanket impl's own where-bound can discharge its Self type's supertrait obligation

For a blanket impl `impl<T: Super> Child for T {}`, the supertrait requirement `T: Super` is discharged directly by the impl's own where-clause bound on `T` when `Type` is itself that impl's bounded type parameter and one of its declared bounds equals `Super`, or transitively reaches `Super` through that bound trait's own supertrait chain.

Source: `src/compiler/sema_collect.cpp#L5155-L5168` · `src/compiler/sema_collect.cpp#L5208-L5228`

### `trait.supertrait.unknown-name-error` — A trait's supertrait list must name only declared traits

For every declared trait T with a supertrait list, each supertrait name (other than `Copy`) must refer to a trait that exists in the program; otherwise it is a compile error "trait {T}: unknown supertrait '{name}'". This check runs over all declared traits, including ones never implemented.

Source: `src/compiler/sema_collect.cpp#L5144-L5152`

### `trait.supertrait.via-blanket` — Supertrait satisfied by a blanket impl

A supertrait requirement `T: Super` is satisfied if a blanket `impl<U: Bound> Super for U` exists and T implements `Bound` (and all extra bounds), directly or via another blanket.

Source: `src/compiler/sema_collect.cpp#L4973-L4991`

### `trait.supertrait.via-self-bound` — Supertrait satisfied by impl's own type-param bound

For a blanket `impl<T: Super> Child for T {}`, the supertrait requirement on T is discharged by the impl's own where-clause bound on T, where the bound trait directly is or transitively reaches the required supertrait via its own supertrait chain.

Source: `src/compiler/sema_collect.cpp#L4992-L5012`

## Higher-ranked trait bounds (`hrtb`)

### `trait.hrtb.universal-bijective` — HRTB bound satisfaction requires universal-position, bijective lifetime mapping

When a trait bound's type-args carry lifetimes, they must align against the matching impl's trait_type_args: a bound lifetime that is empty/'static must equal the impl-side lifetime literally or the impl-side lifetime must be one of the impl's universally-quantified lifetime params; a named (non-static) bound lifetime requires the impl-side lifetime be universally quantified and the impl-lifetime→bound-lifetime mapping be injective (1-1) across the whole walk. After unification, an impl-level outlives constraint `'a: 'b` is rejected as unsatisfiable if both 'a and 'b map to bound-side HRTB binder (skolem) lifetimes and are not the same binder.

> **Uncertainty:** Region-soundness rule; full derivation lives in sema_collect.cpp's region_ok per the source comment.

Source: `src/compiler/mono_clone.cpp#L5359-L5450`

## Implementations (`impl`)

### `generic.impl.typarams-combine` — Impl-block type-params combine with method's own

For a method collected inside a generic impl block, the impl block's own type-parameters are prepended to the method's type_params (impl_type_params_ ++ info.type_params) — the method's generic scope is the union of impl-level and method-level params. When the impl itself is generic, the impl's target type pattern is additionally stamped onto the method (impl_target_pattern) for later specialization-pattern matching.

Source: `src/compiler/sema_collect.cpp#L4914-L4922`

### `trait.impl.assoc-type-self-scope` — Impl-scoped resolution of `Self::AssocType<...>` (GAT projection)

Within an impl block's method signatures and bodies, a `Self::AssocType<X>` projection resolves through the enclosing impl's own trait (its declared associated types), with `Self` already substituted to the impl's concrete target type — the impl's trait name is pushed/popped as scope around the whole impl body specifically to make this per-impl GAT-projection resolution possible.

> **Uncertainty:** The projection-resolution logic consuming current_impl_trait_name_ lives outside this slice; only the scope push/pop is directly evidenced here.

Source: `src/compiler/sema_decl.cpp#L1697-L1706`

### `trait.impl.blanket-and-semantics` — Blanket impl bounds combine conjunctively; empty bounds = impl-for-all

A blanket impl is satisfied iff ALL of its bounds hold (primary bound, if any, followed by extra bounds, conjoined). A blanket impl with an empty primary bound and no extra bounds is an unconditional impl-for-all (always satisfied).

Source: `src/compiler/mono_clone.cpp#L5005-L5015`

### `trait.impl.blanket-default-method` — Blanket impl inherits trait default methods

For a blanket impl `impl<T: Bound> Trait for T {}`, each non-overridden trait default method is synthesized as a generic fn with Self = the blanket type variable and a blanket-impl entry is recorded so dispatch surfaces it on any concrete receiver satisfying Bound.

Source: `src/compiler/sema_collect.cpp#L3569-L3600`

### `trait.impl.blanket-detection` — An impl whose target is one of its own type parameters is a blanket impl

A trait impl whose target type equals one of the impl's own type parameters (`impl<T: B> Trait for T`) is a blanket impl. The parameter's first bound becomes the impl's bound trait, and its associated-type equalities together with any additional bounds (and their associated-type equality clauses) are recorded, so the blanket can be instantiated at call sites on any concrete type satisfying those bounds.

Source: `src/compiler/sema_collect.cpp#L3114-L3141` · `src/compiler/sema_collect.cpp#L3213-L3225` · `src/compiler/sema_decl.cpp#L1968-L2006`

### `trait.impl.blanket-impl-fallback` — Method-call dispatch falls back to blanket impls

A blanket impl `impl<T: Bound1 [+ Bound2...]> Trait for T { ... }` is consulted for method-call resolution when direct lookup on the concrete receiver type fails; it matches only if the receiver satisfies the primary bound AND every extra bound (AND-filter across all bounds), and, per associated-type-equality clauses declared on each bound, only if those associated-type equalities also hold for the receiver.

Source: `src/compiler/sema_impl.hpp#L3027-L3047`

### `trait.impl.blanket-keyed-by-bound` — distinct blanket impls keyed by bound trait

Blanket impl methods register under a synthetic key incorporating the implemented trait, the bound trait, and the target typevar (`$blanket$Trait$Bound$T`), so two blankets of the same trait over different bounds (e.g. `impl<X: Primitive> Tr for X` vs `impl<X: PodRef> Tr for X`) register separately and do not collide with `T::method` lookups on unrelated generics.

Source: `src/compiler/sema_collect.cpp#L3162-L3173` · `src/compiler/sema_collect.cpp#L3226-L3231`

### `trait.impl.blanket-method-target` — Blanket impl detection, bound-trait capture, and synthetic method target

An impl whose target names one of the impl's own type parameters (`impl<T: Bound> Trait for T`) is a blanket impl: IS_BLANKET is set and BOUND_TRAIT records that type param's first trait bound (with its associated-type equalities as PRIMARY_ASSOC_EQS); additional bounds on the same param are recorded as EXTRA_BOUNDS with per-bound associated-type equalities. Blanket-impl methods are lowered under the synthetic target key `$blanket$<Trait>$<BoundTrait>$<target>` rather than the bare type-param name, so they cannot collide with `T::method` attached for any unrelated generic `T` elsewhere in the program.

Source: `src/compiler/sema_decl.cpp#L2012-L2050` · `src/compiler/sema_decl.cpp#L2165-L2170`

### `trait.impl.canonical-trait-disambiguation` — Supertrait-completeness check uses the impl's own canonically-resolved trait

When checking an impl's supertrait obligations, the trait definition consulted is the one captured canonically at impl-collection time (`impl.canonical_trait`) when set, not whatever trait currently holds the bare name slot — so `impl Container for Foo` (a user trait) is checked against the user `Container`'s supertraits, not an unrelated same-named stdlib trait's.

Source: `src/compiler/sema_collect.cpp#L5175-L5181`

### `trait.impl.default-method-visibility` — Default trait methods inherit trait visibility

A default trait method registered into an impl is marked public (inheriting trait accessibility); all newly-registered overloads of that method are marked pub, not only the first.

Source: `src/compiler/sema_collect.cpp#L3601-L3608`

### `trait.impl.default-on-scalar-self` — Self bound to scalar primitive for default-method bodies

For `impl Trait for <scalar primitive>` (i8..i128/u8..u128/usize/isize/f32/f64/bool/char), Self resolves to the primitive so inherited default method bodies using `&Self` typecheck; this binding is restricted to scalar kinds and excludes `str` and enum targets, whose defaults keep Self as a type variable.

Source: `src/compiler/sema_collect.cpp#L3545-L3567`

### `trait.impl.distinct-trait-args-coexist` — Multiple impls of one trait at distinct args coexist

Multiple impls of the same trait name for one Self type at distinct concrete trait type-args (e.g. `From<i8> for i32` and `From<i16> for i32`, or a concrete `Iterator<i32>` vs a generic `Iterator<&T> for VecIter<T>`) are distinct impls: an all-impls registry (keyed same as the single-valued impl registry) retains every one so a parametrized bound `T: Trait<Args>` can be checked against actual trait-args; coherence/duplicate-detection keys separately on `Trait[arg1,...]::Target`; methods are mangled with the impl's concrete trait type-args so they coexist.

Source: `src/compiler/sema_impl.hpp#L2974-L2987` · `src/compiler/sema_impl.hpp#L3014-L3017`

### `trait.impl.fnptr-target-key` — Fn-pointer-target impls keyed by parameter arity

An impl targeting an fn-pointer value type (`fn(A,B)->C`) is looked up under key `$fnptr$N` where N is the parameter count, rather than under the literal fn-pointer type string.

Source: `src/compiler/mono_clone.cpp#L5109-L5113`

### `trait.impl.foreign-private-trait-error` — impl of a foreign private trait is an error

In `impl Trait for T`, if `Trait` resolves to a trait that is not accessible from the impl site (not pub, or module-only and outside its module), the impl is rejected (privacy error). The check fires at the impl site that introduces the foreign trait name.

> **Divergence (vs Rust):** Note: §4 module/package visibility model; trait must be pub-accessible to be implemented across package boundaries.

Source: `src/compiler/sema_collect.cpp#L2685-L2699`

### `trait.impl.method-completeness` — Trait impl must provide every required method

A trait impl must supply an implementation for every trait method lacking a default; a missing non-default method is an error. Methods with a default that are not overridden are auto-registered from the default body.

Source: `src/compiler/sema_collect.cpp#L3363-L3373` · `src/compiler/sema_collect.cpp#L3502-L3503` · `src/compiler/sema_collect.cpp#L3610-L3613`

### `trait.impl.method-signature-match` — Impl method signature matched against trait by arity and non-receiver param types

An impl method satisfies a trait method when arities agree and each non-receiver parameter type is equal, where a trait parameter that is a type variable or associated-type projection (possibly under &/&mut/*) is treated as polymorphic and matches any concrete impl type; the receiver (param 0) is always skipped.

Source: `src/compiler/sema_collect.cpp#L3389-L3458`

### `trait.impl.method-template-attach` — Generic-impl methods attach to a matching spec or struct template, not free functions

For an impl carrying its own impl-level type parameters, methods attach to a template mono clones (with the impl's type params substituted) rather than being added as free functions: a partial/full struct specialization is preferred when the impl target's generic-instantiation type-arg pattern matches the spec's spec_patterns (equal arity; TypeVar-vs-TypeVar positions agree; concrete positions must be type-equal); otherwise the base struct template of matching name is used, preferring one in the impl's own package over a same-named struct from another package.

Source: `src/compiler/sema_decl.cpp#L2114-L2164`

### `trait.impl.method-unsafe-parity` — Impl method unsafe-ness must match trait method

An impl method's `unsafe` qualifier must match the trait method's: a safe trait method cannot be implemented unsafe and vice versa.

Source: `src/compiler/sema_collect.cpp#L3495-L3501`

### `trait.impl.method-visibility-inherits-trait` — trait-impl methods inherit trait accessibility

Methods in a trait impl take their accessibility from the trait: if the trait is reachable, its impl methods are public (forced is_pub). `pub fn` is disallowed inside trait/trait-impl blocks. Methods of an inherent impl (no trait) keep their explicit pub/private status.

Source: `src/compiler/sema_collect.cpp#L3175-L3212`

### `trait.impl.multi-generic-coherence` — Per-impl trait-arg override for multiple same-target trait instantiations

When multiple `impl Trait<T> for X` blocks exist for the same (Trait, X) pair with different trait type-argument instantiations, each impl's own resolved trait type-args (not the collect-phase coherence map's last-inserted entry, which holds only the last-registered impl's args) are (re-)written into that impl's TRAIT_TYPE_ARGS, so mono keys each instantiation's associated-type impls / method mangling (`Trait$G..$<Args>`) distinctly instead of every impl collapsing onto the last-registered instantiation's args.

Source: `src/compiler/sema_decl.cpp#L2076-L2087`

### `trait.impl.negative-impl` — Negative impl asserts a type does not implement a trait

`impl !Trait for X {}` is a negative impl declaring that X does not implement Trait.

Source: `src/compiler/sema_impl.hpp#L2687`

### `trait.impl.no-standalone-unsafe` — Standalone (inherent) impl cannot be unsafe

`unsafe impl` with no trait (a standalone inherent impl) is an error.

Source: `src/compiler/sema_collect.cpp#L3707-L3709`

### `trait.impl.params-from-impl-header` — impl-level generic params come from the impl header

An impl's own generic parameters are taken from `impl<...>`: for a trait impl `impl<T> Trait for U<T>` from the impl-type-params position, and for an inherent impl `impl<T> U<T>` from the standalone type-params position. These params (and their lifetime params and outlives bounds) are in scope for the target type, trait args, and all method signatures.

Source: `src/compiler/sema_collect.cpp#L2710-L2743` · `src/compiler/sema_collect.cpp#L2748-L2778`

### `trait.impl.ref-target-key` — Reference-type impl lookup key is structure-aware

An `impl Trait for &T` / `&mut T` is looked up under key prefix `$ref_`/`$mut_ref_` followed by the struct name when the pointee is a (possibly zoned) struct, otherwise followed by the full ref's type string (keeping the `&`); this distinguishes `&&i32` from `&i32` — naive stripping of one `&` from the raw type string would incorrectly collide them.

Source: `src/compiler/mono_clone.cpp#L5046-L5063`

### `trait.impl.self-binding` — Self binds to the impl target type

Within an impl, `Self` denotes the target type: for a concrete specialization (`impl U<i32>`) the fully resolved type with its type args; for a generic impl (`impl<T> U<T>`) the type with TypeVar args (including the type's lifetime params); for a blanket impl (`impl<T:B> Trait for T`) the TypeVar `T`; for a primitive target the primitive type.

Source: `src/compiler/sema_collect.cpp#L2972-L3063`

### `trait.impl.self-param-defaults-to-self-ref` — self receiver parameter type defaults to a reference to Self

A method receiver parameter with no written type (`self` / `&self` / `&mut self`) has parameter type a reference to Self, mutable iff the receiver is declared mut.

> **Uncertainty:** Derived from the visibility-promotion re-walk of params; the primary signature construction is in collect_fn.

Source: `src/compiler/sema_collect.cpp#L3195-L3204`

### `trait.impl.self-seeding` — Self is seeded to the impl target for shapes lower_fn's name lookup cannot resolve

Within an impl's methods, `Self` denotes the impl's target type. The compiler explicitly binds `Self` (overriding any stale binding left by a previously-lowered impl) whenever the target's mangled form is not resolvable by lower_fn's ordinary bare-name lookup: unsized-slice/unsized-dyn self-types bind Self to that resolved unsized type; `impl ... for str` binds Self to `UnsizedSlice<u8>`; tuple, fn-pointer, reference, concrete-type-arg (no impl param), and blanket-impl-on-type-variable targets bind Self to the resolved target type. Sized plain struct/datatype/primitive targets fall through to ordinary name lookup instead.

Source: `src/compiler/sema_decl.cpp#L2176-L2181` · `src/compiler/sema_decl.cpp#L2182-L2190` · `src/compiler/sema_decl.cpp#L2191-L2208`

### `trait.impl.str-is-byte-slice` — str impls are keyed under &[u8]'s wire form

For impl-satisfaction lookup, a concrete type whose lookup key is `&[u8]` (str's canonical wire form) is renamed to `str` before the trait-engine query, so impls registered for `str` are found for values typed `&[u8]`.

Source: `src/compiler/mono_clone.cpp#L5119-L5123`

### `trait.impl.target-alias-unfold` — impl target unfolds transparent type aliases

When the impl target names a non-generic, non-lifetime-parameterized type alias, the alias is unfolded so the impl's methods register under the aliased concrete type's name (struct/datatype name, or its mangled concrete name when the alias carries type args; slice type string for a slice alias).

Source: `src/compiler/sema_collect.cpp#L2935-L2955`

### `trait.impl.target-fnptr-erased` — impl for fn-pointer covers all fn-ptrs of an arity

`impl<A,B,C> Trait for fn(A,B)->C` is permitted; because fn-pointers are type-erased to a uniform pointer at the Logos ABI, the impl covers every fn-pointer of the given arity and its methods are collected non-generically (one shared codegen, keyed by arity).

> **Divergence (vs Rust):** Logos additive behavior: fn-ptr impls are arity-keyed and non-generic due to fn-ptr type erasure (no per-signature monomorphization).

Source: `src/compiler/sema_collect.cpp#L2928-L2934` · `src/compiler/sema_collect.cpp#L2963-L2967`

### `trait.impl.target-ref` — impl for reference types

`impl Trait for &T` / `&mut T` is permitted; `&[T]`/`&mut [T]` canonicalize to the fat-pointer slice form and register under the same `$slice$<elem>` key as `impl Trait for [T]` (binding Self to the unsized-slice type); a generic ref-blanket `impl<T> Trait for &T`/`&mut T` keys under a fixed `$ref_$T`/`$mut_ref_$T` sentinel, restricted by coherence to one such impl per trait/ref-shape.

> **Divergence (vs Rust):** Note: receiver-shape mangling is a Logos dispatch-implementation detail; observable rule is which reference forms are valid impl targets and that &[T] ≡ [T] for dispatch.

Source: `src/compiler/sema_collect.cpp#L2818-L2863`

### `trait.impl.target-tuple` — impl for tuple types

`impl Trait for (A, B, ...)` is permitted; the empty tuple `()` is treated as the unit/void target. A variadic form `impl<A...> Trait for (A...)` covers tuples of any arity; otherwise the impl is keyed by arity (with element types when monomorphic).

Source: `src/compiler/sema_collect.cpp#L2887-L2927`

### `trait.impl.target-unsized` — impl for bare unsized self-types

`impl Trait for [T]`, `impl Trait for dyn Foo`, and `impl Trait for str` are permitted: the bare unsized slice / dyn-trait / str self-type is resolved in unsized-OK context, binding Self to UnsizedSlice / UnsizedDyn (and `str` to `UnsizedSlice<u8>`) so `&Self` canonicalizes to the corresponding fat pointer.

Source: `src/compiler/sema_collect.cpp#L2788-L2817` · `src/compiler/sema_collect.cpp#L3037-L3056`

### `trait.impl.trait-arg-resolution` — Impl trait type-args bind the trait's type parameters by position

For `impl SomeTrait<A1,A2,...> for U`, each resolved trait type argument binds the corresponding declared trait type-parameter name (by position) into the current type-param scope used while lowering the impl's methods (Self-substitution scope for the trait's own generics).

Source: `src/compiler/sema_decl.cpp#L2052-L2075`

### `trait.impl.trait-type-args-bind` — trait type args bind the trait's parameters

For `impl Trait<X> for U`, the trait's positional type arguments are resolved and bound to the trait's declared type parameters (e.g. `impl Into<i32> for C` binds the `Into` parameter to `i32`), making them available in method signatures. Lifetime arguments at trait position (`impl Trait<'a>`) are collected separately and not treated as type args.

> **Divergence (vs Rust):** Note: Logos does not track regions structurally for trait dispatch; trait-position lifetime args are skipped from type-arg resolution.

Source: `src/compiler/sema_collect.cpp#L3076-L3110`

### `trait.impl.tuple-target-key` — Tuple-target impls keyed by arity/element/variadic form, never literal type string

An impl targeting a tuple type is looked up under one of three key forms: `$tuple$N` (generic arity match), `$tuple$N$<t1>$<t2>...` (concrete per-element match), or `$tuple$variadic` (pack-target match) — never under the literal `(t1, t2)` type string. A concrete tuple satisfies the trait iff it matches the exact per-element form, or matches the per-arity/variadic form AND every element (for variadic: against the pack bound; for per-arity: against the unified impl-param bound) satisfies the corresponding bound.

Source: `src/compiler/mono_clone.cpp#L5098-L5108` · `src/compiler/mono_clone.cpp#L5130-L5199`

### `trait.impl.type-code-from-genos-spec` — type_code from a genos-specialization decl propagates through a matching trait impl

If a genos specialization `#[type_code=N] pub genos Trait<Args>;` exists and a type implements `Trait<Args>` with the same resolved trait type-args, the implementing struct inherits type_code N (looked up via canonical key `pkg::Trait<Args>`), provided the direct trait itself did not already supply a type_code.

Source: `src/compiler/sema_decl.cpp#L2088-L2113`

### `trait.impl.type-code-inherit` — Implementing a type-coded trait propagates its type_code to the target struct

If a trait carries a nonzero `#[type_code=N]` and a type implements it (`impl Trait for S`), the target struct S inherits type_code N (overriding S's hash-derived default), unless S is not yet a concrete registered struct (a generic instantiation, mangled name containing "$G"). In that case the code is recorded as an InstAnnot decl (mangled name, type_code, struct type, canonical name) for monomorphization to apply once the concrete struct is cloned, and both the canonical (`pkg::Base<Args>`) and mangled (`pkg::Base$G..`) names are registered in the explicit-type-code table for later `type_code_of` queries to hit. An explicit `#[type_code]` already present directly on S is not additionally combined with the trait-derived one on this path.

Source: `src/compiler/sema_decl.cpp#L1952-L2011`

### `trait.impl.unknown-trait-error` — impl of an undeclared trait is an error

`impl Trait for T` requires `Trait` to be a declared trait, except for the built-in marker traits `Copy` and `Drop`, which are always implementable by name without a visible trait declaration; any other unknown trait name is an error.

> **Divergence (vs Rust):** Copy and Drop are treated as compiler built-in marker traits resolvable by name alone (not requiring import/dependency-graph visibility).

Source: `src/compiler/sema_collect.cpp#L3064-L3072`

### `trait.impl.unsafe-trait-parity` — unsafe trait requires unsafe impl and vice versa

Implementing an unsafe trait requires `unsafe impl`; using `unsafe impl` for a safe non-auto trait is an error.

Source: `src/compiler/sema_collect.cpp#L3664-L3670`

### `trait.impl.variadic-pack-param` — Variadic trait type-param pack absorbs trailing impl params

If a trait declares a variadic type parameter `A...` used at a method parameter position, an impl may expose any number of concrete parameters from that position onward; each post-pack impl parameter type must equal the corresponding trait-instantiation type-arg (trait_type_args[k - pack_pos]), and the count of post-pack impl params must equal the number of pack instantiation args.

> **Divergence (vs Rust):** Fn-family variadic type packs are a Logos extension (no stable Rust equivalent).

Source: `src/compiler/sema_collect.cpp#L3408-L3492`

### `trait.impl.where-outlives-bounds` — where-clause outlives bounds augment impl params

Lifetime-outlives bounds (`'a: 'b`) and type-outlives bounds (`T: 'a`) written in an impl's where-clause are collected in addition to those in the `impl<...>` header and attached to the matching impl type parameter.

Source: `src/compiler/sema_collect.cpp#L2748-L2778`

## Impl methods (`impl-method`)

### `trait.impl-method.method-vs-impl-param-split` — Impl-level vs method-level type params partitioned on struct-template methods

When an impl method (explicit override or synthesized default) is attached to a struct/enum template, its TYPE_PARAMS (as returned by lower_fn) are filtered: any param whose name matches an impl-level param (impl_tps) is removed from TYPE_PARAMS and instead reappears verbatim in IMPL_TYPE_PARAMS; mono re-injects impl-level params at instantiation. Only method-local generics (e.g. `fn m<H>` under `impl<T> Trait for Foo<T>`) remain in TYPE_PARAMS.

Source: `src/compiler/sema_decl.cpp#L2260-L2284` · `src/compiler/sema_decl.cpp#L2522-L2537`

### `trait.impl-method.self-binding-by-target-shape` — Self binding for trait-default lowering follows impl-target shape

Before lowering a trait's default methods, Self is bound as follows: (1) blanket impl → fresh TypeVar named after target; (2) non-blanket, target is a struct/datatype, and the impl-target pattern is 'shaped' (has a type-arg that is not a bare TypeVar/ConstVar, e.g. `CopiedIter<I,&T>`) → Self = that exact impl_target_typeref (not a positionally-reconstructed generic instance); (3) non-blanket generic impl (impl_tps non-empty), unshaped → Self = a fresh generic struct/datatype instance with TypeVar args named after impl_tps; (4) non-generic, unshaped → Self = the plain concrete struct/datatype type; (5) target is a primitive type name → Self = that primitive type.

> **Uncertainty:** The 'shaped' check governs both struct (ssi2) and datatype (dsi2) branches identically; not independently exercised in this slice.

Source: `src/compiler/sema_decl.cpp#L2424-L2475`

### `trait.impl-method.unsized-self-seed` — seed_self continuation: concrete type-arg and blanket fallback

In the seed_self chain governing Self while lowering an impl's own explicit items: for a Struct/ZonedStruct/Enum target with no impl-level type params but a non-empty concrete type-argument list, seed_self := the target's resolved type. Else, if still unseeded and the impl is a blanket impl with impl-level params, seed_self := a fresh TypeVar named after target. When seed_self is set, current_type_params_["Self"] is temporarily rebound to it for the duration of lowering the impl's explicit items, then restored to its prior value (or erased if absent before).

> **Uncertainty:** The unsized/dyn/str seed_self cases preceding this branch live outside this slice (earlier lines of the same if/else chain); this statement covers only the concrete-type-arg and blanket-fallback branches visible here.

Source: `src/compiler/sema_decl.cpp#L2209-L2226` · `src/compiler/sema_decl.cpp#L2592-L2596`

### `trait.impl-method.visibility-inherits-trait` — Trait-impl methods force pub; inherent-impl methods keep explicit visibility

A method lowered under a trait-impl block (non-empty trait_name) is unconditionally flagged IS_PUB=true, overriding any source-level visibility, because a trait's methods are callable wherever the trait is reachable (Rust semantics) and the grammar disallows `pub` on trait methods. Inherent-impl methods (empty trait_name) keep their explicit `pub fn`/private marking.

Source: `src/compiler/sema_decl.cpp#L2246-L2252`

## Blanket implementations (`blanket`)

### `trait.blanket.auto-ref-receiver` — Auto-ref of receiver for &self/&mut self blanket method

When a dispatched blanket method's self parameter is `&self`/`&mut self` but the receiver is a value (not already a ref/ptr), the receiver's address is taken (mutably for `&mut self`).

Source: `src/compiler/sema_expr.cpp#L6385-L6401`

### `trait.blanket.method-dispatch-unique-or-error` — Blanket-impl method dispatch requires a unique applicable blanket

Dispatching a method call to a blanket impl (`impl<T: Bound> Trait for T { fn m ... }`) tries every registered blanket for `Trait` against the receiver's type name: a UNIQUE applicable blanket dispatches the call via generic-call finish (consuming receiver + args); ≥2 viable (overlapping) blankets is an ambiguity error; none applicable leaves receiver/args untouched so the caller falls through to the next dispatch path. Applies uniformly to struct and primitive receivers, so a value blanket (`impl<T> Trait for T`) also reaches primitive receivers.

Source: `src/compiler/sema_impl.hpp#L3911-L3923`

### `trait.blanket.overlap-ambiguity` — Two applicable distinct blanket impls are ambiguous

If two or more distinct blanket impls of the same method both apply to the receiver, the method call is an ambiguity error naming both impls.

Source: `src/compiler/sema_expr.cpp#L6336-L6351`

### `trait.blanket.recursive-impl-gating` — Blanket method dispatch gated by recursive bound satisfaction

A blanket impl provides a method for a receiver type only if the receiver satisfies the blanket's primary bound trait (checked recursively, including supertraits) and all extra bounds, AND every associated-type-equality clause on the primary and extra bounds is satisfied for the receiver.

Source: `src/compiler/sema_expr.cpp#L6296-L6335`

### `trait.blanket.recursive-satisfaction` — Trait satisfaction is recursive through chains of blanket impls

A concrete type satisfies trait `T` if it has a direct impl of `T`, OR transitively via any chain of blanket impls (`impl<P: Bound> T for P`) whose bounds it recursively satisfies. Cycle detection via a `seen` set prevents infinite recursion through cyclic blanket chains; each candidate blanket receives its OWN copy of `seen`, so a failed first candidate does not poison sub-checks for the next candidate. Associated-type-equality clauses (ADR 0008) are NOT validated by this recursive check — callers needing them must check separately.

Source: `src/compiler/sema_impl.hpp#L3603-L3620`

### `trait.blanket.type-param-inference-by-name` — Blanket type-params inferred by name from receiver, args, and return hint

For a dispatched blanket method, the blanket's target type-param (and `Self`) bind to the receiver's concrete type (unwrapped from ref/ptr); remaining type-params appearing only in argument or return position are inferred by unifying parameter types with argument types and, when present, the return type with the call-site return-type hint. Binding is by name, not position.

Source: `src/compiler/sema_expr.cpp#L6362-L6384`

### `trait.blanket.unbound-param-bail` — Blanket dispatch bails when a type-param is uninferable

If any of a dispatched blanket method's type-params cannot be inferred (e.g. the destination of `x.into()` with no expected-type annotation), the blanket impl does not dispatch; the normal 'cannot resolve' / type-annotation-needed diagnostic fires instead.

Source: `src/compiler/sema_expr.cpp#L6402-L6417`

## Blanket implementations — lowering (`blanket-impl`)

### `trait.blanket-impl.synthetic-target-name` — Blanket-impl lowering keys on synthetic $blanket$Trait$Bound$Target name

A blanket impl lowers its methods and default synthesis under lower_target (the synthetic `$blanket$Trait$Bound$target` form registered at collect time) with Self bound to a fresh TypeVar over the bare target name; associated-type lookups for a blanket impl likewise key on `$blanket$` + trait_name + `$` + impl_bound_trait + `$` + target rather than the plain target.

Source: `src/compiler/sema_decl.cpp#L2216-L2217` · `src/compiler/sema_decl.cpp#L2353-L2358` · `src/compiler/sema_decl.cpp#L2425-L2426` · `src/compiler/sema_decl.cpp#L2610-L2613`

## Coherence (`coherence`)

### `trait.coherence.no-overlapping-impl` — Coherence: no two impls of same trait+args for same target

Two non-generic, non-negative impls of the same trait (canonical name) with the same trait type-arguments for the same target type conflict and are an error. The coherence key includes the trait's spelled-out type-args (so `From<i8>` and `From<i16>` for one target do not collide) and uses the canonical scope-resolved trait name (so distinct same-name traits do not collide). Generic impls (with impl type/lifetime params) and negative impls are exempt.

Source: `src/compiler/sema_collect.cpp#L3729-L3773`

## Operator overloading (`overload`)

### `generic.overload.receiver-autoref-autoderef` — Method receiver autoref/autoderef in overload matching

For the receiver argument of a method overload, a by-value actual matching a `&self`/`&mut self` formal pointee (autoref) ranks as an exact match (score 2); a reference actual matching a by-value `self` formal pointee (autoderef) ranks one below (score 1). The receiver is unified through these autoref/autoderef shapes so unification does not see Ref-vs-Struct and bind nothing.

Source: `src/compiler/sema_expr.cpp#L3783-L3797` · `src/compiler/sema_expr.cpp#L3821-L3835`

### `generic.overload.score-select` — Generic overload selection by argument fit score

Among ≥2 generic overloads of a name, each arity-compatible candidate is scored by per-argument fit after substituting inferred bindings: exact type match = 2, compatible (incl. autoderef receiver / general type-compatibility) = 1; any incompatible argument disqualifies the candidate. A candidate whose fixed type-params cannot all be bound is rejected (unless its body always diverges, or a return-type hint is present). The highest-scoring candidate wins; ties are broken in favour of a candidate defined in the current package.

Source: `src/compiler/sema_expr.cpp#L3759-L3845`

### `trait.overload.candidate-visibility-filter` — Call candidates filtered by package visibility; explicit pkg:: overrides imports

Function-name resolution collects all overloads (concrete and generic) then filters to those visible from the call site: own package, wildcard-imported packages, or empty-package (extern/prelude). A `use pkg from <module>` import deliberately excludes other modules' same-pkg fns. An explicit `pkg::fn(...)` qualifier restricts to that package only with no empty-fallback. Otherwise, if filtering leaves nothing and no deliberate `from`-exclusion occurred, all candidates are returned (synthetic-phase robustness).

Source: `src/compiler/sema.cpp#L1638-L1700`

### `trait.overload.generic-arity-and-package` — Generic function selection: package-qualifier filter, arity match, own-package preference

Generic-function lookup among an overload set keeps only candidates passing the package-qualifier filter; with an arg count, a candidate matches if its arity equals n (or n>=arity when vararg). An arity match in the current package wins immediately; else the first matching-arity candidate; else a wrong-arity fallback.

Source: `src/compiler/sema.cpp#L1578-L1608`

### `trait.overload.generic-select-by-arg-shape` — Generic overload selection scores candidates by substituted-parameter match against argument shape

When a generic base name has ≥2 overloads distinguished by parameter shape, overload selection unifies each candidate's params against the actual arg types, then scores the SUBSTITUTED params: +2 per exact-matching param, +1 per coercion-compatible param, and any incompatible param disqualifies the candidate entirely; the highest-scoring candidate is selected. With <2 overloads, or when no candidate matches, resolution falls back to first-arity-match (unchanged behavior).

Source: `src/compiler/sema_impl.hpp#L3799-L3811`

## Binary-operator traits (`binop`)

### `trait.binop.enum-eq-impl` — == / != on same-named enums requires structural Eq impl for payload enums

== / != between two enums of the same name route to the enum's eq/ne impl (a 2-param candidate keyed `EnumName__eq`, concrete or generic) when one exists, auto-borrowing operands; a payload-less (C-like) enum without an impl falls through to discriminant comparison, while a payload-carrying enum with no Eq/PartialEq impl is rejected.

Source: `src/compiler/sema_expr.cpp#L2114-L2192`

### `trait.binop.operator-method-autoref` — Operator-method operands auto-borrowed to match by-ref formals

When the resolved operator-overload method takes an operand by reference (&self / &other), the corresponding value operand is auto-borrowed (addr_of_temp) to match; by-value method formals receive the operand by value unchanged.

Source: `src/compiler/sema_expr.cpp#L1956-L1988`

### `trait.binop.partial-ord-derive` — Relational ops derive from partial_cmp when direct method absent

For a struct LHS with relational op {`<,<=,>,>=`}, if the direct lt/le/gt/ge method is not implemented but partial_cmp is, the comparison derives as a.partial_cmp(&b) followed by is_lt/is_le/is_gt/is_ge; when partial_cmp returns `Option<Ordering>` it routes through `cmp_opt_is_<op>` (None => false), and when it returns Ordering directly it calls `Ordering::is_<op>`.

> **Divergence (vs Rust):** Mirrors Rust's default PartialOrd lt/le/gt/ge bodies.

Source: `src/compiler/sema_expr.cpp#L1990-L2055`

### `trait.binop.struct-operator-overload` — Operator overloading on struct LHS desugars to trait method

When the left operand is a struct, the operator desugars to the corresponding trait method: + Add::add, - Sub::sub, * Mul::mul, / Div::div, % Rem::rem, & BitAnd::bitand, | BitOr::bitor, ^ BitXor::bitxor, << Shl::shl, >> Shr::shr, == Eq::eq, != Eq::ne, < Ord::lt, <= Ord::le, > Ord::gt, >= Ord::ge.

Source: `src/compiler/sema_expr.cpp#L1930-L1958`

### `trait.binop.tuple-eq-impl` — Tuple == / != routes to Eq impl only for non-primitive tuples

== / != between two tuples of equal arity routes to the tuple's Eq eq/ne impl (keyed concrete `$tuple$N$...`, then arity `$tuple$N`, then variadic `$tuple$variadic`) ONLY when at least one field is non-primitive; an all-primitive tuple falls through to per-field value comparison and never requires the Eq trait. Operands are auto-borrowed to &Tuple.

> **Divergence (vs Rust):** Primitive-tuple fast path avoids requiring f64:Eq (f64 is PartialEq-only, Rust parity).

Source: `src/compiler/sema_expr.cpp#L1812-L1928`

### `trait.binop.typevar-eq-bound` — == / != on bounded type variable dispatches to Eq method

== / != where the left operand is a type variable whose bounds (transitively, through supertraits) provide an `eq` method desugar to an auto-ref'd eq/ne method call dispatched after monomorphization; if more than one trait in scope provides `eq`, the call is tagged with trait Eq for disambiguation. Absent an eq-providing bound, falls through to the generic operator check.

Source: `src/compiler/sema_expr.cpp#L2060-L2112`

## Formatting traits (`fmt`)

### `trait.fmt.trait-dispatcher-fn` — Formatting-trait free-fn dispatchers

Format macro lowering dispatches via free-fn wrappers bound at sema-time through a generic trait bound (rather than a `.method()` dot-call): Display=`fmt_display`, Debug=`fmt_debug`, and hex/oct/bin/exp variants named identically to their method names.

Source: `src/compiler/sema_fmt.cpp#L25-L41`

### `trait.fmt.trait-method-names` — Formatting-trait method names

Each formatting trait maps to a method name used by macro lowering: Display=`fmt`, Debug=`dbg`, LowerHex=`fmt_lower_hex`, UpperHex=`fmt_upper_hex`, Octal=`fmt_octal`, Binary=`fmt_binary`, LowerExp=`fmt_lower_exp`, UpperExp=`fmt_upper_exp`.

Source: `src/compiler/sema_fmt.cpp#L11-L23`

## Indexing traits (`index`)

### `trait.index.mut-place-projection` — `&f[i]`/`&mut f[i]` over a user Index/IndexMut type dispatches directly to index()/index_mut()

Taking a place reference to an element of a user-defined Index/IndexMut container (`&f[i]` or `&mut f[i]`) dispatches directly to that type's `index()`/`index_mut()` method and yields the returned reference AS the place — with no additional deref and no temporary. The mutable form requires the type implement `IndexMut`; this path applies only over user Index/IndexMut types, otherwise the caller falls through to the generic address-of path.

Source: `src/compiler/sema_impl.hpp#L3947-L3953`

## Deref (`deref`)

### `trait.deref.multi-impl-target-match` — Deref impl selected by strict self-type match among multiple impls

When a type carries several Deref impls distinguished by self type-args (e.g. `Pin<&T>/Pin<&mut` `T>/Pin<Box<T>>`), the impl whose target pattern strictly unifies-substitutes-and-equals the receiver type is selected; a non-matching impl is used only as a loose fallback.

Source: `src/compiler/sema_expr.cpp#L123-L158`

## Closure traits (`closure`)

### `trait.closure.fn-family-auto-impl` — Closure types automatically satisfy Fn/FnMut/FnOnce

Every closure type (canonical type name beginning with `|`, i.e. `|T1,...| -> R`) is treated by the trait engine as satisfying Fn, FnMut, and FnOnce via a shape-auto rule, without requiring any explicit `impl Fn for <closure>` to be registered.

Source: `src/compiler/mono_clone.cpp#L5016-L5027`

## Trait resolution (`resolve`)

### `trait.resolve.auto-impl-for-all` — Auto/marker trait holds for all types

An auto-trait T (e.g. marker traits Copy/Send/Sync) holds unconditionally for every type, subject to negative carve-outs.

Source: `src/compiler/trait_engine.hpp#L49-L55` · `src/compiler/trait_engine.hpp#L83`

### `trait.resolve.blanket-and-bounds` — Blanket impl with conjunctive bounds

A blanket impl `impl<S> T for S where S: B1 + ... + Bn` makes T hold for any type S iff S satisfies ALL bounds B1..Bn (AND-conjunction). An empty bound set degenerates to an unconditional impl-for-all.

Source: `src/compiler/trait_engine.hpp#L39-L47` · `src/compiler/trait_engine.hpp#L78-L82`

### `trait.resolve.blanket-conjunction` — Blanket impl bounds are an AND-conjunction

A blanket impl blanket(T←{Tb1..Tbn}) derives satisfies(T,X) only if every bound trait Tbi satisfies satisfies(Tbi,X) for the same type X. An empty bound set {} is an unconditional impl-for-all-types of T.

Source: `src/compiler/trait_engine.cpp#L104-L121`

### `trait.resolve.blanket-first-match` — First fully-satisfied blanket wins

When multiple blanket impls target the same trait T, the first one (in declaration/registration order) whose bounds are all satisfied is selected; remaining candidate blankets are not considered.

> **Uncertainty:** Coherence is deferred; overlapping blankets are resolved by order rather than rejected here.

Source: `src/compiler/trait_engine.cpp#L108-L121`

### `trait.resolve.cycle-terminates-no-impl` — Cyclic blanket bounds resolve to no-impl on the cyclic path

If resolving satisfies(T,X) recursively re-enters the same query (T,X) through a blanket-bound chain, that recursive path yields no impl rather than diverging, allowing outer rules to try alternatives; resolution always terminates.

Source: `src/compiler/trait_engine.cpp#L89-L96` · `src/compiler/trait_engine.cpp#L14-L17`

### `trait.resolve.derivation-modes` — Trait satisfaction derivation modes

satisfies(T, X) holds iff at least one derivation succeeds: (D) a direct impl fact impls(T,X); (B) a blanket impl blanket(T←{Tb...}) whose every bound Tb satisfies satisfies(Tb,X); (A) an auto impl auto(T) with no negative carve-out; or (S) a shape-auto impl shape_auto(T,S) whose predicate S(X) matches with no negative carve-out.

Source: `src/compiler/trait_engine.cpp#L98-L148` · `src/compiler/trait_engine.cpp#L151-L153`

### `trait.resolve.derived-impl-distinct-identity` — Derived auto/shape impls get a fresh stable impl identity per (trait,type)

Auto and shape-auto derivations produce a fresh impl identity the first time a given (T,X) pair is queried; that identity is memoized so subsequent queries of the same pair compare equal.

Source: `src/compiler/trait_engine.cpp#L125-L145`

### `trait.resolve.direct-impl-fact` — Direct impl fact

A declaration `impl T for X` makes X satisfy trait T directly (a direct impl fact keyed by (T, X)).

Source: `src/compiler/trait_engine.hpp#L31-L37` · `src/compiler/trait_engine.hpp#L76`

### `trait.resolve.direct-impl-idempotent` — Duplicate direct impls collapse to one impl identity

Registering a direct impl for an already-implemented (T,X) pair does not create a new impl; the original impl identity is returned, so impls(T,X) names a single impl.

Source: `src/compiler/trait_engine.cpp#L28-L37`

### `trait.resolve.fact-monotonic-invalidation` — Adding a fact may flip previous negative results

Adding any impl fact (direct, blanket, auto, shape-auto, or negative) invalidates all previously cached resolution results, because a prior 'no impl' may become satisfiable (or vice versa).

Source: `src/compiler/trait_engine.cpp#L34` · `src/compiler/trait_engine.cpp#L48` · `src/compiler/trait_engine.cpp#L55` · `src/compiler/trait_engine.cpp#L62` · `src/compiler/trait_engine.cpp#L68`

### `trait.resolve.impl-id-dispatch-selection` — Resolution selects a concrete impl for dispatch

resolve(T, X) yields the impl identity through which a call on X is dispatched, or NO_IMPL when no direct or derived impl exists; satisfaction and dispatch-selection are the same predicate (resolve = NO_IMPL iff not satisfies).

Source: `src/compiler/trait_engine.hpp#L101-L103` · `src/compiler/trait_engine.hpp#L28-L29`

### `trait.resolve.memoization-stable-result` — Resolution result per (trait,type) is memoized and stable

satisfies(T,X) is a deterministic function of the current fact set: results (including negative/no-impl outcomes) are memoized per (T,X) pair and re-queries return the same answer until the fact set changes.

Source: `src/compiler/trait_engine.cpp#L85-L87` · `src/compiler/trait_engine.cpp#L99-L148`

### `trait.resolve.negative-overrides` — Negative carve-out beats auto/shape facts

A negative fact `X does NOT implement T` overrides (beats) any auto-impl or shape-auto-impl fact for (T, X); satisfies(T, X) is then false even if an auto/shape rule would otherwise derive it.

Source: `src/compiler/trait_engine.hpp#L87-L90`

### `trait.resolve.negative-priority` — Negative impls beat all derivations

A negative fact !impls(T,X) makes satisfies(T,X) false unconditionally; it is checked before and overrides direct, blanket, auto and shape-auto derivations.

Source: `src/compiler/trait_engine.cpp#L82-L83` · `src/compiler/trait_engine.cpp#L66-L70`

### `trait.resolve.priority-order` — Fixed derivation priority order

Resolution tries derivations in strict order: negative carve-out (reject), then direct, then blanket, then auto, then shape-auto. The first kind that succeeds determines the result; later kinds are not consulted.

> **Uncertainty:** Phase-1 simple priority ordering; coherence/overlap checks are stated to live elsewhere.

Source: `src/compiler/trait_engine.cpp#L98-L148`

### `trait.resolve.recursive-bound-cycle-terminates` — Circular bound resolution terminates as unsatisfied

Recursive bound resolution is cycle-guarded: a query (T, X) that re-asks itself through a circular blanket bound resolves that inner query as not-satisfied (NO_IMPL) rather than diverging, and the outer rule then decides.

Source: `src/compiler/trait_engine.hpp#L145-L148`

### `trait.resolve.satisfies-fixpoint` — Satisfaction by fixpoint closure

satisfies(T, X) is decided by the least fixpoint over {direct, blanket, auto, shape-auto} facts minus negative facts: X satisfies T iff a direct fact (T,X) exists, or some applicable blanket/auto/shape rule derives it (transitively through bound resolution).

Source: `src/compiler/trait_engine.hpp#L92-L99` · `src/compiler/trait_engine.hpp#L153-L156`

### `trait.resolve.scope-aware-supertrait-name` — Supertrait/trait bare-name resolution is scope-aware, not first-match

Resolving a trait name (root or supertrait) during vtable-layout construction prefers a same-named trait declared in the current package over a same-named prelude/imported trait, so a user trait `Sub: Add` shadowing a built-in `Add` walks the user trait's own methods rather than the incumbent's. Non-colliding names resolve to the single bare-name trait as usual.

> **Uncertainty:** Exact shadowing precedence rules (package-qualified vs bare) are implemented in find_trait_iter_scoped, outside this slice; only the intent/consumer here is visible.

Source: `src/compiler/sema_collect.cpp#L5122-L5127`

### `trait.resolve.shape-auto-predicate-on-typename` — Shape-auto impls match by a predicate over the type name

A shape-auto impl applies to type X iff its shape predicate evaluates true on X's type name; a shape-auto impl with no predicate never matches.

Source: `src/compiler/trait_engine.cpp#L136-L145`

### `trait.resolve.shape-conditioned-auto` — Shape-conditioned auto-impl

A shape-conditioned auto-impl makes trait T hold for every type whose structural shape matches a predicate S (e.g. `Fn`-family for every closure type), subject to negative carve-outs.

Source: `src/compiler/trait_engine.hpp#L57-L69` · `src/compiler/trait_engine.hpp#L84-L85`

## Function lookup (`lookup`)

### `trait.lookup.exact-signature-fnitem-coerce` — Exact-signature lookup matches params by equality, with FnItem to FnPtr coercion

Exact-signature function lookup requires equal vararg-ness and param arity, and each param either types_equal or (arg-side FnItem vs candidate FnPtr) compatible. This mirrors the FnItem to FnPtr coercion on the otherwise exact-match path.

Source: `src/compiler/sema.cpp#L1610-L1636`

## Impl satisfaction (`satisfy`)

### `trait.satisfy.blanket-recursive` — Recursive impl satisfaction with blanket chains

A type satisfies a trait if a direct impl exists (by primary or alternate mangled key), or a blanket impl `impl<T: B> Trait for T` applies whose bound trait B (and all extra bounds) are themselves recursively satisfied by the type. A cycle-guard set prevents infinite recursion; each blanket candidate uses a per-attempt copy of the seen-set so a failed sibling does not poison later candidates. An unbounded blanket (`impl<T> Trait for T`) trivially satisfies any type.

Source: `src/compiler/sema_collect.cpp#L764-L806`

### `trait.satisfy.ref-self-mangling` — Reference-Self impls keyed by $ref_/$mut_ref_ mangling

An `impl Trait for &T` / `&mut T` registers under a mangled key (`$ref_`/`$mut_ref_` prefix); a query whether `&T` impls Trait matches both the full-string pointee form (`$ref_&i32`) and the bare-name pointee form (`$ref_Foo`).

Source: `src/compiler/sema_collect.cpp#L776-L788`

## Trait bounds — checking (`bound`)

### `generic.bound.assoc-eq` — Associated-type equality constraints in bounds

A trait bound may bind associated types by equality, `Trait<Assoc = Ty>`; each `Assoc = Ty` is recorded as an associated-type equality on the bound.

```logos
fn f<I: Iterator<Item = i32>>(i: I) {}
```

Source: `src/compiler/sema.cpp#L4025-L4033`

### `generic.bound.fn-family-paren-form` — Fn-family parenthesized bound syntax

The traits Fn, FnMut, FnOnce admit a parenthesized bound form `Fn(P1, ..., Pn) -> R`; the parenthesized list supplies the argument types and `-> R` the return type (both optional), distinct from the `<...>` type-argument list.

```logos
fn call<F: FnOnce(i32, i32) -> bool>(f: F) {}
```

Source: `src/compiler/sema.cpp#L3969-L3992`

### `generic.bound.hrtb-binder` — Higher-ranked trait bound binders

A trait bound may carry a `for<'a, 'b, ...>` higher-ranked lifetime binder; the bound lifetime names are recorded on the bound.

```logos
fn f<F: for<'a> Fn(&'a i32)>(f: F) {}
```

Source: `src/compiler/sema.cpp#L3994-L4017`

### `generic.bound.lifetime-arg-not-structural` — Lifetime args in trait bounds are recorded but not dispatched on

A lifetime argument at a trait bound's type-argument position (e.g. `Foo<'a>`) is captured for record only; regions are not tracked structurally for bound dispatch.

> **Divergence (vs Rust):** Logos does not track regions structurally for bound dispatch; lifetime bound-args carry no dispatch significance.

Source: `src/compiler/sema.cpp#L4034-L4041`

### `generic.bound.lifetime-outlives-clause` — Lifetime outlives bounds in generic param list

A lifetime parameter may carry outlives bounds `'long: 'a + 'b + 'c`, which desugar to the set of pairwise constraints {('long,'a), ('long,'b), ('long,'c)} meaning 'long outlives each listed shorter lifetime.

> **Uncertainty:** Encoding read here; enforcement of the outlives relation is elsewhere.

Source: `src/compiler/sema.cpp#L3324-L3351`

### `generic.bound.relaxed-not-propagated` — Relaxed markers are consumed, never positive bounds

A relaxed `?Trait` marker is removed from a type parameter's bound set during finalization; it is never carried forward as a positive bound to monomorphization or bound-checking.

Source: `src/compiler/sema.cpp#L3937-L3954`

### `generic.bound.relaxed-only-sized` — Only ?Sized is a permitted relaxed bound

A relaxed bound `?Trait` on a type parameter is permitted only when Trait = Sized. `?Sized` clears the parameter's implicit Sized bound; any other `?Trait` is a hard error "relaxed bound '?T' is not permitted (only `?Sized` is supported)".

```logos
fn f<T: ?Sized>(x: &T) {}

fn g<T: ?Clone>() {}  // error
```

Source: `src/compiler/sema.cpp#L3944-L3954` · `src/compiler/sema.cpp#L3957-L3968`

### `generic.bound.type-outlives` — Type-outlives bounds

A type parameter may carry type-outlives bounds `T: 'a (+ 'b)*`; the outlived lifetime names are recorded on the parameter.

```logos
fn f<T: 'static>(x: T) {}
```

Source: `src/compiler/sema.cpp#L4098-L4102`

### `generic.bound.well-formed` — trait bounds are validated at the declaration site

Trait bounds written on generic declarations are validated where written: each bound must name a known trait and supply the correct number of trait arguments.

> **Uncertainty:** check_trait_bounds_well_formed() body is defined outside this unit; only its invocation site is evidenced here.

Source: `src/compiler/sema_collect.cpp#L569-L571`

### `trait.bound.arity` — Trait-bound type-argument arity

A trait bound `T: Tr<A...>` must supply type arguments matching Tr's type-parameter arity: if Tr's last param is variadic, |A| >= |params|-1; else |A| == |params|. A trait with no type params accepts no args. Mismatch is an error at the definition site.

Source: `src/compiler/sema_impl.hpp#L1637-L1651`

### `trait.bound.auto-trait-structural` — Auto-trait bounds checked structurally, not via impl lookup

When a method's type-param bound names an auto trait (trait_def.is_auto()), satisfaction is decided by structural auto-trait analysis of the concrete type (is_auto_satisfied) instead of by trait-engine impl lookup.

Source: `src/compiler/mono_clone.cpp#L5349-L5357`

### `trait.bound.fn-family-intrinsic` — Fn-family bound satisfied by callable shapes intrinsically

A parenthesized Fn/FnMut/FnOnce bound is satisfied intrinsically (no registered impl consulted) by any fn-value kind, Closure, TypeVar (deferred — resolved by an outer mono pass), or Struct/ZonedStruct (struct-with-Fn-impl bridge); any other concrete kind fails the bound.

Source: `src/compiler/mono_clone.cpp#L5235-L5246` · `src/compiler/mono_clone.cpp#L5339-L5348`

### `trait.bound.generic-arg-recursion` — Generic instantiation bound satisfaction recurses into type-args

Satisfying `Concrete<A..>: Trait` for a Struct/ZonedStruct/Enum/Tuple concrete requires: (1) a matching impl exists by the concrete's bare lookup key; (2) if the concrete has no type-args, step 1 alone suffices; (3) otherwise, for each candidate impl whose target_type matches the key, unify the impl's target pattern against the concrete to obtain a TypeVar→argument substitution, then for every impl_type_param bound, recursively check the bound holds for the substituted argument; the concrete satisfies Trait iff some candidate's bounds all hold under the unification.

Source: `src/compiler/mono_clone.cpp#L5200-L5253` · `src/compiler/mono_clone.cpp#L5130-L5199`

### `trait.bound.unknown-trait` — Trait bound naming an unknown trait is rejected

Every recorded type-parameter trait bound `T: Tr` must name a known trait; an unknown `Tr` is an error at the definition site. Exceptions: the Fn-family bounds (`Fn`/`FnMut`/`FnOnce`) and `Sized` are compiler-builtin and require no user-space trait declaration.

```logos
fn f<T: Nonexistent>() {} // error: unknown trait
```

Source: `src/compiler/sema_impl.hpp#L1612-L1636`

### `trait.bound.where-clause-gate` — Method where-bounds gate method instantiation

A method with a where-bound `Subject: Trait` is only instantiated for a concrete substitution if the substituted Subject satisfies Trait; if the substituted Subject still contains a TypeVar the check is deferred (treated as passing) to an outer mono pass rather than failing; an unsatisfied where-bound suppresses synthesis of that method for this instantiation.

Source: `src/compiler/mono_clone.cpp#L5258-L5275`

## Trait bounds — propagation (`bounds`)

### `generic.bounds.defer-typevar-bearing` — TypeVar-bearing subjects defer bound checking to mono

A concrete-arg type-expression that mentions a TypeVar anywhere (`&T`, `[T;0]`, `&[T]`, `EnumPair<T>`, `(T,U)`, closure param/ret) has undecidable trait satisfaction here and is deferred to monomorphization, where the substituted form is re-checked. AssocType and CfgSlotType kinds are likewise deferred.

Source: `src/compiler/sema_collect.cpp#L838-L866`

### `generic.bounds.no-empty-params` — Bound checking is a no-op for non-generic targets

When a target has no type parameters, bound checking is skipped entirely.

Source: `src/compiler/sema_collect.cpp#L808-L811`

### `generic.bounds.relaxed-only-sized` — Only ?Sized is a permitted relaxed bound

A relaxed bound (`?Trait`) is permitted only for `Sized`; any other relaxed bound is an error. Seeing `?Sized` clears the parameter's implicit `Sized` requirement, and the relaxed entry is removed from the bounds list so downstream code sees only positive bounds.

Source: `src/compiler/sema_impl.hpp#L3272-L3280` · `src/compiler/sema_impl.hpp#L3334-L3340`

### `generic.bounds.relaxed-sized-substitution-check` — ?Sized param substituted where Sized required errors

When a type parameter carrying `?Sized` is passed as a type-argument to a callee whose corresponding parameter requires `Sized`, the same unsized-substitution diagnostic is emitted as for an explicit unsized substitution.

Source: `src/compiler/sema_impl.hpp#L3020-L3025`

### `generic.bounds.substitute-call-args` — Call's type-args substituted into parametrized bounds

Before checking a parametrized bound `I: Iterator<T>`, the call's mapping of type-params to concrete args is substituted into the bound's type-args, so the bound is checked against the concrete value of T (e.g. turbofish T=i32) rather than the bare TypeVar.

Source: `src/compiler/sema_collect.cpp#L815-L825` · `src/compiler/sema_collect.cpp#L947-L955`

### `generic.bounds.variadic-tail-param` — Variadic tail parameter absorbs extra type args

If the last type parameter is variadic, type args beyond the non-variadic count are all checked against that final (variadic) parameter; otherwise excess args are ignored once parameters are exhausted.

> **Divergence (vs Rust):** A6

Source: `src/compiler/sema_collect.cpp#L812-L833`

### `trait.bounds.dyn-object-self-and-super` — Trait object satisfies its own trait and supertraits

A `dyn Trait` (TraitObject / UnsizedDyn) value satisfies a `T: Bound` bound iff the trait object's trait equals Bound or transitively reaches it through supertraits.

Source: `src/compiler/sema_collect.cpp#L1273-L1296`

### `trait.bounds.fn-family-by-callable` — Fn-family bounds satisfied by callable types

An `F: Fn*(args)->R` bound is satisfied by any closure or fn-pointer type; by `&F`/`&mut F` whose pointee is a closure, fn-ptr, or Fn-bounded TypeVar (autoderef-invoke); and by a concrete fn-pointer matching an arity-keyed `$fnptr$N` impl. Arity/arg/ret compatibility is enforced at the call site.

Source: `src/compiler/sema_collect.cpp#L1245-L1272`

### `trait.bounds.generic-struct-base-key` — Generic-struct impl keyed by base struct name

A generic impl `impl<T: X> Trait for GenericStruct<T>` registers under the base name; a bound on a concrete instantiation of that struct is satisfied if a generic impl for its base struct exists (with type-args matching), the impl's own param bounds being validated recursively at monomorphization.

Source: `src/compiler/sema_collect.cpp#L1181-L1191`

### `trait.bounds.parametrized-type-args-match` — Parametrized bound requires a type-arg-matching impl

For a bound `T: Trait<Args>`, a name-keyed impl hit proves only that SOME Trait impl exists; satisfaction additionally requires that some impl for this Self (enumerated via multi-valued impls registry) has matching trait type-args after substituting the impl's params from the concrete Self. An empty bound type-arg list imposes no constraint. Enumeration covers the concrete, unwrapped, and bare struct/enum names; abstract (TypeVar-bearing) sides defer.

Source: `src/compiler/sema_collect.cpp#L936-L1007`

### `trait.bounds.partialeq-via-eq` — PartialEq/PartialOrd satisfied by Eq/Ord impls

A `T: PartialEq` bound is satisfied by an existing Eq impl, and `T: PartialOrd` by an Ord impl (alias resolution over concrete and unwrapped names).

> **Divergence (vs Rust):** Logos Eq/Ord carry the methods Rust puts on PartialEq/PartialOrd; full split pending.

Source: `src/compiler/sema_collect.cpp#L1110-L1131`

### `trait.bounds.ref-subject-impl-key` — where &T: Trait satisfied only by reference-Self impl

A `where &T: Trait` bound (ref-subject) is satisfied only by an `impl Trait for &Concrete` / `&mut Concrete` (keyed `$ref_<C>` / `$mut_ref_<C>`), NOT by `impl Trait for Concrete`; absence is an error naming parameter `&<name>`.

Source: `src/compiler/sema_collect.cpp#L899-L911`

### `trait.bounds.region-mismatch-error` — Found impl with incompatible regions is an error

When a direct/alias impl is found and its type-args match, satisfaction holds only if the region check passes; on region failure the bound is rejected with a diagnostic citing incompatible trait-arg lifetimes (and HRTB `for<...>` binders if present).

See also: `region.bounds.universal-lifetime-position`, `region.bounds.impl-tie-injectivity`, `region.bounds.hrtb-outlives-unsat`.

Source: `src/compiler/sema_collect.cpp#L1102-L1154`

### `trait.bounds.tuple-impl-satisfaction` — Tuple impls keyed by arity, with per-element recursion

A tuple satisfies a bound if a variadic tuple impl (`$tuple$variadic`) exists (any arity), or an arity-specific impl (`$tuple$N`) exists AND every non-TypeVar element itself satisfies the trait (element checked via direct impl, nested tuple-arity, or auto-trait short-circuit). TypeVar elements defer to mono.

Source: `src/compiler/sema_collect.cpp#L1192-L1244`

### `trait.bounds.unsatisfied-error` — Unsatisfiable bound emits a diagnostic

When no satisfaction path applies (direct/alias impl, blanket, generic-struct, tuple, Fn-family, dyn, or reference-Self mangled key), the bound is rejected: `'<target>': type '<C>' does not implement trait '<Trait>' required by parameter '<name>'`.

Source: `src/compiler/sema_collect.cpp#L1297-L1318`

## Where clauses (`where`)

### `generic.where.concrete-subject-obligation` — where-clause with concrete subject is an obligation, not a param

A `where <ConcreteType>: Trait` clause (subject names a known type, e.g. `where i32: Show`) is a trivially-checked obligation and does not introduce a new type parameter; only a genuinely-undeclared type-param name in a where clause is added as a parameter.

Source: `src/compiler/sema.cpp#L4260-L4279`

### `generic.where.merged-with-inline` — where-clause bounds merge into parameter bounds

Bounds from a `where T: Trait, U: Trait2` clause are merged onto the corresponding type parameters; an inline `<T, F: Bound>` and the equivalent `where`-clause form are semantically identical, and sibling type-params are in scope when resolving where-clause bound arguments.

Source: `src/compiler/sema.cpp#L4195-L4295`

### `generic.where.projection-subject-skipped` — where-clause projection subject is parsed but not enforced

A `where C::Item<T>: Bound` clause whose subject is an associated-type projection is accepted but not yet enforced (parse-and-skip).

> **Uncertainty:** Statement reflects current parse-and-skip behavior; full projection-bound checking is noted as a separate unimplemented feature.

Source: `src/compiler/sema.cpp#L4218-L4227`

### `generic.where.ref-subject` — where-clause with reference subject

A `where &T: Trait` / `where &mut T: Trait` clause records its bounds on the underlying type-param T, flagged as applying only to a matching (shared/mut) reference receiver.

Source: `src/compiler/sema.cpp#L4210-L4259`

## Dispatch (`dispatch`)

### `trait.dispatch.ambiguous-method-error` — Method matching multiple bound traits is ambiguous

When a method name `m` is provided by two distinct traits reachable from a type parameter's bounds (e.g. `trait Foo: A + B` where both A and B define `m`), the call is an error: method `m` is ambiguous (matches both traits). All supertrait siblings are searched so the ambiguity is detected rather than silently resolving to one.

Source: `src/compiler/sema_expr.cpp#L7411-L7421`

### `trait.dispatch.assoc-type-nondefault-gate` — Associated-type receiver intercepted only for a non-default method

A method call whose receiver is an associated-type projection `G::R` is dispatched via the assoc-type's declared bounds (`type R: HasId`) only when those bounds supply a NON-default method of that name; if the only provider is a default method (e.g. via a blanket impl), dispatch defers to the ordinary path.

Source: `src/compiler/sema_expr.cpp#L7341-L7366` · `src/compiler/sema_expr.cpp#L7444-L7458`

### `trait.dispatch.autoderef-applied-for-byvalue-self` — Once a by-value-self deref-fallback candidate is chosen, the receiver is materialized via an explicit deref

When the selected method candidate is the deref-fallback (by-value `self`, receiver was `&T`/`&mut T`/`*T`), the receiver expression is replaced with an explicit deref of the pointee before building the call. Soundness of the resulting move/copy is enforced by borrow-checking downstream, not by this dispatch step.

Source: `src/compiler/sema_expr.cpp#L8620-L8627`

### `trait.dispatch.autoderef-to-pointee-method` — `&T`/`&mut T` receiver auto-derefs to T's method when no ref-targeted impl is found

For a primitive-or-ref receiver of type `&T`/`&mut T`, if no `T`-value, `&T`-impl, or generic `&T`-blanket impl matched, resolution derefs to the pointee type `T` and retries lookup under `<T>__<method>` with param[0] = `T`; if that also fails, it retries with param[0] left as the original ref type (covers a `self: &Self` method reached only via this path). The receiver expression is rebuilt via an explicit deref only in the pointee-param[0] case.

Source: `src/compiler/sema_expr.cpp#L8244-L8277`

### `trait.dispatch.blanket-bound-transitive` — Blanket extension trait holds transitively on a bounded type param

If `impl<U: B> Ext for U {}` exists and the receiver type parameter `T` satisfies bound `B` (directly or via a supertrait of one of its bounds) and all the blanket impl's extra bounds, then `T: Ext` holds and `Ext`'s methods (including defaults) are searched for the call.

Source: `src/compiler/sema_expr.cpp#L7460-L7485`

### `trait.dispatch.byvalue-self-lowest-priority` — An auto-deref'd by-value-`self` candidate is chosen only when no non-deref candidate matches

A candidate reachable only by auto-dereffing the receiver to satisfy a by-value `self` parameter is recorded but not immediately selected; the first non-deref-matching candidate in scan order still wins. The deref-only candidate is used as a fallback only if the whole candidate scan finds no non-deref match. This mirrors Rust's autoderef order: exact/autoref matches at the current deref level are preferred over stepping to a further deref level.

Source: `src/compiler/sema_expr.cpp#L8531-L8613`

### `trait.dispatch.candidate-signature-match` — Struct-method candidate selection matches receiver/args against each candidate's formal signature with one autoref/autoderef step

Among same-name non-generic candidates with matching arity, a candidate matches if: (a) receiver type equals formal param[0] exactly, OR (b) formal param[0] is `&T`/`&mut T`/`*T` and actual receiver's type equals its pointee (autoref/auto-addr the receiver to match), OR (c) both are raw pointers with equal pointee (const/mut pointer receivers are mutually compatible), OR (d) actual receiver is `&T`/`&mut T`/`*T` and formal param[0] is the bare pointee type (auto-deref candidate, by-value self) — otherwise the general `types_compatible` check applies; AND every remaining argument is `arg_compatible_for_dispatch` against its formal (with struct type-param substitution applied to formals when the receiver is a generic struct instantiation).

Source: `src/compiler/sema_expr.cpp#L8538-L8598`

### `trait.dispatch.dstref-unsafe` — &DstStruct method dispatch requires unsafe unless self-describing

A method call on a `DstRef` receiver requires `unsafe` context unless the referenced struct is declared `#[self_describing]` (its tail length is recoverable in-band, making the reference a complete/safe view; a plain custom-DST reference is out-of-band/raw-shaped and needs `unsafe`). Dispatch looks up `<Struct>__<method>` directly (concrete signature, then generic); a generic match seeds type inference with `Self` bound to the receiver's type. No matching method is an error.

Source: `src/compiler/sema_expr.cpp#L6785-L6828`

### `trait.dispatch.dyn-inherent-first` — &dyn Trait: inherent impl methods checked before vtable dispatch

A method call on a `TraitObject` receiver (or a `Ref`/`MutRef` to one) first tries an inherent `impl Trait for dyn Foo` method, looked up under the mangled key `$dyn$<Trait>__<method>` (concrete signature, then generic). If found and generic, type-argument inference seeds `Self` with the receiver type; on inference failure the call falls through to vtable dispatch rather than erroring. Only if no inherent method matches does resolution proceed to vtable dispatch.

Source: `src/compiler/sema_expr.cpp#L6834-L6889`

### `trait.dispatch.dyn-return-subst` — &dyn Trait method return-type substitution

The dispatched trait method's return type has `Self` substituted with the receiver's expression type, and the owning trait's (possibly a supertrait's) own type/const parameters substituted with the trait-object's `type_args()` (read from the peeled `TraitObject`, not from a `Ref<TraitObject>` wrapper, since the latter carries no type args) — so a method returning a type mentioning the trait's params (e.g. `Trait<CFG>`) does not leak an unsubstituted parameter to the call site. The call's arguments are then marked moved and the method-call node records the resolved vtable slot index.

Source: `src/compiler/sema_expr.cpp#L7022-L7053`

### `trait.dispatch.dyn-vtable-supertrait` — &dyn Trait vtable dispatch, scope-aware trait resolution, arg checking

Failing an inherent match, a `&dyn Trait` method call resolves the trait scope-aware (current-package-qualified name preferred over an incumbent same-named prelude/imported trait) and dispatches through the trait's flattened supertrait-closure vtable layout: the method's position in that flattened order is its vtable slot, so a supertrait method reachable through `&dyn Sub` dispatches via `Sub`'s own vtable slot for it. `unsafe`-declared methods require unsafe context; explicit-argument count must equal `param_types.size()-1`. Each argument is coerced to its substituted (Self→receiver type) parameter type in the fixed order arg-to-dyn → implicit reborrow → int-widen, then checked: type-compatibility (skipped for TypeVar/AssocType params), variance, and — recursively into array-literal and tuple-literal arguments — that any int-literal element fits its (possibly nested) target integer type. Unknown method on a resolved trait is an error.

Source: `src/compiler/sema_expr.cpp#L6899-L7059`

### `trait.dispatch.mut-ref-demoted-for-ref-self` — A `&mut T` receiver may satisfy a `&self` method on T by demoting to `&T`

If auto-deref lookup for a `&mut T` receiver fails to find a `T`-value method, resolution retries once more with param[0] coerced to `&T` (demoted mutability, same pointee) before falling back to a generic-function lookup under the same mangled key.

Source: `src/compiler/sema_expr.cpp#L8278-L8301`

### `trait.dispatch.primitive-receiver-ref-ptr-ladder` — Method lookup on a primitive receiver tries value, &T, &mut T, *const T, *mut T in order

For a method call `recv.method(args)` where `recv`'s type is a primitive (mangled key `<type>__<method>`), resolution tries param[0] = `T` (value), then `&T`, then `&mut T`, then `*const T`, then `*mut T`, accepting the first exact-signature match. This exposes `impl Trait for T` methods declared with `self: &Self`/`&mut Self`/`self: *const Self`/`self: *mut Self` to dot-call on a bare-value receiver.

Source: `src/compiler/sema_expr.cpp#L8138-L8169`

### `trait.dispatch.ref-blanket-generic-impl` — Generic blanket impl `impl<T> Trait for &T` dispatches via sentinel key with T bound to the pointee

When `recv`'s type is `&T`/`&mut T` and no concrete `$ref_`-mangled impl matched, lookup tries the generic sentinel key `$ref_$T__<method>` / `$mut_ref_$T__<method>`. On match, the impl's type parameter named `T` is bound to `recv`'s pointee type, other type params bind to an error placeholder, `recv` is auto-ref'd (`&recv`), and the call routes through `finish_generic_call` for monomorphization.

Source: `src/compiler/sema_expr.cpp#L8203-L8225`

### `trait.dispatch.ref-impl-mangling-preferred` — Struct-receiver ref-typed impls (`$ref_`/`$mut_ref_`) are tried before auto-deref'd bare-type impls

When a struct-typed receiver's expression type is `&T`/`&mut T`, method lookup first tries mangled keys `$ref_<T>__<method>` (and, if T is a generic struct instantiation, also its base-name form) or `$mut_ref_<T>__<method>`, against the receiver's type as-is, then `&recv`, then `&mut recv` as param[0] (in that order), before falling through to the bare-`T` auto-deref path below. This lets `impl Trait for &T` win over an auto-deref'd `impl Trait for T`.

Source: `src/compiler/sema_expr.cpp#L8399-L8467`

### `trait.dispatch.ref-mangling-nonstruct-pointee` — `impl Trait for &T` / `&mut T` (T non-struct) registers under `$ref_`/`$mut_ref_` mangled keys

When `recv`'s type is `&T`/`&mut T` and no bare-`T` impl matched, method lookup additionally tries the mangled key `$ref_<T>__<method>` (or `$mut_ref_` for `&mut T`), first with `recv`'s type as param[0] as-is, then with an added `&`/`&mut` level (for `self: &Self` methods whose Self is already `&T`). On match, `recv` is auto-ref'd to the matching level via `materialize_recv_ref`.

Source: `src/compiler/sema_expr.cpp#L8170-L8202`

### `trait.dispatch.slice-impl` — impl Trait for [T] / impl Trait for str dispatch

A method call on a `Slice<T>` receiver (or `Slice<u8>` == `&str`) resolves user-defined `impl Trait for [T]` / `impl Trait for str` methods by looking up mangled keys, in order: `$slice$<elem>__<method>` (concrete), `$slice$T__<method>` (generic blanket), and for `Slice<u8>` also `str__<method>`. If no direct match is found, args of type `Ref<Slice<U>>` are flattened to `Slice<U>` and the lookup retried (impl `&Self` for `UnsizedSlice` canonicalizes to bare `Slice`); on a match via flattening, the corresponding argument ASTs are re-lowered stripping an outer `&`/`&mut` so the emitted arg matches the flattened ABI. No autoref is needed for the receiver itself since `Kind::Slice` already matches `&Self`. A `Slice` receiver with no matching key is an error.

Source: `src/compiler/sema_expr.cpp#L6551-L6646`

### `trait.dispatch.supertrait-dag-search` — Bounded-type method search walks the supertrait DAG

Method lookup on a bounded type parameter searches each bound trait and, transitively, its supertraits (depth-first, with cycle/diamond guarding). Substitutions compose along supertrait references: a supertrait reference's type-args (written in the sub-trait's namespace incl. Self) are resolved through the current substitution and bound to the supertrait's formal params.

Source: `src/compiler/sema_expr.cpp#L7400-L7437` · `src/compiler/sema_expr.cpp#L7386-L7398`

### `trait.dispatch.tagged-ptr` — `&tagged<TS>` Trait method dispatch

A method call on a `TaggedPtr` receiver resolves the trait named by the receiver's `trait_name()` in the trait registry (error if absent), looks up the method by name (error if absent), requires `unsafe` context if the method is declared `unsafe`, requires the explicit-argument count to equal `param_types.size()-1`, substitutes a `Self` return type with the receiver's own type, marks the call's arguments moved, and emits a method-call node carrying the tag-system name and trait name (dispatch is by runtime type-code read via the tag-system, not a static vtable slot).

Source: `src/compiler/sema_expr.cpp#L6732-L6777`

### `trait.dispatch.tuple-impl` — impl Trait for (A,B,…) tuple method dispatch

A method call on a tuple receiver (or `&Tuple`/`&mut Tuple`, for trait methods taking `&Self`) resolves user-defined `impl Trait for (A,B,...)` methods via a sentinel-name lookup mirroring the slice path: a generic blanket keyed by arity `$tuple$N`, and a concrete form keyed by arity plus element types `$tuple$N$<t1>$<t2>...`. No match (receiver not a tuple, or no tuple impl matches) falls through to the standard struct-method diagnostic further down.

> **Uncertainty:** The body of this dispatch function lies past this unit's end (L7067); statement is inferred from its header comment only, not its implementation.

Source: `src/compiler/sema_expr.cpp#L7061-L7067`

### `trait.dispatch.value-blanket-fallback-primitive` — Value blanket impl (`impl<T> Trait for T`) is tried as a last resort for primitive receivers

If no method-info was resolved for a primitive receiver through any of the concrete/ref/generic lookup paths, dispatch falls back to the generic value-blanket-impl mechanism before reporting an error.

Source: `src/compiler/sema_expr.cpp#L8377-L8382`

## Method dispatch (`method-dispatch`)

### `trait.method-dispatch.ambiguous-trait-methods` — Method name colliding across multiple traits on a type is a hard error

When a method name is provided by more than one trait implemented for the receiver's type, the plain unqualified mangled symbol is absent from the registry and the trait-qualified registry entry (`Sname__method`) has >1 owning trait; this is reported as an error (`method '{}' on '{}' is provided by multiple traits (...)`) requiring disambiguation via a trait-bounded generic fn or an explicit trait-qualified call, rather than silently picking one trait.

Source: `src/compiler/sema_expr.cpp#L8730-L8747`

### `trait.method-dispatch.blanket-impl-fallback` — Blanket trait-impl methods considered after direct/base-name lookup fails

If no inherent/registered method is found (directly or via the generic base-name fallback), method resolution tries a blanket impl (`impl<T: Bound> Trait for T { fn method … }`) via `try_blanket_method_dispatch`, shared with the primitive-receiver dispatch path.

Source: `src/compiler/sema_expr.cpp#L8723-L8729`

### `trait.method-dispatch.bound-provides-method` — Type-parameter method dispatch via trait bound

A method call `t.m(args)` where `t : T` (T a type parameter) resolves `m` only if some in-scope bound `T: Trait` (or supertrait reference) declares a method named `m`; the chosen trait/method drive the call. If no in-scope bound on T provides `m` and no Deref/DerefMut bound applies, it is an error: "type parameter `'<T>'` has no trait bound providing method `'<m>'"`.

Source: `src/compiler/sema_expr.cpp#L7613-L7635` · `src/compiler/sema_expr.cpp#L7822-L7826`

### `trait.method-dispatch.deref-bound-fallback` — Deref/DerefMut bound autoderef fallback for unresolved method on generic receiver

For a generic type-parameter receiver, if no bound on the type parameter directly provides the called method, but the type parameter carries a `Deref<Target>` or `DerefMut<Target>` bound, the receiver is rewritten to a call to `deref()`/`deref_mut()` (typed `&Target`/`&mut Target` respectively) and method resolution falls through to continue on the dereferenced value.

Source: `src/compiler/sema_expr.cpp#L7845-L7868`

### `trait.method-dispatch.no-bound-error` — No-bound-provides-method is a compile error

Calling a method on a generic type-parameter receiver is a compile error ("type parameter '{name}' has no trait bound providing method '{method}'") when neither a direct trait bound provides the method nor a Deref/DerefMut bound fallback applies.

Source: `src/compiler/sema_expr.cpp#L7869-L7873`

### `trait.method-dispatch.self-subst-from-bound` — Substitute Self and trait type-params from the receiver bound

When dispatching a trait method on receiver of type T, the substitution binds `Self := T` and binds each trait type-parameter to the matching concrete type-argument from the in-scope bound `T: Trait<A0,A1,...>` (positional pairing, type_params[i] := type_args[i] when present). The method return type is computed by applying this substitution.

Source: `src/compiler/sema_expr.cpp#L7617-L7636` · `src/compiler/sema_expr.cpp#L7670`

### `trait.method-dispatch.self-subst-supertrait` — Self/bound-arg substitution seed for supertrait-provided methods

When a method is resolved for a generic receiver via a trait bound (possibly a supertrait reference), the substitution used to instantiate the method's return type seeds Self := receiver's inner (dereferenced) type, and additionally binds each of the chosen trait's own type parameters to the concrete type argument supplied by the matching bound on the receiver's type parameter (e.g. a bound `T: Into<i32>` binds the trait's formal parameter to i32).

Source: `src/compiler/sema_expr.cpp#L7660-L7683` · `src/compiler/sema_expr.cpp#L7717`

### `trait.method-dispatch.trait-tag-mangling` — Trait-qualified symbol tagging for multi-provider / inherent-shadowed methods

When a called method name is declared by more than one trait (or, more generally, whenever any trait declares it), the call site records the chosen trait as a dispatch tag, optionally suffixed with the bound's concrete trait type arguments, so that monomorphization may resolve the trait-qualified symbol `<Concrete>__<Trait>[<targs-suffix>]__<method>` instead of the plain name; this also correctly routes a `T: Trait` bound call to the trait method rather than to a same-named inherent method that would otherwise occupy the plain (unqualified) symbol on the concrete type.

Source: `src/compiler/sema_expr.cpp#L7800-L7841`

## Static dispatch (`static-dispatch`)

### `trait.static-dispatch.trait-qualified-self-inference` — `Trait::static_method(args)` infers Self from hint or unique bounded type-param

A trait-qualified static call `Trait::method(args)` inside a generic fn resolves Self by: (a) the let-annotation/return hint's concrete type if it implements Trait (keyed on bare struct base or concrete-spec name), emitting `<Type>__<method>` directly; else (b) the unique in-scope type-param whose transitive bound-closure includes Trait, emitting `<param>__<method>` for mono to retarget. Ambiguity (>1 candidate) leaves it unresolved.

Source: `src/compiler/sema_expr.cpp#L13433-L13525`

### `trait.static-dispatch.via-type-param-bound` — Generic static dispatch `T::method()` through type-param trait bounds

`T::method()` where T is a type-param resolves the static method (first param not Self) by searching the transitive supertrait closure of T's bounds. Single-dispatch traits route through the uniform generic-call resolver (turbofish/arg-infer/return-hint); multi-param traits (`Sum<Item>`) emit empty type-args and an abstract `T__method` symbol that monomorphization retargets to the concrete impl.

Source: `src/compiler/sema_expr.cpp#L13363-L13432` · `src/compiler/sema_expr.cpp#L13016-L13074`

## Static trait methods (`static-method`)

### `trait.static-method.bound-typeparam-dispatch` — Static trait-method call through a bound type-parameter

A qualified call `Z::m(args)` / `Z::m::<T..>(args)`, where Z is a type-parameter bound by a trait declaring static method `m`, dispatches by substituting Self→Z and m's own type-params (from an explicit turbofish, else inferred from argument types) into m's return type, and passes the resolved type-args as the call's own type-args — so that once Z is later monomorphized to a concrete type, instantiation resolves to `<Concrete>__m::<..>`. Produces no lowering if Z does not name such a bound type-parameter.

Source: `src/compiler/sema_impl.hpp#L4009-L4017`

## Tagged dispatch (`tagdispatch`)

### `trait.tagdispatch.dispatch-table-layout` — Tag-dispatch table maps per-type type_code to fn_ptr per (tag_system, trait, method)

Tag-dispatch (an alternative to vtable-based dyn dispatch) is backed by a per-(tag_system, trait, method) dispatch table mapping a per-concrete-type integer `type_code` (logically u64) to the implementing method `fn_ptr`. A `type_code == 0` entry is the unset sentinel meaning 'no impl registered' and is skipped at table build / treated as no-impl at lookup.

> **Divergence (vs Rust):** Logos addition: tag-dispatch dispatch model has no Rust analogue (Rust uses fat-pointer vtables only).

> **Uncertainty:** type_code allocation policy (which integers map to which types) is defined elsewhere; only the dispatch-table consumption is observable here.

See also: `trait.tagdispatch.type-code-keyed`.

Source: `src/compiler/mlir_gen_dyn.cpp#L181-L186` · `src/compiler/mlir_gen_dyn.cpp#L302-L321`

### `trait.tagdispatch.registration-uniqueness` — At most one impl per (tag_system, trait, type_code)

Each (tag_system, trait, type_code) registration is unique program-wide: registering the same triple from two separately-compiled units is a hard error (detected as a multiply-defined link symbol). Multiple methods of one trait for one type share a single registration (deduplicated per triple, not per method).

> **Divergence (vs Rust):** Logos addition (tag-dispatch); analogous to Rust's coherence/orphan-style uniqueness but enforced at link time.

Source: `src/compiler/mlir_gen_dyn.cpp#L188-L214` · `src/compiler/mlir_gen_dyn.cpp#L324-L355`

### `trait.tagdispatch.registry-lookup-api` — Per-triple public dispatch-lookup function

For each (tag_system, trait, method) triple with at least one tier, a public lookup function `type_code -> fn_ptr` is exposed, checking tier-1 (with an in-range guard against the 256 bound) and falling back to tier-2, returning null when no table has the entry. This enables reflective / deferred invocation of trait methods by type_code.

> **Divergence (vs Rust):** Logos addition: runtime trait-method registry by type_code has no Rust analogue.

Source: `src/compiler/mlir_gen_dyn.cpp#L538-L645`

### `trait.tagdispatch.startup-table-init` — Dispatch tables are populated at program startup

Dispatch tables are zero-initialized statically and filled at program startup before user code runs (one initializer per tag system, invoked from main's prologue). Method bodies observe fully-populated tables; the dispatch tables are not const-folded per call site.

> **Divergence (vs Rust):** Logos addition (tag-dispatch).

Source: `src/compiler/mlir_gen_dyn.cpp#L184-L186` · `src/compiler/mlir_gen_dyn.cpp#L434-L530` · `src/compiler/mlir_gen_dyn.cpp#L532-L536`

### `trait.tagdispatch.tier-boundary-256` — Tag dispatch is two-tier with a type-code boundary of 256

Tag dispatch tables are split into a dense tier-1 array of 256 slots indexed directly by type_code, and a tier-2 sparse lookup function. When both exist, dispatch selects tier-1 iff type_code < 256 (unsigned), else calls the tier-2 lookup(type_code); a missing tier resolves to a null function pointer. At least one tier must exist for the call to be emitted.

> **Divergence (vs Rust):** Logos addition: tiered type-code dispatch table; no Rust analogue.

> **Uncertainty:** Comment text says 'type_code < 223' but the emitted boundary constant is kTier1Size=256; the code is authoritative.

Source: `src/compiler/mlir_gen_dyn.cpp#L1287` · `src/compiler/mlir_gen_dyn.cpp#L1366-L1370` · `src/compiler/mlir_gen_dyn.cpp#L1375-L1442`

### `trait.tagdispatch.tier2-binary-search-sorted` — Tier-2 dispatch requires sorted, gap-free codes

Tier-2 dispatch tables list only registrations whose callee is defined; the (type_code, fn) entries are sorted ascending by type_code with no zero/placeholder gaps, and resolution performs an unsigned binary search over type_code returning the paired fn or null on miss.

> **Divergence (vs Rust):** Logos addition (tag-dispatch).

Source: `src/compiler/mlir_gen_dyn.cpp#L82-L128` · `src/compiler/mlir_gen_dyn.cpp#L378-L391`

### `trait.tagdispatch.two-tier-codespace` — type_code space is split into two dispatch tiers

The type_code key space is partitioned at 256: codes in [1,255] dispatch via a dense direct-indexed table of fixed size 256 (tier-1, O(1) index); codes >= 256 dispatch via a sorted (type_code, fn) pair table searched by binary search (tier-2, O(log n)). A lookup that hits neither tier yields null (no matching impl).

> **Divergence (vs Rust):** Logos addition (tag-dispatch).

> **Uncertainty:** Comment at L307 mentions 1-127 inline / 128-255 zone tier-1 sub-ranges, but the dispatch split here only observes the <256 vs >=256 boundary.

Source: `src/compiler/mlir_gen_dyn.cpp#L181-L186` · `src/compiler/mlir_gen_dyn.cpp#L239-L240` · `src/compiler/mlir_gen_dyn.cpp#L316-L320` · `src/compiler/mlir_gen_dyn.cpp#L604-L645`

### `trait.tagdispatch.type-code-keyed` — Tag-based dispatch keys on a runtime type-code read from the receiver

A `#[tag_dispatch]`-style trait call resolves the target method at runtime by (1) reading an integer `type_code` from the receiver value via the trait's TagSystem `read_tag(self=null, obj_ptr) -> i64`, then (2) indexing a per-(tag_system, trait, method) dispatch structure by that type_code to obtain the method function pointer, then (3) calling it indirectly with the receiver pointer as `self: *const u8` followed by the user args. The TagSystem is a stateless unit struct (self passed as null).

> **Divergence (vs Rust):** Logos addition: runtime type-code/TagSystem dispatch has no direct Rust analogue (Rust uses vtables only).

> **Uncertainty:** TagSystem read_tag encoding variants (legacy 2-byte / vlen / TOM inline header) are noted in comments but not enforced in this unit.

See also: `trait.tagdispatch.dispatch-table-layout`.

Source: `src/compiler/mlir_gen_dyn.cpp#L1308-L1356` · `src/compiler/mlir_gen_dyn.cpp#L1360-L1364` · `src/compiler/mlir_gen_dyn.cpp#L1444-L1474`

## Tagged dispatch — entry emission (`tag-dispatch`)

### `trait.tag-dispatch.entry-emission` — DispatchEntry records emitted for concrete impls of a #[tag_dispatch] trait

For a concrete impl (non-empty trait_name, empty impl_tps) of a trait carrying a non-empty tag_dispatch_system, one DispatchEntry (TAG_SYSTEM, TRAIT_NAME, METHOD_NAME, FN_SYMBOL, IMPL_TYPE_NAME, TYPE_CODE) is emitted per trait method that is actually lowered for this impl (overridden explicitly, or has_default). TYPE_CODE is resolved as: first, an already-annotated nonzero type_code on a matching zoned struct in prog.structs; else explicit_type_codes_ under the current package's fully-qualified name, falling back to the target's own (possibly foreign, base-name-stripped-of-`$G...`) package-qualified name; else a structural hash (type_hash_56bit of type_hash_23(pkg::name)), folded into [128, 2^56) when the raw hash is <128. FN_SYMBOL resolves the mangled `target__method` convention name to the actual compiled symbol_name via find_func_candidates.

Source: `src/compiler/sema_decl.cpp#L2634-L2723`

## Trait objects (`dyn`)

### `trait.dyn.blanket-impl-vtable-synthesis` — blanket impl supplies dyn vtable for concrete types

When no explicit (trait, type) vtable is registered but the trait has a blanket impl `impl<T> Trait for T`, the concrete type's vtable methods are the blanket instantiations named `<type>__<method>` (possibly package-qualified / `__g__`/`__f__`-suffixed), enabling `&Concrete as &dyn BlanketTrait`.

Source: `src/compiler/mlir_gen_dyn.cpp#L1089-L1133`

### `trait.dyn.blanket-vtable-synthesis` — Blanket-impl concrete vtable synthesis

When a concrete type reaches &dyn Trait only via a blanket impl (`impl<T>` Trait for T), a concrete `<Type>__<method>` vtable is synthesized on demand from the trait's slot-ordered method names.

Source: `src/compiler/mlir_gen_impl.hpp#L471-L477`

### `trait.dyn.drop-full-concrete` — drop_in_place runs the full concrete drop

drop_in_place for a dynamically-dispatched value runs the FULL drop of the concrete type (recursive field drops), matching the static drop of that type.

See also: `trait.dyn.vtable-drop-slot0`.

Source: `src/compiler/mlir_gen_impl.hpp#L464-L467`

### `trait.dyn.fat-pointer-vtable-dispatch` — &dyn Trait dispatch loads self=data_ptr and method from vtable

A method call on a `&dyn Trait` receiver is dispatched indirectly: the receiver is a fat pointer {data_ptr, vtable_ptr}; the call loads data_ptr as `self`, loads the method function pointer from the vtable, and calls it indirectly with data_ptr followed by the user args.

Source: `src/compiler/mlir_gen_dyn.cpp#L1481-L1483` · `src/compiler/mlir_gen_dyn.cpp#L1555-L1574` · `src/compiler/mlir_gen_dyn.cpp#L1576-L1607`

### `trait.dyn.object-safety` — Object-safety (dyn-compatibility) constraints (E0038)

A trait may be used as `dyn Trait` only if every method has a vtable slot. A method is rejected if it: is generic (`fn f<T>`); has no `self` receiver (associated fn); returns `Self` by value; returns `impl Trait` (opaque); takes `Self` by value as a parameter; or takes `impl Trait` as a parameter. A method with a `where Self: Sized` bound is excluded from the vtable and so never affects object-safety. A trait owning a generic associated type (GAT) is also not object-safe. The diagnostic is emitted once per trait.

Source: `src/compiler/sema.cpp#L3031-L3130`

### `trait.dyn.supertrait-vtable-slots` — Supertrait pointer slots and upcast layout

A dyn Trait vtable carries, after its method slots, a pointer slot per transitive supertrait; an upcast &dyn Sub -> &dyn Super selects the corresponding super-vtable pointer. A trait with no supertraits has no extra slots (unchanged vtable layout).

Source: `src/compiler/mlir_gen_impl.hpp#L478-L483`

### `trait.dyn.upcast-via-stored-super-vtable` — trait upcast recovers super-vtable from a stored slot

An upcast `&dyn Sub` → `&dyn Super` recovers Super's vtable for the concrete type by loading the super-vtable slot stored at index `3 + |methods| + idx(Super)` of Sub's vtable; each transitive supertrait has exactly one such slot.

Source: `src/compiler/mlir_gen_dyn.cpp#L1149-L1161`

### `trait.dyn.vtable-drop-slot0` — vtable slot 0 is drop_in_place glue

Every dyn-Trait vtable's slot 0 is a drop_in_place glue function that runs the concrete type's full Drop; for a non-droppable type it is a no-op.

See also: `trait.dyn.drop-full-concrete`.

Source: `src/compiler/mlir_gen_impl.hpp#L464-L470`

### `trait.dyn.vtable-layout-order` — dyn-Trait vtable slot ordering

A dyn-Trait vtable is laid out by post-order DFS over the trait's transitive supertrait graph (deepest deduped ancestors first, root trait's own methods last); each method's position is its vtable slot index. Transitive supertraits (every visited trait except the root, same deepest-first order) each get one stored super-vtable-pointer slot after the methods; the `Copy` marker contributes no vtable.

Source: `src/compiler/sema_collect.cpp#L4903-L4921`

### `trait.dyn.vtable-static` — dyn-Trait vtables are static, one per (Trait, concrete type)

Each &dyn Trait coercion of a given concrete type uses a single static vtable global ([N x ptr] of method addresses) shared across all coercions; coercion takes the address of that static vtable rather than allocating/filling a fresh vtable per coercion.

Source: `src/compiler/mlir_gen_impl.hpp#L457-L470`

## Object safety — trait objects (`object`)

### `trait.object.object-safety-required` — Trait objects require object-safe traits

A trait used as a trait object (`&dyn`/`*dyn`/`Box<dyn>`) must be object-safe (dyn-compatible, Rust E0038); a non-object-safe trait used this way is an error, checked when the `dyn Trait` type is resolved and reported once per offending trait.

Source: `src/compiler/sema_impl.hpp#L3210-L3215`

## Object safety (`object-safety`)

### `trait.object-safety.method-generic-not-dispatchable` — method-level generic methods are not dispatchable through dyn

A trait method whose every implementation carries method-level type parameters (e.g. `fn fold<Acc>(...)`) is not callable through `&dyn Trait`: it occupies a non-dispatchable (empty) vtable slot rather than a method pointer (Rust object-safety rule).

Source: `src/compiler/mlir_gen_dyn.cpp#L909-L931`

### `trait.object-safety.sized-self-method-excluded` — where Self: Sized method excluded from vtable

A trait method with `where Self: Sized` is excluded from the trait's vtable and ignored for object-safety determination.

Source: `src/compiler/sema_impl.hpp#L2622-L2623`

## Method mangling — collision (`mangle`)

### `trait.mangle.trait-qualified-on-collision` — Trait-qualified method mangling on name collision

When two distinct traits define a same-named method with the same signature on one type, the colliding methods re-key under `<target>__<trait>__<method>`; a plain base `<target>__<method>` with more than one such entry is ambiguous for concrete-receiver dispatch and requires disambiguation.

Source: `src/compiler/sema_impl.hpp#L2880-L2886`

## Symbol mangling (`mangling`)

### `generic.mangling.duplicate-fn-error` — Duplicate function detection is full-signature + package qualified

A fn's registration key (symbol_name) is derived from its package, base name, and parameter-type/vararg signature. Two fn declarations (generic or non-generic) whose fully computed symbol_name collide within the same base-name overload bucket are rejected with error "duplicate function `'<base>'"` — EXCEPT when both are `extern` declarations of the same signature (accepted silently, see item.extern-fn.dedup-signature). Because the key includes package and full parameter signature, the SAME base name with a DIFFERING parameter signature, or the SAME base+signature in a DIFFERENT package, does not collide and both coexist.

> **Divergence (vs Rust):** Rust does not permit free-function overloading by parameter-type signature under one name in one scope; Logos's signature-keyed registration allows same-named fns with differing signatures to coexist.

Source: `src/compiler/sema_collect.cpp#L4924-L4926` · `src/compiler/sema_collect.cpp#L5049-L5067` · `src/compiler/sema_collect.cpp#L5077-L5093`

## Method mangling (`method-mangling`)

### `trait.method-mangling.bound-targ-suffix` — Trait tag folds concrete bound type-args into a `$G<n>$` suffix

When the receiver bound carries concrete trait type-args (`T: MyTrait<u64>`), the trait tag becomes `Trait` + `$G<n>$<args>` suffix, so mono resolves the args-qualified symbol `<Concrete>__MyTrait$G1$u64__method`, distinct from a sibling `impl MyTrait<u8>`.

Source: `src/compiler/sema_expr.cpp#L7778-L7792`

### `trait.method-mangling.trait-qualified-symbol` — Trait-qualified method symbol when a method name is provided by a trait

When at least one trait declares a method of the given name, the call is tagged with the chosen trait so monomorphization may resolve the trait-qualified symbol `<Concrete>__<Trait>__<method>`; mono falls back to the plain name when no qualified symbol exists. This disambiguates multi-trait collisions and cases where a same-named inherent method occupies the plain symbol.

Source: `src/compiler/sema_expr.cpp#L7753-L7793`

## Auto traits (`auto`)

### `trait.auto.aggregate-structural` — Tuples, structs, and enums auto-satisfy a trait iff all components do

A tuple/struct/enum auto-satisfies an auto-trait (Send/Sync) iff every component type satisfies it: tuple elements, (substituted) struct fields, and every enum-variant payload type. Auto-trait membership is structural over the substituted constituent types.

Source: `src/compiler/mono_clone.cpp#L237-L289`

### `trait.auto.array-element` — Array satisfies auto trait iff element does

An array type [T; N] satisfies auto trait A iff its element type T satisfies A; an array with no element type vacuously satisfies.

Source: `src/compiler/sema_auto_trait.cpp#L221-L222`

### `trait.auto.closure-over-captures` — Closure auto-trait membership is computed over capture types

A closure type's auto-trait (Send/Sync) membership is determined by its captured values' types, not its parameter types. By-reference captures enter as `&T`/`&mut T` (so the reference auto-trait rules apply: `&T: Send` iff `T: Sync`); owned (move) captures enter as `T`; narrow captures use the captured field's type. Since closure types intern by signature, the recorded capture set is the union across all same-signature literals, so the type is `!Send`/`!Sync` if any such literal captures a `!Send`/`!Sync` value.

Source: `src/compiler/sema_expr.cpp#L14900-L14925`

### `trait.auto.closure-via-captures` — Closure auto trait follows its captured types

A closure type satisfies auto trait A iff every captured value's type satisfies A (by-ref captures recorded as &/&mut so reference rules apply). A closure type with no recorded capture set (e.g. a bare dyn Fn annotation without + Send) does not satisfy. Auto-trait satisfaction is computed over captures, not parameter types.

Source: `src/compiler/sema_auto_trait.cpp#L246-L252`

### `trait.auto.conservative-false` — Other types conservatively fail auto traits

Any type kind not otherwise handled (e.g. trait objects) is conservatively treated as not satisfying any auto trait.

Source: `src/compiler/sema_auto_trait.cpp#L254-L256`

### `trait.auto.cycle-guard` — Recursive types terminate as satisfied

Auto-trait checking memoizes on key (type, trait); revisiting an in-progress (type, trait) pair during recursion is treated as satisfied (true), so a recursive type does not loop and is not rejected merely for self-reference.

Source: `src/compiler/sema_auto_trait.cpp#L32-L35`

### `trait.auto.empty-body` — Auto trait must have empty body

An `auto trait` must declare no members; a body containing any FN, associated type, or associated const is an error. Visibility modifiers, type params, and supertraits do not count as members.

Source: `src/compiler/sema_collect.cpp#L2445-L2461`

### `trait.auto.enum-all-payloads` — Enum satisfies auto trait iff all variant payloads do

Absent an overriding impl, an enum satisfies auto trait A iff every payload type of every variant satisfies A. An unknown enum is leniently treated as satisfying.

Source: `src/compiler/sema_auto_trait.cpp#L202-L218`

### `trait.auto.explicit-impl-override` — Explicit impl overrides structural check for struct/enum

For struct/enum (and zoned struct) types, an explicit auto-trait impl is consulted first by candidate key (mangled concrete name, then full type string, then base name): a positive impl accepts and a negative impl rejects, short-circuiting the structural field/variant walk.

Source: `src/compiler/sema_auto_trait.cpp#L37-L85` · `src/compiler/sema_auto_trait.cpp#L168-L170` · `src/compiler/sema_auto_trait.cpp#L203-L205`

### `trait.auto.explicit-impl-overrides` — An explicit impl of an auto-trait short-circuits the structural check

If a type has an explicit impl of the auto-trait (matched by mangled concrete name, type_str form, or unmangled base name), the type satisfies the trait unconditionally, bypassing the structural field/variant walk.

Source: `src/compiler/mono_clone.cpp#L199-L201` · `src/compiler/mono_clone.cpp#L242-L248` · `src/compiler/mono_clone.cpp#L275-L277`

### `trait.auto.generic-impl-bound-check` — Generic positive impl honours its type-param auto-trait bounds

When a positive auto-trait impl has impl type parameters and a generic target, each impl type param is substituted with the corresponding query type argument; for every bound on that param that names an auto trait, the substituted argument must itself satisfy that auto trait, else the impl is rejected.

Source: `src/compiler/sema_auto_trait.cpp#L56-L84`

### `trait.auto.mut-ref-send-sync` — &mut T: Send iff T:Send, Sync iff T:Sync

For a mutable reference &mut T, Send(&mut T) = Send(T) and Sync(&mut T) = Sync(T).

Source: `src/compiler/sema_auto_trait.cpp#L125-L130`

### `trait.auto.mutref-delegates-same-trait` — &mut T: Send iff T: Send, Sync iff T: Sync

&mut T auto-satisfies the queried trait iff T satisfies that same trait (Send→T:Send, Sync→T:Sync). Matches Rust.

Source: `src/compiler/mono_clone.cpp#L226-L230`

### `trait.auto.phantompinned-not-unpin` — PhantomPinned and #[pinned] types are !Unpin

The lang-item logos.lang.marker.PhantomPinned does not satisfy Unpin; likewise a #[pinned] (arena-resident) struct does not satisfy Unpin. These structural opt-outs propagate via the all-fields rule.

Source: `src/compiler/sema_auto_trait.cpp#L164-L167` · `src/compiler/sema_auto_trait.cpp#L176`

### `trait.auto.pointer-always-unpin` — Pointers/references are always Unpin

Raw pointers, &T, and &mut T are Unpin regardless of the pointee's pin-ness, unless an explicit negative Unpin impl exists for that exact pointer type; references never carry a negative Unpin impl and are unconditionally Unpin.

Source: `src/compiler/sema_auto_trait.cpp#L101-L112` · `src/compiler/sema_auto_trait.cpp#L121` · `src/compiler/sema_auto_trait.cpp#L126`

### `trait.auto.raw-pointer-not-send-sync` — Raw pointers are !Send/!Sync absent an explicit impl

A raw pointer type (*const T / *mut T) does not satisfy a Send/Sync-shaped auto trait unless an explicit positive impl exists for that exact pointer type; a matching explicit impl is honoured (positive accepts, negative rejects).

Source: `src/compiler/sema_auto_trait.cpp#L107-L117`

### `trait.auto.raw-ptr-not-send-sync` — Raw pointers are !Send and !Sync absent an explicit impl

A raw pointer type *const T / *mut T does NOT auto-satisfy Send or Sync; it satisfies them only if an explicit (unsafe) impl is present for that pointer type. Matches Rust.

Source: `src/compiler/mono_clone.cpp#L216-L220`

### `trait.auto.recursive-structural-satisfaction` — Auto-trait satisfaction is structural and recursive over constituent fields

A type satisfies an auto trait (e.g. `Send`/`Sync`) iff, recursively and structurally, every constituent field/element type also satisfies it; a `visited` set guards cyclic type graphs. The first field found NOT to satisfy the auto trait is recorded (name + type) to drive the offending-field diagnostic.

Source: `src/compiler/sema_impl.hpp#L3645-L3655`

### `trait.auto.ref-send-iff-pointee-sync` — &T: Send/Sync iff T: Sync

&T auto-satisfies Send iff T: Sync, and Sync iff T: Sync — i.e. the auto-trait obligation on &T is delegated to T: Sync for both Send and Sync. Matches Rust.

Source: `src/compiler/mono_clone.cpp#L222-L224`

### `trait.auto.scalars-and-fnptr` — Scalars and function pointers satisfy all auto traits

Every scalar type (bool, unit/void, iN/uN incl. i24/u24/i56/u56/i128/u128, f32/f64, integer/float literal types), function items, and function pointers satisfy every auto trait unconditionally.

Source: `src/compiler/sema_auto_trait.cpp#L89-L99`

### `trait.auto.scalars-fn-always-satisfy` — Scalars and function types unconditionally auto-satisfy Send/Sync

Primitive scalar types (bool, all integer widths, f32/f64, char), integer/float literals, and function-item / function-pointer types unconditionally satisfy auto-traits (Send/Sync).

Source: `src/compiler/mono_clone.cpp#L204-L214`

### `trait.auto.shared-ref-via-sync` — &T auto-trait reduces to T: Sync

For a shared reference &T, both Send and Sync are satisfied iff T: Sync. (Send(&T) = Sync(T), Sync(&T) = Sync(T).)

Source: `src/compiler/sema_auto_trait.cpp#L120-L122`

### `trait.auto.slice-send-iff-elem-sync` — Slice auto-trait delegates to element Sync; array preserves the trait

[T] (slice) auto-satisfies the queried trait iff T: Sync (slice behaves like &-borrowed element storage). [T; N] (array) auto-satisfies the queried trait iff T satisfies that same trait.

> **Uncertainty:** Slice→Sync delegation for BOTH Send and Sync queries is inferred from the literal "Sync" argument regardless of trait_name; Rust treats [T] like &-storage.

Source: `src/compiler/mono_clone.cpp#L232-L235`

### `trait.auto.slice-via-sync` — Slice auto-trait reduces to element Sync

A slice type [T] (a shared-reference-shaped view) satisfies Send and Sync iff its element type T satisfies Sync (not the queried trait); an empty-element slice vacuously satisfies.

Source: `src/compiler/sema_auto_trait.cpp#L224-L227`

### `trait.auto.struct-all-fields` — Struct satisfies auto trait iff all fields do

Absent an overriding impl, a struct/zoned-struct satisfies auto trait A iff every field type satisfies A, with generic field TypeVars substituted by the struct's concrete type arguments. An unknown struct (no struct/datatype info) is leniently treated as satisfying.

Source: `src/compiler/sema_auto_trait.cpp#L171-L198`

### `trait.auto.structural-satisfaction` — Auto traits are satisfied structurally

An auto trait (e.g. Send, Sync, Unpin) is satisfied by a concrete type via structural recursion over its composition (its field/element types) rather than by an explicit impl, except where an explicit (possibly negative) impl overrides. On failure the offending field (name + type) is reported when known, otherwise that the type is not inherently the trait. The error type and the unit/never-shaped Error kind vacuously satisfy every auto trait.

Source: `src/compiler/sema_auto_trait.cpp#L24-L30` · `src/compiler/sema_auto_trait.cpp#L87-L257` · `src/compiler/sema_collect.cpp#L912-L933`

### `trait.auto.tuple-all-elements` — Tuple satisfies auto trait iff all elements do

A tuple type satisfies auto trait A iff every element type satisfies A.

Source: `src/compiler/sema_auto_trait.cpp#L230-L233`

### `trait.auto.typevar-via-bounds` — Type parameter satisfies an auto trait iff bounded by it

A type variable T satisfies auto trait A iff A appears in T's declared bound list in the current generic context; otherwise it does not.

Source: `src/compiler/sema_auto_trait.cpp#L133-L140`

### `trait.auto.unpin-default-true` — Unpin is satisfied by default

Unpin holds for all types except those that (transitively) contain PhantomPinned, are #[pinned], or carry an explicit negative Unpin impl.

Source: `src/compiler/sema_auto_trait.cpp#L101-L112` · `src/compiler/sema_auto_trait.cpp#L164-L176`

### `trait.auto.unresolved-conservative-true` — Unresolved or self-referential auto-trait queries default to satisfied

An auto-trait (Send/Sync) check conservatively returns TRUE when: the queried type is Error or an unsubstituted TypeVar; the type's struct/enum definition cannot be located in either the output or input tables; or the same (type, trait) pair is already being checked further up the call stack (cyclic structural reference).

> **Uncertainty:** Comment at L255 states this is deliberate ('unknown — be lenient (matches sema)'); a conservative-soundness tradeoff rather than a type-system guarantee.

Source: `src/compiler/mono_clone.cpp#L193-L197` · `src/compiler/mono_clone.cpp#L255` · `src/compiler/mono_clone.cpp#L281` · `src/compiler/mono_clone.cpp#L292-L293`

### `trait.auto.unsafecell-not-sync` — `UnsafeCell<T>` is !Sync; Send follows T

The lang-item `logos.lang.cell.UnsafeCell<T>` never satisfies Sync; it satisfies Send iff its wrapped T satisfies Send (no arg => not Send). Recognition is by qualified name to avoid clashing with same-named user types.

Source: `src/compiler/sema_auto_trait.cpp#L145-L160`

## Copy (`copy`)

### `trait.copy.auto-derive-conditions` — Auto-Copy for all-Copy fieldless-Drop structs

A struct is automatically Copy (never move-only) iff every field type is Copy and it has no `impl Drop`. Manual `impl Copy` entries take precedence (computed after supertrait-impl checking).

Source: `src/compiler/sema_impl.hpp#L2059-L2062` · `src/compiler/sema_impl.hpp#L1930`

### `trait.copy.conditional` — Conditional Copy via Copy-bounded impl params

A generic `impl<P: Copy> Copy for Type<P>` registers conditional Copy: the instance is Copy iff each target type-arg position bound to a Copy-bounded impl parameter is itself Copy. A bound-less param or non-generic target registers Copy unconditionally.

Source: `src/compiler/sema_collect.cpp#L3678-L3705`

### `trait.copy.conditional-copy-impl` — Conditional Copy depends on Copy of bounded type-args

For a conditional `impl<P: Copy> Copy for Wrapper<P>`, an instance `Wrapper<A>` is Copy iff every type-arg position bound to a Copy-bounded impl parameter is itself Copy (evaluated recursively); otherwise the instance is move-only and a move is not a bitwise copy.

Source: `src/compiler/sema_impl.hpp#L1934-L1941`

### `trait.copy.register` — impl Copy registers target as a Copy type

`impl Copy for T` registers T as a Copy type (affecting move semantics); `unsafe impl Copy` is an error because Copy is a safe built-in trait.

Source: `src/compiler/sema_collect.cpp#L3673-L3677` · `src/compiler/sema_collect.cpp#L3702-L3703`

## Built-in traits (`builtin`)

### `trait.builtin.copy-handle-kinds` — Copy is built-in for bitwise-copyable handle kinds

A `Copy` bound is satisfied without an explicit `impl Copy` for the bitwise-copyable handle kinds: shared reference `&T` (incl. `&dyn Trait`), raw pointer `*const/*mut T`, slice `&[T]`, fn pointer, and trait-object fat pointer. `&mut T` is an exclusive move-only borrow and is NOT Copy (falls through to impl lookup).

Source: `src/compiler/sema_collect.cpp#L884-L898`

### `trait.builtin.sized-noop` — `Sized` is a builtin no-op bound

`Sized` is a compiler-builtin marker auto-implemented for every size-known concrete type; a `T: Sized` bound is admitted as a no-op and accepted without trait lookup (matching Rust's implicit `Sized`). This positive-bound path is a no-op; the `?Sized` opt-out is handled elsewhere and IS supported (see `generic.bound.relaxed-only-sized`).

See also: `generic.bound.relaxed-only-sized`.

Source: `src/compiler/sema_impl.hpp#L1623-L1628` · `src/compiler/sema_collect.cpp#L879-L883`

## Freeze (`freeze`)

### `trait.freeze.reachable-unsafecell` — Freeze (no-interior-mutability) predicate

A type is Freeze (no interior mutability reachable by value) unless its own inline byte layout reaches `logos.lang.cell::UnsafeCell` through struct fields, tagged-enum payload variants, or tuple/array elements. A reference/pointer/slice/closure/dyn/custom-DST indirection always STOPS the recursion and counts as Freeze (interior mutability behind an indirection does not infect the container — e.g. `Rc<Cell<T>>`/`&Cell<T>` stay Freeze). Recognition of `UnsafeCell` is by qualified package+name, not bare name, so a user type also named `UnsafeCell` elsewhere does not trigger it. Any unresolved/unknown shape conservatively returns non-Freeze.

> **Uncertainty:** Mirrors rustc's internal (unstable) Freeze auto-trait for noalias/readonly optimization eligibility; this slice shows only the codegen-side predicate, not a user-facing `T: Freeze` bound.

Source: `src/compiler/mlir_gen_types.cpp#L544-L615`

## Generic parameters (`param`)

### `generic.param.bounds-and-const` — type-parameter and const-parameter forms

A type parameter is `NAME [: bound + bound + ...]`; a const generic parameter is `const NAME : TYPE`. Either may be marked variadic with `...`. Bounds are joined with `+`.

> **Divergence (vs Rust):** Variadic type/const parameters (`...`) are a Logos extension.

Source: `src/compiler/sema_render.cpp#L1052-L1099`

### `generic.param.const-generic` — Const generic parameters

A type parameter list may contain const parameters `const N: T`; each carries a const value-type T and may be marked variadic.

```logos
fn f<const N: usize>() -> [i32; N] {}
```

Source: `src/compiler/sema.cpp#L4070-L4080` · `src/compiler/sema.cpp#L4147-L4158`

### `generic.param.default-type-arg` — Default type arguments

A type parameter may declare a default `<T = Default>` (or `<T: Bound = Default>`); the default type is recorded and substituted at use sites when the argument is omitted.

Source: `src/compiler/sema.cpp#L4105-L4112` · `src/compiler/sema.cpp#L4180-L4187`

### `generic.param.implicit-sized` — Type parameters are implicitly Sized

Every type parameter carries an implicit `Sized` bound by default; it is cleared only by an explicit `?Sized` relaxed bound.

Source: `src/compiler/sema.cpp#L3938-L3946`

### `generic.param.lexical-shadowing` — Type/const params shadow outer same-named params

Pushing a scope of type parameters shadows any outer binding of the same name (type, bounds, and `?Sized` relaxation), and popping restores the prior binding exactly; this permits e.g. a method `<T>` to shadow an enclosing trait/impl `<T>`.

Source: `src/compiler/sema_impl.hpp#L3292-L3355`

### `generic.param.sibling-in-scope` — Sibling type-params visible to bound argument resolution

When resolving a type-parameter's bound arguments, all sibling type-param names in the same list are in scope as type variables (so `where F: FnOnce(T, T) -> bool` resolves T).

Source: `src/compiler/sema.cpp#L4055-L4066` · `src/compiler/sema.cpp#L4131-L4142`

### `generic.param.unused-warn` — unused function type-parameter warns

A function type-parameter that appears nowhere in the function's signature is a warning.

> **Uncertainty:** check_unused_generics_in_funcs() body is defined outside this unit; only its invocation site is evidenced here.

Source: `src/compiler/sema_collect.cpp#L573-L575`

### `generic.param.variadic-last` — Variadic type parameter must be last

A variadic type parameter `T...` must be the final entry in the type-parameter list; a non-final variadic param is an error "variadic type parameter must be last".

> **Divergence (vs Rust):** Variadic type/const parameters are a Logos addition not present in Rust.

Source: `src/compiler/sema.cpp#L4188-L4190`

## Type parameters (`typaram`)

### `generic.typaram.shadow-warn` — Fn type-param shadowing an existing type/trait is warned

A (non-specialization) fn type-parameter whose name matches an existing struct, datatype, enum, or trait name (via package-qualified lookup) is accepted but triggers a warning, since the collision currently breaks fn-name resolution at call sites.

> **Uncertainty:** Warning documents a current resolution limitation rather than a designed rule.

Source: `src/compiler/sema_collect.cpp#L4827-L4842`

## Type-parameter shadowing (`shadow`)

### `generic.shadow.type-param-shadows-type-warn` — Type-param shadowing a type/trait warned

A fn type-parameter whose name shadows an existing struct/datatype/enum/trait is warned because it currently breaks fn-name resolution at use sites.

> **Uncertainty:** Stated as a current implementation limitation rather than a designed rule.

Source: `src/compiler/sema_collect.cpp#L4619-L4630`

## Generic arity (`arity`)

### `generic.arity.exact-or-variadic-tail` — Type-argument arity matching

A generic instantiation with parameter list P and argument list A is well-formed iff: if P's last parameter is a variadic pack (`T...`), |A| >= |P|-1 (the pack absorbs >=0 trailing args); otherwise |A| == |P|. Mismatch is an error stating the expected and actual counts.

```logos
Vec<i32>      // P={T}, ok

Tuple<A,B,...> // last param variadic, >=2 args
```

Source: `src/compiler/sema_impl.hpp#L1337-L1366`

### `generic.arity.type-args-on-non-generic` — Type-args on a non-generic target rejected

Supplying >=1 type argument to a target whose type-parameter list is empty (non-generic) is an error: `<context> '<name>': not generic — cannot accept N type arg(s)`. (Diagnosed only after full type-parameter info is available, never during forward-declaration prepass.)

```logos
struct S {} ; let x: S<i32>; // error: not generic
```

Source: `src/compiler/sema_impl.hpp#L1341-L1352`

## Const generic arguments (`const`)

### `generic.const.binds-as-const-var` — Const-generic parameters bind as const-vars

A const-generic type parameter `const N: T` binds in scope as a ConstVar carrying its declared const type; a non-const type parameter binds as a type variable.

Source: `src/compiler/sema_impl.hpp#L3320-L3328`

## Generics lints (`lint`)

### `generic.lint.unused-type-param` — Unused type parameter warning (functions)

A function's declared type parameter that does not appear in its signature (parameter types, return type, or any trait-bound type-args) is warned as unused. Exemptions: variadic packs; const-generic params; names equal to `_` or beginning with `_`; and any param that itself carries a trait bound (the bound counts as a use). Lifetime parameters get the analogous unused-lifetime warning.

```logos
fn f<T>(x: i32) {} // warn: type parameter 'T' is unused

fn f<_T>(x: i32) {} // ok
```

Source: `src/compiler/sema_impl.hpp#L1555-L1604`

## Generic calls (`call`)

### `expr.call.generic-inference-deferred-in-generic-context` — Generic-call inference is deferred inside a generic context

If any argument is a pack expansion or has a TypeVar/AssocType type (partially-substituted context), call inference is deferred to monomorphization: the generic overload is selected and the call shape is preserved (callee type-vars and substituted return type) rather than pinning a concrete instantiation.

Source: `src/compiler/sema_expr.cpp#L3435-L3462` · `src/compiler/sema_expr.cpp#L3586-L3601`

### `expr.call.generic-inference-from-args` — Type arguments of a generic call are inferred from argument types

When a callee is generic and the call is not in a generic/pack-expansion context, type arguments are inferred from the actual argument types; if not all can be inferred it is an error directing the user to explicit `f::<T>(...)` syntax.

Source: `src/compiler/sema_expr.cpp#L3413-L3457`

### `generic.call.antiquot-pack-type-arg` — Type-arg antiquote pack splices a reflected type list

An antiquote pack `$v...` in a generic call's type arguments splices a runtime-produced list of types (e.g. a struct's field types) into the callee's type args; it is carried as a marker TypeVar `__splicepack$v` that flows like a variadic pack and is expanded during monomorphization by chasing the variable to its type-list producer.

> **Divergence (vs Rust):** Logos metaprog reflection extension (no Rust analogue)

Source: `src/compiler/sema_expr.cpp#L5985-L6000`

### `generic.call.callee-resolution-order` — Generic call callee resolution precedence

For a call `f::<TARGS>(args)` with n value args, the callee resolves in order: (1) a generic function overload matching name `f` and arity n; (2) if none, a single non-generic candidate named `f`. If exactly one of these is found it is the callee; otherwise fall through to alternative interpretations (struct ctor / enum variant) before erroring.

Source: `src/compiler/sema_expr.cpp#L5854-L5861`

### `generic.call.impl-target-pattern-unify` — Impl-level type-params bound by target-pattern unification

For a method on `impl<...> Trait for Foo<Pat..>`, the impl-level type-parameters are bound by structurally unifying the recorded impl-target pattern against the receiver-positional type-arguments (e.g. pattern `Vec<T>` vs concrete `Vec<i32>` ⇒ T=i32), not positionally; method-level type-arguments are then layered positionally on top. Positional binding is used when no target pattern is recorded.

Source: `src/compiler/sema_expr.cpp#L4085-L4122` · `src/compiler/sema_expr.cpp#L4033-L4052`

### `generic.call.pub-access-check` — Callee visibility check

Once a generic-call callee is resolved, its visibility (pub / module-only) is enforced relative to the calling package and module.

Source: `src/compiler/sema_expr.cpp#L5962` · `src/compiler/sema_expr.cpp#L6110`

### `generic.call.sized-enforcement` — Sized bound enforced at type-argument substitution

Every type-parameter carries an implicit `Sized` bound unless declared `?Sized`. Passing an unsized type-argument (UnsizedSlice/UnsizedDyn), or a `?Sized` outer type-parameter, at a Sized-required parameter position is an error ("requires `Sized`"), reported before trait-bound checking.

Source: `src/compiler/sema_expr.cpp#L4150-L4184`

### `generic.call.tuple-struct-arity` — Tuple-struct constructor arity check

A tuple-struct constructor call is an error if the number of arguments differs from the number of struct fields.

Source: `src/compiler/sema_expr.cpp#L5880-L5884`

### `generic.call.tuple-struct-field-type-check` — Tuple-struct constructor field type checking

For each tuple-struct constructor argument, the corresponding field type (after substitution) is the expected type: integer literals are widened to it, and a non-TypeVar non-error field type that is incompatible with the argument type is an error.

Source: `src/compiler/sema_expr.cpp#L5893-L5907`

### `generic.call.turbofish-arity` — Type-argument count validation

A generic call supplying more type-arguments than the function has type-parameters is an error. With a variadic type-param, fewer than the non-variadic count is an error. Supplying fewer than the full count (partial turbofish), or interior `_` placeholders, is permitted: the explicit head is pre-bound and the remaining/`_` positions are inferred from argument types and the return-type hint; a still-uninferable position is an error directing the user to turbofish.

Source: `src/compiler/sema_expr.cpp#L4001-L4082`

### `generic.call.turbofish-tuple-struct-ctor` — Turbofish on a tuple-struct constructor

If callee resolution finds no function but `f` names a tuple struct, `f::<TARGS>(args)` constructs that struct: the explicit turbofish TARGS pin the leading struct type-params positionally, any remaining type-params are inferred by unifying each field type with its argument type, and the result is a struct literal of `f` with fields '0','1',… . Argument count must equal the struct's field count.

Source: `src/compiler/sema_expr.cpp#L5862-L5920`

### `generic.call.undefined-callee-error` — Undefined-function diagnostic gated by metaprog mode

If a call's callee resolves to nothing (no fn, struct ctor, or variant), it is an error `call to undefined function 'f'`, EXCEPT in metaprog mode where the call silently lowers with `<error>` type so a not-yet-emitted derive-synthesized function can resolve in a later sema pass.

Source: `src/compiler/sema_expr.cpp#L5954-L5960`

### `generic.call.underscore-type-arg-inference` — `_` turbofish argument is an inference hole

An explicit type argument written `_` becomes an inference hole (TypeVar `_`) and is inferred from the value arguments during call finishing rather than pinned.

Source: `src/compiler/sema_expr.cpp#L5982-L6003`

### `generic.call.unsized-targ-for-relaxed-param` — ?Sized type-param relaxes unsized turbofish argument

When resolving the i-th explicit turbofish type argument, a bare unsized type (`[T]`, `dyn Trait`) is accepted iff the i-th target type-param was declared `?Sized` (implicit_sized=false); otherwise the unsized-by-value diagnostic applies.

Source: `src/compiler/sema_expr.cpp#L5964-L6007` · `src/compiler/sema_expr.cpp#L6112-L6127`

## Generic functions (`fn`)

### `generic.fn.cross-pkg-coexist` — Cross-package same-name generics coexist

Generic functions are keyed by package-qualified mangled symbol; same base+signature in different packages produce distinct symbols and coexist; a duplicate error fires only on an exact symbol-name match within a package.

Source: `src/compiler/sema_collect.cpp#L4837-L4856`

### `generic.fn.lifetime-param-unique` — Lifetime parameters of a fn must be unique

A function's lifetime parameter names must be pairwise distinct; a duplicate lifetime parameter is ill-formed.

Source: `src/compiler/sema_decl.cpp#L463-L467`

### `generic.fn.type-param-unique` — Type parameters of a fn must be unique

A function's type parameter names must be pairwise distinct; a duplicate type parameter is ill-formed.

Source: `src/compiler/sema_decl.cpp#L521-L524`

## Method type arguments (`method-typeargs`)

### `generic.method-typeargs.must-bind-and-check-bounds` — Generic method type args must be fully inferred/bound and satisfy declared bounds

After inference/turbofish, a generic method call requires every entry of `m_type_args` to be present (one per `fi.type_params`); if any slot is unbound, it is an error `could not infer type arguments for generic method '{}'`. Regardless, `check_type_bounds` validates the (possibly partial) `m_type_args` against `fi.type_params`'s declared bounds.

Source: `src/compiler/sema_expr.cpp#L9014-L9020`

### `generic.method-typeargs.turbofish-binds-method-params` — Turbofish binds method-level type-params in order, skipping already-bound

An explicit turbofish on a method call (`it.fold::<i32>(..)`) provides the method-level type-arguments in order; each is bound to the next method type-parameter not already bound by the receiver substitution (struct-inherited params bound from the receiver are skipped).

Source: `src/compiler/sema_expr.cpp#L7919-L7936`

### `trait.method-typeargs.propagate-trait-and-method-params` — Propagate trait-level then method-level type-args to the call

The lowered method call carries type-args in order: first each owning-trait type-parameter (bound from the Self-substitution), then each method-level type-parameter inferred by unifying substituted formal param types against actual argument types. Unbound positions are left null.

Source: `src/compiler/sema_expr.cpp#L7713-L7746`

## Method turbofish (`method-turbofish`)

### `generic.method-turbofish.tail-binding` — Method turbofish binds only the method's own trailing type params

Explicit method turbofish (`recv.method::<T1,T2>(args)`) type args win over inference. When `fi.type_params` carries the receiver struct's/enum's own type params as a prefix (a struct-method-template clone of a trait default method, e.g. `impl<I,T,R> Iterator<R> for MapIter<I,T,R> { fn fold<Acc> }`), the turbofish args are assigned starting at the first `fi.type_params` entry whose name is NOT one of the receiver struct's/enum's own type-param names, not at index 0 — so `mi.fold::<i32>()` binds `Acc=i32`, never clobbering a struct-level param already bound from the receiver.

Source: `src/compiler/sema_expr.cpp#L8831-L8884`

## Method type parameters (`method-typaram`)

### `generic.method-typaram.turbofish-binding` — Turbofish type args bind to a method's own (non-receiver-inherited) type params in order

Explicit turbofish type arguments supplied at a method call site (`recv.method::<...>(...)`) are bound, in left-to-right order, to the callee's type parameters that are not already bound by the receiver's substitution (i.e. skipping struct/enum-inherited type parameters and binding only the method-level ones).

Source: `src/compiler/sema_expr.cpp#L7966-L7983`

## Method type-argument inference (`method-infer`)

### `generic.method-infer.receiver-formal-unify` — Impl-level type params visible only in the receiver formal are unified from the actual receiver

Before general argument-based inference, if the method's first (self) formal type and the actual receiver type differ only in ref-vs-value shape (formal is ref-like and actual is not, or vice versa, with matching pointees), the formal and actual are unwrapped to matching shape and unified; any bindings produced (e.g. `impl<T> Pin<&T> { fn get_ref(&self) -> &T }`, binding `T` from the receiver) seed the substitution context ahead of `infer_type_args`, since such params never appear in `arg_exprs` (which starts after `self`).

Source: `src/compiler/sema_expr.cpp#L8885-L8912`

## Method substitution (`method-subst`)

### `generic.method-subst.enum-receiver-typeargs` — Enum receiver's type-arguments seed the method's substitution context

For an `Enum` receiver type (e.g. `Option<T>`, `Result<T,E>`) with non-empty `type_args`, the same positional type-param → type-arg substitution as for structs is built from the enum's declared `type_params`, so method-level generic inference (e.g. `Option<T>::and<U>`) sees the enum's own type params pre-resolved instead of leaving them as free `TypeVar`s.

Source: `src/compiler/sema_expr.cpp#L8807-L8823`

### `generic.method-subst.owning-trait-params-from-impl` — Owning-trait type-params bound from the receiver's impl

For a method belonging to a trait, the trait's type-parameters (e.g. `Iterator<Item>`) are bound from the receiver type's `impl Trait for Recv` trait-type-args (positional, only filling names not already bound by the receiver's own type-args), so Fn-family bound argument types resolve concretely for closure-formal hints.

Source: `src/compiler/sema_expr.cpp#L7895-L7918`

### `generic.method-subst.recv-typeargs` — Receiver nominal type-args bound into method formal substitution

When the receiver is a generic Struct/ZonedStruct or Enum carrying type-args, the nominal type's declared type-parameters are bound positionally to those args; this substitution is applied to the method's formal parameter types when computing argument type hints.

Source: `src/compiler/sema_expr.cpp#L7877-L7894` · `src/compiler/sema_expr.cpp#L7938-L7941`

### `generic.method-subst.struct-receiver-typeargs` — Struct receiver's type-arguments seed the method's substitution context

For a `Struct`/`ZonedStruct` receiver type with non-empty `type_args`, a substitution map is built pairing the struct's declared `type_params` positionally with the receiver's `type_args` (after stripping one level of `Ptr`/ref indirection); this substitution is applied to the method's param types and return type so, e.g., `Vec<i32>::push(val: T)` type-checks with `T` resolved to `i32`.

Source: `src/compiler/sema_expr.cpp#L8789-L8806`

## Type inference for generics (`infer`)

### `generic.infer.array-len-const` — Const-generic array length inferred from concrete length

Unifying a formal array `[T; N]` (N a const-generic length parameter) against a concrete `[U; M]` binds N → IntLit(M) (if N not already bound and M > 0), in addition to unifying element types.

Source: `src/compiler/sema_expr.cpp#L3683-L3698`

### `generic.infer.bind-first-wins` — Type-param unification binds first occurrence

Unifying a formal type-parameter (TypeVar or ConstVar) against an actual type T records the binding param→T only if the parameter is not already bound; subsequent occurrences do not overwrite. A const-generic parameter at type-argument position (ConstVar) is bound the same way as a type-generic parameter.

See also: `generic.infer.literal-default`.

Source: `src/compiler/sema_expr.cpp#L3619-L3637`

### `generic.infer.fn-bound-propagation` — Fn-family bound drives inference of its signature params

When a type-parameter F with an Fn-family bound `F: Fn*(X..)->Y` is bound to an actual closure/fn-ptr, the actual callable's parameter types and return type are unified against the bound's fn_params/fn_ret, so type-params X/Y that appear only inside F's bound are inferred from the actual callable's signature.

Source: `src/compiler/sema_expr.cpp#L3867-L3901`

### `generic.infer.literal-default` — Literal types default before type-param binding

During unification an actual operand of type IntLit defaults to i32 and FloatLit defaults to f64 before it is used to bind any type parameter. A variadic pack element of literal type is likewise widened (IntLit→i32, FloatLit→f64) before being recorded as a pack type-arg.

See also: `generic.infer.bind-first-wins`.

Source: `src/compiler/sema_expr.cpp#L3612-L3617` · `src/compiler/sema_expr.cpp#L3971-L3976`

### `generic.infer.never-fallback` — Unbound type-param falls back to ! for diverging callees

If a non-variadic type-parameter remains unbound after inference, it is an error (ambiguous) UNLESS the callee's body is statically known to always diverge (panic/loop/never-returning tail), in which case the parameter falls back to the never type `!`. The discriminator is the callee body, not the surrounding callsite divergence: `fn f<T>()->T { return 0; }` errors as ambiguous while `fn f<T>()->T { panic(); }` resolves T = `!`.

> **Divergence (vs Rust):** A7 — abort-only panic; `!`-fallback for diverging bodies follows Rust-2024 inference.

Source: `src/compiler/sema_expr.cpp#L3946-L3966`

### `generic.infer.no-bind-infer-hole` — Inference holes never bind a type-param

Unifying a type-parameter against an inference hole `_` (InferredType) leaves the parameter unbound; the hole is resolved by other arguments or later uses, never pinned to the literal `_`.

Source: `src/compiler/sema_expr.cpp#L3629-L3634`

### `generic.infer.no-bind-self` — Self is never bound by unification

A formal type-parameter named `Self` is never bound during unification (it is resolved by the impl/receiver, not inferred from arguments).

Source: `src/compiler/sema_expr.cpp#L3628`

### `generic.infer.ptr-ref-cross` — Pointer/reference families cross-unify on pointee

A formal Ptr unifies against an actual Ptr, Ref, or MutRef by recursing on pointee. A formal Ref or MutRef unifies against an actual Ref, MutRef, or Ptr by recursing on pointee. Reference/pointer mutability and kind do not block unification; only the pointee is matched.

> **Uncertainty:** Mutability compatibility is enforced elsewhere (arg type-check / B6); unify itself is mutability-agnostic.

Source: `src/compiler/sema_expr.cpp#L3641-L3653`

### `generic.infer.ref-unsize-pointee` — ?Sized inference through reference to slice/dyn

When a formal `&T`/`&mut T` is unified against an actual slice value (Kind::Slice → `&[U]`/`*const [U]`) or trait object (`&dyn Trait`), the actual is treated as a reference whose pointee is the unsized form `UnsizedSlice<U>` (resp. `UnsizedDyn<Trait>`), so a `T: ?Sized` formal pointee binds to that unsized type; substitution later canonicalises Ref/MutRef/Ptr-of-unsized back to the original Slice/TraitObject (same ABI).

Source: `src/compiler/sema_expr.cpp#L3654-L3681`

### `generic.infer.return-type-hint` — Return-type hint participates in inference

When an expected return type is in scope (e.g. from a let-binding annotation), the function's (partially substituted) return type is unified against the expected type, inferring type-params that appear only in return position. The expected-type hint may legitimately bind a parameter even when overload pre-selection found no argument binding for it.

Source: `src/compiler/sema_expr.cpp#L3800-L3811` · `src/compiler/sema_expr.cpp#L3936-L3944` · `src/compiler/sema_expr.cpp#L4061-L4067`

### `generic.infer.structural-recursion` — Unification recurses structurally on matching constructors

Unification of two types with the same constructor recurses into components: Ptr/Ref/MutRef into pointee; Array/Slice into element; Struct (resp. Enum) into positional type-args when struct (resp. enum) names match; Tuple into positional elements; Fn-item/Fn-ptr/Closure into positional parameter types and return type. Mismatched constructors bind nothing.

Source: `src/compiler/sema_expr.cpp#L3640-L3756`

### `generic.infer.trait-bound-impl-args` — Trait-bound type-args inferred via the bound impl

When a type-parameter I is bound to a concrete struct and carries a non-Fn trait bound `I: Trait<A..>`, inference looks up I's impl of Trait, unifies the impl's target pattern against I's actual type, substitutes the impl's recorded trait type-args, and unifies those against the bound's args A.. — so a type-param appearing only inside another param's trait bound becomes deducible.

Source: `src/compiler/sema_expr.cpp#L3903-L3934`

### `generic.infer.unify-return-type-from-let-annotation` — `let` type annotation fills missing turbofish type-args via return-type unification

When a `let` binding carries an explicit type annotation and its initializer is a generic call whose supplied turbofish under-determines the callee's type parameters, the callee's return type is unified against the let's annotated type to fill the remaining type-args — covering the case where a type parameter appears only in the function's return type, not in any argument.

Source: `src/compiler/sema_impl.hpp#L3714-L3719`

### `generic.infer.variadic-pack` — Variadic type-pack collects one element per trailing arg

For a function with a trailing variadic type-parameter, each value argument beyond the fixed parameters contributes one element to the type pack (with IntLit/FloatLit defaulted to i32/f64). The pack length is recorded for `sizeof...` / `[T; sizeof...(P)]` resolution under a symbolic key, and a single-type-param variadic also binds that param to the tuple of pack elements.

Source: `src/compiler/sema_expr.cpp#L3968-L3978` · `src/compiler/sema_expr.cpp#L4123-L4147`

## Substitution (`subst`)

### `generic.subst.method-turbofish-args-verbatim` — Method-call turbofish type args are used verbatim, padded with an error type if short

For a generic method call with explicit turbofish (`x.foo::<A,B>()`), the supplied type arguments are bound positionally to the method's type parameters without inference; any type parameter beyond the supplied count binds to the error type.

Source: `src/compiler/sema_expr.cpp#L8312-L8321`

### `generic.subst.method-type-arg-inference-self-seed` — Generic-method type-argument inference seeds `Self` from the receiver's type

When a generic method call has no turbofish, type-argument inference runs over the argument expressions with the substitution seeded `Self := (receiver's type)`, starting from parameter index 1 (the receiver itself is excluded from the inferred-from set). Failure to infer all type parameters is a diagnostic.

Source: `src/compiler/sema_expr.cpp#L8322-L8329`

### `generic.subst.ref-impl-type-params-from-pointee` — A generic ref-impl's type parameters bind from the pointee struct's own type-argument list

For a matched `$ref_`/`$mut_ref_`-mangled method belonging to a generic impl, the impl's type parameters are bound positionally to the receiver pointee struct's type-argument list (looked up by struct/datatype name), and the call routes through `finish_generic_call`; for a non-generic matched method, the same substitution is applied to compute the concrete return type.

Source: `src/compiler/sema_expr.cpp#L8468-L8501`

## Generic desugaring (`desugar`)

### `generic.desugar.impl-trait-param` — `impl Trait` parameter desugars to a synthetic generic type-param

While collecting a fn's parameter types, an `impl Trait`-style parameter type is desugared into a synthetic generic type-parameter appended to the fn's type_params list (collected via impl_param_desugar_active_/pending_impl_trait_params_); the fn becomes an ordinary generic fn over that parameter's position.

Source: `src/compiler/sema_collect.cpp#L4868-L4877`

## Generic struct literals (`struct-lit`)

### `generic.struct-lit.array-field-elem-infer` — `[T; N]` field infers T from array-value element type

A field declared `[T; N]` where T is the struct's type-param infers T from the element type of an array-typed field value; an IntLit element type falls back to the struct-literal's hint for T or defaults to i32.

Source: `src/compiler/sema_expr.cpp#L10125-L10138`

### `generic.struct-lit.bounds-checked-on-inferred-args` — inferred type-args checked against declared bounds

The fully-assembled list of inferred/hinted type-args for a generic struct-literal is validated against the struct's declared generic bounds (check_type_bounds) before the concrete instantiation is formed.

Source: `src/compiler/sema_expr.cpp#L10190`

### `generic.struct-lit.concrete-spec-selection` — concrete or partial specialization selects effective field set

Once a generic struct-literal's concrete type-args are known, an exact-match concrete specialization (struct_specs_sema_ keyed by mangled concrete name) is used if present; otherwise the best pattern-matching partial specialization (find_best_sema_struct_spec) is used; otherwise the generic template's own field list is the effective definition against which field-inits are validated.

Source: `src/compiler/sema_expr.cpp#L10198-L10206`

### `generic.struct-lit.const-generic-field-subst` — field type substituted with resolved type-args before checking

For a generic struct-literal, each field's declared type is substituted with the literal's resolved struct type-args (index-aligned to the struct's type-params) before comparison against the field-value's type, so a const-generic array-length field (`[T; N]`) resolves to a concrete length at the literal site.

Source: `src/compiler/sema_expr.cpp#L10249-L10254`

### `generic.struct-lit.direct-typevar-field-infer` — direct type-param field infers from value, literal defaults

A field whose declared type is directly one of the struct's type-param TypeVars infers that type-param from the field-value's type; if the value's type is IntLit or FloatLit, the type-param instead takes the struct-literal's type hint for that position if present, else defaults to i32 (IntLit) or f64 (FloatLit).

Source: `src/compiler/sema_expr.cpp#L10112-L10124`

### `generic.struct-lit.explicit-typearg-seed` — turbofish type-args seed generic struct-literal inference

`Struct::<T1, T2, ..> { .. }` seeds each of the struct's declared type-params, positionally up to its arity, with the corresponding resolved explicit type-arg before any field-based inference runs.

Source: `src/compiler/sema_expr.cpp#L10029-L10046`

### `generic.struct-lit.hint-fallback-uninferred-typevar` — uninferred type-param falls back to expected-type hint

After field-based inference, any struct type-param not yet bound is filled from the corresponding type-arg position of the struct-literal's expected-type hint (hint_struct_type_), when that hint names the same struct.

Source: `src/compiler/sema_expr.cpp#L10159-L10165`

### `generic.struct-lit.partial-spec-fields` — full explicit type-args select a struct specialization for field defs

If the struct-literal supplies explicit type-args covering every type-param, the best-matching specialization (full or partial, via find_best_sema_struct_spec) supplies the field declarations used to type-check the literal's field-inits, instead of the generic template's fields.

Source: `src/compiler/sema_expr.cpp#L10048-L10052`

### `generic.struct-lit.ptrref-field-pointee-infer` — pointer/reference field infers T from value's pointee

A field declared `*T`, `&T`, or `&mut T` where T is the struct's type-param infers T from the pointee type of a ref/ptr-typed field value (skipped if the pointee resolves to Error).

Source: `src/compiler/sema_expr.cpp#L10139-L10151`

### `generic.struct-lit.recursive-typevar-unification` — nested type-param inference from field values

A struct type-param appearing nested inside a compound field type (generic struct/enum type-args, array element, tuple element, pointer/ref pointee, closure param/return) is inferred by structurally unifying the field's declared type against the field-value's type, recursing through elem/pointee/type_args/closure_params/closure_ret in parallel; a value typed Error/IntLit/FloatLit at the matching position is not bound.

Source: `src/compiler/sema_expr.cpp#L10063-L10097` · `src/compiler/sema_expr.cpp#L10152-L10156`

### `generic.struct-lit.typevar-field-defers-to-mono` — residual type-param in field type defers check to monomorphization

If a field's (post-substitution) declared type still contains a TypeVar, ConstVar, CfgSlotType, or AssocType anywhere in its structure (recursively through elem/pointee/type_args), sema skips the direct field type-compatibility check for that field and defers validation to monomorphization time.

Source: `src/compiler/sema_expr.cpp#L10259-L10281`

### `generic.struct-lit.variadic-typearg-collection` — trailing variadic type-param collects remaining hint args

A struct's trailing variadic type-param (is_variadic, necessarily last) collects all remaining type-args from the expected-type hint positionally if a matching hint is present; otherwise it takes the single inferred value for that name, or the error type if neither is available.

> **Divergence (vs Rust):** A6

Source: `src/compiler/sema_expr.cpp#L10167-L10177`

## Generic enum literals (`enum-lit`)

### `generic.enum-lit.bounds-check` — Resolved type-args checked against type-param bounds

After resolving the enum's type arguments, each is checked against the corresponding type parameter's trait bounds; unresolved type parameters (no binding) yield an error type.

Source: `src/compiler/sema_expr.cpp#L12502-L12513`

### `generic.enum-lit.dyn-hint-preference` — Trait-object type-arg preferred over concrete payload

When the hint pins a type-param to a (possibly Box/ref-wrapped) trait-object type while the payload argument is a concrete type compatible with it, the enum's type-arg is taken as the trait-object (dyn) type, while the payload expression stays concrete; the concrete payload is unsize-fattened into the dyn slot at codegen.

Source: `src/compiler/sema_expr.cpp#L12428-L12455`

### `generic.enum-lit.hint-ref-ptr-preference` — Hint reference/pointer kind overrides inferred pointee

When the context hint for type-param T is `&U`/`&mut U` and inference produced bare `U`, the hint's reference type is used for T; when the hint is `*const U`/`*mut U` over the same pointee as an inferred reference/pointer, the hint's raw-pointer type is used (preserving the annotated repr, e.g. tagged `Option<*const T>` rather than niche `Option<&T>`).

Source: `src/compiler/sema_expr.cpp#L12464-L12501`

### `generic.enum-lit.intlit-payload-pin` — Integer/float-literal payload type-param resolution

When a variant payload whose formal is a type-param T receives an unresolved integer- or float-literal argument, T is bound to the type the surrounding hint pins for T (widening the literal accordingly) if available; otherwise T defaults to i32 for an integer literal and f64 for a float literal.

Source: `src/compiler/sema_expr.cpp#L12407-L12427`

### `generic.enum-lit.structural-unify` — Structural unification of non-TypeVar payload formals

A variant payload formal that is not a bare TypeVar but mentions the enum's type parameters (e.g. `Pair<T>`) is unified against the actual argument's type to extract nested type-param bindings.

Source: `src/compiler/sema_expr.cpp#L12456-L12462`

### `generic.enum-lit.turbofish-first` — Explicit turbofish type-args bind before inference

Explicit turbofish type arguments on an enum literal (`E::<A>::V`) are applied to `E`'s type parameters before any payload-derived inference, so a payload-less variant (`None`/unit variant) still receives the user-given type-args.

Source: `src/compiler/sema_expr.cpp#L12386-L12406`

## Enum methods (`enum-method`)

### `generic.enum-method.instantiate-on-typeargs` — Generic-enum method dispatch instantiates the method template

For a receiver of generic Enum type (`Enum<...>` with type-args) whose method is a generic template, dispatch routes through the generic-call path with type-args = receiver enum type-args (struct-level prefix) followed by inferred or turbofish-supplied method-level type-args; the receiver's enum type-params are pre-seeded into the substitution before inference.

Source: `src/compiler/sema_expr.cpp#L8004-L8088`

## Field access on generics (`field`)

### `generic.field.spec-overrides-base` — Field type comes from the matching specialization when one exists

For a field of an instantiated generic struct, if a partial/full specialization matches the type-args, the field type is taken from that specialization's field list (and a field absent there resolves to nothing); only when no specialization matches is the field looked up on the base template and substituted.

See also: `generic.spec.most-specific-match`, `generic.field.subst`.

Source: `src/compiler/sema.cpp#L6586-L6603`

### `generic.field.subst` — Generic struct field types are substituted with the instance's type and lifetime args

For a non-specialized instantiated generic struct, each base field type is substituted by mapping the struct's (non-variadic) type parameters to the supplied type-args positionally and its lifetime parameters to the supplied lifetime-args positionally; `&'z T` fields thus resolve to the caller's lifetime.

See also: `generic.field.variadic-expansion`.

Source: `src/compiler/sema.cpp#L6644-L6655`

### `generic.field.variadic-expansion` — Variadic field `name_N` selects the Nth element of the variadic type-arg pack

A variadic struct field declared `name: A...` expands to fields `name_0, name_1, …`; field `name_<idx>` whose declared type is the variadic type parameter resolves to the type-arg at (start-of-pack + idx), where start-of-pack is the count of preceding non-variadic type parameters. Out-of-range or non-TypeVar variadic field types fall back to the raw declared type.

> **Divergence (vs Rust):** A6 — variadic type/field packs are Logos-only.

See also: `generic.field.subst`.

Source: `src/compiler/sema.cpp#L6578-L6582` · `src/compiler/sema.cpp#L6606-L6631`

## References and generics (`ref`)

### `generic.ref.bounds-check-when-concrete` — Generic-ref bound check deferred when TARGS contain TypeVars

Type-param bound checking on a generic-ref value runs eagerly only when all type arguments are concrete; if any type argument is a TypeVar the real bound check is deferred to monomorphization.

Source: `src/compiler/sema_expr.cpp#L6141-L6150`

### `generic.ref.no-variadic-packs` — Variadic type packs forbidden in generic-ref value

A generic-ref value of a function whose last type-param is a variadic pack is an error; variadic type packs are not supported in value position.

Source: `src/compiler/sema_expr.cpp#L6129-L6134`

### `generic.ref.sized-enforcement` — Sized enforcement at generic-ref substitution

Substituting a generic-ref type argument of unsized kind (`[T]` slice or `dyn`) into a type-param that requires `Sized` (implicit_sized true) is an error, suggesting `T: ?Sized` to relax the bound.

Source: `src/compiler/sema_expr.cpp#L6152-L6168`

### `generic.ref.turbofish-no-payload-variant` — Turbofish on a no-payload enum variant in value position

In value position, `V::<TARGS>` where `V` is a no-payload variant of `Option`/`Result` (or a use-aliased variant) constructs that variant, pinning the enum's type-args from the turbofish when the turbofish arity matches the enum's type-param count.

Source: `src/compiler/sema_expr.cpp#L6071-L6109`

### `generic.ref.type-arg-arity` — Generic-ref requires exact type-arg arity

A generic-ref value must supply exactly as many type arguments as the function has type-params; a mismatch is an error.

Source: `src/compiler/sema_expr.cpp#L6135-L6139`

### `generic.ref.value-position-fn-pointer` — `IDENT::<TARGS>` as a value yields a fn-pointer literal

`f::<TARGS>` in expression (non-call) position evaluates to a function-pointer value whose type is the callee signature with TARGS substituted (params and return). TARGS containing TypeVars are deferred: the node carries (base, type_args) and is mangled/substituted at monomorphization time.

Source: `src/compiler/sema_expr.cpp#L6043-L6182`

## Variadic packs (`pack`)

### `expr.pack.sizeof-and-expand` — Variadic pack size and expansion

`P...(N)` yields the length of variadic pack `P` (sizeof-pack), and `P...` in expression position expands the pack `P`.

Source: `tools/peg_gen/grammars/logos.peg#L2737-L2738` · `tools/peg_gen/grammars/logos.peg#L2774`

### `generic.pack.expand-value-source` — name... pack-expand resolves a type-pack or const-pack variable

`name...` (pack-expand) resolves `name` first as an ordinary scope variable (a type-pack TypeVar); if absent, it is looked up among the current type parameters and, if bound with kind ConstVar, treated as a const-pack (`<const N: i64...>`). An unresolved name is an error. Per-element expansion happens later, at monomorphization.

> **Uncertainty:** Full pack-expansion semantics complete at monomorphization, outside this unit; this rule covers only the sema-time name-resolution step.

Source: `src/compiler/sema_expr.cpp#L1047-L1068`

## Specialization (`spec`)

### `generic.spec.bare-ident-resolution` — Bare-ident spec param: known type is concrete, else fresh TypeVar

A bare-IDENT type-param in a specialization pattern that names a known type resolves to that CONCRETE type (a specialization leg); otherwise it is treated as a fresh unbounded TypeVar scoped over the fn signature and body.

Source: `src/compiler/sema_collect.cpp#L4454-L4464`

### `generic.spec.bounded-param-is-typevar` — Bounded spec type-param stays a TypeVar

A type-param carrying trait bounds in a specialization pattern is always a TypeVar (never coerced to a concrete leg), with its declared trait bounds recorded.

Source: `src/compiler/sema_collect.cpp#L4437-L4453`

### `generic.spec.classify-fn` — Specialization-vs-generic classification for fn type-param lists

A fn's type-parameter list classifies the fn as a SPECIALIZATION (rather than a plain generic fn) iff at least one entry is either a structured type pattern (pointer type `*T` or array type `[T; N]`), or a bare identifier naming an already-known concrete type (resolved via the known-type table, or — if not yet resolvable — found in the pre-scanned pass-0 set of all declared names). Otherwise the fn is an ordinary generic fn.

> **Divergence (vs Rust):** A6 — generic specialization has no stable-Rust analogue (Rust `min_specialization` is nightly-only); Logos-only addition.

Source: `src/compiler/sema_collect.cpp#L4434-L4457`

### `generic.spec.classify-struct` — Specialization-vs-generic classification for struct/datatype type-param lists

A struct/datatype decl's type-parameter list classifies it as a SPECIALIZATION iff: (a) an entry is a structured pattern (ptr/array type) — always; or (b) a bare-IDENT entry names a known concrete/primitive type — always; or (c) a bare-IDENT entry names a user-declared type from the pass-0 name set AND a struct/datatype of the SAME name is already registered in the current package (the specialization's "base"). Condition (c)'s base-existence gate prevents a fresh generic struct's own type-param name from being misclassified as a specialization merely because an unrelated type of the same name exists elsewhere (e.g. `struct ChainIter<A, B, T>` colliding with an unrelated `struct A`/`struct B` in another module).

> **Divergence (vs Rust):** A6 — generic specialization has no stable-Rust analogue; Logos-only addition.

Source: `src/compiler/sema_collect.cpp#L4461-L4507` · `src/compiler/sema_collect.cpp#L4484-L4491` · `src/compiler/sema_collect.cpp#L4501-L4503`

### `generic.spec.fn-skip-registration` — Specialization fns bypass normal overload registration

A fn classified as a specialization is validated and lowered inline (via lower_spec_fn, routed into the program's specialization set) and is NOT entered into the ordinary fn overload/registration tables (funcs_ / generic_funcs_) used for plain fn name resolution.

Source: `src/compiler/sema_collect.cpp#L4761-L4764` · `src/compiler/sema_collect.cpp#L4798-L4799`

### `generic.spec.lifetime-param-skip` — Lifetime params contribute no spec-pattern leg

A LIFETIME_PARAM entry in a specialization fn's type-parameter list contributes no spec-pattern leg; lifetime handling is deferred entirely to the borrow checker.

Source: `src/compiler/sema_collect.cpp#L4431` · `src/compiler/sema_collect.cpp#L4536` · `src/compiler/sema_collect.cpp#L4643`

### `generic.spec.method-shadows-impl-param-warn` — Method type-param shadowing an impl-block param becomes an implicit specialization (warned)

Inside a generic impl block, if a method declares a bare, unbounded type-param whose name is identical to one of the enclosing impl block's type-params, the method is silently classified as a specialization on the impl's param (not a fresh method-level generic); the compiler emits a warning naming the shadowed parameter and the method and recommending a rename if a fresh generic was intended.

> **Divergence (vs Rust):** No Rust analogue: Rust treats the method's type-param as a plain shadowing generic, never as an implicit specialization leg; Logos reinterprets it as a specialization on the impl's param (warned).

Source: `src/compiler/sema_collect.cpp#L4763-L4797` · `src/compiler/sema_collect.cpp#L4551-L4586`

### `generic.spec.most-specific-match` — Partial-spec selection picks the most specific matching pattern

Among struct specializations sharing a base name with arity equal to the supplied type-args, a spec is a candidate iff every pattern position matches the corresponding type-arg (TypeVar binds any type but must bind consistently within one spec; concrete kinds must match structurally; Struct/ZonedStruct match by name). The selected spec is the one whose per-position specificity scores are lexicographically greatest (specificity: 0 for a TypeVar, 100 for a concrete leaf, 1+inner for Ptr/Array). No candidate ⇒ no specialization.

> **Uncertainty:** Specificity ordering inferred from specificity_sema constants and lexicographic vector compare.

Source: `src/compiler/sema.cpp#L6506-L6563`

### `generic.spec.partial-pattern-typevars` — Partial specialization keeps unbound params as scope-local type variables

A struct-spec pattern parameter that cannot resolve to a known concrete type (partial specialization, e.g. `Map<Bitmap, V>` keeps `V` free) is registered as a TypeVar in current_type_params_ only for the duration of that spec item's field collection; the concrete spec name derives from concrete_struct_name(make_generic_struct(name, patterns)), so both full and partial specs register under a name usable for later best-fit matching. All pattern typevars are erased from scope immediately after the spec's fields are recorded.

> **Divergence (vs Rust):** A6: partial specialization of user structs is a Logos addition.

Source: `src/compiler/sema_collect.cpp#L3857-L3862` · `src/compiler/sema_collect.cpp#L3869-L3878` · `src/compiler/sema_collect.cpp#L3897-L3899`

### `generic.spec.pass0-forward-ref` — Order-independent concrete-type recognition via pass-0 name set

Classifying a bare type-param name as a concrete-type specialization leg also consults a pre-scanned set of ALL declared type names across modules (pass0_decl_names_), so a spec pattern naming a concrete type whose defining module has not yet been processed in this compilation pass is still classified as a specialization, independent of inter-module processing order.

Source: `src/compiler/sema_collect.cpp#L4450-L4454` · `src/compiler/sema_collect.cpp#L4501-L4503`

### `generic.spec.pattern-legs` — Spec-pattern leg resolution and TypeVar scoping

For a specialization fn/struct, each type-param-list entry resolves to one SPEC_PATTERNS leg: a structured type node (ptr/array) resolves via full type resolution, and any TypeVars nested inside it are extracted into the pattern's TypeVar scope; a bare-IDENT entry WITH trait bounds always stays a TypeVar (bounds are recorded, never coerced to a concrete leg); a bare-IDENT entry WITHOUT bounds resolves to a concrete type if it names a known type, else becomes a fresh unbounded TypeVar. All pattern TypeVars are in scope only while lowering that spec's signature/fields/methods, then removed.

Source: `src/compiler/sema_collect.cpp#L4524-L4562` · `src/compiler/sema_collect.cpp#L4630-L4682` · `src/compiler/sema_collect.cpp#L4606-L4607` · `src/compiler/sema_collect.cpp#L4746-L4748`

### `generic.spec.pattern-list` — Specialization function pattern parameters

A specialization fn's type-parameter list is interpreted as a list of type PATTERNS (not plain generic params): each entry is resolved into a SPEC_PATTERNS type that can be a structured type (e.g. `*T`, `[T; N]`), a TypeVar (bounded or unbounded type-param), or a concrete known type. The patterns drive dispatch to the matching specialization leg.

> **Uncertainty:** Logos has a specialization mechanism with no direct stable-Rust analogue (Rust specialization is unstable).

Source: `src/compiler/sema_collect.cpp#L4414-L4474`

### `generic.spec.pattern-typevar-extraction` — Typevar extraction walks pointer/ref/array type nodes

Extracting typevars from a spec-pattern type node recurses through PTR_TYPE/REF_TYPE/MUT_REF_TYPE into the pointee and through ARR_TYPE into the element type; a TYPE_REF leaf whose name is neither a known type name nor already a bound type param is registered as a fresh TypeVar in the current scope.

> **Uncertainty:** The known-type check here delegates to is_known_type_name, whose builtin primitive list is shorter than try_resolve_as_known_type's (see module.lookup.unqualified-name-scope) — spellings like usize/isize/char/i24/u24 are known here only via the struct/datatype/enum/alias lookups, not the primitive fast path.

Source: `src/compiler/sema_collect.cpp#L4411-L4429`

### `generic.spec.schema-type-params-scope` — Schema/schema-enum bind own type params during collection

A `schema`/`schema enum` item's declared type params (`schema Box<T: WritField>`) are pushed into scope (push_type_params) before resolving its code/category clause and its field/variant types — so a field type `val: T` resolves to a TypeVar rather than failing as an unknown type name — and popped again after the item's fields/variants are fully collected, mirroring collect_struct/collect_impl.

Source: `src/compiler/sema_collect.cpp#L4033-L4037` · `src/compiler/sema_collect.cpp#L4105` · `src/compiler/sema_collect.cpp#L4129-L4131` · `src/compiler/sema_collect.cpp#L4173`

### `generic.spec.spec-registry-keyed-lookup` — Struct specs register concrete-name-keyed; excluded from plain struct registry

A struct specialization registers into struct_specs_sema_ keyed by the concrete name derived from its spec pattern, independent of whether the pattern is fully concrete or partial; partial specs are matched against a call site's actual type arguments later via find_best_sema_struct_spec. A struct recognized as a specialization is excluded from the plain struct registry (structs_) — it is registered only through this specialization path, not through collect_struct.

See also: `generic.spec.struct-pattern-classification`.

Source: `src/compiler/sema_collect.cpp#L3871-L3895` · `src/compiler/sema_collect.cpp#L4178-L4179`

### `generic.spec.struct-pattern-classification` — Struct-spec pattern node classification

In a struct specialization item `struct Name<pat, …> {…}`, each type-param-list entry is classified by node kind: PTR_TYPE/ARR_TYPE nodes are concrete spec patterns (embedded typevars extracted, then the node resolved to a TypeRef); TYPE_PARAM (bare-name) nodes first try to resolve as an already-known concrete type — if resolvable, that concrete type is the pattern, else the name is a partial-spec parameter and is bound as a fresh TypeVar for the duration of collection.

> **Divergence (vs Rust):** A6: Logos supports user struct specialization (`struct Map<Bitmap, V> {...}`), which Rust lacks for structs.

Source: `src/compiler/sema_collect.cpp#L3846-L3864`

### `generic.spec.type-arg-classification` — global name pre-scan disambiguates a partial specialization from a fresh generic base

Before per-module symbol registration, the compiler pre-scans every module's top-level STRUCT/DATATYPE/ENUM/UNION_DEF/SCHEMA_DEF/SCHEMA_ENUM_DEF item names across the whole compilation (order-independent) into a global declared-type-name set. Classifying a second same-named declaration with type arguments as a partial specialization (an argument names an already-declared concrete type, e.g. `Map<K, AnyVal>`) versus a fresh generic base (all arguments are unbound type parameters, e.g. `Map<K, V>`) consults this set, so the classification does not depend on whether the argument's defining module was processed first.

> **Uncertainty:** The actual classifier (is_specialization_fn / is_specialization_struct) is defined outside this unit; only the pre-scan it depends on, and its documented purpose, are evidenced here.

See also: `item.name.forward-reference`.

Source: `src/compiler/sema_collect.cpp#L284-L312`

## Variance (`variance`)

### `generic.variance.adt-by-table` — ADT variance is per-parameter from the computed variance table

For a struct/zoned-struct/enum `D<A0..,'L0..>`, the variance contribution of each type argument `Ai` (resp. lifetime arg) is the meet over arguments of `compose(ambient, declared_variance(D,#i))` recursed into `Ai`, where `declared_variance(D,#i)` (`@i` for lifetimes) comes from the variance table keyed by `pkg.Name`; a parameter absent from the table defaults to covariant (Co).

Source: `src/compiler/sema.cpp#L8087-L8132`

### `generic.variance.compose` — Variance composition under nesting

For a parameter P used at variance `inner` inside `Wrapper<P>`, where the Wrapper field occupies a position of variance `outer` in the enclosing type, P's effective variance there is outer ∘ inner: BiVar absorbs (BiVar ∘ x = x ∘ BiVar = BiVar); Inv dominates the non-BiVar cases (Inv ∘ x = x ∘ Inv = Inv); otherwise the result is Contra iff exactly one of outer/inner is Contra (sign-flip rule: Co∘Co=Co, Co∘Contra=Contra, Contra∘Co=Contra, Contra∘Contra=Co).

See also: `generic.variance.four-kinds`, `generic.variance.meet`.

Source: `include/logos/compiler/variance.hpp#L40-L46`

### `generic.variance.dyn-trait` — Trait objects: covariant in lifetime bound, invariant in type args

`dyn Trait<A...> + 'a` is covariant in its lifetime bound `'a` (the erased object's storage must outlive `'a`) and invariant in each type argument `Ai` (ambient composed with Inv); auto-trait bounds (Send/Sync) are set-membership and contribute nothing to variance.

Source: `src/compiler/sema.cpp#L8144-L8163`

### `generic.variance.field-based-fixpoint` — Struct/enum variance computed by field-type fixpoint

Variance of each struct/enum's type and lifetime parameters is computed by a fixed-point iteration over its fields' types, and recorded per declaration (keyed `pkg.Name`) as a map from parameter index (`#i` for type params, `@i` for lifetime params) to variance.

> **Uncertainty:** Only the declaration and summary comment are in this slice; the fixed-point algorithm itself is defined elsewhere.

Source: `src/compiler/sema_impl.hpp#L3254-L3258`

### `generic.variance.fixpoint` — ADT variances computed by monotone fixpoint over fields

Variances of all struct/datatype/enum parameters are computed by a fixpoint iteration: each parameter is seeded BiVar, then on each round its variance is set to the meet over all field types (enum: over all variant payload types) of `variance_in_type(field, param, ambient=Co)`; iteration repeats until no entry changes, bounded at 32 rounds.

> **Uncertainty:** The 32-round cap is an implementation safety bound; the language semantics is the least fixpoint.

Source: `src/compiler/sema.cpp#L8171-L8261`

### `generic.variance.fn-contravariant-params` — Function types are contravariant in parameters, covariant in return

A function item or function pointer `fn(P0..)->R` is contravariant in each parameter type `Pi` (ambient composed with Contra) and covariant in the return type `R`.

Source: `src/compiler/sema.cpp#L8134-L8143`

### `generic.variance.four-kinds` — Generic-parameter variance kinds

Each type/lifetime parameter of a struct/enum has a variance in {BiVar, Co, Contra, Inv}: Co (covariant) preserves the subtype direction (`Foo<Sub>` <: `Foo<Super>`, &'long T <: &'short T when 'long: 'short); Contra (contravariant) reverses it (fn-argument position); Inv (invariant) requires both directions (mutable-reference content); BiVar (bivariant) places no constraint, used when the parameter appears only in phantom/absent positions.

See also: `generic.variance.meet`, `generic.variance.compose`.

Source: `include/logos/compiler/variance.hpp#L31`

### `generic.variance.meet` — Variance meet (combine multiple uses of a parameter)

When a parameter is used in several positions, its variance is the meet (most-restrictive demand): BiVar ∧ x = x; x ∧ x = x; Co ∧ Contra = Inv; Inv ∧ x = Inv. The lattice ordering is BiVar < {Co, Contra} < Inv.

See also: `generic.variance.four-kinds`.

Source: `include/logos/compiler/variance.hpp#L33-L38`

### `generic.variance.mutref-invariant-pointee` — Mutable reference is covariant in lifetime, invariant in pointee

`&'a mut T` is covariant in its lifetime `'a`, but invariant in its pointee `T` (recurses with ambient composed with Inv).

Source: `src/compiler/sema.cpp#L8062-L8071`

### `generic.variance.raw-ptr` — Raw pointers: *const covariant, *mut invariant

`*const T` is covariant in pointee `T`; `*mut T` is invariant in pointee `T` (ambient composed with Co or Inv respectively). Matches Rust.

Source: `src/compiler/sema.cpp#L8072-L8076`

### `generic.variance.ref-covariant` — Shared reference is covariant in lifetime and pointee

`&'a T` is covariant in its lifetime `'a` (contributes ambient) and covariant in its pointee `T` (recurses with unchanged ambient).

Source: `src/compiler/sema.cpp#L8054-L8061`

### `generic.variance.tuple-array-slice-covariant` — Tuples, arrays, and slices are covariant in element types

A tuple is covariant in each element type (meet over elements, unchanged ambient); `[T; N]` and `[T]` are covariant in element type `T` (recurse with unchanged ambient).

Source: `src/compiler/sema.cpp#L8077-L8086`

### `generic.variance.type-param-occurrence` — Variance of a parameter is the meet over its occurrences

The variance of a type/lifetime parameter `p` in a type `T` is `variance_in_type(T,p,ambient=Co)`, computed structurally: a leaf occurrence of `p` contributes the ambient variance; a parameter that does not occur is bivariant (BiVar). The overall variance combines occurrences via the meet operator (variance_meet); an unmentioned parameter stays BiVar (unconstrained).

> **Uncertainty:** variance_meet/variance_compose lattice (BiVar top, Co/Contra, Inv bottom) defined elsewhere; semantics inferred from usage.

Source: `src/compiler/sema.cpp#L8047-L8053` · `src/compiler/sema.cpp#L8165-L8166`

### `generic.variance.unknown-type-bivariant` — Type kinds not contributing the parameter are bivariant

A type that does not mention the target parameter (or whose kind is not variance-relevant) contributes BiVar (the identity for meet, i.e. no constraint).

Source: `src/compiler/sema.cpp#L8047` · `src/compiler/sema.cpp#L8053` · `src/compiler/sema.cpp#L8164-L8166`

### `generic.variance.unsafecell-invariant` — `UnsafeCell<T>` is invariant in T

`logos.lang.cell::UnsafeCell<T>` (the interior-mutability lang item, recognised by qualified name) is invariant in each type argument (ambient composed with Inv), overriding the table-driven ADT rule.

Source: `src/compiler/sema.cpp#L8090-L8104`

## Writ integration (`writ`)

### `trait.writ.type-code-marks-writ-family` — #[type_code] on trait marks Writ-datatype family

A `#[type_code]` annotation pending on a trait item flags the trait `is_writ` at collection, identifying it as part of the Writ-datatype family that `reflect::<T>()` and reflection emission consult.

> **Divergence (vs Rust):** A6: Writ reflection fabric, Logos-only.

Source: `src/compiler/sema_collect.cpp#L1882-L1892`

## Writ tags (`writ-tag`)

### `trait.writ-tag.type-code-routes-reflect` — #[type_code] trait tags a Writ-tagged datatype family

A trait annotated `#[type_code]` is a Writ-tagged datatype family (is_writ); `reflect::<T>()` for such a trait routes through the Writ reflection path instead of the plain trait path.

Source: `src/compiler/sema_impl.hpp#L2671-L2674`
