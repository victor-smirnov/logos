# B122 — UI-surfaced gaps (tests/ui/array-slice-vec + tests/ui/cast run-pass)

Batch B122 imported 29 run-pass tests (15 array-slice-vec `-av`, 14 cast `-ca`)
maximising DISTINCT array/slice/Vec and cast features. All 29 compile + link +
exit 0. The gaps below are what was surfaced while distilling the corpora; all
are §B catch-up TODOs (no new §A blessed divergence).

## NEW gaps (§B catch-up)

### G122-1. Nested `Vec<Vec<T>>` + `get` returning inner Vec by value → double-free (runtime abort)
- Upstream: `array-slice-vec/nested-vec-1.rs` (`let nested = vec![vec![1,2,3]]; nested[0][1]`).
- Repro:
  ```
  let mut outer = vec_new::<Vec<u32>>();
  let mut inner = vec_new::<u32>();
  inner.push(1u32);
  outer.push(inner);
  let row = outer.get(0i64);   // returns the inner Vec BY VALUE
  let _ = row.get(1i64);
  ```
  → runtime `free(): double free detected in tcache 2` / SIGABRT (exit 134).
- Diagnosis: `Vec::get` on a `Vec<Vec<T>>` returns the inner `Vec` by value
  (a shallow copy sharing the heap buffer), but the element is still owned by
  the outer Vec. Both the returned temporary and the still-owned element get
  Dropped → the inner buffer is freed twice. Construction alone (push + length,
  no `get`) is fine — the double-free is specific to reading a non-Copy
  (heap-owning) element out of a Vec by value. The fix is either a borrowing
  `get` (`&T`) for non-Copy element types, or a deep clone on by-value `get`.
- §B. Test `nested-vec-av` was written, failed this way, and DELETED.

### G122-2. Typed deferred-init local assigned in both if/else branches → segfault (heap type)
- Upstream: `array-slice-vec/vec-late-init.rs`
  (`let mut later: Vec<isize>; if … { later = vec![1]; } else { later = vec![2]; }`).
- Repro:
  ```
  let mut later: Vec<i64>;
  if true { later = vec_new::<i64>(); later.push(7i64); }
  else     { later = vec_new::<i64>(); }
  let _ = later.get(0i64);
  ```
  → SIGSEGV (exit 139).
- Diagnosis: `let v: T;` declare-without-init parses + reads work for simple
  types (B3-bg-01 closed the immutable-read case), but **assigning** to a typed
  deferred-init local inside a conditional, then reading it after the join, does
  not flow the assigned value to the post-join slot for a heap type — the later
  read dereferences an uninitialised slot. This is the "full definite-assignment
  analysis deferred" tail of B3-bg-01, now with a concrete heap-type miscompile
  (not just an uninit-load diagnostic). Plain `if-cond ? a : b` value-of-let works;
  the deferred-decl-then-branch-assign-then-read pattern is the broken one.
- Related facet: a deferred-init local declared WITHOUT `mut`
  (`let later: i64; later = 7i64;`) is rejected `assignment to immutable
  variable 'later'` — the first (and only) assignment of a deferred-init binding
  should be permitted (Rust allows `let x; x = …;` once). Definite-assignment is
  not modelled, so the "init = first write" exemption is missing.
- §B. Test `vec-late-init` not imported (would segfault); the Vec grow-then-read
  intent is covered by `vec-grow-iterate-av`.

## Re-confirmed (already documented — NOT a new gap)

- **Byte-char / byte-string literals** `b'a'`, `b"a\xF0\t"`, `br"…"` →
  `syntax error near 'b'`. Already documented pending in
  `docs/spec/lexical.md` ("no character literal beyond `'a'`
  char; no byte-string literal `b\"…\"`; pending"). `array-slice-vec/byte-literals.rs`
  left unimported; `cast/u8-to-char-cast`-style byte values written as integer
  literals (`0x61u8 as char`) per the lexical-ref guidance.
- **Const items** (`const QUUX: isize = 5; enum { Bar = QUUX }`,
  `const ARR: [i32;6] = …`) → §A1 (const-eval → metacall). Enum-as-int tests
  use literal discriminants instead.
- **Dynamic `&[T]` slice-as-value / slicing sugar `&a[lo..hi]`** — KNOWN-OPEN
  (skip list). Subrange intent expressed via index-loop helpers
  (`array-range-sum-helper-av`).
- **`.iter()` on a `[T;N]` array** — KNOWN-OPEN; `for x in &arr` used instead.
- **Vec covariance / lifetime intersection** (`variance-vec-covariant.rs`) —
  not a runtime feature; distilled to the `get`-or-default shape.

## Skipped (feature/surface, not new runtime gaps)

- `slice.rs` (custom `Index`/`IndexMut` for `Range<Foo>`/`RangeTo`/… overloads —
  user-defined indexing operator + range types; large surface, not distilled).
- `huge-largest-array.rs`, `fixed-size-arrays-zero-size-types-8898.rs`
  (`mem::size_of`, ZST `()` element Debug — §A5 / introspection).
- `vec-matching*.rs`, `vec-tail-matching.rs`, `vec-matching-fold.rs`
  (slice patterns with named tail bindings `[ref head, ref tail @ ..]` — B5
  remaining named-nested case; top-level slice patterns separately covered).
- `cast-rfc0401-vtable-kinds.rs`, `fat-ptr-cast-rpass.rs`,
  `cast-to-box-arr.rs`, `coercion-as-explicit-cast.rs` trait/unsize tail
  (`as &dyn Foo`, `as Box<[u32]>`, fat-ptr/vtable casts — dyn/unsize axis,
  separate features; numeric/char/enum/ptr subset imported).
- `nested-vec-2.rs` (Drop side-effect counting via `println!` — §A3).
