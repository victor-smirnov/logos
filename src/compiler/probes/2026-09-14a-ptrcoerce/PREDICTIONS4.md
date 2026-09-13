# 2026-09-14a-ptrcoerce — BATCH 4 PREDICTIONS, BY NAME, before its build

Why batch 4: batch 2's RUNTIME column (build 041a5b988a104e81, one configure, unarmed 16:20:46 -> 16:30:41, pcunion
16:30:41 -> 16:40:38; 6737 common, 0 lost, 0 added, cast-region-to-uint subtracted by name) moved THREE legal pass fixtures
from run 42 to COMPILE REFUSED under pcunion, every other column 0: tests/logos/pass/array_ref, struct_ptr_field,
while_search. Each passes `&mut a` over `a: [i32; N]` where `*mut i32` is expected — types_compatible's array DECAY. By
reading, refptrco compares `[i32; N]` with `i32` under `*mut` invariance. aorecvsk cannot be involved (no method receiver).
The attribution is measured here by hand: `refptrco` stays armable in the batch-4 code without being priced.

| probe       | predicted closed set                                         | certainty |
|-------------|--------------------------------------------------------------|-----------|
| refptrcod   | {type-check-pointer-coercions}                               | certain (3 of its doors are not decays) |
| ptrcoerced  | {type-check-pointer-coercions}                               | certain |
| crosskindxd | {type-check-pointer-coercions}                               | likely |
| pcuniond    | {type-check-pointer-coercions, borrowck-loan-vec-content}    | additive |
Runtime column on pcuniond: 0 changed after subtracting cast-region-to-uint (predicted).
Hand: the three fixtures REFUSE under `refptrco` and `crosskindx`-era spellings (attribution) and COMPILE and RUN 42 under
every `d` name; decay hand programs: D01/D02 legal unmoved, D03 (`&mut [&'a i64; 2] -> *mut &'b i64`) closes under the
`d` names (a decay door the element peel now reaches — Logos-only coercion, reported not claimed).
