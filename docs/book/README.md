# The Logos Book

A monograph on how Logos is built: premises, the core language, the compiler, the data layers, Memoria, execution, directions. Primary reader: a model loading context before work on an area. Secondary reader: an engineer entering a subsystem. The table of contents is [src/SUMMARY.md](src/SUMMARY.md); `mdbook build docs/book` renders it.

## How to load it

Chapters are in reading order: a chapter uses only terms and results of earlier chapters. Each chapter is also self-contained enough to load alone. For a cross-layer task load [System map](src/p1-02-system-map.md) first; otherwise load the chapter for the area and the [Glossary](src/appendix-a-glossary.md) if a term is unclear. A chapter listed in SUMMARY.md without a file is not written yet; do not cite it.

## Place among the other docs

| Layer | Answers | Lifetime |
|---|---|---|
| [docs/spec/](../spec/) | what the language must do (normative) | edited with the language |
| [docs/adr/](../adr/) | what was decided, when, why | append-only history; not rewritten |
| [docs/internals/](../internals/) | working notes on an implementation | often stale; check against code |
| docs/book/ | how the system is built now: model, invariants, mechanism, location in code, rejected alternatives, evidence | rewritten when the code moves; `verified-at` pins the commit |

A chapter cites ADRs and spec sections instead of copying them. When a chapter and an ADR disagree on current state, the chapter is checked against code and the loser is fixed. Part VII (Directions) describes proposed work; every other part describes the code at `verified-at`, with proposed parts marked in the chapter's status section.

## Chapter shape

Front matter:

```yaml
---
title: <name>
verified-at: <commit hash the content was checked against>
sources: [<ADR / spec paths the chapter draws on>]
---
```

Sections, in order; a section with nothing to say is omitted:

1. Problem and constraints: what must hold, under which limits.
2. Model: definitions, invariants, notation.
3. Mechanism: phases, data, where each lives in code.
4. Alternatives and prior art: what was rejected and why (rustc, Soufflé, DBSP, CIRCT, C++ Memoria, ...).
5. Evidence: tests, oracles, measurements that back the claims.
6. Status and open questions: per part, LANDED / PARTIAL / PROPOSED.

Size: 4-8 k words, so that two or three chapters fit beside code in one context. File names: `pN-MM-slug.md` (part N, chapter MM), `appendix-X-slug.md`.

## Notes

[notes/](notes/) holds raw material per chapter: decisions and positions from discussions, facts found in code, open questions. It is not part of the book and not linted. A chapter is written from its notes file; the notes stay as provenance.

## Anchors and the lint

A claim about the mechanism names where it lives. Anchor forms (paths relative to `src/`):

- File or directory: a relative link, `[sema_expr.cpp](../../../src/compiler/sema_expr.cpp)`.
- Symbol: a link whose text is a code span, `` [`TypePool::alloc`](../../../src/compiler/sema.cpp) ``. The last segment of the name (`alloc`) must occur in the target file.
- Test: a link to the test file or directory.

No line numbers in anchors: they rot on every edit above them. Name the symbol instead.

[`scripts/book_lint.py`](../../scripts/book_lint.py) checks every chapter: front matter present, `verified-at` is a commit, every relative link resolves, every symbol anchor occurs in its file, every chapter file is in SUMMARY.md and every SUMMARY.md link has a file. It does not check that a claim is true; it catches anchors that went stale. `verified-at` older than the last commit touching an anchored file is a warning: the chapter may describe old code.

## Style

The `docs/` register: terse, declarative, one claim per sentence, notation over paraphrase, no hard line wrap. English.
