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
#           EXCEPT: this census's SUBJECT is files that get deleted, so it must
#           be able to say `eval.logos` after `eval.logos` is gone. A `GONE-FILE`
#           line in the pin block declares one, and FACTS 1 and 2 then let it
#           through — but FACT 7 immediately checks the declaration the OTHER
#           way, so it buys nothing except the right to name a corpse.
#
#   FACT 3  EVERY LIVE CENSUS ROW IS A REGISTERED TEST. The §3 table's fixture
#           column must name a file that exists AND has the `.expected` beside it
#           — registration is by GLOB over `pass/*.expected`, so a `.logos` with
#           no `.expected` is a fixture that silently does not run. "A test that
#           stops existing" is the failure mode this repo has already met.
#
#           EXCEPT a row declared `GONE-FIXTURE` in the pin block. P5 deleted the
#           interpreter and 53 of the 85 rows died with their subject; the table
#           keeps them because it IS the loss ledger, and a loss ledger with the
#           losses removed is a list of survivors. The exemption is not a hole:
#           FACT 8 immediately checks it the other way, exactly as FACT 7 does for
#           `GONE-FILE`. Every one of the 85 rows is therefore checked in one
#           direction or the other, and a row cannot move between the two without
#           an explicit edit to the pin block.
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
#   FACT 6  THE CUT STAYS CUT, AND THE MENTIONS STAY ACCOUNTED FOR. RE-AIMED BY
#           P5, FROM BACKWARD TO FORWARD — the one fact whose QUESTION changed
#           rather than its numbers. It used to ask "is the census population
#           complete?": grep tests/ for the cut's symbols and require the answer
#           to be exactly the rows plus the NOT-AFFECTED lines. After the deletion
#           that is a question about a table that has become history — 53 of the
#           rows name files that no longer exist, so the old form would have
#           reported 53 disagreements on a tree where nothing had drifted.
#           Weakening it was not on the table and neither was deleting it, so it
#           asks the question that still has teeth:
#
#             (i)  NO `CUT-SYMBOL` IS DEFINED ANYWHERE UNDER `stdlib/`, AND NONE
#                  IS NAMED AS A STRING LITERAL UNDER `src/`. The RESURRECTION
#                  check. The C++ fallback seed in
#                  `SemaChecker::seed_builtin_source_impls` hard-named `IncrRec`
#                  and four `deem_state_*` materializers in string literals and
#                  was CONTENT-guarded on a stdlib file P5 deleted — so deleting
#                  the file alone would have re-registered four materializers that
#                  do not exist, and the program would have died at LINK rather
#                  than at sema. That is the defect this half is pointed at, and
#                  it is falsifiable on day one.
#
#             (ii) EVERY FILE UNDER tests/ THAT MENTIONS A `CUT-SYMBOL` IS
#                  ACCOUNTED FOR: a LIVE census row, or a declared NOT-AFFECTED
#                  line. And every declared NOT-AFFECTED file must still be found
#                  — that direction is unchanged and is what caught the three
#                  `FactStore` fixtures. A live row need NOT be found: after the
#                  rewrite most of them stopped mentioning the cut at all, which
#                  is what the rewrite was for.
#
#   FACT 7  EVERY DECLARED CORPSE IS ACTUALLY DEAD. `GONE-FILE <path> <why>`
#           exempts a name from FACTS 1 and 2 — so the exemption is checked in
#           the direction that can be abused: the path must NOT exist. Declaring
#           a live file gone (to silence FACT 1) reds here instead, and if a
#           deleted file is ever restored the census learns about it the same
#           day. A `GONE-FILE` also has to carry a reason, because "deleted at
#           <sha>, <why>" is the sentence FACT 2 was going to force anyway.
#
#   FACT 8  EVERY DECLARED DEAD FIXTURE IS ACTUALLY DEAD — the abuse direction
#           FACT 7 guards for files, applied to the §3 rows P5 killed.
#           `GONE-FIXTURE <path> <why>` exempts a row from FACT 3, so the path
#           must NOT exist, its `.expected` sibling must NOT exist either (a
#           `.logos` deleted while its `.expected` survives leaves a registered
#           test with no program), and the line must carry a reason. The 53-row
#           loss ledger stays MACHINE-CHECKED instead of turning into prose, and
#           the failure this repo has already met — a fixture that silently stops
#           existing — is caught in the opposite direction too: restore one and
#           this reds the same day.
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

# The declared corpses: `GONE-FILE <repo-relative path>  <why>`. Two views —
# the paths (for FACT 1 and FACT 7) and their basenames (for FACT 2).
gone_paths()  { pin_get "$1" GONE-FILE | awk '{print $1}'; }
gone_bases()  { gone_paths "$1" | sed 's#.*/##'; }
# The declared dead FIXTURES: `GONE-FIXTURE <repo-relative path>  <why>`.
gonefx_paths() { pin_get "$1" GONE-FIXTURE | awk '{print $1}'; }

check_paths() {                                   # FACT 1
    local f=$1 p q
    # A declared corpse may be NAMED by path — FACT 7 (files) and FACT 8
    # (fixtures) own the check that it really is one. The GONE-FIXTURE lines are
    # themselves path tokens in this file, so without this they would red FACT 1
    # by existing.
    { gone_paths "$f"; gonefx_paths "$f"; } | sort -u > "$TMPD/gone_p"
    while read -r p; do
        [ -n "$p" ] || continue
        grep -qxF "$p" "$TMPD/gone_p" && continue      # declared gone; FACT 7 owns it
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
    { gone_bases "$f"; gonefx_paths "$f" | sed 's#.*/##'; } | sort -u > "$TMPD/gone_b"
    while read -r b; do
        [ -n "$b" ] || continue
        n=$(grep -cxF "$b" "$TMPD/basenames")
        # A declared corpse may be named bare, but ONLY while it is really gone:
        # if the tree has one again, fall through and let the count speak.
        [ "$n" = 0 ] && grep -qxF "$b" "$TMPD/gone_b" && continue
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
    gonefx_paths "$f" | sort -u > "$TMPD/gone_fx"
    while IFS=$'\t' read -r p cls; do
        [ -n "$p" ] || continue
        grep -qxF "$p" "$TMPD/gone_fx" && continue     # declared dead; FACT 8 owns it
        if [ ! -e "$ROOT/$p" ]; then
            note "census row names \`$p\`, which does not exist.
      If it died with its subject, say so with a GONE-FIXTURE line in the pin
      block — that is the exemption, and FACT 8 then checks it the other way."
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
    [ -n "$syms" ] || { note "the pin block declares no CUT-SYMBOL, so the
      resurrection check and the mention census are not executable and the cut is
      whatever someone remembered."; return; }
    re="\\b(${syms})\\b"

    # (i) THE RESURRECTION CHECK.
    local defs lits
    defs=$(cd "$ROOT" && grep -rnE "^(pub )?(fn|struct|enum|trait|const|static|type|impl) +(${syms})\\b" \
             --include='*.logos' stdlib/ 2>/dev/null)
    if [ -n "$defs" ]; then
        note "a CUT-SYMBOL is DEFINED again under stdlib/. The cut is the claim
      this census records; a definition coming back is either the capability being
      restored (then rewrite the census, do not delete this check) or a name
      collision (then rename). Sites:"
        printf '%s\n' "$defs" | sed 's/^/      /'
    fi
    lits=$(cd "$ROOT" && grep -rnE "\"(${syms})\"" src/ 2>/dev/null)
    if [ -n "$lits" ]; then
        note "a CUT-SYMBOL appears as a STRING LITERAL under src/. The compiler
      does not resolve those against the stdlib, so a fallback that hard-names a
      deleted type registers materializers that do not exist and the program dies
      at LINK, not at sema. Sites:"
        printf '%s\n' "$lits" | sed 's/^/      /'
    fi

    # (ii) THE MENTION CENSUS. Cached per distinct symbol list, so the canaries
    # do not perturb the expensive grep.
    local key
    key=$(printf '%s' "$re" | cksum | tr -d ' /')
    if [ ! -f "$TMPD/pop.$key" ]; then
        (cd "$ROOT" && grep -rlE "$re" tests/ 2>/dev/null) | sort > "$TMPD/pop.$key"
    fi
    cp "$TMPD/pop.$key" "$TMPD/pop_got"
    gonefx_paths "$f" | sort -u > "$TMPD/gone_fx6"
    : > "$TMPD/pop_want"
    if [ -s "$TMPD/gone_fx6" ]; then
        table_rows "$f" | cut -f1 | grep -vxF -f "$TMPD/gone_fx6" >> "$TMPD/pop_want"
    else
        table_rows "$f" | cut -f1 >> "$TMPD/pop_want"
    fi
    pin_get "$f" NOT-AFFECTED | awk '{print $1}' >> "$TMPD/pop_want"
    sort -u -o "$TMPD/pop_want" "$TMPD/pop_want"

    local extra
    extra=$(comm -13 "$TMPD/pop_want" "$TMPD/pop_got")
    if [ -n "$extra" ]; then
        note "a file under tests/ mentions a symbol from the cut and is neither a
      LIVE census row nor a declared NOT-AFFECTED line. Anything else is a fixture
      nobody weighed — which is how three FactStore fixtures went uncounted."
        printf '%s\n' "$extra" | sed 's/^/      tree only:   /'
    fi
    local missing
    missing=$(pin_get "$f" NOT-AFFECTED | awk '{print $1}' | sort -u \
              | grep -v '^$' | grep -vxF -f "$TMPD/pop_got")
    if [ -n "$missing" ]; then
        note "a declared NOT-AFFECTED file no longer mentions any cut symbol (or
      no longer exists). The declaration was a MEASURED exemption, so it expires
      with the mention it exempted; drop the line."
        printf '%s\n' "$missing" | sed 's/^/      census only: /'
    fi
}

check_gone() {                                    # FACT 7
    local f=$1 line p why
    while read -r line; do
        [ -n "$line" ] || continue
        p=${line%%[[:space:]]*}
        why=$(printf '%s' "$line" | sed -E 's/^[^[:space:]]+[[:space:]]*//')
        if [ -e "$ROOT/$p" ]; then
            note "the pin block declares \`$p\` GONE-FILE, but it EXISTS.
      That declaration exempts the name from FACTS 1 and 2, so a live file
      declared dead is a hole punched in the pin. Either the file came back (say
      so in the prose and drop the GONE-FILE line) or the declaration was a way
      of silencing FACT 1, which is the thing this gate is for."
        fi
        [ -n "$why" ] || note "the GONE-FILE line for \`$p\` carries no reason.
      A corpse is nameable only with its cause of death — the sha that removed it
      and where its contents went — because that is the sentence a reader needs
      and FACT 2 was going to force it anyway."
    done < <(pin_get "$f" GONE-FILE)
}

check_gone_fixture() {                            # FACT 8
    local f=$1 line p why exp
    while read -r line; do
        [ -n "$line" ] || continue
        p=${line%%[[:space:]]*}
        why=$(printf '%s' "$line" | sed -E 's/^[^[:space:]]+[[:space:]]*//')
        if [ -e "$ROOT/$p" ]; then
            note "the pin block declares \`$p\` GONE-FIXTURE, but it EXISTS.
      That declaration exempts the row from FACT 3, so a live fixture declared
      dead is a hole punched in the pin — and a restored fixture the census still
      counts as a loss is a ledger that overstates the cost."
        fi
        exp="${p%.logos}.expected"
        if [ -e "$ROOT/$exp" ]; then
            note "\`$p\` is declared GONE-FIXTURE but \`$exp\` is still there.
      Registration is a GLOB over *.expected: an orphaned expectation is a
      registered test with no program."
        fi
        [ -n "$why" ] || note "the GONE-FIXTURE line for \`$p\` carries no reason.
      This table is the LOSS LEDGER; a row with no cause of death records that
      something was removed and not what was given up."
    done < <(pin_get "$f" GONE-FIXTURE)
}

check_all() {
    check_paths "$1"; check_bare "$1"; check_rows "$1"
    check_arithmetic "$1"; check_registry "$1"; check_population "$1"
    check_gone "$1"; check_gone_fixture "$1"
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

# ⚠ `path` NO LONGER USES exec.logos: P5 deleted it, so it is a declared
# GONE-FILE and FACT 1 lets it through — the canary would plant nothing. It is
# re-pointed at the SURVIVING tpl.logos, which is also the file whose survival
# the whole C2 port was for.
canary path       's#stdlib/mem/deem/tpl\.logos#stdlib/mem/deem/tpl_GONE.logos#'
canary bare       's#`graphsrc\.logos`#`graphsrc_NOSUCH.logos`#'
# ⚠ `row` USED TO TARGET row 11 `query_diff_fuzz`, which P5 deleted: the row is
# now a declared GONE-FIXTURE that FACT 3 skips, so the canary would have planted
# a defect nothing looks at. Re-pointed at row 1, a LIVE rewritten fixture.
canary row        's#^\| 1 \| `adv_rec_tc`#| 1 | `adv_rec_tc_nope`#'
canary arithmetic 's#^CENSUS-ROWS( +)[0-9]+#CENSUS-ROWS\1999#'
canary registry   's#^REGISTRY-ALL( +)[0-9]+#REGISTRY-ALL\19999999#'
canary population 's#^NOT-AFFECTED#NOT-AFFECTED-DISABLED#'

# A canary for the OTHER direction: a LIVE row DELETED from the table must read as
# a disagreement, not as a smaller census — FACT 4's row count moves and FACT 6
# then finds a file that mentions a cut symbol and is nobody's row.
# ⚠ RE-POINTED: it used to sed row 13 `query_diff_str_adv`, which P5 deleted.
canary row-deleted 's#^\| 4 \| `deem_incr_diff_harness`.*$##'

# FACT 7, both directions. `gone-live` declares a file that IS there — the abuse
# the exemption invites, and the reason FACT 7 exists at all. `gone-mute` strips
# the reason. Neither may pass.
# ⚠ `gone-live` USED TO PLANT stdlib/mem/deem/incr.logos as its live-file abuse
# case. After P5 that file is a genuine corpse, so the canary would have asserted
# nothing at all — it is re-pointed at stdlib/mem/deem/tpl.logos, which survives
# the cut by construction.
canary gone-live 's#^GONE-FILE( +)stdlib/mem/deem/eval\.logos#GONE-FILE\1stdlib/mem/deem/tpl.logos#'
canary gone-mute 's#^(GONE-FILE +stdlib/mem/deem/eval\.logos).*$#\1#'

# FACT 8, both directions, the same shape as FACT 7's pair. `gonefx-live`
# declares a fixture that IS there — the abuse the FACT 3 exemption invites, and
# the whole reason FACT 8 exists. `gonefx-mute` strips the reason, which is what
# turns a loss ledger into a list of removals.
canary gonefx-live 's#^GONE-FIXTURE( +)tests/logos/pass/query_diff_fuzz\.logos#GONE-FIXTURE\1tests/logos/pass/adv_rec_tc.logos#'
canary gonefx-mute 's#^(GONE-FIXTURE +tests/logos/pass/query_diff_fuzz\.logos).*$#\1#'

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
echo "  registered fixture (.logos + .expected) unless declared GONE-FIXTURE;"
echo "  the header count, the row count"
echo "  and the per-class totals agree; the registry baseline is this tree's"
echo "  ($CT_ALL all / $CT_NOIMP -LE imported / $CT_TIER tier_commit); and the"
echo "  $n_syms pinned cut symbols are DEFINED nowhere under stdlib/ and named as"
echo "  a string literal nowhere under src/, and every file under tests/ that"
echo "  mentions one is a live row or one of $n_na declared NOT-AFFECTED file(s)."
n_gone=$(pin_get "$CENSUS" GONE-FILE | grep -c .)
n_gfx=$(pin_get "$CENSUS" GONE-FIXTURE | grep -c .)
echo "  $n_gone declared GONE-FILE(s) are really gone and each says why."
echo "  $n_gfx declared GONE-FIXTURE(s) are gone with their .expected, each with a"
echo "  cause of death. Eleven self-canaries live."
exit 0
