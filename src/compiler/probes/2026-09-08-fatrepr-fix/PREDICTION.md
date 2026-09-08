# ROUND 2026-09-08-fatrepr-fix — PREDICTION, DECLARED BEFORE THE COMPILER WAS EDITED

Base build hash `2c5a33ef99e94fc9 43`, HEAD `e14eae50d`, tree clean.
Baselines READ, not re-run: queue gate rc 0 / 80 rows; `gate-run.sh -L bc`
build_id 939, "all 2698 tests in this filter are ALREADY MEASURED", 6528
recorded / 1 failed (`logos_00_census_pin`, reds on any new test BY DESIGN).

## THE CLASS, ENUMERATED BY THE PROPERTY (not by spelling)

PROPERTY: "an expression checked against the DECLARED type of an element or
field of an aggregate being CONSTRUCTED". Every such site in the compiler,
found by taking every `expect_type` call whose expected type comes from an
aggregate's declared member list:

    site                                            file:line                    CoercePos
    struct literal field            sema_expr.cpp:11787,11846,12013,12037   StructLitField  ok
    tuple-struct ctor arg           sema_expr.cpp:3467, 7032                StructLitField  ok
    enum variant ctor arg           sema_expr.cpp:14259 (lower_enum_lit_data)        Operand  DEFECT
    enum variant ctor arg           sema_expr.cpp:14628 (…_from_static)              Operand  DEFECT
    enum VARIADIC variant arg       sema_expr.cpp:14342, 14711                       Operand  DEFECT
    tuple literal element           (TupleElem)                                              ok  (A2 rc 0)
    array literal element           (ArrayElem)                             ArrayElem  SUSPECT (A1)

`mask_for(Operand)` is `CFLAG_WIDEN_INT` alone; `Operand`'s own comment says it
is "an operand of a larger construct (compound-assign RHS): verdict + widen
only — reborrow/unsize make no sense for `place op rhs`". An enum payload is
NOT that. It is a struct-literal field with a tag, and the tuple-STRUCT ctor
one token away already says so.

## THE CHANGE (build A)

Four sites: `CoercePos::Operand` -> `CoercePos::StructLitField` at
sema_expr.cpp 14259, 14342, 14628, 14711. `mask_for` is NOT touched, so
`Operand` keeps `CFLAG_WIDEN_INT` for the three compound-assign sites in
sema_stmt.cpp. STRICTLY NARROWER than the priced probe `enumunsize`, which
widened the whole `Operand` position.

Delta at the four sites: + CFLAG_CLOSURE_TO_FNPTR, + CFLAG_ARRAY_TO_SLICE,
+ CFLAG_SLICE_TO_ARRAY. No reborrow (Rust MOVES into a ctor) — same as
StructLitField, which is the point.

## PREDICTED, PROGRAM BY PROGRAM (build A vs the base column already measured)

    prog  base                      predicted after
    G1    REFUSED                   rc=0   generic enum, `&[i64;3]`->`&[T]` after subst
    G2    REFUSED                   rc=0   `&mut [i64;3]`->`&mut [i64]`, written through
    G3    REFUSED                   rc=0   ctor at a CALL-ARG door (no LetInit hint)
    G4    REFUSED                   rc=0   ctor at a RETURN door, operand is a PARAM
    G5    REFUSED                   rc=0   closure -> fn ptr in a payload (2nd flag)
    G6    REFUSED                   rc=0   two ctors in an ARRAY of enums, 2 lengths
    G7    REFUSED                   rc=0   ctor inside a GENERIC fn
    G8    REFUSED                   rc=0   arg INDEX 1, thin args either side
    S1    REFUSED (tuple-struct H)  REFUSED, THE SAME LINE — `&[i64]`->`&[i64;3]`
                                    must stay whatever the struct ctor says
    X1    REFUSED                   REFUSED  `&[u8;2]` -> `&[i64]`
    X2    REFUSED                   REFUSED  array BY VALUE -> `&[i64]`
    X3    REFUSED                   REFUSED  `&[i64;3]` -> `&[i64;4]`
    X4    REFUSED                   REFUSED  closure of the WRONG signature
    X5    REFUSED                   REFUSED  `i64` -> `i32` (no two-way widening)
    X6    REFUSED                   REFUSED  `&` -> `&mut`
    X7    REFUSED                   REFUSED  `bool` -> `&[i64]`
    A1    REFUSED                   REFUSED  (build A does not touch ArrayElem)
    A2    rc=0                      rc=0
    A3    rc=0                      rc=0

    ROW enum_variant_ctor_arg_no_unsize_coercion   REFUSED -> rc=0   CLOSED
    ROW zonemut_fat_ref_struct_field_layout_abort  CCFAIL 134 -> UNCHANGED
    ROW fatslice_field_match_binder_invalid_mlir   REFUSED    -> UNCHANGED
    ROW method_with_unsized_wrapper_param_not_found REFUSED   -> UNCHANGED

## PREDICTED NUMBERS

    rows CLOSED: 1        enum_variant_ctor_arg_no_unsize_coercion
    new queue # TOTAL: 79 (re-derived by direct listing, not by arithmetic)
    bc ledger rows opened/closed: 0
    gate-run -L bc: 0 new failures beyond logos_00_census_pin
    run_oracle.py: 0 of ~6555 changed
    stdlib-cost.sh: all four layers ok
    fail_text_oracle.py: 0 changed

## BUILD B — A SECOND, SEPARATE CANDIDATE, MEASURED APART

A1 (an array literal of CLOSURES at a declared `[fn(i64)->i64; 2]`) is refused
at base and is legal Rust. Whether its root is `mask_for(ArrayElem)` missing
CFLAG_CLOSURE_TO_FNPTR is a HYPOTHESIS, not a fact: the diagnostic it prints is
the `let`-level one, not an element-level one. Build B = build A + that flag.
PREDICTED: if A1 stays REFUSED under build B, the flag is not its root, build B
is DISCARDED, and A1 becomes a NEW soundness-queue row (`refuses`) instead.
The two candidate sets are DISJOINT by construction (no G/X program contains an
array literal of closures; A1 contains no enum), so one binary each keeps the
credit separable.

# ADDENDUM — BUILD B, DECLARED BEFORE IT FINISHED BUILDING

Build A measured (hash `aca6d8a5fef7d705 43`): the enum prediction above is
matched cell for cell, `gate-run -L bc` build 945 = 2696 passed / 0 failed / 2
other, and the D-series below is IDENTICAL on the base binary and on build A —
so the two candidate sets really are disjoint and credit is separable.

## THE SECOND CLASS, ENUMERATED BY THE PROPERTY

PROPERTY: "a coercion the MethodArg mask can produce that the method-candidate
SELECTOR (`arg_compatible_for_dispatch`) does not admit". The site's own comment
states the rule and implements it for ONE coercion. Every flag in
`mask_for(CoercePos::MethodArg)`, measured with a hand program each, on the base
binary AND on build A (same answers):

    flag                      probe  verdict at base
    CFLAG_ARRAY_TO_SLICE      —      implemented at the site
    CFLAG_IMPLICIT_REBORROW   D3     rc 0   admitted
    CFLAG_ARG_TO_DYN          D4     rc 0   admitted
    CFLAG_WIDEN_INT           —      implemented at the site (IntLit clause)
    CFLAG_CLOSURE_TO_FNPTR    D2     REFUSED "method call: 'H' has no method 'ap'"   DEFECT
    (try_struct_unsize_coerce, the pipeline's wrapper unsize, is NOT a mask flag
     but runs unconditionally in coerce_arg_to_param)
                              D1/D5/D8 REFUSED "has no method"                       DEFECT
                                       — inherent, TRAIT method, and OVERLOADED

So the class has TWO live members, not one, and D2 contains no smart pointer
and no `dyn` at all — it is a closure at an `fn(i64)->i64` method parameter.

## THE CHANGE (build B, on top of A)

`struct_unsize_shape_ok(src, target)` is SPLIT OUT of
`try_struct_unsize_coerce` (sema_expr.cpp) so the selector asks the pipeline's
own question rather than a twin of it, and `arg_compatible_for_dispatch` loses
`const noexcept` (two callers, both non-const) and gains two clauses: the
closure→fn-ptr shape (the exact test in `try_coerce_closure_to_fnptr`: FnPtr
target, ClosureBox with ZERO captures, arity and parameter types compatible) and
`struct_unsize_shape_ok`. It is NARROWER than the priced probe `dispunsize`,
which accepted ANY pair of structs sharing a mangled base name.

## PREDICTED FOR BUILD B

    D1  REFUSED -> rc=0     Rc<A> at an Rc<dyn Sp> INHERENT method param
    D5  REFUSED -> rc=0     the same at a TRAIT method
    D8  REFUSED -> rc=0     the same with the name OVERLOADED; the i64 arm
                            must still pick the i64 candidate (rc 0 covers both)
    D2  REFUSED -> rc=0     closure -> fn ptr at a method param
    D3 D4                   rc=0, UNCHANGED
    D6  REFUSED -> REFUSED  `W<bool>` at `W<i64>` — NOT an unsize (both field
                            types sized), so struct_unsize_shape_ok must say no.
                            The SENTENCE may change; it is READ, not assumed.
    D7  REFUSED -> REFUSED  `Rc<A>` at `Rc<dyn Other>` where A does not impl
                            Other: the SHAPE matches, so the selector now admits
                            the candidate and the refusal must come from the
                            coercion's own bound check. The sentence is READ.
    A1 A2 A3 G1-G8 S1 X1-X7 UNCHANGED from build A
    ROW method_with_unsized_wrapper_param_not_found  REFUSED -> rc=0  CLOSED
    ROW zonemut… CCFAIL 134 UNCHANGED   ROW fatslice… REFUSED UNCHANGED

## PREDICTED NUMBERS FOR THE ROUND

    rows CLOSED: 2      enum_variant_ctor_arg_no_unsize_coercion
                        method_with_unsized_wrapper_param_not_found
    rows MINTED: 1      the A1 refusal (array literal of closures at a declared
                        `[fn(i64)->i64; N]`) if it survives to the final binary
    new queue # TOTAL: 79 (80 - 2 + 1), re-derived BY DIRECT LISTING
    gate-run -L bc: 0 failures · run_oracle.py 0 of ~6555 changed
    stdlib-cost.sh all four layers ok · fail_text_oracle.py 0 changed
