# Corpus revision — pilot slice: coercion + array-slice-vec (S3)

The measure Victor asked for instead of "на вскидку": 164 originals,
37 traceable imports, 127 without a per-file verdict. Of the unimported,
110 carry no known-divergence marker — those are the prime suspects for
uncovered cells and get imported FIRST in the full revision (post-P7,
see project_imported_corpus_revision). Every row must end as one of:
imported | divergence §N | gap-ticket. A skip without a verdict is the
survivor-bias mechanism itself.

| original | status | verdict/markers |
|---|---|---|
| coercion/any-trait-object-debug-12744.rs | unimported | MUST-TRY |
| coercion/basic-ptr-coercions.rs | imported |  |
| coercion/cast-higher-ranked-unsafe-fn-ptr.rs | unimported | MUST-TRY |
| coercion/closure-in-array.rs | unimported | MUST-TRY |
| coercion/codegen-smart-pointer-with-alias.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/coerce-bare-fn-returning-zst-to-closure.rs | imported |  |
| coercion/coerce-block-tail-26978.rs | unimported | MUST-TRY |
| coercion/coerce-block-tail-57749.rs | unimported | MUST-TRY |
| coercion/coerce-block-tail-83783.rs | unimported | MUST-TRY |
| coercion/coerce-block-tail-83850.rs | unimported | MUST-TRY |
| coercion/coerce-block-tail.rs | unimported | MUST-TRY |
| coercion/coerce-box-new-to-unboxed.rs | unimported | MUST-TRY |
| coercion/coerce-expect-unsized-ascribed.rs | unimported | likely-divergence: unstable-feature |
| coercion/coerce-expect-unsized.rs | imported |  |
| coercion/coerce-issue-49593-box-never.rs | unimported | likely-divergence: unstable-feature |
| coercion/coerce-loop-issue-122561.rs | unimported | MUST-TRY |
| coercion/coerce-many-with-ty-var.rs | imported |  |
| coercion/coerce-mut-ref-to-raw-ptr-borrow-expires.rs | unimported | MUST-TRY |
| coercion/coerce-mut-trait-object-8248.rs | imported |  |
| coercion/coerce-mut.rs | unimported | MUST-TRY |
| coercion/coerce-overloaded-autoderef-fail.rs | unimported | MUST-TRY |
| coercion/coerce-overloaded-autoderef.rs | unimported | MUST-TRY |
| coercion/coerce-reborrow-imm-ptr-arg.rs | imported |  |
| coercion/coerce-reborrow-imm-ptr-rcvr.rs | imported |  |
| coercion/coerce-reborrow-imm-vec-arg.rs | imported |  |
| coercion/coerce-reborrow-imm-vec-rcvr.rs | imported |  |
| coercion/coerce-reborrow-multi-arg-fail.rs | unimported | MUST-TRY |
| coercion/coerce-reborrow-multi-arg.rs | unimported | MUST-TRY |
| coercion/coerce-reborrow-mut-ptr-arg.rs | imported |  |
| coercion/coerce-reborrow-mut-ptr-rcvr.rs | imported |  |
| coercion/coerce-reborrow-mut-vec-arg.rs | imported |  |
| coercion/coerce-reborrow-mut-vec-rcvr.rs | imported |  |
| coercion/coerce-to-bang-cast.rs | unimported | likely-divergence: unstable-feature |
| coercion/coerce-trait-object-removes-send-bound.rs | unimported | likely-divergence: unstable-feature |
| coercion/coerce-unify-return.rs | imported |  |
| coercion/coerce-unify.rs | unimported | MUST-TRY |
| coercion/coerce-unsize-subtype.rs | unimported | MUST-TRY |
| coercion/coercion-missing-tail-expected-type.rs | unimported | MUST-TRY |
| coercion/coercion-slice.rs | unimported | MUST-TRY |
| coercion/constrain-expectation-in-arg.rs | unimported | MUST-TRY |
| coercion/fake-sized-ptr-cast.rs | unimported | MUST-TRY |
| coercion/hr_alias_normalization_leaking_vars.rs | unimported | MUST-TRY |
| coercion/index-coercion-over-indexmut-72002.rs | unimported | MUST-TRY |
| coercion/intrinsic-in-unifying-coercion-149143.rs | unimported | likely-divergence: transmute |
| coercion/invalid-blanket-coerce-unsized-impl.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/issue-101066.rs | unimported | MUST-TRY |
| coercion/issue-14589.rs | unimported | MUST-TRY |
| coercion/issue-26905-rpass.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/issue-26905.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/issue-32122-1.rs | unimported | MUST-TRY |
| coercion/issue-32122-2.rs | unimported | MUST-TRY |
| coercion/issue-36007.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/issue-37655.rs | unimported | MUST-TRY |
| coercion/issue-3794.rs | imported |  |
| coercion/issue-39823.rs | unimported | MUST-TRY |
| coercion/issue-53475.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/issue-73886.rs | unimported | MUST-TRY |
| coercion/issue-88097.rs | unimported | MUST-TRY |
| coercion/leak_check_fndef_lub.rs | unimported | MUST-TRY |
| coercion/leak_check_fndef_lub_deadcode_breakage.rs | unimported | likely-divergence: transmute |
| coercion/lub_coercion_handles_safety.rs | unimported | MUST-TRY |
| coercion/method-return-trait-object-14399.rs | unimported | MUST-TRY |
| coercion/mut-mut-wont-coerce.rs | unimported | MUST-TRY |
| coercion/mut-trait-object-coercion-8398.rs | unimported | MUST-TRY |
| coercion/no-implicit-box-to-ref-coercion.rs | unimported | MUST-TRY |
| coercion/no_local_for_coerced_const-issue-143671.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/non-primitive-cast-135412.rs | unimported | MUST-TRY |
| coercion/pin-dyn-dispatch-sound.rs | unimported | MUST-TRY |
| coercion/ptr-mutability-errors.rs | unimported | MUST-TRY |
| coercion/retslot-cast.rs | unimported | MUST-TRY |
| coercion/struct-coerce-vec-to-slice.rs | unimported | MUST-TRY |
| coercion/struct-literal-field-type-coercion-to-expected-type.rs | unimported | MUST-TRY |
| coercion/structural_identity_dependent_reborrows.rs | unimported | MUST-TRY |
| coercion/sub-principals.rs | unimported | likely-divergence: unstable-feature |
| coercion/trait-object-arrays-11205.rs | unimported | MUST-TRY |
| coercion/trait-object-coercion-distribution-9951.rs | imported |  |
| coercion/type-errors.rs | unimported | MUST-TRY |
| coercion/unboxing-needing-parenthases-issue-132924.rs | unimported | MUST-TRY |
| coercion/unsafe-coercion.rs | imported |  |
| coercion/variance-coerce-unsized-cycle.rs | unimported | likely-divergence: unstable-feature, CoerceUnsized-trait |
| coercion/vec-macro-coercions.rs | unimported | MUST-TRY |
| array-slice-vec/array-not-vector.rs | unimported | MUST-TRY |
| array-slice-vec/array_const_index-0.rs | unimported | MUST-TRY |
| array-slice-vec/array_const_index-1.rs | unimported | MUST-TRY |
| array-slice-vec/array_const_index-2.rs | unimported | MUST-TRY |
| array-slice-vec/bounds-check-no-overflow.rs | unimported | MUST-TRY |
| array-slice-vec/box-of-array-of-drop-1.rs | unimported | MUST-TRY |
| array-slice-vec/box-of-array-of-drop-2.rs | unimported | MUST-TRY |
| array-slice-vec/byte-literals.rs | unimported | MUST-TRY |
| array-slice-vec/cast-in-array-size.rs | unimported | MUST-TRY |
| array-slice-vec/check-static-slice.rs | unimported | MUST-TRY |
| array-slice-vec/closure-in-array-len.rs | unimported | MUST-TRY |
| array-slice-vec/copy-out-of-array-1.rs | imported |  |
| array-slice-vec/destructure-array-1.rs | imported |  |
| array-slice-vec/driftsort-off-by-one-issue-136103.rs | unimported | MUST-TRY |
| array-slice-vec/dst-raw-slice.rs | unimported | MUST-TRY |
| array-slice-vec/empty-mutable-vec.rs | imported |  |
| array-slice-vec/estr-slice.rs | imported |  |
| array-slice-vec/evec-slice.rs | imported |  |
| array-slice-vec/fixed-length-vector-pattern-matching-7784.rs | imported |  |
| array-slice-vec/fixed-size-arrays-zero-size-types-8898.rs | unimported | MUST-TRY |
| array-slice-vec/fixed_length_copy.rs | imported |  |
| array-slice-vec/huge-largest-array.rs | unimported | MUST-TRY |
| array-slice-vec/infer_array_len.rs | unimported | MUST-TRY |
| array-slice-vec/issue-15730.rs | imported |  |
| array-slice-vec/issue-18425.rs | unimported | MUST-TRY |
| array-slice-vec/issue-69103-extra-binding-subslice.rs | unimported | MUST-TRY |
| array-slice-vec/ivec-pass-by-value.rs | imported |  |
| array-slice-vec/large-zst-array-compilation-time-68010.rs | unimported | MUST-TRY |
| array-slice-vec/match_arr_unknown_len.rs | unimported | MUST-TRY |
| array-slice-vec/mut-vstore-expr.rs | imported |  |
| array-slice-vec/mutability-inherits-through-fixed-length-vec.rs | imported |  |
| array-slice-vec/mutable-alias-vec.rs | imported |  |
| array-slice-vec/nested-vec-1.rs | unimported | MUST-TRY |
| array-slice-vec/nested-vec-2.rs | imported |  |
| array-slice-vec/nested-vec-3.rs | unimported | MUST-TRY |
| array-slice-vec/new-style-fixed-length-vec.rs | imported |  |
| array-slice-vec/rcvr-borrowed-to-slice.rs | imported |  |
| array-slice-vec/repeat_empty_ok.rs | unimported | MUST-TRY |
| array-slice-vec/repeated-vector-syntax.rs | imported |  |
| array-slice-vec/return-in-array-len.rs | unimported | MUST-TRY |
| array-slice-vec/show-boxed-slice.rs | unimported | MUST-TRY |
| array-slice-vec/slice-2.rs | unimported | MUST-TRY |
| array-slice-vec/slice-mut-2.rs | unimported | MUST-TRY |
| array-slice-vec/slice-mut.rs | unimported | MUST-TRY |
| array-slice-vec/slice-of-multi-ref.rs | unimported | MUST-TRY |
| array-slice-vec/slice-of-zero-size-elements.rs | unimported | MUST-TRY |
| array-slice-vec/slice-panic-1.rs | unimported | MUST-TRY |
| array-slice-vec/slice-panic-2.rs | unimported | MUST-TRY |
| array-slice-vec/slice-pat-type-mismatches.rs | unimported | MUST-TRY |
| array-slice-vec/slice-subslice-ref-lifetime.rs | unimported | MUST-TRY |
| array-slice-vec/slice-to-vec-comparison.rs | unimported | MUST-TRY |
| array-slice-vec/slice.rs | unimported | MUST-TRY |
| array-slice-vec/slice_binary_search.rs | unimported | MUST-TRY |
| array-slice-vec/slice_is_sorted_by_borrow.rs | unimported | MUST-TRY |
| array-slice-vec/subslice-only-once-semantic-restriction.rs | unimported | MUST-TRY |
| array-slice-vec/subslice-patterns-const-eval-match.rs | unimported | likely-divergence: const-eval→metacall §A1 |
| array-slice-vec/subslice-patterns-const-eval.rs | unimported | likely-divergence: const-eval→metacall §A1 |
| array-slice-vec/subslice-range-return-ref.rs | unimported | MUST-TRY |
| array-slice-vec/suggest-array-length.rs | unimported | MUST-TRY |
| array-slice-vec/variance-vec-covariant.rs | imported |  |
| array-slice-vec/vec-dst.rs | unimported | MUST-TRY |
| array-slice-vec/vec-fixed-length.rs | unimported | MUST-TRY |
| array-slice-vec/vec-index-bounds-check-overflow.rs | unimported | MUST-TRY |
| array-slice-vec/vec-late-init.rs | imported |  |
| array-slice-vec/vec-macro-no-std.rs | unimported | MUST-TRY |
| array-slice-vec/vec-macro-rvalue-scope.rs | unimported | MUST-TRY |
| array-slice-vec/vec-macro-with-brackets.rs | unimported | MUST-TRY |
| array-slice-vec/vec-macro-with-comma-only.rs | unimported | MUST-TRY |
| array-slice-vec/vec-macro-with-trailing-comma.rs | unimported | MUST-TRY |
| array-slice-vec/vec-matching-autoslice.rs | unimported | MUST-TRY |
| array-slice-vec/vec-matching-fixed.rs | imported |  |
| array-slice-vec/vec-matching-fold.rs | unimported | MUST-TRY |
| array-slice-vec/vec-matching-legal-tail-element-borrow.rs | unimported | MUST-TRY |
| array-slice-vec/vec-matching.rs | unimported | MUST-TRY |
| array-slice-vec/vec-mut-iter-borrow.rs | unimported | MUST-TRY |
| array-slice-vec/vec-overrun.rs | unimported | MUST-TRY |
| array-slice-vec/vec-repeat-with-cast.rs | unimported | MUST-TRY |
| array-slice-vec/vec-res-add.rs | unimported | MUST-TRY |
| array-slice-vec/vec-tail-matching.rs | imported |  |
| array-slice-vec/vector-cast-weirdness.rs | unimported | MUST-TRY |
| array-slice-vec/vector-no-ann-2.rs | unimported | MUST-TRY |
| array-slice-vec/vector-no-ann.rs | unimported | MUST-TRY |
| array-slice-vec/vector-slice-matching-8498.rs | unimported | MUST-TRY |
