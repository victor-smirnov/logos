export const meta = {
  name: 'spec-extract',
  description: 'Extract language-spec rules from compiler source units, then assemble prose spec',
  whenToUse: 'Run after `python tools/spec-extract/chunk.py plan` to extract spec rules from stale units and refresh docs/spec/*.md',
  phases: [
    { title: 'Extract', detail: 'one agent per stale work-unit -> rules/<unit>.json' },
    { title: 'Assemble', detail: 'one agent per affected spec section -> docs/spec/<section>.md' },
  ],
}

// args (the JSON from `chunk.py plan`, optionally trimmed). Two ways to feed units:
//   A) inline:    P.units = [{unit,layer,file,start,end,source,hash,rules_out,prior_ids,prior_domains}, ...]
//   B) by path:   P.plan_path = "tmp/plan.json", P.unit_ids = ["unit/a", ...]
//      (each agent reads its own metadata slice from the plan file — keeps args tiny
//       for big runs, since the workflow JS itself has no filesystem access)
// Always required: rules_dir, spec_dir, sections, id_domains, schema_path.
// Flags: P.assemble_only (skip extract), P.no_assemble (skip prose),
//        P.assemble_all (rebuild every section regardless of changed domains).
const P = (typeof args === 'string') ? JSON.parse(args) : args
if (!P || typeof P !== 'object') {
  throw new Error('spec-extract: pass the `chunk.py plan` JSON as args')
}

const RULES_DIR = P.rules_dir || 'tools/spec-extract/rules'
const SPEC_DIR = P.spec_dir || 'docs/spec'
const SCHEMA = P.schema_path || 'tools/spec-extract/rule.schema.json'
const DOMAINS = P.id_domains || []
const SECTIONS = P.sections || {}

let workUnits = []
if (!P.assemble_only) {
  if (P.plan_path) {
    workUnits = (P.unit_ids || []).map(id => ({ unit: id, plan_path: P.plan_path }))
  } else {
    workUnits = P.units || []
  }
}

const EXTRACT_RET = {
  type: 'object',
  additionalProperties: false,
  required: ['unit', 'rule_count', 'domains', 'ids'],
  properties: {
    unit: { type: 'string' },
    rule_count: { type: 'integer' },
    domains: { type: 'array', items: { type: 'string' } },
    ids: { type: 'array', items: { type: 'string' } },
    note: { type: 'string' },
  },
}

const ASSEMBLE_RET = {
  type: 'object',
  additionalProperties: false,
  required: ['section', 'rule_count', 'path'],
  properties: {
    section: { type: 'string' },
    rule_count: { type: 'integer' },
    path: { type: 'string' },
    note: { type: 'string' },
  },
}

const COMMON = `You are extracting the LANGUAGE SPECIFICATION from the Logos compiler. The compiler is the source of truth; recover the normative rules it enforces — what the LANGUAGE does, not how the C++/PEG is structured.

Identify every observable LANGUAGE rule in the slice: grammar productions, type/inference rules, coercions, trait resolution, borrow/move rules, monomorphization/substitution rules, diagnostics-as-constraints, intrinsic semantics, etc. A plumbing-only unit legitimately yields zero rules.

For each rule assign a STABLE human-readable id "<domain>.<group>.<slug>" (lowercase kebab); the first segment is the domain. Allowed domains: ${DOMAINS.join(', ')}.
RULE QUALITY (mandatory): statement = normative, implementation-agnostic, notation over prose, precise enough to test; evidence = >=1 "file#Lxxx" anchor INSIDE this unit per rule; divergence = Rust-divergence tag (docs/DIVERGENCES.md §A, e.g. A1/A2) or note, omit when Rust-conformant; uncertainty = flag inferred/ambiguous rules.

SCOPE DISCIPLINE (critical — your slice is PARTIAL; other parts of the compiler implement features you cannot see here):
- State only what THIS slice's code positively shows. Describe what it DOES.
- Do NOT make absolute/global NEGATIVE claims ("X is not supported", "not yet expressible", "only the bare form exists", "never allowed", "no X yet") UNLESS this slice itself contains the code that rejects/forbids the alternative. A feature absent from your slice is NOT evidence it is absent from the language — another unit likely implements it. When tempted to write a negative, either scope it ("this path handles only …") or omit it.
- A grammar token/regex shows the GRAMMAR's shape, not the full lexer; hand-rolled lexing/decoding elsewhere may accept more. Don't conclude the language rejects what the regex doesn't match.
- For concrete syntax in examples: only write an example whose syntax you can ground in THIS slice (or omit examples). Do not invent surface syntax (e.g. parameter-pack spelling) you haven't seen here.
Read the artifact schema at ${SCHEMA} and conform EXACTLY. Be terse and technical; do NOT write prose explanations to the user.`

function priorBlock(prior) {
  return (prior && prior.length)
    ? `PRIOR ids for this unit (REUSE the exact id for the same rule — ids are permanent addressing handles; renaming one is a breaking change):\n${prior.map(x => '  - ' + x).join('\n')}`
    : 'No rules were extracted from this unit before; assign fresh ids.'
}

function extractPromptInline(u) {
  return `${COMMON}

WORK UNIT: ${u.unit}   (layer: ${u.layer})
SOURCE:    ${u.source}

STEPS:
1. Read exactly ${u.file} lines ${u.start}-${u.end} (peek around only to understand a rule; do not extract rules belonging to another unit).
2. ${priorBlock(u.prior_ids)}
3. Write the artifact to ${u.rules_out}. Set unit="${u.unit}", layer="${u.layer}", source="${u.source}", source_hash="${u.hash}" verbatim.
Return the structured summary.`
}

function extractPromptByPath(u) {
  const codegenNote = u.unit.startsWith('codegen/')
    ? `\nCODEGEN LAYER: extract ONLY target-INDEPENDENT language semantics (literal decode such as raw-string r#"..."# hash counting, integer-coercion/widening lowering, DST/fat-pointer layout rules, drop/dispatch semantics). SKIP register allocation, ABI/calling-convention specifics, MLIR-op/dialect plumbing, and anything platform-dependent — those are not language rules.\n`
    : ''
  return `${COMMON}
${codegenNote}
WORK UNIT: ${u.unit}

STEPS:
1. Read the plan file ${u.plan_path}; find the object in its "units" array whose "unit" == "${u.unit}". It gives: file, start, end, source, hash, rules_out, prior_ids.
2. Read exactly that file's lines start-end (peek around only to understand a rule; do not extract rules belonging to another unit).
3. Reuse any prior_ids for the same rule; assign fresh ids otherwise (ids are permanent addressing handles — never rename).
4. Write the artifact to the unit's rules_out. Set unit, layer, source, source_hash=hash verbatim from the plan entry.
Return the structured summary.`
}

function assemblePrompt(section, domains) {
  if (section === 'divergences') {
    return `You are assembling the DIVERGENCES register of the Logos language specification — a cross-cutting view, NOT a single domain.

OUTPUT: ${SPEC_DIR}/divergences.md

STEPS:
1. Find every rule artifact:  find ${RULES_DIR} -name '*.json'.
2. Collect every rule (across ALL domains) whose "divergence" field is non-empty. The divergence text usually starts with a tag (e.g. A1, A10, G156-1) or says "Logos-specific".
3. Write ${SPEC_DIR}/divergences.md grouped by divergence tag/kind (one "##" per tag family: blessed §A tags like A1/A2…, baghunt ids like G156-1, and a "Logos-specific additions" group). Within each group, list each rule as  "### \`${'${id}'}\` — Title"  with its divergence note, one-line statement, and source evidence (file#line).
4. Cross-reference docs/DIVERGENCES.md: where a tag matches a §A blessed divergence there, say so; flag any divergence tag that does NOT appear in docs/DIVERGENCES.md as "unregistered — needs triage".
5. Preamble: one line stating this is the compiler-derived divergence register and how it relates to docs/DIVERGENCES.md.
Preserve every id exactly. Return the structured summary (section="divergences", rule_count, path).`
  }
  return `You are assembling one section of the Logos language specification from extracted rule artifacts.

SECTION:  ${section}   ->  ${SPEC_DIR}/${section}.md
DOMAINS:  ${domains.join(', ')}

STEPS:
1. Find every rule artifact:  find ${RULES_DIR} -name '*.json'. Each is a JSON object with a "rules" array.
2. Collect all rules whose "domain" is one of [${domains.join(', ')}]. Ignore other domains.
3. Write/refresh ${SPEC_DIR}/${section}.md as a clean spec section:
   - Group rules by their "group" (middle id segment) under "##" headings, ordered logically.
   - Emit each rule as  "### \`${'${id}'}\` — Title"  so the id is the permanent linkable address. Render statement, examples (fenced \`logos\`), divergence note, and source evidence (file#line).
   - Preserve EVERY id exactly; never invent, merge-away, or rename. If two rules conflict, surface both and flag it — do not silently pick one.
   - One-line preamble: scope + source layers.
Return the structured summary.`
}

// ── Phase 1: extract ───────────────────────────────────────────────────────
phase('Extract')
let summaries = []
if (workUnits.length) {
  const byPath = !!P.plan_path
  log(`extracting ${workUnits.length} unit(s)${byPath ? ' (metadata via plan file)' : ''}`)
  summaries = (await parallel(workUnits.map(u => () =>
    agent(byPath ? extractPromptByPath(u) : extractPromptInline(u),
      { label: u.unit, phase: 'Extract', schema: EXTRACT_RET })
  ))).filter(Boolean)
} else {
  log(P.assemble_only ? 'assemble-only: skipping extraction' : 'no units to extract')
}

// Domains touched this run: agent-reported ∪ each unit's prior_domains (so a
// section a unit previously fed is rebuilt even if the agent under-reports).
const changedDomains = new Set()
for (const s of summaries) for (const d of (s.domains || [])) changedDomains.add(d)
for (const u of workUnits) for (const d of (u.prior_domains || [])) changedDomains.add(d)

// ── Phase 2: assemble ──────────────────────────────────────────────────────
phase('Assemble')
let assembled = []
if (!P.no_assemble) {
  const buildAll = P.assemble_all || P.assemble_only
  const sectionsToBuild = Object.entries(SECTIONS).filter(([, doms]) =>
    buildAll || doms.some(d => changedDomains.has(d))
  )
  if (sectionsToBuild.length) {
    log(`assembling ${sectionsToBuild.length} section(s): ${sectionsToBuild.map(s => s[0]).join(', ')}`)
    assembled = (await parallel(sectionsToBuild.map(([sec, doms]) => () =>
      agent(assemblePrompt(sec, doms), { label: sec, phase: 'Assemble', schema: ASSEMBLE_RET })
    ))).filter(Boolean)
  } else {
    log('no spec sections affected this run')
  }
} else {
  log('no_assemble: extraction artifacts written, prose left untouched')
}

return {
  extracted: summaries.length,
  total_rules: summaries.reduce((n, s) => n + (s.rule_count || 0), 0),
  changed_domains: [...changedDomains],
  sections_written: assembled.map(a => a.section),
}
