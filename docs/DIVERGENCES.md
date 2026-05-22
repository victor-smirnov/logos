# Logos ↔ Rust divergences — the single source of truth

This is the canonical, language-wide register of where Logos behaves
differently from Rust. It governs the gap-grind triage: **on every failing
imported test you must classify the cause as either a blessed divergence
(§A) or a catch-up TODO (§B) — never leave a silent green or a silent
"that's just how Logos is".**

## The rule (read first)

A difference from Rust is a **blessed divergence** *only* if it is one of:

- **(a) Explicitly replaced** — Rust has a mechanism; Logos provides a
  *different, named* mechanism that covers the capability (e.g. `const fn`
  → `metacall`). The Rust mechanism will not be added.
- **(b) Deliberate design model** — Logos chose a different model on purpose,
  documented, with the capability preserved (e.g. async *colour* → green
  fibres).
- **(c) Logos addition** — Logos has it, Rust doesn't (variadics, Hermes,
  metaprog). Not a parity concern; listed for completeness.

**Everything else that currently differs from Rust is NOT a divergence — it
is a catch-up TODO marked "НЕ ОТКЛАДЫВАТЬ" (§B), with a convergence plan.**
"Edge case rare — defer" / "deferred indefinitely" is a banned classification
(see `feedback_full_rust_parity_now`). The only legitimate non-blessed delay
is an item explicitly gated on a *named* prerequisite roadmap step.

Discipline (`project_gap_grind_strategy`): **no green without an explicit
reason** — a red imported test is either a real gap (→ fix now), a §B catch-up
TODO (→ scheduled, not deferred), or a §A blessed divergence (→ re-import via
the replacement mechanism, e.g. `metacall`-splice; do NOT leave it trimmed).

---

## §A — Blessed divergences (intentional; won't converge to the Rust form)

| # | Area | Rust | Logos | Kind | Notes |
|---|---|---|---|---|---|
| A1 | const-eval | compile-time const folding (miri) | `metacall { … }` (full Logos on JIT, splices a literal) | replaced | const at array-length / enum-discriminant / type position is this. **Re-import the test via metacall**, don't trim. If metacall is rejected in that position → that's a *metacall gap* to fix, NOT a divergence. Const-**generics** `<const N: i64>` are NOT this (they work). See `project_no_const_eval`. |
| A2 | `const fn` | const-evaluable fn | plain `fn`; const-context call sites rewrite to `metacall` | replaced | `feedback_const_fn_via_metacall`. Any inability = a metacall gap, not a const-fn gap. |
| A3 | `macro_rules!` / proc-macros / `#[derive]` | declarative + procedural macros | `metaprog` handlers + `quote_*!` / fn-macros over `metacall` | replaced | The expressive macro layer. `:tt` "any token tree" fragment → Logos requires an explicit fragment choice. |
| A4 | async / await / `Future` / `Pin` | stackless coroutines, function "colour" | green fibres + reactor; every fn is implicitly suspendable, no colour | design model | The *capability* exists (write the sync form; the fibre runtime makes it non-blocking). The colour mechanism itself is intentionally absent. `tests/ui/async-await/*` permanently skipped (`WHY-WE-SKIP`). |
| A5 | rustc-internal attrs: `#[lang]`, `#[rustc_*]`, `#[stable]`/`#[unstable]`, `#[diagnostic::*]` | compiler-internal hooks / stability / diagnostics | distilled to a Logos mechanism (`is_anyval`, `#[hermes_eidos]`, …) or stripped | replaced/N-A | No stability-tracking concept; lang-item roles are wired compiler-side. |
| A6 | (additions) — variadics (`A...`), Hermes datatype/view fabric, metaprog/metacall, fibres | n/a | Logos-only features | addition | Not parity items; here so "Logos has X, Rust doesn't" isn't mistaken for a gap. |

---

## §B — Catch-up TODOs ("НЕ ОТКЛАДЫВАТЬ" — must reach Rust parity)

These currently differ from Rust but are **not** blessed — each needs a fix
that converges to Rust behaviour. They are NOT to be parked as "divergence".

| # | Behaviour today | Rust behaviour (target) | Plan / gating | Priority |
|---|---|---|---|---|
| B1 | generic `T: Copy` is treated as move (conservative), not auto-copied | a `T: Copy` bound makes `T` `Copy` — used-after-pass stays valid | sema move-tracking: when a value's type satisfies `Copy` (incl. a `T: Copy` bound), do not mark it moved on by-value use. Flagged "(divergence)" in B107/B113 notes — RECLASSIFIED here as catch-up. | high (touches many imports) |
| B2 | custom DST tail-slice `struct Foo { hdr: H, tail: [T] }` — only `&Foo` references; no by-value read/construct | full custom-DST layout + construction | layout + alloc codegen for a trailing unsized field; was "deferred indefinitely" in `stdlib/imported/DIVERGENCES.md` — RECLASSIFIED as catch-up with a plan. | medium (rare in idiomatic code, common in `alloc` internals) |
| B3 | `Box<T: ?Sized>` (heap unsized) unsupported | `Box<dyn Trait>` / `Box<[T]>` by value | depends on B2 (custom-DST/sized-via-Box layout). | medium |
| B4 | accumulated unsupported-syntax / unimplemented features surfaced by imports B106–B114 (pattern features: refutable field/inner sub-patterns, `ref _y @ Pat`, tuple-of-refs `let`, 1-tuple `(z,)`, empty struct-variant `A {}`; `&[T;N]`→`&[T]` coercion; `-128i8` literal range; nullary-closure mutable capture; …) | as Rust | These are plain GAPS, tracked per-batch in `tests/imported/notes/B1xx-ui-surfaced-gaps.md` and the baghunt catalog. Work them in the grind; this row is the pointer, not the list. | rolling |

### Recently caught up (kept for the record — no longer divergences)
- bool bitwise `&` / `|` / `^` — now works (was rejected as "left/right must be integer"). ✅ verified 2026-05-21.
- `&T == &T` — now value-equality (was pointer-equality). ✅ verified 2026-05-21.

---

## Process (per imported batch)

1. A failing test needs a feature Logos lacks → **gap**: implement now (or, if it
   maps to §A, re-import via the replacement — e.g. metacall-splice for const).
2. A failing test relies on a §A blessed divergence → re-write the test through
   the Logos mechanism; record it; never leave it trimmed.
3. A behaviour difference that "works but not as Rust intends" → it is §B, not
   silent acceptance. Add a row (or reference an existing one) + a plan.
4. Never silently change semantics to make a port compile. Document in the same
   batch.

## Registers & cross-refs
- `stdlib/imported/DIVERGENCES.md` — coretest/alloc/std per-module divergence
  ledger (this doc is the language-wide superset; keep the architectural rows in
  sync).
- `tests/imported/WHY-WE-SKIP.md` — permanently-skipped categories (async,
  proc-macro, rustc-internal) with rationale.
- `tests/imported/notes/B1xx-ui-surfaced-gaps.md` — per-batch surfaced gaps.
- `docs/baghunt/README.md` — bug/gap catalog (the `Divergence` tag must mean §A).
- Memory: `feedback_full_rust_parity_now`, `project_no_const_eval`,
  `feedback_const_fn_via_metacall`, `project_gap_grind_strategy`.
