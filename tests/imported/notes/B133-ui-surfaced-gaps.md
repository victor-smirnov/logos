# B133 — UI-surfaced gaps

Batch B133 imported ~22 run-pass tests from rustc UI areas under-covered in
`tests/imported/pass/`: `unboxed-closures/` (had only 1), `ops/` (had only a
coretest), plus distinct `methods/`, `mir/`, `structs-enums/` facets. Source
pin: rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7. Suffix `-b133`.

## NEW gaps surfaced (not previously catalogued)

- **(G133-1)** User operator overloading is only wired for a SUBSET of the
  binary operators. `+` (Add), `-` (Sub), `*` (Mul), `/` (Div), `%` (Rem),
  unary `-` (Neg) on a user struct DO dispatch to the trait impl. But the
  bitwise operators `^` (BitXor), and (by the same hardcoded path) `&`
  (BitAnd) / `|` (BitOr) / `<<` (Shl) / `>>` (Shr) on a user struct are NOT
  routed to their `ops` trait impls — sema rejects with `operator '^': left
  must be integer or bool, got <Struct>`. The traits exist in
  `stdlib/lang/ops/ops.logos` (BitAnd/BitOr/BitXor/Shl/Shr) but the operator
  lowering for these does not consult them for non-primitive operands. (Add/
  Sub/Mul/Div/Rem/Neg lowering does.) — §B catch-up.

- **(G133-2)** Assigning to an OUTER mutable local that the closure did not
  already capture, from inside a non-`mut` (`Fn`-kind) closure body, fails with
  `mlir_gen: assign to undefined '<name>'`. A `mut`-marked closure that mutates
  a captured local works (see uc-infer-fnmut / uc-by-ref-kinds). Worked around
  by having the read-only closure RETURN the value rather than write through an
  outer var. (May be the documented capture-mode boundary rather than a true
  bug; recorded for completeness.) — §B catch-up.

## Confirmed WORKING (this batch)

- Closure literals with explicit param/return types, called directly
  (uc-simple); two-arg closures.
- Higher-order fns over `fn(..)->..` pointer params; fn ITEM → fn-ptr coercion
  at the call site (uc-generic-call-it).
- `move`-style closures capturing an upvar by value, single-word env
  (uc-single-word-env).
- Closure that mutates a captured local (`FnMut` kind inferred), called twice;
  mutation observed after the closure scope (uc-infer-fnmut).
- Generic `f<F: FnMut()>` invoking a closure that mutates a captured local
  through the bound (uc-infer-upvar).
- By-ref capture across Fn / FnMut kinds: `Fn`-bound HOF reads a captured var,
  `FnMut`-bound HOF mutates it (uc-by-ref-kinds).
- One closure shape satisfying all three `Fn` / `FnMut` / `FnOnce` generic
  bounds in turn (uc-all-traits).
- User operator overloading: binary `+`/`-` (Add/Sub), unary `-` (Neg),
  custom `PartialEq`-driven `==` on a struct (operator-overloading);
  `*`/`/`/`%` (Mul/Div/Rem) on a newtype (operator-mul-rem-div).
- Compound-assignment `+=`/`-=`/`*=` on a user struct dispatching to
  AddAssign/SubAssign/MulAssign (augmented-assign-struct).

## Re-confirmed known-open (NOT re-reported; source facet dropped/distilled)

- Operator MULTIDISPATCH (`impl Add<isize> for Point` distinct RHS/Output) —
  Logos `ops` traits are single-type (Self=RHS=Output), the documented
  divergence; the `Add<isize>` + `Index<bool>` facets of operator-overloading
  were dropped.
- `feature(fn_traits)` manual `impl Fn/FnMut/FnOnce for S` with the
  `extern "rust-call"` ABI + tuple-args (overloaded-calls-simple/zero-args) —
  not the Logos call-trait shape; skipped.
- `Box` / `Rc` / `RefCell` overloaded `Deref`/auto-deref chains
  (overloaded-deref / overloaded-autoderef) — Rc/RefCell-heavy; skipped.
- Method autoderef THROUGH a user `Index` place (`f[1].inc()`) — pre-existing
  open gap (notes/B108); the existing overloaded-index test already documents
  it.
- Scalar `move`-closure capture VALUE-isolation: a `move || { counter += 1 }`
  closure capturing a scalar local mutated the OUTER `counter` (Rust isolates
  the moved copy, so the outer stays 0). One candidate
  (unboxed-closures-infer-fnmut-move) exited 1 on this and was DROPPED rather
  than re-reported as a confirmed gap — recorded here for the next batch to
  decide if it is a true divergence or a capture-mode bug.
