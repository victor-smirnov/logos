# ROUND 2026-09-07q — PREDICTED CLOSED SET, DECLARED BEFORE THE COMPILER IS TOUCHED
# HEAD at selection 0cdb8e22c, clean. build hash READ 95d01d3ae0a1858d 43.
# queue `# TOTAL` 63 = 63 by direct listing (tier1=21 tier2=6 tier3=33 tier4=3).
# bc_admits 98 / bc_admits_blocked 25. probe-log-lint 237. queue gate rc 0.
# gate-run -L bc rc 0, READ from the store ("Nothing has changed that a test run could see"),
#   with the inherited failure logos_05_integration_pass_http_workers_basic.

## THE CLASS, ENUMERATED BY PROPERTY (not by spelling)
PROPERTY: "a recursion into `build_pattern` whose TYPE ARGUMENT is a COMPONENT of the
door's own scrutinee". The default binding mode is a fact of the WALK; the code represents
it only as "the scrutinee type is a reference", so a door that hands a sub-pattern the BARE
element/field type destroys it and the sub-pattern's own door re-derives `move`.

The 12 recursion sites in sema_stmt.cpp, classified:
    3829  variant_data, tuple-STRUCT field sub         component   DOES NOT CARRY (out of scope, see below)
    4025  variant_data, nested VARIANT_DATA sub        component   CARRIES (rt wrapped at 4008)
    4095  variant_data, refutable-inner guard          component   DOES NOT CARRY (out of scope)
    4813  PAT_OR single alt                            whole       carries by construction
    4816  PAT_OR alts                                  whole       carries by construction
    5438  TUPLE door, scalar-literal element           component   DOES NOT CARRY  ← FIXED
    5449  TUPLE door, PAT_VARIANT_DATA element         component   DOES NOT CARRY  ← FIXED
    5498  TUPLE door, PAT_OR 1-alt scalar              component   DOES NOT CARRY  ← FIXED
    5502  TUPLE door, PAT_OR 1-alt VARIANT_DATA        component   DOES NOT CARRY  ← FIXED
    5526  TUPLE door, multi-alt PAT_OR                 component   DOES NOT CARRY  ← FIXED
    5693  PAT_AT / delegating                          whole       carries by construction
    5738  PAT_REF (`&pat`)                             component   MUST NOT CARRY (an explicit
                                                                   `&` RESETS the mode to move)
    5947  STRUCT door, field sub-pattern               component   DOES NOT CARRY  ← FIXED
    6062  SLICE door, element sub-pattern              component   DOES NOT CARRY  ← FIXED
SEVEN sites fixed by ONE helper (`dbm_sub_ty`), which is the same wrap `mint_dbm_ref`
already applies to a plain named binder at the same three doors.

## WHY 3829 / 4095 ARE NOT IN THIS LANDING (named, with the number)
They are `build_pattern_variant_data`'s own sub sites, and the cell they hold wrong is
c11/c12 (variant payload door × nested STRUCT or TUPLE pattern, measured 2 drops each on
the base binary). Their repair is NOT a type wrap: the payload door synths a BY-VALUE
binding (`binding_from_wild=false`) and emits a body `let <sub> = __synth;`, so repairing
it needs the destructure to run over a reference — which is soundness queue row
`let_tuple_destructure_ref_scrutinee` (OPEN: `let (s, b) = &p;` is refused today,
"right-hand side must be a tuple, got &(S, i64)"). A blocked row, not a declined one.

## ⚠ CONTRADICTS A RECORDED CLAIM
`nested_variant_payload_under_ref_double_drops`' own header table records
"variant door `match &e { Outer::W(Option::Some(a)) }`  1  CORRECT". Measured today on the
same binary 95d01d3ae0a1858d: that is true only for a nested VARIANT_DATA sub (c10 = 1).
The SAME door with a nested STRUCT sub (c11) and a nested TUPLE sub (c12) reads 2. The
variant door does not "carry the mode"; ONE of its four sub paths does.

## PREDICTED CLOSED SET — 2 ROWS, BY NAME
    nested_variant_payload_under_ref_double_drops   tier 1  run 2   -> exit 0
    match_ergo_nested_tuple_mut_admit               tier 2  admits  -> refused with the
        Rust-2024 sentence (`modifier_under_ref_scrutinee`), read in full
`# TOTAL` 63 -> 61 by direct listing, MINUS the new row below.

## PREDICTED NOT CLOSED — BY NAME
    match_ergo_ref_modifier_ref_mode_admit   a DIFFERENT fact (the `!explicit_ref` guard),
        and its price is the 12-site stdlib `match *self` rewrite. Not in this landing.
    match_tuple_door_nested_struct_binds_nothing / let_tuple_destructure_ref_scrutinee /
    arrayelem_default_ref_mode_not_minted    unchanged.

## PREDICTED NEW ROW
    variant_payload_nested_struct_sub_double_drops  tier 1  run 2   (c11/c12)

## PREDICTED HAND MATRIX (base -> armed), 24 programs, oracle = destructor COUNT
    CLOSE 2 -> 1 : c01 c03 c04 c05 c06 c07 c13 c14 c15 c19        (10)
    STAY  2      : c11 c12                                        (2, the blocked pair)
    STAY  1      : c08 c10 c21 a01 a02                            (5)
    STAY  0      : a03 a04                                        (2)
    STAY REFUSED : c02 c16 c17 c18 c20                            (5)
