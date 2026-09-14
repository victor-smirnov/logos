# 2026-09-14m-storeedge-land — PREDICTIONS, written before the armed build was read

Site: apply_flow_outparams, the A2 `reborrow_of_.add(dst, p)` loop. Names (one build, build-land0913d):
- `se_nomut`     — no A2 alias edge when the stored operand holds NO `&mut` (`bc_holds_mut_ref_type` false): the summarizer's
                   own U2 rule ("a deposit travels through `&mut` and nothing else") read on the checker side.
- `se_nomutelem` — se_nomut OR the priced element test (operand type == an element type argument of the out-param's type).
- `a2elemorshr`, `a2shr` — the 14l arms verbatim, as controls on this build.

## bc_admits
- se_nomut, se_nomutelem: closes {buffer-reuse-pattern-issue-147694} (1). Nothing else.

## soundness_queue (rows that stop reproducing)
- se_nomut: 4 — vec_push_after_outer_push_store_rehomed_{admits,refused}, setter_field_store_after_outer_set_rehomed_{admits,refused}.
- se_nomutelem: 6 — the 4 above + vec_mutref_elem_push_store_rehomed_{admits,refused}.
- no other row moves under either.

## fail-text
- se_nomut: 0 changed. se_nomutelem: 1 text-only (issue-62007-assign-differing-fields--t22, the aggregate-param false line).

## new hand battery (seland hb/, base verdicts measured on build/ 0230e503bd682184)
- s35 (Option::replace, block local, illegal, base ADMITS): REFUSED under both names.
- s35b (legal twin): compiles and runs 0 under both (as base).
- s28 (two Vec holders of &x dead at block end, then `x = 4`, legal, base REFUSES): COMPILES, runs 0 under both.
- s34 (Option::replace(&x) then `x = 2` while o used, illegal, base ADMITS): REFUSED under both (E0506 through the loan channel).
- s36 (`Vec<&mut i64>` block push, source used after the Vec dies, legal, base REFUSES): unmoved under se_nomut; compiles under se_nomutelem.
- s22 / s22b (setter storing `&mut` into a field; illegal admitted / legal refused on base): UNMOVED under every name (operand holds &mut, M has no type args).
- s26 (RefCell interior push through a shared holder, illegal, base ADMITS): unmoved (still admitted) — not modelled on base at all.
- s02, s03 (copy an element out / move the holder, then mutate the source; illegal, base ADMITS): unmoved — not this fact.
- s37 (free-fn out-param fill of a block local; illegal, base ADMITS): unmoved (the c22 row's Call arm).
- s18 (generic W<U> field `&mut` wire, illegal, base REFUSES): STAYS REFUSED under se_nomut; UNCERTAIN under se_nomutelem / a2elemorshr
  (operand `&mut Vec<&C>` equals W's type argument, so the element test drops A2's own edge).
- every other base-refused illegal program stays refused with the same first error; every base-compiled legal program compiles and runs as base.
