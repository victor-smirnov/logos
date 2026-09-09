# ROUND 2026-09-09i-cesc — `NEW-CESC` IS NOT ONE ROOT, AND THE CENSUS SAYS SO
#                          BEFORE ANY ARM DOES

Base `06f0c66be0400a1d 43` (HEAD `02c79cfba`), READ not assumed. Base binary
preserved as the round's control and proven LIVE before any verdict was read:
it reproduces all 8 target rows as ADMITTED and refuses hand shape I4 with the
recorded object-lifetime sentence. Tree restored; `git status` clean but for
this directory.

## 0. CENSUS

    soundness queue gate      rc 0 — 76 rows (t1=21 t2=7 t3=42 t4=6), '# TOTAL' 76
    bc_admits_ledger gate     rc 0 — see the CORRECTION below, a 3-argument
                                     hand invocation reports a FALSE RED
    bc_admits 92 · bc_admits_blocked 8
    probe-log-lint            256 records, every site symbol resolves
    build_hash.py             06f0c66be0400a1d 43

## 1. TARGET, AND WHY — see TARGETS.md, written before the compiler was touched

`bck.NEW-CESC` (3) + `nllmoves.NEW-CESC` (5) = 8 rows, 7 distinct programs. The
largest root in the ledger, and the only block where the arm EXISTS, the fact IS
deposited, and the fact has exactly ONE reader:

    arm      SemaChecker::check_object_lifetime_bound   sema_expr.cpp:15231
    deposit  closure_caps_by_id_[closure_id]            sema_expr.cpp:18310
    census   3 textual hits in the whole compiler: decl (sema_impl.hpp:3755),
             ONE read (15333), ONE write (18310)

## 2. CONTROLS RE-VERIFIED ON TODAY'S BINARY BEFORE ANY EDIT

    all 8 rows                                     still ADMITTED, rc 0
    NEW-CESC minting pair (2026-09-07), reproduced digit for digit:
      Box::new(|| -> i64 { x }) -> Box<dyn Fn()->i64>   REFUSED rc 1
        "coercion to `dyn Fn` requires `|| -> i64: 'static` — lifetime `'_` may
         not live long enough (object lifetime bound)"
      keep(|| -> &i64 { &x }) through `fn keep<F>(f:F)->F`  ADMITTED rc 0
    bck.D / nllmoves.D pairs (the block the prompt recommends), also reproduced:
      let m = &mut a; let s = &a;  "cannot borrow 'a' as shared: already mutably
        borrowed"  REFUSED   vs  double_access(&mut a, &a)  ADMITTED
      `fn f() -> &i64 { let v = 3; return &v; }`  "cannot return reference to
        local variable 'v': dangling reference"  REFUSED
    => neither root's control has decayed. What condemns the D merge is the
       ledger's own 2026-09-08 error-code split, not staleness.

## 3. THE CENSUS ANSWERS THE BLOCK BEFORE ANY ARM DOES  (rule 1, rule 16)

`cesc.olb.enter` counts arrivals at `check_object_lifetime_bound`, per program:

    issue-95079-missing-move-in-nested-closure   2   (cesc.olb.closure_src 1)
    hand L1 (legal, `dyn Fn + 'a` over a `&'a` capture)   1
    hand I4 (illegal, same bound over a LOCAL's borrow)   1
    anonymous-region-in-apit--closure-param-escapes       0
    borrowed-data-escapes-closure-148392                  0
    issue-40510-1                                         0
    issue-40510-3                                         0
    issue-42574-...--t15                                  0
    issue-48697--t16                                      0

SIX OF THE EIGHT ROWS NEVER REACH THE DOOR THE ROOT IS NAMED AFTER. Any ceiling
or cost measured at that door is, for those six, an unreached site and not a
zero. The root's own gloss — "an escaping closure's region is constrained ONLY
by a `dyn` coercion" — is a true statement about the COMPILER and a false
statement about the ROWS: it names the one door that exists, then groups seven
programs that never arrive at it.

AND THE DOOR IS NOT MISSING A FACT. L1/I4 is a one-variable pair — only the
captured region changes — and the existing arm gets BOTH right. So the fact the
prompt's preferred shape looks for ("an arm that exists reached through a fact
the code does not carry") is already carried here, for the capture case.

## 4. THE PROBE TABLE — every cost column, and the runtime column's absence

    probe        fires  ceiling  cost  cfail                         stdlib  runtime
    cescarr          0       —     —     —                              —      —
    cescbound       39       1      3   3 of 1473 (match 1, text 2)   4 of 4    not run
    cesccapmut      28       4      4  12 of 1473 (match 11, text 1)  4 of 4    not run

`cescarr`'s `fires 0` is a HARNESS READING, NOT A DEAD SITE: it is a census-only
edit with no `probe::on()` call, so the counter the table reads is never
incremented. Its census fired 4 times across the programs above. Recorded here
because the table prints "NEVER FIRED — not a zero, an unreached site" for it,
which is precisely wrong in this one case.

RUNTIME COLUMN NOT MEASURED, and the reason stated rather than hidden: both arms
are already STOP on an rc-visible column (3 and 4 legal `pass` fixtures refused,
plus a hand-written legal refusal each), so a 50-minute saturating `run_oracle`
pass could only make a condemned arm more condemned. A round that FUNDS either
shape must price the runtime column before landing; this round funds neither.

## 5. THE SETS, DIFFED BOTH WAYS AGAINST THE PREDICTION

`cescbound` — "a capture can never discharge a non-'static object bound"
  closed:    issue-95079-missing-move-in-nested-closure                    (1)
  predicted: issue-95079                                                   (1)
  predicted∖closed = ∅   closed∖predicted = ∅
  cost, predicted BY NAME before the run: hand L1, a LEGAL program. Confirmed,
    and the corpus adds three more of the same shape —
      pass_bc_objlt_closure_move_ref · pass_bc_objlt_closure_reborrow ·
      pass_bc_objlt_same_sig_twins
  cfail: `borrowck-escaping-closure-error-2` LOSES its `.expected`. That is one
    of the two BUCKET-1 rows retired on 2026-09-07 by this very arm — the crude
    form still refuses it, but FOR THE WRONG REASON, so the pinned sentence
    breaks. Rule 14 in its exact form.
  DIAGNOSTIC READ on the row it closes:
    "coercion to `dyn Fn` requires `|| -> i64: 'a` — lifetime `'_ (a capture
     cannot discharge 'a)` may not live long enough (object lifetime bound)"
    Correct verdict, WRONG SENTENCE: it says no capture can ever discharge 'a,
    which is false (L1). A row closed by this is not closed.

`cesccapmut` — "a by-reference capture of a `&mut` escapes"
  closed:    issue-42574-...--b · issue-42574-...--t15 · issue-51268 ·
             match-guards-always-borrow                                    (4)
  predicted: issue-42574-...--b · issue-42574-...--t15                     (2)
  predicted∖closed = ∅   closed∖predicted = { issue-51268,
                                              match-guards-always-borrow }
  AND THAT DIFFERENCE IS THE FINDING, not a bonus: the two unpredicted rows are
  rooted `nllmoves.NEW-CAPLOAN` and `nllmoves.B`. One crude arm reaches into
  THREE roots. Rule 6: a partition is not a root — this arm is keyed on a
  SYMPTOM ("there is a `&mut` in the env") that three different defects share.
  cost: hand L3 (a `&mut i64` parameter captured by a closure called in place —
    the most ordinary legal closure in the language) plus four pass fixtures,
    including `pass_closure-fn-mut-incr`.
  cfail: ELEVEN pinned fail fixtures LOSE their `.expected` — the whole
    `borrowck-closures-*` family. Eleven correct diagnostics replaced by one
    wrong sentence to close four rows.

## 6. WHAT THE ROUND ESTABLISHES ABOUT THE ROOT

Neither arm moves a row the other moves; the two closed sets are DISJOINT, and
together they cover 3 of the 8 rows. Five rows — `anonymous-region-in-apit`,
`borrowed-data-escapes-closure-148392`, `issue-40510-1`, `issue-40510-3`,
`issue-48697--t16` — are moved by NOTHING measured this round and reach the
root's own door ZERO times.

`NEW-CESC` is at least four mechanisms wearing one root name:

    D-DYNNEST  the object bound is checked against the enclosing FN's region
               where the capture actually lives in an enclosing CLOSURE's frame
               — issue-95079.  THE ONLY ROW THAT ARRIVES AT THE EXISTING ARM.
    D-CAPRB    a non-move capture of a `&mut` reborrowed for a longer region
               — issue-42574--b/--t15 (ONE program under two row ids)
    D-RET      a closure's RETURN region is not checked at all
               — issue-40510-1, issue-40510-3, issue-48697--t16
    D-STORE    a write INTO a capture of a borrow of another capture
               — borrowed-data-escapes-closure-148392
    D-SIG      a closure PARAMETER's anonymous region flowing into a capture's
               region argument — anonymous-region-in-apit
               (one row, no separating pair measured; do not group it)

This is the third handed-down grouping in three rounds to split under the "does
ONE candidate change move BOTH members" test. `NEW-CESC` was minted with a real
pair on 2026-09-07 and the pair still reproduces — but a pair speaks for the row
it was written from, exactly as the 2026-09-08 D-block note says, and this one
was written from issue-95079, the single member that reaches the door.

## 7. WHAT DESERVES FUNDING

FUND: D-RET, three rows (issue-40510-1, issue-40510-3, issue-48697--t16), the
largest sub-block and the only one whose members share a construct by READING as
well as by symptom: a closure's return type carries a region and nothing checks
it. The syntactic dangling-local rule already exists and already fires on the
function door (`fn f() -> &i64 { let v = 3; return &v; }`, measured today), and
`clos.param.elided` / `mint.ret.unified` census non-zero on exactly these
programs — so this is the arm-exists shape with the fact genuinely absent, which
is what NEW-CESC was believed to be and is not.

DO NOT FUND: `cescbound` or `cesccapmut` in any form resembling the probes. Both
buy rows with a legal-program refusal, which the round rules forbid outright.

RE-ROOT, as a ledger edit with the owner: split `NEW-CESC` into the five names
above. It is 8 rows currently claiming one mechanism they measurably do not
share, and the next round that picks this block inherits the wrong denominator.

REPORT TO THE OWNER: `issue-42574-...--b` and `--t15` are the SAME PROGRAM under
two row ids (diffed today: identical but for the package name), and so are
`borrowck-loan-blocks-move-cc--r10`/`--t10`. Merging rows is the owner's call;
the ledger already records both pairs and this round confirms the 42574 one by
measurement — a single probe closed both, always together, never one.
