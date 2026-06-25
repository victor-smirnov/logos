# Debugging Logos in VSCode

Interactive source-level debugging of Logos programs — breakpoints, stepping,
and a Variables pane that pretty-prints `Vec`/`String`/enums/`Option` — via the
**C/C++ extension** (gdb backend). Logos binaries built with `logosc -g` carry
standard DWARF, so the stock cppdbg adapter just works.

This is a reusable config; it carries no sample program — it builds and debugs
**whatever `.logos` file is currently open**.

## One-time setup

1. Install the **C/C++** extension (`ms-vscode.cpptools`).
2. Copy this `.vscode/` and `build.sh` into your project (or open this folder).

## Use

1. Open a `.logos` file, click the gutter to set a breakpoint.
2. Press **F5** (Run → Start Debugging).

`preLaunchTask` runs `build.sh ${file}` (`logosc -g` + link), then gdb launches
the sibling executable and the pretty-printers load. You get:

- breakpoints by line, **F10** step-over / **F11** step-into / **F5** continue;
- the **Variables** pane: `p = {x = 3, y = 4}`, `xs = Vec(len=3)` (expand →
  elements), `label = "logos"`, `sh = Rect(5, 6)`, `some = Some(42)`, `None`;
- the **Call Stack** pane with frames + lines;
- **Watch** expressions and hover-to-evaluate (`p.x`, `xs.ptr[2]`, …);
- the **Debug Console** for gdb commands (`-exec print sh`, `-exec logos-hermes …`).

## How it's wired

- [.vscode/tasks.json](.vscode/tasks.json) — `build-logos`: `build.sh ${file}`.
- [.vscode/launch.json](.vscode/launch.json) — `cppdbg`/`gdb` on
  `${fileDirname}/${fileBasenameNoExtension}`; `setupCommands` enable
  pretty-printing and `source` the Logos printers.
- [build.sh](build.sh) — `logosc <abs path> -g` (abs path so DWARF source-maps
  cleanly) + `cc` link against the `logosc --print-lib-dir` archives.

## Installed logosc (not a dev build)

Replace the printer `source` line in `launch.json` with the per-slot path
(run in a shell to resolve it):

```
logosc --print-prefix      # → /usr/lib/logos/<slot>;  use <that>/share/gdb/logos_gdb.py
```

The printers ship per compiler version, so this keeps them matched to `logosc`.

## Notes

- Use the gdb backend (cppdbg) for the full pretty-printed experience. CodeLLDB
  also debugs the DWARF (breakpoints/stepping/raw values) but our printers are
  gdb-Python, so enums/`Vec` show as raw structs there.
- Line accuracy and printer details: [../debugging.md](../debugging.md).
