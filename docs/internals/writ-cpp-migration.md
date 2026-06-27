# Writ C++ Migration — Working Document

> **Audience: a future focused session (likely me) continuing this migration.**
> This is the single self-contained entry point. Read §1 for status, §5 before
> writing any code (gotchas), §6 for the next task. Companion memory note:
> `project_writ2_cpp_migration` (terser). Design doc: `writ-design.md`.

---

## 1. The plan & current status

Migrate the Logos compiler (`logosc`) and the Logos stdlib off **legacy** onto
**Writ**. Seven steps:

1. **Audit legacy C++ usage in the compiler.** ✅ DONE (see §6.2 for the findings).
2. **Build the C++ Writ library (`logos_writ2`).** ✅ **DONE** — data model +
   serialization. Commits `e718990e`..`905379c8`. 7 exercisers, all valgrind-clean.
   Remaining sub-parts (separable, not blocking): multi-arena, path/template — §6.
3. **Cut `logosc` over to Writ** (the deep one). ⬜ NEXT. Multi-arena (§6.1) now
   ported. Scope in §6.2.

   **schema_type_code is now a first-class TinyObjectMap field (both impls).** Done
   2026-06-10. The C++ `TinyObjectMap` and the Logos `HMap<Wu6,HAny>` both carry a
   `schema_type_code : u64` (the node-class discriminator: LirArenaRoot 5002,
   ImportTable 5003, metaprog ExprBlob roots). Layout is now **24 bytes**, byte-shared:
   `{ header:u64, schema_type_code:u64, data: self-rel ptr }` — the `#[zoned2]` pointer
   stays LAST (the Logos zoned-layout convention; putting a plain field after it is
   what existing specs avoid). Round-tripped through `clone` + `binary_codec`; text
   (`stringify`) drops it (JSON has no schema slot). `code_of(node)` reads the AST
   `CODE` *field*, NOT schema_type_code — they are independent.
4. **Port the embedded Writ** (Logos `stdlib/lang/writ` + `stdlib/mem/writ`) to
   Writ. ⬜ (Logos-side; the `lang/writ` + `mem/writ` stdlib already exists
   and is the byte-layout spec — see `project_writ2_port_complete`.)
5. **Port the legacy infra `persistent` needs** (`std.data.persistent`: fabric
   `WritStatic`, `AnyVal`, `View`) to Writ. ⬜
6. **Remove legacy** from Logos + C++. ⬜
7. **Celebrate.** ⬜

---

## 2. The `logos_writ2` library (DONE) — file map

Headers `include/logos/writ/`, sources + tests `src/writ/`. CMake target
`logos_writ2` (registered in top `CMakeLists.txt` after `src/writ`). Namespace
`logos::writ`.

| File | What |
|---|---|
| `relative_ptr.hpp` | `RelativePtr<T>` — self-relative i64; `get()=(&this)+off`; copy/move RE-ANCHOR |
| `any_val.hpp` | `AnyVal` — 8B niche, byte-identical to Logos `HAny` (see §3) |
| `config.hpp` | `ErrCode`, `arena_offset_t` (serialization only) |
| `type_tag.hpp` | in-band varint type tag (≤222→1B at obj[-1]; >222→header+LE) |
| `type_codes.hpp` | `tc::*` — the shared wire codes (= Logos stdlib W_*/HA_*/HT_*) |
| `varint.hpp`, `fnv_hash.hpp` | ported verbatim from legacy (byte-identical to Logos) |
| `arena.{hpp,cpp}` | `Arena` (MultiChunk never-move default; GrowableSingleChunk; `from_bytes`) |
| `arena_string.hpp` | `ArenaString` = Logos `HString` (`[vlen][utf8]`, tag 130) |
| `object_array.hpp` | `ObjectArray` = `HArray<HAny>` (24B) |
| `typed_array.hpp` | `TypedArray<T>` = `HArray<T>` (24B, plain elements) |
| `tiny_object_map.hpp` | `TinyObjectMap` = `HMap<Wu6,HAny>` (16B, bitmap+popcount) |
| `object_map.hpp` | `ObjectMap` = `HMap<HString,HAny>` (24B, open-addr+rehash, `for_each`) |
| `map.hpp` | `TypedMap<K>` = `HMap<K,HAny>` (32B, dense int, `for_each`) |
| `compound_types.hpp` | `Decimal`(16B) / `TypedValue`(24B) / `Parameter`(16B) |
| `mem_holder.hpp` | `MemHolder` — refcounted arena owner (Rc<dyn Resident>); `make`/`from_bytes` |
| `view.hpp` | OWNING `View<Obj>` + String/Array/TinyMap/Map views + `as_*` navigation |
| `document.hpp` | `DocumentHeader{AnyVal root}`@offset0 + `WritCtr` owning handle |
| `clone.{hpp,cpp}` | `clone()` + `compactify()` (one deep_copy + dedup; all types) |
| `binary_codec.{hpp,cpp}` | portable tree codec `binary_encode`/`binary_decode` |
| `stringify.{hpp,cpp}` | doc → canonical JSON text (sorted map keys) |
| `text_parser.{hpp,cpp}` | JSON text → fresh doc |
| `arena_pool.{hpp,cpp}` | `arena_id_t`, `ArenaPool`/`InMemoryArenaPool`, `global_arena_pool`, import-table resolution |
| `external_ref.{hpp,cpp}` | `ExternalRef` = **AnyVal Pod niche** (arena_id 24 + obj_id 32 = i56); encode/decode/detect + `resolve_external_ref[_local]` |
| `lir_arena_root.hpp` | `LirArenaRootView` + schema (codes 0..4, SCHEMA_CODE 5002) over a schema-tagged TinyObjectMap |
| `import_table.{hpp,cpp}` | `ImportEntry` + `build_/read_import_table_blob` (compacted single-segment) |
| `arena_publish.{hpp,cpp}` | `ArenaPublishBuilder` + `lir_arena_root_begin/_finalize`, `arena_publish[_named]`, `register_lir_arena` |

---

## 3. Core design (read before extending)

**The two changes from legacy (everything else is a port).**

1. **`RelativePtr` is SELF-relative** (`int64_t off`; `get() = (uint8_t*)this + off`).
   No base threaded anywhere. Copy/move RE-ANCHOR (recompute off vs the new address)
   — that is what lets a relptr (and an `AnyVal` Ref) ride an ordinary value-copy.
   Ported from Memoria `arena::RelativePtr`. the legacy was a base-relative `u32`.

2. **The arena is multi-segment, never-move** (MultiChunk: grow = append a chunk,
   nothing relocates). With self-relative pointers, cross-chunk refs just work. (The
   Logos legacy arena already had MultiChunk; only the pointer model changed.)

**The byte-layout invariant (do not violate).** Writ/C++ and Writ/Logos share
ONE memory layout, byte-for-byte (same wire/disk format; both read the same bytes).
**The Logos writ stdlib (`stdlib/lang/writ/*.logos`) is the layout spec; C++
mirrors it.** Some Logos layouts DIFFER from legacy — always match Logos:
- `AnyVal`/`HAny` = **8 bytes** (not the legacy 4): `word==0` null; `&1==1` Pod
  `(v<<8)|(code<<1)|1` (code bits[7:1], i56 value bits[63:8], position-independent);
  `&1==0` Ref = self-relative delta (value-form = absolute pointer).
- `TinyObjectMap` = `{header,data-ptr}` (NOT inline values + schema_code); header
  bits **cap[52:57], size[58:63]** (SWAPPED vs legacy).
- `MapEntry` = `{key:AnyVal, val:AnyVal}` = 16B (NOT the legacy 8B).
- String tag = **130** (Logos `W_STRING`), not the legacy `28`. (Flagged: if the
  canonical Writ string code should be 28, change Logos first, then `type_codes.hpp`
  in lockstep.)

**Owning views (Victor's rule).** In C++ the views are OWNING — they carry a +1
`MemHolder` ref. Without a borrow checker, C++ can't prove the holder outlives a view,
so the view must keep it alive. This collapses the legacy non-owning-view + `Own<>`
split into one view: `View<Obj> = {MemHolder*(+1), resolved absolute Obj*}`; copy +1,
move transfer, destroy -1; `as_string/as_array/...` make owning CHILD views sharing
the holder.

**Re-anchoring makes container code clean.** Because `AnyVal` copy/assign re-anchors:
storing into a slot (`slot = v`) LOWERS (absolute→self-rel), reading by value
(`AnyVal x = slot`) MATERIALIZES — no explicit bridge calls. **But** buffer growth /
shift / rehash must copy AnyVals **element-by-element** (each re-anchors); a raw
`memcpy` would leave every Ref pointing at the old buffer. Plain-T buffers
(TypedArray, TypedMap keys) CAN `memcpy`.

**Clone = compaction = ONE op** (`deep_copy_object`/`deep_copy_anyval`, `clone.cpp`):
recursive copy from root into a fresh holder; `DeepCopyState` dedup map (src-ptr →
dst-ptr) breaks cycles + shared subgraphs (each src object copied once); copying only
reachable objects drops dead space. `compactify()` copies into a pre-sized
GROWABLE_SINGLE_CHUNK (2× source used + slack) so NO realloc happens → a rigid,
relocatable single-segment blob → dump `blob_data()/blob_size()`, reload via
`WritCtr::from_bytes` (rigid relocation: self-rel deltas valid at any base). This is
the zero-copy serialization path; `binary_codec` is the portable (tree) path.

---

## 4. Conformance harness

`ctest -R writ2_exerciser` (label `writ;cpp`). 8 tests, each returns the first
failing check code; run each under valgrind. Add one per new layer.
`smoke` (foundation) · `containers` · `views` · `clone` · `document` (compactify+blob
reload) · `codec` · `text` · `multi_arena` (ExternalRef niche + pool + publish/resolve
+ import table).

---

## 5. Patterns & gotchas (learned the hard way)

- **`LOGOS_TRY_VOID(x)` inside a braceless `if` breaks scope** (the rv var). ALWAYS
  brace: `if (cond) { LOGOS_TRY_VOID(...); }`.
- **Match Logos layouts, not legacy** (see §3 — tinymap, MapEntry, AnyVal width,
  string tag all differ).
- **AnyVal buffers: element-by-element copy on grow/shift/rehash** (re-anchor); never
  `memcpy`. Plain-T buffers may `memcpy`.
- **`RelativePtr::get() const` returns mutable `T*`** (Memoria convention; the relptr's
  constness ≠ the pointee's).
- **Hash maps are not canonical**: `ObjectMap` order depends on cap + insertion
  (linear probing). So binary re-encode bytes / stringify aren't byte-stable unless you
  sort (stringify sorts keys → canonical). The AST uses ordered TinyObjectMap/arrays →
  stable.
- **`#[zoned2]` structs keep the self-relative pointer LAST** — adding `schema_type_code`
  to `HMap<Wu6,HAny>` only worked as `{header, schema, data}` (not `{header, data, schema}`):
  a plain field trailing the `*zoned` pointer breaks the Logos zoned layout. Mirror the
  same order in the C++ TOM (its `RelativePtr` is self-anchored so it works at any offset,
  but the byte layout must match Logos).
- **ExternalRef is a Pod niche, not a tagged object** (writ change). It fits the 8B
  AnyVal Pod (arena_id 24 + obj_id 32 = i56), so NO allocation; and clone/compactify copy
  Pods VERBATIM — exactly right for a cross-arena id (a logical reference must not be
  followed/rewritten by deep-copy). `is_external_ref_av` = `is_pod && pod_code==110`.
- **`compactify` MUST NOT realloc** — the never-move container methods hold `this`
  across their own allocations; a realloc dangles it. The pre-size (2×) guarantees no
  realloc; the result is asserted to be one chunk. If you ever allow realloc, you must
  use the AddrResolver pattern (§6.4) AND make the containers resolver-safe.

---

## 6. Remaining work (prioritized)

### 6.1 Multi-arena — ✅ DONE (2026-06-10)

Ported onto `logos_writ2` (file map in §2): `arena_pool.{hpp,cpp}`,
`external_ref.{hpp,cpp}`, `lir_arena_root.hpp`, `import_table.{hpp,cpp}`,
`arena_publish.{hpp,cpp}`, exercised by `exerciser_multi_arena.cpp` (valgrind-clean).
The API + invariants match legacy so logosc's call-sites move with a `writ::` →
`writ::` rename. Two writ-specific changes:
- **`ExternalRef` = AnyVal Pod niche** (NOT a tagged 8B object — see §5). arena_id 24 +
  obj_id 32 = i56, inline, no allocation; clone copies it verbatim (correct for a
  cross-arena id). `resolve_external_ref[_local]` walks pool → LirArenaRoot DIRECTORY.
- **No `base` threading.** A registered module's root is `doc_header(mem)->root` (a
  self-relative AnyVal that resolves in place); LirArenaRoot / ImportTable are
  discriminated by the now-first-class `schema_type_code` (5002 / 5003).
- Watch: with self-relative refs, an `ExternalRef` is the ONLY legal cross-arena
  pointer; intra-arena refs stay self-relative.

### 6.2 Step 3 — cut `logosc` over to Writ (the deep one)

**Audit findings (step 1).** The compiler is woven into legacy at three layers:
1. **AST = 100% a Writ document** — a tree of `TinyObjectMap` nodes keyed by schema
   codes. Sema walks it via helpers in `sema_impl.hpp`: `code_of(node)`,
   `str_of(av)`, `map_of(av)→TinyMapView`, `arr_of(av)→ArrayView`. Keys are
   `NamedCode<uint8_t>` in `include/logos/compiler/ast.hpp` (`la::CODE`, `la::NAME`…).
   The parser (peg_gen output, `logos_parser.hpp`) builds the AST into a `writ::Arena`.
2. **LIR mirror** — the C++ LIR (`LExpr`/`LStmt` variants) is ALSO mirrored into a
   Writ arena (`lir_mirror.cpp`, `mirror_offset_`, `lir_view::ExprRef/StmtRef/...`,
   `lir_schema.hpp`). Mono clones generic stdlib template bodies by walking the mirror.
3. **Module format `.writ0`** — `emit_module.cpp`: AST via `writ::binary_encode`;
   LIR template bodies published via `arena_publish_named` into a `LirArenaRoot`
   EXPORTS dir, then the arena is `clone()`d (compacted) and the head-chunk bytes
   dumped as the LIR blob. Loaded via `from_bytes_copy` + the global ArenaPool.
   Load-bearing Writ APIs: `arena_offset_t`(276), `TinyMapView`(263), `AnyVal`(178),
   `MemHolder`(33), `binary_codec`, `clone`, `arena_publish_named`, `schema`.

**DECIDED (Victor 2026-06-10): NATIVE rename, no base-model leak.** Rename
`writ::` → `writ::` across the compiler and adapt each site to the native
self-relative API; do NOT reintroduce a base/offset facade. End-state is clean
writ. Incremental by translation unit, gated by the full build + test suite.
- **Phase A — DONE (`c9980a59`).** Additive writ API so the rename is near-mechanical
  for value/discriminant sites WITHOUT a base model: AnyVal `is_pointer/is_value`
  (= `is_ref/is_pod`), `as_value<T>`, `value_type_hash`, `from_value<T>(v,code)`. The
  genuinely base-relative sites (`to_offset`/`as_ptr(base)`/`from_offset`) get NO alias —
  they adapt to `resolve()`/`set_ref()`.
- **Per-site mapping:** `av.to_offset()`+`holder` → pass `av` to `as_tinymap/as_array/
  as_string(av, holder)`; `av.as_ptr<const T>(base)` → `reinterpret_cast<const T*>(av.resolve())`;
  `from_offset(off)`/`set_pointer(p,base)` → `set_ref(p)`; `make_doc(n)` → `WritCtr::make(n)`
  (now `expected`); `doc.root_object().as_tiny_map()` → `as_tinymap(doc.root(), doc.holder())`;
  `binary_encode/clone/from_bytes_copy` → writ `binary_encode`/`clone`/`compactify`/
  `WritCtr::from_bytes`; multi-arena names already match (§6.1).
- **Phase B — AST producer + readers (the big-bang; do atomically).** The AST is built
  by the GENERATED `build/src/compiler/logos_parser.hpp/.cpp` (~55K lines; every rule
  returns `writ::AnyVal`), emitted by `tools/peg_gen/src/codegen.cpp` — so the producer
  flip is REGENERATION via codegen edits, not hand-editing the parser. All AST readers
  (`sema_impl.hpp` `code_of/str_of/map_of/arr_of`, `sema_expr/stmt/decl/collect`,
  `module_loader` AST reads) flip together (shared AST arena → cannot half-migrate).
  The writ producer surface is READY (`make_doc`/`WritCtr::make_tiny_map/make_array/
  make_string/make_object_map/seal`, `schema_codes.hpp`, AnyVal `from_value`/`set_ref`;
  exerciser_producer proves the exact build+read path) — Phase A + producer commits landed.

  **Producer codegen recipe** (the ~8 emission points in `codegen.cpp`, all currently
  emitting legacy `WritAccess`/base/offset → flip to writ-native):
  | codegen emits now (legacy) | flip to (writ-native) |
  |---|---|
  | `logos::writ::Writ doc_;` (line ~751) | `logos::writ::WritCtr doc_;` |
  | `make_doc(524288).get()` (~1637) | `*logos::writ::make_doc(524288)` (MultiChunk) |
  | `WritAccess::raw_tiny_map(doc_, n).get()` (~2145) | `*doc_.make_tiny_map(n)` |
  | `node->put(K, AV, WritAccess::arena(doc_)).get()` (~2171…) | `(void)node->put(K, AV, doc_.arena())` |
  | `node->set_schema_type_code(writ::schema::ast(c))` (~2207) | `…writ::schema::ast(c)` |
  | `result_.set_pointer(node, WritAccess::base(doc_))` (~2240) | `result_.set_ref(node)` |
  | `WritAccess::set_root_offset(doc_, root.to_offset())` (~1650) | `doc_.set_root(root)` |
  | `doc_.make_array(4).get()` (~1747,1979) + `.to_anyval()` | `*doc_.make_array(4)`; wire via a local `AnyVal{}; v.set_ref(arr)` |
  | `doc_.make_string(t).get().to_anyval()` (~1830…) | `AnyVal v; v.set_ref(*doc_.make_string(t))` |
  | includes (~585,784) `writ/view.hpp`,`access.hpp`,`schema_codes.hpp` | `writ/document.hpp`,`view.hpp`,`schema_codes.hpp` |

  **GOTCHA — parser arena mode.** The legacy parser runs on a GrowableSingleChunk arena
  with base-relative offsets + `arena_checkpoint`/rollback for backtracking. On writ
  (self-relative storage, ABSOLUTE pointers held across allocations), a GrowableSingleChunk
  realloc would dangle the live `node`/`items` pointers. So the parser doc MUST be
  **MultiChunk (never-move)** (`make_doc` defaults to it). Consequence: the codegen's
  `arena_checkpoint()`/rollback emission (backtracking space reclaim) becomes a no-op or
  a MultiChunk tail-drop — backtracking stays correct, just keeps dead nodes from failed
  alternatives until the doc frees (acceptable transient memory for a compile). Either
  extend `Arena::rollback` to MultiChunk (drop appended chunks + rewind tail) or emit no-ops.
- **Phase C — LIR mirror — THE HARD PART (finding, 2026-06-10, branch `writ-cutover`).**
  Attempting the full flip revealed the mirror is **~85% of the cut-over and a genuine
  base+offset SUBSYSTEM**, not a mechanical rename. Of 551 compile errors after the bulk
  sed, **470 are the mirror** (`sema.hpp` 286 + `lir_view.hpp` 184) and `lir_mirror.cpp`
  (447 sites) had not even reached compile. `ExprRef/StmtRef/TypeRef` store `arena_offset_t`
  and resolve via `parent.base() + av.to_offset().value()`. This COLLIDES with the AST
  readers: they want `AnyVal::to_offset()` to feed a `View(AnyVal,holder)` ctor, while the
  mirror wants `arena_offset_t` — the SAME method, two incompatible return types. So the
  mirror needs its OWN self-relative redesign: `ExprRef{holder, AnyVal-or-ptr}` (drop the
  `arena_offset_t mirror_offset_` storage in `LExpr`), `mirror()->get(key,base)` → `get(key)`,
  `parent.base()`/`mirror_base()`/`av.to_offset()` removed, mono's cross-arena clone
  (ExternalRef) re-expressed on writ. SEPARATE arena from the AST, so it CAN be staged:
  either convert it to self-relative as a dedicated push, OR keep `lir_view`/`lir_mirror`/
  `sema.hpp` mirror types on `logos_writ` (legacy) while the AST goes writ (both libs
  link; different arenas). The AST side (true Phase B) is only ~53 errors.
- **Phase D — `.writ0`** (`emit_module.cpp`, `module_loader.cpp`) — binary_encode for
  AST + publish/clone for the LIR blob + ArenaPool load (§6.1 layer is ready).
- **Phase E — link swap:** drop `logos_writ`, link `logos_writ2`; delete legacy (step 6).

**Original strategy note (kept for context):** keep the legacy **API + collection algorithms** so
logosc's ~1150 `writ::` call-sites barely change; swap the **primitives** (done in
`logos_writ2`). Concretely: provide the same view surface (`TinyMapView::get`,
`map_of/arr_of/str_of`) on top of `logos_writ2`, so the sema/mirror walkers are a
near find-replace (`writ::` → `writ::`, drop the `base` arg — self-relative needs
none). Tiers: parser output + AST repr + mirror emit/read + `.writ0` codec MUST
change; mlir-gen / borrow-check / mono read LIR via the C++ variant + schema_code so
they barely move. **AnyVal 4→8 widens every AST/mirror slot** — verify schema-key
layouts. Do it module-by-module behind the existing helper boundary.

### 6.3 path (JMESPath) + template (Jinja)

`path.{hpp,cpp}` (~1350L) + `template.{hpp,cpp}` (~720L) — a user-facing query/template
utility layer, NOT used by logosc. Port last (or never, if Logos drops them). They sit
on the same view/document API, so they port mechanically once the data model is stable.

### 6.4 Refinements

- **Reallocating compactify via `AddrResolver`** (Memoria's way; studied, not needed —
  pre-size suffices). `AddrResolver<T> = variant<T*, ptrdiff_t>`, `.get(arena)`
  re-resolves; `Arena::get_resolver_for` picks form by mode (absolute in MultiChunk,
  offset in GrowableSingleChunk). `DeepCopyState` would store resolvers; deep_copy
  re-resolves dst after each recursion. Only needed if you must compact without a size
  bound AND make the containers resolver-safe.
- **Exact Logos text grammar** — current `stringify`/`text_parser` are JSON-core; the
  Logos `stdlib/mem/writ/{stringify,parser}.logos` define the real Writ text
  grammar. Match it if text interop with Logos-side is required.

---

## 7. Sources & references

- **Memoria** (`/home/victor/cxx/memoria`) — the original self-relative multi-segment
  Writ. Key: `core/include/memoria/core/arena/{relative_ptr,arena}.hpp`,
  `core/include/memoria/core/writ/{container,serialization}.hpp`,
  `core/lib/writ/writ_container.cpp` (compactify/clone). Apache-2.0, © V. Smirnov →
  copy-and-adapt is fine.
- **Logos writ stdlib** (`stdlib/lang/writ/*.logos`) — the BYTE-LAYOUT SPEC.
  `anyval.logos` (HAny niche), `wstring/array/wmap/decimal/typed_value/parameter`.
- **legacy C++** (`src/writ`, `include/logos/writ`) — API + algorithms to keep.
- **Design**: `docs/internals/writ-design.md` (§2 self-rel, §3 segments, §4
  compaction=copying-GC + Rc liveness).
- **Memory notes**: `project_writ2_cpp_migration` (this migration, terse),
  `project_writ2_port_complete` (the Logos-side wire-type port),
  `project_writ2_partialspec_refactor` (the HMap/HArray partial-spec mechanics).
