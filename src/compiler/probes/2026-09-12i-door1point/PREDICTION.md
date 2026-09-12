# ROUND 2026-09-12i — PREDICTION, WRITTEN BEFORE THE PREDICATE WAS INSTALLED
build read at the time of writing: 4e8255bd5a2f8397 43 (HEAD ebbdb3cd0, clean)
soundness_queue gate rc 0 · 89 rows · bc_admits 82 · bc_admits_blocked 8 ·
probe-log-lint 275 records, every site symbol resolves.

## WHAT IS BEING LANDED
Round 2026-09-12h priced the class and DECLINED all five of its arms by name.
Its recommendation — not a verdict (rule: the pricing phase's recommendation is
not one) — was to re-spell DOOR 1 as a PROGRAM-POINT predicate instead of the
crude "a mut loan never expires inside a loop body":

  DOOR 1. A MUT loan RAISED at point P inside a loop body must not be retired
  by a holder last-use at point Q with body_first <= Q < P in the SAME body:
  the back edge makes Q LATER than P in time, and `holders_last_use` is a
  per-function MAXIMUM over SOURCE LINES that cannot see that.
  Outside that window the ordinary NLL rule is unchanged — in particular
  Q > P (the holder's last use BELOW the raise) and Q < body_first (the
  holder's last use BEFORE the loop) both still retire.

  DOOR 2. The pass-1 (dry-run) re-home in `pop_scope` is gated on
  `!suppress_reports_`, so the very pass whose job is to compute the back-edge
  state releases a loan whose holder outlives the body. Ungate it FOR MUT LOANS
  ONLY — the shared half is what breaks `walk_program_params`/`src_bufs`
  (measured 2026-09-12h: `loanbk1` ⛔ stdlib, `loanbk2` mut-only ok).

They are IN SERIES (rule 2): 2026-09-12h measured 0 + 1 = 3 for the crude pair.

## THE NUMBER: **2** LEDGER ROWS

## THE LIST, BY NAME
CLOSED (predicted):
    borrowck-lend-flow-loop                  bck.NEW-2     E0502
    two-phase-across-loop                    bck.NEW-L     E0499

NOT CLOSED (predicted to stay):
    issue-62007-assign-differing-fields--c   nllmoves.R3   field-path loan; the
        field loop is a second copy of the rule and its own arm `fldnlldrop` is
        declined at ceiling 0 / cost 13. The 2026-09-12h grouping claim was
        REFUTED for this row and is not re-made.
    buffer-reuse-pattern-issue-147694        bck.NEW-L     referent dies, holder lives
    issue-53773                              nllmoves.E    destructor observes

MUST NOT BE REFUSED (it is not a row at all):
    issue-75904-move-closure-loop — `bc_admits_blocked` bucket 3, blessed
    divergence A16 structural auto-Copy, LEGAL Logos. The crude arm "closed" it;
    that was a legal-program refusal counted as a purchase, because
    `ceiling-probe.sh` counts `logos_00_bc_admit_*` tests and the 2026-09-04
    split left those on BOTH ledgers' programs.

## THE COUNTER-EXAMPLES, WRITTEN BEFORE THE PREDICATE (`legal/C1..C12`)
Twelve legal programs in shapes the pricing round's battery did NOT contain —
its twelve never reassigned a holder inside a loop, which is the one shape this
predicate is most likely to get wrong. C1 is the ledger row
`borrowck-lend-flow-loop` with its `borrow(&v)` line DELETED: ONE TOKEN APART
from the program door 1 must refuse, and legal Rust (the old loan's last use is
`*x` at the top, so it is dead when `x = &mut v` raises the new one).
C3 is the lower-bound control (holder's last use BEFORE the loop — must still
retire). C6 is the shared-loan control (door 1 is mut-only). C4 nests the loops,
C5 shadows the holder name across two loops, C9 raises inside a bare block,
C11 reborrows a parameter, C12 asserts an arithmetic RESULT and not just rc 0.

## THE SIX FIXTURES THE CRUDE ARM REVERTED — ALL SIX MUST STAY GREEN
bc_d3_loop_bare_block_release_admit · bc_nll_d1_for_ref_admit ·
bc_nll_d1_loop_closure_admit · bc_nll_d1_loop_ref_admit ·
bc_patmut_for_arr_recv · bc_patmut_for_vec_recv
They are the D1 fixtures that BOUGHT intra-body NLL. Their holders' last uses
are BELOW the raise (Q > P), which the corrected predicate leaves alone and the
crude arm did not. If any of them reds, door 1 as re-spelled is refuted and the
round declines it by that number.

## PLUMBING, INSTALLED FIRST AND INERT
`BorrowRecord::raise_line` (no other field carries the raise point) and
`LoopFrame::body_first_point` (no other field says whether a line is inside THIS
body), plus `first_point_of(BlockRef)`. Both are WRITTEN and never READ at this
commit, so this commit must not move any verdict.
