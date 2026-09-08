# ROUND 2026-09-08-fatrepr-fix — WHAT LANDED, WHAT WAS DECLINED, WITH NUMBERS

Base `2c5a33ef99e94fc9 43` (HEAD e14eae50d) -> build A `aca6d8a5fef7d705 43`
-> build B `69440e4620b32dcb 43`. The base binary was kept as
`build/bin/logosc.base` for the control column (a COPY OUTSIDE build/bin cannot
resolve `logos.std.prelude` and answers CCFAIL rc 4 to every program — a control
that changes nothing, caught before it was believed).

## CLOSED: 2 ROWS, AT 2 SEPARATE ROOTS, SETS DIFFED BOTH WAYS

### 1. enum_variant_ctor_arg_no_unsize_coercion (tier 3, `refuses`)

CLASS, by the property "an expression checked against the DECLARED type of an
element or field of an aggregate being CONSTRUCTED": 8 sites. struct-literal
field (4) and tuple-struct ctor arg (2) already carry
`CoercePos::StructLitField`; the ENUM VARIANT CTOR carries `CoercePos::Operand`
at 4 sites — `lower_enum_lit_data` and `lower_enum_lit_data_from_static`, each
with its VARIADIC twin. `mask_for(Operand)` is `CFLAG_WIDEN_INT` alone and its
own comment says why ("compound-assign RHS … unsize make no sense for
`place op rhs`"). An enum payload is not that.

FIX: those 4 sites say `CoercePos::StructLitField`. `mask_for` is UNTOUCHED, so
`Operand` keeps its mask for the three compound-assign sites in sema_stmt.cpp.
STRICTLY NARROWER than the priced probe `enumunsize`, which widened the whole
`Operand` position.

SET, MEASURED (16 programs written for this round in shapes the pricing phase
did not use, base column measured before the edit):

    CLOSED (REFUSED -> rc 0, compiled + linked + RUN, answer checked):
      G1 generic enum, unsize AFTER substitution
      G2 `&mut [i64;3]` -> `&mut [i64]`, WRITTEN through the binder
      G3 ctor at a CALL-ARG door (no `let` hint reaches the payload)
      G4 ctor at a RETURN door, operand is a PARAMETER
      G5 CLOSURE -> FN POINTER in a payload (a DIFFERENT flag of the same mask)
      G6 two ctors of two lengths in an ARRAY of enums, read by index
      G7 ctor inside a GENERIC function
      G8 argument INDEX 1 with thin args either side
      + the row program itself
    UNMOVED, and each one REFUSED for the right reason (diagnostic READ):
      X1 `&[u8;2]` -> `&[i64]`            X2 array BY VALUE -> `&[i64]`
      X3 `&[i64;3]` -> `&[i64;4]`         X4 closure of the WRONG signature
      X5 `i64` -> `i32` (no two-way widen) X6 `&` -> `&mut`
      X7 `bool` -> `&[i64]`
      S1 `&[i64]` -> `&[i64;3]`: refused at the tuple-STRUCT ctor one token
         away, and STILL refused there — the two positions agree, which is the
         point of copying the position rather than inventing a mask
      A1 A2 A3 D1-D8 (the other class's programs) — UNMOVED, base and build A
         IDENTICAL cell for cell

### 2. method_with_unsized_wrapper_param_not_found (tier 3, `refuses`)

CLASS, by the property "a coercion the MethodArg pipeline can produce that the
method-candidate SELECTOR does not admit". `arg_compatible_for_dispatch` states
that rule in its own comment and implemented it for ONE coercion. Every flag of
`mask_for(MethodArg)` probed with a hand program, on the base binary:

    CFLAG_ARRAY_TO_SLICE      implemented at the site
    CFLAG_WIDEN_INT           implemented at the site (IntLit clause)
    CFLAG_IMPLICIT_REBORROW   D3  rc 0    admitted
    CFLAG_ARG_TO_DYN          D4  rc 0    admitted
    CFLAG_CLOSURE_TO_FNPTR    D2  REFUSED "method call: 'H' has no method 'ap'"
    try_struct_unsize_coerce  D1 D5 D8 REFUSED "has no method"

TWO live members, not one, and D2 contains no smart pointer and no `dyn` — it
is a non-capturing closure at an `fn(i64)->i64` method parameter, a LEGAL
program refused. THE HANDED-DOWN LIST WAS ONE MEMBER SHORT (rule 17).

FIX: `struct_unsize_shape_ok(src, target)` is SPLIT OUT of
`try_struct_unsize_coerce`, so the selector asks the pipeline's OWN question
instead of a twin of it (rule 18: no twin without a control twin — here there
is no twin at all), and `arg_compatible_for_dispatch` gains that call plus the
closure->fn-ptr shape test copied from `try_coerce_closure_to_fnptr`
(FnPtr target, ClosureBox, ZERO captures, arity and parameter types compatible).
NARROWER than the priced probe `dispunsize`, which accepted ANY two structs
sharing a mangled base name — and `dispunsizeagg`, the twin asked through
`aggregate_unsize_pending`, was measured to close NOTHING.

SET, MEASURED:

    CLOSED (REFUSED -> rc 0, RUN):  D1 inherent method · D5 TRAIT method ·
      D8 the name OVERLOADED (both arms answer correctly) · D2 closure -> fn ptr
      · the row program itself
    UNMOVED: D3 D4 rc 0 · G1-G8 S1 X1-X7 A1 A2 A3 unchanged from build A
    STILL REFUSED, diagnostic READ: D6 `W<bool>` at a `W<i64>` parameter —
      not an unsize (both field types sized), `struct_unsize_shape_ok` says no,
      and the sentence is unchanged.

## THE ONE PREDICTION THAT MISSED, AND WHAT IT FOUND

D7 — `Rc<A>` at an `Rc<dyn Other>` parameter where `A` does NOT implement
`Other` — was predicted REFUSED and is refused, but the SENTENCE moved from
sema's (wrong) "has no method" to the BACKEND's self-diagnosis:
`mlir_gen: internal: no vtable for 'A' as '&dyn Other' …  COMPILE FAILED`.

CONTROL, on the BASE binary: the identical ill-typed unsize at a `let` door
(no method anywhere in the program) ALREADY prints exactly that. So the hole is
pre-existing — `try_struct_unsize_coerce` tests the SHAPE and never asks whether
the trait is implemented — and this change only routes the METHOD-ARGUMENT door
into it. Minted as a row rather than left in prose:
`wrapper_unsize_missing_impl_backend_diag` (tier 4, `diag`), program written at
the `let` door so the row does not depend on the repair that exposed it.

## MINTED: 2 ROWS

  arraylit_closure_elem_fnptr_refused (3, refuses) — an ARRAY LITERAL of
    non-capturing closures at a declared `[fn(i64)->i64; 2]`. Same class as the
    enum ctor (an aggregate element position missing a coercion its siblings
    carry) and NOT the same root: build A repairs the enum member and leaves
    this one refused, byte for byte. ⚠ its header records that the mask is a
    HYPOTHESIS — the diagnostic printed is the `let`-level one.
  wrapper_unsize_missing_impl_backend_diag (4, diag) — above.

## DECLINED BY NAME, WITH THE NUMBER

  zonemut_fat_ref_struct_field_layout_abort — DECLINED. The prompt recommended
    it first; the pricing round measured it as THREE engines in series, and
    that number is what declines it: with the LLVM struct type builder repaired
    (`fatfield`) `LOGOS_VERIFY_LAYOUT=1` still reports 4 disagreements
    (`mono_abi_layout` 8, `sema_abi_layout` 8, `layout_of` 16), down from 5.
    One site of three is not a fix; a compiler that writes an object file while
    its own layout law says the type is inconsistent is a QUIETER crash, which
    the prompt names as the one outcome this row must not buy. Unmoved by this
    round: still CCFAIL 134 on both binaries.
  fatslice_field_match_binder_invalid_mlir — DECLINED, and the number is 139.
    The one-line convention swap (`fatbind`) closes the row and takes hand B4 —
    a `&dyn Tr` FIELD whose method is called through a by-value struct-pattern
    binder, rc 0 at base — to a SEGFAULT, with 0/0/ok/0-of-6555 in all five
    cost columns. Buying a `refuses` row with a silent miscompile in a legal
    program is the most expensive trade in this queue. Unmoved by this round.

## OWNER'S CALL, REPORTED NOT EDITED

D6 (`W<bool>` at a `W<i64>` method parameter) is refused with
`method call: 'H' has no method 'eat'`. The closed row's header called that
sentence a defect. It is NOT minted as a row: Logos has method OVERLOADING, so
"no candidate accepts this argument" is a defensible verdict for the whole
class, and whether it should name the argument instead is a language decision,
not a measurement. tests/logos/fail/method_arg_wrapper_unsize_dispatch pins the
current sentence so a change to it is visible.

## EVERY ORACLE, ON THE FINAL TREE

    soundness queue gate              rc 0 — 80 rows (t1=27 t2=7 t3=41 t4=5),
                                      '# TOTAL' 80, ROWS == PROGRAMS both ways
    test-levels.sh L1                 rc 0 — 780/780, gates 156/156,
                                      enumerator smoke 12 684 generated cases
    test-levels.sh L4 bc              rc 0 — 4970/4970, then 1560/1562 on the
                                      `bc` filter (gate-db build 947)
    gate-run.sh -L bc                 rc 0 — 2696 passed / 0 failed / 2 other
                                      (build 946; the store's baseline READ at
                                      the start was build 939, "all 2698 already
                                      measured", 6528 recorded / 1 failed)
    stdlib-cost.sh                    all four layers compile
    run_oracle.py                     6555 fixtures shared with the base column,
                                      1 CHANGED and it is cast-region-to-uint,
                                      subtracted BY NAME (it prints a stack
                                      address — and it differed between two runs
                                      of the SAME binary, which is the proof)
                                      + 2 fixtures present only in the new
                                      column: the two new pass halves
    cmake --build (full)              rc 0
    population pins re-derived        direct_door 2934 -> 2936 = 191 + 2745;
                                      census REGISTRY-ALL 9424 -> 9428,
                                      NOIMPORTED 4966 -> 4970, TIERCOMMIT 156

⚠ THE run_oracle BASELINE IS A REAL BASE BUILD, not the shipped binary moved
aside: a COPY of logosc outside `build/bin` cannot resolve `logos.std.prelude`
and answers CCFAIL rc 4 to every program, and the same binary left in
`build/bin` beside NEWER archives prints an ABI-freshness warning on every
compile. The baseline was taken from a `git worktree` at e14eae50d configured
with the SAME toolchain the main build uses (clang-20 + Ninja; the default
cmake pick is gcc and the generated parser does not compile under it).

## CONTROL REVERT, ON THAT BASE BUILD

    tests/logos/pass/enum_ctor_arg_array_to_slice_unsize.logos
      base: rc 1, "E::S arg 0: expected &[i64], got &[i64; 3]"
      now:  compiles, links, RUNS, exit 7
    tests/logos/pass/method_arg_wrapper_unsize_dispatch.logos
      base: rc 1, "method call: 'Holder' has no method 'eat'"
      now:  compiles, links, RUNS, exit 7

## ONE CLEANUP, MEASURED RATHER THAN ASSERTED

Splitting `struct_unsize_shape_ok` out left the shape test written TWICE. The
duplicate was deleted from `try_struct_unsize_coerce` and the tree rebuilt: the
27-program hand matrix is byte-identical before and after (diff empty), the
queue gate is rc 0 on both, and every column above was re-measured on the
DE-DUPLICATED binary, not on the one that had the copy.
