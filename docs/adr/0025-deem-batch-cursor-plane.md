# ADR 0025 — The batch-cursor plane: one logical data representation for query execution

Status: ACCEPTED (design pinned 2026-08-10, Victor + Claude PAIR).
Three subsidiary forks carry adopted recommendations, marked `[REC]` where they
appear (§4, §10, §11); each is one paragraph to reverse.
Scope: the data plane between storage (heap, Writ, Memoria) and Deem's emitted
query code; the lifetime/pinning contract that makes zero-copy sound. Builds on
ADR 0024 (typed plan IR); feeds ADR 0013 (DBSP) and the streaming-Deem line.

## Problem

Query execution has no logical data representation — it has ten physical ones,
and every consumer branches on which one it holds. Measured on the tree at
`ca6184ab`:

| # | representation | pull | re-pass | random access | free size | order as a FACT |
|---|---|---|---|---|---|---|
| 1 | `&[T]` heap slice (`is_slice`) | indexed loop | yes | yes | yes | recorded nowhere |
| 2 | family `…Walk` (Memoria) | `next()` per row | no (contract) | no | declared (`size`) | exists physically, recorded nowhere |
| 3 | drained iterator → `Vec` + `_sl` | — | yes | yes | yes | no |
| 4 | `__rel_X_sl` (fixpoint output) | indexed | yes | yes | yes | no |
| 5 | `__dl_X_sl` (semi-naive delta) | indexed | — | — | — | no |
| 6 | `VecIter`/`HashMapIter`/`BTreeMapIter` | `next()` | no | no | len exists | BTreeMap's exists physically, recorded nowhere |
| 7 | Writ: prebuilt `Vec<WritEdgeRow>` | indexed | yes | yes | yes | no |
| 8 | join buckets: `HashMap<K, Vec<i64>>` (INDICES into a slice) OR rows | two shapes | | | | |
| 9 | sort: `__ix0: Vec<i64>` permutation over an INDEXED source | 17 sites in rexpr_walk | | | | |
| 10 | query output: always `Vec<elem>` | | | | | |

Consequences, each a recurring defect class: materialization is implicit
prelude text, not an algebra node with a `why`; "read exactly once" is a global
side-channel proof (`plan_mark_single_pass`) instead of a property of a type;
(HALF-CLOSED at S2j: the side channel is gone — `plan_insert_drains` writes the
node — but the refusal is still a planner walk, not a type;)
three parallel booleans (`is_slice`/`rel_stream`/`rel_iter`) restate one fact;
`order by` demands `Indexed`, which only slices have, so an ordered container
is drained and re-sorted; every new storage adds a branch to every consumer.
The per-row `Walk.next()` is a container method per row, and a leaf-boundary
crossing re-descends (CoW forbids sibling pointers), so even the streamed path
pays a per-row constant the leaf already amortizes.

## Decision (summary)

One logical object: a **stream of bounded batches of typed rows**.

1. The pull unit is a BATCH, not a row: `next() -> Option<B>`; an EMPTY batch
   is legal (min size 0). Less communication; a leaf, an epoch, a keepalive are
   all one shape.
2. A batch is a typed VIEW over storage, generic over layout WITHOUT dynamic
   dispatch: row-major (`RowsBatch<R>` = `&[R]`) and columnar (`ColsBatch` =
   per-column `PdtCol` wrappers over the leaf's own PdtBuf slots). The emitter
   knows the batch type from the source's declaration and monomorphizes the
   access spelling. (Almost) zero copy: the batch borrows the leaf.
3. Capabilities are orthogonal traits on the stream type, not shapes:
   `Rewind`, `SizedStream`, `OrderedBy<K>`, `Landed<K>`. Existence of a
   capability = trait membership (a type cannot drift from the code — the S4c
   lesson); cost and exactness stay DECLARATIONS (`op … exact`, `size`) because
   types cannot carry them.
4. Materialization becomes an explicit, priced plan node: `Drain`, `Sort`,
   `Arrange`. `Buffer<R>` (today's Vec) is the degenerate stream — one packet,
   implements everything. Read-once stops being a proof and becomes typing: a
   second use of a non-`Rewind` stream is a plan-level error whose repair is an
   inserted `Drain` with its ground.
5. Zero-copy is made sound by a two-part, store-invariant PIN CONTRACT
   (§7): "the snapshot is alive" (altitude: snapshot) + "this leaf's in-memory
   image is resident" (altitude: batch). Cursors are BORROWS — no reference
   counting appears on any path the query compiler emits.

## 1. The pull protocol

```logos
trait BatchStream<B> { fn next(&mut self) -> Option<B>; }
// None      = exhausted
// Some(b), b.len() == 0 = legal: empty leaf, empty epoch, no-progress tick
```

Row-at-a-time is a CONSUMER pattern (the inner loop), not a protocol. The one
emitted scan shape:

```logos
while let Some(b) = s.next() {
    let n = b.len(); let mut i = 0i64;
    while i < n { /* clauses over b @ i */ i = i + 1i64; }
}
```

A heap slice is a stream of EXACTLY ONE batch — the whole `&[T]`. The old
indexed loop is literally the inner loop of the degenerate case, so the
`is_slice` branch does not get adapted, it dissolves; codegen for slice sources
must stay byte-comparable (S1 gate). Drained sources, `__rel_X_sl`, an epoch's
delta: all one-packet streams.

## 2. The batch: two layouts, declaration-selected

* **`RowsBatch<R>`** — `&[R]`: heap collections, fixpoint outputs, deltas.
* **`ColsBatch`** — the Memoria leaf AS IT IS: per-column `PdtCol` wrappers
  around the leaf's PdtBuf slots. FSE column → slice-like indexation over the
  payload; VLE/DST column → `at(i)` computed from offsets+payload on access
  (str views). The column wrapper IS the PdtBuffer in a convenient shell —
  indexation comes with it; no intermediate view-buffer is built.

Read mode vs write mode: the `(allocator, slot)` re-resolve-per-call idiom of
`stdlib/mem/bt/view.logos` exists because a WRITE can resize slots. A read
batch over a pinned leaf (§7) may resolve payload pointers ONCE and cache —
the leaf is immutable (committed snapshot) or BC-stable (no `&mut` possible
while the batch lives). Two modes of one wrapper, not two types.

Layout genericity "без фанатизма": the emitter is a metaprogram — it reads the
batch layout from the source's declaration (the natspec rel entry gains a
layout marker next to `%<ret-ty>`) and spells access accordingly
(`b.row(i).k` / `b.col_k().at(i)`) in ONE emitter function. A third layout is
a new row in that function, not a new branch in every consumer. Selection
vectors (filtered batch without copy) are deliberately NOT in v1 — recorded as
an axis.

**THE LAYOUT MARKER LANDED (S6, 2026-08-14), AND UNTIL THEN THIS SECTION WAS
HALF PROSE.** "The natspec rel entry gains a layout marker" above was written
in the future tense and stayed there through S1–S5, because only `ColsBatch`
had a consumer: `params::batch_row_text` spelled `bb.<col>_at(j)`
UNCONDITIONALLY, and the counter was typed `u64` because that is what
`ColsBatch::len` returns. So a ROW-MAJOR producer — the case §2 lists FIRST,
and the shape every heap collection, fixpoint output and §10 delta has — did
not compile. Measured on a user source whose producer returns
`Buffer<(i64, i64)>`, three errors, three different halves of one gap:

```
error: 'Buffer$…$G1$tup$2$i64$i64' has no method 'next_batch'
error: slice has no method 'a_at'
error: let '__bn0': type mismatch — expected u64, got i64
```

The marker is `logos.lang.stream::RowMajor` — a MARKER TRAIT on the stream
type, asked of the producer's return type exactly as `o`/`r`/`n` are, emitted
as the natspec letter `m`, absent ⇒ columnar (so every pre-S6 producer's
emission is unchanged, byte for byte, and the corpus measured it). It is a
separate trait rather than `BatchStream<B>`'s ARGUMENT — which is where the
answer physically is — because `sema_has_impl_recursive` answers membership
with a bool and hands back no trait type arguments; the marker retires when
that recorded residual (the same one behind `order <rel> = <col>`) closes.
Two functions read it and nothing else does: `batch_row_text` (`bb[j as i64]`
vs `bb.<col>_at(j)`) and its new companion `batch_len_text` (the `as u64`
cast), which is the "a layout is a ROW IN THAT FUNCTION" rule made structural
across all three pull sites (`batch_scan_frag`, `batch_scan_rev_frag`, the
drain prelude). `Buffer<R>` also gained the INHERENT `next_batch()` door the
emitter calls, with `BatchStream::next` delegating to it — the shape the
generated family has carried since S0; the degenerate stream implemented the
protocol and could not be consumed BY it. Fixture
`pass/deem_rowmajor_batch_source` (four packets, one EMPTY mid-stream, the
source's own `PULLS`/`ROWS` log, and a `Vec`-producer twin as the answer
differential) + gate `logos_09_rowmajor_batch_layout` (both layouts, each
spelling asserted absent from the other subject).

⚠ THE COLUMN ACCESSOR IS NOT A METHOD ON THE BATCH — measured 2026-08-11,
landing `stdlib/mem/bt/batch.logos`. The spelling above says `b.col_k()`, but a
column drawn as a METHOD of the batch is NOT borrow-tied to the leaf: a free
function returning a `#[borrow_carrying]` value takes the merged provenance of
its REFERENCE arguments, and a method's result takes its receiver's — and in
both cases the provenance stops after ONE hop, so a `&ColsBatch` argument
contributes the borrow of the batch LOCAL, not the `&NodeView` borrow that
local carries. Probes: with a `&ColRef`- or `&ColsBatch`-rooted constructor the
"mutate the leaf while the batch/column lives" program COMPILES; with every
constructor rooted at `&NodeView` it is refused (`cannot borrow 'nv' as
mutable: 'nv' has shared borrows`). Hence the v1 spelling is
`pdt_col(&leaf, &b, k).at(i)` — the leaf is passed as a borrow WITNESS and is
checked (the batch keeps its slot and re-resolves through the witness once per
column, per batch) so the witness cannot be laundered with an unrelated node.
One emitter function still, one row per layout; only the column-access row
carries the extra argument. Making `b.col_k()` legal is a borrow-checker
change (transitive provenance through a borrow-carrying local), not a batch
change — recorded as an axis, not adopted here.

⚠⚠ THE AXIS ABOVE IS CLOSED — first half after D1 round 1, second half after D1
round 2, both re-measured 2026-08-11. This heading read "HALF OF THE AXIS ABOVE
IS CLOSED" and is corrected in place: the two bullets below were written one
round apart, the second one carries its own supersession, and the state of the
tree is now that BOTH hops re-export. The paragraph stands as the HISTORY (it is
why v1 is spelled
`pdt_col(&leaf, &b, k)`), but it stated ONE rule for two hops — "a free function
… takes the merged provenance of its REFERENCE arguments, and a method's result
takes its receiver's — and in both cases the provenance stops after ONE hop" —
and the two halves have come apart. A loan now follows the HOLDER graph, and
MEASURED, with the two probes differing in exactly one line:

* **METHOD RECEIVER — RE-EXPORTS NOW.** The receiver's result takes everything
  the receiver transitively carries. `fail/bc_d1_holder_chain_held` pins it
  generically (`s.get()` on a borrow-carrying local, held across `c.bump()`,
  refused). CONCRETELY on this plane, on `{N}LeafBatch::keys()` — a real method
  returning a real `PdtCol` — `let kc = b.keys(); c.insert(…); kc.at(0)` is
  REFUSED with `cannot borrow 'c' as mutable: 'c' has shared borrows`, through
  the two by-value hops of `next_batch().unwrap()` and then the method.
  Controlled by a ONE-LINE perturbation: make `kc` dead after the insert and the
  same program COMPILES, so the refusal is the COLUMN's loan reaching `c`, not
  the batch's left over. **So `b.col_k()` as a method IS now expressible and
  would tie — the answer to the question this ⚠ was raised to ask is YES.**
* **FREE-FN `&`-ARG — STILL DROPS. The original claim survives here, and this
  is the hop that is still open.** ⚠ **SUPERSEDED IN PLACE — CLOSED by D1 round
  2, re-measured 2026-08-11, same day.** The paragraph is kept as the history
  because it is what the sentence below about `&NodeView` was derived FROM; as
  a description of the tree it is now false. It said: a free fn whose only
  reference argument is a borrow-CARRYING local contributes that local's own
  borrow and not what the local carries — `fn thru(b: &B) -> B { B { p: b.p } }`
  then `let b1 = thru(&b0); c.bump(); *b1.p` COMPILES, while the same hop
  written as a METHOD is refused. That asymmetry was a compiler defect, not a
  decision, and it was recorded here and NOT pinned, because pinning an admit
  would write the laundering down as intended.
  **POST-FIX VERDICT: the `&`-arg is a hop like any other.** The hop-root walk
  gated on "is this argument borrow-CARRYING?", which is false for `&B`
  (Kind::Ref); it now asks "may this type carry a borrow?" at that gate only, so
  a plain-ref argument rooted at a loan holder re-exports what the holder
  carries. The free-fn and method spellings agree. The program above is REFUSED
  and is pinned as `fail/bc_d1r2_ref_arg_hop_held` — the admit could not be
  pinned, the refusal can be — with `pass/bc_d1r2_ref_arg_hop_admits` as its
  twin, which keeps a CONSUMING `&`-arg call (scalar result) admitted so the
  refusal cannot be mistaken for a checker that freezes every `&` argument's
  root. CONTROL REVERT (round-1 checker rebuilt): the leak program compiled
  rc=0, so the pin is a new refusal and not a rename of a round-1 one.

Consequently `cols_batch`'s `&NodeView`-not-`&ColRef` rooting (recorded at the
constructor in `stdlib/mem/bt/batch.logos`) is NO LONGER FORCED by the checker.
Measured on this plane's own two-hop shape (`/tmp/bcr2/S1_{colref,nodeview}_
root.logos`: `let cr = nv.col(); let b = cols_batch_cr(&cr); nv.put_cell();
*b.q`): the `&ColRef`-rooted constructor is now rc=1 `cannot borrow 'nv' as
mutable` where it was rc=0, and the `&NodeView`-rooted one is rc=1 as before.
So `&NodeView` STAYS, but as a DESIGN choice — it keeps the tie one hop long
and states the dependency the batch actually has — and S1 is now free to pick
the source that reads best rather than the one the checker tolerated. The
refusal probe above is the control any such change must keep at rc=1. The same
verdict is recorded at the constructor itself.

Nothing is respelled here. The v1 witness form stays exactly as it is: rewriting
`pdt_col`'s signature is S1's reconciliation (which also has to decide
`ColsBatch`'s multi-slot form, §5), and doing it in a docs step would be a
spelling change measured by nothing. What changed is the CONSTRAINT, which is
what this ADR records. Note also that the witness argument was never only a
borrow device — it is the pointer-identity check against the slot
(`cols_batch`'s `slot` field, abuse direction measured), so dropping it stays a
separate question from whether the borrow ties.

**The batch is ONE FORM FOR ALL STORES (Victor, 2026-08-10).** PdtBuffer has
no dyn and needs none: a leaf's column region has the same shape whichever
store resolved the block, so `ColsBatch`/`PdtCol` are one concrete type per
config — which is exactly what lets the layers ABOVE it go dyn (§7b) without
a per-row cost. The batch is the currency that crosses the dyn boundary.

## 3. Capabilities

```logos
trait Rewind          { fn rewind(&mut self); }        // re-land; multi-pass
trait SizedStream     { fn size(&self) -> u64; }       // free, no drain
trait OrderedBy<K>    { … }                            // batches arrive in K order, intra-batch sorted
trait Landed<K>       { fn seek_key(&mut self, k: K); }// ordered positioning, BY KEY
trait Bidirectional<B>            { fn prev(&mut self) -> Option<B>; }   // traversal ⊂ …
trait RandomAccess<B>: Bidirectional<B> { fn seek_nth(&mut self, n: u64); } // … ⊂ by ORDINAL
```

⚠ `Landed::seek` WAS RENAMED `seek_key` WHEN THE TRAVERSAL PAIR LANDED (S1,
2026-08-13), and the rename is the whole repair of a naming hazard, not a
tidy-up. The generated container beneath a family walk spells `seek(n: u64)` BY
ORDINAL and `seek_key(k)` BY KEY; a family walk implements `Landed<K>` AND
`RandomAccess<B>` simultaneously, so a `Landed::seek(k)` would have sat on one
type beside `seek_nth(n)` with the two conventions INVERTED relative to the layer
underneath. It was cheap because `Landed` had exactly one impl and one caller
when S1 opened; it stops being cheap once S3 consumes the trait.

**The TRAVERSAL axis (Victor, 2026-08-11).** Memoria containers support
random access by entry ORDINAL, so the batch cursor carries a THIRD
orthogonal capability axis — `forward-only ⊂ bidirectional ⊂ random-access`
(by ordinal) — beside order-as-fact (`OrderedBy<K>`) and key-positioning
(`Landed<K>`). This generalizes the recorded "`can_seek` as one bool is
WRONG" lesson to three independent axes: probe ⟂ ordered positioning ⟂
traversal degree. Family walks declare random-access (`seek(n)` is ONE
DESCENT — every container is an array of its entries — so the per-LEAF
frequency invariant of §7b holds); heap slices and `Buffer` are trivially
random-access; `HashMapIter` is forward-only; btree walks are bidirectional.
Vocabulary lands at S1 as a trait pair beside `Rewind`/`SizedStream`
(spelling settled there); the S3 harvest: `offset`/`limit` push down as one
`seek_nth` instead of a skip loop, `order by … desc` over a bidirectional
source emits no `Sort`, and ordinal sampling needs no drain.

**LANDED (S1, 2026-08-13)** in `stdlib/lang/stream/stream.logos` as the pair
above. THE ⊂ IS THE SUPERTRAIT EDGE, so the lattice is in the type system and
cannot drift from this paragraph: an `impl RandomAccess` without the matching
`impl Bidirectional` is refused (`missing impl … (required by supertrait)`,
measured). No third name is minted for the BOTTOM of the chain — forward-only is
a plain `BatchStream` with neither trait, which is the recorded "`can_seek` as
one bool is WRONG" lesson applied in the other direction: the absence of a
capability is not a capability. `HashMapIter`, §3's forward-only example,
declares neither and is not on this plane at all (it implements `Iterator`; the
only `BatchStream` impls tree-wide are `Buffer` and the generated
`{N}LeafWalk`). Implementations: `Buffer` — both, field arithmetic, no descent;
`{N}LeafWalk` — both, one descent each, `prev` re-using the SAME ordinal
`seek(n)` the forward pull uses. Fixtures `pass/stream_traversal_buffer` and
`pass/ctr_leaf_family_spelling`, each held by a CONTROL REVERT run one clause at
a time (neuter `seek_nth` → red; restore, rebuild green; neuter `prev` → red).

TWO CONTRACTS SETTLED HERE, because both were ambiguous in the paragraph above.
(a) THE POSITION IS A POINTER BETWEEN BATCHES: `next` hands out the batch after
it and moves past that batch; `prev` hands out the batch before it and moves
before that batch. So `next` then `prev` hands out the SAME batch — `prev`
UN-CONSUMES, it does not skip — and the walk needs no second cursor.
(b) `seek_nth` REPOSITIONS INSIDE A LANDING; it does not re-land. Ordinals are
0-based over the STREAM's own landing (a `from` landing's ordinal 0 is its first
row, not the container's), and `SizedStream::size()` — the landing's total — does
not move under it. Re-landing is `Landed::seek_key` (by key) or `Rewind`.

`[REC]` Existence = trait membership of the producer's return type, asked by
the planner via metaprog trait queries (the seam `join_key_caps` already
anticipates). Cost, exactness, and the operation set remain declarations
(`op <rel>.<col> <cmp> = <fn> [exact]`, `size <rel> = <fn>`) — ADR 0024 S6 is
unchanged by this ADR. Rationale: a declaration can lie about code; a type
cannot; but cost is data-dependent and inexpressible as a type.

The order fact this table finally records: family walks and `BTreeMapIter`
declare `OrderedBy<K>`; `HashMapIter` does not. Immediate harvest: `order by`
on a column whose source already delivers that order is a NO-OP — today it is
a full drain plus a 17-site `__ix0` permutation; `limit` over it becomes a
bounded walk.

## 4. Adapters as plan nodes

* `Drain : BatchStream<B> → Buffer` — today's implicit prelude drain, now an
  AccessPlan step with a ground (`"drained: second use"` / `"order by"` /
  `"rel block"`), visible in `explain()`.
* `Sort : BatchStream<B> × key → Buffer(OrderedBy)` — a sorted buffer is
  `Indexed` + ordered; the `__ix0` shape disappears.
* `Arrange : BatchStream<B> × key → HashIndex<K,R>` — the join build side;
  buckets hold ROWS, the bucket-of-indices/bucket-of-rows split dies.
  (Columnar build side — indices INTO retained batches — is an axis, not v1.)

LANDED (S2 node layer, 2026-08-13) in `stdlib/mem/wql/access_plan.logos`:
`MAT_DRAIN`/`MAT_SORT`/`MAT_ARRANGE` + the `MG_*` ground tokens + the node list,
derived from the mode pass's conclusions and named per rel by
`access_plan_explain`. THE EMITTER IS UNTOUCHED and the byte-pin
`logos_09_slice_scan_codegen` is what says so.

LANDED (S2j INVERSION, 2026-08-14) — THE DERIVATION NOW RUNS THE OTHER WAY, and
the side channel is DELETED. `plan_mark_single_pass` is `plan_walker::plan_insert_drains`:
the same walk, the same eight grounds, the same `js_reads_once` cascade, and the
output is `ap_drain(ap, prm, ri, ground, why)` — a `MAT_DRAIN`/`MAT_SORT` node —
where it used to be `read_once(ri, false, gnd, why)`. `AccessPlan::once` and
`AccessPlan::owhy` are gone; `access_plan_plan_nodes`, the third pass that turned
the proof into a node, is gone; `access_plan_decide_mode` recovers "the plan reads
this once" as `prelude_ix(ri) < 0` — THE ABSENCE OF A DRAIN NODE. One
representation, no agreement to maintain.

TEXT-PRESERVING, as predicted: whole-corpus snapshot 161 dumps / 6,856,383 bytes,
`diff -rq` EMPTY, the same two pinned compile failures. The instrument is live and
the inverted walk carries the same load — CONTROL, one at a time, restored to a
byte-identical source with a green checkpoint after: `plan_prove_once` neutered to
an immediate `return pv` reproduces S2h's probe exactly — the SAME 11 artifacts
move, +3,061 bytes (S2h measured +2,953 on the then-159-dump corpus; the two extra
fixtures are the difference).

Still owed and the rest of S2: the `rel_stream`/`rel_iter`/`is_slice` booleans,
and the half §4 cares about most — the second read refused by `Rewind` rather than
by a planner walk.

Two corrections this stage owes §4 as written, both measured:

* **THE ARRANGEMENT IS A LIST, NOT A PER-REL FACT.** A chain naming one source
  twice builds TWO indexes over it (`__hm1` and `__hm2` over one drained
  `__rel_d`, emitted for `pass/deem_join_step_reread`), so arrangements are
  recorded per STEP, off the STRATEGY — not in the else-branch of the once
  cascade, where every arrangement over a twice-named source would be invisible.
* **"READ ONCE" AND "materializes" ARE ORTHOGONAL, and that is the fact the
  boolean could not state — which is why S2j kept the node and deleted the
  boolean rather than the reverse.** A hash join's build side is `stream=true`, read exactly
  once, AND fully materialized keyed. `logos_09_plan_nodes` asserts it as an
  inequality no flag can satisfy: more arrange nodes than `materialize`
  verdicts.
* The three grounds §4 names are spelled verbatim; the OTHER six are NOT folded
  into them (`JS_NONE` is an undecided, a nested loop proves many reads, an
  aggregate regroups). The container ground dies as §4 says — by being recorded
  as "no node: already a buffer" rather than by going quiet.
* NOT YET NAMED, and the gate header says so rather than letting the green read
  wider: an aggregate's group-state vectors and their group-row permutation (S4)
  — measured as THREE `__ix0` permutations against TWO `sort` nodes in
  `deem_batch_scan_drain` — and a derived rel's / SCC member's total, which is
  not a source's drain.
  **HALF OF THAT IS DISCHARGED AT §8 (S4-naming) AND HALF IS NOT, stated as two
  facts rather than as one closed bullet.** The GROUP-STATE VECTORS are named:
  four `AG_*` grounds, 380 bindings corpus-wide, plan count == artifact count per
  fixture (FACT H). The GROUP-ROW PERMUTATION is NOT: `__ix0` in the output phase
  is still counted (`EXPECT_PERM` 319) and still attributed to no node, exactly as
  the 85 permutations emitted where there is no sort at all are. A stage that
  named one and let the other read as named would be the census counting the
  question instead of the answer.

LANDED (S2b, 2026-08-13) — THE PRELUDE READS THE NODE. `plan_apply_access`
writes `prm.rel_node[r]` (`MAT_DRAIN`/`MAT_SORT` / `AD_NONE` / `AD_BUFFER`) from
`AccessPlan::prelude_node`, and `emit_prelude_oneshot` chooses its arm from that
field instead of from `rel_stream`/`rel_iter`. PROVENANCE MOVES, VALUE DOES NOT:
the emitted text is byte-identical over the whole corpus — 159 `--gen-dir` dumps
(6 776 657 bytes) across `tests/logos/pass/{wql_,deem_}*.logos`, `diff -r` empty
against the pre-S2b snapshot, the instrument having first been shown stable
against itself on one binary. L2 green (2116/2116), `logos_09_slice_scan_codegen`
and `logos_09_plan_nodes` green, `slice_scan_shape.golden` untouched.

The arm is PROVED live in both directions, one at a time, restored to a
byte-identical checkpoint between (a control on an un-restored control proves
nothing):

* force `AD_NONE` where the plan buffers ⇒ `deem_batch_scan_drain` loses all 6
  `__it_` bindings and `deem_join_step_reread` both of its 2, and `logosc` then
  FAILS on the emitted code (the scan expects the slice the drain no longer
  builds) — an expensive refusal, not a quiet one;
* force `MAT_DRAIN` where the plan streams ⇒ `deem_join_base_streams` goes 0 → 4
  `__it_` bindings; `deem_hashmap_source` stays at 0, because its producer is a
  container (`AD_BUFFER`) and neither forcing touches that arm — which is the
  separation the pair exists to show.

⚠ AND THE COST TO THE GATE, STATED: `logos_09_plan_nodes`'s clause
`#(drain+sort nodes) == #(__it_ bindings)` was an independent comparison before
S2b and is now true by construction for the per-rel prelude — both sides read one
lookup. The arrange↔index and `__ix0`↔sort clauses are untouched and stay
independent. The gate header carries this; the independent oracle for the prelude
clause is the forcing control above.

⚠ THE BLOCKER THE NEXT STAGE HITS, MEASURED BEFORE STARTING IT rather than
discovered halfway. "Slices ride as one-packet streams" needs a one-packet
`BatchStream` over a BORROWED `&[R]`, and the vocabulary has none: the only row
implementations in the tree are `Buffer<R>` — which OWNS a `Vec<R>`
(`buffer.logos`: "a Buffer that borrowed its rows from somewhere else would not
have materialized anything") — and the generated leaf-walks. A query's slice
parameter is `rows: &[Row]`, borrowed from the caller, so routing it through
`Buffer::from_vec` would COPY every row: an intermediate materialization added by
the compiler, which is the exact thing Victor's criterion 1 forbids. The same
gap blocks stage 1's "the scan reads the Buffer as a one-packet stream" for every
consumer that today indexes `(src)[__i]` (`emit_simple`'s slice arm, `emit_find`,
the join and aggregate emitters). So the next unit of work is a VOCABULARY unit,
not an emitter unit: `SliceStream<R>` (`#[borrow_carrying]`, one packet, `Rewind`
+ `SizedStream` + `RandomAccess`) beside `Buffer<R>`, and only then the emitter
collapse and the byte-pin's measured-equal transition.

LANDED (S2c, 2026-08-13) — THE REFUSAL CENSUS RE-DERIVED BY MACHINE, and the
"no silence where a drain happens" gate widened from four plans to every plan the
corpus compiles. `tests/logos/plan_ground_census_gate.sh`
(`logos_09_plan_ground_census`, tier_full, ~45 s at `-P nproc`) compiles all 175
`pass/{wql_,deem_}*.logos` with `LOGOS_TRACE_PLAN=1` and `--gen-dir` and reads
BOTH justification vocabularies — `why::wg_words`/`rj_words`/`sz_*_words` (WHICH
ANTECEDENT refused the order axis) and `access_plan::MG_*` (WHY a buffer exists)
— EXTRACTED FROM THE SOURCE FILES, never from a copy of the list, because a
hand-kept copy is how a new ground escapes its own census.

THE CENSUS, MEASURED 2026-08-13 (the gate prints it on every run, green or red):

```
  drain 4  sort 3  arrange 31    | artifact: __it_ 7   index bindings 594
  hash-join decisions 491        | materialize 210   stream 18
  no materialization: already-a-buffer 187, read-once 15   | SILENT 0
```

* **SILENT 0, over 175 plans.** Every rel that reports `materialize` names a node
  or the positive "already a buffer" ground. This is the §4 claim as a corpus
  fact rather than as a four-fixture sample.
* **THE PRELUDE CLAUSE HOLDS EXACTLY AND CORPUS-WIDE**: 7 drain+sort nodes, 7
  `let mut __it_…` bindings in the emitted artifacts, and per fixture as well.
  (Since S2b this is by construction for the per-rel prelude — the independent
  oracle is the forcing control above, not this gate.)
* **THE ARRANGE DEFICIT IS NOW A NUMBER, NOT A SENTENCE**: 31 Arrange nodes
  against 491 `hash join` decisions and 594 emitted index bindings. Two named
  classes account for the gap and NEITHER has a node yet — (i) a derived rel's /
  SCC member's own chain, emitted once per fixpoint variant (`wql_datalog_*`,
  `wql_incr_*`: 5 to 45 hash joins each, 0 arrange nodes), and (ii) the SECOND
  and further carried nests of one chain, each emitting its own build phase
  (`wql_deferred_plan_e2e`: 5 decisions, 14 bindings). Pinned, so the unnamed
  remainder cannot grow quietly; per fixture `arrange <= hash joins` is asserted,
  because an arrangement over a step no strategy decided would be invention.

THE WHY-VOCABULARY, WITNESSED vs UNWITNESSED — the coverage question answered
against what the CORPUS reaches, not what the lattice admits. Witnessed:
`WG_SCAN` 219, `WG_AGG` 146, `WG_NO_SORT` 124, `WG_ANTI_FIRST` 14,
`WG_EDGE_FIRST` 10, `WG_FALSE_FILTER` 4, `WG_HEAD` 3, `WG_MAX_CAND` 2,
`WG_IDENTITY` 1, `WG_FALSE_PRED` 1; `RJ_PREDDUP` 62, `RJ_PINDEP` 2;
`SZ_RUN_DERIVED` 27, `SZ_RUN_OK` 12, `SZ_PREP_LOCAL` 8, `SZ_PREP_DERIVED` 6;
`MG_CONTAINER` 187, `MG_JOIN_BUILD` 31, `MG_ORDER_BY` 3, `MG_REGROUP` 2,
`MG_RESCAN` 1, `MG_SECOND_USE` 1.

FIFTEEN GROUNDS ARE PUBLISHED SENTENCES NO CORPUS QUERY REACHES, and they are
pinned as the debt ledger rather than left to look like coverage:
`WG_NO_STEP`, `WG_UNDECIDED`, `WG_ONE_FLOAT`, `WG_MAX_FL`, `WG_CROSS`,
`WG_NO_SIZE`; `RJ_PREDBASE`, `RJ_PREDPIN`, `RJ_SHAPE`; `SZ_RUN_STREAMS`,
`SZ_PREP_STREAMS`; and — the four that matter for the rest of S2 —
`MG_REL_BLOCK`, `MG_UNDECIDED`, `MG_GPATH`, `MG_UNPROVEN`. THOSE FOUR ARE THE
ANSWER TO "which of the single-pass walk's 9 grounds still need a node
fixture": the other five are witnessed as nodes today. The pin is checked in BOTH
directions — a ground declared unwitnessed that the corpus reaches fails exactly
as loudly, because an exemption nobody checks in the abuse direction turns the
green into a voucher for it.

LANDED (S2h, 2026-08-13) — THE MATERIALIZATION DEBT LEDGER IS EMPTY, and its four
entries left by two different routes. The GROUND → WITNESSING FIXTURE table, all
nine, which is the precondition §4 sets for retiring the single-pass proof:

| ground | witness | node |
|---|---|---|
| `MG_SECOND_USE` | `deem_join_step_reread` | drain |
| `MG_ORDER_BY` | `deem_batch_scan_drain` (×2), `wql_deferred_plan_e2e` | sort |
| `MG_RESCAN` | `deem_join_step_streams` | drain |
| `MG_REGROUP` | `deem_batch_scan_drain`, `deem_pushdown_all_shapes` | drain |
| `MG_JOIN_BUILD` | 79 fixtures, 596 nodes | arrange |
| `MG_REL_BLOCK` | `deem_mat_ground_witness::w_rel_block` | drain — NEW |
| `MG_UNDECIDED` | `deem_mat_ground_witness::w_undecided` | drain — NEW |
| `MG_UNPROVEN` | `deem_mat_ground_witness::w_unproven` | drain — NEW |
| `MG_GPATH` | none — REFUTED AND DELETED | — |

WHY 175 FIXTURES COULD NOT REACH THREE OF THEM AND ONE FILE COULD: all three
require a source whose producer OFFERS AN ITERATOR, because
`access_plan_decide_mode` answers `MG_CONTAINER` before any of them is consulted
— and every `rel` block, `&Writ` graph and `&[T]` param in the corpus is a
container. `MG_GPATH` was not debt at all: `desugar_program_gpaths`
(`wql.logos:177`) runs BEFORE the walk and clears `has_gpath` on every query
kind, so the two arms reading it were UNREACHABLE. Control, both directions:
`error(…)` in both gpath arms left the corpus byte-identical (159 dumps /
6,776,657 bytes, `diff -rq` empty) across ~20 graph-path fixtures, while the same
edit one line up in the `has_order` arm took it to 158 dumps / −111,952 bytes.
The ground and its two arms are deleted; a ground nothing can reach is not a
fixture someone owes.

⚠ THIS REVERSES THE "THE CORPUS WAS NOT TOUCHED" RULE BELOW, DELIBERATELY. S2 was
right about the risk and wrong that recording debt was the only way to take it: a
proof may not be retired on the strength of sentences nobody has compiled, so the
debt had to be discharged before §4's deletion could even be considered. What
keeps the fixture a measurement is that each query carries a RUNTIME oracle over
the emitted artifact (row sequence + the pull count of an iterator that has no
length and cannot be read twice) — and writing it FOUND two things the census
could not: a node whose ground was set and whose justification was the empty
string (fixed by the `swhy` fallback in `access_plan_plan_nodes`, kept by the
census's new FACT F, probe-paired — reverting the fallback reds FACT F on exactly
that fixture), and a DRAIN OF A SOURCE THE QUERY NEVER NAMES (`w_unproven` pulls
six rows out of a param no clause mentions, because this pass answers only for
the sources the ENTRY QUERY NAMES while the prelude materializes every source the
natspec REGISTERED). That drain is pinned, not fixed: removing it is an emitter
change with its own artifact delta.

⚠⚠ AND THE BARE DELETION IS REFUSED — with a number, not an opinion. (S2j then
retired the proof WITHOUT deleting its grounds; this probe is why that was the
only admissible route, and it is re-run against the inverted walk above.) PROBE
(S2h, restored to a byte-identical source with a green checkpoint after): the walk
replaced by an immediate `return`, i.e. the proof deleted with nothing in its
place. 11 artifacts move, +2,953 bytes, every diff the same
shape (`let mut __rel_s: StepsIter = …` becomes a `Vec` plus a drain loop);
`logos_09_plan_nodes` and `logos_09_plan_ground_census` red; the byte-pin
`logos_09_slice_scan_codegen` stays GREEN; and EVERY RUNTIME FIXTURE STILL
PASSES. That is the argument: deleting the proof does not break the corpus, it
makes every query quietly pessimal, and the corpus cannot separate a correct plan
from a conservative one because both return the same rows — only a count gate
can. §4's replacement is not "delete this and materialize everything", it is that
the second read becomes UNSPELLABLE. That needs the drain arm to become
`Drain -> Buffer`, which this ADR lists as owed and NOT STARTED. Until a TYPE
refuses the second read, this function is the only thing that refuses it at all.

⚠ THE CORPUS WAS NOT TOUCHED. No fixture was added, edited or retired for this
census: a fixture written to witness a ground would be the gate grading its own
homework, and the fifteen unwitnessed sentences are recorded as debt instead.
Two fixtures cannot compile standalone (`wql_mapping_cross_module_e2e`,
`wql_wref_field_pkg` — each `use`s a companion package the suite supplies through
a lib path); they are pinned BY NAME, so a third failure, or one of these two
starting to compile, is red.

PROVED TO BITE, BOTH DIRECTIONS, ONE AT A TIME, RESTORED TO A BYTE-IDENTICAL
SOURCE WITH A GREEN CHECKPOINT BETWEEN THEM: (1) silencing the container absence
line in `access_plan_explain` ⇒ 22 reds — 187 silent materializations,
`already a buffer` 187 → 0, `MG_CONTAINER` reported as a sentence nothing
reaches; (2) pushing every prelude node TWICE ⇒ 6 reds — five per-fixture
artifact comparisons (`deem_batch_scan_drain`: 6 nodes against 3 `__it_`
bindings) plus the corpus total 7 → 14. Each probe cost a full stdlib rebuild and
a re-run of the sweep in both the perturbed and the restored state.

STILL OWED BY S2, and not started: the drain arm becoming `Drain -> Buffer` with
the scan reading the Buffer as a one-packet stream; the SLICE arm's death (the
byte-pin's measured-equal transition); `Sort` replacing the `__ix0` permutation;
`Arrange` holding ROWS; the death of the SHAPE readings of
`rel_stream`/`rel_iter`/`is_slice` (they are still read by the SCAN side —
`rel_src_streams`, `params.logos:657`, and the slice arms of `emit_simple` /
`emit_find` / the join and aggregate emitters); and the deletion of
the single-pass proof, whose 9 grounds each still need a node fixture first.
⚠ AS OF S2f/S2g the SHAPE readings are dead (three emitter sites now ask the
plan's node through `MacroParams::rel_src_unmaterialized`; the survivors read a
DECISION or an OFFER and carry the S1 gate's correction at the line). As of S2h
the 9-ground precondition is DISCHARGED — 8 witnessed, 1 refuted and deleted —
and the bare deletion is nonetheless refused with a measured probe. As of S2j the
proof is RETIRED BY INVERSION instead — `plan_insert_drains` carries every ground
onto a node, `once`/`owhy`/`access_plan_plan_nodes` are deleted, corpus
text-preserving. All three are recorded in §4 above.

`Buffer<R>` implements every capability; "a Vec is an eagerly-drained stream,
never the reverse" stops being a comment and becomes the type lattice.
The single-pass proof and the `rel_stream`/`rel_iter`/`is_slice` booleans are
deleted in favor of typed uses: consuming a non-`Rewind` stream twice cannot be
spelled; the plan inserts `Drain` and says why. (S2j landed the second clause —
the plan inserts the `Drain` and `explain()` says why, and the proof is gone. The
first clause, the UNSPELLABILITY, is what remains.)

## 5. Memoria mapping: the cursor moves by LEAVES

Landing constructors are unchanged in ROLE (the pushdown plane of ADR 0024 S6
is untouched); what changes is what they return: a leaf-batch stream. ⚠ THE
NAMES IN THIS PARAGRAPH ARE THE PRE-S1 ONES. As of S1 (2026-08-13) the
declared landings are `__ctr_bat_/bfrom_/bupto_` and their per-row twins
`__ctr_at_/from_/upto_` are DELETED (ordered_map at S1, positional at S1b) —
the role survived the rename, which is exactly the claim this paragraph makes. `next()` hands out the CURRENT leaf's window as a
`ColsBatch` (the `hi` bound trimmed inside the leaf via `lower_bound`), then
descends for the next leaf. One descent per LEAF instead of a container method
per row; n/fanout descents per scan — the asymptotics the CoW no-sibling-
pointer rule already imposes, with the per-row constant gone.

LANDED (S0, 2026-08-11) in the ordered_map family emitter
(`stdlib/lcm/canon/container_item.logos`), BESIDE the per-row `{N}Walk`, which
stays until S1: `{N}LeafBatch` (one leaf window) + `{N}LeafWalk` (the stream)
with the four landings of the per-row form, as free fns (`__ctr_brows_`/`bat_`/
`bfrom_`/`bupto_`) and as handle methods (`leaf_batches[_at/_from/_upto]` — a
family's type has no spelling a human can write, so the free form has no door
for a hand-written consumer). MEASURED against the container's own per-row
cursor over 1000 entries in 4K leaves: same rows, same order, same sums, and
**8 `next()` calls for 1000 rows** — per LEAF, not per row. Three deviations
were forced. Two of them were the SAME compiler defect (D1) at different sites
and are re-measured below on the tree that closed it — the first is GONE, the
second survives only as a pointer-identity check; the third was never a defect
at all. Each bullet carries its own re-measurement:

* **`Option<B>` LAUNDERED THE BORROW — FIXED, re-measured 2026-08-11 (same
  day).** As first measured: `Option::Some(b)` takes the batch BY VALUE and
  provenance did not survive a by-value generic constructor, so the batch (or
  the un-unwrapped `Option`) held across `insert` COMPILED — §7 rung 1 was not
  enforced through §1's own pinned signature. THAT WAS A COMPILER DEFECT (D1),
  not a property of the protocol, and it is closed: a loan follows the HOLDER
  graph, so composition into the `Option` and extraction by `unwrap` both keep
  it. `next_batch()` is now SOUND — the trait door is no longer a hole. Pinned
  as a PAIR: `fail/ctr_family_mut_while_next_batch` (the D1 repro verbatim, now
  refused with `cannot borrow 'c' as mutable: 'c' has shared borrows`) +
  `pass/ctr_family_next_batch_then_mut` (scope closed, compiles AND answers —
  and its second scan SEES the mutation, so the door did not lose its results
  to the fix). The SPLIT pull `advance(&mut self) -> bool` +
  `batch(&self, c: &{N}) -> {N}LeafBatch` STAYS — it is now an ergonomic form
  and a witness-checked one, not a soundness workaround — and keeps its own
  pair (`fail/ctr_family_mut_while_batch` + `pass/ctr_family_batch_then_mut`).
  Both pairs stay: they are two different laundering routes to the same freed
  leaf and a checker can lose one without the other.
* **`&self` DID NOT TIE EITHER — re-measured 2026-08-11, and this reason is now
  HALF stale.** As written: a `&{N}LeafWalk` argument contributes the borrow of
  the stream LOCAL, not the `&{N}` borrow that local carries, so `batch()` takes
  the container as a WITNESS. After D1 the hop matters, and `batch(&self, …)`
  sits on the RECEIVER hop, which re-exports now (§2 ⚠⚠, probes `p3`/`p2`: the
  method form refuses, the free-fn `&`-arg form still admits). So the witness is
  no longer load-bearing FOR THE BORROW at this site. **It stays anyway**, for
  the reason it was always also doing: it is the pointer-identity check (abuse
  direction measured — a witness from another container traps, SIGABRT 134), and
  that check is not something the borrow rule replaces. Whether the argument can
  be dropped is an ERGONOMIC question for S1, not a soundness one, and it must
  not be dropped without replacing the identity check.
* **The batch is a PAIR of `ColsBatch`es, not one.** This family's leaf keeps
  key and value in two separate single-column slots, and a `ColsBatch` is one
  SLOT's window. Nothing is re-implemented — both halves go through
  `batch.logos`'s constructors, its `size()` trim and its witness check — but
  S1 must decide whether `ColsBatch` grows a multi-slot form or the leaf grows
  a two-column slot. The delta is recorded at the type, not silently married.

Reading the batch through the TRAIT method also binds the arm at the trait's
declared parameter (the `deem_ctr_family_streams` defect, verbatim), which is
the second reason the inherent door exists.

**THE SPELLABILITY LAYER — LANDED (S1, 2026-08-13).** S0 recorded that "a
family's type has no spelling a human can write", and left the handle methods as
the only door. That door is not enough: a hand-written consumer cannot name the
BATCH TYPES at all, so it can neither take a walk as a parameter nor return a
batch. `CtrLeafFamily` (a SECOND trait beside `CtrFamily`, in
`stdlib/lcm/canon/metaclass.logos`) closes it with `type LeafBatch` +
`type LeafWalk`, and the WORKING SPELLING — measured end-to-end — is a `pub type`
alias over the projection:

```logos
pub type LedWalk  = <typeof(Led) as CtrLeafFamily>::LeafWalk;
pub type LedBatch = <typeof(Led) as CtrLeafFamily>::LeafBatch;
fn first_batch(s: &mut LedWalk) -> LedBatch { return s.next_batch().unwrap(); }
```

Four facts, each measured, none of them guessable from the shape:

* **A SECOND TRAIT, NOT TWO MORE MEMBERS ON `CtrFamily`.** `impl CtrFamily` is
  emitted for ordered_map, multimap AND vector; the leaf-batch types come out of
  the FSE ordered_map arm alone. Members on `CtrFamily` break the vector family's
  own emission (`unknown type 'Hs…LeafBatch'`, caught by the live
  `pass/container_item_e2e`); and guarding the separate impl on `kind ==
  ordered_map` up in the factory still breaks the str-valued (VLE/volume)
  ordered_map (`pass/metaclass_str_generic`). The impl is therefore emitted from
  INSIDE the same block that emits the two types, which makes co-emission
  structural rather than a restated condition. This is §3's own rule applied one
  level down: existence of a capability = trait membership.
* **THE ALIAS IS REQUIRED, AND IT IS LOAD-BEARING.** The projection written
  INLINE in a plain `fn` signature is refused — `typeof(container Led): its
  config const 'LedCfg' did not resolve to a WritStatic document` — a ROUND-ORDER
  fact about `typeof(container …)` in a signature checked in the round the
  container lowers, NOT a fact about assoc types: the byte-identical error
  appears with `::Handle`, which has existed for months. (A `deem` signature is
  unaffected; it lowers later.) Behind a `pub type` it resolves at mono, and it
  does not erase: `fail/ctr_leaf_family_wrong_family` points the alias at a
  SECOND family and the call is refused with both CFG hashes in the message.
* **NO BOUND ON `LeafWalk`.** `type LeafWalk: BatchStream<Self::LeafBatch>` does
  compile and would state §1 at the trait, but it needs `use logos.lang.stream;`
  in `metaclass`, which EVERY `create_ctr` consumer imports — hubbing
  `BatchStream::next` into all of them and making `it.next()` ambiguous against
  `Iterator::next` tree-wide. That is the exact hazard `logos.lang.stream`'s
  header records as the reason it is out of the prelude. The emitted
  `impl BatchStream<{N}LeafBatch> for {N}LeafWalk` is the fact; `has_trait` is
  how the planner asks.
* **THE EMITTER DOES NOT NEED ANY OF THIS.** Emitted code already resolves the
  bare hash names, because the chunk imports the producer's defining package via
  `native_use_text` — which is what makes today's streamed prelude work. The
  spellability layer is the door for HAND-WRITTEN consumers (tests, oracles,
  §12's returned streams), not a prerequisite for the scan-shape collapse.

TWO BOUNDARIES OF THE SPELLING, recorded because a green fixture would otherwise
hide them. (1) The projection is spellable BARE — parameter, return, `let` — but
NOT as a type ARGUMENT to a generic: `-> Option<LedBatch>` kills the metaprog
MLIR pass (`unknown field type in 'SkipWhileIter$G2$OptionIter$G1$<error>::
LeafBatch…'`), so consumers take the `Option` by inference. (2) The projection is
NOT obligation-checked: `<typeof(Vec1) as CtrLeafFamily>::LeafWalk` for a family
that does not implement the trait resolves to `<error>` and the compile continues
(one variant then produced the unrelated `call to unsafe function 'take' requires
unsafe context`). `fail/ctr_leaf_family_volume_refused` pins the refusal that DOES
exist — the volume handle has no `leaf_batches` — and names the missing one. (It
was written over a VECTOR family at S1 and re-aimed at S1b, when the vector arm
gained a producer of its own and the claim stopped being true of it.)

`{N}LeafWalk` also gained the traversal pair here (§3): `Bidirectional::prev` via
a `retreat` that mirrors `advance` (one descent, per leaf, clamped to the
landing), and `RandomAccess::seek_nth` as field arithmetic whose descent is the
NEXT pull's — so repositioning plus reading costs one descent in total, which is
the claim §3 makes.

No compilation firewall exists in Logos Memoria — the batch type is the leaf's
own typed view, fully inlined into the query code: zero-copy AND
zero-abstraction at once. The C++ original lifts leaf data through the CF via
lightweight views; here the PdtBuf-backed column wrapper is passed straight to
the emitted loop.

## 6. Other storages

* Heap `Vec<T>`/`&[T]`: ⚠ CORRECTED (S6 audit, 2026-08-14 — the same
  falsehood as the writ bullet below, struck the same day): a Vec producer
  emits `Vec` + `as_slice()` + the indexed slice arm, NOT a stream (measured:
  the `deem_rowmajor_batch_source` twin producer in the same dump). The
  row-major CONSUMER exists since S6-A (`RowMajor` + the `m` letter) — a
  Vec-SHAPED source CAN join the plane, but no corpus source does yet.
  `HashMapIter`: row batches, no order, point-probe ops as today.
* Writ: ~~the prebuilt `Vec<WritEdgeRow>` is a one-packet stream now~~ — **FALSE
  AS SPELLED, CORRECTED S6 (2026-08-14) rather than "formalized".** Measured on
  the 11 corpus dumps that carry `writ_graph_edges` (45 call sites): the emitted
  code is `let __rel_g: Vec<WritEdgeRow> = writ_graph_edges(g);` +
  `.as_slice()` + the SLICE arm's indexed loop. Zero `next_batch`, zero
  `Buffer`, zero `BatchStream` ON THE WRIT PATH ITSELF (⚠ scoped by the S6
  verify: a raw grep over the 11 writ-carrying DUMPS returns 1 `next_batch`
  and 49 `Buffer<` — the former is a Memoria LeafWalk sharing the dump, the
  latter S5's query-output surface; the per-path claim is what holds, and
  this sentence is spelled so the grep it invites agrees with it). What IS true is the weaker
  PLAN-level fact the trace already prints — `MG_CONTAINER`, "no
  materialization on already a buffer: the producer hands back a container,
  which IS a buffer — nothing is built". That is a MATERIALIZATION ground, not
  a stream, and the difference is the whole subject of §4. A `WritWalk` cursor
  streaming document-walk batches remains the real work and is NOT landed; see
  the S6 slice entry for the measured refusal of the intermediate step
  (wrapping the prebuilt `Vec` in a `Buffer` and declaring it), which costs 29
  inserted materialization nodes and buys nothing.
* rel fixpoint outputs and semi-naive deltas: ⚠ CORRECTED (S6 audit, same
  day): 1,691 `__rel_*_sl` and 540 `__dl_*_sl` INDEXED SLICE landings across
  the corpus against 12 drain-created Buffers — these are NOT one-packet
  streams today; the epoch-as-batch vocabulary exists (§10, `Epoch<R>`) and
  the empty-epoch tick is pinned, but the fixpoint plane itself still lands
  slices. An EPOCH is one batch — including the empty one, a meaningful tick
  (§10) — at the VOCABULARY level; the consumption seam is the declared next
  step.

## 7. Lifetime and pinning — what makes zero-copy sound

**The altitude theorem.** CoW + structural sharing give: a block reachable
from snapshot S is LIVE while S's tree shares are held, and IMMUTABLE if S is
committed. Therefore a cursor never pins a BLOCK for liveness; it needs
exactly two properties:

* **existence** — S's tree shares are not released (granularity: snapshot);
* **stability** — no mutation through THIS snapshot while reading
  (granularity: container; purely BC).

**The ladder (RC minimized, BC maximized):**

1. *Stability — zero counters.* Read ops take `&self`, mutations `&mut self`;
   the batch is `#[borrow_carrying]`-tied to the cursor, the cursor to `&C`.
   `insert` while a cursor lives is a COMPILE error (the view.logos idiom, one
   level up). This also legalizes scanning an uncommitted (writable) snapshot:
   BC supplies the stability that immutability would.
2. *Existence — one already-paid count.* The chain batch ← cursor ← `&C` ←
   handle-OWNING-the-snapshot-wrapper extends snapshot liveness transitively
   through the wrapper count that `MemorySnapshot::remove` already documents
   ("the entry stays until the LAST wrapper closes"). No new increments on the
   read path — not per block, batch, or cursor.
3. *Owned cursor — the only place counting is added, opt-in.* A cursor that
   must cross a scope (task handoff, REPL, a query stream ESCAPING its
   caller's scope) clones the wrapper: one pin per cursor LIFETIME. It is
   legal ONLY over a COMMITTED snapshot (`status == SNAP_COMMITTED`, one
   branch at open): it borrows no `&C`, so stability must come from
   immutability. It is the only Send shape. One walk core, two constructors —
   the owned form wraps the borrowed one plus the pin token, never a second
   implementation.
   ⚠ A STREAMING QUERY OUTPUT IS NOT AUTOMATICALLY THIS CASE. A stream a
   `deem` RETURNS crosses the query FN's boundary, not the caller's scope: as
   a borrow-carrying return value tied to the source arguments
   (`-> Result<QStream, ElError>` over a generated `#[borrow_carrying]` type —
   the shape family producers already have; §12 records why the `+ '_` this
   line used to carry does not exist),
   it composes into pipelines under rung 1–2 with ZERO counting. Rung 3 is
   reached only when the stream ESCAPES the handle's scope. §12.

**Why not RC-per-cursor:** a join probe opens a cursor PER OUTER ROW
(`__ctr_bat_(c,k)` — `__ctr_at_` pre-S1); counting there is per-row traffic bought for nothing BC
does not already give. Why not BC-only: the owned cases above would become
inexpressible rather than safe.

**The pin contract at the Snapshot seam — store-invariant, two parts:**

| part | altitude | MemoryStore | SWMR | OLTP |
|---|---|---|---|---|
| "snapshot alive" | snapshot | wrapper hold (existing count) | GC ROOT: pinned snapshots are roots for the tracing GC | epoch/lease |
| "leaf image resident" | batch (= leaf) | **no-op (unit type)** — handles ARE addresses, no eviction | cache-page pin: the ARC+BC pair | epoch covers |

RECLAMATION is per-store and NOT part of the contract: MemoryStore's CoW
per-block RC is its memory management substrate and stays (structural sharing
is runtime- and data-dependent — BC cannot express it); disk stores do NOT use
CoW RC as the primary mechanism — they combine an ARC block cache with a
tracing GC. Residency-during-use is the **ARC + BC pair**: BC yields
deterministic pin windows (batch borrow = pin; Drop/advance = unpin — a leaked
pin is unspellable), ARC evicts only among unpinned pages. One pin pair per
batch, i.e. per leaf — never per row or per access. Both pin parts are
METHODS ON THE GENERALIZED `dyn Snapshot` (§7b) — a uniform call whose body
is a no-op in MemoryStore — so the emitted query code is not merely one shape
for all stores: it is one CODE, store-agnostic, with no per-store
substitution left on the query side.

**Requirements this puts on the Memoria API (none exists today):**

1. `Walk`-style cursors hold `*const C` — a convention, checked by nothing.
   Becomes a `#[borrow_carrying]` tie to `&C`.
2. The handle `{snap: *mut MemorySnapshot, ctr_id}` is a raw pointer with no
   tie to the wrapper whose Drop releases trees — AND it names a concrete
   store. Becomes owner of (or borrower from) a wrapper of the GENERALIZED
   snapshot (`dyn Snapshot`, §7b) — the knot of ladder rung 2, generalized on
   day one rather than at store #2. **First slice: without it the borrow
   chain does not close.**
   ⚠ SUPERSEDED AS A DESCRIPTION OF THE TREE (re-measured 2026-08-11): the
   sentence above was stale when written. The generated handle has been
   `{snap: Arc<dyn Snapshot>, ctr_id}` — an OWNER of the generalized wrapper,
   two-tier interface included — since `c03c30c3`, an ancestor of the very
   tree this ADR was measured on. Rung 2 therefore existed already; what did
   NOT exist was rung 1, and it was missing on BOTH sides: the generated
   mutators took `&self` and the generated cursors held raw pointers with no
   recorded borrow, so `insert` while a cursor held the adopted leaf was
   admitted — a dangling view, since `insert` `tree_release`s the old tree.
   Landed with req. 1 below: mutators take `&mut self`, all six generated
   cursor/walk types are `#[borrow_carrying]`, and the refusal is pinned as a
   PAIR (`fail/ctr_family_mut_while_cursor` + `pass/ctr_family_cursor_then_mut`)
   because it is a conjunction — either half alone stays permissive.
   Declared, not closed here: `tree()` / `block()` still hand out raw views
   around the borrow (the privileged tier's hatch), and
   `create_ctr_rc`/`open_ctr_rc` (zero consumers, measured) cannot mutate
   through `Rc` at all under the new signatures.
3. The Snapshot seam vocabulary gains the two-part pin capability (the table
   above), spelled per store by the emitter.
4. `remove()`/future GC currently see only the wrapper count; after (2)
   cursors are covered transitively — no separate mechanism.
5. The owned cursor owes a Drop-unpin (the `*r = new` drop path is closed
   since `df79e048`; the substrate exists).
6. A borrow-carrying cursor must be unable to cross a task boundary — the
   owned form is the only Send shape. The concurrency model has no such
   predicate yet; it must grow one.
7. (§12, PARKED until the `queued` form) A lazy-producer API — a generator on
   pinned fibers or a task feeding a bounded queue — with KILL-ON-DROP:
   dropping the stream must stop the producer and run its frame's drops, or
   the leaf pin leaks with the frame.
8. (§12, PARKED until the `queued` form) Borrows THROUGH the producer: the
   stream handle is borrow-carrying over the deem's source arguments; the
   producer must not outlive them — the producer-side half of requirement 6.

**Known limit, stated:** two handles to the SAME container in one snapshot
alias past BC — the same limit view.logos declares intra-node. Backstop is a
store-level check, not the type system.

## 7b. The dyn boundary (Victor, 2026-08-10: "предусмотри это сразу")

The store-facing surfaces are generalized NOW, not at store #2:

* **`dyn Snapshot`** — for container work every store's snapshot has the SAME
  API; the handle holds a wrapper of `dyn Snapshot` (the `Arc<dyn Snapshot>`
  shape `store_fork_arc` already returns), and the two pin parts (§7) are its
  methods. This SUPERSEDES the container⟂store deferral ("`trait Snapshot`
  stays a minimal marker; capability→trait when store #2 arrives") for the
  INTERFACE; the emitter-parameterized decision (b) survives only INSIDE the
  container family's implementation, where per-store node specifics live.
* **`dyn` container interfaces** — a family's consumer-facing surface (the
  landing constructors, the batch source) hides which store implements it:
  `next_batch(&mut) -> Option<ColsBatch>` is dyn-able precisely because the
  batch is one concrete form (§2). Payoff: a compiled query binds the CONFIG,
  not the store — one binding serves every store carrying that family.
* **The batch itself — never dyn.** PdtBuffer is one shape for all stores;
  the inner loop over a batch is monomorphic and inlined.

**The frequency invariant that prices this:** every dyn call sits at
per-query (open, landing), per-descent-node (`get_child`), or per-LEAF
(`next_batch`, pin/unpin) frequency. **No dyn call is per-row.** The batch
protocol is what holds that line — which is the second reason (after §12's
fiber suspend) that the batch, not the row, had to be the protocol unit.

## 8. Aggregates (later slice)

γ over a batch stream is a single-pass fold — "the aggregate revisits rows
after grouping" (today's materialization ground) is an emitter shape, not a
semantic need, for accumulator aggregates. A fold over `ColsBatch` runs down a
column slice — the vectorizable shape; taken when the two ~720-line aggregate
emitters are unified, not before.

LANDED (S4-naming, 2026-08-14) — THE GROUP FRAME ENTERS THE PLAN VOCABULARY.

THE OBLIGATION, AND IT IS A NUMBER. The aggregate emitter materializes a family
of per-group columns and the plan named NOT ONE of them: `MG_REGROUP` grounds the
aggregate's DRAIN of its base source and says nothing about the state the
grouping itself builds. `let mut (__g_key|__g_cnt|__g_row|__ga_*|__gf_*)` was
therefore the largest unnamed class in criterion 1 — 1,122 bindings when the
class was first counted. THE S4 FOLD TOOK IT TO 380 BEFORE THIS STAGE NAMED
ANYTHING, and the two halves are separate results: 488 `__gf_*` left by being
DELETED (the finalize pass), and `__g_cnt` 146 → 13 / `__g_row` 146 → 7 by being
put behind their consumers (S4b: `avg` is `__g_cnt`'s only reader; S4c: `__g_row`
belongs to the representative class alone).

THE DECISION: FIELDS ON THE AGGREGATE THE PLAN ALREADY CARRIES, NOT A NEW NODE
KIND. The plan's node kinds are ADAPTERS over a stream (`Drain`, `Sort`,
`Arrange`) — things that materialize something a later phase reads back. A group
table is the OPERATOR'S OWN STATE, as a join's build side is, and §4 already
models that build side as an `Arrange` rather than as a `Join` node. A
`GroupBy`/`Fold` KIND would put an operator into a vocabulary of adapters and
make `explain()` print two different kinds of thing under one word. γ is already
in the plan (`RQuery::Aggr`); what was missing is the STATE'S PRICE and its
GROUND.

⚠ AND THE FIRST ANSWER — "`Arrange` is the group table's kind, it is a keyed
table built to be probed" — IS REFUSED ON A MEASUREMENT. The census pins
`#(arrange nodes) == #(emitted `__hm`/`__hs`/`__bt` bindings)`, 598 == 598, per
fixture and in total: an arrange node is the claim that the artifact BUILT AN
INDEX. The group table is a linear `==` scan over `__g_key` (S4's MVP dedup) and
builds no index, so 152 group tables spelled `arrange` would have falsified that
identity and forced it to be re-partitioned by ground — a sensor weakened to
admit a claim the artifact does not support. The group table is reported the way
`sort_key_vector` reports `__ks`: as a FIELD, with its own verdict word. 598 ==
598 is unchanged across this stage, and that is the stage's sharpest control.

THE FOUR GROUNDS (`access_plan::AG_*`), authored at the DECLARATION SITE
(`join_sel`, for the same emitted-text reason `arrange_node` lives there — a
`use access_plan` in `rexpr_walk` would put one `use` line into every corpus
dump), each firing inside the branch that declares its column:

| ground | verdict | column | corpus | why it is its own ground |
|---|---|---|---|---|
| `AG_GROUPS` | `group frame` | `__g_key` | 152 | one row per distinct key; the fold's probe target, priced with the frame's WIDTH on its own line |
| `AG_ACCUM` | `accumulator` | `__ga_<out>` | 208 | one cell per group per aggregate, named by output and typed by the ACCUMULATOR type |
| `AG_COUNT` | `group count` | `__g_cnt` | 13 | S4b — `avg`'s denominator and nothing else's |
| `AG_REP_ROW` | `representative row` | `__g_row` | 7 | S4c — the projection names a source row; its ABSENCE is what a single-pass fold IS |

380 = 152 + 208 + 13 + 7, and the criterion-1 numerator moves by exactly that:
the class is now 0 unnamed, with `__gf_*` at 0 by deletion. `AG_COUNT` is NOT
folded into `AG_GROUPS` although the design bracketed it there — a column present
in 13 of 152 group tables cannot be counted by a census that counts ground lines
if it shares a ground with the table, and its sentence is a different sentence.

THE GATES. `logos_09_plan_ground_census` (tier_full) grows by FACT H: plan lines
== emitted `let mut` bindings per family, per fixture AND corpus-wide, beside
per-family pins — the pins catch a class that shrank, the identity catches a plan
that stopped describing the artifact, and a defect moving both sides together
fails the first. `logos_09_group_frame_naming` (tier_commit) is the L2 half and
asserts what a corpus total cannot: PER-QUERY attribution, by slicing the trace
at its `group frame` lines — a corpus total of 2 representative rows is equally
consistent with "the two queries that need one have one each" and with "one query
has two and the class predicate is broken". Both proved to bite, one perturbation
at a time, each reverted to a byte-identical source with a green checkpoint
between: the rep-row call fired outside its `if !pure_group` branch reds 3 claims
at L2 and 59 in the census (57 per-fixture identities + the pin + the total).

THE CONTROL: the corpus snapshot is BYTE-IDENTICAL across this stage — 165 dumps
/ 7,067,309 bytes, `diff -rq` empty. The group frame enters the plan as TRACE,
so nothing it says may move an emitted byte, and nothing did.

## 9. What this deletes

The `is_slice`/`rel_stream`/`rel_iter` triple (one fact, now a type); the 17
`__ix0` sites (one `Sort` adapter); the two bucket shapes (one `Arrange`); the
drain-vs-stream prelude split (one protocol, `Drain` explicit);
the single-pass proof (typing + adapter insertion — adapter insertion landed at
S2j, typing owed); the per-row container-method walk (leaf batches).

⚠ TWO CORRECTIONS TO THE LIST ABOVE, both measured on the tree rather than read
off it (2026-08-13, S1).

* **`is_slice` IS NOT A THIRD SPELLING OF THE SCAN SHAPE, and S2 must not
  delete the bit.** Only ONE of the triple reaches the scan-shape decision:
  `prm.rel_src_streams(src)` over `rel_stream`. `rel_iter` is the SOURCE'S
  OFFER (read at `access_plan.logos:210`, overwritten from the chosen op at
  `plan_walker.logos`'s `plan_apply_access`) and `rel_stream` is the PLAN'S
  DECISION — offer ⟂ decision is the whole "a rel scanned twice must still be
  drained" rule, not two spellings of one fact. `is_slice` carries two
  UNRELATED facts: the source-vs-scalar discriminator (`params.logos`'s
  `is_source`/`is_scalar`) and the incremental arm's "exactly one slice
  parameter" admission. What S2 deletes is the SHAPE reading; the bit survives.
* **The per-row container-method walk IS DELETED FOR BOTH ARMS (ordered_map
  S1, positional/vector S1b, both 2026-08-13)** — `{N}Walk`, its `Iterator`
  impl and the four producers `__ctr_rows_/at_/from_/upto_` are gone from
  `stdlib/lcm/canon/container_item.logos` entirely; each family declares its
  leaf-batch producers and both consumers of a batch producer emit §1's one
  shape. ⚠ SCOPE, corrected by the S1b audit the same day: FAMILY sources now
  carry ONE pull shape (both arms) — but the EMITTER still carries three live
  branches at each scan site (batch / per-row `.next()` / indexed slice), and
  the two non-batch ones are exercised by the corpus. The per-row branch
  serves HAND-WRITTEN Iterator sources (e.g. deem_source_size's StepsIter) —
  collapsing those IS S2's `Drain`/`Buffer`, not a family question; the slice
  branch is pinned byte-for-byte by logos_09_slice_scan_codegen until S2 takes
  it deliberately. Family-arm evidence, measured on the emitted artifact: `container_item_e2e`'s vector scans now read
  `Hs…LeafWalk = __ctr_brows_Hs…(v)` / `__ctr_bfrom_Hs…(v, 2i64)` with the
  `next_batch()` outer pull and the row spelled `(pos_at(j), val_at(j))`, and
  NO emitter change was needed for the collapse: `rel_batch` comes off the
  natspec `b` flag, so naming a batch producer in `impl PositionalSource` is
  the whole switch. S1b's producer differs from S1's exactly where the family
  does — ONE `ColsBatch` (single FSE64 slot), no `{N}Leaf` type to adopt (the
  leaf's row count reads through `NodeView::col(0).size()`), a purely ORDINAL
  bound (no in-leaf `lower_bound`, nothing for a key bound to disagree with),
  and a batch that carries `p0`, the absolute ordinal of its window's row 0,
  because `pos` HAS NO CELL. `RandomAccess::seek_nth` is this family's NATIVE
  operation rather than a derived one. The
  drain-vs-stream prelude split is NOT yet deleted — it is now a split over one
  PULL PROTOCOL (both legs pull batches), which is what makes `Drain` an
  adapter rather than a rewrite when S2 takes it.

## 10. DBSP seam

A delta stream is `BatchStream<Weighted<R>>`; an epoch is one batch; an empty
epoch is `Some(empty)` — a tick, not an end. `Arrange` is DBSP's arranged
input; a `Buffer` kept across epochs is z⁻¹. `[REC]` The weight does NOT enter
the core row model: ordinary queries pay nothing; the incremental tier rides
the same plane with a wrapped row type.

LANDED (S6, 2026-08-14) as VOCABULARY WITH A BOUNDARY FIXTURE AND AN UNMOVED
EMITTER — the split is stated first because a type with no consumer that reads
as "landed" is the recorded green-over-a-branch-that-never-executed shape:

* `logos.lang.stream`: `Weighted<R>` (`{ row: R, w: i64 }` + `at`/`insert`/
  `retract`), `DeltaBatch<R>` = `RowsBatch<Weighted<R>>` = `&[Weighted<R>]`,
  and `Epoch<R>` — a `#[borrow_carrying]` ONE-PACKET stream over the caller's
  `&[R]` at ONE weight, `BatchStream` + `RowMajor` + `Rewind` + `SizedStream`.
* `logos.mem.stream`: no new type. `Buffer<Weighted<R>>` IS the arranged
  mixed-sign delta, and a `Buffer` kept across epochs IS z⁻¹ — recorded on
  `Buffer` itself, next to the ownership note that explains why the delay owns
  (`Epoch` borrows) rather than in a doc a reader of the type never opens.
* `tests/logos/pass/stream_weighted_epoch_seam` (labels `logos;pass;corpus;suite_semantic_core`, i.e. L2): the four §10
  clauses as VALUES. The empty-epoch tick is pinned by making the consumer
  answer `Some(empty)` and `None` DIFFERENTLY (`0` rows folded vs `-1` = no
  epoch happened), so a producer that reported the empty delta as exhaustion
  returns the other code rather than merely folding zero rows.

TWO SPELLINGS, AND THE SECOND IS FORCED BY THE TYPES. `Epoch<R>` (weight beside
the packet) and `Weighted<R>` (weight per row) are not a redundancy: `&[R]` and
`&[Weighted<R>]` are different layouts, so handing a borrowed constant-weight
delta out as `DeltaBatch<R>` would MATERIALIZE a `Vec<Weighted<R>>` the caller
never asked for — a compiler-inserted intermediate materialization, which is
criterion 1's subject. The generalization is real where it is needed: one
`Buffer<Weighted<R>>` packet carries +1 and −1 rows, which the scalar surface
cannot express at all (today it must be split into two `_apply` calls), and the
fixture folds that packet against the two constant-weight epochs it replaces.

ORDINARY QUERIES PAY NOTHING — MEASURED, not asserted: the whole-corpus
artifact snapshot is byte-identical across this stage (168 dumps / 7,688,158
bytes over the 184 `deem_*`/`wql_*` fixtures, `diff -rq` empty). The new
fixture is `stream_*` by construction — it compiles no query — so it is not
inside that population and cannot flatter it.

THE CONSUMPTION SEAM IS THE PLANE'S DECLARED NEXT STEP, AND ITS COST WAS
MEASURED RATHER THAN GUESSED. The seam is `<q>_apply`'s parameter list:
`(__h, __src: i32, __wd: i64, <params VERBATIM>)` → `(__h, __src: i32, d: &mut
impl BatchStream<…>)`, with `_epoch`/`_retract` staying as forwarders that wrap
the caller's slice in an `Epoch` (which is exactly "an epoch is one batch", and
keeps the empty tick representable — an empty `&[]` is already a legal no-op
fold returning `Ok(0)`). What stops it today, over the corpus snapshot:

* **THE DELTA'S PARAMETER LIST IS THE ORDINARY QUERY'S PARAMETER LIST.** In
  70 of 70 emitted `_apply` fns the parameters after the `(__h, __src, __wd)`
  prefix are TEXTUALLY IDENTICAL to the batch fn `<q>`'s own parameter list
  (measured over the 168 dumps). `rexpr_walk` states this as the rule ("`<q>_epoch`
  takes the query's parameter list VERBATIM — the same one `<q>()` takes"); the
  consequence is that re-spelling the delta as a stream either re-spells every
  ordinary query's signature — which is the corpus-wide change this stage's
  control exists to forbid — or forks the two lists, which is the divergence
  that rule was written to prevent. Population: 70 `_apply`, 102 `_epoch`,
  88 `_retract` across 26 of the 168 dumps. ⚠ S6 audit: re-measured 71/103/89 across 27 of 169 — a uniform +1 not attributable to the fixture added since; the IDENTITY claim re-derived independently and holds at 71/71 parameter lists. Derivation: grep -c over the corpus user dumps, command in the criteria doc §2.
* **DRed's PHASES ARE NOT A DELTA AT ALL.** All 124 emitted `__wql_<q>_scc<c>`
  phase fns (`_od`/`_odp`/`_cpt`/`_i`) take `&[…]` of DERIVED facts whose sign
  is fixed by PHASE (`__rm_<m>` at −1, the post-`_cpt` suffix at +1). Wrapping
  those rows would attach a weight to a row that has none — and §"WEIGHTS MAY
  NEVER FLOW NAIVELY INTO AN SCC" (`incr_eligible`) says why the answer is not
  "carry `__wd` through": counting derivations through a cycle diverges. So the
  seam stops at `<q>_apply`; DRed is NOT in it.
* The stored state needs no change and gets none: `__s<k>: Vec<R>` + its
  sidecar `__s<k>w: Vec<i64>` is already the Z-set in normal form with the
  weight OFF the row, and `__edb` is deliberately weightless (a set). That is
  §10's `[REC]` already true by construction — `Weighted<R>` is a boundary
  type, not a change to state.

## 11. Vocabulary home

`[REC]` Capability traits (`Rewind`, `SizedStream`, `OrderedBy`, `Landed`,
`BatchStream`, batch types) live at the language tier next to `Iterator`
(`logos.lang.iter` or a sibling `logos.lang.stream` module) — they are
general-language notions (think ExactSizeIterator in core). The query plane
consumes them; source hub traits (`OrderedMapSource` etc.) move OUT of
`logos.mem.bt.map` into the query plane's own module, closing the recorded
"vocabulary rooted in one supplier" defect.

LANDED (S0, 2026-08-11) as the sibling module `logos.lang.stream`
(`stdlib/lang/stream/stream.logos`): `BatchStream<B>`, `Rewind`,
`SizedStream`, `OrderedBy<K>`, `Landed<K>`, and `RowsBatch<R>` — which is a
type ALIAS for `&[R]`, not a wrapper: the slice already carries the whole
batch surface (`len()`, `[i]`) the scan loop uses, so `impl BatchStream<&[R]>`
and `impl BatchStream<RowsBatch<R>>` are the same impl. TWO deviations from
"one module" were forced and are recorded here rather than in the code alone:

* `Buffer<R>` (§4) does NOT live with the traits. The lang tier is
  no-alloc/no-OS and `Vec` is `logos.mem.collections.vec`, so the degenerate
  stream is `logos.mem.stream` (`stdlib/mem/stream/buffer.logos`). It OWNS its
  `Vec` — a Buffer is the RESULT of a materialization, and a Buffer borrowing
  its rows would not have materialized anything; the BATCH it hands out
  borrows (rung 1, §7: `push` while a batch lives is a borrow-check error).
  It implements `BatchStream` + `Rewind` + `SizedStream` and deliberately NOT
  `OrderedBy<K>` — an unsorted `Vec` has no order fact, and the `Sort`
  adapter's output type is what will carry it (S2).
* The module is NOT re-exported from `logos.lang.prelude`. `BatchStream::next`
  and `Iterator::next` share name and receiver shape, so an implicit prelude
  import would make `it.next()` ambiguous tree-wide. Consumers write
  `use logos.lang.stream;`.

`ColsBatch`/`PdtCol` (`stdlib/mem/bt/batch.logos`) and the family's leaf-batch
producer (§5, LANDED) close the S0 items; the hub-trait move
(`OrderedMapSource` etc.) is a later slice and was NOT started here. One import
edge was forced by the producer: `logos.mem.bt.map` — the hub every declaring
unit already imports — now carries `logos.mem.bt.view` and `logos.mem.bt.batch`,
because a generated struct's FIELD types are resolved before its own emitted
`use` can load a module (`error [struct …LeafBatch]: unknown type 'ColsBatch'`
on every existing container test, measured). `logos.lang.stream` is deliberately
NOT hubbed: it appears only in impl headers, which resolve from the generated
unit's own `use`, and hubbing it would make `it.next()` ambiguous for every
consumer of the map hub — the same collision that keeps it out of the prelude.

## 12. Deem-call pipelines and the fiber-backed pull form

Today every query copies its result into a returned `Vec` — correct, poorly
composable: `q2` over `q1`'s result pays a full materialization at the seam.
With batch streams both halves of composition exist on this plane:

* CONSUMING a stream: a `deem` source parameter whose type is a batch stream —
  the S4c path as it stands (membership-checked, planned as non-`Rewind`,
  non-`SizedStream`).
* RETURNING a stream: `deem q(m: &Hs…) -> Result<QStream, ElError>` over a
  generated `#[borrow_carrying]` stream type — a borrow-carrying return tied to
  the source arguments. Pipelines within one scope are rung 1–2 (zero counting,
  §7); only an ESCAPING stream is rung 3.

  **THE SPELLING THIS BULLET CARRIED UNTIL S5 — `-> impl BatchStream<B> + '_` —
  DOES NOT PARSE, AND THE TICK NEVER CARRIED MEANING.** Measured at S5, probe
  pairs with rc from a redirect (`/home/logos/sandbox/s5r`, re-run on the S5
  tree — the probe dir the numbers below come from):
  * `p1_borrow_elided` rc 0 — `-> impl BatchStream<i64>` over a struct holding
    `rows: &[i64]` taken from the fn's own `&[i64]` argument compiles, links and
    runs. THE BORROW-CARRYING RETURN THIS BULLET ASKS FOR EXISTS TODAY, with
    lifetimes ELIDED. `p0_ctl` (owned struct) rc 0 is the control that says the
    borrow is not what carries the answer.
  * `p2_borrow_tick` rc 4, `syntax error near 'fn' at line 13 col 1`;
    `p2b_tick_named` (`+ 'a` instead of `+ '_`) is refused BYTE-IDENTICALLY, so
    the refusal is the `+ 'lt` bound on RETURN-POSITION impl-trait, not the `'_`
    label. `logos.peg` accepts `+ 'lt` only on `dyn` (`dyn_auto_bound` /
    `AUTO_LIFE_BOUND`) and states that lifetime bounds are "recorded but not
    enforced… Logos's lifetime model is elision-based" (logos.peg:1949-1950).
    The tick was decoration over an elision-based model: RETIRED, not owed.
  * `p9_result_impl` rc 1, `return type mismatch — expected Result, got Result`
    — `Result<impl BatchStream<i64>, i64>` is REFUSED: impl-trait is
    TOP-LEVEL-return-position only, it does not nest inside a generic. THIS is
    the load-bearing measurement: today's surface is `Result<Vec<T>, ElError>`,
    so the streaming form CANNOT be `Result<impl BatchStream<B>, ElError>` and
    must be a NAMED type per query (the `CtrLeafFamily` precedent) — the only
    spelling that keeps `Result`. `p3_named` (named struct, elided borrow field)
    rc 0 and `p4_lt_param` (`SliceS<'a>` + `fn mk<'a>(rows:&'a [i64])`) rc 0 say
    both named spellings are available.
  * `p5_compose` rc 0 — `fn q2<S: BatchStream<i64>>(s: S) -> impl BatchStream<i64>`
    over `q1(...)`: PIPELINE COMPOSITION COMPILES. Parameter-position `impl
    Trait` is refused (G156-2/G125-2), so the consumer side is a generic BOUND;
    that is the working spelling, measured, not assumed.

**RETURNING in general is a push/pull inversion, and the inverter is a
QUEUE (Victor, 2026-08-10: fibers deferred — do not complicate yet).** The
emitter produces push-shaped bodies (`__out.push(row)` inside a fused nest).
Between that body and a pull consumer stands a queue — and **a `Vec` IS the
degenerate queue**: eager producer, no bound. So the forms, in order of
arrival:

* `direct` — single-loop streamable plans: the stream is a state struct (the
  Walk shape), no queue at all, no cost. Exists from S5 day one.
* `buffered` — ANY other body, NOW: the body runs as emitted into a `Buffer`,
  and the Buffer is served through the same batch-stream return type.
  Semantically a `Drain` at the output seam, and the plan records it as one
  (`"buffered: body is not single-loop"`), so the seam materialization the
  pipeline was meant to remove is at least VISIBLE while it remains.
* `queued` — LATER, the upgrade this paragraph exists to reserve: the `Vec`
  becomes a bounded queue fed incrementally; who stands on the producer side
  (a fiber `yield`ing per batch, a task) is decided THEN, not now. The
  surface does not move: all three forms inhabit the same return type, so
  the upgrade swaps an implementation, never a caller.

What survives of the fiber analysis, recorded for the `queued` step so it is
not rediscovered: one suspend per BATCH (= leaf), never per row — batching is
what prices a lazy producer in; an empty batch is the liveness tick for
budgeted/anytime execution; the GOTTPOFF "no fibers in metacall JIT"
constraint is compile-time-only and does not apply to emitted runtime code;
requirements 7–8 (§7) are the substrate the producer side will owe — PARKED
until `queued`, not v1 obligations. Operator-over-batch-queues is the runtime
shape of the DBSP netlist and of Hest's dataflow — Deem pipelines and Hest
converge on this plane when `queued` lands.

## Axes ledger

source-kind × layout(rows/cols) × batch bound(leaf/whole/epoch) ×
capability(4) × adapter(3) × consumer(scan/build/probe/sort/γ/fixpoint) ×
pass-count × order × weightedness × pin(existence/residency) ×
snapshot state(committed/writable) × cursor form(borrowed/owned).
v1 exercises narrow: Memoria map scan/point/range + heap slice through the one
scan shape; every other cell is declared, not silently absent.

## Slices

* **S0 — vocabulary + Buffer + leaf batches.** The traits; `RowsBatch`/
  `ColsBatch`/`PdtCol` (read mode); `Buffer`; the family emits a leaf-batch
  producer beside the per-row walk (both live; the per-row form dies in S1).
  Gate: trait-membership questions answerable from the planner.
  **DONE 2026-08-11, gate ANSWERED WITH TWO HOLES NAMED.** Fixtures:
  `pass/stream_buffer_degenerate` (§4's one-packet base case, values asserted),
  `pass/ctr_family_leaf_batches` (§5 against the container's own per-row cursor;
  batch count pinned EXACTLY at the leaf count, 8 for 1000 entries),
  `pass/stream_caps_trait_query` (the gate: `has_trait` over
  `typeof(c.leaf_batches())` answers `BatchStream`/`SizedStream`/`OrderedBy`/
  `Landed` yes and `Rewind` no, with a local `impl Rewind` control so the no is
  not a blanket false, and the answer SELECTS the code path), and the batch's
  own rung-1 pair `fail/ctr_family_mut_while_batch` +
  `pass/ctr_family_batch_then_mut`. The two holes, both recorded at the fixture
  and owed by S1:
  * **The query needs a type SPELLED at the call site.** `has_trait::<T, Tr>()`
    takes a type, `has_trait_of::<Tr>(t: Type)` takes a `Type` mono can only
    recover from a spelled type — and `join_key_caps_named`, the seam §3 names,
    holds a type NAME (`StepKey.ktn`, a `str`). S1 owes a name-keyed query or a
    `Type`-valued type environment on the plan IR; until then the capability
    rows there stay a hand-written table.
  * **`has_trait` answers 0 for a generic type whose bare name is AMBIGUOUS
    tree-wide** — which `Buffer` is (`logos.mem.stream` vs
    `logos.lang.fabric`). G156-1 folds a module fingerprint into the type's
    identity (`Buffer$M…$G1$i64`, visible in the emitted symbols) while a
    GENERIC impl registers its target under the bare spelling written in the
    source, so the two keys never meet and the query says "no impl" instead of
    erroring. Control in one program: `VecIter`/`Iterator` and `Vec`/`Index` —
    same tier, same imported-generic shape, unambiguous names — answer 1, while
    `Buffer`/`Rewind` answers 0 against an impl that demonstrably works. The
    fixture therefore does NOT assert the Buffer half: writing 0 down would pin
    the defect. The repair belongs on the IMPL side (qualify the generic impl's
    target key with its module, as the concrete arm already does); widening the
    QUERY to also try the unsuffixed name would re-conflate homonyms.
* **S1 — one scan shape.** The emitter's three scan branches collapse; slices
  ride as one-packet streams.
  **SPELLABILITY + TRAVERSAL SUB-SLICE DONE 2026-08-13** (the scan-branch
  collapse itself is still open): `CtrLeafFamily` and its factory-emitted impl
  (§5), the `Bidirectional`/`RandomAccess` pair and the `Landed::seek` →
  `seek_key` rename (§3). Fixtures `pass/ctr_leaf_family_spelling`,
  `pass/stream_traversal_buffer`, `fail/ctr_leaf_family_vector_refused` (re-aimed
  at S1b as `fail/ctr_leaf_family_volume_refused`),
  `fail/ctr_leaf_family_wrong_family`; registry 7135→7139 predicted before the
  reconfigure and measured after; full build + L2 2110/2110 + the 212-test
  bc/ctr corpus green. Gates: slice-source codegen byte-comparable OR
  measured equal (objdump/callgrind — the S4k standard); Memoria scan via leaf
  batches, oracle = same rows against the container's own per-row cursor +
  descent count (leaves, not rows). Requires Memoria API req. 1–2 — req. 2 in
  its §7b form (`dyn Snapshot` in the handle), so the dyn boundary lands here,
  not as a later migration.
  **THE MEMORIA SCAN SUB-SLICE DONE 2026-08-13; THE SLICE ARM IS STILL OPEN.**
  What landed, in three stages with a green checkpoint and its own control
  between each:
  * **The channel.** `producer_batches_` (`src/compiler/sema_expr.cpp`) is the
    exact twin of `producer_streams_` — `sema_has_impl_recursive("BatchStream",
    <ret-ty base>)` — and both now share one reduction of the return type to
    its impl key, so the two answers cannot come to disagree about which type
    they were asked about. The natspec flag field carries `b` beside `i`: `i`
    stays "consumable in place" (the OFFER, ADR 0024 S4), `b` says only THE
    PULL UNIT IS A BATCH. Membership, not equality — `pw_flag` already tested
    it that way, so nothing that reads `i` changed. `b` travels on the rel's
    own materializer AND on each declared operation, because
    `plan_apply_access` replaces the rel's producer with the chosen op's and a
    pushdown would otherwise fall back to a row pull against a batch stream.
    Parsed into `rel_batch`/`opc_batch` (`params.logos`).
  * **The declaration.** The generated ordered_map family declares the
    leaf-batch producers: `rel entry = #browsfn`, `op … eq/ge/le =
    #batfn/#bfromfn/#buptofn`. Nothing else about the declaration moves — same
    rel, same columns, same exactness, same `size` reporter. That is the whole
    switch: the pull unit is a property of the PRODUCER, not a second
    vocabulary.
  * **The two consumers.** `rexpr_walk::batch_scan_frag` is §1's shape verbatim
    and is called from BOTH scan sites (`emit_simple`'s streamed arm and
    `chain_nest_frag`'s streaming join base) — one function, so a layout is a
    row in it rather than a branch in every consumer (§2). The DRAIN prelude
    (`plan_walker::emit_prelude_oneshot`) keeps the outer `next_batch()` pull
    and puts the index loop inside it, so a query the plan cannot read once
    still pays one pull per LEAF. The row is spelled by ONE shared function,
    `params::batch_row_text`, keyed off the DECLARED column names
    (`<col>_at(i)`), so the emitter still knows no source type.
  * **The per-row form is DELETED — BOTH arms (ordered_map at S1, positional
    at S1b)** — `{N}Walk`, its `Iterator` impl, and
    `__ctr_rows_/at_/from_/upto_`. The S1b half's ledger: the five positional
    symbols were grepped tree-wide (build/ and .git/ excluded) BEFORE the cut,
    and the one MACHINE reference — `tests/logos/ctr_access_path_gate.sh`'s
    three vector clauses — was re-pinned WITH the cut in the same change rather
    than found three steps later, which is what the S1 audit's refutation below
    cost and what naming the gate in the ledger buys. ⚠ The ledger sentence that
    stood here ("referenced NOWHERE but at their own definition … no fixture
    and no gate named them") was REFUTED by this round's own audit: the
    access-path gate and fixtures did name them. The honest ledger: every
    remaining reference was updated WITH the cut in the same change, and the
    audit's grep of all 14 deleted symbols (build/ excluded) is the record —
    a cut's ledger must be the measured reference list, not a claim of
    emptiness.
  MEASURED, not inferred: the shape change was read off the EMITTED ARTIFACT
  (`--gen-dir`) for every family-source deem fixture in the tree, which is also
  how the drain leg was found to have NO coverage at all — hence
  `pass/deem_batch_scan_drain` (`order by`, an aggregate, and a narrowed
  landing under a sort, each against the container's own per-row cursor).
  CONTROL, run and restored (md5-verified) with a rebuild green on both sides:
  neutering the drain prelude's `rel_batch` arm reds that fixture at compile
  time (`let '__dr': type mismatch — expected Option, got Option`) while
  `deem_ctr_family_streams` stays green — the two legs are independent and the
  new fixture is the only thing holding the new arm. Registry 7139→7140
  predicted before the reconfigure and measured identical after; L2 2110/2110.
  STILL OPEN in S1, and named rather than absorbed: the SLICE arm. §1 says the
  indexed loop is literally the inner loop of the one-packet case, and that is
  true of the SHAPE — but a heap slice has no packet PRODUCER, and minting one
  is `Drain`/`Buffer`, which is S2. Emitting a `while __p < 1` skeleton around
  the existing loop would have put a shape in the tree that no source produces,
  against a gate (byte-comparable slice codegen) it can only lose. So the slice
  arm still reads `is_slice` and is untouched — and the byte-comparability gate
  was therefore not exercised, which is the honest state, not a pass.
  **BOTH S1 GATES ARE NOW REGISTERED ARTIFACTS (2026-08-13).** A gate that is a
  SENTENCE in an ADR is a gate nobody runs, and both of these guard the same
  defect class — one that a green corpus cannot see BY CONSTRUCTION, because the
  ANSWER of a scan is what every fixture asserts and neither defect changes it.
  * `logos_09_slice_scan_codegen` + `pass/wql_slice_scan_shape` +
    `tests/logos/slice_scan_shape.golden`. The emitted `slice_scan_run`, byte for
    byte. The gate additionally refuses a golden that could not be an assertion
    (a size floor, the indexed loop, the `where` predicate — the vacuous-
    expectation defect `run_test.sh` refuses on the other side of the harness),
    and asserts that this arm carries NONE of §1's batch vocabulary. That last
    clause is the S1 state written down: when S2 makes slices ride §1's shape the
    gate goes red twice, and the ADR's "OR measured equal" arm becomes a
    deliberate move with a recorded measurement rather than a silent re-golden.
    CONTROL, run and restored (md5-verified, rebuilt green on both sides):
    perturbing the EMITTER — an answer-preserving `let __n0: i64 = (rows).len();`
    hoist added to this arm's emitted block in `rexpr_walk.logos` — reds it on
    that one added line, while the fixture and every other slice fixture stay
    green.
  * `logos_09_ctr_leaf_descent` + `pass/ctr_leaf_descent_count`. §5's asymptotics
    as an assertion, not a comment: callgrind CALL COUNTS (never a duration —
    shared box), attributed by CALLER so the batch plane's descents and the
    per-row oracle's are counted apart rather than summed. MEASURED: 1000 rows
    scanned in **8 descents over 8 leaves**, against **1000** per-row container
    calls on the oracle side; 9 batch pulls (leaves + the terminating `None`);
    and TOTAL descents in the whole program 18 = 2·8+2, fully accounted — the
    clause that closes "something else descends". The leaf count is INDEPENDENT
    of the batch plane: §5's own no-sibling-pointer fact means the ROW cursor
    crosses a leaf boundary by descending, so its `bt_cur_next -> bt_seek_at`
    count IS the container's leaf count, and the two must agree. No counter was
    welded into the stdlib for this — an instrument inside `advance()` would be a
    production edit on a hot path, measuring itself. CONTROL, run and restored:
    a second `cp.seek(self.at)` in the family's `advance()` changes no answer at
    all (fixture green, ctr corpus green) and reds this gate at 16 descents for 8
    leaves — which is precisely the regression the whole corpus is blind to.
  * **A CORRECTION §9 OWES, measured here.** `is_slice` is NOT a third spelling
    of the scan shape. `params.logos:275/282` read it as the SOURCE-vs-SCALAR
    discriminator (`is_source`/`is_scalar`), and `rexpr_walk` reads it a second,
    unrelated way as the incremental arm's "exactly one slice parameter"
    admission. S2 deletes the SHAPE reading; the bit survives both other roles.
  * **THE ColsBatch-PAIR DELTA (S0's §2 ⚠) IS DEFERRED, WITH ITS GROUND.** S0
    recorded that `{N}LeafBatch` is a PAIR of `ColsBatch`es (key slot + value
    slot) rather than one, and left S1 to decide between a multi-slot
    `ColsBatch` and a two-column leaf slot. Neither is taken, because the
    question came off the critical path: the emitted scan reaches the batch
    only through `<col>_at(i)`, which is layout-agnostic by construction, so
    the pair costs the emitter nothing and the choice is now free to be made by
    whoever needs the columnar fold (§8/S4). What IS owed and is recorded as
    owed rather than skipped: §2's resolve-once hoist. The emitted inner loop
    calls `<col>_at(i)` per cell, which re-resolves the column's `(allocator,
    slot)` per read — never a DESCENT, so §5's per-leaf claim is untouched, but
    not the hoist §2 asks for. It needs a per-column accessor named off the
    declared column (`col_<c>()`) that the family does not publish yet.
* **S2 — adapters as plan nodes.** `Drain`/`Sort`/`Arrange` in AccessPlan with
  grounds; delete the side-channel booleans and the single-pass proof (the
  latter DONE at S2j — inverted, not dropped: `plan_insert_drains`);
  `explain()` names every materialization. Gate: the refusal census (why-
  vocabulary) re-derived, no silence where a drain happens — LANDED as
  `logos_09_plan_ground_census` (whole corpus, both vocabularies, the arrange
  deficit pinned; see S2c above), beside `logos_09_plan_nodes` (four plans, the
  cross-channel comparison).
* **S3 — order and limit as facts.** `OrderedBy` no-op sort; bounded walks.
  Gate: an `order by` over the seek column emits NO `Sort` node and the trace
  says why.
* **S4 — aggregates fold single-pass** (with the emitter unification).
  The GROUP FRAME's naming half is LANDED — see §8: four `AG_*` grounds, 380
  bindings named, `logos_09_group_frame_naming` (tier_commit) + FACT H in the
  corpus census. Still open on this axis: the group-row `__ix0` permutation,
  which no node attributes.
* **S5 — streaming query output + pipelines (§12).** A NAMED batch-stream
  return surface, TWO forms: `direct` for single-loop plans, `buffered`
  (Vec-as-degenerate-queue) for every other body; `queued` deferred with
  req. 7–8. Oracle = pull count (direct: one packet per source leaf, no seam
  materialization; buffered: exactly one) + same rows.
  **THE `buffered` FORM IS LANDED (2026-08-14). `direct` IS NOT, AND THE
  BLOCKER IS MEASURED AND NAMED BELOW. THE OWNED CURSOR IS REFUSED FOR S5.**
  * **What landed.** Every `Vec`-returning query emits a SECOND entry beside
    its own: `q_stream(…) -> Result<Buffer<E>, ElError>` (`rexpr_walk::
    emit_stream_surface`, reached through the one door `emit_query_fn`, which
    now takes the row type as a parameter — "" for every single-row/find/
    identity shape — so each emitter STATES whether its query has a multi-row
    result instead of the surface re-reading `ret_ty`'s text). `Buffer<E>` is
    §12's degenerate queue made literal and is ALREADY the landing the `Drain`
    node builds at the internal seams (S2j/S3b), so the output seam and the
    internal seams materialize into one type, not two.
  * **The Vec surface is NOT migrated, and the measurement is what says so.**
    Corpus delta over the 165-dump snapshot: PURE ADDITION — as a line multiset,
    ZERO baseline lines disappeared; 486 `*_stream` entries appeared; all 505
    `*_run` entries unchanged; +567,879 bytes (+8.0%). That is why the codegen
    byte-pin `logos_09_slice_scan_codegen` needed no re-golden, and why
    `logos_09_plan_ground_census` moved on its FIXTURE COUNT alone with all
    fifteen plan-plane numbers standing.
  * **Fixture `pass/deem_stream_return_surface`,** asserting at `next()` and not
    at `len()`: values through the STREAM; EXACTLY ONE packet then exhaustion
    (the `buffered` form's single seam materialization — the number that changes
    when `direct` lands, which is why it is pinned now); the EMPTY result as ONE
    EMPTY PACKET and not an absence (§1's legal tick — an implementation
    short-circuiting to a bare `None` is green on `len()==0` and makes "no rows"
    indistinguishable from "the stream never ran"); composition through a
    generic `BatchStream` bound applied to TWO queries' streams; and both
    surfaces of one query asserted row-for-row so a later migration cannot be
    silent. CONTROL, run and restored (md5-verified, rebuilt green on both
    sides): removing the `emit_stream_surface` call reds the fixture at compile
    time on all three call sites (`call to undefined function 'q_all_stream'`)
    while every sibling deem fixture stays green — the two legs are independent
    and this fixture is the only thing holding the new door.
  * **THE PIPELINE FIXTURE — §12's COMPOSITION ORACLE, `pass/deem_pipeline_chain`
    (2026-08-14).** Two chained deems, and the stage's own finding is that a
    pipeline has TWO seams with DIFFERENT answers, so each gets its own
    instrument rather than one number covering both:
    * **SEAM 1** (q1's output) counted in PACKETS at `q1_stream`'s `next()`:
      EXACTLY 1, the `buffered` form's single materialization. This is the
      number `direct` moves.
    * **SEAM 2** (q2's input) counted in PULLS at the chained source's own
      `next()`: `limit 3` over the 12-row seam pulls **3**, and the CONTROL
      `q2_all` — same source, same rel, no bound — pulls **12**. One variable,
      both directions, in one program over one source, so the 3 is PULL-THROUGH
      and not a short source. (Pre-measured standalone at
      `/home/logos/sandbox/s5pipe/p2_{lim,nolim}`, exit code = the pull count:
      3 and 20 over a 20-row source.)
    Same rows both ways: the chain is run with seam 1 as the `Vec` surface and
    as the `Buffer` surface, and the two row sequences are asserted against each
    other, against q1's own Vec, and against literals. NON-VACUITY MEASURED, not
    argued: five perturbations of the fixture's own oracle numbers (pull 3→4,
    control 12→11, packet len 12→11, exhaustion inverted, Vec-seam pull 3→4) each
    red at exactly the predicted exit code (7/10/3/4/15), so every number the file
    asserts is a number it actually compares.
    * **WHAT THE FIXTURE REFUSES TO ASSERT, on measurement rather than by
      omission.** DIRECT-OVER-DIRECT: `direct` is not landed (blocker below), so
      that arm would assert a form that does not exist. THE OUTPUT-SEAM `Drain`
      NODE: deliberately not inserted at S5 (bullet below). "The plan SAYS so" is
      therefore asserted at SEAM 2, where the plan has two answers and gives the
      positive-absence one — `[plan] s -> no materialization (no node: the plan
      reads this source once and consumes it where it stands)`, the streamed-rel
      arm, checked on the trace channel by `plan_ground_census_gate.sh`.
    * **CENSUS, PREDICTED THEN MEASURED.** Registry 7171/3488/36 → **7172/3489/36**
      (+1/+1/0), predicted before the reconfigure and measured identical: one
      fixture with its `.expected`, no new gate, because a pull count is not
      visible to an artifact grep. Corpus: **PURE ADDITION** — `diff -rq` against
      the re-baselined 166-dump snapshot lists ONLY the new dump, so no existing
      dump moved a byte and the codegen byte-pin needed no re-golden
      (7,649,796 → 7,672,649 = +22,853, exactly the new dump, residual 0).
      Ground census: fixtures 182→183, `readonce` 19→**21** (two streamed rels,
      the ground that IS the seam-2 claim). ⚠ A THIRD MOVE WAS PREDICTED AND
      REFUTED: `container` 204 was predicted to go to 205 for q1's `&[Row]`
      source and did NOT move — the trace shows q1 emits no per-rel
      materialization ground at all, because a bare slice parameter is scanned
      where it stands and never reaches the container arm. Two of three
      predictions right, and the wrong one corrected at the gate with its cause.
  * **`direct` IS NOT LANDED, AND THE BLOCKER IS AN ORDERING FACT, NOT A
    LANGUAGE GAP.** The direct stream's state IS the source walk, so its struct
    must SPELL that walk's type. Probed (`/home/logos/sandbox/s5d`, rc from a
    redirect), a three-way probe that separates the two candidate causes:
    `pd1_direct_named` — the whole hand-written direct stream, field
    `w: <typeof(Ded) as CtrLeafFamily>::LeafWalk` — rc 1; `pd2_struct_handle` —
    the SAME struct with the assoc type swapped to `::Handle`, the spelling the
    corpus uses successfully — rc 1, byte-identical error; `pd3_param_leafwalk`
    — no struct at all, the assoc type in a plain `pub fn` PARAMETER — rc 1,
    same error again: `typeof(container Ded): its config const 'DedCfg' did not
    resolve to a WritStatic document`. So it is neither the struct position nor
    `LeafWalk`: `typeof(<container>)` does not resolve in ANY hand-written item
    of the declaring module, because ordinary items are lowered BEFORE the
    container factory runs. The control is already in the tree and green —
    `pass/deem_ctr_family_streams` spells `m: &<typeof(Led) as CtrFamily>::Handle`
    and compiles, because a `deem` parameter is resolved at METAPROG time, after
    the factory. CONSEQUENCE: the direct stream type can only be emitted from
    inside metaprog, where `plan_walker`'s prelude ALREADY spells the concrete
    `Hs…LeafWalk` as text (`let mut __rel_m: Hs…LeafWalk = __ctr_bfrom_…(m, 5u64);`)
    — so `direct` is an emitter-plumbing step (carry the prelude's per-rel
    binding TYPE and CONSTRUCTOR text into `rexpr_walk` so the struct and its
    `next()` can be emitted from the same place that emits the loop), and it is
    NOT a language change. Priced as its own stage rather than absorbed here.
    ⚠ **SUPERSEDED AT R-E (2026-08-15), BOTH HALVES.** (1) The typeof blocker
    fell: the compiler fix landed (sema typeof arm → demand;
    `pass/typeof_container_hand_written_state` — param/let/alias positions
    compile; the struct-FIELD position still refuses, pinned by
    `fail/typeof_container_field_no_family_fail`). (2) And landing the
    emitter-plumbing precondition found the TRUE blocker one layer down:
    an EMITTED struct holding `Hs…LeafWalk` as a field compiles and then dies
    under `LOGOS_VERIFY_LAYOUT` — `sema_abi_layout` returns {8,8} for a
    metaprog-emitted struct asked from a foreign package (sema 8 vs
    llvm::DataLayout 72) — a class the `sema.cpp` comment already names,
    never reached because no emitted struct in the corpus holds an emitted
    struct as a field. `direct` is blocked on that compiler task. Seam-1
    correction, measured: 489 of 502 seam-1 sites are 1 packet BECAUSE THE
    SOURCE is 1 packet (SliceStream / no-stream) — `direct` moves 13
    container-walk sites, and on criterion 1's printed share it is a
    PESSIMIZATION (eligible landings leave both numerator and denominator:
    ~−1.2 to −1.4 pp).
  * **The output-seam `Drain` node is deliberately NOT inserted at S5.** §12
    asks for one. The node list is per-REL and the output seam is not a rel —
    but the load-bearing reason is that at S5 the answer is CONSTANT across the
    corpus (`direct` absent ⇒ all 486 entries buffered), so the node would move
    the census by 486 while telling a reader nothing. It lands with `direct`,
    the stage where the plan first has two answers to give.
  * **The OWNED CURSOR is refused for S5, with its ground.** Grepped tree-wide:
    no `OwnedCursor`, no owned constructor, no pin token — the only thing that
    exists is the store-side hook (`stdlib/mem/bt/map.logos`: `snap_pin`/
    `snap_unpin`/`leaf_pin`/`leaf_unpin`, all four defaulted to no-ops for
    MemoryStore), i.e. §7's two-part store invariant has its interface and no
    caller; the family emits ONE constructor (borrowed), and `Send` is on no
    stream type anywhere. The escape shape is rung 3 with its own Drop-unpin
    obligation (§7 item 5), while §12 puts within-scope pipelines at rung 1–2
    with ZERO counting — which is exactly what the probe set above measures as
    already working. Building it here would have been a rung-3 mechanism with
    no rung-3 consumer.
  * **S5-D5 — THE AGGREGATE PULLS BATCHES, closing S4's other half
    (2026-08-14).** §8's fold was single-pass at the operator and INDEXED at the
    artifact (`while __i0 < (src).len()`), which is why `plan_walker`'s Aggr arm
    could not claim the pass it had proved — the plan may not claim a property
    the artifact does not have, and the one-sided attempt is recorded there with
    its failure (`'…LeafWalk' has no method 'len'`). Both halves land together:
    `emit_aggregate`'s base nest routes through `batch_scan_frag` (§1's ONE
    shape, third and last call site), and the plan stops draining the pure
    class. GATE: `pure_group` — the representative class seeds
    `__g_row.push(__i0)` and a GLOBAL ROW ORDINAL is unspellable in a batch pull,
    so that class keeps the indexed walk and says so — plus the producer's batch
    flag, read as `ap_effective_batch` because `plan_apply_access` installs the
    pushdown-chosen op's flag AFTER the drain pass. MEASURED: the corpus moves
    exactly ONE file (`deem_batch_scan_drain`, −23 bytes); the `__it_m` +
    `Buffer<(u64,u64)>` pair collapses to one binding; drain+sort 13→12,
    `__it_` 13→12, read-once 18→19, and every other census pin holds.
  * **S5-D4 — THE SLICE ARM's COMPILER BLOCKER IS CLOSED (2026-08-14).** S3d
    stopped at a `'__bb0' does not live long enough … borrowed by '__ks'`
    (E0597) that D7 classified as a borrow-provenance over-refusal. Rooted to an
    8-line program with no stream in it and fixed in the CHECKER: the §B6 source
    channel's `AddrOfTemp` arm emitted the place's root LOCAL even when the walk
    went THROUGH a reference, where the storage borrowed is the POINTEE and its
    life is whatever the reference itself borrows. Probe pair (one variable —
    the referent's origin): parameter ⇒ admit, dying local ⇒ refuse, naming the
    LOCAL that dies. Corpus byte-identical. **THE COLLAPSE ITSELF IS STILL NOT
    LANDED**: the byte-pin's "measured equal" arm is refused by S3d/D5's numbers
    (59 → 128 instructions for an already-materialized source), which no checker
    fix moves. Two blockers became one, and the survivor is a number.
* **S6 — WritWalk batches; weighted batches** (the DBSP seam §10).
  **S6-A — THE ROW-MAJOR LAYOUT LANDED, AND THE WRIT SWITCH IS REFUSED WITH ITS
  NUMBERS (2026-08-14).** The stage opened at §6's Writ sentence, which the map
  found FALSE as spelled (corrected in place, above). What the plane was
  actually missing turned out to be one layer down and NOT Writ-specific: §2's
  row-major batch had no consumer, so no `Vec`-shaped source could join the
  plane at all. That half is landed (§2's LANDED block: `RowMajor` + the `m`
  letter + `batch_row_text`/`batch_len_text` + `Buffer::next_batch`), with
  `pass/deem_rowmajor_batch_source` and `logos_09_rowmajor_batch_layout`.
  Registry 7172→7174 / 3489→3491 / 36 unchanged — predicted before the
  reconfigure, measured after, exact.
  * **THE REFUSAL, MEASURED BY CONTROL AND THEN REVERTED.** The cheap way to
    "join the plane" is to declare the Writ rel over a Buffer producer
    (`rel edge = writ_graph_stream;` where
    `writ_graph_stream(root) = Buffer::from_vec(writ_graph_edges(root))`).
    Landed on a scratch tree, full corpus sweep (203 files, both trees, same
    build, rc from a redirect), then reverted with a green build checkpoint:
    - **3 fixtures stop compiling** (`wql_writ_graph_e2e`,
      `deem_three_domain_join`, `wql_graph_root_id_cross_document`) — 2 known
      pinned failures in both sweeps, so the delta is exactly 3.
    - **+29 inserted materialization NODES.** "A rel block may scan this source
      repeatedly" 1→20, "the query names this source on both sides of a join"
      0→9, `order by` 4→5. Each is a full second copy of the document's entire
      edge relation, built by the drain prelude out of a `Buffer` the producer
      had ALREADY built. `MG_CONTAINER` grounds 227→167 lines.
    - **+35,073 corpus bytes** (8,876,668 → 8,911,741), same 4,011 dumps.
    - ⚠ **AND THE CRITERION-1 INSTRUMENT WOULD HAVE CALLED THIS AN
      IMPROVEMENT**: `[plan]` lines matching "materializ" go 668 → 638 while the
      artifact materializes 29 times MORE. The instrument counts SENTENCES, and
      the switch trades 60 "nothing is built" sentences for 30 streams and 29
      drains. Recorded here for #47: a ratio over ground sentences is not a
      count of materializations, and this is the first measured case where the
      two move in opposite directions.
    The cause is structural, not a bug: a `Buffer` is a POSITIONED stream with
    ONE cursor, so every re-read (a rel block, a self-join) must be drained,
    while a `Vec` + `as_slice()` is a landed relation any number of nested loops
    can index. `Rewind` is the capability that would say "re-readable in place",
    and the drain pass does not consult it — but rewinding a SHARED binding
    mid-outer-loop is not the same operation as a second independent cursor, so
    consulting it is not the repair either. The honest repair is the one §6
    always named: the `WritWalk` CURSOR, which streams document-walk batches
    without prebuilding anything, and whose re-read story is a second walk
    rather than a second copy.
  * **THE FOURTH PULL SITE, FOUND BY THE CONTROL AND STILL OPEN.**
    `rexpr_walk::build_phase_frag` — the HASH-JOIN BUILD phase — pulls
    row-at-a-time (`let __bo1: Option<Row> = (src).next();`) and was never
    converted when S1 collapsed the scan. Any batch source on a join's build
    side dies there: `let '__bo1': type mismatch — expected Option, got Option`
    (the annotation is the ROW type, the pull yields a BATCH). This is not
    writ-specific — a family leaf walk on a build side would fail identically —
    and it is invisible to the green corpus BY CONSTRUCTION, because no corpus
    query puts a batch source on that side today. It is criterion 2's join-plane
    gap made concrete, and it is owed by whichever stage first needs it (the
    `WritWalk` cursor will).
  * **THE LANDING IS INERT FOR EVERY EXISTING PRODUCER, ON THE ARTIFACT.** A
    layout letter nothing emits is a change that must not move a byte, and that
    is checked rather than argued: the four writ-carrying dumps re-compiled on
    this tree are BYTE-IDENTICAL to their `af17c2fa` baselines
    (`wql_writ_graph_e2e`, `wql_gpath_e2e`, `query_tree_source_graph_e2e`,
    `derive_graph_source_root_row`), and a whole-corpus `diff -rq` between the
    tree before and after the fixture landed reports no shared dump changed at
    all. CORPUS SNAPSHOT, re-baselined for this sweep's denominator (203→204
    files, `sandbox/s6writ/sweep.sh`, rc from a redirect, the same 2 pinned
    non-compiling fixtures both times): 4,011 → 4,021 dumps, 8,876,668 →
    8,892,177 bytes, 4,249 → 4,259 `[plan]` lines — every delta is the new
    fixture's own.
  * **NOT STARTED HERE: the weighted half (§10).** `Weighted<R>` + the wrap, and
    what the incremental tier can consume today, are untouched by S6-A — they
    are S6-B below.
  **S6-B — THE DBSP SEAM: VOCABULARY LANDED, CONSUMPTION SEAM MEASURED AND
  DECLARED (2026-08-14).** §10's LANDED block has the whole statement; the
  slice-level facts:
  * `Weighted<R>` / `DeltaBatch<R>` / `Epoch<R>` in `logos.lang.stream`; z⁻¹
    recorded on `Buffer` (`Buffer<Weighted<R>>` = the arranged mixed-sign
    delta, NO new type); `pass/stream_weighted_epoch_seam` pins the four §10
    clauses as VALUES, including `Some(empty)` != `None` by making the consumer
    answer the two differently.
  * **THE WEIGHT COSTS ORDINARY QUERIES NOTHING, CONTROLLED:** whole-corpus
    artifact snapshot byte-identical, 168 dumps / 7,688,158 bytes over the 184
    `deem_*`/`wql_*` fixtures, `diff -rq` empty (`sandbox/wql_corpus_snapshot_par.sh`,
    rc from a redirect). The new fixture is outside that population by
    construction — it compiles no query.
  * **THE FIXTURE BITES — THREE PERTURBATIONS, EACH REVERTED TO A GREEN
    CHECKPOINT BEFORE THE NEXT** (21-fixture stream-vocabulary consumer sweep,
    one rc file per fixture, count asserted; green0/green1/green2/green3 all
    0 red): (1) `Epoch::next_batch` returning `None` for an empty slice — the
    empty-epoch-is-an-end defect — reds 1 fixture at code 13 (the tick clause);
    (2) `Weighted::retract` at +1 reds 1 at code 19 (the mixed-sign clause);
    (3) `Buffer::rewind` not un-sending reds 4 — `stream_buffer_degenerate`,
    `stream_rewind_door_admitted`, `stream_traversal_buffer` and this fixture at
    code 24 (the z⁻¹ clause), which is the fire count that shows the replay
    clause reads the shared door rather than a private one.
  * **THE CONSUMPTION SEAM IS DECLARED, NOT ATTEMPTED, AND THE REASON IS TWO
    NUMBERS:** 70 of 70 emitted `_apply` fns take a delta parameter list
    TEXTUALLY IDENTICAL to their own batch fn's, so a stream-spelled delta
    re-spells every ordinary query's signature or forks the two lists; and all
    124 DRed phase fns take `&[…]` of derived facts whose sign is fixed by
    PHASE, where a per-row weight would be wrong in the direction
    `incr_eligible` already records ("weights may never flow naively into an
    SCC"). The seam is therefore `<q>_apply`'s parameter list ALONE — named in
    §10, owed by the stage that changes the incremental surface.

## Consequences

The execution plane stops multiplying representations: a new storage
implements batch producers + declares ops, and every consumer already
compiles against it. Materialization becomes visible, priced, and justified —
the "justification drifts from mechanism" class loses its habitat on this
axis, because the mechanism IS the justification carrier (types + plan nodes).
The Memoria API changes (handle, seam pin vocabulary) are prerequisites and
are co-owned with the container⟂store plane (ADR 0020).

Cost: rexpr_walk's scan/join/sort/aggregate emission is rewritten across the
slices; the Memoria handle change is ABI-breaking (bump owed as its own step).

## References

- **`0025-criteria-and-instruments.md` — THE ARC'S DEFINITION OF DONE.** Victor's
  three criteria as stated, the operational reading of each, the instrument that
  reads it (`tests/logos/criterion1_materialization_instrument.sh`,
  `tests/logos/rc_seam_gate.sh`), the reading on the current tree, and §4: the
  residual inventory — every open item of this arc in ONE walkable list (the S5
  audit's F3/F5/F7-F10, this document's own owed bullets, the compiler residuals
  of tasks #50/#51). ⚠ Numbers quoted in the slice journal ABOVE were measured
  when their slice landed; the criteria file is where they are kept current, and
  it supersedes any figure here that it re-measures — in particular the recorded
  `609/4,023 = 15.14%`, which it shows was a TEXT ratio whose numerator was
  mostly `no materialization` ABSENCES.
- ADR 0024 (typed plan IR; S6 declared operation sets — unchanged here).
- ADR 0020 (container⟂store split; emitter-parameterized store specifics).
- ADR 0013 (DBSP; §10).
- ADR 0023 (node stream-CRUD algebra — the WRITE-side sibling of this plane).
- `stdlib/mem/bt/view.logos` — the borrow-carrying idiom this generalizes.
- `stdlib/mem/pkd/pdtbuf.logos` — the columnar leaf the batches wrap.
- `1f2dabe1` cursor navigation — the walk this ADR turns into leaf batches.
