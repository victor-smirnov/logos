// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

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
void fmt_int(T val, const char* suffix, std::string& out) noexcept {
    out += std::to_string(static_cast<int64_t>(val));
    if (suffix) out += suffix;
}

template <typename T>
void fmt_uint(T val, const char* suffix, std::string& out) noexcept {
    out += std::to_string(static_cast<uint64_t>(val));
    if (suffix) out += suffix;
}

void fmt_float(float val, std::string& out) noexcept {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", val);
    std::string_view sv(buf, n);
    out += sv;
    if (sv.find('.') == std::string_view::npos &&
        sv.find('e') == std::string_view::npos)
        out += ".0";
    out += 'f';
}

void fmt_double(double val, std::string& out) noexcept {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", val);
    std::string_view sv(buf, n);
    out += sv;
    if (sv.find('.') == std::string_view::npos &&
        sv.find('e') == std::string_view::npos)
        out += ".0";
    out += 'd';
}

void fmt_string(std::string_view sv, std::string& out) noexcept {
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

void newline_indent(int indent, std::string& out) noexcept {
    out += '\n';
    for (int i = 0; i < indent; ++i) out += "  ";
}

// ============================================================================
// Per-type stringify_tagged handlers
// ============================================================================

logos::expected<void> s_tinyint  (const uint8_t* o, StringifyCtx* c) noexcept { fmt_int(*reinterpret_cast<const int8_t*>(o),   "_s8",  *c->out); return {}; }
logos::expected<void> s_utinyint (const uint8_t* o, StringifyCtx* c) noexcept { fmt_uint(*reinterpret_cast<const uint8_t*>(o), "_u8",  *c->out); return {}; }
logos::expected<void> s_smallint (const uint8_t* o, StringifyCtx* c) noexcept { fmt_int(*reinterpret_cast<const int16_t*>(o),  "_s16", *c->out); return {}; }
logos::expected<void> s_usmallint(const uint8_t* o, StringifyCtx* c) noexcept { fmt_uint(*reinterpret_cast<const uint16_t*>(o),"_u16", *c->out); return {}; }
logos::expected<void> s_integer  (const uint8_t* o, StringifyCtx* c) noexcept { fmt_int(*reinterpret_cast<const int32_t*>(o),  nullptr,*c->out); return {}; }
logos::expected<void> s_uinteger (const uint8_t* o, StringifyCtx* c) noexcept { fmt_uint(*reinterpret_cast<const uint32_t*>(o),"u",    *c->out); return {}; }
logos::expected<void> s_bigint   (const uint8_t* o, StringifyCtx* c) noexcept { fmt_int(*reinterpret_cast<const int64_t*>(o),  "ll",   *c->out); return {}; }
logos::expected<void> s_ubigint  (const uint8_t* o, StringifyCtx* c) noexcept { fmt_uint(*reinterpret_cast<const uint64_t*>(o),"ull",  *c->out); return {}; }
logos::expected<void> s_real     (const uint8_t* o, StringifyCtx* c) noexcept { fmt_float(*reinterpret_cast<const float*>(o),   *c->out); return {}; }
logos::expected<void> s_double   (const uint8_t* o, StringifyCtx* c) noexcept { fmt_double(*reinterpret_cast<const double*>(o), *c->out); return {}; }

logos::expected<void> s_boolean(const uint8_t* o, StringifyCtx* c) noexcept {
    uint8_t v; std::memcpy(&v, o, 1);
    *c->out += v ? "true" : "false";
    return {};
}

logos::expected<void> s_varchar(const uint8_t* o, StringifyCtx* c) noexcept {
    fmt_string(reinterpret_cast<const ArenaString*>(o)->view(), *c->out);
    return {};
}

// ============================================================================
// Per-type stringify_embed handlers (value-mode AnyVal)
// ============================================================================

logos::expected<void> e_tinyint  (const AnyVal* s, StringifyCtx* c) noexcept { fmt_int(s->as_value<int8_t>(),   "_s8",  *c->out); return {}; }
logos::expected<void> e_utinyint (const AnyVal* s, StringifyCtx* c) noexcept { fmt_uint(s->as_value<uint8_t>(), "_u8",  *c->out); return {}; }
logos::expected<void> e_smallint (const AnyVal* s, StringifyCtx* c) noexcept { fmt_int(s->as_value<int16_t>(),  "_s16", *c->out); return {}; }
logos::expected<void> e_usmallint(const AnyVal* s, StringifyCtx* c) noexcept { fmt_uint(s->as_value<uint16_t>(),"_u16", *c->out); return {}; }
logos::expected<void> e_integer  (const AnyVal* s, StringifyCtx* c) noexcept { fmt_int(s->as_value<int32_t>(),  nullptr,*c->out); return {}; }
logos::expected<void> e_uinteger (const AnyVal* s, StringifyCtx* c) noexcept { fmt_uint(s->as_value<uint32_t>(),"u",    *c->out); return {}; }
// e_real — deleted: float (Real, hash=30) is not embeddable in the 4-byte
// AnyVal layout; it always lives in the arena as a pointer-mode slot and is
// stringified via s_real.

logos::expected<void> e_boolean(const AnyVal* s, StringifyCtx* c) noexcept {
    *c->out += s->as_value<uint8_t>() ? "true" : "false";
    return {};
}

// ============================================================================
// Container handlers
// ============================================================================

logos::expected<void> s_object_array(const uint8_t* o, StringifyCtx* c) noexcept {
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
        LOGOS_TRY_VOID(c->recurse_anyval(arr_mut->slot(i, c->base), c));
    }
    *c->out += ']';
    return {};
}

logos::expected<void> s_tiny_map(const uint8_t* o, StringifyCtx* c) noexcept {
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
        LOGOS_TRY_VOID(c->recurse_anyval(map->slot(key, c->base), c));
    }
    if (c->pretty) { c->indent--; newline_indent(c->indent, *c->out); }
    *c->out += '}';
    return {};
}

logos::expected<void> s_object_map(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* map = reinterpret_cast<const ObjectMap*>(o);
    *c->out += '{';
    if (c->pretty) c->indent++;
    bool first = true;
    // for_each callback is void — use status-variable pattern.
    logos::expected<void> status{};
    map->for_each([&](ArenaString* key, AnyVal* val) noexcept {
        if (!status) return;
        if (!first) *c->out += ',';
        if (c->pretty) newline_indent(c->indent, *c->out);
        else if (!first) *c->out += ' ';
        first = false;
        fmt_string(key->view(), *c->out);
        *c->out += c->pretty ? ": " : ":";
        auto res = c->recurse_anyval(val, c);
        if (!res) { status = std::unexpected(std::move(res.error())); }
    }, c->base);
    LOGOS_TRY_VOID(std::move(status));
    if (c->pretty) { c->indent--; newline_indent(c->indent, *c->out); }
    *c->out += '}';
    return {};
}

// ============================================================================
// Compound type handlers
// ============================================================================

logos::expected<void> s_datatype(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* dt = reinterpret_cast<const DatatypeData*>(o);
    *c->out += dt->name_view(c->base);
    if (dt->has_params()) {
        *c->out += '<';
        auto* params = const_cast<ObjectArray*>(dt->params.get(c->base));
        for (uint64_t i = 0; i < params->size(); ++i) {
            if (i > 0) *c->out += ", ";
            LOGOS_TRY_VOID(c->recurse_anyval(params->slot(i, c->base), c));
        }
        *c->out += '>';
    }
    if (dt->has_ctr()) {
        *c->out += '(';
        auto* ctr = const_cast<ObjectArray*>(dt->ctr.get(c->base));
        for (uint64_t i = 0; i < ctr->size(); ++i) {
            if (i > 0) *c->out += ", ";
            LOGOS_TRY_VOID(c->recurse_anyval(ctr->slot(i, c->base), c));
        }
        *c->out += ')';
    }
    for (uint8_t i = 0; i < dt->ptr_count(); ++i) *c->out += '*';
    if (dt->is_const())    *c->out += " const";
    if (dt->is_volatile()) *c->out += " volatile";
    if (dt->ref_count() == 1) *c->out += '&';
    else if (dt->ref_count() == 2) *c->out += "&&";
    return {};
}

logos::expected<void> s_typed_value(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* tv = reinterpret_cast<const TypedValueData*>(o);
    *c->out += '@';
    LOGOS_TRY_VOID(c->recurse_tagged(reinterpret_cast<const uint8_t*>(tv->datatype.get(c->base)), c));
    *c->out += " = ";
    LOGOS_TRY_VOID(c->recurse_anyval(&tv->value, c));
    return {};
}

// Decimal stringify: mirrors decimal_write_number from stdlib/hermes/decimal.logos.
// Reconstructs digit string from base-1e9 limbs, then inserts decimal point.
logos::expected<void> s_decimal(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* d = reinterpret_cast<const DecimalData*>(o);
    uint32_t nlimbs = d->nlimbs();
    uint32_t scale  = d->scale();
    bool     neg    = d->negative();
    const uint32_t* limbs = d->limbs();

    // Build digit string from most-significant limb downward.
    std::string digits;
    if (nlimbs == 0) {
        // Zero value.
        if (scale == 0) {
            *c->out += '0';
        } else {
            *c->out += "0.";
            for (uint32_t k = 0; k < scale; ++k) *c->out += '0';
        }
        return {};
    }

    // High limb (no leading-zero padding).
    digits += std::to_string(static_cast<uint64_t>(limbs[nlimbs - 1]));
    // Remaining limbs padded to 9 digits each.
    for (int32_t i = static_cast<int32_t>(nlimbs) - 2; i >= 0; --i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%09u", limbs[i]);
        digits += buf;
    }

    int64_t dlen  = static_cast<int64_t>(digits.size());
    int64_t s_i   = static_cast<int64_t>(scale);

    if (neg) *c->out += '-';

    if (scale == 0) {
        *c->out += digits;
        return {};
    }

    if (dlen <= s_i) {
        *c->out += "0.";
        for (int64_t z = 0; z < s_i - dlen; ++z) *c->out += '0';
        *c->out += digits;
        return {};
    }

    int64_t int_len = dlen - s_i;
    *c->out += digits.substr(0, static_cast<size_t>(int_len));
    *c->out += '.';
    *c->out += digits.substr(static_cast<size_t>(int_len));
    return {};
}

logos::expected<void> s_parameter(const uint8_t* o, StringifyCtx* c) noexcept {
    auto* p = reinterpret_cast<const ParameterData*>(o);
    *c->out += '?';
    *c->out += p->name_view(c->base);
    return {};
}

} // anonymous namespace

// ============================================================================
// Clone-handler forward decls (defined in clone.cpp).
// ============================================================================
namespace clone_impl {
    logos::expected<uint32_t> c_tinyint   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_utinyint  (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_smallint  (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_usmallint (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_integer   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_uinteger  (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_bigint    (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_ubigint   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_real      (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_double    (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_boolean   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_varchar   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_object_array(const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_tiny_map   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_object_map (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_datatype   (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_typed_value(const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_decimal     (const uint8_t*, CloneCtx*) noexcept;
    logos::expected<uint32_t> c_parameter  (const uint8_t*, CloneCtx*) noexcept;
}

namespace {

// ============================================================================
// TypeOps table entries for all core types
// ============================================================================

const TypeOps k_tinyint_ops    = { type_hash::I8,    s_tinyint,    e_tinyint,    nullptr, clone_impl::c_tinyint };
const TypeOps k_utinyint_ops   = { type_hash::U8,   s_utinyint,   e_utinyint,   nullptr, clone_impl::c_utinyint };
const TypeOps k_smallint_ops   = { type_hash::I16,   s_smallint,   e_smallint,   nullptr, clone_impl::c_smallint };
const TypeOps k_usmallint_ops  = { type_hash::U16,  s_usmallint,  e_usmallint,  nullptr, clone_impl::c_usmallint };
const TypeOps k_integer_ops    = { type_hash::I24,    s_integer,    e_integer,    nullptr, clone_impl::c_integer };
const TypeOps k_uinteger_ops   = { type_hash::U24,   s_uinteger,   e_uinteger,   nullptr, clone_impl::c_uinteger };
const TypeOps k_bigint_ops     = { type_hash::I64,     s_bigint,     nullptr,      nullptr, clone_impl::c_bigint };
const TypeOps k_ubigint_ops    = { type_hash::U64,    s_ubigint,    nullptr,      nullptr, clone_impl::c_ubigint };
const TypeOps k_real_ops       = { type_hash::F32,       s_real,       nullptr,      nullptr, clone_impl::c_real };
const TypeOps k_double_ops     = { type_hash::F64,     s_double,     nullptr,      nullptr, clone_impl::c_double };
const TypeOps k_boolean_ops    = { type_hash::Bool,    s_boolean,    e_boolean,    nullptr, clone_impl::c_boolean };
const TypeOps k_varchar_ops    = { type_hash::HermesString,    s_varchar,    nullptr,      nullptr, clone_impl::c_varchar };
const TypeOps k_obj_array_ops  = { type_hash::Array,s_object_array,nullptr,     nullptr, clone_impl::c_object_array };
const TypeOps k_tiny_map_ops   = { type_hash::TinyObjectMap,     s_tiny_map,   nullptr,      nullptr, clone_impl::c_tiny_map };
const TypeOps k_obj_map_ops    = { type_hash::ObjectMap,  s_object_map, nullptr,      nullptr, clone_impl::c_object_map };
const TypeOps k_datatype_ops   = { type_hash::Datatype,   s_datatype,   nullptr,      nullptr, clone_impl::c_datatype };
const TypeOps k_typed_val_ops  = { type_hash::TypedValue, s_typed_value,nullptr,      nullptr, clone_impl::c_typed_value };
const TypeOps k_decimal_ops    = { type_hash::Decimal,    s_decimal,    nullptr,      nullptr, clone_impl::c_decimal };
const TypeOps k_parameter_ops  = { type_hash::Parameter,  s_parameter,  nullptr,      nullptr, clone_impl::c_parameter };

} // anonymous namespace

// Link-time anchor: referenced from type_ops.cpp so that this TU (and its
// HERMES_REGISTER_TYPE entries below) is pulled in when the Hermes library is
// consumed as a static archive by a client that doesn't otherwise call any
// stringify function (e.g. the logos compiler, which uses clone() only).
void hermes_stringify_anchor() noexcept {}

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
HERMES_REGISTER_TYPE(k_decimal_ops);
HERMES_REGISTER_TYPE(k_parameter_ops);

// ============================================================================
// Recursive dispatch
// ============================================================================

static logos::expected<void> do_recurse_tagged(const uint8_t* obj, StringifyCtx* ctx) noexcept {
    TypeTag tag = TypeTag::read_before(obj);
    const TypeOps* ops = find_type_ops(tag.type_code());
    if (ops && ops->stringify_tagged) {
        LOGOS_TRY_VOID(ops->stringify_tagged(obj, ctx));
    } else {
        *ctx->out += "null";
    }
    return {};
}

static logos::expected<void> do_recurse_anyval(const AnyVal* slot, StringifyCtx* ctx) noexcept {
    if (slot->is_null()) { *ctx->out += "null"; return {}; }
    if (slot->is_value()) {
        const TypeOps* ops = find_type_ops(slot->value_type_hash());
        if (ops && ops->stringify_embed) {
            LOGOS_TRY_VOID(ops->stringify_embed(slot, ctx));
        } else {
            *ctx->out += "null";
        }
        return {};
    }
    return do_recurse_tagged(slot->as_ptr<uint8_t>(ctx->base), ctx);
}

// ============================================================================
// Public API
// ============================================================================

logos::expected<std::string> stringify(const Hermes& doc, bool pretty) noexcept {
    std::string out;
    if (!doc.has_root()) return std::string("null");
    StringifyCtx ctx;
    ctx.base           = const_cast<uint8_t*>(HermesAccess::base(doc));
    ctx.pretty         = pretty;
    ctx.indent         = 0;
    ctx.out            = &out;
    ctx.recurse_anyval = do_recurse_anyval;
    ctx.recurse_tagged = do_recurse_tagged;
    // Root may be an inline AnyVal (value-mode: bit 0 = 1) stored directly in
    // the root_offset field rather than a tagged-object offset.
    arena_offset_t root_off = HermesAccess::root_offset(doc);
    if (root_off.value() & 1u) {
        // Value-mode root: treat raw bits as AnyVal.
        AnyVal v = AnyVal::from_raw(root_off.value());
        LOGOS_TRY_VOID(do_recurse_anyval(&v, &ctx));
    } else {
        const uint8_t* root = static_cast<const uint8_t*>(HermesAccess::root<void>(doc));
        LOGOS_TRY_VOID(do_recurse_tagged(root, &ctx));
    }
    return out;
}

} // namespace logos::hermes
