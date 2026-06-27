# Multi-Arena IR — Architecture and Refactor Plan

**Status:** Design approved 2026-05-16. Phase 0 ready to start.
**Scope:** ~17 sessions across ~9 phases.
**Owner:** Compiler core.

This document is the canonical reference for the multi-arena IR refactor.
Each phase commits should cite this document and its phase number.

---

## 1. Motivation

Three independent pressures converge on the same architectural answer:

1. **Hard 4 GB arena limit.** AST/LIR is granular. Stdlib's mono output is
   already 22 MB (M4 step 1, [M4 round 21]). Trajectory: app codebases will
   hit 4 GB single-arena limits within a year of real use. Writ arena
   uses 32-bit offsets — physical limit, no growth path.

2. **Cold sema re-lowers stdlib every compile (~300 ms).** Current
   architecture: stdlib AST loaded, sema lowers it (every primitive type
   interned, every fn body lowered). Even with `binary_symbols` skip on
   codegen side, sema does the heavy work. M5 step 6 (LIR bundle cache,
   round 10) helps in-process across re-sema invocations, but cold logosc
   starts re-do it all.

3. **Compiler as data platform** (long-term vision). Future-state goal:
   compiler internal state is queryable, persistent, multi-threaded,
   distributable. LSP, refactoring, distributed builds, time-travel
   debugging — all become incremental additions on top.

A multi-arena IR — where the compiler's internal representation is
distributed across many bounded-size arenas with safe cross-arena
references — solves (1) directly, (2) as a side-effect of "stdlib lives in
its own loaded-once arena", and lays the foundation for (3).

## 2. Vision

```
Process memory:
                       ┌─────────────────────────────────────┐
                       │           ArenaPool                  │
                       │  arena_id → MemHolder*               │
                       │  + cached directory_offset           │
                       └────┬────────┬────────┬───────────────┘
                            │        │        │
                  ┌─────────┴──┐ ┌───┴────┐ ┌─┴─────────┐
                  │ coremeta   │ │ alloc  │ │  stdlib    │
                  │ (always)   │ │(--no-  │ │ (--no-std  │
                  │            │ │ alloc) │ │  excludes) │
                  └────────────┘ └────────┘ └────────────┘
                       ▲              ▲              ▲
                       │              │              │
                       └──────────────┴──────────────┘
                                      │
                              ┌───────┴──────┐
                              │ user arena   │
                              │ (this compile)│
                              └──────────────┘

Cross-arena ref: ExternalRef{arena_id (3B), obj_id (4B)} via ArenaPool dispatch.
Inside any arena: local arena_offset_t / RelativePtr as today.
```

**Hot path locality:** Within a single arena, references are local offsets
(existing Writ mechanism, sub-nanosecond pointer arithmetic). Cross-arena
references go through ArenaPool dispatch — three indirections, all
cache-warm. Locality invariant: local refs outnumber external refs by
~100×, so branch prediction stays on the local-fast-path.

**Append-only directories.** Each arena exposes a published object directory
(`obj_id → arena offset`). Once published, entries are immutable: an obj_id
slot is never reassigned, content never mutated. This is the contract that
makes cross-arena refs safe.

## 3. Core abstractions

### 3.1 ExternalRef (Writ type)

```
TypeTag::ExternalRef        — new tag
Layout, 8 bytes total, byte-aligned:
    tag:        1 byte    (TypeTag protocol)
    arena_id:   3 bytes   (16 M arenas)
    obj_id:     4 bytes   (4 G objects per arena)
```

**Resolve dispatch (three indirections, all cache-friendly):**

```cpp
inline TaggedRef resolve(ExternalRef ref) noexcept {
    auto& entry = pool_[ref.arena_id];                    // (1) pool index
    auto* dir   = entry.mem->base() + entry.dir_offset;   // (2) cached dir ptr
    auto target_av = dir->get(ref.obj_id);                // (3) directory index → AnyVal
    return {entry.mem, offset};
}
```

### 3.2 ObjectDirectory (per-arena)

The directory is an `ObjectArray<AnyVal>` hanging off `LirArenaRoot.DIRECTORY`.
Each entry is a typed pointer (or null) to a published object in the same arena.

```
LirArenaRoot.DIRECTORY → ObjectArray<AnyVal>
                         [ AnyVal{Ptr→obj_1}, AnyVal{Ptr→obj_2}, ..., AnyVal{Null}, ... ]
                                  ↑                                       ↑
                                  obj_id 1                                deprecated slot
```

**Why ObjectArray<AnyVal> and not flat `u32[]`:**

A flat `u32[count]` of raw arena offsets would break Writ
compactification — the compactifier sees raw integers, not pointers, so:
- Reachability tracing misses the targets → may discard them
- Relocation pass doesn't update raw offsets → dangling refs after compact

`ObjectArray<AnyVal>` gives compactifier compatibility out of the box
(Case B per the `[[writ-immutable-doc-exports-pattern]]` note):
- Compactifier traces through AnyVal pointers → keeps targets alive
- Relocation patches AnyVal offsets automatically
- Sparse directories supported via `AnyVal::null()` slots (useful for
  deprecation without shifting later obj_ids)

**Cost:** zero. `AnyVal` is 4 bytes — same as the flat `u32[]` alternative
would have been. We get compactifier compatibility free. For ~60-110 K
entries that's ~240-440 KB directory.

**Reserved obj_ids:** obj_id 0 = sentinel invalid, real entries start at 1.
Lookup: `directory[obj_id]` returns the AnyVal; consumer expects `is_pointer()`.

**No new TypeTag:** the directory is a normal `ObjectArray<AnyVal>`.
Semantics conveyed via position (LirArenaRoot.DIRECTORY field). If future
tooling needs to mark a directory specifically (validation, specialized
compactifier rules), we can wrap in a thin `TypeTag::ObjectDirectory`
without breaking existing readers.

### 3.3 LirArenaRoot (per-arena metadata anchor)

DocumentHeader (4 bytes, unchanged) points to LirArenaRoot at root_offset:

```
TypeTag::LirArenaRoot
TinyObjectMap {
    SCHEMA_VERSION : uint32_t,
    MODULE_NAME    : String,
    DEPS           : ObjectArray<String>,    // dep module names
    DIRECTORY      : arena_offset_t,         // → ObjectDirectory
}
```

LirArenaRoot is a normal application-level object. DocumentHeader stays
4 bytes (single `root_offset` field). Existing AST arenas — whose root is
the PROGRAM node — are unaffected; new LIR arenas put LirArenaRoot at
root. Loader knows which to expect by context (which file in the archive).

### 3.4 ArenaPool (process-global registry)

```cpp
class ArenaPool {
public:
    // Register a module's arena. Returns assigned arena_id (3-byte slot).
    // Asserts that all dep_names are already registered.
    ModuleHandle register_module(
        MemHolder* mem,
        std::string name,
        std::vector<std::string> dep_names);

    void unregister(arena_id_t aid);  // refcount decrement; bytes freed when refcount=0

    // Hot path — cache-hot index access.
    inline MemHolder*    get_unchecked(arena_id_t aid) noexcept;
    inline DirectoryRef  directory_of(arena_id_t aid) noexcept;

    // Cold paths.
    std::optional<ModuleHandle> find_by_name(std::string_view name);
};

struct ModuleHandle {
    arena_id_t           arena_id;
    std::string          name;
    std::vector<arena_id_t> depends_on;
};
```

Initial implementation: `InMemoryArenaPool`. Future swap to
`MemoriaArenaPool` (backed by `std.data.persistent` B+tree) is a drop-in
replacement at this interface — no consumer changes required. The
persistent-substrate path is **deliberately out of scope** for this
refactor; it falls out of the abstraction naturally when need arises.

### 3.5 Inline-encoded primitives

Primitive types (`i8..i64`, `u8..u64`, `f32/f64`, `bool`, `char`, `str`)
do **not** live in any arena. They are synthesized on-the-fly from a
small `(Kind, width)` descriptor:

```cpp
TypeRef TypeRef::primitive(Kind k, uint8_t width);
```

Rationale: primitives are referenced thousands of times per compile.
Treating them as cross-arena refs would dominate the resolve path and
violate the locality invariant. Inline encoding sidesteps interning
entirely — every TypeRef to `i32` produces the same logical type without
arena allocation.

## 4. Invariants

These hold across all phases. Violations are bugs.

| # | Invariant | Where enforced |
|---|-----------|----------------|
| 1  | `DocumentHeader` stays 4 bytes (single `root_offset`) | Writ core |
| 2  | Published `obj_id`s are append-only, never reused within an arena's lifetime | publish phase |
| 3  | Published object content is frozen (arena sealed after publish) | publish phase + `Arena::seal()` |
| 4  | User compile never writes to library arena (read-only after load) | sema + mono |
| 5  | Cross-arena refs use only `ExternalRef` via `ObjectDirectory` — no raw remote offsets | `ExternalRef` is the only cross-arena type |
| 6  | Internal refs (same-arena) use local offsets, never `ExternalRef` | reference encoding |
| 7  | Library publishes **all** externally-visible items (no pre-publish DCE) | emit_module |
| 8  | DCE runs in consumer codegen, not at publish time | mlir_gen |
| 9  | Library dependency DAG is strict; no cycles | loader + register_module |
| 10 | Primitives are inline-encoded; no arena entry, no obj_id | `TypeRef::primitive()` |
| 11 | Arena is sealed at end of publish phase | emit_module |
| 12 | Locality invariant: in any LIR body, local refs ≫ cross-arena refs (~100×) | design check at each phase |
| 13 | `obj_id 0` is sentinel "invalid"; real entries start at 1 | directory layout |
| 14 | Bootstrap compiler has hardcoded primitives; coremeta is built FROM compiler, not loaded INTO it | bootstrap step |

## 5. Library tiering

```
liblcoremeta.a       (always loaded; --no-std --no-alloc still loads this)
  Primitives:        i8..i64, u8..u64, f32/f64, bool, char, str
  Pointer/ref types: &T, &mut T, *const T, *mut T
  Composite shapes:  [T] (slice), [T; N] (array), tuples
  Lang items:        Drop, Sized, Copy, Send, Sync (marker traits)
  Foundational:      Option<T>, Result<T,E>
  Closure traits:    Fn, FnMut, FnOnce
  Iterator trait
  Atomics            (sync.atomic — primitive ops)

liblalloc.a          (--no-std loads this; --no-alloc excludes)
  Allocator trait + global allocator
  Box<T>             (lang item, impl here)
  Vec<T>, VecDeque
  String
  Rc<T>, Arc<T>
  Cow<T>
  (BTreeMap/HashMap — debatable, lean toward stdlib)

liblstdlib.a         (default; --no-std excludes)
  IO subsystem, FS, network
  Fibers + reactor
  std.data.persistent (Memoria)
  Time, Process, Environment
  Hash-based collections (if not in alloc)
  Threading utilities beyond raw atomics
```

**Dependency DAG (strict):**

```
user code  →  stdlib  →  alloc  →  coremeta
                 ↓         ↓
                 └─────────┴─→  coremeta (always)
```

**Flag combinations:**

| Flags | Loaded |
|---|---|
| (default) | coremeta + alloc + stdlib |
| `--no-std` | coremeta + alloc |
| `--no-alloc` (implies `--no-std`) | coremeta only |

Compiler rejects `--no-alloc` without `--no-std` (stdlib needs alloc).

**Note:** Source-tree reorganization (moving `Vec`/`String`/`Box`/etc. from
`stdlib/` → `alloc/`) is Phase 9 — a mechanical refactor done after
multi-arena infrastructure proves out. Through Phase 8 the existing
monolithic `liblstdlib.a` is treated as a single "library arena".

## 6. Data flow

### 6.1 Library build (emit_module eager mode)

```
1. sema_lower                  (existing)
2. mono_pass                   (existing — produces LIR mirrors in arena)
3. borrow_check                (existing)
4. PUBLISH PHASE (new):
     - walk all externally-visible items
     - assign sequential obj_ids (1, 2, 3, ...)
     - build ObjectDirectory (flat u32[count])
     - construct LirArenaRoot (SCHEMA_VERSION, MODULE_NAME, DEPS, DIRECTORY)
     - set DocumentHeader.root_offset → LirArenaRoot
5. Arena::seal()
6. write to .writ0 (in .a archive)
```

**Publish policy** — what gets an obj_id:
- All `LogosType*` (every type interned in the arena)
- All named items (`LStructDef`, `LEnumDef`, `LFunction`, `LTraitDef`, `LImplBlock`)
- All body blocks (`LBlock` at `LFunction::body`) — for cross-arena body reads
- All consts, type aliases

**NOT published** (internal-only):
- Anonymous LExpr sub-nodes (reachable via parent's local offset)
- Internal LStmt/LBlock nodes inside fn body
- Helper TinyObjectMaps in payload

Estimate: stdlib ≈ 60-110 K obj_ids × 4 bytes per AnyVal ≈ 240-440 KB
directory. Negligible vs 22 MB arena.

### 6.2 User compile (consumer load)

```
1. Loader resolves manifest deps in topological order:
     load coremeta.a   → register_module("coremeta", deps=[])
     load alloc.a      → register_module("alloc",    deps=["coremeta"])
     load stdlib.a     → register_module("stdlib",   deps=["alloc", "coremeta"])
     load user libs    → register_module("mylib",    deps=[...])

2. For each loaded module:
     from_bytes_copy(lir_blob)  → fresh Writ doc
     walk DocumentHeader.root → LirArenaRoot
     read DIRECTORY field → cache directory_offset in pool
     read name_to_obj_id map from exports trailer (Phase 3 extends M3 trailer)

3. Sema:
     - User AST: lower as today (writes into user arena)
     - from_binary AST: skip lower_module_items; build skeleton
         LStructDef/LFunction with body_external_ref = ExternalRef{lib_aid, obj_id}

4. Mono:
     - Read template bodies via ExternalRef (lir_view crosses arena boundary)
     - Substituted instances written into user arena
     - Cross-arena type refs preserved where unchanged

5. Borrow check + Codegen as today (binary_skip stays for stdlib symbols)
```

### 6.3 Resolve hot path (cross-arena field access)

```cpp
// Inside lir_view, traversing a child field:
auto child_av = parent.get(FIELD_TAG);
if (child_av.is_external_ref()) [[unlikely]] {
    // Cold path: resolve via ArenaPool.
    auto ref       = child_av.as_external_ref();    // unpack 8 bytes
    auto& entry    = pool[ref.arena_id];            // hot array index
    auto* dir_arr  = entry.directory_ptr;           // cached at register time
    auto target_av = dir_arr->get(ref.obj_id);      // ObjectArray<AnyVal>::get
    return ChildRef{entry.mem, target_av.to_offset()};
}
// Hot path: local arena, single offset arithmetic.
return ChildRef{this_mem, child_av.to_offset()};
```

Branch hint `[[unlikely]]` on `is_external_ref()` keeps the CPU on the
local path when locality invariant holds.

## 7. Backward compatibility

**Preserved:**
- `DocumentHeader` (4 bytes, single `root_offset`) — unchanged
- AST `.writ0` format — unchanged (no LirArenaRoot, no directory)
- `binary_codec` for AST documents — unchanged
- M3-era `.writ0` v3 archives — readable (no LIR blob OR LIR blob without
  LirArenaRoot, treated as legacy through Phase 8)
- Existing single-arena Writ docs — read with empty pool, no External refs
- Bootstrap compiler — operates on hardcoded primitives, no coremeta needed

**Changes:**
- LIR mirror blob format (Phase 3) — gains LirArenaRoot at root_offset
- `TypeRef` internals (Phase 2) — multi-arena aware (arena_id field)
- `lir_view` dispatchers (Phase 2) — handle ExternalRef
- Sema/Mono internals (Phase 4/5) — read across arenas

**No silent format breakage:** every Phase commit that touches `.writ0`
format documents what new readers tolerate, what old readers do, and
which environment-variable flags preserve legacy paths during rollout.

## 8. Bootstrap

The bootstrap compiler has primitive types hardcoded (`TypeRef::primitive`).
It does not load coremeta to compile coremeta.

**Build chain:**

```
Stage 1 (existing):
    bootstrap logosc compiles stdlib/ → liblstdlib.a (monolithic)

Stage 2 (Phase 3+):
    bootstrap logosc compiles stdlib/ → liblstdlib.a with LirArenaRoot
                                          + ObjectDirectory + LIR blob

Stage 3 (Phase 9, source reorg):
    bootstrap logosc compiles coremeta/ → liblcoremeta.a
    bootstrap logosc compiles alloc/    → liblalloc.a (loads coremeta)
    bootstrap logosc compiles stdlib/   → liblstdlib.a (loads alloc + coremeta)
```

No circular dependency: `emit_module` generates arena bytes from source
files; it does not need pre-existing tiers loaded.

## 9. Phased plan

Each phase has: explicit deliverables, exit criteria, and validation steps.
Sessions are estimates — sequencing matters more than precise count.

### Phase 0 — ArenaPool API + skeleton (≈ 0.5 session)

**Deliverables:**
- `include/logos/writ/arena_pool.hpp` — `ArenaPool` interface + `ModuleHandle`
- `src/writ/arena_pool.cpp` — `InMemoryArenaPool` implementation
- Unit test: register 2 dummy MemHolders, lookup by name + by id, refcount, drop
- No consumer wiring; no existing TU changes

**Exit criteria:**
- New unit test passes
- Full ctest 3192/3192 unchanged

### Phase 1 — Writ foundation (≈ 2 sessions)

**1.A — Type tags + structs:**
- `TypeTag::ExternalRef`, `::LirArenaRoot` added to enum
  (no new TypeTag for ObjectDirectory — uses `ObjectArray<AnyVal>` directly)
- C++ struct definitions with alignment + size asserts
- View wrappers (`ExternalRefView`, `LirArenaRootView`)
- `TypeRef::primitive(Kind, width)` synthesis helper

**1.B — Encoding + integration:**
- `binary_codec` handles new tags (ExternalRef, LirArenaRoot)
- Allocation/write helpers: `write_external_ref(arena, aid, oid)`,
  `make_directory(arena, initial_capacity) → ObjectArray<AnyVal>`,
  `directory_append(dir, target_av) → obj_id`
- `make_doc` and `from_bytes_copy` register with `ArenaPool` automatically
- Publish helpers: `arena_publish(arena, obj_av) → obj_id`,
  `arena_finalize(arena, name, deps) → LirArenaRoot offset`

**Exit criteria:**
- Synthetic 2-arena test: build A, publish 3 objects, register; build B
  with `ExternalRef`s into A; traverse B's structure → values from A
  resolve correctly via pool dispatch
- ctest 3192/3192

### Phase 2 — View layer cross-arena (≈ 2 sessions)

**2.A — TypeRef multi-arena:**
- `TypeRef` shape: `(pool*, arena_id, offset)` (was `(Arena*, offset, pool_impl*)`)
- All TypeRef constructors updated
- Audit existing single-arena users; most adapt with `arena_id = "this"`
- Compiler still effectively single-arena; just plumbing in place

**2.B — lir_view dispatchers:**
- `ExprRef`, `StmtRef`, `BlockRef`, `PatRef`, `WritValRef` gain
  cross-arena traversal: when child field is `ExternalRef`, resolve via
  pool, continue in target arena
- Manual cross-arena test: walk a synthetic mixed-arena LIR via existing
  view code, assert values

**Exit criteria:**
- ctest 3192/3192 (still single-arena in practice; multi-arena infra layered)
- Manual cross-arena traversal test passes

### Phase 3 — emit_module publish + load (≈ 1.5 sessions)

**Deliverables:**
- emit_module gains publish phase: walk → assign obj_ids → build directory
  → build LirArenaRoot → seal
- `.writ0` lir_blob now wraps the LirArenaRoot (the blob's root_offset
  points to it)
- module_loader walks LirArenaRoot, registers arena with pool, exposes
  `name → obj_id` map
- `LProgram.attached_modules` field
- M3-era archives still readable (no LirArenaRoot → legacy path)
- Exports trailer extended (M3 trailer_version → 3): each template entry
  carries `obj_id` field

**Exit criteria:**
- Stdlib emit produces directory + LirArenaRoot
- User-side load registers stdlib in pool, can query obj_ids by name
- No consumer of obj_ids yet — just plumbing
- ctest 3192/3192

### Phase 4 — Sema register-pre-lowered (≈ 3 sessions)

**4.A — Skeleton builder + opt-in flag:**
- Sema: for `from_binary` AST items, skip `lower_module_items`
- Build skeleton `LFunction`/`LStructDef` with `body_external_ref` field
- Gated behind `LOGOS_SEMA_USE_BLOB=1`; default OFF
- Validate: with flag on, skeletons appear in `prog`, no body lowered

**4.B — Type resolution cross-arena:**
- Type interning skip for stdlib types (use `ExternalRef`)
- Inline-encoded primitives bypass interning entirely
- Skeleton param/ret types resolve through external refs

**4.C — Wire mono + codegen, flip default:**
- Mono accepts external body refs
- Codegen path: skeleton fns treated as `binary_skip` semantics
- Flip default to `LOGOS_SEMA_USE_BLOB=1`; preserve `LOGOS_SEMA_USE_BLOB=0`
  for debug fallback

**Exit criteria:**
- Cold sema time reduced ≥ 200 ms on iter_cycle.logos (target: 250 ms)
- `LOGOS_SEMA_USE_BLOB=0` fallback still works (regression-safety)
- ctest 3192/3192 with both `=0` and `=1` modes

### Phase 5 — Mono cross-arena clone (≈ 3 sessions)

**5.A — clone_fn source arena:**
- `clone_fn` accepts source arena parameter (defaults to `in_`'s)
- Reads body via `lir_view` through external arena
- Substituted output written to user arena

**5.B — Type substitution cross-arena:**
- Substituted types alloc in user arena
- Cross-arena type refs preserved where unchanged
- Type interning lookup: user arena first, then libraries via `ExternalRef`

**5.C — `drain_method_worklist` + edge cases:**
- Lazy method instantiation handles external body refs
- Closures, writ literals (`@{...}`), metacall sites — all handle
  cross-arena traversal

**Exit criteria:**
- User code with `Vec<MyType>::push(x)` compiles, runs, correct
- Mono time measurement (should be similar or better than baseline)
- ctest 3192/3192

### Phase 6 — Hybrid lazy mode (≈ 1.5 sessions)

**Deliverables:**
- Manifest gains `lowering: eager | lazy` field (default: `lazy`)
- `emit_module` lazy mode: ships ASTs only, no LIR blob, no directory
- User-side: for lazy-module item references, lower the item locally
  on first use; cache `(module_id, item_name) → user-arena LIR offset`
- Stdlib manifest switches to `eager`

**Exit criteria:**
- 3rd-party crate in lazy mode compiles
- Cache hit on repeated reference
- ctest 3192/3192

### Phase 7 — Multi-arena per module (≈ 1.5 sessions)

**Deliverables:**
- `emit_module` monitors arena size; rolls to new arena at threshold
  (default: 3.5 GB headroom under 4 GB hard limit)
- Cross-arena refs between rolling arenas (same module) handled
- Loader iterates all arenas of a module, registers each
- Module manifest carries `arena_ids` list

**Exit criteria:**
- Synthetic test with module exceeding 4 GB total emits + loads + queries
  correctly across rolled arenas
- ctest 3192/3192

### Phase 8 — Measure + close baseline (≈ 1 session)

**Deliverables:**
- Profile cold sema on iter_cycle, m5_iter, several stdlib-heavy tests
  vs pre-refactor baseline
- Memory profile: `ArenaPool` overhead measurement
- Update `invariants.md` with the 14 invariants from §4
- Update `docs/internals/architecture.md` with multi-arena section
- Update relevant memory notes

**Exit criteria:**
- Demonstrable cold-sema win (target: ≥ 200 ms on iter_cycle)
- No new ctest failures
- Documentation up-to-date

### Phase 9 — Three-tier source reorg (≈ 2 sessions)

**Deliverables:**
- Move source files: `stdlib/...` → `coremeta/...` / `alloc/...` / `stdlib/...`
- Three `emit_module` invocations in build chain
- Manifests with `depends_on` arrays
- Flag support: `--no-std`, `--no-alloc` (with mutual exclusion check)
- Build chain wiring

**Exit criteria:**
- Three-tier build works (coremeta → alloc → stdlib → user)
- `--no-alloc` smoke test compiles a minimal program (no Box, no Vec, no IO)
- `--no-std` smoke test compiles a `Box`-using program (no IO)
- ctest 3192/3192

## 10. Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `TypeRef` refactor breaks many consumers | High | High | Phase 2A audit before flip; default `arena_id = "this"` keeps single-arena working |
| Cross-arena perf (3 indirections) too high | Med | Med | Locality invariant + cached directory pointer at register time; benchmark in Phase 5 |
| Skeleton `LFunction` missing fields | Med | Med | Q4 settled; iterate in Phase 4 if gaps found |
| `.a` archive format breakage | Low | High | Maintain backward compat: M3-era v3 archives still readable through Phase 8 |
| Bootstrap circular (coremeta needed to compile coremeta?) | Low | High | Hardcoded primitives in bootstrap compiler; coremeta source files compile without pre-loaded tier |
| Multi-arena per module emerges with cycles | Med | Med | Phase 7 emit logic ensures DAG; failing cycle = bug |
| `ArenaPool` memory grows unbounded | Low | Med | Defer concurrent-compile concerns to future (Memoria substrate); single-process is fine |
| Phase 4 regression: `LOGOS_SEMA_USE_BLOB=0` rots | Med | Med | Run ctest with both modes in Phase 4 exit criterion |
| Locality invariant violated (too many cross-arena refs in hot path) | Med | High | Per-phase audit: count cross-arena ref ratio in sample bodies before flipping default |

## 11. Open questions (post-Phase 8)

These are deferred — not blockers for Phase 0-8.

- **`obj_id` stability across rebuilds.** Currently sequential. Could move
  to mangled-symbol-keyed for cross-rebuild stability (enables incremental
  compilation). Phase 6/7 territory.
- **User-arena directory.** Does the user arena need its own directory
  (for IDE / incremental builds)? Probably yes, but not urgent.
- **Concurrent `ArenaPool` access.** Future Memoria substrate makes this
  natural. Single-process serial is fine through Phase 8.
- **Memoria substrate timing.** When does `InMemoryArenaPool` get swapped
  for `MemoriaArenaPool`? Probably driven by need (compilation server,
  distributed builds) rather than schedule.

## 12. Glossary

| Term | Meaning |
|------|---------|
| arena | A contiguous Writ-managed byte segment, capped at 4 GB. Holds typed objects with internal `RelativePtr` refs. |
| arena_id | 3-byte (24-bit) process-local identifier assigned by ArenaPool. |
| obj_id | 4-byte (32-bit) per-arena published-item identifier. Stable for arena lifetime. |
| `ArenaPool` | Process-global registry mapping arena_id → MemHolder*. |
| `ExternalRef` | 8-byte typed object encoding a cross-arena reference `{tag, arena_id, obj_id}`. |
| `ObjectDirectory` | Per-arena `ObjectArray<AnyVal>` hanging off `LirArenaRoot.DIRECTORY`. obj_id (array index) → published-object pointer. Compactifier-compatible. |
| `LirArenaRoot` | Per-arena metadata anchor (SCHEMA_VERSION, MODULE_NAME, DEPS, DIRECTORY), pointed-to by DocumentHeader.root_offset. |
| publish | Act of assigning an obj_id to an externally-visible object and registering it in the directory. |
| inline-encoded primitive | Type whose identity is derived from (Kind, width), with no arena entry — used for `i32`, `bool`, etc. |
| skeleton item | `LFunction`/`LStructDef` built by sema for from_binary AST with body reachable via `body_external_ref` instead of locally-lowered. |
| eager-mode module | `emit_module` ships AST + LIR blob + directory. Loaded once, used by many compiles without re-lowering. |
| lazy-mode module | `emit_module` ships AST only. Consumer lowers items on first reference, caches in user arena. |

## 13. References

- M3 (rounds 16-20) — names catalog in `.writ0` exports trailer
- M4 step 1 (round 21) — LIR blob shipped (basis for Phase 3)
- M5 step 6 (round 10) — in-process LIR bundle cache
- `docs/internals/big-memoria-architecture.md` — future substrate
- `docs/internals/writ-runtime.md` — Writ type system
- `[[writ-immutable-doc-exports-pattern]]` (memory) — Case B name-keyed exports
- `[[ref-subsystem-persistent]]` (memory) — Memoria infrastructure (future substrate)
