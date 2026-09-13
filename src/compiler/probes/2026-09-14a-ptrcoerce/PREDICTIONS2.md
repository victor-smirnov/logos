# 2026-09-14a-ptrcoerce — BATCH 2 PREDICTIONS, BY NAME, before its build

Batch 1 (spec 6b7f0b2b3, build d54975fbc27f66b7 43) did not price: its L1 went red with NO probe armed on
`logos_00_key_identity_lint` ("subtype.hpp holds 1 bare-name intercept") — crosskindx's `struct_name() == "Vec"`.
Its HAND battery ran on that build and stands (106 programs × 8 configurations):
  R14 names: exactly as predicted, every door and neighbour; 0 legal programs moved.
  idxstoremut / amutrecv: moved NOTHING, J04 (lv1) included — PREDICTION WRONG. A census on lv1 shows the arm was
  never entered (`amutrecv.*` 0). Reading the site: method_self_kind resolves `index_mut` to 2 by base name, so the
  `sk == 0` gate skipped it, while extract_borrow_place cannot root a Code::AddrOf receiver and check_recv_conflict
  returns at `bp.root.empty()`.

Batch 2 changes: crosskindx loses the name test (body otherwise identical); idxstoremut/amutrecv are REPLACED by
aorecvsk (sk == 2 only) and aorecvty (any self kind, keyed on the receiver's MutRef TYPE). pcunion = crosskindx ∪ aorecvsk.

| probe      | predicted closed set                                         | certainty |
|------------|--------------------------------------------------------------|-----------|
| refptrco   | {type-check-pointer-coercions}                               | certain (hand, batch 1) |
| mutconstco | {type-check-pointer-coercions}                               | certain (hand, batch 1) |
| ptrcoerce  | {type-check-pointer-coercions}                               | certain |
| crosskindx | {type-check-pointer-coercions}                               | likely |
| aorecvsk   | {borrowck-loan-vec-content}                                  | UNCERTAIN: J04 should close; the row's store is in a closure passed beside `&v[0u64]` |
| aorecvty   | ⊇ aorecvsk                                                   | uncertain |
| pcunion    | {type-check-pointer-coercions, borrowck-loan-vec-content}    | additive iff aorecvsk holds |
Hand: J04 closes under aorecvsk, aorecvty, pcunion; B01 B03 B04 B06 B07 B08 B09 N01-N06 unmoved.
