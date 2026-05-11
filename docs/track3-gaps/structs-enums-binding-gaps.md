# Struct / enum / binding gaps surfaced by Track 3 imports

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| S8-st-01 | Keyword-shaped identifier as struct field name | `new` (KW_NEW in `tools/peg_gen/grammars/logos.peg`) is reserved. The grammar permits it as a method name (logos.peg:788) but not as a struct field. Probably the field-name production binds `IDENT` strictly, so any KW_* is rejected. Closing this is a small grammar tweak (allow KW_NEW as field ident). Cross-link to M7-mt-01 (same root cause in trait-body context). | `struct-new-as-field-name` | `struct Foo { new: isize }` ⇒ "syntax error near 'struct'" |
| S8-en-01 | Negative integer literal as enum discriminant | `enum E { … X = -1 }` — Logos's enum-discriminant production doesn't accept `-1`; only non-negative integer literals. Closing requires admitting a unary `-` on the literal at discriminant position. | `enum-disr-val` (one variant dropped) | `enum color { …, imaginary = -1 }` |
