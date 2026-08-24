# Operating Instructions — Autonomous Track 3 Work

This file is read by **AI agents** (and human contributors) doing
the rustc test-import + gap-grind work autonomously. It captures
the user's operating preferences for Track 3 specifically.
Read it before each batch.

## Operating mode

* **Autonomous, weeks-scale.** The maintainer will not be replying
  in real time. Make decisions, document them in commit messages
  and manifest rows, keep moving. The maintainer reviews
  periodically; surprise is expected to be low because of clear
  per-batch commits.
* **Don't ask permission to proceed within scope.** If the strategy
  in [STRATEGY.md](STRATEGY.md) covers it, just do it. Ask only
  when a decision falls outside the strategy or contradicts an
  earlier choice.
* **Commit per batch.** A batch = 30-60 ported tests + manifest
  rows + ctest green. One commit. Commit message names the
  rustc category + count + any failures triaged.

## Default to the most fundamental implementation

When the test reveals a Logos gap, the **default action is to
close the gap properly** — design + implement + test — rather
than skip or hack around the test. Concrete decisions:

* **A real Logos compiler bug** (mismatch between sema rules and
  what they ought to be): fix it. Add a regression test from the
  rustc import. Commit fix and import together (or fix as a
  separate prior commit if it's substantial).
* **A missing Logos language feature** (e.g. some construct not
  yet implemented): implement it if it falls in the "deferred"
  block of STRATEGY.md or is a reasonable scope. If it's a major
  multi-week piece (Datalog resolver, async equivalent), park
  the test, file a gap entry, move on.
* **A missing stdlib API** (e.g. some collection method not
  ported yet): port it from rust-lang/rust under
  `stdlib/imported/` with full provenance, or hand-roll if the
  Rust version drags in too many internal dependencies. Same
  per-file header / manifest rules.
* **A diagnostic-quality difference** (Logos's error message
  wording differs from rustc's): keep Logos's wording, update
  the `.expected` file to match Logos.

**No "we'll fix later" markers in committed code.** If a test
can't land cleanly, either fix the underlying issue or leave
the test out and record it in the gap registry. Half-finished
ports rot in the worst way.

**Only simplify when truly stuck.** If after a reasonable effort
the fundamental implementation looks like a multi-week excursion
out of scope, write a brief design note, file the gap, and skip
the test. The maintainer will weigh in on next review.

## Logos features win over Rust features

The goal is **not** Rust source compatibility. The goal is a
strong Logos test suite. When the rust test exercises a Rust-
specific mechanic that conflicts with a Logos design choice:

1. **Adapt the test to Logos's behaviour**, exercising the same
   underlying concept. Not 1:1 port — semantic port.
2. **Update the per-file provenance header** to note the
   adaptation in the `Modifications:` line.
3. **Update `.expected`** to match Logos's actual output.

Examples:

| Rust behaviour | Logos behaviour | Action |
|---|---|---|
| `v[0..2]` returns `&[T]` | Slice indexing returns `&[T]` too — likely matches | port as-is |
| `vec![1, 2, 3]` | `vec!(1, 2, 3)` (paren form) or `vec![...]` | port to whatever Logos accepts |
| `format!("{}", x)` with rust-specific format spec | Logos format-engine: closest match | port with adapted spec |
| Rust diagnostic: "expected `{`, found `;`" | Logos: "expected `{`, got `;`" | port; update `.expected` to Logos wording |
| Rust feature: `async fn` | Logos: fibres | DO NOT port; permanent skip |
| Rust feature: `?` operator | Logos: not yet implemented | DEFERRED — don't import this test until `?` lands |

**Why this rule exists**: Logos and Rust will diverge further as
Logos adds verification (effects, refinement types, etc.).
Anchoring tests to Rust's surface freezes us into Rust's design
choices. Anchor them to Logos's instead — they keep value as
Logos evolves.

## Per-batch workflow checklist

For each batch:

1. **Pick category + scope** per STRATEGY.md ordering. If the
   strategy says "parser top-level" but parser/ is too big for
   one batch, pick a subset (e.g. "parser if-expression tests")
   and note it in the batch row.
2. **Source paths**: rustc tests at `/home/logos/cxx/rust/tests/ui/`.
   Pinned commit is in [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md);
   each batch reuses that SHA unless a new pin is needed.
3. **For each test**:
   * Copy `<name>.rs` → `tests/imported/ui/<category>/<name>.logos`.
   * Add provenance header (see [README.md](README.md) for format).
   * Mechanical port: imports, entry point, syntax tweaks.
   * Adapt to Logos behaviour where it diverges from Rust.
   * Add `.expected` matching Logos's actual output.
4. **Manifest row** for each test in
   [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).
5. **Run ctest**. Green expected. Failures → fix or defer (see
   the gap-handling rules above).
6. **Commit**. Message: "imports/<category>: <N> tests (batch N)" +
   bullet list of any gaps closed inline.

## Tools and references

* `/home/logos/cxx/rust` — local clone of rust-lang/rust. Browse
  `tests/ui/` here. ⚠ It is a SPARSE clone (`--filter=blob:none --sparse`,
  `sparse-checkout set tests/ui`, 189 MB instead of the whole repo), so
  `library/{core,alloc,std}/src/` is NOT checked out; widen the sparse set
  if a batch needs it. The path was `/home/victor/cxx/rust` until the
  2026-08-01 box migration, which left this file pointing at a directory
  that no longer exists — every import agent read it, found nothing, and
  had to rediscover the corpus. Re-cloned 2026-08-24.
* `git -C /home/logos/cxx/rust rev-parse HEAD` — current upstream.
  Pin a new SHA if the current commit drifts more than a few
  months from the manifest row.
* `cd /home/logos/devel/logos/build && ctest -j$(nproc)` — test
  runner. Always green before commit. ⚠ `ctest` DEFAULTS TO -j1, so the
  flag is not optional; and it is `$(nproc)` rather than the `-j12` this
  line carried until 2026-08-24, because 12 was the OLD box's core count
  and this one has 32.
* `cd /home/logos/devel/logos/build && ninja` — rebuild after
  compiler changes.
* `tests/imported/STRATEGY.md` — what to import in what order.
* `tests/imported/WHY-WE-SKIP.md` — what NOT to import and why.
* `stdlib/imported/README.md` + `stdlib/imported/RUSTC-PROVENANCE.md`
  — same workflow for stdlib imports when test imports reveal a
  stdlib gap that's better filled by porting than hand-rolling.

## When spawning sub-agents

If you spawn an `Explore` or `general-purpose` agent for batch
work, prepend a one-paragraph summary of this file to the agent's
prompt — they don't auto-read it. Key points to convey:

* Autonomous mode; don't ask the user.
* Logos features win in any conflict.
* Default to fundamental fix, not skip.
* Per-file provenance header + manifest row are mandatory.

## When in genuine doubt

Stop and write a short design note (markdown file in
`tests/imported/notes/` or similar) summarising:

* The decision point.
* The options considered.
* Recommended approach.

Continue with the recommendation. The maintainer sees the note
on next review and either agrees or course-corrects.

The opposite failure mode — **stopping mid-batch waiting for an
answer** — is worse than a wrong-but-revertible decision. Keep
moving.
