# ROUND 2026-09-17o-ordcmp — TIER-3 PRICING: ORDERING ON A POINTER OPERAND

build: base = build/bin/logosc (Sep 17 13:15, HEAD 11bb91f02, clean tree).
       one-configure pair = build-ordcmp2/bin/logosc (clang++-20, RelWithDebInfo).
       ⚠ build-ordcmp (GCC) FAILED to configure usefully — the generated parser
       (`logos_parser.cpp`, `na_fail_0` not declared) only builds under clang.
       A second build dir MUST be configured with -DCMAKE_CXX_COMPILER=clang++-20.
       ⚠ binaries identified by PATH + MTIME and by each arm's OWN fire marker,
       never by build_hash.py.

## THE DOOR (read, then measured)

`MLIRGenImpl`'s binop comparison emitter (src/compiler/mlir_gen_expr.cpp, the
block opened by the comment "For pointer comparisons, use llvm.icmp instead of
arith.cmpi", ~line 1200) computes

    bool is_ptr_cmp = mlir::isa<mlir::LLVM::LLVMPointerType>(lhs.getType());

and consults it under `op == "=="` and `op == "!="` ONLY. The ordering branch
below it emits `arith::CmpIOp` unconditionally, so any operand that lowered to
`!llvm.ptr` dies in the MLIR verifier:

    error: 'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'

`K::Ptr`, `K::Ref`, `K::MutRef` and `K::FnPtr` all lower to `ptr_type()`
(mlir_gen_types.cpp:797), so all four arrive the same way.

## THE GROUPING I TESTED, AND ITS REFUTATION (the eighth in this queue)

Hypothesis from READING: one strict extension at that one site moves both
`rawptr_ordering_compare_mlir_verifier_refused` and
`generic_ref_typevar_ordering_mlir_verifier_refused`, which print a
BYTE-IDENTICAL sentence.

Refuted twice, by measurement, before and after arming:

1. BASE battery. sema_expr.cpp ALREADY peels reference layers pairwise for the
   ORDERING operators (the `cmp_op` block, ~line 2417), so a CONCRETE ref
   pointee never reaches the emitter as a pointer. Measured on base, all
   agreeing with rustc 1.98.1: `&u64` lt=0, `&bool` lt=1, `&f64` lt=1,
   `&&i64` lt=0, all run 0. The "ordering has no reference path" reading was
   wrong: it has one, in sema. What actually arrives as `!llvm.ptr` is a RAW/FN
   pointer, or a ref whose pointee sema could not peel (a type variable).

2. ARMED. The crude arm (gate = `is_ptr_cmp`, the MLIR type) DID close both
   rows — and was condemned by its own battery: see the table below.

## TWO ARMS, ONE AT A TIME, EACH WITH ITS OWN FIRE MARKER

  * `ordptr`  (CRUDE)   — gate: `is_ptr_cmp`.
  * `ordptr2` (REFINED) — gate: `is_ptr_cmp && lhs_ty->kind() is Ptr or a fn-value kind`.

Both emit unsigned `LLVM::ICmpOp` predicates (ult/ugt/ule/uge). Unsigned is not
cosmetic: `is_unsigned_repr_kind` is `unsigned int kinds | Bool | Char`
(sema.hpp:249) and `Kind::Ptr` is NOT in it, so the shipped predicate would have
picked SIGNED `slt` for an address comparison. ⚠ This choice is NOT discriminated
by any runnable carrier — Linux userspace never hands out an address above 2^63 —
so it rests on the standing rule (Rust orders raw pointers by unsigned address),
not on a measurement. Said plainly rather than implied.

Inertness control: with the probe OFF the armed binary reproduces BASE line for
line on all 17 battery programs, and its fire log prints 0. With `ordptr2` ON,
c01 (three ordering ops on `*mut i64`) fires exactly 3 times, and d02 (a `&T`
pointee) fires 0 — the gate does what its name says.

## THE PROBE TABLE — EVERY COLUMN, DIFFED BOTH WAYS

One configure (build-ordcmp2), base build then incremental armed rebuild, so the
fail-text oracle's baselines are comparable (it self-invalidates across a configure).

| column                         | base              | armed (`ordptr2`) | cost |
|--------------------------------|-------------------|-------------------|------|
| soundness queue gate           | rc 0, 235 rows    | rc 1, ONE row named | −1 row |
| spec fail tier (by name)       | 494/494 pass rc 0 | 494/494 pass rc 0 | 0 |
| fail_text_oracle.py            | 1913 rows         | 1913 rows         | **0 differing lines** |
| run_oracle.py                  | 7495 rows         | 7495 rows         | 2 lines = 1 test, `cast-region-to-uint`, SUBTRACTED BY NAME (prints a stack address) ⇒ **0** |
| stdlib                         | archives built    | archives rebuilt, rc 0 | 0 |

The gate names exactly one row and names it correctly:
`FAIL: row 'rawptr_ordering_compare_mlir_verifier_refused' (tier 3) NO LONGER
REPRODUCES — the program now compiles clean (cc=0 diag=0 run=0).`
⚠ That gate verdict is satisfied by mere compilation, so it is NOT the evidence.
The evidence is the RUN against the rustc twin, below.

## THE BATTERY — 17 PROGRAMS, EVERY LEGAL ONE RUN AGAINST ITS rustc TWIN

rustc 1.98.1 --edition 2024, called by path. BASE = build/bin/logosc.
"refused" = `arith.cmpi ... but got '!llvm.ptr'` unless stated.

| program                | rustc              | BASE    | CRUDE `ordptr` | REFINED `ordptr2` |
|------------------------|--------------------|---------|----------------|-------------------|
| **row** rawptr_ordering_compare | run 0     | refused | run 0 ✓        | **run 0 ✓ CLOSES** |
| **row** generic_ref_typevar_ordering | run 0 | refused | run 0          | refused (stays rowed) |
| a05 `*const u8 <`      | run 0 lt=1         | refused | run 0 lt=1 ✓   | **run 0 lt=1 ✓** |
| c01 `*mut` `>`,`<=`,`>=` | run 0 g=1 le=1 ge=0 | refused | run 0 ✓      | **run 0 ✓** |
| c02 malloc addr order  | run 0 by_addr=1 by_ptr=1 | refused | run 0 ✓ | **run 0 ✓** |
| c03 `fn()` pointer `<` | run 0              | refused | run 0 ✓        | **run 0 ✓** |
| **a01 no-bound `&T<&T`** | **E0369 REFUSED** | refused | **run 1 — ADMITTED** ⛔ | refused ✓ |
| c04 `T: Ord` u64 twin  | run 0              | refused | **run 1 — MISCOMPILE** ⛔ | refused ✓ |
| d02 ref lex-vs-addr    | run 0 lt=0         | refused | (not built)    | refused ✓ |
| d01 slice lex-vs-addr  | run 0 lt=0         | refused | (not built)    | refused ✓ |
| a08 `&[i64] <`         | run 0              | refused | run 0 (FALSE PASS: address order agreed by accident) | refused ✓ |
| a02 struct no PartialOrd | E0369            | refused (`func.call Q__lt`) | unchanged ✓ | unchanged ✓ |
| c05 mixed mutability   | E0308              | refused (correct sentence) | unchanged ✓ | unchanged ✓ |
| eqsib `*mut ==`        | run 0 e1=0 e2=1    | run 0 e1=0 e2=1 | unchanged ✓ | unchanged ✓ |
| a03 `&&i64`, a04 `&u64`, a06 `&bool`, a07 `&f64` | run 0 | run 0, all correct | unchanged ✓ | unchanged ✓ |

### WHY THE CRUDE ARM IS CONDEMNED, AND WHY a08 MATTERED
Gating on `is_ptr_cmp` (the MLIR type) catches Ref/MutRef too, and then ordering
compares ADDRESSES where Rust compares POINTEES. It closed both target rows and
bought that with: an E0369 program ADMITTED (a01), and a LEGAL `T: Ord` program
MISCOMPILED (c04 — run 1 where rustc runs 0). a08 `&[i64] <` "passed" under it
only because declaration order happened to agree with lexicographic order; d01,
written to make the two DISAGREE, is what turns that false pass into a verdict.
⚠ This is the "sibling arm already does this" trap again: the `==`/`!=` branch
four lines up DOES deref refs — copying its gate installs a miscompile.

## FUNDING RECOMMENDATION

FUND the REFINED arm (`ordptr2`), ~12 lines at ONE site, cost 0 in every column:
it closes `rawptr_ordering_compare_mlir_verifier_refused` run-verified, and moves
four carriers that are legal Rust and refused today (`*const`, all four ops on
`*mut`, malloc address order, fn-pointer ordering) — of which **c03 fn-pointer
ordering and a05 are unrowed tier-3 shapes the queue does not know about**.

## NEIGHBOUR TABLE (standing rule)

| neighbour | closed here / rowed | reason + number |
|---|---|---|
| `generic_ref_typevar_ordering_mlir_verifier_refused` | ROWED | **reason 2, doors in series.** Its `&T` operands need sema to route `<` to an `Ord` bound (sema_expr.cpp ~2697); the emitter cannot tell a bounded `&T` from an unbounded one, and the arm that does admits a01 (E0369) and miscompiles c04. MEASURED on the crude binary, both directions. |
| a08 / d01 `&[T]` slice ordering | ROWED (new shape) | **reason 1, no carrier.** Lexicographic slice ordering is a trait/stdlib question, not an address compare; the emitter has no length to compare. |
| `at_binding_array_sub_verifier_error_refused`, `match_ref_array_nested_struct_sub_refused` | NOT NEIGHBOURS | byte-identical sentence, different door (array-pattern lowering). Refined arm leaves both refused — measured. |
