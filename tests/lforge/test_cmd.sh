#!/usr/bin/env bash
# B1.5 smoke for `lforge test`: discover + compile + run tests/*.logos files,
# aggregate pass/fail, exit 0 iff all pass.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

# Project: a lib + a few tests that link against the lib.
mkdir -p "$PROJ/src/core" "$PROJ/tests"

cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core", src: "src/core" }
    ]
}
EOF

cat > "$PROJ/src/core/core.logos" <<'EOF'
package core;
pub fn answer() -> i32 { return 42; }
EOF

cat > "$PROJ/tests/passing_test.logos" <<'EOF'
package passing_test;
use core;
fn main() -> i32 {
    if answer() != 42 { return 1; }
    return 0;
}
EOF

cat > "$PROJ/tests/another_passing.logos" <<'EOF'
package another_passing;
fn main() -> i32 { return 0; }
EOF

cat > "$PROJ/tests/failing_test.logos" <<'EOF'
package failing_test;
fn main() -> i32 { return 7; }
EOF

cd "$PROJ"

# Run lforge test. Should fail overall (one test exits 7), but should still
# build the lib and run all three tests, reporting 2 passed / 1 failed.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test > "$PROJ/test.log" 2>&1 && rc=$? || rc=$?

if [ "$rc" = "0" ]; then
    echo "FAIL: 'lforge test' should exit non-zero with one failing test"
    cat "$PROJ/test.log"
    exit 1
fi

grep -q "passed=2" "$PROJ/test.log" || { echo "FAIL: missing 'passed=2'"; cat "$PROJ/test.log"; exit 1; }
grep -q "failed=1" "$PROJ/test.log" || { echo "FAIL: missing 'failed=1'"; cat "$PROJ/test.log"; exit 1; }

# Per-test artifacts under .lforge/<profile>/test/
[ -x "$PROJ/.lforge/debug/test/passing_test" ]   || { echo "FAIL: no test bin for passing_test"; exit 1; }
[ -x "$PROJ/.lforge/debug/test/another_passing" ] || { echo "FAIL: no test bin for another_passing"; exit 1; }
[ -x "$PROJ/.lforge/debug/test/failing_test" ]   || { echo "FAIL: no test bin for failing_test"; exit 1; }

# When all tests pass, lforge test exits 0.
rm "$PROJ/tests/failing_test.logos"
rm -rf "$PROJ/.lforge"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test > "$PROJ/test2.log" 2>&1 && rc=$? || rc=$?
if [ "$rc" != "0" ]; then
    echo "FAIL: 'lforge test' should exit 0 when all pass"
    cat "$PROJ/test2.log"
    exit 1
fi
grep -q "passed=2" "$PROJ/test2.log" || { echo "FAIL: missing 'passed=2'"; exit 1; }
grep -q "failed=0" "$PROJ/test2.log" || { echo "FAIL: missing 'failed=0'"; exit 1; }

# Empty tests/ — lforge test reports nothing-to-test, exits 0.
rm -rf "$PROJ/tests"
rm -rf "$PROJ/.lforge"
mkdir "$PROJ/tests"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test > "$PROJ/test3.log" 2>&1 && rc=$? || rc=$?
[ "$rc" = "0" ] || { echo "FAIL: empty tests dir should exit 0"; cat "$PROJ/test3.log"; exit 1; }

# No tests/ dir at all — also exit 0 with a message.
rm -rf "$PROJ/tests"
rm -rf "$PROJ/.lforge"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test > "$PROJ/test4.log" 2>&1 && rc=$? || rc=$?
[ "$rc" = "0" ] || { echo "FAIL: missing tests dir should exit 0"; cat "$PROJ/test4.log"; exit 1; }
grep -q "no tests" "$PROJ/test4.log" || { echo "FAIL: missing 'no tests' message"; cat "$PROJ/test4.log"; exit 1; }

echo "OK"
