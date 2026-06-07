# Thin `&self_describing` DST refs + vlen HString — design map

Status: **DONE (2026-06-07, commits 65e960bc thin-DstRef + 3a89ea18 vlen).** A
`&Foo`/`&mut Foo` to a `#[self_describing]` DST is now a THIN 8-byte reference,
so a safe, RETURNABLE `&HString` works; HString is a vlen-prefixed proper DST.
Full suite 5557/5557, valgrind-clean.

KEY CORRECTION to the step-0 analysis below: the fat path was NOT "contradictory/
broken" — it WORKS for LOCAL borrows (GBlock in-suite proves it). The real reason
thin is REQUIRED is the RETURN case: a fat ref's {data,len} pair lives in the
callee's stack alloca, so a returned `&HString` dangles (reads garbage after any
intervening call). Thin (the ref IS the heap header pointer) is the only way to
return one. The two surgical bugs found: (1) `&*p` construction always
materialized a fat pair → made thin; (2) the self_describing predicate looked up
the BARE template name (`GBlock`) but all_struct_defs_ keys the CONCRETE mono'd
name (`GBlock$G1$i64`) → generics silently fell back to fat repr while
construction went thin → mismatch segfault → fixed by concrete-name lookup. The
slice-len projection needed `emit_dst_len` (in-band) instead of repr_meta's
fat-pair-field-1 load. Implemented via the single `dstref_pointee_self_describing`
predicate across repr/construction/cast/store/slice-len + the sema gate exemption.

(Original step-0 map below, kept for the historical reasoning.)

## Why a fresh session (the key finding)

The `#[self_describing]` / `DstRef` / thinness model is **inconsistent across codegen
sites** (mid-evolution) — the same `&HString` is treated 8B-thin at some sites and
16B-fat at others. Closing safe-`&` means UNIFYING that model across ~8-10 sites in a
contradictory area; the failure mode is **silently-wrong codegen** (not a build
error), catchable only by per-step behavior tests. This is exactly the class of
deep, woven change that must be done with fresh focus + per-site behavior-gating, not
rushed — see the borrow-checker lesson in
[borrow-escape-analysis-design.md](borrow-escape-analysis-design.md).

## Representation facts (step-0)

- A `FatCustomDst` `DstRef` is an **8-byte VALUE = pointer to a 16-byte `{data, len}`
  pair** in memory. Extraction: `data = load(gep(val,0))`, `len = load(gep(val,1))`.
- A thin `&self_describing` DST should be: **8-byte VALUE = pointer DIRECTLY to the
  header** (identical to `*const HString`). Then `data = val` (no load),
  `len = dst_len(val)` (in-band). Field/tail access then = the working `*const`
  self-describing path, but SAFE.
- `dst_len` = TAIL ELEMENT count (Segment: `cap` = `data.len()`), NOT whole-object.
  For a `[u8]`-tail with no sized prefix, that equals the whole object's bytes.

## The 4 inconsistent sites (the tangle)

| Site | File:line | Treats `&self_describing` as |
|---|---|---|
| element-storage / stride (`ref_repr_of`) | mlir_gen_types.cpp ~1062, 536 | **thin Ptr** (8B) — comment says so |
| construction `&*p` | mlir_gen_expr.cpp ~1335-1341 | **FAT** — `materialize_self_describing_ref` → `{data, len=dst_len}` (16B) |
| cast / data-ptr extract | mlir_gen_expr.cpp ~3236-3261 | **FAT** — `load(gep(val,0))` (val = ptr to pair) |
| store/copy (`dstref_has_slice_tail`) | mlir_gen_stmt.cpp ~1160 | **FAT** for `[T]`-tail (16B memcpy) |

The committed-HString bug (when `hstring` returned `&HString`): construction made a
fat ref with `len=0` (the `dst_len` resolve in `materialize_self_describing_ref`
fell back to 0), so `self.len` (header via `data`) read 14 but `self.bytes` (slice
`{data+8, 0}`) was empty and the fat→raw cast took the 0 half → segfault.

## Plan (refactor → flip → stdlib), per-step L4-gated

1. **One predicate** (single source): `dstref_is_thin(t) = DstRef && (dyn/TypeVar-tail
   OR pointee.self_describing)` + `dstref_pointee_self_describing(t)` (looks up
   `all_struct_defs_[name]->self_describing`). Plus two extraction helpers
   `dstref_data(val,t)` / `dstref_tail_len(val,t)` that branch on it (thin → `val` /
   `dst_len(val)`; fat → `load(gep,0)` / `load(gep,1)`).
2. **Refactor the 4 sites onto the helpers — NO behavior change** (L4 green). This
   isolates every fat-extraction in one place under a green suite.
3. **Flip**: `ref_repr_of(DstRef self_describing) → ThinPtr` (8B); the helpers now
   return thin for self_describing. Behavior changes → L4 + valgrind + the hstring
   behavior test.
4. **Construction**: `&*p` of a thin self_describing → return the thin ptr (skip
   `materialize_self_describing_ref` entirely — no fat pair, no `len=0` bug).
5. **Unsafe gate**: exempt `self_describing` from the `&DstStruct` method-call (sema
   ~5969) + field-read (sema ~8388) unsafe gates — now SOUND (thin ptr + in-band
   length make the access well-defined). (An earlier exemption was reverted because
   the fat path was still broken; with steps 3-4 it is correct.)

## vlen HString (fold in — orthogonal but part of "HString done right")

- Relocate the vlen codec (`vlen_read/write/prefix_size/encode_size`) from
  `logos.lang.hermes.string` to a **neutral module** (`logos.lang.vlen`) — it's a
  general variable-length-integer codec, not string-specific; and Hermes2 shouldn't
  depend on Hermes1. Update HermesString's imports.
- `#[self_describing] struct HString { bytes: [u8] }` — NO explicit `len` field;
  layout `[vlen(payload_len)][utf8 payload]` (HermesString's layout, as a DST).
  `dst_len = vlen_prefix_size(p) + vlen_read(p)` (whole tail = whole object, no
  sized prefix). `len()` = `vlen_read(p)` (logical string length, payload only).
  `as_str` skips the vlen prefix.
- With safe-`&` (steps 1-5): `hstring(&self,s) -> &HString`, safe `as_str(&self)`/
  `len(&self)`, `HAny::from(&HString)`, `hstring_of -> &HString`. Showcase §7 + test
  `hermes2_hstring` back to the safe `&` form (no `unsafe` at call sites).
