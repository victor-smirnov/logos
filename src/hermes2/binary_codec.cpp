// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes2/binary_codec.hpp>
#include <logos/hermes2/type_tag.hpp>
#include <logos/hermes2/type_codes.hpp>
#include <logos/hermes2/varint.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/tiny_object_map.hpp>
#include <logos/hermes2/typed_array.hpp>
#include <logos/hermes2/map.hpp>
#include <logos/hermes2/compound_types.hpp>

#include <cstring>

namespace logos::hermes2 {
namespace {

// ── write helpers (append to a byte vector) ────────────────────────────────────
void w_byte(std::vector<uint8_t>& o, uint8_t b) { o.push_back(b); }
void w_varint(std::vector<uint8_t>& o, uint64_t v) {
    uint8_t buf[8]; size_t n = varint_encode(v, buf); o.insert(o.end(), buf, buf + n);
}
void w_raw(std::vector<uint8_t>& o, const void* p, size_t n) {
    auto* b = static_cast<const uint8_t*>(p); o.insert(o.end(), b, b + n);
}

void enc_av(AnyVal av, std::vector<uint8_t>& o);

template <typename T>
void enc_typed_array(const uint8_t* obj, std::vector<uint8_t>& o) {
    auto* a = reinterpret_cast<const TypedArray<T>*>(obj);
    w_varint(o, a->size());
    for (uint64_t i = 0; i < a->size(); ++i) { T v = a->get(i); w_raw(o, &v, sizeof(T)); }
}
template <typename K>
void enc_typed_map(const uint8_t* obj, std::vector<uint8_t>& o) {
    auto* m = reinterpret_cast<const TypedMap<K>*>(obj);
    w_varint(o, m->size());
    m->for_each([&](K key, AnyVal val) { w_raw(o, &key, sizeof(K)); enc_av(val, o); });
}

void enc_obj(const uint8_t* obj, std::vector<uint8_t>& o) {
    uint64_t tag = TypeTag::read_before(obj).type_code();
    w_varint(o, tag);
    switch (tag) {
    case tc::STRING: {
        auto* s = reinterpret_cast<const ArenaString*>(obj);
        auto sv = s->view(); w_varint(o, sv.size()); w_raw(o, sv.data(), sv.size()); break;
    }
    case tc::ARRAY: {
        auto* a = reinterpret_cast<const ObjectArray*>(obj);
        w_varint(o, a->size());
        for (uint64_t i = 0; i < a->size(); ++i) enc_av(a->get(i), o); break;
    }
    case tc::MAP: {
        auto* m = reinterpret_cast<const ObjectMap*>(obj);
        w_varint(o, m->size());
        m->for_each([&](std::string_view k, AnyVal v) {
            w_varint(o, k.size()); w_raw(o, k.data(), k.size()); enc_av(v, o);
        }); break;
    }
    case tc::TINYMAP: {
        auto* t = reinterpret_cast<const TinyObjectMap*>(obj);
        w_varint(o, t->capacity()); w_varint(o, t->size());
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k)
            if (t->has_key(k)) { w_byte(o, k); enc_av(t->get(k), o); }
        break;
    }
    case tc::DECIMAL: {
        auto* d = reinterpret_cast<const Decimal*>(obj);
        w_raw(o, &d->spec, sizeof(uint32_t)); w_raw(o, &d->coeff, sizeof(uint64_t)); break;
    }
    case tc::I64: case tc::U64: case tc::F32: case tc::F64:
        w_raw(o, obj, 8); break;
    case tc::TYPEDVALUE: {
        auto* t = reinterpret_cast<const TypedValue*>(obj);
        enc_av(t->type_name, o); enc_av(t->params, o); enc_av(t->init, o); break;
    }
    case tc::PARAMETER: {
        auto* p = reinterpret_cast<const Parameter*>(obj);
        enc_av(p->name, o); enc_av(p->value, o); break;
    }
    case tc::ARRAY_U8:  enc_typed_array<uint8_t>(obj, o);  break;
    case tc::ARRAY_U16: enc_typed_array<uint16_t>(obj, o); break;
    case tc::ARRAY_U32: enc_typed_array<uint32_t>(obj, o); break;
    case tc::ARRAY_U64: enc_typed_array<uint64_t>(obj, o); break;
    case tc::ARRAY_I8:  enc_typed_array<int8_t>(obj, o);   break;
    case tc::ARRAY_I16: enc_typed_array<int16_t>(obj, o);  break;
    case tc::ARRAY_I32: enc_typed_array<int32_t>(obj, o);  break;
    case tc::ARRAY_I64: enc_typed_array<int64_t>(obj, o);  break;
    case tc::ARRAY_F32: enc_typed_array<float>(obj, o);    break;
    case tc::ARRAY_F64: enc_typed_array<double>(obj, o);   break;
    case tc::MAP_I32: enc_typed_map<int32_t>(obj, o);  break;
    case tc::MAP_U32: enc_typed_map<uint32_t>(obj, o); break;
    case tc::MAP_I64: enc_typed_map<int64_t>(obj, o);  break;
    case tc::MAP_U64: enc_typed_map<uint64_t>(obj, o); break;
    default: break;
    }
}

void enc_av(AnyVal av, std::vector<uint8_t>& o) {
    if (av.is_null()) { w_byte(o, 0); return; }
    if (av.is_pod())  { w_byte(o, 1); int64_t w = av.raw(); w_raw(o, &w, 8); return; }
    w_byte(o, 2); enc_obj(av.resolve(), o);
}

// ── read cursor (bounds-checked; sets ok=false on overrun) ──────────────────────
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    uint8_t u8() { if (p >= end) { ok = false; return 0; } return *p++; }
    uint64_t varint() {
        if (p >= end) { ok = false; return 0; }
        VarIntResult r = varint_decode(p);
        if (p + r.bytes_read > end) { ok = false; return 0; }
        p += r.bytes_read; return r.value;
    }
    const uint8_t* take(size_t n) {
        if (p + n > end) { ok = false; return nullptr; }
        const uint8_t* r = p; p += n; return r;
    }
};

AnyVal dec_av(Reader& r, Arena& a);

template <typename T>
void* dec_typed_array(Reader& r, Arena& a) {
    uint64_t n = r.varint();
    auto res = TypedArray<T>::create(a, n ? n : 1); if (!res) { r.ok = false; return nullptr; }
    TypedArray<T>* d = *res;
    for (uint64_t i = 0; i < n; ++i) {
        const uint8_t* b = r.take(sizeof(T)); if (!b) return d;
        T v; std::memcpy(&v, b, sizeof(T)); (void)d->push_back(v, a);
    }
    return d;
}
template <typename K>
void* dec_typed_map(Reader& r, Arena& a) {
    uint64_t n = r.varint();
    auto res = TypedMap<K>::create(a, n ? n : 1); if (!res) { r.ok = false; return nullptr; }
    TypedMap<K>* d = *res;
    for (uint64_t i = 0; i < n; ++i) {
        const uint8_t* b = r.take(sizeof(K)); if (!b) return d;
        K key; std::memcpy(&key, b, sizeof(K)); d->put(key, dec_av(r, a));
    }
    return d;
}

void* dec_obj(Reader& r, Arena& a) {
    uint64_t tag = r.varint();
    switch (tag) {
    case tc::STRING: {
        uint64_t len = r.varint(); const uint8_t* b = r.take(len); if (!b) return nullptr;
        auto res = ArenaString::create(a, {reinterpret_cast<const char*>(b), len});
        if (!res) { r.ok = false; return nullptr; } return *res;
    }
    case tc::ARRAY: {
        uint64_t n = r.varint();
        auto res = ObjectArray::create(a, n ? n : 1); if (!res) { r.ok = false; return nullptr; }
        ObjectArray* d = *res;
        for (uint64_t i = 0; i < n; ++i) (void)d->push_back(dec_av(r, a), a);
        return d;
    }
    case tc::MAP: {
        uint64_t n = r.varint();
        auto res = ObjectMap::create(a, n ? n * 2 : 8); if (!res) { r.ok = false; return nullptr; }
        ObjectMap* d = *res;
        for (uint64_t i = 0; i < n; ++i) {
            uint64_t kl = r.varint(); const uint8_t* kb = r.take(kl); if (!kb) return d;
            std::string_view key{reinterpret_cast<const char*>(kb), kl};
            (void)d->put(key, dec_av(r, a), a);
        }
        return d;
    }
    case tc::TINYMAP: {
        uint64_t cap = r.varint(); uint64_t cnt = r.varint();
        auto res = TinyObjectMap::create(a, cap); if (!res) { r.ok = false; return nullptr; }
        TinyObjectMap* d = *res;
        for (uint64_t i = 0; i < cnt; ++i) { uint8_t k = r.u8(); (void)d->put(k, dec_av(r, a), a); }
        return d;
    }
    case tc::DECIMAL: {
        const uint8_t* sp = r.take(4); const uint8_t* cf = r.take(8); if (!sp || !cf) return nullptr;
        uint32_t spec; uint64_t coeff; std::memcpy(&spec, sp, 4); std::memcpy(&coeff, cf, 8);
        auto res = Decimal::create(a, coeff, spec & Decimal::SCALE_MASK, (spec & Decimal::SIGN_BIT) != 0);
        if (!res) { r.ok = false; return nullptr; } return *res;
    }
    case tc::I64: case tc::U64: case tc::F32: case tc::F64: {
        const uint8_t* b = r.take(8); if (!b) return nullptr;
        auto res = a.allocate(8, 8, TypeTag{tag}); if (!res) { r.ok = false; return nullptr; }
        std::memcpy(*res, b, 8); return *res;
    }
    case tc::TYPEDVALUE: {
        auto res = TypedValue::create(a, AnyVal::null(), AnyVal::null(), AnyVal::null());
        if (!res) { r.ok = false; return nullptr; } TypedValue* d = *res;
        d->type_name = dec_av(r, a); d->params = dec_av(r, a); d->init = dec_av(r, a); return d;
    }
    case tc::PARAMETER: {
        auto res = Parameter::create(a, AnyVal::null(), AnyVal::null());
        if (!res) { r.ok = false; return nullptr; } Parameter* d = *res;
        d->name = dec_av(r, a); d->value = dec_av(r, a); return d;
    }
    case tc::ARRAY_U8:  return dec_typed_array<uint8_t>(r, a);
    case tc::ARRAY_U16: return dec_typed_array<uint16_t>(r, a);
    case tc::ARRAY_U32: return dec_typed_array<uint32_t>(r, a);
    case tc::ARRAY_U64: return dec_typed_array<uint64_t>(r, a);
    case tc::ARRAY_I8:  return dec_typed_array<int8_t>(r, a);
    case tc::ARRAY_I16: return dec_typed_array<int16_t>(r, a);
    case tc::ARRAY_I32: return dec_typed_array<int32_t>(r, a);
    case tc::ARRAY_I64: return dec_typed_array<int64_t>(r, a);
    case tc::ARRAY_F32: return dec_typed_array<float>(r, a);
    case tc::ARRAY_F64: return dec_typed_array<double>(r, a);
    case tc::MAP_I32: return dec_typed_map<int32_t>(r, a);
    case tc::MAP_U32: return dec_typed_map<uint32_t>(r, a);
    case tc::MAP_I64: return dec_typed_map<int64_t>(r, a);
    case tc::MAP_U64: return dec_typed_map<uint64_t>(r, a);
    default: r.ok = false; return nullptr;
    }
}

AnyVal dec_av(Reader& r, Arena& a) {
    uint8_t kind = r.u8();
    if (kind == 0) return AnyVal::null();
    if (kind == 1) {
        const uint8_t* b = r.take(8); if (!b) return AnyVal::null();
        int64_t w; std::memcpy(&w, b, 8);
        return AnyVal::pod(w >> 8, static_cast<uint8_t>((w >> 1) & 0x7F));   // reconstruct Pod
    }
    AnyVal av; av.set_ref(dec_obj(r, a)); return av;
}

} // namespace

logos::expected<std::vector<uint8_t>> binary_encode(const HermesCtr& doc) noexcept {
    std::vector<uint8_t> out;
    enc_av(doc.root(), out);
    return out;
}

logos::expected<HermesCtr> binary_decode(const uint8_t* data, size_t size) noexcept {
    LOGOS_TRY(auto ctr, HermesCtr::make());
    Reader r{data, data + size};
    AnyVal root = dec_av(r, ctr.arena());
    if (!r.ok) return std::unexpected(logos::err(ErrCode::parse_error));
    ctr.set_root(root);
    return ctr;
}

} // namespace logos::hermes2
