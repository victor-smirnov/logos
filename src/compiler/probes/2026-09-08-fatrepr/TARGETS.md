# ROUND 2026-09-08-fatrepr — TARGET ROWS, declared before the compiler was touched

Build hash at declaration: `2c5a33ef99e94fc9 43`. HEAD `8edce4409`, tree clean.
Queue gate rc 0, 80 rows (tier1=27 tier2=7 tier3=42 tier4=4). bc_admits 91,
bc_admits_blocked 15. probe-log-lint 248 records.

## THE FOUR ROWS, BY NAME (the fat-representation block, all tier 3, all `refuses`)

    zonemut_fat_ref_struct_field_layout_abort
    fatslice_field_match_binder_invalid_mlir
    enum_variant_ctor_arg_no_unsize_coercion
    method_with_unsized_wrapper_param_not_found

## WHY THIS BLOCK

Every one is an ARM THAT EXISTS reached through a FACT THE SITE DOES NOT CARRY
— the shape the prompt says has paid every time. Located by reading, before any
edit:

  * zonemut — `register_struct` (mlir_gen_types.cpp:306) has a
    `Ptr/Ref/MutRef`-over-struct branch that sets `ft = ptr_type()`
    UNCONDITIONALLY. Four sibling branches in the same loop (TraitObject,
    Slice, DstRef, Closure) already say `ft = repr_storage_type(ref_repr_of(fv))`.
    The missing fact is `ref_repr_of(fv) == FatZoneMut`, which `layout_of`
    already computes (16) — hence the two-engine abort.
  * fatslice — `bind_struct_field` (mlir_gen_stmt.cpp:4805-4837) loads the
    field by its StructInfo LLVM type (`struct<(ptr,i64)>` for `&[T]`) into an
    alloca and records `var_elem_types_ = that pair`, so the index site loads a
    VALUE and GEPs it. VERIFIED in the emitted MLIR (%36/%39/%41 of the
    verify-fail dump), not inferred. The arm exists 500 lines away in
    `bind_name_at_slot`'s `slice_closure` branch (mlir_gen_stmt.cpp:4272): a
    fat binding is bound as a POINTER to the storage with `var_elem_types_ = ptr`.
  * enumctor — the enum variant ctor routes its args through
    `expect_type(..., CoercePos::Operand, ...)`, and `mask_for(Operand)`
    (sema_expr.cpp:14941) is `CFLAG_WIDEN_INT` alone. Six sibling positions
    carry `CFLAG_ARRAY_TO_SLICE`. The missing fact is that an enum ctor arg is
    an ARGUMENT, not an "operand of a larger construct".
  * method — site not yet located by reading; censused this round.

They are NOT one root, and the round does not claim they are: the first fires
in the LLVM struct type builder before codegen, the second in a match-binder
GEP with layout AGREEING, the third in sema's coercion mask, the fourth in
method resolution. Additivity is checked, not assumed (rule 13).
