# Spec conformance — compiler bugs surfaced by test generation

Writing executable conformance tests for the extracted spec is an oracle: a rule
that cannot be confirmed by an honest, runnable assertion is either untestable at
language level (internal invariant) *or* the compiler does not actually uphold it.
The 130-unit rollout flagged the following as the latter — real defects, marked
`untestable`/`transitive` in their rule artifacts (not faked green) and recorded
here for triage. Each rule's spec statement is what the compiler *should* do.

## Confirmed (reproduce in isolation)

| Rule id | Symptom |
|---|---|
| `expr.method.receiver-multiref-autoderef` | A method call on `&&T` — both `r.m()` and explicit `(*r).m()` — reads the wrong field / segfaults at runtime, for both `Vec` and a plain user struct. Sema accepts it; codegen is wrong. |
| `stmt.let-destruct.move-on-bind` | **Double-free.** `let tup = (Own, Own); let (e0, e1) = tup;` drops 4 times — the source `tup` is not marked moved by the tuple-destructure bind. (Destructuring directly from a tuple *literal* drops correctly; the struct path `stmt.let-pat.consumes-source` is covered green.) |
| `coerce.unsize.return-concrete-to-trait-object` | Returning a `Box<Concrete>` from a `-> Box<dyn Trait>` function segfaults (139) at the return-coercion. The arg-position / let-position `Concrete -> Box<dyn>` coercion works (covered green). |
| `coerce.deref.box-struct-borrow` | Borrowing an owning DST reference — `&Box<dyn Sp>` to get `&dyn Sp` — segfaults at runtime (owning-DstRef codegen path). |
| `trait.def.vtable-layout-supertrait-closure` | Supertrait-method dispatch through a single `&dyn Sub` layout: own-method dispatch on `&dyn Sub` fails sema (`trait Sub has no method …`) and the supertrait-upcast path mis-dispatches. |

## Context-dependent (green in isolation; break only inside a large function)

These compile and run green as small standalone programs (and are covered
transitively by isolated tests), but miscompile when placed in a big,
generic-heavy `main()`. The common smell is a **monomorphization-ordering /
codegen issue at scale** (a value gets a pointer slot and a later `cmp` lowers to
`icmp ptr-vs-int`, or a scalar-cast hits a nullptr fallthrough). Worth isolating
into a minimal large-function repro before filing.

| Rule id | Symptom (large-file only) |
|---|---|
| `coerce.cast.float-to-float` | In a multi-cast body, `f64 as f32` hits the scalar-cast nullptr fallthrough (`mlir_gen: unsupported cast`) and `f64 !=` lowers to `llvm.icmp` on f64 (module verification fails). Correct in single-cast tests. |
| `type.typeof.expr` | A `typeof(a)` annotation triggers an `icmp/addi` ptr-vs-int mono bug only in the large file (a sibling `b` gets a ptr slot, so `b != N` compares ptr to int). Green in `tests/logos/pass/typeof_basic.logos`. |
| `generic.enum-lit.hint-ref-ptr-preference` | `Some(pr)` binding of an `Option<&T>`/`Option<*const T>` payload yields the match-temp address instead of the loaded pointer (`icmp ptr-vs-i64`) only in the multi-rule file. Green in isolation. |

## Not bugs (internal invariants, correctly untestable)

`expr.index.indexmut-place`, `trait.overload.generic-select-by-arg-shape`,
`metaprog.quote-item.placeholder-walk-balance` — internal lowering mechanisms
with no stable surface diagnostic; any user-level violation is caught earlier by
a dedicated diagnostic that *is* covered.

---
*Generated during the spec-extract conformance-test rollout. Re-derive with
`tools/spec-extract` (`spec-test.workflow.js` reports `untestable` with reasons).*
