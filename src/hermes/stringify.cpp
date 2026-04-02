// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/stringify.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>

#include <cstring>
#include <string>

namespace logos::hermes {

class Stringifier {
public:
    Stringifier(bool pretty) : base_(nullptr), pretty_(pretty), indent_(0) {}

    void stringify_root(const HermesCtr& doc) {
        base_ = const_cast<uint8_t*>(doc.base());
        if (!doc.has_root()) { out_ += "null"; return; }
        auto* root = static_cast<const uint8_t*>(doc.root<void>());
        stringify_tagged(root);
    }

    std::string& output() { return out_; }

private:
    std::string out_;
    uint8_t* base_;
    bool pretty_;
    int indent_;

    void stringify_tagged(const uint8_t* obj) {
        TypeTag tag = TypeTag::read_before(obj);
        uint64_t tc = tag.type_code();

        switch (tc) {
            case type_hash::TinyInt:   append_int(*reinterpret_cast<const int8_t*>(obj), "_s8"); return;
            case type_hash::UTinyInt:  append_uint(*reinterpret_cast<const uint8_t*>(obj), "_u8"); return;
            case type_hash::SmallInt:  append_int(*reinterpret_cast<const int16_t*>(obj), "_s16"); return;
            case type_hash::USmallInt: append_uint(*reinterpret_cast<const uint16_t*>(obj), "_u16"); return;
            case type_hash::Integer:   append_int(*reinterpret_cast<const int32_t*>(obj)); return;
            case type_hash::UInteger:  append_uint(*reinterpret_cast<const uint32_t*>(obj), "u"); return;
            case type_hash::BigInt:    append_int(*reinterpret_cast<const int64_t*>(obj), "ll"); return;
            case type_hash::UBigInt:   append_uint(*reinterpret_cast<const uint64_t*>(obj), "ull"); return;
            case type_hash::Real:      append_float(*reinterpret_cast<const float*>(obj)); return;
            case type_hash::Double:    append_double(*reinterpret_cast<const double*>(obj)); return;
            case type_hash::Boolean: {
                uint8_t v;
                std::memcpy(&v, obj, 1);
                out_ += v ? "true" : "false";
                return;
            }
            case type_hash::Varchar: {
                auto* s = reinterpret_cast<const ArenaString*>(obj);
                stringify_string(s->view());
                return;
            }
            default: break;
        }

        // Compound types.
        if (tc == type_hash::Datatype) {
            stringify_datatype(reinterpret_cast<const DatatypeData*>(obj));
        } else if (tc == type_hash::TypedValue) {
            stringify_typed_value(reinterpret_cast<const TypedValueData*>(obj));
        } else if (tc == type_hash::Parameter) {
            stringify_parameter(reinterpret_cast<const ParameterData*>(obj));
        }
        // Container types.
        else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::Hermes) {
            stringify_tiny_map(reinterpret_cast<const TinyObjectMap*>(obj));
        } else if (tag.descriptor() == TagDescriptor::Array && tc == type_hash::ObjectArray) {
            stringify_object_array(reinterpret_cast<const ObjectArray*>(obj));
        } else if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::ObjectMap) {
            stringify_object_map(reinterpret_cast<const ObjectMap*>(obj));
        } else {
            out_ += "null"; // Unknown type.
        }
    }

    void stringify_tagged_ptr(const AnyVal* slot) {
        if (slot->is_null()) {
            out_ += "null";
        } else if (slot->is_value()) {
            stringify_embedded(slot);
        } else {
            auto* target = slot->as_ptr<uint8_t>(base_);
            stringify_tagged(target);
        }
    }

    void stringify_embedded(const AnyVal* slot) {
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::TinyInt:   append_int(slot->as_value<int8_t>(), "_s8"); return;
            case type_hash::UTinyInt:  append_uint(slot->as_value<uint8_t>(), "_u8"); return;
            case type_hash::SmallInt:  append_int(slot->as_value<int16_t>(), "_s16"); return;
            case type_hash::USmallInt: append_uint(slot->as_value<uint16_t>(), "_u16"); return;
            case type_hash::Integer:   append_int(slot->as_value<int32_t>()); return;
            case type_hash::UInteger:  append_uint(slot->as_value<uint32_t>(), "u"); return;
            case type_hash::Real:      append_float(slot->as_value<float>()); return;
            case type_hash::Boolean: {
                out_ += slot->as_value<uint8_t>() ? "true" : "false";
                return;
            }
            default:
                out_ += "null";
        }
    }

    // --- Formatters ---

    template <typename T>
    void append_int(T val, const char* suffix = nullptr) {
        out_ += std::to_string(static_cast<int64_t>(val));
        if (suffix) out_ += suffix;
    }

    template <typename T>
    void append_uint(T val, const char* suffix = nullptr) {
        out_ += std::to_string(static_cast<uint64_t>(val));
        if (suffix) out_ += suffix;
    }

    void append_float(float val) {
        char buf[32];
        int n = std::snprintf(buf, sizeof(buf), "%g", val);
        std::string_view sv(buf, n);
        out_ += sv;
        // Ensure 'f' suffix unless it already contains 'e' or '.'.
        if (sv.find('.') == std::string_view::npos && sv.find('e') == std::string_view::npos) {
            out_ += ".0";
        }
        out_ += "f";
    }

    void append_double(double val) {
        char buf[32];
        int n = std::snprintf(buf, sizeof(buf), "%g", val);
        std::string_view sv(buf, n);
        out_ += sv;
        if (sv.find('.') == std::string_view::npos && sv.find('e') == std::string_view::npos) {
            out_ += ".0";
        }
        out_ += "d";
    }

    // --- Compound types ---

    void stringify_datatype(const DatatypeData* dt) {
        out_ += dt->name_view(base_);
        if (dt->has_params()) {
            out_ += '<';
            auto* params = dt->params.get(base_);
            auto* params_mut = const_cast<ObjectArray*>(params);
            for (uint64_t i = 0; i < params->size(); ++i) {
                if (i > 0) out_ += ", ";
                stringify_tagged_ptr(params_mut->slot(i, base_));
            }
            out_ += '>';
        }
        if (dt->has_ctr()) {
            out_ += '(';
            auto* ctr = dt->ctr.get(base_);
            auto* ctr_mut = const_cast<ObjectArray*>(ctr);
            for (uint64_t i = 0; i < ctr->size(); ++i) {
                if (i > 0) out_ += ", ";
                stringify_tagged_ptr(ctr_mut->slot(i, base_));
            }
            out_ += ')';
        }
        // Pointer qualifiers.
        for (uint8_t i = 0; i < dt->ptr_count(); ++i) {
            out_ += '*';
        }
        if (dt->is_const()) out_ += " const";
        if (dt->is_volatile()) out_ += " volatile";
        if (dt->ref_count() == 1) out_ += '&';
        else if (dt->ref_count() == 2) out_ += "&&";
    }

    void stringify_typed_value(const TypedValueData* tv) {
        out_ += '@';
        stringify_datatype(tv->datatype.get(base_));
        out_ += " = ";
        // The value AnyVal lives at &tv->value — pass its actual address
        // so pointer-mode offsets resolve correctly.
        stringify_tagged_ptr(&tv->value);
    }

    void stringify_parameter(const ParameterData* p) {
        out_ += '?';
        out_ += p->name_view(base_);
    }

    void stringify_string(std::string_view sv) {
        out_ += '"';
        for (char c : sv) {
            switch (c) {
                case '"':  out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char hex[8];
                        std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
                        out_ += hex;
                    } else {
                        out_ += c;
                    }
            }
        }
        out_ += '"';
    }

    // --- Containers ---

    void newline_indent() {
        if (!pretty_) return;
        out_ += '\n';
        for (int i = 0; i < indent_; ++i) out_ += "  ";
    }

    void stringify_object_array(const ObjectArray* arr) {
        out_ += '[';
        auto* arr_mut = const_cast<ObjectArray*>(arr);
        for (uint64_t i = 0; i < arr->size(); ++i) {
            if (i > 0) out_ += pretty_ ? ", " : ",";
            if (pretty_ && arr->size() > 4) { indent_++; newline_indent(); indent_--; }
            stringify_tagged_ptr(arr_mut->slot(i, base_));
        }
        out_ += ']';
    }

    void stringify_tiny_map(const TinyObjectMap* map) {
        out_ += '{';
        if (pretty_) indent_++;
        uint64_t bm = map->bitmap();
        bool first = true;
        for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            if (!first) out_ += pretty_ ? "," : ",";
            if (pretty_) newline_indent();
            else if (!first) out_ += ' ';
            first = false;
            // Use string keys like "k0", "k5" to make the output re-parseable
            // as an ObjectMap (Hermes text format only supports string keys in maps).
            out_ += "\"k";
            out_ += std::to_string(key);
            out_ += "\"";
            out_ += pretty_ ? ": " : ":";
            stringify_tagged_ptr(map->slot(key, base_));
        }
        if (pretty_) { indent_--; newline_indent(); }
        out_ += '}';
    }

    void stringify_object_map(const ObjectMap* map) {
        out_ += '{';
        if (pretty_) indent_++;
        bool first = true;
        map->for_each([&](ArenaString* key, AnyVal* val) {
            if (!first) out_ += pretty_ ? "," : ",";
            if (pretty_) newline_indent();
            else if (!first) out_ += ' ';
            first = false;
            stringify_string(key->view());
            out_ += pretty_ ? ": " : ":";
            stringify_tagged_ptr(val);
        }, base_);
        if (pretty_) { indent_--; newline_indent(); }
        out_ += '}';
    }
};

// ============================================================================
// Public API
// ============================================================================

std::string stringify(const HermesCtr& doc, bool pretty) {
    Stringifier s(pretty);
    s.stringify_root(doc);
    return std::move(s.output());
}

} // namespace logos::hermes
