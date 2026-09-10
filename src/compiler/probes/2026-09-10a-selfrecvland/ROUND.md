# ═══ ROUND 2026-09-10a-selfrecvland — THE RECEIVER SLOT OF THE IMPL-VS-TRAIT
# CONFORMANCE CHECK IS LANDED, BOTH DOORS, AS TWO COMMITS: THE 33-FILE CORPUS
# CONVERSION ON THE UNMODIFIED COMPILER FIRST, THEN THE ARM — AND THE PROOF THE
# CONVERSION IS A SPELLING CHANGE IS 29 RUNTIME TRIPLES THAT DID NOT MOVE ══════

STEP 1, read not assumed. HEAD `1fad68657`, tree clean. Build `911811129fca26d5 43`.
Queue gate rc **0**, 75 rows (tier1 19 / tier2 9 / tier3 40 / tier4 7), `# TOTAL` 75.
`bc_admits` 90, `bc_admits_blocked` 8. probe-log-lint: 264 records, every site
symbol resolves. Targets written before the compiler was touched: `TARGETS.md`;
the by-name prediction before the armed build: `PREDICTION.md`.

## 0. CORRECTIONS TO THE PROMPT, EACH RE-VERIFIED AGAINST THE TEXT GIVEN

 1. **The STEP-1 gate command DOES carry `LOGOS_LIB_DIR`.** Checked against the
    prompt in front of me, not against the journal: it is present, with the
    four-round history beside it. Nothing to correct. (This is the correction
    two earlier rounds got wrong by copying.)
 2. **"RE-VERIFY THE THREE ROWS' RECORDED CONTROLS" names no rows.** The prompt
    then fixes the subject to `sigselfty`, which is not a queue row at all. The
    control that WAS re-verified is the one the subject needs: the 30-name
    `run_oracle` set from `2026-09-09k-selfrecv/tables/run_oracle_diff_sigselfty.txt`,
    re-measured today (§2) — 29 of 30 identical, the 30th the documented
    nondeterministic one.
 3. **The spec repairs the prompt orders were PARTLY already done.** `e6a13b523`
    repaired `expr.drop.struct-user-drop-then-fields` and its enum twin and
    repointed the four stale `#L880-L938` ranges. What it MISSED, and what this
    round repaired, is the third copy of the same fact:
    `intrinsic.drop.owner-drops-fields-after-user-drop`.
 4. **`probe-batch.sh` was never invoked** — this round installs no probe. The
    arm's text came from `2026-09-09k-selfrecv/spec{1,2}.txt` with the probe
    gates removed. `git status` checked after every step; 43 live probes before
    and after (`build_hash.py`).

## 1. THE 90-FILE QUESTION, MEASURED — IT IS A DROP QUESTION, AND IT IS THE OWNER'S

The prompt says: if a LOCALLY declared `trait Drop` does not drive drop glue,
those 90 files are not a drop question and there is nothing to escalate. Three
programs on the unmodified compiler, differing in ONE line, each returning the
destructor's counter as its exit code:

    local `trait Drop { fn drop(self: Self); }` + by-value impl   rc 1
    no local declaration (the stdlib lang item) + by-value impl   rc 1
    the same trait RENAMED `Kill`, identical body                 rc 0

**It drives glue.** The mechanism is in the open at `sema.cpp` `is_drop_impl_`:
`return c && c->trait_name == "Drop";` — a bare NAME, with its own comment saying
"collect_impl's builtin_marker_ merges any package's `trait Drop`". So the
90 files are live `Drop` impls whose *declaration* happens to be local, and that
local declaration is what governs conformance (which is why the arm's corpus cost
excludes all 90 and all 26 `tests/imported/pass/drop` ports).

**REPORTED, NOT EDITED.** 213 by-value sites in 90 files. The question — may a
user trait be named `Drop` and re-declare the lang item's signature — is the
owner's, and 26 imported ports depend on the answer.

⚠ It is also RULE 12 in a new column: a lookup KEY (`trait_name == "Drop"`) is
not an IDENTITY. The destructor-identity fix `3478f9298` re-keyed glue away from
the mangled `<T>__drop` name to the trait — but to the trait's NAME.

## 2. THE CORPUS CONVERSION — `176170b6d`, ON THE UNMODIFIED COMPILER

Census re-derived, before and after, by anchored grep:

    before   251 sites / 123 files, 0 in stdlib/
             of which 213 / 90 declare their own `trait Drop`  ← untouched
             and       38 / 33 bind the stdlib lang item       ← converted
    after    213 sites /  90 files, and all 90 declare their own `trait Drop`
             — the stdlib-bound by-value population is EMPTY
    also     8 shared-ref `fn drop(self: &…)` sites: 4 are a local
             `trait Drop { fn drop(self: &Self); }` + its impls, 3 are INHERENT
             methods named `drop` returning i32/i64, and none binds the lang item

Two of the 38 needed a body change, both auto-deref shortfalls, both one
character: `zz_btree_dyn_probe` (`arc_data_ptr(&self)` is `&&mut NodeARC`) and
`drop_mut_self_and_enum` (through `&mut E` the payload binder is
`&mut *mut i64`, so the fold is `**c`). `drop_generic_struct`'s `ByVal`/`ByMut`
pair was renamed `ByExpl`/`ByShort` — the names had become false and the pair
now separates the explicit spelling from the shorthand.

**THE PROOF IT IS A SPELLING CHANGE.** `run_oracle.py`, 6614 pass fixtures
compiled, LINKED and RUN, on the unmodified compiler AFTER the conversion,
diffed against the 30 names the pricing round recorded BEFORE it:

    29 of 30 triples identical, digit for digit — (ccrc, runrc, stdout-sha)
     1 differs: cast-region-to-uint, which prints a stack address and is the
       name that column is documented to subtract

A destructor lost or run twice moves that sha or that rc. None moved. Green was
never the argument; the triples are.

⚠ **THE ROW THAT WOULD HAVE BEEN CORRUPTED.**
`tests/soundness/open/unsized_local_binds_place_dropped_after_free.logos`
(tier 2, `admits`) carried an `fn drop(self: A)` INCIDENTAL to its defect. Under
the arm it is refused for the RECEIVER; its row reds; and the gate — which only
asks whether an `admits` row still admits — reads that as CLOSED by a landing
that never touched it. Rewritten in the corpus commit; gate re-run before and
after, rc 0, 75 rows, the row still admits.

## 3. THE ARM — TWO DOORS IN SERIES, NEITHER SUFFICIENT

`src/compiler/sema_collect.cpp`, `SemaChecker::collect_impl`:

 * **Door 1**, in `_self_shape_artefact`: the escape hatch written for a DST
   ALIAS (`str` → `[u8]` vs the written `&str` → `&[u8]`) now declines only when
   both sides carry the SAME reference-indirection PREFIX. Without it the hatch
   ended `return !types_equal(sub, impl_t);` and was TRUE exactly when the check
   would have something to say — the receiver conformance check was dead by
   tautology for every trait method whose declared receiver mentions `Self`, and
   its sentence had been in the tree unreached.
 * **Door 2**, at the param-0 comparison: `|| !types_equal(_t0, c->param_types[0])`.
   `_alpha_ok` compares LIFETIME STRINGS only, so `&S` and `&mut S` both collect
   the empty list and read as conformant. This is the receiver twin of the
   `|| !types_equal(tra, c->ret_type)` that landed for the RETURN slot in
   `2026-09-09sigland`.

Rule 2, measured rather than assumed: door 1 alone (`sigselfdepth`) leaves
`&Self` vs `&mut Self` admitted — h2, h9, n9, n10 below are the four programs
that separate the halves. Door 2 alone is never reached, because door 1's hatch
short-circuits first. 34 added lines, 1 removed.

Armed build `70933411074df20c 43` (READ), 43 live probes before and after — this
round installs none.

## 4. THE HAND TABLE — 31 PROGRAMS, PREDICTED BY NAME FIRST, ZERO SURPRISES

`PREDICTION.md` was written on the unarmed binary before the arm was applied.
`tables/hand_base.txt` vs `tables/hand_armed.txt`, diffed BOTH ways: the set that
moved is EXACTLY the predicted set, no member more and none missing.

    REFUSED after the arm (10)
      h1  by-value `self: S` vs stdlib `&mut Self`
      h2  SHARED-ref `self: &S`                          ← door 2 only
      h5  by-value on a GENERIC struct
      h7  NON-Drop trait `Tick`, `&mut Self` declared, by value written
      h9  non-Drop, trait `&mut Self`, impl `&S`         ← door 2 only
      h10 REVERSE: trait by value, impl `&mut S`
      h13 by-value Drop reached through a generic bound
      n9  trait `&Self`, impl `&mut S`                   ← door 2 only
      n10 trait `&mut Self`, impl `&S`                   ← door 2 only
      n12 stdlib `Drop` written by value
    SENTENCE CHANGED, same rc — a text-only cost no rc column can see (1)
      h6  by-value Drop whose body moves a field out: was E0509, now the receiver
    ADMITTED, unchanged — the direction that condemns an arm (20)
      h3 h4 h8 h_c h_i n1 n2 n3 n4 n6 n7 n8 n11 n13
      h11 h12 h_a h_b h_d h_e   (already refused for another reason)

⚠ **RULE 5 IS WHY THE ADMIT SIDE IS TWENTY.** The previous round in this arc had
seven correct verdicts drawn from ONE syntax and four legal refusals underneath
them. The twelve NEW programs (`hand/n*.logos`) vary the SYNTAX, not the count:
a generic trait with its own type parameter; `Self` substituted to a generic impl
target and written out in full; the `&mut self` shorthand declared against
`self: &mut S` explicit; an associated fn with NO receiver; an unoverridden
DEFAULT body; a by-value receiver declared AND written; a LIFETIME-annotated
declared receiver against a bare impl one (rule 12 — an alpha-rename is never a
shape difference); the stdlib `Drop` in its canonical shape with the destructor
count as the exit code; and a LOCAL `trait Drop` declaring by value.

Every refusal's sentence was READ, not inferred from an exit code — the check
names BOTH sides and BOTH directions:

    impl Drop for S:  the receiver is declared '&mut S' and the impl declares 'S'
    impl Drop for S:  the receiver is declared '&mut S' and the impl declares '&S'
    impl Peek for S:  the receiver is declared '&S' and the impl declares '&mut S'
    impl Tick for S:  the receiver is declared '&mut S' and the impl declares '&S'

## 5. THE PINS — FOUR REFUSALS AND ONE ADMIT, IN PAIRS

Only a FIXTURE ratchets behaviour, so the landing brings five, and every one of
the four refusals was verified RED on the unarmed binary first (rc 1 each) and
green after — a fixture that passes both ways pins nothing.

    fail/impl_recv_byvalue_vs_mutref            trait `&mut Self`, impl by value
    fail/impl_recv_sharedref_vs_mutref          trait `&mut Self`, impl `&S`   ← door 2 only
    fail/impl_recv_mutref_vs_sharedref          trait `&Self`,     impl `&mut S`
    fail/drop_impl_byvalue_receiver_refused     the `Drop` INSTANCE of the rule
    pass/impl_recv_conformant_shapes            SEVEN legal shapes in one program

`impl_recv_conformant_shapes` exits with the destructor COUNT of its local
`trait Drop` member, so a lost glue reads 0 and a double free 2 without moving
any of the other six assertions.

## 6. THE SPEC — THREE CLAUSES, TWO OF THEM ALREADY FALSE BEFORE THIS ROUND

 * NEW `trait.impl.method-receiver-conforms` (traits-generics.md): the rule, both
   doors, both directions, the exact sentence, and the `Drop` consequence.
 * `trait.impl.method-signature-match` said "**the receiver (param 0) is always
   skipped**". Repaired, and its `#L3389-L3458` Source — a range that no longer
   holds the code — replaced by a SYMBOL citation.
 * `type.drop.receiver-shapes` (types.md) blessed by-value / `&T` / `&mut T`
   drop receivers. It is TRUE, of the GLUE MATCHER; it was being read as a
   conformance licence. Scoped, with §1's three-program measurement in it.
 * `intrinsic.drop.owner-drops-fields-after-user-drop` (expressions.md) still
   said a nested drop "stops, because the by-value `self` consumes its own
   fields" — deleted by `1979d72f4`, repaired in its two `expr.drop.*` twins by
   `e6a13b523`, and MISSED here. Repaired in the corpus commit with its own
   measurement (a three-deep `Top { Mid { Leaf } }` folding 100/10/1 exits
   **111**). ⚠ THE SHAPE OF THIS DEFECT: one fact had THREE copies in the spec
   and the round that repaired two of them did not grep for the third.

Both divergence registries checked again on today's tree, by CONSTRUCT and not
by guessed id: `docs/DIVERGENCES.md`'s 17 letter rows carry nothing about impl
receivers (A7 panic/Drop-on-unwind, A16 auto-Copy, B1/B2/B3/B6/B7/B8 — read),
and no `docs/spec/*.md` clause named impl-vs-trait receiver conformance before
this one. So the standing rule applies and the answer is Rust's, which is also
the owner's decision of 2026-09-09.

## 7. THE CONTROL REVERT — MEASURED AFTER THE LANDING, ON ITS OWN BUILD

The arm alone was reverted (`git show 176170b6d:src/compiler/sema_collect.cpp`),
the corpus, the spec, the pins and the five fixtures left exactly as landed, and
the tree rebuilt: control build `db84de12f8f489fa 43`.

    the four refusal fixtures          ALL FOUR RED   (20% of 5 passed)
    pass/impl_recv_conformant_shapes   still green    (it must pass either way —
                                       it is the over-refusal guard, not a pin)
    the 31 hand programs               IDENTICAL to the original unarmed
                                       baseline, every column
                                       (tables/hand_control_revert.txt vs
                                        tables/hand_base.txt — empty diff)

So the four pins are held by the ARM and by nothing else in the two commits, and
every armed difference in §4 was caused by the arm alone.

`fail_text_oracle.py` was taken on the control build and diffed against the armed
run: **1478 fail fixtures, 0 rows differing in ANY column** — not the exit code,
not the normalised stderr sha, not the `.expected` match. The text-only column
`ctest` cannot produce is empty too. (The two runs came from DIFFERENT builds and
still agree on every sha, which is itself the evidence that the ABI-freshness
warning that makes this oracle self-invalidate did not fire.)

RESTORED, and the restore PROVEN: the arm re-applied, rebuilt, all five fixtures
green again.

## 8. ⚠ `build_hash.py` DISAGREED WITH ITSELF, AND IT IS NOT NON-DETERMINISM

Same sources, `git checkout` of the identical file, rebuilt: `70933411074df20c`
became `975b6cac446d26be`. A no-op `cmake --build` immediately after leaves it at
`975b6cac446d26be`, so codegen is deterministic. The mover is the CONFIGURE
timestamp baked into the binary — `logosc --version` reads
`0.43.0-preview+main-g176170b6-dirty.20260910T065656Z` — and `build_hash.py`
hashes `bin/logosc` byte for byte. Its own docstring warns that the version
STRING is not an identity; the finding is the other direction: that string lives
INSIDE the artefact the key is computed from, so **a reconfigure moves the key
with no source change**. Adding five fixtures forces a reconfigure (the corpus is
globbed), which is why a fixture-adding round sees this and a probe round does
not. The version string's commit id is stale too (`g176170b6`, one commit back).
Recorded, not repaired — tooling is frozen.

    STEP 1 build      911811129fca26d5 43
    armed build       70933411074df20c 43   (§4, §6 measurements)
    control revert    db84de12f8f489fa 43   (§7)
    restored build    975b6cac446d26be 43   (identical sources to the armed one)
