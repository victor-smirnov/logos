# Generics and Traits

Logos generics are monomorphized: every concrete instantiation of a generic function or type produces its own specialized code at compile time. There is no type erasure and no runtime generic dispatch except through explicit trait objects.

## Generic Functions

```logos
fn max<T>(a: T, b: T) -> T where T: Ord {
    if a > b { return a; }
    return b;
}
```

Bounds may be written inline (`fn f<T: Trait>(...)`) or in a `where` clause.

## Generic Types

```logos
struct Pair<A, B> {
    first: A;
    second: B;
}

fn swap<A, B>(p: Pair<A, B>) -> Pair<B, A> { ... }
```

`Array<T>` and `Map<K, V>` in the standard library are templated containers that follow this pattern. They have blanket trait impls for the operations they support (clone, equality, hash, etc.) and rely on element-side traits like `CloneElem` and `RelPtr`.

## Traits

```logos
trait Shape {
    fn area(&self) -> f64;
    fn name(&self) -> &str;
}

impl Shape for Circle {
    fn area(&self) -> f64 { ... }
    fn name(&self) -> &str { return "circle"; }
}
```

Traits may have default method bodies. Associated types are partially supported.

## Blanket vs. Concrete Impls

A blanket impl applies to a parameterized form, e.g. `impl<V> Foo for Map<i64, V>`. A concrete impl applies to a single specialization, e.g. `impl Foo for Map<i64, String>`.

There is a known quirk: sibling concrete specializations can suppress each other's tag-dispatch registration in some configurations. The recommended pattern is to write a blanket impl over a parameter and let it apply, rather than enumerating concrete cases. (See `feat_map_concrete_impl_quirk` in the project memory.)

## Overloading

Function and method overloading by parameter type is supported. Resolution is **strict exact-type**: candidates that would only match through implicit conversion are rejected, and the dispatch decision uses no coercions. This keeps overload sets unambiguous and predictable.

Operator overloading follows the same rule, dispatched through the corresponding traits. Generic and non-generic candidates can coexist; duplicates are diagnosed.

Tests: `fn_overload`, `fn_overload_generic`, `op_overload` in `tests/logos/pass`.

## Specialization and Monomorphization

Monomorphization is performed in [src/compiler/mono.cpp](../../src/compiler/mono.cpp) and friends. The compiler walks reachable instantiations, substitutes type parameters, and emits one MLIR/LLVM body per concrete shape. The implementation is being refactored to operate on a `TypeRef` accessor abstraction (see [Metaprogramming](../internals/metaprog.md)).

For generic trait impls, calls of the form `T::assoc_fn(...)` for a generic associated function may not currently monomorphize correctly in all cases. The workaround is to call the function as a method on `&self` with a phantom argument; this is tracked as a known gap.

## Runtime Dispatch

Logos has two distinct runtime-dispatch mechanisms, used in different contexts.

**`dyn Trait` for plain Logos values.** Heap-allocated objects are *not* tagged. Polymorphism over plain values goes through trait objects (`dyn Trait`, `&dyn Trait`, `Box<dyn Trait>`) the same way Rust does it: a fat pointer carrying a data pointer plus a vtable. The compiler emits one vtable per `(type, trait)` pair.

**Tag-based dispatch for Hermes data.** Hermes is different: its values are *self-describing* and survive serialization, so they carry their own type identity. Hermes containers carry an 8-byte schema type code; values inside them carry a 1- to 8-byte type tag. Type codes 1–128 are reserved for the system registry; the user range is derived from a 56-bit slice of the type definition's hash. Trait method tables (e.g. `HermesStringify`, `HermesEqual`) are emitted per trait and indexed by tag at the dispatch site. Link-time collision detection on user codes is planned but not yet at full strength.

The two mechanisms do not overlap: `dyn Trait` covers heterogeneity over heap-allocated Logos objects; tag dispatch covers heterogeneity that has to round-trip through bytes. Statically known types in either world are still monomorphized and dispatched at compile time.

## Reflection and Metadata

Each Logos type has a content-addressed hash (SHA-256 of the resolved type expression), with 23 bytes used for type identity and 8 bytes for member identity. Metadata lookup is O(1) via the hash; dispatch type codes are derived from the same source.

This system is shared with Hermes (which uses 8-byte schema type codes) and is the substrate for compile-time metaprogramming. See [Metaprogramming](../internals/metaprog.md) for the in-progress story.
