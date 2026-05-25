# B169 broad run-pass import — gap census (2026-05-25)

Batch goal: volume ("добьём массу") after the B168 dyn adversarial work. The easy
rustc-UI run-pass corpus is heavily mined; ~50% of fresh candidates surface a gap.

## CLOSED this batch
- **`&fn`/`&mut fn` call auto-deref** (529e58e7) — `x(args)` where `x: &fn(…)->R`
  was mis-resolved as a named call. Sema Ref-over-FnPtr/Closure callee peel + deref.
- **Parenthesised patterns `(P)`/`(P|Q)`** + **`n @ (1|2|3)` scalar @-over-or**
  (8a6ce442) — grammar production + PatAt Or-sub OR-chain codegen.

## Imports banked (PASS as-is or after standard adaptation)
- closures/call-fn-ref-autoderef, traits/borrowed-traitobject-method-5008,
  traits/generic-dyn-getter-sugar-object (529e58e7).
- drop/newtype-struct-drop-run, drop/drop-scope-exit (tuple/struct Drop at scope exit).
- or-patterns/paren-or-pattern, or-patterns/at-binding-paren-or (8a6ce442).

## OPEN gaps surfaced (not yet fixed — candidates for a focused pass)
1. **Tuple or-pattern alternatives with bindings** — `(a,0)|(a,1)` / `(0,a)|(a,0)`
   silently never match. See [[baghunt_tuple_or_pattern_binding]]. (variant-or
   `Ok(x)|Err(x)` WORKS.) rustc: or-patterns/search-via-bindings.rs, inner-or-pat.rs.
2. **Temporary rvalue Drop not run for a method receiver** — `W::mk(…).get()` (a
   droppable temporary used as an autoref'd `&self` receiver) is NOT dropped at end
   of statement. ISOLATED 2026-05-25: bare `mk(c);` (temp_stmt) and `consume(mk(c))`
   (temp moved into fn) BOTH drop correctly — only the method-receiver temp leaks.
   ROOT: the B140-G1 statement-temp drop (sema_stmt.cpp ~240-272) binds+drops only
   the TOP-LEVEL discarded rvalue; here the statement result is `i64` (get's return),
   so the inner materialized receiver temp (addr_of_temp of the droppable `W`) is
   never registered. FIX SCOPE: collect ALL droppable temporaries materialized
   during a statement (Rust temporary scope = end of statement) and drop them, not
   just the result. Architectural drop-insertion extension. rustc:
   drop/drop-immediate-non-box-ty-9446.rs.
3. **`impl Trait for &[T]`** (impl on a reference/slice type) — rejected: "the type
   `[i64]` is unsized: cannot be used by value". rustc: array-slice-vec/rcvr-borrowed-to-slice.rs.
4. **Blanket default method via generic `Box<dyn>` cast** — `impl<A> Hax for A {}`
   default method, dispatched after `box_new(x) as Box<dyn Hax>` where x is generic
   — SIGSEGV at dispatch (vtable for the blanket-over-concrete not built on the
   generic cast path). Cf. [[baghunt_blanket_default_method_dispatch]] (CLOSED for
   the non-Box-cast case). rustc: drop/issue-2734.rs.
