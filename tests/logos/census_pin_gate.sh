#!/usr/bin/env bash
# census_pin_gate.sh REPO_ROOT CENSUS CTEST BUILD_DIR
#
# A CENSUS THAT NO GATE READS IS A DOCUMENT ABOUT THE PAST.
#
# `docs/deem-interpreter-deletion-census.md` is the measurement that P5 (delete
# the Deem interpreter) is planned against: which files are in the cut, which
# fixtures die with it, what the registry looked like before. Every number in it
# was measured once and then went out of date, TWICE, silently — a registry
# baseline that ctest no longer agreed with, three fixtures the population rule
# should have caught and did not, a `wql_domain_static_*` sibling built under a
# name the census never learned. Nothing went red, because prose has no exit
# code.
#
# The precedent is `tests/logos/shared_ref_ub_lint.sh`: a row of that ledger
# storing a PATH was caught drifting twice, immediately, because a path is
# machine-checkable, while the sentence next to it saying the same thing was not
# caught at all. This gate applies that to the whole census. It does NOT judge
# prose. It checks every claim in the census that is DECIDABLE:
#
#   FACT 1  EVERY PATH EXISTS. Every `docs/…` `tests/…` `stdlib/…` `src/…`
#           `tools/…` `scripts/…` `abi/…` token in the file names something that
#           is there. Brace lists (`{check,exec}.logos`) are expanded; globs are
#           required to match at least one file.
#
#   FACT 2  EVERY BARE FILENAME RESOLVES, UNIQUELY. The census talks about
#           `incr.logos` without a directory 20 times. A bare name is only
#           decidable if the tree answers it: exactly one file must carry it.
#           ZERO is the drift this gate exists for (a deleted file still being
#           discussed in the present tense); TWO OR MORE means the sentence is
#           ambiguous and must be written with its path.
#
#   FACT 3  EVERY CENSUS ROW IS A REGISTERED TEST. The §3 table's fixture column
#           must name a file that exists AND has the `.expected` beside it —
#           registration is by GLOB over `pass/*.expected`, so a `.logos` with no
#           `.expected` is a fixture that silently does not run. "A test that
#           stops existing" is the failure mode this repo has already met.
#
#   FACT 4  THE TABLE'S OWN ARITHMETIC. The section header's file count, the
#           number of table rows, and the per-class totals line are three
#           statements of one number. They are checked against each other and
#           against the class column actually written in the rows.
#
#   FACT 5  THE REGISTRY BASELINE IS TODAY'S. `ctest -N` is run, three ways
#           (all / `-LE imported` / `-L '^tier_commit$'`), and held against the
#           pinned counts. THIS ONE GOES RED WHENEVER ANY TEST IS ADDED
#           ANYWHERE, on purpose and by design: the census's whole method is
#           "PREDICT the registry count before the cut and compare after", which
#           is worth nothing measured against a stale number. The fix is one line
#           in the pin block, and the failure message prints the exact values.
#
#   FACT 6  THE POPULATION, IN BOTH DIRECTIONS. §3 states its own population
#           rule: grep tests/ for the CUT's symbols, not the interpreter's entry
#           points. That rule is executed here over the symbol list pinned in the
#           census itself. Every file it finds must be either a census row or an
#           explicitly declared NOT-AFFECTED line — and every declared file must
#           still be found. A new fixture touching the cut cannot be invisible to
#           the census any more; nor can a censused one be quietly deleted.
#
# AND IT PROVES ITSELF, in the same run: each fact is re-measured through the
# SAME reader on a PLANTED census whose answer is known. A pin that cannot fail
# is the original defect one level up.
set -u

ROOT=${1:?usage: census_pin_gate.sh <repo root> <census.md> <ctest> <build dir>}
CENSUS=${2:?usage: census_pin_gate.sh <repo root> <census.md> <ctest> <build dir>}
CTEST=${3:?usage: census_pin_gate.sh <repo root> <census.md> <ctest> <build dir>}
BUILD=${4:?usage: census_pin_gate.sh <repo root> <census.md> <ctest> <build dir>}

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

broken() { echo "GATE BROKEN: $*"; exit 4; }
[ -f "$CENSUS" ] || broken "the census $CENSUS does not exist. If it was renamed,
  this gate must be repointed in tests/logos/CMakeLists.txt; if it was DELETED,
  say so in the commit — it is the only record of what P5 costs."
[ -d "$ROOT" ] || broken "repo root $ROOT is not a directory."

SEARCH_ROOTS="stdlib src tools tests docs abi scripts"

# ── READERS ──────────────────────────────────────────────────────────────────
# Everything that reads the census lives here, so the canaries below exercise
# the same code as the real measurement.

# The pin block: fenced ```pin … ``` , `KEY  value…` lines, `#` comments.
pin_lines() { awk '/^```pin$/{p=1;next} /^```$/{p=0} p' "$1" | grep -vE '^\s*(#|$)'; }
pin_get()   { pin_lines "$1" | awk -v k="$2" '$1==k {$1=""; sub(/^ +/,""); print}'; }

# The §3 table: rows are `| <n> | `name` | … | <class> | … |`. Prints
# `<repo-relative path>\t<class>` per row, in file order.
table_rows() {
    awk -F'|' '
        /^\| *[0-9]+ *\|/ {
            name = $3; cls = $6;
            gsub(/`/, "", name); gsub(/^ +| +$/, "", name);
            gsub(/`/, "", cls);  gsub(/^ +| +$/, "", cls);
            sub(/ .*$/, "", cls);                    # "B K" -> "B"
            if (name !~ /\//) name = "tests/logos/pass/" name ".logos";
            printf "%s\t%s\n", name, cls;
        }' "$1"
}

# Path-shaped tokens (those carrying a directory).
path_tokens() {
    grep -oE '(docs|tests|stdlib|src|tools|scripts|abi)/[A-Za-z0-9_./{},*-]*[A-Za-z0-9_}*]' "$1" | sort -u
}

# Bare filenames: a known extension, NOT preceded by a path character.
bare_tokens() {
    grep -oE '(^|[^A-Za-z0-9_./-])[A-Za-z0-9_][A-Za-z0-9_-]*\.(logos|sh|cpp|md|abi|ledger|json|check)' "$1" \
        | sed -E 's/^[^A-Za-z0-9_]//' | sort -u
}

# ── the checker: prints FAIL lines, returns the number of failures ───────────
fail=0
note() { echo "FAIL: $*"; fail=$((fail + 1)); }

check_paths() {                                   # FACT 1
    local f=$1 p q
    while read -r p; do
        [ -n "$p" ] || continue
        for q in $(eval echo "$ROOT/$p" 2>/dev/null); do
            compgen -G "$q" > /dev/null && continue
            [ -e "$q" ] && continue
            note "the census names \`$p\`, which does not exist.
      Either the file moved (fix the sentence) or it was deleted (then the
      paragraph around it is describing a tree that is gone)."
        done
    done < <(path_tokens "$f")
}

check_bare() {                                    # FACT 2
    local f=$1 b n
    while read -r b; do
        [ -n "$b" ] || continue
        n=$(grep -cxF "$b" "$TMPD/basenames")
        if [ "$n" = 0 ]; then
            note "the census writes the bare filename \`$b\`, and no such file
      exists anywhere in the tree. This is the drift shape exactly: a file gets
      moved or deleted and the prose keeps discussing it in the present tense."
        elif [ "$n" != 1 ]; then
            note "the bare filename \`$b\` matches $n files in the tree, so no
      reader — human or gate — can tell which one the sentence means. Write it
      with its path."
        fi
    done < <(bare_tokens "$f")
}

check_rows() {                                    # FACT 3
    local f=$1 p cls exp
    while IFS=$'\t' read -r p cls; do
        [ -n "$p" ] || continue
        if [ ! -e "$ROOT/$p" ]; then
            note "census row names \`$p\`, which does not exist."
            continue
        fi
        case "$p" in
            tests/logos/pass/*.logos|tests/logos/fail/*.logos)
                exp="${p%.logos}.expected"
                [ -e "$ROOT/$exp" ] || note "census row \`$p\` has no
      \`$(basename "$exp")\` beside it. Registration is a GLOB over *.expected,
      so this fixture is NOT a registered test — it is a file that compiles
      nowhere and runs never, while the census counts it as coverage." ;;
        esac
    done < <(table_rows "$f")
}

check_arithmetic() {                              # FACT 4
    local f=$1 hdr rows pinned c want got sum
    hdr=$(sed -nE 's/^## 3\. Fixture census — ([0-9]+) files.*$/\1/p' "$f")
    rows=$(table_rows "$f" | grep -c .)
    pinned=$(pin_get "$f" CENSUS-ROWS)
    [ -n "$hdr" ] || note "§3's header does not carry a file count in the form
      '## 3. Fixture census — <n> files'; the gate cannot hold the prose total
      against the table."
    [ -n "$hdr" ] && [ "$hdr" != "$rows" ] && note "§3's header says $hdr files
      and the table has $rows rows."
    [ -n "$pinned" ] || note "the pin block has no CENSUS-ROWS."
    [ -n "$pinned" ] && [ "$pinned" != "$rows" ] && note "the pin block says
      CENSUS-ROWS $pinned; the table has $rows rows."
    sum=0
    for c in A B C D G; do
        want=$(pin_get "$f" "CLASS-$c")
        got=$(table_rows "$f" | awk -F'\t' -v c="$c" '$2==c' | grep -c .)
        sum=$((sum + got))
        if [ -z "$want" ]; then
            note "the pin block has no CLASS-$c count."
        elif [ "$want" != "$got" ]; then
            note "the pin block says CLASS-$c is $want; the table's class column
      says $got. The totals line and the rows are two statements of one number
      and they have to agree, or the loss ledger is sized off the wrong one."
        fi
    done
    [ "$sum" = "$rows" ] || note "the class column covers $sum of $rows rows —
      some row's class cell is not one of A B C D G."
}

check_registry() {                                # FACT 5
    local f=$1 pin_all pin_noimp pin_tier
    pin_all=$(pin_get "$f" REGISTRY-ALL)
    pin_noimp=$(pin_get "$f" REGISTRY-NOIMPORTED)
    pin_tier=$(pin_get "$f" REGISTRY-TIERCOMMIT)
    [ -n "$pin_all$pin_noimp$pin_tier" ] || { note "the pin block carries no
      REGISTRY-* counts, so §7's 'predict the count before the cut' has nothing
      to predict against."; return; }
    if [ "$pin_all" != "$CT_ALL" ] || [ "$pin_noimp" != "$CT_NOIMP" ] || [ "$pin_tier" != "$CT_TIER" ]; then
        note "the pinned registry baseline is not this tree's.
      pinned:   ALL $pin_all / -LE imported $pin_noimp / tier_commit $pin_tier
      measured: ALL $CT_ALL / -LE imported $CT_NOIMP / tier_commit $CT_TIER
      THIS IS NOT AN ACCUSATION: adding a test anywhere in the repo moves these,
      and that is the point — a baseline nobody re-measures is what made §7
      useless twice. The fix is three lines in the pin block:
        REGISTRY-ALL         $CT_ALL
        REGISTRY-NOIMPORTED  $CT_NOIMP
        REGISTRY-TIERCOMMIT  $CT_TIER
      Update them in the same commit that adds the test, and if the delta is NOT
      what you expected, that is the gate earning its keep."
    fi
}

check_population() {                              # FACT 6
    local f=$1 syms re
    syms=$(pin_get "$f" CUT-SYMBOL | tr '\n' '|' | sed 's/|$//')
    [ -n "$syms" ] || { note "the pin block declares no CUT-SYMBOL, so §3's
      population rule ('grep the CUT's symbols, not the entry points') is not
      executable and the census population is whatever someone remembered."; return; }
    re="\\b(${syms})\\b"
    # The grep over tests/ is the expensive part and depends ONLY on the symbol
    # list, so it is cached per distinct list — the canaries do not perturb it.
    local key
    key=$(printf '%s' "$re" | cksum | tr -d ' /')
    if [ ! -f "$TMPD/pop.$key" ]; then
        (cd "$ROOT" && grep -rlE "$re" tests/ 2>/dev/null) | sort > "$TMPD/pop.$key"
    fi
    cp "$TMPD/pop.$key" "$TMPD/pop_got"
    { table_rows "$f" | cut -f1; pin_get "$f" NOT-AFFECTED | awk '{print $1}'; } \
        | sort -u > "$TMPD/pop_want"
    if ! diff -q "$TMPD/pop_want" "$TMPD/pop_got" > /dev/null; then
        note "the census population and the tree disagree.
      A file under tests/ that mentions a symbol from the cut is either a census
      ROW (it is affected) or a declared NOT-AFFECTED line (the mention is a
      comment or a ledger entry). Anything else is a fixture nobody weighed —
      which is how three FactStore fixtures went uncounted, twice."
        diff "$TMPD/pop_want" "$TMPD/pop_got" \
            | sed 's/^</  census only: /; s/^>/  tree only:   /' \
            | grep -E 'census only|tree only'
    fi
}

check_all() {
    check_paths "$1"; check_bare "$1"; check_rows "$1"
    check_arithmetic "$1"; check_registry "$1"; check_population "$1"
}

# ── the tree index, once ─────────────────────────────────────────────────────
# FACT 2 asks "how many files carry this basename" up to 20 times per census and
# eight times over (seven canaries + the real run). One index, not 160 finds.
(cd "$ROOT" && find $SEARCH_ROOTS -type f -printf '%f\n' 2>/dev/null) | sort > "$TMPD/basenames"
[ -s "$TMPD/basenames" ] || broken "the tree index is empty — \`find\` over
  $SEARCH_ROOTS under $ROOT returned nothing, so FACT 2 would call every bare
  filename in the census missing."

# ── the registry measurement, once ───────────────────────────────────────────
ctest_count() {
    local out rc
    out=$("$CTEST" --test-dir "$BUILD" -N "$@" 2>/dev/null); rc=$?
    [ "$rc" = 0 ] || return 1
    printf '%s\n' "$out" | sed -nE 's/^Total Tests: ([0-9]+)$/\1/p'
}
CT_ALL=$(ctest_count)      || broken "\`$CTEST --test-dir $BUILD -N\` failed."
CT_NOIMP=$(ctest_count -LE imported)   || broken "ctest -N -LE imported failed."
CT_TIER=$(ctest_count -L '^tier_commit$') || broken "ctest -N -L tier_commit failed."
[ -n "$CT_ALL" ] && [ "$CT_ALL" -gt 100 ] 2>/dev/null || \
    broken "ctest -N reported '$CT_ALL' tests. A tiny or unparsable total means
  the reader is wrong, not that the registry emptied — and FACT 5 would then
  'pass' against any census."

# ── SELF-CANARIES ────────────────────────────────────────────────────────────
# Each plants one defect in a COPY of the real census and demands this same
# checker sees it. They run BEFORE the real check so a dead reader reports the
# gate broken instead of reporting the census clean.
canary() {                                        # canary <name> <sed program>
    local name=$1 prog=$2 n
    sed -E "$prog" "$CENSUS" > "$TMPD/canary.md"
    if cmp -s "$TMPD/canary.md" "$CENSUS"; then
        broken "canary '$name' did not modify the census — its pattern no longer
  matches, so it has been measuring nothing. Re-point it at the real text."
    fi
    n=$( fail=0; check_all "$TMPD/canary.md" > /dev/null 2>&1; echo "$fail" )
    [ "$n" -gt 0 ] || broken "canary '$name' planted a defect the checker did NOT
  see. Whatever it is blind to there, it is blind to in the real census."
}

canary path       's#stdlib/mem/deem/exec\.logos#stdlib/mem/deem/exec_GONE.logos#'
canary bare       's#`incr\.logos`#`incr_NOSUCH.logos`#'
canary row        's#^\| 11 \| `query_diff_fuzz`#| 11 | `query_diff_fuzz_nope`#'
canary arithmetic 's#^CENSUS-ROWS( +)[0-9]+#CENSUS-ROWS\1999#'
canary registry   's#^REGISTRY-ALL( +)[0-9]+#REGISTRY-ALL\19999999#'
canary population 's#^NOT-AFFECTED#NOT-AFFECTED-DISABLED#'

# A canary for the OTHER direction of FACT 6: a row DELETED from the table must
# read as a population disagreement, not as a smaller census.
canary row-deleted 's#^\| 13 \| `query_diff_str_adv`.*$##'

# ── THE REAL CHECK ───────────────────────────────────────────────────────────
fail=0
check_all "$CENSUS"
[ "$fail" -ne 0 ] && exit 1

n_rows=$(table_rows "$CENSUS" | grep -c .)
n_paths=$(path_tokens "$CENSUS" | grep -c .)
n_bare=$(bare_tokens "$CENSUS" | grep -c .)
n_syms=$(pin_get "$CENSUS" CUT-SYMBOL | grep -c .)
n_na=$(pin_get "$CENSUS" NOT-AFFECTED | grep -c .)
echo "census pin: $(basename "$CENSUS") holds. $n_paths path token(s) and"
echo "  $n_bare bare filename(s) resolve; $n_rows table rows each name a"
echo "  registered fixture (.logos + .expected); the header count, the row count"
echo "  and the per-class totals agree; the registry baseline is this tree's"
echo "  ($CT_ALL all / $CT_NOIMP -LE imported / $CT_TIER tier_commit); and the"
echo "  population derived from $n_syms pinned cut symbols matches the rows plus"
echo "  $n_na declared NOT-AFFECTED file(s), in both directions."
echo "  Seven self-canaries live."
exit 0
