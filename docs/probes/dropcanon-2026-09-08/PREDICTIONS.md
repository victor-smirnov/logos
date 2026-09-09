# PREDICTIONS — probe `dropbstruct`, written BEFORE the armed binary ran
Arm: `src/compiler/mlir_gen_stmt.cpp` struct branch of `gen_drop_value` —
`if (!top_level) return;` becomes `if (!top_level && !probe::on("dropbstruct")) return;`
i.e. design B at every depth for STRUCT values. The enum branch and the
scope-exit enum guard are NOT touched by this arm.

CLOSES (predicted by name):
  · soundness_queue replace_site_skips_field_drop_glue     rc 11 -> 0
  · its own s_field and s_nested shapes                    Vec__drop 0 -> 1
  · tests/logos/pass/drop_glue_three_levels                stdout "b3" -> "b3 a7 "
      => that fixture goes RED. Its `.expected` pins the DEFECT and the last
         round measured it blocks EVERY answer to the owner's question.

DOES NOT MOVE (predicted by name):
  · drop_body_moving_field_double_drops_local              stays 2 (top-level local:
      the scope-exit emitter already recursed; this arm only changes NESTED)
  · drop_body_conditional_move_double_drops_both_paths     stays 14
  · enum_user_drop_skips_payload_glue                      stays 0   (enum arm)
  · letstruct_destructure_skips_user_drop                  stays 100 (a match/let
      pattern door, not a depth question)
  · tests/imported/pass/drop/drop-trait-enum-b154          stays rc 0 (enum)

GETS WORSE (and is NOT a cost under the owner's decision, because E0509 makes
the program illegal — measured: `fldmovedrop` refuses it):
  · a `P2`-as-a-FIELD whose drop body moves its own field out   1 -> 2
