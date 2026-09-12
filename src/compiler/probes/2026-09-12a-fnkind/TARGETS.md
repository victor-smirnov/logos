# TARGET ROWS — round 2026-09-12a, chosen BEFORE any compiler edit

Subject: `tests/logos/bc_admits.ledger` (84 rows, `# TOTAL 84`).

## The rows, by name

| row (fixture) | root | why in this block |
|---|---|---|
| `borrow-immutable-upvar-mutation-impl-trait` | `bck.NEW-CMUT` | never surveyed: a literal grep of `NEW-CMUT` against `src/compiler/PROBES.md` reads **1** (the string, not a survey) |
| `borrowck-unboxed-closures`                 | `bck.A-FNMUT`  | never surveyed: grep of `A-FNMUT` against PROBES.md reads **0** |
| `borrowed-referent-issue-38899`             | `bck.NEW-BLOCKREF` | never surveyed: grep of `NEW-BLOCKREF` against PROBES.md reads **0**; the row's own header says SINGLE-ROW ROOT, no separating pair |

## Why this block over the others

* The prompt's handed-down never-surveyed list is a HYPOTHESIS and it is stale
  (rule 17). Re-derived today by grepping every root id in the ledger against
  PROBES.md: `nllmoves.R11-ASSIGN` is gone (closed `b6e916800`), `bck.NEW-N4`
  does not exist — the ledger spells it `lifereg.NEW-N4` — and `nllmoves.R5`
  now reads 5 hits, not 0. The surviving 0/1-hit roots are the seven above plus
  `lifereg.NEW-E0226`, `lifereg.NEW-PROJBOUND`, `nllmoves.R14`.
* All three are EXCLUDED-list-free: none is in the `lifereg.B` / `NEW-B2`
  holder-deposit door plane, none is `argresvact`, `bck.D`, `nllmoves.D` or
  `bck.NEW-CAPMOVE`, and none of the three is an A16 row.
* `bck.NEW-CMUT` has the shape the prompt says has paid every time: **an arm
  that EXISTS, reached through a fact the code does not carry.** The E0525 arm
  (`check_type_bounds`, sema_collect.cpp:1551-1567) prints exactly upstream's
  verdict and FIRES today; what is missing is the ARRIVAL.

## Grouping, tested rather than assumed

Does ONE candidate change move more than one member? **No — three roots.**
`NEW-CMUT` wants the declared `impl Trait`/`dyn Trait` bound routed to the
kind check; `A-FNMUT` wants a `mut`-binding requirement at a call of an
FnMut-bounded callee (an arm that does not exist anywhere); `NEW-BLOCKREF`
is a loan-conflict question through `&mut &mut`. Consistent with the last two
rounds: five defects, five roots.
