# Logos baghunt — feature-group index

Result of Phase 1A of the [refactor + adversarial bag-hunt](../../../.claude/plans/memoized-whistling-cherny.md). Single point of navigation across feature-groups, the grammar productions they cover, the reference doc that documents them, and the per-feature bug catalog.

## Conventions

- **Grammar source**: [tools/peg_gen/grammars/logos.peg](../../tools/peg_gen/grammars/logos.peg). Production names below are quoted verbatim from the `%rules` block.
- **Reference docs** live in [docs/language/reference/](../language/reference/). Where coverage is incomplete it's marked **GAP**.
- **Bug catalog** files have format `docs/baghunt/<group-slug>.md`. They don't exist yet on Phase 1A; Phase 2 fills them.
- **Implementation entry points** are the primary `src/compiler/*.cpp` files for each group. Many features cross several files; only the dominant ones are listed.

Counts: 159 productions in the grammar's `%rules`. Groups below cover all of them.

## Feature groups

| # | Group | Productions | Reference | Catalog | Notes |
|---|---|---|---|---|---|
| 1 | [Module & Visibility](#1-module--visibility) | 5 | items.md (top), no module-resolution doc | `module-visibility.md` | Resolution semantics are a known **GAP** — likely a new `modules.md`. |
| 2 | [Items: structs / enums / datatypes / traits](#2-items) | 24 | items.md | `items.md` | Largest group; covers all top-level definitions and impl blocks. |
| 3 | [Functions & Methods](#3-functions--methods) | 8 | items.md, statements.md | `functions.md` | `extern fn`, methods on structs/datatypes/traits, static fns. |
| 4 | [Const & Type aliases](#4-const--type-aliases) | 2 | items.md | `const-alias.md` | `pub const X<...>: T = expr;`, `pub type Foo<...> = T;`. |
| 5 | [Annotations & Meta blocks](#5-annotations--meta-blocks) | 4 | attributes.md (partial) | `attributes.md` | **GAP**: registry of all `#[...]` forms + meta `@{...}`. |
| 6 | [Type System](#6-type-system) | 24 | types.md | `types.md` | Includes pkg-aware lookup, `<type:CFG.SLOT>`, `typeof`, `@{...}` at type-position (now retired). |
| 7 | [Generics & Bounds](#7-generics--bounds) | 9 | generics-traits.md | `generics.md` | Type/lifetime/const params, trait bounds, where-clauses, super traits. |
| 8 | [Statements & Assignments](#8-statements--assignments) | 30 | statements.md | `statements.md` | **GAP**: exhaustive compound-assign matrix (field/chain/deref/index permutations). |
| 9 | [Pattern Matching](#9-pattern-matching) | 11 | patterns.md | `patterns.md` | Struct/enum/slice/tuple/Writ-map patterns, bindings, guards. |
| 10 | [Expressions & Precedence](#10-expressions--precedence) | 13 | expressions.md | `expressions.md` | The log/cmp/bitwise/add/mul/cast/unary/atom/primary chain. |
| 11 | [Literals (struct/arr/tuple/closure)](#11-literals-non-writ) | 10 | expressions.md (partial) | `literals.md` | Struct lits, array lits, fill-arrays, list-comp / map-comp, closures. |
| 12 | [Writ Literals](#12-writ-literals) | 9 | writ.md | `writ.md` | `@{...}`, `@[...]`, typed_array / typed_map / list_comp / map_comp. |
| 13 | [Metaprog](#13-metaprog) | 8 | metaprog.md | `metaprog.md` | quote_*!, template, metacall, instantiate. **GAP**: quote-form semantics depth. |
| 14 | [Lexical](#14-lexical) | 0 (token-level) | lexical.md | `lexical.md` | Tokens, comments, identifiers; covered in grammar's `%tokens` block, not `%rules`. |

Total: 159 productions classified. Lexical group has no `%rules` productions; it lives in `%tokens` and is documented separately.

---

## 1. Module & Visibility

**Productions** (5):

| Production | Line in logos.peg | Purpose |
|---|---|---|
| `module` | 395 | Top-level: `package <path>; <use*> <item*>` |
| `path_dot_ident` | 406 | `.IDENT` segment in dotted package paths |
| `any_use_decl` | 398 | Dispatcher: pub_use_decl / use_decl |
| `pub_use_decl` | 400 | `pub use <path>;` (re-export) |
| `use_decl` | 403 | `use <path>;` |

**Reference**: [docs/language/reference/items.md](../language/reference/items.md) covers `module`/`use` superficially. **GAP**: deep semantics of name resolution, visibility scope, re-export propagation, `use` ordering, and how multi-file packages work (`logos.module` files). Phase 1B may add `docs/language/reference/modules.md`.

**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — `cur_imports_`, `cur_package_`, `pkg_reexports_`
- [src/compiler/sema_impl.hpp](../../src/compiler/sema_impl.hpp) — `find_struct_by_name`, `lookup_struct_pkg`, `sema_key`

**Memory**: see [feat_pub_const](../../../.claude/projects/-home-victor-devel-logos/memory/feat_pub_const.md), [feat_definition_centric_tu](../../../.claude/projects/-home-victor-devel-logos/memory/feat_definition_centric_tu.md), [feat_type_uid_pkg_skip_bug](../../../.claude/projects/-home-victor-devel-logos/memory/feat_type_uid_pkg_skip_bug.md).

---

## 2. Items

**Productions** (24):

| Production | Line | Purpose |
|---|---|---|
| `item` | 409 | Top-level dispatcher |
| `pub_struct_def` / `struct_def` | 805/812 | `struct Foo { ... }` |
| `pub_struct_inst` / `struct_inst` | 799/802 | `struct Foo<...>;` (forward / explicit instantiation hint) |
| `pub_enum_def` / `enum_def` | 538/547 | `enum Foo { ... }` (with optional type-code attr / payload) |
| `variant_list` / `variant_def` / `variant_payload_list` | 556/562/559 | Enum variant syntax |
| `pub_datatype_def` / `datatype_def` | 773/776 | legacy `KW_EIDOS` datatype keyword — **being removed**; canonical form is `#[zoned] struct` |
| `pub_datatype_inst` / `datatype_inst` | 782/785 | legacy `eidos Foo<...>;` — use `struct Foo<...>;` |
| `pub_trait_def` / `trait_def` | 597/622 | `trait Foo : Bound { ... }` |
| `pub_trait_inst` / `trait_inst` | 791/794 | `trait Foo<...>;` |
| `pub_genos_def` / `genos_def` | 587/592 | `genos Foo { ... }` — computable form-specification (NOT a trait); see [overview](../language/overview.md) |
| `trait_kw` | 583 | `trait` keyword |
| `super_list` | 576 | `: Trait1 + Trait2` super-trait list |
| `trait_method` | 647 | Method declaration inside trait |
| `impl_block` | 690 | `impl Trait for Type { ... }` |
| `impl_item` | 734 | Item inside impl (method / static) |
| `meta_block` | 821 | `meta @{...}` block on struct/datatype/trait |

**Reference**: [items.md](../language/reference/items.md). Coverage is comprehensive at the surface level. **GAP**: meta-block semantics + the relation between `auto trait` and ordinary `trait` deserve a dedicated section. (`genos` is a separate computable form-specification keyword, not a trait flavour.)

**Implementation entry points**:
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `lower_struct_def`, `lower_enum_def`, `lower_datatype_def`
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — first pass (registration into `structs_`, `datatypes_`, `enums_`, `traits_`)
- [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp) — `clone_struct_def`, `clone_enum_def`
- [src/compiler/mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) — `register_struct`, `register_tagged_enum`

**Memory**: [feat_explicit_instantiation](../../../.claude/projects/-home-victor-devel-logos/memory/feat_explicit_instantiation.md), [feat_tag_dispatch](../../../.claude/projects/-home-victor-devel-logos/memory/feat_tag_dispatch.md), [feat_writ_datatype](../../../.claude/projects/-home-victor-devel-logos/memory/feat_writ_datatype.md).

---

## 3. Functions & Methods

**Productions** (8):

| Production | Line | Purpose |
|---|---|---|
| `pub_fn_def` / `fn_def` | 849/867 | Free function (with optional `unsafe`, optional generics) |
| `extern_fn_def` | 835 | `extern fn(..., ...)` (varargs only — for FFI) |
| `pub_static_fn_def` / `static_fn_def` | 744/759 | Static method on struct/datatype/trait |
| `param_list` / `param` | 888/891 | Parameter syntax (incl. `&mut`, `*const`, `*mut`) |
| `method_def` | 832 | Dispatcher for impl/struct method (via static_fn / fn) |

**Reference**: [items.md](../language/reference/items.md), [statements.md](../language/reference/statements.md). Functions are surface-only; ABI rules and self-receiver lowering rules are in internals docs.

**Implementation entry points**:
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `lower_fn`, `lower_static_fn`
- [src/compiler/mlir_gen_fn.cpp](../../src/compiler/mlir_gen_fn.cpp) — `make_fn_type`, `forward_declare`, `gen_function_body`
- [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp) — `clone_fn`

**Memory**: [project_fnptr_sret_bug](../../../.claude/projects/-home-victor-devel-logos/memory/project_fnptr_sret_bug.md), [project_compiler_threads_no_fibers](../../../.claude/projects/-home-victor-devel-logos/memory/project_compiler_threads_no_fibers.md).

---

## 4. Const & Type aliases

**Productions** (2):

| Production | Line | Purpose |
|---|---|---|
| `const_def` | 510 | `pub const X<...>: T = expr;` (incl. parametric WritStatic) |
| `type_alias` | 525 | `pub type Foo<...> = T;` |

**Reference**: [items.md](../language/reference/items.md), [metaprog.md](../language/reference/metaprog.md) (parametric WritStatic section).

**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — `collect_const`, `collect_type_alias`
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `resolve_wstatic_value`, `generic_consts_` map for parametric

**Memory**: [feat_wstatic_const_generic](../../../.claude/projects/-home-victor-devel-logos/memory/feat_wstatic_const_generic.md), [feat_parametric_wstatic](../../../.claude/projects/-home-victor-devel-logos/memory/feat_parametric_wstatic.md), [feat_pub_const](../../../.claude/projects/-home-victor-devel-logos/memory/feat_pub_const.md).

---

## 5. Annotations & Meta blocks

**Productions** (4):

| Production | Line | Purpose |
|---|---|---|
| `annotation` | 455 | `#[name(args)]`, `#[name = value]`, `#[name]` |
| `annot_val` | 462 | Annotation value: int / enum lit |
| `annot_args` | 493 | Comma-separated annotation arguments |
| `meta_block` | 821 | `meta @{...}` Writ-literal payload on struct/trait/datatype |

**Reference**: [attributes.md](../language/reference/attributes.md). **GAP**: complete registry of recognized `#[...]` forms (derive, repr, type_code, metaprog_handler, ad-hoc user annotations). Meta-block semantics also light. Phase 1B should expand this.

**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — `parse_annotation`, derive registration, type_code attribute
- [stdlib/std/compiler/metaprog/derive_clone.logos](../../stdlib/std/compiler/metaprog/derive_clone.logos) — example annotation handler

**Memory**: [feat_derive_clone_stdlib](../../../.claude/projects/-home-victor-devel-logos/memory/feat_derive_clone_stdlib.md), [feat_derive_clone_quote](../../../.claude/projects/-home-victor-devel-logos/memory/feat_derive_clone_quote.md).

---

## 6. Type System

**Productions** (24):

| Production | Line | Purpose |
|---|---|---|
| `type_ref` | 910 | Top-level type-ref dispatcher |
| `simple_type` | (inside type_ref) | Plain `IDENT[<args>]` |
| `path_step` | (inside) | `Module.Type` segment |
| `ptr_type` | (inside) | `*const T` / `*mut T` |
| `ref_type` / `ref_pointee` | | `&T` / `&mut T` (with optional lifetime) |
| `slice_type` | | `&[T]` |
| `arr_type` | | `[T; N]` (with sizeof_pack support) |
| `tuple_type` | | `(T1, T2)` |
| `unit_type` | | `()` |
| `dyn_type` | | `dyn Trait` |
| `tagged_type` | | `Tagged<T>` (TaggedPtr) |
| `closure_type` / `closure_type_args` | | `\|T1, T2\| -> R` |
| `fn_ptr_type` / `fn_ptr_type_args` | | `fn(T1, T2) -> R` |
| `impl_type` | 902 | `impl Trait` (return-position) |
| `assoc_type_ref` | | `T::Assoc` |
| `antiquot_type` | | `#(...)` antiquot in quote_ty! |
| `typeof_type` | | `typeof(expr)` |
| `cfg_slot_type` | | `<type:CFG.slot>` WritStatic-slot extraction |
| `cfg_slot_assoc_ref` | | `<type:CFG.slot>::Assoc` |
| `wstatic_lit_type` | | `@{...}` at type position (RETIRED — see feat_parametric_wstatic) |
| `writ_arr_type` | | Writ array shape at type-position |
| `writ_map_type` | | Writ map shape at type-position |
| `type_or_lt_arg` | | Type or lifetime argument (for type_arg_list) |

**Reference**: [types.md](../language/reference/types.md). Comprehensive at surface. **Recent change** (2026-05-04): `wstatic_lit_type` removed from `type_ref` after parametric WritStatic landed.

**Implementation entry points**:
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `resolve_type` (~2000 lines into the file), `compute_type_uid`, `types_equal`, `mangle_type_for_name`, `concrete_struct_name`
- [src/compiler/sema_impl.hpp](../../src/compiler/sema_impl.hpp) — `make_struct_type`, `make_datatype_type`, `make_generic_struct`, `make_generic_datatype`, `make_enum_type`
- [src/compiler/mono_subst.cpp](../../src/compiler/mono_subst.cpp) — `subst_type`
- [src/compiler/mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) — `logos_to_mlir`

**Memory**: [feat_logos_type_hash](../../../.claude/projects/-home-victor-devel-logos/memory/feat_logos_type_hash.md), [feat_type_uid_pkg_skip_bug](../../../.claude/projects/-home-victor-devel-logos/memory/feat_type_uid_pkg_skip_bug.md), [feat_cfg_slot_type](../../../.claude/projects/-home-victor-devel-logos/memory/feat_cfg_slot_type.md), [feat_int_widening](../../../.claude/projects/-home-victor-devel-logos/memory/feat_int_widening.md).

---

## 7. Generics & Bounds

**Productions** (9):

| Production | Line | Purpose |
|---|---|---|
| `type_param_list` | 1720 | `<T: Bound, U, const N: usize, 'a>` |
| `type_param` | 1741 | Single type/const/lifetime param |
| `lifetime_param` | 1738 | `'a` |
| `type_arg_list` | 1764 | `<T1, T2>` (instantiation) |
| `bound_arg_list` / `bound_arg` | 1730/1733 | Bound arguments inside trait bounds |
| `trait_bound` | 1723 | `Trait<T> + Send + 'a` |
| `super_list` | 576 | (also in Items) — super traits |
| `where_clause` | 843 | `where T: Bound` |

**Reference**: [generics-traits.md](../language/reference/generics-traits.md). Comprehensive.

**Implementation entry points**:
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `unify_types`, `match_type_sema`, type-param substitution
- [src/compiler/mono_subst.cpp](../../src/compiler/mono_subst.cpp) — `subst_type`, blanket-impl resolution
- [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp) — `find_best_struct_spec`

**Memory**: [feat_wstatic_const_generic](../../../.claude/projects/-home-victor-devel-logos/memory/feat_wstatic_const_generic.md), [feat_const_variadic_mvp](../../../.claude/projects/-home-victor-devel-logos/memory/feat_const_variadic_mvp.md), [feat_method_assoc_type_outptr_silent](../../../.claude/projects/-home-victor-devel-logos/memory/feat_method_assoc_type_outptr_silent.md), [feat_generic_ref](../../../.claude/projects/-home-victor-devel-logos/memory/feat_generic_ref.md), [feat_arr_type_sizeof_pack_gap](../../../.claude/projects/-home-victor-devel-logos/memory/feat_arr_type_sizeof_pack_gap.md).

---

## 8. Statements & Assignments

**Productions** (30):

| Production | Line | Purpose |
|---|---|---|
| `block` / `unsafe_block` | | Statement-list with optional `unsafe` |
| `stmt` | | Dispatcher for all statement forms |
| `let_stmt` | | `let [mut] x[: T] = expr;` |
| `let_else_stmt` | | `let pat = expr else { ... };` |
| `return_stmt` | | `return [expr];` |
| `if_expr` | | `if c { ... } else { ... }` (also expression) |
| `while_stmt` | | `while cond { ... }` |
| `for_stmt` | | `for x in iter { ... }` |
| `labeled_loop_stmt` | | `'lbl: loop { ... }` |
| `continue_stmt` | | `continue [label];` |
| `match_stmt` / `match_arm` | | `match e { pat => ..., }` |
| `assign_stmt` | | `lhs = rhs;` |
| `compound_assign_op` / `compound_assign_stmt` | | `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `<<=`, `>>=` |
| `field_write_stmt` / `field_compound_assign_stmt` | | `recv.field = ...` / `recv.field += ...` |
| `chain_path_id` / `chain_field_path` / `chain_field_write_stmt` / `chain_field_compound_assign_stmt` | | `a.b.c.d = ...` chains |
| `tuple_field_write_stmt` / `tuple_field_compound_assign_stmt` | | `t.0 = ...` |
| `index_write_stmt` / `index_compound_assign_stmt` | | `arr[i] = ...` |
| `field_index_write_stmt` / `field_index_compound_assign_stmt` | | `recv.field[i] = ...` |
| `deref_write_stmt` / `deref_field_write_stmt` / `deref_field_compound_assign_stmt` | | `*p = ...`, `(*p).f = ...`, `(*p).f += ...` |

**Reference**: [statements.md](../language/reference/statements.md). **GAP**: the assignment matrix (12 distinct forms × 10 compound ops) lacks an exhaustive table; bug-hunt should reveal which combinations are not actually wired up.

**Implementation entry points**:
- [src/compiler/sema_stmt.cpp](../../src/compiler/sema_stmt.cpp) — `lower_let`, `lower_assign`, `lower_compound_assign`, etc.
- [src/compiler/mlir_gen_stmt.cpp](../../src/compiler/mlir_gen_stmt.cpp) — emit-side for each assignment form
- [src/compiler/borrow_check.cpp](../../src/compiler/borrow_check.cpp) — borrow rules for assignment / mutation

**Memory**: [feat_logos_generic_struct_offset](../../../.claude/projects/-home-victor-devel-logos/memory/feat_logos_generic_struct_offset.md), [feat_struct_move_default](../../../.claude/projects/-home-victor-devel-logos/memory/feat_struct_move_default.md), [project_struct_mut_rebind_bug](../../../.claude/projects/-home-victor-devel-logos/memory/project_struct_mut_rebind_bug.md), [project_borrow_checker](../../../.claude/projects/-home-victor-devel-logos/memory/project_borrow_checker.md).

---

## 9. Pattern Matching

**Productions** (11):

| Production | Line | Purpose |
|---|---|---|
| `pattern` | | Top-level pattern dispatcher |
| `pat_binding` / `pat_binding_list` | | `Foo(a, b)` / `Some(x)` / wildcards |
| `pat_field` / `pat_field_list` | | Struct-shaped pattern: `Foo { x, y: z }` |
| `pat_slice_elem` / `pat_slice_elems` | | `[a, b, ..rest, c]` slice patterns |
| `pat_writ_map_entry` / `pat_writ_map_entries` | | `@{ "k": pat, .. }` Writ-map patterns |
| `pat_writ_arr_elem` / `pat_writ_arr_elems` | | `@[a, b, ..]` Writ-array patterns |

**Reference**: [patterns.md](../language/reference/patterns.md). Comprehensive.

**Implementation entry points**:
- [src/compiler/sema_pat.cpp](../../src/compiler/sema_pat.cpp) — pattern lowering, binding registration
- [src/compiler/mlir_gen_stmt.cpp](../../src/compiler/mlir_gen_stmt.cpp) — match arm emission
- [src/compiler/mlir_gen_dyn.cpp](../../src/compiler/mlir_gen_dyn.cpp) — variant tag dispatch

**Memory**: [feat_enum_payload_drop_bug](../../../.claude/projects/-home-victor-devel-logos/memory/feat_enum_payload_drop_bug.md), [project_result_err_destructure_bug](../../../.claude/projects/-home-victor-devel-logos/memory/project_result_err_destructure_bug.md).

---

## 10. Expressions & Precedence

**Productions** (13):

| Production | Line | Purpose |
|---|---|---|
| `expr` | | Top-level expression |
| `log_expr` | | `&&` / `\|\|` |
| `cmp_expr` | | `==` / `!=` / `<` / `<=` / `>` / `>=` |
| `bitwise_expr` | | `&` / `\|` / `^` |
| `add_expr` | | `+` / `-` |
| `mul_expr` | | `*` / `/` / `%` |
| `cast_expr` | | `expr as Type` |
| `unary_expr` | | `-` / `!` / `*` / `&` / `&mut` / `?` |
| `atom` | | call / field-read / index / method-call chain |
| `primary_expr` | | Identifier / literal / paren_expr / etc. |
| `paren_expr` | 1688 | `(expr)` |
| `call_expr` | 1773 | `f(args)` / `f::<T>(args)` |
| `call_arg_list` | 1768 | Comma-separated args |

**Reference**: [expressions.md](../language/reference/expressions.md). Comprehensive.

**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_expr`, all expr-kind handlers
- [src/compiler/mlir_gen_expr.cpp](../../src/compiler/mlir_gen_expr.cpp) — emit-side

**Memory**: [feat_try_operator](../../../.claude/projects/-home-victor-devel-logos/memory/feat_try_operator.md), [project_shadowed_let_eq_bug](../../../.claude/projects/-home-victor-devel-logos/memory/project_shadowed_let_eq_bug.md), [feat_int_widening](../../../.claude/projects/-home-victor-devel-logos/memory/feat_int_widening.md).

---

## 11. Literals (non-Writ)

**Productions** (10):

| Production | Line | Purpose |
|---|---|---|
| `enum_lit` | | `Enum::Variant(payload)` |
| `struct_lit` | 1545 | `Foo { f: v, ... }` |
| `generic_struct_lit` | | `Foo::<T> { ... }` |
| `struct_update_lit` | | `Foo { ..base, f: v }` |
| `field_init` | 1555 | Single field initializer |
| `arr_lit` | 1657 | `[v1, v2, v3]` |
| `arr_fill_lit` | 1569 | `[v; N]` |
| `tuple_lit` | 1661 | `(v1, v2)` |
| `list_comp` | 1575 | `[expr for x in iter if cond]` |
| `map_comp` | 1581 | `{k: v for ... }` (regular dict comp) |
| `closure_expr` | 1670 | `\|x, y\| { ... }` |

**Reference**: [expressions.md](../language/reference/expressions.md) covers most. List-comp and map-comp surface coverage is light. Closures are mentioned but capture rules deep in internals.

**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_struct_lit`, `lower_arr_lit`, `lower_tuple_lit`, `lower_closure_expr`
- [src/compiler/mlir_gen_expr.cpp](../../src/compiler/mlir_gen_expr.cpp) / `mlir_gen.cpp` — `gen_struct_lit`
- [src/compiler/mlir_gen_dyn.cpp](../../src/compiler/mlir_gen_dyn.cpp) — closure capture lowering

**Memory**: [feat_quote_expr_struct_lit](../../../.claude/projects/-home-victor-devel-logos/memory/feat_quote_expr_struct_lit.md), [feat_arr_lit_struct_ptr_layout](../../../.claude/projects/-home-victor-devel-logos/memory/feat_arr_lit_struct_ptr_layout.md), [project_struct_array_literal_stride](../../../.claude/projects/-home-victor-devel-logos/memory/project_struct_array_literal_stride.md).

---

## 12. Writ Literals

**Productions** (9):

| Production | Line | Purpose |
|---|---|---|
| `writ_lit` | 1608 | `@{...}` / `@[...]` dispatcher |
| `writ_map` | 1623 | `@{ "k": v, ... }` |
| `writ_array` | 1626 | `@[v, v, v]` |
| `writ_entry` | 1630 | `"key": value` inside writ_map |
| `writ_val` | 1640 | A single Writ value (str/int/float/bool/null + nested map/arr) |
| `writ_typed_array` | 1590 | `@[T; ...]` typed array literal |
| `writ_typed_map` | 1593 | `@{T => U; ...}` typed map literal |
| `writ_list_comp` | 1598 | `@[expr for x in iter]` (Writ-typed comprehension) |
| `writ_map_comp` | 1603 | `@{k: v for ...}` (Writ-typed map comprehension) |

**Reference**: [writ.md](../language/reference/writ.md). Comprehensive at design level.

**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_writ_val`, `lower_writ_lit`, `lower_writ_typed_*`
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `eval_static_writ_lit` (for meta blocks)
- [stdlib/std/writ/](../../stdlib/std/writ/) — runtime Writ layer

**Memory**: [feat_writ_syntax](../../../.claude/projects/-home-victor-devel-logos/memory/feat_writ_syntax.md), [feat_writ_datatype](../../../.claude/projects/-home-victor-devel-logos/memory/feat_writ_datatype.md), [feat_writ_capture_*](../../../.claude/projects/-home-victor-devel-logos/memory/feat_writ_compile_runtime_fabric.md), [feat_anyval_layout](../../../.claude/projects/-home-victor-devel-logos/memory/feat_anyval_layout.md).

---

## 13. Metaprog

**Productions** (8):

| Production | Line | Purpose |
|---|---|---|
| `metacall_item_decl` | 416 | `metacall foo();` at item position |
| `instantiate_decl` / `pub_instantiate_decl` | 429/426 | `instantiate Foo<T>;` (root-pin) |
| `template_decl` | 439 | `template <decl>` (AST data, not binding) |
| `template_inner` | 442 | What `template` can wrap (struct/enum/datatype/trait/genos/impl/fn) |
| `quote_item_expr` | 1697 | `quote_item! { ... }` |
| `quote_expr_expr` | 1706 | `quote_expr! { ... }` |
| `quote_ty_expr` | 1714 | `quote_ty! { ... }` |

**Reference**: [metaprog.md](../language/reference/metaprog.md). Surface coverage good. **GAP**: depth on quote-form semantics — antiquotation rules per-form, repeat-group expansion, hygiene model with gensym, `#[metaprog_handler]` lifecycle.

**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_quote_item`, `lower_quote_expr`, `lower_quote_ty`, `lower_metacall_item`
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — generic_consts_, parametric WritStatic substitution
- [stdlib/std/compiler/metaprog/](../../stdlib/std/compiler/metaprog/) — runtime metaprog machinery, `logos_emit_item_blob_subst`

**Memory**: [feat_metaprog_quote](../../../.claude/projects/-home-victor-devel-logos/memory/feat_metaprog_quote.md), [feat_metacall_arch](../../../.claude/projects/-home-victor-devel-logos/memory/feat_metacall_arch.md), [feat_quote_item_dedupe](../../../.claude/projects/-home-victor-devel-logos/memory/feat_quote_item_dedupe.md), [feat_quote_jinja_min_b](../../../.claude/projects/-home-victor-devel-logos/memory/feat_quote_jinja_min_b.md), [feat_metaprog_inversion](../../../.claude/projects/-home-victor-devel-logos/memory/feat_metaprog_inversion.md).

---

## 14. Lexical

No `%rules` productions; lives in the grammar's `%tokens` block.

**Reference**: [lexical.md](../language/reference/lexical.md). Comprehensive.

**Implementation entry points**:
- Generated lexer in [build/tools/peg_gen/](../../build/tools/peg_gen/) (regenerated from logos.peg)
- [tools/peg_gen/grammars/logos.peg](../../tools/peg_gen/grammars/logos.peg) `%tokens` section

**Known sensitivities**: Unicode rejection in identifiers and comments (see [feat_unicode_parser](../../../.claude/projects/-home-victor-devel-logos/memory/feat_unicode_parser.md)). Numeric literals + suffixes are documented in [feat_int_widening](../../../.claude/projects/-home-victor-devel-logos/memory/feat_int_widening.md).

---

## Phase 1B reference gaps to plug

Confirmed from the inventory above:

1. **`docs/language/reference/modules.md`** (NEW) — name resolution, `use` ordering, visibility scope, multi-file packages, `pub use` re-exports, find-best-spec across packages.
2. **`docs/language/reference/attributes.md`** (EXTEND) — exhaustive registry of recognized `#[...]` attributes (derive_*, repr, type_code, metaprog_handler, custom user annotations) + meta-block (`@{...}`) semantics.
3. **`docs/language/reference/statements.md`** (EXTEND) — exhaustive compound-assign matrix (12 LHS-shapes × 10 ops) + which combinations are wired up.
4. **`docs/language/reference/metaprog.md`** (EXTEND) — antiquot rules per quote form, repeat-group expansion semantics, hygiene/gensym model, `#[metaprog_handler]` lifecycle.

---

## Tracked deliverables for Phase 2

Catalog files to create:
- `docs/baghunt/module-visibility.md`
- `docs/baghunt/items.md`
- `docs/baghunt/functions.md`
- `docs/baghunt/const-alias.md`
- `docs/baghunt/attributes.md`
- `docs/baghunt/types.md`
- `docs/baghunt/generics.md`
- `docs/baghunt/statements.md`
- `docs/baghunt/patterns.md`
- `docs/baghunt/expressions.md`
- `docs/baghunt/literals.md`
- `docs/baghunt/writ.md`
- `docs/baghunt/metaprog.md`
- `docs/baghunt/lexical.md`

(14 catalogs; ~10 confirmed bugs each ≈ 120-160 entries.)

Format spec is in the [plan file](../../../.claude/plans/memoized-whistling-cherny.md#per-feature-bug-catalog-format).
