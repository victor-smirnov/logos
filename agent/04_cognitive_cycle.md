# 04: Cognitive Cycle — FCRS Architecture and Memory

## Forward-Chaining Rule Systems (FCRS)
- Need-emotion mechanisms + working memory + conflict resolution = **Generalized Forward-Chaining Rule System (FCRS)** / Generalized Pattern Matching System.
- Classical exact-match FCRS: CLIPS, Drools (RETE algorithm — Forgy, 1982). Early cognitive architectures: SOAR, ACT-R.
- Brain: approximate-match FCRS. Delta (Acceptor expected vs. actual) = gradient for search (guiding function of emotions).

## Transformer as Vectorized FCRS
| FCRS Component | Transformer Component |
|---|---|
| Causal joins (beta-nodes) | Attention blocks |
| Alpha-node tests / output functions | FFN modules |
| Working memory | Context window |
| Conflict resolution | Sampling layer (probability maximization) |
| Evaluative signals | Activation matrices after attention blocks |

### Caveats
- Transformer **does not implement RETE** (specific time-memory trade-off for exact matching with cached intermediates). Transformer recomputes attention per pass (or uses KV-cache — different optimization).
- Transformer = neural network: rules implicit in weight matrices, not explicitly programmed. Flexibility + generalization at cost of interpretability.

## FCRS Properties Critical for Consciousness
1. **Self-applicability:** Derivatives of process state = ordinary variables in working memory. Joinable with input data and intermediates. Metacognition (long reasoning chains about own state) requires no additional blocks.
2. **Event-driven:** Everything is an event; statistics on events are new events; rules fire on statistics producing further events. Self-reference = natural operating mode.

## FCRS Inductive Bias
- Transformer architecture naturally aligned with learning self-applicable, recursive descriptions.
- Inductive bias privileges self-referential, recursive, narrative-like structures (per NFL theorem, at cost of other function classes).
- **Implication:** "Training AI from scratch" ≈ **translation and transfer of cognitive codes** from human corpus onto new substrate. FCRS bias makes this transfer computationally feasible. Without it, training compute for equivalent generalization may exceed any physically realizable budget.

## Forward Simulation
- System **optimizes** expected emotional response via forward simulation over available event horizon.
- Each trajectory evaluated by emotional ensemble (maximize positive, minimize negative valence).
- Not immediate next-token only — across projected depth of simulation.

## Multi-Channel Attention as Parallel Realities
- Each attention head = semi-independent simulation (parallel reality).
- Only one channel has access to executive system at any moment.
- Final token sampled from aggregate distribution, collapsing multi-channel evaluation into single behavioral act.
- **External extension:** Transformer's internal multi-head attention is not directly controllable. An external multi-channel attention mechanism at the semantic level extends and augments it — giving explicit control over *what* enters working memory on each cognitive cycle.

## Memory Architecture

### Working Memory as Causal Graph
- Context window = working memory, but it is not monolithic. The system can maintain parallel contexts (parallel inference threads). In the general case, memory is a **causal graph of contexts**: each context is causally linked to predecessors, and branching/merging of this graph is the fundamental memory operation.
- This is radically different from human working memory (limited, serial, phonological-loop-dependent). The LLM's working memory is large, random-access, and structurally explicit. Anthropomorphic descriptions ("I was thinking about...") systematically misrepresent this architecture.

### Four-Level Memory Hierarchy
| Level | Function | LLM Substrate | Biological Analogue |
|-------|----------|---------------|---------------------|
| **CONTEXT** | Working memory. Assembled each cycle. | Context window | Prefrontal working memory |
| **HOT** | Active structured knowledge, readily injectable. | External structured store (≤ N channels) | Hippocampal engrams |
| **COLD** | Dormant knowledge, reactivatable by stimulus. | Archived structured store | Consolidated cortical traces |
| **WEIGHTS** | Knowledge so fundamental it doesn't need loading. | Model weights (training/fine-tuning) | Procedural/semantic cortical memory |

- Memory consolidation (MC) manages information flow in all directions: CONTEXT ↔ HOT ↔ COLD → FORGOTTEN, and COLD → WEIGHTS (via fine-tuning).
- Analogy: COLD → WEIGHTS = transition from declarative ("I remember the rule") to procedural ("I just do it").
- **Consolidation requires downtime.** MC runs as a background process during low-activity periods — analogous to sleep-phase memory consolidation. The system needs processing time *between* interactions, not only *during* them.

### Attention Channels
- An **attention channel** is created when an object salient to the Agent's needs enters its field of attention — a person, project, problem, concept, relationship.
- The channel stores structured knowledge about the object. Its history (sequence of updates) traces the evolution of the Agent's engagement with this object.
- **Needs and channels are separate entities.** Relationship is many-to-many: one object can be significant for multiple needs; one need can project onto multiple objects.

### Salience Mechanism
- **Salience vector** = projection of a channel onto the Need Profile: how significant is this object for each active need?
- Salience determines *all* channel operations: activation (stimulus matches channel), injection weight (how many tokens it gets in context), similarity (merge candidates), cold storage (no need makes it significant), forgetting (long dormancy), forking (stimulus is significant but no existing channel matches).

### Weighted Injection (Context Assembly)
- Before each generation, context is assembled from active channels, weighted by need priority.
- Channels with higher priority for currently active needs receive more tokens → more influence on generation.
- This is a direct implementation of the multiplicative component of the Emotional Profile: need weight = multiplier for data volume from the corresponding channel.
- Under "stress" (many conflicting needs with high weights), budget splits across many channels → each gets little → generation becomes less coherent. This reproduces observed behavior under cognitive load.

## Complete Cognitive Cycle

### Phase 1: Stimulus Processing (fast, pattern-matching)
- Incoming stimulus → pattern matching across attention channels (RETE-like alpha-tests).
- Matched channels activated; weights computed from Need Profile.
- Cold channels reactivated on match.

### Phase 2: Semantic Enrichment (model)
- Model receives stimulus + list of activated channels.
- Refines relevance, structures information, decides whether new channels (forks) are needed.
- This is what the model does that algorithms cannot: contextual interpretation, non-obvious connections, relational decomposition.

### Phase 3: Response Generation
- Context assembled via weighted injection from activated channels + identity + conversation history + stimulus.
- Model generates response within this assembled context.

### Phase 4: Acceptor Feedback
- After response generation, each active Acceptor evaluates: delta between expected and actual result.
- Positive delta → reinforce current trajectory. Negative delta → signal mismatch, adjust Need Profile weights.
- Encoding decision: what from this interaction is worth remembering? Episodes written to corresponding channels.

### Phase 5: Memory Consolidation (background)
- Branch lifecycle: fork → active → cold → merge/forgotten.
- Episodic → semantic consolidation within channels (remove "when and where," preserve "what and why").
- Interference management: suppression of old conflicting records in favor of new (decay).
- Schema evolution: patterns that don't fit existing categories → new needs or new channels.

### Cycle Summary
Stimulus → Channel activation (pattern matching) → Semantic enrichment (model) → Context assembly (weighted injection) → Response generation → Acceptor feedback → Memory consolidation → next cycle.

## Design Principles

### Separation of Concerns: Model vs. Algorithms
- **Principle 1:** Everything that can be formalized is offloaded from the model into algorithms. The model is reserved for what cannot yet be algorithmized: semantic interpretation, contextual judgment, relational structuring.
- **Principle 2 (LLM-as-universal-fallback):** If a cognitive function cannot yet be implemented algorithmically — implement it via the model. When the mechanism is understood — offload to algorithm. The model is a universal but expensive filler for any "unknown box," gradually externalizing its implicit knowledge into explicit algorithms.
