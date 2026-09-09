# ROUND 2026-09-09g-closureenv — PREDICTED BY NAME, BEFORE THE ARMED BINARY EXISTED

Base `30683596ac68fda8 43` (HEAD `3478f9298`), base binary preserved as
`build/bin/logosc.base09g` (INSIDE build/bin — a copy outside it cannot resolve
`logos.std.prelude` and answers CCFAIL to everything, a control that changes
nothing; the copy was proven live by reproducing F1's 2 invalid accesses and
F3's wrong value).

## BASE COLUMN, MEASURED FIRST (rc / valgrind ERROR SUMMARY)

    F1  one `str` capture, boxed              rc 0    2   DEFECT, silent: right value, heap overflow
    F3  three `str` captures, boxed           rc 1    2   DEFECT, WRONG VALUE
    F4  two `&[i64]` slice captures, boxed    REFUSED     "coercion to `dyn Fn` requires `|| -> i64: 'static`
                                                           — lifetime `'%1` may not live long enough"
                                                           READ, and CORRECT: Rust refuses this too. Not a defect.
    F5  two SCALAR captures, boxed            rc 0    0   control ✓
    F7  two `str` captures, NOT boxed         rc 0    0   control ✓ — the STACK env is not corrupt
    F8  two CLOSURE captures, boxed           rc 0    0   control ✓ — Kind::Closure is already right
    F9  a `&dyn Tr` capture                   rc 0    0   control ✓ — TraitObject is already right
    R2  NON-capturing closure ret `impl Fn`   rc 0    2   DEFECT: "Use of uninitialised value" IN MAIN
    R3  closure at a bare `Fn` ARGUMENT       rc 0    0   control ✓ (the iterator-adapter shape)
    R5  the same closure through Box<dyn Fn>  rc 0    0   control ✓
    R6  a `str` capture ret as `impl Fn`      rc 139  6   the INTERACTION cell

⚠ F8 AND F9 ALREADY NARROW THE CLASS BEFORE THE PROBE RUNS. `capfat`'s arm names
four fat kinds; two of them are measured CORRECT at the base. So `Slice` (incl.
`str`) is the only live member, and a ceiling on the other three would be a
ceiling off an empty population (rule 4).

⚠ R2 IS THE SERIES DOOR (rule 2). Its closure has NO captures, so codegen takes
the `captures.empty()` NULL-env path and `escapes` cannot reach it — yet main
reads uninitialised memory. What dangles is the `{fn, env}` PAIR: `gen_closure`
returns `create_entry_alloca(ctype)`, an alloca in `mk`'s frame, and an
`impl Fn` return hands back that POINTER. `Box<dyn Fn>` (R5) is clean because
`Box::new` copies the pair to the heap.

## PREDICTED, BY NAME

### capfat (mlir_gen_dyn.cpp, the closure capture-struct field walk)
    F1                                        2 -> 0 errors, rc 0
    F3                                        rc 1 -> rc 0, 2 -> 0 errors
    ROW boxed_move_closure_fat_capture_env_overflow   rc 134 -> rc 0, 2 -> 0
    tests/logos/pass/bc_objlt_str_literal      2 -> 0 errors, rc 0 unchanged
    F5 F7 F8 F9 R3 R5                          UNCHANGED (all already clean)
    F4                                         UNCHANGED refusal, SAME sentence
    R2                                         UNCHANGED 2 errors — no captures
    R6                                         UNCHANGED rc 139 — the env is a STACK
                                               env here, so a wider slot changes
                                               nothing; the pair still dangles
    the other six queue rows                   UNCHANGED
    cost                                       0 / cfail 0 / stdlib ok
    ceiling                                    0, AND THAT IS NOT A REFUTATION —
                                               the bc ledger prices borrow-check
                                               rows and holds no closure-env program

### capretesc (sema_expr.cpp, the `ec->escapes` guard)
    ROW impl_fn_return_stack_env_dangles       rc 139 -> STILL rc 139.
        I predict this probe DOES NOT CLOSE ITS OWN ROW, on R2's evidence: the
        row has two doors in series and this arm opens only the inner one. If it
        DOES close, R2 is a second, separate defect and the row's header is
        wrong about "the env" being the whole story.
    R6                                         rc 139 -> STILL rc 139 (same series)
    R3                                         rc 0 -> rc 0, but now with a heap
                                               env: predict a LEAK (definitely
                                               lost > 0) or a double free at the
                                               iterator-adapter shape
    F1 F3 F5 F7 F8 F9 R5                       UNCHANGED
    cost                                       NON-ZERO. This is the crude arm:
                                               it escapes every closure whose hint
                                               peels to a callable, including every
                                               borrowed adapter closure.

### BOTH ARMED
    R6                                         still rc 139 if the pair door is real;
                                               rc 0 only if it is not.

## THE GROUPING CLAIM
Two probes, two rows, ZERO overlap predicted: no program in the queue moves
under both. If that holds, the closure-capture block is at least FOUR roots
(fat env layout · escape predicate · the returned-pair · the FnOnce consume
rule), not one, and `bug_boxed_closure_runtime_garbage` names only one of them.
