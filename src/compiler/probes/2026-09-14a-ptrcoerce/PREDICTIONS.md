# 2026-09-14a-ptrcoerce — PREDICTIONS, BY NAME, before the build

Ledger population: bc_admits.ledger 69 rows (the harness's pass column).

| probe       | predicted closed set (bc_admits)                          | certainty |
|-------------|-----------------------------------------------------------|-----------|
| refptrco    | {type-check-pointer-coercions}                            | certain: 3 of its 7 doors are Ref/MutRef -> Ptr returns |
| mutconstco  | {type-check-pointer-coercions}                            | certain: its `mut_to_const` door is `*mut &'a -> *const &'b` |
| ptrcoerce   | {type-check-pointer-coercions}                            | certain (union of the two, non-additive: 1 ∪ 1 = 1) |
| crosskindx  | {type-check-pointer-coercions}                            | likely; no other row is known to spell `&mut T -> &U` or `&Vec -> &[U]` with unrelated regions — a SECOND row here would be a surprise and is to be read |
| idxstoremut | {borrowck-loan-vec-content}                               | UNCERTAIN: the row's store is inside a CLOSURE passed beside `&v[0u64]`; whether the closure body's MethodCall arm runs with the arg loan live is unmeasured |
| amutrecv    | ⊇ idxstoremut's set                                       | uncertain in size |
| pcunion     | {type-check-pointer-coercions, borrowck-loan-vec-content} = crosskindx ∪ idxstoremut, additive if both parts hold |

## Doors inside type-check-pointer-coercions (not the ledger's verdict, the diagnostics)
Predicted to refuse under ptrcoerce: shared_to_const, unique_to_const, unique_to_mut (refptrco), mut_to_const (mutconstco).
Predicted NOT to refuse under any name: array_elem, array_coerce, nested_array — each goes through an explicit `as` cast and an
ELIDED let annotation `*const &i64`; the elided region is the let-region plane (doors in series).

## Hand programs (scratch battery, base binary 6d1bbee8344b2a81 admits all 25 R14 programs and bck.B J04)
Illegal, predicted to CLOSE:
  I01 let `&mut &'a -> *mut &'b`       refptrco, ptrcoerce, crosskindx, pcunion
  I02 arg `& &'a -> *const &'b`        refptrco, ptrcoerce, crosskindx, pcunion
  I03 `*mut &'a -> *const &'b` return  mutconstco, ptrcoerce, crosskindx, pcunion  (NOT refptrco)
  I04 struct field `*const &'b` from `& &'a`  refptrco, ptrcoerce, crosskindx, pcunion
  I09 `&mut &'a -> *mut &'b` under `'a: 'b` (Inv)  refptrco, ptrcoerce, crosskindx, pcunion
  I05 `&'c mut &'a -> &'c &'b`          crosskindx, pcunion ONLY
  I07 `&Vec<&'a> -> &[&'b]`             crosskindx, pcunion ONLY
  J04 (lv1) `let e = &v[0]; v[1] = 4; *e`  idxstoremut, amutrecv, pcunion
Illegal, predicted NOT to close under any name (Logos-only coercions, not Rust's class):
  I06 `*const &'a -> &&'b`, I08 `&[&'a; 2] -> *const &'b`
Legal, predicted UNMOVED under every name: L01..L16, B01 B03 B04 B06 B07 B08 B09.
  Named risk: L07 / L08 (an ELIDED `*const &i64` annotation or param receiving `& &'a`) — if the elided region is
  compared as a named one they refuse, and that would condemn refptrco as written.
