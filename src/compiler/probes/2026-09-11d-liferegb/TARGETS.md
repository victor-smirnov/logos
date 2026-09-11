# TARGET ROWS — written BEFORE any compiler source was read for editing
# 2026-09-11d, block `lifereg.B`

ROWS, BY ID (both rows of the root, the whole root):
  1. mut-slice-struct-lifetime-transmute--c17   lifereg.B
     tests/imported/admit/lifetimes/mut-slice-struct-lifetime-transmute--c17
     FIELD door: `{ let dst: &mut Struct<&i64> = &mut out; dst.head = y; } return out.head;`
  2. mut-slice-struct-lifetime-transmute--t17   lifereg.B
     tests/imported/admit/lifetimes/mut-slice-struct-lifetime-transmute--t17
     INDEX door: `{ let s: &mut [&i64] = &mut out; s[0u64] = y; } return out[0u64];`

WHY THIS BLOCK OVER THE OTHERS (the next round inherits this reasoning):
  * Shape the prompt says has paid every time: AN ARM THAT EXISTS, reached
    through A FACT THE CODE DOES NOT CARRY. The arm is
    `borrow_check.cpp:9396-9500` (return-type explicit-lifetime check) and it
    FIRES on the root-local spelling today. The fact it is missing is the
    flow of `y` into `out` through a projection/reborrow hop.
  * Never surveyed: `lifereg.B` has 2 literal hits in PROBES.md and no
    `lifereg*` probe at the store site. Checked BY PROPERTY, not by name grep:
    the two installed `lifereg_*` probes at the return site
    (`lifereg_retinner`, `lifereg_aggtrust`) both read PARAM lifetimes and
    neither touches provenance deposit.
  * The other candidates, and why not:
      bck.B / nllmoves.B (2+2) ...... 88-112 PROBES.md hits, heavily worked.
      bck.D + nllmoves.D (4+3) ...... the ledger's own 2026-09-08 DECLINE.
      *.NEW-CESC (3+4) .............. worked 2026-09-09i/j, split three times.
      bck.NEW-CAPMOVE (3) ........... looks untouched by name (1 hit) but the
                                      SITE already carries five installed arms
                                      (capmove ⛔ 2/3, capmoveloan 1/0,
                                      capmovety, capmovedrop, capmoveref).
      bck.NEW / bck.NEW-A16 ......... A16 is a blessed divergence (owner).
      argresvact .................... blocked on the owner by the prompt.
  * The upstream oracle is ON THE BOX and READ: one upstream file,
    `tests/ui/lifetimes/lifetime-errors/mut-slice-struct-lifetime-transmute.rs`,
    two E0621 errors, blame at the READ-BACK (`out[0]` / `out.head`), not at
    the store. The two rows are that file's two doors.

GROUPING TEST THE PROMPT DEMANDS (does ONE candidate change move BOTH members?):
  the two rows differ only in the PROJECTION KIND (field vs index). The
  candidate change is at the provenance deposit for an assignment to a
  projected place, which is one site for both. PREDICTION: they move together.
  A third projection kind (deref, `*dst = y`) is ALSO broken and has NO ROW —
  measured below; if the arm closes it too, that is a closing with no row and
  must be reported, not counted.
