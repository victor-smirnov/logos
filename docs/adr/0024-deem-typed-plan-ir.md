# ADR 0024 — Deem's typed plan IR: the universal query compiler

Status: ACCEPTED (design pinned 2026-07-27, Victor + Claude PAIR).
Scope: the whole Deem compilation path — the expression language, the relational
algebra, the planner, and codegen. Canon becomes a supplier of facts and a
codegen instrument for it, not its owner. Memoria containers become one source
kind among Writ, `mem` and library structures.

## Problem

Deem is a second, weaker language embedded inside a strong one, and it pays for
that twice.

**It has a value domain of its own.** `stdlib/mem/wql/el.logos` defines exactly
four: `EL_TY_INT` (an i64), `EL_TY_STR`, `EL_TY_BOOL`, `EL_TY_FLT`. Sema rejects
any rel column outside `i64/str/bool` — identically for containers, slices and
mappings, in two copies of the same three-way string comparison
(`sema_collect.cpp:2770`, `sema_expr.cpp:21471`). Every consequence follows from
that one fact:

- a container column of type `u64` must become an EL value, so the generated
  producer emits `as i64` — and `u64::MAX` arrives as `-1`. `ctr_plan_pushdown`
  records this as expected behaviour, which is how a data-loss bug becomes a
  documented invariant;
- `str` works not because that path is typed but because STR is one of the four;
- `TreeMap<String, MyStruct>` is not merely unsupported, it is INEXPRESSIBLE;
- there is no type checking of query expressions at all. A mixed-type comparison
  is caught by the HOST compiler on generated code, at `<metaprog>:6` — a
  position inside a synthesized string, naming neither the query nor the column.

**It has no plan.** `RExpr` (`RScan`/`RFilter`/`RProj`/`RJoin`/`RAnti`/`RAggr`/
`RSort`/`RLimit`/`RDistinct`) is built, then handed to ~4000 lines of emitters
that write source text with `push_text`. The "plan" is control flow inside the
code generator. Therefore:

- pushdown had to be WELDED into `__deem_bind` as a special case, because there
  was nothing to rewrite;
- a decision's justification has nowhere to live, so "why a scan and not a seek"
  must be reconstructed rather than reported;
- "materialize or stream" is nobody's decision — the emitter always materializes
  a `Vec` and then filters it, because that is how it is written;
- every improvement is necessarily a small piece: there is no object to apply a
  rule to.

An optimizing compiler without an IR it can rewrite, cost and explain is not an
optimizing compiler. That is the root cause of "solving a big problem one small
piece at a time".

## Decision

**Deem stops having a semantics of its own.**

1. **No value domain.** A query expression is typed by the Logos type system,
   accessed reflectively — the same machinery, not a reimplementation. Deem's
   compiler already runs inside metaprog and can ask. A column's type is a Logos
   type, full stop. What may be done to a value is decided by TRAIT MEMBERSHIP
   (`PartialEq`, `Ord`, `Hash`, `Add`, …), not by a tag switch. `e.key == 7`
   type-checks the way Logos does, literal inference included.

2. **The plan is data.** `RExpr` grows into a real IR: every node carries a
   TYPE, a POSITION, a COST and a JUSTIFICATION. Planning is a pass that
   produces it; codegen is a separate consumer that reads it. Pushdown, access-
   path choice and materialize-vs-stream become rewrites and cost decisions over
   the IR instead of special cases inside emitters.

These are one decision at two levels. An untyped plan cannot be costed (cost
depends on the key type and on the source's capabilities) nor verified (a
rewrite must preserve meaning, and meaning is types). Types without a plan have
nowhere to live between parsing and text.

**Diagnostics are Deem's.** Deem reports; Canon supplies facts. The bar is
justification in the DL-reasoner sense: not the conclusion but the minimal set of
facts and rules entailing it. Because the capability plane is relational, "why"
is a DERIVATION, and the antecedent that failed is the "how" — the remedy.

**Sources declare, the planner never branches per domain.** A structure joins
the query plane by declaring a relation with typed columns plus a set of access
operations, each carrying a demand pattern (`key == k`, `key >= k`, `pos == i`),
a cost, an exactness (exact/superset) and a native iteration order. The
declaration a factory generates and the one a human writes are the same object;
that identity is the test that the planner is not Memoria-shaped.

## What already fits, and is kept

- **The algebra.** `RExpr` is a tree of the right shape. This ADR grows it; it
  does not replace it.
- **The capability seam in the emitter.** `join_key_caps(el_ty) -> KeyCaps{hash,
  ord,eq}` with a strategy cascade (hash/tree/loop) reading only that row. Its
  own comment anticipates this ADR: the structural version swaps the body for
  metaprog `has_trait` queries without touching the selector. Emitted joins
  already write `HashMap<K, Vec<i64>>` with K as real type text.
- **The natspec transport.** `MacroParams` already carries `rel_rowty` and
  `relc_ty`; the walker was made source-agnostic when natspec replaced the
  `rel_nkind` flavour switch and knows no source type today.
- **Cursor navigation** (`1f2dabe1`). `next`/`prev`/`skip`/`seek_key` on both
  substrates are what lets a plan choose to stream instead of materialize.

## Slices

Each leaves the tree green (L4) and is independently valuable.

**S0 — POSITIONS.** Query nodes carry a source span. `wql.peg` fields gain it;
both generated parsers (C++ `wql_surface_parser.hpp`, Logos
`wql_surface_parser.logos`) fill it. ⚠ Provenance must survive mapping fusion:
`enrich_deem_params` does `raw_text = prefix_body + raw_text`, so an offset in
the fused body points into a synthesized string — a node must know whether it
came from the query or from a mapping, and which. Without this "where" lies
exactly where mappings are in play. No semantic change; nothing reads the spans
yet.

**S1 — THE TYPE ORACLE.** A Deem-side facility answering, for a Logos type:
does it implement trait T; what is the type of field F; what is the result type
of `a op b`. Backed by metaprog queries — Deem asks the compiler rather than
modelling types itself. New facility, no behaviour change until used.

**S2 — TYPED COLUMNS.** A rel column's type is a Logos type. Admission is a
capability predicate through the oracle (Eq+Hash required for set semantics and
joins; Ord where an order is used) instead of the three-way string compare, with
a diagnostic naming the missing trait and why it is needed. Generated producers
stop casting: `Vec<(K, V)>`, not `Vec<(i64, i64)>`. Observable win: `u64::MAX`
stops being `-1`, and `ctr_plan_pushdown` stops recording data loss as an
invariant.

**S3 — TYPED EXPRESSIONS + DEEM DIAGNOSTICS.** Every expression node gets a
type; mismatches are reported BY DEEM, at a position, in the user's terms
("column `name` is str, compared with an integer literal"), never by the host
compiler on generated code. Needs S0 + S1 + S2.

**S4 — THE PLAN AS DATA.** IR nodes carry type, position, cost and
justification. Decisions move out of the emitters into a planning pass: access
path, pushdown, materialize-vs-stream. `__deem_bind`'s welded pushdown becomes a
rewrite. The planner returns a decision WITH its justification — recorded at the
moment of deciding, because a returned `"scan"` has already lost it.

*Landed so far:* **S4a** — one decision channel; the join strategy cascade
records its ground alongside the access path. **S4b** — the ACCESS plan is an
object (`logos.std.wql.access_plan`): deciding is read-only and returns an
`AccessPlan`, applying is the only thing that mutates, and every rel reports
including the ones left scanning. Cardinality enters as an ordinal class, which
is what makes choosing among covering operations a comparison rather than a
search order. **S4c** — MATERIALIZE-VS-STREAM is a decision. The opt-in is the
producer's RETURN TYPE: a source that returns an `Iterator<Row>` may be consumed
in place, one that returns a container is drained. A keyword would have been
weaker — it can drift from what the function does, and a return type cannot. The
planner still has to prove the plan reads the source ONCE, and that proof starts
conservative (a lone native rel under a simple scan with no `order by`);
widening it is how a join's driving side learns to stream. Canon's generated
families stream as well: their four producers are one walk type with four
constructors, so a store-backed container pays no materialization for a query
that reads it once. **S4d** — the single-read proof became a PER-REL fact, so a
join streams its DRIVING side (the outermost loop of a left-deep nest, read once
by construction) while its steps are drained. **S4e** — the ACCESS decision
stopped being shape-specific: it matched `RQuery::Simple` and did nothing
otherwise, so the same filter over the same source narrowed in a scan and was
ignored in a join or an aggregate. The shape is read in one place and
contributes one thing — whether an exact access may RETIRE the filter, which
only a simple scan's `where` permits, because a join's ranges over every bound
variable. **S4f** — the JOIN-STRATEGY decision moved out of the emitter
(`logos.std.wql.join_sel`) and onto the plan: it is made before the prelude
exists and recorded on the step's IR node, so a step whose strategy reads its
source once (hash, tree) streams it. The equi-key selection rule has one copy,
used by the planner to CHOOSE the key term and by the emitter to compose text
from the term the plan chose. A streamed step's hash index holds the ROWS
rather than row indices — there is no slice left to index — which also removes
the second structure the drained path built. **S4g** — a source's SIZE is a fact
the plan ASKS FOR. `card` stays an ORDINAL, because expansion time has no
container to count; what a compile-time plan can hold about a run-time number is
the asking, so `AccessPlan` records the reporter, the binding the answer lands
in, and — when the source declares none — that it could not ask. The two facts
have different consumers and neither replaces the other: `card` orders candidate
accesses in `plan_narrow_rel`, which must choose where no data exists; the
measured number is for a decision made where the data is. DECLARED like every
other capability on this plane (`size <rel> = <fn>;`, one per relation): a
container publishes `measure count`, a `mem` collection has `len`, and a
hand-written source may have neither — deriving a size from a source's shape
would exclude the third, which must stay usable. Asked ONCE, in the prelude
beside the source's own binding, so it cannot become a per-row cost however the
walk below it is shaped. ⚠ `size` needed no grammar rule of its own — it is
`rel_bind`'s shape and REL_KW tells them apart — but it did need the AST→source
RENDERER to stop printing the constant `"rel "`: under `-g` a dump is REPARSED,
so a lead the renderer cannot say comes back as a duplicate rel binding. That is
the fifth time in this arc the reading instrument was blind exactly where
emitters write. **S4h** — EITHER SIDE MAY DRIVE, and the number has its consumer.
The join's first pair is emitted TWICE and a discriminant computed once, in the
prelude's scope, from the sizes S4g asked for (`(rs).len() > (ls).len()`, or the
declared reporter's binding where a source has one) picks the nest that indexes
the SMALLER side. Four things differ between driving from `a` and from `b`, and
only the first is a swap of names: the outermost loop's source; WHICH HALF of the
equi-term is the build key (`equi_term_sides` splits the term by the step's NEW
row var, so exchanging the vars exchanges build and probe); therefore the
STRATEGY, a decision of its own that the reverse direction may not even have
(recorded as its own trace line, made by the one copy of the rule in `join_sel`);
and the PREDICATE, which is stored on the step but constrains the PAIR, so it
moves to whichever source is now the step. The nest became a function of the
chain (`chain_nest_frag`), so a second order is a second CHAIN
(`chain_swap_head`) and not a second emitter — and the transposition is of the
FIRST TWO sources for a reason that holds at any chain length: both vars are
bound by position 1 either way, so no later predicate becomes unevaluable, none
has to merge with another (a step carries one `on`), and no later strategy
changes. ⚠ THE LICENCE IS A TOTAL SORT. A join's row order IS the nest's, so the
same multiset in b-major order is a semantic change — the failure that returns
different rows without crashing. The sort's comparator now falls back to the
row-index TUPLE, lexicographic in QUERY source order, so the sorted sequence is
the same whichever nest collected it; and the tiebreak is a no-op for every
collection whose tuples already ascend (a scan, an aggregate's groups, a join in
query order), which is why it changed no existing expectation. Hence the plan
REFUSES without `order by` — there is no sequence to restore to — and refuses an
ANTI step (not symmetric: the base rows with no match are a different question
from the step rows with no match), a TRAVERSAL step (its source is a field path
of an outer row, so it cannot be an outermost loop at all), a step the planner
never decided (a rel body's own chain, re-emitted per fixpoint variant), a
reverse direction with no indexable equi-key, and a side whose size cannot be had
without draining it. Every refusal is a trace line rather than a silence, and the
aggregate shape is left fixed on purpose: its output order is its groups'
FIRST-OCCURRENCE order, which the sort does not restore. **S4i** — THE PLAN IS A
RUNTIME VALUE, chosen once and reusable: JDBC's PreparedStatement, and the answer
to the framing this whole sub-arc rests on. A query compiles when nothing is known
about the data, so what a static plan can optimize is bounded; by the time the
data exists the code is fixed. S4h answered with two nests and a run-time test —
but a test re-evaluated on every call is a property of the FUNCTION, not an object
an application holds. So each query now emits FOUR items
(`logos.std.wql.prepared`): a plan TYPE, `<q>_prepare(sources)` which measures and
decides, `<q>_run(plan, sources)` which is the query's real body, and `<q>`, which
becomes prepare-then-run so nothing existing changes behaviour. The per-row shape
is untouched: `if (__pl.swap)` reads a field once where the size comparison used to
be read once, and the loops below it are the same specialised ones.
⚠ THE FACTS ARE THE POINT, NOT THE BIT. "Reusable on data of the same
distribution" is a CLAIM BY THE CALLER, and an unchecked claim yields a silently
pessimal plan — so the plan carries the two sizes it measured, not just the
decision they produced. `agrees(&fresh)` then makes re-checking a COMPARISON
rather than a guess (this plan's decision against a fresh measurement's facts),
`margin()` says how close to flipping it is (1000-vs-999 is right and fragile;
10-vs-10000 is right and robust, and only the numbers distinguish them), and
`explain()` returns the ground. That is EXPLAIN addressed to the program rather
than to a human. ⚠ AND `prepare` MAY NOT RUN THE QUERY, which is the property the
gate exists for: its whole body is the declared size reporters plus the
comparison — no prelude, no rel fixpoint, nothing drained — because a re-check
that cost as much as the query would be advice nobody can take. That tightened
S4h in one place: a size obtainable only by MATERIALIZING a rel is no longer
usable, since a plan prepared over the input data cannot have it — a derived
relation's row count is a result of the query's own work, not a fact about its
input. ⚠ THE SURFACE IS UNIFORM, including for queries with nothing to decide (a
scan, a find, an aggregate, a join without `order by`): they prepare too, report
`dyn_order == false`, and agree with everything, so a caller can re-check every
plan it holds without knowing which ones had a choice. The generated HELPERS a
query is made of (a rel's own fn, an SCC's semi-naïve driver) get no surface —
they are internals of one body, not queries an application holds. ⚠ THE
OBSERVABLE IS AN ORDER, NOT A COUNT, and the fixture had to be built around that:
in a hash join each side's key is computed exactly once per row of that side in
BOTH nests, so the two orders do the same amount of work and no count
distinguishes them. What differs is WHEN — the indexed side's keys are all
computed before the other side is touched — so the fixture ticks each half of the
equi-key through an identity UDF and reads the FIRST tick, then asserts the counts
are EQUAL precisely to pin that they could not have been the discriminator. Two
data sets whose better side differs, one prepared plan each, both plans run against
the same data: same rows, different driving side. ⚠ MEASURED COST OF THE TIGHTENING,
recorded rather than glossed: two corpus queries (`wql_writ_graph_e2e`,
`wql_gpath_e2e` — joins over a Writ-graph rel with `order by`) had a second nest
under S4h and now keep one, because their sides are only measurable after
materialization. Rows are unchanged and the refusal is a trace line; the REMEDY was
read as the mechanism this plane already has — those sources declare no `size`
operation, and declaring one (S4g) restores the choice without a special case.
**S4j found a better remedy, and it makes that one wrong for these sources.**

**S4j — THERE ARE TWO DECISION POINTS, AND S4i NAMED ONLY ONE.** S4i's tightening
is right about `prepare` and was read too widely. A derived rel's row count is not
a fact about the INPUT data — hence unusable at point (1) — but by the time the join
builds its index the query has already materialized that rel for its own reasons:
the binding exists and its `len()` is the same field read the nest's own loop
condition makes. So the number point (1) could not afford is FREE at a second
point, and refusing there was refusing for a reason that had stopped applying.

  * **(1) `prepare`** — before any data is touched. Facts: the parameters and the
    sizes sources DECLARE. Reusable across calls. The PreparedStatement; unchanged.
  * **(2) inside `run`** — after the prelude's unavoidable materializations, before
    the join's index. Facts: (1)'s, plus the `len()` of every rel the query
    materializes ANYWAY. Per-call, therefore not reusable.

Built as a DEFERRED HALF OF THE SAME PLAN, not a second mechanism: one plan object,
one decision channel, one `why` vocabulary. `join_order::run_size_expr_of` is asked
only after `size_expr_of` has failed — a reusable decision is worth more than one
that is merely right per call, so the deferral is the fallback and never the
preference — and the emitted `run` gains exactly three statements at BODY LEVEL,
where the prepared plan's field read sits: two `len()` reads and
`let __defer_swap: bool = __pl.order_swap(__defer_n0, __defer_n1);`. Per-row cost
does not move; the branch is one `if` above every loop, as `__pl.swap` is.

⚠ THE RULE LIVES ON THE PLAN, in one place: `order_swap` is a method, `agrees`
re-derives the prepared decision through it, and the deferred binding calls the same
method — so the two points cannot reach different conclusions from the same pair of
numbers, which is the entire claim that they are one plan. ⚠ A DEFERRED DECISION IS
NOT A PREPARED ONE, and the plan says so instead of leaving it silently true:
`defer_order` is its own field (not a third value of `dyn_order`), `base_n`/`step_n`
stay `-1` because nothing was measured, `agrees`/`margin` answer about the PREPARED
half only, and `settled()` is the accessor that discloses which halves a caller has
actually pinned. The trace distinguishes them too — `drive either side` vs
`drive either side in `run``, plus a second line naming the two expressions and
where in `run` they become free.

⚠ WHAT IS STILL REFUSED, and would be wrong to relax: an anti join is not
symmetric; a traversal step's source is a field path of an outer row; without
`order by` the nest's order IS the answer's order. And a STREAMED side has no
length at either point — counting means draining the iterator, the query's own work
traded for a plan fact, which is the error this axis exists to refuse.

⚠ MEASURED, AND IT CONTRADICTS THE OBVIOUS DESIGN: that streamed-side guard CANNOT
FIRE on this axis. Streaming requires the ABSENCE of `order by`
(`plan_mark_single_pass`: a sort re-binds joined rows by position, so every side
must be indexable) and a transposition requires its PRESENCE — the conditions
exclude each other, so every side reaching `run_size_expr_of` is materialized. The
guard stays, because the coupling is another module's decision and a streaming
order-preserving sort would end it silently; a function right only by virtue of a
distant invariant it does not name is the phase-proxy anti-pattern. What the fixture
measures instead is the consequence: an ITERATOR producer whose length is free
*because* the sort drained it anyway, with a pull count pinning that the deferral
added no second drain.

⚠ AND A SECOND REFUSAL WAS ADDED, not removed: a SELF-JOIN. Both sides being the
same binding makes the two sizes equal by construction, so the comparison can only
ever answer "do not swap" and the transposed nest could never be selected. The two
`wql_gpath_e2e` queries S4i costed are exactly this shape — a gpath lowers to a
self-join of the edge relation — so what they lost was never available, and the
"declare a `size`" remedy would not have restored it. The Writ-graph source still
must not declare one: `__gs_edges_<T>` builds its edge list by walking the struct,
so a declared size would either cost the query's own work or report a lie.

*Remaining:* the plan holds only the join-order discriminant, because access is the
axis where a run-time decision is nearly always the same answer — a plan field for
it would be a mechanism with no consumer.

**S4k — BEYOND THE FIRST PAIR: A COST, A DERIVED SET, AND A STATED BOUND.** S4h–S4j
transposed the FIRST PAIR and compared the two candidates by ONE number each. With
three or more sources that stopped being a decision: it compared two members of a
space, on a fact that does not distinguish the rest of it. Three things replace it.

**The cost function is one stdlib function that generated code CALLS**
(`logos.std.wql.join_cost::jc_order_cost`). It charges ROW EVENTS: a base SCAN 2 per
row, an index BUILD 4 per row, a PROBE 1 per row reaching the step, a RESCAN 2 per
row read, with the intermediate estimated as `R × n / 10`. Every plan carries a
`JCTable` — the admissible orders as permutations of its own size facts plus the
cost role each position holds — and `order_pick`, `cost_of`, `margin` and `agrees`
all go through the one function. It is NOT emitted per query: the same rule spelled
once per query is one chance per query to drift from the account the trace gives, in
the place where drift is silent.

⚠ THE PAIR RULE IS THE DEGENERATE CASE, not a branch inside it. For k = 2 the two
costs are `3n₀ + 4n₁` and `3n₁ + 4n₀`, so the query's order wins exactly when
`n₁ < n₀` — "index the SMALLER side, walk the larger", which is `step_n > base_n`
and nothing else. `margin` as the runner-up's cost minus the winner's comes out as
`|n₀ − n₁|`, which is the number S4i's plans already reported. That the weights had
to be BUILD > SCAN + PROBE for this to hold is not a coincidence to hide: an index
insert writes, may grow the table, and touches an allocator, while a probe and a
sequential row read do not.

**The admissible set is DERIVED, and every constraint comes from what the emitter
requires** — an illegal order does not crash, it silently returns different rows.
C1 a total sort is the licence (without `order by` the nest's order IS the answer's).
⚠ and TOTAL is a claim about the KEY'S TYPE, not about the presence of `order by`:
the sort's row-index tiebreak sits behind an EQUAL compare, so a key whose comparison
is partial never reaches it. Measured on an `order by <f64>` with a NaN present: four
carried nests, four different row sequences from the same data, and user-visible with
no plan field written by hand — appending rows that match NOTHING moved the cost
numbers, the plan picked another nest, and the answer's order changed. The 2-source
transposition carried the same false premise from the start; the licence now asks
`el::el_total_order` about the key's EL class, and that is the ONE answer to "which
key types admit a total comparison" for every consumer of the sort (the sort's
tiebreak, this licence, the recursive aggregate's lattice). It is DEFAULT-DENY: `str`
is byte-lexicographic and keeps the licence, f64/f32 lose it, and a class nobody has
added yet licenses nothing until it is admitted there deliberately. A key that fails
it refuses the reorder WHOLE with its ground on the trace and in the plan's `why`, so
`explain()` states the failed antecedent and the remedy at run time. What f64
`order by` MEANS for a single nest is untouched — a licence was removed, not a
capability, and redefining the comparison (a total NaN order) would move the answer
of existing single-nest queries, which is a separate question. ⚠ A single-nest f64
sort with a NaN in it is therefore still not an ordered result: it is a deterministic
function of the input, which is all this constraint needs, and the partiality remains
visible in the answer.
C2 a plan must have decided the chain (JS_NONE = nobody looked). C3 a PINNED step —
an anti join, a traversal — keeps its depth in the bound stream: an anti's predicate
is inseparable from its source and a traversal's source is a field path of an outer
row. C4 every slot's dependencies precede it, which is the constraint that does the
work on real chains. C5 `on` moves with the transposition and attaches to the LAST of
the vars it reads; two predicates onto one position is inadmissible, because a step
carries one `on` and nothing builds a conjunction. C6 no candidate may turn a
read-once source into a rescanned one, because `plan_walker` decided
materialize-vs-stream against the strategies the query's own order got. C7 an order
whose (size, role) sequence an earlier candidate already has cannot be cheaper for
any data — the self-join refusal S4j added is the pair-shaped instance of it, now
derived rather than special-cased.

⚠ THE JUSTIFICATION IS THE WHOLE CENSUS, not the winner. Every permutation is
reported on the one decision channel by the row-var sequence it names, admitted or
refused with the constraint code that refused it, plus a summary line giving
enumerated / admissible / carried and the cost model's weights and its one
assumption. At run time `cost_of(ix)` prices any candidate from the plan's own facts,
so "on what ground did each loser lose" is a question the PROGRAM can answer.

**The bound is stated and the fallback is loud.** Four floatable sources (the plan's
fact table is four wide, the search is 4! = 24) and four carried nests. Measured: a
three-source body goes from 3 536 bytes at one nest to 15 366 at four — the nest is
~3.6 KB and the shell is small, so the artifact grows ~4×. When the derivation proves
MORE admissible orders than the artifact carries, the plan declines the reorder WHOLE
and the trace says how many it proved: carrying "the first four" would mean choosing
three challengers by enumeration order, which is not a decision, and dropping the
rest without saying so is the failure the bound exists to make visible. The corpus's
four-source `q5` shape proves 8 of 24 legal and carries 1.

⚠ WHAT THE COST FUNCTION IS NOT GIVEN, and both are facts nothing in this compiler
reports. SELECTIVITY: no source declares a distinct-value count, so an equi-step's
output is the classic no-statistics tenth of the cross product — the one number here
that data could contradict, written as a function so there is one place to consult
when a source can declare it. And a PINNED step is UNPRICED: a traversal's field-path
length and an anti's filtered fraction have no reporter. The derivation keeps every
pinned step at the same depth in every candidate, so the omission does not favour one
candidate's shape — but the intermediate reaching that depth differs, so that is not
neutrality either.

⚠ TWO GUARDS IN THIS SLICE CANNOT FIRE ON ANY QUERY THAT EXISTS, and saying so is
the point. C6's refusal needs a candidate order in which a source the query read once
loses its equi-key; with scalar keys `equi_term_sides` is symmetric on `==`, so the
reverse direction of a two-source chain always has the same key and a three-var
predicate is LOOP in every order. C5's "a predicate lands on the base" needs a
predicate whose every referenced var sits at position 0, which for an equi-term means
a single-var predicate that is not a join key at all. Both stay, for the same reason
S4j's streamed-side guard stays: they are claims about another module's decisions, and
a function right only by virtue of a distant invariant it does not name is the
phase-proxy anti-pattern.

⚠ AND THE ARTIFACT BOUND WAS NOT ONLY A PREFERENCE — IT WAS ENFORCED BY A SILENT AST
CORRUPTION. `logos_emit_item_blob_subst` front-loads one realloc of the substitution
arena because the walk holds raw pointers into it, and the bound it reserved counted
the TEMPLATE's bytes but not the bytes of the plain `#(fragment)` splices, which are
not in the template at all. A four-nest join body crossed it: just under the
threshold sema reported "not all paths return a value" about a body that visibly ends
in `return`, just over it the compiler segfaulted in `map_of`, and the `--gen-dir`
render of the same document came out CORRECT in both cases — so every piece of
evidence pointed at the front end. `GrowableSingleChunk` growth reallocates the one
chunk rather than adding a second, so `chunk_count()` stayed 1 and said nothing; the
base POINTER moving is the fact, and the fix checks it after the walk so that a wrong
bound is a diagnostic instead of a corrupt AST. Pinned by
`quote_large_fragment_splice`.

**S4l — THE APPARATUS OF CHOOSING IS CARRIED ONLY BY PLANS THAT CHOOSE.** S4k put
`tbl: JCTable` on EVERY emitted plan, and 249 of the corpus's 266 `prepare` bodies
filled it with `jc_table_none()`. A `JCTable` is 2 + 16 + 16 `i64` = 272 bytes
returned by value, so a query with no choice to make paid, PER CALL, a 272-byte stack
write and an out-of-line cross-module call for a field it cannot read. Measured at
`-O2` on `wql_prepared_plan_e2e::evens`: the direct fn's frame 32 → 312 bytes, calls
1 → 2, and under `--emit-llvm-opt` the fetched table is provably dead — it survives
DCE only because a call to another module is not `readnone`. The same 32 → ~304-312
on `wql_find_e2e`, `wql_join_e2e`, `wql_query_e2e` and `deem_hashmap_source`; the
fixed `prepare` bodies went 12 → 79-81 instructions and 0 → 288-byte frames, all of
it insert/extract over the 34-cell zero table.

⚠ BE EXACT ABOUT WHAT THAT VIOLATED. "Per-row cost does not move" HELD — the per-row
body never touched `tbl`. What failed is "no hidden work", and it failed where it
matters most: a point-get query invoked in an application loop, which is the shape the
whole prepared surface exists to serve.

The field is therefore CONDITIONAL on one predicate the emitter already had — does the
derivation have a table to give this plan (`Prepared.tbl` non-empty: the prepared half
and the deferred half both do; a fixed or refused plan does not). After, on the same
five fixtures: every fixed `prepare` body is 18 instructions, NO frame and NO call
(was 79, 288 bytes, 1 call), every fixed direct fn's frame is 0 or 32 bytes (was
272-312) with one call fewer — `evens` 21/2/312 → 16/1/32, `find_41` 24/1/272 →
17/0/0 (the table also blocked inlining `run` into the direct fn), `adults`
49/3/304 → 46/2/32 — and `jc_table_none` appears in no emitted artifact at all.
Plans that DO have candidates
are byte-for-byte unchanged — `by_key`, `q3`, `q4`, `qi`, `qs`, `iter_step`,
`via_rel`, `db_i64_leaves` do not move — because the saving is not taken from them.

⚠ WHAT IS NOT CONDITIONAL. The SURFACE: both plan shapes carry the same eight methods
with the same signatures, `dyn_order`/`defer_order`/`order_ix`/the four facts/`why`,
so one loop over a caller's queries is still one loop — S4i's reason for emitting the
surface where there is nothing to decide is untouched, and only its COST changed. And
the COST MODEL: the four answers a table-less plan gives are `jc_none_cost` /
`jc_none_pick` / `jc_none_margin` / `jc_none_ncand`, four functions in
`logos.std.wql.join_cost` beside the cost function, and `wql_plan_no_table_e2e` PINS
each equal to `jc_order_cost`/`jc_order_pick`/`jc_margin`/`ncand` applied to
`jc_table_none()`, both sides evaluated in the same run. Writing `0i64` and `-1i64`
into the emitted impl would have put the cost model's boundary behaviour in a second
place that no test compares to the first.

⚠ AND THE ABSENCE IS DATA. A plan with no table says so, in `explain()` and on the
`plan_trace` channel — composed from ONE buffer, so the object and the channel cannot
disagree — because otherwise `considered() == 0` and `cost_of(ix) == -1` read as a
table that answered nothing rather than as the absence of one.

⚠ A ROUTE THAT WOULD HAVE BEEN BETTER IS BLOCKED BY A COMPILER DEFECT, recorded here
rather than attempted. Every `JCTable` is a COMPILE-TIME CONSTANT, so the ideal shape
is one `static` per query plus a shared empty one, with the plan carrying
`&'static JCTable` — 8 bytes, no call, one struct shape, and the dynamic plans' 34
`insertvalue`s per call would go too. Struct-typed statics do not work: `static T: S =
S { … }` with array fields is rejected outright ("initializer must be a literal
expression"), and with scalar fields it MISCOMPILES — the global is emitted as
`@T = global ptr null` (8 bytes) with the initializer lowered to runtime stores into
it, so a 32-byte struct's fields are written PAST the global. At `-O0` a 32-byte
`memcpy` into the 8-byte global; at `-O2` all but one store folded away and the reads
returned garbage. That is an out-of-bounds write at every optimisation level, not a
missing feature, and it is a defect of its own rank.

⚠ ONE ANSWER TO "HOW IS A SIZE FACT BOUND", at both decision points. The deferred path
had no identity check at all (`db_i64_leaves_run` read `(__rel_g_sl).len()` three
times) and the prepared path deduped only the declared reporter CALLS by reporter
index — which a source with no declared reporter escapes, because its size is the
`(<src>).len()` fallback and has no index (`q3self_prepare` read `(as_).len()` twice).
Both now go through one binder that dedups on the expression TEXT, which is the right
granularity: two positions spelling the size the same way ask the same question of the
same source. `db_i64_leaves_run` now reads `(__rel_g_sl).len()` once and aliases two
slots to it; `q3self_prepare` reads `(as_).len()` once.

⚠ AND THE OBJECT CODE DID NOT MOVE — `q3self_prepare` is 136 instructions before and
after, `db_i64_leaves_run` 1234 — which is the measurement that says what the dedup is
FOR. LLVM already CSE'd the repeats, because a `&[T]` length is a load it can prove
redundant. The repeats therefore cost nothing today *by grace of the optimizer*, and a
`len()` the optimizer cannot prove redundant (an opaque call) would have been paid per
duplicated slot. The dedup makes the property structural instead of incidental; it is
not a speedup and is not claimed as one. The surviving question is then recorded rather
than fixed: `run_size_expr_of` now STATES that `len()` must
be O(1), that this is the entire licence for deferring (the number is "already there"),
that every source reaching it satisfies that structurally, and that this compiler
cannot ASK — no capability reports what a receiver's `len()` costs. The remedy the day
one arrives whose length is computed is a declaration on the same channel as `size`,
not a special case in that function.

**S5 — CODEGEN AS A CONSUMER.** Emitters read the IR instead of deciding.
`push_text` gives way to quotes, which also settles the standing debt that
`push_text` is a workaround rather than the intended codegen surface.

*The conversion is DONE, BODIES INCLUDED.* `rexpr_walk.logos` has no
`Emitter::commit` and — since `dbe92778` — **no
`push_text` at all** (nor a `begin_chunk` caller; there is no `begin_chunk`
any more, see the census at the end of this section): all nine emitters (`emit_find`, `emit_simple`,
`emit_none_find`, `emit_identity`, `emit_head_row`, `emit_empty`,
`emit_join_chain`, `emit_aggregate`, `emit_rel_fns`) build BOTH the item and its
body as quotes through one shared shell, and `emit_fn_head` is gone. The only
text left is the SCALAR clauses — `emit_sexpr` renders a `where` / `select` /
`on` / group-key body in codegen.logos and `parse_expr` reifies it at the leaf.
Return types were the first single win: `") -> Result<Vec<"` … `">, ElError>
{\n"` used to be spelled once per branch with the body's opening brace welded
on, so a two-armed emitter carried two copies of the fn's syntax; the arms now
differ in the return TYPE alone.

⚠ THE PARAGRAPH THAT STOOD HERE SAID THE BODY MUST STAY TEXT, and gave the
reason: a generated body's shape is a runtime value (a join nest of `ch.n`
levels, one accumulator per aggregate, one semi-naïve variant per in-SCC source
occurrence) and a repeat produces a flat sequence. The premise was right and the
conclusion was wrong. What a runtime-shaped body actually needs is three
primitives, all of which now exist:

  * `#(body)` **wherever a block goes** — while / loop / for / if / else /
    unsafe / let-else, not only a fn body (`f8f715e9`). A nest of runtime DEPTH
    is then built by RECURSION: start from the innermost complete fragment and
    wrap it once per level.
  * **`let #n` / `#n = e`** — a binding whose NAME the emitter computes
    (`5e9488f3`). This was the hard blocker: every level of a join nest opens
    with `let __pk{s}` / `let mut __m{s}` / `let mut __j{s}`, so the conversion
    died at the first statement of every fragment.
  * **a statement LIST that composes flat** — `#frag;` inlines its statements,
    `{ #frag; }` still scopes them (`bdd9476c`). A body is a SEQUENCE of
    runtime-many statement RUNS (a rel prelude, one build phase per join step,
    the walker), each declaring bindings the runs after it read; without the
    inline a body could be wrapped but never appended to.

⚠ AND THE REAL OBSTACLE WAS THE EMITTERS' OWN SHAPE, not the grammar. Bodies
were written by open/close PAIRS — `emit_step_open` wrote `{`, `emit_step_close`
the matching `}`, separated by the whole inner body and a LEVEL COUNTER threaded
between them. A quote fragment is a whole construct, so a fragment that is only
an opening brace cannot exist. Every pair became "take a complete inner
fragment, return the wrapped one" (`step_wrap`, `member_block_frag`), the level
counter became that fold's recursion, and indentation bookkeeping
(`push_ind`/`ind_string`) lost its subject — a fragment has no column.

⚠ Equivalence was checked against the ARTIFACT, not argued: `--gen-dir` over all
155 compiling `wql_`/`deem_`/`query_` pass tests, before and after each step.
Exit codes identical test-for-test; the only surviving line differences are
`x.next()` → `(x).next()` and a streamed limit break losing a vacuous `true &&`.

⚠ REMOVED with the text: `emit_strategy_comment` and the `// join strategy: …`
trace. It never reached generated code — the body went through `parse_block`,
which drops comments — so no dump has ever contained it. A trace that survives
needs a channel, not a text push.

⚠ Two things the shell had to absorb. VISIBILITY is decided in one place — the
`-` prefix on a fn name (`vis_is_priv`, params.logos) that had no producer until
the rel helpers started building names with it instead of writing `"fn "`. And
the MULTI-ITEM case (`emit_rel_fns`: a helper fn per rel plus a driver per
recursive SCC) emits SEVERAL quotes rather than one, because no `parse_as` rule
reifies an ITEM LIST — `parse_block` reifies statements and a fn is not a
statement. Growing the shell to take a list would have meant handing it text to
re-split, which is the concatenation being removed.

⚠ AND AN IMPORT LIST IS A SET, which nothing enforced. A synth module's USES is
fed by three sources that cannot see each other (the quote's own imports, the
handler module's baked into the blob at lowering, the user module's merged
after), so the intersection landed twice and `sema_collect` warned once per
duplicate per emitted item — 103 warnings per full build before this arc, 819
after, on code the user cannot edit. `logos_emit_item_blob_subst` now sweeps its
USES once, at the end: 819 → 6, and the 6 are real duplicates in hand-written
source. ⚠ The dump renderer prints each package once, so `--gen-dir` showed
nothing wrong either way — the third time in this arc that the reading
instrument was blind exactly where emitters write.

*Enabling step landed:* the obstacle was never syntax. A generated fn's body can
be a loop nest whose DEPTH is a runtime value (a join chain of N steps), which no
fixed template expresses and `#( … )*` cannot either — a repeat produces a flat
sequence and a nest is not flat. `parse_as` already carried the intended answer
in its own comment (let an emitter BUILD fragments as strings and splice them
hygienically) but had no rule for the one thing an emitter's body is. The
grammar's `block` rule existed and was simply never exported; it is now rule 4,
reachable as `parse_block`. ⚠ CLOSED, and the paragraph that stood here said
otherwise: a `parse_block` fragment first spliced at a STATEMENT position, so a
fn's body landed as a nested block statement and cost one scope, and removing
that was written up as needing a new fn-rule alternative plus a block-typed
substituter slot. `c53675fc` added exactly that — `fn_body <- HASH LPAREN expr
RPAREN`, one rule shared by all four fn alternatives — so `#(body)` occupies the
body slot itself. Emitted fns are FLAT, and `logos_09_flat_emitted_body_*`
(tests/logos/flat_body_gate.sh) now holds that: a dump whose fn head is followed
by a bare `{` fails. ⚠ The extra scope was never merely cosmetic — putting it
back fails the stdlib build on `canon_split_fast` with "use of moved variable
`__out`", because a block changes what move analysis sees. But that is an
ACCIDENT of one query moving one local, which is why the property needs a gate
and not a memory of it.

The rest of the route is now proven too. `param_list` gained an antiquote
alternative — on the LIST, not on the dozens of fn rules — so parameters splice;
`type_ref` already handled the return type. Together those answer the mechanism
question this arc had left open: an `Emitter` does not need to learn to take a
`QuoteItemBlob`, because the quote route replaces the text CHUNK outright — uses
included. (Written before the conversion; it is DONE — see the head of this
section.)

⚠ IMPORTS WERE THE BLOCKER, and for two rounds the record said otherwise. A
converted emitter whose signature named a factory-generated handle (`Hs…` in
`logos.gen`) died with "unknown type", and that was written up as TIMING — a
text chunk being its own module compiled a round later, when the family is
registered. It is not: the chunk imported `logos.gen` all along (the natspec
carries the source param type's defining package; `native_use_text` renders it),
and the quote did not, because BOTH antiquoted import forms were no-ops.
`quote_item_expr` captures `USES` and `ITEMS` as two disjoint arrays — no `$...`
aliasing, unlike the `module` rule — and the quote lowering's placeholder walk
visited ITEMS only, so a `use #pkg;` was never numbered and the splice path
dropped it, while a `#( use #us; )*` group was never expanded at all. The tests
that "proved" both forms named packages the implicit prelude re-exports, so they
passed on a mechanism that did nothing; they now import a package the prelude
does not reach, which is what makes the compile an assertion.

⚠ AND THE READING INSTRUMENT WAS BLIND IN EXACTLY THE PLACES EMITTERS WRITE.
`--gen-dir` is the only way to read what an emitter produced, and under `-g` the
dump is REPARSED and the reparse REPLACES the synth doc — so the AST→source
renderer is not a display, it is a compilation stage. Three expression shapes had
no case in it: a unit enum variant / associated const (`ENUM_LIT`, i.e.
`Option::None`), the try operator (`TRY_EXPR`, `f(x)?`), and a bare block at
statement position (rendered `{ … };`, which this grammar rejects — Rust's
`block_expr ';'` statement form does not exist here). The first two degraded to a
`/* … */` comment, and a comment inside an argument list REPARSES: `Result::Ok(
Option::None)` came back as `Result::Ok()`. The round-trip's shape gate is a
top-level item census, so an arity change inside a body is invisible to it. The
existing `--gen-dir` corpus was all hand-written quotes, which happen to contain
none of the three; the gate now carries emitter output as well.

⚠ A FOURTH, and it kept the pattern exactly: a PACKAGE-QUALIFIED call. `pkg.path::fn(args)`
holds its package in `RECEIVER` + `QUAL_PARTS`, never in `CALLEE`, and both the
`CALL` and `GENERIC_CALL` render cases read `CALLEE` alone — so a dump called by
bare name. That text parses, censuses identically, and resolves to a DIFFERENT fn
(the form exists precisely to pick one of several same-named free fns), and a
same-named fn in the chunk's own package wins. Fixed in `5bc998e2` by asking
`extract_pkg_qualifier`, the function sema reads the qualifier with, so the two
cannot drift. Found because trama!'s codegen writes `logos.std.wql.el::wql_upper(…)`
for every `{{ upper(x) }}` and nothing hand-written did.

*THE SWEEP IS FINISHED, AND FIVE FILES ARE AT ZERO.* `rexpr_walk.logos`,
`mapping_item.logos` (`4c1014ba`), `catalog_macro.logos` (`6a081d14`),
`derive_graph_source.logos` (`8d8b9283`) and `codegen.logos` — the last because
`begin_chunk`, its chunk prologue, lost its final caller when trama!'s render fn
became a quote, and a function with no callers is deleted rather than kept. The
two halves it did are now structural: the import list is a run of `use` decls
inside each emitting quote (plus `#( use #uses; )*` for the runtime-sized
native-source part), and the package is not computed at all, because
`logos_emit_item_blob_subst` stamps it from the metacall SITE while
`logos_emit_source` had to be handed it in text.

The survivors are three, each for a reason that is a property of the file rather
than unconverted residue:

  * `trama_render.logos` — the AST→source RENDERER. Text is its OUTPUT: a Trama
    template's statement shape is the template's own nesting (a `while` per
    `{% for %}`, an `if` per `{% if %}`, to arbitrary depth), which is a runtime
    value no fixed template spells and no repeat flattens. Its ITEM is a quote;
    `parse_block` reifies the body once, at the body slot.
  * `emitter.logos` — the Emitter's own implementation. `push_text` is the method
    being defined; the rest are doc references to it.
  * `deem_bind.logos` — BLOCKED, twice measured, recorded at the site (`99abf493`).
    (1) The quote channel inherits the metacall SITE's package and this handler's
    site is a `package logos.gen;` driver chunk, while the overload must land in
    `cs.pkg`; `QuoteItemBlob` carries no package field. (2) `deem_def`'s NAME is a
    plain `IDENT` with no `HASH IDENT` alternative and its body is a
    `RAW_GROUP_BRACE`, so `deem #dn(…) { #(qb) }` is a syntax error and a
    malformed query respectively — and this handler's two inputs are exactly those
    two positions. Closing it wants a package on the quote channel and a raw-group
    reifier beside `parse_block`/`parse_params`.

**S6 — DECLARED OPERATION SETS.** Capability stops being derived from node
structure (`can_seek ← ordered_map ∧ measure(max,col)` is Memoria-specific) and
becomes declared per source. `can_seek` as one boolean is already known wrong:
`HashMap` answers `==` in O(1) and cannot answer `>` at all. Two independent
capabilities — point probe and ordered positioning — plus order and exactness as
facts of their own. A library `TreeMap<K,V>` joins here, by declaration.

## Consequences

The compiler becomes a separate entity, not segmented by domain: one query may
join a Memoria container, Writ and `mem`, because rows share a language of types
rather than a domain of four tags. `__deem_bind` HAS moved out of
`logos.lcm.canon.container_item`: it is `logos.std.wql.deem_bind`, and Canon is
its supplier of container facts (`logos.lcm.canon.spec`) and its codegen
instrument.

⚠ The move forced the supplier to become a module of its own, and the reason is
worth keeping. The binder consumes Canon, so it must be built AFTER logos-lcm —
which rules out the mem tier where the rest of `logos.std.wql.*` lives, since
logos-mem is built BEFORE logos-lcm (package name and build tier are different
things; that inversion is also how Canon may import the query surface at all).
It therefore sits in the lcm tier, the earliest one that can see Canon. But an
emitted `use` resolves names without LOADING a module, so the driver's metacall
chunk cannot pull the binder in by itself: the edge has to ride in from
`container_item`, which every container-declaring unit imports. That makes
container_item → deem_bind a required edge, and a binder reading container_item
would close the cycle. Hence `logos.lcm.canon.spec` — the declaration parser and
the fact builder, with no emission and no decision in it — below both the
container builder and the query binder. Canon-as-supplier stopped being a
description and became a module boundary.

Cost: `rexpr_walk.logos` (~4000 lines) is rewritten across several cycles, and
Deem grows a type layer. The slicing above is what keeps that from being a
long-lived broken branch.

## References

- ADR 0016 (deem mappings as first-class), ADR 0020 (Memoria as Deem's container
  plane + Canon), ADR 0023 (node stream-CRUD algebra).
- `1f2dabe1` cursor navigation — the walk that makes streaming possible.
- `d61c7db1` capability-directed pushdown — the special case this ADR
  generalizes.
