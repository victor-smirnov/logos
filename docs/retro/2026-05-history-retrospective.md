# Logos — git-history retrospective (snapshot 2026-05-19)

Built from `main` commit messages to reconstruct **what problems arose and how they were solved**. Messages are detailed: 62% have a body >10 lines, and nearly every fix carries a "symptom → root cause → fix → verification ctest N/N" narrative.

> Reproducibility: every number here comes from `git log` at commit
> `0d981302` (the latest at snapshot time). Commands are in the
> [Appendix](#appendix--how-to-recompute). Recomputing on a later history will
> change the numbers; this document is a point-in-time snapshot.

## Summary

| Metric | Value |
|---|---|
| Commits (non-merge) | 2045 |
| Span | 2026-03-11 → 2026-05-19 (~69 days) |
| Average rate | ~30 commits/day |
| Commits with body >10 lines | 1276 (62%) |
| Commits with subject only | 66 (3%) |
| Unique bug-IDs (B series) | 105 (B0…B111) |

"Problem/solution" markers in messages (subject+body):
`fix` 1260 · `bug` 718 · `close` 383 · `hang` 328 · `defer` 268 ·
`regress` 243 · `workaround` 103 · `baghunt` 99 · `segfault` 79 ·
`crash` 69 · `root cause` 60.

---

## Slice 1 — Bug families

### Where it hurt: bug-bearing commits by subsystem

"Bug-bearing" = subject/body contains `bug|fix|segfault|crash|hang|regress|root cause|baghunt`.
Subsystem = normalized subject prefix before the colon (`sema:`, `mono_clone:`→`mono`, …).

| Subsystem | Commits | Bug-bearing | % |
|---|---:|---:|---:|
| sema | 242 | 123 | 50% |
| stdlib | 133 | 59 | 44% |
| writ | 105 | 51 | 48% |
| compiler | 69 | 46 | **66%** |
| mono | 63 | 37 | **58%** |
| mlir-gen | 76 | 26 | 34% |
| persistent | 37 | 25 | **67%** |
| metaprog | 45 | 19 | 42% |
| imports | 48 | 17 | 35% |
| lir | 47 | 12 | 25% |
| grammar | 18 | 11 | 61% |

**Reading:** highest bug ratios belong to the youngest code (`persistent` 67%, `compiler` 66%, `mono` 58%) — errors caught immediately on hard semantics. `lir`/`mlir-gen` (25-34%) are steadier: more often receiving finished changes than active debugging.

### Chains of related bugs

Direct commit→commit references by hash are sparse (93 commits, max 2×
citations), so "families" are tracked by **bug-ID codes**, not a hash graph:

| Series | Unique IDs | Problem class |
|---|---:|---|
| `B0…B111` | 105 | main bug tracker — tactical batches |
| `P4-pm-NN` | 25 | parser/grammar features (phase 2) |
| `CP-cm-NN` | 18 | core-port compiler gaps |
| `SL-sl-NN` | 13 | stdlib surface gaps |
| `C6-cc-NN` | 10 | codegen corner cases |
| `T9-tr-NN` | 6 | trait dispatch |
| `MC-mc-NN` | 3 | macro family (line!/stringify!/…) |

Textual kinship markers appear explicitly: `same family as 5efb1bb9`
(mono per-iter refresh in walk loops), `mirrors a08c40a`,
`closed in 9856866f`. A single root mechanism (mono substitution inside
loops) spawned several bugs — and the fixes note it.

---

## Slice 2 — Long-running epics

B-numbers close **fast** (1-4 days) — tactical batches. The real epics are cross-cutting thematic lines running through almost the whole project (commit counts measured by thematic regexes over the subject):

| Theme | Commits | Active days | Span |
|---|---:|---:|---|
| Writ datatype | 310 | 50 | 03-28 → 05-18 |
| generics/traits | 275 | 47 | 04-02 → 05-19 |
| metaprog/quote | 160 | 51 | 03-28 → 05-19 |
| iterator/closures | 113 | 42 | 04-07 → 05-19 |
| mono substitution | 110 | 43 | 04-06 → 05-19 |
| imports/archive | 106 | 47 | 04-02 → 05-19 |
| Drop/ownership | 103 | 51 | 03-28 → 05-18 |
| persistent/B-tree | 76 | **67** | 03-12 → 05-19 |
| fiber/runtime | 41 | 47 | 04-02 → 05-19 |
| multi-arena IR | 31 | 49 | 03-28 → 05-16 |
| fmt/Formatter | 31 | 49 | 03-30 → 05-19 |

`Phase` labels confirm the length: Phase 1A span 52 days, Phase 4 — 38 commits over 40 days. **Architectural phases run long; point bugs do not.** persistent was laid down on day one and runs the whole project — the foundation of the Memoria co-development.

---

## Slice 3 — Workaround vs fundamental fix

Policy is "fix the root, don't route around it." The message lexicon bears it out:

| Workaround / deferral | × | | Fundamental | × |
|---|---:|---|---|---:|
| defer/deferred | 268 | | rewrite/rework/refactor | 210 |
| workaround | 103 | | supersede / no-longer | 96 |
| for now | 44 | | root cause | 60 |
| temporary | 25 | | proper fix | 59 |
| TODO/FIXME | 17 | | revert | 43 |
| hack | 3 | | rescind | 6 |

**~172 "workaround" vs ~474 "fundamental"** (defer counted separately — deliberate prioritization, not debt). Health signal: workarounds **don't accrete** — a distinct class of commits *retires* a workaround with a fundamental fix. Examples:

- `a6a04330` — match-arm pattern bindings now fire Drop at arm exit
- `9856866f` — quote-walk no longer dereferences CALL.CALLEE as a TOM
- `d640cb8d` — fixed `Box` vs stdlib-`Box` cross-package layout collision
- `a202bbc3` — Slice is 16 bytes: fixed variant-payload truncation
- `33693de8` — generic-struct Drop dispatch (replacing a manual workaround)

`hack`=3, `FIXME`=0 against 210 `refactor` — the fingerprint of "fix the root, not the symptom." 6 `rescind`s mark previously-declared *gaps* found already closed and withdrawn — the gap catalog is actively reconciled against reality.

---

## Slice 4 — Activity over time × topic

Commits by ISO week (bar normalized to the peak):

```
2026-03-09  W11      3  ·                     persist (project start)
2026-03-23  W13      9  ·                     writ, arena/IR    [W12 = 0: gap]
2026-03-30  W14     59  #####                 writ:21 runtime:6 mlir-gen:5
2026-04-06  W15    221  ####################   traits:49 writ:31 sema:26
2026-04-13  W16    168  ###############        writ:101 traits:45 sema:22
2026-04-20  W17    404  ######################################  writ:80 sema:68 mlir-gen:58
2026-04-27  W18    249  #######################  metaprog:73 sema:48 writ:28
2026-05-04  W19    308  #############################  sema:85 persist:46 metaprog:33
2026-05-11  W20    531  ##################################################  imports:121 coreport:92 traits:67
2026-05-18  W21     93  ########              traits:21 sema:21 mono:19   [week not closed]
```

Phase-shaped waves of focus:

- **W11-W14 (March):** laying down Writ + runtime, slow start.
- **W15-W17 (April):** explosive growth, **Writ-dominant** (101 commits in
  W16) + traits + the first serious push into mlir-gen. Volume peak — W17 (404).
- **W18-W19:** focus on **metaprog/quote**, then persistent.
- **W20 (peak 531):** pivot to **imports/archive + core-port** — mass intake of
  rustc tests and the fight with archives/linking (the stale-embed footgun
  lived here, closed by `cce5c2f6`).
- **W21:** cooling down on traits/sema/mono polish.

**Layering over time:** Writ is the foundation (early, dominated April); metaprog and persistent are middle layers; imports + core-port is the late mass layer, appearing once the language could run third-party tests.

---

## Takeaways

1. **History is self-documenting.** Detailed messages + bug-ID series + related-commit references reconstruct "what broke and how it was fixed" almost without external sources. (Some deep investigation context lives in out-of-repo `baghunt_*.md`, which commits reference.)
2. **Healthy debt balance:** ~2.7× more fundamental fixes than workarounds, actively retired rather than accumulated.
3. **Clear layered trajectory:** foundation (Writ, persistent) → semantics (sema/mono/traits) → metaprogramming → maturity layer (imports + core-port). Each layer makes its own weekly-chart wave.
4. **Point bugs cheap, architecture expensive:** B series closes in days; Phase series live 40-52 days — expected and healthy.

---

## Appendix — how to recompute

```bash
# Summary
git rev-list --count HEAD
git log --format='%cI %s' --no-merges | wc -l

# Slice 1: bug-bearing by subsystem (subject prefix + bug flag in subject/body)
git log --format='%H%x09%s' | while IFS=$'\t' read h s; do
  pre=$(echo "$s" | grep -oP '^[a-zA-Z0-9_+./-]+(?=:)' | head -1)
  bug=$(git log -1 --format='%s %b' $h | grep -ciE 'bug|fix|segfault|crash|hang|regress|root cause|baghunt')
  echo "$pre|$bug"
done   # then normalize prefixes + aggregate (see the awk used in the session)

# bug-ID series
git log --format='%s %b' | grep -oP '\b(CP-cm-[0-9]+|SL-sl-[0-9]+|P4-pm-[0-9]+|C6-cc-[0-9]+|T9-tr-[0-9]+|MC-mc-[0-9]+|B[0-9]{1,3})\b' \
  | sort -u | sed -E 's/-[0-9]+$//; s/[0-9]+$//' | sort | uniq -c | sort -rn

# Slice 3: workaround vs fundamental lexicon
git log --format='%s %b' --no-merges | grep -ciE 'workaround|work[ -]around'
git log --format='%s %b' --no-merges | grep -ciE 'rewrit|rework|refactor'
# (full marker list — in the body of this document)

# Slice 4: commits by ISO week + top topics — python3 over `git log --format='%cI%x09%s'`
```
