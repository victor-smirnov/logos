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

    // Type lookup is added by Phase 3d when a reader needs it (requires a
    // TypePoolImpl* to wrap the offset in a TypeRef). For now views that
    // don't use the type field skip the dependency.

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

// ── Exemplar view structs (one per category) ─────────────────────────────
//
// Pattern: EXxxView holds the ExprRef; accessors lazily decode fields. The
// remaining variants will follow as Phase 3d migrates each reader.

namespace ek = lir_schema::expr_keys;
namespace pk = lir_schema::pat_keys;

// ELitInt — leaf, single i64 payload.
struct ELitIntView {
    ExprRef self;
    int64_t value() const noexcept { return detail::read_i64(self, ek::LIT_I64.code); }
};

// EBinOp — two sub-expressions + Varchar op name.
struct EBinOpView {
    ExprRef self;
    std::string_view op() const noexcept { return detail::read_string(self, ek::OP.code); }
    ExprRef lhs() const noexcept { return self.sub_expr(ek::LHS.code); }
    ExprRef rhs() const noexcept { return self.sub_expr(ek::RHS.code); }
};

// SLet — LSlot wrapped under sk::*; covers stmt-with-expr-and-pat shape.
struct SLetView {
    StmtRef self;
    ExprRef value() const noexcept { return self.sub_expr(ek::OPERAND.code); }
};

// PatBool — leaf pat.
struct PatBoolView {
    PatRef self;
    bool value() const noexcept { return detail::read_bool(self, pk::BOOL_VALUE.code); }
};

} // namespace logos::compiler::lir_view
