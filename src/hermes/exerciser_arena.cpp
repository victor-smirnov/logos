// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/varint.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace logos::hermes;

// ============================================================================
// RelativePtr tests
// ============================================================================

static void test_relative_ptr() {
    std::printf("--- RelativePtr ---\n");

    // Round-trip: place two values in a buffer, point between them.
    alignas(8) uint8_t buf[64] = {};
    int32_t* a = reinterpret_cast<int32_t*>(buf + 0);
    int32_t* b = reinterpret_cast<int32_t*>(buf + 32);
    *a = 42;
    *b = 99;

    // Segment-relative: buf is the base, offsets are from buf[0].
    auto* ptr = reinterpret_cast<RelativePtr<int32_t>*>(buf + 8);
    ptr->set(b, buf);  // set offset = (b - buf)
    LOGOS_ASSERT(ptr->get(buf) == b, "HERMES-RELPTR-001",
        "RelativePtr deref must return the original target address");
    LOGOS_ASSERT(*ptr->get(buf) == 99, "HERMES-RELPTR-001",
        "RelativePtr deref must yield the correct value (expected 99, got {})", *ptr->get(buf));

    // Null
    RelativePtr<int32_t> null_ptr;
    LOGOS_ASSERT(null_ptr.is_null(), "HERMES-RELPTR-002",
        "Default-constructed RelativePtr must be null");
    LOGOS_ASSERT(null_ptr.get(buf) == nullptr, "HERMES-RELPTR-002",
        "Null RelativePtr::get() must return nullptr");

    LOGOS_TRACE("hermes.relptr", "status", "pass");
    std::printf("  RelativePtr: OK\n");
}

// ============================================================================
// AnyVal tests
// ============================================================================

static void test_tagged_ptr_pointer_mode() {
    std::printf("--- AnyVal pointer mode ---\n");

    // Pointer mode round-trip with various arena offsets (segment-relative, always >= 0).
    arena_offset_t test_offsets[] = {
        arena_offset_t{2}, arena_offset_t{256}, arena_offset_t{1024},
        arena_offset_t{65536}, arena_offset_t{0x7FFFFFFE}
    };
    for (arena_offset_t offset : test_offsets) {
        AnyVal p = AnyVal::from_offset(offset);
        LOGOS_ASSERT(p.is_pointer(), "HERMES-TAGPTR-003",
            "AnyVal from offset {} must be in pointer mode", offset);
        LOGOS_ASSERT(!p.is_value(), "HERMES-TAGPTR-003",
            "AnyVal from offset {} must not be in value mode", offset);
        LOGOS_ASSERT(!p.is_null(), "HERMES-TAGPTR-003",
            "AnyVal from non-zero offset {} must not be null", offset);

        arena_offset_t recovered = p.to_offset();
        LOGOS_ASSERT(recovered == offset, "HERMES-TAGPTR-001",
            "AnyVal pointer mode round-trip failed: wrote {}, got {}", offset, recovered);
    }

    // Null
    AnyVal null_p;
    LOGOS_ASSERT(null_p.is_null(), "HERMES-TAGPTR-004",
        "Default-constructed AnyVal must be null");
    LOGOS_ASSERT(null_p.raw() == 0, "HERMES-TAGPTR-004",
        "Null AnyVal raw bits must be all zeros");

    LOGOS_TRACE("hermes.tagptr.pointer", "status", "pass");
    std::printf("  AnyVal pointer mode: OK\n");
}

static void test_tagged_ptr_value_mode() {
    std::printf("--- AnyVal value mode ---\n");

    // Embed int8_t (TinyInt, type_hash=20)
    {
        int8_t val = -42;
        AnyVal p = AnyVal::from_value(val, 20);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003",
            "AnyVal with embedded int8_t must be in value mode");
        LOGOS_ASSERT(p.value_type_hash() == 20, "HERMES-TAGPTR-002",
            "Embedded int8_t type_hash must be 20, got {}", p.value_type_hash());
        int8_t extracted = p.as_value<int8_t>();
        LOGOS_ASSERT(extracted == val, "HERMES-TAGPTR-002",
            "Embedded int8_t round-trip failed: wrote {}, got {}", val, extracted);
    }

    // Embed uint8_t (UTinyInt, type_hash=21)
    {
        uint8_t val = 200;
        AnyVal p = AnyVal::from_value(val, 21);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003", "");
        LOGOS_ASSERT(p.as_value<uint8_t>() == val, "HERMES-TAGPTR-002",
            "Embedded uint8_t round-trip failed");
    }

    // Embed int16_t (SmallInt, type_hash=22)
    {
        int16_t val = -12345;
        AnyVal p = AnyVal::from_value(val, 22);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003", "");
        LOGOS_ASSERT(p.as_value<int16_t>() == val, "HERMES-TAGPTR-002",
            "Embedded int16_t round-trip failed: wrote {}, got {}", val, p.as_value<int16_t>());
    }

    // Embed int32_t (Integer, type_hash=23)
    {
        int32_t val = -1234567;
        AnyVal p = AnyVal::from_value(val, 23);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003", "");
        LOGOS_ASSERT(p.as_value<int32_t>() == val, "HERMES-TAGPTR-002",
            "Embedded int32_t round-trip failed: wrote {}, got {}", val, p.as_value<int32_t>());
    }

    // Embed uint32_t (UInteger, type_hash=25) — must fit in 24 bits.
    {
        uint32_t val = 0x00BEEF42;
        AnyVal p = AnyVal::from_value(val, 25);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003", "");
        LOGOS_ASSERT(p.as_value<uint32_t>() == val, "HERMES-TAGPTR-002",
            "Embedded uint32_t round-trip failed");
    }

    // Embed bool (Boolean, type_hash=37)
    {
        // Use uint8_t for embedding since bool has implementation-defined size
        uint8_t val_true = 1;
        AnyVal p = AnyVal::from_value(val_true, 37);
        LOGOS_ASSERT(p.is_value(), "HERMES-TAGPTR-003", "");
        LOGOS_ASSERT(p.as_value<uint8_t>() == 1, "HERMES-TAGPTR-002",
            "Embedded boolean true round-trip failed");

        uint8_t val_false = 0;
        AnyVal pf = AnyVal::from_value(val_false, 37);
        LOGOS_ASSERT(pf.as_value<uint8_t>() == 0, "HERMES-TAGPTR-002",
            "Embedded boolean false round-trip failed");
    }

    LOGOS_TRACE("hermes.tagptr.value", "status", "pass");
    std::printf("  AnyVal value mode: OK\n");
}

static void test_tagged_ptr_set_pointer() {
    std::printf("--- AnyVal set_pointer ---\n");

    // Use aligned buffer to simulate arena objects.
    alignas(8) uint8_t buf[128] = {};
    int32_t* target = reinterpret_cast<int32_t*>(buf + 64);
    *target = 777;

    AnyVal* slot = reinterpret_cast<AnyVal*>(buf + 0);
    slot->set_pointer(target, buf);

    LOGOS_ASSERT(slot->is_pointer(), "HERMES-TAGPTR-003",
        "AnyVal after set_pointer must be in pointer mode");
    LOGOS_ASSERT(*slot->as_ptr<int32_t>(buf) == 777, "HERMES-TAGPTR-001",
        "AnyVal set_pointer/as_ptr round-trip failed");

    LOGOS_TRACE("hermes.tagptr.set_pointer", "status", "pass");
    std::printf("  AnyVal set_pointer: OK\n");
}

// ============================================================================
// TypeTag tests
// ============================================================================

static void test_type_tag() {
    std::printf("--- TypeTag ---\n");

    // Core type examples from the spec.
    struct TestCase {
        uint64_t type_hash;
        TagDescriptor descriptor;
        size_t expected_bytes;
    };

    TestCase cases[] = {
        // Logos byte-direct encoding: type_code ≤ 222 → single byte in obj[-1].
        {20, TagDescriptor::Data, 1},   // TinyInt
        {26, TagDescriptor::Data, 1},   // BigInt
        {28, TagDescriptor::Data, 1},   // Varchar
        {31, TagDescriptor::Data, 1},   // Double
        {37, TagDescriptor::Data, 1},   // Boolean

        // Container tags — descriptor is derived from type_code.
        {98,  TagDescriptor::Map,   1}, // TinyObjectMap (Hermes)
        {100, TagDescriptor::Array, 1}, // ObjectArray
        {101, TagDescriptor::Map,   1}, // ObjectMap

        // type_code > 222 uses multi-byte encoding: header + LE code bytes.
        {223,       TagDescriptor::Data, 2},   // smallest multi-byte
        {0xFFFF,    TagDescriptor::Data, 3},   // 2 code bytes
        {0x010000,  TagDescriptor::Data, 4},   // 3 code bytes

        // Sentinel: type_code == 0 is "unset" — 0 bytes on the wire.
        {0, TagDescriptor::Data, 0},
    };

    alignas(8) uint8_t buf[64] = {};

    for (const auto& tc : cases) {
        TypeTag original(tc.type_hash, tc.descriptor);

        LOGOS_ASSERT(original.byte_length() == tc.expected_bytes, "HERMES-TYPETAG-001",
            "TypeTag byte_length for type_hash={} descriptor={}: expected {}, got {}",
            tc.type_hash, static_cast<int>(tc.descriptor),
            tc.expected_bytes, original.byte_length());
        LOGOS_ASSERT(original.type_code() == tc.type_hash, "HERMES-TYPETAG-001",
            "TypeTag type_code round-trip failed for type_hash={}", tc.type_hash);
        LOGOS_ASSERT(original.descriptor() == tc.descriptor, "HERMES-TYPETAG-001",
            "TypeTag descriptor round-trip failed for type_hash={}", tc.type_hash);

        // Write to buffer and read back.
        uint8_t* obj_addr = buf + 32; // Enough room for tag before this address.
        std::memset(buf, 0, 64);
        original.write_before(obj_addr);
        TypeTag recovered = TypeTag::read_before(obj_addr);

        LOGOS_ASSERT(recovered == original, "HERMES-TYPETAG-002",
            "TypeTag write/read round-trip failed for type_hash={}: "
            "wrote 0x{:X}, read 0x{:X}",
            tc.type_hash, original.raw(), recovered.raw());
    }

    LOGOS_TRACE("hermes.typetag", "status", "pass");
    std::printf("  TypeTag: OK\n");
}

// ============================================================================
// VarInt tests
// ============================================================================

static void test_varint() {
    std::printf("--- VarInt ---\n");

    uint64_t test_values[] = {
        0, 1, 127, 248,                     // 1-byte range
        249, 255,                            // 2-byte range
        256, 65535,                          // 3-byte range
        65536, 16777215,                     // 4-byte range
        16777216, 0xFFFFFFFF,                // 5-byte range
        0x100000000ULL, 0xFFFFFFFFFF,        // 6-byte range
        0x10000000000ULL, 0xFFFFFFFFFFFF,    // 7-byte range
        0x1000000000000ULL, 0xFFFFFFFFFFFFFF,// 8-byte range (max 56-bit)
    };

    uint8_t buf[16] = {};

    for (uint64_t val : test_values) {
        std::memset(buf, 0xCC, sizeof(buf)); // Canary fill.
        size_t written = varint_encode(val, buf);

        LOGOS_ASSERT(written >= 1 && written <= 8, "HERMES-VARINT-001",
            "VarInt encode for {} produced {} bytes (expected 1-8)", val, written);

        VarIntResult result = varint_decode(buf);
        LOGOS_ASSERT(result.value == val, "HERMES-VARINT-001",
            "VarInt round-trip failed: wrote {}, decoded {}", val, result.value);
        LOGOS_ASSERT(result.bytes_read == written, "HERMES-VARINT-001",
            "VarInt bytes_read mismatch: encode wrote {}, decode read {}", written, result.bytes_read);
    }

    LOGOS_TRACE("hermes.varint", "status", "pass");
    std::printf("  VarInt: OK\n");
}

// ============================================================================
// Arena tests
// ============================================================================

static void test_arena_basic() {
    std::printf("--- Arena basic allocation ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    TypeTag int_tag(23, TagDescriptor::Data); // Integer, type_hash=23

    // Allocate a few int32_t objects.
    void* p1 = arena.allocate(sizeof(int32_t), alignof(int32_t), int_tag).get();
    void* p2 = arena.allocate(sizeof(int32_t), alignof(int32_t), int_tag).get();

    LOGOS_ASSERT(p1 != nullptr, "HERMES-ARENA-001", "First allocation must succeed");
    LOGOS_ASSERT(p2 != nullptr, "HERMES-ARENA-001", "Second allocation must succeed");
    LOGOS_ASSERT(p1 != p2, "HERMES-ARENA-001", "Two allocations must return different addresses");

    // Check alignment.
    LOGOS_ASSERT(reinterpret_cast<uintptr_t>(p1) % alignof(int32_t) == 0, "HERMES-ARENA-001",
        "Allocated address must be aligned to {}", alignof(int32_t));
    LOGOS_ASSERT(reinterpret_cast<uintptr_t>(p2) % alignof(int32_t) == 0, "HERMES-ARENA-001",
        "Allocated address must be aligned to {}", alignof(int32_t));

    // Write values and read back.
    *static_cast<int32_t*>(p1) = 42;
    *static_cast<int32_t*>(p2) = 99;
    LOGOS_ASSERT(*static_cast<int32_t*>(p1) == 42, "HERMES-ARENA-001", "");
    LOGOS_ASSERT(*static_cast<int32_t*>(p2) == 99, "HERMES-ARENA-001", "");

    // Read back type tags.
    TypeTag t1 = TypeTag::read_before(static_cast<const uint8_t*>(p1));
    TypeTag t2 = TypeTag::read_before(static_cast<const uint8_t*>(p2));
    LOGOS_ASSERT(t1 == int_tag, "HERMES-ARENA-002",
        "TypeTag before first object must match: expected 0x{:X}, got 0x{:X}",
        int_tag.raw(), t1.raw());
    LOGOS_ASSERT(t2 == int_tag, "HERMES-ARENA-002",
        "TypeTag before second object must match: expected 0x{:X}, got 0x{:X}",
        int_tag.raw(), t2.raw());

    LOGOS_TRACE("hermes.arena.basic", "status", "pass", "total_used", arena.total_used());
    std::printf("  Arena basic: OK (used %zu bytes)\n", arena.total_used());
}

static void test_arena_mixed_types() {
    std::printf("--- Arena mixed type allocation ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    TypeTag tiny_tag(20, TagDescriptor::Data);   // TinyInt
    TypeTag big_tag(26, TagDescriptor::Data);     // BigInt (8 bytes, align 8)
    TypeTag map_tag(98, TagDescriptor::Map);      // TinyObjectMap

    void* p_tiny = arena.allocate(1, 2, tiny_tag).get();
    void* p_big  = arena.allocate(8, 8, big_tag).get();
    void* p_map  = arena.allocate(16, 8, map_tag).get();

    // All must be non-null and properly aligned.
    LOGOS_ASSERT(p_tiny != nullptr, "HERMES-ARENA-001", "");
    LOGOS_ASSERT(reinterpret_cast<uintptr_t>(p_big) % 8 == 0, "HERMES-ARENA-001",
        "BigInt must be 8-byte aligned");
    LOGOS_ASSERT(reinterpret_cast<uintptr_t>(p_map) % 8 == 0, "HERMES-ARENA-001",
        "TinyObjectMap must be 8-byte aligned");

    // Tags must read back correctly.
    TypeTag rt = TypeTag::read_before(static_cast<const uint8_t*>(p_tiny));
    TypeTag rb = TypeTag::read_before(static_cast<const uint8_t*>(p_big));
    TypeTag rm = TypeTag::read_before(static_cast<const uint8_t*>(p_map));

    LOGOS_ASSERT(rt == tiny_tag, "HERMES-ARENA-002", "TinyInt tag mismatch");
    LOGOS_ASSERT(rb == big_tag, "HERMES-ARENA-002", "BigInt tag mismatch");
    LOGOS_ASSERT(rm == map_tag, "HERMES-ARENA-002", "TinyObjectMap tag mismatch");

    LOGOS_TRACE("hermes.arena.mixed", "status", "pass");
    std::printf("  Arena mixed types: OK\n");
}

static void test_arena_grow_single_chunk() {
    std::printf("--- Arena single chunk grow ---\n");

    auto arena = Arena::make(ArenaMode::GrowableSingleChunk, 64).get(); // Tiny initial size.

    TypeTag tag(23, TagDescriptor::Data);

    // Allocate until we exceed the initial capacity.
    void* first = arena.allocate(sizeof(int32_t), 4, tag).get();
    uintptr_t first_offset = static_cast<uint8_t*>(first) - arena.head().data();

    for (int i = 0; i < 100; ++i) {
        arena.allocate(sizeof(int32_t), 4, tag).get();
    }

    LOGOS_ASSERT(arena.chunk_count() == 1, "HERMES-ARENA-003",
        "GrowableSingleChunk must stay as 1 chunk after growth, got {}", arena.chunk_count());
    LOGOS_ASSERT(arena.head().capacity > 64, "HERMES-ARENA-003",
        "Arena must have grown beyond initial 64 bytes");

    // First allocation content must be preserved at the same offset.
    void* first_after = arena.head().data() + first_offset;
    TypeTag first_tag = TypeTag::read_before(static_cast<const uint8_t*>(first_after));
    LOGOS_ASSERT(first_tag == tag, "HERMES-ARENA-003",
        "Tag of first allocation must survive chunk growth");

    LOGOS_TRACE("hermes.arena.grow", "status", "pass",
        "final_capacity", arena.head().capacity);
    std::printf("  Arena grow (single chunk): OK (capacity grew to %zu)\n",
        arena.head().capacity);
}

static void test_arena_grow_multi_chunk() {
    std::printf("--- Arena multi-chunk grow ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 64).get();

    TypeTag tag(23, TagDescriptor::Data);

    for (int i = 0; i < 100; ++i) {
        arena.allocate(sizeof(int32_t), 4, tag).get();
    }

    LOGOS_ASSERT(arena.chunk_count() > 1, "HERMES-ARENA-003",
        "MultiChunk arena must create additional chunks, got {}", arena.chunk_count());

    LOGOS_TRACE("hermes.arena.multi", "status", "pass",
        "chunk_count", arena.chunk_count());
    std::printf("  Arena multi-chunk: OK (%zu chunks)\n", arena.chunk_count());
}

static void test_arena_raw_allocation() {
    std::printf("--- Arena raw allocation ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    // Raw allocation (no tag) — used for DocumentHeader.
    void* raw = arena.allocate_raw(8, 8).get();
    LOGOS_ASSERT(raw != nullptr, "HERMES-ARENA-001", "Raw allocation must succeed");
    LOGOS_ASSERT(reinterpret_cast<uintptr_t>(raw) % 8 == 0, "HERMES-ARENA-001",
        "Raw allocation must be aligned");

    // After raw allocation, tagged allocation must still work.
    TypeTag tag(20, TagDescriptor::Data);
    void* tagged = arena.allocate(1, 2, tag).get();
    LOGOS_ASSERT(tagged != nullptr, "HERMES-ARENA-001", "");

    TypeTag rt = TypeTag::read_before(static_cast<const uint8_t*>(tagged));
    LOGOS_ASSERT(rt == tag, "HERMES-ARENA-002",
        "Tag must be correct after raw allocation");

    LOGOS_TRACE("hermes.arena.raw", "status", "pass");
    std::printf("  Arena raw allocation: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes Layer 0: Arena Primitives Exerciser ===\n\n");

    test_relative_ptr();
    test_tagged_ptr_pointer_mode();
    test_tagged_ptr_value_mode();
    test_tagged_ptr_set_pointer();
    test_type_tag();
    test_varint();
    test_arena_basic();
    test_arena_mixed_types();
    test_arena_grow_single_chunk();
    test_arena_grow_multi_chunk();
    test_arena_raw_allocation();

    std::printf("\n=== All Layer 0 tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
