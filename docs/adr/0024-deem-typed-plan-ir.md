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
