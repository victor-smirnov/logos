# Ownership, Borrowing, and Lifetimes

Logos uses a Rust-style ownership model: every value has a single *owner*, ownership transfers on assignment / argument-passing of non-`Copy` types, and references (`&T`, `&mut T`) are checked at compile time for exclusivity and lifetime validity. The borrow checker is implemented in `src/compiler/borrow_*.cpp` and runs after sema, before LIR lowering. See [memory: project_borrow_checker](../../README.md) for status.

## Move vs Copy

```logos
let s = String::from("hi");
let t = s;            // moves; `s` is no longer usable
let n: i32 = 42;
let m = n;            // i32 is Copy — `n` still usable
```

A type is `Copy` if it consists only of `Copy` primitives (integer/float/bool/raw pointer) and `Copy` aggregates. References (`&T` / `&mut T`) are `Copy` (a borrow can be re-borrowed). User types are not `Copy` by default — opt in via `#[derive_copy]` (Logos uses one `#[derive_<trait>]` annotation per derived trait, not `#[derive(...)]`).

Once moved, the source binding cannot be read or borrowed; trying to use it produces a "use after move" diagnostic.

## Borrows

```logos
let v = vec![1, 2, 3];
let r: &Vec<i32> = &v;          // shared borrow
let m: &mut Vec<i32> = &mut v;  // exclusive borrow (requires `mut` binding)
```

Two rules:

1. **Aliasing XOR mutation** — at any point in the program, a value is either pointed to by any number of `&T` or by exactly one `&mut T`. Never both.
2. **No dangling borrows** — a borrow's lifetime cannot exceed the lifetime of the value it points into.

Violations are reported with point-of-issue and conflicting-borrow location.

### Re-borrows

```logos
let r: &mut Vec<i32> = &mut v;
let s: &Vec<i32> = &*r;    // re-borrow r as shared (suspends r until s ends)
```

The compiler inserts re-borrows automatically at most call sites; explicit `&*r` / `&mut *r` is needed when nudging the inference.

## Lifetimes

Most lifetimes are inferred. They become explicit when:

- A function takes multiple borrows and returns a borrow whose source is ambiguous.
- A struct or enum stores a borrow.
- A trait method's borrow lifetime relates to a type parameter.

```logos
fn first<'a>(xs: &'a [u32]) -> &'a u32 { &xs[0] }

struct Slice<'a, T> { data: &'a [T] }

impl<'a, T: Display> Display for Slice<'a, T> { ... }
```

Lifetime tokens are `'name` ([logos.peg:35](../../../tools/peg_gen/grammars/logos.peg#L35)) — leading apostrophe + lowercase identifier. `'static` is the all-program lifetime; `&'static T` typically points to rodata.

### Elision

The standard elision rules apply: when there's exactly one input lifetime (after `&self` / `&mut self` are excluded), it is the output lifetime; when there's `&self`, `&self`'s lifetime is the output lifetime. Otherwise the function must annotate.

## Mutability

```logos
let mut v = Vec::new();
v.push(1);             // requires `let mut`
let r = &v;            // shared borrow — read-only
let m = &mut v;        // exclusive — needs `mut` on the binding
```

`mut` opts the binding into:

- Reassignment: `v = other_vec;`.
- Mutable borrows: `&mut v`.
- Field-level write through `v.field = ...`.

Function parameters pattern: `fn f(mut x: i32)` is allowed and binds `x` mutably inside the body — does *not* affect the caller (parameters are always by-value or by-reference, no out-parameters).

## Ownership of Composite Values

- **Struct fields** — owned by the struct; partial-move out of a non-`Copy` field is allowed and "splits" the struct.
- **Enum payloads** — owned by the enum; pattern-matching can move them out (e.g. `Some(x) => use(x)`).
- **Tuples and arrays** — same: own their elements, partial-move is permitted.
- **Vecs / HashMaps / Boxed values** — own their heap allocation; dropped when the owner goes out of scope.

## Drop

```logos
struct File { fd: i32 }

impl Drop for File {
    fn drop(&mut self) { unsafe { close(self.fd); } }
}
```

When a value goes out of scope and isn't moved, `Drop::drop` runs (if implemented) before the storage is reclaimed. Drop order is reverse of declaration within a block, and depth-first within composites. Manual `drop(x)` is the standard early-drop primitive.

## Raw Pointers

`*const T` and `*mut T` are *not* borrow-checked. They escape the lifetime system entirely and are usable only inside `unsafe` for dereferences and writes. Use them at FFI boundaries, in low-level data structures, and for tag manipulation.

```logos
let p: *const u8 = b.as_ptr();
let n = unsafe { *p };
```

Casting `&T → *const T` and `&mut T → *mut T` is implicit when the destination type demands it; the reverse (`*const T → &T`) requires a `&*p` re-borrow inside `unsafe` and is the programmer's promise that the pointer is valid.

## View Types and Fat Pointers

For Writ-stored objects, *view* types replace bare references:

```logos
fn show(v: WritCtrView<'_>) -> Result<(), io::Error> { ... }
```

A `WritCtrView<'a>` is a fat borrow `(base, &doc)` — the view carries enough state to resolve relative pointers without owning the document. See [memory: feat_view_ownership](../../README.md) and [memory: feat_writ_read_write_traits](../../README.md). The compiler infers view-or-reference mode from escape analysis; `#[yields_view_of]` and `.own(source)` are explicit annotations when inference is wrong.

## Roadmap

- **Non-lexical lifetimes (NLL)** — basic NLL is in; some early-drop / branch-merge cases still over-conservative.
- **Polonius-style flow** — under consideration; not on near-term roadmap.
- **`Pin<T>` / self-referential structs** — not yet a language feature; coroutines need this and the borrow checker is being prepared.
- **Cross-package borrow inference** — currently every `pub fn` annotates lifetimes explicitly; cross-package elision is planned.
