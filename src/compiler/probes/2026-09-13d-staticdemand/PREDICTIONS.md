# 2026-09-13d-staticdemand — PREDICTIONS, copied verbatim from the files written before each step (timestamps: ls of the scratch originals)

<!-- PREDICT_battery_1789302676.md written 08:31:16 -->
# Predictions for MY battery under the pricing names, written 08:3x BEFORE build-land0913d finished (no armed output read)
## M2
cooutsites (mapped only):  illegal closed = {x03, x05, x06}; NOT x01 x02 x04 x08 x09
                           legal moved    = {e12?}  (pick's ret region may map 'a to the callee's 'b)
cooutsitese (+ empty):     illegal closed = cooutsites + {x01, x02, x08}; NOT x04 (first mapping wins) NOT x09 (receiver Ref vs Struct: no record)
                           legal REFUSED  = {e01 e02 e03 e05 e08 e10} + possibly {e04 e09 e11 e12}
                           => cooutsitese condemned if any of these moves; empty ≠ absent (rule 16)
## M1
stmutdeclx:  illegal closed = {y02 y04 y05 y06}; NOT y01 (Struct lifetime arg not filled), NOT y03 (call arg variance permissive)
             legal REFUSED  = {r08} (as r08c on base) + possibly {r02 r03 r06 r07 r09 r12 r13}
             legal FIXED    = {r16}? (base refusal was the empty inner region)

<!-- PREDICT_landing_1789302976.md written 08:36:16 -->
# LANDING PREDICTION — written BEFORE any landing edit (tree reverted to HEAD 148299718, 08:4x)
## bc_admits rows closed: 2 — {issue-69114-static-mut-ty (nllmoves.R1), regions-static-bound (lifereg.L2)}; # TOTAL 74 -> 72
## M2 (check_call_outlives: 'static short side checked on the MAPPED caller region, transitive; helper called at exact + generic + method + T::f)
  illegal closed (hand): x01 x02 x03 x05 x06 x09 + pricing c07 d26 m2i_named_caller m2i_struct_arg m2i_mut_ref m2i_method_param m2i_generic_T_where_static m2i_static_method
  variant `cooutmulti` (every caller region of a callee region checked, not the first): + x04
  NOT closed: x08 c06 m2i_let_local_ref d24 (EMPTY argument region; its arm refuses legal e01 e02 e05 e08) · closure/fn-ptr invoke (no where-clause carrier)
## M1 (a write to a `static mut` compared against the static's declared type with elided regions read as 'static — AT THE WRITE, not at the declaration)
  bare write + recursion (Ref/Array/Tuple/Slice): row + y04 y06 + pricing c09 c10 m1i_param_elided m1i_named_param m1i_nested_ref m1i_tuple_elided
  place write rooted at a static mut (no deref hop): + y05 + pricing m1i_array_elided
  variant `stmutstruct` (a Struct's elided lifetime args also 'static): + y01
  NOT closed: y02 y03 (reach the static's type through a BORROW = a read; decl-fill closes them and refuses legal r04 r08 r17 — doors in series with the base over-refusal k04 k17 k17c)
## legal moved: 0 in my 36 legal + base-refused controls, 0 in pricing's legal battery
## cost: pass 0 · cfail rc 0 · stdlib ok · runtime 0 · fail_text: 0 expected changes (method-site non-'static pairs are new only where no fixture has the shape)

<!-- DLOG_PREDICT_1789303912.md written 08:51:52 -->
# dlog static_demand_sites.dl on the LANDED sema_expr.cpp — known answer stated BEFORE the run
outlives_askers = {lower_call, finish_generic_call, lower_method_call, lower_static_call, try_method_on_dyn}   (was {lower_call} on 148299718)
arg_site_blind  = {expect_type, lower_enum_lit_data, lower_enum_lit_data_from_static, lower_generic_ref, lower_invoke_expr, lower_struct_lit, type_bounds_satisfied_quiet}
  (the 4 contexts that moved are exactly the 4 that gained a call; lower_struct_lit asks through its sibling; lower_invoke_expr has no where-clause carrier)

