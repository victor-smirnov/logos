# Writ document model — status (legacy→Writ port)

Status: **roadmap core complete (2026-06-07)** — containers + type-tags +
stringify + binary + parser + compaction, all alongside the old Writ (no cutover
yet). Each landed L4-green + valgrind-clean. Built on the Writ foundation
(never-move arena, HAny, self-describing DSTs, thin self_describing refs).

## The value model

`HAny` (lang.writ.anyval) — an 8-byte tagged word, the AnyVal analog:
- **Pod** (low bit 1): inline 56-bit value + 7-bit code. `HA_I56`, `HA_BOOL`.
- **Ref** (low bit 0, ≠0): absolute pointer to a tagged arena object.
- **null**: 0.

**Wide scalars**: values that don't fit the 56-bit inline Pod are BOXED in the
segment by Ref — an 8-byte tagged object `H2_I64` (full 64-bit int) / `H2_F64`
(IEEE double). `Writ::int(v)` / `float(v)` build them (Pod when it fits, else
boxed); `HAny::is_int` (Pod i56 OR boxed i64) / `is_float` / `as_i64` / `as_f64`
read either form transparently.

`HAnyRel` is the at-rest (self-relative) form in arena slots; `ha_materialize` /
`ha_lower` bridge value↔at-rest.

### Type-tags (Ref dispatch)

Every Ref'd arena object carries its type code IN-BAND immediately before it
(`obj[-1]`, the legacy datatag wire). `container.alloc_tagged(alloc, size, code)`
reserves an 8-byte tag word (keeps the object 8-aligned) and writes the code;
`anyval.h2_type_code(obj)` reads it. `HAny::type_code()` / `is_string()` /
`is_array()` / `is_map()` are the single dispatch point for generic walkers.
Codes: `H2_STRING=130`, `H2_ARRAY=100`, `H2_MAP=101`.

## Containers (lang.writ)

| Type | What | Notes |
|---|---|---|
| `HString` | UTF-8 string, self-describing DST `[vlen(len)][utf8]` | vlen-prefixed, safe `&HString` |
| `Array<HAny>` | heterogeneous growing array (JSON array) | self-relative buffer, grow=append+re-anchor |
| `HMap` | string-keyed growing hash map (JSON object) | FNV-1a + open addressing + rehash; was legacy ObjectMap |

All grow by appending a fresh 2× buffer through the stored allocator and
re-anchoring each self-relative slot through its absolute value (no in-place move).
Overloaded `push` / `set` for i64 / bool / str / &HString / &Array / &HMap.

## Operations

| Op | Module | Direction |
|---|---|---|
| stringify | mem.writ.stringify | doc → JSON-ish text |
| parse | mem.writ.parser | JSON-ish text → doc |
| hbs_write / hbs_read | mem.writ.hbs | doc ⇄ bytes (lead-byte + vlen) |
| compactify / clone | lang.writ.compactify | doc → fresh-arena copy (copying GC, design §6) |
| equal | lang.writ.equal | structural (deep) equality by value, cross-arena |

All are the same `type_code`-dispatched recursive walk, differing only in the sink
(text / bytes / live arena / bool). The model is round-trippable **text ⇄ doc ⇄
binary**, **self-compacting** (a multi-segment doc copies into one rigid block),
and **comparable** (an original equals its round-tripped/compacted copy).
`examples/writ2_showcase.logos` §8 demonstrates the whole pipeline.

Tests: `writ2_{wmap,typetags,stringify,hbs,parser,compactify,wide_scalars,equal}`.

## Lessons (parallel-Writ hazards)

- **Global type namespace**: Writ type names that collide with the legacy resolve
  ambiguously. Caught `ObjectMap`→`HMap` and `MapEntry`→`HMapEntry` (the latter:
  `sizeof` resolved to the new 16-byte type but `.add` STRIDE to the legacy 8-byte
  one → entry overlap → corruption on the first hash collision). Prefix/namespace
  every new type.
- **self_describing gate exemption** needs the struct's def imported (`use
  logos.lang.writ.wstring`) for the compiler to see the flag and allow safe
  `&HString` field/method access.

## Remaining for full parity (not started)

- `Map<K,V>` generic (non-string keys; HMap covers the string-keyed JSON object).
- `check` (validate an HBS byte buffer / doc before trusting it — hbs_read
  currently assumes well-formed input).
- `decimal` (arbitrary-precision decimal — a substantial separate piece).
- **Cutover**: retire `stdlib/lang/writ/*` + `stdlib/mem/writ/*` wholesale —
  explicitly a SEPARATE session (and gated on a new C++ Writ implementation
  first). Writ lives alongside the old Writ until then (design §9 part 7).

Done since the roadmap core: **wide scalars** (Ref-boxed i64/f64), **equal**,
**clone**, showcase §8.
