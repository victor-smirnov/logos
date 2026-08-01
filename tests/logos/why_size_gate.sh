#!/usr/bin/env bash
# why_size_gate.sh LOGOSC
#
# THE JUSTIFICATION IS REACHABLE ONLY THROUGH `explain()` (ADR 0024 S4q) — and this
# gate is the ONLY thing that can say so, because the property is about the LINKED
# IMAGE and no .logos fixture can weigh its own binary.
#
# ⚠⚠ WHY IT EXISTS AT ALL. `f5688fb8` asserted, in its commit message, "Linked with
# --gc-sections: +8 bytes" for the round that gave every plan a rendered `why` string.
# An independent re-measure found +1168 bytes on `wql_deferred_plan_e2e` and +784 on
# `wql_join_order_multi_e2e` — two orders of magnitude out. The +8 came from reading
# TOTAL FILE SIZE, which had fallen ~160 bytes because `.debug_str`/`.debug_line_str`
# shrank for unrelated reasons, and from measuring only at -O2 while the suite
# compiles at -O0, where the growth is strictly larger. Nothing in the gates could see
# the claim, so the next round inherited it as established fact.
#
# SO THE RULE THIS FILE ENFORCES, and the two halves are both required:
#
#   THE LOADED IMAGE, NOT THE FILE. `size`'s text+data+bss. `.debug_*` is not loaded
#   and moves for reasons that have nothing to do with what a plan carries.
#
#   AT THE LEVEL THE SUITE COMPILES AT. No -O flag, exactly as `run_test.sh` links.
#
# WHAT IS PINNED. Two programs, IDENTICAL but for one expression: five queries, five
# `prepare` calls, five `run` calls, and either five `explain()` reads or five
# `ground()` reads. The justification prose must be:
#
#   • PRESENT in the program that asks — it is the deliverable, and a gate that only
#     rewarded shrinkage would be satisfied by deleting it;
#   • ABSENT from the program that does not — which is the design claim. `prepare`
#     used to STORE the rendered text into the plan, so the literal was written by a
#     live constructor and `--gc-sections` could not touch it: measured at
#     `df129585`, both programs carried all five literals and differed by 96 bytes.
#     As a constant returned BY `explain()` the same bytes reach only the caller who
#     asks — measured: 5 literals vs 0, and 32392 vs 29432 bytes of loaded image.
#
# The size assertion is a FLOOR, not an equality: it must not go red because a
# justification was reworded, only because the reachability property stopped holding.
set -euo pipefail

LOGOSC="$1"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# The five shapes whose grounds differ, so the gate weighs a realistic spread of
# justification text rather than five copies of one sentence: a bare scan, an anti
# join refused before enumeration, a join with no named sequence, a searched join,
# and a joined aggregate.
cat > "$TMPD/base.logos" <<'PROBE'
package test;
use logos.std.wql.wql;
use logos.mem.collections.vec;
use logos.lang.option;
use logos.lang.str;
struct A { pub k: i64, pub v: i64 }
struct B { pub k: i64, pub m: i64 }
struct W { pub k: i64 }
pub deem q0(as_: &[A]) { from as_ a where a.v > 0 select a.k }
pub deem q1(as_: &[A], ws: &[W]) { from as_ a anti join ws w on a.k == w.k select a.v order by a.k }
pub deem q2(as_: &[A], bs: &[B]) { from as_ a join bs b on a.k == b.k select a.v * 100 + b.m }
pub deem q3(as_: &[A], bs: &[B]) { from as_ a join bs b on a.k == b.k select a.v * 100 + b.m order by a.k }
pub deem q4(as_: &[A], bs: &[B]) { from as_ a join bs b on a.k == b.k group by a.k aggregate t = sum(b.m) select key + t }
fn main() -> i32 {
    let as_: [A; 2] = [ A { k: 1i64, v: 10i64 }, A { k: 2i64, v: 20i64 } ];
    let bs: [B; 1] = [ B { k: 1i64, m: 7i64 } ];
    let ws: [W; 1] = [ W { k: 2i64 } ];
    let p0: Q0Plan = q0_prepare(&as_[..]);
    let p1: Q1Plan = q1_prepare(&as_[..], &ws[..]);
    let p2: Q2Plan = q2_prepare(&as_[..], &bs[..]);
    let p3: Q3Plan = q3_prepare(&as_[..], &bs[..]);
    let p4: Q4Plan = q4_prepare(&as_[..], &bs[..]);
    let mut n: i64 = 0i64;
    /*ASK*/
    n = n + q0_run(&p0, &as_[..]).unwrap().len();
    n = n + q1_run(&p1, &as_[..], &ws[..]).unwrap().len();
    n = n + q2_run(&p2, &as_[..], &bs[..]).unwrap().len();
    n = n + q3_run(&p3, &as_[..], &bs[..]).unwrap().len();
    n = n + q4_run(&p4, &as_[..], &bs[..]).unwrap().len();
    if n < 0i64 { return 1i32; }
    return 0i32;
}
PROBE

# ⚠ THE TWO PROGRAMS DIFFER IN ONE EXPRESSION AND NOTHING ELSE. Both construct all
# five plans and run all five queries, so every difference the measurement reports is
# the justification and not the query.
sed 's|/\*ASK\*/|n = n + p0.explain().len() + p1.explain().len() + p2.explain().len() + p3.explain().len() + p4.explain().len();|' \
    "$TMPD/base.logos" > "$TMPD/ask.logos"
sed 's|/\*ASK\*/|n = n + p0.ground() as i64 + p1.ground() as i64 + p2.ground() as i64 + p3.ground() as i64 + p4.ground() as i64;|' \
    "$TMPD/base.logos" > "$TMPD/noask.logos"

LINK_ARCHIVES=()
if [ -n "${LOGOS_LIB_DIR:-}" ]; then
    for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/liblogos-*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
    for a in "$LOGOS_LIB_DIR"/*.a; do
        case "$(basename "$a")" in
            liblstdlib*|liblogos-*) ;;
            *) [ -f "$a" ] && LINK_ARCHIVES+=("$a") ;;
        esac
    done
fi
if [ "${#LINK_ARCHIVES[@]}" -eq 0 ]; then
    echo "FAIL: no link archives — LOGOS_LIB_DIR is '${LOGOS_LIB_DIR:-}'"
    exit 1
fi

# The LOADED image: text + data + bss, which is `size`'s `dec` column. NOT the file.
image_bytes() {
    size "$1" | awk 'NR==2 { print $1 + $2 + $3 }'
}

build() {
    local src="$1" out="$2"
    if ! "$LOGOSC" "$src" -o "$out.o" >"$out.compile" 2>&1; then
        echo "FAIL: logosc failed on $src:"; cat "$out.compile"; exit 1
    fi
    if ! cc "$out.o" -Wl,--start-group "${LINK_ARCHIVES[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
            -o "$out.bin" 2>"$out.link"; then
        echo "FAIL: cc link failed for $src:"; cat "$out.link"; exit 1
    fi
}

build "$TMPD/ask.logos"   "$TMPD/ask"
build "$TMPD/noask.logos" "$TMPD/noask"

fail=0

# Both programs must actually RUN — a gate on an artifact that does not work is a gate
# on nothing.
if ! "$TMPD/ask.bin"; then   echo "FAIL: the explaining probe exited non-zero";     fail=1; fi
if ! "$TMPD/noask.bin"; then echo "FAIL: the non-explaining probe exited non-zero"; fail=1; fi

# ── HALF ONE: THE JUSTIFICATION IS STILL THERE FOR THE CALLER WHO ASKS ───────
# One census clause per plan. `explain()` returning "" would shrink the image and
# satisfy every size assertion below, which is exactly why this half comes first.
NASK=$(strings -a "$TMPD/ask.bin" | grep -c 'ORDER AXIS' || true)
if [ "$NASK" -lt 5 ]; then
    echo "FAIL: the explaining probe carries $NASK justification literals, want >= 5"
    echo "      (a plan's explain() must still deliver the whole rendered ground)"
    fail=1
fi
# ⚠ NOT `strings … | grep -q`. `grep -q` exits on the first match, `strings` takes
# SIGPIPE, and under `pipefail` the pipeline reports 141 — a PASSING assertion read as
# a failure. Count and compare instead; the same rule applies to every pipe here.
NREM=$(strings -a "$TMPD/ask.bin" | grep -c 'THE REMEDY, derived from the antecedent that failed' || true)
if [ "$NREM" -lt 1 ]; then
    echo "FAIL: the explaining probe carries no remedy clause — explain() has been thinned"
    fail=1
fi

# ── HALF TWO: AND NOT THERE FOR THE CALLER WHO DOES NOT ──────────────────────
# THE PROPERTY. `prepare` must not store it: a literal a live constructor writes is a
# literal `--gc-sections` cannot drop, whatever the commit message says.
NNO=$(strings -a "$TMPD/noask.bin" | grep -c 'ORDER AXIS' || true)
if [ "$NNO" -ne 0 ]; then
    echo "FAIL: the non-explaining probe still links $NNO justification literals."
    echo "      Something on the plan-construction path references the rendered text —"
    echo "      a stored field, a trace call, anything reachable from \`prepare\`."
    strings -a "$TMPD/noask.bin" | grep -m2 'ORDER AXIS' || true
    fail=1
fi

# ── AND THE IMAGE FOLLOWS ────────────────────────────────────────────────────
# A FLOOR, not an equality — AT THE MEASURED VALUE. It was 1500 against a
# measured 2960 with the ground "so a reworded justification does not turn this
# red". That is the argument for halving a floor, and halving a floor is choosing
# not to notice: two thirds of the justification could stop being stripped and
# this stayed green.
#
# MEASURED 2026-07-31 at `62835ad3`, x86_64-linux: explain=32360 no-explain=29416
# delta=2944. ONE observation, so this is the minimum ever legitimately seen. A
# rewording that genuinely shortens the justification WILL turn this red — that
# is the point; re-measure, put the new number here, and say in the commit
# message what changed. A drop nobody had to look at is the failure being
# removed.
ASKB=$(image_bytes "$TMPD/ask.bin")
NOB=$(image_bytes "$TMPD/noask.bin")
DELTA=$(( ASKB - NOB ))
echo "[why-size] loaded image (text+data+bss): explain=$ASKB  no-explain=$NOB  delta=$DELTA"
MIN_DELTA=2944
if [ "$DELTA" -lt "$MIN_DELTA" ]; then
    echo "FAIL: the two images differ by only $DELTA bytes (want >= $MIN_DELTA,"
    echo "      MEASURED 2026-07-31 at 62835ad3 — the value this gate saw, not a"
    echo "      fraction of it; if the justification legitimately got shorter,"
    echo "      re-measure and edit the floor with its ground)."
    echo "      The justification is supposed to be the difference between them:"
    echo "      either it is being linked into the program that never asks for it,"
    echo "      or explain() no longer carries it."
    fail=1
fi

exit "$fail"  # lint:exit-ok — `fail` is set only to the literals 0 and 1
