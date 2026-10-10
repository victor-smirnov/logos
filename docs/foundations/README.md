# Foundations

Foundational articles on Logos: one article per subsystem or idea, each self-contained, each describing the current state. Primary reader: a model loading context before work on that area. Secondary reader: an engineer entering the subsystem.

## Index

Load the overview first when the task crosses layers; otherwise load only the article for the area.

| Article | Load when |
|---|---|
| [overview.md](overview.md) | any cross-layer task; first contact with the project; choosing which article to load next |
| [glossary.md](glossary.md) | a term in an article, ADR or issue is unclear; the canonical meaning of a project term |

Planned (not written yet; do not cite): compiler as a fixpoint; Datalog borrow checker and trait solver; metaprogramming and quotes; Deem query compiler; Memoria container plane and persistent snapshots; incremental and streaming Deem; zones and the Writ document model; verification process (oracles, `lt`, gates).

## Place among the other docs

| Layer | Answers | Lifetime |
|---|---|---|
| [docs/spec/](../spec/) | what the language must do (normative) | edited with the language |
| [docs/adr/](../adr/) | what was decided, when, why | append-only history; not rewritten |
| [docs/internals/](../internals/) | working notes on an implementation | often stale; check against code |
| docs/foundations/ | how the system is built now: model, invariants, mechanism, location in code, rejected alternatives, evidence | rewritten when the code moves; `verified-at` pins the commit |

An article cites ADRs and spec sections instead of copying them. When an article and an ADR disagree on current state, the article is checked against code and the loser is fixed.

## Article shape

Front matter:

```yaml
---
title: <name>
verified-at: <commit hash the content was checked against>
sources: [<ADR / spec paths the article draws on>]
---
```

Sections, in order; a section with nothing to say is omitted:

1. Problem and constraints: what must hold, under which limits.
2. Model: definitions, invariants, notation.
3. Mechanism: phases, data, where each lives in code.
4. Alternatives and prior art: what was rejected and why (rustc, Soufflé, DBSP, CIRCT, C++ Memoria, ...).
5. Evidence: tests, oracles, measurements that back the claims.
6. Status and open questions: per part, LANDED / PARTIAL / PROPOSED.

Size: 4-8 k words, so that two or three articles fit beside code in one context.

## Anchors and the lint

A claim about the mechanism names where it lives. Anchor forms:

- File or directory: a relative link, `[sema_expr.cpp](../../src/compiler/sema_expr.cpp)`.
- Symbol: a link whose text is a code span, `` [`TypePool::alloc`](../../src/compiler/sema.cpp) ``. The last segment of the name (`alloc`) must occur in the target file.
- Test: a link to the test file or directory.

No line numbers in anchors: they rot on every edit above them. Name the symbol instead.

[`scripts/foundations_lint.py`](../../scripts/foundations_lint.py) checks every article: front matter present, `verified-at` is a commit, every relative link resolves, every symbol anchor occurs in its file, every article is listed in this index. It does not check that a claim is true; it catches anchors that went stale. `verified-at` older than the last commit touching an anchored file is reported as a warning: the article may describe old code.

## Style

The `docs/` register: terse, declarative, one claim per sentence, notation over paraphrase, no hard line wrap. English.
