# Overview

Logos is a statically typed, compiled systems-level programming language. It produces native binaries via LLVM/MLIR and has its own compiler (`logosc`), standard library (`stdlib/std`), and runtime (`stdlib/rt`).

Logos descends from ideas developed in the [Memoria Framework](https://github.com/victor-smirnov/memoria), but it is a standalone language platform, not a C++ framework layer.

Logos and Memoria are co-developed. Memoria will eventually be written *in* Logos, and Logos is Memoria's first and primary user — exercising the language's data, metaprogramming, and runtime surfaces from day one. This is already partially in place: Hermes was originally designed inside Memoria and ported into Logos as the data substrate, and other Memoria components are expected to follow the same path as the Logos toolchain matures.

## Design Axes

Logos is shaped by a small number of opinions, in rough order of importance:

1. **AI-first ergonomics.** The syntax and semantics are chosen so that LLMs can generate, modify, and verify Logos code reliably. That means: linear control flow, explicit names, no SFINAE/CRTP-style metaprogramming, no magical dispatch, no hidden conversions, and a strong test culture.
2. **Code and data unified.** Hermes — a relocatable tagged object graph format — is *built into the language*, not bolted on. `@{...}` and `@[...]` are literal forms in the grammar; capture (`$ident`, `${expr}`) is type-checked at sema time; view types carry real lifetimes through the borrow checker; module-scope literals fold to rodata as `HermesStatic`. There is no DSL, no macro, no FFI boundary between Logos values and Hermes data — a document is just a value. See [Hermes in Logos](hermes.md).
3. **Systems-level performance.** Ownership, borrowing, monomorphized generics, AOT native codegen, and explicit memory control. The model is closer to Rust than to Go or Swift.
4. **Verification orientation.** Diagnostics, runtime tracing, and a sizeable executable test suite (~660 passing, ~245 diagnostic tests) are core deliverables, not afterthoughts.
5. **Pragmatic interop.** Logos can call C/C++ for FFI and links against LLVM/MLIR/Hermes implementations written in C++, but Logos source is the primary programming model. There is no stable C++ AST interop layer.

## Relationship to Rust

The current Logos syntax and resource model are borrowed from Rust. This is deliberate: Rust experience transfers cleanly to Logos today, and the borrow checker, ownership rules, and trait/generics surface will feel familiar to a Rust programmer.

The similarity is a starting point, not a goal. Logos is being built in a fundamentally different paradigm — AI-first ergonomics, code-and-data unification through Hermes, compile-time programming as ordinary Logos code, native green-fiber concurrency without async coloring. Over the medium term Logos and Rust are expected to diverge both syntactically and paradigmatically; some of that divergence is already visible in the absence of Rust modules, async, and procedural macros.

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
