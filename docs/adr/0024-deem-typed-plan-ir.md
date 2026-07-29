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
the second structure the drained path built. *Remaining:* cost on the
relational IR nodes proper.

**S5 — CODEGEN AS A CONSUMER.** Emitters read the IR instead of deciding.
`push_text` gives way to quotes, which also settles the standing debt that
`push_text` is a workaround rather than the intended codegen surface.

*Enabling step landed:* the obstacle was never syntax. A generated fn's body can
be a loop nest whose DEPTH is a runtime value (a join chain of N steps), which no
fixed template expresses and `#( … )*` cannot either — a repeat produces a flat
sequence and a nest is not flat. `parse_as` already carried the intended answer
in its own comment (let an emitter BUILD fragments as strings and splice them
hygienically) but had no rule for the one thing an emitter's body is. The
grammar's `block` rule existed and was simply never exported; it is now rule 4,
reachable as `parse_block`. ⚠ It splices at a STATEMENT position — the quote
grammar has no antiquote alternative where a fn's BODY goes — so the fragment
lands as a nested block statement, costing one scope. Removing that needs a new
alternative in the fn rule plus substituter support for a block-typed slot.

The rest of the route is now proven too. `param_list` gained an antiquote
alternative — on the LIST, not on the dozens of fn rules — so parameters splice;
`type_ref` already handled the return type. Together those answer the mechanism
question this arc had left open: an `Emitter` does not need to learn to take a
`QuoteItemBlob`, because the quote route replaces the text CHUNK outright — uses
included. What remains is the conversion itself, emitter by emitter, with L4 as
the oracle.

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
