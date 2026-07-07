# Logos compiler — pre-PR checklist

**Definition-of-Done** for new features and bug fixes in the Logos compiler. Run through this before opening a PR. Reviewers verify it.

The checklist exists because the [bag-hunt](baghunt/README.md) found ~122 bugs that all fall into ~12 systemic patterns. Each item below either prevents one of those patterns or applies a specific invariant from [memory/invariants.md](../../.claude/projects/-home-victor-devel-logos/memory/invariants.md).

## Always

- [ ] **Existing tests still pass.** Run `ninja -C build && ctest --test-dir build -j12`. 1132/1132 baseline as of 2026-05-07.
- [ ] **No new compiler warnings.** Treat warnings as errors during local build.
- [ ] **No `LOGOS_ASSERT` reachable from user input.** `LOGOS_ASSERT` is for internal invariants only — see [antipat_assertion_as_diagnostic](../../.claude/projects/-home-victor-devel-logos/memory/antipat_assertion_as_diagnostic.md).
- [ ] **No new inline `LogosTypeBuilder t; t.kind = K::Struct/Enum/...`.** Route through `make_*_type` helpers in [sema_impl.hpp](../src/compiler/sema_impl.hpp). See [antipat_inline_typebuilder](../../.claude/projects/-home-victor-devel-logos/memory/antipat_inline_typebuilder.md).
- [ ] **Diagnostics on malformed input are clear.** No cryptic `mlir_gen: ...` errors that user can't trace to source. See [antipat_validation_at_codegen](../../.claude/projects/-home-victor-devel-logos/memory/antipat_validation_at_codegen.md).

## When adding a new feature

### Test coverage (adversarial, not just happy path)

- [ ] **Empty-input case** — does the feature accept `()` / `{}` / `[]` / 0 elements when applicable?
- [ ] **Single-element case** — different from multi (e.g. `(i32,)` tuple is not the same as `(i32)` paren).
- [ ] **Multi-element case** — typical use.
- [ ] **Nested case** — does the feature compose with itself? (`@{ "k": @{} }`, `Vec<Vec<i32>>`, `match m { _ => match n { ... } }`)
- [ ] **Neighbor-ambiguity case** — does the syntax conflict with adjacent forms? (`&& vs & &mut`, `|| vs zero-arg-closure`, `'A' vs lifetime 'A`)
- [ ] **Cross-feature interaction** — if the feature touches generics × patterns × closures × etc., test the relevant pairs.
- [ ] **Refutability / error cases** — what fails? Does it fail with a clear diagnostic?

### Validation

- [ ] **List-of-named-items?** Run `validate_unique_names` (when M0.1 lands) at the registration site. See [antipat_list_no_dup_check](../../.claude/projects/-home-victor-devel-logos/memory/antipat_list_no_dup_check.md).
- [ ] **Generic instantiation?** Run `check_type_arg_arity` at the resolution site.
- [ ] **Recursion into definitions?** Has the recursion got a visited-set / depth-limit guard?
- [ ] **Numeric literal parse?** Does it bounds-check?
- [ ] **New attribute?** Registered in `attr_registry` (when M0.3 lands).
- [ ] **Cast / coerce?** Pair-compatibility validated.

### Invariants

- [ ] Identified which invariants from [memory/invariants.md](../../.claude/projects/-home-victor-devel-logos/memory/invariants.md) this change touches.
- [ ] Verified each touched invariant is preserved.
- [ ] If a NEW invariant emerges, added it to invariants.md.

### Documentation

- [ ] **Reference doc updated.** [docs/spec/](spec/) has the right page for the feature; update it.
- [ ] **Internals doc updated** if the change is implementation-relevant.
- [ ] **Memory file added** if this is a multi-month arc or load-bearing decision (`feat_*.md` for done work, `project_*.md` for in-progress).

## When fixing a bug

- [ ] **Bag-hunt entry referenced.** If this fixes a `B-XX-NN` bug, reference it in the commit message.
- [ ] **Regression test added.** The minimal repro from `/tmp/baghunt/` (or wherever) becomes a permanent test in `tests/logos/`.
- [ ] **Cluster check.** Does this bug fit a cluster from [cluster_index.md](../../.claude/projects/-home-victor-devel-logos/memory/cluster_index.md)? If yes, **also fix the other instances** in the same PR or note them as follow-ups.
- [ ] **Anti-pattern memo updated.** If this bug was an instance of an anti-pattern, the memo's "historical violations" list should be updated.

## When changing the parser / grammar

- [ ] **Grammar production added/modified in [logos.peg](../tools/peg_gen/grammars/logos.peg).**
- [ ] **PEG generator regenerates cleanly.**
- [ ] **Reference doc updated** to match the new grammar.
- [ ] **Lexer changes** (if any) avoid greedy-collision (see B-ty-07/08, B-lx-07).
- [ ] **Parser errors are recoverable.** No `LOGOS_ASSERT` on missing tokens. Use error productions.

## When changing the type system / sema

- [ ] **TypeUID hash inputs** ([compute_type_uid in sema.cpp](../src/compiler/sema.cpp)) — if you add a new type field, ensure it's hashed if it's identity-relevant.
- [ ] **TypeRef construction** routes through `make_*_type` helpers.
- [ ] **`pkg_name` propagation** through clone / subst / mlir-gen — see [I-1, I-2, I-3, I-4](../../.claude/projects/-home-victor-devel-logos/memory/invariants.md).
- [ ] **`type_str` output** — if it's used in user-facing diagnostics, qualify with pkg when relevant.

## When changing mlir-gen

- [ ] **`mlir_struct_key(t)` for struct lookups** — not bare `concrete_struct_name(t)` — when registering / looking up struct types.
- [ ] **Bare `concrete_struct_name(t)` for method symbol mangling** — strip pkg via `strip_struct_pkg` before constructing `Foo__method`.
- [ ] **AnyVal special-case** — `type_str(t) == "AnyVal"` checks must wrap with `strip_struct_pkg` if the input is a qualified key.
- [ ] **No mlir-gen errors on user-input that sema accepted.** If you find one, file a sema bug, not a mlir-gen workaround.

## When changing mono

- [ ] **`clone_struct_def` / `clone_enum_def`** propagate `tmpl.pkg → nd.pkg`.
- [ ] **Spec-vs-generic pkg unification** — if this is a generic with a spec in a different pkg, the inst inherits the GENERIC's pkg.
- [ ] **Worklist dedup** — keyed by mangled instantiation name.

## When changing Writ

- [ ] **Compile/runtime parity** — same byte layout across WritStatic, runtime Writ, HBS wire, on-disk modules.
- [ ] **Zone base ptr** re-read on each relative-offset deref.
- [ ] **MemHolder rc** correctly counted; custom destroyer fires on rc=0.

## When changing borrow check

- [ ] **Move semantics** — moved-from binding can't be used until rebound.
- [ ] **`&mut` exclusivity** — at most one `&mut` to a place; no `&` aliases simultaneously.
- [ ] **Lifetime bounds** — `&'a T` outliving the data is rejected.

## When changing the runtime (fiber / reactor / sync)

- [ ] **Send/Sync correctness** — types crossing fiber/thread boundaries are Send.
- [ ] **No blocking syscall** in the fiber path (use reactor).
- [ ] **Drop-on-cancel** for fibers parked on channels (verify behavior).

## Final

- [ ] **Commit message references the bag-hunt entry / cluster** if applicable.
- [ ] **PR description summarizes**:
  - What changed
  - Which invariants are touched / preserved / added
  - Which bag-hunt entries are closed (B-XX-NN identifiers)
  - Test coverage (adversarial cases)

## Reviewer extras

When reviewing someone else's PR:

- [ ] **Check the catalog**: does the change look like a known pattern in [cluster_index.md](../../.claude/projects/-home-victor-devel-logos/memory/cluster_index.md)?
- [ ] **Run adversarial cases**: pick 2-3 corner cases the author might've missed.
- [ ] **Verify the diff is small** for "applied helper" PRs and large for "new feature" PRs (suspicious ratios → audit).

## Out of scope (intentional non-goals)

- This checklist does NOT enforce code style (formatting, naming) — that's clang-format / linters.
- This checklist does NOT cover performance — separate review path.
- This checklist focuses on **correctness + diagnostics + invariant preservation** because that's what the bag-hunt revealed as the gap.

## When this list grows

The checklist starts at ~50 items as of Phase 4 of the bag-hunt. Future items will be added as new patterns emerge. **Don't shy from growing it** — every item here is anchored to a real historical bug. The cost of one PR running through 60 checks is much less than the cost of one P0 silent miscompile.

## Cross-references

- [docs/baghunt/README.md](baghunt/README.md) — feature-group inventory
- [docs/baghunt/categorization.md](baghunt/categorization.md) — cluster analysis
- [docs/baghunt/strategy.md](baghunt/strategy.md) — sequencing
- [docs/baghunt/meta-strategy.md](baghunt/meta-strategy.md) — infra-first plan
- [memory/invariants.md](../../.claude/projects/-home-victor-devel-logos/memory/invariants.md) — formal invariants
- [memory/cluster_index.md](../../.claude/projects/-home-victor-devel-logos/memory/cluster_index.md) — operational pattern→bug index
- [memory/antipat_*.md](../../.claude/projects/-home-victor-devel-logos/memory/antipat_inline_typebuilder.md) — anti-pattern memos
