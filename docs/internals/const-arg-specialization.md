# Const-arg specialization (interproc const-prop via mono) — T2-24 (B)

STATUS (2026-06-14): **engine SHIPPED + proven** (mono_const_arg.cpp). A
`const`-forwarding wrapper called with a literal Ordering gets a per-value
spec clone with the literal baked → the atomic op carries the precise
ordering (acquire/release), not seq_cst. Verified by IR-snapshot test
`const_arg_atomic` (FileCheck on the spec bodies). L4 5671/5671.

REMAINING — the stdlib last mile: the stdlib `_ordered` wrappers are
NON-GENERIC, so emit_module compiles them into the `.a` as binary symbols
(only generic fns get their bodies stashed as templates, emit_module.cpp:450
/ 452-453). At user-compile their bodies aren't in `in_`, so
`find_fn_def_by_base` misses them and no spec is made (sound: they keep
seq_cst). To make REAL atomic call sites benefit, body-export the
const-spec-source wrappers: at stdlib-emit stash bodies of methods that
forward a param to a registry intrinsic (even on non-generic structs), and
reconstruct them into `in_.structs[].methods` in the loader. Scoped, but in
the (delicate) serialization/trailer subsystem — a separate step.

---


Goal: a compile-time-constant value argument at a call site reaches an
intrinsic that const-evaluates it, even across non-inlined wrapper
functions. Concrete driver: `a.load_ordered(Ordering::Acquire)` must let
mlir-gen's `read_ordering_at` see the literal `Acquire` (today the wrapper's
`ord` param is a runtime VAR_REF → `seq_cst` fallback). Logos has no
inliner and the stdlib `.a` is `-O0`/no-LTO, so LLVM can't fold it — the
fix must happen at the Logos (mono) level. This RIDES the existing mono
specialization engine (the same machinery that clones a generic fn per
type-arg), extended to specialize per constant value-arg.

## Surface — seed is a registry, NOT a grammar `const` marker

DECISION (investigation 2026-06-14): the AST key space is **hard-full** —
`TinyObjectMap` packs bitmap bits[0:51] + capacity[52:57] + size[58:63] into
one u64, so `MAX_KEYS=52` cannot grow and every slot is taken (with
intentional collisions). Adding a `const`-param AST key would mean an
obscure slot reuse. So the const-want SEED is a small in-mono registry
mirroring the positions mlir-gen's `read_ordering_at` already keys on — the
reusable engine (mono spec) is unchanged; only the seed differs, and it can
become a declarative attribute later if the key space frees up.

### Const-arg position registry (from mlir_gen_expr.cpp:1895-1970)
```
logos_atomic_load{32,64}_ord       → {1}
logos_atomic_store{32,64}_ord      → {2}
logos_atomic_fetch_add{32,64}_ord  → {2}
logos_atomic_swap/or/and/xor_*_ord → {2}
logos_atomic_cas{32,64}_ord        → {3,4}   // success, failure orderings
```
A wrapper like `load_ordered(&self, ord: Ordering)` is NOT in the registry —
mono derives that its `ord` is const-forwarding (fixpoint).

### Exact injection point (found)
mono_clone.cpp:3236-3240 — after `nc.callee` is finalised (`mangle(...)`)
and `nc.args` are the cloned arg exprs, BEFORE `lir_mirror_emit_call`. Here:
inspect `nc.args` at the callee's const-want positions; if literal
(EnumLit/IntLit), rewrite `nc.callee → spec` and enqueue the const-spec.
The call scan that seeds the worklist is mono_scan.cpp:144 (ECallView).

## Phases

### 1. Grammar + sema + LIR — the `const` param marker
* Grammar: `param <- KW_CONST? (pattern COLON type_ref | …)` — a leading
  `const` sets `IS_CONST_PARAM: 1` on the PARAM node.
* Sema: copy the flag onto the LIR param (new `bool is_const` on the LIR
  param struct / FN param list).
* Behaviorally inert until mono consumes it.

### 2. Mono — const-want fixpoint
Build `const_want_: fn_base → set<param_index>`, seeded from `is_const`
params. Fixpoint over the call graph: if fn F passes its param P (a bare
VAR_REF, no transform) as the arg at a const-want position of callee C,
mark (F, P) const-want. Iterate to fixpoint (covers multi-level forwarders;
atomics need just one level). An extern (no body) only seeds; it is never
specialized (nothing to clone).

### 3. Mono — const-arg specialization
At call instantiation (mono_scan, where a NewCall is enqueued):
* Collect the const-want positions of the callee that receive a
  **compile-time constant** actual arg (EnumLit with known disc, or
  IntLit). Call these the const-bindings.
* If non-empty AND the callee has a body (not extern): spec key =
  `mangle(base) + "__cv__" + join(pos=value)`; enqueue a clone whose
  SubstMap also carries a value-substitution `param_index → literal AST`.
  Redirect the call's callee to the spec name; DROP the const args from the
  call's arg list iff the spec signature drops the const params — simpler:
  KEEP the params in the signature but bake the literal in the body, so the
  call's arg list is unchanged (the passed literal is just ignored by the
  baked body). Keeps arity stable across all sites → no ABI churn.
* Runtime (non-const) callers keep calling the unspecialized fn.

### 4. clone_fn value-substitution
In the cloned body, replace every `VAR_REF(param_name)` for a baked param
with the literal AST (EnumLit/IntLit). Reuse the existing body-rewrite walk
(the one that already substitutes TypeVars / renames). The baked literal
then flows to the intrinsic call, and mlir-gen's `read_ordering_at` reads
it. Mangling: `__cv__<pos>_<disc>` per baked param.

### 5. Seed the atomic intrinsics + test
Mark `const ord: Ordering` on the `logos_atomic_*_ord` externs. Test:
`fetch_add_ordered(Relaxed)` / `load_ordered(Acquire)` → assert the emitted
MLIR atomic op carries `monotonic` / `acquire` (not `seq_cst`). Runtime
(non-literal) ord still lowers to `seq_cst`. Gate L4.

## Bounds / non-goals
* Only EnumLit + IntLit constants (the const-foldable leaf forms). No
  arithmetic const-eval (that is metacall's job).
* Specialization is keyed per distinct const tuple → bounded by the number
  of distinct literal combos at call sites (for Ordering: ≤ 5).
* `quote_expr!` / other consumers can opt in later by marking params
  `const`; the mechanism is not atomic-specific.
