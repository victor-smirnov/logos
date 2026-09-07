# The closure lattice

Re-baseline in one command from the repo root:

    python3 tests/lattice/closure/gen.py  /tmp/clat/progs
    bash    tests/lattice/closure/run.sh  /tmp/clat/progs /tmp/clat/out > /tmp/clat/res.txt
    python3 tests/lattice/closure/score.py /tmp/clat/res.txt
    bash    tests/lattice/closure/vg.sh   /tmp/clat/out > /tmp/clat/vg.txt   # THE SECOND HALF OF THE ORACLE

`run.sh` saturates the box (`xargs -P $(nproc)`); do not run it beside a build or a
ctest level. 228 cells take ~40 s; the valgrind sweep another ~60 s. `LOGOSC` and
`LOGOS_LIB` override the compiler and the archive directory; `LOGOS_ROOT` the tree.

`cells.json` carries, per cell: its id, its block, the axes' values (`how`, `pay`,
`go`, `shape`, `scope`, `width`), `expect_count`, `expect_value`, and `why` — the
reason that number is Rust's answer.

THE ORACLE IS A DESTRUCTOR COUNT **PLUS VALGRIND**, NOT AN EXIT CODE. Every owner
carries a distinct power-of-ten weight and a raw `*mut i64`; its destructor ADDS its
weight, so the final number is a signature naming exactly which owners ran. Every cell
also reads a value out of the closure into a variable declared OUTSIDE it and printed
AFTER it, so a cell whose captures are entirely absent cannot pass.

⚠ `score.py` ALONE IS NOT THE ANSWER — MEASURED 2026-09-07. Of the seven cells that
turned out to be memory-unsafe, **four exit 0 with the correct count and the correct
value**: `i_impl_fn_scalar`, `i_impl_fn_owner_two`, `i_impl_fn_nocap`,
`i_fat_boxed_noannot_1`. Run `vg.sh` and read it. `--error-exitcode=97` reports
definitely-lost leaks as well as invalid accesses.

A LATTICE DECAYS. Do not carry a result forward: re-run it.

## Baseline, build febd1aa6e49ae30c 43, 2026-09-07

    TOTAL 228   OK 192   WRONGCOUNT 26   REFUSED 7   RUNRC 2   WRONGVALUE 1
    valgrind-dirty among the OK: 4   (see above)

Wrong cells by ROOT — five roots, seven rows:

  * `if (body_moved_outer.count(ec->captures[i])) continue;` (sema_expr.cpp) — 14 cells
    `a_move_nocall_consume_*`, row closure_capture_body_moved_never_dropped.
  * `if (narrow_owned) continue;` (sema_expr.cpp) — 6 cells `b_narrow_consume_nocall_*`,
    row closure_narrow_capture_never_dropped.
  * the `capture_drops[i]` loop in `MLIRGenImpl::emit_closure_env` (mlir_gen_dyn.cpp),
    which the two above delegate to — 1 cell `l_boxed_fnonce_escape_consume`, row
    boxed_escaping_fnonce_capture_double_free. SAME MISSING FACT AS THE FIRST TWO,
    OPPOSITE SIGN: no glue ⇒ leak, glue ⇒ double free.
  * the missing params-scope epilogue in the closure-literal lowering (sema_expr.cpp),
    whose twin is `lower_fn`'s `if (!body_terminated) … emit_frame_drops(…)`
    (sema_decl.cpp) — 4 cells `h_bv_void_*` + `l_param_string_void`, row
    closure_byvalue_param_never_dropped.
  * `ec->escapes = true` guarded by `hk.kind() == Struct || ZonedStruct` (sema_expr.cpp)
    — 7 cells `k_impl_fn_*` / `i_impl_fn_*` / `g_returned_impl_fn`, row
    impl_fn_return_stack_env_dangles.
  * `sizeof_struct(cap_struct)` for a heap env holding fat captures (mlir_gen_dyn.cpp) —
    3 cells `i_fat_boxed_noannot_*`, row boxed_move_closure_fat_capture_env_overflow.
  * `SemaImpl::declare_var`'s name-keyed `var_order` — 1 cell `j_closure_shadowed`,
    ALREADY ROWED as shadowed_binding_never_dropped and NOT closure-specific
    (`let x: D = a; let x: D = b;` with no closure anywhere leaks identically).

Refusals of legal Rust: `e_fat_boxed_*` (row str_annot_loses_static_for_dyn_fn),
`g_closure_struct_field` (row boxed_closure_struct_field_not_callable),
`g_dyn_fn_behind_ref` (row box_dyn_fn_shared_ref_arg_refused), `g_cast_box_dyn`
(row boxed_closure_to_dyn_fn_cast_internal), `g_generic_two_insts`
(row closure_in_generic_two_insts).
