// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes2/clone.hpp>
#include <logos/hermes2/type_tag.hpp>
#include <logos/hermes2/type_codes.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/tiny_object_map.hpp>

#include <cstring>

namespace logos::hermes2 {

AnyVal deep_copy_anyval(AnyVal src_av, DeepCopyState& dedup) noexcept {
    if (src_av.is_null()) return AnyVal::null();
    if (src_av.is_pod())  return src_av;                   // Pod: position-independent
    void* dst = deep_copy_object(src_av.resolve(), dedup); // Ref: recurse the pointee
    AnyVal r; r.set_ref(dst); return r;
}

void* deep_copy_object(const uint8_t* src_obj, DeepCopyState& dedup) noexcept {
    if (void* d = dedup.resolve(src_obj)) return d;        // already copied (cycle/shared)

    Arena&   dst  = dedup.arena();
    uint64_t code = TypeTag::read_before(src_obj).type_code();

    switch (code) {
    case tc::STRING: {
        auto* s = reinterpret_cast<const ArenaString*>(src_obj);
        auto r = ArenaString::create(dst, s->view());
        if (!r) return nullptr;
        dedup.map(src_obj, *r);
        return *r;
    }
    case tc::ARRAY: {
        auto* s = reinterpret_cast<const ObjectArray*>(src_obj);
        uint64_t n = s->size();
        auto r = ObjectArray::create(dst, n ? n : 1);
        if (!r) return nullptr;
        ObjectArray* d = *r;
        dedup.map(src_obj, d);                             // map BEFORE recursing (cycles)
        for (uint64_t i = 0; i < n; ++i)
            (void)d->push_back(deep_copy_anyval(s->get(i), dedup), dst);
        return d;
    }
    case tc::MAP: {
        auto* s = reinterpret_cast<const ObjectMap*>(src_obj);
        auto r = ObjectMap::create(dst, s->size() ? s->size() * 2 : 8);
        if (!r) return nullptr;
        ObjectMap* d = *r;
        dedup.map(src_obj, d);
        s->for_each([&](std::string_view key, AnyVal val) {
            (void)d->put(key, deep_copy_anyval(val, dedup), dst);
        });
        return d;
    }
    case tc::TINYMAP: {
        auto* s = reinterpret_cast<const TinyObjectMap*>(src_obj);
        auto r = TinyObjectMap::create(dst, s->capacity());
        if (!r) return nullptr;
        TinyObjectMap* d = *r;
        dedup.map(src_obj, d);
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k)
            if (s->has_key(k))
                (void)d->put(k, deep_copy_anyval(s->get(k), dedup), dst);
        return d;
    }
    // Boxed wide scalars — 8-byte leaf objects, no internal Refs → verbatim copy.
    case tc::I64: case tc::U64: case tc::F32: case tc::F64: {
        auto a = dst.allocate(8, 8, TypeTag{code});
        if (!a) return nullptr;
        std::memcpy(*a, src_obj, 8);
        dedup.map(src_obj, *a);
        return *a;
    }
    default:
        // TODO: TypedArray<T> (2101-2110), TypedMap<K> (3101-3104), Decimal (102),
        // TypedValue (4115), Parameter (127) — added when those C++ types land.
        return nullptr;
    }
}

logos::expected<ClonedDoc> clone(AnyVal root) noexcept {
    LOGOS_TRY(auto* holder, MemHolder::make());
    DeepCopyState dedup(holder);
    AnyVal new_root = deep_copy_anyval(root, dedup);
    return ClonedDoc{holder, new_root};
}

} // namespace logos::hermes2
