# Memoria (Logos port)

The [Memoria Framework](https://github.com/victor-smirnov/memoria) ported from
C++ to Logos — ADR 0018. A SEPARATE project incubating in `conuco/` while it
tracks the in-tree compiler; builds with **lforge**, not the repo CMake.

Logos *is* Memoria in the form of a programming language — Writ descends from
Hermes, metaprog subsumes the TMP framework and the mbt codegen tool, Deem sits
on top. This port therefore carries over only what the language does not
already embody:

- algorithms, data structures and the patterns around them;
- `PackedAllocator` and the packed in-block layout discipline;
- MemoryStore / SWMRStore — API and implementation;
- the Store / Snapshot / Container API contracts.

C++ Memoria (`~/cxx/memoria`) is the semantics reference and oracle. No byte
compatibility is kept — formats, layouts and hashes diverge freely.

## Modules

| Target | Contents |
|---|---|
| `memoria-pkd` | PackedAllocator, packed columnar buffers (fse/vle) |
| `memoria-bt` | B+tree algorithms over packed nodes; chunk iterators |
| `memoria-ctr` | container declarations: Map, Set, Vector, Collection |
| `memoria-store` | memory CoW store: snapshot DAG, branches, registry, dump/load |

Container configs are `WritStatic` values (profiles included — u64 and UID256
ID widths are profile axes). Threads-only concurrency for now; the SWMR store
arrives later directly on fibers, without mmap.

## Build & test

```
cd conuco/memoria
lforge build
lforge test
```
