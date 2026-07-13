# Memoria/Canon wave-0 — frozen implementation design

- Status: FROZEN for wave-0 implementation (proto/memoria-canon-wave0)
- Governs: ADR 0020 (container-interface plane; Canon = judge-not-doer),
  riding the ADR 0016 item pipeline (`mapping`/`rel`/`deem` precedent).
- Scope (hard cut): ONE language item `container`; Vector closes the full
  loop (item → facts → Canon verdicts → generated rel-source → static deem
  query over Vector-v0 returns rows); Map = parse + facts + verdicts ONLY.
  SCAN materializer only (no adornment seek, no zero-copy). Canon wave-0 =
  stdlib fn `canon_verdicts(facts: &Writ) -> Writ` implemented with static
  deem items over the &Writ graph source.

Canonical surface (both must work):

```
pub container Vector<T> for VecCtr<T> { kind vector; entry { elem: T } measure count; }
pub container Map<K, V> for MapCtr<K, V> { kind ordered_map; entry { key: K, val: V } measure count; measure max(key); }
```

`for <Type>` = wave-0 pragmatic binding of declaration to backing type; the
final language may separate them (flagged, not solved here).

Responsibility chain (rigid, per ADR 0020 §5): container declaration →
Sema records + serializes → handler mirrors to FACTS (Writ doc) → Canon
(deem rules) returns VERDICTS → handler consumes verdicts and EMITS (or
diagnoses). Canon never emits; the handler never decides.

---

## (a) Grammar — `tools/peg_gen_cpp/grammars/logos.peg`

`container` is a CONTEXTUAL keyword (the identifier `container` is common —
stdlib `logos.lang.writ.container` &c; same rationale as `deem`/`rel`): lead
bare IDENT captured in the `REL_KW` slot (key 20), text validated in sema.
Clause leads (`kind`/`entry`/`measure`) are likewise contextual.

Rules (add after `mapping_def`, ~logos.peg:1300; `KW_FOR` and `type_ref`,
`type_param_list`, `param_list` all exist):

```
    // ADR 0020 wave-0: `pub? container C<T…> for BackingTy { kind k; entry { col: ty, … } measure m; measure m(col); … }`
    // Contextual lead ident in REL_KW (same convention as deem/rel — a
    // `container` keyword would break the very common identifier). The
    // backing type after KW_FOR rides TYPE as an ordinary type_ref. Clauses
    // are grammar-owned (no RAW_TEXT — the body is fully structured).
    container_clause <- doc_line_decl / doc_block_decl
                      / container_entry / container_measure_arg / container_word
    container_entry  <- IDENT LBRACE param_list? RBRACE
                     => { CODE: CONTAINER_CLAUSE, REL_KW: $1, PARAMS: $3 }
    container_measure_arg <- IDENT IDENT LPAREN IDENT RPAREN SEMI
                     => { CODE: CONTAINER_CLAUSE, REL_KW: $1, NAME: $2, VALUE: $4 }
    container_word   <- IDENT IDENT SEMI
                     => { CODE: CONTAINER_CLAUSE, REL_KW: $1, NAME: $2 }

    pub_container_def <- pub_vis IDENT IDENT type_param_list KW_FOR type_ref LBRACE container_clause* RBRACE
                      => { CODE: CONTAINER_DEF, IS_PUB: 1, VIS: $1, REL_KW: $2, NAME: $3, TYPE_PARAMS: $4, TYPE: $6, FIELDS: $8 }
                       / pub_vis IDENT IDENT KW_FOR type_ref LBRACE container_clause* RBRACE
                      => { CODE: CONTAINER_DEF, IS_PUB: 1, VIS: $1, REL_KW: $2, NAME: $3, TYPE: $5, FIELDS: $7 }
    container_def     <- IDENT IDENT type_param_list KW_FOR type_ref LBRACE container_clause* RBRACE
                      => { CODE: CONTAINER_DEF, REL_KW: $1, NAME: $2, TYPE_PARAMS: $3, TYPE: $5, FIELDS: $7 }
                       / IDENT IDENT KW_FOR type_ref LBRACE container_clause* RBRACE
                      => { CODE: CONTAINER_DEF, REL_KW: $1, NAME: $2, TYPE: $4, FIELDS: $6 }
```

- TYPE_PARAMS-optional handled by duplicated alternatives (the mapping_def
  precedent — do not rely on `?` around a capturing non-terminal).
- Clause disambiguation is ordered: `container_entry` (LBRACE after first
  IDENT), then `container_measure_arg` (parenthesized arg), then
  `container_word` (`kind vector;` and `measure count;` share this rule —
  sema dispatches on the REL_KW text).
- Hook into the `item` alternation (logos.peg:551): insert
  `pub_container_def / container_def` immediately after
  `pub_mapping_def / mapping_def`. No prefix conflict: `deem_def` requires
  LPAREN after the second IDENT, container requires `<` or `for` — PEG
  backtracking resolves either order, adjacency to mapping keeps intent
  readable.
- Regeneration: the parser is built from the .peg by CMake
  (src/compiler/CMakeLists.txt:28-39). `ast.hpp` is GENERATED
  (`peg_gen_cpp logos.peg --ast-header`) and gated byte-identical by
  `tools/peg_gen_cpp/check_ast_header.cmake` — regenerate + commit BOTH the
  .peg and `include/logos/compiler/ast.hpp`.

## (b) AST node codes

Verified free: current global max = 265 (`MAPPING_DEF_DONE`); no 266+ in the
.peg or ast.hpp. Declared in the .peg `%nodes` block (logos.peg:329-342) and
mirrored into the generated ast.hpp:

- `CONTAINER_DEF = 266` — item node. REL_KW = lead ident ("container");
  NAME; TYPE_PARAMS? (generics incl. bounds, syntactic); TYPE = backing
  type_ref; FIELDS = array of CONTAINER_CLAUSE; IS_PUB/VIS as usual.
- `CONTAINER_DEF_DONE = 267` — sema-internal section (next to
  MAPPING_DEF_DONE=265, WRIT_BLOB=261): a CONSUMED container item. Only CODE
  flips (driver); NAME/TYPE_PARAMS/TYPE/FIELDS survive so archive consumers
  re-register the declaration (cross-module Canon reasoning later).
- `CONTAINER_CLAUSE = 268` — one clause. REL_KW = lead ident
  ("kind"|"entry"|"measure"); NAME = word (`vector`, `count`, `max`);
  PARAMS = entry column list (PARAM array, reused param_list); VALUE =
  measure arg ident (`key` in `max(key)`).

No new field keys: REL_KW (20), NAME, TYPE, TYPE_PARAMS, PARAMS, VALUE,
FIELDS, IS_PUB, VIS all exist.

## (c) Sema registration

`src/compiler/sema_impl.hpp` (next to MappingInfo, ~:4218):

```cpp
struct ContainerCol     { std::string name, ty; bool is_param = false; };  // ty syntactic; is_param: ty ∈ container generics
struct ContainerMeasure { std::string mfn, arg; };                          // ("count",""), ("max","key")
struct ContainerInfo {
    std::string name;  bool is_pub = false;  std::string vis;
    std::string generics_src;                 // "<T>" / "<K, V>" verbatim (bounds included)
    std::vector<std::string> generic_names;   // ["T"] / ["K","V"]
    std::string backing_src;                  // "VecCtr<T>" — SYNTACTIC render (mapping precedent; resolved only on the emission path)
    std::string backing_pkg;                  // defining package of the backing base type if resolvable, else ""
    std::string kind;                         // "vector" | "ordered_map"
    std::vector<ContainerCol>     entry;
    std::vector<ContainerMeasure> measures;
};
std::unordered_map<std::string, ContainerInfo> containers_;
```

Flow (mapping template, exactly):

1. **Pre-scan** (sema.cpp ~7567, in the same items loop as the mapping
   pre-scan): accept BOTH `CONTAINER_DEF` and `CONTAINER_DEF_DONE`, call
   `reconstruct_container_def(TinyMapView, ContainerInfo&)`
   (sema_expr.cpp, next to reconstruct_mapping_def :20308) → idempotent
   insert into `containers_`. Validation here: REL_KW=="container"; clause
   leads ∈ {kind, entry, measure}; exactly one `kind`, value ∈ {vector,
   ordered_map}; exactly one `entry`, non-empty; measure `count` takes no
   arg, `max` requires one. Entry column types recorded via
   `render_type_src_syntactic_`; `is_param` = ty equals one of
   generic_names. COMPLETENESS IS NOT CHECKED HERE — that is Canon's verdict
   (judge-not-doer).
2. **Item dispatch** (sema.cpp ~7838 block): `CONTAINER_DEF` →
   `lower_container_def`; `CONTAINER_DEF_DONE` → skip (pre-scanned).
3. **`lower_container_def`** (sema_expr.cpp, next to lower_mapping_def
   :20550): resolve handler `__container_item` in `func_overloads_`,
   require `is_token_macro` + exact ABI
   `(name: str, spec: str) -> ItemList`; serialize ContainerInfo to the
   spec string (below); route through the SHARED seam
   `emit_token_macro_item_site(node, prog, macro_info, rt_is_il, /*nargs=*/2,
   cname, /*params_text=*/"", /*raw_text=*/spec, IrEntry::None, "", "", "")`
   — the nargs==2 non-IR path packs exactly (name, spec) blobs; no rule IR
   (the container body is fully structured — nothing for the WQL parser).
4. **DEF→DONE flip** (`src/compiler/main.cpp`, both flip sites — iter loop
   :2781-2820 and compile-mode loop :4364-4397): add
   `CONTAINER_DEF → CONTAINER_DEF_DONE` alongside `MAPPING_DEF →
   MAPPING_DEF_DONE`.
5. Cross-module: DONE-node re-registration from archived AST (step 1)
   suffices for wave-0; the explicit export mirror (sema_impl.hpp:5178
   trait_rels/mappings) is NOT extended now.

**Spec string** (compiler → handler, one line, `|`-separated `k=v`, values
never contain `|`):

```
pub=1|generics=<T>|gnames=T|backing=VecCtr<T>|backing_pkg=memoria.ctr.vec|kind=vector|entry=elem:T:1|measures=count
pub=1|generics=<K, V>|gnames=K,V|backing=MapCtr<K, V>|backing_pkg=|kind=ordered_map|entry=key:K:1,val:V:1|measures=count,max(key)
```

entry cols = comma-separated `name:ty:is_param(0|1)`; measures =
comma-separated `mfn` or `mfn(arg)`.

## (d) Fact and verdict Writ schemas

Both docs follow the deem-over-&Writ convenience shape (writ_graph module
doc): top-level WMap, arrays of FLAT maps, scalar leaves i64/str/bool only,
one nesting level. Built with the `logos.lang.writ` API (`writ_new(65536)`,
`d.map(n)`, `d.array(n)`, `m.set(k, v)`, `WAny::from(&*x)`).

**Fact doc** (built by `__container_item` from the spec string — one doc per
container item; wave-0 has no cross-item accumulation):

```
{
  containers: [ { name: "Vector", pub: true, generics: "T", backing: "VecCtr<T>", kind: "vector" } ],
  cols:       [ { ctr: "Vector", idx: 0, name: "elem", ty: "T", param: true } ],
  measures:   [ { ctr: "Vector", mfn: "count", arg: "" } ]
}
```

(Map adds `{ctr:"Map", idx:0, name:"key", ty:"K", param:true}`,
`{…idx:1, name:"val"…}` and measures `{mfn:"count",arg:""}`,
`{mfn:"max",arg:"key"}`.) This is the ADR 0020 §5 fact vocabulary
restricted to wave-0: kind/entry_shape/measure; domain/encoding/builder/
format_hash columns enter in later waves without reshaping (new arrays/keys
are additive).

**Verdict doc** (returned by `canon_verdicts`):

```
{
  ok: true,                                          // ⇔ errors array empty
  caps:      [ { ctr: "Vector", cap: "can_scan", col: "" },
               { ctr: "Vector", cap: "can_rank", col: "" },
               { ctr: "Map",    cap: "can_seek", col: "key" }, … ],
  col_modes: [ { ctr: "Vector", col: "elem", mode: "by_value" }, … ],
  errors:    [ { ctr: "Map", code: "missing_measure", detail: "max(key)",
                 reason: "kind=ordered_map requires measure(max(<first key col>)) — declare `measure max(key);`" } ]
}
```

Capability/mode rules (wave-0 Canon axioms):

- `can_scan(C)` ← `kind(C, k)`, k ∈ {vector, ordered_map}.
- `can_rank(C)` ← `measure(C, count)`.
- `can_seek(C, col)` ← `kind(C, ordered_map)` ∧ `measure(C, max, col)`.
- `col_mode(C, col) = by_value` iff col ty ∈ fixed primitive list
  {i8,i16,i32,i64,u8,u16,u32,u64,usize,isize,bool,f32,f64,char} ∨
  `param(col)` (a container type param — wave-0 approximation of
  “Fst ∧ sizeof ≤ 32”, flagged); `str` and everything else → `by_view`.
- completeness: `kind(C, ordered_map)` ∧ ¬`measure(C, max, first_key_col(C))`
  → error `missing_measure`; `first_key_col` = entry col idx 0 (wave-0
  positional convention). Stratified negation — already shipped machinery.

## (e) Canon stdlib module

New stdlib package dir `stdlib/std/canon/` (std layer auto-discovers by
`root stdlib/std/` in `stdlib/std/logos.module` — no manifest edit):

- **`stdlib/std/canon/canon.logos`** — `package logos.std.canon.canon;`
  - `pub fn canon_verdicts(facts: &Writ) -> Writ` — runs the static deem
    queries below over the fact doc, assembles the verdict doc with the
    writ API, returns it owned.
  - The deem rules: attribute-harvest + joins on row id (the proven
    wql_gpath_e2e patterns — `[*]` step, `*` wildcard binding `a.key/a.vs/
    a.vi`). Shape (bodies tuned against the wql surface at impl time):

    ```
    deem canon_ctr_attr(g: &Writ)  { from g .containers [*] c * a select (c.child, a.key, a.vs, a.vi) }
    deem canon_col_attr(g: &Writ)  { from g .cols [*] c * a select (c.child, a.key, a.vs, a.vi) }
    deem canon_meas_attr(g: &Writ) { from g .measures [*] m * a select (m.child, a.key, a.vs, a.vi) }
    ```

    plus rule-form deem items joining these into `(ctr, kind)`,
    `(ctr, idx, col, ty, param)`, `(ctr, mfn, arg)` tuples and deriving
    caps/col_modes/errors per the (d) axioms. Field harvest of one flat map
    row = self-join of the attr rel on row id filtered by key — pure
    joins + stratified negation, squarely inside the COMPLETE ADR 0013
    engine.
  - `pub trait CanonCol { fn canon_col(&self) -> i64; }` + impls for
    i64,u64,i32,u32,i16,u16,i8,u8,usize,isize,bool — the by_value→i64
    column projection used by generated materializers (§f).
- **`stdlib/std/canon/container_item.logos`** —
  `package logos.std.canon.container_item;`
  - `#[token_macro] pub fn __container_item(name: str, spec: str) -> ItemList`
    (mirror of `__mapping_item`, stdlib/std/wql/mapping_item.logos:45):
    parse spec → build fact doc → `let v = canon_verdicts(&facts);` →
    if `errors` non-empty: `error(...)` (logos.std.compiler.metaprog —
    the mapping_item diagnostic mechanism; EmitProvenance points at the
    item) with the row's `reason`, return empty ItemList → else if
    kind=="vector": emit the §f projection chunk → else (ordered_map):
    emit nothing (Map wave-0 stops at verdicts).
- Consumers write `use logos.std.canon.container_item;` (handler
  visibility — the `use logos.std.wql.mapping_item;` precedent).

Note on the facts round-trip: the fact doc is built and consumed INSIDE the
metacall JIT (handler → stdlib call → verdict back as an owned Writ). No
compiler↔handler Writ crossing exists on this path (unlike `logos_rule_ir`)
— the round-trip gap class is avoided by construction.

## (f) Emission plan for Vector (kind=vector, requires can_scan verdict)

Generated through the EXISTING sources-as-traits seam (ADR 0016 §6): a
trait with a `rel` signature + an impl `rel` binding + a scan materializer.
Emitted as one source chunk with explicit `use` lines (the deem-handler
chunk precedent — plan_walker/params.logos emit `use <module>;` text).
Generated symbols for `container Vector<T> for VecCtr<T>`:

```
use logos.mem.collections.vec;
use logos.std.canon.canon;
use <backing_pkg>;                                   // only when backing_pkg ≠ "" and ≠ consuming package

pub trait __CtrSrc_Vector { rel row(pos: i64, elem: i64); }

pub fn __ctr_rows_Vector<T: CanonCol>(c: &VecCtr<T>) -> Vec<(i64, i64)> {
    let mut out: Vec<(i64, i64)> = vec_new::<(i64, i64)>();
    let n: i64 = c.count();
    let mut i: i64 = 0i64;
    while i < n { out.push((i, c.get(i).canon_col())); i = i + 1i64; }
    return out;
}

impl<T: CanonCol> __CtrSrc_Vector for VecCtr<T> { rel row = __ctr_rows_Vector; }
```

- Naming scheme: trait `__CtrSrc_<Name>`, rel `row`, materializer
  `__ctr_rows_<Name>`. Single-rel vocabulary ⇒ a deem param of the backing
  type is addressable as the param itself (`from v e select (e.pos, e.elem)`).
- Column projection per entry col, decided from the verdict `col_modes` +
  declared ty: `str` → `str` column, value taken directly (view into
  source, legal per materializer contract); `bool` → `bool` column direct;
  everything else by_value → `i64` column via `.canon_col()`. Rel row =
  `(pos: i64, <cols…>)`, pos = leaf index.
- Generic bounds: emitted generics = the container's declared
  `generics_src` verbatim, with `CanonCol` appended (`+ CanonCol`, or
  `: CanonCol` when unbounded) to each param used in a canon_col-projected
  column. The generic impl registers in `source_impls_` under the BASE
  name (`VecCtr`) — verified: GENERIC_INST impl target with own type params
  = base NAME (sema_collect.cpp:3010-3032); rel_bind collection
  (sema_collect.cpp:3308) is target-keyed and impl-genericity-agnostic.
- **Wave-0 scan contract on the backing type** (duck-typed, checked when
  the emitted materializer compiles): `count(&self) -> i64`,
  `get(&self, i: i64) -> T`. Kind=vector emission requires exactly these.
- **One narrow compiler fix required** — `native_source_spec`
  (sema_expr.cpp:20263) lookup: the deem-site key is the param type
  stripped of `&`/spaces (`"VecCtr<u64>"`), but the generic impl registered
  under `"VecCtr"`. Fix inside native_source_spec: on lookup miss, strip
  a trailing `<…>` (and any whitespace) from the key and retry. ~6 lines,
  benefits any future generic source impl; both call sites (:20638, :20939)
  go through this one fn.
- Consumption (nothing new): the natspec spec
  `<param>=__ctr_rows_Vector[@<pkg>]:<param>(pos i64,elem i64);` is emitted
  by the existing `native_source_spec`; `register_native_rels`
  (plan_walker.logos:1728) registers it; the generated query calls
  `__ctr_rows_Vector(v)` — generic inference from the concrete
  `&VecCtr<u64>` arg.

## (g) Vector-v0 backing container — `conuco/memoria/src/ctr/vec.logos`

Package `memoria.ctr.vec`, target `memoria-ctr` (deps memoria-pkd already in
lforge.writ). Owned single block + FSE PkdArray (BtArena idiom,
src/bt/bt.logos:40-67; per-op view re-resolution per pkd_arr.logos):

```
package memoria.ctr.vec;
use memoria.pkd.alloc;   // pkd_format, PkdAlloc, PkdError
use memoria.pkd.arr;     // PkdArray (FSE spec: T: Copy + Fst + StableLayout)
use logos.lang.mem;      // alloc / dealloc

pub struct VecCtr<T: Copy + Fst + StableLayout> { mem: *mut u8, cap: u32 }

impl<T: Copy + Fst + StableLayout> VecCtr<T> {
    pub fn new(cap_bytes: u32) -> VecCtr<T>
        // alloc(cap_bytes) + pkd_format(mem, cap_bytes, 1) + PkdArray::<T>::format(a, 0)
    fn arr(self: &VecCtr<T>) -> *mut PkdArray<T>
        // (self.mem as *mut PkdAlloc).get::<PkdArray<T>>(0) — re-resolved EVERY call (ptrs dangle across mutations)
    pub fn count(self: &VecCtr<T>) -> i64      // arr().count()
    pub fn get(self: &VecCtr<T>, i: i64) -> T  // arr().get(i)
    pub fn push(self: &mut VecCtr<T>, v: T)    // arr().push(&v); on PkdError (full) → grow() then retry
    fn grow(self: &mut VecCtr<T>)
        // v0 rebuild: alloc 2×cap, pkd_format fresh, PkdArray format,
        // copy elements via old as_slice → push loop, dealloc old block.
        // (Relocatable-block memcpy + in-place enlarge is a later wave.)
}
impl<T: Copy + Fst + StableLayout> Drop for VecCtr<T> { fn drop(self: &mut VecCtr<T>) { /* dealloc(self.mem) */ } }
```

e2e element type = u64. Method set intentionally = the §f scan contract +
push.

## (h) E2E test plan

1. **Compiler-suite pass fixture** `tests/logos/pass/container_item_e2e.logos`
   (+ `.expected` = `exit: 0`; run via run_test.sh) — single file:
   - minimal inline backing type named `VecCtr<T>` (plain `Vec<T>`-backed
     struct with count/get/push — no pkd; the container surface is what is
     under test);
   - the two canonical declarations VERBATIM (`container Vector<T> for
     VecCtr<T>`, `container Map<K, V> for MapCtr<K, V>` — MapCtr
     intentionally UNDEFINED: ordered_map emits nothing, backing resolves
     only on the emission path; this pins the parse+facts+verdicts-only
     contract for Map);
   - `pub deem big(v: &VecCtr<u64>) { from v e where e.elem > 10 select (e.pos, e.elem) }`
     (body adjusted to the wql surface at impl time);
   - main: push 5, 25, 7, 42 → assert rows {(1,25),(3,42)}; exit 0.
   - imports: `use logos.std.wql.wql; use logos.std.canon.container_item;`
     + vec/str as needed.
2. **Compiler-suite fail fixture**
   `tests/logos/fail/container_ordered_map_missing_max_fail.logos` —
   `container Map<K, V> for MapCtr<K, V> { kind ordered_map; entry { key: K, val: V } measure count; }`
   → expect the Canon diagnostic naming the missing measure
   (`missing_measure` / `measure max(key)` in the message).
3. **Canon unit pass fixture** `tests/logos/pass/canon_verdicts_basic.logos`
   — builds a fact doc BY HAND with the writ API (one vector-complete, one
   ordered_map-incomplete container), calls `canon_verdicts`, asserts
   caps/col_modes/errors rows. Decouples Canon-rule debugging from the item
   pipeline.
4. **conuco lforge test** `conuco/memoria/tests/ctr_vec_deem.logos` — real
   `VecCtr<u64>` from memoria.ctr.vec; the Vector container declaration
   (with the pkd bounds on T, which the surface admits:
   `pub container Vector<T: Copy + Fst + StableLayout> for VecCtr<T> { … }`);
   deem query; assert rows. Run:
   `cd conuco/memoria && rm -rf .lforge && PATH=build/bin lforge test`.
   Module-mode trap: explicit `use logos.lang.result;` in the test.

Loop discipline: after ANY .logos/stdlib or compiler change →
`cmake --build build -j12` → targeted `ctest -R container\|canon\|wql`;
full suite (ctest-summary.sh) once at the end (compiler+stdlib touched).
Commit per green milestone: (M1) grammar+codes+ast.hpp regen, (M2) sema+
flip+spec+stub handler, (M3) canon module+unit test, (M4) emission+
native_source_spec fallback+e2e pass/fail fixtures, (M5) conuco VecCtr+
lforge test, (M6) full-suite green + report.

## (i) Risks and fallbacks

- **R1 — first deem items INSIDE the std layer** (canon.logos; today stdlib
  contains zero deem/mapping items — writ_graph/mapping_state declare
  traits/impls only). The metacall iter loop + poison guard were built for
  in-compilation emission, but the std-layer self-build is the untrodden
  path. Fallback: fix at the root in the thunk pipeline (likeliest spot:
  meta-JIT module reachability prune, main.cpp:4141/:4205, must keep the
  same-layer wql handler cone). Canon stays deem-implemented — no plain-
  Logos rewrite fallback (scope requires deem rules).
- **R2 — generic-source lookup miss** (`VecCtr<u64>` vs base-name key):
  the named narrow fix IS the plan (§f, native_source_spec base-name
  retry). If a second seam ALSO strips types (none found — both natspec
  call sites route through this fn), apply the same base-name rule there.
- **R3 — generic impl `rel row = …` rejection**: collection looks
  genericity-agnostic (target-keyed); if a hidden guard rejects it, fix
  that guard (class fix), not the emission.
- **R4 — emitted-chunk name resolution** (Vec/vec_new/CanonCol/backing in
  the consumer's module): chunks carry explicit `use` lines (deem-handler
  precedent). If item-blob splice rejects `use` items, fall back to
  fully-expanded text emission through the same Emitter the deem handler
  uses (`em.push_text` path — already emits use-lines today).
- **R5 — lforge/module-mode metacall for container items**: grammar! in
  peg_gen_logos is the working precedent; known trap = Result prelude
  masking (explicit `use logos.lang.result;` in emitted chunk + tests).
  Module-only failures reproduced with a mini-lforge project in scratch,
  not single-file logosc.
- **R6 — wql surface details in Canon rule bodies** (`*` wildcard harvest +
  self-joins + `where` on str eq): all exercised by wql_gpath_e2e /
  writ_graph e2e; if a specific form is missing, prefer reshaping the rule
  (joins are complete per ADR 0013) over touching the engine.
- **R7 — bounds interaction**: emitted `T: CanonCol` must not violate the
  backing struct's own bounds — the container declaration carries the
  backing's bounds in ITS generic list (conuco test does; the fixture's
  inline backing is unbounded so the canonical unbounded surface is also
  exercised). If bound-spec selection (A14-ф2) still trips, tighten the
  emitted bound set to `declared + CanonCol` — never weaken the check.
- **R8 — facts round-trip**: avoided by construction (§e note) — facts
  live entirely inside the metacall JIT heap. No compiler fix anticipated.

Out of scope (restated): adornment-aware seek, zero-copy rows, Map
emission/substrate, measure `freq`, builder/DML/DDL vocabulary, parity
benchmarks, declaration/binding separation for `for <Type>`.
