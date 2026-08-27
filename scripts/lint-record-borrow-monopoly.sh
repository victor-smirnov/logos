#!/usr/bin/env bash
# The BORROW-RECORD MONOPOLY gate.
#
# `record_borrow` is the ONE place this compiler deposits a borrow. Its two
# tails — `take_borrow_whole_` (path "") and `take_field_borrow_path_` (a
# non-empty dotted path) — are private in intent, and the trailing underscore
# says so, but intent is not a mechanism. This lint is the mechanism.
#
# WHY IT EXISTS. The whole/field choice used to be made by hand at sixteen call
# sites inside `take_ref_borrows`, each spelling `if (!bp.path.empty()) field
# else whole` plus its own exemption flag. Six pairs, four bare sites, four
# different spellings of ONE question — and the question "is this `&mut` legal
# through the reference we crossed?" was answered at some of them and not
# others. A seventeenth site added later would answer it at none. Making the
# tails reachable only through `record_borrow` is what turns "someone should
# check" into "the build fails".
#
# ⚠ A C++ `private:` CANNOT DO THIS JOB HERE: every caller is a member of the
# same class, so access control enumerates nothing. The population is a NAME,
# and a name is greppable, so the gate greps — but it greps for the CALL, and
# it names every offender rather than printing a count.
set -u
cd "$(dirname "$0")/.."
FILE=src/compiler/borrow_check.cpp

# ⚠ THE EXEMPTION IS A LINE RANGE, NOT A SPELLING, AND THE FIRST DRAFT PROVED
# WHY. It excluded any call spelled `take_*_(bp.root, bp.root_slot, ...)` — the
# shape record_borrow's own two calls happen to have — and an ABUSE PROBE that
# put a direct `take_borrow_whole_(bp.root, bp.root_slot, ...)` back at one of
# the sixteen former call sites passed the gate GREEN. An unchecked hatch is
# worse than no gate. The legal population is now "inside record_borrow's body"
# — a POSITION, which a copied call text cannot claim.
range=$(awk '
    /void record_borrow\(const BorrowPlace& bp/ { start=NR; depth=0; seen=0 }
    start && !end {
        n=gsub(/\{/,"{"); m=gsub(/\}/,"}")
        depth += n - m
        if (n) seen=1
        if (seen && depth<=0) { end=NR }
    }
    END { if (start && end) print start" "end; else print "0 0" }
' "$FILE")
set -- $range
lo=$1; hi=$2
if [ "$lo" = 0 ]; then
    echo "lint: could not locate record_borrow's body in $FILE — the gate cannot"
    echo "  say where a call is legal, so it reports itself broken rather than green."
    exit 1
fi

hits=$(grep -n "take_borrow_whole_(\|take_field_borrow_path_(" "$FILE" \
       | awk -F: -v lo="$lo" -v hi="$hi" '$1 < lo || $1 > hi' \
       | grep -vE "^[0-9]+:\s*void (take_borrow_whole_|take_field_borrow_path_)\(const std::string& target")
n=$(printf '%s' "$hits" | grep -c . || true)
if [ "$n" -ne 0 ]; then
    echo "lint: a borrow may only be deposited through record_borrow (lines $lo-$hi);"
    echo "      found $n call(s) to a tail outside it:"
    printf '%s\n' "$hits"
    echo "  Build a BorrowPlace and call record_borrow — the whole/field dispatch, the"
    echo "  union widening and the through-reference legality question are ITS job, and"
    echo "  a site that calls a tail directly silently opts out of all three."
    exit 1
fi

# And the record site itself must still exist, or the gate is green about nothing.
if ! grep -q "void record_borrow(const BorrowPlace& bp" "$FILE"; then
    echo "lint: record_borrow not found in $FILE — did the record site change shape?"
    exit 1
fi
echo "lint: borrow-record monopoly holds (record_borrow is the only depositor)"
