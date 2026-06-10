// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <new>

#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes2 {

// Decimal — fixed-precision signed decimal (Logos HDecimal, code 102). No internal
// Refs (plain POD). value = (neg ? -1 : 1) * coeff / 10^scale. spec packs scale in
// bits[0:11] and the sign in bit[31].
struct Decimal {
    uint32_t spec;
    uint64_t coeff;

    static constexpr uint32_t SCALE_MASK = 0xFFF;
    static constexpr uint32_t SIGN_BIT   = 0x80000000u;

    uint32_t scale()  const noexcept { return spec & SCALE_MASK; }
    bool     is_neg() const noexcept { return (spec & SIGN_BIT) != 0; }
    uint64_t coefficient() const noexcept { return coeff; }

    [[nodiscard]] static logos::expected<Decimal*>
    create(Arena& arena, uint64_t coeff, uint32_t scale, bool neg) noexcept {
        TypeTag tag(tc::DECIMAL);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(Decimal), alignof(Decimal), tag));
        auto* d = new (mem) Decimal();
        d->spec  = (scale & SCALE_MASK) | (neg ? SIGN_BIT : 0);
        d->coeff = coeff;
        return d;
    }
};
static_assert(sizeof(Decimal) == 16);

// TypedValue — SDN datatype instantiation `@Type(params?) = init` (Logos
// HTypedValue, code 4115). Three AT-REST AnyVal words (type_name Ref→string, params,
// init).
struct TypedValue {
    AnyVal type_name;
    AnyVal params;
    AnyVal init;

    [[nodiscard]] static logos::expected<TypedValue*>
    create(Arena& arena, AnyVal type_name, AnyVal params, AnyVal init) noexcept {
        TypeTag tag(tc::TYPEDVALUE);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(TypedValue), alignof(TypedValue), tag));
        auto* t = new (mem) TypedValue();
        t->type_name = type_name;   // AnyVal assignment lowers (re-anchors) at the slot
        t->params    = params;
        t->init      = init;
        return t;
    }
};
static_assert(sizeof(TypedValue) == 24);

// Parameter — query-parameter slot `?name` (Logos HParameter, code 127). Two AT-REST
// AnyVal words (name Ref→string, value or null).
struct Parameter {
    AnyVal name;
    AnyVal value;

    [[nodiscard]] static logos::expected<Parameter*>
    create(Arena& arena, AnyVal name, AnyVal value) noexcept {
        TypeTag tag(tc::PARAMETER);
        LOGOS_TRY(auto* mem, arena.allocate(sizeof(Parameter), alignof(Parameter), tag));
        auto* p = new (mem) Parameter();
        p->name  = name;
        p->value = value;
        return p;
    }
};
static_assert(sizeof(Parameter) == 16);

} // namespace logos::hermes2
