# Logos — undefined behavior register

> Mirrors `rust-lang/reference/src/behavior-considered-undefined.md`
> section-by-section. Each anchor records what is **enforced** (compiler
> rejects), **partial** (caught for some cases but not all), or
> **unenforced** (programmer's responsibility, no compile-time check).
>
> Logos-core item 5.2.

The Logos panic strategy is **abort-only** (DIVERGENCES §A7) — none of
the "panic on unwind" entries in the Rust list apply; replace them with
"abort on unwind site" semantics throughout.

## Data races
*Rust anchor:* `undefined.race`.
**Status:** PARTIAL.

- The atomic API (`stdlib/lang/atomic/`) accepts an `Ordering` parameter
  but currently discards it (every atomic op lowers to `seq_cst` on x86).
  On x86 this is sound (TSO collapses the distinction); on ARM / RISC-V
  it is unsound — relaxed-store paired with acquire-load may reorder.
  Tracked in [logos-core.md §6 item 5.1](logos-core.md) as Phase 6 #19.
- Outside the atomic API, two threads writing to overlapping `&mut T`
  views is rejected by the borrow checker (single-thread aliasing) at
  *compile time* only when both writers are in the same fn body. Cross-
  thread sharing requires `Sync`; the auto-trait machinery is partly
  permissive (closures-walk-captures landed in logos-core 2.4).
- **Follow-up:** `logos-core.md §5.1` (atomics Ordering threading) +
  `logos-core.md §2.4` (auto-trait propagation `dyn+Auto` enforce).

## Dereferencing a dangling, null, or unaligned raw pointer
*Rust anchor:* `undefined.deref`.
**Status:** UNENFORCED (within `unsafe`).

- `unsafe { *p }` performs the deref unchecked. Programmer guarantees
  validity, alignment, and non-null.
- Dropping the dangling-ref through references *is* enforced by the
  borrow checker: a `&'a T` whose source is a local is rejected at the
  return site (B66 outlives + region_infer named lifetimes, partial).
- **Follow-up:** by design — `unsafe { *p }` is the programmer's
  contract per the panic-strategy register (`DIVERGENCES.md §A7`). A
  static lint for null-checked-by-construction raw pointers would be
  breadth-tier (not core); track via baghunt `UB-deref` when needed.

## Producing an invalid value
*Rust anchor:* `undefined.validity`.
**Status:** PARTIAL.

- `transmute` of mismatched-size types — REJECTED at compile (size_of
  comparison at the call site).
- `transmute` to a niched type with an invalid bit pattern — UNENFORCED.
- Reading uninitialized memory through `mem::uninitialized()` — Logos
  uses zero-init via `alloc()`, so this category is reduced but not
  eliminated when raw-ptr work re-introduces it.
- **Follow-up:** niche-validity check at `transmute` callsite needs the
  same niche-optimization metadata used by enum layout; track as a
  baghunt `UB-validity-niche` entry alongside `#[repr(transparent)]`
  niche propagation (`logos-core.md §1.5` extension).

## Mutating immutable bytes
*Rust anchor:* `undefined.mut_immutable`.
**Status:** ENFORCED.

- Writing through `&T` is rejected at compile time (M2 borrow check).
- Writing through `*const T` is rejected by the type system (require
  `*mut T`).
- The `UnsafeCell<T>` exception — `&UnsafeCell<T>` permits interior
  writes — is the lang-item recognised by sema variance + auto-trait
  (Phase 4 #11). Borrow-check carve-out for the actual write path is
  follow-up work tracked in logos-core.md §2 item 2.2 (partial).

## Calling a function with the wrong ABI / fn signature mismatch
*Rust anchor:* `undefined.call`.
**Status:** ENFORCED at the language boundary; UNENFORCED across FFI.

- Logos-to-Logos fn pointer calls type-check at the FnPtr level
  (param types and arity).
- `extern "C"` calls: signature match between declaration and host
  library is the user's responsibility; the compiler has no link-time
  ABI validation.
- **Follow-up (FFI side):** link-time ABI validation needs a
  cross-source signature register; track via baghunt `UB-ffi-abi` when
  a real link-time mismatch surfaces in imports.

## Producing a dangling `&` / `&mut`
*Rust anchor:* `undefined.dangling`.
**Status:** ENFORCED for the common cases.

- Returning `&local_var` is rejected (B61 NLL + Phase 9 dropck +
  region_infer named regions). Cross-fn-call provenance is conservatively
  rejected (`721a3780`).
- Reborrow chains are first-class as of M2 (`AddrOfTemp(Deref(VarRef))`
  shape preserved through LIR; borrow_check unwraps at the use site).
- **Follow-up:** the "common cases" still excludes default
  trait-object lifetime + full HRTB instantiation — see
  `logos-core.md §2.1` finish + `§3.1`.

## Breaking the pointer aliasing rules
*Rust anchor:* `undefined.aliasing`.
**Status:** PARTIAL.

- `&mut T` exclusivity is enforced at sema + borrow_check (M2 reborrow
  landing). Two-phase borrows (B82) accepted.
- Overlapping `&mut` and `*mut` derived from the same source: UNENFORCED
  (raw-ptr writes inside `unsafe` are programmer's responsibility).
- **Follow-up:** the `UnsafeCell<T>` borrow-check carve-out
  (`logos-core.md §2.2`) — distinguish "write through `&UnsafeCell<T>.get()`"
  from a generic `&T → *mut T` violation. Today both work via the
  unsafe-block escape hatch.

## Calling `Drop` on a moved value
*Rust anchor:* (drop ordering).
**Status:** ENFORCED.

- M2 assignment drop-elaboration handles all cases (drop flags for
  conditional init; static drop placement for straight-line).
- `mem::ManuallyDrop` lang-item (= Logos `#[no_auto_drop]`) opts out
  cleanly.

## Calling a `fn` whose preconditions were violated
*Rust anchor:* `undefined.lib_unsafe`.
**Status:** UNENFORCED.

- Standard pattern: stdlib fns marked `unsafe fn` document their
  invariants in doc-comments; callers in `unsafe { ... }` blocks assert
  compliance. No compile-time enforcement beyond the `unsafe` keyword
  itself.
- **Follow-up:** by design — same panic-strategy contract as raw-ptr
  deref (`DIVERGENCES.md §A7`). Tooling around invariant assertions
  (`debug_assert!` extensions, refinement types) is breadth-tier; no
  core item.

## Integer overflow
*Rust anchor:* (numeric).
**Status:** UNENFORCED at runtime; PARTIAL at compile time.

- `let x: i32 = 256i8;` — integer-literal overflow IS detected (Phase
  1 intlit fits check).
- Runtime overflow of `+`/`-`/`*` on integers wraps (Rust release-mode
  default). No debug-mode panic on overflow.
- **Follow-up:** runtime overflow checks (`debug_assert!`-on-overflow)
  are tracked in `docs/language/feature-audit/K-unsafe.md` (audit
  cross-category #1 for K). Not a logos-core item; track via baghunt
  `UB-integer-overflow` when an import depends on Rust's debug-mode
  semantics.

## Inline assembly
*Rust anchor:* `undefined.asm`.
**Status:** n/a — Logos has no inline assembly today (audit category N).

---

## Cross-references

- `docs/DIVERGENCES.md` §A7 (panic = abort) — every "panic on unwind"
  rule in Rust's UB list becomes "abort on unwind site".
- `docs/language/logos-core.md` §1 item 1.1 (Never tightening), §2 item
  2.4 (auto-trait propagation), §2 item 2.6 (slice mut), §5 item 5.1
  (atomic Ordering), §6 (coupling rules).
- `docs/language/feature-audit/K-unsafe.md` for the per-category audit
  of the unsafe surface (gap analysis vs Rust spec).

This document is a register, not a contract. When a rule above moves
from PARTIAL → ENFORCED (or UNENFORCED → PARTIAL) record the closure in
the corresponding `logos-core.md` "Recently closed" entry.
