#!/usr/bin/env bash
# ty_default_gate.sh LOGOSC TSRC FACTS — ADR 0031 R4.5: no well-formed query
# reads a name as the type dictionary's `i64` baseline.
#
# `ElTypes::ty_name_of` answers `i64` for a name nothing typed; under
# LOGOS_TRACE_PLAN it says so (`[plan] ty-default <name>`). Every pass fixture's
# trace is in FACTS/<fixture>/plan.err; the count of those lines must be 0.
# Canary: a fail fixture whose user error leaves a column untyped still reaches
# the fallback, so the sensor is proven live on every run.
set -u
LOGOSC="$1"; TSRC="$2"; FACTS="$3"
n=$(ls -d "$FACTS"/*/ 2>/dev/null | wc -l)
if [ "$n" -lt 4000 ]; then
    echo "FAIL: $n fixture trace(s) under $FACTS, floor 4000 — the census went vacuous"; exit 1
fi
hits=$(grep -l "^\[plan\] ty-default " "$FACTS"/*/plan.err 2>/dev/null)
if [ -n "$hits" ]; then
    echo "FAIL: a pass fixture reads a name as the i64 baseline:"
    for f in $hits; do echo "  $(basename "$(dirname "$f")"): $(grep -h '^\[plan\] ty-default ' "$f" | sort -u | sed 's/\[plan\] ty-default //; s/ —.*//' | tr '\n' ' ')"; done
    exit 1
fi
canary="$TSRC/fail/wql_column_decl_tuple_col_fail.logos"
if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$canary" -o /dev/null 2>&1 | grep -q "^\[plan\] ty-default x "; then
    echo "FAIL: canary — $canary no longer reaches the i64 fallback, so this gate cannot prove its sensor fires"; exit 1
fi
echo "ty-default census: 0 over $n pass fixture traces; canary live."
