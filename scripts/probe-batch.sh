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
[ -f "$SPEC" ] || { echo "probe-batch: no such spec: $SPEC" >&2; exit 2; }

# lint:git-ok — HYGIENE is exactly what git knows: whether someone else's
# uncommitted edit is in the tree. A batch price taken over one measures both.
if ! git diff --quiet || ! git diff --cached --quiet; then  # lint:git-ok — hygiene is exactly what git knows: someone else's uncommitted edit, which a batch price would silently measure too
    echo "probe-batch: the tree is dirty. Commit or stash first — a batch price" >&2
    echo "  taken over someone else's uncommitted edit measures both." >&2
    exit 2
fi

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
io.open("/tmp/probe-batch-names.txt", "w").write("\n".join(names))
print("probe-batch: applied", len(names), "edits:", " ".join(names))
PY

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
printf '%-26s %10s %8s %7s  %s\n' probe fires ceiling cost verdict
while read -r n; do
    [ -z "$n" ] && continue
    out=$(bash scripts/ceiling-probe.sh "$n" 2>&1)
    f=$(printf '%s' "$out" | grep -oP "fired \K\S+" | head -1)
    c=$(printf '%s' "$out" | grep -oP 'CEILING = \K\d+' | head -1)
    co=$(printf '%s' "$out" | grep -oP 'COST    = \K\d+' | head -1)
    # ⚠ NOT `| grep -q`: under `set -o pipefail` grep exits at the first match,
    # the writer takes SIGPIPE 141, and pipefail reports the MATCH as a failed
    # pipeline. Write to a file first, match second.
    printf '%s\n' "$out" > "/tmp/probe-$n.out"
    if grep -q 'NEVER FIRED' "/tmp/probe-$n.out"; then
        printf '%-26s %10s %8s %7s  %s\n' "$n" 0 — — "NEVER FIRED — not a zero, an unreached site"
    else
        v="?"
        [ -n "${c:-}" ] && [ -n "${co:-}" ] && { [ "$co" -ge "$c" ] && v="STOP cost>=ceiling" || v="ok"; }
        printf '%-26s %10s %8s %7s  %s\n' "$n" "${f:-?}" "${c:-?}" "${co:-?}" "$v"
    fi
done < /tmp/probe-batch-names.txt
echo
echo "probe-batch: full output per probe in /tmp/probe-<name>.out"
echo "⚠ a CEILING bounds the COUNT, not the SET — diff each closed row list"
echo "  against what you predicted BY NAME before believing any of these."
echo "⚠ COST 0 is not a safety claim. Write the counter-examples."
