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

## NEVER register a ctest test that does BULK work in one slot (Victor 2026-08-25)

**A registered ctest test compiles or runs ONE thing. If a verdict needs N
programs, register N tests and fold their verdicts — never loop inside one.**

ctest is the only scheduler (#85): a registered test may not fan out its own
workers. The corollary was left unwritten and it is the half that bites — a test
that CANNOT fan out and still has N programs to get through simply runs them
serially, occupying one core while the rest of the box idles, and there is no
flag that fixes it.

⚠ MEASURED, and I built the offender myself the same day I recorded the rule it
breaks. `logos_00_bc_admits_ledger` compiles 462 programs in one slot: ~7 min,
1 core busy, 31 idle. It landed because the import brief said "reuse
`mlir_gen_bug_ledger_gate.sh`'s idiom, do not write a second gate" — right
semantics, and an idiom built for TENS of rows applied to hundreds without
recomputing. Three rounds then paid the bill; in one of them, eight runs of the
lint tier were ~56 minutes of which nearly all was this.

THE SHAPE THAT IS ALWAYS AVAILABLE:
  * register one test PER PROGRAM — ctest parallelises them, and a failure then
    NAMES the program instead of saying the fold disagreed;
  * leave the gate as a cheap fold that checks the COUNT and the ROSTER against
    what is on disk, compiling nothing;
  * where the programs are already compiled by their own tests, emit facts from
    that compile and fold the facts (`facts_emit.sh` / `facts_fold.sh`, task #85).

⚠ THE SMELL, in one line: if a gate script contains a loop over a list of source
files, it is the wrong shape — no matter how correct its verdict is.

## When budgeting a round's EVIDENCE (measured 2026-08-25)

The proof burden is a cost, it scales with the suite, and it must be recomputed
whenever the suite changes size. A class-C round spent **6.2 hours**, and 3.9 of
them were accounted for by four lines of a brief:

    ctest -L imported (4095 tests)      x10 = ~80 min
    the gates tier (tier_commit)        x10 = ~70 min   <- carries a 463-row
                                                           SERIAL ledger fold
    cmake --build (borrow_check.cpp)    x12 = ~55 min
    -L pass -LE imported, L1, L2         x5 = ~29 min

The brief had said "red list per cell: `-L pass -LE imported`, `-L imported`,
`-L fail`". That was written when the suite was 7139 tests and was carried over
verbatim after the same author had grown it to 8014 that morning. Not one of the
ten intermediate `-L imported` runs found a red.

RULES:

* **Cheap list between cells, full sweep ONCE before handing over.**
  `ctest -j$(nproc) -L pass -LE imported` is the between-cells list — and it
  is the ONLY one. It catches the over-refusal direction, which is where
  rounds actually die. **Everything else runs once, at handover:**
  `-L imported`, `-L fail`, L1, L2, **and the gates tier / any `logos_00_` or
  `logos_09_` census gate**.
  ⚠ SPELL THE GATES OUT, because the first cut of this rule did not and the
  next round ran them EIGHT times (~56 min) while cutting `-L imported` from
  ten runs to one. The prohibition had named `-L imported`, `-L fail`, L1 and
  L2 explicitly and left the gates tier only implied — so the agent obeyed
  what was written. A rule that lists its exceptions by name will be read as
  exhaustive.
* **Never put a growing fold in `tier_commit`** unless it answers "did something
  break". `bc_admits.ledger` answers "what got FIXED", so an intermediate run of
  it is worthless — and it is 7 minutes single-threaded per run (see 8g).
* **Quote the unit costs in the brief** so the agent can compute its own bill
  instead of running everything reflexively. On this box today: incremental
  `borrow_check.cpp` build ~4.5 min · `-L imported` ~8 min · `-L pass -LE
  imported` ~7 min · gates tier ~7 min · L2 ~11 min.
* **Builds: ≤3 per cell, and that is a CEILING, not an encouragement.**
  One for the fix, one for the control revert, one for the restore. A cell that
  wants a fourth is not being implemented — it is being SEARCHED, and searching
  by rebuild costs ~4.5 min a step.
  ⚠ MEASURED, and it is why the number is here: the round after this rule was
  first written ran **37 builds for two cells** (~2.8 h) while obeying the sweep
  limits perfectly — `-L imported` zero times, gates five, `-L pass` six. The
  rule had said "one rebuild per cell is worth paying for; economise on sweeps,
  not on builds", which named a direction and set no bound, and was read as a
  licence.
* **If you are hunting for where a value is lost, INSTRUMENT — do not iterate.**
  One debug build that prints the value at the producer and at the consumer
  answers in a single step what a rebuild-per-guess loop does not answer at all.
  Measured twice: three inferences about `&<const item>`'s LIR shape were all
  wrong (one of them read a RETIRED schema code as live) and one instrumented
  build settled it; the D1 diagnosis refuted BOTH named suspects the same way.
* ⚠ **A rule that names a direction without a number is a wish.** Both breaches
  of this section so far came from that: the sweep list enumerated its
  exceptions by name and the gates tier — unnamed — ran eight times; the build
  guidance praised builds without a ceiling and got 37. State the bound.
* ⚠ **Recompute when the suite changes.** Two edits in one day — landing 875
  imported tests, and a per-cell evidence rule — multiplied each other. Either
  alone was reasonable.

## When writing a VERIFY phase (Victor 2026-08-25)

**Verify buys INDEPENDENCE, not coverage. Coverage is the landing phase's job.**

* Its task is to check a SPECIFIC CLAIM, or a short list of them, **in a fresh
  context** — someone who did not write the fix, trying to break it.
* **It does NOT run the corpus.** No `-L pass`, no `-L imported`, no `-L fail`,
  no gates tier, no L1/L2. Those answer "did anything break", which the landing
  phase already answered and which a second run cannot answer better.
* **L4 is run ONCE, by Land or by Verify — preferably Land.** Never both.
* A narrow `ctest -R <the round's own fixtures>` is fine; a sweep is not.
* **Short in COMMANDS, exhaustive in PROGRAMS.** This is the whole distinction:
  thoroughness comes from enumerating the property space with hand-written
  probes, not from re-running tests that are already green. Sweep every shape
  the property can take — nesting, both directions, each spelling — and do it
  with `logosc` and `run_test.sh`, which cost seconds, instead of with ctest,
  which costs minutes. The previous verify ran 80 commands including ten
  `test-levels` invocations and two `-L imported`, and every finding it actually
  produced came from a probe.

WHAT VERIFY IS FOR, and it is worth the whole phase: the last three rounds were
each saved by it, and in every case by a PROBE — a fixture that could not
contradict its own claim (three times), an over-refusal the corpus did not
contain (twice), a promoted i128 holding a different value from the same
literal. None of that came from re-running tests that were already green.

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
- [ ] **A message about the COMPILER goes through the sink, never `fprintf(stderr, ...)`.** "mlir-gen has no diagnostic channel" was true, and it is the reason a `let` could vanish behind a green gate: mlir-gen printed `statement DROPPED (compiler bug)`, wrote the object file and returned 0, so `run_test.sh` pass mode ("logosc succeeded") saw nothing. The channel is `MLIRGenImpl::bug` / `bug_null` / `bug_false` / `bug_raw`; the check is ONE `if (bugs_)` at the single exit of `generate()`. `LOGOS_MLIRGEN_ABORT_ON_BUG=1` restores a stack trace — the *check* is never opt-in. The one exclusion is `tests/logos/mlir_gen_bug.ledger`, by name, re-measured in both directions by `logos_00_mlir_gen_bug_ledger`.
- [ ] **`logos_to_mlir` returning null is an ANSWER, not a malfunction** — callers use it as a predicate ("does this type have an MLIR representation"). Report at the caller that needed a type and got none.

## When composing or reading a MANGLED NAME (any phase)

- [ ] **`__` is legal inside an identifier, so `name.find("__")` is a GUESS, not a boundary.** A name is composed from parts; carry the parts to every consumer. Measured failures from re-deriving them: `enum foo_<T>` never instantiated (exit-0 compile, dropped `let`), a `fn a__f__b` method never emitted (SIGSEGV), a doubled `__g__b__g__b` symbol, `fn a__b` rejected as a duplicate of `impl a { fn b }`, `T_::mk` unresolvable, and a type-var shadow returning the wrong trait's value.
- [ ] **Use `src/compiler/mangled_name.hpp`.** `sig_of(name, owner, method)` recomposes-and-compares (strongest); `split_known_owner` anchors on a carried owner; `split_by_registry` matches a declared-name set longest-first. Every one returns `std::optional` — there is deliberately no guessing overload, and `nullopt` is a FACT to report.
- [ ] **Carried facts already available:** `dk::METHOD_BASE` (a method's unmangled source name, `lir_view::method_base()`), `SemaFuncInfo::owner_struct` / `::is_method`, `Mono::EnumInst::base`/`::pkg`, `MethodWorkItem::base_struct`/`::method_name`.
- [ ] **A separator that must survive into a link symbol belongs OUTSIDE the identifier alphabet** — `concrete_struct_name`'s `$G<n>$` form is the proof; `record_needed_enum`'s `__` is the counter-example.
- [ ] **The mangled TYPE ARGUMENT is part of the name too.** `box_$G1$k___s` is `box_<k_>::s`, and cutting at the first `__` after `$G` lands inside the ARGUMENT: measured, no vtable and no drop glue were emitted for `box_<k_>`, compile exit 0 with empty stderr, program SIGSEGV. The owner's spelling was irrelevant; the argument's decided.
- [ ] **Both `.` and `$` are composed head boundaries.** A METHOD's link name joins with `.`, a FREE fn's with `$` (`sym::mangle`). A lookup that accepts only one silently misses the other — `resolve_fn("fmt_seq")` never resolved, so the whole variadic-tuple `Debug` chain lowered to nothing in the SHIPPED stdlib archive.

## When a fact about a NODE is needed downstream

- [ ] **State it, do not spell it.** If a consumer needs to know *why* a node exists (synthesized vs user-written), that is the producer's fact — give it a schema key and carry it. Measured: mlir-gen decided "is this block a sema-synthesized transparent wrapper" by testing whether its first `let` was named `__…`, so the legal user program `{ let __x = 9; let v = 100; }` lost its scope restore and the outer `v` read the inner value, silently, at exit 0. Now `stmt_keys::TRANSPARENT`, set at every producer in `sema_stmt.cpp` and carried through `mono_clone`. `LOGOS_TRANSPARENT_AUDIT=1` reports a producer that forgot.
- [ ] **A predicate written as a LIST OF KINDS drifts.** Prefer the structural question. `logos_to_mlir(t) == null` *is* "zero-width"; a hand-listed `{Void, Never}` missed `()` (a zero-arity Tuple) and an empty struct. A hand-listed `{i8..u64, f32, f64, bool, char}` missed `usize`, `isize` and `str` and dropped their method calls entirely.

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
