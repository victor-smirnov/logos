# B132 — UI-surfaced gaps (tests/ui/binding + tuple)

rustc commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`. Batch B132 imports
DISTINCT run-pass binding + tuple features into `tests/imported/pass/{binding,tuples}/`
(suffix `-bn2` / `-tp2` to dodge the global ctest-name clash). DO NOT modify the
compiler/stdlib. This file records gaps surfaced while distilling the sources.

## NEW gaps (candidate — not previously recorded)

### N1. 2-tuple `match`: a no-payload first-element variant short-circuits the
remaining tuple-element discriminant checks (arm-ordering-dependent miscompile)
- Source: `binding/borrowed-ptr-pattern-option.rs` (distilled to a by-value
  `(Option<i64>, Option<i64>)` match).
- Symptom: with arms ordered `(None, None)` BEFORE `(None, Some(b))`, matching the
  value `(None, Some(9))` wrongly selects the `(None, None)` arm. Reordering so the
  `(None, Some(_))` arm comes first yields the correct result. When the first
  element is `Some(_)`, the second element IS discriminated correctly — so the bug
  is specifically: when the first tuple-element pattern is a NO-PAYLOAD variant
  (`None`), the matcher commits to that arm without testing the second element's
  discriminant.
- This is a CORRECTNESS bug (silent wrong-arm), not a parse/typeck error. Test
  dropped (relying on it would be fragile + would mask the bug).

### N2. nested tuple DESTRUCTURING sub-pattern inside a `match` arm
- Sources: tuple-as-enum-payload (`E::Pair((a, b))`) and plain nested tuple match
  (`match t { (a, (b, c)) => .. }`).
- Symptom: BOTH `match e { E::Pair((a, b)) => a + b, .. }` and
  `match t { (a, (b, c)) => a + b + c }` → `undefined variable 'b'` / `'c'` at
  sema — the inner tuple-pattern's bindings are not registered when the tuple
  pattern is nested (under an enum-variant pattern OR under an outer tuple pattern)
  in a MATCH arm. Note the asymmetry: a nested tuple pattern in a `let`
  (`let (a, (b, c)) = ..`) WORKS (imported as `nested-tuple-let-bn2`); only the
  match-arm form drops the inner bindings. Binding the payload WHOLE
  (`E::Pair(p)` then `p.0`/`p.1`) also works and is the form imported
  (`tuple-in-enum-payload-tp2`).

### N3. `ref` ARGUMENT pattern (`fn f(ref _s: S)`)
- Source: `binding/ref-pattern-drop-behavior-8860.rs`.
- Symptom: `fn f(ref _s: S)` is parsed as a `&`-typed parameter — call site reports
  `expected &<error>, got S`. The `ref` binding-MODE on a function parameter is not
  honored. (Niche; test dropped.)

### N4. `name @ <enum-variant>` at-binding breaks exhaustiveness
- Symptom: `match opt { x @ Some(_) => .., y @ None => .. }` → "match is not
  exhaustive — missing variant(s): Some, None". `@`-binding over a RANGE subpattern
  works (imported as `match-at-binding-range-bn2`); `@` over an enum-variant
  subpattern confuses the exhaustiveness checker into not seeing the variants as
  covered. (Range-`@` form imported instead.)

## Re-confirmed KNOWN-OPEN (NOT re-reported as new — source dropped or distilled)

- `mut` binding modifier inside a tuple/struct destructuring `let`-pattern, a
  destructuring fn-param pattern, and `mut x @ pat`: `let (a, mut b) = ..` →
  `undefined variable 'b'`; `let A { x: mut x } = ..` → the binding is immutable;
  `mut z @ 32` → parse error. ALREADY recorded as B109 gap #9
  (`binding/mut-in-ident-patterns.rs`). Sources dropped.
- `match return X { .. }` — a diverging `return` in the SCRUTINEE position is a
  parse error (`match-bot-2.rs`); parenthesizing `match (return X)` also fails.
  Bot-in-scrutinee unsupported. (Existing `match-bot` covers the diverging-ARM
  form, which works.) Source dropped.
- string-literal `match` patterns (`match s { "a" => .. }`) — known-open parse
  gap; `match-str` / `match-borrowed_str` dropped.
- reference patterns `&Pat` (`let (&x, &y) = (&3, &'a')`) — known-open; use `*x`.
  `borrowed-ptr-pattern-infallible` / `match-unsized` / `match-ref-unsized` dropped.
- never type `!` in patterns (`empty-types-in-patterns.rs`) — out of scope; dropped.
- the `&mut self` stdlib `Drop` does not fire glue; observable-drop tests must use
  the local `trait Drop { fn drop(self: Self) }` by-value idiom (B125 convention).
- bare-rest tuple/tuple-struct patterns `(.., W)` / `(S, ..)` (`pat-tuple-5.rs`),
  and tuple-struct ctors — known-open. Dropped.

## Tests imported (22 total — all compile + link + exit 0)

binding (`-bn2`, 12): match-with-ret-arm (diverging-arm type unification — `return`
in one arm, value in the other), pattern-in-closure (closure params that are a
tuple PATTERN `|(x,_)|` and a struct PATTERN `|Foo{x,y}|`), match-at-binding-range
(`name @ <inclusive-range>` at-bindings + catch-all `@`), match-phi (fieldless-enum
match, per-arm assignment join + a closure invoked in one arm), match-ref-binding-in-guard
(`ref`-binding read inside a match-arm GUARD + re-read in body), match-tag-multi-payload
(enum with 3 tuple-payload variants of differing arity, distinct field bound per arm),
while-let-option (`while let Some(x) = ..` draining loop), let-else-binding
(`let Some(x) = .. else { diverge };` refutable binding), nested-tuple-let
(`let (a,(b,c)) = ..` + `let ((p,q),r) = ..`), match-guard-binding (binding + boolean
GUARD arms over overlapping ranges + literal arm + catch-all), if-let-else-chain
(`if let .. else if let .. else ..`), struct-rest-pattern (struct `..` rest in both a
destructuring `let` and a `match` arm).

tuples (`-tp2`, 10): tuple-in-struct-field (tuple-typed struct field + chained
projection), tuple-in-enum-payload (tuple as enum payload, bound whole + projected),
tuple-eq-compare (structural `==`/`!=` on 2- and 3-tuples), tuple-return-destructure
(fn returning a 2-tuple consumed by destructuring `let`, both branches), tuple-deep-nested-field
(`.1.1.0` / `.1.1.1` chained projection through a 4-deep nested tuple), tuple-index-on-borrow
(`.0`/`.1` on a `&(i64,i64)` reference param), tuple-mixed-types (`(i64, bool, char)`
heterogeneous tuple), tuple-mut-field-write (writing through a tuple field place on a
`mut` local), tuple-arg-ret-chain (3-tuple through fn arg + return, composed twice),
unit-tuple-value (`()` as a value: fn `-> ()` returning `()`, `let u: () = ()`).
