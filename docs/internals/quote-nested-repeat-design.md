# Nested `#(…)*` repetition in quote templates (T2-30)

Status: **`quote_item!` depth-2 SHIPPED** (ragged `Vec<Vec<Ident>>`, test
quote_nested_repeat, L4-gated, valgrind-clean). `quote_expr!` nesting is a
SEPARATE machinery (sema_expr.cpp:16287 `walk` with its own `repeat_depth`/
`repeat_stack_count` + `[Ident; N]` cursor arrays + its own runtime shim) and
remains a graceful error (16315) — no consumer drives it (expr-context
nesting is rare; the derive consumer uses `quote_item!`). Depth ≥ 3 is a
graceful error in both.

## As-built notes (deviations from the original plan below)

* No placeholder depth-bit was needed: the cursor's depth is read at runtime
  from `CursorHdr.inner_counts_offset != 0`, so the dst `cidx` encoding is
  unchanged and DFS-consistent.
* Outer index is carried between the two existing expansion phases by
  PINNING it into the depth-2 cursor placeholder's NAME_VAR encoding (bits
  8-21 = pinned_outer+1; bits 0-7 = cidx; bit 22 = is-cursor) during the
  outer pass; the inner pass (run by subst_walk's normal recursion) reads
  the pin. No new inline-expand tangle.
* **Realloc-safety**: `doc` is a GrowableSingleChunk arena; a mid-copy
  realloc FREES the chunk and dangles the raw pointers deep_copy_object /
  expand hold. Nested expansion's higher volume exposed this LATENT bug
  (present for single-level too). Fix: `Arena::reserve(free_bytes)` (new
  public) called ONCE after from_bytes_copy with an upper bound
  (`template_size·(2·Σpods+2) + Σident_bytes + 64K`) so no later allocation
  reallocs. Front-loaded while nothing points into doc.

## Original design (retained for the quote_expr! follow-up)

## Goal

Rust `macro_rules!`/`quote!` allow `#( … #( … )* … )*`. Logos rejects depth
> 1. The realistic consumer is enum-`derive(Debug)` (outer = variants, inner
= that variant's fields — ragged: each variant has a different field count).

## Why it's non-trivial

The cursor pipeline is fundamentally 1-level:

- A cursor variable is `Vec<Ident>`; the runtime cursor blob is
  `CursorHdr { count, pods_offset }` → one flat `IdentPod[]`.
- `expand_cursor_in_subtree(off, iter)` substitutes with a SINGLE `iter`.
- `find_cursor_count_in_body` returns one count.

Nested repetition needs a **ragged 2-D** cursor: the inner cursor's length
varies per outer iteration.

## Design — iters-vector coordinate model (impl restricted to depth-2)

A cursor's **declared nesting depth** = how deep its `Vec<…>` nesting goes:

| template depth | cursor type        | indexed by      |
|----------------|--------------------|-----------------|
| 1 (outer)      | `Vec<Ident>`       | `iters[0]`      |
| 2 (inner)      | `Vec<Vec<Ident>>`  | `iters[0],[1]`  |

A depth-1 cursor referenced inside an inner loop is **constant** across the
inner index (uses `iters[0]` only). Counts: the outer repeat's count = a
cursor's OUTER dimension; the inner repeat's count at outer `o` = the
depth-2 cursor's `o`-th sublist length.

### 1. Blob ABI (backward-compatible)

```
CursorHdr { uint64_t count; uint64_t pods_offset; uint64_t inner_counts_offset; }
```
- depth-1: `inner_counts_offset == 0`. `count` pods at `pods_offset` (as today).
- depth-2: `inner_counts_offset != 0` → `count` = OUTER count O; at
  `inner_counts_offset` lie O `uint64` sub-lengths; `pods_offset` holds the
  FLATTENED inner idents (Σ sub-lengths), CSR-style. Inner ident `[o][n]` =
  `pods[ prefix_sum(inner_counts, o) + n ]`.

Dedup-key hashing (main.cpp ~57-61) must fold `inner_counts` when present.

### 2. `logos_qib_pack_cursors` (main.cpp:1231)

Take a per-cursor `depth` array (new param, or a second pack fn). For a
depth-2 cursor the arg is `*const Vec<Vec<Ident>>`: read outer len → O,
write `inner_counts[o] = inner[o].len`, flatten all inner idents into pods,
set `inner_counts_offset`.

### 3. sema `quote_item!` (sema_expr.cpp walk_src 15214 / dst-walk 15369)

- Replace the depth-gate: allow `qi_repeat_depth ≤ 2`; reject ≥ 3 with the
  existing message (depth-3 is the new boundary).
- `Placeholder` gains `uint8_t cursor_depth`. A `#x` at template depth d
  type-checks x as Vec-nested-d-deep of Ident (`is_vec_ident_qi` → add
  `is_vec_vec_ident_qi`). Encode depth into the placeholder via a bit
  (`0x200000` = depth-2) alongside `0x400000` (is-cursor).
- Cursor-gather/pack call site (15740): for a depth-2 placeholder emit
  `&var : *const Vec<Vec<Ident>>` and pass its depth.

### 4. Runtime expansion (main.cpp subst_walk / try_expand_array_repeats /
   expand_cursor_in_subtree)

Thread a coordinate `o` (outer index) into the inner expansion. Outer
`try_expand_array_repeats` expands the outer repeat O times; for each `o` it
calls `expand_cursor_in_subtree(copy, o)` which now ALSO expands any nested
REPEAT_GROUP array inline: `M_o = inner_counts[o]`, for `n in 0..M_o` copy
the inner body and substitute depth-2 cursors → `pods[prefix[o]+n]`,
depth-1 cursors → `pods[o]`. Single pass, no defer/pin hack.

`find_cursor_count_in_body`: outer body → a cursor's `count` (outer dim);
inner body → a depth-2 cursor's `inner_counts[o]` (needs `o` in scope).

### 5. `quote_expr!` (sema_expr.cpp 16100+)

Same shape; apply after `quote_item!` lands and its test is green.

## Test

Handler that builds `outer: Vec<Ident>` and `inner: Vec<Vec<Ident>>`, emits
`#( fn #outer() { #( let _ = #inner; )* } )*`, and asserts the rendered
source (via `--dump-metaprog`) has the right ragged per-outer inner counts.
Gate L4.
