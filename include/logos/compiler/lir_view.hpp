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
    auto* arr = av.as_ptr<const hermes::ObjectArray>(base());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto el = arr->get(i, base());
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
    auto* arr = av.as_ptr<const hermes::ObjectArray>(r.base());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto el = arr->get(i, r.base());
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
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
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
    int32_t          vtable_index() const noexcept {
        auto av = self.mirror()->get(ek::VTABLE_INDEX.code, self.base());
        if (av.is_null()) return -1;
        return int32_t(detail::read_u32(self, ek::VTABLE_INDEX.code));
    }
    template <class F> void each_arg(F&& f) const noexcept {
        detail::for_each_arg(self, std::forward<F>(f));
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
        auto* names_arr  = names_av.as_ptr<const hermes::ObjectArray>(self.base());
        auto* values_arr = values_av.as_ptr<const hermes::ObjectArray>(self.base());
        uint64_t n = std::min(names_arr->size(), values_arr->size());
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_arr->get(i, self.base());
            auto vv = values_arr->get(i, self.base());
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
        auto* names_arr  = names_av.as_ptr<const hermes::ObjectArray>(self.base());
        auto* values_arr = values_av.as_ptr<const hermes::ObjectArray>(self.base());
        uint64_t n = std::min(names_arr->size(), values_arr->size());
        for (uint64_t i = 0; i < n; ++i) {
            auto nv = names_arr->get(i, self.base());
            auto vv = values_arr->get(i, self.base());
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
};

struct EEnumLitDataView {
    ExprRef self;
    std::string_view enum_name() const noexcept { return detail::read_string(self, ek::ENUM_NAME.code); }
    int64_t          disc()      const noexcept { return detail::read_i64(self, ek::DISC.code); }
    template <class F> void each_payload(F&& f) const noexcept {
        detail::for_each_payload(self, std::forward<F>(f));
    }
};

// EClosureBox { closure: RelPtr<EClosure-mirror> } — captures live in the
// closure mirror's CL_CAPTURE_NAMES (closure_keys::CAPTURE_NAMES).
struct EClosureBoxView {
    ExprRef self;

    // Block of the captured closure body (closure_keys::BLOCK = 0 within the
    // closure-map). Returns null BlockRef if the closure mirror is missing.
    BlockRef body() const noexcept {
        auto cl_av = self.mirror()->get(ek::CLOSURE.code, self.base());
        if (cl_av.is_null()) return {};
        auto* cl_map = reinterpret_cast<const hermes::TinyObjectMap*>(
            self.base() + cl_av.to_offset().value());
        auto blk_av = cl_map->get(lir_schema::closure_keys::BLOCK.code, self.base());
        if (blk_av.is_null()) return {};
        return BlockRef(self.arena(), blk_av.to_offset());
    }

    template <class F>
    void each_capture_name(F&& f) const noexcept {
        auto cl_av = self.mirror()->get(ek::CLOSURE.code, self.base());
        if (cl_av.is_null()) return;
        auto* cl_map = reinterpret_cast<const hermes::TinyObjectMap*>(
            self.base() + cl_av.to_offset().value());
        auto names_av = cl_map->get(
            lir_schema::closure_keys::CAPTURE_NAMES.code, self.base());
        if (names_av.is_null()) return;
        auto* arr = names_av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>(self.base())->view());
        }
    }
};

// ── Stub views (Phase 3d): bodies still go through lexpr_of() to reach the
// underlying variant. Promoted to richer accessors as call-sites migrate.

struct EAddrOfTempView {
    ExprRef self;
    ExprRef inner() const noexcept { return self.sub_expr(ek::INNER.code); }
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

struct EHermesLitView  { ExprRef self; };
struct EPackExpandView { ExprRef self; };

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
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) continue;
            f(el.as_ptr<const hermes::ArenaString>(self.base())->view());
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
        auto* arr = av.as_ptr<const hermes::ObjectArray>(self.base());
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto el = arr->get(i, self.base());
            if (el.is_null()) continue;
            f(PatRef(self.arena(), el.to_offset()));
        }
    }
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
    ExprRef          index() const noexcept    { return detail::stmt_sub_expr(self, sk::INDEX.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SChainFieldWriteView {
    StmtRef self;
    std::string_view receiver() const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SDerefFieldWriteView {
    StmtRef self;
    std::string_view receiver() const noexcept { return detail::stmt_str(self, sk::RECEIVER.code); }
    ExprRef          value() const noexcept    { return detail::stmt_sub_expr(self, sk::VALUE.code); }
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
    ExprRef  cond() const noexcept { return detail::stmt_sub_expr(self, sk::COND.code); }
    BlockRef body() const noexcept { return detail::stmt_sub_block(self, sk::BODY.code); }
};

struct SForView {
    StmtRef self;
    std::string_view var() const noexcept  { return detail::stmt_str(self, sk::VAR.code); }
    ExprRef          lo() const noexcept   { return detail::stmt_sub_expr(self, sk::LO.code); }
    ExprRef          hi() const noexcept   { return detail::stmt_sub_expr(self, sk::HI.code); }
    BlockRef         body() const noexcept { return detail::stmt_sub_block(self, sk::BODY.code); }
};

struct SLoopView {
    StmtRef self;
    BlockRef body() const noexcept { return detail::stmt_sub_block(self, sk::BODY.code); }
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
};

struct SLetElseView {
    StmtRef self;
    ExprRef  scrut() const noexcept       { return detail::stmt_sub_expr(self, sk::SCRUT.code); }
    BlockRef else_block() const noexcept  { return detail::stmt_sub_block(self, sk::ELSE_DIVERGE.code); }
};

struct SBreakView {
    StmtRef self;
    // Optional break-with-value expression (null ExprRef when absent).
    ExprRef value() const noexcept { return detail::stmt_sub_expr(self, sk::VALUE.code); }
};

struct SContinueView { StmtRef self; };
struct SDropView     { StmtRef self; };

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
