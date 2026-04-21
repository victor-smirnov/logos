// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/fnv_hash.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <cmath>
#include <cstring>

using namespace logos::hermes;

// ============================================================================
// Type registry: compile-time checks
// ============================================================================

static void test_type_registry() {
    std::printf("--- Type registry ---\n");

    // Verify type hashes match hermes-abi.json
    static_assert(TypeTraits<int8_t>::hash == 20);
    static_assert(TypeTraits<uint8_t>::hash == 21);
    static_assert(TypeTraits<int16_t>::hash == 22);
    static_assert(TypeTraits<int32_t>::hash == 23);
    static_assert(TypeTraits<uint16_t>::hash == 24);
    static_assert(TypeTraits<uint32_t>::hash == 25);
    static_assert(TypeTraits<int64_t>::hash == 26);
    static_assert(TypeTraits<uint64_t>::hash == 27);
    static_assert(TypeTraits<float>::hash == 30);
    static_assert(TypeTraits<double>::hash == 31);

    // Embeddability under the 4-byte AnyVal layout: integer types up to 24
    // bits' worth (i32/u32 fit when their runtime value fits), bool, i16/u16,
    // i8/u8. f32/f64/i64/u64 live in the arena as pointer-mode slots.
    static_assert(is_embeddable<int8_t>());
    static_assert(is_embeddable<uint8_t>());
    static_assert(is_embeddable<int16_t>());
    static_assert(is_embeddable<int32_t>());
    static_assert(is_embeddable<uint32_t>());
    static_assert(!is_embeddable<float>());
    static_assert(!is_embeddable<int64_t>());
    static_assert(!is_embeddable<uint64_t>());
    static_assert(!is_embeddable<double>());

    // TypeTag generation
    constexpr TypeTag int_tag = type_tag_for<int32_t>();
    static_assert(int_tag.type_code() == 23);
    static_assert(int_tag.descriptor() == TagDescriptor::Data);

    LOGOS_TRACE("hermes.types.registry", "status", "pass");
    std::printf("  Type registry: OK (all compile-time checks passed)\n");
}

// ============================================================================
// Arena allocation of fixed-size types
// ============================================================================

static void test_arena_put_get() {
    std::printf("--- arena_put / arena_get ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    // Integer types
    {
        int8_t* p = arena_put<int8_t>(arena, -42).get();
        LOGOS_ASSERT(arena_get(p) == -42, "HERMES-TYPES-001",
            "arena_put/get int8_t round-trip failed");

        uint8_t* q = arena_put<uint8_t>(arena, 200).get();
        LOGOS_ASSERT(arena_get(q) == 200, "HERMES-TYPES-001",
            "arena_put/get uint8_t round-trip failed");

        int16_t* r = arena_put<int16_t>(arena, -12345).get();
        LOGOS_ASSERT(arena_get(r) == -12345, "HERMES-TYPES-001",
            "arena_put/get int16_t round-trip failed");

        int32_t* s = arena_put<int32_t>(arena, -1234567).get();
        LOGOS_ASSERT(arena_get(s) == -1234567, "HERMES-TYPES-001",
            "arena_put/get int32_t round-trip failed");

        int64_t* t = arena_put<int64_t>(arena, -9876543210LL).get();
        LOGOS_ASSERT(arena_get(t) == -9876543210LL, "HERMES-TYPES-001",
            "arena_put/get int64_t round-trip failed");
    }

    // Floating point
    {
        float* f = arena_put<float>(arena, 3.14f).get();
        LOGOS_ASSERT(std::abs(arena_get(f) - 3.14f) < 1e-6f, "HERMES-TYPES-001",
            "arena_put/get float round-trip failed");

        double* d = arena_put<double>(arena, 2.718281828).get();
        LOGOS_ASSERT(std::abs(arena_get(d) - 2.718281828) < 1e-9, "HERMES-TYPES-001",
            "arena_put/get double round-trip failed");
    }

    // Verify TypeTags are written correctly
    {
        int32_t* p = arena_put<int32_t>(arena, 0).get();
        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(p));
        LOGOS_ASSERT(tag.type_code() == type_hash::Integer, "HERMES-TYPES-002",
            "TypeTag for int32_t must have type_code={}, got {}",
            type_hash::Integer, tag.type_code());

        double* d = arena_put<double>(arena, 0.0).get();
        TypeTag dtag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(d));
        LOGOS_ASSERT(dtag.type_code() == type_hash::Double, "HERMES-TYPES-002",
            "TypeTag for double must have type_code={}, got {}",
            type_hash::Double, dtag.type_code());
    }

    LOGOS_TRACE("hermes.types.arena_put", "status", "pass");
    std::printf("  arena_put/get: OK\n");
}

// ============================================================================
// AnyVal embedding with TypeTraits
// ============================================================================

static void test_tagged_ptr_with_traits() {
    std::printf("--- AnyVal + TypeTraits ---\n");

    // Embed each embeddable type using its TypeTraits hash
    {
        int8_t val = -42;
        AnyVal p = AnyVal::from_value(val, TypeTraits<int8_t>::hash);
        LOGOS_ASSERT(p.is_value(), "HERMES-TYPES-003", "");
        LOGOS_ASSERT(p.value_type_hash() == type_hash::TinyInt, "HERMES-TYPES-003",
            "Embedded int8_t must have type_hash={}", type_hash::TinyInt);
        LOGOS_ASSERT(p.as_value<int8_t>() == -42, "HERMES-TYPES-003",
            "Embedded int8_t value mismatch");
    }

    {
        int32_t val = 999999;
        AnyVal p = AnyVal::from_value(val, TypeTraits<int32_t>::hash);
        LOGOS_ASSERT(p.value_type_hash() == type_hash::Integer, "HERMES-TYPES-003", "");
        LOGOS_ASSERT(p.as_value<int32_t>() == 999999, "HERMES-TYPES-003", "");
    }

    // f32/f64/i64/u64 are not embeddable; they live in the arena and AnyVal
    // holds a segment-relative offset to them (anyval_put<T>).
    {
        auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();
        AnyVal p = anyval_put<float>(arena, -1.5f).get();
        LOGOS_ASSERT(p.is_pointer(), "HERMES-TYPES-003",
            "anyval_put<float> must produce a pointer-mode AnyVal");
        const uint8_t* base = arena.head().data();
        LOGOS_ASSERT(*p.as_ptr<float>(base) == -1.5f, "HERMES-TYPES-003", "");
    }

    static_assert(!TypeTraits<float>::embeddable);
    static_assert(!TypeTraits<int64_t>::embeddable);
    static_assert(!TypeTraits<double>::embeddable);

    LOGOS_TRACE("hermes.types.tagged_embed", "status", "pass");
    std::printf("  AnyVal + TypeTraits: OK\n");
}

// ============================================================================
// ArenaString
// ============================================================================

static void test_arena_string() {
    std::printf("--- ArenaString ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    // Basic string
    {
        ArenaString* s = ArenaString::create(arena, "hello").get();
        LOGOS_ASSERT(s->view() == "hello", "HERMES-STRING-001",
            "ArenaString view must return original content");
        LOGOS_ASSERT(s->length() == 5, "HERMES-STRING-001",
            "ArenaString length must be 5, got {}", s->length());

        // TypeTag check
        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(s));
        LOGOS_ASSERT(tag.type_code() == type_hash::Varchar, "HERMES-STRING-002",
            "ArenaString TypeTag must be Varchar ({}), got {}",
            type_hash::Varchar, tag.type_code());
    }

    // Empty string
    {
        ArenaString* s = ArenaString::create(arena, "").get();
        LOGOS_ASSERT(s->view() == "", "HERMES-STRING-001", "Empty string round-trip failed");
        LOGOS_ASSERT(s->length() == 0, "HERMES-STRING-001", "Empty string length must be 0");
    }

    // UTF-8 string (multi-byte characters)
    {
        std::string_view utf8 = "Привет мир";  // Russian "Hello world"
        ArenaString* s = ArenaString::create(arena, utf8).get();
        LOGOS_ASSERT(s->view() == utf8, "HERMES-STRING-001",
            "UTF-8 string round-trip failed");
    }

    // Long string (triggers multi-byte vlen encoding)
    {
        std::string long_str(300, 'x');
        ArenaString* s = ArenaString::create(arena, long_str).get();
        LOGOS_ASSERT(s->view() == long_str, "HERMES-STRING-001",
            "Long string (300 chars) round-trip failed");
        LOGOS_ASSERT(s->length() == 300, "HERMES-STRING-001",
            "Long string length must be 300, got {}", s->length());
    }

    // Equality operator
    {
        ArenaString* s = ArenaString::create(arena, "test").get();
        LOGOS_ASSERT(*s == "test", "HERMES-STRING-001", "Equality operator failed");
        LOGOS_ASSERT(*s != "other", "HERMES-STRING-001", "Inequality operator failed");
    }

    LOGOS_TRACE("hermes.types.arena_string", "status", "pass");
    std::printf("  ArenaString: OK\n");
}

// ============================================================================
// FNV-1a hash
// ============================================================================

static void test_fnv_hash() {
    std::printf("--- FNV-1a hash ---\n");

    // Known FNV-1a 64-bit test vectors
    LOGOS_ASSERT(fnv1a_hash("") == 0xCBF29CE484222325ULL, "HERMES-HASH-001",
        "FNV-1a hash of empty string must be offset basis");

    // Different strings must (very likely) produce different hashes
    uint64_t h1 = fnv1a_hash("hello");
    uint64_t h2 = fnv1a_hash("world");
    uint64_t h3 = fnv1a_hash("hello");
    LOGOS_ASSERT(h1 != h2, "HERMES-HASH-001", "Different strings should hash differently");
    LOGOS_ASSERT(h1 == h3, "HERMES-HASH-001", "Same strings must hash identically");

    // ArenaString hash must match direct hash
    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();
    ArenaString* s = ArenaString::create(arena, "hello").get();
    LOGOS_ASSERT(s->hash() == h1, "HERMES-HASH-002",
        "ArenaString::hash() must match fnv1a_hash() for same content");

    LOGOS_TRACE("hermes.types.fnv_hash", "status", "pass");
    std::printf("  FNV-1a hash: OK\n");
}

// ============================================================================
// UIDs
// ============================================================================

static void test_uid_types() {
    std::printf("--- UID types ---\n");

    auto arena = Arena::make(ArenaMode::MultiChunk, 4096).get();

    // Uid64
    {
        Uid64* p = arena_put<Uid64>(arena, Uid64{0xDEADBEEFCAFE1234ULL}).get();
        LOGOS_ASSERT(arena_get(p).value == 0xDEADBEEFCAFE1234ULL, "HERMES-TYPES-001",
            "Uid64 round-trip failed");

        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(p));
        LOGOS_ASSERT(tag.type_code() == type_hash::Uid64, "HERMES-TYPES-002",
            "Uid64 TypeTag mismatch");
    }

    // Uid128
    {
        Uid128 val{};
        for (int i = 0; i < 16; ++i) val.bytes[i] = static_cast<uint8_t>(i + 1);
        Uid128* p = arena_put<Uid128>(arena, val).get();
        Uid128 got = arena_get(p);
        LOGOS_ASSERT(std::memcmp(got.bytes, val.bytes, 16) == 0, "HERMES-TYPES-001",
            "Uid128 round-trip failed");

        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(p));
        LOGOS_ASSERT(tag.type_code() == type_hash::Uid128, "HERMES-TYPES-002",
            "Uid128 TypeTag mismatch");
    }

    // Uid256
    {
        Uid256 val{};
        for (int i = 0; i < 32; ++i) val.bytes[i] = static_cast<uint8_t>(255 - i);
        Uid256* p = arena_put<Uid256>(arena, val).get();
        Uid256 got = arena_get(p);
        LOGOS_ASSERT(std::memcmp(got.bytes, val.bytes, 32) == 0, "HERMES-TYPES-001",
            "Uid256 round-trip failed");

        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(p));
        LOGOS_ASSERT(tag.type_code() == type_hash::Uid256, "HERMES-TYPES-002",
            "Uid256 TypeTag mismatch");
    }

    LOGOS_TRACE("hermes.types.uids", "status", "pass");
    std::printf("  UID types: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes Layer 1: Type System & Core Datatypes Exerciser ===\n\n");

    test_type_registry();
    test_arena_put_get();
    test_tagged_ptr_with_traits();
    test_arena_string();
    test_fnv_hash();
    test_uid_types();

    std::printf("\n=== All Layer 1 tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
