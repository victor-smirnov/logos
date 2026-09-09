# PREDICTION FOR THE CODEGEN HALF (design B, struct AND enum together)

Written 2026-09-08, AFTER the source edit and BEFORE the armed binary existed —
no measurement of the combined arm has been taken at the time of writing.

THE CHANGE. `gen_drop_value`'s `top_level` parameter is retired. Drop glue is
"run `Drop::drop`, then drop the fields", at EVERY depth, in both the struct and
the enum branch. The scope-exit arm's enum case loses its `&& drop_fn.empty()`
guard and instead passes `run_user_drop = drop_fn.empty()`, because step 1 of
that arm has already emitted the user drop.

⚠ THE TWO HALVES WERE PRICED ON TWO DIFFERENT BINARIES AND ARE MEASURED HERE
TOGETHER FOR THE FIRST TIME. Rule 13: a per-site measurement is not additive and
the increment can be negative. 1 + 1 closed rows is a hypothesis, not a sum.

## QUEUE ROWS PREDICTED TO CLOSE — 2, BY NAME

    replace_site_skips_field_drop_glue        rc 11 -> rc 0
    enum_user_drop_skips_payload_glue         rc  1 -> rc 0

## QUEUE ROWS PREDICTED NOT TO MOVE — 6, BY NAME

    drop_body_moving_field_double_drops_local              stays 2   (E0509 class)
    drop_body_conditional_move_double_drops_both_paths     stays 14  (E0509 class)
    letstruct_destructure_skips_user_drop                  stays 100 (E0509 class)
    generic_drop_body_calling_closure_param_corrupts_...   stays 139 (unrelated)
    struct_fields_dropped_reverse_order                    stays 1   (field ORDER)
    tuple_elems_dropped_reverse_order                      stays 1   (field ORDER)

## FIXTURE COST PREDICTED — 2, BY NAME

    tests/logos/pass/drop_glue_three_levels
        stdout `b3` -> `b3 a7 `, exit 42 unchanged. This is what the fixture's
        OWN comment already documents ("c drops via glue -> B::drop(b3) ->
        A::drop(a7)"): today `A::drop` runs ZERO times and the fixture pins that.
        Re-pinned here; not a weakening, the subject runs MORE of itself.

    tests/imported/pass/drop/drop-trait-enum-b154
        PREDICTED RED, exit 2. Its `Foo::drop` is by-value `self: Foo` and its
        arm `Foo::Nested(s, c)` binds the `SendOnDrop` payload BY VALUE, so `s`
        is dropped at the drop body's scope end AND the payload is now dropped
        by the call site's recursion: 201 where the fixture wants 101.
        ⚠ That double drop is not design B being wrong — it is the port being
        illegal: `Foo::Nested(s, c)` is E0509, and upstream's own arm is
        `Foo::NestedVariant(_, _, ref f)`. The repair is to restore upstream's
        `_`, which is MORE faithful, not less. The receiver deviation
        (`self: Foo` where upstream has `&mut self`) is NOT repaired here and
        stays with the by-value-receiver round.

## WHAT WOULD FALSIFY THE DESIGN RATHER THAN THE COUNT

Any row moving in the OPENING direction, any pass fixture whose destructor
count goes DOWN, or a runtime-oracle triple that changes on a program with no
user `Drop` at all.
