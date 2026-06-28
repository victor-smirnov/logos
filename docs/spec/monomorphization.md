# Monomorphization & Compile-time Evaluation

Scope: monomorphization (generic instantiation, substitution, mangling, dispatch lowering) and compile-time evaluation (const-argument specialization, CTFE). Rules are extracted from the compiler `mono` layer (`src/compiler/mono*.cpp`, `mono_*.hpp`) and the supporting `sema`/`codegen` layers; each `### ` heading is the rule's permanent linkable id.

## Reachability

### `mono.reach.lazy-module-emit-on-reach` — Lazy-module functions emitted only when reachable

A function originating from a lazy module is code-generated only if it is reachable from some non-lazy function. Reachability roots are all non-lazy functions and methods; the reach set propagates through direct calls, method-call resolved symbols, closure/fn-pointer call callees, address-of of a function, generic turbofish references, and bare variable references that name a function. Unreached lazy functions are emitted neither as forward declaration nor body.

Uncertainty: Function-pointer/closure values stored without an immediate call are conservatively over-pruned per the source comment.

Source: `src/compiler/mlir_gen.cpp#L215-L268`, `src/compiler/mlir_gen.cpp#L447-L478`

### `mono.reach.generic-call-enqueues-instance` — Generic call site forces instantiation

A reachable call expression carrying explicit type-arguments forces monomorphization of the callee specialized to those type-arguments; the instantiation is keyed by the mangled callee name and instantiated at most once.

Source: `src/compiler/mono_scan.cpp#L145-L156`, `src/compiler/mono_scan.cpp#L458-L460`, `src/compiler/mono_scan.cpp#L625`

### `mono.reach.transitive-from-roots` — Reachability is the transitive closure over function bodies

The set of instantiated/emitted functions is the transitive closure of calls reachable from the entry points: every sub-expression and sub-block of a reachable function body is scanned, and each call therein contributes its callee to the reachable set.

Source: `src/compiler/mono_scan.cpp#L26-L36`, `src/compiler/mono_scan.cpp#L142-L396`

### `mono.reach.writ-builder-fn` — Writ container cast pulls in its builder function

A Writ container cast (e.g. &[T] as <I32>[], or an @{...} comprehension with captures) lowers to a call to a named builder function; that builder function is made reachable even though the cast is not itself a call node.

Source: `src/compiler/mono_scan.cpp#L231-L239`

### `mono.reachability.entry-point-pruning` — Entry points prune non-generic free functions to the reachable closure

When a non-empty entry-point set is supplied, only non-generic free functions transitively reachable (callee-discovery) from those entry points are monomorphized; with an empty set, all non-generic free functions are emitted.

Source: `src/compiler/mono_impl.hpp#L42-L44`, `src/compiler/mono_impl.hpp#L489-L506`

## Work Queue

### `mono.queue.record-generic-only` — Only fully-resolved generic nominals are queued for instantiation

A struct/zoned-struct or enum is recorded as a needed instantiation only when (a) it is the corresponding nominal kind, (b) it has a non-empty type-arg list, and (c) no type-arg contains an unresolved type (TypeVar/Error) at any depth. A non-generic nominal (empty type-args) is never queued.

Source: `src/compiler/mono_impl.hpp#L637-L645`, `src/compiler/mono_impl.hpp#L658-L662`

### `mono.queue.instantiation-depth-limit` — Bounded monomorphization recursion depth

Nominal instantiation tracks a recursion depth; queuing an instance at depth >= max_depth is a hard error ("<kind> instantiation depth limit (N) exceeded for '<cname>'"), bounding infinitely-recursive generic expansion such as Wrap<Wrap<Wrap<...>>>. Each enqueued instance carries depth+1.

Source: `src/compiler/mono_impl.hpp#L648-L654`, `src/compiler/mono_impl.hpp#L666-L672`, `src/compiler/mono_impl.hpp#L1099-L1112`

## Reflection-driven Reachability

### `mono.reflect.varref-let-chase` — Reflection-intrinsic operands chase VarRef through let-inits, bounded 8 hops

When folding a reflection intrinsic, an operand that is a VarRef is resolved by following the variable's recorded let-initializer expression, repeated up to 8 times, to reach the underlying producer expression; this enables `let x = <producer>; intrinsic(x)` to fold identically to `intrinsic(<producer>)`. The hop cap guards against self-referential bindings.

Divergence: A6 (mechanism specific to Logos compile-time reflection)

Source: `src/compiler/mono_clone.cpp#L1599-L1610`, `src/compiler/mono_clone.cpp#L1755-L1762`, `src/compiler/mono_clone.cpp#L1870-L1877`, `src/compiler/mono_clone.cpp#L2099-L2110`

### `mono.reflect-of.zoned-request` — reflect_of on a non-generic zoned struct registers a reflect request

When reflect_of(T) monomorphizes to a ZonedStruct with no type arguments, the struct's fully-qualified name (pkg::name) is recorded as a reflect request for the emitted program.

Source: `src/compiler/mono_clone.cpp#L988-L1001`

## Generic References

### `mono.generic-ref.mangle-enqueue` — Generic function reference resolved to mangled instantiation

A reference to a generic function with type arguments is monomorphized by substituting its type arguments, mangling base+args into a concrete symbol, scheduling that instantiation for emission, and rewriting the node into a plain VarRef carrying the mangled symbol and the substituted function-pointer type; later passes never observe a GenericRef.

Source: `src/compiler/mono_clone.cpp#L606-L623`

## Incremental Instantiation

### `mono.incremental.preserve-prior-instances` — Incremental mono preserves already-cloned instances

When monomorphization is invoked with a previous pass output (prev_out) as seed, generic instances already cloned and non-generic items already passed through in the prior output are preserved and not re-cloned in the current invocation.

Uncertainty: Statement read from MonoOpts dispatch + comment at L1017-L1023; the done_-set seeding that realizes it lives outside this slice.

Source: `src/compiler/mono.cpp#L1014-L1023`

## Pruning

### `mono.prune.entry-point-reachability` — Entry-point-driven reachability mono

Monomorphization may be restricted to a supplied set of entry points; in this mode only items reachable from those entry points are instantiated, supported additionally by a non-owning stdlib template catalog.

Uncertainty: Reachability semantics inferred from set_entry_points / set_stdlib_exports forwarding; the prune walk is implemented elsewhere in the file.

Source: `src/compiler/mono.cpp#L1014-L1027`

## Depth Limits

### `mono.depth.bounded-instantiation` — Monomorphization bounded by max instantiation depth

The monomorphization pass is parameterized by a maximum instantiation depth; generic instantiation that would exceed this depth is not performed (guards against unbounded/infinite recursive instantiation).

Uncertainty: Entry point only declares the bound; the actual depth-limit check and the on-exceed diagnostic are enforced in Mono::run / the worklist driver outside this slice.

Source: `src/compiler/mono.cpp#L1009-L1011`, `src/compiler/mono.cpp#L1014-L1015`

## Instantiation Gating

### `mono.gate.contains-typevar-recursive` — Instantiation gated on no unresolved type at any depth

A generic shape is treated as substitution-incomplete (and its instantiation deferred) iff it contains a TypeVar OR an Error type at ANY depth — recursing through generic type-args, pointee, array/slice element, tuple elements, and closure params/return. Unresolved-reference (Error) types are equated with TypeVars for gating, since both mangle to leaked symbols the codegen layer cannot lower.

```logos
// Option<(A, B)> with A a TypeVar is NOT cloned until A resolves
```

Source: `src/compiler/mono_impl.hpp#L605-L620`

### `mono.gate.assoc-type-defer` — Associated-type projection in pattern defers, never mismatches

A pattern argument containing an associated-type projection (e.g. D::Resources in impl<D: Device> Tr for Foo<D, D::Resources>) cannot be structurally unified against a concrete argument; such a position must DEFER (bind nothing), not report a mismatch — its resolution requires the trait-impl table. Detection recurses through type-args, pointee, element, and tuple elements.

Source: `src/compiler/mono_impl.hpp#L622-L634`

## Instantiation

### `mono.inst.generic-fn-by-substitution` — Generic functions monomorphized per distinct type-argument set

A function with type parameters is not emitted as-is; for each distinct set of type arguments demanded by reachable code, the compiler clones the template body with the substitution applied and emits one concrete instance. Non-generic free functions are emitted directly (cloned once).

Source: `src/compiler/mono.cpp#L380-L383`, `src/compiler/mono.cpp#L591-L618`, `src/compiler/mono.cpp#L621-L653`, `src/compiler/mono.cpp#L646-L646`

### `mono.inst.dedup-by-mangled-name` — Each monomorphized instance emitted at most once

Monomorphization is idempotent per mangled symbol: an instance whose mangled name has already been produced is not re-emitted. The mangled name is the deduplication key (function instances keyed by mangled name; structs/enums by pkg-qualified or bare name; methods by `Struct__method`).

Source: `src/compiler/mono.cpp#L68-L68`, `src/compiler/mono.cpp#L86-L86`, `src/compiler/mono.cpp#L194-L223`

### `mono.inst.demand-driven-closure` — Instantiation set is the demand-driven reachable closure

The set of monomorphized instances is the fixpoint closure of demand: scanning each emitted body discovers further generic-fn instances, method instances, free-fn callees and generic struct/enum uses, which are enqueued and drained until no work remains. Nothing not transitively demanded is instantiated.

Source: `src/compiler/mono.cpp#L615-L615`, `src/compiler/mono.cpp#L650-L650`, `src/compiler/mono.cpp#L805-L843`

### `mono.inst.explicit-instantiate-pins-methods` — `instantiate Foo<T>;` pins every inherent and trait method of the instance

An explicit `instantiate Foo<T>;` (or `pub instantiate`) item forces instantiation of every inherent and trait method of that concrete instance as a demand root (the C++ `template class Foo<int>;` analog), independent of whether any call site references them.

Source: `src/compiler/mono.cpp#L741-L778`

### `mono.inst.pkg-identity-from-generic` — An instantiation's package identity comes from the generic's home package

A monomorphized struct instance is assigned the package of its generic template, not that of the matched specialization; a specialization living in a different package contributes layout only, not conceptual identity.

Source: `src/compiler/mono_clone.cpp#L5751-L5758`

### `mono.inst.explicit-type-code` — Explicit instantiation annotation sets a fixed type_code

A declared explicit instantiation annotation matching an instance's mangled name and carrying a nonzero type_code (e.g. `#[type_code=100] eidos Array<AnyVal>;`) overrides that instance's type_code with the annotated value.

Source: `src/compiler/mono_clone.cpp#L5766-L5772`

### `mono.instantiate.decl` — Explicit instantiation root-pin

`[pub] instantiate <type_ref> ;` materializes the named generic instance as a monomorphization root: all its inherent and trait methods become roots, transitively pulling everything they call. `pub instantiate` additionally marks the instance as part of the package's public API surface.

```logos
instantiate Vec<i32>;
```
```logos
pub instantiate Foo<T>;
```

Divergence: No Rust equivalent; analog of C++ `template class Foo<int>;`.

Source: `tools/peg_gen/grammars/logos.peg#L591-L595`

## Definition Cloning

### `const.def.no-inferred-type` — Const item type may not be inferred

A const item with an explicit type annotation resolves it in item-signature context; `const C: _ = …` is rejected. (Rust E0121)

Source: `src/compiler/sema_collect.cpp#L2148-L2152`

### `const.def.duplicate` — Const item uniqueness

A const name that collides with an already-collected module const or generic const in the same scope is an error.

Source: `src/compiler/sema_collect.cpp#L2158-L2163`

### `const.def.no-self-reference` — Const initializer may not reference the const itself

A const initializer that directly references the const being defined is an error (e.g. `const X = X` or `const X = X + N`).

Source: `src/compiler/sema_collect.cpp#L2179-L2208`

### `const.def.initializer-const-evaluable` — Const/static initializer must be const-evaluable

A const/static initializer must be one of: a literal (int/bool/str/float/char/bytes/wstatic); a WritStatic literal (writ map/array/str/int/float/bool/null); a `metacall fn(...)`; a CAST/PAREN/UNARY of a const-evaluable operand; a BINOP whose both operands are const-evaluable; an array/tuple literal (deferred to a later, more specific check); a struct literal all of whose field-init values are const-evaluable (field-shorthand rejected); a VAR_REF to an already-collected module const/static or to a known free fn (fn-pointer constant); or `&X` where X is a VAR_REF to a module const or otherwise const-evaluable. Any other form (notably a bare fn call) is rejected, because it would silently inline at every read site rather than produce a compile-time constant.

Divergence: A2 — const-evaluable bare fn calls are not supported; the escape hatch is explicit `metacall fn(...)` (Rust would allow `const fn`).

Related: `const.def.no-self-reference`

Source: `src/compiler/sema_collect.cpp#L2218-L2327`

### `const.def.static-reads-static` — Static initializer may read other module items

A const/static initializer may refer to and read another already-collected module const/static via a bare VAR_REF, and may take its address via `&X` (shared ref); `&mut X` in static-init position is not const-evaluable here and is rejected.

Source: `src/compiler/sema_collect.cpp#L2277-L2318`

### `const.def.type-checked-initializer` — Const initializer must be compatible with the declared const type

For `const NAME: T = expr`, the type of `expr` must be compatible with the declared type `T`; an incompatible initializer is a compile error reported at sema. Error types on either side suppress the check.

Source: `src/compiler/sema_decl.cpp#L1541-L1560`

### `const.def.writ-static-literal-compat` — WStaticLit initializer is compatible with a WritStatic const

A const whose declared type is a WritStatic struct accepts an initializer whose type is a Writ-static literal (WStaticLit); this combination is treated as type-compatible.

Divergence: A: Writ-static literal coercion is a Logos addition.

Source: `src/compiler/sema_decl.cpp#L1549-L1554`

### `const.def.generic-const-params-in-scope` — Generic const value sees its type params as type variables

When lowering a generic const's value expression, the const's type parameters are in scope as type variables so `<type:T>` uses resolve to the parameter rather than an unbound name; concrete instantiation happens per use-site.

Source: `src/compiler/sema_decl.cpp#L1527-L1540`, `src/compiler/sema_decl.cpp#L1564`

## Generic Instantiation

### `const.generic.per-instantiation-reresolve` — Generic compile-time const re-resolved per instantiation

A generic compile-time const `const X<T1,...>: WritStatic = @{ ... };` re-resolves its value-AST under each use-site's bound type-args: `<type:Ti>` slots resolve to the concrete type and the resulting value has a distinct per-instantiation identity.

Uncertainty: Writ-static metaprogramming feature; identity-per-instantiation inferred from comment.

Source: `src/compiler/sema_impl.hpp#L2934-L2947`

## Method Instantiation

### `mono.method-inst.struct-receiver-only` — Method instantiation requires a struct receiver

Demand-driven method instantiation applies only when the concrete receiver type is a (possibly zoned) struct; receivers of any other type kind produce no method instances.

Source: `src/compiler/mono_scan.cpp#L715-L718`

### `mono.method-inst.overload-name-match` — Overload matching by user name plus signature suffix

A method reference name M matches a struct method whose stored short-name S satisfies S == M or S == M ++ "__g__" ++ sig; every match is instantiated as a distinct overload, each keeping its own signature.

Source: `src/compiler/mono_scan.cpp#L730-L743`

### `mono.method-inst.skip-method-tparams` — Method-level type-params are not bound by struct instantiation

A method carrying type-params beyond the struct's own (e.g. `fn map<U>`) is NOT instantiated by the struct-driven monomorphization path; its method-level type-params can only be bound by an actual call site supplying them (turbofish/inference).

Source: `src/compiler/mono_scan.cpp#L750-L767`

### `mono.method-inst.positional-tparam-bind` — Struct type-args bind struct type-params positionally with variadic tail

Concrete struct type-arguments bind to the struct's type-parameters left-to-right by position; a variadic type-parameter absorbs all remaining trailing type-arguments as one pack.

Source: `src/compiler/mono_scan.cpp#L774-L784`

### `mono.method-inst.impl-target-applicability` — Structured impl self-type gates method applicability by unification

When a method's impl has a structured target pattern (e.g. `impl<T> Pin<&T>`), the pattern's args are unified against the concrete struct's args: failure to unify means the method does not apply to this instantiation and is skipped; success merges the impl-level bindings (T) into the substitution alongside the struct-level bindings.

Source: `src/compiler/mono_scan.cpp#L786-L814`

### `mono.method-inst.defer-unresolved-args` — Non-concrete or projection args defer applicability decision

When unifying an impl target pattern, a concrete struct arg that is null, contains a type-variable, or whose pattern position is an associated-type projection is not structurally decidable and is skipped (deferred to a later fully-concrete pass) rather than treated as a mismatch.

Source: `src/compiler/mono_scan.cpp#L799-L803`

### `mono.method-inst.dedup-per-concrete-overload` — Method instances dedup per concrete type and overload

At most one instance is produced per (concrete struct type, user-facing method name, overload short-name); overloads sharing one user-facing name collapse to a single slot per signature, matching eager-clone semantics.

Source: `src/compiler/mono_scan.cpp#L769-L772`, `src/compiler/mono_scan.cpp#L870-L878`

### `mono.method-inst.specialization-free-fn-wins` — Concrete impl specialization preempts blanket method clone

If a non-generic `impl Foo<Concrete>` already provides a free function under the target method's mangled name, the blanket/generic method body is not cloned for that instantiation; the concrete specialization is used instead.

Source: `src/compiler/mono_scan.cpp#L879-L886`

## Impl Method Instantiation

### `trait.impl-method.impl-target-pattern-preserved` — Impl target pattern preserved for impl-arg to receiver mapping

Each lowered impl method carries the impl's structured self-type target pattern so monomorphization can unify the pattern against the concrete receiver and map impl-level type arguments to the type's concrete arguments, rather than binding positionally by base name.

Source: `src/compiler/sema_decl.cpp#L2241-L2257`, `src/compiler/sema_decl.cpp#L2531-L2541`

## Specialization

### `mono.spec.partial-specialization-preferred` — Best-matching partial specialization is selected over the generic base

Resolving a concrete struct type to its definition prefers the best-matching partial specialization (positionally), falling back to the generic base template (with positional type-var binding) only when no specialization matches. Layout/size computations use the definition that is actually emitted.

Source: `src/compiler/mono_impl.hpp#L215-L221`, `src/compiler/mono_impl.hpp#L276-L277`

### `mono.spec.most-specific-wins` — Most-specific specialization is selected

Among the specialization patterns of a function whose arity equals the call's type-argument count and that all match the type-arguments, the one with the strictly greatest specificity vector is selected; a matching specialization is preferred over the generic template.

Source: `src/compiler/mono_scan.cpp#L427-L446`, `src/compiler/mono_scan.cpp#L627-L636`

### `mono.spec.ambiguous-is-error` — Tied specializations are an error

If two or more matching specializations have equal greatest specificity (no unique most-specific match), instantiation fails with an 'ambiguous specializations' error.

Source: `src/compiler/mono_scan.cpp#L439-L451`

## Struct Specialization

### `mono.struct-spec.best-by-specificity` — Generic struct instantiation selects the most specific matching specialization

For a generic struct base B applied to type-args A, among all declared specializations of B whose pattern arity equals |A| and each of whose patterns matches the corresponding A[i], the one selected is that with the lexicographically greatest specificity vector. Non-matching or arity-mismatched specializations are excluded.

Related: `mono.struct-spec.ambiguous-error`

Source: `src/compiler/mono_clone.cpp#L5461-L5491`

### `mono.struct-spec.ambiguous-error` — Two equally-specific matching struct specializations is an error

If two or more matching struct specializations share the maximal specificity vector (no strict winner), instantiation is rejected with an 'ambiguous specializations for struct' error.

Related: `mono.struct-spec.best-by-specificity`

Source: `src/compiler/mono_clone.cpp#L5484-L5496`

### `mono.struct-spec.fallback-template` — Absent a matching specialization, the bare generic template is used

When no declared specialization matches a struct instantiation, the generic template for (pkg, base) is used and its type-params are bound positionally to the instantiation's type-args; a trailing variadic type-param absorbs all remaining type-args as a pack.

Related: `mono.subst.variadic-pack-absorb`

Source: `src/compiler/mono_clone.cpp#L5724-L5743`

### `mono.struct-spec.bound-str-canonical` — &[u8] canonicalizes to str for bound checking

When checking a type-param bound, a concrete type of `&[u8]` is treated as `str` (its canonical wire form) for purposes of trait-impl lookup.

Source: `src/compiler/mono_clone.cpp#L6158-L6160`

## Spec Selection

### `mono.select.type-specificity-score` — Specialization specificity ranking

Among overlapping partial specializations, candidates are ranked by per-position specificity: a TypeVar (or null) scores 0; Ptr/Array score 1 + specificity(pointee/elem); any other concrete type scores 100. Selection uses both the summed score and the per-position specificity vector (lexicographic) so specs equal by sum but differing positionally (Map<Bitmap,V> vs Map<K,AnyVal>) are disambiguated.

Source: `src/compiler/mono_impl.hpp#L1029-L1050`

## Type Substitution

### `mono.subst.fn-signature` — Function signature monomorphization substitutes type params

Monomorphizing a function clones its signature applying the type substitution s to the return type and to every non-variadic parameter type; non-type signature attributes (name, package, extern/vararg flags, local slot count, lifetime params, lifetime outlives) are preserved verbatim.

Source: `src/compiler/mono_clone.cpp#L4866-L4908`

### `mono.subst.variadic-pack-expand` — Variadic type-pack parameter expands to N concrete params

A variadic parameter whose type is a TypeVar pack P, when P is bound in the pack map to a list of N types, expands into N distinct parameters named `<param>$<i>` (i in 0..N), each typed by the i-th pack member; if P is not bound the parameter is dropped.

Uncertainty: Expanded name form `make_pack_arg_name` inferred from call; exact separator defined elsewhere.

Source: `src/compiler/mono_clone.cpp#L4889-L4902`, `src/compiler/mono_clone.cpp#L4897`

### `mono.subst.variadic-pack-absorb` — Variadic type-param absorbs trailing type-args as a pack

During positional binding of a template's type-params to type-args, a variadic type-param `...T` binds to a pack consisting of all remaining type-args from its position onward; non-variadic params bind one-to-one.

Source: `src/compiler/mono_clone.cpp#L5734-L5742`, `src/compiler/mono_clone.cpp#L5893-L5901`, `src/compiler/mono_clone.cpp#L6117-L6125`

### `mono.subst.const-generic-value-use` — Const-generic params used in value position substitute their concrete value

A const-generic parameter `<const N: T>` referenced in expression position is monomorphized by splicing its concrete value: a scalar/IntLit binding lowers to an integer literal of the substituted value; a WritStatic-literal binding splices the registered WritStatic literal at the use site.

Divergence: A1/A2 related: const-generics are real, distinct from const-eval

Source: `src/compiler/mono_clone.cpp#L509-L538`

### `mono.subst.i128-literal-high-half` — Cloning a 128-bit literal preserves its high 64 bits

When monomorphization clones an integer literal whose value needs more than 64 bits (i128/u128), both the low and high 64-bit halves are preserved; cloning never silently truncates to 64 bits.

Source: `src/compiler/mono_clone.cpp#L483-L493`

### `mono.subst.var-ref` — Variable references preserve var slot across mono

A non-specialized variable reference is cloned as a VarRef carrying the (substituted) type and the original variable slot, so that post-monomorphization passes retain slot identity rather than re-keying by name.

Source: `src/compiler/mono_clone.cpp#L578-L582`

### `mono.subst.size-align-of` — sizeof/alignof operand type is substituted

sizeof(T) and alignof(T) are monomorphized by substituting type variables in T with the instance's type arguments before emitting the size/align query; the result type is unchanged.

Source: `src/compiler/mono_clone.cpp#L594-L605`

### `mono.subst.generic-enum-lit-mangle` — Generic enum literal takes mangled concrete enum name

When an EnumLit/EnumLitData is monomorphized and its result type is an Enum with non-empty type-args, the emitted enum name is `enum_name` followed by `__<mangle(arg)>` for each type-arg; the concrete enum is recorded as needed. Otherwise the original enum name is kept.

Source: `src/compiler/mono_clone.cpp#L1068-L1099`

### `mono.subst.generic-struct-lit-name` — Generic struct literal takes concrete (mangled) struct name

When a StructLit is monomorphized and its result type is a Struct/ZonedStruct with non-empty type-args, the emitted struct name is the concrete (monomorphized) struct name; otherwise the original literal name is used. The concrete struct is recorded as needed.

Source: `src/compiler/mono_clone.cpp#L1108-L1124`

### `mono.subst.match-arm-pattern-types` — Match-arm patterns are type-substituted during monomorphization

Cloning a MatchExpr substitutes types within each arm's pattern (via the current SubstMap) and recursively substitutes scrutinee, guard, and arm-value expressions.

Source: `src/compiler/mono_clone.cpp#L1127-L1144`

### `mono.subst.splicepack-producer-fold` — $fs... splice-pack folds reflected [Type] producers into call type-args

A call type-arg encoded as TypeVar named `__splicepack$<v>` is resolved by chasing `v` (≤8 VarRef hops) to a producer call and folding its element types into the callee's type-args: `__type_refs_of__` → all (substituted) type-args; `__args_of__` → type-args of the first type-arg T; `__typelist_tail__` → T.type_args[1..]; `__tuple_elems_of__` → T's tuple elements (T must be Tuple); `__field_types_of__` → the (substituted) field types of struct T. An unrecognized producer is a hard error.

Divergence: Logos reflection/metaprogramming extension; no Rust equivalent.

Source: `src/compiler/mono_clone.cpp#L1287-L1392`

### `mono.subst.pack-key-expansion` — Type/const pack-key in call type-args expands to the bound pack

A call type-arg that is a TypeVar or ConstVar whose name is a key in the active pack map expands to that pack's entries (each localized into the current arena). Otherwise the type-arg is type-substituted normally.

Source: `src/compiler/mono_clone.cpp#L1394-L1409`

### `mono.subst.method-vs-receiver-tparam-split` — Method-level type params separated from receiver-level for inference

For a generic method on a generic receiver, the template's type-parameter list is partitioned: names matching the receiver type's (struct or enum) type-parameter names are receiver-level; the rest are method-level. Method-level type arguments are inferred by structurally matching the call's argument types against the template's parameter types; the call is only retargeted to a concrete callee when ALL method-level params are thereby bound.

Source: `src/compiler/mono_clone.cpp#L3240-L3298`, `src/compiler/mono_clone.cpp#L3261-L3271`

### `mono.subst.impl-target-pattern-unify` — Impl-level bindings recovered by unifying impl target pattern

When an impl's target is a structured pattern (e.g. `impl<T,E> Foo<Vec<T>,E>`) rather than the bare shape `Foo<T,E>`, impl-level type parameters are bound by unifying the recorded impl-target pattern against the concrete receiver type, not by positional copy of the receiver's type arguments. If no pattern is recorded or unification fails, positional binding from the receiver's type arguments is used as fallback. The final monomorphized type-argument vector is [receiver/impl-level..., method-level...].

Source: `src/compiler/mono_clone.cpp#L3299-L3350`, `src/compiler/mono_clone.cpp#L3313-L3347`

### `mono.subst.struct-self-type-pattern-args` — Structured impl self-type yields substituted pattern args for the concrete receiver

For a fully-concrete generic call rewritten to a struct-method callee, when the impl has a structured self-type (e.g. `impl<T> Pin<&T>`), the concrete struct's type arguments are the impl-target pattern args after substituting the impl-level and method-level type arguments, not the raw impl-level params; positional copy is used only when no pattern exists or substitution leaves a TypeVar/assoc-type unresolved.

Source: `src/compiler/mono_clone.cpp#L3403-L3446`

### `mono.subst.enum-receiver-spec-synthesis` — Generic-trait-for-enum method instantiates per-enum spec

For a method call whose receiver is an enum (or pointer/ref/mut-ref to an enum) dispatched through a generic `impl Trait for Enum<T>`, monomorphization synthesizes the concrete spec key by inserting the receiver enum's concrete type-args between the enum base name and the method suffix, mirroring eager enum-template instantiation, so the call binds to the cloned spec rather than the bare base template.

Source: `src/compiler/mono_clone.cpp#L3761-L3803`

### `mono.subst.lazy-blanket-method-instantiation` — Lazy blanket-impl method instantiation for generic receiver

A method call on a generic struct/enum receiver (after stripping ptr/ref/mut-ref) dispatched through a blanket `impl<T: Bound> Trait for T` is instantiated lazily: the blanket method template is cloned with {T -> receiver type} and enqueued, provided the receiver concrete type satisfies the blanket's bound trait and all extra bounds; the resulting spec is `<recv>__<method>`.

Source: `src/compiler/mono_clone.cpp#L3804-L3878`

### `mono.subst.blanket-bound-check` — Blanket-impl instantiation gated by bound satisfaction

A blanket `impl<T: Bound + Extra...> Trait for T` is instantiated for a concrete receiver type only if that type satisfies the bound trait and every extra bound; otherwise the blanket impl does not apply.

Source: `src/compiler/mono_clone.cpp#L3854-L3864`

### `mono.subst.typevar-pack-expansion` — Type-argument variable bound to a pack expands in place

When substituting a method call's type-arguments, a type-argument that is a type-variable currently bound to a type pack is replaced by the full sequence of the pack's element types (variadic expansion); other type-arguments are substituted individually.

Source: `src/compiler/mono_clone.cpp#L3884-L3893`, `src/compiler/mono_clone.cpp#L3995-L4004`

### `mono.subst.tuple-receiver-elem-args` — Tuple receiver supplies its element types as impl type-args

For a method call whose receiver is a tuple type, the call's impl-level type-arguments are set to the tuple's element types, enabling specialization of `impl<A,B,...> Trait for (A,B,...)`; for nested tuple recursion the inner receiver's own element types override stale outer-spec args.

Divergence: A6: Logos-only variadic tuple-type impls — `impl Trait for (A,B,...)` specialized by tuple element types.

Source: `src/compiler/mono_clone.cpp#L3894-L3912`

### `mono.subst.drop-args-non-generic-impl` — Type-args dropped/truncated to template type-param count

A method call's type-arguments are kept only up to the resolved template's declared type-parameter count: if the template has zero type-params (concrete impl) all type-args are cleared; otherwise they are truncated to the param count, EXCEPT a variadic template type-param consumes all trailing type-args (no truncation).

Divergence: Logos-specific (T9-tr-02) plus variadic-pack handling

Source: `src/compiler/mono_clone.cpp#L3913-L3963`

### `mono.subst.mangle-and-enqueue-method-generic` — Method-generic call is mangled then enqueued for cloning

When a method call retains nonzero type-arguments after truncation, its callee is mangled with those type-args and the resulting specialization is explicitly enqueued onto the monomorphization worklist so its body is cloned (otherwise the call would name a spec that is never emitted).

Source: `src/compiler/mono_clone.cpp#L3964-L3974`, `src/compiler/mono_clone.cpp#L4064-L4080`, `src/compiler/mono_clone.cpp#L4126-L4138`

### `mono.subst.const-arg-method-specialize` — Const-argument specialization includes the method receiver

For a concrete method call kept as a method call, const-argument specialization treats the receiver as parameter index 0 (a combined [receiver, args...] view) so a const-wanting parameter at any position, including the receiver, is mapped and the callee specialized.

Source: `src/compiler/mono_clone.cpp#L4144-L4156`

### `mono.subst.closure-substitution` — Closure substitution rewrites param/return/capture types

Substituting a closure applies the type substitution to each parameter type, the return type, and each capture type, while preserving the closure id, move-ness, fn-pointer-ness, escape (heap-env) flag, capture names, per-capture mutability flags, per-capture field paths (RFC-2229), and per-capture narrow field types (also substituted).

Divergence: RFC-2229 disjoint-closure-capture metadata preserved through mono

Source: `src/compiler/mono_clone.cpp#L4164-L4208`

### `mono.subst.pattern-structure-preserved` — Pattern substitution preserves pattern shape, substitutes only types

Monomorphizing a pattern reproduces the same pattern kind (Variant/Int/Bool/Wild/Range/VariantData/Or/Tuple/Struct/Slice/At/RefBind/RefPat) with identical structural fields (names, bindings, discriminants, has_rest, is_mut, slots); only the carried binding types are type-substituted. Sub-patterns are recursively substituted.

Source: `src/compiler/mono_clone.cpp#L4227-L4367`

### `mono.subst.stmt-structure-preserved` — Statement substitution preserves statement kind and effect flags

Monomorphizing a statement reproduces the same statement kind with all sub-expressions, sub-blocks, types and side-channel flags re-emitted; child expressions/blocks/patterns/types are recursively substituted while non-type metadata (names, labels, indices, slots) is preserved.

Source: `src/compiler/mono_clone.cpp#L4369-L4744`

### `mono.subst.assign-drop-old-preserved` — Drop-before-replace flag survives monomorphization

For an assignment `x = v` and for deref-write `*p = v`, the drop-before-replace flag (drop old contents iff the place is initialized) is carried verbatim through substitution.

Divergence: B8 (resolved — Rust-conformant)

Related: `mono.drop.typevar-pending-resolve`

Source: `src/compiler/mono_clone.cpp#L4406-L4412`, `src/compiler/mono_clone.cpp#L4536-L4542`

### `mono.subst.foreach-symbolic-len-rederive` — Symbolic-length array iteration re-derives its length after substitution

A `for x in arr` over a symbolic-length array (sema recorded arr_size==0, not a slice) re-derives arr_size from the substituted iterator's concrete array type once the pack/const length is known.

Source: `src/compiler/mono_clone.cpp#L4708-L4724`

### `mono.subst.variadic-param-expand` — A variadic param expands to one concrete param per pack element

A variadic parameter `p: A...` whose type is a TypeVar bound to a type pack of length N expands into N non-variadic params named via the per-index pack-arg naming scheme, each typed by the corresponding pack element; non-variadic params are type-substituted unchanged with name/slot/owning-box-dyn flags preserved.

Divergence: A6 (Logos addition — variadics)

Source: `src/compiler/mono_clone.cpp#L4791-L4818`

### `mono.subst.lifetime-identity` — Lifetime params and outlives bounds pass through mono unchanged

Monomorphization substitutes only type parameters; lifetime parameters and their outlives bounds are not in the substitution map and are copied verbatim onto the cloned signature (lifetime substitution is identity).

Source: `src/compiler/mono_clone.cpp#L4776-L4790`

### `mono.subst.instances-are-monomorphic` — Instantiated functions carry no type-params

An instantiated function instance is monomorphic: it is renamed to its mangled name and carries no generic type-params (clones never emit the template's type_params).

Source: `src/compiler/mono_impl.hpp#L1099-L1112`

### `mono.subst.depth-limit` — Instantiation recursion depth is bounded

Monomorphization recursion is bounded by a maximum instantiation depth; exceeding it (an unbounded recursive instantiation chain) is a compile error rather than nontermination.

Source: `src/compiler/mono_scan.cpp#L618-L623`

### `mono.subst.struct-then-method-tparams` — Generic method type-args split struct-level then method-level

For a call to a method of a generic struct, the call's type-argument vector is the struct-level type-parameters followed by the method-level type-parameters; substitution binds the struct prefix to the struct tparams and the remaining suffix to the method's own tparams in order.

Source: `src/compiler/mono_scan.cpp#L469-L474`, `src/compiler/mono_scan.cpp#L588-L610`

### `mono.subst.impl-target-unification` — Partial-spec impl target is unified to bind impl params

When an impl block's target is a structured pattern (e.g. impl<T,E> for Foo<Vec<T>,E>), the call's positional type-arguments are unified against the impl-target pattern to bind the impl-level type-parameters by name, rather than bound positionally; method-level type-arguments are then layered positionally after the impl-level prefix.

Source: `src/compiler/mono_scan.cpp#L573-L587`, `src/compiler/mono_scan.cpp#L650-L681`

### `mono.subst.variadic-pack-tail` — Trailing variadic type-parameter binds remaining type-args as a pack

If a template's last type-parameter is variadic, the non-variadic leading type-parameters bind positionally and all remaining type-arguments are collected into a pack bound to the variadic parameter.

Source: `src/compiler/mono_scan.cpp#L645-L702`

### `mono.subst.const-pack-typed` — Const variadic-pack elements carry their numeric type

When a variadic type-parameter is a const-pack, each scalar type-argument is wrapped as a ConstVar carrying both the parameter's numeric type and the constant value, so pack expansion can emit a typed integer literal.

Source: `src/compiler/mono_scan.cpp#L682-L701`

### `mono.subst.type-var` — Type/const variable substitution

Monomorphization substitution maps a type position of kind TypeVar or ConstVar to the concrete type bound for its name in the active substitution map; an unbound variable is left unchanged.

Source: `src/compiler/mono_subst.cpp#L28-L32`

### `mono.subst.identity-preservation` — Substitution preserves identity when nothing changes

Substitution is structural and recursive over every type constructor (Array, Ptr/Ref/MutRef, Struct, Enum, Slice, Tuple, Fn/Closure, TraitObject, DstRef, etc.); when no contained component is altered by the map, the original type is returned unchanged rather than a fresh allocation.

Source: `src/compiler/mono_subst.cpp#L62`, `src/compiler/mono_subst.cpp#L185-L187`, `src/compiler/mono_subst.cpp#L199`, `src/compiler/mono_subst.cpp#L232`, `src/compiler/mono_subst.cpp#L339`, `src/compiler/mono_subst.cpp#L360`

### `mono.subst.array-const-size` — Array length from const-generic binding

For an array type [T; N] whose length is a symbolic const-generic name, substitution resolves N through the map: a binding with a concrete const value becomes a literal length; a binding that is still a ConstVar keeps a (renamed) symbolic length.

Source: `src/compiler/mono_subst.cpp#L33-L67`

### `mono.subst.array-sizeof-pack` — Array length sizeof...(P) equals pack arity

An array length written as sizeof...(P) over a type-parameter pack P lowers to a special size marker; at monomorphization its literal length is set to the number of types in the concrete expansion of P.

```logos
[T; sizeof...(P)]
```

Source: `src/compiler/mono_subst.cpp#L36-L48`

### `mono.subst.unsized-slice-to-slice` — ?Sized binding to slice canonicalizes pointer to fat Slice

When a `?Sized` type parameter under a pointer/reference wrapper (&T, &mut T, *const T, *mut T) is bound to an UnsizedSlice<U>, the result is canonicalized to a fat-pointer Slice<U> (the slice fat-pointer ABI).

Source: `src/compiler/mono_subst.cpp#L70-L83`

### `mono.subst.unsized-dyn-to-traitobject` — ?Sized binding to dyn canonicalizes pointer to TraitObject

When a `?Sized` type parameter under a pointer/reference wrapper is bound to an UnsizedDyn (bare `dyn Trait`), the result is canonicalized to a TraitObject carrying the trait name and trait type-args (the dyn fat-pointer form).

Source: `src/compiler/mono_subst.cpp#L84-L90`

### `mono.subst.dst-pointee-to-dstref` — Pointer to DST struct canonicalizes to fat DstRef

A pointer/reference whose pointee substitutes to a struct (or zoned struct) that is a custom-DST — either because the struct template is flagged is_dst, or because this instantiation bound its bare unsized tail type-parameter to an unsized arg (UnsizedDyn, UnsizedSlice, or a bare `dyn` TraitObject of owning-kind Borrow) — is canonicalized to a fat DstRef carrying the struct name, package, mutability, and type-args.

Source: `src/compiler/mono_subst.cpp#L91-L183`

### `mono.subst.self-describing-dst-thin-ptr` — Raw pointer to self-describing DST stays thin

When the pointee struct is `#[self_describing]` (recovers its tail length from an in-band prefix field), a raw `*const Self`/`*mut Self` (kind Ptr) stays a thin 8-byte pointer and is NOT canonicalized to fat DstRef; `&Self`/`&mut Self` still take the fat representation.

Divergence: Logos-specific Writ/RefRepr self-describing-DST contract; no Rust analogue.

Source: `src/compiler/mono_subst.cpp#L164-L174`

### `mono.subst.generic-struct-instantiation` — Generic struct/enum type-arg substitution records instantiation

Substituting a Struct/ZonedStruct/Enum recurses into its type-args; the resulting concrete instantiation is recorded as a needed monomorphization. For enums the need is recorded even when no type-arg changed (e.g. a non-generic use of Option<i32>).

Source: `src/compiler/mono_subst.cpp#L189-L229`

### `mono.subst.tuple-pack-expansion` — Variadic tuple pack expansion

A tuple whose sole element is a TypeVar that maps to (or is a pack name in cur_packs_ of) a concrete tuple/type-list is spliced in place: the wrapper tuple's elements become the recursively-substituted elements of the bound list, so `(A...)` expands to `(T1, T2, ...)`.

Source: `src/compiler/mono_subst.cpp#L298-L331`

### `mono.subst.fn-closure-signature` — Fn/closure signature substitution

Substituting an FnItem, FnPtr, or Closure type recurses into each parameter type and the return type; signatures sharing the closure_params/closure_ret slots (e.g. `fn(T, U) -> V`) substitute their TypeVars accordingly.

Source: `src/compiler/mono_subst.cpp#L344-L365`

### `mono.subst.assoc-type-resolution` — Associated-type projection resolution

An AssocType `<Base as Trait>::Name` is resolved by first substituting Base; once Base is a concrete struct, zoned struct, enum, or scalar primitive, the key Trait::ConcreteBase::Name is looked up in the recorded associated-type impls and the bound type is returned (with nested associated-type chains collapsed fully).

Source: `src/compiler/mono_subst.cpp#L366-L402`, `src/compiler/mono_subst.cpp#L425-L430`

### `mono.subst.assoc-type-blanket-fallback` — Associated-type blanket-impl fallback

When no direct associated-type impl matches, an associated-type projection falls back to a blanket impl `impl<T: Bound> Trait for T` whose required bound (and all extra bounds) the concrete base recursively satisfies; the blanket's associated type is returned with its target type-var bound to the concrete base.

Source: `src/compiler/mono_subst.cpp#L403-L423`

### `mono.subst.cfg-slot-type-projection` — CFG-slot type projection from Writ config

A CfgSlotType `<type:CFG.path>` resolves CFG (a const-generic param or inlined Writ static literal) to a WStaticLit, walks the encoded path (string-keyed 'F', int-keyed 'I' map fields and 'A' array indices joined by 0x1F) through the Writ value, and yields the type named at the terminal Type node (primitive, struct, or enum); if CFG is not yet concrete or any step misses, the projection stays deferred (unchanged).

Divergence: Logos-specific compile-time Writ-config-driven type projection; no Rust analogue.

Source: `src/compiler/mono_subst.cpp#L432-L535`

### `mono.subst.const-array-size` — Substitution resolves symbolic array sizes

Substituting an array type `[T; N]` whose size is a symbolic const-var N: if N maps to a concrete const value, the size becomes that value; if it maps to another ConstVar, the symbolic name is updated.

Source: `src/compiler/sema.cpp#L4495-L4513`

### `mono.subst.ptr-ref-unsized-canonicalize` — Substitution canonicalizes unsized inner types under ptr/ref

When substitution yields an UnsizedSlice inside a `&`/`&mut`/`*const`/`*mut`, it canonicalizes to a slice type (fat pointer); an UnsizedDyn canonicalizes to a trait object. A safe reference drops per-instance lifetime info on the slice (Logos lifetime model is elision-based at this layer).

Source: `src/compiler/sema.cpp#L4514-L4527`, `src/compiler/sema.cpp#L4555-L4572`

### `mono.subst.dst-ref` — Substitution forms DstRef for references to DST structs

Substituting a `&`/`&mut`/`*const`/`*mut` to an effective DST struct yields a wide DstRef (carrying the struct name, package, mutability, and type-args) — UNLESS the struct is `#[self_describing]`, in which case a raw pointer stays thin (8B), its tail length recoverable in-band at deref.

Source: `src/compiler/sema.cpp#L4528-L4551`, `src/compiler/sema.cpp#L4573-L4584`

### `mono.subst.preserves-pkg-qualification` — Substitution preserves package qualification

Substituting a struct or enum type preserves its package qualification (pkg_name) on the result.

Source: `src/compiler/sema.cpp#L4588-L4637`

### `mono.subst.variadic-tuple-splice` — Variadic-tuple pack expansion

A single-element tuple whose sole element is a pack type-var `(A...)` splices in the elements of the concrete tuple A maps to during substitution, yielding the full concrete tuple.

Divergence: Variadic tuples are a Logos addition not present in Rust.

Source: `src/compiler/sema.cpp#L4639-L4659`

### `mono.subst.fn-identity-preserved` — Substitution preserves Closure/FnItem/FnPtr kind and FnItem identity

Substituting a callable type preserves its kind (Closure vs FnItem vs FnPtr) and substitutes parameter and return types; a FnItem additionally preserves its identity (struct_name + type_args), substituting the type_args.

Source: `src/compiler/sema.cpp#L4722-L4750`

### `mono.subst.assoc-type-projection` — Associated-type projection resolution during substitution

Substituting an associated-type projection `Base::Assoc` first substitutes Base; if Base becomes a concrete type, the projection resolves via the impl's `type Assoc = ...` (direct keyed lookup, then generic base-name lookup, then a blanket-impl whose bound is satisfied by the concrete type), substituting impl and GAT parameters; otherwise it remains a projection with substituted base, GAT args, and lifetime args.

Source: `src/compiler/sema.cpp#L4816-L4959`

### `mono.subst.assoc-type-typevar-blanket` — Projection on a still-typevar base reduces via a satisfied blanket impl

A projection `K::Assoc` where K remains a type-var reduces when K's recorded bounds include a trait for which a blanket `impl<DT: BoundTrait> Trait for DT` exists (with all extra bounds also among K's bounds); the result substitutes the blanket's target typevar with K (kept as a type-var).

Source: `src/compiler/sema.cpp#L4849-L4886`

## Unification

### `mono.unify.deep-impl-target` — Deep unification of impl-target pattern against concrete receiver

Deep unification binds impl-level type-params from a partial-spec impl target (e.g. Result<Vec<T>, E>) against a concrete receiver (Result<Vec<i32>, i32>): TypeVar/ConstVar bind by name (re-binding requires type-equality); kinds must match; Ptr requires equal mutability; Array requires equal size; Struct/ZonedStruct and Enum require equal name and equal arity then unify type-args positionally; Tuple unifies elements positionally; Slice unifies element. Returns false on structural mismatch or conflicting bindings; succeeds with partial binding when the concrete side itself carries TypeVars.

Source: `src/compiler/mono_impl.hpp#L949-L1007`

### `mono.unify.collect-pattern-typevars-first-order` — Impl-target free type-vars recovered in first-appearance order

The free TypeVar/ConstVar names of an impl-target pattern are collected in first-appearance (deduplicated) order, mirroring the unify traversal (pointee, element, type-args, tuple-elems). This recovers impl-level type-param names (e.g. T in impl<T> Pin<&T>) for binding a call's type-args when the method's own type-param list is empty.

Source: `src/compiler/mono_impl.hpp#L1009-L1027`

## Associated Items

### `mono.assoc.suffixed-projection-resolution` — Associated types resolve per trait type-args when a type has multiple impls of one trait

When a type has multiple impls of one parameterized trait at distinct type-args (e.g. two `Trait<T>` impls), an associated-type projection `<P as Trait<i64>>::A` resolves to the impl matching the trait's concrete type-args (via a type-args suffix key); a bare projection resolves first-wins.

Divergence: G156-1: addresses two same-trait impls at distinct type-args; tracked as a known narrow area.

Source: `src/compiler/mono.cpp#L251-L277`

### `const.assoc.inherent-and-trait-assoc-const-access` — `Type::CONST` accesses inherent then trait associated constants

When no function symbol resolves, `Type::NAME` is treated as an associated-constant access: inherent assoc-consts (`impl S { const C }`) are tried first (keyed per type-name, turbofish-independent), then assoc-consts from each trait implemented by the type. The const value is lowered once and cached (retyped to its declared type).

Source: `src/compiler/sema_expr.cpp#L13330-L13362`

## Tuple/Variadic Packs

### `mono.pack.type-pack-expansion-args` — Type-pack expansion in call arguments

A pack-expansion call argument over a type-pack variable expands at monomorphization to one argument per bound pack element, each a per-element variable reference into the callee signature; when the callee is a generic template, the pack's element types are also appended to the call's type arguments.

Source: `src/compiler/mono_clone.cpp#L2871-L2904`

### `mono.pack.const-pack-expansion-args` — Const-pack expansion in call arguments

A pack-expansion call argument over a const-pack variable expands at monomorphization to one literal-int argument per bound element, each carrying the pack element's underlying numeric type.

Source: `src/compiler/mono_clone.cpp#L2877-L2899`

## Template Cloning

### `mono.clone.instance-is-monomorphic` — An instantiated function carries no type parameters

A function produced by instantiation is monomorphic: its type-parameter list is empty; signature flags (extern, vararg, local_count) and method-base/package are preserved, and return/param types are type-substituted.

Source: `src/compiler/mono_clone.cpp#L4759-L4846`

### `mono.clone.binary-vs-lazy-origin` — Clone origin: binary-module flag dropped, lazy-module flag propagated

An instantiated function is never marked from-binary-module (instances are new, absent from the binary archive); the from-lazy-module flag IS propagated so the clone's body is subject to the same reach-based emit filtering as lazy-archive originals.

Source: `src/compiler/mono_clone.cpp#L4766-L4774`

## Const-argument Specialization

### `mono.const-arg.literal-substitution` — Const generic param baked to literal in spec clone

When a value parameter is specialized to a compile-time const argument, every read of that parameter in the monomorphized body is replaced by a literal of the parameter's substituted type: an integer literal for scalar const args, or an enum literal (enum_name, variant, discriminant) for enum-typed const args.

Related: `mono.subst.var-ref`

Source: `src/compiler/mono_clone.cpp#L564-L577`

### `mono.const-arg.atomic-ordering-positions` — Atomic intrinsic ordering operands are const-read

The atomic intrinsics const-evaluate their memory-ordering operand(s) at the following 0-based arg positions: load{32,64}_ord at {1}; store/swap/fetch_{add,or,and,xor}{32,64}_ord at {2}; cas{32,64}_ord at {3,4}. These positions must be supplied a compile-time-constant ordering value to receive the corresponding ordering semantics; otherwise a seq_cst fallback applies.

Uncertainty: Positions are the seed registry of which intrinsic operands the backend name-keys; the seq_cst fallback is documented in the unit header comment (L7-L9) not enforced here.

Source: `src/compiler/mono_const_arg.cpp#L23-L36`

### `mono.const-arg.literal-arg-class` — Const-specializable argument values

Only an integer literal or an enum literal (with statically known discriminant) counts as a compile-time-constant argument eligible for const-arg specialization; any other (runtime) argument value yields no specialization.

Source: `src/compiler/mono_const_arg.cpp#L202-L221`, `src/compiler/mono_const_arg.cpp#L177`

### `mono.const-arg.transparent-forward-peel` — Value-transparent wrapper peeling for const forwarding

When determining whether an argument is a bare forward of a parameter or a constant, the following value-transparent wrappers are peeled through: `as`-casts, unary operators, and dereferences. An argument seen as a parameter VarRef through these wrappers is treated as forwarding that parameter to the callee position.

Source: `src/compiler/mono_const_arg.cpp#L51-L66`, `src/compiler/mono_const_arg.cpp#L104-L108`, `src/compiler/mono_const_arg.cpp#L202-L203`

### `mono.const-arg.transitive-want-propagation` — Transitive const-want through forwarding calls

A function parameter is const-want (worth baking into a specialized clone) iff its value, possibly through wrapper-peeled forwards, reaches a callee argument position that is itself const-want — transitively closed from the seed atomic intrinsics. A function with no body (extern/bodyless/unknown) contributes no const-want.

Source: `src/compiler/mono_const_arg.cpp#L68-L150`, `src/compiler/mono_const_arg.cpp#L101-L109`, `src/compiler/mono_const_arg.cpp#L78-L79`

### `mono.const-arg.recursion-guard` — Const-want recursion guard

While computing the const-want set of a function, that function is treated as having no const-want, so recursive/mutually-recursive call cycles terminate (conservatively yielding no specialization rather than diverging).

Source: `src/compiler/mono_const_arg.cpp#L72-L73`, `src/compiler/mono_const_arg.cpp#L148`

### `mono.const-arg.spec-clone-emission` — Per-value specialized clone emission and naming

At a call site, if at least one const-want parameter receives a compile-time-constant argument, a specialized clone of the callee is emitted with those parameters bound to their constant values; the call is rewritten to the clone. The clone is named by appending `__cv` plus, per bound param, `_<pos>_<enum-name|i><ival>`; identical specializations are emitted once (deduplicated by name).

Source: `src/compiler/mono_const_arg.cpp#L157-L196`, `src/compiler/mono_const_arg.cpp#L171-L182`, `src/compiler/mono_const_arg.cpp#L185-L194`

### `mono.const-arg.intrinsic-not-spec-target` — Seed intrinsics are never specialization targets

A function that is itself a seed atomic intrinsic is never const-specialized (its const operand is already a literal at the call site, and renaming it would hide it from the backend's name-keyed atomic lowering).

Source: `src/compiler/mono_const_arg.cpp#L160-L162`

### `mono.const-arg.method-receiver-self-mapping` — Const-arg index identity across method-to-call rewrite

For a method call rewritten to a free call, the receiver is args[0] and self is params[0]; a const-want parameter index therefore maps to the same argument index with no offset, so const-arg specialization applies uniformly to free calls and method calls.

Source: `src/compiler/mono_const_arg.cpp#L152-L156`, `src/compiler/mono_const_arg.cpp#L173-L178`

## Const Specialization

### `mono.const.const-arg-specialization` — Compile-time-constant call arguments specialize the callee

When a call-site argument forwarding (directly or transitively) to a const-evaluating intrinsic position (e.g. an atomic `Ordering`) is a compile-time literal, the callee is specialized with that constant baked in: each use of the parameter is replaced by the literal (an IntLit for integers, or an EnumLit `(enum_name, variant, discriminant)` for enums).

Divergence: Logos const-generic-like specialization driven by const-eval reachability; see explicit-metacall comptime model.

Source: `src/compiler/mono_impl.hpp#L368-L401`

## Array Literals

### `mono.arr-lit.pack-expand` — Array-literal pack expansion

An array element that is a parameter-pack expansion is monomorphized by expanding it to one element per pack member: a const pack yields per-member integer literals; a type/value pack yields per-member VarRefs named by the pack arg naming scheme.

Source: `src/compiler/mono_clone.cpp#L794-L832`

### `mono.arr-lit.sizeof-fill` — Fill-array `[v; sizeof...(P)]` repeats value N times

A single-element fill array literal whose array length type variable substitutes to N>1 is monomorphized by re-substituting the source value expression N times so the resulting array has N elements.

Source: `src/compiler/mono_clone.cpp#L833-L843`

## Integer Literals

### `const.intlit.method-arg-range-check` — Integer-literal method args range-checked against param type

An integer-literal argument (including elements of array/tuple literals, recursively through nested arrays and tuples) passed to a method parameter of a concrete integer type must fit that type; an out-of-range value is an error.

Source: `src/compiler/sema_expr.cpp#L7541-L7609`

### `const.intlit.fold-unary-neg` — Constant integer-literal value extraction through block/negation

A constant integer value is extracted from an expression by: unwrapping a block expression to its result subexpression; folding a unary `-` applied to a constant integer to its negation; and reading the value of an integer literal. Any other expression form has no constant integer value.

Source: `src/compiler/sema_impl.hpp#L4524-L4542`

## Literals

### `const.literal.float-needs-decimal-point` — float CTFE literal must be syntactically distinguishable from int

A floating-point constant value rendered to source must contain a decimal point, exponent, or nan/inf marker so it lexes as a float literal and not an integer literal; otherwise `.0` is appended. F32 values carry an `f32` suffix and F64 a `f64` suffix.

Source: `src/compiler/sema_render.cpp#L978-L991`

### `const.literal.string-escapes` — string constant escape set

A string constant value renders with the escapes `\\`, `\"`, `\n`, `\r`, `\t`; all other bytes are emitted verbatim within double quotes.

Source: `src/compiler/sema_render.cpp#L963-L977`

### `const.literal.integer-suffix-by-kind` — integer constant suffix by type kind

An integer constant carries a type suffix matching its kind (i8/i16/i32/i64/u8/u16/u32/u64); IntLit and the non-power-of-two-byte kinds I24/U24/I56/U56 are emitted unsuffixed. Signedness is determined by the kind (signed: i8/i16/i24/i32/i56/i64/i128/IntLit).

Divergence: Logos has additional integer widths I24/U24/I56/U56 beyond Rust's fixed set.

Source: `src/compiler/sema_render.cpp#L992-L1016`

### `const.lit.int-suffix-kind` — Integer literal kind from suffix

An integer literal must be well-formed; its CTFE kind is its explicit integer suffix kind (e.g. i32, u64) if present, else the polymorphic IntLit. Its value is parsed as a 64-bit integer (both signed and unsigned views retained).

Source: `src/compiler/ctfe.cpp#L68-L81`

### `const.lit.float-suffix-kind` — Float literal kind from suffix; underscores stripped

A float literal must be well-formed; underscore separators are removed before parsing. Its CTFE kind is its explicit float suffix kind (f32/f64) if present, else the polymorphic FloatLit.

Source: `src/compiler/ctfe.cpp#L83-L97`

### `const.lit.bool-value` — Bool literal value

A bool literal evaluates to kind Bool; its value is true iff its stored byte is nonzero.

Source: `src/compiler/ctfe.cpp#L99-L107`

### `const.lit.str-as-slice` — String literal CTFE representation

A string literal evaluates with kind Slice (a stand-in for str / Slice<u8>); the literal's actual &str/Slice<u8> typeref is validated separately by the caller.

Uncertainty: Slice kind is a CTFE-internal stand-in; the actual surface type check happens outside this unit.

Source: `src/compiler/ctfe.cpp#L109-L118`

## Const Evaluation

### `const.eval.expr-forms` — CTFE-evaluable expression forms

Compile-time constant evaluation accepts exactly: integer/float/bool/string literals, parenthesized expressions (transparent), unary expressions, binary expressions, and bare identifier references (VAR_REF) resolvable to a const. Any other expression form is not a compile-time constant and is rejected.

Source: `src/compiler/ctfe.cpp#L249-L271`

### `const.eval.paren-transparent` — Parenthesized expression is transparent

A parenthesized expression evaluates to its inner expression's CTFE value (no effect on kind or value).

Source: `src/compiler/ctfe.cpp#L255`

### `const.eval.path-to-const` — Identifier resolves to const RHS via resolver

A bare identifier in const-eval position (VAR_REF) is resolved, when a resolver is supplied, to the named const's RHS expression and its owning holder, then evaluated recursively; cross-package consts resolve provided the resolver knows them. Without a resolver, or if unresolved, the identifier is not a compile-time constant.

Source: `src/compiler/ctfe.cpp#L258-L271`

### `const.eval.bare-ident-const-fold` — CTFE folds bare-ident const paths to their value

Compile-time const evaluation resolves a bare-identifier const path (e.g. inside `metacall { N }`) to its registered right-hand-side value.

Source: `src/compiler/sema.cpp#L4320-L4341`

## Symbol Mangling

### `mono.mangle.generic-enum-concrete-name` — Generic enum concrete name mangling

A generic enum instantiation's concrete name is `EnumName` followed by `__<mangle(arg)>` for each type argument, where struct/datatype args mangle via concrete-struct-name and others via their type string.

Source: `src/compiler/mlir_gen_types.cpp#L876-L884`

### `mono.mangle.generic-call-callee` — Instantiated generic call mangles callee from type arguments

After a generic call is instantiated with non-empty type arguments and not rewritten as a struct-method, its callee symbol is the base name mangled together with its type arguments.

Source: `src/compiler/mono_clone.cpp#L3374-L3463`

### `mono.mangle.concrete-receiver-cname` — Concrete receiver name from struct/enum/pointer kind

The concrete receiver class name for symbol keying is: the concrete struct name for Struct/ZonedStruct; `<enum>__<arg1>__<arg2>...` (each arg mangled) for Enum; for a pointer/reference receiver, the pointee's struct/enum name, else the pointer's own type string if a `<ptr>__<method>` symbol exists, else the pointee's type string.

Source: `src/compiler/mono_clone.cpp#L3571-L3610`, `src/compiler/mono_clone.cpp#L3579-L3589`

### `mono.mangle.ptr-ref-prefixes` — Pointer/reference mangle prefixes

Type mangling encodes indirection by prefix on the recursively-mangled pointee: const raw pointer -> 'pcst_', mut raw pointer -> 'pmut_', shared reference -> 'ref_', mutable reference -> 'refmut_'. Mutability of raw pointers is part of the mangled identity.

Source: `src/compiler/mono_impl.hpp#L692-L695`

### `mono.mangle.array-size-and-elem` — Array mangle carries length and element

An array type [T; N] mangles as 'arr<N>_' followed by the mangled element type; distinct N yield distinct symbols.

Source: `src/compiler/mono_impl.hpp#L696-L697`

### `mono.mangle.enum-type-args-recursive` — Generic enum mangle includes inner type-args

A generic enum mangles as '<enum-name><module-suffix>' with each type-arg appended as '__<mangled-arg>', so nested instances (Option<Option<i32>>) get distinct symbols and payload-layout lookup agrees with the instantiation queue. If any type-arg contains a TypeVar at any depth, mangling falls back to the bare name+module-suffix (no '__' args), avoiding leaking 'T' into a spec name.

Source: `src/compiler/mono_impl.hpp#L704-L728`

### `mono.mangle.const-arg-value` — Const-generic argument mangled by value

A const-generic argument (IntLit or ConstVar with a bound const value v) mangles as 'cN_<v>', with negatives as 'cN_n<|v|>', so distinct values yield distinct symbol names. A const-var without a bound value falls back to its type string.

Source: `src/compiler/mono_impl.hpp#L729-L739`

### `mono.mangle.owning-vs-borrowed-dyn` — Owning Box<dyn T> mangles distinctly from borrowed &dyn T

> NOTE: id extracted from 2 sources (DIVERGENT STATEMENTS across sources — both surfaced below; reconcile).

Statement (1): A trait object that is OWNING (Box<dyn T>) mangles as 'owndyn_<trait-name>' plus '__<arg>' per type-arg, while a borrowed &dyn T keeps the plain type-string mangling. This keeps Vec<Box<dyn T>> and Vec<&dyn T> as DISTINCT specs so the owning bit is not dropped (which would skip element drop and leak).

Statement (2): An owning TraitObject mangles to owndyn_<trait>$<args...> while a borrowed &dyn T keeps the type_str form, so e.g. Vec<Box<dyn T>> and Vec<&dyn T> get distinct generic spec names (preserving owning-ness / drop). FnItem and FnPtr mangle identically (FnItem ZST auto-coerces to FnPtr).

Divergence: Internal mangling distinction with no Rust analog; reflects owning-dyn vs borrowed-dyn repr split.

Source: `src/compiler/mono_impl.hpp#L740-L751`, `src/compiler/sema.cpp#L1489-L1512`

### `mono.mangle.name-plus-type-args` — Generic symbol = base name plus mangled type-args

A monomorphic symbol name is formed from a base name with each type argument appended as '__<mangled-type>' in declaration order.

Source: `src/compiler/mono_impl.hpp#L757-L765`

### `mono.mangle.concrete-struct-name` — Concrete struct mangled name = base[+module-suffix][+$G<arity>$args]

A Struct/ZonedStruct's mangled name is its struct_name, plus the owning-package module suffix, plus (when type_args non-empty) '$G' + arity + '$'-joined recursively mangled args. Type-arg-less structs mangle to base+suffix.

Source: `src/compiler/sema.cpp#L1417-L1444`

### `mono.mangle.type-args-recursive` — Type-name mangling is structural and recursive per kind

mangle_type_for_name maps each type kind to a stable identifier: Ptr to pmut_/pcst_+pointee, Ref to ref_, MutRef to refmut_, Array to arr<N>_elem, Tuple to tup$<n>$elems, Slice to slice_, UnsizedSlice to uslice_, UnsizedDyn to udyn_<trait>, DstRef to dstref_/dstmutref_+struct, AssocType to base::name, WStaticLit to hs_<hex64 of const_val>; null to 'null'. Identical-byte WritStatic literals share the hs_ suffix.

Source: `src/compiler/sema.cpp#L1446-L1516`

### `mono.mangle.fn-signature-key` — Function signature key = base + '__'-joined mangled param types

A function's signature key is base_name followed by '__'+canonical param-type name for each param, '__void' when no params, and '__vararg' when vararg. Within a fn-ptr-impl method ($fnptr$N base), fn-pointer params erase to arity-only '$fnptr$<n>' so the symbol is stable across the impl's type-vars.

Source: `src/compiler/sema.cpp#L1518-L1541`

### `mono.mangle.trait-typearg-disambiguation` — Distinct trait type-args coexist via mangled method keys

Two impls of the same trait name for one type at distinct trait type-args (`impl Trait<u64> for X` vs `impl Trait<u32> for X`) mangle their methods by the impl's concrete trait type-args so the methods coexist and dispatch independently.

Source: `src/compiler/sema_impl.hpp#L2545-L2549`

### `mono.mangle.trait-qualified-on-collision` — Colliding trait methods re-keyed under trait-qualified name

When two traits define the same method on the same target type and signature, the colliding methods are re-keyed under the trait-qualified base `<target>__<trait>__<method>` to disambiguate.

Source: `src/compiler/sema_impl.hpp#L2537-L2544`

## Symbols

### `mono.symbol.module-qualify-package` — Synthesised symbols are module-qualified by owning package

A symbol synthesised from a type/method belonging to package `pkg` is mangled as `<module_id>.<pkg>` when `pkg` belongs to a non-global module, and as `pkg` unchanged otherwise. This definition-side mangle is identical to the one applied at call sites, so a method-call symbol resolves to the emitted definition.

Source: `src/compiler/mono_impl.hpp#L106-L116`

## Type UIDs

### `mono.uid.module-fingerprint-tags` — Runtime type UID includes per-module fingerprint tags

The canonical type-identity string for runtime UID hashing (type_id::<T>(), Any/downcast/quote_ty) is the type-string PLUS a '|<name>$M<module_id>' tag for EVERY non-stdlib nominal node anywhere in the type tree (recursing through pointee/elem/type-args/tuple-elems/closure params+ret), so two modules' same-named pkg::Type (incl. nested, e.g. Box<pkg::Widget>) hash to DISTINCT UIDs. stdlib (logos.*) and no-module compiles contribute no tags, yielding a string byte-identical to the plain type-string (UIDs unchanged).

Divergence: A9 — Logos coexistence of same-named types across modules; no Rust crate-disjointness analog.

Source: `src/compiler/mono_impl.hpp#L772-L808`

## Cross-module Coexistence

### `mono.coexist.module-qualified-instances` — Type-keyed instance names are module-qualified for cross-module coexistence

Monomorphized type-keyed symbols (e.g. blanket-method instances `Concrete__method`) are module-qualified by the concrete type's package so that two modules' instances of the same-named type do not collide at link time.

Source: `src/compiler/mono.cpp#L46-L67`, `src/compiler/mono.cpp#L135-L136`

### `mono.coexist.package-qualified-instance-key` — Same-named structs from distinct packages are monomorphized independently

A concrete struct instance is keyed by `pkg.bare` when the type carries a package name, else by `bare`. Two structs with the same bare name from different packages (e.g. user `Box<i64>` vs `std.mem.box.Box<i64>`) receive distinct keys and are both instantiated in one compilation.

Related: `mono.lookup.bare-fallback-package-bounded`

Source: `src/compiler/mono_impl.hpp#L576-L588`

## Lookup

### `mono.lookup.bare-fallback-package-bounded` — Bare-name template fallback must not cross package boundaries

When resolving a package-qualified name `pkg.base` to a generic template, a bare-name `base` fallback is accepted only if the matched template's owning package equals `pkg`. A package-qualified non-generic struct never resolves to a same-named generic struct in a different package.

Source: `src/compiler/mono_impl.hpp#L243-L268`, `src/compiler/mono_impl.hpp#L249-L264`

## Dispatch Resolution

### `mono.dispatch.generic-impl-entries` — Dispatch entries synthesized for trait impls over generic structs

For `impl<T> Trait for GenericStruct<T>`, a tag-dispatch entry is emitted for each monomorphized instance of the struct (those with a nonzero type code) and each trait method that was actually cloned onto that instance, keyed by (tag_system, trait, method, type_code). Entries duplicating ones already emitted for concrete specializations are skipped.

Source: `src/compiler/mono.cpp#L912-L975`

### `mono.dispatch.requires-cloned-method` — Dispatch entry emitted only when its target method body exists

A dispatch entry is emitted only if the implementing method symbol was actually monomorphized (present as a cloned method on the struct or as an emitted function); the entry records the actual pkg-qualified, signature-suffixed symbol. If no such symbol exists, no entry is emitted.

Source: `src/compiler/mono.cpp#L944-L953`, `src/compiler/mono.cpp#L962-L970`

### `mono.dispatch.trait-static-callee-rewrite` — Generic trait-static dispatch rewrites the receiver-type prefix

A call whose mangled callee is `[pkg.]<DT>__<method...>` where DT is a generic type parameter bound by the substitution map is rewritten to dispatch on the concrete bound type: the `<DT>` prefix is replaced by the concrete receiver name and the substituted type's package (falling back to the original callee package). The concrete name is the concrete struct name for a struct, the mangled enum name `<base>__<arg>...` for an enum, `$ref_`/`$mut_ref_` prefixed for a `&T`/`&mut T` receiver, and the printed type name otherwise; a `&[u8]` receiver name is normalized to `str`.

Source: `src/compiler/mono_clone.cpp#L2909-L2967`

### `mono.dispatch.method-tparam-inference` — Inferring method type-args at trait-static dispatch

When a rewritten trait-static call carries no explicit type arguments but the target method has method-level type parameters, the call is re-formed into a template callee and supplied inferred type arguments so the correct specialization is instantiated; without this a `.collect::<C>()`-style call would lower to an un-parameterized callee that is never emitted.

Source: `src/compiler/mono_clone.cpp#L2969-L3049`

### `mono.dispatch.assoc-type-projection-disambig` — Associated-type projection disambiguates multi-impl trait-static dispatch

When trait-static dispatch on `Self::method` is ambiguous among multiple trait-qualified candidates `<Self>__<Trait>$G..__<m>` distinguished only by a trait type-argument that is not a method parameter but an associated type of an argument, the correct candidate is selected by associated-type projection: for each argument type, find non-blanket impls whose target unifies with it, substitute the impl's type-params, and mangle the impl's trait type-args; pick the unique candidate whose `$G..` token an argument carries. A re-instantiable template is preferred over an already-monomorphized spec sharing the same token.

Uncertainty: Selection mechanism inferred from candidate-gathering and token-matching logic; only the disambiguation start is within this unit (completion continues past L3172).

Source: `src/compiler/mono_clone.cpp#L3050-L3172`

### `mono.dispatch.multi-impl-arg-disambig` — Multi-impl trait method disambiguated by argument-type signature

When a multi-parameter trait `Trait<A> for Self` has several impls for the SAME `Self` differing only in `A` (e.g. `impl Sum<i32> for i32` and `impl Sum<&i32> for i32`), a call is resolved to the impl whose parameter signature matches the call's concrete argument types: among candidate symbols `<Self>__<m>__[fg]__<sig>`, the one whose `<sig>` equals the mangled concatenation of the call's argument types is selected. This override applies only when more than one candidate matches; single-impl dispatch is unchanged.

Source: `src/compiler/mono_clone.cpp#L3183-L3227`

### `mono.dispatch.receiver-only-spec-guard` — Receiver-concretization only when no method-level type-arg tail remains

A generic struct-method call is rewritten to a concrete-struct callee (`<concrete-struct>__<method>`) with cleared type_args ONLY when every type argument is concrete (no TypeVar) AND the type-argument count does not exceed the impl-level type-parameter count (no method-level tail). If a method-level tail remains, the bare-template callee plus full type_args is retained so a single full specialization can be driven.

Source: `src/compiler/mono_clone.cpp#L3375-L3458`, `src/compiler/mono_clone.cpp#L3396-L3399`

### `mono.dispatch.dyn-receiver-vtable-retarget` — Generic receiver monomorphized to a trait object dispatches through the vtable

A generic method call whose receiver, after substitution, becomes a (possibly reference-wrapped) trait object / unsized-dyn with a known trait — and that carries no resolved symbol, tag, or vtable index — is re-typed to a bare TraitObject and dispatched virtually: the vtable slot is the method's index in the trait's declared method order, and the call is emitted as a dynamic method call. Gated on an unset vtable index (24-bit sign-bit set) with empty resolved symbol/tag so an already-resolved dispatch is never disturbed.

Source: `src/compiler/mono_clone.cpp#L3486-L3538`, `src/compiler/mono_clone.cpp#L3499-L3522`

### `mono.dispatch.projection-receiver-retarget` — TypeVar / assoc-projection receiver retargeted to concrete symbol after subst

A method call whose original receiver (after peeling one pointer/reference layer) is a TypeVar or associated-type projection is retargeted to a concrete callee symbol only when substitution makes the receiver concrete (kind is not TypeVar/AssocType); an unresolved projection is left as-is.

Source: `src/compiler/mono_clone.cpp#L3539-L3570`

### `mono.dispatch.tuple-receiver-sentinel` — Tuple receiver dispatches via $tuple$N sentinel keys

For a (possibly `&`/`&mut`) tuple receiver, method dispatch prefers the concrete sentinel key `$tuple$N$<t1>$...` (N = arity, each element type-string), falling back to the generic `$tuple$N` blanket, then `$tuple$variadic`, choosing the first whose `<key>__<method>` symbol exists.

Source: `src/compiler/mono_clone.cpp#L3611-L3657`

### `mono.dispatch.fnptr-receiver-sentinel` — Function-pointer receiver dispatches via $fnptr$N sentinel key

For a (possibly `&`/`&mut`) function-value receiver, a method from `impl<...> Trait for fn(...)->R` is dispatched via the arity-keyed sentinel `$fnptr$N` (N = parameter count) when a `$fnptr$N__<method>` symbol exists.

Source: `src/compiler/mono_clone.cpp#L3658-L3684`

### `mono.dispatch.slice-receiver-is-str` — &[u8] receiver class name maps to str

A receiver whose computed class name is `&[u8]` is keyed as `str` for method dispatch.

Source: `src/compiler/mono_clone.cpp#L3685-L3686`

### `mono.dispatch.ref-impl-symbol-preference` — Reference receiver prefers an `impl Trait for &C` symbol when it exists

A `&`/`&mut` receiver over a concrete type whose peeled class name is `<C>` is keyed to the ref-impl symbol (`$ref_<C>__m` / `$mut_ref_<C>__m`) instead of the plain `<C>__m` when the plain symbol is absent but the ref-impl symbol exists; otherwise the plain symbol is used (a plain `&Struct` call without a ref-impl is unaffected).

Source: `src/compiler/mono_clone.cpp#L3687-L3699`

### `mono.dispatch.trait-qualified-mangling` — Ambiguous-by-name dispatch resolves to trait-qualified symbol

When a method call on a type-variable receiver is dispatch-ambiguous by name and a trait was selected (trait T), monomorphization resolves the callee base to `<recv-type>__<T>__<method>` if such a symbol exists; otherwise it falls back to the plain `<recv-type>__<method>`.

Divergence: Logos-specific name-mangling scheme; see G156-1 baghunt for two-impl collision

Source: `src/compiler/mono_clone.cpp#L3716-L3737`

### `mono.dispatch.ref-receiver-fallback` — Reference-receiver method falls back to ref-mangled symbol

If a method `<recv>__<method>` does not exist but a reference-typed variant `$ref_<recv>__<method>` (or `$mut_ref_<recv>__<method>` for a mut-ref receiver) does, the call resolves to the reference-typed symbol.

Source: `src/compiler/mono_clone.cpp#L3700-L3715`

### `mono.dispatch.spec-lookup-best-match` — Concrete struct/enum method resolves to best matching spec

For a method call on a concrete struct/enum receiver (after stripping ptr/ref/mut-ref), the candidate template key is `<base>__<method>` (base = resolved type name, else enum/struct name), preferring sema's resolved_symbol when it names an existing template/spec; the call resolves to the best-matching specialization keyed by the receiver's type-args combined with the method's type-args.

Source: `src/compiler/mono_clone.cpp#L4008-L4080`

### `mono.dispatch.combined-type-args` — Specialization key combines receiver and method type-args

Specialization selection for a concrete-receiver method combines the receiver type's type-arguments followed by the method-level type-arguments into a single ordered argument list used for spec lookup and mangling.

Source: `src/compiler/mono_clone.cpp#L4021-L4022`, `src/compiler/mono_clone.cpp#L4053-L4065`

### `mono.dispatch.self-generic-template-mangle` — Self-typed method-generic call mangles resolved template directly

A trait-default body call to another method-generic method on a `Self`-typed generic-struct receiver, where the resolved symbol is a method-generic template (contains `__g__`) with method-level type-params and all call type-args are concrete, is resolved by mangling the resolved template with the call's type-args and enqueuing it; impl-only-generic methods (no method-level type-params) are excluded from this path.

Divergence: Logos-specific monomorphization fixup

Source: `src/compiler/mono_clone.cpp#L4083-L4139`

## Dynamic Dispatch

### `mono.dyn.blanket-instantiated-only-for-coerced-targets` — Blanket impls are instantiated only for actually `dyn`-coerced targets

A blanket impl is not eagerly instantiated for all candidate types; for primitive and generic-struct-instantiation targets, the blanket is cloned only for types that are actually coerced to `dyn Trait` somewhere in the program (collected from `as dyn` casts).

Uncertainty: Restriction motivated by verification soundness (integer-bodied blanket fails on f32/f64); rule states the observable instantiation set.

Source: `src/compiler/mono_impl.hpp#L312-L326`

### `mono.dyn.coerce-instantiates-blanket-impl` — Concrete-to-dyn coercion instantiates the blanket impl for the source type

An unsize coercion of a concrete value to a trait object (&X as &dyn Trait, box X as Box<dyn Trait>) makes the trait's blanket implementation for the concrete source type reachable; the instance is keyed by the trait name and the concrete Self type so the instance name matches the vtable key.

Source: `src/compiler/mono_scan.cpp#L242-L280`

### `mono.dyn.self-derivation-ref-vs-box` — Self type for dyn coercion: pointee for ref/ptr, one Box-unwrap for box value

For a coercion to a trait object the Self type is derived as: for a reference/pointer source, the pointee (no Box unwrapping, so &Box<T> as &dyn keys on Box<T>); for a Box-value source (box X), the boxed type by unwrapping exactly one Box.

Source: `src/compiler/mono_scan.cpp#L249-L264`

## Drop Glue

### `mono.drop.implicit-drop-pinned` — Drop::drop is instantiated even without an explicit call site

For a type with `impl Drop`, the `drop` method is treated as a demanded root and instantiated even though it is invoked only implicitly by drop-glue; otherwise the destructor would silently never run for a generic struct's Drop impl.

Source: `src/compiler/mono.cpp#L873-L883`

### `mono.drop.typevar-pending-resolve` — Generic-param drop resolves at instantiation to the substituted type's drop

A drop statement whose drop-fn is the sentinel `__typevar_pending__drop` (original type was a generic param) is resolved against the substituted concrete type T: if T is Struct/ZonedStruct with a non-empty concrete name, drop-fn = `<concrete_name>__drop` and field-recursion is enabled when any field is droppable (Struct/ZonedStruct/Enum/Tuple/Array/Closure, owning TraitObject/Slice/DstRef); if T has no Drop impl the drop is skipped.

Source: `src/compiler/mono_clone.cpp#L4560-L4621`

### `mono.drop.typevar-pending-owning-dyn` — Generic-param drop of an owning dyn payload routes to dyn drop-in-place

When the substituted concrete type of a `__typevar_pending__drop` is an owning trait object, the move-out drop routes to `__box_dyn__drop`; an owned unsized-dyn tail projection (UnsizedDyn, or borrow-owning TraitObject whose move source is an owned dyn tail) routes to `__dyn_drop_in_place__` (run Drop, do not free); Enum/Tuple/Array/Closure enable field-recursion instead.

Related: `mono.drop.typevar-pending-resolve`

Source: `src/compiler/mono_clone.cpp#L4631-L4672`

### `mono.drop.generic-struct-remangle` — Drop call to a generic-struct template is re-mangled to the monomorphized name

A non-empty drop-fn whose type T is a Struct/ZonedStruct with non-empty type-args is rewritten from the template drop name to `<concrete_struct_name(T)>__drop`, pointing at the monomorphized Drop function emitted for the instance.

Source: `src/compiler/mono_clone.cpp#L4674-L4686`

## Dynamically-sized Types

### `mono.dst.field-prefix-relower` — Custom-DST sized prefix field re-lowered per instantiation

When a field-read receiver monomorphizes to a custom-DST fat pointer (DstRef) and the field is a sized prefix field at byte offset off, the access is re-emitted as a typed deref of (data_half + off) rather than a thin struct GEP.

Uncertainty: Field layout offset computed by mono_dst_prefix_field (defined elsewhere); rule states observable result.

Source: `src/compiler/mono_clone.cpp#L630-L713`, `src/compiler/mono_clone.cpp#L706-L711`

### `mono.dst.field-slice-tail` — Custom-DST unsized slice tail projects to fat slice

For a custom-DST receiver, an unsized slice tail field projects to a slice value `&[U]` whose data pointer is (data_half + off) cast to `*U` and whose length is the receiver fat pointer's metadata half.

Source: `src/compiler/mono_clone.cpp#L693-L705`

### `mono.dst.field-dyn-tail` — Custom-DST unsized dyn tail projects to fat dyn

For a custom-DST receiver, an unsized dyn (UnsizedDyn, or borrow-owned TraitObject) tail field projects to a `&dyn Trait` value whose data pointer is (data_half + off) and whose metadata is the receiver fat pointer's vtable (metadata half), retyped as TraitObject with the field's trait name and type-args carried over.

Source: `src/compiler/mono_clone.cpp#L668-L692`

### `mono.dst.nongeneric-rawptr-canonicalizes-to-dstref` — Pointer to a custom-DST struct canonicalizes to a fat DstRef

A pointer `*mut S`/`*const S` to a struct `S` carrying an unsized (`[u8]`/slice) tail field is canonicalized to a fat DstRef representation, for non-generic as well as generic `S`; it is not left as a thin pointer.

Related: `mono.subst.unsized-slice-to-slice`

Source: `src/compiler/mono_impl.hpp#L152-L168`

## Layout

### `mono.layout.partial-spec-preferred` — Layout resolution prefers the best-matching partial specialization

When resolving the field layout of a struct instance S<A...>, the best-matching partial specialization (e.g. WMap<WString,V> over base WMap<K,V>) is selected and its pattern type-vars bound by matching the concrete type-args against the spec patterns; only if no spec matches is the base template used with positional type-param binding.

Source: `src/compiler/mono_clone.cpp#L327-L346`

## Struct Monomorphization

### `mono.struct.markers-preserved` — Struct repr/zone markers preserved through monomorphization

Cloning a generic struct to a monomorphic instance preserves its markers: zoned, self-describing, rel-ptr, borrow-carrying, zone-mut, zoned2, non-null, union, and lifetime params/outlives; type_params are cleared (result is monomorphic).

Source: `src/compiler/mono_clone.cpp#L5301-L5328`

### `mono.struct.dst-inheritance` — DST-ness inherited when last field instantiates to unsized

A monomorphized struct becomes a custom-DST if, after substitution, its LAST field has an unsized type (UnsizedSlice or UnsizedDyn); the template's own DST flag is otherwise preserved.

Source: `src/compiler/mono_clone.cpp#L5302`, `src/compiler/mono_clone.cpp#L5350-L5361`

### `mono.struct.method-rename` — Struct methods renamed from Base__m to Instance__m on clone

When a generic struct is monomorphized to a new name, each method `Base__method` is renamed to `<new_name>__method`, preserving any package prefix before the bare name.

Source: `src/compiler/mono_clone.cpp#L5286-L5287`, `src/compiler/mono_clone.cpp#L5411-L5426`

### `mono.struct.specialization-override` — Concrete impl method overrides blanket method for that instantiation

If a non-generic function exists whose bare name equals the renamed method name (a user-written concrete `impl Foo<Concrete>` method), the blanket `impl<T> Foo<T>` method is NOT cloned for that instantiation; the concrete definition is used instead.

Source: `src/compiler/mono_clone.cpp#L5428-L5438`

### `mono.struct.structured-self-pattern-skip` — Structured-impl method skipped when its self pattern mismatches

For a method from a structured-self impl (e.g. `impl<T> Pin<&T>` on `struct Pin<P>`), impl-level params are bound by unifying the impl target pattern's args against the concrete struct args; if unification of any decidable arg fails, the method does not belong to this instantiation and is not cloned. Args still containing TypeVars, or projection/associated-type args, are skipped (deferred / not structurally decidable) rather than failing.

Source: `src/compiler/mono_clone.cpp#L5374-L5408`

### `mono.struct.lazy-method-defer` — Generic struct methods cloned lazily on demand

In lazy-methods mode, a generic struct (non-empty type_params) is cloned with an empty METHODS array; its methods are cloned on demand later (call-site / dispatch pin / root pin), with the bound gate applied at that time.

Source: `src/compiler/mono_clone.cpp#L5366-L5371`

## Enum Monomorphization

### `const.enum.discriminant` — Enum discriminant value forms

A variant discriminant `Name = D` may be: a bare (optionally negated) integer literal that is the complete value (no trailing binary operator); `metacall <block>`; a cross-enum reference `OtherEnum::Variant` (with optional `as T` cast whose type is dropped, width governed by the enclosing enum's backing/repr); or a general constant expression evaluated via CTFE. A bare literal alt only matches when no binary operator follows; otherwise the value falls through to the const-expr alternative.

```logos
Green = 5
```
```logos
Lo = -1
```
```logos
Purple = 1 << 1
```
```logos
X = Other::Y as u8
```

Divergence: Cross-enum discriminant reference `OtherEnum::Variant` as a discriminant value has no Rust analog.

Source: `tools/peg_gen/grammars/logos.peg#L788-L812`, `tools/peg_gen/grammars/logos.peg#L760-L763`

### `mono.enum.instance-drops-metadata` — Enum instances carry only name/pkg/zoned2/variants

A monomorphized enum instance preserves its name, package, zoned2 (niche/at-rest-relative) marker, and variants; type_params, backing_type, borrow_carrying, and doc are dropped on the instance.

Source: `src/compiler/mono_clone.cpp#L5812-L5859`

### `mono.enum.variadic-variant-payload` — Variadic enum variant payload expands its pack

An enum variant whose first payload type is a variadic type-var (e.g. `Multi(...T)`) expands to one payload per element of the bound pack, each substituted; a non-pack-bound variadic payload substitutes as a single type.

Source: `src/compiler/mono_clone.cpp#L5829-L5858`

### `mono.enum.void-payload-spec-skip` — Enum instantiation with a void type-arg emits no methods

When a generic enum is instantiated with a `()`/void type-argument (e.g. `Option<()>`, `Result<(),E>`), the enum's variants are still materialized but no impl methods are cloned, since a void-typed bound value carries no slot and cannot be passed or returned.

Uncertainty: Spec-level: such instances are usable only for marker/discriminant matching, not value-carrying method calls.

Source: `src/compiler/mono_clone.cpp#L5905-L5926`

### `mono.enum.self-referential-method-skip` — Eager clone of self-referential enum methods is skipped

A generic enum method is not eagerly cloned when any of its parameter or return types references the same enum base with a type-arg that is structurally larger than a bare type-var AND still contains a type-var (e.g. `Option::as_ref(&Option<T>) -> Option<&T>`); fully-concrete recursive args (`Result<(),Error>`) do not trigger the skip. Real call sites still enqueue the needed specialization.

Source: `src/compiler/mono_clone.cpp#L5954-L5995`, `src/compiler/mono_clone.cpp#L6076`

### `mono.enum.method-bound-gate` — Generic enum method clone is gated by type-param bound satisfaction

An impl-derived generic enum method (e.g. `impl<T: Echo> Echo for Option<T>`) is cloned for a concrete instantiation only if each bounded type-param's concrete binding satisfies its trait bound; auto-traits are checked by auto-satisfaction and ordinary bounds by recursion-aware concrete-satisfies-bound. Failing bounds suppress the clone.

Source: `src/compiler/mono_clone.cpp#L6140-L6186`

### `mono.enum.method-fully-bound-gate` — A blanket method with unbound type-params is not cloned

A generic (blanket free-fn / impl) enum method is cloned only if every non-variadic type-param receives a binding (subst or pack) from this instantiation; if any param remains unbound (e.g. an output `D` in `impl<S, D: From<S>> Into<D> for S`), the clone is suppressed and the correct specialization is emitted by the full call site instead.

Source: `src/compiler/mono_clone.cpp#L6194-L6201`

### `mono.enum.method-partial-spec-target` — Partial-spec impl target binds type-params by unification

When a generic enum method carries an impl_target_pattern that is not the bare enum (e.g. `Result<Vec<T>, E>` vs `Result<T, E>`), its impl-level type-params are bound by unifying the concrete receiver type-args against the pattern, rather than by positional assignment.

Source: `src/compiler/mono_clone.cpp#L6089-L6126`

### `mono.enum.method-dual-mangling` — Generic enum methods are emitted under both cname-insert and type-arg-append names

A generic enum method may be demanded under two mangled forms: the cname-insert form (`Option__i8__eq`, used for direct concrete-receiver calls and `==`/`!=` operator lowering) and the type-arg-append form (`Option__eq__g__sig__i8`, used for trait-bound dispatch). The same body is emitted under whichever of the two names a caller demands but no path has yet produced.

Source: `src/compiler/mono_clone.cpp#L6031-L6075`, `src/compiler/mono_clone.cpp#L6209-L6230`

## Match Lowering

### `mono.match.shallow-name-only` — Shallow pattern match binds vars and compares nominals by name only

Shallow pattern matching of concrete c against pattern p: a TypeVar or ConstVar pattern binds by name (already-bound -> require type-equality with the concrete; unbound -> bind it). Otherwise kinds must be equal; Ptr requires equal mutability and matches pointee; Ref/MutRef match pointee; Array requires equal size and matches element; Struct matches by struct-name ONLY (no type-arg descent); all other kinds fall back to full type-equality.

Source: `src/compiler/mono_impl.hpp#L907-L938`

## Binary Operators

### `const.binop.logical-bool-only` — && and || require bool operands

Binary '&&' and '||' require both operands to be Bool and yield Bool; otherwise rejected.

Source: `src/compiler/ctfe.cpp#L159-L163`

### `const.binop.int-promotion` — Integer kind promotion in binary ops

For an integer binop, the result kind is the promotion of the two operand kinds: equal kinds stay; a polymorphic IntLit yields to the other operand's kind; otherwise the result is I64.

Uncertainty: Mixed concrete-int kinds (e.g. i32 op u32) fall back to I64 rather than a Rust-style type error.

Source: `src/compiler/ctfe.cpp#L51-L57`, `src/compiler/ctfe.cpp#L192-L197`

### `const.binop.int-signedness` — Integer arithmetic/comparison uses result-kind signedness

Integer +,-,*,/,%,comparisons are computed using signed semantics iff the promoted result kind is signed, else unsigned; both signed and unsigned representations of the result are kept.

Source: `src/compiler/ctfe.cpp#L192-L235`

### `const.binop.div-mod-by-zero` — Division/modulo by zero is a CTFE error

Integer '/' or '%' with a zero divisor, and float '/' with a zero divisor, are rejected as compile-time errors (no panic, no UB at CTFE).

Source: `src/compiler/ctfe.cpp#L179-L182`, `src/compiler/ctfe.cpp#L201-L219`

### `const.binop.shift-mask` — Shift amount masked to 6 bits

Integer '<<' and '>>' mask the shift amount to its low 6 bits (& 63). '<<' is computed unsigned; '>>' is arithmetic when the result kind is signed, logical otherwise.

Uncertainty: Mask 63 assumes a 64-bit shift width regardless of operand bit-width (e.g. u8).

Source: `src/compiler/ctfe.cpp#L221-L226`

### `const.binop.float-promotion` — Mixed int/float binop coerces to float

When either operand of an arithmetic/comparison binop is a float, integer operands are coerced to double; result kind is F64 if either side is F64, else F32 if either is F32, else FloatLit.

Source: `src/compiler/ctfe.cpp#L58-L62`, `src/compiler/ctfe.cpp#L166-L190`

### `const.binop.bool-eq` — Bool equality comparisons

Binary '==' and '!=' on two Bool operands compare their values and yield Bool.

Source: `src/compiler/ctfe.cpp#L240-L243`

### `const.binop.div-by-literal-zero` — Division or remainder by literal zero rejected at compile time

/ or % whose right operand is an integer literal 0 is a compile-time error; restricted to untyped IntLit RHS so statically-unreachable guarded divides are not rejected.

Source: `src/compiler/sema_expr.cpp#L2309-L2318`, `src/compiler/sema_expr.cpp#L2335-L2337`

### `const.binop.intlit-fold-overflow` — Integer-literal arithmetic is folded; i64 overflow is rejected

When both arithmetic operands are integer literals with recoverable values, +,-,*,/,% are constant-folded to a single integer literal (of untyped IntLit type); if the fold overflows i64 the expression is rejected rather than silently wrapped.

```logos
2147483647 + 1
```

Divergence: Rust folds in the inferred type; Logos folds in i64 and errors on i64 overflow, deferring per-type fit to the coercion site.

Source: `src/compiler/sema_expr.cpp#L2319-L2355`

### `const.binop.shift-negative-count` — Negative literal shift count rejected

<< or >> whose right operand is a negative integer literal is a compile-time error.

Source: `src/compiler/sema_expr.cpp#L2417-L2423`

### `const.binop.shift-count-overflow-width` — Literal shift count >= LHS bit-width rejected

<< or >> whose shift count is a literal value >= the bit-width of the left operand's type is a compile-time error (shifting by >= width is undefined); widths: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, usize/isize=64.

Divergence: usize/isize fixed at 64-bit (target-specific).

Source: `src/compiler/sema_expr.cpp#L2424-L2453`

## Unary Operators

### `const.unary.neg-numeric` — Unary '-' requires numeric operand

Unary '-' negates an integer or float operand (preserving its kind); applied to any non-numeric operand it is rejected.

Source: `src/compiler/ctfe.cpp#L127-L135`

### `const.unary.not-bool` — Unary '!' requires bool operand

Unary '!' logically negates a Bool operand; applied to any non-bool operand it is rejected. (No bitwise-not on integers in CTFE.)

Uncertainty: Bitwise '!' on integers is not handled here; only bool is accepted.

Source: `src/compiler/ctfe.cpp#L136-L139`

## Operators

### `mono.operator.unary-overload` — Unary operator on struct dispatches to overload method

After substitution, a unary operator whose operand is a struct is rewritten to a call to the struct's operator method: `-` -> `neg`, `!` -> `not`; resolved on the concrete struct name within the struct's package.

Source: `src/compiler/mono_clone.cpp#L1002-L1024`

### `mono.operator.binop-overload` — Binary operator on struct dispatches to overload method

After substitution, a binary operator whose left operand is a struct is rewritten to a two-argument call to the struct's operator method: + add, - sub, * mul, / div, % rem, == eq, != ne, < lt, <= le, > gt, >= ge; resolved on the concrete struct name within the struct's package.

Source: `src/compiler/mono_clone.cpp#L1025-L1058`

## Closure Calls

### `mono.closure-call.fnptr-switch` — Closure call to a substituted fn-value becomes a fn-pointer call

A ClosureCall whose callee monomorphizes to a function-value type (fn pointer / Fn-family bound resolved to a concrete fn value) is rewritten to a direct function-pointer call.

Source: `src/compiler/mono_clone.cpp#L848-L864`

### `mono.closure-call.struct-fn-method` — Closure call to a struct implementing Fn-family routes to its call method

A ClosureCall whose callee monomorphizes to a (zoned) struct that implements an Fn/FnMut/FnOnce trait is rewritten to a method call on that struct: the receiver is prepended as the self argument, and the called method is the first of `call`, `call_mut`, `call_once` for which an impl method exists (defaulting to `call`), resolved on the concrete struct name including type-args.

Source: `src/compiler/mono_clone.cpp#L865-L936`

## Assignment

### `const.assign.intlit-fits` — Integer-literal RHS must fit the place's integer type

An integer-literal RHS (scalar, or each element of an array/tuple literal RHS, recursively through nested array/tuple literals) must fit the corresponding narrow integer place type; a value that does not fit is rejected.

Source: `src/compiler/sema_stmt.cpp#L7374-L7447`

## Let Bindings

### `const.let.intlit-fits-annotation` — Integer literal must fit the annotated type

An integer literal RHS (scalar, or per-element of an array/tuple literal, recursively into nested array/tuple literals) whose value does not fit in the annotated (element) integer type is a compile error.

Source: `src/compiler/sema_stmt.cpp#L2083-L2178`

## Intrinsics

### `mono.intrinsic.type-code-of` — type-code-of resolves concrete struct type code else hashes

TypeCodeOf(T): if substituted T still contains a TypeVar (or is null), it stays a runtime type-code-of node. For a concrete Struct/ZonedStruct, the type code is the registered struct's type_code (or matching inst-annotation's). If no registered code, the code is a 56-bit hash of `type_str(T)` biased into [128, ...] (raw<128 → raw+128).

Uncertainty: Exact hash construction (type_hash_23 → type_hash_56bit) is delegated to helpers outside this unit.

Source: `src/compiler/mono_clone.cpp#L1146-L1183`

### `mono.intrinsic.writ-lit-clone` — Writ literal value tree is deep-cloned with substituted captures

A WritLit is deep-cloned over its Writ value tree (Null/Bool/Int/Float/Str/Capture/Type/Map/Array), preserving map key-type and int-vs-string keying and array elem-type; capture expressions are recursively substituted and capture types are type-substituted. The static_blob is copied before emit (the emit reuses the moved type-pool arena).

Source: `src/compiler/mono_clone.cpp#L1186-L1281`

### `mono.intrinsic.field-types-of-nonstruct-empty` — field-types-of on a non-struct yields an empty type pack

`__field_types_of__` applied to a substituted T that is not a Struct/ZonedStruct yields an empty pack (the [Type;0] result), which resolves variadic instantiation to the 0-arg base overload rather than aborting. Field types of a generic struct are computed by substituting the struct template's type-params with T's type-args.

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1342-L1380`

### `mono.intrinsic.sizeof-pack` — sizeof...(T) evaluates to the post-expansion pack length

`__sizeof_pack__` is replaced by an integer literal equal to the number of concrete type-args after pack expansion.

Source: `src/compiler/mono_clone.cpp#L1411-L1418`

### `mono.intrinsic.type-kind-of` — type-kind-of yields the kind discriminant of substituted T

`__type_kind_of__` is replaced by an integer literal equal to the LogosType kind discriminant of the concrete substituted first type-arg (0 if none).

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1419-L1427`

### `mono.intrinsic.wstatic-hash-of` — wstatic-hash-of yields the byte-hash of CFG

`__wstatic_hash_of__` is replaced by an integer literal equal to the const-value (u64 byte-hash) carried by the substituted first type-arg (a WStaticLit kind); 0 if absent.

Divergence: Logos compile-time-static (Writ) extension.

Source: `src/compiler/mono_clone.cpp#L1428-L1437`

### `mono.intrinsic.type-hash-of` — type-hash-of yields a structural layout-stable hash

`__type_hash_of__` is replaced by an integer literal equal to a structural FNV-1a-64 hash of the substituted T (no struct/field names; recurses into field types), i.e. layout-stable identity.

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1438-L1449`

### `mono.intrinsic.type-name-of` — type-name-of yields the canonical type string of T

`__type_name_of__` is replaced by a string literal equal to the canonical `type_str(T)` of the concrete substituted T (empty if none).

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1450-L1457`

### `mono.intrinsic.type-uid-of` — type-uid-of yields the low 64 bits of the nominal type UID

`__type_uid_of__` is replaced by an integer literal equal to the low 64 bits of the nominal UID (derived from a hash of the canonical type id of substituted T); the mapping uid→type is recorded for later reification. `__type_uid_hi_of__` yields the high 64 bits over the same canonical input, so the halves agree.

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1458-L1481`

### `mono.intrinsic.is-same` — is-same compares two substituted types for equality

`__is_same__` yields a bool literal true iff exactly two type-args are given and they are equal after substitution.

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1490-L1494`

### `mono.intrinsic.type-kind-predicates` — Type-trait predicates evaluate on the substituted type's kind

Each predicate yields a bool literal computed from the kind of the concrete substituted first type-arg: __is_ptr__/ref/mut_ref/struct/zoned/enum/tuple/slice/array/bool test the exact kind; __is_integer__ is true for I8..I128/U8..U128; __is_float__ for F32/F64; __is_signed__ for I8..I128; __is_unsigned__ for U8..U128; __is_primitive__ for bool|float|integer.

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1495-L1528`

### `mono.intrinsic.args-count-of` — args-count-of yields the number of generic type arguments of T

`__args_count_of__` yields an i64 literal equal to the count of T's generic type-args (0 for non-generic/primitive T).

Divergence: Logos reflection extension.

Source: `src/compiler/mono_clone.cpp#L1530-L1543`

### `mono.intrinsic.has-trait` — has-trait resolves a trait implementation at monomorphization time

`__has_trait__` yields a bool literal: it reads the trait name from the call's first string-literal argument and the concrete type from type_args[0], reduces T to a concrete name (concrete_struct_name for Struct/ZonedStruct, enum_name for Enum, else type_str, with any `$G...` generic suffix stripped), and recursively tests trait satisfaction against concrete + blanket impl tables.

Divergence: Logos reflection extension (compile-time trait-satisfaction predicate).

Source: `src/compiler/mono_clone.cpp#L1544-L1584`

### `mono.intrinsic.has-trait-of` — has-trait-of recovers T from a reflected Type value then resolves the trait

`__has_trait_of__(trait, t: Type)` recovers the concrete T from t's StructLit `uid` field (a `__type_uid_of__` call, chasing ≤8 VarRef hops) and then performs the same impl-table recursion as __has_trait__ to yield a bool literal.

Divergence: Logos reflection extension (Type-method form).

Related: `mono.intrinsic.has-trait`, `mono.intrinsic.type-uid-of`

Source: `src/compiler/mono_clone.cpp#L1585-L1592`

## Writ Statics

### `const.wstatic.content-identity` — Writ static literal type-arg identity is content-only

A Writ static literal `@{...}` used at type-argument position is reduced to a `WStaticLit` type whose identity is a position-free content hash of the literal AST (schema-aware FNV-1a over node CODE plus value bytes/string children). Two structurally identical `@{...}` literals at different source positions yield the SAME type; differing content yields distinct types. First-write-wins: the first lowering of a given hash registers the materialising LExpr that mono later substitutes for `__const_param:CFG` references.

Uncertainty: Identity-as-content-hash inferred from the walk + first-write-wins registry; the const_val stores the hash bit-pattern read as u64 by mangling.

Divergence: A6 — Writ is a Logos-only feature; no Rust analogue.

Source: `src/compiler/sema.cpp#L6392-L6499`, `src/compiler/sema.cpp#L6486-L6498`

### `const.wstatic.dup-key-error` — Duplicate keys in a Writ map literal are rejected

Within a Writ map literal (`WRIT_MAP`), two entries with the same key (after stripping surrounding quotes) are an error: "duplicate key '<k>' in Writ map literal". Empty keys are ignored. This applies to map literals at type-argument position, not only `pub const … = @{...}`.

Divergence: A6 — Writ-specific.

Source: `src/compiler/sema.cpp#L6425-L6440`

### `const.wstatic.type-lit-resolves-scope` — Writ type literals resolve type params in current scope

A `@type(T)` (`WRIT_TYPE_LIT`) child resolves its TYPE node with the in-scope type parameters and contributes its canonical `type_str` to the literal's content identity; thus the same syntactic literal under different type-param bindings produces distinct WStaticLit types. A legacy NAME-only shape substitutes the bound type param when present, else uses the bare name.

Divergence: A6 — Writ-specific.

Related: `const.wstatic.content-identity`

Source: `src/compiler/sema.cpp#L6462-L6482`

### `const.wstatic.type-alias-binding` — Non-generic WritStatic const binds as a type alias

A non-generic `const X: WritStatic = @{…};` whose value resolves to a WStaticLit type registers X as a type alias to that WStaticLit, so X usable at type-arg positions with byte-hash identity (replacing the legacy `type X = @{…};` form).

Source: `src/compiler/sema_collect.cpp#L2331-L2384`

### `const.wstatic.generic-per-use-instantiation` — Generic WritStatic const instantiated per use-site

A generic `const X<T1,…>: WritStatic = @{… <type:T1> …}` is recorded as a generic const (its type params and value AST saved) and instantiated per use-site, rather than bound as a single type alias.

Source: `src/compiler/sema_collect.cpp#L2385-L2407`

## cfg Resolution

### `const.cfg.predicate-evaluation` — cfg!() and #[cfg(...)] compile-time predicate evaluation

`cfg!(...)` and `#[cfg(...)]` are evaluated at compile time over the active target metadata and feature set: combinators `all(...)`/`any(...)`/`not(...)` are recursive; built-in keys (target_pointer_width, target_arch, target_os, target_endian, target_family) resolve against compile-target metadata; `feature = "name"` resolves against features supplied via `--cfg`/manifest; a multi-arg attribute list is a conjunction. An item whose cfg predicate is false is dropped.

Uncertainty: Attribute-form combinators (all/any/not) noted as MVP-limited in some helpers; cfg_attr activation precedes cfg evaluation.

Source: `src/compiler/sema_impl.hpp#L710-L738`

## Compile-time Function Evaluation (CTFE)

### `const.ctfe.supported-expr-forms` — CTFE-evaluable expression forms

Compile-time constant evaluation (used for metacall arguments and const-evaluable contexts) accepts only: integer/float/bool/string literals, parenthesized expressions, unary `-`/`!`, binary `+ - * / % << >> & | ^ && || == != < <= > >=`, and bare identifier references to consts. Any other expression form is not a compile-time constant.

Uncertainty: Exact operator semantics/overflow behavior live in ctfe.cpp; this header only enumerates the accepted node set.

Source: `src/compiler/ctfe.hpp#L9-L14`, `src/compiler/ctfe.hpp#L64-L69`

### `const.ctfe.value-kinds` — CTFE value kinds

A compile-time value carries a primitive type tag and is restricted to scalar numeric, Bool, unsuffixed IntLit/FloatLit, or Slice<u8> (string literal) kinds; the static type tag is derived from the literal's suffix (unsuffixed ⇒ IntLit/FloatLit) and from operator type rules, independent of the type-inference pool.

Source: `src/compiler/ctfe.hpp#L5-L7`, `src/compiler/ctfe.hpp#L34-L45`

### `const.ctfe.path-to-const-resolution` — Bare identifier resolves to a const's RHS

A bare identifier in a const-evaluable position resolves by looking up a const item and recursively evaluating its right-hand-side expression. The const may live in another package (resolution returns the owning holder). An identifier that does not resolve to a const-evaluable item is treated as not a compile-time constant.

Related: `const.ctfe.supported-expr-forms`

Source: `src/compiler/ctfe.hpp#L47-L62`, `src/compiler/ctfe.hpp#L64-L69`

### `const.ctfe.non-const-is-error` — Non-constant metacall argument is rejected

An expression in a metacall argument (or other const-evaluable context) that is not reducible to a CTFE value via the supported forms is rejected with a diagnostic stating the argument is not a compile-time constant.

Source: `src/compiler/ctfe.hpp#L12-L14`, `src/compiler/ctfe.hpp#L52-L53`

## Blanket Impls

### `mono.blanket.eager-over-satisfying-types` — Blanket-impl methods instantiated for every concrete type satisfying the bound

For a blanket impl `impl<T: Bound> Trait for T`, the methods are instantiated for each non-generic concrete type that satisfies `Bound` (and all extra bounds, recursively, including chain-satisfaction through other blanket impls). An unbounded blanket `impl<T> Trait for T` instantiates for every non-generic struct and enum in the program.

Uncertainty: Primitives/references are excluded from the eager candidate list; they are instantiated lazily via the dyn-coercion supplementary pass (mono.blanket.dyn-coercion-targets).

Source: `src/compiler/mono.cpp#L396-L446`, `src/compiler/mono.cpp#L511-L555`

### `mono.blanket.assoc-eq-constraint` — Blanket instantiation requires associated-type equality clauses to hold

A blanket impl carrying associated-type equality bounds `Trait<Assoc = U>` is instantiated for a concrete type only if, for every such clause, the type's resolved associated type equals `U`. The associated type is resolved from a direct impl when present, otherwise from a satisfying blanket impl's `type Assoc = ...` definition with the target type-var substituted.

Source: `src/compiler/mono.cpp#L452-L510`, `src/compiler/mono.cpp#L539-L544`

### `mono.blanket.transparent-output-param-deferred` — Blanket methods with unbound (output) type params instantiate at the call site

A blanket method carrying type parameters not bound by the blanket's target type-var (e.g. the output `D` in `impl<S, D: From<S>> Into<D> for S`) is not eagerly instantiated; such methods are left transparent and instantiated at the actual call site with full type arguments.

Source: `src/compiler/mono.cpp#L72-L79`

### `mono.blanket.dyn-coercion-targets` — Blanket methods instantiated for types coerced to dyn Trait

Any concrete type (including primitives and generic struct instantiations) that is coerced to `dyn Trait` by reachable code triggers instantiation of the blanket impl methods of `Trait` for that type, so the vtable slot is populated. Such targets are still bound-filtered: a coerced pointee that does not satisfy the blanket's bound is not instantiated.

Source: `src/compiler/mono.cpp#L656-L716`, `src/compiler/mono.cpp#L684-L696`

## Method

### `mono.method.binary-symbol-stub` — Methods present in prebuilt archive get signature-only stubs

If a method's final (renamed) name is present in the binary-symbol set (body in a prebuilt stdlib archive), only its signature is cloned (no body); code-gen skips body emission and relies on the archive.

Source: `src/compiler/mono_clone.cpp#L5440-L5449`

### `mono.method.lazy-instantiation-with-pinned-roots` — Generic struct methods are instantiated lazily, modulo pinned roots

Struct/enum methods of a generic type are instantiated on demand from call sites rather than eagerly per instantiation; methods that are trait-dispatch entries or explicitly root-pinned are force-instantiated even with no referencing call site.

Source: `src/compiler/mono_impl.hpp#L403-L487`

### `mono.method.generic-specialization` — Method-level generic call specialized

A method call is routed to monomorphized specialization only when it has a genuine method-level type parameter (one not bound by the receiving type's type params) and all type arguments resolve to concrete (non-TypeVar, non-Error) types; pure struct-level-generic methods stay on the non-specialized method-call lowering.

Source: `src/compiler/sema_expr.cpp#L8975-L9051`
