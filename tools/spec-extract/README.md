# Language-spec extraction harness

Recover the **Logos language specification** from its source of truth — the
compiler — and keep it refreshed deterministically. The spec is derived from
three layers of rules: **grammar** (`logos.peg`), **sema**, and **mono**
(codegen is target-flavored and off by default).

The task is too large for one context, so it is split: a deterministic script
owns everything mechanical, and models only ever see one bounded work-unit at a
time plus a fixed prompt.

## The two halves

| Half | Who | What |
|---|---|---|
| **Deterministic skeleton** | `chunk.py` (no model) | enumerate sources → slice into bounded work-units at natural boundaries → content-hash each → `manifest.json` → compute the stale set → reverse-map spec ids to units (targeted mode) |
| **Model work** | `spec-extract.workflow.js` | one agent per stale unit extracts structured **rules** → `rules/<unit>.json`; then one agent per affected section assembles prose → `docs/spec/<section>.md` |

Workflow JS has no filesystem access, so the chunker (a real on-disk script)
produces the plan and the agents do all file I/O via their own Read/Write tools.

## Artifacts

```
tools/spec-extract/
  config.json              layers, sources, chunk budget, id domains, section map
  chunk.py                 the deterministic skeleton (stdlib only)
  rule.schema.json         schema every rules/*.json conforms to
  spec-extract.workflow.js the orchestration (extract + assemble)
  manifest.json            generated: all units + hashes
  rules/<layer>/<file>/<sym>.json   generated: structured rules per unit (git-tracked)
docs/spec/<section>.md     generated: the assembled prose spec
```

## Addressing: human-readable ids

Every extracted rule carries a **stable** id `domain.group.slug`
(e.g. `expr.cast.numeric-truncation`, `grammar.item.struct-decl`,
`mono.subst.const-arg`). This is the permanent handle:

- assembled spec renders each rule under an anchor of its id;
- `chunk.py ids` prints the full address book (`id → unit → title`);
- targeted re-runs are keyed on these ids (see below).

Ids are never renamed across runs: on re-extraction the chunker feeds the unit's
prior ids back to the agent, which must reuse them.

## Running

Everything runs from the repo root.

### Full / periodic refresh

```bash
python tools/spec-extract/chunk.py manifest          # refresh unit inventory
python tools/spec-extract/chunk.py plan > /tmp/plan.json   # stale units (changed since last extract)
```
Then run the workflow with that plan as `args`:
> Workflow({ scriptPath: "tools/spec-extract/spec-extract.workflow.js", args: <contents of /tmp/plan.json> })

Because staleness is per-unit content-hash, a periodic run only re-extracts
units whose source bytes changed since their `rules/*.json` was written — steady
state is cheap. First run extracts everything.

### Targeted ("точечный") mode

When the spec shows a rule that's wrong/outdated and you want to re-derive just
its source:

```bash
# by spec-component id (reverse-mapped to the unit(s) that define it):
python tools/spec-extract/chunk.py plan --only 'expr.cast.*'   > /tmp/plan.json
# by unit id:
python tools/spec-extract/chunk.py plan --touch 'sema/sema_expr/lower_cast' > /tmp/plan.json
```
Run the workflow on that plan; only the named units re-extract and only the
sections whose domains changed are reassembled.

### Other commands

```bash
python tools/spec-extract/chunk.py status              # units / stale / rule counts per layer
python tools/spec-extract/chunk.py ids                 # the id → unit address book
python tools/spec-extract/chunk.py plan --layer grammar   # scope to one layer
python tools/spec-extract/chunk.py plan --force        # re-extract everything in scope
```

Reassemble prose without re-extracting (e.g. after editing the assembly prompt):
pass `assemble_only: true` alongside the plan in `args`. Extract without touching
prose: `no_assemble: true`.

## Collisions (one id ↔ one rule)

A rule `id` is the permanent address, so it must identify exactly one rule.
Independent per-unit agents can converge on the same slug for different rules.
Detect and resolve:

```bash
python tools/spec-extract/chunk.py collisions            # report (FRAG vs corrob)
python tools/spec-extract/chunk.py collisions --json > tools/spec-extract/.collisions.json
```
Feed `.collisions.json` (path mode) to the dedup workflow — one agent per
collision decides **merge** (same rule stated twice → one rule, id kept, evidence
unioned) or **split** (distinct rules → new specific slugs, at most one keeps the
original id):
> Workflow({ scriptPath: "tools/spec-extract/spec-dedup.workflow.js", args: { collisions_path: "tools/spec-extract/.collisions.json", ids: [<the colliding ids>] } })

Apply the returned decisions deterministically, then reassemble:
```bash
python tools/spec-extract/chunk.py apply-dedup tools/spec-extract/.dedup-decisions.json
python tools/spec-extract/chunk.py collisions      # expect: 0
```
Run `collisions` after any large extraction; resolve before adding spec tests
(tests address rules by id).

## Markdown safety

Bare `Type<...>` in prose is swallowed as an HTML tag by the renderer (the angle
brackets and their contents vanish). The assembler is prompted to backtick-wrap
type/generic notation, but as a deterministic, idempotent safety net run after
any assembly:

```bash
python tools/spec-extract/chunk.py mdsafe   # wrap bare Type<...> in docs/spec/*.md
```
It skips fenced/indented code and already-backticked spans, so it is safe to
re-run. Rule artifacts stay plain text — markdown escaping is a render concern.

## Determinism guarantees

- Unit boundaries are natural and stable: C++ column-0 definitions; PEG directive
  blocks + rule heads. Oversized spans (a giant `switch`) split at blank lines
  nearest to target offsets — stable positions, so unit ids don't drift.
- Staleness is pure content-hash; no timestamps.
- The plan is the only nondeterministic input to the models, and it is produced
  mechanically. Same sources + same `rules/*` → empty plan → no model work.

## Tuning

Edit `config.json`: add source files to a layer, enable `codegen`, adjust
`unit_target_lines` / `unit_max_lines`, extend `id_domains`, or remap which
domains feed which `sections`. Re-run `chunk.py manifest` after any change.
