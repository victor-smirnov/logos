# B143 — UI-surfaced gaps

Batch B143 imported 23 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: traits (6), mir (5),
for-loop-while (3), binding (2), self (2), structs-enums (2),
overloaded (1), coercion→dropped, drop (1).
Do NOT modify the compiler/stdlib. All 23 compile + link + exit 0.

Suffix `-b143` on every file (global ctest-name uniqueness). Mined from
less-tapped run-pass areas: tests/ui/{traits, mir, for-loop-while, binding,
self, structs-enums, overloaded, deref, coercion} run-pass tests that earlier
batches had not exhausted, de-duplicated against `RUSTC-PROVENANCE.md`,
`pass/<area>/`, and per-file `Original path:` headers.

## NEW gaps surfaced

### G143-1 — Drop does NOT fire for a value moved into an enum variant then consumed (TRACTABLE)

A `Drop`-bearing struct moved into an enum variant and then consumed by a fn
does not have its destructor run. Isolated repro (distilled `resource-in-struct.rs`):

```
trait Drop { fn drop(self: &mut Self); }
struct CloseRes { cell: *mut bool }
impl Drop for CloseRes { fn drop(self: &mut CloseRes) { unsafe { *self.cell = false; } } }
enum Opt { None_, Some_(CloseRes) }
fn sink(_res: Opt) { }
// ...
{ let r = CloseRes { cell: &mut flag }; sink(Opt::Some_(r)); }
// flag stays `true` — CloseRes::drop never ran
```

A plain block-scoped `{ let r = CloseRes { .. }; }` (no enum) DOES drop correctly
at block exit (verified, flag→false), and the enum value IS consumed by `sink`.
So the destructor scheduling for a value owned through an enum-variant payload —
when the enum is moved into a fn and dropped there (or at the temporary's scope
exit) — is not enqueuing the contained Drop type. Upstream `resource-in-struct.rs`
checks exactly this ("class dtors run if the object is inside an enum variant");
it was DROPPED on this gap.

Tractability: TRACTABLE — missing-case in drop-scheduling for a Drop-typed value
nested inside an enum-variant payload that is moved/consumed. The plain `let`-temp
drop path already fires; the recursive-drop of an enum's owned payload field
(when the enum itself is dropped) isn't propagating to the inner type's `drop`.
Parallel-mapping to the working block-scoped struct drop + the existing
match-arm-binding drop path. NOT a deep (region/representation) gap.

### G143-2 — supertrait methods are NOT resolved through a `&dyn SubTrait` object (TRACTABLE)

Calling an inherited (super-)trait method through a `&dyn Super` trait object
errors: with `trait Super: Base { .. }` and `let s: &dyn Super = &x;`, the calls
`s.foo()` / `s.baz()` / `s.root()` (methods of `Base`/`Base2`/`Base3`) all error
`trait 'Super' has no method 'foo'` — only `Super`'s OWN method `s.bar()` resolves.
The SAME calls on the concrete `&X` receiver all resolve fine (that is the kept
`issue-9394-inherited-calls-b143`, which uses `let s: &X = &n;`). So the gap is
specific to dynamic dispatch: a `&dyn Super` vtable / method-lookup does not
include the supertrait methods.

Tractability: TRACTABLE — missing-case. The supertrait method set is correctly
inherited for static (concrete-receiver) dispatch (4-level chain verified) but the
`&dyn Super` object's method-resolution does not search the supertrait closure.
Parallel-mapping to the working concrete-receiver inherited-call path. Trait-object
upcast/vtable family. NOT a deep gap.

### G143-3 — a LABELED `while let` rejects the label (`break 'l` not in scope) (TRACTABLE)

`'a: while let Some(x) = src { ...; break 'a; }` errors `'break 'a': label not in
scope`. A labeled plain `'a: while cond { break 'a; }` (verified) and a labeled
`'a: loop { break 'a v; }` (label_break_value-b143) both work — so the label
scope is established for `loop`/`while` but NOT threaded through the `while let`
desugar. `while-let-b143` uses a plain `break` for the conditional-break arm.

Tractability: TRACTABLE — missing-case. The `while let` lowering desugars to a
`loop { match { ..break } }` (or similar) but does not register the user's outer
label on the synthesized loop, so `break 'a` cannot find it. Parallel-mapping to
the working labeled-`while` / labeled-`loop` paths. NOT a deep gap.

### G143-4 — an inherent method taken as a fn-pointer VALUE fails (`A::bar` → "unknown enum 'A'") (TRACTABLE)

`let f: fn(&A)->u64 = A::bar;` (an inherent METHOD reified to a fn-pointer value,
no call) errors `unknown enum 'A'` — the `A::bar` path is misparsed as an
enum-variant access rather than a method-item reference. Calling the method
`a.bar()` works; only taking it as a value fails. Upstream `traits/issue-18412.rs`
(`let f = A::bar; let g = Foo::foo; f(&a)`) was DROPPED on this.

Tractability: TRACTABLE — missing-case. The `Type::method` path resolves fine as a
CALL target (`A::bar(&a)` works elsewhere) but not as a value to bind; the
value-position path falls back to enum-variant lookup. Parallel-mapping to the
existing free-fn-item-as-value path (fn items reify to fn-ptrs fine — see
coerce-unify-return). Same family as the method-as-fn-ptr point upstream wants.
NOT a deep gap.

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **operator overloading with a PRIMITIVE LHS** `impl Mul<Foo> for f64` — `val * f`
  (val: f64, f: Foo) errors *"operator '*': right must be numeric, got Foo"*; the
  overloaded `mul` is not dispatched when the left operand is a primitive.
  `binop/binops-issue-22743.rs` DROPPED. (Operator-overload dispatch is keyed to a
  struct LHS, same family as the B117-G1 `&Primitive as &dyn` note.)
- **`@`-binding combined with an enum-variant subpattern** `a @ Some(_)` /
  `ref a @ Some(_)` — a plain `a @ <literal>` binding works, but `a @ Option::Some(_)`
  is reported as a non-exhaustive match (`missing variant(s): Some`), and `ref a @ ..`
  is a parse error. `binding/match-pattern-bindings.rs` + `match-with-at-binding-8391.rs`
  DROPPED. (At-binding-with-subpattern; cf. the existing match-binding notes.)
- **`match` over a TUPLE of C-like enum VALUES** mis-dispatches even by-value
  (B140 G140-2): `match (c, c2) { (Hello, Hello)=>.., (World, Hello)=>.. }` returned
  the first arm for `(World, Hello)`. `mir/mir_adt_construction.rs` (test1) DROPPED.
- **`&mut dyn FnMut` trait-object capturing mutable outer state** segfaults at
  RUNTIME when the closure mutates a captured local across calls (nested or in a
  while loop) — `iterators/iter-range.rs` + `for-loop-while/foreach-nested.rs`
  rewrites used a generic `F: FnMut(..)` bound instead (which works). The `&mut dyn
  FnMut` trait-object of FIXED args (no captured-mut state across calls) works
  (B142 overloaded-calls-object). Fn-family / closure-capture-lifetime area (B107+).
- **labeled BARE block** `'b: { .. }` (not a loop) is a parse error
  (`syntax error near ':'`); `label_break_value-b143` models it with a single-pass
  labeled `loop`. (Logos labels attach to loops only; same surface family as the
  notes on labeled control flow.)
- **`where <ConstructedType>: Trait`** clause `where Option<K>: Sized` is a parse
  error (`syntax error near 'fn'`); `where K: Sized` (bound on a bare type param)
  parses fine. B108/B117-G3 known-open (where-clause on a constructed type);
  `false-ambiguity-where-clause-builtin-bound.rs` DROPPED on this.
- **1-tuple `(x,)`** literal + `let (y,) = x` is a parse error (trailing-comma
  1-tuple, B139/B140 known-open); `tuple/one-tuple.rs` DROPPED.
- **inherent vs (single) trait same-name method** collide on the emitted symbol
  (`duplicate function 'Foo__foo'`, B117-G5); `traits/inherent-method-order.rs`
  DROPPED.
- **`Box<T>`** receivers/fields → stack `T` / `&dyn` (explicit-self-objects-uniq,
  drop-on-empty-block-exit) — B111 known-open.
- **assoc-type projection IN FIELD-TYPE position** `<Global<[u32;2]> as DataBind>::Data`
  → the concrete `[u32;2]` it resolves to (mir-struct-with-assoc-ty) — §B projection
  area; the assoc-type trait is still declared + impl'd, so the decl path is exercised.
- String returns / `format!` / `==`-on-Option/Result → distinct i64 codes / a second
  match (B111/B135 known-open).

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!`/`unreachable!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; integer literals suffixed; negatives `0 - n` (negative
  enum DISCRIMINANTS kept as the literal `-1`).
- `&self`→`self: &Self`/`self: &<Type>`; `&mut self`→`self: &mut <Type>`;
  `mut self`→`mut self: Self`; by-value `self`→`self: <Type>`; `match self`→`match *self`.
- `#[repr]`/`#[derive]`/`Box`/`Vec`/`String`/`format!`/`PhantomData`/`mem::transmute`/
  `static`/`thread`/`channel` facets dropped or distilled where incidental.
- closures given block bodies; explicit lifetime params kept verbatim where present.

## Source dups dropped (checked by exact basename vs RUSTC-PROVENANCE.md + ls pass/)

- `coercion/coerce-unify-return` — ALREADY imported MANY times (B108 base,
  B117 `-co`, B134 `-coe4`). A b143 port was written then dropped.
- `iterators/iter-range` — ALREADY imported (`pass/iterators/iter-range.logos`).
  A b143 port was written then dropped.
- `deref/deref-newtype-method-call` — ALREADY imported (B141 skip-list);
  a b143 port was written then dropped.
- `pattern/issue-8351-1`, `binding/match-pattern-bindings` (also dropped on G143-2-style
  at-binding gap), `traits/inherent-method-order` (collision) — DROPPED.

## Final test set (23)

traits (6): coercion-generic (dispatch through a `&dyn Trait` of a parameterized
trait), multidispatch2 (blanket `impl<T:Zeroish> MyTrait<T> for T` + a concrete
override; concrete wins; `T::zero()` static-through-type-param works), superdefault-
generics (supertrait DEFAULT method `translate` calling the subtrait's required
`x`/`set_x` on self), issue-9394-inherited-calls (4-level supertrait chain
Super:Base:Base2+Base3 incl. a default `foo2`, on a concrete `&X` receiver — dyn
form = G143-2), issue-23825 (generic `pr<T:Stringify>(x:T)` dispatching to the right
primitive impl u32 vs f32), monomorphized-callees-with-ty-params-3314 (generic method
`serialize<S:Serializer>` on i64 + recursively through a generic `F<A>` field),
issue-40085 (overloaded `Index<fn()->i64>` keyed on a fn-POINTER index type, via `s[bar]`).
mir (5): mir-misc-casts (an integer literal cast to every int width + f32/f64),
mir-codegen-calls (pass a value + a tuple through a call, return a `(i64,tuple)` out
value + a single-arg passthrough), mir-match-arm-guard (`Some(xyz) if xyz>100` arm
falling through to a later Some(_)), mir-struct-with-assoc-ty (assoc-type trait
declared/impl'd + an array field write), issue-29227 (trait default method `any3`
ORing several default/overridden `&self` predicates via `||`).
for-loop-while (3): label-break-value (labeled `break 'b` / value-yielding `break 'b v`
via a single-pass labeled loop), loop-break-value (`let v = loop { break N }`, labeled
`break 'outer V`, `break <bool>`), while-let (`while let Some(x)=..` accumulation +
conditional `break`).
binding (2): fn-pattern-expected-type-2 (iterate a borrowed slice of tuples + destructure
each deref'd element), match-ref-binding-mut-option (`Some(ref mut p)` mutating the
payload in place).
self (2): explicit-self-objects-uniq (`&S`→`&dyn Foo` coercion + dispatch + field read),
self-in-mut-slot-default-method (trait default `change(mut self)` mutating self via the
required `set_to(&mut self)` then returning by-value self).
structs-enums (2): enum-discrim-autosizing (explicit enum discriminants incl. negative
`-1` and large `0x8000`/`0x40000`, read via `as i64`), struct-variant-xc-match (match a
struct-variant enum binding its named field, tuple-variant arm present).
overloaded (1): overloaded-index-autoderef (`impl Index<i64> for Foo` so `f[z]` dispatches).
drop (1): drop-on-empty-block-exit (enum-payload value matched with an ignored binding,
exiting the block).
