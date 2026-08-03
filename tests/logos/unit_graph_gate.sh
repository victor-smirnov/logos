#!/usr/bin/env bash
# unit_graph_gate.sh LOGOSC STDLIB_BIN_DIR REPO_ROOT WORK_DIR
#
# THE UNIT PARTITION AND ITS ORDER, ASSERTED — because the suite could not see
# either. Before this file, `ctest -N` reported the same 6828 tests with and
# without src/compiler/unit_graph.cpp existing: the artifact was written, the
# census printed, and NOTHING compared any of it to a known answer. By this
# repo's own rule (a test missing from the suite is one of the seven ways a gate
# lies) the green suite said nothing about the partition.
#
# ── WHAT IS ASSERTED, AND WHY EACH ONE ─────────────────────────────────────
#
#  A. CASE (1), a cross-TU metafunction call, produces an ORDER EDGE.
#     `unit_order` is a provider TU defining `#[fn_macro] emit_gamma` and a
#     consumer TU calling `emit_gamma!{}`. The compiler process must CALL
#     emit_gamma before it can finish consumer.logos.
#     MEASURED BEFORE THE FIX: units=3 edges=0 levels=1 — "three independent
#     units, any order, in parallel". Not a missing edge: a FALSE claim of
#     independence, on the canonical shape the whole arc is about. The cause is
#     that prog.metacall_sites is a WORKLIST the dispatch loop drains, so the
#     final post-sema program has none and the graph was reading an emptied
#     list.
#
#  B. CASE (2), a bootstrap cycle, is ONE SCC and ONE work group.
#     `unit_cycle` is two TUs each defining a metafunction the other calls.
#     MEASURED BEFORE: edges=0 and three separate SCCs — a real cycle read as
#     three independent units. The discriminator the design rests on (same SCC
#     ⇒ genuine JIT; cross-SCC edge ⇒ AOT-then-load) returned the wrong answer
#     on BOTH canonical shapes.
#
#  C. THE CHECK CAN FAIL. `LOGOS_UNITS_NO_ORDER_FACTS=1` suppresses the
#     accumulator. Under it, A must report edges=0 AND `order_established:
#     false` AND one unit per level. That proves two things at once: the edges
#     in A come from the accumulation and not from some coincidence, and the
#     total-order degradation is LIVE code rather than an unreachable branch.
#     A gate that cannot fail is not evidence.
#
#  D. THE DEGRADATION GOES THE SAFE WAY. A level wider than one group is a
#     POSITIVE claim ("build these concurrently"). With no trustworthy edges
#     the only defensible answer is the sequential order, and the artifact must
#     say so in a field a consumer can read (`order_established`).
#
#  E. THE CANARY EXAMINED SOMETHING. `LOGOS_VERIFY_UNITS=1` compares the
#     DECLARED partition against the family hash tag the mangler independently
#     baked into link names. `tags=0` means it looked at nothing; a caller that
#     reads that as a pass has a check that cannot fail, so tags>0 is asserted
#     on a fixture that HAS families.
#
#  F. CROSS-MODULE case (1) COMPILES. lforge stays a thin wrapper calling
#     logosc per MODULE, so every case-1 edge it will ever schedule is a
#     module-to-module edge. MEASURED BEFORE: every one of them failed —
#     `metaprog item-thunk lookup '__metacall_thunk_…': Symbols not found`.
#
#  G. THE CHAIN SHAPE REFUSES INSTEAD OF DROPPING THE ITEM. A metafunction
#     emitting a call to a metafunction stubbed in an earlier round used to
#     compile with exit 0 and simply omit the emitted item. It must fail, and
#     the message must name the mechanism.
#
# ⚠ The gate asserts on the SIDECAR, which is written by `--emit-units`, and on
# the compiler's own census line. It never re-derives the partition itself: a
# checker that recomputes the answer its own way is the drift this whole file
# exists to prevent.
set -uo pipefail

LOGOSC="${1:?logosc}"
LIBDIR="${2:?stdlib bin dir}"
REPO="${3:?repo root}"
WORK="${4:?work dir}"

mkdir -p "$WORK" || exit 1
fail=0
note() { printf '%s\n' "$*"; }
bad()  { printf 'FAIL: %s\n' "$*"; fail=1; }

# ⚠ NEVER `producer | grep -q` UNDER `set -o pipefail`.
# grep -q exits the moment it matches and closes the pipe; the producer takes
# SIGPIPE, pipefail propagates its non-zero status, and the pipeline reports
# FAILURE precisely when the thing was FOUND. This gate lost a whole assertion
# to it once already (the cross-module archive check reported "symbol absent"
# while nm was printing the symbol). Count instead — grep -c drains its input.
has_sym() {  # has_sym <archive> <needle>
    local n
    n=$(nm -g "$1" 2>/dev/null | grep -c -- "$2")
    [ "${n:-0}" -gt 0 ]
}
has_text() { # has_text <file> <pattern>
    local n
    n=$(grep -c -- "$2" "$1" 2>/dev/null)
    [ "${n:-0}" -gt 0 ]
}

build_units() {   # build_units <manifest> <out.a> [extra args...]
    local manifest="$1" out="$2"; shift 2
    ( cd "$REPO" && "$LOGOSC" --emit-module "$manifest" -L "$LIBDIR" \
        -o "$out" --emit-units "$@" ) > "$out.log" 2>&1
    return $?
}

# `census: { … key: value, … }` — one field out of the sidecar's census map.
census_field() {  # census_field <sidecar> <key>
    sed -n 's/.*[{,] *'"$2"': *\([^,}]*\).*/\1/p' <<< "$(grep -o 'census: {.*}' "$1")" \
        | head -1 | tr -d ' '
}

# ── A. case (1): one cross-SCC order edge, two levels ──────────────────────
UO_A="$WORK/libunit_order.a"
if ! build_units tests/logos/unit_order/unit_order.module "$UO_A"; then
    bad "unit_order (case 1) failed to build:"; tail -5 "$UO_A.log"
elif [ ! -f "$UO_A.units" ]; then
    bad "unit_order: --emit-units wrote no sidecar $UO_A.units"
else
    e=$(census_field "$UO_A.units" edges)
    l=$(census_field "$UO_A.units" levels)
    oe=$(census_field "$UO_A.units" order_established)
    up=$(census_field "$UO_A.units" unresolved_providers)
    [ "$e"  = "1" ]    || bad "case 1: expected edges=1, got '$e' (a cross-TU metafunction call MUST be an order edge)"
    [ "$l"  = "2" ]    || bad "case 1: expected levels=2 (provider before consumer), got '$l'"
    [ "$oe" = "true" ] || bad "case 1: order_established='$oe' — the order was not derived"
    [ "$up" = "0" ]    || bad "case 1: unresolved_providers=$up (any unknown provider forces the total order)"
    # The edge must run consumer -> provider and cross an SCC boundary.
    has_text "$UO_A.units" 'cause: "metacall", same_scc: false' \
        || { bad "case 1: no cross-SCC metacall edge in the sidecar"; grep -A3 'edges:' "$UO_A.units"; }
    has_text "$UO_A.units" 'consumer.logos' || bad "case 1: consumer.logos is not a unit"
    has_text "$UO_A.units" 'provider.logos' || bad "case 1: provider.logos is not a unit"
    note "case 1: edges=$e levels=$l order_established=$oe"
fi

# ── B. case (2): the bootstrap cycle is ONE scc, ONE group ─────────────────
UC_A="$WORK/libunit_cycle.a"
if ! build_units tests/logos/unit_cycle/unit_cycle.module "$UC_A"; then
    bad "unit_cycle (case 2) failed to build:"; tail -5 "$UC_A.log"
else
    e=$(census_field "$UC_A.units" edges)
    bc=$(census_field "$UC_A.units" bootstrap_cycles)
    [ "$e"  = "2" ] || bad "case 2: expected edges=2 (each TU calls the other's metafn), got '$e'"
    [ "$bc" = "1" ] || bad "case 2: expected bootstrap_cycles=1, got '$bc' — a real cycle read as independent units"
    has_text "$UC_A.units" 'units: \[1, 2\], cycle: true' \
        || { bad "case 2: the two TUs are not ONE work group"; grep -A3 'levels:' "$UC_A.units"; }
    has_text "$UC_A.units" 'same_scc: true' || bad "case 2: no same-SCC edge — the cycle was not condensed"
    note "case 2: edges=$e bootstrap_cycles=$bc"
fi

# ── C+D. the check can fail, and it degrades the SAFE way ──────────────────
UO_N="$WORK/libunit_order_noacc.a"
if ! ( cd "$REPO" && LOGOS_UNITS_NO_ORDER_FACTS=1 "$LOGOSC" \
        --emit-module tests/logos/unit_order/unit_order.module -L "$LIBDIR" \
        -o "$UO_N" --emit-units ) > "$UO_N.log" 2>&1; then
    bad "negative control failed to build:"; tail -5 "$UO_N.log"
else
    e=$(census_field "$UO_N.units" edges)
    oe=$(census_field "$UO_N.units" order_established)
    u=$(census_field "$UO_N.units" units)
    l=$(census_field "$UO_N.units" levels)
    w=$(census_field "$UO_N.units" max_level_width)
    [ "$e"  = "0" ]     || bad "negative control: edges=$e with the accumulator suppressed — assertion A is not sensitive to the mechanism it claims to test"
    [ "$oe" = "false" ] || bad "negative control: order_established=$oe — an unestablished order was reported as derived"
    [ "$l"  = "$u" ]    || bad "negative control: levels=$l units=$u — the fallback must be the TOTAL order, one unit per level"
    [ "$w"  = "1" ]     || bad "negative control: max_level_width=$w — an unestablished order claimed parallelism"
    note "negative control: edges=$e order_established=$oe levels=$l/$u width=$w"
fi

# ── E. the canary examined something, on a fixture that HAS families ───────
# The families a container generates are produced on the CONSUMER side, so a
# library MODULE that only DECLARES a container has none — measured: ctr_mod
# reports tags=0, and a gate pointed at it would have been green while
# examining nothing. The consumer here is the single-file e2e fixture, which
# instantiates the family it declares. `--emit-units` and LOGOS_VERIFY_UNITS are
# wired on both driver paths, so this also covers the single-file half of the
# graph that the module fixtures above do not reach.
CIE_O="$WORK/container_item_e2e.o"
if ( cd "$REPO" && LOGOS_VERIFY_UNITS=1 "$LOGOSC" \
        tests/logos/pass/container_item_e2e.logos -L "$LIBDIR" \
        -o "$CIE_O" --emit-units ) > "$CIE_O.log" 2>&1; then
    vline=$(grep -m1 '^unit-verify: tags=' "$CIE_O.log")
    if [ -z "$vline" ]; then
        bad "LOGOS_VERIFY_UNITS=1 printed no verdict line — the canary was not run"
    else
        tags=$(sed -n 's/.*tags=\([0-9]*\).*/\1/p' <<< "$vline")
        split=$(sed -n 's/.*split=\([0-9]*\).*/\1/p' <<< "$vline")
        merged=$(sed -n 's/.*merged=\([0-9]*\).*/\1/p' <<< "$vline")
        [ "${tags:-0}" -gt 0 ] || bad "unit-verify: tags=0 — the canary examined NOTHING; '0 mismatches' is not a pass"
        [ "${split:-1}" = "0" ]  || bad "unit-verify: split=$split (a family across >1 generated unit)"
        [ "${merged:-1}" = "0" ] || bad "unit-verify: merged=$merged (one unit holding >1 family)"
        note "canary: $vline"
    fi
    # ── E2. "NO EDGES" MUST BE A STATEMENT, NOT A SILENCE ──────────────────
    # This fixture's metaprogram providers live in the stdlib ARCHIVES: already
    # object code, so they impose no intra-module order and produce no edge.
    # That is a different fact from "we could not find the provider", and the
    # artifact has to keep them apart — conflating them is how edges=0 came to
    # mean both "independent" and "we did not look".
    if [ -f "$CIE_O.units" ]; then
        ext=$(census_field "$CIE_O.units" external_providers)
        unr=$(census_field "$CIE_O.units" unresolved_providers)
        oe=$(census_field "$CIE_O.units" order_established)
        [ "${ext:-0}" -gt 0 ] || bad "single-file: external_providers=$ext — the stdlib-provided metaprogram sites were not accounted for"
        [ "$unr" = "0" ]      || bad "single-file: unresolved_providers=$unr"
        [ "$oe" = "true" ]    || bad "single-file: order_established=$oe"
        # ── E3. THE LOAD-BEARING NUMBER IS IN THE ARTIFACT ─────────────────
        # `fns_non_common` says how much was ATTRIBUTED; it is NOT what a
        # parallel backend can do with it, and reported alone it gets read as
        # if it were. The bound (total / largest unit) and the Common residue
        # travel with it so the two cannot be separated by a reader.
        pb=$(census_field "$CIE_O.units" parallel_bound)
        fc=$(census_field "$CIE_O.units" fns_common)
        [ -n "$pb" ] || bad "sidecar has no parallel_bound — the attribution rate would be read as a speedup"
        [ -n "$fc" ] || bad "sidecar has no fns_common — the complement of the attribution rate is missing"
        note "single-file: external=$ext unresolved=$unr parallel_bound=$pb fns_common=$fc"
    else
        bad "single-file: --emit-units wrote no sidecar $CIE_O.units"
    fi
else
    bad "container_item_e2e failed to compile under LOGOS_VERIFY_UNITS=1:"
    grep -v '^mono: note' "$CIE_O.log" | tail -5
fi

# ── F. cross-MODULE case (1) compiles and emits the generated item ─────────
XC_A="$WORK/libunit_xmod_consumer.a"
if ( cd "$REPO" && "$LOGOSC" \
        --emit-module tests/logos/unit_xmod_consumer/unit_xmod_consumer.module \
        -L "$LIBDIR" -L "$WORK" -l "$UO_A" -o "$XC_A" ) > "$XC_A.log" 2>&1; then
    if has_sym "$XC_A" 'use_gamma_xmod'; then
        note "cross-module case 1: OK (the consumer compiled against the provider MODULE's metafunction)"
    else
        bad "cross-module case 1: compiled but use_gamma_xmod is not in the archive"
    fi
else
    bad "cross-module case 1 FAILED — this is the boundary lforge crosses for every module:"
    grep -v '^mono: note' "$XC_A.log" | tail -5
fi

# ── G. the chain shape refuses; it does not drop the item ─────────────────
CH_A="$WORK/libunit_chain.a"
( cd "$REPO" && "$LOGOSC" --emit-module tests/logos/unit_chain/unit_chain.module \
    -L "$LIBDIR" -o "$CH_A" ) > "$CH_A.log" 2>&1
ch_rc=$?
if [ "$ch_rc" = "0" ]; then
    # The ONLY acceptable exit 0 is one where the item actually arrived.
    if has_sym "$CH_A" 'beta_marker_fn'; then
        note "chain: exit 0 AND beta_marker_fn present — the chain now works; tighten this branch into an unconditional success assertion"
    else
        bad "chain: exit 0 with beta_marker_fn ABSENT — the emitted item was SILENTLY DROPPED. This is the defect the guard exists to prevent."
    fi
else
    has_text "$CH_A.log" 'PLACEHOLDER' \
        || { bad "chain: refused (rc=$ch_rc) but the message does not name the mechanism:"; tail -3 "$CH_A.log"; }
    note "chain: refused loudly (rc=$ch_rc), message names the placeholder"
fi

if [ "$fail" != "0" ]; then
    echo "unit_graph_gate: FAILED"
    exit 1
fi
echo "unit_graph_gate: OK"
