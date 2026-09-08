# THE ENUM HALF OF THE DROP-EMISSION DECISION, MEASURED — AND THE PIN THAT BLOCKS ALL OF IT
Round 2026-09-08(b). Nothing was landed from the design decision. One new queue
row was added.

## 0. CENSUS (STEP 1, run, not copied)

    git log --oneline -3   cccfc7e89 / 977917036 / e14eae50d      git status: clean
    # TOTAL   soundness_queue 80 · bc_admits 91 · bc_admits_blocked 15
    awk rows  80 by direct listing
    probe-log-lint  248 records, every site symbol resolves
    build_hash.py   eaac8e4b73c1fe24 43   (READ)
    QUEUE GATE      rc 0
    gate-run.sh -L bc   rc 0 — build 947, 6532 recorded, 0 failed, ALREADY MEASURED
                        (logosc 0.42.0-preview+main-ge14eae50, libs eaac8e4b73c1fe24)
    run_oracle.py (base)  6557 pass fixtures compiled, linked and RUN

The STEP-1 gate command in this round's prompt CARRIES `LOGOS_LIB_DIR`, as it did
in the last one. Re-verified against the prompt text, not against the journal.

## 1. THE CLUSTER SPLITS IN TWO, AND ONLY ONE HALF IS ABOUT THE DESIGN QUESTION

The previous round priced "the BODY owns" (A) against "the SITE recurses" (B) and
showed A is not a candidate. What it did not separate is that B is TWO
independent edits in two different arms:

  * B-enum  — the scope-exit emitter's `else if (k == K::Enum && drop_fn.empty())`
              guard, and `gen_drop_value`'s enum branch `if (!top_level) return;`
  * B-struct — `gen_drop_value`'s STRUCT branch `if (!top_level) return;`

They are measured here one at a time. B-enum touches no struct program and
B-struct touches no enum program.

## 2. THE CLASS, ENUMERATED BY THE PROPERTY

Class: **a value of ENUM type with a user `impl Drop` and an owning payload.**
Members = the emission SITES at which such a value is destroyed. Enumerated from
the two emitters, not from a grep, and each written as a program
(`docs/probes/dropenum-2026-09-08/n*.logos`, destructor counts, not exit codes):

    site                                   want  base   E1   E2
    n01 top-level local, 2 droppable vars    11     0    11   11
    n06 moved into a callee                   1     0     1    1
    n07 returned from a fn, then bound        1     0     1    1
    n08 drop body reads, never moves          1     0     1    1
    n11 loop body, ten iterations            10     0    10   10
    n12 early return past the local           1     0     1    1
    n05 assignment over a local              11     0    10   11
    n03 held as a STRUCT FIELD                1     0     0    1
    n04 element of an ARRAY                  11     0     0   11
    n15 element of a TUPLE                    1     0     0    1
    n02 payload of another user-Drop ENUM     1     0     0    1
    n13 payload is a user-Drop STRUCT         1     0     0    0   <- STRUCT class
    n09 non-droppable variant taken  CONTROL  0     0     0    0
    n10 no droppable payload at all  CONTROL  0     0     0    0
    n14 drop body DESTRUCTURES `self`         1     1     2    2   <- THE COST

**BASE IS 0 AT EVERY SITE.** The enum class does not leak "at three sites" as its
row header says; it leaks at every site a value can occupy, and the row's own
program only exercises one of them.

E1 = the scope-exit guard alone: closes the SCOPE-EXIT site and nothing else.
E2 = E1 + the enum branch's `!top_level` return: closes **11 of the 12 defective
members**; the twelfth (n13) is a STRUCT payload with its own user Drop and
belongs to the struct class.

PREDICTION (`PREDICTION.txt`, written before any of the 15 was compiled): correct
on 14 of 15 for E1. The miss is n05 — predicted "not closed, 0", measured 10: the
scope-exit drop of the FINAL value is itself a scope-exit site, so the
assignment-over shape half-closes under E1 and fully under E2. A prediction that
forgot a site, in a round whose subject is a list of sites.

## 3. THE QUEUE, ALL 81 PROGRAMS, DIFFED BOTH WAYS

Every program under `tests/soundness/open/` compiled, linked and RUN on base and
on E1, then on base and on E2. Both diffs contain exactly one line:

    enum_user_drop_skips_payload_glue   rc 1 -> rc 0    (stdout "…(want 1): 0" -> "…: 1")

No other row moves in either direction. E2 closes one queue row and masks none.

## 4. THE COST OF E2, EVERY COLUMN

    column                                    E2
    queue rows closed                         1   (enum_user_drop_skips_payload_glue)
    queue rows broken / masked                0   (all 81 programs, diffed both ways)
    23-shape sweep vs base                    1 moves (s06 0->1, correct); s22 1001->1002
    15 counter-examples                       11 fixed, 1 broken (n14), 2 controls held
    RUNTIME oracle, 6557 fixtures             1 damaged
    cfail — fail_text_oracle, 1457 fixtures   0 changed (rc, stderr sha, .expected match)
    stdlib, four layers                       ok (lang, mem, lcm, std all built)

RUNTIME ORACLE, DIFFED BOTH WAYS, no row present on one side only:

    logos_02_semantic_core_pass_drop-trait-enum-b154   runrc 0 -> 2
    logos_02_semantic_core_pass_cast-region-to-uint    stdout sha changed
                                                       (subtracted by name — it
                                                        prints a stack address)

That is the WHOLE cost of E2 in the tree: one fixture.

THE PATCH (reverted before this round closed; `git status` clean, `build`
rebuilt from the restored sources):

    src/compiler/mlir_gen_stmt.cpp
      · gen_drop_enum_payload() split out of gen_drop_value's enum branch — the
        variant switch + payload recursion WITHOUT the user `Drop::drop` call,
        so a site that already called it does not call it twice (rule 7: the
        crude spelling that re-enters gen_drop_value reads 2003 for 1002).
      · the scope-exit emitter: `else if (k == K::Enum && drop_fn.empty())`
        becomes `else if (k == K::Enum)`, with `drop_fn.empty() ?
        gen_drop_value(..) : gen_drop_enum_payload(..)`.
      · gen_drop_value's enum branch: `if (!top_level) return;` after the user
        drop call is DELETED.
    src/compiler/mlir_gen_impl.hpp — the declaration.

## 5. THE ONE FIXTURE, AND WHY IT IS NOT A LICENCE

`tests/imported/pass/drop/drop-trait-enum-b154` is the only thing E2 breaks
anywhere: 1 of 6557 at run time, 0 of 1457 in the fail column, 0 in the stdlib,
0 of 81 in the queue. Its shape is n14 / s22 — a by-value `Drop::drop` body that
DESTRUCTURES `self` and so consumes the payload itself, after which the site's
recursion drops it a second time.

E2 IS THEREFORE DECLINED, BY NAME AND BY NUMBER: one pass fixture, red.
Not edited. A pass fixture whose PROGRAM diverges from its upstream is a corpus
decision with an owner, and the re-port policy defers such a divergence rather
than repairing it in a fix round. See §8.2 for what the repair is and what it
measures.

## 6. THE FINDING THAT BLOCKS THE WHOLE STRUCT HALF, AND IT IS NOT THE DESIGN QUESTION

`tests/logos/pass/drop_glue_three_levels` — RUN on the base binary this round:

    stdout "b3", exit 42.   `A::drop` runs ZERO times.

The program builds `C { nested: B { inner: A { id: 7 }, tag: 3 } }`, C is
destroyed at scope exit, and `A::drop`'s ONLY effect is printing `a7 `. The A
value is owned, reachable and destroyed, so **every design in which its
destructor runs at all prints `b3 a7 `**, and `run_test.sh` compares the whole
stdout for equality (`[ "$ACTUAL_STDOUT" != "$WANT_STDOUT" ]`, line 246), not as
a substring. The `.expected` pins `b3`.

So the pin does not cost design B specifically. It costs EVERY answer to the
owner's question, including "the body owns" implemented properly (the drop body
would drop `self`'s remaining fields at its epilogue and print `a7` there
instead). The fixture's own comment says `c drops via glue → B::drop(b3) →
A::drop(a7)`. **It is the gate on the struct half of the cluster, and the design
decision cannot be tested behind it.** NOT EDITED — a pass fixture asserting a
divergence is a corpus decision with an owner.

## 7. THE NEW ROW

`drop_body_conditional_move_double_drops_both_paths` (tier 1, run 1). A by-value
`Drop::drop` body that moves a field of `self` out **under an `if`** double-drops
that field on BOTH paths — including the one where the move never executes. The
destructor prints its own `w`, so both calls are visibly reading the same
initialised value; this is not a garbage read:

    G::drop armed=0 / N::drop w=7 / N::drop w=7 / total=14   want 7
    G::drop armed=1 / N::drop w=7 / N::drop w=7 / total=14   want 7

Two controls, one token apart each, correct on the same binary: the same body
with the move statement DELETED reads 7, and the same conditional move in a PLAIN
function taking the container by value reads 7 in both spellings. The
discriminator is that the value is the `self` of a drop body, whose fields are
released by the CALLER's glue.

It is not `drop_body_moving_field_double_drops_local`: that row is the
UNCONDITIONAL move, which a static moved-path record closes. This one is why no
static record can be the whole answer — on the disarmed path the body consumes
nothing, so a static skip turns the double free into a leak. The stdlib's
`DropGuard` is exactly this shape.

## 8. WHAT THE BLOCK NEEDS

  1. `drop_glue_three_levels` re-pinned to `b3 a7` (or retired). Without it the
     STRUCT half of the cluster — `replace_site_skips_field_drop_glue`, n13, s04,
     s05, s11, s13, s18, s19 — cannot be tested at all, under ANY design.
  2. `tests/imported/pass/drop/drop-trait-enum-b154`'s receiver. Upstream is
     `fn drop(&mut self)`; the port wrote `fn drop(self: Foo)` and its body
     `match self` MOVES the payload out. That single divergence is the ENTIRE
     drop-related cost of E2, and it is what makes today's compiler LOOK correct
     on the very test written to catch this defect. MEASURED, one file, two
     receivers, two binaries (the `&mut` spelling also needs `**c` where the
     by-value one writes `*c`, so it is not a one-token edit):

         receiver            base binary        E2 binary
         `self: Foo`         rc 0  (passes)     rc 2  (201, payload dropped twice)
         `self: &mut Foo`    rc 2  (1, the      rc 0  (101, exactly upstream's
                             nested SendOnDrop        assertion)
                             never runs)

     The port's receiver is load-bearing in the wrong direction: with the
     upstream receiver the fixture is RED on the unmodified compiler today.
  3. THEN E2 lands as written, closing the enum class at 11 of 12 sites.

## 9. HYGIENE AND THE CLOSING NUMBERS

The E2 arm was reverted before this round closed (`git checkout` of the two
files), `build` was rebuilt from the restored sources (rc 0, 59 targets), and
`build2` is deleted. `scripts/build_hash.py` reads `eaac8e4b73c1fe24 43` — byte
for byte the pre-round value.

    L1                        rc 0 — 780/780, 12 684 generated cases, gates 156/156
    L4 bc                     rc 0
    queue gate                rc 0 — 81 rows (tier1=28 tier2=7 tier3=41 tier4=5),
                                     `# TOTAL` 81, re-derived by direct listing
    bc_admits / _blocked      91 / 15, unchanged — no bc row opened or closed
    run_oracle (final tree)   unchanged: the compiler and the four stdlib layers
                              are byte-identical to the base measured above; the
                              round's only tree changes are a ledger line, one
                              program under tests/soundness/open/ (not globbed by
                              CMake) and documentation.

The 15 counter-example programs, the prediction written before they were run and
this record are kept together under `docs/probes/dropenum-2026-09-08/`. They are
not registered tests; any binary can be re-measured against them with
`docs/probes/dropdesign-2026-09-08/run1.sh`.
