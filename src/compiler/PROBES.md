# Probe measurements

⚠ **This file exists so that recording a measurement does not cost a rebuild.**
`scripts/probe-batch.sh` builds *before* it prices, so a round that writes its
numbers into a `.cpp` comment must rebuild to commit them — measured on
2026-08-29, that is one extra ~150 s build plus a re-run of L1, the ledger and
`-L bc` under a new build identity, for no change in behaviour. This file is not
compiled. It sits beside the sources and is versioned with them.

⚠ **The link to the code is a SYMBOL, never a line number.** A line number in a
record about another file goes stale silently and a stale number is read as
current. `scripts/probe-log-lint.py` checks every `site:` here against the named
file and reds if the symbol is gone — so a rename breaks the record loudly
instead of leaving it pointing at nothing.

⚠ **A record here is a MEASUREMENT, not a verdict about the tree.** Each carries
the build it was taken under, because a ceiling decays as the ledger shrinks and
a cost grows as the corpus widens: both numbers are about a population that
moves. Re-price before funding anything measured more than a round ago.

## Format

    ## <probe-name>
    site: <path>::<symbol>
    build: <build-hash>            what `scripts/build_hash.py` said
    measured: <YYYY-MM-DD>
    fires: <n>                     arrivals; 0 means NEVER FIRED, which is not a zero
    ceiling: <n>                   ledger rows a crude edit closes — an UPPER bound
    cost: <n>                      legal programs it refuses — a LOWER bound
    verdict: <one line>
    note: <free text, any length>

---

## selftest_refuse
site: src/compiler/borrow_check.cpp::record_borrow
build: 3aeaa1737dd22dd3
measured: 2026-08-29
fires: 364946
ceiling: 365
cost: 1033
verdict: the harness's known answer — it must close everything
note: refuses every borrow the pass records. A reader that has never SEEN a row
  close cannot tell a dead hypothesis from a broken reader, and on its first run
  this one WAS broken. `ceiling-probe.sh --selftest` asserts it closes the whole
  ledger; if that number is ever small, the READER is what broke, not the tree.

## selftest_inert
site: src/compiler/borrow_check.cpp::record_borrow
build: 3aeaa1737dd22dd3
measured: 2026-08-28
fires: 1599734
ceiling: 0
cost: 0
verdict: the null pole — it must change nothing
note: proves the reader does not invent changes. Fires on every recorded borrow
  and moves no verdict in either direction.

## recvresvamut
site: src/compiler/borrow_check.cpp::check_recv_conflict
build: 3aeaa1737dd22dd3
measured: 2026-08-29
fires: —
ceiling: 5
cost: 11
verdict: STOP — and the decline was for the WRONG REASON
note: recorded as a stop sign for days. Compiling each cost fixture by hand
  showed all nine are ONE diagnostic — "cannot borrow 'X' as mutable: not
  declared as mut" — i.e. `take_borrow`'s binding-mut check, not the reservation.
  All nine are rc 0 under the narrow `recvamutraw`, which landed at 3/0. A
  mechanism buried as too expensive was buried for a reason that was not its own.

## rootkeep
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: (pre-store; measured against the 447-row ledger)
measured: 2026-08-27
fires: 427
ceiling: 0
cost: 0
verdict: REFUTED at the IndexRead site; NOT MEASURED at the SliceIndex twin
note: ⚠ ONE NAME GUARDED TWO BAILS. The coverage map counts 21,299 arrivals at
  the IndexRead spelling and ZERO at the SliceIndex one, so the 427 fires
  measured the first and said nothing about the second. The fire counter cannot
  catch this by construction — it aggregates by NAME, so two sites are one
  number. Only a per-region count separates them.

## genrecvtie
site: src/compiler/borrow_check.cpp::take_ref_borrows
build: (pre-store)
measured: 2026-08-27
fires: 1
ceiling: 0
cost: 0
verdict: UNPRICEABLE by the ledger — not refuted
note: fired ONCE in 423 ledger compiles and 14,075 times over the pass corpus,
  where it still changed nothing. An insurance probe on the same lookup without
  the pruning guard fired 176,555, which proved the site hot and named the
  pruner. The defect's population is the stdlib and pass corpus, not the ledger:
  proven-live is necessary and not sufficient.

## fldrootbits
site: src/compiler/borrow_check.cpp::field_borrow_conflicts
build: armed gate build 22 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 5354921
ceiling: 1
cost: 0
verdict: ✓ the PATH-keyed reader never asks the ROOT bits — predicted set closed EXACTLY
note: the 2026-08-28 clang enumeration read 76 accesses in the OTHER direction (a
  ROOT reader while a path is held) and classified all 76 correct: 0 defects.
  This is the inverse, and it is a defect. `field_borrow_conflicts` is the ONLY
  path-keyed conflict reader in the file — 30,490,642 arrivals in the 8060-run
  coverage map — and it reads `mut_field_borrows` and `shared_field_borrows`
  and nothing else. So `let b = &a;` (which sets `shared_borrows` on the ROOT,
  not a path) followed by `let z = a.i;` (a field move, checked only through
  this reader) is admitted. PREDICTED borrowck-move-from-subpath-of-borrowed-
  path; CLOSED exactly that, both directions empty. COST 0 over the ledger's
  legal halves — and rule 5 applies: no counter-example was hand-written.
  ⚠ CEILING 1 off 5.35M fires is not a big prize, but it is a one-site
  delegation at the single reader every path-keyed question already goes
  through, and the callers inherit it for free.

## recvfieldpath
site: src/compiler/borrow_check.cpp::check_recv_conflict
build: armed gate build 23 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 85
ceiling: 1
cost: 0
verdict: ✓ the ROOT-keyed gate BAILS on a projection — predicted set closed EXACTLY
note: `if (bp.root.empty() || !bp.path.empty()) return;` is the first line of the
  method-receiver conflict gate: a receiver reached through a FIELD is not
  checked at all. ISOLATED ON ONE VARIABLE, by hand, before the probe:
    let mut v: Vec<i64>; let e = &v[0];   v.push(1);    → REFUSED (2 diagnostics)
    let mut t: Thing;    let e = &t.v[0]; t.v.push(1);  → ADMITTED
    let mut t: Thing;    let e = &t.v;    t.v.push(1);  → ADMITTED
  The only difference is one field hop. The probe routes the non-empty-path case
  to `field_borrow_conflicts` — the reader that already answers exactly this —
  instead of returning. PREDICTED issue-82032; CLOSED issue-82032, both
  directions empty.
  ⚠ RULE 4: 85 fires over the ledger. A small population, so the ceiling is a
  weak bound in both directions.
  ⚠ `refwhole` (below) closes the SAME row by collapsing every ref-rooted place
  to the whole root, at COST 600. Same row, two spellings, 600x the price.

## dwatunwrap
site: src/compiler/borrow_check.cpp::check_place_mut_use
build: armed gate build 24 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 467
ceiling: 0
cost: 0
verdict: ⛔ REFUTED for the two rows it was aimed at — the AddrOfTemp hop is not what holds them open
note: the DerefWrite arm hands `check_place_mut_use` a place computed from
  `v.ptr()`, and for the `s.f = v` spelling that is an `AddrOfTemp`, on which
  `extract_borrow_place` breaks — empty root, null `through_ref_type`. So the
  landed E0594 "behind a `&` reference" rule (which closed 4 rows for the
  `*h.r = v` spelling) looked structurally unable to run for a plain field
  write. Unwrapping one hop closes NOTHING.
  PREDICTED issue-85765 (`let rofl: &V = &mut test; rofl.n += 1;`) and
  issue-93093 (`fn bar(self: &S) { self.foo += 1; }`). Both stayed open.
  ⚠ THE FIRE COUNT IS COARSER THAN THE SITE. `probe::on` sits FIRST in the `&&`
  chain, so 467 counts every arrival at that call, not the AddrOfTemp subset —
  the site is proven live, the SUB-population is not. Whoever re-opens this
  must move the `probe::on` after the `kind()` test.
  The residual question is upstream: for these two rows the walk either never
  calls `cross()` on the `&`-typed root, or the compound `+=` spelling takes a
  door that is not DerefWrite at all. Not measured here.

## recvpartial
site: src/compiler/borrow_check.cpp::method_self_kind
build: armed gate build 25 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 10017
ceiling: 2
cost: 0
verdict: ✓ a METHOD-CALL RECEIVER is never asked about partial moves — and COST 0 has a counter-example behind it
note: `consume()` reads `moved_fields` and refuses "use of partially moved
  value"; `check_live` does not, and the receiver position reaches only
  `check_live`. HAND-WRITTEN, ONE VARIABLE, BEFORE THE PROBE — all three
  programs partially move `line2.origin` and then use the whole value:
    eat(line2);        → REFUSED   "use of partially moved value 'line2'"
    let _c = line2;    → REFUSED   same
    line2.consume();   → ADMITTED  (by-value `self`, rc 0, no diagnostic)
  One token apart. This is why COST 0 here is worth more than the other zeros
  in this file: the shape was found by construction, not by corpus silence.
  PREDICTED borrowck-uninit-field-access. CLOSED that AND
  moves/move-deref-coercion — an `nllmoves.B` row nobody nominated, and it is
  the same observation in a second ledger block. Predicted∖closed = ∅.
  ⚠ `method_self_kind` returns 0 for FOUR different facts — by-value `self`,
  unresolved, ambiguous, no params. The probe deliberately does not branch on
  it (it checks the receiver at every spelling), which is why the cost stayed
  0; a fix that keys on `sk == 0` would inherit that overload. The probe body
  sits in visit()'s MethodCall arm beside the `method_self_kind` call.

  ── LANDED 2026-08-29. Ledger 365 -> 363; the probe is gone from the tree and
  the rule stands in its place.
  CLOSED SET = {borrowck-uninit-field-access, move-deref-coercion}, i.e. the
  ceiling was REACHED. Predicted-by-name before the build; predicted∖closed = ∅
  and closed∖predicted = ∅. COST measured again on the landed rule: ledger
  363/363, `-L bc` 1794 passed / 0 failed / 2 disabled, and the
  `25_spec|03_ownership|04_advanced` pass selection 190/190. Zero.
  ⚠ RULE 7 — THE CORRECT FIX IS NARROWER THAN THE PROBE. The probe asked at
  EVERY receiver path; the landed rule asks only where the path is EMPTY.
  Measured by hand: `o.i.look()` after `let _x = o.i.a;` already refuses with
  "use of moved field 'o.i.a'" from visit()'s FieldRead arm, so the probe's
  non-empty-path branch bought a SECOND diagnostic for one fact and no row.
  Same ceiling, fewer sentences.
  DELEGATION, NOT A SECOND SPELLING: the partial-move report was HOISTED out of
  `consume` into `report_partial_move(VarState&, name, line)` and called from
  both routes, so the whole-value MOVE and the whole-value USE cannot drift.
  FIXTURES: the two closed programs move to tests/imported/fail/{borrowck,moves}
  with "partially moved" pinned; native pairs are
  tests/logos/fail/bc_recvpartial_{byval,shared}_recv_fail (the two self kinds)
  against tests/logos/pass/bc_recvpartial_{disjoint,reinit}_admit (the eight
  hand-written counter-examples, seven of which fired the armed site).

## recvaddrofpartial
site: src/compiler/borrow_check.cpp::check_live
build: —
measured: 2026-08-29 (OBSERVED, NOT PRICED)
fires: —
ceiling: —
cost: —
verdict: OPEN — the SIBLING SPELLING that `recvpartial` does not reach
note: found while writing `recvpartial`'s counter-examples, and it is the same
  missing observation one spelling over. A whole-value use through an EXPLICIT
  `&` handed to a call is still admitted after a partial move:
    let l = L{origin: P{..}, middle: P{..}};
    let _a = l.origin;
    let n = ro(&l);        → ADMITTED (rc 0, no diagnostic)   ⚠ rustc: E0382
  while `l.look()` — the same whole-value use, spelled as a method call — now
  refuses. `&l` reaches visit()'s AddrOf arm, which asks `check_live`, which
  reads the whole-variable `moved` flag and never `moved_fields`; the landed
  rule sits in the MethodCall arm and does not see this door.
  ⚠ NOT PRICED AND SO NOT CLAIMED: it has no `bc_admits.ledger` row of its own,
  so a ceiling probe would read 0 and that 0 would be corpus silence, not a
  refutation — rule 1 in its second form. The repro above is the evidence; a
  round that funds it must bring its own population (the pass corpus, or a
  hand-built one), not this file's.

## slicepatnull
site: src/compiler/borrow_check.cpp::each_pat_binding_place
build: armed gate build 26 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 66
ceiling: 3
cost: 6
verdict: ⛔ STOP AS SPELLED — the largest ceiling in the batch, bought with four spec-pass refusals
note: THE OBSERVATION IS SOUND AND THE SPELLING IS NOT. `each_pat_binding_place`
  gives every sub-pattern of a `PC::Slice` the container's own `base` (no index
  segment, unlike Tuple/Struct which call `sub()`), and those sub-patterns are
  `PC::Wild`, whose arm passes `TypeRef(nullptr)` as the binding TYPE. So the
  landed by-value sub-place move rule (`patbyvalsubmove`) tests
  `is_move_type(nullptr)` and skips EVERY array-pattern binding: `match a {
  [_, _, x] => … }` over `[String; 3]` records no move at all, confirmed by
  `LOGOS_PBSM_TRACE=1` emitting not one line for these three fixtures.
  Coverage map 2026-08-28: `PC::Slice` 111 arrivals, `PC::Wild` 1512.
  PREDICTED borrowck-move-out-from-array-match, --use-match--b, --use-match--t13.
  CLOSED exactly those three, both directions empty — the cleanest aim in the
  batch.
  ⚠ THE COST IS THE PROBE'S SPELLING, NOT THE MECHANISM'S PRICE. "null type ⇒
  move type" also catches struct-shorthand field bindings (82 arrivals) and
  every other null-typed binding, and four of the six costs are SPEC RULES
  (25_spec_pass pat_3, pat_4, pat_7, stmt_2). The correct spelling asks the
  SCRUTINEE for the element type instead of assuming; that is a change to what
  the pattern walk CARRIES, and rule 7 says it will not close the same three.
  NOT FUNDABLE AS MEASURED — a ledger row may not be bought with a legal-program
  refusal.

## destrmove
site: src/compiler/borrow_check.cpp::deref_move_exempt
build: armed gate build 27 (unarmed baseline 21; probe batch of 2026-08-29)
measured: 2026-08-29
fires: 4
ceiling: 2
cost: 1
verdict: the exemption's own NAMED residual, priced — and RULE 4 was declared in advance
note: exemption (4) of `deref_move_exempt` says in its own comment that "a
  destructure that binds a NON-Copy field out of a reference stays admitted
  (tests/imported/admit/nll/move-errors--d keeps its row)". Suppressing it
  closes move-errors--d — the row the comment names — and access-mode-in-
  closures (`let S { v: inner } = *s;`, the same shape through a closure param).
  PREDICTED four: access-mode-in-closures, borrowck-move-error-with-note--a,
  deref-field-pattern-ref-suggestion-issue-146995, move-errors--d.
  CLOSED two; predicted∖closed = {borrowck-move-error-with-note--a,
  deref-field-pattern-…-146995}, closed∖predicted = ∅. Those two are NOT this
  exemption: they move out of a user-`Deref` receiver, where the walk breaks at
  the CALL before any deref arm is reached.
  ⚠ RULE 4, DECLARED BEFORE THE RUN: the coverage map reaches this guard 2944
  times over 8060 runs and TAKES it three times; the probe fired four. A
  ceiling off a population of four bounds almost nothing.
  ⚠ THE COST IS THE EXEMPTION'S OWN PAIRED CONTROL (bc_deref_move_exempt_admit),
  i.e. the exemption is load-bearing exactly where it was documented to be. A
  fix must carry the pattern's move-ness to the destructure temp's `let`, which
  the comment already says is a sema change and its own round.

## callroot
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 18 (unarmed baseline 17; re-priced against the 365-row ledger)
measured: 2026-08-29
fires: 4046842
ceiling: 4
cost: 3
verdict: RE-PRICED (rule 8) — 3 rows on the 447-row ledger of 2026-08-27, 4 on the 365-row one
note: the walk breaks when a place is reached THROUGH A CALL (a user `Deref` /
  `Index` impl, or an autoref'd receiver sema lowered to a plain `Call`), so
  `bp.root` stays empty and `record_borrow` returns on its first line. Rooting
  at the receiver / arg0 with a whole-container path closes:
    borrowck-no-cycle-in-exchange-heap--move-while-refmut-borrowed
    cannot-borrow-index-of-hashmap-in-for · issue-81365-2 · issue-81365-3
  ⚠ THE SET IS THE FINDING, NOT THE COUNT. The `bck.B` gloss invites reading
  the nine `issue-81365-*` rows as ONE mechanism at this site. They are not:
  `callroot` closes TWO of the nine and leaves seven — -4--d2, -4--rd2, -8,
  -9--explicit-deref-call-borrow-then-write, -9--g-method-call-deref, -10, -11.
  A shared symptom is not a shared defect.
  COST 3: 03_ownership_pass_drop_for_loop_item_once (+ its control),
  25_spec_pass_borrow_2. Priced 2026-08-27 in the sixteen-hypothesis batch and
  never recorded outside that commit message; this is its first record here.
  ── 2026-08-29b: SUPERSEDED BY `callrootref`, which keeps all four rows and
  takes the corpus cost to 0 by hopping only through REFERENCE-RETURNING calls.
  The hand-written legal program that declined this probe still refuses under
  BOTH spellings (re-run this round; see `callrootref`), so neither is fundable
  and the blocker is sema's deref-mode selection, not this arm.

## refwhole
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 19 (unarmed baseline 17; re-priced against the 365-row ledger)
measured: 2026-08-29
fires: 2348033
ceiling: 1
cost: 600
verdict: ⛔ DEAD — 600 legal programs for one row, and `recvfieldpath` buys that row at 0
note: a place reached THROUGH a reference is recorded as a FIELD borrow of the
  REFERENCE BINDING, so two projections of one ref never overlap. Collapsing to
  the whole root closes issue-82032 and refuses 600 legal programs across
  02_semantic_core, the bc pass corpus and the spec dirs. `recvfieldpath`
  closes the same single row at COST 0 by asking the path maps instead of
  destroying the paths. Recorded so the collapse is not re-proposed.

---

## ⚠ TWO PROBE RULES PULL AGAINST EACH OTHER

── ⚠ TWO RULES PULL AGAINST EACH OTHER, AND ONE PROBE IS NOT ENOUGH ────────
"Put probe::on() FIRST in any &&" exists so a zero means the site was never
reached rather than the redirect never matching. But putting it first makes
the count the population of the OUTER condition, not of the mechanism.
MEASURED 2026-08-29: `dwatunwrap` reported 467 fires — every `DerefWrite` —
while the subset it was actually about, the `AddrOfTemp` spelling, went
uncounted. Its ceiling of 0 was then read against the wrong denominator.

So a mechanism with an inner predicate needs TWO names, not one:
    if (logos::probe::on("x_site") && inner_predicate(e)) {
        (void)logos::probe::on("x_match");   // the subset that matters
        ...
    }
`x_site` says the code path is live; `x_match` says how often the mechanism's
own condition held. A zero on the second over a large first is a refutation;
a zero on both is an unreached site; and only the pair can tell them apart.


⚠ THIS NOTE LIVES HERE AND NOT IN `probe.hpp`, AND THAT IS THE SECOND LESSON.
I wrote it into the header first — "put the rule beside the thing it qualifies"
— and `probe.hpp` is COMPILED. Twelve lines of prose, no code, shifted the line
tables of a RelWithDebInfo build, changed the binary hash, and invalidated all
58,703 verdicts in the measurement store: a green `L4 bc` from minutes earlier
suddenly described a compiler that no longer existed. This file was created an
hour before, for exactly this, and I did not use it.

**Prose about probes goes here. `probe.hpp` carries only what the compiler needs.**

---

# ROUND 2026-08-29b — G1 AND G2 RE-ATTRIBUTED BY MEASUREMENT

Eight probe names, five source edits, ONE build (`scripts/probe-batch.sh`).
L1 rc=0 with nothing armed, so the batch was inert. The three `callroot`-family
names and the two `mutstatic` names are RULE 9 PAIRS: an observational name
counting the OUTER population beside the mechanism's own name counting the
INNER one, because `probe::on()` first in an `&&` counts the wrong denominator
and `probe::on()` last cannot tell a false predicate from an unreached site.

    probe            fires  ceiling cost  predicted vs closed
    callsite          1131        0    0  observational (outer population)
    callrootref        355        4    0  EXACT 4/4 — ⛔ declined, see below
    callfldw          1299        4    0  predicted 11, closed 4 — REFUTED
    dwnoidx            944        0    0  re-price (rule 8), still 0
    nomutskip           25        2    2  EXACT 2/2 — ⛔ STOP, cost is legal
    opeqwritable        15        2    0  EXACT 2/2 — ✓ THE ONE TO FUND
    mutstaticsite        3        0    0  observational (outer population)
    mutstaticborrow      2        2    0  predicted 1, closed 2
    ptrderef          1095        0    0  re-price (rule 8), still 0

## G1 IS NOT ONE MECHANISM — IT IS FOUR, AND ONLY ONE IS FUNDABLE

The seven rows glossed "mut-ness is asked of the ROOT BINDING, never of the last
hop" were compiled by hand, one variable at a time, BEFORE any probe. They are
four different missing observations:

  G1a  a COMPOUND assignment never asks writability at all — 2 rows
       issue-85765, issue-93093.  ONE TOKEN APART, measured:
         let rofl: &V = &mut t;  rofl.n = 1i64;   -> REFUSED
         let rofl: &V = &mut t;  rofl.n += 1i64;  -> ADMITTED
       `lower_place_compound_assign` calls `place_write_supported` and never
       `check_place_writable`, which the plain-assign path calls. -> opeqwritable.
  G1b  `mut` in a PATTERN / on a PARAM has no bit to read — 1 row
       borrowck-ref-mut-of-imm--ref-mut-of-imm.  MEASURED:
         fn d(x: Option<i64>) { match x { Some(ref mut v) => … } }  -> ADMITTED
         let x: Option<i64>;   match x { Some(ref mut v) => … }     -> REFUSED
       The coverage map prices this hatch exactly: of 1,061,549 `&mut` arrivals
       at `take_borrow_whole_`, `is_mut_binding` is true 14,237 times and
       `param_names_` exempts 1,047,220 — 98.7% of every mut borrow in the tree.
       NOT PROBED: refusing that population is a legal-program refusal machine.
       The prerequisite is already named at `recvmutbind` — a by-value-`mut` bit
       on the pattern/param schema, set by sema. Same blocker, second row.
  G1c  `&mut <immutable module static>` is never asked — 2 rows (1 predicted)
       -> mutstaticborrow.
  G1d  a deref hop through an OWNING container drops the binding-mut question —
       2 rows: borrow-immutable-deref-box, borrowck-access-permissions--c.
       -> nomutskip, and its two costs are legal programs. STOP.
  and cannot-borrow-index-of-hashmap-in-for, the seventh, is a G2 row: it closes
  under callrootref, not under anything mut-shaped.

## G2's HOP IS THE RIGHT IDEA AT THE RIGHT PLACE AND STILL CANNOT BE BOUGHT

Narrowing the hop to REFERENCE-RETURNING calls keeps all four of `callroot`'s
rows and takes its corpus cost from 3 to 0 (`callrootref`). It does NOT rescue
the hand-written program that declined `callroot` on 2026-08-28 — re-verified on
today's tree under each of the three names, identical diagnostic each time:
    match *x { Cycle::Node(ref mut y) => { y.a = Box::new(2i64); } }   // legal
    -> "cannot borrow 'x.0' as mutable: 'x' is behind a `&` reference"
The reason is upstream of the walker: sema lowers a `match *box` scrutinee
through the SHARED `Deref::deref` even when a sub-pattern binds `ref mut`, so
`cross()` records a `&` crossing and record_borrow's E0596 gate fires on a
program that needs `DerefMut`. Until the scrutinee's deref MODE follows the
pattern's binding mode, every hop through that call refuses this program.

## callsite
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 40 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 1131
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — the OUTER population of the call-hop arm, and it changes nothing
note: RULE 9's missing half. `callroot`'s condition is
  `probe::on("callroot") && (kind == MethodCall || Call || AddrOfTemp)`, so its
  4,046,842 fires count every arrival at the arm and say nothing about the
  subset a NARROWED hop would take. This name sits alone at the top of the arm
  and counts arrivals without changing a verdict: 1131 over the 363-row ledger,
  against `callrootref`'s 355. So the reference-returning predicate holds on
  31% of the arm's traffic, and `callrootref`'s numbers are read against 355 —
  not against 1131, and not against 4 million.
  ⚠ The coverage map cannot supply this number: over the 8060-run population it
  counts 22,933,255 arrivals at the `} else { break; }` that swallows the call
  kinds together with every other unhandled expression kind. Only a name at the
  arm separates them.

  ── 2026-08-29c, RE-PRICED (rule 8) under armed gate build 51 against the
  361-row ledger: 1132 fires, ceiling 0, cost 0. 1131 on 363 rows, 1132 here —
  the arm's traffic is stable. The reference-returning subset is 346-356
  depending on which name is armed (a hop that refuses earlier reaches the arm
  slightly less often), so read `callindexchain`'s numbers against ~350, not
  against 1132.
## callrootref
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 34 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 355
ceiling: 4
cost: 0
verdict: ⛔ DECLINED — the corpus cost went 3 → 0 and the LEGAL PROGRAM that declined `callroot` still refuses
note: `callroot` hops to the receiver/arg0 of ANY Call/MethodCall/AddrOfTemp.
  This narrows it to calls whose RESULT IS A REFERENCE (`is_ref_kind`, the same
  predicate `cross()` already uses) — a call returning an owned value is a
  TEMPORARY, not a place, and rooting its borrow at arg0 is what refused legal
  programs. MEASURED: same ceiling, same four rows, and the corpus cost drops
  from `callroot`'s 3 to 0.
  PREDICTED borrowck-no-cycle-in-exchange-heap--move-while-refmut-borrowed,
  cannot-borrow-index-of-hashmap-in-for, issue-81365-2, issue-81365-3.
  CLOSED exactly those four; predicted∖closed = ∅ and closed∖predicted = ∅.
  ⚠ AND COST 0 IS STILL NOT A SAFETY CLAIM — RULE 5, AND IT BIT. The program
  that declined `callroot` on 2026-08-28 is not in any corpus, so it cannot
  appear in a COST column. Re-run by hand on today's tree under `callroot`,
  `callrootref` AND `callfldw`, identical diagnostic under all three:
      let mut x: Box<Cycle> = Box::new(Cycle::Node(NodeD{a: Box::new(1i64)}));
      match *x { Cycle::Node(ref mut y) => { y.a = Box::new(2i64); } … }
      → "cannot borrow 'x.0' as mutable: 'x' is behind a `&` reference"
  legal Rust, refused. The narrowing does not touch it, because the defect is
  UPSTREAM OF THE WALKER: sema lowers a `match *box` scrutinee through the
  SHARED `Deref::deref` even when a sub-pattern binds `ref mut`, so `cross()`
  records a `&` crossing and record_borrow's E0596 gate fires. THE PREREQUISITE,
  named so the next round does not re-derive it: the scrutinee's deref MODE must
  follow the pattern's binding mode (`DerefMut::deref_mut` when any sub-pattern
  is `ref mut`). Ten counter-examples were run under each name; the other nine
  stayed rc 0, seven of them with the probe firing (7, 7, 1, 1, 1, 1, 1).

  ── 2026-08-29c, RE-PRICED (rule 8) under armed gate build 49 against the
  361-row ledger: 356 fires, CEILING 4, COST 0, THE SAME FOUR ROWS. Predicted by
  name again before the run; both diffs ∅. Its value this round is as the
  CONTROL for `callindexchain`: one property differs between them — does the hop
  set `index_in_chain` — and the ceiling goes 4 → 13.
  ⚠ AND THE CONTROL IS WHAT EXPOSED THE COST. `let r: &mut i64 = &mut b.f;` on a
  `Box<S>` is legal Rust, is rc 0 HERE with the hop running (6 fires, so the site
  is reached), and is rc 1 under `callindexchain`. Without a control that hops
  and deposits nothing, that E0596 refusal would have been blamed on the hop.
## callfldw
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 39 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 1299
ceiling: 4
cost: 0
verdict: ⛔ REFUTED AS A COMPOSITION — predicted 11 rows, closed the same 4 as `callrootref` alone
note: THE HYPOTHESIS WAS THE "NULL RESULT THROUGH A BROKEN CHANNEL" SHAPE, and
  it was wrong. `callroot` closes 2 of the 9 `issue-81365-*` rows; the other 7
  differ in exactly one property — the hop lands with an EMPTY path (a whole-`c`
  / whole-`self` borrow) where -2/-3 land with path "container". `dwnoidx` had
  already priced the place-write reader at 0, but that zero was taken with NO
  hop armed, i.e. against a channel that deposits nothing. So: arm both.
  This name arms the reference-narrowed hop AND drops the `saw_index` conjunct
  at the DerefWrite exclusivity gate, in one process.
  PREDICTED 11: `callrootref`'s four PLUS issue-81365-4--d2, -4--rd2, -8,
  -9--explicit-deref-call-borrow-then-write, -9--g-method-call-deref, -10, -11.
  CLOSED 4. closed∖predicted = ∅; predicted∖closed = ALL SEVEN.
  ⚠ TWO SITES, ONE NAME — declared in advance and the sum decomposes exactly:
  1299 = 355 (`callrootref`'s site) + 944 (`dwnoidx`'s site), both of which were
  measured separately in the same build. Neither site's liveness rests on this
  aggregate.
  ── AND THE ONE-VARIABLE ISOLATION SAYS WHERE THE SEVEN ACTUALLY LIVE. Three
  programs, same struct, run against the armed binary:
      let r: &C = &c;        c.b = 9;   → REFUSED TODAY, unarmed
        ("cannot borrow 'c.b' as mutable: 'c' has shared borrows")
      let first = &c.tf;     c.cf = 9;  → ADMITTED under callfldw (7 hop fires)
        (`tf` reached through a user `Deref` — the -4--d2 shape)
      let first = &c.t.tf;   c.cf = 9;  → ADMITTED (legal: disjoint fields)
  The whole-root-borrow vs field-write direction is ALREADY CLOSED by an
  existing reader — line 1 proves it — so the read side was never the hole and
  `dwnoidx`'s zero was an honest zero. The hop RUNS on line 2 (7 fires) and the
  borrow is still not seen, so nothing recorded it: `visit()`'s AddrOfTemp arm
  records a whole-root borrow only when the place is "reached through DEREFS
  ALONE" (the reborrow peel), or `index_in_chain`, or the path is non-empty, or
  `slice_view_base_`. A place reached through a user `Deref` CALL satisfies NONE
  of them, and the arm's own comment says why the obvious widening was reverted:
  "recording a whole-root borrow whenever the path came back empty also fires
  for a plain AddrOfTemp(VarRef) — every method autoref — so `it.next()` in a
  loop conflicted with itself and liblogos-lang stopped building".
  ⇒ NEXT PROBE, NAMED: `callindexchain` — have `extract_borrow_place` set
  `index_in_chain = true` when it hops a reference-returning call. The
  whole-container semantics of that hop IS the index step's, the AddrOfTemp arm
  already records unconditionally for `index_in_chain`, and the flag does not
  fire for a bare `AddrOfTemp(VarRef)`. One flag, and it is the DEPOSIT side —
  not the walker (which already gives the right root) and not the reader (which
  already refuses the same shape without a deref).

## dwnoidx
site: src/compiler/borrow_check.cpp::visit_stmt
build: armed gate build 36 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 944
ceiling: 0
cost: 0
verdict: RE-PRICED (rule 8) — 0/0 on the 363-row ledger, as on the 400-row one
note: the place-write exclusivity refusal is gated on the AddrOfTemp walk having
  crossed an IndexRead/SliceIndex, so `s.f = v` / `t.0 = v` are exempt. Dropping
  the conjunct changed nothing on 2026-08-28 (189 fires, 400 rows) and changes
  nothing now (944 fires, 363 rows). Coverage map: 19,193 arrivals, `saw_index`
  true 6,518 — ~12,675 rooted place writes per pass really are exempt.
  ⚠ AND THE ZERO IS NOW EXPLAINED, not merely repeated. Isolated by hand on the
  armed binary: `let r = &c; c.b = 9;` is refused TODAY with no probe armed
  ("cannot borrow 'c.b' as mutable: 'c' has shared borrows"), so the whole-root
  vs field-write question already has a reader and this gate would only add a
  second diagnostic for the same fact. See `callfldw`.

## nomutskip
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: armed gate build 35 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 25
ceiling: 2
cost: 2
verdict: ⛔ STOP — EXACT aim, and both costs are legal programs
note: THE EXEMPTION IN THE ABUSE DIRECTION. `take_borrow_whole_`'s binding-mut
  arm is skipped whenever the caller passes `skip_mut_binding_check`. Ignoring
  the flag closes exactly the two G1d rows. ISOLATED ON ONE VARIABLE first:
      let x: i64 = 3;          f(&mut x);   → REFUSED "not declared as mut"
      let x: Box<i64> = …;     f(&mut *x);  → ADMITTED
      let x: Box<i64> = …;     let y: &mut i64 = &mut *x;  → ADMITTED
  and the borrow IS recorded (two of them collide, and moving `x` while one is
  live is refused) — so the loan lands and only the binding question is dropped.
  PREDICTED borrow-immutable-deref-box, borrowck-access-permissions--c-mut-
  borrow-deref-box. CLOSED exactly those two, both directions empty.
  ⚠ COST 2, and they are the exemption's own reason for existing:
  02_semantic_core_pass_bc_genrecv_constructed_legals_admit and
  02_semantic_core_pass_zone_mut_thin_source_admits_generic. A ledger row may
  not be bought with a legal-program refusal.
  ⚠ RULE 4, DECLARED IN ADVANCE: the coverage map reaches this guard 1,061,549
  times and the hatch is taken only 76 of them; the probe fired 25 over the
  ledger. A ceiling off that population bounds very little.
  ⚠ AND THE HATCH IS NOT WHERE THE PERMISSIVENESS IS. In the same 1,061,549
  arrivals `param_names_` exempts 1,047,220 — 98.7% — because a param carries no
  mut bit. That is G1b, and it needs the sema bit `recvmutbind` already named.

## opeqwritable — LANDED
site: src/compiler/sema_stmt.cpp::lower_place_compound_assign
build: 7f686966b0e62b67 (landed, gate build 43; priced under armed gate build 37)
measured: 2026-08-29
fires: 15
ceiling: 2
cost: 0
verdict: ✓ LANDED — the probe's edit IS the fix, one call, unchanged
note: FUNDED AND LANDED 2026-08-29. `check_place_writable(place_node)` now runs
  in `lower_place_compound_assign` immediately before the read-twice desugar,
  the same position the probe occupied, and it is the SAME unconditional call
  the plain place-assign path (`lower_place_assign`) has always made.
  CLOSED SET, DIFFED BOTH WAYS AGAINST A PREDICTION OF 2 MADE BY NAME BEFORE
  THE EDIT: predicted {issue-85765, issue-93093}; `ctest -R '^logos_00_bc_admit'`
  returned exactly those two as the only failures out of 364. predicted∖closed
  = ∅, closed∖predicted = ∅. Ledger 363 → 361, re-derived by direct listing.
  COST 0 RE-CONFIRMED ON THE LANDED RULE, not inherited from the probe:
  `ctest -L bc` 1800/1800 passed, 0 failed, 2 pre-disabled (build 42, the fix
  alone), and re-confirmed on the final tree under build 43 — the store holds
  the whole `bc` label at 0 failed.
  ⚠ REACH WITHOUT A FIRE LOG. A landed rule has no `probe::on()` counter, so
  every legal counter-example was PAIRED with a one-token twin that the new call
  must refuse; the twin's refusal is what proves the check reached that place
  shape. Twelve legal shapes green, ten twins refused, each read:
    &mut param / &param             → "assignment through a shared reference (variable 'v' is `&`)"
    mut local / immutable local     → "assignment to immutable variable 't'"
    mut array / immutable array     → "assignment to immutable variable 'a'"
    mut tuple field / immutable     → "assignment to immutable variable 't'"
    nested field / through `&O`     → "assignment through a shared reference (variable 'o' is `&`)"
    `&mut [T]` / `&[T]`             → "cannot write through a shared `&[T]` slice"
    `(*r)` over &mut / over &       → "assignment through a shared reference `&` (need `&mut`)"
    `self: &mut S` / `self: &S`     → issue-93093 itself
    `static mut` field / `static`   → "assignment to immutable static 'SV'"
    `*mut` in unsafe / `*const`     → "assignment through a `*const` pointer (need `*mut`)"
  Held as tests/logos/pass/bc_opeq_place_writable_ok.logos (twelve shapes, RUN,
  `exit: 0` gated on twelve value inequalities) and two fail fixtures.
  ⚠ RULE 7 — THE LANDED RULE AND THE PROBE ARE THE SAME EDIT, and that is the
  finding this time: twice before, the correct fix was narrower than its probe.
  Here the narrower fix was the WRONG one. Asking an `&`-only question in the
  compound path would have left two notions of writability in the tree, which is
  precisely how this gap opened; the unconditional call is the point. It buys
  one extra fact, predicted in advance and now pinned:
  `s.n += 1` on a non-`mut` local is refused (upstream E0594) —
  tests/logos/fail/bc_opeq_immut_local_write_fail. That is an ILLEGAL program,
  not a legal-program refusal.
  ⚠ CEILING 2 IS BELOW THE ROUND'S OWN FUNDING BAR OF 3, AND IT WAS FUNDED.
  Stated plainly so the next round can disagree: the two mechanisms that cleared
  the bar numerically — `callrootref` and `callfldw`, both ceiling 4 / cost 0 —
  each refuse a legal program (`match *x { Cycle::Node(ref mut y) => … }` over a
  `Box`), and a row may not be bought with a legal-program refusal. The bar
  prices a GUESS; this hole was found by construction, one token apart, and its
  fix is one call that already existed on the sibling path. Holding an admitted
  write-through-`&` open to satisfy a threshold meant for speculative work is
  the wrong trade.
  ⚠ AND `dwatunwrap` PRICED THESE SAME TWO ROWS AT 0 on 2026-08-29 at the
  borrow-check DerefWrite door. Its own note left the residual open — "or the
  compound `+=` spelling takes a door that is not DerefWrite at all" — and this
  is the answer. The borrow checker was never the site.
  ⚠ ONE INCIDENTAL LEGAL-PROGRAM REFUSAL FOUND WHILE WRITING THE COUNTER-
  EXAMPLES, AND IT IS NOT THIS RULE'S. `static mut CNT: i64; unsafe { CNT += 1 }`
  is refused — "compound assignment to immutable variable 'CNT'". That string is
  emitted at exactly one place, the `!lookup_is_mut(name)` arm of
  `lower_compound_assign`, i.e. the BARE-VarRef fast path, which a bare `CNT`
  takes and which this change does not touch: the `static mut` FIELD spelling
  (`SV.n += 1`, which does reach the new call) compiles and runs, and is held in
  the pass fixture. So `lookup_is_mut` has no `static mut` arm where
  `check_place_writable` does. PRE-EXISTING, unpriced, recorded not fixed — it
  is a different mechanism in a different function and pricing it is its own
  round.

## opeqwritable-as-priced
site: src/compiler/sema_stmt.cpp::lower_place_compound_assign
build: armed gate build 37 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 15
ceiling: 2
cost: 0
verdict: ✓ FUND THIS — a compound assignment never asks writability, and the sibling call already exists
note: ONE TOKEN APART, MEASURED BY HAND BEFORE THE PROBE, four programs:
      let rofl: &V = &mut t;      rofl.n = 1i64;   → REFUSED
      let rofl: &V = &mut t;      rofl.n += 1i64;  → ADMITTED
      fn bar(self: &S) { self.foo = 2i64; }        → REFUSED
      fn bar(self: &S) { self.foo += 1i64; }       → ADMITTED
  The refusing spelling goes through `check_place_writable(place_node)`, called
  by the plain place-assign path. `lower_place_compound_assign` calls
  `place_write_supported` — "can the address machinery lower this" — and never
  asks the writability question at all. Its VarRef sibling
  (`lower_compound_assign`) DOES ask, via `lookup_is_mut`; the tree's own
  comment there records the gap ("the field spelling is UNCHECKED by this rule
  and would need its own"). This probe adds the ONE MISSING CALL.
  PREDICTED issue-85765, issue-93093. CLOSED exactly those two; predicted∖closed
  = ∅ and closed∖predicted = ∅.
  ⚠ AND `dwatunwrap` PRICED THE SAME TWO ROWS AT 0 ON 2026-08-29, at the borrow-
  check DerefWrite door. Two spellings of one question; the borrow-check one is
  refuted and the sema one closes both. Its own note left exactly this residual
  open — "or the compound `+=` spelling takes a door that is not DerefWrite at
  all" — and that is the answer.
  ⚠ COST 0 WITH COUNTER-EXAMPLES READ, not corpus silence: `fn bump(v:&mut V){
  v.n += 1; }`, `t.n += 1` on a mut local, `a[0] += 5` on a mut array, and
  `fn bar(self:&mut S){ self.foo += 1; }` all stay rc 0 AND each fired the armed
  site (1, 1, 1, 2 fires). 15 fires over the ledger — RULE 4 applies to the
  ceiling, not to the counter-examples.
  ⚠ RULE 7 WARNING FOR WHOEVER LANDS IT: the probe calls `check_place_writable`
  unconditionally, which also refuses `s.n += 1` on a non-`mut` local `s`. That
  is correct Rust and costs nothing here, but it is a SECOND fact the probe buys
  along with the two rows; a landed rule should predict both or ask only the
  `&`-reference half.

## mutstaticsite
site: src/compiler/sema_expr.cpp::lower_expr_inner
build: armed gate build 41 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 3
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — the OUTER population of `&mut <module static>`
note: RULE 9's other half. `mutstaticborrow` sits AFTER the `!module_static_muts_`
  predicate so its own count is the non-`mut` SUBSET; this one counts every
  arrival at the `is_module_static_unshadowed` branch of ADDR_OF_MUT. 3 arrivals
  over the ledger, of which `mutstaticborrow` fires on 2 — so exactly one is a
  genuine `static mut`. MEASURED on the counter-example too: `static mut SY;
  unsafe { &mut SY }` fires `mutstaticsite` once and `mutstaticborrow` zero
  times. That zero is the exemption HOLDING, and without this name it would be
  indistinguishable from an unreached site.

## mutstaticborrow
site: src/compiler/sema_expr.cpp::lower_expr_inner
build: armed gate build 38 (unarmed baseline 33; probe batch of 2026-08-29b)
measured: 2026-08-29
fires: 2
ceiling: 2
cost: 0
verdict: ✓ `&mut <immutable static>` is assumed, never asked — and it closed a row nobody nominated
note: the branch's own comment says "`&mut STATIC` (a `static mut`) IS the
  global's address" and nothing checks that the static is `mut`. ISOLATED, one
  variable:
      static SX: i64 = 1;  let y: &mut i64 = &mut SX;  → ADMITTED
      let sx: i64 = 1;     let y: &mut i64 = &mut sx;  → REFUSED
  and the WRITE half is already asked — `SX = 2;` is refused ("assignment to
  immutable variable"). Only the BORROW half was missing.
  PREDICTED borrowck-access-permissions--b-mut-borrow-of-static. CLOSED that AND
  issue-42344 — a `bck.NEW` row nobody nominated, and it is the same three lines
  (`static TAB: i64 = 5; let r: &mut i64 = &mut TAB;`). predicted∖closed = ∅.
  ⚠ RULE 4 IN FORCE: 2 fires off an outer population of 3 (`mutstaticsite`).
  A ceiling of 2 off a population of 3 bounds almost nothing about the SET.
  ⚠ AND THE ABUSE DIRECTION IS STILL OPEN AT THIS SITE, unmeasured: the branch
  also hands out `&mut SY` for a genuine `static mut` with no `unsafe`
  requirement, while the WRITE path does demand one. Not this round's row.

## ptrderef
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 32 (unarmed baseline 30; 2026-08-29b, before the batch)
measured: 2026-08-29
fires: 1095
ceiling: 0
cost: 0
verdict: RE-PRICED (rule 8) — still 0/0, now on the 363-row ledger
note: last priced 2026-08-27 at 314 fires against the 447-row ledger. The
  population more than tripled and the answer did not move. Priced FREE: the
  probe was already compiled into the baseline binary, so this cost one armed
  run and no build. It was re-asked because `*x` on a `Box` lowers to a RAW-
  pointer deref (measured: `*x = 5` says "write through raw pointer requires
  unsafe context"), which made the raw-ptr bail a candidate for G1d's two rows.
  It is not: those two close under `nomutskip` and not here.

---

# ROUND 2026-08-29c — THE DEPOSIT SIDE, AND THE BLOCKER RE-LOCATED

Four source edits, ONE build (`scripts/probe-batch.sh`), L1 rc=0 with nothing
armed so the batch was inert. Eleven names priced (four in the batch, seven by
hand against the same build, because several names share one edit and
`probe-batch.sh` prices one name per record).

    probe            fires  ceiling cost  predicted vs closed
    callsite          1132        0    0  observational (outer population of the call arm)
    callrootref        356        4    0  EXACT 4/4 — re-priced (rule 8), unmoved on 361 rows
    callindexchain     346       13    0  predicted 11, closed 13 — all 11 PLUS 2 nobody nominated
    callidxcallonly    347       13    0  the SAME 13 — the AddrOfTemp hop contributes nothing
    callidxdm          347       13    0  the same 13, AND the legal program that declined the family compiles
    matchderefsite      14        0    0  observational (every `match *x`)
    matchderefmut        1        0    0  ONE arrival in the whole population
    mbsite          183912        0    0  observational (guard arrivals with the mut bit absent)
    mbhatch         183912        0    0  observational — the hatch is taken 183912 / 183912
    mbrefuse             0        —    —  NEVER FIRED here; proven live BY HAND (see below)
    mbnoparam       177798      361 1037  ⛔ closing the hatch closes the ENTIRE ledger by breaking the build

## THE DEPOSIT WAS THE HOLE, AND IT IS WORTH NINE MORE ROWS THAN THE HOP

`callrootref` (hop through reference-returning calls only) closes 4.
`callindexchain` (the same hop, plus `index_in_chain` on the hopped step)
closes 13. The difference is NINE rows and it is entirely the DEPOSIT: the
walker already produced the right root, and `visit()`'s AddrOfTemp arm threw it
away because the place came back with an empty path and no index step.

CLOSED SET (13), diffed both ways against a prediction of 11 made BY NAME
before the build:
    borrowck-borrow-overloaded-auto-deref                     ← not predicted
    borrowck-no-cycle-in-exchange-heap--move-while-refmut-borrowed
    borrowck-overloaded-index-and-overloaded-deref--t15       ← not predicted
    cannot-borrow-index-of-hashmap-in-for
    issue-81365-2 · -3 · -4--d2 · -4--rd2 · -8
    issue-81365-9--explicit-deref-call-borrow-then-write
    issue-81365-9--g-method-call-deref · -10 · -11
predicted∖closed = ∅.  closed∖predicted = {borrowck-borrow-overloaded-auto-deref,
borrowck-overloaded-index-and-overloaded-deref--t15}.
⚠ ALL NINE `issue-81365-*` ROWS CLOSE TOGETHER. The 2026-08-29b note warned that
the `bck.B` gloss invites reading them as one mechanism and that `callroot`
closed only two of nine — "a shared symptom is not a shared defect". They ARE
one defect; `callroot` was only half of it (walker without deposit), and half a
mechanism closes a subset that looks like a refutation of the whole.

## READ THE ARTEFACT: THIRTEEN ROWS, FOUR DIAGNOSTICS

Each closed row was compiled by hand under `callidxdm` and its diagnostic read.
Ten refuse for the mechanism's own reason (the E0506 write-after-borrow family):
`'c' has shared borrows` (5), `'self.container' is already borrowed` (2),
`already mutably borrowed` (1), `cannot assign to 'v' because it is borrowed` (1),
plus `borrowck-borrow-overloaded-auto-deref`'s E0596. TWO refuse for a reason
that is NOT this mechanism's and was inherited from `callrootref`:
    cannot-borrow-index-of-hashmap-in-for            "not declared as mut"
    borrowck-no-cycle-in-exchange-heap--move-while-… "'x' not declared as mut"
Upstream those are E0502 and E0505. The rows close; the SENTENCES are wrong.
Whoever lands this must predict that, not discover it in a fixture.

## ⛔ AND COST 0 IS WRONG — RULE 5, AND IT DECIDED THE ROUND

The corpus says 0 for `callindexchain`, `callidxcallonly` and `callidxdm`.
A five-line hand-written program says otherwise:
    let mut b: Box<S> = Box::new(S { f: 1i64 });
    let r: &mut i64 = &mut b.f;   *r = 2i64;      // legal Rust
    unarmed        rc 0        callrootref    rc 0 (6 fires)
    callindexchain rc 1        callidxdm      rc 1
    → "cannot borrow 'b' as mutable: 'b' is behind a `&` reference"
`callrootref` admits it because it deposits nothing; the moment the deposit
lands, `record_borrow`'s E0596 gate reads a `through_ref_type` that `cross()`
took from `Box::deref`'s SHARED `&S` result. The plain-write twin `b.f = 2i64`
stays rc 0 under every name, so the defect is the `&mut` BORROW spelling.
Four other legal shapes stay rc 0 with the armed site PROVEN REACHED (6, 6, 12,
8 fires): a shared borrow through the `Deref` plus a disjoint field READ; the
same borrow scoped to end before the write; two shared borrows through the
`Deref`; and `&v[0]` / `&v[1]`. Two more (a direct `&c.t.tf` field path, and
`it.next()` in a loop — the shape whose earlier widening broke liblogos-lang)
do not reach the arm at all, 0 fires, which is the narrowness this flag needed.

## (B) IS NOT A MATCH-SCRUTINEE DEFECT AND IT IS NOT PHASE ORDERING

The 2026-08-29b note said the blocker was sema lowering a `match *box`
scrutinee through the shared `Deref::deref`. That is one SPELLING of it. The
answer to "where does sema choose, and can it see the context":

  · METHOD receivers ASK. `lower_method_call`'s auto-deref loop computes
    `bool want_mut = target_method_wants_mut_self(probe_target, m)` and its own
    comment records that an over-eager `true` is safe, "emit_generic_deref_step
    falls back to Deref if there's no DerefMut impl". ⚠ THAT CLAIM IS FALSE —
    measured in part 2 below; the fallback recovers the TARGET TYPE and still
    emits a `deref_mut` call that resolves to nothing.
  · FIELD access does NOT. `lower_field_read`'s auto-deref loop calls
    `emit_generic_deref_step(recv, /*want_mut=*/false)` — hardcoded, no
    parameter, no channel from the use context. THIS is what refuses `&mut b.f`.
  · `*x` does NOT. `lower_deref` calls the same step with `/*want_mut=*/false`.
  · A MATCH SCRUTINEE is lowered by `lower_match`'s first statement, before any
    arm is inspected — but the arms are in the SAME `node` (`la::ITEMS`), which
    the same function reads a few lines later for the catchall lint. So the
    information IS available where the choice is made. This is a MISSING
    PARAMETER, not a phase-ordering answer.

MEASURED, so the scrutinee spelling is not where the population is:
`matchderefsite` counts every `match *x` in the whole population — 14 — and
`matchderefmut`, which fires only when a mutable step was actually BUILT, fires
ONCE. The scrutinee fix is free and closes nothing by itself (ceiling 0, cost 0);
it makes the declining program of 2026-08-29b compile (verified by hand: rc 1
under `callroot`/`callrootref`/`callindexchain`/`callidxcallonly`, rc 0 under
`callidxdm` and `matchderefmut`). But `callidxdm` still refuses `&mut b.f`,
because that goes through the FIELD site, not the scrutinee site.

## (C) THE HATCH IS NOT A HATCH — IT IS THE GUARD'S ONLY EXIT

Three names at `take_borrow_whole_`'s binding-mut arm, all inside
`!skip_mut_binding_check && !it->is_mut_binding`:
    mbsite   183912   every arrival with the mut bit absent
    mbhatch  183912   the subset `param_names_` exempts
    mbrefuse      0   what the guard actually refuses
100.0%, not 98.7%. Over the acceptance ledger and the whole legal corpus the
guard refuses NOTHING.
⚠ AND THAT ZERO IS A PROPERTY OF THE POPULATION, NOT OF THE GUARD — rule 2, in
its sharpest form yet. Both selections consist ENTIRELY of programs that
compile, so no refusing branch anywhere in the compiler can fire in them; a
`cost`/`ceiling` harness can never see a refusal site's own traffic. Proven live
BY HAND, six lines:
    fn f(p: &mut i64) -> i64 { return *p; }
    let x: i64 = 3i64;  let _ = f(&mut x);
    → "cannot borrow 'x' as mutable: not declared as mut",
      mbrefuse 1 fire, mbsite 129 fires IN THE SAME COMPILE.
So the guard decides exactly one thing — is a NON-param, non-`mut` local being
`&mut`-borrowed — and that decision is invisible to every corpus this harness
measures. The earlier reading ("the guard runs a million times and declines to
decide in all but 14,329") counted arrivals in a population that cannot contain
its decisions.
⚠ AND THE HATCH IS STRUCTURAL. `mbnoparam` (refuse anyway when `param_names_`
would have exempted) fires 177,798 times, refuses 1037 legal programs and
"closes" all 361 ledger rows — the degenerate pole, i.e. the stdlib stops
compiling. `param_names_` is not an escape hatch that occasionally lets
something through; it is the arm's only exit for every program that builds.
⚠ THE SECOND COPY. The same `!it->is_mut_binding && !param_names_.count(target)`
test exists a second time in `take_field_borrow_path_`, with a different
sentence ("'{}' not declared as mut"). Two notions of one question; unmeasured.
⚠ `nomutskip`'s TWO LEGAL COSTS COME FROM THE GUARD, NOT THE HATCH, and the
argument is structural rather than measured: `skip_mut_binding_check` and
`param_names_` are two independent exits, and the report is only reachable when
`param_names_` does NOT hold the target. A fixture that emits the diagnostic
under `nomutskip` therefore declined the hatch. Not re-measured — that would
need `nomutskip` back in a build.



## callindexchain
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 45 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 346
ceiling: 13
cost: 0 by the corpus, ⛔ NOT 0 — one hand-written legal program is refused
verdict: ⛔ NOT FUNDABLE AS SPELLED — the largest ceiling this file has recorded, and it refuses `&mut b.f` on a `Box`
note: the probe named by 2026-08-29b's `callfldw` finding, and the finding was
  right: the hole is the DEPOSIT. `visit()`'s AddrOfTemp arm records a whole-root
  borrow only for a place reached through derefs alone, or `index_in_chain`, or a
  non-empty path, or `slice_view_base_`; a place reached through a user `Deref`
  CALL satisfies none of them, so the hop produced a correct root and nothing
  wrote it down. Setting `index_in_chain` on the hop routes it to the arm that
  already records unconditionally — the whole-container semantics of a
  reference-returning call hop IS the index step's.
  CLOSED 13, PREDICTED 11 BY NAME BEFORE THE BUILD; predicted∖closed = ∅ and
  closed∖predicted = {borrowck-borrow-overloaded-auto-deref,
  borrowck-overloaded-index-and-overloaded-deref--t15}, i.e. an overloaded-Index
  place and an `Rc<Point>` auto-deref — the same mechanism at two more spellings.
  ⚠ THE COST. See the round header: `let r: &mut i64 = &mut b.f;` on a
  `Box<S>` is legal Rust, compiles today, compiles under `callrootref`, and is
  REFUSED here. The deposit is not what is wrong — the deposit is what makes an
  already-wrong `through_ref_type` visible. The blocker is in sema, and it is
  the FIELD auto-deref step, not the match scrutinee.

  ── 2026-08-29c part 2, RE-PRICED under a SECOND build (armed 60, unarmed
  baseline 56) (rule 8, within the
  same day): 346 fires, CEILING 13, COST 0, SET-IDENTICAL. Two builds apart,
  same thirteen names — the measurement is stable, and that is what licenses
  reading `callidxfdm`'s set as a DIFFERENCE rather than as noise.
## callidxcallonly
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: armed gate build 50 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 347
ceiling: 13
cost: 0 by the corpus (same hand-written refusal as `callindexchain`)
verdict: the NARROWER spelling closes the SAME SET — rule 7, measured rather than assumed
note: identical to `callindexchain` except that the flag is set only when the
  hopped node is a `Call`/`MethodCall`, never a bare `AddrOfTemp`. Same 13 rows,
  set-identical. So the AddrOfTemp arm of the hop buys nothing and can be
  excluded for free — worth knowing, because AddrOfTemp is every autoref and is
  where the 2026-08-27 widening broke liblogos-lang.

## callidxdm
site: src/compiler/borrow_check.cpp::extract_borrow_place + src/compiler/sema_stmt.cpp::lower_match
build: armed gate build 47 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 347
ceiling: 13
cost: 0 by the corpus (same hand-written refusal as `callindexchain`)
verdict: the joint probe — deposit + the match-scrutinee deref mode
note: TWO SITES, ONE NAME, DECLARED IN ADVANCE, and the sum decomposes exactly:
  347 = 346 (`callindexchain`'s site) + 1 (`matchderefmut`'s), both measured
  separately in the same build.
  It buys `callindexchain`'s 13 rows AND makes the program that declined the
  whole call-hop family on 2026-08-28 compile again (verified by hand under six
  names). It does NOT rescue `&mut b.f`, because the scrutinee is the wrong
  spelling of the deref-mode defect.

## matchderefsite
site: src/compiler/sema_stmt.cpp::lower_match
build: armed gate build 53 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 14
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — every `match *x` in the whole population is fourteen
note: rule 9's outer half for `matchderefmut`. Fourteen arrivals; the inner name
  fires once, so THIRTEEN of the fourteen are `match *r` over a plain reference,
  which `emit_generic_deref_step` declines (non-struct operand) and `lower_deref`
  handles with the ordinary pointee path. The Deref-struct scrutinee is a
  population of ONE.

## matchderefmut
site: src/compiler/sema_stmt.cpp::lower_match
build: armed gate build 48 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 1
ceiling: 0
cost: 0
verdict: the 2026-08-29b BLOCKER, REPAIRED AND PRICED — and it is not where the population is
note: `lower_deref` lowers `*x` over a Deref-impl struct with `want_mut`
  HARDCODED false, so a `match *box` scrutinee crosses the SHARED `Deref::deref`
  and `cross()` records a `&` crossing that `record_borrow`'s E0596 gate then
  refuses. This probe takes the DerefMut step for a match scrutinee instead, in
  both the statement (`lower_match`) and expression (`lower_match_expr`) forms.
  ⚠ THE FIRE COUNT IS THE MECHANISM'S OWN, NOT THE ARM'S. `probe::on` is called
  only where a mutable step was actually BUILT; the armed name is resolved from
  the environment once, by hand, so the decision does not itself count. That is a
  second armed-detection path and it is recorded here rather than hidden.
  MEASURED BY HAND on the program that declined `callroot`/`callrootref`/
  `callfldw` (2026-08-28, re-verified 2026-08-29b):
      match *x { Cycle::Node(ref mut y) => { y.a = Box::new(2i64); } … }
    unarmed rc 0 · callroot rc 1 · callrootref rc 1 · callindexchain rc 1 ·
    callidxcallonly rc 1 · matchderefmut rc 0 · callidxdm rc 0
  ⚠ AND THE PROMPT'S FRAMING OF IT WAS WRONG, WHICH IS WORTH RECORDING: this is
  NOT "a live over-refusal on legal Rust independent of any probe". The tree
  ADMITS that program today (rc 0 unarmed, measured). It is a refusal the
  call-hop probes MANUFACTURE, i.e. a blocker for the hop, not a standing defect.
  ⚠ ceiling 0 / cost 0 is what a repaired OVER-refusal must look like: the
  harness measures rows CLOSED and legal programs BROKEN, and an over-refusal
  repair does neither. Its evidence is the hand-run above, not this table.

## mbsite
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: armed gate build 54 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 183912
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — the denominator for (C)
note: arrivals at the binding-mut arm with `skip_mut_binding_check` false and
  `is_mut_binding` false, i.e. every borrow whose legality the guard is actually
  asked about.

## mbhatch
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: armed gate build 55 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 183912
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — 183912 / 183912. The exemption is total, not 98.7%
note: the coverage map's 1,047,220-of-1,061,549 reading was over a different
  population (8060 runs including four stdlib layers). On the ledger plus the
  whole legal corpus the ratio is 1.000: `param_names_` exempts every arrival.

## mbrefuse
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: armed gate build 56 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 0 over the harness population; 1 on a hand-written program
ceiling: — (the harness refuses a ceiling on a zero fire count, correctly)
cost: —
verdict: NEVER FIRED — and the reason is the POPULATION, which is rule 2's sharpest instance in this file
note: the branch that actually emits "cannot borrow 'X' as mutable: not declared
  as mut". Zero arrivals across 361 ledger rows and the whole legal corpus,
  while its enclosing `if` (`mbsite`) took 183,912. A zero on the inner name
  over a large outer is normally a REFUTATION (rule 9). It is not one here:
  BOTH harness selections consist only of programs that COMPILE, so no refusing
  branch in the compiler can fire in either. Proven live by hand — six lines,
  one fire, with `mbsite` at 129 in the same compile (see the round header).
  ⚠ THE GENERAL LESSON, and it applies to every `cost` in this file: the ceiling
  harness can measure how often a REFUSAL SITE IS AVOIDED and never how often it
  fires, because its populations are defined by success. A refusal site's own
  traffic needs the fail corpus, which nothing here selects.

## mbnoparam
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: armed gate build 46 (unarmed baseline 44; probe batch of 2026-08-29c)
measured: 2026-08-29
fires: 177798
ceiling: 361
cost: 1037
verdict: ⛔ THE DEGENERATE POLE — it closes the WHOLE ledger, which means it broke the build
note: closing the `param_names_` hatch refuses 177,798 borrows, 1037 legal
  programs, and "closes" all 361 rows the way `selftest_refuse` does: by making
  nothing compile. This is the priced answer to "is the hatch load-bearing" —
  it is not a hatch, it is the arm's only exit. G1b's row
  (borrowck-ref-mut-of-imm--ref-mut-of-imm) was predicted and is inside the 361,
  which tells us nothing: a ceiling equal to the whole ledger names no set.
  ⚠ RULE 6 IN THE OTHER DIRECTION — a ceiling that equals the population is not
  a big win, it is a broken run, and the harness's own `selftest_refuse` is the
  calibration that says so.

---

# ROUND 2026-08-29c PART 2 — THE BLOCKER, PRICED AT ITS REAL SITE

Four more edits, ONE build, L1 rc=0 unarmed. Part 1 ended with a legal program
(`let r: &mut i64 = &mut b.f;` on a `Box<S>`) refused by the deposit and with
the blocker located at the FIELD auto-deref step. This part prices that site.

    probe            fires  ceiling cost  predicted vs closed
    fldderefsite        16        0    0  observational — the field auto-deref loop is a population of SIXTEEN
    fldderefmut         16        6    0  predicted 0, closed 6 — and cost 0 is WRONG (three hand-written programs)
    callidxfdm         363       13    0  predicted 12, closed 13 — one predicted swap, one row nobody nominated
    callindexchain     346       13    0  re-priced on a second build, SET-IDENTICAL

## THE SET DIFFERENCE IS THE WHOLE RESULT

callindexchain (deposit only)   ∖ callidxfdm = {borrowck-borrow-overloaded-auto-deref}
callidxfdm (deposit + deref mode) ∖ callindexchain = {deref-field-pattern-ref-suggestion-issue-146995}

Both halves were worth predicting and one was predicted:
  · borrowck-borrow-overloaded-auto-deref RE-OPENS, exactly as declared before
    the build. It closed under `callindexchain` only because `&mut p.y` on an
    `Rc<Point>` crossed `Rc::deref`'s shared `&Point` and hit the E0596 gate.
    rustc rejects it because Rust's `Rc<T>` has NO `DerefMut`; Logos's Rc HAS
    one. So that row is a DIVERGENCE about the stdlib's trait set, not a
    borrow-check hole, and a mechanism that closes it is closing it by accident.
  · deref-field-pattern-ref-suggestion-issue-146995 is new and nobody nominated
    it — and it is already NAMED in this file, as one of `destrmove`'s two
    unpredicted misses ("they move out of a user-`Deref` receiver, where the
    walk breaks at the CALL before any deref arm is reached"). It closes here.

## ⛔ COST 0 IS WRONG A SECOND TIME, AT A SECOND SITE, AND IT IS THE SAME RULE 5

`fldderefmut` prices at cost 0 against the whole corpus. Three hand-written
legal programs over a struct `C` that impls `Deref` and NOT `DerefMut` fail:
a shared borrow through the Deref plus a disjoint field read; the same borrow
scoped; two shared borrows. All three were rc 0 under every part-1 name with the
armed site REACHED (6, 6, 12 fires), and all three now die — not with a
borrow-check diagnostic but with
    mlir_gen: internal: `let first` initializer produced no value (expr kind 12)
        — statement DROPPED (dependents will vanish too)

⚠ AND THAT FALSIFIES A SAFETY CLAIM WRITTEN IN THE TREE. The method-call
sibling's own comment says an over-eager `want_mut=true` is safe because
"emit_generic_deref_step falls back to Deref if there's no DerefMut impl". The
fallback picks the `Deref` IMPL to recover the Target TYPE and still emits
`mc.method = "deref_mut"` with `tag_trait = "DerefMut"`, which resolves to
nothing. So the fallback is a TYPE fallback, not a DISPATCH fallback, and the
comment describes a safety property the code does not have. Measured, three
programs, one diagnostic. That is its own defect and its own round.

## fldderefsite
site: src/compiler/sema_expr.cpp::lower_field_read
build: armed gate build 59 (unarmed baseline 56; probe batch of 2026-08-29c part 2)
measured: 2026-08-29
fires: 16
ceiling: 0
cost: 0
verdict: OBSERVATIONAL — and the number is the surprise: SIXTEEN
note: rule 9's outer half for `fldderefmut`. The field auto-deref loop is
  entered only when the receiver's own type LACKS the field, so its whole
  population across 361 ledger rows and the entire legal corpus is sixteen
  arrivals. `fldderefmut` fires on all sixteen.
  ⚠ RULE 4, AND IT CUTS BOTH WAYS HERE. A ceiling of 6 off a population of 16 is
  a weak bound on the SET — but it is also six rows for sixteen decisions, which
  is the densest ratio in this file. Read it as "this site is tiny and load-
  bearing", not as "six is the number".

## fldderefmut
site: src/compiler/sema_expr.cpp::lower_field_read
build: armed gate build 58 (unarmed baseline 56; probe batch of 2026-08-29c part 2)
measured: 2026-08-29
fires: 16
ceiling: 6
cost: 0 by the corpus, ⛔ NOT 0 — three hand-written legal programs die in mlir_gen
verdict: ⛔ NOT FUNDABLE AS SPELLED — the observation is right and "always mutable" is not the question to ask
note: THE SITE (B) ACTUALLY LIVES AT. The field auto-deref loop calls
  `emit_generic_deref_step(recv, /*want_mut=*/false)` — hardcoded, no parameter,
  no channel from the use context — while its METHOD sibling forty lines up
  computes `target_method_wants_mut_self(probe_target, m)` and asks. So `&mut
  b.f` on a `Box<S>` crosses `Box::deref`'s SHARED `&S`, and every consumer
  downstream of `cross()` sees a `&` where the program wrote `&mut`.
  PREDICTED ceiling 0 (an over-refusal repair closes no admit row). CLOSED SIX:
  deref-field-pattern-ref-suggestion-issue-146995, issue-81365-2, -3, -4--d2,
  -4--rd2, -8. predicted∖closed = ∅ trivially; closed∖predicted = all six.
  ⚠ THE PREDICTION WAS WRONG IN AN INSTRUCTIVE DIRECTION. Choosing `deref_mut`
  does not only stop refusing — it changes what `cross()` records from `Ref` to
  `MutRef`, which lets EXCLUSIVITY questions downstream be asked at all. Five of
  the six also close under the deposit mechanism; one (146995) does not.
  ⚠ COST 0 IS FALSE — see the part-2 header. `C: Deref` without `DerefMut`
  produces a `deref_mut` call that resolves to nothing and mlir_gen drops the
  statement. THE CORRECT SPELLING must ask TWO questions the crude one skips:
  is the field access in a mutable-use position (the method sibling's question),
  and does the receiver type actually impl `DerefMut` (the question the tree's
  own comment wrongly assumes `emit_generic_deref_step` already answers).

## callidxfdm
site: src/compiler/borrow_check.cpp::extract_borrow_place + src/compiler/sema_expr.cpp::lower_field_read
build: armed gate build 57 (unarmed baseline 56; probe batch of 2026-08-29c part 2)
measured: 2026-08-29
fires: 363
ceiling: 13
cost: 0 by the corpus; inherits `fldderefmut`'s three hand-written refusals
verdict: THE COMPOSITION — 13 rows, the blocker GONE, and a new blocker one layer down
note: hop (reference-returning calls) + `index_in_chain` deposit + DerefMut for a
  match scrutinee + DerefMut for a field auto-deref, all in one process.
  THREE SITES, ONE NAME, declared: 363 = 346 (`callindexchain`) + 1
  (`matchderefmut`) + 16 (`fldderefmut`), each measured separately in this same
  build. The sum decomposes exactly.
  PREDICTED 12 BY NAME (callindexchain's 13 minus borrowck-borrow-overloaded-
  auto-deref, on the reasoning that that row closes only through the E0596
  over-refusal this probe removes). CLOSED 13. predicted∖closed = ∅;
  closed∖predicted = {deref-field-pattern-ref-suggestion-issue-146995}. The
  re-opening was predicted and happened; the replacement was not.
  ⚠ THE BLOCKER OF 2026-08-28 IS GONE, MEASURED BY HAND on both programs:
      match *x { Cycle::Node(ref mut y) => … }   rc 1 under callroot/callrootref/
        callindexchain/callidxcallonly · rc 0 under callidxdm and callidxfdm
      let r: &mut i64 = &mut b.f;  (Box<S>)      rc 1 under callindexchain and
        callidxdm · rc 0 under callidxfdm (6 fires — the site is reached)
  and `b.f = 2i64`, `&v[0]`/`&v[1]`, a direct `&c.t.tf` field path and
  `it.next()` in a loop all stay rc 0.
  ⚠ AND THE NEW BLOCKER IS ONE LAYER DOWN, not in this mechanism: the three
  `Deref`-without-`DerefMut` programs that `fldderefmut` kills in mlir_gen.

## ⇒ THE ONE MECHANISM TO FUND, AND WHAT IT COSTS TO MAKE CORRECT

`callidxcallonly` — the hop through a REFERENCE-RETURNING `Call`/`MethodCall`
setting `index_in_chain`, which routes the place to the AddrOfTemp arm that
already records unconditionally. 13 rows, corpus cost 0, and the narrow spelling
(no AddrOfTemp) is set-identical to the wide one so the autoref population is
excluded for free. It is the DEPOSIT, it is one flag, and the reader it feeds
already refuses the same shape when no deref is involved.

IT MAY NOT BE LANDED ALONE. On its own it refuses `&mut b.f` on a `Box`, and
that is a legal-program refusal. Its PREREQUISITE is now located and priced,
which is the difference between this round and the last two:
  (1) the field auto-deref step must take the mutable step in a mutable-use
      position — the question `target_method_wants_mut_self` already asks for
      methods, asked for fields;
  (2) `emit_generic_deref_step` must not emit a `deref_mut` call for a type with
      no `DerefMut` impl. Its call site's comment claims it already falls back;
      it falls back on the TARGET TYPE only, and three hand-written programs die
      in mlir_gen because of it.
Neither is a borrow-check change. Both are in sema, both are small, and (2) is a
defect with no ledger row and no fixture that is worth its own round regardless
of whether the 13 are ever bought.

## callidxcallonly-LANDED — the funded mechanism, and the three sema repairs it cost
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: 51ec320220e5e558 (fixed tree; probe baseline 56, fixed gate builds 61/62)
measured: 2026-08-29
fires: n/a — the mechanism is LANDED, not armed; `callsite` (1132) is its outer half
ceiling: 13 measured as a probe
cost: 0 by the corpus, and ⛔ NOT 0 by hand — three legal programs, all repaired
verdict: FUNDED. PREDICTED TWELVE BY NAME, CLOSED TWELVE, both diffs ∅.
note: THE PREDICTION AND THE RESULT, as sets. The probe's 13 were
  `callindexchain`'s set; the fix was predicted to close 12 of them — all but
  `borrowck-borrow-overloaded-auto-deref` — BEFORE the gate ran, on the reading
  that that row is `&mut p.y` on an `Rc<Point>` and rustc refuses it ONLY
  because Rust's `Rc` has no `DerefMut`, while `stdlib/lang/rc/rc.logos:232`
  gives Logos's one under the comment "Rust parity". Closed set, measured:
    borrowck-no-cycle-in-exchange-heap--move-while-refmut-borrowed
    borrowck-overloaded-index-and-overloaded-deref--t15
    cannot-borrow-index-of-hashmap-in-for
    issue-81365-2 · -3 · -4--d2 · -4--rd2 · -8 · -10 · -11
    issue-81365-9--explicit-deref-call-borrow-then-write
    issue-81365-9--g-method-call-deref
  predicted∖closed = ∅; closed∖predicted = ∅. Ledger 361 -> 349.
  ⚠ A CEILING BOUNDS THE COUNT, NOT THE SET (rule 6), and here the count moved
  too: 13 -> 12. The row the fix does NOT close is the one the probe closed for
  a reason the fix removes — the E0596 over-refusal — which is exactly what the
  part-2 report predicted for `callidxfdm` and why `callidxfdm` and
  `callindexchain` had the same SIZE and different SETS.
  ⚠ AND THE FIX IS NARROWER THAN THE PROBE IN A SECOND PLACE. `fldderefmut`'s
  extra row, deref-field-pattern-ref-suggestion-issue-146995, is NOT closed:
  it is `let val: NonCopy = w.field;`, a MOVE OUT OF A DEREF, and it closed
  under the crude probe only because an always-mutable step changes what
  `cross()` records. Its defect is a missing move-out-of-deref check and it
  stays in the ledger under its own name. Rule 7, in the usual direction.

  WHAT LANDED, three sites, one question:
  (1) borrow_check `extract_borrow_place` — hop a Call/MethodCall/AddrOfTemp
      whose type IS a reference to its receiver/arg0, and set `index_in_chain`
      for the Call/MethodCall half (NOT AddrOfTemp: that autoref population
      broke liblogos-lang on 2026-08-27 and is excluded for free, since the
      narrow spelling was set-identical to the wide one).
  (2) sema `lower_field_read` + `lower_deref` — the auto-deref step is MUTABLE
      exactly in a mutable-use position, carried by `mut_place_ctx_`, set by
      `&mut <field place>` and by a `match *x` scrutinee whose arms bind
      `ref mut` (`arms_bind_ref_mut`, which reuses the tree's one pattern
      walker rather than growing a second).
  (3) sema `emit_generic_deref_call` — a `want_mut` step on a type with no
      `DerefMut` impl DEGRADES to the shared step instead of emitting a
      `deref_mut` call that resolves to nothing.

  ⚠ (2) AND (3) ARE NOT OPTIONAL EXTRAS, THEY ARE THE PRICE. Without them the
  hop refuses legal Rust: `let r: &mut i64 = &mut b.f;` on a `Box<S>` and
  `match *x { Cycle::Node(ref mut y) => … }` on a `Box<Cycle>`, both measured
  rc 1 with (1) alone and rc 0 with all three. Eleven mechanisms have been
  declined on the legal-refusal rule; this is the first round that paid it off
  instead of declining, because the report named the sites instead of the
  symptom.

  THE CONTROL, and it is the reason the cost claim is not rule-5 bait again:
  the three new `fail` fixtures were compiled on a build with `src/compiler`
  reverted and the fixtures in place — no diagnostic on any of them, i.e. all
  three were ADMITTED before this change and are refused after. So each legal
  twin's one-token sibling proves the new rule REACHES it:
    bc_field_deref_mut_borrow (pass)      ⟷ bc_field_deref_mut_not_mut (`let b`)
    bc_match_deref_mut_refmut_arm (pass)  ⟷ bc_match_deref_mut_not_mut (`let x`)
    bc_field_deref_mut_borrow (pass)      ⟷ bc_field_deref_no_deref_mut (Deref,
                                             no DerefMut — the (3) reach proof)
  plus bc_call_hop_disjoint_ok, the cost side: a write through the field
  auto-deref, a shared field borrow across a disjoint read, two disjoint index
  borrows, and a shared user-`Deref` borrow across a disjoint field read.
  ⚠ THE SHELL LIED IN THE CONTROL RUN and the number reported here is the one
  that was actually measured: `printf '%s rc=%d' "$(basename $f)" "$?"` expands
  the command substitution FIRST, so every `rc=` printed 0 whatever the
  compiler did. The diagnostic column of that same run — a grep over the
  captured stderr — is what says "admitted", and it is empty for all three.

  TWO DIAGNOSTICS WERE RE-PINNED, and neither is a weakening. `borrowck-issue-
  14498--box-mut-ref` and `--b-write-through-shared` asserted "cannot assign to
  a place behind a `&` reference", the ANONYMOUS branch of the DerefWrite guard,
  which is taken only when the place has no root. The hop supplies the root, so
  they now print "cannot assign to 'y': 'y' is behind a `&` reference" — the
  same refusal, naming the place. The guard's own comment predicted this ("the
  walk breaks at the user-Deref call and loses the root") and has been corrected.

  LEFT OPEN, NAMED:
  · `borrowck-borrow-overloaded-auto-deref` — its row now says its real cause is
    `impl DerefMut for Rc<T>`. Rust does not have it, deliberately: an `Rc` is a
    SHARED owner and `&mut` through it aliases every other handle. That is a
    stdlib/divergence question, not a borrow-check one.
  · deref-field-pattern-ref-suggestion-issue-146995 — move out of a `Deref`.
  · `fldderefmut` and `matchderefmut` remain armable as the WIDER spellings of
    (2): mutable step whatever the use context. `callroot` remains armable as
    the widening of (1): hop a call that does NOT return a reference.
  · The mutable-use positions that (2) does NOT arm: a write LHS (`b.f = …`
    still crosses the shared step and is admitted by other means), a compound
    assignment, `&mut b.f[i]`. One notion, deliberately under-armed at the
    edges, each edge named here rather than discovered later.

---

# CLASS C, 2026-08-29 — THE 15 "SIGNATURE-REGION" ROWS ARE NOT REGION ROWS

The survey that grouped class C called its largest unclaimed block "closure
SIGNATURE REGIONS, 15 rows — the error is at the CALL, not in the body", and the
standing advice was that if they need region inference they are not fundable
(measured: over 162 `lifereg` programs, 0 of 91 named lifetime regions ever gets
a CFG point, and 46.7M RegionInferer analyses produced 57 conflicts and ONE
pinned refusal). SETTLED BY HAND FIRST, before a probe was written, on
f41cb31ce. Six programs, one token apart:

    fn id(x: &i64) -> &i64 { return x; }
    fn get() -> &i64 { let l: i64 = 5i64; return id(&l); }             REFUSED
    fn get() -> &i64 { let l: i64 = 5i64;
        let c = |x: &i64| -> &i64 { return x; }; return c(&l); }       ADMITTED
    let r = id(&l);                       l = 6i64;                    REFUSED
    let c = |x:&i64|->&i64{return x;};  let r = c(&l);  l = 6i64;      ADMITTED
    let p = (|x:&i64|->&i64{return x;},);  return p.0(&z);             ADMITTED
    let fp: fn(&i64)->&i64 = id;           return fp(&l);              REFUSED

The fn spelling refuses, the fn-POINTER spelling refuses, the closure spelling
admits. Nothing about a region separates them and nothing about indirection
does either — a fn pointer is as indirect as a closure. The discriminator is a
NODE KIND: a closure call is `Code::ClosureCall`, and the arms that answer
"what does this reference name" enumerate call kinds BY SPELLING.

⚠ AND THE FIRST SITE PRICED FOR IT WAS THE WRONG ONE. See `capargtie` below.
The §B6 source walk (`collect_ref_sources_paths`) has the same hole in the same
words, it fires, arming it changes verdicts — and it closes NOTHING, because the
dangling-RETURN gate reads `prov_of`, a different walker. A hypothesis can be
right about the defect and wrong about the site, and the fire count cannot tell
you: `capargtie` fired 21 times and cost 2 legal programs while buying 0 rows.

⚠ MEASUREMENT HYGIENE, PAID FOR TWICE BEFORE THIS FILE SAID IT: THE HAND
PROGRAMS MUST BE MULTI-LINE. The NLL last-use scan is LINE-KEYED, so a whole fn
body written on one line collapses every last-use to one point and the answers
invert. Two of the six above read the opposite way in their single-line form and
were believed for twenty minutes.

## capargtie
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths
build: e7259149c5f64564 (gate-db 64 unarmed -> 65 armed)
measured: 2026-08-29
fires: 21
ceiling: 0
cost: 2
verdict: ⛔ RIGHT DEFECT, WRONG WALKER — the §B6 channel is not what the return gate reads
note: the `case EC::ClosureCall/FnPtrCall` arm leaves by its capture list
  (`if (caps) { …; return; }`) and never looks at the ARGUMENTS; the FnPtrCall
  tail walks them only when `caps` is null and a ClosureCall has no arg walk at
  all. Arming the missing walk closed ZERO ledger rows and refused two legal
  programs (`bc_esc_fnptr_admit`, `bc_esc_fnptr_param_admit`) — and BOTH costs
  are the FnPtrCall half, which already has a summary-aware walk that this crude
  one duplicated without the summary. PROVEN LIVE AND STILL SILENT: armed by
  hand on the closure twin of the dangling-return program the fire count was
  **0** — the arm is not on that path — and on the loan-conflict twin it fired
  once and the verdict did not move, because the consumer of a §B6 source is not
  the loan reader. Rule 1 gives you "the site was reached"; it does not give you
  "the site is on the path from THIS defect to THIS diagnostic", and only
  arming the probe on the hand program answers that. 21 fires over the whole
  ledger + legal corpus is also a rule-4 population.

## caphopclo
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: e7259149c5f64564 (gate-db 64 unarmed -> 66 armed)
measured: 2026-08-29
fires: 6
ceiling: 0
cost: 0
verdict: the LOAN-channel twin of the same hole — real, and rule 4 says 6 is not a population
note: the call hop that landed on 2026-08-29 enumerates `MethodCall | Call |
  AddrOfTemp`, so `&c(&v).f` and `*c(&v) = 1` still lose the root the way
  `&v[0]` did before it. The shape exists; the corpus has SIX arrivals of it.
  Not refuted, not fundable: a ceiling off a population of 6 is rule 4.

## capclosbox
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths
build: e7259149c5f64564 (gate-db 64 unarmed -> 67 armed)
measured: 2026-08-29
fires: 194
ceiling: 0
cost: 0
verdict: A NULL RESULT THROUGH A BROKEN CHANNEL — and this time the break is MEASURED
note: `lifereg_closurestore` measured this arm at CEILING 0 / 49 fires on the
  379-row ledger. That zero was suspect for a structural reason: the arm makes a
  closure binding a §B6 source of its captures, and the next hop — `return
  Box::new(f)` / `self.bar = Box::new(f)` passing `f`, a value of
  `Kind::Closure`, to a Call whose per-arg filter asks is_plain_ref_kind /
  is_borrow_carrying_type / forms_borrow_at_call / the summary — answers NO to
  all four. So `capclosbox` armed the deposit AND its consumer under one name.
  Still 0. THE PROOF THAT THE CHANNEL IS BROKEN A THIRD TIME IS `capclosarg`,
  which arms the consumer ALONE: **NEVER FIRED, 0 arrivals**. The Call arm's
  ENTRY gate — `type_may_carry_borrow(e.type(pool))` on a result of type
  `Box<dyn Fn…>` — answers NO, so no closure-typed argument in the entire
  corpus ever reaches the per-arg filter. Three hops, three breaks. The E0373
  block ("a closure value outlives a borrow it captured", ~8 rows) is NOT bought
  by supplying the source; it needs the closure TYPE to be borrow-carrying, and
  that is a type-predicate change with its own blast radius, not a probe.
  ⚠ ONE NAME, TWO SITES: `capclosbox`'s 194 is the SUM of ClosureBox arrivals
  and (zero) Call arrivals, which is exactly `rootkeep`'s defect. Only
  `capclosarg`'s separate name separated them.

## capclosarg
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths
build: e7259149c5f64564 (gate-db 64 unarmed -> 68 armed)
measured: 2026-08-29
fires: 0
ceiling: —
cost: —
verdict: NEVER FIRED — and that is the round's most useful zero
note: see `capclosbox`. A never-fired probe is normally an unreadable result;
  here it is the MEASUREMENT, because the question it was written to answer was
  "is the consumer reachable at all". It is not.

## capretcaps (RE-PRICED, rule 8)
site: src/compiler/borrow_check.cpp::walk_closure_body
build: 51ec320220e5e558 (gate-db 62 unarmed -> 63 armed)
measured: 2026-08-29
fires: 3674149
ceiling: 2
cost: 0
verdict: the 2026-08-28 claim of 2 SURVIVES the ledger's 371 -> 349 shrink, and now its SET is named
note: measured at 371 rows as ceiling 2 with no row list recorded. Re-priced
  against 349: still 2, still cost 0, and the two are
  `borrowck/issue-53432-nested-closure-outlives-borrowed-value` and
  `nll/nested-bodies-in-dead-code`. A ceiling decays; this one did not.

## capprovarg / capprovnocap / capprovcaps
site: src/compiler/borrow_check.cpp::prov_of
build: 91952ac05596d7d8 (gate-db 69 unarmed -> 70 / 71 / 72 armed)
measured: 2026-08-29
fires: 11420 / 11420 / 11428
ceiling: 6 / 6 / 0
cost: 0 / 0 / 0
verdict: ✓ THE MECHANISM, AND THE NARROW HALF IS THE WHOLE OF IT — ONE LINE, SIX ROWS, COST 0
note: `prov_of`'s `case Code::ClosureCall/FnPtrCall` has TWO permissive exits
  and the arguments are read by NEITHER:

      if (!caps && e.kind() == Code::FnPtrCall) { …walks the args… }
      if (!caps) return {};                     // ← capture-less closure: SILENT
      RefProv merged = {};
      for (auto& cap : *caps) { … }             // ← captures only, args never merged
      return merged;

  `note_closure_caps` ERASES the entry when the capture list is empty, so a
  closure that captures nothing is indistinguishable here from a callee this
  walker cannot name, and takes the permissive answer. The repair is DELEGATION:
  give the capture-less ClosureCall the rule the FnPtrCall branch three lines up
  already applies.
  DECOMPOSED ON PURPOSE (rule 6 — a ceiling bounds the count, not the set):
  `capprovnocap` (the capture-less exit alone) closes THE SAME SIX ROWS as
  `capprovarg` (both exits), and `capprovcaps` (the caps loop alone) closes
  ZERO at 11428 arrivals. The widening buys nothing at a larger blast radius —
  the same verdict the closure BODY walk reached about `move` arms.
  ⚠ AND THE ZERO HAS A MECHANISM, not just a number: the caps exit is ALREADY
  maximally conservative. `ce5` below — a CAPTURING closure whose body returns
  its own parameter, legal Rust — is refused on the UNPATCHED tree with "cannot
  return reference to local variable 'k'", `k` being the capture. Adding an arg
  tie to an exit that already answers is_local for every captured local cannot
  move a verdict. That over-refusal is the tree's today and is not this round's.

  PREDICTED BY NAME BEFORE THE RUN, and diffed BOTH ways:
    predicted, closed:  borrowck/anonymous-region-in-apit--ctl-return-channel
                        borrowck/cannot-return-ref-to-fn-param-in-filter-map
                        nll/issue-48697--b
                        nll/promoted-closure-pair
                        regions/regions-ret-borrowed-1
    predicted (hedged), NOT closed:
                        regions/regions-ret-borrowed — `return f(&3i64)`; the
                        argument is a const-promoted temp, so `prov_of` answers
                        {} by design (#92) and there is nothing to tie.
    closed, NOT predicted:
                        nll/check-normalized-sig-for-wf — root `nllmoves.A`,
                        not a class-C row at all: `fn whoops<F>(s:&i64, f:F) ->
                        &'static i64 { return f(s); }`. The tie lands on a
                        PARAM and the elision gate refuses. A closure-call arg
                        rule is not a class-C rule; it is a rule about calls.

  RULE 5 — COST 0 IS NOT A SAFETY CLAIM. Six hand programs, each compiled
  unarmed and under `LOGOS_PROBE=capprovnocap` with `LOGOS_PROBE_FIRE` read, so
  every one is PROVEN TO HAVE REACHED the arm (0 fires unarmed, 8-9 armed):
    ce1 a param passed through a capture-less closure and returned  ADMITTED both
    ce2 the result used while the referent is alive                 ADMITTED both
    ce3 a const-promoted `&0i64` argument                           ADMITTED both
    ce4 a closure returning a SCALAR                                ADMITTED both
    ce5 a CAPTURING closure returning its own param   REFUSED BOTH (see above)
    ce6 the defect itself: `let c=|x:&i64|->&i64{…}; return c(&l);`
                                                     admitted unarmed, REFUSED armed
  RULE 7 — a crude probe and a correct fix do not close the same programs. ce6's
  diagnostic under the probe is "cannot return reference to local variable '?'":
  the tie reaches the ARGUMENT but the report site cannot recover the NAME `l`.
  A correct landing owes that name, and the six `.expected` files will pin it.

## THE RE-GROUPING, AND WHAT IS NOW UNCLAIMED
note: 52 rows carry a C root today (bck.C 25, nllmoves.C 18, lifereg.C 9), not
  43. Re-grouped by MISSING OBSERVATION rather than by the year-old survey's
  nine:
    A  the result of a call to a CLOSURE VALUE is tied to nothing
       → `capprovnocap`, CEILING 6 / COST 0, ONE LINE. FUND THIS.
    B  a closure VALUE does not carry its captures' provenance out of the fn
       (E0373 / E0521-escape, ~8 rows) → `capclosbox`: 0, and the reason is a
       THIRD broken hop that is now measured (`capclosarg` never fired).
    C  a closure BODY returning a ref to a BODY LOCAL → `capretcaps`, 2 / 0,
       re-priced today, set named.
    D  a capture-by-ref is not a loan in the ENCLOSING frame (closure-borrow-
       spans a/b, borrowck-closures-mut-and-imm, mut-borrow-conflict-in-
       closures-vec, issue-42574 b/t15, issue-51268, issue-40510-3,
       issue-101119) → `capshared` 3 claimed, `capmut` ⛔ 18/17. ~9 rows.
    E  move-vs-loan at the capture (borrowck-loan-blocks-move-cc r10/t10,
       borrowck-multiple-captures, issue-52663, issue-75904, borrowck-move-by-
       capture, region-bound-on-closure-outlives-call) → `capmoveloan` 1,
       `capescmove` 1. ~7 rows.
    F  ⚠ THE LARGEST UNCLAIMED BLOCK IS NOT THE SIGNATURE ONE. The closure BODY
       WRITES its own `&` parameter into a place that outlives the call:
         borrowck/issue-45983            `give_any(|y| { x = y; })`
         borrowck/regions-escape-bound-fn, -2
         nll/escape-argument--t09        `|q: &mut &i64, r: &i64| { *q = r; }`
         borrowck/borrowed-data-escapes-closure-148392
         borrowck/anonymous-region-in-apit--closure-param-escapes
         borrowck/issue-7573             `|installed| { lines.push(installed); }`
       SEVEN ROWS, no probe, and it is NOT region inference either: the missing
       observation is a closure-body flow summary in the WRITE direction, the
       thing the ClosureCall arm's own comment has been asking for since
       2026-08-28 ("the repair is a flow summary for a closure BODY; it is its
       own round and it is not priced yet"). That is the next round's question.
    G  GENUINELY REGION, and therefore not fundable today: closure-substs and
       nll/issue-58053 (`-> &'static` from a param), return-wrong-bound-region
       (`for<'a>`), regions/regions-escape-method. FOUR rows — not fifteen.
  So of the survey's "15 signature-region rows": FIVE are bought by one line of
  argument tying (the sixth row `capprovnocap` closes is `nllmoves.A`, outside
  class C entirely), FOUR are genuinely region, and the rest were never one
  block — they are group F, a WRITE-direction question wearing a signature's
  clothes. "Not fundable today because it needs region inference" would have
  been the honest answer to fifteen rows and is the honest answer to four.

## capprovnocap-LANDED — the funded mechanism, and the narrowing the probe bought
site: src/compiler/borrow_check.cpp::prov_of
      (the ClosureCall capture-less exit inside it)
build: e0bbe6a8d4fb8328 (fixed tree; probe baseline 69, armed 71, fixed gate 73)
measured: 2026-08-29
fires: n/a — LANDED, not armed. Its reach is proved by a ONE-TOKEN TWIN
  (pass/bc_h4e_closure_arg_tie_param `c(p)` ⟷ fail/bc_h4e_closure_arg_tie_dangle
  `c(&l)`) and by a CONTROL REVERT, below.
ceiling: 6 measured as a probe (11 420 arrivals at the site)
cost: 0 by the corpus at build 73 — 1823 `-L bc`, 852 `-L bc -L pass`, 190
  spec/ownership/advanced, 745 L1, 12 684 generated cases, all green — and
  ⛔ NOT 0 for the PROBE by hand: ce7, one legal program, which is why what
  landed is not what was armed.
verdict: FUNDED. PREDICTED SIX BY NAME, CLOSED SIX, both diffs ∅. Ledger 349 -> 343.

THE DEFECT. `prov_of`'s `case ClosureCall/FnPtrCall` has two permissive exits and
the call's ARGUMENTS were read by neither. `if (!caps) return {}` answered
NOTHING for a capture-less closure — `note_closure_caps` ERASES the entry when
the capture list is empty, so such a closure is indistinguishable there from a
callee the walker cannot name — and the caps loop that follows merges captures
only. The FnPtrCall branch three lines up already walked its args. So the same
program refused through a fn, refused through a fn POINTER, and was ADMITTED
through a closure: the discriminator was a node kind, not a region.

THE CLOSED SET, measured at build 73, and the prediction was made before it ran:
    borrowck/anonymous-region-in-apit--ctl-return-channel   'local'
    borrowck/cannot-return-ref-to-fn-param-in-filter-map    'line'
    nll/check-normalized-sig-for-wf                         (elision, see below)
    nll/issue-48697--b                                      'z'
    nll/promoted-closure-pair                               'z'
    regions/regions-ret-borrowed-1                          'three'
  predicted∖closed = ∅; closed∖predicted = ∅. `regions/regions-ret-borrowed` was
  predicted NOT to close and did not: its argument is the const-promoted `&3i64`,
  for which `prov_of` answers `{}` by design (#92).
  ⚠ ONE OF THE SIX IS NOT CLASS C. `check-normalized-sig-for-wf` is nllmoves.A —
  `fn whoops<F>(s:&i64,f:F)->&'static i64 { return f(s); }` — where the tie lands
  on a PARAM and the ELISION gate refuses it, not the dangling gate. A rule about
  closure-call ARGUMENTS is a rule about calls, and it reaches outside the class
  that motivated it. Rule 6 the other way round: the count held and the SET was
  larger than the class.

⚠ WHAT LANDED IS NARROWER THAN THE PROBE (rule 7), AND THE PROBE IS HOW THAT WAS
FOUND. `capprovnocap` merged EVERY reference argument into the result. Nine hand
programs were compiled against it before a line of the fix was written, each
proved to have REACHED the arm by its fire count (8-10 armed, 0 unarmed), and
one of them is refused by the probe and is LEGAL:
    ce7  fn get(p:&i64)->&i64 { let l:i64=5;
             let c=|x:&i64,y:&i64|->&i64{return y;}; return c(&l,p); }
The result derives from `y` alone; the tie to `&l` is a legal-program refusal,
and A LEDGER ROW MAY NOT BE BOUGHT WITH ONE. So what landed is the LANGUAGE'S OWN
elision rule instead of "merge the arguments": with exactly ONE reference-typed
argument the result can only borrow THAT one, and the answer is EXACT rather than
conservative. All six rows pass exactly one reference, so the narrowing is free —
ceiling 6 survived it intact.

⚠ AND THE TWO-ARGUMENT CLOSURE IS A NAMED RESIDUE, NOT AN OVERSIGHT. With two or
more reference arguments there is no elision rule to apply. The tree refuses to
let a FN even be WRITTEN in that shape — measured, tw1/tw3: E0106, "more than one
input lifetime and no `&self`" — and a closure has no syntax to annotate the tie:
`|x:&i64,y:&'b i64|->&'b i64` parses and is read blanket-wise anyway (tw7, rc 0).
The precisely-annotated FN twin is refused for an unrelated reason (tw5: "variance
mismatch — expected &'b i64, got &'a i64"), which is the inert lifetime channel
again. So ce7 STAYS ADMITTED. It is a hole; it is NOT pinned as a green pass
fixture, because a green test asserting a defect is the thing this file exists to
stop. Its repair is either an E0106 for closure signatures or a per-closure
`to_result` mask — which is the same "flow summary for a closure BODY" that the
loan channel's ClosureCall arm has been asking for since 2026-08-28, and it would
buy group F as well. That is the next round's question and it now has two callers.

RULE 7, THE NAME. The probe printed "cannot return reference to local variable
'?'" — §B6's `collect_ref_sources` has no ClosureCall arm and answers nothing for
the whole expression. A landing owes the name, so the report site now asks §B6
about the ARGUMENTS. MESSAGE ONLY, and deliberately not a repair of §B6 itself:
`capargtie` armed that idea INSIDE `collect_ref_sources_paths` and priced CEILING
0 / COST 2, because that walk feeds verdicts other than this one. All five
dangling rows now name their local, pinned in full in their `.expected`.

THE CONTROL, because cost 0 was wrong twice in the round before this one: with
`src/compiler/borrow_check.cpp` REVERTED and all nine fixtures in place, every one
compiled with NO diagnostic — the six relanded imported programs and
bc_h4e_closure_arg_tie_dangle were ADMITTED before and are refused after, and the
two pass fixtures were legal on both trees. The two halves of the pair are one
token apart (`c(p)` / `c(&l)`), which is how reach is proved for a landed rule
with no fire log.

⚠ THE CAPTURING EXIT WAS LEFT ALONE ON A MEASUREMENT, not by omission:
`capprovcaps` priced that widening at CEILING 0 over the whole ledger, and it has
a mechanism and not just a number — the caps loop already answers `is_local` for
every captured local, so ce5 (a CAPTURING closure returning its own param) is
refused on the unpatched tree today and no argument tie can move it.

⚠ AND `capprovnocap` RE-PRICED ON AN UNCHANGED TREE READS "NEVER FIRED", which is
not rule 1 firing. LOGOS_PROBE is in the build key, so the armed build id was
already fully measured in the store from the pricing round; gate-run correctly ran
nothing, so no logosc process existed to append to the fire log. A fire count of
zero is only readable when tests actually RAN. The ceiling was re-read instead as
`gate_db.py compare 69 71`, which is the same six rows.

## fpsrc
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 76 armed)
measured: 2026-08-29
fires: 2213384
ceiling: 3
cost: 0
verdict: ✓ THE GROUP-F ANSWER, AND IT IS ONE LINE AT AN ARM THAT ALREADY EXISTS
note: THE MISSING OBSERVATION, established by a ONE-VARIABLE CONTROL rather than
  by reading code — the same store, once through a block and once through a
  closure:
      { let t: i64 = 7i64; let y: &i64 = &t; x = y; }   REFUSED, E0597
      let c = |y: &i64| { x = y; }; c(&t);              rc 0
  The refusing half names `t`; the closure half says nothing at all. §B6's
  `case EC::VarRef` answers `ref_sources_under(n)` — a closure PARAMETER has no
  recorded sources, so a store of one into a place in the ENCLOSING frame
  deposits nothing and `pop_scope` has nothing to hang a dangle on. The rule:
  inside a closure body a parameter of KNOWN reference type is its own §B6
  source, because its referent is supplied at the call and does not outlive the
  enclosing frame.
  PREDICTED BY NAME BEFORE THE RUN, diffed BOTH ways:
    predicted, closed:  borrowck/issue-45983
                        borrowck/regions-escape-bound-fn-2
    closed, NOT predicted:
                        borrowck/issue-7573 — `|installed: &CrateId| {
                        lines.push(installed); }`. Predicted to need `fpprov`
                        because `note_holder_escape_prov` bails when the value's
                        provenance is neither local nor temp; it does not — the
                        §B6 MethodCall arm deposits the source on its own. The
                        prediction was wrong about the CHANNEL, not the row.
    predicted NOT to close, and did not: borrowck/regions-escape-bound-fn (the
                        stored binding is never used again, and §B6 reports only
                        at the first USE past the death — see `fpwrite`),
                        borrowck/anonymous-region-in-apit--closure-param-escapes
                        (the destination is `qux`'s own PARAM, which
                        note_holder_escape_prov skips by #78/#138),
                        nll/escape-argument--t09 (`*q = r`: both ends are
                        closure params and die in the SAME frame, so pop_scope's
                        `dying.count(binding)` skips it),
                        borrowck/borrowed-data-escapes-closure-148392 (a `move`
                        closure — `walk_closure_body` returns at its first line).
  RULE 5 — COST 0 IS NOT A SAFETY CLAIM. Seven hand programs, each compiled
  unarmed (0 fires) and armed (1572-1639 fires), so every one is PROVEN to have
  reached the arm — five legal, two defective:
    ce1 param stored into a body-local ref                       ADMITTED both
    ce2 `acc = *y` — a deref COPY into an outer local            ADMITTED both
    ce3 an outer place assigned a borrow of an outer local       ADMITTED both
    ce4 `let c = |y| { x = y; }; c(&w);` — UNANNOTATED param,
        legal Rust (the region is inferred, not higher-ranked)   ADMITTED both
    ce4b the same UNANNOTATED shape with a genuinely dying
        referent — rustc REFUSES, we admit                       ADMITTED both
    f_clo_let    `let c = |y:&i64| { x = y; }; c(&t); *x`   admitted, REFUSED
    f_clo_param  the same store reached through a generic
                 `give_any<F: FnOnce(&i64)>` bound          admitted, REFUSED
  ⚠ THE GATE IS `is_ref_kind(param type)` AND THAT IS THE RUST RULE BY ACCIDENT,
  not by design. issue-45983's closure is written `|y| { x = y; }` — unannotated
  — and still closes, because `give_any`'s `F: FnOnce(&i64)` bound RESOLVES the
  parameter type; ce4's identical spelling has no bound and the type is not a
  ref kind at this point, so it is admitted. Bound-driven ⇒ higher-ranked ⇒
  refuse; inference-driven ⇒ one region ⇒ admit. That is exactly rustc's split
  and this probe reproduces it through the type, not through the binder. ce4b is
  the residue: an inference-driven closure that genuinely dangles stays admitted.
  A landing owes the split as a STATED rule and the ce4/ce4b pair as its pin.

## fpprov
site: src/compiler/borrow_check.cpp::prov_of
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 77 armed)
measured: 2026-08-29
fires: 504476
ceiling: 0
cost: 0
verdict: the ESCAPE-FACT half of group F buys NOTHING — 504 476 arrivals, and the site is the right one
note: `prov_of`'s VarRef arm answers `{{name}, false}` — "a parameter, therefore
  outliving" — for a CLOSURE parameter exactly as for a fn parameter. Making it
  answer is_local instead is the same claim `fpsrc` makes, written in the escape
  channel rather than in the §B6 source channel. It closes NOTHING, and `fpboth`
  (both arms armed at once) closes `fpsrc`'s three rows and not one more — same
  COUNT and same SET, so this is not two errors cancelling. The consumer of an
  is_local provenance is the RETURN gate, and `check_return_value` is
  hard-suppressed inside a closure body; the consumer of a §B6 source is
  `pop_scope`, which runs at the closure body's own scope exit. Only one of the
  two channels is even awake in there.
  Same shape as `capprovcaps` last round: the decomposition, not the ceiling,
  is the result.

## fpboth
site: src/compiler/borrow_check.cpp::closure_param_names_
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 80 armed)
measured: 2026-08-29
fires: 2717865
ceiling: 3
cost: 0
verdict: fpsrc ∪ fpprov = fpsrc, in COUNT and in SET
note: borrowck/issue-45983, borrowck/issue-7573,
  borrowck/regions-escape-bound-fn-2 — byte-identical to `fpsrc`'s list. Run
  because a ceiling bounds the count and not the set: two probes closing three
  rows each could have been closing different threes.

## fpwrite
site: src/compiler/borrow_check.cpp::record_ref_sources
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 78 armed)
measured: 2026-08-29
fires: 2306063
ceiling: 4
cost: 0
verdict: ✓ fpsrc's three PLUS regions-escape-bound-fn — and the fourth row costs a NEW REPORT SITE
note: `fpsrc` deposits a source and lets §B6 report at the first USE past the
  referent's death. rustc reports E0521 AT THE WRITE, and the difference is
  exactly one ledger row: `regions-escape-bound-fn` is
  `with_int(|y: &i64| { x = Option::Some(y); });` where `x` is NEVER READ again,
  so there is no use for §B6 to report at. `fpwrite` adds the direct refusal —
  in a closure body, an assign whose destination is not declared inside that
  body and whose value's §B6 sources include a closure parameter.
    predicted, closed:  borrowck/issue-45983
                        borrowck/regions-escape-bound-fn
                        borrowck/regions-escape-bound-fn-2
    closed, NOT predicted: borrowck/issue-7573 — through the `fpsrc` arm this
                        probe shares, not through the new report site.
    predicted∖closed = ∅.
  Same seven hand programs as `fpsrc`, same reach proof (1636-1639 fires armed,
  0 unarmed), all seven unchanged — ce1-ce4b ADMITTED, both defect twins
  REFUSED (with the E0521-shaped message instead of the E0597 one).
  ⚠ THE +1 IS NOT FREE. The destination test is "not in `closure_body_decls_`",
  a CONTEXT-level stand-in for "this place's region outlives the closure's
  parameter region", and it needs a diagnostic site that does not exist today.
  `fpsrc` routes the same fact through machinery that already knows drop order,
  slots and shadowing (F5/F6) and already prints the right sentence. Three rows
  at one line versus four rows at a new report site.

## tmcbdyn
site: src/compiler/borrow_check.cpp::type_may_carry_borrow
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 79 armed)
measured: 2026-08-29
fires: 10872879
ceiling: 3
cost: 0
verdict: ✓ THE ROUND'S FINDING — group B is NOT a closure defect, it is TWO NOTIONS OF ONE CONCEPT
note: `type_hides_borrow` (the RETURN gate's predicate) lists the erased-payload
  kinds verbatim — TraitObject, UnsizedDyn, Closure, ImplTrait — with its own
  note saying "a Ref or an erased dyn/closure sits inside an owned wrapper, and
  is_borrow_carrying_type answers no". `type_may_carry_borrow`, the predicate
  every OTHER gate in the file asks, has never had them. So the return gate
  knows an erased payload can hide a borrow and no other gate does.
  ⚠ AND THE THREE ROWS IT CLOSES ARE NOT THE ROWS THE HYPOTHESIS WAS ABOUT.
  Predicted 0 (both downstream hops were expected shut); closed 3, and TWO
  CARRY A DIFFERENT ROOT:
    borrowck/do-not-suggest-adding-move-move                    bck.C
    lifetimes/issue-55796--r09b                                 lifereg.N1
    regions/regions-close-param-into-object--b-object-dangles   lifereg.L5
  The last two contain no closure at all: `Box<dyn It>` holding `&self.v`
  assigned to an outer binding and used after the owner dies, and
  `Box::new(Holder{r:&local})` returned through a generic `erase<T: X>`.
  predicted∖closed = ∅ only because the prediction was ZERO; closed∖predicted is
  the whole set. Rule 6 in its sharpest form so far: the count was predicted
  right by accident of being wrong about everything.
  RULE 5, and this is the half it does NOT yet satisfy: ce6 (`keep(Box::new(move
  || n))`) and ce7 (`fn mk() -> Box<dyn Fn() -> u64> { let k = 11u64; return
  Box::new(move || k); }`) are both legal, both ADMITTED armed and unarmed, and
  both proven reached (7711/7712 fires armed, 0 unarmed). TWO hand programs
  against a predicate with 10 872 879 arrivals and ~20 read sites is not a
  safety argument. The corpus says 0 across 1823 `-L bc`, 852 `-L bc -L pass`,
  190 spec/ownership/advanced; the hand set does not yet reach the other read
  sites, and a landing owes one counter-example per site that consumes it.

## bxsrc
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 81 armed)
measured: 2026-08-29
fires: 10873777
ceiling: 4
cost: 0
verdict: tmcbdyn's three PLUS the row group B was named for — THREE HOPS, and hop 1 is now MEASURED
note: RULE 11, WALKED BY HAND BEFORE ANYTHING WAS PRICED, with the existing
  binary and the already-committed `lifereg_closurestore`:
      let c: || -> u64;  c = || -> u64 { return *r; };
                                     armed: 1 fire, REFUSED (E0597, names `r`)
      let b: Box<dyn Fn() -> u64>;  b = Box::new(|| -> u64 { return *r; });
                                     armed: 0 fires, admitted
  One variable — the erasing wrapper. `lifereg_closurestore`'s CEILING 0 / 49
  fires on the 379-row ledger was a NULL RESULT THROUGH A BROKEN HOP, and the
  broken hop is §B6's `case EC::Call` entry gate,
  `if (type_may_carry_borrow(e.type(pool)))` on `Box<dyn Fn…>` — the same
  predicate answering the same "no" that `capclosarg` measured at the
  ClosureCall per-arg filter yesterday. THREE SITES, ONE PREDICATE.
  The three hops, each armed under this one name: (1) `tmcbdyn`'s kinds in
  `type_may_carry_borrow`; (2) a ClosureBox NODE admitted by the Call per-arg
  filter, which rejects it by TYPE (`is_plain_ref_kind` no,
  `is_borrow_carrying_type` no, `forms_borrow_at_call` no); (3) the ClosureBox
  arm of the §B6 walk, which exists but is `lifereg_closurestore`-gated.
    predicted, closed:  borrowck/unconstrained-closure-lifetime-generic--control-escape-to-outer-local
    closed, NOT predicted: the three `tmcbdyn` rows (see there).
    predicted (hedged), NOT closed: borrowck/unconstrained-closure-lifetime-
                        generic--min-capture-escapes-to-field — `self.bar =
                        Box::new(…)`, whose root `self` and whose sources `f`/`r`
                        all die in ONE frame, so pop_scope skips it exactly as
                        escape-argument--t09 is skipped;
                        borrowck/borrowck-escaping-closure-error-1 — a RETURN,
                        read by `prov_of_retained`'s ClosureBox arm
                        (`capescape` / `capescmove`), not by §B6 at all.
  Counter-examples: ce6, ce7 ADMITTED armed; b3_boxlocal (the same
  `Box::new(|| *r)` with holder and referent in ONE frame, legal) ADMITTED
  armed; b1 (the ledger row's shape) REFUSED armed, admitted unarmed — a
  one-scope twin, and it names `r`.

## bxhold
site: src/compiler/borrow_check.cpp::note_holder_escape_prov
build: 98f66c0aebc5cc5d (gate-db 75 unarmed -> 82 armed)
measured: 2026-08-29
fires: 92520
ceiling: 0
cost: 0
verdict: the SECOND site of the same asymmetry is NOT load-bearing — 92 520 arrivals, and a mechanism for the zero
note: `note_holder_escape_prov`'s gate is
  `!holder_ty || !type_may_carry_borrow(holder_ty)`; widening it with the
  return gate's own `type_hides_borrow` closes nothing. The mechanism, not just
  the number: what this deposit WRITES is `prov_[name]`, and the only consumer
  of `prov_[name]`'s escape bits is `check_return_value`. A holder that is
  ASSIGNED an erased closure and then merely USED — which is every group-B store
  row — never reaches a return gate, so opening the deposit gate deposits into a
  channel nobody reads for these programs. The store rows are §B6's, and §B6 is
  where `bxsrc` closes them. ⚠ Two notions of one concept, and widening the
  WRONG one of the two narrow sites buys nothing: the site matters as much as
  the predicate.

## GROUP F IS THREE MECHANISMS, NOT ONE — AND GROUP B IS NOT ABOUT CLOSURES
note: the seven rows the 2026-08-29 re-grouping put in F were read as one
  question ("a closure-body flow summary in the WRITE direction, handed back to
  the call site"). Compiled by hand, multi-line, they are three:
    F1  a closure PARAM stored into a place in the ENCLOSING frame
        — issue-45983, regions-escape-bound-fn, regions-escape-bound-fn-2,
          issue-7573 (through `Vec::push`'s out-param), and
          anonymous-region-in-apit--closure-param-escapes (through `bar`'s).
        MISSING OBSERVATION: a closure parameter is not a §B6 source.
        ⚠ NO CALL-SITE SUMMARY IS NEEDED FOR ANY OF THEM. The whole fact is
        visible inside the body, at the store. Four of the five close under
        `fpwrite`; the fifth (anonymous-region-in-apit) does not, because its
        destination is the enclosing fn's own `&mut` PARAM and
        note_holder_escape_prov skips params by #78/#138 — task #78, still open,
        and NOT a closure question.
    F2  a closure param stored THROUGH another closure param — nll/escape-
        argument--t09, `|q: &mut &i64, r: &i64| { *q = r; }`. ONE row. Both ends
        are parameters of the same closure and die in the same frame, so no
        scope-exit reader can see it; this one really does need the call-site
        summary, and it is the only row in F that does.
    F3  a `move` closure writing a borrow of its OWN ENV into a moved-in capture
        — borrowck/borrowed-data-escapes-closure-148392. ONE row.
        `walk_closure_body` returns at `if (cbv.is_move()) return;`, so no body
        rule of any kind reaches it. Its two ends (`a` and `b`) are both main's
        locals in one frame, so §B6 could not see it even if the body were
        walked. Not priced; it is a third question.
  And group B ("a closure VALUE does not carry its captures out of the fn"):
  the defect is real but it is NOT closure-shaped. `type_may_carry_borrow` does
  not know that an ERASED payload can hide a borrow, and two of the three rows
  `tmcbdyn` closes contain no closure at all — they are `Box<dyn Trait>` holding
  a `&`. The closure rows are the subset of the erasure rows whose payload
  happens to be a closure.
  ⇒ THE ONE MECHANISM TO FUND: `fpsrc`. Three rows, ONE line, at an arm that
  already exists, with a diagnostic that is already correct and already names
  the local; cost 0 by the corpus and by six hand programs that all reached it.
  `tmcbdyn` is the more INTERESTING result and is the runner-up on purpose: 3
  rows across 3 roots for one predicate line, but 10.87M arrivals across ~20
  read sites and only two hand counter-examples — rule 5 is not met for it yet,
  and the way to meet it is one counter-example per consuming site, not more
  corpus. The two are DISJOINT (F1 rows vs erasure rows) and can land in either
  order.

## fpsrc-LANDED — F-1, and the TWO narrowings the probe bought
site: src/compiler/borrow_check.cpp::collect_ref_sources_paths (VarRef arm)
      + names_live_closure_param + closure_param_frame_
build: 064f209b2e5760d6 (gate-db 83 first measure, 84 after the control round-trip)
measured: 2026-08-29
fires: n/a — LANDED, not armed. (Field added 2026-08-29e: the record asserted a
  landing in its verdict but carried no `fires:` line, and the log lint could not
  see the omission because its heading carries a title. No number is invented
  here — "not armed" is what the record already says.)
ceiling: 3   predicted: 3   closed: 3   cost: 0
verdict: ✓ LANDED. CEILING = PREDICTED = ACTUAL, as a SET, both diffs empty.

THE MISSING OBSERVATION, in one sentence: §B6 asks
`collect_ref_sources_paths` what a value borrows, and for a reference bound by
a CLOSURE PARAMETER the answer was NOTHING — a parameter is not a `let` and
never went through `record_ref_sources`, so `ref_sources_under` had no record
to find. `x = y` inside a closure body with `x` in the enclosing frame
therefore deposited no source, and `pop_scope` had nothing to find dying. That
is E0521, "borrowed data escapes outside of closure".

    predicted, closed:  borrowck/issue-45983
                        borrowck/issue-7573
                        borrowck/regions-escape-bound-fn-2
    predicted∖closed = ∅        closed∖predicted = ∅
  Every one names its local in the sentence — `y`, `installed`, `y` — because
  the fact is spent through F5/F6's existing scope arithmetic and the
  diagnostic that already prints there. No `'?'`.

⚠ WHAT LANDED IS NARROWER THAN THE PROBE, TWICE, AND BOTH NARROWINGS ARE
LEGAL PROGRAMS THE PROBE REFUSED. This is the third round running in which
COST 0 over the whole corpus was not a safety claim, and the second in which a
hand program found the refusal the corpus could not contain.

  (1) A SET OF STRINGS CANNOT SAY WHICH BINDING A NAME DENOTES. `fpsrc` keyed
      on `closure_param_names_.count(n)`. ce5, multi-line, compiles on every
      tree before this round and is refused by the probe:

          give(|y| {
              let y: &i64 = &z;     // SHADOWS the parameter
              x = y;                // stores a borrow of main's own `z`
          });

      `z` outlives every use of `x`, so this is legal and rustc accepts it. The
      probe emits `y` as a source at the SHADOW's slot, the shadow dies at the
      body's scope exit, and `x` is refused with E0597 — measured, rc=0
      unarmed and rc=1 armed, one build apart.
      THE FIX asks which FRAME declares the name instead. `visit_block` pushes
      its own scope for the body, so every body `let` lands strictly deeper
      than the parameter frame, and `names_live_closure_param` compares the
      innermost declaring frame against the frame recorded at parameter
      declaration. F5's `declared_slots` cannot decide it: a closure parameter
      is `declare_var(nm, NO_SLOT)` and NO_SLOT compares equal to everything.
      ⚠ AND THE FRAME TEST IS NOT "ANY SHADOW DISABLES THE RULE". A
      shadow-erases-the-name narrowing would have admitted ce15, where the
      shadow is confined to an inner block and the store BELOW it names the
      parameter again — a real E0521. The frame predicate refuses ce15 and
      admits ce5, and the two are one pair of braces apart. Both are pinned:
      pass/bc_f1_closure_param_shadow_legal and
      fail/bc_f1_closure_param_shadow_inner_block.

  (2) RECORDED SOURCES WIN. A parameter reassigned in the body (`y = &z;`)
      borrows what the assignment says, not itself; emitting `y` there would
      name the wrong binding and, when `z` outlives, refuse a legal program.
      So the parameter identity is consulted only when `ref_sources_under` is
      empty. This can only ADMIT more than the probe, never refuse more, and
      the ceiling survived it unchanged.

RULE 5 — TWELVE HAND PROGRAMS, ALL MULTI-LINE, EACH WITH ITS REACH PROVED.
The `fpsrc` arm fires on every VarRef arrival, so the stdlib floor is 1572
(measured with a four-line program that has no closure at all). A program is
proved to have reached the arm WITH A CLOSURE PARAMETER IN HAND by its count
above that floor:
    reached (+1): ce1 body-local holder · ce3 outer borrow stored outward ·
                  ce5 the shadow · ce6 the inference-driven closure ·
                  ce10 parameter into a struct literal · ce12 closure returns
                  its parameter through a body-local hop · ce13 holder in an
                  inner block
    reached (+2): ce11 nested closures, inner writes the OUTER parameter into
                  an OUTER-body binding · ce15 the inner-block shadow
    floor (+0):   ce2 `out = *y` · ce8 `*y = 11` through a `&mut` parameter ·
                  ce9 `out = twice(y)` — these three never reach the walk at
                  all, which is the right answer and a WEAK counter-example.
                  Recorded as such: they prove nothing about the rule.
Eleven of the twelve are ADMITTED by the landed rule; the twelfth is ce15,
which is a defect. Six of the reached-legal shapes are folded into
pass/bc_f1_closure_param_legal_shapes, asserting a VALUE (`exit: 0` gated on
the computed sum), not a diagnostic.

CONTROL REVERT, with all eight fixtures in place and ONLY
src/compiler/borrow_check.cpp back at HEAD: every one of the five fail
fixtures compiled at rc=0 with NO DIAGNOSTIC, and the three pass fixtures
compiled clean on both trees. The fail/pass pairs are one token apart (the
holder inside vs outside the body; the shadow inside vs outside a `{ }`),
which is how reach is proved for a landed rule with no fire log.

WHAT IS STILL OPEN, AND WHY EACH IS A DIFFERENT QUESTION (four F rows):
  · regions-escape-bound-fn — the holder is NEVER READ again, so §B6 has no
    use to report at. `fpwrite` (ceiling 4) buys it with a NEW report site
    whose destination test, "not in `closure_body_decls_`", is a context-level
    stand-in for a region question. Deliberately not bought.
  · anonymous-region-in-apit--closure-param-escapes — destination is the
    enclosing fn's own `&mut` PARAM; task #78, not a closure question.
  · nll/escape-argument--t09 — `*q = r`, both ends parameters of ONE closure
    in ONE frame. The only F row that needs a call-site write summary.
  · borrowck/borrowed-data-escapes-closure-148392 — a `move` body;
    `walk_closure_body` returns at its first line.
AND THE INFERENCE-DRIVEN RESIDUE, ce6: `let mut c = |y| { x = y; }; c(&z);`
with no trait bound anywhere stays ADMITTED, because the gate is the
parameter's TYPE. That is, by accident, exactly rustc's bound-driven vs
inference-driven split — an accident, not a proof. It has no imported row to
sit on and is deliberately NOT pinned as a green pass fixture.

⚠ `tmcbdyn` IS STILL THE RUNNER-UP AND IS STILL UNFUNDED, unchanged by this
round: the two sets are disjoint (F-1 closes closure-parameter rows, `tmcbdyn`
closes ERASED-PAYLOAD rows) and F-1 touched neither `type_may_carry_borrow`
nor any gate that asks it. Its ceiling of 3 and cost of 0 stand as recorded;
what it still lacks is one counter-example per consuming site, and 10.87M
arrivals over ~20 read sites is why that is the price and more corpus is not.

### ⚠ RULE 8 — THE THREE SURVIVING F PROBES DECAYED THE MOMENT F-1 LANDED
`fpsrc`'s edit is now the tree's behaviour, so its arm is gone from the probe
census (`grep 'probe::on("fp'` → `fpprov`/`fpboth` at prov_of, `fpwrite` at the
assign). The numbers recorded above were measured against a baseline that no
longer exists, and RE-PRICING THEM TODAY MEASURES THE INCREMENT, NOT THE ROW
COUNT:
  · `fpwrite` ceiling 4 → its `fpsrc` half landed, so what is left to buy is
    ONE row (`regions-escape-bound-fn`) at the cost of a new report site.
    Do not read "4" as four rows.
  · `fpboth` ceiling 3 → its `fpsrc` half landed and `fpprov` was already
    measured to add nothing, so armed today it should measure 0. If it does
    not, that is a finding.
  · `fpprov` ceiling 0 stands; nothing it touches moved.
`tmcbdyn` / `bxsrc` / `bxhold` are untouched — F-1 changed neither
`type_may_carry_borrow` nor any gate that asks it — but they were priced on
build 98f66c0aebc5cc5d against a 343-row ledger and the ledger is now 340. The
three rows that left are closure-parameter rows and none of them appears in
`tmcbdyn`'s or `bxsrc`'s recorded sets, so those two ceilings are expected to
hold at 3 and 4. EXPECTED, not measured — re-price before funding.

## tmcbsite — RULE 5, DISCHARGED SITE BY SITE, AND IT FAILS AT TWO
site: src/compiler/borrow_check.cpp::type_may_carry_borrow — all 28 consumers
build: b440b13e2eccc1a1 (READ; gate-db 85 unarmed -> 113 armed)
measured: 2026-08-29
fires: 6818652   arrivals 6818512 / flips 948 over the 1385-program COST population
ceiling: 3 (SET unchanged)   cost: 0 by corpus, **2 by hand**
verdict: ⛔ RULE 5 IS **NOT** MET FOR THE PREDICATE. It IS met for a FOUR-SITE
  subset that closes the SAME THREE ROWS, and the two guilty sites buy NOTHING.

### 1. THE SITE CENSUS — 28 CALLS, DERIVED, NOT GREPPED
`tools/dlog/tmcb_sites.dl` (new) asks the general C++ schema for every CallExpr
whose callee decl is `type_may_carry_borrow`, with its enclosing named context.
28 calls, `tmcb_unnamed` EMPTY (no call sits in a context the tool cannot name)
and `tmcb_nocall_ref` EMPTY (the predicate's address is never taken, so there is
no std::function hop a call-graph question would miss). Grep agreed on the
count only AFTER the three self-recursive calls were split out into `tmcb_walk`;
before that grep's 31 lines mixed calls with prose mentions. RE-DERIVE THE LINE
NUMBERS WITH THAT QUESTION — they are keyed to this commit and nothing else.

⚠ 20 WAS THE WRONG NUMBER. Yesterday's `tmcbdyn` note said "roughly 20 read
sites" and that estimate is what the round was refused on. It is 28, and two of
the 28 are not live at all (below), so the population that mattered was 26.

### 2. DIRECTION, AND THE INSTRUMENT THAT ANSWERS IT MECHANICALLY
A site is a RISK when a widened "yes" can newly reach a REFUSING branch. Rather
than argue direction per site, `type_may_carry_borrow` now takes the CALLER's
line as a default `__builtin_LINE()` argument and, under `LOGOS_TMCB_FLIP`,
records per site {arrivals, FLIPS} — a flip being a type the erased-payload
widening answers differently. The walk was split into `tmcb_walk(t, wide)` so
one process answers the same type BOTH ways; the flip is attributed to the
CONSUMER, never to the walk's own recursion. `LOGOS_PROBE_SITE=<line>[,…]`
then restricts the widening to named sites, so a refusal is attributed to ONE
consumer instead of to the predicate.
INERT, and proven so at the verdict level, not by argument: gate-db compare
84 -> 85 (pre-instrument vs instrumented, both unarmed) = 1387 tests measured
under both, **0 changed**.

### 3. THE CENSUS BY DIRECTION — arrivals/flips over 1385 LEGAL programs
(ledger 340 + `-L bc -L pass` 855 + spec/ownership/advanced 190; every one
COMPILES, which is why the flip column, not the cost column, is the evidence
that a site was reached at all.)

    line   consumer / arm                                arrivals  flips
    3578   collect_ref_sources_paths  MethodCall by_flow       495      2
    3633   collect_ref_sources_paths  FieldRead                658      3
    3652   collect_ref_sources_paths  TupleIndex                65      0
    3657   collect_ref_sources_paths  IndexRead                288     19
    3691   collect_ref_sources_paths  SliceIndex                 6      0
    3807   collect_ref_sources_paths  Deref                    161      0
    3951   collect_ref_sources_paths  Call ENTRY              5027     32
    4684   apply_flow_outparams       outparam escape           78      0
    6704   note_holder_escape_prov    own gate               92 954      3
    7090   bc_hop_roots               MethodCall arg           132      0
    7098   bc_hop_roots               Call arg                 669      5
    7114   bc_hop_roots               ClosureCall/FnPtrCall     75      0
    7151   bc_hop_roots               EnumLitData payload   225 333     12
    7157   bc_hop_roots               StructLit field         1417      0
    7163   bc_hop_roots               TupleLit elem             32      0
    7169   bc_hop_roots               ArrLit elem               95      0
    7841   prov_of                    MethodCall recv-carried   83      0
    7908   prov_of                    Call/MethodCall ENTRY 46 051     19
    8525   check_return_value         holds_gate          3 579 149    180
    8535   check_return_value         LOGOS_DUMP_RETGATE print   0      0
    8954   take_ref_borrows           holder hop            380 601    273
    9640   take_ref_borrows           MatchExpr scrut           12      0
    11908  visit_stmt                 Let escape record     591 955    331
    12580  visit_stmt                 derefwrite                 7      0
    12688  visit_stmt                 LetElse scrut             15      0
    12888  visit_stmt                 Match stmt scrut    1 892 772     67
    14336  visit                      recvstore                382      2
    14592  visit                      mexprpatloan-gated         0      0

  NOT A RISK, and MEASURED rather than assumed — both at **0 arrivals**:
    8535  is inside `if (std::getenv("LOGOS_DUMP_RETGATE"))`: a debug print, no
          branch, and never executed in 1385 programs.
    14592 is inside `if (logos::probe::on("mexprpatloan"))`: dead unless that
          probe is armed, which the §14592 note already records as
          UNMEASURABLE-HERE. Confirmed dead, not merely believed dead.
  RISK SITES: 26. Corpus flips at 13; ZERO flips at 13, and those thirteen are
  exactly what no amount of corpus could have spoken for.

### 4. ONE HAND COUNTER-EXAMPLE PER RISK SITE — 26/26 REACHED
All multi-line, all committed under `docs/probes/tmcbsite/` with `run.sh` (compiles
unarmed then `LOGOS_PROBE=tmcbdyn`, prints rc for both and site:arrivals/flips
from the ARMED run — a site downstream of a flipped gate is reachable ONLY
armed, which the first cut of the runner got wrong and under-reported).

    site   program        shape                                          verdict
    3578   ce7072         `w.keep(b)`, method result Box<dyn Give>        ADMITTED
    3633   ce3633         `h.b` FieldRead of a Box<dyn> field             ADMITTED
    3652   ce3636         `t.0` of `(Box<dyn Give>, i64)`                 ADMITTED
    3657   ce9622         IndexRead under Option<Box<dyn Give>>           ADMITTED
    3691   ce3677         `sl[0]` on a slice of NON-CAPTURING CLOSURES    ADMITTED
    3807   ce3791b        `*rc` where rc: &<closure>                      ADMITTED
    3951   adv7894        ⛔ **REFUSED** — see §5                          COST
    4684   ce4668         `v.push(b)`, Vec<Box<dyn Give>> out-param       ADMITTED
    6704   ce9622         holder-escape gate under Option<Box<dyn>>       ADMITTED
    7090   ce7076         method taking Box<dyn>, returning `&self.k`     ADMITTED
    7098   ce7098         free fn `keep(b: Box<dyn Give>, r:&i64)->&i64`  ADMITTED
    7114   ce7100         CLOSURE taking Box<dyn> and returning its &arg  ADMITTED
    7151   ce9622         EnumLitData payload Box<dyn Give>               ADMITTED
    7157   ce7143         `H { r: &n, b: bx }` (H is holds_any_ref)       ADMITTED
    7163   ce7145         `(bx, 1i64)` TupleLit                           ADMITTED
    7169   ce7151         `[bx]` ArrLit                                   ADMITTED
    7841   ce7827c        ⛔ **REFUSED** — see §5                          COST
    7908   adv8511        `fn mk() -> Box<dyn Give>` from a local         ADMITTED
    8525   adv8511        the return gate on the same program             ADMITTED
    8954   ce3636         take_ref_borrows holder hop                     ADMITTED
    9640   ce9626         `let r:&i64 = match ob {…}` , ob:Option<Box<dyn>> ADMITTED
    11908  smoke          `let b: Box<dyn Give> = Box::new(Hold{r:&n})`   ADMITTED
    12580  ce12566        `*s.cell() = bx`, cell(&mut self)->&mut Box<dyn> ADMITTED
    12688  ce12684        `let Some(bb) = ob else {…}`                     ADMITTED
    12888  adv12874       `match ob { Some(bb) => return bb, … }`          ADMITTED
    14336  ce4668         recvstore on `v.push(b)`                        ADMITTED

  ⚠ TWO SITES NEEDED A NON-OBVIOUS SHAPE, and saying so is the point:
  * 3691 (SliceIndex) is reachable ONLY through a slice of NON-CAPTURING
    CLOSURES. A flip needs an ELEMENT type of erased kind; `Box<dyn T>` is a
    Move type, and the only spelling that reaches this arm (`o = sl[0]`, a read
    OUT of a slice) is then a move out of a borrow. A non-capturing closure is
    the one erased kind that copies. Three Box-shaped attempts (ce3675/b/c) all
    landed on the ArrLit arm instead — the site was NOT skipped, it was reached
    by changing the erased kind, not the spelling.
  * 7841 needs a GENERIC receiver: `Cell<Box<dyn Give>>`. `type_may_carry_borrow`
    does not walk a named struct's FIELDS, so a plain `struct Cell { b: Box<dyn
    Give> }` never flips; only a type ARGUMENT does. The arm's own entry gate
    (`if (!plain && !fat && !m_bc) return {}`) also demands a REF-shaped result,
    which is why three earlier `-> Box<dyn Give>` attempts got 0 arrivals.

### 5. THE TWO REFUSALS — LEGAL PROGRAMS, ATTRIBUTED TO ONE SITE EACH
Attribution is `LOGOS_PROBE_SITE=<one line>` over all 26: each program is
refused by EXACTLY ONE site and admitted by the other 25.

  (a) site 3951 — `collect_ref_sources_paths`, the §B6 Call ENTRY gate.
      This is the site `bxsrc` was built on.

        fn mkb(n: &i64) -> Box<dyn Give> {
            return Box::new(Sq { s: *n });
        }
        fn main() -> i32 {
            let h: Box<dyn Give>;
            {
                let x: i64 = 6i64;
                h = mkb(&x);
            }
            let v: i64 = h.get();
            return v as i32;
        }

      `mkb` COPIES `*n` into an owned `Sq`; the returned box holds no borrow.
      Unarmed rc 0. Armed: E0597, "'x' does not live long enough: it is borrowed
      by 'h'". The widened entry gate says the Call's RESULT may carry a borrow,
      so the arm walks the arguments and deposits `x` as a §B6 source of `h`.
      A `Box<dyn Trait>` result is exactly as opaque about its arguments as it
      is about its payload — the widening makes the gate assume the one and it
      silently assumes the other.

  (b) site 7841 — `prov_of`, #86 SUB-SITE C, "the borrow the receiver CARRIES".

        struct Cell<T> { t: T }
        impl<T> Cell<T> {
            fn thru(&self, r: &i64) -> &i64 {
                return r;
            }
        }
        fn pick(c: Cell<Box<dyn Give>>, r: &i64) -> &i64 {
            return c.thru(r);
        }

      `thru` returns its ARGUMENT and its EXACT summary says so, so
      `recv_contributes` is false and every earlier clause is (rightly) skipped.
      The widening opens sub-site C anyway, `carried_prov_of_recv(c)` answers
      is_local, and the result adopts the RECEIVER's locality:
      "cannot return reference to local variable 'c'". Unarmed rc 0.
      ONE-VARIABLE CONTROL, `ce7827ctl`: the same program with `Cell<i64>` —
      site 7841 reached (1 arrival), NO flip, ADMITTED armed. The only
      difference is the erased payload in the receiver's type ARGUMENT.
      ⚠ This is the F2 over-refusal the arm's own comment says cannot happen
      ("`Id{z:0}` carries no borrow at all, so type_may_carry_borrow is false
      for it and this clause never opens"). That sentence is TRUE of the
      predicate as it stands and FALSE the moment it learns about erasure. The
      note is load-bearing and the widening invalidates it.

### 6. THE CEILING AS A SET, RE-PRICED (rule 8) AND DECOMPOSED (rule 6)
`tmcbdyn`, re-priced at ledger 340 (was 351 when 3 was first recorded):
CEILING **3**, COST 0, and the SET IS UNCHANGED, name for name:
    logos_00_bc_admit_borrowck_do-not-suggest-adding-move-move
    logos_00_bc_admit_lifetimes_issue-55796--r09b
    logos_00_bc_admit_regions_regions-close-param-into-object--b-object-dangles
  predicted∖closed = ∅   closed∖predicted = ∅.
  `fires` moved 10 872 879 -> 6 818 652 and NOTHING about the mechanism changed:
  the count is now one per TOP-LEVEL call instead of one per recursion level,
  because the widening moved from the recursive body into `tmcb_walk`'s `wide`
  parameter. It now equals the arrival census (6 818 512 over the cost
  population), which is the number that should have been quoted all along.

⚠ **A CEILING IS NOT ADDITIVE OVER SITES.** Per-site ceiling, all 26 armed one
at a time against the 340-row ledger: **3578 closes 1; every other site closes
0.** Sum = 1, whole = 3. Delta-debugged to the minimal set PER ROW:
    issue-55796--r09b                              {3578}
    do-not-suggest-adding-move-move                {8525, 11908}
    regions-close-param-into-object--b-object-dangles  {7908, 8525}
Two of the three rows need a PAIR of sites and are invisible to any one of
them. A per-site sweep that read only the single-site column would have
reported this whole mechanism as ceiling 1 and killed it.

### 7. THE FOUR-SITE SUBSET — SAME THREE ROWS, AND RULE 5 IS MET FOR IT
    LOGOS_PROBE_SITE=3578,7908,8525,11908
    CEILING = 3   the SAME THREE ROWS   COST = 0 over 1385 legal programs
    and it ADMITS ALL 32 HAND PROGRAMS, ce7827c and adv7894 included.
The two guilty sites are priced alone and buy NOTHING:
    3951 alone: CEILING 0, COST 0 by corpus — and refuses adv7894.
    7841 alone: CEILING 0, COST 0 by corpus — and refuses ce7827c.
So the cost is not a price paid for the rows; it is paid at sites that
contribute nothing to them. ⚠ AND BOTH GUILTY SITES PRICED **COST 0** ON 1385
LEGAL PROGRAMS. That is the fourth round running in which COST 0 was not a
safety claim, and the first in which the corpus said 0 at a site it had
FLIPPED 32 TIMES (3951). A flip count is proof the site was REACHED; it is not
proof the site was reached by the shape that breaks.

### 8. VERDICT
RULE 5 is **NOT met** for `type_may_carry_borrow`. It fails at
`collect_ref_sources_paths`' Call ENTRY gate and at `prov_of`'s #86 sub-site C.
It IS met, against 26 site-attributed hand programs and 1385 corpus programs,
for the four-site subset {MethodCall by_flow, prov_of Call entry,
check_return_value holds_gate, visit_stmt Let escape} — which closes the same
three rows across the same three roots.
NOT LANDED HERE. What a landing owes beyond this measurement: the four sites
are still spelled by LINE, and a landing must spell them by ARM; and rule 7
says a crude probe and a correct fix do not close the same programs — the three
rows' diagnostics under the subset have not been read.

### 9. LANDED 2026-08-29 — THE FOUR-SITE SUBSET, SPELLED BY ARM
The two things §8 said a landing still owed are paid here, and both changed the
result. Ledger **340 -> 337**, build hash READ not guessed.

**(a) BY ARM, NOT BY LINE.** `LOGOS_PROBE_SITE` names call sites by
`__builtin_LINE()`, which any edit above them invalidates — and the census
instrument stays in the tree, so the numbers in §1-§7 are still readable. What
LANDED is a second named entry beside the predicate:

    bool type_may_carry_borrow_erased(TypeRef t) const { return tmcb_walk(t, true); }

and exactly four arms call it. The mapping from §1's line census to the arm:

    3578   collect_ref_sources_paths  EC::MethodCall, the `by_flow` entry gate
    7908   prov_of                    EC::Call, the §B6 door
    8525   check_return_value         `holds_gate`
    11908  visit_stmt                 the #86 Let SUB-SITE 2

The other 24 consumers keep the narrow predicate. `type_may_carry_borrow` is
unchanged for them, `tmcbdyn` still arms all 28, and the two over-refusals of
§5 are still reachable under it — deliberately, so the residue keeps a probe.
Adding a FIFTH caller of the erased entry is a measurement, not an edit: it
re-opens rule 5 at a site no counter-example has discharged.

**(b) RULE 7 BIT, AND IT WAS THE THIRD ROW'S DIAGNOSTIC.**
`regions-close-param-into-object--b-object-dangles` refused with

    error [fn make]: cannot return reference to local variable '?': dangling reference

'?' IS NOT A NAME. `return erase(h);` is an `EC::Call`, and §B6's
`collect_ref_sources` has no answer for a Call whose result is erased — because
the widening that WOULD supply it is `collect_ref_sources_paths`' Call ENTRY,
i.e. site **3951**, the site that refuses `adv7894`. The name cannot be bought
there. It is recovered at the REPORT site instead, mirroring the H4-e
ClosureCall arm already three lines above it: ask the Call's ARGUMENTS for their
sources, fall back to the argument's own name. MESSAGE ONLY — the verdict is
already made when this runs, so the §5 cost cannot be re-incurred through it.
The row now reads `local`, which is the `let` that actually dies.

**PREDICTED vs ACTUAL, as SETS.** Predicted 3 BY NAME before the edit;
closed exactly those three. predicted∖closed = ∅, closed∖predicted = ∅.

    logos_00_bc_admit_borrowck_do-not-suggest-adding-move-move          bck.C
    logos_00_bc_admit_lifetimes_issue-55796--r09b                       lifereg.N1
    logos_00_bc_admit_regions_regions-close-param-into-object--b-object-dangles  lifereg.L5

**COST, re-measured on the landed tree:** 0. `-L bc` 1840 passed / 0 failed,
`-L bc -L pass` + the spec/ownership/advanced selection 190 passed / 0 failed,
and ALL 36 hand programs in docs/probes/tmcbsite/ compile rc=0 — `adv7894` and
`ce7827c`, the two the blanket widening refused, included. Those two are the
whole reason the landing is four arms and not one predicate.

**CONTROL REVERT**, all nine fixtures in place and only borrow_check.cpp back at
`c4faa921e`: every one of the SIX fail fixtures compiled rc=0 with NO
diagnostic, and all three pass twins compiled clean on both trees. Restored
byte-identical before the final gates.

**WHAT IS STILL OPEN.** 22 of the 28 consuming sites have a counter-example but
no landing, because they buy nothing measurable; the two that are *known* to
over-refuse (3951, 7841) are named above and stay narrow. `escape-argument--t09`
still needs a call-site write summary, `anonymous-region-in-apit--closure-param-
escapes` is task #78, `borrowed-data-escapes-closure-148392` is a `move` body,
and `ce4b`'s inference-driven residue is unchanged. ⚠ RULE 8: `tmcbdyn`'s
ceiling of 3 DECAYED to 0 the moment this landed — its rows are the tree's
behaviour now, and re-arming it measures only the increment from the other 24
sites, which is what the two known over-refusals sit in.

---

## GROUP B SURVEYED BY MISSING OBSERVATION — 34 ROWS, SEVENTEEN QUESTIONS

`bck.B` 21 + `nllmoves.B` 13, compiled BY HAND, multi-line, each against a
one-variable control. The block's own gloss is "bookkeeping is ROOT-keyed and
does not follow a projection". **Eighteen of the 34 are that; sixteen are not.**
The partition below is by what the checker would have to OBSERVE, and the
control that isolates it is quoted for every group. Row counts sum to 34.

**B-1 — MOVE OUT OF A REFERENCE AT A *PATTERN*. 5 rows.**
  `bind_pattern` is handed the scrutinee's TYPE, never its EXPRESSION, so
  `is_unowned_move_source` — the one predicate for "this place does not own what
  it yields" — cannot be asked at any of the four pattern sites.
  · scrutinee IS the unowned source (3): borrowck-move-error-many-places--move-
    out-of-ref-in-match, --r-runtime, borrowck-move-error-with-note--a.
    `patmoveref` (in the tree since 4bdbfe94e, never recorded here) — RE-PRICED
    below: ceiling 3, **cost 2**, so STOP as spelled.
  · the PATTERN does the deref, `match r { &q => … }` (2): do-not-suggest-
    removing-wrong-ref-pattern-issue-132806, issue-99470-move-out-of-some.
    MEASURED: `patmoveref` fires ZERO on these — the scrutinee is `r`, a `&NC`,
    which is Copy, so `is_move_type(scrut_type)` is false before the source test
    is reached. A `&`-pattern needs its own arm; not probed this round.
  CONTROL (one variable, the deref moved from the `let` to the pattern):
      let q = *r;                      → REFUSED E0507, always
      match *f { Foo1(a, b) => … }     → ADMITTED, refused under patmoveref
      match r  { &q => … }             → ADMITTED under patmoveref too

**B-2 — THE DESTRUCTURE TEMP DISCARDED THE PATTERN'S MOVE-NESS. 2 rows.**
  access-mode-in-closures, move-errors--d. `deref_move_exempt` exemption (4),
  already priced as `destrmove` (ceiling 2, COST 1 — its own paired control).
  Its comment already names the fix as a sema change. Unchanged this round.

**B-3 — THE PLACE WALK BREAKS AT A USER `Deref` CALL. 2 rows.**
  deref-field-pattern-ref-suggestion-issue-146995 (a user `impl Deref for Wrap`),
  issue-52086 (`Rc<Bar>`). CONTROL, one variable — the wrapper:
      let x: &Bar  = &b;  eat(x.field);   → REFUSED E0507
      let x = rc_new(Bar{…}); eat(x.field); → ADMITTED
  `rcexempt`, `callroot`, `callrootref`, `dwnoidx`, `dwatunwrap` all armed on
  the second: NONE fires. This is `callroot`'s family (empty `bp.root` after a
  Call hop) and its blocker is sema's deref-mode selection, recorded there.

**B-4 — E0509, MOVE OUT OF A `Drop` TYPE. 1 row. RETIRED FROM THE QUEUE.**
  borrowck-move-error-with-note--b. `fldmovedrop`'s note settles it: the Logos
  spec DELIBERATELY admits this (`@rule intrinsic.drop.skip-moved-out-paths`,
  25_spec_pass_intrinsic_1). Funding it is a DESIGN decision (PAIR), not a
  checker round. It is not fundable by anyone this week.

**B-5 — AN ARRAY-PATTERN BINDING RECORDS NO MOVE. 3 rows.**
  borrowck-move-out-from-array-match, --use-match--b, --use-match--t13.
  `slicepatnull` ceiling 3 / COST 6 (four are spec rules). STOP as spelled;
  the correct spelling asks the SCRUTINEE for the element type and rule 7 says
  it will not close the same three.

**B-6 — A PARTIAL MOVE IS NEVER ASKED AT A BORROW. 1 row.**
  moves-based-on-type-match-bindings. See `addrofpart` / `borrowpart` below:
  the observation is REAL and CONFIRMED by hand, the site the previous round
  named for it is WRONG, and the ledger row needs a SECOND mechanism as well.

**B-7 — `visit()`'s `TupleIndex` ARM DOES NO BOOKKEEPING AT ALL. 1 row.**
  move-out-of-tuple-field. Two lines against `FieldRead`'s ~140. See
  `tupidxmove`: ceiling 1, COST 0.

**B-8 — A `ref` / `ref mut` PATTERN BINDING IS NOT A TRACKED BORROW-HOLDER. 2 rows.**
  borrowck-issue-2657-1, issue-27282-mutation-in-guard. CONTROL, one variable —
  the binding spelling, everything else identical:
      let y = &x;                 let a = x;   → REFUSED "cannot move 'x' while borrowed"
      match x { Some(ref y) => { let a = x; } } → ADMITTED
      let foo = &mut o; let a = foo; let b = foo; → REFUSED "already mutably borrowed"
      match o { ref mut foo => { let a = foo; let b = foo; } } → ADMITTED
  `propagate_pat_borrows` raises the loan at all three match sites already; what
  is missing is that the MOVE/reborrow side does not see it. Not probed.

**B-9 — A GUARD'S VIEW OF THE SCRUTINEE IS SHARED-ONLY. 1 row.**
  match-guards-always-borrow. Nothing anywhere restricts a pattern binding
  inside a guard. A new observation, 1 row; not probed.

**B-10 — A CALL RESULT THAT CARRIES A BORROW INSIDE AN AGGREGATE
INHERITS NO LOAN. 2 rows.** issue-85581, borrowed-mut-pointer-assign-overflow-
off. THE SHARPEST CONTROL IN THE SURVEY — four programs, one variable, the
RESULT TYPE, bodies otherwise identical:
      fn mk(r:&mut i64) -> &mut i64          → REFUSED "cannot use 'x' while … borrowed"
      let s = S { pointer: &mut x }          → REFUSED (the LITERAL, in-frame)
      fn mk(r:&mut i64) -> S{pointer:r}      → ADMITTED
      fn mk(r:&mut i64) -> Option<&mut i64>  → ADMITTED
  MECHANISM, read not guessed: TWO gates ask `is_borrow_carrying_type`, which is
  the `#[borrow_carrying]` ATTRIBUTE set plus a type-arg walk — a plain user
  struct with a `&mut` field is NOT in it — where the question is the STRUCTURAL
  "does this value hold a loan", i.e. `type_may_carry_borrow`. Two notions of
  one concept, at two new sites. See `aggcallloan` / `aggletroute` / `aggboth`.

**B-11 — A `&mut` OF AN IMMUTABLE ROOT REACHED THROUGH A HOP. 3 rows.**
  · borrow-immutable-deref-box, --c-mut-borrow-deref-box: `nomutskip`,
    COST 2 legal programs. STOP — recorded there.
  · borrowck-access-permissions--b-mut-borrow-of-static: `mutstaticborrow`,
    ceiling 2 COST **0**, and THE PROBE IS NO LONGER IN THE TREE while the row
    is still open. RE-PRICED below; it holds, name for name.

**B-12 — A PARAMETER CARRIES NO `mut` BIT. 1 row.**
  borrowck-ref-mut-of-imm--ref-mut-of-imm. CONTROL, one variable, local vs param:
      let x: Option<i64> = …;   match x { Some(ref mut v) … } → REFUSED "'x' not declared as mut"
      fn f(x: Option<i64>)      { match x { Some(ref mut v) … } } → ADMITTED
  This is G1b, already named at `nomutskip` (`param_names_` exempts 98.7% of
  1,061,549 arrivals). It needs the sema bit `recvmutbind`, which does not exist.

**B-13 — AN INDEX WRITE THROUGH A USER `Index` IS NOT A MUTABLE USE. 1 row.**
  borrowck-loan-vec-content. CONTROL, one variable — the container:
      let e = &a[0]; a[1] = 4;   (array) → REFUSED "cannot assign through 'a[..]'"
      let e = &v[0]; v[1] = 4;   (Vec)   → ADMITTED
      let e = &v[0]; v.push(9);  (Vec)   → REFUSED
  Same family as B-3: the write's place is reached through a Call.

**B-14 — PATH-KEYED vs ROOT-KEYED READERS — THE ONLY ROWS THAT ARE
LITERALLY WHAT `B` SAYS AND ARE CHEAP. 2 rows.**
  borrowck-move-from-subpath-of-borrowed-path (`fldrootbits`) and issue-82032
  (`recvfieldpath`). Both RE-PRICED below on the 337-row ledger: ceiling 1,
  COST 0, sets unchanged. Both probes are still in the tree, UNFUNDED.

**THE SIXTEEN THAT DO NOT BELONG IN B**

**X-1 — `'static` IN A TYPE ANNOTATION IS NOT A CONSTRAINT ANYWHERE. 3 rows.**
  adt-brace-enums, issue-46036, lub-match. CONTROL — the annotation site does
  not matter, which is the finding:
      struct Foo { x: &'static i64 }  Foo { x: &a }    → ADMITTED
      let f: &'static i64 = &a;                        → ADMITTED
  These are region rows (`lifereg`-shaped), not bookkeeping rows.

**X-2 — A WRITE THROUGH A `&mut &T` DOES NOT REACH THE POINTEE. 2 rows.**
  capture-ref-in-struct--ctl, --t08. CONTROL, one variable — the indirection:
      p = &y;                       → REFUSED E0597, names `y`
      q = &mut p;  *q = &y;         → ADMITTED
  This is `escape-argument--t09`'s question (a call-site write summary), which
  is already named as the one F row that needs it. Two more rows sit on it.

**X-3 — THE HOLDER IS NEVER READ AGAIN. 1 row.**
  regions-escape-unboxed-closure. CONTROL, one token — a later use of `x`:
      { let t = 5; x = Some(&t); }              → ADMITTED
      { let t = 5; x = Some(&t); } let _q = x;  → REFUSED E0597, names `t`
  §B6 has no use to report at. Identical to `regions-escape-bound-fn`, whose
  new report site `fpwrite` was deliberately not bought. ⚠ RULE 8: that makes
  `fpwrite`'s remaining prize TWO rows, not the one recorded at fpsrc-LANDED.

**X-4 — plus B-4 (1, a spec DESIGN decision), B-5 (3, a pattern TYPE carrier),
B-8 (2) and B-9 (1, the pattern LOAN channel), B-12 (1, a sema `mut` bit), and
issue-51117 (1) — the ergonomic default-binding-mode loan, which
`propagate_pat_borrows` excludes with a MEASURED reason in its own comment
(modes 3/4 need the loan keyed on the POINTEE; recording it on the local red
25_spec_pass type_3 and type_8). 10 rows.**

**WHAT THIS SURVEY CHANGES.** Twelve of the 34 already had a priced mechanism
from an earlier round and the label hid it; three of those price at COST 0 and
were never funded (`fldrootbits`, `recvfieldpath`, `mutstaticborrow`). Sixteen
rows are not bookkeeping-through-a-projection at all, and six of those (X-1,
X-2, X-3) need machinery that does not exist and are RETIRED from the class-B
queue and named for the block that owns them.

---

## patmoveref — RE-PRICED (rule 8), AND ITS COST HAD NEVER BEEN RECORDED
site: src/compiler/sema_stmt.cpp::lower_match
build: eca91795fcce2717 (READ; gate-db 116 unarmed -> 117 armed)
measured: 2026-08-29
fires: 549
ceiling: 3
cost: 2
verdict: ⛔ STOP AS SPELLED — a ledger row may not be bought with a legal-program refusal
note: recorded in 4bdbfe94e's commit message as "patmoveref 4" against the
  447-row ledger and never entered here, so no reader could see that its COST
  was UNMEASURED. On the 337-row ledger it closes THREE, and the set is exactly
  the three B-1a rows this survey predicted BY NAME before the run:
    borrowck_borrowck-move-error-many-places--move-out-of-ref-in-match
    borrowck_borrowck-move-error-many-places--r-runtime
    borrowck_borrowck-move-error-with-note--a
  predicted∖closed = ∅   closed∖predicted = ∅.
  ⚠ AND `borrowck-move-error-with-note--a` IS THE ROW `destrmove` PREDICTED AND
  MISSED. destrmove's note says it "moves out of a user-`Deref` receiver, where
  the walk breaks at the CALL"; it does not — it is a `match a.a` on `a: &A`,
  and the pattern site is where the question lives. One survey, one row moved
  from a wrong mechanism to a right one.
  COST 2, both legal: 02_semantic_core_pass_bc_deref_move_exempt_admit and
  02_semantic_core_pass_bc_match_deref_mut_refmut_arm. The first is
  `deref_move_exempt`'s own paired control, i.e. the probe re-refuses the
  exemption that arm exists for; the second is a `ref mut` arm over a deref
  scrutinee, which is a BORROW, not a move. A correct rule asks what the ARM
  BINDS, not what the scrutinee is — and rule 7 then says it will not close the
  same three.

## recvfieldpath / fldrootbits — RE-PRICED (rule 8), BOTH HOLD
site: src/compiler/borrow_check.cpp::check_recv_conflict
      src/compiler/borrow_check.cpp::field_borrow_conflicts
build: eca91795fcce2717 (READ; gate-db 116 unarmed -> 118 / 119 armed)
measured: 2026-08-29
fires: 91 / 5302137
ceiling: 1 / 1
cost: 0 / 0
verdict: ✓ UNCHANGED across the 365 -> 337 shrink, SET for SET
note: `recvfieldpath` closes issue-82032, `fldrootbits` closes borrowck-move-
  from-subpath-of-borrowed-path — the same single rows recorded on 2026-08-29
  against a 365-row ledger, at the same cost. Both probes are STILL IN THE TREE
  and neither has been funded. They are the only two rows of the 34 that are
  literally what the `B` label claims AND cost nothing, and between them they
  are the cheapest two rows on this file's whole board.

## aggcallloan — the SOLO column, and it is a rule-13 zero
site: src/compiler/borrow_check.cpp::is_self_borrowing
build: c774ec282c7d2d64 (READ from the gate DB; 120 unarmed -> 121 armed)
measured: 2026-08-29
fires: 180
ceiling: 0
cost: 0
verdict: 0 ALONE AND LOAD-BEARING FOR ONE ROW — predicted zero, and the reason was predicted too
note: `is_self_borrowing`'s result test is `is_borrow_carrying_type(ret)`, the
  ATTRIBUTE-keyed predicate; widened to `!is_ref_kind(ret) &&
  type_may_carry_borrow(ret)` it buys NOTHING on its own, and the zero was
  PREDICTED BEFORE THE RUN with its mechanism: the gate is only reached from
  `take_ref_borrows`' Call arm, and `visit_stmt`'s Let routing gate — which asks
  THE SAME NARROW PREDICATE — never routes `let s: S = mk(&mut x);` there at
  all. 180 arrivals is the site's own population of aggregate-carrying,
  non-reference results over 1385 legal programs plus the ledger, so this is a
  LIVE site with a zero, not an unreached one. `aggboth` shows it is required
  for one of the four rows.

## aggletroute — the OTHER solo column
site: src/compiler/borrow_check.cpp::visit_stmt (Let routing gate)
build: c774ec282c7d2d64 (READ; 120 unarmed -> 122 armed)
measured: 2026-08-29
fires: 57680
ceiling: 3
cost: 40
verdict: ⛔ STOP — and it carries the WHOLE cost of the pair
note: routing a `let` whose annotated type structurally carries a borrow through
  `take_ref_borrows` closes borrowck-assign-to-andmut-in-borrowed-loc,
  borrowed-mut-pointer-assign-overflow-off, nll_issue-54382-use-span-of-tail-of-
  block. PREDICTED ZERO for this half — wrong, and the three rows are the
  finding. But 40 legal programs die, all in 02_semantic_core, and the routing
  gate's own comment already said why: take_ref_borrows does not only hop, it
  RECORDS a fresh borrow for every `&`/`&mut` ARGUMENT with this binding as
  holder, which is exactly the `let res: GpRes = gp_build(…, &mut sa, …)`
  over-refusal that comment names. Measured, not argued: the comment was right.

## aggboth — THE WHOLE, PRICED FIRST-CLASS (rule 13)
site: both of the above, one name
build: c774ec282c7d2d64 (READ; 120 unarmed -> 126 armed)
measured: 2026-08-29
fires: 58458
ceiling: 4
cost: 40
verdict: ⛔ NOT FUNDABLE AS SPELLED — and RULE 13 held again, in the smaller direction
note: solo ceilings 0 + 3 = 3; the WHOLE is 4. The extra row is
  borrowck_already-borrowed-as-mutable-if-let-133941, which needs BOTH sites and
  is invisible to either. A per-site sweep reading only the solo column would
  have killed `aggcallloan` as dead — its solo ceiling is 0 and its solo cost is
  0 — while it is the half that makes one row close. Blame is per site, CREDIT
  IS PER SET, for the second round running.
    predicted, closed:      borrowck_borrowed-mut-pointer-assign-overflow-off
    predicted (hedged), NOT closed:  borrowck_issue-85581 — the loan it needs is
        deposited from a MATCH SCRUTINEE (`match heap.peek_mut() { Some(g) … }`),
        not from a `let`, so neither of these two gates is on its path. A third
        site, and this round did not find it.
    closed, NOT predicted:  borrowck_already-borrowed-as-mutable-if-let-133941 ·
        borrowck_borrowck-assign-to-andmut-in-borrowed-loc ·
        nll_issue-54382-use-span-of-tail-of-block
  COST 40 is the whole reason this is a stop sign, and it is entirely
  `aggletroute`'s: `aggcallloan` prices 0/0 alone. So the cost is NOT paid for
  the rows — three of the four need the guilty half, but the half that is FREE
  is the one no ledger row can be bought with alone. The shape a landing would
  need is a routing that HOPS without RECORDING, which is exactly the split the
  Door E / EXEMPT block beside that gate already draws for a different reason.

## tupidxmove — the cheapest true class-B row on the board
site: src/compiler/borrow_check.cpp::visit (Code::TupleIndex arm)
build: c774ec282c7d2d64 (READ; 120 unarmed -> 123 armed)
measured: 2026-08-29
fires: 8
ceiling: 1
cost: 0
verdict: ✓ THE ARM IS TWO LINES AND `FieldRead`'s IS ~140 — predicted set closed EXACTLY
note: `case Code::TupleIndex:` in visit() is `visit_place_base(receiver); break;`
  and nothing else: no partial-move record, no `moved_fields` overlap check, no
  field-borrow conflict. CONTROL, ONE VARIABLE — the projection spelling, with
  byte-identical bodies otherwise:
      struct W { a: B }   let y = x.a; let z = x.a;  → REFUSED "use of moved field 'x.a'"
      (B,)                let y = x.0; let z = x.0;  → ADMITTED
  PREDICTED move-out-of-tuple-field, and predicted it would be the ONLY one: an
  enumeration of every admit program containing a real tuple-index projection
  (not an integer literal suffix) finds FIVE rows in the whole 337, of which
  this is the only move. CLOSED exactly that; both diffs ∅.
  ⚠ RULE 4, DECLARED: 8 fires — the entire population of "a move-typed tuple
  projection in a consuming position" over the ledger plus 1385 legal programs
  is EIGHT. A ceiling off eight bounds the COUNT and nothing else, and COST 0
  over that population is worth very little. What makes this fundable anyway is
  not the number: it is that the arm is MISSING, and the correct fix is the
  FieldRead arm's own bookkeeping reached through `extract_borrow_place`, which
  already decomposes `TupleIndex` (it emits the index as a path segment).

## mutstaticborrow — RE-PRICED (rule 8), AND IT IS STILL UNFUNDED
site: src/compiler/sema_expr.cpp::lower_expr_inner (ADDR_OF_MUT static branch)
build: c774ec282c7d2d64 (READ; 120 unarmed -> 125 armed)
measured: 2026-08-29
fires: 2
ceiling: 2
cost: 0
verdict: ✓ HOLDS EXACTLY, and the survey is what found it again
note: priced on 2026-08-29 at ceiling 2 / cost 0, then the probe left the tree
  and BOTH ROWS ARE STILL IN THE LEDGER. Re-installed and re-priced against the
  337-row ledger: the same two, name for name —
    borrowck_borrowck-access-permissions--b-mut-borrow-of-static  (bck.B)
    borrowck_issue-42344                                          (bck.NEW)
  predicted∖closed = ∅   closed∖predicted = ∅.
  ⚠ RULE 4 STILL IN FORCE, unchanged: 2 fires off an outer population of 3.
  ⚠ AND THE ABUSE DIRECTION IS STILL UNMEASURED at this site: the branch hands
  out `&mut SY` for a genuine `static mut` with no `unsafe`, while the WRITE
  path demands one.

## addrofpart — NEVER FIRED, AND THE ZERO IS A MIS-SITED PROBE
site: src/compiler/borrow_check.cpp::visit (Code::AddrOf arm)
build: c774ec282c7d2d64 (READ; 120 unarmed -> 124 armed)
measured: 2026-08-29
fires: 0 over 337 ledger rows + 1385 legal programs, AND 0 on three hand programs
ceiling: — (the harness refuses a ceiling on a zero fire count, correctly)
cost: —
verdict: NEVER FIRED — and it names the site `recvaddrofpartial` got wrong
note: `recvaddrofpartial` (above, OBSERVED 2026-08-29, deliberately not priced)
  says: "`&l` reaches visit()'s AddrOf arm, which asks `check_live`, which reads
  the whole-variable `moved` flag and never `moved_fields`". The observation is
  right and THE SITE IS WRONG. A `report_partial_move` installed in that arm
  fires ZERO times — not only over the corpus, which would be corpus silence,
  but on THREE HAND-WRITTEN PROGRAMS of the exact shape the note describes
  (`let g = x.f;` then `touch(&x)`, then the `let r: &Foo = &x;` spelling, then
  the `let r = &x; r.f.v` spelling). An explicit `&x` in argument or `let`
  position does not reach visit()'s AddrOf arm at all; it reaches
  `take_ref_borrows`' AddrOf arm and lands in `take_borrow_whole_`. See
  `borrowpart`. ⚠ Rule 1 has a second edge here: this zero could not have been
  READ as a mis-siting from the fire count alone, because the probe sat inside
  its own shape test — the hand programs are what separated "arm not reached"
  from "reached without a partial move".

## borrowpart — THE CORRECTED SITE: CONFIRMED BY HAND, UNPRICEABLE BY THE LEDGER
site: src/compiler/borrow_check.cpp::take_borrow_whole_
build: f84f58c3d6f7b5bf (READ; gate-db 127 unarmed -> 128 armed)
measured: 2026-08-29
fires: 0 over 337 ledger rows + 1385 legal programs; 1 on a hand-written program
ceiling: — (refused on a zero fire count)
cost: —
verdict: THE OBSERVATION IS CONFIRMED AND THE POPULATION IS NOT HERE — rule 10, exactly
note: `take_borrow_whole_`'s third line is `if (it->moved)` and it never asks
  `moved_fields`. This is the SAME asymmetry `recvpartial` landed at the
  method-call receiver, one route over, and `report_partial_move` is already
  hoisted for exactly this reason. PROVED LIVE BY HAND, one token apart:
      let g = x.f; let _ = g.v; let _ = touch(&x);
      unarmed rc 0, no diagnostic
      armed   "use of partially moved value 'x' (field 'f' moved on line 8)"
  and the whole-value twin (`let a = x;` then `&x`) is already refused unarmed
  by the `it->moved` line right below, which is what says the two are one
  question asked at half strength.
  ⚠ THE LEDGER ROW IT WAS AIMED AT DOES NOT CLOSE, AND THE REASON IS A PAIR.
  moves-based-on-type-match-bindings partially moves through a MATCH ARM, and
  MEASURED: after `match x { Foo { f } => … }` the bc partial-move map for `x`
  is EMPTY — `patbyvalsubmove` records on the sub-place but both match sites
  save/restore `states_` per arm, so the record dies with the arm (its own note
  at propagate_pat_borrows says so for six other rows). So this row needs
  {a partial-move record that survives the match} ∪ {borrowpart}, and neither
  half closes it alone. Rule 13 in its third instance this round.
  ⚠ AND THE ZERO OVER 1722 PROGRAMS IS RULE 10 IN ITS PUREST FORM: both halves
  of this harness consist only of programs that COMPILE, and "a value partially
  moved and then borrowed" is precisely what no green program contains. A round
  that funds this brings its own population; this file's cannot price it.

## THREE ARMS LANDED — fldrootbits · recvfieldpath · tupidxmove
site: src/compiler/borrow_check.cpp::field_borrow_conflicts (root bits)
      src/compiler/borrow_check.cpp::check_recv_conflict (non-empty path)
      src/compiler/borrow_check.cpp::visit (Code::TupleIndex, merged into FieldRead)
build: d77e1435df3d19a0 (READ, post-landing; the probe prices were read off
      eca91795fcce2717 / c774ec282c7d2d64)
measured: 2026-08-29
fires: n/a — LANDED, not armed; priced as probes at 5302137 / 91 / 8 arrivals
  respectively (that round's own numbers, quoted here so the record can be read
  without them). Field added 2026-08-29e for the same reason as fpsrc-LANDED.
ceiling: 1 / 1 / 1        cost: 0 / 0 / 0
landed:  HALF of arm 1 (the shared-count branch; the `mut_borrowed` branch was
      measured to buy 0 rows and is NOT in the tree), all of arms 2 and 3
predicted: borrowck-move-from-subpath-of-borrowed-path (bck.B) ·
      issue-82032 (bck.B) · move-out-of-tuple-field (nllmoves.B)
closed:    the same three.  predicted∖closed = ∅   closed∖predicted = ∅
verdict: ✓ LANDED — ledger 337 -> 334, re-derived FOUR ways (rows 334, `# TOTAL`
      334, admit `.logos` on disk 334, registered admit ctest tests 334)

### WHERE THE FIX DIFFERS FROM THE PROBE
Nowhere in what it decides, and that is worth saying because four rounds running
the landing was NARROWER than the probe. All three probes were already the
correct rule rather than a crude over-approximation of one:
  · `fldrootbits` IS THE EXCEPTION, and it was found by the gate rather than by
    reading. The probe had TWO branches; only `need_exclusive && shared_borrows
    > 0` landed. MEASURED: with the `mut_borrowed` branch removed the ledger is
    334/334 green (so it buys zero rows) and `-L bc` is 1857/1858 green with all
    ten pinned texts UNTOUCHED (so its entire effect was rewording ten already-
    red diagnostics — the whole-var reader answers that question at every site
    the corpus reaches). Diagnostic text is not nothing, but a second name for a
    question that already has one is the defect this file keeps recording, so it
    is left OUT with its price named. The landed diagnostic is reworded from
    `ceiling-probe fldrootbits: cannot …` to the sibling loops' own wording
    (`fmt_path(target, "")` prints the bare root, which is what the path loops
    below already do for an empty borrowed path).
  · `recvfieldpath` lost only its `probe::on` guard.
  · `tupidxmove` is the one with an actual shape change: the probe measured "the
    arm is missing", the fix is `case Code::FieldRead: case Code::TupleIndex:`
    over ONE segment walk (`seg_of` / `recv_of`), so the tuple spelling inherits
    the moved-overlap question, `field_borrow_conflicts`, the raw-pointer bail
    and the `moved_fields` record rather than a copy of any of them. +36 lines
    over the three arms, of which the tuple arm is 6 lines of code.

### RULE 5, DISCHARGED BY HAND — 23 PROGRAMS, NOT BY THE CORPUS
Every one multi-line, every one proven to reach its arm by an armed fire log
that printed the place AND the state the arm was asked about, every one rc=0:
  ce01 read under a live shared root borrow (`shared=1 excl=0 flip=0`) ·
  ce02 NLL, the root loan dead before the field MOVE (`excl=1 shared=0`) ·
  ce03 the same over `&mut` · ce04 the loan scoped away · ce05 shared+shared
  live together · ce06 a disjoint FIELD loan vs a sibling move ·
  ce07 NLL before a `&mut self` field call · ce08 shared loan vs `&self` call ·
  ce09 a live `&mut` loan vs a call on the DISJOINT field · ce10 `n.t.v` two
  hops deep · ce11 three sequential `&mut self` calls on one path ·
  ce12 disjoint tuple elements moved · ce13 a Copy element read twice ·
  ce14 a non-consuming read through a move-typed element · ce15 `s.t.0` (tuple
  inside a struct field, sibling still read) · ce16 through `&(B,B)` ·
  ce17 a Copy element read on every turn of a loop · ce18 an element as a
  method-call PLACE BASE, twice · ce19 element moved, sibling borrowed ·
  ce20 THE ARM1×ARM2 COMPOSITE — `recvfieldpath` delegates INTO the new root
  bits, so a whole-root `&mut` and a field-place call now meet; the fire log
  shows both arms on the path at each of three lines and `flip=0` at all three ·
  ce21 a `&self` method call on the root, then a field move · ce22 the root as
  a `&a` ARGUMENT, then a field move · ce23 a field read after a dead `&mut a.i`.
⚠ RULE 10 IS WHY THESE EXIST: both halves of the harness consist only of
programs that COMPILE, so it can measure how often a refusal site is AVOIDED
and never how often it should fire. The three fail halves (the relanded rows)
are the other direction, and each was run before the fixtures were written.

### THE TEN PINNED DIAGNOSTICS — THE RED THAT SAID "YOU BUILT A SECOND READER"
Armed in full, `fldrootbits` reddened ten `fail/` fixtures. None was a cost:
every one already refused, on both trees, for the same rule. The new branch
simply answered EARLIER — at the field read, before the whole-var reader — and
named the place:
    was:  cannot use 'f' while it is mutably borrowed
    now:  cannot use 'f.x' while 'f' is mutably borrowed
borrowck-describe-lvalue · borrowck-union-borrow-nested ·
borrowck-uniq-via-lend--b · --t18 · issue-25793 · issue-47646 ·
two-phase-surprise-no-conflict · borrowed-referent-issue-38899 (nll) ·
issue-45157 (nll) · issue-57100 (nll).
⚠ THE TEN ARE THE MEASUREMENT, AND THE ANSWER IS THAT THE BRANCH IS REDUNDANT.
Ten fixtures reworded, ZERO rows bought, and four of the ten kept emitting the
whole-var line as a SECOND error — two readers, one question. The branch is
dropped and all ten `.expected` files are byte-unchanged from `0e62af0ce`.
What remains open, with its price named: the whole-var reader's message could
name the field path, and doing it THERE — one reader, not two — is worth ten
pinned texts. That is a diagnostics task, and it is not this round's.
⚠ AND THE FIXTURE THAT CAUGHT IT WAS MY OWN. Pair 1's first fail half
(`&mut a` live across a read of `a.i.n`) REFUSED ON THE REVERTED TREE — the
control revert, run before the fixtures were believed, is what said the
mechanism was not the one being pinned. The pair now isolates the half that
landed: a whole-root `&a` vs a sibling `&a.j`, both across a move of `a.i`.

### WHAT THIS ROUND DID NOT BUY, restated so it is not re-derived
`aggboth` (CEILING 4 / COST 40 — 40 legal programs, all at `aggletroute`, whose
free partner `aggcallloan` buys 0 alone; the shape a landing needs is a routing
that HOPS without RECORDING) · `patmoveref` (CEILING 3 / COST 2) ·
`mutstaticborrow` (CEILING 2 / COST 0, still unfunded, abuse direction still
unmeasured) · `borrowpart` (confirmed by hand, needs the PAIR with a partial-move
record that survives a match arm) · `addrofpart` (a mis-sited zero, retired).

---

# ROUND 2026-08-29d — THE THREE RE-SHAPINGS, AND WHAT EACH ONE ANSWERED

Subject: the four mechanisms the group-B survey PRICED AND DID NOT FUND.
Eight probes, ONE build (`e1c01cd58d49e571`, READ), L1 rc=0 with nothing armed.
Two more were priced free on the same build afterwards (`structpatty`,
`patmoveref`, `mutstaticsite` — no rebuild, the store already held the unarmed
baseline).

    probe             fires   ceiling  cost  verdict
    mutstaticborrow       2         2     0  ✓ HOLDS, third pricing, set for set
    patmovebind           8         3     0  ✓ THE NARROWING WORKS — same 3 rows, cost 2 -> 0
    aggwhole          58458         4    40  ⛔ STOP — reproduces `aggboth` to the digit
    aggnarrow         58024         0     0  ✗ THE HOP ALONE BUYS NOTHING — site LIVE
    aggscrutpair        258         2     0  ✓ THE THIRD SITE EXISTS, and it is free
    partpair             73         1     0  ✓ the PAIR closes it; neither half does
    borrowpart            0         —     —  NEVER MATCHED — its population is MADE by its partner
    aggcallloan         180         0     0  = re-priced (rule 8), unchanged from yesterday
    structpatty          72         0     0  = the other solo column of `partpair`
    patmoveref          549         3     2  = re-priced (rule 8), unchanged — the control for the narrowing
    mutstaticsite         3         0     0  = observational, rule 9's outer population

## (1) `patmoveref`: THE DISCRIMINATOR IS A CARRIED FACT, NOT A RELATION

The round's question was what separates the two legal casualties from the three
rows. It is the ARM'S BINDING MODE, and the mode is a fact the LIR already
carries (`pat_keys::BINDING_REF_MODES`, minted where the `ref` keyword and the
default-binding-mode decision both live). Both casualties are `ref` / `ref mut`
arms; all three rows bind BY VALUE:

    borrowck-move-error-many-places--move-out-of-ref-in-match
        match *f { Foo::Foo1(num1, num2) => … }      modes 0,0   move-typed  ⇒ FIRE
    borrowck-move-error-many-places--r-runtime       same shape              ⇒ FIRE
    borrowck-move-error-with-note--a
        match a.a { n => … }                          a named Wild binding   ⇒ FIRE
    02_semantic_core_pass_bc_deref_move_exempt_admit
        match *r { E::A(ref d) => … }                 mode 1                 ⇒ no
    02_semantic_core_pass_bc_match_deref_mut_refmut_arm
        match *x { Cycle::Node(ref mut y) => … }      mode 2                 ⇒ no

This is a NODE KIND, not a relation, and it was as cheap as the survey guessed
— the same shape as the fifteen "signature region" rows collapsing to four.

## (2) `aggboth`: "HOPS WITHOUT RECORDING" IS REFUTED, AND THE THIRD SITE IS REAL

The round's own words for what a landing needed were *"a routing that HOPS
without RECORDING"*. Spelled at the place that already draws that split — Door
E / EXEMPT's inherit-only hop, one `else` branch below the routing gate — it
buys ZERO ROWS off 58,024 arrivals. The mechanism the four rows need IS the
recording: `inherit_loans` can only EXTEND an existing loan, and at
`let z = copy_borrowed_ptr(&mut y);` there is no loan on `y` to extend — the
call-site borrow of the argument is transient. So the shape that was nominated
as the way out is not one; the cost and the rows come from the same effect.

But the SCRUTINEE site is real and it is FREE. See `aggscrutpair`.

## (3) `borrowpart`: THE PARTNER IS NOT THE ARM JOIN — IT IS A NULL TYPE

Yesterday's note said `moves-based-on-type-match-bindings` needs "a partial-move
record that survives the match", because "the bc partial-move map for `x` is
EMPTY" after `match x { Foo { f } => … }`. The map is empty for a DIFFERENT
reason, measured with `LOGOS_PBSM_TRACE=1` on the row itself: no record is ever
MADE. `each_pat_binding_place`'s `PC::Struct` arm passes `TypeRef(nullptr)` for
a shorthand field, and `patbyvalsubmove`'s gate is `is_move_type(t)` — so a
struct pattern is skipped exactly as an array pattern is (`slicepatnull`, B-5).
The variant spelling of the same program DOES record and DOES survive the arm:

    match x { Foo::F1(p) => { let _ = p.v; } }   →  [pbsm] place=x.0 root=x
    match x { Foo  { f } => { let _ = f.v; } }   →  no [pbsm] line at all

One token apart. The arm join was never the blocker.

---

## mutstaticborrow — RE-PRICED A THIRD TIME (rule 8), AND THE ABUSE DIRECTION IS NOW MEASURED
site: src/compiler/sema_expr.cpp::lower_expr_inner (ADDR_OF_MUT static branch)
build: e1c01cd58d49e571 (READ; gate-db unarmed baseline -> armed run)
measured: 2026-08-29
fires: 2 (of `mutstaticsite`'s 3 arrivals)
ceiling: 2
cost: 0
verdict: ✓ UNCHANGED across 365 -> 337 -> 334, name for name, three pricings
note: PREDICTED both rows by name before the run —
    borrowck_borrowck-access-permissions--b-mut-borrow-of-static  (bck.B)
    borrowck_issue-42344                                          (bck.NEW)
  predicted∖closed = ∅   closed∖predicted = ∅.
  ⚠ RULE 4 DECLARED AND UNCHANGED: 2 fires off an outer population of 3
  (`mutstaticsite`, re-priced this round at 3). A ceiling of 2 off 3 bounds
  almost nothing about the SET. What argues for it is the ARM, as with
  `tupidxmove`: the branch's own comment asserts "`&mut STATIC` (a `static mut`)
  IS the global's address" and NOTHING anywhere checks the `mut`.
  ⚠ RULE 5, DISCHARGED BY HAND — four programs, all rc 0, each proven to reach
  or to MISS the branch by an armed fire log under BOTH rule-9 names:
    ce_ms1 `static mut SY` borrowed inside `unsafe`   site=1 borrow=0  rc 0
    ce_ms4 `static mut CS: C` struct, `&mut CS`       site=1 borrow=0  rc 0
    ce_ms2 a `let mut` LOCAL, `&mut x`                site=0           rc 0
    ce_ms3 the SHARED `&SX` of an immutable static    site=0           rc 0
  The first two are the exemption HOLDING with the site proven reached; the
  last two never arrive at all, so they measure the branch's narrowness and not
  this probe — recorded as such, not as safety.
  ⚠ AND THE ABUSE DIRECTION IS NO LONGER UNMEASURED. IT IS A HOLE, and its two
  controls are one token away. `static mut SY: i64 = 1;` at module scope:
      let y: &mut i64 = &mut SY;   OUTSIDE unsafe   → rc 0, ADMITTED
      let v: i64 = SY;             OUTSIDE unsafe   → rc 1, "read of mutable
                                     static `SY` requires `unsafe` block"
      SY = 2;                      OUTSIDE unsafe   → rc 1, "write to mutable
                                     static `SY` requires `unsafe` block"
  Three paths to the same global, two ask for `unsafe` and the BORROW path asks
  nothing — and a `&mut` is strictly stronger than either. This is a second,
  independent defect at the same branch, with no ledger row and no fixture;
  `abuse_ms5/6/7` are the demonstrator and its controls.

## patmovebind — THE NARROWING, AND IT IS FREE
site: src/compiler/sema_stmt.cpp::lower_match
build: e1c01cd58d49e571 (READ)
measured: 2026-08-29
fires: 8
ceiling: 3
cost: 0
verdict: ✓ SAME THREE ROWS AS `patmoveref`, COST 2 -> 0 — rule 7 did NOT bite
note: `patmoveref` asks only what the SCRUTINEE is; this asks additionally what
  the ARM BINDS. Fires 549 -> 8: the population is no longer "every arm under a
  move-typed unowned scrutinee" but "an arm that binds a move-typed sub-place BY
  VALUE", which is 8 over the ledger plus 1385 legal programs.
    PREDICTED, closed:  borrowck_borrowck-move-error-many-places--move-out-of-ref-in-match
                        borrowck_borrowck-move-error-many-places--r-runtime
                        borrowck_borrowck-move-error-with-note--a
    predicted∖closed = ∅   closed∖predicted = ∅
    `patmoveref` re-priced on the SAME build as the control: 549 / 3 / 2, and
    the 2 are the same two names. So the narrowing is exactly the two casualties
    and nothing else moved.
  ⚠ RULE 7 SAYS A CRUDE PROBE AND A CORRECT FIX DO NOT CLOSE THE SAME PROGRAMS,
  and here they DO. That is a measurement, not a refutation of the rule: the
  crude form was already the right question asked at half strength (the
  scrutinee half), and the missing half was a fact the LIR carries rather than
  one that had to be recomputed. `tupidxmove` was the same shape a round ago.
  ⚠ THE PROBE IS DELIBERATELY SILENT ON FOUR PATTERN KINDS. `PC::Struct`,
  `PC::Slice`, `PC::RefBind` and `PC::RefPat` take the `default:` arm and make
  NO by-value move claim. Struct and Slice are silent because their binding
  types are not reachable here (that is `structpatty` / `slicepatnull`, one
  door over); the other two are by-reference by construction. A correct landing
  would have to decide Struct and Slice, and each is a row of its own.
  ⚠ MODES 3/4 CANNOT CO-OCCUR WITH THIS PROBE, by construction and not by luck:
  a default-binding-mode binding exists only under a REFERENCE scrutinee, and
  the outer gate requires `is_move_type(scrut_type)`, which a reference is not.
  ⚠ RULE 5, DISCHARGED BY HAND — ten programs, all rc 0, eight PROVEN to reach
  the site by an armed fire log (fires in brackets):
    ce_pb1 `E::A(ref d)` over `*r`                       [2] · ce_pb2 `ref mut`
    arm over a `Box` deref [2] · ce_pb3 a by-value COPY payload beside a `ref`
    arm [2] · ce_pb4 arms that bind nothing (`E::A(_)`, `E::B`) [2] ·
    ce_pb7 a STRUCT pattern, Copy shorthand + `ref` field [1] · ce_pb8 an `@`
    pattern with a `ref` sub [2] · ce_pb9 an OR pattern, every alt `ref` [2] ·
    ce_pb10 the INDEX scrutinee shape (`match a[0] { E::A(ref d) … }`) [2].
    ce_pb5 (an OWNED scrutinee) and ce_pb6 (a Copy scrutinee) fire ZERO — the
    OUTER gate excludes them, so they measure `patmoveref`'s half and not this
    one. Recorded as such; a counter-example that does not reach the site
    proves nothing (rule 1).

## aggwhole — THE BLUNT WHOLE, RE-SPELLED AND RE-PRICED (rule 8)
site: src/compiler/borrow_check.cpp::visit_stmt (Let routing gate)
      src/compiler/borrow_check.cpp::is_self_borrowing (result test)
build: e1c01cd58d49e571 (READ)
measured: 2026-08-29
fires: 58458
ceiling: 4
cost: 40
verdict: ⛔ STOP — and it reproduces `aggboth` TO THE DIGIT on a ledger 3 rows smaller
note: written from scratch this round (the previous round's probes had left the
  tree) as `type_may_carry_borrow` at the Let routing gate plus the same
  predicate at `is_self_borrowing`'s result test. Fires 58458, ceiling 4, cost
  40 — `aggboth`'s recorded 58458 / 4 / 40. The same four rows, the same forty
  legal programs. That agreement is worth stating because it is the only
  evidence that two independently-written spellings of "the structural notion of
  carrying a borrow" are the SAME mechanism.
    predicted, closed (all four, by name):
      borrowck_already-borrowed-as-mutable-if-let-133941 ·
      borrowck_borrowck-assign-to-andmut-in-borrowed-loc ·
      borrowck_borrowed-mut-pointer-assign-overflow-off ·
      nll_issue-54382-use-span-of-tail-of-block
    predicted∖closed = ∅   closed∖predicted = ∅
  COST 40, all in 02_semantic_core / 03_ownership / 25_spec, unchanged name for
  name from the 120 -> 122 delta the store still holds.

## aggnarrow — "A ROUTING THAT HOPS WITHOUT RECORDING": REFUTED, SITE LIVE
site: src/compiler/borrow_check.cpp::visit_stmt (Door E / EXEMPT hop)
      src/compiler/borrow_check.cpp::is_self_borrowing (result test)
build: e1c01cd58d49e571 (READ)
measured: 2026-08-29
fires: 58024
ceiling: 0
cost: 0
verdict: ✗ THE HOP IS FREE AND WORTH NOTHING — the recording IS the mechanism
note: the previous round named the landing shape as "a routing that HOPS without
  RECORDING, which is exactly the split the Door E / EXEMPT block beside that
  gate already draws". Spelled there — the same structural widening, applied to
  the inherit-only hop instead of to the routing gate — it moves NOT ONE ROW
  over 58,024 arrivals. The site is live by that count and by construction (it
  is the `else` branch the four rows' `let`s actually take today).
  THE MECHANISM, read not guessed: `inherit_loans` can only EXTEND a loan that
  already exists, and Door E's own comment says so. At
  `let z = copy_borrowed_ptr(&mut y);` nothing holds a loan on `y` — the
  argument's call-site borrow is transient and released at the call — so there
  is nothing for `z` to inherit. What closes the row is take_ref_borrows
  RECORDING a fresh borrow of `&mut y` with `z` as holder, and that is the very
  effect whose over-reach is the whole of the COST 40. The cost and the rows are
  ONE effect, so the split that was nominated does not exist at this site.
  ⇒ B-10 stays a STOP at the `let`. A landing would have to gate the ARGUMENT
  recording on the RESULT structurally carrying a borrow — a second question, at
  a third place (take_ref_borrows' Call/MethodCall `each_arg`), and it is not
  what "hops without recording" meant.

## aggscrutpair — THE THIRD SITE, AND IT IS THE ONE THAT IS FREE
site: src/compiler/borrow_check.cpp::retain_temp_scrut_loan
      src/compiler/borrow_check.cpp::is_self_borrowing (result test)
build: e1c01cd58d49e571 (READ)
measured: 2026-08-29
fires: 258
ceiling: 2
cost: 0
verdict: ✓ IT EXISTS, it closes TWO, and it costs nothing the corpus can see
note: `aggboth` hedged issue-85581 and missed it, correctly diagnosing why: its
  loan comes from a MATCH SCRUTINEE (`match heap.peek_mut() { Some(g) … }`), so
  neither B-10 gate is on its path. `retain_temp_scrut_loan` is that path, and
  its gate is `loan_carrying_type` — the NAMED-carrier closure — where the
  question is the structural one. Exactly the same two-notions-of-one-concept
  defect, at a third site.
    PREDICTED, closed:  borrowck_issue-85581
    closed, NOT predicted:  borrowck_reborrow-in-match-suggest-deref  (bck.A)
    predicted∖closed = ∅
  The unpredicted row is `match (&mut outer, 23i64) { … }` — a TUPLE-LITERAL
  temporary scrutinee. `tmcb_walk` recurses into tuple elements and
  `loan_carrying_type` does not, so the whole shape was invisible. It is a
  bck.A row: this mechanism reaches OUT of group B.
  ⚠ IT IS A PAIR, not one gate. `peek_mut`'s result is `Option<&mut i64>`, which
  `is_borrow_carrying_type` denies, so `is_self_borrowing` says no and
  take_ref_borrows' MethodCall arm ties no receiver — the scrutinee gate would
  fire and record nothing. Both halves are armed under this name.
  ⚠ RULE 5, DISCHARGED BY HAND — four programs, all rc 0, three PROVEN to reach
  the site armed: ce_as1 a temp scrutinee carrying a SHARED borrow, source read
  again inside the arm [3] · ce_as2 the ledger row's own shape with the second
  use moved AFTER the match, i.e. NLL must retire the loan [3] · ce_as3 a
  tuple-literal scrutinee holding `&x`, then a read of `x` [1] · ce_as4 a
  scalar-returning temp scrutinee, which fires ZERO — the gate declines it,
  which is the answer and not a silence.
  ⚠ COST 0 OVER THE CORPUS IS STILL NOT A SAFETY CLAIM: the loan's LIFETIME is
  decided by the synthetic holder's inheritors, and ce_as2 is the only NLL
  release this round tested.

## partpair — THE PAIR CLOSES ONE ROW AND NEITHER HALF CLOSES ANYTHING
site: src/compiler/borrow_check.cpp::each_pat_binding_place (PC::Struct arm)
      src/compiler/borrow_check.cpp::take_borrow_whole_
build: e1c01cd58d49e571 (READ)
measured: 2026-08-29
fires: 73   (solo: structpatty 72, borrowpart 0)
ceiling: 1  (solo: 0 and 0)
cost: 0
verdict: ✓ RULE 13's FOURTH INSTANCE, and the cleanest — the READER'S POPULATION IS MADE BY THE PRODUCER
note: PREDICTED moves_moves-based-on-type-match-bindings and closed exactly
  that; predicted∖closed = ∅, closed∖predicted = ∅.
  THE TWO HALVES:
   · `structpatty` — a struct-pattern SHORTHAND field reaches every consumer
     with a NULL binding type, so the landed `patbyvalsubmove` rule (gated on
     `is_move_type(t)`) skips every `match x { Foo { f } => … }`. The type is
     recoverable where the loss happens: the pattern carries the struct's NAME
     and `ts_.struct_by_name` / `spec_by_name` already index every def by it.
     SOLO: 72 fires, ceiling 0, cost 0 — it produces a record nobody reads.
   · `borrowpart` — `take_borrow_whole_`'s third line is `if (it->moved)` and it
     never asks `moved_fields`, the same asymmetry `recvpartial` landed at the
     method-call receiver one route over.
     SOLO: **0 fires**. Not an unreached site and not a refutation: the probe
     sits after `!it->moved_fields.empty()`, so its fire count IS its match
     population, and that population is EMPTY until `structpatty` creates it.
  ⚠ THE ARITHMETIC IS THE RESULT. 0 + 0 = 0, the whole is 1, and 72 + 0 = 72
  against the pair's 73 — the ONE extra arrival is `borrowpart`'s, and it exists
  only because the producer ran. A per-site sweep would have killed both halves
  as dead. Blame is per site, CREDIT IS PER SET, for the fourth round running.
  ⚠ AND IT CORRECTS YESTERDAY'S ATTRIBUTION. `borrowpart`'s note said this row
  needs "a partial-move record that survives the match", because the arm sites
  save/restore `states_`. MEASURED with LOGOS_PBSM_TRACE on the row and on its
  one-token twin: the VARIANT spelling records (`place=x.0 root=x`) and the
  record DOES survive the arm; the STRUCT spelling emits no trace line at all.
  The arm join was never the blocker for this row.
  DEMONSTRATION, three runs of the ledger row on one binary:
      unarmed                     rc 0, no diagnostic
      LOGOS_PROBE=structpatty     rc 0, `[pbsm] ln=12 b=f place=x.f root=x`
      LOGOS_PROBE=partpair        "use of partially moved value 'x'
                                   (field 'f' moved on line 12)"
  ⚠ RULE 5, DISCHARGED BY HAND — four programs, all rc 0, ALL proven live [2,2,2,2]:
    ce_pp1 one field moved, a DISJOINT sibling borrowed by `ref` in the same arm ·
    ce_pp2 Copy shorthand fields, whole-value borrow after the match ·
    ce_pp3 a `ref` shorthand field, whole-value borrow after the match ·
    ce_pp4 a GENERIC struct pattern (`W<i64>`), where the pattern carries the
    BASE name and the def is stored mono-mangled — a lookup MISS must leave the
    old null-type behaviour, and it does.

## aggcallloan / structpatty / patmoveref / mutstaticsite — THE SOLO COLUMNS
build: e1c01cd58d49e571 (READ) — all four priced FREE on the batch build
measured: 2026-08-29
fires:   180 / 72 / 549 / 3
ceiling: 0 / 0 / 3 / 0
cost:    0 / 0 / 2 / 0
verdict: three zeros that are each a LOAD-BEARING HALF or a CONTROL, and one
         re-price that reproduces yesterday exactly
note: `aggcallloan` re-priced under rule 8 on a ledger three rows smaller: 180
  fires, 0/0, unchanged. It is still the half that makes
  already-borrowed-as-mutable-if-let-133941 close and still worth nothing alone.
  `structpatty` and `borrowpart` are `partpair`'s two solo columns (above).
  `patmoveref` is the CONTROL for `patmovebind` and it is why the narrowing can
  be believed: same build, same population, 3 rows and the same 2 casualties.
  `mutstaticsite` is rule 9's outer half, unchanged at 3 arrivals.

## ⇒ WHAT DESERVES FUNDING, AND WHAT IS NOW CLOSED AS A QUESTION

FUNDABLE, in order of rows per unit of doubt:

 1. **`aggscrutpair` — 2 rows, cost 0, and it reaches OUT of group B.**
    Two gates, both a delegation from an ATTRIBUTE-keyed predicate to the
    structural one this file already owns. Closes issue-85581 (bck.B, the row
    `aggboth` explicitly could not reach) and reborrow-in-match-suggest-deref
    (bck.A, unpredicted). The largest free result on the board.
 2. **`patmovebind` — 3 rows, cost 0.** A narrowing whose whole content is a
    fact the LIR already carries. Its residual is named: `PC::Struct` and
    `PC::Slice` are deliberately silent.
 3. **`partpair` — 1 row, cost 0, and it repairs a NULL TYPE.** `structpatty`
    is worth landing on its own terms even at ceiling 0: a shorthand struct
    field arriving with no type is a hole in every consumer of
    `each_pat_binding_place`, not only in this one. `slicepatnull` (B-5, 3 rows)
    is the same defect at the array arm and still costs six spec rules.
 4. **`mutstaticborrow` — 2 rows, cost 0, third pricing, set for set.** Rule 4
    is in force (2 fires of 3) and the case is the ARM, not the number. It now
    carries a SECOND, independent finding at the same branch — the `unsafe`
    hole in the abuse direction, with its two one-token controls.

NOT FUNDABLE, and now for a MEASURED reason rather than a nominated one:

 · **`aggboth` / `aggwhole` at the `let`.** 4 rows / 40 legal programs,
   reproduced to the digit by an independent spelling. The escape hatch the
   previous round named — "a routing that hops without recording" — was built
   and priced this round and buys ZERO off 58,024 arrivals, because
   `inherit_loans` cannot create the loan the rows need. The cost and the rows
   are one effect. A landing would have to gate take_ref_borrows' ARGUMENT
   recording on the RESULT's structural carry: a different question, at a site
   nobody has priced.
 · **`borrowpart` alone** and **`aggcallloan` alone** — both 0, both
   load-bearing halves of a pair. Do not re-price either solo.

STILL OPEN AND UNCLAIMED, carried forward unchanged: `escape-argument--t09`
(call-site write summary, TWO rows) · anonymous-region-in-apit (#78) ·
borrowed-data-escapes-closure-148392 · the bare closure arm has no holder ·
22 of `type_may_carry_borrow`'s 28 consumers · four class-C region rows ·
`slicepatnull` (B-5, 3 rows, six spec rules) · `emit_generic_deref_step`'s
fallback on the TARGET TYPE (a sema defect with no row and no fixture) ·
and NEW this round: the `&mut <static mut>` `unsafe` hole (no row, no fixture).

# ROUND 2026-08-29e — FIVE ARMS LANDED, AND THE CORRECT FIX HAD TWO CASUALTIES THE PROBE DID NOT

Subject: the four mechanisms the previous round PRICED at cost 0 and did not fund.
All four landed, plus a fifth that had no ledger row. Ledger **334 → 326**,
re-derived by direct listing (rows 326 · `# TOTAL` 326 · admit `.logos` on disk 326).
Build `dce7383673e4964b` (READ). Baseline READ from the store before any edit:
build 134, 334 ledger rows / 1860 `-L bc` / 5777 recorded / 0 failed.

    arm                 rows  predicted  closed  cost   verdict
    mutstaticborrow        2      2         2      0    ✓ landed (E0596)
    patmovebind            3      3         3      1    ✓ landed, NARROWED TWICE (see §4b)
    aggscrutpair           2      2         2      0    ✓ landed (a PAIR of gates)
    partpair               1      1         1      0    ✓ landed (a PAIR of sites)
    mutstaticunsafe        0      0         0      0    ✓ landed, no row, 2 fixtures
    ------------------------------------------------------------------
    TOTAL                  8      8         8      1    predicted∖closed = ∅
                                                        closed∖predicted = ∅

## (1) THE COST-0 THAT WAS NOT FREE — `patmovebind` LOST TWO LEGAL PROGRAMS

The corpus said COST 0 and it was right about the corpus. Two HAND-WRITTEN legal
programs died anyway, for a sixth round running, and both were in the CORRECT
fix rather than in the probe — rule 7 from the other side. Neither shape exists
anywhere in 2195 borrow-corpus programs.

    match *r { (ref a, b) => … }        REFUSED   — the Tuple arm
    match *r { … ref b @ E::B => … }    REFUSED   — the At arm

INSTRUMENTED ONCE rather than guessed at (the walk printed node kind, binding
count and mode-vector length), and the two causes are DIFFERENT:

  · `PatAt` carries {name, sub, type, bind_slot} and NO MODE FIELD AT ALL, so
    `ref b @ E::B` and `b @ E::B` are the same node. The arm now makes no claim
    about its own binding and walks only the SUB. Residual, stated: `b @ E::B`
    by value is missed. A missing refusal is a row; a wrong refusal is a legal
    program killed.
  · A TUPLE's `ref a` never reaches the walk as a mode at all —
    build_pattern's PAT_WILD tuple-element arm rebuilds it as a bare named
    Wild (MEASURED: Tuple → Wild name='a'). So the named-Wild claim is trusted
    only at the arm's ROOT, never under a Tuple.

⚠ AND THE FIRST REPAIR OF THIS WAS ITSELF WRONG, which is why the instrument
was worth its build. Reading "no mode recorded" as "no claim" silenced ALL
THREE ROWS. `bind_ref_modes()` is minted ONLY where a mode is spelled —
`E::A(d)` walks with modes=0, `E::A(ref d)` with modes=1 — so an EMPTY vector
means "all by value". For THIS node absence IS a zero; for `PatAt` and for a
tuple's elements the fact is genuinely absent. The two look identical from
outside and only the minting site distinguishes them.

Only TWO node kinds now make a by-value claim: `VariantData` (which carries
BINDING_REF_MODES) and a named `Wild` AT THE ROOT. Strictly narrower than what
was priced, a SIXTH round running.

## (2) RULE 14, DISCHARGED BY MEASUREMENT AND IT CAUGHT ONE

A fail fixture matches its `.expected` as a SUBSTRING, so a second diagnostic on
an already-red program is invisible to `ctest` — cost 0 over `-L bc` does NOT
discharge rule 14. Instrument: compile all 2195 bc-labelled programs before and
after, diff rc AND stderr. A program whose rc did not move but whose stderr did
is a rewording; that is what `fldrootbits` did to ten diagnostics last round.

    first measurement:   9 programs changed — 8 rc changes, 1 TEXT-ONLY
    after the narrowing: 8 programs changed — 8 rc changes, 0 TEXT-ONLY

The one catch: `bc_match_slice_elem_moved`, already red with "cannot move out of
type `&[W]`, a non-copy slice", gained a SECOND line at the SAME line number.
`is_unowned_move_source` answers "deref OR index", and for the INDEX half an
array/slice reader already owns the question everywhere the corpus reaches. The
index half is now excluded from this gate — it costs nothing (none of the three
rows is an index scrutinee; they are `match *f` and `match a.a`) and it removes
the only overlap in the tree.

⚠ THE NARROW PREDICATE WAS NOT TOUCHED. `is_unowned_move_source` has four other
consumers; the exclusion lives in THIS gate, where the duplication is.

## (3) A FOURTH PATH TO A `static mut`, AND ONLY TWO ASKED

The previous round recorded three paths with two asking. There are FOUR:

    let v: i64 = SY;    read    → refused
    SY = 2i64;          write   → refused
    let y = &mut SY;    &mut    → ADMITTED   ← strictly stronger than either
    let y = &SY;        &       → ADMITTED   ← not previously measured

Both BORROW paths route around `lower_var_ref` to the global's address, so
neither ever asked. Repaired by DELEGATION, not by a third copy: the three
exemptions the read path spelled inline — local shadowing, const-generic name
pollution, extern-vs-`mut` — now live once in
`sema_impl.hpp::static_access_needs_unsafe`, and all four paths consult it.
Two names for one question is the defect this file keeps recording.

## (4) RULE 5, DISCHARGED BY HAND — 25 LEGAL PROGRAMS, 13 REFUSALS

Every counter-example is MULTI-LINE. The probes left the tree with the fix, so
liveness is proven by ONE-TOKEN TWINS rather than by a fire log: a twin that
refuses proves the site was reached AND that the discriminator is what declined
— strictly more than a fire count.

    25 legal programs   all rc 0   (6 static · 11 pattern · 4 scrutinee · 4 partial-move)
    13 must-refuse      all rc 1   (4 abuse · 9 one-token twins)

Recorded as exemptions and NOT as safety, because they never reach the site:
`ce_pb5` (an OWNED scrutinee) and `ce_pb6` (a Copy scrutinee) are excluded by the
outer gate, so they measure the scrutinee half and not the binding half.

## (4b) THE COST ORACLE WAS TOO NARROW — `-L bc` SAID 0, THE FULL SUITE SAID 1

`-L bc` is 1858 tests and it reported COST 0 for all five arms. The FULL suite
is 8685, and it found ONE program the bc corpus does not contain:

    tests/logos/ir/param_attrs_freeze.logos:49   (label `ir_snapshot`, not `bc`)
        fn interior_payload(h: &HasOpt) -> i64 {
            match h.o { Option::Some(c) => { return c.get(); } … } }

RULE 2, VERBATIM: proven live is necessary, not sufficient — the population may
be elsewhere. The `bc` label is keyed on an upstream directory for imports and on
FILENAME PREFIXES for natives; a borrow-check-relevant program under
`tests/logos/ir/` is in neither half, and the label's own comment already warned
that the prefix half "will go stale the same way the next time someone names a
fixture freshly". It did.

⚠ AND THE HIT IS NOT A CASUALTY — IT IS A SECOND INSTANCE OF THE DEFECT.
`h: &HasOpt`, `o: Option<Cell<i64>>`, and `Cell<i64>` is not Copy, so binding the
payload BY VALUE moves a non-Copy value out of `*h`: E0507, the same shape as the
ledger row borrowck-move-error-with-note--a (`match a.a { n => … }` on `a: &A`).
It sat green in the corpus only because no site asked the question. A permissive
defect is invisible to a green corpus BY CONSTRUCTION, and this is the third form
of that: not a missing test, but a test whose PROGRAM relies on the hole.

Repaired by one token (`Option::Some(ref c)`), and that is NOT weakening a test:
what this fixture asserts is the LLVM PARAMETER attribute bundle for `&HasOpt`,
pinned literally in `param_attrs_freeze.check` line 43 and derived by
`apply_param_attrs` from the pointee TYPE. The snapshot re-matched byte for byte
after the edit, which is the proof that the binding mode was never part of the
claim.

⇒ THE COST NUMBER FOR THIS ROUND IS 1, NOT 0, and the arm that paid it is
`patmovebind`. Reported as 1.

## (4c) THE PROBE-LOG LINT WAS CHECKING 46 OF 73 RECORDS

`probe-log-lint.py` matched records with `^## (\S+)\n` — a BARE `## name`
heading. Every record whose heading carries a title (`## mutstaticborrow —
RE-PRICED A THIRD TIME`) was silently skipped, which is every record the last
several rounds wrote, including all five of this one. MEASURED: 47 bare headings
seen, 54 titled ones never looked at. The "46 records" the gate kept reporting was
a count that had quietly stopped growing.

A record is now defined by CONTAINING a `site:` line rather than by how its
heading is spelt, and the name is the heading's first token. Three genuine
defects were sitting behind that blindness and are repaired here, none by
relaxing a check:

  · `capprovnocap-LANDED` — its `site:` line ran prose on after the symbol, so
    the parsed symbol was `prov_of,` and pointed at nothing. Prose moved to its
    own line.
  · `fpsrc-LANDED` and the three-arm landing record — NO `fires:` line at all.
    Both assert a landing in their verdicts, so both now say `fires: n/a —
    LANDED, not armed`, which is what they already claimed. No number invented.

⚠ AND ONE RULE HAD TO CHANGE FOR A REASON, not for convenience. Identity was the
NAME alone, so "two measurements under one name cannot be told apart" would have
forbidden re-pricing — which rule 8 REQUIRES. It only ever looked satisfied
because the repeats were invisible. Identity is now (name, build), and `build:`
is mandatory for every record: the field that distinguishes two pricings is the
one the format already carried.

## (5) CONTROL REVERT — THE FIXTURES WERE NOT BELIEVED UNTIL IT RAN

Sources reverted, rebuilt, all 25 fixtures re-measured, sources restored, rebuilt:

    15/15 new FAIL fixtures ADMITTED under the control  → each measures the change
    10/10 PASS twins green under the control            → each is legal either way

## (6) MEASURED RESIDUALS, RECORDED AND NOT CLAIMED

  · `&mut ARR` on an IMMUTABLE STATIC ARRAY still compiles. The branch's guard is
    `kind() != Array`, so a static array routes to `addr_of(name)` — a different
    path this landing does not touch. Demonstrator `ab_arr` run, rc 0.
  · `b @ E::B` by value under a deref scrutinee is missed (see §1).
  · A tuple's dropped `ref` keyword is a SEMA defect one door over
    (build_pattern's PAT_WILD tuple-element arm); `each_pat_binding_place`'s
    tuple arm records the identical silence for the identical reason.
  · `slicepatnull` (B-5) is the SAME null-type hole as `structpatty` at the ARRAY
    arm, still open, still six spec rules.

## (7) PROBES RETIRED WITH THEIR ROWS — AND ONE RE-PRICING NOW OWED

`mutstaticborrow`, `mutstaticsite`, `patmoveref`, `patmovebind`, `aggscrutpair`,
`aggcallloan`, `structpatty` and `borrowpart` have left the tree AS FIXES. Their
rows are closed, so they are not the rule-8 hazard (a probe leaving with its rows
still open).

⚠ `aggwhole` AND `aggnarrow` MUST BE RE-PRICED BEFORE ANYONE READS THEIR
NUMBERS. Both were measured with the `is_self_borrowing` result-test widening
armed as part of them; that half is now UNCONDITIONAL, so their recorded
58458/4/40 and 58024/0/0 are about a compiler that no longer exists. The `let`
routing gate is what is left of `aggwhole`, and it is the only thing its next
number will be about.

---

## mutstaticborrow — LANDED
site: src/compiler/sema_expr.cpp::lower_expr_inner (ADDR_OF_MUT static branch)
build: dce7383673e4964b (READ)
measured: 2026-08-29
fires: n/a (landed; was 2 of `mutstaticsite`'s 3 arrivals)
ceiling: 2
cost: 0
verdict: ✓ LANDED — E0596, set for set across four pricings (365→337→334→326)
note: PREDICTED both by name: borrowck-access-permissions--b-mut-borrow-of-static
  (bck.B), issue-42344 (bck.NEW). predicted∖closed = ∅, closed∖predicted = ∅.
  ⚠ RULE 4 WAS IN FORCE AND STAYS THE RECORD: 2 fires off an outer population of
  3 bounds almost nothing about the SET. What funded it is the ARM — the branch's
  own comment asserted "`&mut STATIC` (a `static mut`) IS the global's address"
  and nothing anywhere checked the `mut` — exactly as `tupidxmove`'s case was the
  missing arm and not its eight fires.
  Counter-examples, all rc 0 on the landed build: a `static mut` borrowed inside
  `unsafe` · a `static mut` STRUCT · a `let mut` local · a shared `&` of an
  immutable static · a LOCAL SHADOWING the static (rule 12 — the guard is a name
  SET, and the shadowing walk is what keeps it from being one).

## mutstaticunsafe — LANDED, NO LEDGER ROW
site: src/compiler/sema_expr.cpp::lower_expr_inner (ADDR_OF_MUT static branch)
      src/compiler/sema_expr.cpp::lower_unary (`&` static branch)
      src/compiler/sema_impl.hpp::static_access_needs_unsafe
build: dce7383673e4964b (READ)
measured: 2026-08-29
fires: n/a (landed)
ceiling: 0
cost: 0
verdict: ✓ LANDED — a PERMISSIVE defect with no row, invisible to a green corpus
note: see §3. Closed at the one place the question can be asked once rather than
  at two more call sites. THREE fixtures: the `&mut` half, the `&` half, and the
  SHADOWING guard. No ledger row because no import exercises it — the population
  is 87 corpus files using `static mut` and not one of them borrows a static
  outside `unsafe`, which is exactly why a green corpus could never have found it.

## patmovebind — LANDED, NARROWED TWICE
site: src/compiler/sema_stmt.cpp::lower_match
build: dce7383673e4964b (READ)
measured: 2026-08-29
fires: n/a (landed; the probe fired 8)
ceiling: 3
cost: 1 over the FULL 8685-test suite (0 over the 1858-test `-L bc` corpus,
      which does not contain the program — see §4b). The one hit is itself an
      instance of the defect, repaired by one token with its IR snapshot
      re-matching unchanged.
verdict: ✓ LANDED — and the CORRECT fix cost two legal programs the probe did not
note: PREDICTED three by name: borrowck-move-error-many-places--move-out-of-ref-in-match,
  --r-runtime, borrowck-move-error-with-note--a. predicted∖closed = ∅,
  closed∖predicted = ∅. The sibling `borrowck-move-error-with-note--b` was named
  in advance as the one that must NOT close, and it did not (still admitted).
  See §1 for the two casualties and §2 for the index exclusion. The landed rule
  is narrower than the probe in THREE independent ways — At silent, Wild
  untrusted under a Tuple, index scrutinees excluded — and closes the same three.

## aggscrutpair — LANDED (A PAIR OF GATES)
site: src/compiler/borrow_check.cpp::retain_temp_scrut_loan
      src/compiler/borrow_check.cpp::is_self_borrowing (result test)
build: dce7383673e4964b (READ)
measured: 2026-08-29
fires: n/a (landed; the probe fired 258)
ceiling: 2
cost: 0
verdict: ✓ LANDED — reaches OUT of group B, and adds NO new reader
note: PREDICTED issue-85581 (bck.B); ALSO closed reborrow-in-match-suggest-deref
  (bck.A), which the previous round had already named as the unpredicted one, so
  both were predicted this time. predicted∖closed = ∅, closed∖predicted = ∅.
  ⚠ NEITHER HALF CLOSES ANYTHING ALONE — `aggcallloan` prices 0/0 off 180
  arrivals and is load-bearing for this set. Blame is per site, CREDIT IS PER SET,
  for the fifth round running.
  ⚠ THIS ARM EMITS NO DIAGNOSTIC OF ITS OWN. It RECORDS a loan; the refusal comes
  from the conflict readers that already exist ("already mutably borrowed",
  "cannot use … while it is mutably borrowed"). That is why it is rule-14-clean
  by construction rather than by measurement.
  ⚠ COST 0 IS STILL NOT A SAFETY CLAIM: the loan's LIFETIME is decided by the
  synthetic holder's inheritors. `bc_aggscrutpair_use_after_match_twin` is the
  one NLL RELEASE this arm pins, and it is one program.

## partpair — LANDED (A PAIR OF SITES)
site: src/compiler/borrow_check.cpp::each_pat_binding_place (PC::Struct arm)
      src/compiler/borrow_check.cpp::take_borrow_whole_
build: dce7383673e4964b (READ)
measured: 2026-08-29
fires: n/a (landed; the pair fired 73, the halves 72 and 0)
ceiling: 1
cost: 0
verdict: ✓ LANDED — rule 13's fourth instance, and the producer MAKES the reader's
         population
note: PREDICTED moves-based-on-type-match-bindings and closed exactly that.
  0 + 0 = 0, the whole is 1, and 72 + 0 = 72 against the pair's 73 — the ONE
  extra arrival is the reader's and exists only because the producer ran. A
  per-site sweep would have killed both halves as dead.
  ⚠ THE REPAIR IS BY DELEGATION: `take_borrow_whole_` now calls
  `report_partial_move`, the reader that already owns this question and its
  wording at the method-call receiver. No second name was added.
  ⚠ A LOOKUP MISS LEAVES THE OLD BEHAVIOUR — a generic pattern carries the BASE
  name while the def is stored mono-mangled, so the field type stays null and the
  answer stays permissive. Pinned by
  `pass/bc_partpair_generic_lookup_miss_twin`.

# ROUND 2026-08-30a — ONE PRODUCER, ONE READER, AND THE SIX SPEC RULES WERE THE SPELLING

Subject: `each_pat_binding_place` hands a binding `TypeRef(nullptr)` and every
consumer that needs the type skips it. The struct-shorthand half LANDED yesterday
(`structpatty`, inside `partpair`); the ARRAY half — `slicepatnull`, B-5, three
rows — stood open at a recorded price of SIX SPEC RULES.

**THE ROUND'S NUMBER: the six were the CRUDE SPELLING, not the mechanism.** The
careful form — ask the SCRUTINEE for the element type instead of reading a null
as a move — refuses **ZERO** legal programs where the crude form refuses eight,
and every one of the eight is admitted again. What the careful form costs
instead is TWO REWORDED DIAGNOSTICS, in the half nobody priced separately.

Build `6a75e28d5731a885` (READ, `scripts/build_hash.py build`). Store builds:
149 unarmed baseline → 150 `slicepatnull`, 151 `slicesite`, 152 `slicewhole`,
153 `slicearr`, 154 `slicetype`, 155 `sliceplace`. L1 rc=0 with nothing armed.
Ledger 326, unchanged — this round PRICES, it does not fix, and the probes were
REVERTED. Their spelling is `/home/logos/sandbox/slicepat/slice.spec`, a
`probe-batch.sh` spec that re-applies all six in one build.

## (1) THE CONSUMER CENSUS — SATURATED, AND THERE IS EXACTLY ONE READER

`tools/dlog` over `borrow_check.cpp` (question kept at
`/home/logos/sandbox/slicepat/`): `each_pat_binding_place` has **two**
non-recursive call sites; every other row is its own recursion, at eight sites
across four instantiations.

    consumer                      line   params READ        DIRECTION
    propagate_pat_borrows         5825   b, t, place, mode
      · patbyvalsubmove           5964   b, T, place, mode  RISK — the ONLY type reader
      · the ref/ref mut loan      5984   b, place, mode     zero for the TYPE
    propagate_pat_reborrows       6071   b, place           zero — its TypeRef and
                                                            mode parameters are UNNAMED

⚠ AND THE CONTEXT-LEVEL ANSWER WAS NOT ENOUGH — the per-context row says
`propagate_pat_borrows` "uses t", which is true of a lambda holding two
independent rules. Asked PER SITE, `t` is referenced at exactly **two lines,
5966 and 5967**, both inside one condition. So a null becoming non-null can
newly reach exactly one refusing branch in the whole tree.

⚠ THE SECOND HALF OF THE DIRECTION IS NOT THE TYPE. `propagate_pat_borrows`'s
loan record and `propagate_pat_reborrows` both read the PLACE, so a producer
that refines the place changes them even though neither reads a type. That is
where this round's whole measured cost turned out to live.

## (2) THE PER-KIND MINT TABLE — RULE 16, SCHEMA READ AND THEN MEASURED

Where the type is minted, per kind, from `include/logos/compiler/lir_view.hpp`
and `lir.hpp`; and the null RATE measured over 1419 programs (the 326 admit
ledger, `tests/spec`, `tests/imported/fail/{borrowck,nll}`) with an env-gated
per-kind tally at the callback.

    kind          minted at                                  a null means            arrivals   null
    Wild          NOWHERE — PatWild carries NAME + BIND_SLOT  ABSENT BY CONSTRUCTION       333    333
                  and NO TYPE KEY AT ALL                      (context, never the node)
    VariantData   BINDING_TYPES, sema_stmt.cpp 4242-4433,     sema wrote a null       2903714      0
                  arity asserted at 4347
    Tuple         BINDING_TYPES, sema_stmt.cpp 5153           sema wrote a null             14      0
    Struct        NOWHERE on the node (PatFieldBinding is     nobody filled it in —        18      8
                  field_name + sub + slot); recovered IN      REPAIRED by `partpair`;
                  THE WALK from ts_.struct_by_name /          a mono-mangled generic
                  spec_by_name since `partpair`               lookup MISS stays null
    At            TYPE key                                    sema wrote a null             6      0
    RefBind       BIND_TYPE key                               sema wrote a null            42      0
    Slice/Or/     no bindings of their own; PatSlice has      pass-through; the element    —       —
    RefPat        prefix/rest/suffix and NO type key          type lives in the SCRUTINEE

⇒ **After `structpatty`, exactly one kind is 100% null, and its null is the
kind that cannot be repaired at the node.** `PC::Wild` has no type slot in the
mirror, so "give the binding its type" is not a fill-in — it is a change to what
the WALK CARRIES. That is why the crude spelling was crude: it read an
unrepresentable fact as a decision.
Struct's 8 of 18 are the generic lookup MISS, already pinned by
`pass/bc_partpair_generic_lookup_miss_twin`, and they stay permissive.

## (3) THE PROBE TABLE

    probe          fires  ceiling  cost(-L bc)  cost(FULL 8690)  verdict
    slicepatnull      61     3          8            —           ⛔ the CRUDE form, RE-PRICED
    slicesite         13     0          0            —           = rule 9's outer population
    slicewhole        13     2          0            —           peeling elem type + index place
    slicearr           6     2          0            2           ✓ THE CAREFUL FORM
    slicetype         13     3          0            —           type only, coarse place
    sliceplace        13     0          0            —           = place only, its reader is blind

`slicesite` fires on every `PC::Slice` arrival whose container type yields an
element type; `slicearr`'s 6 against that 13 IS the narrowing — seven of the
thirteen are reference scrutinees the careful form declines.

## (4) THE SETS, DIFFED BOTH WAYS — AND MY MECHANISM STORY WAS INVERTED

    slicepatnull  PREDICTED ceiling 3 by name; CLOSED exactly those three.
                  predicted∖closed = ∅   closed∖predicted = ∅
                  PREDICTED cost 4..6.  MEASURED 8 — rule 8 in the GROWING
                  direction. The eight, named:
                    25_spec_pass pat_3 · pat_4 · pat_6 · pat_7 · stmt_2
                      (FIVE spec rules; the recorded "four" was already stale)
                    02_semantic_core_pass bc_d3_thin_ref_binding_class
                    02_semantic_core_pass bc_patmovebind_tuple_ref_element_twin
                    02_semantic_core_pass regions-infer-borrow-scope-addr-of

    slicewhole    PREDICTED {array-match, --use-match--t13}
                  CLOSED    {array-match, --use-match--b}
                  predicted∖closed = {--t13}   closed∖predicted = {--b}
                  BOTH DIRECTIONS NON-EMPTY, and the count matched at 2 — the
                  "two errors cancelling" the reader warns about, in the wild.
                  I predicted `--b` would NOT close because its refusal is an
                  INDEX ASSIGN and the dotted-path partial-move tracking at
                  borrow_check.cpp:13880 handles FieldRead/TupleIndex only. It
                  closes anyway. And I predicted `--t13` WOULD close through
                  `take_borrow_whole_`; it does not, because under an index
                  place the second match's `ref y` borrows the SUB-place and
                  never reaches the whole-value reader.

    slicearr      the same two, same both-ways diff.
    slicetype     PREDICTED all three; CLOSED all three. ∅ / ∅.
    sliceplace    PREDICTED 0 for the stated reason (the only type reader skips
                  on `!t`, and propagate_pat_reborrows was left UNSEEDED so the
                  place change reaches nothing). MEASURED 0.
    slicesite     PREDICTED 0/0 observational. MEASURED 0/0, 13 arrivals.

## (5) RULE 13'S FIFTH INSTANCE, AND THE FIRST ONE THAT SUBTRACTS

    the TYPE alone, container's place   3 rows
    the PLACE alone, null type          0 rows
    both                                2 rows

Adding the half that looks more correct REMOVES a row. The coarse place is what
makes an element binding a WHOLE-VALUE use of the array, and the whole-value
readers — `consume`'s `report_partial_move`, `take_borrow_whole_`'s delegation
landed by `partpair` yesterday — are the ones that see `--use-match--t13`. Refine
the place and the binding stops being a whole-value use, so the row re-opens.
Blame is per site; CREDIT IS PER SET; and this round adds that an increment can
be NEGATIVE, which no per-site sweep would ever report.

## (6) RULE 10 AND RULE 5 — THIRTEEN HAND-WRITTEN PROGRAMS, ALL MULTI-LINE

Sources under `/home/logos/sandbox/slicepat/`. Every fire count below is from an
armed `LOGOS_PROBE_FIRE` log, so "reached the site" is measured, not assumed.

REACHING THE REFUSAL (the corpus cannot do this — rule 10):
    hp_move    two matches binding the SAME element of `[String; 3]`
               unarmed rc 0 · slicearr rc 1 "use of moved field 'a.2'"  [2 fires]
    hp_suffix  `[.., z]` twice — proves the SUFFIX index arithmetic (N-sc+j)
               unarmed rc 0 · slicearr rc 1 "use of moved field 'a.2'"  [2 fires]

LEGAL AND MUST STAY ADMITTED — all rc 0 under `slicearr`:
    ce_s1   `[i64; 3]`, three bindings, array used after
            ⚠ THE CRUDE FORM REFUSES IT: "use of moved value 'a'" [3 fires].
            One line, and it is four of the five spec-rule costs in miniature.
    ce_s9   `[String; 3]`, ALL THREE elements bound BY VALUE in ONE pattern
            ⚠ `slicetype` REFUSES IT [1 fire] — the coarse place consumes the
            root on the first binding and the second is "use of moved value".
            Legal in Rust. THIS IS WHY THE 3-ROW ARM IS NOT THE ANSWER, and the
            corpus said cost 0 for it: rule 5, first constructed try.
    ce_s11  `&[String]` scrutinee, element bound, `s` used afterwards
            ⚠ `slicewhole` REFUSES IT [1 fire] — the peel hands a by-REFERENCE
            ergonomic binding a move-typed element. `slicearr` fires ZERO here:
            the site DECLINES because the container is not an owned Array, and
            that decline IS the narrowing.
    ce_s2 `ref` element then whole-value use · ce_s3 `&[String]` element ·
    ce_s6 all-`_` · ce_s7 a fresh array per loop iteration · ce_s8 a move with
    no later use · ce_s10 a two-element array with one element bound

MEASURING THE LANGUAGE AND NOT THIS PROBE, recorded as such:
    ce_s4   `&[String; 3]` — sema refuses first, "slice pattern requires array
            or slice scrutinee", so it never reaches the site. Not safety.

THE PERMISSIVE RESIDUAL, HAND-WRITTEN AND NOT CLAIMED:
    hp_disjoint  move `a.0` in one match, `a.2` in the next. rustc E0382 (the
            second `match a` reads `a` whole); `slicearr` ADMITS. Identical to
            `--use-match--t13`, and it needs a partial-move-aware read at a
            match SCRUTINEE — `take_borrow_whole_`'s question, one door over.

## (7) RULE 15 AGAIN, AND THE FULL SUITE FOUND WHAT `-L bc` COULD NOT

`ceiling-probe`'s legal selections (`-L bc -L pass`, plus the spec/ownership/
advanced pass dirs) reported COST 0 for `slicearr`. The FULL 8690-test suite
reports **2**:

    logos_06_diagnostics_fail_borrowck-vec-pattern-move-tail
    logos_06_diagnostics_fail_borrowck-vec-pattern-nesting

⚠ NEITHER IS A LOST REFUSAL, AND THE CHANGE IS IN THE RIGHT DIRECTION. Both
still refuse (rc 1), and both went from TWO diagnostics to ONE:

    was   error: cannot assign through 'a[..]' because 'a' is borrowed
          error: cannot borrow 'a' as mutable: 'a' has shared borrows
    now   error: cannot borrow 'a' as mutable: 'a.2' is already borrowed

A `ref` binding in a slice pattern used to raise its loan on the CONTAINER, and
TWO readers answered the same question about it — the duplicated-diagnostic shape
rule 14 exists for, sitting green in the corpus because a `.expected` matches as a
SUBSTRING. Under an index place the loan sits on the element, one reader answers,
and it names the element. So the cost is re-pinning two `.expected` files against
output that is strictly better. It belongs to the PLACE half alone: `slicetype`
reproduces both old diagnostics byte for byte, and `sliceplace` armed alone
produces the change at ceiling 0.

⚠ AND THE TEXT ORACLE WAS RUN OVER EVERYTHING, NOT OVER THE SUITE. All 8642
`tests/**/*.logos` compiled twice, rc AND stderr captured and diffed:
**exactly four programs change, and they are the same four names the store's
149->153 delta reports.** predicted∖measured = ∅ both ways, on the widest oracle
available.

    rc CHANGES (2) — the ledger rows, and both close through readers `partpair`
      landed yesterday:
        borrowck-move-out-from-array-match
          "use of moved field 'a.0' / 'a.1' / 'a.2' (moved on line 10)"
        borrowck-move-out-from-array-use-match--b
          "use of partially moved value 'a' (field '2' moved on line 9)"
      — the second is `report_partial_move`, which is why my prediction that an
      INDEX ASSIGN could not see the sub-place record was wrong: it does not need
      to, the whole-value reader in `consume` asks first.
    TEXT-ONLY (2) — the two above, and both DELETE a duplicate rather than add one.

⚠ THE INSTRUMENT WAS BROKEN ON ITS FIRST READING AND SAID 6433. `logosc` prints
`logosc: wrote <path>` to stderr and the harness gave every compile a fresh
mktemp directory, so the oracle was reading its own scaffolding. Normalising the
temp path is what turns 6433 into 4. A text oracle has to be controlled like any
other channel.

## (8) WHAT DESERVES FUNDING

**`slicearr` — 2 rows, 0 legal programs refused over the corpus AND over eleven
hand-written legal shapes, 2 diagnostics to re-pin.** The producer carries the
scrutinee type down the walk; `PC::Slice` computes the element type only when
the container is an OWNED `[T; N]` (no ref peel, no `Kind::Slice`), gives prefix
elements index 0.. and suffix elements N-sc+j, and hands that type to the
`PC::Wild` sub-pattern that is the element binding. `PC::Wild` consumes a
SEPARATE carried parameter from the one `elem_ty` reads, because the walk is
seeded at its caller for every pattern and consuming the seed at `Wild` would
give a top-level `match x { n => … }` a non-null type with no probe armed — a
behaviour change in the BASELINE, attributed to nothing. That spelling was
written, and `probe-batch`'s L1 inertness check is what would have caught it.

NOT FUNDABLE, and now for a measured reason:
 · **`slicepatnull` as spelled** — cost 8, ceiling 3, and its cost is entirely
   "a null type is a move". RETIRE THE SPELLING, KEEP THE OBSERVATION.
 · **`slicetype`** — the only arm that reaches all three rows, and it buys the
   third with a first-try legal casualty (ce_s9) the corpus does not contain.
 · **`sliceplace` alone** — 0, and load-bearing for `slicearr`. Do not re-price
   it solo.

STILL OPEN after this round: `borrowck-move-out-from-array-use-match--t13`
(and its hand twin `hp_disjoint`) — a whole-value use of a partially-moved array
at a match SCRUTINEE. One row, one named mechanism, at a site `partpair` already
touched.

## slicepatnull — RE-PRICED (rule 8), AND THE PRICE GREW
site: src/compiler/borrow_check.cpp::each_pat_binding_place
build: 6a75e28d5731a885 (READ; store 149 unarmed -> 150 armed)
measured: 2026-08-30
fires: 61
ceiling: 3
cost: 8
verdict: ⛔ RETIRE THE SPELLING, KEEP THE OBSERVATION — every one of the eight is
         the "a null type is a move" over-reach, and the careful form pays none
note: recorded 2026-08-29 as 66 / 3 / 6 with "four are spec rules". Re-priced on
  a tree where `structpatty` has LANDED: fires 66 -> 61 (the struct-shorthand
  nulls have left its population), ceiling unchanged set for set, COST 6 -> 8
  and FIVE of the eight are spec rules. A cost GROWS, and this one grew because
  the arms landed around it, not because anything about it changed.
  ce_s1 is the whole of it in one legal program: `[i64; 3]`, three element
  bindings, the array used afterwards — refused with "use of moved value 'a'".

## slicearr — THE CAREFUL FORM
site: src/compiler/borrow_check.cpp::each_pat_binding_place (PC::Slice arm)
      src/compiler/borrow_check.cpp::propagate_pat_borrows (the scrutinee seed)
build: 6a75e28d5731a885 (READ; store 149 unarmed -> 153 armed)
measured: 2026-08-30
fires: 6   (of `slicesite`'s 13 arrivals — the other 7 are reference scrutinees)
ceiling: 2
cost: 2 over the FULL 8690-test suite (0 over `-L bc`, which does not contain
      either program); BOTH are REWORDED diagnostics, not lost refusals
verdict: ✓ 2 rows, no legal program refused, two `.expected` files to re-pin
note: PREDICTED borrowck-move-out-from-array-match and
  borrowck-move-out-from-array-use-match--t13. CLOSED array-match and
  --use-match--b. predicted∖closed = {--t13}, closed∖predicted = {--b}: BOTH
  directions non-empty with a MATCHING COUNT, which is the reader's own warning
  made concrete.
  ⚠ RULE 5, DISCHARGED BY HAND — eleven legal programs, all rc 0, and the two
  that decide the shape are ce_s9 (which `slicetype` refuses) and ce_s11 (which
  `slicewhole` refuses). ce_s4 measures the LANGUAGE, not this arm.
  ⚠ RULE 10, DISCHARGED — hp_move and hp_suffix reach the refusal with a fire
  log, and hp_suffix is what proves the suffix index arithmetic.
  ⚠ THE PERMISSIVE RESIDUAL IS NAMED: hp_disjoint / --t13.

## slicetype — THREE ROWS, AND A LEGAL PROGRAM ON THE FIRST TRY
site: src/compiler/borrow_check.cpp::each_pat_binding_place (PC::Slice arm)
build: 6a75e28d5731a885 (READ; store 149 unarmed -> 154 armed)
measured: 2026-08-30
fires: 13
ceiling: 3
cost: 0 over `-L bc` — AND THE 0 IS FALSE, broken by hand on the first
      constructed counter-example (ce_s9)
verdict: ⛔ the only arm that reaches all three rows, and it over-refuses the
         plain destructure of an owned array
note: the element type with the CONTAINER's place, so `mroot.size() ==
  place.size()` and the reader CONSUMES THE WHOLE ARRAY. That whole-value
  consume is exactly what `--use-match--t13` needs and exactly what kills
  `match a { [p, q, r] => … }` over `[String; 3]` — legal in Rust, admitted at
  HEAD, refused here with "use of moved value 'a'".

## slicewhole — THE PEEL, AND ERGONOMICS PAYS FOR IT
site: src/compiler/borrow_check.cpp::each_pat_binding_place (PC::Slice arm)
build: 6a75e28d5731a885 (READ; store 149 unarmed -> 152 armed)
measured: 2026-08-30
fires: 13
ceiling: 2
cost: 0 over `-L bc` — AND THE 0 IS FALSE (ce_s11)
verdict: = `slicearr` with the ref peel left in; the peel buys no row and costs
         a legal program
note: `elem_ty` peels one reference hop and accepts `Kind::Slice`, so a
  `&[T]` scrutinee — whose element bindings are BY REFERENCE under match
  ergonomics — hands the by-value move rule a move-typed element. ce_s11:
  `let s: &[String] = &a[..]; match s { [x, _, _] => … } use_s(s);` refuses with
  "use of moved value 's'". `slicearr` fires ZERO on it. Same two rows, one
  extra casualty: the peel is the whole difference and it is worth nothing.

## slicesite / sliceplace — THE OUTER POPULATION AND THE BLIND HALF
build: 6a75e28d5731a885 (READ; store 149 -> 151 / 155)
measured: 2026-08-30
fires: 13 / 13
ceiling: 0 / 0
cost: 0 / 0
verdict: rule 9's outer name, and a producer half whose reader cannot see it
note: `slicesite` is the `x_site` half — every `PC::Slice` arrival with a
  recoverable element type, 13 over the ledger plus the legal corpus. It is what
  makes `slicearr`'s 6 readable as a narrowing rather than as a small number.
  `sliceplace` gives elements their index segments and leaves the type null: the
  only type reader skips on `!t` and `propagate_pat_reborrows` was deliberately
  left UNSEEDED, so nothing downstream can see it — 0 rows, and it is
  load-bearing for `slicearr`. `borrowpart`'s shape a second time.
  ⚠ IT IS NOT INERT THOUGH: armed alone it produces both of the round's reworded
  diagnostics, at ceiling 0. A half that buys nothing can still cost something.

# ── ROUND 2026-08-30 · ONE PRODUCER DEFECT, MANY READERS · THE LANDING ───────

## slicearr — LANDED. Ledger 326 -> 324.
site: src/compiler/borrow_check.cpp::each_pat_binding_place — the `cty`/`wty`
      parameters, the PC::Wild arm, the PC::Slice arm — and
      ::propagate_pat_borrows, which seeds `cty` with the scrutinee type.
      ⚠ ONE OF THE TWO NON-RECURSIVE CALL SITES IS SEEDED. `propagate_pat_
      reborrows` is left UNSEEDED, exactly as priced: its TypeRef and mode
      parameters are UNNAMED, so it cannot read the fact and seeding it would be
      a change nothing measured.
build: a0d40357aaede068 (baseline, READ) -> cbba590b1a119ffe (source only)
measured: 2026-08-30
fires: 6     on the pricing build 6a75e28d5731a885, of `slicesite`'s 13
             arrivals — the other 7 are REFERENCE scrutinees the arm declines.
             The landed arm carries no probe gate, so this count is the priced
             probe's; the landed form is held by its fixtures instead.
ceiling: 2   PREDICTED BY NAME BEFORE THE EDIT, closed exactly:
             borrowck-move-out-from-array-match
             borrowck-move-out-from-array-use-match--b
             predicted∖closed = ∅, closed∖predicted = ∅ over all 326 rows.
cost: 2 over the FULL suite, PREDICTED BY NAME, and BOTH TEXT-ONLY:
             logos_06_diagnostics_fail_borrowck-vec-pattern-move-tail
             logos_06_diagnostics_fail_borrowck-vec-pattern-nesting
      Both still refuse (rc 1). Both went from TWO diagnostics to ONE:
        was  cannot assign through 'a[..]' because 'a' is borrowed
             cannot borrow 'a' as mutable: 'a' has shared borrows
        now  cannot borrow 'a' as mutable: 'a.2' is already borrowed
      RULE 14, AND IT RESOLVES IN THE OTHER DIRECTION: a slice `ref` binding
      used to raise its loan on the CONTAINER and TWO readers answered the same
      question about it. Under an index place ONE answers, and it names the
      element. The branch to drop was the DUPLICATE, and the fix deletes it —
      nothing new was added that another reader already emits.
verdict: ✓ LANDED, with three fixture pairs and both `.expected` files re-pinned.

### RULE 15, RUN AS THE ROUND'S PRIMARY ORACLE — AND IT BIT AGAIN
All 8642 `tests/**/*.logos` compiled twice, rc AND stderr captured and diffed:
**exactly four programs change**, and they are exactly the four names above.
    rc CHANGES (2)   the two ledger rows
    TEXT-ONLY (2)    the two diagnostics fixtures, each DELETING a duplicate
    fifth name       none, in either direction
⚠ THE INSTRUMENT'S SECOND SCAFFOLDING FAULT, ONE DAY AFTER THE FIRST. Yesterday
the oracle read `logosc: wrote <mktemp path>` and reported 6433 changed programs;
that is normalised now. TODAY the raw `diff -rq before after` reported TWELVE,
and eight of them were STALE FILES left in a REUSED output directory by
yesterday's sweep — the eight rows yesterday closed, whose `.logos` files no
longer exist on disk. Both dirs held 8651 entries against a tree of 8642. The
comparison must be driven by the CURRENT file list, not by the directory
listing: a text oracle needs its population pinned exactly like any other.

### RULE 5 AND RULE 10, RE-DISCHARGED ON THE LANDED FORM
The probe was gated on `logos::probe::on("slicearr")`; the landed arm is not.
Rule 7 says a crude probe and a correct fix do not close the same programs, so
the thirteen hand-written programs were re-run against the LANDED compiler:
    hp_move, hp_suffix   rc 1, "use of moved field 'a.2'" — the refusal is
                         reached, and hp_suffix is what proves N-sc+j
    ce_s1 ce_s2 ce_s3 ce_s6 ce_s7 ce_s8 ce_s9 ce_s10 ce_s11   all rc 0
    ce_s4                measures the LANGUAGE (sema refuses the `&[T; N]`
                         scrutinee first), recorded as such, not as safety
    hp_disjoint          ADMITTED — the named permissive residual, = --t13
CONTROL REVERT: `git checkout` of the one file rebuilt to a0d40357aaede068 BYTE
FOR BYTE, and under it hp_move and hp_suffix are rc 0 again.

### A CODEGEN DEFECT MET WHILE WRITING THE PASS TWINS — NO ROW, NOT THIS ARM
Reading a FIELD through an array-pattern element binding is broken on this tree,
and it is not borrow checking: `match a { [_, _, x] => x.n }` over `[P; 3]`
SEGFAULTS, `[ref x, _, _] => x.n` returns garbage (96, then 224), while
`a[2u64].n` returns 3. Separately, a `[String; 3]` with an element moved out
ABORTS at runtime — drop elaboration drops both the moved-out element and the
array. Neither can be reached by a change to borrow_check.cpp, and both are why
the three legal twins bind without reading and carry an i64 `Drop` payload
rather than a `String`. Named here so the next round does not read the fixture
shape as taste.

## THE FIVE DECLINES, EACH WITH THE NUMBER THAT CONDEMNS IT
    slicepatnull  ceiling 3, COST 8 (five spec rules + three fixtures). Its
                  whole cost is "a null type is a move"; ce_s1 — `[i64; 3]`,
                  three bindings, the array used after — is legal and refused.
                  RETIRE THE SPELLING, KEEP THE OBSERVATION. It is kept in the
                  tree as an ARMED-ONLY probe branch and buys nothing unarmed.
    slicetype     ceiling 3 — the ONLY arm that reaches --t13 — and cost 0 over
                  `-L bc` WHERE THE 0 IS FALSE: ce_s9, one fire, legal in Rust,
                  admitted at HEAD. Pinned as pass/bc_slicearr_owned_destructure_legal.
    slicewhole    ceiling 2, THE SAME TWO ROWS as slicearr, plus one casualty:
                  ce_s11, one fire. The reference peel buys ZERO rows and costs
                  a legal program. Pinned as
                  pass/bc_slicearr_ref_slice_scrutinee_legal.
    sliceplace    ceiling 0 — and NOT inert: armed alone it produces both of the
                  round's reworded diagnostics. Load-bearing for slicearr, never
                  landed alone.
    slicesite     ceiling 0, observational — rule 9's outer `x_site` name.

### ⚠ A 16th WAY A GATE CAN LIE, MET IN THIS ROUND'S OWN LADDER
`gate-run.sh` keys a recorded verdict on `scripts/build_hash.py` — logosc plus
the stdlib archives. A `.expected` file is in NEITHER. So after re-pinning the
two reworded diagnostics, `test-levels.sh L4 bc` answered **RC=0 while still
holding the two FAILED verdicts** recorded minutes earlier against the OLD
expectation, and printed "Nothing has changed that a test run could see." The
fixture had changed and the key could not see it. `FORCE=1 gate-run.sh -L bc`
then measured 1891 passed / 0 failed. A run whose identity omits the corpus is
a cache that answers questions about a tree that no longer exists — the same
shape as the version-string key that bit on 08-29, one layer out.

---

# 2026-08-30 — THE `bck.NEW` SURVEY BY MISSING OBSERVATION

## THE ROOT LABEL WAS WRONG ABOUT SIXTEEN OF ITS NINETEEN MEMBERS

`bck.NEW` holds **19** rows, not 20 — the prompt's count is one round stale
(`git log` on tests/logos/bc_admits.ledger: the suffixed roots `bck.NEW-1..-4`,
`-L`, `-M` are seven SEPARATE rows and were not counted here). Each of the 19
was compiled BY HAND, multi-line, against a one-variable control, and asked one
question: WHAT WOULD THE CHECKER HAVE TO OBSERVE THAT IT DOES NOT. The answer
partitions them into **ELEVEN** groups, and only three of the eleven are new
names. `NEW` names WHEN a row was filed, and nothing else.

    partition                                  rows  status
    P1  a `&&`/`||` RHS is a CONDITIONAL path      3  ALREADY PRICED — scinitcond
    P2  a `&mut` binding is AFFINE, not Copy       4  new name, site MIS-AIMED
    P3  an aggregate result carries a borrow       2  ALREADY REFUTED — aggwhole
    P4  E0509 — moving out of a Drop type          2  RETIRE (spec + §B1)
    P5  raw-pointer deref-move                     1  RETIRE (documented divergence)
    P6  a by-value `self` receiver is a MOVE       1  new name — recvselfderef
    P7  a guard's MOVES must outlive its arm       1  new name — guardmovearm
    P8  an overloaded index in WRITE position      1  new name — indexnomut
    P9  a STRUCT decl's lifetime names             1  existing rule, MISSING SITE
    P10 the index loan on the OUTERMOST root       1  ALREADY NAMED — idxbaseloan
    P11 nothing — the mechanism is already live    1  MIS-REDUCED ROW
                                                 ---
                                                  19

SIX of the nineteen (P1, P3, P10 and both P4 rows' analysis) belong to
partitions this file ALREADY carries; three more (P4, P5) are retirements the
tree had already argued for in its own comments and nobody had read back to the
ledger. THREE new names came out of it, and all three priced.

### P11 — THE ROW THAT NAMES A MECHANISM THAT IS ALREADY THERE

`borrowck-no-cycle-in-exchange-heap--min-move-while-mut-borrowed` is a hand
"MINIMAL admitting reduction", and the reduction lost the property. MEASURED,
one token at a time on this build:

    let y: &mut N = &mut x;  y.a = 2i64;  let z = x;                → ADMITTED
    let y: &mut N = &mut x;  y.a = 2i64;  let z = x;  let _ = y.a;  → REFUSED
    let y: &mut N = &mut x;  let z = x;   y.a = 2i64;               → REFUSED
    let y: &mut N = &mut x;  y.a = 2i64;  let q = eat(x);           → REFUSED
    let y: &N     = &x;      let z = x;   let _ = y.a;              → REFUSED

`consume()` asks `mut_borrowed || shared_borrows || mut_reservations` and gets
it right at every spelling. The row survives ONLY because `y` is NLL-dead at the
move — which is what makes the reduction legal Rust, not a hole. No rustc in
this tree to adjudicate, so it is recorded, not deleted: the row is either
mis-reduced or needs a fact its own program does not carry. Either way NO
mechanism is missing, and it must not be funded as if one were.

## scinitcond — RE-PRICED UNDER RULE 8, AND THE CEILING DID NOT DECAY
site: src/compiler/sema_expr.cpp::lower_binop
build: b817d199044cfb03 (READ; 158 unarmed -> 165 armed)
measured: 2026-08-30
fires: 12361
ceiling: 3
cost: 0
verdict: ✓ THE BEST THING ON THIS BOARD — 3 rows, cost 0, and it is ALREADY WRITTEN
note: recorded 2026-08-28 at ceiling 3 / cost 0 against a 400-row ledger and
  never landed. Re-priced here against 324 rows: SAME THREE ROWS, same zero.
    predicted, closed:  borrowck-and-init--r03 · borrowck-and-init--t03 ·
                        borrowck-or-init
    predicted∖closed = ∅   closed∖predicted = ∅
  ⚠ AND --r03 AND --t03 ARE BYTE-IDENTICAL PROGRAMS modulo the package name.
  Three rows, ONE question, TWO of them the same file twice. The honest count
  of DEFECTS this closes is two, and the ledger will still fall by three.
  The edit is `auto uninit_pre = currently_uninit_vars_;` before the RHS and a
  re-insert after — strictly conservative (names are only ever RESTORED to the
  uninit set), and the same fork `if`/`match`/loops already carry. The lines
  ABOVE it in the same function already snapshot `moved_vars_` across the same
  RHS for the same two-path reason; this is the definite-assignment tracker
  getting the fork the move tracker got.

## indexnomut — AN `Index` IMPL IS NOT A WRITABLE PLACE
site: src/compiler/sema_stmt.cpp::lower_place_assign
build: b817d199044cfb03 (READ; 158 unarmed -> 160 armed)
measured: 2026-08-30
fires: 2
ceiling: 2
cost: 0
verdict: ✓ FUND — and it closes a row from a SECOND root
note: `try_index_mut_assign` returns nullopt when the receiver's type has no
  `IndexMut` impl, and `lower_place_assign` then falls through to the RAW
  address machinery, which writes the struct's first field by accident. So
  `m[0i64] = 9i64` over a type with `Index` and no `IndexMut` compiles and
  silently writes the wrong thing. rustc: E0594.
    predicted, closed:      borrowck_index-mut-help
    closed, NOT predicted:  borrowck_borrowck-overloaded-index-ref-index
        — root `bck.NEW-3`, i.e. OUTSIDE the 19 surveyed. Predicted 1, closed 2,
        and the extra is the same question under a different root letter.
    predicted∖closed = ∅
  ⚠ RULE 5, AND THE ANSWER IS STRUCTURAL RATHER THAN EMPIRICAL. Eleven
  hand-written legal programs, and the `indexnomutsite` ARRIVAL count is what
  they measure, not their greenness:
    ce_ix1  Index + IndexMut, `m[0]=9`                  arrivals 0 (early return)
    ce_ix5  GENERIC `G<T>` with both impls              arrivals 0 (early return)
    ce_ix11 both impls, two writes                      arrivals 0 (early return)
    ce_ix2  native array `a[0]=9`                       arrivals 0 (not a Struct)
    ce_ix7  `arr[0].a = 9` element field write          arrivals 0 (not a Struct)
    ce_ix8  `m.vals[0]=9` — a FieldRead base            arrivals 0 (not a VarRef)
    ce_ix4  `Vec<i64>` `v[0]=9`                         arrivals 0 (another path)
    ce_ix3  a READ `m[0]` through Index only            arrivals 0 (not an assign)
    ce_ix10 a struct with NO index overload at all      arrivals 1, REFUSED
            ALREADY, armed and unarmed — the `has Index` conjunct is what keeps
            this arm off it, and it is the only OTHER thing that reaches here.
  So the site's ENTIRE live arrival population is "a struct with `Index`, no
  `IndexMut`, in write position", which is E0594 without exception. COST 0 here
  is a property of the arrival set, not a corpus reading — the strongest form
  of the claim this harness can make. (ce_ix6, `IndexMut` with no `Index`, and
  ce_ix9, `m[1]` read through a `&mut M` param, are refused UNARMED for
  unrelated reasons and are not costs; recorded so they are not re-counted.)

## guardmovearm — A GUARD THAT RAN AND FAILED STILL MOVED
site: src/compiler/sema_stmt.cpp::lower_match_expr (the arm-guard block)
build: b817d199044cfb03 (READ; 158 unarmed -> 162 armed)
measured: 2026-08-30
fires: 1
ceiling: 1
cost: 0
verdict: ✓ FUND — predicted set closed EXACTLY, and the comment beside it said why
note: the block already carries, in the tree, the sentence that names this:
  "the arm was not taken, so the next arm restarts from `pre_moves` and the
  guard's move is forgotten." That was written for the DROP side and closed
  with a #118 conditional-move flag; the DIAGNOSTIC side was left open. So a
  guard that moves `s` and returns false is invisible to every LATER arm:
    match 0 { 0 if { let _ = eat(s); false } => 5, _ => eat(s) }   → ADMITTED
    ... same guard, then `eat(s)` AFTER the match                  → REFUSED
  One token of position apart. The probe unions the guard's `moved_vars_` into
  `pre_moves`, which is exactly the set each subsequent arm restarts from.
    predicted, closed:  borrowck_use-moved-value-in-match-guard-drop
    predicted∖closed = ∅   closed∖predicted = ∅
    diagnostic: "use of moved variable 's'" at the arm, line 13.
  ⚠ RULE 5, DISCHARGED — five hand-written legal programs, and ALL FIVE REACH
  THE SITE (`guardmovearmsite` arrivals = 1 each), green armed and unarmed:
    ce_g1 a guard that BORROWS (`peek(&s)`) and an arm that moves ·
    ce_g2 the guard moves in the LAST arm ·
    ce_g3 a Copy scrutinee-adjacent local moved in a guard ·
    ce_g4 an EARLIER arm moves and a later guard exists (order direction) ·
    ce_g5 the guard moves a local it declared ITSELF.
  ⚠ AND THE STATEMENT SPELLING IS A SEPARATE SITE, PRICED SEPARATELY.
  `lower_match` (statement `match`) carries the byte-identical block;
  `guardmovearmstmt` fires ONE time over 324 rows and closes NOTHING. RULE 4
  APPLIES AND IS STATED: a population of one refutes nothing. It is not
  evidence the statement site is right — it is evidence the ledger contains no
  statement-`match` guard that moves. A landing goes in at BOTH spellings, for
  the reason `guardscrutloan` recorded on 2026-08-28: a rule at one match
  spelling is a rule at half of them.

## recvselfderef / recvselfmv — THE NARROW FORM DOMINATES, MEASURED
site: src/compiler/borrow_check.cpp::visit (Code::MethodCall arm)
build: b817d199044cfb03 (READ; 158 unarmed -> 163 / 164 armed)
measured: 2026-08-30
fires: 2 (narrow) / 478 (wide) — outer population `recvselfderefsite` = 174
ceiling: 2 / 2
cost: 0 / 1
verdict: ✓ FUND THE NARROW ONE — same ceiling, and the wide one's extra is a LEGAL program
note: the tree already names this residual, at `deref_move_exempt`'s own arm:
  "⚠ NOT a place base: visit_place_base visits with consuming=false, so
  `(*r).copy_field` / `(*r).method()` never reach this report." One token apart,
  on this build:
    fn eat(f: F) -> i64        eat(*r)        → REFUSED  E0507
    fn eat(self: Self) -> i64  (*r).eat()     → ADMITTED
  A by-value `self` IS a consuming position; `visit_place_base` hands the
  receiver to visit() with `consuming=false`, so the position-general Deref rule
  is never asked. NARROW = ask `deref_move_exempt` + `is_move_type` at a Deref
  receiver whose `method_self_kind` is 0. WIDE = visit that receiver with
  `consuming=true`.
    predicted, closed (BOTH probes, identical sets):
        borrowck_clone-span-on-try-operator   (predicted)
        moves_suggest-clone                   (NOT predicted — root nllmoves.R2,
                                               a THIRD block, reached from here)
    predicted∖closed = ∅
  ⚠ RULE 13, AND IT RESOLVED AGAINST THE BIGGER EDIT. The wide form buys the
  SAME TWO ROWS and refuses `logos_25_spec_pass_expr_4`: "use of moved value
  'm'" on `m.get(&k)` over a `HashMap`, where `method_self_kind` resolves 0 for
  a call that is really an autoref. More machinery, identical ceiling, one legal
  program dead. This is rule 7 in its cleanest form yet — the crude probe and
  the careful one close the SAME programs and only the crude one has a price.
  ⚠ RULE 9, both halves priced. `recvselfderefsite` (by-value self × Deref
  receiver, before the exemption test) fires 174 and closes 0: the exemptions —
  Copy pointee, raw pointer, TypeVar, destructure temp — hold on 172 of 174.
  ⚠ RULE 5. ce_rs4 (`(*r).eat()` TWICE on a Copy struct) reaches the site TWICE
  and stays green under both probes: `is_move_type` on the deref result is the
  load-bearing conjunct. The other five (`&self` through `*r`, `&self` autoref,
  by-value self on an OWNED local, by-value self on a temp, two `&self` calls)
  are green and DO NOT ARRIVE — recorded as such, because a counter-example
  that misses the site proves the OUTER guard and nothing about this one.

## mutrefmv / mutrefmvsite — A HOT, PROVEN-LIVE SITE WITH AN EMPTY INNER POPULATION
site: src/compiler/sema_impl.hpp::mark_moved_expr (the VarRef arm)
build: b817d199044cfb03 (READ; 158 unarmed -> 159 / 167 armed)
measured: 2026-08-30
fires: 0 (mutrefmv) / 9854 (mutrefmvsite)
ceiling: — / 0
cost: — / 0
verdict: ✗ NEVER FIRED, AND THE SITE IS THE WRONG ONE — rule 11, cleanly
note: FOUR rows share one missing observation, and it is a notion this file
  ALREADY carries a correction for at a DIFFERENT consumer.
  `moveclass::is_move_type` sends `MutRef` to `default: return false`, so a
  `&mut` binding is treated as Copy. MEASURED, one variable at a time:
    let q: &mut S = r;  q.v=2;  r.v=3;                  → ADMITTED
    gen(r) with `fn gen<T>(t: T)`, then `r.v = 2`       → ADMITTED
    for n in v { }  twice, `v: &mut Vec<i64>`           → ADMITTED
    the same `for` twice over an OWNED `Vec<i64>`       → REFUSED
  The rows: reborrow-sugg-move-then-borrow (`let`),
  moved-value-suggest-reborrow-issue-127285--r32 and --t32 (a generic by-value
  param), issue-83924 (the `for` head). ISSUE-83924 IS THE ONE THAT JOINS: the
  `for` head already moves its operand — for an OWNED operand it refuses — so
  its row is not a for-loop question at all, it is this one.
  ⚠ AND THE FIX IS ALREADY WRITTEN, ONCE, AT ONE CONSUMER.
  `SemaChecker::struct_type_is_copy` says, in the tree: "`!is_move_type` is the
  Copy proxy used elsewhere, but it misclassifies `&mut T`: a mutable reference
  owns nothing (not a move type in the drop-glue sense) yet is NOT Copy — it is
  affine". It repairs that at ITS OWN call and nowhere else. Two notions of one
  concept, and the narrow one wins everywhere it was not corrected.
  ⚠ RULE 11 — WHY THIS PROBE MEASURED NOTHING. `mark_moved_expr`'s VarRef arm
  is HOT: `mutrefmvsite`, placed unconditionally at that arm, fires 9854 times
  over the 324 rows. The MutRef subset fires ZERO. Proven live, and the inner
  population is genuinely empty — because EVERY caller pre-gates:
    sema_stmt.cpp:1001, 1798, 2661  `if (is_move_type(rhs_type)) mark_moved_expr`
    sema_stmt.cpp:7409              the `for` head, same gate
    sema_stmt.cpp:1049, 1082, 8869, 8899   destructure / element moves, same gate
  Hand-checked on this binary: the m4 control (a Drop struct moved twice)
  reaches the arm TWICE; m1 / m2 / m3 (the three `&mut` shapes above) reach it
  ZERO times. `track_args_moved` calls it UNGATED, which is why the arm looked
  like the right site from a read — and the three shapes still never arrive.
  THE MECHANISM IS NOT REFUTED. Its site is the eight caller gates, not this
  arm, and pricing it means a probe per gate (rule 13: the whole first). NOT
  MEASURED THIS ROUND, and the four rows stay where they are, named.

## THE FOUR RETIREMENTS, AND EACH IS ARGUED IN THE TREE ALREADY

Retiring a row honestly is worth as much as closing one, and NONE of these four
needed a new measurement — each is a verdict this codebase had already reached
and never written back to the ledger.

 * **borrowck-move-from-unsafe-ptr** — `deref_move_exempt` exemption (2), in
   full: "RAW-POINTER DEREF-MOVE IS A DOCUMENTED DIVERGENCE, NOT AN OVERSIGHT
   … Removing it here would refuse the stdlib, so the row it costs
   (borrowck-move-from-unsafe-ptr) stays on the ledger, named." Narrowing it by
   a Copy test does not help: logos.mem's ptr / Vec / Cell primitives move
   NON-Copy values out of memory they own. RETIRED, not deferred.
 * **borrowck-move-out-of-tuple-struct-with-dtor--r13 / --t13** — blocked
   TWICE, independently. (a) `fldmovedrop`'s own record: the E0509 rule
   "contradicts a written language rule" — `@rule intrinsic.drop.skip-moved-out
   -paths`, logos_25_spec_pass_intrinsic_1 — so funding it is a PAIR design
   decision, not a checker round. (b) `patdropdestr`'s record: these two rows
   specifically "produce NO fldmovedrop line at all, because their moved field
   is `struct Inner { a: i64 }` and `is_move_type` calls an all-scalar struct
   Copy. That is a Copy-inference question (DIVERGENCES §B1)". Two blockers,
   neither a checker site. RETIRED.
 * **already-borrowed-as-mutable-if-let-133941 / borrowck-assign-to-andmut-in-
   borrowed-loc** — both are `aggwhole`'s named closed set: CEILING 4 / COST 40,
   reproduced to the digit by two independently-written spellings, and
   `aggnarrow` refuted the "hop without recording" shape at the same site.
   ALREADY REFUTED; they are not `bck.NEW` rows in any useful sense.

## TWO CHEAP THINGS THIS ROUND DID NOT PRICE, NAMED SO THEY ARE NOT RE-FOUND

 * **generic-const-early-param** — the undeclared-lifetime rule EXISTS and is
   landed (`ltundecl_wide`, CEILING 4 / COST 0), and its site is FN
   DECLARATIONS ONLY. MEASURED on this build: `fn f<'a>(x: &'b i64)` is
   REFUSED; `struct W<'b> { data: &'a i64 }` COMPILES. The `known()` predicate
   is written, proven and exempt-checked; what is missing is a walk of a
   STRUCT declaration's field types against its own `lifetime_params`. A
   MISSING SITE for a landed rule, one row — the cheapest shape there is. Not
   priced here only because `sema_collect.cpp` does not yet include probe.hpp.
 * **slice-index-bounds-check-invalidation--t35** — already diagnosed in the
   ledger's own 388->387 entry: "it needs the loan keyed on the OUTERMOST place
   root", and `idxbaseloan` PREDICTED it would not close and it did not. A
   named residual of a landed mechanism, not a new question.

## ⇒ WHAT DESERVES FUNDING OUT OF `bck.NEW`

 1. **`scinitcond` — 3 rows, cost 0, and the edit already exists.** Two lines,
    the strictly conservative direction, re-priced today at exactly its
    2026-08-28 numbers on a ledger 76 rows smaller. Nothing else on this board
    is this cheap. (Honest note: two of the three rows are the same program.)
 2. **`indexnomut` — 2 rows, cost 0, and the zero is structural.** Reaches a
    second root (`bck.NEW-3`). The arrival population IS the defect population.
 3. **`recvselfderef` — 2 rows, cost 0, and it reaches a third block**
    (`nllmoves.R2`). Fund the NARROW form; the wide one is measured to buy
    nothing extra and cost a legal program.
 4. **`guardmovearm` — 1 row, cost 0, five counter-examples all reaching the
    site.** Land it at BOTH match spellings; the statement twin prices 0 off a
    population of ONE and that is not a reason to leave it out.
 5. **the `&mut`-is-affine partition — 4 rows, UNPRICED, site now known.**
    The largest single group in `bck.NEW`, with a written precedent for the
    exact correction at one consumer. Needs a probe per caller gate.

# ROUND 2026-08-30b — FOUR ARMS LANDED, AND THE TWO ZEROS THAT WERE FALSE

The `bck.NEW` survey (§ 2026-08-30, above) priced five things and recommended
four. All four landed. Ledger **324 -> 316**, predicted by name before the edit
and closed exactly: predicted∖closed = ∅, closed∖predicted = ∅.

⚠ **AND BOTH OF THE ROUND'S SURPRISES WERE MEASURED COSTS THAT PRICED ZERO.**
`ceiling-probe.sh`'s legal selections are `-L bc -L pass` plus three `pass`
directories. That population contains no `fail` fixtures and does not build the
stdlib, so it cannot see a cost that lands in either — and this round's two
real costs landed in exactly those two places. Rule 5 has never been this
literal: two probes, both priced COST 0, both zeros false, and neither
counter-example was reachable by writing more small programs — one needed
`cmake --build` and the other needed `ctest`'s fail half.

## WHAT LANDED, AND THE ONE ARM THAT DID NOT

    mechanism        site                              rows  predicted  closed
    scinitcond       sema_expr.cpp lower_binop            3      3         3
    indexnomut       sema_stmt.cpp lower_place_assign     2      2         2
    recvselfderef    borrow_check.cpp MethodCall arm      2      2         2
    guardmovearm     sema_stmt.cpp lower_match_expr       1      1         1
    guardmovearm     sema_stmt.cpp lower_match (stmt)     0      —      DECLINED
                                                        ---
                                                          8      8         8

## scinitcond — LANDED, AND IT WAS EXACTLY WHAT THE RECORD SAID

    borrowck-and-init--r03   "use of possibly uninitialised binding 'i'"
    borrowck-and-init--t03   the same, and BYTE-IDENTICAL to --r03
    borrowck-or-init         the same

Two lines: snapshot `currently_uninit_vars_` before the `&&`/`||` RHS and
re-insert after it. Strictly conservative — a name is only ever RESTORED. The
pass twin `bc_scinitcond_lhs_init_twin` puts the initialising block on the LHS,
one token away, and stays green: the restore is of the PRE-RHS set only.
Cost on the full 8648-program oracle: **0**, rc and text.

## indexnomut — LANDED, AND THE ZERO WAS STRUCTURAL AND STAYED TRUE

    index-mut-help                       "cannot assign to index of 'm': type
    borrowck-overloaded-index-ref-index   'M' implements `Index` but not
                                          `IndexMut`"  (the second is bck.NEW-3)

The only arm whose measured zero survived every widening this round. The site's
entire live arrival population is "a Struct VarRef base, in write position, with
`Index` and no `IndexMut`" — every legal spelling leaves earlier (eleven hand
programs, arrival counts in § 2026-08-30). The landing adds an early return so
the raw address machinery is not reached after the diagnostic; the probe only
reported. Cost on the full oracle: **0**.

## recvselfderef — LANDED NARROW, AND ITS COST 0 DIED ON THE STDLIB BUILD

    clone-span-on-try-operator   "cannot move out of a value behind a shared
    suggest-clone (nllmoves.R2)   reference (E0507)"

⚠ **THE FIRST BUILD AFTER THE EDIT REFUSED NINE `logos.mem` FUNCTIONS.**
`ssrle_encode_run`, `ssrle_finish_segment`, `ssrle_compactify` (twice),
`SsrleRun__pattern_ranks_up_to`, `SsrleRun__full_ranks`,
`SsrleRun__ranks_up_to`, `index_push_block`, `pack_runs_push` — every one of
them `target.set(...)` on a `&mut Vec<u16>` parameter. `emit_module` failed and
`liblogos-mem.a` did not build. Measured COST over `-L bc -L pass` and three
`pass` directories: **0**. The corpus does not compile the stdlib.

**THE CAUSE IS RULE 16, AT A PREDICATE THIS FILE ALREADY QUOTED.**
`tests/logos/fail/bc_recvpartial_byval_recv_fail.logos` says it in the tree:
"`method_self_kind` returns 0 for a by-value `self` AND for 'unresolved' AND
for 'ambiguous' AND for 'no params' — four facts under one number". Every
existing consumer reads that 0 as "not a borrow", where the conflation is
CONSERVATIVE. A consuming-position rule reads it in the opposite direction,
where the same conflation refuses legal code. `method_self_by_value` splits it:
the callee must actually be FOUND (`by_name`, or a `by_base` set of size one)
before its self kind is a fact about self at all.

Under the split the two rows still close — so the narrowing cost the mechanism
nothing — and `pass/bc_recvselfderef_unresolved_callee_twin` pins the shape the
stdlib supplied, since the corpus never did.

⚠ AND THE WIDE FORM (`recvselfmv`, visit the receiver with consuming=true) was
already declined at cost 1 for the same underlying reason: `m.get(&k)` over a
`HashMap` resolves `method_self_kind` 0 for an autoref. Two spellings, one
confusion, and only the second one made it to a build.

## guardmovearm — LANDED AT THE EXPRESSION SPELLING, DECLINED AT THE STATEMENT

    use-moved-value-in-match-guard-drop   "use of moved variable 's'"

⚠ **RULE 14, AND THE STATEMENT HALF IS THE HALF TO DROP.** The survey
recommended landing at BOTH match spellings, on the precedent that "a rule at
one match spelling is a rule at half of them". Measured: the statement spelling
buys **0** rows and costs **5** regressed diagnostics.

    logos_06_diagnostics_fail_borrowck-drop-from-guard
    logos_06_diagnostics_fail_move-guard-same-consts
    logos_06_diagnostics_fail_move-in-guard-1
    logos_06_diagnostics_fail_move-in-guard-2
    logos_06_diagnostics_fail_match-cfg-fake-edges--d-guard-may-be-taken

All five still REFUSE. What changes is WHO refuses: the borrow checker already
answers every statement-`match` guard move in the corpus, and answers BETTER —
`borrow_check.cpp:4973` reports "use of moved value 'x' (moved on line 11)"
where sema (`sema_expr.cpp:824`) reports "use of moved variable 'x'". Sema runs
first, so arming the statement half REPLACES a located diagnostic with an
unlocated one at five programs and buys nothing. At the EXPRESSION spelling the
borrow checker answers NOTHING, which is why that row was in the ledger.

⚠ AND `-L bc -L pass` PRICED THIS 0 TOO. ctest ANDs its filters, so the legal
selection holds no `fail` fixture; a change that only re-words a REFUSAL is
invisible to it by construction. Only the `-L bc` gate (which does include
`06_diagnostics_fail`) and the full-suite text oracle can see it.

## THE FIXTURE PAIRS — FOUR FAIL, FIVE PASS, EVERY FAIL HALF CONTROL-REVERTED

    tests/logos/fail/bc_scinitcond_and_rhs_init          `false && { i = 5; true }`
    tests/logos/pass/bc_scinitcond_lhs_init_twin         the block on the LHS
    tests/logos/fail/bc_indexnomut_index_only_write      `Index`, no `IndexMut`
    tests/logos/pass/bc_indexnomut_indexmut_twin         the `IndexMut` impl added
    tests/logos/fail/bc_recvselfderef_byval_self_through_ref  `(*r).eat()`, `self: Self`
    tests/logos/pass/bc_recvselfderef_ref_self_twin      `self: &F`, one token
    tests/logos/pass/bc_recvselfderef_unresolved_callee_twin  THE NARROWING FIXTURE
    tests/logos/fail/bc_guardmovearm_expr_guard_moved    a guard that moves, `match` expr
    tests/logos/pass/bc_guardmovearm_expr_guard_borrows_twin  the guard BORROWS

CONTROL REVERT, run before any of them was believed: the stashed tree rebuilt to
`b817d199044cfb03` BYTE FOR BYTE (`scripts/build_hash.py build`), and under that
binary all four fail fixtures compile rc=0 (ADMITTED) and all five pass twins
compile rc=0. So each fail half is closed by THIS round and not by something
already in the tree.

The five statement-`match` guard fixtures listed above are the DECLINED half's
pins: they are already in the corpus, already red under the arm, and they stay
green only because the arm is not there.

## THE RE-PRICED CEILINGS, RULE 8

`scinitcond`'s record was written on 2026-08-28 against a 400-row ledger and
re-priced by the survey against 324: same three rows, same zero. It landed
today against 324 and closed the same three. Three readings, two ledger sizes,
no decay — the only mechanism in this file with that history.

`indexnomut`, `recvselfderef` and `guardmovearm` were priced yesterday and
landed today on the same build read, so no decay window existed for them.

## WHAT IS STILL OPEN OUT OF `bck.NEW`, NAMED

 * **the `&mut`-is-affine partition, 4 rows** — the largest group, UNPRICED.
   `mark_moved_expr`'s VarRef arm is the WRONG site (fires 9854, its MutRef
   subset 0; every caller pre-gates on `is_move_type`). The site is the eight
   caller gates at sema_stmt.cpp 1001, 1049, 1082, 1798, 2661, 7409, 8869,
   8899, and pricing it means a probe per gate with the WHOLE priced first
   (rule 13). The correction itself is already written once, at
   `SemaChecker::struct_type_is_copy`. The probe was REMOVED from
   `sema_impl.hpp` this round and replaced by the measurement, as a comment.
 * **P9, one row, the cheapest thing left** — `generic-const-early-param`.
   `ltundecl_wide`'s undeclared-lifetime rule is landed and exempt-checked; its
   site is FN DECLARATIONS ONLY. `struct W<'b> { data: &'a i64 }` compiles. A
   MISSING SITE for a landed rule, unpriced only because `sema_collect.cpp`
   does not include `probe.hpp`.
 * **the statement-`match` guard-move union** — declined at 0 rows / 5
   regressed diagnostics, above. It becomes fundable the day sema's
   "use of moved variable" carries the move LINE, at which point the two
   readers are one reader and rule 14 resolves the other way.
 * **P11, one row** — `borrowck-no-cycle-in-exchange-heap--min-move-while-mut-
   borrowed` is legal under NLL as reduced; four one-token controls all refuse.
   No mechanism is missing. Not funded, and it must not be.
 * **P4 (2 rows) / P5 (1 row) / P3 (2 rows)** — retired by the survey with the
   verdicts the tree had already reached; unchanged.

## A DIAGNOSTIC RESIDUAL WITH NO ROW, MET WHILE PINNING THE FIXTURES

The `Code::MethodCall` receiver block in `borrow_check.cpp` is reached with
`line == 0` at every spelling measured (`let n = (*r).eat();`, a bare
`(*r).eat();` statement, and a `return (*r).eat()`), so BOTH E0507 diagnostics
this round lands print without a source line, and so does the LANDED
`recvpartial` rule beside them (`tests/logos/fail/bc_recvpartial_shared_recv_
fail.expected` pins an unlocated line too). The neighbouring `eat(*r)` spelling
prints `…logos:13:` correctly. Not this round's mechanism, not a new defect, and
it is why the two `.expected` files here pin a message with no location.

## RULE 15 — THE FULL-SUITE TEXT ORACLE, AND IT IS THE ROUND'S PRIMARY NUMBER

All **8648** `tests/**/*.logos` compiled twice, rc AND stderr captured and
diffed, driven from the CURRENT file list (never a directory diff) with
`logosc: wrote <path>` and the mktemp path normalised out.

    rc CHANGES     8   — exactly the eight ledger rows, and nothing else
    TEXT-ONLY      0   — no diagnostic anywhere else is re-worded

    tests/imported/admit/borrowck/borrowck-and-init--r03            RC 0 -> 1
    tests/imported/admit/borrowck/borrowck-and-init--t03            RC 0 -> 1
    tests/imported/admit/borrowck/borrowck-or-init                  RC 0 -> 1
    tests/imported/admit/borrowck/borrowck-overloaded-index-ref-index RC 0 -> 1
    tests/imported/admit/borrowck/clone-span-on-try-operator        RC 0 -> 1
    tests/imported/admit/borrowck/index-mut-help                    RC 0 -> 1
    tests/imported/admit/borrowck/use-moved-value-in-match-guard-drop RC 0 -> 1
    tests/imported/admit/moves/suggest-clone                        RC 0 -> 1

predicted∖measured = ∅ and measured∖predicted = ∅, on the widest oracle
available. **FINAL COST: 0** — and that zero is worth exactly as much as the
two false zeros above, which is why it is stated with the population (8648
programs, the stdlib build, and the `fail` half of `-L bc`) rather than alone.

⚠ THE COST WAS NOT ZERO WHILE THE ROUND WAS RUNNING. It reached zero because
two things were REMOVED after being measured: the statement-`match` guard union
(5 diagnostics) and the bare `method_self_kind(v) == 0` test (9 stdlib
functions). A final zero over the corpus is the OUTPUT of the pricing, never a
substitute for it.

## THE PINS, RE-DERIVED

`logos_00_census_pin` and `logos_00_population_pin_lint` both went red, and both
reconcile term for term (⚠ read BEFORE they were predicted, which is the wrong
order and is recorded as such):

    REGISTRY-ALL        8696 -> 8705   -8 admit tests, +8 imported fail, +9 new
    REGISTRY-NOIMPORTED 4462 -> 4463   -8 admit tests, +9 new (the imported fail
                                        tests carry the `imported` label)
    REGISTRY-TIERCOMMIT  373 -> 365    -8; the ledger rows are the tier_commit
                                        half that moved
    direct_door corpus  2480 -> 2485   +5 = the five PASS twins; the four new
    direct_door nonglob 2289 -> 2294    fail fixtures and the eight moved
    direct_door glob     191 -> 191     imported programs are not in this
                                        population (pass corpus only)

Ledger re-derived FOUR ways: rows 316, `# TOTAL` 316, admit `.logos` on disk
316, registered `logos_00_bc_admit_*` ctest tests 316.

## THE GATES, ON THE COMMITTED TREE

    L1                        rc 0   745/745 + 12 684 enumerator cases
                                     + 365 tier_commit gates
    L4 bc  (detached)         rc 0   4463/4463, then 1344/1344
    full `cmake --build`      rc 0   build read 63678a4d6a5f87d9
    ledger  (store build 171) rc 0   316 passed / 0 failed / 316 recorded
    `-L bc` (store build 171) rc 0   1908 passed / 0 failed / 2 disabled
                                     (1893 -> 1910: +9 new fixtures,
                                      +8 imported programs that moved to fail)

⚠ THE LIBS HASH MOVED BETWEEN THE `L4 bc` RUN (84807cd183a12186) AND THE FINAL
FULL BUILD (63678a4d6a5f87d9) WITH NO COMPILED SOURCE CHANGED — the cmake
reconfigure that registers the nine new fixtures rebuilds the stdlib archives,
whose version string carries a timestamp. So both ledger selections were
RE-RUN on the final hash rather than read from the store, and the numbers above
are that re-run. A gate's rc is a measurement with a timestamp.
