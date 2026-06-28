# Spec conformance — compiler bugs surfaced by test generation (resolution log)

Writing executable conformance tests is an oracle: a rule that cannot be
confirmed by an honest runnable assertion is either untestable at language level
or the compiler does not uphold it. The rollout flagged 8 candidates; each was
investigated with a minimal repro and root-caused in `src/compiler`. Status:

## Fixed (verified green + regression-clean)

| Rule id | Bug | Fix |
|---|---|---|
| `stmt.let-destruct.move-on-bind` | **Double-free.** `let (a,b) = tup;` dropped the elements twice — `lower_let_destruct` spilled the source into a temp without marking the named source moved. | `sema_stmt.cpp`: mark the RHS source moved before the spill (mirrors the array-destructure path). Valgrind-clean. Test: `tests/spec/pass/stmt_let_destruct_move.logos`. |
| `coerce.unsize.return-concrete-to-trait-object` | Returning `Box<Concrete>` from `-> Box<dyn>` segfaulted: the hand-rolled return path didn't unwrap `Box`, mis-keyed the vtable, and left the source box un-consumed (double-free). | `sema_stmt.cpp` `lower_return`: desugar the implicit return to the proven `as`-unsize cast (consume source + build fat pair + Box drop-glue). Test: `tests/spec/pass/coerce_box_dyn.logos`. |
| `coerce.deref.box-struct-borrow` | `&Box<dyn Trait>` segfaulted — the `&`-handler had no branch for an owning `TraitObject`, so `&b` produced `&&dyn` (a thin ptr where the callee wanted the fat pair). | `sema_expr.cpp`: add the owning-TraitObject branch (read the fat-pair value, re-type non-owning), mirroring the existing owning-DstRef branch. Test: same file as above. |
| *(bonus)* float↔pointer cast | `f64 as *const f64` (invalid per Rust E0606) was accepted by sema and silently miscompiled (mlir_gen nullptr fallthrough → exit 0, rest of fn elided). | `sema_expr.cpp`: reject float↔pointer casts in the cast-validation block. |
| `expr.method.receiver-multiref-autoderef` | A method call / deref through an immutable `&&T` *local* binding (`let r2 = &r1; r2.m()`) read one indirection too deep (wrong field / segfault): an immutable `&Struct` local aliases its pointee address with no own slot, so `let r2 = &r1` stored a one-short `&P` into the `&&P` slot. (The call-arg `f(&r)` + `&&pat` form was already correct — it uses an internally-consistent one-short convention.) | `mlir_gen_stmt.cpp`: a store-side case for `let r2 = &<aliased-immutable-ref-local>` of `&&`-type materialises the missing mid slot (`r2 → mid → P`), restoring the second level. Narrow — doesn't touch the shared EAddrOf, call-arg, or read paths. Test: `tests/spec/pass/expr_multiref_deref.logos`. |

## Not bugs (investigated, no compiler change)

| Rule id | Verdict |
|---|---|
| `type.typeof.expr` | Sound. `typeof` lowering allocates no slot and never poisons sibling typing at any scale; the original symptom was a test-construction artifact (a shadowed binding). |
| `generic.enum-lit.hint-ref-ptr-preference` | Already fixed on HEAD (niche/tagged repr commits, Jun). Could not reproduce; the rule is sound. |
| `coerce.cast.float-to-float` | The float↔float width rule is sound at any scale. The real defect was the separate float↔pointer gap (fixed above). |
| `trait.def.vtable-layout-supertrait-closure` | The supertrait-closure vtable mechanism is **correct** (verified with a non-colliding trait name; test `tests/spec/pass/trait_supertrait_dyn.logos`). The reported failure was a trait-name collision: the repro named its trait `Sub`, which shadows the prelude arithmetic operator trait `Sub`, so `&dyn Sub` bound to the wrong trait. That is the pre-existing `dyn`-local-trait-shadowing gap (adversarial sweep ADV1-H), a separate broad canonicalization fix — not this rule. |

## Also fixed

| Issue | Bug | Fix |
|---|---|---|
| `dyn`-local-trait-shadowing (ADV1-H) | A user `trait Sub` whose bare name collides with a prelude/imported trait (the `ops::Sub` operator) registers only under its package-qualified key (B-mv-02), but the `dyn` path resolved the trait by its BARE name, so dispatch bound to the prelude trait → "trait 'Sub' has no method". Also affected a shadowed *supertrait* (`Sub: Add`, both shadowing operators). | The in-tree comment predicted a 12-site sweep; the actual fix is **3 scope-aware lookups** — the impl/vtable ecosystem is already bare-keyed + target-disambiguated, so only the name-only lookups mis-bound. Swap bare `traits_.find` → `find_trait_iter_scoped` (current-package first) in `lower_trait_def` (`sema_decl.cpp`), `try_method_on_dyn` (`sema_expr.cpp`), and the supertrait-closure walk in `trait_vtable_layout` (`sema_collect.cpp`). No-op for non-colliding names. Regression: `tests/logos/pass/dyn_trait_shadowing.logos`. |

---
*Generated during the spec-extract conformance rollout (`tools/spec-extract`,
`spec-test.workflow.js`). Fixes verified individually + full `ctest` regression.*
