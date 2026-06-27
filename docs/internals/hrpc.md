# HRPC

HRPC is Logos's bidirectional, Hest RPC and streaming protocol. It is the planned universal transport in [LCM](../lcm/README.md) — host↔guest, intra-die, cross-die, cross-machine — and, in the near term, the connective tissue between Logos and C++ components inside the project.

This document describes what is in place today.

## Status

- **C++ implementation** — present and load-bearing. Lives in [src/hrpc/](../../src/hrpc) and [include/logos/hrpc/](../../include/logos/hrpc). Used for Logos↔C++ component communication and as the reference for the wire format.
- **Logos implementation** — not yet. Planned alongside the broader LCM toolchain.
- **Hardware HRPC** — long-term target, see [LCM](../lcm/README.md). Not in scope for this document.

The C++ side currently runs on Logos's green-fiber reactor (`logos::reactor::TcpSocket`, `Channel`, `Mutex`, `ConditionVariable`). That coupling is going to change — see [Repository Split](#repository-split) below.

## What HRPC Is

HRPC is conceptually similar to gRPC: a typed RPC protocol with unary calls, streaming in either or both directions, and IDL-driven codegen. The differences are deliberate:

- **Writ is the payload format**, not protobuf. Requests, responses, and stream messages are Writ documents — the same in-memory shape used everywhere else in Logos. No serialise/deserialise step at the API boundary; on a shared-memory transport, payloads are zero-copy.
- **No HTTP/2.** Framing is a fixed 16-byte header followed by a Writ blob; one TCP connection multiplexes all calls.
- **Designed for direct hardware implementation.** The wire shape and message-type set are kept small and regular so HRPC can be implemented in HDL without sweating an HTTP/2 stack. Today only the software path is realised.

The wire format and message type codes are wire-compatible with Memoria's HRPC; [Memoria](https://github.com/victor-smirnov/memoria) is the original venue for the protocol and is currently the other implementer.

## Wire Format (Summary)

Every message is `MessageHeader` (16 bytes, little-endian) followed by an optional fixed field block and an optional Writ payload.

```
+--------+--------+----------------+------------------+----------------+
| size   | bits   | call_id (u64)  | optional fields  | Writ payload |
| (u32)  | (u32)  |                | (e.g. EndpointID)|                |
+--------+--------+----------------+------------------+----------------+
   4         4           8              0 or 32          variable
```

`bits` packs `message_type` (6 bits), `channel_code` (16 bits), and an `optionals` nibble (currently: `has_endpoint_id`).

The `MessageType` enum (numeric values match Memoria HRPC):

| Code | Type | Direction |
|------|------|-----------|
| 0 | `SessionStart` | both — handshake |
| 1 | `SessionClose` | both |
| 2 | `Call` | client → server |
| 3 | `Return` | server → client |
| 4 | `CallChannelMessage` | client → server (input stream) |
| 5 | `ContextChannelMessage` | server → client (output stream) |
| 6 | `CallCloseInput` | client → server (end of input stream) |
| 7 | `ContextCloseInput` | server → client |
| 8 | `CallCloseOutput` | client → server |
| 9 | `ContextCloseOutput` | server → client (end of output stream) |
| 10 | `CallBufferReset` | flow control |
| 11 | `ContextBufferReset` | flow control |
| 12 | `CancelCall` | client → server |

Authoritative source: [include/logos/hrpc/common.hpp](../../include/logos/hrpc/common.hpp).

## Endpoint Identification

Endpoints are addressed by 256-bit `EndpointID`s. Two construction paths:

- **Random** — `make_random_endpoint_id()` for ephemeral or generated endpoints.
- **Deterministic from a name** — `endpoint_id_from_name("package.Service/method")` derives the ID via FNV-1a with four independent seeds. Both ends compute the same ID from the same name; this is what `hrpc_gen` emits.

256 bits is large enough to avoid collision pressure under random IDs and large enough to absorb a name-derived hash without coordination.

## Session Model

A `Session` wraps one connection (today: one `TcpSocket`) in full-duplex mode. Either side can issue calls; client/server is purely a handshake role. Session lifecycle:

1. `Session::start()` — exchange `SESSION_START` messages, negotiate parameters.
2. `Session::run()` — driven from a dedicated fiber, processes inbound messages and dispatches them to either pending client-side calls (`pending_calls_`) or active server-side contexts (`contexts_`).
3. `Session::call()` / `call_async()` — issue an RPC. Returns a `Response` (sync) or a `Call` handle (async, with `wait()` / streaming methods).
4. `Session::close()` — sends `SESSION_CLOSE`, marks the session closed.

Call IDs are assigned per-side: client uses even IDs, server uses odd IDs, both increment by 2. Disjoint by construction, no central coordination.

Server-side handlers register with the session's `EndpointRegistry`. A handler is a `std::move_only_function<Response(Context&)>`. The handler can block on the reactor (e.g. `ctx.pop()` to read from an input stream, `ctx.push(...)` to send on an output stream, or even reach into the same session and issue further calls).

Authoritative sources: [session.hpp](../../include/logos/hrpc/session.hpp), [context.hpp](../../include/logos/hrpc/context.hpp), [endpoint.hpp](../../include/logos/hrpc/endpoint.hpp).

## Streaming Model

A call has *N* input channels (client → server) and *M* output channels (server → client), each addressed by a 16-bit `channel_code`. Both `N` and `M` may be zero. This generalises the four gRPC variants (unary, server-stream, client-stream, bidi) to arbitrary in/out fan-out per call.

- Server reads from an input channel via `Context::pop(msg, code)` — fiber-blocking.
- Server writes to an output channel via `Context::push(msg, code)` — sends a `ContextChannelMessage` over the wire immediately.
- Client mirror: `Call::push(msg, code)` writes a `CallChannelMessage`; `Call::pop(msg, code)` reads from a `ContextChannelMessage`-fed channel.
- End-of-stream is signalled by an explicit `*CloseOutput` message; `pop` returns `false` when a sentinel arrives.

Flow control is via `*BufferReset` messages; the current C++ implementation uses these conservatively.

## IDL and Code Generation

Service interfaces are defined in `.hrpc` files — a small protobuf-like IDL.

```hrpc
package echo;

message PingRequest {
    required string name  = 1;
    optional uint32 count = 2;
}

message PingResponse {
    required string greeting = 1;
    required uint32 counter  = 2;
}

service Echo {
    rpc ping(PingRequest)         returns (PingResponse);          // unary
    rpc upload(stream Chunk)      returns (UploadResult);          // client-stream
    rpc subscribe(PingRequest)    returns (stream ChatMessage);    // server-stream
    rpc chat(stream ChatMessage)  returns (stream ChatMessage);    // bidi
}
```

Supported types: `string`, `bytes`, the integer/float scalars, `bool`, `repeated T`, `map<K,V>`, nested messages, `enum`, `oneof`. The grammar is in [tools/peg_gen/grammars/hrpc.peg](../../tools/peg_gen/grammars/hrpc.peg). Worked example: [tools/hrpc_gen/examples/echo.hrpc](../../tools/hrpc_gen/examples/echo.hrpc).

`hrpc_gen` generates client and server stubs from a `.hrpc` file. The generated code derives endpoint IDs from `"package.Service/method"`, so client and server agree on IDs without out-of-band setup.

## Repository Split

Today HRPC's C++ implementation depends on Logos's green-fiber reactor (`logos::reactor::TcpSocket`, `Channel`, etc.). That coupling is **not** the long-term shape:

- **The reactor is moving out of this repository.** The Logos reactor is a C++ component that does not need to ship in lock-step with the Logos compiler and standard library; it is going to its own repository.
- **The HRPC C++ stack will follow ordinary C++ infrastructure** — OS threads, C++20/26 coroutines, and either Boost.Asio or raw sockets — rather than continuing to ride on the Logos reactor. The Logos compiler is built on threads + coroutines for its own reasons (see memory: `project_compiler_threads_no_fibers`); HRPC-in-C++ will follow the same approach.
- **Logos-side green fibers stay in Logos.** When a Logos implementation of HRPC lands, *that* one will use Logos green fibers, because that is how Logos programs do concurrency.

The wire format does not change across this split. A Logos client can talk to a C++ server (or the other way around) regardless of which side's runtime is fibers or threads.

## Roadmap

- **Logos-side implementation.** Mirror the C++ surface (`Session`, `Context`, `Call`, `EndpointRegistry`) in Logos, on top of Logos green fibers and Hest-native channels. Codegen path through `hrpc_gen` to Logos stubs.
- **Decouple from the Logos reactor.** Port the C++ implementation off `logos::reactor::*` to threads + coroutines (Boost.Asio or sockets); migrate the reactor itself out of this repository.
- **Transports beyond TCP.** Shared-memory rings (intra-host), read-only mmap zones, eventual hardware transports for LCM. The wire format is transport-agnostic; only the byte mover changes.
- **Schema evolution.** Versioning rules, additive vs. breaking change semantics, on-the-wire negotiation during `SESSION_START`.
- **Hardware HRPC.** Long-term — IDL → IP generation, see [LCM](../lcm/README.md).
