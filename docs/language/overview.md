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
| Async coloring | None — implicit suspend points | N/A | None |
| Build | CMake + VCPKG (today); module binaries (planned) | Standard C++ stack | Comparable |

Logos deliberately does **not** adopt several Rust features: there is no module system in the Rust sense, no async/`.await` coloring, and no procedural macros. Each of these is intended to be replaced by Logos-native mechanisms (packages, fibers, compile-time programs).

## Status

Logos is in active implementation. The compiler self-hosts no parts of itself yet (the frontend is still C++). The standard library is small but real; Hermes is integrated and used by examples and tests. See the [Roadmap](../roadmap.md) for current milestones.
