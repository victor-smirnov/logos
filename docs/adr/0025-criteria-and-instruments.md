# ADR 0025 — the three criteria, their instruments, and the residual inventory

Sibling of [`0025-deem-batch-cursor-plane.md`](0025-deem-batch-cursor-plane.md).
That document is the DESIGN and the slice journal. This one is the arc's
DEFINITION OF DONE: Victor's three criteria as stated, what each means
operationally on this tree, the instrument that reads it, the reading it gives
today, and — §4 — every named residual in ONE list an audit can walk.

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
inherited from a commit message.

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
| `D2` | artifact (`--gen-dir`, `logos.gen.*` excluded) | `let [mut] n: (Vec\|Buffer\|HashMap\|BTreeMap)<…>` bindings |

**Reading on this tree (185 fixtures, 169 with dumps, 2 known non-compiling):**

```
D1 = 4036   N1 = 1334 (33.05%)   T = 615   D2 = 3620, accounted 1891 (52.24%)
```

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
under any reading (`D1` = 4036 here; 4011 on `af17c2fa` before S6-A's fixture;
the S5 auditor's nearest artifact-channel reading was 3,312, `D2` = 3620 here).
The 4,023 was measured at S2's close, three slices and several node renames ago,
and re-deriving it would mean building `e98f5a58`. It is recorded as
SUPERSEDED — not reproduced — and every number above is from this tree.

**Gate or instrument? Instrument, and the reason is measured.** `D1`/`N1`/`D2`
are corpus-size-dependent: S6-A's one fixture moved `D1` +10, and
`deem_pipeline_handle_seam` (added by this ticket) moved it a further +15. A
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
largest first; the script prints the full list):

| bindings | class | status |
|---|---|---|
| 646 | `__out` | the query-output Vec. S2 assigned it to S5; **S5 did not take it and no owner was re-assigned** (audit F5). Unowned. |
| 145 | `__cp` | DRed phase collections (`__wql_*_dred`) — the incremental tier, declared out of the batch plane by S6-B. |
| ~330 | `__nd_*`, `__dl_*` | incremental derived/delta relations — same declaration. |
| 70 | `__tt` | fixpoint temporaries. S2 assigned the 791-strong fixpoint-buffer class to S6; S6-A took the Writ half only. |
| rest | `__wcd`, `__nw`, `__odv`, `__rmv`, `__rel_*`, `__pres`, `__lt`, `__ecp`, … | per-query scratch and rel landings, ≤44 each, un-triaged. |

---

## 2. Criterion 2 — full algebra integration with batch + cursor

**This criterion had no statement in the tree before this file (audit F6).**

**Operational definition**: *every plane of the algebra — scan, join, sort,
aggregate, output — pulls BATCHES, through the one pull protocol
(`next_batch() -> Option<Batch>`), rather than rows.* The criterion is met when
enumerating the planes leaves no row-at-a-time pull site in emitted query code.
So the instrument is the ENUMERATION, and it is falsifiable because both
spellings are visible in the artifact: `next_batch()` vs `.next()`.

**Instrument**: the same sweep as criterion 1 (it already produces the artifact
dumps), plus two greps whose definitions are fixed here:

```
tests/logos/criterion1_materialization_instrument.sh /tmp/c1
grep -ho 'next_batch()\|\.next()' /tmp/c1/*.user | sort | uniq -c   # pull shape
grep -c 'let mut __out'          /tmp/c1/*.user                     # output plane
```

**Reading on this tree** — 15 `next_batch()` pulls in 9 dumps, **65 row-at-a-time
`.next()` pulls in 27 dumps**:

| plane | pulls batches? | evidence on this tree |
|---|---|---|
| scan, container family | **yes** | leaf batches via `__ctr_b*` / `__ctr_leafbatch` (S1); the descent appears in the hot closure of `deem_pipeline_handle_seam` |
| scan, native iterator source | **no** | 14 sites, `let __opt: Option<R> = (__rel_s).next()` in the row loop |
| drain prelude | **no** | 9 sites, `let __dr: Option<R> = __it_s.next()` landing into a `Buffer` |
| join — probe side | **yes** | the probe reads the driving nest, which is the scan plane |
| join — **build side** | **no** | 3 sites, `let __bo1: Option<R> = (__rel_p).next()` — `rexpr_walk::build_phase_frag`, **the fourth pull site**, never converted when S1 collapsed the scan. Any batch source on a build side dies there (`type mismatch — expected Option, got Option`: the annotation is the ROW type, the pull yields a BATCH). Invisible to the green corpus BY CONSTRUCTION — no corpus query puts a batch source on that side. |
| sort | **yes** (S3) | `land_end` + `prev_batch` for the desc elision; `key vector` + permuted index vectors otherwise |
| aggregate | **yes** (S4/S5) | single-pass fold over pulled batches; ⚠ the new pure-aggregate-over-a-row-producer arm has ZERO corpus executions (audit F10) |
| output | **no** | 646 `let mut __out: Vec<…>` — the class S2 assigned to S5, unowned since |
| incremental (`__dl`/`__nd`/`__edb`, DRed) | **no, declared** | all 124 DRed phase fns take `&[…]`; S6-B measured the seam as `<q>_apply`'s parameter list alone and declared rather than attempted it |

**Verdict: criterion 2 is NOT met**, and the three open planes are named with
counts rather than adjectives: the build side (3), the native-source scan +
drain prelude (23), the output plane (646 by the instrument's binding CLASS; a raw `grep -c 'let mut __out'` answers 647 — one binding is not __out-classed; the class number is the authority).

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

---

## 4. THE RESIDUAL INVENTORY — one list, walkable

Everything the arc has named and not closed. Each row says where it is recorded
and who owns it; "unowned" is written as unowned rather than left implied.

### 4a. From the S5 audit (10 findings; 6 CONFIRMED, 4 PLAUSIBLE)

| # | status | subject | disposition |
|---|---|---|---|
| F1 | CONFIRMED | `deem` was not a HOT token — 84%-style sampling risk | **CLOSED** at `af17c2fa` (L2 2120 → 2146) |
| F2 | CONFIRMED | the RC instrument is blind by construction | **CLOSED HERE** — §3, `rc_seam_gate.sh` + `deem_pipeline_handle_seam` |
| F3 | CONFIRMED | ADR §12's requirement line still says the pipeline return is `#[borrow_carrying]` and tied to the source args; what landed is an OWNED `Buffer<E>` carrying no borrow | **OPEN** — one ADR sentence to supersede in place, S5's owner |
| F4 | CONFIRMED | §7 rungs 1–2 "ZERO counting" rests on probes `s5r/p1..p5`: hand-written non-Deem programs over `&[i64]`. They measure that the LANGUAGE admits a borrow-carrying return, nothing about counting in an emitted pipeline | **CLOSED IN SUBSTANCE by §3** (the emitted-artifact reading now exists); the §7 prose still cites the probes — supersede in place |
| F5 | CONFIRMED | the 617-strong query-output class was assigned to S5 and not taken; `let mut __out` 637 before, 637 after; **646 today** | **OPEN, UNOWNED** — the largest criterion-1 class and criterion-2's output plane |
| F6 | CONFIRMED | criteria not stated, instruments not in tree | **CLOSED HERE** (this file + two instruments) |
| F7 | PLAUSIBLE | "486 `*_stream`" is stale (490 measured) | **OPEN** — a number in ADR §12 to re-measure or strike |
| F8 | PLAUSIBLE | "PURE ADDITION" is a sub-stage property stated at round scope | **OPEN** — S4's claim, scope to be narrowed in place |
| F9 | PLAUSIBLE | plan and emitter resolve the same rel by two different lookups (`rel_find_name` vs `rel_find_src`) | **OPEN** — a divergence hazard with no gate |
| F10 | PLAUSIBLE | the pure-aggregate-over-a-row-producer arm has ZERO corpus executions; its only oracle is a sandbox file | **OPEN** — needs a corpus fixture or an honest deletion |

### 4b. Owed by the ADR itself

| where | subject | owner |
|---|---|---|
| §6 / S6-A | **the fourth pull site** — `rexpr_walk::build_phase_frag` pulls rows; any batch source on a join build side dies there. Criterion 2's join-plane gap, made concrete | whichever stage first needs it (the `WritWalk` cursor will) |
| §6 / S6-A | the `WritWalk` CURSOR itself: S6-A landed the row-major layout and MEASURED the Buffer-producer route as a regression (+29 materialization nodes, +35,073 bytes, 3 fixtures stop compiling), then reverted it. The honest repair is still owed | S6 successor |
| §12 | the `direct` stream form — blocked on an ordering fact: `typeof(<container>)` does not resolve in any hand-written item of the declaring module, so the state type is spellable only from inside metaprog. Until then EVERY query's stream surface is `buffered` and seam 1 is 1 packet | emitter-plumbing step |
| §12 | the `queued` form + reqs 7–8 (lazy producer with kill-on-drop; borrows through the producer) | PARKED by Victor 08-11 |
| §10 / S6-B | the DBSP consumption seam = `<q>_apply`'s parameter list ALONE (70/70 `_apply` fns take a list textually identical to their batch fn's; 124 DRed phase fns take `&[…]` with sign fixed by PHASE) | the stage that changes the incremental surface |
| §3, §10, §11 | three `[REC]` forks, each one paragraph to reverse: capability-existence-by-trait; the weight NOT in the core row model; vocabulary at lang tier + hub traits out of `logos.mem.bt.map` | open by design |
| §1 | S2's own "STILL OWED": the drain arm becoming `Drain -> Buffer` end to end | S2 successor |
| §2 | selection vectors, columnar build side — declared axes, not v1 | out of v1 |

### 4c. Compiler residuals (tasks #50 / #51)

| task | subject | state |
|---|---|---|
| **#50** | **D3 miscompile** — a struct pattern binding a `&mut` aggregate field in a by-value match; a method call through the binding returns garbage | OPEN, not started |
| **#51** | documented D1 residuals off the Deem↔Memoria coupling: the slice §B6 store-side pair (R1/R2), the r11 imported-NLL labeled-loop over-refusal, the P2 extra-diagnostics noise | OPEN, D1 closed OVER them by its own closure criterion |
| — | `static mut` + a family deem + a native-source deem in ONE module ⇒ `logosc-metaprog: jit add_module: Duplicate definition of symbol`. Recorded at `pass/deem_order_desc_elision`; met again by `pass/deem_pipeline_handle_seam`, which works around it with a `*mut i64` counter | OPEN, metaprog/JIT, unowned |

### 4d. Instrument-side, found while landing this ticket

| subject | state |
|---|---|
| `gate_lint.py`'s `NOT_GATES` exemption has **no abuse-direction check**: an entry buys silence from R5-unregistered-gate, and nothing asserts that its key names a file that exists or that ctest really does not invoke it. R5's own selftest checks the exemption HOLDS (a declared reporter is not flagged), never that it is honest. This ticket used the exemption (for the criterion-1 instrument, with its ground) and so is the natural place to record the hole | OPEN, unowned |
| the criterion-1 instrument's ACC table maps binding-name classes to owner nodes; two pairings are EXACT (`__ks` 127 = `key vector` 127, `__ga_*` 208 = `accumulator` 208) and two are not (`__hm` 588 vs `arrange` 598; `__sv` 497 vs `materialize` 217). The deltas are printed, not explained — a per-node attribution is the next honest step, and until it exists "accounted 52.24%" is a class-level reading, not a site-level one | OPEN |

### 4e. Census-side

The census pin (`docs/deem-interpreter-deletion-census.md` §9) is the machine
half and is checked by `logos_00_census_pin`; the registry baseline moves with
every registration and is predicted-then-measured per slice. This ticket's
registrations: `pass/deem_pipeline_handle_seam` + `logos_09_rc_seam_hot_path`,
predicted +2 ALL / +2 `-LE imported` / +0 tier_commit.

---

## 5. Re-running all of it (the corpus-snapshot control's tree-resident route: the promoted criterion-1 instrument IS a whole-corpus sweep — run it on two trees and `cmp` the dumps; the sandbox scripts are convenience, not the record)

```
cmake --build build -j$(nproc)                                   # instruments read the BUILD
tests/logos/criterion1_materialization_instrument.sh /tmp/c1     # criteria 1 and 2
tests/logos/rc_seam_gate.sh build/bin/logosc tests/logos/pass    # criterion 3
cd build && ../tests/logos/test-levels.sh L2                     # the gate
```
