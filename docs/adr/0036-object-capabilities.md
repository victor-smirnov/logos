# ADR 0036. Object capabilities: no ambient authority

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Depends on [0035](0035-effects.md) (effects) for the derived summary.

## Problem

- std carries ambient authority: any fn may open files, sockets, read the environment and the clock, spawn processes (`stdlib/std/io/{fs,net,http,pipe,linux}`, `std/env/env.logos:10-15`, `std/time`, `std/process`, `std/os`, `std/random`).
- `extern fn` is allowed in every package and every tier; any package can reach libc directly.
- The tier rule "lcm cannot do IO" is a convention (link order only).
- Code is written and run by agents. Least authority is the boundary of trust: a package should be unable to exceed the authority passed to it, checked by the compiler, not by review.
- Hest placement (ADR 0034 D7) needs to know what authority a vertex needs; Deem circuits' sinks (ADR 0033 D7) execute effects.
- Shapes that are already capability-like: Memoria's `Snapshot` / `Store` (public vtable, privileged tier reachable only by an explicit downcast, `map.logos:60-71`); Deem's source traits (authority to read a relation is the source value); ADR 0003's capability tokens for metafunctions (`ReflectCtx`, `InjectCtx`, `QueryCtx`).

## Decisions

### D1. Authority is a value

Every authority-bearing operation is a method on (or takes) a capability value. Capability types in the runtime tiers:

| Type | Authority | Attenuations (examples) |
|---|---|---|
| `Fs` / `Dir` | file system, rooted at a directory | `Dir::sub(path)`, `read_only()` |
| `Net` | sockets | `connect_only(host_set)`, `listen_only(port)` |
| `Clock` | wall and monotonic time | `monotonic_only()` |
| `Entropy` | randomness | |
| `Env` | environment variables, argv | `vars(names)` |
| `Proc` | spawning, signals | `spawn_only(program)` |
| `Stdio` | stdin / stdout / stderr | `stdout_only()` |
| `Device` / `Mmio` | device registers (lcm) | per region |

Properties: unforgeable (constructors private to the runtime); not `Copy`; `Clone` only where duplicating the authority is intended; attenuation returns a new value with less authority, never more; capabilities are ordinary Logos values (moved, borrowed, stored, passed to threads and Hest vertices under the usual rules).

### D2. The root

`main` receives the root bundle: `fn main(caps: Caps)`, where `Caps` holds one value of each D1 type. A `main()` without parameters has no authority. Tests receive a test bundle (temporary `Dir`, captured `Stdio`). Library code never constructs capabilities; it receives them.

### D3. Capabilities and effects

A capability is authority (a value); an effect is a summary (a set, ADR 0035). The link: `IO(k)` originates only at operations on a capability of kind `k` (ADR 0035 D3). A fn that is not handed a capability of kind `k`, directly or inside a value, cannot have `IO(k)` unless it calls an `extern fn` (D4). Consequence: for safe code, `E(f)` is predictable from the types of `f`'s parameters and captures.

### D4. FFI and unsafe are a grant

- `extern fn` declarations and `unsafe` blocks that can forge a capability (raw pointer into runtime structures, transmute of a capability type) are allowed only in packages granted `ffi` in the manifest (`logos.module` / `lforge.toml`).
- The trusted base is the set of packages with the `ffi` grant. Every other package is capability-safe: it cannot exceed the authority passed to it. The grant list of a build is reported (`--emit-facts`).
- std's runtime packages hold the grant; Deem, Canon, Memoria logic, user application code do not need it. The mem-tier libc uses of the ADR 0035 census move into a granted runtime package.

### D5. Compile time

Metacalls run with the `metafn` effect bound (ADR 0035 D5) and receive only ADR 0003's compile-time capabilities (`ReflectCtx`, `InjectCtx`, `QueryCtx`) as values. The JIT's symbol resolution (`jit.cpp:163`, currently every process symbol) is filtered to the granted runtime symbols. A build-time need for IO goes into a build script, not a metacall (ADR 0003 §4).

### D6. Deem, Hest, Memoria

- Deem: a query's sources are values (already); a circuit's effect sinks (ADR 0033 D7) receive their capabilities from the boundary that drives `step`.
- Hest: a vertex's required capabilities are part of its signature; placement (ADR 0034 D7) matches them against what a node offers; a capability crossing a radius is a remote capability (proxy), out of scope here.
- Memoria: stores and snapshots are capabilities; the privileged tier is reached by holding a privileged capability value, not by `dyn_data` downcast.

### D7. Migration

Ambient std APIs get capability-taking replacements first; the ambient forms are deprecated with a gate counting their uses in the tree, driven to zero, then deleted. No compatibility mode survives the arc (pre-release, break freely).

## Rows

| Row | Content | Acceptance |
|---|---|---|
| K0 | census: every ambient std API and every `extern fn` outside the runtime | ledger, pinned |
| K1 | `Caps` and the D1 types in the runtime; `main(caps)`; test bundle | fixtures per type; attenuation tests |
| K2 | std APIs on capabilities; ambient uses counted by a gate | gate count → 0 |
| K3 | `ffi` grant in manifests; `extern fn` / forging `unsafe` refused without it | planted `extern fn` in an ungranted package fails |
| K4 | `IO(k)` from capability methods (ADR 0035 E6) | effect fixtures through capabilities |
| K5 | metacall capabilities and JIT symbol filter (D5) | a metacall reaching libc fails |
| K6 | Memoria privileged tier by capability (D6) | `dyn_data` downcast path deleted |

## Open

- O1. Granularity of `Fs`: one type with runtime-checked attenuation, or distinct types per authority (`ReadDir`, `WriteDir`) checked statically. Recommendation: distinct types where the split is common (read vs write), runtime attenuation for paths and hosts.
- O2. `Clock` and `Entropy` vs determinism: holding them implies `Nondet`; deterministic replay (Memoria, Hest) may substitute recorded values through the same capability.
- O3. Revocation (membranes) and capabilities across radii (proxies): later.
- O4. Allocation: whether a heap is a capability (arenas, zones as values) or stays ambient with the `Alloc` effect only. Linked to ADR 0035 O3.
