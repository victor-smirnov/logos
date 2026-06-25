# Debugging Logos (DWARF + gdb/lldb)

`logosc -g` emits standard DWARF; stock gdb/lldb work. No custom debugger.

## Use

```
logosc prog.logos -g -o prog.o
cc prog.o <stdlib archives> -o prog        # normal link
gdb ./prog
(gdb) source <logos>/tools/gdb/logos_gdb.py   # optional pretty-printers
```

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

## Not yet

- **Enum** variant-name display (`Some(42)`): MLIR 20 lacks `DIEnumeratorAttr` /
  variant-part DI, so native DWARF enums aren't expressible — needs a
  compiler-emitted metadata section consumed by the printer.
- **Hermes containers**: format is decodable (self-relative ptrs, `AnyVal` tags,
  `stringify_value` runtime dump) — printer pending the same metadata channel.
- No **columns** (lexer doesn't track) → line-accurate, col 0.
- Nested compiler-generated functions carry no debug info.
