# Hermes2 document model — status (Hermes1→Hermes2 port)

Status: **roadmap core complete (2026-06-07)** — containers + type-tags +
stringify + binary + parser + compaction, all alongside the old Hermes (no cutover
yet). Each landed L4-green + valgrind-clean. Built on the Hermes2 foundation
(never-move arena, HAny, self-describing DSTs, thin self_describing refs).

## The value model

`HAny` (lang.hermes2.anyval) — an 8-byte tagged word, the AnyVal analog:
- **Pod** (low bit 1): inline 56-bit value + 7-bit code. `HA_I56`, `HA_BOOL`.
- **Ref** (low bit 0, ≠0): absolute pointer to a tagged arena object.
- **null**: 0.

`HAnyRel` is the at-rest (self-relative) form in arena slots; `ha_materialize` /
`ha_lower` bridge value↔at-rest.

### Type-tags (Ref dispatch)

Every Ref'd arena object carries its type code IN-BAND immediately before it
(`obj[-1]`, the Hermes1 datatag wire). `container.alloc_tagged(alloc, size, code)`
reserves an 8-byte tag word (keeps the object 8-aligned) and writes the code;
`anyval.h2_type_code(obj)` reads it. `HAny::type_code()` / `is_string()` /
`is_array()` / `is_map()` are the single dispatch point for generic walkers.
Codes: `H2_STRING=130`, `H2_ARRAY=100`, `H2_MAP=101`.

## Containers (lang.hermes2)

| Type | What | Notes |
|---|---|---|
| `HString` | UTF-8 string, self-describing DST `[vlen(len)][utf8]` | vlen-prefixed, safe `&HString` |
| `Array<HAny>` | heterogeneous growing array (JSON array) | self-relative buffer, grow=append+re-anchor |
| `HMap` | string-keyed growing hash map (JSON object) | FNV-1a + open addressing + rehash; was Hermes1 ObjectMap |

All grow by appending a fresh 2× buffer through the stored allocator and
re-anchoring each self-relative slot through its absolute value (no in-place move).
Overloaded `push` / `set` for i64 / bool / str / &HString / &Array / &HMap.

## Operations

| Op | Module | Direction |
|---|---|---|
| stringify | mem.hermes2.stringify | doc → JSON-ish text |
| parse | mem.hermes2.parser | JSON-ish text → doc |
| hbs_write / hbs_read | mem.hermes2.hbs | doc ⇄ bytes (lead-byte + vlen) |
| compactify | lang.hermes2.compactify | doc → fresh-arena copy (copying GC, design §6) |

All four are the same `type_code`-dispatched recursive walk, differing only in the
sink (text / bytes / live arena). The model is round-trippable **text ⇄ doc ⇄
binary** and **self-compacting** (a multi-segment doc copies into one rigid block).

Tests: `hermes2_{hmap,typetags,stringify,hbs,parser,compactify}`.

## Lessons (parallel-Hermes hazards)

- **Global type namespace**: Hermes2 type names that collide with Hermes1's resolve
  ambiguously. Caught `ObjectMap`→`HMap` and `MapEntry`→`HMapEntry` (the latter:
  `sizeof` resolved to the new 16-byte type but `.add` STRIDE to Hermes1's 8-byte
  one → entry overlap → corruption on the first hash collision). Prefix/namespace
  every new type.
- **self_describing gate exemption** needs the struct's def imported (`use
  logos.lang.hermes2.hstring`) for the compiler to see the flag and allow safe
  `&HString` field/method access.

## Remaining for full parity (not started)

- `Map<K,V>` generic (non-string keys).
- Wider scalars: `i64` beyond 56-bit, `f64` — need a Ref-boxed scalar (HAny Pod is
  56-bit only).
- Heavier Hermes1 modules: `clone` (≈ compactify), `equal` (structural), `check`,
  `decimal`.
- **Cutover**: retire `stdlib/lang/hermes/*` + `stdlib/mem/hermes/*` wholesale once
  consumers move to Hermes2 (design §9 part 7).
