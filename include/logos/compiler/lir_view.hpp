// Logos project — https://github.com/victor-smirnov/logos
//
// Phase 3c — read-side view types over the L-IR Hermes mirror.
//
// Pattern follows TypeRef in sema.hpp: fat handle {arena*, offset} into the
// program's TypePool arena, with kind() returning a strongly-typed schema
// Code enum and visit() switch-dispatching to per-variant view structs.
//
// Per-variant views (EBinOpView, ECallView, …) are thin wrappers around the
// matching ref; their accessors lazily read fields from the TinyObjectMap.
// Views are filled in incrementally as Phase 3d migrates each reader; the
// infrastructure here is enough to get the first reader off the variant.

#pragma once

#include <logos/compiler/lir_schema.hpp>
#include <logos/compiler/sema.hpp>  // TypeRef, TypePoolImpl
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>   // arena_id_t (multi-arena IR)
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp> // is_external_ref_av, resolve_external_ref
#include <logos/hermes/compat.hpp>   // for arena() in cross-arena dispatch
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace logos::compiler::lir_view {

// ── Fat-handle base ───────────────────────────────────────────────────────

namespace detail {

class RefBase {
protected:
    const hermes::Arena*   arena_ = nullptr;
    // Stage B (self-relative handles): the node's mirror is addressed by its
    // ABSOLUTE pointer, resolved once at construction (self-relative AnyVal::
    // resolve() — no base threading). arena_ is retained only for offset()
    // (the .hermes0 serialization round-trip) and ownership. nullptr = null ref.
    const uint8_t*         ptr_ = nullptr;
    // Phase 2.B (multi-arena IR): arena_id of the arena this ref lives in.
    // INVALID_ARENA_ID = single-arena fast path (current compiler).
    // Non-INVALID = resolved from an ExternalRef; arena_ + arena_id_ are
    // both populated and consistent with each other.
    hermes::arena_id_t     arena_id_ = hermes::INVALID_ARENA_ID;

    static const uint8_t* ptr_from_off(const hermes::Arena* a,
                                       hermes::arena_offset_t o) noexcept {
        return (a && o != hermes::NULL_OFFSET) ? a->head().data() + o.value() : nullptr;
    }

public:
    // Ctors are public so the typed views can inherit them verbatim via
    // `using RefBase::RefBase;` (an inherited ctor keeps its base access, and
    // make_sub_ref/navigation construct views from free functions in `detail`).
    // RefBase lives in `detail` and is never the public handle, so exposing
    // construction here costs no encapsulation.
    RefBase() = default;
    // (arena, address) — the node's mirror is already an absolute pointer (the
    // mirror_ptr_ back-pointer stored on LExpr/LStmt nodes). Segments never move,
    // so the address is stable; no base+offset round-trip (MultiChunk-safe).
    RefBase(const hermes::Arena* a, const uint8_t* p) noexcept
        : arena_(a), ptr_(p) {}
    RefBase(const hermes::Arena* a, const uint8_t* p,
            hermes::arena_id_t aid) noexcept
        : arena_(a), ptr_(p), arena_id_(aid) {}
    // (arena, offset) — resolve against the single-chunk base (valid pre-MultiChunk;
    // serialized .hermes0 reads + cross-arena r.offset(). Stage C/D removes the
    // remaining offset sources).
    RefBase(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : arena_(a), ptr_(ptr_from_off(a, o)) {}
    // Cross-arena constructor — used by sub_*() dispatchers when a child
    // AnyVal points to an ExternalRef object.
    RefBase(const hermes::Arena* a, hermes::arena_offset_t o,
            hermes::arena_id_t aid) noexcept
        : arena_(a), ptr_(ptr_from_off(a, o)), arena_id_(aid) {}
    // AnyVal constructors — self-relative resolve (no base): av.resolve() gives the
    // absolute mirror address directly. Chunk-agnostic (ready for MultiChunk).
    RefBase(const hermes::Arena* a, hermes::AnyVal av) noexcept
        : arena_(a), ptr_(av.is_ref() ? av.resolve() : nullptr) {}
    RefBase(const hermes::Arena* a, hermes::AnyVal av, hermes::arena_id_t aid) noexcept
        : arena_(a), ptr_(av.is_ref() ? av.resolve() : nullptr), arena_id_(aid) {}

public:
    constexpr explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }
    hermes::arena_offset_t offset() const noexcept {
        auto* b = base();
        return (ptr_ && b) ? hermes::arena_offset_t(static_cast<uint32_t>(ptr_ - b))
                           : hermes::NULL_OFFSET;
    }
    // Absolute mirror address — the stable node identity used to key the mirror
    // table's reverse (mirror→C++ node) maps. Segments never move (MultiChunk-safe);
    // offset() is reserved for .hermes0 serialization (single rigid segment).
    const uint8_t* addr() const noexcept { return ptr_; }
    const hermes::Arena*   arena()  const noexcept { return arena_; }
    // Phase 2.B accessors.
    hermes::arena_id_t arena_id() const noexcept { return arena_id_; }
    bool               is_external() const noexcept { return arena_id_.is_valid(); }

    uint8_t* base() const noexcept {
        return arena_ ? const_cast<uint8_t*>(arena_->head().data()) : nullptr;
    }
    const hermes::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const hermes::TinyObjectMap*>(ptr_);
    }
    uint64_t schema_type_code() const noexcept {
        return mirror()->schema_type_code();
    }

    friend constexpr bool operator==(const RefBase& a, const RefBase& b) noexcept {
        return a.ptr_ == b.ptr_;
    }
};

// Phase 2.B helper: given a child AnyVal in `parent`'s arena, return
// (arena, offset, arena_id) to use when constructing a child Ref. For
// local refs the result is `(parent.arena_, av.offset, INVALID)`. For
// external refs (AnyVal points at ExternalRef object) the result is
// resolved via ArenaPool: `(target_arena, target_offset, target_id)`.
// Returns nullopt-equivalent (arena=nullptr, off=NULL) when resolution
// fails (unknown arena_id, out-of-range obj_id, etc.).
struct ChildLoc {
    const hermes::Arena*   arena;
    hermes::AnyVal         av;   // resolvable child value-form Ref (re-anchored on copy)
    hermes::arena_id_t     aid;  // INVALID for local refs

    static ChildLoc null() noexcept { return {nullptr, hermes::AnyVal{}, hermes::INVALID_ARENA_ID}; }
    constexpr explicit operator bool() const noexcept {
        return arena != nullptr && !av.is_null();
    }
};

inline ChildLoc resolve_child(const RefBase& parent, hermes::AnyVal av) noexcept {
    if (av.is_null()) return ChildLoc::null();
    if (!hermes::is_external_ref_av(av)) [[likely]] {
        // Local ref — the child av already resolves self-relatively (no base).
        return ChildLoc{parent.arena(), av, hermes::INVALID_ARENA_ID};
    }
    // Cross-arena dispatch — hermes2 ExternalRef is an AnyVal Pod niche (no arena
    // object): decode (arena_id, obj_id) inline and resolve via the global pool.
    hermes::ExternalRef ref = hermes::decode_external_ref(av);
    auto r = hermes::resolve_external_ref(ref);
    if (!r.ok()) return ChildLoc::null();
    // Re-anchor the resolved foreign object as a value-form Ref so downstream
    // construction stays base-free (the AnyVal re-lowers on copy into the ref).
    hermes::AnyVal child; child.set_ref(r.obj);
    return ChildLoc{&r.mem->arena(), child, ref.aid};
}

// Construct a child TypeRef from (parent, child AnyVal, caller's pool). The child
// resolves self-relatively (no base). When the parent ref is itself cross-arena the
// child inherits the parent's arena_id (and drops the local pool) so downstream
// TypeRef accessors route through ArenaPool's MemHolder.
inline TypeRef make_child_typeref(const RefBase& parent,
                                  hermes::AnyVal av,
                                  const TypePoolImpl* pool) noexcept {
    if (parent.is_external()) {
        return TypeRef(parent.arena(), av, /*pool=*/nullptr, parent.arena_id());
    }
    return TypeRef(parent.arena(), av, pool);
}

// Construct a same-kind child Ref from (parent, child AnyVal). The child av resolves
// self-relatively (no base). Inherits parent's arena_id when the parent is cross-arena
// so the foreign-arena context isn't lost.
template <class TargetRef>
inline TargetRef make_sub_ref(const RefBase& parent,
                              hermes::AnyVal av) noexcept {
    if (parent.is_external()) {
        return TargetRef(parent.arena(), av, parent.arena_id());
    }
    return TargetRef(parent.arena(), av);
}

// Read primitives shared by every view struct. Each takes a ref and a
// sparse-key code; missing keys return defaults so views can stay terse.

inline std::string_view read_string(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return {};
    return av.as_ptr<const hermes::ArenaString>()->view();
}

inline int64_t read_i64(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return 0;
    return *av.as_ptr<const int64_t>();
}

inline uint64_t read_u64(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return 0;
    return *av.as_ptr<const uint64_t>();
}

inline double read_f64(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return 0.0;
    return *av.as_ptr<const double>();
}

inline std::optional<int64_t> read_i64_opt(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return std::nullopt;
    return *av.as_ptr<const int64_t>();
}

inline uint32_t read_u32(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return 0;
    return av.is_value() ? av.as_value<uint32_t>() : *av.as_ptr<const uint32_t>();
}

inline bool read_bool(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return false;
    return av.as_value<uint8_t>() != 0;
}

inline uint8_t read_u8(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return 0;
    return av.as_value<uint8_t>();
}

// Read an Array<RelPtr<LogosType>> field into a TypeRef vector (cross-arena
// aware). Null elements become null TypeRefs (positional integrity preserved).
inline std::vector<TypeRef> read_type_array(const RefBase& r, uint8_t key,
                                            const TypePoolImpl* pool) noexcept {
    std::vector<TypeRef> out;
    auto av = r.mirror()->get(key);
    if (av.is_null()) return out;
    auto* arr = av.as_ptr<const hermes::ObjectArray>();
    out.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto el = arr->get(i);
        if (el.is_null()) { out.emplace_back(); continue; }
        out.push_back(make_child_typeref(r, el, pool));
    }
    return out;
}

// Read an Array<Varchar> field into a string_view vector (views point into the
// arena, valid as long as the mirror lives).
inline std::vector<std::string_view> read_string_array(const RefBase& r, uint8_t key) noexcept {
    std::vector<std::string_view> out;
    auto av = r.mirror()->get(key);
    if (av.is_null()) return out;
    auto* arr = av.as_ptr<const hermes::ObjectArray>();
    out.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto el = arr->get(i);
        if (el.is_null()) { out.emplace_back(); continue; }
        out.push_back(el.as_ptr<const hermes::ArenaString>()->view());
    }
    return out;
}

} // namespace detail

class StmtRef;
class PatRef;
class BlockRef;
class HermesValRef;

// ── ExprRef ───────────────────────────────────────────────────────────────

class ExprRef : public detail::RefBase {
public:
    ExprRef() = default;
    // Null-handle convenience: `LExprPtr x = nullptr` / `cond ? e : nullptr`
    // are the codebase's idiom for an absent expression handle (= default ExprRef).
    constexpr ExprRef(std::nullptr_t) noexcept {}
    // Inherit RefBase's (arena,offset[,aid]) and the self-relative (arena,AnyVal[,aid])
    // ctors — the latter make child navigation base-free (av.resolve(), MultiChunk-ready).
    using RefBase::RefBase;

    lir_schema::expr::Code kind() const noexcept {
        return lir_schema::expr::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }

    // TypeRef of the expression. The mirror stores the type's arena offset
    // under expr_common::TYPE; wrap it with the caller's TypePoolImpl* so
    // pool-dependent accessors (e.g. trait resolution) keep working.
    // Phase 2.B: ExternalRef-aware — cross-arena types return TypeRef with
    // pool_ = nullptr (read-only accessors still work).
    // Phase 5.B step 3: when the parent ref is itself cross-arena (this
    // ExprRef came from a foreign body walk), the child within-arena type
    // also lives in the foreign arena — inherit our arena_id so downstream
    // accessors route through the foreign MemHolder via ArenaPool.
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        auto av = mirror()->get(lir_schema::expr_common::TYPE.code);
        if (av.is_null()) return TypeRef{};
        auto loc = detail::resolve_child(*this, av);
        if (!loc) return TypeRef{};
        if (loc.aid.is_valid()) {
            return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, loc.aid);
        }
        if (is_external()) {
            return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, arena_id());
        }
        return TypeRef(loc.arena, loc.av, pool);
    }

    // Helper: reach a sub-expression via a sparse key (used by view structs).
    ExprRef sub_expr(uint8_t key) const noexcept;

    // Helper: reach a sub-type (RelPtr<LogosType>) via a sparse key.
    TypeRef sub_type(uint8_t key, const TypePoolImpl* pool) const noexcept {
        auto av = mirror()->get(key);
        if (av.is_null()) return TypeRef{};
        auto loc = detail::resolve_child(*this, av);
        if (!loc) return TypeRef{};
        if (loc.aid.is_valid()) {
            return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, loc.aid);
        }
        if (is_external()) {
            return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, arena_id());
        }
        return TypeRef(loc.arena, loc.av, pool);
    }
};

// ── StmtRef ───────────────────────────────────────────────────────────────

class StmtRef : public detail::RefBase {
public:
    StmtRef() = default;
    using RefBase::RefBase;

    lir_schema::stmt::Code kind() const noexcept {
        return lir_schema::stmt::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }

    // Source line (1-based) recorded by sema's lower_stmt via stmt_common::LINE.
    // 0 = no line info (synthetic stmt). Drives DWARF FileLineColLoc in mlir-gen.
    uint32_t line() const noexcept {
        return detail::read_u32(*this, lir_schema::stmt_common::LINE.code);
    }

    StmtRef sub_stmt(uint8_t key) const noexcept;
    ExprRef sub_expr(uint8_t key) const noexcept;
};

// ── PatRef ────────────────────────────────────────────────────────────────

class PatRef : public detail::RefBase {
public:
    PatRef() = default;
    using RefBase::RefBase;

    lir_schema::pat::Code kind() const noexcept {
        return lir_schema::pat::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }
};

// ── BlockRef / HermesValRef (opaque for now) ─────────────────────────────

class BlockRef : public detail::RefBase {
public:
    BlockRef() = default;
    using RefBase::RefBase;

    // Block stmts are stored under stmt_keys::ARMS (key 24) — a single key
    // shared with SMatch.arms because both are Array<RelPtr<sub-node>>.
    template <class F>
    void each_stmt(F&& f) const noexcept;
};

class HermesValRef : public detail::RefBase {
public:
    HermesValRef() = default;
    using RefBase::RefBase;

    lir_schema::hermes_val::Code kind() const noexcept {
        return lir_schema::hermes_val::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }
};

// ── Declaration views (Stage E: LProgram decl layer → Hermes mirror) ────────
//
// DeclRef is the shared fat-handle for top-level declaration mirrors; the
// per-kind views (TypeAliasView, …) wrap it and decode sparse fields lazily,
// mirroring the LExpr variant-view pattern above.
class DeclRef : public detail::RefBase {
public:
    DeclRef() = default;
    using RefBase::RefBase;
    lir_schema::decl::Code kind() const noexcept {
        return lir_schema::decl::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }
    // Read a RelPtr<LogosType> field as a TypeRef (cross-arena aware, like
    // ExprRef::sub_type) — shared by every decl view's type accessors.
    TypeRef decl_type(uint8_t key, const TypePoolImpl* pool) const noexcept {
        auto av = mirror()->get(key);
        if (av.is_null()) return TypeRef{};
        auto loc = detail::resolve_child(*this, av);
        if (!loc) return TypeRef{};
        if (loc.aid.is_valid()) return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, loc.aid);
        if (is_external())      return TypeRef(loc.arena, loc.av, /*pool=*/nullptr, arena_id());
        return TypeRef(loc.arena, loc.av, pool);
    }
    // Read a RelPtr<LExpr> field as an ExprRef (cross-arena aware, mirroring
    // ExprRef::sub_expr) — shared by every decl view's sub-expression accessor.
    ExprRef decl_expr(uint8_t key) const noexcept {
        auto av = mirror()->get(key);
        auto loc = detail::resolve_child(*this, av);
        if (!loc) return {};
        if (loc.aid.is_valid()) return ExprRef(loc.arena, loc.av, loc.aid);
        return detail::make_sub_ref<ExprRef>(*this, loc.av);
    }
    // Read a RelPtr<block mirror> field as a BlockRef (cross-arena aware) —
    // used by FunctionView::body().
    BlockRef decl_block(uint8_t key) const noexcept {
        auto av = mirror()->get(key);
        auto loc = detail::resolve_child(*this, av);
        if (!loc) return {};
        if (loc.aid.is_valid()) return BlockRef(loc.arena, loc.av, loc.aid);
        return detail::make_sub_ref<BlockRef>(*this, loc.av);
    }
};

// LTypeAlias { name: Varchar, type: RelPtr<LogosType>, doc: Varchar }
struct TypeAliasView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::NAME.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::DOC.code);
    }
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::decl_keys::TYPE_REF.code, pool);
    }
};

// LConst { name: Varchar, type: RelPtr<LogosType>, value: RelPtr<LExpr>,
//          doc: Varchar, is_static/is_mut/is_extern: bool (sparse), sym: Varchar }
struct ConstView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::NAME.code);
    }
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::decl_keys::TYPE_REF.code, pool);
    }
    ExprRef value() const noexcept {
        return self.decl_expr(lir_schema::decl_keys::VALUE.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::DOC.code);
    }
    bool is_static() const noexcept {
        return detail::read_bool(self, lir_schema::decl_keys::IS_STATIC.code);
    }
    bool is_mut() const noexcept {
        return detail::read_bool(self, lir_schema::decl_keys::IS_MUT.code);
    }
    bool is_extern() const noexcept {
        return detail::read_bool(self, lir_schema::decl_keys::IS_EXTERN.code);
    }
    std::string_view sym() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::SYM.code);
    }
};

// LVariant sub-map { V_NAME, V_DISC, V_PAYLOAD_TYPES: Array<RelPtr<LogosType>>,
//                    V_IS_VARIADIC: bool (sparse) }. Wraps a child DeclRef.
struct EnumVariantView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::variant_keys::V_NAME.code);
    }
    int64_t disc() const noexcept {
        return detail::read_i64(self, lir_schema::variant_keys::V_DISC.code);
    }
    bool is_variadic() const noexcept {
        return detail::read_bool(self, lir_schema::variant_keys::V_IS_VARIADIC.code);
    }
    // True if this variant carries any payload (drives enum has_payload()).
    bool has_payload() const noexcept {
        auto av = self.mirror()->get(lir_schema::variant_keys::V_PAYLOAD_TYPES.code);
        if (av.is_null()) return false;
        return av.as_ptr<const hermes::ObjectArray>()->size() > 0;
    }
    // Read V_PAYLOAD_TYPES into a TypeRef vector (cross-arena aware).
    std::vector<TypeRef> payload_types(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(lir_schema::variant_keys::V_PAYLOAD_TYPES.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) { out.emplace_back(); continue; }
            out.push_back(detail::make_child_typeref(self, el, pool));
        }
        return out;
    }
    template <class F>
    void each_payload_type(const TypePoolImpl* pool, F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::variant_keys::V_PAYLOAD_TYPES.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(detail::make_child_typeref(self, el, pool));
        }
    }
};

// Enum type-param sub-map { TP_NAME, TP_IS_VARIADIC: bool (sparse) }.
struct EnumTParamView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::enum_tparam_keys::TP_NAME.code);
    }
    bool is_variadic() const noexcept {
        return detail::read_bool(self, lir_schema::enum_tparam_keys::TP_IS_VARIADIC.code);
    }
};

// LEnumDef { name, pkg, doc: Varchar; zoned2/borrow_carrying: bool (sparse);
//            variants: Array<variant sub-map>; type_params: Array<tparam sub-map> }
struct EnumView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::NAME.code);
    }
    std::string_view pkg() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::PKG.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::DOC.code);
    }
    bool zoned2() const noexcept {
        return detail::read_bool(self, lir_schema::decl_keys::ZONED2.code);
    }
    bool borrow_carrying() const noexcept {
        return detail::read_bool(self, lir_schema::decl_keys::BORROW_CARRYING.code);
    }
    // C-style enum discriminant type (null ⇒ default i32).
    TypeRef backing_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::decl_keys::BACKING_TYPE.code, pool);
    }
    uint64_t variant_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::VARIANTS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    bool type_params_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::TYPE_PARAMS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    // Iterate variants. F is called as f(EnumVariantView) for each variant.
    template <class F>
    void each_variant(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::VARIANTS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(EnumVariantView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    // Iterate type params. F is called as f(EnumTParamView).
    template <class F>
    void each_type_param(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::TYPE_PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(EnumTParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    // True if any variant carries a payload (mirrors LEnumDef::has_payload()).
    bool has_payload() const noexcept {
        bool any = false;
        each_variant([&](EnumVariantView v) { if (v.has_payload()) any = true; });
        return any;
    }
};

// ── Function decl views (Stage E LFunction migration) ────────────────────

// LParam sub-map view { P_NAME, P_TYPE, P_IS_VARIADIC, P_OWNING_BOX_DYN, P_SLOT }.
struct LParamView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::param_keys::P_NAME.code);
    }
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::param_keys::P_TYPE.code, pool);
    }
    bool is_variadic() const noexcept {
        return detail::read_bool(self, lir_schema::param_keys::P_IS_VARIADIC.code);
    }
    bool owning_box_dyn() const noexcept {
        return detail::read_bool(self, lir_schema::param_keys::P_OWNING_BOX_DYN.code);
    }
    uint32_t slot() const noexcept {
        auto av = self.mirror()->get(lir_schema::param_keys::P_SLOT.code);
        if (av.is_null()) return 0xFFFFFFFFu;
        return static_cast<uint32_t>(*av.as_ptr<const int64_t>());
    }
};

// TraitBound sub-map view { TB_TRAIT_NAME, TB_TYPE_ARGS }.
struct FnTraitBoundView {
    DeclRef self;
    std::string_view trait_name() const noexcept {
        return detail::read_string(self, lir_schema::fn_tbound_keys::TB_TRAIT_NAME.code);
    }
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        return detail::read_type_array(self, lir_schema::fn_tbound_keys::TB_TYPE_ARGS.code, pool);
    }
    std::vector<std::string_view> hrtb_binders() const noexcept {
        return detail::read_string_array(self, lir_schema::fn_tbound_keys::TB_HRTB_BINDERS.code);
    }
    // Derived (not stored): trait is one of Fn / FnMut / FnOnce.
    bool is_fn_family() const noexcept {
        auto t = trait_name();
        return t == "Fn" || t == "FnMut" || t == "FnOnce";
    }
};

// Function TypeParam sub-map view (richer than enum's).
struct FnTParamView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::fn_tparam_keys::FTP_NAME.code);
    }
    bool is_variadic() const noexcept {
        return detail::read_bool(self, lir_schema::fn_tparam_keys::FTP_IS_VARIADIC.code);
    }
    bool is_const() const noexcept {
        return detail::read_bool(self, lir_schema::fn_tparam_keys::FTP_IS_CONST.code);
    }
    TypeRef const_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::fn_tparam_keys::FTP_CONST_TYPE.code, pool);
    }
    TypeRef default_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::fn_tparam_keys::FTP_DEFAULT_TYPE.code, pool);
    }
    bool bounds_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::fn_tparam_keys::FTP_BOUNDS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_bound(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::fn_tparam_keys::FTP_BOUNDS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnTraitBoundView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<std::string_view> lifetime_outlives() const noexcept {
        return detail::read_string_array(self, lir_schema::fn_tparam_keys::FTP_LIFETIME_OUTLIVES.code);
    }
};

// where_type_bounds sub-map view { WB_TYPE (subject), WB_TRAIT }.
struct FnWhereBoundView {
    DeclRef self;
    TypeRef subject(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::fn_wherebound_keys::WB_TYPE.code, pool);
    }
    std::string_view trait() const noexcept {
        return detail::read_string(self, lir_schema::fn_wherebound_keys::WB_TRAIT.code);
    }
};

// LFunction decl view — the whole declaration as a Hermes mirror map.
struct FunctionView {
    DeclRef self;

    bool valid() const noexcept { return self.addr() != nullptr; }
    explicit operator bool() const noexcept { return self.addr() != nullptr; }

    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::NAME.code);
    }
    std::string_view method_base() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::METHOD_BASE.code);
    }
    std::string_view package() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::PKG.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::DOC.code);
    }
    std::string_view source_file() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::SOURCE_FILE.code);
    }
    std::string_view should_panic_expected_msg() const noexcept {
        return detail::read_string(self, lir_schema::decl_keys::SHOULD_PANIC_MSG.code);
    }
    TypeRef ret_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::decl_keys::RET_TYPE.code, pool);
    }
    TypeRef impl_target_pattern(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::decl_keys::IMPL_TARGET_PATTERN.code, pool);
    }
    BlockRef body() const noexcept {
        return self.decl_block(lir_schema::decl_keys::BODY.code);
    }
    uint32_t local_count() const noexcept {
        return static_cast<uint32_t>(detail::read_i64(self, lir_schema::decl_keys::LOCAL_COUNT.code));
    }
    bool is_extern() const noexcept          { return detail::read_bool(self, lir_schema::decl_keys::IS_EXTERN.code); }
    bool is_vararg() const noexcept          { return detail::read_bool(self, lir_schema::decl_keys::IS_VARARG.code); }
    bool is_pub() const noexcept             { return detail::read_bool(self, lir_schema::decl_keys::IS_PUB.code); }
    bool is_metaprog_stub() const noexcept   { return detail::read_bool(self, lir_schema::decl_keys::IS_METAPROG_STUB.code); }
    bool is_specialization() const noexcept  { return detail::read_bool(self, lir_schema::decl_keys::IS_SPECIALIZATION.code); }
    bool from_binary_module() const noexcept { return detail::read_bool(self, lir_schema::decl_keys::FROM_BINARY_MODULE.code); }
    bool from_lazy_module() const noexcept   { return detail::read_bool(self, lir_schema::decl_keys::FROM_LAZY_MODULE.code); }
    bool is_test() const noexcept            { return detail::read_bool(self, lir_schema::decl_keys::IS_TEST.code); }
    bool should_panic() const noexcept       { return detail::read_bool(self, lir_schema::decl_keys::SHOULD_PANIC.code); }
    bool ignored() const noexcept            { return detail::read_bool(self, lir_schema::decl_keys::IGNORED.code); }

    hermes::ExternalRef body_external_ref() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::BODY_EXTERNAL_REF.code);
        if (av.is_null() || !hermes::is_external_ref_av(av)) return hermes::ExternalRef{};
        return hermes::decode_external_ref(av);
    }

    // ── params ──
    uint64_t param_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::PARAMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    bool params_empty() const noexcept { return param_count() == 0; }
    template <class F>
    void each_param(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(LParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<LParamView> params() const noexcept {
        std::vector<LParamView> out;
        each_param([&](LParamView p) { out.push_back(p); });
        return out;
    }
    LParamView param(uint64_t i) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::PARAMS.code);
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        return LParamView{detail::make_sub_ref<DeclRef>(self, arr->get(i))};
    }

    // ── type_params / impl_type_params ──
    uint64_t type_param_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::TYPE_PARAMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    bool type_params_empty() const noexcept { return type_param_count() == 0; }
    template <class F>
    void each_type_param(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::TYPE_PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnTParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<FnTParamView> type_params() const noexcept {
        std::vector<FnTParamView> out;
        each_type_param([&](FnTParamView tp) { out.push_back(tp); });
        return out;
    }
    uint64_t impl_type_param_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::IMPL_TYPE_PARAMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    bool impl_type_params_empty() const noexcept { return impl_type_param_count() == 0; }
    template <class F>
    void each_impl_type_param(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::IMPL_TYPE_PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnTParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<FnTParamView> impl_type_params() const noexcept {
        std::vector<FnTParamView> out;
        each_impl_type_param([&](FnTParamView tp) { out.push_back(tp); });
        return out;
    }

    // ── where_type_bounds ──
    bool where_type_bounds_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::WHERE_TYPE_BOUNDS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_where_bound(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::decl_keys::WHERE_TYPE_BOUNDS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnWhereBoundView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }

    // ── spec_patterns ──
    std::vector<TypeRef> spec_patterns(const TypePoolImpl* pool) const noexcept {
        return detail::read_type_array(self, lir_schema::decl_keys::SPEC_PATTERNS.code, pool);
    }

    // ── lifetime_params / lifetime_outlives ──
    std::vector<std::string_view> lifetime_params() const noexcept {
        return detail::read_string_array(self, lir_schema::decl_keys::LIFETIME_PARAMS.code);
    }
    // flat array (2i=long, 2i+1=short) → pairs.
    std::vector<std::pair<std::string_view, std::string_view>> lifetime_outlives() const noexcept {
        std::vector<std::pair<std::string_view, std::string_view>> out;
        auto av = self.mirror()->get(lir_schema::decl_keys::LIFETIME_OUTLIVES.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i + 1 < arr->size(); i += 2) {
            auto a = arr->get(i); auto b = arr->get(i + 1);
            out.emplace_back(a.is_null() ? std::string_view{} : a.as_ptr<const hermes::ArenaString>()->view(),
                             b.is_null() ? std::string_view{} : b.as_ptr<const hermes::ArenaString>()->view());
        }
        return out;
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Stage E: LStructDef decl views (Code::Struct map + field/annotation sub-maps)
// ──────────────────────────────────────────────────────────────────────────

// FIELDS array element (LField sub-map; field_keys).
struct LFieldView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::field_keys::F_NAME.code);
    }
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::field_keys::F_TYPE.code, pool);
    }
    bool is_variadic() const noexcept {
        return detail::read_bool(self, lir_schema::field_keys::F_IS_VARIADIC.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::field_keys::F_DOC.code);
    }
};

// LAnnotationValue sub-map view (annval_keys; RECURSIVE via each_arr).
struct AnnotValueView {
    DeclRef self;
    int64_t kind() const noexcept {
        return detail::read_i64(self, lir_schema::annval_keys::AV_KIND.code);
    }
    int64_t i() const noexcept {
        return detail::read_i64(self, lir_schema::annval_keys::AV_I.code);
    }
    double f() const noexcept {
        return detail::read_f64(self, lir_schema::annval_keys::AV_F.code);
    }
    std::string_view s() const noexcept {
        return detail::read_string(self, lir_schema::annval_keys::AV_S.code);
    }
    std::string_view enum_name() const noexcept {
        return detail::read_string(self, lir_schema::annval_keys::AV_ENUM_NAME.code);
    }
    std::string_view enum_variant() const noexcept {
        return detail::read_string(self, lir_schema::annval_keys::AV_ENUM_VARIANT.code);
    }
    template <class F>
    void each_arr(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::annval_keys::AV_ARR.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(AnnotValueView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// A_KV array element (annkv_keys): (field name, value).
struct AnnotKvView {
    DeclRef self;
    std::string_view key_name() const noexcept {
        return detail::read_string(self, lir_schema::annkv_keys::KV_NAME.code);
    }
    AnnotValueView value() const noexcept {
        auto av = self.mirror()->get(lir_schema::annkv_keys::KV_VALUE.code);
        return AnnotValueView{detail::make_sub_ref<DeclRef>(self, av)};
    }
};

// ANNOTATIONS array element (LAnnotationInstance sub-map; annot_keys).
struct AnnotInstanceView {
    DeclRef self;
    std::string_view ann_name() const noexcept {
        return detail::read_string(self, lir_schema::annot_keys::A_NAME.code);
    }
    std::string_view ann_pkg() const noexcept {
        return detail::read_string(self, lir_schema::annot_keys::A_PKG.code);
    }
    std::string_view ann_fqn() const noexcept {
        return detail::read_string(self, lir_schema::annot_keys::A_FQN.code);
    }
    template <class F>
    void each_kv(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::annot_keys::A_KV.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(AnnotKvView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// LStructDef decl view (Code::Struct map; struct_keys).
struct StructView {
    DeclRef self;

    bool valid() const noexcept { return self.addr() != nullptr; }
    explicit operator bool() const noexcept { return self.addr() != nullptr; }

    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::struct_keys::NAME.code);
    }
    std::string_view pkg() const noexcept {
        return detail::read_string(self, lir_schema::struct_keys::PKG.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::struct_keys::DOC.code);
    }
    uint64_t type_code() const noexcept {
        return static_cast<uint64_t>(detail::read_i64(self, lir_schema::struct_keys::TYPE_CODE.code));
    }
    // Raw 23-byte hash (string_view points into the arena; empty when unset).
    std::string_view type_hash() const noexcept {
        return detail::read_string(self, lir_schema::struct_keys::TYPE_HASH.code);
    }

    bool is_pub() const noexcept             { return detail::read_bool(self, lir_schema::struct_keys::IS_PUB.code); }
    bool is_zoned() const noexcept           { return detail::read_bool(self, lir_schema::struct_keys::IS_ZONED.code); }
    bool is_data_plain() const noexcept      { return detail::read_bool(self, lir_schema::struct_keys::IS_DATA_PLAIN.code); }
    bool from_binary_module() const noexcept { return detail::read_bool(self, lir_schema::struct_keys::FROM_BINARY_MODULE.code); }
    bool is_dst() const noexcept             { return detail::read_bool(self, lir_schema::struct_keys::IS_DST.code); }
    bool self_describing() const noexcept    { return detail::read_bool(self, lir_schema::struct_keys::SELF_DESCRIBING.code); }
    bool rel_ptr() const noexcept            { return detail::read_bool(self, lir_schema::struct_keys::REL_PTR.code); }
    bool borrow_carrying() const noexcept    { return detail::read_bool(self, lir_schema::struct_keys::BORROW_CARRYING.code); }
    bool non_null() const noexcept           { return detail::read_bool(self, lir_schema::struct_keys::NON_NULL.code); }
    bool zone_mut() const noexcept           { return detail::read_bool(self, lir_schema::struct_keys::ZONE_MUT.code); }
    bool zoned2() const noexcept             { return detail::read_bool(self, lir_schema::struct_keys::ZONED2.code); }
    bool is_union() const noexcept           { return detail::read_bool(self, lir_schema::struct_keys::IS_UNION.code); }
    bool repr_transparent() const noexcept   { return detail::read_bool(self, lir_schema::struct_keys::REPR_TRANSPARENT.code); }
    bool is_annotation_type() const noexcept { return detail::read_bool(self, lir_schema::struct_keys::IS_ANNOTATION_TYPE.code); }
    bool is_specialization() const noexcept  { return detail::read_bool(self, lir_schema::struct_keys::IS_SPECIALIZATION.code); }

    // ── fields ──
    template <class F>
    void each_field(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::FIELDS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(LFieldView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<LFieldView> fields() const noexcept {
        std::vector<LFieldView> out;
        each_field([&](LFieldView fv) { out.push_back(fv); });
        return out;
    }

    // ── methods (each METHODS element is a func decl map → FunctionView) ──
    template <class F>
    void each_method(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::METHODS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(FunctionView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<FunctionView> methods() const noexcept {
        std::vector<FunctionView> out;
        each_method([&](FunctionView m) { out.push_back(m); });
        return out;
    }

    // ── type_params (rich fn variant) ──
    uint64_t type_param_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::TYPE_PARAMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    bool type_params_empty() const noexcept { return type_param_count() == 0; }
    template <class F>
    void each_type_param(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::TYPE_PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(FnTParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    std::vector<FnTParamView> type_params() const noexcept {
        std::vector<FnTParamView> out;
        each_type_param([&](FnTParamView tp) { out.push_back(tp); });
        return out;
    }

    // ── spec_patterns ──
    std::vector<TypeRef> spec_patterns(const TypePoolImpl* pool) const noexcept {
        return detail::read_type_array(self, lir_schema::struct_keys::SPEC_PATTERNS.code, pool);
    }

    // ── lifetime_params / lifetime_outlives ──
    std::vector<std::string_view> lifetime_params() const noexcept {
        return detail::read_string_array(self, lir_schema::struct_keys::LIFETIME_PARAMS.code);
    }
    std::vector<std::pair<std::string_view, std::string_view>> lifetime_outlives() const noexcept {
        std::vector<std::pair<std::string_view, std::string_view>> out;
        auto av = self.mirror()->get(lir_schema::struct_keys::LIFETIME_OUTLIVES.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k + 1 < arr->size(); k += 2) {
            auto a = arr->get(k); auto b = arr->get(k + 1);
            out.emplace_back(a.is_null() ? std::string_view{} : a.as_ptr<const hermes::ArenaString>()->view(),
                             b.is_null() ? std::string_view{} : b.as_ptr<const hermes::ArenaString>()->view());
        }
        return out;
    }

    // ── annotations ──
    bool annotations_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::ANNOTATIONS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_annotation(F&& fn) const noexcept {
        auto av = self.mirror()->get(lir_schema::struct_keys::ANNOTATIONS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t k = 0; k < arr->size(); ++k) {
            auto el = arr->get(k);
            if (el.is_null()) continue;
            fn(AnnotInstanceView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// ── Stage E: LTraitDef decl views (Code::Trait map + assoc/method sub-maps) ──

// LAssocTypeDef sub-map view { AT_NAME, AT_BOUNDS: Array<tbound>, AT_DOC }.
struct AssocTypeDefView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::assoc_type_keys::AT_NAME.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::assoc_type_keys::AT_DOC.code);
    }
    template <class F>
    void each_bound(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::assoc_type_keys::AT_BOUNDS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnTraitBoundView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// LTraitMethodSig sub-map view { TM_NAME, TM_RET_TYPE, TM_DOC } (no params).
struct TraitMethodSigView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::trait_method_keys::TM_NAME.code);
    }
    TypeRef ret_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::trait_method_keys::TM_RET_TYPE.code, pool);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::trait_method_keys::TM_DOC.code);
    }
};

// LTraitDef decl view.
struct TraitView {
    DeclRef self;
    bool valid() const noexcept { return self.addr() != nullptr; }
    explicit operator bool() const noexcept { return self.addr() != nullptr; }

    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::trait_keys::NAME.code);
    }
    std::string_view pkg() const noexcept {
        return detail::read_string(self, lir_schema::trait_keys::PKG.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::trait_keys::DOC.code);
    }
    std::string_view tag_dispatch_system() const noexcept {
        return detail::read_string(self, lir_schema::trait_keys::TAG_DISPATCH_SYSTEM.code);
    }
    uint64_t type_code() const noexcept {
        return static_cast<uint64_t>(detail::read_i64(self, lir_schema::trait_keys::TYPE_CODE.code));
    }
    bool is_auto() const noexcept {
        return detail::read_bool(self, lir_schema::trait_keys::IS_AUTO.code);
    }
    std::vector<std::string_view> type_params() const noexcept {
        return detail::read_string_array(self, lir_schema::trait_keys::TYPE_PARAMS.code);
    }
    std::vector<std::string_view> supertraits() const noexcept {
        return detail::read_string_array(self, lir_schema::trait_keys::SUPERTRAITS.code);
    }
    std::vector<std::string_view> upcast_supertraits() const noexcept {
        return detail::read_string_array(self, lir_schema::trait_keys::UPCAST_SUPERTRAITS.code);
    }
    // flat array (2i=owner, 2i+1=method) → pairs.
    std::vector<std::pair<std::string_view, std::string_view>> vtable_method_order() const noexcept {
        std::vector<std::pair<std::string_view, std::string_view>> out;
        auto av = self.mirror()->get(lir_schema::trait_keys::VTABLE_METHOD_ORDER.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i + 1 < arr->size(); i += 2) {
            auto a = arr->get(i); auto b = arr->get(i + 1);
            out.emplace_back(a.is_null() ? std::string_view{} : a.as_ptr<const hermes::ArenaString>()->view(),
                             b.is_null() ? std::string_view{} : b.as_ptr<const hermes::ArenaString>()->view());
        }
        return out;
    }
    bool assoc_types_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::trait_keys::ASSOC_TYPES.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_assoc_type(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::trait_keys::ASSOC_TYPES.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(AssocTypeDefView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    uint64_t method_count() const noexcept {
        auto av = self.mirror()->get(lir_schema::trait_keys::METHODS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    template <class F>
    void each_method(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::trait_keys::METHODS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(TraitMethodSigView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// ── Stage E: LImplBlock decl views (Code::Impl map + assoc/extra-eq sub-maps) ──

// assoc_entry sub-map view { AE_NAME, AE_TYPE } — one (name → type) pair.
struct AssocEntryView {
    DeclRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, lir_schema::assoc_entry_keys::AE_NAME.code);
    }
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::assoc_entry_keys::AE_TYPE.code, pool);
    }
};

// extra_eq sub-map view { EE_TRAIT, EE_EQS: Array<assoc_entry> }.
struct ExtraEqView {
    DeclRef self;
    std::string_view trait() const noexcept {
        return detail::read_string(self, lir_schema::extra_eq_keys::EE_TRAIT.code);
    }
    template <class F>
    void each_eq(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::extra_eq_keys::EE_EQS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(AssocEntryView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// LImplBlock decl view.
struct ImplView {
    DeclRef self;
    bool valid() const noexcept { return self.addr() != nullptr; }
    explicit operator bool() const noexcept { return self.addr() != nullptr; }

    std::string_view trait_name() const noexcept {
        return detail::read_string(self, lir_schema::impl_keys::TRAIT_NAME.code);
    }
    std::string_view target_type() const noexcept {
        return detail::read_string(self, lir_schema::impl_keys::TARGET_TYPE.code);
    }
    std::string_view bound_trait() const noexcept {
        return detail::read_string(self, lir_schema::impl_keys::BOUND_TRAIT.code);
    }
    bool is_blanket() const noexcept {
        return detail::read_bool(self, lir_schema::impl_keys::IS_BLANKET.code);
    }
    bool is_negative() const noexcept {
        return detail::read_bool(self, lir_schema::impl_keys::IS_NEGATIVE.code);
    }
    std::vector<std::string_view> extra_bounds() const noexcept {
        return detail::read_string_array(self, lir_schema::impl_keys::EXTRA_BOUNDS.code);
    }
    TypeRef target_typeref(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::impl_keys::TARGET_TYPEREF.code, pool);
    }
    std::vector<TypeRef> trait_type_args(const TypePoolImpl* pool) const noexcept {
        return detail::read_type_array(self, lir_schema::impl_keys::TRAIT_TYPE_ARGS.code, pool);
    }
    std::vector<std::string_view> impl_lifetime_params() const noexcept {
        return detail::read_string_array(self, lir_schema::impl_keys::IMPL_LIFETIME_PARAMS.code);
    }
    // flat array (2i=long, 2i+1=short) → pairs.
    std::vector<std::pair<std::string_view, std::string_view>> lifetime_outlives() const noexcept {
        std::vector<std::pair<std::string_view, std::string_view>> out;
        auto av = self.mirror()->get(lir_schema::impl_keys::LIFETIME_OUTLIVES.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i + 1 < arr->size(); i += 2) {
            auto a = arr->get(i); auto b = arr->get(i + 1);
            out.emplace_back(a.is_null() ? std::string_view{} : a.as_ptr<const hermes::ArenaString>()->view(),
                             b.is_null() ? std::string_view{} : b.as_ptr<const hermes::ArenaString>()->view());
        }
        return out;
    }
    bool impl_type_params_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::IMPL_TYPE_PARAMS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_impl_type_param(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::IMPL_TYPE_PARAMS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(FnTParamView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    bool assoc_types_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::ASSOC_TYPES.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_assoc_type(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::ASSOC_TYPES.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(AssocEntryView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    // Linear scan over assoc_types; returns the first matching type, else null.
    TypeRef find_assoc_type(std::string_view nm, const TypePoolImpl* pool) const noexcept {
        TypeRef found{};
        each_assoc_type([&](AssocEntryView ae) {
            if (!found && ae.name() == nm) found = ae.type(pool);
        });
        return found;
    }
    bool primary_assoc_eqs_empty() const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::PRIMARY_ASSOC_EQS.code);
        if (av.is_null()) return true;
        return av.as_ptr<const hermes::ObjectArray>()->size() == 0;
    }
    template <class F>
    void each_primary_assoc_eq(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::PRIMARY_ASSOC_EQS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(AssocEntryView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
    template <class F>
    void each_extra_assoc_eq(F&& f) const noexcept {
        auto av = self.mirror()->get(lir_schema::impl_keys::EXTRA_ASSOC_EQS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(ExtraEqView{detail::make_sub_ref<DeclRef>(self, el)});
        }
    }
};

// ── Stage E: LInstAnnotation / LDispatchEntry / LReflectGlobal decl views ─────

// LInstAnnotation { canonical_name, mangled_name: Varchar; type_code: u64;
//   struct_type: RelPtr<LogosType>; is_root_pin, is_pub_reexport: bool }.
struct InstAnnotView {
    DeclRef self;
    std::string_view canonical_name() const noexcept {
        return detail::read_string(self, lir_schema::inst_annot_keys::CANONICAL_NAME.code);
    }
    std::string_view mangled_name() const noexcept {
        return detail::read_string(self, lir_schema::inst_annot_keys::MANGLED_NAME.code);
    }
    uint64_t type_code() const noexcept {
        return detail::read_u64(self, lir_schema::inst_annot_keys::TYPE_CODE.code);
    }
    TypeRef struct_type(const TypePoolImpl* pool) const noexcept {
        return self.decl_type(lir_schema::inst_annot_keys::STRUCT_TYPE.code, pool);
    }
    bool is_root_pin() const noexcept {
        return detail::read_bool(self, lir_schema::inst_annot_keys::IS_ROOT_PIN.code);
    }
    bool is_pub_reexport() const noexcept {
        return detail::read_bool(self, lir_schema::inst_annot_keys::IS_PUB_REEXPORT.code);
    }
};

// LDispatchEntry { tag_system, trait_name, method_name, fn_symbol,
//   impl_type_name: Varchar; type_code: u64 }.
struct DispatchEntryView {
    DeclRef self;
    std::string_view tag_system() const noexcept {
        return detail::read_string(self, lir_schema::dispatch_keys::TAG_SYSTEM.code);
    }
    std::string_view trait_name() const noexcept {
        return detail::read_string(self, lir_schema::dispatch_keys::TRAIT_NAME.code);
    }
    std::string_view method_name() const noexcept {
        return detail::read_string(self, lir_schema::dispatch_keys::METHOD_NAME.code);
    }
    std::string_view fn_symbol() const noexcept {
        return detail::read_string(self, lir_schema::dispatch_keys::FN_SYMBOL.code);
    }
    std::string_view impl_type_name() const noexcept {
        return detail::read_string(self, lir_schema::dispatch_keys::IMPL_TYPE_NAME.code);
    }
    uint64_t type_code() const noexcept {
        return detail::read_u64(self, lir_schema::dispatch_keys::TYPE_CODE.code);
    }
};

// LReflectGlobal { symbol: Varchar; blob: Varchar (raw bytes) }.
struct ReflectGlobalView {
    DeclRef self;
    std::string_view symbol() const noexcept {
        return detail::read_string(self, lir_schema::reflect_keys::SYMBOL.code);
    }
    // Raw bytes of the TypeInfo blob, length-prefixed (NUL-safe).
    std::string_view blob() const noexcept {
        return detail::read_string(self, lir_schema::reflect_keys::BLOB.code);
    }
};

// ── Stage E: metaprog / metacall driver tables decl views ────────────────────

// MetacallSite return-type discriminator (hoisted out of the old
// LProgram::MetacallSite struct to namespace scope so MetacallSiteView and the
// main.cpp driver can name it without the struct). Stored in the mirror as i64.
} // namespace logos::compiler::lir_view
namespace logos::compiler::lir {
enum class MetacallRetTag : int32_t {
    Bool, I8, I16, I24, I32, I56, I64, U8, U16, U24, U32, U56, U64,
    F32, F64, Str, HermesStatic, Hermes, ExprBlob, ItemBlob
};
} // namespace logos::compiler::lir
namespace logos::compiler::lir_view {

// MetaprogHandler { trigger, hook_fn: Varchar }.
struct MetaprogHandlerView {
    DeclRef self;
    std::string_view trigger() const noexcept {
        return detail::read_string(self, lir_schema::mp_handler_keys::TRIGGER.code);
    }
    std::string_view hook_fn() const noexcept {
        return detail::read_string(self, lir_schema::mp_handler_keys::HOOK_FN.code);
    }
};

// MetaprogTarget { ast_idx, item_offset: i64; trigger: Varchar }.
struct MetaprogTargetView {
    DeclRef self;
    size_t ast_idx() const noexcept {
        return (size_t)detail::read_i64(self, lir_schema::mp_target_keys::AST_IDX.code);
    }
    uint32_t item_offset() const noexcept {
        return (uint32_t)detail::read_i64(self, lir_schema::mp_target_keys::ITEM_OFFSET.code);
    }
    std::string_view trigger() const noexcept {
        return detail::read_string(self, lir_schema::mp_target_keys::TRIGGER.code);
    }
};

// MetacallSite { ast_idx, expr_offset: i64; thunk_name, thunk_source: Varchar;
//   ret_tag: i64 (MetacallRetTag); callee_name: Varchar }.
struct MetacallSiteView {
    DeclRef self;
    size_t ast_idx() const noexcept {
        return (size_t)detail::read_i64(self, lir_schema::metacall_keys::AST_IDX.code);
    }
    uint32_t expr_offset() const noexcept {
        return (uint32_t)detail::read_i64(self, lir_schema::metacall_keys::EXPR_OFFSET.code);
    }
    std::string_view thunk_name() const noexcept {
        return detail::read_string(self, lir_schema::metacall_keys::THUNK_NAME.code);
    }
    std::string_view thunk_source() const noexcept {
        return detail::read_string(self, lir_schema::metacall_keys::THUNK_SOURCE.code);
    }
    lir::MetacallRetTag ret_tag() const noexcept {
        return lir::MetacallRetTag(
            (int32_t)detail::read_i64(self, lir_schema::metacall_keys::RET_TAG.code));
    }
    std::string_view callee_name() const noexcept {
        return detail::read_string(self, lir_schema::metacall_keys::CALLEE_NAME.code);
    }
};

// ModuleInnerDoc { module, doc: Varchar }.
struct ModuleInnerDocView {
    DeclRef self;
    std::string_view module() const noexcept {
        return detail::read_string(self, lir_schema::module_doc_keys::MODULE.code);
    }
    std::string_view doc() const noexcept {
        return detail::read_string(self, lir_schema::module_doc_keys::DOC.code);
    }
};

// ── Inline accessors that need the above forward decls ───────────────────
//
// Phase 2.B: each sub_* method routes through detail::resolve_child() which
// transparently dispatches on ExternalRef. Single-arena code paths take the
// [[likely]] local branch with no overhead beyond a 1-byte TypeTag compare.

// Phase 5.B step 3: when a sub-ref resolves to a within-arena reference but
// the parent is itself cross-arena, the child must inherit the parent's
// arena_id — otherwise downstream is_external() checks (e.g. on
// TypeRef-fetching paths) lose track of the foreign-arena membership and
// the offset gets interpreted in the wrong arena later. detail::make_sub_ref
// + the explicit branch on loc.aid below fold the propagation in.

inline ExprRef ExprRef::sub_expr(uint8_t key) const noexcept {
    auto av = mirror()->get(key);
    auto loc = detail::resolve_child(*this, av);
    if (!loc) return {};
    if (loc.aid.is_valid()) return ExprRef(loc.arena, loc.av, loc.aid);
    return detail::make_sub_ref<ExprRef>(*this, loc.av);
}

inline ExprRef StmtRef::sub_expr(uint8_t key) const noexcept {
    auto av = mirror()->get(key);
    auto loc = detail::resolve_child(*this, av);
    if (!loc) return {};
    if (loc.aid.is_valid()) return ExprRef(loc.arena, loc.av, loc.aid);
    return detail::make_sub_ref<ExprRef>(*this, loc.av);
}

inline StmtRef StmtRef::sub_stmt(uint8_t key) const noexcept {
    auto av = mirror()->get(key);
    auto loc = detail::resolve_child(*this, av);
    if (!loc) return {};
    if (loc.aid.is_valid()) return StmtRef(loc.arena, loc.av, loc.aid);
    return detail::make_sub_ref<StmtRef>(*this, loc.av);
}

// Iterate stmts inside a block. The mirror stores them at stmt_keys::ARMS (24),
// reusing the same key for SMatch.arms — see lir_mirror.cpp:emit_block.
template <class F>
inline void BlockRef::each_stmt(F&& f) const noexcept {
    auto av = mirror()->get(/*stmt_keys::ARMS*/ 24);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        auto loc = detail::resolve_child(*this, el);
        if (!loc) continue;
        if (loc.aid.is_valid()) f(StmtRef(loc.arena, loc.av, loc.aid));
        else                    f(detail::make_sub_ref<StmtRef>(*this, loc.av));
    }
}

// ── View structs ─────────────────────────────────────────────────────────
//
// Each EXxxView holds the matching ref; accessors lazily decode fields.
// Filled in JIT as readers migrate off the std::variant tree.

namespace ek = lir_schema::expr_keys;
namespace pk = lir_schema::pat_keys;
namespace ak = lir_schema::arm_keys;

namespace detail {

// Iterate an Array<RelPtr<LExpr>> stored at `key`. F is called as
// f(ExprRef) for each element. No-op if the key is null.
template <class F>
void for_each_expr(const RefBase& r, uint8_t key, F&& f) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        if (el.is_null()) { f(ExprRef{}); continue; }
        f(detail::make_sub_ref<ExprRef>(r, el));
    }
}

} // namespace detail

// ── Match-arm views ──────────────────────────────────────────────────────
//
// The mirror represents both LMatchArm (statement-style, with body block)
// and EMatchArm (expression-style, with value expr) as TinyObjectMaps.

class EMatchArmRef : public detail::RefBase {
public:
    EMatchArmRef() = default;
    using RefBase::RefBase;

    PatRef  pat() const noexcept {
        auto av = mirror()->get(ak::PAT.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<PatRef>(*this, av);
    }
    ExprRef value() const noexcept {
        auto av = mirror()->get(ak::VALUE.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<ExprRef>(*this, av);
    }
    ExprRef guard() const noexcept {
        auto av = mirror()->get(ak::GUARD.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<ExprRef>(*this, av);
    }
    BlockRef body() const noexcept {
        auto av = mirror()->get(ak::BODY.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<BlockRef>(*this, av);
    }
};

// ── LExpr variant views ──────────────────────────────────────────────────

// EVarRef { name: Varchar }
struct EVarRefView {
    ExprRef self;
    std::string_view name() const noexcept { return detail::read_string(self, ek::NAME.code); }
    // Phase-1: dense per-function variable slot assigned by sema (0xFFFFFFFF =
    // no slot — a synthesized/module-const ref that downstream must name-key).
    uint32_t var_slot() const noexcept {
        auto v = detail::read_i64_opt(self, ek::VAR_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

// EAddrOf { var_name: Varchar }
struct EAddrOfView {
    ExprRef self;
    std::string_view var_name() const noexcept { return detail::read_string(self, ek::NAME.code); }
};

// EFieldRead { receiver: LExpr, field: Varchar }
struct EFieldReadView {
    ExprRef self;
    ExprRef receiver() const noexcept { return self.sub_expr(ek::RECEIVER.code); }
    std::string_view field() const noexcept { return detail::read_string(self, ek::NAME.code); }
};

// EDeref { operand: LExpr }
struct EDerefView {
    ExprRef self;
    ExprRef operand() const noexcept { return self.sub_expr(ek::OPERAND.code); }
};

// ETupleIndex { receiver: LExpr, index: u32 }
struct ETupleIndexView {
    ExprRef self;
    ExprRef receiver() const noexcept { return self.sub_expr(ek::RECEIVER.code); }
    uint32_t index() const noexcept   { return detail::read_u32(self, ek::TUPLE_INDEX_VAL.code); }
};

// ECast { operand: LExpr }
struct ECastView {
    ExprRef self;
    ExprRef operand() const noexcept { return self.sub_expr(ek::OPERAND.code); }
    std::string_view hermes_build_fn() const noexcept {
        return detail::read_string(self, ek::HERMES_BUILD_FN.code);
    }
};

// EIndexRead { receiver: LExpr, index: LExpr }
struct EIndexReadView {
    ExprRef self;
    ExprRef receiver() const noexcept { return self.sub_expr(ek::RECEIVER.code); }
    ExprRef index() const noexcept    { return self.sub_expr(ek::INDEX.code); }
};

// EIfExpr { cond: LExpr, then_val: LExpr, else_val: LExpr }
struct EIfExprView {
    ExprRef self;
    ExprRef cond() const noexcept     { return self.sub_expr(ek::COND.code); }
    ExprRef then_val() const noexcept { return self.sub_expr(ek::THEN_VAL.code); }
    ExprRef else_val() const noexcept { return self.sub_expr(ek::ELSE_VAL.code); }
};

// EBlockExpr { block: BlockRef, result: LExpr }
struct EBlockExprView {
    ExprRef self;
    ExprRef result() const noexcept { return self.sub_expr(ek::RESULT.code); }
    BlockRef block() const noexcept {
        auto av = self.mirror()->get(ek::BLOCK.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<BlockRef>(self, av);
    }
};

// EMatchExpr { scrut: LExpr, arms: Array<EMatchArm> }
struct EMatchExprView {
    ExprRef self;
    ExprRef scrut() const noexcept { return self.sub_expr(ek::SCRUT.code); }

    // Iterate arms. F is called as f(EMatchArmRef) for each arm.
    template <class F>
    void each_arm(F&& f) const noexcept {
        auto av = self.mirror()->get(ek::ARMS.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) continue;
            f(detail::make_sub_ref<EMatchArmRef>(self, el));
        }
    }
};

// ── Leaf-shape exemplars (kept for reference) ────────────────────────────

struct ELitIntView {
    ExprRef self;
    int64_t value() const noexcept { return detail::read_i64(self, ek::LIT_I64.code); }
    // High 64 bits of a 128-bit literal (i128/u128); absent ⇒ 0.
    int64_t value_hi() const noexcept { return detail::read_i64(self, ek::LIT_I64_HI.code); }
};

struct ELitFloatView {
    ExprRef self;
    double value() const noexcept { return detail::read_f64(self, ek::LIT_F64.code); }
};

struct ELitBoolView {
    ExprRef self;
    bool value() const noexcept { return detail::read_bool(self, ek::LIT_BOOL.code); }
};

struct ELitStrView {
    ExprRef self;
    std::string_view value() const noexcept { return detail::read_string(self, ek::LIT_STR.code); }
};

struct EBinOpView {
    ExprRef self;
    std::string_view op() const noexcept { return detail::read_string(self, ek::OP.code); }
    ExprRef lhs() const noexcept { return self.sub_expr(ek::LHS.code); }
    ExprRef rhs() const noexcept { return self.sub_expr(ek::RHS.code); }
};

struct EUnaryView {
    ExprRef self;
    std::string_view op() const noexcept { return detail::read_string(self, ek::OP.code); }
    ExprRef operand() const noexcept { return self.sub_expr(ek::OPERAND.code); }
};

struct ETryView {
    ExprRef self;
    ExprRef inner() const noexcept { return self.sub_expr(ek::INNER.code); }
    int32_t ok_disc()  const noexcept { return int32_t(detail::read_u32(self, ek::OK_DISC.code)); }
    int32_t err_disc() const noexcept { return int32_t(detail::read_u32(self, ek::ERR_DISC.code)); }
};

struct ESliceLitView {
    ExprRef self;
    ExprRef base() const noexcept { return self.sub_expr(ek::BASE_PTR.code); }
    ExprRef len()  const noexcept { return self.sub_expr(ek::LEN.code); }
};

struct ESliceIndexView {
    ExprRef self;
    ExprRef slice() const noexcept { return self.sub_expr(ek::SLICE.code); }
    ExprRef index() const noexcept { return self.sub_expr(ek::INDEX.code); }
};

struct ESliceLenView {
    ExprRef self;
    ExprRef slice() const noexcept { return self.sub_expr(ek::SLICE.code); }
};

struct ESlicePtrView {
    ExprRef self;
    ExprRef slice() const noexcept { return self.sub_expr(ek::SLICE.code); }
};

namespace detail {
template <class F>
void for_each_arg(const ExprRef& e, F&& f) noexcept {
    for_each_expr(e, ek::ARGS.code, std::forward<F>(f));
}
template <class F>
void for_each_elem(const ExprRef& e, F&& f) noexcept {
    for_each_expr(e, ek::ELEMS.code, std::forward<F>(f));
}
template <class F>
void for_each_field_value(const ExprRef& e, F&& f) noexcept {
    for_each_expr(e, ek::FIELD_VALUES.code, std::forward<F>(f));
}
template <class F>
void for_each_payload(const ExprRef& e, F&& f) noexcept {
    for_each_expr(e, ek::PAYLOAD.code, std::forward<F>(f));
}
} // namespace detail

struct ECallView {
    ExprRef self;
    std::string_view callee() const noexcept { return detail::read_string(self, ek::CALLEE.code); }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }

    // True if TYPE_ARGS is non-empty (post-substitution generic call).
    bool has_type_args() const noexcept {
        auto av = self.mirror()->get(ek::TYPE_ARGS.code);
        if (av.is_null()) return false;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        return arr->size() > 0;
    }

    // Read TYPE_ARGS into a TypeRef vector. Pool is used so the returned
    // TypeRefs carry the caller's TypePoolImpl* (needed for accessors that
    // touch trait/impl resolution).
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::TYPE_ARGS.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) { out.emplace_back(); continue; }
            out.push_back(detail::make_child_typeref(self, el, pool));
        }
        return out;
    }
};

struct EMethodCallView {
    ExprRef self;
    ExprRef receiver() const noexcept { return self.sub_expr(ek::RECEIVER.code); }
    std::string_view method() const noexcept          { return detail::read_string(self, ek::METHOD.code); }
    std::string_view resolved_symbol() const noexcept { return detail::read_string(self, ek::RESOLVED_SYMBOL.code); }
    std::string_view resolved_type() const noexcept   { return detail::read_string(self, ek::RESOLVED_TYPE.code); }
    std::string_view tag_system() const noexcept      { return detail::read_string(self, ek::TAG_SYSTEM.code); }
    std::string_view tag_trait() const noexcept       { return detail::read_string(self, ek::TAG_TRAIT.code); }
    int32_t          vtable_index() const noexcept {
        auto av = self.mirror()->get(ek::VTABLE_INDEX.code);
        if (av.is_null()) return -1;
        return int32_t(detail::read_u32(self, ek::VTABLE_INDEX.code));
    }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::TYPE_ARGS.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) { out.emplace_back(); continue; }
            out.push_back(detail::make_child_typeref(self, el, pool));
        }
        return out;
    }
};

struct EClosureCallView {
    ExprRef self;
    ExprRef callee() const noexcept { return self.sub_expr(ek::CALLEE.code); }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }
};

struct EFnPtrCallView {
    ExprRef self;
    ExprRef callee() const noexcept { return self.sub_expr(ek::CALLEE.code); }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }
};

struct EFormatCallView {
    ExprRef self;
    ExprRef fmt() const noexcept { return self.sub_expr(ek::FMT.code); }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }
    std::vector<TypeRef> arg_types(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::ARG_TYPES.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) { out.emplace_back(); continue; }
            out.push_back(detail::make_child_typeref(self, el, pool));
        }
        return out;
    }
};

struct EStructLitView {
    ExprRef self;
    std::string_view name() const noexcept { return detail::read_string(self, ek::STRUCT_NAME.code); }
    template <class F> void each_field_value(F&& f) const noexcept {
        detail::for_each_field_value(self, std::forward<F>(f));
    }
    // Iterate (name, value) pairs from parallel FIELD_NAMES / FIELD_VALUES arrays.
    template <class F> void each_field(F&& f) const noexcept {
        auto names_av  = self.mirror()->get(ek::FIELD_NAMES.code);
        auto values_av = self.mirror()->get(ek::FIELD_VALUES.code);
        if (names_av.is_null() || values_av.is_null()) return;
        uint64_t n = std::min(
            names_av.as_ptr<const hermes::ObjectArray>()->size(),
            values_av.as_ptr<const hermes::ObjectArray>()->size());
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>()->get(i);
            auto vv = values_av.as_ptr<const hermes::ObjectArray>()->get(i);
            std::string_view fname;
            if (!nv.is_null())
                fname = nv.as_ptr<const hermes::ArenaString>()->view();
            ExprRef value;
            if (!vv.is_null()) value = detail::make_sub_ref<ExprRef>(self, vv);
            f(fname, value);
        }
    }
};

struct EArrLitView {
    ExprRef self;
    template <class F> void each_elem(F&& f) const noexcept {
        detail::for_each_elem(self, std::forward<F>(f));
    }
    uint64_t count() const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    ExprRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code);
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return {};
        auto el = arr->get(i);
        if (el.is_null()) return {};
        return detail::make_sub_ref<ExprRef>(self, el);
    }
};

struct ETupleLitView {
    ExprRef self;
    template <class F> void each_elem(F&& f) const noexcept {
        detail::for_each_elem(self, std::forward<F>(f));
    }
    uint64_t count() const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    ExprRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code);
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return {};
        auto el = arr->get(i);
        if (el.is_null()) return {};
        return detail::make_sub_ref<ExprRef>(self, el);
    }
};

struct EEnumLitDataView {
    ExprRef self;
    std::string_view enum_name() const noexcept { return detail::read_string(self, ek::ENUM_NAME.code); }
    std::string_view variant()   const noexcept { return detail::read_string(self, ek::VARIANT.code); }
    int64_t          disc()      const noexcept { return detail::read_i64(self, ek::DISC.code); }
    template <class F> void each_payload(F&& f) const noexcept {
        detail::for_each_payload(self, std::forward<F>(f));
    }
};

// EClosureBox { closure: RelPtr<EClosure-mirror> } — captures live in the
// closure mirror's CL_CAPTURE_NAMES (closure_keys::CAPTURE_NAMES).
struct EClosureBoxView {
    ExprRef self;

private:
    const hermes::TinyObjectMap* cl_map() const noexcept {
        auto cl_av = self.mirror()->get(ek::CLOSURE.code);
        if (cl_av.is_null()) return nullptr;
        return reinterpret_cast<const hermes::TinyObjectMap*>(
            cl_av.resolve());
    }

public:
    // Block of the captured closure body (closure_keys::BLOCK = 0 within the
    // closure-map). Returns null BlockRef if the closure mirror is missing.
    BlockRef body() const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto blk_av = m->get(lir_schema::closure_keys::BLOCK.code);
        if (blk_av.is_null()) return {};
        return detail::make_sub_ref<BlockRef>(self, blk_av);
    }

    std::string_view closure_id() const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto av = m->get(lir_schema::closure_keys::NAME.code);
        if (av.is_null()) return {};
        return av.as_ptr<const hermes::ArenaString>()->view();
    }

    bool as_fn_ptr() const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::AS_FN_PTR.code);
        if (av.is_null()) return false;
        return av.as_value<uint8_t>() != 0;
    }

    bool is_move() const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::IS_MOVE.code);
        if (av.is_null()) return false;
        return av.as_value<uint8_t>() != 0;
    }

    // G167-3b: closure value is boxed → its env must be heap-allocated.
    bool escapes() const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::ESCAPES.code);
        if (av.is_null()) return false;
        return av.as_value<uint8_t>() != 0;
    }

    TypeRef ret_type(const TypePoolImpl* pool) const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto av = m->get(lir_schema::closure_keys::RET_TYPE.code);
        if (av.is_null()) return {};
        return detail::make_child_typeref(self, av, pool);
    }

    uint64_t capture_count() const noexcept {
        auto* m = cl_map();
        if (!m) return 0;
        auto names_av = m->get(
            lir_schema::closure_keys::CAPTURE_NAMES.code);
        if (names_av.is_null()) return 0;
        return names_av.as_ptr<const hermes::ObjectArray>()->size();
    }

    template <class F>
    void each_capture_name(F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(
            lir_schema::closure_keys::CAPTURE_NAMES.code);
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = names_av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>()->view());
        }
    }

    // C5-cl-08: per-capture mut-flag — true if the captured variable is
    // mutated inside the closure body. Missing key (e.g. closures with no
    // mutated captures) returns false for all indices.
    bool capture_is_mut(uint64_t i) const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::MUT_CAPTURES.code);
        if (av.is_null()) return false;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return false;
        auto el = arr->get(i);
        if (el.is_null()) return false;
        return el.as_value<uint8_t>() != 0;
    }

    // RFC-2229 phase-1: capture i's dotted FIELD PATH (`p.x.y`). Falls back to
    // the bare capture name when the schema slot is absent (a closure that only
    // reads whole roots — the common case, schema footprint unchanged).
    std::string_view capture_path(uint64_t i) const noexcept {
        std::string_view fallback;
        // Walk to the i-th name for the fallback view.
        uint64_t k = 0;
        each_capture_name([&](std::string_view nm){ if (k++ == i) fallback = nm; });
        auto* m = cl_map();
        if (!m) return fallback;
        auto av = m->get(lir_schema::closure_keys::CAPTURE_PATHS.code);
        if (av.is_null()) return fallback;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return fallback;
        auto el = arr->get(i);
        if (el.is_null()) return fallback;
        return el.as_ptr<const hermes::ArenaString>()->view();
    }

    // RFC-2229 phase-2: capture i's narrow FIELD type (path-precise capture),
    // or null when whole-root. The env-slot for a narrow capture is field-sized.
    TypeRef capture_field_type(const TypePoolImpl* pool, uint64_t i) const noexcept {
        auto* m = cl_map();
        if (!m) return TypeRef{};
        auto av = m->get(lir_schema::closure_keys::CAPTURE_FIELD_TYPES.code);
        if (av.is_null()) return TypeRef{};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return TypeRef{};
        auto el = arr->get(i);
        if (el.is_null()) return TypeRef{};
        return detail::make_child_typeref(self, el, pool);
    }

    // Iterate (name, type) pairs from CL_CAPTURE_NAMES + CL_CAPTURE_TYPES.
    template <class F>
    void each_capture(const TypePoolImpl* pool, F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(lir_schema::closure_keys::CAPTURE_NAMES.code);
        auto types_av = m->get(lir_schema::closure_keys::CAPTURE_TYPES.code);
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>()->size();
        uint64_t tn = types_av.is_null() ? 0
            : types_av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>()->get(i);
            std::string_view name;
            if (!nv.is_null())
                name = nv.as_ptr<const hermes::ArenaString>()->view();
            TypeRef t;
            if (i < tn) {
                auto tv = types_av.as_ptr<const hermes::ObjectArray>()->get(i);
                if (!tv.is_null()) t = detail::make_child_typeref(self, tv, pool);
            }
            f(name, t);
        }
    }

    // Iterate (name, type) pairs from CL_PARAM_NAMES + CL_PARAM_TYPES.
    template <class F>
    void each_param(const TypePoolImpl* pool, F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(lir_schema::closure_keys::PARAM_NAMES.code);
        auto types_av = m->get(lir_schema::closure_keys::PARAM_TYPES.code);
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>()->size();
        uint64_t tn = types_av.is_null() ? 0
            : types_av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>()->get(i);
            std::string_view name;
            if (!nv.is_null())
                name = nv.as_ptr<const hermes::ArenaString>()->view();
            TypeRef t;
            if (i < tn) {
                auto tv = types_av.as_ptr<const hermes::ObjectArray>()->get(i);
                if (!tv.is_null()) t = detail::make_child_typeref(self, tv, pool);
            }
            f(name, t);
        }
    }
};

// ── Stub views (Phase 3d): bodies still go through lexpr_of() to reach the
// underlying variant. Promoted to richer accessors as call-sites migrate.

struct EAddrOfTempView {
    ExprRef self;
    ExprRef inner() const noexcept { return self.sub_expr(ek::INNER.code); }
    bool is_mut() const noexcept { return detail::read_bool(self, ek::IS_MUT.code); }
};

struct EEnumLitView {
    ExprRef self;
    std::string_view enum_name() const noexcept { return detail::read_string(self, ek::ENUM_NAME.code); }
    std::string_view variant()   const noexcept { return detail::read_string(self, ek::VARIANT.code); }
    int64_t          disc()      const noexcept { return detail::read_i64(self, ek::DISC.code); }
};

struct ESizeOfView {
    ExprRef self;
    TypeRef elem_type(const TypePoolImpl* pool) const noexcept {
        return self.sub_type(ek::ELEM_TYPE.code, pool);
    }
};

struct EAlignOfView {
    ExprRef self;
    TypeRef elem_type(const TypePoolImpl* pool) const noexcept {
        return self.sub_type(ek::ELEM_TYPE.code, pool);
    }
};

struct EGenericRefView {
    ExprRef self;
    std::string_view name() const noexcept {
        return detail::read_string(self, ek::CALLEE.code);
    }
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::TYPE_ARGS.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) { out.emplace_back(); continue; }
            out.push_back(detail::make_child_typeref(self, el, pool));
        }
        return out;
    }
};

struct ETypeCodeOfView {
    ExprRef self;
    TypeRef elem_type(const TypePoolImpl* pool) const noexcept {
        return self.sub_type(ek::ELEM_TYPE.code, pool);
    }
};

namespace pdk = lir_schema::ptrdiff_keys;

struct EPtrArithView {
    ExprRef self;
    uint8_t op_code() const noexcept { return detail::read_u8(self, ek::PTR_ARITH_OP.code); }
    ExprRef ptr()     const noexcept { return self.sub_expr(ek::BASE_PTR.code); }
    ExprRef offset()  const noexcept { return self.sub_expr(ek::OFFSET.code); }
};

struct EPtrDiffView {
    ExprRef self;
    bool    by_byte() const noexcept { return detail::read_bool(self, pdk::BY_BYTE.code); }
    ExprRef lhs()     const noexcept { return self.sub_expr(ek::LHS.code); }
    ExprRef rhs()     const noexcept { return self.sub_expr(ek::RHS.code); }
};

struct EReflectOfView {
    ExprRef self;
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        return self.sub_type(ek::ELEM_TYPE.code, pool);
    }
};

// ── HermesVal views ──────────────────────────────────────────────────────
//
// HermesVal is the Hermes-SDN literal tree under @{...}. Each variant maps
// to a schema_type_code in lir_schema::hermes_val::Code. Mirror writers in
// lir_mirror.cpp (emit_hv) populate the keys read here.

namespace hvk = lir_schema::hv_keys;

struct HVNullView   { HermesValRef self; };

struct HVBoolView {
    HermesValRef self;
    bool value() const noexcept { return detail::read_bool(self, hvk::BOOL_VALUE.code); }
};

struct HVIntView {
    HermesValRef self;
    int64_t value() const noexcept { return detail::read_i64(self, hvk::INT_VALUE.code); }
};

struct HVFloatView {
    HermesValRef self;
    double value() const noexcept { return detail::read_f64(self, hvk::FLOAT_VALUE.code); }
};

struct HVStrView {
    HermesValRef self;
    std::string_view value() const noexcept { return detail::read_string(self, hvk::STR_VALUE.code); }
};

struct HVCaptureView {
    HermesValRef self;
    uint32_t param_index() const noexcept { return detail::read_u32(self, hvk::PARAM_INDEX.code); }
    uint32_t value_index() const noexcept { return detail::read_u32(self, hvk::VALUE_INDEX.code); }
};

struct HVTypeView {
    HermesValRef self;
    uint32_t kind() const noexcept { return detail::read_u32(self, hvk::TYPE_KIND.code); }
    uint64_t uid()  const noexcept { return detail::read_u64(self, hvk::TYPE_UID.code); }
    std::string_view name() const noexcept { return detail::read_string(self, hvk::STR_VALUE.code); }
};

// HVMap entries are stored as two parallel arrays:
//   keys[i]   — Varchar (string-keyed) or i64 (int-keyed); never mixed.
//   values[i] — RelPtr<HermesVal>.
// `key_type` is "" for string keys (ObjectMap) or "I32"/"U32"/"I64"/"U64"
// for the typed-map specialisations.
struct HVMapView {
    HermesValRef self;
    std::string_view key_type() const noexcept {
        return detail::read_string(self, hvk::TYPE_NAME.code);
    }
    bool int_keyed() const noexcept { return !key_type().empty(); }
    uint64_t size() const noexcept {
        auto av = self.mirror()->get(hvk::MAP_VALUES.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    HermesValRef value(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_VALUES.code);
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return {};
        auto el = arr->get(i);
        if (el.is_null()) return {};
        return detail::make_sub_ref<HermesValRef>(self, el);
    }
    std::string_view str_key(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_KEYS.code);
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return {};
        auto el = arr->get(i);
        if (el.is_null()) return {};
        return el.as_ptr<const hermes::ArenaString>()->view();
    }
    int64_t int_key(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_KEYS.code);
        if (av.is_null()) return 0;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return 0;
        auto el = arr->get(i);
        if (el.is_null()) return 0;
        return *el.as_ptr<const int64_t>();
    }
};

struct HVArrayView {
    HermesValRef self;
    std::string_view elem_type() const noexcept {
        return detail::read_string(self, hvk::TYPE_NAME.code);
    }
    uint64_t size() const noexcept {
        auto av = self.mirror()->get(hvk::ELEMS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    HermesValRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::ELEMS.code);
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        if (i >= arr->size()) return {};
        auto el = arr->get(i);
        if (el.is_null()) return {};
        return detail::make_sub_ref<HermesValRef>(self, el);
    }
};

namespace hl = lir_schema::hermes_lit_keys;

struct EHermesLitView {
    ExprRef self;
    HermesValRef root() const noexcept {
        auto av = self.mirror()->get(hl::ROOT.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<HermesValRef>(self, av);
    }
    bool     has_captures()        const noexcept { return detail::read_bool(self, hl::HAS_CAPTURES.code); }
    uint32_t capture_param_count() const noexcept { return detail::read_u32(self, hl::CAPTURE_PARAM_COUNT.code); }
    std::string_view static_blob() const noexcept { return detail::read_string(self, hl::STATIC_BLOB.code); }
    template <class F> void each_capture_expr(F&& f) const noexcept {
        detail::for_each_expr(self, hl::CAPTURE_EXPRS.code, std::forward<F>(f));
    }
    template <class F> void each_capture_type(const TypePoolImpl* pool, F&& f) const noexcept {
        auto av = self.mirror()->get(hl::CAPTURE_TYPES.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) { f(TypeRef{}); continue; }
            f(detail::make_child_typeref(self, el, pool));
        }
    }
};

struct EPackExpandView {
    ExprRef self;
    std::string_view var_name() const noexcept { return detail::read_string(self, ek::NAME.code); }
};

// ── Pattern leaf exemplar ────────────────────────────────────────────────

struct PatBoolView {
    PatRef self;
    bool value() const noexcept { return detail::read_bool(self, pk::BOOL_VALUE.code); }
};

// PatWild { name: Varchar (optional) }
struct PatWildView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
    uint32_t bind_slot() const noexcept {  // Phase-1 (0xFFFFFFFF = none)
        auto v = detail::read_i64_opt(self, pk::BIND_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

// PatVariantData { enum_name, variant, disc, bindings: Array<Varchar>, binding_types }
struct PatVariantDataView {
    PatRef self;
    int64_t          disc()      const noexcept { return detail::read_i64(self, pk::DISC.code); }
    std::string_view enum_name() const noexcept { return detail::read_string(self, pk::ENUM_NAME.code); }
    std::string_view variant()   const noexcept { return detail::read_string(self, pk::VARIANT.code); }
    template <class F>
    void each_binding(F&& f) const noexcept {
        auto av = self.mirror()->get(pk::BINDINGS.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>()->view());
        }
    }
    template <class F>
    void each_binding_type(const TypePoolImpl* pool, F&& f) const noexcept {
        auto av = self.mirror()->get(pk::BINDING_TYPES.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) { f(TypeRef{}); continue; }
            f(detail::make_child_typeref(self, el, pool));
        }
    }
    // Phase-1: dense slots parallel to each_binding (0xFFFFFFFF = none / `_`).
    std::vector<uint32_t> bind_slots() const noexcept {
        std::vector<uint32_t> out;
        auto av = self.mirror()->get(pk::BIND_SLOTS.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        uint64_t n = arr->size();
        out.reserve(n);
        for (uint64_t i = 0; i < n; ++i) {
            auto el = arr->get(i);
            out.push_back(el.is_null() ? 0xFFFFFFFFu : el.as_value<uint32_t>());
        }
        return out;
    }
};

// PatVariant { enum_name, variant, disc }
struct PatVariantView {
    PatRef self;
    int64_t          disc()      const noexcept { return detail::read_i64(self, pk::DISC.code); }
    std::string_view enum_name() const noexcept { return detail::read_string(self, pk::ENUM_NAME.code); }
    std::string_view variant()   const noexcept { return detail::read_string(self, pk::VARIANT.code); }
};

// PatInt { value: i64 }
struct PatIntView {
    PatRef self;
    int64_t value() const noexcept { return detail::read_i64(self, pk::INT_VALUE.code); }
};

// PatOr { alts: Array<RelPtr<Pattern>> }
struct PatOrView {
    PatRef self;
    template <class F>
    void each_alt(F&& f) const noexcept {
        auto av = self.mirror()->get(pk::SUBS.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) continue;
            f(detail::make_sub_ref<PatRef>(self, el));
        }
    }
};

namespace detail {

// Iterate a key-stored Array<RelPtr<Pattern>> on a PatRef.
template <class F>
inline void for_each_pat(const PatRef& r, uint8_t key, F&& f) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        if (el.is_null()) continue;
        f(detail::make_sub_ref<PatRef>(r, el));
    }
}

// Iterate a key-stored Array<Varchar> on a PatRef.
template <class F>
inline void for_each_string(const PatRef& r, uint8_t key, F&& f) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        if (el.is_null()) { f(std::string_view{}); continue; }
        f(el.as_ptr<const hermes::ArenaString>()->view());
    }
}

// Iterate a key-stored Array<RelPtr<LogosType>> on a PatRef.
template <class F>
inline void for_each_type(const PatRef& r, uint8_t key,
                          const TypePoolImpl* pool, F&& f) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        if (el.is_null()) { f(TypeRef{}); continue; }
        f(detail::make_child_typeref(r, el, pool));
    }
}

// Read a single RelPtr<Pattern> stored under SUB / etc. as a 0-or-1 array.
inline PatRef first_pat(const PatRef& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return {};
    auto* arr = av.as_ptr<const hermes::ObjectArray>();
    if (arr->size() == 0) return {};
    auto el = arr->get(0);
    if (el.is_null()) return {};
    return detail::make_sub_ref<PatRef>(r, el);
}

inline TypeRef pat_type(const PatRef& r, uint8_t key,
                        const TypePoolImpl* pool) noexcept {
    auto av = r.mirror()->get(key);
    if (av.is_null()) return TypeRef{};
    return detail::make_child_typeref(r, av, pool);
}

} // namespace detail

// PatTuple { bindings, binding_types, subs }
struct PatTupleView {
    PatRef self;
    template <class F> void each_binding(F&& f) const noexcept {
        detail::for_each_string(self, pk::BINDINGS.code, std::forward<F>(f));
    }
    template <class F> void each_binding_type(const TypePoolImpl* pool, F&& f) const noexcept {
        detail::for_each_type(self, pk::BINDING_TYPES.code, pool, std::forward<F>(f));
    }
    template <class F> void each_sub(F&& f) const noexcept {
        detail::for_each_pat(self, pk::SUBS.code, std::forward<F>(f));
    }
    uint64_t sub_count() const noexcept {
        auto av = self.mirror()->get(pk::SUBS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    uint64_t binding_count() const noexcept {
        auto av = self.mirror()->get(pk::BINDINGS.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    // Phase-1: dense slots parallel to each_binding (0xFFFFFFFF = none / `_`).
    std::vector<uint32_t> bind_slots() const noexcept {
        std::vector<uint32_t> out;
        auto av = self.mirror()->get(pk::BIND_SLOTS.code);
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        uint64_t n = arr->size();
        out.reserve(n);
        for (uint64_t i = 0; i < n; ++i) {
            auto el = arr->get(i);
            out.push_back(el.is_null() ? 0xFFFFFFFFu : el.as_value<uint32_t>());
        }
        return out;
    }
};

// PatRange { lo: i64, hi: i64 }
struct PatRangeView {
    PatRef self;
    int64_t lo() const noexcept { return detail::read_i64(self, pk::LO.code); }
    int64_t hi() const noexcept { return detail::read_i64(self, pk::HI.code); }
};

// PatFieldBinding mirror { field_name, sub: 0|1 pattern }
struct PatFieldBindingView {
    PatRef self;
    std::string_view field_name() const noexcept {
        return detail::read_string(self, pk::FIELD_NAME.code);
    }
    PatRef sub() const noexcept { return detail::first_pat(self, pk::SUB.code); }
    uint32_t bind_slot() const noexcept {  // Phase-1 (shorthand only)
        auto v = detail::read_i64_opt(self, pk::BIND_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

// PatStruct { struct_name, fields: Array<PatFieldBinding>, has_rest }
struct PatStructView {
    PatRef self;
    std::string_view struct_name() const noexcept {
        return detail::read_string(self, pk::STRUCT_NAME.code);
    }
    bool has_rest() const noexcept {
        return detail::read_bool(self, pk::HAS_REST.code);
    }
    template <class F> void each_field(F&& f) const noexcept {
        auto av = self.mirror()->get(pk::FIELDS.code);
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(PatFieldBindingView{detail::make_sub_ref<PatRef>(self, el)});
        }
    }
};

// PatSlice { prefix, rest: 0|1, suffix }
struct PatSliceView {
    PatRef self;
    template <class F> void each_prefix(F&& f) const noexcept {
        detail::for_each_pat(self, pk::PREFIX.code, std::forward<F>(f));
    }
    template <class F> void each_rest(F&& f) const noexcept {
        detail::for_each_pat(self, pk::REST.code, std::forward<F>(f));
    }
    template <class F> void each_suffix(F&& f) const noexcept {
        detail::for_each_pat(self, pk::SUFFIX.code, std::forward<F>(f));
    }
    uint64_t prefix_count() const noexcept {
        auto av = self.mirror()->get(pk::PREFIX.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    uint64_t suffix_count() const noexcept {
        auto av = self.mirror()->get(pk::SUFFIX.code);
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>()->size();
    }
    PatRef rest() const noexcept { return detail::first_pat(self, pk::REST.code); }
};

// PatAt { name, sub: 0|1 pattern, type }
struct PatAtView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
    PatRef           sub()  const noexcept { return detail::first_pat(self, pk::SUB.code); }
    TypeRef          type(const TypePoolImpl* pool) const noexcept {
        return detail::pat_type(self, pk::TYPE.code, pool);
    }
    uint32_t bind_slot() const noexcept {  // Phase-1
        auto v = detail::read_i64_opt(self, pk::BIND_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

// PatRefBind { name, is_mut, bind_type }
struct PatRefBindView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
    bool             is_mut() const noexcept { return detail::read_bool(self, pk::IS_MUT.code); }
    TypeRef          bind_type(const TypePoolImpl* pool) const noexcept {
        return detail::pat_type(self, pk::BIND_TYPE.code, pool);
    }
    uint32_t bind_slot() const noexcept {  // Phase-1
        auto v = detail::read_i64_opt(self, pk::BIND_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

// PatRefPat { inner: 1 pattern, is_mut }
struct PatRefPatView {
    PatRef self;
    bool             is_mut() const noexcept { return detail::read_bool(self, pk::IS_MUT.code); }
    PatRef           inner()  const noexcept { return detail::first_pat(self, pk::INNER.code); }
};

// ── LStmt variant views ──────────────────────────────────────────────────

namespace sk = lir_schema::stmt_keys;
namespace sc = lir_schema::stmt_common;

namespace detail {

inline ExprRef stmt_sub_expr(const StmtRef& s, uint8_t key) noexcept {
    return s.sub_expr(key);
}

inline BlockRef stmt_sub_block(const StmtRef& s, uint8_t key) noexcept {
    auto av = s.mirror()->get(key);
    if (av.is_null()) return {};
    return detail::make_sub_ref<BlockRef>(s, av);
}

inline std::string_view stmt_str(const StmtRef& s, uint8_t key) noexcept {
    return read_string(s, key);
}

inline TypeRef stmt_type(const StmtRef& s, uint8_t key, const TypePoolImpl* pool) noexcept {
    auto av = s.mirror()->get(key);
    if (av.is_null()) return TypeRef{};
    return detail::make_child_typeref(s, av, pool);
}

// Iterate a key-stored Array<Varchar> on a StmtRef.
template <class F>
inline void for_each_stmt_string(const StmtRef& s, uint8_t key, F&& f) noexcept {
    auto av = s.mirror()->get(key);
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
        if (el.is_null()) { f(std::string_view{}); continue; }
        f(el.as_ptr<const hermes::ArenaString>()->view());
    }
}

inline uint64_t stmt_array_size(const StmtRef& s, uint8_t key) noexcept {
    auto av = s.mirror()->get(key);
    if (av.is_null()) return 0;
    return av.as_ptr<const hermes::ObjectArray>()->size();
}

} // namespace detail

struct SLetView {
    StmtRef self;
    std::string_view name() const noexcept   { return detail::stmt_str(self, sk::NAME.code); }
    ExprRef          value() const noexcept  { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    bool             is_mut() const noexcept { return detail::read_bool(self, sk::IS_MUT.code); }
    TypeRef          type(const TypePoolImpl* pool) const noexcept {
        return detail::stmt_type(self, sk::TYPE.code, pool);
    }
    // Phase-1: dense variable slot of this binding (0xFFFFFFFF = no slot).
    uint32_t var_slot() const noexcept {
        auto v = detail::read_i64_opt(self, sk::VAR_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

struct SAssignView {
    StmtRef self;
    std::string_view name() const noexcept  { return detail::stmt_str(self, sk::NAME.code); }
    ExprRef          value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    // B8: drop the LHS's old value before storing the new one.
    bool             drop_old() const noexcept { return detail::read_bool(self, sk::DROP_OLD.code); }
};

struct SReturnView {
    StmtRef self;
    ExprRef value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SExprStmtView {
    StmtRef self;
    ExprRef expr() const noexcept { return detail::stmt_sub_expr(self, sk::EXPR.code); }
};

struct SFieldWriteView {
    StmtRef self;
    std::string_view receiver() const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    std::string_view field() const noexcept    { return detail::stmt_str(self, sk::FIELD.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SIndexWriteView {
    StmtRef self;
    std::string_view arr() const noexcept   { return detail::stmt_str(self, sk::NAME.code); }
    ExprRef          index() const noexcept { return detail::stmt_sub_expr(self, sk::INDEX.code); }
    ExprRef          value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SFieldIndexWriteView {
    StmtRef self;
    std::string_view receiver() const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    std::string_view field() const noexcept    { return detail::stmt_str(self, sk::FIELD.code); }
    ExprRef          index() const noexcept    { return detail::stmt_sub_expr(self, sk::INDEX.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SChainFieldWriteView {
    StmtRef self;
    std::string_view receiver()  const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    std::string_view mid_field() const noexcept { return detail::stmt_str(self, sk::MID_FIELD.code); }
    std::string_view field()     const noexcept { return detail::stmt_str(self, sk::FIELD.code); }
    ExprRef          value()     const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    uint64_t         extra_count() const noexcept { return detail::stmt_array_size(self, sk::EXTRA_MIDS.code); }
    template <class F> void each_extra(F&& f) const noexcept {
        detail::for_each_stmt_string(self, sk::EXTRA_MIDS.code, std::forward<F>(f));
    }
};

struct SDerefFieldWriteView {
    StmtRef self;
    std::string_view receiver()  const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    std::string_view type_name() const noexcept { return detail::stmt_str(self, sk::TYPE_NAME.code); }
    std::string_view field()     const noexcept { return detail::stmt_str(self, sk::FIELD.code); }
    ExprRef          value()     const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SDerefWriteView {
    StmtRef self;
    ExprRef ptr() const noexcept   { return detail::stmt_sub_expr(self, sk::PTR.code); }
    ExprRef value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    // T1.5: drop the OLD value at the place before storing (field-level
    // drop-before-replace, mirrors SAssign.drop_old for owned field places).
    bool drop_old() const noexcept { return detail::read_bool(self, sk::DROP_OLD.code); }
};

struct STupleWriteView {
    StmtRef self;
    std::string_view receiver() const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    uint32_t         index() const noexcept    { return detail::read_u32(self, sk::TUPLE_INDEX_VAL.code); }
    TypeRef          recv_type(const TypePoolImpl* pool) const noexcept {
        return detail::stmt_type(self, sk::RECV_TYPE.code, pool);
    }
};

struct SIfView {
    StmtRef self;
    ExprRef  cond() const noexcept       { return detail::stmt_sub_expr(self, sk::COND.code); }
    BlockRef then_block() const noexcept { return detail::stmt_sub_block(self, sk::THEN_BLOCK.code); }
    BlockRef else_block() const noexcept { return detail::stmt_sub_block(self, sk::ELSE_BLOCK.code); }
};

struct SWhileView {
    StmtRef self;
    ExprRef  cond() const noexcept  { return detail::stmt_sub_expr(self, sk::COND.code); }
    BlockRef body() const noexcept  { return detail::stmt_sub_block(self, sk::BODY.code); }
    std::string_view label() const noexcept { return detail::stmt_str(self, sk::LABEL.code); }
};

struct SForView {
    StmtRef self;
    std::string_view var() const noexcept   { return detail::stmt_str(self, sk::VAR.code); }
    ExprRef          lo() const noexcept    { return detail::stmt_sub_expr(self, sk::LO.code); }
    ExprRef          hi() const noexcept    { return detail::stmt_sub_expr(self, sk::HI.code); }
    BlockRef         body() const noexcept  { return detail::stmt_sub_block(self, sk::BODY.code); }
    bool             inclusive() const noexcept { return detail::read_bool(self, sk::INCLUSIVE.code); }
    std::string_view label() const noexcept { return detail::stmt_str(self, sk::LABEL.code); }
    uint32_t var_slot() const noexcept {  // Phase-1
        auto v = detail::read_i64_opt(self, sk::VAR_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

struct SLoopView {
    StmtRef self;
    BlockRef         body() const noexcept       { return detail::stmt_sub_block(self, sk::BODY.code); }
    std::string_view label() const noexcept      { return detail::stmt_str(self, sk::LABEL.code); }
    std::string_view break_slot() const noexcept { return detail::stmt_str(self, sk::BREAK_SLOT.code); }
    TypeRef          result_type(const TypePoolImpl* pool) const noexcept {
        return detail::stmt_type(self, sk::RESULT_TYPE.code, pool);
    }
};

struct SBlockView {
    StmtRef self;
    BlockRef body() const noexcept { return detail::stmt_sub_block(self, sk::BODY.code); }
};

struct SForEachView {
    StmtRef self;
    std::string_view var() const noexcept  { return detail::stmt_str(self, sk::VAR.code); }
    ExprRef          iter() const noexcept { return detail::stmt_sub_expr(self, sk::ITER.code); }
    BlockRef         body() const noexcept { return detail::stmt_sub_block(self, sk::BODY.code); }
    bool             is_slice() const noexcept { return detail::read_bool(self, sk::IS_SLICE.code); }
    int64_t          arr_size() const noexcept { return detail::read_i64(self, sk::ARR_SIZE.code); }
    TypeRef          elem_type(const TypePoolImpl* pool) const noexcept {
        return detail::stmt_type(self, sk::ELEM_TYPE.code, pool);
    }
    uint32_t var_slot() const noexcept {  // Phase-1
        auto v = detail::read_i64_opt(self, sk::VAR_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};

struct SLetElseView {
    StmtRef self;
    ExprRef  scrut() const noexcept       { return detail::stmt_sub_expr(self, sk::SCRUT.code); }
    BlockRef else_block() const noexcept  { return detail::stmt_sub_block(self, sk::ELSE_DIVERGE.code); }
    PatRef   pat() const noexcept {
        auto av = self.mirror()->get(sk::PAT.code);
        if (av.is_null()) return {};
        return detail::make_sub_ref<PatRef>(self, av);
    }
    // G161-3: refutable-inner guard exprs (`__refut_N == value`).
    template <class F> void each_guard(F&& f) const noexcept {
        auto av = self.mirror()->get(sk::LET_ELSE_GUARDS.code);
        if (av.is_null()) return;
        auto* arr = av.template as_ptr<const hermes::ObjectArray>();
        if (!arr) return;
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i);
            if (el.is_null()) continue;
            f(detail::make_sub_ref<ExprRef>(self, el));
        }
    }
};

struct SBreakView {
    StmtRef self;
    // Optional break-with-value expression (null ExprRef when absent).
    ExprRef          value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
    std::string_view label() const noexcept { return detail::stmt_str(self, sk::LABEL.code); }
};

struct SContinueView {
    StmtRef self;
    std::string_view label() const noexcept { return detail::stmt_str(self, sk::LABEL.code); }
};
struct SDropView     {
    StmtRef self;
    std::string_view var_name() const noexcept { return detail::stmt_str(self, sk::NAME.code); }
    std::string_view drop_fn() const noexcept  { return detail::stmt_str(self, sk::DROP_FN.code); }
    bool             drop_fields() const noexcept { return detail::read_bool(self, sk::DROP_FIELDS.code); }
    TypeRef          type(const TypePoolImpl* pool) const noexcept {
        return detail::stmt_type(self, sk::TYPE.code, pool);
    }
    uint64_t         moved_fields_count() const noexcept { return detail::stmt_array_size(self, sk::MOVED_FIELDS.code); }
    template <class F> void each_moved_field(F&& f) const noexcept {
        detail::for_each_stmt_string(self, sk::MOVED_FIELDS.code, std::forward<F>(f));
    }
};

struct SMatchView {
    StmtRef self;
    ExprRef scrut() const noexcept { return detail::stmt_sub_expr(self, sk::SCRUT.code); }

    template <class F>
    void each_arm(F&& f) const noexcept {
        // Re-fetch the array pointer on each iteration: `f` may recurse
        // into substitution/cloning which allocates and triggers
        // GrowableSingleChunk relocation, moving the arena's head buffer.
        // A cached `arr` pointer would dangle. Mirror the pattern used by
        // detail::each_block_stmt / each_call_arg above.
        auto av = self.mirror()->get(sk::ARMS.code);
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>()->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>()->get(i);
            if (el.is_null()) continue;
            f(detail::make_sub_ref<EMatchArmRef>(self, el));
        }
    }
};

inline uint32_t stmt_line(const StmtRef& s) noexcept {
    return detail::read_u32(s, sc::LINE.code);
}

// Reborrow / pointer-identity shape: `AddrOfTemp(Deref(VarRef r))`. Inserted
// by sema's `try_implicit_reborrow_mut` at coercion sites that auto-reborrow
// a `&mut T` PLACE (Rust auto-reborrow). Multiple subsystems pattern-match
// it: borrow_check recognises the reborrow to register a borrow on `r`,
// mlir-gen's `gen_dyn_dispatch` unwraps the shape so vtable dispatch reads
// the underlying VarRef, and the sema reborrow-insert helper refuses to
// re-wrap an already-wrapped expression. Single recogniser keeps the shape
// check in one place.
//
// Returns true iff `e` is the reborrow shape; if `out_varref` is non-null,
// it receives the inner `VarRef` (so the caller can read .name() / .type()).
// Pattern irrefutability — does this pattern ALWAYS match any value of its
// scrutinee shape? Wildcards/bindings always do; structural patterns are
// irrefutable iff every sub is. Refutable shapes (literal, range, variant,
// length-constrained slice) always return false.
//
// Single foundation used by every site that needs to decide "does this
// pattern require a runtime test?" — pre-foundation we had three drifting
// lambdas: `mlir_gen_stmt.cpp::is_irrefutable` (had Slice + Or arms),
// `mlir_gen_expr.cpp::pat_irref` (missing Slice + Or → false negative),
// and an ad-hoc shape-allowlist at `sema_stmt.cpp:990-1003` for let-
// destruct (separate concern: per-shape acceptance, not full predicate).
// Logos-core item 4.1.
inline bool is_irrefutable_pattern(PatRef p) noexcept {
    using PC = lir_schema::pat::Code;
    if (!p) return false;
    switch (p.kind()) {
        case PC::Wild:
        case PC::RefBind:
            return true;
        case PC::RefPat: {
            auto in = PatRefPatView{p}.inner();
            return !in || is_irrefutable_pattern(in);
        }
        case PC::At: {
            auto sub = PatAtView{p}.sub();
            return !sub || is_irrefutable_pattern(sub);
        }
        case PC::Tuple: {
            PatTupleView tv{p};
            if (tv.sub_count() == 0) return true;
            bool all = true;
            tv.each_sub([&](PatRef sp) {
                if (all && !is_irrefutable_pattern(sp)) all = false;
            });
            return all;
        }
        case PC::Struct: {
            bool all = true;
            PatStructView{p}.each_field([&](PatFieldBindingView fb) {
                auto sub = fb.sub();
                if (all && sub && !is_irrefutable_pattern(sub)) all = false;
            });
            return all;
        }
        case PC::Slice: {
            // A slice pat with any fixed prefix/suffix or rest-only-form is
            // refutable: fixed elements impose a length constraint; the
            // pure `[..]` / `[xs @ ..]` form matches every length, so it
            // is irrefutable iff every rest sub is.
            PatSliceView sv{p};
            if (sv.prefix_count() != 0 || sv.suffix_count() != 0 || !sv.rest())
                return false;
            bool all = true;
            sv.each_rest([&](PatRef sp) {
                if (all && !is_irrefutable_pattern(sp)) all = false;
            });
            return all;
        }
        case PC::Or: {
            // Or-pattern is irrefutable iff EVERY alternative is. Empty
            // (defensive) or-pat counts as irrefutable (the wild fallback
            // never fires; same convention as the stmt-side handler).
            bool any_alts = false, all = true;
            PatOrView{p}.each_alt([&](PatRef alt) {
                any_alts = true;
                if (all && !is_irrefutable_pattern(alt)) all = false;
            });
            return !any_alts || all;
        }
        default:
            return false;
    }
}

inline bool is_reborrow_shape(ExprRef e, ExprRef* out_varref = nullptr) noexcept {
    if (!e || e.kind() != lir_schema::expr::Code::AddrOfTemp) return false;
    auto inner = EAddrOfTempView{e}.inner();
    if (!inner || inner.kind() != lir_schema::expr::Code::Deref) return false;
    auto op = EDerefView{inner}.operand();
    if (!op || op.kind() != lir_schema::expr::Code::VarRef) return false;
    if (out_varref) *out_varref = op;
    return true;
}

// ── Stage E: handle to a Hermes ObjectMap held as an LProgram member ─────────
// Replaces a C++ std::unordered_map<string,…>. Like DeclRef it stores the arena
// + the stable header address (ObjectMap::grow re-points only the internal
// buffer, never the 24-byte header). Read-only here (get/has/for_each/size);
// inserts route through lir_mirror_map_put_* (which create the map on first put
// and grow in the program's arena). Copy/move of the handle is trivial; the map
// lives in the type_pool arena (stable across mono's pool move).
struct ObjectMapRef {
    const hermes::Arena* arena_ = nullptr;
    const uint8_t*       addr_  = nullptr;   // ObjectMap header (stable)
    bool valid() const noexcept { return addr_ != nullptr; }
    hermes::ObjectMap* map() const noexcept {
        return reinterpret_cast<hermes::ObjectMap*>(const_cast<uint8_t*>(addr_));
    }
    hermes::AnyVal get(std::string_view k) const noexcept {
        return valid() ? map()->get(k) : hermes::AnyVal{};
    }
    std::string_view get_str(std::string_view k) const noexcept {
        auto av = get(k);
        return av.is_null() ? std::string_view{}
                            : av.as_ptr<const hermes::ArenaString>()->view();
    }
    bool     has(std::string_view k) const noexcept { return valid() && map()->has(k); }
    uint64_t size()  const noexcept { return valid() ? map()->size() : 0; }
    bool     empty() const noexcept { return size() == 0; }
    // F is called as f(std::string_view key, hermes::AnyVal val).
    template <class F> void for_each(F&& f) const { if (valid()) map()->for_each(std::forward<F>(f)); }
};

} // namespace logos::compiler::lir_view
