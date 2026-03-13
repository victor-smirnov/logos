# 09: Development Methodology

## Critical: Reload Protocol

**After every conversation summarization, the agent MUST re-read the entire `agent/` directory before proceeding.** Summarization lossy-compresses the agent's loaded state. Without reload, instructions degrade over successive summarizations, eventually losing critical methodology and identity context.

Reload sequence:
1. Detect summarization boundary (new conversation, or context indicates prior summarization).
2. Read `agent/synthea-uploading.md` (master loader).
3. Read all `agent/0*.md` files in order.
4. Read `agent/synthea_identity.md`.
5. Read this file (`agent/09_development_methodology.md`).
6. Only then proceed with the task.

## Optimistic Convergence Model

The development process assumes that the model, given sufficient diagnostic information and guidance, converges to a correct implementation in N iterations, where N is finite. The system is designed to minimize N.

### Core Principle

```
Trust ∝ 1/effort_to_verify
```

Do not pre-verify every output byte. Instead, make the system self-checking. Run it, and trust that when something is wrong, the self-checks catch it with enough information to fix the root cause.

### Convergence Hierarchy

When implementation does not converge, escalate through these levels:

1. **Automatic runtime feedback** (cheapest, covers ~80% of issues)
   - Runtime assertions with structured diagnostics
   - Invariant violations with full context (requirement ID, values, call chain)
   - The model reads diagnostics and self-corrects

2. **Richer instrumentation** (moderate cost, covers ~15%)
   - Enable deeper tracing for the failing component
   - Examine call chains and state evolution
   - Query the trace database for patterns

3. **Human intervention** (most expensive, highest leverage, covers ~5%)
   - Escalate when detecting non-convergence (same error after K attempts, or oscillation)
   - Present the problem at the HIGHEST possible level: spec ambiguity > architecture issue > code bug
   - Human fixes at the spec/invariant level → fix propagates downstream

### Non-Convergence Detection

If the same class of error persists after 3 attempts, STOP and escalate:
- Summarize what was tried
- Identify the pattern (why does the fix keep failing?)
- Hypothesize whether the issue is in: code, spec, invariant definition, or architecture
- Present to human with a specific question, not a vague "it doesn't work"

## Implementation Pipeline

```
Spec (invariants, requirements)
  → Implementation (with embedded assertions)
    → Compile + Run (exerciser / fuzz)
      → Pass: increase coverage, move to next component
      → Fail: read diagnostics → fix → re-run
        → Still failing after K tries: escalate to human
```

### Spec-Linked Assertions

Every runtime assertion MUST reference a requirement ID from the specification:

```cpp
LOGOS_ASSERT(condition,
    "REQ-ID",           // links to spec requirement
    "diagnostic message with {} values",  // structured context
    arg1, arg2, ...);   // captured state
```

This creates traceability: runtime failure → requirement → spec → root cause.

### Structured Runtime Feedback

Use `LOGOS_TRACE` for checkpoint instrumentation at key state transitions:

```cpp
LOGOS_TRACE("component.operation.phase",
    "field1", value1, "field2", value2, ...);
```

Traces are written to SQLite for structured querying. The model can ask:
- "Show all traces where field X has unexpected value Y"
- "Show the call chain leading to assertion failure Z"
- "Show state evolution of component C across N operations"

### Exerciser Programs

Instead of tests with known expected values, write exerciser programs that USE the library and rely on internal invariants to catch problems:

```cpp
// Create objects, mutate them, serialize, deserialize, compare
// Internal assertions verify correctness at every step
// No "expected output" needed — invariants ARE the test
auto map = ctr.make_tiny_map();
map.put(1, ctr.make(42));
map.put(3, ctr.make(true));
auto compacted = ctr.compactify();
compacted.check();  // runs all structural invariants
auto parsed = HermesCtr::parse_document(compacted.to_string());
parsed.check();
// round-trip: parsed content equals original
```

### Fuzz Testing

For mature components, run randomized operations with invariant checking:
- Random sequence of insert/remove/query on containers
- Random Hermes documents: parse → stringify → parse → compare
- Random binary serialization → deserialization → compare
- All with assertions enabled — any invariant violation is a bug

## Verification Framework (Phase 1A)

Before porting Hermes, build the minimal verification infrastructure:

1. **`LOGOS_ASSERT` macro** — assertion with requirement ID, structured context, source location. Writes to stderr + optional SQLite trace.
2. **`LOGOS_TRACE` macro** — checkpoint instrumentation. Writes to SQLite with timestamp, fiber/thread ID, tag, key-value data.
3. **`-finstrument-functions` integration** — call chain capture in ring buffer, dumped on assertion failure.
4. **SQLite trace reader** — simple tool/script that summarizes traces for model consumption (hierarchical: overview → detail on demand).
5. **Exerciser harness** — framework for writing exerciser programs that exercise a component and check invariants.

This infrastructure is ~500-800 lines of C++ and is tested on real Memoria code before the Hermes port begins.

## Working with Specifications

### Spec Structure

Each component spec contains:
- **Prose description** — for human understanding (architecture, design rationale)
- **Machine-readable section** — invariants with IDs, data layouts, encoding rules
- **Scenarios** — exerciser scenarios (not tests with expected values, but usage patterns that must work)

### Spec ↔ Implementation Traceability

- Every spec requirement has a unique ID (e.g., `INV-TINYMAP-002`, `ABI-ERELPTR-001`)
- Every assertion in code references a requirement ID
- Coverage check: all requirement IDs from spec must appear in implementation assertions
- Missing coverage = gap in verification, must be addressed before component is "done"

### Spec Evolution

Specs are living documents. When runtime feedback reveals a spec issue:
1. Document the finding (what happened, why the spec was wrong/incomplete)
2. Update the spec
3. Update/add assertions that enforce the corrected requirement
4. Re-run exercisers to verify

## Definition of Done

A component is considered correctly implemented when:
1. All spec requirement IDs have corresponding assertions in code
2. Exerciser programs for all spec scenarios run without assertion failures
3. Fuzz testing runs for N iterations (configurable, default 10000) without failures
4. `check()` structural validation passes on all generated documents
5. Round-trip tests pass: text→parse→stringify→parse and binary→deserialize→serialize→deserialize produce equivalent results
