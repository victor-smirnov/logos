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

- **Spans.** Every AST and HIR node carries `SRC_SPAN` = FILE-RELATIVE byte
  offset of its first token << 23 | its length (23 bits, saturating), key code
  51 (freed by moving `KEY` onto `LHS`'s slot). The file is the document's,
  known to sema exactly as it knows `SRC_LINE`'s file — so, unlike rustc's
  global `BytePos` space, a span needs no rebasing when an AST is decoded from
  a module archive. Line and column come from the file's line table
  (`include/logos/compiler/source_map.hpp`, registered by the module loader);
  a fragment parse (macro arguments) starts at the fragment's offset
  (`RAW_OFF`, `set_first_offset`). A synthesized HIR node takes the span of
  the construct it desugars. Cross-file provenance (`include!`, metaprog
  output) needs a file id beside the offset — added with the first consumer.
  **Landed in H0 (2026-09-27)**; diagnostics print `file:line:col:` and JSON
  carries `column`.
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
| R0 | macro arguments parsed as AST; format glue, `?`, `vec!`, `matches!`, `dbg!`, panic wrappers, `include!` built structurally, no render-and-reparse (Q4) | — |
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
| L0 | `#[lang = "…"]` attribute; `lang_item → DefId` table at collection; missing/duplicate is an error; the 11 package-path sites (`k*LangPkg`) and the stdlib names the macro expansions spell bare (`String`, `Formatter`, `fmt_*`, `vec_from_arr`, `vec_from_elem`, `vec_new`; R0 pinned them in key_identity.ledger) move onto it (Q6) | — |
| S9a | Rust-shaped `Iterator { type Item }`, `Try { type Output; type Residual }`, `FromResidual<R>` | S9 |
| S10 | HIR `for`, `?`, comprehensions via lang items; C-CLO rest | S8, S9a, L0 |

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
  is slower than today's direct loop, accepted (Q3).
- The AST key set changes (span, origin, aliasing): a minor version bump and a
  metaprog ABI note (numeric key readers such as
  `stdlib/mem/compiler/metaprog/derive_branch_node.logos` move with it).
- Diagnostics gain columns and secondary spans at H0; desugared constructs get
  construct-aware messages.
- Metaprog can inspect both the surface AST and the core form with the same
  API.

## HIR status (2026-09-27)

The pass exists: `src/compiler/hir_lower.{hpp,cpp}` (`hir::Lowering`), called
on every fn / spec-fn body in sema (`hir_body_`). It rewrites persistently —
only the spine above a desugared node is copied, every untouched subtree
(and its identity: metacall sites patch the AST doc by node offset) is
shared. Synthesized nodes carry the construct's SRC_LINE / SRC_SPAN and
`ORIGIN` (key 28, formerly the dead PARENT; guarded as a global key by
logos_00_ast_key_census). Sema's branches for the forms moved in are
replaced by `hir_gate_`, an internal error — never a fallback.

Moved in so far: `if let` (statement and expression, with `else if` chains),
let-chains, `while let` (plain and chained); expression-position exits
(`return e` / `break 'l v` / `continue` → the statement form in a block; a
block ending in an exit has type `!`); destructuring assignment (`(a, (b, _),
..) = e`, `[a, b] = e`, `S { f, g: b } = e` → `{ let <pattern of fresh names> =
e; a = t0; … }`, rustc's desugaring — closed squeue destructuring_assign_drop_wrong).
The expression form's missing `else` is refused by sema from ORIGIN
(IfLetNoElse / LetChainNoElse); an undefined destructuring temporary (its
`let` already refused) is not reported twice (ORIGIN Destructure).
Entry points: fn and spec-fn bodies, reparsed bodies, macro arguments,
`include!` — the gate found the last three.
`while c { A }` → `loop { if c { A } else { break } }` (rustc's shape;
lower_while deleted): the `if` speaks as the `while` in its condition
diagnostic, and the `loop` frame knows it came from a `while`, so `break`
with a value is E0571 (as for `for`) — closed squeue
break_value_in_for_while_admitted. A metacall is opaque to the pass (the
driver patches it by offset); lower_metacall runs its inner code through the
pass at the lowering site.
Field shorthand `S { x }` → `S { x: x }` (ORIGIN FieldShorthand).
The built-in macros are expanded by the pass (ORIGIN Macro): the format family
(format!/print!/println!/eprint!/eprintln!/panic!/format_args_str!/write!/
writeln!) with a literal format string, matches!, dbg!, unreachable!/todo!/
unimplemented!. The pass parses a macro's raw argument text where it stands
(RAW_LINE / RAW_OFF) and lowers it like the body around it; sema's
`synth_format_expansion_` and its matches!/dbg!/marker branches are deleted
(the gate stands in their place). A refused call keeps its node with ORIGIN
Macro after the pass's diagnostic (reported at its own line). The format
family is declared in `logos.std.fmt` and is in scope only where that is:
the expansion block carries CALLEE, and sema asks `macro_in_scope_` there —
the same lookup a `#[fn_macro]` call makes. `panic!`'s expansion is a block
ending in `__fmt_panic(…) -> !`; the three private copies of the
diverging-call predicate were one rule written three times and are now
`is_divergent_call_node`, which also reads a block ending in a diverging call.
vec! stays in sema until S7 (its element type comes from the `let`
annotation).
Labels and loop exits are resolved in the pass, lexically, with closures and
nested fns as barriers (`resolve_exit`): E0268 / E0426 / E0267 / E0767 /
E0696 / E0695 are the pass's refusals, and a refused exit keeps its node with
ORIGIN ExitRefused (sema's older checks stand only for a FRAGMENT — a user
macro's arguments, include!, a spliced quote — whose enclosing loops the pass
cannot see). Discovery found two holes this closes: a `break` inside a
closure broke the enclosing fn's loop, and `break 'a` across a closure was
accepted (it did nothing in a `for`, looped forever in a `loop`) — closed squeue
break_through_closure_admitted. Labeled blocks `'a: { B }` parse (LABELED_BLOCK = 271) and lower to
`'a: loop { break 'a { B } }` (ORIGIN LabeledBlock). A `loop` statement ending
a block whose breaks carry a value is that block's tail expression (the pass
knows which loops a valued break targets) — `fn f() -> i64 { loop { … break
v; } }` was refused ("not all paths return a value"). Neighbours closed with it: a labeled
block's whole body is its synthesized break's value, so a loop inside a break
value became common — sema's break-frame pointer and mlir_gen's loop-stack
pointer dangled when that inner loop pushed its frame (both now indices); and
the breaks of one loop are one type — their open inference variables unify as
an `if`'s arms do (`break None; … break Some(5)` read back `None`). Labels stay strings:
after lexical resolution, sema's innermost-out search by name finds the same
loop, so an id rewrite would change no target.
Next: or-patterns / parameter patterns (sema already binds a parameter pattern
through the `let` door), `..base`; tail → return stays sema's single judgment
(a unit tail needs a type).

## S3 status (2026-09-27)

S3.1 — ONE exhaustiveness verdict. The usefulness matrix
(`ast_patterns_exhaustive`) models every column type: enums, bool, tuples,
structs, references (peeled; an EMPTY match asks the type as written —
`&Empty` is inhabited), integers and `char` (the domain split into the
elementary intervals the column's literals / ranges bound; `char` skips the
surrogates), slices and arrays (one constructor per length up to
max(longest fixed + 1, longest prefix + suffix)), strings / floats / 128-bit
integers (unbounded: only wildcard rows cover). `check_exhaustive_` is the
verdict for the statement and the expression `match` alike; a decided miss is
E0004 (naming the missing top-level variants / bool values, else the generic
sentence). The LIR-level variant checks run only where the matrix could not
decide; `refuse_uncovered_aggregate` is gone. `let` refutability is the same
matrix (`let_pattern_irrefutable_`): a one-variant enum, a full-domain range,
`Ok(v)` over an uninhabited error are irrefutable. An arm after an unguarded
`_` is rustc's warning, not an error. Or-patterns parse in a struct-pattern
field. Oracle: tests/interactions/exhaustiveness (106 probes vs rustc 1.98.1;
33 disagreements before, 3 after — all the const-path range bound, squeue
range_pattern_const_bound_refused); 30 catches became fixtures (`exh_*`);
squeue range_pattern_full_span_refused and let_single_variant_enum_refused
closed. Five pass fixtures asserted a non-exhaustive slice / array match
(rustc: E0004) and carry the missing arm now.
S3.2 (in progress) — one pattern tester, carried sub-patterns.
- A `&P` arm is tested by `pat_test` in both match codegen doors (they had a
  scalar-only copy; `&(1..=5)` never matched — squeue #517 closed).
- `n @ P` under a by-reference default binding mode binds the place's address
  (mlir `pat_bind`: the binding type `&T` over a place of type T adds the one
  layer; equal types copy the reference). let-else over `&Agg` matches the
  aggregate: test and bind see its real type (the tester was handed the
  reference type and loaded the first element as a pointer — SIGSEGV).
- `PatVariantData` carries payload SUB-PATTERNS (`SUBS`, positional, parallel to
  `BINDINGS`; a sub position binds `_` and its BINDING_TYPES entry is the real
  field type). `pat_test` tests them once the discriminant matched (a join
  block — another variant's payload bytes are never read as this one's
  fields); both `gen_match` doors and let-else test them after the disc;
  `bind_enum_payload` (the one payload binder) binds them with `pat_bind`; BIR,
  the old checker and mono's clone recurse into them. sema routes the
  sub-patterns that MOVE nothing: scalar tests (literal, range, char, bool,
  or-patterns of those, `n @ <those>`), `&x` / `&<those>` over `&T` (a by-value
  scrutinee; a move out of the reference is E0507), and structural subs under
  a by-reference scrutinee, over a Copy payload, or without binders.
  A literal / range over a place of reference type tests the referent
  (`Some(7)` over `Option<&i64>`). let-else's private variant-binding copy is
  gone (it saw BINDINGS only). The synthesized binding + arm guard remains
  only for subs whose binders move a non-Copy payload out.
  Interaction clusters literal-subpattern-under-ref-no-deref (7) and
  nested-pattern-in-variant-payload-unsupported (9) agree with rustc.
- Slice / array arms are tested by `pat_test` in both doors (length gate, then
  prefix and suffix elements, any sub-pattern kind) and bound by the one
  binder (`bind_dyn_slice_elems` for a dynamic `&[T]`). The door copies tested
  literal prefix elements only and read them before the length gate. Element
  STRIDE is the slot type (`place_slot_type`): a `&str` element is 16 bytes.
- The slot convention holds through references: `pat_test` / `pat_bind` load a
  thin `&Agg` for tuple, struct and array patterns as they did for variants
  (`peel_thin_ref_slots`), and a door hands them the scrutinee value at the
  pointee type (`door_place_type`: the value of `&T` is T's address). The
  tuple door's private Struct-only peel is gone. `n @ sub` at a door binds
  through `bind_whole_scrutinee_at` (one copy for both doors).
- A slice pattern nested in a tuple element or carried as a variant payload
  sub tests and binds like a top-level one.
  Clusters slice-pattern-subpattern-bindings-lost (6) and
  slice-pattern-element-offset (5) agree with rustc.
- A string literal is a pattern of its own (`PatStr`, LIR pattern code 13,
  key STR_VALUE): `pat_test` compares length, then the bytes (`memcmp`, only
  when the lengths agree), in every position — a whole arm, `n @ ("a" | "b")`,
  a tuple element, a variant payload (a carried sub), a slice element, a
  struct field, under `&`, if-let / let-else. The `__smatch` hoist, `str_at_arm`,
  the tuple / variant / struct-field synthesized guards (`str_eq`, and `==` for
  literal struct fields) and `make_str_eq_guard` are gone. A string literal
  over a non-`str` scrutinee is E0308 (a `String` was accepted and its hoisted
  temp double-freed). Spec: pat.str.position-restricted retired (divergence
  closed), pat.str.any-position.
- A payload sub whose binders MOVE a non-Copy value out is carried too
  (tuple, struct, variant, slice, and `n @ <structural>`): bound once, before
  the guard, like a direct payload binder. `mark_match_scrutinee_moved` records
  the partial move per leaf (`o.#<d>.<i>.<j>`, walked by `emit_moved_leaves`,
  which gained a VariantData case); `pattern_moves_out` reads SUBS; the enum
  paths one arm moves share one drop flag (`elaborate_cond_moves`), and
  `emit_frame_drops` expands one level per distinct flag. An arm with a
  refutable sub or any guard (user or synthesized) is not exact. Double free
  closed: the synthesized payload's guard copy was dropped with the body's.
  The synthesized route remains for or-patterns of binding alternatives.
- A bare pattern name that is a VALUE (a no-payload variant of the
  component's enum through `&` layers, a `use`-imported unit variant, a module
  const) is a test at every door — `bare_name_is_value_pattern_`, asked by the
  tuple door, `mint_dbm_ref` and `carried_payload_sub`; build_pattern's own
  unit-variant rule now peels `&`. Clusters unit-path-pattern-binds-under-ref
  (4) and none-arm-typed-as-scrutinee (2) agree with rustc.
- 2026-09-28: every S3 audit cluster agrees with rustc on stdout and exit
  code (unit-path, closure `|&x|`, mutable slice patterns, partial moves,
  guard-in-loop, array-of-arrays, const-generic lengths among them).
- S3.3a (2026-09-28): an `@` binder carried as a payload sub-pattern with any
  binding mode (`mut n @ 1..=5`, `ref [mut] n @ …`, `n @ _`). Census of the
  pass corpora (8907 programs, LOGOS_CENSUS buckets `s3.*`): the synthesized
  binding + guard route went from 11 entries to 0; the K4 body re-extraction
  (`emit_nested_pat_destructure`) has 0; arm fan-out still 99 (83 top-level
  or-patterns with non-scalar alternatives, 8 payload ors, 8 `@` ors). What
  still reaches the synthesized route is an or-pattern of binding
  alternatives in a multi-argument payload — refused today ("undefined
  variable"), rustc accepts.
- S3.4a (2026-09-28): ONE mlir match door (`gen_match_door`). The statement
  match, the expression match and let-else evaluate the scrutinee once into
  its PLACE (`match_scrut_place`: a by-pointer value is its storage; a place
  expression of any other type is addressed; a thin reference or fn pointer
  is spilled) and run `pat_test` / `pat_bind` per arm — the per-kind dispatch
  of the three doors (value vs slot, scalar core, enum-disc fast path,
  first-alternative or-binder, `ref` / `@` / `&` special cases) is gone, with
  the helpers only they used (`bind_match_ref_binder`, `scalar_core_scrut`,
  `ref_pat_core_scrut`, `door_place_type`, `bind_whole_scrutinee_at`).
  Budget ≤ −1800 net; landed −2361 (+237). Defects the three doors had, now
  fixture `match_one_door`: let-else tested an or-pattern's first alternative
  only (and bound variant alternatives from its payload layout — garbage),
  peeled one `&` of `&&Option`; the expression door tested `n @ Some(1)` by
  the discriminant alone; `ref mut r` over a scalar local bound a copy; a
  whole binder over a fn pointer loaded from the function's address
  (SIGSEGV). The door emits no dead-code special cases, so the metaprog and
  metacall JIT pipelines met unreachable blocks the object pipeline swept:
  the three MLIR → LLVM-dialect lowerings are one (`lower_mlir_to_llvm_dialect`).
Next: S3.4b — ONE `lower_match_core` in sema: the statement match as the
expression match (arm types unify, a block arm ending in a statement is `()`,
as rustc), a tail match as `return <match>`, let-else on the core's arm phase.
Then S3.3b — or-patterns of binding alternatives in the tester (guarded arms
retry the next alternative, alternative-dependent moves), which retires the
fan-out, the synthesized route, the guard channel and the K4 prologue.

## L0 status (2026-09-27)

Slice 1 — the mechanism and the traits. `#[lang = "name"]` (the annotation
value now takes a string) binds a stdlib item to a name in the compiler's
vocabulary (`SemaChecker::known_lang_item`: rustc's lang-item name where rustc
has one, else the item's name in snake case). A pre-pass over EVERY ast, before
collection, fills `lang_items_` (rebuilt each call, not snapshotted: cached
holders skip collection but not this pass); an unknown name is E0522, a second
item for one name E0152. The 25 compiler-known traits of `logos.lang.*` carry
the attribute; `trait_key_is_lang_item(key, lang)` and `lang_trait_package`
read the table, and the five `k*LangPkg` package constants and the
trait→package table are gone. The test stays narrow-only: an unresolvable key
matches on the item's name alone, an undeclared lang item is no trait.
Neighbour closed with it: `impl Copy for T` with a non-Copy field or variant
payload is E0204 (checked after collection; a field mentioning a type
parameter is the bound's business) — it was accepted, and a by-value use then
copied the owning field (double free, abort).
Slice 2a — the table crosses phases. `LProgram::lang_items` (lang →
`pkg::Name`) is filled by collect's pre-pass, carried by mono, and installed per
phase (`LangItemsScope`: sema, mono, borrow_check, mlir_gen, emit_module);
`type_is_lang_item(t, lang)` answers for a struct / enum TypeRef (a
package-less one on the name alone; `_exact` refuses it). Bound and converted:
`owned_box`, `rc`, `arc`, `unsafe_cell`, `phantom_pinned`, `atomic_ordering` —
the three copies of `is_stdlib_box`, `is_stdlib_rc_or_arc`, the owning-kind
table, `stdlib_smart_ptr_kind` (which probed `Rc` under `logos.mem.rc`, a
package it is not declared in), UnsafeCell's six sites (auto traits, variance,
freezability, the C ABI ×3), PhantomPinned's `!Unpin`, and the atomic
`Ordering` (logos.lang.cmp declares an `Ordering` too; it was told apart by
name).
Slice 2b — `Option` / `Result` as lang items where the COMPILER identifies
them: `?`'s operand, `partial_cmp`'s result, the synthesized `Option<…>`
(`lang_enum_`). A bare `Some` / `None` / `Ok` / `Err` stays name resolution (a
user homonym may shadow it, as in Rust). `?` on a type without a `Try` impl is
E0277 before the Try dispatch: a user `enum Result` was taken for the lang
item (two fixtures asserted that and now use the prelude's Result).
Expansion hygiene — the names the HIR's built-in macro expansions spell
(`String`, `Formatter`, `ok`, `fmt_*`, `__fmt_*`) are lang items; a TYPE_REF,
STATIC_CALL receiver or CALL callee with ORIGIN Macro resolves through the lang
table, never to a homonym in scope (a user `struct Formatter` or `fn
fmt_display` captured the expansion). The callee restriction reuses
`call_pkg_qualifier_`, now scoped to ONE name (`call_pkg_qualifier_name_`):
the call's arguments are lowered while the qualifier stands, and the old
package-wide filter refused `buf.as_str()` inside `__fmt_println(…)`. Found on
the way (a separate defect, rowed): a user type named like a stdlib type breaks
linking to that type's methods (squeue user_type_homonym_of_stdlib_type_link_refused).
`vec!` stays in sema until S7.
Next slice (optional, low value): the cmp / str / range / slice
helper fns, Vec / HashMap / String, and the names the HIR expansions spell
(`Formatter`, `fmt_*`, `__fmt_*`) through `LANG_PATH` (census:
docs/audit/2026-09-27-lang-item-census.md).

## R0 status (2026-09-27)

Landed (census `docs/audit/2026-09-26-reparse-census.md` site ids):

- M1/M4: metacall thunk text renders types in source form (`Option<i64>` no
  longer prints `Option`) and negative literals parseably.
- G1: peg_gen stamps `RAW_LINE` (line of the opening delimiter) whenever a raw
  group is captured into `RAW_TEXT`; parsers gain `set_first_line`; exported
  entries `macro_args` (`name = expr` is a FIELD_INIT), `matches_args`,
  `vec_repeat_args`. `SemaChecker::parse_macro_args_` parses a macro's
  arguments where they stand.
- U9/U10: `#[fn_macro]` arguments (assert!, assert_eq!, …) keep their lines.
- U1: the format family is built as AST over the parsed arguments
  (`synth_format_expansion_`); arguments are evaluated once, left to right,
  borrowed, as format_args!; any expression form is an argument.
- U4–U7: `vec!` = `vec_from_arr([…])` (elements move; no `Copy` bound),
  `vec![e; n]` = `vec_from_elem(e, n)`, `matches!`, `dbg!`, `unreachable!` /
  `todo!` / `unimplemented!` built over parsed arguments.
- U8: `include!` parses the file as one expression from its own first line.
- The EOF token's text is an in-source empty view (#695: the null view indexed
  the memo tables out of range).

Not done in R0, by reason:

- U2/U3 (`?` over a user `Try` type / the `Box<dyn>` conversion arm):
  superseded by S10, which replaces `?` wholesale with the lang-item
  desugaring; a structured interim needs the return type as an AST type node,
  which S10 does not use. The double report of an erroneous operand is closed.
- M2/M3/M6/M7 (metacall block thunk, Writ-returning thunk, container factory
  config text, deem bind text), D1 (`--gen-dir -g` body check): metaprog and
  driver text paths, not user-code desugarings; they follow R0.
- Include file attribution: nodes of an included file still name the
  includer as their file until spans carry a file id (H0).

## Decisions taken in review (2026-09-26, Victor)

- **Q1. Path resolution after S8.** The HIR resolves only lang items, labels
  and synthesized bindings. Moving path → DefId resolution into the HIR is
  decided after S8.
- **Q2. Follow Rust.** `Iterator` takes `type Item`, `Try` takes
  `type Output` / `type Residual` (with `FromResidual<R>` as in Rust). This is
  a new step, S9a, and a prerequisite of S10.
- **Q3. No `-O0` fast path.** `for` over arrays and ranges goes through the
  iterator lang items; `-O0` speed does not matter within reasonable limits.
  No `ORIGIN`-keyed special case in mono or codegen.
- **Q4. DECIDED: step R0 is the FIRST step, before H0** (census:
  `docs/audit/2026-09-26-reparse-census.md`). Root cause: the grammar keeps macro arguments only
  as `RAW_TEXT`, so `vec!`, `matches!`, `dbg!`, the `panic!`-wrapper macros,
  every `#[fn_macro]` (incl. `assert!`) and the format family wrap text in a
  fake `fn __f()` and reparse it; every macro argument reports line 2
  (measured, also in the `-g` line table). R0 = parse macro arguments as AST,
  then build the user-code desugarings structurally (format glue, `?`,
  `vec!`, `matches!`, `dbg!`, panic wrappers, `include!`). Doing H0 first
  would need a virtual-file workaround on each of these sites that R0 then
  deletes, and the rendered ones (format glue, `?`) cannot be mapped back to
  user source at all. Generated code (deem pipeline, Logos-side emitters,
  test runner) stays textual and gets virtual `SourceMap` files at H0.

- **Q5. Deferred with a ticket.** Metaprog handlers see the surface AST; HIR
  exposure to `metacall` reflection is tracked in #649.
- **Q6. DECIDED: `#[lang = "…"]` in the stdlib (rustc's mechanism), as in
  Rust.** Today the compiler keys lang items on package-path strings
  (`kCopyLangPkg = "logos.lang.clone"`, `kDropLangPkg`, `kDerefLangPkg`,
  `kFnLangPkg`, `kCmpLangPkg`; 11 use sites in `sema_impl.hpp`). That is an
  identity carried as spelling: moving an item between stdlib packages
  (module stratification) silently breaks it. With the attribute, the item
  declares its role once, the compiler collects a `lang_item → DefId` table
  at collection, a missing or duplicated lang item is a gate error, and the
  existing path tables migrate onto it.

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
