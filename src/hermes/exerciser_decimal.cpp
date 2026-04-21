// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Exerciser for Hermes Decimal: stringify + clone round-trip.

#include <logos/hermes/access.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <cstring>

using namespace logos::hermes;

// Allocate a DecimalData in the document's arena and set it as root.
// nlimbs_array must point to nlimbs uint32_t values (little-endian limbs, limb[0] = least significant).
static DecimalData* make_decimal(Hermes& doc, bool negative, uint32_t precision,
                                  uint32_t scale, uint32_t nlimbs, const uint32_t* limbs_array) {
    Arena& arena = HermesAccess::arena(doc);
    // spec_and_len: bits[0..11]=scale, [12..23]=precision, [24..30]=nlimbs, [31]=sign
    uint32_t spec = (scale & 0xFFF)
                  | ((precision & 0xFFF) << 12)
                  | ((nlimbs & 0x7F) << 24)
                  | (negative ? (1u << 31) : 0u);

    size_t sz = sizeof(uint32_t) * (1 + nlimbs);
    TypeTag tag(type_hash::Decimal, TagDescriptor::Data);
    auto* mem = static_cast<uint32_t*>(arena.allocate(sz, alignof(uint32_t), tag).get());
    mem[0] = spec;
    for (uint32_t i = 0; i < nlimbs; ++i) {
        mem[1 + i] = limbs_array[i];
    }
    auto* d = reinterpret_cast<DecimalData*>(mem);

    // Set as root.
    uint32_t off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(mem) - HermesAccess::base(doc));
    HermesAccess::set_root_offset(doc, arena_offset_t(off));
    return d;
}

static void check_stringify(const char* label, Hermes& doc, const char* expected) {
    auto s = stringify(doc, false).get();
    LOGOS_ASSERT(s == expected, "DEC-STRINGIFY-001",
        "{}: expected '{}', got '{}'", label, expected, s);
    std::printf("  %-50s OK  (got '%s')\n", label, s.c_str());
}

static void test_zero() {
    std::printf("--- decimal: zero (scale=2, precision=3) ---\n");
    // Zero: nlimbs=0 with scale=2 → "0.00"
    auto doc = make_doc().get();
    make_decimal(doc, false, 3, 2, 0, nullptr);
    check_stringify("zero scale=2", doc, "0.00");
}

static void test_positive_integer() {
    std::printf("--- decimal: positive integer 42 (scale=0, precision=2) ---\n");
    auto doc = make_doc().get();
    uint32_t limbs[] = {42};
    make_decimal(doc, false, 2, 0, 1, limbs);
    check_stringify("42 scale=0", doc, "42");
}

static void test_positive_decimal() {
    std::printf("--- decimal: 123.456 (scale=3, precision=6) ---\n");
    // value_abs = 123456, 1 limb, scale=3
    auto doc = make_doc().get();
    uint32_t limbs[] = {123456};
    make_decimal(doc, false, 6, 3, 1, limbs);
    check_stringify("123.456", doc, "123.456");
}

static void test_negative() {
    std::printf("--- decimal: -7.5 (scale=1, precision=2) ---\n");
    // value_abs = 75, scale=1
    auto doc = make_doc().get();
    uint32_t limbs[] = {75};
    make_decimal(doc, true, 2, 1, 1, limbs);
    check_stringify("-7.5", doc, "-7.5");
}

static void test_large_multilimb() {
    std::printf("--- decimal: 1000000001.23 (scale=2, two limbs) ---\n");
    // value = 100000000123
    // limb[0] = 100000000123 % 1e9 = 123 (wait: 100000000123 % 1000000000 = 123)
    // limb[1] = 100000000123 / 1e9 = 100
    // i.e. 100 * 1e9 + 123 = 100000000123, /100 = 1000000001.23
    auto doc = make_doc().get();
    uint32_t limbs[] = {123, 100};  // limb[0]=LSB, limb[1]=MSB
    make_decimal(doc, false, 12, 2, 2, limbs);
    check_stringify("1000000001.23", doc, "1000000001.23");
}

static void test_leading_zeros_after_point() {
    std::printf("--- decimal: 0.007 (scale=3, precision=3) ---\n");
    // value_abs = 7, scale=3, dlen=1, s_i=3 → dlen <= s_i → "0." + "00" + "7"
    auto doc = make_doc().get();
    uint32_t limbs[] = {7};
    make_decimal(doc, false, 3, 3, 1, limbs);
    check_stringify("0.007", doc, "0.007");
}

static void test_clone_round_trip() {
    std::printf("--- decimal: clone round-trip for -123.456 ---\n");
    auto doc = make_doc().get();
    uint32_t limbs[] = {123456};
    make_decimal(doc, true, 6, 3, 1, limbs);

    auto src_str = stringify(doc, false).get();
    auto cloned = clone(doc).get();
    auto dst_str = stringify(cloned, false).get();

    LOGOS_ASSERT(src_str == dst_str, "DEC-CLONE-001",
        "Clone stringify mismatch: src='{}' dst='{}'", src_str, dst_str);
    std::printf("  clone round-trip: src='%s'  dst='%s'  OK\n",
                src_str.c_str(), dst_str.c_str());

    // Byte-identical check: compare the raw DecimalData bytes.
    auto* src_root = HermesAccess::root<DecimalData>(doc);
    auto* dst_root = HermesAccess::root<DecimalData>(cloned);
    size_t sz = src_root->byte_size();
    LOGOS_ASSERT(sz == dst_root->byte_size(), "DEC-CLONE-002",
        "byte_size mismatch: {} vs {}", sz, dst_root->byte_size());
    bool identical = (std::memcmp(src_root, dst_root, sz) == 0);
    LOGOS_ASSERT(identical, "DEC-CLONE-003", "DecimalData bytes not identical after clone");
    std::printf("  byte-identical: OK (%zu bytes)\n", sz);
}

static void test_clone_multilimb_round_trip() {
    std::printf("--- decimal: clone round-trip for 1000000001.23 (2 limbs) ---\n");
    auto doc = make_doc().get();
    uint32_t limbs[] = {123, 100};
    make_decimal(doc, false, 12, 2, 2, limbs);

    auto src_str = stringify(doc, false).get();
    auto cloned = clone(doc).get();
    auto dst_str = stringify(cloned, false).get();

    LOGOS_ASSERT(src_str == dst_str, "DEC-CLONE-004",
        "Multilimb clone stringify mismatch: src='{}' dst='{}'", src_str, dst_str);
    std::printf("  multilimb clone: src='%s'  dst='%s'  OK\n",
                src_str.c_str(), dst_str.c_str());
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});
    logos::hermes::hermes_init();

    test_zero();
    test_positive_integer();
    test_positive_decimal();
    test_negative();
    test_large_multilimb();
    test_leading_zeros_after_point();
    test_clone_round_trip();
    test_clone_multilimb_round_trip();

    std::printf("\nAll Decimal exerciser tests passed.\n");
    logos::shutdown_sqlite_sink();
    return 0;
}
