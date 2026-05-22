# B129 — UI-surfaced gaps (tests/ui/{traits,issues} run-pass import)

Source commit: `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`
Date: 2026-05-22

NEW gaps surfaced while distilling B129 (not previously recorded in KNOWN-OPEN /
DIVERGENCES §B). Each was reduced to a minimal repro; the test that surfaced it
was either re-shaped to the supported subset (and kept) or dropped (and recorded
here). KNOWN-OPEN re-hits (string-literal match patterns, for-over-`&[&dyn]`)
are NOT re-reported.

---

## G1 — UFCS trait-method call through a `&dyn Trait` receiver  (§B, catch-up)

`Trait::method(obj)` where `obj: &dyn Trait` (trait-object receiver) fails:

```
error [fn main]: call to undefined static method 'Foo::test'
```

```logos
trait Foo { fn test(self: &Self) -> i64; }
struct W { v: i64 }
impl Foo for W { fn test(self: &W) -> i64 { return (*self).v; } }
fn main() -> i32 {
    let w = W { v: 22i64 };
    let a: &dyn Foo = &w;
    return if Foo::test(a) == 22i64 { 0i32 } else { 1i32 };   // Foo::test(a) → undefined
}
```

Trait-qualified UFCS dispatch on a *concrete* receiver works (DIVERGENCES says so,
and `Foo::test(&w)` compiles). The gap is specifically the trait-object receiver:
the UFCS resolver dispatches off the first arg's concrete type and has no branch
for a `&dyn Trait` arg → it should resolve to the object's vtable slot.
Original: `tests/ui/traits/ufcs-object.rs`. Test dropped.

§B catch-up — Rust supports `Foo::test(a)` for `a: &dyn Foo`.

---

## G2 — `impl Trait for fn(...) -> ...`  (parse)  (§B, catch-up)

Implementing a trait for a bare function-pointer type is a parse error:

```
error: syntax error near 'impl' at line 10 col 1
```

```logos
trait MyTrait { fn foo(self: &Self) -> i64; }
impl MyTrait for fn(i64, i64) -> i64 { fn foo(self: &fn(i64, i64) -> i64) -> i64 { return 1i64; } }
```

The grammar's `impl ... for <type>` target does not accept a `fn(...)->...` type
in head position. (Logos *does* accept `fn(...)` as a field/param/let type; only
the impl-target position rejects it.)
Original: `tests/ui/traits/fn-type-trait-impl-15444.rs`. Test dropped.

§B catch-up — Rust allows `impl Trait for fn(A,B)->C`.

---

## G3 — trait-object upcast `&dyn Sub` → `&dyn Super`  (runtime SIGSEGV)  (§B, catch-up)

Casting one trait object to a supertrait object and dispatching a method through
the upcast object compiles but **segfaults at runtime**:

```logos
trait Pollable { fn poll(self: &Self) -> i64 { return 0i64; } }
trait FileIo : Pollable { fn read(self: &Self) -> i64 { return 7i64; } }
trait Terminal : FileIo {}
struct A {}
impl Pollable for A {} impl FileIo for A {} impl Terminal for A {}
fn main() -> i32 {
    let a = A {};
    let b = (&a) as &dyn Terminal;
    let c = b as &dyn FileIo;     // upcast
    return if c.read() == 7i64 { 0i32 } else { 1i32 };   // SIGSEGV
}
```

`(&a) as &dyn Terminal` builds a vtable for the most-derived trait; re-casting to
`&dyn FileIo` does not pick the FileIo sub-vtable, so the method-index lookup on
`c.read()` reads the wrong slot → segfault. Related to (but distinct from) the
KNOWN-OPEN "supertrait method on &dyn Sub": here the explicit *upcast* itself is
the trigger.
Original: `tests/ui/traits/upcast_reorder.rs`. Test dropped.

§B catch-up — Rust trait-upcasting coercion (stable since 1.86) re-points the vtable.

---

## G4 — unconstrained blanket impl `impl<T> Foo for T` + PRIMITIVE receiver through a generic fn  (mono)  (§B, catch-up)

A blanket impl works on a struct receiver through a generic fn, and works on a
primitive receiver via a *direct* call — but a primitive receiver routed through a
generic-bound fn fails to emit the specialization:

```
error: 'func.call' op 'i64__xyz' does not reference a valid function
mlir_gen: module verification failed
```

```logos
trait Foo { fn xyz(self: &Self) -> i64; }
impl<T> Foo for T { fn xyz(self: &T) -> i64 { return 7i64; } }
fn foo<T: Foo>(t: &T) -> i64 { return t.xyz(); }
fn main() -> i32 { return if foo(&0i64) == 7i64 { 0i32 } else { 1i32 }; }  // i64__xyz not emitted
```

Works (kept as `blanket-impl-all-T-via-generic-fn-tr3`): `foo(&s)` for a struct `s`.
Works: `(0i64).xyz()` directly. Fails: only the `foo::<i64>` path — mono enqueues a
call to `i64__xyz` but never instantiates the blanket method template for the
primitive `T=i64` binding when reached through the generic bound. Likely the same
template-enqueue site as the blanket-default-method baghunt, but for a *primitive*
(non-struct) concrete `T`. Test kept in the struct-receiver form.

§B catch-up — Rust dispatches the blanket impl uniformly for primitive and struct T.
