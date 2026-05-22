# B134 — UI run-pass import: surfaced gaps

Batch B134 imported 22 distinct run-pass tests from rustc UI areas
(`self`, `for-loop-while`, `expr`, `coercion`, `numbers-arithmetic`,
`methods`, `generics`, `structs-enums`, `functions-closures`, `traits`,
`where-clauses`, `cast`). All 22 compile + link + exit 0.

## NEW gap surfaced

### (G134-1) `for x in &v` over a `Vec<Struct>` yields the WRONG element
Iterating a `Vec` whose element type is a multi-field struct **by
reference** (`for elt in &v { ... (*elt).field ... }`) does not yield the
correct per-iteration element. The iteration COUNT is correct (matches
`v.length()`), but the dereferenced struct value read in each arm is wrong
— e.g. with three pushed `Pair{x:1}`, `Pair{x:2}`, `Pair{x:4}`, an
accumulator `acc = acc*10 + (*elt).x` produced `102` instead of the
expected `124`, and a 2-element sum of `.x` produced `30` instead of `40`
(i.e. it reads the LAST element's fields, not each successive element's).

Direct indexed access through the same Vec is CORRECT: `v.get(0)` /
`v.get(1)` return the right structs (`.x` reads 10 and 30 respectively),
so the bug is specific to the `for ... in &Vec<Struct>` iterator lowering /
the element-handle deref it produces — NOT to Vec storage or struct field
offsets. `for i in &[T; N]` (array, scalar elements) works fine (existing
`foreach-external-iterators-nested` passes), so this is the Vec-of-struct
by-ref-iteration path specifically. The candidate test
(`for-loop-while/for-destruct.rs`) was dropped from this batch as a result.

Minimal repro:
```
struct Pair { x: i64, y: i64 }
fn main() -> i32 {
    let mut v: Vec<Pair> = vec_new::<Pair>();
    v.push(Pair { x: 1i64, y: 0i64 });
    v.push(Pair { x: 2i64, y: 0i64 });
    v.push(Pair { x: 4i64, y: 0i64 });
    let mut acc = 0i64;
    for elt in &v { acc = acc * 10i64 + (*elt).x; }
    return acc as i32;   // expect 124, gets 102
}
```

## Re-confirmed known-open (NOT re-reported; candidate dropped/distilled)

- **never-typed `if`/`loop` arm as a sub-expression value**: `let i: i64 =
  if c { return 7; } else { 5 };` → "if-expression branches have
  incompatible types: void vs i64". (`expr/if-bot.rs` dropped; matches the
  known-open "if-as-subexpr never-type".)
- **GENERIC tuple-struct `S<T>(T)`**: `struct S<T>(T); let a = S::<i64>(2);`
  → "call to undefined function 'S'" + "tuple index on non-tuple type". A
  CONCRETE tuple struct `Pair(i64,i64)` works fine, so the test was
  re-distilled to the concrete form (`structs-enums/tuple-struct-fields-se4`).
- **shift-expr in enum discriminant position**: `enum Color { Purple = 1 << 1 }`
  → "syntax error near '1'". Logos requires a literal in disr position; the
  shift-derived disr values were pre-evaluated to literals
  (`structs-enums/tag-variant-disr-val-se4`).
- **radix integer literal with `_` BEFORE the suffix**: `0xBEEF_i64` →
  "malformed integer literal". The supported form is `0xBEEFisize` /
  `0xBEEFi64` (no underscore between digits and suffix); the existing
  `numbers-arithmetic/integer-literal-radix.logos` already covers radix
  literals, so that candidate was dropped as a dupe.
- **unit struct `struct A;`**: not accepted ("'A' is not defined — did you
  mean 'struct A { ... }'"); use `struct A {}` + `A {}` (applied in
  `traits/default-method-simple-tr4`).
- **zero-arg closure without explicit return type / block**: `|| f(v)` →
  "syntax error near '||'"; use `|| -> T { return f(v); }`
  (`functions-closures/closure-calls-fn-fc4`).
- **block-wrapped closure literal `{ || ... }`**: also a syntax error;
  drop the wrapping braces.
- **chained `arr[i](args)`**: index-then-call in one expression is a syntax
  error; bind `let f = arr[i];` first then `f(args)`
  (`coercion/coerce-fn-if-else-coe4`).
