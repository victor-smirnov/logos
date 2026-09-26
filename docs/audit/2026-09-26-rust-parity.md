# Rust-parity audit — 2026-09-26

Logos compared with Rust 1.98 two ways: (1) the Ferrocene Language Specification (FLS, the Rust project's specification, `spec_version` 1.98.0) paragraph by paragraph — 58 units, each rule classified and, where observable, probed against `rustc 1.98.1`; (2) a behaviour audit of 14 everyday-use areas (collections, strings, iterators, closures, errors, …) with Logos/Rust program pairs. Every finding was re-run by an independent verifier. The two inventories were merged into 153 tickets (13 findings dropped as blessed divergences / unstable-in-Rust / not observable).

Tickets are soundness-queue rows (`tests/logos/soundness_queue.ledger`, programs in `tests/soundness/open/`) mirrored as GitHub issues; tier = severity (1 critical, 2 high, 3 medium, 4 low). Two tickets that the queue gate cannot express are issues only (#647 two-module repro, #648 `--test`).

## FLS rule status by chapter (rules, not paragraphs)

| chapter | supported | partial | unsupported | divergent | n/a | untestable | untested |
|---|---|---|---|---|---|---|---|
| attributes | 17 | 6 | 22 | 10 | 11 | 6 | 0 |
| concurrency | 7 | 0 | 0 | 1 | 0 | 2 | 0 |
| entities-and-resolution | 61 | 7 | 7 | 21 | 9 | 9 | 3 |
| exceptions-and-errors | 2 | 0 | 0 | 1 | 0 | 0 | 0 |
| expressions | 161 | 17 | 20 | 10 | 2 | 17 | 4 |
| ffi | 1 | 0 | 13 | 1 | 2 | 2 | 0 |
| functions | 29 | 7 | 7 | 4 | 15 | 6 | 0 |
| generics | 11 | 1 | 5 | 1 | 0 | 1 | 2 |
| implementations | 7 | 0 | 4 | 0 | 2 | 1 | 1 |
| inline-assembly | 0 | 0 | 16 | 0 | 1 | 0 | 0 |
| items | 6 | 2 | 1 | 2 | 0 | 3 | 0 |
| lexical-elements | 15 | 7 | 4 | 5 | 1 | 5 | 1 |
| macros | 1 | 0 | 0 | 15 | 2 | 2 | 0 |
| ownership-and-deconstruction | 30 | 6 | 0 | 1 | 2 | 1 | 0 |
| patterns | 57 | 6 | 9 | 2 | 0 | 8 | 3 |
| program-structure-and-compilation | 3 | 1 | 2 | 5 | 3 | 0 | 0 |
| statements | 9 | 0 | 4 | 1 | 0 | 2 | 0 |
| types-and-traits | 146 | 15 | 13 | 11 | 3 | 20 | 17 |
| unsafety | 7 | 0 | 0 | 0 | 1 | 2 | 0 |
| values | 14 | 2 | 2 | 2 | 1 | 3 | 10 |

Most `unsupported` rules sit in inline assembly, the built-in attribute catalogue and FFI details; `divergent` in macros is Logos's own metaprogramming (blessed).

## Tickets

### critical (21)

- `and_or_same_precedence_wrong` (wrong_result) — && and || parse at one precedence level (a || b && c groups wrong) — #503
- `assert_with_message_crash` (crash) — assert!(cond, "message") crashes the metacall splice — #504
- `btreemap_entries_never_dropped_wrong` (wrong_result) — BTreeMap never drops its keys/values (scope exit, clear, remove) — fixed in this commit
- `collect_into_map_set_string_refused` (refuses) — Iterator::collect into HashMap, HashSet or String is refused (FromIterator is Vec-only) — #505
- `compound_assign_place_double_eval_wrong` (wrong_result) — Compound assignment evaluates a side-effecting place expression twice — #506
- `derive_clone_nonprimitive_field_refused` (refuses) — #[derive_clone] fails on a struct with a String/Vec/heap-owning field — #507
- `destructuring_assign_drop_wrong` (wrong_result) — Destructuring assignment of Drop types leaks the old values and drops the new ones 3 times — #508
- `enum_type_param_variance_admitted` (admits) — Enum type-param variance is not computed; an invariant-in-T enum is treated as covariant — #509
- `generic_bound_assoc_fn_prefers_inherent_wrong` (wrong_result) — T::f() in a generic bound by a trait dispatches to the concrete type's same-named inherent fn — #510
- `hashmap_remove_leaks_value_wrong` (wrong_result) — HashMap::remove tombstones without dropping the entry and returns bool, not Option<V> — fixed in this commit
- `iter_filter_capturing_closure_refused` (refuses) — Iterator::filter (take_while/skip_while/inspect) rejects capturing closures — #511
- `let_binding_named_like_const_admitted` (admits) — `let NAME = v;` where NAME is an in-scope const silently does nothing — #512
- `loop_local_move_closure_shares_slot_wrong` (wrong_result) — move closures built in a loop share one capture slot (all see the last value) — #513
- `main_unit_return_garbage_exit_wrong` (wrong_result) — fn main() with no return type exits with a garbage status instead of 0 — #514
- `map_named_fn_item_collect_crash` (crash) — .map(named_fn).collect() crashes MLIR generation — #515
- `overlapping_trait_impls_admitted` (admits) — Overlapping trait impls (blanket vs specific) are accepted; the choice is order-dependent — #516
- `primitive_assoc_consts_refused` (refuses) — i32::MAX / i32::MIN / T::BITS associated consts on primitive types are refused — fixed in this commit
- `pub_module_visibility_leak_admitted` (admits) — pub(module) item is callable from a separately built module — #647
- `reference_range_pattern_wrong` (wrong_result) — Range pattern under a reference pattern (&(lo..=hi)) never matches — #517
- `vec_literal_elem_type_ignores_call_site_wrong` (wrong_result) — vec![...] element type ignores the call-site expected type; reads uninitialised memory — #518
- `vec_sort_refused` (refuses) — Vec/slice has no sort, sort_by, sort_by_key or sort_unstable — fixed in this commit

### high (51)

- `array_nonconst_length_crash` (crash) — A non-constant array length ([x; n], [T; n]) aborts the compiler instead of a diagnostic — #519
- `asref_asmut_traits_refused` (refuses) — AsRef / AsMut traits do not exist — #520
- `basic_assignment_eval_order_wrong` (wrong_result) — place = value evaluates the place before the value — #521
- `binaryheap_refused` (refuses) — BinaryHeap (and cmp::Reverse) do not exist — #522
- `break_through_closure_admitted` (admits) — break / break 'label inside a closure is accepted and silently does nothing — #523
- `closure_mixed_return_types_crash` (crash) — A closure whose returns disagree in type fails MLIR verification instead of a type error — #524
- `closure_move_capture_drop_delayed_wrong` (wrong_result) — A move closure's captured value is dropped at the source scope's end, not when the closure is consumed — #525
- `closure_param_type_from_usage_refused` (refuses) — Untyped closure params are not inferred from a later use of the closure — #526
- `constant_promotion_refused` (refuses) — &5 / &mut [] returned as 'static is refused (no constant promotion) — #527
- `custom_iterator_item_assoc_type_refused` (refuses) — A user Iterator impl with `type Item` is refused (stdlib Iterator is param-shaped) — #528
- `default_impls_for_std_types_refused` (refuses) — Default is not implemented for String, Vec, Option, HashMap (blocks mem::take) — #529
- `derive_default_nonpod_field_refused` (refuses) — #[derive_default] fails on a struct with a String/Vec field — #530
- `destructuring_assign_assignee_forms_refused` (refuses) — Destructuring assignment refuses place elements (arr[0], s.f), tuple-struct assignees and `_ = e` — #531
- `file_line_streaming_refused` (refuses) — File cannot be wrapped in BufReader to stream lines (File has no Read impl) — #532
- `float_methods_refused` (refuses) — f32/f64 have no methods (.sqrt .floor .abs .powi .min .max ...) — #533
- `format_named_and_captured_args_refused` (refuses) — format!/println! named arguments and inline captured identifiers ("{name}") are refused — #534
- `generic_index_output_refused` (refuses) — Generic code bound by Index cannot name the element type (Index<Idx, Output> is a type param, not an assoc type) — #535
- `generic_param_leaks_into_nested_item_admitted` (admits) — A nested fn item can use its enclosing fn's generic parameter — #536
- `generic_trait_multi_impl_dispatch_refused` (refuses) — Method call / UFCS fails when a type implements one generic trait at two type args — #537
- `hashmap_entry_api_refused` (refuses) — HashMap::entry().or_insert / or_insert_with / and_modify / or_default are missing — #538
- `impl_method_extra_bound_admitted` (admits) — An impl method may add bounds the trait method lacks; generic callers then break them — #539
- `iter_fold_accumulator_inference_refused` (refuses) — fold(init, closure) does not infer the accumulator type from init — #540
- `iterator_cloned_copied_methods_refused` (refuses) — Iterator::cloned() / copied() are not methods — #541
- `iterator_enumerate_tuple_item_refused` (refuses) — enumerate() yields a named struct, so for (i, x) / |(i, x)| destructuring is refused — #542
- `iterator_flat_map_flatten_methods_refused` (refuses) — flat_map / flatten are not methods (free fns with sentinel args only) — #543
- `iterator_partition_unzip_refused` (refuses) — Iterator::partition and unzip are missing — #544
- `iterator_zip_method_refused` (refuses) — .zip(other) cannot infer its generics and yields a named struct instead of a tuple — #545
- `let_else_nondiverging_else_admitted` (admits) — A let-else whose else block does not diverge is accepted and supplies the value — #546
- `match_guard_temp_drop_timing_wrong` (wrong_result) — A temporary made in a failed match guard is dropped after the next arm runs — #547
- `nonunit_expr_statement_admitted` (admits) — A non-unit expression statement without `;` is accepted and acts as an early return — #548
- `option_method_surface_refused` (refuses) — Option methods unwrap_or_default, zip, copied/cloned, get_or_insert_with do not resolve — #549
- `orphan_rule_not_enforced_admitted` (admits) — Orphan rule is not enforced for trait impls — #550
- `question_op_box_dyn_error_refused` (refuses) — `?` does not convert a concrete error into Box<dyn Error> — #551
- `raw_pointer_matched_as_option_crash` (crash) — Matching a raw pointer against Some/None passes sema and fails in mlir_gen — #552
- `rc_arc_assoc_fn_surface_refused` (refuses) — Rc::new / Arc::new, Rc::strong_count, Rc::ptr_eq and Weak::new are missing or unsafe-only — #553
- `refcell_guard_no_deref_refused` (refuses) — Field access through a RefCell borrow guard (cell.borrow().x) is refused — #554
- `sized_supertrait_blocks_impl_refused` (refuses) — trait T: Sized cannot be implemented for an ordinary struct — #555
- `slice_split_at_mut_refused` (refuses) — split_at / split_at_mut are not methods; split_at_mut does not exist at all — #556
- `str_find_not_option_refused` (refuses) — str::find returns i64 (-1 sentinel) instead of Option<usize> — #557
- `str_iterator_methods_refused` (refuses) — .chars() .bytes() .char_indices() .lines() .split_whitespace() are not methods on str — #558
- `str_method_family_refused` (refuses) — str has no replace, rfind, repeat, to_uppercase/lowercase, strip_prefix/suffix, split_once — #559
- `string_add_operator_refused` (refuses) — String + &str and String += &str are refused — #560
- `string_ordering_refused` (refuses) — String / &str have no < > <= >= (no PartialOrd/Ord) — fixed in this commit
- `string_pop_insert_remove_refused` (refuses) — String::pop / insert / remove are missing — #561
- `test_result_return_refused` (refuses) — #[test] fn returning Result<(), E> is refused — #648
- `tryfrom_assoc_error_type_refused` (refuses) — TryFrom has no associated Error type (hard-coded ConvertError) — #562
- `unary_negation_overflow_not_trapped_wrong` (wrong_result) — Unary -x on i32::MIN wraps silently instead of trapping — #563
- `unsuffixed_int_literal_method_arg_refused` (refuses) — An unsuffixed integer literal as an argument to a primitive method (x.pow(2)) is refused — #564
- `vec_get_mut_refused` (refuses) — Vec::get_mut(i) -> Option<&mut T> is missing (exists as try_borrow_mut) — #565
- `vec_growth_methods_refused` (refuses) — Vec has no extend, extend_from_slice, append, insert, truncate, resize, swap_remove, split_off, drain — #566
- `vec_retain_dedup_refused` (refuses) — Vec::retain and Vec::dedup are missing — fixed in this commit

### medium (48)

- `array_methods_refused` (refuses) — Arrays have no methods (.iter(), .to_vec()) — #567
- `assoc_const_path_pattern_refused` (refuses) — Type::CONST (associated const) in pattern position is refused — #568
- `assoc_const_trait_dyn_admitted` (admits) — A trait with an associated const is accepted as dyn Trait — #569
- `assoc_type_bound_conformance_admitted` (admits) — An impl's associated type need not satisfy the trait's declared bound — #570
- `assoc_type_bound_sugar_refused` (refuses) — Associated-type bound sugar T: Trait<Assoc: Bound> does not parse — #571
- `attr_name_eq_literal_form_refused` (refuses) — #[name = "literal"] attributes (doc, export_name, link_section, crate_name, ...) do not parse — #572
- `attr_on_extern_block_item_refused` (refuses) — Attributes on items inside an extern block (link_name, ...) do not parse — #573
- `attr_unsafe_wrapper_refused` (refuses) — #[unsafe(no_mangle)] / #[unsafe(export_name = ...)] cannot be written — #574
- `break_value_in_for_while_admitted` (admits) — break with a value inside for / while is accepted — #575
- `btreeset_refused` (refuses) — BTreeSet does not exist — #576
- `byte_char_literal_refused` (refuses) — Byte literal b'A' is refused — #577
- `c_string_literal_refused` (refuses) — C string literals c"..." / cr"..." and CStr are missing — #578
- `char_from_u32_from_digit_refused` (refuses) — char::from_u32 / char::from_digit are missing — #579
- `closure_to_fn_pointer_coercion_refused` (refuses) — A non-capturing closure does not coerce to a fn pointer — #580
- `collections_owning_into_iter_refused` (refuses) — Owning for-in / into_iter over HashMap, BTreeMap, HashSet, VecDeque is refused — #581
- `const_generic_default_refused` (refuses) — Const generic parameter defaults (const N: usize = 3) do not parse — #582
- `dyn_second_nonauto_trait_admitted` (admits) — dyn A + B with two non-auto traits is accepted — #583
- `enum_discriminant_validation_admitted` (admits) — Duplicate enum discriminants and discriminants out of the repr range are accepted (the latter silently truncated) — #584
- `extern_fn_param_pattern_admitted` (admits) — An extern fn declaration accepts a destructuring parameter pattern — #585
- `extern_safe_item_qualifier_refused` (refuses) — `safe fn` / `safe static` inside `unsafe extern` are refused — #586
- `gat_lifetime_projection_refused` (refuses) — A lifetime GAT used as Self::Item<'a> does not parse — #587
- `global_allocator_attr_ignored_admitted` (admits) — #[global_allocator] is warned-unknown and ignored; no GlobalAlloc hook exists — #588
- `hashset_set_ops_refused` (refuses) — HashSet union/intersection/difference/symmetric_difference are missing — #589
- `i128_literal_above_u64_refused` (refuses) — i128/u128 literals above u64::MAX are refused, contradicting lex.literal.int128-magnitude — #590
- `inline_asm_refused` (refuses) — asm! / global_asm! / naked_asm! are unsupported — #591
- `inner_attr_in_item_body_refused` (refuses) — Inner attributes (#![...]) and inner doc comments (//!) are refused inside item bodies — #592
- `iter_adapters_spuriously_unsafe_refused` (refuses) — Iterator position(), Peekable::peek() and step_by() need unsafe (step_by also needs an extra arg) — #593
- `labeled_block_expr_refused` (refuses) — Labeled block expression 'a: { ... break 'a v; ... } is refused — #594
- `let_ref_mut_binding_crash` (crash) — `let ref mut a = expr;` fails in mlir_gen (binding never emitted) — #595
- `let_single_variant_enum_refused` (refuses) — let with an irrefutable enum-variant pattern (single-variant enum) is refused — #596
- `main_signature_unchecked_admitted` (admits) — main is not checked: unsafe/generic/parameterised main is accepted — #597
- `map_values_mut_refused` (refuses) — HashMap/BTreeMap::values_mut is missing — #598
- `match_guard_if_let_refused` (refuses) — Match-arm guards with if let (and let-chains) are refused — #599
- `mem_size_of_refused` (refuses) — mem::size_of::<T>() / align_of are missing — #600
- `mutex_rwlock_raii_guard_refused` (refuses) — Mutex/RwLock have no RAII guard: lock() is unsafe and returns *mut T with manual unlock — #601
- `non_exhaustive_attr_ignored_admitted` (admits) — #[non_exhaustive] is warned-unknown and has no effect — #602
- `operator_bound_explicit_rhs_refused` (refuses) — A bound written T: Add<T, Output = T> fails on primitives; Add<Output = T> works — #603
- `partialeq_heterogeneous_refused` (refuses) — PartialEq<Rhs> for a different Rhs type is refused (trait has no Rhs param) — #604
- `range_literal_methods_need_import_refused` (refuses) — Methods on a range literal ((0..5).sum(), .rev(), .contains) need an explicit import, and the hint names a wrong path — #605
- `range_not_generic_refused` (refuses) — Ranges are per-integer types: no .start/.end fields, no char/float bounds, mixed-width bounds are widened — #606
- `range_pattern_const_bound_refused` (refuses) — A named constant as a range-pattern bound (LO..=HI) does not parse — #607
- `raw_borrow_expr_refused` (refuses) — &raw const / &raw mut do not parse — #608
- `ref_shorthand_shadows_const_crash` (crash) — A shorthand `ref C` field pattern whose name is a const crashes MLIR verification — #609
- `slice_method_surface_refused` (refuses) — windows, chunks, binary_search, concat/join, starts_with, fill, rotate_left, copy_from_slice are not methods — #610
- `string_literal_newline_continuation_refused` (refuses) — String literal with a physical newline or a \<newline> continuation is refused — #611
- `target_feature_attr_ignored_admitted` (admits) — #[target_feature(enable=...)] is warned-unknown and ignored — #612
- `union_functional_update_admitted` (admits) — Union literal with a ..base initializer is accepted — #613
- `vec_capacity_api_refused` (refuses) — Vec::with_capacity and Vec::capacity are missing — fixed in this commit

### low (33)

- `assignment_as_expression_refused` (refuses) — Assignment is not an expression (cannot be used where a () value is expected) — #614
- `assoc_type_unsized_binding_admitted` (admits) — An unsized type (dyn Trait) is accepted for an assoc type without ?Sized — #615
- `bare_tuple_variant_name_pattern_admitted` (admits) — A tuple variant written without (..) in a pattern matches by discriminant only — #616
- `cfg_all_any_empty_refused` (refuses) — cfg(all()) / cfg(any()) with empty argument lists do not parse — #617
- `closure_param_duplicate_binding_admitted` (admits) — Two closure params with the same name are accepted — #618
- `cyclic_supertrait_admitted` (admits) — Cyclic supertraits (A: B, B: A) are accepted — #619
- `duplicate_field_in_pattern_admitted` (admits) — A struct pattern naming the same field twice is accepted — #620
- `empty_statement_refused` (refuses) — A bare `;` empty statement is refused — #621
- `enum_variant_field_pub_admitted` (admits) — pub on an enum variant field is accepted — #622
- `extern_system_unwind_abi_refused` (refuses) — extern "system-unwind" ABI string is refused — #623
- `for_while_as_expression_refused` (refuses) — for / while loops are not accepted in expression position — #624
- `impl_trait_in_let_admitted` (admits) — impl Trait accepted as a let binding type — #625
- `indexed_record_pattern_on_variant_refused` (refuses) — Record-form indexed pattern Variant { 0: x } on a tuple enum variant is refused — #626
- `iterator_max_by_key_methods_refused` (refuses) — max_by_key / min_by_key are not methods — #627
- `lifetime_param_order_admitted` (admits) — Lifetime params after type/const params are accepted — #628
- `link_attr_ignored_admitted` (admits) — #[link(name = ...)] on an extern block is warned-unknown and ignored — #629
- `match_guard_mutates_binding_admitted` (admits) — A match guard may mutate a by-value mut binding — #630
- `no_main_attr_ignored_admitted` (admits) — #![no_main] has no effect — #631
- `no_std_attr_ignored_admitted` (admits) — #![no_std] has no effect (std prelude still visible) — #632
- `numeric_literal_underscore_placement_refused` (refuses) — Digit separators before a suffix / exponent or repeated (1___2, 0b1_u8, 8.4_e-12) are refused — #633
- `println_empty_args_refused` (refuses) — println!() with no arguments is refused — #634
- `range_pattern_full_span_refused` (refuses) — A range pattern covering the whole type (0..=255 for u8) is treated as refutable — #635
- `raw_byte_string_literal_refused` (refuses) — Raw byte string br"..." is refused — #636
- `source_bom_shebang_refused` (refuses) — A leading UTF-8 BOM or #! shebang line makes the file unparsable — #637
- `std_hint_attrs_unrecognized_admitted` (admits) — Standard Rust attributes (#[inline], #[cold], #[allow], #[deprecated], #[track_caller], #[used], #[no_builtins]) warn as unknown and do nothing — #638
- `struct_valued_const_pattern_refused` (refuses) — A struct-valued const used as a pattern is refused (not ctfe-evaluable) — #639
- `test_attr_fn_legality_unchecked_admitted` (admits) — #[test] on an unsafe or generic fn is not rejected cleanly — #640
- `trivial_false_predicate_admitted` (admits) — A where-clause predicate that does not hold and names no generic param is accepted — #641
- `tuple_unsized_tail_refused` (refuses) — A tuple type with an unsized last field ((i32, [i32])) is refused — #642
- `unsized_const_type_admitted` (admits) — A const of unsized type (str) is accepted — #643
- `unused_generic_param_admitted` (admits) — Unused type/lifetime params on a struct/enum are accepted — #644
- `variadic_fn_pointer_type_refused` (refuses) — A variadic fn-pointer type extern "C" fn(T, ...) -> R is refused — #645
- `vt_ff_whitespace_refused` (refuses) — Vertical tab / form feed are not whitespace separators — #646

