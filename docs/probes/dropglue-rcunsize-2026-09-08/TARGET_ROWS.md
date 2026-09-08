# ROUND TARGET — written 2026-09-08 BEFORE touching the compiler
# build read: 8e5f92705b285d29 43 (identical to the build the three rows were minted on)
# queue gate rc: 0, 73 rows / 73 programs / # TOTAL 73

PRIMARY   replace_site_skips_field_drop_glue   (tier 1, run 11)
SECONDARY rc_coerce_unsized_source_not_moved   (tier 2, admits)
NOT PRICED unsized_local_binds_place_dropped_after_free (tier 2, admits)

WHY THIS BLOCK, and why in this order:

1. replace_site_skips_field_drop_glue is THE ARM-THAT-EXISTS SHAPE. The correct
   emission is already written and already runs: gen_drop_value's struct arm at
   mlir_gen_stmt.cpp:990 recurses a value's fields after its user Drop::drop
   when `top_level` is true. The defect is not a missing arm; it is that
   `top_level` DEFAULTS TO FALSE (mlir_gen_impl.hpp:1973) and only two callers
   in the whole tree pass true (mlir_gen_dyn.cpp:1018, :1067). Every other
   drop-emission site therefore calls the user destructor and STOPS.
   That is "a fact the code does not carry", reaching an arm that exists.

2. It has a DENOMINATOR the other two do not: the minting round measured 32 of
   the 90 leaking corpus fixtures leaking through vec_new reached from their own
   `mk`, which is this shape. Rows 2 and 3 are each a reduction of ONE corrupt
   fixture.

3. rc_coerce_unsized_source_not_moved comes second because it already has a
   WORKING CONTROL INSIDE THE COMPILER: `Box<A> as Box<dyn Sp>` consumes its
   source (0 valgrind errors, re-verified today) and `Rc<A> as Rc<dyn Sp>` does
   not (3 invalid accesses, re-verified today). Two paths, one right; that is a
   diffable site, not a new rule.

4. unsized_local_binds_place_dropped_after_free is DELIBERATELY NOT PRICED this
   round. Closing it reds tests/logos/pass/custom_dst_smartptr_owning_drop,
   a pinned-green fixture that commits the use-after-free (re-verified today:
   2 invalid reads under valgrind, rc 0 unarmed). That is a corpus decision
   with an owner. Pricing a refusal whose only cost column is a fixture I am
   forbidden to edit would produce a number nobody can act on.

THE QUESTION THE MINTING ROUND LEFT OPEN, and which this round must answer
BEFORE any grouping claim: are the three failing shapes ONE root or two?
Predicted decomposition, written before the run (rule 13, credit is per SET):
  * s_assign + the assignment half of s_field arrive at the B8 drop-before-
    replace site, mlir_gen_stmt.cpp:2464 / :2475 / :2480 — bare
    `gen_drop_value(it->second, val_ty)`, top_level defaulted false.
    ⚠ its own comment on :2444 says "gen_drop_value runs the full destructor
    (user Drop impl + owned children)". That comment is FALSE at the default.
  * s_nested + the scope-exit half of s_field arrive at the scope-exit field
    recursion, mlir_gen_stmt.cpp:1339 (structs) / :1357 (arrays) — EXPLICIT
    `/*top_level=*/false`.
  So it is TWO SITES and ONE PREDICATE. Neither site alone can move all three
  shapes; the predicate can. Flipping the DEFAULT alone also cannot, because
  :1339 passes false explicitly. This is the additivity claim to test, not
  assume.
