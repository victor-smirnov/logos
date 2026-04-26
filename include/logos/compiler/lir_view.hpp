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

struct EBinOpView {
    ExprRef self;
    std::string_view op() const noexcept { return detail::read_string(self, ek::OP.code); }
    ExprRef lhs() const noexcept { return self.sub_expr(ek::LHS.code); }
    ExprRef rhs() const noexcept { return self.sub_expr(ek::RHS.code); }
};

// ── Pattern leaf exemplar ────────────────────────────────────────────────

struct PatBoolView {
    PatRef self;
    bool value() const noexcept { return detail::read_bool(self, pk::BOOL_VALUE.code); }
};

} // namespace logos::compiler::lir_view
