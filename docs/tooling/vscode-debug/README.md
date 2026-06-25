# Debugging Logos in VSCode

Interactive source-level debugging of Logos programs — breakpoints, stepping,
and a Variables pane that pretty-prints `Vec`/`String`/enums/`Option` — via the
**C/C++ extension** (gdb backend). Logos binaries built with `logosc -g` carry
standard DWARF, so the stock cppdbg adapter just works.

## One-time setup

1. Install the **C/C++** extension (`ms-vscode.cpptools`).
2. Open *this folder* in VSCode (`File → Open Folder…` → `vscode-debug/`).

## Use

1. Open `show.logos`, click the gutter to set a breakpoint (e.g. line 27).
2. Press **F5** (or Run → Start Debugging).

`preLaunchTask` runs `build.sh` (`logosc -g` + link), then gdb launches and the
pretty-printers load. You get:

- breakpoints by line, **F10** step-over / **F11** step-into / **F5** continue;
- the **Variables** pane showing `p = {x = 3, y = 4}`, `xs = Vec(len=3)` (expand →
  `7, 8, 9`), `label = "logos"`, `sh = Rect(5, 6)`, `some = Some(42)`, `nope = None`;
- the **Call Stack** pane with frames + lines;
- **Watch** expressions and hover-to-evaluate (`p.x`, `xs.ptr[2]`, …);
- the **Debug Console** for any gdb command (`-exec print sh`, `-exec logos-hermes …`).

## How it's wired

- [.vscode/tasks.json](.vscode/tasks.json) — `build-logos`: `build.sh show.logos`.
- [.vscode/launch.json](.vscode/launch.json) — `cppdbg`/`gdb`; `setupCommands`
  enable pretty-printing and `source` the Logos printers.
- [build.sh](build.sh) — `logosc <abs path> -g` (abs path so DWARF source-maps
  cleanly) + `cc` link against `logosc --print-lib-dir` archives.

To debug a different file, change the `args` in `tasks.json` and `program` in
`launch.json`.

## Installed logosc (not a dev build)

Replace the printer `source` line in `launch.json` with the per-slot path:

```
source $(logosc --print-prefix)/share/gdb/logos_gdb.py     # run in a shell to get the path
```

i.e. put that resolved absolute path into the `setupCommands` `source` entry. The
printers ship per compiler version, so this keeps them matched to `logosc`.

## Notes

- For the **full pretty-printed** experience use the gdb backend (cppdbg). The
  CodeLLDB extension also debugs the DWARF (breakpoints/stepping/raw values) but
  our printers are gdb-Python, so enums/`Vec` show as raw structs there.
- Line accuracy: see [../debugging.md](../debugging.md).
