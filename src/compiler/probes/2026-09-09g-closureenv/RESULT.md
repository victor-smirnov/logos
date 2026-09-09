# ROUND 2026-09-09g-closureenv — WHAT THE NUMBERS SAY

Base `30683596ac68fda8 43` (HEAD `3478f9298`) -> probe build (one build, two
edits; `probe-batch` proved the batch INERT: L1 rc=0 with nothing armed).
Base binary preserved as `build/bin/logosc.base09g`, proven LIVE by reproducing
F1's two invalid accesses and F3's wrong value before any verdict was read.

## 0. CENSUS

    soundness queue gate  rc 0  — 78 rows (t1=22 t2=7 t3=43 t4=6), '# TOTAL' 78
    bc_admits 92 · bc_admits_blocked 8 · unrowed_backlog 17
    probe-log-lint: 250 records, every site symbol resolves
    build_hash.py: 30683596ac68fda8 43   (READ, not assumed)

PROMPT CORRECTIONS, each verified against the text actually given:
  * The STEP-1 gate command DOES carry `LOGOS_LIB_DIR`. Nothing to re-report.
    (Four rounds recorded the omission; two more repeated it after the fix.)
  * HEAD is `3478f9298`, not the `48cc71ae7` the prompt's log shows — three
    further commits landed after it was composed, two of them in drop/destructor
    identity.
  * The prompt names SIX closure-capture rows. The ledger has SEVEN: it omits
    `generic_drop_body_calling_closure_param_corrupts_callers_closure` (t1,
    `run 139`), minted 2026-09-08.

## 1. ALL SEVEN ROWS RE-MEASURED ON TODAY'S BINARY — ALL SEVEN STILL REPRODUCE

rc, and the valgrind ERROR SUMMARY beside it, because rc alone cannot tell a
leak from a double free:

    boxed_move_closure_fat_capture_env_overflow    rc 134   2 errors
    closure_owned_dyn_capture                      rc 1     1 error  (Invalid read)
    boxed_escaping_fnonce_capture_double_free      rc 2     0 errors — the double
        DESTRUCTION is a counter, not a heap free; valgrind is silent BY ORACLE
    closure_fnonce_cond_call_untaken_leak          rc 1     0 errors, 0 allocs
    closure_fnonce_call_in_loop_multi_free         rc 1     0 errors, 0 allocs
    impl_fn_return_stack_env_dangles               rc 139   5 errors
    generic_drop_body_calling_closure_param_…      rc 139   2 errors

Not one row was retired by `1979d72f4` / `48cc71ae7` / the `&mut self` receiver
/ `3478f9298`. Three of the seven produce ZERO valgrind records, so for those
the destructor COUNT in the program's own exit code is the only oracle there is.

## 2. `boxed_move_closure_fat_capture_env_overflow` — ITS MEMBERSHIP IS WRONG, RE-DERIVED

The header claims three corpus members. Measured today, by valgrind STACK:

    tests/logos/pass/bc_objlt_str_literal          2 errors   MEMBER, CONFIRMED
        "Invalid write of size 8 … 0 bytes after a block of size 16 alloc'd",
        both the write and the malloc inside `bc_objlt_str_literal$f__f__void`
        — the closure env malloc, the row's own site, and the row program's own
        stack is the same shape one word wider (block of size 24).
    tests/logos/pass/custom_dst_smartptr_owning_drop  2 errors  NOT A MEMBER
        The stack is `__drop_in_place__A` reading a 64-byte block freed by
        `test.MyRc$G1$udyn_Tr__drop_me` — a custom-DST Rc drop-order
        use-after-free with no closure anywhere in it. AND IT ALREADY HAS ITS
        OWN ROW: `custom_dst_smartptr_owning_drop_uaf` in
        tests/logos/unrowed_backlog.ledger:204. Claimed twice, in two ledgers.
    tests/spec/pass/coerce_4                       0 errors   NO LONGER A MEMBER
        The header records 9 records. Today: rc 0, ERROR SUMMARY 0, 5 allocs /
        5 frees. Repaired by a different landing, as the prompt suspected.

So a corpus of three is a corpus of ONE. The header's prose is corrected, not
inherited. `bc_objlt_str_literal` is NOT in the unrowed backlog — its only
registration anywhere is this row's header, so if the header is ever rewritten
without it, a live silently-corrupting green fixture loses its last mention.

## 3. `closure_owned_dyn_capture` — ITS DECLINED ARM RE-MEASURED FOR FREE

`clowndyn` is INSTALLED IN TODAY'S BINARY (mlir_gen_dyn.cpp:1885), so the
2026-09-04 declination cost nothing to re-verify. On `30683596ac68fda8 43`:

    unarmed            rc 1     (the row)
    LOGOS_PROBE=clowndyn  rc 134

Identical to the recorded 1 -> 134. THE DECLINATION HOLDS; a round that
re-derives this arm from the header will re-buy a measurement that already
exists. (Rule 8 cuts both ways — a declination decays too, and this one did not.)

## 4. THE PROBE TABLE — FULL COST LINE

    probe      site                                        fires ceiling cost cfail std
    capfat     mlir_gen_dyn.cpp, capture-struct field walk    85     0     0     0   ok
    capretesc  sema_expr.cpp, the `ec->escapes` guard        170     0     0     0   ok

Both sites PROVEN LIVE (rule 1): 85 and 170 arrivals over the ceiling
population. Ceiling 0 is NOT a refutation here and was predicted as such — the
ceiling population is `bc_admits.ledger`, which prices borrow-check rows and
contains no closure-environment program at all (rule 4).

⚠ **THIS ROUND'S SHARPEST NUMBER IS THAT BOTH `cost 0` COLUMNS ARE FALSE AS
SAFETY CLAIMS, AND THE HAND MATRIX SAYS SO IN TWO DIFFERENT DIRECTIONS.**
`capfat` REFUSES two legal programs (F8, R6) that the cost population does not
contain, and `capretesc` LEAKS 16 bytes on a program that still exits 0 — an
rc-based column cannot see either. Rules 5 and 15, measured again, in one round.

## 5. THE HAND MATRIX — rc AND VALGRIND, BY ARM

`probe::on` arms EXACTLY ONE name per process, so there is no "both" column;
the `both` cells are recorded SKIPPED rather than faked.

    program                          base      unarmed   capfat        capretesc
    F1  1 str capture, boxed         0 / 2     0 / 2     0 / 2         0 / 2
    F3  3 str captures, boxed        1 / 2     1 / 2     1 / 2         1 / 2
    F4  2 &[i64] captures, boxed     REFUSED   REFUSED   REFUSED       REFUSED
    F5  2 scalar captures, boxed     0 / 0     0 / 0     0 / 0         0 / 0
    F7  2 str captures, NOT boxed    0 / 0     0 / 0     0 / 0         0 / 0
    F8  2 closure captures, boxed    0 / 0     0 / 0     **REFUSED**   0 / 0
    F9  &dyn Tr capture              0 / 0     0 / 0     0 / 0         0 / 0
    R2  non-capturing ret impl Fn    0 / 2     0 / 2     0 / 2         0 / 2
    R3  closure at a bare Fn ARG     0 / 0     0 / 0     0 / 0         **0 / 1, 16 BYTES LOST**
    R5  the same through Box<dyn Fn> 0 / 0     0 / 0     0 / 0         0 / 0
    R6  str capture ret as impl Fn   139 / 6   139 / 6   **REFUSED**   139 / 6
    ROW boxed_move…env_overflow      134 / 2   134 / 2   134 / 2       134 / 2
    ROW closure_owned_dyn_capture    1 / 1     1 / 1     1 / 1         1 / 1
    ROW boxed_escaping_fnonce…       2 / 0     2 / 0     2 / 0         2 / 0
    ROW closure_fnonce_cond_call…    1 / 0     1 / 0     1 / 0         1 / 0
    ROW closure_fnonce_call_in_loop  1 / 0     1 / 0     1 / 0         1 / 0
    ROW impl_fn_return_stack_env…    139 / 5   139 / 5   139 / 5       139 / 5
    ROW generic_drop_body_calling…   139 / 2   139 / 2   139 / 2       139 / 2
    pass/bc_objlt_str_literal        0 / 2     0 / 2     0 / 2         0 / 2

`base` and `unarmed` agree in every cell — the control that says the probe build
differs from the base binary only by what the env var arms.

F4's refusal was READ, not counted: "coercion to `dyn Fn` requires
`|| -> i64: 'static` — lifetime `'%1` may not live long enough (object lifetime
bound)". Rust refuses that program too. It is a CORRECT refusal and it is
withdrawn as a defect claim.

## 6. BOTH HYPOTHESES REFUTED, AND EACH ONE REFUTED DIFFERENTLY

### capretesc — REFUTED BY ITS OWN ROW, EXACTLY AS PREDICTED
Predicted before the binary existed: "I predict this probe DOES NOT CLOSE ITS
OWN ROW." It does not. `impl_fn_return_stack_env_dangles` reads 139 / 5 armed
and unarmed, digit for digit, at a site that fired 170 times.

THE ROW HEADER NAMES THE WRONG ROOT, AND R2 IS WHY. R2 is a NON-capturing
closure returned as `impl Fn`; codegen takes the `captures.empty()` NULL-env
path, so `escapes` cannot reach it — and R2 still reads "Use of uninitialised
value of size 8" IN MAIN. What dangles is not (only) the env: `gen_closure`
returns `create_entry_alloca(ctype)`, the `{fn, env}` PAIR itself, an alloca in
the defining frame, and an `impl Fn` return hands back that POINTER. `Box<dyn
Fn>` (R5) is clean precisely because `Box::new` copies the pair to the heap.
TWO DOORS IN SERIES (rule 2), and the row's header names only the inner one.
The header's own text already contains the evidence and does not draw it:
"⚠ THE NON-CAPTURING `impl Fn` RETURN IS DIRTY TOO AND IS PART OF THIS ROW."
An arm behind the env door cannot move a program that has no env.

AND THE CRUDE ARM IS NOT FREE: R3, the iterator-adapter shape, goes from
valgrind-clean to `definitely lost: 16 bytes` while still exiting 0. The
`cost 0 / cfail 0` columns above are rc-based and are BLIND to it (rule 15).

### capfat — THE SITE IS LIVE, THE ARM MOVES PROGRAMS, AND NOT ONE OF THEM IS ITS TARGET
This is the more useful failure. `capfat` changed exactly two cells, F8 and R6,
and changed both from a running program to a REFUSAL:

    F8  error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type …
              but got '!llvm.struct<(ptr, ptr)>'      <- closure_llvm_type
    R6  error: … but got '!llvm.struct<(ptr, i64)>'   <- slice_llvm_type

So the arm DID widen the env slot for a `Kind::Closure` capture and for a
`Kind::Slice` capture — and the CONSUMER, the closure body's binding of that
capture, still GEPs the slot as if it held a pointer. THIS IS HALF A MECHANISM
(rule 2), and it is the same pair of halves the `2026-09-08-fatrepr` round
needed: `fatfield` (the layout) was worthless without `fatbind` (the binder).
I priced the layout half alone and got a compiler that refuses more programs.

AND THE TARGET DID NOT MOVE AT ALL. F1, F3, the row and `bc_objlt_str_literal`
are unchanged in every column. The reason is a THIRD type spelling, measured:

  * `fn f(s: &str)` — the capture is `Kind::Slice`; `capfat` fires on it (R6).
  * `let s = "abc"` — the capture is NEITHER `Slice` nor `Closure` nor
    `TraitObject` nor `DstRef`; `capfat` does not fire on it, and the env slot
    stays 8 bytes while the store writes 16.
  * `let s: &str = "abc"` — REFUSED outright, "coercion to `dyn Fn` requires
    `|| -> i64: 'static` — lifetime `'_` may not live long enough". That is the
    queue's OWN row `str_annot_loses_static_for_dyn_fn` (t3, `refuses`), met
    here from the other side: the ANNOTATION loses the literal's 'static, which
    is exactly why `bc_objlt_str_literal` is pinned as "THE LEGAL TWIN".

⚠ SO THE ROW'S POPULATION IS AT LEAST TWO TYPE SPELLINGS OF ONE VALUE, AND THE
UNANNOTATED LITERAL — the only spelling the row's own program and its only true
corpus member use — IS THE ONE NO KIND TEST CATCHES. A repair keyed on
`Kind::Slice` would close R6's shape and leave the row exactly where it is,
which is what this probe measured. `ref_repr_of` is not the answer either: it
classifies BY THE OUTER KIND and returns `ThinPtr` for `K::Ref` by design
(mlir_gen_types.cpp:878, with a comment saying so).

## 7. THE GROUPING CLAIM — TESTED, AND IT HELD AS A SPLIT
Predicted: "no program in the queue moves under both". Measured: no program in
the queue moves under EITHER. The two arms' changed sets are
{F8, R6} and {R3}, disjoint, and neither intersects any row. The seven
closure-capture rows are not one root; on this round's evidence they are at
least five, and `bug_boxed_closure_runtime_garbage` names one of them:

    1. the env FIELD LAYOUT collapses a fat capture to 8 bytes  (env_overflow)
    2. the returned `{fn,env}` PAIR is an alloca in the dead frame,
       independent of the env and of `escapes`                  (impl_fn_return, R2)
    3. an owning `Box<dyn>` capture is treated as a borrow      (owned_dyn, clowndyn)
    4. `FnOnce` consume is applied at some callable forms and not others,
       and freeing at the consuming call is the missing half    (escaping_fnonce)
    5. `closure_owned_drop_` is a `std::set` with no branch merge and no
       loop-aware move analysis                                 (cond_call, call_in_loop)
    (+ 6. the generic drop body calling its closure type parameter, one day old)

## 8. A CONTROL THAT WOULD HAVE CHANGED NOTHING, CAUGHT BEFORE IT WAS BELIEVED
The scratchpad already held an `ro_base.tsv` from an earlier round (2026-09-08
23:14, 6565 rows, complete and plausible). Ninety seconds after launching this
round's `run_oracle.py` the file was there, full, and looked like an answer. It
was the OLD one. The run's own start/end timestamps — not the file's existence
— are what says a run finished. Same shape as the base-binary copy trap the
2026-09-08 round recorded, one directory over.
(The stale file held 6565 rows; this round's real baseline holds 6572. The
population itself moved between rounds, so the count is a second, independent
tell — and a diff against the stale file would have reported 7 phantom rows.)

## 9. WHAT DESERVES FUNDING, IN ORDER

### A. THE `impl Fn` RETURNED PAIR — the best-shaped work in this block
The row's inner door (`escapes`) is MEASURED not to matter: opening it moves
nothing, at 170 arrivals. The outer door is exact, small, and has a control that
already works — `Box<dyn Fn>` (R5) is clean because `Box::new` copies the
`{fn,env}` pair off the frame. The missing behaviour is that an `impl Fn`
return must hand back the PAIR BY VALUE, not `create_entry_alloca`'s pointer
into the frame that is about to die. R2 is a two-line reproduction with no
captures at all, so the change can be developed against a program in which the
env question cannot confuse the measurement.
⚠ Fund the OUTER door FIRST and re-measure the inner one after. On today's
evidence `escapes` may not be part of this row at all, and the round that funds
both at once cannot tell which one paid (rule 13: the increment can be negative).

### B. THE CLOSURE CAPTURE-STRUCT FIELD WALK — real, and needs THREE things, not one
The site is live (85 arrivals) and its defect is certain: with two `str`
captures the env is `malloc(24)` = three 8-byte slots, and the store writes a
16-byte fat pair into the last one. But this round measured that a repair needs
all of:
    1. the FIELD TYPE (what `capfat` did) — and keyed on the REPR, not the kind;
    2. the BINDER in the closure body, or the program stops compiling with
       "'llvm.getelementptr' op operand #0 must be LLVM pointer type … but got
       '!llvm.struct<(ptr, i64)>'" (this is `fatbind`'s twin, one site over);
    3. AN ANSWER FOR THE UNANNOTATED STRING LITERAL, whose capture type is
       neither `Slice` nor `Ref`-classified-fat and which is the ONLY spelling
       the row's program and its one true corpus member use.
Point 3 is the one to settle first and it costs a CENSUS, not a fix: put
`census()` on the capture kind at mlir_gen_dyn.cpp's field walk and read what
`let s = "abc"` actually produces. Rule 16 — "no fact recorded" and "the fact is
absent" are different, and I could not tell them apart from outside the compiler.
This round bought that question precisely; it did not buy the answer.

### C. NOT WORTH FUNDING NEXT
  * `clowndyn` for `closure_owned_dyn_capture` — re-measured today, still
    1 -> 134. Do not re-derive it from the header.
  * `capretesc` in any crude form — it leaks 16 bytes at the iterator-adapter
    shape (R3) and closes nothing.
  * The two `FnOnce` flow rows — named arms, no codegen question, and the loop
    row's correct answer is a REFUSAL, so closing it CHANGES ITS `observed`
    KIND. That is a corpus decision to state before the round, not inside it.

## 10. LEDGER / CORPUS DECISIONS FOR THE OWNER (reported, not edited)
  1. `custom_dst_smartptr_owning_drop` is claimed by TWO ledgers — as a corpus
     member of `boxed_move_closure_fat_capture_env_overflow` (wrongly, by
     stack) and as `custom_dst_smartptr_owning_drop_uaf` in
     unrowed_backlog.ledger:204 (rightly). One of the two mentions should go.
  2. `tests/logos/pass/bc_objlt_str_literal` is a GREEN, PINNED pass fixture
     that corrupts the heap on every run, and its only registration anywhere is
     one line of prose inside a queue row's header.
  3. `closure_fnonce_call_in_loop_multi_free`'s correct answer is a REFUSAL
     (the program is illegal Rust). Closing it changes `observed`
     `run 1` -> `refuses`; the gate reads `observed` in both directions.

## 11. THE RUNTIME COLUMN — AND IT CAUGHT WHAT `cost 0` DID NOT
`scripts/run_oracle.py`, 6572 pass fixtures compiled, linked and RUN, three
passes off ONE configure (581 s each): unarmed baseline, `capfat`, `capretesc`.
Diffed BOTH ways.

    arm         rows changed   after subtracting cast-region-to-uint by name
    capfat           2         **1 DAMAGED**
    capretesc        1         0

    capfat  logos_04_advanced_features_pass_closure_call_moves_string
            cc 0 / run 0  ->  cc 1 / run -    A GREEN PASS FIXTURE, REFUSED.

`cast-region-to-uint` changes its stdout hash under BOTH arms and under neither
arm's influence — it prints a stack address, and it is subtracted by name as the
tooling notes say.

⚠ SO THE FULL LINE FOR `capfat` IS `fires 85 · ceiling 0 · cost 0 · cfail 0 ·
stdlib ok · RUNTIME 1 DAMAGED`, and every column except the last one said the
arm was free. The batch's `cost`/`cfail` populations do not contain the shape;
`run_oracle` does. Rule 15 in its exact recorded form, met on a real corpus
fixture rather than on a hand program.

⚠ AND THE OTHER DIRECTION IS WORTH AS MUCH: `capretesc` damages ZERO fixtures in
the runtime column and still LEAKS 16 BYTES on R3. A leak has no exit code and
no stdout, so ALL FIVE columns — ceiling, cost, cfail, stdlib and runtime — are
blind to it, and only valgrind on a hand program saw it. In this block the
destructor count and valgrind are not a supplement to the harness; for a leak
they are the whole oracle.

## 12. TREE STATE AT THE END
Both probe edits REVERTED and the compiler REBUILT, because a probe left in the
binary with the sources reverted is code in no source file. Control:
`build_hash.py` reads `30683596ac68fda8 43` again — the base hash, digit for
digit. `logosc.base09g` deleted. Soundness queue gate rc 0, 78 rows.
L1 rc 0 (784/784, 12 684 generated cases, 150 gates).
Nothing landed: this round PRICED, it did not fix. Both hypotheses REFUTED.
`fires:` capfat 85 · capretesc 170, on build `30683596ac68fda8 43`.
