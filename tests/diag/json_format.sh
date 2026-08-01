#!/usr/bin/env bash
# Test logosc --diag-format=json: structured diagnostic output.
#
# Compiles a deliberately broken file and verifies that:
#   - default mode emits "file:line: error [...]: message"
#   - --diag-format=json emits NDJSON with required fields
#   - exit code is 1 in both cases (sema/user error)

set -euo pipefail

LOGOSC="${1:-build/bin/logosc}"
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

cat > "$TMPD/bad.logos" <<'EOF'
package bad;
fn main() -> i32 {
    let x: str = 42;
    return 0;
}
EOF

# ── Default (text) mode ────────────────────────────────────────────────────
TEXT_OUT=$("$LOGOSC" "$TMPD/bad.logos" -o "$TMPD/out.o" 2>&1) && rc=$? || rc=$?
if [ "$rc" != "1" ]; then echo "FAIL: text mode exit=$rc, want 1"; exit 1; fi
case "$TEXT_OUT" in
    *"$TMPD/bad.logos:3"*"error"*"type mismatch"*) ;;
    *) echo "FAIL: text output missing expected substrings"; echo "$TEXT_OUT"; exit 1 ;;
esac

# ── JSON mode ──────────────────────────────────────────────────────────────
JSON_OUT=$("$LOGOSC" "$TMPD/bad.logos" --diag-format=json -o "$TMPD/out.o" 2>&1) && rc=$? || rc=$?
if [ "$rc" != "1" ]; then echo "FAIL: json mode exit=$rc, want 1"; exit 1; fi

# Each line must be a self-contained JSON object. Verify required fields.
LINE=$(echo "$JSON_OUT" | grep -m1 '"level":"error"')
if [ -z "$LINE" ]; then
    echo "FAIL: no error-level diagnostic in JSON output"
    echo "$JSON_OUT"
    exit 1
fi
for field in '"level":"error"' '"file":"' '"line":3' '"context":"fn main"' '"message":"'; do
    case "$LINE" in
        *"$field"*) ;;
        *) echo "FAIL: JSON missing field '$field' in: $LINE"; exit 1 ;;
    esac
done

# ── --diag-format with separate arg form ──────────────────────────────────
JSON_OUT2=$("$LOGOSC" "$TMPD/bad.logos" --diag-format json -o "$TMPD/out.o" 2>&1) && rc=$? || rc=$?
if [ "$rc" != "1" ]; then echo "FAIL: separate-arg form exit=$rc, want 1"; exit 1; fi
# NOT `echo … | grep -q`: under `set -o pipefail` grep exits at its first match
# and `echo` dies of SIGPIPE 141, which pipefail hands to the pipeline — a
# PRESENT string read as a failure. Materialise, then match the file.
printf '%s' "$JSON_OUT2" > "$TMPD/json2.txt"
grep -q '"level":"error"' "$TMPD/json2.txt" || { echo "FAIL: separate-arg form no JSON"; exit 1; }

# ── Bad value ──────────────────────────────────────────────────────────────
"$LOGOSC" "$TMPD/bad.logos" --diag-format=garble -o "$TMPD/out.o" 2>/dev/null && rc=$? || rc=$?
if [ "$rc" != "2" ]; then echo "FAIL: bad value exit=$rc, want 2 (USAGE)"; exit 1; fi

# ── Missing -I usage (also 2) ──────────────────────────────────────────────
"$LOGOSC" "$TMPD/bad.logos" -I /tmp -o "$TMPD/out.o" 2>/dev/null && rc=$? || rc=$?
if [ "$rc" != "2" ]; then echo "FAIL: -I in compile mode exit=$rc, want 2"; exit 1; fi

echo "OK"
