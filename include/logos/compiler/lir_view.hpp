// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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
#include <logos/hermes/arena.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/schema_codes.hpp>
#include <logos/hermes/tiny_object_map.hpp>

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
    hermes::arena_offset_t off_{};

    RefBase() = default;
    RefBase(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : arena_(a), off_(o) {}

public:
    constexpr explicit operator bool() const noexcept {
        return off_ != hermes::NULL_OFFSET;
    }
    hermes::arena_offset_t offset() const noexcept { return off_; }
    const hermes::Arena*   arena()  const noexcept { return arena_; }

    uint8_t* base() const noexcept {
        return arena_ ? const_cast<uint8_t*>(arena_->head().data()) : nullptr;
    }
    const hermes::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const hermes::TinyObjectMap*>(base() + off_.value());
    }
    uint64_t schema_type_code() const noexcept {
        return mirror()->schema_type_code();
    }

    friend constexpr bool operator==(const RefBase& a, const RefBase& b) noexcept {
        return a.off_ == b.off_;
    }
};

// Read primitives shared by every view struct. Each takes a ref and a
// sparse-key code; missing keys return defaults so views can stay terse.

inline std::string_view read_string(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return {};
    return av.as_ptr<const hermes::ArenaString>(r.base())->view();
}

inline int64_t read_i64(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return 0;
    return *av.as_ptr<const int64_t>(r.base());
}

inline double read_f64(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return 0.0;
    return *av.as_ptr<const double>(r.base());
}

inline std::optional<int64_t> read_i64_opt(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return std::nullopt;
    return *av.as_ptr<const int64_t>(r.base());
}

inline uint32_t read_u32(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return 0;
    return av.is_value() ? av.as_value<uint32_t>() : *av.as_ptr<const uint32_t>(r.base());
}

inline bool read_bool(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return false;
    return av.as_value<uint8_t>() != 0;
}

inline uint8_t read_u8(const RefBase& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return 0;
    return av.as_value<uint8_t>();
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
    ExprRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    lir_schema::expr::Code kind() const noexcept {
        return lir_schema::expr::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }

    // TypeRef of the expression. The mirror stores the type's arena offset
    // under expr_common::TYPE; wrap it with the caller's TypePoolImpl* so
    // pool-dependent accessors (e.g. trait resolution) keep working.
    TypeRef type(const TypePoolImpl* pool) const noexcept {
        auto av = mirror()->get(lir_schema::expr_common::TYPE.code, base());
        if (av.is_null()) return TypeRef{};
        return TypeRef(arena(), av.to_offset(), pool);
    }

    // Helper: reach a sub-expression via a sparse key (used by view structs).
    ExprRef sub_expr(uint8_t key) const noexcept;

    // Helper: reach a sub-type (RelPtr<LogosType>) via a sparse key.
    TypeRef sub_type(uint8_t key, const TypePoolImpl* pool) const noexcept {
        auto av = mirror()->get(key, base());
        if (av.is_null()) return TypeRef{};
        return TypeRef(arena(), av.to_offset(), pool);
    }
};

// ── StmtRef ───────────────────────────────────────────────────────────────

class StmtRef : public detail::RefBase {
public:
    StmtRef() = default;
    StmtRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    lir_schema::stmt::Code kind() const noexcept {
        return lir_schema::stmt::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }

    StmtRef sub_stmt(uint8_t key) const noexcept;
    ExprRef sub_expr(uint8_t key) const noexcept;
};

// ── PatRef ────────────────────────────────────────────────────────────────

class PatRef : public detail::RefBase {
public:
    PatRef() = default;
    PatRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    lir_schema::pat::Code kind() const noexcept {
        return lir_schema::pat::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }
};

// ── BlockRef / HermesValRef (opaque for now) ─────────────────────────────

class BlockRef : public detail::RefBase {
public:
    BlockRef() = default;
    BlockRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    // Block stmts are stored under stmt_keys::ARMS (key 24) — a single key
    // shared with SMatch.arms because both are Array<RelPtr<sub-node>>.
    template <class F>
    void each_stmt(F&& f) const noexcept;
};

class HermesValRef : public detail::RefBase {
public:
    HermesValRef() = default;
    HermesValRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    lir_schema::hermes_val::Code kind() const noexcept {
        return lir_schema::hermes_val::Code(
            int32_t(hermes::schema::variant_of(schema_type_code())));
    }
};

// ── Inline accessors that need the above forward decls ───────────────────

inline ExprRef ExprRef::sub_expr(uint8_t key) const noexcept {
    auto av = mirror()->get(key, base());
    if (av.is_null()) return {};
    return ExprRef(arena_, av.to_offset());
}

inline ExprRef StmtRef::sub_expr(uint8_t key) const noexcept {
    auto av = mirror()->get(key, base());
    if (av.is_null()) return {};
    return ExprRef(arena_, av.to_offset());
}

inline StmtRef StmtRef::sub_stmt(uint8_t key) const noexcept {
    auto av = mirror()->get(key, base());
    if (av.is_null()) return {};
    return StmtRef(arena_, av.to_offset());
}

// Iterate stmts inside a block. The mirror stores them at stmt_keys::ARMS (24),
// reusing the same key for SMatch.arms — see lir_mirror.cpp:emit_block.
template <class F>
inline void BlockRef::each_stmt(F&& f) const noexcept {
    auto av = mirror()->get(/*stmt_keys::ARMS*/ 24, base());
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>(base())->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>(base())->get(i, base());
        if (el.is_null()) continue;
        f(StmtRef(arena_, el.to_offset()));
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
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>(r.base())->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>(r.base())->get(i, r.base());
        if (el.is_null()) { f(ExprRef{}); continue; }
        f(ExprRef(r.arena(), el.to_offset()));
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
    EMatchArmRef(const hermes::Arena* a, hermes::arena_offset_t o) noexcept
        : RefBase(a, o) {}

    PatRef  pat() const noexcept {
        auto av = mirror()->get(ak::PAT.code, base());
        if (av.is_null()) return {};
        return PatRef(arena(), av.to_offset());
    }
    ExprRef value() const noexcept {
        auto av = mirror()->get(ak::VALUE.code, base());
        if (av.is_null()) return {};
        return ExprRef(arena(), av.to_offset());
    }
    ExprRef guard() const noexcept {
        auto av = mirror()->get(ak::GUARD.code, base());
        if (av.is_null()) return {};
        return ExprRef(arena(), av.to_offset());
    }
    BlockRef body() const noexcept {
        auto av = mirror()->get(ak::BODY.code, base());
        if (av.is_null()) return {};
        return BlockRef(arena(), av.to_offset());
    }
};

// ── LExpr variant views ──────────────────────────────────────────────────

// EVarRef { name: Varchar }
struct EVarRefView {
    ExprRef self;
    std::string_view name() const noexcept { return detail::read_string(self, ek::NAME.code); }
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
        auto av = self.mirror()->get(ek::BLOCK.code, self.base());
        if (av.is_null()) return {};
        return BlockRef(self.arena(), av.to_offset());
    }
};

// EMatchExpr { scrut: LExpr, arms: Array<EMatchArm> }
struct EMatchExprView {
    ExprRef self;
    ExprRef scrut() const noexcept { return self.sub_expr(ek::SCRUT.code); }

    // Iterate arms. F is called as f(EMatchArmRef) for each arm.
    template <class F>
    void each_arm(F&& f) const noexcept {
        auto av = self.mirror()->get(ek::ARMS.code, self.base());
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) continue;
            f(EMatchArmRef(self.arena(), el.to_offset()));
        }
    }
};

// ── Leaf-shape exemplars (kept for reference) ────────────────────────────

struct ELitIntView {
    ExprRef self;
    int64_t value() const noexcept { return detail::read_i64(self, ek::LIT_I64.code); }
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
        auto av = self.mirror()->get(ek::TYPE_ARGS.code, self.base());
        if (av.is_null()) return false;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        return arr->size() > 0;
    }

    // Read TYPE_ARGS into a TypeRef vector. Pool is used so the returned
    // TypeRefs carry the caller's TypePoolImpl* (needed for accessors that
    // touch trait/impl resolution).
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::TYPE_ARGS.code, self.base());
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) { out.emplace_back(); continue; }
            out.emplace_back(self.arena(), el.to_offset(), pool);
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
        auto av = self.mirror()->get(ek::VTABLE_INDEX.code, self.base());
        if (av.is_null()) return -1;
        return int32_t(detail::read_u32(self, ek::VTABLE_INDEX.code));
    }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
    }
    std::vector<TypeRef> type_args(const TypePoolImpl* pool) const noexcept {
        std::vector<TypeRef> out;
        auto av = self.mirror()->get(ek::TYPE_ARGS.code, self.base());
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) { out.emplace_back(); continue; }
            out.emplace_back(self.arena(), el.to_offset(), pool);
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
        auto av = self.mirror()->get(ek::ARG_TYPES.code, self.base());
        if (av.is_null()) return out;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        out.reserve(arr->size());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) { out.emplace_back(); continue; }
            out.emplace_back(self.arena(), el.to_offset(), pool);
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
        auto names_av  = self.mirror()->get(ek::FIELD_NAMES.code,  self.base());
        auto values_av = self.mirror()->get(ek::FIELD_VALUES.code, self.base());
        if (names_av.is_null() || values_av.is_null()) return;
        uint64_t n = std::min(
            names_av.as_ptr<const hermes::ObjectArray>(self.base())->size(),
            values_av.as_ptr<const hermes::ObjectArray>(self.base())->size());
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            auto vv = values_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            std::string_view fname;
            if (!nv.is_null())
                fname = nv.as_ptr<const hermes::ArenaString>(self.base())->view();
            ExprRef value;
            if (!vv.is_null()) value = ExprRef(self.arena(), vv.to_offset());
            f(fname, value);
        }
    }
};

struct ENewView {
    ExprRef self;
    std::string_view class_name() const noexcept { return detail::read_string(self, ek::CLASS_NAME.code); }
    template <class F> void each_field_value(F&& f) const noexcept {
        detail::for_each_field_value(self, std::forward<F>(f));
    }
    // Iterate (name, value) pairs from parallel FIELD_NAMES / FIELD_VALUES arrays.
    // F is called as f(std::string_view name, ExprRef value).
    template <class F> void each_field(F&& f) const noexcept {
        auto names_av  = self.mirror()->get(ek::FIELD_NAMES.code,  self.base());
        auto values_av = self.mirror()->get(ek::FIELD_VALUES.code, self.base());
        if (names_av.is_null() || values_av.is_null()) return;
        uint64_t n = std::min(
            names_av.as_ptr<const hermes::ObjectArray>(self.base())->size(),
            values_av.as_ptr<const hermes::ObjectArray>(self.base())->size());
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            auto vv = values_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            std::string_view name;
            if (!nv.is_null())
                name = nv.as_ptr<const hermes::ArenaString>(self.base())->view();
            ExprRef value;
            if (!vv.is_null()) value = ExprRef(self.arena(), vv.to_offset());
            f(name, value);
        }
    }
};

struct EArrLitView {
    ExprRef self;
    template <class F> void each_elem(F&& f) const noexcept {
        detail::for_each_elem(self, std::forward<F>(f));
    }
    uint64_t count() const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    ExprRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code, self.base());
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return {};
        auto el = arr->get(i, self.base());
        if (el.is_null()) return {};
        return ExprRef(self.arena(), el.to_offset());
    }
};

struct ETupleLitView {
    ExprRef self;
    template <class F> void each_elem(F&& f) const noexcept {
        detail::for_each_elem(self, std::forward<F>(f));
    }
    uint64_t count() const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    ExprRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(ek::ELEMS.code, self.base());
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return {};
        auto el = arr->get(i, self.base());
        if (el.is_null()) return {};
        return ExprRef(self.arena(), el.to_offset());
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
        auto cl_av = self.mirror()->get(ek::CLOSURE.code, self.base());
        if (cl_av.is_null()) return nullptr;
        return reinterpret_cast<const hermes::TinyObjectMap*>(
            self.base() + cl_av.to_offset().value());
    }

public:
    // Block of the captured closure body (closure_keys::BLOCK = 0 within the
    // closure-map). Returns null BlockRef if the closure mirror is missing.
    BlockRef body() const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto blk_av = m->get(lir_schema::closure_keys::BLOCK.code, self.base());
        if (blk_av.is_null()) return {};
        return BlockRef(self.arena(), blk_av.to_offset());
    }

    std::string_view closure_id() const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto av = m->get(lir_schema::closure_keys::NAME.code, self.base());
        if (av.is_null()) return {};
        return av.as_ptr<const hermes::ArenaString>(self.base())->view();
    }

    bool as_fn_ptr() const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::AS_FN_PTR.code, self.base());
        if (av.is_null()) return false;
        return av.as_value<uint8_t>() != 0;
    }

    bool is_move() const noexcept {
        auto* m = cl_map();
        if (!m) return false;
        auto av = m->get(lir_schema::closure_keys::IS_MOVE.code, self.base());
        if (av.is_null()) return false;
        return av.as_value<uint8_t>() != 0;
    }

    TypeRef ret_type(const TypePoolImpl* pool) const noexcept {
        auto* m = cl_map();
        if (!m) return {};
        auto av = m->get(lir_schema::closure_keys::RET_TYPE.code, self.base());
        if (av.is_null()) return {};
        return TypeRef(self.arena(), av.to_offset(), pool);
    }

    uint64_t capture_count() const noexcept {
        auto* m = cl_map();
        if (!m) return 0;
        auto names_av = m->get(
            lir_schema::closure_keys::CAPTURE_NAMES.code, self.base());
        if (names_av.is_null()) return 0;
        return names_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }

    template <class F>
    void each_capture_name(F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(
            lir_schema::closure_keys::CAPTURE_NAMES.code, self.base());
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = names_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>(self.base())->view());
        }
    }

    // Iterate (name, type) pairs from CL_CAPTURE_NAMES + CL_CAPTURE_TYPES.
    template <class F>
    void each_capture(const TypePoolImpl* pool, F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(lir_schema::closure_keys::CAPTURE_NAMES.code, self.base());
        auto types_av = m->get(lir_schema::closure_keys::CAPTURE_TYPES.code, self.base());
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        uint64_t tn = types_av.is_null() ? 0
            : types_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            std::string_view name;
            if (!nv.is_null())
                name = nv.as_ptr<const hermes::ArenaString>(self.base())->view();
            TypeRef t;
            if (i < tn) {
                auto tv = types_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
                if (!tv.is_null()) t = TypeRef(self.arena(), tv.to_offset(), pool);
            }
            f(name, t);
        }
    }

    // Iterate (name, type) pairs from CL_PARAM_NAMES + CL_PARAM_TYPES.
    template <class F>
    void each_param(const TypePoolImpl* pool, F&& f) const noexcept {
        auto* m = cl_map();
        if (!m) return;
        auto names_av = m->get(lir_schema::closure_keys::PARAM_NAMES.code, self.base());
        auto types_av = m->get(lir_schema::closure_keys::PARAM_TYPES.code, self.base());
        if (names_av.is_null()) return;
        uint64_t n = names_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        uint64_t tn = types_av.is_null() ? 0
            : types_av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            std::string_view name;
            if (!nv.is_null())
                name = nv.as_ptr<const hermes::ArenaString>(self.base())->view();
            TypeRef t;
            if (i < tn) {
                auto tv = types_av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
                if (!tv.is_null()) t = TypeRef(self.arena(), tv.to_offset(), pool);
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
        auto av = self.mirror()->get(hvk::MAP_VALUES.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    HermesValRef value(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_VALUES.code, self.base());
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return {};
        auto el = arr->get(i, self.base());
        if (el.is_null()) return {};
        return HermesValRef(self.arena(), el.to_offset());
    }
    std::string_view str_key(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_KEYS.code, self.base());
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return {};
        auto el = arr->get(i, self.base());
        if (el.is_null()) return {};
        return el.as_ptr<const hermes::ArenaString>(self.base())->view();
    }
    int64_t int_key(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::MAP_KEYS.code, self.base());
        if (av.is_null()) return 0;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return 0;
        auto el = arr->get(i, self.base());
        if (el.is_null()) return 0;
        return *el.as_ptr<const int64_t>(self.base());
    }
};

struct HVArrayView {
    HermesValRef self;
    std::string_view elem_type() const noexcept {
        return detail::read_string(self, hvk::TYPE_NAME.code);
    }
    uint64_t size() const noexcept {
        auto av = self.mirror()->get(hvk::ELEMS.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    HermesValRef elem(uint64_t i) const noexcept {
        auto av = self.mirror()->get(hvk::ELEMS.code, self.base());
        if (av.is_null()) return {};
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        if (i >= arr->size()) return {};
        auto el = arr->get(i, self.base());
        if (el.is_null()) return {};
        return HermesValRef(self.arena(), el.to_offset());
    }
};

namespace hl = lir_schema::hermes_lit_keys;

struct EHermesLitView {
    ExprRef self;
    HermesValRef root() const noexcept {
        auto av = self.mirror()->get(hl::ROOT.code, self.base());
        if (av.is_null()) return {};
        return HermesValRef(self.arena(), av.to_offset());
    }
    bool     has_captures()        const noexcept { return detail::read_bool(self, hl::HAS_CAPTURES.code); }
    uint32_t capture_param_count() const noexcept { return detail::read_u32(self, hl::CAPTURE_PARAM_COUNT.code); }
    template <class F> void each_capture_expr(F&& f) const noexcept {
        detail::for_each_expr(self, hl::CAPTURE_EXPRS.code, std::forward<F>(f));
    }
    template <class F> void each_capture_type(const TypePoolImpl* pool, F&& f) const noexcept {
        auto av = self.mirror()->get(hl::CAPTURE_TYPES.code, self.base());
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) { f(TypeRef{}); continue; }
            f(TypeRef(self.arena(), el.to_offset(), pool));
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
};

// PatVariantData { enum_name, variant, disc, bindings: Array<Varchar>, binding_types }
struct PatVariantDataView {
    PatRef self;
    int64_t          disc()      const noexcept { return detail::read_i64(self, pk::DISC.code); }
    std::string_view enum_name() const noexcept { return detail::read_string(self, pk::ENUM_NAME.code); }
    std::string_view variant()   const noexcept { return detail::read_string(self, pk::VARIANT.code); }
    template <class F>
    void each_binding(F&& f) const noexcept {
        auto av = self.mirror()->get(pk::BINDINGS.code, self.base());
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>(self.base())->view());
        }
    }
    template <class F>
    void each_binding_type(const TypePoolImpl* pool, F&& f) const noexcept {
        auto av = self.mirror()->get(pk::BINDING_TYPES.code, self.base());
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) { f(TypeRef{}); continue; }
            f(TypeRef(self.arena(), el.to_offset(), pool));
        }
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
        auto av = self.mirror()->get(pk::SUBS.code, self.base());
        if (av.is_null()) return;
        uint64_t n = av.as_ptr<const hermes::ObjectArray>(self.base())->size();
        for (uint64_t i = 0; i < n; ++i) {
            auto el = av.as_ptr<const hermes::ObjectArray>(self.base())->get(i, self.base());
            if (el.is_null()) continue;
            f(PatRef(self.arena(), el.to_offset()));
        }
    }
};

namespace detail {

// Iterate a key-stored Array<RelPtr<Pattern>> on a PatRef.
template <class F>
inline void for_each_pat(const PatRef& r, uint8_t key, F&& f) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>(r.base())->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>(r.base())->get(i, r.base());
        if (el.is_null()) continue;
        f(PatRef(r.arena(), el.to_offset()));
    }
}

// Iterate a key-stored Array<Varchar> on a PatRef.
template <class F>
inline void for_each_string(const PatRef& r, uint8_t key, F&& f) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>(r.base())->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>(r.base())->get(i, r.base());
        if (el.is_null()) { f(std::string_view{}); continue; }
        f(el.as_ptr<const hermes::ArenaString>(r.base())->view());
    }
}

// Iterate a key-stored Array<RelPtr<LogosType>> on a PatRef.
template <class F>
inline void for_each_type(const PatRef& r, uint8_t key,
                          const TypePoolImpl* pool, F&& f) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return;
    uint64_t n = av.as_ptr<const hermes::ObjectArray>(r.base())->size();
    for (uint64_t i = 0; i < n; ++i) {
        auto el = av.as_ptr<const hermes::ObjectArray>(r.base())->get(i, r.base());
        if (el.is_null()) { f(TypeRef{}); continue; }
        f(TypeRef(r.arena(), el.to_offset(), pool));
    }
}

// Read a single RelPtr<Pattern> stored under SUB / etc. as a 0-or-1 array.
inline PatRef first_pat(const PatRef& r, uint8_t key) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return {};
    auto* arr = av.as_ptr<const hermes::ObjectArray>(r.base());
    if (arr->size() == 0) return {};
    auto el = arr->get(0, r.base());
    if (el.is_null()) return {};
    return PatRef(r.arena(), el.to_offset());
}

inline TypeRef pat_type(const PatRef& r, uint8_t key,
                        const TypePoolImpl* pool) noexcept {
    auto av = r.mirror()->get(key, r.base());
    if (av.is_null()) return TypeRef{};
    return TypeRef(r.arena(), av.to_offset(), pool);
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
        auto av = self.mirror()->get(pk::SUBS.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    uint64_t binding_count() const noexcept {
        auto av = self.mirror()->get(pk::BINDINGS.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
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
        auto av = self.mirror()->get(pk::FIELDS.code, self.base());
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) continue;
            f(PatFieldBindingView{PatRef(self.arena(), el.to_offset())});
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
        auto av = self.mirror()->get(pk::PREFIX.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
    }
    uint64_t suffix_count() const noexcept {
        auto av = self.mirror()->get(pk::SUFFIX.code, self.base());
        if (av.is_null()) return 0;
        return av.as_ptr<const hermes::ObjectArray>(self.base())->size();
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
};

// PatRefBind { name, is_mut, bind_type }
struct PatRefBindView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
    bool             is_mut() const noexcept { return detail::read_bool(self, pk::IS_MUT.code); }
    TypeRef          bind_type(const TypePoolImpl* pool) const noexcept {
        return detail::pat_type(self, pk::BIND_TYPE.code, pool);
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
    auto av = s.mirror()->get(key, s.base());
    if (av.is_null()) return {};
    return BlockRef(s.arena(), av.to_offset());
}

inline std::string_view stmt_str(const StmtRef& s, uint8_t key) noexcept {
    return read_string(s, key);
}

inline TypeRef stmt_type(const StmtRef& s, uint8_t key, const TypePoolImpl* pool) noexcept {
    auto av = s.mirror()->get(key, s.base());
    if (av.is_null()) return TypeRef{};
    return TypeRef(s.arena(), av.to_offset(), pool);
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
};

struct SAssignView {
    StmtRef self;
    std::string_view name() const noexcept  { return detail::stmt_str(self, sk::NAME.code); }
    ExprRef          value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
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

struct SDeleteView {
    StmtRef self;
    ExprRef expr() const noexcept { return detail::stmt_sub_expr(self, sk::EXPR.code); }
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
};

struct SLetElseView {
    StmtRef self;
    ExprRef  scrut() const noexcept       { return detail::stmt_sub_expr(self, sk::SCRUT.code); }
    BlockRef else_block() const noexcept  { return detail::stmt_sub_block(self, sk::ELSE_DIVERGE.code); }
    PatRef   pat() const noexcept {
        auto av = self.mirror()->get(sk::PAT.code, self.base());
        if (av.is_null()) return {};
        return PatRef(self.arena(), av.to_offset());
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
};

struct SMatchView {
    StmtRef self;
    ExprRef scrut() const noexcept { return detail::stmt_sub_expr(self, sk::SCRUT.code); }

    template <class F>
    void each_arm(F&& f) const noexcept {
        auto av = self.mirror()->get(sk::ARMS.code, self.base());
        if (av.is_null()) return;
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) continue;
            f(EMatchArmRef(self.arena(), el.to_offset()));
        }
    }
};

inline uint32_t stmt_line(const StmtRef& s) noexcept {
    return detail::read_u32(s, sc::LINE.code);
}

} // namespace logos::compiler::lir_view
