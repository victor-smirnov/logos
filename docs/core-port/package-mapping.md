# Core Port — Package Mapping

Maps current Logos stdlib package declarations to their target Rust-aligned paths.

Decisions encoded:
- Mirror `rust/library/core` + `rust/library/alloc` + `rust/library/std` layout for ported modules.
- Logos-only modules (Hermes, persistent, metaprog, datatypes, reactor/fiber, lforge-specific I/O) stay at current paths.
- Logos system has *package declarations*, not file paths — migration changes the `package` line, not file location.
- Rust's `core` vs `alloc` vs `std` split is preserved: `std.alloc.*` for heap-using code, `std.*` for heap-free and re-exports.

Legend:
- **MOVE** — package decl changes during migration.
- **STAY** — Logos-specific, no Rust analog, keep as-is.
- **MERGE** — current package's contents split into multiple Rust paths.

---

## Layer: `std.lang.*` → top-level `std.*`

The `std.lang` namespace has no Rust analog. Everything here moves up.

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.lang.option` | `std.option` | MOVE | direct |
| `std.lang.result` | `std.result` | MOVE | direct |
| `std.lang.clone` | `std.clone` | MOVE | direct (Clone trait) |
| `std.lang.cmp` | `std.cmp` | MOVE | direct (PartialEq, Eq) |
| `std.lang.ord` | `std.cmp` | MERGE | Ord/PartialOrd live in `std::cmp` |
| `std.lang.convert` | `std.convert` | MOVE | From/Into/TryFrom |
| `std.lang.default` | `std.default` | MOVE | Default trait |
| `std.lang.hash` | `std.hash` | MOVE | Hash/Hasher |
| `std.lang.iter` | `std.iter` | MOVE | Iterator + adapters |
| `std.lang.marker` | `std.marker` | MOVE | Send/Sync/Copy/Sized/Unpin |
| `std.lang.panic` | `std.panic` | MOVE | direct |
| `std.lang.range` | `std.ops` | MERGE | Range/RangeFrom/RangeTo in `std::ops` |
| `std.lang.arith` | `std.ops` | MERGE | Add/Sub/Mul/Div/Rem traits in `std::ops` |
| `std.lang.drop` | `std.ops` | MERGE | `Drop` trait is in `std::ops::Drop`, re-exported as `std::mem::drop` (fn) — the trait stays in ops |
| `std.lang.text` | `std.str` + `std.alloc.string` | MERGE | `str` primitive in `std.str`, owned `String` in `std.alloc.string` |
| `std.lang.text.regex` | TBD | unclear | Rust core has no regex; `regex` is a third-party crate. Recommend STAY as `std.text.regex` or move to `std.regex`. Decide before port. |
| `std.lang.reflect` | `std.any` | MOVE | TypeInfo → TypeId in `std::any`; will need shim/divergence note |
| `std.lang.thread` | `std.thread` | MOVE | direct |
| `std.lang.datatypes` | STAY | — | Hermes-fabric traits (Storage, Datatype, Container, Buffer, PrimVec, UnsizedPayload). Logos-specific. Keep at `std.lang.datatypes` or rename to `std.hermes.fabric` for clarity. **DECIDE.** |

---

## Layer: `std.mem.*`

Rust splits owned smart pointers into `alloc::boxed`, `alloc::rc`, `alloc::sync`, and re-exports through `std::boxed`, `std::rc`, `std::sync`.

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.mem` | `std.mem` | MOVE | matches — mem::swap, replace, take, drop, transmute |
| `std.mem.box` | `std.alloc.boxed` | MOVE | `Box<T>` in `alloc::boxed::Box` |
| `std.mem.rc` | `std.alloc.rc` | MOVE | `Rc<T>` in `alloc::rc::Rc` |
| `std.mem.arc` | `std.alloc.sync` | MOVE | `Arc<T>` in `alloc::sync::Arc`; re-exported as `std.sync.Arc` |

---

## Layer: `std.collections.*`

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.collections.vec` | `std.alloc.vec` | MOVE | `Vec<T>` in `alloc::vec::Vec`, re-exported as `std::vec::Vec` |
| `std.collections.deque` | `std.alloc.collections.vec_deque` | MOVE | `VecDeque` in `alloc::collections::vec_deque` |
| `std.collections.hashmap` | `std.collections.hash_map` | MOVE | `HashMap` is in `std::collections::hash_map` (NOT in core/alloc — uses RandomState) |
| `std.collections.set` | `std.collections.hash_set` | MOVE | `HashSet` in `std::collections::hash_set` |
| `std.collections.btree` | `std.alloc.collections.btree_map` | MOVE | `BTreeMap` in `alloc::collections::btree_map`. If we also have BTreeSet, that's `btree_set` |

---

## Layer: `std.io.*`

Rust's `std::io` has just the traits + a few types. File system is separate `std::fs`. Network is separate `std::net`. Paths are `std::path`.

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.io` | `std.io` | MOVE | match |
| `std.io.read` | `std.io` | MERGE | Read trait — lives directly in `std::io` |
| `std.io.write` | `std.io` | MERGE | Write trait — lives directly in `std::io` |
| `std.io.buffered` | `std.io` | MERGE | BufReader/BufWriter live in `std::io` |
| `std.io.bytes` | STAY | — | Bytes/BytesMut analog (cf. `bytes` crate). No Rust core analog. Decide: `std.bytes` or keep nested. **DECIDE.** |
| `std.io.fs` | `std.fs` | MOVE | File, OpenOptions, etc. |
| `std.io.fs.path` | `std.path` | MOVE | Path, PathBuf |
| `std.io.net` | `std.net` | MOVE | TcpStream, UdpSocket, IpAddr |
| `std.io.net.tls` | STAY | — | No Rust core analog (rustls/native-tls are third-party). Keep at `std.net.tls` after net move. |
| `std.io.net.url` | STAY | — | No Rust core analog (`url` crate). Keep at `std.net.url` or `std.url`. **DECIDE.** |
| `std.io.http` | STAY | — | No Rust core analog. Keep as `std.http` or `std.net.http`. **DECIDE.** |
| `std.io.pipe` | STAY | — | Logos-specific abstraction. Keep. |
| `std.io.linux.uring` | STAY | — | Logos-specific runtime. Keep at `std.os.linux.uring` post-rename (matches Rust's `std::os::linux::*` convention). |

---

## Layer: `std.sys.*`

Rust's `std::sys` is an internal namespace (not user-facing). The relevant user-facing modules are `std::env`, `std::process`, `std::os::*`.

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.sys.args` | `std.env` | MERGE | `env::args()` |
| `std.sys.env` | `std.env` | MOVE | env vars |
| `std.sys.process` | `std.process` | MOVE | Command, Child |
| `std.sys.os` | `std.os` | MOVE | currently has pid/uid/hostname — those belong to `std::os::unix::*` or `std::process` depending on call |
| `std.sys.signal` | `std.os.unix.signal` | MOVE | Unix signals — not in Rust core, third-party (`nix`); position under `std.os.unix` consistent with Rust layout |
| `std.sys.fiber` | STAY | — | Logos green-thread runtime. Keep at `std.sys.fiber` or move to `std.rt.fiber`. **DECIDE.** |

---

## Layer: `std.fmt`, `std.time`, `std.sync`, `std.math`, `std.crypto`, `std.encoding`, `std.testing`, `std.log`

| Current | Target | Action | Notes |
|---|---|---|---|
| `std.fmt` | `std.fmt` | KEEP | matches |
| `std.time` | `std.time` | KEEP | matches |
| `std.time.datetime` | STAY | — | Rust's `std::time` has only Duration/Instant/SystemTime — no calendar dates. `chrono` is third-party. Keep `std.time.datetime` as Logos extension. |
| `std.sync` | `std.sync` | KEEP | matches |
| `std.sync.atomic` | `std.sync.atomic` | KEEP | matches |
| `std.math` | TBD | DECIDE | Rust has no `std::math`. Math operations are methods on primitive types (`f64::sin()`, `i32::abs()`). Keep `std.math` as Logos namespace, or move into primitive impls. **Recommend keep namespace for free functions, also expose as methods.** |
| `std.math.random` | TBD | DECIDE | Rust has no `std::random` (third-party `rand`). Keep at `std.random` or `std.math.random`. **Recommend `std.random`** (cf. forthcoming Rust `core::random` accepted RFC). |
| `std.crypto` | STAY | — | No Rust core analog. Keep. |
| `std.encoding.*` (csv/json/base64/hex) | STAY | — | No Rust core analog (serde + dedicated crates). Keep tree. |
| `std.testing` | STAY | — | Rust's test infra is built into rustc + `#[test]` + `libtest`. Logos has its own. Keep. |
| `std.log` | STAY | — | No Rust core analog (`log` + `env_logger` third-party). Keep. |

---

## Layer: Logos-only — `std.hermes.*`, `std.data.persistent.*`, `std.compiler.*`, `rt/`

All **STAY** unchanged. No Rust analog by design.

- `std.hermes.*` — datatype/storage fabric (24 sub-packages)
- `std.data.persistent.*` — mini-Memoria CoW B+tree stack
- `std.compiler.metaprog`, `std.compiler.tokens` — language tooling
- `rt/` (runtime) — Logos runtime (reactor/fiber/sync), already on Logos

---

## Resolved decisions (2026-05-13)

1. `std.lang.text.regex` → **`std.regex`** (top-level Logos extension, consistent with ecosystem `regex` crate position).
2. `std.lang.datatypes` → **`std.hermes.fabric`** — Hermes-fabric traits live under the Hermes namespace for clarity (Storage, Datatype, Container, Buffer, PrimVec, UnsizedPayload).
3. `std.io.bytes` → **`std.bytes`** (top-level, consistent with ecosystem `bytes` crate).
4. `std.io.net.url` → **`std.url`** (top-level, consistent with ecosystem `url` crate).
5. `std.io.http` → **`std.http`** (top-level Logos-only extension, no Rust core analog).
6. `std.sys.fiber` → **`std.rt.fiber`** (separate runtime namespace, distinct from `std.thread`).
7. `std.math.random` → **`std.random`** (top-level, matching Rust's accepted `core::random` RFC direction).

---

## Migration mechanics

Per file:
1. Change `package std.X.Y` to `package std.X'.Y'` (per table above).
2. Find all consumers (`grep -r 'import std.X.Y\|use std::X::Y' ...`) and update.
3. For widely-used types (`Vec`, `Option`, `Result`, `Box`, `Rc`, `Arc`, `String`, `HashMap`) — leave a `pub use` re-export shim at old path during transition, remove by end of Phase 4.

Atomic per-module — never half-migrated. One commit per package or per tightly-coupled cluster.

## Infrastructure (already in place)

The repo-level infrastructure for Rust ports is already established:

| Path | Purpose |
|---|---|
| `stdlib/imported/README.md` | Port policy: when to import, per-file headers, partial imports |
| `stdlib/imported/RUSTC-PROVENANCE.md` | Authoritative manifest of imported stdlib files |
| `tests/imported/RUSTC-PROVENANCE.md` | Authoritative manifest of imported test files |
| `tests/imported/RUST-COPYRIGHT.md` | Attribution pointers |
| `COPYRIGHT`, `NOTICE` | Repo-level dual licensing with explicit rust-lang/rust attribution |
| `LICENSE-APACHE`, `LICENSE-MIT` | License texts |
| `tests/imported/core/` | New: coretests crate imports (parallel to existing `tests/imported/{pass,fail}/` for rustc tests/ui) |

**Convention from existing README**: imported stdlib files live at `stdlib/imported/<package-path>/...`, mirroring Logos package paths (not Rust source-tree split). After the rename in this doc lands, those paths automatically align with Rust paths (e.g. `package std.option` → `stdlib/imported/std/option.logos`).

**Per-file header (required by existing policy)**:

```logos
// Imported from rust-lang/rust@<COMMIT-SHA>
// Original path: library/<crate>/src/<original-path>.rs
// Original copyright: The Rust Project Developers (Apache 2.0 / MIT).
// Modifications: <one-line summary of porting changes>
```

The pinned commit will be set in the first port batch row in `stdlib/imported/RUSTC-PROVENANCE.md` (current candidate: `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`, matching the existing tests/imported batches).
