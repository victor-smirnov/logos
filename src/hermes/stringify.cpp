// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/stringify.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/hermes/type_tag.hpp>

#include <cstring>
#include <string>

namespace logos::hermes {

// ============================================================================
// Helpers (shared by multiple handlers)
// ============================================================================

namespace {

template <typename T>
void fmt_int(T val, const char* suffix, std::string& out) {
    out += std::to_string(static_cast<int64_t>(val));
    if (suffix) out += suffix;
}

template <typename T>
void fmt_uint(T val, const char* suffix, std::string& out) {
    out += std::to_string(static_cast<uint64_t>(val));
    if (suffix) out += suffix;
}

void fmt_float(float val, std::string& out) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", val);
    std::string_view sv(buf, n);
    out += sv;
    if (sv.find('.') == std::string_view::npos &&
        sv.find('e') == std::string_view::npos)
        out += ".0";
    out += 'f';
}

void fmt_double(double val, std::string& out) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", val);
    std::string_view sv(buf, n);
    out += sv;
    if (sv.find('.') == std::string_view::npos &&
        sv.find('e') == std::string_view::npos)
        out += ".0";
    out += 'd';
}

void fmt_string(std::string_view sv, std::string& out) {
    out += '"';
    for (char c : sv) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += hex;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void newline_indent(int indent, std::string& out) {
    out += '\n';
    for (int i = 0; i < indent; ++i) out += "  ";
}

// ============================================================================
// Per-type stringify_tagged handlers
// ============================================================================

void s_tinyint  (const uint8_t* o, StringifyCtx* c) { fmt_int(*reinterpret_cast<const int8_t*>(o),   "_s8",  *c->out); }
void s_utinyint (const uint8_t* o, StringifyCtx* c) { fmt_uint(*reinterpret_cast<const uint8_t*>(o), "_u8",  *c->out); }
void s_smallint (const uint8_t* o, StringifyCtx* c) { fmt_int(*reinterpret_cast<const int16_t*>(o),  "_s16", *c->out); }
void s_usmallint(const uint8_t* o, StringifyCtx* c) { fmt_uint(*reinterpret_cast<const uint16_t*>(o),"_u16", *c->out); }
void s_integer  (const uint8_t* o, StringifyCtx* c) { fmt_int(*reinterpret_cast<const int32_t*>(o),  nullptr,*c->out); }
void s_uinteger (const uint8_t* o, StringifyCtx* c) { fmt_uint(*reinterpret_cast<const uint32_t*>(o),"u",    *c->out); }
void s_bigint   (const uint8_t* o, StringifyCtx* c) { fmt_int(*reinterpret_cast<const int64_t*>(o),  "ll",   *c->out); }
void s_ubigint  (const uint8_t* o, StringifyCtx* c) { fmt_uint(*reinterpret_cast<const uint64_t*>(o),"ull",  *c->out); }
void s_real     (const uint8_t* o, StringifyCtx* c) { fmt_float(*reinterpret_cast<const float*>(o),   *c->out); }
void s_double   (const uint8_t* o, StringifyCtx* c) { fmt_double(*reinterpret_cast<const double*>(o), *c->out); }

void s_boolean(const uint8_t* o, StringifyCtx* c) {
    uint8_t v; std::memcpy(&v, o, 1);
    *c->out += v ? "true" : "false";
}

void s_varchar(const uint8_t* o, StringifyCtx* c) {
    fmt_string(reinterpret_cast<const ArenaString*>(o)->view(), *c->out);
}

// ============================================================================
// Per-type stringify_embed handlers (value-mode AnyVal)
// ============================================================================

void e_tinyint  (const AnyVal* s, StringifyCtx* c) { fmt_int(s->as_value<int8_t>(),   "_s8",  *c->out); }
void e_utinyint (const AnyVal* s, StringifyCtx* c) { fmt_uint(s->as_value<uint8_t>(), "_u8",  *c->out); }
void e_smallint (const AnyVal* s, StringifyCtx* c) { fmt_int(s->as_value<int16_t>(),  "_s16", *c->out); }
void e_usmallint(const AnyVal* s, StringifyCtx* c) { fmt_uint(s->as_value<uint16_t>(),"_u16", *c->out); }
void e_integer  (const AnyVal* s, StringifyCtx* c) { fmt_int(s->as_value<int32_t>(),  nullptr,*c->out); }
void e_uinteger (const AnyVal* s, StringifyCtx* c) { fmt_uint(s->as_value<uint32_t>(),"u",    *c->out); }
void e_real     (const AnyVal* s, StringifyCtx* c) { fmt_float(s->as_value<float>(),   *c->out); }

void e_boolean(const AnyVal* s, StringifyCtx* c) {
    *c->out += s->as_value<uint8_t>() ? "true" : "false";
}

// ============================================================================
// Container handlers
// ============================================================================

void s_object_array(const uint8_t* o, StringifyCtx* c) {
    auto* arr     = reinterpret_cast<const ObjectArray*>(o);
    auto* arr_mut = const_cast<ObjectArray*>(arr);
    *c->out += '[';
    for (uint64_t i = 0; i < arr->size(); ++i) {
        if (i > 0) *c->out += c->pretty ? ", " : ",";
        if (c->pretty && arr->size() > 4) {
            c->indent++;
            newline_indent(c->indent, *c->out);
            c->indent--;
        }
        c->recurse_anyval(arr_mut->slot(i, c->base), c);
    }
    *c->out += ']';
}

void s_tiny_map(const uint8_t* o, StringifyCtx* c) {
    auto* map = reinterpret_cast<const TinyObjectMap*>(o);
    *c->out += '{';
    if (c->pretty) c->indent++;
    uint64_t bm  = map->bitmap();
    bool     first = true;
    for (uint8_t key = 0; key < TinyObjectMap::MAX_KEYS; ++key) {
        if (!(bm & (1ULL << key))) continue;
        if (!first) *c->out += ',';
        if (c->pretty) newline_indent(c->indent, *c->out);
        else if (!first) *c->out += ' ';
        first = false;
        *c->out += "\"k";
        *c->out += std::to_string(key);
        *c->out += '"';
        *c->out += c->pretty ? ": " : ":";
        c->recurse_anyval(map->slot(key, c->base), c);
    }
    if (c->pretty) { c->indent--; newline_indent(c->indent, *c->out); }
    *c->out += '}';
}

void s_object_map(const uint8_t* o, StringifyCtx* c) {
    auto* map = reinterpret_cast<const ObjectMap*>(o);
    *c->out += '{';
    if (c->pretty) c->indent++;
    bool first = true;
    map->for_each([&](ArenaString* key, AnyVal* val) {
        if (!first) *c->out += ',';
        if (c->pretty) newline_indent(c->indent, *c->out);
        else if (!first) *c->out += ' ';
        first = false;
        fmt_string(key->view(), *c->out);
        *c->out += c->pretty ? ": " : ":";
        c->recurse_anyval(val, c);
    }, c->base);
    if (c->pretty) { c->indent--; newline_indent(c->indent, *c->out); }
    *c->out += '}';
}

// ============================================================================
// Compound type handlers
// ============================================================================

void s_datatype(const uint8_t* o, StringifyCtx* c) {
    auto* dt = reinterpret_cast<const DatatypeData*>(o);
    *c->out += dt->name_view(c->base);
    if (dt->has_params()) {
        *c->out += '<';
        auto* params = const_cast<ObjectArray*>(dt->params.get(c->base));
        for (uint64_t i = 0; i < params->size(); ++i) {
            if (i > 0) *c->out += ", ";
            c->recurse_anyval(params->slot(i, c->base), c);
        }
        *c->out += '>';
    }
    if (dt->has_ctr()) {
        *c->out += '(';
        auto* ctr = const_cast<ObjectArray*>(dt->ctr.get(c->base));
        for (uint64_t i = 0; i < ctr->size(); ++i) {
            if (i > 0) *c->out += ", ";
            c->recurse_anyval(ctr->slot(i, c->base), c);
        }
        *c->out += ')';
    }
    for (uint8_t i = 0; i < dt->ptr_count(); ++i) *c->out += '*';
    if (dt->is_const())    *c->out += " const";
    if (dt->is_volatile()) *c->out += " volatile";
    if (dt->ref_count() == 1) *c->out += '&';
    else if (dt->ref_count() == 2) *c->out += "&&";
}

void s_typed_value(const uint8_t* o, StringifyCtx* c) {
    auto* tv = reinterpret_cast<const TypedValueData*>(o);
    *c->out += '@';
    c->recurse_tagged(reinterpret_cast<const uint8_t*>(tv->datatype.get(c->base)), c);
    *c->out += " = ";
    c->recurse_anyval(&tv->value, c);
}

void s_parameter(const uint8_t* o, StringifyCtx* c) {
    auto* p = reinterpret_cast<const ParameterData*>(o);
    *c->out += '?';
    *c->out += p->name_view(c->base);
}

// ============================================================================
// TypeOps table entries for all core types
// ============================================================================

const TypeOps k_tinyint_ops    = { type_hash::TinyInt,    s_tinyint,    e_tinyint,    nullptr };
const TypeOps k_utinyint_ops   = { type_hash::UTinyInt,   s_utinyint,   e_utinyint,   nullptr };
const TypeOps k_smallint_ops   = { type_hash::SmallInt,   s_smallint,   e_smallint,   nullptr };
const TypeOps k_usmallint_ops  = { type_hash::USmallInt,  s_usmallint,  e_usmallint,  nullptr };
const TypeOps k_integer_ops    = { type_hash::Integer,    s_integer,    e_integer,    nullptr };
const TypeOps k_uinteger_ops   = { type_hash::UInteger,   s_uinteger,   e_uinteger,   nullptr };
const TypeOps k_bigint_ops     = { type_hash::BigInt,     s_bigint,     nullptr,      nullptr };
const TypeOps k_ubigint_ops    = { type_hash::UBigInt,    s_ubigint,    nullptr,      nullptr };
const TypeOps k_real_ops       = { type_hash::Real,       s_real,       e_real,       nullptr };
const TypeOps k_double_ops     = { type_hash::Double,     s_double,     nullptr,      nullptr };
const TypeOps k_boolean_ops    = { type_hash::Boolean,    s_boolean,    e_boolean,    nullptr };
const TypeOps k_varchar_ops    = { type_hash::Varchar,    s_varchar,    nullptr,      nullptr };
const TypeOps k_obj_array_ops  = { type_hash::ObjectArray,s_object_array,nullptr,     nullptr };
const TypeOps k_tiny_map_ops   = { type_hash::Hermes,     s_tiny_map,   nullptr,      nullptr };
const TypeOps k_obj_map_ops    = { type_hash::ObjectMap,  s_object_map, nullptr,      nullptr };
const TypeOps k_datatype_ops   = { type_hash::Datatype,   s_datatype,   nullptr,      nullptr };
const TypeOps k_typed_val_ops  = { type_hash::TypedValue, s_typed_value,nullptr,      nullptr };
const TypeOps k_parameter_ops  = { type_hash::Parameter,  s_parameter,  nullptr,      nullptr };

} // anonymous namespace

// ---------------------------------------------------------------------------
// Linker-section registrations (one entry per core type)
// ---------------------------------------------------------------------------
HERMES_REGISTER_TYPE(k_tinyint_ops);
HERMES_REGISTER_TYPE(k_utinyint_ops);
HERMES_REGISTER_TYPE(k_smallint_ops);
HERMES_REGISTER_TYPE(k_usmallint_ops);
HERMES_REGISTER_TYPE(k_integer_ops);
HERMES_REGISTER_TYPE(k_uinteger_ops);
HERMES_REGISTER_TYPE(k_bigint_ops);
HERMES_REGISTER_TYPE(k_ubigint_ops);
HERMES_REGISTER_TYPE(k_real_ops);
HERMES_REGISTER_TYPE(k_double_ops);
HERMES_REGISTER_TYPE(k_boolean_ops);
HERMES_REGISTER_TYPE(k_varchar_ops);
HERMES_REGISTER_TYPE(k_obj_array_ops);
HERMES_REGISTER_TYPE(k_tiny_map_ops);
HERMES_REGISTER_TYPE(k_obj_map_ops);
HERMES_REGISTER_TYPE(k_datatype_ops);
HERMES_REGISTER_TYPE(k_typed_val_ops);
HERMES_REGISTER_TYPE(k_parameter_ops);

// ============================================================================
// Recursive dispatch (populates StringifyCtx callbacks)
// ============================================================================

static void do_recurse_tagged(const uint8_t* obj, StringifyCtx* ctx) {
    TypeTag tag = TypeTag::read_before(obj);
    const TypeOps* ops = find_type_ops(tag.type_code());
    if (ops && ops->stringify_tagged)
        ops->stringify_tagged(obj, ctx);
    else
        *ctx->out += "null";
}

static void do_recurse_anyval(const AnyVal* slot, StringifyCtx* ctx) {
    if (slot->is_null()) { *ctx->out += "null"; return; }
    if (slot->is_value()) {
        const TypeOps* ops = find_type_ops(slot->value_type_hash());
        if (ops && ops->stringify_embed)
            ops->stringify_embed(slot, ctx);
        else
            *ctx->out += "null";
        return;
    }
    do_recurse_tagged(slot->as_ptr<uint8_t>(ctx->base), ctx);
}

// ============================================================================
// Public API
// ============================================================================

logos::expected<std::string> stringify(const HermesCtr& doc, bool pretty) noexcept {
    std::string out;
    if (!doc.has_root()) { out = "null"; return out; }
    StringifyCtx ctx;
    ctx.base           = const_cast<uint8_t*>(HermesCtrAccess::base(doc));
    ctx.pretty         = pretty;
    ctx.indent         = 0;
    ctx.out            = &out;
    ctx.recurse_anyval = do_recurse_anyval;
    ctx.recurse_tagged = do_recurse_tagged;
    const uint8_t* root = static_cast<const uint8_t*>(HermesCtrAccess::root<void>(doc));
    do_recurse_tagged(root, &ctx);
    return out;
}

} // namespace logos::hermes
