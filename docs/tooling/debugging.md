# Debugging Logos (DWARF + gdb/lldb)

`logosc -g` emits standard DWARF; stock gdb/lldb work. No custom debugger.

## Use

```
logosc prog.logos -g -o prog.o
cc prog.o <stdlib archives> -o prog        # normal link
logos-gdb ./prog                            # gdb + printers, one command
```

`logos-gdb` is a `rust-gdb`-style wrapper installed beside `logosc` (on `PATH`
as `logos-gdb` / `logos-gdb-<slot>`). It self-locates **this compiler version's**
printers in its own slot tree and sources them before launching gdb. Override the
debugger with `$LOGOS_GDB`.

### Connecting the printers to a plain `gdb` session

The printers ship **per compiler version** under `$(logosc --print-prefix)/share/gdb/`
(the enum-metadata schema + Hermes layout they decode are version-coupled), so
coexisting `logosc-<slot>` installs each carry matching printers. `logos_gdb.py`
auto-loads `logos_hermes_gdb.py` from the same dir.

- **One session** (the shell expands `$(...)`, gdb's `source` does NOT):
  ```
  gdb -ex "source $(logosc --print-prefix)/share/gdb/logos_gdb.py" ./prog
  ```
- **Persistent** — add to `~/.gdbinit` (auto-discovers via `logosc` on PATH):
  ```
  python
  import subprocess, os
  try:
      p = subprocess.check_output(["logosc", "--print-prefix"]).decode().strip()
      s = os.path.join(p, "share", "gdb", "logos_gdb.py")
      if os.path.exists(s): gdb.execute("source " + s)
  except Exception: pass
  end
  ```
- Inside a running gdb, use the **literal** path (no `$(...)`):
  `source /usr/lib/logos/<slot>/share/gdb/logos_gdb.py`.

In a build tree the same `--print-prefix` path resolves to `<build>/share/gdb/`,
and `<build>/bin/logos-gdb` works too.

`-g` (alias `--debug`) is opt-in; off ⇒ zero DWARF, zero codegen change.

## Works

| Capability | Notes |
|---|---|
| `break file.logos:N`, `break <fn>` | line table; readable fn names (`add`, not the mangled symbol) |
| `next` / `step` / `continue` | per-statement lines; steps across call/return |
| `bt` | frames + call-site lines + readable names |
| `print x`, `info locals` | scalar + aggregate locals |
| `print p`, `info args` | params (struct-by-ptr → declare; scalar/ptr → value) |
| `print s.field`, `ptype T` | struct fields + offsets (x86-64 ABI) |
| pretty-printers | `String`→`"…"`, `Vec<T>`→`{…}`, `&[T]`/`str`, `Box<T>` |
| enums | `Some(42)` / `None` / `Circle(5)` / `Rect(3, 4)` (incl. stdlib Option/Result) |
| Hermes | `logos-hermes <expr>` decodes AnyVal/containers; AnyVal auto-printer |

`print *v.ptr@N` works (typed pointers); gdb reads `String.data` as a C string even without the printer.

## Architecture

All DWARF emission is in [src/compiler/mlir_gen_debug.cpp](../../src/compiler/mlir_gen_debug.cpp),
gated on `debug_info_`. Path: per-stmt `FileLineColLoc` **fused** with the fn's
`DISubprogramAttr` → `translateModuleToLLVMIR` lowers DI attrs → LLVM backend
emits DWARF. (`-g` threads `LowerEmitOpts.debug_info` + `source_path` →
`mlir_gen(...)`.)

- **line tables**: `begin_fn_debug`/`end_fn_debug` (per-fn `DISubprogram`),
  `dbg_loc` (fused per-stmt loc set in `gen_stmt`).
- **types**: `di_type` (scalar→`DIBasicType`, ptr/ref→`DIDerivedType`,
  struct/slice→`DICompositeType` w/ members+offsets, agg fallback→opaque sized);
  member base types come from Logos field types (typed pointers); offsets use a
  hand-rolled x86-64 alignment (`abi_align_bytes` — MLIR's default DataLayout
  under-reports, e.g. `align(i64)=4`). `di_struct_inprogress_` breaks recursive
  structs.
- **variables**: `emit_local_dbg_declare` (alloca-size-matched `dbg.declare`;
  rejects `let r=&s` alias + SSA bindings), `emit_param_dbg_declare`.

## Invariants / gotchas

- Bare `FileLineColLoc` (null scope) → no `DILocation`; MUST fuse with the
  subprogram. Nested compiler-gen fns (closures, drop glue) suspend the scope
  (`DebugScopeSuspend`) so one `DISubprogram` never attaches to two LLVM funcs.
- `DW_AT_linkage_name` omitted: Logos `$`-mangled symbols are un-demanglable by
  gdb (C), would hijack `break <fn>`. Mangled symbol stays in ELF symtab;
  subprogram correlated by address.
- Verify any DWARF change: `opt-20 -passes=verify` on `-g --emit-llvm`.

## Enums

MLIR 20 can't express DWARF variant parts, so `logosc -g` emits a
`__logos_debug_meta` global (section `.logos_debug_meta`, JSON keyed by DWARF
type name): per-enum disc offset/size, payload offset, and per-variant
{disc, name, payload types}; niche-packed enums (null-ptr / low-bit) too. The
gdb printer reads it from the objfile (raw ELF parse — works on cores) and
renders the variant + payload. Loaded automatically by logos_gdb.py.

## Hermes containers

[tools/gdb/logos_hermes_gdb.py](../../tools/gdb/logos_hermes_gdb.py) decodes the
Hermes format (self-relative `RelativePtr`, tagged `AnyVal`, `TypeTag`,
ObjectArray/TinyObjectMap/ObjectMap/ArenaString/Decimal/TypedArray, boxed
scalars). Use `logos-hermes <expr>` on an AnyVal value/address; `AnyVal`-typed
values auto-print. Has a no-gdb self-test: `python3 tools/gdb/logos_hermes_gdb.py`.
The compiler's own IR (LProgram/AST) is Hermes, so this also helps debug logosc.
Live alternative: `call (char*)logos::hermes::stringify_value(av)` if the symbol
is linked (`src/hermes/stringify.cpp`).

## Not yet

- No **columns** (lexer doesn't track) → line-accurate, col 0.
- Nested compiler-generated functions carry no debug info.
- Arrays as `DW_TAG_array_type` (currently opaque sized).
- `--gc-sections` may strip `__logos_debug_meta` (link debug builds without it).
