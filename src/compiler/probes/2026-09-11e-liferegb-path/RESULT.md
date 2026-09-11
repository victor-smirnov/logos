# RESULT — 2026-09-11e, `lifereg.B` CLOSED PER PLACE PATH

builds, all READ:
  base          411ed23b6b9fea05 43   HEAD b98e04040, unmodified tree
  armed-1       e1d3407e6c42b08a 43   the per-path record + census
  armed-2       8f9de85440fa72e3 43   + the `liferegbroot` control probe
  landed        315efdce1676c822 43   after the reconfigure that registers the
                                      four new fixtures (⚠ the hash moved on a
                                      reconfigure alone — `build_hash.py`
                                      identifies a BUILD, not a source state)

## THE ROW
`mut-slice-struct-lifetime-transmute--c17` CLOSED.
  BEFORE (base binary) ....... rc 0, silent.
  AFTER (landed binary) ...... rc 1,
    "error [fn lifetime_transmute_struct]: lifetime mismatch: return type has
     lifetime 'a but 'y' has lifetime (elided)"   — READ, and pinned in full in
    tests/imported/fail/lifetimes/mut-slice-struct-lifetime-transmute--c17.expected.
  Upstream: E0621 "explicit lifetime required in the type of `y`",
    tests/ui/lifetimes/lifetime-errors/mut-slice-struct-lifetime-transmute.rs
    @ da5114692c9ebe46b869488c5f34f92eb10b98c1.

## CLOSED SET, DIFFED BOTH WAYS
Over ALL 86 bc_admits programs, compiled on the armed binary:
  predicted (declared in PREDICTION.md before the first edit) = {--c17}
  actual                                                      = {--c17}
  predicted ∖ actual = EMPTY      actual ∖ predicted = EMPTY

## THE CLASS AND ITS ENUMERATION — dlog AND A PER-SITE READ, SIDE BY SIDE
The class is not "the field spelling". It is: **the `params` component of a
stored borrow is dropped at every holder-deposit door.** The members are the
DOORS. `tools/dlog` `provdep.dl` `holder_call`: **5 call sites / 3 contexts**.
Hand grep on the current source: **5 calls + 1 definition = 5 call sites**.
AGREE, term for term. (Recorded side by side because a dlog verdict has been
wrong once in the expensive direction — `ctx_of` coarsening, 37 vs 0.)
  :4965 outparam · :12971 assign · :13506 derefwrite(field/tuple) ·
  :13623 derefwrite(index_mut recv) · :15643 recvstore(push)
ONE structural change covers all five: the helper takes a PLACE PATH, and
`holder_path_params_[root][path]` is written BEFORE the escape early exit.

## THE COLUMNS
  L1 ................ 804/804, failure list READ = "(none)"; gates tier 143/143
  gates ............. the two POPULATION PINS were RED at first read and are
                      re-derived in this change, not weakened:
                        REGISTRY-ALL 9590 -> 9593 (+4 tests, -1 admit test)
                        REGISTRY-NOIMPORTED 5120 -> 5121
                        REGISTRY-TIERCOMMIT 144 -> 143
                        direct_door corpus 3036 -> 3038, nonglob 2845 -> 2847
                      Every delta is exactly what the change predicts.
  stdlib-cost.sh .... 4 of 4 layers compile
  `-L bc` ........... baseline READ from the store: build 1041, 6694 recorded,
                      0 failed — nothing had changed a test run could see
  queue gate ........ rc 0, 80 rows (78 + 2 OPENED)
  bc ledger gate .... rc 0, 85 rows

## THE COUNTER-EXAMPLES — MINE, IN SHAPES THE PRICING PHASE DID NOT USE
15 legal programs, 12 distinct shapes, ALL rc 0 on the base binary and ALL
still rc 0 on the landed one:
  N1 same-lifetime store · N2 `where 'b:'a` · N3 whole-value re-own ·
  N4 nested-path sibling · N5 holder not returned · N6 both branches safe ·
  N7 elided return 1 ref param · N9 `&STATIC` store · N10 tuple sibling ·
  N11 shadowed holder · N12 sibling under 'a ·
  N13/N14/N15 = the sibling FIELD, OVERWRITE and sibling ELEMENT shapes that
  condemned the 2026-09-11d arm.

## PROVEN LIVE IN BOTH DIRECTIONS — THE ADMISSIONS ARE NOT ZEROS (rule 1)
  bc_lifereg_path_sibling_admit  deposit 1, READ 0  (the sibling path misses)
  bc_lifereg_path_rewrite_admit  deposit 2, read 1  (the 2nd write REPLACED)
  N1_same_lifetime               deposit 1, read 1  — admitted by the LIFETIME
                                 rule (`src_lt == ret_lt`) with the fact PRESENT
Census over all 86 ledger programs: deposit 1 · door.derefwrite 1 · read 1.

## THE CONTROL TWIN (rule 18) — MEASURED THIS ROUND, NOT INHERITED
Probe `liferegbroot` collapses the key back to the ROOT and makes it additive:
the exact form declined 2026-09-11d. On ONE binary, armed / unarmed:
  bc_lifereg_path_sibling_admit   rc 1 / rc 0
  bc_lifereg_path_rewrite_admit   rc 1 / rc 0
  …--c17-samelt                   rc 0 / rc 0   ⚠ NOT a carrier for this form
So the two fixtures are carriers, and the third is recorded as not being one.

## WHAT IS STILL OPEN — TWO NEW QUEUE ROWS, EACH WITH THE CENSUS THAT LOCATES IT
  lifereg_deref_store_param_admits    deposit 0 — `*d = y` is SDerefWrite with
      ptr.kind()==VarRef and reaches NO door. THE DOOR IS MISSING.
  lifereg_container_elem_read_admits  deposit 2 (outparam door), read 0 — the
      index READ is a desugared method call and the projection walk stops
      there. THE READ SIDE IS MISSING, and repairing it prices over every
      `v[i]` in the stdlib. Its own round.
  --t17 / lifereg.NEW-B2 ............. 0 deposits at all five doors, as predicted.

## CONTRADICTIONS OF RECORDED CLAIMS
1. The prompt's "the ledger has stood at 90 rows / 54 roots for six rounds" was
   already stale when written (86 at HEAD); it is 85 now.
2. 2026-09-11d's recommendation "predicted ceiling still 1" is CONFIRMED, but
   its stated fundable shape was the dropck machinery *re-implemented*. It did
   not need re-implementing: the dropck path walker was made SHARED
   (`place_path_under`) rather than copied, which is what this file's own
   comment demands and what the 11d note did not say.
3. `note_holder_escape_prov`'s header still said "ONE helper, four call sites"
   (there are five) when 11d caught it; it still says it. NOT edited here —
   the sentence is in a compiled source and this round did not need to touch
   it; it is reported so the next round that opens the helper fixes it.
