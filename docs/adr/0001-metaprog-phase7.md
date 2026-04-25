# ADR 0001 — Metaprog Phase 7: Hook Loop, AST Ownership, Source-Splice Seam

Status: Partially superseded by ADR 0002 (target-driven `#[metaprog_handler]`
+ pre-sema execution). Sections 1, 2, 3 (with revised termination), 5, 6
remain authoritative; section 4 (snapshot semantics) and references to
`#[metaprogram_post_sema]` are obsolete — see 0002.

Original date: 2026-04-25 (commits 3620659, 9684ad2, ead1df6, 09adaf4).

Retrospective notes for design choices made while landing the first
end-to-end Logos metaprograms (`#[metaprogram_post_sema]`).

## Context

Phase 7 lights up the seam between the host compiler and user-written
hooks running under JIT. Three intertwined questions had to be answered
before the seam could carry a real workload (slice 6 derive-style):

1. How does a hook *get* the user's AST?
2. How does a hook *emit* new code, and how does the driver converge?
3. What stack of stdlib types does the hook stand on?

## Decision

### 1. Two AST views: `HermesView<'a>` (borrow) and `OView` (RC-owning)

`HermesView<'a>` is a non-owning fat borrow `(base, size)` tied to the
hook frame's lifetime. Sound for the hook because the host's
`asts[g_user_root_idx]` outlives every hook invocation in a given iter.

`OView` (`std/compiler/metaprog/oview.logos`) is the ergonomic default:
host-side `logos_get_module_ast_oview` bumps the `MemHolder` refcount,
returns an opaque holder pointer; `OView::drop` calls
`logos_holder_release` which `unref()`s. The holder's internal layout
(C++ `std::atomic<int32_t>` + `Arena`) does not match
`std.hermes.mem_holder.MemHolder` POD — kept opaque on purpose.

**Rule of thumb:** hooks that stash AST AnyVals across iterations / into
module state need `OView`. Hooks that walk-and-emit in one pass can use
`HermesView<'a>`.

### 2. Source-level emit, not AST-graft

`logos_emit_source(*const u8) -> i32` parses a chunk of Logos text via
the runtime parser, appends the resulting holder to `g_asts`, and sets
`g_any_emitted = true`. Deduplicated through `g_emit_seen`
(`std::set<std::string>` of source strings already absorbed).

Chosen over direct AST grafting because:

- Dedup is trivial (string equality vs structural diff).
- The next sema iter sees emitted code as if it were just another input
  module — no special re-validation path.
- An always-emit hook still terminates: iter N parses+appends+flags;
  iter N+1 hits the dedup set and `any_emitted` stays false → break.

Cost: source text is reparsed once per unique emission. Acceptable for
Phase 7's workload sizes; revisit if hot.

### 3. Convergence policy

```
for iter = 0..:
  prog = sema_lower(asts)
  if no post-sema hooks: break
  jit-compile + run all hooks
  if not any_emitted: break        // primary exit
  if iter+1 >= 16: error            // pathological-case safety net
```

Early-exit on no-change is the primary termination. The 16-iter cap
fires only on pathological recursive emission (hooks generating hooks
generating hooks…) and is a hard error, not a silent truncation.

Open: when nested derive-style emission becomes real, the cap may need
to be configurable, and we may want per-hook fixpoint diagnostics
("hook X emitted N items in iter K").

### 4. Snapshot semantics between iters

Each iter re-runs `sema_lower` from scratch on the current `asts`
vector. Hooks always see a fully-typed AST that includes everything
emitted in *prior* iters. They never see a half-finished sema state from
the same iter (hooks for iter K cannot observe siblings from iter K).
This matches the user's "two-level metaprogramming" model in the master
plan: post-sema hooks always read consistent typed AST.

### 5. JIT capability surface

Metaprog JIT enables `DynamicLibrarySearchGenerator::GetForCurrentProcess`
(slice 5). Hooks can therefore call libc directly — `String`, `format`,
etc. work without per-symbol bindings.

The compiler *is* the trust root for `#[metaprogram_post_sema]` code:
this is post-sema, post-borrow-check, code the user opted into running
at compile time. Treat the JIT'd module the same way you treat the
build script.

### 6. AST-walk convenience: `HermesRead::tiny_map_get`

Slice 8 added `tiny_map_get(self, val, key: u8) -> AnyVal` as a default
on `HermesRead`. AST nodes are TinyObjectMaps, but `map_get` rejects the
TOM tag (it's the `ObjectMap` path). Without `tiny_map_get`, every hook
had to drop to:

```logos
let obj: *const Map<Bitmap, AnyVal> =
    ((base as i64) + (off as i64)) as *const Map<Bitmap, AnyVal>;
let v: AnyVal = unsafe { obj.get(base, key) };
```

Now: `let v: AnyVal = view.tiny_map_get(parent_val, key);`. Raw casts
remain only where the TOM pointer itself is needed (e.g. reading the
`schema_type_code` header field).

## Consequences

- Metaprog tests: `jit_metaprog_hook` (slices 3a/3b/3c) and
  `jit_metaprog_derive` (slice 6) prove the seam end-to-end. 909/909
  green at slice 8.
- `g_user_root_idx` (post-order load surprise from slice 3c) is the
  *single* point of truth for "which `asts[i]` is the user's entry
  file". All future host-side AST-introspection bindings must respect
  it; do not assume `asts[0]`.
- Adding new host-extern bindings is mechanical: `extern "C"` function
  + `meta_jit->define_symbol` in the loop body.
- The dedup-by-source-string emit policy makes it cheap to write
  always-emit hooks, but means semantically-equivalent emissions with
  different whitespace duplicate. If that matters, future work: dedup
  on canonical AST hash.

## Followups

- Slice 10: hooks-return-data API (typed builders instead of raw
  source strings).
- Snapshot-vs-mutating semantics for hook AST writes once we have a
  pre-sema hook that mutates in-place.
- Configurable iter cap + per-hook diagnostics if recursive emission
  becomes a real workload.
