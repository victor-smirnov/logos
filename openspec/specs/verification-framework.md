# Verification Framework Specification

The Logos Verification Framework is a runtime observability and self-checking infrastructure designed for AI-driven development. It enables an optimistic implementation workflow: generate code, run it, let runtime diagnostics catch errors, feed structured feedback back to the model for correction.

## 1. Design Principles

### 1.1 Optimistic Convergence

The model converges to correct implementation in N iterations. The framework minimizes N by:
- Maximizing diagnostic information quality (root cause, not just symptom)
- Linking every runtime failure to spec requirements (traceability)
- Structuring feedback for LLM consumption (queryable, hierarchical, within context window)

### 1.2 Gradual Human Involvement

Human effort is proportional to the number of *non-converging* issues, not the total number of requirements. The system auto-escalates when it detects non-convergence.

### 1.3 Self-Checking Code

Implementation embeds its own verification. Invariants from the spec become runtime assertions. Correctness is continuously checked during execution, not only in separate test runs.

## 2. Components

### 2.1 LOGOS_ASSERT

Structured assertion macro with spec traceability.

**Signature:**
```cpp
LOGOS_ASSERT(condition, requirement_id, format_string, args...)
```

**Behavior on failure:**
1. Captures: requirement ID, condition text, formatted message, source location, timestamp, thread/fiber ID
2. Captures call chain from ring buffer (see §2.3)
3. Writes structured record to:
   - stderr (human-readable summary)
   - SQLite trace database (machine-queryable)
4. Aborts (debug builds) or throws (release builds with recovery)

**Example:**
```cpp
LOGOS_ASSERT(
    PopCnt(header_ & BITMAP_MASK) == size(),
    "INV-TINYMAP-002",
    "PopCnt({:#018x} & BITMAP_MASK) = {} != size() = {}. After put(key={}).",
    header_, PopCnt(header_ & BITMAP_MASK), size(), key);
```

### 2.2 LOGOS_TRACE

Checkpoint instrumentation for recording state at key transitions.

**Signature:**
```cpp
LOGOS_TRACE(tag, key1, value1, key2, value2, ...)
```

**Behavior:**
1. Serializes key-value pairs to a structured record
2. Adds: timestamp, thread/fiber ID, source location
3. Writes to SQLite trace database
4. No-op when tracing is disabled for this tag (runtime configuration)

**Selectivity:**
- Tags are hierarchical (e.g., `"hermes.tinymap.put"`)
- Tracing enabled/disabled per tag prefix at runtime
- Default: all tracing off in release, configurable subset in debug

**Example:**
```cpp
LOGOS_TRACE("hermes.tinymap.put",
    "key", key, "size_before", size(), "header_before", header_);
```

### 2.3 Call Chain Capture

Uses `-finstrument-functions` (Clang/GCC built-in) for automatic function entry/exit tracking.

**Runtime library (~100 lines):**
- Thread-local ring buffer (configurable size, default 256 entries)
- Each entry: function address + call site address + timestamp
- On LOGOS_ASSERT failure: dump ring buffer contents with symbol resolution (via `dladdr` or debug info)
- Functions annotated `__attribute__((no_instrument_function))` are excluded (hot paths)

### 2.4 Trace Database (SQLite)

**Schema:**

```sql
CREATE TABLE assertions (
    id INTEGER PRIMARY KEY,
    timestamp_ns INTEGER NOT NULL,
    thread_id INTEGER,
    fiber_id INTEGER,
    requirement_id TEXT NOT NULL,
    condition TEXT,
    message TEXT,
    source_file TEXT,
    source_line INTEGER,
    call_chain TEXT  -- JSON array of {function, callsite} pairs
);

CREATE TABLE traces (
    id INTEGER PRIMARY KEY,
    timestamp_ns INTEGER NOT NULL,
    thread_id INTEGER,
    fiber_id INTEGER,
    tag TEXT NOT NULL,
    source_file TEXT,
    source_line INTEGER,
    data TEXT NOT NULL  -- JSON object of key-value pairs
);

CREATE INDEX idx_traces_tag ON traces(tag);
CREATE INDEX idx_assertions_req ON assertions(requirement_id);
```

### 2.5 Trace Summarizer

A tool (initially a Python script, ~200 lines) that reads the SQLite database and produces hierarchical summaries for model consumption:

**Level 0 (overview):**
```
Assertion failures: 3
  INV-TINYMAP-002: 2 occurrences
  INV-ERELPTR-001: 1 occurrence
Total traces: 1,247
Components exercised: hermes.tinymap (834), hermes.arena (413)
```

**Level 1 (per-requirement):**
```
INV-TINYMAP-002 (2 failures):
  #1: PopCnt(0x0C00000000000005 & BITMAP_MASK) = 2 != size() = 3
      After put(key=5). At tiny_map.hpp:142
      Call chain: main → exerciser_tinymap → TinyObjectMap::put
  #2: PopCnt(0x1800000000000009 & BITMAP_MASK) = 2 != size() = 4
      After put(key=12). At tiny_map.hpp:142
      Call chain: main → exerciser_tinymap → TinyObjectMap::put
```

**Level 2 (full detail):** Raw SQL query results, trace entries around the failure point.

The model starts at Level 0, drills into Level 1 for failing requirements, and Level 2 only if needed.

## 3. Exerciser Framework

### 3.1 Structure

An exerciser is a program that uses a library component through its public API, performing realistic operations. It does NOT contain expected output values. Correctness is verified by internal assertions and structural checks.

```cpp
int main() {
    logos::TraceConfig::enable("hermes.*");
    
    auto ctr = HermesCtr::make_new();
    
    // Scenario: build a complex document
    auto map = ctr.make_tiny_map();
    map.put(1, ctr.make(42));
    map.put(3, ctr.make("hello"));
    map.put(7, ctr.make(true));
    ctr.set_root(map);
    
    // Structural integrity
    ctr.check();
    
    // Serialize round-trip
    auto text = ctr.to_string();
    auto parsed = HermesCtr::parse_document(text);
    parsed.check();
    
    // Compactify round-trip
    auto compact = ctr.compactify();
    compact.check();
    
    // Binary round-trip
    auto binary = compact.span();
    auto from_binary = HermesCtr::from_span(binary);
    from_binary.check();
    
    // Semantic equality
    LOGOS_ASSERT(
        compact.to_string() == from_binary.to_string(),
        "SCEN-ROUNDTRIP-001",
        "Round-trip text mismatch:\n  original: {}\n  restored: {}",
        compact.to_string(), from_binary.to_string());
    
    return 0;
}
```

### 3.2 Fuzz Exerciser

Randomized variant:

```cpp
void fuzz_tinymap(uint64_t seed, size_t iterations) {
    Rng rng(seed);
    auto ctr = HermesCtr::make_new();
    auto map = ctr.make_tiny_map();
    ctr.set_root(map);
    
    for (size_t i = 0; i < iterations; i++) {
        uint8_t key = rng.uniform(0, 51);
        auto value = random_hermes_value(ctr, rng);
        
        if (rng.coin()) {
            map.put(key, value);
        } else {
            map.remove(key);
        }
        
        // Invariants checked inside put/remove via LOGOS_ASSERT
    }
    
    ctr.check();  // full structural validation
    
    // Round-trip
    auto text = ctr.to_string();
    auto parsed = HermesCtr::parse_document(text);
    parsed.check();
}
```

## 4. Spec Integration

### 4.1 Invariant Format in Specs

Each component spec (e.g., `hermes-abi.json`) includes an `invariants` section:

```json
{
  "invariants": [
    {
      "id": "INV-TINYMAP-001",
      "scope": "TinyObjectMap",
      "condition": "size <= capacity <= 52",
      "when": "after any mutation",
      "severity": "fatal"
    },
    {
      "id": "INV-TINYMAP-002",
      "scope": "TinyObjectMap",
      "condition": "PopCnt(header & BITMAP_MASK) == size",
      "when": "after any mutation",
      "severity": "fatal"
    }
  ]
}
```

### 4.2 Coverage Verification

A simple script checks that every invariant ID from the spec appears in at least one LOGOS_ASSERT in the codebase:

```bash
# Extract IDs from spec
spec_ids=$(jq -r '.invariants[].id' hermes-abi.json)

# Find IDs in source
for id in $spec_ids; do
    if ! rg -q "$id" src/; then
        echo "MISSING: $id"
    fi
done
```

No requirement ID missing = full assertion coverage.

## 5. Future Evolution (Platform Integration)

This framework is Phase 1A infrastructure. In later phases it evolves:

- **Phase 2 (Logos compiler):** `LOGOS_ASSERT` and `LOGOS_TRACE` become language-level constructs. The compiler auto-instruments code where dependent types cannot prove properties statically — the residual between static proof and dynamic check.
- **Phase 2+ (HOCP):** Traces become Hermes documents in Memoria containers. Programs observe their own execution traces via HermesPath/Datalog queries. Runtime metrics trigger RETE rules.
- **Tooling:** Trace summarizer evolves into an interactive tool the model queries through MCP or similar protocol.
