# Track 3 — gap-per-imported-test trend

The headline metric: **how many catalogued gaps does an average imported
rustc test surface?** Phase 1 ran at ~2 gaps/test (every test tripped
something new). The trend should fall as Logos's surface grows.

## Per-batch ledger

Counted *at the time the batch landed*. "Gaps" = new catalog entries
(any status — Closed, Partial, Open, Divergence) emitted between the
previous batch's commit and this batch's commit. Retroactive fixes that
silently un-trim earlier imports don't count here (logged in
`docs/track3-gaps/*-gaps.md` instead).

| Batch | Date | Tests | New gaps | gaps / test | Cumulative tests | Cumulative gaps |
|---|---|---|---|---|---|---|
| B1   | 2026-05-11 | 11 |  ≈ 18 | 1.6 | 11  | 18  |
| B2   | 2026-05-11 | 11 |  ≈ 14 | 1.3 | 22  | 32  |
| B3   | 2026-05-11 | 12 |  ≈ 15 | 1.3 | 34  | 47  |
| B4   | 2026-05-11 |  5 |  ≈ 11 | 2.2 | 39  | 58  |
| B5   | 2026-05-11 |  3 |  ≈  5 | 1.7 | 42  | 63  |
| B6   | 2026-05-11 |  5 |  ≈  9 | 1.8 | 47  | 72  |
| B7   | 2026-05-11 |  3 |  ≈  4 | 1.3 | 50  | 76  |
| B8   | 2026-05-11 |  6 |  ≈  6 | 1.0 | 56  | 82  |
| B9   | 2026-05-11 |  2 |  ≈  5 | 2.5 | 58  | 87  |
| B10  | 2026-05-11 |  3 |  ≈  3 | 1.0 | 61  | 90  |
| B11  | 2026-05-11 |  3 |  ≈  3 | 1.0 | 64  | 93  |
| B12  | 2026-05-11 |  6 |     1 | 0.17 | 70  | 94  |
| B13  | 2026-05-11 |  4 |     0 | 0.00 | 74  | 94  |
| B14  | 2026-05-11 |  4 |     1 | 0.25 | 78  | 95  |
| B15  | 2026-05-11 |  4 |     0 | 0.00 | 82  | 95  |
| B16  | 2026-05-11 |  3 |     1 | 0.33 | 85  | 96  |
| B17  | 2026-05-11 |  5 |     0 | 0.00 | 90  | 96  |
| B18  | 2026-05-11 |  3 |     0 | 0.00 | 93  | 96  |
| B19  | 2026-05-11 |  5 |     2 | 0.40 | 98  | 98  |
| B20  | 2026-05-11 | 12 |     6 | 0.50 | 110 | 104 |
| B21  | 2026-05-11 |  9 |     0 | 0.00 | 119 | 104 |
| B22  | 2026-05-11 |  4 |     2 | 0.50 | 123 | 106 |
| B23  | 2026-05-11 |  7 |     0 | 0.00 | 130 | 106 |
| B24  | 2026-05-11 |  3 |     0 | 0.00 | 133 | 106 |
| B25  | 2026-05-11 |  2 |     0 | 0.00 | 135 | 106 |
| B26  | 2026-05-11 |  4 |     0 | 0.00 | 139 | 106 |
| B27  | 2026-05-11 |  2 |     0 | 0.00 | 141 | 106 |

(Phase-1 counts are estimates — pre-batch gap-as-code triage gave coarse
totals only; precise per-batch arrival-order numbers weren't recorded.
Phase-2 onward — from B12 — counts are exact.)

## Reading

Phase 1 (B1–B11) baseline: **~1.5 gaps/test**. Many tests surfaced
multiple gaps because Logos's pattern, coercion, fn-family, generics,
and metaprog surfaces were all rough at once.

Phase 2 (B12+): trending toward **~0.2 gaps/test** as the easy/middling
surfaces close. New gaps are mostly narrow codegen issues (e.g.
P4-pm-17 ref-bind deref chain) or specialised patterns (tuple-rest,
exclusive ranges). Single gap can block multiple tests, so total
unblocked-tests-per-gap-closure is also worth eyeballing.

Update this table whenever a numbered batch lands.
