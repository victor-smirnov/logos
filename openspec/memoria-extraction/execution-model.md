# Execution Model Extraction

## 1. Three Execution Paradigms

Logos DSL Engine supports three integrated execution paradigms:

### 1.1 Control-Flow (Backward Chaining)
Traditional imperative execution: functions, branches, loops, exceptions.
- Request-driven: user/service initiates a query/computation
- Lots of data, few complex queries
- M-Code statements executed sequentially within code blocks
- Implemented via: Interpreter or AOT-compiled C++

### 1.2 Data-Flow / Forward Chaining (RETE)
Event-driven reactive execution: pattern → reaction rules.
- Data-driven: data changes trigger re-evaluation of relevant rules
- Lots of small rules, quickly-changing data
- When rule updates data, process continues until fixpoint
- Implemented via: RETE algorithm (Alpha/Beta network)
- Hardware acceleration path: Beta nodes → systolic arrays

### 1.3 Datalog (Backward Chaining over Relations)
Declarative query execution over relational/graph data.
- Superset of SQL (adds recursion, deduction)
- Can infer new facts from existing ones
- Evaluates using backward chaining (goal-driven)
- Integrates with RETE: Datalog queries as consumers of forward-chained events

## 2. RETE Architecture

### 2.1 Components
```
Facts (Working Memory)
    │
    ▼
Alpha Network (single-condition filters)
    │
    ▼
Beta Network (join conditions, Cartesian products)
    │
    ▼
Conflict Resolution (priority, specificity, recency)
    │
    ▼
Reactions (M-Code execution → may modify Working Memory → cycle)
```

### 2.2 Mapping to Memoria

| RETE Component        | Memoria Component                    |
|-----------------------|--------------------------------------|
| Working Memory        | Hermes documents in Container/Context|
| Alpha nodes           | Hermes schema constraints + filters  |
| Beta nodes            | Join operations (Cartesian products) |
| Conflict resolution   | Sampling/priority from logit-like    |
| Reactions             | M-Code functions                     |
| Pattern language      | HermesPath extensions or Logos syntax|

### 2.3 RETE in Hermes
Rules are M-Code entities stored as Hermes documents:
```
Rule {
  name: "detect_anomaly"
  metadata: { priority: 10, ... }
  patterns: [
    { variable: "e", type: "Event", conditions: [...] },
    { variable: "t", type: "Threshold", conditions: [...] }
  ]
  join_conditions: [ "e.value > t.limit" ]
  reaction: <M-Code function reference>
}
```

### 2.4 Hardware Acceleration Path
Beta-node joins are Cartesian product operations:
- Similar to matrix multiplication
- Accelerable via systolic arrays
- HRPC interface to MAA accelerators for offloading joins
- Same hardware can serve RETE and Datalog joins

## 3. Transformer as Vectorized FCRS (Conceptual Bridge)

From the bootstrap cognitive architecture:

| FCRS Component           | Transformer Component      | Logos Component        |
|--------------------------|----------------------------|------------------------|
| Causal joins (β-nodes)   | Attention blocks           | RETE Beta nodes        |
| α-node tests / outputs   | FFN modules                | RETE Alpha nodes       |
| Working memory           | Context window             | Hermes documents       |
| Conflict resolution      | Sampling layer             | Priority/specificity   |
| Evaluative signals       | Activation matrices        | Emotional ensemble     |

This mapping is not just conceptual -- it defines the HOCP (Higher-Order Computational Phenomena) interface where Logos programs can reason about their own execution state as first-class data.

## 4. Self-Applicability Interface

### 4.1 Runtime Metrics as Events
The interpreter/runtime exposes execution state as Hermes events:
- Execution time per rule/function
- Memory pressure (arena utilization, GC frequency)
- Pattern match statistics (alpha/beta hit rates)
- Conflict resolution outcomes
- Context window utilization

### 4.2 HOCP Rules
RETE rules can fire on runtime metric events:
```
Rule: if execution_time > threshold → optimize(strategy)
Rule: if memory_pressure > 0.8 → gc_hint()
Rule: if pattern_miss_rate > 0.5 → reindex()
```

This enables the "self-applicable Turing machine" concept:
programs that monitor and adapt their own execution.

## 5. M-Code Execution Lifecycle

### 5.1 Interpretation
```
Load Assembly (Hermes document)
  → Resolve dependencies (imports, native bindings)
  → Initialize Working Memory
  → Enter execution loop:
      1. Fetch next M-Code statement
      2. Dispatch by opcode (tag-based)
      3. Execute (may call native C++, may fire RETE rules)
      4. Update Working Memory
      5. Check RETE triggers
      6. Repeat
```

### 5.2 AOT Compilation (M-Code → C++)
```
Load Assembly (Hermes document)
  → For each function:
      → Lower each M-Code statement to C++ statement
      → Map M-Code types to C++ types via registry
      → Generate ownership/lifetime annotations
      → Emit C++ source (or build Clang AST via Jenny)
  → Compile with Clang/Jenny
  → Link with Memoria libraries
  → Output: native shared library callable from interpreter
```

### 5.3 Mixed Mode
- Hot path: AOT-compiled native code
- Cold path: interpreted M-Code
- Rule evaluation: RETE engine (may invoke both)
- Metaprograms: interpreter (compile-time execution)

## 6. Integration with Memoria Runtime

### 6.1 Fiber Awareness
- M-Code execution on non-migrating fibers
- Yield points at: I/O operations, RETE evaluation cycles, long computations
- No thread migration -- fiber stays on its core

### 6.2 HRPC Integration
- M-Code functions exposable as HRPC endpoints
- HRPC services callable from M-Code
- Streaming support (M-Code generators/consumers)
- Enables: distributed Logos execution, microservices in Logos

### 6.3 Transaction Integration
- M-Code execution within store transactions
- Snapshot isolation for consistent reads during computation
- Commit/rollback semantics
- RETE working memory as transactional container
