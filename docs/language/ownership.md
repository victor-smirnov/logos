# Ownership and Borrowing

Ownership and borrow checking in the same family as Rust's. Implemented in [src/compiler/borrow_check.cpp](../../src/compiler/borrow_check.cpp); runs after sema, before codegen.

## The Three Rules

1. **Every value has exactly one owner.** Moving a value transfers ownership; the source binding is no longer usable.
2. **A value may be either uniquely borrowed (`&mut`) or shared-borrowed (`&` by any number of holders) at any one time, never both.**
3. **A borrow must not outlive the value it points to.** Lifetimes track this; the checker rejects programs where a reference would dangle.

The checker was built up over four phases (exclusivity, provenance tracking, named lifetimes, escape analysis). Several dozen pass/fail tests target it directly (names matching `borrow_*`, `lifetime_*`, `move_*`, `drop_*`, `escape_*`); much of the rest of the suite exercises it indirectly.

## Move Semantics

```logos
let s: String = String::from("hi");
let t: String = s;          // move
print_string(&s);           // ERROR: use after move
```

Primitive types (`i64`, `bool`, references, etc.) are `Copy`-like and are not moved. There is no user-visible `Copy` trait yet; the categorization is built in.

## Borrowing

```logos
let mut v: Vec<i64> = Vec::new();
v.push(1);

let r1: &i64 = v.borrow(0);    // shared borrow of an element
let r2: &i64 = v.borrow(0);    // ok — multiple shared
let sum: i64 = *r1 + *r2;       // both borrows used here

let r3: &mut Vec<i64> = &mut v;   // ERROR if r1/r2 still live
```

The checker uses a flow-sensitive analysis. Borrows are live until their last use, not until the end of their lexical scope. Code like:

```logos
let r: &T = &value;
print(r);
let m: &mut T = &mut value;     // ok: r is no longer live
```

is accepted.

## Lifetimes

Lifetimes are named with a leading apostrophe (`'a`) on function signatures:

```logos
fn longest<'a>(x: &'a str, y: &'a str) -> &'a str { ... }
```

For most programs, lifetimes can be elided by the same elision rules Rust uses for the simple cases (one input reference → output borrows from it; `&self` → output borrows from self).

Named lifetimes are required when relating multiple input references to a single output reference, or when storing references in structs.

## Views and Owning References

Logos has two kinds of "reference-like" things in addition to `&T`:

- **`WView2`**: a non-owning fat pointer, like `&T` but pointing into a Writ zone. Lifetime-tracked the same way.
- **`OView`**: an *owning* view that holds a refcount on the underlying memory holder. Used when a value needs to escape a function but the source is a Writ zone, not a stack allocation.

The compiler infers which is needed via escape analysis. The annotation `#[yields_view_of]` is available where escape analysis cannot prove the relationship; the manual workaround is `.own(source)`.

See [Writ in Logos](writ.md) for how views interact with the data substrate.

## Auto Traits (Planned)

Marker traits like `Send`/`Sync` (Logos names TBD) are planned for the concurrency model, with the compiler auto-implementing them based on field types. Today, the fiber runtime predates this and uses convention.

## Common Errors

The checker emits structured diagnostics for:

- use after move
- conflicting borrows (mutable + any other)
- borrows that would outlive the borrowee
- lifetime mismatches in function signatures
- stores into a `&` (non-`&mut`) reference

Tests under `tests/logos/fail/borrow_*` and `tests/logos/fail/lifetime_*` cover the diagnostic shapes.
