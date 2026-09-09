# TARGET ROWS — round 2026-09-08(c), the owner's Rust-canonical drop decision
Written BEFORE any compiler source was touched. Build read: eaac8e4b73c1fe24 43.
Queue gate rc 0, 81 rows by direct listing, `# TOTAL 81`.

## THE BLOCK, BY ID (soundness_queue.ledger)

  1  drop_body_moving_field_double_drops_local            tier 1  run 1
  2  drop_body_conditional_move_double_drops_both_paths   tier 1  run 1
  3  letstruct_destructure_skips_user_drop                tier 1  run 100
  4  enum_user_drop_skips_payload_glue                    tier 1  run 1
  5  replace_site_skips_field_drop_glue                   tier 1  run 11

Adjacent, NOT targeted this round (named so the next round inherits the reason):
     struct_fields_dropped_reverse_order  tier 3 run 1
     tuple_elems_dropped_reverse_order    tier 3 run 1
     — the drop-order half of "field order is Rust's". A separate edit at the
       same two loops; landing B does NOT move them and they must not ride along.
     enum_payload_partial_move_leak       tier 1 run 1  (match door, see below)

## WHY THIS BLOCK OVER THE OTHERS

The owner answered the design question on 2026-09-08 (Rust-canonical: `&mut self`
receiver, E0509 refuses the move-out, glue = user drop THEN fields). That turns
rows 1 and 2 from *design questions* into *illegal programs*, and it retires the
spec clause `intrinsic.drop.skip-moved-out-paths` that is the ONLY recorded
reason the E0509 arm was declined on 2026-08-28 ("it contradicts a written
language rule ... funding it is a DESIGN decision (PAIR), not a checker round").
The decline has been overturned by name. Nothing else in the queue has had its
blocking reason removed by an owner decision this week.

Shape preference from the prompt — "an ARM THAT EXISTS reached through a fact the
code does not carry" — is met literally: the arm is already written and in the
tree, env-gated, at src/compiler/borrow_check.cpp:14741 (`probe::on("fldmovedrop")`).
No build is needed to price it.
