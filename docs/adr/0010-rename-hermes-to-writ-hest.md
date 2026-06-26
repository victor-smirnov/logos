# ADR 0010 — Rename Hermes → Writ (data substrate) + Hest (protocol family)

**Status:** Accepted (decisions locked 2026-06-26; execution pending — incremental on `main`)
**Date:** 2026-06-26

## Context

"Hermes" is being retired as the name for the data substrate. Two reasons:
the word is crowded (Meta's Hermes JS engine, Hermès, …), and "Hermes =
messenger" mis-describes a *storage* format (a messenger carries; it does not
store). See the trademark register in [TRADEMARKS.md](../../TRADEMARKS.md).

## Decision — name map

| Old | New | Notes |
|-----|-----|-------|
| Hermes (the data format / substrate: containers + serialization + API) | **Writ** | OE *gewrit*, "authoritative written record". Plain `Writ` — no `<D>` param (holds a dynamically-typed `WAny` root). |
| The `#[zoned]` memory model (never-move, RelPtr, arenas) | **zones** (unchanged) | Stays the descriptive concept; `#[zoned]`/`ZonedStruct` keywords NOT renamed. |
| HRPC (Hermes RPC) / the RPC + messaging layer | **Hest** (umbrella) | Hest = the *family* of interaction protocols the language/compiler/tools integrate with. **HRPC stays** as a name/dir/ext — it is one protocol in the Hest family, now expanded "Hest RPC" (the "H" was Hermes, now Hest). |
| Memoria | Memoria (unchanged) | Out of scope. |

### Symbol/type map (C++ and Logos)

| Old | New |
|-----|-----|
| `namespace logos::hermes` | `logos::writ` |
| `Hermes` (container) | `Writ` |
| `HAny` | `WAny` |
| `HView2` | `WView2` |
| `HermesCtr` | `WritCtr` |
| `HermesAccess` | `WritAccess` |
| `HermesStatic` | `WritStatic` |
| `hermes_new` / `hermes_rc` | `writ_new` / `writ_rc` |
| grammar tokens `HERMES_*` (e.g. `HERMES_INT`, `HERMES_TYPED_MAP`) | `WRIT_*` |
| arena magic bytes `"HERMES"` | `"WRIT"` |

Note: the user-facing literal *syntax* (`@{...}`, `@[...]`, `@42`, `#[zoned]`)
does NOT change — only internal token/type names.

### Package paths

| Old | New |
|-----|-----|
| `logos.lang.hermes.*` (19 pkgs) | `logos.lang.writ.*` |
| `logos.mem.hermes.*` (8 pkgs) | `logos.mem.writ.*` |

C++ dirs: `src/hermes`→`src/writ`, `include/logos/hermes`→`include/logos/writ`.
`src/hrpc` + `include/logos/hrpc` **stay**.

### On-disk format markers / extensions

| Old | New | Carries |
|-----|-----|---------|
| `.hermes` | `.writ` | SDN text manifests (incl. `lforge.hermes` → `lforge.writ`) |
| `.hermes0` | `.writ0` | serialized AST blob |
| `.hm0` | `.wr0` | LIR blob (compaction) |
| `.hbs` | `.wbs` | binary serialization |
| `.hrpc` | `.hrpc` (unchanged) | HRPC IDL |

## Scope (surveyed 2026-06-26)

~7,460 "hermes" hits: src 2503/54f, include 762/45f, stdlib 731/57f, tools
493/31f (lforge), tests 1809/278f, docs 1080/96f, examples 83/7f. Archives
`liblogos-{lang,mem,std}.a` keep their names (modules are tier-level); only
per-symbol mangling changes when package paths change → **ABI break**, refresh
required (pre-release 0.x, no compat promise).

## Migration phases (each ends build- and L2-green)

The compiler **hardcodes stdlib package strings** (e.g. generated metacall
thunks emit `use logos.mem.hermes.view;`), so a package-path rename must be
atomic between compiler-emitted strings and the stdlib dirs.

- **P0 — Freeze & branch.** No other large in-flight Hermes work; lock this name
  map; branch. Snapshot ABI (`--emit-abi`) as baseline.
- **P1 — C++ internals.** Rename `src/hermes`→`src/writ`, `include/logos/hermes`
  →`include/logos/writ`, `namespace logos::hermes`→`writ`, types
  `Hermes*`/`H*`→`Writ*`/`W*`, magic bytes. Stdlib package strings stay
  `logos.*.hermes.*` for now. Build C++; run C++ exercisers + L2.
- **P2 — Package paths + ABI.** Atomic: rename stdlib `logos.{lang,mem}.hermes.*`
  → `…writ.*` (dirs + `package` decls + every `use` site in stdlib/tests/
  examples/lforge) **and** the compiler's hardcoded path strings (sema thunk
  emit, prelude, intrinsics, schema). Rebuild stdlib; refresh ABI spec; run
  semantic abi-diff vs P0 baseline (expect prefix-only diffs).
- **P3 — Format markers / extensions.** `.hermes`/`.hermes0`/`.hm0`/`.hbs` +
  magic + `lforge.hermes`→`lforge.writ`: update compiler emit/loader, lforge
  (manifest reader, lockfile, build paths), test fixtures, docs. Regenerate any
  committed blobs.
- **P4 — HRPC internals.** Hermes-type refs in `src/hrpc`→Writ; prose
  "Hermes-native/Hermes RPC"→"Hest RPC". Dir/name/`.hrpc` ext unchanged.
- **P5 — Docs.** `language/hermes.md`→`writ.md`, `internals/hermes-runtime.md`→
  `writ-runtime.md`; `hrpc.md` stays (reframe as "Hest / Hest RPC"); fix all
  links + READMEs + DIVERGENCES; introduce the "Hest = protocol family, HRPC =
  one case" framing.
- **P6 — Validate.** Full L4 ctest, examples run, Docker build, semantic
  abi-diff. Update memory ([[project_hermes_rebrand_writ_hest]]).

## Compatibility

No shims/aliases — pre-release (0.x), internal consumers only; a clean break
avoids carrying dead `…hermes.*` re-exports. (Option on the table: keep
`logos.*.hermes.*` as thin re-export aliases for one release if any external
`.logos` exists — currently none known.)

## Code cleanup (rider track)

The rename touches the whole subsystem, so it is a natural moment to clean up —
but **mechanical-rename commits MUST stay pure** (a 7k-hit sed-style rename mixed
with semantic edits is unreviewable and unrevertable). Rule: rename in one commit
per phase; cleanup in *separate adjacent commits*.

The subsystem is already fairly clean (scan 2026-06-26: 0 TODO/FIXME/HACK/
deprecated in core dirs; ~7 "retired" + 2 "legacy" comments). Candidate cleanups:

- Drop now-pointless **"Hermes1 … is retired" / "at-rest-value split is retired"**
  comments (anyval.logos, check.logos, hashing.logos, equal.logos) — the Hermes1
  era is gone; with Hermes itself renamed these read as archaeology.
- Assess the **9 `src/hermes/exerciser_*.cpp`** standalone drivers ("predate full
  Logos coverage"): delete the obsolete ones, port the still-useful to `src/writ`.
- Remove any dead symbols/paths the rename surfaces (unreferenced helpers, the
  `mem.hermes2.*` vestigial references if any remain).
- Re-confirm the lang/mem tier split is clean post-rename (no `logos.mem.writ.*`
  leaking lang-tier primitives or vice-versa).

Sequencing: do a cleanup pass per phase *after* its rename lands green, or fold
into a dedicated **P7 — cleanup** at the end. Either way, separate commits.

## Decisions (locked 2026-06-26)

1. **`lforge.hermes` → `lforge.writ`** — YES. Rename the manifest filename
   (pre-release; visible break accepted).
2. **Format extensions + magic** — YES, clean break: `.hermes0`→`.writ0`,
   `.hm0`→`.wr0`, `.hbs`→`.wbs`, magic `HERMES`→`WRIT`. Regenerate committed/cached
   blobs; no on-disk back-compat.
3. **Type prefix** `H*`→`W*` — YES (`HAny`→`WAny`, `HView2`→`WView2`), per the
   symbol map above.
4. **Execution** — **incremental on `main`**: land P1…P6 phase by phase, each a
   green commit (build + L2). No long-lived mega-branch.
5. **Cleanup** — per-phase, in **separate commits** after each phase's rename is
   green (rider track above); not folded into the mechanical-rename commits.
6. **No compat shims** — clean break (pre-release, no known external `.logos`).
