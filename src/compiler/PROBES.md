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
