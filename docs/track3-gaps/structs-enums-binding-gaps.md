# Struct / enum / binding gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| S8-st-01 | Keyword-shaped identifier as struct field name | ✅ Closed (`77316df`, Sprint 1.1) | `new` (KW_NEW in `tools/peg_gen/grammars/logos.peg`) is reserved. The grammar permits it as a method name (logos.peg:788) but not as a struct field. Cross-link to M7-mt-01 (same root cause in trait-body context). | `struct-new-as-field-name` | `struct Foo { new: isize }` ⇒ "syntax error near 'struct'" |
| S8-en-01 | Negative integer literal as enum discriminant | ✅ Closed (`1ab8826`, Sprint 1.4) | `enum E { … X = -1 }` — Logos's enum-discriminant production didn't accept `-1`; only non-negative integer literals. | `enum-disr-val` (one variant dropped) | `enum color { …, imaginary = -1 }` |
