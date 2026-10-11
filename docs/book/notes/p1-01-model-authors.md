# Notes: 1.1 A language for model authors

Raw material for the chapter. Positions are Victor's unless marked; each with its date and where it was stated. Not part of the book.

## Positions already stated (seeded from session memory, 2026-10-10)

- 2026-10-10: "Пока мы делали Logos, ручное программирование потеряло смысл." Logos is a language for model authors. Motivation: models write well in low-resource languages; their code is locally better than human code (compiler-generated SSE vs hand asm analogy); models take tasks of a complexity humans no longer take on economically and need more powerful languages than ones built for a human at a workstation.
- 2026-10-10: humans still read and write code in far OOD (where models are out of distribution); in InD and near OOD hand work no longer pays. Review goes through models; only critical code (payments, security) gets manual verification on top of formal verification. Large code volumes are hard to understand in any syntax, so "readable syntax" does not rescue human comprehension at scale.
- 2026-06-27 (superseded 10-10): Logos was not to be framed as AI-first; LLM-fit "coincidental". Still true from then: the Rust-like surface was chosen by the model during design.
- 2026-07-13: the compiler is a sensor for a model, not an obstacle. A rejection is a cheap external falsification signal. Humans take rejection personally, and that tolerance ceiling shaped the industry (dynamic languages, error-message diplomacy, gradual typing). Models lack it constitutively; their avoidance behaviours are mimicry, correctable by harness. Consequence: checkers may be stricter than any human population would accept; verification density is a productivity multiplier.
- 2026-08-06: dynamic Logos rejected. Reasons: the stack bets on static derivability (plans, justification derived); strictness is the sensor; a second semantics is a span-killer. Interactivity = compile speed (REPL + incremental JIT); runtime-shaped data = Writ documents; ad-hoc queries = Deem; one boundary dynamic type over Writ.
- 2026-08-11: Rust 1.93 was a starting base, never a compatibility target. Rust's disease: enumerations of "but-if" reasons on a prematurely stabilised base instead of derivation. Direction: re-derive from a small logical core, spec in Deem, semantics simplified, syntax nearly kept.
- 2026-07-11: Rust semantics = surface profile kept for model fluency; semantic core = judgements over a registry of reasoners. "A noticeable part of rustc simulates a missing reasoner" (query system, trait solver, Polonius), as half of C++ Memoria simulated metaprogramming.
- 2026-07-10 (ADR 0018): "Logos IS Memoria in the form of a programming language."
- 2026-10-10 (ADR 0032): Logos Core = Rust + Deem + Writ + Hest; Memoria and Canon periphery. Human reads intent: declarations, plans, justifications, specifications.
- 2026-10-10: every "practical task" sits on a relational-graph model; tasks that do not fit are rare and get rarer on non-coherent hardware. Coherent multithreading stays in Rust-core; without coherence, distributed Datalog (CALM).

## Open questions for the discussion

See the chat of 2026-10-10; answers go below with dates.

## Discussion
