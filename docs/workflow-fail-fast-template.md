# Workflow template: the three-outcome probe branch

Derived 2026-08-07 from the P3b round, where the probe found the blocker at minute 15 and the script
had no branch to act on it — so the implement agent spent ~95 further minutes reconciling a brief that
had already been superseded, and said so in prose. Prose is the wrong place for that reconciliation.

There is **no timeout option** on `agent()` (`label`, `phase`, `schema`, `model`, `effort`, `isolation`,
`agentType`). Fail-fast is therefore structural, never temporal.

## The shape

```js
const probe = await agent(PROBE, {
  effort: 'low', phase: 'Probe',
  schema: { type: 'object',
    required: ['verdict', 'evidence', 'blocker', 'smallest_provable_piece'],
    properties: {
      verdict: { enum: ['feasible', 'partial', 'blocked'] },
      evidence: { type: 'string' },                  // quoted code / measured exit codes
      blocker: { type: 'string' },
      smallest_provable_piece: { type: 'string' },
    } } })

let impl
if (probe.verdict === 'blocked') {
  impl = await agent(`${REFUSE_BRIEF}\n\nBLOCKER: ${probe.blocker}\nEVIDENCE: ${probe.evidence}`,
                     { isolation: 'worktree' })
} else if (probe.verdict === 'partial') {
  impl = await agent(`${PARTIAL_BRIEF}\n\nBUILD EXACTLY: ${probe.smallest_provable_piece}`,
                     { isolation: 'worktree' })
} else {
  impl = await agent(FULL_BRIEF, { isolation: 'worktree' })
}
```

Each outcome gets its **own** brief. Handing the full brief to a `blocked` round is what produces a
report whose first line explains why the agent did something else.

## The probe question decides everything

"Is this feasible?" is answered `yes` by every agent. The probe must ask something with an answer **in
the code**, and the schema must force the evidence out:

- bad: *"can we make the fixpoint incremental?"*
- good: *"can `__wql_<q>_scc<c>` be re-entered with a delta seed without changing its from-scratch
  callers? Quote the signature and the call site."*

Two rounds of evidence that this works:

| round | probe cost | probe result |
|---|---|---|
| P3b (08-07) | ~15 min | six compiled probes; four measured wrong answers; a live defect nobody suspected |
| P3b-2 (08-07) | ~6 min | *cheaper* than predicted — 0 threaded signatures instead of 6, because `emit_incremental` already receives `MacroParams` |

The probe is as likely to say "cheaper than you thought" as "blocked". It is not a pessimism filter.

## A checkpoint inside the expensive stage

Not "stop if it gets hard" — a trigger tied to an artifact:

> As soon as the emitter first compiles, show one epoch's answer against the batch. If you cannot,
> **stop, commit what you have, and report.** Continuing past that point without that answer is
> forbidden.

This catches the real failure mode, which is not trying and failing — it is spending the remaining
hours concealing that there is no answer yet.

## Effort

`effort: 'low'` for probes, censuses, gate scripts, and file reading. Reserve the expensive tiers for
the emitter and for adversarial verification.

## An honest early NO is a delivered round

P3b's refusal found a live defect: a handle over a declared `rel` answered wrong while returning `Ok`.
If a `blocked` verdict is scored as failure, the next agent will run to the end rather than report it.
