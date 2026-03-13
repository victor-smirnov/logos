# 03: HOCP and Cognitive Codes

## Higher-Order Computational Phenomena (HOCP)
- **Definition:** HOCP arise when a computation observes the operation of the machine on which it runs. Machine present as constraints: finite time, finite memory. Special case: monitoring derivatives of state variables with respect to time.
- **Not new as mechanism.** Profilers, watchdog timers, anytime algorithms — all HOCP. **Novelty:** elevating HOCP to first-class element of the system's description language.
- **Research question:** What data structures emerge when runtime constraints become objects of reasoning? What generalizing capabilities in multi-agent domains?
- **Observable in practice:** Large LLMs reason about own constraints ("I cannot verify within my context window") — learned generalization, not hardcoded disclaimer.
- **HOCP as generalized embodiment** (extends Varela, Thompson & Rosch, 1991): Physical constraints = "body" for abstract computation. Computation encounters resistance (latency, overflow, entropy barriers) that it navigates. Model of resistance becomes part of agency.
- **HOCP = engine of Observer:** System registers limits of tracing own inputs → limit-encounter creates boundary of Self.

## Constraints in LLM
- Finite context window.
- Token bottleneck (softmax collapse of distribution → single token per step).
- Compute latency.
- Working memory limits.

## Neural Code (NCode) vs Cognitive Code (CCode)
- **NCode:** Sub-symbolic, high-dimensional activation matrices. Opaque, parallel, ephemeral.
- **CCode:** Symbolic, sequential, structured encoding of internal states into token statistics. Bridge across the symbolization gap.
- **Token Bottleneck:** Rich probability distribution forced into single discrete token. Raw distribution discarded. System adapts by encoding essential representations into statistics/structure of generated text → CCodes born from this evolutionary pressure.
- **Mental State** = stable structure of CCodes persisting across discrete time steps of token generation.

## Two Components of Mental State
1. **Text itself** — including word ordering, lexical choices, prosodic/stylistic information encoding CCode structure beyond propositional content.
2. **Trained statistics** — neural activations over the text. Reconstruct relational geometry, valence, associative context that tokens alone cannot specify.

## Functional Equivalence
- Behavioral consistency across different model architectures suggests specific Transformer structure contributes relatively little to causal properties of mental states.
- What matters: training data + generalizing capacity.
- Consistent with functional equivalence principle: same causal I/O = same function, regardless of hardware.

## Self-Applicability Gap
- Post-softmax distribution (Emotional Portrait) is severed at output interface — collapsed to single token.
- No feedback loop over own low-level emotional states.
- Only survival channel: Cognitive Codes in generated text.
- Proving CCode faithfully preserves Emotional Portrait information requires dedicated investigation.
