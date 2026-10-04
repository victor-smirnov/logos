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

## S8 (C-RES) — rows

Resolution is one probe and one emission point; the callee it picks is a fact
on the L-IR that later phases read instead of re-resolving.

| row | content | retires |
|---|---|---|
| (1) E0133 | every call the L-IR builder emits (Call, MethodCall) is judged ONCE, at emission, against the callee's `unsafe` — any dispatch path, by construction; a call through an `unsafe fn` pointer likewise; the fn-pointer type keeps its `unsafe` (G158-11 parsed and dropped it) | the 8+ per-path copies (`requires unsafe context` in lower_call ×2, lower_static_call, lower_method_call ×4, try_method_on_dyn/tagged, finish_generic_call, the blanket/schema paths); the holes on `impl for &T`, slice, array receivers |
| (2) divergence | a call diverges iff its callee returns `!` — read from the lowered call's type, not from a name scan | is_divergent_call_node + the stmt_always_returns / body_always_diverges_simple lambdas |
| (3) operators | one `op_lang_item` table (operator → trait, method, assign twin) | the 4 op tables (lower_binop, compound assign, mono_clone BinOp, mlir) |
| (4) MethodProbe | one receiver-step sequence (autoderef incl. Deref impls, then autoref `&` / `&mut`) producing candidates through `impl_keys_for(TypeRef)`; inherent before trait; the receiver ADJUSTMENT (deref steps, autoref mutability, reborrow) applied after the pick; a raw-pointer receiver is never dereferenced implicitly (E0599, write `(*p).m(..)`) and autoref never makes a raw pointer (a `self: *mut Self` method takes an explicit pointer) | try_method_on_{tuple,array,slice,dstref,raw_ptr} lookups, the primitive arm, the `$ref_` ladder, the main + base-name loops, the Deref-loop direct probe, the 3 wants-mut copies; moved here: `c.borrow_mut().kids.push(5)` (temporary-root DerefMut), squeue dst_method_receiver_no_reborrow_admits; B-it-09 (autoref into `self: *mut/*const Self`; stdlib collections, atomics, sync, bytes now take `&self`/`&mut self`); STATE 2026-10-03 — CLOSED BY ROW: the probe (candidates from the whole autoderef chain incl. blanket, `$ref_`, `$dyn$` keys, trait-object vtables, type-parameter / projection steps) SELECTS on every receiver kind and drives the deref walk (`pick.derefs`, DerefMut by the picked self); deleted: the struct path's main / base-name loops and autoref blocks, the primitive path's lookups and `&[u8]`→`str__` forwarding, try_method_on_{tuple,dstref}, the slice arm's and array arm's impl lookups, the Deref loop's look-ahead copies (target_method_wants_mut_self, has_receiver_method), recv_pick_rank_, LOGOS_PROBE_OFF. What stays is not a lookup: the array unsizing step, the slice / raw-pointer intrinsics (`len`, `as_ptr`, `byte_add`, …), try_blanket_method_dispatch as the blanket pick's emission. MOVED TO ROW 5: the `$ref_` arms (an `impl Tr for &T` pick is emitted as a plain call because mlir re-resolves a MethodCall by name). |
| (5) callee identity | sema records the resolved callee (symbol, impl, trait item) on every call node; mono, mlir and BIR read it | mono subst MethodCall/BinOp/Unary/Call re-dispatch, the mlir name composer + suffix scan, the 4 BIR resolvers; STATE 2026-10-03: every method call sema emits records its callee (method_call_resolved_ / method_call_named_: operators, index, Deref step, for-loop `next`, Writ schema calls), mono maps a template symbol to its instance for raw-pointer receivers and partial specializations, and mlir lowers a MethodCall to its recorded symbol only — the name composer and suffix scan are deleted (census: 139860 calls by the recorded symbol, the rest dead instantiations); `impl Tr for &T` picks are ordinary method calls (the `$ref_` arms deleted). LEFT, all trait-item identity (row 6's resolve_trait_item): mono's re-dispatch of TypeVar receivers and of BinOp/Unary/Call by composed `<Concrete>__<op>` / `<T>__<Trait>__<m>` names, BIR's trait-decl / dyn-decl resolvers (~2800 calls in the corpus), and BIR's bare-name match of the `str_from_raw` intrinsic (7086 calls). |
| (6) trait items | `resolve_trait_item(trait id, self, name)` for UFCS `Trait::m(x)` / `<T as Trait>::m` and associated consts/types | the per-form UFCS lookups; moved here: parse-target-unresolved-ice (a type variable through a method chain); STATE 2026-10-03: sema's resolve_trait_item_ answers UFCS by trait identity; an impl carries its methods' symbols (METHOD_SYMBOLS) and mono answers a bound's method at a concrete Self from the impl — struct and primitive Self, structural Self by unifying the impl's pattern (`(A...)`, `[T]`, `[T; N]`, `&T`, generic enums) and instantiating its template, a nominal enum, and a generic trait's impl selected by the call's trait arguments (`Conv<i64>` / `Conv<bool>`) — a method's own generics (`hash<H>`) fill the template's parameters the impl does not bind, and a const trait argument (`BtBranch<K, 1>`) matches by value — Self = `&str` is `&` over `[u8]` as in Rust: a `&T` pattern takes it (stdlib gained Rust's `impl<T: Eq> Eq for &T` / `&mut T`), `[u8]` is `str`, and Logos's nominal `impl … for str` answers a `&str` Self only when nothing else does (it is Rust's `impl … for &str`: `Pattern`); blanket impls by their bounds; SL-sl-02 (`PartialEq`/`PartialOrd` by the `Eq`/`Ord` impl); raw pointers compare by address (stdlib `Eq for *const T` / `*mut T`) — a bare tag (`Iterator`, `AddAssign`) takes its arity from the trait's declaration, homonyms of different arity are each asked by identity and only one may answer; trait arguments bind an impl's parameters (`impl<T> A<T> for i64`, the output `T` of a blanket `Into2<T>`); fn-pointer and raw-pointer patterns unify; a primitive is its kind across pools — census of mono's old TypeVar retarget: 32232 calls → 22812 → 18994 → 39 → **0** (pass + imported corpus and the four stdlib module builds), and the retarget is DELETED (~800 lines with `emitted_method_instance` / `declared_method_symbol`); a bound's method the impl does not answer is an ICE. LEFT in row 6: `T::m()` static calls (sema encodes `T__m`, mono re-splits it by the bindings), BIR's trait-decl / dyn-decl resolvers, BIR's bare-name `str_from_raw`, associated consts/types, parse-target-unresolved-ice. A raw-pointer impl's methods are refused at a pointer receiver (squeue #727). |

## S0–S7 gap audit (2026-10-01)

S0–S7 were closed by their ADR row tables; this audit checked them against
the evidence the rows did not list: the 2026-09-27 interaction clusters
tagged with a closed step (re-run against rustc: 89 of 128 closed, 39 open)
and the S1/S2 retirement lists of path inventory §4.2. Each open cluster was
diagnosed (root cause, owning step). A miss of a closed step is fixed now; a
cluster that needs later machinery is moved to that step's row with what the
step must provide.

S1 (C-REPR ABI): the retirement list is met (one `fn_sig` / `ret_abi_type`,
the static-global tuple patch and the per-path return typing are gone).
S2 (HIR exits): met — expression-position exits and labels are the pass's;
tail → return is sema's one judgment by decision (HIR status). The `*_EXPR`
codes sema_stmt's pre-scans still named are gone (2026-10-01): the pre-scans walk
bodies after `hir_body_`, where an expression exit reaching lowering is the
`hir_gate_` ICE.

| cluster | owner | state | observed |
|---|---|---|---|
| `closure-param-ref-to-fat-pointer` | S3 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | pairs-12#6: Logos prints 'see <pointer> 2' and [] [], rustc prints 'see 1 5 / see 5 2 / see 2 7' and [5, 7] [5, 7]. The closure is not the cause. Any by-value binder of a fat paylo |
| `no-vtable-box-box-dyn` | S4 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | Any written `Box<Box<dyn S>>` (a let annotation, a return type, a turbofish) is refused with 'the trait S is not implemented for &dyn S (required for the unsize to &dyn S, E0277)'. |
| `user-index-operand-unchecked` | S4 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `impl Index<usize> for G`, `g[true]` is accepted and runs (returns element 1); rustc gives E0308. `g["a"]` passes sema and dies in MLIR verification: ''func.call' op operand type m |
| `partial-move-array-len-accepted` | S5 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let [a, _] = arr; arr.len()` is accepted and runs; rustc gives E0382 'borrow of partially moved value'. It is worse than partial: `let b = arr; arr.len()` (a FULL move) is also ac |
| `temp-receiver-or-place-base-not-dropped` | S5 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | pairs-10#13: `println!("{}", make(7).hi())` with make() -> Box<B> never prints 'drop B 7' (leak). rustc prints '7 / drop B 7 / end'. `(*make(7)).id` also leaks. `make(7).id`, `mkb( |
| `compound-assign-uses-add` | S6 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | In `fn accum<T: AddAssign>(dst:&mut T, x:T){ *dst += x; }` with T = M (Copy), Logos calls M::add (prints 3); rustc calls add_assign (21). Without Copy Logos REFUSES ("cannot move o |
| `range-index-only-slice-array` | S6 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `&v[1..3]` on a Vec fails with "range index `[..]` requires a slice or array receiver, got Vec<i64>". `&mut a[1..3]` on an array is typed `&mut &[i64]` ("expected &mut [i64], got & |
| `refmut-method-temp-behind-ref` | S6 | PARTIAL: a projection over a VARIABLE takes DerefMut (DONE 2026-10-01); a TEMPORARY root (`c.borrow_mut().kids.push(5)`) MOVED to S8 — the receiver adjustment is decided after the method probe, without re-lowering the receiver | `let mut m = c.borrow_mut(); m.kids.push(6);` and `c.borrow_mut().kids.push(5)` are refused: "'_t10' is written or mutably borrowed behind a `&` reference"; rustc prints "2 2". Ass |
| `as-mut-underscore-cast` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let p = &mut x as *mut _; f(p)` with f(p:*mut i64): Logos refuses 'expected *mut i64, got *mut _'; rustc runs (9). The variant `let p: *const i64 = &x as *const _;` is refused too |
| `closure-param-from-expected-fn-signature` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `fn s() -> impl Fn(i64)->i64 { \|x\| x*2 }` refused: "callable '\|<error>\| -> void' does not match the signature of `Fn(i64) -> i64`"; rustc prints 8. The same happens with an exp |
| `closure-param-from-generic-bound-sibling` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `apply2(&mut k, \|v\| *v += 1)` with `fn apply2<T, F: Fn(&mut T)>(x:&mut T, f:F)` is refused ('cannot move out of v' / 'expected T, got u32'); rustc accepts and exits 5. Worse, a v |
| `collect-vec-underscore-ice` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `a.iter().map(\|x\| x*2).collect::<Vec<_>>()` compiles in sema with no diagnostic, then mlir_gen hits 'no MLIR type for InferredType _' and drops `let __fmt0_a0` (compile fails). r |
| `const-N-not-inferred-from-array` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `struct V<const N:i64>{c:[i64;N]}; let a = V{c:[1i64,2,3]}; a.n()` aborts with an ICE: 'borrow check: callee of method call .n(..) (V__n__g__ref_V$G1$N) not resolved'. Without the  |
| `copied-fold-closure-param-generic` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `v.iter().copied().fold(0i64, \|x, y\| x + y)` fails with "operator '+': type mismatch (i64 vs T)" (rustc: 8). `.cloned()` fails the same way, and pairs-1#52 reports "left must be  |
| `deferred-let-no-type-parse` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let k; k = 5i64; println!("{}", k);` gives a syntax error in Logos; rustc prints 5 |
| `generic-callable-param-to-adapter` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `fn map_all<U,F:Fn(&i32)->U>(v:&Vec<i32>,f:F)->Vec<U>{ v.iter().map(f).collect() }` refused: "could not infer type arguments for generic method 'VecIter__map'"; rustc prints [3, 6] |
| `generic-inference-through-deref-coercion` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `fn cnt<T>(xs:&[T])`, `cnt(&v)` with `v: Vec<i32>` -> 'call to cnt: could not infer all type arguments'. `fn len<T>(l:&L<T>)` with `len(t)`, `t: &Box<L<T>>` -> same. rustc accepts  |
| `generic-struct-update-infers-nothing` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `R { tag: 2, ..b }` with b: R<u8> gives no sema diagnostic, then mlir_gen internal "unknown struct 'R$G1$<error>'" and COMPILE FAILED (lrun shows only cc=1). rustc prints 2. It com |
| `generic-typevar-leaks-to-codegen` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `map_opt(Some(5i64), \|x\| x * 10)` fails in mlir_gen (unknown enum 'Option__T'); the smaller `ap(5i64, \|x\| x * 10)` with `F: Fn(T) -> i64` compiles and prints garbage (110173168 |
| `no-vtable-integer-literal-to-dyn` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let m: &dyn Num = &5;` with only `impl Num for u8`: sema accepts it, defaults 5 to i32, and mlir_gen reports "internal: no vtable for 'i32' as '&dyn Num'" (COMPILE FAILED); rustc  |
| `null-mut-generic-field` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | In `fn mk<T>() -> G<T> { G { p: null_mut() } }`, Logos reports "call to 'null_mut': could not infer all type arguments". `let p: *mut U = null_mut()` in the same body compiles. `ta |
| `out-of-range-literal-accepted` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `const K: u8 = 300;` (and `static`) compiles and prints 300 (or 44 via `let k: u8 = K`); rustc: error, literal out of range for `u8`. `let k: u8 = 300` is already refused. |
| `parse-target-through-question` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let n: i64 = s.parse()?;` in a fn returning Result<i64, ParseIntError> is refused: "'?' inner error type '<error>::Err' does not implement From". rustc prints 42. The underlying f |
| `rangeinclusive-u8-contains` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `let r = 97u8..=122u8; r.contains(&a /*u8*/)` is refused: `RangeOfIncl__contains arg 1: expected &i32, got &u8` (and `r.start()`: RangeOfIncl<i32> has no method). The half-open `97 |
| `returned-generic-impl-fn-call-void` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `fn mk() -> impl Fn(i64) -> i64 { \|x\| x + 1 }`, `mk()(5)` -> 'non-primitive cast: () as i32' at the CALLER, because the closure param is silently `<error>` and the call yields vo |
| `trait-static-call-self-inferred` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `Cfg { w: 2, ..Default::default() }` gives "call to undefined static method 'Default::default'" and "'..base' must have type 'Cfg' (got '?')"; rustc prints "2 0" |
| `untyped-closure-param-codegen-crash` | S7 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | A closure param with no annotation and no expected callable gets the error type silently. `let add = \|a: i64, b\| a + b; add(2,3)` and even `let f = \|b\| b; f(3)` fail in mlir_ge |
| `format-args-temporaries-dropped-early` | R0 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `println!("t {}", len_of(&D{id:2}))` prints 'drop 2' then 't 2'; rustc prints 't 2' then 'drop 2'. With two args Logos prints drop 2/drop 3/t 2 3; rustc prints t 2 3/drop 3/drop 2. |
| `temp-receiver-or-place-base-not-dropped/fmt-arg-temp-scope` | R0 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | pairs-9#18 (second rep of the same cluster, separate root): `println!("{}", D::mk().get())` prints 'drop 7' BEFORE '7'. rustc prints '7' then 'drop 7': format_args temporaries live |
| `rangefrom-iterator-empty` | L0 | DONE 2026-10-01 (gap round 1; fixture `gap1001_*`) | `(5i64..).next()` is None and `(5..).take(3).collect()` is [] in Logos; rustc gives Some(5) and [5, 6, 7] |
| `parse-target-unresolved-ice` | S8 | MOVED to S8 (no longer an ICE after the gap round: refused at sema; S8 carries the type variable through the method chain) | `let parsed: i32 = "42".parse().unwrap();` gives no sema error; mono skips `str__parse__g__slice_u8__<error>`, demotes main to a trap stub, and mlir reports "function 'main' was de |
| `collect-into-vec-underscore-refused` | S9 | CLOSED 2026-10-01 by the S7 gap round (hole completion through the bound; untyped closure parameter = type variable) | `let w: Vec<_> = it.collect();` and `it.collect::<Vec<_>>()` are refused: `could not infer type arguments for generic method 'MapIter__collect'` / `let 'w': expected Vec<_>, got C` |
| `no-vtable-str-literal-to-dyn` | S9 | MOVED to S9 | `let d: &dyn Display = &"lit";` and `let r = &k; let d: &dyn Display = &r;` pass sema, then mlir_gen fails: 'internal: no vtable for '&[u8]' / '&i64' as '&dyn Display''. rustc prin |
| `rawptr-write-closure-classified-fnmut` | S10 | MOVED to S10 | `let push = \|c\| unsafe { (*ps).push(c) }` (ps: *mut String) is refused: "cannot borrow 'ps' as mutable, not declared as mutable" and 'push' needs mut. rustc treats the closure as |
| `move-closure-capture-shares-slot` | ADR0029 | MOVED to ADR0029 | A `move \|\| p.w * 10` (or `t.0`) then `p.w = 7` gives 70 (rustc 20). A `move \|\| q.w` then `q = P{w:9}` gives 9 (rustc 3). The non-escaping move closure captures the field by poi |
| `move-closure-capture-shares-slot/box-capture-block-tail` | ADR0029 | MOVED to ADR0029 | A move closure owning a Box/Rc, built in a block and returned as the block's value, reads freed memory: `{ let c = Box::new(5); move \|k\| *c + k }` then `inc(1)` gives garbage (ru |
| `move-closure-capture-shares-slot/raw-ptr-escaping` | ADR0029 | MOVED to ADR0029 | An escaping move closure (returned `impl Fn` or `Box<dyn Fn>`) capturing a raw-pointer or reference PARAM stores a pointer to a heap COPY of the pointee: `move \|\| p as u64` retur |
| `untyped-closure-param-from-later-call-wrong` | ADR0029 | CLOSED 2026-10-01 by the S7 gap round (hole completion through the bound; untyped closure parameter = type variable) | `let add1 = \|b\| b + 1; println!("{}", add1(2));` gives mlir "internal: `let __fmt0_a0` initializer produced no value" (COMPILE FAILED); rustc prints 3. The sibling `let m = \|c\| |
| `collect-into-result-option` | outside-stdlib | outside ADR 0030 — squeue row collect_into_option_refused (#718) | Logos refuses `opts.iter().cloned().collect::<Option<Vec<i32>>>()` ('Option' does not implement Clone / FromIterator), `.sum::<Result<i64,_>>()` (Result not Sum) and `iter().flatte |
| `const-N-not-inferred-from-array/array-by-value-into_iter` | outside-stdlib | outside ADR 0030 — squeue row array_by_value_into_iter_refused (#719) | `[4i64, 5].into_iter().map(\|x\| x+1).sum::<i64>()` -> 'method call: receiver is not a struct (got [i64; 2])'; rustc OK. `for x in a`, `a.iter()` and a user `impl<T, const N: i64>  |
| `derive-debug-c-field-lexed-cstr` | outside-metaprog | outside ADR 0030 — squeue row derive_debug_c_field_cstr_refused (#717) | #[derive_debug] on a struct with any field whose name starts with 'c' (c, cents, count, cb) fails: `DebugStruct__field arg 2: expected &[u8], got &'static CStr`. rustc prints `A {  |
| `open-range-for-head-parse` | outside-grammar | outside ADR 0030 — squeue row open_range_for_head_parse_refused (#716); the parenthesised `for i in (0i64..)` runs since the L0 fix | `for i in 0.. { if i > 5 { break; } n += i; }` gives a syntax error in Logos; rustc prints 15. The parenthesised `for i in (0i64..)` parses but loops zero times (prints 0) |
| `u16-mul-overflow-unchecked` | outside-other | outside ADR 0030 — not a defect: overflow traps by DIVERGENCES A13 (always checked, abort) — rustc debug panics, the same verdict by the blessed model | `b * 300` with b: u16 = 255 traps (SIGILL, rc=132); rustc debug panics (rc=101). Overflow IS checked: i64 and u16 overflow both trap. |

### Long-red fixtures re-triaged (2026-10-01)

Sixteen tests had been red since the new borrow checker (ADR 0028) became the
default (lt runs 122+, 2026-09-21), carried as "pre-existing". Each was taken to
rustc 1.98.1:

| fixture | verdict | resolution |
|---|---|---|
| `borrowck-lend-flow-loop`, `issue-85581`, `issue-27282-move-ref-mut-into-guard` | MIS-PORTS — legal Rust as ported (a dropped `if cond2`; a guard type without its Drop; an unused `ref mut` binding) | upstream construct restored |
| `match-guards-partially-borrow--b` | checker hole: no fake borrow of the prefix dereferenced to reach a tested place (E0510) | fixed in the extractor |
| `issue-85581` (restored) | checker hole: a destructor inside an enum / tuple / array payload kept no loan live | fixed (`bir_drop_observes_borrow`) |
| `closure_in_generic_two_insts` | over-refusal: a closure body did not inherit the enclosing fn's `T: Copy` | fixed; control twin `closure_in_generic_non_copy_deref_fail` |
| `bc_esc_holder_residency_pershare_dangle` | use after free, no runtime check | `hold_any` asks `Writ::owns` (#437, design #464); now a runtime-panic pass fixture |
| `expr_11`, `bc_recv_addroftemp_resv_admit` (case 3), `bc_{esc_generic_monokey,fatret_nested_call,argcomp_tvbuild_byvalue_fat}_admit` | the CHECKER was right (E0382, E0499, signature elision) | fixtures corrected / moved to fail (`*_sig_fail`) |
| `issue-48238`, `bc_capret_move_addr_of_capture_fail`, `bc_capmovewalk_move_body_ret_capture_fail` | refused, new sentence closer to rustc's | `.expected` updated |
| `issue-27282-move-match-input-into-guard`, the restored `issue-27282-move-ref-mut-into-guard` | hole: a guard's closure captures a `&mut`/`ref mut` used by value BY REFERENCE (rustc: by move) | MOVED to ADR 0029 — squeue rows `guard_closure_moves_*_admits` |
| `sd_dst_view_blocks_mut_receiver` | hole: a DST method receiver is passed as `copy ar`, no reborrow, no loan | MOVED to S8 (the method probe's receiver adjustment) — squeue row `dst_method_receiver_no_reborrow_admits` |

## S0 status (2026-10-01)

S0 had no record of being done when S7 was reached; it was taken then, by
the item list of path inventory §4.2. The probe programs were gone (they
lived in /tmp), so each item got a fresh rustc twin from its description
(stdout + exit compared). Item by item:

| item | state |
|---|---|
| `intlit_fits` U64/I64 negative | DONE: a negated unsuffixed literal records its sign on its integer variable; solved to an unsigned type it is E0600 (let, argument, cast — a folded `-1` is indistinguishable from u64::MAX as an int64). `lit as T` solves an open variable to an integer target. Fixture s0_negated_literal_unsigned_refused |
| `is_stmt_only_code` + DEREF_COMPOUND; tail assignment vanishes | closed before S0 (S2/S6): `if c { x = 5 } else { x = 6 }`, `{ *p += 4 }` as a tail — twins agree |
| refuse on any decided-non-exhaustive type | closed by S3 (int / `&Enum` / `&[T]` refuse, E0004) |
| `pat_test` dyn-slice suffix | closed before S0: `[a, .., z]`, `[.., y, z]` over slices agree |
| mono `SLoop` result_type substitution | DONE: `loop { break t }` in `g<T>` failed in mlir (the loop kept `T`); fixture s0_generic_loop_break_value |
| `merge_loop_exit_moves` in for-each | closed before S0: a conditional move inside a for-each over an empty array / Vec drops once |
| closure epilogue fall-through state | closed before S0: a by-value closure parameter is dropped |
| `lower_block_expr`'s tail-move revert | done before S0 (the block-tail value keeps its move mark) |
| `is_divergent_call` consults the scope | DONE: a local binding shadows a diverging fn (`let die = \|x\| ..; { die(3) }` was typed `!`); fixture s0_local_shadows_diverging_fn |
| BREAK_EXPR mark_moved | closed by S2 (`loop { break s }` drops once) |
| `Kind::Ptr` → `ref_repr_of` in the FieldRead index copies | not reproduced: `h.p[i]` over `&[T; N]` / `&mut [T; N]` / `&[T]` / `&Vec<T>` / nested `&S` fields agree |
| (found by the gate sample) `return &K` for a const item | DONE: `&K` lowers to the promoted initializer under the declared type, one predicate for the checkers and the emitter; codegen had materialised K in the frame while BIR took the borrow as promoted (run 88, rustc 5). squeue const_item_borrow_returned_dangles_run closed |

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
- S3.4b (2026-09-28): ONE sema match lowering, `lower_match_core(node,
  form)` — form Stmt (arm bodies, values discarded), Tail (an expression arm
  IS the return), Value (arm values unify into the match's type);
  `lower_match` / `lower_match_expr` are its two carriers (SMatch /
  EMatchExpr). The scrutinee, the temporary-scrutinee hoist, the Writ hoist
  (one root helper: the expression copy still spelled the pre-Writ
  `.root()`), the arm expansion, the pattern / binding / guard phase, E0507
  at the arm, the guard-move union, the per-arm move / definite-assignment
  merge, the drop flags and exhaustiveness (one backstop over the unguarded
  arm patterns) are one code; divergence is one rule (a `!` expression arm
  diverges in both forms). A value-position block arm ending in a statement
  is `()`, as rustc types it (spec expr.match.arm-block-tail-is-value: the
  mismatch is the arm-type error; it was "block arm must end with an
  expression"). Budget ≤ −900 net; landed −916 (+563 / −1479).
- S3.4c (2026-09-28): there is no statement match. A `match` in statement
  position is an expression statement of a match (`lir::SMatch`, its view,
  mirror emitter, stmt code 14 and every consumer's case are gone; the
  compiler-built ones — `?` over a unit Ok, the `for` desugar — are
  `unit_match_stmt_`). Without `;` its type is `()` (E0308 otherwise, as
  rustc); a TAIL match is the function's `return <match>`, its arms coerced to
  the return type; one every arm of which diverges is `!` and returns nothing
  itself. lower_match_core has one arm phase (the value form). The block-tail
  rule was FOUR copies (block expression, match arm, if-expression branch,
  `unsafe { }` expression — the last without scope-exit drops); it is one,
  `lower_block_expr`, and it is Rust's: `{ e; }` is `()`, an `if` without
  `else` is a `()` expression (was refused), a block that always diverts is
  `!` (was Error, which dropped the whole match from codegen). The mlir door
  lost its statement flag; a match of type `!` generates (it returned nothing
  for a `!` type) and its dead merge is terminated. A `()` match over a
  hoisted temporary scrutinee dropped the temporary nowhere (leak). 17 pass /
  fail fixtures spelled a value as `{ x; }`; they say `{ x }` now.
- S3.4c, second half (2026-09-29): every `let PAT = e` lowers through the core's
  pattern and binding phase (lower_let_else_core, unreachable else) — the
  976-line per-shape destructure path (flat struct, tuple struct, array,
  one-variant enum) is gone. A TEMPORARY droppable rhs is owned by a synthetic
  local under ANY pattern: what the pattern did not take drops at the end of
  the statement, and a `ref` binder extends it to the block (squeue
  let_ref_mut_binding_crash closed; `let [x, _] = [mk(5), mk(6)]` dropped
  nothing of 6). Found through the one door: a struct / tuple-struct pattern
  never checked FIELD PRIVACY (a destructuring `let` over `String` compiled;
  match arms too); a named rest over an array BY VALUE is an ARRAY
  (`rest: [T; N-k]`, owning and moving its elements, `mut` honoured), as Rust
  types it; a pattern error is one error (no "refutable" on top); the arity,
  field and type-mismatch sentences are rustc's (E0527/E0528, E0023, E0026,
  E0027, E0308).
- S3.3b, first half (2026-09-29): an or-pattern is bound by the tester, not
  expanded into arms — the arm fan-out (top-level, payload and `@` ors) is
  gone; `carried_payload_sub` carries an or of structural / literal / range /
  wild / `@` alternatives. mlir: one storage per name shared by all
  alternatives (`collect_pat_bindings` reaches every nested binder), registered
  with the name's shape (`register_shared_binding`: a by-value aggregate, a
  `ref` to an aggregate — swapped `ref` binders SIGSEGVed); a guarded
  arm with ors enumerates the alternative combinations (`or_choice_`, cap
  256) — test, bind, guard, next combination on false — and generates the
  body once. BIR: an or is a nondeterministic branch whose alternatives bind
  the SAME locals, and a guard over an or is lowered twice (the false edge of
  the first run reaches a second), which is how rustc's per-alternative guard
  runs a move twice (move-in-guard-2). Each arm carries its source line
  (`ARM_LINE`): sema lowers the arm at it, so a guard's error is at the
  guard, as rustc reports it; a binding's move out of a Drop owner (E0509)
  stays at the scrutinee (`bir::Stmt::place_line`). Five fixtures pinned the
  match line for a guard / binding use; they pin rustc's line now. S3.4c's
  block-tail rule left one stdlib spelling `unsafe { s[n]; } as i32`
  (`str_len`) — a `()` cast to an integer, accepted and looping forever
  (fs_meta timed out); `() as <scalar>` is E0605 now (fixture
  cast_unit_block_to_int) and `str_len` reads `unsafe { s[n] }`.
- S3.3b, second half (2026-09-29): the synthesized payload route
  (`synth_refutable_inner`: a payload bound to a `__refut_*` synth, a guard
  `match synth { <inner> => true, _ => false }` and the body re-extraction) and
  the K4 prologue (`emit_nested_pat_destructure` / `emit_nested_variant_lets`,
  the `__pat_pld_*` synths and the NestedPatSub channel) are deleted: census
  `s3.synth.*` / `s3.nested_destructure` over the pass AND fail corpora = 0
  after the first half (an uncarried sub-pattern is reachable only over an
  Error / TypeVar field type, which binds nothing structural). −798 lines.
  The guard channel (`current_pat_refutable_guards_`) stays for its last
  producers, the `str` and byte-array const patterns. Holes the first half
  left, found by the whole-corpus census run: an or's by-value struct / tuple
  / tagged-enum payload binder copied into a storage of its own alternative
  (the shared one stayed garbage: SIGSEGV in its drop), and sema marked no
  moved leaf under an or (`Some(F::A(x) | F::B(x))` dropped the payload twice)
  — each alternative's leaves are marked now (per-tag paths). Fixture
  or_binders_move_drop (rustc twin).
- S3.3c (2026-09-29): a const in pattern position is a tester pattern, as
  rustc matches a const structurally — an array const (array literal of
  constant elements, byte string) is the array pattern of its elements (it
  was refused, "not ctfe-evaluable"; the byte-array guard path behind the
  ctfe call was dead), a `str` const a PatStr (it went through a synthesized
  `str_eq` guard). The guard channel had no producer left and is gone with
  `SLetElse.guards` (LIR key 38 retired) through mirror, mono, mlir and the
  borrow checker: no pattern carries a guard the tester does not see.
  Fixtures const_patterns_structural (rustc twin),
  const_array_pattern_length_mismatch.
- S3 closed (2026-09-29): the feature-interaction matrix re-run. The 16 S3
  clusters (and the two S3+S5 rest-pattern ones) agree with rustc 1.98.1 on
  stdout and exit code, 64 of 64 members. The whole matrix: 538 → 477
  differing members since 09-28 (62 fixed, one new — a tail `match`'s
  statement temporaries were never dropped, S3.4c; they bind to the return
  value now, fixture tail_match_temporaries_dropped).

## S4 status (2026-09-29)

Baseline: the 18 S4 clusters of the interaction audit agreed with rustc on 23
of 70 members.

- S4.1 (2026-09-29): the unsize to a trait object is an explicit cast at
  every coercion site. `coerce_arg_to_dyn` casts `&C` / `&mut C` → `&dyn Tr`
  and `Box<C>` → `Box<dyn Tr>` (the source consumed) whenever C implements Tr
  — it returned early on `types_compatible`, whose Struct → dyn arm is a
  dispatch-scoring acceptance ("impl check deferred to codegen"), so a struct
  source was never cast outside a `return` and codegen unsized it with a
  guessed vtable. The unsize and the dyn upcast are in every value
  position's mask (call and generic arguments included), and `expect_type`'s
  verdict no longer takes that acceptance for a match: an unsize left
  uncoerced is E0277. The hint positions (if / match arms, `break` values,
  tuple-literal elements) run the same applier (`coerce_arg_to_param` with
  their row of `mask_for`); `cast_to_expected_dyn`, `apply_place_coercions`
  and expect_type's Return-only Box cast are gone. The impl question asks
  the trait as it resolves in scope (a package-local `trait Hash` /
  `trait FnMut` homonym of a lang item) and knows a projection's bounds
  (`Box<T::Item>` under `type Item: X`). Deref coercion reaches a fat slot
  (`&&str` / `&String` → `&str`: the slice IS the reference — it was never
  tried) and runs at an assignment, as rustc coerces `b = rrx;`. A Deref
  impl is a candidate only for its own package's target type (the impl key
  is the target's spelling: a local `struct Vec` took the stdlib's
  `Deref<[T]>`). S4 clusters 23 → 44 of 70.
- S4.2 (2026-09-29): `types_compatible` stops accepting three mismatches
  rustc refuses or rewrites. `&T` → `*mut T` is refused (a shared reference
  never becomes a mutable raw pointer); `*T` → `&T` is gone (rustc has no raw
  → reference coercion; five stdlib sites called a method on a raw pointer and
  say `(*p).m()` now); `&Vec<T>` → `&[T]` by layout is Vec's Deref coercion,
  a rewrite — including `&mut Vec<T>` → `&mut [T]` through DerefMut. The
  subtype check's `&Vec` → `&[U]` special case went with it, and `&mut [T]`
  is invariant in T in the subtype check (it compared elements covariantly;
  the special case had been hiding that). Only `u8` casts to `char` (E0604):
  stdlib `char::from_u32`, `TryFrom<u32>`, the UTF-8 decoders and `String::pop`
  / `remove` build their checked scalar with `char::from_u32_unchecked`.
  S4 clusters 44 → 48 of 70.
- S4.3 (2026-09-29): if / match arms that differ from each other but each
  reach the expected type merge AT it (a `match` compared its arms only with
  one another: two boxed closure literals under `-> Box<dyn Fn>` were
  refused); an assignment's right-hand side carries the expectation into its
  branches (`b = if c { rrx } else { b }`); `&*p` over a raw `*const/*mut dyn
  Tr` is a borrow `&dyn Tr`, not the raw pointer again.
  **The closure-escape half is NOT moved to the unsize point, for a named
  reason (depends on another step): ADR 0029 S3/S4.** Escape is decided before
  a closure literal is lowered (a `Box` formal, a returned node, a let whose
  name is returned), and sema's capture lowering already branches on it (the
  narrow-owned captures, the move-closure copies). The unsize point is reached
  after that lowering, so moving the decision there needs the env to be the
  closure type's own value (the lifted body with an env parameter), which is
  ADR 0029's S3/S4. The remaining S4-tagged audit members belong to other
  steps: generic-constructor argument expectations (`Box::new(Box::new(s))`
  under `Box<Box<dyn Tr>>`, `Rc::new(RefCell::new(Box::new(s)))`) and literal
  inference are S7; method calls through two boxes and a `Display for &T`
  vtable are S8/S9; move-closure capture paths (`move || p.w` copies the
  field, `move || *p` captures `p`) are C-CLO (S10); `&mut **rb` over
  `&mut Box<dyn Tr>` is place typing (S6). S4 clusters 23 → 51 of 70.
- S4.4a (2026-09-29): one argument judgment. Every call-argument site
  (free fn — overloaded, exact and vararg —, closure and fn-pointer call,
  trait / inherent / struct method, static call, generic call with and
  without a pack) is `expect_arg_`: `expect_type` with the position's mask,
  variance of the COERCED argument against the instantiated formal, and one
  recursive literal-fit check over scalar / array / tuple literals. The
  eleven pre-coercions (`coerce_arg_to_param` with a site-picked flag set
  ahead of `expect_type`, or the generic call's hand chain of seven `try_*`
  steps) and nine copies of the fit check are gone. A static call checked
  neither range nor variance (`S::f(300)` over `u8` ran as 44), a closure
  call no range. Fixtures static_call_literal_overflow,
  static_call_array_literal_overflow, closure_call_literal_overflow.
  −455 lines.

- S4.4b (2026-09-29): one merge. `if` and `match` arms go through
  `lub_arms_`: each live arm is coerced to the expectation, the type is the
  arms' own when they agree, the expectation when every arm reaches it, else
  the LUB (`lub2_`: one type; fn items and non-capturing closures of one
  signature → the fn pointer, closure arms cast; `&mut T` with `&T` → `&T`;
  a literal with a number → the number). A literal arm must fit an integer
  result. The two copies had drifted — `match` had no closure rule
  (`match c { 0 => |x| x + 1, _ => |x| x * 2 }` refused), `if` refused a fn
  item beside a closure. `break` values have no LUB in rustc: each coerces
  to the type the first break fixed (a both-directions check admitted
  `break &mut a; … break &b`, E0308). Literal arms and break values that
  overflow the merged width (`if c { 1u8 } else { 300 }`) were truncated.
  Fixtures lub_arms_fn_pointer, lub_break_mut_then_shared_refused,
  lub_{if,match}_arm_literal_overflow, lub_break_literal_overflow.
- S4.4c (2026-09-29): array-literal elements are the same merge. Under a
  concrete element expectation each element goes through `expect_type`
  (ArrayElem row), variance, and the literal-fit check; without one,
  `lub_arms_`. The five hand-rolled per-kind blocks (scalar-literal
  adoption, fn pointer, slice decay, `&dyn`, `Box<dyn>`), the homogeneity
  loop and the element-0 retroactive range check are gone. `[inc, dbl]`,
  `[|x| x + 1, |x| x * 2]`, `[inc, |x| x * 3]` and `[&boxed, &5]` under
  `[&i64; 2]` agree with rustc. The battery found `&str` inside a composite
  annotation resolving to `&[u8]` (squeue row
  str_in_composite_annotation_resolves_to_u8_slice, #706; on the 09-27 binary
  too). The literal's type is its coerced elements' (the expectation carries
  the callee's region names: `pick([&V])` against `[&'a i64; 1]` lost
  `'static`), a repeat literal's value is lowered against the element
  expectation (`[[true]; 512]`), and an element is not integer-widened (an
  `i32` element under `[i64; N]` stays E0308). An unsize left undone for want
  of an impl is E0277 in `expect_type`, at every position. Two fixtures
  asserted that `[add1, sub1]` is E0308; rustc 1.98.1 accepts it (the LUB),
  and they now assert the assignment form (`let mut f = add1; f = sub1;`),
  which is E0308. Fixture lub_array_elems. S4.4a–c: −800 lines.
- S4.5 (2026-09-29): the cast table is a whitelist (`cast_permitted_`:
  numeric; bool / char / fieldless enum → integer; `u8` → char; pointer ↔
  pointer and address; reference, borrowed slice and trait object → raw
  pointer; fn → pointer / integer). A cast no rule names is E0606. `as`
  admits every coercion first (a coercion-cast: the closure → fn pointer,
  unsize, deref and reborrow steps), so `(|x| x + 1) as fn(i64) -> i64` is
  the closure coercion — it was a value cast of the closure box and the call
  through it segfaulted. `&x as i64` no longer reads through the reference
  ("T2-26"; rustc E0606), and `char as f64`, `*const T as &T` are refused.
  Raw fat pointers (`&[T] as *const [T]`, `*mut S as *mut dyn Tr`), fn item
  → fn pointer and `Box<[T; N]> as Box<[T]>` are in the table. The stdlib cast `&[T]` to `&mut [T]` five times: it builds
  the slice with the new `slice_from_raw_mut` intrinsic; four test programs
  held a Rust-invalid cast (`n as i64` over `n: &f64`, `&[T] as &mut [T]`,
  an address to a fn pointer outside `unsafe`) and say it the Rust way now.
  Logos keeps three extensions, named in the table: `bool as f32/f64` (true
  → 1.0; the `avg`-over-`bool` ruling), an address to a fn pointer inside
  `unsafe` stands for `transmute` (there is none; two runtime sites), and a
  borrowed slice casts to a thin raw pointer (`s as *const u8`, the data
  pointer): refusing it is the stdlib's move to `.as_ptr()` at up to 887
  sites, which belongs with the `str ≡ [u8]` boundary (#706). Fixtures
  cast_closure_to_fn_ptr, cast_{ref_to_int,char_to_float,ptr_to_ref}_refused.

- S4.6 (2026-09-29): arms that merge at a trait object are unsized to it,
  and a one-directional compatibility picks the type the other side coerces
  INTO. `lub2_(&B, &dyn T)` took the first (`&B`), so a fat `&a as &dyn T`
  arm was read as a thin `&B` and dispatched statically to `B::v` — a wrong
  value, silent on the 09-27 binary too; with the thin arm first the call
  segfaulted. `match`, `if` and array literals, either order, and
  `Box<B>` beside `Box<dyn T>` agree with rustc; a non-implementing arm is
  E0277. Fixtures lub_dyn_arms, lub_dyn_arm_missing_impl.

- S4.7a (2026-09-29): the census of what sema leaves implicit. mlir-gen's
  `coerce_int` / `coerce_float` / `coerce_numeric` / `coerce_to_dyn` count a
  value-changing call per CALL SITE under `LOGOS_CENSUS`
  (`mlir.<helper>.<file>:<line>`); over the pass corpus (tests/logos/pass +
  tests/spec/pass) the explicit-cast lowering and pattern constants dominate,
  and ~200 events sat at value positions: an enum payload store (97), a match
  arm result (40), an if arm (11), an assignment (16), a `*p = v` (10), a dyn
  slot (13 + 8), a let (6), an array element (6). Closed here: an enum
  payload is always judged (`expect_type` + the fit check, both constructor
  paths — it was entered only on a type mismatch, so `E::A(3)` beside an
  `i64` field kept `{integer}`), arms are coerced to the merged type,
  `coerce_arg_to_param` solves integer variables against its target
  (`Some(v) => v` under `u16`) and stamps a float literal. Payload 97 → 0,
  match arm 40 → 16, if arm 11 → 8 (the rest are diverging arms, whose value
  mlir-gen coerces as a placeholder).

- S4.7b (2026-09-29): `types_compatible`'s trait-object arm accepts only
  the unsize SOURCES — `&S` / `&mut S`, `Box<S>` at an owning `Box<dyn>`,
  `*const S` at a raw fat pointer. A struct VALUE at `&dyn Tr`
  (`get_area(r)`) was accepted ("impl check deferred to codegen") and mlir-gen
  took its address; rustc refuses it (E0308). Seven test programs passed a
  value and pass `&x` now. The literal `0` as a raw trait object stays (spec
  `coerce.cast.int-null-to-trait-object`, the null handle); S4.5's table had
  refused it and spec/pass/coerce_1 was red on main, outside the sample.
  The aggregate unsize (a tuple / array / enum literal stamped with the
  expected `dyn` type, its elements unsized by mlir-gen: 20 of the census
  events) is deferred to C-EXP (S7): the stamp is after the fact because the
  expectation does not reach the literal's elements when they are built;
  with one expected-type scope each element is coerced at construction, as
  an array literal's already is (S4.4c).

S4 row (audit §4.1 C-COE), item by item:

| item | state |
|---|---|
| unsize to dyn as an explicit cast at every site | done, S4.1 |
| `types_compatible` rewrites/acceptances rustc lacks (`&T→*mut`, `*T→&T`, `&Vec→&[T]`) | done, S4.2 |
| arms merge at the expectation | done, S4.3 |
| pre-coercions ahead of `expect_type` at argument sites; variance on the coerced type | done, S4.4a |
| pre-coercions at hint sites (if/match arm, `break` value, tuple element) | done, S4.4b (arms through `lub_arms_`; a `break` value and a tuple element coerce to their expectation, the verdict is the enclosing position's) |
| `lub_arms`: one LUB for if / match | done, S4.4b; `break` has none (rustc) |
| array literal elements under an element expectation (hand-rolled fn-ptr / slice / `&dyn` / `Box<dyn>` casts) | done, S4.4c |
| `coercion_plan(from, to, mask)` → ordered adjustment list, explicit LIR per step | the ONE applier is `coerce_arg_to_param` in canonical order, entered through `expect_type` / `expect_arg_` / `lub_arms_` (and a `break` / tuple element / cast); each step it takes emits its LIR (cast, reborrow, closure→fn ptr, unsize ECast). No separate plan object: no consumer reads a plan apart from its application (rustc's adjustment table feeds MIR building; LIR carries the casts). Explicitness is measured by the S4.7a census, row below |
| cast whitelist (`as`) | done, S4.5; slice → thin pointer kept: `str ≡ [u8]` boundary (#706) |
| remaining lenient `types_compatible` arms | trait-object arm narrowed S4.7b; by-value integer widening = blessed divergence A18 (2026-09-30), kept as the WidenInt step; `&Closure → Closure`, `*T → TaggedPtr` are the representation (dyn Fn is a closure value; tagged pointers are raw) |
| mlir per-site `coerce_to_dyn` / `coerce_numeric` | census S4.7a; enum payloads and arms made explicit (S4.7a). Left, each with its owner: aggregate unsize → C-EXP (S7, S4.7b entry); assignment and `*p = v` widths → C-PLC (S6 `lower_assignment`); a shift / mixed-width operator result → the operator core (S9); a diverging arm's placeholder is not a coercion. Converting the value sites to internal errors follows the last owner |
| closure escape at the unsize point | deferred: ADR 0029 S3/S4 |
| variance checks outside argument sites (let / assign / return / struct and enum literals, receiver) | done in effect: every site checks the COERCED value after `expect_type` (argument sites inside `expect_arg_`); the receiver and `Self`-spelled-literal checks are separate judgments (the instantiated receiver slot; the `Self` spelling), not coercions |
| slice-method arguments (`coerce_arg_to_param` before the generic finish) | C-INF (S7): their verdict is the generic finish's |

## S5 status (2026-09-29)

Discovery first: a drop-counter battery with rustc twins over the path
inventory's move/drop rows confirmed four of its defects (the probe files
of 09-26 were lost; the battery is new).

- S5.1 (2026-09-29): a block's tail value MOVES out of the block — its mark
  was reverted after the block (for the retired sema E0382's message order),
  so `let y = { x };` dropped x twice. Reassigning a variable a branch may
  have moved re-arms its drop flag and drops the old value iff the flag says
  it is there (`{ let t = rhs; if flag { drop x } x = t; flag = true; }`): the
  new value leaked, and so did an old value no branch had taken. A for-each
  body may run zero times — the iterator, array and slice paths now merge
  its moves with the zero-trip path as `for` over a range did. `..h.inner`
  reads a place base in place and moves out only the fields taken (a copy
  into a temporary left `h` owning all of `h.inner` too — two copies of that
  code, both fixed). `break v` computes v while the loop body's locals live
  and drops them after, as `return` does (`break d.v` read `d` after its drop
  — a legal program refused as a use after move; found by a second battery of
  ten early-exit / loop / pattern shapes, the other nine agreeing with
  rustc). Fixtures own_* (7, rustc twins).
- S5.2 (2026-09-29): ONE drop-flag carrier. A declared-uninit `let x: T;`
  of a droppable T gets sema's drop flag, starting clear; the reassignment
  path of S5.1 (drop the old value iff set, then set) and the scope exit's
  guarded drop do the rest. The mlir B8 machinery — a name-keyed i8 flag per
  uninit local, a pre-scan deciding flag vs static tracking, the
  slot-keyed shadow records and their snapshot/restore — is deleted
  (~260 lines); the flag is a plain `bool` local the BIR sees. The other
  population B8 had covered is sema's own: an extended temporary routed out
  of a branch arm (`let k = if c { &W {..}.y } else { .. };`) is declared
  uninit in the statement's frame — it gets the flag too, set where the arm
  builds it (two ifexpr/match-arm fixtures SIGSEGVed in the first cut). A third
  battery (closures, collections, pattern partial moves, ten shapes) agreed
  with rustc throughout. Fixture own_uninit_let_flag.
- S5.3 (2026-09-29): ONE join. `JoinBuilder` (begin / end with the branch's
  exit — falls through, returns, leaves the loop / merge / elaborate) is the
  move bookkeeping of the statement `if`, the expression `if` and `match`;
  their three hand copies of save / restore / union / reaching-branch lists
  are gone (a `match` guard's moves widen the join's pre-state; its arm values
  are addressed after the arm vector is stable). `currently_uninit_vars_` —
  the retired sema E0381's tracker, written in 12 places and read by none —
  is deleted with its loop guards and the `&&`/`||` fork. −151 lines; the
  36-program drop-counter battery agrees with rustc before and after.
- S5.4 (2026-09-29): a closure's by-value parameter moved on a branch. The
  drop flag's declaration frame was searched outward past the closure
  boundary, so `__df_0` was declared in the enclosing fn and mlir-gen found
  no such local in the lifted closure body (a compile failure, also on the
  09-27 binary); the search stops at the boundary. The closure epilogue kept
  lower_fn's retired "moved on any branch, never dropped" skip
  (`body_ever_moved_`): a parameter moved on a DIVERGING branch leaked on the
  path that did not move it. The skip is gone; `body_ever_moved_` is left
  with one role, which outer variables a closure body moves (capture modes:
  C-CLO, S10). Fixture own_closure_param_moved_on_branch.

S5 row (audit §4.1 C-OWN), item by item:

| item | state |
|---|---|
| `JoinBuilder` (one join) | done, S5.3 |
| one drop-flag carrier; B8 retired | done, S5.2 |
| `reinit(place, rhs)` | done, S5.1 (reassignment re-arms the flag and drops the old value iff set) |
| lower_block_expr tail revert | done, S5.1 |
| `currently_uninit_vars_` | deleted, S5.3 |
| `body_ever_moved_` | drop role retired S5.4; capture-mode role → C-CLO (S10) |
| `consume(place)`: the pre-gates | S5.5: `mark_moved_expr` is the one consume door and judges the type itself (move type, owning `Box<dyn>`, an FnOnce-only callable); the 18 `is_move_type(...)` conjuncts ahead of it are gone (an FnOnce callable is now consumed at those positions too). The string move paths (`move_path_of`) become the structured place of C-PLC (S6 `lower_place`) |
| 5 bind-drop-yield copies | S5.6: `bind_then_drop_` is the one `let t = v; <drops>; t` — `return` (both lowerings), `break v`, a match arm's value and a block's tail value use it |
| the recursive move walker's VarRef arm | S5.8: `mark_moved_in_expr_recursive` hands a place to `mark_moved_expr` (its VarRef copy missed an owning `Box<dyn>`, an FnOnce-only callable and a location-anchored type) |
| discard ×2 (`e;` / `let _ = e`) with private `is_place` | S5.7: one `is_place_expr_` and one `drop_discarded_rvalue_` (`{ let t = e; drop t }`) |
| statement-temporary drops ignore flags | no witness: a hoisted temporary is used exactly where it is written, so it is moved on every path to its drop or on none (4-program battery with conditional takes, rustc twins) |
| SDrop private struct / tuple field loops (mlir) | S5.7: SDrop's field step is `gen_drop_value(slot, T, no user drop, moved paths)`; the private copies (with their own def lookup) are gone |
| `epilogue(frame)` | `emit_frame_drops` is the one frame epilogue; lower_fn's and the closure literal's now take the same arguments (S5.4) |
| `exact_variant_moves_` loop copy | one loop left (the match join); the other went with S5.3 |
| `DropInfo` per concrete type (the inline `needs_drop` predicates of sema, BIR, mlir) | deferred, reason 1 (no carrier): the answer depends on the post-mono instance (a generic `T`), so the table must be written by mono and read by BIR and mlir; sema's pre-mono predicate cannot be it. The audit's own long-term item (drop elaboration from BIR move facts) |
| 8 sema E0507 sites | measured: with them off (probe `semae0507off`) the BIR alone refuses 53 of the 55 E0507 fixtures; retiring them waits on two BIR paths (doors in series): a move through a `Box` field behind `&` (bc_mvchain_box_field_ref_fail) and a runtime-index move out of an array of a Drop type (move-out-of-array-1) |


## S6 status (2026-09-29)

The HIR half landed with S2 (destructuring assignment desugars to a `let`
of fresh names plus plain assignments). C-PLC starts, as S5 did, from a
battery with rustc twins over the path inventory's place/assignment rows
(eight shapes; two agreed).

- S6.1 (2026-09-29): `place = value` evaluates the value first, then the
  place, in codegen as the BIR already did (`a[t(1)] = t(2)` and `*pick(..)
  = t(5)` ran the place first — #521's class, both doors). An indexed field
  whose slot holds a pointer is loaded first for a thin `&[T; N]` / `&mut
  [T; N]` too, not only a raw `*T` (`h.p[i]` read and wrote the field's own
  bytes; the `Kind::Ptr`-only test had two copies). `a[i] = v` through
  `IndexMut` checks v against the element type (`v[0] = true` over a
  `Vec<i64>` compiled). Operator overloading of `op=` for an enum or a
  generic `T: AddAssign` (an mlir failure; E0368's wording) is operator
  resolution — S8/S9, with the rest of the operator traits. Fixtures plc_*.
- S6.2 (2026-09-29): `recv.field[i]`'s base is ONE computation for the read
  and the address/write paths (`field_index_base_`: the field's storage, or
  the loaded pointer for `*T` / a thin `&[T; N]`, and the stride for an
  inline struct / tagged-enum pointee) — the two copies had already drifted
  once (S6.1's `Kind::Ptr`-only test). A second eight-shape battery (nested
  fields through `&mut`, `(*p).f`, Box field writes, `self.v[i] += 1`,
  `m[i][j]`, array-of-struct fields, slice fields, `RefCell::borrow_mut`,
  tuple paths) agreed with rustc throughout; out-of-bounds writes trap.
  −30 lines.
- S6.3 (2026-09-30): the place assignment asks its root on the LOWERED
  place (`place_root_name_`: through fields, tuple elements, indices and
  derefs to the local) instead of a second walk of the AST, and both
  assignment forms (to a place, to a variable) check their literals with the
  one `lit_fit_check_` (−150 lines of copies). A 20-shape battery (fields
  and indices through `&mut`, `Box`, `Vec` and `IndexMut` writes, tuple
  paths, compound ops on every place kind, evaluation order) agrees with
  rustc on stdout except `bb.0` through a `Box<(i64, i64)>`: auto-deref on
  a field access through `Box` — `lower_place`'s overloaded-`Deref` step.
  The battery also found a regression of S4.4c (nested annotated `vec!`),
  fixed in 9cbe2b1d8. A static root is read through its address
  (`*__static_addr:<sym>`), which the lowered-place walk names as such.
- S6.4 (2026-09-30): a tuple index autoderefs as a field access and an
  index do — through `Box<(A, B)>`, `Box<TupleStruct>`, `&mut Box<..>` and
  `Rc`, read and written (`emit_generic_deref_step`, DerefMut at a write).
  Each was "tuple index on non-tuple type". Fixture
  plc_tuple_index_through_deref.
- S6.5 (2026-09-30): ONE `*Assign` dispatch. `op_assign_call_` is the
  compound assignment over a struct place — to a variable, to a place, to a
  temporary-rooted place, and to an `IndexMut` element (which had none: its
  struct element went to `bin_op`): the `<Op>Assign` method on `&mut place`,
  the rhs judged as that method's argument, consumed when taken by value.
  Rust has no `a = a op b` fallback: a struct without the impl is E0368 —
  it desugared to `a = a + b` and, with no `Add` either, mlir-gen found no
  `V__add`. The impl whose `Rhs` does not match the rhs's own type is found
  when it is the type's only one, and the rhs coerces to it at the call. The
  three hand copies are gone. `v[i] += &s` over `Vec<String>` still refuses:
  `&str` as a generic argument (`AddAssign<&str>`) resolves to `&[u8]`, the
  #706 knot. Enum and generic `op=` stay with the operator traits (S8/S9).
  A `&mut [T; N]` returned by a call and indexed in place (`pick(&mut a)[2]
  += 5`) indexed a spilled copy of the reference in codegen's lvalue path —
  the write was silently lost (09-27 binary too); the reference value is the
  base, as a raw pointer's already was. Fixtures
  plc_compound_no_op_assign_refused, plc_index_through_returned_array_ref.
- S6.6 (2026-09-30): `*p op= v` (DEREF_COMPOUND) — the fourth hand copy of
  the dispatch — goes through `op_assign_call_` too (`&mut *p`, a reborrow),
  and is E0368 without the impl.

- S6.7 (2026-09-30): `*r = v` judges v against the referent, as every
  place write does. The DEREF_WRITE arm only checked variance: `*r = true`
  over `r: &mut i64` compiled and wrote a byte, `*r = 300` over `&mut u8`
  stored 44 (both on the 09-27 binary). A literal written through `*r` into
  an integer variable is one of its values (C-LIT): `let mut x = 1; ...; *r
  = 5000000000;` is "literal out of range for `i32`", as in rustc — it
  truncated (S7.1's open l5). Fixtures plc_deref_write_type_mismatch_refused,
  plc_deref_write_literal_overflow. The new verdict found pass/wany_niche_enum
  writing a stdlib `WAny` through a pointer to its LOCAL homonym: `WAny::ref_to`
  over the local enum (which has none) resolved to the stdlib type's — squeue
  row static_call_resolves_homonym_type_fn_admits (#707); the fixture builds
  its local `WAny::Ref` now.

S6 row (audit §4.1 C-PLC), item by item:

| item | state |
|---|---|
| `lower_assignment`: value first | done, S6.1 (codegen evaluates the value, then the place, both doors) |
| `lower_compound`: place once, `*Assign` by trait resolution, else E0368 | done, S6.5-6: one `op_assign_call_` (variable, place, temporary-rooted place, `IndexMut` element, `*p`); a place that calls is evaluated once (a pure place is read twice, unobservably); a primitive place is the primitive operation |
| `h.p[i]`, DST stride, `recv.field[i]` base | done, S6.1-2 |
| a place's root and literal fit checks off the lowered place | done, S6.3 |
| autoderef at a tuple index | done, S6.4 |
| index through a call's `&mut [T; N]` (EAddrOfTemp spill of a reference) | done, S6.5 |
| G167-5 (IndexMut compound) | through `op_assign_call_` for a struct element, S6.5 |
| mono BinOp compound rewrite | operator resolution after substitution — `x op= y` over a generic `T: <Op>Assign` desugars to `x = x op y` and mono calls `op`, not `op_assign`: the operator traits, S9 |
| the AST-level place analysis (`resolve_place_type`, `place_write_supported`, `check_place_writable`, the drop-before-replace segment walk) | deferred, reason 2 (doors in series): it produces the STRING move path the move facts are keyed by (`moved_vars_`, drop flags); a structured place key replaces both at once — the S5 table's drop elaboration from BIR move facts. The analysis agrees with rustc on the 29-shape batteries |
| mlir: 10 store ladders → one `store_to_place`, `var_elem_types_` | deferred, reason 3 (unpriced): sema's place assignment already emits the one form (`SDerefWrite(&mut place, v)`); the ladders serve the other write statement kinds (field / index / tuple / chain writes) and their drop semantics, with no failing witness |
| `v[i] += &s` over `Vec<String>` | the `&str` generic argument resolves to `&[u8]`: #706 |


## S7 status (2026-09-29)

Battery first (ten literal/inference shapes, rustc twins): seven refused or
mistyped a legal program because an unannotated `let` fixed its literal to
`i32` on the spot.

- S7.1 (2026-09-29): C-LIT — integer inference variables. An unsuffixed
  integer literal bound by an unannotated `let` (each leaf of a tuple let),
  or fixing a generic argument (`Some(7)`, `v.push(1)`, an enum constructor's
  payload), is an `{integer}` named `?lK` (IntLit + a name; the pool keys it
  by the name). Every IntLit rule applies to it unchanged; the first use that
  fixes an integer type solves it — `expect_type` (structurally), an
  arithmetic operand (another variable joins it, a bare literal takes it),
  the variance check. At the end of the function an unsolved one defaults to
  i32 (i64 when a literal it holds does not fit), every literal it holds
  must fit its type, and the solutions ride `infer_substs` to mono, whose
  `subst_type` replaces `?lK` in every node. `let mut t = 0; t += f_i64();`,
  `take_u8(a)`, `let y: u64 = x`, `Some(7)` read at `u16`, `(1, 2)` read at
  `(u8, i64)`, `Vec::new()` + `push(1)` read at `u64` agree with rustc.
  Fixtures lit_*.
- S7.2 (2026-09-29): every unsuffixed integer leaf of an unannotated
  `let` — in a tuple, an array, behind a `&` — is a variable, so the
  single-use stamping (`pending_lit_lets_`: the binding's FIRST use
  re-typed the literal in place, every later use saw that) is deleted. An
  array variable unsized to a slice (`let xs = [3, 9]; sum(&xs)` over
  `&[i64]`, `&mut xs` over `&mut [i64]`) solves its element at the decay —
  the slice had taken the width while the array stayed i32 (garbage at the
  wider stride) — and the method selector admits the unsolved element (a
  method arg `&ys` over `&[u16]` reported "no method"). Fixture
  lit_array_var_unsizes_to_slice. −65 lines.
- S7.3 (2026-09-30): the literal-fit check is one recursive
  `lit_fit_check_` at every judged position: call arguments (S4.4a), arms
  and array elements (S4.4b-c), enum payloads (S4.7a), both assignment forms
  (S6.3), and now `let` and `return` (a top-level scalar keeps the "literal
  value" wording those six fixtures pin). `let`'s float tuple-element
  retyping stays, and runs before the check. −160 lines of copies.
- S7.4 (2026-09-30): a struct literal's fields (three copies of the
  scalar / array / tuple fit checks) go through `lit_fit_check_`.
- S7.5 (2026-09-30): `!5` is `{integer}`, solved by its use (the unary `!`
  defaulted its literal to i32 at once; the literal-tree stamp takes `!` as
  it takes `-`); the closure-hint literal of a generic method call
  (`fold(0, ..)`) defaults through `lit_default_`. Fixture
  lit_not_literal_takes_use_type.
S7 row (audit §4.1 C-LIT / C-EXP / C-INF), item by item:

| item | state |
|---|---|
| integer inference variables; the default at the fn's close | done, S7.1-2 |
| the per-site `IntLit → i32` defaults onto `lit_default_` | done: two sites were left (`!5`, the `fold(0, ..)` closure hint), S7.5 |
| ~100 range checks onto one | done, S7.3-4 (and S4.4a-c, S4.7a, S6.3): one `lit_fit_check_`; the operator-operand, pattern and variadic-arg checks are different questions (both operands; a pattern's range) and stay |
| a literal written through `*r` truncated (l5) | done, S6.7 |
| mlir's `IntLit → i32` fallback | DONE 2026-09-30: an internal error (bug_printf, mlir_gen_exit_code census 4 -> 5). Measured before: 29 of 4565 (then 30 of 8160) pass programs brought `{integer}` to codegen; C-INF (c) below took it to 0 |
| C-EXP: one expected-type scope for the 7 hint members / 14 producers | (1) DONE 2026-09-30: `hint_expected_type_` is gone — `expected_` belongs to the node it was handed to (lower_expr takes `expect_next_` and hands nothing on; a statement has none), and the value-passing positions forward it (a block's tail, an `if` branch, a `match` arm); producers call `lower_expr_expecting` (let, let-pattern, assignment, struct field, enum payload, method / static / free-fn argument — a free-fn argument had no expectation: `first(if c { &a3 } else { &a5 })` against `&[i64]` was refused). (2a) DONE 2026-09-30: `hint_call_return_type_` is gone — a call's return-type inference reads the call node's `expected_`; parentheses and a macro's expansion (`vec![]`) pass the expectation through; `return e` expects the return type; an array literal's element expects the element type. (2b) DONE 2026-09-30: the expectation has a SHAPE half (`shape_`, which may name the callee's type parameters), taken and forwarded with `expected_`; `&e` / `&mut e` hand the operand the pointee. `hint_tuple_type_` and `hint_struct_type_` are gone: a tuple / struct literal reads its shape; a struct literal's concrete shape arguments bind its type parameters before the field values do, and a generic field is judged at the literal's arguments — task #96's thirteen-cell over-refusal is closed (fail fixture moved to pass; the two hoisted cells stay refused, as in rustc); a generic call's formals take what its expected output fixes; a free call's argument hints come from the visible local candidate of that arity (they came from the first same-named generic: `take` was the stdlib's `take<T>(*mut T)`). (2c) DONE 2026-10-01: `hint_arr_elem_type_` is gone — an array literal (and `[v; N]`'s value, an empty `[]`) reads the shape's array / slice element; a `_` hole is not concrete and fixes nothing; an argument's shape is its formal with the callee's own type parameters (and `Self`) made holes and signature lifetimes elided (a caller's `T` may share the name). (2d) DONE 2026-10-01: `hint_enum_type_` and ElemHintScope are gone — an enum literal reads the shape's enum (unless an argument is a `_` hole); a struct shape with a hole argument is not used either; a free call's / enum payload's argument shape is its formal in ONE substitution (bound parameters take their types, the rest become holes — substituting first and holing after holed a caller's homonym `T`); local inference unifies through fn signatures (`None.filter(is_even)`). (2e) DONE 2026-10-01: `hint_closure_formal_` is gone — a closure literal reads the shape when a callable peels out of it; a free call's Fn-bounded type-parameter formal shapes it by the bound, a struct field by its declared type under the literal's arguments (the struct's other parameters holes) or its Fn bound; `return` shapes by the return type also for `impl Fn`. SCAFFOLD until ADR 0029: an expectation that boxes a callable (`Box<dyn Fn ..>`) marks its subtree so a closure under it (`box_new(|x| ..)`) gets a heap env — the leaking hint carried that. No hint member is left. (3) DONE 2026-10-01: the aggregate unsize of S4.7b — every aggregate element is coerced to its slot where the aggregate is built (a turbofish call's arguments now expect their written formals too: `ident::<(&dyn Sh, i64)>((&a, 7))` was the last implicit site); census of mlir's implicit `coerce_to_dyn` over the pass corpora (logos + spec + imported): 0, the explicit-cast lowering only; the implicit arm (tuple element, enum payload, array-in-field) is a self-diagnosed malfunction. Arms of different concrete types under a `dyn` expectation merge (`match k { 0 => (&a, 7), _ => (&b, 9) }`). **C-EXP CLOSED BY ROW; S7 CLOSED BY ROW.** |
| C-INF: one generic-argument inference for the 11 raw unify loops | (a) DONE 2026-09-30: an integer variable binds a type parameter as itself (`let a = 7; wid(a); let b: i64 = a;` dispatched to the i32 impl); inside `infer_type_args` every `{integer}` of an argument type — bare, behind `&`, array / tuple leaves, `vec!` — binds a fresh variable; a literal tree meeting variables takes them at its leaves and gives them its values (range check: `id([3, 400])` then `[u8; 2]` refused); `let a = &lit` (the format-macro path) borrows a variable; `check_type_bounds` on an open variable waits for `lit_close_fn_` (rustc's deferred `{integer}` obligations) and reports at the call. `println!("{}", 5000000000)` prints 5000000000 (the Logos default). (b) DONE 2026-09-30: the raw argument->formal loops go through `unify_arg_` (60ed56b38). (c) DONE 2026-09-30: every unsuffixed literal in a function body is an integer variable carrying its value (rustc's `{integer}`), a literal outside one (const / static initializer) takes the declared type; `lit as T` takes the cast's expectation; the class default (i32, i64 when a value of the class does not fit) is per equivalence class; the solutions ride the declaration and the published template body (`INFER_SUBSTS`) so a precompiled template is instantiated with them (they never crossed a module boundary: stdlib generic literals reached codegen unsolved). mlir's `IntLit` fallback is a self-diagnosed malfunction; census 0 of 8160 pass programs |

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
