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

`container_item.logos` (`__deem_bind`) chooses the family's narrowing producer
with a three-way `if` over a strategy string, concatenating one of three fixed
prefixes:

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

The ordered-map family publishes `__ctr_rows_` / `__ctr_at_` / `__ctr_from_` /
`__ctr_upto_`. The vector family publishes `__ctr_rows_` and nothing else, so
`from s r where r.pos > 100` scans the whole container — even though `seek` is
one descent and `skip` is an index bump (both landed in `1f2dabe1`).

Blocked by #3: the planner narrows on a column, and the position is not a column
Canon knows about.

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

## 4. Four near-identical walk producers — OPEN (low)

`rows` / `from` / `upto` differ only in where they land and when they stop;
`at` is a probe. One emitter parameterized by (landing, bound) would replace
four quote blocks — and would make #2 nearly free, since the vector's producers
are the same shape with `seek(pos)` as the landing.

## 5. Two aggregate emitters — OPEN

`emit_aggregate` (scan) and `emit_aggregate_join` (join chain) are ~720 lines
each and share 176 lines verbatim. The duplication is real but the two are not
a mechanical copy; splitting the shared middle out is a refactor with its own
risk budget, listed here so it is not rediscovered.

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

## 10. A `#[derive_hash]` type is not admissible as a rel column — OPEN

`check_rel_column_types` waits for the item-macro drain
(`metaprog_pending_pkgs_`), but annotation-triggered emission is not covered by
that signal, so the derived `impl Hash` does not exist when the check looks. A
hand-written impl is admitted. This is the extension path the universal query
compiler is built on (§S6), so it matters more than its size suggests.

---

## What this list is for

Items 1–3 are one arc: a source should DECLARE its columns and its operations,
and the planner should match a demand against that declaration. Everything else
in the query stack that is "written per case" is downstream of not having that.

Items 6 and 7 are done and are the cheap kind: one idea, N copies, merge. The
audit's value is in noticing that #1–#3 are the same failure at a different
scale — the map's capabilities were written for the map, and there was no
mechanism that would have made writing them for the vector automatic.
