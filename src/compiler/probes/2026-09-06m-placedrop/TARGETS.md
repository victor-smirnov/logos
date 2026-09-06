# TARGET ROWS — round 2026-09-06m (pricing round; I price, I do not fix)

Named BY ID before the compiler was touched. Build hash at selection:
`f29b1b7c50d1a470 43`. Queue gate rc 0, 65 rows (tier1=24 tier2=6 tier3=32 tier4=3).

## THE BLOCK — three tier-1 `run 1` rows, ONE predicate

    assign_index_elem_no_drop_old                 1  run 1
    assign_tuple_elem_no_drop_old                 1  run 1
    assign_field_path_not_var_rooted_no_drop_old  1  run 1

All three are `SemaChecker::lower_place_assign` (src/compiler/sema_stmt.cpp).
ONE local, `bool field_old_live`, decides whether `stmt_deref_write` is emitted
with `drop_old = true`. It is computed inside

    if (pc == la::FIELD_READ) { …peel FIELD_READ links…
        if (!cur.is_null() && code_of(cur) == la::VAR_REF) { …field_old_live = … } }

so it is false for every place that is not a FIELD_READ chain bottoming out at a
bare variable. The three rows are the three ways out of that box, on TWO AXES:

  * KIND axis — the top link is not FIELD_READ:
      `t.0 = new`  (la::TUPLE_INDEX) -> assign_tuple_elem_no_drop_old
      `a[0] = new` (la::INDEX_READ)  -> assign_index_elem_no_drop_old
  * ROOT axis — the chain is FIELD_READ but bottoms out at a DEREF, not a VAR_REF:
      `(*q).d = new`, q: &mut W1     -> assign_field_path_not_var_rooted_no_drop_old

## WHY THIS BLOCK AND NOT THE OTHERS

1. THE ARM EXISTS AND IS CORRECT NEXT DOOR. `stmt_deref_write(..., drop_old_place)`
   already drops the old value; the named-field-over-a-var spelling (`w.d = new`)
   reads 1001 and the SUGARED spelling of the third row (`q.d = new`) is ALSO
   correct — the two spellings of one assignment disagree. That is precisely the
   "an arm that exists, reached through a fact the code does not carry" shape the
   round protocol says has paid every time.
2. THE ORACLE IS THE STRONGEST IN THE QUEUE. All three are destructor counts
   through a raw `*mut i64` — a leak and a double free are BOTH visible in the
   digit, not merely in an exit code.
3. THE GROUPING IS TESTABLE AND HAS TWO AXES, so Rule 13 (a per-site measurement
   is not additive; the increment can be negative) is a live question here rather
   than a formality: KIND alone should move 2 rows, ROOT alone 1, and whether
   the pair sums is the thing to measure, not to assume.

## THE BLOCK I DECLINED, AND WHY (for the next round)

    index_write_through_local_refmut_clobbers_binding        1 run 139
    tuple_index_write_through_local_refmut_clobbers_binding  1 run 1

Also one root, also tier 1, and its grouping is ALREADY PROVEN — the 2026-09-06
triage round measured that `gen_expr_kind(EAddrOfTempView)` routes both
`IndexRead` and `is_mut && TupleIndex` into `MLIRGenImpl::gen_lvalue_addr`
`case ec::Code::VarRef`, which consults `var_local_ptrs_` (RAW `*mut`/`*const`
locals only) and otherwise returns `get_subscript_ptr(vn)` — correct for a ref
PARAMETER, wrong for a reference-typed LOCAL. ONE MISSING LOAD, with six named
controls. It is declined THIS round for exactly that reason: it is already
priced by reading, there is no ambiguity left to buy, and a pricing round should
spend its build on the block whose axes are still open. It should be the next
round's FIX, not this round's price.

⚠ Note the two blocks TOUCH: the lattice cell `f_assign_index_through_refmut`
loses the store (declined block) AND leaks the RHS (this block). Neither block's
fix closes that cell alone.

## ARMS TO PRICE (predictions BY NAME, made before the run)

    patuple  TUPLE_INDEX admitted into the walk, static ".N" segment, VAR_REF root
             -> predicts assign_tuple_elem_no_drop_old ONLY
    paindex  INDEX_READ admitted, root-conservative (no static subscript path)
             -> predicts assign_index_elem_no_drop_old ONLY
    paroot   a FIELD_READ chain rooted at a DEREF of a `&mut` is live
             -> predicts assign_field_path_not_var_rooted_no_drop_old ONLY
    paall    all three together — the ADDITIVITY check (Rule 13)
             -> predicts all three, if the axes are independent
