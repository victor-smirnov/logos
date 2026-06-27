# lforge — Logos Build System

`lforge` is the Logos build system — a binary written in Logos that orchestrates `logosc` and `cc` to turn a Logos project into an executable.

**Status (2026-05-07):** toolchain MVP reached. `lforge` covers project builds, multi-target manifests, native (C/asm) sources, transitive git deps, MVS conflict detection, lockfile pinning, content-addressed build cache, `replace:` overrides, and `requires_logos:` ABI floor. The first external `.logos` package — `github.com/victor-smirnov/lforge-hello-world` — is live. Daemon mode, file watcher, MCP/LSP integration are still open; see [Roadmap](#roadmap) below.

## Why a Build System

Until B0, building a Logos program meant invoking `logosc` directly with the right flags, then `cc` to link the produced object against the stdlib archives. That is the right primitive for testing the compiler, but not for shipping software:

- A real project has multiple source files, packages, and dependencies — the user shouldn't enumerate them manually.
- AI authors and IDEs need a single point of contact that knows the full project state.
- Incremental rebuild requires content-addressed caching of compile actions; the compiler doesn't have a place for that.
- Self-hosting eventually means lforge builds the compiler itself.

`lforge` is the named orchestrator the [SOA-compiler vision](../adr/0004-definition-centric-tu.md) was always implying.

## Quick Start

A minimal project:

```
my-project/
  lforge.writ        # manifest
  src/
    main.logos         # entry
```

Manifest (Writ-SDN text format):

```writ
{
    name:    "my-project",
    version: "0.1.0",
    targets: [
        { kind: "bin", name: "my-project", src: "src", entry: "main" }
    ]
}
```

Source:

```logos
package my_project;
fn main() -> i32 { return 0; }
```

Build, run, install, test, update:

```bash
lforge build              # produces .lforge/debug/out/my-project
lforge run                # build + execute, exit code propagates
lforge run -- arg1 arg2   # forward args to the binary
lforge build --release    # release profile under .lforge/release/
lforge test               # walks tests/, compiles + runs each .logos
lforge install            # copies bins/libs to <prefix>/{bin,lib}
lforge update             # re-resolve git deps, rewrite lforge.lock
lforge clean              # rm -rf .lforge/
lforge version            # print "lforge 0.1.0"
```

The build system finds `logosc` and the Logos lib dir via `$LOGOSC` and `$LOGOS_LIB_DIR`, falling back to in-tree `./build/bin/logosc` and `./build/lib/logos` so a fresh checkout's `lforge` works against the in-tree compiler. **Note:** when invoking lforge from outside the Logos repo, set `LOGOSC` and `LOGOS_LIB_DIR` explicitly — without them the bare-name `logosc` lookup against PATH usually fails.

## Manifest Schema

The manifest is a Writ document in text (SDN) form. Top-level fields:

| Field            | Type                  | Required | Meaning                                                          |
|------------------|-----------------------|----------|------------------------------------------------------------------|
| `name`           | string                | yes      | Project name. Informational; targets carry their own names.      |
| `version`        | string                | yes      | Project version (informational).                                 |
| `requires_logos` | string                | no       | Minimum compiler version (`"X.Y[.Z]"`). Compared against `logosc --version`; lforge errors out if older. |
| `targets`        | array of Target       | yes¹     | Build artifacts the project produces.                            |
| `deps`           | array of ExternalDep  | no       | Project-level dependencies (local path or git).                  |
| `replace`        | array of ReplaceEntry | no       | Local overrides for git deps (root manifest only).               |

¹ Legacy schema with top-level `src` + `entry` (no `targets`) still parses for backwards compat: it synthesizes a single bin target.

### Target

```writ
{
    kind:        "bin" | "lib",
    name:        "<name>",
    src:         "src/<dir>",          // dir of .logos files (may be empty for native-only libs)
    entry:       "main",               // bin-only — entry file is <src>/<entry>.logos
    deps:        ["sibling-lib", ...], // names of sibling lib targets
    c_sources:   ["foo.c", ...],       // optional, project-root-relative
    asm_sources: ["foo.S", ...]
}
```

C and asm sources are compiled with `cc -c <src> -o <stem>.<ext>.o` and folded into the same archive as Logos artifacts.

### ExternalDep

Either local-path or git, exactly one. For git deps, exactly one of `tag` / `branch` / `sha`:

```writ
{ path: "../my-other-project", modules: ["util"] }
{ project: "github.com/foo/bar",     tag:    "v1.0", modules: ["http"] }
{ project: "github.com/foo/bar",     branch: "main", modules: ["http"] }
{ project: "https://git.example.com/team/lib.git", sha: "abc1234...", modules: ["lib"] }
{ project: "git@github.com:foo/bar", tag:    "v1.0", modules: ["http"] }
```

URL forms recognised: bare host/path (Go-style, gets `https://` prefix), explicit `https://` / `http://`, `ssh://` (with optional `user@`), SCP shorthand `user@host:path`. All canonicalise to the same display id (`<host>/<path>` minus scheme and `.git`), so a project pinned via `https` in CI and via `ssh` on a developer's laptop hits the same cache entry.

`modules` is the list of lib targets to link from that project; lforge builds only those.

### ReplaceEntry

Local override for a git dep. Match key is the canonicalised display id; substitution is a local path:

```writ
replace: [
    { project: "github.com/foo/bar", path: "../my-fork-of-bar" }
]
```

Useful for working on a fork, vendored copies, or temporarily redirecting to a sibling checkout. Only the **root** manifest's `replace[]` applies; nested deps' replace entries are ignored (mirrors Go MVS semantics).

## Build Output Layout

```
my-project/
  lforge.lock                          # auto-generated when deps[] is non-empty
  .lforge/
    <profile>/                         # debug | release
      _gen/<lib>.module                # generated lib manifest
      _files/<lib>/<stem>.{o,writ0}  # per-file artifacts (incremental)
      out/<bin>                        # linked executable
      out/<bin>.o                      # bin entry-point object
      out/lib<lib>.a                   # lib archive
      test/<test>.{o,bin}              # per-test artifacts under `lforge test`
```

`debug` is the default profile; `release` is selected with `--release`. Per-file `.o`/`.writ0` artifacts make rebuilds incremental: the action runs only if the source's mtime is newer than the output's. Re-archiving runs only if any per-file artifact rebuilt or the archive itself is stale.

## Build Cache (B4)

Cross-consumer cache for external git deps lives in `~/.cache/lforge/`:

```
~/.cache/lforge/
  src/<flat-display>/<sha>/      # cloned project trees, one per (project, commit)
  build/<sha256-hex>/            # compiled artifacts, one per cache key
    lib<module>.a
    meta.writ                  # what produced this entry
```

Build cache key:

```
sha256( project_display || git_sha || module_name || profile ||
        logosc_binary_mtime || sub_dep_cache_keys )
```

A bump to `logosc` changes the mtime → fresh keys → forced rebuild. Two consumers depending on `foo @ v1.0.0` with the same compiler share the cache: the first one populates, the second's build skips the per-file compile and links directly against the cache archive. Sub-dep keys are folded in transitively, so a change deep in the closure invalidates everything that depends on it.

Local-path deps skip the cache (no stable content identity yet).

The clone cache (`src/`) is keyed by `(canonicalised display, sha)` so multiple consumers on the same `(repo, commit)` clone once. Display canonicalisation means `https://github.com/foo/bar`, `github.com/foo/bar`, and `git@github.com:foo/bar` all share the same clone.

## Diamond Conflicts (MVS)

If A depends on `lib @ v1.0` and B depends on `lib @ v1.1`, both are in the dep closure. lforge uses Go-MVS — highest-version-wins, no SAT solver, no constraint language.

Implementation: post-order walk of the closure (deepest dep first). Each project resolves to the first version encountered; if a later, higher version is requested, lforge errors out with a request to use a `replace:` entry. Lower-version requests after a higher already won are silently absorbed (the higher already covers them).

This is the spec'd behaviour for v1; full MVS with automatic upgrade is deferred.

## Lockfile

`lforge.lock` is auto-written when the manifest has `deps`. Pinned by SHA, with `tag` preserved for diff readability:

```writ
// Auto-generated by lforge. DO NOT EDIT.
{
    pinned: [
        { project: "github.com/foo/bar", tag: "v1.0.0",
          sha:     "abc1234567890..." }
    ]
}
```

Tags can be force-pushed; SHAs cannot. Verification is by SHA. The tag is kept for human readability — when a diff shows `v1.2.3 → v1.3.0`, the reader can guess what changed without crawling git.

`lforge build` consults the lockfile and uses pinned SHAs unconditionally — no `git ls-remote` happens for already-locked deps. `lforge update` rewrites the lockfile by re-resolving every git dep, ignoring the existing pins.

## Diagnostics

`lforge` invokes `logosc` with default text-format diagnostics in MVP. The flag for structured output is `--diag-format=json` (NDJSON, one diagnostic per line):

```json
{"level":"error","file":"src/main.logos","line":3,"context":"fn main","message":"type mismatch — expected i32, got &[u8]"}
```

`logosc` also defines structured exit codes that future lforge code will use to classify failures programmatically:

| Code | Constant          | Meaning                                                      |
|------|-------------------|--------------------------------------------------------------|
| 0    | `EXIT_OK`         | Success                                                      |
| 1    | `EXIT_USER_ERROR` | sema / mono / borrow-check / lir error in user code          |
| 2    | `EXIT_USAGE`      | Bad CLI args, manifest parse failure                         |
| 3    | `EXIT_CODEGEN`    | mlir-gen / lowering failure (reserved)                       |
| 4    | `EXIT_LINK_IO`    | Module loader / archive read failure                         |
| 5    | `EXIT_ICE`        | Internal consistency check (reserved)                        |

## Output Streaming

When `lforge` spawns `logosc` and `cc` with output capture, it reads their stdout/stderr line-by-line via [`Child.stdout_lines()` / `Child.stderr_lines()`](../../stdlib/std/process/process.logos), iterating `LineReader: Iterator<String>`. For the parallel per-file compile fan-out, `spawn_inherit` lets children write directly to the parent's fds — output is per-line atomic on libc-line-buffered streams, which is good enough for build progress without multiplexing.

The reader is currently blocking-read; when the io_uring reactor gains pipe-poll integration, it becomes fiber-yielding without API changes.

## Architecture

`lforge` is split across an entry point (`tools/lforge/main.logos`, ~225 LOC) and 15 sub-packages under `tools/lforge/pkg/` totalling ~2900 LOC. Sub-packages build into a single archive `liblforge_pkg.a` via `logosc --emit-module`; the entry point links against it. The split is documented in [memory: lforge multi-file scaffold](../../tools/lforge/pkg/) — adding a new sub-package is one new `.logos` file plus a `use lforge.<name>;` line in `main.logos`.

Sub-packages (load-order topological):

| Package                 | Role                                                                  |
|-------------------------|-----------------------------------------------------------------------|
| `lforge.schema`         | Struct types: TargetKind, Target, ExternalDep, ExternalLib, LockEntry, GitWinner, Manifest, ManifestError, ReplaceEntry |
| `lforge.util`           | str_eq_local, err_msg, err_msg2, print_str, append_path                |
| `lforge.writ_io`      | read_str_field/opt/array over WritView                               |
| `lforge.cwd`            | save_cwd, chdir_str, make_absolute                                     |
| `lforge.manifest`       | parse_manifest, read_manifest                                          |
| `lforge.proc`           | push_arg, run_child, run_child_capture_stdout                          |
| `lforge.lockfile`       | parse/read/lookup/write_lockfile, cmp_tags, check_git_winner            |
| `lforge.git_dep`        | canonical_url, git_resolve_sha, git_clone_at_sha, resolve_git_dep      |
| `lforge.topo`           | find_target_index, external_lookup, topo_order                         |
| `lforge.build_paths`    | resolve_logos_lib_dir/_logosc_path, output paths, mtime freshness      |
| `lforge.spawn_helpers`  | spawn_inherit, per-file path helpers                                    |
| `lforge.builder`        | build_lib, build_bin                                                    |
| `lforge.external_build` | build_external + build_all_externals (closure walk)                    |
| `lforge.cmd`            | All cmd_* handlers + version-floor check                                |
| `lforge.build_cache`    | compute_key, cache_lookup, cache_populate                               |
| `lforge.version`        | lforge_version()                                                        |

High-level flow of `lforge build`:

```
1. read ./lforge.writ (lforge.manifest)
2. check_requires_logos — error if logosc < manifest's floor
3. build_all_externals (lforge.external_build):
     a. apply replace[] if any
     b. resolve_git_dep — lockfile fast-path or ls-remote + clone
     c. MVS conflict check (winners ledger)
     d. recurse into the dep's own deps[]
     e. for each requested module: cache lookup → hit ? skip : build_lib + populate
4. write_lockfile if deps non-empty
5. topo_order over targets
6. for each target in order: build_lib (lib) or build_bin (bin)
7. announce "build OK"
```

## Limitations

- **No daemon mode / file watcher.** `lforge build` is a one-shot; LSP/MCP integration not yet started.
- **Local-path deps are not cached.** No stable content identity yet (could hash the dir tree later).
- **Build cache is per-user, never shared across users.** A build host wanting cross-user share fills `~/.cache/lforge/build/` from a tarball and trusts the contents.
- **`lforge cache prune`** not implemented yet — entries accumulate.
- **Cross-compilation** is post-v1.
- **Logos source files are ASCII-only.** char literals accept UTF-8 codepoints (since 2026-05-07) but identifiers do not.

## Roadmap

**Done (2026-05-07 toolchain MVP):**
- B0: minimal `build` / `run` / `clean` (single bin target).
- B1: multi-file projects, multi-target manifests, native (C/asm) sources, parallel per-file compile, mtime-based incremental, `lforge test`, `--release` profile, `lforge install`.
- B2: local-path external deps.
- B2.5: git URL deps + `~/.cache/lforge/src/` clones (https / ssh / SCP shorthand).
- B3: lockfile + transitive deps + MVS conflict detection + `lforge update`.
- B4: content-addressed build cache (`~/.cache/lforge/build/<sha256>/`).
- B5: `replace:` directive + `requires_logos:` ABI floor.

**Next:**
- `lforge cache prune` — GC unreferenced cache entries.
- Daemon mode + file watcher.
- LSP server. MCP server.
- More replace forms (replace `foo@v1` with `foo@v2`; replace `foo` with `bar`).
- Hashable local-path deps.
- Memoria-on-Logos consumed via lforge — the use case driving the next phase.

## Source

- Entry point: [tools/lforge/main.logos](../../tools/lforge/main.logos)
- Sub-packages: [tools/lforge/pkg/](../../tools/lforge/pkg/)
- Build target: [tools/lforge/CMakeLists.txt](../../tools/lforge/CMakeLists.txt)
- Tests: [tests/lforge/](../../tests/lforge/) — 17 shell-driven scenarios covering smoke, multitarget, parallel, c+asm, install, test, dogfood, incremental, external paths, git URLs, lockfile, MVS conflict/update, build cache, replace + floor, transitive deps.
- First external package: [github.com/victor-smirnov/lforge-hello-world](https://github.com/victor-smirnov/lforge-hello-world)
