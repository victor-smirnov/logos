# Track 3 Phase 2 — gap-grind sequencing plan

End of Phase 1 (wide sweep): B1-B10 committed, 51 imported tests
across 10 categories, ~30 gaps catalogued in this directory. ctest
1251/1251 green at handoff.

## Phase 2 goal

Close gaps in priority order. Each gap closed → un-trim every
imported test that referenced it → those tests become regression
gate for the fix.

## Gap inventory (consolidated across 10 docs)

Ranked by **blocker breadth** (how many imported / declined tests
would un-trim if the gap closes):

### Tier A — big blockers (>5 tests blocked, lots of un-trim payoff)

1. **Fn / FnMut / FnOnce trait family** (C5-cl-01, T9-tr-03)
   - Blocks: bulk of closures/, half of traits/, parts of borrowck/,
     iterators/, methods/. **The biggest single blocker.**
   - Cost: ~1-2 weeks (trait-resolver work, closure-as-trait-object
     lowering, three-trait family).
   - Approach: do as part of the planned Datalog-derived trait
     resolver (per `project_roadmap_2026_05.md`).

2. **Tuple types / tuple patterns** (P4-pm-03)
   - Blocks: match/, pattern/, many parser/, generics/, borrowck/.
   - Cost: ~1 week (parser + sema + lir for `(T, U, …)` everywhere).
   - Approach: tuple types are independent of trait resolver; can be
     done before Datalog.

3. **`new` / `null` / other KW as identifier** (S8-st-01, M7-mt-01,
   K10-co-02, T9-tr-01-adjacent)
   - Blocks: many constructor-pattern tests across all categories.
   - Cost: small grammar tweak (allow KW_NEW / KW_NULL in IDENT
     positions where unambiguous). ~1 day.
   - Approach: do early — high payoff per LOC.

4. **Implicit coercions** (B3-bg-05, B3-bg-06, C6-cc-04, C6-cc-05,
   C6-cc-06)
   - Blocks: coercion/, parts of borrowck/, parts of typeck/.
   - Cost: medium (sema-pass for known coercion shapes). ~1 week.
   - Approach: target the specific shapes (`&mut→&`, `&→*const`,
     `&literal` temp lifetime). Don't try to be fully Rust-compatible.

5. **Trait default method bodies** (M7-mt-02, T9-tr-05)
   - Blocks: many traits/, methods/ tests.
   - Cost: small (grammar + sema). ~2-3 days.
   - Approach: straightforward; do alongside (3).

### Tier B — narrow but irritating (1-5 tests each)

6. **`if let` form** (P4-pm-05) — small grammar fix.
7. **Struct-shape enum variants** (P4-pm-01) — bigger feature, but
   the syntax is well-defined. ~1 week.
8. **Nested patterns inside variant payload** (P4-pm-02) — sema
   work to recurse pattern lowering.
9. **Array-prefix patterns `[1, ..]`** (P4-pm-04) — pattern grammar
   extension.
10. **Negative integer literal as enum discriminant** (S8-en-01) —
    small grammar fix.
11. **Top-level `const` as array length** (C6-cc-02) — sema; small.
12. **`&&T` greedy-stack at type position** (P3-pg-01, P3-pg-02,
    B3-bg-03) — lexer/parser interplay; medium.
13. **Vec → slice coercion** (B3-bg-06) — depends on slice/IntoSlice
    surface; tied to iterator-borrow gap.
14. **Auto-deref of `&u8` at op site** (C6-cc-01) — small sema fix.

### Tier C — single-test gaps

15. **mlir-gen GEP on by-value struct destructure** (P4-pm-08) —
    codegen bug, isolated repro.
16. **mlir-gen mangled-key miss for impl-on-primitive + generic
    method** (T9-tr-02) — codegen bug.
17. **`for i in &v` borrowed-iterator pattern** (B3-bg-07) — needs
    full reduction first.
18. **Closure capture-by-ref / addr-of captured** (B3-bg-04 = C5-cl-05)
    — sema/codegen for closure capture mode.
19. **`move` keyword on closures** (C5-cl-02) — small grammar.
20. **`ref` in closure parameter pattern** (C5-cl-03) — small grammar.
21. **`Box<dyn FnMut()>` boxing** (C5-cl-04) — depends on (1) + (4).
22. **`mem::transmute`** (K10-co-03) — Logos divergence; document not
    fix.
23. **Unicode in source / identifiers** (P3-pg-03) — deferred per
    user instruction.

## Proposed Phase 2 schedule

Sprint 1 — small grammar wins (~1 week):
- (3) KW-as-ident family
- (5) trait default-bodies
- (6) `if let`
- (10) negative enum discriminants
- (11) top-level const at array-length
- (14) auto-deref at op site
- (19) `move` on closures
- (20) `ref` in closure param

Expected payoff: ~10-15 imported tests un-trimmed; opens up many
follow-on tests for second-pass import.

Sprint 2 — coercion sweep (~1-2 weeks):
- (4) implicit pointer coercions + `&<literal>` temp lifetime
- (12) `&&T` greedy-stack

Sprint 3 — tuple types (~1-2 weeks):
- (2) full tuple type/pattern/expr support

Sprint 4 — pattern / variant features (~2-3 weeks):
- (7) struct-shape enum variants
- (8) nested patterns inside variant payload
- (9) array-prefix patterns

Sprint 5 — Datalog trait resolver (~3-6 weeks):
- (1) Fn / FnMut / FnOnce family
- (13) Vec → slice coercion (touches IntoSlice/iterator family)
- (T-shaped) un-trims dozens of tests across borrowck/closures/
  iterators/methods/traits

Sprint 6 — codegen cleanups:
- (15), (16) — mlir-gen bugs
- (17) iterator-borrow

## When second-pass import happens

After Sprint 1: pass through B1-B10 once, un-trim every test that
references closed gaps. Each un-trim is a 1-line provenance-header
update + the actual code change. Some tests will graduate from
trimmed to full-Rust-equivalent.

After Sprints 2-5: same, plus opens up importing the *next* 20-30
tests per category that were previously declined for the closed gap.

## Notes

- Memory file `project_roadmap_2026_05.md` calls out Datalog as
  Sprint 5's vehicle — confirm before starting.
- The full ctest cost will grow with un-trimming; cross-check the
  segmentation plan (`project_test_segmentation_plan.md`) is
  triggered when wall-clock crosses 3 minutes.
- Avoid scope creep — only un-trim tests that closed gaps actually
  unblock. Don't re-port whole new categories on every sprint.
