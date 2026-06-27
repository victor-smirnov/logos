# ADR 0002 — Target-driven hooks, pre-sema execution, derive(Debug)

Status: Accepted (commits 17682e3, 2c6638a, 1d71187, 8f44d3f, d2cfc0e,
a616875, 103c72a, 2307d3d, 4844816, 27dab8c, ad slices 24a/24b/c/25 on
`compiler-refactoring1`).
Date: 2026-04-25.

Supersedes section 4 of ADR 0001 and removes `#[metaprogram_post_sema]`
as the primary metaprog entry point. The host/JIT seam (ADR 0001
sections 1, 2, 3 first half, 5, 6) is unchanged.

## Context

ADR 0001 framed Phase 7 around `#[metaprogram_post_sema] fn(...)` —
module-wide hooks that fire after every sema iter and decide what to
emit by walking the entire AST themselves. That shape worked for the
slice-3..6 demos but did not scale to derive-style metaprograms:

- A `derive(Debug)`-style hook needs to fire **once per annotated
  item** and receive that item's offset. The post-sema form forced
  hooks to re-walk root.ITEMS searching for their own trigger, then
  match by attribute themselves.
- `derive(Debug) for Foo` synthesises an `impl Debug for Foo`. The
  *entry file's own body* `w.dbg(&mut s)` references `Foo::dbg`, which
  doesn't exist until after the hook runs. Post-sema timing meant the
  body had to lower successfully *before* the hook had a chance to
  emit — chicken/egg.

## Decisions

### 1. Per-target dispatch via `#[metaprog_handler("trigger")]`

```logos
#[metaprog_handler("derive_debug_e2e")]
fn derive_debug_e2e_hook(target_offset: u32) -> () { ... }

#[derive_debug_e2e]
struct Widget { x: i32, y: i32 }
```

Sema collects two parallel facts from the entry AST:
- `metaprog_handlers: HashMap<trigger_name, Vec<handler_fqn>>` —
  every `#[metaprog_handler(K)] fn` declaration.
- `metaprog_targets: Vec<(trigger_name, item_offset)>` — every item
  carrying a `#[K]` attribute where `K` matches a known trigger.

The driver fires `handler(item_offset)` for each (trigger, target)
pair, in source order; multiple handlers under the same trigger run
in declaration order (slice 14). The handler reads the target via
`AnyVal::from_offset(target_offset)` against the host's `OView`.

Validation (slice C / 1d71187): handler signature must be
`fn(u32) -> ()`, free, non-extern, non-generic.

### 2. Pre-sema execution under `metaprog_mode`

Sema gains a `SemaOptions { metaprog_mode: bool, entry_ast_idx: u32 }`
flag (slice 17 / d2cfc0e). When set, sema:

- Lowers stdlib + dependency module bodies as usual.
- Lowers handler fns fully (they need to JIT-run).
- **Skips body lowering** for entry-file fns that are *not* handlers
  — only their signatures are collected. Each such function records
  `is_metaprog_stub = true` on its `LFunction`. This avoids errors on
  references to to-be-synthesised symbols (`w.dbg(...)` works because
  sema never tries to lower it during discovery).

The pipeline becomes:

```
iter 0..16:
  prog = sema_lower(asts, metaprog_mode = true, entry_idx)
  if no targets/handlers: break
  jit-compile + run hooks
  if not any_emitted: break        // primary exit
prog_final = sema_lower(asts, metaprog_mode = false)
prog_final.functions = [f for f in prog_final.functions if not f.is_metaprog_stub]
prog_final.functions = [f for f in prog_final.functions if f not in metaprog_handlers]
```

The final non-metaprog pass sees synthesised items in `asts` and
lowers entry-file bodies for real.

### 3. Hook-strip pass

`#[metaprog_handler]` fns are compile-time-only: they reference
`logos_emit_source` / `logos_metaprog_error` / `logos_get_module_ast_oview`
which exist in the JIT process but are not linkable into the final
artifact. After the final sema pass, the driver removes every
function whose name appears in `prog.metaprog_handlers` from
`prog.functions` (slice 19 / 103c72a) and weak-stubs the host externs
in `libstdlib_rt` (`stdlib/rt/metaprog_stubs.c`) so the regular
sema/codegen path doesn't trip on them.

### 4. Source-splice unchanged from ADR 0001 §2

`logos_emit_source(*const u8) -> i32` still parses the chunk and
appends to `g_asts` with string-equality dedup. Each emission adds
one new Writ document; nothing in `asts` is mutated in place. The
auto-detected target package (`Emitter::emit_target_pkg(&view)`,
slice 20 / 2307d3d) writes synthesis into the same package as the
trigger, so `impl Debug for Widget` in `package main;` works without
the hook hard-coding `"main"`.

### 5. AST schema surface for hooks

`std.compiler.metaprog::OView` carries the AST as `(holder, base, size)`
with a `WritRead` impl. Conveniences live as inherent methods on
`OView`:

- `ast_node_name(node)` — works on STRUCT, ENUM, MODULE, VARIANT_DEF,
  PATH_PART, etc.
- `ast_struct_field_count(node)` / `ast_struct_field(node, idx)` /
  `ast_field_name(field_def)` — STRUCT walk.
- `ast_type_param_count(node)` / `ast_type_param_name(node, idx)` —
  generic struct/enum derive (slice 24a).
- `ast_enum_variant_count(node)` / `ast_enum_variant(node, idx)` /
  `ast_variant_arity(variant)` — enum derive with mixed unit/tuple
  variants (slice 24b/c).
- `module_package_append(buf)` — drives `Emitter::emit_target_pkg`.

Slice 25 (d80c9a4) culled three stale Emitter helpers
(`emit_fn_i64`, `emit_fn_i64_named`, `emit_view`) and one stale
OView helper (`ast_field_type_name`). The Emitter API now consists
of `emit_raw`, `emit_into`, `emit_target_pkg`, plus the public `buf`
field for direct text composition, and `commit()` for flush.

### 6. Removal of `#[metaprogram_post_sema]`

Slice 21 (4844816) deleted the pre-existing module-wide post-sema
hook mechanism entirely: every concern it addressed is covered by
`#[metaprog_handler]`, and keeping two parallel paths added cost
without benefit. Removed: `metaprog_post_sema_hooks` collection in
sema, validation, the `LProgram` field, and the driver-loop branch.

### 7. Convergence policy (revised from ADR 0001 §3)

```
for iter = 0..16:
  prog = sema_lower(asts, metaprog_mode=true, entry_idx)
  if prog.metaprog_targets.empty() && prog.metaprog_handlers.empty(): break
  jit-compile prog into meta_prog (process-symbols enabled)
  for (trigger, off) in prog.metaprog_targets:
    for handler in prog.metaprog_handlers[trigger]:
      handler(off)
  if not any_emitted_this_iter: break
```

Same shape as ADR 0001 §3, but discovery uses `metaprog_mode` so
`w.dbg(...)` doesn't fail in iter 0. The 16-iter cap remains a
hard error for pathological recursive emission.

## Consequences

- Three end-to-end derive tests cover the seam:
  - `derive_debug_e2e` (slice 19) — flat struct.
  - `derive_debug_generic` (slice 24a) — generic struct.
  - `derive_debug_enum` (slice 24b/c) — enum w/ mixed variants.
- `multi_derive_e2e` (slice 22) covers two distinct triggers on the
  same target, each routing to its own handler.
- Hook fns referenced from within metaprog stubs in libstdlib_rt
  must keep a weak C symbol so the regular link path doesn't
  unresolve. New host externs added to the metaprog API need both
  `meta_jit->define_symbol` (real) and a weak stub (linker-bait).
- The `is_metaprog_stub` flag is local to sema/main and never
  surfaces in MLIR/LLVM — the strip pass runs before mono/codegen
  ever sees those functions.
- 916/916 tests green at slice 25.

## Open questions

- Per-trigger ordering when one item carries multiple `#[derive_*]`
  attributes is currently source-textual (the order of `#[...]`
  lines on the item). Worth documenting in user-facing docs once
  the metaprog API stabilises.
- Convergence cap `=16` is arbitrary; configurable via `LOGOSC_META_MAX_ITER`
  is a possible future knob if real workloads need it.
- Whether handler-emitted code can itself carry `#[metaprog_handler]`
  declarations (handlers spawning handlers) is unspecified — the
  current driver registers handlers from `asts` once per iter, so
  a new handler emitted in iter K becomes active in iter K+1.
  Not exercised in tests.
