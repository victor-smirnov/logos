# `tests/logos/core/` — verification tests for core-language items

> Each test in this map proves a specific DoD-depth claim about the core
> language. The normative language spec lives in [`docs/spec/`](../../../docs/spec/)
> (rendered at [logos-lang.dev/spec](https://logos-lang.dev/spec/)); an item is
> "closed at DoD-depth" only if it has a verification test below (or is marked
> "verified-by-suite" — pure internal refactor with nothing user-visible to assert).

## Naming convention

Tests live FLAT in the existing test discovery roots:

- **Pass tests** — accept-side DoD: `tests/logos/pass/core_<§>_<slug>.logos`
- **Fail tests** — reject-side DoD: `tests/logos/fail/core_<§>_<slug>.logos`

(`<§>` is the dotted section number with `_` separators: e.g. `1_3`
for §1.3.) The existing `file(GLOB) pass/*.expected` and `fail/*.expected`
in `tests/logos/CMakeLists.txt` picks them up automatically; no
CMakeLists changes needed.

This directory (`tests/logos/core/`) is **documentation-only** — the
test mapping table below; the actual `.logos`/`.expected` files live
in `pass/` and `fail/`. Two reasons:

1. The native suite glob is non-recursive — keeping core tests under
   `pass/`/`fail/` lets them run with everything else without CMakeLists
   churn.
2. The single README in one place is easier to keep in sync with the
   scoreboard than 21 directories.

## Item → test mapping

| § | Item | Test path(s) | Status |
|---|------|--------------|--------|
| 1.1 | Never/divergence | `tests/logos/pass/core_1_1_never_fallback.logos` ✓ | ✅ |
| 1.2 | Coercion canonical order | verified-by-suite (pure refactor) | ✅ |
| 1.3 | `Kind::InferredType` | `tests/logos/pass/core_1_3_inferred_nested.logos` ✓ | ✅ |
| 1.4 | `Kind::FnItem` distinct | `tests/logos/fail/core_1_4_fnitem_distinct_arms.logos` ✓ | ✅ |
| 1.5 | `#[repr]` minimal | `tests/logos/pass/core_1_5_repr_transparent_layout.logos` ✓ | ✅ |
| 2.1 | region_infer wire | `tests/logos/fail/core_2_1_dyn_ref_outlives_local.logos` ✓ | ✅ |
| 2.2 | UnsafeCell | `tests/logos/pass/core_2_2_unsafecell_write.logos` ✓ | ✅ |
| 2.3 | TraitObject variance | `tests/logos/fail/core_2_3_traitobj_variance_typearg.logos` ✓ | ✅ |
| 2.4 | Auto-trait propagation | `tests/logos/fail/core_2_4c_dyn_send_violation.logos` ✓ | ✅ |
| 2.5 | MutRef Copy-trivial | `tests/logos/fail/struct_with_mut_ref_not_auto_copy.logos` ✓ | ✅ |
| 2.6 | Slice mut | `tests/logos/fail/core_2_6_slice_write_through_shared.logos` ✓ | ✅ |
| 2.7 | Definite-assignment | `tests/logos/fail/core_2_7_use_before_init.logos` ✓ | ✅ |
| 2.8 | Object-safety | `tests/logos/fail/core_2_8_obj_safety_opaque_return.logos` ✓ | ✅ |
| 3.1 | HRTB instantiation | `tests/logos/pass/core_3_1_hrtb_closure_arg.logos` ✓ + 59 hrtb-* tests | ✅ |
| 3.2 | `?Sized` invariants | `tests/logos/pass/core_3_2_qsized_box_dyn.logos` ✓ + `tests/logos/fail/core_3_2_qsized_required.logos` ✓ | ✅ |
| 3.3 | GAT + object-safety | `tests/logos/fail/core_3_3_gat_dyn_rejected.logos` ✓ | ✅ |
| 4.1 | `is_refutable` foundation | verified-by-suite (predicate consumed by 3 sites) | ✅ |
| 4.2 | Match exhaustiveness | `tests/logos/pass/core_4_2_match_exhaustiveness.logos` ✓ + `tests/logos/fail/core_4_2_missing_variant.logos` ✓ | ✅ |
| 4.3 | Chained autoderef in pat | `tests/logos/pass/core_4_3_match_double_ref.logos` ✓ | ✅ |
| 5.1 | Atomics Ordering | `tests/logos/pass/core_5_1_atomic_release_acquire.logos` ✓ | ✅ |
| 5.2 | UB doc | documented in the language spec ([logos-lang.dev/spec](https://logos-lang.dev/spec/)) ✓ | ✅ |
| 4.4 | `PAT_PATH` constants-as-patterns | `tests/logos/pass/core_4_4_pat_path_const.logos` ✓ | ✅ |
| 4.5 | fn-params irrefutable patterns | `tests/logos/pass/core_4_5_fn_param_struct_pat.logos` ✓ | ✅ |
| 6.1 | `union` item | `tests/logos/pass/core_6_1_union_parse.logos` ✓ + `tests/logos/fail/core_6_1_union_{safe_read,multi_init}.logos` ✓ | ✅ |
| 6.2 | `static` vs `const` split (immutable half) | `tests/logos/pass/core_6_2_static_lifetime.logos` ✓ | ✅ |
| 6.3 | `let-else` divergence assertion | `tests/logos/pass/core_6_3_let_else_diverges.logos` ✓ + `tests/logos/fail/core_6_3_let_else_fallthrough.logos` ✓ | ✅ |
| 6.4 | let-chain in if (if-form) | `tests/logos/pass/core_6_4_let_chain.logos` ✓ | ✅ |
| 6.5 | `?` on `Try` / `FromResidual` | `tests/logos/pass/core_6_5_try_on_user_type.logos` ✓ | ✅ |
| 6.6 | `lookup_qualified_` pub-bypass tightening | verified-by-suite (defense-in-depth) | ✅ |
| 6.7 | `extern "ABI" { … }` blocks (parse + ABI gating) | `tests/logos/pass/core_6_7_extern_abi_block.logos` ✓ + `tests/logos/fail/core_6_7_extern_unknown_abi.logos` ✓ | ✅ |
| 6.8 | `#[cfg(all/any/not)]` + `cfg_attr` activation | `tests/logos/pass/core_6_8_cfg_combinators.logos` ✓ + `tests/logos/fail/core_6_8_cfg_combinator_drops.logos` ✓ | ✅ |
| 6.9 | `ConstResolver` seam through `metacall` | `tests/logos/pass/core_6_9_const_resolver_metacall.logos` ✓ | ✅ |
| 6.10 | Derive handlers (8/8 — all) | `tests/logos/pass/core_6_10_derive_{copy,partial_eq,eq,hash,ord,partial_ord,default,debug}.logos` ✓ | ✅ |
| 6.11 | `unreachable!/todo!/unimplemented!` macros | `tests/logos/pass/core_6_11_never_macros.logos` ✓ | ✅ |
| 6.12 | `Range` family generics | `tests/logos/pass/core_6_12_range_generic.logos` ✓ | ✅ |
| 6.13 | `DerefMut` autoderef | `tests/logos/pass/core_6_13_derefmut_autoderef.logos` ✓ | ✅ |
| 6.14 | Atomics per-variant Ordering MLIR | `tests/logos/pass/core_6_14_atomics_per_variant_ordering.logos` ✓ | ✅ |

## Goal contract

When `/goal` closes an item, the agent must:

1. **Read the DoD-depth verbatim** from `logos-core.md` §<N>.<M> body.
2. **Implement** until the DoD-depth claim is met (no shortcuts).
3. **Write a test** at the path above (or one of them).
4. **Run the full suite** (`bash ../tests/logos/ctest-summary.sh` from
   `build/`); must be 5288+ pass.
5. **Update the scoreboard** at `logos-core.md §8a`: change the status
   cell from 🟡/❌ to ✅, replace "to add" with "✓".
6. **Update this README's mapping table** with the new status.
7. **Commit** with message citing the item §, the DoD-depth excerpt,
   and the verification test path.

The score line in `logos-core.md §8a` is the single number `/goal`'s
convergence checker reads. Catalog grew 2026-05-30: §§1-5 closed
21/21 ✅ across Waves 1-3; §6 + §4.4/4.5 (Wave 4-5 catalog) brought
the target to 37. When the score line shows `37 / 37 ✅` and suite
is green, the extended M3 catalog is closed.
