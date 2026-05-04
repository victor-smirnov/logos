# AI Platform Era

This section is about a single thesis: **AI models are becoming primary authors of code, and that changes what we should optimize languages and platforms for.** The way we built compilers, type systems, build tools, and "developer experience" assumes humans at the keyboard. That assumption no longer holds for a growing fraction of real work, and many of the resulting design defaults are now miscalibrated.

The section walks the argument in stages — from how models actually work, to what they do well and badly on coding tasks, to the *new* requirements platforms now have to meet, to where Logos sits against those requirements.

## The Two-User Framing

Logos's shape — *paradigmatically C++, surface-syntactically Rust* — is not aesthetic. It is the falls-out-of-it conclusion when you accept that the language has *two* primary users with asymmetric strengths:

- **Models are the primary author for the substantive work** — algorithms, type systems, large-scale code generation, formal logic. Their structural weakness is *executing* deterministic procedures (arithmetic, type checking, B-tree rebalancing, query planning); the language's job is to give them maximal expressivity and rich offload targets so that work moves out of the model's weights and into a substrate that *does* execute deterministically. That is the C++-paradigm side: arbitrary structured const-generic values, first-class type packs, type-level computation as ordinary metafunctions, generic Drop with proper substitution, registry-driven dispatch keyed by content-hash type identity. *Maximalist by design*, because the alternative — *premature passivisation*, the mainstream-language tendency to file extreme features behind RFCs because junior humans find them confusing — taxes the model for no benefit it needs.

- **Humans remain the responsibility-holding layer.** They own values: which invariants matter, what the failure modes mean in the world, whether shipping is the right call given non-technical context. They fill in the low-compressible side of decisions — judgment, taste, ethics, accountability — where models have no training-data leverage and no in-weights summary. They also handle novel-domain reasoning where the relevant facts haven't been seen before. *Operational closeness to the code* is what lets humans do this oversight effectively at scale: they have to read it, review it, intervene in it, and set policy through it. That is the Rust-skin side: `let mut`, `&mut self`, `match`, traits, ownership, no exceptions. Familiar, low-entropy, modern-systems-flavoured.

Strip the C++ depth and the model hits a metaprogramming ceiling on every database-class problem. Strip the Rust skin and humans cannot effectively oversee output at scale. Both layers exist because both users exist; the two users have *asymmetric strengths* and the language is shaped to compose them rather than to flatten them onto a single notional user.

This framing recurs throughout the section. It also drives the (initially counterintuitive) bottom line: **as models get more capable, the volume of conventional, deterministic, type-disciplined code wrapping them grows**. Models are not replacing the surrounding scaffolding — they are *generating* it, and the language they generate it in is the one that makes the scaffolding cheap to produce, audit, and compose.

## Files

- [How Models Behave](models.md) — facts about LLMs in isolation: compressibility-split tasks, the determinism gap, deterministic components as offload / cache / guardrail, iterated-map dynamics, attraction basins, in-distribution vs out-of-distribution behavior, what cannot be assumed.
- [Models, Humans, and Programs as One System](joint-system.md) — the joint-system consequences: mutual steering, the responsibility asymmetry, the spectrum of ownership, why AI-primary is self-reinforcing, the human as binding constraint, the engaged human's irreplaceability and why AI needs humans, leadership notes on attention as the scarce resource, and the platform's two fundamental goals.
- [Coding Tasks](coding-tasks.md) — what changes when the task is code: the gap between local-syntactic competence and global-semantic correctness, failure modes that humans do not exhibit, the role of feedback signals.
- [New Requirements](requirements.md) — what platforms have to provide to make AI authorship work: machine-readable diagnostics, observable compilation, programmable extension, structured data substrate, fast and incremental feedback loops.
- [Logos Fit](logos-fit.md) — how Logos's design (Hermes-as-IR, modular SOA compiler, metaprogramming-in-the-large, lforge as a data platform) maps onto those requirements, and where it deliberately diverges from Rust/C++/Go defaults.

## Audience

These essays are for: language designers, tooling authors, engineering leaders deciding what to build on, and contributors who want to understand the *why* behind Logos's choices. They are not introductory material for Logos itself — see [language/overview.md](../language/overview.md) for that.
