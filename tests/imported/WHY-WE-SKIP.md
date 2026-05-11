# Why these categories are permanently skipped

[STRATEGY.md](STRATEGY.md) lists categories under "Permanently
skipped" with one-line reasons. This file expands each entry so
future contributors don't have to re-derive the rationale. The
short version: these are either Rust features Logos deliberately
doesn't implement, or rustc-internal tests that aren't about the
language at all.

Distinction with **deferred** (separate section in STRATEGY.md):
deferred items are language features Logos plans to add — their
tests join the import flow once the feature lands. Permanent skips
will never be imported.

## Semantic mismatch — Logos design rejects the Rust mechanism

### `async-await` (461 files)

Rust's async model uses **stackless coroutines** with explicit
`async` / `await` annotations and a state-machine transform. The
async/sync split — "function colour" — propagates through the
entire type system: an async function returns `impl Future<...>`
and can only be called from another async function or via an
executor.

Logos uses **green fibres** instead: every function is implicitly
"async" in the sense that any `.await`-shaped wait point is just a
fibre suspension. No annotations, no `Future` trait, no two
parallel ecosystems. The whole `tests/ui/async-await/` directory
exercises the colour mechanism — it doesn't translate.

If you want the equivalent behaviour in Logos, you write the
synchronous form and the fibre runtime makes it non-blocking
underneath (see `std.sys.fiber`).

### `proc-macro` (360 files)

Rust procedural macros are a separate compilation unit (a "proc-
macro crate") that loads as a dynamic library into the compiler.
The proc-macro crate exports `#[proc_macro]` / `#[proc_macro_derive]`
/ `#[proc_macro_attribute]` functions that take `TokenStream` and
return `TokenStream`.

Logos uses a different but functionally-equivalent design:
`#[fn_macro]` and `#[token_macro]` functions live in the same
module as the rest of your code, run in the metacall JIT at
compile time, and use `quote_expr!` / `quote_item!` for AST
construction. The capability surface is the same (token-in,
AST-out, sema sees the result); the API and packaging are
different. Tests targeting the proc-macro packaging model don't
translate. See [`docs/language/reference/macros.md`](../../docs/language/reference/macros.md).

### `specialization` (113 files)

Rust unstable feature (`#![feature(specialization)]`): a more-
specific `impl` overrides a less-specific one. Without it (today's
stable Rust), overlapping `impl`s are a coherence error.

```rust
impl<T> Print for T { ... }       // blanket
impl Print for i32 { ... }        // specialisation — wins for i32
```

Use cases: optimisation (specialise hot types), better error
messages. Rust hasn't stabilised it in ~10 years because of
soundness issues — a specialised impl can have a longer lifetime
on an associated type than the generic impl promised, breaking
the type system.

Logos doesn't ship specialisation by design. The minority of use-
cases that actually want it can usually be done with a compile-
time `is_same::<T, i32>()` branch (we have that intrinsic) or by
restructuring the trait. The cost-to-soundness ratio doesn't tip.

### `unboxed-closures` (96 files)

Historical Rust nomenclature (~2014, pre-1.0). Original Rust had
**boxed closures**: every closure was a heap-allocated trait
object `Box<dyn FnMut>`. **Unboxed closures** — the modern form —
give each closure a unique anonymous type, inlinable and
monomorphisable.

After 1.0 stabilisation, "unboxed closures" became just
"closures" — there is no other kind. But the `tests/ui/unboxed-
closures/` directory still hosts tests for the **raw mechanics**:
explicit `impl Fn for MyStruct`, `extern "rust-call"` ABI, hand-
written `FnTrait` impls, and other Rust-specific public surface.

Logos has closures (anonymous unique types, inlinable,
monomorphisable — same shape), but **doesn't expose the `Fn` /
`FnMut` / `FnOnce` traits as a public surface** in the same way.
Closures call directly via call-syntax; there's no need to write
`impl Fn for X`. Tests exercising the trait-API mechanics don't
apply.

### `transmute`, `transmutability`, `unsafe-binders`, `unsafe-fields` (small)

Rust offers a family of escape hatches for bit-level type punning:

* `std::mem::transmute<T, U>(x)` — reinterpret bits of T as U.
* `core::mem::TransmuteFrom` trait (unstable transmutability) —
  same, type-checked.
* `unsafe<'a> [...]` (unsafe binders, unstable) — bring lifetimes
  into scope only via an unsafe gate.
* `unsafe<i: i32>` fields (unstable) — fields whose validity
  invariants only an `unsafe` block may rely on.

Logos has `unsafe { }` for the bounded use-cases where bit-level
reinterpretation is necessary, and the Hermes substrate handles
data layout / serialisation for the use-cases that motivate
transmute in idiomatic Rust. We don't ship the four mechanisms
above; the test directories exist primarily for the Rust-specific
forms.

## Not language — rustc-internal or platform-specific

### `bootstrap`, `compiletest-self-test`, `argfile`, `command`, `rustdoc*` (small)

* **bootstrap** — tests for the rustc bootstrap process itself
  (how the compiler builds the compiler that builds the compiler).
  Not about the language.
* **compiletest-self-test** — tests of the test harness.
* **argfile** — tests of `@file` command-line argument expansion.
* **command** — tests of `rustc`'s CLI plumbing.
* **rustdoc***  — tests for the documentation generator.

All of these test rustc's build-and-tooling story, not the Rust
language. Logos has its own tooling (`logosc`, `lforge`); these
tests are not transferable.

### `wasm`, `sanitize-*`, `target_modifiers`, `unwind-abis`, `windows-subsystem` (small each)

Target backends and platform-specific knobs:

* **wasm** — WebAssembly emission corner cases.
* **sanitize-{address,thread,memory,etc.}** — runtime sanitiser
  integration (ASan, TSan, MSan, etc.).
* **target_modifiers** — per-target rustc CLI knobs.
* **unwind-abis** — `extern "C-unwind"` / `extern "system-unwind"`
  ABI variants.
* **windows-subsystem** — Windows-specific PE-subsystem flags.

None of these are about Rust the language; they exercise rustc
backend / target machinery. Logos has its own MLIR → LLVM
pipeline; equivalent tests will appear there if and when we add
those targets.

### `nightly-features`, `rust-2018`, `rust-2021`, `rust-2024` (many)

* **nightly-features** — tests for `#![feature(...)]`-gated
  unstable behaviours. By definition these are not the Rust
  language as it ships.
* **rust-2018 / rust-2021 / rust-2024** — tests for behaviour
  that differs *between editions* (e.g. closure capture rules,
  prelude additions, keyword reservations). Logos has one
  "edition" — its current surface — so edition-differential tests
  don't translate.

### `feature-gates` (273 files)

Tests that verify a feature gate gives a specific error message
("error: this feature is unstable; add `#![feature(...)]` to use
it"). Pure rustc-internal mechanic — Logos doesn't have feature
gates.

### `single-use-lifetime` (small)

A rustc lint that warns when a lifetime is used in only one
place (`fn f<'a>(x: &'a i32) -> i32`, where `'a` is named once
but never reused). It's a lint, not a language rule. Logos doesn't
have an equivalent lint; the underlying language allows the
construct.

## Cross-references

* The **deferred** items (language features Logos plans to add)
  are listed in [STRATEGY.md](STRATEGY.md) under "Deferred —
  implement first, import later".
* The **importable** Tiers 1-3 are listed in [STRATEGY.md](STRATEGY.md)
  with file counts and category notes.
* The import workflow itself is in [README.md](README.md).
