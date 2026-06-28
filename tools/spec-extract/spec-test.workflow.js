export const meta = {
  name: 'spec-test',
  description: 'Generate grouped conformance tests that confirm spec rules, self-verified against the compiler',
  whenToUse: 'After `chunk.py test-plan`, feed the batches here to write + verify tests/spec/*.logos',
  phases: [{ title: 'Author+verify', detail: 'one agent per test-unit: write grouped test, compile/run, iterate to green' }],
}

// args: {
//   units: [{ stem, kind: 'pass'|'fail', rules:[{id,domain,statement,examples,evidence,divergence}] }],
//   tests_dir, lib_dir, logosc, run_test, repo
// }
const P = (typeof args === 'string') ? JSON.parse(args) : args
if (!P || !Array.isArray(P.units)) throw new Error('spec-test: pass {units:[...]} as args')
const TESTS = P.tests_dir || 'tests/spec'
const LIBDIR = P.lib_dir
const LOGOSC = P.logosc || 'build/bin/logosc'
const RUNNER = P.run_test || 'tests/logos/run_test.sh'

const RET = {
  type: 'object', additionalProperties: false,
  required: ['stem', 'covered', 'files', 'green'],
  properties: {
    stem: { type: 'string' },
    green: { type: 'boolean' },          // every written test compiles+passes
    covered: { type: 'array', items: { type: 'string' } },     // rule ids a green test confirms
    untestable: {                          // rules deliberately not tested, with why
      type: 'array',
      items: { type: 'object', additionalProperties: false, required: ['id', 'reason', 'class'],
        properties: { id: { type: 'string' }, reason: { type: 'string' },
                      class: { type: 'string', enum: ['transitive', 'untestable'] } } },
    },
    files: { type: 'array', items: { type: 'string' } },
    note: { type: 'string' },
  },
}

function passPrompt(u) {
  return `Write ONE grouped conformance test that locks these Logos language rules by EXECUTING them. The test must actually compile and run green — a test that doesn't run locks nothing.

RULES to confirm (domain "${u.rules[0].domain}", group "${u.stem}"):
${JSON.stringify(u.rules.map(r => ({ id: r.id, statement: r.statement, examples: r.examples || [], evidence: r.evidence })), null, 1)}

WRITE ${TESTS}/pass/${u.stem}.logos :
- "package ${u.stem.replace(/[^a-z0-9_]/g, '_')};" then any helper structs/impls/fns, then "fn main() -> i64 { ... }".
- One assertion (or small block) PER rule that would only hold if the rule is true; on violation "return <distinct nonzero code>;". All pass ⇒ "return 0;".
- Immediately ABOVE each rule's assertion put a marker comment "// @rule <id>" (the coverage tool keys on it). One rule may need several lines; keep the marker adjacent.
- Use only what the rule needs. Pure-language rules need no imports; for Vec/Box/Rc/String/HashMap/etc add the right "use logos.mem.*;" / "use logos.std.*;". Keep it minimal and faithful — assert ONLY what the rule states, do not invent behavior.
Also WRITE ${TESTS}/pass/${u.stem}.expected containing exactly "exit: 0".

VERIFY (mandatory, iterate until green):
  LOGOS_LIB_DIR=${LIBDIR} bash ${RUNNER} pass ${LOGOSC} ${TESTS}/pass/${u.stem}.logos ${TESTS}/pass/${u.stem}.expected
Exit 0 = green. If it fails: read the error, fix the TEST (not by weakening an assertion away from the rule). If a specific rule genuinely cannot be expressed as a runnable assertion at language level (internal mono/codegen/layout invariant, or needs unavailable stdlib), REMOVE its assertion+marker and report it under "untestable" with class 'transitive' (exercised elsewhere) or 'untestable', and a one-line reason — never fake a passing assertion.

Return the structured result: stem, green (final run exit 0?), covered (rule ids with a live marker in the green file), untestable, files written. Terse; no prose to the user.`
}

function failPrompt(u) {
  return `Write conformance FAIL tests that lock these DIAGNOSTIC rules — each is a program that must be REJECTED by the compiler with a characteristic message. A fail test is one program per diagnostic (a compile error aborts the whole file, so they cannot be grouped into one compile).

RULES (each "X is rejected/an error"):
${JSON.stringify(u.rules.map(r => ({ id: r.id, statement: r.statement, evidence: r.evidence })), null, 1)}

For EACH rule write ${TESTS}/fail/${u.stem}__<short-slug>.logos :
- a minimal "package ...;" program that triggers exactly that error and nothing else;
- a "// @rule <id>" marker comment;
- a sibling .expected file containing a SHORT, stable substring of the compiler's stderr for that error (a few words; avoid line numbers / volatile text).

VERIFY each (iterate until green):
  LOGOS_LIB_DIR=${LIBDIR} bash ${RUNNER} fail ${LOGOSC} <file>.logos <file>.expected
Exit 0 = the program failed to compile AND stderr contained your substring. If the program compiles (no error) or the message differs, fix the program/substring. If the rule cannot be triggered in isolation, report it under "untestable" — do not fake it.

Return: stem, green, covered (rule ids with a passing fail-test), untestable, files. Terse.`
}

phase('Author+verify')
log(`generating ${P.units.length} test-unit(s)`)
const results = (await parallel(P.units.map(u => () =>
  agent(u.kind === 'fail' ? failPrompt(u) : passPrompt(u),
    { label: `${u.kind}:${u.stem}`, phase: 'Author+verify', schema: RET })
))).filter(Boolean)

return {
  units: results.length,
  green: results.filter(r => r.green).length,
  covered: results.reduce((n, r) => n + (r.covered || []).length, 0),
  untestable: results.flatMap(r => r.untestable || []),
}
