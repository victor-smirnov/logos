# Lang-item census — non-trait identities (ADR 0030 L0, slice 2)

Measured 2026-09-27 on the tree after L0 slice 1 (traits bound by `#[lang]`).
Every site where the compiler identifies a NON-TRAIT stdlib item by a hard-coded
package string and/or item name, or spells a stdlib name into synthesized code.
Sites are cited by file and symbol; line numbers drift.

## Defects found by the census (not yet confirmed by a run)

1. `mlir_gen_impl.hpp` unsize-cast owning kind: `is_stdlib_smart_ptr(t, "Rc", "logos.mem.rc")`;
   `Rc` is declared in `logos.lang.rc`. The test passes only for a package-less TypeRef. Sema
   uses `logos.lang.rc` (`sema.cpp` owning-kind table, `is_stdlib_rc_or_arc`).
2. Two stdlib enums are named `Ordering` (`logos.lang.atomic`, `logos.lang.cmp`);
   `mlir_gen_expr.cpp` atomic lowering matches `enum_name() == "Ordering"` with no package.

## Items and sites

| item | sites (file · what) |
|---|---|
| `logos.lang.cell::UnsafeCell` | emit_module.cpp ×3 (C ABI transparency, header, abi_type) · sema_auto_trait.cpp (!Sync) · sema.cpp (invariance) · mlir_gen_types.cpp (not freezable) |
| `logos.lang.marker::PhantomPinned` | sema_auto_trait.cpp (!Unpin) |
| `logos.lang.rc::Rc`, `logos.mem.sync::Arc` | sema.cpp owning-kind table · `is_stdlib_rc_or_arc` · mlir_gen_impl.hpp owning kind (defect 1) · sema_expr.cpp bare-name smart-pointer erasure, writ container · sema_stmt.cpp `writ_pat_root_rc` · sema_impl.hpp writ view inner · borrow_check.cpp ×4 residency skip (bare `Rc`/`Arc`) |
| `logos.mem.boxed::Box`, fn `box_take` | `is_stdlib_box` in THREE copies (sema_impl.hpp, mlir_gen_impl.hpp, mono_impl.hpp) · sema.cpp owning kind · sema_impl.hpp / sema_expr.cpp bare-name receiver and deref-move · `box_take` looked up by bare name |
| `logos.lang.option::Option/Some/None` | sema_expr.cpp: bare `None`, `?` operand, `partial_cmp` → `cmp_opt_is_*`, `Some(` callee, `try_prelude`, synthesized `Option<A>`, writ access · sema_stmt.cpp ×3 pattern remap |
| `logos.lang.result::Result/Ok/Err` | sema_expr.cpp: `?` operand and synthesized `Ok`/`Err`, callee resolution ×3 · sema_stmt.cpp ×3 pattern remap |
| `logos.lang.atomic::Ordering` | mlir_gen_expr.cpp ×2 (defect 2) |
| `logos.lang.cmp` fns `slice_eq`, `slice_eq_raw`, `cmp_opt_is_*` | sema_expr.cpp `find_cmp_fn` (package filter), `cmp_opt_is_{lt,le,gt,ge}` by name |
| `logos.lang.range` `range_i32/i64`, `range_incl_of`, `RangeI32/I64` | sema_expr.cpp `stdlib_range_cands` |
| `logos.lang.mem::dealloc` | sema_expr.cpp (consumed `Box<dyn FnOnce>` block) |
| `logos.lang.str::str_eq/str_cmp` | sema_expr.cpp ×3 (`==`, ordering, `make_str_eq_guard`) · sema_stmt.cpp (str-constant pattern) |
| `logos.lang.slice::slice_get_range` | sema_expr.cpp (`s[a..b]`) |
| `logos.lang.ffi::__cstr_from_lit` | sema_expr.cpp (C-string literal) |
| `logos.mem.collections.vec::Vec` | `is_stdlib_vec` · `synth_owner_pkg_` · list comprehension (`Vec<T>`, `Vec__push`) · metaprog synthesized `Vec<…>` · `for x in &Vec` (`Vec__as_slice`) |
| `logos.mem.collections.hashmap::HashMap` | map comprehension |
| `logos.mem.string::String` | sema_expr.cpp `String == str` (bare name) · hir_lower.cpp format expansion |
| `logos.mem.fmt::Formatter`, `ok`, `fmt_*` | hir_lower.cpp format expansion (type, `Formatter::new`, `as_formatter`, `write_str`, `set_spec`, `as_str`, `ok`) · sema_fmt.cpp `format_trait_dispatcher` |
| `logos.std.fmt::__fmt_print*`, `__fmt_panic` | hir_lower.cpp format-family tails |

Writ / metaprog / CtrClass / deem (a subsystem of its own, ~60 sites): the writ structs
(`WritStatic`, `StringView`, `Writ`, `WAny`, `WMap`) via `is_writ_static` / `is_writ` /
`is_string_view` and ~55 `writ_*` fn spellings; metaprog `is_ident` / `is_exprblob` /
`is_quote_item_blob` / `is_item_list`; CtrClass `struct_pkg_is_metaclass`; deem catalog names
in emit_module.cpp.

Total ≈ 100 (lang/mem) + ≈ 60 (writ/metaprog/CtrClass/deem).

## Existing centralizing helpers (route through `lang_item()`)

`is_stdlib_box` (×3 copies — one rule, three implementations), `is_stdlib_rc_or_arc`,
`is_stdlib_smart_ptr`, `is_stdlib_vec`, `synth_owner_pkg_`, `is_writ_static` / `is_writ` /
`is_string_view` / `is_ident` / `is_exprblob` / `is_quote_item_blob` / `is_item_list`,
`struct_pkg_is_metaclass`, `format_trait_dispatcher`, `make_str_eq_guard`, and the local
lambdas `stdlib_range_cands` and `find_cmp_fn`.
