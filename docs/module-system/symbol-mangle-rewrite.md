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
P2. Route the (b) resolution sites onto an explicit bare `struct_inst(t,false)`
    so they NEVER see a qualified form (decouple from the link form). Inert.
P3. Flip the link form: `mangle` emits `[module..]pkg` for methods+structs too
    (struct_inst(t,true) in (a) sites + LFunction.name + callees). Because (a)
    parsers route through `sym::parse` (not substr) and (b) uses bare, no
    positional breakage and dedup stays consistent (keys move together).
P4. Resolution filter += fi->module_id (cross-module same-pkg). Delete the
    mlir-gen canonical() bridge + bare↔pkg bridging; survivors = unrouted sites.

## Invariants
- (b) `::`-keys stay source-identity (pkg::name / Trait::Type), never module.
- mono dedup keys (done_/done_methods_/concrete_struct_types_) use the SAME form
  as the names they dedup (all bare in P1-P2; all qualified-consistent in P3).
- extern / #[no_mangle] / main / metacall thunks: bare, never qualified.
- rename free `lir.hpp qualify_pkg` → it now lives in `sym` (clash w/ mlir-gen
  member resolved by moving callers to sym::).
