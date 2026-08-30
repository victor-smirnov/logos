#!/usr/bin/env bash
# probe-batch.sh <spec-file> — install N probes, build ONCE, price all of them.
#
# ⚠ THIS EXISTS BECAUSE THE INSTRUCTION DID NOT WORK. The batch protocol says
# "install ALL probes in ONE build"; measured 2026-08-28 on the round that
# carried that sentence in its prompt: SIX builds for a batch of NINE probes,
# ~15 minutes of avoidable rebuilds. The same round ran the 12-minute `L4 bc`
# gate twice where the ladder says once, and the round before it spent 52% of
# its command time in poll loops. An instruction is followed most of the time
# and the residue lands on the expensive commands — so the build has to be
# somewhere the agent cannot put it in a loop.
#
# The build is INSIDE this script, after every edit is applied. There is no
# per-probe build to reach for.
#
# SPEC FILE FORMAT — one probe per record, records separated by a line of ===:
#   name: <probe-name>
#   file: <path>
#   ---
#   <verbatim OLD text>
#   ---
#   <verbatim NEW text>
#   ===
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
SPEC="${1:?usage: probe-batch.sh <spec-file>}"

# ── BARRIER: THIS RUNS LONGER THAN A FOREGROUND TOOL CALL CAN LAST ───────────
# One build (~150 s) plus one pricing per probe (~110 s each): a six-probe batch
# is about thirteen minutes against a 600 s cap. So a foreground invocation
# CANNOT complete, and the agent that meets it invents a poll loop — measured on
# the round of 2026-08-29, 58% of that phase's command time spent waiting, while
# the phase that never hit this barrier spent 0%. The same arithmetic already
# retired the foreground path for `test-levels.sh L4`; this tool needed it too
# and I noticed only after watching a round pay for it.
#
# LOGOS_BATCH_BG=1 is the acknowledgement, and the line below is the invocation.
_n=$(grep -c '^name:' "$SPEC" 2>/dev/null || echo 1)
# ⚠ THE PER-PROBE ESTIMATE MOVED 110 -> 230 ON 2026-08-30, and it is not a
# guess: the pass half through the store is 128 s (measured on a fresh armed
# identity), the `-L bc -L fail` text oracle adds 54 s armed, and the four
# stdlib layers add 49 s. Plus 55 s once per build for the fail baseline, which
# is keyed on `build_hash.py` and shared by every probe in the batch. That
# buys the two damage shapes the old price could not see at all.
_est=$(( 150 + 55 + 230 * _n ))
# ⚠ THE THRESHOLD IS REAL, NOT A ROUND NUMBER. Refusing a batch that WOULD fit
# and telling it "this cannot finish" would be a barrier with a false reason —
# and a false reason is the thing that teaches people to route around barriers.
# 480 s is 80% of the cap: below it a foreground run finishes with margin.
if [ "${LOGOS_BATCH_BG:-0}" != "1" ] && [ "$_est" -gt 480 ]; then
    echo "probe-batch: $_n probes ≈ ${_est} s; a foreground tool call is capped at 600 s." >&2
    echo "  It cannot finish in the foreground, and polling it wastes the whole wait." >&2
    echo "  Run it detached and block on the RC marker:" >&2
    echo "" >&2
    echo "    nohup bash -c 'cd $PWD && LOGOS_BATCH_BG=1 bash scripts/probe-batch.sh $SPEC > /tmp/batch.log 2>&1; echo RC=\$? >> /tmp/batch.log' >/dev/null 2>&1 &" >&2
    echo "    until grep -q '^RC=' /tmp/batch.log; do sleep 30; done; tail -30 /tmp/batch.log" >&2
    echo "" >&2
    echo "  (or the Bash tool's own run_in_background, which notifies on exit)" >&2
    echo "" >&2
    echo "  ⚠ AND IF IT IS INTERRUPTED between applying the edits and finishing, the" >&2
    echo "  tree keeps them: 'git checkout' the touched files before retrying, or the" >&2
    echo "  next call refuses on a dirty tree it caused itself." >&2
    exit 2
fi
[ -f "$SPEC" ] || { echo "probe-batch: no such spec: $SPEC" >&2; exit 2; }

# lint:git-ok — HYGIENE is exactly what git knows: whether someone else's
# uncommitted edit is in the tree. A batch price taken over one measures both.
if ! git diff --quiet || ! git diff --cached --quiet; then  # lint:git-ok — hygiene is exactly what git knows: someone else's uncommitted edit, which a batch price would silently measure too
    echo "probe-batch: the tree is dirty. Commit or stash first — a batch price" >&2
    echo "  taken over someone else's uncommitted edit measures both." >&2
    exit 2
fi

# ⚠ AN INTERRUPTED BATCH USED TO LEAVE ITS EDITS IN THE TREE, and the next call
# then refused on a dirty tree it had caused itself. That happened three times
# while testing this script alone. The trap reverts exactly the files the spec
# touches — not `git checkout .`, which would take somebody else's work with it.
_touched=$(grep '^file:' "$SPEC" | awk '{print $2}' | sort -u)
_cleanup() {
    st=$?
    [ "${_APPLIED:-0}" = "1" ] || exit $st  # lint:exit-ok — nothing applied, nothing to undo
    echo "probe-batch: interrupted after applying — reverting $(printf '%s' "$_touched" | wc -l) file(s)" >&2
    # lint:git-ok — undoing THIS script's own edits, which is hygiene by definition
    for f in $_touched; do git checkout -- "$f" 2>/dev/null; done
    # ⚠ REVERTING THE SOURCE IS HALF THE JOB. Bash defers a trap until the
    # foreground child returns, so an interrupt during the build lets the build
    # FINISH — and then the source is reverted while `build/bin/logosc` still
    # contains the probes. MEASURED 2026-08-29: the binary held `bt_obs_record`
    # and `bt_obs_walk`, the sources held neither, and a ledger baseline of 365
    # verdicts was recorded against a compiler no source in the tree produces.
    # It was harmless only because those two probes were observational.
    # So: rebuild from the restored source, and if that cannot be done, POISON
    # the binary rather than leave a plausible-looking phantom behind.
    echo "probe-batch: rebuilding from the restored source — the binary still has the probes" >&2
    if cmake --build build -j"$(nproc)" >/dev/null 2>&1; then
        echo "probe-batch: rebuilt; the binary matches the tree again" >&2
    else
        echo "probe-batch: ⚠ REBUILD FAILED. build/bin/logosc contains probes that are" >&2
        echo "  in NO source file. Any measurement taken with it is about a compiler" >&2
        echo "  nothing in this tree produces. Rebuild before trusting a single number." >&2
        rm -f build/bin/logosc
        echo "  The binary has been REMOVED so the next run cannot silently use it." >&2
    fi
    exit $st  # lint:exit-ok
}
trap _cleanup INT TERM HUP

echo "probe-batch: applying edits"
python3 - "$SPEC" <<'PY' || exit 2
import sys, io, re
spec = io.open(sys.argv[1], encoding="utf-8").read()
recs = [r for r in spec.split("\n===\n") if r.strip()]
names = []
for r in recs:
    head, old, new = r.split("\n---\n", 2)
    name = re.search(r'name:\s*(\S+)', head).group(1)
    path = re.search(r'file:\s*(\S+)', head).group(1)
    s = io.open(path, encoding="utf-8").read()
    old = old.strip("\n"); new = new.strip("\n")
    n = s.count(old)
    if n != 1:
        print(f"probe-batch: {name}: OLD text occurs {n} times in {path} — "
              f"a non-unique anchor edits the wrong site", file=sys.stderr)
        sys.exit(2)
    io.open(path, "w", encoding="utf-8").write(s.replace(old, new, 1))
    names.append(name)
# ⚠ TRAILING NEWLINE. `while read` drops a final line that has none, so a
# two-probe batch priced ONE and said nothing about the other — a silent
# partial, which is the failure mode this whole harness exists to prevent.
io.open("/tmp/probe-batch-names.txt", "w").write("\n".join(names) + "\n")
print("probe-batch: applied", len(names), "edits:", " ".join(names))
PY

_APPLIED=1
echo "probe-batch: ONE build"
if ! cmake --build build -j"$(nproc)" > /tmp/probe-batch-build.log 2>&1; then
    echo "probe-batch: BUILD FAILED — tail:" >&2; tail -20 /tmp/probe-batch-build.log >&2
    echo "probe-batch: the tree still carries the edits; fix or 'git checkout' them." >&2
    exit 2
fi

echo "probe-batch: proving the batch is inert with nothing armed"
if ! (cd build && bash ../tests/logos/test-levels.sh L1) > /tmp/probe-batch-l1.log 2>&1; then
    echo "probe-batch: L1 IS RED WITH NO PROBE ARMED — some probe is not env-gated." >&2
    grep -E 'Failed|tests passed' /tmp/probe-batch-l1.log | head -10 >&2
    exit 2
fi
echo "probe-batch: L1 rc=0, batch inert"

echo
# ⚠ THREE COST COLUMNS, BECAUSE ONE NUMBER HID TWO DAMAGE SHAPES. Until
# 2026-08-30 this table printed a single `cost` read off the pass corpus, and
# on that round TWO probes printed `cost 0` while one broke the stdlib build
# and the other regressed five pinned diagnostics. `cfail` is the `-L bc -L
# fail` half read by TEXT (rc / .expected-match / text-only, summed), and
# `std` is whether all four stdlib layers still compile. A dash in either
# column means NOT MEASURED — never "nothing happened".
printf '%-24s %9s %7s %6s %6s %4s  %s\n' probe fires ceiling cost cfail std verdict
while read -r n; do
    [ -z "$n" ] && continue
    # `< /dev/null` so an inner command can never consume the loop's input.
    # ⚠ NOT the bug that bit: that was a missing trailing newline in the
    # names file. I diagnosed this first and was wrong; the guard is kept
    # because it is cheap, not because it was the cause.
    out=$(bash scripts/ceiling-probe.sh "$n" 2>&1 < /dev/null)
    f=$(printf '%s' "$out" | grep -oP "fired \K\S+" | head -1)
    c=$(printf '%s' "$out" | grep -oP 'CEILING = \K\d+' | head -1)
    co=$(printf '%s' "$out" | grep -oP 'COST    = \K\d+' | head -1)
    cf=$(printf '%s' "$out" | grep -oP 'COST-fail = \K\d+' | head -1)
    # ⚠ NOT `| grep -q`: under `set -o pipefail` grep exits at the first match,
    # the writer takes SIGPIPE 141, and pipefail reports the MATCH as a failed
    # pipeline. Write to a file first, match second.
    printf '%s\n' "$out" > "/tmp/probe-$n.out"
    grep -q 'COST-fail = NOT MEASURED' "/tmp/probe-$n.out" && cf="—"
    if   grep -q 'stdlib: ⛔ REFUSED' "/tmp/probe-$n.out"; then st="⛔"
    elif grep -q 'COST-stdlib = NOT MEASURED' "/tmp/probe-$n.out"; then st="—"
    elif grep -q 'stdlib: all four layers' "/tmp/probe-$n.out"; then st="ok"
    else st="?"; fi
    if grep -q 'NEVER FIRED' "/tmp/probe-$n.out"; then
        printf '%-24s %9s %7s %6s %6s %4s  %s\n' "$n" 0 — — — — "NEVER FIRED — not a zero, an unreached site"
    else
        v="?"
        # ⚠ 0/0 IS NOT A STOP SIGN. An observational probe has ceiling 0 by
        # construction, and so does a live site whose mechanism changes nothing;
        # calling either "STOP cost>=ceiling" reads as a refutation of something
        # that was never claimed. Only a probe that BUYS something and costs at
        # least as much is a stop sign.
        # ⚠ THE VERDICT READS ALL THREE COLUMNS. A probe that prints cost 0
        # and breaks the stdlib is not "ok", and the old table said it was.
        if [ "$st" = "⛔" ]; then v="STOP — THE STDLIB DID NOT COMPILE"
        elif [ -n "${c:-}" ] && [ -n "${co:-}" ]; then
            cfn=0; case "${cf:-0}" in ''|*[!0-9]*) cfn=0;; *) cfn=$cf;; esac
            if [ "$c" -eq 0 ] && [ "$co" -eq 0 ] && [ "$cfn" -eq 0 ]; then v="no effect (see rule 4: is the site populous?)"
            elif [ "$cfn" -gt "$c" ];             then v="STOP re-words more diagnostics than it closes rows"
            elif [ "$co" -ge "$c" ];              then v="STOP cost>=ceiling"
            else                                       v="ok"; fi
        fi
        printf '%-24s %9s %7s %6s %6s %4s  %s\n' "$n" "${f:-?}" "${c:-?}" "${co:-?}" "${cf:-?}" "$st" "$v"
    fi
done < /tmp/probe-batch-names.txt
echo
echo "probe-batch: full output per probe in /tmp/probe-<name>.out"
echo "⚠ a CEILING bounds the COUNT, not the SET — diff each closed row list"
echo "  against what you predicted BY NAME before believing any of these."
echo "⚠ COST 0 is not a safety claim. Write the counter-examples."
echo "⚠ cfail/std of '—' means NOT MEASURED. On 2026-08-30 the two probes whose"
echo "  costs were false both printed cost 0 with those two columns absent."
