#!/usr/bin/env bash
# ask.sh <question.dl> [source...] — ask the design space a question.
#
# One entry point, so using this during ordinary work costs one command instead
# of the manual dance of extracting, copying .dl files and remembering which
# directory souffle must run in.
#
# ⚠ THE CACHE IS KEYED BY CONTENT, AND THAT IS NOT AN OPTIMISATION. Three times
# in one session an answer came back from a stale copy of the rules that had
# been hand-copied into a facts directory — the shape of failure this whole
# architecture exists to prevent: AN ANSWER TO A QUESTION NOBODY IS ASKING ANY
# MORE. Nothing is "up to date" here because it looks recent; a TU's facts live
# under the hash of that TU's bytes, and the question runs under the hash of the
# rules. Staleness is unrepresentable rather than unlikely.
#
# The incrementality is a side effect of the same key: change one file and one
# file is re-extracted.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD
Q="${1:?usage: ask.sh <question.dl> [source...]}"; shift
[ -f "$Q" ] || Q="$ROOT/tools/dlog/$Q"
[ -f "$Q" ] || { echo "ask: no such question: $1"; exit 2; }

command -v souffle >/dev/null || { echo "ask: souffle not installed"; exit 2; }
# ⚠ RELINKING COSTS 11.5 s AND IS THE WHOLE WARM PATH. Rebuild only when the
# source is actually newer — a tool meant to be cheap enough to reach for during
# ordinary work cannot open with a twelve-second link.
if [ ! -x build/dlog/cxx_facts ] || [ tools/dlog/cxx_facts.cpp -nt build/dlog/cxx_facts ]; then
    bash tools/dlog/make.sh >/dev/null 2>&1 || { echo "ask: cxx_facts does not build"; exit 2; }
fi

# ⚠ ABSOLUTE. A relative cache path stops resolving the moment anything cds
# into it, which is how the first run lost its own souffle log — the same
# cwd-relative slip that made souffle read a stale schema three times today.
CACHE=$ROOT/build/dlog/cache
mkdir -p "$CACHE"

# Default subject: every compiler TU the build actually compiles — from the
# compilation database, not a glob, so a file excluded from the build is
# excluded here too.
if [ $# -gt 0 ]; then SRCS=("$@"); else
    mapfile -t SRCS < <(python3 - "$ROOT/build/compile_commands.json" <<'PY'
import json, sys
# ⚠ EXCLUDE GENERATED SOURCES. build/src/compiler/{logos,wql_surface,el}_parser.cpp
# match "/src/compiler/" too, and a machine-generated PEG parser has no design to
# reason about — it also kept one TU permanently "to extract", so the progress
# line said work was pending on every warm run.
for e in json.load(open(sys.argv[1])):
    f = e["file"]
    if "/build/" in f:
        continue
    if "/src/compiler/" in f and f.endswith((".cpp", ".cc")):
        print(f)
PY
)
fi
[ "${#SRCS[@]}" -ge 1 ] || { echo "ask: no sources (is build/ configured?)"; exit 2; }

# ⚠ THE KEY IS THE TU's BYTES *AND* THE EXTRACTOR's. A schema change must
# invalidate every TU, or the next question reads facts produced by rules that
# no longer exist — which is the same lie in a different direction.
EXV=$(sha256sum "$ROOT/tools/dlog/cxx_facts.cpp" "$ROOT/build/dlog/cxx_facts" | sha256sum | cut -c1-16)

# ⚠ HASH EACH SOURCE ONCE. The first version recomputed it inside the merge
# loop — 18 relations x 40 TUs = 720 sha256sum passes over multi-megabyte files,
# which made "2 of 40 TUs changed" cost 92 s, indistinguishable from a cold run.
# The cache was working perfectly and the bookkeeping around it was the cost.
declare -A HASH
for s in "${SRCS[@]}"; do HASH[$s]=$(sha256sum "$s" | cut -c1-32); done

stale=0
for s in "${SRCS[@]}"; do
    h=${HASH[$s]}
    d="$CACHE/$EXV-$h"
    [ -s "$d/node.facts" ] && continue
    rm -rf "$d"; mkdir -p "$d"
    echo "$s" >> "$CACHE/.todo.$$"
    echo "$d"  >> "$CACHE/.todod.$$"
    stale=$((stale + 1))
done
echo "ask: ${#SRCS[@]} TUs, $stale to extract"

if [ "$stale" -gt 0 ]; then
    # ⚠ A FAILED EXTRACTION MUST NOT BE SILENT. The first version deleted the
    # directory and said nothing, so one TU failed on every single run and the
    # only trace was a progress line that read "1 to extract" forever. A tool
    # that quietly drops part of its subject reports a clean answer over less
    # than it claims — the exact failure this design space exists to prevent.
    paste "$CACHE/.todo.$$" "$CACHE/.todod.$$" |
      xargs -P"$(nproc)" -I{} sh -c '
        set -- $1
        if ! "$0" -p build --out="$2" "$1" >"$2/log" 2>&1; then
            echo "ask: EXTRACTION FAILED for $1" >&2
            tail -3 "$2/log" | sed "s/^/    /" >&2
            rm -rf "$2"
        fi
      ' "$ROOT/build/dlog/cxx_facts" {} 2>&1 >/dev/null
fi
rm -f "$CACHE/.todo.$$" "$CACHE/.todod.$$"

# The answer directory is keyed by the QUESTION and its inputs, so re-asking the
# same question of the same tree is free and re-asking a CHANGED question is not
# silently answered from before.
QV=$(cat "$Q" "$ROOT"/tools/dlog/*.dl | sha256sum | cut -c1-16)
SV=$(for s in "${SRCS[@]}"; do echo "${HASH[$s]}"; done | sha256sum | cut -c1-16)
OUT="$CACHE/ans-$EXV-$QV-$SV"
if [ ! -d "$OUT" ]; then
    mkdir -p "$OUT"
    # ⚠ THE RELATION LIST IS DISCOVERED, NOT TYPED. It used to be two literal
    # lists and adding `decl_loc` to the extractor broke every question with
    # "Cannot open fact file" — an enumeration inside the tool built to catch
    # enumerations, for the fourth time in this directory. Take whatever the
    # extractor wrote.
    #
    # ⚠ AND WHETHER TO DEDUPLICATE FOLLOWS FROM THE IDENTITY SCHEME rather than
    # from a second list: node ids carry a TU tag and a '#', so they are unique
    # across TUs by construction and `sort -u` over them buys nothing while
    # costing everything (node.facts is 144 MB across 40 TUs). Declaration- and
    # type-keyed rows have no '#' — they are canonical precisely so that the same
    # entity seen by forty TUs is ONE row — and those must be deduplicated.
    first=$(ls -d "$CACHE/$EXV-${HASH[${SRCS[0]}]}")
    for f in "$first"/*.facts; do
        r=$(basename "$f" .facts)
        # ⚠ NOT `… | grep -q` UNDER pipefail: grep exits at the first match, the
        # writer takes SIGPIPE, and pipefail reports the MATCH as a failure — so
        # this test was ALWAYS false and the fast path never ran. Caught by
        # gate_lint's R2, which exists because this repo has been bitten before.
        key=$(head -1 "$f" | cut -f1)
        case "$key" in
        *'#'*)
            for s in "${SRCS[@]}"; do cat "$CACHE/$EXV-${HASH[$s]}/$r.facts" 2>/dev/null; done \
                > "$OUT/$r.facts" ;;
        *)
            for s in "${SRCS[@]}"; do cat "$CACHE/$EXV-${HASH[$s]}/$r.facts" 2>/dev/null; done \
                | sort -u > "$OUT/$r.facts" ;;
        esac
    done
    NODES=$(wc -l < "$OUT/node.facts")
    [ "$NODES" -ge 1000 ] || { echo "ask: only $NODES nodes across ${#SRCS[@]} TUs — refusing"; exit 3; }
    cp "$ROOT"/tools/dlog/*.dl "$OUT/"
    grep -vE '^\s*(#|$)' "$ROOT/tools/dlog/not_projection.claim" > "$OUT/not_projection.facts"
        grep -vE '^\s*(#|$)' "$ROOT/tools/dlog/duty.claim" | tr -s ' ' '\t' > "$OUT/duty.facts"
    (cd "$OUT" && souffle -F. -D. -I. "$Q" >"$OUT/souffle.log" 2>&1) || {
        echo "ask: souffle failed"; sed 's/^/  /' "$OUT/souffle.log" | head -20
        rm -rf "$OUT"; exit 2; }
fi
echo "$OUT"
