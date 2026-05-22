# B123 — UI-surfaced gaps (tests/ui/iterators + tests/ui/option-result shapes)

Batch B123 imported 31 run-pass tests (20 option-result `-or`, 11 iterators
`-itx`) maximising DISTINCT Option/Result combinator and iterator-adapter
features. All 31 compile + link + exit 0 (verified individually and via the
ctest harness). The batch targets two surfaces absent from the existing imports:

- The existing `pass/option-result/` files are hand-rolled-enum `Opt<T>`/`Res<T,E>`
  shapes plus the `?`-operator harness (`test_harness_coretest_qmark_from`). The
  prelude `Option`/`Result` *combinator* surface (`map`/`and_then`/`map_or`/
  `filter`/`ok_or`/`take`/`flatten`/`map_err`/`ok`/`err`/`and`/`or`/…) was not
  exercised as standalone run-pass before.
- The existing `pass/iterators/` files are entirely `#[test]`-harness coretest
  ports. Standalone `main()->i32` run-pass for for-loop iteration and free-fn
  adapter pipelines (`iter_filter`/`iter_map`/`iter_fold`/`iter_take`/`iter_skip`/
  `iter_chain`/`iter_zip`/`iter_enumerate` over `iter_over_slice(&arr)`) was not
  represented.

## NEW gaps

**NONE.** Every distinct feature targeted in this batch works end-to-end.

The full WORKING surface confirmed this batch (all via the standard verify
recipe: `logosc … -o t.o` → `cc … -llogos-std -llogos-mem -llogos-lang … ` →
`exit 0`):

- Prelude `Option<T>` methods: `map`, `and_then`, `map_or`, `map_or_else`,
  `filter`, `or`, `or_else`, `and`, `ok_or`, `ok_or_else`, `take`, `flatten`,
  `unwrap`, `unwrap_or`, `unwrap_or_else`, `is_some`, `is_none`.
- Prelude `Result<T,E>` methods: `map`, `map_err`, `and_then`, `and`, `or`,
  `unwrap`, `unwrap_or`, `unwrap_or_else`, `is_ok`, `is_err`, `ok`, `err`.
- Chained combinator pipelines (`a.map(..).map(..).unwrap_or(..)`).
- if-let / while-let over prelude `Option`; nested `match` over
  `Result<Option<i32>, i32>` (two-level enum payload).
- for-over-range (exclusive `a..b`), for-over-`&[T;N]` (deref `*x`), nested for.
- Free-fn iterator adapters chained into `fold`/`.count()` terminals, including
  `ZipPair` field reads (`.first`/`.second`) inside fold closures.

## Non-gaps noted while distilling (deliberate divergences / avoided surface)

These were checked and are NOT gaps — recorded so a future batch does not
re-investigate them:

- **`Option::zip` / `Option::unzip` are free fns, not methods.** `a.zip(b)`
  fails with `method call: receiver is not a struct (got Option)`, but
  `stdlib/lang/option/option.logos` (lines ~239–315) intentionally exposes
  `option_zip<T,U>(a, b)` / `option_unzip<A,B>(opt)` as free fns (the same
  free-fn convention used for the iterator adapters). This is a §A-style
  deliberate API shape, not a §B catch-up. The `unzip` METHOD does exist on
  `Option<(A,B)>`; only `zip` is free-fn-only. No test relies on `a.zip(b)`.

- **`.step_by` on a range literal** (`(0..n).step_by(k)`) requires
  `use std.lang.range` to bring `RangeI32` into scope as a struct receiver;
  without it: `range expression: stdlib RangeI32 not in scope`. Avoided by
  using `iter_step_by`-free-fn / plain index loops; not a gap (scope/import
  surface, well-defined).

- **Lexer pitfall (authoring note, not a compiler gap):** `Foo<i32>=expr`
  without a space before `=` lexes `>=` as one token (`syntax error`). Always
  write `let x: Foo<i32> = expr`. All imported tests follow this.
