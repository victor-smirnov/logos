# B152 — rustc UI run-pass import: surfaced gaps

Batch B152 imported 25 run-pass tests from `tests/ui/` across fresh areas:
numbers-arithmetic, tuple, for-loop-while, methods, binding, match, pattern,
structs-enums, expr, cast. Workflow: faithful ports, `pub fn main()` →
`fn main() -> i32 { …; return 0; }`, isize/usize → i64/u64, integer/float
literals suffixed, `assert!`/`assert_eq!` via `use logos.std.fmt;` (or `if
cond { return N; }` early-returns where Debug is missing, e.g. char/bool),
Box/vec!/println!/panic!/derives/size_of dropped where incidental, nested
type decls hoisted to top level.

All 25 kept tests verified rc=0 against the as-is `logosc` binary (no compiler
changes). The link line uses `-Wl,--gc-sections` (matching `run_test.sh`) so the
stdlib's unreferenced `derive_*_hook` metaprog functions — which reference
JIT-only runtime symbols `logos_emit_item_blob_subst` / `logos_qib_pack_cursors`
— get garbage-collected; without `--gc-sections` every fmt-using test fails to
link. Tests that hit a gap were re-shaped to preserve the essence (noted inline
+ here) or DROPPED.

---

## ⚠️ HIGHEST PRIORITY — SILENT MISCOMPILE

### G152-5 — writing a captured array element inside a closure is LOST ⚠️ SILENT MISCOMPILE
A closure capturing a fixed-size array and **writing** an element (`a[i] = v`)
does not propagate the write to the outer array — the array is captured **by
copy** while scalars and structs are captured **by reference**. Reading a
captured array element works; mutating a captured scalar (`p += i`) works;
mutating a captured struct field (`h.v += i`) works. Only array-element WRITE is
silently dropped.

Minimal repro (should exit 0; outer `a[0]` stays at its initial value):
```
fn main() -> i32 {
    let mut a: [i64; 2] = [0i64, 0i64];
    let mut f = |i: i64| { a[0u64] = i; };
    f(5i64);
    if a[0u64] != 5i64 { return 1; }   // observed: a[0] == 0 (write lost)
    return 0;
}
```
- captured scalar `p += i`        → CORRECT.
- captured struct field `h.v += i`→ CORRECT.
- captured array READ `s += a[i]` → CORRECT.
- captured array WRITE `a[i] = v` → **MISCOMPILE (write discarded)**.
- Surfaced by `tests/ui/for-loop-while/foreach-nested.rs`; that test's whole
  point is mutating a captured array via nested FnMut closures, so it was
  **DROPPED** (cannot preserve the essence without the broken feature).
- Likely root: closure capture-mode classification treats array-typed captures
  as by-value copies rather than by-mut-ref (unlike scalar/struct captures).

### G152-9 — tuple of two `&Enum` matched against struct-variant patterns crashes ⚠️ SEGFAULT / mlir-gen error
Matching a tuple of two enum references against struct-variant patterns either
fails to compile (mlir-gen `'llvm.load' op operand #0 must be LLVM pointer type,
but got 'i32'` when the pattern carries an explicit `&Enum::Foo{..}`) or
SIGSEGVs at runtime (exit 139) when the pattern omits the leading `&`. A SINGLE
`&Enum` matched against a struct-variant pattern works; by-value `Enum` match
works.

Minimal repro (SIGSEGV at runtime):
```
enum Enum { Foo { foo: u64 }, Bar { bar: u64 } }
fn fun1(e1: &Enum, e2: &Enum) -> u64 {
    match (e1, e2) {
        (Enum::Foo { foo: _ }, Enum::Foo { foo: _ }) => 0u64,
        (Enum::Foo { foo: _ }, Enum::Bar { bar: _ }) => 1u64,
        (Enum::Bar { bar: _ }, Enum::Bar { bar: _ }) => 2u64,
        (Enum::Bar { bar: _ }, Enum::Foo { foo: _ }) => 3u64,
    }
}
```
- single `&Enum` struct-variant match (binding the field by ref) → CORRECT.
- by-value `Enum` struct-variant match                          → CORRECT.
- explicit `&Enum::Foo{..}` pattern (single)                    → mlir-gen error.
- tuple `(&Enum, &Enum)` struct-variant patterns                → **SIGSEGV**.
- Surfaced by `tests/ui/match/issue-5530.rs`; **DROPPED** (the tuple-of-refs to
  struct-variant enums is the test's whole point).
- **ROOT CAUSE (2026-05-23, IR-confirmed):** a `&Enum` tuple element is TWO-level
  (ptr-to-enum-heap-ptr, [[ref_enum_two_level_convention]]). The tuple-element
  variant disc test loads the element (`%22` = the `&Enum`) then does
  `GEP %22[0,0] as enum.Enum` — MISSING the second Load to deref the ref to the
  actual enum-struct ptr. The single-`&Enum` match path applies that via_ref Load
  (gen_match ~2342); the tuple-element variant test/extract does not → garbage
  disc → mis-dispatch + payload GEP through garbage → SIGSEGV. FIX: in the
  tuple-element VariantData test + extract (gen_match), when the element type is
  a ref-to-enum, insert the extra Load (deref) before the disc GEP / payload
  extract — mirror the via_ref handling. (The explicit `&Enum::Foo{..}` pattern
  mlir-gen error is the sibling: a ref-pattern wrapping a variant pattern.)

---

## Parse / unsupported-syntax gaps (clean errors, not miscompiles)

### G152-1 — bare-mantissa exponent float literal `5e-11` / `5e9` is a lexer error
Float literals written with an exponent but **no decimal point** are not
recognized (`syntax error near '5'`). Adding a decimal point (`5.0e-11`) works.
- Repro: `let g = 5e-11f64;` → `syntax error near '5'`.
- Workaround in `numbers-arithmetic/floatlits`: `5e-11f64` → `5.0e-11f64`.
- Tractability: trivial (lexer: allow exponent suffix after a bare integer
  mantissa, not only after a fractional one).

### G152-2 — turbofish tuple-struct constructor `Foo::<u64>(5)` — undefined function
A tuple-struct constructor with an explicit turbofish errors `call to undefined
function 'Foo'`. The non-turbofish form (`let a: Foo<u64> = Foo(5u64)`) works.
- Repro: `let a = Foo::<u64>(5u64);` → `call to undefined function 'Foo'`.
- Workaround in `methods/inherent-methods-same-name`: type-annotated `let`.

### G152-6 — `where <CompositeType>: Trait` (bound LHS is an applied type)
A where-clause whose bounded type is an applied/composite type (`where
Option<T>: Foo`) is a parse error (`syntax error near 'Option'`). `where T: Foo`
(bare type param) works.
- Repro: `fn check<T>(x: Option<T>) -> i32 where Option<T>: Foo { … }` →
  `syntax error near 'Option'`.
- Surfaced by `tests/ui/methods/method-where-clause.rs`; **DROPPED** (the
  where-clause on a composite type is the test's whole point).

### G152-7 — turbofish in a PATTERN `Some::<i64>(_)` is a parse error
A turbofish on a constructor used as a pattern (`Some::<i64>(_)`, `S4::<u8>{..}`)
errors `syntax error near '::'`. The plain pattern form works.
- Repro: `match o { T::bar(_i, Some::<i64>(_)) => … }` → `syntax error near '::'`.
- Workaround in `binding/nested-pattern` + `structs-enums/struct-aliases`: drop
  the turbofish (`Some(_)` / use the bare struct name in the pattern).

### G152-8 — turbofish on no-payload variant constructor `None::<i64>`
`None::<i64>` as a constructor expression errors `undefined function in
generic-ref 'None'`. A let-pinned `let n: Option<i64> = None;` supplies it.
- Repro: `nested(T::bar(1i64, None::<i64>))` → `undefined function in
  generic-ref 'None'`.
- Workaround in `binding/nested-pattern`: let-pinned `None`. Sibling of the
  historical turbofish-on-no-payload-variant cluster (see
  baghunt_mono_eager_typevar_default_clone).

### G152-10 — type alias not recognized in a struct PATTERN
A `type S2 = S;` alias used as a struct pattern (`S2 { x, y }`) errors
`struct pattern: unknown struct 'S2'` / `'S2' != scrutinee 'S'`. The alias works
in CONSTRUCTION (`S2 { … }`).
- Workaround in `structs-enums/struct-aliases`: match on the real name `S`.

### G152-11 — GENERIC type alias not recognized even in construction
A generic type alias `type S4<U> = S3<U, char>;` used as a struct literal
(`S4::<u8> { … }`) errors `struct literal: unknown struct 'S4'`.
- Workaround in `structs-enums/struct-aliases`: use the base struct directly.

### G152-13 — `panic!()` is not a callable macro by name
`panic!()` errors `'panic' is not marked #[fn_macro] or #[token_macro]`. Tests
relying on `panic!` for a diverging arm/branch must use `assert!(false)` (changes
the branch type) or a real diverging expression.
- Surfaced widely; benign where the panic arm is unreachable in run-pass tests.

### G152-14 — pattern in a fn PARAMETER position `fn foo((): ())`
A non-identifier pattern as a fn parameter (here a unit pattern `()`) is a parse
error (`syntax error near 'fn'`). (Tuple-destructure params do work via the
`mut x:T` / tuple-param desugar path, but a bare unit/literal pattern does not.)
- Surfaced by `tests/ui/pattern/unit-pattern-matching-in-function-argument-7519.rs`;
  **DROPPED** (the param-position pattern is the test's whole point).

### G152-16 — labeled `break`/`continue` on a `while let` loop — label not in scope
A `'a: while let Some(x) = … { … break 'a; }` errors `'break 'a': label not in
scope`. Labeled break/continue on `loop` / `for` / plain `while` all work
(see `labeled-break`); only the `while let` form drops its label registration.
- Workaround in `for-loop-while/while-let`: unlabeled `break`/`continue`.

---

## Codegen / sema gaps (compile errors, not miscompiles)

### G152-3 — direct field WRITE on a tuple struct `x.0 = …` / `x.0 += …`
Writing a tuple-struct field by index (`x.0 = v`, `x.0 += v`) errors `tuple
field write/compound assign: 'x' is not a tuple (got Point)`. Reads (`x.0`),
`&mut x.0` borrows, and the same writes on a PLAIN tuple all work.
- Repro: `let mut x = Point(3i64,2i64); x.0 += 5i64;` → `'x' is not a tuple`.
- Workaround in `tuple/tuple-index`: route tuple-struct mutation through
  `&mut x.N` field borrows.
- Tractability: the field-write codegen handles `Kind::Tuple` but not a
  tuple-struct (named struct with positional fields) on the write/compound-assign
  path; the read and `&mut`-borrow paths already handle it.

### G152-12 — generic-struct field bound in a PATTERN keeps its type variable
Binding a generic struct's field in a struct pattern (`match s { S3 { x, y } =>
… }` where `s: S3<u8, u16>`) leaves `x`/`y` typed as the template type vars
`U`/`V` rather than the concrete instantiation, so any concrete op (`==`,
`assert_eq!`) errors `operator '!=': type mismatch (U vs u8)`. Field-ACCESS
(`s.x`) resolves to the concrete type correctly.
- Repro: `let s = S3::<u8,u16>{x:4u8,y:9u16}; match s { S3{x,y} => assert_eq!(x,4u8) }`
  → `type mismatch (U vs u8)`.
- Workaround in `structs-enums/struct-aliases`: read fields via `s.x`/`s.y`.

### G152-15 — `..` rest in a tuple-struct / struct `let` pattern
`let Foo(a, b, ..) = …` and `let Bar { b, .. } = …` error `tuple-struct 'let'
pattern: only plain identifier bindings are supported`. The same rest patterns
work in `match` arms (G151-2 fixed those), and `let (a, b, ..)` works for PLAIN
tuples.
- Workaround in `pattern/ignore-all-the-things`: express tuple-struct/struct
  rest patterns via `match` (keep plain-tuple rest in `let`).
