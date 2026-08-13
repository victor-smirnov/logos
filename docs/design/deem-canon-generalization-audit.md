# Deem / Canon / Memoria — generalization audit (2026-07-27)

A sweep for UNDER-GENERALIZATION across the query stack: places where one idea is
written several times, or where a rule that should hold for every source holds
only for the one it was written against. Ordered by value/risk, with the
evidence that made each visible. Closed items keep their entry — the point of
the record is that the same shape recurs.

The trigger was a small one: the showcase tests needed one result-set printer per
row shape. That turned out to be a mistake of mine (`Display` + the `ToString`
blanket generalizes it fine), but the question it raised — *what else is written
per-case that should be written once* — is what this audit answers.

---

## 1. The plan picks a producer by MANGLING A NAME — CLOSED (S6)

`__deem_bind` (then in `container_item.logos`, now `logos.std.wql.deem_bind`)
chooses the family's narrowing producer with a three-way `if` over a strategy
string, concatenating one of three fixed prefixes:

```
if str_eq(strategy, "point_get") { matfn.push_str("__ctr_at_"); }
else if key_op == OP_GT() || key_op == OP_GE() { matfn.push_str("__ctr_from_"); }
else { matfn.push_str("__ctr_upto_"); }
```

The family's operations are not DECLARED anywhere; the planner knows their names
by convention and their applicability by a hardcoded rule. A source that offers
a fourth shape has no way to say so, and one that offers only two cannot say
that either.

This is ADR 0024 **S6** (declared operation sets), and it is the largest single
item in this list. Everything below either feeds it or is blocked by it.

## 2. The POSITIONAL family has no narrowing producers at all — CLOSED (S6)

⚠ THIS SECTION IS DATED — IT DESCRIBES THE TREE BEFORE ADR 0025. Both family
arms have since replaced their per-row producers with LEAF-BATCH ones
(`__ctr_brows_` / `__ctr_bat_` / `__ctr_bfrom_` / `__ctr_bupto_`): ordered_map at
S1, positional/vector at S1b, both 2026-08-13, and the per-row spellings named
below no longer exist anywhere. The vector family also gained the three
narrowing producers it is said below not to have. The audit's ARGUMENT is
unaffected — it is about name conventions vs declared operations — so the text
stands as written history rather than being rewritten into agreement.

The ordered-map family publishes `__ctr_rows_` / `__ctr_at_` / `__ctr_from_` /
`__ctr_upto_`. The vector family publishes `__ctr_rows_` and nothing else, so
`from s r where r.pos > 100` scans the whole container — even though `seek` is
one descent and `skip` is an index bump (both landed in `1f2dabe1`).

Blocked by #3: the planner narrows on a column, and the position is not a column
Canon knows about.

⚠ Both sentences above are the 2026-07-27 STATE, kept because this list keeps its
entries. RE-MEASURED 2026-08-10: the positional family declares
`op row.pos eq/ge/le … exact` beside its walk, and `from v e where e.pos >= 2`
compiles to `__ctr_from_<Family>(v, 2i64)` with the filter retired — measured on
`tests/logos/pass/container_item_e2e.logos`, asserted by
`tests/logos/ctr_access_path_gate.sh`.

## 3. The position is a column NOTHING declares — DISSOLVED (S6)

`PositionalSource<V>` projects `row(pos: i64, val: V)`, but `container Series<V>
{ kind vector; entry { val: V } }` declares ONE column. The ordinal is invented
by the projection: it appears in the relation, not in the declaration, and
therefore not in Canon's `col_fact` / `max_meas`. Canon cannot reason about it,
so `can_seek` cannot hold for it, so #2 cannot be planned.

**It stopped being a problem rather than being solved.** The blocker was an
artifact of the model it was stated in: capability was DERIVED from Canon's
facts, so a column Canon did not know about could not be planned on. Once a
source DECLARES its operations (S6) the planner never consults those facts —
and `pos` was always a declared column of the hub relation
(`PositionalSource<V> { rel row(pos, val) }`). The vector family had only to say
what it can do. `edb_union` is still wanted for Canon's own reasoning; it was
never what stood between a positional container and a pushdown.

⚠ Worth keeping: a blocker that dissolves when the surrounding model changes was
not a blocker, it was a symptom. I had it recorded as the thing to build next.

⚠ Note the shape: a fact that "everyone knows" is exactly the fact nobody
writes down, and the planner then cannot use it.

## 4. Four near-identical walk producers — CLOSED (S4c)

`rows` / `from` / `upto` differ only in where they land and when they stop;
`at` is a probe. They are now ONE `…Walk` type per family with four
constructors — a landing and an `Option` bound — and the loop each of them used
to write out is the walk's `next`. Closed as a side effect of streaming, which
is the usual shape: the duplication was the `Vec` each producer built, not the
logic.

## 13. The access decision was SHAPE-SPECIFIC — CLOSED (S4e)

`plan_decide_access` matched `RQuery::Simple` and silently did nothing
otherwise. The same filter, over the same source, with the same declared
operation, therefore narrowed in a scan and was ignored in a join or an
aggregate — a rule written against the case it was first needed for, which is
the shape this whole audit keeps finding.

The query shape is now read in ONE place and contributes ONE thing: whether an
access that is exact for the demand may RETIRE the query's filter. A simple
scan's `where` belongs to its single source, so it may; a join's is checked per
joined row over variables from every side, so it narrows and keeps the filter.

⚠ The guard the widening required: in a join the `where` names columns from
every side, so the planner must verify the column belongs to THIS rel. Two
sources with a same-named column is the ordinary case, not the exotic one.

## 14. The join strategy was DECIDED WHERE IT WAS EMITTED — CLOSED (S4f)

`analyze_step` ran the equi-key selection and the capability cascade inside the
emitter, which runs AFTER the prelude has already materialized or streamed each
source. The consequence was not a missing feature but a false statement the plan
had to make about itself: every join step was marked "not read once", with the
reason "a strategy the plan does not own yet". Two of the three strategies read
their source exactly once.

A decision made where its effect is emitted cannot be consulted by anything that
runs earlier — the same shape as #13 and as `access_plan` itself. The rule now
lives in `logos.std.wql.join_sel`, the planner calls it and records the answer on
the step's IR node, and the emitter reads it.

⚠ The trap this ordering avoids: having BOTH sides call the same rule looks
equivalent and is not. They build their type environments separately, so a key
that types differently on the two sides would give the emitter a nested loop over
a source the plan had already turned into a consumed iterator — a join that
returns fewer rows and reports nothing. One evaluation, one answer, and the
emitter refuses outright to pair a streamed source with a rescanning strategy.

⚠ The other half of the widening is a fact about the EMITTED SHAPE, not about
the plan: a hash bucket over a streamed step must hold the ROWS, because the
index it used to hold pointed into a slice that no longer exists.

## 5. Two aggregate emitters — CLOSED (this pass)

`emit_aggregate` (scan) and `emit_aggregate_join` (join chain) were ~720 lines
each and shared 176 verbatim. They are now ONE `emit_aggregate`, and the merge
is not "hoist the shared middle": **the scan is the DEGENERATE chain.** The
emitter runs `collect_chain` on its input, and everything per-step — the
strategy trace, the build phase, the nest open, the nest close — is a loop over
`ch.n` that for a bare scan runs zero times. What is left standing is the
single-pass group fold, and that is the whole emitter. There is no scan branch
to keep in sync, because there is no scan branch.

⚠ The two were NOT equivalent, and a merge has to notice that rather than pick
a survivor at random. Three real divergences, the last two from one fast path:

* i64 `sum` accumulated through `el_add(…)?` in the scan shape (overflow ⇒
  `Err(ElError)`, pinned by `query_agg_sum_overflow_e2e`) and through a plain
  `+` in the join shape — which traps on the same input. The CHECKED add is
  what survives, so an aggregate over a joined stream now reports an
  overflowing sum instead of dying on it. This is a bug the duplication was
  hiding: the fix landed in one copy and the other never heard about it — the
  same shape as the `i56`/`u56` divergence in #6, and the reason this list
  exists.
* The scan shape had a no-modifier fast path that pushed the projection
  straight out of the group loop and never built the group columns. Gone: the
  merged shape always materializes the `__gf_*` columns and always runs the
  output phase. Same rows in the same order (columns are in group
  first-occurrence order; the identity permutation preserves it), more emitted
  lines, and one FEWER pass over the source — the scan shape rescanned every
  row once per group, the fold is a single pass with a linear group probe.
* …and that fast path leaked SCOPE, which is how the second divergence was
  found: because it projected inside the row loop, `select (e.key, n)` — the
  row var, not `key` — compiled there and NOWHERE else. Adding `order by` to
  the very same query moved the projection to the output phase and it died with
  `undefined variable 'e'` on GENERATED code. `deem_pushdown_all_shapes` was
  written against the one shape that allowed it.

  Resolved by making the capability real instead of deleting it: the fold now
  carries `__g_row` / `__gf_row`, the index of the row that CREATED each group,
  and re-binds the base row var (`let e: &Emp = &(es)[__gf_row.get(…)];`)
  wherever the group columns are bound. So the row var is nameable from
  `having`, `order by` and `select` alike, in the scan shape and the join shape,
  and it means what it always meant where it worked — the group's first row.
  ⚠ Only the BASE row var: a join STEP's var still has no representative,
  because the nest is what binds it.

`emit_group_binds` lost its `pfx` parameter along with the second prefix and
gained the row re-bind. Measured: −345 lines in `rexpr_walk.logos`, −162
`push_text` calls (1125 → 963). `wql_group_rowvar_e2e` pins the row var in
`having` / `order by` / `select` at both chain lengths.

What did NOT close at the time, and it was the same wall the rest of the body
conversion hit: a quote fragment could not declare a binding whose NAME the
emitter computes. `let_stmt` had ten alternatives and every one took a literal
`IDENT`, so `let #nm: i64 = 7i64;` inside `quote_item!` was `syntax error near
'let'`. This emitter writes `let __ga_<agg>`, `let __gf_<agg>` and `let <row
var>: &<row ty>` on every query, so it could not become fragments at all.

⚠ CLOSED SINCE. `KW_LET` (and `assign_stmt`) gained the `NAME_VAR` alternative
that `fn` / `param` / `struct` already had (`5e9488f3`), a statement LIST learned
to compose flat (`bdd9476c`), and the emitters' own open/close PAIRS became
wrapping fragments (`dbe92778`). `rexpr_walk.logos` now has **zero** `push_text`:
963 → 841 → 0. See ADR 0024 §S5 for the three primitives and why the "the body
must stay text" reasoning was wrong.

## 6. The type-classification table, written FOUR times — CLOSED (this pass)

`el_ty_of_name` (lenient) and `el_ret_class` (strict) carried the same table
with different unknown-name policies; `catalog_macro::classify_field_type` had a
third copy; `trama_render::is_primitive_ty` a fourth.

They had **diverged**: the catalog's copy knew `i56`/`u56` and the canonical one
did not. That is the concrete cost of duplication, and it is why this was worth
doing even though each copy "worked".

Now: one `el_class_lookup(name) -> class | -1`, with the policies as thin
wrappers, and every caller routed through it. Two corrections the shared table
cannot make are kept explicit at the call site rather than folded in:
`String` is not a by-value primitive (it owns its buffer), and `char` is a
scalar the lattice has no class for.

## 7. `el_ty_name` / `el_class_repr` — CLOSED (this pass)

A duplicate I introduced the same day, in `codegen` and `el`. Kept the one in
`el` (the lower module) and, more usefully, kept the HONEST name: after S3 the
"type name" of an expression is `infer_ty_name`; this function returns a class
REPRESENTATIVE, and calling it `el_ty_name` is what made losing column widths
look reasonable.

## 8. Keywords are rejected wherever an identifier is expected — CLOSED

Two confirmed instances: `WAny::null()` cannot be parsed (`null` is `KW_NULL`,
and the public fn at `anyval.logos:79` is therefore uncallable by anyone), and a
rel cannot be named `tagged` (`KW_TAGGED`, for `&tagged<TS>`). The diagnostic
points at the punctuation, not the name, which is why the second one read like a
multi-rel parser bug and cost a long bisection.

**FIXED** with the opt-in matcher `IDENT_ANY` — identifier OR word-like keyword
— a reserved matcher NAME in both peg_gen backends (like `RAW_GROUP_*`), not a
lexed token. Used only where a keyword is grammatically impossible: the method
name after `::`, and a rel's name. Nothing that relies on `IDENT` failing for a
keyword changes.

Worth noting what it replaces: the grammar already carried dedicated `KW_NEW` /
`KW_NULL` alternatives — 46 mentions — one per keyword per position. That is the
same under-generalization this audit is about, and it had been paid for by hand
each time a keyword collided with a name someone wanted.

## 9. A blanket trait's OUTPUT param is unresolved through a bound — CLOSED
##    (…and the diagnosis above it was wrong — see below)

Originally recorded as "an unsatisfied bound reaches MLIR-gen with no
diagnostic". Both halves were false, and the correction is the more useful
entry.

`fn f<V: Into<i64> + Copy>(x: V) -> i64 { return x.into(); }` fails for EVERY V,
including `u8` where `impl From<u8> for i64` exists — so the bound is satisfied
and this is not a bound-check gap. `Into` is blanket-derived
(`impl<S, T: From<S>> Into<T> for S`); `T` is an OUTPUT param, fixed by the
call's expected type. Mono instantiates blanket methods per concrete SELF, which
resolves `S` and leaves `T` open. Called directly (`let b: i64 = a.into()`) the
expected type fixes it; called through a generic fn whose BOUND fixes it, the
bound's trait args are not propagated into the blanket instantiation.

Checks already run, so they are not repeated: explicit turbofish, inferred bare
param, a param inside a compound type, a nonexistent trait in a bound, a
user-defined parametrized trait — all diagnosed correctly.

**FIXED.** The lazy call-site hook (`mono_clone.cpp`, G159-1) bound only the
blanket's SELF param. It now also binds the remaining ones by unifying the
template's return type against the call's, and those bindings are part of the
INSTANCE NAME — two bounds differing only in the output type are two functions,
and one name for both would have silently taken whichever cloned first. A clone
that cannot bind every param falls through to the unchanged path rather than
emitting a partial one. Test: `mono_blanket_output_param_via_bound`.

**Why the first diagnosis was wrong — CLOSED.** An mlir_gen verification failure
dumped the entire module to stderr: the cause printed on line 1, then ~12 000
lines of IR. In scrollback that is indistinguishable from a silent crash, and I
concluded the wrong thing from it with confidence. The module now goes to a
file and the failure reads in six lines, naming the symbol that broke
(`u8__into__g__S`) and the unresolved var. A buried diagnostic is not a
diagnostic.

## 11. The covering relation was a SEARCH ORDER — CLOSED (S4b)

`rel_find_op` answered "which operation serves this demand" by looking for the
demand's own comparison, then trying exactly two weakenings: `ge` for `gt`,
`le` for `lt`. Two things were folded into one function and neither was stated.

The first is the covering relation itself, and the two weakenings were not all
of it: `key >= k` returns every row `key == k` returns, so a source declaring
only a lower bound can answer an equality demand — it positions once and the
query's own filter cuts the tail. That case fell to a full container scan.

The second is that SEARCH ORDER was the choice. "First hit wins" is only right
while there is exactly one hit, and it silently means a source cannot improve
its plan by declaring MORE — a second covering operation is never seen.

Now the covering relation is `ap_covers`, written once, and choosing is a
comparison: every covering operation is enumerated and the narrowest
cardinality class wins, with exactness breaking a tie (an exact access also
retires the filter). Test: `deem_bound_covers_equality`.

⚠ The shape to notice: a predicate and a policy sharing one function, where the
policy is implicit in the control flow. Neither can be tested, and neither can
be extended without editing the other.

## 12. Every producer returned a `Vec` — PARTLY CLOSED (S4c)

"Materialize or stream" was nobody's decision. Every access producer returned a
`Vec` and the emitter looped it, so a query paid for a full materialization of
whatever the access admitted before looking at a single row — and for
`select … first` that is the entire cost of the query spent on rows nobody
reads.

The general form is the ITERATOR; a `Vec` is an eagerly-drained one. Never the
reverse — which is why draining is always available as the fallback and
streaming needs a proof.

The opt-in is the producer's RETURN TYPE, and deliberately not a keyword: a
declaration that says `stream` while returning a `Vec` is a claim nothing
checks, and the plan would be built on it. A return type cannot drift.

Canon's families stream too, and their four producers collapsed into one `…Walk`
type with four constructors differing only in landing and bound — which CLOSES
#4 below.

⚠ The obstacle on the way there is worth more than the feature. The failure
(`expected (u64, u64), got T`) read as a resolution problem and four separate
causes each looked decisive: the missing trait import, the inferred binding,
impl registration, and "trait-impl members index a round later than free
functions". All four were refuted, the last by putting an INHERENT `next` beside
the trait one and watching it fail identically.

It was the MATCH ARM: `Option::Some(r) => { r }` bound `r` at `Option`'s
declared parameter rather than the substituted row type. The emitted loop now
reads the option and unwraps it.

What sent four readings astray was a DIFFERENTIAL — the class spelling compiled
against the same generated impl, so "class works, projection doesn't" got
promoted into a causal story about rounds. A differential is evidence about
where, never about why. What settled it was changing the shape of the emitted
code itself.

⚠ Also open, and recorded rather than overlooked: sema checks trait MEMBERSHIP
only, not that the iterator's item type matches the relation's row type. A
source that returns `Iterator<WrongThing>` is diagnosed by the host compiler on
generated code — the exact diagnostic this ADR exists to abolish.

## 10. A `#[derive_hash]` type is not admissible as a rel column — CLOSED

...and the recorded diagnosis was wrong, which is the useful part.

It was written up as "the check waits for the item-macro drain, but
annotation-triggered emission is not covered by that signal". The check does
read `if (!metaprog_pending_pkgs_.empty()) return;` and its comment does say a
derive synthesizes `impl Hash` later, so waiting is what it means to do.

It never waits. Pending-ness is discovered during LOWERING, one phase AFTER this
check runs during collect, so in the round that matters the set is always empty.
The check ran ONCE, judged a type whose impl did not exist yet, and errored —
which ended the compile before the round that would have produced the answer.
"Nothing is pending" and "nothing has been examined yet" were the same value.

⚠ Two hypotheses died before that one: widening the pending signal to count
annotations changed nothing, and neither did adding an item macro to the same
module. What settled it was one `fprintf` inside the check, printing the number
of rounds and what was registered — direct observation of the failing construct,
after two rounds of reasoning about its context.

The signal that IS available at collect time is the staged handler-target list,
built earlier in the same pass. A type annotated with a registered handler is
one whose capability is being SYNTHESIZED, which is not the same as lacking it.
Tests: `deem_rel_col_derived` (admitted) and `deem_rel_col_hashable_fail` (a type
with no derive and no impl is still refused — the half that could have silently
degraded).

`check_rel_column_types` waits for the item-macro drain
(`metaprog_pending_pkgs_`), but annotation-triggered emission is not covered by
that signal, so the derived `impl Hash` does not exist when the check looks. A
hand-written impl is admitted. This is the extension path the universal query
compiler is built on (§S6), so it matters more than its size suggests.

## 15. NO TEST OVER THE CONTAINER PATHS COULD TELL A PUSHDOWN FROM A FULL SCAN — CLOSED (2026-08-10)

Items 1–3 are closed, and closing them created a defect of the shape this audit
keeps finding — one level up, in the TESTS.

Re-measuring the standing claim ("`__ctr_rows_X` is a loop `seek(i)` over the
whole `size()`, so the filter never reaches storage") found it false at both
emission sites: a walk, three narrowing constructors, a declaration, and one
descent through `bt_cur_seek_key`. What no measurement could find was a test OVER THESE
PATHS that would notice if that stopped being true.

⚠ **THE FIRST WORDING OF THIS ITEM WAS AN OVERSTATEMENT AND IS WITHDRAWN.** It
said no test anywhere could tell the two apart. `tests/logos/pass/deem_pushdown_all_shapes.logos`
already could, and had been able to for as long as it existed: it counts pulls
through a `static mut PULLED` and asserts a floor over 200 rows, covering the
join and aggregate arms. MEASURED by the verifier — under the same Control C that
deletes the JOIN base's narrowing, it PREDICTED and MEASURED **exit 7**. The
narrow claim (the CONTAINER paths had no such sensor) survives; the broad one did
not, and the technique it presents as new — count rows, never duration — already
existed in this tree, uncited.

**Because a plan that fails to narrow keeps the query's own filter and returns
exactly the same rows.** Narrowing is an optimisation, so it is answer-invariant
BY CONSTRUCTION — and every fixture over these paths asserted the answer:
`conuco/memoria/tests/ctr_plan_pushdown.logos` and `ctr_vec_pos_pushdown.logos`
compare against a reference walked through the container's own cursor;
`deem_hashmap_source.logos`, `deem_source_size.logos`,
`deem_three_domain_join.logos` and `deem_cross_domain_join.logos` carry the
expected `[plan]` line in a COMMENT. One of those comments had been wrong for
months (`scan on key` — a scan records no column, because there is no chosen
operation to be on one). Every one of them would have stayed green with the whole
access-path plane reduced to `__ctr_rows_`.

⚠ **THE GENERAL SHAPE: an optimisation cannot be pinned by its result.** The
only witnesses are what was DECIDED and what was EMITTED, and both had to be
asserted, because either alone can lie — a plan that recorded a narrowing while
the emitter called the full-scan producer passes the first half.
`tests/logos/ctr_access_path_gate.sh` asserts the plan trace under
`LOGOS_TRACE_PLAN=1` AND the `--gen-dir` artifact: the producer, the bound
literal, the ABSENCE of the full-scan producer for the narrowed rel, the retired
filter (the bound occurs exactly once, inside the call) and — the other
direction — the KEPT filter of a rel that declared no covering operation.

⚠ And it cannot be a timing test. The box is shared; the property asserted is
the SHAPE of the demand — which producer, which bound, from which position —
which is a fact about the artifact rather than about the machine. That is a
constraint worth generalizing: "how many rows was the container asked for" is
checkable everywhere "how long did it take" is not.

Neutrality (the S6 acceptance criterion) is asserted in the same gate rather
than asserted about: one line grammar must match a generated ordered-map family,
a generated positional family, and the hand-written `impl MapSource` in
`stdlib/mem/collections/hashmap/hashmap.logos`. If the planner ever learned to
treat a factory declaration differently from a written one, the count of matches
would move.

---

## What this list is for

Items 1–3 are one arc: a source should DECLARE its columns and its operations,
and the planner should match a demand against that declaration. Everything else
in the query stack that is "written per case" is downstream of not having that.

Items 6 and 7 are done and are the cheap kind: one idea, N copies, merge. The
audit's value is in noticing that #1–#3 are the same failure at a different
scale — the map's capabilities were written for the map, and there was no
mechanism that would have made writing them for the vector automatic.
