# `tools/dlog` — rules that hold what prose does not

A Datalog (Soufflé 2.5) offloader for questions I answer unreliably.

## Why this exists, precisely

My characteristic failure is not bad implementation — it is **premature closure
of a search**. I stop enumerating when the enumeration *feels* complete. Over one
week that produced the same defect eight times, in the compiler and in my own
rules:

| where | enumerated | missed |
|---|---|---|
| `try_path` | `VarRef`, `FieldRead` | `TupleIndex`, `Deref`, `IndexRead`, `SliceIndex` |
| `extract_borrow_place` | field, index, slice, deref | `TupleIndex` |
| `is_temporary_value_expr` | literals, aggregates, calls | `BinOp`, `Unary`, `Cast` |
| `param_names_` | "is a parameter" | "outlives the call" |
| the evidence rule | `-L imported`, `-L fail`, L1, L2 | the gates tier |
| `one_scheduler_lint` | the word "parallel" | the construct |

Datalog has no "feels complete": it runs to **fixpoint**. Saturation is exactly
the property I lack, which is why this is the right tool and not merely a
convenient one. And the rules I keep breaking all have one form — *for every X
with property P, Q must hold* — which is a Datalog rule, and whose violations
are **derived** rather than grepped. An absence has no spelling; a grep cannot
find it; a rule over a finite domain can.

## Division of labour, and where the risk actually is

**The extractor is the risk, not the rules.** Soufflé saturates or it does not;
it has nothing to lie with. The extractor does: garbage facts give confidently
wrong answers, which is worse than no tool because the form is authoritative.
So:

* **facts are mechanical and GENERAL** — `lir_facts` reports every context that
  tests any code of a named enum, and knows nothing about walkers;
* **the walker list and the projection list are CLAIMS**, and they live in the
  Datalog inputs where a rule can check them;
* **the rule checks the claims against the domain.**

Keeping the claim out of the extractor is not tidiness. The first extractor knew
about walkers, so it could certify a function complete against a domain that
shared its author's blind spot exactly.

## The extractor stopped knowing what the question is

`cxx_facts.cpp` (LibTooling) emits a FIXED relational encoding of a translation
unit and knows nothing about place walkers:

```
node(Id, Kind, Parent, Index)     decl(DeclId, Kind, QualifiedName)
loc(Id, File, Line, Col)          decl_name(DeclId, BareName)
ref(UseId, DeclId)                decl_node(Id, DeclId)
call(CallId, CalleeDeclId)        enum_member(EnumId, ConstId, Name)
type_of(Id, TypeId)               type(TypeId, Class, Name)
type_pointee / type_decl / cast_kind
cfg_block / cfg_entry / cfg_exit / cfg_edge / cfg_stmt(Fn, B, NodeId)
```

115610 nodes, 22 MB, 2.7 s for `borrow_check.cpp`. It replaced a
question-shaped extractor that needed a C++ edit and a rebuild per question —
and whose three extraction bugs were ALL bugs of OMISSION: a domain keyed on a
grep of five typed names, callers left unfiltered so `__stable_sort` came back
as a finding, dispatch attributed for `case` but not `if` so `try_path`
contributed zero edges. In none was a fact wrong; the fact was ABSENT because
nobody had asked. **A complete schema turns "I did not ask for that" into "my
query is wrong", and a wrong query is visible where an absent fact is not.**

⚠ **Completeness is affordable.** `-ast-dump=json` on one TU is 2.8 GB, but that
is JSON verbosity, not information. What had to be cut was system headers.

⚠ **Identity is the point.** Declarations are keyed by the canonical
declaration's source location — stable across TUs, so a 40-TU sweep is
joinable. Nodes are keyed per-TU. Keying on NAMES, which is all a grep can do,
is already half broken here: overloads, template instantiations, lambdas, and
five separate enums named `Code` in `lir_schema.hpp`.

⚠ **The CFG is joined, not parallel.** `cfg_stmt` names the same node ids as
`call` and `type_of`, so per-block anything is a JOIN rather than a second
extraction.

### The layers

| file | what it knows |
|---|---|
| `cxx_facts.cpp` | C++. Nothing about LIR. |
| `cxx_schema.dl` | ancestry, nearest named context, `transparent`, CFG reachability |
| `lir_dispatch.dl` | `expr_code` / `tests` / `arm_call` for a *parameterised* enum |
| `lir_questions.dl` | `ours` / `structural`, by defining file |
| `place_walkers.dl` `duty.dl` `cluster_divergence.dl` | the questions |

## Acceptance: it must reproduce a KNOWN answer

Not "does it run" — *does it say what we already know is true*. `selftest.sh`
builds a worktree of **28fc7c75**, which predates the week's place-walker fixes,
configures it (no compile), generates the headers the build makes, and requires
the chain to name exactly the six defects that were then found, fixed and
pinned — plus the coverage numbers and the derived domain size:

```
try_path              1/5   missing Deref, IndexRead, TupleIndex, SliceIndex
extract_borrow_place  4/5   missing TupleIndex
value_local_root      4/5   missing SliceIndex
domain 42 codes / 5 projections
```

`SliceIndex` in that list is the tool's first real finding: the author believed
the class closed, and a probe confirmed the hole — two mut captures of `s[0]`
for `s: &mut [i64]` did not conflict.

**This is also what licensed replacing the grep extractor with the clang one.**
The AST chain had to reproduce those six rows and those three ratios before it
was allowed to be the only one. A rewrite with no known answer is not a
replacement; it is a different tool wearing the same name. Bite-proved both
ways: exempting a real projection, and disabling one position in the `handles`
rule, each turn the selftest red — and the restore is proven by re-running it,
not assumed.

## Soufflé now, Deem later — and the reason is not speed

Soufflé is the **control**. Building straight on Deem leaves no way to tell a
wrong encoding from a Deem defect. With the same facts and rules on both,
Deem's stabilisation becomes measurable — and Deem gets a real external
customer, which finds gaps a synthetic workload will not.
