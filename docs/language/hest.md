# Hest in Logos

Hest is Logos's family of native communication protocols — RPC, streaming, and messaging. Where [Writ](writ.md) is how Logos data is *shaped*, Hest is how it *moves* between systems: threads, processes, machines, and — long-term — xPUs across an [LCM](../lcm/README.md) fabric. Like Writ, Hest is integrated into the language rather than bolted on as a library.

> **The name.** *Hest* is from Old English *hǣs*, "command, bidding" — the root of *behest* (a thing bidden or carried out on someone's order), fitting a layer whose job is to bear requests and commands between systems. It doubles as Danish/Norwegian *hest*, "horse" — the workhorse that bears the courier, on-theme for a transport. It completes the triad: **Logos** (Greek *λόγος*, "the word / reason") is meaning, a **Writ** is the written record, and **Hest** is the horse-courier that bears the Writ to a foreign court.

## Hest Is Built Into the Language

Hest is not an external messaging stack; it is built on the same substrate as the rest of Logos:

- **Writ is the payload format.** Requests, responses, and stream messages are [Writ](writ.md) documents — the same in-memory shape used everywhere else in Logos. There is no serialize/deserialize step at the API boundary; on a shared-memory transport, payloads are zero-copy.
- **Interfaces are an IDL with codegen.** Service contracts are declared in a small protobuf-like IDL; a generator emits client and server stubs that agree on endpoint identity without out-of-band setup.
- **Concurrency is Logos-native.** Handlers run on Logos's concurrency model (green fibers); streaming maps onto language-level channels — a call carries *N* input channels and *M* output channels, generalizing the unary / client-stream / server-stream / bidi shapes.
- **Designed for direct hardware implementation.** The wire shape and message-type set are kept small and regular, so a Hest protocol can be realized in hardware, not only software — the basis for hardware transport in [LCM](../lcm/README.md).

Result: a Hest call is, from the program's side, an ordinary typed call whose arguments and results are Writ values — no FFI boundary, no separate wire schema, no hand-written codec.

## The Family

Hest names the *family* of these protocols. Today it has one realized member:

- **HRPC** ("Hest RPC") — bidirectional typed RPC and streaming: unary calls, streams in either or both directions, IDL-driven codegen, a fixed 16-byte frame header plus a Writ payload, one connection multiplexing all calls. The C++ implementation is present and load-bearing; a Logos-side implementation on green fibers is planned. Full reference: [HRPC](../internals/hrpc.md). The `H` in HRPC is **Hest** (it does not stand for HTTP — Hest framing is its own, not HTTP/2).

Future members share the same payload model (Writ) and message discipline while varying the byte mover: shared-memory rings for intra-host calls, read-only mmap zones, and eventual hardware transports for LCM. The wire format is transport-agnostic; only the mover changes.

## Relationship to Writ and LCM

- **Writ** is the unit of *data*: a self-describing object graph, durable and relocatable as bytes.
- **Hest** is the unit of *movement*: it carries Writ documents between endpoints, preserving their bytes (zero-copy where the transport allows, validated on untrusted links).
- **[LCM](../lcm/README.md)** is the compute model these serve: many small xPUs near their data, talking over Hest as the universal transport. Hest is what makes "message-passing between compute units" concrete.

## See Also

- [Writ in Logos](writ.md) — the payload format Hest carries.
- [HRPC](../internals/hrpc.md) — the concrete protocol: wire format, session model, streaming, IDL, codegen, repository split.
- [LCM — Logos Compute Model](../lcm/README.md) — where Hest is the planned universal transport.
