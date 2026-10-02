# ⚠ THIS FILE EXISTS TO MAKE A MISTAKE LOUD. IT REGISTERS NO TESTS.
#
# `ctest` looks for CTestTestfile.cmake in the CURRENT directory, so run from
# the source root it used to answer "No tests were found!!!" rc=1 — a failure
# that reads like a red tree, and was once mistaken for one.
#
# ctest is now retired altogether: scripts/lt discovers the tests from the tree
# (tests/lt_registry.py) and runs them. The build dir carries the same guard
# (root CMakeLists.txt, `tests_are_run_by_scripts_lt`); this file covers a bare
# `ctest` typed at the source root.
#
# ⚠ Kept out of .gitignore by an explicit negation for this path only; the
# generated CTestTestfile.cmake under build/ stays ignored.
message(FATAL_ERROR
  "ctest is retired in this repo: tests are discovered and run by scripts/lt "
  "(python3 scripts/lt run --plus 10; python3 scripts/lt task ...). See AGENTS.md.")
