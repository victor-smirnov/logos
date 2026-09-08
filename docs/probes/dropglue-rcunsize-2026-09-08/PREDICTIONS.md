# PREDICTED BY NAME, 2026-09-08, BEFORE the batch ran. build read 8e5f92705b285d29 43.

SHAPE ORACLE (sandbox shapes.logos, objdump call counts per emitting function),
baseline re-measured today and identical to the minting round:
  s_scope   Pay=1 Vec=1 OK | s_callee 0/0 + consume 1/1 OK | s_ifexpr 3/3 OK
  s_assign  Pay=2 Vec=1 WRONG | s_field Pay=2 Vec=0 WRONG | s_nested Pay=1 Vec=0 WRONG
  s_vecelem Vec<Pay>=1 OK (negative control: container element glue is NOT the shape)

PREDICTIONS
  dgall   moves ALL THREE wrong shapes (s_assign Vec 1->2, s_field 0->2, s_nested 0->1).
          Corpus cost NONZERO: it arms every gen_drop_value caller in the tree,
          including the deep-nesting recursions at :1024/:1042 the shapes never reach.
  dgrepl  moves s_assign fully and the ASSIGNMENT HALF of s_field only.
          s_nested UNCHANGED (Vec=0).
  dgnest  moves s_nested fully and the SCOPE-EXIT HALF of s_field only.
          s_assign UNCHANGED (Vec=1).
  rcunsz  refuses tests/soundness/open/rc_coerce_unsized_source_not_moved.logos
          with "use of moved variable 'rc'" (the exact sentence the Box control
          already prints today for `b as Box<dyn Sp>`). Fires HIGH, cost LOW:
          most CoerceUnsized sites coerce a temporary, and mark_moved_expr is a
          no-op on anything that is not a place.

ADDITIVITY, the claim under test (rule 13):
  on the SHAPES:  dgrepl + dgnest  ==  dgall.        (predicted TRUE)
  on the COST:    cost(dgrepl) + cost(dgnest)  <  cost(dgall).  (predicted TRUE,
  because dgall also arms :1024 / :1042 / :805 / :832 / :899 / :1086 / :1101 /
  :1364 / :1367 / :1372 / :1433, which neither site-probe touches.)
  => TWO SITES, ONE PREDICATE. If this holds, the minting round's open question
  is answered: replace_site_skips_field_drop_glue is NOT one site and NOT three
  roots — it is one defaulted parameter reaching two emission sites, and a fix
  must change the PREDICATE, not either site.
