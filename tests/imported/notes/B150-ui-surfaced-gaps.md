# B150 — UI-surfaced gaps

Batch B150 imported **25 run-pass tests** across pattern (4), enum (4),
functions-closures (3), generics (3), traits (3), expr (2), and binop / coercion
/ iterators / mir / structs-enums / tuple (1 each). 4 gaps surfaced.

## Gaps

### G150-2 — ✅ silent miscompile CLOSED; full Option/Result `==` deep-deferred
`Option<T> == Option<T>` compiled, linked, and silently returned the WRONG answer
(always false — heap-pointer compare). FIXED the dangerous part: sema's
`lower_binop` routes enum `==`/`!=` to the enum's `eq` Eq impl when present
(non-generic enums work end-to-end); a C-like enum keeps the correct discriminant
compare; a payload enum with NO Eq impl now ERRORS instead of miscompiling.
Tests `pass/enum_eq_impl_operator`, `fail/enum_eq_no_impl`.

Also FIXED the enabler bug (commit "let-annotation enum type-arg pins integer-
literal payload"): `let a: MyOpt<i64> = MyOpt::MSome(3)` now binds T=i64 (was
i32-default), so a DIRECT `a.eq(&b)` / `a == b` on an annotated generic enum
works (regression `pass/generic_enum_method_let_annotation`).

REMAINING (deep): full `Option/Result ==` via a stdlib generic Eq impl. Adding
`impl<T:Eq> Eq for Option<T>` (+Result) to `stdlib/lang/cmp/cmp.logos` breaks the
STDLIB self-build — a transitive `Option<i8>::eq` instantiation is demanded but
never emitted ("does not reference a valid function"): a generic-enum-method
instantiation-on-transitive-demand gap (mono doesn't enqueue the spec). Backed
the stdlib impls out (stays green). Next: fix the mono enqueue for transitively-
demanded generic enum-method specs, then re-add the stdlib impls.

### G150-1 — `ref IDENT @ Pattern` is a parse error
`ref x` parses, and `x @ Pat` parses, but the combination `ref x @ Pat` does not.
Worked around in `nested-patterns-at`.

### G150-3 — tuple-struct numeric-field literal `S { 0: .., 1: .. }`
Construction via numeric field names (`S { 0: a, 1: b }`) is a parse error.
Dropped `numeric-fields`.

### G150-4 — `Iterator::sum()` unusable on ranges / `.iter()` chains
`(0..n).sum()` / `v.iter()....sum()` errors "receiver is not a struct". Dropped
two sum/cloned tests.
