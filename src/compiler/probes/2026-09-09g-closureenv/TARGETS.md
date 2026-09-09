# ROUND 2026-09-09g-closureenv — TARGET ROWS, NAMED BEFORE THE COMPILER WAS TOUCHED

Base build `30683596ac68fda8 43`, HEAD `3478f9298`. Soundness-queue gate rc 0,
78 rows (t1=22 t2=7 t3=43 t4=6), `# TOTAL` says 78. probe-log-lint 250 records.

## THE BLOCK, DERIVED FROM THE LEDGER (not inherited)

Rows whose subject is a CLOSURE's captured environment, tier 1 (all `run`):

    boxed_move_closure_fat_capture_env_overflow   run 134
    closure_owned_dyn_capture                     run 1
    boxed_escaping_fnonce_capture_double_free     run 2
    closure_fnonce_cond_call_untaken_leak         run 1
    closure_fnonce_call_in_loop_multi_free        run 1
    impl_fn_return_stack_env_dangles              run 139
    generic_drop_body_calling_closure_param_corrupts_callers_closure  run 139   <-- NOT IN THE PROMPT'S LIST

The prompt named six; the ledger has SEVEN. The seventh was minted 2026-09-08.

## TARGETS THIS ROUND, BY ID

    1. boxed_move_closure_fat_capture_env_overflow   probe `capfat`
    2. impl_fn_return_stack_env_dangles              probe `capretesc`

## WHY THIS PAIR AND NOT THE OTHERS

Both are the shape the prompt says has paid every time — AN ARM THAT ALREADY
EXISTS, reached through a fact the code does not carry — and for #1 the arm is
not merely analogous, it is the SAME ARM one round old:

  * `2026-09-08-fatrepr` landed `Slice`/`DstRef`/`Closure`/`TraitObject` fields
    as INLINE 16-byte fat pairs in `register_struct` (mlir_gen_types.cpp:367ff,
    `repr_storage_type(ref_repr_of(fv))`). The CLOSURE CAPTURE STRUCT is built
    by a SECOND, private field-type walk (mlir_gen_dyn.cpp:1939-1959) that ends
    `else ft = logos_to_mlir(ct)` — the VALUE repr, 8 bytes. That round never
    censused this site. It is the un-repaired twin of the site it repaired.
  * `impl_fn_return_stack_env_dangles`: `ec->escapes = true` exists and is
    correct next door; its guard tests the SHAPE OF THE HINT TYPE (Struct
    wrapper) as a proxy for "outlives the frame". The hint IS set from the fn
    return type at sema_stmt.cpp:3493, so the fact reaches the site — the
    predicate declines to read it because `impl Fn` is not a Struct.

The other five are declined for stated reasons, not skipped:

  * `closure_owned_dyn_capture` — its arm `clowndyn` is INSTALLED IN TODAY'S
    BINARY and was declined 2026-09-04. RE-MEASURED TODAY (build
    `30683596ac68fda8 43`): still 1 -> 134. The declination HOLDS; nothing to
    buy without a different mechanism.
  * `boxed_escaping_fnonce_capture_double_free` — its header records a MEASURED
    complete answer (free at the consuming call) and a MEASURED reason the half
    answer was refused (double free traded for a leak). That is a landing
    decision, not a pricing question.
  * `closure_fnonce_cond_call_untaken_leak`, `closure_fnonce_call_in_loop_multi_free`
    — both are SEMA set/flow defects with named arms (`elaborate_cond_moves`,
    borrow_check's move record). Neither is a codegen question, so neither
    shares a build with #1; and the loop row's correct answer is a REFUSAL,
    i.e. its `observed` changes kind. Separate round.
  * `generic_drop_body_calling_closure_param_corrupts_callers_closure` — one day
    old, five controls already recorded, and mono/drop-glue, not the env layout.

## THE GROUPING CLAIM, STATED SO IT CAN BE REFUTED

I predict these two rows are TWO ROOTS and that NEITHER probe moves the other's
row, nor any of the remaining five. Specifically: `capfat` changes only the env
FIELD LAYOUT of a closure that is already escaping; `capretesc` changes only
WHETHER a closure escapes. A row that needs both would be a fat capture in a
returned `impl Fn` — no row in the queue is that shape.
