# B120 — UI-surfaced gaps (for-loop-while + enum run-pass)

Source: `tests/ui/for-loop-while/` + `tests/ui/enum/` `//@ run-pass`, distilled
to DISTINCT loop/control-flow + enum features (both corpora are heavily mined
by prior batches — this batch distils features rather than copying
macro/repr/transmute-driven files). All gaps below are **§B catch-up TODOs** —
no new §A blessed divergences. Pinned commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

30 tests imported (all compile + link + exit 0): 15 `-it` (for-loop-while),
15 `-en` (enum).

## NEW gaps (precise, all §B)

### G120-1 — value-carrying labeled break `break 'label <value>` miscompiles
- **Symptom:** COMPILES + links cleanly, but the loop's value is WRONG
  (silent miscompile — a soundness wobble, NOT a clean error).
- **Repro (minimal):**
  ```
  let value: i64 = 'outer: loop { break 'outer 13i64; };
  // value != 13   (program exits nonzero on the `if value != 13` check)
  ```
- **Discriminator:** the UNLABELED form is CORRECT:
  ```
  let value: i64 = loop { break 13i64; };          // value == 13  OK
  let value: i64 = loop { if true { break 13i64; } }; // OK too
  ```
  Only adding the label to the break (`break 'outer 13i64`) loses the value.
  Plain labeled `break 'outer;` (no value) is fine — see WORKING below.
- **Feature:** labeled break carrying a value out of a labeled `loop`.
- **Classification:** §B (Rust: `'l: loop { break 'l v }` yields `v`). This is
  why the prior `loop-labeled-break-value` import deliberately dropped the
  value-carrying labeled breaks. Left UNIMPORTED (would be a silent green over
  a miscompile) — focused-baghunt candidate.

### G120-2 — labeled `break`/`continue` out of a `while let` loop: label not in scope
- **Symptom:** clean compile error
  `error [fn main]: 'break 'a': label not in scope`.
- **Repro (minimal):**
  ```
  'a: while let Some(x) = v.pop() {
      sum = sum + x;
      if x == 2i64 { break 'a; }   // 'a not in scope
  }
  ```
- **Discriminator:** the SAME labels work on plain `loop`, `while`, and `for`
  (labeled-break-inner-it / labeled-continue-outer-it both pass). Only the
  `while let` lowering fails to register its own loop label, so a `break 'a` /
  `continue 'a` inside it cannot find it. Unlabeled `break`/`continue` inside a
  `while let` work fine (while-let-nested-it passes).
- **Feature:** labeled break/continue targeting a `while let` loop.
- **Classification:** §B (Rust allows it). Imported the unlabeled while-let
  forms; the labeled variant left unimported.

### G120-3 — `.iter()` adapter on a fixed-size array `[T; N]`
- **Symptom:** clean compile error
  `error [fn main]: method call: receiver is not a struct (got [i64; 100])`.
- **Repro (minimal):**
  ```
  let x: [i64; 5] = [1i64; 5];
  for pair in x.iter().enumerate() { ... }   // x.iter() rejected
  ```
- **Discriminator:** for-over-`&array` works directly (foreach-external-iterators
  passes, this batch's range-sum-it / nested-loop-continue-it use arrays fine).
  Only the explicit `.iter()` *adapter method* on an array value is missing.
  Additionally `VecIter` (from `Vec::iter()`) has no `enumerate` method
  (`'VecIter$G1$i64' has no method 'enumerate'`), so the
  `array.iter().enumerate()` chain in `foreach-external-iterators-loop` is
  doubly-blocked. Imported the for-over-`&Vec` / for-over-`&array` forms instead.
- **Feature:** `<[T; N]>::iter()` + `Iterator::enumerate` on `VecIter`.
- **Classification:** §B (Rust has both).

## Re-confirmed known-open (NOT re-reported)
- Tuple-struct / struct literal `_` match arms still require a `_` catch-all
  (P4/known-open) — used the catch-all where needed (generic-either-en).
- Payload-binding nested variant `Some(Inner(x))` — known-open; avoided.

## Skipped (feature/surface, not new gaps)
- `enum-discrim-width-stuff`, `issue-42747`, `while-let-2` — `macro_rules!` (§A).
- `enum-univariant-repr`, `enum-size-variance` (introspection axis) —
  `#[repr]` / `mem::size_of` / `transmute` (§A); imported only the
  discriminant-cast and variant-mix cores.
- `issue-19340-1` — `extern crate` auxiliary.
- `cleanup-rvalue-during-if-and-while` — Drop-counting via `static mut` (§A).
- `foreach-external-iterators-loop` (full form) — `array.iter().enumerate()`
  (G120-3); imported the for-over-collection accumulation core instead.
