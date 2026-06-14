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

- The atomic API (`stdlib/lang/atomic/`) now THREADS the `Ordering`
  (T2-24): a *literal* ordering at the intrinsic call lowers to the exact
  MLIR atomic ordering (`read_ordering_at`); a *runtime* ordering through
  the `_ordered` wrappers lowers a `store` to `SeqCst ? seq_cst : release`
  — the only ordering that changes x86-64 store codegen (`xchg` vs `mov`) —
  and keeps `seq_cst` for load / RMW / CAS, which are byte-identical across
  orderings on x86. On x86-64 (the target) this is precise; the residual is
  purely conformance (the exact attribute on loads/RMW where x86 codegen
  coincides anyway). Earlier weak-memory unsoundness on ARM/RISC-V is moot —
  x86-64 is the only target. See `docs/internals/const-arg-specialization.md`.
- Outside the atomic API, two threads writing to overlapping `&mut T`
  views is rejected by the borrow checker (single-thread aliasing) at
  *compile time* only when both writers are in the same fn body. Cross-
  thread sharing requires `Sync`; the auto-trait machinery is partly
  permissive (closures-walk-captures landed in logos-core 2.4).
- **Follow-up:** Ordering threading is now done (above); the open race
  surface is cross-thread `&mut`/`Sync` enforcement — `logos-core.md §2.4`
  (auto-trait propagation `dyn+Auto` enforce).

## Dereferencing a dangling, null, or unaligned raw pointer
*Rust anchor:* `undefined.pointer-access`.
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

- Logos has **no `mem::transmute`**. The reinterpret path is an explicit
  unsafe pointer cast — `&x as *const T as *mut U`, then deref — done in an
  `unsafe` block; size/validity are the programmer's responsibility (no
  size_of equality check, no niche-validity check). Producing an invalid
  bit pattern this way is UNENFORCED.
- Reading uninitialized memory — Logos zero-inits via `alloc()`, so the
  classic `mem::uninitialized()` hazard is reduced but not eliminated once
  raw-ptr / manual-alloc work re-introduces uninit storage.
- **Follow-up:** if a `transmute` builtin is ever added, it should carry a
  call-site size_of-equality check (the cheap half) and reuse enum-layout
  niche metadata for the validity half; track as baghunt `UB-validity-niche`
  alongside `#[repr(transparent)]` niche propagation (`logos-core.md §1.5`).

## Mutating immutable bytes
*Rust anchor:* `undefined.immutable`.
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
*Rust anchor:* `undefined.alias`.
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
- Runtime overflow of `+`/`-`/`*` on integers is **CHECKED**: each op
  lowers to `llvm.intr.{s,u}add/sub/mul.with.overflow` + a branch to
  `llvm.intr.trap` on the overflow bit (verified in MLIR). This is Rust's
  **debug-mode** semantics (panic/abort on overflow), NOT release-mode
  wrapping — and it stays on regardless of `-O`. Per the abort-only panic
  strategy (DIVERGENCES §A7) the trap aborts rather than unwinding.
- **Follow-up:** wrapping/`checked_*`/`saturating_*` arithmetic as opt-in
  ops (Rust's `Wrapping<T>` / `i32::wrapping_add`) is the catch-up surface,
  tracked in `docs/language/feature-audit/K-unsafe.md`; track via baghunt
  `UB-integer-overflow` when an import needs explicit wrap semantics.

## Inline assembly
*Rust anchor:* `undefined.asm`.
**Status:** n/a — Logos has no inline assembly today (audit category N).

---

## Cross-references

- `docs/DIVERGENCES.md` §A7 (panic = abort) — every "panic on unwind"
  rule in Rust's UB list becomes "abort on unwind site".
- `docs/language/logos-core.md` §1 item 1.1 (Never tightening), §2 item
  2.4 (auto-trait propagation), §2 item 2.6 (slice mut), §6 (coupling
  rules). Atomic Ordering threading (was §5.1) is done — T2-24,
  `docs/internals/const-arg-specialization.md`.
- `docs/language/feature-audit/K-unsafe.md` for the per-category audit
  of the unsafe surface (gap analysis vs Rust spec).

This document is a register, not a contract. When a rule above moves
from PARTIAL → ENFORCED (or UNENFORCED → PARTIAL) record the closure in
the corresponding `logos-core.md` "Recently closed" entry.
