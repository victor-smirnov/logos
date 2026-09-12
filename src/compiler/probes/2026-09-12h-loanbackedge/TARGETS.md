# ROUND 2026-09-12h — TARGET ROWS, WRITTEN BEFORE THE COMPILER WAS TOUCHED
build read: 4e8255bd5a2f8397 43   HEAD 832195eac (clean)

## TARGETS (bc_admits.ledger), BY NAME
  borrowck-lend-flow-loop                  bck.NEW-2     E0502
  two-phase-across-loop                    bck.NEW-L     E0499
  issue-62007-assign-differing-fields--c   nllmoves.R3   E0499/E0500

## NON-TARGETS carried for the both-ways diff (predicted NOT to move)
  buffer-reuse-pattern-issue-147694        bck.NEW-L     E0597  (referent dies, holder lives)
  issue-53773                              nllmoves.E    E0713  (destructor observes)

## WHY THIS BLOCK, AND NOT THE OTHERS
* THREE ROOT IDS, ONE PROPERTY. A root-id grep cannot see this class: the three
  rows are `bck.NEW-2`, `bck.NEW-L`, `nllmoves.R3` in three different blocks.
  The property: *a loan raised inside a loop body whose HOLDER is an OUTER
  binding must be live at the back edge*.
* THE ARM EXISTS AND FIRES; ONE FACT IS NOT CARRIED. Measured today on
  4e8255bd5a2f8397, hand programs L1/L3/L4 (scratchpad):
    L1 straight-line `&mut v` then `borrow(&v)`      -> REFUSED, correct sentence
    L3 loan minted BEFORE the loop, use inside       -> REFUSED
    L4 loan minted INSIDE the loop, use AFTER it     -> REFUSED
    L2 loan minted at the body BOTTOM, use at TOP    -> ADMITTED   <= the row
    L5 same under `loop{}`                           -> ADMITTED
    L6 same over a struct field, no deref            -> ADMITTED
  So loans are recorded, flow forward, and cross the loop exit. What they do
  not do is reach the BACK EDGE.
* THE SITE IS NAMED IN THE FILE, WITH ITS OWN DECLINE, AND THE DECLINE IS
  DATED. borrow_check.cpp `visit_loop_body` (~11815) already calls
  `merge_loans(back_edge, post1_s)`; its own comment says post1_s "carries less
  than the truth" because `pop_scope`'s Door-B RE-HOME is gated on
  `!suppress_reports_` (line 2865), so in pass 1 (the dry run) a loan whose
  holder outlives the body is RELEASED at the body's `}`. The comment records
  the measurement that condemned ungating it: corpus 2048/2048 green, stdlib
  `mem` stops compiling (`plan_walker.walk_program_params`, element-insensitive
  Vec model). A WALL DECAYS — that number is what this round re-measures.
* WHY NOT the alternatives surveyed: `bck.NEW-BLOCKREF`
  (borrowed-referent-issue-38899) is a `&mut &mut` reborrow with no loop and no
  named arm; `nllmoves.R14`/`R2` (pointer coercions/comparisons) need region
  VARIANCE, which nothing in the tree models — a missing-subsystem root, not an
  absent fact; `nllmoves.R13`/`NEW-3` (`impl T<'_>` anonymous impl region) is
  the same shape at the lifetime layer. `lifereg.B`/`NEW-B2` excluded by prompt.

## GROUPING CLAIM, TO BE TESTED NOT ASSUMED
Does ONE candidate change move all three? Predicted YES for the three targets
and NO for the two non-targets. Diff BOTH ways.

## PROBE
`loanrehome` — drop `!suppress_reports_` from pop_scope's Door-B re-home gate
(borrow_check.cpp:2865), leaving `scopes_.size() >= 2`.
