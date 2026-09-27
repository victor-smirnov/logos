# ADR 0030. A core layer between the AST and sema: one desugaring pass, one implementation per semantic rule, lossless with provenance

Status: DRAFT for pair review (2026-09-26). Nothing here is implemented. Inputs:
`docs/audit/2026-09-26-sema-path-inventory.md` (the "path inventory"),
`docs/audit/2026-09-26-representation-pipeline.md` (the "repr audit"),
`docs/audit/2026-09-26-rust-parity.md`. Open questions for the pair are in the
last section. Model: rustc's AST → HIR → typeck → MIR split.

## Problem

**Sema is case analysis on AST shapes.** Each semantic rule is implemented once
per surface form that reaches it, and the forms drift apart.

- The path inventory counts 107 semantic rules and 522 per-rule
  implementations (about 430 distinct code paths): mean 4.9, median 4. Only 2
  rules have a single implementation (the closure capture scanner and
  `gen_drop_value`).
- The worst: method-call probe 19 paths; literal range adoption 17 (about 100
  hand-written checks); expected type 14 producers over 7 `hint_*` channels;
  literal defaulting 13; generic-arg inference 12; trait satisfaction 10, split
  between sema and mono; codegen store-into-place 10.
- This is the mechanism behind the "neighbour form" defects of the last month:
  compound assignment had one path per place shape; `match` as statement and as
  expression are two copies (memory: `feedback_match_stmt_and_expr_are_two_copies`);
  pattern payload shapes; method resolution per receiver kind; closure escape
  decided by three heuristics. A fix lands on the form in the failing test and
  the neighbour stays broken.
- Confirmed silent wrong code whose root is a missed form (path inventory §1):
  a non-exhaustive match that segfaults; divergence decided by the callee's
  name; a boxed closure keeping a stack env; a `&mut`/`&` merge that writes
  through a shared borrow; an if-arm `&dyn` taking the wrong vtable; `let y = { x }`
  double free; vanishing tail assignments.

**Information is lost on the way down.** The repr audit (561 rows):

- positions: the AST keeps only the first line of a node; in LIR only
  statements carry a line; BIR and mono diagnostics reach `Diag` as `""`/0 (the
  line survives only inside the message text);
- desugaring provenance survives only as borrow-checker markers
  (`BorrowOrigin`, `CallMode`, `TRANSPARENT`, `COMPILER_GLUE`,
  `DESTRUCTURE_TMP`) and `__` name prefixes; after lowering, `if let`, `for`,
  `?`, compound assignment and overloaded operators are indistinguishable from
  hand-written code;
- mono drops every item's metadata (DOC, annotations, test flags, TYPE_CODE,
  TYPE_HASH) because it rebuilds items through `clone_fn`/`clone_struct_def`;
- visibility, `unsafe`, extern ABI and the import graph never reach LIR;
- identity travels as spelling: mangled callee names, field-name strings, raw
  label strings (an unknown label silently falls back to the innermost loop);
- several desugarings go through render-and-reparse
  (`lower_reparsed_tail_expr`: `vec!`, `?` dispatch, fn-macro args,
  `include!`), and the round-trip renderer has about 30 node codes with no
  case and about 14 wrong ones.

The consumers that need the lost facts are not built yet (metaprog over typed
code, reflection, debugger, IDE); by the time they are, the facts must already
be there.

## Decision

Insert one layer and one discipline:

1. **Core layer (HIR).** A type-free pass, AST → core AST, run per item after
   macro expansion and before sema. It lowers every construct that rustc
   lowers in AST → HIR, once. Sema never sees the surface forms it replaces.
2. **One implementation per semantic rule in sema (the typed cores).** Every
   rule that needs types (places, patterns, method probe, coercions,
   ownership, closure environments, expected types and literal inference,
   obligations, codegen representation) is one chokepoint that every form
   calls. Downstream stages (mono, BIR, mlir_gen) consume recorded facts, not
   re-derive them.
3. **Lossless with provenance.** Every AST fact is carried, tagged, or
   explicitly consumed. Every core node carries a span and an `origin`.

The HIR is the enabling half: most duplicated paths exist because sema receives
N spellings of one construct. The typed cores are the paying half: they are
where the paths are deleted.

## D1. Placement

```
source → parse (AST, Writ) → macro/metacall expansion (AST)
       → HIR lowering (core AST, Writ)          ← NEW
       → sema: collect, resolve, typeck, lower (LIR)
       → mono → BIR (borrow check) → MLIR/DWARF
```

- Expansion stays on the AST: metaprog handlers and fn-macros operate on
  surface syntax, as in rustc.
- HIR lowering runs per item, lazily or eagerly (implementation choice); it
  needs no types and no name resolution beyond lang-item identities (D4).
- The AST is kept, not overwritten. Every HIR node points back to it by span;
  metaprog and tooling can read either.

## D2. Representation: the core AST is a subset of the AST in the same Writ format

The HIR is not a new data structure. It is the AST's node format
(`TinyObjectMap` nodes, `ast.hpp` codes and keys) restricted to a CORE subset
of node codes, plus a small number of new codes where no surface form exists.

Why:

- sema's existing readers consume core nodes unchanged, so migration is "stop
  producing a surface form, then delete sema's branch for it", one form at a
  time, with no big-bang switch;
- the input is already Writ, so the pass is a Writ → Writ rewrite with the
  existing `synth_*` builders (`synth_match`, `synth_block`, `synth_let_chain`
  already do this for if-let and let-chains inside sema; they move out);
- metaprog and a future logosc-in-Logos read one format.

Rejected: C++ structs like rustc's `hir::Expr` (a second mirror next to LIR's
variant + Writ mirror; invisible to metaprog); rendering to text and reparsing
(loses spans, and the renderer is incomplete).

**Core subset** (the AST codes sema may still receive; the full list is an
implementation artefact, `hir_core_codes`):

- items as today;
- statements: `LET` (with pattern), `EXPR_STMT`, item statements;
- expressions: literals, paths, calls, method calls, field/index/deref, unary
  and binary operators (operators stay as nodes; their resolution is typed,
  D3), `MATCH`, `LOOP` (with label and break value), `BLOCK`, `IF` (bool
  condition only), closures, `EXIT` (D3), assignment to a place, compound
  assignment (resolution is typed), casts, struct/tuple/array/enum literals,
  `UNSAFE_BLOCK`;
- patterns: all pattern codes (the pattern compiler is a typed core).

**New codes** (no surface form): `EXIT {kind: return|break|continue, LABEL
id, VALUE}`, `LANG_PATH {item: lang-item id}` (D4), `ORIGIN` payload (D5).
The key budget comes from slot aliasing (repr audit R4a): a node carries at
most 12 keys today, so codes for `SRC_SPAN` and `ORIGIN` are freed by moving
rarely-used keys onto aliases, not by widening the bitmap.

## D3. What the HIR lowers, and what it must not

Syntactic desugarings, identical for every type:

| surface | core form | retires in sema |
|---|---|---|
| `if let` / `while let` / let-chains | `match` / `loop { match … }` | the in-sema `synth_match` use sites |
| `while c { b }` | `loop { if !c { break } b }` | `lower_while`, zero-trip special case |
| `for p in e { b }` (every spelling: ranges, arrays, slices, Vec, comprehensions) | `match IntoIterator::into_iter(e) { mut it => loop { match Iterator::next(&mut it) { Some(p) => b, None => break } } }` | `SFor`, `gen_for`, `lower_for`, 4 `lower_for_each` copies, 3 comprehension lowerers, range width tables |
| `e?` | `match Try::branch(e) { Continue(v) => v, Break(r) => return FromResidual::from_residual(r) }` | 4 `TRY_EXPR` branches, `ETry` and its 6 consumers, double lowering of the operand |
| destructuring assignment `(a, b.f, *c) = e` | `{ let (t0, t1, t2) = e; a = t0; b.f = t1; *c = t2; }` | `assign_place`; closes #531, #508 |
| implicit tail of a fn/closure body | `EXIT return <tail>` | `TAIL_EXPR` typed and closure branches, tail-match arm returns |
| expression-position `return` / `break` / `continue` | one `EXIT` node (statement vs expression differs only in the result type `!`) | `RETURN_EXPR` / `BREAK_EXPR` / `CONTINUE_EXPR` copies |
| labeled blocks, `loop` with value | one `LOOP` with label and break slot; labels resolved to ids | two label stacks; the innermost-loop fallback |
| field init shorthand, `..base` | explicit field list + base expression | shorthand arms without a slot |
| or-patterns at the top of a `let` / param patterns | `let` of a fresh binding + `match` | param-pattern special paths |
| `[v; N]`, list/map comprehensions | lang-item calls / `for` core | comprehension lowerers |

Not the HIR's job (needs types; stays in a typed core, D6):

- autoderef, autoref, method dispatch, operator overload selection, compound
  assignment on primitive vs overloaded types, coercions, unsize, closure
  capture modes, drop elaboration, integer literal types.

Rule: **a desugaring that must inspect a type is not a HIR desugaring.** rustc
follows the same rule (`for` lowers through the `IntoIterator` lang item
regardless of the iterated type; the array fast path is an optimisation).

## D4. Identities: lang items, labels, bindings

- **Lang items.** Desugarings name traits and functions through `LANG_PATH`
  ids (`IntoIterator::into_iter`, `Iterator::next`, `Try::branch`,
  `FromResidual::from_residual`, `Option::Some/None`,
  `ControlFlow::Continue/Break`), never through a spelled path. A user item
  named `IntoIterator` cannot capture a desugared `for` (hygiene), and no
  consumer re-resolves by name. The stdlib declares the mapping once
  (`#[lang = "…"]` on the item; the attribute is today "distilled" out of
  imported code per `stdlib/imported/DIVERGENCES.md` and comes back for core
  items only).
- **Labels** resolve to loop ids in the HIR. An unknown label is an error, not
  a fallback.
- **Synthesized bindings** (the `mut it` of a `for`, the temporaries of a
  destructuring assignment) are hygienic: a fresh binding id, never a `__name`
  that user code or a later pass can collide with or key on.
- Full name resolution (paths → DefIds) in the HIR is OPEN (Q1).

## D5. Spans and provenance

- **Spans.** Every AST and HIR node carries `SRC_SPAN` (repr audit R4a: global
  byte offset + length over a rustc-style `SourceMap`; reparse and expansion
  buffers are virtual files with an `expanded_from` span). Line, column and
  file are derived. A synthesized HIR node takes the span of the construct it
  desugars.
- **Origin.** Every HIR node produced by a desugaring carries
  `ORIGIN = {construct, parent span}`, with construct one of: `user`, `if_let`,
  `while_let`, `let_chain`, `for_iter`, `try`, `destructure`, `tail_return`,
  `field_shorthand`, `struct_base`, `or_fanout`, `param_pattern`, `arr_repeat`,
  `comprehension`, `macro_expansion(site)`, `metacall(site)`,
  `quote(handler)`. Typed cores add `autoderef`, `autoref`, `op_overload`,
  `compound_assign`, `drop_glue`, `stmt_temp` when they synthesize.
- Sema copies `ORIGIN` and the span onto the LIR nodes it builds (repr audit
  R3); BIR and diagnostics read them. `ORIGIN` subsumes `TRANSPARENT`,
  `COMPILER_GLUE`, `DESTRUCTURE_TMP` and `BorrowOrigin::Desugar`;
  `BorrowOrigin` keeps its two-phase-borrow meaning.
- Diagnostics carry a primary span and labelled secondary spans (R6); a
  diagnostic on a desugared node can say "in this `for` loop" from `ORIGIN`
  instead of naming a synthesized binding.

## D6. The typed cores (one implementation per rule)

Sema keeps typeck and LIR lowering. Each rule below becomes one chokepoint;
every surface form reaches it through the HIR's core forms. Contracts and the
paths each replaces are in path inventory §4.1.

| core | single entry point | typical defects today |
|---|---|---|
| C-EXP expected type | one `expected_` TypeRef scope, projections derived | hint leaks into the wrong subexpression |
| C-LIT literal inference | integer/float inference variables, defaulted once at fn close | 13 default sites, ~100 range checks |
| C-INF generic-arg inference | `infer_generic_args` | 11 raw unify loops |
| C-COE coercions | `coercion_plan(from, to)` + `expect_type` as the only applier; unsize emitted as an explicit cast | writes through `&`, wrong vtable, raw → `&mut` |
| C-PLC places | `lower_place(node, use)`, `lower_assignment`, `lower_compound` | evaluation order, double evaluation, `h.p[i]` garbage |
| C-PAT patterns | usefulness matrix + `lower_match_core`; `match` stmt = void match expr; let-else = one-arm match | non-exhaustive SIGSEGV |
| C-RES resolution | `MethodProbe`, `emit_call`, `resolve_trait_item`; callee identity recorded on LIR | wrong dispatch; the E0133 unsafe check has 8 copies and 3 receiver paths skip it |
| C-OWN ownership | `consume`, `JoinBuilder`, `reinit`, `emit_exit`; one flag carrier | double frees, leaks on expression-position exits |
| C-CLO closure env | per-capture upvar record in the closure type (ADR 0029) | stack env in a boxed closure |
| C-OBL obligations | one solver shared by sema and mono | sema/mono disagree on satisfaction |
| C-REPR codegen repr | `ReprOracle`, `place_addr`, `store_to_place`, one `fn_sig` | tuple-return ABI mismatch, `enum : u64` truncated |

Discipline, enforced by gates rather than prose:

- **Surface-code gate.** After HIR lowering, sema `bug()`s on any non-core
  node code. A form that slips past the HIR fails loudly instead of taking a
  stale branch.
- **Single-applier lint.** Each core's internals (e.g. the coercion step
  appliers, `check_variance`, raw unification) are callable only from the core;
  a lint in the style of `lint-mismatch-monopoly.sh` refuses new call sites
  elsewhere.
- **Key census.** A census of which keys each node code carries (from the
  grammar actions and from sema's puts) is checked in, so key-slot aliasing
  (D2, R4a) cannot collide silently.

## D7. Lossless contract (repr audit R1, R2, R5)

- Every AST key of every node code is, at the HIR boundary, either carried
  onto a core node, recorded in `ORIGIN`, or listed as consumed with a reason
  in a checked-in table (`hir_consumed_keys`). A key with none of the three is
  a gate failure.
- Resolved identities are primary; spellings are a side attribute (R2).
- Mono copies every metadata key it does not substitute (R5); an instance
  records `template` and `subst`.
- Facts sema has but never lowers (visibility incl. `pub(module)`, `unsafe`,
  extern ABI, import graph, assoc consts, GAT params, bounds with `assoc_eqs`
  and ref subjects) get LIR slots in the step that first needs them; the
  checked-in table lists the rest as "owed".

## Migration

Order by harm and dependency; each step lands with its acceptance fixtures
(the path-inventory probes and the feature-interaction audit twins become
`tests/logos/{pass,fail}` in the same series) and deletes the paths it
replaces. A deletion that cannot land gets a queue row with one of the three
named reasons. Each step declares a diff budget before it starts.

| step | content | depends on |
|---|---|---|
| H0 | span key + `SourceMap` (R4a); `ORIGIN` key; HIR pass as identity (core AST = AST); surface-code gate with an empty surface set; key census | — |
| S0 | one-point fixes at existing shared code (path inventory §4.2 S0, ~15 wrong-code programs) | — |
| S1 | C-REPR ABI: `fn_sig`, `ret_abi_type`, `EnumRepr` (mlir only) | — |
| S2 | HIR: tail → `EXIT return`, expression-position exits → `EXIT`, labels → ids; C-OWN exit half (`emit_exit`) | H0 |
| S3 | C-PAT + pattern half of C-REPR; HIR: if-let/while-let/let-chains/let-else → `match` | H0 |
| S4 | C-COE, unsize as explicit cast, closure escape decided at the unsize point | — |
| S5 | C-OWN rest; retire the B8 flags | S2 |
| S6 | HIR destructuring assignment; C-PLC in sema and mlir | S4 |
| S7 | C-EXP + C-LIT + C-INF | S4 |
| S8 | C-RES | — |
| S9 | C-OBL + impl identities + one mangler (PAIR, ABI bump) | S8 |
| S10 | HIR `for`, `?`, comprehensions via lang items; C-CLO rest | S8, lang items, Q2 |

Retirement is measured, not asserted: each step reports the number of sema
branches for the retired forms before and after (a grep over the surface codes
is enough once the gate exists), and the feature-interaction matrix is re-run
at S3, S6 and S10.

## Alternatives considered

- **Keep fixing per form.** The measured cost of the last month: each fix
  covers the failing form, the neighbour breaks later. The inventory shows no
  convergence without a structural change.
- **HIR as C++ structures (rustc's shape).** A second representation next to
  LIR's variant + Writ mirror, invisible to metaprog, and a big-bang switch for
  sema. Rejected in favour of the core-AST subset (D2).
- **Desugar in sema, type-directed (status quo).** This is what produced the
  per-type branches of `for` (range, array, slice, Vec, iterator) and `?`.
- **Replace LIR with a MIR-like CFG IR.** BIR already plays that role for
  borrow checking; the defects are above LIR, not in it. Out of scope.

## Consequences

- Sema shrinks: the path inventory names about 250 functions, blocks or arms
  to delete against 11 cores (estimated, not measured in lines).
- `for` over arrays and ranges goes through iterator lang items; at `-O0` it
  is slower than today's direct loop until mono/codegen adds a fast path
  (Q3).
- The AST key set changes (span, origin, aliasing): a minor version bump and a
  metaprog ABI note (numeric key readers such as
  `stdlib/mem/compiler/metaprog/derive_branch_node.logos` move with it).
- Diagnostics gain columns and secondary spans at H0; desugared constructs get
  construct-aware messages.
- Metaprog can inspect both the surface AST and the core form with the same
  API.

## Open questions (for the pair)

- **Q1. Name resolution in the HIR?** rustc resolves paths to DefIds before
  HIR. Logos resolves inside sema, interleaved with types. Proposal: HIR
  resolves only lang items, labels and synthesized bindings at first; moving
  path resolution into the HIR is a later step, decided after S8.
- **Q2. Rust-shaped `Iterator` and `Try` first?** `Iterator<Item>` and
  `Try<Continue, Residual>` take their associated types as trait parameters
  today (`stdlib/lang/iter/iter.logos`, `stdlib/lang/try_trait/try_trait.logos`).
  The `for`/`?` desugarings are simpler and closer to rustc with
  `type Item` / `type Output` / `type Residual`. Is that migration a
  prerequisite of S10, or does S10 desugar against the parameter form?
- **Q3. `for` performance at `-O0`.** Accept iterator-based loops (LLVM
  inlines them at `-O2`), or keep a range/array fast path in mono or codegen
  keyed on `ORIGIN = for_iter` plus the resolved `IntoIterator` impl?
- **Q4. Macro expansion by render-and-reparse.** `vec!`, `format!`-family
  argument handling and `include!` reparse text today. Minimum: register the
  buffers as virtual `SourceMap` files (spans point at the site). Better:
  structured expansion that builds AST directly. Which one is in scope here?
- **Q5. Do metaprog handlers see the HIR?** Expansion runs before the HIR, so
  handlers see surface AST. Reflection over typed code (later) would want the
  core form plus types. Expose the HIR to `metacall` reflection now, or later?
- **Q6. Lang-item declaration.** `#[lang = "…"]` in the stdlib (rustc's
  mechanism), or a compiler-side table keyed by canonical path? The first
  keeps the compiler free of stdlib paths; the second needs no new attribute.
