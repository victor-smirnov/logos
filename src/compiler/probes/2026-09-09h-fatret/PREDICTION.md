# ROUND 2026-09-09h-fatret — PREDICTION, DECLARED BEFORE ANY COMPILER EDIT

Base build `30683596ac68fda8 43`, HEAD `7a137f7ae`. Soundness-queue gate rc 0,
78 rows (t1=22 t2=7 t3=43 t4=6), `# TOTAL` 78. probe-log-lint 250 records.
bc_admits 92 · bc_admits_blocked 8.

## THE CLASS, ENUMERATED BY THE PROPERTY (not by spelling)

`RefReprKind` is a CLOSED enumeration (mlir_gen_impl.hpp:1584). The property is:

    repr_storage_layout(k).size == 16   AND   repr_return_type(k) == 8B ptr

i.e. a reference repr whose STORAGE is a 16-byte fat pair but whose RETURN ABI
is a pointer — necessarily a pointer to `create_entry_alloca`'s slot in the
frame that is about to die.

Members with 16B fat storage — FIVE, by `repr_storage_layout`
(mlir_gen_types.cpp:940ff):

    FatSlice · FatDyn · FatZoneMut · FatClosure · FatCustomDst

`repr_return_type` (mlir_gen_types.cpp:953) splits them into two rows:

    by VALUE (16B):  FatDyn, FatSlice, FatZoneMut          -- 3, correct
    by POINTER (8B): FatClosure, FatCustomDst              -- 2, THE CLASS

and its own comment says why the second row is what it is: "their fat storage
is not return-materialized — MATCHES THE PRE-REFREPR BEHAVIOR where these fell
through to logos_to_mlir = ptr". That is a preservation-of-behaviour decision,
not a correctness one.

## THE CLASS IS EXACTLY TWO MEMBERS, AND BOTH ARE MEASURED TO REPRODUCE

    FatClosure     N2 rc 139 / 5 vg · N3 rc 139 / 2 vg · N4 rc 139 / 2 vg
                   N1 rc 0 with the RIGHT answer and 3 vg records
                   ROW impl_fn_return_stack_env_dangles rc 139 / 5 vg
    FatCustomDst   D1 rc 139 / 2 vg   -- NO ROW ANYWHERE. NEW DEFECT.

The three by-value members are the control: Z3 (FatZoneMut built by the callee
with `zone_mut_ref` and returned) is clean, and its IR returns `{ptr,i64}` by
value. Same shape, other row of the table, no defect.

Corroboration that the tree already knows this dangle exists: `ref_repr_of`'s
own comment on the `#[self_describing]` DstRef — "physically THIN ... THIS IS
WHAT LETS A `&Foo` TO IT BE RETURNED SAFELY (no stack-local metadata pair to
dangle)". Self-describing is the WORKAROUND for the defect D1 measures.

## PREDICTED CLOSED SET — A NUMBER AND A LIST

I predict the structural change (both members into the by-value row, plus the
producer at `gen_return` and the consumer at the call-site spill) closes:

    1 queue row:  impl_fn_return_stack_env_dangles           (t1, run 139)
    0 other queue rows.

and lands D1 as a NEW pass fixture rather than a new row, because the same
one-table change repairs it.

⚠ I predict `impl_fn_return_stack_env_dangles` needs TWO DOORS IN SERIES and
that this change alone opens only the OUTER one. Its program captures `k`, so
even with the pair returned by value the ENV is still `create_entry_alloca` in
mk's dead frame (`heap_env = escapes && !captures.empty()`, and `escapes` is
false for a `-> impl Fn` return). N1 and D1 — which have NO env — should go
clean on the outer door alone; N2/N3/N4 and the row should NOT, until the
INNER door (return position ⇒ escapes) is also opened. If N2 goes clean on the
outer door alone, this prediction is wrong and I will say so.

## PREDICTED **NOT** CLOSED — BY NAME, THE OTHER SIX CLOSURE-CAPTURE ROWS

    boxed_move_closure_fat_capture_env_overflow   env FIELD layout, not the return ABI
    closure_owned_dyn_capture                     owning Box<dyn> capture read as a borrow
    boxed_escaping_fnonce_capture_double_free     FnOnce consume site
    closure_fnonce_cond_call_untaken_leak         branch-merge move set
    closure_fnonce_call_in_loop_multi_free        loop-aware move set
    generic_drop_body_calling_closure_param_...   mono / drop glue

None of the six returns a closure from a function, so none can touch this table.

## DIFF BUDGET, DECLARED BEFORE IMPLEMENTING

<= 60 lines net across the compiler (`git diff --numstat`), excluding fixtures,
prose and this record. Overrun > 2x is a design smell and I will say so.

## THE OTHER DIRECTION — WHAT WOULD CONDEMN THIS

Returning a 16B pair by value where the caller expected a pointer is a
by-value -> by-pointer transition at EVERY closure/DST-ref consumer. The
failure mode to hunt is not a SIGSEGV, it is a compile refusal
("'llvm.getelementptr' op operand #0 must be LLVM pointer type") on a legal
green fixture — exactly what `capfat` did last round to
`logos_04_advanced_features_pass_closure_call_moves_string`. `run_oracle.py`
over all ~6570 run fixtures is the column that sees it; ceiling/cost/cfail
cannot. And a leak has NO exit code: valgrind on hand programs is the only
oracle for the inner door, because heap-allocating an env that nobody frees
trades a dangle for a leak (measured last round: `capretesc` leaked 16 bytes
at R3 while every one of its five columns read clean).
