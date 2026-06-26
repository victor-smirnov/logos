// Logos project — https://github.com/victor-smirnov/logos

#include <logos/writ/clone.hpp>
#include <logos/writ/type_tag.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/writ/arena_string.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/writ/tiny_object_map.hpp>
#include <logos/writ/typed_array.hpp>
#include <logos/writ/map.hpp>
#include <logos/writ/compound_types.hpp>

#include <cstring>

namespace logos::writ {

// Clone a typed primitive array (plain elements — no Ref recursion).
template <typename T>
static void* clone_typed_array(const uint8_t* src_obj, DeepCopyState& dedup) noexcept {
    auto* s = reinterpret_cast<const TypedArray<T>*>(src_obj);
    uint64_t n = s->size();
    auto r = TypedArray<T>::create(dedup.arena(), n ? n : 1);
    if (!r) return nullptr;
    TypedArray<T>* d = *r;
    dedup.map(src_obj, d);
    for (uint64_t i = 0; i < n; ++i) (void)d->push_back(s->get(i), dedup.arena());
    return d;
}

// Clone a dense int-keyed map (plain keys, Ref-recursed values).
template <typename K>
static void* clone_typed_map(const uint8_t* src_obj, DeepCopyState& dedup) noexcept {
    auto* s = reinterpret_cast<const TypedMap<K>*>(src_obj);
    auto r = TypedMap<K>::create(dedup.arena(), s->capacity() ? s->capacity() : 1);
    if (!r) return nullptr;
    TypedMap<K>* d = *r;
    dedup.map(src_obj, d);
    s->for_each([&](K key, AnyVal val) { d->put(key, deep_copy_anyval(val, dedup)); });
    return d;
}

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
        d->set_schema_type_code(s->schema_type_code());   // carry node-class discriminator
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
    // Decimal — fixed POD leaf, no Refs → verbatim copy.
    case tc::DECIMAL: {
        auto a = dst.allocate(sizeof(Decimal), alignof(Decimal), TypeTag{code});
        if (!a) return nullptr;
        std::memcpy(*a, src_obj, sizeof(Decimal));
        dedup.map(src_obj, *a);
        return *a;
    }
    // TypedValue / Parameter — fixed objects of at-rest AnyVal words → recurse each.
    case tc::TYPEDVALUE: {
        auto* s = reinterpret_cast<const TypedValue*>(src_obj);
        AnyVal tn = s->type_name, pa = s->params, in = s->init;   // by-value re-anchor
        auto r = TypedValue::create(dst, AnyVal::null(), AnyVal::null(), AnyVal::null());
        if (!r) return nullptr;
        TypedValue* d = *r;
        dedup.map(src_obj, d);
        d->type_name = deep_copy_anyval(tn, dedup);
        d->params    = deep_copy_anyval(pa, dedup);
        d->init      = deep_copy_anyval(in, dedup);
        return d;
    }
    case tc::PARAMETER: {
        auto* s = reinterpret_cast<const Parameter*>(src_obj);
        AnyVal nm = s->name, vl = s->value;
        auto r = Parameter::create(dst, AnyVal::null(), AnyVal::null());
        if (!r) return nullptr;
        Parameter* d = *r;
        dedup.map(src_obj, d);
        d->name  = deep_copy_anyval(nm, dedup);
        d->value = deep_copy_anyval(vl, dedup);
        return d;
    }
    // Typed primitive arrays (ArrayU8..ArrayF64) — plain elements.
    case tc::ARRAY_U8:  return clone_typed_array<uint8_t>(src_obj, dedup);
    case tc::ARRAY_U16: return clone_typed_array<uint16_t>(src_obj, dedup);
    case tc::ARRAY_U32: return clone_typed_array<uint32_t>(src_obj, dedup);
    case tc::ARRAY_U64: return clone_typed_array<uint64_t>(src_obj, dedup);
    case tc::ARRAY_I8:  return clone_typed_array<int8_t>(src_obj, dedup);
    case tc::ARRAY_I16: return clone_typed_array<int16_t>(src_obj, dedup);
    case tc::ARRAY_I32: return clone_typed_array<int32_t>(src_obj, dedup);
    case tc::ARRAY_I64: return clone_typed_array<int64_t>(src_obj, dedup);
    case tc::ARRAY_F32: return clone_typed_array<float>(src_obj, dedup);
    case tc::ARRAY_F64: return clone_typed_array<double>(src_obj, dedup);
    // Dense int-keyed maps (MapI32..MapU64).
    case tc::MAP_I32: return clone_typed_map<int32_t>(src_obj, dedup);
    case tc::MAP_U32: return clone_typed_map<uint32_t>(src_obj, dedup);
    case tc::MAP_I64: return clone_typed_map<int64_t>(src_obj, dedup);
    case tc::MAP_U64: return clone_typed_map<uint64_t>(src_obj, dedup);
    default:
        return nullptr;   // unknown tag — caller treats a null dst as a copy failure
    }
}

logos::expected<ClonedDoc> clone(AnyVal root) noexcept {
    LOGOS_TRY(auto* holder, MemHolder::make());
    DeepCopyState dedup(holder);
    AnyVal new_root = deep_copy_anyval(root, dedup);
    return ClonedDoc{holder, new_root};
}

logos::expected<HermesCtr> compactify_root(AnyVal root) noexcept {
    // No source container to size from (the root may live in any arena —
    // e.g. a metacall JIT's Rc<Hermes>), so clone once to measure the live
    // set, then compact into a right-sized single chunk.
    LOGOS_TRY(auto first, clone(root));
    size_t live = first.holder->arena().total_used();
    first.holder->unref();
    LOGOS_TRY(auto dst, HermesCtr::make(live * 2 + 4096, ArenaMode::GrowableSingleChunk));
    DeepCopyState dedup(dst.holder());
    AnyVal new_root = deep_copy_anyval(root, dedup);
    dst.set_root(new_root);
    if (dst.arena().chunk_count() != 1) [[unlikely]]
        return std::unexpected(logos::err(ErrCode::out_of_memory));
    return dst;
}

logos::expected<HermesCtr> compactify(const HermesCtr& src) noexcept {
    // Upper bound: the compact result (live objects, tight buffers) is ≤ the source's
    // used bytes; 2× + slack is a safe over-estimate, so the single chunk never reallocs.
    size_t estimate = src.arena().total_used() * 2 + 4096;
    LOGOS_TRY(auto dst, HermesCtr::make(estimate, ArenaMode::GrowableSingleChunk));

    DeepCopyState dedup(dst.holder());
    AnyVal new_root = deep_copy_anyval(src.root(), dedup);
    dst.set_root(new_root);

    // The pre-size must have prevented every realloc (a realloc would have dangled
    // the dst container pointers held across recursion). One chunk == no realloc.
    if (dst.arena().chunk_count() != 1) [[unlikely]]
        return std::unexpected(logos::err(ErrCode::out_of_memory));
    return dst;
}

} // namespace logos::writ
