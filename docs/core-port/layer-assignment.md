# Per-Package Layer Assignment

Authoritative table assigning each of the 106 current `std.*`
packages to one of the three target layers: **lang** (no-alloc,
no-OS), **mem** (heap, no-OS), **std** (full).

Companion to [three-layer-split.md](three-layer-split.md). This
table is the source of truth for Phase 4 migration commits.

Status: **draft for review.** Verified per-package during Phase 4
when actual code moves and layer enforcement (Phase 6.A) catches
violations. The most likely point of revision is the Hermes split
(`std.hermes.*` 32 packages → ~15 lang + ~17 mem) — some packages
may turn out to drag heap dependencies that demote them to mem.

---

## Decision criteria

- **lang** (logos.lang.*): no heap allocation in any code path; no
  OS dependency. Pure compile-time types, read-only views,
  arithmetic, trait declarations. Metacall JIT runs in compiler
  arena, not user runtime — counts as lang.
- **mem** (logos.mem.*): owns heap; requires an allocator at
  runtime; does not call into the OS for IO/threads/clocks.
  Includes anything backed by `MemHolder`/zone allocation, all
  growing containers, format!-runtime path.
- **std** (logos.std.*): everything touching the operating system
  — IO syscalls, threads, fibers, time, network, filesystem,
  randomness, panicked exits, logging.

When in doubt, the cheaper move is to demote (mem → std, lang →
mem); promoting later requires more careful audit.

---

## Top-level package table

Sorted by current package name within each layer. Sub-table for
`std.hermes.*` and `std.io.*` follows.

### → logos.lang (no-alloc, no-OS)

| Current | Target | Notes |
|---|---|---|
| `std.lang.arith` | `logos.lang.arith` | Add/Sub/Mul/Div/Rem traits — likely folded into `logos.lang.ops` per Rust during Phase 4. |
| `std.lang.bool` | `logos.lang.bool` | bool method impls. |
| `std.lang.char` | `logos.lang.char` | char primitive ops. |
| `std.lang.clone` | `logos.lang.clone` | Clone trait + blanket impls for Copy types. |
| `std.lang.cmp` | `logos.lang.cmp` | PartialEq/Eq + Ordering enum. |
| `std.lang.convert` | `logos.lang.convert` | From/Into/TryFrom/TryInto. |
| `std.lang.default` | `logos.lang.default` | Default trait. |
| `std.lang.drop` | `logos.lang.drop` | Drop trait. May fold into `logos.lang.ops` per Rust. |
| `std.lang.hash` | `logos.lang.hash` | Hash/Hasher traits. |
| `std.lang.iter` | `logos.lang.iter` | Iterator trait + adapter trait surface. Concrete heap-using adapters (Collect→Vec, etc.) factor to mem in Phase 4 if needed. |
| `std.lang.marker` | `logos.lang.marker` | Send/Sync/Sized/Copy/Unpin. |
| `std.lang.ops` | `logos.lang.ops` | Index/Deref operator traits. Will receive merged arith + drop + range items per Rust. |
| `std.lang.option` | `logos.lang.option` | Option<T>. |
| `std.lang.ord` | `logos.lang.cmp` | **MERGED into `logos.lang.cmp`** per Rust convention. |
| `std.lang.panic` | `logos.lang.panic` | Panic-handler trait + builtin hook. Runtime panic impl lives in `logos.std.panic`. |
| `std.lang.range` | `logos.lang.ops` | **MERGED into `logos.lang.ops`** per Rust (Range/RangeFrom/etc.). |
| `std.lang.reflect` | `logos.lang.any` | TypeInfo → Rust's TypeId. Read-only type identity. |
| `std.lang.result` | `logos.lang.result` | Result<T, E>. |
| `std.lang.text` | `logos.lang.str` + `logos.mem.string` | **SPLIT.** Read-side (`&str` ops, str_eq, str_starts_with…) → lang. Owned (`String`) → mem. |
| `std.lang.text.regex` | `logos.lang.regex` (read-only API) + `logos.mem.regex` if needed | Regex matching itself doesn't allocate; compilation may. Audit at Phase 4. |
| `std.lang.datatypes` | `logos.lang.hermes.fabric` | Datatype/Storage/Container traits. Hermes-fabric, no alloc. |
| `std.compiler.metaprog` | `logos.lang.metaprog` | Compile-time only — never runs in user runtime. |
| `std.compiler.tokens` | `logos.lang.tokens` | Token-list types for metaprog input. |
| `std.math` | `logos.lang.math` | Free functions over primitives. No alloc. |

**Count: 24 packages → lang** (after merges: ~20 distinct target packages).

### → logos.mem (heap, no-OS)

| Current | Target | Notes |
|---|---|---|
| `std.mem` | `logos.mem.mem` | swap/replace/take/transmute. Lives at mem root. |
| `std.mem.box` | `logos.mem.boxed` | Box<T>. |
| `std.mem.rc` | `logos.mem.rc` | Rc<T>. |
| `std.mem.arc` | `logos.mem.sync` | Arc<T>. Rust's path is `alloc::sync::Arc`. |
| `std.collections.vec` | `logos.mem.vec` | Vec<T>. |
| `std.collections.deque` | `logos.mem.collections.vec_deque` | VecDeque. |
| `std.collections.hashmap` | `logos.mem.collections.hash_map` | HashMap. |
| `std.collections.set` | `logos.mem.collections.hash_set` | HashSet. |
| `std.collections.btree` | `logos.mem.collections.btree_map` | BTreeMap. |
| `std.fmt` | `logos.mem.fmt` | format!() runtime path. Traits factor to `logos.lang.fmt` during Phase 4. |
| `std.io.bytes` | `logos.mem.bytes` | Bytes/BytesMut. No Rust core analog; consistent with ecosystem `bytes` crate position. |
| `std.data.persistent.cow` | `logos.mem.persistent.cow` | CoW arena. |
| `std.data.persistent.descent` | `logos.mem.persistent.descent` | B+tree descent. |
| `std.data.persistent.handle` | `logos.mem.persistent.handle` | Handle types. |
| `std.data.persistent.iter` | `logos.mem.persistent.iter` | Iterators. |
| `std.data.persistent.mutate` | `logos.mem.persistent.mutate` | Mutation operations. |
| `std.data.persistent.node` | `logos.mem.persistent.node` | Node types. |
| `std.data.persistent.shuttle` | `logos.mem.persistent.shuttle` | Shuttle (cursor). |
| `std.data.persistent.store` | `logos.mem.persistent.store` | Snap/Store. |
| `std.encoding.base64` | `logos.mem.encoding.base64` | Allocates output buffer. |
| `std.encoding.hex` | `logos.mem.encoding.hex` | Allocates output buffer. |
| `std.encoding.csv` | `logos.mem.encoding.csv` | Parses into containers. |
| `std.encoding.json` | `logos.mem.encoding.json` | Heavy Hermes/container allocation. |

**Count: ~23 packages → mem** (plus 17 from Hermes — see sub-section below).

### → logos.std (full)

| Current | Target | Notes |
|---|---|---|
| `std.io` | `logos.std.io` | Top-level io module. Read/Write traits live here, not in lang. |
| `std.io.read` | `logos.std.io` | **MERGED** — Read trait. |
| `std.io.write` | `logos.std.io` | **MERGED** — Write trait. |
| `std.io.buffered` | `logos.std.io` | **MERGED** — BufReader/BufWriter. |
| `std.io.fs` | `logos.std.fs` | File/OpenOptions/etc. |
| `std.io.fs.path` | `logos.std.path` | Path/PathBuf. |
| `std.io.net` | `logos.std.net` | TcpStream/UdpSocket/IpAddr. |
| `std.io.net.tls` | `logos.std.net.tls` | TLS wrappers. |
| `std.io.net.url` | `logos.std.url` | URL parser. Promoted to top-level (cf. `url` crate). |
| `std.io.http` | `logos.std.http` | HTTP. Top-level. |
| `std.io.pipe` | `logos.std.io.pipe` | Pipe IPC. |
| `std.io.linux.uring` | `logos.std.os.linux.uring` | Linux-only, OS-namespaced. |
| `std.sync` | `logos.std.sync` | Mutex/RwLock/CondVar — OS primitives. |
| `std.sync.atomic` | `logos.std.sync.atomic` | Atomic ops. |
| `std.lang.thread` | `logos.std.thread` | **RELOCATED** — pthread wrapper, OS-dependent, was misplaced in lang. |
| `std.sys.args` | `logos.std.env` | env::args(). |
| `std.sys.env` | `logos.std.env` | env vars. |
| `std.sys.os` | `logos.std.os` | pid/uid/hostname → std::os::unix::* in Rust. |
| `std.sys.process` | `logos.std.process` | Command/Child. |
| `std.sys.signal` | `logos.std.os.unix.signal` | Unix signals. |
| `std.sys.fiber` | `logos.std.rt.fiber` | Logos green-thread runtime. Separate namespace from `thread`. |
| `std.time` | `logos.std.time` | Duration/Instant/SystemTime. |
| `std.time.datetime` | `logos.std.time.datetime` | Calendar dates (Logos extension; no Rust analog). |
| `std.math.random` | `logos.std.random` | Needs entropy source — OS-touching. |
| `std.log` | `logos.std.log` | Logging — writes to stderr/file. |
| `std.crypto` | `logos.std.crypto` | May use OS RNG, may touch FS for keys. |
| `std.testing` | `logos.std.testing` | Test harness — writes results to stderr. |

**Count: ~27 packages → std.**

---

## Hermes per-package split

`std.hermes.*` — 32 sub-packages. The cut is read/static vs
mutable/allocating. Each row is a hypothesis verified at Phase 4
when the file actually moves and layer-enforcement runs.

### → logos.lang.hermes.* (read/static; 15 packages)

| Current | Target | Reason |
|---|---|---|
| `std.hermes.view` | `logos.lang.hermes.view` | HermesView (read-only window). |
| `std.hermes.anyval` | `logos.lang.hermes.anyval` | AnyVal read-mode. |
| `std.hermes.scalar` | `logos.lang.hermes.scalar` | Scalar access, pure read. |
| `std.hermes.datatag` | `logos.lang.hermes.datatag` | Tag enum definitions. |
| `std.hermes.tags` | `logos.lang.hermes.tags` | Tag constants. |
| `std.hermes.typetag` | `logos.lang.hermes.typetag` | Type-tag value types. |
| `std.hermes.relptr` | `logos.lang.hermes.relptr` | Relative pointer arithmetic. |
| `std.hermes.relptr_traits` | `logos.lang.hermes.relptr_traits` | Trait surface for relptr. |
| `std.hermes.stringify` | `logos.lang.hermes.stringify` | Format into caller buffer. |
| `std.hermes.hbs_read` | `logos.lang.hermes.hbs_read` | Wire-format parser, read into provided buffer. |
| `std.hermes.pat` | `logos.lang.hermes.pat` | Pattern matching navigation, read-only. |
| `std.hermes.check` | `logos.lang.hermes.check` | Validation pass, read-only. |
| `std.hermes.equal` | `logos.lang.hermes.equal` | Equality comparison, read-only. |
| `std.hermes.hashing` | `logos.lang.hermes.hashing` | Hash function, read-only. |
| `std.hermes.typed_value` | `logos.lang.hermes.typed_value` | Typed accessor wrappers. |

### → logos.mem.hermes.* (mutable/allocating; 17 packages)

| Current | Target | Reason |
|---|---|---|
| `std.hermes.alloc` | `logos.mem.hermes.alloc` | Allocation primitives. |
| `std.hermes.mem_holder` | `logos.mem.hermes.mem_holder` | RC arena owner. |
| `std.hermes.zone` | `logos.mem.hermes.zone` | Zone<M> handle. |
| `std.hermes.own` | `logos.mem.hermes.own` | Own<T> RC wrapper. |
| `std.hermes.release` | `logos.mem.hermes.release` | RC release. |
| `std.hermes.parser` | `logos.mem.hermes.parser` | Text parser, allocates objects. |
| `std.hermes.objectmap` | `logos.mem.hermes.objectmap` | Mutable container. |
| `std.hermes.array` | `logos.mem.hermes.array` | Mutable typed array. |
| `std.hermes.ctr` | `logos.mem.hermes.ctr` | Mutable container builder. |
| `std.hermes.map` | `logos.mem.hermes.map` | Mutable map. |
| `std.hermes.clone` | `logos.mem.hermes.clone` | Clone-into-new-zone. |
| `std.hermes.hbs_write` | `logos.mem.hermes.hbs_write` | Wire-format writer, allocates output. |
| `std.hermes.string` | `logos.mem.hermes.string` | Arena-backed string. |
| `std.hermes.document` | `logos.mem.hermes.document` | Document builder. |
| `std.hermes.registry` | `logos.mem.hermes.registry` | Dynamic type registry. |
| `std.hermes.tag_system` | `logos.mem.hermes.tag_system` | Runtime tag system. |
| `std.hermes.decimal` | `logos.mem.hermes.decimal` | Decimal arithmetic + allocation. Verify Phase 4. |

---

## Splits and merges summary

**Splits** (one current package → multiple target packages):
- `std.lang.text` → `logos.lang.str` (read) + `logos.mem.string` (owned).
- `std.fmt` → `logos.lang.fmt` (traits) + `logos.mem.fmt` (runtime).
- `std.lang.text.regex` → `logos.lang.regex` + `logos.mem.regex` (audit at Phase 4).

**Merges** (multiple current packages → one target package):
- `std.lang.ord` + `std.lang.cmp` → `logos.lang.cmp` (Rust convention).
- `std.lang.arith` + `std.lang.range` + `std.lang.drop` + `std.lang.ops`
  → `logos.lang.ops` (Rust convention).
- `std.io.read` + `std.io.write` + `std.io.buffered` + `std.io` →
  `logos.std.io` (Rust's `std::io` is flat).
- `std.sys.args` + `std.sys.env` → `logos.std.env`.

**Relocations** (cross-layer move):
- `std.lang.thread` → `logos.std.thread` (was misplaced in lang —
  pthread is OS).

**Promotions** (sub → top-level):
- `std.io.net.url` → `logos.std.url` (matches `url` crate).
- `std.io.http` → `logos.std.http`.
- `std.io.bytes` → `logos.mem.bytes` (matches `bytes` crate).

---

## Total counts

| Layer | Packages (target, post-merge) |
|---|---|
| logos.lang | ~20 + 15 hermes = ~35 |
| logos.mem | ~23 + 17 hermes = ~40 |
| logos.std | ~27 |
| **Total** | **~102** |

Down from 106 current (4 merges absorbed: ord, arith+range+drop
into ops, io.* into io, sys.args into env, plus the split of text
adds back two).

---

## What this doc does NOT cover

- **Per-fn or per-type granularity within a package.** If a single
  current package has both read-only and allocating items, the
  Phase 4 commit may need to refactor the file split before
  layering. Most likely sites: `std.hermes.clone`,
  `std.fmt` (Display trait + format! runtime), `std.lang.iter`
  (Iterator trait + Collect-to-Vec adapter). These get factored
  during Phase 4 migration of the parent file, not pre-emptively.
- **`rt/` native runtime support** (fiber_ctx.S, atomic_ops.S,
  uring wrappers in C/asm) — unchanged. Continues to link
  alongside whichever layer needs it.
