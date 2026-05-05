# Reference — Roadmap and Known Gaps

This page collects forward-looking notes from the rest of the reference. Items here are *not* implemented (or only partially), and are likely to change as the language stabilises. Each section links into the relevant reference page.

## Lexical

- **UTF-8 source** — identifiers and comments are ASCII-only outside string literals. UTF-8 string contents already work; UTF-8 source is a planned extension. ([Lexical → Source Encoding](lexical.md#source-encoding))
- **Char and byte literals** — `'a'`, `b"..."` are not lexed. Single-byte values are written as integer literals.

## Types

- **`isize` / `usize` as standalone types** in expression position — currently only as integer-literal suffixes. ([Types → Roadmap](types.md#roadmap))
- **Slice-from-array `&arr[..]`** — grammar gap; whole-array borrow `&arr` works.
- **Higher-kinded polymorphism** — out of scope.
- **Mixed packs** — combining `<T...>` and `<const N...: U>` in one signature is rejected by `mono_scan`. ([memory: feat_const_variadic_mvp](../../README.md))

## Items

- **Module-level `pub let`** — parses but `pub` is not honoured by import resolution. Workaround: accessor function. ([memory: feat_pub_const](../../README.md))
- **Tuple struct field access** — limited; named structs are the path with full support.
- **Visibility refinements** (`pub(crate)`, `pub(super)`) — intentionally not on roadmap; flat `pub` only.

## Expressions

- **Range expressions** — `a..b`, `a..=b` parse but only flow through limited contexts (slicing planned). ([Expressions → Roadmap](expressions.md#roadmap))
- **`if let` chains** — `if let Some(a) = x && let Some(b) = y { ... }` not yet accepted.
- **Method-call generic args** — `xs.iter::<T>()` reserved; deduction usually suffices.
- **Block-expression value capture** — edge cases around `let x = { ... };` with early-return.

## Statements

- **`yield`** — keyword reserved; coroutine-yield form planned alongside stackful-fiber lowering.
- **`async` / `await`** — reserved for the wasm32 stackless-coroutine path; not on near-term native roadmap.

## Patterns

- **Exclusive range patterns** — `0..n` not yet a pattern form (only `..=`).
- **Char / byte patterns** — pending the char primitive.
- **`name @ ..`** in slice rest — not yet supported.
- **Pattern types in function parameters** — fragile under generics.

## Generics & Traits

- **Type-argument deduction at call sites** — turbofish currently mandatory; deduction planned. ([Generics & Traits → Roadmap](generics-traits.md#roadmap))
- **Default type parameters** — only special-cased in stdlib.
- **GATs** — partially supported (`type Item<U>;`); some bound forms still rough.
- **Mixed packs** — `<T..., const N...: U>` rejected.

## Ownership

- **Non-lexical lifetimes** — basic NLL works; some early-drop / branch-merge cases still over-conservative.
- **`Pin<T>` / self-referential structs** — not yet a language feature; coroutine prerequisite.
- **Cross-package borrow inference** — `pub fn` annotates lifetimes explicitly today; cross-package elision planned.

## Metaprogramming

- **Phase 2 transformative passes** — design-only.
- **`metacall` captures** — surrounding-fn locals are explicitly out of scope (compile-time evaluation) and rejected by sema. Hoist to `pub const` or pass as a metacall arg.
- **`std::meta` module** — formal API surface; currently scattered across `std.compiler.metaprog.*`.
- **Constant-folding through `metacall`** — folder will treat `metacall` as a first-class producer; today only literal-args flows fold reliably. ([memory: feat_const_fold_metacall](../../README.md))
- **Hygiene strengthening** — gensym for opaque names planned.

## Hermes

- **`Set<T>`** — currently approximated via `ObjectMap<K, null>`; a real `Set` planned. ([memory: feat_hermes_set_via_objectmap](../../README.md))
- **Decimal view refactor** — `to_string_value` / `to_f64` currently on `*const Decimal`; target is `DecimalView`. ([memory: project_decimal_view_todo](../../README.md))
- **Cross-language Hermes** — three-impl strategy (Logos / Rust / C++) deferred until Hermes API stabilises in Logos.

## Attributes

- Most attributes beyond `#[type_code]`, `#[zoned]`, `#[derive(...)]`, `#[annotation]`, and `#[tag_dispatch(...)]` are planned but not honoured by sema today. ([Attributes → Roadmap](attributes.md#roadmap))
- Unknown attributes silently no-op; a warning, then a hard error, will land once the supported set is frozen.

## Larger-Scale Items

These are language-level features in design but not in the grammar / sema today:

- **Stackful coroutines with implicit suspend** — fibers as first-class values, FSM-lowered where escape analysis allows. ([memory: feat_coroutines_design](../../README.md))
- **Auto traits** — Rust-style `Send` / `Sync`-like markers compiler-implemented from field types. ([memory: feat_auto_traits](../../README.md))
- **Function / method overloading** — strict exact-type resolution; no implicit conversions. ([memory: feat_overloading](../../README.md))
- **Bitwise intrinsics** — `popcount`, `clz`, `ctz`, `bswap` via LLVM intrinsics. ([memory: feat_bitwise_intrinsics](../../README.md))
- **Constraint-solving via Z3** — embedding solver for trait resolution and reward signal. ([memory: project_constraint_solving_priority](../../README.md))
- **Definition-centric translation units** — files become sources, content hash becomes identity. ([memory: feat_definition_centric_tu](../../README.md))
