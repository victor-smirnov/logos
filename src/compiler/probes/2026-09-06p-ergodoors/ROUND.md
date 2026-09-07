# ═══ ROUND 2026-09-06p (PRICING, soundness queue) — ONE PREDICATE, TWO FACTS IT IS NOT GIVEN:
#     THE 2024 MODIFIER RULE IS ASKED ABOUT THE WRONG MODIFIER, AND THE DEFAULT BINDING MODE
#     STOPS AT EVERY CONTAINER DOOR — WHERE IT COSTS A SECOND DESTRUCTOR CALL ═══════════════

## 0. STEP 1, RE-DERIVED (HEAD 0a5e73b05 = origin/main, clean)
    queue `# TOTAL` 62 = 62 by direct listing (tier1=20 tier2=6 tier3=33 tier4=3);
    bc_admits 98 / bc_admits_blocked 25; probe-log-lint 235 records, every site symbol resolves;
    build hash READ 95d01d3ae0a1858d 43; queue gate rc 0 in BOTH directions.
    ⚠ CORRECTION TO THE PROMPT, RE-VERIFIED AGAINST THE TEXT GIVEN (not copied from the journal):
      the STEP-1 gate command DOES carry `LOGOS_LIB_DIR` now. The correction four rounds recorded
      is LANDED and the two rounds that repeated it afterwards were wrong. Nothing to report.
    ⚠ The armed build (all six probes installed, none on) is 3f8fd1ddb67aedee 43, READ.

## 1. THE TARGET ROWS, NAMED BEFORE THE COMPILER WAS TOUCHED
    (src/compiler/probes/2026-09-06p-ergodoors/TARGET_ROWS.md, written at selection)
      match_ergo_ref_modifier_ref_mode_admit   tier 2  admits
      match_ergo_nested_tuple_mut_admit        tier 2  admits
    ROOT: `modifier_under_ref_scrutinee` IS the Rust-2024 sentence, minted once and already asked
    at four doors. Both rows are that ONE predicate not asked, each for a fact computed within
    sight of its call site and not passed: WHICH MODIFIER (the check is guarded `!explicit_ref`),
    and WHERE THE MODE COMES FROM (a container door re-derives it from the element's TYPE).
    Three handed-down groupings were tested by reading and REFUTED (four separate fn/closure/for
    walkers; parser vs `lower_let_pat_bound`; the array-shape pair, a real root but a new codegen
    shape class rather than an arm that exists).

## 2. THE ROW THE ROUND FOUND BEFORE IT ARMED ANYTHING — RULE 5 PAID AGAIN
    An abuse-direction hand program (a nested variant payload of a MOVE type, NO modifier written
    anywhere) runs its destructor TWICE. Measured on the BASE binary 95d01d3ae0a1858d, count on
    stdout, Rust = 1 for every row:
        tuple door   `match &p { (Option::Some(a), b) }`         2   n07 n08
        struct door  `match &w { W { o: Option::Some(a), k } }`  2   n10
        slice door   `match &arr { [Option::Some(a)] }`          2   n11
        if-let tuple `if let (Option::Some(a), b) = &p`          2   n13
        variant door `match &e { Outer::W(Option::Some(a)) }`    1   n12   CORRECT
        top level    `match &o { Option::Some(a) }`              1   n09   CORRECT
    This is the double free the default binding mode exists to prevent — the door's own comment
    says so — at the three doors that do not carry it; the VARIANT door does, which is why it is
    right. ROWED as nested_variant_payload_under_ref_double_drops (tier 1, run 2): the program
    reads the count through a `static mut` after the value's scope has ended, so the wrong count
    is an EXIT CODE (2 today, 0 in Rust). SAME ROOT as match_ergo_nested_tuple_mut_admit — that
    row is this lost fact as a missing diagnostic, this one as memory unsafety.

## 3. THE FALSE REFUSAL THE HAND SET CAUGHT, AND THE SECOND NAME THAT REPAIRS IT (rule 9)
    `ergoref` refuses r01 r02 r06 r14 (all four 2024-illegal, correct) AND n12 — a program with
    no `ref` anywhere — naming `'__refut_W_0_0'`. `binding_is_ref` is true both for a WRITTEN
    `ref` and for the compiler's own refutable-sub synthesis (`binding_is_ref.push_back(
    synth_wants_ref)`). The separating fact is `binding_from_wild`, which is FALSE for the synth
    push and which the landed `mut` half ALREADY asks. So the recorded 2026-09-09a price for
    ergoref (16 fires, "12 stdlib sites") is an OVER-COUNT of unknown size, and `ergorefw`
    (= ergoref + `binding_from_wild[k]`) is the priced-correct form.

## 4. THE PROBE TABLE — BATCH 1 (build 3f8fd1ddb67aedee 43, six probes, ONE build)
    probe        site                                        fires  ceil(bc) ceil(queue)  pass  cfail  stdlib  runtime
    ergoref      build_pattern_variant_data payload door         16       0       0(*)     1     0      ⛔lang   —
    ergoreftup   build_pattern_impl TUPLE push_ref_elem            0       0       0        0     0      ok      —
    ergorefst    build_pattern_impl STRUCT fld_is_ref              0       0       0        0     0      ok      —
    ergorefsl    build_pattern_impl SLICE element                  0       0       0        0     0      ok      —
    ergonest     build_pattern_impl TUPLE -> VARIANT_DATA sub      0       0       1        0     0      ok      0 of 6521
    ergonestchk  same site, narrow (walk ARGS for a written mut)   0       —       —        —     —      —       —
    (*) ergoref does not close match_ergo_ref_modifier_ref_mode_admit: the row names FIVE doors and
        ergoref is the payload door only — PREDICTED P1 before the run, CONFIRMED.
    ⚠ `fires 0` here is NOT "not measured": `probe-batch` skips the cost columns on a zero, so the
      four zero-fire probes were priced BY HAND — `stdlib-cost.sh <name>` (all four layers compile,
      four separate runs), `test-levels.sh L1` armed (767/767 + 12 684 generated smoke cases), and
      for ergonest a full `run_oracle.py` pair on ONE build: 6521 armed vs 6521 base, ONE differing
      triple and it is `cast-region-to-uint` (the stack-address printer, subtracted by name).
    ⚠ THE SITES ARE PROVEN LIVE, three ways, so the zeros are population facts and not dead code:
      13 hand programs move under them one door at a time; the QUEUE GATE ITSELF reads the row
      `match_ergo_nested_tuple_mut_admit` as NO LONGER REPRODUCING under ergonest ("the compile is
      no longer silent (cc=1 diag=1)"); and the class table of §2 changes exactly at the tuple door.
    ⚠ ergonestchk IS A BROKEN INSTRUMENT, reported not hidden (rule 18): its ARGS walk SEGFAULTS the
      compiler on 8 of 26 hand programs. Its columns are meaningless and the narrow/crude pair the
      round set out to separate was NOT separated. What the crude arm's own 13 negatives show is
      that it is not merely crude here: n04 n05 n06 n09 n12 and r01-r14 are byte-identical to base.

## 5. THE SETS, BOTH WAYS, WITH THE DIAGNOSTIC READ
    ergoref REFUSES  (armed \ base): r01 r02 r06 r14 n12 — four correct, ONE FALSE (§3).
    ergoreftup       r04 only.   ergorefst   r03 only.   ergorefsl   r05 only.
        Every one carries the minted 2024 sentence verbatim, read in full, not an rc.
    ergonest CHANGES (armed \ base): n01 n02 refused with the 2024 sentence; n07 n08 n13 destructor
        count 2 -> 1; n14 exit 2 -> 0. NOT changed: n10 (struct door) and n11 (slice door) — rule 6,
        a ceiling bounds the COUNT and not the SET, and the class has three doors while this arm
        carries one.
    UNCHANGED under every probe (the abuse direction, 13 programs, diffed both ways):
        r08 `match o { Some(ref v) }` by value · r09 `match *r { Some(ref v) }` (the stdlib repair
        spelling) · r10 `match &o { &Some(ref v) }` (an explicit `&`-pattern resets the mode to
        move, so `ref` is LEGAL — this is the shape rule 5 was bought with) · r11 r12 r13 by-value
        struct/slice/tuple · n04 by-value tuple · n05 n06 legal default-mode reads · n09 n12.
    THE STDLIB REPAIR IS PROVEN, NOT ASSERTED: s01 (`match self { S(ref v) }`, `self: &Opt2<T>`, the
        exact stdlib shape) is refused by ergoref; s02, the same function written `match *self`,
        compiles clean and runs — so the 12-site rewrite the owner decision costs has a working form.

## 6. BATCH 2 — THE CORRECTED PREDICATE AND THE CLASS'S OTHER TWO DOORS
    (build 70a76153b16e562c 43, three probes, ONE build; L1 rc 0 inert)
    probe        site                                          fires  ceil(bc)  pass  cfail  stdlib   runtime
    ergorefw     payload door + `binding_from_wild[k]`            16       0      1     0     ⛔lang   n/m (see below)
    ergonestst   STRUCT door -> VARIANT_DATA sub                   4       0      0     0     ok      0 of 6521
    ergonestsl   SLICE door -> VARIANT_DATA sub                    0       0      0     0     ok      0 by construction
    FULL COST LINE READ for both ref arms: `1 legal programs refused`, and the name is
    `logos_03_ownership_pass_lifetime_match_ref_option`; `COST-fail = 0 of 1435`; `stdlib ⛔ lang`.
    ⚠ THE TWO REF ARMS ARE IDENTICAL IN EVERY COLUMN THE HARNESS OWNS, DIGIT FOR DIGIT —
      16 / 0 / 1 / 0 / ⛔ — and separate ONLY on a hand program (n12). That is rule 9 stated
      literally, and it is the reason the round wrote counter-examples of a SHAPE the row's own
      program does not have. A round that had read the columns alone would have landed the arm
      that refuses legal code.
    ⚠ ergorefw's RUNTIME column is NOT MEASURED and cannot honestly be: the tool's own verdict is
      that nothing downstream of a stdlib that does not build is meaningful. The runtime column
      for this arm becomes measurable only AFTER the 12 stdlib sites are rewritten `match *self`.
    HANDS, batch 2 (26 programs, diffed both ways against base):
      ergorefw   REFUSES r01 r02 r06 r14 s01 — five, all 2024-illegal, all carrying the minted
                 sentence — and NOT n12. The false refusal is gone; nothing else moves.
      ergonestst n10 destructor count 2 -> 1. Nothing else moves.
      ergonestsl n11 destructor count 2 -> 1. Nothing else moves.
    ⚠ THE PER-DOOR SETS ARE DISJOINT, MEASURED AND NOT ASSUMED (rule 13): ergonest moves
      {n01 n02 n07 n08 n13 n14}, ergonestst moves {n10}, ergonestsl moves {n11}, and no program
      moves under two. So for THIS class the credit IS additive — stated because it was measured,
      and the three arms together take the six-cell table of §2 from four wrong cells to none.

## 7. WHAT DESERVES FUNDING, IN ORDER, WITH ITS PRICE
    1. ergonest + ergonestst + ergonestsl — THE CONTAINER-DOOR CLASS, one hunk per door.
       Closes match_ergo_nested_tuple_mut_admit (queue gate's own reading) AND the new tier-1
       nested_variant_payload_under_ref_double_drops, and takes the six-cell class table to zero
       wrong cells. PRICE, MEASURED: stdlib all four layers under each; L1 767/767 + 12 684 smoke
       under ergonest; cfail 0; runtime 0 of 6521 for ergonest and 0 of 6521 for ergonestst, each
       with only `cast-region-to-uint` differing (the stack-address printer, subtracted by name);
       28 abuse-direction hand programs unchanged. There is no cheaper repair of a double
       destructor call in this queue.
       ⚠ WHAT A FIX MUST DO THAT THESE ARMS DO NOT (rule 7): the arms carry the mode only into a
       PAT_VARIANT_DATA sub. A correct fix carries it into every sub-pattern kind a container door
       builds, and the round did NOT price the wider carriage — a nested TUPLE-in-tuple or
       STRUCT-in-tuple payload of a move type was not measured and may hold the same defect.
    2. ergorefw — the WRITTEN `ref`/`ref mut` half of the 2024 rule, at the payload door.
       PRICE: 1 pass fixture (logos_03_ownership_pass_lifetime_match_ref_option), 0 of 1435
       fail-text, and ⛔ the stdlib `lang` layer — 12 sites in option/result/cmp whose repair is
       proven by construction (s01 refused, s02 = the same function as `match *self` compiles and
       runs). Its blocker was a corpus decision; the owner made it (this round's prompt: "a
       `mut`/`ref` binding modifier under a non-move default binding mode is an ERROR"), and the
       spec ALREADY states it — `pat.binding.modifier-requires-move-mode`, docs/spec/patterns.md,
       names `mut`, `ref` and `ref mut` together. So the gap the two tier-2 rows record is between
       the SPEC and the implementation, not an open language question, and no tier-4 `diag` row
       needs adding: `match &o { Some(mut n) }` already prints the 2024 sentence verbatim.
       ⚠ FUND ONLY WITH `binding_from_wild`. The uncorrected `ergoref` refuses `match &e {
          Outer::W(Option::Some(a)) }` — no modifier written anywhere — naming a SYNTH binding
          `'__refut_W_0_0'`, and its harness columns are identical to the correct arm's.
    3. ergoreftup / ergorefst / ergorefsl — the same 2024 rule at the tuple, struct and slice
       doors. fires 0, cost 0, cfail 0, stdlib ok, each closing exactly its own hand twin (r04,
       r03, r05) and moving nothing else. Free, and the row names these doors, so leaving them
       out re-creates the 2021/2024 hybrid the last landing warned about. They should land in the
       SAME commit as ergorefw or not at all.
    NOT FUNDED THIS ROUND: ergonestchk. Its instrument segfaults (§4); the narrow/crude question
    it was built to answer is still open, and the crude arm's 28-program abuse set is the only
    evidence standing in its place.

## ergoref
site: src/compiler/sema_stmt.cpp::build_pattern_variant_data (payload door, `if (explicit_ref)`)
build: 3f8fd1ddb67aedee
measured: 2026-09-06
fires: 16
ceiling: 0 (bc) / 0 (queue)
cost: 1 pass (logos_03_ownership_pass_lifetime_match_ref_option) / cfail 0 of 1435 / stdlib ⛔ lang (12 sites, option/result/cmp) / runtime not measurable behind a stdlib that does not build
verdict: DO NOT FUND THIS FORM — it refuses n12, a legal program with no modifier written, naming the synth binding '__refut_W_0_0'. Superseded by ergorefw, which is identical in every harness column.

## ergorefw
site: same, plus `binding_from_wild[k]` — the fact the landed `mut` half already asks
build: 70a76153b16e562c
measured: 2026-09-06
fires: 16
ceiling: 0 (bc) / 0 (queue — the row names five doors, this is one; PREDICTED P1, confirmed)
cost: 1 pass (logos_03_ownership_pass_lifetime_match_ref_option) / cfail 0 of 1435 / stdlib ⛔ lang / runtime n/m
hands: refuses r01 r02 r06 r14 s01 (all 2024-illegal, sentence read in full); n12 NOT refused; 21 negatives unchanged
verdict: FUND with ergoreftup + ergorefst + ergorefsl in ONE commit; the price is the 12-site `match *self` rewrite (proven by s01/s02) and one fixture re-pin, which the owner decision has authorised

## ergoreftup / ergorefst / ergorefsl
site: build_pattern_impl — TUPLE `push_ref_elem`, STRUCT `fld_is_ref`, SLICE element
build: 3f8fd1ddb67aedee
measured: 2026-09-06
fires: 0 / 0 / 0 — an unreached POPULATION, not a dead site: r04 / r03 / r05 each move under exactly one of them
ceiling: 0 (bc) / 0 (queue)
cost: 0 pass / cfail 0 / stdlib ok (four layers, three separate runs) / runtime 0 by construction
verdict: FUND — free, and required for the row not to leave a 2021/2024 hybrid across doors

## ergonest
site: src/compiler/sema_stmt.cpp::build_pattern_impl (TUPLE door, the PAT_VARIANT_DATA element branch)
build: 3f8fd1ddb67aedee
measured: 2026-09-06
fires: 0 (the corpus contains no nested variant payload under a reference scrutinee)
ceiling: 0 (bc) / 2 (queue: match_ergo_nested_tuple_mut_admit, read by the GATE itself as no longer reproducing; nested_variant_payload_under_ref_double_drops, n14 exit 2 -> 0 by hand)
cost: 0 pass (L1 767/767 + 12 684 generated smoke cases, armed) / cfail 0 / stdlib ok / runtime 0 of 6521 (one differing triple, cast-region-to-uint, subtracted by name)
hands: n01 n02 refused with the 2024 sentence; n07 n08 n13 destructor count 2 -> 1; n14 exit 2 -> 0; n10 n11 UNCHANGED (the class's other two doors); 20 negatives byte-identical
verdict: FUND — the cheapest repair of a double destructor call in this queue

## ergonestst / ergonestsl
site: build_pattern_impl — STRUCT field sub-pattern, SLICE element sub-pattern
build: 70a76153b16e562c
measured: 2026-09-06
fires: 4 / 0
ceiling: 0 (bc) / 0 (queue — the class's other two doors have no row of their own; they are members of nested_variant_payload_under_ref_double_drops' header table)
cost: 0 pass / cfail 0 / stdlib ok / runtime 0 of 6521 for ergonestst (cast-region-to-uint only), 0 by construction for ergonestsl
hands: n10 2 -> 1 / n11 2 -> 1; nothing else moves under either; the three arms' moved sets are DISJOINT (measured)
verdict: FUND with ergonest — one class, three doors

## ergonestchk
site: same as ergonest, narrow twin (walk the payload ARGS for a WRITTEN `mut`)
build: 3f8fd1ddb67aedee
measured: 2026-09-06
fires: 0
ceiling: — / —
cost: — (NOT MEASURED)
verdict: BROKEN INSTRUMENT, reported not hidden (rule 18): the ARGS walk SEGFAULTS the compiler on 8 of 26 hand programs. The narrow/crude separation this pair existed to make was NOT made; what stands in its place is the crude arm's 20 unchanged negatives.
