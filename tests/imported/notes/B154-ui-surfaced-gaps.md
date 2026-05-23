# B154 — rustc UI run-pass import: surfaced gaps

Batch B154 imported 27 run-pass tests from `tests/ui/` across fresh, lightly
mined areas: drop (6), enum-discriminant (4), or-patterns (3), numbers-arithmetic
(2), reachable (2), issues (2), block-result (1), consts (1), expr (1), let-else
(1), mut (1), return (1), variance (1). Workflow matches B149–B153: faithful
ports, `pub fn main()` → `fn main() -> i32 { …; return 0; }`, isize/usize →
i64/u64, integer/float literals suffixed, `assert!`/`assert_eq!` → early-return
sentinels (distinct nonzero codes), Box/vec!/println!/PhantomData/#[derive]/
#[repr]/named-lifetimes dropped where incidental, nested type decls hoisted, and
the established **local distilled-drop convention** `trait Drop { fn drop(self:
Self); }` with a `*mut i64` counter for observable destructors (as across
`pass/drop/`). All 27 compile + link + exit 0 against the as-is `build/bin/logosc`
(no compiler changes). The link line uses `-Wl,--gc-sections` (as for B149–B153)
so the stdlib's unreferenced `derive_*_hook` metaprog functions (which reference
JIT-only runtime symbols) get garbage-collected.

Several initially-attractive picks turned out to duplicate already-imported
upstream tests (e.g. `drop/issue-979`, `drop/conditional-drop-10734`,
`drop/destructor-run-for-expression-4734`, `overloaded/overloaded-index`,
`overloaded/overloaded-deref`, `deref/deref-newtype-method-call`,
`let-else/issue-99975`) and were skipped.

---

## STATUS 2026-05-23
✅ **G154-5** let-else literal/range pattern never refuted (silent) — FIXED ea45fedb.
REMAINING — high-severity (DEEP, for a focused control-flow/semantics session):
- **G154-1** ⚠️ `return` in a sub-expression (struct-field init / call arg) doesn't
  short-circuit → wrong value / SIGSEGV. Never-type-in-subexpression: the
  diverging `return` in expr position isn't emitted as the terminator before the
  enclosing struct-lit/call continues. Control-flow lowering piece.
- **G154-4** ⚠️ moving a Drop value OUT of a tuple double-drops. ROOT: tuples
  don't have proper Drop semantics — has_droppable_fields is Struct-only, so a
  tuple owning a String doesn't drop it via the tuple; the move-in element isn't
  marked moved and the move-out isn't tracked. Needs tuple-Drop (drop elements +
  move-in/out tracking). (Partial mark_moved_expr TupleIndex tried + reverted —
  insufficient alone.)
- **G154-6** ⚠️ inner `fn` item shadowing a same-named local breaks the outer
  local ref (niche; trivial rename workaround).
REMAINING — parse/sema (workarounds): G154-2 return-diverging let-init, G154-3
block-as-for-iterator, G154-7 `@`-binding in variant payload, G154-8 range/
multi-field or-pattern in payload, G154-9 tuple-struct ctor as fn value, G154-10
`<T as Trait>::Out` projection in type position.
ACCUMULATING DEEP-GAP BACKLOG (across B153/B154, for a focused session): backward
type-inference from later assignment (G153-1/2), `static`/`static mut` items
(G153-3), return-in-subexpression (G154-1), tuple-Drop semantics (G154-4).

## Gaps surfaced

### G154-1 — ⚠️ SILENT MISCOMPILE / CRASH: `return` in a sub-expression does not short-circuit
A `return` placed inside a sub-expression position (a struct-literal field
initializer, or a call/macro argument) does NOT short-circuit the enclosing
function — and in argument position it can SIGSEGV.

Field-init form silently returns the WRONG value (no short-circuit):
```
struct Pair { a: i64, b: i64 }
fn foo() -> Result<i64, i64> {
    let p = Pair { a: 22i64, b: return Err(32i64) };  // does NOT return Err
    return Ok(p.a);                                    // this runs instead → Ok
}
```
Argument-position form CRASHES:
```
fn baz() -> Result<i64, i64> {
    Ok(if true { return Err(32i64); } else { 0i64 })   // SIGSEGV (rc 139)
}
```
Also, a `return <expr>` as an `+`/numeric operand errors `operator '+': right
must be numeric, got !` (the never type is not coerced into arithmetic).
`drop/nested-return-drop-order.rs` was DROPPED for the headline; the surviving
tail-vs-return parity portion is `return/tail-vs-return-parity-b154.logos`.

### G154-2 — block/initializer that diverges via `return` is not seen as diverging
A function body that is a bare trailing block whose tail is a `return`, OR a
`let _x: T = return …;` initializer, errors `not all paths return a value`:
```
fn f() -> i64 { { return 3i64; } }          // error: not all paths return
fn g() -> i64 { let _x: i64 = return 7i64; } // error: not all paths return
```
`break`/`continue` initializers ARE recognized (`let _x: i64 = break;` works).
The working idiom is `return { … return 3; };`. (`reachable/artificial-block`
reshaped; `drop/terminate-in-initializer` keeps only the break/continue cases.)
Same family as G154-1 (never-typed-expression reachability).

### G154-3 — inline block as a `for` iterator expression is a parse error
```
for x in { 0i64..3i64 } { … }   // syntax error near '3i64'
```
Binding the block to a `let` first works (`let r = { 0..3 }; for x in r`).
(`mut/no-mut-lint-for-desugared-mut` reshaped.)

### G154-4 — ⚠️ DOUBLE-DROP: moving a Drop value OUT of a tuple drops it twice
Moving a Drop-typed value INTO a tuple drops it exactly once (correct), but
moving it back OUT — either by tuple destructure `let (c,_d) = b;` or by field
access `let c = b.0;` — drops it TWICE (both the tuple slot and the new binding
fire their destructor). Move-out of the tuple element is not tracked, so the
source is not marked moved.
```
let a = r(i);             // R with Drop
let b = (a, 10i64);       // moved in — OK, 1 drop
let c = b.0;              // moved out — BUG: 2 drops (counter == 2, want 1)
```
(`drop/drop-once-on-move` keeps only the move-INTO-tuple form.)

### G154-5 — ⚠️ SILENT MISCOMPILE: refutable integer-literal pattern in `let … else` never refutes
```
fn t(x: i64) -> i64 {
    let 4i64 = x else { return 99i64; };   // x=5 should take else → 99
    return 42i64;                          // BUG: always runs → returns 42
}
```
`t(5)` returns 42, not 99 — the literal pattern always "matches" and the else
branch is skipped. ENUM-pattern let-else (`let Some(_) = … else { … }`) refutes
correctly. (`let-else/let-else-run-pass` reshaped to enum patterns.)

### G154-6 — ⚠️ MISCOMPILE: an inner `fn` item shadowing a same-named local breaks the OUTER reference
```
fn main() -> i32 {
    let f: i64 = 1i64;
    { fn f() -> i64 { return 42i64; } /* call f() here */ }
    if f != 1i64 { … }   // mlir-gen error: 'llvm.icmp' op requires all operands same type
}
```
The outer `f != 1i64` resolves `f` to the inner fn ITEM (not the local), then
emits a comparison of a function value against i64 → mlir-gen failure
(`resolve/type-param-local-var-shadowing.rs` DROPPED). Inner `fn` items that
DON'T collide with a local name are fine.

### G154-7 — `@`-binding inside an enum-variant payload pattern is unsupported
```
match x { Ok(y @ 4i64) | Err(y @ 6i64) => y, … }
//   error: nested patterns inside enum-variant payloads are not yet supported;
//          bind to a name and match in the body
```
(`or-patterns/bindings-runpass-2.rs` DROPPED; `bindings-runpass-1` moves the
`z @ (0|4)` binding into the arm body.)

### G154-8 — range or-pattern / multi-field or-pattern inside an enum payload unsupported
A single-element payload with a pure-literal or-pattern works
(`Foo::One(42 | 255)`), but a payload or-pattern that mixes a RANGE
(`Foo::One(100 | 110..=120)`) or that spans a MULTI-field payload
(`Foo::Two(42|255, 1024|2048)`) errors `nested patterns inside enum-variant
payloads are not yet supported`. (`or-patterns/basic-switchint` reshaped to
distinct top-level variant or-patterns.)

### G154-9 — tuple-struct constructor cannot be used as a first-class fn value
```
struct A(bool);
let f = A;       // error: undefined variable 'A'
let a = f(true); // error: call to undefined function 'f'
```
(`issues/issue-5315.rs` DROPPED.)

### G154-10 — qualified associated-type projection in a type position not resolved
```
trait Foo { type Out; }
impl Foo for Unit { type Out = bool; }
let x: <Unit as Foo>::Out = true;
//   error: let 'x': type mismatch — expected Unit::Out, got bool
```
The projection `<Unit as Foo>::Out` is not normalized to `bool`, so the binding
type stays the un-normalized `Unit::Out`. (`issues/issue-28828.rs` DROPPED.)

### Divergence re-confirmed (not a new gap)
- **Struct field drop ORDER is REVERSE of Rust.** Logos drops struct fields in
  reverse declaration order (`two` before `one`); Rust drops in declaration
  order. A 2-field struct drops both fields (count correct) but in reverse; a
  drop body that both reads AND writes through a shared pointer field also
  produced inconsistent intermediate reads over 3 fields. Because the order
  diverges from Rust and the read/write interaction is murky,
  `drop/struct-field-drop-order.rs` was DROPPED rather than encode the reversed
  order as "expected".

### Re-confirmed known gaps (NOT new)
- `static`/`static mut` items — no module-level mutable globals (G153-3); the
  `static` portions of `consts/const-fields-and-indexing` and
  `enum-discriminant/conditional-drop` siblings were dropped; const-array
  indexing kept.
- const STRUCT / array-of-struct literal initializers rejected (initializer must
  be literal / simple-arith / metacall); only const scalar/array kept.
- deferred-init assignment to a non-`mut` local rejected (annotate `mut`).
