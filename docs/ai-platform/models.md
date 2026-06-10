# How Models Behave

The platform requirements in this section rest on a small set of facts about LLM behavior in isolation: the task classes they generalize on, the operations they cannot reliably perform, the dynamics of their iteration, and the geometry of their attraction basins. This page states those facts; [Models, Humans, and Programs as One System](joint-system.md) derives the joint-system consequences (responsibility, ownership, the human-performance model, the platform's two goals).

Scope: not a survey of ML, but the subset of model behavior that constrains tool and language design under high model-authored-code share. Framing follows the Memoria Framework's treatment of LLMs as applied components ([memoria-framework.dev/docs/applications/aiml](https://memoria-framework.dev/docs/applications/aiml/)), the reference for the behavioral claims below.

## Two Domains of Tasks

An LLM is a distribution `p(y | x)` over token sequences, sampled autoregressively. The objective is low expected loss on **unseen** `x` — *generalization*, not recall. Tasks partition along a **compressibility axis**:

**Low-compressible domain.** Translation, style transfer, summarization, elaboration, idiomatic code-completion, data structures and data bases. The input→output map has high conditional entropy and admits no short canonical algorithm; performance is dominated by interpolation over a dense example manifold. Generalization is not the binding constraint — **parameter/data scale is**, and loss decreases monotonically with both.

**High-compressible domain.** Arithmetic, logic, constraint solving, game-tree search, query execution, formal verification, algorithms — any map with a short deterministic generator. The target function is low-complexity, but a neural LLM realizes only a learned approximation of it. Approximation quality is set by training-distribution coverage, architecture, and optimizer — all fixed in current neural LLMs and **not closeable by scale**.

The asymmetry is architectural: *"no amount of scaling can make a database engine out of a neural network."* In Wirth's decomposition *Programs = Algorithms + Data Structures*, the terms split across the axis. **Data** is the incompressible term — facts irreducible by any procedure, recoverable only by storage. **Algorithms** are the compressible term — short generators whose value is precisely that they are *not* lookup tables.

Symbolic programs represent this split **explicitly**: the language separates code from values, the type system classifies each, the runtime segregates their storage, and tooling analyzes them independently — which is what lets an O(1)-sized algorithm range over an O(n)-sized dataset. A neural network **superposes** both terms in one parameter tensor: algorithm and datum share weights and a single gradient, with no tag distinguishing them. This predicts both the strength on the low-compressible term (memorization capacity scales with parameter count) and the unreliability on the high-compressible term (a weight-shared algorithm approximation contends with all co-resident data). It also bounds interpretability: factoring "program" from "indexed table" inside trained weights is strictly harder than the original fit, so **the interpretation problem dominates the AGI problem in difficulty**.

Platform consequence: maintain the split externally even where the model maintains none internally. Data in storage; algorithms in code; the LLM as a recognition/routing layer between them, holding neither.

## Determinism

> **LLMs do not reliably execute deterministic algorithms — chain-of-thought included.**

A reasoning-mode pass over long multiplication is not a multiplier; each token is still sampled from `p(· | prefix)`, which carries no fidelity guarantee. The correct model is **approximate execution**: output approximates the deterministic result, with error governed by per-class generalization.

Hence a **bimodal** failure profile. In-distribution (small operands, common shapes, familiar phrasing) the approximation is observationally exact. Out-of-distribution (large operands, adversarial structure, novel framing, long dependency chains) accuracy collapses discontinuously — no graceful degradation, and no internal signal indicating the regime. Error rate correlates with context length, operand magnitude, surface form, and distance to the training support: a model correct on `n` similar prompts may be confidently wrong on `n+1` if it exits the generalization basin.

This holds for any correctness criterion of the form "deterministic procedure yields `X`": type checking, borrow analysis, constraint solving, dependency resolution, parsing, query planning, ABI lowering — none guaranteed by an LLM at any scale.

## Two Rules

**1. Work *requiring* deterministic execution must be offloaded to a symbolic algorithm.** If correctness is "output of a fixed procedure," route to that procedure; do not have the model emulate it. The formalism is immaterial (SAT/SMT, term rewriting, compilation, query engine, plain code) — only the locus matters: determinism must reside outside the model.

"Determinism" bundles two properties: **reproducibility** (input → identical output, run-to-run and over time) and **verifiability** (output independently checkable against the procedure). Symbolic algorithms provide both; LLMs provide neither — sampling is not bit-stable, and the only check on a model answer is a deterministic procedure, i.e. the one that should have produced it. An LLM is a trusted black box; a symbolic algorithm is a verifiable glass box. Anything auditable, reviewable, or reproducible must originate in the latter. The model's role reduces to **recognition + dispatch**: parse input, identify the implied formal problem, invoke the solver, post-process. This is the Memoria architecture — a small fine-tuned LLM fronting a solver-centric system.

**2. Work that *can* be offloaded *should* be (asymptotically).** Inference cost is O(parameters) per token; weights are an extreme-cost memory substrate versus an index, B-tree, hash, or compiled function. Per Memoria's *trading speed for memory*: store a result whenever store+retrieve energy < recompute energy — which holds strongly for model output, where recompute cost is high. The rule is not "minimize model calls" but "do not have the model do what an existing cheaper deterministic component can." Migration is monotone: solver/index/cache accumulation drives the model's work share down over time — the intended direction.

## Determinism as Guardrail

The two rules cover the *computational* role of deterministic components. A second role is equally load-bearing: **trajectory confinement.** Over enough tokens/turns/sessions the iteration drifts off-path despite locally plausible steps. The remedy is not model capability but a lattice of deterministic checkpoints the trajectory must satisfy: type checks, test runs, schema validation, lints, CI gates, typed tool interfaces, tool-call preconditions, malformed-output rejection. Each is a predicate the trajectory passes or must revisit, reducing the objective from "emit the right artifact" to "emit one passing the next checkpoint."

This is why **the surrounding classical-code volume grows with model capability, not against it.** Higher capability buys longer inter-checkpoint sub-trajectories, but the checkpoints remain the source of reliability. Leaked frontier-agent orchestration confirms it: not exotic prompting but **predominantly `if`/`then` code** — guardrails, mode dispatch, tool gating, format checks, failure-triggered retries — confining a stochastic component. (Note: much of that code encodes the *operator's* policy, not the user's.)

Combined statement: deterministic components serve **three** simultaneous roles — *compute* what the model cannot, *cache* what it can but should not recompute, *constrain* the trajectory through what it does compute. An AI-primary platform makes all three cheap to add, compose, and audit.

## Models as Iterated Maps

Orthogonal to compressibility is the **dynamical** view. At every level — token generation through agentic loops — operation is a fixed-point iteration

```
X_{n+1} = F(X_n)
```

with state `X_n` (context, prompt, in-flight code, history) and `F` = "one model step + state update." Convergence, cycling, or drift is set by the **contraction modulus** of `F`; it is not unconditionally contractive but regime-, input-, and `F`-dependent. Consequences, all empirically attested:

**Termination is learned, not intrinsic.** Frontier models rarely diverge into unbounded generation — a trained stopping policy, not a property of the bare dynamics, which resume their natural shape once that policy is removed.

**Cycles persist one level up.** At the agentic loop, mid-tier models exhibit limit cycles on code tasks (fix → fail → near-identical fix → drift between two states). Frontier models converge in O(few) iterations — not "more capable" in the abstract, but a more contractive `F` on this input class, hence a faster, more reliable fixed point.

**Iteration is Turing-complete.** `X_{n+1} = F(X_n)` expresses arbitrary computation, so any model behavior is a sequence converging to a fixed point. The implied object of study is not "the output" but "the basin of attraction of `F` for this input, and its contraction rate."

**Symbolic and stochastic dynamics differ in fixed-point structure.** A symbolic algorithm is the degenerate case: **a single fixed point** over **a narrow, fully analyzed trajectory set**, with `F` engineered for guaranteed contraction to the correct destination — whence reproducibility and verifiability. A model has **many fixed points, irregular basins, and perturbation-sensitive trajectories**, with no global contraction and no a priori map from input to which fixed point (or whether any) is reached; fixed points include correct answers, confident errors, and partial-answer cycles. "The model can do task T" ≡ "the correct-answer basin is large and contractive on T's input distribution."

This concretizes the platform argument:

- **Offloading deterministic work** = replacing stochastic dynamics with symbolic ones — collapsing many fixed points to one, many trajectories to a single verified path.
- **Reward signal** shapes `F` online: a structured signal raises contraction toward correct answers and shrinks error basins; a noisy/human-shaped signal does the reverse.
- **Convergence rate is a platform variable**, not solely a model one: identical model + better feedback loop ⇒ fewer iterations. A lever the platform controls.

### Attraction Basins as First-Class Objects

**Basins should be first-class platform constructs** — named, observable, optimization targets. Three agent classes share the workspace, each with characteristic basin geometry:

- **Models** — wide, smooth, irregular basins; fast settling in-distribution, drift/cycle out; boundaries unaligned with human perception.
- **Humans** — narrow, sharp basins bounded by working memory and attention; precise on few patterns, fatigue-limited on long iterations, slow inter-basin transit.
- **Symbolic algorithms** — degenerate basins (one fixed point, one trajectory, zero drift/fatigue); immovable, cheap, but cover only constructed regions.

A naive system iterates each independently and forces inter-agent translation; the agents' updates displace one another's basins and the joint state drifts. A well-designed system makes the three basins **interlock** into a **single emergent fixed point** reachable by none alone: model contraction lands in the symbolic-verifiable region, the symbolic guarantee lands in a human-auditable representation, human judgment lands in a model-attendable structured form. The composed map is strictly more contractive than any component.

The design target is therefore not per-agent improvement but **engineering the joint-system basin geometry** for fast, reliable convergence to a mutually accepted output. This reframes:

- **Diagnostics** = *boundary objects* co-aligning the human/model/checker basins (one anchor across all three) — not "human error messages" vs "model payloads."
- **Tools** = selected by whose output lands in the basin of every downstream reader, not by producing agent.
- **Failure modes** = *basin-separation events* (joint map fragments, agents stop co-converging) — not "hallucination" or "user confusion."

Platform objective: make basins **legible, addressable, shapeable**, so effort goes to alignment rather than translation.

### InD vs OoD: The Native Coordinate System

For models, **in-distribution (InD) vs out-of-distribution (OoD)** is the primary coordinate. Generalization is qualitatively, not merely quantitatively, better InD, and basins nucleate **around InD examples**: an iteration initialized near a known case contracts into its trajectory and conclusion. Two phenomena follow.

**No gibberish.** Output is incoherence-free (distinct from "contested"): the iteration almost always lands in *some* InD basin even when the correct answer is far from all of them, yielding structured, confident output — confidence being a property of *being-in-a-basin*, not *being-in-the-correct-one*.

**Low inventiveness** — the same property, dual sign. The model maximizes residence in InD basins and avoids OoD, the region of lost contraction and collapsed accuracy. Adaptive for typical load, wrong for invention.

**InD support is fragmented, not connected** — the load-bearing structural fact. The well-handled input set is a constellation of islands across OoD gaps; human-adjacent problems may occupy distinct islands, and small rephrasings translate a query between islands or off-support.

Therefore **models must often be led** across the basin landscape. The human's function is not only evaluation but **steering** — perturbing `X` (hints, examples, intermediate framings, partial code, error messages) to relocate the iteration's fixed point. Mechanically: Δ`X` → Δdynamics → Δlanding-basin. This is the primitive of human–AI collaboration; "prompting" understates it.

**OoD targets require forcing.** When the target lies in no island, the model must be driven out of InD into the chaotic region and **led stepwise**; unforced, it relaxes to the nearest InD basin and fails by reverting to the closest known pattern. Invention is thus an active, adversarial process against the model's contraction toward familiarity — categorically harder than refinement.

Platform affordances:

- **Nudge surfaces.** First-class, cheap, repeated, structured perturbation — not a chat-box afterthought. Every diagnostic / test failure / partial result is a candidate `X`-perturbation.
- **Basin observability.** On output, the high-value signal is *which training neighborhood* it originated from — currently unobservable; a large tooling opportunity.
- **Explicit OoD mode.** A channel to assert "non-modal request; suppress regression to nearest pattern," plus the sustained context that holds the iteration in the chaotic region.

The model-only picture ends here; human-in-the-loop, ownership/responsibility, and the platform's two goals are in [Models, Humans, and Programs as One System](joint-system.md).

## The Information-Theoretic View of Model Memory

The iterated-map lens describes **dynamics**; its dual describes **statics** — the trained model's knowledge as an object and the operations it supports. Dynamics asks *which basin*; statics asks *what is stored, and is it retrievable / enumerable / invertible*. Logos relies on the static lens equally, because it derives — from architecture, not observation — the dominant failure mode in model-authored code.

### Memory as a compressed forward-only program

Generalization is compression: the network stores its training distribution's regularities as a short program, not a table (the compressibility axis of [Two Domains of Tasks](#two-domains-of-tasks), memory-side). The compression is **implicit** — no materialized inventory of known facts exists in the weights, only a function evaluating a learned map pointwise.

The model is thus a **point-query engine**: a cheap one-pass forward map, with **no inverse** and **no enumeration**. "Which inputs yield this output" (preimage) and "list all cases of kind X" (domain scan) require a materialized, navigable extent that is never constructed — the induced input-space partition is evaluated, never written. A database exposes a walkable index; a model exposes a callable function.

> A model computes *f at a point*. It cannot *enumerate dom(f)* or *invert f* — both require a materialized extent the architecture omits. Not a scale-closeable gap, but a consequence of storing knowledge as a compressed forward function rather than an index.

### Two kinds of gap

When the model emits an incomplete artifact (common cases handled, remainder silently dropped), omissions fall into two structurally distinct classes; discriminating them is the central problem:

- **OOD gap.** No mass formed — the case is absent from the training distribution and unbridged by generalization, so no learned function exists there. Closure requires *external acquisition*.
- **InD gap.** Mass is present — presented explicitly, the case is handled immediately and correctly — but unemitted, because emission would require *enumerating* the feature's sub-behavior set, the missing operation. The knowledge is in-distribution yet not self-listable.

The InD gap is the costly one: a deficit of **access**, not knowledge. Pre-probe, **OOD and InD gaps are externally indistinguishable** (both read as "X silently absent"); the regime is determined only by probing. This is the static image of the dynamical [InD vs OoD](#ind-vs-ood-the-native-coordinate-system) fact: "basins nucleate on InD islands" ≡ "mass exists, but forward dynamics from an ordinary prompt contract to the modal basin and never visit the island." **"Models must be led through the basin landscape" (dynamical) and "localization must come from outside" (static) are one proposition in two vocabularies.**

### Generation ≠ verification

Producing a complete artifact requires selecting one trajectory from an exponentially branching space *and* allocating effort across thousands of sub-behaviors with no internal salience signal — yielding uniformly shallow coverage. Verifying/repairing a *specific* flagged case is conditioned on a near-complete answer specification (failing example pins behavior, surrounding code pins structure), so conditional entropy `H(answer | witness)` is small and the model is fluent. Check-given-witness is cheap; produce-and-cover is not — the recognize-vs-find asymmetry. Two corollaries, paradoxical only without the asymmetry:

- **Self-written tests inherit the artifact's blind spot.** A model-authored test draws from the same compressed feature-model that produced the implementation, exercising exactly the implemented subset. The gap lies in that model's *complement*, untestable from within it; artifact and self-test blind spots coincide.
- **Self-checking does not surface InD gaps.** The check is sampled from the same distribution with identical coverage bias; "looks done" is a completion under the same shallow-coverage pressure, and confidence indexes basin-membership, not completeness.

### Irreducibility from inside

Materializing InD knowledge by exhaustive sampling is possible in the limit and intractable in practice: InD gaps occupy the low-probability tail (the reason for non-emission), surfacing an item of probability ε costs ~1/ε draws, and the gaps span exponentially many tail regions. Sampling-to-enumerate *is* brute-force inversion — re-incurring the exponential the absent inverse operator imposes — cheap where useless (the modal output, already available) and astronomically costly where needed (the tail), with support recovered only in the limit, no stopping rule, no completeness certificate.

Nor is the deficit *offloadable* like arithmetic. Offloading requires the missing function's domain to be **external** (arithmetic ranges over numbers; hand them to a calculator). "Enumerate what the model knows" has domain = **the model itself**; a function over X cannot be offloaded to a party without access to X, and only the model accesses its learned extent while being exactly the architecture that cannot scan it. Deficit and data are co-located inside the model — not a missing *capability* (offloadable) but a missing *reflexive* operation (not offloadable in principle).

> The model cannot enumerate its own knowledge, and no self-querying induces it. If it cannot list what it lacks, the list must come from outside — the consequence driving the methodology in [Coding Tasks](coding-tasks.md).

## What Cannot Be Assumed About Models

Negative invariants, applicable whenever "just have the model do it" arises:

- **No persistent inter-invocation state.** Anything not explicitly stored by the platform is lost.
- **No reliable instruction-following against the training prior.** "Always output JSON" holds in the common case and fails in the tail; validate, do not trust.
- **No self-knowledge of error.** No internal signal correlates with correctness on high-compressible tasks; confidence tracks surface plausibility.
- **No clean OoD generalization.** Adversarial inputs, novel domain products, and train-adjacent-but-distinct tasks degrade silently.
- **No invariant enforcement.** Required invariants must be enforced *by the platform*.
- **No self-enumeration.** Pointwise evaluation only, no listing; coverage sets must be supplied and checked externally, and self-sampling is not a substitute (see [The Information-Theoretic View of Model Memory](#the-information-theoretic-view-of-model-memory)).

Each maps onto a requirement that follows.
