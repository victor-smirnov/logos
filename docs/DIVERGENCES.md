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
| ~~B1~~ | ~~generic `T: Copy` is treated as move~~ **DONE 2026-05-22** | a `T: Copy` bound makes `T` `Copy` — used-after-pass stays valid | FIXED: `is_move_type(TypeVar)` now returns false when the type-param carries an explicit `Copy` bound (via `current_type_bounds_`). Copy⊥Drop makes this sound; only explicit `Copy` counts (Rust parity); non-Copy TypeVars stay conservative-move. See "Recently caught up". | ✅ done |
| B2 | custom DST tail-slice `struct Foo { hdr: H, tail: [T] }` — only `&Foo` references; no by-value read/construct | full custom-DST layout + construction | layout + alloc codegen for a trailing unsized field; was "deferred indefinitely" in `stdlib/imported/DIVERGENCES.md` — RECLASSIFIED as catch-up with a plan. | medium (rare in idiomatic code, common in `alloc` internals) |
| B3 | `Box<T: ?Sized>` (heap unsized) unsupported | `Box<dyn Trait>` / `Box<[T]>` by value | depends on B2 (custom-DST/sized-via-Box layout). | medium |
| B5 | **Dynamic-slice patterns — mostly DONE.** Remaining: *named* element bindings in a slice pattern **nested inside a tuple pattern** (`match x { (2, [a, b]) => a+b }`). | Rust slice patterns | DONE (2026-05-22): top-level `match s { [a, b] => …, [h, ..] => … }` over a `&[T]` scrutinee — refutable on length, binds elements through the data ptr; AND nested-in-tuple length **discrimination** (`(2, [_, _])` checks the slice length). Both in the match-AS-EXPRESSION codegen (`gen_expr_kind(EMatchExprView)`) — note statement-position matches lower to `EMatchExpr` too, so that one site covers both. match-tuple-slice now imported + passing. **Remaining gap**: nested-in-tuple slice patterns with *named* elements (`(2, [a, b])`) — sema's tuple-element handler (sema_stmt.cpp:2740-2804) builds the slice sub via the PAT_OR fallback (so length dispatch works) but declares only a top-level `_` binding, never the nested names → clean "undefined variable 'a'" (NOT a miscompile). Fix = declare nested-slice prefix names in scope (bind_pattern_ref recursion into the Slice sub) + the corresponding nested-slice binding GEP in the tuple binding extractor. The `&[_, _]` ref-slice surface stays a Logos `[_, _]` (in Logos `&[T]` is one Slice type — no separate deref in the pattern). | low (only the named-nested-in-tuple case left) |
| B6 | slice mutability is not tracked at the type level — `&[T]` and `&mut [T]` both canonicalise to `Kind::Slice`, so an indexed WRITE through an immutable `&[T]` is NOT rejected | Rust: `&[T]` is read-only; only `&mut [T]` permits `a[i] = v` | Add a mut bit to `Kind::Slice` (the schema `MUT_PTR` field already exists; `make_slice_type` would set it from `&`/`&mut` at resolve + subst sites) and reject writes through a non-mut slice in `lower_index_write`. Broad/regression-prone: `str` is `Slice<u8>`, and the pool would split `&mut [T]`/`&[T]` into distinct TypeRefs — needs a coercion so a `&[T]` param accepts a `&mut [T]` arg. Deferred as its own focused sprint; the index-WRITE codegen + sema acceptance (G162-2) already landed, so `&mut [T]` slice mutation works — only the rejection-of-`&[T]`-write soundness check is outstanding. **Victor 2026-05-24: fix on-site when a real test actually requires `&[T]`-write rejection — don't pre-emptively do the broad type-system change.** | medium (soundness; no test-pass value until a compile-fail import needs it) |
| B4 | accumulated unsupported-syntax / unimplemented features surfaced by imports B106–B114 (pattern features: refutable field/inner sub-patterns, `ref _y @ Pat`, tuple-of-refs `let`, 1-tuple `(z,)`, empty struct-variant `A {}`; `&[T;N]`→`&[T]` coercion; `-128i8` literal range; nullary-closure mutable capture; …) | as Rust | These are plain GAPS, tracked per-batch in `tests/imported/notes/B1xx-ui-surfaced-gaps.md` and the baghunt catalog. Work them in the grind; this row is the pointer, not the list. | rolling |

### Recently caught up (kept for the record — no longer divergences)
- bool bitwise `&` / `|` / `^` — now works (was rejected as "left/right must be integer"). ✅ verified 2026-05-21.
- `&T == &T` — now value-equality (was pointer-equality). ✅ verified 2026-05-21.
- `&[T; N]` → `&[T]` **unsized coercion** — `&named_arr`, `&[lit, …]`, and structurally inside tuple/struct fields (`(i64, &[i64;2])` unifies with `(i64, &[i64])`) all coerce at let/arg/return. ✅ verified 2026-05-21. (Was the headline of B5; only dynamic-slice *patterns* remain — see B5.)
- array pattern `[x, y]` in **match-as-expression** position — was a silent miscompile (bindings read garbage; the expr-form match codegen had no Slice arm-binding case, only the stmt form). Fixed 47413e65. ✅ 2026-05-21. (Fixed-size arrays; dynamic Slice scrutinees are B5.)
- **dynamic-slice patterns** `match s { [a, b] => …, [h, ..] => … }` over a `&[T]` scrutinee + nested-in-tuple length discrimination (`(2, [_, _])`) — ✅ 2026-05-22. Only named-nested-in-tuple bindings remain (see B5).
- **nested variant pattern in enum-variant payload** `Some(Color::Red)` / `Ok(Status::Done)` — ✅ 2026-05-22 (bindingless inner variant gates the arm via a synthesized `match payload { inner => true, _ => false }` guard). Recurring across B107/B112/B114/B116. Needs a catch-all for exhaustiveness (Logos doesn't yet prove finite-enum coverage of guarded arms). Payload-binding inners (`Some(Inner(x))`) still pending.
- **trait-qualified UFCS** `Trait::method(recv, …)` — ✅ 2026-05-22 (dispatch on the first arg's concrete type).
- **bitwise/shift operator overloading on structs** (`& | ^ << >>` → BitAnd/BitOr/BitXor/Shl/Shr) — ✅ 2026-05-22 (arithmetic ops already worked; added the parallel mappings).
- **nested tuple destructure-let** `let (a, (b, c)) = t` (and deeper) — ✅ 2026-05-22 (grammar `pat_binding` += nested tuple; `lower_let_destruct` recurses). Was "supports struct patterns only".
- **generic `T: Copy` auto-copy** (was §B1) — a `T: Copy` bound now makes `T` Copy; by-value use of `x: T` no longer moves it. ✅ 2026-05-22 (`is_move_type` checks the type-param's Copy bound). Non-Copy generics stay move-checked.
- **closure `|mut x|` / `|mut x: T|`** mutable param bindings — ✅ 2026-05-22 (grammar + closure-lowering synth-param/prologue, mirrors fn `mut` params). Nullary capturing closures mutating an outer `mut` local (`|| { hit += 1; }`) also work now.
- **`for x in &coll` yields `&T`** (not `T` by value) — was a soundness divergence (moving out of a borrow; only sound for Copy types). Now Rust-correct for slices, arrays (via slice coercion), and `&Vec` (via new `Vec::as_slice`). ✅ 2026-05-22. By-value `for x in coll` (owned array / `Vec` via VecIter) still consumes/yields `T` — also Rust-correct. `&mut` iteration yielding `&mut T` not yet wired (for-each `&mut` path is separate).

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
