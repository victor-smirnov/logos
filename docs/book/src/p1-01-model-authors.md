---
title: A language for model authors
verified-at: a4f617303
sources: [docs/book/notes/p1-01-model-authors.md, docs/book/notes/p1-01-literature.md, docs/adr/0032-logos-core-composition.md, https://logos-lang.dev/blog/, https://logos-lang.dev/docs/introduction/, https://logos-lang.dev/docs/roadmap/]
status: DRAFT
---

# A language for model authors

The premises of the project: who writes Logos code, who owns it, what each of them can and cannot do, and what a language owes both. Every later chapter is a consequence of this one. The argument draws on the blog series [Code in the AI-Primary Era](https://logos-lang.dev/blog/) (ten parts, 2026-07) and on positions stated in the project's design discussions (2026-10); claims are tagged in §5 as published, observed in the project, or hypothesis.

## 1. Problem and constraints

### 1.1 State of practice, H2 2026

Hand-writing code no longer pays. The reason is not effort: a model produces the same code faster, and the human's time is worth more elsewhere. Two exceptions remain:

- Critical code (payments, security), where manual verification is added on top of formal verification.
- far-OOD work (§2.4), where the model has nothing to derive from.

Institutions lag the practice; hiring still tests timed hand-coding of leetcode-style tasks.

The working unit of software development is a triangle: the model proposes, deterministic programs adjudicate (compilers, tests, solvers, CI), the human owns the result ([Two Users](https://logos-lang.dev/blog/your-language-has-two-users-now/)). This chapter is about the properties of the first and third corners and what they demand from the second.

### 1.2 Defaults calibrated for the wrong author

Every layer of the programming stack was built for a human at the keyboard: prose error messages, coloured logs, features rationed by whether an average engineer will misuse them, strictness limited by what users tolerate. With the model as the author, these defaults optimise the comfort of an agent who no longer does most of the writing. The task of this chapter is to replace them with defaults derived from the actual properties of both users.

## 2. Model

### 2.1 Two sources of experience

A human and a model are trained on different data.

- Human: embodied, interactive. A person learns by acting on an environment and observing the result; a large share of competence is tacit and never written down.
- Model: descriptions. A model memorises large volumes of text describing agents' interactions with environments (code, documentation, discussions, post-mortems, papers) and generalises over the descriptions.

Post-training of current models includes reinforcement learning in execution environments with verifiable rewards. Its volume is small and its scope narrow relative to pretraining on descriptions; the asymmetry stands. (Formulation to confirm.)

Consequences:

- The model's in-distribution is what is well described in text. Tacit practice is out of distribution for it; heavily described material (Rust semantics, SQL, the Datalog literature) is in.
- The descriptions span all traditions at once. C++ template metaprogramming, Datalog, Rust ownership, database internals and formal methods are simultaneously in-distribution for one model, and for no human team.
- The model learned from descriptions of other people running code, not from running it. The interactive loop missing at training time is supplied at inference: compiler, tests, REPL, oracles are the model's environment.
- Interface conventions grounded in embodied perception (colour, screen layout, spatial memory) carry little for the model.

### 2.2 Asymmetry of information processing

Models exceed humans in the capacity to process information by orders of magnitude (project estimate: 1000× and more). A lower bound on throughput alone: adult silent reading of non-fiction runs at ≈238 words per minute (Brysbaert 2019, Journal of Memory and Language; citation to verify), ≈5 tokens/s; model prefill runs at thousands of tokens per second. The estimate covers more than reading speed.

Observable effects:

- Working with models puts a large cognitive load on the human; the human cannot keep up.
- Model-written code is decent, not ideal. The common complaint ("slop") is about fit to human perception: too much code, structured unfamiliarly. A model writes human-fit code on request, and the volume still exceeds what a human reads.
- Models write effectively in languages considered too complex for the average programmer and therefore absent from commercial practice (dependent types, proof assistants, heavy type-level metaprogramming). These languages now become usable, because nobody on the human side has to write them.

The other ergonomic properties of a model are components or corollaries of the asymmetry: a large flat context instead of a small working memory; near-zero cost of verbosity (tokens, not keystrokes); no fatigue; no ego, so a rejection is information rather than injury ([The Compiler Is a Sensor](https://logos-lang.dev/blog/the-compiler-is-a-sensor/)).

The asymmetry also changes which designs spread. A design propagates when its coherence complexity $C$ is below the carrier's bandwidth $B$. For fifty years the carrier was the human implementer with fixed $B$, which selected for low $C$ ("worse is better", [Gabriel 1990](https://www.dreamsongs.com/WorseIsBetter.html)). With models as carriers $B$ rises with every release, and the selection regime flips toward coherent designs ([Worse Is Better Was a Law of the Carrier](https://logos-lang.dev/blog/worse-is-better-was-a-law-of-the-carrier/)).

### 2.3 Generalisation, not execution

A model is a distribution $p(y \mid x)$ trained for generalisation. Tasks split by compressibility ([A Model Is Not a Calculator](https://logos-lang.dev/blog/a-model-is-not-a-calculator/)):

| Task class | Short generator | Model | Error physics |
|---|---|---|---|
| translation, summarisation, idiomatic code | none | scale wins | many outputs acceptable; deviations damped |
| arithmetic, type checking, constraint solving, query planning | exists | approximates; scale does not close the gap | one correct output; deviations amplified downstream |

On the second class the model performs approximate execution: observationally exact in distribution, collapsing without an internal signal outside it ([Dziri et al. 2023](https://arxiv.org/abs/2305.18654)). Two rules follow:

- R1. Work that requires deterministic execution is offloaded to a symbolic component. Only symbolic components give reproducibility and verifiability.
- R2. Periodic work that can be offloaded is offloaded, asymptotically. Weights are the most expensive memory ever deployed.

Deterministic components have three roles: compute what the model cannot, cache what it should not recompute, constrain the trajectory of an agent loop. As models improve, the volume of deterministic code around them grows.

### 2.4 InD, near-OOD, far-OOD

Definitions used throughout the book:

- **InD**: the support of the model's training distribution (§2.1: descriptions).
- **near-OOD**: targets outside InD that are practically reachable from InD by derivation: a chain of steps, each in-distribution for the model, whose length fits the budget and the error bound. An unverified chain survives roughly $1/\varepsilon$ steps for per-step error rate $\varepsilon$; a verified step resets the error.
- **far-OOD**: everything else. Reaching it requires a source outside the model: new information in context (conditioning), training, or an improbable step kept by an external verifier (selection).

The boundary of near-OOD is relative. It is a function of four parameters:

$$\text{near-OOD}(M, S, F, B)$$

- $M$, the model: stronger reasoning lowers $\varepsilon$ and lengthens chains.
- $S$, the symbolic components available to the loop. Verifiers and reasoners are one category: both are offload. A reasoner's steps have $\varepsilon = 0$, so chain length is bounded by compute, not by $1/\varepsilon$; a reasoner has no probability bias, so it takes valid steps the model would find improbable. Offload lifts both the metric limit (error decay) and the topological limit (the model's "island" of probable steps) on the offloaded part.
- $F$, the foundations loaded in context: declarations, invariants, decision records. They shorten derivations.
- $B$, the budget: tokens, steps, wall time.

The measure of distance is conditional compression complexity, $C_M(t \mid F + \text{context})$: the length of the verified reasoning trace from the loaded foundations to target $t$ ([The Phase Transition](https://logos-lang.dev/blog/the-phase-transition/)). Development is a ratchet: derive one step past the known, verify it with $S$, consolidate the result into $F$, repeat. Each turn moves targets from far-OOD into near-OOD.

Under offload the model keeps the two ends of the chain: formalisation (state facts, rules, specifications) and interpretation of the result. Both must be InD or near-OOD for the model. Hence a reasoner's input language must be well described, and its output (derivations, justifications) must come back as data the model reads.

### 2.5 What the model cannot do from inside

Two limits are architectural ([The Model Cannot List What It Knows](https://logos-lang.dev/blog/the-model-cannot-list-what-it-knows/); [Worse Is Better Was a Law of the Carrier](https://logos-lang.dev/blog/worse-is-better-was-a-law-of-the-carrier/)).

- No enumeration. A model is a point-query engine: it evaluates a learned function at a point and has no operator that lists the function's extent. An implementation it generates covers the common paths and omits tail cases uniformly; tests it writes for that implementation share its blind spots; its self-review samples from the same distribution. Large models estimate the *amount* of their knowledge roughly correctly ([Prato et al. 2024](https://arxiv.org/abs/2502.19573)); the limit is on the completeness of what they list. The list of cases must come from outside: an external corpus, a reference implementation, a specification.
- No self-conditioning. Conditioning on one's own sample moves the distribution by zero in expectation: $\mathbb{E}\big[p(\cdot \mid \text{own sample})\big] = p(\cdot)$. Drift requires an information source outside the distribution: a human's constraint, a verifier's rejection, reality's answer.

### 2.6 The human as a complementary distribution

Viewed as a distribution, the human $H$ is complementary to the model $M$: the supports are built from different data (§2.1), overlap on the shared textual culture, and are largely disjoint beyond it. No quantitative estimate of the overlap exists. Consequences:

- The model's far-OOD is in part the human's InD, and the reverse. Humans work mainly where the model has nothing to derive from: judgment of what matters in the world, which invariants carry weight, what failure costs.
- Errors of $H$ and $M$ are weakly correlated, because the same failure modes run on different substrates and fire on different inputs ([The Failure Mode Humans Don't Have](https://logos-lang.dev/blog/the-failure-mode-humans-dont-have/)). Weakly correlated error is the raw material of a joint system that outperforms both members.
- Language is the channel through which the two distributions condition each other. Human text (constraints, specifications, decisions, reviews) supplies the model with the drift it cannot produce itself (§2.5). Model output (proposals, explanations, derivations) conditions the human in turn.
- Only the human carries responsibility, a legal fact, not a cognitive one. Responsibility requires comprehension, so comprehension is something the platform must supply ([One System, One Dynamics](https://logos-lang.dev/blog/one-system-one-dynamics/)). System throughput is bounded by the rate at which a human can responsibly own output.

### 2.7 Knowing a language; understanding AI

The human owner of Logos code is not assumed to know Logos, Rust, or Deem. First-hand example: the Synthea project uses Soufflé for ontological analysis; its owner reads the Datalog with outside help and does not write it.

Knowing a programming language becomes passive, assisted competence: reading with a dictionary, as with a foreign language. The model is the dictionary and the guide; the human checks the explanation against the text.

This is one case of a general skill: understanding AI, defined operationally as effective embedding into the chains through which tasks reach their fixpoints. A task is the joint iteration $X_{n+1} = F(X_n)$ of model, symbolic components and human ([Attraction Basins](https://logos-lang.dev/blog/attraction-basins/)). The human understands AI to the extent that the human's inputs (conditioning from far-OOD, judgments of adequacy, steering) make the composed map contract to the right fixpoint at the lowest cost in the human's attention. Reading a program with a model, reviewing, prompting, and choosing how much to verify by hand are instances.

## 3. Consequences for Logos

Each premise turns into a requirement and a mechanism. The last column names the chapter that describes the mechanism.

| Premise | Requirement | Mechanism in Logos | Chapter |
|---|---|---|---|
| asymmetry; complexity budget set by the model (§2.2) | no feature withheld for being hard for humans | type-level computation as ordinary Logos via [metacall](../../../stdlib/mem/compiler/metaprog/); structured const generics; type packs | 2.5 |
| model InD = described material (§2.1) | surface in the model's densest distribution | Rust surface and semantics as the bootstrap language; differences registered in [DIVERGENCES.md](../../DIVERGENCES.md) | 2.1 |
| offload widens near-OOD (§2.4) | reasoners inside the language, not beside it | [Deem](../../../stdlib/mem/wql/) as the declarative plane; the borrow checker as Datalog rules ([borrow_bir.inc](../../../src/compiler/borrow_bir.inc)); SMT and Lean proposed ([ADR 0037](../../adr/0037-specifications-and-smt.md), [ADR 0038](../../adr/0038-lean-integration.md)) | 2.3, 4.2-4.5, 7.3-7.4 |
| reasoner output must return as data (§2.4) | justifications and diagnostics as structured values | unmet demands reported as [`Pending`](../../../include/logos/compiler/lir.hpp) records; compiler facts as Deem relations ([ADR 0032](../../adr/0032-logos-core-composition.md) D6) | 3.1 |
| no enumeration from inside (§2.5) | the list of cases comes from outside | imported rustc test corpus ([tests/imported/](../../../tests/imported/)); Soufflé and C++ Memoria as oracles ([souffle_oracle_gate.sh](../../../tests/logos/souffle_oracle_gate.sh)); open defects as ledgers generated from issues | 1.3 |
| compiler as environment; no ego (§2.1, §2.2) | strictness beyond human tolerance | Datalog borrow checker as default; checks past the mainstream threshold (view invalidation over packed structures) | 2.3, 5.4 |
| reading with a dictionary (§2.7) | every construct explainable from visible text plus the spec | explicit `metacall` instead of implicit const evaluation; [spec](../../spec/) with evidence links into the compiler; this book pinned to code by `verified-at` | 2.5, 1.3 |
| coherent foundations and checks lower $C_M$ (§2.4) | one mechanism per concern; a fact stated once | one type system and one IR ladder per layer ([ADR 0032](../../adr/0032-logos-core-composition.md) D1, D2); a declaration stated once (D4) | 1.2, 3.2 |
| human owns what is checked, not what is read (§2.2, §2.6) | small, checkable points of human entry: intent, specifications, verdicts | declarations of intent (containers, `deem` items); effects and capabilities ([ADR 0035](../../adr/0035-effects.md), [ADR 0036](../../adr/0036-object-capabilities.md)); specifications ([ADR 0037](../../adr/0037-specifications-and-smt.md)) | 5.1, 7.1-7.3 |
| mutual conditioning through language (§2.6) | the language is precise for the model and readable as intent for the human | declarations, plans with grounds, specifications as the shared layer | 4.3, 7.3 |

Two corrections to earlier project texts follow from §2.7. The Rust surface is justified by model fluency; human readability is a secondary benefit for spot checks, not the design reason. And the human-facing value of formal specifications (Part VII) is ownership, not only correctness: a human who cannot read model-scale code can still judge whether a checked property is the right one.

## 4. Alternatives

| Alternative | Why not |
|---|---|
| "AI-first" as tooling around an unchanged language (assistant plugins) | leaves the defaults of §1.2 in place; the model gets prose feedback and rationed features |
| a language optimised for human writability | optimises an author who no longer writes; the human-side constraint is reading and owning, not writing |
| a dynamic language for interactivity | removes the early falsification channel (§2.2) and needs a second semantics; interactivity is a compile-speed property (REPL, incremental compilation); dynamic data lives in Writ documents |
| minimising coherence complexity $C$ (worse is better) | the selection pressure that justified it was the human carrier's fixed bandwidth (§2.2) |
| reasoners as external tools (an SMT solver called from scripts) | offload must be cheap and its output must return as data the model reads (§2.4); an external tool adds a translation seam per use |
| model self-review as the completeness check | self-review samples the same distribution as the code (§2.5) |

## 5. Evidence

Published:

- Unseen languages. An invented language absent from pretraining: syntax learned quickly; in 80 % of cases models choose the same algorithm as in Python and fail to express it ([Bannihatti Kumar et al. 2026](https://arxiv.org/abs/2605.15607)). Niche languages can perform like popular ones ([MultiPL-E, Cassano et al. 2022](https://arxiv.org/abs/2208.08227)).
- Checkers in the loop. Type-constrained decoding halves compile errors and raises functional correctness ([Mündler et al., PLDI 2025](https://arxiv.org/abs/2504.09246)). Idris: 22/56 exercises zero-shot, 54/56 with a compiler-error repair loop ([Li, Krishnamachari 2026](https://arxiv.org/abs/2602.11481)). An LLM-rustc loop fixes ≈74 % of real Rust compile errors ([Deligiannis et al. 2023](https://arxiv.org/abs/2308.05177)). In single-shot generation without a checker, static typing had no measurable effect ([MultiPL-E](https://arxiv.org/abs/2208.08227)): the effect is in the loop, not in the annotations.
- Verification languages. Off-the-shelf success 82 % in Dafny, 44 % in Verus, 27 % in Lean; Dafny 68 % → 96 % in one year ([Bursuc et al. 2025](https://arxiv.org/abs/2509.22908)).
- Offload of reasoning. Formalize-then-solve gains 65 points absolute on logic benchmarks where models collapse ([arXiv 2502.15776](https://arxiv.org/abs/2502.15776)); inference-time scaling does not lift the collapse ([arXiv 2502.01100](https://arxiv.org/abs/2502.01100)).
- Approximate execution. Accuracy collapses with computation-graph depth ([Dziri et al. 2023](https://arxiv.org/abs/2305.18654)).
- Uncaught tail gaps. 29.6 % of plausible SWE-bench patches behave differently from the ground truth; 7.8 % of accepted patches fail the developer tests ([Wang, Pradel, Liu 2025](https://arxiv.org/abs/2503.15223)).
- Self-knowledge. Reversal curse: facts learned in one direction are not retrieved in the other ([Berglund et al. 2023](https://arxiv.org/abs/2309.12288)); counterweight on amount-of-knowledge estimates: [Prato et al. 2024](https://arxiv.org/abs/2502.19573).

Observed in the project (not independently measured):

- Gap discovery against the imported rustc corpus follows the pattern of §2.5: a constant discovery rate while the corpus reaches new regions, falling depth of found gaps ([The Failure Mode Humans Don't Have](https://logos-lang.dev/blog/the-failure-mode-humans-dont-have/)). A rustc audit of 786 fixtures judged legal by reading: 752 confirmed, 6 contradicted, 23 divergences, 5 not expressible ([rustc-fixture-audit-2026-09.md](../../rustc-fixture-audit-2026-09.md)).
- Strictness beyond human tolerance is absorbed by models without loss of throughput.

Hypotheses without a published test:

- A controlled typed-vs-untyped comparison in an agentic loop on matched tasks does not exist; neither does an end-to-end evaluation of a general-purpose language designed for model authors. Logos is a candidate subject for both.

## 6. Status and open questions

- The premises are positions of the project as of 2026-10. §1.1 is an observation of practice; the 1000× figure in §2.2 is an estimate.
- Open: the trust chain of §2.7. A model renders a checked property and its justification into the human's language; the rendering is itself a model output. Mitigations: renderings by independent models (different distributions), examples and counterexamples generated by $S$, justifications as data that a rendering can be checked against. Mitigated, not removed.
- Open: a model of human performance in the joint loop (attention cost per task type, comprehension under load) barely exists; the human is the binding constraint ([Attention Is the Budget](https://logos-lang.dev/blog/attention-is-the-budget/)).
- To confirm: the formulation of the post-training caveat in §2.1.
