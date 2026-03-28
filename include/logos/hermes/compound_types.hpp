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

// ============================================================================
// Datatype: a type declaration (name + optional params + optional constructor args)
//
// Arena layout (32 bytes + tag):
//   name:   RelativePtr<ArenaString>     — type name (e.g. "Array", "Decimal")
//   params: RelativePtr<ObjectArray>     — type parameters (e.g. <Integer, String>), nullable
//   ctr:    RelativePtr<ObjectArray>     — constructor args (e.g. (10, 2)), nullable
//   extras: uint32_t                     — C++ qualifiers bitmask (const, volatile, ptr, ref)
// ============================================================================

struct DatatypeData {
    RelativePtr<ArenaString>  name;
    RelativePtr<ObjectArray>  params;
    RelativePtr<ObjectArray>  ctr;
    uint32_t extras = 0; // Bits: [0]=const, [1]=volatile, [2..9]=ptr_count, [10..11]=ref_count

    static DatatypeData* create(Arena& arena, ArenaString* type_name,
                                ObjectArray* type_params = nullptr,
                                ObjectArray* ctr_args = nullptr)
    {
        TypeTag tag(type_hash::Datatype, TagDescriptor::Data);
        auto* mem = static_cast<DatatypeData*>(
            arena.allocate(sizeof(DatatypeData), alignof(DatatypeData), tag));
        mem->extras = 0;
        mem->name.set(type_name);
        if (type_params) mem->params.set(type_params);
        if (ctr_args) mem->ctr.set(ctr_args);
        return mem;
    }

    std::string_view name_view() const { return name.get()->view(); }
    bool has_params() const { return !params.is_null(); }
    bool has_ctr() const { return !ctr.is_null(); }

    // Qualifier accessors. Layout of extras:
    //   bit 0     = const
    //   bit 1     = volatile
    //   bits 2..9 = pointer indirection count (0-255)
    //   bits 10..11 = reference count (0 = none, 1 = &, 2 = &&)
    bool is_const() const    { return extras & 1; }
    bool is_volatile() const { return extras & 2; }
    uint8_t ptr_count() const  { return (extras >> 2) & 0xFF; }
    uint8_t ref_count() const  { return (extras >> 10) & 0x3; }

    void set_const(bool v)    { if (v) extras |= 1; else extras &= ~1u; }
    void set_volatile(bool v) { if (v) extras |= 2; else extras &= ~2u; }
    void add_ptr() { uint8_t n = ptr_count() + 1; extras = (extras & ~(0xFFu << 2)) | (uint32_t(n) << 2); }
    void set_refs(uint8_t n) { extras = (extras & ~(0x3u << 10)) | (uint32_t(n & 3) << 10); }
};

static_assert(sizeof(DatatypeData) == 32);

// ============================================================================
// TypedValue: a value paired with its type declaration
//
// Arena layout (16 bytes + tag):
//   datatype: RelativePtr<DatatypeData>
//   value:    TaggedPtr (embedded or pointer to arena object)
// ============================================================================

struct TypedValueData {
    RelativePtr<DatatypeData> datatype;
    TaggedPtr value;

    static TypedValueData* create(Arena& arena, DatatypeData* dt) {
        TypeTag tag(type_hash::TypedValue, TagDescriptor::Data);
        auto* mem = static_cast<TypedValueData*>(
            arena.allocate(sizeof(TypedValueData), alignof(TypedValueData), tag));
        mem->datatype.set(dt);
        mem->value = TaggedPtr{};
        return mem;
    }
};

static_assert(sizeof(TypedValueData) == 16);

// ============================================================================
// ParameterData: a query parameter placeholder (?name)
//
// Arena layout (8 bytes + tag):
//   name: RelativePtr<ArenaString>
// ============================================================================

struct ParameterData {
    RelativePtr<ArenaString> name;

    static ParameterData* create(Arena& arena, ArenaString* param_name) {
        TypeTag tag(type_hash::Parameter, TagDescriptor::Data);
        auto* mem = static_cast<ParameterData*>(
            arena.allocate(sizeof(ParameterData), alignof(ParameterData), tag));
        mem->name.set(param_name);
        return mem;
    }

    std::string_view name_view() const { return name.get()->view(); }
};

static_assert(sizeof(ParameterData) == 8);

} // namespace logos::hermes
