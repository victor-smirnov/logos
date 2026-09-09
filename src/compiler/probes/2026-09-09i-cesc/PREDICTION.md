# 2026-09-09i-cesc — PREDICTION, WRITTEN BEFORE THE ARMED BINARY EXISTED

Base `06f0c66be0400a1d 43` (HEAD `02c79cfba`). Base binary preserved and proven
LIVE before any prediction was written: it reproduces all 8 ledger rows as
ADMITTED and refuses hand shape I4 with the recorded object-lifetime sentence.

## THE BASE MATRIX (measured first, so the prediction is against data)

    L1 boxed `dyn Fn + 'a`, capture is the `&'a i64`          ADMITTED  correct
    L2 boxed `dyn Fn` 'static, owned move capture             ADMITTED  correct
    L3 non-move closure capturing a `&mut i64`, called here   ADMITTED  correct
    L4 move closure over `&'static mut`, boxed 'static        ADMITTED  correct
    L5 closure with a `&i64` PARAM returning a scalar         ADMITTED  correct
    I1 `|| { return &mut x; }`   (rustc E0521)                ADMITTED  DEFECT
    I2 `move || { b = Some(&a); }` (rustc E0521)              ADMITTED  DEFECT
    I3 `|| { doit(data); }`, data `&'static mut` (rustc)      ADMITTED  DEFECT
    I4 boxed `dyn Fn + 'a`, capture is a LOCAL's borrow       REFUSED   correct

L1 vs I4 is a one-variable pair — only the captured region changes — and the
EXISTING arm gets both right. So the object-lifetime door already carries the
region fact for captures; whatever `issue-95079` is missing, it is not that.

## PREDICTION, BY NAME

`cescbound` — "a capture can never discharge a non-'static object bound".
  REFUSES:  issue-95079-missing-move-in-nested-closure   (1 of the 8 ledger rows)
            I4 (already refused at base — INHERITED, buys nothing: rule 14)
            L1  ⚠ A LEGAL PROGRAM. Predicted as a COST, not as a surprise:
                the base pair above shows the arm already separates L1 from I4,
                so a crude "any capture" form must lose L1.
  MOVES NOTHING ELSE in the block: the other seven rows contain no `dyn`
  coercion at all, so the arm is never reached. Predicted ceiling on the block
  = 1 of 8, and the predicted `pass` cost is NOT zero.

`cesccapmut` — "a by-reference capture of a `&mut` escapes".
  REFUSES:  issue-42574-...--b and --t15  (2 rows, one program)
            I3
            L3  ⚠ A LEGAL PROGRAM (`&mut i64` param captured by a closure that
                is called in place). Predicted as a COST.
  DOES NOT MOVE: 40510-1 (capture is `Box<i64>`, not a `&mut`), L4 (a `move`
  capture, `by_ref` false), and the four remaining rows.

`cescarr` — census only, no verdict changes anywhere. Its ONE job is rule 1:
  prove which of the 8 rows reach `check_object_lifetime_bound` at all.
  PREDICTED: exactly one (issue-95079). If any other row censuses an arrival,
  this prediction is wrong and the door map above is wrong with it.

## WHAT THE PREDICTION ALREADY COSTS THE GROUPING

If both predictions hold, no single change at either site moves more than two of
the eight rows, and the two arms move DISJOINT rows. `NEW-CESC` would then be at
least four mechanisms wearing one root name — the same result the last two
rounds got for the two handed-down groupings they tested.
