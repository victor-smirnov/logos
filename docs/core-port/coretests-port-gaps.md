# Coretests port — outstanding gaps

Working tracker for everything deferred during the Phase 3+4 port of
`rust-lang/rust @ library/coretests/tests/` into Logos.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

## Tally — gaps surfaced & closed by the core port

(Snapshot 2026-05-14. Run `grep -cE "^\| CP-cm-.*✅ Closed" docs/core-port/coretests-port-gaps.md`
etc. to re-tally — counts are derived from the A/B/C tables below.)

| Class                 | Total | ✅ Closed | Partial | Open |
|---|---|---|---|---|
| Compiler (CP-cm-*)    | 14    | 5        | 2       | 7    |
| Stdlib (SL-sl-*)      | 10    | 3        | 3       | 4    |
| **Total**             | 24    | 8        | 5       | 11   |

(CP-cm-14 — closure param without type — added with Partial status:
tail-expr-as-implicit-return now works, bare `|x|` parsing still open.)

Closures so far (chronological):
- 2026-05-13 — CP-cm-03 (prelude shorthand), SL-sl-07 (ToString)
- 2026-05-14 — CP-cm-09 (multi-param generic enum mono), SL-sl-04 (Result surface),
  CP-cm-10 (method-generic via fn-ptr), SL-sl-06 (bool conv),
  CP-cm-11 (str == str), CP-cm-13 (None-receiver inference)

Still open, high-leverage:
- CP-cm-08 — tuple `==` returns false even for equal tuples
- CP-cm-12 — mono drops method-level tparam in enum-typed param signature
- SL-sl-02 — PartialEq / Eq separation
- SL-sl-05 — Iterator adapter surface

Notable closures since this tracker started (2026-05-13):
- CP-cm-03 — bare Some/None/Ok/Err prelude shorthand
- CP-cm-09 — multi-param generic enum method mono (Result method surface)
- CP-cm-10 — method-generic inference through fn-ptr argument
- CP-cm-11 — str == str via stdlib str_eq routing
- CP-cm-13 — None-receiver T-inference via hint_enum_type_
- SL-sl-04 — Result full method surface (post-CP-cm-09)
- SL-sl-06 — bool conversion methods (post-CP-cm-10)
- SL-sl-07 — ToString trait + .to_string() on primitives

Still open (high-leverage):
- CP-cm-08 — tuple `==` returns false even for equal tuples
- CP-cm-12 — mono drops method-level tparam in enum-typed param signature
- SL-sl-02 — PartialEq / Eq separation (Logos's Eq = Rust's PartialEq)
- SL-sl-05 — Iterator adapter surface (collect/fold/take/skip/zip/…)

## Workflow

When porting a coretest:
1. Try the most-faithful Logos form first
2. For each error:
   - If a 1-2 LOC compiler/stdlib fix closes it, do it inline (and document closure here)
   - If larger, add an entry below as `Open`, skip the assertion (or whole sub-test) with a port-time comment in the test file pointing at this doc
3. Commit batch with `feat_phase4X_<test>_port` memory entry

----

## A. Compiler / sema gaps

Surfaced by ports of rustc coretests. Each needs C++ code change in
`src/compiler/` or grammar/parser change.

| ID | Surface | Status | Gap | Surfaced by | Notes |
|---|---|---|---|---|---|
| CP-cm-01 | Primitive method dispatch through `&Self` receiver | Open | `self.eq(other)` when `self` is `&Self` from a trait default body fails with `receiver is not a struct (got &usize)`. Auto-deref to underlying primitive is missing. | `Eq.ne` default body | Workaround: write `(*self).eq(other)` explicitly. Affects every trait default that calls another method on `self`. |
| CP-cm-02 | Bare-variant import shorthand (`use core::cmp::Ordering::{Equal, Less, Greater}`) | Open | Rust lets you import enum variants directly so call sites can write `Equal` instead of `Ordering::Equal`. Logos requires the fully-qualified form. | `bool.rs::test_bool` | Port-time workaround: insert `Ordering::` prefix everywhere. |
| CP-cm-03 | Bare-variant shorthand for Option/Result (`Some(x)`, `None`, `Ok(x)`, `Err(x)` without `Option::` / `Result::` prefix) | ✅ Closed (2026-05-13) | Now resolved via prelude shorthand in `lower_call`, `lower_generic_call` (Some/Ok/Err), and bareword VAR_REF + PAT_VARIANT / PAT_VARIANT_DATA (None / variant patterns). When the matching enum is in scope (find_enum_by_name), the bareword routes through enum_lit/enum_lit_data/pat_variant; if a user-defined fn of the same name exists, it wins. | — | — |
| CP-cm-04 | `const fn` for stable consts | Open | Rust core uses `const fn zero() -> i32 { 0 }` extensively. Logos has metacall for compile-time computation but no `const fn` keyword. | `bool.rs::test_bool_to_option` (`const fn zero`) | Port-time workaround: replace `const fn name(args) -> T { body }` with `pub fn name(args) -> T { body }` for now (loses compile-time-eval guarantee, which most uses don't actually rely on). |
| CP-cm-05 | Closure-as-fn-pointer coercion at method args | Open | `false.then(\|\| 0)` — Rust coerces a non-capturing closure to `fn() -> T` at the method-arg site. Logos's closure-to-fn-ptr path may not fire here. | `bool.rs::test_bool_to_option` (`.then(\|\| 0)`) | Needs investigation. |
| CP-cm-06 | `?` operator on Result for non-`std.lang.result` types | Partial | Logos supports `?` for Result; verify it works for ported core::result::Result if path differs. | TBD | — |
| CP-cm-07 | Tuple types in `assert_eq!` mismatch render | Open | `assert_eq!((1, 2), (1, 3))` — assert_eq! macro needs Debug for tuples. Currently no `impl Debug for (A, B, ...)`. | Anticipated; not yet hit | — |

## B. Grammar / syntax gaps

| ID | Surface | Status | Gap | Notes |
|---|---|---|---|---|
| GR-gp-01 | `!` (never) type at signature position | Open (deferred per user 2026-05-13) | `fn abort() -> !` doesn't parse. Use `enum Never {}` (now in `std.lang.marker`) instead. | Port-time workaround: `-> !` → `-> Never`. |
| GR-gp-02 | `use pkg::{a, b, c}` brace-list import | Open | Rust's grouped use. Logos requires one `use` per item. | Port-time workaround: split into N `use` lines. |
| GR-gp-03 | `pub(crate)` / `pub(super)` visibility | Open | Logos has only `pub`. | Port-time workaround: replace with bare `pub` (slightly broader visibility). |
| GR-gp-04 | Doc body containing literal `/*` text | Open (matches rustc) | DOC_BLOCK depth-counter is text-blind. A body containing the literal characters `/*` will demand an extra `*/`. Rust has the same limitation. | Port-time workaround: spell out as "slash-star" in prose. |

## C. Stdlib gaps

| ID | Surface | Status | Gap | Need | Notes |
|---|---|---|---|---|---|
| SL-sl-01 | `core::ops` arithmetic+shift traits | Open | `BitAnd/BitOr/BitXor/Not` in `std.lang.ops` (closed). Add/Sub/Mul/Div/Rem/Neg/Shl/Shr deliberately deferred — Logos's operator dispatch routes `+` etc. to `Type__add` mangled-name lookup directly. Add the traits when a user-struct overload port needs them. | — | — |
| SL-sl-02 | `PartialEq` / `PartialOrd` separation | Open | Logos's `Eq` shape (`eq(&self, &Self) -> bool`) == Rust's `PartialEq`. Rust's stricter `Eq` has no methods (marker). Logos lacks the distinction. | Rename current `Eq` to `PartialEq`, add empty marker `Eq: PartialEq`. Same for `Ord` / `PartialOrd`. | Touches every existing `impl Eq` in stdlib (~50 sites). |
| SL-sl-03 | Option full method surface | Partial (2026-05-13) | `impl<T> Option<T>` block lands: `is_some` / `is_none` / `unwrap` / `expect` / `unwrap_or` / `unwrap_or_else(fn()->T)` / `map<U>(fn(T)->U)` / `and_then<U>(fn(T)->Option<U>)` / `or` / `or_else(fn()->Option<T>)`. Methods needing `&self` / `&mut self` (`.as_ref` / `.as_mut` / `.take` / `.replace`) deferred — Logos's enum-method dispatch through reference receivers needs work first. `Default` / `Clone`-bounded methods deferred. | — | Known issue: T-inference from `None`-typed receiver fails — `None.or(Some(x))` reports `Option__T__or__g_…` mangled-name link error. Use `let n: Option<i32> = None;` first or pass typed Some. |
| SL-sl-04 | Result full method surface | ✅ Closed (2026-05-14) | With CP-cm-09 fixed, the `impl<T, E> Result<T, E>` block lands cleanly: is_ok / is_err / unwrap / unwrap_err / expect / expect_err / unwrap_or / unwrap_or_else(fn(E)->T) / ok / err / map<U> / map_err<F> / and_then<U> / or_else<F>. | — | — |
| CP-cm-09 | Multi-param generic enum method mono | ✅ Closed (2026-05-14) | Root cause: prelude `Ok(x)` / `Err(x)` shorthand built `Result<payload, TypeVar("E")>` with bespoke result-type construction; let-annotation `Result<i32, i32>` didn't retroactively retype. Method dispatch later saw `Result<i32, E_unbound>`. **Fix:** routed prelude through `lower_enum_lit_data_from_static`, which has proper hint_enum_type_ propagation. Helper itself fixed to accept both flat-array and wrapped-map ARGS shapes. | — | — |
| SL-sl-05 | Iterator adapter completeness | Open | Logos has Iterator trait + a few adapters. Missing: `collect`, `fold`, `sum`, `product`, `count`, `nth`, `last`, `take`, `skip`, `take_while`, `skip_while`, `enumerate`, `zip`, `chain`, `rev`, `peekable`, `cycle`, `step_by`, `inspect`, etc. | Port `core::iter` adapter chain. | Large surface; do incrementally as tests need. |
| SL-sl-06 | Bool conversion methods | ✅ Closed (2026-05-14) | Module `std.lang.bool` provides `then` / `then_some` / `ok_or` / `ok_or_else`. CP-cm-10 closure unblocked all four end-to-end. Driver `bool.rs::test_bool_to_option / test_bool_to_result` ported (`tests/imported/pass/bool/test_harness_coretest_bool_conv.logos`). | — | — |
| CP-cm-10 | Method-generic on primitive receivers — type-param inference through fn-ptr arg | ✅ Closed (2026-05-14) | Root cause was the unify_types switch in sema_expr.cpp: it had no case for `Kind::FnPtr` / `Kind::Closure`, so type-params appearing only inside a `fn() -> T` argument signature stayed unbound and inference reported "could not infer type arg T". **Fix:** added FnPtr/Closure case unifying both `closure_params` and `closure_ret`. The previously-suspected mlir-gen specialisation crash didn't reproduce — likely already cleared by the broader Phase-4a-era mono cleanups. | — | — |
| CP-cm-12 | Mono drops method-level type-param in enum-typed param's canonical-signature suffix | Open (2026-05-14) | `Option<T>::and<U>(self: Option<T>, optb: Option<U>)` type-checks fine but the specialised symbol stays `Option__i32__and__g__Option__Option` — `U` not substituted in the `__g__` suffix (canonical-signature carry-over from declaration). `.map<U>(fn(T) -> U)` works because the param is FnPtr (different mono path). Affects every generic method that takes an enum value (Option/Result/user enums) whose generic args reference method-level type-params. | Trace the mono symbol-generation path for enum-typed params with method-level tparams. | Surfaced by attempt to add `Option::and<U>` / `Result::and<U>/or<F>`. Stdlib methods carry a `GAP CP-cm-12` note; re-add once fixed. |
| CP-cm-14 | Closure param without type annotation (`\|x\| body`) | Partial (2026-05-14) | Grammar's `param` alts all require `COLON type_ref` or a `KW_REF`/`AMP` prefix. So `\|x\| body` doesn't parse — closures need either `\|x: T\|` or `\|x: T\| -> R`. Tail-expression-as-implicit-return now works (`\|x: T\| { x + 1 }` instead of forcing `\|x: T\| { return x + 1; }`). | Grammar: add `closure_param` rule that accepts bare IDENT; sema: infer closure param types from the expected `fn(T,…) -> R` formal at the call site. | Affects every coretest using `\|x\|` shorthand (most of upstream). Workaround: explicit `\|x: T\|`. |
| CP-cm-13 | None-receiver T-inference (let-annotated context) | ✅ Closed (2026-05-14) | Bare `None` constructed Option<TypeVar(T)> unconditionally. **Fix:** sema_expr.cpp VAR_REF "None" handler now consults `hint_enum_type_` (already set by let-binding / return-context machinery) and uses the hint's Option-arg if available. So `let r: Option<i32> = None.or(Some(7))` resolves T at receiver-construction time. | — | Driver test: `tests/imported/pass/option/test_harness_coretest_option.logos::test_or_typed` (now uses bare `None.or(Some(x))` shape). |
| SL-sl-07 | `ToString` trait + `.to_string()` on primitives | ✅ Closed (2026-05-13) | `ToString` trait + blanket `impl<T: Display> ToString for T` + explicit primitive impls (bool, i8-64, u8-64, isize, usize) in `std.lang.text` so primitive method dispatch (which doesn't try blanket impls in Logos's current path) still resolves. Method shape `self: Self` (by value) — primitives are Copy. | — | — |
| SL-sl-08 | `Debug` for tuples | Partial (grammar admits `impl<A,B> Debug for (A,B)` after 2026-05-13; sema doesn't yet recognise tuple as impl-target — emits `impl Debug for : missing method 'dbg'`. Closing needs the tuple-target string-mangling path through sema_collect / sema_decl mirroring the struct/enum/dyn paths) | `impl<A: Debug, B: Debug> Debug for (A, B)` etc. Currently Debug impls only for primitives, str, Ordering. | Sema impl-target machinery + tuple-arity-specific stdlib impls. | Driver: any assert_eq! comparing tuples. Related: CP-cm-08 (tuple ==). |
| CP-cm-08 | Tuple `==` (and `!=`) | Open | `(1i32, true) == (1i32, true)` compiles but returns `false`. Likely a struct-bit-compare codegen path that doesn't match field-by-field. Affects every assert that compares tuples by value. | Investigate gen_binop for tuple LHS in mlir_gen_expr.cpp. | Discovered while filing SL-sl-08. |
| SL-sl-09 | `core::panicking::{AssertKind, assert_failed}` | Open | Rust's `assert_eq!`/`assert_ne!` macros expand to `core::panicking::assert_failed(AssertKind::Eq, &left, &right, None)`. Logos uses `__fmt_panic(msg)` and inlines the message format. | Needed BEFORE Phase 3 (macro_rules) so ported core macros can find their target. | Phase 4b. |
| SL-sl-10 | `core::fmt::{Arguments, Formatter, Write}` proper port | Partial | Logos has `Display`/`Debug` with `fn fmt(self, &mut String)` shape. Rust has `fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result`. Different shape. | Port faithfully OR document divergence + provide compat shim. | Big API surface; touches many trait impls. |
| CP-cm-11 | `str == str` returned `false` even for equal values | ✅ Closed (2026-05-14) | Root cause: lower_binop fell through to the generic comparison path; codegen saw Slice<u8> as a pointer (ptr_type at MLIR level) and did pointer-eq on the slice descriptors, not byte-slice compare. **Fix:** sema_expr.cpp lower_binop now detects Slice<u8> on both sides of `==`/`!=` and routes to stdlib's `str_eq` (the same helper P4-pm-06 uses for str-const patterns). `!=` wraps with unary `!`. Regression test `pass/str_eq_op.logos`. | — | — |

## D. Macros / Phase 3 prerequisites

| ID | Surface | Status | Gap | Notes |
|---|---|---|---|---|
| MC-mc-01 | `macro_rules!` parser | Open (Phase 3) | Declarative macro definition syntax. | Big batch; needs DSL: `$ident:tt`, `$($x:expr),*`, etc. |
| MC-mc-02 | `macro_rules!` expansion | Open (Phase 3) | Pattern-match against TT, substitute, hygienic re-resolve. | — |
| MC-mc-03 | Compiler builtins | Partial | `cfg!` done. Need: `panic!`, `format_args!`, `stringify!`, `concat!`, `concat_bytes!`, `line!`, `column!`, `file!`, `module_path!`, `include!`, `include_str!`, `include_bytes!`, `env!`, `option_env!`, `compile_error!`. | Each ~20 LOC sema, except `format_args!` which is a beast. |

## E. Test-harness gaps

| ID | Surface | Status | Gap | Notes |
|---|---|---|---|---|
| TH-th-01 | Multi-file `#[test]` discovery | Open | `--test` mode walks only the entry file. Tests spread across multiple .logos files in one project would need cross-file collection. | Port-time workaround: keep all `#[test]` fns in the entry file. |
| TH-th-02 | `#[should_panic(expected = "msg")]` | Open | Bare `#[should_panic]` works. Pattern-match form (`expected = "..."`) not supported. | Port-time workaround: drop the `expected` arg. |
| TH-th-03 | `panic!` macro vs `panic` fn naming collision | Open | `panic!(...)` macro in `std.fmt` would shadow `panic(str)` fn from `std.lang.panic` for files importing only `std.fmt`. Filed in fmt.logos comment block. | Port-time workaround: use `panic(msg)` fn directly. Real fix: macro/fn overload-set split. |

## F. Specific coretest deferrals

Tests touched but not fully ported. Will revisit when their blockers close.

| Test | Status | Blocker(s) |
|---|---|---|
| `bool.rs::test_bool` | ✅ Closed (commit `99c9dd55`, 2026-05-13) | n/a |
| `bool.rs::test_bool_not` | Open | Tuple Debug for `assert_eq!((bool,bool))` patterns (SL-sl-08) |
| `bool.rs::test_bool_to_option` | ✅ Closed (B26, 2026-05-14) — `match`-shape adapter; closure `\|\| 0` → named fn (closure-as-fn-ptr at method-arg site still open); `const A: …` const-context use deferred (no `const fn` in Logos) | n/a |
| `bool.rs::test_bool_to_result` | ✅ Closed (B26, 2026-05-14) — same shape as test_bool_to_option | n/a |
| `option.rs::test_unwrap` (subset) | ✅ Closed (B24, 2026-05-14) — Some(int) only | String form deferred (PartialEq for String) |
| `option.rs::test_unwrap_panic1` | ✅ Closed (B24, 2026-05-14) — uses `#[should_panic]` | n/a |
| `option.rs::test_unwrap_or` | ✅ Closed (B24, 2026-05-14) | n/a |
| `option.rs::test_is_some_is_none` | ✅ Closed (B24, 2026-05-14) — added beyond upstream | n/a |
| `option.rs::test_or` (test_or_typed shape) | ✅ Closed (B24, 2026-05-14) — typed-receiver workaround | None-receiver T-inference (note in feat_phase4a_option_methods.md) |
| `option.rs::test_get_ptr/str/resource` | Open | Box, `mem::transmute`, Rc<RefCell<...>>, Drop on R |
| `option.rs::test_option_dance` | Open | Option<Box<T>> juggling |
| `option.rs::test_and / test_and_then / test_or_else (full)` | Open | `.and(other)` method missing; None-receiver T-inference |
| `result.rs::test_unwrap_or` | ✅ Closed (B25, 2026-05-14) | n/a |
| `result.rs::test_is_ok_is_err` | ✅ Closed (B25, 2026-05-14) — added beyond upstream | n/a |
| `result.rs::test_unwrap_err` | ✅ Closed (B25, 2026-05-14); upstream str form restored after CP-cm-11 closure | n/a |
| `result.rs::test_expect_panic / test_expect_err_panic / test_unwrap_panic` | ✅ Closed (B25, 2026-05-14) — `#[should_panic]` | n/a |
| `result.rs::test_and / test_or / test_and_then / test_or_else (full)` | Open | `.and()` / `.or()` missing on Result; closures (`|x|`); variant-ctor turbofish (`Err::<i32, _>(...)`) |
| `result.rs::test_impl_map / test_impl_map_err` | Open | Same closure/turbofish blockers + Eq on Result |
| `result.rs::test_collect` | Open | Iterator::collect into Result<Vec<T>, E> |
| `result.rs::test_fmt_default` | Open | Debug for Result + format! macro |

## Per-existing-test deferred assertions

Previously committed coretests where specific assertions / sub-tests were skipped:

| Test file | Skipped piece | Blocker |
|---|---|---|
| `binops-bool.logos` | None (full port restored after 2026-05-13 bool-i1-cmp fix) | n/a |
| `binops-bool-corner-cases.logos` | None (same) | n/a |
| `evec-slice.logos` | Slice ordering operators (`<` / `<=` / `>` / `>=`), `println!("{:?}", &[...])` | Slice Ord trait + Display-for-slice |
| `ref-int.logos` | Display-for-&T blanket so `assert_eq!(&x, &y)` works | PartialEq/Display blanket for `&T` (closed for `==`; Display still open) |
