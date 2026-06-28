export const meta = {
  name: 'spec-dedup',
  description: 'Resolve spec rule-id collisions: per collision, decide merge vs split',
  whenToUse: 'After `chunk.py collisions --json`, feed the result here; then `chunk.py apply-dedup` the returned decisions.',
  phases: [{ title: 'Dedup', detail: 'one agent per colliding id -> merge or split decision' }],
}

// args, two ways:
//   inline:  { collisions: [{id, count, similarity, variants:[{file, rule}]}], schema_path }
//   by path: { collisions_path: "tools/spec-extract/.collisions.json", ids: ["a.b.c", ...] }
//            (each agent reads its own collision slice from the file — keeps args tiny)
const P = (typeof args === 'string') ? JSON.parse(args) : args
if (!P || (!Array.isArray(P.collisions) && !P.collisions_path)) {
  throw new Error('spec-dedup: pass {collisions:[...]} or {collisions_path, ids:[...]}')
}
const SCHEMA = P.schema_path || 'tools/spec-extract/rule.schema.json'
const byPath = !!P.collisions_path
const work = byPath ? (P.ids || []).map(id => ({ id })) : P.collisions

// Returned decision for one collision. final_rules each carry a target file
// (must be one of the variants' files) and a full, schema-valid rule object.
const RULE = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'domain', 'statement', 'evidence'],
  properties: {
    id: { type: 'string', pattern: '^[a-z][a-z0-9]*\\.[a-z0-9][a-z0-9-]*\\.[a-z0-9][a-z0-9-]*$' },
    domain: { type: 'string' },
    title: { type: 'string' },
    statement: { type: 'string' },
    evidence: { type: 'array', items: { type: 'string' }, minItems: 1 },
    examples: { type: 'array', items: { type: 'string' } },
    divergence: { type: 'string' },
    uncertainty: { type: 'string' },
    related: { type: 'array', items: { type: 'string' } },
  },
}
const DECISION = {
  type: 'object',
  additionalProperties: false,
  required: ['id', 'action', 'final_rules'],
  properties: {
    id: { type: 'string' },
    action: { type: 'string', enum: ['merge', 'split'] },
    rationale: { type: 'string' },
    final_rules: {
      type: 'array', minItems: 1,
      items: {
        type: 'object', additionalProperties: false, required: ['file', 'rule'],
        properties: { file: { type: 'string' }, rule: RULE },
      },
    },
  },
}

function prompt(c) {
  const inline = !byPath
  const files = inline ? [...new Set(c.variants.map(v => v.file))] : []
  const variantsBlock = inline
    ? `The ${c.count} colliding variants (each with the artifact FILE it lives in and its full rule JSON):\n${JSON.stringify(c.variants, null, 1)}`
    : `Read ${P.collisions_path}; find the object in its "collisions" array whose "id" == "${c.id}". Its "variants" array lists the colliding rules, each with the artifact FILE it lives in and its full rule JSON. Use exactly those.`
  return `A spec rule id must address exactly ONE rule, but the id "${c.id}" is currently carried by multiple different rules. Resolve the collision.

${variantsBlock}

DECIDE:
- "merge" — they are the SAME normative rule stated differently. Produce ONE final rule: keep the id "${c.id}", write the clearest/most-complete statement, UNION the evidence anchors (and examples), and place it in the single most authoritative variant file (grammar file for pure syntax, sema for typing/semantics, codegen for lowering, mono for monomorphization). Drop the rest.
- "split" — they are DISTINCT rules that merely collided on a generic slug. Produce ONE final rule PER distinct rule, each kept in ITS OWN variant file. Give each a precise, distinct id "<domain>.<group>.<slug>" reusing the same domain.group; at most ONE final rule may keep the exact original id "${c.id}" (the canonical meaning) — the others MUST get new, specific slugs. Do not invent generic slugs that could re-collide.

RULES:
- Each final_rule.file MUST be one of the colliding variants' existing files${inline ? `: ${JSON.stringify(files)}` : ' (as listed in the variants).'}.
- Each rule must conform to ${SCHEMA} (read it). Preserve real divergence tags; do not fabricate.
- If unsure whether two variants are the same rule, read their evidence source lines before deciding.
- Be conservative: prefer split when the statements describe genuinely different behavior.

Return the structured decision (its "id" must be "${c.id}"). Terse; no prose to the user.`
}

phase('Dedup')
log(`resolving ${work.length} colliding id(s)`)
const decisions = (await parallel(work.map(c => () =>
  agent(prompt(c), { label: c.id, phase: 'Dedup', schema: DECISION })
))).filter(Boolean)

return { decisions, resolved: decisions.length, of: work.length }
