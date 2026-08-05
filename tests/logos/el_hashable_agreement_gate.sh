#!/usr/bin/env bash
# el_hashable_agreement_gate.sh LOGOSC LIBDIR
#
# THE QUERY PLANE DECIDES "MAY THIS TYPE BE A HASH KEY" IN TWO PLACES, AND THEY
# ARE DIFFERENT KINDS OF ARTIFACT. This gate pins that they answer the same set.
#
#   TIER 1 — the rel column (ADR 0024 S1/S2). `sema_collect.cpp:check_rel_column_types`
#            asks the LANGUAGE: `sema_has_impl_recursive("Hash", <declared spelling>)`.
#            A user type joins by gaining the impl. DERIVED.
#   TIER 2 — the EL lattice. `logos.std.wql.el::el_index_key_ok` answers from a
#            21-row hand-written table: the class clauses, and every INT row.
#            LISTED.
#
# Tier 2's own doc-comment (stdlib/mem/wql/el.logos, above `el_index_key_ok`) says
# what it is: "THE ONE ROW THAT IS ALLOWED TO BE A LIST — it is a list of impls
# that exist, read off the two files that declare them, and a type gains its entry
# by gaining the impl." Nothing has ever checked that the list still names the
# impls.
#
# MEASURED 2026-08-04, all 21 lattice rows, on this box at a968bc3c: the two
# populations agreed member for member —
#
#   admitted by both (12): i8 i16 i32 i64 u8 u16 u32 u64 isize usize bool str
#   refused  by both  (9): i128 u128 i56 u56 f32 f64 String i24 u24
#
# RE-MEASURED 2026-08-05 after the six missing instances landed — the exception
# that kept i128/u128/i56/u56/i24/u24 out cited a SIGSEGV in `impl Eq for u128`
# that does not reproduce, and the packed-width exception cited a `HashMap` that
# cannot be instantiated over them, which is also false. Both sides moved TOGETHER
# and the populations agree again:
#
#   admitted by both (18): the 12 above + i128 u128 i56 u56 i24 u24
#   refused  by both  (3): f32 f64 String
#
# That agreement is a MEASUREMENT, not a theorem, and either side can still move
# alone: deleting `impl Hash for i56` would flip tier 1 while the lattice's INT
# clause keeps admitting it, and a non-INT row gaining an impl would flip tier 1
# with nothing on the lattice side. Both are exactly the drift the ADR 0024 S6 arc
# exists to close, and both are silent without this gate.
#
# ⚠ THE POPULATION IS DERIVED FROM THE ARTIFACT, NEVER LISTED HERE. The names come
# out of a RUN of `el_ty_at(0 .. el_ty_arity())`, so a row added to the lattice is
# in this gate's population on the next run without anyone editing this file. A
# name typed into this script would be a second copy of the population, which is
# the defect the arc is about.
#
# ⚠ THE CANARY RUNS BEFORE THE VERDICT. Every finding here is "a comparison came
# back non-empty", and a comparison over an empty population reports exactly that
# too. So the same comparator is first run over the same measured data with ONE
# answer flipped, and must report it. If it does not, this exits 3 — the gate is
# broken, which is a different fact from the tree being clean.
#
# Exit: 0 agree · 1 disagree · 2 could not look · 3 the gate's own canary failed.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 LOGOSC LIBDIR" >&2
    exit 2
fi
LOGOSC="$1"
LIBDIR="$2"

if [ ! -x "$LOGOSC" ];  then echo "FAIL: no logosc at $LOGOSC" >&2;  exit 2; fi
if [ ! -d "$LIBDIR" ];  then echo "FAIL: no libdir at $LIBDIR" >&2;  exit 2; fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# ── 1. THE POPULATION, OUT OF THE RUNNING TABLE ──────────────────────────────
cat > "$TMPD/enum.logos" <<'EOF'
package test;
use logos.std.wql.el;
use logos.lang.str;
extern fn printf(fmt: *const u8, ...) -> i32;
fn main() -> i32 {
    let n: i64 = el_ty_arity();
    let h: str = "ARITY %ld\n";
    unsafe { printf(h.as_ptr(), n); }
    let mut i: i64 = 0i64;
    while i < n {
        let t: ElTy = el_ty_at(i);
        let f: str = "ROW %.*s %d\n";
        unsafe {
            printf(f.as_ptr(), t.name.len() as i32, t.name.as_ptr(),
                   el_index_key_ok(t.name) as i32);
        }
        i = i + 1i64;
    }
    return 0i32;
}
EOF

link_and_run() {   # link_and_run <obj> <bin> — echoes the program's stdout
    local obj="$1" bin="$2"
    local archives=()
    local a
    for a in "$LIBDIR"/liblstdlib*.a; do [ -f "$a" ] && archives+=("$a"); done
    for a in "$LIBDIR"/liblogos-*.a;  do [ -f "$a" ] && archives+=("$a"); done
    for a in "$LIBDIR"/*.a; do
        case "$(basename "$a")" in
            liblstdlib*|liblogos-*) ;;
            *) [ -f "$a" ] && archives+=("$a") ;;
        esac
    done
    if [ "${#archives[@]}" -eq 0 ]; then return 1; fi
    cc "$obj" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
       -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
       -o "$bin" 2>"$TMPD/link.err" || return 1
    "$bin"
}

if ! "$LOGOSC" "$TMPD/enum.logos" -o "$TMPD/enum.o" 2>"$TMPD/enum.err"; then
    echo "FAIL: the lattice enumerator did not compile — nothing below was measured."
    cat "$TMPD/enum.err"
    exit 2
fi
if ! link_and_run "$TMPD/enum.o" "$TMPD/enum" > "$TMPD/enum.out"; then
    echo "FAIL: the lattice enumerator did not link or run — nothing below was measured."
    cat "$TMPD/link.err" 2>/dev/null || true
    exit 2
fi

ARITY=$(awk '$1 == "ARITY" { print $2 }' "$TMPD/enum.out")
awk '$1 == "ROW" { print $2, $3 }' "$TMPD/enum.out" | sort > "$TMPD/tier2.txt"
N_ROWS=$(wc -l < "$TMPD/tier2.txt")

# ⚠ A FLOOR, AND IT CARRIES ITS MEASUREMENT. `el_ty_arity` is the table's own
# count and the table's own fixture already pins it against the row boundary; what
# this refuses is the OTHER failure — an enumerator that ran and printed nothing,
# which is indistinguishable from a clean sweep at every line below.
MIN_ROWS=21          # MEASURED 2026-08-04 at a968bc3c: el_ty_arity() == 21
if [ -z "$ARITY" ] || [ "$N_ROWS" -ne "$ARITY" ]; then
    echo "FAIL: the enumerator printed $N_ROWS rows against ARITY='$ARITY' — the"
    echo "      population is not the artifact's, so no comparison below means anything."
    exit 2
fi
if [ "$N_ROWS" -lt "$MIN_ROWS" ]; then
    echo "FAIL: $N_ROWS lattice rows enumerated, want >= $MIN_ROWS (measured 2026-08-04)."
    echo "      A shrunken population turns every check below into 'nothing was examined'."
    exit 2
fi

# ── 2. THE SAME NAMES, ASKED OF THE LANGUAGE ─────────────────────────────────
# One `trait S1 { rel r(k: <NAME>, n: i64); }` per name. `check_rel_column_types`
# is a deferred sema pass, so a refusal is a clean non-zero exit with the column
# named; an admission compiles.
: > "$TMPD/tier1.txt"
while read -r NAME _; do
    P="$TMPD/rc_$NAME.logos"
    cat > "$P" <<EOF
package test;
use logos.std.wql.wql;
use logos.mem.collections.vec;
use logos.lang.hash;
use logos.mem.string;
pub trait S1 { rel r(k: $NAME, n: i64); }
fn main() -> i32 { return 0i32; }
EOF
    if "$LOGOSC" "$P" -o "$TMPD/rc_$NAME.o" >"$TMPD/rc_$NAME.err" 2>&1; then
        echo "$NAME 1" >> "$TMPD/tier1.txt"
    else
        # ⚠ A REFUSAL MUST BE THE COLUMN CHECK'S. Any other compile error — a
        # package that moved, a syntax change in `rel` — would otherwise be
        # recorded as "tier 1 says no" and could make a real disagreement vanish.
        if grep -F -- "a rel column type must implement" "$TMPD/rc_$NAME.err" > /dev/null; then
            echo "$NAME 0" >> "$TMPD/tier1.txt"
        else
            echo "FAIL: the rel-column probe for '$NAME' failed for a reason that is NOT"
            echo "      the column-capability check, so tier 1's answer was never obtained:"
            sed -n '1,6p' "$TMPD/rc_$NAME.err"
            exit 2
        fi
    fi
done < "$TMPD/tier2.txt"
sort -o "$TMPD/tier1.txt" "$TMPD/tier1.txt"

# ── 3. THE COMPARISON, AND THE COMPARATOR PROVED ON A CANARY ─────────────────
# `disagreements <tier1-file> <tier2-file>` writes one line per name whose two
# answers differ.
disagreements() {
    join "$1" "$2" | awk '$2 != $3 { print $1, "tier1=" $2, "tier2=" $3 }'
}

disagreements "$TMPD/tier1.txt" "$TMPD/tier2.txt" > "$TMPD/found.txt"
N_FOUND=$(wc -l < "$TMPD/found.txt")

# ⚠ THE CANARY IS A DELTA, NOT A COUNT, and the first draft of it was the count.
# It flipped one row and demanded EXACTLY ONE finding — which is the right number
# only while the tree is clean. Run against a deliberately broken lattice (the
# control revert: `el_index_key_ok` admitting the non-power-of-two widths) the
# real comparison found four, the canary run found five, and the gate reported
# "THE GATE is broken" over a tree that was exactly as broken as it said. A
# self-check whose answer depends on the subject cannot separate the two.
# (That deliberate break is the MIRROR of the 2026-08-05 widening, and the
# difference is the whole rule: it admitted the widths with no impls behind them;
# the widening wrote the impls first and moved the lattice second.)
#
# So the canary flips a row the real comparison AGREED on, and asserts the
# difference: that row, and only that row, is added to the findings.
FLIP=$(join -v 1 "$TMPD/tier2.txt" "$TMPD/found.txt" | awk 'NR == 1 { print $1 }')
if [ -z "$FLIP" ]; then
    echo "FAIL: every lattice row already disagrees, so there is no agreeing row to"
    echo "      plant a flip in and the comparator cannot be proved on this data."
    exit 3
fi
awk -v n="$FLIP" '$1 == n { print $1, ($2 == 1 ? 0 : 1); next } { print }' \
    "$TMPD/tier2.txt" > "$TMPD/tier2.canary"
disagreements "$TMPD/tier1.txt" "$TMPD/tier2.canary" > "$TMPD/canary.out"
comm -13 "$TMPD/found.txt" "$TMPD/canary.out" > "$TMPD/canary.delta"
CANARY_ADDED=$(awk '{ print $1 }' "$TMPD/canary.delta" | sort -u | tr '\n' ' ')
if [ "$CANARY_ADDED" != "$FLIP " ]; then
    echo "FAIL: the comparator did not catch its own planted flip on '$FLIP'."
    echo "      Flipping one agreeing row must add exactly that row to the findings;"
    echo "      it added: '${CANARY_ADDED:-nothing}'. Every verdict this gate can reach"
    echo "      is 'the comparison found N', and a comparator that cannot see a change"
    echo "      produces the same N. THE GATE is broken, which is a different fact"
    echo "      from the tree being clean."
    exit 3
fi

# ── 4. THE VERDICT ───────────────────────────────────────────────────────────
if [ "$N_FOUND" -ne 0 ]; then
    echo "FAIL: $N_FOUND of $N_ROWS lattice types are hashable in one tier and not the other."
    echo ""
    echo "  tier1 = the language (\`impl Hash\`, via the rel-column check)"
    echo "  tier2 = \`logos.std.wql.el::el_index_key_ok\` (the lattice's own admission)"
    echo ""
    cat "$TMPD/found.txt"
    echo ""
    echo "  A type hashable in tier 1 and refused by tier 2 can be a rel column but"
    echo "  cannot be an EL index key: the join falls to the loop tier with no ground"
    echo "  stated. A type tier 2 admits without an impl behind it emits a HashMap"
    echo "  instantiation the stdlib cannot satisfy."
    echo ""
    echo "  Whichever tier moved, the fix is to move the other one WITH it — or to"
    echo "  restate the exception here, with its ground, deliberately."
    exit 1
fi

echo "PASS: $N_ROWS lattice types, tier 1 (impl Hash) and tier 2 (el_index_key_ok)"
echo "      agree on every one; the comparator caught its planted flip on '$FLIP'."
exit 0
