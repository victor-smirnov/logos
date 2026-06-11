# AI Platform Era

Thesis: **AI models are becoming primary authors of code, which changes what we should optimize languages and platforms for.** Compilers, type systems, build tools, and "developer experience" were built assuming humans at the keyboard. That assumption no longer holds for a growing fraction of real work, and many design defaults are now miscalibrated. The section argues in stages: how models work → what they do well and badly on code → the *new* platform requirements → where Logos sits against them.

## The Two-User Framing

Logos's shape — *paradigmatically C++, surface-syntactically Rust* — falls out of accepting that the language has *two* primary users with asymmetric strengths:

- **Models are the primary author for substantive work** — algorithms, type systems, large-scale generation, formal logic. Their structural weakness is *executing* deterministic procedures (arithmetic, type checking, B-tree rebalancing, query planning); the language gives them maximal expressivity and rich offload targets so that work moves out of the weights into a substrate that executes deterministically. That is the C++-paradigm side: arbitrary structured const-generic values, first-class type packs, type-level computation as ordinary metafunctions, generic Drop with proper substitution, registry-driven dispatch keyed by content-hash type identity. *Maximalist by design* — the alternative, *premature passivisation* (filing extreme features behind RFCs because junior humans find them confusing), taxes the model for no benefit it needs.

- **Humans remain the responsibility-holding layer.** They own values: which invariants matter, what failure modes mean in the world, whether shipping is right given non-technical context. They supply the low-compressible side — judgment, taste, ethics, accountability — where models have no training-data leverage, plus novel-domain reasoning over unseen facts. *Operational closeness to the code* is what lets them oversee at scale: read, review, intervene, set policy. That is the Rust-skin side: `let mut`, `&mut self`, `match`, traits, ownership, no exceptions — familiar, low-entropy, modern-systems-flavoured.

Strip the C++ depth and the model hits a metaprogramming ceiling on every database-class problem. Strip the Rust skin and humans cannot oversee output at scale. Both layers exist because both users exist; the language composes their asymmetric strengths rather than flattening them onto one notional user. This drives the counterintuitive bottom line: **as models get more capable, the volume of conventional, deterministic, type-disciplined code wrapping them grows.** Models are not replacing the scaffolding — they are *generating* it, in the language that makes it cheap to produce, audit, and compose.

## Files

- [How Models Behave](models.md) — facts about LLMs in isolation, two lenses. *Dynamical:* iterated-map dynamics, attraction basins, InD vs OoD. *Information-theoretic:* compressibility-split tasks, the determinism gap, deterministic components as offload/cache/guardrail, model memory as a compressed forward-only program (point-query-only, non-invertible, non-self-enumerable). Plus what cannot be assumed.
- [Models, Humans, and Programs as One System](joint-system.md) — joint-system consequences: mutual steering, the responsibility asymmetry, the ownership spectrum, why AI-primary is self-reinforcing, the human as binding constraint, the engaged human's irreplaceability and why AI needs humans, leadership notes on attention as the scarce resource, the platform's two goals.
- [Coding Tasks](coding-tasks.md) — what changes when the task is code: uniform unknown-unknown gaps, local-syntactic vs global-semantic correctness, the failure mode humans lack, and the forced methodology (external corpus as mandatory index, gap-discovery dynamics, instance-coverage vs class-generality and the generalizing pass), plus how Logos materializes it. The bridge from model-behavior to methodology.
- [New Requirements](requirements.md) — what platforms must provide: machine-readable diagnostics, observable compilation, programmable extension, structured data substrate, fast incremental feedback loops.
- [Logos Fit](logos-fit.md) — how Logos's design (Hermes-as-IR, modular SOA compiler, metaprogramming-in-the-large, lforge as a data platform) maps onto those requirements, and where it deliberately diverges from Rust/C++/Go defaults.

## Audience

Language designers, tooling authors, engineering leaders choosing what to build on, and contributors wanting the *why* behind Logos's choices. Not introductory material for Logos itself — see [language/overview.md](../language/overview.md).
