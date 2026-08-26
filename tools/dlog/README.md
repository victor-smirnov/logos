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

## The extractor is clang, and the grep version is why

`lir_facts.cpp` is a LibTooling binary. The shell version it replaced worked, and
was wrong in three ways that were measured rather than suspected:

1. **`handles` meant "the body MENTIONS the code".** `case SliceIndex: break;`
   counted as handled — the tool for finding a MISSING arm could not see an
   EMPTY one. Positions are now `case_live | case_empty | cond | mention`.
2. **The domain was unqualified.** `lir_schema.hpp` declares **five** enums named
   `Code`: expr(42), stmt(22), writ_val(9), pat(13), decl(14). A `grep -oE
   "::(TupleIndex|…)"` cannot say whose. Measured 2026-08-26: no projection name
   currently appears in another `Code`, so the grep was **lucky, not correct** —
   the day someone adds `pat::Code::Deref` it lies silently.
3. **Bodies were cut by brace balance** from a grepped definition line, and
   `try_path` is not a function: it is a `std::function` assigned a lambda on the
   next line. The awk worked by accident.

The rewrite made two errors of its own, both caught on the first run and both
the same shape as everything else here:

* fall-through case labels (`case EnumLit: case EnumLitData: <body>`) were
  scored `case_empty`, producing **three confident false findings**. A shared arm
  is still an arm; follow the chain to the body that runs.
* the lambda→variable lookup checked only the immediate parent, but assigning a
  lambda to a `std::function` inserts a `CXXConstructExpr` and a
  `MaterializeTemporaryExpr`, so **`try_path` — the walker the tool exists to
  check — emitted zero facts**. Climb the parents, with a bound.

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
