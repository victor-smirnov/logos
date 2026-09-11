# PREDICTION — written BEFORE the armed binary existed
probe `liferegbparam` at src/compiler/borrow_check.cpp::note_holder_escape_prov

MECHANISM (read, then to be measured): the helper records the ESCAPE fact only
(`is_local` / `is_temp`) and NEVER `params`, and its own comment says the skip is
deliberate. A store of a PARAM-rooted borrow into a holder therefore deposits
NOTHING, and `check_return_value`'s explicit-lifetime arm (case 2, :9396) —
which DOES fire, measured today, on the root-local spelling — never learns the
param fed the binding. The arm exists; the fact does not reach it.

CEILING, PREDICTED BY NAME (86 ledger programs):
    { mut-slice-struct-lifetime-transmute--c17,
      mut-slice-struct-lifetime-transmute--t17 }      = 2, the whole root.
No other row predicted. If a third row closes it is a set difference to report.

HAND PROGRAMS, PREDICTED VERDICT UNDER THE ARM:
  X1 (field, through the `&mut Struct` reborrow) ... REFUSED   (= --c17)
  X2 (index, through the `&mut [_]` reborrow) ...... REFUSED   (= --t17)
  X3 (deref, `*d = y` through `&mut &i64`) ......... REFUSED — NO ROW EXHIBITS
      THIS SHAPE. Measured admitted on the base binary today. A closing with
      no row is reported, never counted.
  X4 (field, NO reborrow) .......................... REFUSED
  X5 (index, NO reborrow) .......................... REFUSED
  L1 sibling FIELD still carries 'a ................ admit  ⚠ at risk: the
      deposit is ROOT-keyed and additive, so a param stored into `p.b` will
      also be seen when `p.a` is returned. PREDICT REFUSED = an over-refusal.
  L7 sibling ELEMENT ............................... same risk, PREDICT REFUSED
  L5 OVERWRITE (`u.h = y;` then `u.h = x;`) ........ legal Rust, PREDICT
      REFUSED — the deposit is OR-ed, never cleared (the helper's own comment
      says clearing loses the sibling borrow).
  L2 both params 'a ................................ admit
  L3 `where 'b: 'a` ................................ admit (outlives_named)
  L4 `&'static` source ............................. admit
  L6 / L8 non-reference return ..................... admit (case 2 not reached)
  L9 the reborrow store is of `x` itself ........... admit
  L10 holder read back through a call .............. admit
  L11 the store is in a CALLEE taking `&mut H` ..... admit (params skipped, #78)

So the arm is predicted to buy 2 rows and to over-refuse at least three legal
shapes (L1, L7, L5), all three invisible to a ledger ceiling and to the pass
corpus unless the corpus happens to contain them — which is what the cost
columns are for. THE PREDICTION IS THAT THIS ARM IS NOT FUNDABLE AS SPELLED and
that the fundable shape is PER-FIELD (place-keyed), not root-keyed.
