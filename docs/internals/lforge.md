# lforge — Logos Build System

`lforge` is the Logos build system — a binary written in Logos that orchestrates `logosc` and `cc` to turn a Logos project into an executable.

This page covers the **MVP state (B0, 2026-05-07)**: a single-file build system that handles a hello-world project end-to-end. Daemon mode, incremental rebuild, MCP/LSP, and dependency resolution are designed but not yet implemented; see [Roadmap](#roadmap) below and [docs/roadmap.md](../roadmap.md) for the longer view.

## Why a Build System

Until B0, building a Logos program meant invoking `logosc` directly with the right flags, then `cc` to link the produced object against the stdlib archives. That is the right primitive for testing the compiler, but not for shipping software:

- A real project has multiple source files, packages, and dependencies — the user shouldn't enumerate them manually.
- AI authors and IDEs need a single point of contact that knows the full project state.
- Incremental rebuild requires content-addressed caching of compile actions; the compiler doesn't have a place for that.
- Self-hosting eventually means lforge builds the compiler itself.

`lforge` is the named orchestrator the [SOA-compiler vision](../adr/0004-definition-centric-tu.md) was always implying. B0 is its first stake in the ground.

## Quick Start

A minimal project:

```
my-project/
  lforge.hermes        # manifest
  src/
    main.logos         # entry
```

Manifest (Hermes-SDN text format):

```
{
    name:    "my-project",
    version: "0.1.0",
    src:     "src",
    entry:   "main"
}
```

Source:

```logos
package my_project;

fn main() -> i32 {
    return 0;
}
```

Build and run:

```bash
lforge build       # produces .lforge/debug/out/my-project
lforge run         # build + execute, exit code propagates
lforge clean       # rm -rf .lforge/
```

The build system finds `logosc` and the Logos lib dir via `$LOGOSC` and `$LOGOS_LIB_DIR`, falling back to in-tree `./build/bin/logosc` and `./build/lib/logos` so a fresh checkout's `lforge` works against the in-tree compiler.

## Manifest Schema

The manifest is a Hermes document in text (SDN) form. Required fields:

| Field     | Type   | Meaning                                                |
|-----------|--------|--------------------------------------------------------|
| `name`    | string | Project name. Used for the output binary file name.   |
| `version` | string | Project version (informational in MVP).                |
| `src`     | string | Source directory relative to project root.            |
| `entry`   | string | Entry module (file `<src>/<entry>.logos`).            |

Unknown fields are silently ignored — forward compatibility for `deps`, `build`, `package` and other planned additions.

The manifest is parsed by `std.hermes.parser` (text → Hermes zone) and traversed via `HermesView` (`map_get` / `get_str_view`). See [tools/lforge/main.logos](../../tools/lforge/main.logos) for the implementation and [tests/logos/pass/lforge_manifest.logos](../../tests/logos/pass/lforge_manifest.logos) for the schema regression suite.

## Build Output Layout

```
my-project/
  .lforge/
    debug/
      out/
        my-project           # linked executable
        my-project.o         # object file from logosc
```

The `debug` directory is the build profile; `release` and others are reserved. The CAS primitives that will eventually live in `.lforge/<profile>/cache/<aa>/<rest>` are implemented in stdlib (B0.4) but not yet wired into the build action — every `lforge build` is currently a full rebuild. Incremental rebuild on the existing CAS is B1.

## Diagnostics

`lforge` invokes `logosc` with default text-format diagnostics in MVP. The flag for structured output is `--diag-format=json` (NDJSON, one diagnostic per line):

```json
{"level":"error","file":"src/main.logos","line":3,"context":"fn main","message":"type mismatch — expected i32, got &[u8]"}
```

`logosc` also defines structured exit codes that `lforge` will use to classify failures programmatically:

| Code | Constant          | Meaning                                                      |
|------|-------------------|--------------------------------------------------------------|
| 0    | `EXIT_OK`         | Success                                                      |
| 1    | `EXIT_USER_ERROR` | sema / mono / borrow-check / lir error in user code         |
| 2    | `EXIT_USAGE`      | Bad CLI args, manifest parse failure                         |
| 3    | `EXIT_CODEGEN`    | mlir-gen / lowering failure (reserved; not yet propagated)   |
| 4    | `EXIT_LINK_IO`    | Module loader / archive read failure                         |
| 5    | `EXIT_ICE`        | Internal consistency check (reserved)                        |

Today most failures map to `EXIT_USER_ERROR`. `EXIT_LINK_IO` fires when a `use` references an unknown package; `EXIT_USAGE` on bad flags or a malformed `lforge.hermes`. The granular split exists so that future lforge code can branch on failure kind without parsing diagnostic text — when daemon mode lands and lforge needs to decide whether to surface a diagnostic to the IDE, retry the action, or invalidate a cache entry, the exit code is the first signal.

## Output Streaming

When `lforge` spawns `logosc` and `cc` it captures their stdout/stderr line-by-line via [`Child.stdout_lines()` / `Child.stderr_lines()`](../../stdlib/std/sys/process/process.logos), iterating `LineReader: Iterator<String>`. Each line is associated with the action that produced it; today they are simply forwarded to the parent's streams, but the same primitive is the foundation for IDE-aware diagnostic routing later (LSP `publishDiagnostics`, MCP tool result, etc.).

The reader is currently blocking-read; when the io_uring reactor gains pipe-poll integration, it becomes fiber-yielding without API changes.

## Architecture (MVP)

```
$ lforge build
    │
    │  1. read ./lforge.hermes (std.hermes.parser, std.hermes.view)
    ▼
Manifest { name, version, src, entry }
    │
    │  2. compose entry path: <src>/<entry>.logos
    │     locate logosc via $LOGOSC / ./build/bin/logosc
    │     locate stdlib via $LOGOS_LIB_DIR / ./build/lib/logos
    ▼
spawn logosc <entry> -o .lforge/debug/out/<name>.o
    │  (std.sys.process — fork+execvp, pipes for stdout/stderr,
    │   LineReader streams the output)
    ▼
.lforge/debug/out/<name>.o
    │
    │  3. spawn cc <obj> <stdlib archives> -o <name>
    ▼
.lforge/debug/out/<name>           # linked executable
```

Single-file lforge (~448 LOC at [tools/lforge/main.logos](../../tools/lforge/main.logos)) inlines the manifest schema/parser, process spawn, and CLI dispatch. The structures meant for incremental rebuild (`FileNode`, `PackageNode`, `ActionKey`, FS-CAS get/has/put) live in [tests/logos/pass/lforge_graph_cas.logos](../../tests/logos/pass/lforge_graph_cas.logos) as a separate self-contained module — they will move into lforge proper when B1 connects them.

## Dependencies

lforge sits on top of stdlib facilities that landed in B0.1:

- [`std.lang.text`](../../stdlib/std/lang/text/) — `String`, `str_starts_with` / `str_index_of` / `str_trim`, `Splitter` iterator, `path::normalize`.
- [`std.io.fs`](../../stdlib/std/io/fs/) — `exists` / `is_dir` / `is_file` / `mkdir_p` / `rm_rf` / `walk_dir` / `canonical`, backed by C wrappers in [`stdlib/rt/fs_meta.c`](../../stdlib/rt/fs_meta.c).
- [`std.sys.process`](../../stdlib/std/sys/process/process.logos) — `spawn` with optional pipe stdin/stdout/stderr, `Child.stdout_lines() : Iterator<String>` and `stderr_lines()`.
- [`std.hermes.parser`](../../stdlib/std/hermes/parser/parser.logos) — Hermes text-format (SDN) parser.
- [`std.encoding.json`](../../stdlib/std/encoding/json/) — JSON `parse` returning a `Json` AST. Earmarked for consuming `logosc --diag-format=json` output once lforge needs to introspect diagnostics.
- [`std.crypto`](../../stdlib/std/crypto/) — sha256 hex for content addressing.

## Limitations of B0 MVP

- **Single source file via `entry`.** Multi-file projects don't work yet; the entry file's `use` graph drives logosc, but lforge doesn't iterate multiple top-level files.
- **No deps.** `lforge.hermes` has no `deps` field; the only library lforge links against is the system stdlib.
- **Full rebuild every time.** CAS primitives exist but aren't wired to the build action.
- **Single profile (`debug`).** Output dir hardcoded to `.lforge/debug/`.
- **Batch mode only.** No daemon, no file watcher, no LSP/MCP.

These are deliberate scope cuts to ship B0 and unblock further work — each is addressed in B1 below.

## Roadmap

**B1 — incremental + multi-file:**
- File walker over `<src>/` discovers all `.logos` files; per-file action keyed by content hash + flags.
- CAS hookup: skip compile when `(input hashes, command line)` hash hits the cache.
- Multi-package projects: `packages: [...]` in manifest, each with its own entry.
- `lforge test` action: discovers and runs `tests/`-style fixtures.
- `lforge build --release` profile.

**B2 — package manager + daemon:**
- Local path deps (`deps: { foo: { path: "../foo" } }`).
- Lockfile (`lforge.lock`) — pinned versions, content hashes.
- HTTP registry client (over `std.io.http`).
- Daemon mode (`lforge serve`) — long-running, file watcher, streaming diagnostics.
- LSP server. MCP server.

**B3 — Memoria adoption:**
- Memoria ports to Logos and is built by lforge.
- The compiler's internal scaffold (cmake) becomes a Stage-1 bootstrap; everything else moves to `lforge build`.

## Source

- Entry point: [tools/lforge/main.logos](../../tools/lforge/main.logos)
- Build target: [tools/lforge/CMakeLists.txt](../../tools/lforge/CMakeLists.txt)
- Smoke test: [tests/lforge/smoke.sh](../../tests/lforge/smoke.sh)
- Manifest schema test: [tests/logos/pass/lforge_manifest.logos](../../tests/logos/pass/lforge_manifest.logos)
- Graph + CAS primitives: [tests/logos/pass/lforge_graph_cas.logos](../../tests/logos/pass/lforge_graph_cas.logos)
- JSON diagnostics test: [tests/diag/json_format.sh](../../tests/diag/json_format.sh)
