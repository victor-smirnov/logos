#!/usr/bin/env bash
# change-budget.sh — measure the SIZE of a change against a budget declared
# BEFORE it was written. Prospective home: an `lforge` subcommand.
#
# ── WHY THIS EXISTS ─────────────────────────────────────────────────────────
#
# A human cannot write sheets of code per second, and that expense is a
# REGULARIZER: if 200 lines cost an hour you go find the 20-line version —
# the mechanism that already exists rather than a parallel one, the root
# rather than N sites, deletion rather than addition. A generator has no such
# cost and defaults to "add a well-commented block for this case", which is
# locally reasonable and globally how a compiler ends up with 12 deposit sites
# for one fact, 5 of them writing the same two bits (task #87).
#
# The regularizer has to be supplied from outside, because a self-estimate is
# an oracle sharing the algorithm with the thing it grades: whoever wrote the
# change will rationalise its size. So this tool measures, and the number it
# is measured against must be stated BEFORE the work — the same discipline as
# predicting `ctest -N` before a reconfigure rather than explaining it after.
#
# ── WHAT IT COUNTS, AND WHY NOT LINES ALONE ─────────────────────────────────
#
# Line count is gameable by squishing: dense unreadable code and joined
# statements both shrink it. The counts below are the ones that survive
# reformatting, because they are about how much a READER must newly learn:
#
#   files   files touched                     — how spread the change is
#   add/del lines added / deleted             — kept, but never alone; `del`
#                                               matters as much as `add`: a
#                                               change that removes at least
#                                               what it adds is structurally
#                                               different from one that only
#                                               grows
#   names   NEW top-level definitions added   — the interface a reader must
#                                               learn: fn/struct/trait/enum/
#                                               class + LIR schema keys
#   branch  added `if`/`case`/`while`/`for`   — new decision points, i.e. new
#                                               cases; the #87 disease counted
#                                               directly
#
# ⚠ COMMENTS ARE NOT COUNTED IN `add`. This repo's comments deliberately carry
# measurements, control reverts and task numbers — information that is not in
# the code and must not be compressed out. The budget is on LOGIC, never on
# explanation. (`add_all` is reported too, so the split is visible.)
#
# ⚠ THE NAME COUNT IS A REGEX, AND SAYS SO. It sees `fn foo(`, `struct S {`
# and friends on ADDED lines. It cannot see a name introduced by a macro, and
# it will miscount a definition split across lines. It is a comparable
# quantity, not ground truth — which is enough, because the question is
# "bigger or smaller than what I predicted", not "exactly how many".
#
# ── USAGE ───────────────────────────────────────────────────────────────────
#
#   scripts/change-budget.sh                       # working tree vs HEAD (staged + unstaged)
#   scripts/change-budget.sh HEAD~3..HEAD          # a committed range
#   scripts/change-budget.sh --declare 'files=2 add=40 names=0' [<range>]
#   scripts/change-budget.sh --declare-file b.txt [<range>]
#
# EXIT: 0 within budget (or no budget given) · 1 over budget · 2 cannot measure.
set -uo pipefail

DECL=""
RANGE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --declare)      DECL="${2:?--declare needs a spec}"; shift 2 ;;
        --declare-file) DECL="$(cat "${2:?--declare-file needs a path}")" || exit 2; shift 2 ;;
        -h|--help)      sed -n '2,60p' "$0"; exit 0 ;;
        *)              RANGE="$1"; shift ;;
    esac
done

# git IS the oracle here, and deliberately: a self-estimate of "how big is this
# change" is an oracle sharing the algorithm with the thing it grades. The whole
# externality is that git, not the author, supplies the numbers.
git rev-parse --git-dir >/dev/null 2>&1  # lint:git-ok — git IS the subject, not evidence about one || { echo "FAIL(2): not a git repository — nothing to measure."; exit 2; }

# `git diff` with no range covers the working tree INCLUDING untracked files
# only if they are staged; a round that adds fixtures and forgets to `git add`
# would otherwise measure as smaller than it is. Refuse rather than undercount.
if [ -z "$RANGE" ] && [ -n "$(git ls-files --others --exclude-standard)" ]; then  # lint:git-ok — hygiene IS the question: an unstaged new file is part of the change and absent from the diff
    echo "FAIL(2): untracked files present — the measurement would be SMALLER than"
    echo "         the change. \`git add -N .\` (intent-to-add) or pass a range."
    git ls-files --others --exclude-standard | sed 's/^/           /'
    exit 2
fi

DIFF=$(git diff --unified=0 "${RANGE:-HEAD}" -- . 2>/dev/null)  # lint:git-ok — the diff IS the artefact measured
[ -n "$DIFF" ] || { echo "FAIL(2): empty diff for '${RANGE:-working tree}' — nothing to measure."; exit 2; }

read -r files add_all del <<<"$(printf '%s\n' "$DIFF" | awk '
    /^\+\+\+ /                 { f++ }
    /^\+/ && !/^\+\+\+ /       { a++ }
    /^-/  && !/^--- /          { d++ }
    END { printf "%d %d %d", f, a, d }')"

# Added lines that are neither blank nor comment-only — the logic half.
add=$(printf '%s\n' "$DIFF" | grep '^+' | grep -v '^+++' | sed 's/^+//' \
      | grep -cvE '^[[:space:]]*($|//|#|/\*|\*|///)' || true)

names=$(printf '%s\n' "$DIFF" | grep '^+' | grep -v '^+++' | sed 's/^+//' \
        | grep -cE '^[[:space:]]*(pub[[:space:]]+)?(static[[:space:]]+|inline[[:space:]]+|constexpr[[:space:]]+|extern[[:space:]]+)*([A-Za-z_][A-Za-z0-9_:<>,&*[:space:]]*[[:space:]]+)?(fn|struct|class|trait|enum|union)[[:space:]]+[A-Za-z_]' || true)

branch=$(printf '%s\n' "$DIFF" | grep '^+' | grep -v '^+++' | sed 's/^+//' \
         | grep -cE '(^|[^A-Za-z_])(if|case|while|for|else if)[[:space:]]*[({:]' || true)

printf 'MEASURED %s\n' "${RANGE:-working tree}"
printf '  files=%s  add=%s (add_all=%s)  del=%s  names=%s  branch=%s\n' \
       "$files" "$add" "$add_all" "$del" "$names" "$branch"
[ "$del" -ge "$add" ] 2>/dev/null \
    && printf '  note: deletes >= adds — the change is a simplification by size.\n'

[ -n "$DECL" ] || { echo "  (no budget declared — measurement only)"; exit 0; }

over=0
for kv in $DECL; do
    k=${kv%%=*}; want=${kv#*=}
    case "$k" in
        files) got=$files ;; add) got=$add ;; del) got=$del ;;
        names) got=$names ;; branch) got=$branch ;;
        *) echo "FAIL(2): unknown budget key '$k' (files|add|del|names|branch)"; exit 2 ;;
    esac
    if [ "$got" -gt "$want" ]; then
        over=1
        ratio=$(( want > 0 ? (got * 10 + want / 2) / want : 99 ))
        printf 'OVER  %s: declared %s, measured %s  (x%s.%s)\n' \
               "$k" "$want" "$got" "$((ratio/10))" "$((ratio%10))"
        [ "$ratio" -ge 20 ] && printf '      >2x over. That is a DESIGN SMELL to report, not to explain:\n      the shape is wrong, the same way a red gate means the change is wrong.\n'
    else
        printf 'ok    %s: declared %s, measured %s\n' "$k" "$want" "$got"
    fi
done
exit $over  # lint:exit-ok — `over` is set only to the literals 0 and 1, never a count or a status
