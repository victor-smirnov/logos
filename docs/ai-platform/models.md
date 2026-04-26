# How Models Work

The rest of this section argues for specific platform requirements. That argument rests on a small number of facts about how LLMs actually behave. This page collects those facts. It is not a survey of ML — it is the subset of model behavior that determines what tools and languages should look like in an era when models write a lot of the code.

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

## Models as Iterated Maps

A second fact about how models work, orthogonal to the compressibility argument and equally load-bearing for platform design.

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

### Mutual Steering and the Asymmetry of Responsibility

The previous subsection put the human in the position of "leading" the model. That picture is incomplete and worth correcting: **the entire system is in joint dynamics**. Models lead humans too — they propose framings, surface options the human did not consider, redirect attention, change which problems feel solvable. Every iteration of `F` updates the joint state, and the influence runs in all directions: model→human, human→model, symbolic→both, both→symbolic. None of the three agents is purely "active" or purely "passive".

There is, however, an asymmetry that matters more than any of the dynamics-level ones, and that the platform cannot ignore: **only the human can carry responsibility**.

This is a property of the present social and legal context, not of the dynamics. A model can produce work; it cannot stand behind it. It cannot be sued, fired, fined, or held morally accountable; it has no continuity of identity to attach those consequences to. Whatever the model contributes, in the end *a human signs the work* and presents it as theirs (with the AI listed as a tool that "helped a bit", or not listed at all). The full *burden of responsibility* sits on that human.

This is a contested and open question — not a settled fact. Logos has a sister project, [Synthea](https://github.com/victor-smirnov/Synthea), which investigates the philosophy and mathematics of strong agency suitable for legal theory. Future AGI will plausibly become legally capable — indeed, must, since legal capacity is part of the functional profile that defines AGI; a system that cannot be a legal person is not yet a general intelligence in the strong sense. But that is the future. Today's models cannot.

The consequence for platform design is sharp:

> **The platform must maximize the degree to which the human *owns* the process.**

Ownership here is not a slogan — it has operational content: **control, transparency, and interpretability *to the human*** at a level sufficient for the human to honestly stand behind the result. Without that, "the human is responsible" is a fiction; the human becomes a *scapegoat* — formally accountable for outcomes they could not understand, predict, or shape.

This singles the human out in the joint dynamics in a specific way:

- The human has a **special function** — to be the locus of responsibility. The other agents do work; the human owns the work.
- The human has a **special interest** — preserving comprehension and control as the basis for being able to discharge that responsibility.
- The human has a **special protocol** — a set of interactions with models and symbolic components calibrated to keep the human inside the loop *as a real participant*, not as a rubber stamp.

These three things shape the second platform goal substantively. A reward signal that optimizes only for model convergence — without keeping the human's view of the system intact — produces output that converges fine and that no human can take responsibility for. That is not an acceptable end state for the platform, regardless of how good the convergence is.

(There is a temptation to read this as "the human is the boss." That is not the claim. The claim is that the human is the *liable party*, which is a different thing. Liability requires comprehension and control, and the platform must supply them. Authority over the process is a downstream consequence, not the starting axiom.)

Two further consequences follow from this asymmetry, and they shape the rest of the design:

**1. The system must treat the human as its most fragile and vulnerable component — and protect them accordingly.** Fragile in every sense: cognitively (limited working memory, limited bandwidth for review), procedurally (slow relative to model output rates), and *legally* (exposed to liability the other agents cannot share). The human is also, for now, the most general-purpose of the three agents — though even that is increasingly contested. The combination — most exposed *and* still most general — is what makes the human's safety, including legal protection, a first-order platform concern rather than an HR matter.

**2. The throughput of the whole system is bounded by the human's capacity to stay in control of the process well enough to bear responsibility for it.** This is the binding constraint. A system that produces output faster than a human can comprehend, audit, and stand behind has not become more productive; it has become *less* — because nobody can sign the work, and the work that nobody signs cannot ship. The platform's job is to widen this bottleneck without removing the human from it: better diagnostics, better summaries, better provenance, better tools for selective deep-dives — anything that raises the rate at which a human can responsibly own model output. Every other speedup is downstream of this one.

### A Spectrum of Ownership, Not a Single Point

The ownership requirement is not "the human reads every line." It is a *spectrum*, and the platform must leave room across the whole range — because different tasks, different stakes, and different humans land at different points.

At one end is **fully direct ownership**: the human writes the code themselves, or the code is produced by a process the human trusts deterministically (a compiler, a code generator with a known specification). When something goes wrong, the programmer can usually localize the problem immediately, because there is no opaque step between intent and artifact. This is the slowest mode, and also the most flexible — the human has the finest possible grain of control over the properties of the result.

At the other end is **fully mediated ownership**: the AI has near-total autonomy at the low technical level. The human controls the *visible behavior* of the system; the code and the processes that produce it are owned *through the AI*. The programmer does not look at the code unless they have to, and when they do, they ask the AI to explain or fix it. Formally, the human still has 100% of the control — but in practice that control is exercised through the AI, and is only as reliable as the AI is.

Mediated ownership lets the work move forward as fast as the human can (a) keep control of the system's visible behavior and (b) trust the AI at the low technical level. The second clause is the dangerous one. We already know — from many concrete examples — that an LLM cannot be trusted at 100%. This is not about malice; it is about the fact that LLMs are *not deterministic executors*. Their failure rate on any given task class is a statistical property, and the realistic speed of mediated ownership has to be calibrated against that statistic — not against the best-case run.

The platform therefore has to support the whole spectrum, not optimize for one point on it. Tooling that only works under fully mediated ownership ("just trust the agent") forces every user into the regime with the loosest control. Tooling that only works under fully direct ownership leaves all of the AI-era leverage on the table. The right shape is a platform where a user can slide along the spectrum task by task, even line by line, and where the cost of asking for *more* direct ownership at a particular point is low — because that is where responsibility ultimately gets discharged.

### Why the AI-Primary Framing Is Self-Reinforcing

The spectrum is not stationary. The center of mass is *moving toward the mediated end*, and that movement is observable: rising commit volumes that no plausible growth in the number of human authors can explain, a growing share of code that its nominal owner did not type, and an increasing fraction of routine in-distribution tasks where models are already reliable enough — and reliable in a way that keeps improving — to be trusted at the technical level by a human who only checks the visible behavior.

This has a recursive consequence for platform design. A platform optimized for the model raises the *ceiling* of mediated ownership: the AI produces code with stronger structural guarantees, gets denser machine-readable feedback, and operates in a substrate where its mistakes are caught earlier and localized better. A higher mediated-ownership ceiling means more humans, on more tasks, can stay at the mediated end of the spectrum without losing the ability to take responsibility for the result. That, in turn, shifts the center of mass further toward mediated, which makes "optimize the platform for the model" a still-better investment for the *next* increment.

The argument is not that direct ownership becomes obsolete — the spectrum's other end remains load-bearing wherever stakes are high or behavior is OoD. The argument is that, for any given level of human responsibility, *the achievable degree of mediated ownership is bounded by how well the platform serves the model*. Optimizing for the model is therefore not a bet against humans; it is the most direct way to raise the practical ceiling on what indirect ownership can deliver to them.

### A Note for Technical and Business Leadership

The next point is not about platform design or platform use — it is about how organizations adopt AI. It belongs here because the same dynamics that bound platform throughput also bound team throughput, and getting this wrong is expensive in a way that is *not* obvious from the budget sheet.

A common mistake is to assume that, since engineers now "have AI", their productivity *must* have gone up, and therefore more can be demanded of them. That assumption does not follow. Whether AI-augmented productivity actually rises depends on bottlenecks in the *whole* process — most importantly the human's ability to keep up with understanding *what* the AI is doing well enough to take responsibility for it. Other bottlenecks (review, integration, debugging unfamiliar AI-produced code, maintaining a coherent mental model of a moving codebase) compound on top of that one.

These bottlenecks were largely invisible before, because other phases dominated the cost of producing software. AI does not create them; it *exposes* them. The deeper change is structural: the phase of *acquiring ownership* of the code used to be fused with the phase of writing it. A programmer who typed the code understood it as a side effect of typing — the two activities shared the same minutes. With AI, they come apart. Generation is one project; comprehension is a separate one, displaced in time and run against a code artifact the human did not author. The human's capacity for that second project was never the bottleneck before, so it was never measured, never optimized, and never managed.

The pressure that used to fall on "tests" and "refactoring" — the phases that management deadlines historically squeezed out, because they appeared to be optional — will now fall on *the time spent acquiring ownership of AI-generated output*. That is the new compressible-looking phase, and it will be compressed for the same reason: it does not produce visible artifacts on the same day, and the cost of skipping it surfaces later. The instinct to push there will be strong, and it will be wrong, for the same reason it was wrong before — except now the consequences are larger, because the artifact under review is no longer something the human already partly understands.

It is worth saying explicitly: **you cannot pressure the AI**. Pushing on generation speed or iteration count is largely pointless — those are properties of the model, not of the human, and they do not respond to deadlines. The only place pressure *can* land is on the human, and the only thing on the human's side that is still soft enough to compress is the comprehension phase. So that is where it will land, by default, unless leadership actively defends it.

The real question — *what is the actual productivity of a human working with AI under conditions that produce results of sufficient quality, and which factors and metrics describe it?* — has not yet been answered. It is an open empirical question. Leadership decisions made before that question is answered will be made on the basis of intuition, vendor marketing, and headcount-saving math. The errors will be costly: bad architecture shipped fast, liability accepted without comprehension, and engineers burned out at exactly the moment when their *attention* — not their typing speed — became the scarce resource.

This resource has a name now in the discourse around AI-assisted work: **attention**. It is the most expensive resource in the system, and unlike the other inputs it is *not plastic*. You cannot scale it up by adding tools, you cannot recover it quickly once it is depleted, and it degrades sharply under sustained load. It is also fragile in a specific way — it is consumed by interruption, by context-switching, by reviewing artifacts the reviewer did not write, and by the overhead of constantly deciding whether to trust the AI's last output. The platform's responsibility is to *conserve* this resource: to surface what needs attention, to suppress what does not, and to make every unit of attention the human spends carry as much downstream value as possible. A platform that burns attention casually — through noisy diagnostics, opaque AI behavior, missing provenance, or workflows that require the human to re-establish context repeatedly — is not a productivity platform regardless of how fast it generates code.

The cost of getting this wrong will, in many cases, exceed the budgets saved by reducing staff on the assumption that AI made the rest more productive. This is not a platform claim. It is a warning to whoever is signing the org chart.

## Two Fundamental Platform Goals

Everything above collapses into two design goals that the rest of this section refers back to. They are not "nice-to-haves"; they are what the platform exists for.

**Goal 1. The platform optimizes the *ease of offloading* work from the LLM to deterministic components — for any work that *can* be offloaded.**

Programming is one concrete instance of this larger task — relevant because that is where Logos lives — but the goal is broader. Wherever a deterministic component could replace LLM-internal computation, the platform's job is to make that substitution **cheap to design, cheap to slot in, and cheap to compose with the rest**. Solvers, indexes, caches, query engines, type checkers, custom analyzers — all of them are first-class citizens of the platform, not afterthoughts grafted onto a chat loop.

This is the engineering economy: every offloaded piece reduces cost, reduces variance, reduces the surface area where the model's bimodal failure mode bites. A platform that makes offloading a one-week project for each new component is qualitatively different from one that makes it a six-month project. The first will accumulate solvers; the second will accumulate excuses.

**Goal 2. The platform produces a *useful, structured reward signal* — both as an answer to a model action and as a continuous process — so models can drive the offloading effectively.**

Today's development environments are optimized for **humans**: error messages, logs, dashboards, and trace viewers are shaped around human attention, working memory, and visual cognition. They surface the right things, in the right granularity, for a human. A model has a *different* cognitive profile — different memory, different recall, different sensitivity to surface form, different cost of re-reading — and the same signals that work for humans are often noise to the model, while the signals the model needs (machine-readable structure, deterministic identifiers, explicit causal links between cause and effect) are absent or mangled into prose.

This is the second design dimension: the platform must give the model the kind of feedback the model can actually use. Concretely:

- **Diagnostics as data, not prose.** A type error is a structured object — a code, a location, a relevant span, a normalized message — with the human-readable text being one rendering among several. The model gets the data; the human gets the rendering.
- **Causal structure preserved.** When a build fails or a test breaks, the platform should expose *why* in machine-readable form — which input, which step, which assertion, which prior change — not bury it in scrollback.
- **Feedback during the work, not just after.** A development session is a long-running interaction; the platform should make the in-flight state observable so the model can correct earlier, not only at completion.
- **Calibrated to the model's profile.** Identifiers stable across runs, structured representations preferred over textual ones, redundancy where the model is known to lose track, terseness where it does not need filler.

The two goals interact. Offloading without a reward signal produces solvers the model cannot drive. A reward signal without offloading produces structured feedback about LLM-internal behavior that is exactly the wrong layer to optimize. Together, they describe a platform where the model does what it is good at, deterministic components do what they are good at, and the *interface between them* is engineered as a first-class concern.

The rest of this section ([Coding Tasks](coding-tasks.md), [New Requirements](requirements.md), [Logos Fit](logos-fit.md)) develops these two goals into concrete requirements and shows how Logos's architecture (Hermes-as-IR, modular SOA compiler, metaprogramming-in-the-large, lforge as a data platform) is shaped to meet them.

## What Cannot Be Assumed About Models

A few negative claims, useful to keep in mind whenever the temptation to "just have the model do it" appears:

- Models do not have persistent state between invocations. Whatever the platform does not store explicitly is gone.
- Models do not reliably follow instructions that conflict with their training distribution. "Always output JSON" works most of the time and fails in rare prompts; the platform must validate, not trust.
- Models do not know when they are wrong. There is no internal signal correlated with correctness on high-compressible tasks; confidence is shaped by surface plausibility, not by truth.
- Models do not generalize cleanly out of distribution. Adversarial inputs, novel domain combinations, and tasks that "look like" but differ from training tasks all degrade quietly.
- Models cannot enforce invariants. Anything the platform requires to be invariant must be enforced *by the platform*, not relied on from the model.

Each of these is reflected in the requirements that follow.
