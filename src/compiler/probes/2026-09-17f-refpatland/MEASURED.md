# ROUND 2026-09-17f `refpatland` — MEASUREMENTS, with the binary each was read on

base binary   build_hash 068a4dab393e7aae 43
landed binary build_hash 1d898d8dbc37f4a5 43

## rustc 1.98.1 --edition 2024, measured 2026-09-17 (there IS a rustc on the box)
rust/ref_pattern_nested_in_tuple_...rs   compiles, runs, exit 0     (row is LEGAL)
rust/letelse_ref_pattern_...rs           compiles, runs, exit 0     (row is LEGAL)
rust/let_ref_struct_pattern_...rs        compiles, runs, exit 0     (row is LEGAL)
rust/nE.rs   rc 0 legal        rust/xA.rs rust/xB.rs rust/xC.rs rust/xD.rs   ALL E0507

## logosc, base -> landed
hand/h_tuple.logos           REFUSED "undefined variable 'x'"  ->  got=6, exit 0, warn=0
hand/nE_ref_legal.logos      REFUSED "undefined variable 'q'"  ->  got=2 1, exit 0, warn=0
hand/xA_structfield.logos    refused BY ACCIDENT               ->  refused, REAL E0507 ('d')
hand/xB_variantpayload.logos refused BY ACCIDENT               ->  refused, REAL E0507 ('d')
hand/xC_nestedtuple.logos    refused BY ACCIDENT               ->  refused, REAL E0507 ('d')
hand/xD_at_under_refpat.logos refused BY ACCIDENT              ->  refused, REAL E0507 ('whole')

## diagnostics that refuted a suspected miscompile (the defect was in MY harness)
hand/d1_if_first.logos  exit 0 got=6      — comparison BEFORE the call is fine
hand/d2_twice.logos     exit 0 x=5 j=1 twice — the binding survives an intervening call
valgrind on h_tuple: 0 errors. The `run_rc=1` reading came from `run_rc=$?` placed after a
command substitution on the same line, so `$?` was grep's status. Rule 18.
