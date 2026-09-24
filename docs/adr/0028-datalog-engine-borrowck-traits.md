# ADR 0028. A Datalog engine inside logosc: borrow checking and trait resolution as rules

Status: ACCEPTED as direction (Victor + Claude PAIR, 2026-09-18). D1-D3 below
are TAKEN; O1-O3 are OPEN. The MIR-like linearisation (slice S4) is accepted as
work to do; only its placement (O2) is open. Tracking: #419, slices S1-S7 are
sub-issues #420-#426. Scope: a small bottom-up Datalog engine in C++ inside
logosc; trait resolution and borrow checking re-expressed as rule sets over
facts extracted from the compiler's IR. Deem replaces the engine once logosc is
rewritten in Logos ([project_logosc_rewrite]); the rule files are the part that
survives that move.

## Problem

The borrow checker has no algorithm. It has a state model and a set of doors.

State, `VarState` in `src/compiler/borrow_check.cpp:644`:

```cpp
int  shared_borrows;     // count of live &T on this var
bool mut_borrowed;
int  mut_reservations;   // two-phase &mut taken in argument position
```

A loan is not an object. It has no identity, no issue point and no set of points
where it is live. So "is any shared loan of `v` live at this point" cannot be
asked. The checker approximates it by WHICH SCOPE FRAME holds a record, and every
syntactic path that mints a borrow must repeat the rule by hand.

Measured on HEAD `78ef6e87a`:

| what | count |
|---|---|
| `borrow_check.cpp` lines / of which comment | 16 900 / 8 475 |
| `report(` sites | 78 |
| `probe::on(` arms | 106 |
| `probe::census(` points | 45 |
| probe rounds under `src/compiler/probes/` | 109 |
| open rows: `bc_admits` / `soundness_queue` / `bc_admits_blocked` | 60 / 236 / 8 |

Release is lexical (scope pop) plus a sweep keyed on the source position of a
holder's last use (`(line, ordinal)` since #75). Loops use a two-pass dry run.
The tree walk is recursive over L-IR; a CFG exists only in `region_infer.cpp`,
a parallel mechanism whose `find_conflicts` appends its own diagnostics
(`borrow_check.cpp:16835`).

Worked example, #316 / `regions-adjusted-lvalue-op--c26`:

```logos
let mut v: Box<Vec<Data>> = Box::new(inner);
(*v).oh_no(&*v);   // rustc E0502; logosc admits
```

Traced in gdb on the Debug build: the receiver's `&mut v` reservation is
deposited by the `AddrOfTemp` arm (`borrow_check.cpp:15917`) into the ENCLOSING
frame before `visit_args` pushes the call frame; the argument's `&v` lands in
the call frame; the activation check (`:15218`) scans only the call frame, finds
no `&mut`, and admits. The bare-place arm (`:15994`) has the same defect. Two
doors, one rule, written twice, wrong twice.

`tools/dlog/` is a Soufflé audit of the checker's own C++: it finds which arm
forgot which case (`resv_readers.dl` names this exact class). It confirms the
diagnosis: Datalog was applied TO the checker, to enumerate doors, not IN it.

Trait resolution has a smaller version of the same shape. `trait_engine.cpp`
(219 + 159 lines, used by mono through `populate_trait_engine_`) is a top-down
memoised resolver with four rules hard-coded in C++ (direct, blanket, auto,
shape-auto) over one relation. Suspected, not measured: a `NO_IMPL` obtained
while the cycle guard cut a branch (`trait_engine.cpp:91`) is memoised
permanently (`:147`), so a pair derivable along another path can be answered
"no". Bottom-up evaluation cannot have this defect.

## Decisions

D1. Rules are TEXT, a subset of Soufflé syntax, in `.dl` files embedded into
logosc at build time and parsed at startup. The same files run under Soufflé
(`/usr/bin/souffle`, already used by `tools/dlog`) as an independent oracle in
tests.

D2. Traits first, then borrow checking. The trait port is small, has a fixed
API in mono and a whole-corpus shadow comparison against the old engine, so it
shakes out the engine on real load before the borrow checker depends on it.

D3. Borrow checker v1 covers loans AND moves/initialisation: Polonius's loan
analysis plus its initialisation analysis (`move_errors`). Drop elaboration
facts (`var_dropped_at`, `drop_of_var_derefs_origin`) are inputs to liveness,
so they are in v1 as facts; drop CODE generation stays where it is.

## Engine

`src/compiler/dl/`, no dependency on the rest of the compiler.

- Values are `uint32_t`. Symbols are interned by a caller-owned table; points,
  loans, origins, paths are dense ids the extractor assigns.
- Relations have fixed arity, stored as sorted, deduplicated row vectors, with
  indexes built lazily per bound-column prefix.
- Rules are Horn clauses: variables, constants, `_`, negated atoms, `=` / `!=`
  between terms. No arithmetic, no aggregates, no function symbols in v1.
- Stratification: SCCs of the predicate dependency graph; a negated edge inside
  an SCC is a load-time error naming the cycle.
- Evaluation: bottom-up, semi-naive within each SCC. Join order is the body
  order as written (Soufflé's default too), so a slow rule is fixed in the
  `.dl`, where it is visible.
- Provenance, from v1: each derived tuple records its FIRST derivation (rule id
  and the body tuples). `explain(tuple)` returns the tree. Diagnostics render
  from it at the edge ("loan L is live at P because ..."), not from strings
  patched into arms ([project_universal_query_compiler]: justification as data).
  Switchable off if it is measured to cost.

### Rule language (the subset)

```
.decl rel(a: T, b: T)          // T in {symbol, number}; arity fixed
.input rel                     // EDB: filled by C++ before run
.output rel                    // queryable after run
h(X, Y) :- a(X, Z), b(Z, Y), !c(X), X != Y.
fact("x", 1).                  // inline facts
// and /* */ comments
#include "file.dl"             // resolved among embedded files only
```

Anything else is a load-time error with file and line. The subset is chosen so
that every rule file is a valid Soufflé program unchanged.

### Oracle

Every shipped `.dl` has a test that runs one fixed fact set through both the
engine and Soufflé and compares every `.output` relation as a set. The fact
sets for B are dumped from real corpus programs (`LOGOS_DL_DUMP=dir`), so the
oracle sees extracted facts, not hand-written ones. The engine is never checked
against itself.

## Slice T: traits

Rules in `dl/rules/traits.dl`. Facts from mono's existing tables:
`impl_direct(Trait, Type, Impl)`, `blanket(Impl, Trait)`,
`blanket_bound(Impl, BoundTrait)`, `auto_trait(Trait)`,
`negative_impl(Trait, Type)`, `type_shape(Type, Shape)`,
`shape_auto(Trait, Shape)`, `query_type(Type)` (the finite type universe).

```
impls(T, X, I) :- impl_direct(T, X, I).
impls(T, X, I) :- blanket(I, T), query_type(X),
                  !blanket_unsatisfied(I, X), !negative_impl(T, X).
blanket_unsatisfied(I, X) :- blanket_bound(I, B), query_type(X), !has_impl(B, X).
```

(recursion through negation across `impls` / `blanket_unsatisfied` is exactly
what stratification forbids; the real encoding unrolls bounds positively. The
sketch shows the relations, not the final rules.)

Same limits as today: types are flat canonical strings, pre-substituted by the
caller. Structural matching (`Vec<T>` against `Vec<i32>`) is a later slice, not
this ADR.

Shadow mode: `LOGOS_DL_SHADOW=traits` answers every `satisfies` from both
engines and reports each disagreement once with both derivations. Gate: the
full corpus with zero UNEXPLAINED disagreements. Each disagreement is
classified; a disagreement that is the old engine's defect becomes a fixture
before the switch.

## Slice B: borrow checking

Rules: the naive Polonius variant (loans, subsets, liveness, errors) plus its
initialisation variant, ported to the subset. Inputs, Polonius names:

`cfg_edge`, `loan_issued_at`, `loan_killed_at`, `loan_invalidated_at`,
`subset_base`, `universal_region`, `known_placeholder_subset`, `placeholder`,
`var_used_at`, `var_defined_at`, `var_dropped_at`, `use_of_var_derefs_origin`,
`drop_of_var_derefs_origin`, `path_is_var`, `child_path`,
`path_assigned_at_base`, `path_moved_at_base`, `path_accessed_at_base`.

Outputs: `errors(Loan, Point)`, `subset_errors(O1, O2, Point)`,
`move_errors(Path, Point)`.

Two-phase borrows need no mechanism: the reservation issues the loan and
invalidates nothing shared; the activation point is a write access and
invalidates every conflicting live loan. For c26 the shared loan `&*v` is live
at activation, so `errors` holds; for `v.push(v.len())` the loan taken for
`len` is dead by then.

### The IR requirement: this is the main cost

Polonius's facts presuppose MIR: every evaluation step is a point, places have
projections, temporaries are explicit, every reference TYPE carries origins.
What exists:

| needed | today |
|---|---|
| points inside a statement (evaluation order) | `region_infer` CFG is per statement; c26 is ONE statement |
| places `x.f`, `*x`, `x[i]`, `t.0` | `BorrowSite.target` is a variable NAME |
| liveness per variable identity | `LiveSet` is `unordered_set<std::string>` |
| origins inside types, `subset_base` from assignment and calls | one region per borrow expression; types carry none |
| move paths with parent/child | `moved_fields` string map per var |

So slice B starts with a linearisation of L-IR bodies into a MIR-like form
used only by the fact extractor: basic blocks of simple statements over places
and temporaries, two points per statement (start, mid) as in Polonius, origins
assigned per reference position in each local's type, `subset_base` from
assignments and from callee signatures (the flow summaries in
`borrow_flow_summary.inc` already compute which argument reaches which result).
`region_infer`'s CFG builder and liveness are the starting code; its per-name
keys are not kept.

Shadow mode: `LOGOS_DL_SHADOW=bc` runs both checkers and diffs VERDICTS per
function over the whole corpus. Classes:
- new refuses, old admits: a closed hole if upstream refuses (every
  `tests/imported` program carries rustc's verdict); else a regression.
- new admits, old refuses: a regression, or an old false refusal (rustc admits).
- same verdict, different sentence: diagnostics work, not a gate.

The ledgers (`bc_admits`, `soundness_queue`) are the expected-change list,
predicted by name before the switch.

## What is removed at the end

After the switch and one release cycle: the counter state in `VarState`, the
`in_call_args_` bracket and every reservation deposit, `release_dead_borrows`,
the loop dry run, `region_infer`'s `find_conflicts`, the probe arms whose
mechanism the rules now state, and `trait_engine.{hpp,cpp}`. Each deletion
lands with the shadow diff that licenses it.

## Slices

| id | content | gate |
|---|---|---|
| S0 | this ADR | PAIR |
| S1 | engine: parser, stratifier, semi-naive eval, provenance; unit tests | Soufflé oracle on synthetic sets: transitive closure, same-generation, negation across strata, a naive Polonius fixture |
| S2 | `traits.dl` + extractor from mono tables; shadow mode | full corpus, zero unexplained disagreements |
| S3 | switch mono to the engine; delete `trait_engine` | L1 + L4 |
| S4 | L-IR linearisation + fact extractor; `LOGOS_DL_DUMP` | facts of c26, t26, `v.push(v.len())` read by hand; Soufflé runs them |
| S5 | Polonius rules (loans + init); shadow mode | whole-corpus verdict diff, classified |
| S6 | switch; diagnostics from provenance | ledgers move exactly as predicted |
| S7 | deletions (list above) | each with its diff |

## Open

O1. Performance. Naive Polonius is quadratic in the worst case. Measure S5 on
the largest function in the corpus before choosing between the naive rules and
Polonius's location-insensitive pre-pass.

O2. Where the linearisation lives: an extractor-private form (this ADR's
choice) or a real MIR stage in the pipeline that codegen also uses. Decided
after S4 shows what it costs.

O3. Generic templates. The pre-mono pass checks generic bodies in
exclusivity-only mode today. Placeholders (`universal_region`,
`known_placeholder_subset`) are Polonius's answer for named lifetimes; whether
TypeVar bodies get the full analysis is decided at S5.

O4. E0106 is not enforced. A signature with several input lifetimes, no
`&self`, and an elided lifetime in the result (`fn f(p: &Plan, g: &Writ) ->
Vec<&[u8]>`) is an error in Rust and accepted by Logos; WQL generates such
signatures. Until sema refuses them and the generators name the lifetime,
the Polonius extractor narrows the result's sources by the callee's flow
summary (a superset even when over-approximate) and otherwise ties the result
to every input. Found by the S5 shadow run, 2026-09-18.

O5. Location-sensitive subsets accept what NLL refuses. The rules are
Polonius's: a subset holds at a point and flows along the CFG only while its
origins are live. rustc's NLL gives a local's type ONE region set for the whole
body. So `fn g<'a, 'b>(x: &mut &'a i64, y: &mut &'b i64) { let mut z = x;
z = y; let _w = z; }` is refused by rustc 1.98.1 ("lifetime may not live long
enough": `z`'s invariant inner region must equal both `'a` and `'b`) and
accepted here. `z`'s first value is dead before the reassignment, so `'a ⊆ z`
at p1 and `z ⊆ 'b` at p3 never meet. Polonius accepts it too; the program is
sound (no reference outlives its referent). It is a divergence in what is
accepted, not a hole. The same holds for an `if`/`else` merging two such
values. Measured 2026-09-24 (the elided `*mut &i64` pair; the named pair
behaves identically). Not closed: NLL's location-insensitive local regions
would be a second rule set, and it would refuse only sound programs.

## Decisions taken during S4/S5 (2026-09-18)

- Signatures are read as DECLARED. Mono records the pre-substitution types
  of every instance (`DECL_RET_TYPE`, `P_DECL_TYPE`), and the extractor labels
  each origin with a written lifetime, an elided one, a type parameter's
  position, or `~` for the implicit origin of a loan-carrying type that
  declares no lifetime.
- The stdlib's reference-yielding iterators carry lifetimes as in Rust
  (Victor: "делай как в Rust"): `SliceIter<'a, T>`, `VecIter<'a, T>`,
  `VecIterMut<'a, T>`, `HashMapIter/Keys/Values<'a, K, V>`,
  `HashSetIter<'a, K>`, `BTreeMapIter<'a, K, V>`, `Chunks<'a, T: 'a>`,
  `Windows<'a, T: 'a>`.

- Raw fat pointers are their own types, as in Rust: `*const [T]`,
  `*mut dyn Tr`, `*const Dst` carry `TypeRef::RAW_FAT_BIT` and have no region
  slot. Every walker that rebuilds a Slice / DstRef / TraitObject from its
  parts keeps the bit.
- The extractor reads facts, it does not infer them (Victor, 2026-09-18:
  "no heuristics; thread what the borrow checker needs down from the upper
  levels"). A fact sema decided goes into the L-IR at the site that decided
  it. First instance: `BorrowOrigin` on `AddrOf` / `AddrOfTemp` (Explicit,
  Autoref, Reborrow, CompoundAssign, OperatorAutoref, Desugar). Two-phase
  borrows are the rustc set (AllowTwoPhase::Yes): Autoref, Reborrow,
  CompoundAssign. An explicit `&mut x` argument is not two-phase, so
  `f(&mut x, x)` is refused as rustc refuses it (E0503).
- A loan the L-IR does not contain is not invented. Where sema leaves a
  borrow to codegen (a place passed to a reference parameter or to a
  `&self` receiver without a borrow node), the extractor marks the function
  unsupported and the shadow census counts it per reason; the fix is in sema.
- Call arguments are evaluated into temporaries before the call, as MIR
  building does, so their reads precede the activation of a two-phase
  reservation.
- Open, next: the callee of a Call / MethodCall is found by name matching in
  both codegen and the extractor. Mono must write the exact instance symbol
  into the node, and both read it.
- Box's `*b` is Rust's built-in place projection (DerefMove), not a
  `Deref::deref` call: `Deref(b)` with `b: Box<T>` for a sized `T`. `(*b).s`
  is the move path `b.*.s` in sema, both checkers and BIR; the box's drop
  drops what is left of the pointee and frees the block without
  `Box::drop`. `*b = v` and `*b op= v` still go through `deref_mut()`.
- Elision is read from the DECLARED signature in the old checker too: a
  result that writes every lifetime it has, none of them a reference
  parameter's own, does not borrow through that reference
  (`fn next(&mut self) -> Option<&'a T>`). The body's flow summary no longer
  overrides the signature for a fresh borrow argument.
- The receiver autoref of a method on a generic struct without method-level
  type parameters stays in codegen until callees are exact (#83 key on the
  call); BIR counts those functions as unsupported. Making it explicit first
  exposed loans the old checker could not read the callee signature for.
- Engine: one Database per thread, cleared between functions (rules planned
  once); relation indexes are open hashing over row ids. `bir_check` 5.24 G
  -> 2.64 G instructions on a 7-line program (callgrind, whole compile
  11.7 G -> 9.1 G). Checking generic bodies once, before mono, is #434.
