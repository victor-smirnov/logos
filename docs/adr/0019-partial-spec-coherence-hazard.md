# ADR 0019 — Partial specialization of structs: the coherence hazard

Status: ACCEPTED as a RISK REGISTER entry (user, 2026-07-11). The feature ships
and stays (DIVERGENCES.md §A14); this ADR records the hazard it carries into
stabilization, the mitigations to evaluate, and the rollback shape if none
suffice. «Пока же, живем с тем, что сделали.» Date: 2026-07-11.

## 1. Context

Logos diverges from Rust with C++-style partial specialization of struct
DEFINITIONS (§A14): a second `struct Name<pattern>` declaration selects a
distinct FIELD SET + impl family for matching instantiations. Two selector
forms exist:

- **shape patterns** — `struct PkdArray<[E]>` (structural: slices);
- **bound patterns** — `struct PkdArray<T: Copy + Fst + StableLayout>` after
  a primary `struct PkdArray<T: ?Sized>`: selection by the PROPERTIES of T,
  the C++ `template<typename T, bool FSE = is_fse<T>>` predicate spelled as a
  bound set. This is the form memoria's `PkdArray` uses: `PkdArray<u64>` →
  FSE (inline typed tail), `PkdArray<str>` → VLE (allocator slots) — no shape
  spelling at use sites.

Rust rejects both mechanisms deliberately: one struct definition per name,
impl coherence (E0119), no negative bounds. The reason is not implementation
difficulty — it is the hazard below.

## 2. The hazard: trait impls act at a distance on LAYOUT

Bound-pattern selection makes `impl Trait for Foo` an input to the MEMORY
LAYOUT of unrelated types. Concretely:

- Someone adds `impl StableLayout for Foo` (plus Copy/Fst already holding).
- Every `PkdArray<Foo>` in the program SILENTLY changes family: VLE → FSE.
  Different fields, different sizes, different method bodies, different
  symbols.

Within ONE compilation this is deterministic and sound: sema and mono gate
selection against the same registered impl set, instances are chosen at one
point, and the two families' structurally-identical methods carry distinct
symbols (the `$where$<sorted traits>` fingerprint). The failure modes appear
the moment two compilations can disagree about the impl set:

1. **Binary/ABI skew.** A library compiled when `Foo: StableLayout` did not
   hold embeds VLE-layout `PkdArray<Foo>` in its ABI. A consumer compiled
   after the impl lands selects FSE. Same mangled type name era-to-era ≠ same
   layout; data written by one side is garbage to the other. (Symbols
   partially defend: the *methods* differ by `$where`, so calls miss rather
   than corrupt — but layout-embedding contexts, field offsets baked into
   consumers, serialized images, and `sizeof` constants do not.)
2. **Semver trap.** Adding a trait impl is today an ABI-additive change
   (abi-diff: minor). Under bound-selection it can be a SILENT layout-major
   change to downstream types the impl author has never seen. The abi-diff
   gate as it stands would not flag it.
3. **Orphan-shaped variant.** With a package graph, package C can implement
   package A's trait for package B's type and thereby change the layout of
   `A::PkdArray<B::Foo>` for everyone — the exact action-at-a-distance
   Rust's orphan rules exist to forbid.

Memoria itself is the benign case that motivated the feature: there,
"implementing the FSE contract densifies your arrays" is the DESIRED
semantics, everything is in-tree, and the pre-release rule («ломаем всё»)
holds. The hazard activates at stabilization: external clients, binary
modules with N−1 compatibility windows, and per-slot installs (ADR versioning
scheme) all assume a type's identity pins its layout.

## 3. Decision (now)

Live with it as-is through the pre-release period. No restrictions yet —
memoria and future in-tree users exercise the feature freely and teach us its
real shape. This ADR is the tripwire: **the stabilization checklist (tools-
stable stage of the versioning plan) MUST resolve §4 before bound-pattern
specs are allowed in any ABI-stable surface.**

## 4. Mitigations to evaluate at stabilization (in preference order)

1. **Sealed-selector rule (orphan-style).** A trait usable in a bound pattern
   must be declared `#[layout_selector]` (or: must live in the same package
   as the specialized struct). Implementing a layout-selector trait for a
   type follows the same rules as adding a FIELD to that type: it is a
   layout-major change, abi-diff flags it, and cross-package impls are
   rejected (no orphan selection). This keeps the feature but pins the
   blast radius to the package that owns the array type — likely sufficient
   for the Memoria pattern (Fst/StableLayout/Copy are stdlib-owned marker
   traits with compiler-validated semantics; they change rarely and
   deliberately).
2. **Fingerprint the selection into the TYPE identity.** Mangle the chosen
   family into the instance name (`PkdArray$G1$u64` → `PkdArray$fse$G1$u64`
   or reuse the `$where` fingerprint at the STRUCT level, not just methods).
   Era-skewed binaries then FAIL TO LINK instead of corrupting — degrades
   failure mode from silent to loud, composes with mitigation 1. Cost: any
   impl addition that flips selection is an immediate link break (which is
   the point).
3. **abi-diff selector awareness.** Teach `--emit-abi`/abi-diff that a new
   impl of a bound-pattern-referenced trait is layout-affecting: enumerate
   all bound-pattern specs in the ABI surface, record their selector traits,
   and classify new impls of those traits as MAJOR. Weakest alone (detects,
   doesn't prevent), necessary complement to either of the above.
4. **Rollback shape** (if the above prove insufficient): restrict struct
   specialization to SHAPE patterns only (`[E]` — structural, impl-set-
   independent, no action at a distance) and rehost bound-selection as the
   assoc-type facade (`trait PkdElem { type Repr; }` + alias), where adding
   an impl is at least a VISIBLE choice on a trait with the alias in its
   signature, and where Rust-style coherence machinery applies unchanged.
   Memoria's use-site ergonomics (`PkdArray<u64>` / `PkdArray<str>`) survive
   under 4 only partially (alias projection covers type positions; struct
   literals and field access degrade) — which is why 1+2+3 are preferred.

## 5. Consequences

- Until stabilization: none in practice (in-tree, no external clients, full
  rebuilds). CI keeps the feature honest via the ctest partial-spec family
  and the sd_dst_mod module fixture.
- The versioning/ABI plan (per-slot installs, N−1 window) gains a checklist
  item: bound-pattern specs × binary modules = resolve §4 first.
- DIVERGENCES.md §A14 links here; any future `#[layout_selector]`-style
  restriction is an amendment to this ADR, not a new divergence.
