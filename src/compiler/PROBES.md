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
⚠ THE `cost:` BELOW WAS MEASURED OVER PASS FIXTURES ALONE, and the STATEMENT
spelling this note declines re-prices at 0 pass / **5 fail** with the repaired
oracle — see `## guardmovearmstmt`, § 2026-08-30c.
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
⚠ THE `cost: 0 / 1` BELOW WAS MEASURED OVER PASS FIXTURES ALONE. The crude
predicate both halves used — a bare `method_self_kind(v) == 0` — re-prices with
the repaired oracle as 1 pass, 0 fail, **STDLIB REFUSED: nine `logos.mem`
functions**; see `## recvselfderefwide`, § 2026-08-30c. The narrowing this
record recommended is what makes the LANDED form build.
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

---

# ROUND 2026-08-30c — THE INSTRUMENT, NOT THE TREE: WHAT THE COST ORACLE COULD NOT SEE

No compiler defect was touched this round. The subject is `ceiling-probe.sh`,
which yesterday priced two probes at COST 0 and was wrong both times.

## THE POPULATION BEFORE, AND WHY ITS BLINDNESS WAS STRUCTURAL

    LEDGER   -R '^logos_00_bc_admit_'                              316 rows
    LEGAL_A  -L bc -L pass                                         882 tests
    LEGAL_B  -R '^logos_(25_spec|03_ownership|04_advanced)_pass'   190 tests

Every member of the legal half is a PASSING TEST. The selection therefore
encodes one assumption — **damage looks like a passing test that fails** — and
an instrument's selection is exactly where its assumptions are unfalsifiable
from inside. Two of the three damage shapes were outside it by construction:

    shape                                     old oracle   why
    a legal program starts being refused       SEEN        that is the selection
    a `fail` fixture is refused DIFFERENTLY    BLIND       ctest ANDs its `-L`
                                                           filters: `-L bc -L
                                                           pass` can never name
                                                           a `fail` fixture
    the STDLIB stops compiling                 BLIND       the corpus does not
                                                           build the stdlib

And adding `-L bc -L fail` to ctest would have closed only part of the second:
`tests/logos/run_test.sh` in fail mode is `grep -qF -- "$(cat .expected)"`, a
SUBSTRING test. A probe that appends a note or re-words a hint leaves the ctest
verdict GREEN while changing what the compiler says (rule 15).

## THE POPULATION AFTER — THREE, EACH NAMED WHERE THE NUMBER IS PRINTED

    pass    unchanged: ledger 316 + legal 1072, through the store
    fail    scripts/fail_text_oracle.py — 1028 `-L bc -L fail` fixtures, each
            recorded as a TRIPLE: rc, sha256 of the NORMALISED stderr, and
            whether its `.expected` still matches. Three shapes counted apart:
            rc flip / `.expected`-match lost (what ctest would say) / TEXT-ONLY
            (what ctest CANNOT say).
    stdlib  scripts/stdlib-cost.sh — all four layers compiled from source under
            the probe. It asserts legality by BEING BUILT; no fixture author's
            opinion is involved, which makes it the hardest oracle in the tree.

⚠ `match` REPRODUCES `grep -F`'s ALTERNATION, and getting that wrong invents
differences. A multi-line `.expected` is an OR of its lines to `grep -F`; a
strict whole-string `in` test called all seven `nll_*_outlives_scope` fixtures
changed on an UNARMED baseline, before any probe existed.

⚠ AND THE READER'S OWN NULL POLE CAUGHT THE FIRST REAL BUG. `join` emits KEY,
then every remaining field of the left record, then the right's: with five
columns a side the armed rc is `$6`. The first version read `$5` — the
baseline's PATH column — and reported all 1028 fixtures changed under
`selftest_inert`, a probe that changes nothing.

## WHY NOT `ninja lib/logos/liblogos-*.a`, WHICH `pass-probe.sh` DOES

    -O2, ninja, chained, archives replaced   141 s   AND it moves build_hash.py
    -O0, scratch output, four in parallel     49 s   hash untouched

The second half is the one that matters. A stdlib rebuild changes the archive
bytes with NO source changed — the module's version string carries a timestamp,
measured `63678a4d6a5f87d9 -> e3eed1c6515e4486` — and that hash is the STORE'S
BUILD IDENTITY. Rebuilding the stdlib inside a price would invalidate every
verdict the store holds for the build and make the next gate re-run 1388 tests
it had already measured. Compiling each layer to a scratch output against the
archives already in the tree makes the four layers INDEPENDENT (they are no
longer a chain), so they run in parallel.

⚠ `-O0` IS A NAMED NARROWING, not a free lunch: the shape priced is a FRONT-END
refusal, which no `-O` level reaches. An LLVM-level failure that appears only at
`-O2` stays with the per-batch `cmake --build`.

## THE LADDER, IN SECONDS, MEASURED BOTH WAYS

    per mechanism        before   after
    pass half (store)     128 s    128 s   (ceiling-probe selftest_inert, fresh
                                            armed identity, 1388 tests)
    fail text oracle        —       54 s   (1028 compiles, 32-way)
    stdlib, four layers     —       49 s
                          -----    -----
                          128 s    231 s

    per BUILD, once      fail baseline 55 s — keyed on `build_hash.py` and
                         shared by every probe in the batch, because the
                         baseline is a property of the BINARY, not of the probe.

END-TO-END CONFIRMATION off this round's own two-probe batch (file mtimes):
build+L1 169 s, first probe 346 s (it pays the 55 s baseline and its stdlib
FAILS, which costs full time), second probe **215 s**. The component sum
predicted 231; the measured steady-state figure is 215.

⚠ THE STDLIB BELONGS IN THE PER-MECHANISM LADDER, and the honest ground is that
it is the CHEAPEST of the three additions (49 s against the fail half's 54 and
the pass half's 128) while being the only oracle for the one shape that
invalidates everything downstream of it. The per-batch `cmake --build` is not a
substitute: it builds the tree UNARMED, and a probe is env-gated by design, so
that build says nothing whatever about the armed compiler. `LOGOS_PROBE_SKIP_
STDLIB=1` exists for a deliberate choice — and when it is used the printed line
says `⚠NO-STDLIB` and the table prints `—`, never a digit.

## THE PROOF IS BY KNOWN ANSWER, NOT BY INSPECTION

Both misses were RE-CREATED as env-gated probes in one batch build, and the
repaired oracle was asked to report them. A widening that does not catch a known
miss is not a widening.

    probe                        fires ceiling   cost  cfail  std  verdict
    recvselfderefwide            17272       0      1      0  ⛔  STOP — THE STDLIB DID NOT COMPILE
    guardmovearmstmt                 6       0      0      5   ok  STOP re-words more diagnostics than it closes rows

## recvselfderefwide
site: src/compiler/borrow_check.cpp::method_self_by_value
build: 63678a4d6a5f87d9 (restored; measured under the batch build, 173 -> 174)
measured: 2026-08-30
fires: 17272
ceiling: 0
cost: 1 pass · 0 fail · STDLIB REFUSED (mem, 9 functions)
verdict: KNOWN-ANSWER — the repaired oracle names all nine, by name, and the
  old one printed COST 0 for the same edit
note: the edit is the bare `method_self_kind(v) == 0` predicate that yesterday's
  narrowing replaced with `method_self_by_value`. Predicted by yesterday's
  record, measured today, set-for-set:
    ssrle_encode_run · ssrle_finish_segment · ssrle_compactify (x2) ·
    SsrleRun__pattern_ranks_up_to · SsrleRun__full_ranks · SsrleRun__ranks_up_to
    · index_push_block · pack_runs_push
    predicted∖measured = ∅   measured∖predicted = ∅   (9 of 9, `logos.mem`)
  All nine are the same diagnostic, "cannot move out of a value behind a mutable
  reference (E0507)", on `target.set(...)` over a `&mut Vec<u16>` whose callee
  the index does not resolve — rule 16 at `method_self_kind`, which returns 0
  for four different facts.
  ⚠ THE PASS HALF NOW SHOWS 1 WHERE IT SHOWED 0, and that is not the widening:
  `bc_recvselfderef_unresolved_callee_twin` is the fixture yesterday's landing
  ADDED to pin this exact narrowing. The corpus grew a member because the miss
  was found; it could not have reported it at the time.

## guardmovearmstmt
site: src/compiler/sema_stmt.cpp::lower_match
build: 63678a4d6a5f87d9 (restored; measured under the batch build, 173 -> 175)
measured: 2026-08-30
fires: 6
ceiling: 0
cost: 0 pass · 5 fail (.expected-match LOST) · stdlib clean
verdict: KNOWN-ANSWER — the pass half still prints 0, exactly as it did
  yesterday; the fail column is what makes the zero readable
note: the statement-`match` guard-move union, declined yesterday at 0 rows and 5
  regressed diagnostics. Named by the record before the re-run, closed exactly:
    logos_06_diagnostics_fail_borrowck-drop-from-guard
    logos_06_diagnostics_fail_match-cfg-fake-edges--d-guard-may-be-taken
    logos_06_diagnostics_fail_move-guard-same-consts
    logos_06_diagnostics_fail_move-in-guard-1
    logos_06_diagnostics_fail_move-in-guard-2
    predicted∖measured = ∅   measured∖predicted = ∅   (5 of 5)
  All five are `.expected`-match LOST, not text-only: sema's "use of moved
  variable 'x'" does not contain borrowck's "use of moved value 'x' (moved on
  line 11)". So this shape WOULD also have been caught by putting `-L bc -L
  fail` into ctest. The TEXT-ONLY column caught nothing this round and is
  recorded as UNEXERCISED BY A KNOWN ANSWER — it is there for rule 15, which
  has bitten in the tree before, but it has not yet been proven live by a
  positive. That is a gap in this round's proof and it is stated, not hidden.

## THE RESTORE, PROVEN

    before the batch   scripts/build_hash.py build -> 63678a4d6a5f87d9 43
    after `git checkout` of the two sources + `cmake --build` rc 0
                       scripts/build_hash.py build -> 63678a4d6a5f87d9 43

Byte for byte, and `git status` clean of both compiled files.

## WHAT THE PRINTED LINE NOW SAYS

    probe: COST    = 0 legal programs refused   [saw: pass(ledger+legal) fail(text) stdlib]
    probe: COST-fail = 5 of 1028 `-L bc -L fail` fixtures changed
                       (rc 0, .expected-match 5, text-only 0)
    probe: stdlib: ⛔ REFUSED: mem — the hardest COST there is.
    probe: COST-stdlib = REFUSED — this outranks every number above.

and when a population is skipped, in place of a digit:

    probe: COST    = 0 legal programs refused   [saw: pass(ledger+legal) ⚠NO-FAIL ⚠NO-STDLIB]
    probe: COST-stdlib = NOT MEASURED — recvselfderef priced 0 here and then
    probe:               refused nine logos.mem functions.

The batch table gained `cfail` and `std` columns, and a `—` in either means NOT
MEASURED — never "nothing happened". Rule 4 of this round: a caveat in a
document is left behind with the document; this one travels with the digit.

## THE TWO RECORDS THIS CORRECTS

⚠ Both `## guardmovearm` and `## recvselfderef / recvselfmv` (§ 2026-08-30,
above) carry a `cost:` measured over the blind population. The numbers are not
withdrawn — they are correct ABOUT THE PASS CORPUS — but they are incomplete,
and re-priced today they read:

    record                       as written    with the repaired oracle
    recvselfderef (crude, wide)  cost: 0 / 1   1 pass, 0 fail, STDLIB REFUSED (9)
    guardmovearm (statement)     "closes 0"    0 pass, 5 fail, stdlib clean

⚠ AND THE SAME CAVEAT APPLIES TO EVERY `cost:` IN THIS FILE DATED BEFORE TODAY.
Each was measured over pass fixtures alone. That is not a reason to distrust
them uniformly — it is a reason to re-price before funding anything, which rule
8 already says for the ceiling and now says for the cost as well.

# ROUND 2026-08-30d — THE PARTITION WAS WRONG ABOUT ITS OWN MECHANISM

Two subjects were handed to this round on the repaired cost oracle: the
`&mut`-is-affine partition (4 rows, "a probe per caller gate") and P9, the
missing struct-lifetime site (1 row). Ledger **316 -> 310**, three arms, and the
four-row partition turned out to be **two unrelated mechanisms plus one row that
is neither** — with the eight caller gates it was supposed to be priced at
closing NOTHING.

## THE SETS, DIFFED BOTH WAYS

    probe               fires  ceiling  cost  cfail  std  verdict
    mraffall             1938        3     0      2   ok  the WHOLE (all 8 gates + arm + both real sites)
    mraffvecfor            18        1     0      0   ok  ✓ landed
    mraffbyval             35        2     0      0   ok  ✓ landed
    ltstructfld        358069        3     0      0   ok  ✓ landed
    ltstructfldstrict  358069        3     0      0   ok  the exemption walk — identical, term for term

**mraffall = mraffvecfor ∪ mraffbyval, exactly.** Rule 13's sixth instance and
the first where the whole is exactly the sum of two parts and the OTHER nine
sites contribute zero: the eight `is_move_type` caller gates named in the
2026-08-30 record (sema_stmt.cpp 1001, 1049, 1082, 1798, 2660, 7409, 8883, 8913
— the last two had drifted from the 8869/8899 the record names) plus the
`mark_moved_expr` VarRef arm close **0 rows** and lose **2** pinned diagnostics:
`borrowck-issue-48962--a-move-of-mut-ref-then-use` and `--b-move-of-mut-ref-
tuple-field`, both of which ALREADY refuse, with the borrow checker's located
wording ("use of moved value 'src' (moved on line 11)"), and would be replaced
by sema's unlocated one. **RULE 14 again, and it is the same shape as
`guardmovearm`'s statement half a round ago.** DECLINED.

    predicted (mraffall)  {issue-83924, 127285--r32, --t32, reborrow-sugg-move-then-borrow}
    measured  (mraffall)  {issue-83924, 127285--r32, --t32}
    predicted∖measured = {reborrow-sugg-move-then-borrow}   measured∖predicted = ∅

    predicted (ltstructfld) {generic-const-early-param}
    measured                {generic-const-early-param,
                             undeclared-lifetime-used-in-debug-macro-issue-70152,
                             regions-in-structs}
    measured∖predicted = the two lifereg.R17 rows — named by the ceiling BEFORE
    the edit, and both close for the upstream reason (E0261).

## WHY THE HANDED-DOWN SITE SET WAS WRONG — THE CENSUS, NOT A READ

`LOGOS_MRAFF=<file>` recorded per site `{arrivals, MutRef arrivals}` in ONE
compile, unarmed, for all nine candidate sites at once — the site-census
instrument rather than nine 230 s pricings. It cost one build and answered rule
1 and rule 9 for every site simultaneously:

  * **`for n in v` never reaches gate 7409.** With `v: &mut Vec<i64>` the head
    takes the `&Vec -> vec.as_slice()` desugar and returns before the
    `is_move_type` gate — 0 arrivals there, 2 at the desugar.
  * **`generic(self)` never reaches the `mark_moved_expr` VarRef arm.** It
    arrives as `AddrOfTemp` (expr code 12) of MutRef type, not `VarRef` (4).
    **THAT is why `mutrefmv` fired zero while the arm fired 9854** — the
    2026-08-30 record's explanation (every caller pre-gates on `is_move_type`)
    is TRUE of the gates and NOT the reason: `track_args_moved` is ungated and
    the operand still never arrives as a VarRef. Rule 12's cousin: a set of
    expression KINDS cannot say which one a name will be spelled as.
  * And the same `AddrOfTemp` of MutRef type is what a LEGAL reborrow
    (`bump(s)` into a `&mut S` formal) presents. **The two are identical at the
    argument; only the FORMAL separates them.**

## mraffvecfor — THE `&mut Vec` FOR-HEAD (1 row)

site: src/compiler/sema_stmt.cpp::lower_for_each   (the `&Vec -> as_slice()` desugar)
build: 7c488f42de25e539 (READ) armed; landed on 42036f427872e528
measured: 2026-08-30
fires: 18 · ceiling 1 · cost 0 · cfail 0/1028 · stdlib all four layers
row: issue-83924 — "use of moved variable 'v'"

`IntoIterator for &mut Vec` takes self BY VALUE in Rust, so `for n in v`
consumes the binding and a second loop is E0382. Logos desugars to a
non-consuming `as_slice()` borrow, so both loops were admitted.

⚠ **WHERE THE FIX DIFFERS FROM THE PROBE.** The probe called `mark_moved_expr`
and relied on the arm being widened under any `mraff*` name; the landed form
marks the place DIRECTLY (`VarRef` -> `mark_moved`) and does not touch the arm
at all — which is what keeps the two `borrowck-issue-48962` diagnostics. It is
also narrower by one shape: a FieldRead iter (`for n in self.v`) is not marked.
No row and no fixture asks for it; named here rather than guessed at.

## mraffbyval — A `&mut` PLACE INTO A BY-VALUE FORMAL (2 rows)

site: src/compiler/sema_expr.cpp::track_args_moved   (and its NINE call sites)
build: 7c488f42de25e539 (READ) armed; landed on 42036f427872e528
measured: 2026-08-30
fires: 35 · ceiling 2 · cost 0 · cfail 0/1028 · stdlib all four layers
rows: moved-value-suggest-reborrow-issue-127285--r32 / --t32 —
      "use of moved variable 'self'"

`track_args_moved` marked "by-value move-type args" moved and had no way to ask
what the argument was BOUND TO. It now takes the callee's `param_types` and a
`formal_off` (1 where slot 0 is `self`), and a MutRef argument against a
non-reference formal marks the place the reborrow peels back to. `AddrOfTemp` is
peeled; **`AddrOf` deliberately is not** — it carries a NAME and is `&mut owned`,
a fresh borrow of an owned local, not a move of any `&mut`.

All nine call sites pass formals (the probe wired two). The three extra pass-half
call sites and the two method sites were wired for the CLASS, not for a row, and
the full ladder is what says they cost nothing.

## ltstructfld — P9, A LANDED RULE WITH A MISSING SITE (3 rows)

site: src/compiler/sema_decl.cpp::lower_struct_def   (after the field collection)
build: 7c488f42de25e539 (READ) armed; landed on 42036f427872e528
measured: 2026-08-30
fires: 358069 · ceiling 3 · cost 0 · cfail 0/1028 · stdlib all four layers
rows: generic-const-early-param, undeclared-lifetime-used-in-debug-macro-issue-70152,
      regions-in-structs — "struct 'W': use of undeclared lifetime name ''a'"

`compute_fn_lifetime_outlives` walks a fn's parameter and return types against
the names in scope; its own comment names "struct FIELDS, enum PAYLOADS and
`static` declarations" as reached by nobody. The struct declaration checked its
OUTLIVES CLAUSE only. The field walk is the same rule, the same exemptions and
the same nested recursion (struct/enum lifetime args, tuple elements, slice and
array elements, pointer and reference pointees) — ONE notion of "in scope", not
a second spelling of it (rule 14 in the constructive direction).

The record said this was unpriced "for want of probe.hpp in sema_collect.cpp".
The site is **sema_decl.cpp**, which already includes it through sema_impl.hpp;
nothing was blocking it.

### THE EXEMPTION, WALKED IN THE ABUSE DIRECTION

`ltstructfldstrict` — the same walk with the `'_` exemption REMOVED — priced
**identically at every one of the four populations**: ceiling 3, cost 0,
cfail 0/1028, stdlib clean, and the same fire count to the digit. So the corpus
never exercises it and the zero is not an argument (rule 4). By hand:
`struct W { data: &'_ i64 }` COMPILES today and the strict form REFUSES it, for
zero rows. An exemption with a price and no purchase stays, and it is now pinned
as `tests/logos/pass/bc_structfldlt_placeholder_field_lifetime`, so the next
round meets a fixture instead of a blank.

## RULE 5 AND RULE 10 — THE COUNTER-EXAMPLES, EACH MULTI-LINE AND EACH RUN

Written BEFORE the costs were believed, and every one of them reaches the code
(the census records the arrival; the positive control refuses):

    h1  generic(s) then s.v = 1            positive control  REFUSED, "moved variable 's'"
    h2  bump(s); bump(s)  (&mut formal)    legal             rc 0 under every probe and landed
    h3  let q: &mut S = s; q.v = 1         legal             rc 0
    h4  for n in &mut vals  twice          legal (rvalue)    rc 0
    h5  for n in v  once, v not reused     legal             rc 0
    h6  s.v = 1; generic(s)  (move last)   legal             rc 0
    s2  struct W { data: &'_ i64 }         legal today       rc 0 wide / REFUSED strict
    s3  struct W<'a> { data:&'a i64, pair:(&'a i64,i64) }    rc 0

## WHAT THE REPAIRED ORACLE ACTUALLY BOUGHT THIS ROUND

`cfail` earned its place at `mraffall`: 2 lost `.expected` matches against 0
rows, which is the whole verdict on the eight caller gates, and the old
instrument would have printed `cost 0` and `✓ worth an exemption analysis`. The
STDLIB column found nothing this round (all five probes clean) and the TEXT-ONLY
column is still UNEXERCISED BY A KNOWN ANSWER — two rounds now.

## A DIAGNOSTIC RESIDUAL WITH NO ROW, MET WHILE PINNING

Every diagnostic raised at a STRUCT DECLARATION carries a wrong location and a
wrong context: `logos:557: error [fn iter_partition_vec]: struct 'W': …`. It is
PRE-EXISTING — the landed `struct 'W': … in outlives clause` check prints the
same 557 and the same unrelated stdlib fn — because `ctx_`, `file_` and
`node_line_` are never set for a declaration. Not introduced here and not fixed
here; the `.expected` files pin the message, which names the struct and the
lifetime correctly. One site, no row.

## WHAT IS LEFT OPEN, NAMED

 * **reborrow-sugg-move-then-borrow** — the fourth row of the partition, and it
   is neither of this round's mechanisms. `let moved: &mut State = state;` is a
   MOVE in Rust and a REBORROW in Logos: the RHS reaches gate 2660 as an
   `AddrOfTemp`, so marking anything there marks nothing. Its site is the `let`
   coercion that inserts the reborrow, not a move-tracking gate.
 * **enum PAYLOADS, `static` declarations, TRAIT method signatures** — the field
   walk's own named remainder, and the fn site's comment has named them since
   ltundecl_wide.
 * the declared-but-unfunded list from 2026-08-30b is untouched: escape-argument
   --t09 + fpwrite, #78, the bare closure arm, 22 of `type_may_carry_borrow`'s
   28 consumers, four region-blocked class-C rows.

# ROUND 2026-08-30e — `lifereg.A` + `lifereg.R17`: 32 ROWS, AND NOT ONE OF THEM NEEDS REGION INFERENCE

Subject: the two never-surveyed `lifereg` roots, `lifereg.A` (17) and
`lifereg.R17` (15). The standing advice was that if they need region inference
they are not fundable (0 of 91 named regions ever gets a CFG point; 46.7M
RegionInferer analyses → 57 conflicts and ONE pinned refusal). **The survey
answers the central question with a number: ZERO of the 32 need it.** Every one
is settled by a NAME or a comparison SITE, and the tree already owns machinery
for both.

## THE DISCRIMINATOR — SIXTEEN HAND PROGRAMS, ONE TOKEN APART, ALL MULTI-LINE

Not a relation. Two facts, each provable by flipping one token:

    u1  fn f<'a>(x: &'b i64)                REFUSED  "use of undeclared lifetime name ''b'"
    u2  struct W<'a> { data: &'b i64 }      REFUSED  (landed 2026-08-30d)
    u3  enum E { V(&'b u64) }               rc 0     ← the same rule, a missing site
    u5  impl Tr<'tcx> for W                 rc 0     ← ditto
    u6  trait Tr { fn m(&self, y:&'q i64) } rc 0     ← ditto
    H3  fn g<'a,T>(x:T) where T: 'b         REFUSED  "in `T: 'b` bound"
    H2  fn g<'a, T: 'b>(x: T)               rc 0     ← ONE bound, TWO spellings

    e6  fn f(a:&u8, b:&u8) -> &u8           REFUSED  E0106, elision has no source
    e1  enum E { V(&i64) }                  rc 0     ← same rule, missing site
    e4  type Foo = fn(&u8,&u8) -> &u8       rc 0     ← ditto
    e7  fn bar<F: Fn(&u8,&u8)->&u8>(f:&F)   rc 0     ← ditto
    e3  impl Coll for S { type Item = &T; } rc 0     ← ditto

    a4  struct Foo<T>; foo: &mut Foo<i64,i64>   REFUSED "expected 1 type arg(s), got 2"
    a2  struct Foo<'c,'d>; &mut Foo<'a,'a,'a>   rc 0   ← the arity check counts TYPE args only
    w3  fn f(x: &T)   (no T in scope)           REFUSED "unknown type 'T'"
    w1  fn set(..) where T: HF   (no T)         rc 0   ← same rule, missing site
    k1..k4  struct Foo<'static> / 'self / 'let  rc 0   ← no reserved-name rule at a binder
    s1  impl<'s> Foo<&'s u8> { fn bar<'s>(..) } rc 0   ← no shadow rule at a binder
    n1  impl<'a> Give for &S                    rc 0   ← no E0207 rule

**AND THE `lifereg.A` HALF, WHICH LOOKED THE MOST LIKE REGION INFERENCE AND IS
NOT.** The comparison mechanism (`check_variance` → `subtype` → `outlives`) is
LANDED and LIVE at five sites. It compares NAMES:

    B1  return y  ('a vs 'b, both named)        REFUSED  "variance mismatch"
    A9  -> &'static from &'v                    REFUSED
    B6  let p: &'a i64 = y   ('b)               REFUSED
    C4  x.p = y   (&mut Box2<'a>, y: &'b)       REFUSED  "assignment to 'x.p'"
    E5  p.0 = x   (tuple, named)                REFUSED
    C2  put(x, y) free fn, named                rc 0 → REFUSED under lifereg_callargstrict
    C3  x.put(y)  METHOD, same types            rc 0 under EVERY probe  ← site absent
    D5  Box2::put(x, y) UFCS, same types        rc 0 under EVERY probe  ← site absent

**THE NAMING EXPERIMENT — THE ROUND'S STRONGEST RESULT.** Four ledger rows
rewritten with a fresh name in each ELIDED slot and nothing else changed:

    N1  ex3-both-anon-regions-one-is-struct  → REFUSED today, unarmed
    N3  ex3-both-anon-regions-2              → REFUSED today, unarmed
    N4  regions-infer-at-fn-not-param        → REFUSED today, unarmed
    N2  ex3-...-both-are-structs-2           → still rc 0 (source is a FIELD READ)

Three rows are held open by a MISSING NAME, not a missing analysis. Rule 16
exactly: the empty lifetime string means BOTH "elided here" and "the same region
as the other elided slot", and only the minting site can tell them apart.

## THE SPLIT, EVERY ROW NAMED

**NEEDS CFG REGION INFERENCE: 0 of 32.**
**NEEDS A NAME OR A SITE THE TREE ALREADY HAS A RULE FOR: 32 of 32.**

    R17-a  E0261 undeclared lifetime, LANDED rule at a MISSING SITE      3
           regions-in-enums (enum payload) · issue-107988 (impl header)
           · static-typos (inline `<T: 'b>` bound; the where-clause
             spelling of the same bound already REFUSES — H2/H3)
    R17-b  E0106 elision, LANDED rule at a MISSING SITE                  4
           regions-in-enums-anon (enum payload) ·
           missing-lifetime-in-assoc-type-2 (assoc type) ·
           issue-19707--fn-type-elision (fn-ptr alias) ·
           issue-19707--b-bound (Fn-trait bound)
    R17-c  LIFETIME-ARG ARITY — the landed check counts TYPE args only   2
           noisy-follow-up-erro · constructor-lifetime-early-binding-error
    R17-d  RESERVED / KEYWORD NAME AT A BINDER — no rule anywhere        3
           regions-name-static ('static) · lifetime-no-keyword ('let) ·
           keyword-self-lifetime-error-10412 ('self)
    R17-e  SHADOWED BINDER (E0496) — no rule anywhere                    1
           shadow
    R17-f  UNCONSTRAINED IMPL PARAMETER (E0207) — no rule anywhere       1
           missing-lifetime-in-assoc-type-1
    R17-g  NOT A LIFETIME QUESTION AT ALL — "unknown type" at a
           MISSING SITE (a where-clause bound's subject; w1/w3)          1
           outlives-with-missing

    A-1    THE ELIDED REGION HAS NO NAME — landed comparison, no fact    7
           ex3-both-anon-regions-one-is-struct ·
           ex3-both-anon-regions-one-is-struct-4 ·
           ex3-both-anon-regions-2 · regions-infer-at-fn-not-param ·
           ex3-both-anon-regions-both-are-structs-2 ·
           e0621-mut-ref-aliases-pointee-lifetime-distinct · ex2b-push-no-existing-names
    A-2    THE METHOD-CALL ARGUMENT IS NOT A COMPARISON SITE             6
           ex2a-push-one-existing-name-early-bound · ex2e-push-inference-variable-3 ·
           ex3-both-anon-regions · ex3-...-earlybound-regions ·
           ex3-...-latebound-regions · iterator-trait-lifetime-error-13058
           (the last four carry NAMED lifetimes and still admit: A-2 alone
            holds them, C3/D5 prove the site is absent, not the name)
    A-3    A TRAIT OBJECT'S LIFETIME BOUND IS DROPPED ENTIRELY           3
           region-object-lifetime-in-coercion (`+ 'static` written, never
           compared — G1/G2) · regions-close-object-into-object-1 ·
           issue-103582-hint-type-alias (the DEFAULT object bound)
    A-4    A BORROW EXPRESSION CARRIES NO REGION                         1
           regions-addr-of-self — `let p:&'static mut i64 = &mut self.n`.
           F1 (`= x`, x: &'a) REFUSES; F2/F3 (`= &mut place`) do not.

None of A-1..A-4 is a CFG question. A-1 and A-4 are MINTING; A-2 and A-3 are
SITES. That is the defensible sentence this round was asked for, and it points
the other way from the standing advice: **these 32 rows are fundable, and the
lifetime channel's inertness is a consequence of missing NAMES, not evidence
that inference is required.**

## JOINS TO EXISTING PARTITIONS, RATHER THAN NEW NAMES

R17-a/R17-b are `ltstructfld`'s OWN NAMED REMAINDER from 2026-08-30d — "enum
PAYLOADS, `static` declarations, TRAIT method signatures" — plus two sites that
comment does not list (impl headers, fn-pointer/Fn-bound elision). R17-g joins
nothing lifetime-shaped: it is a name-resolution site. A-2 is the same shape as
`mraffbyval`'s finding that nine call sites had to be wired for the CLASS.

## THE PROBE TABLE, ALL THREE COST COLUMNS

    probe               fires  ceiling  cost  cfail  std  verdict
    ltelideboth            14        0     8      0   ok  ⛔ WRONG DOOR (below)
    ltelidesub              7        0     5      0   ok  ⛔ ditto
    ltelidesup              7        0     4      0   ok  ⛔ ditto
    ltenumpld           29629        1     0      1   ok  ✓ FUND
    ltbindresv              3        2     0      1   ok  ✓ FUND (re-pin one .expected)
    ltargarity             14        2     5      1   ok  ⛔ needs the prepass carve-out
    ltargarity_site      1388        0     0      0   ok  the arrival census (rule 9's outer name)

build: b5d34332f8de7107 (READ, `scripts/build_hash.py build`) · L1 rc=0, inert.

## THE SETS, DIFFED BOTH WAYS

    predicted (ltenumpld)  {regions-in-enums}
    measured               {regions-in-enums}
    both differences ∅.

    predicted (ltbindresv) {regions-name-static, lifetime-no-keyword,
                            keyword-self-lifetime-error-10412}
    measured               {regions-name-static, lifetime-no-keyword}
    predicted∖measured = {keyword-self-lifetime-error-10412}; measured∖predicted = ∅.
    `'self` is NOT rejected: it is a TRAIT's binder (`trait Serializable<'self,T>`)
    and a trait declaration's lifetime params do not come through
    `read_lifetime_params` at all. Rule 17's shape a third time — the one site
    that covers every OTHER declaration kind does not cover traits.

    predicted (ltargarity) {noisy-follow-up-erro,
                            constructor-lifetime-early-binding-error}
    measured               {noisy-follow-up-erro, regions-creating-enums3}
    predicted∖measured = {constructor-lifetime-early-binding-error} — the
      turbofish `S::<'static> { .. }` at a struct LITERAL does not route through
      `resolve_generic_named_type`; a second site, unfound.
    measured∖predicted = {regions-creating-enums3} — and it is an ACCIDENT, not
      a purchase: see below.

## WHY `ltelide*` PRICED ZERO — A LIVE SITE, THE WRONG DOOR (RULES 1 AND 11)

All three fired (14 / 7 / 7), so rule 1 is satisfied and the zero is an answer:
`outlives()`'s two empty-side exits are NOT where the elision rows are decided.
They are decided one frame up, in `include/logos/compiler/subtype.hpp`, at two
places that never call `outlives()` at all:

  * `types_equal_with_lifetimes`' `lt_eq`, line 66: `if (x.empty() || y.empty())
    return true;`
  * `detail::lifetime_at`'s `Variance::Inv` arm: `outlives_norm(sub_lt) ==
    outlives_norm(sup_lt)` — a STRING EQUALITY. `&mut` is invariant, so every
    `x.b = y` / `p.0 = x` row goes through here and `"" == ""` is true.

**A crude probe at the wrong door reports a true zero about a live site.** This
is rule 11 without a multi-hop channel: one hop, and the hop that matters is
above the one carrying the probe. The next round's edit is in `subtype.hpp`,
and the naming experiment (N1/N3/N4 above) already says what it will buy.
The eight legal programs the crude probe refused are all `regions-*` /
`enum-ref-*` shapes and are the reason a NAME must be MINTED rather than the
empty case simply refused.

## `ltenumpld` — 1 ROW, COST 0, AND THE TEXT COLUMN EARNED ITS PLACE

site: src/compiler/sema_decl.cpp::lower_enum_def (beside the outlives-clause check)
build: b5d34332f8de7107 (READ, `scripts/build_hash.py build`)
measured: 2026-08-30
fires: 29629 · ceiling 1 · cost 0 · cfail 1 (TEXT-ONLY) · stdlib all four layers
row: regions-in-enums — E0261, "enum 'No0': use of undeclared lifetime name ''foo'"

The struct field walk verbatim, over `einfo.variants[].payload_types`, with the
same `known()` and the same `'_` exemption. `cfail 1` is the FIRST time the
TEXT-ONLY column has been exercised by a known answer — two rounds of "still
unexercised" in this file: `logos_06_diagnostics_fail_regions-undeclared` gains
a second diagnostic, its `.expected` still matches, and ctest stays green
because `run_test.sh` greps `.expected` as a substring. Not a loss; recorded
because an invisible text change is exactly what that column was built for.

## `ltbindresv` — 2 ROWS, COST 0, AND ONE `.expected` TO RE-PIN

site: src/compiler/sema.cpp::read_lifetime_params (ONE site, every decl kind)
build: b5d34332f8de7107 (READ, `scripts/build_hash.py build`)
measured: 2026-08-30
fires: 3 · ceiling 2 · cost 0 · cfail 1 (`.expected` MATCH LOST) · stdlib clean
rows: regions-name-static (`struct Foo<'static>`, E0262) ·
      lifetime-no-keyword (`fn baz<'let>`)

The lost match is `logos_06_diagnostics_fail_generic-const-early-param`, whose
program is `struct DataWrapper<'static> { data: &'a i64 }` — upstream E0262 +
E0261. It became a fail fixture YESTERDAY on the E0261 half; `ltbindresv`
refuses it first for E0262, which is upstream's PRIMARY error. So this is a
diagnostic getting more correct, not rule 14: the fixture's `.expected` must be
re-pinned in the same change that lands the rule. Naming it here so the next
round meets a stated obligation rather than a red gate.

⚠ Only 3 fires across the whole acceptance population + legal corpus. Rule 4:
the cost-0 is off a TINY population and is not an argument. The reserved list
is written wide (39 names) precisely so that the abuse direction is cheap to
walk later; nothing in the corpus exercises it.

## `ltargarity` — THE CEILING IS 2 AND ONE OF THE TWO IS AN ACCIDENT

site: src/compiler/sema.cpp, before the `check_type_arg_arity` calls
fires: 14 · ceiling 2 · cost 5 · cfail 1 (`.expected` LOST) · stdlib clean

The five legal refusals and the lost diagnostic are ONE shape:

    enum Ast<'a> { Num(u64), Add(&'a Ast<'a>, &'a Ast<'a>) }

a self-referential enum naming itself inside its own payload, where
`esi->lifetime_params` is still EMPTY at the reference. `check_type_arg_arity`
documents that exact hazard for TYPE args and carves it out ("prepass /
forward-decl lookups may legitimately see an empty type_params list"); the
lifetime half of my probe has no such carve-out. `regions-creating-enums3`
(root `lifereg.NEW-N1`, not one of this round's 32) closes for that reason and
not for the arity rule — measured∖predicted is an ARTEFACT, and calling it a
purchase would have been the mistake rule 6 exists to catch.

So the honest reading is CEILING 1 (`noisy-follow-up-erro`) for the rule as
stated, plus a second site (the struct-literal turbofish) still to be found.
`ltargarity_site` says the outer arrival is populous — 1388 references to a
lifetime-declaring type, 14 reaching the inner test — so rule 4 is satisfied in
the direction that matters and the mechanism is worth a careful round with the
phase guard. NOT funded as written.

## COUNTER-EXAMPLES, WRITTEN BEFORE THE COSTS WERE BELIEVED (RULES 5 AND 10)

Each multi-line, each run; the refusals are POSITIVE controls that prove the
branch is reached, not merely that the file compiles.

    u3 enum E { V(&'b u64) }                 ltenumpld  REFUSED   (positive control)
    H6 enum Ok0<'a> { X5(&'a u64) }          ltenumpld  rc 0      legal
    e1 enum E { V(&i64) }                    ltenumpld  rc 0      elided ⇒ exempt, by design
    k1 struct Foo<'static>                   ltbindresv REFUSED   (positive control)
    k4 fn baz<'let>(a: &'let i64)            ltbindresv REFUSED   (positive control)
    k3 trait Ser<'self,T>                    ltbindresv rc 0      ← the trait hole, measured
    a2 &mut Foo<'a,'a,'a> (2 declared)       ltargarity REFUSED   (positive control)
    a3 &mut Foo<'a>       (2 declared)       ltargarity REFUSED   (positive control)
    N5 fn foo<'p,'q>(x:&mut Vec<Ref<'p>>, y:Ref<'q>) { x.push(y) }  rc 0 — A-2, method site

## WHAT DESERVES FUNDING OUT OF THESE 32

 1. **`ltenumpld` — 1 row, cost 0, cfail text-only.** Land it; re-run the text
    oracle and note the `regions-undeclared` addition in the same change.
 2. **`ltbindresv` — 2 rows, cost 0**, with the `generic-const-early-param`
    `.expected` re-pinned in the same change, and the TRAIT binder hole (k3)
    left named rather than guessed at — `keyword-self-lifetime-error-10412`
    does NOT close until a trait declaration's binders reach this function.
 3. **the `subtype.hpp` minting change — 3 rows measured by hand (N1/N3/N4),
    UNPRICED.** The single highest-value thing this survey found, and the
    probe that was supposed to price it measured the wrong door. Its door is
    `lt_eq` line 66 and `lifetime_at`'s `Inv` arm.
 4. **`ltargarity` with the prepass carve-out — 1 row honestly**, plus the
    struct-literal turbofish site, unfound.

## STILL OPEN, NAMED, NOT SPENT ON

 * **A-2, six rows** — the METHOD-CALL argument is not a `check_variance` site,
   and neither is UFCS `T::m(x, y)` (D5). Four of the six carry NAMED lifetimes
   and would close on the site alone.
 * **A-3, three rows** — a trait object's lifetime bound is dropped at the
   coercion; `Box<dyn Foo + 'static>` built from `&'v` admits under every probe
   in the tree (G1/G2). No site exists.
 * **A-4, one row** — a borrow expression carries no region.
 * **R17-e / R17-f, two rows** — no shadow rule and no E0207 rule anywhere;
   these are the only two of the 32 needing a rule that has no landed twin.
 * **the trait-declaration binder hole** (k3, `keyword-self-lifetime-error-10412`)
   and the **impl-header trait-ref lifetime args** (u5, `issue-107988`) — two
   sites, one row each, both unpriced because their arrival was uncertain and
   rule 17 says census before editing.
 * `lifereg_callargstrict` / `lifereg_structlitstrict` / `lifereg_unmentioned`
   were measured on 2026-08-27 (CEILING 4 / 3 / 7 against a 423-row ledger) and
   that measurement lives ONLY in a code comment — it was never in this file.
   Rule 8: the ledger is 310 now and those costs predate the repaired oracle.
   RE-MEASURE BEFORE FUNDING. Recorded here so the next round meets the numbers.

# ROUND 2026-08-31 — THE MINTING QUESTION, PRICED: THE DOORS ARE IN SERIES

Subject: the ONE thing 2026-08-30e left unpriced — the `subtype.hpp` minting
change. Rule 16 in its purest form. Also: `ltargarity` with the prepass
carve-out, and the three comment-only measurements from 2026-08-27 (rule 8).

build: 0dccf860bfd3373b (READ, `scripts/build_hash.py build`) · probe-batch
L1 rc=0, batch inert · the three re-measurements were taken on the UNCHANGED
build b5d34332f8de7107 before the batch touched a line.

## THE ARRIVAL CENSUS, BEFORE ANY GATE WAS EDITED (RULE 17)

    door                                        arrivals   empty-side
    lt_eq's empty early return (subtype.hpp:66)    28844        28844
      · both sides empty                                        13401
      · exactly one side empty                                    682
    lifetime_at's Variance::Inv arm                    9            0

**THE HANDED-DOWN DESCRIPTION OF DOOR 2 WAS WRONG, AND THE CENSUS SAYS SO IN
ONE LINE.** The round's brief said `lifetime_at`'s `Inv` arm "is on the path of
every `&mut` assignment". It is not: `MutRef` takes `Variance::Co` for its
lifetime and routes its POINTEE through `types_equal_with_lifetimes`, so the
`Inv` arm of `lifetime_at` is reachable only from a struct/enum lifetime ARG
whose variance-table entry is `Inv`. Nine arrivals in the whole acceptance
population + legal corpus, and **not one of them with an empty side**:
`ltinvempty_site` and `ltinvempty` are NEVER FIRED, which is not a zero. Rule
17's fourth instance, and the first where the census cost nothing to take
because it rode in the same build as the gate it was checking.

Door 1 is the opposite: 28844 arrivals, every one of them by definition with an
empty side. `lteqempty_site` is the outer name (rule 9), `lteqbothempty` and
`lteqoneempty` the two disjoint inner matches.

## THE PROBE TABLE, ALL THREE COST COLUMNS

    probe            fires  ceiling  cost  cfail  std  verdict
    lteqempty_site   28844        0     0      0   ok  the arrival census, door 1
    lteqbothempty    13401       13     2     14   ⛔  stdlib REFUSED (lang mem)
    lteqoneempty       682       34    26     35   ⛔  stdlib REFUSED
    ltinvarm_site        9        0     0      0   ok  the arrival census, door 2
    ltinvempty_site      0        —     —      —   —   NEVER FIRED
    ltinvempty           0        —     —      —   —   NEVER FIRED
    ltmintfresh      18497      157   650    574   ⛔  stdlib REFUSED
    ltargdecl            1        1     0      0   ok  ✓ FUND

`ltmintfresh` is the faithful crude proxy for "mint a fresh name in every
elided slot": an empty lifetime is related to NOTHING, including another empty
one — door 1 (any-empty), door 2 (any-empty) and BOTH of `outlives()`'s
empty-side exits at once.

## THE ROUND'S ANSWER: THE DOORS ARE IN SERIES, NOT IN PARALLEL

    ltelideboth (outlives alone, 2026-08-30)                 ceiling   0
    lteqbothempty ∪ lteqoneempty (lt_eq alone)               ceiling  45
    ltmintfresh (lt_eq AND outlives)                         ceiling 157

    ltmintfresh ∖ (lteqbothempty ∪ lteqoneempty)   =  112 rows
    (lteqbothempty ∪ lteqoneempty) ∖ ltmintfresh   =    0 rows

**112 rows are gated by the CONJUNCTION and by neither door alone.** That is
the mechanism behind yesterday's most useful negative: `ltelideboth` fired 14
times and closed 0 rows not because `outlives()`'s empty exits are dead, but
because `lt_eq` answers "equal" one frame ABOVE them and `outlives()` is never
consulted. Kill only `lt_eq` and the comparison falls through to
`lifetime_at(Co, "", "")` → `outlives()` → the same permissive exit. Both, and
the row refuses. Rule 11 without a multi-hop channel: two hops in SERIES, and a
probe at either one alone reports a true zero about a live mechanism.

⚠ `lteqbothempty ∩ lteqoneempty = 2` rows, though the two site predicates are
disjoint BY CONSTRUCTION. A ledger row is many comparisons; per-site counts are
not additive and a row can arrive in both shapes (rule 13).

## THE HAND PROGRAMS — POSITIVE CONTROLS AND THE COUNTER-EXAMPLE (RULES 5, 7, 10)

Each multi-line, each run against the armed binary, each with its unarmed
control on the same file.

    P1  struct Ref<'a,'b>; fn foo(x:&mut Ref, y:&i64){ x.b = y; }
        unarmed rc 0 · lteqbothempty rc 0 · ltmintfresh REFUSED
        "assignment to 'x.b': variance mismatch — expected &'b i64, …"
    P2  fn foo(p:&mut (&i64,&i64), x:&i64){ p.0 = x; }
        unarmed rc 0 · lteqbothempty rc 0 · ltmintfresh REFUSED
    C1  fn foo<'a>(x:&mut Ref2<'a>, y:&'a i64){ x.a = y; }   (NAMED, legal)
        ltmintfresh rc 0 — the probe does not simply refuse everything
    C2  fn id(x:&i64)->&i64 { return x; }                    (legal, elided)
        ltmintfresh REFUSED — **the counter-example that prices the fix.**

C2 is the whole of cost 650 in one program. Rust's elision rule 1 UNIFIES the
single input region with the output region; "an empty lifetime relates to
nothing" mints a fresh name at each slot and never unifies them. **A crude
probe and a correct fix do not close the same programs (rule 7): the correct
change MINTS AND UNIFIES by the elision rules, and its cost is not 650.** The
157 is therefore a ceiling on the MINTING MECHANISM, not a forecast of the fix.

P1/P2 are the positive controls: `lteqbothempty` alone leaves them at rc 0 and
`ltmintfresh` refuses them with the right diagnostic at the right site. That is
the series result reproduced on two hand programs one probe apart.

## THE SETS, DIFFED BOTH WAYS

Predicted BY NAME before the batch (written to /tmp/predictions.txt first):

    lteqbothempty  predicted {ex3-both-anon-regions-one-is-struct,
                              ex3-both-anon-regions-2, regions-infer-at-fn-not-param}
                   measured  13 rows, of which 2 are subject rows:
                             {ex3-both-anon-regions-2,
                              ex3-both-anon-regions-latebound-regions}
                   predicted∖measured {ex3-both-anon-regions-one-is-struct,
                                       regions-infer-at-fn-not-param}
                   measured∖predicted 11 rows, ALL of them borrowck/nll roots
                   outside the 32 — the probe closes them for a reason that has
                   nothing to do with elision, and 14 pinned `.expected`s LOST
                   against 13 rows closed. Not a mechanism; a blunt instrument.

    ltmintfresh    predicted {ex3-both-anon-regions-one-is-struct,
                              ex3-both-anon-regions-2,
                              regions-infer-at-fn-not-param, regions-addr-of-self}
                   measured  157 rows, of which 11 are subject rows:
                     constructor-lifetime-early-binding-error · ex2e-push-inference-variable-3
                     · ex3-both-anon-regions-2 · ex3-both-anon-regions-latebound-regions
                     · ex3-both-anon-regions-one-is-struct
                     · ex3-both-anon-regions-one-is-struct-4
                     · issue-103582-hint-type-alias · iterator-trait-lifetime-error-13058
                     · missing-lifetime-in-assoc-type-2 · regions-addr-of-self
                     · regions-close-object-into-object-1
                   predicted∖measured {regions-infer-at-fn-not-param} — and it
                   names a THIRD DOOR, below.
                   measured∖predicted 8 subject rows + 146 others. Two of the
                   eight are `lifereg.R17` DECLARATION-site rows
                   (constructor-lifetime-early-binding-error,
                   missing-lifetime-in-assoc-type-2) that this mechanism has no
                   business closing: rule 6/rule 7 artefacts, counted here and
                   claimed as nothing.

    ltargdecl      predicted {noisy-follow-up-erro}
                   measured  {noisy-follow-up-erro}
                   both differences ∅, and `regions-creating-enums3` — the
                   2026-08-30 ARTEFACT — is GONE.

## THE THIRD DOOR, FOUND BY A PREDICTION THAT MISSED

`regions-infer-at-fn-not-param` was refused BY HAND yesterday once its elided
slot was named (N4), and it is closed by NO probe in this batch. Re-run today,
one token apart:

    fn take1<'a,'b>(p: Parameterized1<'b>) -> Parameterized1<'a> { return p; }
      unarmed REFUSED  "return type mismatch: variance mismatch"
    fn take1<'a>(p: Parameterized1) -> Parameterized1<'a> { return p; }
      unarmed rc 0, and rc 0 under lteqbothempty / lteqoneempty / ltmintfresh

The elided reference carries ZERO lifetime args, the annotation carries one, and
BOTH comparators bail on the arity mismatch before any lifetime is compared:

  * `types_equal_with_lifetimes`, Struct/Enum case: `if (alts.size() !=
    blts.size()) return false;` — "not equal", which is correct;
  * `subtype`, Struct case: `if (sl.size() != pl.size()) return true;` —
    "compatible", a SHAPE guard that admits.

So door 3 is a lifetime-arg ARITY guard, not a lifetime comparison at all, and
minting fixes it by construction (an elided `Parameterized1` would carry one
fresh region and the sizes would match). Named, unpriced, and it is where the
next round's edit goes together with door 1.

## `ltargdecl` — THE CARVE-OUT, AND THE ARTEFACT IS GONE

site: src/compiler/sema.cpp::resolve_type_generic_inst (beside the `ltargarity` probe)
build: 0dccf860bfd3373b (READ) · measured 2026-08-31
fires: 1 · ceiling 1 · cost 0 · cfail 0 · stdlib all four layers
row: noisy-follow-up-erro

One variable against `ltargarity`: `!decl_lts->empty()`, the lifetime half of
the carve-out `check_type_arg_arity` already documents for TYPE args. Controls,
one token apart on the same binary:

    A1  struct Foo<'c,'d>; fn take<'a>(x:&mut Foo<'a>)
        ltargarity REFUSED · ltargdecl REFUSED   (positive control)
    A2  enum Ast<'a> { Num(u64), Add(&'a Ast<'a>, &'a Ast<'a>) }
        ltargarity REFUSED  ← the 2026-08-30 artefact, `lifetime_params` still
                              empty during the enum's own prepass
        ltargdecl  rc 0     ← the carve-out, exactly and only

`ltargarity`'s 5 legal refusals and its lost `.expected` are all that shape, and
they are all gone: cost 5 → 0, cfail 1 → 0, ceiling 2 → 1. The honest reading of
2026-08-30 ("CEILING 1 for the rule as stated") is now the MEASURED reading.

⚠ RULE 4, LOUDLY: **1 fire.** A ceiling off a one-fire population is not a
refutation and not an argument. `ltargarity_site` measured the outer arrival at
1388 on 2026-08-30, so the site is populous; it is the INNER match that is rare.
Say both numbers when this lands.

The second `lifereg.R17-c` row, `constructor-lifetime-early-binding-error`, is
still not closed here: the turbofish `S::<'static> { .. }` at a struct LITERAL
does not route through `resolve_type_generic_inst`. Still a second site, still
unfound. (It appears in `ltmintfresh`'s 157 — for an unrelated reason.)

## THE THREE RE-MEASURED NUMBERS (RULE 8) — 114 LEDGER ROWS STALE, COMMENT-ONLY

Measured 2026-08-27 against a 423-row ledger, recorded ONLY in a code comment
in `sema_impl.hpp` and `outlives.hpp`, never in this file. Re-measured
2026-08-31 on build b5d34332f8de7107 against the 310-row ledger, with the
repaired three-population oracle:

    probe                     fires   ceiling        cost      cfail  std
    lifereg_callargstrict    245664   4  (was 4)   7 (was 2)     1    ok
    lifereg_structlitstrict  245664   3  (was 3)   3 (was 0)     0    ok
    lifereg_unmentioned          18   7  (was 7)   5 (was 2)     1    ok

**THE CEILINGS DID NOT DECAY** across 114 deleted ledger rows — all seven rows
survive, and they are the same seven names. The COSTS all grew, and that growth
is the ORACLE, not the tree: the 2026-08-27 numbers were pass-only (rule 5), and
two of the three now show a `cfail` the old population could not see.

The 2026-08-27 adjudication still holds exactly: unmentioned's 7 =
callargstrict's 4 ⊎ structlitstrict's 3, intersection 0, same names.

    unmentioned  issue-55394--ctl2 · issue-67007-escaping-data
                 · propagate-fail-to-approximate-longer-no-bounds
                 · regions-infer-call-3                          (= callargstrict)
                 · projection-no-regions-closure--c30-direct-call
                 · projection-no-regions-closure--projection-no-regions-closure
                 · account-for-lifetimes-in-closure-suggestion   (= structlitstrict)

**AND THE COST IS NOT ADDITIVE IN THE OTHER DIRECTION**: 7 + 3 = 10 legal
refusals for the two doors, but only 5 for the single line upstream of both.
Forcing `permissive=false` at a call site disables MORE than this tail — it also
turns off the `L.empty()` exit and the `!permissive_empty` early return. So the
upstream line is the STRICTLY MILDER mechanism with the STRICTLY LARGER ceiling:
7 rows for 5 legal refusals versus 4 for 7 and 3 for 3. That reverses which of
the three is fundable, and only the re-measurement could show it.

None of these seven rows is in `lifereg.A` or `lifereg.R17`.

## ⇒ WHAT DESERVES FUNDING

 1. **THE MINTING CHANGE — FUND IT, AT DOORS 1 AND 3, AND NOT AS THIS PROBE
    SPELLS IT.** Ceiling 157 rows (11 of the 32 subject rows) against 310, and
    the two doors are in SERIES so both must move in one change. The crude
    proxy's cost 650 is C2 — `fn id(x:&i64)->&i64` — repeated: the fix MINTS
    AND UNIFIES by Rust's elision rules rather than declaring `""` unrelated to
    everything. The measurement that justifies the round is the series result
    (0 → 45 → 157), not the 157 alone.
 2. **`ltargdecl` — 1 row, cost 0, cfail 0, stdlib ok, artefact gone.** Land it
    with both numbers stated (1 inner fire, 1388 outer arrivals).
 3. **`lifereg_unmentioned` — 7 rows for 5 legal refusals**, and its two
    downstream doors should NOT be funded: they cost more and buy less. This is
    a re-measurement REVERSING a standing recommendation, not confirming it.
 4. `ltenumpld` and `ltbindresv` from 2026-08-30e are unchanged and still
    fundable; nothing this round touched them.

## STILL OPEN, NAMED

 * **DOOR 2 IS NEARLY DEAD** — 9 arrivals, 0 with an empty side. Do not spend a
   round on `lifetime_at`'s `Inv` arm; the census is the argument.
 * **DOOR 3**, the lifetime-arg arity SHAPE guard in `subtype`'s Struct case
   (`if (sl.size() != pl.size()) return true;`) — unpriced, and it is the only
   door that can close `regions-infer-at-fn-not-param`.
 * the struct-literal turbofish site for `lifereg.R17-c` — still unfound.
 * A-2 (6 rows, the method-call/UFCS comparison site), A-3 (3 rows, trait-object
   bound dropped), R17-e / R17-f (no landed twin), the trait-declaration binder
   hole (k3) and the impl-header trait-ref lifetime args (u5) — all carried
   forward from 2026-08-30e, none spent on.


# ═══ 2026-08-31f — THREE DECLARATION-SITE RULES LANDED, LEDGER 310 → 306 ═════

The FUND phase for the three arms the surveys of 2026-08-30e and 2026-08-31
had already earned, and the DECLINE, by name and with the number, of everything
else in the `lifereg.A` / `lifereg.R17` partition. No new probe was armed this
round: every number below was measured on the LANDED tree, which is the only
tree whose cost is the fix's cost (rule 7).

## PREDICTED vs MEASURED, AS SETS, BOTH WAYS

Declared before the first edit, in the round's prediction file:

    mechanism      predicted ceiling            measured           diff
    ltenumpld      {regions-in-enums}           same               ∅ / ∅
    ltbindresv     {regions-name-static,        same               ∅ / ∅
                    lifetime-no-keyword}
    ltargdecl      {noisy-follow-up-erro}       same               ∅ / ∅

Union 4 rows, and the ledger gate agrees by direct listing: 310 → 306.
`regions-in-enums-anon` was named in advance as NOT closing (`'_`, exempt by
design) and did not close. `regions-creating-enums3` — the 2026-08-30 prepass
ARTEFACT — was named in advance as MUST NOT close under the carve-out, and did
not. `constructor-lifetime-early-binding-error` was named in advance as NOT
closing (struct-literal turbofish, a second site) and did not.

Every predicted∖measured and measured∖predicted difference is empty, on the
ledger half AND on the three cost populations. That is the first round in this
file where the prediction was exact in both directions on all three arms; it is
also the round with the least mechanism per arm, which is the honest reading.

## COST, ALL THREE POPULATIONS, ON THE LANDED TREE (rule 5)

    population                       result
    ledger  -R '^logos_00_bc_admit_' 306 passed / 0 failed  (store build 213)
    legal   -L bc -L pass            884 passed / 0 failed / 2 disabled
    legal   25_spec|03_ownership|04_advanced_pass  190 passed / 0 failed
    stdlib  scripts/stdlib-cost.sh   all four layers compile
    fail    fail_text_oracle.py      1044 fixtures, diffed against the CONTROL
                                     REVERT baseline — see the two columns below

COST(pass) = 0 · COST(stdlib) = 0. The fail-text oracle, rc column and text
column reported SEPARATELY (rule 15):

    rc changes                 7 — all seven are this round's OWN new fail
                                   fixtures going 0 → 1. No pre-existing
                                   fixture's rc moved.
    stderr-sha changes         9 — the seven above, plus TWO:
      · regions-undeclared          TEXT ONLY, `.expected` still matches. The
                                    enum `EnumDecl` in that program was
                                    previously unchecked and now raises E0261
                                    ahead of the fn diagnostic the pin names.
                                    A diagnostic getting MORE correct, and the
                                    first time this column has been exercised
                                    by a KNOWN answer (predicted, 2026-08-30e).
      · generic-const-early-param   `.expected` MATCH LOST (1 → 0), predicted,
                                    and RE-PINNED in this same change: the
                                    program is `struct DataWrapper<'static> {
                                    data: &'a i64 }` and the reserved-binder
                                    rule refuses the E0262 FIRST, which is
                                    upstream's PRIMARY error. Rule 14 resolving
                                    the good way, not a weakened pin.
    .expected-match changes    8 — the seven new fixtures (0 → 1) and the one
                                   re-pin above (1 → 0). Nothing else moved.

## THE HAND PROGRAMS, RE-RUN ON THE LANDED BINARY (rules 10 and 2)

Each multi-line, each compiled by hand, refusals are POSITIVE controls that the
branch is REACHED and not merely that the file builds:

    u3  enum E { V(&'b u64) }                 REFUSED  "enum 'E': use of
                                                        undeclared lifetime ''b'"
    h6  enum Ok0<'a> { X5(&'a u64) }          rc 0     the named twin
    e1  enum E1 { V(&i64) }                   rc 0     elided ⇒ exempt, by design
    k1  struct Foo<'static>                   REFUSED  E0262
    k4  fn baz<'let>(a: &'let i64)            REFUSED  keyword binder
    k3  trait Ser<'self, T> { .. }            rc 0     ← THE TRAIT HOLE, STILL OPEN
    a1  fn take<'a>(x:&mut Foo<'a>), 2 declared  REFUSED  "expected 2, got 1"
    a2  enum Ast<'a> { Add(&'a Ast<'a>, ..) } rc 0     ← THE PREPASS CARVE-OUT
    h7  fn take2<'a,'b>(x:&Bar<'a,'b>)        rc 0     the arity-matching twin

k3 and a2 are the two measurements that matter most here, and both are NEGATIVE:
the rule stops exactly where the survey said it would.

## WHERE EACH FIX DIFFERS FROM ITS PROBE

 * `ltenumpld` — identical. The probe body was the struct field walk verbatim
   over `einfo.variants[].payload_types`; landing it removed the `probe::on`
   guard and nothing else. Ceiling 1, cost 0, cfail 1 TEXT-ONLY: all three
   reproduced exactly.
 * `ltbindresv` — identical, guard removed. ⚠ RULE 4, SAID OUT LOUD AS
   PROMISED: this arm fired **3 times** in the whole acceptance population plus
   legal corpus. Its cost 0 is off a TINY population and is not an argument for
   anything. It is landed on the ARM BEING MISSING — a lifetime binder spelled
   `'static` is E0262 in every Rust edition — and not on the number.
 * `ltargdecl` — the probe was `ltargarity && !decl_lts->empty()`; the landed
   rule folds the carve-out into the condition itself and DELETES the
   `ltargarity` spelling with its 5 legal refusals. ⚠ 1 inner fire against
   **1388 outer arrivals** (the `ltargarity_site` census of 2026-08-30, now
   removed from the tree and recorded here instead). Both numbers, as promised.

## WHAT WAS DECLINED, BY NAME, WITH THE NUMBER THAT CONDEMNS IT

 * **the `subtype.hpp` minting change (doors 1 + 3)** — ceiling 157/310, and
   DECLINED THIS ROUND on cost: the crude proxy `ltmintfresh` refuses **650**
   legal programs, loses **574** pinned diagnostics and fails to build the
   stdlib. The counter-example is one line — `fn id(x:&i64)->&i64 { return x; }`
   — and it is not a probe artefact that a guard removes: it says the mechanism
   must MINT AND UNIFY by Rust's elision rules, which is an elision engine, not
   an edit. It stays the highest-value finding in this file and it is a ROUND OF
   ITS OWN, not a rider on three declaration-site rules. The series measurement
   (0 → 45 → 157) is what justifies funding it; nothing this round changes it.
 * `lteqbothempty` — 13 rows, cost 2, **cfail 14**, stdlib REFUSED. More pinned
   diagnostics lost than rows bought.
 * `lteqoneempty` — 34 rows, cost 26, cfail 35, stdlib REFUSED.
 * `ltargarity` as written — superseded and DELETED; ltargdecl is the same row
   for cost 0 instead of 5.
 * **door 2** (`lifetime_at`'s `Variance::Inv` arm) — 9 arrivals, 0 with an
   empty side, `ltinvempty` NEVER FIRED. Not a zero; an absence. Do not spend a
   round there.
 * `lifereg_callargstrict` (4 rows / cost 7) and `lifereg_structlitstrict`
   (3 / 3) — still declined in favour of `lifereg_unmentioned` (7 rows / cost 5),
   which remains FUNDABLE and unspent: it is the milder mechanism with the
   larger ceiling, re-measured 2026-08-31 and unchanged across 114 deleted rows.

## THE FIXTURES, IN PAIRS, ONE TOKEN APART

Relanded in their own root's home, diagnostics pinned in full (no `'?'`):

    tests/imported/fail/regions/regions-in-enums          (+ .expected)
    tests/imported/fail/regions/regions-name-static       (+ .expected)
    tests/imported/fail/lifetimes/lifetime-no-keyword     (+ .expected)
    tests/imported/fail/lifetimes/noisy-follow-up-erro    (+ .expected)

Native pairs, each fail fixture ONE TOKEN from its pass twin:

    fail/bc_enumpldlt_undeclared_payload_lifetime  ⟂ pass/bc_enumpldlt_declared_payload_lifetime
                                                     pass/bc_enumpldlt_placeholder_payload_lifetime
    fail/bc_ltbindresv_reserved_binder_name        ⟂ pass/bc_ltbindresv_ordinary_binder_name
    fail/bc_ltargdecl_lifetime_arg_arity           ⟂ pass/bc_ltargdecl_lifetime_arg_arity_match
                                                     pass/bc_ltargdecl_selfref_enum_prepass

The two extra PASS fixtures are exemptions pinned rather than argued: `'_` and
elision in an enum payload, and the prepass carve-out. An exemption that is only
a sentence in a comment is the thing rule 8 was bought with.

## CONTROL REVERT

With `src/compiler/sema.cpp` and `src/compiler/sema_decl.cpp` at HEAD and the
fixtures in place, ALL SEVEN new fail fixtures go RED and all six new pass
fixtures stay green. That is also where the unarmed fail-text baseline was
taken, so the revert paid for itself twice.

## ⚠ A GATE LIE, 16th KIND: THE STORE ANSWERED FOR A TEST SOURCE IT NEVER READ

`gate-run.sh` keys its verdicts on `build_hash.py`, which hashes the COMPILER.
Three new pass fixtures failed (their `main` returned the value it computed, so
the runner saw exit 7); the fixtures were edited; the re-run reported *"Nothing
has changed that a test run could see"* and re-served the FAILING verdicts. The
compiler had not moved, so the key had not moved — and a FIXTURE edit is
invisible to it. `FORCE=1` is the documented escape and it was used. Recording
it because a stale green would be the same mechanism with the sign flipped.

**AND THE SIGN DID FLIP, ON THE FINAL GATE, IN THIS SAME ROUND.** The first
`test-levels.sh L4 bc` after the three census pins were re-derived exited
**RC=0** while printing three FAILED rows from the store — census_pin,
population_pin_lint and direct_door_census, all three fixed minutes earlier. The
pins live in `docs/` and `tests/`, so `build_hash.py` had not moved, so nothing
was re-run, so the level had nothing to report and reported success. A gate's rc
is a measurement WITH A TIMESTAMP: the honest L4 is the `FORCE=1` one below, and
an unforced L4 after a TEST-SOURCE edit is not evidence at all.

    L4 bc, unforced   RC=0, 5824 recorded, 3 failed  ← the lie
    L4 bc, FORCE=1    RC=0, 4468 + 1354 passed, 0 failed, 2 disabled  ← the gate

## STILL OPEN, NAMED, WITH ITS EVIDENCE

 * **the TRAIT-declaration binder hole** — `keyword-self-lifetime-error-10412`
   stays admitted. `'self` is a TRAIT's binder and a trait declaration's
   lifetime params never reach `read_lifetime_params`, the one site every other
   declaration kind uses. MEASURED on the landed binary (k3 above), not
   inferred. Rule 17: census the arrival before editing a gate for it.
 * the struct-literal turbofish site (`S::<'static> { .. }`) for
   `constructor-lifetime-early-binding-error` — a second arity site, unfound.
 * `static` declarations and TRAIT METHOD SIGNATURES — the remaining halves of
   the E0261 site set the struct field walk named. The enum payload half is
   closed as of today; those two are not.
 * A-2 (6 rows, the method-call / UFCS comparison site), A-3 (3 rows, a trait
   object's `+ 'static` bound dropped entirely), A-4, R17-e (E0496 shadowed
   binder), R17-f (E0207 unconstrained impl param), R17-g (a name-resolution
   site, not a lifetime question) — carried forward, unspent.
 * every diagnostic raised at a DECLARATION still carries a wrong line and a
   wrong `[fn ...]` context (`regions-in-enums` reports `:557: [fn
   iter_partition_vec]`). Pre-existing, shared with the landed struct walk and
   the outlives check, one site, no row — and it is now in FOUR pinned
   fixtures' output rather than one.


# ═══ 2026-08-31g — 27 `nllmoves` ROWS SURVEYED; THREE COST-0 RECOMMENDATIONS REVERSED ═══

Subject 1: the `nllmoves.C` 16 + `nllmoves.B` 11 survey. Subject 2: the
narrowing of `lifereg_unmentioned`. No fix landed — this round PRICES.

build: c4dedf97e7aee29a (READ, `scripts/build_hash.py build`; the two rule-8
re-prices below were taken FIRST, on the unchanged 8f9af403df86f891) ·
probe-batch: 9 edits, ONE build, **L1 rc=0, batch inert** · ledger TOTAL 306.

## ⚠ THE BRIEF SAID "NEVER SURVEYED". ELEVEN OF THE 27 HAD BEEN.

All ELEVEN `nllmoves.B` rows are already partitioned by the 2026-08-29
`bck.B` 21 + `nllmoves.B` 13 survey, above. Not one needs a new name. Re-verified
today on the unarmed binary with four one-token controls, each multi-line:

    b1  let q = *r;                     REFUSED E0507   ⟂ match r { &q => } admitted   (B-1b)
    x1  let f: &'static i64 = &a;       rc 0            (X-1)
    x1b struct Foo{x:&'static i64}; Foo{x:&a}  rc 0     (X-1, the OTHER annotation site)
    x2  p = &y;                         REFUSED E0597   ⟂ q=&mut p; *q=&y admitted     (X-2)
    b8  let foo=&mut o; a=foo; b=foo;   REFUSED         ⟂ ref mut foo => admitted      (B-8)

    B-1b the `&`-PATTERN does the deref                       2  do-not-suggest-…-132806, issue-99470-move-out-of-some
    B-2  the destructure temp discards the pattern's move-ness 1  move-errors--d          (`destrmove` 2/1)
    B-3  the place walk breaks at a `Deref` CALL               1  issue-52086
    B-8  a `ref`/`ref mut` binding is not a borrow-HOLDER      1  issue-27282-mutation-in-guard
    B-9  a guard's view of the scrutinee is shared-only        1  match-guards-always-borrow
    X-1  `'static` in an ANNOTATION constrains nothing         3  adt-brace-enums, issue-46036, lub-match
    X-2  a write through `&mut &T` misses the pointee          2  capture-ref-in-struct--ctl, --t08

X-1 is not a bookkeeping question at all: BOTH annotation sites admit, so these
three belong to the **DECLINED minting round** (`ltmintfresh`, ceiling 157) and
not to `nllmoves`. X-2 joins `escape-argument--t09`'s partition (the call-site
write summary), which now has THREE rows on it, not one.

## THE `nllmoves.C` PARTITION — 16 ROWS BY MISSING OBSERVATION

**C-I. THE CLOSURE'S OWN SIGNATURE CONTRACT IS NEVER CHECKED (5).**
`ret_type_`, `param_lifetimes_` and `param_names_` are all the ENCLOSING fn's,
and `check_return_value` is hard-suppressed by `if (!in_closure_body_)`.
 · I-a a dangling BODY LOCAL (1) — nested-bodies-in-dead-code.
   ⚠ **AND ITS ROOT LABEL IS WRONG.** Measured, one statement apart:

       n1  let c = || -> &i64 { let temp:i64=1; return &temp; }; let _=c();   rc 0
       n2  return 0i32;  <the same three lines>                               rc 0
       n3  fn mkref() -> &i64 { let z:i64=3; return &z; }                     REFUSED

   Dead code is NOT the observation — the live twin admits identically, and the
   FN twin is refused today. The discriminator is a NODE KIND.
 · I-b `-> &'static` from an elided `&` param (2) — closure-substs, issue-58053.
   **NEW, and it CORRECTS the 2026-08-29 grouping**, which put both in group G
   ("genuinely region, not fundable today"). One token apart, unarmed:

       s1  fn  g(x:&i64) -> &'static i64 { return x; }
           REFUSED "lifetime mismatch: return type has lifetime 'static but 'x' has lifetime (elided)"
       s2  let c = |x:&i64| -> &'static i64 { return x; };            rc 0

   The rule EXISTS and lands on fns. The closure never reaches it.
 · I-c bought ONLY by a legal-program refusal (2) — issue-40510-1,
   issue-48697--t16. Both sit in `capretchk`'s 11 and NOT in `capretcaps`'s 2,
   measured 2026-08-28. **DECLINED with the number**; not re-opened.

**C-II. A `move` CLOSURE'S BODY IS NEVER WALKED (1).** issue-48238.
`walk_closure_body` returns at `if (cbv.is_move()) return;`. Rule 16: this is
ZERO ARRIVALS, not a permissive verdict. And `capretcaps`'s cause-B exemption
(every capture into `outliving_params_`) is INVERTED for a move closure — a
moved capture is the CLOSURE's local, not the enclosing frame's.

**C-III. A CAPTURE-BY-REF IS NOT A LOAN IN THE ENCLOSING FRAME (5).**
closure-borrow-spans--a, --b, issue-42574--b, issue-51268, issue-40510-3.
Control, one token — the closure:

    d4  let g=&x; let y=&mut x;              REFUSED "1 shared borrow(s) active"
    d3  let f=||{let n=x;}; let y=&mut x;    rc 0

--a and --b are `capshared`'s (re-priced below). issue-51268 is NOT in this
partition after the census — see C-VII.

**C-IV. MOVE-vs-LOAN AT THE CAPTURE (3).** issue-75904-move-closure-loop,
issue-52663-span-decl-captured-variable, issue-42574--t15. Existing probes
`capmoveloan` 1 row, `capescmove` 1 row.

**C-V. THE CLOSURE-BODY WRITE SUMMARY (1).** escape-argument--t09 — group F,
and it now shares its partition with `nllmoves.B`'s X-2 (2 rows) and
`regions-escape-bound-fn`. FOUR rows on one unbuilt mechanism.

**C-VI. HRTB (1).** return-wrong-bound-region — `for<'a> Fn(&'a i64,&i64)->&'a i64`.
No HRTB machinery exists. **HONEST RETIREMENT**: not fundable by anyone this week.

**C-VII. THE CLOSURE ARGUMENT DEPOSITS NOTHING (issue-51268, moved out of C-III).**
Census by hand with `LOGOS_DUMP_BC_CAPTURE`, one token apart:

    e3  b.bar(|| { let n: i64 = x; });               NO [bc-capture] LINE AT ALL
    e4  let c = || { let n: i64 = x; }; b.bar(c);    [bc-capture] root=x holder=c
    e6  call(|| { return x + 1i64; })                NO LINE
    issue-51268                                      NO LINE

Rule 16 again: "no fact recorded" and "the fact is absent" are different. See
`capargclos` below — and its census REFUTES the site this survey nominated.

## THE PROBE TABLE, ALL THREE COST COLUMNS

    probe               fires  ceiling  cost  cfail  std  verdict
    capretplt         1828682        5     0      4   ok  ⛔ the 4 are rc FLIPS — see below
    capmovewalk       1828706        4     0      4   ok  ⛔ same four, same cause
    capargclos              8        0     0      0   ok  ✗ NEVER FIRED ON ITS OWN SUBJECT
    lifereg_unmentbind      4        5     0      0   ok  ✓ FUND — 5 rows for cost 0
    ── rule-8 re-prices, taken FIRST on build 8f9af403df86f891, nothing armed ──
    capshared             163        4     0      6   ok  ⛔ 4 `.expected` LOST + 2 text-only
    capretcaps        5492361        2     0      4   ok  ⛔ 4 fail fixtures rc 1 → 0

⚠ **EVERY ROW ABOVE WAS PRICED TWICE** (nine spec records, four names). The two
pricings agreed on ceiling, cost, cfail and stdlib for every name — a free
reproducibility pole. They did NOT agree on `fires`: capretplt read 5492361 on
its first pricing and 1828682 on its second. **A FIRE COUNT FROM A RE-PRICED RUN
IS NOT COMPARABLE TO THE FIRST**: the pass half comes from the store the second
time, so fewer compiles ran. Only the first pricing's fire count is a population.

## ⚠ THE ROUND'S HEADLINE: THREE COST-0 RECOMMENDATIONS REVERSED BY POPULATION 2

`capretcaps` has stood since 2026-08-28 as "CEILING 2, COST 0, fundable", and
`capshared` since 2026-08-27 as "CEILING 4, COST 0". Both numbers were measured
over PASSING TESTS ALONE (rule 5). Re-priced today with the three-population
oracle, ceilings UNCHANGED (2 and 4, same names, across a 349→306 and a 368→306
ledger shrink — **the ceilings did not decay**), and:

    capretcaps   4 `-L bc -L fail` fixtures go rc 1 → rc 0
                   escape-argument--b · escape-upvar-nested · escape-upvar-ref
                   · regions-nested-fns
    capshared    4 `.expected` matches LOST + 2 text-only rewordings
                   arc-consumed-in-looped-closure · borrowck-in-static--move-
                   captured-out-of-fn-closure · --r-runtime · closure-move-spans
                   (TEXT: closure-access-spans--a-…, closures-in-loops)

An rc flip is not a rewording. **`capretcaps` UN-REFUSES four programs that are
refused today**, and the pass corpus cannot see that by construction. Diagnosed
by hand on the landed-probe binary:

    tests/imported/fail/nll/escape-upvar-ref.logos
      let mut p:&i64 = &x; { let y:i64=22; let c = || { p = &y; }; c(); } deref(p);
      unarmed     REFUSED "'y' does not live long enough … (E0597)"
      capretcaps  rc 0
      capretplt   rc 0

CAUSE, read not guessed: cause B puts EVERY capture into `outliving_params_`,
and `outliving_params_` is read by the ESCAPE/DANGLING channel as well as by the
return check it was written for. Two notions of one concept, and the exemption
was only ever checked in the direction that helps. The repair is named and is
NOT an edit to the exemption's condition: the capture exemption must be scoped
to the RETURN check, not deposited in a set three other readers consult.
**`capretcaps`, `capretplt` and `capmovewalk` are all DECLINED until it is.**

## capretplt — 5 ROWS, AND THE PREDICTION WAS EXACT ON ITS OWN CLAIM
site: src/compiler/borrow_check.cpp::walk_closure_body (+ the Return gate)
build: c4dedf97e7aee29a (READ) · measured 2026-08-31
fires: 1828682 (first pricing 5492361 — see the re-pricing note above)
ceiling: 5 · cost: 0 · cfail: 4 (rc flips, inherited) · stdlib: all four layers

One variable against `capretcaps`: cause A only CLEARS the enclosing fn's
`param_lifetimes_`; capretplt REBINDS them from the closure's OWN ref params.

    predicted {issue-53432-nested-closure-outlives-borrowed-value, closure-substs,
               issue-58053, nested-bodies-in-dead-code}
    measured  the same four PLUS regions/regions-nested-fns-2
    predicted∖measured  ∅
    measured∖predicted  {regions-nested-fns-2}
    predicted NOT to close, and did not:  issue-40510-1 · issue-48697--t16
                (both are capretchk-minus-capretcaps rows — a legal refusal)

DECOMPOSED, because a per-site count is not additive (rule 13):

    capretcaps            2   issue-53432 · nested-bodies-in-dead-code
    capretplt ∖ capretcaps 3  closure-substs · issue-58053 · regions-nested-fns-2
    capmovewalk ∖ capretcaps 2  borrowck-multiple-captures · issue-48238
    union                 6

⚠ `capmovewalk` IS NOT A ONE-VARIABLE PROBE and its 4 must not be read as four
move-body rows: it also turns the `retchk` gate on for every closure, so two of
its four are `capretcaps`'s own. Its clean increment is TWO.

## THE HAND PROGRAMS (rules 5, 7, 10) — each multi-line, each run

    s2  let c = |x:&i64| -> &'static i64 { return x; };
        unarmed rc 0 · capretplt REFUSED, and with the SAME diagnostic s1's fn
        twin already gets unarmed. capmovewalk rc 0.
    s3  let c = |x:&i64| -> &i64 { return x; };            rc 0 under both  (legal)
    s4  fn foo<'a>(x:&'a i64)->&'a i64 { let c=|y:&i64|->&i64{return y;}; return x; }
        rc 0 under both — the rebind does NOT resurrect cause A, which is the
        over-refusal that cost `capretchk` three legal programs.
    n1  let c = || -> &i64 { let temp:i64=1; return &temp; };
        REFUSED under capretplt AND capmovewalk, "cannot return reference to
        local variable 'temp'"                                  (positive control)
    m1  let orig:i64=5; let c = move || -> &i64 { return &orig; };
        capretplt rc 0 (the move body is not walked) · capmovewalk REFUSED,
        "cannot return reference to local variable 'orig'"       (C-II, isolated)
    m3  let c = move || -> i64 { return orig; }; let v = c();
        rc 0 under both                                          (legal)

## capargclos — ✗ THE SITE WAS A HYPOTHESIS AND THE CENSUS KILLED IT (RULE 17)
site: src/compiler/borrow_check.cpp, the Call and MethodCall per-arg recursion guards
build: c4dedf97e7aee29a (READ) · measured 2026-08-31
fires: 8 · ceiling 0 · cost 0 · cfail 0 · stdlib ok

The guard reads `is_ref_kind(a.type) || (res_bc && is_borrow_carrying_type(a.type))
|| retains_borrowing_operand(a)`, and a bare `Kind::Closure` argument passes
none of the three. Adding `a.kind() == Code::ClosureBox` as a fourth disjunct
closes NOTHING — and the fire count says why twice over:

 * **8 arrivals in the whole acceptance + legal population.** Rule 4.
 * **ZERO of them are the defect.** Armed by hand with `LOGOS_PROBE_FIRE` on
   issue-51268, on e3 (`b.bar(||{…x…})`) and on e6 (`call(||{…x…})`), the fire
   count is **0** on all three, and no `[bc-capture]` line appears either.

RULE 2 IN ITS PUREST FORM: the site is provably live and the decision is taken
in a frame ABOVE it. A closure literal in a call argument never reaches this
per-arg loop at all, because the STATEMENT that owns the call (`let r: i64 =
call(…)`, result type not borrow-carrying) never routes the initialiser into
`take_ref_borrows`. **The next question is the LET/statement gate, not these two
arms.** Fifth instance this week of a nominated site set being wrong; the census
rode in the same build as the gate it checked and cost nothing.

## lifereg_unmentbind — ✓ THE NARROWING: 5 ROWS FOR COST 0, cfail 0
site: include/logos/compiler/outlives.hpp (the single `return true` tail)
      carrier: include/logos/compiler/outlives.hpp::current_lt_binders(),
      filled in src/compiler/sema_decl.cpp beside `current_outlives_`
      ⚠ IT WAS probe.hpp::lt_binders() WHEN THIS RECORD WAS WRITTEN. The arm
      LANDED 2026-08-31h and the carrier moved with it.
build: c4dedf97e7aee29a (READ) · measured 2026-08-31
fires: 4 · ceiling 5 · cost 0 · cfail 0 · stdlib all four layers

    lifereg_unmentioned   fires 18 · ceiling 7 · cost 5 · cfail 1 · stdlib ok
    lifereg_unmentbind    fires  4 · ceiling 5 · cost 0 · cfail 0 · stdlib ok

    predicted {issue-55394--ctl2, issue-67007-escaping-data,
               projection-no-regions-closure--c30-direct-call,
               projection-no-regions-closure--projection-no-regions-closure,
               account-for-lifetimes-in-closure-suggestion}
    measured  the same five
    predicted∖measured ∅   ·   measured∖predicted ∅
    predicted NOT to close, and did not:
        propagate-fail-to-approximate-longer-no-bounds  ('y is demand_y's binder)
        regions-infer-call-3                            ('r is select's binder)

**THE DISCRIMINATOR IS RULE 12: A SET OF STRINGS CANNOT SAY WHICH BINDING A NAME
DENOTES.** All FIVE of `lifereg_unmentioned`'s legal casualties compare a name
from ONE binder against a name from ANOTHER — a callee's or a struct's own
lifetime parameter that arrived UNSUBSTITUTED. The fact `outlives()` does not
carry is not a relation; it is which generic scope each name belongs to.
Counter-examples, written before the costs were believed, each multi-line:

    u1  fn mk<'a>(x:&'a i64)->S<&'a i64>;  fn f<'a,'b>(x:&'b i64)->S<&'a i64> { return mk(x); }
        unmentioned REFUSED · unmentbind REFUSED        (the ledger row's shape)
    u2  struct H<'a>{v:&'a i64};  fn g<'b>(x:&'b i64)->H<'b> { return H{v:x}; }
        unmentioned REFUSED  ← a LEGAL program · unmentbind rc 0
    u5  fn take<'a,'b>(x:&'a,y:&'b);  fn caller<'c,'d>(p:&'c,q:&'d) { return take(p,q); }
        unmentioned REFUSED (twice) ← LEGAL · unmentbind rc 0

⚠ **AND THE NARROWING IS NOT SOUND-BY-CONSTRUCTION. IT WORKS BY NAME COLLISION**,
which is the same defect wearing the other hat. ONE TOKEN APART, both equally
illegal in Rust:

    u7  struct Holder<'a>{v:&'a i64};  fn mk<'b,'a>(y:&'b i64)->Holder<'a> { return Holder{v:y}; }
        unarmed rc 0 · unmentbind REFUSED         ← the struct's binder is spelled 'a, so is mk's
    u8  struct Holder<'h>{v:&'h i64};  fn mk<'b,'a>(y:&'b i64)->Holder<'a> { return Holder{v:y}; }
        unarmed rc 0 · unmentbind rc 0            ← rename ONE token and it is missed

The two rows the narrowing loses say the same thing the u7/u8 pair says: the
correct mechanism SUBSTITUTES the callee's (or the struct's) binder against the
caller's actual regions and then compares two names of ONE binder. That is the
**same MINT-AND-UNIFY engine** the `subtype.hpp` minting round was declined for
on 2026-08-31. Two independent measurements now converge on it.

⚠ RULE 4, SAID OUT LOUD: `lifereg_unmentbind` fired **4 times** in the whole
acceptance population plus legal corpus (`lifereg_unmentioned`, the site
directly above it, fires 18). A ceiling off a four-fire population is not an
argument. What funds this arm is that it is STRICTLY MILDER than a line already
recommended — same site, five of the same seven rows, and it deletes all five
legal refusals and the one text regression.

## ⇒ WHAT DESERVES FUNDING

 1. **`lifereg_unmentbind` — 5 rows, cost 0, cfail 0, stdlib ok.** It SUPERSEDES
    the standing recommendation of `lifereg_unmentioned` (7 rows / cost 5 /
    cfail 1): two fewer rows, five fewer legal refusals, one fewer pin lost.
    Land it with both numbers and with the u7/u8 pair pinned as fixtures — the
    exemption must not be only a sentence in a comment.
 2. **The closure SIGNATURE contract (C-I), AFTER cause B is repaired.** Six
    rows are available at that site (capretcaps 2 ⊎ capretplt 3 ⊎ capmovewalk 2,
    minus the shared 1), and none of them may be bought while the exemption
    un-refuses four pinned fail fixtures. The repair is scoped, not conditional:
    the capture exemption belongs to the RETURN check alone.
 3. The **SUBSTITUTION engine** — named by two independent rounds now
    (`ltmintfresh`'s 157 and this round's two lost rows plus u7/u8). Still its
    own round.

## STILL OPEN, NAMED, WITH ITS EVIDENCE

 * **`capshared` is no longer a cost-0 arm** — 4 `.expected` LOST. Rule 14 says
   read all four before funding: they may be the capture-site diagnostic
   arriving ahead of the move-site one, which is upstream's canonical order and
   would be a repair, not a regression. NOT read this round.
 * **the LET/statement gate for a closure-literal call argument** (C-VII) —
   `capargclos` proved the two per-arg guards are the wrong frame. Census the
   statement gate before editing it.
 * **C-VI return-wrong-bound-region — RETIRED**, no HRTB machinery.
 * **C-I-c issue-40510-1 / issue-48697--t16 — DECLINED**, a legal refusal buys
   them and nothing else does.
 * `escape-argument--t09`'s partition now holds FOUR rows (X-2's two,
   escape-argument--t09, regions-escape-bound-fn) on ONE unbuilt mechanism.
 * ⚠ NEITHER `issue-51268` NOR `issue-42574--b` HAS AN ORACLE ON THIS BOX.
   Both look legal under RFC-2229 / NLL as ported (`self.thing.bar(||self.number)`
   is disjoint-field; `let c=||eat(data); c(); eat(data);` releases at `c`'s last
   use). rustc is not installed here, so this is a SUSPICION with a reason, not
   a retirement. Adjudicate against upstream before spending a round on either.

---

# ROUND 2026-08-31h — TWO ARMS LANDED, NINE ROWS, 306 -> 297

Baseline READ from the store, not re-run: build 218 (libs c4dedf97e7aee29a),
2432 recorded / 0 failed; the admit filter's 306 and `-L bc`'s 1935 were both
"ALREADY MEASURED under this build".

    mechanism            ceiling  cost  cfail  stdlib  predicted vs measured
    lifereg_unmentbind      5       0     0      ok    ∅ / ∅   LANDED
    capretsc                4       0     0      ok    ∅ / ∅   LANDED
    ── and the three it replaces, all DECLINED, unchanged from 2026-08-31g ──
    capretplt               5       0     4      ok    the 4 are rc FLIPS
    capretcaps              2       0     4      ok    the same four
    capmovewalk             4       0     4      ok    the same four
    capshared               4       0     6      ok    4 `.expected` LOST
    capargclos              0       0     0      ok    8 arrivals, 0 on subject

## capretsc — THE SCOPING WAS THE WHOLE PRICE
site: src/compiler/borrow_check.cpp::walk_closure_body (+ the Return gate,
      + check_return_value's report site)
carrier: src/compiler/borrow_check.cpp::closure_capture_names_
build: 571876f6ef48a1ed (READ) · measured 2026-08-31
fires: 5492473 · ceiling 4 · cost 0 · cfail 0 of 1044 (rc 0, .expected 0,
       text-only 0) · stdlib all four layers
verdict: LANDED. It is capretplt's mechanism with cause B moved off
    `outliving_params_`.

`capretplt` / `capretcaps` / `capmovewalk` deposit every capture into
`param_names_` + `outliving_params_`. THREE channels read that set — the
escape/dangling walk (`carried_prov_of_recv`, `prov_of`), the retained-
provenance walk, and the return check — and only the third had asked the
question. Cost: four pinned `-L bc -L fail` fixtures rc 1 -> rc 0.
`capretsc` puts the same fact in a set of its own, read at ONE site (the
"cannot return reference to local variable" report gate), and the four stay
refused. Measured on the landed tree, both directions.

    capretplt  {issue-53432, closure-substs, issue-58053,
                nested-bodies-in-dead-code, regions-nested-fns-2}   cfail 4
    capretsc   the same four MINUS regions-nested-fns-2             cfail 0
    capretplt ∖ capretsc = {regions-nested-fns-2}   capretsc ∖ capretplt = ∅

⚠ **regions-nested-fns-2 WAS BOUGHT BY THE LEAK, AND IS NOT BOUGHT HERE.** It
is the one row the wider exemption closes, and the four un-refusals are what
paid for it. It stays open, named, on purpose.

HAND PROGRAMS (rules 5, 7, 10), each multi-line, each run on the PROBE binary
and again on the LANDED one, identical answers:

    h1  let c = || -> &i64 { let t: i64 = 1i64; return &t; };
        unarmed rc 0 · landed REFUSED "cannot return reference to local
        variable 't'"                                    (the mechanism)
    h2  let u: i64 = 9; let c = || -> &i64 { return &u; };
        rc 0 both                                        (the exemption)
    h3  let c = |x:&i64| -> &i64 { return x; };           rc 0 both  (legal)
    h4  let c = |x:&i64| -> &'static i64 { return x; };
        unarmed rc 0 · landed REFUSED, and with the SAME diagnostic the fn
        twin already got unarmed                         (cause A rebind)
    h5  escape-upvar-ref's own body: `{ let y; let c = || { p = &y; }; c(); }`
        REFUSED under control, under the probe and after landing (E0597)
        ← THIS IS THE ONE capretcaps BROKE
    h6  fn foo<'a>(x:&'a i64)->&'a i64 { let c=|y:&i64|->&i64{return y;}; return x; }
        rc 0 both — the rebind does not resurrect cause A
    h7  fn bad() -> &i64 { let t: i64 = 1i64; return &t; }   REFUSED both

⚠ THE ONE-VARIABLE CONTROL FOR EACH EXEMPTION IS IN-TREE AND WAS RUN. Under
`capretchk` (the gate on, NO exemptions) h2 is refused ("local variable
'orig'") and h6 is refused ("lifetime elision: return reference must derive
from 'x'"). Both exemptions are load-bearing and each was shown so alone.

## lifereg_unmentbind — LANDED AS `current_lt_binders()`
site: include/logos/compiler/outlives.hpp::outlives (the permissive tail)
carrier: include/logos/compiler/outlives.hpp::current_lt_binders(), filled in
      src/compiler/sema_decl.cpp beside `current_outlives_`
build: c4dedf97e7aee29a (READ) · measured 2026-08-31g, NOT re-priced: the
      record's build is this tree's libs hash, so it is current.
fires: 4 · ceiling 5 · cost 0 · cfail 0 · stdlib ok
verdict: LANDED. The carrier moved out of probe.hpp — it is no longer a probe
    fact — and `lifereg_unmentioned`, the wider door directly above it, is KEPT
    as a probe: it now measures the INCREMENT (2 rows for 5 legal refusals).

    predicted, and closed: issue-55394--ctl2, issue-67007-escaping-data,
        projection-no-regions-closure--c30-direct-call,
        projection-no-regions-closure--projection-no-regions-closure,
        account-for-lifetimes-in-closure-suggestion
    predicted NOT to close, and did not: propagate-fail-to-approximate-longer-
        no-bounds ('y is demand_y's binder), regions-infer-call-3 ('r is
        select's binder)

⚠ NOT SOUND BY CONSTRUCTION — IT WORKS BY NAME COLLISION, and that is now
PINNED rather than written in a comment:
    tests/logos/fail/bc_ltunmentbind_two_unrelated_binders   (u7) REFUSED
    tests/logos/pass/bc_ltunmentbind_renamed_binder_hole     (u8) MISSED
one token apart, both equally illegal. The pass fixture's header says out loud
that it pins a HOLE and must move to fail/ when substitution lands. The legal
side (u2, u5, plus the elision shape) is one fixture,
tests/logos/pass/bc_ltunmentbind_legal_shapes, for the reason the field-write
variance round gives: the claim is about the SET.

## RETIRED THIS ROUND, WITH THE REASON

 * **capretty · capretchk · capretcaps · capretplt — RETIRED, SUBSUMED.** Their
   mechanism is the landed behaviour, so re-arming them would measure nothing.
   Their measurement history is preserved verbatim below, because it is the
   record of what the exemption cost and it was the only copy.
 * **capretsc — RETIRED, LANDED.** Same reason, one round later.
 * **capargclos — RETIRED, WRONG FRAME** (2026-08-31g): 8 arrivals in the whole
   population and ZERO fires on its own subject (issue-51268, e3, e6). The
   decision is at the LET/statement gate one frame above. Rule 17, fifth
   instance. The question survives; the site does not.
 * **return-wrong-bound-region — RETIRED**, no HRTB machinery in the tree.

## THE VERBATIM HISTORY OF THE RETIRED capret* FAMILY
(moved out of src/compiler/borrow_check.cpp, where 74 lines of it sat in a
compiled file — prose there costs a build and invalidates recorded verdicts)

// PROBE capretty: `ret_type_` IS SET EXACTLY ONCE, at the enclosing
// FUNCTION'S entry, and this walk never rebinds it — so every
// check_return_value reached from a closure body asks "does this
// escape the FUNCTION", using the FUNCTION'S return type. Inside
// `fn main() -> i32` the typed gate is `i32`: false. So
// `|| -> &i64 { let t: i64 = 1i64; return &t; }` returns a reference
// to a body local past a gate that was answering about `i32`. The
// closure's OWN return type is on the node (`EClosureBoxView::
// ret_type`) and nothing has ever read it here.
// MEASURED 2026-08-28, 371-row population: 66 FIRES, CEILING 0,
// COST 0. ⚠ THIS ZERO IS NOT A REFUTATION — IT IS A NULL RESULT
// THROUGH A BROKEN CHANNEL. The site is provably live (66 arrivals);
// the CONSUMER is switched off, one screen down, by
// `if (!in_closure_body_) check_return_value(val, ln);`. Handing the
// right type to a check that never runs buys nothing, and reads
// exactly like a dead hypothesis. Rule 2: proven live is necessary,
// not sufficient — the population may be elsewhere, and here it was
// behind a guard.
// PROBE capretchk — THE SECOND HALF OF THE SAME MECHANISM, and the
// only two-site probe in this batch. capretty measured the ret_type_
// site alone at CEILING 0 over 66 fires, and the reason is not that
// the hypothesis is dead: `check_return_value` is HARD-SUPPRESSED
// inside a closure body (`if (!in_closure_body_) check_return_value`),
// so handing it the right type changes nothing. A zero read off a
// channel that is switched off is not a refutation. capretchk arms
// BOTH sites; attribution survives because the other half is already
// measured at 0, so anything capretchk closes is the GATE's.
// PROBE capretcaps — capretchk PLUS the exemption its three costs
// asked for. Those costs are TWO causes, both read off the diagnostic
// rather than guessed:
//   A  "[fn foo] lifetime elision: return reference must derive from
//      'x'" — `param_lifetimes_` is the ENCLOSING fn's. This walk
//      already saves/restores param_names_ and outliving_params_ and
//      never touched the third set, so a closure's `return y` was
//      judged against `fn foo(x: &i64)`'s elision contract.
//   B  "cannot return reference to local variable 'x'" where `x` is a
//      CAPTURE. A non-move closure's capture lives in the ENCLOSING
//      frame and by construction outlives the closure — it is the
//      exact situation `outliving_params_` exists to describe (#138),
//      and captures were never put in it.
// Same shape as the params this walk already declares; the captures
// were simply the half nobody added.
//
// ── THE RESULT, AND IT IS THE ROUND'S MAIN FINDING ──────────────────
// MEASURED 2026-08-28, 371-row acceptance population:
//     capretty    66 fires   CEILING  0   COST 0   (channel off)
//     capretchk  (gate on)   CEILING 11   COST 3
//     capretcaps (+exempt)   CEILING  2   COST 0
// THE EXEMPTION COST NINE OF THE ELEVEN. Those nine were not closed by
// observing anything about closure returns; they were bought by
// refusing legal programs, and the three costs are the only three such
// programs the corpus happens to CONTAIN. Rule 7, sharpest form: a
// crude probe and a correct fix do not close the same programs.
// Named, because a ceiling bounds the COUNT and not the SET —
// capretchk closes and capretcaps does NOT:
//   borrowck/issue-58776-borrowck-scans-children
//   borrowck/var-matching-lifetime-but-unused-not-mentioned
//   nll/issue-40510-1  nll/issue-48697--b  nll/issue-48697--t16
//   nll/issue-53040
//   regions/regions-infer-call-3  regions/regions-nested-fns-2
//   regions/regions-return-ref-to-upvar-issue-17403
// THIS TREE ALREADY KNEW. pass/bc_capbody_closure_return_admit's own
// header records that the body-walk round put three of them
// (issue-48697--b, --t16, var-matching-…) BACK ON THE SHELF for
// exactly this reason. The measurement reproduces that verdict from
// the other direction and adds the six it had not enumerated.
// What survives is real and is in no corpus program at all: a closure
// returning a reference to a BODY LOCAL —
//   `let f = || -> &i64 { let t: i64 = 1i64; return &t; };`
// — compiles today and refuses under capretcaps, while the capture
// form (`return &x;`), the param form (`|y| return y`) and the nested
// form still admit, and the ENCLOSING fn's own dangling return and
// elision contract stay refused. Six hand-written programs, because
// COST 0 is not a safety claim.

## THE PREDICTIONS, VERBATIM, AS WRITTEN BEFORE EITHER GATE WAS EDITED

    ROUND 2026-08-31h — PREDICTIONS, WRITTEN BEFORE THE GATES WERE EDITED
    baseline: build 218 (libs c4dedf97e7aee29a), 2432 recorded / 0 failed,
              admit filter 306 rows, `-L bc` 1935 tests, ledger # TOTAL 306.
    
    ARM 1 — lifereg_unmentbind (include/logos/compiler/outlives.hpp)
      measured 2026-08-31 on build c4dedf97e7aee29a: fires 4, ceiling 5, cost 0,
      cfail 0, stdlib all four layers.
      PREDICT 5 ledger rows close, BY NAME:
        logos_00_bc_admit_borrowck_issue-55394--ctl2
        logos_00_bc_admit_nll_issue-67007-escaping-data
        logos_00_bc_admit_nll_projection-no-regions-closure--c30-direct-call
        logos_00_bc_admit_nll_projection-no-regions-closure--projection-no-regions-closure
        logos_00_bc_admit_nll_account-for-lifetimes-in-closure-suggestion
      PREDICT NOT to close (they are the two rows the narrowing gives up vs
      lifereg_unmentioned, and the reason is rule 12 in the other direction):
        propagate-fail-to-approximate-longer-no-bounds
        regions-infer-call-3
      PREDICT cost 0 / cfail 0 / stdlib ok on the LANDED tree.
    
    ARM 2 — capretsc (src/compiler/borrow_check.cpp), THE SCOPED CAUSE B
      measured 2026-08-31 on build 571876f6ef48a1ed: fires 5492473, ceiling 4,
      cost 0, cfail 0 of 1044 (rc 0, .expected-match 0, text-only 0), stdlib ok.
      PREDICT 4 ledger rows close, BY NAME:
        logos_00_bc_admit_borrowck_issue-53432-nested-closure-outlives-borrowed-value
        logos_00_bc_admit_nll_closure-substs
        logos_00_bc_admit_nll_issue-58053
        logos_00_bc_admit_nll_nested-bodies-in-dead-code
      PREDICT NOT to close, and NAMED because a ceiling bounds the count not the set:
        regions-nested-fns-2   (capretplt closed it; capretplt bought it with the
                                cause-B leak that also un-refused four fail fixtures)
        issue-40510-1, issue-48697--t16  (capretchk-minus-capretcaps: a legal refusal
                                buys them and nothing else does)
      PREDICT the four `-L bc -L fail` fixtures capretcaps/capretplt/capmovewalk
      un-refuse STAY REFUSED, since the escape/dangling channel no longer sees the
      captures:  escape-argument--b · escape-upvar-nested · escape-upvar-ref
                 · regions-nested-fns
    
    JOINT: PREDICT 9 rows close, ledger # TOTAL 306 -> 297.
    The two arms are disjoint by site (outlives.hpp vs borrow_check.cpp) and by
    name; each is measured alone and each gets its own fixture pair.

MEASURED: the nine names above and no others. predicted∖measured = ∅,
measured∖predicted = ∅, on the ledger half AND on all three cost populations.
The three named as NOT closing did not close.

## ORACLES, ALL ON THE LANDED TREE

 * `stdlib-cost.sh`: all four layers compile.
 * `fail_text_oracle.py`, landed vs CONTROL REVERT over **1056** fixtures
   (1044 + the 9 relanded + 3 native): **12 rc changes, 12 stderr-sha changes,
   12 `.expected`-match changes — the same 12, and all 12 are this round's own
   new fail fixtures.** Nothing else moved. escape-argument--b,
   escape-upvar-nested, escape-upvar-ref and regions-nested-fns are rc 1 in
   BOTH columns, which is the whole difference between capretsc and capretcaps.
 * CONTROL REVERT: with the four compiler sources at HEAD, all 12 new fail
   fixtures compile rc 0 (i.e. go red) and all 4 pass twins stay green.

## STILL OPEN, NAMED, WITH ITS EVIDENCE

 * **regions-nested-fns-2** — closable only by the capture exemption the
   escape channel also reads. Not bought.
 * **C-II, a `move` closure's body is never walked** — `capmovewalk`'s clean
   increment is TWO (borrowck-multiple-captures, issue-48238), and it must be
   RE-PRICED: it was measured on a tree where it also turned the retchk gate
   on, and that gate is now unconditional. Its cfail 4 was inherited from
   cause B and should now be 0. **A ONE-VARIABLE PROBE FOR IT EXISTS TODAY** —
   the `probe_mv_` gate at the top of walk_closure_body is all that is left of
   it. First thing to price next round.
 * **C-I-c issue-40510-1 / issue-48697--t16 — DECLINED**, a legal refusal buys
   them and nothing else does.
 * **`capshared`** — still 4 `.expected` LOST, unread. Rule 14 may rescue them.
 * **the LET/statement gate for a closure-literal call argument** (C-VII).
 * **the SUBSTITUTION engine** — now named by THREE measurements: `ltmintfresh`
   157, this round's two un-bought `lifereg_unmentioned` rows, and the u7/u8
   pair now pinned as fixtures.
 * ⚠ NEITHER issue-51268 NOR issue-42574--b HAS AN ORACLE ON THIS BOX.
 * A diagnostic-context defect, unpriced: a refusal raised inside a closure
   body is reported as `[fn <enclosing>]` — closure-substs prints `[fn foo]`.
   The `.expected` files pinned this round are message-only, so they do not
   depend on it, but the string is wrong.

# ═══ ROUND 2026-08-31i — THE ELISION ENGINE BUILT, PRICED, AND ITS WALL ═════

Subject: the mechanism two independent rounds converged on — `outlives()` is
handed two STRINGS and cannot say which BINDER each denotes. Built behind flags,
in separable pieces, and priced. NOT LANDED.

build: e0c4dfbe62e140c8 (READ, `scripts/build_hash.py build`) · ledger # TOTAL
297 · L1 rc=0 at every commit of the round (745/745, 346 gates, 12 684 smoke),
so every arm is inert unarmed.

## THE MINT CENSUS, TAKEN BEFORE THE GATE WAS BELIEVED (RULE 17)

`probe.hpp` gained `census()` — the site-census instrument, gated on
LOGOS_CENSUS, riding in the same build as the gate it checks. Over
`tests/imported/admit` + `tests/logos/pass` (1 file = 1 logosc process, summed):

    bucket                     arrivals   what it says
    mint.fn.signature        13 841 071   fn signatures walked
    mint.ref.elided           4 794 309   `&T` slots with NO name
    mint.ref.written             20 966   `&'a T` slots WITH one
    mint.ret.unified          3 431 013   returns elision rule 1/3 could source
    mint.ret.no-source       10 410 058   returns it could NOT — left elided
    mint.slice.noslot         1 296 116   ⚠ Kind::Slice CANNOT CARRY A REGION
    mint.dstref.noslot           38 611   ⚠ nor DstRef
    mint.traitobject.noslot      23 437   ⚠ nor TraitObject
    mint.structarg.written          160   struct lifetime args as written
    mint.structarg.elided            84   present but empty
    mint.structarg.absent            84   ⚠ DOOR 3's whole population
    subst.structlit.site             56   struct literals with a lifetime param
    subst.structlit.differs          13   …where the FIELD's region differs from
                                          the HINT the literal actually used
    subst.call.site                   5   free-fn calls whose callee declares a
    subst.call.mapped                 3   lifetime param (over 200 admit files);
    subst.call.unmapped               3   rule 4 applies before these are read

**THREE THINGS THE CENSUS SETTLED AND NO REASONING WOULD HAVE.**

 1. **99.6% OF REFERENCE SLOTS IN THIS TREE ARE ELIDED** — 4 794 309 against
    20 966 written. The elided slot is not a corner of the language; it is the
    language as this corpus writes it.
 2. **`&[T]`, `&dyn` AND `&DstStruct` HAVE NO LIFETIME SLOT AT ALL.**
    `resolve_type` canonicalises `&[T]` to `Kind::Slice` and the region of the
    borrow is DROPPED there, 1 296 116 times. No mint can name what the type
    cannot carry. This is a STRUCTURAL hole the engine does not close and it is
    the largest single number in the table after the refs themselves.
 3. **DOOR 3 IS TINY** — 84 arrivals. It is real (it closes
    `regions-infer-at-fn-not-param`, predicted and measured) but it is not a
    population.

⚠ AND THE CENSUS CAUGHT THIS ROUND'S OWN SITE LIST BEING WRONG — the sixth such
instance this week and the first where the list was mine. `subst.call.site`
fired ZERO times on the stdlib program that broke the mint: an unambiguous free
fn takes neither the overload-resolved nor the generic call path but the
EXACT-MATCH path, which the first build did not instrument.

## THE ARMS, NAMED SO EACH CAN BE PRICED ALONE (RULES 3, 9, 13)

    ltmintunify  sema_decl.cpp — mint a fresh region into every elided slot of a
                 fn signature, then UNIFY by Rust's elision rules (one input
                 region ⇒ the output takes it; `&self` ⇒ the output takes self's;
                 neither ⇒ the output is LEFT ELIDED, i.e. today's behaviour).
                 Minted names are added to `current_lt_binders()`, so the landed
                 2026-08-31h rule refuses two UNRELATED elided slots while a
                 unified pair stays reflexive.
    ltsubstlit   sema_expr.cpp — a struct literal's lifetime args come from the
                 HINT, never from the values stored. Read them off the FIELD
                 VALUES instead.
    ltsubstcall  sema_expr.cpp — the free-fn call passes a TYPE substitution
                 only, so a callee's `'a` reaches the caller as the string `'a`.
                 A callee region seen at an argument becomes the caller's actual
                 region; one seen nowhere becomes a fresh rigid region.
    ltsubstfree  the same, with the OTHER policy for a callee region seen
                 nowhere: `'static`, its upper bound.
    ltmintsubst  = ltmintunify + ltsubstlit + ltsubstcall
    ltmintfree   = ltmintunify + ltsubstlit + ltsubstfree
    ltmintimpl   = ltmintfree + three repairs read off its own diagnostics

A minted name carries the unspellable prefix `'%` and `lt_is_minted`
(outlives.hpp) makes it invisible to every consumer that treats an elided slot
as ABSENT — `type_str` (including the struct-name key `concrete_struct_name` is
built from), the undeclared-lifetime walk, `param_lifetimes_`, the return-type
elision contract and B86's inner-lifetime hatch. Only the comparators see it.

## THE TABLE, ALL THREE COST COLUMNS

    probe          fires      ceiling  cost  cfail                stdlib
    ltmintunify    9038830      18       9   10 (.expected LOST)  ⛔ lang REFUSED
    ltsubstlit        8516       2       4    3 (text-only)       ok
    ltsubstcall       9787       2       0    1 (text-only)       ok
    ltsubstfree      54728       0       1    1 (text-only)       ok
    ltmintsubst    9056575      21      12   13 (10 LOST, 3 text) ⛔ lang REFUSED
    ltmintfree     9105738      19      12   13 ( 9 LOST, 4 text) ok
    ltmintimpl    20506175      19       7   13 ( 9 LOST, 4 text) ok   ← THE ENGINE

**`fn id(x:&i64)->&i64` SURVIVES EVERY ARM.** It is the counter-example that
priced `ltmintfresh` at 650 legal refusals, and it is admitted here because the
engine UNIFIES: rule 1 gives the output the single input's region and the
comparison is reflexive. Rule 7's prediction is confirmed with a number — the
crude proxy costs 650, the correct engine costs 7, and the mechanism they share
closes an overlapping but different set.

## THE WALL, AND THE ONE VARIABLE THAT WALKS THROUGH IT

`ltmintunify` REFUSED THE STDLIB — `lang`, two functions, and they are the same
shape:

    stdlib/lang/writ/objdata.logos:288
      pub unsafe fn wod_view_array<'a>(p:*const u8, tag:u64) -> &'a WArray<WAny>
      impl WritObjectData { pub fn as_array(self:&WritObjectData) -> &WArray<WAny>
                            { return unsafe { wod_view_array(self.as_ptr(), self.tag) }; } }
      error: return type mismatch — expected &WArray<WAny>, got &'a WArray<WAny>

**A CALLEE LIFETIME THAT APPEARS IN NO PARAMETER IS FREE: THE CALLER
INSTANTIATES IT.** `'a` here is the `&*(p as *const T)` view idiom's "any region
you like". The mint names the method's own return slot, the callee's `'a` stays
rigid, and nothing can relate the two — there is no region INFERENCE in this
tree to discharge it. Measured one variable apart on the same binary:

    ltmintsubst  (a free callee region ⇒ a fresh RIGID region)  stdlib REFUSED
    ltmintfree   (a free callee region ⇒ `'static`, its bound)  all four layers

That is the wall and its door. It cost two rows to walk through it: `ltsubstcall`
(rigid) closes `method-ufcs-inherent-4` and `projection-where-clause-none--b`,
`ltsubstfree` closes NEITHER — ceiling 2 → 0. The policy that saves the stdlib
gives up the only two rows its own arm could buy.

## RULE 13, MEASURED IN BOTH DIRECTIONS

    ltmintunify ∪ ltsubstlit ∪ ltsubstcall   =  22 rows (18 ⊎ 2 ⊎ 2, disjoint)
    ltmintsubst (all three at once)           =  21 rows

**THE INCREMENT IS NEGATIVE FOR ONE ROW.**
`ltmintunify ∖ ltmintsubst = {suggest-introducing-and-adding-missing-lifetime
--param-may-not-live}` — a row the mint closes ALONE and the mint-plus-
substitution does not. Per-site measurements are not additive and the increment
can be negative; here is the case, by name.

## THE COST, READ ONE FIXTURE AT A TIME — 12 → 7

`ltmintfree`'s twelve legal refusals were not one defect. Each was read off its
own diagnostic, and three narrow repairs (`ltmintimpl`) closed five:

 1. **`'_` IS AN ELIDED SLOT THAT WAS SPELLED.** `fn make<'a>(r:&'a i32) ->
    H<'_>` compared `'_` against `'a` by spelling. (anon-lt-return-type)
 2. **THE IMPLIED BOUND `&'x S<'a>` ⇒ `'a: 'x` IS EMITTED ONLY FOR A FN-DECLARED
    `'a`.** `walk_implied`'s struct arm is already correct; `fn_lts` holds the
    fn's own binders and NOT the enclosing `impl<'a>`'s, so an `impl<'a> H<'a>`
    method's minted self region was related to nothing.
    (regions-impl-elided-lt, region_2)
 3. **A MINTED ARG ORPHANED THE B86 HATCH** in borrow_check: it reads "no inner
    lifetime recorded" as "trust the type checker", and the mint FILLS a
    struct's absent lifetime args, so `&Self` stopped being empty and the hatch
    stopped firing. A new sensor orphaning an old guard, silently.
    (self-lt-impl-method, self-method-chain)

**THE SEVEN THAT REMAIN, AND THE ONE MECHANISM UNDER FOUR OF THEM:**

    bc_ltunmentbind_renamed_binder_hole  ← NOT A COST. This fixture's own header
        says it pins a HOLE and must move to fail/ when substitution lands. It
        was PREDICTED to flip and it flipped. u7 (its fail twin) stays refused,
        now at the struct literal for the right reason instead of by spelling.
    region-two-refs-same-region-pick-rg · regions-infer-contravariance-due-to-ret
    · borrowck-unused-mut-locals · regions-mock-codegen
        ⇒ **THE CALLEE-SIGNATURE ASYMMETRY.** The mint is LOCAL to the fn being
        lowered and is never written back to `SemaFuncInfo`, so at a call the
        CALLER's argument carries minted struct args and the CALLEE's parameter
        carries none: `expected &'r BoxedInt, got &BoxedInt<'%3>`. The fix is
        not another exemption — it is to mint at signature COLLECTION so both
        sides of every call see the same signature. That is the next round.
    bc_genrecv_two_mut_sequential_admit · bc_ltscope_impl_legal_shapes
        unread; both are `&mut` / impl-scope shapes.

## THE `.expected` COLUMN — 9 PINS, ALL STILL RED, ALL MOVED UPSTREAM (RULE 14)

Every one of the nine is the SAME substitution, e.g.
ex1-return-one-existing-name-if-else:

    unarmed  lifetime mismatch: return type has lifetime 'a but 'y' has lifetime (elided)
    armed    return type mismatch: variance mismatch — expected &'a i64, got &i64

rc is 1 in BOTH columns. The refusal moves from borrow_check's elision message
to the type checker's variance message — upstream's primary error, arriving
first. These are pins to RE-BASELINE at landing, not damage; rule 14's exemption
("delivers upstream's PRIMARY error first") is the case here, and it is stated
because the alternative reading — nine lost diagnostics — is the one a table
alone would give.

## PREDICTED vs MEASURED, BOTH DIRECTIONS (RULE 6)

Written to /tmp/predictions.txt before any pricing run.

    ltmintunify  predicted 6 by name; MEASURED 18.
                 predicted ∩ measured = {ex3-both-anon-regions-2,
                   ex3-both-anon-regions-one-is-struct,
                   ex3-both-anon-regions-one-is-struct-4,
                   regions-infer-at-fn-not-param}   ← DOOR 3 CONFIRMED BY NAME
                 predicted ∖ measured = {ex3-both-anon-regions-latebound-regions,
                   regions-addr-of-self}
                 measured ∖ predicted = 14 rows, and they are a FAMILY the
                   prediction had no name for: ex1-return-one-existing-name-* (5),
                   ex3-both-anon-regions-{one-is-struct-3,self-is-anon},
                   ex2a-push-one-existing-name, apit-not-targeted…, issue-17728,
                   normalization-self, regions-infer-paramd-indirect,
                   cannot-assign-borrowed-ref-in-slice, suggest-introducing….
                 PREDICTED NOT TO CLOSE and did not: ex3-both-anon-regions (a
                   METHOD-call argument, still not a comparison site), and both
                   `lifereg.R17` declaration-site rows.
    ltsubstlit   predicted 0-1; MEASURED 2 {issue-52113,
                   region-invariant-static-error-reporting}, neither predicted.
                 The predicted u8 flip HAPPENED, u7 stayed refused,
                 bc_ltunmentbind_legal_shapes stayed green — all three by hand.
    ltsubstcall  predicted 0; MEASURED 2 {method-ufcs-inherent-4,
                   projection-where-clause-none--b} — two of the four
                   `lifereg_callretlt` closed on 2026-08-28 with the CRUDE
                   rename, at cost 0, and the hand program that refuted that
                   crude cost 0 (`fn pick<'a,T>(x:&'a T)->&'a T` called from
                   `fn f<'a>`) is ADMITTED here. Rule 7 from the other side.

## THE HAND PROGRAMS (RULES 5, 7, 10) — EACH MULTI-LINE, EACH ON THE ARMED BINARY

    c2  fn id(x:&i64)->&i64 { return x; }                  rc 0 under EVERY arm
        (`ltmintfresh` REFUSED it — the 650)
    c1  fn foo<'a>(x:&mut Ref2<'a,'a>, y:&'a i64){ x.a=y; }  rc 0 every arm
    s1  impl Dog { fn get(&self)->&i64 { return &self.n; } } rc 0 every arm
        (elision rule 3)
    v2  fn pick<'a>(x:&'a i64)->&'a i64 called from fn f<'a>  rc 0 every arm
    p1  struct Ref2<'a,'b>; fn foo(y:&mut Ref2, x:&i64){ y.b=x; }
        unarmed rc 0 · ltmintunify REFUSED "assignment to 'y.b': variance
        mismatch" — and NOTE the diagnostic names no minted region, because
        `lt_is_minted` keeps them out of `type_str`.
    p2  fn foo(p:&mut (&i64,&i64), x:&i64){ p.0 = x; }      same, both arms
    w1  the stdlib shape, minimised:
        unsafe fn view<'a>(p:*const u8)->&'a WA; impl WD { fn as_arr(&self)->&WA }
        ltmintunify REFUSED · ltmintfree rc 0 · ltmintsubst REFUSED
        ← THE WALL AND ITS DOOR, one policy apart, on one program.
    u7/u8 the pinned pair: u8 (renamed binder) REFUSED under ltsubstlit, u7 still
        refused, bc_ltunmentbind_legal_shapes still green.

## ⇒ THE VERDICT

 1. **THE ENGINE IS FUNDABLE, AS `ltmintimpl`: CEILING 19 of 297, COST 7 (one of
    which is a fixture that ASKS to flip), ALL FOUR STDLIB LAYERS COMPILE.**
    That is the first form of this mechanism to build `logos.mem`, and it is
    the same mechanism `ltmintfresh` priced at 157/650 — the ceiling is smaller
    because the engine unifies where the proxy minted blindly, and the cost is
    two orders of magnitude smaller for the same reason.
 2. **IT MUST NOT LAND AS MEASURED.** Four of the seven costs are ONE mechanism
    — the callee-signature asymmetry — and the fix is structural: mint at
    signature COLLECTION (`SemaFuncInfo::param_types`), not at lowering, so
    caller and callee see the same signature. Pricing THAT is the next round,
    and it is the first time this arc has a named next step that is not another
    exemption.
 3. **THE FREE-REGION POLICY IS NOT A DETAIL.** `'static` for a callee region no
    argument mentions is what makes the stdlib compile, and it costs the two
    rows the rigid policy buys. State both when it lands.
 4. `ltsubstcall` and `ltsubstfree` are NOT independently fundable: 2 rows at
    cost 0 and 0 rows at cost 1 respectively, and they are two policies for one
    line, not two mechanisms.
 5. **THE 1 296 116 SLICE ARRIVALS ARE A SEPARATE, LARGER QUESTION.** `&[T]`
    loses its region at `resolve_type`. No amount of minting reaches it, and
    nothing in this arc has ever measured what that costs.

## STILL OPEN, NAMED, WITH ITS EVIDENCE

 * the CALLEE-SIGNATURE ASYMMETRY (4 legal refusals, above) — next round.
 * `Kind::Slice` / `TraitObject` / `DstRef` carry no region: 1 358 164 arrivals.
 * `bc_genrecv_two_mut_sequential_admit` and `bc_ltscope_impl_legal_shapes` —
   the two costs nobody read this round.
 * the 9 `.expected` re-baselines, all still red, all upstream-primary.
 * the METHOD-call argument is still not a comparison site (6 `lifereg.A` rows,
   `ex3-both-anon-regions` among them — predicted not to close, and did not).
 * everything carried forward from 2026-08-31h is untouched.

# ROUND 2026-08-31j — THE ENGINE AT COST 1, AND THE ONE THAT REMAINS IS REGION INFERENCE

Ledger UNTOUCHED at 297. NOTHING LANDED to the ledger. The tree gained four
repairs, all probe-gated and PROVEN INERT: 0 of 1382 pass/ledger verdicts and
**0 of 1056 `-L bc -L fail` fixtures** move between the round's opening build
(`e0c4dfbe62e140c8`) and its closing one (`72d9a3b801eef1f1`) with no probe
armed — measured by `failtext-*.tsv` diff, not asserted.

Opening baseline, READ FROM THE STORE (build 234, `e0c4dfbe62e140c8`):
`-R '^logos_00_bc_admit_'` 297/297 still admitted, 0 failed; `-L bc` 1056/1056
passed. Closing unarmed build 238: 1382 recorded, 0 failed.

## THE VERDICT FIRST

    ltmintimpl  (2026-08-31i, the engine as priced)  CEILING 19  COST 7
    ltmintinst  (this round: engine + FOUR repairs)  CEILING 22  COST 3
                                                     COST-fail rc-changes 0
                                                     stdlib ALL FOUR GREEN
    ltsubstinst (the SUBSTITUTION HALF ALONE)        CEILING  3  COST 0
                                                     ⛔ and it UN-REFUSES THREE
                                                        PINNED ILLEGAL PROGRAMS

Of `ltmintinst`'s three costs, **two are not costs**:

    bc_ltunmentbind_renamed_binder_hole   its own header asks it to flip; it did
    bc_genrecv_two_mut_sequential_admit   `fn pick<T>(&self, other:&i64)->&i64
        { return other; }` — elision rule 3 gives the elided output the
        RECEIVER's region, so rustc REJECTS this. The fixture is illegal as
        written; its subject (the result borrows the PARAMETER, so no loan is
        tied to `c`) survives verbatim under `fn pick<'p,T>(&self,
        other:&'p i64) -> &'p i64`. A corpus repair, to land WITH the engine.

**THE ONE THAT IS REAL, AND IT NAMES THE WALL:**

    variance-option-ref-intersection-rg
        struct Lst<'l> { field1: &'l i32, field2: Option<&'l i32> }
        fn foo(field1: &i32, field2: Option<&i32>) -> i32 {
            let list: Lst = Lst { field1: field1, field2: field2 };  ...
        The two parameters mint TWO regions. `'l` must be ONE. Rust admits it
        because `'l` is instantiated at the INTERSECTION and covariance lets
        both approximate down to it. There is no region inference in this tree
        to compute an intersection, and a substitution can only pick one of the
        two candidates it was handed.

⇒ **NEVER BUY A LEDGER ROW WITH A LEGAL-PROGRAM REFUSAL.** One is one.
`ltmintinst` DOES NOT LAND. 22 rows are held up by a program of six lines.

## THE FOUR REPAIRS, EACH WITH A ROOT READ OFF THE SOURCE

**R1 — THREE OUTCOMES FOR A CALLEE REGION, NOT TWO.** `subst_call_ret_lts_`
knew `mapped` and `free`. A region that appears IN a parameter but whose
argument's own region is unnamed is neither: the caller does constrain it, we
merely cannot name the constraint. `ltmintimpl` sent it down the `free` path
and wrote `'static` into it — which is how `borrowck-unused-mut-locals` came to
read `expected &'a mut B<'a>, got &mut B<'static>`. It is now ELIDED, and only
a region in NO parameter is `'static`. The stdlib's `wod_view_array<'a>(p:*const
u8)` is exactly that case and is untouched.

**R2 — A CALLEE BINDER AT AN ARGUMENT POSITION IS UNIVERSALLY QUANTIFIED.** The
callee→caller map was applied to the RETURN type and to nothing else, so
`max<'r>(bi:&'r BoxedInt, f:&'r i64)` called with a minted caller region
compared `'r` against `'%1` — a region against a BINDER, rule 12 at an argument.
`inst_call_params_` applies the same map to the parameters before
`check_variance`, at the four free-fn argument sites.

**R3 — THE STRUCT'S OWN BINDERS ARE INSTANTIATED AT THE LITERAL, AND ERASING
THEM IS NOT THE SAME THING.** The field-init `check_variance` already carried
the comment "permissive — struct's lifetime args are inferred at this site";
permissive was enough only while the VALUE's regions were elided.

⚠ THIS IS THE ROUND'S OWN MISTAKE, AND THE `fail` ORACLE CAUGHT IT. The first
implementation ERASED the struct's binders. It priced CEILING 22 / COST 2 in the
pass column and looked better than the second. It also flipped TWO PINNED
FAIL FIXTURES from rc 1 to rc 0 —

    account-for-lifetimes-in-closure-suggestion   SameLifetime<'a>{ t:
        TwoThings<'a,'a> } built from a TwoThings<'a,'b>
    nondeterministic-lifetime-errors-15034        Parser<'a>{ lexer:
        &'a mut Lexer<'a> } built from a `&'a mut Lexer`   (E0621)

— whose whole subject is one binder appearing TWICE. Erasing it makes the
comparison vacuous. **A pass-only oracle cannot see an UN-REFUSAL, by
construction**; the `-L bc -L fail` rc column is the only instrument in the tree
that can, and this is the second time this week it has reversed a recommendation
that read as principled. The binder is now INSTANTIATED from the field values
(first occurrence wins) and both pins are back, byte for byte.

**R4 — `Self` LOST ITS LIFETIME ARGS, AND THE REPAIR THAT WAS MEANT TO CARRY
THEM ASKS THE WRONG QUESTION.** `sema_collect.cpp` carries "Bug 2: include
struct's lifetime params so Self carries lifetime_args" INSIDE
`if (!impl_tps.empty())` — a test for TYPE parameters. `impl<'a> Src<'a> for
H<'a>` has none, so Self is built as plain `H` with the lifetime slot ABSENT.
Rule 16: absent and recorded-empty are different, and ONLY the minting site can
tell them apart — door 3 fills the absent slot with a FRESH region, a default
body's `self.raw()` reads `'a` off it, and `bc_ltscope_impl_legal_shapes` is
refused with `expected &'a i32, got &i32`. TWO sites, one defect: the impl
header and the inherited-default synthesis. Minimal repro `t1.logos`; the
control is `t2.logos`, the same program with the default written out in the
impl, which passes under every arm.

## PREDICTED vs MEASURED, BOTH DIRECTIONS (RULE 6)

Written to `/tmp/.../predictions-ltmintinst.txt` before each edit.

    PREDICTION 1 (R1+R2):  cost 7 -> 4, and the four BY NAME.
      MEASURED: 4, THE SAME FOUR. Closed: region-two-refs-same-region-pick-rg,
      regions-infer-contravariance-due-to-ret, borrowck-unused-mut-locals.
    PREDICTION 2 (+R3+R4): cost 4 -> 2, and the two BY NAME.
      MEASURED: 2, THE SAME TWO — and the fail oracle then said the price of
      those two was two un-refusals the pass column could not see.
    PREDICTION 3 (R3''):  the two pins return; regions-mock-codegen RETURNS AS
      A COST; cost 3.
      MEASURED: the two pins returned. **regions-mock-codegen did NOT return —
      it CLOSED.** And a cost appeared that no prediction had a name for:
      variance-option-ref-intersection-rg. The COUNT was right and the SET was
      wrong in both directions at once. Rule 13 from a new side: R3 and R3'' are
      not two grades of one repair, they close DIFFERENT programs.

    CEILING, diffed BOTH WAYS against ltmintimpl's 19:
      ltmintimpl ∖ ltmintinst = ∅          (nothing lost)
      ltmintinst ∖ ltmintimpl = { lifetimes_issue-103582-hint-type-alias,
                                  nll_issue-55394--b,
                                  nll_propagate-fail-to-approximate-longer-no-bounds }
      `propagate-fail` is one of the two rows `lifereg_unmentbind` GAVE UP when
      it landed by name collision on 2026-08-31g. It comes back here through
      substitution, which is the mechanism that report said would return it.

## `current_lt_binders()` SURVIVES, AND NOW THERE IS A MEASUREMENT SAYING WHY

The instruction was: if the unsound name-collision approximation is SUBSUMED,
delete it. It is not, and the reason is rule 2 — the doors are in SERIES.

`ltsubstinst` is the substitution half ALONE, the half that was supposed to
replace it. It closes 3 rows at COST 0 in the pass column, the stdlib is green
— and it flips **three pinned illegal programs from rc 1 to rc 0**:

    bc_ltunmentbind_two_unrelated_binders              1 -> 0   ← u7's OWN TWIN
    projection-no-regions-closure--c30-direct-call     1 -> 0
    projection-no-regions-closure--projection-no-regions-closure  1 -> 0

Substitution instantiates the struct's binder at the literal (`'a ↦ 'b`), which
DELETES the name collision `current_lt_binders()` catches — and without minting
there is no named return region to catch it again downstream. **The replacement
is strictly negative until the mint is there too.** Under `ltmintinst` the same
program is refused at the RETURN instead, and u7 and u8 produce BYTE-IDENTICAL
diagnostics:

    error [fn mk]: return type mismatch: variance mismatch —
                   expected Holder<'a>, got Holder<'b>

That is the caveat removed — the answer no longer depends on how the struct's
binder is SPELLED — and it is removed only by the two halves together.

## THE `.expected` COLUMN — 13 MATCH-LOSSES, 1 TEXT-ONLY, ZERO rc CHANGES

All thirteen are one substitution and all thirteen stay RED. The refusal moves
from borrow_check's elision message to the type checker's variance message,
which is upstream's primary error arriving first (rule 14's stated exemption).
`bc_ltunmentbind_two_unrelated_binders` is the informative one: pinned at the
struct-literal FIELD, now refused at the RETURN — the field check passes because
the binder is correctly instantiated, and the return check refuses because the
fn claims `Holder<'a>` and built a `Holder<'b>`. Same verdict, better site.
These are re-baselines to take AT LANDING, not damage.

## HAND PROGRAMS, EACH MULTI-LINE, EACH ON THE ARMED BINARY

    c2  fn id(x:&i64)->&i64                       rc 0 unarmed and armed
    s1  impl Dog { fn get(&self)->&i64 }          rc 0 both (elision rule 3)
    v2  pick<'a> called from f<'a>                rc 0 both
    mx  max2<'r>(a:&'r i64,b:&'r i64) from &i64   rc 0 both
    w1  the stdlib view idiom, minimised          rc 0 both  ← THE WALL'S DOOR
    u7  struct binder spelled 'a                  rc 1 both
    u8  the same, binder renamed 'h               rc 0 unarmed → rc 1 armed,
                                                  SAME diagnostic as u7
    t1  trait default body + impl<'a>             rc 0 unarmed → rc 0 armed AFTER R4
    t2  t1 with the default written out           rc 0 both  ← R4's control

## THE CONTROL REVERT — ONE BINARY, TWO ARMS, ONE VARIABLE

`ltmintimpl` and `ltmintinst` ride the SAME build. Every repair is gated on the
new name, so the old arm must reproduce the previous round's price exactly, and
it does — all seven of `ltmintimpl`'s costs are still refused on this binary:

    region-two-refs-same-region-pick-rg          rc 1    (ltmintinst: rc 0)
    regions-infer-contravariance-due-to-ret      rc 1    (ltmintinst: rc 0)
    borrowck-unused-mut-locals                   rc 1    (ltmintinst: rc 0)
    regions-mock-codegen                         rc 1    (ltmintinst: rc 0)
    bc_ltscope_impl_legal_shapes                 rc 1    (ltmintinst: rc 0)
    bc_genrecv_two_mut_sequential_admit          rc 1    (ltmintinst: rc 1)
    bc_ltunmentbind_renamed_binder_hole          rc 1    (ltmintinst: rc 1)
    variance-option-ref-intersection-rg          rc 0    (ltmintinst: rc 1)

The last line is the one that matters: the round's ONLY genuine legal-program
refusal is attributable to R3'' alone, one variable apart, on one binary. It is
not an artefact of the build and not a decayed baseline.

⚠ OPERATIONAL, MEASURED HERE: `tests/logos/test-levels.sh` must be invoked with
the BUILD DIRECTORY as cwd. Run from the repo root it selects 683 names, runs
0, and reports "***Failed: ... only 0 ran" together with a failed gates tier —
a red that says nothing about the tree. From `build/` the same command is
745/745 with 346 gates green.

## WHAT IS OPEN, NAMED, WITH ITS EVIDENCE

 1. **REGION INFERENCE IS THE MISSING ENGINE, and this round is the first to
    price the claim.** Minting names every elided slot; substitution instantiates
    every binder; three repairs remove three real defects. What is left is one
    six-line program that needs a region NOBODY WROTE DOWN — the intersection of
    two minted inputs. 22 ledger rows are behind it.
 2. `bc_genrecv_two_mut_sequential_admit`'s `pick` is illegal Rust; repair it
    with an explicit `<'p>` when the engine lands, and pin the unspelled form.
 3. The refusal for that shape reads `expected &i64, got &i64` — both regions
    minted, both hidden by `type_str`. **A diagnostic that prints the same type
    on both sides is not pinnable.** Elision-aware wording is prerequisite work
    for landing, not a polish item.
 4. R4 (the `Self` lifetime-args defect) is a plain bug with a named root and is
    independent of the comparators. It is probe-gated here only because it was
    measured here; it deserves its own unconditional round.
 5. The 1 296 116 `Kind::Slice` arrivals are still unmeasured (2026-08-31i).
 6. The METHOD-call argument is still not a comparison site (6 `lifereg.A` rows).

Files: `include/logos/compiler/outlives.hpp` (`lt_is_minted`,
`current_lt_binders` — caveat STANDS, now with the ltsubstinst measurement
behind it), `src/compiler/sema_impl.hpp` (`build_call_lt_subst_`,
`inst_call_params_`, `structlit_lt_subst_`, `collect_param_regions_`),
`src/compiler/sema_expr.cpp` (four argument sites, two struct-literal field
sites), `src/compiler/sema_collect.cpp` (R4, two sites), `src/compiler/sema_decl.cpp`.

# ROUND 2026-08-31k — THE MEET, AND THE VARIANCE TABLE THAT LIED ABOUT IT

Ledger UNTOUCHED at 297. NOTHING LANDED. build **d1147552dc64f5cf** (READ,
`scripts/build_hash.py build`), L1 rc=0 with nothing armed — 745/745, 346 gates,
12 684 smoke — so every arm below is inert unarmed.

## THE VERDICT FIRST

    arm            fires      ceiling  cost  cost-fail (rc)         stdlib
    ltmintinst   24385266       22       3   14 changed, rc 0       green  ← control
    ltmintmeetrg 24385275       22       2   14 changed, rc 0       green  ← THE MEET
    ltmintmeet   24385281       22       2   15 changed, rc **1**   green
    ltmeetany    24385287       22       2   16 changed, rc **2**   green
    ltmeetco      3933773        3       0    5 changed, rc **3**   green

`ltmintmeetrg`'s `-L bc -L fail` table is **BYTE-IDENTICAL** to `ltmintinst`'s —
every rc, every stderr sha, every `.expected` match, all 1056 rows. The meet
closes ONE legal program and changes nothing else anywhere in the fail half.

    ceiling  ltmintmeetrg ∖ ltmintinst = ∅ ,  ltmintinst ∖ ltmintmeetrg = ∅
    cost     ltmintinst ∖ ltmintmeetrg = { variance-option-ref-intersection-rg }

**COST 2, AND BOTH ARE THE NON-COSTS 2026-08-31j NAMED:**
`bc_ltunmentbind_renamed_binder_hole` (its own header asks it to flip) and
`bc_genrecv_two_mut_sequential_admit` (illegal Rust by elision rule 3). The one
real legal-program refusal that held 22 rows is CLOSED.

## THE MEET IS EXPRESSIBLE WITHOUT CFG POINTS — HERE IS WHY, AND WHAT IT COST

A binder offered two different regions needs the region both outlive. The
lattice fact that makes this cheap: **at a COVARIANT occurrence, the meet's only
obligation — each offered region outlives it — is discharged by construction.**
So the meet needs no NAME and no solver: an elided slot is already the
comparators' spelling for "a region whose obligation is discharged here". At an
INVARIANT occurrence the demand is EQUALITY instead, and no meet discharges
that — which is exactly what the two pins pin.

The variance is not new work: `compute_variances()` (sema.cpp) has computed a
per-def, per-LIFETIME-param variance by fixpoint since B64, keyed `@i`. The meet
reads it. No CFG point is consulted, no program point exists in this arm, and no
constraint is ever propagated.

## ⚠ THE CENSUS FIRST (RULE 17) — AND IT MOVED THE SUBJECT TWICE

`census_meet_` counts, at EVERY first-occurrence-wins binder-binding walk in the
compiler, how many binders arrive and how many arrive with two or more DIFFERENT
incoming regions. It rides the gate's own build. Over **the whole `tests/` tree,
8679 files, 1 file = 1 logosc process**:

    site        binders   multi   co   inv   novariance
    structlit       125       5    4     1        0
    call             80       4    0     0        4     (a fn's binders have
    method           16       0    0     0        0      NO declared variance)
    enumlit           8       1    1     0        0

 1. **THE FIRST POPULATION I CHOSE WAS WRONG.** 2026-08-31i's census ran over
    `tests/imported/admit` + `tests/logos/pass`; I reused it, and it reports
    `meet.structlit.multi` = **0** — the subject fixture lives in
    `tests/imported/pass/variance/`, outside it. A census over the population
    that does not contain the subject reads exactly like a refuted hypothesis.
    Seventh instance this week of a handed-down site list being wrong, and the
    second where the list was mine.
 2. **THE WHOLE MULTI-CANDIDATE STRUCT-LITERAL POPULATION IS THREE PROGRAMS.**
    variance-option-ref-intersection-rg (2 binders, Co),
    nondeterministic-lifetime-errors-15034 (2, Co ⚠),
    account-for-lifetimes-in-closure-suggestion (1, Inv).
 3. **THE FREE-FN CALL HAS THE SAME DEFECT AND NO GUARD AVAILABLE** — 4
    multi-candidate binders, and nothing in this tree computes a variance for a
    fn's own binder, so the meet CANNOT be asked there. Named, not fixed.
 4. Under the substitution half alone the structlit multi count is **1** (the
    Inv one). The meet is a MINT-dependent question by construction: without
    minted names the elided slots offer no candidates to disagree.

## ⚠ THE SECOND INNER PREDICATE, AND THE MEASUREMENT THAT FORCED IT (RULE 9)

The covariance guard is exactly right as a lattice condition and it **still
un-refused a pinned illegal program**. `ltmintmeet` (variance guard only):

    nondeterministic-lifetime-errors-15034     rc 1 -> 0
    struct Lexer<'a> { input: &'a str }
    struct Parser<'a> { lexer: &'a mut Lexer<'a> }

`Parser`'s `'a` should be INVARIANT — `Lexer<'a>` sits under a `&mut`. The
fixpoint says **Co**. Three hand programs, one variable apart, on this binary:

    h1  struct L<'a> { i: &'a i64 }   struct P<'a> { l: &'a mut L<'a> }   Inv ✓
    h3  struct L<'a> { i: &'a str }   the SAME P                          Co ⚠
    h4  struct L<'a> { i: &'a [i64] } the SAME P                          Co ⚠

**`&'a str` AND `&'a [T]` CANONICALISE TO `Kind::Slice` AND THE REGION IS
DROPPED AT `resolve_type`** — 2026-08-31i's 1 296 116-arrival structural hole,
arriving somewhere nobody had looked. `'a` then appears in NO recorded position
of `L`, the fixpoint calls it **BiVar**, and BiVar composes to Co under the
`&mut`. **The variance table cannot tell "this binder appears nowhere" from
"this binder appears only where the type cannot record it"** — rule 16 at a new
site, and BiVar is the spelling of that confusion.

`ltmintmeetrg` adds the second predicate: a def with a region-losing slot
(`Slice` / `TraitObject` / `TaggedPtr`, or an unreadable def) anywhere in its
REACHABLE field types is **REGION-OPAQUE** and its declared variance is not
evidence. Both pins hold, h1/h3/h4 all refused, the subject still compiles.

## THE ABUSE DIRECTION, PRICED (the exemption checked where it is a hatch)

    ltmeetany  — the meet with NO guard at all.  ceiling 22, cost 2 in the PASS
    column, IDENTICAL to ltmintmeetrg — and **rc 2**: it un-refuses BOTH
    nondeterministic-lifetime-errors-15034 and
    account-for-lifetimes-in-closure-suggestion.

**THE PASS COLUMN CANNOT TELL THE THREE GUARDS APART.** 22/2 for all of them.
Only the `-L bc -L fail` rc column separates none from one from two un-refusals.
Third time this week that oracle has reversed a recommendation.

## RULE 13, AND RULE 5 — EACH WITH ITS HAND PROGRAM

`ltmeetco` is the meet on the SUBSTITUTION HALF with no mint: ceiling 3, cost 0,
and the SAME three un-refusals `ltsubstinst` has. **The meet's increment without
the mint is exactly zero**, predicted from the census before it was priced.

**COST 0 IS NOT A SAFETY CLAIM, and here is the counter-example, one field from
the subject:**

    m1  struct S<'a> { a: &'a i32, b: Option<&'a i32>, s: &'a str }
        fn build(a:&i32, b:Option<&i32>, s:&str) -> i32 {
            let v: S = S { a: a, b: b, s: s }; ... }
        LEGAL, and ltmintmeetrg REFUSES IT — `&'a str` makes S region-opaque, so
        the guard withholds a meet the program needs. The price of not trusting
        the variance table is paid by every def that touches a slice.
    m2  struct T<'a> { a: &'a i32, b: &'a mut i32 }  built from two regions
        unarmed rc 0 · ltmintinst rc 1 · ltmintmeetrg rc 0 — a SECOND legal
        program the meet closes (`&'a mut i32` is covariant IN `'a`).

    c2  fn id(x:&i64)->&i64                    rc 0 unarmed and every arm
    s1  impl Dog { fn get(&self)->&i64 }       rc 0 every arm (elision rule 3)
    v2  pick<'a> called from f<'a>             rc 0 every arm
    bc_ltunmentbind_legal_shapes               rc 0 both arms
    bc_ltscope_impl_legal_shapes               rc 0 both arms
    bc_ltunmentbind_two_unrelated_binders      rc 1 under EVERY meet arm,
                                               ltmeetany included

## THE CONTROL REVERT — ONE BINARY, TWO ARMS, ONE VARIABLE

`ltmintinst` re-priced on THIS build (d1147552dc64f5cf, builds 241→246):
ceiling 22, cost 3, cost-fail 14 changed with rc 0, stdlib green — last round's
price to the digit. The only difference between it and `ltmintmeetrg` is
`variance-option-ref-intersection-rg`, and the fail tables are byte-identical.

## PREDICTED vs MEASURED, BOTH DIRECTIONS (RULE 6)

Written to `predictions-meet.txt` before the first pricing run. **All four arms
matched, by name, in both directions** — the 22-row ceiling set, the 2-row cost
set, the one closed row, and the un-refusal counts 0 / 1 / 2 / 3 with the exact
fixture names. The census was what made the predictions cheap: the population
was three programs and they were all read before anything was priced.

## ⇒ WHAT THIS ROUND SETTLES, AND WHAT IT DOES NOT

 1. **THE MEET IS EXPRESSIBLE WITHOUT CFG POINTS.** No program point, no
    liveness, no constraint propagation — a lattice condition read off a
    fixpoint that has been in the tree since B64. The arc does NOT need region
    inference to reach cost 0.
 2. **`ltmintmeetrg` IS CEILING 22 / COST 0-REAL**, both remaining refusals
    being the two 2026-08-31j named as not costs. That is 297 → 275 with a
    corpus repair (`bc_genrecv_two_mut_sequential_admit` gets its `<'p>`) and a
    shelf move (`bc_ltunmentbind_renamed_binder_hole` → fail/).
 3. **IT MUST NOT LAND ON THIS GUARD.** The region-opaque predicate is a
    conservative stand-in for a variance table that is WRONG whenever a binder
    lives only in a `Kind::Slice`, and m1 measures what that stand-in costs:
    a legal program, refused, one field from the subject. The real repair is
    to give `&[T]` / `&str` / `&dyn` / `&Dst` a region slot — the 1 358 164
    arrivals named on 2026-08-31i, now with a second consumer.
 4. **THE FREE-FN CALL SITE HAS THE SAME MULTI-CANDIDATE DEFECT AND NO
    VARIANCE TO GUARD IT** (4 arrivals). Nothing computes a variance for a fn's
    own binder. Named here; not attempted.
 5. The diagnostic still prints `expected Option, got Option` — both regions
    minted, both hidden by `type_str`. Elision-aware wording remains
    prerequisite work for landing (2026-08-31j item 3), and m1's message is a
    fresh instance.

Files: `include/logos/compiler/probe.hpp` (`census_armed`, the `std::string`
census overload, `arm_inst` / `arm_subst` — one process arms ONE name, so a
compound arm has to answer at every gate its components use),
`src/compiler/sema_impl.hpp` (`LtCands`, `census_meet_`, `binder_variance_`,
`binder_is_covariant_`, `type_region_opaque_`, `decl_fields_region_opaque_`,
`structlit_lt_subst_`, `build_call_lt_subst_`), `src/compiler/sema_expr.cpp`
(the two struct-literal sites now pass a variance key; the method and the two
enum-literal sites are censused only), `src/compiler/sema_decl.cpp`,
`src/compiler/sema_collect.cpp` (arm renames only).

# ROUND 2026-08-31l — THE MEET'S GUARD REACHES COST 0, AND THE ENGINE STILL DOES NOT LAND

Ledger UNTOUCHED at **297**. NOTHING LANDED to the ledger, and the engine was
un-landed AFTER it had been landed in the working tree — see §5, which is the
round's finding. Build **366d6784e4813abf** (READ, `scripts/build_hash.py
build`); the pricing rode `c1153ededa69456b`. One new native fixture, a COST
SENSOR, and the census pin moved 8727 → 8728 with the delta predicted first.

Opening baselines, READ FROM THE STORE (build 241, `d1147552dc64f5cf`):
`-R '^logos_00_bc_admit_'` — 297 in the filter, 1382 recorded, 0 failed;
`-L bc` — 1056 / 1056 passed.

## 1. THE VERDICT

    arm            ceiling  cost(pass)  cost(fail)            stdlib
    ltmintinst        22         3      14 chg, rc 0, 13 .exp  green   ← control
    ltmintmeetrg      22         2      14 chg, rc 0, 13 .exp  green   ← control
    ltmintmeetamb     22         2      14 chg, rc 0, 13 .exp  green   ← THIS ROUND

All three re-priced ON ONE BINARY (builds 247 unarmed → 248/249/250 armed), so
the two controls are a revert, not a memory of last round's numbers. Diffed
BOTH WAYS, mechanically:

    ceiling   amb ∖ rg = ∅   rg ∖ amb = ∅   amb ∖ inst = ∅   inst ∖ amb = ∅
    cost      amb ∖ rg = ∅   rg ∖ amb = ∅
              inst ∖ amb = { variance-option-ref-intersection-rg }  ← the subject
    fail      failtext-*-ltmintmeetamb.tsv is BYTE-IDENTICAL to both
              -ltmintinst.tsv and -ltmintmeetrg.tsv, all 1056 rows

`cost(amb)` is the two 2026-08-31j named as NOT costs. **In every population
this harness owns, the sharpened meet is ceiling 22 at cost 0-real.**

## 2. THE SECOND INNER PREDICATE, SHARPENED — AND WHY IT IS SOUND

2026-08-31k withheld the meet whenever a region-losing slot was reachable AT
ALL, and priced its own counter-example: `struct S<'a> { a: &'a i32, b:
Option<&'a i32>, s: &'a str }` is LEGAL and rg refuses it, one field from the
subject. The sharpening is one observation:

**A region `resolve_type` drops is always the region OF A REFERENCE, and a
reference's own region occurs COVARIANTLY in its own position. So the
contribution the variance fixpoint omitted is exactly the AMBIENT there.**
Omitting a Co contribution cannot move a variance in the PERMISSIVE direction
(`variance_meet(V,Co)` differs from V only by BiVar→Co, and BiVar already reads
as covariant). Omitting a NON-Co one can — that is the `&mut` in the 15034 pin.
⇒ the declared variance is evidence unless a region-losing slot is reachable
from the def's fields **at a non-covariant ambient**. Conservative (Inv) at an
unreadable def, at the depth cap, and at a nested def's type argument whose
declared variance is missing or BiVar.

MEASURED, one variable apart, on the landing binary:

    m1  S<'a>{a:&'a i32, b:Option<&'a i32>, s:&'a str}   rg rc 1 → amb rc 0 ✓
    m2  T<'a>{a:&'a i32, b:&'a mut i32}                  rc 0 both
    p1  SliceHold<'a>{a:&'a i32, b:&'a [i64]}            rc 0 both
    q1  Wrap<'w>{s:&'w str}; Q<'a>{x:&'a i32, y:&'a mut Wrap<'a>}
        THE ABUSE DIRECTION: rc 0 under `ltmintmeet` (covariance guard ALONE —
        it ACCEPTS an illegal program), rc 1 under amb, rc 1 unarmed ✓
    q2  the same with the slice removed                  rc 1 every arm
    nondeterministic-lifetime-errors-15034               rc 0 under ltmintmeet,
                                                         rc 1 under amb ✓ PIN
    account-for-lifetimes-in-closure-suggestion          rc 1 under amb ✓ PIN
    variance-option-ref-intersection-rg                  rc 0 ✓ THE SUBJECT

q1 and 15034 are the proof the predicate is LIVE (rule 1): with only the
covariance guard both compile.

## 3. PREDICTED vs MEASURED (rule 6)

`predictions-meetamb.txt`, written before the first edit: ceiling 22 same set,
cost 2 by name, fail table byte-identical with 0 un-refusals, stdlib green, and
m1 rc 1→0 with both pins held. **Every line matched, in both directions.**

## 4. ⚠ THE FINDING: THE `.expected`-LOSS COLUMN IS A LIST OF HYPOTHESES, NOT A RE-BASELINE

Rule 14 exempts the engine's 13 `.expected` losses: rc stays 1, and upstream's
primary error arrives first. Both halves of that are TRUE and it is still not
the whole reading. **A refusal whose REASON moved says a program one construct
away is now refused FOR THE NEW REASON — and that program may be legal.** The
13 losses are 13 such hypotheses. I wrote the twin of exactly one:

    tests/imported/fail/borrowck/borrowck-swap-mut-base-ptr  (upstream E0502)
    reason MOVED: "cannot borrow 't0' as mutable" → "call to 'swap_ref' arg 2:
    variance mismatch — expected &mut &mut i64, got &mut &mut i64"

Delete the two lines that make it illegal (the live `&*t0` across the swap) and
the remainder is an ordinary legal program:

    fn foo(a: &mut i64, b: &mut i64) -> i64 {
        let mut t0 = a;  let mut t1 = b;
        swap_ref(&mut t0, &mut t1);
        *t1 = 22i64;  return *t0; }

**CONTROL REVERT, ONE BINARY, ONE VARIABLE: rc 0 unarmed; rc 1 under
`ltmintinst`, under `ltmintmeetrg` and under `ltmintmeetamb`.** It is a
legal-program refusal that the pass corpus, the 1056-row fail oracle and all
four stdlib layers could not see, because the only near-by program in the
corpus was its ILLEGAL twin, already red.

## 5. SO THE ENGINE WAS UN-LANDED, AND WHAT REMAINS IS NAMED

The engine WAS landed in the working tree — every probe gate made
unconditional, the arms deleted from `probe.hpp`, an elision-aware wording for
the "both sides print alike" diagnostic (2026-08-31j item 3), the 13 `.expected`
re-pins computed. The whole of it was reverted when the control above came in.
ONE LEGAL REFUSAL IS ONE.

**THE MECHANISM, READ OFF THE PROGRAM.** `swap_ref<T>(a:&mut T, b:&mut T)`
binds `T` from ARGUMENT 1 — minted regions and all — and argument 2 is then
compared against the BOUND `T`. That is first-occurrence-wins at a TYPE
parameter: the same defect the struct-literal meet closed for lifetime binders,
one binder-kind over. And the meet CANNOT be asked here — `T` occurs under
`&mut`, so the demand is EQUALITY, not "a region both outlive".

    MEASURED, and it narrows the class: the NON-GENERIC form of the same swap
    (`fn both(a:&mut &mut i64, b:&mut &mut i64)`, two locals) is rc 0 under
    every arm. The site is the GENERIC BINDING, not `&mut`.

Rust accepts the program because both regions are inference VARIABLES and get
equated; ours are two fixed minted names. **What is left after the meet is
UNIFICATION OF MINTED REGIONS** — union-find over minted names, no program
point, no liveness, no constraint over CFG points. It is not region inference
and it is not this round's work: a unification that may equate a minted region
with a NAMED one un-refuses the 15034 pin by construction, so the arm has to be
priced with the pins in the population, not reasoned about.

## 6. THE SENSOR, SO THE NEXT ROUND'S COST COLUMN CAN SEE IT

`tests/logos/pass/bc_ltmintgen_two_minted_regions_one_typaram.logos` — the legal
program above, landed as a PASS fixture with its illegal twin named in the
header. A hand program dies with the round that wrote it; a fixture is what
ratchets. Census pin 8727 → 8728 / 4466 → 4467 / 346, delta predicted before
the gate was read.

## 7. OPEN, WITH ITS EVIDENCE

 1. **UNIFICATION OF MINTED REGIONS AT A GENERIC BINDER** — §5. One legal
    program measured; the class is unenumerated (rule: I can find a class and
    cannot close it).
 2. **THE OTHER 12 `.expected` LOSSES HAVE NOT HAD THEIR TWINS WRITTEN.** One of
    thirteen was a cost. That is the cheapest instrument this round found and it
    is not exhausted.
 3. `&[T]` / `&str` / `&dyn` / `&Dst` still cannot carry a region (1 358 164
    arrivals). §2 is a workaround for the consequence, not a repair: it reasons
    about the ambient of a slot whose region was thrown away.
 4. The free-fn call's 4 multi-candidate binders still have no variance to guard
    them (2026-08-31k §1).
 5. The `expected Option, got Option` wording was written and reverted with the
    landing; it is prerequisite work whenever the engine goes back in.

# ROUND 2026-08-31m — THE REGION SLOT: `&'a [T]`, `&'a str`, `&'a dyn`, `&'a Dst` RECORD THEIR REGION

Ledger UNTOUCHED at **297**. NOTHING LANDED to the ledger. Build
**ce3eb5cc50e53b5d** (READ, `scripts/build_hash.py build`); L1 rc=0 with nothing
armed — 745/745, 346 gates, 12 684 smoke — so every arm below is inert unarmed.
Opening baselines READ FROM THE STORE (build 252, unarmed): `-L bc` 1056/1056;
`-R '^logos_00_bc_admit_'` unchanged.

## 1. THE VERDICT

    arm             fires      ceiling  cost(pass)   cost(fail)              stdlib
    ltmintmeetamb  24395096      22         3        14 chg, rc 0, 13 .exp   green  ← control
    ltregslot      21555290       2         0         1 chg, rc 0,  1 .exp   green  ← THE SLOT ALONE
    ltregmeet      46049528      24         3        14 chg, rc 0, 13 .exp   green  ← SLOT + ENGINE

All three re-priced ON ONE BINARY (unarmed build 252 → armed 253/254/255), so
`ltmintmeetamb` is a CONTROL REVERT, not a memory of 2026-08-31l's numbers — and
it reproduces that round to the digit (22 / 3; the 3rd cost is the sensor 31l
itself landed). Diffed BOTH WAYS, mechanically, over the printed name sets:

    ceiling   regmeet ∖ amb = { regions-glb-free-free--f-control-whole,
                                regions-trait-object-subtyping }   ← EXACTLY the slot's two
              amb ∖ regmeet = ∅
              regslot ∖ regmeet = ∅      regslot ∩ amb = ∅
    cost      regmeet ∖ amb = ∅          amb ∖ regmeet = ∅
              regslot ∖ regmeet = ∅      (the slot's cost set is EMPTY here)
    fail      failtext-…-ltregmeet.tsv is BYTE-IDENTICAL to
              failtext-…-ltmintmeetamb.tsv — sha 9b3deb66eef559b6, all 1056 rows,
              every rc, every stderr sha, every `.expected` match.

⇒ **THE SLOT AND THE ENGINE ARE DISJOINT AND EXACTLY ADDITIVE: 22 ⊎ 2 = 24, cost
UNCHANGED at 3, and the fail half is not touched at all by the slot's presence.**
Rule 13 measured in both directions; here the increment is neither negative nor
overlapping, and that is a fact about these two mechanisms, not a default.

**The three costs are the three already named as NOT costs**:
`bc_ltunmentbind_renamed_binder_hole` (its own header asks it to flip),
`bc_genrecv_two_mut_sequential_admit` (illegal Rust by elision rule 3), and
`bc_ltmintgen_two_minted_regions_one_typaram` (2026-08-31l's own cost sensor —
the generic-binder unification question, untouched by this round).

## 2. ⚠ THE CENSUS FIRST (RULE 17) — AND ITS FIRST DRIVER WAS WRONG

`census()` counts every arrival at the sites where `resolve_type` /
`subst_type_sema` throw a region away, split by whether a lifetime was WRITTEN
there. Over the `tests/` tree — 8680 files listed, **4700 contributing** (the
rest need module/stdlib context and exit before type resolution; the
contributing count is PRINTED next to the totals so an under-walked population
cannot read as a zero):

    site (resolve_type / subst_type_sema)   kind produced   elided    written
    ────────────────────────────────────────────────────────────────────────
    SLICE_TYPE            `&[T]` `&'a [T]`  Slice          200 320         32
    DYN_TYPE              `&dyn` `&'a dyn`  TraitObject     57 793         78
    REF_TYPE → &Unsized   `&str` `&'a str`  Slice           23 519         23
    REF_TYPE → &Dst                         DstRef           4 216        732
    MUT_REF_TYPE → &mut Dst                 DstRef             220          0
    REF/MUT_REF → &dyn (via UnsizedDyn)     TraitObject          0          0
    TAGGED_TYPE          `&tagged<TS> Tr`   TaggedPtr            0          0
    ────────────────────────────────────────────────────────────────────────
    TOTAL                                                  286 068        865

⚠ **THE FIRST RUN OF THIS CENSUS WAS WRONG, AND ONLY THE CONTRIBUTOR COUNT
SHOWED IT.** Its driver reported 8 buckets over 8680 files with NO `*.written`
bucket at all — a table that reads as the substantive claim "nobody in this
corpus writes a lifetime on a slice". 183 of 8680 processes had actually
contributed. `nondeterministic-lifetime-errors-15034` — the fixture the whole
arc pins — contributes `regslot.ref.slice.written 1` on its own. Eighth instance
this week of a population that excludes its own subject, and the FIRST caught by
an instrument rather than by the answer looking implausible: the repair was to
print how many processes contributed, beside the totals. A census with no
denominator is not a measurement.

`TAGGED_TYPE`'s two zeros are also a measurement: the grammar has no
lifetime-carrying `tagged_type` alt, so that slot has a writer and no source
that can reach it. Recorded rather than assumed.

## 3. THE CONSUMERS, CLASSIFIED BY DIRECTION

Every `.lifetime()` reader in the tree, and whether a region present on a
`Slice` / `TraitObject` / `DstRef` / `TaggedPtr` can REACH it:

    consumer                                  site               keyed on             reachable?
    ─────────────────────────────────────────────────────────────────────────────────────────────
    variance_in_type · Ref/MutRef arm         sema.cpp           Ref/MutRef           no
    variance_in_type · TraitObject arm        sema.cpp           TraitObject +        ⚠ YES — AND THE
                                                                 .lifetime()             READER PREDATES
                                                                                         ANY WRITER
    variance_in_type · Slice/DstRef/Tagged    sema.cpp           elem only / BiVar    only after this
                                                                                      round's arms
    ⇒ compute_variances → variance_table_     sema.cpp                                C1
    ⇒ subtype() · Struct/Enum lt-arg arm      subtype.hpp        variance_table_      ⚠ REFUSING
    borrow_check param_lifetimes_ / ret_lt    borrow_check.cpp   is_ref_kind(), which ⚠ REFUSING — AND
                                                                 ALREADY counts Slice/   THE READER
                                                                 TraitObject/non-owning  PREDATES ANY
                                                                 DstRef                  WRITER
    types_equal_with_lifetimes · Slice arm    subtype.hpp        elem only            no
    subtype() · Slice arm                     subtype.hpp        (equal ⇒ true)       no
    structlit_lt_subst_ walk                  sema_impl.hpp      Ref/MutRef, Struct/  no
    build_call_lt_subst_ walk                 sema_impl.hpp       Enum/Tuple          no
    collect_param_regions_                    sema_impl.hpp      Ref/MutRef           no
    mint_type_lts_                            sema_impl.hpp      Ref/MutRef           no
    undeclared-lifetime walks (×3)            sema_decl.cpp      Ref/MutRef, Struct   no — would REFUSE
                                                                 lt_args              if extended; NOT
                                                                                      extended
    walk_implied / has_elided_ref             sema_decl.cpp      Ref/MutRef           no
    impl-match unify (×2)                     sema_collect,      Ref/MutRef both      no
                                              mono_clone          sides
    type_str                                  sema.cpp           Ref/MutRef           no — DIAGNOSTIC
                                                                                      TEXT UNCHANGED
    mangle_type                               mono_impl.hpp      never reads it       no
    compute_type_uid                          sema.cpp           OMITS it for these   no
                                                                 four kinds

**EXACTLY TWO CHANNELS CAN NEWLY REACH A REFUSING BRANCH.** Both were named
before the run, and both are `.lifetime()` READERS THAT ALREADY EXISTED with no
writer anywhere in the compiler — rule 16 twice over, at two sites, in one kind.

## 4. ⚠ THE SLOT CLOSES LIVE UN-REFUSALS THE CORPUS HAD NO INSTRUMENT FOR

`is_ref_kind()` in borrow_check has counted `Kind::Slice`, `TraitObject` and
non-owning `DstRef` as ref kinds since logos-core 2.1 — its own comment says why
(`fn bad() -> &dyn T { return &local; }`). It then reads
`param_lifetimes_[p] = TypeRef(pt).lifetime()` and
`ret_lt = TypeRef(ret_type_).lifetime()` off a slot that has been EMPTY since
the kind existed. So `check_return_value`'s explicit-return-lifetime contract has
NEVER ONCE RUN for a fat-pointer return type. MEASURED, one variable apart, on
this binary:

    ILLEGAL, admitted unarmed, refused under ltregslot:
      fn mix<'a>(x:&'a [i64], y:&[i64]) -> &'a [i64]      { return y; }   rc 0 → 1
      fn pickstr<'a>(x:&'a str, y:&str) -> &'a str        { return y; }   rc 0 → 1
      fn pickdyn<'a>(x:&'a dyn Sp, y:&dyn Sp) -> &'a dyn Sp { … }         rc 0 → 1
    LEGAL, admitted BOTH ways (the refusal is not a blanket one):
      the same three with both params at `'a`, plus a fully elided
      `fn(&str) -> &str`                                                  rc 0 / rc 0

And the ledger agrees, by name: **`ltregslot`'s two ceiling rows are BOTH
known-wrong ADMISSIONS, and both are upstream rejections:**

    regions-trait-object-subtyping   fn foo3<'a,'b>(x:&'a mut dyn Dummy) -> &'b mut dyn Dummy
                                     header: "upstream lifetime may not live long enough"
    regions-glb-free-free--f-control-whole
                                     struct Flag<'a>{name:&'a str, desc:&'a str}
                                     fn h<'a,'b>(f: Flag<'b>) -> Flag<'a> { return f; }
                                     header: "upstream E0621 explicit lifetime required"

One is the `&mut dyn` region; the other is a struct whose BOTH fields are
`&'a str`, so its binder was BiVar — "recorded nowhere" — and `lifetime_at`
returns TRUE unconditionally for BiVar. That is the un-refusal the fixpoint's
own confusion was producing, closed at the site that produced it.

## 5. THE ACCEPTANCE TEST, BEFORE ANY LEDGER ROW (step 5)

`compute_variances` now emits its own answer under `LOGOS_CENSUS`
(`var.<pkg.Def>.@<i>=<Variance>`), so the h1/h3/h4 verdicts 2026-08-31k read
INDIRECTLY off the meet's behaviour are a DIRECT measurement here.

    program                                        unarmed → ltregslot
    h1  L<'a>{i:&'a i64}   P<'a>{l:&'a mut L<'a>}  L @0 Co    → Co     P @0 Inv → Inv  (CONTROL)
    h3  L<'a>{i:&'a str}   the SAME P              L @0 BiVar → Co     P @0 Co  → Inv  ⇐ SUBJECT
    h4  L<'a>{i:&'a [i64]} the SAME P              L @0 BiVar → Co     P @0 Co  → Inv  ⇐ SUBJECT
    q1  Wrap<'w>{s:&'w str}; Q<'a>{x:&'a i32,y:&'a mut Wrap<'a>}
                                                Wrap @0 BiVar → Co     Q @0 Co  → Inv
    m1  S<'a>{a:&'a i32,b:Option<&'a i32>,s:&'a str}
                                                   S @0 Co    → Co     (unchanged — and that is
                                                                        the point: S IS covariant)

**h1, h3 AND h4 ALL READ Inv.** The table no longer confuses "this binder appears
nowhere" with "this binder appears only where the type cannot record it".

    fixture / program                        unarmed  inst  amb  regslot  regmeet
    m1  (LEGAL)                                 0       1    0      0        0    ✓ ADMITTED
    q1  (ILLEGAL)                               1       1    1      1        1    ✓
    q2  (ILLEGAL, q1 with the slice removed)    1       1    1      1        1    ✓
    nondeterministic-lifetime-errors-15034      1       1    1      1        1    ✓ PIN
    account-for-lifetimes-in-closure-suggest.   1       1    1      1        1    ✓ PIN
    variance-option-ref-intersection-rg         0       1    0      0        0    ✓ SUBJECT
    bc_ltmintgen_two_minted_regions_one_typaram 0       1    1      0        1    (31l's sensor,
                                                                                   unchanged)

**`ltregmeet` REPRODUCES `ltmintmeetamb` PROGRAM FOR PROGRAM WITH NO STAND-IN
PREDICATE AT ALL.** Its guard is `binder_is_covariant_` and nothing else, because
`probe_meet_opaque_guard_()` and `probe_meet_amb_guard_()` are both false under
it. m1 — the legal program that refuted 2026-08-31k's first stand-in — is
admitted, and both pins hold, on the plain lattice condition.

## 6. WHAT THE SLOT REPLACES, AND WHY ITS OWN ARGUMENT SAYS SO

    2026-08-31k  type_region_opaque_ / decl_fields_region_opaque_ — withhold the
                 meet whenever a region-losing slot is reachable AT ALL. Priced
                 its own counter-example (m1) the same round.
    2026-08-31l  region_loss_noncov_ / def_lt_var_ /
                 decl_fields_region_evidence_bad_ — "…unless it is reached at a
                 COVARIANT ambient, because the contribution the fixpoint OMITTED
                 IS THE AMBIENT AT THAT POSITION."

That second argument is the proof that the slot is the right repair rather than a
third stand-in: it says the missing contribution is exactly the ambient — which
is precisely what `variance_in_type`'s new Slice / DstRef / TaggedPtr arms now
record DIRECTLY, with no reachability walk, no depth cap, and no conservative
fallback at an unreadable def. ~150 lines of reasoning ABOUT a discarded slot,
replaced by not discarding it.

⚠ BOTH ARE KEPT IN THE TREE under their own probe names. They are this round's
CONTROLS; deleting them here would delete the revert.

## 7. THE REPRESENTATION QUESTION (step 4), ANSWERED: NO WIDER REWRITE

`LogosTypeBuilder` has carried a `lifetime` string for every kind all along, and
`TypeRef::lifetime()` reads it back. The four fat-pointer kinds simply never had
it written. Two facts already in the tree — both put there FOR `Ref` — are what
make writing it cheap:

 1. **`compute_type_uid` OMITS the lifetime** ("matches types_equal semantics").
    `&'a [T]` and `&[T]` therefore have the SAME TypeUID: `types_equal`,
    `mangle_type`, every mono spec key, every layout decision are byte-unchanged.
 2. **`builder_equals_typeref` DISTINGUISHES it** ("borrow_check reads
    .lifetime() off the ptr"). The two get distinct POOL ENTRIES and the region
    survives — exactly how `&'a T` vs `&T` has always worked.

⇒ the edit: three constructors take an optional `lt`; four `builder_equals`
cases compare it; `variance_in_type` grows Slice / DstRef / TaggedPtr arms
(TraitObject's has read `t.lifetime()` since logos-core 2.3 and had no writer);
eight `resolve_type` / `subst_type_sema` sites pass `lt` instead of dropping it.
**+209/−26 over three files**, and all four stdlib layers compile under both arms.

**AND THE AST ALREADY CARRIED IT.** `logos.peg`'s `slice_type` has captured the
lifetime into a LIFETIME slot since L2, with the comment "downstream sema
currently doesn't enforce slice-with-lifetime distinctly from plain &[T]";
`dyn_type`'s `&'a dyn` alts likewise, "downstream sema is currently elision-based
so the slot is informational". Two grammar comments, a `variance_in_type` arm and
an `is_ref_kind()` list all describing the same missing eight lines.

This was flagged as the most invasive change of the arc. It is not. Making these
kinds fully region-GENERIC would be, and was not attempted; the measured need
(2026-08-31k §3) was that the region be RECORDED where variance and the meet can
read it, and that is a data-flow repair.

## 8. ⚠ THE COST THE THREE POPULATIONS COULD NOT SEE — AND THE INSTRUMENT THAT SAW IT

`ltregslot` printed **COST 0 on the pass corpus, 0 rc-changes across 1056 fail
fixtures, and all four stdlib layers green**. It refuses a legal program.

2026-08-31l §4's instrument: a `.expected` LOSS is a HYPOTHESIS — a refusal whose
REASON moved says a program one construct away is now refused FOR THE NEW REASON,
and that program may be legal. `ltregslot` produced exactly ONE such loss, so the
instrument cost one program to apply:

    tests/imported/fail/regions/regions-trait-variance   (upstream E0515)
    reason MOVED: "cannot return reference to local variable 'b'" (borrow_check)
                → "return type mismatch: variance mismatch — expected A<'a>, got
                   A<'r>" (the type checker)

Delete the two lines that make it illegal — the local `b` and the borrow of it —
and the remainder is an ordinary legal program. CONTROL REVERT, ONE BINARY:

    unarmed rc 0 · ltregslot rc 1 · ltregmeet rc 0 · ltmintmeetamb rc 0

**THE MECHANISM, READ OFF THE PROGRAM.** `A<'r> { p: &'r dyn X }` records `'r`
on a `Kind::TraitObject`, so `A`'s binder is Co instead of BiVar and the return
type is genuinely compared. The VALUE side records nothing: the unsize coercion
`&'a B` → `&'a dyn X` (sema_expr.cpp) builds its TraitObject with an empty
region. **DECLARATION SIDE RECORDED, VALUE SIDE NOT** — so `'r` binds to nothing
and `A<'r>` is not `A<'a>`. The engine's mint and substitution close it, which is
why `ltregmeet` admits the same program.

⇒ Second round running in which this instrument reversed a reading that three
populations called clean. It is still not exhausted: the ENGINE's 13 `.expected`
losses have had **two** of thirteen twins written (borrowck-swap-mut-base-ptr on
2026-08-31l, and now this one, which is the slot's own).

Landed as `tests/logos/pass/bc_ltregslot_dyn_field_coerced_arg.logos` with its
illegal twin named in the header — a hand program dies with its round.

## 9. PREDICTED vs MEASURED, BOTH DIRECTIONS (RULE 6)

`predictions-regslot.txt`, written before the first pricing run.

    ltregslot ceiling: PREDICTED 0 ("nothing in the ledger is closed by recording
      a region only the variance fixpoint reads"). MEASURED **2**, and the
      prediction named the reason it was wrong in its own text: it listed C1 and
      C2 as REFUSING channels and then forgot that the ledger's rows ARE
      refusals owed. Both rows are un-refusals C2 and C1 close. Direction
      right, sign wrong.
    ltregslot cost:    PREDICTED 1-5 "and I cannot name them"; MEASURED 0 in
      every population the harness owns — and then 1, by an instrument outside
      all three (§8). The prediction's honest "I cannot name them" was the
      accurate half.
    ltregslot fail:    PREDICTED rc-changes 0 with some `.expected` movement.
      MEASURED exactly that: 1 `.expected` loss, 0 rc changes.
    ltregmeet:         PREDICTED ceiling 22 same set as amb; MEASURED **24** —
      22 ⊎ the slot's 2, which the prediction did not compose. Cost PREDICTED
      "2 + slot, floor 3 by the sensor"; MEASURED 3, the same three by name.
      Stdlib PREDICTED at risk; MEASURED green for both arms.
    MONOTONICITY:      PREDICTED "an un-refusal under this arm would be a DEFECT",
      derived from `lifetime_at`'s BiVar-returns-true. MEASURED: 0 rc 1→0
      transitions in 1056 fail fixtures under either arm. The claim survives —
      but §8 shows it was never the whole safety question, because the cost that
      appeared is a legal program the corpus does not contain.

## ⇒ 10. WHAT THIS ROUND SETTLES

 1. **THE FOUR FAT-POINTER KINDS CAN CARRY A REGION, AND IT DID NOT NEED A WIDER
    REWRITE.** Step 4's stop condition was not met; the wall was not there.
 2. **THE VARIANCE FIXPOINT NO LONGER LIES ABOUT A SLICE** — h3/h4 read Inv,
    measured directly off `compute_variances` rather than inferred from a refusal.
 3. **THE MEET RUNS ON THE PLAIN LATTICE CONDITION.** Both stand-in predicates
    are dead under `ltregmeet`; m1 is admitted and both pins hold.
 4. **THE SLOT IS A SOUNDNESS REPAIR IN ITS OWN RIGHT** — two ledger rows and
    three hand programs, all upstream rejections this compiler admitted, in a
    branch that has never executed for a fat-pointer return type.
 5. **IT DOES NOT LAND, AND THE REASON IS NAMED AND SMALL.** `ltregmeet` is
    ceiling 24 / cost 3-all-named / fail-table-identical-to-its-control /
    stdlib green — and `ltregslot` alone refuses one legal program because the
    unsize-coercion site does not record a region. That is one deposit site, not
    a design problem, and it is the next round: **carry the region across the
    unsize coercion and the other value-side Slice/TraitObject constructors**,
    then re-price the slot ALONE and see whether its cost is still 0 when an
    instrument outside the three populations is pointed at it.

## 11. OPEN, WITH ITS EVIDENCE

 1. **THE VALUE SIDE RECORDS NO REGION** — §8. `resolve_type` and
    `subst_type_sema` write the slot; every other Slice/TraitObject/DstRef
    constructor in the tree (sema_expr's unsize coercion, sema_stmt, mono_clone,
    mono_subst — dozens) writes an empty one. Correct for the fixpoint, which
    reads DECLARED field types; the measured cost of the asymmetry is §8's one
    program.
 2. **`subst_type_sema`'s DstRef arm returns early on `type_args().empty()`**, so
    a `&'a Dst` with no type args carries its region through unchanged but does
    NOT map it through `ls`. Asymmetric with the Slice and TraitObject arms,
    which do. 732 `ref.dst.written` arrivals in the census; not measured
    separately, named here rather than assumed harmless.
 3. **MINTING INTO AN ELIDED SLICE SLOT** — not attempted. `fn id(x:&[i64]) ->
    &[i64]` is still related by elision alone.
 4. **UNIFICATION OF MINTED REGIONS AT A GENERIC BINDER** (2026-08-31l §5) —
    untouched; `bc_ltmintgen_two_minted_regions_one_typaram` is still a cost.
 5. **ELEVEN OF THIRTEEN `.expected` TWINS ARE STILL UNWRITTEN.** Two of two
    written have been costs.
 6. The free-fn call's 4 multi-candidate binders still have no variance to guard
    them (2026-08-31k §1).
 7. The `expected Option, got Option` wording remains prerequisite work.

Files: `include/logos/compiler/probe.hpp` (`arm_regslot`),
`src/compiler/sema_impl.hpp` (`make_slice_type` / `make_dst_ref` /
`make_trait_object` take an optional region; `probe_meet_` learns `ltregmeet`),
`src/compiler/sema.cpp` (`builder_equals_typeref` ×4 kinds; `variance_in_type`
Slice / DstRef / TaggedPtr arms; `compute_variances`' census; the six
`resolve_type` sites and two `subst_type_sema` ones),
`tests/logos/pass/bc_ltregslot_dyn_field_coerced_arg.logos` (+ `.expected`).

# ═══ ROUND 2026-08-31n — THE ELISION ENGINE AND THE REGION SLOT LAND ════════

**LEDGER 297 → 276. TWENTY-ONE ROWS.** ⚠ WHICH BUILD EACH NUMBER RODE: every
gate below is `logosc 0.42.0-preview+main-gb204fe82-dirty.20260831T165634Z (libs
0b6d8ebdae50d23d)`, gate-db build 266, READ via `scripts/build_hash.py build`.
The final `cmake --build` before the commit reports `1991911e2daa5230` with NO
source changed — logosc and the stdlib archives both carry a build TIMESTAMP, so
a relink moves the hash. Not chased; named. L1 **746/746**, gates tier 325, the
enumerator's 12 684 smoke cases green; **the full L4 sweep, `FORCE=1`,
4453/4453** and **`L4 bc`, `FORCE=1`, 1384/1384** (2 disabled, gate-db build
266). All four stdlib layers compile.
Opening baselines, READ from the store on the unarmed pre-round tree
(gate-db build 256): `-R '^logos_00_bc_admit_'` 297/297, `-L bc` 1951 passed /
0 failed / 2 disabled.

## 0. THE WALL THAT WASN'T — AND THE CONTROL THAT DISSOLVED IT

2026-08-31m stopped at ONE legal program: `ltregslot` refuses
`bc_ltregslot_dyn_field_coerced_arg`, and the named cause was the VALUE SIDE —
the unsize coercion `&'a B → &'a dyn X` recording no region. This round's first
measurement was the THIN TWIN of that program, one token apart:

    struct A<'r>{ p: &'r dyn X }   … refused under ltregslot, admitted unarmed
    struct A<'r>{ p: &'r i64   }   … REFUSED UNARMED, on the pre-round binary,
                                     with the IDENTICAL message
        "return type mismatch: variance mismatch — expected A<'a>, got A<'r>"

⇒ **THE SLOT INTRODUCED NOTHING. It removed an accident that was hiding a
pre-existing refusal for one more shape**: with the region thrown away, `A`'s
binder read BiVar and `lifetime_at` returns TRUE unconditionally for BiVar. The
repair is the callee-binder SUBSTITUTION the engine already had, which fixes
BOTH shapes — and it is why `ltregmeet` admitted the program all along. No
value-side deposit was needed and none was made.

⚠ **A TWIN INSTRUMENT NEEDS ITS OWN CONTROL TWIN.** 2026-08-31m's twin varied
ONE variable (delete the illegality) and read the result as a cost of the slot.
Varying the OTHER variable — the field's type, `&'r dyn X` → `&'r i64` — says
the cost is not the slot's. Same rule as the harness's own: an instrument's
reading is a hypothesis until the second variable is moved.

## 1. WHAT LANDED, AND THE TWO MECHANISMS THIS ROUND HAD TO BUILD

    THE REGION SLOT (2026-08-31m)      `&'a [T]`/`&'a str`/`&'a dyn`/`&'a Dst`
                                       record their region at resolve_type
    THE ELISION ENGINE (31i/j/k/l)     mint · substitute · the four repairs ·
                                       the meet at the STRUCT LITERAL
    ── built here, because the engine as priced refuses legal programs ──
    THE MEET AT THE CALL               a callee binder offered TWO caller
                                       regions is instantiated at their meet
                                       when every occurrence of it in the
                                       callee's signature is COVARIANT
    A MINTED REGION IS AN INFERENCE     two minted names are equal where
    VARIABLE, SCOPED                    `permissive_empty` already holds
    THE IMPL HEADER'S OWN BINDERS       Self's lifetime args come from the
                                       IMPL, not from the STRUCT's spelling

`arm_inst()`, `arm_regslot()`, `arm_callmeet()`, `arm_mintiv()` and
`probe_meet_()` now `return true`. ⚠ `probe_meet_retired_()` beside the last of
them is UNCALLED ON PURPOSE and is not dead code by accident: it is the written
record of exactly which arm names the landed meet used to answer for, kept where
the predicate lives rather than only in this file, and it is named so that
nothing mistakes it for a live gate. The arms they answered for — ltmintinst,
ltmintmeet, ltregslot, ltregmeet, ltcallmeet, ltregall — are RETIRED. The arms
that still WIDEN something are kept as probes and are this round's controls:
`ltmeetany`, `ltmintmeetrg`, `ltmintmeetamb`, **`ltcallmeetany`**,
**`ltregallany`**. The control revert is `git revert` of the landing commit.

## 2. ⚠ THE ENGINE AS PRICED REFUSED ORDINARY RUST — AND NO POPULATION SAW IT

    fn pick<'a>(x:&'a i64, y:&'a i64) -> &'a i64 { … }
    fn use2<'p,'q>(p:&'p i64, q:&'q i64) -> i64 { let r = pick(p,q); return *r; }

`ltmintinst`, `ltmintmeetamb`, `ltregmeet` — every arm ever priced in this arc —
REFUSE this, "call to 'pick' arg 2: variance mismatch — expected &'p i64, got
&'q i64". Unarmed rc 0. It is bread-and-butter Rust: 'a is instantiated at the
intersection. PROVEN LIVE AT THE SITE before a line was written, by the census
that was already there:

    meet.call.binder 2 · meet.call.multi 2 · meet.call.multi.novariance 2

**`binder_variance_` is asked with an EMPTY KEY at a call**, because
`compute_variances` answers per TYPE DEFINITION and a fn's own binder has no
entry anywhere — 2026-08-31k named that as a coverage gap ("no variance exists
for a fn's own binder"). Measured here, it is not a gap: it is a LEGAL-PROGRAM
REFUSAL, and the three populations price it 0 because the corpus contains no
such program.

`fn_binder_variance_` (src/compiler/sema.cpp, beside `compute_variances`)
composes `variance_in_type` over the parameter types AND the return type at
ambient Co. The return type is part of the answer, not an optimisation: `->
Inv<'a>` pins 'a at the type the caller already named. Both consumers of the map
(`inst_call_params_`, `subst_call_ret_lts_`) are handed the same return type, or
the arguments and the result would be instantiated differently.

**THE GUARD'S ABUSE DIRECTION, AND WHAT MEASURED IT.** `ltcallmeetany` is the
same site with the guard removed. Over all three populations it is
INDISTINGUISHABLE from the guarded arm — ceiling 23, cost 3, the same rows, a
byte-identical 1056-row fail table. One hand program separates them:

    struct FnCtxt<'a,'tcx>{ a:&'a i64, t:&'a mut &'tcx i64 }   'tcx under a
    fn use_it<'a,'tcx>(f:&FnCtxt<'a,'tcx>, x:&'tcx i64) {}     `&mut` POINTEE
    fn bad<'a,'tcx>(fcx:&FnCtxt<'a,'tcx>){ use_it(fcx, use_fcx(fcx)); }

    guarded (landed)  REFUSED   `var.…FnCtxt.@1=Inv`, meet.call.fnvar.inv 2
    ltcallmeetany     COMPILED

It is in the tree as `tests/logos/fail/bc_ltcallmeet_invariant_binder_no_meet`
with its admit half beside it. ⚠ AND `&'tcx mut i64` IS NOT THE INVARIANT
SPELLING — the REGION of a `&mut` is itself covariant, only its POINTEE is
invariant. That was measured the wrong way round first: the "invariant twin"
compiled under both arms and read as a refutation of the guard until the
variance table was asked directly.

## 3. A MINTED REGION IS AN INFERENCE VARIABLE — AND THE SCOPE IS THE MECHANISM

2026-08-31l's own cost sensor, `bc_ltmintgen_two_minted_regions_one_typaram`,
was still a cost. `swap_ref<T>(&mut t0, &mut t1)` binds T from argument 1 —
minted regions and all — and refuses argument 2 for carrying a different minted
name: "expected &mut &mut i64, got &mut &mut i64", both sides spelled alike
because nobody wrote either. `lt_is_minted` has carried this fact for
borrow_check since the minting round ("a minted name reads as elided here"); the
comparators never asked.

    ltregall     both sides minted, ONLY where `permissive_empty` is true
                 (a CALL-SITE coercion — where the caller's region inference is
                 the thing that fills unresolved lifetimes in, and where the
                 unarmed comparator saw "" on both sides and said true)
    ltregallany  the same predicate with the scope removed — THE ABUSE DIRECTION

    THE SCOPE IS WORTH, MEASURED, ceiling 23 → 19 and one CORRECT refusal:
      cannot-assign-borrowed-ref-in-slice   assignment through a `&mut`, E0506
      ex3-both-anon-regions-2               assignment to `p.0`
      ex3-both-anon-regions-self-is-anon    return type
      issue-17728                           return type of a default method
      + bc_genrecv's `pick<T>(&self, other:&i64) -> &i64`, which rustc REJECTS

Every one of the five is a FN-BODY coercion; the swap is a CALL-SITE one. The
axis was already in the tree — `permissive_empty` is documented as "true at
call-site coercions where the caller's region inference fills in unresolved
lifetimes; false at fn-body coercions" — and had simply never been threaded into
`types_equal_with_lifetimes`.

## 4. PREDICTED vs MEASURED, AS SETS, BOTH WAYS (rule 6)

`predictions-regall.txt`, written before the first pricing run.

    CEILING(ltregall)  PREDICTED 24, "the same set as ltregmeet's, and the
      direction of the risk is named: both new mechanisms are PERMISSIVE and can
      only LOSE rows". MEASURED **23** on build 1f52c82aeaec1458.
        ltregmeet ∖ ltregall = { propagate-fail-to-approximate-longer-no-bounds }
        ltregall ∖ ltregmeet = ∅
      That row is `demand_y<'x,'y>(cell_x,cell_y,y)` called `demand_y(a,b,a)` —
      the CALLEE's binder, covariant everywhere, meet-instantiable. The port
      dropped upstream's closure `where` bound; what is left is a program rustc
      accepts. It stays LISTED and OPEN, named in the ledger.
    COST(pass)         PREDICTED 2 by name and PREDICTED the sensor would
      vanish. MEASURED exactly 2 — bc_genrecv_two_mut_sequential_admit and
      bc_ltunmentbind_renamed_binder_hole — and
      bc_ltmintgen_two_minted_regions_one_typaram COMPILES. Both survivors are
      FIXTURES THAT ASKED TO CHANGE, and both changed (§6).
    COST(fail)         PREDICTED 12 `.expected` losses, 0 rc changes, and "ANY
      rc 1→0 IS AN UN-REFUSAL AND REFUTES THE ARM". MEASURED 12 losses, 1
      text-only — and ONE rc 1→0, `issue-67007-escaping-data` (§5).
    STDLIB             PREDICTED green. MEASURED green, all four layers.
    ABUSE ARMS         PREDICTED "both cost something, and if either costs
      NOTHING its guard is not load-bearing and I must say so". MEASURED:
      `ltregallany` costs 4 ledger rows + 1 correct refusal — load-bearing.
      **`ltcallmeetany` COSTS NOTHING IN ANY POPULATION** — and its guard is
      still load-bearing, which only a hand program could show (§2). The
      prediction's own disjunct is what made that reportable instead of
      comfortable.
    THE TWIN BATTERY   PREDICTED and MEASURED 18/18, then 25/25 as it grew.

## 5. ⚠ THE UN-REFUSAL, AND WHY THE FIXTURE WAS THE THING THAT WAS WRONG

`tests/imported/fail/nll/issue-67007-escaping-data`, rc 1 → 0. Its port:

    struct FnCtxt<'a,'tcx>{ a:&'a i64, t:&'tcx i64 }      BOTH fields SHARED refs
    fn use_it<'a,'tcx>(f:&FnCtxt<'a,'tcx>, x:&'tcx i64) {}

Read DIRECTLY off the compiler's own variance table (`LOGOS_CENSUS`,
`var.<pkg.Def>.@i`), one field apart:

    t: &'tcx i64          FnCtxt.@1 = Co    ⇒ the meet applies, admitted
    t: &'a mut &'tcx i64  FnCtxt.@1 = Inv   ⇒ the meet is refused, rc 1

`use_it`'s 'tcx is UNIVERSALLY QUANTIFIED and occurs only covariantly, so it is
instantiable at the region both arguments outlive: rustc accepts the ported
program. Upstream's illegality is INVARIANCE (a `TyCtxt<'tcx>`), and the port
had dropped it. The fixture was shelved `fail` on 2026-08-31 by the
`current_lt_binders()` narrowing, whose stated reason — "two named generic
regions that appear in no `where` clause are UNRELATED, and both are binders of
the scope being checked" — is FALSE here: 'tcx at that call is a binder of
`use_it`, not of `bad_method`. **A rule that cannot tell a callee's binder from
the caller's shelved a legal program as illegal.**

RE-PINNED, not weakened: the port now carries upstream's invariance, the
`.expected` is UNCHANGED (the same diagnostic, byte for byte, refuses it), and
the covariant original is in `tests/logos/pass/bc_ltcallmeet_callee_binder_meet`
as its legal twin. ⚠ There is no rustc on this box; the argument is structural
and is carried by the compiler's own lattice plus a one-field control, and it is
labelled as such.

## 6. THE FIXTURES THAT ASKED TO CHANGE, AND THE ONE L1 CAUGHT

 1. `bc_ltunmentbind_renamed_binder_hole` — its own header: "WHEN THE
    SUBSTITUTION ENGINE LANDS THIS FILE MUST MOVE TO fail/." MOVED, with the
    original text kept verbatim above the new note, and its one-token pair
    `bc_ltunmentbind_two_unrelated_binders` updated to say the hole is closed.
 2. `bc_genrecv_two_mut_sequential_admit` — `fn pick<T>(&self, other:&i64) ->
    &i64` is E0621 upstream (with `&self`, the elided return region is SELF's).
    The fixture encoded a DIVERGENCE in a line whose subject is not elision at
    all. The parameter's region is NAMED (`pick<'o,T>`) and the exemption the
    file exists to test is unchanged and still RUN.
 3. **`tests/logos/fail/variance_arg_mut_inv` — AN OVER-REFUSAL, AND THE
    `-L bc -L fail` ORACLE NEVER SELECTED IT.** It pinned
    `takes<'a>(&mut &'a i32)` fed a `&mut &'static i32` and claimed Rust rejects
    it. Rust accepts: 'a is `takes`'s OWN binder, instantiated at the call. The
    invariance check is real and still bites when the region is FIXED BY THE
    CALLER, which is what the fixture now pins:
        fn wants_static(p:&mut &'static i32)->i32
        fn f<'a>(p:&mut &'a i32)->i32 { return wants_static(p); }   REFUSED
    Its admit half is `tests/logos/pass/variance_arg_mut_inv_free_binder_admit`.
    ⚠ **A FOURTH POPULATION HOLE, NAMED**: `fail_text_oracle.py` selects
    `-L bc -L fail`, and a NATIVE fail fixture without the `bc` label is outside
    it by construction. L1 is what caught this. The oracle's own header warns
    that a selection can exclude a shape; this is that, one label over.
 4. **AND ITS SPEC-TIER DUPLICATE, WHICH ONLY THE 4453-TEST L4 SWEEP SAW.**
    `tests/spec/fail/coerce_diag_1__variance-gate-on-compatible` is the same
    program under `// @rule coerce.variance.gate-on-compatible`, and neither L1,
    nor `L4 bc`, nor any of the three pricing populations selects it — `L4 bc`
    was 1384/1384 green with this fixture red one filter away. Re-pinned to the
    caller-fixed shape, which is what the rule's own text describes:
    "`permissive=true` (call-site arg-passing) forwards unresolved lifetime
    relations to the caller's region inference". ⇒ **ONE WRONG FIXTURE HAD TWO
    HOMES AND FOUR SELECTIONS MISSED BOTH.**

## 7. THE TWO ROWS GIVEN BACK ON PURPOSE — A ROW CLOSED BY A WRONG DIAGNOSTIC

The arm closed 23; the landed tree closes 21. `issue-103582-hint-type-alias` and
`issue-55394--b` were being refused with **"use of undeclared lifetime name
''a'"** — a binder neither program writes. Both use `'_` in an impl header, and
the Self-type restore built Self's lifetime args from `ssi->lifetime_params`,
the STRUCT DECLARATION's spelling. Three legal programs die on that message:

    impl<'x> Foo<'x>              → "undeclared lifetime name ''a'"
    impl Keep<'_>                 → "…''s'"
    impl Greeter0 for FixedGreeter<'_>  → "…''a'"

and `impl<'a> Foo<'a>` compiles only because the two spellings COLLIDE. The
restore now takes the IMPL HEADER's own binders positionally; a slot the impl did
not declare is elided and the mint gives it a fn-scope region.
`tests/logos/pass/bc_ltimplhdr_self_lifetime_args_from_the_header` holds all
three. ⚠ Positional is a CHOICE — `impl<'a,'b> Pair<'b,'a>` maps by order of
declaration, not by position in the target. Not worse than the spelling it
replaces, and named rather than assumed harmless.

**A ROW CLOSED BY A DIAGNOSTIC THAT NAMES A BINDER THE PROGRAM NEVER WROTE IS
NOT CLOSED.** Both go back to the ledger.

## 8. THE CLOSED SET, 21 ROWS, EACH REFUSED FOR A LIFETIME REASON

Every one moved `tests/imported/admit/` → `tests/imported/fail/` with its
diagnostic pinned in full, and its ledger row DELETED (not commented out).

    borrowck  cannot-assign-borrowed-ref-in-slice          E0506
    lifetimes apit-not-targeted-by-lifetime-suggestion--apit
              ex1-return-one-existing-name-early-bound-in-struct
              ex1-return-one-existing-name-if-else-using-impl-3
              ex1-return-one-existing-name-return-type-is-anon
              ex1-return-one-existing-name-self-is-anon--c14b-elided-plain-param
              ex1-return-one-existing-name-self-is-anon--self-is-anon
              ex2a-push-one-existing-name
              ex3-both-anon-regions-2
              ex3-both-anon-regions-one-is-struct{,-3,-4}
              ex3-both-anon-regions-self-is-anon
              issue-17728
    nll       issue-52113 · normalization-self
    regions   region-invariant-static-error-reporting
              regions-glb-free-free--f-control-whole
              regions-infer-at-fn-not-param
              regions-infer-paramd-indirect
              regions-trait-object-subtyping

## 9. THE `.expected` RE-PINS (rule 5 / rule 14's stated exemption)

TWELVE rows, rc **1 before and 1 after**, zero un-refusals. The refusal moves to
the type checker's return-type variance comparison, which is the site where the
engine and the slot make the callee's binder and the caller's comparable at all.
Each `.logos` header keeps the OLD text verbatim so the move is readable, and
each names its legal twin. THE TWINS ARE IN THE TREE, not in a scratch
directory: `tests/logos/pass/bc_ltcallmeet_expected_loss_twins` carries all
twelve, each program with its illegality removed and nothing else changed, and
it RUNS (`exit: 0`) rather than merely compiling.

    42701-one-named-and-one-anonymous · bc_ltunmentbind_two_unrelated_binders
    ex1-return-one-existing-name-if-else{,-2,-3,-using-impl,-using-impl-2}
    ex3-both-anon-regions-return-type-is-anon · issue-55394--ctl2
    nll-anon-to-static · projection-no-regions-closure--{c30-direct-call,…}
    + the native tests/logos/fail/lifetime_mismatch

**RESTORED, and it is the round's own evidence**: `borrowck-swap-mut-base-ptr`
was one of 2026-08-31m's thirteen losses and is NOT one now. Its E0502 borrow
error had been MASKED by the spurious `swap_ref` variance refusal; the
minted-region repair removes the mask and upstream's primary error comes back.

## 9b. ⚠ THE 16th KIND OF GATE LIE, AGAIN — AND IT IS NOT A NEW KIND, IT IS A
##      SECOND INSTANCE OF ONE ALREADY WRITTEN DOWN IN THIS FILE

Re-running the full 4453-test L4 after re-pinning the spec fixture returned, in
0 seconds:

    gate-run: all 4453 tests in this filter are ALREADY MEASURED under this build
    build 266: 5839 recorded, 1 failed
      failed  logos_25_spec_fail_coerce_diag_1__variance-gate-on-compatible
    RC=0

**RC 0 WITH A FAILING ROW IN THE ANSWER.** The store's key is the COMPILER BUILD
(`logosc --version` + a libs hash), and a FIXTURE edit moves neither. So a tree
whose corpus changed reads as "nothing has changed that a test run could see",
and the verdict it hands back is the one from before the edit. `FORCE=1`
re-measures, and that is what this round's final L4 number is from. The message
itself says to say why the record was not enough; this is why: **the record is
per (build, test) and a test is not only its NAME.**

⚠ THIS FILE ALREADY CARRIES THE KIND — see "A GATE LIE, 16th KIND: THE STORE
ANSWERED FOR A TEST SOURCE IT NEVER READ" earlier, where the same mechanism
served three stale FAILING verdicts and then, with the sign flipped, an RC=0 over
three fixed pins. Knowing the kind did not stop it: the reflex "the source did
not change, so the gate is still valid" is exactly the reflex that is wrong, and
it took a printed FAILED row inside an RC=0 answer to notice, for the second
time. Every gate run in this round's final block is `FORCE=1`.

## 10. OPEN

 1. **`propagate-fail-to-approximate-longer-no-bounds`** — listed, not closed;
    the port is a program rustc accepts (§4).
 2. **`issue-103582-hint-type-alias` / `issue-55394--b`** — back in the ledger,
    and now waiting on something other than a wrong diagnostic.
 3. **THE IMPL HEADER'S BINDERS ARE MAPPED POSITIONALLY** (§7).
 4. **THE MINTED-IV PREDICATE IS ASKED AT TWO EQUALITY SITES AND NOT AT A
    THIRD**: `types_equal_with_lifetimes`' Struct/Enum `lifetime_args` loop
    compares those strings directly and never calls `lt_eq`, so a minted region
    reaching a struct's lifetime ARGUMENT is out of scope. Named, unmeasured.
 5. **`subst_type_sema`'s DstRef arm still returns early on
    `type_args().empty()`** — carried from 2026-08-31m §11.2, unmeasured.
 6. **MINTING INTO AN ELIDED SLICE SLOT** — `fn id(x:&[i64]) -> &[i64]` is still
    related by elision alone (31m §11.3).
 7. **THE VALUE SIDE STILL RECORDS NO REGION** for Slice/TraitObject/DstRef
    constructors outside `resolve_type`/`subst_type_sema` — and §0 says the one
    program that was blamed on it was never its fault. Its real cost is now
    UNMEASURED, not zero.
 8. **`fail_text_oracle.py` cannot see a native fail fixture without the `bc`
    label** (§6.3). Four populations, four holes found in four rounds.
 9. **`current_lt_binders()` IS NOT GONE, AND THIS ROUND DID NOT MEASURE
    WHETHER IT STILL EARNS ITS KEEP.** It is the 2026-08-31h NARROWING of
    `outlives()`'s permissive tail — two named regions that are both binders of
    the scope being checked do not outlive each other — and its stated price was
    that the WIDER rule (`lifereg_unmentioned`, still a probe directly above it)
    closes two more rows and REFUSES FIVE LEGAL PROGRAMS, "every one of them
    comparing a name from ONE binder against a name from ANOTHER: a callee's or
    a struct's own lifetime parameter that reached here UNSUBSTITUTED". That
    sentence names exactly what landed this round. **THE EXPERIMENT IS ONE
    EXISTING PROBE AND ONE BUILD**: price `lifereg_unmentioned` on the landed
    binary; if its five legal refusals are gone the narrowing is dead weight and
    the wider rule is free, and `current_lt_binders()` goes with it. Not run
    here — the box was committed to this round's gates, and a number I did not
    take is not a number I get to report.
10. The `expected Option, got Option` wording is still prerequisite work.

Files: `include/logos/compiler/probe.hpp` (five predicates landed, two abuse
arms kept), `include/logos/compiler/subtype.hpp` (`permissive_minted` threaded
through `types_equal_with_lifetimes`; the minted-iv predicate at both equality
sites), `src/compiler/sema.cpp` (`fn_binder_variance_`),
`src/compiler/sema_impl.hpp` (the meet at the call; `ret` into
`build_call_lt_subst_` / `inst_call_params_`), `src/compiler/sema_collect.cpp`
(`self_lt_args_`), `src/compiler/sema_expr.cpp` (the two `inst_call_params_`
call sites pass the return type). +212/−37 over six files.

# ═══ ROUND 2026-08-31o — THE SHELF RE-PRICED, AND bck.A IS ONE MECHANISM ════

PRICING ROUND — nothing was fixed, no ledger row was deleted, **# TOTAL stays
276**. ⚠ WHICH BUILD EACH NUMBER RODE (rule 8, and the stdlib archives carry a
timestamp so a relink moves the hash):

    a744fc7ba8f7805e   the CENSUS build — the four-site census only
    b8dbdb689af27a8b   EVERY number in the tables below, gate-db builds 267-272
    05db938cfcda88fe   `mbparamvalnbc` alone (§4), reverted afterwards; the
                       rebuild from the restored source returns b8dbdb689af27a8b
                       EXACTLY, which is this round's control on its own binary

L1 rc=0 at every commit: 746/746, gates tier 325, the enumerator's 12 684 smoke
cases. Opening ledger baseline READ from the store, not re-run.

## 0. ⚠ THE CENSUS FIRST (RULE 17), AND IT KILLED ONE PROBE BEFORE IT WAS WRITTEN

`param_names_` at the four `not declared as mut` sites is ONE set answering TWO
questions. `param_byval_` splits it. Whole `tests/` tree, **8687 files walked,
8577 CONTRIBUTED at least one census row**, `LOGOS_CENSUS` riding in the gate's
own build:

    site                              arrive    hatch  hatch.ref  hatch.byval  refuse
    w   take_borrow_whole_         1 950 012 1 949 977  1 949 957      20        35
    f   take_field_borrow_path_           32       17         11       6        15
    aot AddrOfTemp in visit()             16        6          0       6        10
    ao  AddrOf in visit()                  2        2          2       0         0

Three readings, none of which a single number could give:

 1. **99.998% OF THE HATCH IS A REBORROW THROUGH A REFERENCE.** That is why
    `mbnoparam` — closing the hatch for every param — priced at the degenerate
    pole in 2026-08-29 and prices there again today (§3). The hatch is not an
    escape hatch; it is `&mut self` and `&mut` out-parameters.
 2. **THE SUBJECT IS 32 ARRIVALS IN THE WHOLE TREE.** A by-VALUE parameter
    reaching a mut-binding gate is rare, and a ceiling off it is small by
    construction. It is not a refutation (rule 4) — it is a small population.
 3. **SITE `ao` HAS NO BY-VALUE ARRIVAL AT ALL.** No probe was written there:
    it would have read NEVER FIRED, which is not a zero. Rule 17's sixth
    instance in this file, and the first where it saved the work rather than
    explaining it afterwards.

⚠ `param_byval_` deliberately EXCLUDES closure parameters. A fn's `mut x: T` is
DESUGARED in `sema_decl.cpp` — the LParam takes a synth name and the body sees a
prologue `let mut x = synth;` — so a fn param that is in `param_names_` under the
user's own name was NOT written `mut`. A closure's `|mut x: S|` gets no such
desugar, so classifying closure params by value would refuse a legal program.
They stay hatched, which is the permissive direction. The census's `hatch.byval`
is therefore a LOWER bound.

## 1. THE ROOT CAUSE, WITH A ONE-VARIABLE CONTROL

    let x: i64 = 1i64;  use_mut(&mut x);      REFUSED  "not declared as mut"
    fn q(f: i64) { use_mut(&mut f); }         rc 0     ← one binding kind apart

The mechanism EXISTS and lands on LOCALS; a by-VALUE PARAMETER is outside it.
That is the `bck.A` root label, and it is correct. `mut f` is in the grammar
(`param <- KW_MUT IDENT COLON type_ref => IS_MUT`) and compiles.

## 2. THE PROBE TABLE — build b8dbdb689af27a8b (READ), gate-db 267 → 272

    probe               fires  ceil  cost  cfail  std  verdict
    mbparamvalw            10     7     0      0   ok  ✓
    mbparamvalf           107     2     1      0   ok  ⚠ one legal program
    mbparamvalaot        1114     1     0      1   ok  ⚠ one text-only, the SAME shape
    mbparamvalall        1231    10     1      1   ok  ✓ EXACTLY the sum, and the same cost
    mbparamvalnbc     3450062    10     1      1   ok  ⛔ REFUTED — byte-identical to `all`
    mbnoparam          260148   276  1088    661   ⛔  ⛔ the degenerate pole, again
    ltbindersoff            3     0     0      1   ok  ALIVE, and it buys ONE fixture
    lifereg_unmentioned     4     1     0      0   ok  ✓ THE FIVE LEGAL REFUSALS ARE GONE
    capmovewalk            47     2     0      0   ok  ✓ FUNDABLE — cfail 4 → 0
    capshared             164     4     0      6   ok  ⛔ still 4 `.expected` LOST — but READ

## 3. PREDICTED vs MEASURED, AS SETS, BOTH WAYS (rule 6)

Predictions written before the build; the file is reproduced in §8.

**CEILING(mbparamvalall)** PREDICTED 5 by name. MEASURED **10**.

    predicted ∩ measured   issue-111554--b · issue-111554--c ·
                           issue-55492-borrowck-migrate-scans-parents ·
                           mutability-errors · borrowck-argument      (5/5)
    measured ∖ predicted   borrowck-borrow-overloaded-auto-deref ·
                           borrowck-ref-mut-of-imm--ref-mut-of-imm ·
                           lifetimes/ex3-both-anon-regions-using-fn-items ·
                           lifetimes/ex3-both-anon-regions-using-trait-objects ·
                           nll/issue-51191                            (5)
    predicted ∖ measured   ∅

**AND THE FIVE I DID NOT PREDICT ARE THE SURVEY'S RESULT.** Four of them are
NOT IN `bck.A` — they are filed under `lifereg.R2` (×2), `nllmoves.A` and
`bck.B`. Every one refuses with the E0596 message naming the right binding, and
every one's upstream error IS E0596:

    ex3-both-anon-regions-using-fn-items   lifereg.R2   fn foo(y: Vec<&i64>, z:&i64){ y.push(z); }
    ex3-both-anon-regions-using-trait-objects lifereg.R2  the same, one type apart
    issue-51191                            nllmoves.A   fn imm(me: Struct){ let r = &mut me; }
    borrowck-ref-mut-of-imm--ref-mut-of-imm bck.B       match x { Some(ref mut v) => … }, x by value
    borrowck-borrow-overloaded-auto-deref  bck.A        &mut p.y on a non-mut `p: Rc<Point>`

The four mis-rooted rows were filed by the block that IMPORTED them, and each
block named the root it could see. ⚠ `borrowck-borrow-overloaded-auto-deref` is
listed with a caveat: rustc's E0596 there is "cannot borrow data in an `Rc` as
mutable" (no `DerefMut`), and ours is "not declared as mut". Both refuse the
same program for a reason rustc gives, but not for THE reason. A row closed for
a reason upstream also holds is closed; a row closed by a message naming a thing
the program does not contain is not. This is the first kind, and it is labelled.

**THE PER-SITE CEILINGS ARE ADDITIVE HERE, AND THAT WAS NOT PREDICTED.** 7+2+1
= 10 and the union is 10; the three sites partition the ten rows exactly. Rule
13 says that is not guaranteed and it was checked, not assumed.

**COST** PREDICTED 0/0/ok on all four arms. MEASURED cost 1 and cfail 1, and
they are ONE DEFECT at TWO sites (§5).

**CEILING(ltbindersoff)** PREDICTED cfail ≥ 5 by name. MEASURED **1**. §6.

**capmovewalk** PREDICTED ceiling 2 by name and cfail 0. MEASURED exactly that.
**capshared** PREDICTED ceiling 4 by name and cfail 6. MEASURED exactly that.

## 4. THE NARROWING THAT WAS REFUTED BEFORE IT WAS FUNDED

The one cost (§5) is a by-value param that CARRIES a borrow, so the obvious
repair is to drop those from `param_byval_`. `mbparamvalnbc` is that edit, and
it is **BYTE-IDENTICAL to `mbparamvalall`** — same ten rows, the same one cost,
the same one text-only row. `bc_is_borrow_carrying_type` is a lookup in
`TypeSets::borrow_carrying`, a NAME SET filled at registration with
`residency_exempt` applied, and neither `H { r: &mut i64 }` nor `Vec<&i64>`
is in it. A predicate that answers an ESCAPE question cannot answer a
STRUCTURAL one. Its sibling `loan_carrying_type` — the same closure computed
without the residency skip — is the next thing to ask, and is UNMEASURED.
One build, one price, and the hypothesis died: that is what the harness is for.

## 5. THE COST, READ — AND IT IS ONE DEFECT WEARING TWO CLOTHES

    tests/logos/pass/bc_thru_ref_field_mut_reborrow_admit      cost, site f
        struct H { r: &mut i64 }
        fn bump(h: H) { let y: &mut i64 = &mut *h.r; *y = 5i64; }
        armed: "cannot borrow 'h.r' as mutable: 'h' not declared as mut"
    tests/logos/fail/bc_d1r13_p3_byvalue_outparam              cfail, site aot
        struct H { r: &mut Vec<B> }
        fn stashv(h: H, c: &C) { h.r.push(c.mk()); }
        armed: the same message, PREPENDED to the three the fixture pins

Both are **A REBORROW THROUGH A REFERENCE HELD IN A FIELD** of a by-value
parameter. Sites `f` and `aot` exempt a reborrow by asking the ROOT's type
(`root_is_mut_ref` / `root_is_ref`), and the root here is a struct. The
fixture's own header already says what the rule must key on: "THE RULE KEYS ON
THE DEREFERENCED REFERENCE AND NOT ON THE ROOT … the only thing that differs is
the type of the reference actually dereferenced". ⇒ **THE EXEMPTION IS SPELLED
AT THE WRONG PLACE, and the by-value hatch is currently what hides it.**

⚠ AND THE SECOND ONE IS INVISIBLE TO ctest. `bc_d1r13_p3_byvalue_outparam`'s
`.expected` is a grep -F SUBSTRING and the extra error is PREPENDED, so the
fixture stays GREEN with a legal-program refusal inside it. Rule 15, and only
`fail_text_oracle.py` sees it.

⚠ SITE `w` COSTS NOTHING (cost 0, cfail 0) — the whole-variable arm never meets
this shape. So `mbparamvalw` alone is 7 rows at zero measured cost, and rule 13
says that increment is real: the cost belongs to `f` and `aot`, not to the
mechanism.

## 6. THE VERDICT ON `current_lt_binders()` — ALIVE, AND WORTH DELETING ANYWAY

Round 2026-08-31n named the experiment and did not run it. Both halves ran here.

    ltbindersoff          the CONTROL REVERT of that predicate ALONE: skip its
                          refusal, change nothing else.
      fires 3 · ceiling 0 (it is PERMISSIVE, it can close no row) · cost 0
      **cfail 1**: `account-for-lifetimes-in-closure-suggestion` rc 1 → 0.
    ⇒ IT IS NOT DEAD. It still refuses something the engine does not — but the
      five fixtures it landed for in 2026-08-31h are now FOUR-FIFTHS the
      engine's: issue-55394--ctl2, issue-67007-escaping-data and both
      projection-no-regions-closure ports stay rc 1 without it. The
      substitution, the mint and the callee-binder meet answer for them now.

    lifereg_unmentioned   the WIDER rule the narrowing replaced.
      fires 4 · **ceiling 1** (suggest-introducing-and-adding-missing-lifetime--
      param-may-not-live) · **cost 0** · **cfail 0** · stdlib all four layers.
    ⇒ **ITS FIVE LEGAL REFUSALS ARE GONE.** In 2026-08-31h the wider rule bought
      two more rows and refused FIVE legal programs, "every one of them
      comparing a name from ONE binder against a name from ANOTHER: a callee's
      or a struct's own lifetime parameter that reached here UNSUBSTITUTED".
      That sentence describes exactly what round n landed, and the corpus now
      prices the wider rule at zero.

⇒ **THE RECOMMENDATION IS TO DELETE `current_lt_binders()` AND LET THE WIDER
RULE STAND.** The wider rule is a SUPERSET of the narrower one, so the one
fixture `ltbindersoff` loses is kept, one more ledger row comes with it, and the
deliberately-unsound name-collision approximation — with its two pinned
fixtures u7/u8 saying so — leaves the tree. Rule 14's other direction: two names
for one question, and the narrow one is now the one that buys less.

⚠ RULE 5. COST 0 IS NOT A SAFETY CLAIM, so the counter-examples were written and
run under `lifereg_unmentioned` on this binary, each multi-line:

    u1  struct Holder<'h>; fn take<'c>(h: Holder<'c>); fn give<'g>(x:&'g i64)
        — the CALLEE's binder against the CALLER's                    rc 0
    u2  struct Pair<'a>; fn mk<'m>(…) -> Pair<'m>; fn use_it<'u>(…)
        — a STRUCT's own parameter, unsubstituted                     rc 0
    u3  impl<'s> H<'s> { fn get<'m>(self:&'m H<'s>) -> &'m i64 }
        — a METHOD's binder against the impl's                        rc 0
    tests/logos/pass/bc_ltunmentbind_legal_shapes    armed rc 0   ← IN-TREE, and
        it is round h's OWN pin for these shapes: it was the wider rule's cost
        then and it is not now. A pinned artifact, not only a hand program.
    tests/logos/fail/bc_ltunmentbind_two_unrelated_binders  armed rc 1 (the
        illegal twin still refuses)

## 7. THE SHELF, RE-PRICED — AND TWO OF ITS FOUR ENTRIES DO NOT EXIST

    mechanism    shelved as              MEASURED HERE (b8dbdb689af27a8b)
    capmovewalk  4 rows / 4 cfail        ceiling 2 · cost 0 · **cfail 0** · ok
    capshared    4 rows / 6 cfail        ceiling 4 · cost 0 · cfail 6 · ok
    capretplt    5 rows / 4 cfail        NOT IN THE TREE
    capretcaps   2 rows / 4 cfail        NOT IN THE TREE

**`capmovewalk` IS NOW FUNDABLE AND HAS BEEN FOR THREE ROUNDS.** Its cfail 4
was never its own: it was inherited from the cause-B deposit into
`outliving_params_`, and 2026-08-31h moved that deposit into
`closure_capture_names_` when `capretsc` landed. Round h wrote "its cfail 4
should now be 0. First thing to price next round" and three lifetime rounds went
by. MEASURED: cfail 0, cost 0, stdlib ok, ceiling 2 — `borrowck-multiple-captures`
and `nll/issue-48238` — the exact two round h named as its clean increment.
A `move` closure's body is never walked (`probe_mv_` returns before the walk),
so those two are ZERO ARRIVALS, not permissive verdicts (rule 16).

**`capretplt` AND `capretcaps` ARE NOT ON A SHELF; THEY WERE RETIRED AS
SUBSUMED ON 2026-08-31h**, and the repair this round was told had "never been
attempted" — *scope the deposit to the return check* — **IS `capretsc`, and it
LANDED that round for 4 rows.** The scoping is in the tree today:
`closure_capture_names_` is read at exactly one site, the "cannot return
reference to local variable" report gate in `check_return_value`, and the four
fail fixtures the wider deposit un-refused (escape-argument--b,
escape-upvar-nested, escape-upvar-ref, regions-nested-fns) are rc 1. Re-installing
either arm would measure the leak, not a mechanism. The only thing the wider
deposit still buys over the landed tree is `regions-nested-fns-2`, and it buys it
with four un-refusals. **A HANDED-DOWN SHELF IS A HYPOTHESIS, NOT DATA** — rule
17 applied to a recommendation instead of to a site.

**`capshared`'s FOUR `.expected` LOSSES, READ — the first time in four rounds.**
All four are ONE defect and it is nameable:

    arc-consumed-in-looped-closure                   .expected: "use of moved
    borrowck-in-static--move-captured-out-of-fn-closure   value 'x' (moved on
    borrowck-in-static--r-runtime                        line N)"
    closure-move-spans
      unarmed   error: use of moved value 'x' (moved on line 10)
      capshared error: cannot borrow moved value 'x'          ← the LINE IS GONE

Routing the shared whole-var capture through `record_borrow` reaches
`take_borrow_whole_`'s `if (it->moved)` arm, which reports "cannot borrow moved
value" and RETURNS — ahead of the `use of moved value … (moved on line N)`
reader whose text these four pin. The verdict is unchanged (rc 1 both columns);
what is lost is the MOVE LINE, which is the informative half. And
`closure-move-spans` shows the second half of it: unarmed it prints BOTH
messages, armed it prints the borrow one TWICE — a DUPLICATE.

⇒ Rule 14 says a branch that only re-words an already-red diagnostic buys
nothing UNLESS it deletes a duplicate or delivers upstream's PRIMARY error
first. This does the OPPOSITE on both counts. **THE UNBLOCKING IS NAMED AND IT
IS NOT capshared's**: `take_borrow_whole_`'s moved-value arm should DELEGATE to
the moved-value reader that owns the wording and the line — the same repair by
delegation the arm eleven lines below it already uses for `report_partial_move`.
With that landed, capshared's cfail is 2 text-only and its four rows are
buyable. Until then capshared stays DECLINED, but it is no longer declined for
an unread reason.

## 8. THE SURVEY — `bck.A` + `bck.D`, 20 ROWS, COMPILED BY HAND

### `bck.A` — 10 rows, and SEVEN of them are ONE mechanism

    A-1  A BY-VALUE PARAMETER NOT DECLARED `mut` MAY NOT BE MUTABLY BORROWED
         borrowck-argument · issue-111554--b · issue-111554--c ·
         issue-55492-borrowck-migrate-scans-parents · mutability-errors ·
         borrowck-borrow-overloaded-auto-deref (with §3's caveat) ·
         borrowck-unboxed-closures
         MEASURED: six of the seven close under `mbparamvalall`. The seventh,
         **borrowck-unboxed-closures**, does NOT — `fn b<F: FnMut(i64)->i64>(f: F)
         { f(1i64) }` takes no `&mut` at all, because calling an `FnMut` through
         a binding is not modelled as a borrow of that binding. Same PARTITION,
         different missing observation, and it needs the call site, not the gate.
         ⊕ FOUR MORE ROWS JOIN FROM OTHER ROOTS (§3): lifereg.R2 ×2, nllmoves.A,
         bck.B. **The partition is 11 rows, not 7, and 10 are priced.**
    A-2  `&mut b` WHERE `b` IS A REFERENCE-TYPED BINDING — mut-borrow-of-mut-ref.
         `fn f(b:&mut i64) -> i32 { return h2(&mut b); }` is E0596 upstream, and
         `b` is hatched HERE BY DESIGN: 1 949 957 of the hatch's arrivals are
         reborrows (`&mut *b`) and the tree cannot tell `&mut b` from `&mut *b`
         once auto-reborrow has run. NEW NAME, and it is the same question §5
         asks from the other side: WHICH REFERENCE IS BEING DEREFERENCED.
    A-3  A CLOSURE ESCAPING IN `Box<dyn Fn>` CAPTURES A `&` PARAMETER —
         suggest-lt-on-ty-alias-w-generics. `Box<dyn Trait>` implies a `'static`
         bound; nothing in the tree defaults a trait object's region. NEW NAME,
         MACHINERY DOES NOT EXIST.
    A-4  A WRITE THROUGH A `&mut` OUT-PARAMETER ESCAPES TO THE CALLER —
         borrowck-local-borrow-with-panic-outlives-fn: `fn cplusplus_mode(x: &mut
         &mut i64) { let mut z=(0,0); *x = &mut z.1; }`. No closure, so it is NOT
         `escape-argument--t09`'s partition; it is its fn-body twin. NEW NAME.

### `bck.D` — 10 rows, and the root label is right about 3 and backwards about 7

The label says "a TEMPORARY has no owner to key a loan on". SEVEN rows are about
the loan's **HOLDER**; THREE are about its **REFERENT**.

    D-H  A LOAN WHOSE HOLDER IS A COMPILER-MADE TEMPORARY IS RELEASED AT ONCE
         two-phase-nonrecv-autoref--{a-fnmut-twice, c-mut-and-shared-args,
         d-index-two-phase} · mutate-vec-while-iterating--{a,b} ·
         suggest-local-var-for-vector · suggest-storing-local-var-for-vector
         **PROVEN LIVE BY TWO CONTROLS THAT REFUSE THE SAME CONFLICT** — the
         machinery exists, only the holder is missing:
           double_access(&mut a, &a)                    rc 0   (the ledger row)
           let m = &mut a; let s = &a; double_access(m,s)  rc 1
               "cannot borrow 'a' as shared: already mutably borrowed"
           for v in &values { values.push(4i64); }      rc 0   (the ledger row)
           let it = &values; values.push(4i64); touch(it) rc 1
               "cannot borrow 'values' as mutable: 'values' has shared borrows"
         The three surfaces are one question — an ARGUMENT-position autoref, the
         `for`-desugar's iterator temp, and an INDEX autoref are all loans with
         no `let` to hold them. ⚠ AND `v[v.len()-1] = 123` vs the hoisted
         `let n = v.len(); v[n] = 123` (rc 0, legal) is the pair that says the
         rule must not simply refuse indexes. ONE NEW NAME for seven rows.
    D-R  A BORROW OF A CALL TEMPORARY OUTLIVES THE TEMPORARY (E0716)
         borrowck-borrowed-uniq-rvalue (`&mk().v`) ·
         borrowck-borrowed-uniq-rvalue-2 (`&mk()`) · issue-36082 (`&mk().a`)
         The control is `let b = mk(); let r = &b.v;` — rc 0, correctly. ⚠ **NO
         ORACLE ON THIS BOX.** Rust's temporary-lifetime EXTENSION rules make
         `let x = &temp();` legal in some spellings, and whether it extends
         through a field of a CALL result is exactly what these three turn on.
         rustc is not installed here. The three stay listed, and the honest
         statement is that their illegality is INHERITED FROM THE IMPORT, not
         verified. Adjudicate upstream before spending a round.

## 9. THE HAND PROGRAMS (rules 5, 7, 10), EACH MULTI-LINE, EACH ON THIS BINARY

    unarmed / mbparamvalall
    a2  let x: i64 = 1i64; use_mut(&mut x);                    1 / 1  mechanism exists
    a3  fn q(f: i64) { use_mut(&mut f); }                      0 / 1  THE DEFECT
    b5  fn func(arg: S) -> i64 { arg.mutate(); … }             0 / 1  THE DEFECT
    a4  fn q(mut f: i64) { use_mut(&mut f); }                  0 / 0  LEGAL
    a6  let c = |mut s: S| -> i64 { s.bump(); … };             0 / 0  LEGAL (why
                                                       closure params stay hatched)
    a7  fn imm_local(mut x:(i64,)) { use_mut(&mut x.0); }      0 / 0  LEGAL, site f
    b1  fn f(p:&mut S) { take(p); }                            0 / 0  LEGAL reborrow
    b2  fn f(p:&mut i64) { take(&mut *p); }                    0 / 0  LEGAL reborrow
    b3  fn twice(self:&mut S) { self.bump(); self.bump(); }    0 / 0  LEGAL
    b4  b5 WITH `mut` — one token apart                        0 / 0  LEGAL

Ten programs, six of them legal, none refused. b4/b5 are the pair: one token
apart, one refused and one not, which is the claim in its smallest form.

## ⇒ 10. WHAT DESERVES FUNDING, IN ORDER

 1. **DELETE `current_lt_binders()`, KEEP THE WIDER RULE** (§6). One row, cost
    0/0/ok on three populations plus four hand programs and an in-tree pin that
    was the wider rule's OWN cost one round ago. It also removes an
    admittedly-unsound name-collision approximation and its two hole fixtures.
    Smallest change, largest cleanup.
 2. **`capmovewalk`** (§7). Two rows, cost 0, cfail 0, stdlib ok, a one-variable
    probe already in the tree, and round h's prediction confirmed exactly.
 3. **THE BY-VALUE PARAMETER GATE AT SITE `w` ALONE** (§2, §5). Seven rows at
    cost 0 and cfail 0. Sites `f` and `aot` add three more rows and both of
    their costs are the SAME defect — a reborrow through a reference held in a
    FIELD — so they land after (4).
 4. **KEY THE REBORROW EXEMPTION ON THE DEREFERENCED REFERENCE, NOT THE ROOT**
    (§5). It is a defect in its own right today, hidden by the hatch; it
    unblocks three rows in (3) and it is `bck.A-2`'s mechanism too.
 5. **DELEGATE `take_borrow_whole_`'s MOVED-VALUE ARM** to the reader that owns
    "use of moved value … (moved on line N)" (§7). Unblocks capshared's four.
 6. **bck.D-H, THE LOAN WITH NO HOLDER** (§8). Seven rows on one mechanism,
    proven live by two controls that already refuse the identical conflict. The
    biggest single prize surveyed this round, and the most work.

## 11. OPEN, WITH ITS EVIDENCE

 1. **`loan_carrying_type` vs `is_borrow_carrying_type`** at the by-value set
    (§4) — the sibling predicate, UNMEASURED. Two notions of one concept.
 2. **`borrowck-unboxed-closures`** — calling an `FnMut` through a binding takes
    no borrow. Same partition, different site (§8 A-1).
 3. **THE THREE E0716 ROWS HAVE NO ORACLE HERE** (§8 D-R), like issue-51268 and
    issue-42574--b before them.
 4. **`borrowck-borrow-overloaded-auto-deref` closes for a reason upstream also
    holds, but not for THE reason** (§3). Listed, labelled, not counted as clean.
 5. **`bc_d1r13_p3_byvalue_outparam` STAYS GREEN under a legal-program refusal**
    because `.expected` is a substring grep (§5). Rule 15, second instance.
 6. The census's `hatch.byval` is a LOWER bound: closure params are out of
    `param_byval_` on purpose (§0).

# ═══ ROUND 2026-08-31p — FOUR ARMS LANDED, 12 ROWS, AND THE SHELF'S OWN TOP
# RECOMMENDATION DIED ON READING ITS DIAGNOSTIC ═══════════════════════════════

**LEDGER 276 → 264. TWELVE ROWS.** ⚠ WHICH BUILD EACH NUMBER RODE (rule 8, and
the stdlib archives carry a TIMESTAMP so a relink moves the hash with no source
changed — three of the six below are that):

    b8dbdb689af27a8b   the OPENING tree (45555cee1). The 2026-08-31o shelf was
                       measured on it and this round opens on the same binary,
                       so for once nothing on the shelf was stale.
    3397a2dc7ba8d280   M2 + M3 landed (`capmovewalk`, the by-value gate at `w`)
    e044b32a1b411792   THE CONTROL REVERT of M4a, git-stash + rebuild — same
                       source as 3397…, different hash, identical behaviour
    6f4db489f4eb66e5   M4a, FIRST SPELLING — REFUTED, see §3
    e6b440c004a4c6b0   M4a narrowed by `through_ref_is_place`
    a260551f8f0c04eb   M4b (the by-value gate at `f` and `aot`)
    df6585c06bbd0d8b   the final relink; no source changed

## 0. THE ARMS, AND WHAT EACH ONE COST

    arm                        ceiling  cost  cfail  stdlib  ledger
    M2 capmovewalk                   2     0      0      ok  276 -> 274
    M3 by-value gate at `w`          7     0      0      ok  274 -> 267
    M4a exemption re-keyed           0     0   1 TEXT     ok  267 (permissive)
    M4b by-value gate at `f`+`aot`   3     0      0      ok  267 -> 264
    M1 delete current_lt_binders()   1     0      0      ok  ⛔ WITHDRAWN, §1

Every `cost` is the two legal populations `-L bc -L pass` (902) and
`25_spec|03_ownership|04_advanced` pass (190); every `cfail` is
`scripts/fail_text_oracle.py` over 1090 fail fixtures, rc + normalised stderr
sha + `.expected`-match, counted in the three shapes separately.

## 1. ⛔ M1 IS WITHDRAWN, AND THE REASON IS THE DIAGNOSTIC

2026-08-31o's first recommendation was "delete `current_lt_binders()`, keep the
wider rule — one row, cost 0/0/ok on three populations". Both halves of that
re-price were correct. **The row is not.**

`lifereg_unmentioned`'s ceiling of 1 is
`lifetimes/suggest-introducing-and-adding-missing-lifetime--param-may-not-live`:

    fn with_restriction<'b, T: 'b>(x: &'b i64) -> &'b i64 { return x; }
    fn no_restriction<T>(x: &i64) -> &i64 { return with_restriction::<T>(x); }

Upstream is **E0310, "the parameter type `T` may not live long enough"** — the
missing `T: 'b` bound. The wider rule's message is

    call to 'with_restriction' arg 1: variance mismatch — expected &'b i64,
    got &i64 — lifetime structure incompatible

i.e. it refuses the REGION relation between the caller's elided argument region
and the callee's `'b`. rustc's inference satisfies that relation; the program's
only fault is the type-outlives bound, which this rule does not mention and does
not compute. ⇒ **A ROW CLOSED BY A WRONG DIAGNOSTIC IS NOT CLOSED**, so the
wider rule's legitimate ceiling is **0** and its refusal is over-strict on a
satisfiable constraint. Deleting the narrowing would buy nothing and pay for it.

⚠ `current_lt_binders()` is therefore KEPT, and it is NOT DEAD: `ltbindersoff`,
the control revert of that predicate alone, still costs cfail 1
(`account-for-lifetimes-in-closure-suggestion` rc 1 → 0) on the opening binary.
It stays an admittedly-unsound name-collision approximation buying one fixture;
what retires it is the E0310 machinery, not this rule.

⚠ AND THE TAIL IT GUARDS IS NEARLY UNREACHABLE. Four multi-line hand programs
written for this arm — the callee's binder against the caller's, a struct's own
parameter unsubstituted, a method's binder against its impl's, two unrelated
caller binders — plus the in-tree pins u1/u2/u5/u7/u8 all fired
`lifereg_unmentioned` **ZERO** times on the opening binary. The engine's mint
and substitution answer before the permissive tail is reached. A cost of 0 over
programs that never arrive is rule 1, and it is why the ceiling had to be read.

## 2. M2 AND M3 — THE TWO CLEAN ONES

**M2 `capmovewalk`.** `walk_closure_body` RETURNED on `cbv.is_move()`. The
`is_move_` bit stays: it still scopes the cause-B capture deposit, which is
genuinely inverted for a move closure. CEILING 2, PREDICTED BY NAME, closed
set = predicted set, both directions empty:

    borrowck/borrowck-multiple-captures  "cannot move 'x1' while it is
                                          borrowed"        upstream E0505
    nll/issue-48238                      "cannot return reference to local
                                          variable 'orig'" upstream
                                          closure-borrow-escape

**M3 the by-value parameter gate at `take_borrow_whole_`.** CEILING 7,
PREDICTED BY NAME off 2026-08-31o's store deltas, closed set = predicted set,
both directions empty; all seven upstream errors are E0596 and all seven of our
messages are E0596-shaped and name the right binding:

    borrowck/borrowck-borrow-overloaded-auto-deref   bck.A     ⚠ labelled below
    borrowck/issue-111554--b                         bck.A
    borrowck/issue-111554--c                         bck.A
    borrowck/issue-55492-borrowck-migrate-scans-parents bck.A
    lifetimes/ex3-both-anon-regions-using-fn-items   lifereg.R2  ← mis-rooted
    lifetimes/ex3-both-anon-regions-using-trait-objects lifereg.R2 ← mis-rooted
    nll/issue-51191                                  nllmoves.A  ← mis-rooted

⚠ `borrowck-borrow-overloaded-auto-deref` is the LABELLED kind, carried forward
from 2026-08-31o §3: upstream's E0596 there is "cannot borrow data in an `Rc` as
mutable" (no `DerefMut`) and ours is "not declared as mut". Both refuse the same
program for a reason rustc gives; it is not THE reason. Counted, and labelled.

M2 + M3 measured together on 3397a2dc7ba8d280: ledger 267/267, `-L bc -L pass`
898/898, the spec selection 190/190, all four stdlib layers, and the fail-text
oracle **byte-identical** to the opening build over all 1079 fixtures the two
builds share — 0 rc flips, 0 `.expected` losses, 0 text-only changes.

## 3. ⚠ M4a WAS REFUTED AT ITS FIRST SPELLING, BY A CONTROL REVERT

The named repair — "key the reborrow exemption on the DEREFERENCED reference,
not the root" — is the mirror of a gate `record_borrow` already has, and that
gate's own comment says the mirror is "deliberately not made here". Made as
spelled (`through_ref_type` is a `MutRef` ⇒ exempt), the 1090-fixture oracle
read THREE changes, one of them an **UN-REFUSAL**:

    RC    bc_match_deref_mut_not_mut   1 -> 0     ← a pinned illegal program
    MATCH borrowck-no-cycle-in-exchange-heap--move-while-refmut-borrowed  LOST
    TEXT  bc_d1r10_sp1_aggregate_copied_out

**THE CONTROL WAS RUN BEFORE THE CAUSE WAS GUESSED** (rule 18, the other
direction): `git stash` of the borrow_check hunk, rebuild to
e044b32a1b411792, three hand programs one variable apart:

    let b: Box<i64>; &mut *b            ADMITTED on BOTH binaries  ← PRE-EXISTING
    match *x { Node(ref mut y) => … }   REFUSED control, ADMITTED armed ← MINE
      x: Box<Cycle>
    the same match with x: Cycle        REFUSED on both            ← control

So the Box shape is TWO defects, and only the second is this change's. `*b` on a
`Box<T>` is lowered to a deref of a **CALL RESULT**, and the walk crosses the
`&mut T` that `deref_mut` returned — a reference manufactured out of `&mut b`,
which cannot exempt `b` from being `mut`.

⇒ **`BorrowPlace::through_ref_is_place`**: the crossing exempts only when the
reference crossed is a PLACE the source names (`lir_view::is_place_expr` at each
`cross()` call site). With it, e6b440c004a4c6b0 reads **1 of 1090 changed, and
it is TEXT-ONLY**: `bc_d1r10_sp1_aggregate_copied_out` loses a spurious
"cannot borrow 'inn.r' as mutable: 'inn' not declared as mut" that was printed
AHEAD of its pinned line. The edit is purely subtractive on the report path
(`&& !thru_mut_ref` added to four refusal conditions and nothing else), so a
text change can only be a DELETED diagnostic — rule 14's good direction, and the
only one of this round's four arms that deletes anything.

Ledger 267/267 under M4a alone, as predicted: it is permissive and closes no row.

## 4. M4b — THE SAME RULE AT THE OTHER TWO SITES

With the exemption asked of the right thing, the by-value gate moves to `f` and
`aot` unchanged. CEILING 3, PREDICTED BY NAME, both directions empty:

    borrowck/mutability-errors                       site f   upstream E0594/E0596
    borrowck/borrowck-ref-mut-of-imm--ref-mut-of-imm site f   upstream E0596
                                                     (bck.B — mis-rooted)
    borrowck/borrowck-argument                       site aot upstream E0596

COST 0 on both legal populations, stdlib ok, and the fail oracle **0 of 1090
changed against M4a** — i.e. the two costs 2026-08-31o measured for
`mbparamvalf` (bc_thru_ref_field_mut_reborrow_admit) and `mbparamvalaot`
(bc_d1r13_p3_byvalue_outparam, text-only and invisible to ctest) are BOTH gone,
which is the prediction that funded M4a.

⚠ THE PER-SITE CEILINGS WERE RE-PRICED ON THE POST-M3 BINARY, not inherited:
`mbparamvalf` 107 fires / ceiling 2 / cost 1 / cfail 0 and `mbparamvalaot` 1114
fires / ceiling 1 / cost 0 / cfail 1-text-only, on 3397a2dc7ba8d280. Rule 8 was
applied inside the round, not only at its start.

## 5. RULE 5 — THE HAND PROGRAMS, ALL MULTI-LINE, ALL ON THE FINAL BINARY

Nineteen programs. The nine that attack M3 and the four that attack M4 are in
the tree as `pass/bc_mbparamval_legal_shapes`,
`pass/bc_capmovewalk_move_body_legal_shapes` and
`pass/bc_thrumutref_legal_shapes`, each with its refuse half named in its own
header — a hand program dies with its round otherwise.

    M3, the abuse direction (rule 12): a `let mut` and a closure `|mut f|` each
      shadowing the parameter's NAME; a `&mut` param spelled through a TYPE
      ALIAS (`param_byval_` is filled by asking `is_ref_kind` on the declared
      type); a generic `mut t: T` (`is_ref_kind` is false for a type parameter,
      so `T` lands in `param_byval_`); a by-value `mut self`; a SHARED borrow of
      exactly the population the rule now refuses; the three reference-param
      reborrow shapes that are 99.998% of the hatch.        ALL rc 0
    M3, the defect: `fn q(f: i64) { use_mut(&mut f); }`      rc 1, and its
      one-token twin `fn q(mut f: i64)`                      rc 0
    M2: a move body mutating its own capture while the enclosing frame keeps
      using the original; a move body taking `&mut` of both a moved capture and
      a body local; a move closure and the enclosing frame on disjoint roots.
      Each fired `capmovewalk` exactly once on b8dbdb689af27a8b.   ALL rc 0
    M4: `&mut *h.r` and `&mut *o.i.r` out of by-value params, and `*h.r = v`
      (the AddrOfTemp door).                                  ALL rc 0
    M4, the defect, ONE `*` away: `&mut h.r` — the field ITSELF, a place inside
      a by-value `h`.                                         rc 1
    M4, the refusing half unmoved: `h.r: &i64`, `&mut *h.r`   rc 1

## 6. OPEN, WITH ITS EVIDENCE

 1. **`let b: Box<i64>; let r: &mut i64 = &mut *b;` IS ADMITTED, AND WAS BEFORE
    THIS ROUND** — proven by control revert (§3), rustc E0596. A `Box` deref is
    a call and the mut-binding question never reaches the binding. Not this
    round's; named because the round's first spelling was blamed for it.
 2. **`borrowck-unboxed-closures`** — the eleventh row of the `bck.A-1`
    partition. Calling an `FnMut` through a binding takes no borrow at all, so
    the gate has nothing to refuse. Same partition, different site.
 3. **`bck.A-2` IS CLOSED BY M4's MECHANISM AND ITS ROW IS NOT.**
    `mut-borrow-of-mut-ref` (`fn f(b:&mut i64) { h2(&mut b) }`) is `&mut` of a
    REFERENCE-TYPED PARAMETER, which the hatch still exempts — the by-value
    split does not reach it. The exemption now knows which reference was
    dereferenced; what it still cannot do is tell `&mut b` from `&mut *b` after
    auto-reborrow has run.
 4. **`capshared`'s four rows** stay blocked on the same unblocking
    2026-08-31o named: delegate `take_borrow_whole_`'s moved-value arm to the
    reader that owns "use of moved value … (moved on line N)". Not attempted.
 5. **`bck.D-H`, seven rows, one mechanism** — a loan whose HOLDER is a
    compiler-made temporary, proven live by two controls in 2026-08-31o §8.
    The biggest surveyed prize and untouched.
 6. **`bck.D-R`, three rows, NO ORACLE on this box** (E0716 temporary-lifetime
    extension; rustc absent).
 7. `loan_carrying_type` vs `is_borrow_carrying_type` — still unmeasured.
 8. `m4_defect_2`'s message says "'h' is behind a `&` reference" when it is
    `h.r` that is. Pre-existing wording, not moved by this round.

# ═══ ROUND 2026-08-31q — bck.D-H IS NOT ONE MECHANISM, AND THE ONE THAT
# SURVIVES IS THE `for` LOOP'S ITERABLE ══════════════════════════════════════

## 0. ⚠ THE CENSUS FIRST (RULE 17), AND THE BRIEF WAS WRONG ABOUT TWO NAMES

Every name this round's brief handed down, grepped against the tree:

    name                    installed?   what it actually is
    capshared               YES          one `probe::on` site, sema_expr.cpp
    current_lt_binders()    YES          outlives.hpp:51, sema_decl.cpp:1121
    loan_carrying_type      YES          borrow_check.cpp:6658, still unmeasured
    is_borrow_carrying_type YES          borrow_check.cpp:6635
    lifereg_unmentioned     NO           not a `probe::on` name anywhere
    ltbindersoff            NO           not a `probe::on` name anywhere
    capmovewalk             NO           ⚠ NOT A PROBE — IT LANDED

⚠ **`capmovewalk` COULD NOT BE RE-PRICED AS A CONTROL, BECAUSE IT IS NOT A
PROBE ANY MORE.** The brief said "still a probe in borrow_check.cpp, fundable
as measured". It landed LAST ROUND as 2026-08-31p's M2 (ledger 276 → 274); what
is at borrow_check.cpp:2593 today is the LANDED rule with a one-line marker.
Its two rows are already bought. Recommendation 1 of the brief was a
recommendation to re-buy them.

⚠ **RECOMMENDATION 2 (delete `current_lt_binders()`) IS THE ONE 2026-08-31p §1
ALREADY WITHDREW**, by reading the diagnostic: `lifereg_unmentioned`'s single
ceiling row is E0310 upstream and the wider rule refuses a REGION relation
rustc's inference satisfies. A row closed by a wrong diagnostic is not closed.
Re-proposed verbatim by this round's brief; still withdrawn.

Recommendation 3 (`capshared` blocked on delegating `take_borrow_whole_`'s
moved-value arm) is the only one of the three that survives its own grep.

## 1. THE FOUR CONTROLS, RE-RUN ON TODAY'S BINARY (rule 18)

Opening build **df6585c06bbd0d8b**, tree clean at 4fa422e31. All four reproduce:

    A1  double_access(&mut a, &a)                     rc 0   the defect
    A2  let m = &mut a; let s = &a; double_access(m,s) rc 1
          "cannot borrow 'a' as shared: already mutably borrowed"
    B1  for value in &values { values.push(4i64); }    rc 0   the defect
    B2  let it = &values; values.push(4i64); touch(it) rc 1
          "cannot borrow 'values' as mutable: 'values' has shared borrows"

## 2. ⚠ THE HOLDER IS NOT WHERE A1 IS DECIDED — IT IS A TWO-PHASE RESERVATION

The brief's subject was "a loan whose HOLDER is a compiler-made temporary is
released immediately". For the two-phase rows that is the wrong reading, and
five one-variable hand programs on the opening binary say so:

    c1  two_mut(&mut a, &mut a)                       rc 1  "already mutably borrowed"
    c2  let s = &a;      double_access(&mut a, s)     rc 1  "1 shared borrow(s) active"
    c3  let m = &mut a;  double_access(m,  &a)        rc 1  "already mutably borrowed"
    d1  rev(&a, &mut a)                               rc 0  ← admitted, order reversed
    d3  the struct-typed twin of A1                   rc 0  ← admitted

c1 refuses, so the FIRST argument's loan IS alive when the second is taken —
it is not released, and no holder is missing. What A1 walks into is
`take_borrow_whole_`'s **B82 arm at borrow_check.cpp:4377**:

    if (in_call_args_ > 0) { … it->mut_reservations++; … return; }

Inside call-argument evaluation a `&mut` is deposited as a *reservation*, which
by construction "is compatible with SHARED reads taken during the same
argument evaluation" — its own comment says so. That is Rust's two-phase borrow,
and Rust applies it to the **receiver autoref of a method call** only. Ours
applies it to every argument. The upstream file is literally named
`two-phase-NONRECV-autoref`.

**THE SITE AND THE INNER PREDICATE, NAMED SEPARATELY (rule 9).**
`argresvall` deletes the reservation arm; `argresvnonrecv` keeps it only when
`holder == "__recv_resv"` — the synthetic holder the bare-place receiver
deposit at borrow_check.cpp:14415 already uses, so the receiver keeps two-phase
and an explicit `&mut` argument does not.

## 3. THE PROBE TABLE — TWO BATCHES, BOTH BUILD HASHES READ

Batch 1, build **f27993b723005df7**, L1 rc 0 with nothing armed:

    probe             fires  ceiling  cost  cfail  stdlib  verdict
    argresvall       279234        4    12      2      ⛔  DEAD
    argresvnonrecv   279225        4     8      2      ⛔  DEAD
    foreachiterloan      26        2     1      0      ok  cost is the HOLDER
    idxwbase              0        —     —      —      —   NEVER FIRED

Batch 2, build **48ffda773402a6cf**, L1 rc 0 with nothing armed:

    probe             fires  ceiling  cost  cfail  stdlib  verdict
    foreachitertmp       26        2     0      0      ok  ✓ FUNDABLE
    dwbaseloan         4372        4    25     17      ⛔  DEAD, AND ITS FOUR
                                                          ROWS CLOSE ON THE
                                                          WRONG DIAGNOSTIC

Ledger 264 throughout; final build **7d81159883f30270**, L1 rc 0,
`gate-run.sh -L bc` 1997 passed / 0 failed / 2 disabled.

## 4. THE TWO RESERVATION PROBES ARE DEAD, AND THE INNER PREDICATE DID NOT SAVE THEM

Both refuse `logos.mem`:

    error [fn btvec_append]: cannot borrow 't' as shared: already mutably borrowed

⚠ **RULE 9, THE OTHER OUTCOME.** The inner predicate is NOT priced identically
to the site: cost 12 → 8, and the four fixtures it saves are exactly the
receiver ones (`bc_recv_addroftemp_resv_admit`, `tpb-vec-extend-from-self`,
`tpb-vec-push-len`, `two-phase-baseline`). It separates cleanly on the pass
corpus and still dies, because eight legal programs and the stdlib refuse for
a reason the receiver split does not reach — a non-receiver `&mut` argument
beside a shared read of the same root is a shape our own corpus is FULL of.
CEILING 4 vs COST 8, cfail 2 (one `.expected` LOST:
`bc_recv_reservation_conflict_fail`), stdlib ⛔. Declined at both spellings.

⚠ The four ceiling rows' DIAGNOSTICS WERE NOT READ. The stdlib refusal outranks
every number above it, the tree was reverted, and a row under a probe that does
not build the stdlib is not a row. The set is recorded as a set, not as rows:

    borrowck_augmented-assignments                       bck.NEW-1  ← not predicted
    borrowck_two-phase-nonrecv-autoref--c-mut-and-shared-args  bck.D
    borrowck_two-phase-nonrecv-autoref--d-index-two-phase      bck.D
    nll_issue-27868                                      nllmoves.R11 ← not predicted

**PREDICTED vs MEASURED, BOTH WAYS (rule 6):** predicted 2 by name, both hit;
2 more arrived, BOTH FILED UNDER OTHER ROOTS. The brief warned this would
happen and it happened at the same ratio as last round.

## 5. `idxwbase` NEVER FIRED, AND THAT IS RULE 1 IN ITS PLAIN FORM

The probe was written at `Code::IndexWrite` in `visit_stmt`. Fires: **0** over
the whole ledger population. `arr[i] = v` lowers to `SDerefWrite(AddrOfTemp(
IndexRead(...)), val)` — the `Code::DerefWrite` arm's own comment says so, ten
lines above where the probe sat. A zero over an unreached site is not a
measurement of anything, and the harness printed exactly that.

Re-aimed as `dwbaseloan` at the live site (`visit(v.ptr(), …)` at
borrow_check.cpp:12820, holding a mut loan of the indexed root across the
index's own evaluation): 4372 fires, so the site is live.

## 6. ⚠ `dwbaseloan`'s CEILING OF 4 IS FOUR WRONG DIAGNOSTICS

    row                                          our message
    two-phase-nonrecv-autoref--c-mut-and-shared-args
        error [fn double_access]: cannot borrow 'm': 'm' is already mutably borrowed
    two-phase-nonrecv-autoref--d-index-two-phase
        error [fn put]: cannot borrow 'a': 'a' is already mutably borrowed
    mut-slice-struct-lifetime-transmute--c17
        error [fn lifetime_transmute_direct]: cannot borrow 'out': 'out' is …
    mut-slice-struct-lifetime-transmute--t17                (same shape)

Every one names the CALLEE's own parameter and fires inside the CALLEE's body
(`m[0] = s[1]`, `a[i] = v`), not at the caller's `double_access(&mut a, &a)`.
The probe refuses the helper functions themselves. Upstream's error is E0502 at
the CALL. **A ROW CLOSED BY A WRONG DIAGNOSTIC IS NOT CLOSED** — the honest
ceiling is 0, and the cost is 25 legal programs + 17 text-only fail changes +
a stdlib that does not build.

⚠ AND IT DID NOT REACH THE ROWS IT WAS AIMED AT. `suggest-local-var-for-vector`
and `suggest-storing-local-var-for-vector` (`v[v.len() - 1] = 123`) — the two
rows this probe exists for — score **0 fires** on their own files: a `Vec`
index is not a `DerefWrite(AddrOfTemp(IndexRead))` either. The index-self-borrow
partition is STILL UNREACHED BY ANY SITE PROBED THIS ROUND.

## 7. ⇒ `foreachitertmp` — CEILING 2, COST 0, cfail 0, STDLIB ok

The `Code::ForEach` arm (borrow_check.cpp:13119) visits its iterable with a
plain `visit(v.iter(), …)` and **records no loan at all**: under
`LOGOS_DUMP_BC_RELEASE`, B1 prints not one `target=values` line while its named
control B2 prints `frame=1(back) target=values holder=it is_mut=0`. So this
half of bck.D-H IS the brief's subject — a loan with no holder — and the
machinery to fix it already exists under another name: `retain_temp_scrut_loan`
(borrow_check.cpp:6229) does exactly this for a temporary match scrutinee,
under a synthetic holder no source binding can spell.

CEILING 2, PREDICTED BY NAME, closed set = predicted set, both directions
empty, and both refuse for a reason rustc gives:

    borrowck_mutate-vec-while-iterating--a-push-while-iterating   bck.D
        "cannot borrow 'values' as mutable: 'values' has shared borrows"  E0502
    borrowck_mutate-vec-while-iterating--b-push-while-iterating-mut  bck.D
        "cannot use 'values' while it is mutably borrowed"                E0499

## 8. ⚠ AND THE HOLDER CHOICE WAS THE WHOLE COST — MEASURED, ONE TOKEN APART

`foreachiterloan`, the SAME rule with the loop VARIABLE as the holder, priced
CEILING 2 / COST 1. The cost is `logos_25_spec_pass_stmt_2`, and it took three
repros to find because the shape is not local:

    g1  the whole stmt_2 for-block, verbatim                      rc 0
    f1  for x in &mut m { *x = *x + 100 }  then  m[0]             rc 0
    g2  f1 PLUS an unrelated `let x: i64 = 99;` AFTER the loop    rc 1
          "cannot use 'm' while it is mutably borrowed"

The only difference between f1 and g2 is a later binding that happens to be
SPELLED `x`. `last_use_unslotted_` is charged to EVERY binding of a name (its
own comment says so), so the loop variable's name collides with any later `x`
and the iterable's loan outlives the loop. **RULE 12, in its plain form: a set
of strings cannot say which binding a name denotes.**

⇒ **THE ANSWER TO THIS ROUND'S QUESTION.** When a loan's holder is a
compiler-made temporary the holder must be *the enclosing statement*, spelled
as a fresh synthetic name (`__foreach_it_N`, `scrut_tmp_seq_`), NOT any source
binding that happens to be in scope. A synthetic holder has no recorded use, so
its life is decided by the frame it was recorded in — which is precisely the
`for` statement — and the cost goes 1 → 0 with the ceiling unmoved.

## 9. RULE 5 — THE HAND PROGRAMS, ALL MULTI-LINE, ALL ON THE ARMED BINARY

Eight legal programs, all rc 0 armed and unarmed, plus the defect pair:

    f1  for x in &mut m { *x = *x + 100 }; read m after            rc 0
    f2  for x in &arr  { n = n + *x };     read n after            rc 0
    f3  the mutating loop with no later read                       rc 0
    g1  stmt_2's whole for-block: `for x in sl` over `&[i64]`,
        `&arr[i]` INSIDE the body, then `for x in &mut m`          rc 0
    g2  f1 + a later unrelated `let x` — foreachiterloan's ONLY
        cost, and foreachitertmp's discriminator                   rc 0
    h1  for value in &vs { … }  then  vs.push(2)  AFTER the loop   rc 0
    h2  for value in &vs { … total(&vs) … }  — shared read INSIDE  rc 0
    h3  `for (a, b) in &v` (tuple pattern) + a later `let a`       rc 0
    h4  two nested loops over ONE iterable + a later `let x`       rc 0
    b1  for value in &values { values.push(4) }   THE DEFECT       rc 1
    b2  its named control, unmoved                                 rc 1

⚠ **THE POPULATION, WALKED (rule 4).** The ForEach arrival was censused over
the WHOLE `tests/` tree — 8695 `.logos` files compiled under
`LOGOS_PROBE_FIRE`: **70 files CONTRIBUTED, 109 arrivals**. That is small, and
it bounds the COST, not the ceiling: cost 0 over 70 files is weak, which is why
the eight hand programs above are the actual safety argument. The ceiling of 2
is a positive measurement and needs no population argument.

## ⇒ 10. WHAT DESERVES FUNDING, IN ORDER

 1. **`foreachitertmp` — RECORD THE `for`-ITERABLE'S LOAN UNDER A STATEMENT-
    SCOPED SYNTHETIC HOLDER.** Two rows, cost 0 / cfail 0 / stdlib ok, closed
    set = predicted set both ways, both diagnostics read and both E0502/E0499
    shaped. The one clean arm this round produced.
 2. **DELEGATE `take_borrow_whole_`'s MOVED-VALUE ARM** to the reader that owns
    "use of moved value … (moved on line N)" — unblocks `capshared`'s four
    rows. Unchanged from 2026-08-31o §7 and 2026-08-31p §6.4; still the only
    handed-down recommendation that survived its own grep.
 3. **THE NON-RECEIVER TWO-PHASE QUESTION, RE-AIMED.** Both spellings priced
    and both dead (§4), but the OBSERVATION is correct — we two-phase every
    argument and Rust two-phases only the receiver. What the corpus says is
    that the reservation must be activated at the CALL, not deleted at the
    ARGUMENT: `f(&mut a, &a)` is illegal while `f(&mut a, a.len())` is legal,
    and both are "a shared read during argument evaluation". A round that
    cannot tell those apart cannot have this.

## 11. OPEN, WITH ITS EVIDENCE

 1. **bck.D-H IS AT LEAST THREE MECHANISMS, NOT ONE.** The survey grouped seven
    rows by a symptom. Measured: two are the `for`-iterable (closed by 1
    above), three are the two-phase reservation (§2, §4), two — the
    `v[v.len()-1]` pair — are reached by NEITHER, and `a-fnmut-twice` was
    touched by nothing. A SHARED SYMPTOM IS NOT A SHARED DEFECT.
 2. **THE INDEX-SELF-BORROW PAIR HAS NO PROVEN-LIVE SITE YET** (§6). Both
    `Code::IndexWrite` (0 fires) and `Code::DerefWrite` (0 fires ON THOSE TWO
    FILES) miss it. A `Vec` index goes somewhere else; find it before pricing.
 3. **`two-phase-nonrecv-autoref--a-fnmut-twice`** — `f(f(10))` with
    `f: &mut F`. Untouched by all six probes. Same shape as
    `borrowck-unboxed-closures`: calling through a binding takes no borrow.
 4. **`bck.D-R`, three rows, NO ORACLE on this box** (E0716). Unchanged.
 5. `loan_carrying_type` vs `is_borrow_carrying_type` — still unmeasured, third
    round running.
 6. **`argresvnonrecv`'s `.expected` LOSS** on
    `bc_recv_reservation_conflict_fail` was read as a count, not as text. If
    the reservation arc is ever re-opened, read that fixture first.

# ═══ ROUND 2026-08-31r — THE `for`-ITERABLE LOAN LANDS; THE MOVED-VALUE FACT
# GETS ONE READER; AND capshared'S BLOCKER IS NOT THE ONE THE BRIEF NAMED ═════

## 0. THE CENSUS FIRST (RULE 17) — AND THE PREVIOUS ROUND'S CENSUS WAS STALE

Every name in this round's brief, grepped against the tree at d2b662bd8:

    name                    installed?   what it actually is
    foreachitertmp          YES          probe, borrow_check.cpp:13160
    capshared               YES          probe, borrow_check.cpp:10194
    current_lt_binders()    YES          outlives.hpp:167, sema_decl.cpp:1121
    lifereg_unmentioned     YES  ⚠       probe, outlives.hpp:154
    ltbindersoff            YES  ⚠       probe, outlives.hpp:168
    loan_carrying_type      YES          borrow_check.cpp:6658, still unmeasured
    capmovewalk             NO           LANDED 2026-08-31p, marker at :2593

⚠ **2026-08-31q's OWN CENSUS SAID `lifereg_unmentioned` AND `ltbindersoff` WERE
"NOT A `probe::on` NAME ANYWHERE". BOTH ARE.** They sit in
`include/logos/compiler/outlives.hpp`, and that round's grep did not reach it —
a census over `src/compiler/*.cpp` alone cannot see a header. Rule 17 applies to
the round's own census as hard as to the brief: **the population a census walked
must be printed with its answer.** Priced here, both of them (§4).

Only `capmovewalk` is what the previous round said it was: already bought.

## 1. THE FOUR SUBJECT-1 CONTROLS, RE-RUN ON THE FINAL BINARY (rule 18)

    A1  double_access(&mut a, &a)                      rc 0  ← STILL ADMITTED
    A2  the same two loans, each in its own let        rc 1  "cannot borrow 'a'
                                                             as shared: already
                                                             mutably borrowed"
    B1  for value in &values { values.push(4) }        rc 1  ← CLOSED BY M1
    B2  let it = &values; values.push(4); touch(it)    rc 1  unmoved

A1/A2 are the two-phase partition, untouched and declined again (§5). B1 was the
defect and is now refused; B2, its named control, refuses for the same reason it
always did.

## 2. M1 — THE `for` ITERABLE'S LOAN, UNDER A STATEMENT-SCOPED SYNTHETIC HOLDER

`Code::ForEach` visited its iterable with a plain `visit()` and recorded no loan
at all. It now takes one through `take_ref_borrows(..., record_only=true)` held
by `__foreach_it_N` — a fresh synthetic name, the same machinery
`retain_temp_scrut_loan` uses for a temporary match scrutinee, and NOT the loop
variable (the sibling spelling `foreachiterloan` priced COST 1 for exactly that:
`last_use_unslotted_` charges a name to every binding that spells it, so a loop
variable `x` collides with any later `let x`).

**RE-PRICED ON TODAY'S BINARY BEFORE FUNDING (rule 8).** Builds 294 -> 295 on
libs `7d81159883f30270`:

    fires 26 · CEILING 2 · COST 0 (pass) · COST-fail 0 of 1094 · stdlib 4/4

**CEILING vs PREDICTED vs ACTUAL, DIFFED BOTH WAYS (rule 6):** predicted 2 by
name in the pre-edit prediction file, closed exactly 2, both directions empty.
Both diagnostics READ, both E0502/E0499-shaped, both naming `values`:

    borrowck/mutate-vec-while-iterating--a-push-while-iterating   bck.D  E0502
        "cannot borrow 'values' as mutable: 'values' has shared borrows"
    borrowck/mutate-vec-while-iterating--b-push-while-iterating-mut bck.D E0499
        "cannot use 'values' while it is mutably borrowed"

**CONTROL REVERT, RUN BEFORE THE BUILD.** All three programs this arm is
supposed to refuse — the two rows and the new native fail fixture — compile
rc 0 on the pre-fix binary with the probe OFF. The fixtures are held by the
change and by nothing else.

MEASURED on the landed build `09f102389cdb5e77`: ledger **262/262**,
`gate-run.sh -L bc` 2001 passed / 0 failed / 2 disabled, stdlib all four layers,
and the fail oracle **0 rc flips / 0 `.expected` losses / 0 text-only** over all
1094 fixtures the two builds share (the 3 rows that appear only on the new side
are the two relanded ports and the new native fixture).

### 2.1 RULE 5 — THE NINE HAND PROGRAMS, EACH PROVEN TO REACH THE SITE

`tests/logos/pass/bc_foreachit_legal_shapes` carries all nine; each was run with
`LOGOS_PROBE_FIRE` on the armed binary and each fired at least once (g1 twice,
h4 three times, the fixture as a whole **12 fires**). A zero over a site nothing
arrives at is not a measurement, and the cost population here is small: the
ForEach arrival census walked the whole `tests/` tree at 8695 `.logos` files and
only **70 files contributed, 109 arrivals**. That bounds the COST, not the
ceiling. The nine programs are the safety argument; the corpus is not.

    (1) mutate through `&mut` in the loop, read the iterable after
    (2) shared iteration, accumulator read after
    (3) the mutating loop with no later use at all
    (4) a `&[T]` iterable + a shared borrow of an ELEMENT + a second loop
    (5) (1) plus a later `let x` spelled like the loop variable  ← THE
        DISCRIMINATOR between this holder and `foreachiterloan`'s
    (6) a push INSIDE the body, to a DIFFERENT vector
    (7) a shared read of the iterable from INSIDE its own loop
    (8) a tuple-pattern loop variable plus a later binding spelled `a`
    (9) two NESTED loops over ONE iterable plus a later `let x`

Refuse half, ONE TOKEN from (6):
`tests/logos/fail/bc_foreachit_mutate_while_iterating_fail` pushes to `vs`, the
vector being iterated, where (6) pushes to `ws`.

⚠ **OPEN, CREATED BY THIS ARM:** both closed rows now print TWO error lines for
one conflict — the primary plus a second phrasing from the loop's second
(back-edge) visit of the body. `.expected` pins the primary. Not a duplicate of
an existing message (the two texts differ), but it is noise, and rule 14's
reverse direction says to say so.

## 3. M2 — ONE READER FOR THE WHOLE-VALUE MOVED FACT. ZERO ROWS, AND IT DOES
##      NOT UNBLOCK capshared

`take_borrow_whole_`'s moved arm spelled the fact for itself and dropped the
LINE: `"cannot borrow moved value 'x'"` against `consume`'s and `check_live`'s
`"use of moved value 'x' (moved on line N)"`. Three spellings, one fact.
`report_moved_value` now owns the fact and its line; the VERB stays with the
route, because upstream E0382 has both headlines and a borrow of a moved value
is "borrow of moved value".

**THE CHANGED SET WAS ENUMERATED, NOT GUESSED.** All 2218 `*/fail/*.logos`
compiled on the pre-edit binary and grepped for the message: **exactly 8 files**
reach the arm, named in the prediction file before the edit. Measured after:
7 of the 8 changed in the `-L bc -L fail` oracle and the 8th
(`spec/fail/borrow_diag_1__take-moved-cannot-borrow`) is outside that selection,
changed as predicted and green under its own ctest name. Both directions empty.

    rc flips 0 · `.expected` losses 0 · text-only 7 (+1 outside the selection)

All eight pins survive because five pin the borrow phrase (a `.expected` is a
grep -F SUBSTRING and the line is APPENDED), two pin the other route's line, and
one pins `moved`. Ledger **262/262** unmoved — **this arm closes NO row**, which
is what rule 14 predicts for a rewording, and it is landed for the information
it restores plus its own pinned pair, not as a row claim.

Pins: `tests/logos/fail/bc_movedvalue_borrow_carries_move_line_fail` (pinned in
full, WITH the line number) and its admit twin ONE TOKEN apart,
`tests/logos/pass/bc_movedvalue_borrow_not_moved_admit` (`&a` instead of `a`).

## 4. ⛔ THE THREE STANDING RECOMMENDATIONS — ALL THREE DECLINED, BY NAME AND
##      BY NUMBER

**(1) `capmovewalk` — DECLINED: ALREADY BOUGHT.** Not a probe; it landed as
2026-08-31p's M2 and `borrow_check.cpp:2593` carries its marker. Re-priced as a
control it cannot be. This is the SECOND round in a row the brief has proposed
re-buying it.

**(2) DELETE `current_lt_binders()` — DECLINED TWICE OVER, and both halves are
measured on THIS binary (`6f7abce049716a19`), not inherited:**

    lifereg_unmentioned   4 fires  CEILING 1  cost 0  cfail 0  stdlib ok
    ltbindersoff          3 fires  CEILING 0  cost 0  cfail 1  stdlib ok

  · the WIDER rule's single ceiling row is
    `lifetimes/suggest-introducing-and-adding-missing-lifetime--param-may-not-live`,
    and its diagnostic was READ on this binary:

        upstream  E0310  the parameter type `T` may not live long enough
        ours      call to 'with_restriction' arg 1: variance mismatch —
                  expected &'b i64, got &i64 — lifetime structure incompatible

    A row closed by a wrong diagnostic is not closed. Honest ceiling **0**.
  · and the deletion is not free: `ltbindersoff`, the CONTROL REVERT of the
    predicate alone, closes 0 rows and costs one **UN-REFUSAL** —
    `fail/account-for-lifetimes-in-closure-suggestion` rc 1 -> 0. The
    deliberately-unsound name-collision approximation is LOAD-BEARING.

  ⇒ **THE UNSOUND APPROXIMATION IS STILL THERE, DELIBERATELY.** Deleting it
  trades one pinned refusal for one row closed by the wrong error.

**(3) `capshared` — DECLINED, AND ITS BLOCKER IS NOW NAMED CORRECTLY.** The
brief said the unblocking was M2: route the shared whole-var capture through
`record_borrow`, hit `take_borrow_whole_`'s moved arm, lose the move LINE.
M2 LANDED, and capshared was re-priced on top of it (builds 298 -> 299):

    166 fires · CEILING 4 · cost 0 · **cfail 6, UNMOVED** (4 `.expected` LOST,
    2 text-only) · stdlib ok

**THE LINE WAS NEVER THE PROBLEM. THE VERB IS.** Read on the post-M2 binary:

    fixture                              pin                       armed
    moves/arc-consumed-in-looped-closure "use of moved value 'v'    "cannot borrow
    borrowck-in-static--move-captured-…   (moved on line N)"         moved value
    borrowck-in-static--r-runtime                                    'x' (moved on
    nll/closure-move-spans                                           line N)"

All four now carry the line and all four still lose, because the pin says
*use* and the capshared route reports through a *borrow*. Three of the four are
a value moved INTO a closure and used again, where rustc's headline is "use of
moved value"; one (`closure-move-spans`, upstream's own `borrow_after_move` arm)
is a `&x` after the move, where rustc's headline is "borrow of moved value" —
so on that one it is arguably the PIN that is wrong, and **rustc is absent on
this box, so there is no oracle that can say.** Changing four pinned diagnostics
on an argument is weakening four tests to make a mechanism green.

⚠ AND capshared now prints `closure-move-spans`'s message **TWICE** (rule 14 in
reverse): both the capture deposit and the `&x` reach the same arm at the same
line. That is a defect capshared creates, not one it inherits.

## 5. DECLINED AGAIN, WITH THE NUMBER THAT CONDEMNS IT

  · **the two-phase reservation** (`argresvall` 4 vs 12/2/⛔;
    `argresvnonrecv` 4 vs 8/2/⛔) — A1 still admitted, both spellings still
    dead, and the observation still correct: the reservation must be ACTIVATED
    AT THE CALL, not deleted at the argument. Not re-priced this round; the
    stdlib refusal (`btvec_append`) is not a number that decays toward funding.
  · **`dwbaseloan`** (4 vs 25/17/⛔) and its four WRONG diagnostics, all naming
    the CALLEE's parameter from inside the callee's body. Honest ceiling 0.
  · **the index-self-borrow pair** — still no proven-live site.

## 6. OPEN

 1. **bck.D is now 8 rows and still at least three mechanisms.** M1 took the two
    `for`-iterable rows. Three are the two-phase reservation, two
    (`v[v.len()-1]`) are reached by no proven-live site, `a-fnmut-twice` by
    nothing.
 2. **`capshared`'s four rows are blocked on a VERB, and the verb needs rustc.**
    The next round should not re-propose it without an oracle. If one appears,
    the question is per-fixture, not per-mechanism.
 3. **capshared creates a duplicate on `closure-move-spans`** (§4).
 4. **M1's two closed rows each print two error lines** (§2.1).
 5. **`current_lt_binders()` stays** and is load-bearing at cfail 1 (§4).
 6. `loan_carrying_type` vs `is_borrow_carrying_type` — unmeasured, FOURTH round.
 7. **A CENSUS'S POPULATION MUST BE PRINTED WITH ITS ANSWER.** 2026-08-31q's
    census missed two live probes because it walked `src/compiler/` only (§0).

## 7. ⚠ THE 16th GATE LIE, MET IN THIS ROUND: `RC_MARKER=0` OVER THREE
##      STANDING REDS

`test-levels.sh L4 bc` was run once, found the three census/population pins RED
(they red on ANY new test BY DESIGN — this round adds four), the pins were
re-derived BY DIRECT LISTING and fixed, and the SECOND L4 run printed
`RC_MARKER=0` **while still listing all three as failed**. Neither run measured
anything the second time: `gate-run.sh` keys the store on `build_hash.py`, a
`.sh`/`.md` edit does not move the compiler or the libraries, so the store
answered "already measured under this build" and replayed the STALE reds — and
the level's own exit status was 0 over them.

    FORCE=1 gate-run.sh -R '^logos_00_census_pin$|^logos_00_population_pin_lint$|
                            ^logos_09_direct_door_census$'
      -> 2514 passed / 0 failed (FIXTURES_REQUIRED pulls the producers)

after which the store for build 298 reads **5851 recorded, 0 failed**. A gate
whose subject is a SHELL SCRIPT or a PINNED DOCUMENT is invisible to a build
hash, and its record must be forced. Recorded so the next round does not read
that rc 0 as a green tree.

# ═══ ROUND 2026-08-31s — capshared'S BLOCKER IS THE MOVED ROOT, NOT THE VERB;
# THE TWO-PHASE ROW AND ITS COST ARE THE SAME PROGRAM; AND THE INDEX SHAPE HAS
# A SITE AT LAST — IN A CHANNEL NO PROBE HAD LOOKED AT ═════════════════════════

## 0. THE CENSUS FIRST (RULE 17), AND THE BRIEF WAS WRONG ABOUT THREE THINGS

Walked over `src` AND `include`, whole tree, not `src/compiler/` only:

    grep -rhoP 'probe::on\("\K[a-z_0-9]+' src include | sort -u | wc -l   → 129
    grep '^# TOTAL' tests/logos/bc_admits.ledger                          → 262
    rows with 3 fields, non-blank, non-comment                            → 262
    tests/imported/admit/*.logos                                          → 262
    distinct roots                                                        → 71
    top roots: bck.C 17 · lifereg.A 13 · nllmoves.C 12 · nllmoves.B 11
               lifereg.R17 11 · bck.B 11 · bck.NEW 10 · lifereg.R18 8
               lifereg.NEW-N1 8 · lifereg.C 8 · bck.D 8

Opening build **6f7abce049716a19**, tree clean, `git status` empty.

**CORRECTION 1 — `bck.D`'s partition. The brief says "three are the two-phase
partition"; they are TWO, and the LARGEST sub-partition is one the brief does
not name at all.** All eight rows read:

    &mk()      E0716 temporary dropped while borrowed   borrowck-borrowed-uniq-rvalue
    &mk().v    E0716                                     borrowck-borrowed-uniq-rvalue-2
    &mk().a    E0716                                     issue-36082
    v[v.len()-1] = x   E0502                             suggest-local-var-for-vector
    v[v.len()-1] = x   E0502                             suggest-storing-local-var-for-vector
    f(&mut a, &a)      E0502                             two-phase-…--c-mut-and-shared-args
    f(&mut a, i, g(&a)) E0502                            two-phase-…--d-index-two-phase
    f(f(10))           E0499                             two-phase-…--a-fnmut-twice

So bck.D is **FOUR** mechanisms, not three: a THREE-ROW E0716
temporary-lifetime partition that no round has ever mentioned, the two-row
index-self-borrow shape, two two-phase rows, and `a-fnmut-twice` (the known
FnMut-through-a-binding wall). The E0716 three are now the root's biggest
group and nothing in this file has ever priced them.

**CORRECTION 2 — `capshared`'s cause was NOT unknown.** The brief says "the
diagnosis was wrong and the real cause is unknown". 2026-08-31r §4 names it
(the VERB) and this round proves the layer under it (§3). Not unknown; just
not yet actionable in the form it was written down.

**CORRECTION 3 — capshared does NOT create the `closure-move-spans`
duplicate.** 2026-08-31r §4 records it as "a defect capshared creates". Read
on 6f7abce049716a19, UNARMED, that fixture already prints TWO lines:

    error [fn main]: use of moved value 'x' (moved on line 10)
    error [fn main]: cannot borrow moved value 'x' (moved on line 10)

capshared only makes the first line's verb equal the second's. The duplicate
is inherited, not created.

## 1. THE PROBE TABLE — build **dc6cb11cbed9f192** (READ), gate-db 302 → 305

L1 rc 0 with nothing armed (`probe-batch` proved the batch inert before pricing).

    probe             fires  ceiling  cost  cfail        stdlib  verdict
    argresvact            2        1     1      0            ok  see §2
    argresvsibshared     26        4     7      1 (text)      ⛔  DEAD
    capsharedlive       166        4     0      1 (text)      ok  ✓ FUNDABLE

`fires` is the INNER MATCH, not the arrival: all three probes call `probe::on`
only after the predicate is true, so 2 is the whole population of
`f(&mut x, …&x…)`-at-activation in the ledger corpus. The ARRIVALS are the
census, run separately under `LOGOS_CENSUS` on the same build (§2, §4).

## 2. ⚠ ACTIVATION AT THE CALL WORKS, AND ITS "COST" IS THE ROW IT CLOSES

`argresvact` is the spelling the corpus asked for and no round had priced:
`take_borrow_whole_` keeps depositing a RESERVATION at a `&mut` argument, and
at the END of `visit_args` — the call — any reservation whose root still has a
live shared borrow is refused. It is NOT the deletion of the reservation
(`argresvall`, ⛔) and NOT the receiver split (`argresvnonrecv`, ⛔).

    CEILING 1  borrowck_two-phase-nonrecv-autoref--c-mut-and-shared-args  bck.D
    COST    1  logos_02_semantic_core_pass_tpb-mut-with-shared-ref-arg
    cfail 0 of 1098 · **stdlib: all four layers compile** ← the first two-phase
                       spelling that survives the column that killed the others

DIAGNOSTIC READ, on the armed binary, at the CALLER and naming the caller's
root — not the callee's parameter:

    error [fn main]: cannot borrow 'a' as mutable: 1 shared borrow(s) active

⚠ **AND THE COST IS THE SAME PROGRAM AS THE ROW.** `tpb-mut-with-shared-ref-arg`
is, in full:

    fn write_then_read(m: &mut i32, r: &i32) -> i32 { *m = *r; return *m; }
    fn main() -> i32 { let mut x: i32 = 7i32;
                       return write_then_read(&mut x, &x) - 7i32; }

and the ledger row is the same program with `[i64; 3]` for `i32`. rustc rejects
both with E0502 (two-phase borrows are an AUTOREF adjustment — a method
receiver or an overloaded operator — never an explicit `&mut` argument; the
upstream file is named `two-phase-NONRECV-autoref` for that reason). The
fixture's own header says what it pins: "B82: `f(&mut x, &x)` — &mut
reservation + shared ref to same target."

**So the corpus books one program as a HOLE and pins its twin as PARITY.** That
is why every two-phase spelling has priced cost ≥ ceiling: the cost has been
this fixture every time. `argresvall`'s cost included it; `argresvnonrecv`'s
did too. Not a coincidence and not an exemption problem.

⚠ THIS IS NOT A LICENCE TO DELETE THE FIXTURE. A pass fixture that asserts a
DIVERGENCE is a claim about intended behaviour, and re-classifying it is a
corpus decision with an owner, not a probe result. Recorded, not acted on.
With it set aside the arm is CEILING 1 / COST 0 / cfail 0 / stdlib ok.

## 3. ⇒ capshared: THE BLOCKER IS THE MOVED ROOT, AND THE FIX IS A GUARD

Read on 6f7abce049716a19, all four `.expected` losses, base vs `capshared`:

    fixture                          .expected pins       capshared prints
    arc-consumed-in-looped-closure   use of moved value   cannot borrow moved
    borrowck-in-static--move-captu…  'v'/'x' (moved on    value 'v'/'x'
    borrowck-in-static--r-runtime    line N)              (moved on line N)
    nll/closure-move-spans

All four have a **MOVED** root. The base path answers the LIVENESS question
first (`check_live` → "use of moved value"); capshared replaces that branch
wholesale with `record_borrow`, so the moved fact comes back out of
`take_borrow_whole_`'s LOAN arm wearing the loan arm's verb. The line was never
the problem (2026-08-31r) and the verb is a SYMPTOM, not the cause: the cause is
that a liveness question is being routed through a loan channel.

`capsharedlive` = capshared, with the fall-through **guarded on the root being
LIVE**; a moved root keeps `check_live`:

    fires 166 · CEILING 4 · COST 0 · cfail 1 (TEXT-ONLY) · stdlib ok

    logos_00_bc_admit_borrowck_borrowck-closures-mut-and-imm            bck.C
    logos_00_bc_admit_nll_closure-borrow-spans--a                       nllmoves.*
    logos_00_bc_admit_nll_closure-borrow-spans--b                       nllmoves.*
    logos_00_bc_admit_regions_region-bound-on-closure-outlives-call     lifereg.*

**PREDICTED vs MEASURED, BOTH WAYS (rule 6): predicted these four BY NAME,
measured exactly these four, no arrivals and no departures.**

DIAGNOSTICS READ, against each fixture's own recorded upstream error:

    borrowck-closures-mut-and-imm   E0506  → "cannot assign to 'x' because it
                                             is borrowed"                 ✓
    closure-borrow-spans--a         E0502  → "cannot borrow 'x' as mutable:
                                             1 shared borrow(s) active"   ✓
    closure-borrow-spans--b         E0506  → "cannot assign to 'x' because it
                                             is borrowed"                 ✓
    region-bound-on-closure-…       E0505  → "cannot move 'f' while it is
                                             borrowed"                    ✓

⚠ **TWO BLEMISHES A LANDING ROUND MUST CLOSE, BOTH RULE 14 IN REVERSE.**
(a) `borrowck-closures-mut-and-imm` and `closure-borrow-spans--b` each print
the E0506 TWICE, in two wordings ("…because it is borrowed" / "…while 'x' is
borrowed"). (b) the one cfail,
`fail/closure-access-spans--a-closure-imm-capture-conflict`, is an ADDED second
line on an already-red fixture (`.expected` still matches, so ctest stays green
— rule 15's shape, caught only by the text oracle).

capshared's cfail 6 (4 `.expected` LOST) → capsharedlive's cfail 1, text-only.
**The four rows are no longer blocked, and no pinned diagnostic is weakened.**

## 4. ⚠ THE INDEX SHAPE: NOT IN THIS CHANNEL AT ALL — AND THE SITE IS IN THE
##      REGION INFERENCER, WHERE NOTHING HAD LOOKED

`argresvsibshared` is the ABUSE direction of §2 (refuse a SHARED borrow taken
while a reservation is live) and it is dead: `stdlib mem` REFUSED, 10 errors,
first `error [fn alt_expr_0]: cannot borrow 'lex' as shared: mutably reserved`;
cost 7, including `tpb-vec-push-len` and `two-phase-baseline` — i.e. it kills
`v.push(v.len())`, which is legal Rust. Its ceiling of 4 is real but unbuyable:
c-mut-and-shared-args, **d-index-two-phase**, and two rows NOT PREDICTED AND
UNDER ANOTHER ROOT (`regions_regions-adjusted-lvalue-op--c26` / `--t26`).
Third round running that most of a probe's surprise rows land under other
roots.

⚠ **RULE 9's OTHER OUTCOME, AND IT SEPARATES ON THE STDLIB COLUMN.** The site
(activation at the call) and the inner predicate (conflict at the shared side)
are NOT priced identically: 1 vs 4 rows, cost 1 vs 7, and stdlib **ok vs ⛔**.
The census says why, per fixture, on this build:

    fixture                     frame_mut  reserved  shared_live_at_call  sib
    c-mut-and-shared-args           129       129            1             1
    d-index-two-phase               129       129            0             1
    suggest-local-var-for-vector    128       128            0             0

`d`'s shared read lives inside a NESTED call frame that pops before the outer
call, so activation cannot see it; only the reservation-side check can, and
that check is what refuses the stdlib. **`d-index-two-phase` and
`btvec_append` are the same program shape** — `f(&mut t, g(&t))` — so any rule
that closes `d` refuses `logos.mem`. That is the wall, with a number.

⚠ **AND `suggest-local-var-for-vector` SCORES ZERO IN BOTH DIRECTIONS.** 128
is the prelude's constant; the user statement contributes NOTHING. So the
index-self-borrow shape does not reach `take_borrow_whole_`'s reservation
channel at all — no probe in this channel can ever see it, which is the third
round of probes aimed at it and the first measurement that says why.

**THE SITE, FOUND — `RegionInferer`, and its borrow table is ONE-SIDED.**
`LOGOS_DUMP_REGIONS` on three programs one variable apart, same build:

    let n = v.len();                    fn main — 0 regions, 0 borrows
    v[n - 1] = 123;   (split)           fn main — 1 region,  1 borrow
                                          borrow #1 from 'v' to '' is_mut=1
    v[v.len() - 1] = 123;  (the row)    fn main — 1 region,  1 borrow
                                          borrow #1 from 'v' to '' is_mut=1
    let r = &v; v[0] = 5; r.len();      fn main — 2 regions, 2 borrows  → REFUSES
                                          #1 'v'→'r#1' is_mut=0, #2 'v'→'' is_mut=1

The MUT half is already minted: the `index_mut` receiver IS a recorded
`BorrowSite`, at the right point, and when the shared half exists the
inferencer finds the conflict and reports it
("cannot borrow 'v' as mutable: shared borrow still in scope here"). What is
missing is the SHARED half: **a `&self` METHOD RECEIVER mints no BorrowSite at
all** — `let n = v.len();` produces zero borrows. Rule 16's shape exactly, and
only the minting site can tell "no fact recorded" from "the fact is absent".

That is a PROVEN-LIVE SITE for the index-self-borrow rows, on a channel every
previous probe (all of them in `take_borrow_whole_`'s loan tables) was blind
to. It is not priced — a shared BorrowSite per `&self` receiver is a large
population and its cost is the whole question — but it is a site, which is
what four rounds asked for.

## 5. RULE 5 — THE HAND PROGRAMS, ALL MULTI-LINE, ALL ON THE ARMED BINARY

    a1  double_access(&mut a, &a)            argresvact rc 1  ✓ (the row's twin)
    a2  take(&mut a, len3(&a))               argresvact rc 0  ✓ (the stdlib's shape,
                                                                 survives)
    i2  take2(&mut v, &v)   on a Vec         argresvact rc 1  ✓ (AddrOf on a Vec
                                                                 records fine)
    i3  let r=&v; v[0]=5; r.len()            base       rc 1  ← RegionInferer
    i4  let n = v.len();                     0 borrows        ← the missing mint
    idx let n=v.len(); v[n-1]=123;           1 borrow, is_mut=1

## 6. `loan_carrying_type` vs `is_borrow_carrying_type` — SETTLED BY READING,
##      FIFTH ROUND, NO PROBE SPENT

They are not two names for one question, and the tree says so in three lines:

    ts.loan_carrying = ts.borrow_carrying;      // seed
    …fixpoint that only INSERTS, with holds_residency_holder NOT skipped…

so `borrow_carrying ⊆ loan_carrying` as sets, and the two type predicates run
the SAME recursion over them — except that `bc_is_borrow_carrying_type` has a
`residency_exempt` early `return false` that the loan version does not.
Therefore `is_borrow_carrying_type(t) ⇒ loan_carrying_type(t)`, never the
converse. The questions differ on purpose: an Rc/Arc share says the arena is
ALIVE (escape), not that it is being READ (loan) — D1 round 2 measured that
with four named programs (c4/c2/X2/X3) and the loan channel exists because of
it.

⚠ THE ONE CONSEQUENCE WORTH RECORDING: the implication makes the second
conjunct DEAD at both sites that spell it —

    borrow_check.cpp:4744  if (!is_ref_kind(st) && !loan_carrying_type(st) &&
                               !is_borrow_carrying_type(st)) continue;
    borrow_check.cpp:5565  if (!is_borrow_carrying_type(st) &&
                               !loan_carrying_type(st)) return;

Redundancy, not a defect. Derived by READING the construction, not measured;
what would falsify it is a name reaching `borrow_carrying` after the seed copy,
and there is none — the copy is the last statement of the bc fixpoint.

## ⇒ 7. WHAT DESERVES FUNDING, IN ORDER

 1. **`capsharedlive` — LAND IT.** ceiling 4 / cost 0 / cfail 1 text-only /
    stdlib ok, four rows with four correct diagnostics, predicted set = measured
    set both ways. The landing owes two rule-14 duplicates (§3).
 2. **The `&self` receiver's missing `BorrowSite` in `RegionInferer`** (§4) —
    a proven-live site, unpriced, reaching the two `v[v.len()-1]` rows and
    plausibly more. Price the MINT alone before anything built on it (rule 13).
 3. **`bck.D`'s three E0716 rows** (§0) — the root's largest group, never
    priced, never named in this file.
 4. **`argresvact`** stays UNFUNDED but is no longer a dead end: its only cost
    is a fixture that pins the very divergence the row records (§2). It needs a
    corpus decision, not another spelling.

## 8. OPEN

 1. `tpb-mut-with-shared-ref-arg` pins `f(&mut x, &x)` as legal; the ledger
    books its twin as a hole. One of the two is wrong and this round did not
    decide which.
 2. `d-index-two-phase` is unbuyable while `btvec_append` keeps its shape (§4).
 3. `a-fnmut-twice` is still touched by nothing.
 4. The two duplicated E0506s and the added line under `capsharedlive` (§3).
 5. bck.D's E0716 three (§0), unpriced.
 6. `test-levels.sh` run from the SOURCE ROOT prints CMake errors for every
    tier and then `0/0 tests passed … PLUS: the gates tier FAILED` — a red
    summary that means "wrong cwd", not a red tree. Met this round; run it
    from `build/`.

# ═══ ROUND 2026-08-31t — THE WHOLE-VALUE SHARED CAPTURE BECOMES A LOAN (4 ROWS),
# AND TWO DUPLICATE DIAGNOSTICS DIE WITH IT ══════════════════════════════════

## 0. THE CENSUS FIRST (RULE 17), AND THE BRIEF WAS WRONG ABOUT ONE THING

    grep -rhoP 'probe::on\("\K[a-z_0-9]+' src include | sort -u | wc -l   → 132
    grep '^# TOTAL' tests/logos/bc_admits.ledger                          → 262
    non-blank non-# rows                                                  → 262
    tests/imported/admit/*/*.logos                                        → 262
    distinct roots                                                        → 71
    top: bck.C 17 · lifereg.A 13 · nllmoves.C 12 · nllmoves.B 11 ·
         lifereg.R17 11 · bck.B 11 · bck.NEW 10 · lifereg.R18 8 ·
         lifereg.NEW-N1 8 · lifereg.C 8 · bck.D 8

Opening gate build 302 (logosc 0.42.0-preview+main-gd2b662bd-dirty.20260831T232430Z,
libs dc6cb11cbed9f192): `-R '^logos_00_bc_admit_'` 262/262 already measured,
`-L bc` 2005/2005 already measured, 2457 recorded / 0 failed / 2 disabled.

⚠ **CORRECTION TO THE BRIEF (and to 2026-08-31s §3(a)): the two-wording E0506
DUPLICATE IS NOT capsharedlive's.** It is pre-existing and unconditional —
`let mut x = 5; let r = &x; x = 7;` prints BOTH lines on the UNARMED
2026-08-31s binary. Measured before any edit. The brief booked it as a debit
the landing owes; it is a defect the landing is merely the first program to
reach through a closure. It is deleted below, as its own priced mechanism.

## 1. M1 — capsharedlive, LANDED. 4 ROWS.

A whole-value SHARED closure capture was a LIVENESS question only
(`check_live(root)`); it now records a shared LOAN on the root held by the
closure binding — **unless the root is MOVED**, which IS a liveness question
and keeps its own verb. That guard is the whole difference from `capshared`,
whose four `.expected` losses were all a liveness answer coming back out of
the loan arm.

Re-priced on today's binary BEFORE funding (rule 8), build dc6cb11cbed9f192:

    fires 64 · CEILING 4 · COST 0 pass · cfail 1 TEXT-ONLY · stdlib 4/4

PREDICTED BY NAME in `build/predictions-2026-08-31t.txt`, written before the
edit. MEASURED after the landing (gate build 306): **closed set = predicted
set, both directions empty** —

    logos_00_bc_admit_borrowck_borrowck-closures-mut-and-imm        bck.C
    logos_00_bc_admit_nll_closure-borrow-spans--a                   nllmoves.C
    logos_00_bc_admit_nll_closure-borrow-spans--b                   nllmoves.C
    logos_00_bc_admit_regions_region-bound-on-closure-outlives-call lifereg.C

Every diagnostic READ on the landed binary, against the fixture's own recorded
upstream error, and each names the CALLER's own variable in the CALLER's fn:

    borrowck-closures-mut-and-imm   E0506  error [fn main]: cannot assign to
                                           'x' because it is borrowed
    closure-borrow-spans--a         E0502  error [fn main]: cannot borrow 'x'
                                           as mutable: 1 shared borrow(s) active
    closure-borrow-spans--b         E0506  error [fn main]: cannot assign to
                                           'x' because it is borrowed
    region-bound-on-closure-…       E0505  error [fn call_rec]: cannot move 'f'
                                           while it is borrowed

⚠ WHERE THE FIX DIFFERS FROM ITS PROBE: nowhere in the arm — the landed
predicate is the probe's, verbatim minus the env gate — but the two E0506 rows
print ONE line where the probe printed two, because M2 landed with it.

## 2. M2 — assigndupdel, LANDED. 0 ROWS, 22 DUPLICATE LINES DELETED.

`Code::Assign` asked `shared_borrows > 0` and reported, then called
`field_borrow_conflicts(need_exclusive=true)`, whose FIRST arm asks
`shared_borrows > 0` and reports again. Two readers, one question, two
wordings. The second is skipped when the first spoke; nothing else in
`field_borrow_conflicts` is reachable in that state, because that arm returns
true before it looks at either path map.

    build 0b9b6d1db8253980: fires 25 · CEILING 0 · COST 0 pass · stdlib 4/4
    COST-fail 22 of 1098 — rc 0, .expected-match 0, TEXT-ONLY 22

**All 22 diffs READ, by re-running each fixture armed and unarmed and diffing
the lines** (not by trusting the sha): every one is a single deletion of
`cannot assign to 'X' while 'X' is borrowed`, with
`cannot assign to 'X' because it is borrowed` still standing above it. Nothing
was added anywhere. This is rule 14's named exception — a branch that DELETES
a duplicate — and it is the only reason a 0-row / 22-text change is funded.

## 3. M3 — capsharedimp, LANDED. 0 ROWS, THE ONE ADDED LINE REMOVED.

M1's single cfail debit was an ADDED second line on
`fail/closure-access-spans--a-closure-imm-capture-conflict`: the capture's new
loan CONFLICTS at the record (the root is already `&mut`-borrowed) and
`record_borrow` restated a conflict the body walk had already reported.
`RecordFlags::implicit` — which exists for exactly this, a compiler-raised loan
that must not report at its own record site — drops it silently.

    build 0b9b6d1db8253980: fires 71 · COST 0 pass · stdlib 4/4
    COST-fail 1 of 1098, TEXT-ONLY, and it is the DELETION of M1's added line

CEILING 0 is the expected reading here and NOT a refutation: the four rows were
already closed by M1 in the binary it was priced on. What had to be checked
instead is that it does not RE-OPEN them, and that was checked directly — all
four still refuse, with the same four diagnostics, under `LOGOS_PROBE=capsharedimp`.
The silencing is bounded to the loan M1 adds (`cap_shared_loan`), and the moved
root never reaches it.

## 4. RULE 13 — ADDITIVITY CHECKED, AND IT IS NOT ADDITIVE

Fail-text oracle, 1098 fixtures, all four states measured:

    M1 alone       vs pre-round binary   1 changed  (0 rc, 0 match, 1 text)
    M2 alone       on top of M1         22 changed
    M3 alone       on top of M1          1 changed
    ALL THREE      vs pre-round binary  22 changed  (0 rc, 0 match, 22 text)
    union of the parts                  23
    ALL − union = ∅ · union − ALL = {closure-access-spans--a-…}

M1's only debit is CANCELLED by M3, so the round's whole text footprint against
the pre-round compiler is 22 duplicate-line deletions and nothing else. The
increment is NEGATIVE, exactly as rule 13 warns, and the set — not the count —
is what says so.

## 5. RULE 5 — THE HAND PROGRAMS, MULTI-LINE, ON THE ARMED THEN THE LANDED BINARY

Legal, must compile (each proven to reach the arm by `LOGOS_PROBE_FIRE`):

    ce2  capture dead before the write (NLL)                    fires 1  rc 0
    ce3  two closures sharing one root, both live               fires 2  rc 0
    ce4  `&x` taken while the capture is live                   fires 1  rc 0
    ce5  the capture remade each loop iteration                 fires 2  rc 0
    ce6  `move` capture                                    fires 0, DELIBERATELY
         — the `move` arm returns BEFORE this branch; a zero here is the
         mechanism's boundary, and it is why (6) is in the pass fixture.

Illegal, must refuse (all THREE compiled rc 0 on the pre-fix binary — that is
the control revert, run before the fixtures were believed):

    p1   assign to x under a live shared capture   E0506, one line
    p2   `&mut x` under a live shared capture      E0502
    u2   capture under a live `&mut`, closure called later — the check_live line
         survives, the capture's duplicate is gone (M3)

⚠ AND ONE PERMISSIVE SHAPE MEASURED, NOT ARGUED: `u1`, whose closure body takes
`&x` rather than reading `x`, still refuses under M3 — the body's own AddrOf
records the loan, so M3's silence at the capture is not an un-refusal there.
What M3 CANNOT promise is that some shape exists where the capture record is
the only reader; the 1098-fixture oracle shows 0 rc flips and 0 `.expected`
losses, and that is the bound, not a proof.

## 6. THE FIXTURES, AND WHAT RATCHETS THE MOVED-ROOT GUARD

The four rows move to `fail` IN THEIR OWN ROOT'S HOME
(`tests/imported/fail/{borrowck,nll,nll,regions}/`), each with its full
diagnostic pinned. The native pair, ONE TOKEN apart:

    tests/logos/pass/bc_capsharedloan_legal_shapes.logos   (7 programs)
    tests/logos/fail/bc_capsharedloan_assign_under_capture_fail.logos

— case (2) writes `y` in the pass half and `x` in the fail half, and that token
is the whole mechanism.

⚠ THE MOVED-ROOT GUARD NEEDS NO NEW FIXTURE, AND HERE IS WHY: it is already
ratcheted by FOUR existing pins. `capshared` without the guard LOSES the
`.expected` of arc-consumed-in-looped-closure, borrowck-in-static--r-runtime,
borrowck-in-static--move-captured-out-of-fn-closure and nll/closure-move-spans
(measured, 2026-08-31s §3). Deleting the guard turns those four RED by
`.expected` mismatch, not by rc — so the ratchet is real and it is on the text.

## 7. WHAT IS LEFT OPEN

 1. `bck.D`'s **three E0716 temporary-lifetime rows** (borrowck-borrowed-uniq-rvalue,
    -2, issue-36082) — that root's largest group, still never priced, still not
    named anywhere but the previous round's census.
 2. The `&self` receiver's missing `BorrowSite` in `RegionInferer` (2026-08-31s §4)
    — proven-live, unpriced. Nothing in this round touched it.
 3. `d-index-two-phase` stays unbuyable while `btvec_append` keeps the
    `f(&mut t, g(&t))` shape; `argresvact` still needs a corpus decision about
    `tpb-mut-with-shared-ref-arg`, not another spelling.
 4. `a-fnmut-twice` is still touched by nothing.
 5. ⚠ NOTHING IN THE HARNESS ASSERTS THE ABSENCE OF A DUPLICATE LINE. A
    `.expected` is a `grep -F` SUBSTRING, so M2's 22 deletions are invisible to
    ctest in BOTH directions — if the second reader comes back, every one of
    those fixtures stays green. The text oracle is the only witness, and it has
    no stored baseline gate. That is a missing sensor, not a missing test.

# ═══ ROUND 2026-09-01 — THE E0716 PARTITION IS THREE MIS-PORTS AND THE ENGINE
# IS ALREADY RIGHT ON ALL THREE; THE SHARED `&self` MINT BUYS ITS TWO ROWS, AND
# THE TWO NAMES SEPARATE ON A LEGAL PROGRAM THE CORPUS DOES NOT CONTAIN ════════

## 0. THE CENSUS FIRST (RULE 17), AND THE BRIEF WAS WRONG ABOUT FIVE THINGS

    grep -rhoP 'probe::on\("\K[a-z_0-9]+' src include | sort -u | wc -l   → 130
    grep '^# TOTAL' tests/logos/bc_admits.ledger                          → 258
    non-blank non-# rows                                                  → 258
    tests/imported/admit/*/*.logos                                        → 258
    distinct roots                                                        → 71
    top: bck.C 16 · lifereg.A 13 · nllmoves.B 11 · lifereg.R17 11 ·
         bck.B 11 · nllmoves.C 10 · bck.NEW 9 · lifereg.R18 8 ·
         lifereg.NEW-N1 8 · bck.D 8

Opening build **9027a90a26766078**, tree clean at `2965dd9fc`. (262 → 258 and
132 → 130 are last round's four landings; bck.C 17→16, nllmoves.C 12→10,
bck.NEW 10→9 are where they came from.)

**CORRECTION 1 — THE BRIEF'S THIRD SUBJECT IS ALREADY CLOSED.** It asks a
landing round to pay two diagnostic debits: the two-wording E0506 duplicate and
the added `.expected`-invisible line. Both died last round (M2 `assigndupdel`,
M3 `capsharedimp`). READ on today's binary, one line each:

    borrowck-closures-mut-and-imm                     1 line
    nll/closure-borrow-spans--b                       1 line
    nll/closure-access-spans--a-closure-imm-capture…  1 line
    let mut x=5; let r=&x; x=7;                       1 line

**CORRECTION 2 — THE E0716 ROW-TO-SHAPE MAP IS SWAPPED**, here and in
2026-08-31s §0. Read from the files:

    borrowck-borrowed-uniq-rvalue     `let r: &i64 = &mk().v;`   (FIELD of a call)
    borrowck-borrowed-uniq-rvalue-2   `let d: &String = &mk();`  (the call ITSELF)
    issue-36082                       `let val: &i64 = &mk().a;` (FIELD of a call)

**CORRECTION 3 — "no PROBES.md line" is false.** They are named in 2026-08-31s
§0 (CORRECTION 1), §7.3 and §8.5, and in 2026-08-31t §7.1. NEVER PRICED is
true; never named is not.

**CORRECTION 4 — the `&self` three-program table's first row.** The brief prints
`let n = v.len(); v[n-1] = 123;  →  0 regions`. MEASURED, `LOGOS_DUMP_REGIONS`:

    let n = v.len(); v[n-1] = 123;   1 region, borrow #1 from 'v' to '' is_mut=1
    v[v.len()-1] = 123;              1 region, borrow #1 from 'v' to '' is_mut=1
    let r=&v; v[0]=5; r.len();       2 regions → REFUSES

The 0-region program is the BARE `let n = v.len();` with no index write (that
is what 2026-08-31s §4 measured). The legal split and the illegal row mint the
SAME single mut borrow — the two differ only in WHICH STATEMENT it sits in, and
that is the whole reason NLL liveness can separate them.

**CORRECTION 5 — root `bck.D` is "a TEMPORARY has no owner to key a loan on",
and TWO of its eight rows are not about temporaries at all.** The two index
rows are a `RegionInferer` mint gap (§2 below, and 2026-08-31s §4); they are
filed under D by symptom, not by cause.

## 1. THE E0716 PARTITION — THE MACHINERY EXISTS, IT IS CORRECT, AND THE THREE
##      ROWS ARE MIS-PORTS. NO PROBE WAS SPENT AND NONE SHOULD BE.

**WHERE THE TEMPORARY'S LIFETIME IS DECIDED**: `prov_of`'s `AddrOfTemp` arm
(borrow_check.cpp), which answers `{is_local=true, is_temp=false}` for a direct
`&<rvalue>` and says why in its own comment — Rust EXTENDS a temporary borrowed
directly in a `let` initializer. **WHO RECORDS IT**: `RefProv::is_temp`, and the
`let` gate reports on `is_temp` ALONE:

    borrow_check.cpp, the Let arm:  if (vp.is_temp) report(… "temporary value
    dropped while borrowed: … bind the owning value to a variable first …")

So the channel is not missing; it is DELIBERATELY SILENT on the extended shape.
Rust's rule (the Reference, temporary lifetime extension): *if a borrow,
dereference, field, or tuple-indexing expression has an extended temporary scope
then so does its operand* — which extends `&mk()` AND `&mk().v` AND `&mk().a`.
**All three rows are legal Rust.** Closing them is a legal-program refusal.

THE CORPUS ITSELF SAYS SO, in a fixture nobody had connected to these rows:
`tests/imported/fail/borrowck/borrowck-borrow-from-temporary` is PINNED with
`let r: &Foo = &id(Foo{v:3}); return &r.v;` and its `.expected` is the RETURN
error. It only reaches the return because the `let` extended. A fix that made
the `let` refuse would change that pin's text.

MEASURED, on 9027a90a26766078, hand programs multi-line, one variable apart:

    e716_a  let r = &mk().v;                        rc 0   ← the row, legal
    e716_b  fn esc() -> &i64 { return &mk().v; }    rc 1   "reference to temporary value"
    e716_c  fn esc() -> &B   { return &mk(); }      rc 1   "reference to temporary value"

so the escape fact IS recorded for both shapes; only the LET consumer, correctly,
does not report on it.

⚠ **AND THE SAME THREE PROGRAMS FOUND TWO REAL E0716 HOLES WITH NO LEDGER ROW,
one token from fixtures that are already pinned red:**

    e716_d  keep(&mk().v)          rc 0  ADMITTED   ← HOLE, the FIELD hop
    e716_e  keep(&id(5))           rc 1  (= the pinned issue-11493)
    e716_f  keep(&mk())            rc 1  (= the pinned rvalue-borrow-scope-error)
    e716_g  mk().view()            rc 0  ADMITTED   ← HOLE, no `impl Drop`
    e716_h  mk().view() + Drop     rc 1  (= the pinned spec borrow_diag_2)

`e716_g`/`e716_h` differ ONLY in `impl Drop for B`: the temp fact rides on
`materialize_recv_ref`'s `__rtmp_N`, which sema hoists only for a DROPPABLE
rvalue receiver. A non-droppable temporary receiver dangles silently.

## 2. THE PROBE TABLE — build **905fb1a7bbc0ded7** (READ), gate-db 311 → 315

L1 rc 0 with nothing armed (`probe-batch` proved the batch inert before pricing).

    probe          fires  ceiling  cost  cfail       stdlib  verdict
    rgrecvshared   13355        2     0    1 (text)      ok  ⛔ see §3
    rgrecvnest      1298        2     0        0        ok  ✓ FUNDABLE
    e716fldarg      1001        0     0        0        ok  0 rows, 0 cost, 1 hole
    e716recvtmp     1306        0     0        0        ⛔  STOP — stdlib mem

PREDICTED BY NAME in `build/predictions-2026-09-01.txt`, written before the
edits. MEASURED: for BOTH region probes the closed set EQUALS the predicted set,
**both directions empty** —

    logos_00_bc_admit_borrowck_suggest-local-var-for-vector          bck.D
    logos_00_bc_admit_borrowck_suggest-storing-local-var-for-vector  bck.D

and the two E0716 probes' predicted CEILING 0 was measured, at fires 1001 / 1306
— live sites, not unreached ones. First round in four where nothing arrived from
another root; the reason is that the mechanism is keyed on a SYNTACTIC nesting
that only these two rows have.

DIAGNOSTIC READ on the armed binary, both rows, against upstream's E0502
("cannot borrow `v` as immutable because it is also borrowed as mutable"):

    error [fn main]: cannot borrow 'v' as shared: mutable borrow still in
    scope here (first borrowed at line 8, conflicting use at line 8)

## 3. ⚠ RULE 9 AGAIN, AND THIS TIME THE TWO NAMES SEPARATE ON A PROGRAM THE
##      CORPUS DOES NOT CONTAIN — COST 0 IS NOT A SAFETY CLAIM

`RegionInferer::walk_stmt`'s `MethodCall` arm walks the receiver and mints
NOTHING; `AddrOf`/`AddrOfTemp` are the only mints. The SITE is "mint the shared
half of a `&self` receiver"; the INNER PREDICATE is *which* receivers.

    rgrecvshared  every plain-VarRef receiver
    rgrecvnest    only when `in_call_args_depth > 0`

Both price CEILING 2 / COST 0 / stdlib ok, and on the corpus alone
`rgrecvshared` looks like the better buy (it is strictly wider). **IT REFUSES
TWO LEGAL PROGRAMS, BOTH HAND-WRITTEN, NEITHER IN ANY POPULATION THIS HARNESS
OWNS:**

    n1  let n: i64 = v.len();  v[n - 1] = 123;   base 0 · nest 0 · shared 1
        error: cannot borrow 'v' as mutable: shared borrow still in scope here
    n7  v[0] = v.len();                          base 0 · nest 0 · shared 1
        error: cannot borrow 'v' as shared: mutable borrow still in scope here

MECHANISM, and it is the whole difference between the two names: a top-level
mint takes the statement's `holder` — the LET's bound name — so the shared
region lives as long as `n` does, and `n` is an `i64` that carries no borrow at
all. At `in_call_args_depth > 0` the holder is always `""`, the region is
point-scoped, and NLL liveness does the rest. The corpus never noticed because
the split-into-a-local spelling is what the ledger rows are the SUGGESTED FIX
FOR — nobody wrote it down as a pass fixture.

`rgrecvshared`'s one cfail is the same defect seen from the fail side: a THIRD
line added to `nll/loan-ends-mid-block-vec` on `data.push(4)`, `.expected` still
matching (rule 15's shape). ⚠ That fixture ALREADY prints TWO lines unarmed —
inherited, not created (rule 14's new clause, checked on the unarmed binary).

RULE 5 — the legal shapes tried against `rgrecvnest`, all multi-line, all rc 0
under it and under base:

    n2  v.push(v.len())                     n3  let x = v[v.len()-1];
    n4  take(&mut v, v.len())               n5  p.b = p.geta();
    n6  while i < v.len() { v[i] = v[i]+1 } n7  v[0] = v.len();
    n1  let n = v.len(); v[n-1] = 123;

Ten programs, no counter-example found for `rgrecvnest`. That is a bound, not a
proof: what it cannot promise is a legal shape where a `&self` receiver is
NESTED in a call/index argument whose target is mutably borrowed at the same
point and the program is still legal. Two-phase covers the argument case
(`n4` survives via `is_tpb_reservation`); the index case is exactly the row.

## 4. e716fldarg — 0 ROWS, 0 COST, AND IT CLOSES ONE OF THE TWO HOLES. HALF.

`merge_arg_prov`'s `is_temporary_value_expr(EAddrOfTempView{a}.inner())` asks
the question of the PROJECTION, not of its base, so `keep(&mk().v)` walks past a
door `keep(&mk())` is refused at. Peeling FieldRead/TupleIndex/AddrOfTemp first:

    fires 1001 · CEILING 0 · COST 0 pass · cfail 0 of 1103 · stdlib 4/4
    e716_d  keep(&mk().v)            base rc 0 → armed rc 1   ✓ closed
    e716_i  keep(&mk().v) + Drop     base rc 0 → armed rc 0   ✗ STILL OPEN

⚠ **RULE 2 — DOORS IN SERIES, AND THE PROBE ONLY OPENED THE FIRST.** The
droppable half is the DANGEROUS one (a destructor runs on the storage the
reference points into) and it is the half that survives: with `impl Drop`, sema
materialises `__rtmp_N`, the argument becomes a borrow of a NAMED local, and
`is_temporary_value_expr` is the wrong question about it — the right one is
`is_materialized_temp_name`, which the `AddrOf` arm asks and this path never
reaches. A landing needs BOTH, and only the first is priced.

## 5. e716recvtmp — ⛔ THE STDLIB, AND THE WALL HAS A NUMBER

Same peel at the MethodCall arm's `is_temporary_value_expr(v.receiver())`
(the receiver of `mk().view()` is `AddrOfTemp(Call)`, so the existing rule
never sees the call):

    fires 1306 · CEILING 0 · COST 0 pass · cfail 0 · stdlib ⛔ mem, 1 refusal
    error [fn resolve]: temporary value dropped while borrowed: …

It closes `e716_g` and it also REPAIRS a diagnostic: `return mk().view();`
printed `cannot return reference to local variable '?'` — a name in no source
file — and prints `cannot return reference to temporary value` under the arm.
Two legal receiver spellings were checked and survive (`b.view()` on a named
local; `s.len()` / `s.as_bytes()` on a `str`), so the mem refusal is narrower
than "every autoref'd receiver". ⚠ AND THE DIAGNOSTIC CARRIES NO FILE OR LINE
in a `--emit-module` build — `fn resolve` is all there is, and the mem layer has
six of them. That is a second finding: the E0716 report site has no location
in a module compile.

## ⇒ 6. WHAT DESERVES FUNDING, IN ORDER

 1. **`rgrecvnest` — LAND IT.** ceiling 2 / cost 0 / cfail 0 / stdlib ok,
    predicted set = measured set both ways, both diagnostics correct and in
    upstream's direction, ten legal programs unmoved. It is the FIRST thing in
    four rounds to reach the two index rows, and it reaches them in the channel
    2026-08-31s §4 proved was the only one that can see them.
 2. **`e716fldarg` + the `__rtmp` half** — 0 rows but 0 cost, and together they
    close a real dangling-reference hole one token from two pinned fixtures.
    Rows are not the only currency; this one is a soundness fix with a price of
    zero and a fixture that must be authored, not harvested.
 3. **`rgrecvshared` — DO NOT LAND**, and record why: the holder. A top-level
    receiver mint needs the region to die at the end of its statement unless the
    holder is itself borrow-carrying, and nothing in `RegionInferer` knows that.
 4. **THE THREE E0716 ROWS SHOULD BE RETIRED FROM THE LEDGER, not bought.** They
    are ports of E0716 tests into the one syntactic position where Rust extends.
    That is a CORPUS DECISION WITH AN OWNER — recorded here, not acted on.

## 7. OPEN

 1. The three E0716 rows (§1) — a corpus decision, not a probe result.
 2. `e716_i`: `keep(&mk().v)` with a Drop impl, still admitted (§4).
 3. `e716recvtmp`'s mem refusal has no file/line (§5).
 4. `two-phase-nonrecv-autoref--c` vs the pass fixture `tpb-mut-with-shared-ref-arg`
    — unchanged, still one program booked twice (2026-08-31s §2).
 5. `d-index-two-phase` unbuyable while `btvec_append` keeps `f(&mut t, g(&t))`.
 6. `a-fnmut-twice` is still touched by nothing.
 7. Still no sensor asserting the ABSENCE of a duplicate line (2026-08-31t §7.5).

---

# ROUND 2026-08-31u — THE SHARED HALF OF A `&self` RECEIVER LANDS (2 ROWS), AND BOTH E0716 ARGUMENT DOORS CLOSE AT ZERO COST

Opening gate build 311 (logosc 0.42.0-preview+main-gf262954b, libs
905fb1a7bbc0ded7): `-R '^logos_00_bc_admit_'` 258/258 and `-L bc` 2011/2011
ALREADY MEASURED, 5853 recorded / 0 failed / 2 disabled — the store, not a
re-run. Probe build 065cb63432ca02df (batch of two, L1 rc 0, batch inert).
Landed build 81e822c31f58e580.

## 0. CENSUS, AND WHAT THE BRIEF GOT WRONG

    live probes  134 (opening)  = 134 (landed: two added, two consumed)
    ledger       # TOTAL 258 -> 256, re-derived by direct listing (256 rows)
    top roots    bck.C 16 · lifereg.A 13 · nllmoves.B 11 · lifereg.R17 11 ·
                 bck.B 11 · nllmoves.C 10 · bck.NEW 9 · lifereg.R18 8 ·
                 lifereg.NEW-N1 8 · bck.D 8 -> 6

**CORRECTION 1 — the brief's subject 3 ("two diagnostic defects the last landing
owes") IS ALREADY DISCHARGED, and the handed report says so too.** Verified
DIRECTLY on the pre-round binary, not inferred: `let mut x = 5; let r = &x;
x = 7;` prints ONE line; `borrowck-closures-mut-and-imm` and
`closure-borrow-spans--b` print one E0506 each;
`closure-access-spans--a-closure-imm-capture-conflict` prints one line. The
duplicate was PRE-EXISTING and unconditional (2026-08-31t §2 measured it on the
UNARMED pre-round binary — INHERITED, not created by `capsharedlive`) and M2
deleted it; the added line was M3's. Nothing was owed and nothing was repaired.

**CORRECTION 2 — the brief's `rgrecvshared` three-program table is wrong in its
first row**, as the handed report already said: `let n = v.len(); v[n-1] = 123;`
is 1 region / 1 borrow / is_mut=1, identical to the illegal row. Re-measured
here only as a legality question, and the answer is what matters: it is one of
the two programs `rgrecvshared` REFUSES.

## 1. M1 — `rgrecvnest`, LANDED. 2 ROWS. 258 -> 256

`RegionInferer::walk_expr`'s MethodCall arm mints a SHARED BorrowSite for a
plain-VarRef receiver when `in_call_args_depth > 0`. The mut half was already
minted at the right point and already found the conflict whenever a shared half
existed; the shared half did not exist.

    site: src/compiler/region_infer.cpp::walk_expr (MethodCall arm)
    build 065cb63432ca02df: fires 1298 · CEILING 2 · COST 0 pass · cfail 0 · stdlib ok

PREDICTED BY NAME in `build/predictions-2026-08-31u.txt`, written before the
edit. MEASURED after the landing (gate build 319, 256 passed / 2 failed):
**closed set = predicted set, BOTH DIRECTIONS EMPTY** —

    logos_00_bc_admit_borrowck_suggest-local-var-for-vector          bck.D
    logos_00_bc_admit_borrowck_suggest-storing-local-var-for-vector  bck.D

Diagnostic READ on both, upstream E0502's own direction:
`cannot borrow 'v' as shared: mutable borrow still in scope here`.

⚠ **RULE 9 — THE TWO NAMES, AND THE WIDER ONE IS NOT THE BIGGER BUY.**
`rgrecvshared` (the same mint at depth 0) closes THE SAME TWO ROWS and refuses
two legal programs the corpus does not contain:

    let n: i64 = v.len();  v[n - 1i64] = 123i64;   the rewrite upstream SUGGESTS
    v[0i64] = v.len();                             Rust evaluates the RHS first

The mechanism is the HOLDER: a top-level mint takes the statement's holder — for
a `let` the bound name, an `i64` carrying no borrow — so the shared region
outlives the statement. At depth > 0 the holder is `""` and the region is
point-scoped. It stays a probe, and the reason is now in the ledger note.

## 2. M2 + M3 — `e716fldarg` + `e716rtmparg`, LANDED. 0 ROWS, TWO HOLES

`merge_arg_prov` asked `is_temporary_value_expr` of the `AddrOfTemp`'s inner
expression only. It now peels FieldRead / TupleIndex / AddrOfTemp hops, and
accepts a peeled base that is a materialized `__rtmp_N` local — the predicate
the `AddrOf` arm has always asked.

    site: src/compiler/borrow_check.cpp::merge_arg_prov
    e716fldarg   build 905fb1a7bbc0ded7: fires 1001 · CEILING 0 · COST 0 · cfail 0 · stdlib ok
    e716rtmparg  build 065cb63432ca02df: fires 1001 · CEILING 0 · COST 0 · cfail 0 · stdlib ok

⚠ **RULE 2, PAID: THE DOORS WERE IN SERIES AND THE SECOND WAS THE DANGEROUS
ONE.** `keep(&mk().v)` was admitted; the SAME program with `impl Drop` was also
admitted, and the peel alone does not reach it — sema materialises `__rtmp_N`,
so the argument is a borrow of a NAMED LOCAL and `is_temporary_value_expr` is
false by construction. Both halves are now landed and both are pinned, one token
apart (`tests/logos/fail/bc_e716argtemp_field_hop_fail` and
`.../bc_e716argtemp_rtmp_drop_fail`, differing in the `impl Drop` line).

    e716_d  keep(&mk().v)                rc 0 -> 1   E0716
    e716_i  keep(&mk().v) with impl Drop rc 0 -> 1   E0716

## 3. RULE 5 — THE HAND PROGRAMS, MULTI-LINE, PROVEN LIVE BEFORE THEY WERE TRUSTED

Eleven legal programs for M1, each compiled under `LOGOS_PROBE_FIRE` on the
armed binary and each recording ≥1 fire at the new mint, all rc 0 armed and
landed: `v.push(v.len())`, `v.set(v.len()-1,7)`, `v.swap(0,v.len()-1)`,
`sum2(v.len(),v.len())`, `v.push(w.len())`, `v.get(v.len()-1)`,
`v.remove(v.len()-1)`, `let x = v[v.len()-1]` (the SHARED read — one token from
the row), `v[w.len()-1]=42`, `while n < take2(v.len(),0)`,
`v.borrow(v.len()-1)`. THREE more fire ZERO and are recorded as boundaries, not
counter-examples: `let n = v.len(); v[n-1]=123`, `v[0]=v.len()`,
`p.setx(p.getx()+1)` — all at depth 0.

Seven legal programs for M2/M3, all rc 0 on the landed binary, each sharing the
`&<temp>.<field>` argument spelling with a refuse half so the arm is reached:
`let r = &mk().v` (extension), `*keep(&mk().v)` (consumed IN the statement),
`take(&mk().v)` (callee returns a value), `take(&mkt().v.0)` (tuple hop),
`keep(&b.v)` / `keep(&d.v)` (named base, droppable named base),
`take(&mkd().v)` (a materialized temp consumed in its own statement).

## 4. RULE 13 — ADDITIVITY MEASURED, AND HERE IT HOLDS AT ZERO

`scripts/fail_text_oracle.py`, 1103 `-L bc -L fail` fixtures, landed
(81e822c31f58e580) against the pre-round unarmed binary (065cb63432ca02df):

    rc flips 0 · .expected losses 0 · TEXT-ONLY 0 · population identical

Each mechanism priced cfail 0 alone and the union is 0. That was PREDICTED in
`build/predictions-2026-08-31u.txt` rather than assumed, which is the only form
of the claim rule 13 allows. `-L bc` 2009 passed / 0 failed / 2 disabled.
`stdlib-cost.sh` unarmed: all four layers. Full `cmake --build`: green.

⚠ CONTROL REVERT, run BEFORE the fixtures were believed: both `.cpp` files
reverted to HEAD, full rebuild, the seven new tests re-run. **All five
refuse-half fixtures RED** (they compile rc 0 without the landing) and both pass
fixtures GREEN — so the pass halves are legality pins that hold in both
directions, and every fail half is ratcheted by the mechanism it names.

## 5. ⛔ DECLINED BY NAME, WITH THE NUMBER THAT CONDEMNS IT

**`rgrecvshared`** — 2 legal programs refused (§1). Ceiling is the SAME 2 rows,
so the wider rule buys nothing and costs two.

**`e716recvtmp`** (peel Field/TupleIndex/AddrOfTemp at the MethodCall receiver)
— `logos.mem`, 1 refusal. **`e716recvdirect`**, installed this round to test
whether the FIELD HOP was to blame (peel AddrOfTemp ONLY): fires 1306, CEILING 0,
COST 0 pass, cfail 0, **stdlib ⛔ mem, THE SAME 1 refusal**. So the hop is NOT
the cause and narrowing along that axis is refuted.

**THE WALL, NAMED SO THE NEXT ROUND DOES NOT RE-DISCOVER IT.** The refusing
statement is `let segs_any: WAny = prog.segs.any();` in
`stdlib/mem/wql/srcloc.logos::resolve`. Localised by ONE-VARIABLE REBIND, not by
reading: rebinding `segs.seg_at(i-1).end` (the other candidate in the same fn)
left the count at 1; rebinding `prog.segs` took it to 0. `prog` is a Writ SCHEMA
view, so `prog.segs` is a generated ACCESSOR returning a `WRef<RQSrcSegArr>` BY
VALUE — a temporary — and `.any()` is a `&self` method on it. **The reference
does not dangle, because the temporary is a HANDLE into arena storage, not the
storage.** Any E0716 rule keyed on "the receiver is a temporary" alone refuses
`logos.mem`; the rule that could land needs the temporary to OWN its referent.
That is a TYPE question, and nothing at that site asks one today.

⚠ AND A SECOND FINDING, SEEN WHILE LOCALISING AND NOT INVESTIGATED: the rebind
`let sref: WRef<RQSrcSegArr> = prog.segs; sref.any()` does not COMPILE —
`mlir_gen` self-diagnoses "`let segs_any` initializer produced no value (method
call 'any' resolved to '') — statement DROPPED". A `WRef<T>` in a local binding
loses method resolution that the same expression keeps inline.

## 6. OPEN

 1. The three E0716 ledger rows (`borrowck-borrowed-uniq-rvalue`, `-2`,
    `issue-36082`) — legal Rust, ports into the one position Rust extends. A
    CORPUS DECISION WITH AN OWNER: they should be RETIRED, not bought. Unchanged
    from 2026-09-01 §1; still not acted on.
 2. `mk().view()` (`e716_g`) — a real dangling-reference hole with no ledger row,
    blocked by the wall in §5.
 3. The E0716 report carries NO FILE OR LINE in an `--emit-module` build; `fn
    resolve` was all four `resolve`s in `logos.mem` at once, which is why §5 cost
    two stdlib compiles instead of a glance.
 4. `WRef<T>` local binding loses method resolution (§5).
 5. `two-phase-nonrecv-autoref--c` vs the pass fixture `tpb-mut-with-shared-ref-arg`
    — one program booked twice. Unchanged.
 6. `d-index-two-phase` unbuyable while `btvec_append` keeps `f(&mut t, g(&t))`.
 7. `a-fnmut-twice` is still touched by nothing.
 8. Still no sensor asserting the ABSENCE of a duplicate line.

---

# ROUND 2026-09-01v — THE REGION SLOT LANDED AND NO COMPARATOR READS IT; AND THE E0716 RECEIVER WALL HAS TWO SITES, NOT ONE

Opening binary 52e163dd45fa5788 (HEAD `27703d904`). Probe build
**8b5e784d12776013** (batch of five, L1 rc 0 with nothing armed — tier_commit
305/305 — batch inert). gate-db builds 321 (unarmed) → 327/328 (armed pairs).
Predictions filed BEFORE any number was read in
`build/predictions-2026-09-01v.txt`.

## 0. CENSUS, AND FOUR CORRECTIONS TO THE BRIEF

    live probes  133 (opening)
    ledger       # TOTAL 256, re-derived by direct listing (256 rows)
    top roots    bck.C 16 · lifereg.A 13 · nllmoves.B 11 · lifereg.R17 11 ·
                 bck.B 11 · nllmoves.C 10 · bck.NEW 10 · lifereg.R18 8 ·
                 lifereg.NEW-N1 8 · lifereg.NEW-R20 7 · lifereg.N1 7 ·
                 lifereg.C 7

**CORRECTION 1 — THE BRIEF'S SUBJECT 1 IS STALE BY ONE ROUND.** It says
`e716fldarg` "closes the first" hole and that "the dangerous half [the `impl
Drop` / `__rtmp_N` path] is the one still open". BOTH halves landed in
2026-08-31u §2 and neither name is a probe any more —
`grep -rhoP 'probe::on\("\K[a-z_0-9]+'` finds neither. VERIFIED on the opening
binary, not inferred: `tests/logos/fail/bc_e716argtemp_field_hop_fail` and
`bc_e716argtemp_rtmp_drop_fail` both rc 1 with the E0716 text. There was no
half to fund.

**CORRECTION 2 — the brief asks to "find `e716recvtmp`'s wall shape" as if it
were unknown.** It is in 2026-08-31u §5, localised by one-variable rebind to
`let segs_any: WAny = prog.segs.any();` in `stdlib/mem/wql/srcloc.logos`.

**CORRECTION 3 — AND THAT RECORD'S OWN CHARACTERISATION IS WRONG.** §5 says the
rule needs "the temporary to OWN its referent … a TYPE question, and nothing at
that site asks one". Read the callee:
`pub fn any(self: &WRef<S>) -> WAny` in `stdlib/mem/wql/ir.logos` **returns BY
VALUE**. No reference is produced, so no ownership question arises — the site
already has the cheaper predicate (`is_ref_kind(m_rt)`, one line above the arm).
Measured below: it does clear `fn resolve`.

**CORRECTION 4 — "mem: 1 refusal" IS A FIRST-FAILURE, NOT A COUNT.** Clearing
`resolve` moved the refusal to `fn store_save` in `stdlib/mem/bt/memstore.logos`,
a different function with a different shape. The stdlib column reports the first
failing compile; a wall behind it is invisible until the first one is removed.

## 1. THE PROBE TABLE — build **8b5e784d12776013** (READ)

    probe        fires  ceiling  cost  cfail   std  verdict
    ltsliceeq    35702        0     0      0    ok  broken hop (PREDICTED)
    ltslicevar      39        0     0      0    ok  broken hop (PREDICTED)
    ltslicelt    35746        1     1      0    ok  ⛔ COST >= CEILING
    ltdyneq        243        0     0      0    ok  broken hop (PREDICTED)
    ltdynvar         0        —     —      —     —  NEVER FIRED (PREDICTED)
    ltdynlt        244        0     0      1    ok  ⛔ 1 pinned diagnostic LOST
    e716recvref   1334        0     0      0    ⛔  STOP — stdlib mem, store_save

## 2. THE MECHANISM — A NEW SENSOR ORPHANED TWO GUARDS, SILENTLY

`ltregslot` landed the region on the four kinds that used to throw it away
(`&[T]`/`&str` → `Kind::Slice`, `&dyn` → `TraitObject`, `&Dst` → `DstRef`;
`resolve_type`'s own comment names them). **No comparator was taught to read
it.** Both lifetime comparators are blind in exactly those kinds:

    site: include/logos/compiler/subtype.hpp::types_equal_with_lifetimes
          the Array/Slice arm compares the ELEMENT only; TraitObject and
          DstRef fall to `default:` → lifetime-ERASED TypeUID identity
    site: include/logos/compiler/subtype.hpp::subtype
          same Slice arm, same two kinds at `default: return true`

⚠ **RULE 18 PAID BEFORE IT COST ANYTHING: THE TWIN WAS FOUND BY READING, AND
EACH HALF ALONE PRICES ZERO THROUGH A BROKEN HOP (RULE 11).** `subtype`'s own
head calls `types_equal_with_lifetimes`, so an arm added in `subtype` alone is
never reached — `ltdynvar` NEVER FIRED, and `ltslicevar` fired 39 times against
`ltsliceeq`'s 35 702. Splitting the mechanism into three names was the whole
point; a single name would have printed one number and hidden the series.

## 3. ⛔ `ltslicelt` — CEILING 1, COST 1, AND THE PREDICTED ROW IS NOT THE ONE

PREDICTED: `better-blame-constraint-for-outlives-static`. MEASURED, diffed BOTH
ways — neither set contains the other's member:

    CLOSED  logos_00_bc_admit_regions_regions-glb-free-free--e-field-lifetime
            struct Flag<'a>{name:&'a str,desc:&'a str}
            fn h<'a,'b>(s:&'b str)->Flag<'a>{Flag{name:s,desc:s}}   ← 'b:'a, real
    COST    logos_02_semantic_core_pass_regions-dependent-autoslice-b142
            fn both<'r>(v:&'r [u64])->&'r [u64]{subslice1(subslice1(v))}

`better-blame` survives because its argument region is EMPTY (`buf: &[u8]`) and
`lt_eq` treats an empty side as a wildcard — the elision door, not this one.
The COST is the SAME door from the other side: the inner call's Slice result
carries a region the substitution does not map onto `'r`, so the arm refuses a
legal program at `check_variance(permissive=true)`. **THE COST IS A MISSING
EXEMPTION, NOT A WRONG RULE**, and the exemption is the one `lifetime_at`
already has for minted regions. That is the fundable next step, and it is one
variable.

⚠ AND THE DIAGNOSTIC IS UNREADABLE IN BOTH DIRECTIONS: `expected &[u8], got
&[u8]`. `type_str` does not print the region of a Slice/TraitObject/DstRef, so
every message this class of rule can emit names the same type twice. A rule that
lands here needs the printer first.

## 4. ⛔ `ltdynlt` — 0 ROWS, AND IT DESTROYS A CORRECT DIAGNOSTIC (RULE 14/15)

ceiling 0, cost 0 on the pass corpus, stdlib green — and the ONLY oracle that
sees it is `fail_text_oracle.py`: `.expected` LOST on
`logos_06_diagnostics_fail_regions-trait-object-subtyping`. Read on both
binaries, same fixture, `fn foo3<'a,'b>(x:&'a mut dyn Dummy)->&'b mut dyn Dummy`:

    unarmed  lifetime mismatch: return type has lifetime 'b but 'x' has lifetime 'a
    armed    return type mismatch: variance mismatch — expected &dyn Dummy, got
             &dyn Dummy — lifetime structure incompatible

The program is ALREADY refused, correctly, in upstream's own words. The arm
fires ahead of that check and replaces it with a message that prints the same
type on both sides. **DECLINE**, and the reason is not the ceiling: it is that
the arm re-words an already-red diagnostic into a worse one.

## 5. `e716recvref` — THE NARROWING WORKS AND THERE IS A SECOND WALL BEHIND IT

    site: src/compiler/borrow_check.cpp, the MethodCall arm's `rp.is_temp` gate
    e716recvref  build 8b5e784d12776013: fires 1334 · ceiling 0 · cost 0 ·
                 cfail 0 · stdlib ⛔ mem — `fn store_save`, NOT `fn resolve`

RULE 9, MEASURED AT ITS SHARPEST: `e716recvtmp` and `e716recvref` are
**IDENTICAL on every program the harness owns and on all six hand programs** —
both close `mk().view()` (rc 0 → 1) and both leave `b.view()`, `mk().val()`,
`mk().any()` (a struct BY VALUE), `take(mk().view())` and `mk().into_v()` at
rc 0. They separate on ONE column and ONE function name.

**THE SECOND WALL, NAMED AND REPRODUCED BY HAND.** `store_save` contains

    g.branches.borrow(i).name.as_str()

whose peeled base is `borrow(i)` — a CALL RETURNING A REFERENCE. Its referent
lives in `g` and outlives the statement; it is not an owning temporary at all.
`is_temporary_value_expr` answers on the NODE KIND and cannot see that. The hand
twins, scratch `w/w_reffield` and `w/w_refbase`:

    w_reffield  o.get().leaf.view()   unarmed rc 0 · both arms rc 1  ← the wall
    w_refbase   o.get().view()        unarmed rc 1  ← ⚠ SEE §6

## 6. ⚠ A DEFECT THE LEDGER CANNOT REPRESENT — IN THE OTHER DIRECTION

`w_refbase` is refused on the UNARMED binary, with no probe of this round or any
other:

    struct Inner{v:i64}  struct Outer{inner:Inner}
    impl Outer{ fn get(self:&Outer)->&Inner{ return &self.inner; } }
    impl Inner{ fn view(self:&Inner)->&i64{ return &self.v; } }
    let o: Outer = …;  let r: &i64 = o.get().view();   ← rc 1, E0716

That is legal Rust — `r` borrows `o`, which outlives it. The existing
`is_temporary_value_expr(v.receiver())` already treats a reference-returning
call as a temporary, one hop shallower than the peel. **The ledger holds only
programs that COMPILE and should not; a FALSE REFUSAL is invisible to it by
construction**, exactly as a permissive hole is. Recorded here because there is
nowhere else to put it.

## 7. SURVEY — `lifereg.R18` (8) + `lifereg.NEW-N1` (8), BY MISSING OBSERVATION

All 16 compiled MULTI-LINE on 52e163dd45fa5788: **rc 0, all sixteen.** The
lifetime channel's recent changes closed none of them, so nothing here is
credited on a diagnostic it does not print. NEITHER LABEL NAMES A MECHANISM AND
THE MECHANISMS CROSS BOTH LABELS.

**M-SIG — no trait-impl signature conformance check exists for lifetimes. 6.**
site: `src/compiler/sema_collect.cpp`, the `sig_match` loop. THREE missing
observations at ONE site, each independently fatal:
 (a) the loop starts at `k = 1` — the SELF parameter is never compared;
 (b) the RETURN type is never compared at all;
 (c) the comparator is `types_equal`, whose own definition comment in
     `src/compiler/sema.cpp` names the fields it ignores: "lifetime, pkg_name,
     lifetime_args, const_val".
Rows (all R18): iterator-next-extra-named-lifetime ·
lifetime-mismatch-between-trait-and-impl ·
trait-impl-mismatch-elided-lifetime-issue-65866 ·
impl-trait-lifetime-conflict-hashmap-keys · resolve-re-error-ice ·
ex3-both-anon-regions-using-impl-items.
⚠ AND THE DIAGNOSTIC IS WRONG EVEN WITHOUT LIFETIMES: an impl method whose
PARAMETER TYPE differs from the trait's prints `impl Foo for U: missing method
'foo'`. The method is present; its type is wrong.

**M-AGG — an aggregate/call instantiates a region binder and emits no outlives
constraint. 3.** site: `sema_impl.hpp::structlit_lt_subst_` (and the `"call"`
twin): candidates are collected per binder and resolved by first-wins or the
meet; the losing candidate's region is dropped in silence. Rows (NEW-N1):
ex2a-push-one-existing-name-2 · ex3-both-anon-regions-3 · regions-creating-enums3.

**M-SELF — the `self:` parameter's declared type is never checked. 3.**
R18: elided-lifetime-mismatch-in-self-type (`impl Pair<i64,i64> { fn say(self:
&Pair<u8,i64>) }` — an INHERENT impl, and not even a lifetime question).
NEW-N1: ex3-both-anon-regions-one-is-struct-5 (`self: &'a mut Foo<'a>`) ·
trait-method-lifetime-suggestion (a default body calling `&'a Self`).

**M-SLICE — §2's orphaned guards. 1.** NEW-N1: better-blame-constraint-for-
outlives-static. PRICED THIS ROUND and NOT bought: the arm that would close it
closes a different row instead (§3).

**M-WF — declaration-site WF / implied bounds `'b: 'a` on `&'a S<T<'b>>`. 1.**
NEW-N1: regions-outlives-projection-container.

**NOT A LIFETIME OR BORROW ROW AT ALL — MIS-SHELVED. 2.**
R18: dropck-only-error — upstream E0277, a trait BOUND on an associated-type
projection at a generic-argument position.
NEW-N1: drop-impl-for-type-param-with-trait-bound — upstream E0120, COHERENCE:
`impl<T> Drop for T where T: A`.

⇒ **R18 IS ESSENTIALLY ONE MECHANISM (6 of 8) AND IT IS NOT A BORROW-CHECK
MECHANISM AT ALL** — a declaration-conformance check that does not exist.
NEW-N1 is genuinely heterogeneous: five mechanisms over eight rows.

## 8. WHAT DESERVES FUNDING, IN ORDER

 1. **M-SIG.** Six ledger rows at one site, no region inference required, and
    it repairs a WRONG diagnostic ("missing method") on the non-lifetime case
    as well. The three sub-observations are independent and can be measured one
    at a time; (b) the return type is the cheapest.
 2. **`e716recvown`** — `e716recvref` plus "the peeled base's type is not itself
    a reference". §5 says exactly which stdlib statement demands it and §6
    gives the hand twin. Priced separately below.
 3. **`ltslicelt` with the minted-region exemption.** The rule closes a real row
    for the right reason; the one cost is a legal program the exemption
    `lifetime_at` already carries would rescue. But the PRINTER comes first —
    `expected &[u8], got &[u8]` is not a diagnostic.
 4. **The false refusal of §6.** No ledger row can hold it; a pass fixture can.
 5. ⛔ **`ltdynlt` — DO NOT LAND.** 0 rows, and it replaces a correct
    upstream-shaped message with a worse one.

## 9. OPEN

 1. The three E0716 rows — unchanged, still a corpus decision with an owner.
 2. `two-phase-nonrecv-autoref--c` vs `tpb-mut-with-shared-ref-arg` — unchanged.
 3. `d-index-two-phase` unbuyable while `btvec_append` keeps `f(&mut t, g(&t))`.
 4. `a-fnmut-twice` is still touched by nothing.
 5. `WRef<T>` in a local binding loses method resolution (2026-08-31u §5).
 6. The E0716 report still carries no file/line in an `--emit-module` build.
 7. `type_str` does not print the region of Slice / TraitObject / DstRef (§3).
 8. Still no sensor asserting the ABSENCE of a duplicate line.

## 10. `e716recvown` — THE SECOND WALL CLEARS, AND THE HOLE STAYS CLOSED

Second batch, one edit, one build **d0d36da4c844e00a** (READ), L1 rc 0 with
nothing armed.

    site: src/compiler/borrow_check.cpp, the MethodCall arm's `rp.is_temp` gate
    e716recvown    fires 1471 · ceiling 0 · cost 0 · cfail 0 · stdlib ok
    e716recvownbc  fires 1471 · ceiling 0 · cost 0 · cfail 0 · stdlib ok

The predicate is `e716recvref` plus one clause: **the peeled base's own TYPE
must not be a reference kind.** `e716recvownbc` adds `!is_borrow_carrying_type`
on top and is IDENTICAL on all three populations and on all eight hand
programs — the wider exemption buys nothing, so the narrow one is the one to
fund.

THE SUCCESS CRITERION IS THE HAND PROGRAM, AND IT IS MET IN BOTH DIRECTIONS.
Eight programs, all MULTI-LINE, run unarmed and under both arms:

    r_refuse    mk().view()                     rc 0 -> 1   ← THE HOLE, CLOSED
    w_reffield  o.get().leaf.view()             rc 0, rc 0  ← store_save's shape
    r_named     b.view()                        rc 0, rc 0
    r_scalar    mk().val()   -> i64             rc 0, rc 0
    r_handle    mk().any()   -> W by value      rc 0, rc 0  ← resolve's shape
    r_inline    take(mk().view())               rc 0, rc 0
    r_byval     mk().into_v()  (self: B)        rc 0, rc 0
    w_refbase   o.get().view()                  rc 1, rc 1  ← §6, UNARMED too

⚠ `w_refbase` is refused with NOTHING ARMED and is refused under the arms; the
arm neither causes it nor repairs it. Attributing it to this mechanism would
have been the easy mistake, and the unarmed column is the only thing that
prevents it.

⇒ **THIS IS THE ROUND'S ONE FUNDABLE MECHANISM AT ZERO PRICE.** It closes a
real dangling-reference hole (`mk().view()` with no `impl Drop` — one token
from the pinned `tests/spec/fail/borrow_diag_2__ref-from-temp`, which has the
`impl Drop`), on all three cost columns at zero, having cleared BOTH stdlib
walls that stopped its two predecessors. It buys no ledger row and cannot: the
ledger holds only programs that compile and should not, which is what this hole
is.
