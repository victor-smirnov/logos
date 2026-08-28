#!/usr/bin/env bash
# coverage-map.sh — WHICH REGIONS OF ONE COMPILER TRANSLATION UNIT DOES ANYTHING
# WE TEST ACTUALLY EXECUTE, AND HOW OFTEN?
#
# ── WHY THIS EXISTS ─────────────────────────────────────────────────────────
# scripts/ceiling-probe.sh and scripts/pass-probe.sh price a HYPOTHESIS: arm a
# deliberately-wrong edit at a chosen site, read what moves. They work, and they
# have one hole — THE SITE IS A GUESS. include/logos/compiler/probe.hpp already
# records the trap that follows from it: a probe that never executes reports
# ceiling 0, which reads exactly like a refuted hypothesis, so `on()` counts its
# own fires and the reader refuses a run that never fired.
#
# MEASURED 2026-08-27, after the fact: three probes came back zero over sites
# the whole acceptance population reached 4, 3 and 2 times. Not never-fired —
# fired, and reported a ceiling of 0 that no population that small could have
# made nonzero. Three slots spent discovering there was nothing behind them.
# This map would have said so for the entire file at once, before anyone chose.
#
# ── ⚠ THIS IS NOT A PROBE AND DOES NOT REPLACE ONE ──────────────────────────
# Coverage says WHICH CODE EXECUTES AND HOW OFTEN. A probe says WHAT CHANGES if
# the code behaves differently, and coverage cannot answer that at all — every
# number of the last two days came from a probe. This builds the map that says
# where to aim. It never says what will be found there.
#
# ── ⚠ AND A ZERO HERE IS NOT A DEFECT ───────────────────────────────────────
# Count 0 means exactly "nothing in the population below executed it", which is
# dead code, OR a case the corpus does not exercise, OR a structurally
# unreachable guard. Three situations, three different responses; this tool
# cannot tell them apart and the map says so at the top. Reading a zero as a
# defect is the same error as reading the census's 28 915 "arrivals" as
# checks — real numbers, meaning something other than what was taken off them.
#
# ── HOW ─────────────────────────────────────────────────────────────────────
# ONE translation unit is rebuilt with `-fprofile-instr-generate
# -fcoverage-mapping` and relinked into a SEPARATE binary in a scratch
# directory. `build/` is never touched, so no later measurement is slowed by an
# instrumented tree and no timing taken afterwards is meaningless.
#
# The population is the corpus AS CTEST HAS IT: every `run_test.sh` test
# registered in the configured build directory, invoked with that test's own
# source and flags, plus the four stdlib layers. It is asked of the build
# directory, never grepped out of a CMakeLists — a name in a file is not a
# registration. Each compile writes its OWN `.profraw` under a unique index:
# with a shared name the processes overwrite one another and the merge yields a
# silently truncated profile, i.e. a confidently wrong map, which is worse than
# no map at all.
#
# NOT A GATE, and registered as such in tests/logos/gate_lint.py NOT_GATES: it
# pronounces no verdict about the tree. There is no count of cold regions that
# is the right one, and a floor on coverage is a floor somebody guessed.
#
# ── USE ─────────────────────────────────────────────────────────────────────
#     scripts/coverage-map.sh                      # borrow_check.cpp, whole corpus
#     scripts/coverage-map.sh --tu src/compiler/sema_expr.cpp
#     scripts/coverage-map.sh --limit 400          # a cheap look, 400 compiles
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${BUILD:-$ROOT/build}
TU=src/compiler/borrow_check.cpp
WORK=${WORK:-$ROOT/build/coverage-map}
LIMIT=0
OUTDIR=$ROOT/docs/coverage
JOBS=$(nproc)

while [ $# -gt 0 ]; do
    case "$1" in
        --tu)      TU=$2; shift 2 ;;
        --build)   BUILD=$2; shift 2 ;;
        --work)    WORK=$2; shift 2 ;;
        --out)     OUTDIR=$2; shift 2 ;;
        --limit)   LIMIT=$2; shift 2 ;;   # 0 = the whole population
        -h|--help) sed -n '2,60p' "$0"; exit 0 ;;
        *) echo "coverage-map.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

STEM=$(basename "$TU" .cpp)
SRC=$ROOT/$TU
DATE=$(date +%Y-%m-%d)
CC=${CC_COV:-clang++-20}
PROFDATA=${PROFDATA:-llvm-profdata-20}
COV=${COV:-llvm-cov-20}

[ -f "$SRC" ] || { echo "coverage-map.sh: no such TU: $SRC" >&2; exit 2; }
[ -f "$BUILD/build.ninja" ] || {
    echo "coverage-map.sh: $BUILD is not a configured ninja build dir." >&2
    echo "  The population is DERIVED from it. Without one this tool would map a" >&2
    echo "  population it invented, and report it as the corpus. Refusing." >&2
    exit 3; }

echo "== coverage-map: $TU =="
rm -rf "$WORK"
mkdir -p "$WORK/raw" "$WORK/obj" "$OUTDIR"

# ── 1. THE INSTRUMENTED BINARY ──────────────────────────────────────────────
# The TU's real compile line is taken from compile_commands.json, so every
# -I, -D and -std matches the build byte for byte; only the output, the coverage
# flags and the warning flags differ. The link line is taken from ninja for the
# same reason, with the one object substituted.
echo "-- compiling $TU with coverage instrumentation"
python3 - "$BUILD" "$SRC" "$WORK/$STEM.cov.o" <<'PY' > "$WORK/compile.sh"
import json, shlex, sys
build, src, out = sys.argv[1:4]
db = json.load(open(build + "/compile_commands.json"))
hit = [e for e in db if e["file"] == src]
if len(hit) != 1:
    sys.exit("compile_commands.json: %d entries for %s" % (len(hit), src))
args = shlex.split(hit[0]["command"])
keep, skip = [], False
for i, a in enumerate(args):
    if skip:
        skip = False
        continue
    if a in ("-o", "-MT", "-MF"):
        skip = True
        continue
    if a.startswith("-W") or a == "-MD":
        continue
    keep.append(a)
keep += ["-w", "-fprofile-instr-generate", "-fcoverage-mapping", "-o", out]
print("cd %s && %s" % (shlex.quote(build), " ".join(shlex.quote(a) for a in keep)))
PY
bash "$WORK/compile.sh"

echo "-- relinking $STEM into $WORK/logosc-cov"
LINK=$(ninja -C "$BUILD" -t commands bin/logosc | tail -1)
LINK=${LINK//src\/compiler\/CMakeFiles\/logosc.dir\/$STEM.cpp.o/$WORK/$STEM.cov.o}
LINK=${LINK//-o bin\/logosc/-fprofile-instr-generate -o $WORK/logosc-cov}
( cd "$BUILD" && eval "$LINK" )
# The binary must sit beside the real one: logosc finds its stdlib relative to
# argv[0] (LOGOS_LIB_RELDIR), and a compiler that cannot find lang/mem/std would
# fail every compile and produce a map of the front end only.
cp "$WORK/logosc-cov" "$BUILD/bin/logosc-cov"

# ── 2. THE POPULATION, ASKED OF THE BUILD DIRECTORY ─────────────────────────
echo "-- deriving the population from $BUILD"
python3 - "$BUILD" "$LIMIT" <<'PY' > "$WORK/jobs.tsv"
import re, sys
build, limit = sys.argv[1], int(sys.argv[2])
text = open(build + "/tests/logos/CTestTestfile.cmake").read()
n = 0
for line in text.split("\n"):
    if not line.startswith("add_test") or "run_test.sh" not in line:
        continue
    args = re.findall(r'"([^"]*)"', line)
    if not args or not args[0].endswith("run_test.sh"):
        continue          # run_test.sh as an ARGUMENT to another gate, not a fixture
    n += 1
    print("\t".join([str(n)] + args[3:4] + args[5:]))
    if limit and n >= limit:
        break
print("population: %d corpus compiles" % n, file=sys.stderr)
PY
NJOBS=$(wc -l < "$WORK/jobs.tsv")
echo "-- $NJOBS corpus compiles"

# ── 3. RUN IT. ⚠ ONE PROFRAW PER PROCESS, NAMED BY INDEX ────────────────────
# Not a fixed name (the processes would overwrite one another and the merge
# would be a truncated profile reported as a complete one), and not %p either:
# 8000 processes can recycle a pid. The index is the job number, so the file
# count is checkable against the job count, and it is, below.
export LOGOS_LIB_DIR=$BUILD/lib/logos
export LOGOS_TEST_LIB_DIR=$BUILD/tests/logos
export LOGOS_MLIRGEN_BUG_LEDGER=$ROOT/tests/logos/mlir_gen_bug.ledger
echo "-- running the corpus under the instrumented compiler (-P$JOBS)"
cut -f2- "$WORK/jobs.tsv" | tr '\t' ' ' | nl -ba -w1 -s' ' \
  | xargs -P"$JOBS" -I{} sh -c 'set -- {}; i=$1; shift;
      LLVM_PROFILE_FILE='"$WORK"'/raw/c$i.profraw \
      '"$BUILD"'/bin/logosc-cov "$@" -o '"$WORK"'/obj/$i.o >/dev/null 2>&1 || true'

# The four stdlib layers: the largest bodies of Logos in the tree, and the only
# population that is not a fixture somebody wrote to test one thing.
if [ "$LIMIT" -eq 0 ]; then
    echo "-- running the four stdlib layers"
    k=0
    for layer in lang mem lcm std; do
        k=$((k + 1))
        LLVM_PROFILE_FILE=$WORK/raw/s$k.profraw \
          "$BUILD/bin/logosc-cov" --emit-module "$ROOT/stdlib/$layer/logos.module" \
          -o "$WORK/obj/lib-$layer.a" >/dev/null 2>&1 || true
    done
fi

NRAW=$(find "$WORK/raw" -name '*.profraw' | wc -l)
echo "-- $NRAW profraw files from $NJOBS corpus compiles + stdlib layers"
if [ "$NRAW" -lt "$NJOBS" ]; then
    echo "coverage-map.sh: fewer profiles than compiles — some process wrote no" >&2
    echo "  profile (crash before exit, or a clobbered LLVM_PROFILE_FILE). The" >&2
    echo "  map would understate every count. Refusing to write it." >&2
    exit 4
fi

# ── 4. MERGE AND EXPORT ─────────────────────────────────────────────────────
echo "-- merging"
# ⚠ NOT `find | xargs llvm-profdata merge -o out`. xargs SPLITS a long argument
# list into several invocations and each one would rewrite `out`, so the merge of
# 8000 profiles would silently become the merge of the last chunk — a truncated
# profile presented as a complete one, which is the exact failure this whole tool
# is supposed to make impossible. `--input-files` takes the list as a FILE, one
# invocation, all of them.
find "$WORK/raw" -name '*.profraw' > "$WORK/raw.list"
"$PROFDATA" merge -sparse --input-files="$WORK/raw.list" -o "$WORK/$STEM.profdata"
"$COV" export "$WORK/logosc-cov" -instr-profile="$WORK/$STEM.profdata" "$SRC" \
  > "$WORK/$STEM.export.json"
"$COV" report "$WORK/logosc-cov" -instr-profile="$WORK/$STEM.profdata" "$SRC" \
  | tee "$WORK/$STEM.report.txt"

# The raw profiles and the corpus objects are ~1.7 GB for a full run and are
# spent once the merge is done. Left behind they are a trap of their own: the
# next person to look at `build/` finds gigabytes and no way to tell whether the
# tree is instrumented. The .profdata and the export JSON stay, so the map can
# be re-rendered without re-running the corpus.
rm -rf "$WORK/raw" "$WORK/obj" "$WORK/raw.list"

# ── 5. RENDER ───────────────────────────────────────────────────────────────
POP="ctest corpus ($NJOBS run_test.sh fixtures: tests/logos + tests/imported, pass and fail)"
[ "$LIMIT" -eq 0 ] && POP="$POP + the four stdlib layers (lang, mem, lcm, std)"
python3 "$ROOT/scripts/coverage_map_render.py" \
    --json "$WORK/$STEM.export.json" --source "$SRC" \
    --out-md "$OUTDIR/${STEM}_coverage_${DATE}.md" \
    --out-csv "$OUTDIR/${STEM}_regions_${DATE}.csv" \
    --population "$POP" --date "$DATE" --runs "$NRAW" \
    --binary "logosc-cov — $TU instrumented, every other TU as built (RelWithDebInfo, -O2)"

echo "== map: $OUTDIR/${STEM}_coverage_${DATE}.md"
echo "== rows: $OUTDIR/${STEM}_regions_${DATE}.csv"
echo "== build/ was NOT instrumented: one TU was compiled OUT OF TREE and linked"
echo "   into a separate binary. What is left in the build dir is $BUILD/bin/logosc-cov"
echo "   and $WORK (profdata + export JSON, for re-rendering). Neither is in any"
echo "   ctest population and neither is on ninja's graph. To drop both:"
echo "       rm -rf $WORK $BUILD/bin/logosc-cov"
