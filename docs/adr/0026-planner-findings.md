# ADR 0026 — what the planner leaves on the table: a findings ledger

Status: OPEN (a LEDGER, not a design — it collects measured defects in Deem's
optimizer and emitter, and closes a row only when a fix lands with a test).
Scope: `plan_decide_access` / `plan_narrow_rel` in `stdlib/mem/wql/plan_walker.logos`,
the access-path and materialization choices they drive, and the code
`rexpr_walk` emits from them. Sources are the declared kind of ADR 0024 §6 —
generated container families, `mem` collections, slices — so a row here is about
the planner, not about Memoria.

## Why a ledger and not a design

Every row below was found the same way: by writing a query, dumping what the
compiler generated for it (`--gen-dir`), and reading the plan's own account of
why (`LOGOS_TRACE_PLAN=1`). Not one of them is visible from the query's ANSWER —
every query in `examples/deem_memoria_showcase.logos` returns exactly the right
rows, and did so before any of this was noticed. They are visible only in the
emitted code and in what it costs.

That is the reason for the file. A defect that changes no answer has no natural
moment of discovery: it does not fail a test, it does not fail a gate, and the
next reader of the query has no reason to look. It gets found when somebody
reads generated code on purpose, and then it needs somewhere to go.

**HOW TO REPRODUCE EVERY ROW.** The example is the fixture:

```
cmake --build build -j12 --target example_deem_memoria_showcase
LOGOS_LIB_DIR=$PWD/build/lib/logos ./build/bin/logosc \
    examples/deem_memoria_showcase.logos -o /dev/null --gen-dir /tmp/gen \
    -l build/tests/logos/libmemoria-{ctr,store,testkit}.a
LOGOS_TRACE_PLAN=1 ... # the same invocation, for the planner's own account
```

`/tmp/gen/deem_memoria_showcase.<query>_{prepare,run,stream}.*.gen.logos` is the
generated query; `logos.gen.__ctr_size_*.gen.logos` holds the family's
`impl OrderedMapSource`, which is the planner's INPUT.

---

## F1 — a conjunctive `where` is not decomposed, and the pushdown is lost

**Severity: the highest here.** It is the only row that changes asymptotics.

Two spellings of the same restriction over the same container:

```logos
from m e where e.key >= 400 select (e.key, e.val)
    → [plan] m -> __ctr_bfrom_Hs… [a range] on key
      "an operation EXACT for that comparison — the residual filter is
       redundant and was dropped"

from m e where e.key >= 400 && e.val < 1500 select (e.key, e.val)
    → [plan] m -> scan [every row]
      "neither side of the filter is a column"
```

The generated bodies differ in the producer and in one `if`:

```logos
// pushed down: the walk lands at 400, no predicate survives
let mut __rel_m: …LeafWalk = __ctr_bfrom_Hs…(m, 400u64);
…
__out.push(((e).0, (e).1));

// not pushed down: full walk, both conjuncts per row
let mut __rel_m: …LeafWalk = __ctr_brows_Hs…(m);
…
if (((((e).0 >= 400u64)) && (((e).1 < 1500u64)))) { __out.push(…); }
```

100 rows read against 500, for the same answer.

**THE CAUSE, exactly.** `plan_narrow_rel`
(`stdlib/mem/wql/plan_walker.logos`, the `SExpr::Bin` arm) reads the TOP node of
the `where` and asks whether either side is a column:

```logos
SExpr::Bin(b) => {
    let mut col: str = pw_field_col(b.lhs_expr());
    let mut col_left: bool = col.len() > 0i64;
    if !col_left { col = pw_field_col(b.rhs_expr()); }
    if col.len() == 0i64 {
        ap.scan(ri, "neither side of the filter is a column");
        return;
    }
```

For `a && b` the top node is also a `Bin`, whose two sides are COMPARISONS
rather than columns — so both `pw_field_col` calls come back empty and the
function exits to `scan`. There is no conjunct split, no candidate list and no
choice between candidates. What is lost is not "the best of several pushdowns"
but the only one available: the conjunct is in the tree, the declaration
(`op entry.key ge = __ctr_bfrom_… exact`) is on the source, and the producer is
generated.

**THE FIX, in three parts, all in `plan_narrow_rel`:**

1. recognise `&&` at the top and flatten it into a conjunct list (`a && b && c`
   is a right-associated tree, so this is a walk, not a pair);
2. run the existing single-comparison logic per conjunct and take one for which
   the source declares an operation — with several candidates, prefer `eq` to a
   range, which is a choice question and not this row's;
3. **do not retire the residual filter when there is more than one conjunct.**

Part 3 is the cheap half and the load-bearing one. Today `may_drop` retires the
whole filter on the ground that the operation is EXACT for the comparison — true
for a single conjunct. With several, only the CHOSEN conjunct is subsumed and
the others must still run. The safe first landing is to retire nothing: narrow
the walk and keep the whole `&&` in the row loop. The answer is unchanged, the
access path is right, and the price is one redundant re-check of a condition the
walk already guarantees.

**⚠ THIS IS NOT A MEMORIA ROW.** Every source that declares operations is
affected: `BTreeMap` and `HashMap` declare `op entry.key eq` and lose the same
pushdown on any `&&`.

**WHY IT WAS NEVER SEEN.** Measured over the corpus: the queries that carry a
conjunctive `where` (`wql_showcase_e2e`, `wql_typed_params_e2e`,
`token_macro_item_resource`) all range over SLICES, which declare no operations
and would scan regardless. No corpus query combines a conjunctive `where` with a
source that declares any operation. The coverage hole and the defect are the
same shape, which is why the fix needs a corpus test in the same commit and not
only a plan-trace assertion.

---

## F2 — the measured size is a runtime call nobody reads

Every generated `<query>_run` opens with a size read:

```logos
let __sz_m: u64 = __ctr_size_Hs352959f3caf5b795(m);
```

and the trace justifies it as a measurement:

```
[plan] m -> size on __ctr_size_… -> __sz_m
   (measured: the source reports its size, read once before the first row)
```

Measured across all four queries of the example — `key_from`, `mid_band`,
`per_bucket` and the join `enrich` — the name occurs exactly ONCE in each
generated body: at its own definition. Nothing reads it. In `mid_band` the cost
is paid twice over, because the scan producer `__ctr_brows_` calls `c.size()`
again for its own bound.

A size read is cheap for a b+tree (the root carries it) and not free for every
source. The row is not "delete it" — it is that a measurement with no consumer
is either a dead emission or a plan step that stopped consulting its own input,
and the trace currently asserts the second while the code shows the first.

---

## F3 — the prepared plan is passed and never read

`<query>_prepare` builds a plan struct of constants and `<query>_run` takes it:

```logos
pub fn mid_band_prepare(m: &Hs…) -> MidBandPlan {
    return MidBandPlan { dyn_order: false, defer_order: false, swap: false,
                         order_ix: 0i64, base_n: -1i64, step_n: -1i64,
                         n2: -1i64, n3: -1i64 };
}
pub fn mid_band_run(__pl: &MidBandPlan, m: &Hs…) -> Result<…> { … }
```

`__pl` does not appear in the body of any of the four generated `_run`
functions — including the JOIN's, where `swap` and `dyn_order` exist precisely
to be read. For the join the trace explains why: the query names no `order by`,
so the order axis was entered and refused, and there is nothing to choose. That
is a correct verdict for THIS query and not a general one.

The row records the shape, not a bug: a prepare/run split whose runtime half
consults nothing is indistinguishable from a compile-time decision that has been
copied into a struct. What decides whether it is one or the other is a query
that DOES need a runtime choice — a join with `order by` — and none exists in
the corpus or the example. Write it, then this row either closes or becomes F1's
neighbour.

---

## F4 — a point get is served by the scan protocol

`where e.key == 400` differs from `where e.key >= 400` by one token in the
generated body:

```logos
- let mut __rel_m: …LeafWalk = __ctr_bfrom_Hs…(m, 400u64);
+ let mut __rel_m: …LeafWalk = __ctr_bat_Hs…(m, 400u64);
```

The batch loop, the `Buffer`, the tuple materialisation are identical. The
narrowing is real and lives in the producer, which lands a walk bounded to one
row:

```logos
let cu = c.seek_key(k);
let st: u64 = cu.pos();
let mut e: u64 = st;
if cu.valid() { if (cu.key() == k) { e = (st + 1u64); } }
return …LeafWalk { at: st, base: st, endr: e, n0: (e - st), hi: Option::Some(k), … };
```

A miss is an empty window rather than a special case, which is good. What the
row records is that there is no point-get FORM: `==` pays the batch protocol
(`next_batch` → `len` → an index loop over `key_at`/`val_at`) to deliver one
row, plus F2's size read of the whole tree. Whether that costs enough to matter
is unmeasured — this row is an observation with a number attached to it, and it
should not be "fixed" before somebody measures it.

---

## F5 — the streaming form is generated and not used

Every query emits a `<query>_stream` alongside `_run`, and the entry point calls
`_run`. The trace names the reason:

```
[plan] mid_band -> query output on (u64, u64)
   (the direct form is not landed — its state struct needs an emitted walk type
    as a FIELD and sema_abi_layout declines a metaprog-emitted struct field from
    a foreign package (R-E), so this landing is buffered)
```

So every query materialises into a `Buffer` and returns a `Vec`, including
queries whose source is an iterator the plan reads once — which the same trace
reports one line earlier as `streamed` and `no materialization`. The two
statements are about different things (the SOURCE is streamed into a buffered
OUTPUT), and reading them together is confusing enough to be worth saying so
here.

This is not a planner defect: it is a layout-plane limitation with a named
cause, which makes it the one row here with a known owner outside this ADR.

---

## F6 — the stdlib's ordered sources do not declare their order

The generated families declare which column their rows arrive sorted by:

```logos
impl OrderedMapSource<u64, u64> for Hs… {
    …
    order entry = key;
}
```

`BTreeMap` — the stdlib's ordered map, whose whole point is that it is
ordered — declares `rel`, three `op`s and a `size`, and no `order`:

```logos
impl<K: Ord + Copy, V: Copy> OrderedMapSource<K, V> for BTreeMap<K, V> {
    rel entry = btree_rows;
    op entry.key eq = btree_at    exact;
    op entry.key ge = btree_from  exact;
    op entry.key le = btree_upto  exact;
    size entry = btree_len;
}
```

`order <rel> = <col>` is what lets the planner drop a sort node
(`access_plan.logos`: "no sort node: the query orders by the column this source
declares its rows already arrive sorted by"), so an `order by key` over a
BTreeMap sorts rows that are already sorted. The declaration is missing rather
than false, so nothing is wrong today — it is a capability the source has and
does not state.

⚠ The producer's return type is half of the fact: the ordered form requires a
producer whose type carries `OrderedBy<K>`, and `btree_rows` returns a plain
`Vec<(K, V)>`. So this row is not a one-line addition — the stdlib's producers
are materialising ones and the generated families' are typed streams, and F6 is
where that difference stops being invisible.

**No corpus query orders by key over a BTreeMap**, so nothing measures either
half of this today.

---

## F7 — a base-only predicate is evaluated per MATCHED PAIR, not per base row

The join's `where e.val > 900` names a column of the BASE row and nothing else.
The emitted probe puts it in the innermost loop:

```logos
while (__p1 < __bv1.len()) {                    // once per MATCH
    let __ri1: i64 = __bv1.get(__p1);
    let q: &(u64, u64) = (&((__rel_b_sl))[__ri1]);
    if ((((e).1 > 900u64))) {                   // depends only on `e`, the base row
        __out.push(((e).0, (e).1, (q).1));
    }
    __p1 = (__p1 + 1i64);
}
```

Two costs, and the second is the larger one:

* the predicate is re-evaluated once per matching row. The showcase's join is
  1:1 so this is invisible; at 1:N it is N evaluations of a condition whose
  inputs did not change;
* it runs AFTER the probe. A base row that fails the predicate has already paid
  a hash lookup and a `Vec` walk. Hoisting the base-only conjuncts above
  `__mp1.get(…)` skips both for every rejected row.

The plan already knows which side each column belongs to — `plan_narrow_rel`
refuses to narrow on a column that belongs to another rel, so the ownership test
exists. What is missing is using it to PLACE the predicate rather than only to
reject a narrowing.

Note the interaction with F1: the join's `where` is a single conjunct here. With
`&&` it would not be split, so a mixed predicate (`e.val > 900 && q.val < X`)
would neither push down nor hoist — the two rows compound.

---

## F8 — the build side is indexed as a multimap even where the source declares a map

The hash build maps a key to a VECTOR of row indices:

```logos
let mut __hm1: HashMap<u64, Vec<i64>> = hashmap_new::<u64, Vec<i64>>();
…
let __vp1: *mut Vec<i64> = __mp1.get_or_insert(__k1, Vec::<i64>::new());
(&mut (*__vp1)).push(__b1);
```

and the probe walks that vector. For a build side whose keys are UNIQUE every
vector has length one, so the cost is one heap allocation per distinct key (250
in the showcase), one indirection per probe, and a loop that always runs once.

Uniqueness is not a guess here. `OrderedMapSource` is documented as "one row per
entry, in key order", and `BTreeMap`'s keys are unique by construction — the
source's own declaration entails it. The plane has no way to SAY it: there is no
`unique entry.key`-shaped declaration beside `rel`, `op`, `size` and `order`, so
the emitter cannot specialise `Vec<i64>` to `i64` and drop the inner loop.

This is the same shape as F6 — a capability the source has and cannot state —
and the two should probably be answered together, since both add a fact to the
declaration vocabulary rather than a rule to the planner.

---

## F9 — there is no merge join, and the strategy is chosen by key capability alone

The strategy set is `JS_NONE / JS_HASH / JS_TREE / JS_LOOP` (`join_sel.logos`),
and the cascade picks:

```logos
let caps: KeyCaps = join_key_caps_named(k.ktn);
if caps.hash && !join_force_tree() {
    return StepSel { strat: JS_HASH(), …,
                     why: "the key type is hashable — build once, probe per row" };
```

The ground is a property of the KEY TYPE. Whether the two sources are ordered by
the join key is not consulted, and a merge is not in the set to be consulted
about.

In the showcase both sides ARE ordered by the join key: the generated family
declares `order entry = key`, and the BTreeMap is ordered in fact (F6 — it does
not say so). A merge would need no `btree_rows` materialisation, no hash table,
no 250 `Vec` allocations and no probe indirection — two synchronised walks.

**AND THIS IS WHERE F2 AND F3 MEET.** Choosing a build side is exactly the
decision `__sz_c` and `__sz_b` are the inputs to, and choosing between a merge
and a hash is what `EnrichPlan.swap` / `base_n` / `step_n` are shaped to carry.
The sizes are computed and discarded; the plan is passed and unread; the side is
fixed by the order the query names its sources in. Here that happens to be right
(250 < 500) and it is not measured. Three rows, one missing decision.

---

## F10 — the group frame probes LINEARLY, in the file that owns the hash test

An aggregate's fold finds a row's group by scanning every group seen so far:

```logos
let __k: u64 = ((el_divu(((e).0), (((100u64) as u64))))?);
let mut __gi: i64 = (-1i64);
let mut __s: i64 = 0i64;
while ((__s < __g_key.len()) && (__gi < 0i64)) {
    if (__g_key.get(__s) == __k) { __gi = __s; }
    __s = (__s + 1i64);
}
```

It exits on a hit, so the cost is O(rows x groups) and not O(rows x groups)
always — but the shape is unchanged: 500 x 5 in the showcase is nothing, 500
groups is 250 000 comparisons, 5 000 groups is 2.5 million. The trace says so
itself, which is why this row is about a CHOICE and not about a hidden cost:

```
[plan] m -> group frame on group table: one row per distinct key
   (the fold finds a row's group by a linear `==` scan over `__g_key: Vec<u64>`
    (no index is built — this is the group frame's dedup, not an Arrange) …)
```

WHAT MAKES IT A DEFECT RATHER THAN A TRADE-OFF: the same compiler builds a
`HashMap` for the same lookup — key to slot — on the join path (F8), and the
capability test that licenses it, `join_key_caps_named`, lives in the SAME FILE
as the group frame's own trace emitter (`join_sel.logos`). For `u64` it answers
yes. One compiler, two answers to one question, and the linear one sits where
the data is larger: a join's build side is one source, an aggregate's group
count grows with the query's own key expression.

The fix is not "always hash": a handful of groups is genuinely faster linear, and
the group count is not known before the fold. What is missing is the decision —
and note that this is F2 again from the other end, because the source's declared
`size` is the one number that bounds the group count before the first row.

---

## F11 — the aggregate's output phase materialises an IDENTITY permutation

After the fold, the output phase builds an index vector and reads the group
frame through it:

```logos
let mut __ix0: Vec<i64> = Vec::<i64>::new();
let mut __r: i64 = 0i64;
while (__r < __g_key.len()) { __ix0.push(__r); __r = (__r + 1i64); }
…
let key:   u64 = __g_key.get(__ix0.get(__o));
let n:     i64 = __ga_n.get(__ix0.get(__o));
let total: u64 = __ga_total.get(__ix0.get(__o));
```

`__ix0` is `[0, 1, …, n-1]`. The vector exists because the general shape supports
an `order by` over groups, where the permutation would be the sort — and this
query names none, so it is the identity: one allocation, plus a double
indirection on every column of every output row, for a reordering that reorders
nothing.

Same class as F2 and F3 — machinery emitted for a case the query does not
have — and the cheapest of the three to answer, because "no `order by`" is known
where the permutation is emitted.

---

## F12 — a checked division by a literal is emitted with its check

The group key is computed per row through the EL's checked division:

```logos
let __k: u64 = ((el_divu(((e).0), (((100u64) as u64))))?);
```

```logos
pub fn el_divu(a: u64, b: u64) -> Result<u64, ElError> {
    if b == 0u64 { return Result::Err(ElError::DivByZero); }
    match a.checked_div(b) { … }
}
```

The divisor is the literal `100`. The zero test and the `Result` plumbing with
its `?` propagation cannot fire, and run once per row anyway. Contrast the
accumulator's `el_addu(…)?` in the same loop, whose overflow check is
load-bearing and must stay: the row is not "the EL should stop checking", it is
that a divisor known non-zero at compile time makes the check decidable there.

Scope note: this is EL lowering rather than planning, and it is in this ledger
because it was found the same way and lives in the same emitted body. It is the
one row here that a constant-folding pass closes without any plan-level decision.

---

## What closes a row

A row closes when a fix lands together with a test that fails without it. For
F1 that test must use a source that DECLARES operations — the existing
conjunctive-`where` queries all range over slices and would pass either way,
which is exactly how F1 survived. `examples/deem_memoria_showcase.logos` holds
the shape and asserts that both spellings return the same rows in the same
order, so it pins the answer; it does not pin the PLAN, and a closing commit
should add that assertion where the corpus can see it.
