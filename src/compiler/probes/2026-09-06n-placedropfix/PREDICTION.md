# ROUND 2026-09-06n — PREDICTION, declared before the compiler was touched

Build hash at declaration: `f29b1b7c50d1a470 43`. HEAD `546cb165b`, tree clean.
Queue gate rc 0, 65 rows (tier1=24 tier2=6 tier3=32 tier4=3). bc_admits 98,
bc_admits_blocked 25. probe-log-lint 235.

## THE EDIT

ONE block in `SemaChecker::lower_place_assign` (src/compiler/sema_stmt.cpp): the
`field_old_live` walk. Three changes, one structural:

  1. KIND — the walk is entered for `la::TUPLE_INDEX` and `la::INDEX_READ`, not
     `la::FIELD_READ` alone, and a TUPLE_INDEX link normalises to the decimal
     segment `move_path_of` records. An INDEX_READ link has no static path, so it
     coarsens to the CONTAINER.
  2. ROOT — a chain bottoming out at a DEREF of a `&mut` is live, which is the
     rule the code already states in prose for the sugared spelling. A DEREF of a
     raw pointer, of a `&`, or of anything else is NOT.
  3. PARENS — receivers are unwrapped as the walk descends. `(*q).d` parses as
     FIELD_READ(PAREN_EXPR(DEREF)), so a root rule that does not unwrap sees
     nothing (the pricing round censused `pasgn.root.deref` = ZERO against
     `pasgn.root.other` = 134).

And ONE DECOUPLING the crude probe did not have: action (b) — erasing covered
paths from `moved_vars_` — does NOT run when the chain passed through an index
link. `a[i] = v` re-initialises one element, not `a`. This is the change that
answers the probe's single measured cost.

## PREDICTED CLOSED SET — 4 rows, BY NAME

    assign_tuple_elem_no_drop_old                 1  run 1
    assign_index_elem_no_drop_old                 1  run 1
    assign_field_path_not_var_rooted_no_drop_old  1  run 1
    tuple_elem_reinit_after_move_never_dropped    1  run 1

Predicted new `# TOTAL`: **61**. Predicted rows OPENED: 0 by the edit.

## PREDICTED NOT CLOSED, by name

    nested_tuple_field_assign_unimplemented  — refused EARLIER, at
        `place_write_supported`; this edit never reaches it. It stays refused,
        and with this edit landed it would no longer leak if it were opened.
    index_write_through_local_refmut_clobbers_binding        — codegen, gen_lvalue_addr
    tuple_index_write_through_local_refmut_clobbers_binding  — same

## MY OWN COUNTER-EXAMPLES — baseline read BEFORE the edit, on `f29b1b7c50d1a470 43`

Shapes the pricing round did not use. `hand/`, run by `run_hand.sh`.

| program | shape | baseline | correct |
|---|---|---|---|
| N1 | `[W;2]`, W has droppable FIELDS and NO `Drop` impl | COUNT=1010 | 1011 |
| N2 | one tuple element assigned THREE times in a `while` | COUNT=4 | 10 |
| N3 | element MOVED OUT, then assigned (double-free direction) | REFUSED `use of moved field 't.0'` | COUNT=1001 |
| N4 | `(*pa)[0]` through a RAW `*mut [D;2]` — MUST NOT DROP | COUNT=1010 | 1010 (already right) |
| N5 | `(*q).a[0]`, both axes in one place expression | COUNT=1010 | 1011 |
| N6 | mixed chain TUPLE-then-FIELD, `t.0.d` | COUNT=1010 | 1011 |
| N7 | the RHS READS the place it overwrites | COUNT=1001 | 1002 |
| N8 | heap `String` payload — oracle is VALGRIND, not a counter | COUNT=16 | 16, no leak |
| N9 | move out, re-init, use again — LEGAL RUST, must COMPILE | REFUSED `use of moved field 't.0'` | COUNT=1001 |
| N10 | whole array moved, then `a[0] = …` — MUST STAY REFUSED | `use of moved variable 'a'` | same |
| N11 | whole tuple moved, then `t.0 = …` — MUST STAY REFUSED | `use of moved variable 't'` | same |
| N12 | `(*q)[0]` through a SHARED `&` — MUST STAY REFUSED | `assignment through a shared reference` | same |

⚠ N3 and N9 are an OVER-REFUSAL the pricing round recorded as unrowed and left
for an owner: `let x = t.0; t.0 = D{…};` is legal Rust and is refused today,
while the struct-field twin `let x = w.d; w.d = D{…};` compiles. It is refused
at `lower_mut_place`, which reads the LHS place — and the (b) erase runs BEFORE
that read, which is exactly why the FIELD spelling survives and the TUPLE one
does not. So this edit is PREDICTED TO CLOSE IT TOO, as a consequence of (b)
reaching the tuple door, not as a separate change. If it does, it needs a
fixture; it has no row to delete.

## THE FOUR MUST-NOTs (rule 5 — a cost of 0 is not a safety claim)

N4 must stay 1010, N10/N11/N12 must stay refused with the SAME sentence. An
over-drop here is a DOUBLE FREE, which is the expensive direction.
