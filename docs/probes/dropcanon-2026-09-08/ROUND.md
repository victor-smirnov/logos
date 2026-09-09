# THE OWNER'S DROP DECISION, PRICED — BOTH HALVES, ON AN ARM THAT WAS ALREADY IN THE TREE
Round 2026-09-08(c). PRICING ONLY. No compiler change is landed; the one probe
installed is env-gated and is reverted before the round closes.

## 0. CENSUS (STEP 1, RUN, NOT COPIED)

    git log --oneline -3   ca9ec814f / cccfc7e89 / 977917036     git status: clean
    # TOTAL   soundness_queue 81 · bc_admits 91 · bc_admits_blocked 15
    awk rows  81 by direct listing; 81 programs on the shelf
    probe-log-lint  248 records, every site symbol resolves
    build_hash.py   eaac8e4b73c1fe24 43   (READ; 3bd2c3af1c114227 43 after the probe build)
    QUEUE GATE      rc 0 — 81 open rows (tier1=28 tier2=7 tier3=41 tier4=5)

CORRECTIONS TO THE PROMPT, each re-verified against the prompt text in front of
me and not against the journal:
  · the STEP-1 gate command CARRIES `LOGOS_LIB_DIR`. Third round running. The
    correction is landed and must stop being reported.
  · "close, or row by name: the unrowed double free … `if self.armed`" — ALREADY
    ROWED, by the previous round, as `drop_body_conditional_move_double_drops_both_paths`
    (tier 1, run 1). Nothing to row.
  · "`drop_body_moving_field_double_drops_local` … B's whole remaining cost is
    ONE property" — MEASURED HERE AS FIVE SHAPES, not one program (§4).

## 1. THE THREE RECORDED CONTROLS RE-VERIFIED ON TODAY'S BINARY (eaac8e4b73c1fe24 43)

    row                                                  recorded   today
    drop_body_moving_field_double_drops_local            2 (want 1)   2  REPRODUCES
    drop_body_conditional_move_double_drops_both_paths  14 (want 7)  14  REPRODUCES
    enum_user_drop_skips_payload_glue                    0 (want 1)   0  REPRODUCES
    replace_site_skips_field_drop_glue                  rc 11        11  REPRODUCES
    letstruct_destructure_skips_user_drop               rc 100      100  REPRODUCES

None decayed.

## 2. THE ARM WAS ALREADY IN THE TREE, AND ITS DECLINE NAMED THE CLAUSE THE OWNER JUST RETIRED

`src/compiler/borrow_check.cpp:14741` — `probe::on("fldmovedrop")`, E0509, priced
2026-08-28 with the verdict written into the source: *"it contradicts a written
language rule, and funding it is a DESIGN decision (PAIR), not a checker round.
Recorded, not fixed."* The written rule is
`intrinsic.drop.skip-moved-out-paths`. The owner retired it on 2026-09-08.
So the decline is overturned BY NAME, and no build was needed to re-price it.

## 3. THE E0509 DOOR CENSUS — MEASURED, TEN DOORS, TWO HOLES

Each door a separate hand program, shapes varied (rule 5), all on today's binary
under `LOGOS_PROBE=fldmovedrop LOGOS_FLDMOVEDROP_TRACE=1`:

    door                                                      trace  refuses
    dotted path            `let q = s.f;`                       yes    YES
    destructuring let      `let S { f: x, g: y } = s;`          yes    YES (2 sites)
    tuple-struct pattern   `let S(x, y) = s;`                   yes    YES (2 sites)
    FRU                    `let s1 = T { a: 2, ..s0 };`         yes    YES
    drop body, uncond      `fn drop(self: P2) { let q = self.inner; }`  YES
    drop body, conditional `if self.armed { let q = self.f; }`  yes    YES
    GENERIC drop body      `impl<T> Drop for W<T>`, instantiated yes    YES
    GENERIC plain fn       `fn take_out<T>(g: W<T>) -> T`       yes    YES
    nested path            `let q = a.b.inner;` (B impls Drop)  yes    YES
    MATCH struct pattern   `match s { S { f: a, g: b } => .. }`  NO    no   <- HOLE
    MATCH enum payload     `match e { E::V(x) => .. }`           NO    no   <- HOLE
    `&mut self` field move `fn take(&mut self) -> N`             --    inherited E0507

The two MATCH doors produce NO trace line at all — the `moving` branch is never
reached there, so this is a structural hole, not a predicate that answers wrong.
It is load-bearing three times: it is why `borrowck-move-error-with-note--b`
does not close, and it is why `tests/imported/pass/drop/drop-trait-enum-b154`
(whose ported drop body is `match self`) is NOT refused today.

LEGAL CONTROLS, all silent under the arm (over-refusal check, six shapes):
  move a field out of a Drop-LESS struct · move the WHOLE Drop value · read a
  Copy field of a Drop struct · borrow `&s.f` · move the field out of a
  `#[no_auto_drop]` wrapper · a drop body that only READS its fields.

## 4. B's "ONE PROPERTY" IS FIVE SHAPES, AND E0509 REFUSES EXACTLY THOSE FIVE

The previous round's 23-shape table records design B wrong in FIVE cells, not
one. Each of the five was compiled under `fldmovedrop` today:

    shape (docs/probes/dropdesign-2026-09-08/)          want  B   refused by E0509
    s02 local, drop body MOVES its field out              1    2        YES
    s15 assign-over, drop body moves field                2    4        YES
    s16 drop body moves field, value as a FIELD           1    2        YES
    s20 drop body moves field under `if armed`  ARMED     1    2        YES
    s20 the same guard DISARMED                           1    2        YES
    ── controls, where B is CORRECT ───────────────────────────────────
    s01 local, empty user drop, droppable field           1    1        no
    s03 assignment over a local                           2    2        no
    s17 partial move of a field                           2    2        no
    s22 enum whose drop body DESTRUCTURES `self`       1001 2003/1002   no  <- MATCH-door hole

5 of 5 B-wrong shapes refused, 0 of 3 B-correct shapes refused. The intersection
is exact. **Design B's remaining wrongness IS the E0509 class**, and the one
shape it misses (s22) misses for the measured MATCH-door hole in §3, not for a
design reason — s22 is `drop-trait-enum-b154`'s shape, whose port changed the
receiver away from upstream's `&mut self`.

## 5. WHAT THE OWNER'S DECISION RETIRES, ROW BY ROW

`docs/spec/expressions.md` `intrinsic.drop.skip-moved-out-paths` is cited by SIX
rows in `tests/logos/bc_admits_blocked.ledger`, all tagged `BUCKET-3
DIVERGENCE-DROP`. Derived from the file, not from the prompt. Every one of the
six moves BECAUSE OF THE CLAUSE — none has an independent reason to stay:

    row                                              root     door        E0509 refuses today
    borrowck-move-out-of-struct-with-dtor            bck.E    dotted path      YES
    borrowck-struct-update-with-dtor--b              bck.E    `T{a:2,b:s0.b}`  YES
    borrowck-struct-update-with-dtor--t17            bck.E    FRU `..s0`       YES
    borrowck-move-error-with-note--b                 bck.E    MATCH pattern    no  (§3 hole)
    borrowck-move-out-of-tuple-struct-with-dtor--r13 bck.NEW  struct-pat let   no  (A16 Copy)
    borrowck-move-out-of-tuple-struct-with-dtor--t13 bck.NEW  struct-pat let   no  (A16 Copy)

The last three carry a SECOND blocker each, which the clause's retirement does
not remove: `--r13`/`--t13` move a field of type `struct Inner { a: i64 }`,
which DIVERGENCES A16 structural auto-Copy makes Copy, so no move is seen at all
(trace: zero `[fldmovedrop]` lines — reason UNCHANGED from the 2026-08-28
record); `move-error-with-note--b` uses the match door. They still return to
`bc_admits.ledger` — they are defects again — but they return with a root that
is not `bck.E`.

⚠ THE ACTIONABLE LEDGER GROWS BY 6, 91 -> 97, and `bc_admits_blocked` shrinks
15 -> 9. That is the CORRECT result.

Also retired with the clause: the spec fixture block that pins it,
`tests/spec/pass/intrinsic_1.logos` lines 217-238 (`let moved: Noisy = h.inner;`
out of a `Holder` that impls Drop, asserting `DROP_LOG == 49`). That block is the
WHOLE `pass` cost of the E0509 arm — see §6.

## 7. THE BY-VALUE DROP RECEIVER — PRICED SEPARATELY, AND IT IS NOT SMALL

The prompt asks how many programs declare `fn drop(self: T)`. Measured by direct
listing over `tests/` and `stdlib/`:

    `fn drop(self: <bare type>)`, IMPL sites   176 occurrences in 136 files
    `fn drop(self: &mut …)`                    496 files
    `fn drop(self)` with no type                 0

AND THE DECLARATION SITE IS THE STDLIB'S OWN TRAIT: `stdlib/lang/drop/`
declares `pub trait Drop { fn drop(self: Self); }`. A by-value receiver is not
136 fixtures' spelling choice — it is what the trait's signature REQUIRES, and
every one of the 496 `&mut` impls is currently mis-signed against it (which is
exactly the class `bug_missigned_default_impl_miscompiles` covers).

VERDICT: **refusing the by-value receiver is its own round and its own row.** It
is a stdlib trait-signature change plus 176 impl sites plus whatever the
mis-signing already hides. It must NOT ride along inside either half priced here,
and neither half needs it: `dropbstruct` and `fldmovedrop` are both correct with
the by-value receiver in place.

## 8. THE STDLIB `DropGuard` — THE PROMPT'S STEP 1 IS NOT A BLOCKER, MEASURED

`stdlib/mem/manually_drop/manually_drop.logos` has TWO E0509 sites, not one:

    line 158  `drop_guard_disarm_into<F>(g: DropGuard<F>) -> F { return g.f; }`
    line 164  `impl<F> Drop for DropGuard<F> { fn drop(self){ if self.armed { let f = self.f; … } } }`

Under `LOGOS_PROBE=fldmovedrop` the file compiles SILENT, rc 0. The trace shows
line 158 reaching the site with `drop=1` — the report is raised and never
surfaces, because **nothing in the tree instantiates `DropGuard`**: `grep -rn
DropGuard tests/ stdlib/` outside the defining file returns only two COMMENT
lines inside a soundness-queue program's header. A generic body is checked at
its MONO instance (measured: the same shape in a hand program with an explicit
instantiation refuses, naming `W$G1$N__drop`), so an un-instantiated generic
costs nothing and is protected by nothing.

Consequences, both directions:
  · the E0509 arm's stdlib column is CLEAN (`ceiling-probe` §6: all four layers
    compile) — the corpus-repair-first step the prompt orders is not gating
    either compiler half;
  · but the two sites are REAL and the repair is still owed, because the
    `DropGuard` shape is `drop_body_conditional_move_double_drops_both_paths`
    verbatim. It should be repaired the Rust way (`ManuallyDrop<F>` + an explicit
    `ptr::read`, or `Option::take` on `f`) — and the repair must be accompanied
    by a fixture that INSTANTIATES `DropGuard`, or nothing will ever check it.

## 9. FIELD ORDER — STATED, AND IT IS A THIRD EDIT, NOT PART OF B

Rust drops a struct's fields in DECLARATION order (and a tuple's elements in
index order). Logos counts DOWN at all four loops:

    src/compiler/mlir_gen_stmt.cpp  gen_drop_value struct branch   `for (i = size-1; i >= 0; --i)`
                                    gen_drop_value tuple  branch   same
                                    scope-exit struct arm          same
                                    scope-exit tuple  arm          same

So the order to implement is FORWARD (declaration order), because that is Rust's
and the owner's decision is "Rust-canonical". It is ALREADY rowed twice —
`struct_fields_dropped_reverse_order` and `tuple_elems_dropped_reverse_order`
(tier 3, run 1) — and both were re-verified today: rc 1 on base AND rc 1 under
`dropbstruct`. **The `dropbstruct` arm does not move either of them**, so the
order flip is a separate change with a separate control and must not ride along
inside the B landing (rule 13: a per-site measurement is not additive).

## 6. THE PROBE TABLE — EVERY COST COLUMN, BOTH ARMS

    column                                   fldmovedrop (E0509)      dropbstruct (design B, struct)
    ---------------------------------------- ------------------------ ------------------------------
    build (unarmed -> armed)                 947 -> 951               952 -> 953
    fired                                    101                      15729
    CEILING, bc_admits ledger                5 rows                   0 rows  ⚠ WRONG POPULATION
    CEILING, soundness queue (81 progs, run) 3 rows                   1 row
    COST, pass (ledger+legal, 1047 tests)    1                        1
      the one program                        logos_25_spec_pass_       logos_03_ownership_pass_
                                             intrinsic_1               drop_glue_three_levels
    COST-fail (1457 fixtures, rc/sha/.exp)   2 text-only, 0 rc         0
    COST, stdlib (four layers)               clean                    clean
    RUNTIME oracle (~6557 fixtures, RUN)     n/a (a refusal arm)       see §6.2

⚠ `dropbstruct`'s `CEILING = 0` and the script's `⛔ COST >= CEILING` verdict are
an artefact of the POPULATION, not a refutation. `ceiling-probe.sh` counts its
ceiling over `logos_00_bc_admit_*` — a BORROW-CHECK shelf whose oracle is "the
compile is silent". `dropbstruct` is a CODEGEN arm; it cannot move a compile's
exit code and no admit row can ever see it. Its ceiling was measured where it
lives: all 81 soundness-queue programs compiled, linked and RUN on both binaries.

### 6.1 THE 81-PROGRAM QUEUE SWEEP, DIFFED BOTH WAYS

`fldmovedrop` (compile only): exactly THREE of 81 newly refused, and refusal is
the FIX for all three (each is an illegal program under the owner's decision):
    drop_body_moving_field_double_drops_local
    drop_body_conditional_move_double_drops_both_paths
    letstruct_destructure_skips_user_drop
No other row is refused; nothing is masked.

`dropbstruct` (compile + link + RUN): exactly ONE of 81 moves, in the closing
direction, and nothing moves the other way:
    replace_site_skips_field_drop_glue   rc 11 -> rc 0

### 6.2 PREDICTED BY NAME BEFORE THE RUN — 8 of 8 CORRECT

`PREDICTIONS.md` in this directory, written before the armed binary existed:

    program                                             predicted   measured
    replace_site_skips_field_drop_glue                  11 -> 0     11 -> 0    ok
    drop_glue_three_levels                              "b3"->"b3 a7 "         ok
    drop_body_moving_field_double_drops_local           stays 2     2 -> 2     ok
    drop_body_conditional_move_double_drops_both_paths  stays 14    14 -> 14   ok
    enum_user_drop_skips_payload_glue                   stays 0     0 -> 0     ok
    letstruct_destructure_skips_user_drop               stays 100   100 -> 100 ok
    drop-trait-enum-b154                                stays rc 0  0 -> 0     ok
    custom_dst_smartptr_owning_drop                     stays 42    42 -> 42   ok

### 6.3 THE RUNTIME COLUMN, DIFFED BOTH WAYS

6557 pass fixtures compiled, linked and RUN on the base binary and on
`dropbstruct`. No row present on one side only. EXACTLY TWO triples differ:

    logos_02_semantic_core_pass_cast-region-to-uint    stdout sha changed
                                                       (subtracted by name — it
                                                        prints a stack address)
    logos_03_ownership_pass_drop_glue_three_levels     stdout sha changed,
                                                       ccrc 0, runrc 42 unchanged
                                                       "b3" -> "b3 a7 "

**The whole cost of design B's struct half in this tree is one fixture, and that
fixture's own comment asks for the output the arm produces** ("c drops via glue
-> B::drop(b3) -> A::drop(a7)"). Its `.expected` pins `b3`.

## 10. THE THREE NAMED FIXTURES — WHAT EACH ASSERTS NOW, AND WHAT ITS SUBJECT DOES

  · `tests/logos/pass/drop_glue_three_levels` — pins `exit: 42 / stdout: b3`.
    Under `dropbstruct` it prints `b3 a7 ` and still exits 42. Its SUBJECT
    (three-level glue) still runs, and runs MORE of itself than before: `A::drop`
    goes from ZERO calls to one. The repair is `stdout: b3 a7 `, which is what the
    file's own comment already documents. NOT a weakening — the fixture currently
    pins a destructor NOT running.
  · `tests/imported/pass/drop/drop-trait-enum-b154` — rc 0 today, rc 0 under
    `dropbstruct` (it is an ENUM; the struct arm does not touch it). It is NOT
    refused by the E0509 arm either, because its body is `match self` and the
    match door is the measured hole. The recorded E2 measurement stands: with
    upstream's `&mut self` receiver the fixture is RED on today's compiler and
    GREEN under the enum arm. Re-porting it as-is is therefore a REPAIR that
    makes the fixture assert upstream's own assertion (101), not a weakening.
  · `tests/logos/pass/custom_dst_smartptr_owning_drop` — pins `exit: 42`.
    Re-checked under the new rule: rc 42 on base AND under `dropbstruct`, and the
    E0509 arm does not fire on it at all. Neither half of the decision moves it.
    Its recorded use-after-free is NOT reproduced by either arm and is not
    evidence for or against them; it needs its own valgrind read, not this round.

## 11. WHAT DESERVES FUNDING

FUND, as ONE landing in three commits (they are in SERIES — rule 2 — because
each half alone is measurably wrong where the other covers it):

  1. CORPUS/SPEC, unmodified compiler: retire `intrinsic.drop.skip-moved-out-paths`
     and the `tests/spec/pass/intrinsic_1.logos` block that pins it; move the six
     `DIVERGENCE-DROP` rows from `bc_admits_blocked` to `bc_admits` with the roots
     in §5; re-pin `drop_glue_three_levels` to `b3 a7 `; re-port
     `drop-trait-enum-b154` to upstream's `&mut self` receiver; repair the stdlib
     `DropGuard` and ADD a fixture that instantiates it.
  2. BORROW CHECK: promote `fldmovedrop` to a real E0509 with an E0509-shaped
     sentence naming the type (the probe's own sentence is a probe's), plus the
     MATCH-door hole in §3 — without that door the class is 8 of 10 and
     `drop-trait-enum-b154` stays hidden. Cost after step 1: ZERO in every column.
  3. CODEGEN: `dropbstruct` (struct half) + the previously measured enum half.
     ⚠ ADDITIVITY IS NOT ASSUMED (rule 13) — they were priced SEPARATELY, on two
     different binaries, and the combined arm has never been run. The landing
     round must measure the two TOGETHER before believing 1 + 1 queue rows.

DO NOT FUND IN THIS BLOCK:
  · the by-value drop receiver refusal (§7) — 176 impl sites and a stdlib trait
    signature; its own round, its own row.
  · the field-order flip (§9) — a separate change at four loops; already rowed
    twice and moved by neither arm.
