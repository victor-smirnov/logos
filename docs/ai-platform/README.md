# AI Platform Era

This section is about a single thesis: **AI models are becoming primary authors of code, and that changes what we should optimize languages and platforms for.** The way we built compilers, type systems, build tools, and "developer experience" assumes humans at the keyboard. That assumption no longer holds for a growing fraction of real work, and many of the resulting design defaults are now miscalibrated.

The section walks the argument in stages — from how models actually work, to what they do well and badly on coding tasks, to the *new* requirements platforms now have to meet, to where Logos sits against those requirements.

## Files

- [How Models Work](models.md) — the parts of model behavior that matter for platform design: context as the sole memory, attention and recall, the role of prompt structure, what cannot be assumed.
- [Coding Tasks](coding-tasks.md) — what changes when the task is code: the gap between local-syntactic competence and global-semantic correctness, failure modes that humans do not exhibit, the role of feedback signals.
- [New Requirements](requirements.md) — what platforms have to provide to make AI authorship work: machine-readable diagnostics, observable compilation, programmable extension, structured data substrate, fast and incremental feedback loops.
- [Logos Fit](logos-fit.md) — how Logos's design (Hermes-as-IR, modular SOA compiler, metaprogramming-in-the-large, lforge as a data platform) maps onto those requirements, and where it deliberately diverges from Rust/C++/Go defaults.

## Audience

These essays are for: language designers, tooling authors, engineering leaders deciding what to build on, and contributors who want to understand the *why* behind Logos's choices. They are not introductory material for Logos itself — see [language/overview.md](../language/overview.md) for that.
