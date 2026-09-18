#!/usr/bin/env bash
# The Souffle oracle for the logosc Datalog engine (ADR 0028 S1, #420).
#
#   oracle.sh LOGOS_DL CASES_DIR RULES_DIR
#
# Every CASES_DIR/<case>/ holds prog.dl and its .facts files. Souffle and
# logos-dl both run on them; they must write the same set of .csv files and
# every relation must be equal as a set. A case with an expect/ directory is
# also compared against those hand-derived answers, which checks that the
# FACTS mean what the case says (Souffle cannot catch a wrong fixture).
#
# Exit 77 (ctest SKIP) when souffle is not installed: an absent oracle is
# reported as skipped, never as passed. Zero cases is a failure.
set -u

DL=$1
CASES=$2
RULES=$3

if ! command -v souffle >/dev/null 2>&1; then
    echo "SKIPPED: souffle not found on PATH; the oracle did not run"
    exit 77
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

n=0
fail=0
for dir in "$CASES"/*/; do
    [ -f "$dir/prog.dl" ] || continue
    name=$(basename "$dir")
    n=$((n + 1))
    s="$tmp/$name/souffle"
    l="$tmp/$name/logos-dl"
    mkdir -p "$s" "$l"

    if ! souffle -F "$dir" -D "$s" -I "$RULES" "$dir/prog.dl" >"$tmp/$name/s.log" 2>&1; then
        echo "FAIL $name: souffle failed"; sed 's/^/  /' "$tmp/$name/s.log"; fail=1; continue
    fi
    if ! "$DL" "$dir/prog.dl" -F "$dir" -D "$l" -I "$RULES" >"$tmp/$name/l.log" 2>&1; then
        echo "FAIL $name: logos-dl failed"; sed 's/^/  /' "$tmp/$name/l.log"; fail=1; continue
    fi

    s_files=$(cd "$s" && ls -1 *.csv 2>/dev/null | sort)
    l_files=$(cd "$l" && ls -1 *.csv 2>/dev/null | sort)
    if [ "$s_files" != "$l_files" ]; then
        echo "FAIL $name: different output relations"
        echo "  souffle:  $(echo $s_files)"; echo "  logos-dl: $(echo $l_files)"
        fail=1; continue
    fi
    if [ -z "$s_files" ]; then
        echo "FAIL $name: no .output relation"; fail=1; continue
    fi

    rows=0
    case_ok=1
    for f in $s_files; do
        if ! diff <(sort "$s/$f") <(sort "$l/$f") >"$tmp/$name/$f.diff"; then
            echo "FAIL $name: $f differs (< souffle, > logos-dl)"
            sed 's/^/  /' "$tmp/$name/$f.diff"; case_ok=0
        fi
        rows=$((rows + $(wc -l <"$s/$f")))
    done
    if [ -d "$dir/expect" ]; then
        for e in "$dir"/expect/*.csv; do
            f=$(basename "$e")
            if [ ! -f "$l/$f" ]; then
                echo "FAIL $name: expect/$f names a relation that is not an output"; case_ok=0; continue
            fi
            if ! diff <(sort "$e") <(sort "$l/$f") >"$tmp/$name/$f.expect.diff"; then
                echo "FAIL $name: $f differs from expect/ (< expected, > computed)"
                sed 's/^/  /' "$tmp/$name/$f.expect.diff"; case_ok=0
            fi
        done
    fi
    if [ "$case_ok" = 1 ]; then echo "ok   $name ($rows rows over $(echo $s_files | wc -w) relations)"
    else fail=1; fi
done

if [ "$n" = 0 ]; then
    echo "FAIL: no cases under $CASES"
    exit 1
fi
echo "oracle: $n case(s), $([ $fail = 0 ] && echo 'all agree' || echo 'FAILURES')"
exit $fail
