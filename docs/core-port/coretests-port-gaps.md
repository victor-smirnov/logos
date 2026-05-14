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
| Compiler (CP-cm-*)    | 15    | 10       | 3       | 2    |
| Stdlib (SL-sl-*)      | 10    | 4        | 4       | 2    |
| **Total**             | 25    | 14       | 7       | 4    |

(CP-cm-04 reclassified 2026-05-14 as Divergence — Logos's design uses
`metacall` for compile-time evaluation; `const fn` keyword isn't going
to be added. Not counted in any of the columns above; design-level
divergence, not a gap.)

Closures so far (chronological):
- 2026-05-13 — CP-cm-03 (prelude shorthand), SL-sl-07 (ToString)
- 2026-05-14 — CP-cm-09 (multi-param generic enum mono), SL-sl-04 (Result surface),
  CP-cm-10 (method-generic via fn-ptr), SL-sl-06 (bool conv),
  CP-cm-11 (str == str), CP-cm-13 (None-receiver inference),
  CP-cm-12 (method-generic on generic enum receivers — Option::and/.map etc.),
  CP-cm-08 (tuple == for primitive fields),
  CP-cm-01 (primitive method dispatch via &Self receiver — auto-deref),
  SL-sl-08 (Debug for tuples — language path; stdlib impls follow-up),
  CP-cm-08b partial (tuple == through Eq trait — struct fields work; str + nested still open)

Still open, high-leverage:
- CP-cm-08b — str / nested-tuple field comparisons (canonicalisation + nested-bound work)
- SL-sl-05 — Iterator adapter surface (incremental; .filter/.count/.for_each landed, .map blocked on baghunt_iter_method_generic_mono)
- SL-sl-02 — full PartialEq/Eq split (deferred — no driver yet)
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
| CP-cm-01 | Primitive method dispatch through `&Self` receiver | ✅ Closed (2026-05-14) | `self.eq(other)` when `self: &i32` produced `mangled_prim = "&i32__eq"` which wasn't registered (the impl is on `i32`). Sema then errored with `receiver is not a struct (got &i32)`. **Fix:** sema_expr.cpp lower_method_call now auto-derefs `&T` / `&mut T` receivers when a method is registered on the pointee — looks up `<pointee>__method`, emits `deref(recv)` if the target method takes self by value, leaves recv as-is when the target takes `&Self`. Regression test `tests/logos/pass/primitive_method_through_ref.logos` (i32 via `&i32` → `.eq` / `.ne` / `.cmp`). Stdlib `Eq.ne` default body reverted from `(*self).eq(other)` workaround to natural `self.eq(other)`. | — | — |
| CP-cm-02 | Bare-variant import shorthand (`use core::cmp::Ordering::{Equal, Less, Greater}`) | Open | Rust lets you import enum variants directly so call sites can write `Equal` instead of `Ordering::Equal`. Logos requires the fully-qualified form. | `bool.rs::test_bool` | Port-time workaround: insert `Ordering::` prefix everywhere. |
| CP-cm-03 | Bare-variant shorthand for Option/Result (`Some(x)`, `None`, `Ok(x)`, `Err(x)` without `Option::` / `Result::` prefix) | ✅ Closed (2026-05-13) | Now resolved via prelude shorthand in `lower_call`, `lower_generic_call` (Some/Ok/Err), and bareword VAR_REF + PAT_VARIANT / PAT_VARIANT_DATA (None / variant patterns). When the matching enum is in scope (find_enum_by_name), the bareword routes through enum_lit/enum_lit_data/pat_variant; if a user-defined fn of the same name exists, it wins. | — | — |
| CP-cm-04 | `const fn` keyword | Divergence (by design, 2026-05-14) | Logos has no `const fn` keyword and isn't going to add one. The port rule (`feedback_const_fn_via_metacall.md`): strip `const fn` → `fn`; rewrite Rust const-context call sites (`const X: T = foo();`, array-size, cfg predicate, …) to metacall. Any compile-time-evaluation capability the body needs is a metacall surface gap, not a const-fn gap — file under `feat_metacall_*`. | — | The "Port-time workaround" framing this entry used to have isn't a gap, it's the canonical port. |
| CP-cm-05 | Closure-as-fn-pointer coercion at method args | ✅ Closed (2026-05-14) | Already works via existing `try_coerce_closure_to_fnptr` (sema_impl.hpp ~298) + CP-cm-10's unify_types FnPtr/Closure case + CP-cm-14's tail-expr-as-implicit-return. Verified: `true.then(\|\| { 0i32 })` returns `Some(0)`; `Option<i32>.map(\|x: i32\| { x + 1i32 })` works end-to-end. Remaining sugar (`\|\|` body without braces, `\|x\|` without type) is tracked under CP-cm-14. | — | — |
| CP-cm-06 | `?` operator on Result for non-`std.lang.result` types | Partial | Logos supports `?` for Result; verify it works for ported core::result::Result if path differs. | TBD | — |
| CP-cm-07 | Tuple types in `assert_eq!` mismatch render | ✅ Closed (2026-05-14) | Required three pieces: (a) stdlib Display + Debug impls for tuple arities 1–4 (Display is Logos-specific — Rust's tuples don't impl Display, but Logos's format_args_str bound is `T...: Display + Debug` so both slots need coverage); (b) sema_collect.cpp bound-check recognises tuple types via `<trait>::$tuple$N` impl key with recursive element-level bound check; (c) mono's TypeVar-receiver MethodCall resolution recognises Tuple receivers and emits the `$tuple$N` sentinel mangling + injects the tuple's element types as impl-level type_args. Regression test `tests/logos/pass/assert_eq_tuple.logos` (2- and 3-tuple). | — | — |

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
| SL-sl-05 | Iterator adapter completeness | Partial (2026-05-14) | Logos already had free-fn adapters (iter_map / iter_filter / iter_take / iter_skip / iter_enumerate / iter_chain / iter_zip / iter_count / iter_fold / iter_for_each / iter_sum_i32/i64). 2026-05-14 added trait-default-method shortcuts: `.filter()` / `.count()` / `.for_each()`. Blocked: `.map<R>` / `.fold<B>` etc. — method-generic-on-trait-default-method mono mis-specialises (filed as baghunt_iter_method_generic_mono.md). Still missing: `nth`, `last`, `take_while`, `skip_while`, `rev`, `peekable`, `cycle`, `step_by`, `inspect`, `all`, `any`, `find`, `position`, `min`, `max`, `product`, `collect` (needs FromIterator infra). | Continue incremental port; close mono baghunt to unblock method-generic adapters. | Regression: `tests/logos/pass/iter_method_shortcuts.logos`. |
| SL-sl-06 | Bool conversion methods | ✅ Closed (2026-05-14) | Module `std.lang.bool` provides `then` / `then_some` / `ok_or` / `ok_or_else`. CP-cm-10 closure unblocked all four end-to-end. Driver `bool.rs::test_bool_to_option / test_bool_to_result` ported (`tests/imported/pass/bool/test_harness_coretest_bool_conv.logos`). | — | — |
| CP-cm-10 | Method-generic on primitive receivers — type-param inference through fn-ptr arg | ✅ Closed (2026-05-14) | Root cause was the unify_types switch in sema_expr.cpp: it had no case for `Kind::FnPtr` / `Kind::Closure`, so type-params appearing only inside a `fn() -> T` argument signature stayed unbound and inference reported "could not infer type arg T". **Fix:** added FnPtr/Closure case unifying both `closure_params` and `closure_ret`. The previously-suspected mlir-gen specialisation crash didn't reproduce — likely already cleared by the broader Phase-4a-era mono cleanups. | — | — |
| CP-cm-12 | Method-generic on generic enum receivers — call-site driven specialisation | ✅ Closed (2026-05-14) | **All** generic methods on generic enums with method-level type-params previously failed mlir-gen — `.and<U>`, `.map<U>`, `.and_then<U>`, etc. Root cause: sema's enum-method dispatch path (sema_expr.cpp:4576) hand-rolled receiver-concretization at compile time and emitted a Call with empty type_args. Mono's `instantiate_enum_templates` only handled fns whose `type_params.size()` matched the enum's exactly — methods with extra method-level tparams were silently skipped, producing no specialisation. **Fix:** route enum-method dispatch in sema through `finish_generic_call` (same path used by struct-method dispatch with method-level tparams). The Call carries the canonical template-form callee + full type_args (struct-level prefix + method-level tail). Mono's existing subst_expr `nc.callee = mangle(nc.callee, nc.type_args)` fall-through produces the suffix-mangled name; `enqueue_if_needed` matches this against `templates_[fn.name]` and clones the full template with `{T:concrete, U:concrete}` to produce the fully-monomorphic spec. Zero new mono machinery — leverage what already worked for free generic functions. | — | Stdlib `Option::and<U>` / `Result::and<U>` / `Result::or<F>` restored; `tests/imported/pass/option/test_harness_coretest_option.logos::test_and` + `tests/imported/pass/result/test_harness_coretest_result.logos::test_and / test_or` ported. ctest 3052/3052. |
| CP-cm-14 | Closure param without type annotation (`\|x\| body`) | Partial (2026-05-14) | Grammar's `param` alts all require `COLON type_ref` or a `KW_REF`/`AMP` prefix. So `\|x\| body` doesn't parse — closures need either `\|x: T\|` or `\|x: T\| -> R`. Tail-expression-as-implicit-return now works (`\|x: T\| { x + 1 }` instead of forcing `\|x: T\| { return x + 1; }`). | Grammar: add `closure_param` rule that accepts bare IDENT; sema: infer closure param types from the expected `fn(T,…) -> R` formal at the call site. | Affects every coretest using `\|x\|` shorthand (most of upstream). Workaround: explicit `\|x: T\|`. |
| CP-cm-13 | None-receiver T-inference (let-annotated context) | ✅ Closed (2026-05-14) | Bare `None` constructed Option<TypeVar(T)> unconditionally. **Fix:** sema_expr.cpp VAR_REF "None" handler now consults `hint_enum_type_` (already set by let-binding / return-context machinery) and uses the hint's Option-arg if available. So `let r: Option<i32> = None.or(Some(7))` resolves T at receiver-construction time. | — | Driver test: `tests/imported/pass/option/test_harness_coretest_option.logos::test_or_typed` (now uses bare `None.or(Some(x))` shape). |
| SL-sl-07 | `ToString` trait + `.to_string()` on primitives | ✅ Closed (2026-05-13) | `ToString` trait + blanket `impl<T: Display> ToString for T` + explicit primitive impls (bool, i8-64, u8-64, isize, usize) in `std.lang.text` so primitive method dispatch (which doesn't try blanket impls in Logos's current path) still resolves. Method shape `self: Self` (by value) — primitives are Copy. | — | — |
| SL-sl-08 | `Debug` for tuples | ✅ Closed (2026-05-14) | Added tuple-impl-target sentinel mangling (`$tuple$N` generic blanket, `$tuple$N$<t1>$<t2>…` concrete) in sema_collect.cpp + sema_decl.cpp. Tuple-receiver method dispatch in sema_expr.cpp's lower_method_call mirrors the slice path: positional substitution of method-template type-params from the tuple's element types. Stdlib Debug impls for tuples still need to be added — but the language-level path is in. Regression test `tests/logos/pass/impl_for_tuple.logos` (2- and 3-tuple Debug impls). | — | Driver: tuple.rs `test_show`. Stdlib follow-up: add the `impl<...> Debug for (...)` instances for common arities to `std.fmt`. |
| CP-cm-08 | Tuple `==` (and `!=`) — primitive-field tuples | ✅ Closed (2026-05-14) | gen_binop's `is_ptr_cmp` branch compared the tuple's by-pointer ABI (`Kind::Tuple` → `ptr_type`), returning false for two distinct slots even with equal contents. **Fix:** mlir_gen_expr.cpp emits per-field GEP + load + CmpI (or CmpF for floats), AND'd together. `!=` XORs the AND result with `1` (i1 not). Regression test `tests/logos/pass/tuple_eq_op.logos`. **Limitation:** primitive-only fields; tuples with str / nested-tuple / struct fields still hit the historic pointer-cmp path (CP-cm-08b — separate follow-up entry below). | — | — |
| CP-cm-08b | Tuple `==` for non-primitive fields (struct ✅ / str ◌ / nested tuple ◌) | Partial (2026-05-14) | Routed tuple `==`/`!=` through SL-sl-08's tuple-impl dispatch: stdlib now has `impl<A:Eq, B:Eq> Eq for (A, B)` / `(A, B, C)` / `(A, B, C, D)` blankets that compare element-wise via per-element `Eq::eq`. sema_expr.cpp lower_binop now desugars `tup_a == tup_b` → call to `$tuple$N__eq`, with autoref of lhs/rhs to `&Tuple` since Eq takes `&self`. **Works** for primitive fields (also covered by the CP-cm-08 mlir-gen fast-path) and **for struct fields** (e.g. `(Point, i32) == (Point, i32)` — Eq trait on Point handles the comparison). **Still open:** (a) str fields — Logos's `&str` ↔ `Slice<u8>` canonicalisation asymmetry breaks the `str.eq(&str)` method-dispatch leg ("slice has no method 'eq'"). (b) Nested tuple fields — bound-check doesn't yet recognise that `(i32, i32)` satisfies `A: Eq` through the blanket (needs trait-engine recursive-bound handling). | (a) Audit slice-method dispatch's arg-type canonicalisation, or register `str__eq` with both `(Slice, &Slice)` and `(Slice, Slice)` shapes. (b) Extend `check_type_bounds` to consult tuple-blanket impls when the type-arg is itself a Tuple. | Driver: tuple.rs `test_partial_eq` with nested 3-tuples (still requires nested-tuple bound). Regression test `tests/logos/pass/tuple_eq_via_trait.logos` (primitive + struct fields). |
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
| `cmp.rs::test_int_totalord` | ✅ Closed (B27, 2026-05-14) | n/a |
| `cmp.rs::test_bool_totalord` | ✅ Closed (B27, 2026-05-14) | n/a |
| `cmp.rs::test_isize_totalord` | ✅ Closed (B27, 2026-05-14) — added beyond upstream; stdlib gained `impl Ord for isize` | n/a |
| `cmp.rs::test_mut_int_totalord` | Open | `(&mut 5).cmp(...)` — `&mut` on rvalue temporary |
| `cmp.rs::test_ord_max_min` | Open | `.max(other)` / `.min(other)` missing on primitives (SL-sl-13 — new) |
| `cmp.rs::test_ord_min_max_by / max_by / minmax_by` | Open | `core::cmp::min_by` free fns; closure body capturing into Vec |

## Per-existing-test deferred assertions

Previously committed coretests where specific assertions / sub-tests were skipped:

| Test file | Skipped piece | Blocker |
|---|---|---|
| `binops-bool.logos` | None (full port restored after 2026-05-13 bool-i1-cmp fix) | n/a |
| `binops-bool-corner-cases.logos` | None (same) | n/a |
| `evec-slice.logos` | Slice ordering operators (`<` / `<=` / `>` / `>=`), `println!("{:?}", &[...])` | Slice Ord trait + Display-for-slice |
| `ref-int.logos` | Display-for-&T blanket so `assert_eq!(&x, &y)` works | PartialEq/Display blanket for `&T` (closed for `==`; Display still open) |
