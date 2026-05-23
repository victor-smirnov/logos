# B156 — rustc UI run-pass import: surfaced gaps

Batch B156 imported **27 NEW DISTINCT run-pass tests** from `tests/ui/`, mined
for FEATURE COVERAGE across FRESH / lightly-mined areas:
traits (4), structs-enums (4), regions (3), mir (3), closures (3),
inference (2), associated-types (2), overloaded (2), where-clauses (1),
generics (1), type (1), typeck (1).

Workflow matches B149–B155: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0; }`, isize/usize → i64/u64, integer/float literals suffixed,
`assert!`/`assert_eq!` → early-return sentinels (distinct nonzero codes),
println!/derive/Box/Rc/RefCell/Vec/PhantomData/named-lifetimes/static/`#[repr]`
dropped or reshaped where incidental, nested type decls hoisted, Drop modeled via
the established distilled local `trait Drop { fn drop(self: &mut Self); }` + a
`*mut i64` counter. All 27 compile + link + exit 0 against the as-is
`build/bin/logosc` (no compiler changes). Link line uses `-Wl,--gc-sections` (as
for B149–B155).

Operator-overloading / Fn-family traits use the Logos `logos.lang.ops` forms:
`Fn<A...>` / `FnMut<A...>` / `FnOnce<A...>` each declare `type Output;` and a
`call`/`call_mut`/`call_once` method.

Coverage highlights: a blanket `impl<T:Foo> Bar for T {}` makes Bar's default
method callable on a Foo type (impl-trait-chain-14229); a Drop value moved INTO a
struct field is dropped exactly once (init-res-into-things); enum-discriminant
edge wraparound at u8 0/255 and i8 -128/127 (small-enum-range-edge); a blanket
`impl<T:Eq> Equal for T` with a method-level `where U:Eq, X:Eq` (where-clauses);
i8 SwitchInt match (mir-codegen-switchint); a C-repr enum matched in a 2-tuple
scrutinee + tuple-like struct construction (mir-adt-construction); LUB of two
closure→`fn(i64)` across match arms then called (lub-glb-with-unbound-infer-var);
f32 is_nan/is_infinite temp-promotion (mir-temp-promotions); a closure capturing a
struct field formed before the struct is otherwise used (lambda-infer-unresolved);
a lifetime-trait `GetRef` + generic-method `add<G:GetRef>` (regions-early-bound-
used-in-bound-method); a trait impl ON A REFERENCE TYPE `impl Get for &i64`
(rcvr-borrowed-to-region); a generic struct `Cat<U>` with inherent methods + a
method-generic `speak<T>` (class-typarams, class-poly-methods); mutually recursive
enum definitions via raw-pointer indirection (mutually-recursive-types); a
heavily-method'd generic struct with an `Option<&T>`-returning `find`
(class-very-parameterized); Result `and_then`+`unwrap_or_else` chain inference
(inference-method-chain-diverging-fallback); a generic fn bounded by an
associated-type equality `T: SomeTrait<SomeType=i32>` (object-method-numbering);
a blanket `impl<P> Parser for P` with an associated type + default method
(associated-types-issue-21212); a generic struct with multiple impl blocks keyed
on distinct concrete type-args + a fully generic impl (generic-multi-impl-on-type-
arg); RFC 2229 disjoint closure capture over struct fields / tuple elements
(capture-disjoint-field-struct, capture-disjoint-field-tuple, disjoint-capture-in-
same-closure); user structs implementing the Fn-family traits via `call`/
`call_mut`/`call_once` methods (overloaded-calls-simple, overloaded-calls-zero-
args); `Self` in a supertrait bound `trait Float: FuzzyEq<Self>` + static
supertrait method `Float::two_pi()` over f32+f64 (self-in-supertype); a method-
generic trait method called with predicate closures (assignability-trait); a
conditional blanket `impl<M,F:Bar<M>> Foo<M> for F` (where-clause-vs-impl).

## Gaps surfaced

### G156-1 — same type-param trait impl'd twice for one type → duplicate method
A single struct implementing a type-parameterized trait `MyTrait<T>` for two
distinct concrete `T` collides on method mangling (the trait type-arg is not part
of the mangled method symbol). Tractable: same root as trait-aware method
mangling (`baghunt_trait_aware_method_mangling`) but extended to a trait's TYPE
ARGUMENTS, not just trait name.
```
trait MyTrait<T> { fn get(self: &Self) -> T; }
struct MyType { dummy: u64 }
impl MyTrait<u64> for MyType { fn get(self: &MyType) -> u64 { return self.dummy; } }
impl MyTrait<u8>  for MyType { fn get(self: &MyType) -> u8  { return self.dummy as u8; } }
```
Observed: `error [fn MyType__get]: duplicate function 'MyType__get'`.
Decision: **multidispatch1 DROPPED** (this IS the test's whole point).

### G156-2 ⚠️ SILENT MISSED-DROP — tuple-element ✅ FIXED; enum-payload still open
**TUPLE-ELEMENT half FIXED** (this session — see G154-4 in B154 notes): tuple
struct-element storage unified to inline (Copy+move), `ETupleIndex` returns the
element address, and the SDrop Tuple branch drops each owned element exactly once
(with moved-out elements skipped). **ENUM-payload half still OPEN** — needs
variant-switched drop glue (load disc, switch, drop the active variant's
droppable payload fields); planned in `baghunt_enum_tuple_drop_glue`. Original
report follows.

### G156-2 ⚠️ SILENT MISSED-DROP — Drop does not fire for enum-payload / tuple-element
When a Drop-bearing value is moved into an ENUM variant payload or a TUPLE
element and the container is dropped at scope end, the payload/element's `drop`
is NOT run (counter stays 0). The struct-FIELD case fires correctly (counter=1).
```
trait Drop { fn drop(self: &mut Self); }
struct R { i: *mut i64 }
impl Drop for R { fn drop(self: &mut Self) { unsafe { *self.i += 1; } } }
enum T { T0(R) }
{ let _a: T = T::T0(mk_r(i)); }          // i stays 0 — WRONG, should be 1
{ let _a: (R, i64) = (mk_r(i), 0i64); }  // i stays 0 — WRONG, should be 1
{ let _a: BoxR = BoxR { x: mk_r(i) }; }  // i == 1 — CORRECT
```
Observed: silently incorrect (no error/crash). Workaround: init-res-into-things
keeps only the struct-field case; the enum-payload + tuple-element cases dropped.

### G156-3 — a `Type::method` cannot be used as a first-class fn value
Taking `A::bar` (an inherent/impl method) as a `fn(&A)->i64` value fails — the
path is parsed as an enum path. Free fns work as fn values (`let f = bar;`).
```
struct A { v: i64 }
impl A { fn bar(self: &Self) -> i64 { return self.v; } }
let f: fn(&A) -> i64 = A::bar;   // error: unknown enum 'A'
```
Observed: `error [fn main]: unknown enum 'A'`. Decision: **method-as-fn-ptr-18412
DROPPED** (essence is method-pointer equality; not expressible with free fns).

### G156-4 — assoc-type projection `<T as Trait>::Assoc` as an impl self-type
An impl whose target type is an associated-type projection does not parse.
```
trait Int { type T; }
impl Int for i32 { type T = i32; }
impl NonZero for <i32 as Int>::T { ... }   // syntax error near 'impl'
```
Observed: `syntax error near 'impl'`. Decision: **associated-types-projection-
from-known-type-in-impl DROPPED**.

### G156-5 — inherent method NOT preferred over a same-named trait method (collision)
A type having both an inherent method and a trait method of the SAME name
collides on mangling instead of preferring the inherent one (Rust prefers the
inherent). Tractable: relates to G156-1 / trait-aware method mangling — an
inherent method and a trait method should disambiguate.
```
struct Foo {}
trait Trait { fn bar(self: &Self) -> i64; }
impl Foo { fn bar(self: &Self) -> i64 { return 7i64; } }
impl Trait for Foo { fn bar(self: &Self) -> i64 { return 999i64; } }
```
Observed: `error [fn Foo__bar]: duplicate function 'Foo__bar'`. Decision:
**impl-inherent-prefer-over-trait DROPPED**.

### G156-6 — empty type-parameter list `<>` does not parse (decl / turbofish / path)
`fn foo<>()`, `foo::<>()`, and `E::<>::V` are all syntax errors. (Rust treats
`<>` as equivalent to no type params.)
```
fn foo<>() { }            // syntax error near 'fn'
let _e = E::<>::V;        // syntax error near '<'
foo::<>();                // syntax error near '<'
```
Observed: `syntax error`. Decision: **empty-generic-brackets-equiv DROPPED**
(the equivalence relies on `<>` parsing).

### G156-7 ⚠️ DOUBLE-FREE — ✅ FIXED (borrow capture): closure capturing a Drop struct
**FIXED** (this session): a `return` in the closure body called collect_all_drops()
which crossed into the enclosing fn's scope and dropped the borrowed capture (on
each call) on top of its real scope-exit drop. The closure body's scope is now a
DROP BOUNDARY (collect_all_drops stops at it). Regression
`tests/logos/pass/closure_capture_drop_once.logos`. FOLLOW-UP: a `move ||`
closure OWNING a droppable capture now leaks it (needs proper closure capture
drop glue — separate). Original report follows.

### G156-7 (orig) ⚠️ DOUBLE-FREE — closure capturing a Drop-bearing struct double-frees
A closure that captures a struct OWNING a Drop-bearing field (e.g. a `Vec`) by
reference double-frees at runtime (`free(): double free detected`). The
underlying struct's owned Vec is dropped both via the closure's captured copy and
the original. Plain (Copy) struct captures are fine.
```
struct Refs { refs: Vec<i64>, n: i64 }
let e = Refs { refs: vec_new::<i64>(), n: 0 };
let f = || -> i64 { return e.n; };   // capturing e → SIGABRT (double free) on exit
```
Observed: runtime `free(): double free detected in tcache 2`, abort (134).
Workaround: lambda-infer-unresolved replaces the Vec field with a scalar so the
struct is Copy (no Drop) — preserves the closure-formed-before-struct-use essence.

### G156-8/9/13 — ✅ FIXED: bare `Self` in non-plain-struct impl targets
**FIXED** (this session). `lower_impl` now seeds `current_type_params_["Self"]`
from the resolved target (with save/restore) for the impl-target shapes whose
mangled `struct_ctx` name is not a plain struct/datatype/primitive name —
mirroring the pre-existing tuple/fn-ptr seeding. Covers reference targets
(`impl Get for &i64` → Self = &i64), concrete-type-arg targets with no impl
generic-param (`impl Foo<i64>` → Self = Foo<i64>), and blanket impls on a bound
type-var (`impl<F: Bar> Trait for F` → Self = F, for explicit methods; default
methods were already seeded). Regression:
`tests/logos/pass/impl_self_in_nonstruct_targets.logos`. The original workarounds
(explicit `self: &i64` / `self: &Foo<i64>` / `self: &mut F`) remain valid.
NOTE: an adjacent harder gap remains — a blanket `impl<M, F: Bar<M>> Foo<M> for F`
where the trait type-arg `M` must be inferred PURELY from the receiver's bound
(no `M`-typed argument to infer from) still fails to DISPATCH; the imported
where-clause-vs-impl-b156 form (where `M` is inferred from a `msg: M` argument)
works. Original report follows.

### G156-8 — bare `Self` in an `impl Trait for &T` (reference target) errors
An impl on a reference type cannot write `self: Self` / `Self`; the explicit
`self: &i64` works.
```
impl Get for &i64 { fn get(self: Self) -> i64 { return *self; } }  // unknown type 'Self'
impl Get for &i64 { fn get(self: &i64) -> i64 { return *self; } }  // OK
```
Observed: `error [fn $ref_&i64__get]: unknown type 'Self'`. Workaround: rcvr-
borrowed-to-region writes the explicit `self: &i64`.

### G156-9 / G156-13 — bare `Self` in concrete-type-arg / type-var impl targets errors
Same root as G156-8 for two more impl-target shapes:
- a concrete-type-arg inherent impl with NO impl-generic-param: `impl Foo<i64>
  { fn m(self: &Self) }` errors `unknown type 'Self'` (write `self: &Foo<i64>`).
- a blanket impl on a type-var: `impl<M,F:Bar<M>> Foo<M> for F { fn foo(self:
  &mut Self) }` errors `unknown type 'Self'` (write `self: &mut F`).
Observed: `error: unknown type 'Self'`. Workarounds applied in generic-multi-impl-
on-type-arg (G156-9) and where-clause-vs-impl (G156-13). NOTE: bare-`Self` works
fine in a plain `impl Foo {…}` and in `impl<T> Foo<T> { … self: &Self … }`; the
failing shapes are concrete-type-arg-without-impl-param and impl-on-type-var.

### G156-10 ⚠️ SILENT MISCOMPILE — closure mutating a MULTI-LEVEL field path is LOST
A closure that mutates a NESTED field path (`w.p.x += 20`) does not persist the
write — `w.p.x` stays unchanged after the call (no error/crash). SINGLE-level
field mutation in a closure works correctly (`p.x += 20` persists).
```
struct Point { x: i64, y: i64 }
struct Wrapper { p: Point }
let mut w = Wrapper { p: Point { x: 10, y: 10 } };
let mut c = || { w.p.x += 20; };
c();                       // w.p.x stays 10 — WRONG, should be 30
```
Observed: silently wrong (multilevel path captured by-copy). Decision:
**multilevel-path-1 DROPPED**; capture-disjoint-field-struct keeps single-level.

### G156-11 — a user struct impl'ing the Fn-family is not callable via `s(args)` syntax
A struct implementing `Fn`/`FnMut`/`FnOnce` (logos.lang.ops) cannot be invoked
with call syntax `s(3)`; only closures are call-syntax-dispatched. The method
forms `s.call(z)` / `s.call_mut(z)` / `s.call_once(args)` DO work.
```
impl Fn<i32> for S2 { type Output = i32; fn call(&self, z: i32) -> i32 {…} }
let ans = s(3i32);     // error: call to undefined function 's'
let ans = s.call(3i32); // OK
```
Observed: `error [fn main]: call to undefined function 's'`. Workaround:
overloaded-calls-simple / overloaded-calls-zero-args call via the `.call*`
methods.

### G156-12 — method-generic trait method called through a generic fn bound → codegen error
Calling a METHOD-GENERIC trait method (e.g. `iterate<F: FnMut(&A)->bool>`) from
within a GENERIC fn that takes the impl by a trait bound (`length<T:Iterable<i64>>`)
produces an mlir-gen verification failure (GEP on i64). Calling the same method-
generic method directly (non-generic context) works.
```
trait Iterable<A> { fn iterate<F: FnMut(&A)->bool>(self: &Self, f: F) -> bool; }
fn length<T: Iterable<i64>>(x: &T) -> i64 {
    let mut len = 0i64; x.iterate(|_y| { len += 1; true }); len   // codegen error
}
x.iterate(|_y| { len += 1; true });   // OK directly
```
Observed: `'llvm.getelementptr' op operand #0 must be LLVM pointer type … but got
'i64'`. Workaround: assignability-trait drops the generic-fn indirection and calls
`iterate` directly.

## Dropped tests (and why)

- **traits/multidispatch1** — G156-1 (same type-param trait impl'd twice on one
  type → duplicate-method mangling). This is the test's entire point.
- **traits/impl-inherent-prefer-over-trait** — G156-5 (inherent + trait method
  of the same name collide instead of the inherent winning).
- **methods/issue-18412 (method-as-fn-ptr)** — G156-3 (`Type::method` not a
  first-class fn value; cannot be expressed with free fns without losing the
  point).
- **associated-types-projection-from-known-type-in-impl** — G156-4 (`<T as
  Trait>::Assoc` impl self-type does not parse).
- **generics/empty-generic-brackets-equiv** — G156-6 (`<>` does not parse in
  decl / turbofish / path).
- **closures/2229 multilevel-path-1** — G156-10 (⚠️ multilevel-path closure
  mutation silently lost).
- Several already-imported obvious picks were skipped (rec-tup, class-poly-methods
  basenames pre-existed under other batches, generic-tag-values, struct-field-
  shorthand, etc.) — verified against existing basenames before porting.
