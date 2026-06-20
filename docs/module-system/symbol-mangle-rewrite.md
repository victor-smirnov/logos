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

## Invariants
- (b) `::`-keys stay source-identity (pkg::name / Trait::Type), never module.
- mono dedup keys (done_/done_methods_/concrete_struct_types_) use the SAME form
  as the names they dedup (all bare in P1-P2; all qualified-consistent in P3).
- extern / #[no_mangle] / main / metacall thunks: bare, never qualified.
- rename free `lir.hpp qualify_pkg` → it now lives in `sym` (clash w/ mlir-gen
  member resolved by moving callers to sym::).
