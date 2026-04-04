// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Datatype: type declaration (name + optional params + optional constructor args).
struct DatatypeData {
    RelativePtr<ArenaString>  name;
    RelativePtr<ObjectArray>  params;
    RelativePtr<ObjectArray>  ctr;
    uint32_t extras = 0;

    [[nodiscard]] static logos::expected<DatatypeData*> create(Arena& arena, ArenaString* type_name,
                                ObjectArray* type_params = nullptr,
                                ObjectArray* ctr_args = nullptr) noexcept
    {
        TypeTag tag(type_hash::Datatype, TagDescriptor::Data);
        LOGOS_TRY(auto* mem_void, arena.allocate(sizeof(DatatypeData), alignof(DatatypeData), tag));
        auto* mem = static_cast<DatatypeData*>(mem_void);
        uint8_t* base = arena.head().data();
        mem->extras = 0;
        mem->params.clear();
        mem->ctr.clear();
        mem->name.set(type_name, base);
        if (type_params) mem->params.set(type_params, base);
        if (ctr_args) mem->ctr.set(ctr_args, base);
        return mem;
    }

    std::string_view name_view(uint8_t* base) const noexcept { return name.get(base)->view(); }
    bool has_params() const noexcept { return !params.is_null(); }
    bool has_ctr() const noexcept { return !ctr.is_null(); }

    bool is_const() const noexcept    { return extras & 1; }
    bool is_volatile() const noexcept { return extras & 2; }
    uint8_t ptr_count() const noexcept  { return (extras >> 2) & 0xFF; }
    uint8_t ref_count() const noexcept  { return (extras >> 10) & 0x3; }

    void set_const(bool v) noexcept    { if (v) extras |= 1; else extras &= ~1u; }
    void set_volatile(bool v) noexcept { if (v) extras |= 2; else extras &= ~2u; }
    void add_ptr() noexcept { uint8_t n = ptr_count() + 1; extras = (extras & ~(0xFFu << 2)) | (uint32_t(n) << 2); }
    void set_refs(uint8_t n) noexcept { extras = (extras & ~(0x3u << 10)) | (uint32_t(n & 3) << 10); }
};

// 3 x RelativePtr(4) + extras(4) = 16, but with alignment padding...
// Actually: name(4) + params(4) + ctr(4) + extras(4) = 16
static_assert(sizeof(DatatypeData) == 16);

// TypedValue: a value paired with its type declaration.
struct TypedValueData {
    RelativePtr<DatatypeData> datatype;
    AnyVal value;  // 8 bytes (embedded or segment-relative pointer)

    [[nodiscard]] static logos::expected<TypedValueData*> create(Arena& arena, DatatypeData* dt) noexcept {
        TypeTag tag(type_hash::TypedValue, TagDescriptor::Data);
        LOGOS_TRY(auto* mem_void, arena.allocate(sizeof(TypedValueData), alignof(TypedValueData), tag));
        auto* mem = static_cast<TypedValueData*>(mem_void);
        uint8_t* base = arena.head().data();
        mem->datatype.set(dt, base);
        mem->value = AnyVal{};
        return mem;
    }
};

// datatype(4) + padding(4) + value(8) = 16
static_assert(sizeof(TypedValueData) == 16);

// ParameterData: a query parameter placeholder (?name).
struct ParameterData {
    RelativePtr<ArenaString> name;

    [[nodiscard]] static logos::expected<ParameterData*> create(Arena& arena, ArenaString* param_name) noexcept {
        TypeTag tag(type_hash::Parameter, TagDescriptor::Data);
        LOGOS_TRY(auto* mem_void, arena.allocate(sizeof(ParameterData), alignof(ParameterData), tag));
        auto* mem = static_cast<ParameterData*>(mem_void);
        uint8_t* base = arena.head().data();
        mem->name.set(param_name, base);
        return mem;
    }

    std::string_view name_view(uint8_t* base) const noexcept { return name.get(base)->view(); }
};

static_assert(sizeof(ParameterData) == sizeof(arena_offset_t));

} // namespace logos::hermes
