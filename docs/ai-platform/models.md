# How Models Behave

The rest of this section argues for specific platform requirements. That argument rests on a small number of facts about how LLMs actually behave. This page collects those facts — model behavior in isolation: what tasks they generalize on, what they cannot reliably do, how their iteration dynamics work, where their basins of attraction sit. The companion page [Models, Humans, and Programs as One System](joint-system.md) takes those facts and works out the joint-system consequences (responsibility, ownership, the human-performance model, and the platform's two fundamental goals).

This is not a survey of ML — it is the subset of model behavior that determines what tools and languages should look like in an era when models write a lot of the code.

The framing here follows the Memoria Framework's analysis of LLMs as applied components ([memoria-framework.dev/docs/applications/aiml](https://memoria-framework.dev/docs/applications/aiml/)). Where this page makes claims about model behavior, that source is the reference.

## Two Domains of Tasks

LLMs are probability distributions over text. They generate by autoregressive sampling, and what we want from them is correct prediction on **unseen** inputs — i.e. *generalization*, not recall.

Tasks divide into two regimes:

**Low-compressible tasks.** Translation, style transfer, summarization, elaboration, code-completion in well-trodden idioms. The mapping from input to output has high entropy, no short canonical algorithm, and benefits from massive memorization across many examples. Here, *generalization is not the bottleneck*; **scale is**. Bigger models with more data get monotonically better on these tasks.

**High-compressible tasks.** Arithmetic, logic puzzles, constraint solving, board-game search, database-query execution, formal verification, anything that admits a short deterministic procedure. The mapping is highly regular — there *is* a short program — but neural LLMs cannot reliably run that program. Generalization on these tasks depends on training data quality, architecture, and learning algorithm — all of which are fixed in current neural LLMs and not solvable by scale.

The asymmetry is structural, not a bug to be patched out by the next training run. Per the Memoria text: *"no amount of scaling can make a database engine out of a neural network."*

A useful illustration: borrow the old Wirth formula, *Programs = Algorithms + Data Structures*. The two halves sit on opposite sides of the compressibility axis. **Data** is the low-compressible half — facts about a specific world, irreducible by any clever procedure, recoverable only by storing them. **Algorithms** are the high-compressible half — short procedures that, applied to data, produce many outputs; their value is exactly that they are *not* a lookup table.

Symbolic programs make this split **explicit**: the language separates code from values, the compiler types each, the runtime allocates them in different memory, and tools can reason about them independently. This is not bookkeeping — it is what allows a 10-line algorithm to operate on a terabyte of data. The compression ratio is enormous, and visible.

Neural networks fold both halves into one homogeneous parameter tensor. Algorithms and data live in the same weights, learned from the same gradient, with no marker telling you which is which. That is why these models are so good at the low-compressible half (memorization scales with parameter count) and so unreliable at the high-compressible half (a learned approximation of an algorithm has to share weights with all the *data* the network also stores). It is also why interpretability is so much harder than the original modelling problem: separating "the program" from "the table it runs against" inside a trained network is a problem strictly harder than the one the network was trained to solve. There is a real sense in which **the interpretation problem looks harder than the AGI problem itself**.

The platform consequence is straightforward: keep the split explicit on the outside, even when the inside has no notion of one. Data in storage; algorithms in code; the LLM as a routing/recognition layer between them, not as the thing that holds either.

## What This Means for Determinism

The practical consequence is sharp:

> **LLMs cannot reliably execute deterministic algorithms — even when told to "reason step by step."**

A reasoning-mode model walking through long multiplication does not become a multiplier. It still samples each token from a distribution conditioned on the prior tokens, and that distribution carries no guarantee of executing the algorithm faithfully. The right framing is not "imitate vs run" but **approximate execution**: the model produces an answer that *approximates* the result of the deterministic algorithm, and the quality of the approximation tracks how well the model generalizes on that specific task class.

That makes the failure mode bimodal rather than uniform. On task classes where the model generalizes well — small operands, common shapes, in-distribution wording — the approximation is indistinguishable from a faithful run; nothing visible goes wrong. On task classes where it generalizes poorly — large operands, adversarial structure, novel framings, long chains — it falls over abruptly. There is no graceful degradation in between, and no internal signal that tells you which regime a given input lands in.

Errors are correlated with context length, with operand magnitude, with surface form, and with whatever happens to be in distribution near the prompt. The same model that handles a hundred similar arithmetic prompts in a row may still produce confidently wrong output on the hundred-and-first that happens to land outside its generalization basin.

This is not specific to arithmetic. It applies to any task whose correctness criterion is "the deterministic procedure produces output X". Type checking, borrow analysis, constraint solving, dependency resolution, parser implementation, query planning, ABI lowering — all of these have correctness criteria that LLMs cannot guarantee no matter how big they get.

## Two Rules That Follow

From the asymmetry between the two domains, two design rules:

**1. Anything that *requires* deterministic execution must be offloaded to a classical symbolic algorithm.**

If the task has a "right answer" the model must produce, and that right answer is the output of a deterministic procedure, route the task to that procedure. Do not ask the model to perform it. The choice of formalism does not matter — SAT/SMT, term rewriting, compilation, database query, plain code — what matters is that the determinism live somewhere other than the model.

"Determinism" here is shorthand for two distinct properties that travel together: **reproducibility** (the same input yields the same output, today and a year from now) and **verifiability** (the output can be independently checked against the rules of the procedure). Symbolic algorithms give both for free. LLMs give neither: their outputs are not bit-stable across runs in the general case, and there is no way to *check* a model's answer except by running another deterministic procedure against it — which, if available, is the procedure that should have produced the answer in the first place. An LLM is a black box you trust; a symbolic algorithm is a glass box you verify. Anything that needs to be auditable, reviewable, or reproducible has to come from the latter.

The model's role in such systems is *recognition and routing*, not computation: read the input, identify which formal problem it implies, dispatch to the appropriate solver, post-process the result for the human (or the next model). This is the architecture the Memoria text recommends: a relatively small, fine-tuned LLM as the front-end of a system whose substantive work happens in solvers.

**2. Anything that *can* be offloaded *should* be (eventually).**

Even when the model could plausibly handle a task, offloading is preferable on cost and energy grounds. LLM inference is O(N) in parameter count per token; the model's "implicit memory" — its weights — is an extraordinarily expensive substrate compared to a database index, a B-tree, a hash table, or a compiled function. As the Memoria text puts it under the *trading speed for memory* heading: when the energy cost of storing and retrieving a solution is lower than recomputing it, store it. This applies recursively to model output, since recomputing model output is much more expensive than storing it.

The rule is not "minimize model usage at all costs". It is "do not have the model do something a cheaper deterministic component could do, once that deterministic component exists." The migration is incremental: in the early stages of a system, the model does more; as solvers, indexes, and caches accumulate, the model's share of the work shrinks — and that is the desired direction.

## Determinism as Guardrail

The two rules above cover deterministic components in their *computational* role — substituting for work the model cannot do reliably, or doing work it should not be asked to repeat. There is a second role, less often named but at least as load-bearing: **keeping the model on a trajectory**. Models drift. Across enough steps — tokens, turns, sessions — they wander off the path the task requires, even when each individual step looked locally sensible. The mitigation is not "a better model"; it is a scaffold of deterministic checkpoints that the trajectory has to pass through. Type checks, test runs, schema validation, lints, CI gates, structured tool interfaces, formal preconditions on tool calls, refusal of malformed outputs — every one of these is a deterministic predicate the trajectory either satisfies or is forced to revisit. The model's job becomes "produce something that passes the next checkpoint", not "produce the right thing in one go".

This is the main reason **the volume of classical code in front of and around models grows, not shrinks, as models get more capable.** A more capable model can be entrusted with longer sub-trajectories between checkpoints, but the checkpoints themselves are still where reliability comes from. The pattern is visible in the wild: when the prompts and orchestration logic of frontier coding agents have leaked, the bulk of what was found was not clever prompting but **a great deal of plain `if`/`then` code** wrapping the model — guardrails, mode dispatch, tool gating, format checks, retries on detectable failures. There are no magic prompts; there is a lot of conventional software keeping a probabilistic component on rails. (And, notably, much of that conventional software encodes the operator's preferences and policies, not the user's.)

The two rules above and this one combine into a sharper statement: deterministic components in an AI system serve **three** roles simultaneously — *compute* the parts the model cannot, *cache* the parts it can but shouldn't recompute, and *constrain* the trajectory through the parts it does compute. A platform optimized for AI as primary user is, in large part, a platform that makes all three easy to add, compose, and audit.

## Models as Iterated Maps

The compressibility argument above is one half of how models behave. The other half — orthogonal to it and equally load-bearing for platform design — is dynamical.

Model operation — at every level, from token-by-token generation up to multi-turn agentic loops — has the form

```
X_{n+1} = F(X_n)
```

where `X_n` is the state (context, prompt, in-flight code, conversation history) and `F` is "run the model once and update". Whether such a sequence converges, cycles, or drifts is governed by the **contraction properties** of `F` — and the answer is not "always converges". It depends on `F`, on the input, and on the regime.

Several behaviors fall out of this view, and they all show up in practice.

**Stopping is taught, not intrinsic.** Modern frontier models almost never run away into infinite generation. This is not because the underlying dynamics naturally terminate — it is because the models have been trained to stop at appropriate points. Remove that training and the dynamics resume their natural shape.

**Looping is not gone, just moved up a level.** The same dynamics reappear at the **agentic-loop** level. On code-generation tasks, mid-tier models can still enter cycles: try a fix, fail, try a similar fix, fail, drift between two near-identical attempts indefinitely. Frontier models on the same tasks converge in a few iterations. The difference is not "more capable" in some general sense — it is that the frontier model's `F` happens to be more contractive on this class of inputs, so the iteration finds a fixed point faster and more reliably.

**Iteration is Turing-complete.** The form `X_{n+1} = F(X_n)` can express arbitrary computation (the proof is standard and not repeated here). So *anything* a model does — at any level of the stack — can in principle be cast as a sequence of iterations converging to some fixed point. This is not a vacuous claim: it tells you what the right object of study is. Not "what does the model output?" but "what is the basin of attraction of `F` for this input, and how fast does it contract?"

**Symbolic vs. probabilistic dynamics differ sharply in fixed-point structure.**

A symbolic algorithm is a degenerate case of the iterated-map view: it has **one well-defined fixed point** (the result), reached by **a narrow, fully analyzed set of trajectories** (the algorithm's execution paths). The algorithm designer has done all the work of choosing `F` so that contraction is guaranteed and the destination is the right one. Reproducibility and verifiability follow because the dynamics are trivially simple.

A probabilistic algorithm — a model — has, in general, **many fixed points, complicated basins of attraction, and trajectories sensitive to small input perturbations**. There is no global guarantee of contraction. There is no a priori knowledge of which fixed point a given input will reach, or whether it will reach one at all rather than enter a cycle or drift. Some fixed points correspond to correct answers; others correspond to confidently wrong ones; still others correspond to looping on partial answers. The model's ability to "do" a task is, in this view, the property that the basin of attraction around correct answers is large and contractive enough on the relevant input distribution.

This reframing is what makes the platform argument concrete:

- **Offloading deterministic work** is replacing a region of complicated probabilistic dynamics with a region of trivial symbolic dynamics — collapsing many fixed points into one, and many trajectories into one verified path.
- **Reward signal** is the thing that shapes `F` in flight: a useful, structured signal makes the iteration more contractive on the right answer, and shrinks the basins of the wrong ones. A noisy or human-shaped signal does the opposite.
- **Convergence speed is a platform property, not just a model property.** The same model, given a better feedback loop, converges in fewer iterations on the same task. That is one of the levers the platform actually controls.

### Attraction Basins as First-Class Objects

The iterated-map view turns out to be more than a metaphor. **Attraction basins should be first-class concepts** in the design of a human–AI development platform — named, observable, optimized for, talked about explicitly.

Three kinds of agents share the workspace, and each has its own basin geometry:

- **Models** have wide, smooth, but irregular basins. They settle quickly when the input is in distribution, drift or cycle when it is not, and the boundaries between basins do not align with anything humans naturally perceive.
- **Humans** have narrower, sharper basins, shaped by working memory, attention, and learned heuristics. We recognize a small number of patterns very precisely, get exhausted by long-running iterations, and move slowly between basins.
- **Symbolic algorithms** have degenerate basins: one fixed point, one trajectory, no drift, no fatigue. They are immovable and cheap to run, but they only cover the regions someone has built them for.

A naive system runs each of these in its own loop and forces the others to translate. The human translates model output into something verifiable; the model translates human prose into something it can act on; the symbolic algorithm only sees the small part of the problem someone hand-encoded. Each agent's iterations leave the others' basins in different positions, and the joint state drifts.

A well-designed system arranges things so that the basins of all three **interlock** — the trajectories of one feed productively into another, and the system as a whole has a **single emergent fixed point** that none of the three could reach alone. The model's wide-but-noisy contraction lands in a region the symbolic component can verify; the symbolic component's narrow guarantee lands in a representation the human can audit; the human's sharp judgment lands in a structured form the model can attend to next iteration. The joint map is more contractive than any of its parts.

This is the actual target of platform design. Not "make the model better" or "make the tools better" or "make the human's life easier" individually, but **engineer the basin geometry of the joint system** so that it converges, reliably and quickly, to outputs everyone agrees on.

Concretely, this reframes several familiar concerns:

- **Diagnostics** are not "error messages for humans" or "structured payloads for models" — they are *boundary objects* that keep the human, model, and symbolic-checker basins aligned. The same diagnostic should be the same anchor in all three minds.
- **Tools** are not picked by which agent uses them. The right tool is the one whose output is in the basin of all the agents that need to read it next.
- **Failure modes** are not "the model hallucinated" or "the user got confused" — they are *basin separation events*, places where the joint map fragmented and the agents stopped converging on the same thing.

The platform's job is to make basins **legible, addressable, and shapeable** — so that engineering effort can be spent on aligning them rather than on translating between mismatched ones.

### InD vs OoD: The Native Coordinate System

For models specifically, **in-distribution (InD) vs out-of-distribution (OoD)** is not a footnote — it is the primary coordinate system for everything else. Models generalize substantially better in-distribution than out-of-distribution; the performance profiles of the two regimes are different in kind, not in degree. And basins of attraction form **around InD examples**: dynamics initialized anywhere near a known case will slide into a familiar trajectory and a familiar conclusion.

This explains two things at once.

**Why models do not produce gibberish.** Not "things some humans aggressively disagree with" — actual incoherence. They almost always land in *some* InD basin, even when the right answer is far from any of them. The output is recognizable, structured, and confident, because confidence is a property of being-in-some-basin, not of being-in-the-right-one.

**Why models are not very inventive.** The same property that prevents incoherence prevents novelty. Models work hard to stay inside InD basins and steer away from OoD. OoD is the chaotic region where their dynamics lose contraction, where their accuracy collapses, where they themselves "feel" the pull of nothing — so they avoid it. This avoidance is adaptive for typical use; it is exactly wrong for invention.

**InD is fragmentary, not contiguous.** This is the crucial structural fact. The set of inputs the model handles well is *not* a single connected region. It is a constellation of islands separated by OoD ravines. Two problems that look adjacent to a human can sit in entirely different InD islands; a small rephrasing can move a query from one island to the next, or off the map.

The practical consequence is that **models often need to be led** through the basin landscape. The human's role is not just to evaluate output — it is to nudge the system from one InD basin into another, by feeding back hints, examples, intermediate framings, partial code, error messages, or any other change to `X` that reshapes the iteration's destination. Mechanically, feedback changes `X`, which changes the dynamics, which changes which basin the model lands in next. This *is* the steering primitive of human–AI collaboration. Calling it "prompting" undersells what it is doing.

**OoD work requires forcing.** Where the task genuinely demands something new — not in any island — the model has to be pushed out of its comfortable InD basins on purpose, into the chaotic region, and then **led by hand**. Left alone, it will drift back to the nearest familiar basin and fail by reverting to the closest known thing. Genuine invention with a model is therefore a much more active process than refinement: it is sustained adversarial guidance against the model's own contraction toward familiarity.

For the platform, this turns several pieces of design into specific affordances:

- **Surfaces for nudging.** The platform must make the cheap, repeated, structured nudge a first-class action — not an afterthought layered onto a chat box. Every diagnostic, every test failure, every partial result is potentially a nudge; the platform decides whether it is a useful one.
- **Visibility into which basin the model landed in.** When the model gives an answer, the most useful thing to know is often *which neighborhood* of training experience it came from. Today this is invisible; better tooling here is one of the larger open opportunities.
- **Explicit support for OoD work.** The platform should make it easy to *tell* the system "this is not a normal request, do not regress to your nearest familiar pattern" — and to provide the running context that keeps the model out in the chaotic zone for the duration of the task.

That is as far as the model-only picture goes. The remaining design consequences — what it means for the human in the loop, how ownership and responsibility are allocated, and what the platform's two fundamental goals are — are the subject of [Models, Humans, and Programs as One System](joint-system.md).

## The Information-Theoretic View of Model Memory

The iterated-map view is one lens on model behavior; it describes the **dynamics**. There is a second lens, dual to it, that describes the **statics** — what a trained model's knowledge *is*, as an object, and which operations that object does and does not support. The two do not compete. Where the dynamical view asks *which basin does this trajectory fall into*, the information-theoretic view asks *what is stored, and can it be retrieved, enumerated, or inverted*. Logos's design leans on the second lens as heavily as the first, because it is the one that explains — structurally, from the architecture rather than as an observed quirk — the single most consequential failure mode in AI-authored code.

### Memory as a compressed, forward-only program

Generalization is compression: a trained network stores the regularities of its training distribution as a short program, not as a table (this is the same compressibility axis as [Two Domains of Tasks](#two-domains-of-tasks), seen from the memory side rather than the task side). The decisive point is that the compression is **implicit**. There is no materialized list of "what the model knows" anywhere in the weights — there is a function that, run forward, evaluates a learned mapping at one point.

That makes the model a *point-query engine*. It computes a forward map cheaply, in one pass. It does not compute the **inverse**, and it does not **enumerate**. "Given this output, what inputs produce it" and "list all the cases/behaviors of kind X" are not forward evaluations — they are an inversion or a scan over a materialized, navigable extent, and no such extent exists. The partition a network induces over its input space is never written down; it is only ever evaluated. A database has an index you can walk; a model has a function you can call.

> A model answers *what is f at this point*. It cannot answer *enumerate the domain* or *invert f* — those require a materialized extent the architecture does not build. This is not a gap scale closes; it is a property of storing knowledge as a compressed forward function instead of as an index.

### Two kinds of gap

When a model emits an incomplete artifact — an implementation that handles a feature's common cases and silently drops the rest — the omissions fall into two structurally different classes, and distinguishing them is the whole game.

- **OOD gap.** The relevant mass never formed. The training distribution lacked the case, generalization did not bridge it, so there is no learned function to evaluate there. Closing it requires *new external content* — genuine acquisition.
- **InD gap.** The mass is present — the model "knows" the case, in the operational sense that when the case is placed in front of it, it handles it immediately and correctly — but it was not emitted, because emitting it would have required *enumerating* the feature's full set of sub-behaviors, and enumeration is the one operation the model does not have. The knowledge is in-distribution but not self-listable.

The InD gap is the surprising and expensive one: the deficiency is not knowledge but *access*. And before the case is probed, **OOD and InD gaps are indistinguishable from the outside** — both present as "the model silently did not do X." Which regime you are in is knowable only after probing.

This is precisely the static shadow of the dynamical [InD vs OoD](#ind-vs-ood-the-native-coordinate-system) picture. "Basins form around InD examples, and the InD region is a constellation of islands" is the same fact as "the mass exists, but the forward dynamics started from an ordinary prompt contract toward the modal basin and never visit the island." The case is reachable — but only if something *leads* the trajectory there. **"Models need to be led through the basin landscape" (dynamical) and "localization must come from outside" (information-theoretic) are one statement in two vocabularies.**

### Generation is not verification

Producing a complete artifact means committing to one trajectory out of an astronomically branching space *and* allocating effort correctly across thousands of sub-behaviors with no internal signal for which deserves attention next — so the model spreads effort and leaves uniform shallow gaps. Verifying or repairing a *specific* flagged case is conditioned on a near-complete specification of the answer (the failing example pins the expected behavior; the surrounding code pins the structure), so its conditional entropy is small and the model does it fluently. Checking-given-a-witness is cheap; producing-and-covering is not — the same asymmetry that separates recognizing a solution from finding one.

Two corollaries look like paradoxes until the asymmetry is in hand:

- **Self-written tests route around the model's own gaps.** A test the model writes is drawn from the same compressed model of the feature that produced the implementation; it exercises exactly the subset that was implemented. The gap lives in the *complement* of that shared model — and you cannot write a test for what lies outside your model of the feature. The artifact's blind spot and its self-test's blind spot coincide.
- **"Ask the model to check its own work" does not surface InD gaps.** The check is generated from the same distribution as the artifact, with the same coverage bias. Confidence is a property of being-in-a-basin, not of being-complete; the model's "looks done" is itself a generated completion under the identical shallow-coverage pressure.

### Why this cannot be fixed from inside

You might hope to materialize the model's InD knowledge by sampling it exhaustively — drive the forward map until everything it knows has been seen, then store *that* as an enumerable extent. In the limit this is possible. It is also intractable, for a principled reason: InD gaps live in the low-probability tail (that is *why* they were not emitted), surfacing an item of probability ε costs ~1/ε draws, and the gaps are spread over exponentially many such tail regions. Sampling-to-enumerate is brute-force inversion; it re-incurs exactly the exponential the missing inverse operator imposed. It is also cheap precisely where it is useless (the modal output you already get) and astronomically expensive precisely where you need it (the rare tail). And even unbounded sampling yields the support only in the limit, with no stopping rule and no completeness certificate — you never know whether the whole tail has been seen.

Nor can the deficiency be *offloaded* the way arithmetic is. Offloading works when the missing function's domain is **external** to the model: arithmetic is over numbers, numbers live outside, hand it to a calculator. "Enumerate what the model knows" is a function whose domain *is the model*. You cannot offload a function over X to a party with no access to X — and only the model has access to its own learned extent, while it is exactly the architecture that cannot scan it. The deficit and the data coincide inside the model. This is not a missing *capability* (offloadable) but a missing *reflexive* operation (not offloadable in principle).

> The model cannot enumerate its own knowledge, and no amount of self-querying makes it. If the model cannot list what it does not know, the list must come from outside. That single consequence drives the development methodology in [Coding Tasks](coding-tasks.md).

## What Cannot Be Assumed About Models

A few negative claims, useful to keep in mind whenever the temptation to "just have the model do it" appears:

- Models do not have persistent state between invocations. Whatever the platform does not store explicitly is gone.
- Models do not reliably follow instructions that conflict with their training distribution. "Always output JSON" works most of the time and fails in rare prompts; the platform must validate, not trust.
- Models do not know when they are wrong. There is no internal signal correlated with correctness on high-compressible tasks; confidence is shaped by surface plausibility, not by truth.
- Models do not generalize cleanly out of distribution. Adversarial inputs, novel domain combinations, and tasks that "look like" but differ from training tasks all degrade quietly.
- Models cannot enforce invariants. Anything the platform requires to be invariant must be enforced *by the platform*, not relied on from the model.
- Models cannot enumerate their own knowledge. They can evaluate what they know at a point, but cannot list it. Any set of cases that must be covered has to be supplied and checked from outside; the model has no operation that scans its own extent, and self-sampling cannot stand in for one (see [The Information-Theoretic View of Model Memory](#the-information-theoretic-view-of-model-memory)).

Each of these is reflected in the requirements that follow.
