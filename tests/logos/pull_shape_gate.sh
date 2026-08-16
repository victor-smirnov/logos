#!/usr/bin/env bash
# pull_shape_gate.sh LOGOSC PASS_DIR
#
# ADR 0025 CRITERION 2 — THE PULL SHAPE, PINNED. (R-H, 2026-08-16.)
#
# UNTIL THIS FILE EXISTED, CRITERION 2 HAD NO GATE. Every number §2 of
# `docs/adr/0025-criteria-and-instruments.md` prints — `next_batch()` pulls,
# `SliceStream` wraps, the 65 row-at-a-time `.next()` calls and their four
# classes, the indexed walks and their four buckets — was measured by a human
# typing a grep at the shell, recorded in prose, and then quoted by the next
# stage. §7 says so in as many words. A number no test holds is a number that
# decays silently: R-A found `3975` was the READER (a regex blind to the `limit`
# arm's double paren, 8% of the population invisible), R-F found the three
# indexed-walk buckets did not sum (a fourth, `21` field slices, fell between
# three greps), R-E's batch-loop grep counted a NUMBERED spelling and so missed
# the three bare `__bj` loops. Three instrument defects in three stages, all in
# numbers that had a consumer and no owner.
#
# THIS GATE OWNS THEM. It re-derives every criterion-2 figure from the artifact
# on every run, and it is built so that the three defects above would each be
# RED here rather than found by the next reader:
#
#   * NOTHING IS COUNTED BY A LONE GREP. Every population is asserted against a
#     SECOND, INDEPENDENTLY-SPELLED count of the same thing (declaration vs use,
#     producer vs consumer), so a regex that goes blind takes its partner's
#     equality down with it instead of just reading smaller.
#   * EVERY SPLIT IS A PARTITION AND THE SUM IS ASSERTED. R-F's F2 lesson,
#     stated as code: a bucket split that does not sum to the canonical grep is
#     not a partition, it is four greps that happen to be near each other. Both
#     the `.next()` 4-way and the indexed-walk 4-way carry a SUM clause.
#     ⚠ CORRECTED BY THE R-H CLOSING AUDIT: an earlier draft of this paragraph
#     said the residual bucket "is defined as the COMPLEMENT". IT IS NOT, and
#     the whole point is that it must not be — a complement bucket makes its own
#     sum clause vacuous (see the `walks_internal` note below, and the note in
#     `CMakeLists.txt` beside the registration). ALL FOUR walk buckets are
#     POSITIVE rules and a fifth `walks_unclaimed` counter is PINNED AT ZERO, so
#     a walk subject no rule claims reds on its own pin AND on the sum. The code
#     always did this; the sentence was left over from the draft that did not.
#   * THE BUCKET RULES ARE RULES, NOT SPELLINGS. `field slice` is "the walk
#     subject is parenthesised and contains a `.`", not "the subject ends in
#     `.as_slice()`" — which is the one-line difference between R-F's 21/157 and
#     the R-G verifier's 22/156 split (the odd line is `(c.nums)`, a field slice
#     that never spells `as_slice`). This gate takes the RULE-derived split and
#     records the other reading here so the two are never confused again.
#
# ⚠ WHAT THIS GATE IS NOT. It is not a claim that criterion 2 is met — it is
# not, and §7 says why. It is the instrument that makes the NEXT stage's claim
# falsifiable: a stage that says "I moved the pull plane" must move these
# numbers, and a stage that says "I touched nothing here" must leave all of them
# alone. R-B and (b′) are both stages of the second kind and both were BELIEVED
# on a hand-run grep; from here they would be gated.
#
# ── THE PINS AND WHERE EACH NUMBER COMES FROM ───────────────────────────────
#
# All figures below are measured on THIS tree (188 corpus fixtures, 171 with
# user dumps) and every one of them reproduces the figure §2/§6.2 of the ADR
# records, which is the point of pinning them rather than re-deriving a fresh
# set: this gate does not restate criterion 2, it holds the reading the arc
# closed on.
#
#   next_batch() pulls                     1018   (R-F: 165 → 1018)
#   SliceStream::<  wraps                  1002   (R-F: 149 → 1002)
#   .next() row pulls                        65   (unmoved since the audit)
#       aggregate key enumeration            39   declared out (Part 3b)
#       native iterator scan                 14   declared source kind (Part 3a)
#       drain prelude                         9   declared source kind (Part 3a)
#       join build side                       3   the fourth pull site
#   indexed walks (canonical grep)         3301
#       INTERNAL compiler containers       2513   not sources; no route claims
#       fixpoint `_sl` slices               610   the (b′) plane
#       DECLARED SLICE PARAMS               156   the routable population, was 1010
#       FIELD slices of the bound row        22   step_wrap's byval tier
#
# ⚠ THE MOVEMENT RULE, and it is the standing one. These pins move only when a
# stage MOVES THE PLANE, and the edit that moves them carries a derivation
# comment naming the stage and the direction. The directions are not symmetric
# and the gate prints them: `next_batch`/`SliceStream` UP and the param bucket
# DOWN is criterion 2 progressing; `.next()` UP is a REGRESSION toward row-pull
# and is exactly what a mis-routed re-walk capability would look like (R-H
# Part 2 measured that route and refused it on this ground). Nothing here may be
# relaxed to accommodate a reading — re-derive the number, or the stage did not
# do what it said.
#
# ⚠ THE `.next()` DECOMPOSITION IS THE PLACE A FUTURE STAGE WILL BE TEMPTED TO
# CHEAT, because three of its four classes are DECLARED OUT (ADR §7 C2). A
# declared-out class that GROWS is still a red here: "declared out" is a
# statement about a route, not a licence for the population to drift. The
# aggregate class additionally carries a CROSS-PIN against its producer — 39
# `.next()` sites against 39 `HashMapKeys<…>` declarations, 1:1 — so the class
# cannot be re-attributed by re-spelling one side of it.
#
# ⚠ NO PIPE INTO `wc`/`grep -c` IS TRUSTED FOR A COUNT under `pipefail`: zero is
# a legitimate answer for several clauses here and `grep` exits 1 on it. All
# tallying happens in the python pass, which reads files.
#
# PROVED TO BITE — one targeted perturbation per clause family, each applied
# alone to a SANDBOX COPY of the gate's own input (never to the tree's emitter),
# each restored md5-proven with a green checkpoint after. See the table in
# `tests/logos/CMakeLists.txt` beside the registration.
#
# EXIT 0 all pins hold · 1 a pin moved · 2 the gate could not measure.
set -uo pipefail

LOGOSC="${1:?logosc}"
PASS="${2:?pass dir}"
# The third argument is a private hook for the bite-proof: a directory of
# already-swept `*.user` dumps to read INSTEAD of sweeping. It exists so a
# perturbation can be applied to the gate's INPUT (a copy of the corpus dumps)
# rather than to the stdlib emitter, which is the only way to red a clause
# without a stdlib rebuild inside a test. Unused by the registered test.
PRESWEPT="${3:-}"

export LC_ALL=C
[ -x "$LOGOSC" ] || { echo "FAIL(2): no logosc at $LOGOSC"; exit 2; }

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
OUT="$TMPD/o"
mkdir -p "$OUT/_st"

if [ -n "$PRESWEPT" ]; then
    [ -d "$PRESWEPT" ] || { echo "FAIL(2): no pre-swept dir $PRESWEPT"; exit 2; }
    cp "$PRESWEPT"/*.user "$OUT/" 2>/dev/null
    NFIX=0
else
    shopt -s nullglob
    FIXTURES=("$PASS"/wql_*.logos "$PASS"/deem_*.logos)
    NFIX=${#FIXTURES[@]}
    # THE BLINDNESS FLOOR. A gate that sweeps three fixtures and finds all its
    # pins at zero reads exactly like a gate whose subject was deleted.
    if [ "$NFIX" -lt 150 ]; then
        echo "FAIL(2): only $NFIX corpus fixtures matched — the sweep is blind."
        exit 2
    fi
    one() {
        local f="$1" OUT="$2" LOGOSC="$3"
        local b; b=$(basename "$f" .logos)
        local d; d=$(mktemp -d)
        LOGOS_TRACE_PLAN=1 "$LOGOSC" "$f" --gen-dir "$d/gen" -o "$d/o.o" \
            > "$d/out" 2> "$d/err"
        echo "$?" > "$OUT/_st/$b"
        shopt -s nullglob
        # The USER module's dumps only — `logos.gen.*` holds the family
        # DEFINITIONS, and a `next_batch()` found there is the stdlib's own
        # `BatchStream` impl, not a query pulling anything. Scoped by SHAPE
        # ("everything that is not `logos.gen.*`") and not by `test.*`, because
        # two corpus fixtures declare their own package name — the S2d defect.
        local U=() x
        for x in "$d"/gen/*.gen.logos; do
            case "$(basename "$x")" in logos.gen.*) ;; *) U+=("$x");; esac
        done
        [ "${#U[@]}" -ge 1 ] && cat "${U[@]}" > "$OUT/$b.user"
        rm -rf "$d"
    }
    export -f one
    printf '%s\0' "${FIXTURES[@]}" \
        | xargs -0 -P "$(nproc)" -I{} bash -c 'one "$@"' _ {} "$OUT" "$LOGOSC"
    ST=("$OUT"/_st/*)
    if [ "${#ST[@]}" -ne "$NFIX" ]; then
        echo "FAIL(2): ${#ST[@]} rc files for $NFIX probes — probes were lost."
        exit 2
    fi
fi

python3 - "$OUT" "$NFIX" <<'PY'
import re, sys, glob, os

OUT   = sys.argv[1]
NFIX  = int(sys.argv[2])
dumps = sorted(glob.glob(os.path.join(OUT, '*.user')))

# ── THE PIN BLOCK ───────────────────────────────────────────────────────────
# One dict, so the failure message can print every number and the fix is one
# edit next to the sentence saying which plane moved.
PIN = {
    # population
    'dumps'            : 171,

    # ── BATCH PULLS ─────────────────────────────────────────────────────────
    # `next_batch()` is criterion 2's numerator. R-F took it 165 → 1018 by
    # routing the twelve remaining slice sites; every wrap it added removed
    # exactly one indexed walk, which is the accounting the three-way
    # correspondence below re-asserts on every run.
    'next_batch'       : 1018,
    # THE CORRESPONDENCE, and it is R-E's F7 defect fixed rather than repeated.
    # F7 counted `while (__bj<N> < __bn<N>)` — a NUMBERED spelling — and so
    # missed the three bare `__bj` loops, reading 1015 against 1018 pulls and
    # calling the difference "a different spelling of the same grep". It was
    # not: it was a regex that could not see 3 sites. Here the loop is counted
    # THREE ways that must agree with the pull count exactly —
    #   (a) the `while` header, digits OPTIONAL;
    #   (b) the loop variable's DECLARATION at its zero seed;
    #   (c) the pull itself.
    # A batch pull with no loop is a batch dropped on the floor; a loop with no
    # pull is an indexed walk wearing the batch plane's variable names.
    'batch_loop_while' : 1018,
    'batch_loop_decl'  : 1018,
    # ⚠ THE FOURTH `__bj` DECLARATION SHAPE, pinned APART so it cannot absorb
    # a member of the population above. `let mut __bj0: u64 = __bn0;` seeds the
    # cursor at the END of the batch — the descending elision's backward walk
    # (`deem_order_desc_elision` ×3, `deem_batch_scan_drain` ×1). It is a batch
    # CONSUMER with no pull of its own, which is why the three-way equality
    # above must not count it, and why leaving it unpinned would let a new
    # non-zero seed shape appear silently.
    'batch_loop_reseed': 4,

    # ── THE SLICE WRAP ──────────────────────────────────────────────────────
    # Three spellings of one act: the type mention, the stream variable's
    # declaration, and the pull through it. R-A's plane is the only one that
    # emits `SliceStream`, from ONE function (`rexpr_walk::batch_scan_frag`),
    # so all three are 1:1 by construction — and that is exactly why they are
    # worth asserting: the day they disagree, a second emitter has appeared.
    'slicestream_ty'   : 1002,
    'slicestream_decl' : 1002,
    'slicestream_pull' : 1002,

    # ── ROW-AT-A-TIME PULLS, THE 4-WAY PARTITION ────────────────────────────
    # 65 since the audit, unmoved by R-A, R-B, R-F, R-G and (b′) — five stages,
    # five zero deltas. That stability is the reason this number is the best
    # control in the arc, and the reason a stage that moves it must say so.
    'next_total'       : 65,
    # (i) AGGREGATE KEY ENUMERATION — `match __it.next()` over a `HashMapKeys`,
    # the min/max retract-rebuild arm. DECLARED OUT permanently (ADR §7 C2 (b)):
    # the receiver is the emitter's OWN per-group map, not a source, and
    # `RowsBatch<R>` is `&[R]` which cannot form over `Entry<K,V>` stride
    # without the copy criterion 1 forbids.
    'next_agg_keys'    : 39,
    # THE CROSS-PIN: 1:1 against the PRODUCER's declaration. Counting the pull
    # alone would let the class be re-attributed by re-spelling the receiver.
    'hashmapkeys_decl' : 39,
    # (ii) NATIVE ITERATOR SCAN — a source whose producer offers an iterator.
    # DECLARED (ADR §7 C2 (a)): row-pull IS this source kind's protocol
    # (natspec `i`), and S6-A MEASURED the alternative (wrapping it in a
    # `Buffer`) as a regression. On-plane by declaration, not a gap.
    'next_native_iter' : 14,
    # (iii) DRAIN PRELUDE — `__it_<s>.next()` landing into a `Buffer`. Same
    # declaration as (ii): the drain exists because the source is an iterator.
    'next_drain_prel'  : 9,
    # (iv) JOIN BUILD SIDE — `rexpr_walk::build_phase_frag`, the fourth pull
    # site, never converted when S1 collapsed the scan. NOT declared out: this
    # is a real gap, kept visible at 3 so it cannot be forgotten. Any batch
    # source on a build side dies there, and no corpus query puts one there —
    # a permissive defect invisible to a green corpus by construction.
    'next_join_build'  : 3,

    # ── INDEXED WALKS, THE 4-WAY PARTITION ──────────────────────────────────
    # The canonical grep is §2's fixed derivation (BOTH paren spellings — the
    # single-paren version cannot see the `limit` arm and understated the
    # population by 8%).
    'walks_total'      : 3301,
    # (i) INTERNAL compiler containers — `__ks`, `__ix0`, `__g_key`, `__bv<s>`,
    # `__h.__s<n>`, `__out` … Not sources; no route has ever claimed them.
    # ⚠ DEFINED BY A POSITIVE RULE (an UNPARENTHESISED dotted-or-bare name),
    # NOT as "everything else". A residual bucket makes its own sum clause
    # VACUOUS — four buckets that partition by complement always add up to the
    # total no matter what the emitter does, which is precisely the shape of
    # green the R-F F2 lesson is about. All four rules are positive and the
    # UNCLAIMED count below is pinned at zero, so a fifth walk subject reds
    # here instead of being absorbed.
    'walks_internal'   : 2513,
    'walks_unclaimed'  : 0,
    # (ii) THE FIXPOINT `_sl` PLANE — `(__rel_<r>_sl)` / `(__dl_<r>_sl)`.
    # DECLARED OUT per S6-B (ADR §7 C2 (c)): the DRed/fixpoint driver walks
    # consume `&[…]` by design and the incremental tier is written to that
    # contract. ⚠ 0 of the 1002 `SliceStream` wraps touch one of these names
    # (R-H Part 2 measured it): the rel plane is off the batch plane entirely,
    # which is the ground on which the re-walk capability was refused.
    'walks_fixpoint'   : 610,
    # (iii) DECLARED SLICE PARAMS — `(<param>).len()`, the ONLY routable
    # population, 1010 before R-F and 156 after. The residual is entirely
    # declared-out planes (119 incremental/DRed + 37 fixpoint drivers).
    'walks_param'      : 156,
    # (iv) FIELD SLICES OF THE BOUND ROW — `(n.kids.as_slice())`,
    # `(e.skills.as_slice())`, `(c.nums)`. `step_wrap`'s byval tier: scalar
    # traversal over a field of the row just bound, never a declared param, so
    # `slice_stream_src` is false there by construction. Declared out.
    # ⚠ THE RULE IS "PARENTHESISED SUBJECT CONTAINING A `.`", NOT "ends in
    # `.as_slice()`". R-F's split was 21/157 and the R-G verifier's 22/156; the
    # single line between them is `(c.nums)`, a field slice that does not spell
    # `as_slice`. The rule-derived reading (22/156) is the one pinned, because
    # a bucket defined by a spelling is a bucket that stops being a bucket the
    # day the emitter changes the spelling.
    'walks_field'      : 22,
}

if not dumps:
    print("FAIL(2): no user dumps in %s — nothing was swept." % OUT)
    sys.exit(2)

M = {'dumps': len(dumps)}
text = []
for p in dumps:
    with open(p, 'r', errors='replace') as fh:
        text.append(fh.read())
BLOB = '\n'.join(text)

def n(pat):
    return len(re.findall(pat, BLOB))

# ── batch pulls ─────────────────────────────────────────────────────────────
M['next_batch']        = n(r'\.next_batch\(\)')
M['batch_loop_while']  = n(r'while \(__bj[0-9]* < __bn[0-9]*\)')
M['batch_loop_decl']   = n(r'let mut __bj[0-9]*: u64 = 0u64;')
M['batch_loop_reseed'] = n(r'let mut __bj[0-9]*: u64 = __bn[0-9]*;')

# ── the slice wrap ──────────────────────────────────────────────────────────
M['slicestream_ty']    = n(r'SliceStream::<')
M['slicestream_decl']  = n(r'let mut __ss[0-9]*:')
M['slicestream_pull']  = n(r'\(__ss[0-9]*\)\.next_batch\(\)')

# ── row-at-a-time pulls ─────────────────────────────────────────────────────
M['next_total']        = n(r'\.next\(\)')
M['next_agg_keys']     = n(r'match __it\.next\(\) \{ Option::Some\(__kp\) =>')
M['hashmapkeys_decl']  = n(r'let mut __it: HashMapKeys<')
M['next_native_iter']  = n(r'let __opt[0-9]*: Option<.*> = \(__rel_[a-z_0-9]*\)\.next\(\);')
M['next_drain_prel']   = n(r'let __dr: Option<.*> = __it_[a-z_0-9]*\.next\(\);')
M['next_join_build']   = n(r'let __bo[0-9]*: Option<.*> = \(__rel_[a-z_0-9]*\)\.next\(\);')

# ── indexed walks, bucketed by RULE over the walk SUBJECT ───────────────────
# The canonical §2 grep, both paren spellings. The subject is what follows the
# `<`; the bucket rules read only the subject, so a bucket cannot be defined by
# accident on the loop VARIABLE's name (which is what made the `__i` family
# look like a bucket in the audit — it is a spelling of the walker, not of the
# thing walked).
SUBJ = re.findall(r'while \(\(?[A-Za-z_0-9]* < ([^;{]*\.len\(\))\)', BLOB)
M['walks_total'] = len(SUBJ)
buckets = {'walks_fixpoint': 0, 'walks_param': 0, 'walks_field': 0,
           'walks_internal': 0, 'walks_unclaimed': 0}
RE_FIX   = re.compile(r'^\((?:__rel_|__dl_)[A-Za-z_0-9]*_sl\)\.len\(\)$')
RE_PARAM = re.compile(r'^\([a-z][A-Za-z_0-9]*\)\.len\(\)$')
RE_FIELD = re.compile(r'^\(.*\..*\)\.len\(\)$')
RE_INT   = re.compile(r'^[A-Za-z_0-9]+(?:\.[A-Za-z_0-9]+)*\.len\(\)$')
UNCLAIMED = []
for s in SUBJ:
    if   RE_FIX.match(s):   buckets['walks_fixpoint'] += 1
    elif RE_PARAM.match(s): buckets['walks_param']    += 1
    elif RE_FIELD.match(s): buckets['walks_field']    += 1
    elif RE_INT.match(s):   buckets['walks_internal'] += 1
    else:
        buckets['walks_unclaimed'] += 1
        UNCLAIMED.append(s)
M.update(buckets)

fail = []

# ── CLAUSE 1: every pin, exactly ────────────────────────────────────────────
for k in PIN:
    if M.get(k) != PIN[k]:
        fail.append("PIN %-18s expected %5d  measured %5d" % (k, PIN[k], M[k]))

# ── CLAUSE 2: the `.next()` 4-way SUMS (R-F F2) ─────────────────────────────
nsum = (M['next_agg_keys'] + M['next_native_iter']
        + M['next_drain_prel'] + M['next_join_build'])
if nsum != M['next_total']:
    fail.append(
        "PARTITION .next(): %d agg-keys + %d native-iter + %d drain-prelude + "
        "%d join-build = %d, but the corpus has %d `.next()` calls — %d "
        "unaccounted. A split that does not sum is not a partition: a FIFTH "
        "row-pull class exists and no decision covers it."
        % (M['next_agg_keys'], M['next_native_iter'], M['next_drain_prel'],
           M['next_join_build'], nsum, M['next_total'], M['next_total'] - nsum))

# ── CLAUSE 3: the aggregate class is 1:1 with its PRODUCER ──────────────────
if M['next_agg_keys'] != M['hashmapkeys_decl']:
    fail.append(
        "CROSS-PIN aggregate keys: %d `__it.next()` pulls against %d "
        "`HashMapKeys<…>` declarations. The 1:1 is the whole ground for calling "
        "this class an ENUMERATION of the emitter's own map rather than a "
        "source scan (ADR §7 C2 (b)); if it breaks, the declaration is stale."
        % (M['next_agg_keys'], M['hashmapkeys_decl']))

# ── CLAUSE 4: the indexed-walk 4-way SUMS to the canonical grep ─────────────
wsum = (M['walks_internal'] + M['walks_fixpoint']
        + M['walks_param'] + M['walks_field'])
if wsum != M['walks_total']:
    fail.append(
        "PARTITION indexed walks: %d internal + %d fixpoint + %d param + %d "
        "field = %d against a canonical grep of %d — %d walk subject(s) match "
        "NO bucket rule. Sample: %s"
        % (M['walks_internal'], M['walks_fixpoint'], M['walks_param'],
           M['walks_field'], wsum, M['walks_total'],
           M['walks_total'] - wsum,
           ', '.join(sorted(set(UNCLAIMED))[:5]) or '(none)'))

# ── CLAUSE 5: batch loops correspond to batch pulls, three ways ─────────────
if not (M['batch_loop_while'] == M['batch_loop_decl'] == M['next_batch']):
    fail.append(
        "CORRESPONDENCE batch plane: %d `while (__bj < __bn)` headers, %d "
        "`__bj` zero-seed declarations, %d `next_batch()` pulls — these are "
        "three spellings of ONE act and must agree. A pull with no loop is a "
        "batch dropped; a loop with no pull is an indexed walk wearing the "
        "batch plane's names. (R-E F7 read 1015 vs 1018 here and called it a "
        "spelling difference; it was a regex blind to three bare `__bj`.)"
        % (M['batch_loop_while'], M['batch_loop_decl'], M['next_batch']))

# ── CLAUSE 6: the slice wrap corresponds, three ways ────────────────────────
if not (M['slicestream_ty'] == M['slicestream_decl'] == M['slicestream_pull']):
    fail.append(
        "CORRESPONDENCE SliceStream: %d `SliceStream::<` mentions, %d `__ss` "
        "declarations, %d pulls through one. All three are emitted by "
        "`rexpr_walk::batch_scan_frag` and by nothing else; a disagreement "
        "means a SECOND emitter of this plane has appeared."
        % (M['slicestream_ty'], M['slicestream_decl'], M['slicestream_pull']))

# ── the report, printed whether or not anything failed ──────────────────────
print("ADR 0025 criterion 2 — PULL SHAPE (%d fixtures, %d user dumps)"
      % (NFIX, M['dumps']))
print("  batch pulls   next_batch()          %5d   (loops %d while / %d decl, "
      "reseed %d)"
      % (M['next_batch'], M['batch_loop_while'], M['batch_loop_decl'],
         M['batch_loop_reseed']))
print("  slice wrap    SliceStream::<        %5d   (decl %d, pull %d)"
      % (M['slicestream_ty'], M['slicestream_decl'], M['slicestream_pull']))
print("  row pulls     .next()               %5d   = %d agg-keys (== %d "
      "HashMapKeys) + %d native-iter + %d drain-prelude + %d join-build"
      % (M['next_total'], M['next_agg_keys'], M['hashmapkeys_decl'],
         M['next_native_iter'], M['next_drain_prel'], M['next_join_build']))
print("  indexed walks canonical             %5d   = %d internal + %d fixpoint "
      "+ %d param + %d field   (unclaimed %d)"
      % (M['walks_total'], M['walks_internal'], M['walks_fixpoint'],
         M['walks_param'], M['walks_field'], M['walks_unclaimed']))

if fail:
    print("")
    print("FAIL: the pull shape moved and no derivation says why.")
    for f in fail:
        print("  " + f)
    print("")
    print("⚠ DIRECTIONS ARE NOT SYMMETRIC. `next_batch`/`SliceStream` UP and "
          "`walks_param` DOWN is criterion 2 progressing — re-derive the pin "
          "WITH the stage that moved it and say so in a comment. `.next()` UP "
          "is a REGRESSION toward row-pull (the shape a mis-routed re-walk "
          "capability takes; R-H Part 2 refused one on exactly this ground). "
          "A declared-out class that GROWS is red too: `declared out` is a "
          "statement about a ROUTE, not a licence for the population to drift.")
    sys.exit(1)

print("OK: every criterion-2 pin holds, both partitions sum, all three "
      "correspondences agree.")
PY
# THE PYTHON PASS IS THE LAST COMMAND and its status IS the gate's verdict
# (0 / 1 / 2, set by the `sys.exit` calls above). No `exit $rc` trailer: a
# computed status is the 8-bit ceiling waiting to happen, and the recorded
# lesson is that a real process status is already a byte.
