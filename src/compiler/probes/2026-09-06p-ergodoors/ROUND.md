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
