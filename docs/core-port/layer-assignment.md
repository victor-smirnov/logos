# Per-Package Layer Assignment

Authoritative table assigning each of the 106 current `std.*`
packages to one of the three target layers: **lang** (no-alloc,
no-OS), **mem** (heap, no-OS), **std** (full).

Companion to [three-layer-split.md](three-layer-split.md). This
table is the source of truth for Phase 4 migration commits.

Status: **draft for review.** Verified per-package during Phase 4
when actual code moves and layer enforcement (Phase 6.A) catches
violations. The most likely point of revision is the Writ split
(`std.writ.*` 32 packages → ~15 lang + ~17 mem) — some packages
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
`std.writ.*` and `std.io.*` follows.

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
| `std.lang.datatypes` | `logos.lang.writ.fabric` | Datatype/Storage/Container traits. Writ-fabric, no alloc. |
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
| `std.encoding.json` | `logos.mem.encoding.json` | Heavy Writ/container allocation. |

**Count: ~23 packages → mem** (plus 17 from Writ — see sub-section below).

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

## Writ per-package split

`std.writ.*` — 32 sub-packages. The cut is read/static vs
mutable/allocating. Each row is a hypothesis verified at Phase 4
when the file actually moves and layer-enforcement runs.

### → logos.lang.writ.* (read/static; 15 packages)

| Current | Target | Reason |
|---|---|---|
| `std.writ.view` | `logos.lang.writ.view` | WritView (read-only window). |
| `std.writ.anyval` | `logos.lang.writ.anyval` | AnyVal read-mode. |
| `std.writ.scalar` | `logos.lang.writ.scalar` | Scalar access, pure read. |
| `std.writ.datatag` | `logos.lang.writ.datatag` | Tag enum definitions. |
| `std.writ.tags` | `logos.lang.writ.tags` | Tag constants. |
| `std.writ.typetag` | `logos.lang.writ.typetag` | Type-tag value types. |
| `std.writ.relptr` | `logos.lang.writ.relptr` | Relative pointer arithmetic. |
| `std.writ.relptr_traits` | `logos.lang.writ.relptr_traits` | Trait surface for relptr. |
| `std.writ.stringify` | `logos.lang.writ.stringify` | Format into caller buffer. |
| `std.writ.hbs_read` | `logos.lang.writ.hbs_read` | Wire-format parser, read into provided buffer. |
| `std.writ.pat` | `logos.lang.writ.pat` | Pattern matching navigation, read-only. |
| `std.writ.check` | `logos.lang.writ.check` | Validation pass, read-only. |
| `std.writ.equal` | `logos.lang.writ.equal` | Equality comparison, read-only. |
| `std.writ.hashing` | `logos.lang.writ.hashing` | Hash function, read-only. |
| `std.writ.typed_value` | `logos.lang.writ.typed_value` | Typed accessor wrappers. |

### → logos.mem.writ.* (mutable/allocating; 17 packages)

| Current | Target | Reason |
|---|---|---|
| `std.writ.alloc` | `logos.mem.writ.alloc` | Allocation primitives. |
| `std.writ.mem_holder` | `logos.mem.writ.mem_holder` | RC arena owner. |
| `std.writ.zone` | `logos.mem.writ.zone` | Zone<M> handle. |
| `std.writ.own` | `logos.mem.writ.own` | Own<T> RC wrapper. |
| `std.writ.release` | `logos.mem.writ.release` | RC release. |
| `std.writ.parser` | `logos.mem.writ.parser` | Text parser, allocates objects. |
| `std.writ.objectmap` | `logos.mem.writ.objectmap` | Mutable container. |
| `std.writ.array` | `logos.mem.writ.array` | Mutable typed array. |
| `std.writ.ctr` | `logos.mem.writ.ctr` | Mutable container builder. |
| `std.writ.map` | `logos.mem.writ.map` | Mutable map. |
| `std.writ.clone` | `logos.mem.writ.clone` | Clone-into-new-zone. |
| `std.writ.hbs_write` | `logos.mem.writ.hbs_write` | Wire-format writer, allocates output. |
| `std.writ.string` | `logos.mem.writ.string` | Arena-backed string. |
| `std.writ.document` | `logos.mem.writ.document` | Document builder. |
| `std.writ.registry` | `logos.mem.writ.registry` | Dynamic type registry. |
| `std.writ.tag_system` | `logos.mem.writ.tag_system` | Runtime tag system. |
| `std.writ.decimal` | `logos.mem.writ.decimal` | Decimal arithmetic + allocation. Verify Phase 4. |

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
| logos.lang | ~20 + 15 writ = ~35 |
| logos.mem | ~23 + 17 writ = ~40 |
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
  layering. Most likely sites: `std.writ.clone`,
  `std.fmt` (Display trait + format! runtime), `std.lang.iter`
  (Iterator trait + Collect-to-Vec adapter). These get factored
  during Phase 4 migration of the parent file, not pre-emptively.
- **`rt/` native runtime support** (fiber_ctx.S, atomic_ops.S,
  uring wrappers in C/asm) — unchanged. Continues to link
  alongside whichever layer needs it.
