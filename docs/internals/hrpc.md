# HRPC

*Stub — to be written.*

HRPC is Logos's bidirectional, Hermes-native RPC and transport layer.

Planned coverage:

- Wire protocol and framing.
- Bidirectional streaming model.
- Hermes as the payload format (zero-copy where possible).
- Use as a universal host↔guest transport (shared-memory rings, r/o mmap zones).
- Relationship to the io_uring reactor and green fibers.
- Schema, versioning, and codegen story.
- Long-term role as a low-level, RTL-friendly protocol for xPU/MAA targets.
