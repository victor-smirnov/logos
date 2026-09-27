# Sema path inventory: one rule, many implementations (2026-09-26)

Read-only audit. Input: ten per-family inventories (place/assignment, patterns, name/method/call resolution, type inference, coercions, closures, moves/drops, control flow, generics/traits/mono, codegen shape). Probes ran on `logosc 0.47.0-preview+main-g8c1f094c-dirty` (one commit behind HEAD `3df07251e`); Rust twins on rustc 1.98.1. Probe dirs: `/tmp/place_probe`, `/tmp/patinv`, `/tmp/mprobe`, `/tmp/tinf`, `/tmp/coerce_audit`, `/tmp/mvaudit`, `/tmp/cfaudit`, `/tmp/cgaudit`. The closures and generics families were read only; nothing was run for them. Code is cited by `file :: symbol`. "Confirmed" means a probe was run against a rustc twin. "Inferred" means the code was read and nothing was run.

## 1. Executive summary

| metric | value |
|---|---|
| semantic rules inventoried | **107** (10 families) |
| independent implementations, summed per rule | **522** (mean 4.9 per rule, median 4) |
| rules with ≥ 5 independent paths | 47 |
| rules with exactly 1 implementation | 2 (closure capture scanner, drop glue `gen_drop_value`) |
| new wrong-code / crash / permissive defects confirmed by run, unrowed | ~60 distinct programs (probe lists per family below) |

The counts are per family, and the families overlap. The same code appears in more than one family: the match stmt/expr twins (patterns, control flow, codegen), compound assignment (place, name resolution), the branch LUB (coercion, inference, control flow), closure escape (inference, closures), and the `lower_return` / TAIL_EXPR pair (inference, control flow, moves). After removing this double counting, the estimate is about 430 distinct code paths (inferred).

The duplication follows one pattern. Sema lowers per AST spelling, so each door re-decides the rule, and mono and mlir_gen then re-derive what sema already decided (by name composition, type re-walks, or codegen side tables). Where a real chokepoint exists (`expect_type`, `emit_frame_drops`, `elaborate_cond_moves`, `pat_test`/`pat_bind`, `gen_drop_value`, `infer_type_args`), the defects sit in the doors that bypass it.

Worst offenders, ranked by independent paths:

1. **Method-call probe: 19 paths** (8 arms of `lower_method_call`, 7 `try_method_on_*`, blanket fallback, `Mono::subst_expr` MethodCall, the mlir name composer, and 4 BIR resolvers). Confirmed: wrong dispatch (p15, p10) and admitted `&mut self` through `&` (p11).
2. **Literal range adoption: 17** (~100 hand-written "does not fit" checks). Confirmed: `-1` becomes `u64::MAX` at six positions.
3. **Expected-type downward flow: 14 producers × 7 `hint_*` channels.** Confirmed: a hint leak instantiates the wrong T (sizeof 8 vs 1), and boxed escaping closures get a stack env (SIGSEGV).
4. **Literal defaulting: 13 sites**, including an mlir fallback.
5. **Generic-argument inference: 12** raw `unify_types` loops that bypass `infer_type_args`.
6. **Trait satisfaction: 10 engines** across sema and mono.
7. **Store-into-place (codegen): 10.** Confirmed: DST element stride mismatch.
8. **Plain assignment: 9 doors.** Confirmed: eval order (#521, two doors) and the `try_index_mut_assign` type hole.
9. **Unsize to dyn: 9** (sema + 12 mlir sites). Confirmed tier-1: if-arm LUB dispatches through the wrong vtable.
10. **Unsafe-call check (E0133): 8 copies.** Confirmed: admitted on slice, array and `impl for &T` receivers.

Tier-1 silent wrong code (compile rc 0, then garbage, SIGSEGV or a write into immutable data), independent of rank:
- non-exhaustive `match` over int/char/`&Enum`/`&[T]` falls off the end
- a local closure named like a `-> !` fn makes a missing return pass
- a boxed closure escaping in any spelling except `return Box::new(..)` keeps a stack env
- `&mut a` / `&b` branch merges write through the shared borrow
- an if-arm `&dyn` LUB uses the first arm's vtable
- raw pointer → `&`/`&mut` and `&T` → `*mut T` coerce implicitly
- tuple-returning closures, and dyn methods returning tuples, have a return ABI mismatch
- `enum : u64` is truncated to i32 on return
- index through a reference-typed struct field reads garbage or loses the write
- `let y = { x };` double free
- the reassign-after-conditional-move leak
- assignments in if-branch tails vanish
- a non-tail expression-arm match is counted as returning (falls into `llvm.unreachable`)

## 2. Ranked table

Score = `n_independent × risk × defect`. Risk: high 3, medium 2, low 1. Defect: 3 = new wrong code / crash / permissive confirmed by run; 2 = known open row, confirmed over-refusal, or ICE; 1 = inferred or historical only. The score favours rules with many paths, so it under-ranks single-door tier-1 defects; those are listed in §1.

| # | score | family | rule | n | risk | defect | main paths | linked defects |
|---|---|---|---|---|---|---|---|---|
| 1 | 171 | NAME | method-call probe (autoderef/autoref, inherent-first) | 19 | H | 3 | sema_expr.cpp :: lower_method_call (prelude, Deref loop, primitive arm, `$ref_` ladder, main loop, base-name loop, TypeVar arm, enum arm), try_method_on_{tuple,array,slice,dstref,raw_ptr,dyn,tagged}, try_blanket_method_dispatch; mono_clone.cpp :: subst_expr MethodCall; mlir_gen_expr.cpp :: gen_expr_kind(EMethodCallView); borrow_bir.inc resolvers | p15, p10, p11, p7b, p4; #537; 6dbe3a161 |
| 2 | 153 | TINF | literal adopts expected int type / E0600 | 17 | H | 3 | sema_impl.hpp :: intlit_fits, widen_int_expr, adopt_literal_widths_; ~100 per-site checks in lower_call, lower_method_call, lower_arr_lit, lower_struct_lit, … | lit_u64_neg, neg_u64_arg; 7ee7faa06, 86731b73a |
| 3 | 126 | TINF | expected type flows down, cleared elsewhere | 14 | H | 3 | sema_stmt.cpp :: lower_let, lower_let_pat, lower_return, lower_stmt_inner TAIL_EXPR, lower_assign_to; sema_expr.cpp :: lower_call, lower_method_call, lower_static_call, lower_struct_lit, lower_enum_lit_data(_from_static), ElemHintScope | leak_size, br_*; #518 |
| 4 | 90 | CG | store value of T into place | 10 | H | 3 | mlir_gen_stmt.cpp :: gen_assign, gen_let_inner, gen_index_write, gen_field_index_write, SDerefWrite, gen_field_write, gen_chain_field_write, gen_tuple_write, bind_name_at_slot; mlir_gen.cpp :: gen_struct_lit | dstasg |
| 5 | 81 | PLACE | plain assignment | 9 | H | 3 | sema_stmt.cpp :: lower_assign_to, lower_place_assign, try_index_mut_assign, lower_temp_rooted_place_assign_, try_schema_field_write, DEREF_WRITE arm, lower_destructure_assign::assign_place; mlir SDerefWrite, gen_assign; BIR DerefWrite | #521 (+p13), #508, #531, #614, p30 |
| 6 | 81 | COER | unsize to dyn | 9 | H | 3 | sema.cpp :: types_compatible (TraitObject blanket arm); sema_expr.cpp :: coerce_arg_to_dyn, expect_type (Return only); mlir ECall/EMethodCall blocks, gen_return, gen_let_inner/gen_assign, gen_struct_lit, gen_arr_lit, ECast | l03b, d02, d03, d01/d04; #569, #583 |
| 7 | 72 | NAME | unsafe fn call E0133 | 8 | H | 3 | finish_generic_call, lower_call ×2, lower_static_call, lower_method_call ×4, try_method_on_dyn/tagged; **missing** in try_method_on_slice/array/tuple and the `$ref_` ladder | p6, p6d, p6e |
| 8 | 72 | TINF | generic args inferred jointly | 12 | H | 2 | sema_expr.cpp :: infer_type_args vs raw unify_types in lower_call (tuple ctor), lower_generic_call, lower_enum_lit_data ×2, lower_struct_lit, lower_method_call side loops, try_blanket_*, try_method_on_slice ×4 | r_tstruct/r_enum/r_struct, lit_nested_T; #540, #545 |
| 9 | 63 | MOVE | by-value use records a move of the place | 7 | H | 3 | sema_impl.hpp :: mark_moved_expr, mark_moved_in_expr_recursive (private VarRef arm); sema_expr.cpp :: lower_struct_lit `..base`, tuple-lit arm; sema_stmt.cpp :: lower_let_pat_bound string paths; borrow_bir.inc operand() | h, i (double free), b, j |
| 10 | 63 | MOVE | CFG join union + drop flags | 7 | H | 3 | lower_if, lower_if_expr, lower_match, lower_match_expr, lower_binop sc_fork, merge_loop_exit_moves, lower_for_each (none), lower_let_else_core, lower_block_expr revert | g; #547 |
| 11 | 60 | GEN | does C implement Tr | 10 | H | 2 | sema_collect.cpp :: check_type_bounds, sema_has_impl_recursive, check_supertrait_impls; ~20 `has_impl‖has_impl` sites; mono_clone.cpp :: populate_trait_engine_, mono_concrete_satisfies_bound, method_bound_ok, instantiate_enum_templates; mono.cpp eager pass | 4c504130e (#469), sized_supertrait_blocks_impl_refused |
| 12 | 54 | PLACE | compound assignment | 6 | H | 3 | sema_stmt.cpp :: lower_compound_assign, lower_place_compound_assign (G167-5, eval-once, read-twice), DEREF_COMPOUND arm; mono_clone.cpp BinOp table | p16, p17, p2, p5, p4, p3, p22, p9 (E0368 admitted); #506 residual |
| 13 | 54 | NAME | inherent before trait; bound call binds trait item | 6 | H | 3 | sema_collect.cpp :: collect_impl G156-5; lower_typaram_static_method; mono_clone.cpp subst_expr Call/MethodCall; `$ref_` ladder placement | #510, p15, #516 |
| 14 | 54 | NAME | UFCS resolves to that trait's impl | 6 | H | 3 | lower_static_call (trait-UFCS arm, `<T as Tr>` arm, generic fallback, Self::), try_blanket_static_dispatch; mono_clone.cpp static retarget | p2, p3 (E0277 admitted); #537 |
| 15 | 54 | NAME | `&mut self` needs a mutable receiver | 6 | H | 3 | lower_method_call main loop inline lambda, sema_impl.hpp :: sd_thin_compatible, try_method_on_dstref/tuple/array, 3 private wants-mut predicates | p11, p7b |
| 16 | 54 | MOVE | reassign re-inits, drop-before-replace | 6 | H | 3 | lower_assign_to, lower_place_assign, DEREF_WRITE arm, try_index_mut_assign & co, lower_destructure_assign; mlir gen_stmt_kind(SAssignView) B8 | c, e; #508 |
| 17 | 54 | MOVE | scope exit drops every live local once | 6 | H | 3 | sema.cpp :: emit_frame_drops (core); lower_closure_expr epilogue (&body_ever_moved_), lower_stmt temp wrap, EXPR_STMT/`let _` discard ×2, 5 bind-drop-yield copies; BIR exit_scopes | a; #547, #525 |
| 18 | 54 | COER | coercion-site judgment | 6 | H | 3 | sema_expr.cpp :: expect_type/mask_for (core), coerce_arg_to_param; sema_stmt.cpp :: apply_place_coercions; pre-coerce in lower_call/lower_method_call/lower_static_call/try_method_on_slice; lower_let extra checks; overload scoring | l05, l06, l08, l09 |
| 19 | 54 | CG | unsize at coercion sites (codegen) | 6 | H | 3 | ECall, EMethodCall inline blocks; coerce_concrete_source_to_dyn; gen_return; coerce_value_to_dyn_if_needed; gen_arr_lit; ECast; **missing** in gen_index_write, SDerefWrite, tuple write, dyn/closure call args, EIfExpr join | dync0–dync4, join2 |
| 20 | 54 | CG | inline storage type / stride | 6 | H | 3 | mlir_gen_expr.cpp :: place_slot_type (core); mlir_gen_types.cpp :: logos_to_mlir(Array), tuple_llvm_type; EIndexRead ladder; gen_index_write→subscript_elem_type; gen_arr_lit | dstasg |
| 21 | 54 | CF | divergence is one fact | 6 | H | 3 | sema_stmt.cpp :: stmt_always_returns, stmt_always_diverts, body_always_diverges_simple, is_infinite_loop_node, branch_div_kind/expr_arm_div_kind; mlir_gen_fn.cpp :: gen_fn_body | p12 (SIGSEGV); #548 |
| 22 | 54 | GEN | blanket applicability | 9 | H | 2 | check_type_bounds, sema_has_impl_recursive, viable_blanket_impls, assoc_eqs_satisfied, check_supertrait_impls, lower_method_call abstract-T, subst_type_sema; mono.cpp eager pass; mono_subst.cpp subst_type; populate_trait_engine_ | #516; B-mv-03 |
| 23 | 52 | TINF | unsuffixed literal default | 13 | M | 2 | lower_let ×2, fill_inferred_from_rhs, unify_types, infer_unify_rec_, infer_type_args, TUPLE_LIT/RANGE_EXPR, lower_arr_lit, lower_if_expr, lower_match_expr, lower_method_call; mlir logos_to_mlir/ELitInt | lit_id_big; let_tuple_literal_width_from_later_use_refused |
| 24 | 45 | PAT | exhaustiveness E0004 | 5 | H | 3 | sema_stmt.cpp :: ast_patterns_exhaustive, refuse_uncovered_aggregate, check_match_exhaustiveness, lower_match_expr inline copy; mlir gen_match / EMatchExpr exhaustive_discrete | non-exhaustive int/char/&Enum/&[T] SIGSEGV (unrowed); #602 |
| 25 | 45 | PLACE | `a[i]` Index vs IndexMut | 5 | H | 3 | lower_index_read, try_index_mut_assign, lower_place_assign refusal, G167-5 block, lower_index_place | p2, p30; generic_index_output_refused |
| 26 | 45 | NAME | binary op → trait method | 5 | H | 3 | lower_binop (~12 arms, 3 op tables); mono_clone.cpp BinOp; mlir EBinOp; find_op_assign_impl | p5, p5c, p9, p9c (MLIR crash); #603 |
| 27 | 45 | COER | branch LUB | 5 | H | 3 | lower_if_expr, lower_match_expr, BREAK arm, lower_arr_lit, lower_binop; mlir coerce_numeric on if arms | m01/m02/b01/a01 (write via &), m03b/b02/a02 (truncation), m04–m06, l01–l04 |
| 28 | 45 | CG | return ABI | 5 | H | 3 | mlir_gen_fn.cpp :: make_fn_type, fn_call_ret_llvm_type; mlir_gen_impl.hpp :: llvm_fn_ret_type; gen_tagged_dispatch; forward_declare vararg | dynpair, clotup2, genfn, itermap3, boxdyn, e64 |
| 29 | 45 | CF | early exit drops live locals | 5 | H | 3 | push_stmt_with_unwind, make_return_with_drops, lower_stmt pending_ret_bind_ (shared); **bare** RETURN_EXPR/BREAK_EXPR/CONTINUE_EXPR, TRY_EXPR reparse, lower_match tail-position arms | cfaudit p1–p7 |
| 30 | 45 | CF | returned value judged/consumed identically | 5 | H | 3 | lower_return vs TAIL_EXPR typed/closure branches, RETURN_EXPR, tail-match arms | tail `t.0` double drop; #524 |
| 31 | 42 | CG | place address vs value read | 7 | H | 2 | gen_lvalue_addr, gen_recv_struct, EAddrOfTemp legacy, EAddrOf, EIndexRead/ETupleIndex/ESliceIndex/EDeref/EFieldRead, all write stmts, gen_tagged_dispatch receiver | match_array_field_place_verifier_error_refused (closed) |
| 32 | 36 | PAT | arm test | 4 | H | 3 | pat_test (core), gen_match chain, EMatchExpr chain, SLetElse | #517; slice suffix; `[1..=5,_]` |
| 33 | 36 | PLACE | place address (mlir) | 4 | H | 3 | gen_lvalue_addr, EAddrOfTemp, EIndexRead, sema place_write_supported | p1a–p1d |
| 34 | 36 | CG | pattern lowering doors | 4 | H | 3 | gen_match, EMatchExpr, SLetElse, pat_test/pat_bind | rr (#517), le |
| 35 | 36 | GEN | assoc-type projection | 6 | H | 2 | resolve_type_assoc_ref, find_assoc_type_entry, subst_type_sema, assoc_eqs_satisfied; mono.cpp assoc_impls_; mono_subst.cpp subst_type | G156-1; positional vs unify (inferred) |
| 36 | 36 | NAME | overload selection | 9 | M | 2 | resolve_function_call, find_func_by_base_and_signature, find_generic_func ×2, find_generic_func_for_args, lower_call, lower_static_call, lower_method_call, try_method_on_slice, find_op_assign_impl, lower_binop | p4; #564 |
| 37 | 36 | GEN | deferred bound obligations | 4 | H | 3→2 | check_type_bounds defer, lower_call/lower_static_call in_generic_context, lower_impl_block, method_bound_ok (silent drop) | impl_method_extra_bound_admitted, trivial_false_predicate_admitted |
| 38 | 36 | GEN | impl method identity / vtable slots | 4 | H | 3 | collect_fn lazy re-key, lower_method_call, mono method_instance_name family, mlir_gen_dyn.cpp :: emit_trait_vtables resolve_methods | #510; vtable-by-name (inferred) |
| 39 | 27 | NAME | call diverges iff callee returns `!` | 3 | H | 3 | is_divergent_call_node, stmt_always_returns lambda, body_always_diverges_simple lambda | p8, p8c, p8d (SIGSEGV) |
| 40 | 27 | TINF | closure env escape | 3 | H | 3 | lower_closure_expr G167-3b, lower_return returned_closure_node_, collect_returned_closure_lets_ | tail_box, let_ret, opt_*, tup_*, st_*; loop_local_move_closure_shares_slot_wrong |
| 41 | 27 | GEN | coherence | 3 | H | 3→2 | collect_impl coherence_keys_, viable_blanket_impls, mono eager pass | #516, orphan_rule_not_enforced_admitted |
| 42 | 24 | COER | int widening never in place | 4 | H | 3→2 | types_compatible recursion, widen_int_expr (~35 direct calls), unify_int/unify_numeric, mlir coerce_int/coerce_numeric | q01c, q03, q07, q06b, p11, f01/f02 |
| 43 | 18 | COER | ref/raw-pointer coercions | 3 | H | 3→2 | types_compatible (Ptr→Ref arm, Ref→Ptr no mut guard, decay arm), try_implicit_reborrow_mut, subtype.hpp | p04, q09, q10, p05 |
| 44 | 28 | CF | loop-body frame protocol | 7 | M | 2 | lower_while, lower_for, lower_loop, lower_for_each ×4 | cf7cf545a |
| 45 | 28 | TINF | closure params from expected callable | 7 | M | 2 | lower_closure_expr, closure_hint_for, formals_hint, closure_hint_from_fn_bound, lower_return, lower_let, lower_enum_lit, infer_type_args | #526, #524, #580, #540, #511 |
| 46 | 27 | CF | integer range iteration | 3 | H | 3 | lower_for, gen_for, RANGE_EXPR | usize refused, `(lo..n)` truncation, `..=i64::MAX` wrap |
| 47 | 27 | CF | `break` value | 3 | H | 3 | BREAK stmt, BREAK_EXPR, mono_clone.cpp SLoop | `loop { break s }` double drop; #575, #523 |

Rules scoring below 24 are listed per family in §3.

## 3. Per family

Legend: **S** = uses a shared core; **R** = re-implements the rule. "n" = independent implementations.

### 3.1 Place expressions and assignment (8 rules, 40 paths)

| rule | n | risk | paths (S/R) | defects |
|---|---|---|---|---|
| plain assignment `place = v` (value first, check, drop old, mark moved) | 9 | H | R: sema_stmt.cpp :: lower_assign_to, lower_place_assign, try_index_mut_assign, try_schema_field_write, lower_stmt_inner DEREF_WRITE, lower_destructure_assign::assign_place; mlir_gen_stmt.cpp :: gen_stmt_kind(SDerefWriteView) (place first), gen_assign; borrow_bir.inc DerefWrite (value first). S: lower_temp_rooted_place_assign_ (the only value-first sema path) | #521 + DEREF_WRITE twin p13; #508; #531; #614; **new** p30 (no type check in try_index_mut_assign, permissive) |
| compound `place op= v` (primitive RHS-first, eval once, `*Assign` for any kind, E0368) | 6 | H | R: lower_compound_assign, lower_place_compound_assign ×3 blocks, DEREF_COMPOUND arm, mono_clone.cpp BinOp table. S: op_assign_trait_method, find_op_assign_impl, compound_rhs_first, ast_has_call | #506 closure keyed on the spelling `ast_has_call`: p16, p17, p2 double eval; p5 bare var not RHS-first; Struct-only `*Assign` in 4 copies (p4 enum MLIR fail, p3 generic refused, p22 `M__add`); p9 E0368 admitted |
| `a[i]` Index vs IndexMut | 5 | H | R: lower_index_read (mut_place_ctx_ side channel), try_index_mut_assign, lower_place_assign refusal, G167-5, lower_index_place (receiver lowered twice) | p2, p30; 508489016 per-door fix |
| writability E0594/E0596 | 7 | M | R: lower_assign_to, lower_compound_assign, check_place_writable + resolve_place_type (AST type re-derivation), lower_place_assign ptr check, DEREF arms, try_index_mut_assign/G167-5, assign_place; BIR backstop | none open; wrong sentence on the eval-once path (p19, p20) |
| place address = real storage | 4 | H | R: mlir_gen_expr.cpp :: gen_lvalue_addr, gen_expr_kind(EAddrOfTempView) (legacy IndexRead, silent spill fallback), EIndexReadView; sema_stmt.cpp :: place_write_supported. S: is_place_chain. Dead: 6 retired write stmts in ~9 consumers | **new** p1a–p1d (`h.p[i]` with a `&[T;N]` field: garbage read, lost write, `+=` SIGSEGV); `Kind::Ptr`-only test in 3 copies |
| store T into slot | 3 | M | R: gen_assign, SDerefWrite, gen_let/declare_local_place (+ dead gen_*_write) | historical #80, b167/b168 |
| assignment in tail position is a statement | 2 | H | S: sema_expr.cpp :: is_stmt_only_code (DEREF_COMPOUND missing, 12 dead codes). R: lower_if_expr::lower_block_last_expr (own list) | **new** p25, p26 (if-branch assign vanishes), p23 (`{ *p += 4; }` vanishes) |
| ref-typed local slot is loaded | 4 | M | R: EVarRef read, get_struct_ptr, gen_lvalue_addr VarRef, EAddrOfTemp legacy (4 different set intersections of ref_slot_vars_/var_local_ptrs_/let_vars_) | inferred only |

### 3.2 Pattern matching (10 rules, 38 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| exhaustiveness for all types | 5 | H | R: ast_patterns_exhaustive (int/char/slice/str undecidable), check_match_exhaustiveness (by-value Enum + bool), lower_match_expr inline copy, mlir gen_match / EMatchExpr exhaustive_discrete. S: refuse_uncovered_aggregate (Tuple/Struct only) | **new, unrowed, tier 1**: non-exhaustive i32/char/u8/`&Enum`/`&[T]` compiles and falls off the end (SIGSEGV/garbage) |
| arm test | 4 | H | S: pat_test (dyn slice suffix untested). R: gen_match chain (inline RefPat/Slice/Range/At/Or), EMatchExpr chain (get_disc default 0), SLetElse | #517 two failure modes; `&(1,_)` wrong/ICE; `[..,3]` stmt vs expr split (G167-6a fixed only in expr); let-else suffix; `[1..=5,_]` ignored |
| binding | 4 | H | S: pat_bind (no dyn slice), bind_enum_payload & co. R: extract_payload, extract_arm_payload, collect_pat_bindings, bind_pattern/bind_pattern_ref whitelist | let-else `[x,..]` over `&[T]` ICE; tuple-with-slice refused |
| refutability E0005 | 4 | H | R: pattern_irrefutable, ast_pat_irrefutable, lir_view::is_irrefutable_pattern, lower_let_pat_bound is_single_variant_struct_pat. S: EMatchExpr is_wild | #596, #635, let_binding_named_like_const_admitted (tier 1) |
| match lowering one rule stmt/expr | 2 | M | R: lower_match, lower_match_expr (byte copy of the fan-out; no E0507-at-arm, no B-st-07, no Never early return). S: if-let/while-let/let-chain delegators | #547, #630, #599 (placement unverified) |
| nested refutable subpattern tested | 3 | M | R: build_pattern_variant_data guard channel, build_pattern_impl struct-field door, tuple door ×2. S: emit_nested_*; pat_test | #616; ordering-held property |
| default binding modes | 4 | M | S: mint_dbm_ref & co, pat_scrut_one_layer, ref_bind_kind. R: build_pattern_variant_data (only node carrying modes), BIR bind_pattern (re-derives from type), borrow_check declare_pat_bindings, borrow_flow_summary bind_pat | B-5, 7dde577ec |
| or-patterns | 3 | M | S: build_pattern_or, check_or_alt_binding_consistency; pat_test/pat_bind Or. R: fan-out ×2, SLetElse or_discs | 44c12b326, 46008d9da |
| let door (let/for/param) | 5 | M | R: lower_let_pat_bound 5 sub-paths, plain lower_let. S: lower_let_pat(_rhs), for/param/closure delegators | #596, #585, #620 |
| literal/str/const in pattern | 4 | M | R: str_at_arm ×2, build_pattern_impl str/const doors, unit-variant/const identity asked 3 ways, render_pat_src (lossy) | #609, #607, #568, #639; format!-reparse refusal |

### 3.3 Name / method / call resolution (11 rules, 76 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| method probe | 19 | H | see §2 row 1. S: try_blanket_method_dispatch, generic-enum arm | p15 (12 vs 22), p10 (2 vs 1), p11, p7b, p4 |
| inherent vs trait priority | 6 | H | collect_impl G156-5, lower_method_call tag_trait, lower_typaram_static_method (no tag), mono_clone Call (composes `<cname>__f`), MethodCall, `$ref_` ladder placement | #510 (100 vs 200), #516, p15 |
| UFCS / `<T as Tr>` | 6 | H | lower_static_call trait-UFCS arm (never checks the impl), `<T as Tr>` arm (falls to inherent), generic fallback, Self::, mono retarget. S: lower_typaram_static_method, try_blanket_static_dispatch | p2, p3 admit; #537 |
| overload selection | 9 | M | resolve_function_call (1 caller), find_func_by_base_and_signature (exact, 55 sites), find_generic_func ×2, find_generic_func_for_args, lower_call chain, lower_static_call chain, literal-width pick ×4 | p4; #564 closed at 1 of 4 copies |
| scope/visibility | 7 | M | find_func_candidates (S), find_generic_func(base,n) visible lambda, find_generic_func(base) no filter, resolve_function_call tie-break, lower_call nulling, lower_binop package filter, mlir find_func_op | ec9032bb7, 5c2889a91 |
| call diverges iff `-> !` | 3 | H | is_divergent_call_node + 2 lambda copies (no scope lookup) | **tier 1** p8, p8c, p8d SIGSEGV |
| binary operator | 5 | H | lower_binop (12 arms, 3 tables), mono BinOp (4th table, no bit/shift), mlir EBinOp, *Assign table | p5, p5c, p9, p9c MLIR crash; #603 |
| unary operator | 2 | M | lower_unary, mono Unary (Struct only) | p9b |
| unsafe E0133 | 8 | H | 5 copies with the check, 4 paths without | p6, p6d, p6e |
| `&mut self` receiver | 6 | H | see §2 row 15 | p11, p7b |
| lang-item implicit calls | 5 | M | emit_generic_deref_* (S), 3 composed `__index(_mut)` sites, lower_for_each 8 lookups, drop_fn_for, find_op_assign_impl | generic_index_output_refused; G156-5b |

### 3.4 Type inference, literals, expected types (10 rules, 84 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| expected type flows down | 14 | H | 7 hint members × 14 producers; only ElemHintScope is shared (partially) | leak_size wrong T; br_* refusals; TAIL sets only the enum hint |
| closure env escape | 3 | H | G167-3b hint shape, returned_closure_node_, escaping_closure_lets_ | SIGSEGV family (tail_box, let_ret, letann_ret, opt_*, tup_*, st_*, if_tail); if_ret refused |
| closure params from expected callable | 7 | M | 3 different "is callable" predicates | #526, #524, #580, #540, #511 |
| literal default i32/f64 (+ blessed let upgrade) | 13 | M | 12 sema sites + mlir fallback | lit_id_big inconsistency |
| literal adoption / E0600 | 17 | H | intlit_fits `default: return true` for U64 | -1 → u64::MAX at 6 positions |
| joint generic-arg inference | 12 | H | infer_type_args (S) vs 11 raw unify loops | ctor/variant/struct-lit refusals; nested literals not deferred |
| return-only type param | 6 | M | infer_type_args (S), lower_static_call (mints ?iN), 4 private readers, lower_call (gives up) | rt_return/rt_tail/rt_arg/rt_assign; make_vec vs Vec::new |
| ?iN minted and solved | 3 | M | mint sites (S), expect_type (S), bypass solve sites, lower_var_ref hinted branch (R) | leak_size path; closure sig vars unsolved (inferred) |
| branch arms against the expected type | 3 | M | lower_if_expr, lower_match_expr, lower_match | br_ret, br_match_ret, br_arg |
| structural unification | 6 | M | unify_types (first-wins), infer_unify_rec_, match_type_sema, Mono::match_type, unify_impl_target, array_spelling_unify (string level) | sema/mono spec divergence (inferred) |

### 3.5 Coercions and compatibility (12 rules, 47 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| coercion-site judgment | 6 | H | expect_type/mask_for + coerce_arg_to_param (S); apply_place_coercions (narrower second pipeline); dead mask rows TupleElem/BranchArm; lower_static_call skips expect_type; try_method_on_slice; lower_let extras; overload scorer | l05, l06, l08, l09 |
| branch LUB | 5 | H | 4 merges + binop; mlir coerce_numeric | m01/m02/b01/a01, m03b/b02/a02, m04–m06/a03, l03b (tier 1), d03, l01–l04 |
| int widening never in place | 4 | H | types_compatible recursion leaks widening under `&`, generics, fn signatures; widen_int_expr AddrOfTemp arm | q01c, q03, q07, p11, f01/f02, q06b (lost write) vs q06c (refused) |
| ref/raw-pointer coercion | 3 | H | types_compatible arms (Ptr→Ref "needed for existing code", Ref→Ptr no mut guard, `&[T;N]`→`&T` decay), reborrow rewriters | p04, q09, q10, p05 (write into immutable); spec coerce.ref.permission-and-pointee contradicted |
| unsize to dyn | 9 | H | §2 row 6 | l03b, d02, d03, d01, d04; #569, #583 |
| array→slice / deref coercion | 4 | H | try_coerce_array_ref_to_slice (S), hardcoded `&Vec<T>`→`&[T]` arm (lenient element), Slice/Slice arm (exact), try_deref_coerce (S) | p11 = root of #518 |
| closure/fn item → fn ptr | 4 | H | types_compatible ×2 arms (types_equal vs lenient), try_coerce_closure_to_fnptr (keyed on ClosureBox shape), LUB copies ×2 | #580, l01, l02, f01/f02 |
| Never | 2 | L | types_compatible (S), hand skips per merge | let_else_nondiverging_else_admitted |
| `as` casts | 2 | M | lower_cast (blacklist), mlir ECast (own dispatch) | c01–c09 (E0604/5/6 admitted); c06 unregistered divergence |
| aggregate structural compat | 4 | H | Struct arm, Enum arm, elem_compatible, try_retype_bare_enum_arg: 5 differing "unresolved" lists | q07 |
| variance gate | 2 | M | check_variance (S) called by hand at 28 sites; absent at arms/breaks/tuple elements; runs after the rewrite | q06b vs q06c; enum_type_param_variance_admitted |
| pattern vs scrutinee type | 2 | M | check_variant_scrut (blacklist), build_pattern_variant | raw_pointer_matched_as_option_crash |

### 3.6 Closures and captures (9 rules, 28 paths; read only)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| capture set (RFC 2229) | 1 | M | sema_expr.cpp :: lower_closure_expr scanner (S); consumers in mono_clone, mlir gen_closure (silent skip of a missing capture), BIR, borrow_check | match-arm binder #444 remainder |
| capture mode | 4 | H | folded `mut_captures` bit (codegen, old BC, Fn-kind); `capture_modes` (BIR only); ownership keyed on the `move` keyword; mlir representation from var_* maps | fnonce_generic_consume_skips_capture_drop, #525; widened-mut classified Fn (inferred) |
| env allocation (escape) | 3 | H | G167-3b, returned_closure_node_, escaping_closure_lets_; entry-block alloca shared across loop iterations | loop_local_move_closure_shares_slot_wrong (tier 1) |
| affine / droppable closure | 4 | M | OWNED_ENV_BIT set only by lower_return; sema is_move_type, needs_drop, borrow_check twin, mlir value_needs_drop | fnonce_generic_consume_skips_capture_drop |
| capture drop ownership | 3 | H | sema owned_by_closure kind list vs mlir capture_own_inline (code maps; "MUST match exactly"); lower_let claim (no paren unwrap); Box<dyn FnOnce> dealloc only in lower_call | Box<[T]>/DST capture double free (inferred) |
| Fn-family / call mode | 4 | M | closure_kind_value, lower_call fn_bound_mode, lower_invoke_on (TypeVar replaced before the mode is asked), mono ClosureCall (probe order) | #440 |
| call through a callable | 4 | M | lower_call, lower_invoke_on, N7 field call, mono ClosureCall/FnPtrCall | #580, map_named_fn_item_collect_crash |
| closure signature typing | 3 | M | closure_hint_from_fn_bound (S), lower_call closure_hint_for (no subst), return inference (first wins, no Match/For) | #524, #526, #511 |
| closure body is a fn boundary | 2 | M | lower_closure_expr vs lower_fn; mlir gen_closure 2× ~15-map save lists | #523, closure_param_duplicate_binding_admitted |

### 3.7 Moves and drops (9 rules, 42 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| move records the exact place | 7 | H | mark_moved_expr (S) + ~45 per-site pre-gates; walker's private VarRef arm; string-built paths in struct-lit/let-pattern; BIR's independent facts | **new** h, i (`let y = { x }` double free: lower_block_expr revert justified only by the retired sema E0382), b (`..h.inner`), j (tuple of T) |
| join union + flags | 7 | H | elaborate_cond_moves (S); per-construct save/restore in 7 places; lower_for_each has no merge; stmt/expr guard-union mismatch | **new** g (for-each zero-trip leak) |
| reassign re-inits | 6 | H | 5 sema writers + mlir B8 | **new** c, e (leak on both paths); #508 |
| scope exit drops | 6 | H | emit_frame_drops (S); closure epilogue on old skip list; temp wrap ignores flags; discard ×2 with private is_place; 5 bind-drop-yield copies; BIR re-placement | **new** a (closure by-value param not dropped; f0b4028f6 fixed fn only) |
| one drop-flag carrier | 2 | M | sema `__df_N` vs mlir B8 (name-keyed, invisible to BIR) | gap = reassign leak |
| needs_drop / destructor | 5 | M | sema.cpp :: drop_fn_for/needs_drop (S) + inline copies; mlir value_needs_drop; borrow_check needs_drop, bir_drop_observes_borrow; 3 destructor resolvers | #123, mlirgen_odr_drop_glue_homonym |
| drop glue order | 2 | M | SDrop private struct/tuple loops (bare-name 3rd lookup) vs gen_drop_value (S) | inferred |
| move/init diagnostics owner | 4 | M | BIR (default), borrow_check (fallback), 8 sema E0507 sites, sema E0508; currently_uninit_vars_ write-only in 12 functions | vestiges cause h/i and the guard mismatch |
| drop order incl. captures | 3 | M | emit_frame_drops groups, BIR exit_scopes, closure_owned_drop_ cascade | #525 |

### 3.8 Control flow (15 rules, 58 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| early exits drop locals | 5 | H | §2 row 29 | **new** RETURN_EXPR/BREAK_EXPR/CONTINUE_EXPR leaks; `?` Box<dyn> leak; tail-match expr arms leak |
| return value one judgment | 5 | H | lower_return vs 2 TAIL_EXPR branches, RETURN_EXPR, tail-match arms | **new** tail `t.0` / `s.a` / closure double drop (#110 R1 fixed only in `return`) |
| break value / target | 3 | H | BREAK stmt vs BREAK_EXPR (no label check, no mix check, no move); mono SLoop result_type not substituted | **new** `loop { break s }` double drop; mixed break E0381 on internal name; generic loop-break mlir fail; #575, #523 |
| labels | 3 | M | two label stacks; expression forms unchecked; mlir silent innermost fallback | #523, #594 |
| zero-trip loop merge | 3 | H | merge_loop_exit_moves in while/range-for only | **new** for-each over empty collection leaks (array, `&v`, iterator) |
| loop-body frame protocol | 7 | M | lower_while, lower_for, lower_loop, 4 copies in lower_for_each | cf7cf545a |
| integer range iteration | 3 | H | lower_for (usize = 32), gen_for (usize = 64), RANGE_EXPR (only I64/U64 are 64) | **new** usize refused; `(lo..n)` truncation (silent); `..=i64::MAX` wraps |
| IntoIterator protocol | 4 | H | lower_for_each special shapes, IntoIterator block (bare key), next()/into_iter fallback, comprehensions (bind by value) | **new** `[x+1 for x in s]` MLIR verifier failure |
| value block tail | 5 | M | lower_block_expr (tail return lowered twice), if-expr lambda, match-expr arms, UNSAFE_BLOCK (no scope), metacall | #118 R2 |
| divergence one fact | 6 | H | §2 row 21 | **new** p12 SIGSEGV |
| match stmt/expr/let-else | 3 | H | lower_match, lower_match_expr, lower_let_else_core; mlir ×3; BIR shared | schema-enum intercept missing in the expression form (inferred) |
| `?` desugar | 4 | H | 4 TRY_EXPR branches (2 reparse, operand lowered twice); dead ETry still in 6 consumers | Box<dyn> leak; #551 |
| if/else | 2 | M | lower_if vs lower_if_expr (no uninit merge) | #118 R2, #122 |
| main entry ABI | 3 | L | declare, fall-through, gen_return unit_main copies; no Termination | #514, #597, #631 |
| mono walkers see all stmts | 2 | L | compute_const_want skips For/ForEach/Match/LetElse | inferred |

### 3.9 Generics, traits, mono (13 rules, 58 paths; read only)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| trait satisfaction | 10 | H | §2 row 11 | #469; sized_supertrait_blocks_impl_refused; owning Box<dyn> passes Copy (inferred) |
| auto traits | 2 | H | sema_auto_trait.cpp :: is_auto_trait_satisfied vs mono_clone.cpp :: is_auto_satisfied ("Mirrors"): ≥7 divergent cases | inferred |
| blanket applicability | 9 | H | §2 row 22 | #516; unbounded blanket assoc unresolved in sema (inferred) |
| Self binding | 6 | H | collect_impl header, collect_impl defaults, lower_impl_block seed_self, lower_impl_block defaults, lower_fn (datatype-first), struct/trait | G160-3; str/lt-args divergence (inferred) |
| default-method instantiation | 3 | M | collect_impl (no where-gate), lower_impl_block (where-gate, alt=""), orphan path; mono method_bound_ok | missigned default (memory); signature without body (inferred) |
| assoc projection | 6 | H | §2 row 35 | G156-1 family rows |
| `$G` trait-arg suffix | 3 | M | sema trait_targ_suffix (regions erased) vs 2 mono copies (regions kept) | wrong impl under a lifetime arg (inferred) |
| instance mangling | 3 | H | sema mangle_type_for_name family, mono mangle_type/enum_instance_name, want_sig text compare | recorded in mono_impl.hpp comments; want_sig never matches tuple/slice (inferred) |
| impl method identity | 4 | H | §2 row 38 | #510; dyn homonym slot (inferred) |
| coherence E0119/E0117 | 3 | H | collect_impl non-generic only, viable_blanket_impls call-site, mono none | #516, orphan row |
| specialisation selection | 3 | M | find_best_sema_struct_spec (silent tie), find_best_struct_spec (ambiguity error), find_best_spec | inferred layout mismatch |
| deferred obligations | 4 | H | sema defers, in_generic_context door, mono drops silently | E0276 and trivial-predicate rows |
| vtable layout / object safety | 2 | M | trait_vtable_layout (includes Sized-only methods), check_trait_object_safe (excludes them), mlir slot fill by name | #569, #583 |

### 3.10 Codegen shape (10 rules, 51 paths)

| rule | n | risk | paths | defects |
|---|---|---|---|---|
| return ABI | 5 | H | make_fn_type, fn_call_ret_llvm_type, llvm_fn_ret_type (no Tuple), gen_tagged_dispatch, forward_declare; emit_static_globals patches Tuple locally | **new** dynpair SIGSEGV; closure tuple returns garbage (clotup2, genfn, itermap3, boxdyn); e64 `ret i32` |
| param ABI / indirect call type | 5 | M | make_fn_type (S with apply_param_attrs), gen_closure ×2 loops, indirect calls typed from argument values, ECall/EMethodCall copies, vararg | G167-7; latent |
| unsize at coercion sites | 6 | H | §2 row 19 | dync0–dync4, join2; the BC admits `*p = b` for `&mut &dyn` (out of family) |
| storage type / stride | 6 | H | §2 row 20; layout_of is the checker | dstasg |
| store into place | 10 | H | §2 row 4 | dstasg; history #80, C5-cl-04 |
| place address vs read | 7 | H | §2 row 31; 5 variable-home tables consulted in ~22 functions | closed c02 |
| pattern lowering | 4 | H | gen_match (~1250 lines), EMatchExpr (~1150 lines), SLetElse, pat_test/pat_bind (S) | rr (#517), le |
| branch-join repr | 4 | M | EIfExpr ×2 inline, store_arm_result, gen_return/ETry/EBlockExpr | #94, G161-4, join2 |
| enum repr | 3 | H | enum_* chokepoints (S), logos_to_mlir→enum_disc_mlir (S), make_fn_type i32, gen_return hand wrapper, match doors | e64 |
| drop glue | 1 | L | gen_drop_value (S) | none; already one implementation |

## 4. Consolidation plan

### 4.1 Proposed core

Two layers, modelled on rustc's HIR → typeck/MIR split.

**A. HIR-like desugaring layer (type-free, AST → core AST, structured Writ rewrites; no render-and-reparse).** Every construct that rustc desugars in AST→HIR lowering is lowered once, before sema:

| surface | core form | retires |
|---|---|---|
| if-let / while-let / let-chains | `match` (done today via synth_match) | nothing new |
| `while c {}` | `loop { if !c { break } … }` | lower_while frame copy, zero-trip special case |
| `for p in e` (every spelling incl. ranges, comprehensions) | `match IntoIterator::into_iter(e) { mut it => loop { match Iterator::next(&mut it) { Some(p) => body, None => break } } }` | SFor, gen_for, lower_for, 4 lower_for_each copies, 3 comprehension lowerers, range width tables. Needs stdlib IntoIterator for arrays/slices; array/slice loops become a mono/codegen optimisation |
| `e?` | `match Try::branch(e) { Continue(v) => v, Break(r) => return FromResidual::from_residual(r) }` | 4 TRY_EXPR branches, ETry and its 6 consumers, operand double lowering |
| destructuring assign `(a, b.f, *c) = e` | `{ let (__0, __1, __2) = e; a = __0; b.f = __1; *c = __2; }` | assign_place; closes #531, #508 |
| implicit tail of a fn/closure body | explicit `return <tail>` marker | TAIL_EXPR typed/closure branches, tail-match arm returns |
| expression-position `return`/`break`/`continue` | the same node as the statement form (the value/expression distinction is only the result type `!`) | RETURN_EXPR / BREAK_EXPR / CONTINUE_EXPR copies |
| labeled blocks, `loop` value | one Loop node with label + break slot | two label stacks |

**B. Single typed implementations in sema (typeck), consumed as recorded facts by mono / BIR / mlir_gen.**

| core | contract | replaces |
|---|---|---|
| **C-EXP** `ExpectedTypeScope` / single `expected_` TypeRef | derives every projection (enum/struct/tuple/elem/callable/return) and clears on entry | 7 hint members, 14 producer setups |
| **C-LIT** integer/float inference variables in the ?iN table | defaulted once in infer_close_fn_ (the blessed let-overflow rule lives there); range check in expect_type's WIDEN_INT only | 13 default sites, ~100 range checks, mlir IntLit fallback |
| **C-INF** `infer_generic_args` | infer_type_args policy + conflict detection + ?iN for unbound params | 11 raw unify loops, lower_static_call private minting |
| **C-COE** `unify_in_place` + `coercion_plan(from, to, mask)` + `expect_type` as the only applier + `lub_arms` + cast whitelist | ordered adjustment list (Never, Reborrow, RefToRaw, Deref, Unsize{array, dyn, CoerceUnsized, upcast}, ClosureToFnPtr by capture set, FnItemToFnPtr, WidenInt); each step emits explicit LIR (unsize = ECast); variance on the original types | apply_place_coercions, 9 pre-coerce sites, 4 LUB merges, lenient types_compatible recursion, Vec/Ptr→Ref/TraitObject blanket arms, 28 manual check_variance calls, mlir per-site coerce_to_dyn and value-site coerce_numeric |
| **C-PLC** `lower_place(node, Use{Read, Write, MutBorrow, SharedBorrow})` → LIR place with overloaded Index/Deref steps bound once; `lower_assignment` (value first), `lower_compound` (place once, primitive RHS-first, `*Assign` by trait resolution else E0368); `check_writable(LirPlace)` | lower_place_assign, try_index_mut_assign, G167-5, eval-once block, read-twice tail, DEREF_WRITE/DEREF_COMPOUND arms, lower_index_place, resolve_place_type, place_write_supported, mono BinOp compound rewrite |
| **C-PAT** usefulness matrix (exhaustive + refutable for all types), `lower_match_core`, `resolve_pat_ident`, explicit binding mode on every binder, PatStr LIR node | ast_patterns_exhaustive + check_match_exhaustiveness + inline copy + refuse_uncovered_aggregate whitelist, pattern_irrefutable, ast_pat_irrefutable, lower_let_pat_bound special paths 1–4, match fan-out copy, guard channel, BIR/borrow_check mode re-derivation |
| **C-RES** `MethodProbe` (one receiver-step fn + `impl_keys_for(TypeRef)`), `emit_call` chokepoint (unsafe/pub/receiver mut/coercion/moves), `resolve_trait_item(trait id, self, name)`, `resolve_value_path(scope)`, `select_overload`, `op_lang_item` table; callee identity recorded on LIR | try_method_on_*, lower_method_call arms, 4 op tables, mono subst MethodCall/BinOp/Unary/Call re-dispatch, mlir name composer, BIR spelling resolvers, 3 divergent-call scans (divergence becomes the Never type) |
| **C-OWN** `consume(place)`, `JoinBuilder`, `reinit(place, rhs)`, `epilogue(frame)`, `emit_exit(kind, label, value)`, `lower_return_value`; one flag carrier; `DropInfo` per concrete type. Long-term: drop elaboration from BIR move facts | ~45 pre-gates, walker VarRef arm, string paths, 7 save/restore/union copies, lower_block_expr revert, body_ever_moved_, currently_uninit_vars_, B8, 5 bind-drop-yield copies, 8 sema E0507 sites |
| **C-CLO** per-capture upvar record {place, mode, widened, owned_by_env} in the Closure type; escape from value flow (box/unsize ECast, return, outliving store); `lower_callable_call`; `enter_fn_body` guard | folded mut bit, var_*-map representation, two ownership predicates, 3 escape heuristics, 4 call paths, closure_hint_for |
| **C-OBL** shared sema+mono obligation solver (TypeRef/DefId): candidate keys, builtins, auto traits, `blanket_applies`, `resolve_projection`, `select_spec`, param-env mode; `impl_self_type` on SemaImplInfo; default verdict in collect_impl; post-collection overlap pass; shared type-arg mangler; impl items as IDs to mono and vtables | 10 satisfaction engines, 2 auto engines, 9 blanket loops, 6 Self derivations, 6 projection paths, 3 `$G` composers, 2 manglers, name-matched vtable fill |
| **C-REPR** (mlir) `ReprOracle`: value/storage/return/param type per TypeRef, `fn_sig`, `EnumRepr`, `place_addr`, `store_to_place`, `to_slot_repr`, `VarHome` record | make_fn_type/fn_call_ret_llvm_type/llvm_fn_ret_type trio, hand element ladders in logos_to_mlir/tuple_llvm_type, 10 store ladders, EAddrOfTemp legacy + silent spill, 6 dead write stmts, var_elem_types_ as stride source, gen_match/SLetElse (match stmt = void match expr, let-else = one-arm match) |

### 4.2 Migration order (by harm, then blast radius)

Each step lands with its fixtures (the confirmed probes above become `tests/logos/pass`/`fail` in the same commit) and deletes the listed paths in the same series. A deletion that cannot land gets a row with one of the three named reasons.

| step | scope | why here | deletes |
|---|---|---|---|
| **S0** one-point class closures at an existing chokepoint (≤ 20 lines each; not a substitute for the steps below) | intlit_fits U64/I64 negative; is_stmt_only_code gets DEREF_COMPOUND + lower_block_last_expr calls it + lower_expr default → bug(); refuse on any decided-non-exhaustive type; pat_test dyn-slice suffix; mono_clone SLoop result_type subst; merge_loop_exit_moves in lower_for_each; closure epilogue passes fall-through state; delete lower_block_expr's tail-move revert; is_divergent_call consults lookup() first; BREAK_EXPR mark_moved; `Kind::Ptr` → ref_repr_of in the FieldRead index copies | stops ~15 confirmed silent wrong-code programs now; each fix goes at the one place already shared | lower_block_expr revert, lower_block_last_expr's private list, 12 dead codes in is_stmt_only_code |
| **S1** C-REPR ABI part: `ret_abi_type`/`fn_sig`/`EnumRepr` | mlir-local, no sema change; SIGSEGV/garbage on every tuple-returning closure and dyn method | llvm_fn_ret_type, fn_call_ret_llvm_type (alias to one), emit_static_globals Tuple patch, gen_tagged_dispatch/indirect call typing from argument values, gen_return enum wrapper, gen_closure param loops |
| **S2** HIR exits + C-OWN exit half: tail→return, one exit node, `emit_exit`, `lower_return_value`, `lower_break`, `lower_value_block` | leaks/double drops on every expression-position exit; prerequisite for S5 | RETURN_EXPR/BREAK_EXPR/CONTINUE_EXPR arms, TAIL_EXPR typed/closure branches, match_in_tail_position_ arm returns, make_return_with_drops internals, UNSAFE_BLOCK private block lowering, lower_if_expr lambda, BIR E0381 on internal names |
| **S3** C-PAT + C-REPR pattern half | tier-1 non-exhaustive SIGSEGV; #517; mostly self-contained | check_match_exhaustiveness arms, lower_match_expr inline copy, refuse_uncovered_aggregate whitelist, pattern_irrefutable, ast_pat_irrefutable, match fan-out byte copy, gen_match + SLetElse (→ EMatchExpr/void), inline RefPat/Slice/Range/At/Or branches, get_disc/get_scalar_disc catch-alls, exhaustive_discrete ×2 (reads a sema flag) |
| **S4** C-COE + unsize as sema ECast + closure escape at the unsize point (C-CLO escape half) | writes through `&`, wrong vtable, raw pointer→ref, garbage reads, closure stack env; widest permissive class | apply_place_coercions, dead mask rows (made live), pre-coerce calls in lower_call/lower_invoke_on/lower_method_call ×2/try_method_on_dyn/lower_static_call/try_method_on_slice, 4 LUB merges, types_compatible lenient recursion + Vec/Ptr→Ref/decay/TraitObject arms, 5 concrete_scalar lambdas, 28 manual check_variance calls, mlir unsize blocks in ECall/EMethodCall/gen_let_inner/gen_assign/gen_return/gen_struct_lit/gen_arr_lit/literals, value-site coerce_numeric, G167-3b/returned_closure_node_/escaping_closure_lets_, lower_cast blacklist |
| **S5** C-OWN rest: consume/JoinBuilder/reinit/epilogue/DropInfo; retire B8 | double frees and leaks (h, i, b, j, c, e, g, a); needs S2 | per-construct save/restore/union ×7, exact_variant_moves_ loop copy, mark_moved_in_expr_recursive VarRef arm, string-built move paths, body_ever_moved_, currently_uninit_vars_, B8 flags, temp-wrap drop loop, discard ×2, SDrop private field loops, inline needs_drop predicates |
| **S6** HIR destructuring + C-PLC sema + C-REPR place half | eval order #521, double eval, `*Assign` crashes, E0368 admit, `h.p[i]`, DST stride | lower_place_assign, try_index_mut_assign, G167-5, eval-once and read-twice blocks, DEREF_WRITE/DEREF_COMPOUND arms, assign_place, lower_index_place, resolve_place_type, place_write_supported, mono_clone compound BinOp rewrite, EAddrOfTemp legacy handler + spill (→ bug()), 6 dead write stmts in ~9 consumers, 10 store ladders → store_to_place, var_elem_types_ |
| **S7** C-EXP + C-LIT + C-INF (+ C-CLO signature half) | hint-leak wrong T; many over-refusals; needs S4's expect_type as the only applier | 7 hint members and their manual restores, 12 sema default sites + mlir IntLit fallback, ~100 range checks, 11 raw unify loops, lower_var_ref hinted branch, closure_hint_for, 2 private return-hint readers |
| **S8** C-RES | wrong dispatch, UFCS admits, E0133 holes, operator crashes, divergence-by-name | try_method_on_tuple/array/slice/dstref lookups, primitive arm, `$ref_` ladder, main + base-name loops, Deref loop direct probe, 3 wants-mut copies, 4 op tables, unsafe checks ×8 → 1, mono subst MethodCall/BinOp/Unary/Call re-dispatch, mlir name composer + suffix scan, BIR 4 resolvers, stmt_always_returns/body_always_diverges_simple lambdas |
| **S9** C-OBL + impl identities (PAIR-gated; mangler changes ride an ABI bump) | sema/mono split, #510, #516, #537; largest blast radius | check_type_bounds satisfaction half, sema_has_impl_recursive, ~20 has_impl‖has_impl sites, check_supertrait_impls loop, TraitEngine shape predicates, mono_concrete_satisfies_bound, method_bound_ok/instantiate_enum_templates bound loops, Mono::is_auto_satisfied, 9 blanket loops, positional make_subst, mono assoc_impls_ strings, 2 mono `$G` composers, mono_impl.hpp mangle/enum_instance_name, want_sig, 5 Self derivations, lazy trait re-key, name-matched vtable resolve_methods, viable_blanket_impls overlap diagnostic |
| **S10** HIR `for` / `?` / comprehensions + C-CLO rest | range truncation, comprehension crash, `?` double lowering; needs S8 (IntoIterator via trait resolution) and stdlib impls | SFor + gen_for + lower_for, lower_for_each 4 copies, comprehension lowerers, TRY_EXPR 4 branches + ETry, folded mut_captures bit, closure_caps_by_id_, closure_capture_env_ (write-only), lower_invoke_on/N7/mono ClosureCall call paths, mlir gen_closure map save lists, EntryAbi copies ×3 |

The order is set by dependencies as well as harm. S2 must come before S5 (exits carry drops). S4 must come before S6 and S7 (expect_type becomes the only applier). S8 must come before S9 (the call site carries the trait identity that mono then consumes) and before S10 (for-desugar needs trait resolution). S0 and S1 have no prerequisites and can run in parallel with anything.

Expected net deletion: the per-family inventories name about 250 functions, blocks or arms to delete against 11 new cores (inferred from the tables above; not measured in lines).
