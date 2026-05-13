// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis + L-IR lowering — core file.
//
// This file contains:
//   - Free utility functions (types_equal, type_str, types_compatible, etc.)
//   - SemaChecker::run(), init_primitives(), lookup_type_by_name()
//   - Scope/drop helpers, type-param helpers, subst_type_sema, resolve_type
//   - lower_program(), lower_module_items(), sema_lower() entry point
//
// Method bodies for collect_*, lower_expr/stmt/decl are in sema_collect.cpp,
// sema_expr.cpp, sema_stmt.cpp, sema_decl.cpp respectively.

#include "sema_impl.hpp"
#include "ctfe.hpp"

#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/sha256.hpp>
#include <logos/compiler/sema_schema.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/schema_codes.hpp>
#include <logos/verification/assert.hpp>

#include <cstdio>
#include <format>
#include <functional>

namespace logos::compiler {

// ── TypePool PIMPL ─────────────────────────────────────────────────────────
//
// Owns the single Hermes arena that backs every interned type. Each unique
// type lives as a TinyObjectMap inside this arena; TypeRef is a fat pointer
// {arena, offset, pool} into it.
//
// Arena mode: GrowableSingleChunk so that RelativePtrs resolved against
// arena.head().data() are always valid (MultiChunk would scatter tail
// allocations into separate buffers, breaking TinyObjectMap's fixed-base
// addressing).

class TypePoolImpl {
public:
    // Owning ref to MemHolder: refcount==1 at init, dropped in dtor. Views
    // (OStringView et al.) take additional refs via Own<>. The arena moves on
    // grow() — never cache base pointers; always re-fetch via holder_->base().
    hermes::MemHolder*                                     holder_ = nullptr;

    hermes::Arena&       arena()       noexcept { return holder_->arena(); }
    const hermes::Arena& arena() const noexcept { return holder_->arena(); }
    hermes::MemHolder*   holder() const noexcept { return holder_; }

    // 2c.5.4: intern table keyed by TypeUID (32-byte SHA-256-derived).
    // Bucket walk via builder_equals_typeref preserves byte-strict equality
    // (lifetime, pkg_name, lifetime_args, const_val) which TypeUID
    // intentionally collapses to match types_equal semantics.
    struct UIDHash {
        size_t operator()(const LogosType::TypeUID& u) const noexcept {
            size_t h = 0; std::memcpy(&h, u.bytes, sizeof(h)); return h;
        }
    };
    std::unordered_map<LogosType::TypeUID,
                       std::vector<hermes::arena_offset_t>, UIDHash> intern_buckets_;

    // 2c.6.6.B.6: TypeUID per offset. Populated by TypePool::alloc(); read by
    // put_sub (UID composition) and types_equal.
    std::unordered_map<hermes::arena_offset_t, LogosType::TypeUID> uid_of_;

    LogosType::TypeUID uid_of(TypeRef p) const noexcept {
        if (!p) return LogosType::TypeUID{};
        auto it = uid_of_.find(p.offset());
        return it != uid_of_.end() ? it->second : LogosType::TypeUID{};
    }

    TypeRef ref(hermes::arena_offset_t off) const noexcept {
        return TypeRef{&arena(), off, this};
    }

    TypePoolImpl(logos::InitTag& tag) {
        // MemHolder is heap-only (private dtor), so allocate directly and
        // release via ref/unref on failure.
        holder_ = new hermes::MemHolder(
            tag, 64 * 1024, hermes::ArenaMode::GrowableSingleChunk);
        if (!tag.ok()) {
            holder_->ref();
            holder_->unref();
            holder_ = nullptr;
            return;
        }
        holder_->ref();  // initial owning reference
        // Reserve offset 0 for the DocumentHeader so a zero offset reads as
        // the canonical "null" sentinel for AnyVal / RelativePtr.
        auto hdr_exp = arena().allocate_raw(sizeof(hermes::DocumentHeader),
                                            alignof(hermes::DocumentHeader));
        if (!hdr_exp) {
            tag.fail(std::move(hdr_exp.error()));
            return;
        }
        auto* hdr = static_cast<hermes::DocumentHeader*>(*hdr_exp);
        hdr->root_offset = hermes::NULL_OFFSET;
    }

    ~TypePoolImpl() {
        if (holder_) holder_->unref();
    }
    TypePoolImpl(const TypePoolImpl&) = delete;
    TypePoolImpl& operator=(const TypePoolImpl&) = delete;

    static std::unique_ptr<TypePoolImpl> make() {
        logos::InitTag tag;
        auto p = std::make_unique<TypePoolImpl>(tag);
        LOGOS_ASSERT(tag.ok(), "SEMA-TYPEPOOL-001",
            "TypePool Hermes arena initialisation failed");
        return p;
    }

    hermes::arena_offset_t offset_of(const void* p) const noexcept {
        auto off = static_cast<uint32_t>(
            static_cast<const uint8_t*>(p) - arena().head().data());
        return hermes::arena_offset_t{off};
    }

    hermes::TinyObjectMap* at(hermes::arena_offset_t off) noexcept {
        return reinterpret_cast<hermes::TinyObjectMap*>(
            arena().head().data() + off.value());
    }

    // Allocate an ArenaString and return an AnyVal pointing at it.
    hermes::AnyVal put_string(std::string_view s) {
        auto p = hermes::ArenaString::create(arena(), s);
        LOGOS_ASSERT(p.has_value(), "SEMA-TYPEPOOL-003", "ArenaString alloc failed");
        return hermes::AnyVal::from_offset(offset_of(*p));
    }

    // Translate a TypeRef to an AnyVal pointing at its mirror.
    hermes::AnyVal ptr_to_mirror(TypeRef p) {
        if (!p) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(p.offset());
    }

    // Build an ObjectArray from a vector<TypeRef> and return AnyVal.
    hermes::AnyVal put_type_vec(const std::vector<TypeRef>& v) {
        auto arr = hermes::ObjectArray::create(arena(), v.empty() ? 1 : v.size());
        LOGOS_ASSERT(arr.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray alloc failed");
        auto arr_off = offset_of(*arr);
        for (auto elem : v) {
            auto v_any = ptr_to_mirror(elem);
            auto r = reinterpret_cast<hermes::ObjectArray*>(
                         arena().head().data() + arr_off.value())
                     ->push_back(v_any, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray push failed");
        }
        return hermes::AnyVal::from_offset(arr_off);
    }

    // Build an ObjectArray from a vector<std::string> (lifetime_args).
    hermes::AnyVal put_string_vec(const std::vector<std::string>& v) {
        auto arr = hermes::ObjectArray::create(arena(), v.empty() ? 1 : v.size());
        LOGOS_ASSERT(arr.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray alloc failed");
        auto arr_off = offset_of(*arr);
        for (const auto& s : v) {
            auto v_any = put_string(s);
            auto r = reinterpret_cast<hermes::ObjectArray*>(
                         arena().head().data() + arr_off.value())
                     ->push_back(v_any, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray push failed");
        }
        return hermes::AnyVal::from_offset(arr_off);
    }

    // Build a Hermes mirror for `t` and return its arena offset.
    // Every field populated on the C++ struct is also written to the mirror
    // under the key defined in sema_schema.hpp. Reads still go through the
    // raw struct pointer — Phase 2c.3 will switch TypeRef to read the mirror.
    hermes::arena_offset_t mirror(const LogosTypeBuilder& t) {
        namespace k = sema_schema;

        // Pre-allocate all sub-values first (each may grow the arena and
        // invalidate `map`); re-fetch the map pointer before every put via
        // the at(map_off) helper.
        hermes::AnyVal v_mut_ptr, v_arr_size, v_const_val;
        hermes::AnyVal v_pointee, v_elem, v_assoc_base, v_closure_ret;
        hermes::AnyVal v_lifetime, v_arr_size_var, v_struct_name, v_enum_name;
        hermes::AnyVal v_pkg_name, v_trait_name, v_type_var_name, v_assoc_type_name;
        hermes::AnyVal v_type_args, v_tuple_elems, v_closure_params, v_gat_args;
        hermes::AnyVal v_lifetime_args;

        if (t.kind == LogosType::Kind::Ptr) {
            v_mut_ptr = hermes::AnyVal::from_value<uint8_t>(
                t.mut_ptr ? 1 : 0, hermes::type_hash::Bool);
        }
        if (t.kind == LogosType::Kind::Array && t.arr_size != 0) {
            auto av = hermes::anyval_put<uint64_t>(arena(), t.arr_size);
            LOGOS_ASSERT(av.has_value(), "SEMA-TYPEPOOL-003", "arr_size put failed");
            v_arr_size = *av;
        }
        if (t.const_val.has_value()) {
            auto av = hermes::anyval_put<int64_t>(arena(), *t.const_val);
            LOGOS_ASSERT(av.has_value(), "SEMA-TYPEPOOL-003", "const_val put failed");
            v_const_val = *av;
        }

        if (t.pointee)     v_pointee     = ptr_to_mirror(t.pointee);
        if (t.elem)        v_elem        = ptr_to_mirror(t.elem);
        if (t.assoc_base)  v_assoc_base  = ptr_to_mirror(t.assoc_base);
        if (t.closure_ret) v_closure_ret = ptr_to_mirror(t.closure_ret);

        if (!t.lifetime.empty())        v_lifetime        = put_string(t.lifetime);
        if (!t.arr_size_var.empty())    v_arr_size_var    = put_string(t.arr_size_var);
        if (!t.struct_name.empty())     v_struct_name     = put_string(t.struct_name);
        if (!t.enum_name.empty())       v_enum_name       = put_string(t.enum_name);
        if (!t.pkg_name.empty())        v_pkg_name        = put_string(t.pkg_name);
        if (!t.trait_name.empty())      v_trait_name      = put_string(t.trait_name);
        if (!t.type_var_name.empty())   v_type_var_name   = put_string(t.type_var_name);
        if (!t.assoc_type_name.empty()) v_assoc_type_name = put_string(t.assoc_type_name);

        if (!t.type_args.empty())       v_type_args       = put_type_vec(t.type_args);
        if (!t.tuple_elems.empty())     v_tuple_elems     = put_type_vec(t.tuple_elems);
        if (!t.closure_params.empty())  v_closure_params  = put_type_vec(t.closure_params);
        if (!t.gat_args.empty())        v_gat_args        = put_type_vec(t.gat_args);
        if (!t.lifetime_args.empty())   v_lifetime_args   = put_string_vec(t.lifetime_args);

        // Create the map last so it doesn't get moved around by sub-allocs
        // (the map's own grow() handles relocation internally during put()).
        auto map_exp = hermes::TinyObjectMap::create(arena(), /*capacity=*/8);
        LOGOS_ASSERT(map_exp.has_value(), "SEMA-TYPEPOOL-002",
            "TinyObjectMap allocation failed");
        hermes::arena_offset_t map_off = offset_of(*map_exp);
        (*map_exp)->set_schema_type_code(
            hermes::schema::type(int32_t(t.kind)));

        auto put = [&](const sema_schema::Key& key, hermes::AnyVal val) {
            if (val.is_null()) return;
            auto r = at(map_off)->put(key.code, val, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003",
                "TinyObjectMap put failed");
        };

        put(k::MUT_PTR,          v_mut_ptr);
        put(k::ARR_SIZE,         v_arr_size);
        put(k::CONST_VAL,        v_const_val);
        put(k::POINTEE,          v_pointee);
        put(k::ELEM,             v_elem);
        put(k::ASSOC_BASE,       v_assoc_base);
        put(k::CLOSURE_RET,      v_closure_ret);
        put(k::LIFETIME,         v_lifetime);
        put(k::ARR_SIZE_VAR,     v_arr_size_var);
        put(k::STRUCT_NAME,      v_struct_name);
        put(k::ENUM_NAME,        v_enum_name);
        put(k::PKG_NAME,         v_pkg_name);
        put(k::TRAIT_NAME,       v_trait_name);
        put(k::TYPE_VAR_NAME,    v_type_var_name);
        put(k::ASSOC_TYPE_NAME,  v_assoc_type_name);
        put(k::TYPE_ARGS,        v_type_args);
        put(k::TUPLE_ELEMS,      v_tuple_elems);
        put(k::CLOSURE_PARAMS,   v_closure_params);
        put(k::GAT_ARGS,         v_gat_args);
        put(k::LIFETIME_ARGS,    v_lifetime_args);

        return map_off;
    }

};

TypePool::TypePool() = default;
TypePool::~TypePool() = default;
TypePool::TypePool(TypePool&&) noexcept = default;
TypePool& TypePool::operator=(TypePool&&) noexcept = default;

// ── Canonical structural hash ──
// 2c.5.4: canonical TypeUID computation.
//
// Layout per master plan: byte 0 = kind tag, bytes 1..23 = SHA-256 trim of
// canonical structural serialization (lifetime ignored, matches types_equal),
// bytes 24..31 = reserved member-id (0 for pure types).
//
// Serialization composes sub-type UIDs (already computed bottom-up) so the
// induction "sub-uid equality ⇔ sub-types-equal" closes the same way the
// previous u64 hash did.
namespace {

inline void put_byte(std::string& s, uint8_t b) { s.push_back(char(b)); }
inline void put_u64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) { s.push_back(char(v & 0xFF)); v >>= 8; }
}
inline void put_str(std::string& s, std::string_view v) {
    put_u64(s, v.size());
    s.append(v);
}
inline void put_sub(std::string& s, const TypePoolImpl* impl, TypeRef p) {
    if (!p) { for (int i = 0; i < 32; ++i) s.push_back(0); return; }
    auto uid = impl ? impl->uid_of(p) : LogosType::TypeUID{};
    s.append(reinterpret_cast<const char*>(uid.bytes), 32);
}

LogosType::TypeUID compute_type_uid(const TypePoolImpl* impl,
                                    const LogosTypeBuilder& t) noexcept {
    std::string buf;
    buf.reserve(64);
    put_byte(buf, uint8_t(t.kind));
    using K = LogosType::Kind;
    switch (t.kind) {
    case K::Ptr:
        put_byte(buf, t.mut_ptr ? 1 : 0);
        put_sub(buf, impl, t.pointee);
        break;
    case K::Ref:
    case K::MutRef:
        // lifetime intentionally omitted — matches types_equal semantics.
        put_sub(buf, impl, t.pointee);
        break;
    case K::Array:
        put_u64(buf, t.arr_size);
        put_str(buf, t.arr_size_var);
        put_sub(buf, impl, t.elem);
        break;
    case K::Struct:
    case K::ZonedStruct:
        put_str(buf, t.pkg_name);
        put_str(buf, t.struct_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Enum:
        put_str(buf, t.pkg_name);
        put_str(buf, t.enum_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Tuple:
        for (auto e : t.tuple_elems) put_sub(buf, impl, e);
        break;
    case K::Slice:
        put_sub(buf, impl, t.elem);
        break;
    case K::UnsizedSlice:
        put_sub(buf, impl, t.elem);
        break;
    case K::UnsizedDyn:
        put_str(buf, t.trait_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::DstRef:
        put_str(buf, t.pkg_name);
        put_str(buf, t.struct_name);
        put_byte(buf, t.mut_ptr ? 1 : 0);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Closure:
    case K::FnPtr:
        for (auto p : t.closure_params) put_sub(buf, impl, p);
        put_sub(buf, impl, t.closure_ret);
        break;
    case K::TraitObject:
        put_str(buf, t.trait_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::TaggedPtr:
        put_str(buf, t.trait_name);
        break;
    case K::ImplTrait:
        put_str(buf, t.struct_name);
        break;
    case K::TypeVar:
    case K::ConstVar:
        put_str(buf, t.type_var_name);
        break;
    case K::AssocType:
        put_str(buf, t.trait_name);
        put_str(buf, t.assoc_type_name);
        put_sub(buf, impl, t.assoc_base);
        for (auto a : t.gat_args) put_sub(buf, impl, a);
        break;
    case K::IntLit:
        // const_val distinguishes IntLit instances — the type pool dedupes
        // by UID, so without this two `{integer}` types with different
        // const_val would collapse and lose the value (notably breaks the
        // sizeof-pack array-size path which materialises IntLit(N) via
        // pool->alloc to feed subst_type_sema).
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        break;
    case K::HStaticLit:
        // Identity = the byte-hash stashed in const_val. Without this,
        // two distinct `Foo::<@{...}>` instantiations would dedupe to the
        // same TypeRef and collapse the configuration.
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        break;
    case K::CfgSlotType:
        // Identity = (cfg-typevar-name, slot-key). Reuses type_var_name +
        // assoc_type_name as carrier fields; both must contribute or
        // distinct slots collapse to one interned TypeRef.
        put_str(buf, t.type_var_name);
        put_str(buf, t.assoc_type_name);
        break;
    default:
        break;  // primitives: kind tag alone identifies
    }

    auto sha = sha256(buf);
    LogosType::TypeUID uid{};
    uid.bytes[0] = uint8_t(t.kind);
    std::memcpy(&uid.bytes[1], sha.data(), 23);
    // bytes[24..31] left zero (reserved for future member-id / dispatch).
    return uid;
}

// Hash adapter for unordered_map keying — first 8 bytes of the (already
// well-distributed) SHA-256 trim are sufficient.
struct TypeUIDHash {
    size_t operator()(const LogosType::TypeUID& u) const noexcept {
        size_t h = 0;
        std::memcpy(&h, u.bytes, sizeof(h));
        return h;
    }
};

} // namespace

// 2c.5.2b: byte-strict structural compare between a fresh builder and an
// already-interned TypeRef. Sub-types compared by ptr-equality (interning is
// bottom-up, so all sub-types in `b` are canonical and `t` was constructed
// from canonical sub-types as well). Distinguishes lifetime (borrow_check
// reads .lifetime() off the ptr), Enum type_args, and TypeVar names — fields
// types_equal collapses but consumers read directly.
namespace {

bool vec_ptr_eq(const std::vector<TypeRef>& a,
                const std::vector<TypeRef>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

bool builder_equals_typeref(const LogosTypeBuilder& t, TypeRef r) noexcept {
    if (!r) return false;
    if (t.kind != r.kind()) return false;
    using K = LogosType::Kind;
    switch (t.kind) {
    case K::Ptr:
        return t.mut_ptr == r.mut_ptr() && t.pointee == r.pointee();
    case K::Ref:
    case K::MutRef:
        return t.pointee == r.pointee() &&
               t.lifetime == r.lifetime();
    case K::Array:
        return t.arr_size == r.arr_size() &&
               t.arr_size_var == r.arr_size_var() &&
               t.elem == r.elem();
    case K::Struct:
    case K::ZonedStruct:
        return t.struct_name == r.struct_name() &&
               t.pkg_name == r.pkg_name() &&
               vec_ptr_eq(t.type_args, r.type_args()) &&
               t.lifetime_args == r.lifetime_args();
    case K::Enum:
        return t.enum_name == r.enum_name() &&
               t.pkg_name == r.pkg_name() &&
               vec_ptr_eq(t.type_args, r.type_args()) &&
               t.lifetime_args == r.lifetime_args();
    case K::Tuple:
        return vec_ptr_eq(t.tuple_elems, r.tuple_elems());
    case K::Slice:
    case K::UnsizedSlice:
        return t.elem == r.elem();
    case K::UnsizedDyn:
        return t.trait_name == r.trait_name() &&
               vec_ptr_eq(t.type_args, r.type_args());
    case K::DstRef:
        return t.struct_name == r.struct_name() &&
               t.pkg_name == r.pkg_name() &&
               t.mut_ptr == r.mut_ptr() &&
               vec_ptr_eq(t.type_args, r.type_args());
    case K::Closure:
    case K::FnPtr:
        return vec_ptr_eq(t.closure_params, r.closure_params()) &&
               t.closure_ret == r.closure_ret();
    case K::TraitObject:
        return t.trait_name == r.trait_name() &&
               vec_ptr_eq(t.type_args, r.type_args());
    case K::TaggedPtr:
        return t.trait_name == r.trait_name();
    case K::ImplTrait:
        return t.struct_name == r.struct_name();
    case K::TypeVar:
    case K::ConstVar:
        return t.type_var_name == r.type_var_name() &&
               t.const_val == r.const_val();
    case K::AssocType:
        return t.trait_name == r.trait_name() &&
               t.assoc_type_name == r.assoc_type_name() &&
               t.assoc_base == r.assoc_base() &&
               vec_ptr_eq(t.gat_args, r.gat_args()) &&
               t.lifetime_args == r.lifetime_args();  // B88
    case K::CfgSlotType:
        return t.type_var_name == r.type_var_name() &&
               t.assoc_type_name == r.assoc_type_name();
    default:
        return true;  // primitives — kind alone is identity
    }
}

} // namespace

hermes::Arena* TypePool::arena() noexcept {
    return impl_ ? &impl_->arena() : nullptr;
}
const hermes::Arena* TypePool::arena() const noexcept {
    return impl_ ? &impl_->arena() : nullptr;
}
hermes::Arena& TypePool::arena_or_init() {
    if (!impl_) impl_ = TypePoolImpl::make();
    return impl_->arena();
}
LogosType::TypeUID TypePool::uid_of(TypeRef t) const noexcept {
    return impl_ ? impl_->uid_of(t) : LogosType::TypeUID{};
}

TypeRef TypePool::alloc(LogosTypeBuilder t) {
    if (!impl_) impl_ = TypePoolImpl::make();
    LogosType::TypeUID uid = compute_type_uid(impl_.get(), t);
    auto& bucket = impl_->intern_buckets_[uid];
    for (auto cand_off : bucket) {
        TypeRef cand = impl_->ref(cand_off);
        if (builder_equals_typeref(t, cand)) return cand;
    }

    auto off = impl_->mirror(t);
    impl_->uid_of_[off] = uid;
    bucket.push_back(off);
    return impl_->ref(off);
}

// ── TypeRef pointer-valued accessors (Phase 2c.4d.0) ───────────────────────
//
// These cross-check the mirror's AnyVal pointee offset against the source
// struct field via TypePoolImpl's inverse map. The struct field is still
// what we return; the check confirms the mirror stays consistent under
// real workloads before 2c.4d flips reads to the mirror.
namespace {

// 2c.4e.3.1: accessors source from TypeRef's fat-pointer fields directly.
TypeRef ptr_via_mirror(const TypeRef& self, sema_schema::Key key) {
    if (!self) return {};
    auto av = self.mirror()->get(key.code, self.mirror_base());
    if (av.is_null()) return {};
    return self.pool()->ref(av.to_offset());
}

}  // namespace

TypeRef TypeRef::pointee()     const noexcept { return ptr_via_mirror(*this, sema_schema::POINTEE);     }
TypeRef TypeRef::elem()        const noexcept { return ptr_via_mirror(*this, sema_schema::ELEM);        }
TypeRef TypeRef::assoc_base()  const noexcept { return ptr_via_mirror(*this, sema_schema::ASSOC_BASE);  }
TypeRef TypeRef::closure_ret() const noexcept { return ptr_via_mirror(*this, sema_schema::CLOSURE_RET); }

// String accessors return realloc-safe owning views. The MemHolder is reached
// via pool_; if pool_ is null (synthetic / TypeUID-only TypeRef) the result is
// a null OStringView.
namespace {
hermes::OStringView ostr_via_mirror(const TypeRef& self,
                                    sema_schema::Key key) noexcept {
    if (!self || !self.pool()) return {};
    auto av = self.mirror()->get(key.code, self.mirror_base());
    if (av.is_null()) return {};
    return hermes::OStringView(av.to_offset(), self.pool()->holder());
}
} // namespace

hermes::OStringView TypeRef::lifetime()        const noexcept { return ostr_via_mirror(*this, sema_schema::LIFETIME);        }
hermes::OStringView TypeRef::struct_name()     const noexcept { return ostr_via_mirror(*this, sema_schema::STRUCT_NAME);     }
hermes::OStringView TypeRef::enum_name()       const noexcept { return ostr_via_mirror(*this, sema_schema::ENUM_NAME);       }
hermes::OStringView TypeRef::pkg_name()        const noexcept { return ostr_via_mirror(*this, sema_schema::PKG_NAME);        }
hermes::OStringView TypeRef::trait_name()      const noexcept { return ostr_via_mirror(*this, sema_schema::TRAIT_NAME);      }
hermes::OStringView TypeRef::type_var_name()   const noexcept { return ostr_via_mirror(*this, sema_schema::TYPE_VAR_NAME);   }
hermes::OStringView TypeRef::assoc_type_name() const noexcept { return ostr_via_mirror(*this, sema_schema::ASSOC_TYPE_NAME); }
hermes::OStringView TypeRef::arr_size_var()    const noexcept { return ostr_via_mirror(*this, sema_schema::ARR_SIZE_VAR);    }

// 2c.4e.3.0/.1: vector accessors via mirror ObjectArray, sourced from
// TypeRef's base/off/pool fat pointer.
namespace {
std::vector<TypeRef> type_vec_via_mirror(const TypeRef& self,
                                          sema_schema::Key key) {
    std::vector<TypeRef> result;
    if (!self) return result;
    auto* base = self.mirror_base();
    auto av = self.mirror()->get(key.code, base);
    if (av.is_null()) return result;
    auto* arr = av.as_ptr<const hermes::ObjectArray>(base);
    result.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto e = const_cast<hermes::ObjectArray*>(arr)->get(i, base);
        result.push_back(self.pool()->ref(e.to_offset()));
    }
    return result;
}
std::vector<std::string> string_vec_via_mirror(const TypeRef& self,
                                                sema_schema::Key key) {
    std::vector<std::string> result;
    if (!self) return result;
    auto* base = self.mirror_base();
    auto av = self.mirror()->get(key.code, base);
    if (av.is_null()) return result;
    auto* arr = av.as_ptr<const hermes::ObjectArray>(base);
    result.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto e = const_cast<hermes::ObjectArray*>(arr)->get(i, base);
        auto* s = e.as_ptr<const hermes::ArenaString>(base);
        result.emplace_back(s->view());
    }
    return result;
}
}  // namespace

std::vector<TypeRef> TypeRef::type_args()      const noexcept { return type_vec_via_mirror(*this, sema_schema::TYPE_ARGS); }
std::vector<TypeRef> TypeRef::tuple_elems()    const noexcept { return type_vec_via_mirror(*this, sema_schema::TUPLE_ELEMS); }
std::vector<TypeRef> TypeRef::closure_params() const noexcept { return type_vec_via_mirror(*this, sema_schema::CLOSURE_PARAMS); }
std::vector<TypeRef> TypeRef::gat_args()       const noexcept { return type_vec_via_mirror(*this, sema_schema::GAT_ARGS); }
std::vector<std::string> TypeRef::lifetime_args()  const noexcept { return string_vec_via_mirror(*this, sema_schema::LIFETIME_ARGS); }

// Reconstruct a LogosTypeBuilder from a TypeRef by reading every field
// through the mirror accessors. Callers use this when they want to
// copy-and-mutate an interned type (e.g. mono substitution).
LogosTypeBuilder TypeRef::to_builder() const {
    LogosTypeBuilder b;
    if (!*this) return b;
    b.kind            = kind();
    b.mut_ptr         = mut_ptr();
    b.pointee         = pointee();
    b.lifetime        = std::string(lifetime());
    b.elem            = elem();
    b.arr_size        = arr_size();
    b.arr_size_var    = std::string(arr_size_var());
    b.struct_name     = std::string(struct_name());
    b.enum_name       = std::string(enum_name());
    b.pkg_name        = std::string(pkg_name());
    b.type_args       = type_args();
    b.lifetime_args   = lifetime_args();
    b.tuple_elems     = tuple_elems();
    b.closure_params  = closure_params();
    b.closure_ret     = closure_ret();
    b.trait_name      = std::string(trait_name());
    b.type_var_name   = std::string(type_var_name());
    b.assoc_base      = assoc_base();
    b.assoc_type_name = std::string(assoc_type_name());
    b.gat_args        = gat_args();
    b.const_val       = const_val();
    return b;
}

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// ── types_equal ─────────────────────────────────────────────────────────────

// 2c.5.3: post-interning, types_equal collapses to ptr-or-hash compare.
//
// Invariant after 2c.5.2b: every LogosType* comes from the interned pool;
// two distinct ptrs are byte-strict different. Hash buckets can only hold
// distinct ptrs that differ in fields types_equal ignores (lifetime, pkg_name,
// lifetime_args, const_val). Therefore within the pool:
//   hash_eq ⇒ types_equal   (and ptr_eq trivially ⇒ types_equal).
//
// Sub-types are interned bottom-up so sub-hash equality ⇒ sub-types-equal,
// closing the inductive step for Closure/FnPtr/Ref/etc.
bool types_equal(TypeRef a, TypeRef b) noexcept {
    if (!a || !b) return false;
    if (a == b) return true;
    auto* pa = a.pool();
    auto* pb = b.pool();
    if (!pa || pa != pb) return false;
    return pa->uid_of(a) == pa->uid_of(b);
}

// ── Generic struct name helpers ───────────────────────────────────────────────

static std::string mangle_type_for_name(TypeRef t);

std::string concrete_struct_name(TypeRef t) {
    if (!t || (TypeRef(t).kind() != LogosType::Kind::Struct &&
               TypeRef(t).kind() != LogosType::Kind::ZonedStruct)) return {};
    std::string base(TypeRef(t).struct_name());
    if (!TypeRef(t).type_args().empty()) {
        base += "$G";
        base += std::to_string(TypeRef(t).type_args().size());
        for (auto a : TypeRef(t).type_args()) { base += "$"; base += mangle_type_for_name(a); }
    }
    return base;
}

std::string concrete_struct_name_raw(std::string_view base_name,
                                     const std::vector<TypeRef>& type_args) {
    if (type_args.empty()) return std::string(base_name);
    std::string r(base_name);
    r += "$G";
    r += std::to_string(type_args.size());
    for (auto a : type_args) { r += "$"; r += mangle_type_for_name(a); }
    return r;
}

static std::string mangle_type_for_name(TypeRef t) {
    if (!t) return "null";
    switch (TypeRef(t).kind()) {
    case LogosType::Kind::Ptr:
        return (TypeRef(t).mut_ptr() ? "pmut_" : "pcst_") + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::Ref:
        return "ref_" + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::MutRef:
        return "refmut_" + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::Array:
        return "arr" + std::to_string(TypeRef(t).arr_size()) + "_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        return concrete_struct_name(t);
    case LogosType::Kind::Tuple: {
        std::string r = "tup$" + std::to_string(TypeRef(t).tuple_elems().size());
        for (auto e : TypeRef(t).tuple_elems()) { r += "$"; r += mangle_type_for_name(e); }
        return r;
    }
    case LogosType::Kind::Slice:
        return "slice_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::UnsizedSlice:
        return "uslice_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::UnsizedDyn:
        return "udyn_" + std::string(TypeRef(t).trait_name());
    case LogosType::Kind::DstRef:
        return (TypeRef(t).mut_ptr() ? "dstmutref_" : "dstref_") +
               std::string(TypeRef(t).struct_name());
    case LogosType::Kind::AssocType:
        return mangle_type_for_name(TypeRef(t).assoc_base()) + "::" + std::string(TypeRef(t).assoc_type_name());
    case LogosType::Kind::HStaticLit: {
        // hs_<hex64>: identity-stable suffix for HermesStatic value used as
        // const-generic argument. Two `@{...}` literals with identical bytes
        // hash to the same const_val and therefore the same suffix.
        char buf[24];
        std::snprintf(buf, sizeof(buf), "hs_%016llx",
                      (unsigned long long)(uint64_t)(TypeRef(t).const_val().value_or(0)));
        return std::string(buf);
    }
    default:
        return type_str(t);  // primitives / TypeVar / Enum already valid identifiers
    }
}

std::string SemaChecker::canonical_func_type_name(TypeRef t) const {
    return mangle_type_for_name(t);
}

std::string SemaChecker::function_signature_key(std::string_view base_name,
                                                const std::vector<TypeRef>& param_types,
                                                bool is_vararg) const {
    std::string key(base_name);
    for (auto pt : param_types) {
        key += "__";
        key += canonical_func_type_name(pt);
    }
    if (param_types.empty()) key += "__void";
    if (is_vararg) key += "__vararg";
    return key;
}

std::string SemaChecker::function_symbol_name(std::string_view base_name,
                                             const SemaChecker::SemaFuncInfo& info) const {
    // Pkg-qualified mangling: `pkg$base__f__sig` (or `__g__` for generic).
    // Two packages defining `error(msg: str)` get distinct symbols
    // (`std.log$error__f__str` vs `std.compiler.metaprog$error__f__str`)
    // so they coexist in funcs_, in user `.o`, and at link time.
    //
    // Carve-outs that stay bare:
    //   - extern fns: ABI symbols (malloc, printf) must keep their C name.
    //   - struct methods (base_name contains `__`): already disambiguated
    //     by their struct's pkg-qualified name in mlir_gen.
    bool is_method = base_name.find("__") != std::string_view::npos;
    bool with_pkg  = !info.package.empty() && !info.is_extern;

    std::string key = function_signature_key(base_name, info.param_types, info.is_vararg);
    auto suffix = key.substr(std::string(base_name).size() + 2);
    std::string out;
    if (with_pkg) {
        out = info.package;
        out += is_method ? '.' : '$';
    }
    out += std::string(base_name);
    out += info.type_params.empty() ? "__f__" : "__g__";
    out += suffix;
    return out;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_symbol(std::string_view symbol) const {
    if (auto it = funcs_.find(std::string(symbol)); it != funcs_.end())
        return &it->second;
    if (auto it = generic_funcs_.find(std::string(symbol)); it != generic_funcs_.end())
        return &it->second;
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name) const {
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end())
                return &fit->second;
        }
    }
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name,
                                                                size_t n_args) const {
    const SemaFuncInfo* fallback = nullptr;
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit == generic_funcs_.end()) continue;
            auto& fi = fit->second;
            if (fi.is_vararg) {
                if (n_args >= fi.param_types.size()) return &fi;
            } else if (fi.param_types.size() == n_args) {
                return &fi;
            } else if (!fallback) {
                fallback = &fi;
            }
        }
    }
    return fallback;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_base_and_signature(
        std::string_view base_name,
        const std::vector<TypeRef>& param_types,
        bool is_vararg) const {
    for (auto* fi : find_func_candidates(base_name)) {
        if (fi->is_vararg != is_vararg) continue;
        if (fi->param_types.size() != param_types.size()) continue;
        bool same = true;
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (!fi->param_types[i] || !param_types[i] ||
                !types_equal(fi->param_types[i], param_types[i])) {
                same = false; break;
            }
        }
        if (same) return fi;
    }
    return nullptr;
}

std::vector<const SemaChecker::SemaFuncInfo*> SemaChecker::find_func_candidates(std::string_view base_name) const {
    std::vector<const SemaChecker::SemaFuncInfo*> all;
    if (auto it = func_overloads_.find(std::string(base_name)); it != func_overloads_.end()) {
        for (auto& sym : it->second) {
            auto fit = funcs_.find(sym);
            if (fit != funcs_.end()) all.push_back(&fit->second);
        }
    } else {
        auto fit = funcs_.find(std::string(base_name));
        if (fit != funcs_.end() && fit->second.source_file.size())
            all.push_back(&fit->second);
    }
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end()) all.push_back(&fit->second);
        }
    }
    // Visibility filter: under pkg-qualified mangling two packages can
    // define the same base+sig fn. The user's call site sees only fns
    // reachable through cur_package_ (own pkg) or cur_imports_ (use
    // pkg;). Empty pkg (extern fns / prelude) stay visible.
    // Fallback: if filtering would leave nothing, return everything —
    // sema-internal lookups during synthetic phases (metaprog stubs,
    // mono pre-image) may run before cur_imports_ is primed.
    std::vector<const SemaChecker::SemaFuncInfo*> out;
    out.reserve(all.size());
    for (auto* fi : all) {
        if (fi->package.empty() ||
            fi->package == cur_package_ ||
            std::find(cur_imports_.wildcard_packages.begin(),
                      cur_imports_.wildcard_packages.end(),
                      fi->package) != cur_imports_.wildcard_packages.end()) {
            out.push_back(fi);
        }
    }
    if (out.empty()) return all;
    return out;
}

const SemaChecker::SemaFuncInfo* SemaChecker::resolve_function_call(
        std::string_view base_name,
        const std::vector<lir::LExprPtr>& arg_exprs,
        bool allow_generic,
        bool exact_only) const {
    const SemaChecker::SemaFuncInfo* best = nullptr;
    int best_score = -1;
    bool ambiguous = false;

    auto candidates = find_func_candidates(base_name);
    for (auto* fi : candidates) {
        if (!fi || fi->type_params.size() || fi->source_file.empty()) continue;
        bool arity_ok = fi->is_vararg ? arg_exprs.size() >= fi->param_types.size()
                                      : arg_exprs.size() == fi->param_types.size();
        if (!arity_ok) continue;

        int score = 0;
        bool ok = true;
        for (size_t i = 0; i < fi->param_types.size(); ++i) {
            auto at = arg_exprs[i] ? arg_exprs[i]->type : nullptr;
            auto pt = fi->param_types[i];
            if (!at || !pt) { ok = false; break; }
            if (types_equal(at, pt)) score = std::max(score, 2);
            else if (!exact_only && types_compatible(at, pt)) score = std::max(score, 1);
            else { ok = false; break; }
        }
        if (!ok) continue;
        if (score > best_score) {
            best = fi;
            best_score = score;
            ambiguous = false;
        } else if (score == best_score && best_score != -1) {
            ambiguous = true;
        }
    }

    if (ambiguous) {
        // Tiebreaker: prefer a candidate whose package matches cur_package_.
        // Local definition shadows imported same-named pub fn (Rust/C++ rule).
        const SemaFuncInfo* local = nullptr;
        bool local_ambiguous = false;
        for (auto* fi : candidates) {
            if (!fi || fi->type_params.size() || fi->source_file.empty()) continue;
            if (fi->package != cur_package_) continue;
            bool arity_ok = fi->is_vararg ? arg_exprs.size() >= fi->param_types.size()
                                          : arg_exprs.size() == fi->param_types.size();
            if (!arity_ok) continue;
            int score = 0;
            bool ok = true;
            for (size_t i = 0; i < fi->param_types.size(); ++i) {
                auto at = arg_exprs[i] ? arg_exprs[i]->type : nullptr;
                auto pt = fi->param_types[i];
                if (!at || !pt) { ok = false; break; }
                if (types_equal(at, pt)) score = std::max(score, 2);
                else if (!exact_only && types_compatible(at, pt)) score = std::max(score, 1);
                else { ok = false; break; }
            }
            if (!ok) continue;
            if (score == best_score) {
                if (local) local_ambiguous = true;
                local = fi;
            }
        }
        if (local && !local_ambiguous) return local;
        const_cast<SemaChecker*>(this)->error(std::format("ambiguous call to '{}'", base_name));
        return nullptr;
    }
    if (best || !allow_generic) return best;
    return nullptr;
}

// ── types_compatible ─────────────────────────────────────────────────────────

bool types_compatible(TypeRef from, TypeRef to) noexcept {
    if (!from || !to) return false;
    if (types_equal(from, to)) return true;
    if (from.kind() == LogosType::Kind::IntLit && is_integer_kind(to.kind())) return true;
    if (from.kind() == LogosType::Kind::IntLit && to.kind() == LogosType::Kind::TypeVar) return true;
    if (from.kind() == LogosType::Kind::IntLit &&
        (to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64)) return true;
    if (from.kind() == LogosType::Kind::FloatLit &&
        (to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64 ||
         to.kind() == LogosType::Kind::TypeVar)) return true;
    // Cfg-slot types are deferred placeholders for HermesStatic-bound
    // primitives. Treat them like TypeVar at sema for coercion checks:
    // any concrete numeric (and IntLit/FloatLit) compatible-with the
    // resolved primitive — mono enforces the resolved-type compatibility
    // when STORE_CFG is substituted. Flowing in BOTH directions so both
    // `slot_typed = 0u64` and `u64_typed = slot_value` checks pass.
    if (from.kind() == LogosType::Kind::IntLit && to.kind() == LogosType::Kind::CfgSlotType) return true;
    if (from.kind() == LogosType::Kind::FloatLit && to.kind() == LogosType::Kind::CfgSlotType) return true;
    if (from.kind() == LogosType::Kind::CfgSlotType || to.kind() == LogosType::Kind::CfgSlotType) {
        if (is_integer_kind(from.kind()) || is_integer_kind(to.kind())) return true;
        if (from.kind() == LogosType::Kind::F32 || from.kind() == LogosType::Kind::F64 ||
            to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64) return true;
    }
    if (from.kind() == LogosType::Kind::Enum   && is_integer_kind(to.kind())) return true;
    if (is_integer_kind(from.kind()) && to.kind() == LogosType::Kind::Enum)   return true;
    // Safe implicit integer widening (e.g. u32 → i64, i32 → i64, u8 → u32).
    // Value preservation guaranteed; signed→unsigned never allowed here.
    if (can_widen_int(from.kind(), to.kind())) return true;
    if (from.kind() == LogosType::Kind::Array &&
        to.kind() == LogosType::Kind::Ptr   &&
        from.elem() && to.pointee())
        return types_equal(from.elem(), to.pointee());
    // Arrays are compatible if same size and elements are compatible (handles nested arrays).
    if (TypeRef(from).kind() == LogosType::Kind::Array && TypeRef(to).kind() == LogosType::Kind::Array &&
        TypeRef(from).arr_size() == TypeRef(to).arr_size() && TypeRef(from).elem() && TypeRef(to).elem())
        return types_compatible(TypeRef(from).elem(), TypeRef(to).elem());
    // Tuple: element-wise compatibility (e.g. ({integer}, {integer}) → (i32, i32))
    if (TypeRef(from).kind() == LogosType::Kind::Tuple && TypeRef(to).kind() == LogosType::Kind::Tuple) {
        if (TypeRef(from).tuple_elems().size() != TypeRef(to).tuple_elems().size()) return false;
        for (size_t i = 0; i < TypeRef(from).tuple_elems().size(); ++i)
            if (!types_compatible(TypeRef(from).tuple_elems()[i], TypeRef(to).tuple_elems()[i])) return false;
        return true;
    }
    // C5-cl-04 slice: `&Closure → Closure` / `&mut Closure → Closure`. Users
    // typically spell the boxable-closure surface as `&dyn FnMut(…)`, which
    // sema resolves to bare Closure (since `dyn Fn*` IS already fat-ptr-like
    // — the `&` carries no extra meaning). To make `take_ref(&cl)` type-check
    // against `f: &dyn FnMut(…)` (parsed as `Closure`), accept a reference
    // over a Closure as a Closure value.
    if (TypeRef(to).kind() == LogosType::Kind::Closure &&
        (TypeRef(from).kind() == LogosType::Kind::Ref ||
         TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(from).pointee() &&
        TypeRef(from).pointee().kind() == LogosType::Kind::Closure)
        return types_compatible(TypeRef(from).pointee(), to);
    // Struct → &dyn Trait coercion (impl check deferred to codegen).
    // Also accept `&T` / `&mut T` over a struct (the natural unsize-coercion
    // source form): `foo(&b)` where `foo` expects `&dyn Trait` and `b: T`.
    if (TypeRef(to).kind() == LogosType::Kind::TraitObject &&
        (TypeRef(from).kind() == LogosType::Kind::Struct ||
         (TypeRef(from).kind() == LogosType::Kind::Ptr && TypeRef(from).pointee()) ||
         ((TypeRef(from).kind() == LogosType::Kind::Ref ||
           TypeRef(from).kind() == LogosType::Kind::MutRef) &&
          TypeRef(from).pointee() &&
          TypeRef(from).pointee().kind() == LogosType::Kind::Struct)))
        return true;
    // &T / &mut T → *const T / *mut T coercions (for backward compat with existing raw-ptr code)
    if ((TypeRef(from).kind() == LogosType::Kind::Ref || TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(to).kind() == LogosType::Kind::Ptr &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *const T / *mut T → &T (reverse coercion — less safe but needed for existing code)
    if (TypeRef(from).kind() == LogosType::Kind::Ptr &&
        (TypeRef(to).kind() == LogosType::Kind::Ref || TypeRef(to).kind() == LogosType::Kind::MutRef) &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // &mut T → &T coercion (shared ref from exclusive ref)
    if (TypeRef(from).kind() == LogosType::Kind::MutRef && TypeRef(to).kind() == LogosType::Kind::Ref &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // B3-bg-06: `&Vec<T> → &[T]` / `&mut Vec<T> → &[T]` Deref-like coercion.
    // In Logos `&[T]` is the Slice fat-pointer type itself (NOT Ref<Slice>),
    // so the from side is `Ref<Vec<T>>` / `MutRef<Vec<T>>` and the to side
    // is `Slice<T>`. Vec's `{ptr, len, cap}` layout has `{ptr, len}` (the
    // slice fat-pointer) as a prefix, so at the LLVM level the pointer is
    // reused verbatim — no runtime conversion needed. Hardcoded to the
    // stdlib Vec struct; full `Deref` trait surface is the longer path.
    if ((TypeRef(from).kind() == LogosType::Kind::Ref || TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(to).kind() == LogosType::Kind::Slice &&
        TypeRef(from).pointee() &&
        TypeRef(from).pointee().kind() == LogosType::Kind::Struct &&
        TypeRef(from).pointee().struct_name() == "Vec" &&
        !TypeRef(from).pointee().type_args().empty() &&
        TypeRef(to).elem())
        return types_compatible(TypeRef(from).pointee().type_args()[0],
                                TypeRef(to).elem());
    // &T → &T and &mut T → &mut T with compatible pointees (e.g. &{integer} → &i32)
    if (TypeRef(from).kind() == LogosType::Kind::Ref && TypeRef(to).kind() == LogosType::Kind::Ref &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    if (TypeRef(from).kind() == LogosType::Kind::MutRef && TypeRef(to).kind() == LogosType::Kind::MutRef &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *mut T → *const T coercion (dropping write permission is always safe).
    if (TypeRef(from).kind() == LogosType::Kind::Ptr && TypeRef(to).kind() == LogosType::Kind::Ptr &&
        TypeRef(from).mut_ptr() && !TypeRef(to).mut_ptr() &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *const u8 (or any *T) → &tagged<TS> Trait coercion.
    // &tagged<TS> Trait is a thin pointer to a tagged object.  The caller passes
    // a raw *const u8 and the compiler reads the tag at dispatch time.
    if (TypeRef(to).kind() == LogosType::Kind::TaggedPtr && TypeRef(from).kind() == LogosType::Kind::Ptr)
        return true;
    // Class pointer covariance: *mut/const Derived is compatible with *const/mut Class
    // (same class — exact equality handled above; hierarchy checked in SemaChecker::compat)
    return false;
}

// ── type_str ─────────────────────────────────────────────────────────────────

std::string type_str(TypeRef t) {
    if (!t) return "<null>";
    switch (TypeRef(t).kind()) {
    case LogosType::Kind::Void:   return "void";
    case LogosType::Kind::I32:    return "i32";
    case LogosType::Kind::I64:    return "i64";
    case LogosType::Kind::F64:    return "f64";
    case LogosType::Kind::F32:    return "f32";
    case LogosType::Kind::Bool:   return "bool";
    case LogosType::Kind::U8:     return "u8";
    case LogosType::Kind::I8:     return "i8";
    case LogosType::Kind::I16:    return "i16";
    case LogosType::Kind::U16:    return "u16";
    case LogosType::Kind::I24:    return "i24";
    case LogosType::Kind::U24:    return "u24";
    case LogosType::Kind::I56:    return "i56";
    case LogosType::Kind::U56:    return "u56";
    case LogosType::Kind::U32:    return "u32";
    case LogosType::Kind::U64:    return "u64";
    case LogosType::Kind::I128:   return "i128";
    case LogosType::Kind::U128:   return "u128";
    case LogosType::Kind::Usize:  return "usize";
    case LogosType::Kind::Isize:  return "isize";
    case LogosType::Kind::Char:   return "char";
    case LogosType::Kind::IntLit:   return "{integer}";
    case LogosType::Kind::HStaticLit: {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "@hs_%016llx",
                      (unsigned long long)(uint64_t)(TypeRef(t).const_val().value_or(0)));
        return std::string(buf);
    }
    case LogosType::Kind::FloatLit: return "{float}";
    case LogosType::Kind::Ptr:
        return std::string(TypeRef(t).mut_ptr() ? "*mut " : "*const ") + type_str(TypeRef(t).pointee());
    case LogosType::Kind::Ref: {
        std::string s = "&";
        if (!TypeRef(t).lifetime().empty()) { s.append(TypeRef(t).lifetime()); s += " "; }
        return s + type_str(TypeRef(t).pointee());
    }
    case LogosType::Kind::MutRef: {
        std::string s = "&";
        if (!TypeRef(t).lifetime().empty()) { s.append(TypeRef(t).lifetime()); s += " "; }
        return s + "mut " + type_str(TypeRef(t).pointee());
    }
    case LogosType::Kind::Array:
        return std::format("[{}; {}]", type_str(TypeRef(t).elem()), TypeRef(t).arr_size());
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        if (TypeRef(t).type_args().empty() && TypeRef(t).lifetime_args().empty()) return std::string(TypeRef(t).struct_name());
        { std::string r = std::string(TypeRef(t).struct_name()) + "<";
          bool first = true;
          for (auto& lt : TypeRef(t).lifetime_args()) {
              if (!first) r += ", "; first = false;
              r += lt;
          }
          for (size_t i = 0; i < TypeRef(t).type_args().size(); ++i) {
              if (!first) r += ", "; first = false;
              r += type_str(TypeRef(t).type_args()[i]);
          }
          return r + ">"; }
    case LogosType::Kind::Tuple: {
        std::string r = "(";
        for (size_t i = 0; i < TypeRef(t).tuple_elems().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).tuple_elems()[i]);
        }
        return r + ")"; }
    case LogosType::Kind::Slice:
        return std::format("&[{}]", type_str(TypeRef(t).elem()));
    case LogosType::Kind::UnsizedSlice:
        return std::format("[{}]", type_str(TypeRef(t).elem()));
    case LogosType::Kind::UnsizedDyn:
        return std::format("dyn {}", TypeRef(t).trait_name());
    case LogosType::Kind::DstRef: {
        std::string s = TypeRef(t).mut_ptr() ? "&mut " : "&";
        s += TypeRef(t).struct_name();
        auto args = TypeRef(t).type_args();
        if (!args.empty()) {
            s += "<";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) s += ", ";
                s += type_str(args[i]);
            }
            s += ">";
        }
        return s;
    }
    case LogosType::Kind::Closure: {
        std::string r = "|";
        for (size_t i = 0; i < TypeRef(t).closure_params().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).closure_params()[i]);
        }
        r += "| -> ";
        r += type_str(TypeRef(t).closure_ret());
        return r; }
    case LogosType::Kind::FnPtr: {
        std::string r = "fn(";
        for (size_t i = 0; i < TypeRef(t).closure_params().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).closure_params()[i]);
        }
        r += ") -> ";
        r += type_str(TypeRef(t).closure_ret());
        return r; }
    case LogosType::Kind::Enum:        return std::string(TypeRef(t).enum_name());
    case LogosType::Kind::TraitObject: {
        std::string r = "&dyn " + std::string(TypeRef(t).trait_name());
        auto ta = TypeRef(t).type_args();
        if (!ta.empty()) {
            r += "<";
            for (size_t i = 0; i < ta.size(); ++i) {
                if (i) r += ", ";
                r += type_str(ta[i]);
            }
            r += ">";
        }
        return r; }
    case LogosType::Kind::TaggedPtr:   return "&tagged<" + std::string(TypeRef(t).struct_name()) + "> " + std::string(TypeRef(t).trait_name());
    case LogosType::Kind::TypeVar:     return std::string(TypeRef(t).type_var_name());
    case LogosType::Kind::ConstVar:    return std::string(TypeRef(t).type_var_name());
    case LogosType::Kind::AssocType: {
        std::string r = type_str(TypeRef(t).assoc_base()) + "::" + std::string(TypeRef(t).assoc_type_name());
        if (!TypeRef(t).gat_args().empty()) {
            r += "<";
            for (size_t i = 0; i < TypeRef(t).gat_args().size(); ++i) {
                if (i) r += ", ";
                r += type_str(TypeRef(t).gat_args()[i]);
            }
            r += ">";
        }
        return r;
    }
    case LogosType::Kind::ImplTrait:   return "impl " + std::string(TypeRef(t).struct_name());
    case LogosType::Kind::Generic:     return "generic " + std::string(TypeRef(t).struct_name());
    case LogosType::Kind::CfgSlotType: return "<cfg-slot-type>";
    case LogosType::Kind::Error:       return "<error>";
    }
    return "<unknown>";
}

// ── SemaChecker method definitions ───────────────────────────────────────────

lir::LProgram SemaChecker::run(const std::vector<hermes::Hermes>& asts,
                                const std::vector<std::string>& filenames,
                                const std::vector<bool>& from_binary) {
    filenames_ = &filenames;
    from_binary_ = from_binary.empty() ? nullptr : &from_binary;

    lir::LProgram prog;
    pool_ = &prog.type_pool;  // bind so all alloc()s share prog's arena
    // Set cur_prog_ before `collect` so LIT_HSTATIC encountered inside
    // type-alias rhs / supertrait bounds / etc. can register into
    // prog.hstatic_registry_ during collection. Otherwise alias-routed
    // HermesStatic const-args produce TypeRefs whose hash is in
    // const_val but whose literal never lands in the registry, and
    // mono later fails to materialise `__const_param:CFG`.
    cur_prog_ = &prog;

    init_primitives();
    phase_ = SemaPhase::Collect;
    collect(asts);

    if (!result_.ok()) {
        prog.diags = std::move(result_);
        return prog;
    }

    phase_ = SemaPhase::Lower;
    lower_program(asts, prog);

    // Enforce the "one eidos per (genos, tag-system)" invariant at compile
    // time.  Two different impl targets that end up with the same
    // (tag_system, type_code) pair would overwrite each other in the
    // dispatch table (and trigger a link-time collision via the sentinel
    // globals in mlir_gen_dyn.cpp).  Surface a clearer diagnostic here.
    {
        StrMap<std::string> seen;
        for (const auto& de : prog.dispatch_entries) {
            if (de.type_code == 0) continue;
            auto key = de.tag_system + "#" + std::to_string(de.type_code);
            auto [it, inserted] = seen.emplace(std::move(key), de.impl_type_name);
            if (!inserted && it->second != de.impl_type_name) {
                ctx_.clear();
                error(std::format(
                    "two eide register for genos type_code {} in tag system '{}': "
                    "'{}' and '{}' — only one eidos per (genos, tag-system) is allowed",
                    de.type_code, de.tag_system,
                    it->second, de.impl_type_name));
            }
        }
    }

    // Phase 7 slice 12: validate `#[metaprog_handler(...)]` registrations.
    // Hook fn must be a non-extern, non-generic free fn taking a single
    // `*const u8` parameter (target node ptr) and returning ().
    // Trigger names must be unique across the program — collisions would
    // make handler dispatch ambiguous.
    {
        for (const auto& mh : metaprog_handlers_) {
            ctx_ = std::format("fn {}", mh.hook_fn);
            node_line_ = 0;
            if (mh.trigger == "<missing>") {
                error("#[metaprog_handler] requires a string-literal trigger name, e.g. #[metaprog_handler(\"derive_debug\")]");
                continue;
            }
            // Phase 7 slice 14: multiple handlers per trigger are allowed —
            // all fire on each match in source-declaration order. No
            // dedup of the (trigger, hook_fn) pair: registering the same
            // fn twice would call it twice, which is the user's bug.
            // mh.hook_fn was captured as the bare AST name; the actual
            // emitted symbol may carry `pkg$base__f__sig` mangling.
            const lir::LFunction* fn = nullptr;
            for (const auto& f : prog.functions)
                if (bare_fn_name(f->name) == mh.hook_fn) { fn = f.get(); break; }
            if (!fn) { error("#[metaprog_handler] not a free fn"); continue; }
            if (fn->is_extern)
                error("#[metaprog_handler] cannot be applied to extern fn");
            if (!fn->type_params.empty())
                error("#[metaprog_handler] hook must not be generic");
            if (fn->params.size() != 1) {
                error("#[metaprog_handler] hook must take exactly one parameter (target_offset: u32)");
                continue;
            }
            // Param is the AnyVal-style offset of the triggered item
            // within the module's Hermes doc. Hooks reconstruct the
            // node via AnyVal::from_offset(target_offset) + existing
            // HermesView/OView API.
            auto pt = TypeRef(fn->params[0].type);
            if (pt.kind() != LogosType::Kind::U32)
                error("#[metaprog_handler] hook param must be u32 (offset of triggered item)");
            if (fn->ret_type && TypeRef(fn->ret_type).kind() != LogosType::Kind::Void)
                error("#[metaprog_handler] hook must return ()");
        }
    }

    // B-gn-05: every specialisation `fn helper<Foo>` needs a generic `fn
    // helper<T>` to specialise on; otherwise the def silently disappears
    // (lowered into prog.specializations, never resolved at call sites)
    // and the user gets a misleading "undefined function 'helper'".
    {
        std::set<std::string> generic_bases;
        for (auto& f : prog.functions) {
            if (!f->type_params.empty())
                generic_bases.insert(std::string(bare_fn_name(f->name)));
        }
        for (auto& s : prog.specializations) {
            // s->name is the bare base (e.g. "helper" / "describe").
            if (!generic_bases.count(std::string(bare_fn_name(s->name)))) {
                ctx_  = std::format("fn {}", s->name);
                file_ = s->source_file;
                node_line_ = 0;
                error(std::format(
                    "specialisation 'fn {}<...>' has no generic counterpart "
                    "'fn {}<T>' to specialise on. If you meant a regular "
                    "free fn, rename the type parameter so it doesn't shadow "
                    "an existing type.",
                    s->name, s->name));
            }
        }
    }

    prog.diags      = std::move(result_);
    prog.metaprog_handlers = std::move(metaprog_handlers_);
    prog.metaprog_targets = std::move(metaprog_targets_);

    // Stage 3g.1: populate prog.mirror_table after lowering completes. With
    // pool_ bound to prog.type_pool throughout sema, mirror offsets and
    // TypeRef offsets share the same arena from the start.
    lir_mirror_emit_into(prog, *prog.mirror_table);

    return prog;
}

void SemaChecker::init_primitives() {
    auto ap = [&](LogosType::Kind k) {
        LogosTypeBuilder t; t.kind = k;
        prims_[int(k)] = pool_->alloc(t);
    };
    ap(LogosType::Kind::Void);
    ap(LogosType::Kind::I32);
    ap(LogosType::Kind::I64);
    ap(LogosType::Kind::F64);
    ap(LogosType::Kind::F32);
    ap(LogosType::Kind::Bool);
    ap(LogosType::Kind::U8);
    ap(LogosType::Kind::I8);
    ap(LogosType::Kind::I16);
    ap(LogosType::Kind::U16);
    ap(LogosType::Kind::U32);
    ap(LogosType::Kind::U64);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::I56);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::U56);
    ap(LogosType::Kind::I128);
    ap(LogosType::Kind::U128);
    ap(LogosType::Kind::Usize);
    ap(LogosType::Kind::Isize);
    ap(LogosType::Kind::Char);
    ap(LogosType::Kind::IntLit);
    ap(LogosType::Kind::FloatLit);
    ap(LogosType::Kind::Error);
}

TypeRef SemaChecker::lookup_type_by_name(std::string_view name) {
    if (name == "i32")  return prim(LogosType::Kind::I32);
    if (name == "i64")  return prim(LogosType::Kind::I64);
    if (name == "f64")  return prim(LogosType::Kind::F64);
    if (name == "f32")  return prim(LogosType::Kind::F32);
    if (name == "bool") return prim(LogosType::Kind::Bool);
    if (name == "u8")   return prim(LogosType::Kind::U8);
    if (name == "i8")   return prim(LogosType::Kind::I8);
    if (name == "i16")  return prim(LogosType::Kind::I16);
    if (name == "u16")  return prim(LogosType::Kind::U16);
    if (name == "u32")  return prim(LogosType::Kind::U32);
    if (name == "u64")  return prim(LogosType::Kind::U64);
    if (name == "i24")  return prim(LogosType::Kind::I24);
    if (name == "u24")  return prim(LogosType::Kind::U24);
    if (name == "i56")  return prim(LogosType::Kind::I56);
    if (name == "u56")  return prim(LogosType::Kind::U56);
    if (name == "i128") return prim(LogosType::Kind::I128);
    if (name == "u128") return prim(LogosType::Kind::U128);
    if (name == "usize") return prim(LogosType::Kind::Usize);
    if (name == "isize") return prim(LogosType::Kind::Isize);
    if (name == "char")  return prim(LogosType::Kind::Char);
    if (name == "void") return prim(LogosType::Kind::Void);
    if (name == "str") {
        // Phase 1B-3: `str` keyword. Default meaning is the existing
        // fat-pointer form Slice<u8> (Rust's `&str` shape). When the
        // surrounding context explicitly permits an unsized result
        // (e.g. turbofish for a `T: ?Sized` parameter), produce the
        // unsized form so the substitution canonicalisation can route
        // `&T` to the same Slice<u8> ABI without double-wrapping.
        if (unsized_ok_) return make_unsized_slice_type(u8_t());
        return make_slice_type(u8_t());
    }
    auto tvit = current_type_params_.find(std::string(name));
    if (tvit != current_type_params_.end()) return tvit->second;
    // Type alias: check current package and imports too
    {
        auto ukey = std::string(name);
        auto check_alias = [&](const std::string& key) -> TypeRef {
            auto it = type_aliases_.find(key);
            if (it != type_aliases_.end() &&
                it->second.type_params.empty() && it->second.lifetime_params.empty())
                return it->second.type;
            return nullptr;
        };
        if (auto t = check_alias(ukey)) return t;
        if (!cur_package_.empty()) if (auto t = check_alias(sema_key(cur_package_, ukey))) return t;
        for (auto& pkg : cur_imports_.wildcard_packages)
            if (auto t = check_alias(sema_key(pkg, ukey))) return t;
    }
    {
        auto [spkg, ssi] = find_struct_by_name(name);
        if (ssi) return make_struct_type(name, spkg);
    }
    {
        auto [dpkg, dsi] = find_datatype_by_name(name);
        if (dsi) return make_datatype_type(name, dpkg);
    }
    {
        auto [epkg, esi] = find_enum_by_name(name);
        if (esi) return make_enum_type(name, epkg);
    }
    return nullptr;
}

// ── Drop/move helpers ────────────────────────────────────────────────────────

bool SemaChecker::is_move_type(TypeRef t) const {
    if (!t) return false;
    // TypeVar — generic body. We don't know if T resolves to a Copy or
    // move type at sema; treat as move (conservative). If T resolves to
    // Copy at mono, the suppressed scope-exit drop is harmless (Copy
    // types have no Drop). If T resolves to a move-type, the
    // suppression is necessary to avoid double-free across slots that
    // bitwise-share the value (Vec.push / Vec.remove / let val: T =
    // *ptr / etc.). Cross-arm move pollution that this could cause is
    // handled by lower_match / lower_if's per-arm save/restore.
    if (TypeRef(t).kind() == LogosType::Kind::TypeVar) return true;
    // Struct types are move types unless they implement Copy.
    if (TypeRef(t).kind() != LogosType::Kind::Struct)
        return false;
    return !copy_types_.count(std::string(TypeRef(t).struct_name()));
}

std::string SemaChecker::drop_fn_for(TypeRef t) const {
    if (!t) return {};
    // TypeVar — a generic param. Whether the substituted concrete type has a
    // Drop impl is unknown at sema; emit a deferred drop stmt with a sentinel
    // drop_fn that mono's SDrop case rewrites (or removes) after substitution.
    if (TypeRef(t).kind() == LogosType::Kind::TypeVar) return "__typevar_pending__drop";
    std::string type_name;
    if (TypeRef(t).kind() == LogosType::Kind::Struct) type_name = std::string(TypeRef(t).struct_name());
    if (type_name.empty()) return {};
    std::string mangled = type_name + "__drop";
    std::vector<TypeRef> sig{t};
    if (auto* fi = find_func_by_base_and_signature(mangled, sig, false))
        return fi->symbol_name.empty() ? mangled : fi->symbol_name;
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto pt = cand->param_types[0];
        if (pt && types_equal(pt, t))
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    // Generic Drop impl: `impl<T> Drop for Foo<T>` registers Foo__drop with
    // param Foo<TypeVar>. Strict types_equal can't match a concrete
    // Foo<i64>. Fall back to a base-name match — any one-param candidate
    // whose param is a struct of the same base name accepts the concrete
    // after monomorphisation. mono_clone's SDrop case re-mangles the
    // returned template name to <concrete_struct_name>__drop at clone time,
    // matching the symbol clone_struct_def emits when instantiating the
    // struct's methods.
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto pt = cand->param_types[0];
        if (!pt) continue;
        auto pk = TypeRef(pt).kind();
        if (pk != LogosType::Kind::Struct && pk != LogosType::Kind::ZonedStruct) continue;
        if (TypeRef(pt).struct_name() == TypeRef(t).struct_name())
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    return {};
}

bool SemaChecker::has_droppable_fields(TypeRef t) const {
    if (!t || TypeRef(t).kind() != LogosType::Kind::Struct) return false;
    auto sit = structs_.end();
    if (!TypeRef(t).pkg_name().empty()) {
        auto qkey = sema_key(TypeRef(t).pkg_name(), TypeRef(t).struct_name());
        sit = structs_.find(qkey);
    }
    if (sit == structs_.end()) sit = structs_.find(std::string(TypeRef(t).struct_name()));
    if (sit == structs_.end()) return false;
    for (auto& f : sit->second.fields) {
        if (!drop_fn_for(f.type).empty()) return true;
        if (has_droppable_fields(f.type)) return true;
    }
    return false;
}

// Auto-Copy: a struct with no `impl Drop` and whose every field is itself
// a Copy type behaves as Copy (uniform structural rule — no
// `#[derive(Copy)]` opt-in). Fixpoint over the struct dependency graph;
// `impl Copy for X` entries inserted by collect_impl seed the set.
//
// Copy field kinds: all primitives (integers, floats, bool, char,
// usize/isize), raw pointers (*const/*mut), references (&/&mut),
// function pointers, and structs already known Copy. Tuples are Copy
// iff every element is Copy. Anything else (TypeVar, Closure, Array of
// non-Copy, Slice, TraitObject, Enum-with-payload, etc.) blocks Copy.
//
// `impl Drop for X` blocks Copy regardless of field shape. has_droppable_fields
// is *not* used here — transitive drop comes from a field type, and if that
// type isn't Copy, the field-kind check already rejects.
void SemaChecker::compute_auto_copy_types() {
    using K = LogosType::Kind;
    auto field_kind_is_trivially_copy = [](K k) {
        switch (k) {
            case K::I8: case K::I16: case K::I24: case K::I32:
            case K::I56: case K::I64: case K::I128:
            case K::U8: case K::U16: case K::U24: case K::U32:
            case K::U56: case K::U64: case K::U128:
            case K::F32: case K::F64:
            case K::Bool: case K::Char:
            case K::Usize: case K::Isize:
            case K::Ptr: case K::Ref: case K::MutRef:
            case K::FnPtr: case K::TaggedPtr:
            case K::Enum:               // payload-less enums; payload enums rejected below
                return true;
            default: return false;
        }
    };
    auto has_drop_impl = [&](const std::string& bare_name) {
        // Drop registration: collect_impl inserts into impls_ keyed
        // "Drop::<target>". Plain bare-name lookup matches both
        // `impl Drop for X` and pkg-qualified variants.
        return impls_.count("Drop::" + bare_name) != 0;
    };
    // is_copy_field: does this field-type qualify as Copy given the current
    // pending-copy set? Recurses into struct/tuple shapes; bottoms out on
    // primitive kinds or the pending set.
    std::function<bool(TypeRef)> is_copy_field;
    is_copy_field = [&](TypeRef t) -> bool {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        if (field_kind_is_trivially_copy(k)) {
            // Enum: Copy iff no variant has a payload AND no impl Drop.
            // (Logos enums-with-payload are tagged unions storing owned data.)
            if (k == K::Enum) {
                auto ename = std::string(TypeRef(t).struct_name());
                auto eit = enums_.find(ename);
                if (eit == enums_.end()) return true;  // unknown — be generous
                for (auto& v : eit->second.variants)
                    if (!v.payload_types.empty()) return false;
                return true;
            }
            return true;
        }
        if (k == K::Struct) {
            // Look up by qualified-then-bare key, same as has_droppable_fields.
            auto sit = structs_.end();
            if (!TypeRef(t).pkg_name().empty()) {
                auto qkey = sema_key(TypeRef(t).pkg_name(), TypeRef(t).struct_name());
                sit = structs_.find(qkey);
            }
            if (sit == structs_.end()) sit = structs_.find(std::string(TypeRef(t).struct_name()));
            if (sit == structs_.end()) return false;  // unknown — conservative
            return copy_types_.count(std::string(TypeRef(t).struct_name())) != 0;
        }
        if (k == K::Tuple) {
            for (auto sub : TypeRef(t).type_args())
                if (!is_copy_field(sub)) return false;
            return true;
        }
        return false;
    };

    // Fixpoint: each round, promote any non-Drop struct whose every field is
    // a copy_field. Stops when no new struct is promoted.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [skey, info] : structs_) {
            // skey is "pkg::name" (sema_key separator) or "name"; strip pkg
            // for the copy_types_ set because that's what is_move_type keys
            // on (TypeRef::struct_name() returns bare).
            std::string bare = skey;
            if (auto sep = bare.rfind("::"); sep != std::string::npos)
                bare = bare.substr(sep + 2);
            if (copy_types_.count(bare)) continue;
            // Spec / annotation / Hermes datatypes — leave to manual `impl Copy`.
            if (!info.is_data_plain) continue;
            if (info.fields.empty()) continue;  // zero-sized; skip (Logos treats odd)
            if (has_drop_impl(bare)) continue;
            bool all_copy = true;
            for (auto& f : info.fields) {
                if (!is_copy_field(f.type)) { all_copy = false; break; }
            }
            if (all_copy) {
                copy_types_.insert(bare);
                changed = true;
            }
        }
    }
}

std::optional<lir::LStmt> SemaChecker::make_drop_stmt(const std::string& name, const VarInfo& info) const {
    auto dfn = drop_fn_for(info.type);
    bool df  = has_droppable_fields(info.type);
    if (dfn.empty() && !df) return std::nullopt;
    // Suppress auto-drop of the `self` param of a Drop fn — calling drop
    // on `self` from inside its own drop body is infinite recursion.
    // Detected via name match: when the resolved drop_fn equals the
    // currently-being-lowered fn's mangled name. Mono will re-mangle the
    // call site to the concrete instance, but the SAME identity match
    // holds because both sides see the same template name at sema time.
    if (!dfn.empty() && !current_fn_mangled_.empty()) {
        // Both names may carry an overload-disambig "__g__..." suffix —
        // strip from each before compare so the self-recursion check
        // catches the template-vs-template equivalence.
        // Strip pkg prefix (`pkg.`) and overload-disambig suffix (`__[fg]__`)
        // before compare so the self-recursion check catches the
        // template-vs-template equivalence. After unconditional pkg-mangling
        // dfn carries `pkg.Base__drop__g__sig` while current_fn_mangled_ may
        // be the bare `Base__drop`.
        auto strip_g = [](std::string s) {
            if (auto dot = s.rfind('.'); dot != std::string::npos)
                s = s.substr(dot + 1);
            if (auto p = s.find("__g__"); p != std::string::npos) s.resize(p);
            else if (auto p = s.find("__f__"); p != std::string::npos) s.resize(p);
            return s;
        };
        if (strip_g(dfn) == strip_g(current_fn_mangled_)) return std::nullopt;
    }
    // Collect field paths that were moved out of this var. moved_vars_ stores
    // dotted paths like "<name>.<field>" (and deeper, ignored at this level —
    // a field move implies the whole field is gone, so we only need the
    // first-level field name). The mlir-gen field-drop loop reads this and
    // skips matching fields, so the underlying value isn't released twice.
    std::vector<std::string> moved_fields;
    {
        std::string prefix = name + ".";
        for (auto& mv : moved_vars_) {
            if (mv.size() <= prefix.size()) continue;
            if (mv.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = mv.substr(prefix.size());
            // Take only the first segment — a deeper move (a.b.c) still means
            // a.b is partially consumed; we suppress the whole-field drop.
            auto dot = rest.find('.');
            std::string field = (dot == std::string::npos) ? rest : rest.substr(0, dot);
            // De-dup
            bool seen = false;
            for (auto& f : moved_fields) if (f == field) { seen = true; break; }
            if (!seen) moved_fields.push_back(std::move(field));
        }
    }
    lir::LStmt s; s.line = node_line_;
    if (cur_prog_)
        s.mirror_offset_ = lir_mirror_emit_drop(*cur_prog_, node_line_, name, dfn, info.type, df, moved_fields);
    return s;
}

std::vector<lir::LStmt> SemaChecker::collect_drops() const {
    std::vector<lir::LStmt> drops;
    if (scope_.empty()) return drops;
    auto& frame = scope_.back();
    for (auto it = frame.var_order.rbegin(); it != frame.var_order.rend(); ++it) {
        if (moved_vars_.count(*it)) continue;
        auto vit = frame.vars.find(*it);
        if (vit == frame.vars.end()) continue;
        if (auto d = make_drop_stmt(*it, vit->second))
            drops.push_back(std::move(*d));
    }
    return drops;
}

std::vector<lir::LStmt> SemaChecker::collect_all_drops() const {
    std::vector<lir::LStmt> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        for (auto it = fit->var_order.rbegin(); it != fit->var_order.rend(); ++it) {
            if (moved_vars_.count(*it)) continue;
            auto vit = fit->vars.find(*it);
            if (vit == fit->vars.end()) continue;
            if (auto d = make_drop_stmt(*it, vit->second))
                drops.push_back(std::move(*d));
        }
    }
    return drops;
}

// ── Name helpers ─────────────────────────────────────────────────────────────

std::string SemaChecker::struct_name_of(std::string_view var_name) {
    auto t = lookup(var_name);
    if (!t) return {};
    if (TypeRef(t).kind() == LogosType::Kind::Struct ||
        TypeRef(t).kind() == LogosType::Kind::ZonedStruct) return TypeRef(t).struct_name().to_string();
    if (is_ref_like(TypeRef(t).kind()) && TypeRef(t).pointee() &&
        (TypeRef(t).pointee().kind() == LogosType::Kind::Struct ||
         TypeRef(t).pointee().kind() == LogosType::Kind::ZonedStruct))
        return TypeRef(t).pointee().struct_name().to_string();
    return {};
}


std::string SemaChecker::struct_name_from_type(TypeRef t) {
    if (!t) return {};
    if (TypeRef(t).kind() == LogosType::Kind::Struct || TypeRef(t).kind() == LogosType::Kind::ZonedStruct) {
        if (!TypeRef(t).type_args().empty()) return concrete_struct_name(t);
        return TypeRef(t).struct_name().to_string();
    }
    if (is_ref_like(TypeRef(t).kind()) && TypeRef(t).pointee() &&
        (TypeRef(t).pointee().kind() == LogosType::Kind::Struct ||
         TypeRef(t).pointee().kind() == LogosType::Kind::ZonedStruct)) {
        if (!TypeRef(t).pointee().type_args().empty()) return concrete_struct_name(TypeRef(t).pointee());
        return TypeRef(t).pointee().struct_name().to_string();
    }
    return {};
}


// ── Type parameter helpers ───────────────────────────────────────────────────

std::vector<std::string> SemaChecker::read_lifetime_params(TinyMapView node) {
    std::vector<std::string> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) != la::LIFETIME_PARAM) continue;
        result.push_back(std::string(str_of(tpnode.get(la::NAME.code))));
    }
    return result;
}

// B65: extract `'long: 'short` clauses from a node's TYPE_PARAMS items.
// LIFETIME_PARAM nodes with non-empty ITEMS encode outlives bounds:
//   `'long: 'a + 'b + 'c` → ('long, 'a), ('long, 'b), ('long, 'c)
// Each ITEMS entry is a LIFETIME_PARAM sub-node with NAME = shorter lifetime.
std::vector<std::pair<std::string, std::string>>
SemaChecker::read_lifetime_outlives_from(TinyMapView node, int32_t field_code) {
    std::vector<std::pair<std::string, std::string>> result;
    if (!node.has_key(field_code)) return result;
    AnyVal tpav = node.get(field_code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) != la::LIFETIME_PARAM) continue;
        if (!tpnode.has_key(la::ITEMS)) continue;
        std::string longer(str_of(tpnode.get(la::NAME.code)));
        auto inner = arr_of(tpnode.get(la::ITEMS.code));
        for (uint64_t j = 0; j < inner.size(); ++j) {
            auto inode = map_of(inner.get(j));
            if (code_of(inode) != la::LIFETIME_PARAM) continue;
            std::string shorter(str_of(inode.get(la::NAME.code)));
            result.emplace_back(longer, shorter);
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>>
SemaChecker::read_lifetime_outlives(TinyMapView node) {
    return read_lifetime_outlives_from(node, la::TYPE_PARAMS.code);
}

bool SemaChecker::assoc_eqs_satisfied(
    const std::string& trait_name,
    const std::string& concrete_name,
    const std::string& base_name,
    const std::vector<std::pair<std::string, TypeRef>>& expected) {
    if (expected.empty()) return true;
    for (auto& [aname, expected_ty] : expected) {
        if (!expected_ty) continue;
        // Look up the impl's `type Assoc = X` for this trait+concrete.
        // 1. Direct (concrete name).
        std::string key = trait_name + "::" + concrete_name + "::" + aname;
        auto it = assoc_type_impls_.find(key);
        if (it == assoc_type_impls_.end() && !base_name.empty() && base_name != concrete_name) {
            std::string bkey = trait_name + "::" + base_name + "::" + aname;
            it = assoc_type_impls_.find(bkey);
        }
        TypeRef found = (it != assoc_type_impls_.end()) ? it->second.type : nullptr;
        // 2. Blanket-derived: collect_impl keys blanket-impl assoc-types under
        // `Trait::$blanket$Trait$BoundTrait$Target::AssocName`. If `concrete`
        // doesn't have a direct impl but satisfies a blanket's bounds, use
        // that blanket's assoc-type definition. The stored type may reference
        // the blanket's target typevar (e.g. `type P = DT::Prim`); substitute
        // target → concrete and recursively resolve via subst_type_sema so
        // chains like `K: Primitive ⇒ K: HasPrim<P = K::Prim = i32>` reduce
        // to a concrete type before the equality check.
        if (!found) {
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != trait_name) continue;
                logos::compiler::StrSet seen_pri;
                bool ok = bi.bound_trait.empty()
                    || sema_has_impl_recursive(bi.bound_trait, concrete_name, base_name, seen_pri);
                if (ok) {
                    for (auto& eb : bi.extra_bounds) {
                        logos::compiler::StrSet seen_eb;
                        if (!sema_has_impl_recursive(eb, concrete_name, base_name, seen_eb)) {
                            ok = false; break;
                        }
                    }
                }
                if (!ok) continue;
                std::string bkey = trait_name + "::$blanket$" + trait_name + "$"
                                 + bi.bound_trait + "$" + bi.target_typevar
                                 + "::" + aname;
                auto bit = assoc_type_impls_.find(bkey);
                if (bit == assoc_type_impls_.end()) continue;
                TypeRef concrete_t = lookup_type_by_name(concrete_name);
                if (!concrete_t && !base_name.empty() && base_name != concrete_name)
                    concrete_t = lookup_type_by_name(base_name);
                if (concrete_t) {
                    SemaSubst bsubst;
                    bsubst[bi.target_typevar] = concrete_t;
                    found = subst_type_sema(bit->second.type, bsubst);
                } else {
                    found = bit->second.type;
                }
                break;
            }
        }
        if (!found) return false;
        if (!types_equal(found, expected_ty)) return false;
    }
    return true;
}

bool SemaChecker::is_effective_dst(TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct) return false;
    std::string sn(t.struct_name());
    auto [spkg, ssi] = find_struct_by_name(sn);
    if (!ssi) { auto [dpkg, dsi] = find_datatype_by_name(sn); ssi = dsi; }
    if (!ssi) return false;
    if (ssi->is_dst) return true;
    // Generic instantiation check: substitute type-args into the
    // template's last field; if it lands on UnsizedSlice/UnsizedDyn,
    // the instance is effectively DST.
    if (ssi->fields.empty() || ssi->type_params.empty()) return false;
    auto args = t.type_args();
    if (args.empty()) return false;
    SemaSubst tmp;
    for (size_t i = 0; i < ssi->type_params.size() && i < args.size(); ++i)
        tmp[ssi->type_params[i].name] = args[i];
    auto subst_last = subst_type_sema(ssi->fields.back().type, tmp);
    if (!subst_last) return false;
    auto sk = subst_last.kind();
    return sk == LogosType::Kind::UnsizedSlice || sk == LogosType::Kind::UnsizedDyn;
}

uint64_t SemaChecker::sema_abi_byte_size(TypeRef t, logos::compiler::StrSet& seen) {
    using K = LogosType::Kind;
    if (!t) return 8;
    TypeRef tv{t};
    switch (tv.kind()) {
    case K::Void:    return 0;
    case K::Bool:    return 1;
    case K::U8: case K::I8:      return 1;
    case K::I16: case K::U16:    return 2;
    case K::I24: case K::U24:    return 3;
    case K::I32: case K::U32: case K::F32: case K::IntLit:
    case K::Char:                return 4;
    case K::I56: case K::U56:    return 7;
    case K::I64: case K::U64: case K::F64: case K::FloatLit:
    case K::Ptr:  case K::Ref:  case K::MutRef:
    case K::FnPtr: case K::TaggedPtr:
    case K::Usize: case K::Isize: return 8;
    case K::I128: case K::U128:  return 16;
    case K::Slice: case K::Closure: case K::TraitObject: case K::DstRef:
        return 16;
    case K::UnsizedSlice: case K::UnsizedDyn:
        return 0;
    case K::Array:
        if (!tv.elem()) return 0;
        return tv.arr_size() * sema_abi_byte_size(tv.elem(), seen);
    case K::Tuple: {
        uint64_t off = 0, max_align = 1;
        for (auto e : tv.tuple_elems()) {
            uint64_t esz = sema_abi_byte_size(e, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) off = (off + align - 1) & ~(align - 1);
            off += esz;
            if (align > max_align) max_align = align;
        }
        return (off + max_align - 1) & ~(max_align - 1);
    }
    case K::Struct:
    case K::ZonedStruct: {
        std::string sn(tv.struct_name());
        if (seen.count(sn)) return 8;  // cycle guard
        auto [spkg, ssi] = find_struct_by_name(sn);
        if (!ssi) { auto [dpkg, dsi] = find_datatype_by_name(sn); ssi = dsi; }
        if (!ssi) return 8;  // unknown — assume pointer size
        seen.insert(sn);
        uint64_t off = 0, max_align = 1;
        for (auto& f : ssi->fields) {
            uint64_t esz = sema_abi_byte_size(f.type, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) off = (off + align - 1) & ~(align - 1);
            off += esz;
            if (align > max_align) max_align = align;
        }
        seen.erase(sn);
        return (off + max_align - 1) & ~(max_align - 1);
    }
    case K::Enum: {
        // Simplified: tag (i32) + max variant payload. Mirrors mlir_gen.
        auto [epkg, esi] = find_enum_by_name(std::string(tv.enum_name()));
        if (!esi) return 8;
        uint64_t max_payload = 0;
        for (auto& v : esi->variants) {
            uint64_t variant = 0;
            for (auto& pt : v.payload_types) {
                if (TypeRef(pt).kind() == K::Void) continue;
                variant += sema_abi_byte_size(pt, seen);
            }
            if (variant > max_payload) max_payload = variant;
        }
        return 4 + max_payload;
    }
    default: return 8;
    }
}

void SemaChecker::finalize_relaxed_bounds(TypeParam& tp) {
    // Walk in-place: keep positive bounds, consume `?Trait` markers.
    // `?Sized` clears the implicit Sized bound; any other relaxed name
    // is a hard error. The relaxed bound itself is not propagated as a
    // positive bound — downstream code (mono, bound-check) should never
    // see one in tp.bounds.
    auto it = tp.bounds.begin();
    while (it != tp.bounds.end()) {
        if (!it->is_relaxed) { ++it; continue; }
        if (it->trait_name == "Sized") {
            tp.implicit_sized = false;
        } else {
            error(std::format(
                "type parameter '{}': relaxed bound '?{}' is not permitted "
                "(only `?Sized` is supported)",
                tp.name, it->trait_name));
        }
        it = tp.bounds.erase(it);
    }
}

void SemaChecker::read_trait_bound_args(TinyMapView bnode, TraitBound& tb) {
    // Phase 1: `?Trait` relaxed-bound marker. Grammar emits RELAXED=true
    // for the `?IDENT` form. Only `?Sized` is semantically valid; other
    // relaxed names are rejected when bound list is finalized on the
    // parent type-param. Stored on the bound itself so the post-parse
    // sweep can find it.
    if (bnode.has_key(la::RELAXED)) {
        auto rav = bnode.get(la::RELAXED.code);
        if (!rav.is_null() && rav.is_value()) {
            tb.is_relaxed = (rav.as_value<uint8_t>() != 0);
        }
    }
    // Sprint 5.7: Fn-family parenthesized form `Fn(args) -> ret`.
    // PARAMS holds the arg-type list; RET_TYPE holds the return type
    // (both optional). Distinct slots from TYPE_PARAMS so the two
    // bound forms coexist. `is_fn_family` flagged here for downstream
    // dispatch (sema bound-check, mono substitution).
    if (tb.trait_name == "Fn" || tb.trait_name == "FnMut" ||
        tb.trait_name == "FnOnce") {
        tb.is_fn_family = true;
    }
    if (bnode.has_key(la::PARAMS)) {
        auto pav = bnode.get(la::PARAMS.code);
        if (!pav.is_null()) {
            auto pmap = map_of(pav);
            if (pmap.has_key(la::ITEMS)) {
                auto pitems = arr_of(pmap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < pitems.size(); ++i)
                    tb.fn_params.push_back(resolve_type(map_of(pitems.get(i))));
            }
        }
    }
    if (bnode.has_key(la::RET_TYPE)) {
        auto rav = bnode.get(la::RET_TYPE.code);
        if (!rav.is_null()) tb.fn_ret = resolve_type(map_of(rav));
    }

    // B63 limit-1: capture `for<'a, 'b>` binders from HRTB_BINDERS slot.
    // Items may be raw LIFETIME terminals (strings) or LIFETIME_PARAM maps
    // depending on grammar action shape — accept both defensively.
    if (bnode.has_key(la::HRTB_BINDERS)) {
        auto hav = bnode.get(la::HRTB_BINDERS.code);
        if (!hav.is_null()) {
            auto hmap = map_of(hav);
            if (hmap.has_key(la::ITEMS)) {
                auto hitems = arr_of(hmap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < hitems.size(); ++i) {
                    auto av = hitems.get(i);
                    if (av.is_null()) continue;
                    if (av.is_value()) {
                        tb.hrtb_binders.push_back(std::string(str_of(av)));
                    } else {
                        auto m = map_of(av);
                        if (m.has_key(la::NAME))
                            tb.hrtb_binders.push_back(
                                std::string(str_of(m.get(la::NAME.code))));
                    }
                }
            }
        }
    }

    if (!bnode.has_key(la::TYPE_PARAMS)) return;
    auto tpav = bnode.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return;
    auto tamap = map_of(tpav);
    if (!tamap.has_key(la::ITEMS)) return;
    auto items = arr_of(tamap.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t ic = code_of(item);
        if (ic == la::ASSOC_EQ_BIND) {
            std::string aname(str_of(item.get(la::NAME.code)));
            TypeRef rhs = nullptr;
            if (item.has_key(la::TYPE))
                rhs = resolve_type(map_of(item.get(la::TYPE.code)));
            tb.assoc_eqs.emplace_back(std::move(aname), rhs);
        } else if (ic == la::LIFETIME_PARAM) {
            // L1: lifetime arg at trait-bound TYPE_PARAMS position
            // (e.g. `Foo<'a>` bound). Logos doesn't track regions
            // structurally for bound dispatch — capture for record
            // (lifetime_args) and skip the resolve_type path that
            // would otherwise error "unexpected type node 131".
            if (item.has_key(la::NAME))
                tb.lifetime_args.push_back(std::string(str_of(item.get(la::NAME.code))));
        } else {
            tb.type_args.push_back(resolve_type(item));
        }
    }
}

std::vector<TypeParam> SemaChecker::read_type_params_from(TinyMapView node, int32_t field_code) {
    std::vector<TypeParam> result;
    AnyVal tpav = node.get(field_code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            if (tpnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = tpnode.get(la::IS_VARIADIC.code);
                tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    read_trait_bound_args(bnode, tb);
                    tp.bounds.push_back(std::move(tb));
                } else if (code_of(bnode) == la::LIFETIME_PARAM) {
                    // B65: `T: 'a (+ 'b)*` — type-outlives bounds.
                    tp.lifetime_outlives.push_back(
                        std::string(str_of(bnode.get(la::NAME.code))));
                }
            }
        }
        finalize_relaxed_bounds(tp);
        result.push_back(std::move(tp));
    }
    // Remove temp typevars added in pre-pass (push_type_params will re-add them properly).
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    return result;
}

std::vector<TypeParam> SemaChecker::read_type_params(TinyMapView node) {
    std::vector<TypeParam> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    // type_param_list => { ITEMS: $... }
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        // Skip lifetime params ('a) — deferred to borrow checker.
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            if (tpnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = tpnode.get(la::IS_VARIADIC.code);
                tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        // Check variadic flag (T...)
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        // Optional bounds: ITEMS contains TRAIT_BOUND nodes
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    read_trait_bound_args(bnode, tb);
                    tp.bounds.push_back(std::move(tb));
                }
            }
        }
        // Validate: variadic param must be last
        if (tp.is_variadic && i + 1 < tpitems.size())
            error("variadic type parameter must be last in the type parameter list");
        // Note: relaxed-bound finalization happens after `where`-clause
        // merge below, so a `where T: ?Sized` clause is also honored.
        result.push_back(std::move(tp));
    }
    // Remove temp typevars added in pre-pass.
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    // Merge bounds from `where T: Trait, U: Trait2` clause.
    if (node.has_key(la::WHERE)) {
        AnyVal wav = node.get(la::WHERE.code);
        if (!wav.is_null()) {
            auto wnode = map_of(wav);
            if (wnode.has_key(la::ITEMS)) {
                auto witems = arr_of(wnode.get(la::ITEMS.code));
                for (uint64_t i = 0; i < witems.size(); ++i) {
                    auto constraint = map_of(witems.get(i));
                    if (code_of(constraint) != la::TYPE_PARAM) continue;
                    auto tname = std::string(str_of(constraint.get(la::NAME.code)));
                    // Find the type param in result and add bounds.
                    TypeParam* tp_ptr = nullptr;
                    for (auto& tp : result)
                        if (tp.name == tname) { tp_ptr = &tp; break; }
                    if (!tp_ptr) {
                        // type param in where clause not in param list — add it
                        TypeParam tp; tp.name = tname;
                        result.push_back(std::move(tp));
                        tp_ptr = &result.back();
                    }
                    if (constraint.has_key(la::ITEMS)) {
                        auto bounds = arr_of(constraint.get(la::ITEMS.code));
                        for (uint64_t b = 0; b < bounds.size(); ++b) {
                            auto bnode = map_of(bounds.get(b));
                            if (code_of(bnode) == la::TRAIT_BOUND) {
                                TraitBound tb;
                                tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                                read_trait_bound_args(bnode, tb);
                                tp_ptr->bounds.push_back(std::move(tb));
                            }
                        }
                    }
                }
            }
        }
    }
    // Phase 1: finalize relaxed bounds (`?Sized`) after all bounds —
    // including those from `where` clauses — have been merged. This is
    // the canonical post-parse point for type-param invariants.
    for (auto& tp : result) finalize_relaxed_bounds(tp);
    return result;
}

// ── Sema-side type substitution ──────────────────────────────────────────────

TypeRef SemaChecker::subst_type_sema(TypeRef t, const SemaSubst& s,
                                               const SemaLifetimeSubst& ls) {
    if (!t) return t;
    switch (t.kind()) {
    case LogosType::Kind::ConstVar:
    case LogosType::Kind::TypeVar: {
        auto it = s.find(std::string(t.type_var_name()));
        return (it != s.end()) ? TypeRef(it->second) : t;
    }
    case LogosType::Kind::Array: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        uint64_t size = t.arr_size();
        std::string symbolic{t.arr_size_var()};
        if (!symbolic.empty()) {
            auto it = s.find(symbolic);
            if (it != s.end()) {
                TypeRef sub(it->second);
                if (auto cv = sub.const_val()) {
                    size = (uint64_t)*cv;
                    symbolic = "";
                } else if (sub.kind() == LogosType::Kind::ConstVar) {
                    symbolic = std::string(sub.type_var_name());
                }
            }
        }
        if (elem == t.elem() && size == t.arr_size() && symbolic == t.arr_size_var()) return t;
        return make_array(elem, size, symbolic);
    }
    case LogosType::Kind::Ptr: {
        auto inner = subst_type_sema(t.pointee(), s, ls);
        // Phase 1B-2: `*const [T]` / `*mut [T]` after substitution are fat
        // pointers. When substitution lands an UnsizedSlice<U> inside a
        // raw pointer, canonicalise to the existing Kind::Slice so the
        // type matches the SLICE_TYPE grammar route (which also lowers
        // `*const [T]` directly to Slice).
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        // Phase 1B-4: same canonicalisation for UnsizedDyn → TraitObject.
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `*const DstStruct` / `*mut DstStruct` → DstRef.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_dst_ref(sn, spkg, t.mut_ptr(), std::move(args_vec));
        }
        if (inner == t.pointee()) return t;
        return make_ptr(t.mut_ptr(), inner);
    }
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto inner = subst_type_sema(t.pointee(), s, ls);
        std::string lt{t.lifetime()};
        if (!lt.empty()) { auto it = ls.find(lt); if (it != ls.end()) lt = it->second; }
        // Phase 1B-2: `&[T]` / `&mut [T]` after substitution are fat
        // pointers. When substitution lands an UnsizedSlice<U> inside a
        // safe reference, canonicalise to the existing Kind::Slice — the
        // same kind produced by the `&[T]` grammar route. Lifetime info
        // is dropped here because Kind::Slice does not carry per-instance
        // lifetimes (Logos lifetime model is elision-based at this layer).
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        // Phase 1B-4: same canonicalisation for UnsizedDyn → TraitObject.
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `&DstStruct` / `&mut DstStruct` → DstRef.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, t.kind() == LogosType::Kind::MutRef, std::move(targs));
        }
        if (inner == t.pointee() && lt == t.lifetime()) return t;
        return make_ref(t.kind() == LogosType::Kind::MutRef, inner, lt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        if (t.type_args().empty() && t.lifetime_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosTypeBuilder nt;
        nt.kind = t.kind();
        nt.struct_name = t.struct_name();
        nt.pkg_name = t.pkg_name();  // preserve package qualification after substitution
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::Enum: {
        if (t.type_args().empty() && t.lifetime_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosTypeBuilder nt;
        nt.kind = LogosType::Kind::Enum;
        nt.enum_name = t.enum_name();
        nt.pkg_name = t.pkg_name();  // preserve package qualification after substitution
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        std::vector<TypeRef> new_elems;
        bool changed = false;
        for (auto e : t.tuple_elems()) {
            auto ne = subst_type_sema(e, s, ls);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return t;
        return make_tuple_type(std::move(new_elems));
    }
    case LogosType::Kind::Slice: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        if (elem == t.elem()) return t;
        return make_slice_type(elem);
    }
    case LogosType::Kind::UnsizedSlice: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        if (elem == t.elem()) return t;
        return make_unsized_slice_type(elem);
    }
    case LogosType::Kind::UnsizedDyn: {
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_unsized_dyn_type(t.trait_name(), std::move(new_args));
    }
    case LogosType::Kind::DstRef: {
        // Phase 1B-15: substitute type-args.
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_dst_ref(t.struct_name(), t.pkg_name(), t.mut_ptr(),
                            std::move(new_args));
    }
    case LogosType::Kind::TraitObject: {
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_trait_object(t.trait_name(), std::move(new_args));
    }
    case LogosType::Kind::Closure:
    case LogosType::Kind::FnPtr: {
        std::vector<TypeRef> new_params;
        bool changed = false;
        for (auto p : t.closure_params()) {
            auto np = subst_type_sema(p, s, ls);
            changed |= (np != p);
            new_params.push_back(np);
        }
        auto new_ret = subst_type_sema(t.closure_ret(), s, ls);
        changed |= (new_ret != t.closure_ret());
        if (!changed) return t;

        LogosTypeBuilder nt;
        nt.kind = t.kind();  // preserve Closure vs FnPtr
        nt.closure_params = std::move(new_params);
        nt.closure_ret = new_ret;
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::CfgSlotType: {
        // Substitute the cfg-typevar binding; if it now resolves to a
        // concrete HStaticLit, walk the registered LIR mirror via the
        // encoded path (assoc_type_name) and return the resolved TypeRef.
        std::string cfg_name(t.type_var_name());
        std::string path_enc(t.assoc_type_name());
        auto it = s.find(cfg_name);
        if (it == s.end()) return t;
        TypeRef cfg = TypeRef(it->second);
        cfg = subst_type_sema(cfg, s, ls);
        if (!cfg || TypeRef(cfg).kind() != LogosType::Kind::HStaticLit) return t;
        if (!cur_prog_) return t;
        uint64_t hash = (uint64_t)cfg.const_val().value_or(0);
        auto rit = cur_prog_->hstatic_registry_.find(hash);
        if (rit == cur_prog_->hstatic_registry_.end()) return t;
        if (!rit->second || rit->second->mirror_offset_ == hermes::arena_offset_t{}) return t;
        lir_view::ExprRef eref(cur_prog_->type_pool.arena(), rit->second->mirror_offset_);
        if (eref.kind() != lir_schema::expr::Code::HermesLit) return t;
        // Decode path.
        struct Step { char kind; std::string name; int64_t index; };
        std::vector<Step> steps;
        {
            size_t p = 0;
            while (p < path_enc.size()) {
                Step st{};
                st.kind = path_enc[p++];
                size_t e = path_enc.find('\x1F', p);
                if (e == std::string::npos) e = path_enc.size();
                std::string payload = path_enc.substr(p, e - p);
                if (st.kind == 'F') st.name = std::move(payload);
                else st.index = std::stoll(payload);
                steps.push_back(std::move(st));
                p = e + 1;
            }
        }
        lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
        for (auto& st : steps) {
            using K = lir_schema::hermes_val::Code;
            bool found = false;
            if (st.kind == 'F' || st.kind == 'I') {
                if (cur.kind() != K::Map) return t;
                auto map = lir_view::HVMapView{cur};
                if (st.kind == 'F' && !map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.str_key(i) == st.name) { cur = map.value(i); found = true; break; }
                } else if (st.kind == 'I' && map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.int_key(i) == st.index) { cur = map.value(i); found = true; break; }
                }
            } else if (st.kind == 'A') {
                if (cur.kind() != K::Array) return t;
                auto arr = lir_view::HVArrayView{cur};
                if ((uint64_t)st.index >= arr.size()) return t;
                cur = arr.elem((uint64_t)st.index);
                found = true;
            }
            if (!found) return t;
        }
        if (cur.kind() == lir_schema::hermes_val::Code::Type) {
            std::string tname(lir_view::HVTypeView{cur}.name());
            if (auto resolved = const_cast<SemaChecker*>(this)->try_resolve_as_known_type(tname))
                return resolved;
        }
        return t;
    }
    case LogosType::Kind::AssocType: {
        // Substitute the base type first.
        auto subbed_base = subst_type_sema(t.assoc_base(), s, ls);
        
        // Try resolving: if base is substituted to a concrete type, look up impl.
        TypeRef concrete = nullptr;
        if (subbed_base && TypeRef(subbed_base).kind() != LogosType::Kind::TypeVar && TypeRef(subbed_base).kind() != LogosType::Kind::ConstVar) {
            concrete = subbed_base;
        } else if (subbed_base && TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
             // Even if it's a typevar, perhaps it is a known concrete name like "i32" (though unlikely for TypeVar)
             // actually if it's still a typevar, we can't resolve it yet.
        }
        // If still unresolved, try looking up the *substituted* base typevar by name.
        // Using the original base (often "Self") here can over-resolve in generic contexts
        // where Self was already substituted to another typevar (e.g. T).
        if (!concrete && subbed_base && TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
            if (auto looked = const_cast<SemaChecker*>(this)->lookup_type_by_name(TypeRef(subbed_base).type_var_name())) {
                if (TypeRef(looked).kind() != LogosType::Kind::TypeVar &&
                    TypeRef(looked).kind() != LogosType::Kind::ConstVar) {
                    concrete = looked;
                }
            }
        }

        // Substitute gat_args as well
        std::vector<TypeRef> subbed_gat_args;
        bool gat_changed = false;
        for (auto ga : t.gat_args()) {
            auto nga = subst_type_sema(ga, s, ls);
            gat_changed |= (nga != ga);
            subbed_gat_args.push_back(nga);
        }

        // TypeVar-with-bound branch: `K::AssocType` where K is a still-typevar
        // and K's bounds include some `BoundTrait` for which there exists a
        // blanket `impl<DT: BoundTrait> Trait for DT { type AssocType = … }`.
        // Reduce by substituting the blanket's target typevar with K (kept as
        // a TypeVar). Closes abstraction-debt #6 — `K::ViewInStore` → `*const K`
        // when K: PodRef in the surrounding generic scope.
        if (!concrete && subbed_base &&
            TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
            std::string tvname = std::string(TypeRef(subbed_base).type_var_name());
            auto bit = current_type_bounds_.find(tvname);
            if (bit != current_type_bounds_.end()) {
                // Build a flat set of bound trait names for this typevar.
                StrSet tv_bound_set;
                for (auto& tb : bit->second) tv_bound_set.insert(tb.trait_name);
                for (auto& bi : blanket_impls_) {
                    if (bi.trait_name != t.trait_name()) continue;
                    if (!tv_bound_set.count(bi.bound_trait)) continue;
                    bool all_extra = true;
                    for (auto& eb : bi.extra_bounds)
                        if (!tv_bound_set.count(eb)) { all_extra = false; break; }
                    if (!all_extra) continue;
                    std::string blanket_key = std::string(t.trait_name()) + "::$blanket$"
                        + std::string(t.trait_name()) + "$" + bi.bound_trait
                        + "$" + bi.target_typevar
                        + "::" + std::string(t.assoc_type_name());
                    auto bait = assoc_type_impls_.find(blanket_key);
                    if (bait == assoc_type_impls_.end()) continue;
                    SemaSubst bsubst;
                    bsubst[bi.target_typevar] = subbed_base;
                    return subst_type_sema(bait->second.type, bsubst);
                }
            }
        }

        if (concrete) {
            std::string concrete_name = type_str(concrete);
            // Helper: build combined substitution (impl params + GAT params)
            auto make_subst = [&](const AssocTypeEntry& entry) -> SemaSubst {
                SemaSubst combined;
                for (size_t i = 0; i < entry.impl_type_params.size() &&
                                   i < TypeRef(concrete).type_args().size(); ++i)
                    combined[entry.impl_type_params[i].name] = TypeRef(concrete).type_args()[i];
                for (size_t i = 0; i < entry.gat_type_params.size() &&
                                   i < subbed_gat_args.size(); ++i)
                    combined[entry.gat_type_params[i].name] = subbed_gat_args[i];
                return combined;
            };

            // 1. Direct lookup (non-generic impls: key stored under concrete name).
            std::string key = std::string(t.trait_name()) + "::" + concrete_name + "::" + std::string(t.assoc_type_name());
            auto ait = assoc_type_impls_.find(key);
            if (ait != assoc_type_impls_.end()) {
                return subst_type_sema(ait->second.type, make_subst(ait->second));
            }
            // 2. Base-name fallback (generic impls).
            std::string base_name = (TypeRef(concrete).kind() == LogosType::Kind::Struct)
                                    ? std::string(TypeRef(concrete).struct_name()) : "";
            if (!base_name.empty() && base_name != concrete_name) {
                std::string base_key = std::string(t.trait_name()) + "::" + base_name
                                      + "::" + std::string(t.assoc_type_name());
                auto ait2 = assoc_type_impls_.find(base_key);
                if (ait2 != assoc_type_impls_.end())
                    return subst_type_sema(ait2->second.type, make_subst(ait2->second));
            }
            // 3. Blanket-impl fallback: `impl<T: Bound> Trait for T` provides
            // `type Assoc = …`.  Use it when `concrete` satisfies Bound.
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != t.trait_name()) continue;
                // Concrete type must implement every bound of the blanket.
                auto bound_satisfied = [&](const std::string& bt) {
                    if (impls_.count(bt + "::" + concrete_name)) return true;
                    if (!base_name.empty() && base_name != concrete_name &&
                        impls_.count(bt + "::" + base_name)) return true;
                    return false;
                };
                if (!bound_satisfied(bi.bound_trait)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds)
                    if (!bound_satisfied(eb)) { all_extra = false; break; }
                if (!all_extra) continue;
                std::string blanket_key = std::string(t.trait_name()) + "::$blanket$"
                    + std::string(t.trait_name()) + "$" + bi.bound_trait
                    + "$" + bi.target_typevar
                    + "::" + std::string(t.assoc_type_name());
                auto bait = assoc_type_impls_.find(blanket_key);
                if (bait == assoc_type_impls_.end()) continue;
                // Substitute the blanket's target typevar → concrete.
                SemaSubst bsubst;
                bsubst[bi.target_typevar] = concrete;
                return subst_type_sema(bait->second.type, bsubst);
            }
        }
        // B88: substitute GAT lifetime args.
        std::vector<std::string> subbed_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { subbed_lt_args.push_back(it->second); lt_changed = true; }
            else                  subbed_lt_args.push_back(lt);
        }
        if (subbed_base != t.assoc_base() || gat_changed || lt_changed) {
            LogosTypeBuilder nt = t.to_builder();
            nt.assoc_base    = subbed_base;
            nt.gat_args      = std::move(subbed_gat_args);
            nt.lifetime_args = std::move(subbed_lt_args);
            return pool_->alloc(std::move(nt));
        }
        return t;
    }
    default: return t;
    }
}

// ── Type resolution ──────────────────────────────────────────────────────────

TypeRef SemaChecker::resolve_type(TinyMapView node) {
    int32_t tc = code_of(node);

    if (tc == la::ANTIQUOT_TYPE) {
        std::string nm;
        if (node.has_key(la::NAME)) nm = std::string(str_of(node.get(la::NAME.code)));
        error("`$" + nm + "` antiquotation is only valid inside `quote_ty! { ... }`");
        return error_t();
    }
    if (tc == la::ANTIQUOT_PACK) {
        std::string nm;
        if (node.has_key(la::NAME)) nm = std::string(str_of(node.get(la::NAME.code)));
        error("`$" + nm + "...` pack-splice is only valid inside `quote_ty! { ... }`");
        return error_t();
    }

    if (tc == la::TYPEOF_TYPE) {
        // typeof(expr) — lower the inner expression purely for type inference.
        // Expression is not evaluated at runtime; only its sema-computed type
        // is returned.  The LExpr we build is discarded after this call.
        auto expr_node = map_of(node.get(la::VALUE.code));
        auto lex = lower_expr(expr_node);
        if (!lex || !lex->type) return error_t();
        return lex->type;
    }

    if (tc == la::CFG_SLOT_TYPE) {
        // <type:CFG.path> — extract a type from a HermesStatic-typed binding
        // through an arbitrary path of field/index steps. Each step is an
        // AST item with OP discriminator (0=field_str, 1=field_int,
        // 2=array_idx). Two resolution paths:
        //   • CFG is a const-generic type-param of the enclosing item.
        //     Defer; mono_subst resolves once the param is bound.
        //   • CFG is a type alias to an HStaticLit (`pub type Cfg = @{…};`).
        //     Resolve eagerly by walking the registered LIR mirror.
        //
        // The path is encoded into assoc_type_name (string-typed slot we
        // already reuse on CfgSlotType) using a delimited form:
        //   "F<name>\x1F" | "I<int>\x1F" | "A<int>\x1F"  (one per step)
        // Decoded by mono_subst at concretisation time.
        auto cfg_name = std::string(str_of(node.get(la::NAME.code)));
        bool is_typeparam = current_type_params_.count(cfg_name) > 0;

        // B-ty-06: when CFG is a generic type-param, it must be a const-
        // generic of HermesStatic kind for cfg_slot extraction to make
        // sense.  Inspect current_type_params_[cfg_name] — for const params
        // push_type_params stores a ConstVar whose pointee is const_type.
        if (is_typeparam) {
            auto it = current_type_params_.find(cfg_name);
            if (it != current_type_params_.end()) {
                TypeRef tv = it->second;
                bool ok = TypeRef(tv).kind() == LogosType::Kind::ConstVar &&
                          TypeRef(tv).pointee() &&
                          TypeRef(TypeRef(tv).pointee()).kind() == LogosType::Kind::Struct &&
                          TypeRef(TypeRef(tv).pointee()).struct_name() == "HermesStatic";
                if (!ok) {
                    error(std::format(
                        "'<type:{0}.…>': type-param '{0}' must be declared "
                        "as 'const {0}: HermesStatic' for cfg_slot extraction",
                        cfg_name));
                }
            }
        }

        // Read path steps from ITEMS array.
        struct Step {
            int kind;          // 0=field_str, 1=field_int, 2=array_idx
            std::string name;  // for kind=0
            int64_t  index;    // for kind=1, 2
        };
        std::vector<Step> steps;
        if (node.has_key(la::ITEMS)) {
            auto items_av = node.get(la::ITEMS.code);
            if (!items_av.is_null()) {
                auto items = arr_of(items_av);
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto step_node = map_of(items.get(i));
                    Step s{};
                    if (step_node.has_key(la::OP)) {
                        auto opv = step_node.get(la::OP.code);
                        if (opv.is_value() && !opv.is_pointer())
                            s.kind = (int)opv.as_value<int32_t>();
                    }
                    if (s.kind == 0) {
                        s.name = std::string(str_of(step_node.get(la::NAME.code)));
                    } else {
                        // INTEGER token comes through as a string (peg lexer).
                        if (step_node.has_key(la::INDEX)) {
                            auto sv = str_of(step_node.get(la::INDEX.code));
                            s.index = parse_int_literal(sv);
                        }
                    }
                    steps.push_back(std::move(s));
                }
            }
        }
        if (steps.empty()) {
            error("<type:CFG.path>: empty path");
            return error_t();
        }

        // Encode path for deferred resolution.
        auto encode = [&] {
            std::string r;
            for (auto& s : steps) {
                if (s.kind == 0) { r += 'F'; r += s.name; }
                else if (s.kind == 1) { r += 'I'; r += std::to_string(s.index); }
                else { r += 'A'; r += std::to_string(s.index); }
                r += '\x1F';
            }
            return r;
        };

        if (!is_typeparam) {
            // Eager resolution against an HStaticLit alias.
            TypeRef cfg_t = try_resolve_as_known_type(cfg_name);
            if (cfg_t && TypeRef(cfg_t).kind() == LogosType::Kind::HStaticLit && cur_prog_) {
                uint64_t hash = (uint64_t)cfg_t.const_val().value_or(0);
                auto rit = cur_prog_->hstatic_registry_.find(hash);
                if (rit != cur_prog_->hstatic_registry_.end() && rit->second &&
                    rit->second->mirror_offset_ != hermes::arena_offset_t{}) {
                    lir_view::ExprRef eref(cur_prog_->type_pool.arena(), rit->second->mirror_offset_);
                    if (eref.kind() == lir_schema::expr::Code::HermesLit) {
                        // Walk path through the Hermes value.
                        lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
                        bool ok = true;
                        for (auto& s : steps) {
                            using K = lir_schema::hermes_val::Code;
                            if (s.kind == 0 || s.kind == 1) {
                                if (cur.kind() != K::Map) { ok = false; break; }
                                auto map = lir_view::HVMapView{cur};
                                bool found = false;
                                if (s.kind == 0 && !map.int_keyed()) {
                                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                                        if (map.str_key(i) == s.name) {
                                            cur = map.value(i); found = true; break;
                                        }
                                } else if (s.kind == 1 && map.int_keyed()) {
                                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                                        if (map.int_key(i) == s.index) {
                                            cur = map.value(i); found = true; break;
                                        }
                                }
                                if (!found) { ok = false; break; }
                            } else { // s.kind == 2 — array
                                if (cur.kind() != K::Array) { ok = false; break; }
                                auto arr = lir_view::HVArrayView{cur};
                                if ((uint64_t)s.index >= arr.size()) { ok = false; break; }
                                cur = arr.elem((uint64_t)s.index);
                            }
                        }
                        if (ok && cur.kind() == lir_schema::hermes_val::Code::Type) {
                            std::string tname(lir_view::HVTypeView{cur}.name());
                            if (auto resolved = try_resolve_as_known_type(tname))
                                return resolved;
                        }
                    }
                }
            }
        }
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::CfgSlotType;
        t.type_var_name = cfg_name;       // CFG ident
        t.assoc_type_name = encode();     // encoded path
        return pool_->alloc(std::move(t));
    }

    if (tc == la::PTR_TYPE) {
        bool mut = false;
        AnyVal mv = node.get(la::MUTPTR.code);
        if (!mv.is_null() && mv.is_value()) mut = mv.as_value<uint8_t>() != 0;
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        // Phase 1B-14: `*const DstStruct` / `*mut DstStruct` → DstRef
        // (fat pointer). Same canonicalisation as REF_TYPE for DST.
        if (inner && (inner.kind() == LogosType::Kind::Struct ||
                      inner.kind() == LogosType::Kind::ZonedStruct)) {
            std::string sn(inner.struct_name());
            auto [spkg, ssi] = find_struct_by_name(sn);
            if (!ssi) { auto [dpkg, dsi] = find_datatype_by_name(sn); ssi = dsi; if (dsi) spkg = dpkg; }
            if (ssi && ssi->is_dst)
                return make_dst_ref(sn, spkg, mut);
        }
        return make_ptr(mut, inner);
    }

    if (tc == la::REF_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        // Phase 1B-11: canonicalise `&UnsizedSlice<T>` → `Slice<T>` and
        // `&UnsizedDyn<Trait>` → `TraitObject<Trait>` at resolve time too
        // (not only at substitution per 1B-2). This is needed when an
        // impl-on-unsized method body refers to `&Self` literally — the
        // resolved type must be the canonical fat-pointer form, not a
        // nested Ref<Unsized>.
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14: `&DstStruct` → Kind::DstRef (fat pointer to the
        // custom-DST struct). is_dst is on SemaStructInfo, looked up
        // by struct name.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, /*is_mut=*/false, std::move(targs));
        }
        return make_ref(false, inner, std::move(lt));
    }

    if (tc == la::MUT_REF_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        // Phase 1B-11: same canonicalisation for `&mut`.
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `&mut DstStruct` → Kind::DstRef. Includes
        // post-substitution DST (generic `?Sized` instantiation).
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, /*is_mut=*/true, std::move(targs));
        }
        return make_ref(true, inner, std::move(lt));
    }

    // Sprint 6.2 / B-ty-07: `&&T` and `&&mut T` — lexer collapses `&&`.
    if (tc == la::DOUBLE_REF_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        return make_ref(false, make_ref(false, inner));
    }
    if (tc == la::DOUBLE_REF_MUT_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        return make_ref(false, make_ref(true, inner));
    }

    if (tc == la::SLICE_TYPE) {
        auto elem = node.has_key(la::TYPE)
            ? resolve_type(map_of(node.get(la::TYPE.code)))
            : error_t();
        return make_slice_type(elem);
    }

    if (tc == la::UNSIZED_SLICE_TYPE) {
        // Phase 1B: bare `[T]` — unsized slice type. Valid only when the
        // surrounding context explicitly opts in via `unsized_ok_` (e.g. a
        // turbofish type argument bound for a `T: ?Sized` parameter — Phase
        // 1B-2). Any other position (function param/return type, local-var
        // type ascription, struct/enum field, type alias RHS, etc.) is a
        // hard error: unsized types have no size and cannot occupy value
        // positions. The `&[T]` / `*const [T]` / `*mut [T]` syntaxes are
        // handled by SLICE_TYPE above and never reach this branch.
        auto elem = node.has_key(la::TYPE)
            ? resolve_type(map_of(node.get(la::TYPE.code)))
            : error_t();
        if (!unsized_ok_) {
            error(std::format(
                "the type `[{}]` is unsized: it cannot be used by value. "
                "Wrap it in a reference (`&[{}]`) or pointer "
                "(`*const [{}]` / `*mut [{}]`).",
                type_str(elem), type_str(elem),
                type_str(elem), type_str(elem)));
            // Continue with the unsized type so downstream type-checking
            // can still produce useful diagnostics; the error above is the
            // load-bearing signal.
        }
        return make_unsized_slice_type(elem);
    }

    if (tc == la::PAREN_TYPE) {
        // B-ty-09: `(T)` — paren-wrapped type, structurally same as T.
        if (!node.has_key(la::TYPE)) return error_t();
        return resolve_type(map_of(node.get(la::TYPE.code)));
    }

    if (tc == la::TUPLE_TYPE) {
        if (!node.has_key(la::ITEMS))
            return void_t();  // () = unit/void type
        std::vector<TypeRef> elems;
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() == 0) return void_t();
        for (uint64_t i = 0; i < items.size(); ++i)
            elems.push_back(resolve_type(map_of(items.get(i))));
        return make_tuple_type(std::move(elems));
    }

    if (tc == la::DYN_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        // C5-cl-04 slice: `dyn Fn(…)` / `dyn FnMut(…)` / `dyn FnOnce(…)` —
        // Logos's Fn-family isn't a registered trait (it's a bound-check
        // shortcut), but the trait-object layout for these matches the
        // existing Closure type exactly: {fn_ptr, env_ptr}. So we resolve
        // `dyn Fn*(...) -> R` directly to Kind::Closure, which gets the
        // existing call-via-fat-pointer dispatch + Box<Closure> layout for
        // free.
        if (tname == "Fn" || tname == "FnMut" || tname == "FnOnce") {
            LogosTypeBuilder t;
            t.kind = LogosType::Kind::Closure;
            if (node.has_key(la::PARAMS)) {
                auto params_node = map_of(node.get(la::PARAMS.code));
                if (params_node.has_key(la::ITEMS)) {
                    auto items = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        t.closure_params.push_back(resolve_type(map_of(items.get(i))));
                }
            }
            t.closure_ret = node.has_key(la::RET_TYPE)
                ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
                : void_t();
            return pool_->alloc(std::move(t));
        }
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &dyn type", tname));
        // Optional type-args: &dyn Trait<T,…> — same shape as Struct<T,…>.
        std::vector<TypeRef> args;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t ic = code_of(item);
                // B63.3: HRTB binder sub-node (from `for<...>` prefix) gets
                // collected by $...; it has no CODE. Skip.
                if (ic < 0) continue;
                // L1: skip LIFETIME_PARAM at trait-arg position; lifetimes
                // aren't part of TypeUID for trait dispatch.
                if (ic == la::LIFETIME_PARAM) continue;
                args.push_back(resolve_type(item));
            }
        }
        // Phase 1B-4: at unsized-ok positions (turbofish for `T: ?Sized`),
        // bare `dyn Trait` is the unsized form — distinct from the existing
        // fat-pointer Kind::TraitObject which represents `&dyn Trait`.
        // Substitution canonicalises `&UnsizedDyn` back to TraitObject,
        // matching the `&dyn Trait` grammar route. Outside unsized-ok
        // context, behaviour is unchanged (legacy: bare `dyn Trait` and
        // `&dyn Trait` both produce TraitObject; downstream sema rejects
        // bare-by-value when it matters).
        if (unsized_ok_)
            return make_unsized_dyn_type(tname, std::move(args));
        return make_trait_object(tname, std::move(args));
    }

    if (tc == la::TAGGED_TYPE) {
        // &tagged<TS> Trait — thin pointer with tag-based dispatch.
        // struct_name = tag system type name; trait_name = dispatched trait name.
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &tagged type", tname));
        // Resolve the tag system type (used to check it's a struct/class).
        TypeRef ts_type = nullptr;
        if (node.has_key(la::TYPE.code))
            ts_type = resolve_type(map_of(node.get(la::TYPE.code)));
        std::string ts_name = ts_type ? std::string(TypeRef(ts_type).struct_name()) : "";
        if (ts_name.empty())
            error("&tagged<TS> Trait: TS must be a concrete struct type");
        LogosTypeBuilder t;
        t.kind       = LogosType::Kind::TaggedPtr;
        t.struct_name = ts_name;   // tag system type name
        t.trait_name  = tname;     // dispatched trait name
        return pool_->alloc(std::move(t));
    }

    if (tc == la::IMPL_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::ImplTrait;
        t.struct_name = tname;  // reuse struct_name to store trait name
        return pool_->alloc(std::move(t));
    }

    if (tc == la::CLOSURE_TYPE) {
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::Closure;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_->alloc(std::move(t));
    }

    if (tc == la::FN_PTR_TYPE) {
        // fn(T1, T2) -> R — bare function pointer, single ptr in LLVM.
        // Reuse closure_params / closure_ret fields.
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::FnPtr;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_->alloc(std::move(t));
    }

    if (tc == la::LIT_INT) {
        auto sv = str_of(node.get(la::VALUE.code));
        LogosTypeBuilder t; t.kind = LogosType::Kind::IntLit;
        int64_t v = parse_int_literal(sv);
        if (node.has_key(la::LO_NEG)) {
            AnyVal av = node.get(la::LO_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0)
                v = -v;
        }
        t.const_val = v;
        return pool_->alloc(std::move(t));
    }

    if (tc == la::ARR_TYPE) {
        auto elem = node.has_key(la::TYPE)
                     ? resolve_type(map_of(node.get(la::TYPE.code)))
                     : error_t();
        uint64_t n = 0;
        std::string symbolic;
        // MP-mc-01: `[T; metacall { <expr> }]` — array length via metacall
        // splice. Block tail expression evaluated by ctfe and the integer
        // result becomes the size. Logos's replacement for Rust's
        // const-eval at this position.
        if (node.has_key(la::BODY)) {
            auto blk = map_of(node.get(la::BODY.code));
            hermes::TinyMapView tail{};
            bool have_tail = false;
            if (blk.has_key(la::ITEMS)) {
                auto items = arr_of(blk.get(la::ITEMS.code));
                for (uint64_t i = items.size(); i-- > 0; ) {
                    auto s = map_of(items.get(i));
                    int32_t sc = code_of(s);
                    if ((sc == la::TAIL_EXPR || sc == la::EXPR_STMT) && s.has_key(la::VALUE)) {
                        tail = map_of(s.get(la::VALUE.code));
                        have_tail = true;
                        break;
                    }
                }
            }
            if (!have_tail) {
                error("metacall in array length must contain a single integer expression");
                return make_array(elem, 0, symbolic);
            }
            auto r = ctfe::eval_expr(tail, holder_);
            if (!r) {
                error(std::format("metacall in array length: {}", r.error().msg));
                return make_array(elem, 0, symbolic);
            }
            n = static_cast<uint64_t>(r.value().i);
            return make_array(elem, n, symbolic);
        }
        // [T; sizeof...(P)] — grammar's sizeof_pack alt encodes the pack form
        // as OP="sizeof" + NAME=<pack-ident>. Lower to a symbolic arr_size_var
        // "__sizeof_pack:P" that mono_subst resolves once P expands.
        if (node.has_key(la::OP) && node.has_key(la::NAME)) {
            auto op = std::string(str_of(node.get(la::OP.code)));
            auto pn = std::string(str_of(node.get(la::NAME.code)));
            if (op != "sizeof") {
                error(std::format("expected 'sizeof...(T)' in array size, got '{}...(T)'", op));
            } else {
                auto it = current_type_params_.find(pn);
                if (it == current_type_params_.end()) {
                    error(std::format("[T; sizeof...({})]: undefined type parameter", pn));
                } else {
                    symbolic = std::string("__sizeof_pack:") + pn;
                }
            }
            return make_array(elem, 0, symbolic);
        }
        if (node.has_key(la::SIZE)) {
            auto av = node.get(la::SIZE.code);
            auto parse_array_size = [](std::string_view sv) -> uint64_t {
                uint64_t r = 0;
                for (char c : sv) {
                    if (c >= '0' && c <= '9') r = r * 10 + (c - '0');
                    else break;
                }
                return r;
            };
            if (av.is_value()) {
                auto sv = str_of(av);
                // If sv starts with a digit, it's a literal size.
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    // Otherwise, it might be a symbolic constant parameter.
                    symbolic = std::string(sv);
                }
            } else if (av.is_pointer()) {
                // Safety fallback: if it's somehow a string object
                auto sv = str_of(av);
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    symbolic = std::string(sv);
                }
            }
        }
        return make_array(elem, n, symbolic);
    }

    if (tc == la::ASSOC_TYPE_REF) {
        // base::Item or base::Item<A,B> — associated type reference (plain or GAT)
        auto base_type = resolve_type(map_of(node.get(la::RECEIVER.code)));
        auto assoc      = std::string(str_of(node.get(la::FIELD.code)));  // "Item"
        // Read GAT type args if present (e.g. T::Item<i32>)
        std::vector<TypeRef> gat_args;
        // B88: GAT lifetime args (e.g. T::Item<'a>) — separate from gat_args
        // (which holds type-position args) so they can be matched against
        // the trait's GAT lt-params at use-site validation.
        std::vector<std::string> gat_lt_args;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto item = map_of(items.get(i));
                        if (code_of(item) == la::LIFETIME_PARAM) {
                            auto name_av = item.get(la::NAME.code);
                            if (!name_av.is_null()) {
                                std::string lt(str_of(name_av));
                                gat_lt_args.push_back(std::move(lt));
                            }
                            continue;
                        }
                        gat_args.push_back(resolve_type(item));
                    }
                }
            }
        }
        std::string trait_for_assoc;

        if (TypeRef(base_type).kind() == LogosType::Kind::TypeVar) {
            auto tp_name = TypeRef(base_type).type_var_name();
            if (tp_name == "Self" && !current_trait_name_.empty()) {
                trait_for_assoc = current_trait_name_;
            } else {
                auto bit = current_type_bounds_.find(tp_name);
                if (bit != current_type_bounds_.end()) {
                    // Walk each bound trait AND its supertrait chain to find
                    // the assoc type. A `Container: Datatype` bound pulls in
                    // Datatype's `View` through the supertrait edge.
                    std::vector<std::string> worklist;
                    StrSet seen;
                    for (auto& b : bit->second) worklist.push_back(b.trait_name);
                    while (!worklist.empty() && trait_for_assoc.empty()) {
                        std::string tn = std::move(worklist.back());
                        worklist.pop_back();
                        if (!seen.insert(tn).second) continue;
                        auto tit = traits_.find(tn);
                        if (tit == traits_.end()) continue;
                        for (auto& at : tit->second.assoc_types) {
                            if (at.name == assoc) { trait_for_assoc = tn; break; }
                        }
                        if (!trait_for_assoc.empty()) break;
                        for (auto& sup : tit->second.supertraits)
                            worklist.push_back(sup.trait_name);
                    }
                }
            }
        } else if (TypeRef(base_type).kind() == LogosType::Kind::CfgSlotType) {
            // CfgSlotType base — type isn't known until mono substitutes
            // CFG. Resolve by assoc-name alone: pick the first trait that
            // declares an assoc type with this name. Mono's subst_type
            // for AssocType then resolves via concrete_impls_ /
            // blanket_impls_ once the base becomes concrete.
            for (auto& [tname, tinfo] : traits_) {
                for (auto& at : tinfo.assoc_types) {
                    if (at.name == assoc) { trait_for_assoc = tname; break; }
                }
                if (!trait_for_assoc.empty()) break;
            }
        } else if (TypeRef(base_type).kind() == LogosType::Kind::AssocType) {
            // T::A::B — search bounds of the associated type itself if we had them,
            // but currently we only store trait_name for the assoc type.
            // We'll search the trait indicated by base_type's own resolution.
            auto tit = traits_.find(TypeRef(base_type).trait_name());
            if (tit != traits_.end()) {
                // This is slightly wrong: T::A might be bound to traits OTHER than the one it's defined in.
                // But our current system doesn't support "type Item: Bound;".
                // So we look in the trait that owns the associated type.
            }
            // Fallback: check all traits implemented by the concrete type if base is already concrete,
            // or just error if we can't find it.
        }

        // Phase 6: check the current impl's trait first. When `Self::Item<X>`
        // appears inside an impl method body (or signature), `Self` resolves
        // to the impl's target type (concrete struct, not TypeVar), and the
        // impl itself isn't yet in impls_ when collect_impl is still walking
        // method signatures. Look up the assoc-type definition on the
        // impl's trait directly.
        if (trait_for_assoc.empty() && !current_impl_trait_name_.empty()) {
            auto tit = traits_.find(current_impl_trait_name_);
            if (tit != traits_.end()) {
                for (auto& at : tit->second.assoc_types) {
                    if (at.name == assoc) {
                        trait_for_assoc = current_impl_trait_name_;
                        break;
                    }
                }
            }
        }
        if (trait_for_assoc.empty()) {
            // Check all traits for ANY type that might have this assoc type (last resort lookup).
            // Try both the full concrete name (e.g. "Box<i32>") and the base struct name ("Box")
            // to handle generic impls like impl<V> Trait for Box<V>.
            std::string cname = type_str(base_type);
            std::string base_name;
            if (TypeRef(base_type).kind() == LogosType::Kind::Struct ||
                TypeRef(base_type).kind() == LogosType::Kind::ZonedStruct)
                base_name = TypeRef(base_type).struct_name();

            for (auto& [tname, tinfo] : traits_) {
                bool found_impl = impls_.count(tname + "::" + cname) > 0
                               || (!base_name.empty() && impls_.count(tname + "::" + base_name) > 0);
                if (found_impl) {
                    for (auto& at : tinfo.assoc_types) {
                        if (at.name == assoc) { trait_for_assoc = tname; break; }
                    }
                }
                if (!trait_for_assoc.empty()) break;
            }
        }

        if (trait_for_assoc.empty()) {
            error(std::format("no associated type '{}' found for '{}'", assoc, type_str(base_type)));
            return error_t();
        }
        // Bug 5 fix: check GAT arity against the trait's declaration.
        auto tit_gat = traits_.find(trait_for_assoc);
        if (tit_gat != traits_.end()) {
            for (auto& at_def : tit_gat->second.assoc_types) {
                if (at_def.name == assoc) {
                    size_t expected_gat = at_def.type_params.size();
                    if (!at_def.type_params.empty() && gat_args.size() != expected_gat)
                        error(std::format("associated type '{}::{}' expects {} GAT argument(s), got {}",
                                          trait_for_assoc, assoc, expected_gat, gat_args.size()));
                    // Enforce trait bounds on GAT type parameters.
                    if (!at_def.type_params.empty() && gat_args.size() == expected_gat)
                        check_type_bounds(trait_for_assoc + "::" + assoc,
                                          at_def.type_params, gat_args);
                    break;
                }
            }
        }
        LogosTypeBuilder t;
        t.kind            = LogosType::Kind::AssocType;
        t.assoc_base      = base_type;
        t.trait_name      = trait_for_assoc;
        t.assoc_type_name = assoc;
        t.gat_args        = std::move(gat_args);
        // B88: stash GAT lifetime args on lifetime_args field — distinct
        // from struct lt args (AssocType doesn't have struct lt_args
        // semantics) so reusing the slot is safe and lets the existing
        // lifetime_args mirror accessor read them.
        t.lifetime_args   = std::move(gat_lt_args);

        auto result = pool_->alloc(std::move(t));
        // Propagate bounds for T::Item back into the context
        auto tit = traits_.find(trait_for_assoc);
        if (tit != traits_.end()) {
            for (auto& at : tit->second.assoc_types) {
                if (at.name == assoc && !at.bounds.empty()) {
                    current_type_bounds_[type_str(result)] = at.bounds;
                    break;
                }
            }
        }
        return result;
    }

    // <ElemType>[] and <K,V>{} — Hermes typed container type-expressions.
    // Resolved to a special Struct type: struct_name="HermesArr"/"HermesMap",
    // type_args[0] = elem/key type, type_args[1] = val type (map only).
    // The result type of an `as <T>[]` cast is always Hermes (owning zone).
    if (tc == la::HERMES_ARR_TYPE) {
        auto elem_name = str_of(node.get(la::TYPE.code));
        // Resolve element type — must be a known Hermes scalar type name.
        static const StrMap<const char*> arr_elem_map = {
            {"I8",  "ArrayI8"},  {"U8",  "ArrayU8"},
            {"I16", "ArrayI16"}, {"U16", "ArrayU16"},
            {"U32", "ArrayU32"}, {"I32", "ArrayI32"},
            {"I64", "ArrayI64"}, {"U64", "ArrayU64"},
            {"F32", "ArrayF32"}, {"F64", "ArrayF64"},
        };
        auto it = arr_elem_map.find(std::string(elem_name));
        if (it == arr_elem_map.end()) {
            error(std::format("<{}>[] type: unsupported element type '{}'; "
                              "supported: I8/U8/I16/U16/I32/U32/I64/U64/F32/F64",
                              elem_name, elem_name));
            return error_t();
        }
        // Resolve the underlying logos primitive type for the element.
        TypeRef elem_t = nullptr;
        if      (elem_name == "I8")  elem_t = prim(LogosType::Kind::I8);
        else if (elem_name == "U8")  elem_t = prim(LogosType::Kind::U8);
        else if (elem_name == "I16") elem_t = prim(LogosType::Kind::I16);
        else if (elem_name == "U16") elem_t = prim(LogosType::Kind::U16);
        else if (elem_name == "U32") elem_t = prim(LogosType::Kind::U32);
        else if (elem_name == "I32") elem_t = prim(LogosType::Kind::I32);
        else if (elem_name == "I64") elem_t = prim(LogosType::Kind::I64);
        else if (elem_name == "U64") elem_t = prim(LogosType::Kind::U64);
        else if (elem_name == "F32") elem_t = prim(LogosType::Kind::F32);
        else if (elem_name == "F64") elem_t = prim(LogosType::Kind::F64);
        else elem_t = error_t();
        // Result type: struct LogosType with special name "HermesArr".
        return make_generic_struct("HermesArr", {elem_t});
    }
    if (tc == la::HERMES_MAP_TYPE) {
        auto key_name = str_of(node.get(la::TYPE.code));
        auto val_name = node.has_key(la::RET_TYPE.code)
            ? str_of(node.get(la::RET_TYPE.code)) : std::string_view{"AnyVal"};
        // C6-fix1: removed "Varchar" — it was advertised as supported but key_t
        // resolution only handled "I32", producing silent error_t() for Varchar.
        static const StrMap<const char*> map_key_map = {
            {"I32", "I32"}, {"U32", "U32"}, {"I64", "I64"}, {"U64", "U64"},
        };
        if (map_key_map.find(std::string(key_name)) == map_key_map.end()) {
            error(std::format("<{},{}>" "{{}} type: unsupported key type '{}'; "
                              "supported: I32/U32/I64/U64", key_name, val_name, key_name));
            return error_t();
        }
        TypeRef key_t = nullptr;
        if      (key_name == "I32") key_t = prim(LogosType::Kind::I32);
        else if (key_name == "U32") key_t = prim(LogosType::Kind::U32);
        else if (key_name == "I64") key_t = prim(LogosType::Kind::I64);
        else if (key_name == "U64") key_t = prim(LogosType::Kind::U64);
        else key_t = error_t();
        TypeRef val_t = nullptr;
        if (val_name == "AnyVal") {
            val_t = make_struct_type("AnyVal");
        } else {
            // C6-fix2: emit error for unsupported val type (previously silent error_t()).
            error(std::format("<{},{}>" "{{}} type: unsupported val type '{}'; "
                              "supported: AnyVal", key_name, val_name, val_name));
            return error_t();
        }
        return make_generic_struct("HermesMap", {key_t, val_t});
    }

    if (tc == la::PACK_EXPAND) {
        // T... in type-arg position — refer to a variadic type parameter
        // currently in scope. Sema yields the TypeVar; mono expands it via
        // cur_packs_ at call sites that iterate type_args.
        auto name = std::string(str_of(node.get(la::NAME.code)));
        auto it = current_type_params_.find(name);
        if (it == current_type_params_.end()) {
            error(std::format("pack expand: undefined type parameter '{}'", name));
            return error_t();
        }
        return it->second;
    }

    if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        if (name == "Self") {
            auto tvit = current_type_params_.find("Self");
            if (tvit != current_type_params_.end()) return tvit->second;
        }
        auto t = lookup_type_by_name(name);
        if (t) return t;
        // See #20 sister site below: in metaprog discovery loop, swallow
        // unknown-type silently across all asts — the type may be
        // synthesised by a hook later. Final non-metaprog sema pass
        // catches real errors.
        if (metaprog_mode_)
            return error_t();
        // Bug 4 fix: give a more informative error when a generic alias is used
        // without its required type arguments.
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end() &&
            (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty()))
            error(std::format("generic type alias '{}' requires type arguments", name));
        else
            error(std::format("unknown type '{}'", name));
        return error_t();
    }

    if (tc == la::GENERIC_INST) {
        auto name = str_of(node.get(la::NAME.code));

        // Generic compile-time const: `pub const X<T1, T2>: HermesStatic =
        // @{...};`. Push type-args into current_type_params_ and re-resolve
        // the saved value-AST under that scope. resolve_hstatic_value walks
        // the AST and substitutes TypeVar HERMES_TYPE_LIT names through
        // current_type_params_, producing a fresh per-instantiation
        // HStaticLit identity.
        {
            auto git = generic_consts_.find(std::string(name));
            if (git != generic_consts_.end()) {
                std::vector<TypeRef> args;
                if (node.has_key(la::ITEMS)) {
                    auto items = arr_of(node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        args.push_back(resolve_type(map_of(items.get(i))));
                }
                if (args.size() != git->second.type_params.size()) {
                    error(std::format("generic const '{}' expects {} type argument(s), got {}",
                                      name, git->second.type_params.size(), args.size()));
                    return error_t();
                }
                // Save + push type-param bindings.
                StrMap<TypeRef> saved_params;
                for (size_t i = 0; i < args.size(); ++i) {
                    const std::string& pname = git->second.type_params[i].name;
                    auto it = current_type_params_.find(pname);
                    if (it != current_type_params_.end()) saved_params[pname] = it->second;
                    current_type_params_[pname] = args[i];
                }
                // Switch holder_ to the const decl's holder so arr_of/map_of
                // resolve offsets against the correct base. Restored after.
                auto* saved_holder = holder_;
                if (git->second.holder) holder_ = git->second.holder;
                TypeRef result = resolve_hstatic_value(git->second.value_node);
                holder_ = saved_holder;
                // Restore type-params.
                for (size_t i = 0; i < args.size(); ++i) {
                    const std::string& pname = git->second.type_params[i].name;
                    auto sit = saved_params.find(pname);
                    if (sit != saved_params.end()) current_type_params_[pname] = sit->second;
                    else current_type_params_.erase(pname);
                }
                return result;
            }
        }

        // Generic type alias: type Foo<T> = Bar<T>;  →  Foo<i32> resolves to Bar<i32>
        {
            auto ait = type_aliases_.find(std::string(name));
            if (ait != type_aliases_.end() &&
                (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty())) {
                // Resolve type and lifetime arguments at the call site.
                std::vector<TypeRef> args;
                std::vector<std::string> lt_args;
                if (node.has_key(la::ITEMS)) {
                    auto items = arr_of(node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto item = map_of(items.get(i));
                        if (code_of(item) == la::LIFETIME_PARAM) {
                            lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                            continue;
                        }
                        args.push_back(resolve_type(item));
                    }
                }
                size_t expected = ait->second.type_params.size();
                if (args.size() != expected)
                    error(std::format("type alias '{}' expects {} type argument(s), got {}",
                                      name, expected, args.size()));
                size_t lt_expected = ait->second.lifetime_params.size();
                if (lt_args.size() != lt_expected)
                    error(std::format("type alias '{}' expects {} lifetime argument(s), got {}",
                                      name, lt_expected, lt_args.size()));
                SemaSubst s;
                for (size_t i = 0; i < expected && i < args.size(); ++i)
                    s[ait->second.type_params[i].name] = args[i];
                SemaLifetimeSubst ls;
                auto& lparams = ait->second.lifetime_params;
                for (size_t i = 0; i < lparams.size() && i < lt_args.size(); ++i)
                    ls[lparams[i]] = lt_args[i];
                return subst_type_sema(ait->second.type, s, ls);
            }
        }

        // Special case: Box<dyn Trait> = owned trait object (same layout as &dyn Trait)
        if (name == "Box" && node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            if (items.size() == 1) {
                auto inner = resolve_type(map_of(items.get(0)));
                if (inner && TypeRef(inner).kind() == LogosType::Kind::TraitObject)
                    return inner;  // Box<dyn T> ≡ &dyn T in our type system
            }
        }
        auto [spkg, ssi] = find_struct_by_name(name);
        auto [dpkg, dsi] = find_datatype_by_name(name);
        auto [epkg, esi] = find_enum_by_name(name);
        // Cross-pkg ambiguity: user's local `pub struct Foo` shadowing a
        // datatype `#[zoned] pub struct Foo` from an imported package
        // would otherwise lose to the datatype because the dispatch
        // below checks is_dtype before is_struct. Pin to cur_package_
        // when one kind is local and the other isn't.
        if (!cur_package_.empty()) {
            bool s_local = ssi && spkg == cur_package_;
            bool d_local = dsi && dpkg == cur_package_;
            bool e_local = esi && epkg == cur_package_;
            if (s_local && !d_local) { dsi = nullptr; }
            if (s_local && !e_local) { esi = nullptr; }
            if (d_local && !s_local) { ssi = nullptr; }
            if (d_local && !e_local) { esi = nullptr; }
            if (e_local && !s_local) { ssi = nullptr; }
            if (e_local && !d_local) { dsi = nullptr; }
        }
        bool is_struct = ssi != nullptr;
        bool is_dtype  = dsi != nullptr;
        bool is_enum   = esi != nullptr;
        if (!is_struct && !is_dtype && !is_enum) {
            // Metaprog discovery loop runs sema BEFORE handler hooks
            // emit derived items. Unknown types referenced from any
            // user-side ast may be ones a hook will synthesise — the
            // final, non-metaprog sema pass after the loop will
            // re-resolve and surface a real error if the type still
            // doesn't exist. Silently fall through here.
            if (metaprog_mode_)
                return error_t();
            error(std::format("unknown generic type '{}'", name));
            return error_t();
        }
        // Resolve each type arg (TypeVars in current scope are expanded).
        // Collect LIFETIME_PARAM items ('a) separately — erased at codegen but
        // tracked for borrow checking (struct fields that borrow through a lifetime).
        // Phase 1B-5: when the target type-param at index i has
        // `implicit_sized=false` (declared with `?Sized`), enable unsized_ok_
        // for that arg's resolution so bare `[T]` / `dyn Trait` parse without
        // the unsized-by-value diagnostic. The Sized-enforcement check below
        // catches the inverse case (unsized arg at sized param).
        const std::vector<TypeParam>* target_params =
            ssi ? &ssi->type_params :
            dsi ? &dsi->type_params :
            esi ? &esi->type_params : nullptr;
        std::vector<TypeRef> args;
        std::vector<std::string> lt_args;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            size_t type_arg_idx = 0;  // separate index — lifetimes don't consume a param slot
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) == la::LIFETIME_PARAM) {
                    lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                    continue;
                }
                bool was_ok = unsized_ok_;
                if (target_params && type_arg_idx < target_params->size() &&
                    !(*target_params)[type_arg_idx].implicit_sized) {
                    unsized_ok_ = true;
                }
                args.push_back(resolve_type(item));
                unsized_ok_ = was_ok;
                ++type_arg_idx;
            }
        }
        // Phase 1B-5/10: Sized-enforcement at struct/enum/datatype generic
        // instantiation. Parallel to the fn-call path in finish_generic_call.
        // Phase 1B-10 adds the TypeVar→Sized propagation check too.
        if (target_params) {
            for (size_t i = 0; i < args.size() && i < target_params->size(); ++i) {
                if (!(*target_params)[i].implicit_sized) continue;
                auto t = args[i];
                if (!t) continue;
                auto k = t.kind();
                if (k == LogosType::Kind::UnsizedSlice ||
                    k == LogosType::Kind::UnsizedDyn) {
                    error(std::format(
                        "generic '{}': type argument '{}' has unsized type `{}` "
                        "but the type parameter '{}' requires `Sized` "
                        "(add `T: ?Sized` to relax the bound)",
                        name, type_str(t), type_str(t),
                        (*target_params)[i].name));
                } else if (k == LogosType::Kind::TypeVar) {
                    std::string tvname(t.type_var_name());
                    if (current_type_relaxed_sized_.count(tvname)) {
                        error(std::format(
                            "generic '{}': type argument '{}' is a `?Sized` "
                            "outer type parameter; cannot be passed to '{}' "
                            "which requires `Sized` (add `?Sized` to the "
                            "target's bound or constrain the outer parameter "
                            "to `Sized`)",
                            name, tvname, (*target_params)[i].name));
                    }
                }
            }
        }
        if (is_enum) {
            if (esi) {
                check_type_arg_arity(name, esi->type_params, args, "enum");
                check_type_bounds(std::string(name), esi->type_params, args);
            }
            return make_generic_enum(name, std::move(args), std::move(lt_args), epkg);
        }
        if (is_dtype) {
            if (dsi) {
                check_type_arg_arity(name, dsi->type_params, args, "datatype");
                check_type_bounds(std::string(name), dsi->type_params, args);
            }
            return make_generic_datatype(name, std::move(args), std::move(lt_args), dpkg);
        }
        if (ssi) {
            check_type_arg_arity(name, ssi->type_params, args, "struct");
            check_type_bounds(std::string(name), ssi->type_params, args);
        }
        return make_generic_struct(name, std::move(args), std::move(lt_args), spkg);
    }

    if (tc == la::LIT_HSTATIC) {
        // HermesStatic literal at type-arg position: Foo::<@{...}>.
        if (!node.has_key(la::VALUE)) {
            error("HermesStatic type-arg: missing literal payload");
            return error_t();
        }
        return resolve_hstatic_value(map_of(node.get(la::VALUE.code)));
    }
    // Bare hermes-lit codes also reach resolve_type when `pub const X:
    // HermesStatic = @{...}` is being recognised in collect_const — there
    // the value-AST is the unwrapped hermes_lit, not LIT_HSTATIC.
    if (tc == la::HERMES_MAP.code || tc == la::HERMES_ARRAY.code ||
        tc == la::HERMES_STR.code || tc == la::HERMES_INT.code ||
        tc == la::HERMES_NEG_INT.code || tc == la::HERMES_FLOAT.code ||
        tc == la::HERMES_BOOL.code || tc == la::HERMES_NULL.code) {
        return resolve_hstatic_value(node);
    }

    error(std::format("unexpected type node code {}", tc));
    return error_t();
}

TypeRef SemaChecker::resolve_hstatic_value(TinyMapView val_node) {
    // Identity = byte-hash over the AST (content only, position-free) so two
    // identical `@{...}` instances at different sites produce the same TypeRef.
    {
        // FNV-1a 64-bit hash, schema-aware (content only, position-free).
        // Walks the hermes_lit AST tree using each node CODE's known shape
        // — distinguishes string-valued (HERMES_INT/STR/FLOAT) from
        // map-valued (HERMES_ENTRY's VALUE) children, so identical content
        // at different source positions hashes to the same value.
        auto fnv_byte = [](uint64_t h, uint8_t b) {
            return (h ^ b) * 0x100000001b3ULL;
        };
        auto fnv_u64 = [&](uint64_t h, uint64_t x) {
            for (int i = 0; i < 8; ++i) { h = fnv_byte(h, (uint8_t)(x & 0xff)); x >>= 8; }
            return h;
        };
        auto fnv_str = [&](uint64_t h, std::string_view s) {
            h = fnv_u64(h, (uint64_t)s.size());
            for (char c : s) h = fnv_byte(h, (uint8_t)c);
            return h;
        };
        std::function<uint64_t(hermes::TinyMapView, uint64_t)> walk;
        walk = [&](hermes::TinyMapView n, uint64_t h) -> uint64_t {
            int32_t c = code_of(n);
            h = fnv_u64(h, (uint64_t)(int64_t)c);
            if (c == la::HERMES_MAP.code || c == la::HERMES_ARRAY.code) {
                if (n.has_key(la::ITEMS) && !n.get(la::ITEMS.code).is_null()) {
                    auto items = arr_of(n.get(la::ITEMS.code));
                    h = fnv_u64(h, (uint64_t)items.size());
                    // B-he-02: duplicate-key check at this layer (was only done
                    // for `pub const … = @{...}` via eval_static_hermes_lit;
                    // hstatic literals at type-arg position skipped through here
                    // unchecked).
                    if (c == la::HERMES_MAP.code) {
                        logos::compiler::StrSet seen_keys;
                        for (uint64_t i = 0; i < items.size(); ++i) {
                            auto entry = map_of(items.get(i));
                            if (code_of(entry) != la::HERMES_ENTRY.code) continue;
                            if (!entry.has_key(la::KEY)) continue;
                            auto raw = str_of(entry.get(la::KEY.code));
                            std::string key(raw);
                            if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
                                key = key.substr(1, key.size() - 2);
                            if (key.empty()) continue;
                            if (!seen_keys.insert(key).second) {
                                error(std::format("duplicate key '{}' in Hermes map literal", key));
                            }
                        }
                    }
                    for (uint64_t i = 0; i < items.size(); ++i)
                        h = walk(map_of(items.get(i)), h);
                }
            } else if (c == la::HERMES_ENTRY.code) {
                if (n.has_key(la::KEY))
                    h = fnv_str(h, str_of(n.get(la::KEY.code)));
                if (n.has_key(la::VALUE))
                    h = walk(map_of(n.get(la::VALUE.code)), h);
                if (n.has_key(la::LO_NEG)) {
                    auto av = n.get(la::LO_NEG.code);
                    if (av.is_value() && av.as_value<uint8_t>() != 0) h = fnv_byte(h, 1);
                }
            } else if (c == la::HERMES_INT.code || c == la::HERMES_NEG_INT.code ||
                       c == la::HERMES_FLOAT.code || c == la::HERMES_STR.code) {
                if (n.has_key(la::VALUE))
                    h = fnv_str(h, str_of(n.get(la::VALUE.code)));
            } else if (c == la::HERMES_BOOL.code) {
                if (n.has_key(la::VALUE)) {
                    auto av = n.get(la::VALUE.code);
                    if (av.is_value() && av.as_value<uint8_t>() != 0) h = fnv_byte(h, 1);
                }
            } else if (c == la::HERMES_TYPE_LIT.code) {
                // 3a': grammar feeds a simple_type child via TYPE — resolve
                // it with current_type_params_ in scope and hash the
                // canonical type_str. That subsumes the legacy NAME-only
                // path since type-param substitution flows through
                // resolve_type → lookup_type_by_name.
                if (n.has_key(la::TYPE)) {
                    auto type_node = map_of(n.get(la::TYPE.code));
                    TypeRef t = resolve_type(type_node);
                    if (t) h = fnv_str(h, type_str(t));
                } else if (n.has_key(la::NAME)) {
                    // Legacy AST shape (kept for safety; pre-3a' grammar).
                    auto nm = str_of(n.get(la::NAME.code));
                    auto pit = current_type_params_.find(std::string(nm));
                    if (pit != current_type_params_.end() && pit->second) {
                        h = fnv_str(h, type_str(pit->second));
                    } else {
                        h = fnv_str(h, nm);
                    }
                }
            }
            // HERMES_NULL / unknown: code-only contribution (already mixed in).
            return h;
        };
        uint64_t hash = walk(val_node, 0xcbf29ce484222325ULL);
        // Register the lowered @-literal so mono can materialise it in
        // place of `__const_param:CFG` references inside generic bodies.
        // First-write-wins: identical hashes resolve to the same registered
        // LExpr (content-only identity).
        if (cur_prog_ && !cur_prog_->hstatic_registry_.count(hash)) {
            auto lit = lower_hermes_lit(val_node);
            if (lit) cur_prog_->hstatic_registry_[hash] = lit;
        }
        LogosTypeBuilder t; t.kind = LogosType::Kind::HStaticLit;
        t.const_val = (int64_t)hash;  // bit-pattern reuse; mangling reads it as u64
        return pool_->alloc(std::move(t));
    }
}

// ── Lowering helpers ─────────────────────────────────────────────────────────

// Match `concrete` against `pattern`, binding TypeVars.  Minimal mirror of
// mono's match_type — only the cases we need for struct-spec selection.
static bool match_type_sema(TypeRef c, TypeRef p,
                            StrMap<TypeRef>& bindings) {
    if (!c || !p) return false;
    if (p.kind() == LogosType::Kind::TypeVar) {
        auto it = bindings.find(p.type_var_name());
        if (it != bindings.end()) return types_equal(c, TypeRef(it->second));
        bindings[std::string(p.type_var_name())] = c;
        return true;
    }
    if (p.kind() != c.kind()) return false;
    switch (p.kind()) {
    case LogosType::Kind::Ptr:
        return p.mut_ptr() == c.mut_ptr() &&
               match_type_sema(c.pointee(), p.pointee(), bindings);
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        return match_type_sema(c.pointee(), p.pointee(), bindings);
    case LogosType::Kind::Array:
        return p.arr_size() == c.arr_size() &&
               match_type_sema(c.elem(), p.elem(), bindings);
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        return p.struct_name() == c.struct_name();
    default:
        return types_equal(c, p);
    }
}

static int specificity_sema(TypeRef t) {
    if (!t || t.kind() == LogosType::Kind::TypeVar) return 0;
    if (t.kind() == LogosType::Kind::Ptr)   return 1 + specificity_sema(t.pointee());
    if (t.kind() == LogosType::Kind::Array) return 1 + specificity_sema(t.elem());
    return 100;
}

// Find the most specific spec in struct_specs_sema_ whose patterns match
// `type_args` under template `base_name`.  Returns nullptr if none match.
const SemaChecker::SemaStructInfo* SemaChecker::find_best_sema_struct_spec(
        std::string_view base_name,
        const std::vector<TypeRef>& type_args) {
    const SemaStructInfo* best      = nullptr;
    std::vector<int>      best_vec;
    for (auto& [key, info] : struct_specs_sema_) {
        if (info.base_name != base_name) continue;
        if (info.spec_patterns.size() != type_args.size()) continue;
        StrMap<TypeRef> binds;
        bool ok = true;
        std::vector<int> scores(type_args.size());
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type_sema(type_args[i], info.spec_patterns[i], binds)) { ok = false; break; }
            scores[i] = specificity_sema(info.spec_patterns[i]);
        }
        if (!ok) continue;
        // Lexicographic comparison: prefer higher specificity at earlier positions.
        if (!best || scores > best_vec) { best = &info; best_vec = scores; }
    }
    return best;
}

TypeRef SemaChecker::field_type_of(std::string_view sname, std::string_view fname,
                                             std::string_view pkg_hint) {
    SemaStructInfo* si = nullptr;
    // If we have a pkg_hint, try the fully-qualified key first (avoids import-scope dependence).
    if (!pkg_hint.empty()) {
        auto qkey = sema_key(std::string(pkg_hint), std::string(sname));
        auto sit = structs_.find(qkey);
        if (sit != structs_.end()) si = &sit->second;
        if (!si) { auto dit = datatypes_.find(qkey); if (dit != datatypes_.end()) si = &dit->second; }
    }
    if (!si) { auto [pkg, ssi] = find_struct_by_name(sname); si = ssi; }
    if (!si) { auto [pkg, dsi] = find_datatype_by_name(sname); si = dsi; }
    if (!si) return nullptr;
    for (auto& f : si->fields) {
        if (f.name == fname) return f.type;
        if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
            return f.type;
    }
    return nullptr;
}

TypeRef SemaChecker::field_type_of_for_type(TypeRef struct_t,
                                             std::string_view fname) {
    if (!struct_t || (TypeRef(struct_t).kind() != LogosType::Kind::Struct &&
                      TypeRef(struct_t).kind() != LogosType::Kind::ZonedStruct)) return nullptr;
    // Check for a concrete specialization first (including partial specs
    // via pattern matching).
    if (!TypeRef(struct_t).type_args().empty()) {
        if (auto* spec = find_best_sema_struct_spec(TypeRef(struct_t).struct_name(), TypeRef(struct_t).type_args())) {
            for (auto& f : spec->fields) {
                if (f.name == fname) return f.type;
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                    return f.type;
            }
            return nullptr;  // field not in specialization
        }
    }
    auto raw = field_type_of(TypeRef(struct_t).struct_name(), fname, TypeRef(struct_t).pkg_name());
    if (!raw || TypeRef(struct_t).type_args().empty()) return raw;

    // If it's a variadic expansion (name_N), we need to resolve it against the type arguments.
    if (fname.find('_') != std::string::npos) {
        SemaStructInfo* si2 = nullptr;
        { auto [pkg, ssi] = find_struct_by_name(TypeRef(struct_t).struct_name()); si2 = ssi; }
        if (!si2) { auto [pkg, dsi] = find_datatype_by_name(TypeRef(struct_t).struct_name()); si2 = dsi; }
        if (si2) {
            for (auto& f : si2->fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    size_t idx = std::stoull(std::string(fname.substr(f.name.size() + 1)));
                    if (f.type && TypeRef(f.type).kind() == LogosType::Kind::TypeVar) {
                        for (size_t i = 0, arg_idx = 0; i < si2->type_params.size(); ++i) {
                            if (si2->type_params[i].is_variadic) {
                                if (si2->type_params[i].name == TypeRef(f.type).type_var_name()) {
                                    if (arg_idx + idx < TypeRef(struct_t).type_args().size())
                                        return TypeRef(struct_t).type_args()[arg_idx + idx];
                                }
                                break;
                            } else {
                                arg_idx++;
                            }
                        }
                    }
                    return raw;
                }
            }
        }
    }

    // Bug 5: look up structs_ OR datatypes_ for the substitution info.
    // Try bare name first (same-package/unqualified), then pkg_name-qualified key.
    SemaStructInfo* si2 = nullptr;
    { auto it = structs_.find(TypeRef(struct_t).struct_name()); if (it != structs_.end()) si2 = &it->second; }
    if (!si2) { auto it = datatypes_.find(TypeRef(struct_t).struct_name()); if (it != datatypes_.end()) si2 = &it->second; }
    if (!si2 && !TypeRef(struct_t).pkg_name().empty()) {
        auto qkey = sema_key(TypeRef(struct_t).pkg_name(), TypeRef(struct_t).struct_name());
        { auto it = structs_.find(qkey); if (it != structs_.end()) si2 = &it->second; }
        if (!si2) { auto it = datatypes_.find(qkey); if (it != datatypes_.end()) si2 = &it->second; }
    }
    if (!si2) return raw;
    SemaSubst subst;
    auto& tps2 = si2->type_params;
    for (size_t i = 0, j = 0; i < tps2.size() && j < TypeRef(struct_t).type_args().size(); ++i) {
        if (tps2[i].is_variadic) break;
        subst[tps2[i].name] = TypeRef(struct_t).type_args()[j++];
    }
    // Bug 4: build lifetime substitution so &'z T fields resolve to caller's lifetime.
    SemaLifetimeSubst ls;
    auto& lps = si2->lifetime_params;
    for (size_t i = 0; i < lps.size() && i < TypeRef(struct_t).lifetime_args().size(); ++i)
        ls[lps[i]] = TypeRef(struct_t).lifetime_args()[i];
    return subst_type_sema(raw, subst, ls);
}

// ── lower_program and lower_module_items ─────────────────────────────────────

void SemaChecker::lower_program(const std::vector<hermes::Hermes>& asts, lir::LProgram& prog) {
    using namespace ast;
    // B64: variance fixed-point — runs after collect_*, before any lower_fn
    // (the subtype check at return / arg / let-init sites consults the table).
    compute_variances();
    for (size_t i = 0; i < asts.size(); ++i) {
        cur_ast_idx_ = i;
        holder_ = asts[i].holder();
        file_ = (filenames_ && i < filenames_->size()) ? (*filenames_)[i] : std::string{};
        cur_from_binary_ = (from_binary_ && i < from_binary_->size()) ? (*from_binary_)[i] : false;
        auto root = asts[i].root_object().as_tiny_map();
        cur_root_ = root;
        cur_package_ = read_package_name(root);
        // Rebuild import scope (same logic as in collect()) so find_*_by_name
        // works during lowering for cross-package type lookups.
        cur_imports_ = {};
        if (root.has_key(USES)) {
            auto uses_av = root.get(USES.code);
            if (!uses_av.is_null() && uses_av.is_pointer()) {
                auto uses = arr_of(uses_av);
                for (uint64_t ui = 0; ui < uses.size(); ++ui) {
                    auto use_node = map_of(uses.get(ui));
                    std::string dotted;
                    if (use_node.has_key(NAME))
                        dotted = std::string(str_of(use_node.get(NAME.code)));
                    if (use_node.has_key(mod::PATH_PARTS)) {
                        auto parts = arr_of(use_node.get(mod::PATH_PARTS.code));
                        for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                            auto part = map_of(parts.get(pi));
                            if (!part.has_key(NAME)) continue;
                            if (!dotted.empty()) dotted += '.';
                            dotted += std::string(str_of(part.get(NAME.code)));
                        }
                    }
                    if (!dotted.empty())
                        cur_imports_.wildcard_packages.push_back(std::move(dotted));
                }
            }
        }
        lower_module_items(root, prog);
    }
    cur_package_ = {};
    cur_imports_ = {};

    // ── Impl-method re-attachment pass ─────────────────────────────
    // lower_impl_block tries to attach methods to their target struct's
    // template by scanning prog.structs at impl-lowering time. When the
    // struct is in a *later-processed* ast (typical for derive-emitted
    // structs in synth docs appended at the end), the lookup misses
    // and methods land in prog.functions. mono's
    // struct_method_templates_ is built from struct.methods only, so
    // those orphaned methods are never cloned for concrete struct
    // instantiations → dyn vtable lookup fails.
    //
    // Walk prog.functions for impl-method-shaped names
    // (`<Struct>__<method>__[fg]__<sig>`) whose <Struct> exists as a
    // generic template in prog.structs. Move into struct.methods.
    {
        // O(1) lookup: bare struct name → template LStructDef*.
        std::unordered_map<std::string, lir::LStructDef*> templates_by_name;
        for (auto& sd : prog.structs) {
            if (sd.type_params.empty()) continue;
            templates_by_name.emplace(sd.name, &sd);  // first wins
        }
        auto is_impl_method_shape = [](std::string_view nm) -> std::string_view {
            if (auto dot = nm.rfind('.'); dot != std::string_view::npos)
                nm.remove_prefix(dot + 1);
            auto sep = nm.find("__");
            if (sep == std::string_view::npos) return {};
            // Require __f__ or __g__ further along — distinguishes impl
            // methods from coincidentally-named free fns.
            if (nm.find("__f__", sep) == std::string_view::npos &&
                nm.find("__g__", sep) == std::string_view::npos)
                return {};
            auto base = nm.substr(0, sep);
            if (base.empty() || base[0] == '$') return {};
            return base;
        };
        std::vector<std::unique_ptr<lir::LFunction>> kept;
        kept.reserve(prog.functions.size());
        for (auto& fp : prog.functions) {
            if (!fp) continue;
            auto base = is_impl_method_shape(fp->name);
            lir::LStructDef* host = nullptr;
            if (!base.empty()) {
                auto it = templates_by_name.find(std::string(base));
                if (it != templates_by_name.end()) host = it->second;
            }
            if (host) host->methods.push_back(std::move(fp));
            else      kept.push_back(std::move(fp));
        }
        prog.functions = std::move(kept);
    }
}

// Parse one annotation literal AST node into an LAnnotationValue.
// Handles LIT_INT, LIT_FLOAT, LIT_BOOL, LIT_STR, ENUM_LIT, ANNOT_ARR.
lir::LAnnotationValue SemaChecker::parse_annot_literal(TinyMapView v) {
    using Kind = lir::LAnnotationValue::Kind;
    lir::LAnnotationValue out;
    int32_t c = code_of(v);
    if (c == la::LIT_INT) {
        out.kind = Kind::Int;
        auto sv = str_of(v.get(la::VALUE.code));
        out.i = parse_int_literal(sv);
    } else if (c == la::LIT_FLOAT) {
        out.kind = Kind::Float;
        std::string s(str_of(v.get(la::VALUE.code)));
        s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
        // strip optional f32/f64 suffix
        if (s.size() > 3 && (s.ends_with("f32") || s.ends_with("f64")))
            s.resize(s.size() - 3);
        out.f = std::stod(s);
    } else if (c == la::LIT_BOOL) {
        out.kind = Kind::Bool;
        AnyVal av = v.get(la::VALUE.code);
        bool b = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        out.i = b ? 1 : 0;
    } else if (c == la::LIT_STR) {
        out.kind = Kind::Str;
        auto raw = str_of(v.get(la::VALUE.code));
        // Strip surrounding quotes (raw form may lack them for r"..." — handle both).
        std::string inner;
        if (!raw.empty() && (raw.front() == '"')) {
            inner.assign(raw.substr(1, raw.size() - 2));
            // Unescape common escapes (same set used elsewhere in the compiler).
            std::string dec;
            dec.reserve(inner.size());
            for (size_t i = 0; i < inner.size(); ++i) {
                if (inner[i] == '\\' && i + 1 < inner.size()) {
                    switch (inner[++i]) {
                    case 'n':  dec += '\n'; break;
                    case 't':  dec += '\t'; break;
                    case 'r':  dec += '\r'; break;
                    case '\\': dec += '\\'; break;
                    case '"':  dec += '"';  break;
                    case '0':  dec += '\0'; break;
                    default:   dec += '\\'; dec += inner[i]; break;
                    }
                } else {
                    dec += inner[i];
                }
            }
            out.s = std::move(dec);
        } else if (!raw.empty() && raw.starts_with("r\"")) {
            // r"..." raw string: strip r" ... "
            out.s.assign(raw.substr(2, raw.size() - 3));
        } else {
            out.s = std::string(raw);
        }
    } else if (c == la::ENUM_LIT) {
        out.kind = Kind::Enum;
        out.enum_name = std::string(str_of(v.get(la::NAME.code)));
        out.enum_variant = std::string(str_of(v.get(la::FIELD.code)));
    } else if (c == la::ANNOT_ARR) {
        out.kind = Kind::Array;
        if (v.has_key(la::ITEMS)) {
            auto items = arr_of(v.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i)
                out.arr.push_back(parse_annot_literal(map_of(items.get(i))));
        }
    }
    return out;
}

// Given an annotation AST node whose NAME resolves to a `#[annotation]` datatype,
// build an LAnnotationInstance.  Positional arguments are matched to fields in
// declaration order; named arguments (ANNOT_KV) match by field name.  Missing
// fields are left unset.  Unknown field names / type mismatches emit an error
// but do not abort.
std::optional<lir::LAnnotationInstance>
SemaChecker::build_annotation_instance(TinyMapView ann,
                                       std::string_view ann_name,
                                       std::string_view ann_pkg,
                                       const SemaStructInfo& ann_info) {
    lir::LAnnotationInstance inst;
    inst.ann_name = std::string(ann_name);
    inst.ann_pkg  = std::string(ann_pkg);
    inst.ann_fqn  = ann_pkg.empty() ? std::string(ann_name)
                                    : std::string(ann_pkg) + "::" + std::string(ann_name);

    // Case 1: #[A] — no args.
    if (!ann.has_key(la::ARGS) && !ann.has_key(la::VALUE)) return inst;

    // Case 2: #[A = lit] — single positional, maps to first field.
    if (ann.has_key(la::VALUE)) {
        auto v = map_of(ann.get(la::VALUE.code));
        if (ann_info.fields.empty()) {
            error(std::format("annotation '{}' takes no arguments", ann_name));
            return inst;
        }
        inst.kv.emplace_back(std::string(ann_info.fields[0].name), parse_annot_literal(v));
        return inst;
    }

    // Case 3: #[A(args...)] — iterate arg list.
    auto args_map = map_of(ann.get(la::ARGS.code));
    if (!args_map.has_key(la::ITEMS)) return inst;
    auto items = arr_of(args_map.get(la::ITEMS.code));
    size_t pos_idx = 0;
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto arg = map_of(items.get(i));
        int32_t ac = code_of(arg);
        if (ac == la::ANNOT_KV) {
            auto key = std::string(str_of(arg.get(la::NAME.code)));
            // Validate the field exists on the annotation datatype.
            bool found = false;
            for (auto& f : ann_info.fields) if (f.name == key) { found = true; break; }
            if (!found) {
                error(std::format("annotation '{}' has no field '{}'", ann_name, key));
                continue;
            }
            auto val = map_of(arg.get(la::VALUE.code));
            inst.kv.emplace_back(std::move(key), parse_annot_literal(val));
        } else if (ac == la::ANNOT_POS) {
            if (pos_idx >= ann_info.fields.size()) {
                error(std::format("annotation '{}' takes at most {} positional args",
                                  ann_name, ann_info.fields.size()));
                break;
            }
            auto val = map_of(arg.get(la::VALUE.code));
            inst.kv.emplace_back(std::string(ann_info.fields[pos_idx].name),
                                 parse_annot_literal(val));
            ++pos_idx;
        }
        // Legacy {NAME: $1} bare-ident form has no CODE key — ignore for user annotations.
    }
    return inst;
}

void SemaChecker::lower_module_items(TinyMapView mod, lir::LProgram& prog) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));

    cur_prog_ = &prog;

    // Annotations accumulate until the next non-annotation item, then are consumed.
    std::vector<TinyMapView> pending_annots;

    auto apply_annots_to_struct = [&](lir::LStructDef& sd) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                sd.type_code = read_annotation_u64(ann);
                // Cache with fully-qualified key so type_code_of::<T>() works
                // across packages.  Bare sd.name would collide if two packages
                // define the same struct name with different type_codes.
                auto fqn = cur_package_.empty() ? sd.name
                                                 : cur_package_ + "::" + sd.name;
                explicit_type_codes_[fqn] = sd.type_code;
            } else if (aname == "annotation") {
                // Marker: this datatype is itself a user-annotation declaration.
                sd.is_annotation_type = true;
            } else {
                // User annotation: NAME must resolve to a registered `#[annotation]` datatype.
                auto [pkg, info] = find_datatype_by_name(aname);
                if (info && info->is_annotation_type) {
                    if (auto inst = build_annotation_instance(ann, aname, pkg, *info))
                        sd.annotations.push_back(std::move(*inst));
                }
                // Unknown annotations silently ignored (future compiler-internal
                // keys, or forward-declared not-yet-seen).
            }
        }
    };

    auto apply_annots_to_trait = [&](lir::LTraitDef& td) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "tag_dispatch" && ann.has_key(la::ARGS)) {
                // ARGS: { ITEMS: [ { NAME: "system_name" } ] }
                auto args_map = map_of(ann.get(la::ARGS.code));
                if (args_map.has_key(la::ITEMS)) {
                    auto arr = arr_of(args_map.get(la::ITEMS.code));
                    if (arr.size() > 0)
                        td.tag_dispatch_system = std::string(str_of(map_of(arr.get(0)).get(la::NAME.code)));
                }
            }
            // #[type_code=N] on a trait makes it a genos: the code identifies
            // the logical datatype family, and each `impl Trait for Eidos`
            // propagates the code to its target struct during lowering.
            else if (aname == "type_code" && ann.has_key(la::VALUE)) {
                td.type_code = read_annotation_u64(ann);
            }
        }
    };

    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t c = code_of(item);
        if (c == la::ANNOTATION) {
            pending_annots.push_back(item);
            continue;
        }
        if (c == la::INSTANTIATE_DECL) {
            // `instantiate Foo<T>;` / `pub instantiate Foo<T>;` — pre-instantiation
            // root pin (C++ `template class Foo<int>;` analog). Pushes an
            // LInstAnnotation that mono picks up via the existing path: it demands
            // struct instantiation, which in the current eager scheme clones every
            // method. When lazy method codegen lands (L1), the annotation will also
            // pin all methods as roots so the worklist transitively pulls everything
            // they call. `pub` is stored for L3 lib-site re-export semantics; until
            // separate codegen exists it's a marker.
            if (!item.has_key(la::TYPE.code)) {
                error("instantiate declaration missing type expression");
                pending_annots.clear();
                continue;
            }
            auto type_node = map_of(item.get(la::TYPE.code));
            TypeRef resolved = resolve_type(type_node);
            if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                if (TypeRef(resolved).kind() != LogosType::Kind::Struct &&
                    TypeRef(resolved).kind() != LogosType::Kind::ZonedStruct &&
                    TypeRef(resolved).kind() != LogosType::Kind::Enum) {
                    error("instantiate target must be a struct, datatype, or enum");
                } else if (TypeRef(resolved).type_args().empty()) {
                    // B-mt-02: `instantiate Foo;` on a non-generic type adds
                    // no information.  Reject with a clear diagnostic.
                    auto nm = (TypeRef(resolved).kind() == LogosType::Kind::Enum)
                                ? std::string(TypeRef(resolved).enum_name())
                                : std::string(TypeRef(resolved).struct_name());
                    error(std::format("'instantiate {0};': '{0}' is not generic — "
                                      "'instantiate' only applies to generic templates",
                                      nm));
                } else {
                    lir::LInstAnnotation ia;
                    ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                    if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                         TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                        !TypeRef(resolved).type_args().empty()) {
                        ia.mangled_name = concrete_struct_name(resolved);
                    } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                               TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                        ia.mangled_name = TypeRef(resolved).struct_name();
                    } else {
                        ia.mangled_name = TypeRef(resolved).enum_name();
                    }
                    ia.struct_type = resolved;
                    ia.is_root_pin = true;
                    ia.is_pub_reexport = item.has_key(la::IS_PUB) &&
                                         item.get(la::IS_PUB.code).is_value() &&
                                         item.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                    prog.inst_annotations.push_back(std::move(ia));
                }
            }
            pending_annots.clear();
            continue;
        }
        if (c == la::METACALL_ITEM) {
            // MC1.1: synthesise a void thunk that calls the metafn and
            // forwards the resulting QuoteItemBlob to logos_emit_item_blob_subst.
            // Driver invokes the thunk and marks this node consumed.
            lower_metacall_item(item, prog);
            pending_annots.clear();
            continue;
        }
        if (c == la::FN_MACRO_CALL_ITEM) {
            // Slice 6 of fn-macros: `name!{...}` at item position.
            // Parallel to METACALL_ITEM but routes through the fn-macro
            // pipeline (callee returns ItemList / QuoteItemBlob).
            lower_fn_macro_call_item(item, prog);
            pending_annots.clear();
            continue;
        }
        if (c == la::FN_MACRO_CALL_ITEM_DONE) {
            pending_annots.clear();
            continue;
        }
        if (c == la::METACALL_ITEM_DONE) {
            // Driver-set marker: this node has already been processed in
            // a prior round (its thunk has run, items have been spliced).
            // Silently skip.
            pending_annots.clear();
            continue;
        }
        if      (c == la::STRUCT) {
            // Explicit struct instantiation: `#[type_code=N] struct Pair<i32>;`
            // Has TYPE key, no NAME key — delegate to same logic as DATATYPE inst.
            if (!item.has_key(la::NAME.code)) {
                if (!item.has_key(la::TYPE.code)) {
                    error("struct instantiation declaration missing type expression");
                } else {
                    auto type_node = map_of(item.get(la::TYPE.code));
                    // B-it-08: `pub struct Foo<T>;` (forward-decl-style with bare
                    // type-vars at item scope) hits resolve_type with T unbound,
                    // which produces a misleading "unknown type 'T'" diagnostic.
                    // Detect the shape and surface a specific message.
                    if (code_of(type_node) == la::GENERIC_INST && type_node.has_key(la::ITEMS)) {
                        auto args = arr_of(type_node.get(la::ITEMS.code));
                        bool has_unbound_var = false;
                        for (uint64_t ai = 0; ai < args.size(); ++ai) {
                            auto a = map_of(args.get(ai));
                            if (code_of(a) == la::TYPE_REF && a.has_key(la::NAME)) {
                                std::string an(str_of(a.get(la::NAME.code)));
                                if (!try_resolve_as_known_type(an) &&
                                    current_type_params_.count(an) == 0) {
                                    has_unbound_var = true;
                                    break;
                                }
                            }
                        }
                        if (has_unbound_var) {
                            std::string nm(str_of(type_node.get(la::NAME.code)));
                            error(std::format(
                                "'struct {0}<...>;': explicit instantiation requires "
                                "concrete type arguments. For a generic struct "
                                "definition write the body directly: "
                                "`pub struct {0}<...> {{ ... }}` (B-it-08).",
                                nm));
                            pending_annots.clear();
                            continue;
                        }
                    }
                    TypeRef resolved = resolve_type(type_node);
                    // B-it-07: `struct Empty;` (no body, not a real instantiation)
                    // resolves to error_t and silently drops.  When the type
                    // expression is a bare ident with no type-args, surface
                    // a clear diagnostic.
                    if ((!resolved || TypeRef(resolved).kind() == LogosType::Kind::Error) &&
                        type_node.has_key(la::NAME) &&
                        !type_node.has_key(la::ITEMS)) {
                        std::string nm(str_of(type_node.get(la::NAME.code)));
                        error(std::format(
                            "'struct {0};': '{0}' is not defined — "
                            "did you mean 'struct {0} {{ ... }}' to declare a body?",
                            nm));
                        pending_annots.clear();
                        continue;
                    }
                    if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                        lir::LInstAnnotation ia;
                        ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                        if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                             TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                            !TypeRef(resolved).type_args().empty()) {
                            ia.mangled_name = concrete_struct_name(resolved);
                        } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                                   TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                            ia.mangled_name = TypeRef(resolved).struct_name();
                        }
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        if (ia.type_code != 0)
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                        ia.struct_type = resolved;
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
                pending_annots.clear();
                continue;
            }
            // Check if #[zoned] annotation is present → treat as zoned struct.
            bool has_zoned = false;
            for (auto& ann : pending_annots) {
                auto aname = std::string(str_of(ann.get(la::NAME.code)));
                if (aname == "zoned") { has_zoned = true; break; }
            }
            if (is_specialization_struct(item)) {
                auto sd = lower_spec_struct(item);
                if (has_zoned) {
                    sd.is_zoned = true;
                    apply_annots_to_struct(sd);
                }
                prog.struct_specializations.push_back(std::move(sd));
            } else if (has_zoned) {
                auto sd = lower_struct_def(item);
                sd.is_zoned = true;
                sd.pkg = std::string(cur_package_);
                { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) sd.is_data_plain = dsi->is_data_plain; }
                apply_annots_to_struct(sd);
                if (sd.type_params.empty()) {
                    std::string canon = std::string(cur_package_) + "::" + sd.name;
                    sd.type_hash = type_hash_23(canon);
                    if (sd.type_code == 0) {
                        uint64_t raw = type_hash_56bit(sd.type_hash);
                        sd.type_code = (raw < 128) ? (raw + 128) : raw;
                    }
                }
                prog.structs.push_back(std::move(sd));
            } else {
                auto sd = lower_struct_def(item);
                sd.pkg = std::string(cur_package_);
                prog.structs.push_back(std::move(sd));
            }
        }
        else if (c == la::DATATYPE) {
            if (!item.has_key(la::NAME.code)) {
                // Explicit instantiation declaration: #[type_code=N] datatype SomeType<T>;
                // Has TYPE key, no NAME key, no FIELDS key.
                if (!item.has_key(la::TYPE.code)) {
                    error("explicit instantiation declaration missing type expression");
                } else {
                    auto type_node = map_of(item.get(la::TYPE.code));
                    TypeRef resolved = resolve_type(type_node);
                    if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                        lir::LInstAnnotation ia;
                        // Include package prefix for a globally unique canonical name.
                        ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                        // Mangled name for matching against monomorphized struct defs.
                        if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                             TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                            !TypeRef(resolved).type_args().empty()) {
                            ia.mangled_name = concrete_struct_name(resolved);
                        } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                                   TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                            ia.mangled_name = TypeRef(resolved).struct_name();
                        }
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        // Register into explicit_type_codes_ so sema-time queries
                        // (`type_code_of::<Foo<i32>>()`) resolve to the annotated code.
                        if (ia.type_code != 0)
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                        // Store resolved type so mono can demand struct instantiation
                        // even when no Logos code references this type directly.
                        ia.struct_type = resolved;
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else if (is_specialization_struct(item)) {
                // Datatype specialization (e.g. `pub eidos Map<Bitmap, V> { ... }`).
                auto sd = lower_spec_struct(item);
                sd.is_zoned = true;
                // Apply #[type_code=N] annotations on full (all-concrete) specs.
                // Without this, `impl Trait for Map<i32, AnyVal>` has no way to
                // find the annotated code during dispatch-entry emission.
                bool all_concrete = !sd.spec_patterns.empty();
                for (auto p : sd.spec_patterns)
                    if (!p || TypeRef(p).kind() == LogosType::Kind::TypeVar) { all_concrete = false; break; }
                if (all_concrete) {
                    for (auto& ann : pending_annots) {
                        auto aname = std::string(str_of(ann.get(la::NAME.code)));
                        if (aname == "type_code" && ann.has_key(la::VALUE)) {
                            sd.type_code = read_annotation_u64(ann);
                            // Register mangled fqn so dispatch-entry lookup
                            // (target = "Map$G2$i32$AnyVal") succeeds.
                            auto inst_type = make_generic_struct(sd.name, sd.spec_patterns);
                            std::string mangled = concrete_struct_name(inst_type);
                            std::string canon = type_str(inst_type);  // "Name<Args>"
                            auto fqn_mangled = cur_package_.empty()
                                ? mangled : cur_package_ + "::" + mangled;
                            auto fqn_canon = cur_package_.empty()
                                ? canon : cur_package_ + "::" + canon;
                            explicit_type_codes_[fqn_mangled] = sd.type_code;
                            explicit_type_codes_[fqn_canon]   = sd.type_code;
                            // Also register under the template's package (see
                            // matching note in the genos-spec annotation path).
                            std::string tmpl_pkg;
                            { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) tmpl_pkg = dsi->package; }
                            if (tmpl_pkg.empty()) { auto [pkg, ssi] = find_struct_by_name(sd.name); if (ssi) tmpl_pkg = ssi->package; }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                explicit_type_codes_[tmpl_pkg + "::" + mangled] = sd.type_code;
                                explicit_type_codes_[tmpl_pkg + "::" + canon]   = sd.type_code;
                            }
                        }
                    }
                }
                prog.struct_specializations.push_back(std::move(sd));
            } else {
                // Normal datatype definition.
                auto sd = lower_struct_def(item);
                sd.is_zoned = true;
                sd.pkg = std::string(cur_package_);
                // Propagate is_data_plain only for datatypes (not regular structs).
                { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) sd.is_data_plain = dsi->is_data_plain; }
                apply_annots_to_struct(sd);
                // Compute type_hash for concrete (non-generic) datatypes only.
                // Generic templates get their hash at instantiation time in mono_pass.
                if (sd.type_params.empty()) {
                    std::string canon = std::string(cur_package_) + "::" + sd.name;
                    sd.type_hash = type_hash_23(canon);
                    // Auto-assign type_code from hash; ensure it's outside the reserved
                    // inline-AnyVal range 1-127 (those are for zone-stored types >= 128).
                    if (sd.type_code == 0) {
                        uint64_t raw = type_hash_56bit(sd.type_hash);
                        sd.type_code = (raw < 128) ? (raw + 128) : raw;
                    }
                }
                prog.structs.push_back(std::move(sd));
            }
        }
        else if (c == la::ENUM)       prog.enums.push_back(lower_enum_def(item));
        else if (c == la::FN || c == la::EXTERN_FN) {
            if (is_specialization_fn(item))
                prog.specializations.push_back(std::make_unique<lir::LFunction>(lower_spec_fn(item)));
            else
                prog.functions.push_back(std::make_unique<lir::LFunction>(lower_fn(item)));
        }
        else if (c == la::CONST_DEF)  prog.consts.push_back(lower_const_def(item));
        else if (c == la::TYPE_ALIAS) prog.type_aliases.push_back(lower_type_alias_def(item));
        else if (c == la::TRAIT_DEF) {
            // Explicit genos specialization decl: `#[type_code=N] pub genos Array<i32>;`.
            // No NAME on the decl (name lives inside TYPE node); type_ref is
            // `Bag<Args>` where Bag is a trait name, not a struct.  Don't call
            // resolve_type — traits aren't types.  Extract trait_name + args
            // manually and canonicalize.
            if (!item.has_key(la::NAME.code) && item.has_key(la::TYPE.code)) {
                auto type_node = map_of(item.get(la::TYPE.code));
                if (code_of(type_node) != la::GENERIC_INST) {
                    error("genos specialization decl must have type arguments");
                } else {
                    auto tname = std::string(str_of(type_node.get(la::NAME.code)));
                    std::string canon = std::string(cur_package_) + "::" + tname + "<";
                    bool arg_ok = true;
                    if (type_node.has_key(la::ITEMS.code)) {
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        for (uint64_t i = 0; i < items2.size(); ++i) {
                            auto at = resolve_type(map_of(items2.get(i)));
                            if (!at || TypeRef(at).kind() == LogosType::Kind::Error) { arg_ok = false; break; }
                            if (i) canon += ", ";
                            canon += type_str(at);
                        }
                    }
                    canon += ">";
                    if (!arg_ok) {
                        error(std::format("genos '{}': cannot resolve type arguments", tname));
                    } else {
                        lir::LInstAnnotation ia;
                        ia.canonical_name = canon;
                        // Also compute the mangled eidos name ("Map$G2$Bitmap$AnyVal")
                        // for an eidos with the same name as the genos — this is the
                        // typical case where `genos Foo<X>` specialization's type_code
                        // should land on the like-named `eidos Foo<X>` struct.
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        std::vector<TypeRef> resolved_args;
                        for (uint64_t i = 0; i < items2.size(); ++i)
                            resolved_args.push_back(resolve_type(map_of(items2.get(i))));
                        // Find the template's kind (datatype or struct) using package-aware lookup.
                        TypeRef like_eidos = nullptr;
                        {
                            auto [dpkg, dsi] = find_datatype_by_name(tname);
                            if (dsi) like_eidos = make_generic_datatype(tname, resolved_args, {}, dpkg);
                        }
                        if (!like_eidos) {
                            auto [spkg, ssi] = find_struct_by_name(tname);
                            if (ssi) like_eidos = make_generic_struct(tname, resolved_args, {}, spkg);
                        }
                        // Legacy bare-name fallback for same-package structs
                        if (!like_eidos) {
                            if (datatypes_.count(tname))
                                like_eidos = make_generic_datatype(tname, resolved_args);
                            else if (structs_.count(tname))
                                like_eidos = make_generic_struct(tname, resolved_args);
                        }
                        if (like_eidos)
                            ia.mangled_name = concrete_struct_name(like_eidos);
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        if (ia.type_code != 0) {
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                            // Register mangled fqn key too so the dispatch-entry
                            // emission (sema_decl.cpp) can find the type_code via
                            // mangled-target lookup.
                            if (!ia.mangled_name.empty())
                                explicit_type_codes_[std::string(cur_package_) + "::" + ia.mangled_name] = ia.type_code;
                            // Also register under the *template's* package, for both
                            // the canonical-name and mangled keys.  Every lookup site
                            // (dispatch-entry emission in sema_decl.cpp, type_code_of
                            // in sema_expr.cpp, etc.) resolves the package via
                            // `datatypes_[base].package` — the template's package.
                            // When a genos specialisation is declared in a *different*
                            // package from its template (e.g. `genos Map<Varchar,
                            // AnyVal>` lives in hermes.objectmap while `datatype
                            // Map<K,V>` lives in hermes.map), lookups keyed by the
                            // template's package would otherwise miss this annotation
                            // and fall back to the auto-hashed type_code, silently
                            // producing a different code at the use site than the
                            // one registered in the dispatch table.  Mirror both keys
                            // under the template's package to keep the two sides in
                            // agreement.
                            std::string tmpl_pkg;
                            { auto [pkg, dsi] = find_datatype_by_name(tname); if (dsi) tmpl_pkg = dsi->package; }
                            if (tmpl_pkg.empty()) { auto [pkg, ssi] = find_struct_by_name(tname); if (ssi) tmpl_pkg = ssi->package; }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                // Canonical form: "pkg::Name<Args>".  ia.canonical_name
                                // was built from cur_package_ at the top of this block
                                // (`canon = cur_package_ + "::" + tname + "<…>"`) so we
                                // reconstruct the template-package form by substring.
                                auto colon2 = ia.canonical_name.find("::");
                                if (colon2 != std::string::npos) {
                                    explicit_type_codes_[tmpl_pkg + ia.canonical_name.substr(colon2)] = ia.type_code;
                                }
                                if (!ia.mangled_name.empty())
                                    explicit_type_codes_[tmpl_pkg + "::" + ia.mangled_name] = ia.type_code;
                            }
                        }
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else {
                auto td = lower_trait_def(item);
                apply_annots_to_trait(td);
                // Reject #[type_code] on a template genos (type_params present).
                // It would collide at dispatch-table level: every concrete
                // specialization would land in the same tag-system slot.
                if (td.type_code != 0) {
                    auto tit = traits_.find(td.name);
                    if (tit != traits_.end() && !tit->second.type_params.empty())
                        error(std::format("genos '{}': #[type_code] on a template "
                                          "(parametric) genos is forbidden — "
                                          "attach it to a concrete specialization "
                                          "(e.g. `#[type_code=N] genos {}<T>;`)",
                                          td.name, td.name));
                }
                prog.traits.push_back(std::move(td));
            }
        }
        else if (c == la::IMPL_BLOCK) lower_impl_block(item, prog);
        pending_annots.clear();
    }
}

// ── B64: per-struct/enum variance via fixed-point ─────────────────────────────

namespace {

// Compute the variance with which `target` (a type-param or lifetime-param
// name, e.g. "T" or "'a") appears in `t`, given the in-progress variance
// table for other user defs and an `ambient` variance context (the variance
// at which `t`'s containing position is held).
Variance variance_in_type(TypeRef t,
                          const std::string& target,
                          bool target_is_lifetime,
                          const DefVarianceTable& table,
                          Variance ambient = Variance::Co)
{
    if (!t) return Variance::BiVar;
    using K = LogosType::Kind;
    switch (t.kind()) {
        case K::TypeVar:
            if (!target_is_lifetime && std::string(t.type_var_name()) == target)
                return ambient;
            return Variance::BiVar;
        case K::Ref: {
            Variance v = Variance::BiVar;
            if (target_is_lifetime && std::string(t.lifetime()) == target)
                v = variance_meet(v, ambient);  // Ref is Co in lt
            v = variance_meet(v, variance_in_type(t.pointee(), target,
                                                  target_is_lifetime, table, ambient));
            return v;
        }
        case K::MutRef: {
            Variance v = Variance::BiVar;
            if (target_is_lifetime && std::string(t.lifetime()) == target)
                v = variance_meet(v, ambient);  // MutRef is Co in lt
            // Inv in pointee.
            v = variance_meet(v, variance_in_type(t.pointee(), target,
                                                  target_is_lifetime, table,
                                                  variance_compose(ambient, Variance::Inv)));
            return v;
        }
        case K::Ptr:
            // B84: *const T is Co in pointee (matches Rust); *mut T is Inv.
            return variance_in_type(t.pointee(), target, target_is_lifetime, table,
                                    variance_compose(ambient,
                                        t.mut_ptr() ? Variance::Inv : Variance::Co));
        case K::Tuple: {
            Variance v = Variance::BiVar;
            for (auto e : t.tuple_elems())
                v = variance_meet(v, variance_in_type(e, target,
                                                       target_is_lifetime, table, ambient));
            return v;
        }
        case K::Array:
        case K::Slice:
            return variance_in_type(t.elem(), target, target_is_lifetime, table, ambient);
        case K::Struct:
        case K::ZonedStruct:
        case K::Enum: {
            std::string key = std::string(t.pkg_name()) +
                              (t.pkg_name().empty() ? "" : ".") +
                              std::string(t.kind() == K::Enum ? t.enum_name() : t.struct_name());
            auto it = table.find(key);
            const VarianceMap* vm = (it == table.end()) ? nullptr : &it->second;
            auto var_for = [&](size_t i, bool is_lt) -> Variance {
                if (!vm) return Variance::Co;
                std::string ikey = (is_lt ? "@" : "#") + std::to_string(i);
                auto vit = vm->find(ikey);
                return (vit == vm->end()) ? Variance::Co : vit->second;
            };
            Variance v = Variance::BiVar;
            size_t i = 0;
            for (auto a : t.type_args()) {
                Variance inner_ambient = variance_compose(ambient, var_for(i++, false));
                v = variance_meet(v, variance_in_type(a, target,
                                                       target_is_lifetime, table,
                                                       inner_ambient));
            }
            i = 0;
            for (auto& lt : t.lifetime_args()) {
                if (target_is_lifetime && std::string(lt) == target) {
                    Variance inner_ambient = variance_compose(ambient, var_for(i, true));
                    v = variance_meet(v, inner_ambient);
                }
                ++i;
            }
            return v;
        }
        case K::FnPtr: {
            Variance v = Variance::BiVar;
            for (auto p : t.closure_params())
                v = variance_meet(v, variance_in_type(p, target, target_is_lifetime, table,
                                                       variance_compose(ambient, Variance::Contra)));
            v = variance_meet(v, variance_in_type(t.closure_ret(), target,
                                                   target_is_lifetime, table, ambient));
            return v;
        }
        default:
            return Variance::BiVar;
    }
}

} // anonymous namespace

void SemaChecker::compute_variances() {
    variance_table_.clear();
    auto seed = [&](const std::string& key,
                    const std::vector<TypeParam>& tps,
                    const std::vector<std::string>& lts) {
        VarianceMap m;
        for (size_t i = 0; i < tps.size(); ++i)
            m["#" + std::to_string(i)] = Variance::BiVar;
        for (size_t i = 0; i < lts.size(); ++i)
            m["@" + std::to_string(i)] = Variance::BiVar;
        variance_table_[key] = std::move(m);
    };
    // structs_/datatypes_/enums_ are keyed by "pkg::Name" (sema_key). Subtype
    // lookup uses "pkg.Name". Strip the "pkg::" prefix from the map key and
    // re-join with "." to match what subtype expects.
    auto qkey = [](const std::string& pkg, const std::string& map_key) {
        std::string name = map_key;
        if (!pkg.empty()) {
            std::string prefix = pkg + "::";
            if (name.compare(0, prefix.size(), prefix) == 0)
                name = name.substr(prefix.size());
            return pkg + "." + name;
        }
        return name;
    };
    for (auto& [k, si] : structs_)
        seed(qkey(si.package, k), si.type_params, si.lifetime_params);
    for (auto& [k, si] : datatypes_)
        seed(qkey(si.package, k), si.type_params, si.lifetime_params);
    for (auto& [k, ei] : enums_)
        seed(k, ei.type_params, ei.lifetime_params);

    bool changed = true;
    int rounds = 0;
    const int MAX_ROUNDS = 32;
    while (changed && rounds++ < MAX_ROUNDS) {
        changed = false;
        auto update_def =
            [&](const std::string& key,
                const std::vector<TypeParam>& tps,
                const std::vector<std::string>& lts,
                auto field_types_fn) {
            VarianceMap& vm = variance_table_[key];
            for (size_t i = 0; i < tps.size(); ++i) {
                Variance v = Variance::BiVar;
                for (auto ft : field_types_fn()) {
                    v = variance_meet(v,
                        variance_in_type(ft, tps[i].name, /*lt=*/false,
                                         variance_table_, Variance::Co));
                }
                std::string ikey = "#" + std::to_string(i);
                if (vm[ikey] != v) { vm[ikey] = v; changed = true; }
            }
            for (size_t i = 0; i < lts.size(); ++i) {
                Variance v = Variance::BiVar;
                for (auto ft : field_types_fn()) {
                    v = variance_meet(v,
                        variance_in_type(ft, lts[i], /*lt=*/true,
                                         variance_table_, Variance::Co));
                }
                std::string ikey = "@" + std::to_string(i);
                if (vm[ikey] != v) { vm[ikey] = v; changed = true; }
            }
        };
        for (auto& [k, si] : structs_) {
            update_def(qkey(si.package, k), si.type_params, si.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& f : si.fields) ts.push_back(f.type);
                           return ts;
                       });
        }
        for (auto& [k, si] : datatypes_) {
            update_def(qkey(si.package, k), si.type_params, si.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& f : si.fields) ts.push_back(f.type);
                           return ts;
                       });
        }
        for (auto& [k, ei] : enums_) {
            update_def(k, ei.type_params, ei.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& v : ei.variants)
                               for (auto pt : v.payload_types) ts.push_back(pt);
                           return ts;
                       });
        }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

lir::LProgram sema_lower(const std::vector<logos::hermes::Hermes>& asts,
                          const std::vector<std::string>& filenames,
                          const std::vector<bool>& from_binary,
                          SemaOptions opts) {
    SemaChecker checker;
    checker.set_metaprog_options(opts.metaprog_mode, opts.entry_ast_idx);
    checker.set_metaprog_keep_fns(opts.metaprog_keep_fns);
    return checker.run(asts, filenames, from_binary);
}

} // namespace logos::compiler
