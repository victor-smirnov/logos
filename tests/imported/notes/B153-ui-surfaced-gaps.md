# B153 — rustc UI run-pass import: surfaced gaps

Batch B153 imported 26 run-pass tests from `tests/ui/` across fresh, lightly
mined areas: borrowck, regions, inference, moves, mir, self, recursion, deref,
loops, statics. Workflow matches B149–B152: faithful ports, `pub fn main()` →
`fn main() -> i32 { …; return 0; }`, isize/usize → i64/u64, integer/float
literals suffixed, assertions via `use logos.std.fmt;` (`assert!`/`assert_eq!`)
or `if !cond { return N; }` early-returns where Debug/Eq is missing,
Box/vec!/println!/PhantomData/#[derive]/named-lifetimes dropped where
incidental, nested type decls hoisted to top level, `vec![..]` → `vec_new` +
push, recursive-enum-by-value reshaped through raw pointers (Logos has no
auto-boxing).

## STATUS 2026-05-23
✅ **G153-5** empty-String compare SIGSEGV (CRASH) — FIXED 601eb119 (eq_string
compares length+bytes, null-safe). ✅ **G153-4** `Self::method()` static call in
impl body — FIXED de797ac8 (resolve Self in lower_static_call + overwrite stale
Self in lower_fn). REMAINING (3 bigger pieces, workarounds exist):
- **G153-1** backward type-flow: `let mut x = None;` typed from a LATER
  `x = Some(0i64)` leaves Option<T> unresolved → Eq pointer-compares. Known hard
  cluster ([[baghunt_mono_eager_typevar_default_clone]]); needs backward
  inference into the binding type + Eq dispatch. Workaround: annotate.
- **G153-2** bare deferred-init `let mut n;` (type from later assign) parse error
  — COUPLED to G153-1 (grammar trivial, but useless without backward inference).
  Workaround: `let mut n: T;`.
- **G153-3** `static`/`static mut` module items — a real FEATURE (parse + sema +
  codegen for mutable globals + init order). `const` is the partial workaround.
  Dedicated feature session.

All 26 kept tests verified rc=0 against the as-is `build/bin/logosc` (no
compiler changes). The link line uses `-Wl,--gc-sections` so the stdlib's
unreferenced `derive_*_hook` metaprog functions (which reference JIT-only
runtime symbols) get garbage-collected; without it every fmt-using test fails
to link. Tests that hit a gap were re-shaped to preserve the essence (noted
inline + here) or DROPPED.

Areas were chosen to avoid re-importing already-covered upstream tests: 13 of
the initial candidate picks turned out to already be imported in earlier
batches (e.g. `moves/move-2`, `moves/move-4`, `self/by-value-self-in-mut-slot`,
`mir/mir_codegen_switch`, `mir/mir_match_arm_guard`, …) and were skipped.

---

## Gaps surfaced

### G153-1 — `None`-inferred `let` binding leaves the Option payload type unresolved
A `let mut x = None;` whose payload type is only determined by a later
`x = Some(0i64)` assignment leaves the binding typed `Option<T>` with `T`
unresolved. A subsequent `assert_eq!(x, Some(0i64))` then emits a *pointer*
comparison (`arith.cmpi op operand must be integer, but got '!llvm.ptr'`,
mlir-gen failure), and extracting+comparing the payload errors
`operator '!=': type mismatch (T vs i64)`. Adding an explicit annotation
(`let mut x: Option<i64> = None;`) resolves it.

Minimal repro (mlir-gen failure):
```
use logos.std.fmt;
use logos.lang.option;
fn main() -> i32 {
    let mut x = None;
    match x { None => { x = Some(0i64); } Some(_) => {} }
    assert_eq!(x, Some(0i64));   // 'arith.cmpi' op operand must be integer, but got '!llvm.ptr'
    return 0;
}
```
- Workaround in `borrowck/borrowck-pat-reassign-no-binding`: annotate the
  binding `Option<i64>` and extract the payload via `match`.
- Sibling of the historical `None`-inference cluster
  (baghunt_mono_eager_typevar_default_clone): backward type-flow from a later
  assignment into a `None`-initialized binding is not threaded into the Eq /
  operator dispatch.

### G153-2 — bare deferred-init `let mut n;` (type from later assignment) is a parse error
A `let mut n;` with no type annotation, where the type is meant to be inferred
purely from a later `n = 1;`, is a parse error (`syntax error near …` at the
statement after the `let`). The annotated form `let mut n: i64;` (deferred init
with a known type) works.
- Repro: `let mut n; n = 1i64;` → `syntax error near 'fn'` (parser does not
  accept a `let` with neither initializer nor type).
- Workaround in `inference/simple-infer`: add the `: i64` annotation.

### G153-3 — no `static` / `static mut` items
Module-level `static F: T = …;` / `static mut G: T = …;` is a parse error
(`syntax error near 'static'`). `const` items work, but a `const` fn-pointer
(`const F: fn(i64)->i64 = f;`) is separately rejected — const initializers must
be a literal / simple arithmetic / explicit `metacall` (a bare fn reference is
not a const-foldable initializer).
- Repro: `static F: i64 = 42i64;` → `syntax error near 'static'`.
- Workaround in `statics/static-function-pointer`: express the two fn-pointer
  globals as `let` / `let mut` locals inside `main` (preserves the fn-pointer
  call + reassignment, drops the global-storage axis).

### G153-4 — `Self::method()` static-method call inside an impl body is unresolved
A static (no-receiver) method call written `Self::new()` inside another method
of the same impl errors `call to undefined static method 'Self::new'`. Writing
the concrete type name (`Foo::new()`) resolves it; the `-> Self` *return type*
position works fine.
- Repro: `impl Foo { fn new() -> Self {…} fn bar() -> i64 { Self::new().x } }`
  → `call to undefined static method 'Self::new'`.
- Workaround in `self/self-in-method-body-resolves`: use `Foo::new()`.

### G153-5 — comparing against an empty `String::from("")` SIGSEGVs ⚠️ CRASH
`String::from("")` produces a String whose backing buffer is null/unallocated;
comparing it with `==` / `!=` against another String dereferences that buffer
and SIGSEGVs (exit 139) at runtime. Comparison between two *non-empty* Strings
is fine.

Minimal repro (exit 139):
```
use logos.mem.string;
fn main() -> i32 {
    let s = String::from("hej");
    let e = String::from("");
    if s == e { return 1; }   // SIGSEGV (exit 139)
    return 0;
}
```
- The empty-string operand is the trigger (`String::from("hej") == String::from("ho")`
  works). Likely the String Eq impl unconditionally loads the byte buffer
  without a length/null guard, and the empty-literal backing pointer is null.
- Surfaced by `tests/ui/loops/issue-1974.rs`, whose entire point is
  `while s != "".to_string() { … }`; **DROPPED** (cannot preserve the essence
  without the empty-string compare).

---

## Tests DROPPED (and why)

- `tests/ui/loops/issue-1974.rs` — empty-`String` comparison SIGSEGVs (G153-5).
- `tests/ui/mir/issue-78496.rs` — the test's point is the nested by-value
  recursive pattern `if let E::Some(E::Some(_)) = e` over an enum with a
  by-value self payload; Logos rejects the by-value recursive enum
  (`infinite-size enum 'E'`, no auto-boxing) and a raw-pointer reshape loses the
  nested-pattern essence. (Not a new gap — Rust's `Box`/`&` indirection on
  recursive enums is the expected workaround; Logos requires an explicit raw
  pointer field.)
- `tests/ui/self/self-re-assign.rs` — the point is that a self-assignment
  `x = x` of an owned (heap) value works without a glue_drop-before-glue_take
  bug; on a move-typed `String`, Logos's borrow checker rejects `x = x` with
  `use of moved variable 'x'` (the self-assign moves x out of itself). A
  primitive reshape compiles but loses the drop-glue point. (Borderline gap:
  self-assignment of a move type is rejected; left unnumbered as it overlaps the
  move-checker's no-self-move policy.)
- `tests/ui/deref/deref-in-for-loop.rs` — needs `Option::iter()` as an iterator
  source for the for-loop (`method call: receiver is not a struct (got Option)`);
  the Option-iterator + double-ref `for &&x` pattern is the point.
- `tests/ui/overloaded/subtyping-both-lhs-and-rhs-in-add-impl.rs` — Logos's
  `Add` trait is the non-parameterized `Add { fn add(self, rhs: Self) -> Self }`
  (no `Add<RHS>` type parameter, output fixed to `Self`); the test's
  `impl<'a> Add<&'a Foo> for &'a Foo { type Output = (); … }` (distinct RHS +
  associated Output on a reference receiver) is not expressible.
- `tests/ui/borrowck/incorrect-loan-error-on-local-update-5550.rs` — `&str`
  subslice `&s[0..3]` needs `use std.lang.range` plus a `&String`→`&str`
  coercion that Logos's slicing API (`as_str()` returning `str`, `len() -> i64`)
  doesn't provide in the upstream shape; reshape diverges too far.
