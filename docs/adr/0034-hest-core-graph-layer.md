# ADR 0034. Hest core: the dataflow graph layer and its IR shared with DPlan

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). First ADR on Hest itself ([0010](0010-rename-hermes-to-writ-hest.md) only names it). Inputs: the Hest decisions of 2026-07-02/03 (memory notes, not ADRs), [0013](0013-incremental-dataflow-dbsp.md) §7 reaction model, [0025](0025-deem-batch-cursor-plane.md) lines 1230-1238 (operators over batch queues as the Deem/Hest convergence point), [0033](0033-incremental-deem-dbsp-translator.md).

## Problem

Hest is the dataflow aspect of Logos: the unit of composition above the fn is the operator graph. Decisions already taken (2026-07-02/03):

- DF is the physically grounded model; CF is an optimisation valid inside a synchrony radius. Programs are CF inside a radius, DF between radii; vertices are ordinary Logos fns.
- The graph is a language construct, not a library: the compiler must see it to fuse, partition, retarget and verify (type compatibility on edges, causality, stratification, state privacy).
- Execution model (LCM): internally synchronous xPUs, asynchronous messages over possibly unreliable channels, distributed memory and function blocks, CGRA / eFPGA logic blocks. Hardware without cache coherence takes the distributed model directly.
- Rejected neighbour: the actor model (dynamic topology, addressing as computation). Hest wants topology as structure.
- CIRCT is an oracle for design answers (DC vs Handshake det/nondet split, latency-insensitive protocols, SSP scheduling, ESI channels), not a dependency.

What exists in code: C++ HRPC (`src/hrpc/session.cpp`, `tools/hrpc_gen`, `hrpc.peg`) on the C++ reactor; Logos fibers (`stdlib/lcm/fiber/fiber.logos`), `Chan<T>` and `Latch` (`sync.logos:22,82`), `Reactor` (`stdlib/std/rt/fiber/reactor.logos:47`). Nothing else: no graph construct, no `Stream` / `Delta`, no Logos HRPC.

Since ADR 0031, Deem's plan is a dataflow graph (DPlan: scans, probes, aggregates, SCC fixpoints as nested loops) and ADR 0033 makes its incremental form a DBSP circuit with explicit `Delay` and `Integrate` nodes. Two dataflow IRs would be the duplication ADR 0032 removes elsewhere.

## Decisions

### D1. Scope of this ADR

The graph layer: its types, IR, checks and the software backends. Out of scope, each its own later ADR: hardware synthesis of vertex bodies (HLS: CF body → FSM + datapath), distributed transport beyond HRPC, clock calculus beyond epochs.

### D2. Types

- `Stream<T>`: a sequence of values indexed by epoch (logical time). One value per epoch per port (batches are values: `Stream<Vec<T>>`, `Stream<Delta<T>>`).
- `Delta<T>` from ADR 0033: a `Stream<Delta<T>>` is the change stream of a relation.
- Edge payloads crossing a radius are Writ-representable (relocatable, self-describing); a marker trait (`Reloc`, structural, at the level of `Send`) is derived by the compiler and required on every cross-radius edge. Inside a radius any type passes.

### D3. One graph IR, shared with DPlan

The graph IR is DPlan's operator and statement layer generalised, not a second IR:

| Node | Meaning | Origin |
|---|---|---|
| `Vertex(fn)` | a Logos fn from input ports to output ports, one firing per epoch | Hest |
| `Circuit(prog)` | a Deem program's incremental circuit (ADR 0033), a vertex whose body is DPlan | Deem |
| `Delay` | z⁻¹: output at epoch t = input at t-1 | shared |
| `Integrate` | running sum of a `Delta` stream | shared |
| `Exchange(key)` | partition a stream by key across instances | Hest |
| `Merge` | NONDETERMINISTIC interleaving of streams; explicit, typed | Hest |
| `Source` / `Sink` | boundary to the outside (capability-bearing, ADR 0036) | shared |

Deem circuits lower into this IR without translation; user graphs lower into it from the surface (D5). One verifier (`graph_verify`, ICE on violation, as `core_verify` / `dplan_verify`).

### D4. Determinism contract

The core is deterministic: Kahn process network discipline (blocking reads, no peeking) plus synchronous epochs. `Merge` is the only source of nondeterminism and is an explicit typed escape (like `unsafe`): a graph containing `Merge` is marked nondeterministic and the mark propagates (effect `Nondet`, ADR 0035).

### D5. Surface

An item kind for graphs with typed ports. Syntax is open (O1); the entry path follows the Deem precedent: a metaprog handler first, a grammar keyword after the capability is proven. A vertex is any fn whose effects fit the vertex bound (D7) and whose state is owned by the vertex.

### D6. Checks (rules over graph facts)

Run on the graph IR as Datalog (ADR 0028 engine now, Deem later), each with a derivation-tree diagnostic:

- edge type compatibility, including `Reloc` on cross-radius edges;
- causality: every cycle contains a `Delay` (Lustre); for a Deem circuit this is the semi-naive round;
- state privacy: a vertex's state is owned by the vertex and no borrow of it escapes a firing (borrow facts of ADR 0028);
- stratification: negation and aggregation across a cycle need a `Delay`;
- coordination map (CALM): monotone subgraphs are marked coordination-free, non-monotone boundaries are the barriers; the map is a report, not a refusal.

### D7. Placement and bounds

A vertex has an effect set (ADR 0035) and a capability set (ADR 0036). Placement on an xPU / radius is a constraint problem over these sets and the node's declared resources, solved by a Deem program. Vertices bound to `synth` (ADR 0035 D5) are candidates for hardware mapping; the mapping itself is out of scope (D1).

### D8. Backends

| Backend | What | When |
|---|---|---|
| fused | DF → CF: the graph inside one radius fused into loops in one fn (what the Deem emitter does today for a whole program) | first |
| local concurrent | vertices on pinned fibers, edges on `Chan<T>` with credit-based backpressure | second |
| distributed | edges over Hest transport (HRPC as the first member, a delta-stream profile as the second: epochs, exactly-once, credits) | after a Logos HRPC |
| hardware | `synth` subgraphs to CGRA / eFPGA | separate ADR |

The fused backend is the default inside a radius. A program's meaning does not depend on the backend (D4); the D9 oracle of ADR 0033 extends to graphs: every backend's per-epoch outputs equal the fused backend's.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| H0 | graph IR node kinds in `dplan` (`Delay`, `Integrate` with ADR 0033 I2; `Vertex`, `Exchange`, `Merge`, `Source`, `Sink`), `graph_verify`, `LOGOS_DEEM_DUMP=graph` | golden dumps; planted-violation canary per invariant |
| H1 | `Stream<T>`, `Reloc` derivation | `Reloc` refusal fixtures (absolute pointer, escaping borrow) |
| H2 | surface (metaprog handler) for user graphs; fused backend | fixtures: pipeline, diamond, cycle with `Delay`; a cycle without `Delay` refused with the cycle named |
| H3 | D6 checks as rules with derivation diagnostics | one fail fixture per check |
| H4 | local concurrent backend, backpressure | equality with fused per epoch; stress with bounded channels |
| H5 | Deem circuits as `Circuit` vertices; an incremental Deem program inside a user graph | ADR 0033 D9 oracle through the graph |
| H6 | Logos HRPC and the distributed backend | two-process fixture; equality with fused |

## Open

- O1. Surface syntax and item kind (`flow` item with ports vs fns over `Stream` args). Recommendation: an item with declared ports (the compiler sees the topology without inference).
- O2. Epochs now, clocks as types (Lustre / N-synchronous) later: confirm that the epoch model does not block the later refinement.
- O3. Tick granularity and backpressure policy of interactive programs (ADR 0033 O3).
- O4. Error model on unreliable channels (statistical handling per the LCM declaration): a vertex-level concern or a transport profile.
