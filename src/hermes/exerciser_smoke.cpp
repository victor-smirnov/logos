// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 foundation smoke test — the conformance gate for the bottom layer:
// self-relative RelativePtr, the AnyVal niche (byte-identical to Logos HAny), and
// the multi-chunk never-move Arena. Returns 0 on success, non-zero on the first
// failed check (the code identifies which).

#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/type_tag.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace logos::hermes;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

// A struct with a self-relative pointer field — the canonical at-rest reference.
struct Node {
    int64_t           value;
    RelativePtr<Node> next;   // 8 bytes, self-relative
};

int main() {
    // ── RelativePtr: self-relative roundtrip (no base) ─────────────────────────
    {
        static_assert(sizeof(RelativePtr<int>) == 8);
        std::vector<Node> nodes(2);
        nodes[0].value = 10;
        nodes[1].value = 20;
        nodes[0].next = &nodes[1];                       // lower: delta = &nodes[1] - &nodes[0].next
        CHECK(nodes[0].next.is_not_null(), 1);
        CHECK(nodes[0].next.get() == &nodes[1], 2);      // resolve back to the same object
        CHECK(nodes[0].next->value == 20, 3);            // operator-> resolves in place
        Node fresh; fresh.value = 99; fresh.next.reset();
        CHECK(fresh.next.is_null(), 4);
    }

    // ── AnyVal niche: byte-identical encoding to Logos HAny ─────────────────────
    {
        static_assert(sizeof(AnyVal) == 8);
        CHECK(AnyVal::null().is_null(), 10);
        CHECK(AnyVal::null().raw() == 0, 11);

        // Pod(36, code=1) → (36<<8)|(1<<1)|1 = 0x2403
        AnyVal p = AnyVal::pod(36, /*HA_I56=*/1);
        CHECK(p.is_pod(), 12);
        CHECK(p.raw() == ((36 << 8) | (1 << 1) | 1), 13);
        CHECK(p.raw() == 0x2403, 14);
        CHECK(p.pod_code() == 1, 15);
        CHECK(p.as_i56() == 36, 16);

        // negative i56 sign-extends through bits[63:8]
        AnyVal neg = AnyVal::pod(-5, 1);
        CHECK(neg.as_i56() == -5, 17);

        AnyVal b = AnyVal::pod_bool(true, /*HA_BOOL=*/2);
        CHECK(b.is_pod() && b.pod_code() == 2 && b.as_bool(), 18);

        // Ref: self-relative to a ≥2-aligned target, resolves back; low bit 0.
        alignas(8) uint8_t target[8] = {};
        AnyVal r;
        r.set_ref(target);
        CHECK(r.is_ref(), 19);
        CHECK((r.raw() & 1) == 0, 20);
        CHECK(r.resolve() == target, 21);

        // copy RE-ANCHORS the Ref (points at the same target from a new address)
        AnyVal r2 = r;
        CHECK(r2.is_ref() && r2.resolve() == target, 22);
        // Pod copies verbatim
        AnyVal p2 = p;
        CHECK(p2.raw() == p.raw(), 23);
    }

    // ── TypeTag: varint encoding (matches Logos h2_write_tag/h2_type_code) ──────
    {
        alignas(8) uint8_t buf[16] = {};
        uint8_t* obj = buf + 8;
        TypeTag{98}.write_before(obj);                   // ≤222 → single byte at obj[-1]
        CHECK(obj[-1] == 98, 30);
        CHECK(TypeTag::read_before(obj).type_code() == 98, 31);

        TypeTag{4115}.write_before(obj);                 // >222 → header + LE code bytes
        CHECK(TypeTag::read_before(obj).type_code() == 4115, 32);
        CHECK(TypeTag{4115}.byte_length() == 3, 33);     // 0x1013 → 2 code bytes + header
    }

    // ── Arena: tagged alloc + multi-chunk NEVER-MOVE ───────────────────────────
    {
        auto arena_exp = Arena::make(ArenaMode::MultiChunk, 256);
        CHECK(arena_exp.has_value(), 40);
        Arena& arena = *arena_exp;

        // First allocation: tag it, write a sentinel, keep the absolute pointer.
        auto a0 = arena.allocate(sizeof(int64_t), 8, TypeTag{26 /*H2_I64*/});
        CHECK(a0.has_value(), 41);
        auto* p0 = static_cast<int64_t*>(*a0);
        *p0 = 0x1122334455667788LL;
        CHECK(TypeTag::read_before(reinterpret_cast<uint8_t*>(p0)).type_code() == 26, 42);

        // Allocate enough to force several new chunks (never-move: p0 stays valid).
        std::vector<int64_t*> ptrs;
        for (int i = 0; i < 1000; ++i) {
            auto ai = arena.allocate(sizeof(int64_t), 8, TypeTag{26});
            CHECK(ai.has_value(), 43);
            auto* pi = static_cast<int64_t*>(*ai);
            *pi = i;
            ptrs.push_back(pi);
        }
        CHECK(arena.chunk_count() > 1, 44);              // genuinely multi-chunk
        CHECK(*p0 == 0x1122334455667788LL, 45);          // earliest pointer NOT invalidated
        for (int i = 0; i < 1000; ++i) CHECK(*ptrs[i] == i, 46);

        // A self-relative AnyVal Ref in chunk N resolving to a target in chunk M
        // (the whole point of self-relative + multi-segment).
        auto slot_exp = arena.allocate(sizeof(AnyVal), 8, TypeTag{0});
        CHECK(slot_exp.has_value(), 47);
        auto* slot = new (*slot_exp) AnyVal();           // at-rest AnyVal in a late chunk
        slot->set_ref(p0);                               // self-relative delta to chunk 0
        CHECK(slot->is_ref() && slot->resolve() == reinterpret_cast<uint8_t*>(p0), 48);
    }

    std::printf("hermes foundation smoke: OK\n");
    return 0;
}
