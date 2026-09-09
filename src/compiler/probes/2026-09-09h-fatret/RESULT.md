# ROUND 2026-09-09h-fatret — WHAT THE NUMBERS SAY

Base `30683596ac68fda8 43`, HEAD `7a137f7ae`. Base tree preserved as a git
worktree at `/tmp/logos-base09h` with its own configure and build, so every
"before" number below is a real binary and not a recollection.

## 0. CENSUS (STEP 1, RUN, NOT INHERITED)

    soundness queue gate  rc 0 — 78 rows (t1=22 t2=7 t3=43 t4=6), '# TOTAL' 78
    bc_admits 92 · bc_admits_blocked 8
    probe-log-lint: 250 records, every site symbol resolves
    build_hash.py: 30683596ac68fda8 43   (READ)

The handed-down report reproduced digit for digit. Two corrections to it:

  * It says HEAD is `3478f9298`. It is `7a137f7ae` — the pricing round's own
    commit landed after that report's text was written. The report warns it is
    a paraphrase; this is where it is stale.
  * Its closure-capture block of SEVEN rows is correct and I re-derived it from
    the ledger independently. The prompt's list of six is the one that is short.

## 1. THE CLASS, AND WHY IT IS NOT THE ONE ANYBODY WAS LOOKING FOR

The pricing round's recommendation A was "the `impl Fn` returned pair — the
outer door", located at `gen_closure`'s `create_entry_alloca`. That is where
the SYMPTOM is. The DECISION is one table:

    mlir_gen_types.cpp  repr_return_type(RefReprKind)

    by VALUE (16B):  FatDyn, FatSlice, FatZoneMut
    by POINTER (8B): FatClosure, FatCustomDst, ThinPtr, RelOffset

with a comment that says exactly why the second row holds what it holds:
"their fat storage is not return-materialized — MATCHES THE PRE-REFREPR
BEHAVIOR where these fell through to logos_to_mlir = ptr". A
preservation-of-behaviour decision, never a correctness one.

The class is stated as a PROPERTY over a CLOSED enumeration, not a grep:

    repr_storage_layout(k).size == 16   AND   repr_return_type(k) == 8B ptr

`RefReprKind` has seven members. Five have 16-byte fat storage. Three of those
five were already in the by-value row. **The class is exactly the other two,
and I measured both:**

    FatClosure     N2 rc 139 / 5 vg · N3 rc 139 / 2 vg · N4 rc 139 / 2 vg
                   N1 rc 0 with the RIGHT answer and 3 vg records
    FatCustomDst   D1 rc 139 / 2 vg  — NO ROW IN ANY LEDGER. NEW DEFECT.

and the control that says the property is the right one: Z3, a FatZoneMut pair
BUILT BY THE CALLEE (`zone_mut_ref`) and returned — the same shape, the other
row of the table — is clean, and its IR returns `{ptr,i64}` by value.

The IR is the whole argument, four lines of it, before any edit:

    define noundef ptr @"R2$mk__f__void"() {
      %1 = alloca { ptr, ptr }, i64 1, align 8
      ...
      ret ptr %1                      <- a pointer INTO THE FRAME BEING POPPED
    }

## 2. THE FIX IS ONE TABLE ROW AND ITS CONSUMER

    repr_return_type: FatClosure and FatCustomDst move to the by-value row,
      and the two rows are re-stated as one rule — 16B fat is returned by
      value, thin is returned as its pointer — so no future repr can land in
      the wrong half by being forgotten in a case list.

    spill_slice_call_result: the call-site half. Its predicate was a list of
      kinds (`Slice || FatZoneMut`); it is now the RETURN ABI itself,
      `repr_return_type(rk) == repr_storage_type(rk)`. The two halves are one
      mechanism and used to be able to drift apart — measured, they HAD:
      FatZoneMut was in the spill's list and in no branch of the older
      `llvm_fn_ret_type`.

Half a mechanism is not one (rule 2), and this round proved it the expensive
way: with ONLY the return-type half landed, six programs stopped compiling with
`'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but got
'!llvm.struct<(ptr, ptr)>'` — the identical failure `capfat` produced last
round from the identical mistake, one file over.

### AND IT WAS THREE SITES, NOT TWO — THE THIRD FOUND BY `run_oracle` ALONE

THREE functions answer "what LLVM type does this return position have":

    make_fn_type            the fn definition        reads repr_return_type
    fn_call_ret_llvm_type   the call site            reads repr_return_type
    llvm_fn_ret_type        A CLOSURE'S OWN RETURN   hand-rolled its own table

With the descriptor moved and the third site left alone, a CLOSURE RETURNING A
CLOSURE was DEFINED `-> ptr` and CALLED `-> {ptr,ptr}` — the caller read two
words out of a one-word return. Three green imported pass fixtures went
`runrc 0 -> -11`:

    logos_02_semantic_core_pass_nested-closure-call
    logos_02_semantic_core_pass_nested-closure-call-b141
    logos_02_semantic_core_pass_closure-returning-closure-b161

**Every compile-time column called the change free.** The compile SUCCEEDS; the
program crashes. `run_oracle.py` — 6572 fixtures compiled, linked and RUN — is
the only column in this tree that can see that, and the prediction file named
this exact direction as the thing that would condemn the round. It nearly did.
The third site now reads the same descriptor and all three are pinned in
agreement by `fatret_closure_returning_closure`, natively, because the three
fixtures that caught it are imported tier and `L4 bc` does not run them.

DIFF BUDGET, declared before implementing: <= 60 lines net across the compiler.
ACTUAL, `git diff --numstat` on the compiler, code lines only: well inside it;
the three sites are one case-list move, one predicate, and one delegation.

## 3. THE CLOSED SET, DIFFED BOTH WAYS

CLOSED, 1 queue row:

    generic_drop_body_calling_closure_param_corrupts_callers_closure  (t1, run 139)
        base 7a137f7ae   rc 139, 2 valgrind records, no stdout at all
        now              rc 0, 0 valgrind records, and the firing COUNT exact:
                           arm: before call, firings (want 0): 0
                           arm: after call,  firings (want 1): 1
                           control: after call, firings (want 1): 1

⚠ **I PREDICTED THIS ROW WOULD NOT CLOSE, BY NAME, AND I WAS WRONG.** The
prediction file lists it among the six that "cannot touch this table" because
none of them returns a closure from a function. It does: `a_out<F>(a) -> F`
returns the closure by its generic type parameter. I read the row's header and
not its code.

⚠ **AND THE ROW'S RECORDED ROOT IS REFUTED BY ITS OWN CLOSING.** The header
says the discriminator is "the presence of a CALL to `F` inside the drop body",
backed by a one-token control and five further controls. Nothing in the drop
mechanism was touched. The one-token control worked because deleting `g();`
changed the frame layout the dangling pointer happened to land in. A one-token
control can separate two programs without naming their cause — it localises,
it does not explain.

NOT CLOSED, and the number for each:

    impl_fn_return_stack_env_dangles   rc 139/5  ->  rc 0/2 vg. RE-ARMED, see §4.
    boxed_move_closure_fat_capture_env_overflow   134/2 -> 134/2   unchanged
    closure_owned_dyn_capture                       1/1 ->   1/1   unchanged
    boxed_escaping_fnonce_capture_double_free       2/0 ->   2/0   unchanged
    closure_fnonce_cond_call_untaken_leak           1/0 ->   1/0   unchanged
    closure_fnonce_call_in_loop_multi_free          1/0 ->   1/0   unchanged

The other direction: the two THIN-row twins are unchanged in both directions —
`fatret_fnptr_thin_return` and `fatret_customdst_selfdescribing_thin` read
rc 0 / 0 vg on the base binary and rc 0 / 0 vg now. The repair went to the fat
row of the table and did not widen the thin one.

## 4. THE ROW WHOSE INSTRUMENT MY OWN LANDING RETIRED

`impl_fn_return_stack_env_dangles` was recorded `run 139`. After the pair fix
its program exits 0 WITH THE RIGHT ANSWER and two valgrind
"Conditional jump ... uninitialised value(s)" records in `main`. Its defect did
not move: the ENV is still `create_entry_alloca` in the dead frame. The exit
code stopped being able to see it.

This is the recorded pattern `feedback_stronger_rule_retires_the_instrument`,
met from inside a single commit, and the response it prescribes is a REPLACEMENT
CARRIER, which is what the row now holds. Its program is PART 1 (the original
spelling, kept so the evidence still runs) plus PART 2, two closures built by
two different functions and alive at once — their envs are allocas at the same
frame depth, so the second clobbers the first and `f() + g()` is not 43.

    re-armed carrier   rc 1, 4 valgrind records, deterministic over 5 runs
    observed           run 139  ->  run 1

## 5. AND THE ROW'S NAMED ROOT IS REFUTED AT THE INTERACTION CELL

The old header names `ec->escapes = true` in `SemaChecker`'s closure-literal
lowering as this row's root, and last round's `capretesc` widened exactly that
guard and moved nothing at 170 arrivals. Last round could not tell whether that
was because the arm is wrong or because the PAIR door upstream masked it, and
said so: "on today's evidence `escapes` may not be part of this row at all".

**The interaction cell is now measured.** With the pair fix landed, the same
crude escape arm re-installed and re-measured:

    program   pair fix only        pair fix + crude escape arm
    ROW       rc 0 / 2 vg          rc 0 / 2 vg
    N2        rc 1 / 2 vg          rc 1 / 2 vg
    N4        rc 1 / 1 vg          rc 1 / 1 vg
    R3        rc 0 / 0 vg          rc 0 / 1 vg, DEFINITELY LOST: 16 BYTES

`escapes` is not this row's root, and opening it is not free. And the hint the
guard reads does not reach a `-> impl Fn` return at all — H1, a returned
`|x| { return x + 1i64; }`, infers `void`, which is the same hint failing at
its OTHER consumer, at zero build cost.

So the residual work is THREE doors in series, and the third has no
implementation anywhere: the return-position fact must reach the escape
decision; `gen_closure` must take the heap-env path; and something must FREE
that heap env, which nothing does today for an unboxed closure — that is what
R3's 16 lost bytes are. Doors 1 and 2 without door 3 trade a dangle for a leak,
and a leak has no exit code.

## 6. DECLINED, BY NAME, WITH THE NUMBER

    closure_owned_dyn_capture — `clowndyn` is installed in the binary and
        re-measured 1 -> 134 twice now (2026-09-04, 2026-09-09g). Unchanged by
        this round: 1/1 before and after. Declination holds; do not re-derive it.
    boxed_move_closure_fat_capture_env_overflow — 134/2 before and after. Its
        defect is the env FIELD WIDTH, not the return ABI; measured last round
        to need three things, of which the unannotated `let s = "abc"` capture
        kind is still an open QUESTION, not an open answer.
    boxed_escaping_fnonce_capture_double_free (2/0), closure_fnonce_cond_call_
        untaken_leak (1/0), closure_fnonce_call_in_loop_multi_free (1/0) — all
        three produce ZERO valgrind records, all three unchanged, and the loop
        row's correct answer is a REFUSAL, so closing it changes its `observed`
        KIND. That is a corpus decision with an owner, stated before a round and
        not inside one.

## 7. WHAT CONTRADICTS A RECORDED CLAIM

  1. `generic_drop_body_...`'s header names the wrong root (§3), and its five
     recorded controls are all consistent with the right one.
  2. `impl_fn_return_stack_env_dangles`'s header names the wrong root (§5),
     now measured at the interaction cell rather than inferred.
  3. The load-profile table says "Compiler builds — YES, several at once, in
     SEPARATE build dirs". A fresh from-scratch build at `-j32` FAILED on
     `objcopy: the input file '/tmp/logos_emit_logos-lang/logos-lang.imp.raw'
     is empty` — three targets rebuilding `liblogos-lang.a` concurrently share
     a FIXED `/tmp/logos_emit_<module>` scratch directory. It is an INTRA-build
     race, not an inter-build one, and it is silent on a warm tree because
     nothing rebuilds. A plain retry was green. Pre-existing; reported, not
     touched.

## 8. EVERY ORACLE, WITH ITS rc

    soundness queue gate            rc 0  — 77 rows (t1=21 t2=7 t3=43 t4=6),
                                            '# TOTAL' 77, re-derived by direct
                                            listing; 77 programs on the shelf
    L1 (test-levels.sh, from build/) rc 0 — 786/786, 12 684 generated cases,
                                            150 gates
    gate-run.sh -L bc               rc 0  — 2710 passed / 0 failed / 2 other,
                                            2712 recorded; store: build 961
    test-levels.sh L4 bc            rc 0  — store: build 961, 6559 recorded,
                                            0 failed
    scripts/stdlib-cost.sh          ok    — all four layers compile
    scripts/run_oracle.py           6572 base rows vs 6578 after, DIFFED BOTH
                                    WAYS: 0 rows lost, 6 gained (the six new
                                    fixtures), 1 changed — `cast-region-to-uint`,
                                    which prints a stack address and is
                                    subtracted by name. **0 DAMAGED.**
    scripts/fail_text_oracle.py     1464 rows both sides. rc moved on 0 rows and
                                    `.expected` match moved on 0 rows. 626 stderr
                                    SHAs differ, which is the recorded
                                    cross-configure self-invalidation and not a
                                    text change: 60 of those 626 were re-run
                                    byte-for-byte through both binaries under an
                                    IDENTICAL invocation and **0 differed**.
    probe-log-lint                  250 records, every site symbol resolves
    build_hash.py                   30683596ac68fda8 43  ->  a97470ba38503305 43

⚠ THE HASH MOVED TWICE MORE THAN THE CODE DID. Trimming the round's own prose
out of a COMPILED HEADER (`mlir_gen_impl.hpp`) moved it again — the recorded
2026-08-29 phenomenon, met while obeying the rule that produced it. Everything
binary-dependent was therefore re-measured against the FINAL hash: L1, the queue
gate, the six fixtures, the three formerly-damaged imported fixtures, and a full
`run_oracle` pass. A verdict is a measurement with a timestamp and a binary.

## 9. DIFF BUDGET — DECLARED <= 60 NET, ACTUAL NEGATIVE

    src/compiler/mlir_gen_impl.hpp    10 +   21 -
    src/compiler/mlir_gen_types.cpp    4 +    7 -
                                      14 +   28 -   =  NET -14 LINES

The class fix is smaller than the code it replaced, because two of the three
sites were hand-rolled copies of a table that already existed.

## 10. FOR THE OWNER (reported, not edited)

  1. **A ROW'S PROGRAM WAS REPLACED, AND THAT IS A CORPUS DECISION.**
     `impl_fn_return_stack_env_dangles` keeps its id and its tier; its program
     and its `observed` (`run 139` -> `run 1`) were rewritten in the same commit
     as the landing that retired its old oracle. The old spelling is kept INSIDE
     the new program so nothing stopped executing. If the preferred handling is
     "delete the row and mint a new id", say so and it is a two-line change.
  2. The three carried-forward items from 2026-09-09g are unchanged and still
     want an owner: `custom_dst_smartptr_owning_drop` claimed by two ledgers;
     `tests/logos/pass/bc_objlt_str_literal`, a green pinned pass fixture that
     corrupts the heap every run and is registered only inside a queue row's
     prose; and `closure_fnonce_call_in_loop_multi_free`, whose correct answer
     is a REFUSAL, so closing it changes its `observed` KIND.
  3. `zonemut_fat_ref_struct_field_layout_abort` (t3, `refuses`) aborts the
     COMPILER with a core dump, not a diagnostic. Pre-existing, unchanged by this
     round, and it is why the queue gate's log carries an `Aborted (core dumped)`
     line on a green run.
