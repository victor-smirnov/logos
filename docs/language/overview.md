# Overview

Logos is a statically typed, compiled, systems-level language. It produces native binaries via LLVM/MLIR and has its own compiler (`logosc`), standard library (`stdlib/std`), and runtime (`stdlib/rt`). It descends from ideas in the [Memoria Framework](https://github.com/victor-smirnov/memoria) but is a standalone language platform, not a C++ framework layer.

Logos and Memoria are co-developed: Memoria will eventually be written *in* Logos, and Logos is Memoria's first and primary user, exercising its data, metaprogramming, and runtime surfaces from day one. Already partial: Writ was designed inside Memoria and ported into Logos as the data substrate; other components follow as the toolchain matures.

## Design Axes

Logos is shaped by a few opinions, in rough priority order:

1. **Simplicity and clarity, Rust-inherited.** Logos is built on Rust and inherits its ergonomics: linear control flow, explicit names, no C++-style template metaprogramming (SFINAE/CRTP), no magical dispatch, no hidden conversions, a strong test culture. This makes it *relatively* well-suited to LLM authorship — but that fit is coincidental, a side effect of Rust's low-entropy, conventional surface, not a goal Logos was designed around. Where Logos extends the language beyond Rust, it holds to the same principle of simplicity and clarity, within reasonable limits. **Human readability stays a first-class priority**: people keep a substantial role in the programming process (review, redirection, ownership), so the code must remain readable and writable for them.
2. **Code and data unified.** Writ — a relocatable tagged object graph format — is *built into the language*. `@{...}`/`@[...]` are grammar literals; capture (`$ident`, `${expr}`) is type-checked at sema; view types carry real lifetimes through the borrow checker; module-scope literals fold to rodata as `WritStatic`. No DSL, macro, or FFI boundary between Logos values and Writ data — a document is just a value. See [Writ in Logos](writ.md).
3. **Systems-level performance.** Ownership, borrowing, monomorphized generics, AOT native codegen, explicit memory control — closer to Rust than Go or Swift. The memory mechanisms Memoria needs (relocatable object graphs, in-pointer constants, fat references, zone-scoped mutability) are lifted into the base type system rather than living as a library atop an unaware compiler, so the optimiser reasons about them as facts.
4. **Compile-time metaprogramming as a load-bearing layer.** Metafunctions are ordinary Logos code running at compile time through compiler APIs. Type-level computation is expressed as ordinary metafunctions, not a separate template language: C++20/23-style template *expressiveness* is the lesson taken, type-level programming as the way to wield it is rejected. The build system is itself a data platform (Datalog query engine, abstraction layering, large-data support), not a `cc` driver. See [Metaprogramming](reference/metaprog.md).
5. **Convergent computation models.** Conventional control flow is one model; production systems (forward/backward-chaining rules) and dataflow (digital-circuit-style graphs) are slated for first-class integration — one toolchain over systems programming, data processing, application development, and digital-circuit design, composing cleanly.
6. **Verification orientation.** Diagnostics, runtime tracing, and a large executable test suite (~5,750 tests: ~5,080 positive, ~630 diagnostic) are core deliverables. The type system is strengthened in directions that improve the reward signal for model-driven authorship; adjacent tooling (static analysers, type-aware lints, formal-property checkers) gets first-class attention.
7. **Pragmatic interop.** Logos calls C/C++ for FFI and links against C++-written LLVM/MLIR/Writ implementations, but Logos source is the primary programming model. No stable C++ AST interop layer.

## Paradigm and Skin: C++ Ambition, Rust Ergonomics

**Paradigmatically Logos is C++** — a *maximalist* systems language that does not pre-compromise expressiveness for a gentler learning curve. Arbitrary structured const-generic values (WritStatic), variadic generics with first-class type packs, type-level computation through ordinary compile-time metafunctions, deep-path slot extraction, registry-driven dispatch keyed by content-hash type identity, generic Drop with proper substitution — the features the C++ committee partly delivered after thirty years, here as load-bearing primitives rather than later additions wedged into a system not designed for them. The `1-2%` of programming where the language genuinely matters (databases, kernels, scientific computing, persistent storage engines, formally verified components) is what Logos is shaped by, not the mainstream "easy and safe" 98%.

**Surface-syntactically Logos is Rust** — `let mut`, `&mut self`, `match`, traits, monomorphized generics, ownership and borrowing, no exceptions. Not sentimental: it is the syntax human systems programmers already read fluently, readable and writable for reviewers/owners without C++-style obscurity (template specialisation, SFINAE, header-template-stamping, ABI archaeology). Being statically typed, low-entropy on character, and verbose-but-conventional, it also happens to be syntax LLMs generate reliably. That fit is coincidental — a side effect of inheriting Rust, not a goal the surface was bent toward.

By *target user*:

- **Models are the primary author** for serious systems software. Their structural strengths are the high-compressible side (algorithms, type systems, formal logic, large-scale generation); their weakness is the deterministic side (they cannot reliably *execute* the algorithms they understand). The offload target is exactly what Logos provides: explicit metaprogramming, verifiable type-level computation, deterministic codegen, persistent data structures. The C++ paradigm minimises the work the model does implicitly inside its weights — every load-bearing capability is a first-class construct it can name, manipulate, verify, reason about. *Premature passivisation* (filing extreme features behind RFCs/macros because they confuse junior humans) is a tax the model does not need.
- **Humans remain the responsibility-holding layer.** A model can author a kernel storage engine but cannot bear ownership of what it *means*: which invariants matter, who is harmed by which failure mode, whether shipping is right given non-technical context. Those calls — the low-compressible side of decision-making (judgment, taste, ethics, accountability) — stay with humans, who also fill in novel-domain reasoning, creative framing, value-laden trade-offs. Rust-shaped surface keeps them operationally close to the code without being C++ template savants.

Synthesis: **C++-paradigm depth so the model can write serious systems code; Rust-skin readability so the human can review, redirect, and own the result.** Strip the C++ depth → a tutorial-grade language whose models hit a metaprogramming ceiling on every database-class problem. Strip the Rust skin → an internal-only language whose humans cannot oversee model output at scale. Both layers exist because both users exist, with asymmetric strengths the language composes rather than flattens. This is structural, not marketing: when the primary author shifts human→model, the design constraints realign — features deferred as "too advanced" for human juniors are the ones models need most, while "human readability" now means *reviewer*, not *author*. For the deeper argument see the [AI Platform Era](../ai-platform/README.md) essays.

## What AI Authorship Actually Changes

The discourse around AI and programming has overshot in both directions: first broad skepticism that models would *ever* replace programmers; then alarm that they already had, wholesale; then the correction that they had replaced *some*, not all; then the further correction that even where they replaced, they did it poorly. The settling expectation inverts the panic — *more* programmers will be needed, not fewer: both to clean up after code generated on autopilot, and because the frontier of what is worth building widens precisely because models *can* now build it.

That models *can* write serious programs is no longer in question, and this project is direct evidence. Logos is a full-featured compiler for **Rust 1.93** — with non-trivial extensions for Memoria — built in roughly **three months** under *mediated* human ownership: a person directing and owning the result without writing most of the code. Rust's own sources were never used as an oracle; only its test-suite behaviour (conformance) and standard library were. And Logos is no Rust clone internally — it diverges sharply, a derivative of a different target objective (Writ-unified data, the zone memory model, load-bearing metaprogramming).

The work also mapped where models are strong and where they are weak (details: [Coding Tasks](../ai-platform/coding-tasks.md)). In short: the weaknesses can be *partially* compensated, not erased. Human participation is still assumed and still required; fully autonomous development remains impossible — not only normatively (someone must own what the code *means*) but technically (models cannot yet carry the whole loop alone). What has already changed is the *cost surface*: capable models create a new profile of complexity and effort, making feasible work that was, in practice, very nearly impossible before.

The shift is as much economic as technical. Decades of programming — corporate and open-source alike — have accumulated a deep reservoir of dissatisfaction, with both the process and its output, that the old cost structure left little room to act on: people build what they can, what they enjoy, and what someone will pay for. Subsidised individual subscriptions change the arithmetic. Far more becomes feasible to attempt *for its own sake*, because within the span of one person's motivation a model now covers far more ground — in depth and in breadth — than that person could alone. A heterodox individual with unconventional ideas, paired with a capable model, is a kind of force that did not exist before: ideas one person could never have materialised now can be — for as long as the subsidised economics hold. Logos is one such idea, materialised.

This releases at least part of that pent-up dissatisfaction: far more people get to have the program they *need*, rather than the one its authors found *pleasant* to write. The authors deserve real gratitude — the alternative to software written for the love of it is a landscape of nothing but invasive, audited, penalty-laden EULAs. But software progress is, in the end, steered by user *needs*, and that has to be faced squarely: when those needs can be met a different way, the way to stay relevant is to join the shift, not resist it. Nothing personal — just the real order of things.

## Three Premises Behind the Design

Every design decision in Logos traces, one way or another, to three premises:

1. **LLMs are slow and unreliable executors.** Whatever needs determinism or speed must be lifted out of the model and into a program — *execution offload*. The consequence is *very many* programs: a shift from programming-in-the-large to **programming-in-the-very-large**.
2. **Human ownership of code trends toward mediation through AI** — as far as that still meets the reliability and correctness goals. Little code gets written directly by hand; the priority shifts to optimising everything *for reading* and for analysis. That is a different ergonomic target than human authorship, and it gives strict, expressive type systems a strong impulse to develop.
3. **The accumulated codebase is no longer the brake it has been.** Legacy mass held change back because rewriting was expensive; when generation is cheap, modernising and consolidating the existing corpus becomes a natural background process rather than a blocking cost.

## Genos: the Third Layer of Code Authorship

Two forms of metaprogramming are simultaneously load-bearing:

- **Deterministic** — `template`, `metacall`, `#[derive_*]`, `quote_*!`. Mechanical, reproducible, type-driven. Reliable on combinatorics; rigid; harder to debug.
- **Probabilistic** — AI sessions writing Logos from human intent. Flexible, semantically aware, handles novel structure; brittle on combinatorics, leaves gaps, validation collapses at scale.

Both fail at the extremes. *All-deterministic* mechanises what need not be — TMP-grade pain for variations a human or model writes trivially. *All-probabilistic* drowns in N×M validation as combinatorics blow up across (data type × algorithm × infrastructure × index strategy). Memoria's rule, ported for the AI era: **base algorithms and data structures are written by hand in maximally generic, composable form; combinatorial interactions are generated by metaprograms.** The "by hand" role shifts to the model; the layer split remains, and calls for a third artifact between intent and machine.

A `genos` is a **semi-formal, parametric form specification** — Logos syntax with relaxed type rules expressing the *shape and invariants* of an algorithm or data structure, versioned in the codebase, executable through a minimal interpreter, the canonical statement of intent for a family. Test vectors run against the genos directly *and* against any concrete instantiation; agreement is the conformance contract. An AI session reads the genos before generating combinatoric implementations; the deterministic metaprog under it does the mechanical specialisation; per-instantiation artifacts are emergent and unread.

Not a new idea (automatic programming, program synthesis from specs, refinement types, executable specs; Knuth's MIX/MMIX plays the same role). What is new is *that it works in practice* — the natural shape of the "human writes spec, AI generates code, conformance is checked, iterate" loop people already run with AI tools. Genos turns that ad-hoc loop into a first-class artifact: the spec stops being a Slack message and becomes versioned source.

The keyword `genos` (Greek γένος = "kind, family") names a *parametric form*, reserved exclusively for computable form specifications. The earlier data-trait-family role migrated to `pub trait` + `#[writ_eidos]`, so `genos` now carries one meaning. Three layers under the source:

| Layer | Author | Cardinality | Validation surface |
|---|---|---|---|
| Genos | Human ↔ AI co-curated | One per family | Interpreter + test vectors |
| Deterministic metaprog | AI under human review | One per family | Conformance vs. genos |
| Combinatoric instantiations | Generated | N×M×... — never read | Conformance harness |

The cardinality column is the argument. AI is strong at one-per-family work (a careful genos, a genos→output transformer, validation against golden cases) and weak at N-many independent variations with independent validation — where drift accumulates between near-identical cases. The metaprog layer is the firewall: it absorbs the N×M explosion mechanically, so AI effort goes where AI is best and validation goes where it scales.

Logos can land this because the prerequisites exist: load-bearing metaprog substrate, the keyword reserved for the right reason, a compiler shaped for the incremental-iterative compilation genos+metaprog needs, and a model as primary author. The pattern is bottom-up — grown from observed AI workflow, not top-down design — which is what makes it likely to land where earlier attempts did not. For detail (interpreter sketch, conformance harness, property-based testing, eventual PPL substrate) see [internals: metaprog](../internals/metaprog.md#two-kinds-of-metaprogramming-and-the-bridge-between-them).

## Relationship to Rust

The Rust-like surface was chosen by the model itself. The original plan was a simpler, more verbose syntax barely above an IR, with no conventional expressions — the intuition being that explicitness gives smaller models fewer places to stumble. But the language must be human-readable too, and Rust sits in a sweet spot: expressive, low-level, a good DSL host, and — at the time of the choice — the syntax the models themselves generated most reliably. That model-fluency reinforced the original decision, but it is not a standing design goal: the surface is inherited from Rust wholesale, and where Logos extends it the leads are human readability and simple, clear semantics.

Logos is not Rust. It inherits surface syntax, the affine type system, generics, and the ownership-and-borrowing model, but is not source-compatible, does not aim for portability either way, and will not warp its design to preserve compatibility (nor gratuitously break it). It is expected to diverge substantially — driven by Writ code/data unification, the zone-based memory model, compile-time programming as ordinary Logos, and native green-fiber concurrency without async coloring. Already visible: no Rust module system, no async/`.await`, no procedural macros.

The base is Rust's *type system*; the compatibility is **deliberately temporary** and never source-level. At the preview stage, basic compatibility is still a design goal — Rust familiarity, easy porting — but by 1.0 the two will have diverged by design, at the level of both syntax and semantics. Before AI this would have been suicidal for a language. Rust is already good; programmers do not need many languages and least of all another Rust — not even one promising better batteries for the database-engine niche Logos targets through Memoria. The rational move would have been to upstream the missing pieces into Rust, not to fork a language out of it. The AI factor inverts that calculus: carrying a divergent language is no longer the more expensive path — it is *no longer* simpler to just extend Rust.

## Relationship to C++

C++ has an extraordinarily powerful template system that is, in equal measure, exhausting at scale — Memoria is the witness: templates work, large-scale type-level programming on them does not. Avoiding that Turing-tarpit is an original motivation for Logos: Memoria's problems should not become multi-thousand-line metaprograms in partial specialisation, SFINAE, and tag dispatch. Rust's type-level metaprogramming is historically weak; porting Memoria onto it sinks into the same tarpit by another route. Affine types are a delicious primitive but may not alone suffice for what Memoria asks.

Logos's response: full compile-time metaprogramming as a load-bearing feature. A *metafunction* is ordinary Logos running at compile time through compiler APIs — normally via higher-level interfaces, dropping to AST level when needed. The lesson taken from C++20/23 templates is template *expressive power* as a DSL; the lesson rejected is type-level programming as the way to wield it. Type-level computation moves into normal metafunctions.

Second motivation: Memoria's memory mechanisms (relocatable object graphs, in-pointer constants, fat references, zone-scoped mutability) live in C++ as a library on an unaware compiler, which then conservatively assumes the worst about aliasing/lifetimes/side effects, making optimisation a guessing game. Logos lifts those into the language: the base type system is large enough to describe them directly, so the optimiser reasons about them without restrictions — the invariants are facts the compiler sees, not gentleman's agreements with a generous inliner. Metaprogramming is supported at language and platform levels: the compiler integrates with a build system that is itself a data platform (abstraction layering, large-data support, a Datalog query engine), not a Make-like `cc` driver.

## Relationship to Java / Scala / Python

Despite the IR-flavoured intent, Logos ended up fairly *high-level* at integrating structured data with code, borrowing from the JVM and Python lineages. From Java: the package and module system, and metadata that lives through to runtime — Writ is built into the surface syntax, packs efficiently into `.rodata`, and is addressable as ordinary constants at runtime; reflection metadata and code attributes are laid out as Writ containers, so runtime access is zero-serialisation (the in-memory shape *is* the on-disk shape). From Python: dynamism via runtime dispatch on Writ objects, plus list/map comprehensions intended to grow (via the DSL subsystem) into a full integrated query language — the non-goal being the Tinkerpop tarpit where every non-trivial traversal becomes a Turing exercise.

The high-level surface (integrated Writ, data-processing eDSLs, runtime-accessible metadata) makes Logos viable for ordinary *application* development, not just systems work. Domain-specific abstractions are built inside the language rather than around it, so an app developer reaches for the same toolchain a systems programmer does. Conventional control flow is one of three planned computation models — production systems and dataflow are slated for first-class integration — toward **convergent programming**: every class of task in one environment, composing cleanly.

## Relationship to Haskell / Idris 2

Strongly-typed functional languages remained niche despite elegance. Agentic coding changes that: models need a rich *reward signal*, and a strong expressive type system delivers it almost for free — the compiler tells the model precisely *what* it got wrong, *where*, often *why*. Anaemic type systems give binary feedback: it ran or it didn't. Empirically this matters — models handle C++ surprisingly well, but the most *effective* language for them is Rust (not Python/JS), because of the diagnostics. The Logos compiler is written in C++ by models and works.

Logos borrows no concrete Haskell/Idris features, only the *direction*: the type system will be strengthened deliberately, specifically to improve the model reward signal; adjacent tooling (static analysers, type-aware lints, formal-property checkers) gets first-class attention, and the language will significantly *grow as a layer on top of that tooling*. Near-term: a Datalog engine in the compiler used for resolution; SMT and constraint solvers to follow.

## Where Logos Sits

| Comparison | Logos vs. Rust | Logos vs. C++ | Logos vs. Go |
|------------|----------------|---------------|--------------|
| Memory model | Similar: ownership, borrowing, lifetimes | Strictly safer | Lower-level |
| Generics | Monomorphic, with traits | More structured, no SFINAE | Type-checked, not duck-typed |
| Concurrency | Stackful green fibers (planned: FSM lowering) | Has reactor + fibers built in | Comparable model, different runtime |
| Macros / metaprog | Compile-time programs in Logos itself | Replaces templates/macros | Not comparable |
| Async coloring | None by default — implicit suspend via fibers; `async` retained only for targets like `wasm32` | N/A | None |
| Build | CMake + VCPKG (today); module binaries (planned) | Standard C++ stack | Comparable |

Logos deliberately omits several Rust features: no module system in the Rust sense, no procedural macros, default concurrency not async/`.await`. Packages and compile-time programs replace the first two; stackful green fibers replace `.await` as the default. `async`/`.await` is *kept* only for platforms that cannot provide system-level concurrency — notably `wasm32` in a browser (no threads, fibers, or preemption), where the colored model is the price of admission. Elsewhere `async` sees limited use: coloring scales poorly, propagates through every caller, and interacts badly with the rest of the language.

## Status

Logos is in active implementation. The compiler self-hosts nothing yet (frontend still C++). The stdlib is small but real; Writ is integrated and used by examples and tests. See the [Roadmap](../roadmap.md).
