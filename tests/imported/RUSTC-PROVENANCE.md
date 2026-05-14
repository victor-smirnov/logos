# rustc Test Import Provenance

This file is the **authoritative manifest** for every test under
[tests/imported/ui/](ui/) — the upstream commit it was sourced from,
the original path in rust-lang/rust, and a one-line summary of any
adaptations made.

When this file is empty (no rows in the manifest table), no tests
have been imported yet — the infrastructure is in place for the
first batch.

## Pinned upstream commit

Imports happen in **batches**; each batch pins a single rustc commit
that all files in that batch were sourced from. When a new batch
starts, a new commit row appears here and is referenced from the
per-file rows below.

| Batch | rustc commit (SHA) | Date | Imported by | Scope |
|---|---|---|---|---|
| B1 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | parser top-level (11 tests) |
| B2 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | borrowck — move/copy/reference (11 tests) |
| B3 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | generics — generic fn / struct / enum / type-alias / fn-ptr (12 tests) |
| B4 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | patterns + match (5 tests; many features deferred — see pattern-match-gaps.md) |
| B5 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | closures (3 tests; bulk blocked on Fn/FnMut/FnOnce — see closures-gaps.md) |
| B6 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | coercion + cast (5 tests) |
| B7 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | methods (3 tests) |
| B8 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | structs + enums + binding (6 tests) |
| B9 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | traits — basic (2 tests; bulk blocked on Fn-trait + default-bodies + dyn-trait) |
| B10 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | consts + inference + typeck (3 tests; end of Tier-1 wide sweep) |

To kick off a batch:

1. Pick a stable, recent rustc commit
   (`git -C ~/src/rust rev-parse HEAD` on a fresh `master` clone, or
   a tagged release like `1.83.0`).
2. Add a row above with the SHA, date, and importer's git
   `user.name`.
3. Land all files in that batch in one commit (or a small commit
   series); each per-file row below references the SHA.

## Per-file manifest

Columns:

* **Our path** — path under `tests/imported/ui/`.
* **rustc path** — original path under `tests/ui/` in rust-lang/rust.
* **Commit** — the batch SHA from the table above (or `(direct)` if
  the import was a one-off outside a numbered batch).
* **Modifications** — one-line summary; matches the per-file
  provenance header in the test itself.

| Our path | rustc path | Commit | Modifications |
|---|---|---|---|
| `pass/parser/as-precedence.logos` | `tests/ui/parser/as-precedence.rs` | B1 | suffixed integer literals; ports as-is via `assert_eq!` |
| `pass/parser/doc-comment-parsing.logos` | `tests/ui/parser/doc-comment-parsing.rs` | B1 | `pub fn main()` → `fn main() -> i32`; bare `5;` → `let _: i64 = 5;` |
| `pass/parser/generics-rangle-eq-15043.logos` | `tests/ui/parser/generics-rangle-eq-15043.rs` | B1 | tuple-struct `S<T>(T)` → named-field `S<T> { v: T }` (Logos has no tuple structs) |
| `pass/parser/integer-literal-method-call-underscore.logos` | `tests/ui/parser/integer-literal-method-call-underscore.rs` | B1 | trait method signature carries explicit `self: Self` |
| `pass/parser/multiline-comments-basic.logos` | `tests/ui/parser/multiline-comments-basic.rs` | B1 | `pub fn main()` → `fn main() -> i32` |
| `pass/parser/nested-block-comments.logos` | `tests/ui/parser/nested-block-comments.rs` | B1 | `pub fn main()` → `fn main() -> i32` |
| `pass/parser/operator-associativity.logos` | `tests/ui/parser/operator-associativity.rs` | B1 | suffixed literals; uses `assert_eq!` |
| `pass/parser/operator-precedence-braces-exprs.logos` | `tests/ui/parser/operator-precedence-braces-exprs.rs` | B1 | suffixed literals; relies on Logos block-as-expression |
| `pass/parser/parse-panic.logos` | `tests/ui/parser/parse-panic.rs` | B1 | `panic!()` / `println!()` → `panic("")` / `let _ = 1;` (function is never called) |
| `pass/parser/reference-whitespace-parsing.logos` | `tests/ui/parser/reference-whitespace-parsing.rs` | B1 | trimmed to `&T` depth 1 (Logos `&&T` / whitespace-tolerant `&` stacking at type position is a tracked grammar gap, see `docs/track3-gaps/parser-gaps.md`) |
| `pass/parser/super-fast-paren-parsing.logos` | `tests/ui/parser/super-fast-paren-parsing.rs` | B1 | `static a: isize = (...)` → `const A: isize = (...)` |
| `pass/borrowck/borrowck-assign-to-subfield.logos` | `tests/ui/borrowck/borrowck-assign-to-subfield.rs` | B2 | nested struct decls hoisted to top level (Logos doesn't permit struct decls inside fn bodies) |
| `pass/borrowck/borrowck-borrow-of-mut-base-ptr-safe.logos` | `tests/ui/borrowck/borrowck-borrow-of-mut-base-ptr-safe.rs` | B2 | trimmed: `let t2: &&mut isize = &t0;` step crashes mlir-gen at runtime (`&&mut T` codegen partial) — tracked gap |
| `pass/borrowck/borrowck-closures-two-imm.logos` | `tests/ui/borrowck/borrowck-closures-two-imm.rs` | B2 | trimmed to fn `a()`: cases `b/c` use `&x` inside closure body (capture-by-ref / addr-of-captured-local) — mlir-gen "& undefined 'x'", tracked gap |
| `pass/borrowck/borrowck-fixed-length-vecs.logos` | `tests/ui/borrowck/borrowck-fixed-length-vecs.rs` | B2 | explicit array type `[i64; 1]` |
| `pass/borrowck/borrowck-mut-vec-as-imm-slice.logos` | `tests/ui/borrowck/borrowck-mut-vec-as-imm-slice.rs` | B2 | `&[isize]` slice arg → `&Vec<i64>` (no implicit Vec→slice coercion); for-loop unrolled to index loop (tracked gap on borrowed for-iter pattern) |
| `pass/borrowck/borrowck-pat-reassign-no-binding.logos` | `tests/ui/borrowck/borrowck-pat-reassign-no-binding.rs` | B2 | `Option::Some` / `Option::None` qualified |
| `pass/borrowck/borrowck-rvalues-mutable.logos` | `tests/ui/borrowck/borrowck-rvalues-mutable.rs` | B2 | explicit lifetime `<'a>` dropped from `inc` — Logos elides |
| `pass/borrowck/borrowck-scope-of-deref-issue-4666.logos` | `tests/ui/borrowck/borrowck-scope-of-deref-issue-4666.rs` | B2 | rust user-struct `Box` renamed `MyBox`; `fun1` (declare-then-init for immut binding) dropped — grammar gap |
| `pass/borrowck/lazy-init.logos` | `tests/ui/borrowck/lazy-init.rs` | B2 | declare-without-init `let mut x: isize;` not accepted — pre-init to 0 (grammar gap tracked) |
| `pass/borrowck/pointer-reassignment-after-deref-78192.logos` | `tests/ui/borrowck/pointer-reassignment-after-deref-78192.rs` | B2 | explicit `as *const u32` cast on `c = d` (no implicit `&T → *const T` coercion) |
| `pass/borrowck/two-phase-baseline.logos` | `tests/ui/borrowck/two-phase-baseline.rs` | B2 | `Vec` via `vec_new::<i64>` + `push`; `assert_eq!` per-element since Logos `Vec` doesn't implement equality vs array literal |
| `pass/generics/generic-fn.logos` | `tests/ui/generics/generic-fn.rs` | B3 | `char` literals + `Triple` Copy semantics dropped (Logos: no auto-Copy for scalar structs); pre-read `p.z` before move |
| `pass/generics/generic-fn-infer.logos` | `tests/ui/generics/generic-fn-infer.rs` | B3 | unchanged |
| `pass/generics/generic-ivec-leak.logos` | `tests/ui/generics/generic-ivec-leak.rs` | B3 | `vec![…]` → `vec_from_arr([…])` (no `vec!` macro yet) |
| `pass/generics/generic-tag-local.logos` | `tests/ui/generics/generic-tag-local.rs` | B3 | turbofish on variant ctor dropped (gap G3-tg-01) |
| `pass/generics/generic-tag-match.logos` | `tests/ui/generics/generic-tag-match.rs` | B3 | turbofish on variant ctor in match-pat dropped (same gap) |
| `pass/generics/generic-tag-values.logos` | `tests/ui/generics/generic-tag-values.rs` | B3 | same — turbofish dropped on variants |
| `pass/generics/generic-temporary.logos` | `tests/ui/generics/generic-temporary.rs` | B3 | fn-pointer type requires explicit `-> ()` (no bare `fn(T)` form) |
| `pass/generics/generic-type.logos` | `tests/ui/generics/generic-type.rs` | B3 | unchanged |
| `pass/generics/generic-type-synonym.logos` | `tests/ui/generics/generic-type-synonym.rs` | B3 | call site exercises the alias |
| `pass/generics/issue-1112.logos` | `tests/ui/generics/issue-1112.rs` | B3 | explicit literal suffixes |
| `pass/generics/issue-2936.logos` | `tests/ui/generics/issue-2936.rs` | B3 | rename `fn cbar(...)` → `cbar_new` to avoid name-shadow of struct |
| `pass/generics/issue-333.logos` | `tests/ui/generics/issue-333.rs` | B3 | explicit `fn(T) -> T` type on the binding for `id::<T>` as fn-ptr |
| `pass/match/guards.logos` | `tests/ui/match/guards.rs` | B4 | trimmed to integer-guard half; struct-pattern + guard half deferred to a later batch |
| `pass/match/match-large-array.logos` | `tests/ui/match/match-large-array.rs` | B4 | trimmed — array-prefix patterns `[1, ..]` not yet supported (gap P4-pm-04) |
| `pass/match/match-on-negative-integer-ranges.logos` | `tests/ui/match/match-on-negative-integer-ranges.rs` | B4 | `if let` form rewritten as `match` (no `if let` sugar in Logos — gap P4-pm-05) |
| `pass/pattern/issue-10392.logos` | `tests/ui/pattern/issue-10392.rs` | B4 | nested struct pattern inside Option payload rewritten as bind-then-destructure (gap P4-pm-02); separate mlir-gen GEP crash on struct-by-value destructure (gap P4-pm-08) workaround |
| `pass/pattern/match-ref-option-pattern.logos` | `tests/ui/match/match-ref-option-pattern.rs` | B4 | `&Some(_)` / `&None` rewritten via auto-deref + qualified path |
| `pass/closures/issue-5239-2.logos` | `tests/ui/closures/issue-5239-2.rs` | B5 | `ref x` in closure param dropped (gap C5-cl-03); explicit param + ret type |
| `pass/closures/no-capture-closure-call.logos` | `tests/ui/closures/no-capture-closure-call.rs` | B5 | unrelated `Box::new(1)` dropped; explicit `-> ()` and `return` |
| `pass/closures/simple-capture-and-call.logos` | `tests/ui/closures/simple-capture-and-call.rs` | B5 | explicit return-type / `return` on the closure |
| `pass/cast/cast-does-fallback.logos` | `tests/ui/cast/cast-does-fallback.rs` | B6 | `(&u8 >> 4)` → `(u8 >> 4)` (no auto-deref of `&u8` at shift-op site, tracked gap) |
| `pass/cast/cast-enum-const.logos` | `tests/ui/cast/cast-enum-const.rs` | B6 | top-level `const` as enum discriminant rejected — inlined literal |
| `pass/cast/cast-region-to-uint.logos` | `tests/ui/cast/cast-region-to-uint.rs` | B6 | `println!("…{:x}…")` dropped (no `{:x}` dispatch on usize yet); cast retained |
| `pass/cast/constant-expression-cast-9942.logos` | `tests/ui/cast/constant-expression-cast-9942.rs` | B6 | `[0; S]` rejected — top-level `const` not yet a valid array length; inlined literal |
| `pass/coercion/basic-ptr-coercions.logos` | `tests/ui/coercion/basic-ptr-coercions.rs` | B6 | explicit reborrow + `as`-casts (Logos has no implicit `&mut→&` / `&→*const` / `*mut→*const` at binding sites); `&<literal>` rejected by temp-lifetime check |
| `pass/methods/inherent-methods-same-name.logos` | `tests/ui/methods/inherent-methods-same-name.rs` | B7 | tuple struct `Foo<T>(T)` rewritten with named field `v` |
| `pass/methods/method-two-trait-defer-resolution-1.logos` | `tests/ui/methods/method-two-trait-defer-resolution-1.rs` | B7 | `Vec::new()` → `vec_new::<T>()` |
| `pass/methods/trait-method-resolution-7575.logos` | `tests/ui/methods/trait-method-resolution-7575.rs` | B7 | trait default bodies dropped (no defaults in Logos traits); method name `new` renamed `mk` — `new` rejected by trait-body grammar (gap M7-mt-01) |
| `pass/structs/large-records.logos` | `tests/ui/structs/large-records.rs` | B8 | unchanged shape — 12-field struct |
| `pass/structs/struct-new-as-field-name.logos` | `tests/ui/structs/struct-new-as-field-name.rs` | B8 | `new` field renamed `new_field` — KW_NEW reserved (gap S8-st-01) |
| `pass/structs/struct-pattern-matching.logos` | `tests/ui/structs/struct-pattern-matching.rs` | B8 | `println!` dropped; match shapes preserved |
| `pass/enum/enum-disr-val.logos` | `tests/ui/enum/enum-disr-val-pretty.rs` | B8 | `imaginary = -1` dropped — negative discriminants not parsed (gap S8-en-01); `String::to_string()` not exercised |
| `pass/binding/exhaustive-bool-match-sanity.logos` | `tests/ui/binding/exhaustive-bool-match-sanity.rs` | B8 | tuple scrutinee `match (x, y)` not supported (gap P4-pm-03) — rewritten as if-chain preserving the truth table |
| `pass/binding/match-bot.logos` | `tests/ui/binding/match-bot.rs` | B8 | turbofish on variant ctor dropped (gap G3-tg-01); `Option::Some(3)` direct |
| `pass/traits/anon-static-method.logos` | `tests/ui/traits/anon-static-method.rs` | B9 | `pub fn new()` → `pub static fn new()` (Logos explicit `static fn` for associated constructors; see logos.peg:780+) |
| `pass/traits/impl-implicit-trait.logos` | `tests/ui/traits/impl-implicit-trait.rs` | B9 | explicit `self: &T` |
| `pass/consts/arithmetic-expr-in-array-len.logos` | `tests/ui/consts/arithmetic-expr-in-array-len.rs` | B10 | arithmetic at array-length position rejected (gap K10-co-01); literal inlined |
| `pass/inference/auto-instantiate.logos` | `tests/ui/inference/auto-instantiate.rs` | B10 | `println!` dropped; type-param inference at call site |
| `pass/typeck/unify-return-ty.logos` | `tests/ui/typeck/unify-return-ty.rs` | B10 | `null` → `null_p` (KW_NULL keyword, gap K10-co-02); `mem::transmute` → `as *const T` cast (K10-co-03) |
| `pass/unsized/unsized.logos` | `tests/ui/unsized/unsized.rs` | B21 | PhantomData removed (Logos has no PhantomData and doesn't need it here); `type TT<T:?Sized> = T;` dropped (Logos type-aliases don't allow bare unsized on RHS yet) |
| `pass/unsized/maybe-bounds-where-cpass.logos` | `tests/ui/unsized/maybe-bounds-where-cpass.rs` | B22 | tuple-struct → named-field (Logos uses named fields throughout); `vec![1,2,3]` → fixed-size array (no vec! macro); `&u[..]` slice-from-array → explicit cast `&buf as *const [u8]`; `where T:?Sized` placed in-bracket as `<T:?Sized>` (Logos accepts both forms; in-bracket is idiomatic) |
| `pass/bool/test_harness_coretest_bool.logos` | `library/coretests/tests/bool.rs` (test_bool only) | B23 | first port from `library/coretests/`; `use core::cmp::Ordering::{Equal, Greater, Less}` → `use std.lang.ord;` (path syntax) + `Ordering::Equal` etc. (no bare-variant re-export — gap CP-cm-02); `use core::ops::{BitAnd, BitOr, BitXor}` → `use std.lang.ops;`; sibling tests `test_bool_not`/`test_bool_to_option`/`test_bool_to_result` deferred (gaps SL-sl-06, SL-sl-08, CP-cm-03, CP-cm-04, CP-cm-05). Full gap catalog: docs/core-port/coretests-port-gaps.md |

## When upstream changes

The manifest pins to a commit, not to `master`. If rust-lang/rust
later changes a test we imported, our copy diverges silently. A
periodic reconciliation sweep:

1. Run a diff tool (TBD) comparing each imported file against the
   current upstream version at the same path.
2. For each non-trivial drift, decide:
   * **Re-import** the new version (replace, bump the manifest row's
     commit).
   * **Keep** our version (rustc tightened a Rust-specific check
     that doesn't apply to Logos).
   * **Retire** our copy if upstream removed the test.

This file is the source of truth for what came from where; keep it
honest.
| B11 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | Phase 3 second-pass — new imports (binop + array-slice-vec, 3 tests) |
| B12 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | expr — block-as-expr / fn-ptr / if-generic / panic / early-return (6 tests) |
| B13 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | array-slice-vec — fixed-length copy / index / repeat / static-array (4 tests) |
| B14 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | array-slice-vec — &[1,2,3] slice from array literal (B-as-01 close); fn — expr-fn variants + fn-ptr-in-struct-field; for-loop-while — break/continue (4 tests) |
| B15 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | binding — match-as-expression (basic / panic / panic-all / fat-arrow over enum) (4 tests) |
| B16 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | expr — return-in-block-tuple (K10-co-04 partial close); match — issue-33498 (tuple pattern arms); functions-closures — fn-bare-{item,assign} (4 tests) |
| B17 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | structs — drop+destructure (dtor-6344, newtype-struct-with-dtor); match — guard with closure / guard with parenthesised AND / char-range guard arms (5 tests) |
| B18 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | generics — derived-type (Pair<T> auto-Copy); regions — escape-into-other-fn / nullary-variant (3 tests) |
| B19 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | coercion — 3 reborrow tests (mut-ptr-arg, imm-ptr-arg, imm-ptr-rcvr); enum — issue-23304-2 (disc literal), match-either-enum-variants-6117 (S8-en-03 close: `_` shorthand for unit-payload variant) (5 tests) |
| B20 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | grab-bag — enum/issue-23304-1 (cross-enum disc), closures/old-closure-fn-coerce, regions/regions-reassign-match-bound-pointer (P4-pm-12 close `mut z`), binding/let-destruct-ref (P4-pm-14 close `let ref y`), binding/fn-pattern-expected-type (C5-cl-06 close closure-ret-infer), binding × 5 (borrowed-ptr-pattern P4-pm-17 close, match-tag, nullary-or-pattern, match-naked-record-expr, nested-pattern, use-uninit-match2, match-phi, match-range, inconsistent-lifetime-mismatch), binding/func-arg-wild-pattern (12 tests) |
| B21 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-13 | Victor Smirnov | unsized — `?Sized` syntax-coverage smoke (Phase 1B-6 validation of trait / struct / enum / impl / fn declaration sites with `?Sized`) (1 test) |
| B22 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-13 | Victor Smirnov | unsized — maybe-bounds-where-cpass (Phase 1B-8 validation: generic struct with `?Sized` field of `*const T` instantiated with bare `[u8]`) (1 test) |
| B23 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-13 | Victor Smirnov | coretests — first port from `library/coretests/tests/`: `bool.rs::test_bool` (PartialEq method form, BitAnd/BitOr/BitXor/Not methods+operators, ordering, `.cmp()` returning Ordering). Phase 4a kickoff. Gap catalog: `docs/core-port/coretests-port-gaps.md`. |
| B24 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-14 | Victor Smirnov | coretests — `option.rs` subset (test_unwrap, test_unwrap_panic1 with `#[should_panic]`, test_unwrap_or, test_is_some_is_none, test_or_typed). Validates Option method surface (SL-sl-03) + prelude shorthand (CP-cm-03). Box/Rc/RefCell/Drop-impl forms deferred; full `.and()` / `.or_else()` / `.or(Some(x))` shapes deferred (None-receiver T-inference gap). |
| B25 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-14 | Victor Smirnov | coretests — `result.rs` subset (test_unwrap_or, test_is_ok_is_err, test_unwrap_err with isize payload, test_expect_panic / test_expect_err_panic / test_unwrap_panic via `#[should_panic]`). Validates Result method surface (SL-sl-04). Surfaced new gap CP-cm-11 (`str == str` returns false even for equal values). Closures / `.and()`/`.or()` / variant-ctor turbofish deferred. |
| B26 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-14 | Victor Smirnov | coretests — `bool.rs::test_bool_to_option` / `test_bool_to_result` (closes SL-sl-06). Validates the four bool conversion methods (`then` / `then_some` / `ok_or` / `ok_or_else`) end-to-end after CP-cm-10 closure (sema's unify_types now handles `Kind::FnPtr` / `Kind::Closure` so type-params inside fn-ptr arg signatures infer correctly). Closures (`\|\| 0`) lowered to named fn refs; `const A: …` const-context uses deferred (no `const fn` — metacall channel instead). |
