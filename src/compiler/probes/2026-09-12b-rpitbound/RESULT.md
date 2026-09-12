# RESULT — round 2026-09-12b. The prediction in PREDICTION.md, adjudicated.

| predicted | actual |
|---|---|
| bc_admits closes exactly 1 row: `borrow-immutable-upvar-mutation-impl-trait` | **1 row, that one.** 84 -> 83 by direct listing |
| `impl-trait-captures` and `issue-95079` do NOT close | neither closed; both still compile rc 0 |
| soundness_queue closes 0 rows | 0 closed; **2 OPENED** (85 -> 87) |
| c1/c2/c3/c5 illegal, rc 0 before, refused after | all four, with the verdict read |
| L1/L2/L3/L5/L6/L7 legal, identical exit codes | 14 / 9 / 9 / 4 / 6 / 8 before **and** after |
| the 20 `-> impl X` arrivals: only the row changes | exactly one changed |

Not predicted, and each is in the commit message: the first version of the fix was
RED on `key_identity_lint`; the signature half came along free once the bound was
read where it was written (c7/c8); `bc_rpitbound_trait_unimpl_refuse` was already
red on the control at a distance, so `..._uncalled_refuse` was added as the half
that was genuinely invisible; and dlog's enumeration found `lower_let`.

`hand/` — the battery as run. c* illegal, L* legal, d* the `lower_let` hole.
