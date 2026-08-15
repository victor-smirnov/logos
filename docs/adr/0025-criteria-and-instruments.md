# ADR 0025 — the three criteria, their instruments, and the residual inventory

Sibling of [`0025-deem-batch-cursor-plane.md`](0025-deem-batch-cursor-plane.md).
That document is the DESIGN and the slice journal. This one is the arc's
DEFINITION OF DONE: Victor's three criteria as stated, what each means
operationally on this tree, the instrument that reads it, the reading it gives
today, and — §4 — every named residual in ONE list an audit can walk.
**§6 is the arc's CLOSING SECTION**: the three verdicts, the priced remainder
with an owner or the word UNOWNED against every row, this round's own overclaims,
and the STOP.

**Why it exists.** The S5 round's audit (2026-08-14) confirmed as F6: criterion
1's instrument was *not in the repository* (`609/4,023 = 15.14%` lived only in
commit prose and could not be re-derived from the tree), criterion 3's
instrument was missing on the same terms *and* blind by construction where it
was applied, and **criterion 2 was not STATED anywhere** in the tree or the last
30 commits. Its verdict, verbatim: *"#47 cannot be answered until the three
criteria and their two instruments are written into the tree."* This file is
that, plus the third instrument the audit's F2 asked for.

**Supersede-in-place discipline applies here as to any ledger.** A number in
this file is either re-derivable by running the command printed beside it, or it
is struck through with the measurement that replaced it. Nothing here is
inherited from a commit message. ⚠ The closing audit found that rule broken in
three ways and left the breaks visible rather than tidied: a number borrowed
from a different quantity (§1), an instrument that could not see the shape it
judged (§2, §3), and two §4c rows whose content lives in the task tracker rather
than in the tree (§4c). A ledger that only records the numbers it got right is
not a ledger.

---

## 0. The mandate, verbatim (Victor, 2026-08-11)

1. **No intermediate materialization inside Deem.**
2. **Full integration of the query algebra with batch + cursor.**
3. **ARC/RC only where needed; in ALL hot data-access paths BC alone carries
   memory safety.** «Если для этого нужно будет переписать всю Меморию и весь
   Deem — переписываем.»

The verbatim intent is the subject; the operational definitions below are this
tree's reading of it, and each is falsifiable. Where a reading is narrower than
the sentence, that is said out loud rather than absorbed.

---

## 1. Criterion 1 — no intermediate materialization

**Operational definition** (fixed at S2, unchanged): *every compiler-inserted
materialization is a NAMED PLAN NODE WITH A GROUND.* The criterion is not "zero
collections" — a hash join builds an index and an `avg` carries a denominator.
It is that no collection appears in emitted code without an identity a reader
can ask about and `explain()` can print. The named identities are the census
identities: `materialize`, `arrange`, `key vector`, `group frame`,
`accumulator`, `group count`, `representative row`, `drain`, `sort`.

**Instrument**: [`tests/logos/criterion1_materialization_instrument.sh`](../../tests/logos/criterion1_materialization_instrument.sh)
— the sandbox `audit_cov_instr.sh` promoted, with the classifier that was never
written down. Its header carries the definitions; run it as

```
tests/logos/criterion1_materialization_instrument.sh /tmp/c1
```

over the whole `pass/wql_*` + `pass/deem_*` population, two channels:

| symbol | channel | definition |
|---|---|---|
| `D1` | trace (`LOGOS_TRACE_PLAN=1`) | all `[plan]` ground sentences |
| `N1` | trace | `[plan]` lines whose HEAD is a materializing node kind |
| `T` | trace | the historical reading: `[plan]` lines containing the text `materializ` |
| `D2` | artifact (`--gen-dir`, `logos.gen.*` excluded) | `let [mut] n: (Vec\|Buffer\|HashMap\|HashSet\|BTreeMap)<…>` bindings. ⚠ `HashSet` was ADDED AT R-C1 and was worth 319 bindings — every `D2` printed below the R-C table is over the old, wrong population |

**Reading on this tree (185 fixtures, 169 with dumps, 2 known non-compiling):**

```
D1 = 4036   N1 = 1334 (33.05%)   T = 615   D2 = 3620, accounted 1911 (52.79%)
```

⚠ **RE-RUN AT R-A, 2026-08-15 (186 fixtures, 170 with dumps, 2 non-compiling):**

```
D1 = 4042   N1 = 1336 (33.05%)   T = 617   D2 = 3632, accounted 1919 (52.84%)
```

⚠⚠ **SUPERSEDED BY R-B, 2026-08-15 — THE OUTPUT SEAM IS NAMED, AND THE FIRST
VERSION OF THIS BLOCK MIS-ATTRIBUTED HALF THE MOVEMENT. Both columns below are
the SAME instrument over TWO TREES separated by ONE variable** — the control is
this tree with `rexpr_walk.logos` reverted to `93295e0c` (pre-R-B emitter),
rebuilt (33 ninja steps, `liblogos-mem.a` re-archived), swept, and restored to a
green checkpoint after:

| | CONTROL (pre-R-B emitter) | R-B | Δ |
|---|---:|---:|---:|
| `D1` all `[plan]` sentences | 4042 | **4692** | **+650** |
| `N1` named materialization nodes | 1336 (33.05%) | **1986 (42.33%)** | **+650, +9.28 pp** |
| `T` historical text match | 617 | 617 | **0** |
| `D2` emitted collection bindings | 3632 | 3632 | **0** |
| accounted | 1919 (52.84%) | **2569 (70.73%)** | **+650, +17.89 pp** |
| criterion-1 worklist | 1713 | **1063** | **−650** |
| `let mut __out: Vec<` | 650 | 605 | −45 |
| `let mut __rout:` | 0 | 45 | +45 |
| `let __out: &mut Vec<` aliases | 336 | 381 | +45 |
| corpus-wide ANSWER DIFF | — | — | **0 lines** |

⚠⚠ **THE `accounted` COLUMN COULD NOT BE READ AS A TREE MEASUREMENT UNTIL THIS
STAGE FIXED THE INSTRUMENT, AND THE CONTROL IS WHAT FOUND IT.** The first version
of this block wrote "`N1` +650 and `accounted` +650" and attributed both to the
emitter. `N1` is right. `accounted` was **1919 → 2569 compared across two
SEPARATELY-MEASURED columns** — the R-A tree read with the R-A table against the
R-B tree read with the R-B table — and the tree-vs-tree delta was **ZERO**: run
on the pre-R-B tree with the post-R-B table, the instrument printed
`accounted=2569 (70.73%)`, identical to R-B, crediting 650 `__out` bindings to
`query output` while all four query-output heads had a fire count of **0 on that
same sweep**. It printed the claim and the evidence against it three lines apart
and summed the claim.

The cause was structural, not a typo: `ACC` mapped a binding-name class to a
PROSE STRING, so the artifact channel asked *"is this name in my table"* and
never *"did the node I am naming get emitted"*. A ledger entry was
self-certifying — the permissive-defect shape, invisible to a green corpus
because nothing was ever red. **`ACC` now names the OWNING HEAD, that head must
be in `MAT`, and a class whose owners all read a zero fire count is REFUSED
(`G4`): reported under CLAIMED BUT UNWITNESSED and counted in the worklist,
where an unowned collection belongs.** With that in place both trees were
re-swept and the table above is a tree comparison throughout. The control now
exits 2 and prints its 1919 first — a tree older than the ledger gets the
refusal, not a flattering number.

**What moved and what did not.** `D2` is UNCHANGED at 3632 and `T` is UNCHANGED
at 617: R-B built nothing, removed nothing, and renamed nothing the text channel
can see. The corpus-wide ANSWER DIFF
(`tests/logos/answer_diff_instrument.sh`, 186 fixtures compiled, linked, RUN,
exit + full-stdout hash per fixture; 184 run, 2 known non-compiling) is
**byte-identical between the two trees — zero lines moved**, which is what an
ownership stage must be able to say. The artifact delta is the rename and
nothing else, read line by line across 19 of 170 dumps: **−45** `let mut __out:`
bindings, **−45** `return Result::Ok(__out);`, **+45** `let mut __rout:`, **+45**
`let __out: &mut Vec<…> = &mut __rout;`, **+45** `return Result::Ok(__rout);`
(18+14+8+3+2 = 45 by element type). Total landings 650 on both trees: one row was
SPLIT, nothing was created or destroyed.

**The nodes, five-valued, each with its own pin** (`plan_ground_census_gate.sh`
FACT J, per fixture AND in total, plan side and artifact side):

| lines | head | the landing it names |
|---:|---|---|
| 477 | `query output` | the plain `_run` landing |
| 16 | `query output bounded by limit` | `__out.len()` IS the limit guard's operand — the landing is READ |
| 5 | `query output distinct carrier` | the landing IS the dedup structure; `distinct` rescans it per push |
| 107 | `incremental snapshot output` | the incremental tier's read surface; no `_stream` door of its own |
| 605 | — | `== let mut __out: Vec<` bindings in the artifact |
| 45 | `rel result` | an internal seam, consumed by the enclosing query, never returned; `== let mut __rout:` |

**Why this is not the node S5 refused.** S5 declined an output-seam node
(`0025-deem-batch-cursor-plane.md:1524`) because with `direct` blocked its answer
would be one constant word on every site — +N census lines telling a reader
nothing. That ground still stands against the node S5 was offered. R-B's claim
is narrower and it is MEASURED, not argued: **the class is not one class.** The
five heads above were counted by fire count at the five emitter sites in R-B0
*before* any node existed (234 `emit_simple` + 152 `emit_aggregate` + 112
`emit_join_chain` + 107 `emit_incremental` + 45 `emit_rel_fn_oneshot` = 650,
predicted then measured, no arm zero), and the artifact-side classifier reads the
same 477/16/5/107/45 independently.

**And the refusal probes prove the pin can tell the two apart** — four
perturbations, one at a time, each restored to a green checkpoint, each rebuilt
(the `logos_09_plan_ground_census` gate reads `build/bin/logosc`, so an
unrebuilt probe is the recorded "control that changes nothing"):

| probe | edit | measured red |
|---|---|---|
| P1 | route the bounded arm to the plain sentence | `query output` 477→493, `bounded by limit` 16→**0** |
| P2 | route the dedup arm to the plain sentence | `query output` 477→482, `distinct carrier` 5→**0** |
| P3 | route the snapshot seam to the plain sentence | `query output` 477→584, `snapshot output` 107→**0** |
| P4 | invert the rel/query discriminator | FACT J reds per fixture on BOTH sides at once, 605 and 45 |

⚠ **In P1, P2 and P3 the TOTAL of 605 never moved.** A node pinned as one number
— "650 output landings" — stays green through all three, which is precisely the
constant-valued node S5 refused, wearing a count. The per-head pins are the only
thing that fails, and that is the operational difference between a discriminator
and a label.

**FACT J found a defect on its first run, and it was in the gate's own
population**, not in the emitter: `let mut __out:` matched 606 corpus-wide
against a plan side of 605. The 606th is `let mut __out: String` — the TRAMA
TEMPLATE renderer's buffer (`stdlib/mem/wql/trama_render.logos:732`), a homonym
on a different plane with no query and no plan. `D2` never counted it either
(its type filter is `Vec|Buffer|HashMap|BTreeMap`), so nothing in the tree had
ever counted it at all. The grep was narrowed to `Vec<` **and the String landing
given its own pin** (`EXPECT_OUTS = 1`) in the same edit — a population narrowed
without pinning what left it is how a class goes quiet.

**R-B1 is what made the pin expressible.** The rel one-shot's landing used to be
spelled `__out` too, so one ACC row held 605 query outputs and 45 rel results and
any owner claimed for it would have over-credited the 45 — the same name-only-key
defect this section already records against `__rel_*`. It was renamed to
`__rout`, with `__out` kept as a `&mut Vec` ALIAS onto it so the four shared push
fragments stay one fragment (the idiom `member_block_frag` already uses 336 times
on the fixpoint plane). The alias is a borrow, so `D2`'s `let mut` regex does not
see it: the row SPLIT 650 → 605 + 45 with the total unchanged, and the artifact
delta was exactly 45 bindings + 45 tails + 45 alias lines, read line by line.

⚠⚠⚠ **SUPERSEDED BY R-C, 2026-08-15 — AND THE FIRST THING R-C DID WAS MAKE THE
TABLE ABOVE WORSE, BECAUSE ITS DENOMINATOR WAS WRONG.** `D2`'s type filter was
`Vec|Buffer|HashMap|BTreeMap` and the dumps contain, by type, **Vec 3028 +
HashMap 592 + HashSet 319 + Buffer 12 + BTreeMap 0**. 3028 + 592 + 12 = 3632
exactly: the miss was a WHOLE CONTAINER KIND, cleanly, not a partial undercount.
Every `accounted` percentage printed in this file before R-C1 — 52.79%, 52.84%,
70.73% — is a share of a population that EXCLUDED 319 emitted collections, and
none of them is comparable to what follows.

**The three stages are separated because two of them are not the same KIND of
change**, and a single before/after column would have laundered an instrument
fix into an emitter result — this arc's recorded failure mode:

| | R-B ledger (WRONG POPULATION) | **CONTROL** = R-C1 instrument, `9395c3d1` emitter | **R-C** = R-C1 + R-C2 + R-C3 | Δ control→R-C |
|---|---:|---:|---:|---:|
| `D1` all `[plan]` sentences | 4692 | 4692 | **5407** | **+715** |
| `N1` named materialization nodes | 1986 (42.33%) | 1986 (42.33%) | **2701 (49.95%)** | **+715, +7.62 pp** |
| `T` historical text match | 617 | 617 | **621** | +4 |
| `D2` emitted collection bindings | ~~3632~~ | **3951** | **3951** | **0** |
| accounted | ~~2569 (70.73%)~~ | 2579 (**65.27%**) | **3294 (83.37%)** | **+715, +18.10 pp** |
| criterion-1 worklist | ~~1063~~ | 1372 | **657** | **−715** |
| instrument exit | — | **2** (G4) | 0 | — |
| corpus-wide ANSWER DIFF | — | — | — | **0 lines / 186 fixtures** |

**ATTRIBUTION, ROW BY ROW — which stage owns each move.**

* **R-C1 owns the whole `D2` 3632 → 3951 and it is an INSTRUMENT change, zero
  emitter lines.** It is therefore in the CONTROL column, not in the Δ: the
  control tree is this tree with only `stdlib/mem/wql/rexpr_walk.logos` reverted
  to `9395c3d1` (HEAD, post-R-B — the one-variable control for R-C's uncommitted
  diff), rebuilt (33 ninja steps, `liblogos-mem.a` re-archived) and swept
  with **the same post-R-C instrument**. One variable, and it is the emitter.
  ⚠ NOT `93295e0c` (its parent): that emitter under this instrument reads
  D1 4042 / N1 1336 (33.05%) / accounted 1929 (48.82%) / worklist 2022 with
  NINE refused classes, so a `93295e0c`-labelled control would inflate R-C's
  Δ to 1365 by crediting it with R-B's 650 `__out` bindings.
  R-C1 landed ALONE, before any naming, on the rule that *a criterion cannot be
  closed on the same commit that fixes the population it is measured over*.
* **R-C1 also owns +10 accounted for free** (`__hs`, the `arrange` node's SET
  form) — and that credit is likewise in BOTH columns. `hash_build_frag`'s
  `set_form` arm emits `__hs<k>: HashSet<K>` at the same site and under the same
  node as `__hm`; `plan_ground_census_gate.sh` FACT C has always counted it
  (`__(hm|hs|bt)`, 598 == 598) while this instrument read 588 against an
  `arrange` fire count of 598. The 10-line gap sat in the worklist looking
  unowned. **It was never unowned; it was untyped.** So R-C1's own before/after
  is 2569 (70.73% of 3632) → 2579 (65.27% of 3951): the credit went UP by 10 and
  the percentage went DOWN by 5.46 pp, which is what a corrected denominator does
  and what no single column would have shown.
* **R-C2 owns +670 of the Δ** — the fixpoint plane, one emitter region
  (`_scc`/`_od`/`_odp`), six heads whose fire counts were measured AT THE EMITTER
  BEFORE THE NODES EXISTED and every one landed on prediction: `__nd_<m>` 222,
  `__rs_<m>` 218, `__dl_<m>` 176, `__os_<m>` 46, `__best_<m>` 4, `__keys_<m>` 4.
  S6-B's "declared out of the BATCH plane" was read for two rounds as a naming
  exemption. It is not one: that declaration says what CONSUMES these rows, and
  this criterion names what EXISTS regardless of which plane consumes it.
* **R-C3 owns the remaining +45** — `rel dedup set`, the one-shot rel helper's
  novelty set, 1:1 with `rel result`. **The control column is what proves it was
  a real class and not a re-label**: on the control the key `__rs` holds **263**
  bindings in ONE row, because the `_run` helper's set was spelled `__rs` too;
  crediting it to `fixpoint novelty set` would have over-credited 45 by the
  `__rel_*` two-owner defect, knowingly. R-C2 refused the sixth of its six
  credits in writing for exactly that reason, and R-C3 paid the debt by R-B1's
  method — RENAME AT THE EMITTER (`__rds`), then name. 263 ambiguous bindings
  became 218 + 45 in two clean rows; **nothing was created or destroyed**.
* **`D2` at a ZERO delta across the control is the load-bearing row.** R-C2 writes
  only to the trace channel (the 170 user dumps are byte-for-byte identical
  between the two trees) and R-C3 is a rename. The naming stage built nothing.
* **`T` +4 is not a materialization.** `fixpoint lattice key roster`'s ground text
  contains the string `materializ`, so the historical text metric picks up its 4
  lines — which is the standing demonstration that `T` counts SENTENCES
  CONTAINING A WORD, not materializations (238 of its 621 are `no
  materialization`, an ABSENCE).

**THE CONTROL EXITS 2, AND THAT IS THE INSTRUMENT WORKING.** G4 refuses a credit
whose owning head has a zero fire count on the sweep being read, and on the
pre-R-C emitter all seven R-C classes read zero: `__rs` 263, `__nd` 222, `__dl`
176, `__os` 46, `__best` 4, `__keys` 4, and `__rds` **0 bindings** — the last
being the rename's own signature. It prints `accounted=2579 (65.27%)` and then
refuses, i.e. a tree older than the ledger gets the refusal rather than the
flattering number. That is the same shape R-B's control caught by measurement
(`accounted` credited to heads that never fired), now caught by construction.

**Restored to a green checkpoint after**: emitter put back, rebuilt, re-swept —
`N1/D1=2701/5407=49.95% T=621 D2=3951 accounted=3294 (83.37%)`, identical to the
R-C column, instrument rc=0. The corpus-wide answer diff
(`answer_diff_instrument.sh`, 186 fixtures compiled + linked + RUN, exit and
full-stdout sha256 per fixture; 184 run, 2 known non-compiling) is **byte-identical
between the two trees: `diff` prints 0 lines** — which is what a naming round
must be able to say, and what neither channel of the criterion-1 instrument can
see on its own.

**WHAT R-B DID NOT DO, priced.** The design's `(b′)` — invert the facade inside
the emitter so the body builds the `Buffer` and the `Vec` entry becomes
`into_vec()` — is criterion 2's output row, orthogonal to the naming above, and
it is NOT landed.

⚠ **ITS RECORDED BLOCKER IS REFUTED — R-C verdict, proven by execution, and the
swap still does not land this round.** The sentence this paragraph used to carry
(*"`Buffer` publishes no `len()` and no `get(i)`, so 477 landings could swap
today, 16 need `len()`, 5 need `len()` + `get(i)` — two Memoria-side methods"*)
was a READ of the published surface that skipped one of the methods it listed:
`Buffer::as_slice(&self) -> &[R]` (`stdlib/mem/stream/buffer.logos:99`) returns a
slice, and `&[R]` carries both `len()` and indexing. **Zero Memoria-side methods
are required**; `len()`/`get(i)` on `Buffer` would be sugar over `as_slice()`,
not a precondition. Probes at `/home/logos/sandbox/bprime/`: `p1.logos` runs both
read shapes (limit guard `(__out.as_slice().len() as i64) < lim` and the distinct
rescan `__out.as_slice()[__d]`) against a `Buffer<i64>` landing returning
`into_vec()` — compiles, links, **rc=0**, seven value assertions; `p1_ctl.logos`
is the same file with the limit guard deleted, **rc=1**, so the probe is not
vacuous; `p2.logos` is the IDENTICAL spelling with `__out` typed `Vec<i64>` —
**rc=0**, so the spelling is TYPE-AGNOSTIC across `Vec` and `Buffer`.

That last result also re-prices the ⚠ below: because the read spelling does not
depend on the container type, the shared fragments (`rel_push_frag`,
`rexpr_walk.logos:9218-9223`) and the 336 `let __out: &mut Vec<…>` fixpoint
aliases stay ONE fragment and do NOT have to move with the swap.

THE MEASURED PRICE (a READ count, distinct from the ledger's landing count):
**14 emitter read sites on `__out`** — 13 `.len()` limit guards
(`rexpr_walk.logos:2391, 2451, 2464, 2511, 2513, 2527, 3295, 3337, 3665, 3673,
3691, 4109, 4833`) + 1 distinct rescan emitting both `.len()` and `.get()`
(`rexpr_walk.logos:1315-1316`), producing 27 `.len()` + 5 `.get()` occurrences
across the corpus. Landing sites: 605 `let mut __out: Vec<E>` declarations + their
`return Result::Ok(__out)` tails (`_snapshot` tail at `rexpr_walk.logos:7614`,
return type built at :7620) + 45 `__rout`.

**VERDICT: priced OUT for this round — refuted blocker, no edit.** The swap is
unblocked but unlanded; it stays R-C's neighbour and criterion 2's output row.

**The whole delta is the corpus, not the emitter.** R-A's control tree
(`slice_stream_src` → `return false`, rebuilt, swept — §5) prints that line
character for character, and the worklist buckets with it. A stage that changes
the SCAN SHAPE and materializes nothing must move criterion 1 by zero, and this
is the measurement that says it did. See §6.1.

⚠ ~~accounted 1891 (52.24%)~~ — SUPERSEDED by the closing audit (§6, D2). The ACC
table named six of the nine identities; `group count` (`__g_cnt` 13) and
`representative row` (`__g_row` 7) had no entry, so 20 bindings the census gate
pins as NAMED (FACT H: plan 13 == artifact 13, plan 7 == artifact 7) were being
reported as the criterion-1 worklist. Both are added; the printed figure is now
1911/3620. **The printed ACCOUNTED is a FLOOR, not the site-level truth:** a
further 12 `Buffer`-typed `__rel_*` prelude landings (`__rel_s` 7, `__rel_m` 3,
`__rel_t` 1, `__rel_d` 1 = `drain` 7 + `sort` 5, the identity FACT B pins) are
owned and still counted as worklist, because ACC is keyed on the NAME alone and
the `__rel_*` names carry 217 bindings in total, 205 of them unowned `Vec`
landings (`__rel_g` 35, `__rel_r` 17, `__rel_w` 17, `__rel_path` 15, …). A
name-only key must take all 217 or none, and it takes none. Site-level
reading: **accounted 1923 (53.12%), remainder 1697**. Closing the gap needs the
per-node attribution recorded in §4d, not a wider name key.

**⚠ The recorded 15.14% was a text ratio, and the majority of its numerator was
not a materialization.** `T = 615` today decomposes as 217 positive
`-> materialize` nodes, **238 `-> no materialization` ABSENCES**, 127 `key
vector` lines whose ground says *"not a second materialization"*, and 33
residual mentions — of which the instrument's classifier counts 12 as
MATERIALIZATION (7 `drain`, 5 `sort`) and 21 as truly incidental; the script
is the authority where the prose and the classifier disagree. Counting absences as coverage is why the number could rise
while the artifact got worse — which the ADR's S6 writ control measured
directly: the text ratio moved 668 → 638 (an "improvement") while the artifact
materialized **29 times more**. The instrument now prints `T` with this
composition beside `N1`, so the historical figure stays reproducible and its
defect stays visible.

**On the recorded denominator 4,023.** It is not re-derivable from this tree
under any reading (`D1` = 4036 here; the S5 auditor's nearest artifact-channel
reading was 3,312, `D2` = 3620 here). The 4,023 was measured at S2's close,
three slices and several node renames ago, and re-deriving it would mean
building `e98f5a58`. It is recorded as SUPERSEDED — not reproduced — and every
number above is from this tree.

⚠ ~~4011 on `af17c2fa` before S6-A's fixture~~ — **STRUCK: a number transplanted
from a different quantity, and the round's own overclaim** (closing audit §6,
D1). `4011` occurs in this tree exactly twice, both in the plane ADR and both as
a DUMP-FILE count: `:1594` "same 4,011 dumps" and `:1632` "4,011 → 4,021 dumps".
The `[plan]`-LINE population at that same snapshot is on the next line, `:1633`:
"4,249 → 4,259 `[plan]` lines". So the tree's own `af17c2fa` reading for the `D1`
quantity is 4,249, and the dump count had been borrowed into the `D1` slot. The
borrowing was invisible because BOTH deltas are +10 (dumps 4,011→4,021 AND plan
lines 4,249→4,259), so "moved `D1` +10" read true against either. Nor is 4,249
usable as a `D1` predecessor: it was measured over a SANDBOX sweep of 203→204
files, while `D1 = 4036` here is over 185 fixtures / 169 dumps — different
populations, not a trajectory. **There is no tree-resident historical `D1`.**
The paragraph whose whole purpose is to disown an unreproducible number was
itself carrying one; that is exactly the failure it warns about.

**Gate or instrument? Instrument, and the reason is measured.** `D1`/`N1`/`D2`
are corpus-size-dependent: adding one fixture moves `D1` — the plane ADR `:1633`
measures +10 `[plan]` lines for S6-A's fixture on its own sweep, and
`deem_pipeline_handle_seam` was measured at +15 when it was added. (⚠ Both are
DELTAS recorded at the time of their slice; neither BASE is re-derivable on this
tree — see the strike above. A delta without a re-derivable base supports the
size-dependence argument and nothing more, and it is used for nothing more.) A
ctest gate over such a value is either re-baselined every commit — a number that
always agrees — or it becomes a target to tune, and the writ control shows
tuning it can move it the wrong way. **The values are reported; three properties
that do NOT move with a slice are gated inside the script (exit 2):**

* **G1** every `materializ` occurrence lies on a `[plan]` line — the text and
  structured readings see one population. *Refusal probe: routing the
  `no materialization` lines off the channel reds with fire count 238.*
* **G2** every `[plan]` HEAD is classified — a slice adding a node kind must
  come to the table and say which side of criterion 1 it is on, instead of
  landing silently in "not a materialization". *Refusal probe: deleting
  `group frame` from the table reds with fire count 152.*
* **G3** one rc file per fixture, count asserted — no probe lost.

**The unaccounted classes ARE the criterion-1 worklist** (artifact channel,
largest first; the script prints the full list). **1709 bindings, 47.21% of
`D2`** — 1697 (46.88%) on the site-level reading that credits the 12 owned
`__rel_*` Buffer landings. ⚠ **At R-A: 1713 of 3632 (47.16%), site-level 1701
(46.83%) — the +4 is `__out` and the corpus, and the control tree prints the
same 1713 (§6.1).**

⚠⚠ **AT R-B: 1063 of 3632 (29.27%), and the movement is the emitter's — the
control tree (pre-R-B, same instrument) still prints 1713.** The `__out` row
below leaves this table entirely: 650 bindings became 605 `query output` + 45
`rel result`, both now ACC-credited AND witnessed by a nonzero fire count (`G4`).
**This is the largest single movement of the criterion in the arc, and 1063 is
still not zero.** ⚠ The `G4` repair also means this list is now honest in a way
it was not: a class credited to a node that does not exist falls back HERE
instead of quietly inflating the accounted column (§4d).

| bindings | class | status |
|---|---|---|
| ~~650~~ **0** (⚠ ~~646~~ ⚠ ~~650~~) | `__out`, `__rout` | ⚠ **LEFT THE WORKLIST AT R-B, 2026-08-15.** The query-output Vec. S2 assigned it to S5; S5 did not take it and no owner was re-assigned (audit F5); **R-B named it** — 605 `let mut __out: Vec<` under four heads (`query output` 477, `… bounded by limit` 16, `… distinct carrier` 5, `incremental snapshot output` 107) and 45 `let mut __rout:` under `rel result`, each with a falsifiable ground, pinned per fixture and per head by FACT J. ⚠ **Named, not removed**: the landings are still `Vec`s filled to completion, which is criterion 2's row and `(b′)`'s job (§6.1). |
| ~~398~~ **0** | `__nd_*`, `__dl_*` | ⚠ **LEFT THE WORKLIST AT R-C2, 2026-08-15 — AND SO DID FOUR CLASSES THIS ROW NEVER LISTED.** The six fixpoint heads are named plan nodes with grounds, pinned per fixture and per head by FACT K: `__nd_<m>` 222 `fixpoint derived frontier`, `__dl_<m>` 176 `fixpoint frontier`, `__rs_<m>` 218 `fixpoint novelty set`, `__os_<m>` 46 `over-deletion set`, `__best_<m>` 4 `fixpoint novelty lattice`, `__keys_<m>` 4 `fixpoint lattice key roster` = **670**. The 264 `HashSet`-typed ones (`__rs_<m>` 218 + `__os_<m>` 46) were absent from this inventory because `D2`'s type filter dropped the kind (R-C1). **S6-B's "declared out of the batch plane" is retired as the argument for this row**: it states what CONSUMES these rows; criterion 1 names what EXISTS |
| ~~45~~ **0** | bare `__rs` → `__rds` | ⚠ **LEFT THE WORKLIST AT R-C3.** The one-shot rel helper's dedup set, `rel dedup set`, 1:1 with `rel result` (identity (iii)). It was never in this inventory either — under its old spelling it was indistinguishable from the 218 fixpoint shadow sets, so it could not be counted separately to be listed |
| 145 | `__cp` | DRed phase collections (`__wql_*_dred`) — the incremental tier. ⚠ **NOW THE LARGEST UNOWNED NON-`__rel_*` CLASS IN THE TREE** (R-C2 took its former row-mates), and the "same declaration" it rested on is the one R-C2 retired. UNOWNED |
| 70 | `__tt` | fixpoint temporaries. S2 assigned the 791-strong fixpoint-buffer class to S6; S6-A took the Writ half only. |
| rest | `__wcd` 44, `__nw` 44, `__odv` 35, `__rmv` 35, `__rel_g` 35, `__rel_r` 17, `__pres` 22, `__lt` 22, `__ecp` 22, … | per-query scratch and rel landings, ≤44 each, un-triaged. ⚠ `__rel_*` is TWO populations under one name: the 12 `Buffer` landings here are OWNED (`drain` 7 + `sort` 5, FACT B) and appear in this row only because ACC keys on the name — see the ⚠ under the reading above. | ⚠ **RE-DERIVED AT R-C: the row is 225 in 8 non-`__rel_*` classes** — `__wcd` 44, `__nw` 44, `__odv` 35, `__rmv` 35, `__pres` 22, `__lt` 22, `__ecp` 22, `__cv` 1 — **plus `__rel_*` 217 in 51 name classes, refused not deferred** (12 owned `Buffer` + 205 unowned `Vec` under one key). 217 + 145 + 70 + 225 = the 657 worklist |

⚠ `__g_cnt` (13) and `__g_row` (7) were swept into this table's last row until
the closing audit; they are NAMED (`group count`, `representative row`) and the
plane ADR `:965` already said of that class "the class is now 0 unnamed". Two
instruments in one tree disagreed about 20 bindings and the pessimistic one was
being reported as the honest reading. Fixed in ACC, not in prose.

---

## 2. Criterion 2 — full algebra integration with batch + cursor

**This criterion had no statement in the tree before this file (audit F6).**

**Operational definition**: *every plane of the algebra — scan, join, sort,
aggregate, output — pulls BATCHES, through the one pull protocol
(`next_batch() -> Option<Batch>`), rather than rows.* The criterion is met when
enumerating the planes leaves no row-at-a-time pull site in emitted query code.
So the instrument is the ENUMERATION, and it is falsifiable because the
spellings are visible in the artifact.

⚠ ~~it is falsifiable because BOTH spellings are visible: `next_batch()` vs
`.next()`~~ — **STRUCK: THERE IS A THIRD SPELLING, AND IT IS THE DOMINANT ONE**
(closing audit §6, C2). Emitted query code does per-row iteration by INDEXED
WALK — `while (<minted> < X.len()) { … X.get(i) … }` — which neither grep
matches. Measured on the audit sweep: ⚠ ~~**3975 indexed walks**~~ **4293**, of
which the `__i` family alone is ⚠ ~~**1160**~~ **1162**; that family is the base
scan of `&[Row]` sources. And the batch-pull inner loop is a FOURTH spelling
again — `while (__bjN < __bnN)` over a saved `__bb.len()`, **15 sites, exactly
one per `next_batch()`** — so ZERO of those indexed walks are inside a batch
pull. The two greps below see 80 sites (15 + 65) of a per-row population that is
at least 4358. **Criterion 2's DIRECTION is unaffected — it is NOT met either
way — but every count this section prints is a FLOOR, not a population**, and the
"yes" rows below rest on greps that cannot see the dominant shape. This is the
same class as audit F2 (an instrument blind by construction), found inside the
instrument written to close it.

⚠ **THE 3975 AND THE 1160 ARE THE READER, NOT THE TREE (R-A, 2026-08-15).** The
derivation printed here was `grep -ho 'while ([A-Za-z_0-9]* < [^;{]*\.len())'`,
and it cannot match the `limit` arm, which emits a DOUBLE paren:
`while ((__i0 < (rows).len()) && (__n < 3i64))`. On the very sweep that produced
3975 the two spellings together are **4293** — 318 sites, 8% of the population,
invisible to the number this section used to size the criterion by. Same defect
one level down for the family: `while (__i[01]\? <` misses `__i2…` and the
double paren, 1160 vs **1162**. The fixed derivation, and the one every number
below now uses:

```
grep -ho 'while ((\?[A-Za-z_0-9]* < [^;{]*\.len())' /tmp/c1/*.user | wc -l   # ALL indexed walks
grep -ho 'while ((\?__i[0-9]* < '                   /tmp/c1/*.user | wc -l   # the __i family
grep -ho 'while (__bj[0-9]* < __bn[0-9]*)'          /tmp/c1/*.user | wc -l   # batch inner loop
grep -ho 'SliceStream::<'                           /tmp/c1/*.user | wc -l   # R-A's wrap
```

⚠⚠ **AND R-A MOVED IT. THE BEFORE/AFTER IS A CONTROL, NOT TWO DATES.** The
before column is not the audit tree (its corpus is 169 dumps against today's
170, and a corpus move is not an emitter move): it is THIS tree with
`rexpr_walk::slice_stream_src` forced to `return false` and the stdlib rebuilt,
so exactly one variable separates the columns. Both sweeps are the whole-corpus
instrument, `/tmp/ra_ctl` and `/tmp/ra_final`:

| spelling | audit (169 dumps) | CONTROL (170) | R-A (170) | Δ control→R-A |
|---|---|---|---|---|
| indexed walks, both parens | 4293 | 4301 | **4152** | **−149** |
| … the `__i` family alone | 1162 | 1166 | **1017** | **−149** |
| … the old single-paren regex | 3975 | 3982 | 3835 | −147 (it misses 2 of the 149 — the `limit` arm again) |
| `next_batch()` pulls | 15 | 15 | **164** | **+149** (9 dumps → 53) |
| batch inner loop `while (__bjN < __bnN)` | 15 | 15 | **164** | +149 |
| `SliceStream::<` wraps | 0 | 0 | **149** | +149 |
| `.next()` row pulls | 65 | 65 | **65** | **0** |

**The accounting closes with no remainder: every wrap emitted removed exactly
one indexed walk and added exactly one batch pull, corpus-wide.** The `.next()`
column is the control INSIDE the measurement: R-A dissolved the third spelling
and touched none of the four row-pull sites, which is what "one plane moved"
looks like when it is true.

**Instrument**: the same sweep as criterion 1 (it already produces the artifact
dumps), plus two greps whose definitions are fixed here:

```
tests/logos/criterion1_materialization_instrument.sh /tmp/c1
grep -ho 'next_batch()\|\.next()' /tmp/c1/*.user | sort | uniq -c   # pull shape
# output plane — ⚠ THREE counts, not one grep (R-B, 2026-08-15). `let mut __out`
# is no longer a population: R-B1 split the rel landing out to `__rout`, and one
# `__out` was never a collection at all. Asking by TYPE answers directly instead
# of by subtraction, which is what the old one-liner forced:
grep -ho 'let mut __out[A-Za-z_0-9]*\s*:\s*[A-Za-z_0-9]*' /tmp/c1/*.user \
    | sed 's/.*: *//' | sort | uniq -c        # 605 Vec + 1 String
grep -h  'let mut __rout:'      /tmp/c1/*.user | wc -l   # 45 rel landings
grep -h  'let __out: &mut Vec<' /tmp/c1/*.user | wc -l   # 381 ALIASES — not landings
```

⚠ **The alias line is in this list because leaving it out is how the output
plane gets double-counted.** 381 = the 336 fixpoint `member_block_frag`
rebindings + R-B1's 45. They are BORROWS onto a landing someone else allocated;
`D2`'s regex requires `let [mut] <n>: <Coll><` and does not match them, and FACT
J excludes them for the same reason. A future reader who greps `__out` without
the `mut` will read 986 and think the plane grew.

— plus the four indexed-walk greps fixed above, which are the ones that see the
dominant shape. ⚠ Two greps were never enough and the ⚠⚠ block above is why.

**Reading on this tree (R-A)** — ⚠ ~~15 `next_batch()` pulls in 9 dumps~~ **164
`next_batch()` pulls in 53 dumps**, **65 row-at-a-time `.next()` pulls in 27
dumps** (unmoved), **4152 indexed walks**:

| plane | pulls batches? | evidence on this tree |
|---|---|---|
| scan, container family | **yes** | leaf batches via `__ctr_b*` / `__ctr_leafbatch` (S1); the descent appears in the hot closure of `deem_pipeline_handle_seam` |
| scan, **`&[Row]` slice parameter** | **yes** (R-A, 2026-08-15) | the plane this row exists to record, added when it moved: `SliceStream::<R>::new(<param>)` + `next_batch()` + the `__bj/__bn` inner loop, **149 sites**, replacing 149 indexed walks one-for-one (control table above). Emitted from ONE function, `rexpr_walk::batch_scan_frag`, which is the same §1 shape the container family already rode — so this is not a second scan shape, it is the slice arm arriving at the first. Routed sites: `emit_simple`, BOTH arms (plain scan, and the sort's phase-1 collect). ⚠ the other TWELVE S2j sites still emit the indexed walk — see the verdict below |
| scan, native iterator source | **no** | 14 sites, `let __opt: Option<R> = (__rel_s).next()` in the row loop. ⚠ UNMOVED BY R-A and the remainder row that predicted otherwise is corrected below: a native iterator source is not a slice, `SliceStream` never sees it |
| drain prelude | **no** | 9 sites, `let __dr: Option<R> = __it_s.next()` landing into a `Buffer` |
| join — probe side | **no** (⚠ ~~yes~~) | the probe reads the driving nest, which is the scan plane — but the scan plane is itself split yes/no by this table, so the row inherited the half it liked. Measured: of **81** dumps that build a join structure (`__hm`/`__bt`), ⚠ ~~**2**~~ **11** contain any `next_batch()` — AND THAT NUMBER MUST NOT BE READ AS THIS ROW MOVING. R-A took 2 → 11 by batch-pulling the SCAN in those dumps; `step_wrap`, the join's own probe nest, is one of the twelve unrouted sites and still emits the indexed walk. A dump-level co-occurrence count is exactly the inheritance defect this cell was struck for the first time; it is kept only because it is the number the instrument prints, and it is labelled. |
| join — **build side** | **no** | 3 sites, `let __bo1: Option<R> = (__rel_p).next()` — `rexpr_walk::build_phase_frag`, **the fourth pull site**, never converted when S1 collapsed the scan. Any batch source on a build side dies there (`type mismatch — expected Option, got Option`: the annotation is the ROW type, the pull yields a BATCH). Invisible to the green corpus BY CONSTRUCTION — no corpus query puts a batch source on that side. |
| sort | **its INPUT does; its key vector still materializes** (⚠ ~~yes (S3)~~, ⚠ ~~the elision arm only~~) | `land_end` (4) + `prev_batch` (4) in **2** dumps for the desc elision, unmoved. What moved is phase 1: the sort arm of `emit_simple` collects through `SliceStream`, so ⚠ ~~3 of the 54~~ **23 of the 55** `__ks` dumps now carry a `next_batch()` — the rows ARRIVE in batches (`wql_distinct_e2e.user:290`, `__ss0.next_batch()` feeding `__ks.push`). The OTHER half is unmoved and is a criterion-1 fact, not a pull-shape one: **55 dumps, 129 `key vector` plan lines, 321 `__ix` bindings** are still built, and the insertion sort that permutes them (`while (__a < __ks.len())`) is itself an indexed walk over a materialized vector. The split the verdict column once erased now runs INSIDE this row: input yes, ordering no. |
| aggregate | **the fold, not the enumeration** (⚠ ~~yes (S4/S5)~~) | single-pass fold over pulled batches — but the min/max retract-rebuild arm (`stdlib/mem/wql/rexpr_walk.logos:5695`) emits `match __it.next()` over a `HashMapKeys<…>`: **39 row-at-a-time pulls in 14 dumps**, 60% of this section's own `.next()` numerator, in a plane scored yes. ⚠ the pure-aggregate-over-a-row-producer arm has ZERO corpus executions (audit F10) |
| output | **no** — but it is now NAMED, and the two are different questions | ⚠ ~~646~~ ⚠ ~~650~~ **605 `let mut __out: Vec<…>` + 45 `let mut __rout:`** (R-B, 2026-08-15). ⚠ **R-B MOVED THIS ROW'S VERDICT BY EXACTLY ZERO AND ITS ATTRIBUTION COMPLETELY, and the control separates the two.** The pull shape is unmoved on this plane and on every other: control and R-B both read `.next()` **65**, `next_batch()` **164**, indexed walks **4152**, `__i` family **1017**, `SliceStream::<` **149** — five numbers, five zero deltas. The landing is still a `Vec` the body fills to completion, so the answer to *"does the output plane pull batches"* is still **no**, and the reason is R-E's `typeof` blocker, unchanged. What DID move is that these 650 collections are no longer UNOWNED: each now carries one of five named plan nodes with a falsifiable ground (§1's table), gated by FACT J per fixture and per head. That is criterion **1**'s question, answered here; criterion 2's question is answered by `(b′)`, which is priced in §1 and NOT landed. The 650 → 605+45 split is R-B1's rename, not a shrink — the total is identical on both trees |
| incremental (`__dl`/`__nd`/`__edb`, DRed) | **no, declared** | all 124 DRed phase fns take `&[…]`; S6-B measured the seam as `<q>_apply`'s parameter list alone and declared rather than attempted it |

**Verdict: criterion 2 is NOT met**, and the open planes are named with counts
rather than adjectives. Of the 65 `.next()` pulls: the build side **3**
(`(__rel_p)` 1, `(__rel_t)` 1, `(__rel_d)` 1), the native-source scan **14**
(`(__rel_s)`), the drain prelude **9** (`__it_s` 7, `__it_t` 1, `__it_d` 1), and
the aggregate key enumeration **39** (`__it.next()` over `HashMapKeys`, 14
dumps) — 3+14+9+39 = 65. Plus the output plane, ⚠ ~~646~~ ⚠ ~~650~~ **605 + 45**
bindings (R-B split the row; the plane's total and its verdict are unchanged, and
R-B's control measured all five pull-shape numbers at a zero delta). **Plus
the third spelling, which after R-A is 4152 indexed walks — 1017 of them the
`__i` family that R-A's own plane is made of.**

⚠ **R-A DID NOT CHANGE THIS VERDICT AND ITS SIZE IS STATED AS A NUMBER RATHER
THAN AS "PARTIAL" (2026-08-15).** One plane crossed from no to yes and it is the
biggest single one measured so far — 149 sites, −149 indexed walks, +149 batch
pulls, no remainder — but 4152 indexed walks remain, the four `.next()` sites
are untouched at 65, and of the thirteen emitter sites the S2j audit enumerated
R-A routed exactly ONE (`emit_simple`, both arms). The twelve that still emit
the indexed walk for a slice param: `emit_find`, `build_phase_frag`,
`step_wrap` ×2, `chain_nest_frag`, `emit_aggregate`'s else arm,
`rel_body_simple_frag`, `chain_body_frag`, and `emit_incremental` ×4 (the last
four DECLARED out of the batch plane by S6-B, so eight are owed). The join sites
are where the wrap's per-scan-site cursor numbering (`__ss<k>`) acquires its
first real consumer — today every call site passes k = 0, because `emit_simple`
scans once.

⚠ ~~the three open planes are named with counts: the build side (3), the
native-source scan + drain prelude (23), the output plane (646)~~ — that
enumeration accounted for **26 of 65**; the missing 39 are the aggregate arm now
in the table above, and no row of the plane table owned them (closing audit §6,
D5). A verdict whose counts do not sum to its own printed population is the
same defect as an unaccounted worklist, one section further on.

⚠ On 646 vs 647. The raw `grep -c 'let mut __out'` answers 647, and ~~"one
binding is not `__out`-classed"~~ is the WRONG REASON (closing audit §6, D4).
All 647 are literally `let mut __out`; the classifier takes every one of them.
The 647th is `let mut __out: String` and it is dropped by the TYPE filter —
`D2`'s regex admits only `Vec|Buffer|HashMap|BTreeMap`. The number is right and
the sentence pointed the next reader at the name classifier when the denominator
filter is what moved. 646 remains the authority for the collection class. ⚠
**RE-MEASURED AT R-A: 651 raw = 650 `Vec` + 1 `String`, the same shape one
fixture-set larger** (`grep -ho 'let mut __out[A-Za-z_0-9]*\s*:\s*[A-Za-z_0-9]*'
| sed 's/.*: *//' | sort | uniq -c` — the derivation that answers the question
directly instead of by subtraction). ⚠ ~~**650** is the authority now.~~

⚠⚠ **RE-MEASURED AT R-B, AND THE SINGLE AUTHORITATIVE NUMBER IS RETIRED
(2026-08-15).** The same derivation now answers **606 raw = 605 `Vec` + 1
`String`**, and there is a second landing beside it: **45 `let mut __rout:`**.
The `Vec` count fell by 45 and `__rout` rose by 45 because R-B1 renamed the rel
one-shot's landing — **the total is 650 on both trees**, and the corpus did not
change (186 fixtures, 170 dumps, both columns). There is no longer one number to
be the authority, and that is the correction: `let mut __out` was never a
population, it was two identities sharing a name plus a homonym on a third plane
(the trama `String`). The authority is now the TRIPLE — **605 query outputs / 45
rel results / 1 template buffer** — and all three are pinned in
`plan_ground_census_gate.sh` (`EXPECT_OUTQ`, `EXPECT_OUTR`, `EXPECT_OUTS`), which
is the first time any of them has been gated at all.

**Criterion 2 has NO GATE OF ANY KIND.** Its instrument is the criterion-1
sweep, which is exempted in `gate_lint.py`'s `NOT_GATES`, so ctest never runs
it: every number in the plane table can drift silently, and this section's own
history — three "yes" rows that did not survive re-measurement — is what that
drift looks like. The exemption itself has no abuse-direction check (§4d).

---

## 3. Criterion 3 — ARC/RC only where needed; BC alone in hot paths

**Operational definition**: *no reference-count operation and no atomic
read-modify-write is reachable from an emitted query's PER-ROW code.* Setup may
hold an `Arc` — the handle IS `{snap: Arc<dyn Snapshot>, ctr_id}` (req. 2 / §7b),
and the pin ladder (§7) is what makes the borrow sound without counting. The
criterion is about where counting HAPPENS.

**Instrument**: [`tests/logos/rc_seam_gate.sh`](../../tests/logos/rc_seam_gate.sh),
registered as `logos_09_rc_seam_hot_path` (tier_full). `objdump -dr` over the
emitted object; each function is its own `.text.<sym>` section, so a relocation
is attributable to its caller.

* **RC operation** = a relocation target matching
  `(Arc|Rc)$G<n>$…__(clone_ref|drop|inc|dec)` or `__rc_(inc|dec)` — the count
  entry points, not merely a type name containing "Arc". *(The old grep could
  not tell a `&Arc<T>` parameter, which counts nothing, from a count.)*
* **Atomic RMW** = a `lock`-prefixed instruction.
* **HOT SET** = the transitive call closure over static relocation edges from
  the emitted query entries (any `N` with a `$N_run__f`; `main` is not an entry).
* **Dispatch-proof second reading**: a closure cannot follow a `dyn` call, and
  the hot path deliberately makes them. So every RC site in the WHOLE object
  must also fall inside the SETUP classifier (`main`, `*__create__*`,
  `*__open__*`, `*create_ctr*`). If nothing outside setup counts at all, no
  vtable can route to a count.

**Why the previous reading did not count (audit F2, CONFIRMED).**
`deem_pipeline_chain`'s object contains zero Arc/Rc — *and so would any program
of its shape*: its source is a bare `&[Row]` and a hand-written
`SeamSrc`/`SeamIter`, never a Memoria handle. Corpus-wide the audit found 77
Arc/Rc symbols, 11 each in exactly 7 fixtures — **precisely the fixtures that
are not chained**. The missing arm it named: *a pipeline whose q1 reads a
container handle*.

**That arm is now `pass/deem_pipeline_handle_seam`** — a container family, `q1`
over `&<typeof(Led) as CtrFamily>::Handle`, its stream consumed by a second deem
with a `limit 3` pull oracle and an unbounded control. Reading:

```
[deem_pipeline_handle_seam] queries=3 entries=12 functions=811 hot-closure=86
[deem_pipeline_handle_seam] RC call sites: object=68  hot=0  outside-setup=0
[deem_pipeline_handle_seam] lock-prefixed RMW: object=0  hot=0
[deem_pipeline_handle_seam] descent symbols in the hot closure: 2
              66  main
               1  logos.gen.Hs…__create__f__ref_Arc$G1$udyn_Snapshot__u64
               1  logos.gen.Hs…__open__f__ref_Arc$G1$udyn_Snapshot__u64
[deem_pipeline_chain] NOT-EVIDENCE arm: RC symbols=0 RC call sites=0 lock RMW=0
```

**The zero is evidence because three arms make it falsifiable**, and the gate
refuses to report it otherwise (exit 2):

* **V1 sensitivity** — the same classifier finds **68** RC call sites in this
  very object (66 `Arc::drop` in the fixture's own `main`, 2 `Arc::clone_ref` in
  the family's `__create__`/`__open__`). A zero measured against 68 is a
  comparison, not an absence of data. *(This arm fired for real during
  development: `__(drop)\b` never matches inside `__drop__g__…` because `_` is a
  word character, so the classifier silently counted zero — V1 caught it.)*
* **V2 subject** — the hot closure must contain a container descent symbol, i.e.
  q1 genuinely walks the container.
* **V3 the refuted arm, checked in the abuse direction** — `deem_pipeline_chain`
  is asserted VACUOUS and labelled NOT-EVIDENCE. If it ever gains a count the
  gate reds, so the audit's refutation cannot be silently inherited again.

**Refusal probe (the gate is a PAIR).** A copy of the fixture whose rel producer
`seam_rows` clones an `Arc<MemoryStore>` reds at **2 sites, on both readings**
(hot closure and dispatch-proof), naming `seam_rows` and both the `clone_ref`
and the paired `drop`. Object-wide sites went 68 → 70 (⚠ corrected by the S6 verify: the recorded 85 was a different perturbation than the sentence describes; the load-bearing half — 2 sites, both readings, seam_rows, clone_ref + paired drop — reproduces exactly) under the perturbation.

**Verdict: criterion 3 holds on this arm**, and for the first time the reading
is about the emitter rather than about the fixture. What it does NOT yet cover:
a pipeline whose SECOND stage also reads a handle (handle → handle), and the
`queued` producer form, which does not exist yet.

**⚠ TWO HALVES OF THIS INSTRUMENT ARE BLIND, AND ONE IS BLIND BY CONSTRUCTION —
the class F2 was raised to close, inside its replacement** (closing audit §6,
C3). The verdict's SUBSTANCE survives both (an independent broader sweep over 12
emitted-query objects and 39 batch-plane objects found zero RC and zero atomic
RMW reachable from any emitted query entry), but the instrument does not earn it:

* **THE `lock`-PREFIXED HALF CAN NEVER FIRE.** `object=0 hot=0` is printed beside
  a zero that IS falsifiable (68 RC sites) and borrows its authority. Measured:
  every `lock`-prefixed instruction in the whole toolchain lives inside
  `logos.lang.atomic.Atomic*::{fetch_*, compare_exchange*}` — 66 in
  `build/lib/logos/liblogos-lang.a`, 0 in `liblogos-mem/lcm/std`. An emitted
  `.o` reaches an atomic only by CALL, so an object-level `lock` scan reports 0
  whatever the emitter does. The V1 ladder makes only the RC-call-site half
  non-vacuous; nothing makes the lock zero falsifiable. Derivation:
  `for a in build/lib/logos/*.a; do objdump -d "$a" | grep -c $'\tlock'; done`.
* **THE RC CLASSIFIER IS PERMISSIVE.** `(Arc|Rc)$G<n>$…__(clone_ref|drop|inc|dec)`
  is called "the count entry points", and `stdlib/mem/sync/arc.logos` exports
  more of them: `clone_arc` and `drop_arc` (the actual `fetch_add`/`fetch_sub`
  sites, `:166`/`:188`), `downgrade`/`downgrade_arc`, `upgrade`, `drop_weak`,
  `arc_from_raw_inner` — none match. A copy of the evidence fixture whose
  per-pull producer calls `s.st.downgrade()` reaches
  `Arc$G1$MemoryStore__downgrade -> __downgrade_arc -> AtomicI32__fetch_add` on
  the per-row path and the gate still reports `RC sites hot=0 outside-setup=0`,
  `EXIT 0 / PASS`. A permissive classifier is invisible to a green corpus by
  construction; the §3 refusal probe pins the `clone_ref` spelling only.
  **Owed: widen `RC` to the export list, then re-run the refusal probe against
  EACH spelling** — a pair per entry point, not one pair for the family.

---

## 4. THE RESIDUAL INVENTORY — one list, walkable

Everything the arc has named and not closed. Each row says where it is recorded
and who owns it; "unowned" is written as unowned rather than left implied.

### 4a. From the S5 audit (10 findings; 6 CONFIRMED, 4 PLAUSIBLE)

| # | status | subject | disposition |
|---|---|---|---|
| F1 | CONFIRMED | `deem` was not a HOT token — 84%-style sampling risk | **CLOSED** at `af17c2fa` (L2 2120 → 2146) |
| F2 | CONFIRMED | the RC instrument is blind by construction | **CLOSED HERE** — §3, `rc_seam_gate.sh` + `deem_pipeline_handle_seam` |
| F3 | CONFIRMED | ADR §12's requirement line still says the pipeline return is `#[borrow_carrying]` and tied to the source args; what landed is an OWNED `Buffer<E>` carrying no borrow | **OPEN** — ⚠ ~~one ADR sentence~~ **TWO SITES** (closing audit §6, C3): `0025-deem-batch-cursor-plane.md:1163` (§12's requirement line) **and `:805-811`** (§7 rung 3's warning block, `-> Result<QStream, ElError>` … "composes into pipelines under rung 1–2 with ZERO counting") — which is precisely the rung criterion 3's verdict leans on. Measured: `QStream` exists in no `.logos`/`.rs` file; `stdlib/mem/stream/buffer.logos:32` says outright "`Buffer<R>` OWNS its `Vec<R>` — it is not `#[borrow_carrying]`"; `pass/deem_pipeline_handle_seam` binds `q1_stream`'s result as an owned `Buffer<(u64,u64)>`. S5's owner, both sites |
| F4 | CONFIRMED | §7 rungs 1–2 "ZERO counting" rests on probes `s5r/p1..p5`: hand-written non-Deem programs over `&[i64]`. They measure that the LANGUAGE admits a borrow-carrying return, nothing about counting in an emitted pipeline | **CLOSED IN SUBSTANCE by §3** (the emitted-artifact reading now exists); the §7 prose still cites the probes — supersede in place |
| F5 | CONFIRMED | the 617-strong query-output class was assigned to S5 and not taken; `let mut __out` 637 before, 637 after; **646 at the audit, 650 at R-A (the control tree answers 650 too — no emitter moved it)** | ⚠ **HALF CLOSED AT R-B, 2026-08-15 — and the halves are two different criteria.** The criterion-1 half (UNOWNED) is **CLOSED**: 650 landings are now 605 `query output` + 45 `rel result` under five named heads with falsifiable grounds, `G4`-witnessed and FACT-J-pinned per fixture and per head; the control tree (pre-R-B, same instrument) still reads them as 650 unowned, so the move is the emitter's. The criterion-2 half is **OPEN, UNOWNED**: the landings are still `Vec`s filled to completion, R-B's control measured this plane's pull shape at a zero delta, and the remaining step is `(b′)` — the `Buffer` inversion, blocked on two missing `Buffer` methods (`len()`, `get(i)`), priced in §6.1 |
| F6 | CONFIRMED | criteria not stated, instruments not in tree | **CLOSED HERE** (this file + two instruments) |
| F7 | PLAUSIBLE | "486 `*_stream`" is stale (490 measured) | **OPEN** — a number in ADR §12 to re-measure or strike |
| F8 | PLAUSIBLE | "PURE ADDITION" is a sub-stage property stated at round scope | **OPEN** — S4's claim, scope to be narrowed in place |
| F9 | PLAUSIBLE | plan and emitter resolve the same rel by two different lookups (`rel_find_name` vs `rel_find_src`) | **OPEN** — a divergence hazard with no gate |
| F10 | PLAUSIBLE | the pure-aggregate-over-a-row-producer arm has ZERO corpus executions; its only oracle is a sandbox file | **OPEN** — needs a corpus fixture or an honest deletion |

### 4b. Owed by the ADR itself

| where | subject | owner |
|---|---|---|
| §6 / S6-A | ~~**the fourth pull site** — `rexpr_walk::build_phase_frag` pulls rows; any batch source on a join build side dies there~~ **LANDED (R-D, 2026-08-15)**: a fourth arm selected by the SAME two-term test the three collapsed sites use (`streams && rel_batch`); fires exactly once corpus-wide (`pass/deem_batch_build_side_join`, the today-dying probe of the refuse-matrix); answer diff 0 pre-existing rows moved; the head is classified NOMAT (it builds nothing the row spelling did not) — classified, NOT count-pinned | CLOSED — R-D |
| §6 / S6-A | the `WritWalk` CURSOR: **R-D landed the cursor** (`writ_graph.logos`: recursion→explicit frame stack, owned seen set, `WG_BATCH_CAP` boundary, `#[borrow_carrying]`, Rewind=RESTART; 8-column differential vs the Vec producer, `pass/wql_writ_walk_cursor`) — **landed-but-UNCONSUMED: no `rel` declares over it, zero gen dumps, invisible to every arc instrument by construction**. The rel-declaration route is REFUSED with a measured ground (writ_graph.logos REFUSAL block: +3 mat nodes same-population / +17,048 bytes, D2 −21 and accounted +0.45pp on the same build — the blanket `MG_REL_BLOCK` drain + 9 by-name `rel_srcs` read sites whose partial landing = silent wrong answers). What remains owed: the plan must say "no copy — READ IT AGAIN" (a re-walk capability distinct from Rewind-in-place) AND the 9 read sites must take the re-pull spelling TOGETHER | REFUSED-WITH-GROUND — R-E or later |
| §12 | the `direct` stream form — ⚠ **THE RECORDED BLOCKER WAS FALSIFIED AT R-E, TWICE**: the typeof-in-module compiler fix LANDED (sema.cpp typeof arm → demand + `pass/typeof_container_hand_written_state`), AND `direct` never depended on it (the emitter spells the concrete walk type as TEXT from `MacroParams`). The TRUE blocker, found by landing the precondition: `direct`'s state struct holds the emitted `<C>LeafWalk` as a FIELD, and `sema_abi_layout` declines a metaprog-emitted struct field asked from a foreign package (returns {8,8}; `LOGOS_VERIFY_LAYOUT` aborts — sema 8 vs DataLayout 72; the class is named at `sema.cpp` `find_struct_repr_` comment, never reached because no emitted struct in the corpus holds an emitted struct as a field — a permissive defect invisible to a green corpus, third instance). ⚠ AND THE WIN IS SMALLER THAN PRICED: seam-1 is 1 packet at 489 of 502 sites BECAUSE THE SOURCE IS 1 PACKET (SliceStream/no-stream), which `direct` cannot change; the measurable effect is 13 container-walk sites (⌈leaves⌉ packets); the eligible range 276–315 of 606 is a criterion-1 PESSIMIZATION (~−1.2 to −1.4 pp accounted, both sides shrink) | compiler task (#61-class) first; then emitter |
| §12 | the `queued` form + reqs 7–8 (lazy producer with kill-on-drop; borrows through the producer) | PARKED by Victor 08-11 |
| §10 / S6-B | the DBSP consumption seam = `<q>_apply`'s parameter list ALONE (⚠ ~~70/70~~ **71/71** `_apply` fns take a list textually identical to their batch fn's — the plane ADR `:1095` already carried the re-measure 71/103/89 and this file did not; 124 DRed phase fns take `&[…]` with sign fixed by PHASE) | the stage that changes the incremental surface |
| §10 / S6-B | ⚠ **the 124 was CHALLENGED as not re-derivable and the challenge is REFUTED** — recorded so it does not recur. 124 is the four PHASE suffixes, not all `_scc` definitions: `_od` 31 + `_odp` 31 + `_cpt` 31 + `_i` 31 = 124. (`grep -ho 'fn __wql_[A-Za-z_0-9]*_scc[0-9]*[A-Za-z_0-9]*' /tmp/c1/*.user` gives 214 definitions / 123 unique names / suffix split `""` 68, `_dred` 22, and the four phases at 31 each — 214 and 123 are different quantities and neither is the claim.) | closed |
| §7 / S5 | **the pin ladder's residency half has NO CALLER.** §7 states in the present tense that "batch borrow = pin; Drop/advance = unpin" and that both pin parts are methods on the generalized `dyn Snapshot` "so the emitted query code is one CODE, store-agnostic"; `stdlib/mem/bt/map.logos:115-134` repeats it at the declaration. Measured (closing audit §6, C3): `snap_pin` / `snap_unpin` / `leaf_pin` / `leaf_unpin` have ZERO call sites anywhere in the repository outside their own declarations — the emitter emits no pin call, so a disk store's override would never be invoked. The arc records this absence only inside S5's "the OWNED CURSOR is refused for S5" bullet ("its interface and no caller"), scoping it to rung 3 — but `leaf_pin` is declared per-LEAF on the BORROWED path, rungs 1–2, so the measured gap is WIDER than the recorded one | **OPEN, UNOWNED** — either a caller on the borrowed path or the §7 sentences moved to future tense |
| §3, §10, §11 | three `[REC]` forks, each one paragraph to reverse: capability-existence-by-trait; the weight NOT in the core row model; vocabulary at lang tier + hub traits out of `logos.mem.bt.map` | open by design |
| §1 | S2's own "STILL OWED": the drain arm becoming `Drain -> Buffer` end to end | S2 successor |
| §S3 / `access_plan.logos:187-205` | **A LYING `order` DECLARATION IS UNCHECKED.** `order <rel> = <c>;` elides the Sort node on the SOURCE'S WORD — admitted "only because the producer's return type implements `OrderedBy`", which is a TYPE fact about the producer, not a fact about the rows. Nothing in the tree verifies that the rows really do arrive in `<c>` order (`is_sorted` exists only inside `bt/pack.logos` and `bt/btfl.logos`, over buffers, never on this path). A wrong declaration therefore yields an unsorted answer to an `order by` query with no diagnostic, and the grounds `MG_ORDERED_SOURCE` / the desc form make the ELISION visible while leaving its PREMISE unmeasured. The absence is now a ground rather than a silence — the premise is still an unchecked assertion | **OPEN, UNOWNED** — a debug-tier verification on the first batch, or the declaration documented as trusted-by-contract at its site |
| §2 | selection vectors, columnar build side — declared axes, not v1 | out of v1 |

### 4c. Compiler residuals (tasks #50 / #51)

| task | subject | state |
|---|---|---|
| **#50** | **D3 miscompile** — a struct pattern binding a `&mut` aggregate field in a by-value match; a method call through the binding returns garbage | OPEN, not started |
| **#51** | documented D1 residuals off the Deem↔Memoria coupling: the slice §B6 store-side pair (R1/R2), the r11 imported-NLL labeled-loop over-refusal, the P2 extra-diagnostics noise | OPEN, D1 closed OVER them by its own closure criterion |
| — | `static mut` + a family deem + a native-source deem in ONE module ⇒ `logosc-metaprog: jit add_module: Duplicate definition of symbol`. Recorded at `pass/deem_order_desc_elision`; met again by `pass/deem_pipeline_handle_seam`, which works around it with a `*mut i64` counter | OPEN, metaprog/JIT, unowned |

⚠ **NEITHER #50 NOR #51 IS DERIVABLE FROM THIS TREE — the entry-ticket test,
failed by this file's own §4** (closing audit §6, C3). This document opens with
"Nothing here is inherited from a commit message", and both rows are inherited
from the TASK TRACKER, which is not a tree source either. Measured: repo-wide,
`imported-NLL`, `store-side pair` and `extra-diagnostics` occur ONLY in the #51
row itself; the census's own P2 is "the out-param deposit chased through the
reborrow edge" (unrelated to diagnostics noise) and its r11 entries are the
X0/X1/X2 loop-exit collector fixtures, none an imported over-refusal. #50's D3
miscompile has no repro, no fixture and no bug doc anywhere in the tree — only
that row. So "each still exactly as documented, no wider" is UNANSWERABLE here
for both. **Owed before either is worked: a tree-resident statement — a
`tests/logos/fail/` or `pass/` repro for D3, and for #51 the three subjects
named at their sites — or the rows must say out loud that their content lives
outside the tree.** They are recorded, not struck: the tasks are real; it is the
CLAIM of tree-derivability that is false.

### 4d. Instrument-side, found while landing this ticket

| subject | state |
|---|---|
| `gate_lint.py`'s `NOT_GATES` exemption has **no abuse-direction check**: an entry buys silence from R5-unregistered-gate, and nothing asserts that its key names a file that exists or that ctest really does not invoke it. R5's own selftest checks the exemption HOLDS (a declared reporter is not flagged), never that it is honest. This ticket used the exemption (for the criterion-1 instrument, with its ground) and so is the natural place to record the hole | OPEN, unowned |
| the criterion-1 instrument's ACC table maps binding-name classes to owner nodes; the pairings are EXACT for four (`__ks` 127 = `key vector` 127, `__ga_*` 208 = `accumulator` 208, `__g_cnt` 13 = `group count` 13, `__g_row` 7 = `representative row` 7) and not for two (`__hm` 588 vs `arrange` 598; `__sv` 497 vs `materialize` 217). The deltas are printed, not explained — a per-node attribution is the next honest step, and until it exists "accounted 52.79%" is a class-level reading, not a site-level one | OPEN |
| **ACC's key is the NAME, and one name can carry two owners.** 12 `Buffer`-typed `__rel_*` bindings are owned (`drain` 7 + `sort` 5, FACT B) while 205 of the 217 `__rel_*` bindings are unowned `Vec` landings; a name-only key must take all or none. The printed ACCOUNTED is therefore a FLOOR (1911), the site-level reading is 1923. The fix is the per-node attribution above, not a wider name key — widening it would over-account by 205 | OPEN |
| ⚠⚠ **THE TWO-OWNER CLASS WAS NOT ONE CLASS EITHER — R-C3 CLOSED HALF OF IT BY RENAME, AND THE OTHER HALF IS A STANDING REFUSAL.** The row above states the defect for `__rel_*`; R-C found a SECOND instance of the same shape and closed it. Bare `__rs` (45, the `_run` rel helper's dedup set) shares a name prefix with `__rs_<m>` (218, the fixpoint member's novelty shadow set) — a different role in a different emitter region. R-C2 measured this BEFORE taking the credit and refused the sixth of its six heads in writing rather than over-crediting 45; R-C3 then renamed the `_run` set to `__rds` (R-B1's method, one layer down) and both classes took clean owners, 263 bindings in two rows. **The remaining `__rel_*` 217 is REFUSED, not deferred: re-measured on this tree as 12 `Buffer` + 205 `Vec`, exactly as recorded.** A name key cannot hold two owners, so ACC credits NEITHER and all 217 sit in the worklist — the printed ACCOUNTED is a FLOOR by 12. The ground for the refusal is now demonstrated rather than asserted: R-C3 is the worked example of the rename that closes it | **HALF CLOSED (R-C3); `__rel_*` REFUSED with a recorded ground** |
| ⚠⚠ **CLOSED BY R-B, 2026-08-15 — `ACC` CREDITED CLASSES WITHOUT CHECKING THAT THE OWNING NODE EXISTED, AND A CONTROL MEASURED WHAT THAT WAS WORTH.** `ACC` mapped a binding-name class to a PROSE STRING; `acc_n` summed the classes whose name was a key. Nothing connected the credit to the trace channel, so `accounted` moved when the TABLE moved on a tree where nothing had changed: swept on the pre-R-B tree with the post-R-B table, the instrument printed `accounted=2569 (70.73%)` — identical to R-B — crediting 650 `__out` bindings to `query output` while all four query-output heads read a fire count of **0 on that same sweep**, three lines above. This is the permissive-defect shape: nothing was ever red, because a self-certifying ledger cannot fail. **FIXED: `ACC` now names the OWNING HEAD, the head must be a key of `MAT`, and a class whose owners all read zero is REFUSED (`G4`) — printed under CLAIMED BUT UNWITNESSED and counted in the worklist.** Both trees were re-swept after the fix and now read 1919 (52.84%) and 2569 (70.73%), a genuine tree comparison. ⚠ The FLOOR caveat in the two rows above is NOT retired by this — G4 proves the owner fired, not that the counts correspond site for site | **CLOSED (R-B)** |
| **criterion 2 has no gate at all** — its instrument is the criterion-1 sweep, exempted in `NOT_GATES`, so nothing in ctest reads the plane table. The closing audit re-measured it and three "yes" rows changed. A gate is not obviously right here (the counts are corpus-size-dependent, §1's argument), but the ASYMMETRY is recorded: criterion 1 has three non-moving properties gated inside its script and criterion 2 has none identified | OPEN, unowned |
| **the `lock`-prefixed half of `rc_seam_gate.sh` is vacuous by construction** (§3): every `lock` in the toolchain is inside `logos.lang.atomic.Atomic*`, reachable from an emitted object only by CALL, so the object-level scan cannot fire. Owed: classify the atomic ENTRY POINTS as edges (like RC), or stop printing the zero | OPEN |
| **the RC classifier admits count entry points it does not name** (§3): `clone_arc`, `drop_arc`, `downgrade(_arc)`, `upgrade`, `drop_weak`, `arc_from_raw_inner`. A probe reaching `AtomicI32__fetch_add` per pull through `downgrade` PASSES the gate. Owed: the export list, plus one refusal pair per spelling | OPEN |

### 4e. Census-side

The census pin (`docs/deem-interpreter-deletion-census.md` §9) is the machine
half and is checked by `logos_00_census_pin`; the registry baseline moves with
every registration and is predicted-then-measured per slice. This ticket's
registrations: `pass/deem_pipeline_handle_seam` + `logos_09_rc_seam_hot_path`,
predicted +2 ALL / +2 `-LE imported` / +0 tier_commit.

**The closing audit (§6) registers NOTHING**: it changes this file, and it adds
two entries to the criterion-1 instrument's ACC table — no fixture, no ctest
test, no gate. Predicted census movement +0 / +0 / +0, and the pin was walked to
measure that prediction rather than to assume it.

---

## 5. Re-running all of it (the corpus-snapshot control's tree-resident route: the promoted criterion-1 instrument IS a whole-corpus sweep — run it on two trees and `cmp` the dumps; the sandbox scripts are convenience, not the record)

```
cmake --build build -j$(nproc)                                   # instruments read the BUILD
tests/logos/criterion1_materialization_instrument.sh /tmp/c1     # criteria 1 and 2
tests/logos/rc_seam_gate.sh build/bin/logosc tests/logos/pass    # criterion 3
cd build && ../tests/logos/test-levels.sh L2                     # the gate
```

**The one-variable control, which is how §2's before/after column was taken**
(R-A, and the pattern for every stage that claims to move an emitted-shape
count). Two sweeps of the SAME instrument over the SAME corpus, differing only
in the emitter predicate under test:

```
# CONTROL: force the routing predicate off, rebuild, sweep
#   stdlib/mem/wql/rexpr_walk.logos :: slice_stream_src  ->  `return false;`
cmake --build build -j$(nproc) && tests/logos/criterion1_materialization_instrument.sh /tmp/ra_ctl
git checkout stdlib/mem/wql/rexpr_walk.logos            # RESTORE, then rebuild again
cmake --build build -j$(nproc) && tests/logos/criterion1_materialization_instrument.sh /tmp/ra_final
```

⚠ The restore-and-rebuild between the two is not optional bookkeeping: the
instrument reads the BUILD, so a sweep taken against an un-restored tree is a
measurement of the control twice. The criterion-1 SUMMARY line is the check that
the pair is honest in the other direction too — it must be IDENTICAL across the
two trees for a change that materializes nothing, and for R-A it is.

⚠⚠ **AND THE SUMMARY LINE ALONE IS NOT ENOUGH — R-B ADDS TWO STEPS, EACH BECAUSE
A DEFECT GOT THROUGH THE PREVIOUS RECIPE (2026-08-15).**

```
# (i) THE ANSWER DIFF — the values, which no artifact or trace channel can see.
#     D4 was a wrong-answer miscompile through which every number on both
#     channels was unchanged. Run on BOTH trees, diff the two files:
tests/logos/answer_diff_instrument.sh /tmp/x_ctl     # on the control build
tests/logos/answer_diff_instrument.sh /tmp/x_final   # on the restored build
diff /tmp/x_ctl/answers /tmp/x_final/answers         # 0 lines = no answer moved

# (ii) BOTH SWEEPS WITH THE SAME INSTRUMENT REVISION. If the stage also edits
#      the instrument (a new ACC key, a new MAT head), the control must be
#      re-swept with the EDITED script — otherwise the columns differ by two
#      variables and the ledger's movement is attributed to the tree.
```

⚠ **(ii) is not hypothetical: it is exactly how R-B first mis-stated its own
result** (`accounted +650`, when the tree-vs-tree delta was zero and the whole
movement was the table). The instrument-side repair that makes the mistake
loud rather than silent is `G4` — a credit whose owning head has a zero fire
count is refused — but the recipe step is what makes the pair a comparison in
the first place. **A ratio from two separately-measured columns is not a
comparison, and an instrument edited in the same stage is a second variable.**

---

## 6. THE CLOSING SECTION — the arc's verdict, the priced remainder, and the STOP

Written after the closing audit (three phases, adversarial toward this arc's own
records, everything derivable FROM THE TREE ALONE). Its corrections are landed
above, in place, beside the sentences they supersede; this section states the
result and stops. **Every round of this arc that was audited had an overclaim in
it — the record is §4a's ten findings and the ⚠ strikes now standing beside the
sentences of §1–§3. This round is no exception, and its own overclaims are §6.4:**
the headline one sat in the paragraph whose whole purpose was to disown an
unreproducible number.

### 6.1 Criterion 1 — no intermediate materialization: **NOT MET**

There was no stated verdict to confirm or refute — §2 and §3 each end in a bold
"Verdict"; §1 never did. Against the definition this tree fixes ("every
compiler-inserted materialization is a NAMED PLAN NODE WITH A GROUND", and the
instrument's own "the unaccounted classes ARE the criterion-1 worklist — that
list is the honest reading"):

* `D1` 4036, `N1` 1334 (33.05%), `T` 615, `D2` 3620, **accounted 1911 (52.79%)**
  by the printed name-key reading, **1923 (53.12%)** site-level.
* ⚠ **RE-MEASURED AT R-A (2026-08-15), AND THE DISSOLVE MOVED CRITERION 1 BY
  ZERO.** Today: `D1` 4042, `N1` 1336 (33.05%), `T` 617, `D2` 3632, **accounted
  1919 (52.84%)**, worklist **1713**. Every one of those deltas is the three new
  registrations, not the emitter — **the control tree (`slice_stream_src` forced
  to `return false`, rebuilt) prints the identical SUMMARY line, class for class:
  `N1/D1=1336/4042=33.05% T=617 D2=3632 accounted=1919 (52.84%)`**, and the
  worklist buckets are identical too (`__out` 650, `__cp` 145, `__tt` 70,
  `__nd_*` 222 + `__dl_*` 176, rest 450). That is the control for R-A's claim
  about itself: it changed the SCAN SHAPE and materialized nothing — no new
  `Vec`, no new `Buffer`, no new worklist class. `SliceStream` borrows the rows
  it walks. The fixture-side move is `__out` 646 → 650 and `__ks`/`__ix`/`__sv`
  +2/+2/+4 (one sorted, materialized query added to the corpus), so the
  arithmetic below reads 650 + 543 + 70 + 450 = 1713.
* **1697–1709 of 3620 emitted collection bindings have no named owner.** Of
  those, 646 `__out` are UNOWNED by this file's own F5 row; 543 (`__cp` 145 +
  `__nd_*`/`__dl_*` 398) are OWNED by declaration-out-of-plane (S6-B); 70 `__tt`
  are half-assigned (S6-A took the Writ half only); **450** across the remaining
  classes are written "un-triaged" (646 + 543 + 70 + 450 = 1709). **The label
  "partially met with an owned remainder" is REFUTED by this file's own records:
  1,096 bindings — 30.3% of `D2` — are neither named nor owned (646 `__out` +
  450 un-triaged), and 1,166 (32.2%) counting the half-assigned `__tt`.**
  (At R-A: **1,100 = 30.3% of 3632**, and 1,170 = 32.2% with `__tt`. The
  percentages are unmoved to the decimal — the corpus grew, the ownership did
  not.)
* ⚠⚠ **RE-MEASURED AT R-B (2026-08-15), AND THIS TIME THE CRITERION MOVED — BY
  ITS LARGEST STEP OF THE ARC, AND IT IS STILL NOT MET.** `D1` 4692, `N1` **1986
  (42.33%)**, `T` 617, `D2` 3632, **accounted 2569 (70.73%)**, worklist **1063**.
  Unlike the R-A entry above, these deltas ARE the emitter: the control tree
  (`rexpr_walk.logos` at `93295e0c`, rebuilt, swept with the SAME instrument,
  restored after) reads `N1/D1=1336/4042=33.05% T=617 D2=3632 accounted=1919
  (52.84%)`, worklist 1713 — so R-A's "identical SUMMARY line" result is exactly
  what R-B is NOT. **+650 named nodes, −650 worklist, `D2` and `T` and every
  criterion-2 pull-shape number at a zero delta, and the corpus-wide answer diff
  byte-identical.** The arithmetic below reads 543 + 70 + 450 = 1063: the `__out`
  row has left the worklist.
* ⚠⚠⚠ **RE-MEASURED AT R-C (2026-08-15). THE POPULATION WAS WRONG, AND FIXING IT
  MADE EVERY NUMBER ABOVE WORSE BEFORE ANYTHING WAS NAMED.** `D2`'s type filter
  was `Vec|Buffer|HashMap|BTreeMap`. The dumps contain, by type: Vec 3028,
  HashMap 592, **HashSet 319**, Buffer 12, BTreeMap 0 — and 3028 + 592 + 12 =
  3632 exactly, so the miss was clean rather than partial. **319 emitted HashSet
  collections were counted by NO instrument in this tree** — not `D2`, not any
  `ACC` class, not any census FACT. They are the fixpoint plane's NOVELTY
  structures (`__rs_<m>` 218, `__os_<m>` 46, bare `__rs` 45, `__hs` 10), one
  hash-set entry per derived row, which is a compiler-inserted materialization
  under any reading of this criterion. **This is §6.4's D4 shape recurring — "a
  right number with a wrong reason: the TYPE filter dropped it" — now at 319×,
  the second time in this arc** (new entry in §6.4 below).
  * **R-C1 landed the population fix ALONE, first, with nothing else in it**, on
    the rule that *a criterion cannot be closed on the same commit that fixes the
    population it is measured over*. Predicted then measured: `D2` 3632 →
    **3951**, accounted 2569 **unchanged**, **70.73% → 65.02%**, worklist 1063 →
    **1382**. The printed 70.73% was a share of a population that excluded the
    class; it is superseded here and at §1, §6.1's earlier readings, and the §4d
    FLOOR rows.
  * **R-C2 named the fixpoint plane — 670 bindings in ONE emitter region
    (`_scc`/`_od`/`_odp`) that had no plan node at all.** S6-B's "declared out of
    the BATCH plane" was read for two rounds as a naming exemption; it is not
    one. **That declaration is a statement about what CONSUMES these rows; this
    criterion names what EXISTS regardless of which plane consumes it.** Six
    heads, fire counts measured at the emitter BEFORE the nodes existed and every
    one landing on prediction: `fixpoint derived frontier` 222, `fixpoint novelty
    set` 218, `fixpoint frontier` 176, `over-deletion set` 46, `fixpoint novelty
    lattice` 4, `fixpoint lattice key roster` 4.
  * **R-C3 paid R-C2's recorded debt by the R-B1 method.** R-C2 landed five of
    its six credits and REFUSED the sixth in writing: a name key `__rs` would
    also have matched the 45 bare `__rs` bindings (the one-shot rel helper's
    dedup set — a different role in a different region), which is the `__rel_*`
    two-owner defect repeated knowingly. R-C3 renamed the `_run` set to `__rds`,
    gave it its own head (`rel dedup set`, 45, 1:1 with `rel result`), and the
    263 bindings that were one ambiguous row became two owned rows.
  * **R-C1 also closed a class for free, with no emitter change at all.** `__hs`
    (10) is `hash_build_frag`'s SET form — the same site and the same `arrange`
    node as `__hm`, which `plan_ground_census_gate.sh` FACT C has always counted
    (`__(hm|hs|bt)`, 598 == 598). This instrument read 588 against an `arrange`
    fire count of 598 and the 10-line gap sat in the worklist looking unowned.
    **It was never unowned; it was untyped.**
  * **NET, ON THE CORRECTED POPULATION, AND THE TWO "BEFORE" FIGURES ARE
    DIFFERENT QUANTITIES — say which:** R-C1's population fix alone, before the
    `__hs` credit, reads accounted **2569 (65.02%)**, worklist 1382; the CONTROL
    TREE measured with the FINAL instrument (`9395c3d1` emitter, everything else
    this tree, rebuilt and swept — §1's table) reads **2579 (65.27%)**, worklist
    **1372**, because `__hs`'s 10 are owned on both trees. The one-variable
    control is the second pair; the first is R-C1's own before/after. **R-C:
    `D1` 5407, `N1` 2701 (49.95%), `T` 621, `D2` 3951, accounted 3294 (83.37%),
    worklist 657 — Δ against the control +715 named, −715 worklist, `D2` at
    ZERO.** The instrument exits 2 on the control (G4 refuses all seven R-C
    classes, `__rds` at 0 bindings) and 0 on this tree; the restore was
    re-measured to the same 2701/5407 and 3294 before anything else was run.
    The corpus-wide answer diff is byte-identical to HEAD across all 186 fixtures
    for the whole round, and the R-C2 emitter change was additionally proven
    artifact-identical (170/170 user dumps byte-for-byte) because it writes only
    to the trace channel. **Criterion 1 is still NOT MET**: 657 bindings have no
    named owner — `__cp` 145, `__tt` 70, `__nw`/`__wcd` 88, `__odv`/`__rmv` 70,
    `__pres`/`__lt`/`__ecp` 66, `__cv` 1, and the `__rel_*` 217 (of which 12 are
    owned and uncreditable, see the refusal below). 145 + 70 + 88 + 70 + 66 + 1 +
    217 = 657, in 61 name classes; the instrument prints only the top 20, so the
    tail is 41 `__rel_*` names of 1–7 bindings each and nothing else.
* ⚠ **AND THE 52.84% → 70.73% IS ONLY A COMPARISON BECAUSE R-B FIXED THE
  INSTRUMENT FIRST.** The `accounted` column used to sum a hand-written
  name→prose table with nothing tying a credit to the trace; measured on the
  control it awarded 650 bindings to four heads whose fire count was zero. `G4`
  now refuses an unwitnessed credit, and §1 carries the control that found it.
  **The first version of this stage's own record claimed `accounted +650` as an
  emitter result when the tree-vs-tree delta was ZERO — the thirteenth
  consecutive round-overclaim, caught by the control this arc's rules require.**
* **What IS met, and should be said:** the TRACE channel is complete. Every
  `[plan]` head is classified (G2 gates it), no rel reports a `materialize`
  verdict without naming a node, and all named identities check against the
  artifact per the census gate's FACT B/C/E/G/H — **and now FACT J, which is the
  first of them to cover the output plane at all.** The gap is in the ARTIFACT
  channel, and it is a gap of OWNERSHIP, not of visibility.
* **Neither named nor owned: ⚠ ~~1,100 (30.3% of `D2`)~~ → 450 (12.39%)**, and
  520 (14.32%) counting the half-assigned `__tt`. That is the honest headline of
  R-B: the largest unowned class in the arc was 650 bindings and it is now
  named, gated per fixture and per head, with a falsifiable ground. **It is not
  MET, because 450 bindings are still un-triaged and 543 more rest on a
  declaration rather than a node** — and because naming a landing is not
  removing it, which is `(b′)`'s job and `(b′)` is not landed.

**Owned remainder, ⚠ RE-DERIVED AT R-C ON THE CORRECTED POPULATION (the `__out`
row was retired at R-B; the fixpoint rows are retired here). The arithmetic
closes on the measured worklist and every term is a class the instrument prints:
`__rel_*` 217 + `__cp` 145 + `__tt` 70 + the 8-class rest 225 = 657, in 61 name
classes.** ⚠ The R-B version of this table closed at 1063 over `D2` 3632; both
numbers are superseded — see the §1 control table for why 1063 and 657 are not
subtractable.

| class | size | next step | owner |
|---|---:|---|---|
| ~~`__out`, the query-output Vec — 650~~ | **0** | **RETIRED AS AN OWNERSHIP ROW BY R-B.** The 650 landings are now 605 `query output` (four heads) + 45 `rel result`, each a named plan node with a ground, pinned per fixture and per head by FACT J. ⚠ **The row leaves criterion 1's worklist and does NOT leave criterion 2's**: the landings still exist and are still filled to completion. What is left of it is `(b′)`, below | **CLOSED (R-B)** — was S5's, never taken (F5) |
| `(b′)` — the Buffer inversion of the output plane | 605 + 45 | the body builds a `Buffer` and the `Vec` entry becomes `into_vec()`. ⚠ **BLOCKER REFUTED (R-C, by execution — see §the `(b′)` paragraph)**: `Buffer::as_slice` (`buffer.logos:99`) returns `&[R]`, which carries `len()` and indexing, so **ZERO Memoria-side methods** are required; the spelling is type-agnostic (`p2.logos` rc=0 on `Vec`), so the 381 `let __out: &mut Vec<…>` aliases do NOT move with the swap. Measured price: **14 emitter read sites** on `__out`. **Still priced OUT this round — unblocked, unlanded.** | **UNOWNED** — criterion **2**'s output row (R-C's neighbour) |
| `__tt`, fixpoint temporaries | 70 | **the fixpoint plane**: S6-A took the Writ half; the buffer half is untouched | S6 successor |
| ~~`__cp`, `__nd_*`, `__dl_*` — 543~~ | **145** | ⚠ **THE ROW WAS THREE THINGS AND THE DECLARATION COVERED NONE OF THEM.** `__nd_*` (222) and `__dl_*` (176) are NAMED as of R-C2 — they leave this row for the ACC table, and with them go `__rs_*` 218, `__os_*` 46, `__best_*` 4, `__keys_*` 4, none of which this row ever listed because the instrument could not see a `HashSet` (R-C1). What is LEFT is `__cp` alone, 145. **S6-B's declaration is retired as an argument here, not satisfied**: it says which plane CONSUMES these rows, and criterion 1 asks what EXISTS | `__cp` UNOWNED — **R-D/R-E**; the incremental naming is DONE |
| the rest | ⚠ ~~450~~ **225**, in 8 classes | **triage**, class by class, into owned-or-admitted. Re-derived on the corrected population, and it is the WHOLE of it: `__wcd` 44, `__nw` 44, `__odv` 35, `__rmv` 35, `__pres` 22, `__lt` 22, `__ecp` 22, `__cv` 1 = 225 (`__cp` 145 and `__tt` 70 have their own rows above). ⚠ **`__rel_*` is NO LONGER in this bucket**: it is 217 in 51 name-classes and it is REFUSED, one row down, on a stated ground | UNOWNED — **R-D/R-E** |
| `__rel_*`, the two-owner class | 217 (12 `Buffer` + 205 `Vec`) | ⚠ **REFUSED, NOT DEFERRED, and now with a worked example against it.** 12 are OWNED (`drain` 7 + `sort` 5, the identity FACT B pins) and 205 are unowned `Vec` landings under the same name key; ACC cannot express two owners so it credits NEITHER, and the printed ACCOUNTED is a FLOOR by 12. R-C3 is the demonstration of the fix — rename at the emitter, then name — applied to the OTHER instance of this exact shape (`__rs`/`__rds`, 263 bindings) | UNOWNED — the stage that renames |
| per-node attribution (`__hm` 588 vs `arrange` 598; `__sv` 501 vs `materialize` 217; the 12 two-owner `__rel_*`) | — | site-level accounting, which also retires the FLOOR caveat. ⚠ **G4 makes this sharper, not softer**: it proves the owning head FIRED, it does not prove the counts correspond site for site — `__sv` 501 against 217 `materialize` lines is still an unreconciled 2.3:1, and FACT J is the only class where the correspondence is actually pinned | §4d |

### 6.2 Criterion 2 — full algebra integration with batch + cursor: **NOT MET**

Direction confirmed and unchanged. **SIZE substantially understated by the
instrument**, and three plane rows did not survive re-measurement (§2, in place).

⚠ **SUPERSEDED IN PLACE BY R-A, 2026-08-15 — the verdict does NOT change, one
plane and three numbers do.** The audit-tree reading is kept below the R-A
reading, because the pair is the argument: a criterion that moves by its largest
single step so far and is still NOT MET is priced honestly only when both
columns are visible.

**R-A reading (this tree, control-separated — §2's table):**

* Pull shape: **164** `next_batch()` in **53** dumps; **65** `.next()` in 27
  dumps, UNMOVED — 3 build side, 14 native scan, 9 drain prelude, **39
  aggregate key enumeration**. R-A retired none of the four row-pull sites.
* **Plus 4152 indexed per-row walks** (down 149 from the control's 4301, one per
  wrap), **1017** of them the `__i` family (down 149 from 1166). The batch-pull
  inner loop is its own spelling, now 164 sites, still exactly one per
  `next_batch()`.
* Planes: scan/container **yes**; scan/**slice parameter** **YES — NEW, 149
  sites**; scan/native **no** (14, and `SliceStream` does not reach it);
  drain prelude **no** (9); join build side **no** (3); join probe **no** (the
  11-of-81 dumps are the scan plane, not the probe); sort **input yes / key
  vector no**; aggregate **the fold yes, the key enumeration no** (39, 14
  dumps); output **no** (⚠ ~~650~~ **605 + 45, and NAMED as of R-B — but naming
  is criterion 1; this plane's answer is unchanged and was control-measured at a
  zero delta**); incremental **no, declared**.
* **What is left, as a number and not as an adjective: 4152 indexed walks, and
  12 of the S2j audit's 13 emitter sites unrouted (8 owed, 4 declared out).**

**Audit reading (33a32b01), kept for the pair:**

* Pull shape: 15 `next_batch()` in 9 dumps; 65 `.next()` in 27 dumps — 3 build
  side, 14 native scan, 9 drain prelude, **39 aggregate key enumeration**.
* **Plus ⚠ ~~3975~~ 4293 indexed per-row walks** (`while (<minted> < X.len())`,
  both paren spellings — see §2's ⚠ on the reader) that neither instrument grep
  can see, ⚠ ~~1160~~ 1162 of them the `__i` family — the base scan of every
  `&[Row]` source. The batch-pull inner loop is its own spelling
  (`while (__bjN < __bnN)`, 15 sites). **The instrument saw 80 sites of a
  per-row population of at least 4358.**
* Planes: scan/container **yes**; scan/native **no**; drain prelude **no**; join
  build side **no** (3 sites, the fourth pull site); join probe **2 of 81
  dumps**; sort **the elision arm only** (2 dumps; the key-vector arm is 54
  dumps / 319 `__ix`); aggregate **the fold yes, the key enumeration no** (39
  pulls, 14 dumps); output **no** (646); incremental **no, declared**.

**Owned remainder, with its named next step:**

| gap | next step | owner |
|---|---|---|
| the output plane (⚠ ~~646~~ ⚠ ~~650~~ **605 + 45**) | ⚠ **R-B LANDED AND THIS ROW DID NOT MOVE — the half it took was criterion 1's.** The landings are now NAMED (five plan nodes, FACT J) but they are still `Vec`s filled to completion, and R-B's control measured this criterion's five pull-shape numbers at a **zero delta** (`.next()` 65, `next_batch()` 164, indexed walks 4152, `__i` 1017, `SliceStream::<` 149 — both trees). The remaining step is `(b′)`, the **Buffer inversion**: body builds a `Buffer`, the `Vec` entry becomes `into_vec()`. ⚠ **the recorded blocker is REFUTED (R-C, proven by running it)**: `Buffer::as_slice` returns `&[R]` — `len()` and indexing come with the slice, **zero Memoria-side methods**; and the spelling is type-agnostic across `Vec`/`Buffer`, so the 381 `let __out: &mut Vec<…>` aliases do NOT move with it. Price re-measured as **14 emitter read sites** (13 limit guards + 1 distinct rescan). **Priced OUT this round: unblocked, unlanded** | UNOWNED — R-B took the naming half; `(b′)` is R-C's neighbour |
| the build side (3 sites) | ~~`build_phase_frag` must take a batch source~~ **LANDED (R-D)** — the batch-pull arm exists and fires (1 corpus firing, its fixture); the WritWalk cursor that motivated it is landed-but-unconsumed (Stage 3 refused, see §6/S6-A row) | CLOSED — R-D |
| native scan + drain prelude (23) | ⚠ ~~**`SliceStream`** — one batch producer over a `&[Row]`, which retires the native-source `.next()` and the drain prelude's row pull together~~ — **REFUTED BY LANDING IT.** `SliceStream` landed (R-A) and this pair measured **65 → 65**: a native iterator source is not a slice and never reaches the wrap, and the drain prelude pulls from the same iterator to LAND it. The correct next step is a batch-side `Drain` — the prelude reads `next_batch()` and extends the `Buffer` per packet — and it is a different change from R-A's | UNOWNED |
| the twelve unrouted slice sites (`emit_find`, `build_phase_frag`, `step_wrap` ×2, `chain_nest_frag`, `emit_aggregate` else, `rel_body_simple_frag`, `chain_body_frag`; `emit_incremental` ×4 declared out) | route them through `batch_scan_frag` as R-A did for `emit_simple` — the wrap, the numbering (`__ss<k>`) and the by-reference row bind already exist and are gated by `logos_09_slice_scan_codegen`. The JOIN pair is first: it is the only one with a per-scan-site cursor consumer | UNOWNED — R-A's own remainder |
| the aggregate key enumeration (39) | batch the `HashMapKeys` walk at `rexpr_walk.logos:5695`, or record it as declared-out like the incremental tier — **not left in a row scored "yes"** | UNOWNED |
| the ⚠ ~~3975~~ **4152** indexed walks (4301 before R-A) | decide, once, whether the criterion is about the PULL PROTOCOL or about per-row iteration; the instrument must then see the shape it judges — ⚠ and it now DOES: §2 fixes the four greps, and R-A is the first stage whose effect on this number is measured against a control rather than asserted | the stage that re-states criterion 2 |
| every query's stream surface is `buffered` | ~~`direct`'s `typeof` blocker~~ **FALSIFIED AT R-E** (the fix landed; `direct` never needed it) — the true blocker is `sema_abi_layout`'s decline of a metaprog-emitted struct FIELD from a foreign package (see the §12 row above for the full measurement and the seam-1 correction: `direct` moves 13 sites, not 502) | compiler task first |
| no gate at all | §4d — the asymmetry is recorded, not closed | UNOWNED |

### 6.3 Criterion 3 — ARC/RC only where needed; BC alone in hot paths: **HOLDS IN SUBSTANCE; INSTRUMENT AND PROSE OVERCLAIM**

Every documented criterion-3 number reproduces on this tree, and the audit's own
broader sweep (12 emitted-query objects, 39 batch-plane objects) found ZERO
reference-count and ZERO atomic-RMW operations reachable from any emitted query
entry — a stronger reading than the gate's; the method is `rc_seam_gate.sh`'s
own (`objdump -dr` + the RC/lock classifiers + the hot closure) applied to every
emitted object rather than to the one evidence fixture, so it is re-runnable
from §5 by widening the object list. **The substance is confirmed
corpus-wide. The instrument does not earn it on two axes, and three prose
sentences are wider than the tree.**

**Owned remainder, with its named next step:**

| residual | next step | owner |
|---|---|---|
| the `lock` half is vacuous by construction | classify the atomic ENTRY POINTS as edges, or stop printing the zero | §4d, UNOWNED |
| the RC classifier misses `clone_arc`/`drop_arc`/`downgrade`/`upgrade`/… | widen to the export list + one refusal pair per spelling | §4d, UNOWNED |
| the pin ladder's residency half has NO CALLER, on rungs 1–2 as well as 3 | a caller on the borrowed path, or §7's sentences moved to future tense | §4b, UNOWNED |
| the refuted `QStream` sentence sits at TWO sites (`:1163` and `:805-811`) | supersede both in place | F3, S5's owner |
| §4c's `#50`/`#51` are inherited from the task tracker, not the tree | a `pass/`-or-`fail/` repro for the D3 miscompile; the three `#51` subjects named at their sites | §4c |
| a LYING `order <rel> = <c>;` declaration is unchecked — the Sort elision rests on a TYPE fact about the producer, never on a fact about the rows | a debug-tier verification on the first batch, or the premise documented as trusted-by-contract at its site | §4b, UNOWNED |
| handle → handle pipelines, and the `queued` producer form | not covered by the evidence arm; `queued` is PARKED by Victor | — |

### 6.4 What this round itself got wrong

⚠⚠ **R-C, 2026-08-15 — THE DENOMINATOR EXCLUDED A WHOLE CONTAINER KIND, 319×,
AND THIS IS THE SECOND TIME IN THIS ARC.** `D2`'s type filter was
`Vec|Buffer|HashMap|BTreeMap`. It read 3632, and 3632 was reproduced exactly by
every audit of this file including the one that wrote §6.1's R-B entry — because
Vec 3028 + HashMap 592 + Buffer 12 IS 3632. The agreement was total and it was
about the wrong population: **319 emitted `HashSet` collections were counted by
no instrument in the tree.** Every ownership percentage published for criterion 1
before R-C1 — 52.79%, 52.84%, 70.73% — is a share of a population that excluded
the fixpoint plane's novelty structures.

**This is D4's shape exactly** (§6.4's existing entry: *a right number with a
wrong reason — the TYPE filter dropped it*), which makes it a CLASS and not an
incident. The rule it costs, stated so the third instance is caught by a method
rather than by luck: **a denominator defined by an ENUMERATION of kinds must be
re-derived against the artifact's own type histogram, never maintained by hand.**
The instrument now carries that histogram in its header, and every excluded kind
is named with the reason it is excluded (`SliceStream` 149 and `HashMapKeys` 39
are borrows/views, `Option` 26 is a scalar cell) — so an exclusion is an argument
a reader can attack, not a list they must trust.

⚠ **AND THE MISS WAS NOT UNIFORMLY INVISIBLE — one of the 319 was already gated,
which is the part worth keeping.** `__hs` (10) is `hash_build_frag`'s set form,
owned by the SAME `arrange` node as `__hm`, and `plan_ground_census_gate.sh`
FACT C has counted it correctly all along (`__(hm|hs|bt)`, 598 == 598). So the
census gate and the criterion-1 instrument disagreed by 10 about the same
emitter site for the whole arc, in the same tree, and nothing compared them. Two
instruments over one population must be reconciled or one of them is decoration.


Recorded here because the pattern, not the instance, is the finding.

* **D1 (the headline).** "`D1` = 4011 on `af17c2fa`" was the DUMP-FILE count
  borrowed into the plan-line slot; the tree's own plan-line reading at that
  snapshot is 4,249, over a different (sandbox, 203→204 file) population. It
  survived because both quantities moved +10, so the supporting clause read true
  against either. Struck in §1.
* **D2.** The ACC table covered 6 of the 9 named identities, so 20 bindings the
  census gate pins as NAMED were reported as the worklist, and a further 12 are
  unrepresentable under a name-only key. Fixed in the instrument; the FLOOR is
  now said out loud.
* **D3.** "~330 `__nd_*`, `__dl_*`" is 398 — the one row of the worklist table
  that did not reproduce.
* **D4.** The 647th `__out` was blamed on the name classifier; the TYPE filter
  dropped it (`let mut __out: String`). A right number with a wrong reason
  points the next reader at the wrong half of the instrument.
* **D5 / C2.** A verdict whose named counts summed to 26 of its own 65, and a
  falsifiability claim resting on two spellings where the artifact has four.
* **C3.** An instrument half that cannot fire, inside the instrument written to
  close "blind by construction".
* **Refuted challenge, recorded so it does not recur:** "124 DRed phase fns" IS
  re-derivable — `_od`/`_odp`/`_cpt`/`_i` at 31 each (§4b).

⚠⚠ **R-B (2026-08-15) — THE SAME PATTERN AGAIN, AND IT IS THE ONE THIS SECTION
KEEPS RECORDING: a delta measured across two columns that differ by more than
the thing being claimed.** R-B's first record read "`N1` +650 and `accounted`
+650: 650 collections that existed and had no name now have one." `N1` +650 is
true and control-separated. **`accounted` +650 was 1919 (R-A tree, R-A table)
against 2569 (R-B tree, R-B table), attributed entirely to the tree — and the
tree-vs-tree delta was ZERO.** It is D5's shape (a number whose parts do not
sum), the ratio-from-two-separate-measurements shape, and the "oracle that shares
the algorithm" shape at once: the ledger being credited was the same artifact
the stage had just edited.

Three things follow, and only the first is about R-B:

1. The number is corrected and both columns are re-swept with one instrument
   revision (§1's control table). The stage's real result is **+650 named,
   −650 worklist, 30.3% unowned → 12.39%**, which is larger than what was
   claimed, not smaller — the overclaim was in the ATTRIBUTION, not the size.
2. The instrument is repaired so the mistake cannot be silent again: `G4`
   refuses a credit whose owning head has a zero fire count (§4d, CLOSED). The
   defect was invisible for the whole arc because a self-certifying ledger
   produces no red.
3. **The control is what found it, and nothing else could have.** The dumps were
   identical, the answer diff was clean, the gate was green, and the number was
   still wrong. That is the argument for §5's rule (ii) being a step in the
   recipe rather than a habit.

### 6.5 THE STOP

**The arc pauses HERE.** Victor's three criteria are stated, instrumented and
read: criterion 1 NOT met, criterion 2 NOT met, criterion 3 holding in substance
with its instrument owing two repairs. Every remaining piece of work is named
above, sized in the units its own instrument prints, and carries an owner or the
word UNOWNED — nothing is left implied, and no remainder is described as small.

**The remainder is priced and owned. Work resumes by Victor's pick, not by
inertia.** No stage follows this one automatically; the next slice is whichever
row of §6.1–§6.3 Victor names, and the honest default if he names none is that
the arc stays paused with its books open.

⚠ **R-A ran on that basis (2026-08-15) — the picked row, and then the pause
again.** The row was §6.2's "the 3975 indexed walks", taken as its dominant
concrete half: the `&[Row]` slice arm. It moved 149 sites and closed with no
remainder, criterion 1 measured unmoved against a control, and **both verdicts
above are unchanged: criterion 1 NOT met, criterion 2 NOT met.** What R-A adds
to this section is not a verdict, it is arithmetic: 4152 indexed walks left, 12
of 13 emitter sites unrouted, `.next()` still 65, `__out` still unowned. The arc
pauses here again, with the books open and one number smaller.
