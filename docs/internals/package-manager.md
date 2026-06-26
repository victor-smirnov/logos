# Package Management in lforge

This document describes how `lforge` resolves, fetches, builds, and caches external dependencies. **Status (2026-05-07): shipped end-to-end.** B2..B5 (local paths, git URLs, lockfile + MVS, build cache, replace + requires_logos) are implemented and exercised by [tests/lforge/](../../tests/lforge/). The first external `.logos` package — `github.com/victor-smirnov/lforge-hello-world` — is live. See [internals/lforge.md](lforge.md) for the user-facing manifest schema and CLI; this page focuses on the model and the rationale.

The model deliberately rejects two common shapes:

- **Maven-style central registry** with prebuilt platform-specific artifacts is wrong for a compiled language with metaprogramming: ABI changes whenever the compiler does, packages exporting metaprog handlers must be rebuildable from source, and a central server is a single point of governance and outage we don't want.
- **vcpkg-style overlay ports** carry too much CMake legacy and a parallel package universe maintained by a different team. We have neither problem.

Instead: source distribution, projects = git repositories, lforge does everything (resolve, fetch, build, cache, link). No separate package-manager binary, no separate registry server.

## The Model

A **project** is a git repository containing exactly one `lforge.hermes` at the root. The project is the unit of versioning, identity, and release cadence. A project may ship multiple build *artifacts* (libs, bins) — those share the project's release cycle. If two artifacts need independent release cycles, they live in different repositories.

A **dependency** names a project (by git URL), a version (tag, branch, or SHA), and which of the project's modules the consumer wants linked. lforge fetches the source, builds it locally with the consumer's compiler version, caches the result, and links it.

There is no separate "package author" role. Publishing = pushing a tagged commit to git. Discovery is left as a non-goal in v1; later we can sketch a curated index repo (awesome-list style) but it isn't part of the core protocol.

## Identity

A project is identified by its git URL. Several forms are accepted in the manifest, all resolve via `git fetch`:

```
github.com/anthropics/logos-http        # Go-style — implied https://github.com/...
https://git.example.com/team/lib.git    # explicit https URL — any git host
ssh://git@git.example.com/team/lib.git  # explicit ssh URL
git@github.com:anthropics/logos-http    # SCP-style ssh shorthand
```

GitHub `https` gets the short form because that's where ~95% of code lives. SSH forms are first-class — required for private repos and self-hosted git. lforge does not implement transport itself: it shells out to `git`, which inherits whatever credential helpers / SSH agent / `~/.ssh/config` the user already has configured. No lforge-specific auth config.

A consequence: the same project can be referred to via either `https` or `ssh`. lforge canonicalises to a single form for cache and lockfile keys (recommended: `<host>/<path>` stripped of scheme, port, and `.git` suffix) so the same project pinned via `https` in CI and via `ssh` on a developer's laptop hits the same cache entry.

Identity is stable across hosting moves. If a project relocates, the manifest gains a `replace` entry — the consumer's manifest doesn't change. (Same shape as Go's `replace` directive.)

## Versions

Three forms are accepted at the manifest level:

```
{ project: "github.com/x/lib", tag:    "v1.2.3" }
{ project: "github.com/x/lib", branch: "main"   }
{ project: "github.com/x/lib", sha:    "abc1234567..." }
```

Tag is the human-friendly default; branch is for tracking work-in-progress; SHA is for absolute pinning. SemVer ranges (`^1.2`, `>=1.0,<2.0`) are explicitly **not** in v1 — see "Diamond conflicts" below for why we don't need them.

At lock time, every manifest version (whatever form) is resolved to a concrete commit SHA. The lock file is the source of truth for reproducibility:

```hermes
{
    pinned: [
        { project: "github.com/x/lib",
          tag:     "v1.2.3",      // for human readers
          sha:     "abc1234567def...",
          fetched: "2026-05-07T12:34:56Z" }
    ]
}
```

Tags can be force-pushed; SHAs cannot. Verification is by SHA. The tag is kept in the lock for diff readability — when `cat lforge.lock` shows `v1.2.3 → v1.3.0`, the human can guess what changed without crawling git.

## Diamond Conflicts

If A depends on lib at v1.0 and B depends on lib at v1.1, both are in the dependency closure. The resolution is **highest version wins** (Go MVS — Minimum Version Selection's "max of mins" rule).

Concretely:

- For each project, lforge collects every version mentioned anywhere in the closure.
- It sorts them (semver-ish lexical, with SHAs treated as opaque equal-or-not).
- The highest is selected and built once.
- If the build fails or a downstream consumer's API expectation is violated at type-check time, the user is shown the full transitive path and a `replace`-directive instruction.

This avoids needing a SAT solver, a constraint language, or version ranges. The cost is "no theoretical guarantee that picking max is safe" — but in practice if `v1.0` and `v1.1` are both in the closure, somebody has been making semver-compatible changes, and if they haven't, the type checker will catch it. Cargo and npm pay a heavy resolver cost to defer the same failure to a different layer. Go-MVS is simpler and the failures are no worse.

Side-by-side install of multiple versions is **not** supported. Two versions of `lib` linked into one binary breaks metaprog symbol resolution and doubles binary size for no semantic gain. If the consumer truly needs two incompatible APIs, they vendor one or fork.

## Manifest

`lforge.hermes` already describes a project's build (name, version, targets). Package metadata extends it with new keys; no second file:

```hermes
{
    name:      "http-client",
    version:   "1.2.3",
    project:   "github.com/anthropics/logos-http",   // self-identity
    license:   "Apache-2.0",
    authors:   ["Foo Bar <foo@example.com>"],

    // Compiler ABI floor. Build is rejected if logosc is older than this.
    requires_logos: "0.1",

    deps: [
        { project: "github.com/anthropics/logos-json",
          tag:     "v0.4.0",
          modules: ["json"] },         // pull only the `json` target

        { project: "github.com/anthropics/logos-regex",
          sha:     "deadbeef...",
          modules: ["regex"] },

        // Local-path dep for parallel development. Skips fetch / cache.
        { path:    "../my-other-project",
          modules: ["util"] }
    ],

    // Identity remap — for projects that moved hosts or were forked.
    replace: [
        { project: "github.com/old-org/lib",
          with:    "github.com/new-org/lib" }
    ],

    targets: [
        { kind: "lib", name: "http", src: "src/http" },
        { kind: "bin", name: "http-cli", src: "src/cli", entry: "main",
          deps: ["http"] }
    ]
}
```

Every existing key (`name`, `version`, `targets`, `src`, `entry`, …) keeps its current meaning. The new keys are additive: a project that doesn't depend on anything external never writes `deps`/`replace`/`requires_logos`.

## Cache

Build artifacts live under `~/.cache/lforge/`, content-addressed:

```
~/.cache/lforge/
  src/                                   # cloned project trees
    <project-id-hash>/<sha>/             # one tree per (project, commit)
      lforge.hermes
      src/...
  build/                                 # compiled artifacts
    <build-key>/                         # one entry per cache hit
      out/lib<module>.a
      meta.hermes                        # what produced this
```

The `build-key` is `sha256(project_id + sha + module_name + compiler_version + opt_level + relevant_flags + dependency_build_keys)`. Any change in any input → fresh entry. Cache reads check `meta.hermes` to confirm the entry actually matches the current build (defence against hash collisions and corrupt cache).

The cache is per-user and never shared across users by default — a build host that wants to share fills `~/.cache/lforge/build/` from a tarball and trusts the contents (see "Out of scope" below).

The clone cache (`src/`) is keyed by `(project_id, sha)` so multiple consumers depending on the same `(repo, sha)` clone once. SHAs are immutable, so cache invalidation is never needed for clones — only on `lforge cache prune`.

## Workflow

```
$ lforge build
```

1. Read `./lforge.hermes`.
2. If `lforge.lock` exists and is consistent with manifest deps:
   - Use locked SHAs. Done resolving.
3. Else (fresh project, or manifest deps changed):
   - For each dep, resolve `tag`/`branch` to SHA via `git ls-remote`.
   - Recursively load each dep's `lforge.hermes` (clone if not in cache).
   - Run MVS over the closure: per project, max version wins.
   - Write `lforge.lock` with the chosen SHAs.
4. For each project in dependency order:
   - Compute the build-key.
   - If `~/.cache/lforge/build/<key>/` exists, reuse.
   - Else build the requested modules, write the cache entry.
5. Link the consumer's targets against the resolved archives.

```
$ lforge update            # re-resolve, ignore lockfile
$ lforge update <project>  # re-resolve only that project
$ lforge cache prune       # GC unreferenced cache entries
```

`lforge build` is the only path that auto-generates a lockfile. `lforge update` is the only path that bumps locked versions. CI runs `lforge build`; humans run `lforge update` deliberately.

## What v1 Doesn't Do

These belong on the roadmap but explicitly aren't in the v1 scope:

- **Build hooks / pre-build scripts.** Arbitrary code execution at build time = supply-chain attack surface. v1 packages are pure source + manifest.
- **Cross-compilation.** v1 builds for the host platform only. Cross-builds need either prebuilt sysroots or full rebuild of stdlib for each target — both are post-v1.
- **Discovery / search.** No registry, no index. A separate "awesome-logos" curated repo can come later.
- **Yanking, deprecation, mirror trust.** All trust delegated to git (push/force-push policy is the project owner's).
- **Artifact signing.** Git provides commit-SHA integrity; SHA-pinned lockfiles propagate it. Signed releases (sigstore-style) are a separate later epic.
- **Private repos beyond what git already supports.** SSH/HTTPS auth = git's problem; lforge calls `git fetch` and inherits.
- **Vendoring.** A `lforge vendor` that copies the resolved closure into `vendor/` for offline builds is useful but not v1.

## Implementation Status

All v1 milestones shipped on 2026-05-07. Each step landed independently and ships its own test under [tests/lforge/](../../tests/lforge/):

1. **B2 — local-path deps.** `deps: [{ path: "../foo", modules: [...] }]`. No fetching, no cache, no SHA.
2. **B2.5 — git URL deps.** `{ project, tag/branch/sha }` for bare host/path, `https://`, `ssh://`, and SCP-shorthand forms; clone into `~/.cache/lforge/src/<flat>/<sha>/`.
3. **B3 — lockfile + MVS.** Closure walk with diamond detection (highest-version-wins, errors on lower-version-after-higher), lockfile generation, `lforge update`.
4. **B4 — build cache.** Content-addressed `~/.cache/lforge/build/<sha256>/`, key includes `logosc` mtime + sub-dep cache_keys (transitive invalidation). Cross-consumer share verified.
5. **B5 — replace + requires_logos.** Local override of git deps (root manifest only), compiler ABI floor compared via `logosc --version`.

`lforge cache prune` (cache GC) is **not** in v1 — entries accumulate. That's the only piece of the original v1 list still pending.

## Why Not …

**Why not a central registry like crates.io?** It's a single point of failure, governance, and outage. It also requires running infrastructure that someone has to pay for and maintain. Distributed git solves the problem with already-present infra.

**Why not Maven?** Prebuilt platform binaries don't work for a language with metaprogramming where ABI tracks compiler version. A package shipping a `derive_clone` handler must be source-compiled by the consumer's `logosc` to be ABI-compatible.

**Why not vcpkg ports?** vcpkg's complexity comes from CMake variance, multi-target install layouts, and 20+ years of legacy build systems. We have one build system (lforge) and one binary format (Logos archive). The vcpkg model fits, but the implementation overhead doesn't.

**Why not Go modules?** This is mostly Go modules: git URL identity, MVS resolver, no central registry, lockfile ≈ `go.sum`. Differences:
- Hermes-SDN manifest, not `go.mod` syntax.
- One repo = one project (no submodule paths).
- Module selection per dep (Go pulls the whole module; we pull listed targets only).

**Why not Cargo?** Cargo's central registry is an active liability, semver ranges plus PubGrub is a complex resolver for diminishing returns over MVS, and `crates.io` ABI assumptions don't hold for source-distributed compiled code with metaprog.

The shortest description: **Go modules with a Hermes manifest.**
