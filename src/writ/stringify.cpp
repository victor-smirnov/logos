// Logos project — https://github.com/victor-smirnov/logos

#include <logos/writ/stringify.hpp>
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
#include <cstdio>
#include <vector>
#include <algorithm>
#include <utility>

namespace logos::writ {
namespace {

void str_av(AnyVal av, std::string& out);

void quote(std::string_view s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\t': out += "\\t";  break;
        case '\r': out += "\\r";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf;
            } else out += c;
        }
    }
    out += '"';
}

std::string fmt_double(double d) {
    char buf[32]; std::snprintf(buf, sizeof(buf), "%g", d); return buf;
}

template <typename T>
void str_typed_array(const uint8_t* obj, std::string& out) {
    auto* a = reinterpret_cast<const TypedArray<T>*>(obj);
    out += '[';
    for (uint64_t i = 0; i < a->size(); ++i) {
        if (i) out += ", ";
        if constexpr (std::is_floating_point_v<T>) out += fmt_double(a->get(i));
        else out += std::to_string(static_cast<int64_t>(a->get(i)));
    }
    out += ']';
}
template <typename K>
void str_typed_map(const uint8_t* obj, std::string& out) {
    auto* m = reinterpret_cast<const TypedMap<K>*>(obj);
    out += '{'; bool first = true;
    m->for_each([&](K key, AnyVal val) {
        if (!first) out += ", "; first = false;
        out += std::to_string(static_cast<int64_t>(key)); out += ": "; str_av(val, out);
    });
    out += '}';
}

void str_obj(const uint8_t* obj, std::string& out) {
    uint64_t tag = TypeTag::read_before(obj).type_code();
    switch (tag) {
    case tc::STRING: quote(reinterpret_cast<const ArenaString*>(obj)->view(), out); break;
    case tc::ARRAY: {
        auto* a = reinterpret_cast<const ObjectArray*>(obj);
        out += '[';
        for (uint64_t i = 0; i < a->size(); ++i) { if (i) out += ", "; str_av(a->get(i), out); }
        out += ']'; break;
    }
    case tc::MAP: {
        // Canonical output: sort keys so stringify is deterministic and idempotent
        // (ObjectMap is hash-ordered, with insertion-order-dependent probing).
        auto* m = reinterpret_cast<const ObjectMap*>(obj);
        std::vector<std::pair<std::string, AnyVal>> entries;
        m->for_each([&](std::string_view k, AnyVal v) { entries.emplace_back(std::string(k), v); });
        std::sort(entries.begin(), entries.end(),
                  [](const auto& x, const auto& y) { return x.first < y.first; });
        out += '{';
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i) out += ", ";
            quote(entries[i].first, out); out += ": "; str_av(entries[i].second, out);
        }
        out += '}'; break;
    }
    case tc::TINYMAP: {
        auto* t = reinterpret_cast<const TinyObjectMap*>(obj);
        out += '{'; bool first = true;
        for (uint8_t k = 0; k < TinyObjectMap::MAX_KEYS; ++k)
            if (t->has_key(k)) {
                if (!first) out += ", "; first = false;
                out += std::to_string(k); out += ": "; str_av(t->get(k), out);
            }
        out += '}'; break;
    }
    case tc::DECIMAL: {
        auto* d = reinterpret_cast<const Decimal*>(obj);
        double v = static_cast<double>(d->coefficient());
        for (uint32_t i = 0; i < d->scale(); ++i) v /= 10.0;
        if (d->is_neg()) v = -v;
        out += fmt_double(v); break;
    }
    case tc::I64: out += std::to_string(*reinterpret_cast<const int64_t*>(obj)); break;
    case tc::U64: out += std::to_string(*reinterpret_cast<const uint64_t*>(obj)); break;
    case tc::F32: out += fmt_double(*reinterpret_cast<const float*>(obj)); break;
    case tc::F64: out += fmt_double(*reinterpret_cast<const double*>(obj)); break;
    case tc::TYPEDVALUE: {
        auto* t = reinterpret_cast<const TypedValue*>(obj);
        out += '@';
        if (t->type_name.is_ref())
            out += reinterpret_cast<const ArenaString*>(t->type_name.resolve())->view();
        if (!t->params.is_null()) { out += '('; str_av(t->params, out); out += ')'; }
        out += " = "; str_av(t->init, out); break;
    }
    case tc::PARAMETER: {
        auto* p = reinterpret_cast<const Parameter*>(obj);
        out += '?';
        if (p->name.is_ref())
            out += reinterpret_cast<const ArenaString*>(p->name.resolve())->view();
        if (!p->value.is_null()) { out += " = "; str_av(p->value, out); }
        break;
    }
    case tc::ARRAY_U8:  str_typed_array<uint8_t>(obj, out);  break;
    case tc::ARRAY_U16: str_typed_array<uint16_t>(obj, out); break;
    case tc::ARRAY_U32: str_typed_array<uint32_t>(obj, out); break;
    case tc::ARRAY_U64: str_typed_array<uint64_t>(obj, out); break;
    case tc::ARRAY_I8:  str_typed_array<int8_t>(obj, out);   break;
    case tc::ARRAY_I16: str_typed_array<int16_t>(obj, out);  break;
    case tc::ARRAY_I32: str_typed_array<int32_t>(obj, out);  break;
    case tc::ARRAY_I64: str_typed_array<int64_t>(obj, out);  break;
    case tc::ARRAY_F32: str_typed_array<float>(obj, out);    break;
    case tc::ARRAY_F64: str_typed_array<double>(obj, out);   break;
    case tc::MAP_I32: str_typed_map<int32_t>(obj, out);  break;
    case tc::MAP_U32: str_typed_map<uint32_t>(obj, out); break;
    case tc::MAP_I64: str_typed_map<int64_t>(obj, out);  break;
    case tc::MAP_U64: str_typed_map<uint64_t>(obj, out); break;
    default: out += "null"; break;
    }
}

void str_av(AnyVal av, std::string& out) {
    if (av.is_null()) { out += "null"; return; }
    if (av.is_pod()) {
        if (av.pod_code() == tc::WA_BOOL) out += av.as_bool() ? "true" : "false";
        else out += std::to_string(av.as_i56());
        return;
    }
    str_obj(av.resolve(), out);
}

} // namespace

std::string stringify_value(AnyVal value) { std::string s; str_av(value, s); return s; }
std::string stringify(const WritCtr& doc) { return stringify_value(doc.root()); }

} // namespace logos::writ
