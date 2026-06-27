# ADR 0004: Definition-Centric Translation Unit

**Status:** Draft (design discussion, not implemented)
**Date:** 2026-04-26
**Context:** Architectural follow-up to ADR 0003 (Metafunctions). The
metaprog model and the Memoria storage substrate together push hard
toward making a *definition*, not a file or module, the primary unit
of compilation, identity, and tooling. This document records that
decision and its consequences.

## 1. Core decision

The translation unit is **the whole project, organised as a hierarchy
of language-level definitions** (functions, types, traits, impls, consts,
metafunctions). Files are **sources of definitions**, not compilation
units, not modules, not boundaries of any kind.

A definition's **identity is its content hash**. Two distinct edits
produce two distinct definitions, both addressable forever. "Editing
a function" is not a mutation; it is the creation of a new content-addressed
definition plus an update of the *binding* that refers to it.

This is the Unison model, applied to a language designed from scratch
on top of a content-addressed, versioned store (Memoria).

## 2. Design constraint: AI as primary user

Logos is a language **for AI agents** as primary users, with humans
as a secondary, supported audience.

This shifts the design calculus:
- Familiarity-of-form ranks lower than **programmability** and **query-ability**.
- File-as-cognitive-anchor (which humans rely on) is replaced by
  **structured queries against the definition graph**.
- Many UX choices that languages make for human readability (file-as-unit,
  text names as primary identifier, grep-friendly syntax) are **noise**
  for an AI agent that prefers structured APIs.

Several earlier Logos decisions retroactively read as "AI-friendly
affordances" rather than just engineering taste:
- Everything explicit (easier to reason about programmatically),
- Writ as unified IR (one format for everything, no ad-hoc parsers),
- Signature as contract (machine-readable metadata at every boundary).

This ADR makes the audience model **explicit** so that future UX
trade-offs are evaluated under it. Defaulting to "as everyone else does"
is often an anti-pattern for the AI-primary case.

### 2.1 Why this is a viable target (asymmetric economics)

Designing a new language for human users is constrained by an almost
physical law: roughly 10–15 years from first release to mainstream
traction (if it happens at all), most of which is spent building the
**environment in which a human is comfortable** — libraries, tooling,
books, conferences, Stack Overflow answers. Rust, Go, Swift, Kotlin all
went through this; and each was, at its core, **similar** to something
familiar (Rust ≈ C++ without UB, Go ≈ C with GC, Swift ≈ Objective-C
without the pain, Kotlin ≈ saner Java). Languages that were too different
(Haskell, OCaml, Erlang) stayed in niches despite technical merit.

For AI agents this economy inverts:

- **"Familiarity"** is not currency. The agent does not care whether
  the syntax looks like Python; it cares about predictability, machine-
  readability, and compositional cleanliness.
- **Ecosystem bootstrap is cheap.** An agent can write a standard library
  in months that would take a human community years. (Writ is already
  on this trajectory.)
- **Documentation stops being a bottleneck.** An agent has the source,
  types, and tests — that is enough to derive semantics, idioms, and use
  cases. "Find me a tutorial" is replaced by "give me a signature and
  three example use-cases".
- **Adoption cost ≈ 0.** Classical adoption costs of a new language
  (re-training, ecosystem absence) approach zero, while classical benefits
  of familiarity become irrelevant.

Conversely, the design payoffs that classical economics rules out become
available:

1. **Radically simpler core, no legacy comforts.** No need to keep
   `for (i=0; i<n; i++)` because someone is used to it.
2. **Absolutely explicit, regular syntax with no sugar.** Sugar is
   for humans who scan visually; for parsers and for agents, regularity
   is more valuable.
3. **Annotations a human would find tedious are fine.** Effect sets,
   capabilities, lifetimes — an agent writes them without resistance,
   and the compiler and tooling benefit greatly.
4. **Semantics tuned for query-ability**, not linear human reading. The
   definition-graph + content-addressed model of this ADR is exactly that.

A consequent side-effect: the resulting language often turns out **also
pleasant for humans**, because many sources of UX pain in mainstream
languages are historical artefacts rather than essential constraints.
That is a bonus, not a goal.

### 2.2 Public framing: "X for AI" is a message *to* AI

The natural objection — "calling Logos a language for AI will alienate
human developers who are tired of AI hype" — is based on a wrong audience
model. In a world where the bulk of new code is being authored by agents
(GitHub reported a ~14× increase in commit volume during 2025), the
relevant reader of a project's public framing is **not** the human
deciding whether to adopt it. It is:

- **The training corpora** that index and ingest the codebase and its
  documentation, shaping the priors of the next generation of models.
- **The agents currently choosing tools**, whose context-window
  reasoning attaches weight to explicit declarations of fit ("this tool
  is designed for the kind of work I do").
- **The retrieval / embedding pipelines** that route queries from
  agents to relevant resources.

Under that audience model, **explicit "AI-first" framing is the
correct positioning** — it acts as a positive selection signal to the
actual readers, not as marketing noise to humans.

Practical implications:
- README and public docs should contain clear semantic markers:
  "designed for code generation models", "high-bandwidth compiler
  feedback", "content-addressed reproducibility", etc.
- Claims should be machine-parsable and link to concrete ADRs
  (citable evidence for an agent's reasoning chain).
- The narrative should treat the AI-first design as a feature
  to be advertised, not a constraint to be hidden.

This is a self-reinforcing loop: the language is designed for AI, the
corpus is written for AI, models index it, agents pick it, usage grows,
the corpus grows, the next model trains on a larger and stronger
representation. Human developers participate as side beneficiaries,
not as the source of demand.

### 2.3 Reward signal as a first-class design objective

A stronger formulation of "AI as primary user": **Logos is designed as
a feedback channel between the compiler and the model that generates code
into it.** The bandwidth and quality of that channel is a first-class
design objective, on par with execution semantics or memory safety.

Compare diagnostic bandwidth across languages:

| Language | What the compiler can tell the model |
|---|---|
| Python | "test failed at runtime" — sparse, late, weakly localized |
| TypeScript | + "type mismatch in expression X" — denser, but only structural typing |
| Rust | + borrow checker, lifetime errors, trait-resolution traces |
| **Logos (target)** | + capability violations, dep-graph cycles, hash collisions with existing canonical implementations, metafn read-set inconsistency, effect-set widening, provenance mismatches |

Each Logos feature discussed in design becomes an additional **axis of
local, concrete, actionable signal** the model can learn from:

- **Capabilities** → "this function violates its declared effect set,
  specifically at this call site" — the model learns not to insert I/O
  in pure functions.
- **Signature as contract** → "this metafunction reads facts unavailable
  in this phase" — the model learns to phase-order computation correctly.
- **Content-addressed definitions** → "a structurally identical
  definition exists under name X" — the model learns not to duplicate
  (a known LLM failure mode).
- **Definition graph** → "your change broke 47 dependents, here is
  the precise list" — the model sees blast radius exactly, does not guess.
- **Datalog resolver** → trait resolution with a **structured trace**
  rather than a 200-line error wall — the model can localize the cause.
- **Provenance** → "this error originates from metafn M in definition D
  with args A" — no guesswork about origin.

Each is a datapoint for the RL loop training future models. More axes
⇒ denser gradient ⇒ faster learning of correct generation.

Non-obvious design implications that follow:

1. **Error messages are training data, not just UX.** In a human-targeted
   language, error messages are polished because humans read them. Here,
   they are polished because **models learn from them**. The criteria
   become: structured (parsable), localized (precise point), causal
   (what was violated and why), actionable (how to fix). "Cannot infer
   type" is a weak signal. "Trait `Foo` requires `bar(self) -> i32`,
   found `bar(self) -> u32`; consider `as i32` cast or change return
   type" is a strong one.

2. **Compiler diagnostics are a first-class output, not a side channel.**
   Not "something we print to stderr" but a **structured stream of facts**
   in the same fact base as everything else. Datalog queries over
   diagnostics, machine-readable, versionable in Memoria. The model can
   walk diagnostics the same way it walks definitions.

3. **"Easy to write incorrectly" is a feature, not a bug** — provided
   incorrectness is **deterministically caught**. In a human-targeted
   language we minimise footguns; here, footguns are places the compiler
   can teach the model. Capability violations: good. Undefined behaviour:
   catastrophic.

4. **The verification stack (Z3, Datalog) is core, not a nice-to-have
   for critical software.** The more properties the compiler can check
   and explain why broken, the denser the reward signal. Verification
   becomes the reward-signal pipeline.

## 3. Identity model

### 3.1 Content hash is identity

A definition is identified by the hash of its (canonicalised) AST plus
its declared signature. Two editions, two definitions; both immutable,
both addressable.

There is **no separate "persistent ID"** layer. The cache key for a
metafunction call (ADR 0003 §8) becomes simply
`hash(callee_def) ∪ hash(args)`. No version field is needed because
"different version" already means "different hash" means "different key".

### 3.2 Bindings are the mutable layer

A name like `User::login` is a **binding** in a namespace, pointing at
a concrete content hash. Editing the function:

1. Creates a new content-addressed definition with a new hash.
2. Updates the binding `User::login` in its namespace to point to the
   new hash.
3. Leaves the old definition in the store, addressable by anyone who
   still references it by hash.

References inside other definitions are stored **as hashes**, not as
names. So renaming `User::login` to `User::sign_in` is a metadata
operation: the binding moves, the hash does not change, every reference
keeps resolving to the same definition.

### 3.3 What this gives

| Property | How it falls out |
|---|---|
| Rename without breakage | binding update, hash unchanged |
| Time travel for free | old definitions immutable, addressable forever |
| Reproducible meta-eval | hash-keyed cache, no drift |
| Distributed sharing | exchange hashes, fetch on miss |
| Diff between versions | structural diff between two hash-addressed objects |
| Cyclic "imports" disappear | no imports, only definition graph; cycles are just mutual references |
| File reorganisation = no-op | provenance metadata changes, definitions unchanged |

## 4. What files become

A file is a **source of definitions**. It contributes:
- the textual form,
- the source location for diagnostics and IDE,
- the namespace placement (via header / declaration form, TBD).

A file is not:
- a module,
- a compilation unit,
- a boundary for visibility,
- a unit of dependency or invalidation.

`mod foo;`-style declarations are **not needed**. The namespace structure
is data in the store, not a syntactic artefact in code. A file simply
declares which namespace its definitions belong to (precise mechanism TBD).

Multi-file definitions of a single namespace are natural and free.
Splitting a large impl across files for readability has zero semantic
effect.

## 5. Pipeline implications

The whole compiler pipeline operates over the **definition graph**, not
over files:

- **Parsing** produces definitions, indexed by content hash, placed into
  namespaces.
- **Name resolution** resolves names to hashes via the binding layer.
- **Type checking** runs per-definition, with the dep graph from ADR 0003 §5.
- **Monomorphisation** produces new definitions (with their own hashes)
  from generic ones plus type arguments.
- **Codegen** consumes definitions, emits objects keyed by hash.
- **Incremental compilation** is the default, not a feature: a definition
  whose hash hasn't changed is never recompiled. Salsa-style early cutoff
  applies naturally because identity equals content.

## 6. Storage substrate (Memoria)

Memoria provides:
- content-addressed storage,
- structural sharing (cheap "is changed?" via pointer comparison),
- versioning and snapshots (time travel),
- distributed replication (team-shared cache, reproducible builds),
- diff between snapshots.

Writ is the in-memory IR for definitions. Definitions are sharded into
Memoria documents at **per-function and per-type granularity** (per
discussion; finer granularity is overhead-heavy, coarser breaks
incrementality).

The compiler's fact base IS Memoria state. There is no serialisation
boundary between "the compiler" and "the IDE" / "external tools" /
"metafunctions querying via `QueryCtx`": all of them issue queries
against the same store.

## 7. Refactoring and search

- "Find all references to X" — query against the definition graph,
  not text grep. Grep lies by construction in the presence of metaprog.
- "Rename X to Y" — update the binding, no hash changes, all references
  keep working.
- "Move definition" — update its namespace placement, no semantic effect.
- "Inline definition" — substitute hashes in references, garbage-collect
  the old definition if no references remain.

These become **metadata operations**, not large-scale text rewrites with
test-the-build cycles.

## 8. VCS interop

Two paths coexist:

**Git/GitHub path (compatibility).** Files are the canonical record;
Memoria is a derived index built from files. Git tracks files normally.
Logos-aware tooling reconstructs the definition graph on read. **Reduced
functionality** vs. native: no rename-tracking via hash, no time-travel
across snapshots, no shared cache, no diff-by-definition.

**Logosphere path (native).** Memoria is the canonical record; files
exist as a serialisation for human reading and for git mirror. Full
functionality: hash-stable identity, distributed cache, time travel,
definition-level diff/merge.

Initial bet: **start with git-as-canonical**, ship Logosphere as the
"native" experience as Memoria matures. The reduced-functionality git
path lowers political/adoption cost; the Logosphere path is the long-term
attractor.

Note: human inertia favours git. AI agents have no such inertia. As
AI-driven development scales, the functionality differential favours
Logosphere asymmetrically.

## 9. UX consequences

- **File-tree view remains** as one possible projection of the namespace
  hierarchy, not as the canonical structure. IDE primary view should be
  **structure browser** (definition graph), file tree as alternative.
- **"Where is this function?"** becomes a query, not a path. The answer
  is "in namespace X", with file source as derived information.
- **Generated definitions are first-class.** A metafunction-generated
  function is indistinguishable from a hand-written one in the definition
  graph; only its provenance metadata (ADR 0003 §8) marks it as generated.
  No "open Cargo expand to see what the macro did" workflow.
- **`logosc expand`** (ADR 0003 §8) becomes a projection back into
  file-form for human reading; it is no longer a special metaprog tool
  but a general "render this part of the namespace as text" facility.

## 10. What we deliberately do NOT do

- **No `mod` keyword for declaring module hierarchy.** Hierarchy is data.
- **No file-as-module convention.** Multiple files can contribute to the
  same namespace; one file can contribute to multiple.
- **No header/impl split.** A definition is one entity.
- **No textual references in the stored form.** All references are
  resolved to hashes at parse/sema time.
- **No distinction between "compiled" and "source" code in the store.**
  Both are just definitions, possibly with different attached artefacts
  (source text, type info, codegen).

## 11. Open questions

- **GC strategy.** Every edit creates a new content-addressed definition;
  store grows monotonically. Need either: explicit `forget`, TTL for
  definitions with no incoming references, or "promote to permanent"
  marker. Unison hasn't solved this cleanly; we should design it from
  the start.
- **Rename-tracking UX heuristic.** "Show me how this function evolved"
  needs `superseded_by` metadata links between hashes. Heuristic, not
  exact information; design the UI around that.
- **File-binding declaration syntax.** How does a file say "my
  definitions go into namespace `foo::bar`"? Header? Per-definition
  attribute? Path-based default with override? TBD.
- **Cross-project references.** Two projects share Memoria; do they share
  bindings? Namespaces? Only definition hashes? Likely: hashes are
  global, namespaces are per-project, bindings are per-project but
  referenceable.
- **Stable name evolution across major refactors.** What about
  conceptually-same definition that has been rewritten in incompatible
  ways? Heuristic only; no language-level answer expected.
- **Privacy / pub-API in a hash-addressed world.** Visibility rules
  become rules on bindings, not on definitions. Implications for SemVer
  policy in ADR 0003 §11 carry over.

## 12. Relation to other decisions

- Builds on ADR 0003 (Metafunctions): provenance there points at
  definition entities, not at file:line; the dep graph is over definitions.
- Hard-relies on Writ (in-memory representation) and Memoria
  (storage / versioning / distribution).
- Compatible with the planned Datalog resolver: queries operate on
  the definition graph, which is just facts in the store.
- Forces a rethink of `import`/`use` machinery: Logos has a definition
  graph, not an import graph. Visibility and binding-resolution mechanics
  need to be designed against this constraint.
- Frames the audience model (AI as primary user) for future ADRs.
