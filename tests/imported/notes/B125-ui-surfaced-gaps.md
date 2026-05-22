# B125 — UI-surfaced gaps (traits/dyn + drop + impl-trait run-pass)

Batch B125 imported 19 run-pass tests distilling DISTINCT trait-object (`&dyn`),
drop-order/observable-Drop, and `-> impl Trait` features. Source: `tests/ui/traits/`
(dyn/object subset), `tests/ui/drop/`, `tests/ui/impl-trait/` at pinned commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

All 19 compile + link + exit 0. Gaps below were surfaced while distilling and are
classified per docs/DIVERGENCES.md. None block the imported set (each test was
written around the gap or to the working subset).

## NEW gaps (this batch)

### G125-1 — supertrait method not in `&dyn Sub` vtable (§B catch-up)
A `&dyn Sub` where `trait Sub: Super` cannot call the inherited supertrait method
through the trait-object handle:

```
trait Super { fn base(self: &Self) -> i64; }
trait Sub: Super { fn extra(self: &Self) -> i64; }
fn use_sub(s: &dyn Sub) -> i64 { return s.base() + s.extra(); }
//  error [fn use_sub]: trait 'Sub' has no method 'base'
//  error [fn use_sub]: trait 'Sub' has no method 'extra'
```

The dyn method-resolver only searches the named trait's own declared methods; it
does not walk the supertrait chain when building/looking-up the trait-object
vtable. The SAME program works through a generic bound `fn use_sub<T: Sub>(s: &T)`
— so the supertrait machinery exists for static dispatch; only the `&dyn`
resolution path is missing it. §B (Rust resolves supertrait methods on trait
objects). Fix-location: the dyn method-name resolver (mirror the generic-bound
supertrait walk used by the `T: Sub` path). Test withheld
(`dyn-supertrait-dispatch`); re-import on closure.

### G125-2 — `impl Trait` at PARAMETER position rejected (§B catch-up, with diagnostic)
`fn count(a: impl Animal) -> i64` and `fn count(a: &impl Animal)` are rejected:

```
error [fn count]: parameter 'a': 'impl Trait' is not yet supported at parameter
position; use an explicit generic 'fn f<T: Animal>(x: T)' or '&dyn Animal'
```

This is the well-formed APIT (argument-position impl Trait) sugar = `<T: Animal>`
desugar. The diagnostic itself points at the two working alternatives (explicit
generic / `&dyn`). §B catch-up (it is pure sugar over an existing capability).
Note `&impl Animal` additionally fails at the PARSER (`syntax error near 'fn'`) —
`&impl Trait` is not parsed at all. Tests use the explicit-generic form instead.

### G125-3 — `impl Fn(..)->..` return type fails to parse (§B catch-up)
`fn adder(n: i64) -> impl Fn(i64) -> i64 { … }` is a parse error
(`syntax error near 'fn'`). The `impl <Fn-family>(args)->ret` form of return-position
impl-Trait (returning an opaque closure) is not parsed. Non-Fn-family
`-> impl Trait` returns parse + work fine (see the 5 impl-trait/* tests). §B
catch-up. (Returning a *concrete* closure value via the `(|| -> T)` fn-type
already works — see closures/closure-returns-boxed-dyn-cl2.)

## Re-confirmed KNOWN-OPEN (NOT re-reported as new)

- `for s in &arr` / `for s in &v` where the element is `&dyn Trait` SIGSEGVs (the
  double-ref `&&dyn` deref through the borrow-iteration path corrupts the fat
  pointer). Indexed access `arr[i].method()` works — used in
  dyn-array-indexed-dispatch. This is the dyn-element facet of the existing
  "`for x in &coll` yields `&T`" iteration surface; raw `&dyn` element handling
  through the slice/Vec borrow iterator is the missing piece.
- stdlib `&mut self` Drop does not fire glue → the local `trait Drop { fn drop(self: Self) }`
  by-value idiom over a `*mut i64` counter is used for ALL observable-drop tests
  (per the established drop/* convention).
- enum Drop-glue does not fire (only struct Drop-glue) — drop tests use structs.

## WORKING (confirmed this batch — all exit 0)

- `&dyn Trait`: single + multi-method vtable dispatch; as fn arg; returned/selected
  by a fn (conditional pick of one of two impls); stored in a struct field (two
  distinct `&dyn` fields); fixed array `[&dyn Trait; N]` indexed dispatch (two
  distinct impls); `let h: &dyn Trait = &x;` coercion at the let binding; `&mut dyn`
  with a `self: &mut Self` mutating method; a DEFAULT (provided) trait method called
  through the trait object (vtable thunk → required method on self); a dyn method
  result fed into a `match` expression.
- Drop: reverse-declaration drop ORDER of multiple locals (tick-stamped); drop at
  end of EACH loop iteration (N drops); nested-struct field drop (outer + owned
  field both fire); value moved into a fn drops exactly once (no double-drop);
  drop on EARLY return out of a conditional branch; value returned from a fn is
  moved out (NOT dropped in the producer, drops in caller); nested-scope drop
  order (inner before outer, tick-stamped).
- `-> impl Trait` return: basic (concrete value behind opaque return); built from a
  runtime arg (two calls, distinct results); opaque result passed into a generic
  `T: Trait` fn; returned from an inherent METHOD; default-method called on the
  opaque result.
