# Notes: 2.1 Rust as the reference semantics

Raw material for the chapter. Positions are Victor's unless marked; each with its date. Not part of the book.

## Sources already in the repo and on the site

- logos-site docs/introduction.md "Relationship to Rust": surface chosen with the model in mind; tracks Rust 1.93; deviation = bug unless blessed; compatibility not a goal, divergence not a goal.
- logos-site docs/roadmap.md: "Logos is not Rust 2.0"; Rust = bootstrap language; divergence where a resident reasoner changes the economics (coherence, specialization, repeated bounds, const exprs).
- Memory 2026-08-11: Rust 1.93 a starting base, never a compatibility target; Rust's disease = enumerations of but-if reasons on a prematurely stabilised base; re-derive from a small logical core, syntax nearly kept, semantics simplified.
- docs/DIVERGENCES.md: §A blessed, §B catch-up.

## Discussion

- 2026-10-10 (Victor), history. Original idea: a systems language without C++'s fundamental problems (UB, metaprogramming, memory safety), with strongly simplified syntax (down to having no expressions), and a developed type system up to dependent types. Goal: derive as many program properties as possible at compile time (Rust), not at run time (Python).
- 2026-10-10 (Victor), what changed along the way: (1) current models no longer need syntactic simplicity; they handle complex and even ambiguous syntax fine. (2) A human is better served by a syntactically expressive language. (3) Rust was chosen as the language where affine types are already worked out. (4) Modern Rust turned out to be syntactically quite complex, in Victor's view more complex than C++. (5) Plus the accumulated influence of the code base. (6) A clear trend: humans will no longer write in programming languages, so developing syntax toward fewer keystrokes makes no sense; humans will not press those keys anyway.
- 2026-10-10 (Victor), summary: Rust was chosen for its worked-out affine types; its current syntactic variety is a "suitcase without a handle": it has to be carried, it cannot be thrown away, and it is hard to maintain.
- 2026-10-10 (Claude): this corrects chapter 1.1 §3: the reason for Rust is the worked-out semantics of affine types (plus model fluency), not the syntax; the syntax is an inherited cost. The 1.1 table row "model InD → Rust surface" must say so.
