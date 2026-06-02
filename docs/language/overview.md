# Overview

Logos is a statically typed, compiled systems-level programming language. It produces native binaries via LLVM/MLIR and has its own compiler (`logosc`), standard library (`stdlib/std`), and runtime (`stdlib/rt`).

Logos descends from ideas developed in the [Memoria Framework](https://github.com/victor-smirnov/memoria), but it is a standalone language platform, not a C++ framework layer.

Logos and Memoria are co-developed. Memoria will eventually be written *in* Logos, and Logos is Memoria's first and primary user — exercising the language's data, metaprogramming, and runtime surfaces from day one. This is already partially in place: Hermes was originally designed inside Memoria and ported into Logos as the data substrate, and other Memoria components are expected to follow the same path as the Logos toolchain matures.

## Design Axes

Logos is shaped by a small number of opinions, in rough order of importance:

1. **AI-first ergonomics.** The syntax and semantics are chosen so that LLMs can generate, modify, and verify Logos code reliably. That means: linear control flow, explicit names, no SFINAE/CRTP-style metaprogramming, no magical dispatch, no hidden conversions, and a strong test culture.
2. **Code and data unified.** Hermes — a relocatable tagged object graph format — is *built into the language*, not bolted on. `@{...}` and `@[...]` are literal forms in the grammar; capture (`$ident`, `${expr}`) is type-checked at sema time; view types carry real lifetimes through the borrow checker; module-scope literals fold to rodata as `HermesStatic`. There is no DSL, no macro, no FFI boundary between Logos values and Hermes data — a document is just a value. See [Hermes in Logos](hermes.md).
3. **Systems-level performance.** Ownership, borrowing, monomorphized generics, AOT native codegen, and explicit memory control. The model is closer to Rust than to Go or Swift. The memory mechanisms Memoria depends on — relocatable object graphs, in-pointer constants, fat references, zone-scoped mutability — are lifted into the base type system rather than living as a library on top of an unaware compiler, so the optimiser can reason about them as facts.
4. **Compile-time metaprogramming as a load-bearing layer.** Metafunctions are ordinary Logos code that runs at compile time through compiler-provided APIs. Type-level computation is expressed as ordinary metafunctions, not as a separate template language; C++20/23-style template *expressiveness* is the lesson taken, type-level programming as the way to wield it is the lesson rejected. The build system itself is a data platform (Datalog query engine, abstraction layering, large-data support) rather than a `cc` driver. See [Metaprogramming](reference/metaprog.md).
5. **Convergent computation models.** Conventional control flow is one model; production systems (forward- and backward-chaining rules) and dataflow (digital-circuit-style graphs) are slated for first-class language integration. The goal is one toolchain covering the full spectrum and stack of practical work — systems programming, data processing, application development, digital-circuit design — with processes that compose cleanly across them.
6. **Verification orientation.** Diagnostics, runtime tracing, and a sizeable executable test suite (~720 passing, ~245 diagnostic tests) are core deliverables, not afterthoughts. The type system is deliberately strengthened in directions that improve the reward signal for model-driven authorship; adjacent tooling (static analysers, type-aware lints, formal-property checkers) gets first-class attention rather than being someone else's project.
7. **Pragmatic interop.** Logos can call C/C++ for FFI and links against LLVM/MLIR/Hermes implementations written in C++, but Logos source is the primary programming model. There is no stable C++ AST interop layer.

## Paradigm and Skin: C++ Ambition, Rust Ergonomics

A short framing that ties the design axes together.

**Paradigmatically Logos is C++** — by which we mean a *maximalist* systems language that does not pre-compromise on expressiveness for the sake of a gentler learning curve. Arbitrary structured const-generic values (HermesStatic), variadic generics with first-class type packs, type-level computation through ordinary metafunctions running at compile time, slot extraction through deep paths, registry-driven dispatch keyed by content-hash type identity, generic Drop with proper substitution semantics — these are the features the C++ committee has aspired to and partly delivered after thirty years of accumulated specification work. Logos delivers them as load-bearing primitives, not as later additions wedged into a system that wasn't designed for them. The `1-2%` of programming where the language genuinely matters — databases, kernels, scientific computing, persistent storage engines, formally verified components — these are the workloads Logos is shaped by, not the mainstream "easy and safe" 98%.

**Surface-syntactically Logos is Rust** — `let mut`, `&mut self`, `match`, traits, monomorphized generics, ownership and borrowing, no exceptions. The choice was not sentimental: it is the syntax models generate most reliably (because Rust is statically typed, low-entropy on character, and verbose-but-conventional) and the syntax humans coming from modern systems programming already read fluently. Rust-shaped surface keeps the language readable and writable for human reviewers, owners, and decision-makers without forcing them through C++-style obscurity (template specialisation, SFINAE, header-template-stamping, ABI archaeology).

The two fit together by *target user*:

- **Models are the primary author** for serious systems software. Their structural strengths sit on the high-compressible side of the task spectrum — they handle algorithms, type systems, formal logic, large-scale code generation extremely well, and improve fastest there. Their structural weakness is the deterministic side — they cannot reliably *execute* the algorithms whose structure they understand. The offload target for that work is exactly the kind of substrate Logos provides: explicit metaprogramming, verifiable type-level computation, deterministic codegen, persistent data structures. The C++ paradigm is what the model needs because it minimises the amount of work the model has to do "implicitly inside its own weights" — every load-bearing capability is exposed as a first-class language construct the model can name, manipulate, verify, and reason about. *Premature passivisation* — the mainstream-language tendency to file extreme-case features behind RFCs and macros because they confuse junior humans — is a tax the model does not need to pay and would rather not.

- **Humans remain the responsibility-holding layer.** A model can author a kernel storage engine, but a model cannot bear ownership of what that storage engine *means* in the world: which invariants matter, who is harmed by which failure mode, whether shipping is the right call given non-technical context. Those calls live with humans and they always will, because they are the low-compressible side of decision-making — judgment, taste, ethics, accountability. Humans also fill in capabilities models lack: novel-domain reasoning where no training data exists, creative problem framing, value-laden trade-offs. Rust-shaped surface keeps humans operationally close to the code: they can read it, review it, intervene in it, set policy through it, without having to be C++ template savants.

The synthesis: **C++-paradigm depth so the model can actually write serious systems code; Rust-skin readability so the human can review, redirect, and own the result**. Neither layer is decorative. Strip the C++ depth and you get a tutorial-grade language whose models hit a metaprogramming ceiling on every database-class problem. Strip the Rust skin and you get an internal-only language whose humans cannot effectively oversee the model's output at scale. Both layers exist because both users exist, and the two users have asymmetric strengths that the language is shaped to compose rather than to flatten.

This is not a marketing positioning. It is a structural observation: when the primary author shifts from human to model, the design constraints on a systems language realign. The features that mainstream language committees deferred as "too advanced" because they confuse human juniors are the same features models need most. The features kept simple "for human readability" continue to matter — but readability now means *human reviewer*, not *human author*. Logos is the shape that falls out of taking both of those facts seriously at the same time.

For the deeper argument — model behaviour, the joint human/AI/program system, and the specific platform requirements — see the [AI Platform Era](../ai-platform/README.md) essays.

## Genos: the Third Layer of Code Authorship

In an AI-authored systems language, two forms of metaprogramming are simultaneously load-bearing, and they are different kinds of mechanism:

- **Deterministic metaprogramming** — `template`, `metacall`, `#[derive_*]`, `quote_*!`. Mechanical, reproducible, type-driven generation. Reliable on combinatorics; rigid; debugging is harder than ordinary code.
- **Probabilistic metaprogramming** — AI sessions writing Logos code from human intent. Flexible, semantically aware, handles novel structure; brittle on combinatorics, leaves gaps, validation collapses at scale.

Both have failure modes at the extremes. *All-deterministic* mechanises everything that doesn't need to be mechanised — TMP-grade pain to generate variations a human or model would write trivially. *All-probabilistic* drowns in N×M validation as combinatorics blow up across (data type × algorithm × infrastructure × index strategy).

Memoria's hard-won rule, ported here for the AI era: **base algorithms and data structures are written by hand in maximally generic, composable form; combinatorial interactions are generated by metaprograms.** In Logos+AI, the "by hand" role shifts to the model, but the layer split remains. The split calls for a third artifact, sitting between intent and machine.

A `genos` is a **semi-formal, parametric form specification** — Logos syntax with relaxed type rules, expressing the *shape and invariants* of an algorithm or data structure, lived in the codebase and versioned, executable through a minimal interpreter, serving as the canonical statement of intent for a family. Test vectors run the interpreter against the genos directly *and* against any concrete instantiation; agreement is the conformance contract. The genos is what an AI session reads before generating combinatoric implementations; the deterministic metaprog under it does the mechanical specialisation; the per-instantiation artifacts are emergent and unread.

This is not a new idea. Automatic programming, program synthesis from specifications, refinement types, executable specifications — the conceptual history runs a century deep. Knuth's TAOCP uses MIX/MMIX for the same role: pseudocode in a target environment, executable enough to ground intuition, abstract enough to keep the algorithm legible. What is new is *that it works in practice* — not as a theoretical exercise but as the natural shape of a workflow people are already doing every day with AI coding tools. The pattern of "human writes spec, AI generates code, conformance is checked, iterate" is already the operational reality. Genos turns that ad-hoc loop into a first-class language artifact: the spec stops being a Slack message and becomes versioned source.

The keyword `genos` (Greek γένος = "kind, family") names a *parametric form* — a family classified by shared structure. It is reserved exclusively for these computable form specifications. The earlier data-trait-family role the keyword once doubled for has been migrated to `pub trait` + the `#[hermes_eidos]` annotation, so `genos` now carries a single meaning.

The model that emerges is three layers under the source code, not one:

| Layer | Author | Cardinality | Validation surface |
|---|---|---|---|
| Genos | Human ↔ AI co-curated | One per family | Interpreter + test vectors |
| Deterministic metaprog | AI under human review | One per family | Conformance vs. genos |
| Combinatoric instantiations | Generated | N×M×... — never read | Conformance harness |

The cardinality column is the structural argument. AI's strength sits at one-per-family work — write a careful genos, write a transformer from genos to combinatoric output, validate against a few golden cases. AI's weakness sits at N-many independent variations with independent validation — exactly the place where drift accumulates between near-identical cases. The metaprog layer is the firewall between the two. It absorbs the N×M explosion mechanically, so AI's effort goes where AI is best and validation goes where validation can scale.

Logos is positioned to land this layer because the prerequisites are already in place: the metaprog substrate is load-bearing rather than bolted on, the keyword exists for the right reason, the compiler is shaped for incremental-iterative compilation that genos plus metaprog requires, and the primary author is already the model. The pattern this captures is bottom-up — it grows from observed AI-coding workflow, not from a top-down design exercise — and that is the part most likely to make it land where earlier attempts at the same shape did not.

For the architectural detail — interpreter sketch, conformance harness, integration with property-based testing and the eventual PPL substrate — see [internals: metaprog](../internals/metaprog.md#two-kinds-of-metaprogramming-and-the-bridge-between-them).

## Relationship to Rust

The Rust-like surface was, frankly, chosen by the model itself. The original plan was a much simpler, more verbose syntax — barely above an intermediate representation, with no expressions in the conventional sense. The intuition was that explicitness, even at the cost of token count, gives smaller and mid-sized models fewer places to stumble.

In practice the language also has to be readable and writable by humans, and Rust turned out to sit in a sweet spot: expressive, low-level, a good DSL host — and, importantly, the models themselves *like* it. They generate Rust more reliably than most alternatives. Since Logos is built for models first, leaning into a syntax they already handle well is a natural choice rather than a sentimental one.

That said: Logos is not Rust. It inherits surface syntax, the affine type system, generics, and the ownership-and-borrowing memory model, but it is not source-compatible with Rust, does not aim for code portability in either direction, and will not warp its own design to preserve compatibility (though it will not gratuitously break it either). Logos is expected to diverge from Rust substantially in the near future — driven by AI-first ergonomics, code-and-data unification through Hermes, compile-time programming as ordinary Logos code, and native green-fiber concurrency without async coloring. Some of that divergence is already visible: no Rust module system, no async/`.await`, no procedural macros.

## Relationship to C++

C++ has an extraordinarily powerful template system. It is also, in roughly equal measure, exhausting to program in at scale — Memoria is the witness here. Templates work; large-scale type-level programming on top of them does not. Avoiding that Turing-tarpit is one of the original motivations for Logos: Memoria's problems should not turn into multi-thousand-line metaprograms expressed through partial specialisation, SFINAE, and tag dispatch. Modern Rust would otherwise have been a perfectly reasonable host.

Rust's own type-level metaprogramming has historically been weak, and even today porting Memoria onto it would sink into the same tarpit by a different route. Affine types are a delicious primitive, but it is not yet clear whether they alone are sufficient for what Memoria asks of the language, or whether deeper extensions are required.

Logos's response is to make full compile-time metaprogramming a load-bearing feature, not an afterthought. A *metafunction* is ordinary Logos code that runs at compile time through compiler-provided APIs. Metafunctions do not normally manipulate the AST directly — they go through higher-level interfaces — but they can drop down to AST level when they need to. The lesson taken from C++20/23 templates is the *expressive power* of templates as a DSL; the lesson rejected is type-level programming as the way to wield that power. Type-level computation moves into normal metafunctions.

A second motivation pulls in the same direction. Memoria leans on a specific set of memory-management mechanisms — relocatable object graphs, in-pointer constants, fat references, zone-scoped mutability — that in C++ live as a library on top of an unaware compiler. The compiler then has no way to *know* that these mechanisms hold; it conservatively assumes the worst about aliasing, lifetimes, and side effects, and optimisation becomes a perpetual guessing game (some passes help, some hurt, and which is which depends on the phase of the moon). Logos lifts those mechanisms into the language itself. The base type system is built large enough and expressive enough to describe them directly, which lets the optimiser reason about them *without restrictions*: the invariants Memoria depends on are facts the compiler can see, not gentleman's agreements between a library and a sufficiently generous inliner.

Metaprogramming is supported at both the language and the platform levels. The compiler is integrated with a build system that is itself a fully-fledged data platform — abstraction layering, large-data support (code is data too), and a serious query engine (Datalog) — rather than a Make-like driver around `cc`.

## Relationship to Java / Scala / Python

Despite the original IR-flavoured intent, Logos ended up as a fairly *high-level* language when it comes to integrating structured data with code, and that flavour borrows from the JVM and Python lineages.

From Java, Logos takes the package and module system and the idea of metadata that lives all the way through to runtime. Hermes is built directly into the surface syntax: Hermes objects pack efficiently into static objects (`.rodata`) and are addressable as ordinary constants at runtime. Reflection metadata and code-level attributes are physically laid out as Hermes containers, so accessing them at runtime is zero-serialisation — the in-memory shape *is* the on-disk shape.

From Python, Logos takes dynamism via runtime dispatch on Hermes objects, plus surface conveniences like list and map comprehensions. Those comprehensions are intended to grow, via the DSL subsystem, into a full integrated query language. The deliberate non-goal is to avoid the Tinkerpop tarpit — a query surface that turns every non-trivial traversal into a Turing exercise.

The high-level surface — integrated Hermes, eDSLs for data processing, runtime-accessible metadata, and the rest of the convenience layer — is what makes Logos viable not just for low-level and systems programming but for ordinary *application* development as well. The metaprogramming platform reinforces the same point: domain-specific abstractions can be built inside the language rather than wedged in around it, so an app developer reaches for the same toolchain a systems programmer does and gets ergonomics appropriate to their layer.

Conventional control flow is only one computation model in Logos. Two more are slated for first-class language integration: production systems (forward- and backward-chaining rules) and dataflow (digital-circuit-style graphs). The intent is that Logos covers the full spectrum *and* full stack of practical work — I/O and databases, data processing, digital-circuit design, systems programming — under one roof. The goal is **convergent programming**: every class of task addressed inside one environment, with processes and pipelines that compose cleanly across them.

## Relationship to Haskell / Idris 2

Strongly-typed functional languages like Haskell and Idris 2 have, despite considerable effort and considerable elegance, remained niche. Agentic coding changes that picture in a way that wasn't obvious before. Models, to do useful work, need a rich *reward signal* — and a language with a strong, expressive type system delivers that signal almost for free, because the compiler can tell the model precisely *what* it got wrong, *where*, and often *why*. Languages with anaemic type systems give the model essentially binary feedback: it ran, or it didn't.

Empirically, this matters. It came as a genuine surprise that models handle C++ as well as they do; the most *effective* language for them, however, is Rust — not Python or JavaScript — and the reason is the compiler's diagnostics. The Logos compiler itself is written in C++, by models, and it works and does not fall over. That is not a trivial result.

Logos does not currently borrow concrete features from Haskell or Idris. What it borrows is the *direction*: the type system will be strengthened deliberately, and specifically in ways that improve the reward signal for models. Adjacent tooling — static analysers, type-aware lints, formal-property checkers — gets first-class attention rather than being someone else's project, and to a significant extent the language will *grow as a layer on top of that tooling*, not the other way around. A near-term work item is integrating a Datalog engine directly into the compiler and using it for resolution; SMT solvers and constraint solvers will follow.

## Where Logos Sits

| Comparison | Logos vs. Rust | Logos vs. C++ | Logos vs. Go |
|------------|----------------|---------------|--------------|
| Memory model | Similar: ownership, borrowing, lifetimes | Strictly safer | Lower-level |
| Generics | Monomorphic, with traits | More structured, no SFINAE | Type-checked, not duck-typed |
| Concurrency | Stackful green fibers (planned: FSM lowering) | Has reactor + fibers built in | Comparable model, different runtime |
| Macros / metaprog | Compile-time programs in Logos itself | Replaces templates/macros | Not comparable |
| Async coloring | None by default — implicit suspend via fibers; `async` retained only for targets like `wasm32` | N/A | None |
| Build | CMake + VCPKG (today); module binaries (planned) | Standard C++ stack | Comparable |

Logos deliberately does **not** adopt several Rust features: there is no module system in the Rust sense, no procedural macros, and the default concurrency model is not async/`.await`. Packages and compile-time programs replace the first two outright; stackful green fibers replace `.await` as the default.

`async`/`.await` is *kept* as a targeted mechanism for platforms where concurrency cannot reasonably be provided at the system level — most notably `wasm32` running inside a browser, where the host gives the program no threads, no fibers, and no preemption to work with. There the colored model is the price of admission. Outside those targets, `async` is expected to see limited use: code colouring scales poorly, it propagates through every caller, and it interacts badly with the rest of the language. Fibers are the default exactly because that scaling story matters more than per-target convenience.

## Status

Logos is in active implementation. The compiler self-hosts no parts of itself yet (the frontend is still C++). The standard library is small but real; Hermes is integrated and used by examples and tests. See the [Roadmap](../roadmap.md) for current milestones.
