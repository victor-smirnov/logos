# Symbol mangling — module-qualified, bridge-free (design, LOCKED 2026-06-20)

Goal: one canonical symbol form, produced by sema, carried through mono
UNCHANGED, no reconstruction → the `canonical()` bare↔pkg bridge
(mlir_gen.cpp:564) and all ad-hoc bare re-synthesis are DELETED. Module-id is an
intrinsic part of identity (distinguishes same-named `foo.Bar::baz` from
different modules in ONE compilation — the `use … from` case), so it is present
in the name wherever mono compares/keys/dedups identity. Decided with Victor.

## Encoding — `..` sentinel
Qualified package = `[<module_id>..]<pkg>` (module-id sanitized `[A-Za-z0-9_]`;
pkg is `.`-dotted, never empty segments ⇒ `..` is unambiguous).

- free fn:  `[<mid>..]<pkg>$<base>__f__|__g__<sig>`
- method:   `[<mid>..]<pkg>.<Owner>__<method>__f__|__g__<sig>`
- type/struct name (concrete_struct_name): `[<mid>..]<pkg>.<Struct>$G<n>$<arg…>`
- extern:   bare `<name>` (own ABI — NEVER qualified)
- absent module → no `<mid>..`; absent pkg → collapses; both absent → bare.
  ONE parser: "contains `..` ⇒ left of it is module-id"; the rest is the
  existing `[pkg][$|.]owner…` logic, unchanged. Safe omission falls out.

`module-id` source: `LProgram::pkg_module_ids[pkg]` (filled by sema §2a). Both
roles of `concrete_struct_name` (method-OWNER and type-ARG component) get the
SAME qualification — else `Vec<A::Foo>` and `Vec<B::Foo>` collide.

## Why this avoids the perf blowup (the §2b-methods attempts' failure)
The blowup came from qualifying the call/emit name OUTSIDE `concrete_struct_name`
while registry KEYS still came from (bare) `concrete_struct_name` → mismatch →
`concrete_struct_types_`/`done_methods_` miss → re-instantiation storm. Putting
the qualification INSIDE `concrete_struct_name` moves registry-insert + lookup +
call + emit TOGETHER → consistent → no mismatch, no storm, no bridge.

## Single API (source of truth — sema + mono + mlir-gen all route through it)
- `qualify_pkg(module, pkg) -> string`  (`[mid..]pkg`)
- `split_symbol(sym) -> {module, pkg, rest}`  (strip on `..`, then existing)
- `concrete_struct_name` builds via qualify_pkg (uses the global pkg→module map
  pointer, set per-compilation like `g_mangle_erase_fnptr`).
- `function_symbol_name`: methods un-exempted; must NOT double-add pkg when the
  base already carries it from `concrete_struct_name`.

## Staging (each step gates L2 on CORRECTNESS **and** compile-TIME, then L4)
1. canonical API + global pkg→module pointer plumbed (sema run + mono run).
2. FLIP `concrete_struct_name` → fully qualified (both roles). Keep the
   `canonical()` bridge as a SAFETY NET this stage (reconciles any straggler).
   Gate L2 (watch wall-time — a regression = a surviving bare re-synthesis site).
3. un-exempt methods in `function_symbol_name`; kill ad-hoc bare re-synthesis in
   mono (blanket emit, `T→Concrete`, dest_name builders) — make them build from
   the full template name / qualified `concrete_struct_name`.
4. DELETE `canonical()` + bare↔pkg bridging. Any breakage now = a real surviving
   re-synthesis site → fix THAT, never restore the bridge.

## Carve-outs / invariants
- extern fns: bare, never touched.
- package-less / `main` / runtime-entry: bare (no module, no pkg) — `..`-absent.
- `bare_fn_name` (lir.hpp:1317) already strips module+pkg+suffix to a canonical
  bare key (rfind('.') takes the last segment) — keep it as the bare-key helper
  for any place that legitimately needs the short name.

## Stage-2 first attempt (2026-06-20) — REVERTED, crucial scope finding
Flipped `concrete_struct_name` → `[<module>..]<pkg>.<Struct>$G…` (global pkg→module
pointer `g_pkg_module_ids` set in sema/mono/mlir-gen run). logosc compiled, but
the FIRST stdlib file (`stdlib/lang/iter/iter.logos`) failed AT SEMA:
  `'m58232a1d094f719f..logos.lang.iter.RevIter$G2$I$T' has no method 'next'`
i.e. the blast radius reaches **sema's OWN method/impl dispatch**, not just
mono/mlir-gen codegen. `concrete_struct_name` is used by sema to form the
method-lookup key, but sema's method/impl/struct REGISTRIES are keyed by the
BARE struct name. Qualifying the lookup key (even just pkg, let alone module)
desyncs it from the bare-keyed tables → self-method resolution fails everywhere.

⇒ REVISED STAGING. Stage 2 must be SPLIT:
  2a. Make sema's type/method/impl resolution **qualified-name-tolerant** FIRST —
      key (or normalize) the dispatch tables by the SAME form `concrete_struct_name`
      will produce. Find every sema site that builds a struct/method/impl key from
      a struct name (lower_method_call, impls_all_, struct method tables,
      struct_specs, datatypes_) and route through one key form. Gate L2 WHILE
      concrete_struct_name is still bare (inert wrt output) — proves the tables
      tolerate the future form.
  2b. THEN flip concrete_struct_name (pkg first, then module). Then mono/mlir-gen.
This makes the sema-core re-keying its own gated sub-stage — the largest part,
now identified. The earlier "keep canonical() bridge as net" does NOT help here
(this is pre-mlir-gen, sema-level resolution).

## Status
Foundation committed 3823425f (doc + qualify_pkg/split_qualified_pkg primitives),
on free-fn checkpoint 47199179 (free fns + module-level qualified, L4 5733/5733).
concrete_struct_name flip REVERTED. Methods exempt. Branch module-system.
NEXT = stage 2a (sema dispatch qualified-name-tolerant), fresh session.
