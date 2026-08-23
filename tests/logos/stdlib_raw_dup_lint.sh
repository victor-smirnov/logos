#!/usr/bin/env bash
# stdlib_raw_dup_lint.sh REPO_ROOT LEDGER
#
# #112 — THE `*((&x) as *const T)` DUPLICATE-OWNER CLASS, PINNED.
#
# The shape reads an OWNING value out of a place through a raw pointer, producing a
# second owner of one allocation. Every occurrence is a double free waiting for a
# droppable payload, and the whole class was invisible to a green corpus by
# construction: every in-tree user of the affected combinators carried a `Copy`
# payload, so a droppable one never reached a site. MEASURED before the repair:
# `Option::Some(mk(7)).filter(keep)` with `struct Inner { n: i64, v: Vec<i64> }` and a
# printing `impl Drop` printed `DROP n=7` twice and died `free(): double free detected
# in tcache 2`, rc 134.
#
# The shape is a GREP, so the population is mechanically enumerable, and this lint
# derives it from the tree rather than listing it.
#
# ── WHY THE BROAD GREP, AND WHY THAT IS FACT 0 ───────────────────────────────
# The census that opened #112 was handed the grep `\*\(\(&[a-z_]*\) as \*const `. That
# pattern anchors the place on `[a-z_]*`, so it CANNOT MATCH A DOTTED RECEIVER — and
# twelve of the thirty-four occurrences were exactly that (`self.sep`, `self.peeked`,
# `self.buf`, `self.value`). A roster built on the narrow grep would let a new
# `*((&self.thing) as *const T)` be born green, which is precisely how this round
# inherited a list that was missing a third of its subject. So the population here is
# the BROAD grep, the narrow one is computed too, and the lint refuses to run (exit 2)
# if the narrow result is ever NOT a subset of the broad one — that would mean the two
# patterns had drifted apart and the measurement could no longer be trusted.
#
# ── WHAT THIS LINT ASSERTS ───────────────────────────────────────────────────
#   FACT 1  EVERY SURVIVOR IS GROUNDED AT THE SITE. Each occurrence must carry a
#           `#112-ROSTER: <symbol>, ground=<tag>` marker within the 40 lines above it.
#           A NEW raw duplicate — written anywhere in stdlib, in any spelling the broad
#           grep matches — reds AT BIRTH, because it has no marker.
#
#   FACT 2  THE LEDGER AND THE TREE AGREE IN BOTH DIRECTIONS. The (file, symbol,
#           ground) set derived from the markers must equal the ledger's, and the
#           per-file occurrence counts must match. A site that disappears without its
#           row disappearing reds; a row invented without a site reds.
#
#   FACT 3  THE `copy-bound` ENFORCER IS REAL, CHECKED IN THE ABUSE DIRECTION. For
#           every `ground=copy-bound` row, the nearest enclosing `impl` header above
#           the occurrence must carry a `Copy` bound. This is re-derived from the impl
#           header by brace-free upward scan, NOT matched against a literal signature
#           string — the shared_ref_ub lint's second recorded defect was a lint that
#           cried wolf on an honest refactor, and a lint that cries wolf gets disabled.
#           Deleting `T: Copy` from either adapter reds here.
#
#   FACT 4  THE REFUSE HALF EXISTS AND IS NOT VACUOUS. Each `copy-bound` row names a
#           fail fixture; that fixture must exist and its `.expected` must actually
#           name the Copy diagnostic. An enforcement claim with no fixture behind it is
#           the same unenforced assumption the ground tag says it is not.
#
#   FACT 5  THE ADMIT HALF EXISTS. The repaired functions are pinned by
#           `tests/logos/pass/rawdup_*_drop_once` fixtures (heap-owning payload,
#           printing `Drop`, exact destructor-line count) each paired with a
#           `_copy_ctl` twin proving the Copy path did not regress. Both directions are
#           required — "exactly once" AND "not zero": #110 nearly shipped a double free
#           traded for a leak. This lint checks the pairing is complete, so deleting
#           one half of a pair reds.
#
#   FACT 6  EVERY OWNING YIELDER RECONCILES ITS CURSOR. Added by #116. A struct
#           whose whole job is to hand out OWNED elements from storage it owns
#           (`*IntoIter` / `*Drain` / `*IntoValues` / `*IntoKeys`) must carry an
#           `impl … Drop for` it in the same file. `ArrayIntoIter` did not: its
#           `next` performed a correct `ptr::read`, and the inline `[T; N]` field's
#           automatic drop glue then destroyed EVERY element a second time —
#           MEASURED 2 items in, 4 destructor calls out, rc 0. No widening of the
#           raw-duplicate grep could ever have seen it, because the defect was the
#           ABSENCE of a line and a missing line has no spelling. This fact is
#           derived from the tree (the struct roster and the impl roster), not
#           listed, so a NEW owning yielder written without a `Drop` reds at birth.
#
#   FACT 7  THE #114/#115/#116 ADMIT FIXTURES ARE PAIRED, like FACT 5's.
#           `tests/logos/pass/dupown_*_drop_once` (heap-owning payload, printing
#           `Drop`, exact destructor-line count) each with a `_copy_ctl` twin.
#
# ── WHAT THIS LINT DOES *NOT* SEE — SAID PLAINLY, BECAUSE ITS GREEN VOUCHES ──
# FACTs 1-5 hold exactly ONE READ SPELLING: the broad grep's
# `*((&X) as *const T)`. The property is "a value read out of, or duplicated from,
# a place that still owns it, such that two paths run its destructor", and stdlib
# contains at least three further spellings of it, ALL INVISIBLE HERE:
#   A'  two-step:  `let p: *const T = (&X) as *const T; let v: T = p[i];`
#                  — array.logos (the #116 site).
#   B   raw-ptr field index: `let val: T = self.<rawptrfield>[i];`
#                  — vec.logos x6, deque.logos x2. All sound TODAY, and sound
#                    because of a reconciling `Drop`, which is what FACT 6 pins.
#   C   raw deref: `let old: T = *p;` — cell.logos x6, slice.logos x1. One of the
#                  six is now MEASURED and pinned: `OnceCell::into_inner` (#119)
#                  was a live silent double drop in exactly this spelling — k=2,
#                  one destructor line printed TWICE, rc 0 — repaired by moving
#                  `self` into a private `#[no_auto_drop]` guard so the read has
#                  a single owner, and held by
#                  tests/logos/pass/dupown_oncecell_into_inner_drop_once. The
#                  other five, and slice.logos's, remain UNMEASURED.
# FACT 6 covers the OBLIGATION side of A'/B (the reconciling Drop) for the
# yielder families it can name; it does not cover C, and it does not cover an
# owning yielder whose name is outside those four suffixes. The instrument that
# DOES answer the property is a drop-count harness with a heap-owning payload —
# the `*_drop_once` fixtures FACTs 5 and 7 pin — and its coverage is by
# REACHABILITY: a surface no fixture instantiates is unmeasured, whatever the
# spelling. Do not read a green here as "the class is closed".
#
# EXIT 2 (cannot measure), never 0, when: the repo root or ledger is missing, the
# broad grep returns nothing at all (the shape was renamed and this lint has silently
# become a no-op), the narrow grep is not a subset of the broad one, or the
# owning-yielder roster of FACT 6 comes back empty.

set -uo pipefail

ROOT="${1:-}"
LEDGER="${2:-}"

die2() { echo "CANNOT MEASURE: $*" >&2; exit 2; }
fail() { echo "FAIL: $*" >&2; RC=1; }
RC=0

[ -n "$ROOT" ]   && [ -d "$ROOT" ]        || die2 "repo root '$ROOT' is not a directory"
[ -n "$LEDGER" ] && [ -f "$LEDGER" ]      || die2 "ledger '$LEDGER' not found"
[ -d "$ROOT/stdlib" ]                     || die2 "$ROOT/stdlib not found"
command -v grep >/dev/null                || die2 "grep unavailable"

cd "$ROOT" || die2 "cannot cd $ROOT"

BROAD_RE='\*\(\(&[^)]*\) as \*const '
NARROW_RE='\*\(\(&[a-z_]*\) as \*const '

BROAD=$(grep -rnE "$BROAD_RE" stdlib/ | sort)
NARROW=$(grep -rnE "$NARROW_RE" stdlib/ | sort)

N_BROAD=$(printf '%s' "$BROAD" | grep -c . || true)
N_NARROW=$(printf '%s' "$NARROW" | grep -c . || true)

# ── FACT 0 ──────────────────────────────────────────────────────────────────
[ "$N_BROAD" -gt 0 ] || die2 \
  "the broad grep matched NOTHING in stdlib/. Either the class is genuinely gone (in
   which case delete this lint and its ledger deliberately) or the shape was respelled
   and this lint has become a no-op. It refuses to report success either way."

if [ -n "$NARROW" ]; then
    ONLY_NARROW=$(comm -23 <(printf '%s\n' "$NARROW") <(printf '%s\n' "$BROAD"))
    [ -z "$ONLY_NARROW" ] || die2 \
      "the narrow grep matched lines the broad grep did not — the two patterns have
       drifted and the population can no longer be derived:
$ONLY_NARROW"
fi

echo "population: broad=$N_BROAD narrow=$N_NARROW (narrow must be, and is, a subset)"

# ── FACT 1: every occurrence carries a ground marker within 40 lines above ──
MARKER_RE='#112-ROSTER: *([A-Za-z_][A-Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*) *, *ground=([a-z-]+)'
DERIVED=$(mktemp); SCRATCH=$(mktemp); trap 'rm -f "$DERIVED" "$SCRATCH"' EXIT

while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    f=${hit%%:*}; rest=${hit#*:}; ln=${rest%%:*}
    [ -f "$f" ] || die2 "grep named a file that does not exist: $f"
    from=$(( ln - 40 )); [ "$from" -lt 1 ] && from=1
    win=$(sed -n "${from},${ln}p" "$f")
    if [[ "$win" =~ $MARKER_RE ]]; then
        printf '%s\t%s\t%s\n' "$f" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" >> "$DERIVED"
    else
        fail "UNGROUNDED RAW DUPLICATE at $f:$ln"
        echo "      $(sed -n "${ln}p" "$f")" >&2
        echo "      This shape makes a SECOND OWNER of one allocation. Either remove it" >&2
        echo "      (make the callee borrow, move the value out with mem::replace, or" >&2
        echo "      require T: Clone), or, if it is genuinely sound, say WHY at the site" >&2
        echo "      with a '#112-ROSTER: <Type>::<method>, ground=<tag>' marker AND add" >&2
        echo "      the row to $LEDGER. An unexplained survivor is how the next round" >&2
        echo "      inherits the whole class again." >&2
    fi
done <<< "$BROAD"

# ── FACT 2: two-way agreement with the ledger ───────────────────────────────
LED=$(mktemp); DERF=$(mktemp)
trap 'rm -f "$DERIVED" "$SCRATCH" "$LED" "$DERF"' EXIT
grep -vE '^[[:space:]]*(#|$)' "$LEDGER" | awk '{printf "%s\t%s\t%s\n", $1, $2, $3}' | sort > "$LED"
sort "$DERIVED" > "$DERF"
[ -s "$LED" ] || die2 "ledger $LEDGER holds no rows"

MISSING=$(comm -23 "$LED" "$DERF")
EXTRA=$(comm -13 "$LED" "$DERF")
[ -z "$MISSING" ] || fail "ledger rows with no matching grounded site in the tree:
$MISSING"
[ -z "$EXTRA" ]   || fail "grounded sites in the tree with no ledger row:
$EXTRA"

# per-file counts, derived both ways
for f in $(cut -f1 "$LED" | sort -u); do
    want=$(grep -c "^$f	" "$LED")
    got=$(printf '%s\n' "$BROAD" | grep -c "^$f:" || true)
    [ "$want" = "$got" ] || fail "per-file count for $f: ledger says $want, tree has $got"
done

# ── FACT 3: the copy-bound enforcer, re-derived from the impl header ────────
while IFS=$'\t' read -r f sym ground fixture; do
    [ "$ground" = "copy-bound" ] || continue
    struct=${sym%%::*}
    # nearest `impl` header above the occurrence that names this struct
    ln=$(printf '%s\n' "$BROAD" | grep "^$f:" | while IFS= read -r h; do
             r=${h#*:}; echo "${r%%:*}"; done | while read -r l; do
             from=$(( l - 40 )); [ "$from" -lt 1 ] && from=1
             # ⚠ not `… | grep -q`: under pipefail grep exits at the first match and
             # the writer takes SIGPIPE 141, which pipefail reports as a failed
             # pipeline. Read into a file, then match the file.
             sed -n "${from},${l}p" "$f" > "$SCRATCH"
             if grep -q "#112-ROSTER: *$sym *," "$SCRATCH"; then echo "$l"; fi
         done | head -1)
    if [ -z "$ln" ]; then fail "cannot locate the $sym occurrence to check its bound"; continue; fi
    hdr_line=$(awk -v end="$ln" 'NR<=end && /^impl[<[:space:]]/ { n=NR } END { print n }' "$f")
    if [ -z "$hdr_line" ] || [ "$hdr_line" = "0" ]; then
        fail "no enclosing impl header found above $f:$ln for $sym"; continue
    fi
    hdr=$(awk -v s="$hdr_line" 'NR>=s { print; if (/\{[[:space:]]*$/) exit }' "$f")
    printf '%s' "$hdr" > "$SCRATCH"
    grep -q -- "$struct" "$SCRATCH" || \
        fail "the impl header above $f:$ln does not name $struct — cannot ground $sym"
    if ! grep -qE '[A-Za-z_][A-Za-z0-9_]*: *[^,>]*\bCopy\b' "$SCRATCH"; then
        fail "$sym is rostered ground=copy-bound but its impl header carries NO Copy bound.
      The bitwise duplicate at $f:$ln is then reachable with a droppable payload, which
      is a double free. Header read:
$hdr"
    fi
    if [ "$fixture" = "-" ] || [ ! -f "$fixture" ]; then
        fail "$sym is ground=copy-bound but names no existing refuse fixture ('$fixture')"
    elif ! grep -q "does not implement trait 'Copy'" "$fixture"; then
        fail "refuse fixture $fixture does not assert the Copy diagnostic"
    fi
done < <(grep -vE '^[[:space:]]*(#|$)' "$LEDGER" | awk '{printf "%s\t%s\t%s\t%s\n", $1, $2, $3, $4}')

# ── FACT 7: THE UNVERIFIABLE GROUNDS ARE COUNTED, BECAUSE THEY ARE A HATCH ──
# FACT 3 re-derives `copy-bound` from the impl header and demands a refuse
# fixture — that ground earns its keep. `bc-blocked` and `rust-contract` earn
# nothing: they are prose. This round's verify planted a brand-new, genuine
# class-A raw duplicate in array.logos with a fabricated
# `// #112-ROSTER: Bogus::yank, ground=bc-blocked` and one ledger line, and the
# gate reported `OK: 6 surviving raw duplicates, all grounded and rostered`.
# Anyone could neutralise this lint for any new duplicate with a comment and a
# text line — and the project's own rule is that a gate's EXEMPTION must be
# checked in the ABUSE direction, because an unchecked hatch is worse than no
# gate: the green now vouches for it.
#
# The grounds cannot be verified mechanically — `bc-blocked` means "the borrow
# checker refuses the correct spelling", which is a fact about the compiler on
# the day it was written. What CAN be held is that no NEW row acquires one
# unnoticed. So the count per ground is pinned. Adding a `bc-blocked` row is
# then a deliberate act with a number to move, exactly like every other census
# in this tree; removing one (because it got fixed) is equally visible.
#
# ⚠ HONEST LIMIT, at the site: this pins the COUNT, not the TRUTH. A new
# duplicate can still take the place of an old one at a constant count if the
# old one is deleted in the same change — the roster's per-symbol rows below
# are what make that visible, not this fact.
GROUND_PIN="bc-blocked 1
copy-bound 3
rust-contract 1"
GROUND_NOW=$(grep -vE '^[[:space:]]*(#|$)' "$LEDGER" | awk '{print $3}' | sort | uniq -c \
             | awk '{printf "%s %s\n", $2, $1}' | sort)
if [ "$(printf '%s' "$GROUND_NOW")" != "$(printf '%s' "$GROUND_PIN" | sort)" ]; then
    fail "the per-ground row census moved.
      pinned:
$(printf '%s' "$GROUND_PIN" | sort | sed 's/^/        /')
      measured:
$(printf '%s' "$GROUND_NOW" | sed 's/^/        /')
      `bc-blocked` and `rust-contract` are PROSE — nothing verifies them, so a new
      row carrying one is how this lint gets switched off for a new duplicate.
      Move the pin deliberately and say what the new row is for."
fi

# ── FACT 5: the admit/control pairing is complete ───────────────────────────
PAIR_MISS=0
for p in tests/logos/pass/rawdup_*_drop_once.logos; do
    [ -e "$p" ] || continue
    twin=${p%_drop_once.logos}_copy_ctl.logos
    [ -f "$twin" ] || { fail "$p has no _copy_ctl twin ($twin)"; PAIR_MISS=1; }
    exp=${p%.logos}.expected
    [ -f "$exp" ] || { fail "$p has no .expected"; PAIR_MISS=1; }
    grep -q 'DROP n=' "$exp" || fail "$exp pins no destructor line — the oracle of this
      family is a DESTRUCTOR COUNT with a heap-owning payload, not an exit code"
done
NPAIR=$(ls tests/logos/pass/rawdup_*_drop_once.logos 2>/dev/null | wc -l)
[ "$NPAIR" -ge 11 ] || fail "only $NPAIR rawdup_*_drop_once fixtures found; #112 repaired
      eleven call surfaces and each must keep its counting oracle"
echo "pairs: $NPAIR drop_once fixtures rostered (pairing checked above)"

# ── FACT 6: every owning yielder reconciles its cursor with a Drop ──────────
# The population is derived, not listed: the four owning-yielder name families.
# ⚠ THIS IS A NAME FAMILY, i.e. still a spelling — see the header's limit note.
# What it buys is the axis the grep cannot reach at all: the ABSENCE of a Drop.
YIELDER_RE='^[[:space:]]*pub struct ([A-Za-z0-9_]*(IntoIter|Drain|IntoValues|IntoKeys))[<[:space:]{]'
YIELDERS=$(grep -rnE "$YIELDER_RE" stdlib/ | sort)
Y_OK=0
N_YIELD=$(printf '%s' "$YIELDERS" | grep -c . || true)
[ "$N_YIELD" -gt 0 ] || die2 \
  "the owning-yielder roster is EMPTY. stdlib always has at least VecIntoIter; an
   empty roster means the name families were renamed and FACT 6 has silently become
   a no-op, which is the failure mode its own header says it prevents."
while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    yf=${hit%%:*}
    name=$(printf '%s' "$hit" | sed -E "s/.*pub struct ([A-Za-z0-9_]+).*/\1/")
    if ! grep -qE "^[[:space:]]*impl[<[:space:]].*Drop for ${name}\b" "$yf"; then
        fail "OWNING YIELDER WITH NO RECONCILING DROP: $name in $yf
      It hands out OWNED elements from storage it owns, so the elements it has
      already yielded belong to the consumer and the ones it has not belong to it.
      Without an 'impl Drop for $name' that drops exactly the un-yielded tail, the
      compiler's own field glue destroys elements the consumer already owns (#116:
      MEASURED 2 items in, 4 destructor calls out, rc 0 — no abort, only the COUNT
      sees it). Write the Drop, and pin it with a tests/logos/pass/*_drop_once
      fixture whose .expected holds the exact destructor lines."
        continue
    fi
    Y_OK=$(( Y_OK + 1 ))
done <<< "$YIELDERS"
echo "yielders: $Y_OK of $N_YIELD owning-yielder structs carry a reconciling Drop"

# ── FACT 7: the #114/#115/#116 admit/control pairing ────────────────────────
for p in tests/logos/pass/dupown_*_drop_once.logos; do
    [ -e "$p" ] || continue
    twin=${p%_drop_once.logos}_copy_ctl.logos
    [ -f "$twin" ] || fail "$p has no _copy_ctl twin ($twin)"
    exp=${p%.logos}.expected
    [ -f "$exp" ] || fail "$p has no .expected"
    grep -q 'DROP n=' "$exp" || fail "$exp pins no destructor line — the oracle of this
      family is a DESTRUCTOR COUNT with a heap-owning payload, not an exit code"
done
NDUP=$(ls tests/logos/pass/dupown_*_drop_once.logos 2>/dev/null | wc -l)
[ "$NDUP" -ge 4 ] || fail "only $NDUP dupown_*_drop_once fixtures found; the #114/#116
      round closed four surfaces (ArrayIntoIter, the consuming-predicate adapters, the
      two-line generic indirect call, the four comparator selectors) and each must keep
      its counting oracle"
echo "dupown pairs: $NDUP drop_once fixtures rostered (pairing checked above)"

if [ "$RC" = 0 ]; then
    echo "OK: $N_BROAD surviving raw duplicates, all grounded and rostered"
fi
# lint:exit-ok — RC is set only by the two literals 0 and 1 in this file (`RC=0` at
# the top, `RC=1` inside `fail`), so it cannot reach the 8-bit wrap. Every other exit
# path is an explicit `exit 2` from `die2`.
exit $RC  # lint:exit-ok — RC is set only to the literals 0 and 1, see above
