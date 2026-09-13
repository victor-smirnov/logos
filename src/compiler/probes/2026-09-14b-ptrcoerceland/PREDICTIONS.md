# 2026-09-14b-ptrcoerceland — PREDICTIONS, written before any compiler edit (base build 6d1bbee8344b2a81 43)

## The change (two files)
1. include/logos/compiler/subtype.hpp::subtype — before the kind-mismatch exit, ask the kind the coercion LANDS in:
   `&T`/`&mut T` -> `*const U` (pointee Co; types_compatible's array decay compares the element) / `*mut U` (asked both ways);
   `&mut T` -> `&U` (region Co, pointee Co); `&Vec<T>`/`&mut Vec<T>` -> `&[U]` (element Co) / `&mut [U]` (element both ways);
   and the Ptr arm's `*mut T` -> `*const U` shape exit asks pointee Co.
   = pricing's crosskindxd + ONE strict extension (the `&mut [U]` target asked both ways, hand pair Y08 / Q10).
2. src/compiler/borrow_check.cpp MethodCall arm — a receiver extract_borrow_place cannot root that is a Code::AddrOf
   (an explicit `&mut v` / `&v`) is rooted at its variable before check_recv_conflict. = pricing's aorecvty
   (every self kind, is_mut = sk == 2), measured identical to aorecvsk in every priced column.

## bc_admits.ledger: 69 -> 67, closed set BY NAME
- type-check-pointer-coercions   (4 of 7 doors refuse; the 3 `as`-cast doors stay silent — elided let annotation)
- borrowck-loan-vec-content
No other bc_admits row moves. bc_admits_blocked unchanged (8).

## soundness_queue.ledger: 130 rows, predicted 0 rows move

## Hand battery (scratch land14a_hand), predicted verdict change vs base, BY NAME
Refused after (illegal, admitted on base): X01 X05 Y01 Y02 Y03 Y04 Y05 Y06 Y07 Y08
Uncertain: X06 (a struct-literal holder of `&v[0]`: whether the loan reaches shared_borrows), Y01 (FieldWrite site asks check_variance?)
Unchanged, still compile and RUN the same exit code: P01-P16 Q01-Q07 Q09 Q10 V01-V05 V09-V13 Z02
Unchanged refused on base: V08 (legal, separate row), Z01 (mlir_gen internal, separate row), X02 X04 W01 J03P J03S T1-T4
Text: J03S gains a second line (the push twin J03P prints two today); W01 stays ONE line.
