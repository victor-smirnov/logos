# ADR 0022 — logos-format: the canonical source formatter

- Status: **DRAFT** (2026-07-18)
- Builds on: the --gen-dir render/reparse machinery (d25d4fa1: render →
  file → REPARSE → swap, per-emission fidelity gate), the sema_render item/
  block renderer, ADR 0021 (generated families are real source; dumps must
  read like hand-written code), the accumulated style rules (2026-07-18).
- Drives: retiring hand-formatting from emitters; a `--check` CI gate; the
  end of ad-hoc reformatting sweeps.

## 0. Context

Three formatting consumers emerged in one day:

1. **Hand-written source** — style rules now exist (container declarations
   multi-line; block bodies on their own lines; ≤1 statement per line;
   one-line `match` split by arm, empty `{}` arms and single-statement arm
   bodies inline) and were applied by ad-hoc python/perl sweeps — unrepeatable
   and risky.
2. **Rendered dumps** (quote-blob emissions) — sema_render formats them; the
   rules live as C++ code (render_block_src, IMPL_BLOCK case) and were
   patched twice today (49c1f89e one-liner removal).
3. **Text-channel emissions** — emitters hand-format with `\n    ` pushed
   into strings (99f447cc) — style encoded in string literals, per emitter.

Three encodings of one style drift apart by construction. The compiler
already owns the only trustworthy style implementation (the renderer, proven
by the per-emission reparse-fidelity gate); everything else should derive
from it.

## 1. Decision

**`logos-format` is a thin CLI over the compiler's own parse→render pipeline.**
One style implementation — the renderer; every consumer routes through it:

```
logos-format file.logos            # rewrite in place
logos-format --check file...       # exit 1 + diff summary if not canonical
logos-format --stdin               # filter mode (editor integration)
lforge fmt [--check]               # project-wide, honors lforge.writ layout
```

- Formatter = parse (the ONE peg grammar) → AST → `render_module_src` →
  write. No separate token-stream engine, no second style spec. A style
  change is a renderer change: the formatter, the --gen-dir dumps, and the
  quote-blob display all move together — one flip point.
- **Emitters stop hand-formatting.** Text-channel chunks may emit compact
  single-line source; the dump path (and `lforge fmt` over gen dirs) runs the
  renderer for display. The `\n    `-in-string-literals style dies.
- **Gates carried over from --gen-dir**: fidelity (reparse(render(x)) ≡ x,
  AST-identical) and idempotency (format(format(x)) == format(x)) are the
  formatter's own CI tests, run over the whole tree + all gen dirs.

## 2. The hard part: trivia (comments) — phased

The renderer works on the lowered AST; the PEG lexer discards ordinary
comments (doc comments survive as DOC_LINE_LIT/doc-block items). A formatter
that eats comments is unusable on hand-written code.

- **Phase 1 (immediately useful): generated + checked surfaces.** Format
  gen-dir dumps and run `--check` on tree files that already round-trip
  losslessly (no ordinary comments lost — detectable by comparing comment
  byte-spans pre/post). Files failing the trivia check are skipped with a
  note, not mangled.
- **Phase 2 (the real tool): comment attachment.** The lexer records comment
  spans (position + text) into a side table; the renderer re-attaches by
  anchor (preceding-item / trailing-on-line rules, the standard formatter
  discipline). This is grammar+renderer work with its own fidelity gate:
  byte-identical comment text, stable anchoring under reformat.
- Phase 2 unlocks in-place formatting of the whole tree and editor-on-save.

## 3. Style spec

The style is SPECIFIED by the renderer's code plus a corpus of
golden files (tests/format/*.logos + *.expected — reformat-stable pairs).
Today's rules, restated as the initial spec:

1. Declarations (container et al.): clause-per-line, 4-space indent.
2. Block bodies (`if`/`while`/`for`/`match`/fn bodies): body on its own
   line(s), ≤1 statement per line; `match` arm-per-line; empty `{}` and
   single-statement ARM bodies stay inline; if-EXPRESSIONS
   (`let x = if c { a } else { b }`) stay inline — they are values.
3. Item spacing: one blank line between items; `package`/`use` header block
   separated from items.
4. 4-space indentation throughout; no tabs; no trailing whitespace.

## 4. Non-goals (now)

- Configurability (line width knobs, style options) — one canonical style,
  zero configuration, rustfmt-default philosophy.
- Formatting inside quote_item!/metaprog string templates in SOURCE form —
  templates are code and follow the style by hand; the formatter formats
  their OUTPUT (the dumps).
- Semantic rewrites (import sorting beyond dedup, item reordering).

## 5. Staging

1. `logos-format` binary: parse→render→write + `--check` + `--stdin`;
   golden-corpus + idempotency + fidelity CI tests; wire `lforge fmt`.
2. Point the --gen-dir dump path and text-channel display at the formatter
   (emitters emit compact; dumps render canonical); delete hand-formatting
   from container_item.logos emitters.
3. Trivia phase 2 (comment side-table + re-attachment); then tree-wide
   `--check` in CI and the one-time canonical reformat commit.

## 6. Open questions

- Comment anchoring rules for edge positions (between match arms, inside
  long argument lists) — decide by corpus, not upfront.
- Whether `lforge fmt` formats gen dirs by default (display concern) or only
  source dirs.
- Self-hosting horizon: once logosc-in-Logos lands, the formatter's core is
  a stdlib metafunction (render is already metaprog-adjacent); the CLI stays.
