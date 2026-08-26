#!/usr/bin/env bash
# gate.sh [--update] — the findings baseline.
#
# ⚠ WHY A BASELINE AND NOT A THRESHOLD. The questions currently report 164 rows,
# and several are KNOWN to be false — triaged by hand, refuted with one-variable
# controls. A gate that fires on all of them teaches its reader to ignore it,
# which is strictly worse than no gate. A gate that fires on rows ABOVE some
# score is worse still: the score would be tuned until the output looked right,
# and a rule whose threshold is adjusted to taste has stopped being an oracle.
#
# So: every row is named, once, and the gate fires on the DIFFERENCE. New rows
# are red because nobody has looked at them. Vanished rows are red too, because
# the baseline has stopped being true — a defect was fixed and the record must
# say so. This is exactly the shape of tests/logos/bc_admits.ledger and
# logos_00_census_pin, both of which have earned their keep in this repo.
#
# ⚠ NOT A ctest TEST YET, and the reason is in the baseline itself: most rows are
# UNTRIAGED. Until that count is small, this gate would be asserting that a pile
# of unexamined rows is the correct state of the world. It is a ratchet first —
# it stops the pile GROWING silently — and a gate when the debt is paid.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD
BASE=tools/dlog/findings.baseline
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

# question:relation — the .csv each question puts its findings in.
QUESTIONS="place_walkers:spelling_keyed cluster_divergence:odd_one_out duty:neglects"

CUR=$(mktemp); trap 'rm -f "$CUR" "$CUR".*' EXIT
for qr in $QUESTIONS; do
    q=${qr%%:*}; r=${qr##*:}
    d=$(bash tools/dlog/ask.sh "$q.dl" 2>/dev/null | tail -1)
    [ -d "$d" ] || { echo "gate: '$q' produced no answer"; exit 2; }
    [ -f "$d/$r.csv" ] || { echo "gate: '$q' has no relation '$r'"; exit 2; }
    # ⚠ REFUSE ON AN EMPTY ANSWER rather than reporting a clean tree. Silence
    # from a question that has always had rows means the chain broke, not that
    # the compiler improved overnight.
    n=$(wc -l < "$d/$r.csv")
    [ "$n" -gt 0 ] || { echo "gate: '$q' reported ZERO rows — refusing to treat that as progress"; exit 2; }
    sed "s|^|$q\t|" "$d/$r.csv"
done | sort > "$CUR"

if [ "$UPDATE" = 1 ]; then
    { sed -n '1,/^# ---- rows below/p' "$BASE"
      # Carry every disposition and note forward; only genuinely new rows are
      # marked UNTRIAGED, so re-running --update never silently discards triage.
      while IFS= read -r row; do
          old=$(grep -F -m1 "$(printf '%s' "$row")" "$BASE" 2>/dev/null)
          if [ -n "$old" ]; then printf '%s\n' "$old"
          else printf 'UNTRIAGED\t%s\n' "$row"; fi
      done < "$CUR"
    } > "$BASE.new" && mv "$BASE.new" "$BASE"
    echo "gate: baseline updated — $(grep -c $'^[A-Z-]*\t' "$BASE") rows," \
         "$(grep -c '^UNTRIAGED' "$BASE") untriaged"
    exit 0
fi

[ -f "$BASE" ] || { echo "gate: no baseline; run gate.sh --update to seed it"; exit 2; }
sed -n '/^# ---- rows below/,$p' "$BASE" | grep -v '^# ---- rows below' |
    sed 's/^[A-Z-]*\t//' | sort > "$CUR.base"

RC=0
NEW=$(comm -23 "$CUR" "$CUR.base")
GONE=$(comm -13 "$CUR" "$CUR.base")
if [ -n "$NEW" ]; then
    echo "gate: NEW findings — nobody has looked at these:"
    printf '%s\n' "$NEW" | sed 's/^/    /'
    RC=1
fi
if [ -n "$GONE" ]; then
    echo "gate: findings that VANISHED — the baseline is no longer true."
    echo "      If a defect was fixed, say so: add a fixture and run --update."
    printf '%s\n' "$GONE" | sed 's/^/    /'
    RC=1
fi
[ "$RC" = 0 ] && echo "gate: $(wc -l < "$CUR") findings, all accounted for" \
                      "($(grep -c '^UNTRIAGED' "$BASE") still untriaged)"
exit $RC  # lint:exit-ok — RC is set only from the two explicit diffs above
