// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/relative_ptr.hpp>
#include <logos/hermes/tagged_ptr.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes {

// Datatype: type declaration (name + optional params + optional constructor args).
struct DatatypeData {
    RelativePtr<ArenaString>  name;
    RelativePtr<ObjectArray>  params;
    RelativePtr<ObjectArray>  ctr;
    uint32_t extras = 0;

    static DatatypeData* create(Arena& arena, ArenaString* type_name,
                                ObjectArray* type_params = nullptr,
                                ObjectArray* ctr_args = nullptr)
    {
        TypeTag tag(type_hash::Datatype, TagDescriptor::Data);
        auto* mem = static_cast<DatatypeData*>(
            arena.allocate(sizeof(DatatypeData), alignof(DatatypeData), tag));
        uint8_t* base = arena.head().data();
        mem->extras = 0;
        mem->params.clear();
        mem->ctr.clear();
        mem->name.set(type_name, base);
        if (type_params) mem->params.set(type_params, base);
        if (ctr_args) mem->ctr.set(ctr_args, base);
        return mem;
    }

    std::string_view name_view(uint8_t* base) const { return name.get(base)->view(); }
    bool has_params() const { return !params.is_null(); }
    bool has_ctr() const { return !ctr.is_null(); }

    bool is_const() const    { return extras & 1; }
    bool is_volatile() const { return extras & 2; }
    uint8_t ptr_count() const  { return (extras >> 2) & 0xFF; }
    uint8_t ref_count() const  { return (extras >> 10) & 0x3; }

    void set_const(bool v)    { if (v) extras |= 1; else extras &= ~1u; }
    void set_volatile(bool v) { if (v) extras |= 2; else extras &= ~2u; }
    void add_ptr() { uint8_t n = ptr_count() + 1; extras = (extras & ~(0xFFu << 2)) | (uint32_t(n) << 2); }
    void set_refs(uint8_t n) { extras = (extras & ~(0x3u << 10)) | (uint32_t(n & 3) << 10); }
};

// 3 x RelativePtr(4) + extras(4) = 16, but with alignment padding...
// Actually: name(4) + params(4) + ctr(4) + extras(4) = 16
static_assert(sizeof(DatatypeData) == 16);

// TypedValue: a value paired with its type declaration.
struct TypedValueData {
    RelativePtr<DatatypeData> datatype;
    TaggedPtr value;  // 8 bytes (embedded or segment-relative pointer)

    static TypedValueData* create(Arena& arena, DatatypeData* dt) {
        TypeTag tag(type_hash::TypedValue, TagDescriptor::Data);
        auto* mem = static_cast<TypedValueData*>(
            arena.allocate(sizeof(TypedValueData), alignof(TypedValueData), tag));
        uint8_t* base = arena.head().data();
        mem->datatype.set(dt, base);
        mem->value = TaggedPtr{};
        return mem;
    }
};

// datatype(4) + padding(4) + value(8) = 16
static_assert(sizeof(TypedValueData) == 16);

// ParameterData: a query parameter placeholder (?name).
struct ParameterData {
    RelativePtr<ArenaString> name;

    static ParameterData* create(Arena& arena, ArenaString* param_name) {
        TypeTag tag(type_hash::Parameter, TagDescriptor::Data);
        auto* mem = static_cast<ParameterData*>(
            arena.allocate(sizeof(ParameterData), alignof(ParameterData), tag));
        uint8_t* base = arena.head().data();
        mem->name.set(param_name, base);
        return mem;
    }

    std::string_view name_view(uint8_t* base) const { return name.get(base)->view(); }
};

static_assert(sizeof(ParameterData) == sizeof(arena_offset_t));

} // namespace logos::hermes
