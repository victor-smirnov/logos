# Symbol-name formation — single canonical mechanism (rewrite, 2026-06-20)

Victor: stop point-fixes; redo the WHOLE symbol-name mechanism properly. Length
is not a constraint. This is the DERIVE-from-foundation rewrite: ONE structured
symbol + ONE mangle + ONE parse; nobody hand-builds or positionally-parses a
symbol string again. Supersedes the staged-flip attempts (all reverted) — see
mangling.md for why qualifying concrete_struct_name in place exploded.

## The two key classes (from the full catalog)
Symbol-name uses split into two DISTINCT concerns that were conflated:
- **(a) LINK names** — the emitted symbol + everything keyed by it: `funcs_`,
  `generic_funcs_`, `templates_`, `specs_`, `done_`, `declared_fn_names_`,
  `fn_param_types_`, `LFunction.name`, ECall callees, vtable/dispatch/reflect
  globals. Module-id BELONGS in these (cross-module link distinctness).
- **(b) RESOLUTION keys** — source identity: `structs_`/`enums_`/`datatypes_`/
  `traits_` via `sema_key(pkg,name)`=`pkg::name`; `impls_`/`impls_all_` =
  `Trait::Type`; `func_overloads_`/`generic_overloads_` by BARE base. Module-id
  does NOT go in these keys — module distinction in resolution is the FILTER
  (find_func_candidates += fi->module_id). Keep `::` source-identity keys.

`concrete_struct_name` today serves BOTH → the dual-role pain. The rewrite splits
the roles: a bare instance form for (b) resolution, a qualified link form for (a).

## Canonical module — `compiler/symbol_mangle.{hpp,cpp}`, namespace `sym`
Structured identity (the single source of truth):
```
struct Sym {
  std::string module_id;          // owning module; "" = global
  std::string package;            // dotted pkg; "" = package-less
  std::string owner;              // method receiver / impl target, already
                                  // type-mangled (e.g. "Vec$G1$i32"); "" = free fn
  std::string trait;              // trait-qualifier (collision methods); "" else
  std::string base;               // free-fn name or method name
  std::vector<std::string> sig;   // mangled param-type fragments
  bool is_generic=false, is_vararg=false, is_extern=false, no_mangle=false;
};
std::string mangle(const Sym&);          // → canonical LINK symbol
Sym         parse(std::string_view);     // ← decompose (round-trips mangle)
std::string bare_base(std::string_view); // owner+base, no module/pkg/sig (= bare_fn_name)
// the type-fragment engine (was mangle_type_for_name); qualify=true → struct
// args carry [module..]pkg (link form), false → bare (resolution form):
std::string mangle_type(TypeRef, bool qualify);
std::string struct_inst(TypeRef, bool qualify);   // was concrete_struct_name(_bare)
```
Encoding (internal): `[<module>..]<pkg>{$|.}<owner__>base{__f__|__g__}<sig>`;
`..` sentinel for module (pkg has no empty segment). extern/no_mangle/main → bare.

## Migration phases (each: build → L4 green → commit; watch compile-time)
P1. Add `sym` module reproducing CURRENT encoding EXACTLY (module per §2b rules:
    free fns qualified, methods/structs bare). Re-express function_symbol_name,
    function_signature_key, concrete_struct_name(_raw), mangle_type_for_name,
    bare_fn_name as thin wrappers over `sym`. Byte-identical → L4 5733/5733.
P2. ~~Route the (b) resolution sites onto an explicit bare struct_inst(t,false)
    so they never see a qualified form.~~ **OBSOLETE / dropped.** P2 was needed
    ONLY for the abandoned "qualify concrete_struct_name in-place" plan (which
    needed resolution insulated from qualified names — attempts 1/2 exploded).
    The chosen EMISSION-BOUNDARY approach keeps concrete_struct_name FULLY BARE
    in sema AND mono — qualification is a pure final string-prefix in mlir-gen
    (link_name). Resolution/mono are never exposed to a qualified struct name →
    nothing to decouple. The OTHER aspect P2 implied — qualifying nested type-arg
    components so `Vec<A::Foo>` ≠ `Vec<B::Foo>` — is DEFERRED (emission-boundary
    leaves the owner's type-args bare; this is the cross-module same-named-type
    case, unneeded for coherent source-dist; future = resolution filter).
P3. Emission-boundary qualification (mlir-gen link_name). DONE/viable — see
    Progress. (Replaces the old "flip concrete_struct_name" P3.)
P4. Resolution filter += fi->module_id (cross-module same-pkg). Delete the
    mlir-gen canonical() bridge + bare↔pkg bridging; survivors = unrouted sites.

## Progress
- **P1 DONE + committed (abfe4997, L4 5733/5733):** `sym::Sym` + `sym::mangle`
  (lir.hpp); `function_symbol_name` routes through it byte-identically. The
  module-qualification policy (incl. the methods-exempt carve-out) now lives in
  ONE place — flipping methods is dropping `!s.is_method` in sym::mangle.
- **P3 emission qualification — ATTEMPTED, REVERTED, but PROVEN VIABLE.**
  Added `MLIRGenImpl::link_name(fn)` = insert `<module>..` before a method's
  package (method shape = `fn.name` starts with `fn.package + "."`; free fns use
  `$` boundary → unchanged); module from `prog.pkg_module_ids[fn.package]`.
  Routed forward_declare (FuncOp/llvm.func/declared_fn_names_/fn_param_types_/
  vararg_fns_), the body-emit FuncOp lookups (mlir_gen.cpp:495,502), cur_fn_name_,
  and is_binary_skip through link_name. RESULT: methods correctly module-qualified
  — `nm liblogos-lang.a` shows `m58232a1d094f719f..logos.lang.fabric.PrimVec$G1$bool__push__g__…`
  (5530 syms); lang + mem build CLEAN; sema + mono untouched (NO explosion, NO
  perf storm — the whole point of the emission-boundary approach). **BLOCKER:**
  the std build's METAPROG-JIT fails with `jit add_module: duplicate definition
  of symbol m1ff69f5e219a5697..logos.lang.option.Option__is_some__g__ref_Option__File`.
  Root: the dispatch loop add_module's the growing program each iteration; a
  cross-module method INSTANCE (Option<File>::is_some — File is std) is emitted
  External in iteration K and again in K+1 → ORC duplicate. The FINAL `.a` avoids
  this via lazy archive member-selection, but the in-process ORC JIT can't.
  CANDIDATE FIXES (next): (a) emit generic/method INSTANCES with LinkonceODR/
  WeakODR linkage (ODR-mergeable — ORC keeps one; also correct for cross-module
  final dedup) — needs setting linkage on the func.func→llvm.func path (today
  only the vararg llvm.func path sets linkage; normal methods are External by
  default); OR (b) dedup symbols across dispatch-iteration JIT modules before
  add_module. (a) is the principled one. P3 edits are reverted (tree green at
  abfe4997); re-apply + the linkage fix is the next step.

## P3 attempt #2 (2026-06-20, autonomous) — reached 823/828, REVERTED
Re-applied link_name + fixed the metaprog-JIT duplicate via the SHARED
`sym::link_name` free fn (lir.hpp) used by BOTH mlir-gen (forward_declare /
is_binary_skip / body-emit) AND the dispatch emitted-set tracking (main.cpp
M6.3) — the desync there caused the duplicate. Then a 370-fail PERF BLOWUP:
`is_binary_skip`/link_name read `pkg_module_ids`, but it was MISSING entries for
cache-skipped binary stdlib files (collect `continue`d before recording) →
stdlib methods looked un-binary → re-emitted (O(n²) lookupSymbol). FIXED by
recording pkg→module for EVERY ast in pass 0 BEFORE the skips (sema_collect.cpp).
→ 823/828. Then the dyn/tag-dispatch + vtable paths looked up methods by BARE
name (gen_tagged_dispatch / vtable slots) → routed through link_name/link_name_str
(mlir_gen_dyn.cpp). RESIDUAL (5 L2 + writ_container_showcase example): a
`func.call operand type mismatch` (expected ptr, got i64) — a NO-SIGNATURE method
callee (operator / method-as-call paths emit `<pkg>.<Owner>__<m>` without
`__f__sig`) resolves through the lossy `canonical()` bridge (which strips the
signature) to a SINGLE def of that base whose signature differs from what the
call passes. Module IDs are CONSISTENT (verified: lang.a + std.a both
`m1ff..logos.lang.iter`), so NOT an id bug. `link_name_str` handles WITH-sig
callees exactly; the no-sig ones can't be disambiguated by the bridge.
TRUE GLOBAL FIX (next): make call resolution non-lossy — either (a) qualify call
callees in the LIR so mlir-gen needs no bridge (blocked: bodies live in the
immutable Writ mirror, and mono's scan_fn re-parses callees off that mirror, so
qualifying there breaks the scan), or (b) make the no-sig method-call SITES emit a
signature (route operator/method-as-call callee synthesis through the real
resolved symbol), or (c) make `canonical()` signature-aware so distinct-sig
methods don't collapse. (b)/(c) are the tractable ones. REVERTED to P1 (green);
all the diagnosis above is reusable.

## P3 attempt #3 (2026-06-20, autonomous "доделываем") — 827/828 L2, but breaks
## the HERMES tag-dispatch / HAny class in the FULL suite. PRESERVED in git
## stash (`stash@{0}` off 67b2010e) + recoverable.
Re-applied all of P3 + the dyn/tag-dispatch+vtable link_name routing. L2 SAMPLE
= 827/828 (1 fail). But the FULL ctest fails ~dozens of writ_* tests
(writ_equal/hmap/hbs/scalars, persistent_dview, …). writ_equal builds CLEAN
at P1 (verified by stash+rebuild) → P3 regression. Symptom: a DIRECT
`func.call` to `…HMap$G2$HString$HAny__set__f__…__slice_u8__HAny` with
`(!llvm.ptr, !llvm.ptr, i64)` operands vs the func's `(ptr, ptr, ptr)` decl —
i.e. the HAny value-param is passed as i64 (niche value) but `make_fn_type`
declares it as the tagged-enum llvm_type (ptr). ZERO bridge renames for this
test (callee already resolves), so it's NOT a name-resolution miss. Module ids
verified CONSISTENT (lang.a all `ma15d566..`). Could not pinpoint why P3
(NAME-only changes) flips this HAny repr/decl — make_fn_type + call-arg lowering
are P3-invariant by inspection, yet the verifier disagrees only under P3.
Hypothesis: is_binary_skip(link_name) flips a specific HMap<HString,HAny>
instance from defined→declared (or the reverse) so the call binds to a
declaration whose make_fn_type HAny-repr differs from the call's — i.e. a
PRE-EXISTING HAny dual-repr (i64 niche value vs *zoned/enum ptr) latent
inconsistency that P3 exposes via the skip-path. Needs HAny-lowering debugging.

## P3 LANDED (2026-06-20, autonomous) — methods module-qualified, L4 5733/5733
The earlier attempts' "HAny repr bug" was a RED HERRING. The whole failure class
(writ operand-type mismatch, box_deref arity, String::from→new, Rc/Box/RAII/
persistent/zoned runtime drop holes) reduced to ONE root: **method/drop callees
are BARE in the LIR but their FuncOps are emitted module-QUALIFIED**, and every
resolution site that looked up by the bare name missed → either picked the wrong
overload (signature-blind suffix fallback) or silently skipped the call.

THE FIX is DERIVE-from-foundation: make `find_func_op` (THE callee-resolution
chokepoint, now a member) module-aware — after a bare miss it tries the EXACT
`link_name_str`-qualified form (full signature preserved, O(1) hash) BEFORE any
O(n) walk — and route the two drop-emission sites (`gen_drop_value` struct/enum +
the `SDrop` handler in mlir_gen_stmt.cpp) through it instead of direct
`mod.lookupSymbol`. Edits:
- `find_func_op`: member; qualified-hash-probe-before-walk (correctness + avoids
  O(n²)); memoises successful resolutions (`find_func_op_cache_`).
- forward_declare: `fn_param_types_`/`fn_param_owning_box_dyn_` dual-keyed by BOTH
  the bare `fn.name` AND the qualified link name (call-arg coercion looks up by
  the bare callee during body emission).
- gen_method_call: rely on the chokepoint (removed the per-site qualify patch).
- drop glue: `gen_drop_value` (struct+enum user-Drop) + SDrop `drop_fn` lookups →
  `find_func_op`.

PERF (Victor flagged a regression — root-caused + fixed):
- **find_func_op O(n²)**: first cut placed the qualified probe AFTER the O(n)
  find_fn_matching walk; under modules nearly every method callee misses the bare
  lookup → O(calls×functions). Fixed by ordering the qualified O(1) hash first.
- **mono+borrow re-instantiation STORM** (borrow 12ms→1321ms on a TRIVIAL test):
  the P3 `.a` emits METHODS module-qualified (`<module>..<pkg>.<rest>` via
  link_name), but mono's precompiled-skip check uses the BARE `<pkg>.<rest>`
  (function_symbol_name exempts methods) → mono didn't recognise stdlib methods
  as precompiled → RE-MONOMORPHISED the whole stdlib in EVERY consumer compile;
  the linker dedups the final `.a` (same symbol count) so it was invisible there.
  Isolated by P3-logosc × HEAD-.a (fast) vs × P3-.a (slow). Fixed at the single
  `binary_symbols` load point (main.cpp `collect_syms`): also insert the BARE
  alias (everything after the `..` sentinel) for each qualified method symbol.
  RESULT: light tests at parity (lit_i64 0.45s vs HEAD 0.46s; writ 0.47 vs
  0.51), heavy mono/codegen tests ~1.25-1.4× (inherent longer-name cost); full
  stdlib build 1:43 vs 1:09.

DEFERRED RESIDUAL: the ~1.25-1.4× on codegen-heavy tests (and ~1.5× stdlib build)
is the inherent cost of longer module-qualified symbols in MLIR emission + the
canonical() bridge's per-name string ops; not a storm. Future micro-opt, not a
blocker.

## (superseded) earlier conclusion — kept for context
CONCLUSION: the emission-boundary P3 repeatedly hits deep interactions (perf →
dyn-dispatch → HAny). The name-qualified/bare boundary is too pervasive. RECON-
SIDER: either (1) the truly-global LIR-qualification (blocked by Writ-mirror
immutability + mono scan_fn re-parsing — needs solving that first), or (2) ship
with methods EXEMPT (committed P1 + §2b free-fns, green) and qualify method link
symbols at LINK time (objcopy `--redefine-syms` on the .a using the pkg→module
map) — a post-compiler step that sidesteps the whole in-compiler boundary. (2)
is likely the pragmatic winner. P1 stands green (L4 5733/5733). [RESOLVED: the
in-compiler boundary WAS tractable via the find_func_op chokepoint + the mono
binary_symbols bare-alias — see "P3 LANDED" above.]

## Invariants
- (b) `::`-keys stay source-identity (pkg::name / Trait::Type), never module.
- mono dedup keys (done_/done_methods_/concrete_struct_types_) use the SAME form
  as the names they dedup (all bare in P1-P2; all qualified-consistent in P3).
- extern / #[no_mangle] / main / metacall thunks: bare, never qualified.
- rename free `lir.hpp qualify_pkg` → it now lives in `sym` (clash w/ mlir-gen
  member resolved by moving callers to sym::).
