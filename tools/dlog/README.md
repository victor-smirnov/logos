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

**The extractor is the risk, not the rules.** Garbage facts give confidently
wrong answers, which is worse than no tool because the form is authoritative.
So:

* **facts are mechanical** — derived from the source, keyed on the DOMAIN;
* **the walker list is a CLAIM** — a human says "these functions walk places";
* **the rule checks the claim against the domain.**

That division earned itself on the first run, twice:

1. The extractor listed the qualifier spellings it had seen — `EC::`, `Code::`,
   `ec::Code::` — and returned **zero** facts for a function that spells them
   `EK::`. The tool built to catch enumeration-instead-of-property committed it
   immediately. Fixed by keying on the domain: any qualifier, kind in the set.
2. Two names in the claim list yielded zero projections. Not an extractor bug —
   a wrong claim: `is_temporary_value_expr` classifies value-PRODUCING
   expressions and `collect_borrow_locals` walks value constructors. Neither
   decomposes a place. A wrong claim shows up as a wall of violations rather
   than passing quietly, which is the behaviour you want from a claim.

## Acceptance: it must reproduce a KNOWN answer

Not "does it run" — *does it say what we already know is true*, on two revisions:

```
HEAD~1   try_path 1/5   missing Deref, IndexRead, SliceIndex, TupleIndex
HEAD     try_path 4/5   missing SliceIndex
```

The three that were fixed that day are named, and so is a fourth **that the
author had missed while believing the class closed**. A probe confirmed it:
two mut captures of `s[0]` for `s: &mut [i64]` did not conflict. That is the
tool's first finding and it is why it exists.

## Running it

```
tools/dlog/extract.sh <outdir> src/compiler/borrow_check.cpp src/compiler/sema_expr.cpp
souffle -F <outdir> -D <outdir> tools/dlog/place_walkers.dl
cat <outdir>/coverage.csv <outdir>/spelling_keyed.csv
```

## Soufflé now, Deem later — and the reason is not speed

Soufflé is the **control**. Building straight on Deem leaves no way to tell a
wrong encoding from a Deem defect. With the same facts and rules on both,
Deem's stabilisation becomes measurable — and Deem gets a real external
customer, which finds gaps a synthetic workload will not.
