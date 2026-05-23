# B150 — UI-surfaced gaps

Batch B150 imported **25 run-pass tests** across pattern (4), enum (4),
functions-closures (3), generics (3), traits (3), expr (2), and binop / coercion
/ iterators / mir / structs-enums / tuple (1 each). 4 gaps surfaced.

## Gaps

### G150-2 — ✅ FULLY CLOSED (2026-05-22): Option/Result `==` works end-to-end
Full `Option/Result ==` now works for ANY Eq payload (primitive, nested
`Option<Option<T>>`, and owned/Drop like String) via stdlib `impl<T: Eq> Eq for
Option<T>` + `impl<T: Eq, E: Eq> Eq for Result<T, E>` + `impl Eq for String`.
Four interlocking fixes:
1. **mono naming-scheme unify** (mono_clone.cpp instantiate_enum_templates): a
   generic enum method is demanded under TWO mangled forms — cname-INSERT
   (`Option__i64__eq`, direct/operator call) and type-arg APPEND
   (`Option__eq__g__sig__i64`, trait-bound dispatch). Emit BOTH (alias copy),
   and dedup per-name (need_primary/need_alias) so a direct `Option<i64> ==`
   pre-emitting the append form doesn't suppress the insert-form alias a nested
   generic body still demands.
2. **bare enum-literal operand re-lower** (sema_expr lower_binop enum-eq): a
   bare `Option::None` RHS gets its type-args pinned from the typed LHS
   (hint_enum_type_) so both sides share the heap layout `eq` expects (was
   SIGSEGV).
3. **typevar `==`/`!=` → eq/ne desugar** (sema_expr lower_binop): `a == b` on a
   bounded `T: Eq` emits an `eq` EMethodCall (mono-dispatched) instead of a raw
   LBinOp that mlir-gen pointer-compares — fixes nested enum equality.
4. **struct-operator `==` auto-ref + ref-signature find** (sema_expr): match an
   `&self` Eq impl and borrow operands in place (place-aware take_operand_ref:
   var→addr_of, `*p`→`&*p≡p` peephole) so owned operands aren't moved/copied.
NOT done (separate feature, `ref` is the working idiom): default binding modes
for `match &enum { V(plainBinding) }` over a Drop payload — see
baghunt-match-ref-enum-default-binding-modes. Regression: pass/
stdlib_option_result_eq + stdlib_string_eq. 4582/4582.

#### (historical) G150-2 — silent miscompile CLOSED; full == was deep-deferred
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

### G150-1 — ✅ FIXED: `ref IDENT @ Pattern`
Grammar `KW_REF IDENT AT pat_single` → PAT_AT(IS_REF), before the greedy bare
`KW_REF IDENT` alt. `*x` reads through via the deref-identity shim (as with
`Some(ref v) => *v`). Regression `pass/ref_at_binding_pattern`.

### G150-3 — ✅ FIXED: tuple-struct numeric-field literal `S { 0: a, 1: b }`
Added `INTEGER COLON expr` to field_init; tuple-struct fields are named "0"/"1"
(same as `s.0`), so sema's by-name resolution maps them positionally (out-of-order
OK). Regression `pass/tuple_struct_numeric_field_literal`.

### G150-4 — ✅ NOT A COMPILER GAP: `Iterator::sum()` on ranges works with imports
`(0..n).sum()` errored "receiver is not a struct" only because the port omitted
`use logos.lang.range; use logos.lang.iter;` — range-as-VALUE needs the `RangeI64`
struct + `Iterator` in scope (they aren't in the prelude, unlike Rust; for-loop
ranges are special-cased and don't need the import). With the two `use`s,
`(1..5).sum()` / `(1..=4).sum()` work. Regression `pass/iterator_sum_range`.
(Open question, not a bug: whether range+iter should join the prelude for Rust
parity — a prelude-policy decision, deferred.)

#### G150-2 Option== — EXECUTABLE LEAD (read-only, 2026-05-22)
The transitive `Option<i8>::eq` "no valid function" is a NAMING-SCHEME MISMATCH
between two generic-enum-method emit paths:
- `instantiate_enum_templates` (mono_clone.cpp ~4913, eager during the library
  layer build): builds inst names as `cname + bare.substr(base.size())` →
  CNAME-INSERTED form `Option__i8__eq__g__…`.
- the call-site / `finish_generic_call` / scan-enqueue path: appends the
  method-level type-arg at the END → `Option__eq__g__…__i8`.
A direct user call (meqg MyOpt<i64>) works because the call-driven path emits the
matching appended-arg name; the stdlib build eagerly emits the inserted-cname
name, but the call expects the appended-arg name → mismatch. FIX: unify the two
schemes (make instantiate_enum_templates emit via the same function_symbol_name /
appended-type-arg convention the call site uses, OR vice-versa). VERIFY post-B151
by instrumenting both name builders for `Option__eq`.
