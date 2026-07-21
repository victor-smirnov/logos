# Corpus revision — full ledger

The honest number replacing the hunch: across the 77 rust `tests/ui` categories
we have drawn from, **864/6819 = 12%** of originals are traceably imported
(via `Original path:` headers). 5955 untraced. This is NOT a bug count: each untraced
original owes a verdict — imported | divergence §N | gap-ticket | not-applicable. A skip
without a verdict is the survivor-bias mechanism (see project_imported_corpus_revision).

Tiering steers triage: PORTABLE = behavior Logos should match, a gap likely means a real
bug; DIVERGENT/NA = rust-specific diagnostics, const-eval (→ metacall §A1), rustc
regressions, unstable features — gaps expected and cheap to verdict; MIXED = case by case.

## PORTABLE — 351/1609 = 21%  (1258 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| pattern | 17 | 129 | 112 |
| closures | 27 | 136 | 109 |
| self | 16 | 100 | 84 |
| methods | 10 | 89 | 79 |
| drop | 22 | 100 | 78 |
| unboxed-closures | 21 | 95 | 74 |
| match | 23 | 93 | 70 |
| associated-consts | 3 | 69 | 66 |
| coercion | 17 | 81 | 64 |
| array-slice-vec | 20 | 83 | 63 |
| generics | 32 | 95 | 63 |
| iterators | 1 | 61 | 60 |
| cast | 15 | 73 | 58 |
| binop | 8 | 48 | 40 |
| enum | 20 | 57 | 37 |
| let-else | 4 | 40 | 36 |
| for-loop-while | 39 | 69 | 30 |
| loops | 5 | 25 | 20 |
| range | 2 | 22 | 20 |
| tuple | 4 | 23 | 19 |
| destructuring-assignment | 2 | 18 | 16 |
| functions-closures | 19 | 33 | 14 |
| autoref-autoderef | 6 | 19 | 13 |
| deref | 2 | 14 | 12 |
| str | 1 | 13 | 12 |
| overloaded | 15 | 24 | 9 |

## MIXED — 320/2035 = 15%  (1715 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| traits | 49 | 333 | 284 |
| associated-types | 27 | 290 | 263 |
| impl-trait | 5 | 198 | 193 |
| structs | 19 | 82 | 63 |
| inference | 11 | 73 | 62 |
| moves | 5 | 62 | 57 |
| type | 2 | 55 | 53 |
| fmt | 1 | 48 | 47 |
| statics | 2 | 48 | 46 |
| fn | 5 | 49 | 44 |
| static | 3 | 47 | 44 |
| reachable | 3 | 46 | 43 |
| numbers-arithmetic | 27 | 69 | 42 |
| binding | 68 | 108 | 40 |
| unsized | 2 | 41 | 39 |
| structs-enums | 33 | 71 | 38 |
| variance | 2 | 38 | 36 |
| or-patterns | 9 | 44 | 35 |
| enum-discriminant | 5 | 39 | 34 |
| dst | 1 | 33 | 32 |
| where-clauses | 5 | 34 | 29 |
| type-inference | 1 | 22 | 21 |
| return | 2 | 22 | 20 |
| box | 1 | 20 | 19 |
| recursion | 5 | 24 | 19 |
| reborrow | 1 | 19 | 18 |
| type-alias | 1 | 14 | 13 |
| block-result | 1 | 13 | 12 |
| type-alias-enum-variants | 2 | 14 | 12 |
| builtin-superkinds | 1 | 12 | 11 |
| numeric | 2 | 13 | 11 |
| shadowed | 1 | 9 | 8 |
| mut | 3 | 10 | 7 |
| ufcs | 1 | 7 | 6 |
| never_type | 0 | 4 | 4 |
| ptr_ops | 2 | 6 | 4 |
| zero-sized | 1 | 5 | 4 |
| expr | 11 | 13 | 2 |

## DIVERGENT/NA — 193/3175 = 6%  (2982 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| consts | 4 | 483 | 479 |
| borrowck | 26 | 454 | 428 |
| parser | 12 | 419 | 407 |
| issues | 52 | 394 | 342 |
| nll | 2 | 224 | 222 |
| typeck | 12 | 226 | 214 |
| regions | 42 | 218 | 176 |
| const-generics | 2 | 160 | 158 |
| attributes | 1 | 139 | 138 |
| privacy | 2 | 126 | 124 |
| lifetimes | 6 | 123 | 117 |
| mir | 30 | 139 | 109 |
| codegen | 2 | 70 | 68 |

## Verdicts landed

### cast (58 originals; first triaged category)
30 PORTABLE (7 works, 13 GAP, 10 capped) · 21 DIVERGENCE (mostly region-variance dyn-ptr cast legality, an NLL-integrated subsystem) · 7 not-applicable.
**Root-cause finding, verified in sema_expr.cpp:1082**: `as`-cast validity is a BLOCKLIST (aggregate→scalar) not the RFC0401 permitted-cast allowlist, so `()`/`&ref`/`char→float`/`bool→ptr`/`enum→ptr` are accepted; two are memory-unsafe at runtime (`Box<[T;N]> as Box<[T]>` double-frees, `usize as *const [u8]` segfaults). Recorded as an arc-candidate (bug_cast_validity_blocklist) — a positive allowlist rewrite, PAIR (how strict `as` should be is a language-surface question). Plus a small parser ticket: zero-arg tuple variant `Foo()` doesn't parse.

### coercion (63 originals; validates the just-closed coercion arc)
36 PORTABLE (11 works, 7 distinct GAPs across ~9 files, 16 capped/probed) · 15 DIVERGENCE (mostly unstable `#![feature(coerce_unsized/unsize/type_ascription/never_type)]`, async, HRTB leak-check) · 12 not-applicable (rustc diagnostic-wording, ICE-avoidance, inference internals). All compile-only (weak oracle, flagged).
Verified gaps (spot-checked personally, not just agent-reported):
- **GAP2 deref coercion absent** — `f(&b)` for `b: Box<i64>` → "expected &i64, got &Box<i64>"; `String→&str` likewise. RFC-241; explicit `&*b` works. Arc-sized (bug_deref_coercion_absent). The just-closed arc applied EXISTING coercions everywhere; this coercion does not exist at all — orthogonal.
- **GAP3 array-of-fn-items doesn't LUB to fn-ptr** while if/else and match DO — inconsistent with the branch-merge LUB (arc A3). Adjacent, small.
- **GAP5 `&(dyn Trait + Send)` is a parse error** (`Box<dyn Trait + Send>` parses). Parser ticket.
- **GAP7 `impl Unpin for dyn Trait` accepted** — Rust E0321 (auto-trait impl on dyn is unsound). Soundness ticket.
- GAP1 `dyn Any` lacks Debug impl; GAP6 `&&[i32] as &[i32]` over-permissive `as` (folds into cast blocklist arc).

### match + enum (107 originals) & drop (78 originals)
- **match/enum**: verdicts across 107; ~17 agent-reported gap classes. PERSONALLY VERIFIED: `()` pattern is an unchecked wildcard (`[()]` matches `[i64;1]`, no type error); `#[repr(u8)] enum {A=300}` does NOT enforce range while native `enum : i8 {A=223}` correctly rejects (attribute path no-ops the check — doc/impl disagree, items.md says they're identical). NOT REPRODUCED at minimal: enum-with-Drop→int cast "ICE" (my minimal compiles clean — a real gap, accepts what Rust rejects, but not the crash the agent reported). Measurement refinement: 9 "untraced" files are actually imported under other names → the `Original path:` grep UNDERCOUNTS; true coverage is modestly above 12%.
- **drop** (agent LINKED+RAN — strong effect oracle): VERIFIED accepts-that-should-reject: explicit `x.drop()` (E0040) → double-drop; `impl Drop for Option<A>` foreign type (E0117 orphan). Agent-reported (not personally re-run, need link+run): array-literal-element + `break`-in-loop miscompile (segfault/hang, adjacent to known G167-4); `*box = v` deref-assign unsupported. NOT REPRODUCED at minimal: match-on-place over-moves when arm binds nothing (my minimal compiles clean).

### Two arc-candidate CLUSTERS from this wave
- **Drop-trait coherence not enforced** (bug_drop_coherence_unenforced): explicit-destructor-call ban + orphan rule for Drop impls; explicit `.drop()` is a double-free class hole.
- **Pattern/discriminant type-checking is lenient**: `()` pattern hole, literal-suffix-in-pattern unchecked, `#[repr]` range no-op. A pattern-typing pass audit.

### Discipline note
Two agent-reported gaps (enum-Drop-cast ICE, match-move) did NOT reproduce at minimal — recorded as such, not as confirmed. The "agent is not an oracle; the probe must be able to reproduce" rule caught both.

## Wave 2 (in progress)

### self (84 originals; agent did this half directly)
46 DIVERGENCE (arbitrary self types — `Box<Self>`/`Rc<Self>`/`Pin<P<Self>>`/raw-ptr/custom Receiver; Logos supports only `self`/`&self`/`&mut self` — a large, coherent divergence) · 9 DIVERGENCE (dispatch_from_dyn/CoercePointee unstable) · 2 DIVERGENCE (no `self::` path prefix, no inline `mod {}`) · 6 n-a · 12 WORKS · 9 GAP in 5 classes. PERSONALLY VERIFIED:
- **`Self(v)` tuple-struct ctor unresolved** — `call to undefined function 'Self'`. Common Rust idiom; should work. (bug_self_handling)
- `Self` not lexically reserved — `let Self = 5`, `struct Self {}` both compile (Rust hard-rejects).
- trait method `fn m(self: &Unrelated)` accepted — self-param type validity unchecked at decl.
- HRTB `for<'a> &'a T: Bar` parse-fails on compound LHS (plain-ident LHS works).

### closures + unboxed-closures (182 originals; agent re-verified personally after a delegation false start)
closures: 6 works, 13 gap, ~26 divergence (unstable feature-gates), ~30 n-a, ~34 capped. unboxed-closures: 6 works, 9 gap, ~30 divergence (genuinely need `#![feature(unboxed_closures,fn_traits)]`), rest n-a/capped.
**HEADLINE — biggest find of the revision, root VERIFIED in spec traits-generics.md:100**: Fn/FnMut/FnOnce are NOT distinguished — the bound is satisfied intrinsically by any closure regardless of capture/mutation/consumption. Documented, but UNSOUND: `FnOnce` callable twice → confirmed **double-free** (valgrind/SIGABRT); `Fn` closure mutating a capture accepted (E0525). One root explains 8/14 repros. Recorded bug_fn_trait_kind_noop (arc + PAIR — closure-kind inference; documented-wrong ≠ blessed when it double-frees). Plus separate codegen bugs: segfault on `f: &F` called via `f(x)`; MLIR crash on labeled-break-in-closure targeting an enclosing fn.

  - *(orphan sub-agent batch, corroborating)*: net-new specific beyond the Fn-kind/deref findings — **drop-coverage LEAK on an escaped-borrow closure capture**: a closure capturing a local by-ref that outlives the local's block both misses the escape/region check AND never runs the capture's Drop (verified via `--emit-mlir`, 0 drop calls). Folds under the escape-check gap + drop-coverage.

### pattern (112 originals; agent cross-checked the live spec suite, refined sibling framing)
9 confirmed GAP + 2 noted · 33 divergence (const-pattern/structural-match, box_patterns, rust-2024 match-ergonomics, turbofish-in-pattern, static-in-pattern) · 36 n-a · 34 already-covered-live. PERSONALLY VERIFIED cluster (bug_pattern_typing_lenient):
- **G8 variant-shape not validated** — struct-variant matched as tuple `FooB(a,b)` binds fields POSITIONALLY & silently (correctness hole); unit-as-tuple and tuple-as-bare also accepted.
- G6/G7 duplicate binding in a match arm / struct field named twice — accepted ("last wins", no equality check).
- G1 bare ident shadows a tuple-struct name → fresh catch-all binding. G2 `()` matches any type. G3 literal-suffix unchecked.
- Refinement: slice-pattern-in-tuple is a CLEAN rejection (baghunt B-pt-13 open), NOT the "silent vanish" the sibling reported — good rigor.
