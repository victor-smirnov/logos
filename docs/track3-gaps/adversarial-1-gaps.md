# Adversarial Testing #1 — catalog (2026-06-12)

First adversarial sweep post-baghunt: cross-feature intersection probes
(enum×refs×niche, Pin×move, closures×Drop, iterators×&T, Deref×generics,
slice patterns, dyn×name-resolution). rustc 1.93 as oracle where
translatable. Probes: 12 tests + ~15 bisection probes (/tmp/adv1, regressions
in `tests/logos/pass/adv1_*`).

Status legend: `Open` — not started; `✅ Closed` — fixed this session.

| ID | Class | Status | Root cause / fix | Regression |
|---|---|---|---|---|
| ADV1-A | Enum `&Struct` payload field mis-read (P0 silent) | ✅ Closed | 4 drifting copies of the payload-binding loop, none registering gen_let's &Struct convention; unified all match/let-else/expr-match loops onto `bind_enum_payload`, added thin-&Struct case + droppable-struct copy + uniform peer-shape eviction. | `adv1_enum_ref_payload_field` |
| ADV1-B | `Pin<Box<T>>` move ⇒ double free (P0) | ✅ Closed | stdlib blanket `impl<P> Copy for Pin<P>`; now `impl<P: Copy> Copy` with NEW compiler support for conditional Copy (`conditional_copy_` registry; `struct_type_is_copy` evaluates recorded arg positions recursively, honors `T: Copy` bounds in generic bodies). | `adv1_pin_conditional_copy` |
| ADV1-C | Capture drop order ≠ Rust (`d2 d1` vs `d1 d2`) | ✅ Closed | Two roots: (1) RFC 2229 drop-order rule — a move-closure path capture whose root type impls Drop captures the WHOLE var (was: narrow capture left root to scope); (2) un-skipped captures dropped at their own var_order slots — now drop with the owning closure binding in capture order (`closure_drop_group_`, single `emit_frame_drops` behind all 4 drop walks). | `adv1_capture_drop_order` |
| ADV1-D | No method autoderef through `T: Deref<C>` bound | ✅ Closed | TypeVar method resolution now falls back to rewriting the receiver as `recv.deref()` (`&Target`) and re-dispatching on the struct path. | `adv1_deref_bound_autoderef` |
| ADV1-E1 | `iter_copied(it)` uninferable (`I: Iterator<&T>, T`) | ✅ Closed | Bound-driven type-arg inference in `infer_type_args`: a param only mentioned in another param's trait bound is deduced from the bound type's own impl (target-pattern unification + trait-arg substitution). Mirrors the Fn-family propagation. | `adv1_iter_copied_bound_infer` |
| ADV1-E2 | `.copied()` METHOD form | Open | Trait-default `fn copied<U>` can't deduce U: `where Self: Trait<args>` clauses are DROPPED at default-method synthesis (where_param_bounds keys on trait params only; stored bounds carry no type-args). Alt route (CopiedIter parameterised by ref-item + shape-selected impls) fixed inference but synthesized default bodies then mis-resolve `self.next()`. Needs synthesis-side where-Self unification. Free fn `iter_copied` is the working spelling. | note in iter.logos |
| ADV1-F | Compound-assign depth limit (`o.i.a[1] += v` → "too deeply nested … yet") | Open | Place-path resolver depth limit; clean diagnostic, not silent. Lift in gen_lvalue_addr-based compound path (post place-writer-retirement follow-on). | t09 in /tmp/adv1 |
| ADV1-G | `&a[..]` typed `&&[i64]`, slice patterns broke | ✅ Closed | `&` of a RANGE-index place is the slice value itself (Rust: `a[..]` is the place `[T]`; Logos Slice ≡ fat `&[T]`). Identity rule in unary-& lowering. | `adv1_slice_reborrow_pattern` |
| ADV1-H | Local trait invisible at `dyn` position when name collides with prelude (user `trait Sub` vs ops `Sub`) | Open | `dyn` type resolution + `lower_trait_def` use BARE `traits_` lookups; B-mv-02 pkg-qualified registration never flows into TraitObject identity. Canonicalizing only the dyn site turns the compile error into a dispatch segfault — needs the full chokepoint sweep (lower_trait_def naming, upcast check, mlir-gen vtable keys). Gap note at the dyn resolution site in sema.cpp. | p2 in /tmp/adv1 |
| ADV1-I | Self-pattern of shaped generic impls (`impl … for CopiedIter<I, &T>` synthesized defaults bound Self positionally as `CopiedIter<I, T>`) | ✅ Closed | Both synthesis sites (sema_collect signature registration + sema_decl lowering) now use the impl's target PATTERN when it is shaped (any non-TypeVar/ConstVar arg); plain + const-generic impls keep positional construction. | covered via Pin/multi-impl suites |
| — | `logosc --help` crashes (`std::logic_error`: string from null) | Open | Driver arg parsing; trivial. | — |

Also fixed in-session (baseline breakage, not adversarial): HVal→HAny rename
fallout in `examples/writ_container_showcase.logos`, `tools/lforge/pkg/*`,
`docs/internals/writ2-cpp-migration.md` (whole-word rename missed non-stdlib
trees; build was red on main).

Green probes (no gap): GAT-projection equality rejection matches Rust (t08 —
my test was invalid Rust; Logos rejected identically), multi-impl shape
selection (t06), labeled break-with-value incl. in closures (t10), int
cast/wrap/shift edges (t12), `&Enum` payloads via match auto-deref (a5).
