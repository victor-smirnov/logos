#!/usr/bin/env bash
# perf-slow.sh — the slowest tests (>5s wall in a full L4 run, captured 2026-06-21),
# carved out as a runnable subset for performance work. Run from the build/ dir:
#
#   bash ../tests/logos/perf-slow.sh            # run them, sorted slowest-first
#   bash ../tests/logos/perf-slow.sh 1          # serial (-j1): truest per-test wall
#   bash ../tests/logos/perf-slow.sh 8          # parallel -j8
#
# Two clusters dominate: `coretest_*` batches (many #[test] fns + generic iterators)
# and `persistent_*`/`dview` (heavy persistent data structures). Profile a single
# one with `--stats` to find the hot phase, e.g.:
#   ./bin/logosc ../tests/imported/pass/.../<t>.logos -L lib --test --stats -o /tmp/x.o
#
# Re-capture the list any time from a full run:
#   ctest -j12 | sed -E 's/.*Test #[0-9]+: +([^ ]+) .*(Passed|Failed) +([0-9.]+) sec.*/\3 \1/' \
#     | sort -rn | awk '$1>5'
set -uo pipefail

JOBS="${1:-4}"

# Exact ctest names (suffix-anchored regex), slowest-first.
TESTS=(
  logos_02_semantic_core_pass_test_harness_coretest_batch_b49        # ~12.1s
  logos_02_semantic_core_pass_test_harness_coretest_batch_b51_dei    # ~9.6s
  logos_14_lforge_dump_metacall                                      # ~8.9s
  logos_02_semantic_core_pass_test_harness_coretest_result           # ~8.3s
  logos_02_semantic_core_pass_dview_try_insert                       # ~7.6s
  logos_02_semantic_core_pass_persistent_small_fanout                # ~7.4s
  logos_02_semantic_core_pass_persistent_dag_topology                # ~7.3s
  logos_02_semantic_core_pass_persistent_multi_container             # ~6.9s
  logos_02_semantic_core_pass_persistent_handle_smoke                # ~6.9s
  logos_02_semantic_core_pass_test_harness_coretest_iter_zip_nth     # ~6.5s
  logos_02_semantic_core_pass_persistent_release                     # ~6.5s
  logos_02_semantic_core_pass_test_harness_coretest_int_battery      # ~6.3s
  logos_02_semantic_core_pass_test_harness_coretest_iter_zip_dei     # ~6.3s
  logos_02_semantic_core_pass_persistent_remove_patterns             # ~5.8s
  logos_02_semantic_core_pass_test_harness_coretest_nonzero          # ~5.8s
)

# Anchored alternation: ^(name1|name2|...)$ — each name is exact (no accidental
# match of e.g. coretest_result_combinators by coretest_result).
re="^($(IFS='|'; echo "${TESTS[*]}"))\$"

echo "perf-slow: ${#TESTS[@]} tests, -j${JOBS}"
out=$(ctest -R "$re" -j"$JOBS" 2>&1)
echo "$out" | grep -E "Test #[0-9]+:" \
  | sed -E 's/.*Test #[0-9]+: +([^ ]+) .*(Passed|Failed) +([0-9.]+) sec.*/\3s \2 \1/' \
  | sort -rn
echo "$out" | grep -iE "tests passed|tests failed|Total Test time" | tail -2
