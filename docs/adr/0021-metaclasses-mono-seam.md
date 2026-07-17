# ADR 0021 — Metaclasses: one instantiation engine, factories via metacall, reasoners as metafunctions

- Status: **DRAFT** (pair-designed 2026-07-17; implementation in progress —
  converting the wave-0 container pipeline of ADR 0020 onto this seam)
- Date: 2026-07-17
- Builds on: ADR 0020 (Memoria container plane + Canon; the invariant "Canon
  never emits; metaprog never decides" is generalized here), ADR 0017
  (WritStatic config documents — `MapCfg: WritStatic = @{...}` and
  `<type:CFG.slot>` extraction are the type-level document precedent), ADR
  0009 (lazy mono infrastructure), the explicit-metacall / no-const-fn
  language policy.
- Supersedes: the *implementation strategy* of ADR 0020 §10 wave-0 (eager
  declaration-site emission, string-keyed identity, sema-side harvest for
  generics). The ADR 0020 *design* (level map, Canon as judge, verdict
  vocabulary, structure-as-verdict) is unchanged and load-bearing.

---

## 0. Context: we grew a second monomorphizer

Wave-0 of ADR 0020 proved the reasoning chain end-to-end (declaration →
facts → Canon verdicts → `container_shape` plan → emitted leaf/branch/tree/
CoW-map families). In doing so it quietly re-implemented, at the metaprog
level, every function of the compiler's monomorphization engine — worse:

| Function        | LIR mono (compiler)                          | wave-0 container path                          |
|-----------------|----------------------------------------------|------------------------------------------------|
| demand          | scan_fn over call sites, worklists, fixpoint | sema-side harvest patch (`pending_container_srcs`) |
| identity        | mangled type-args, pkg-qualified             | string concatenation of names (`{N}Leaf`, config tuple) |
| memoization     | done-sets, O(1)                              | `blob_seen` over emitted bytes                  |
| cross-module    | binary_symbols + link_name algebra           | CONTAINER_DEF_DONE re-registration (broken for generics) |
| ODR             | mangled names, archive gates                 | unsolved (string names collide across archives) |

Every wall hit during wave-0 was a collision between the two systems, not an
isolated bug: the mixed concrete+generic unit failure (metaprog stubs vs mono
discovery), the twins probe (two structurally identical declarations → 2×770
lines of duplicated emission, `blob_seen` cannot merge them — names are in the
key), the cross-module generic wall (`Map<K,V>` declared in a library is
invisible to the consumer's harvest — `map_consumer.logos.pending`), and the
open ODR question for generic instantiations between two consumer archives.

C++ Memoria's TypeFactory stack (CtrTF/BTTypes) had the same three roles
fused in one medium — TMP: selection (partial-spec lattices), assembly, and
identity/demand (which the template engine provided for free). The fusion is
what made MBT painful; the free identity/demand is what wave-0 lost when it
moved assembly to strings.

## 1. Decision

**There are exactly two mechanisms. Roles are contracts, not machinery.**

1. **mono** — the single instantiation engine of the language. It
   *substitutes and memoizes*; it never computes and never decides.
2. **metafunctions** — ordinary Logos functions executed at compile time,
   bound to the compiler through **metacall**, which is not an evaluator but
   a *binding*: function-call semantics as the bridge between the compiler
   and compile-time computation. A metafunction is callable wherever the
   syntax admits the metacall operator; the set of "extension points" is
   exactly the set of metacall positions.

Within the metafunction level, roles are separated by *contract*:

- a **reasoner** is a metafunction set in declarative form (deem/mapping
  rules): pure, `facts → verdict document`, memoizable on its input,
  decisions explainable by provenance. **Canon is such a set** — not a third
  mechanism. The future registry of reasoners (Canon → Nous →
  logos-compiler-on-Logos) is a registry of metafunctions behind one slot
  contract.
- a **factory** is a metafunction `(CFG, verdict) → items`: mechanical
  rendering of a verdict, no decisions. The jinja/trama discipline, stated
  once for the whole architecture: the template receives a render-ready
  structure; logic inside is trivial and rendering-local. The reasoner
  produces that structure.

The ADR 0020 invariant — *Canon never emits; the factory never decides* — is
therefore a discipline of contracts inside one level, enforced by review and
by the declarative form of reasoners, not by separate engines.

### 1.1 The phase rule (why the generics library cannot grow towers)

The boundary between "reuse a generic library" and "generate concrete code"
is not empirical tuning; it is a *phase* rule, enforced by the language:

- code that executes at **runtime** is written with ordinary language means
  and may be organized as a library of generic functions (`logos.mem.pkd`,
  `logos.mem.bt` are exactly that library);
- code that executes at **compile time** and is more complex than an
  arithmetic expression goes through **metacall**. Logos deliberately has no
  `const fn` — so there is no temptation to do type-level computation with
  type-level means that happen to be Turing-complete. C++ TMP's sin, in one
  line: compile-time computation executed in the type layer because there
  was nowhere else to put it.

Consequence: mono only substitutes, metafunctions compute, reasoners decide —
and each layer is *unable* to do its neighbor's job by construction.

### 1.2 The placement rule (where each piece of functionality lives)

Given a piece of container functionality, decide its home by looking at the
C++ Memoria oracle:

- if it is a **runtime function** in Memoria → it belongs in a **code
  library** (ordinary generic Logos functions; mono monomorphizes them —
  `logos.mem.pkd`, `logos.mem.bt`);
- if it is **held by a type factory / computed at compile time (TMP)** in
  Memoria → the **computation is a metafunction**, but its **result is
  generation**. The *form* of that generation is free, chosen per piece for
  clarity: a type declaration, a generated generic function/struct/trait, a
  generated WritStatic, or plain monomorphized code.

This dissolves the false dichotomy "the factory emits everything concrete"
vs "reuse a generic library." A metafunction *computes the shape* (what
CtrTF/BTTypes computed as nested type aliases) and *emits the cleanest
representation per piece*: the b+tree ops (runtime methods in Memoria) become
a generated/library **generic** the mono unrolls; the FSE/VLE node-layout
selection (a partial-spec TMP in Memoria) is **generated concrete**. A
container family is therefore a *mix* of forms, not a concrete monolith — and
this is exactly what ADR 0017's `create_ctr` + generic `ContainerRef` +
`<type:CFG.slot>` already is (a metafunction result that is a generated
generic, monomorphized by mono); it merely lacked Canon deciding the node
specialization. The wave-0 all-concrete emission is one valid output form,
not the mandatory one.

## 2. Metaclasses

A **metaclass** is the type of a declaration kind: the classifier that turns
a declaration into (a) a typed configuration document, (b) a type-level
identity, (c) knowledge, and (d) an on-demand factory binding.

```
container Legend { kind ordered_map; entry { key: u64, val: str } measure max(key); }
  ⇒ (a) CFG: a WritStatic document lowered from the body
    (b) type Legend = ContainerType<CFG>      — a plain type ALIAS
    (c) decl-time facts (classification + descriptive facet, ADR 0020)
    (d) instantiation of ContainerType<CFG> demands the factory (metacall)
```

Key properties, each inherited from an existing mechanism rather than built:

- **Aliases are transparent.** `Legend` is a type alias; sema resolves it;
  mono sees only the canonical `ContainerType<CFG>`. Several aliases of one
  configuration are one type — mono unifies them out of the box. Container
  typing is **structural**; distinguishing two containers of the same shape
  is a runtime matter (`ctr_id`). No `typeof` operator, no newtype wrappers.
- **Identity = the config document.** WritStatic documents already mangle as
  content hashes (`@hs_<64bit>`, ADR 0017 machinery). The mangle of
  `ContainerType<@hs_X>` is the family key: structural memoization, twin
  deduplication, and cross-archive ODR all come from one place — and the
  generated families enter the same link-name algebra that gates every
  body-skip layer (sema skip / mono stub / mlir-gen forward-declare).
- **Two facets, two times.** The *descriptive* facet (facts, catalog,
  Deem-plane reasoning over "what containers exist") stays at declaration
  time. The *operational* facet (verdicts → emission) moves to demand time.
  This is ADR 0020's facet pair, projected onto the timeline correctly.

### 2.1 The metaclass protocol (slots)

Every slot is a metacall position plus a contract on the metafunction bound
to it. Uniform by construction — slots differ by contract, not by kind.

| Slot       | Phase        | Contract                                            | Precedent                     |
|------------|--------------|-----------------------------------------------------|-------------------------------|
| `schema`   | parse/collect| validate + type the declaration body → CFG doc      | Writ schemas (ADR 0011)       |
| `binds`    | collect      | what the name denotes: `type` \| `const` \| none    | type aliases, WritStatic consts |
| `facts`    | collect      | decl-time facts (descriptive facet); classification fact automatic | `container_facts` + vocab seam |
| `reasoner` | two moments  | pure `facts → verdict doc`; see §2.2                | Canon (structure-as-verdict)  |
| `factory`  | mono demand  | `(CFG, verdict) → items`; memoized on `@hs(CFG)`    | `__container_item` emitters   |
| `catalog`  | runtime      | store-catalog descriptor for DDL life (ADR 0020 §4.4)| deferred                      |

### 2.2 Reasoner timing: two moments

Diagnostics for a malformed declaration must fire at the declaration site;
but for a generic declaration (`Map<K, V>`) the concrete CFG does not exist
yet. The protocol therefore splits the reasoner into:

- **template validation** at decl time — judge what is already known; errors
  land on the declaration;
- **concrete verdict** at demand time — judge the full configuration,
  memoized by `@hs(CFG)` (one Canon run per configuration, ever). This is
  also the small-scale rehearsal of incremental re-deem performance that the
  Nous/self-hosting line needs.

### 2.3 Bootstrap and strata

`metaclass` is the **single built-in concept** of this system; every
declaration kind is a library-defined instance of it. Fixed strata:

    built-in metaclass → user metaclasses → declarations → values

There is no metaclass of a metaclass. If classification *of* metaclasses is
ever needed (Nous/ontology), it lives in facts (punning), not in the
language. This is the OWL-Full guard.

### 2.4 Surface syntax: deferred

No user-facing `metaclass { ... }` item and no universal Writ-literal body
for now. `container` keeps its current grammar; the protocol lives as a
contract inside the compiler and stdlib. An extensible-parser story (in the
spirit of the macro/eDSL machinery, one-peg-dialect) is separate fundamental
work. Until then there is exactly **one metaclass: `container`**.

## 3. The mono seam

The one genuinely new compiler mechanism: a **demand edge from mono to the
metaprog dispatch loop**.

When mono instantiates a type whose base is registered as factory-backed
(`ContainerType<CFG>`, reached through `create_ctr::<Legend>(&mut snp,
ctr_id)` or any other use), instead of failing on a missing template it:

1. computes the family key = mangle of the canonical type (`@hs(CFG)`),
2. checks memoization (already emitted in this compilation? pre-baked in a
   linked archive? — the `binary_has_link` algebra covers the second),
3. if new: invokes the factory metafunction with the CFG document as the
   argument (a metacall position), collects the emitted items into the next
   metaprog dispatch round, and defers the instantiation until that round
   lands (the deferred-emission poison guard and dispatch fixpoint already
   model exactly this re-entry).

The CFG document crosses the mono→metacall boundary *as data* with no new
reification machinery — that is what WritStatic-at-type-level is for, and it
is the precise fix for TMP's "config exists only as types" pathology.

### 3.1 Seam coordinates (from recon, 2026-07-17)

**Demand points** — two existing "give up and defer" guards, both natural
interception sites:
- struct side: `instantiate_struct_templates` missing-template branch,
  `mono_clone.cpp:6236-6258` (records `missing_struct_insts_`, poisons
  referencing fns). Fixpoint at `mono_clone.cpp:6199`; `record_needed_struct`
  gate at `mono_impl.hpp:713`.
- fn side: `enqueue_if_needed` poison gate, `mono_scan.cpp:482-498` via
  `type_contains_error` (`mono_scan.cpp:38-51`, already treats `CfgSlotType`
  as an error class).

**CFG already flows through mono, structurally** — mono splices a registered
WritStatic literal for a `__const_param:CFG` VarRef (`mono_clone.cpp:531`) and
reads `<type:CFG.slot>` (`CfgSlotType`) structurally into the registered
document during substitution (`mono_subst.cpp:432`). The `wstatic_registry_`
(`lir.hpp:848`, sema-filled, mono-read) is the document store; the demand key
is the document's content hash.

**Mangling landmine** — two incompatible spellings of the same `WStaticLit`:
struct-name mangling emits `hs_<hex64>` (no `@`, `sema.cpp:1501`), while
mono's `type_str`/`mangle` emit `@hs_<hex>` (`sema.cpp:2141`). **Key demands
off the raw `uint64` hash**, never off a mangled substring.

**Channel** — no existing `LProgram` field carries a mono→driver "invoke a
factory, feed me the result" signal. Add one, symmetric to
`pending_container_srcs` (`lir.hpp:869`, the documented sema-filled precedent
drained at `main.cpp:4540`) but **mono-filled** and keyed by `(hash,
template-identity)`. Drain it in a **new wrapper loop around the terminal
`mono_pass`** (`main.cpp:5468`) — the only mono call that sees full user-code
generic bodies; the in-loop mono at `main.cpp:3117` runs under
`metaprog_mode` and skips entry-file bodies (`main.cpp:4479`). The wrapper
mirrors the existing 8-round container-harvest loop (`main.cpp:4460`): invoke
the factory (JIT via the metacall-thunk machinery, cf. `main.cpp:4816`),
splice returned source via `logos_emit_source`, re-run
`run_metaprog_dispatch` + a fresh terminal `mono_pass`. The closest existing
"invoke compile-time fn, get source back" is the metacall `ItemBlob` drain
(`main.cpp:3040`) — same shape, different trigger (sema expression vs mono
instantiation).

**Aliases confirmed dead by mono time** — `resolve_type` returns the RHS
`TypeRef` (`sema.cpp:2606` non-generic, `sema.cpp:5509` generic); a
`const CFG: WritStatic` reference eagerly resolves to `WStaticLit(hash)`
before becoming a type-arg (`sema.cpp:5169`). mono sees `Foo<WStaticLit(hash)>`,
never `Foo<CFG>` — so twin aliases unify by hash out of the box, and the
demand key is built from `(hash, template)`, never a source name.

**Surface + CFG-threading are already proven by ADR 0017.** `handle.logos`
(`stdlib/lcm/deem/data/handle.logos:717`) ships
`create_ctr::<const CFG: WritStatic, const STORE_CFG: WritStatic>(snap: &mut
Snap<STORE_CFG>, ctr_id) -> ContainerRef<STORE_CFG, CFG>` and
`open_ctr::<…> -> Option<ContainerRef<…>>` (`:737`), threading
`<type:CFG.key>`/`<type:CFG.value>` into generic ops (`leaf_alloc`,
`bt_size_rec`). This is exactly the target surface, working today over
ordinary mono. **But its body is a hand-written generic-DST b+tree library**
(`NodeARC<STORE_CFG>`, generic over slot types) — the very tower wave-0
deliberately replaced with metaprog-concrete node emission (ADR 0020 Track B:
"Node generated as a CONCRETE type per instantiation, NOT hand-written
generic `PkdLeaf<K,V>`"). We therefore **adopt ADR 0017's surface and
CFG-threading, but route the body to the metaprog-emitted concrete families**,
not back to the generic library. The two `create_ctr` also **collide by
name**: ADR 0017's lives in `logos.lcm.deem` and is still linked/used by
`facthistory.logos`; the Canon-plane one must be package-scoped (own module),
not a second bare `create_ctr`.

**The C++ factory chain maps directly onto the three-layer split** (user,
2026-07-17): the TMP factories become metafunctions, Canon does their
reasoning, their results become generation visible in `--gen-dir` files.

| C++ Memoria (TMP)                    | Logos                                          |
|--------------------------------------|------------------------------------------------|
| `CtrTF` (container type factory)     | metafunction — the factory orchestrator        |
| `BTTypes` (b+tree type/mixin select) | metafunction                                   |
| `NodeTF` (packed node struct select) | metafunction                                   |
| partial-spec / `IfThenElse<FIXED,…>` | **Canon** — verdicts with provenance           |
| nested type aliases (invisible)      | **generation** — real source in `--gen-dir`    |

What was compile-time reasoning buried in template error spew becomes
explainable Canon verdicts plus dumped, debuggable generated source.

**TypeFactory oracle checklist** (C++ Memoria `/home/victor/cxx/memoria`, the
canonical repo — `memoria-core` is unrelated) — the family the factory must
emit per config, so a hook that produces only "the container class" is
incomplete: leaf node type(s) + branch node type(s) (driven by the FSE/VLE
fold — plan for >1 of each), the runtime-tag→static dispatcher, the container
class, the traversal shuttles (find/skip/select/rank), and the handle + its
registry-facing ops. Identity/memoization must be **enforced** (register once
per (profile, config); colliding key = hard error, catching "two aliases →
two instantiations" early), keyed structurally (salt + recursive hash of type
args), never by source alias identity — which is exactly the `@hs(CFG)` mangle
+ the link-name algebra give us.

## 4. Migration ladder

Wave-0 stays green while the seam lands; each phase gates on the conuco
memoria suite + targeted ctest slices; full ctest in background per group.

1. **CFG + alias**: `container` declarations additionally lower to a
   WritStatic CFG doc + `type <Name> = ContainerType<CFG>`; decl-time facts
   unchanged. (Handler synthesizes the const+alias pair.)
2. **Demand hook**: mono demand on `ContainerType<CFG>` → factory metacall →
   dispatch round; sema-side harvest replaced by the mono edge.
3. **Factory re-keying**: identity and memoization by `@hs(CFG)`; Canon
   invoked at demand time (memoized); `blob_seen` and nominal name-threading
   between emitters retired; family member names derived from the hash, with
   alias names emitted as a comment header in `--gen-dir` dumps for
   navigability.
4. **Uniform API + retirement**: `create_ctr::<Legend>(&mut snp, ctr_id)` as
   the single instantiation surface (package-scoped — see §5 collision);
   consumers migrated (pre-release, break freely); retire the harvest patch,
   CONTAINER_DEF_DONE re-registration as a visibility mechanism, and the
   concrete-vs-generic bind asymmetry (CoW handle vs standalone `{N}Tree`,
   `sema.cpp:5649-5656`).

**Consumer migration classes** (recon inventory):
- *API rename* (mechanical): `T::create/open(&mut s, id)` →
  `create_ctr/open_ctr::<T>` — store_workflow, gen_cow, gen_cow_memstore,
  and the store-backed half of gen_cow_kv/gen_fse. Caveat: `open_ctr`
  returns `Option` (ADR 0017 precedent), so `::open` sites gain a
  match/unwrap — behavior-visible, not pure rename.
- *Compiler-internal fixtures* (unaffected by the user surface): direct
  `{N}Leaf/{N}Branch/{N}Tree` and standalone `kind node`/`kind branch` users
  (gen_map, gen_branch, gen_node*, gen_tree, and the standalone-tree half of
  several tests). Their own headers state "the declaration is its entire
  source" — they test the factory's codegen contract, not the API. They keep
  **bare, internal, non-source-addressable** family names (navigable via
  `--gen-dir`); they are *not* a `create_ctr` surface and do **not** warrant a
  5th ladder item. `mixed_concrete_generic` is the bind-asymmetry regression
  fixture — its premise dissolves at phase 4; it gets rewritten, not renamed.
- *Unaffected*: `kind vector` (emits only an internal source-trait +
  rel-materializer, no `{N}` family) — ctr_vec_deem, vec.logos, ctr_mod;
  `container_item_e2e/parse` (parse+facts+verdicts-only contract, kept).

### What this unlocks (acceptance gates)

- `conuco/memoria/tests/map_consumer.logos.pending` un-parked: a generic
  `pub container Map<K,V>` in a library (`memoria.ctr.map`), instantiated
  cross-module.
- Twin declarations deduplicate to one family (alias transparency).
- ODR between two consumer archives holds by construction (hash-mangled
  names in the shared link-name algebra).
- Oracle: C++ Memoria (`/home/victor/cxx/memoria`) — behavioral parity for
  the b+tree map family and the instantiation-protocol checklist (§3.1).

## 5. Open questions

- **`create_ctr` name collision** (recon): ADR 0017's `create_ctr`/`open_ctr`
  live in `logos.lcm.deem` (`handle.logos:717/737`) and are still linked and
  used by `facthistory.logos`. The Canon-plane API must be package-scoped in
  its own module (candidate: `logos.mem.bt.map` or a new
  `logos.std.memoria`), never a second bare `create_ctr`. ADR 0017's engine
  is nominally superseded (memoria port) but cannot be deleted while linked;
  coexist by package.
- Naming of the metaclass base type (`ContainerType<CFG>` vs lowercase
  `container<CFG>`); collision with the existing `Ctr` handle trait.
- `catalog` slot shape (runtime DDL descriptor) — deferred to the DDL arc.
- When the second metaclass appears (schemas? rel-modules for Nous?), what
  of the protocol needs to become user-visible surface vs stays internal.
- Store selection for instantiations (ADR 0020 §9 store profile) — the CFG
  doc is the natural carrier (ADR 0017 already threads a second
  `const STORE_CFG: WritStatic`), not decided here.
