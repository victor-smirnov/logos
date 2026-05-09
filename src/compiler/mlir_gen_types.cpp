// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_types.cpp — Type conversion, struct/enum/class registration.

#include "mlir_gen_impl.hpp"
#include "mono_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// Type conversion: LogosType → mlir::Type
// ---------------------------------------------------------------------------

mlir::Type MLIRGenImpl::logos_to_mlir(TypeRef tv) {
    if (!tv) return nullptr;
    if (type_str(tv) == "AnyVal") return builder_.getI32Type();
    switch (tv.kind()) {
    case LogosType::Kind::Void:   return nullptr;
    case LogosType::Kind::I32:    return builder_.getI32Type();
    case LogosType::Kind::I64:    return builder_.getI64Type();
    case LogosType::Kind::F64:    return builder_.getF64Type();
    case LogosType::Kind::F32:    return builder_.getF32Type();
    case LogosType::Kind::Bool:   return builder_.getI1Type();
    case LogosType::Kind::U8:     return builder_.getIntegerType(8);
    case LogosType::Kind::I8:     return builder_.getIntegerType(8);
    case LogosType::Kind::I16:    return builder_.getIntegerType(16);
    case LogosType::Kind::U16:    return builder_.getIntegerType(16);
    case LogosType::Kind::I24:    return builder_.getIntegerType(24);
    case LogosType::Kind::U24:    return builder_.getIntegerType(24);
    case LogosType::Kind::I56:    return builder_.getIntegerType(56);
    case LogosType::Kind::U56:    return builder_.getIntegerType(56);
    case LogosType::Kind::U32:    return builder_.getIntegerType(32);
    case LogosType::Kind::U64:    return builder_.getIntegerType(64);
    case LogosType::Kind::I128:   return builder_.getIntegerType(128);
    case LogosType::Kind::U128:   return builder_.getIntegerType(128);
    case LogosType::Kind::Usize:  return builder_.getIntegerType(::logos::compiler::g_target_pointer_bits);
    case LogosType::Kind::Isize:  return builder_.getIntegerType(::logos::compiler::g_target_pointer_bits);
    case LogosType::Kind::Char:   return builder_.getI32Type();  // Unicode scalar = 4 bytes
    case LogosType::Kind::IntLit:   return builder_.getI32Type();
    case LogosType::Kind::FloatLit: return builder_.getF64Type();
    case LogosType::Kind::Enum: {
        // Tagged enums are passed by pointer; C-style enums use their
        // backing integer type (i32 by default, or `enum Foo : u64 {}`).
        if (resolve_tagged_enum(std::string(tv.enum_name()), tv)) return ptr_type();
        return enum_disc_mlir(std::string(tv.enum_name()));
    }
    case LogosType::Kind::Ptr:    return ptr_type();
    case LogosType::Kind::Ref:    return ptr_type();  // &T — same layout as *const T
    case LogosType::Kind::MutRef: return ptr_type();  // &mut T — same layout as *mut T
    case LogosType::Kind::Array: {
        // For struct-typed elements, inline the LLVM struct aggregate as the
        // slot type (sizeof(Struct) per slot) instead of the bare pointer
        // type — otherwise array slots are undersized and storing each
        // element would overlap into the next slot, plus pointers into a
        // local slot would dangle once the function returns. Mirrors the
        // inline-embed path in `register_struct` for struct-of-struct.
        TypeRef elem_tv = tv.elem();
        if (elem_tv && (elem_tv.kind() == LogosType::Kind::Struct ||
                        elem_tv.kind() == LogosType::Kind::ZonedStruct) &&
            type_str(elem_tv) != "AnyVal") {
            auto cname = concrete_struct_name(elem_tv);
            auto sit   = struct_types_.find(cname);
            if (sit == struct_types_.end()) {
                auto def_it = all_struct_defs_.find(cname);
                if (def_it != all_struct_defs_.end()) {
                    register_struct(*def_it->second);
                    sit = struct_types_.find(cname);
                }
            }
            if (sit != struct_types_.end())
                return mlir::LLVM::LLVMArrayType::get(
                    sit->second.llvm_type, tv.arr_size());
        }
        auto elem = logos_to_mlir(elem_tv);
        if (!elem) return nullptr;
        return mlir::LLVM::LLVMArrayType::get(elem, tv.arr_size());
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        // Check type alias first.
        auto cname = concrete_struct_name(tv);
        auto ait = type_aliases_.find(cname);
        if (ait != type_aliases_.end()) return ait->second;
        // Structs/datatypes are always passed by pointer; no need to wait for registration.
        return ptr_type();
    }
    case LogosType::Kind::Closure:
        // Closures are {fn_ptr, env_ptr}, passed by pointer.
        return ptr_type();
    case LogosType::Kind::FnPtr:
        // Bare function pointer: just a single ptr.
        return ptr_type();
    case LogosType::Kind::Slice:
        // Slices are fat pointers {ptr, i64}, passed by pointer (like structs/tuples).
        return ptr_type();
    case LogosType::Kind::Tuple: {
        // Tuples are anonymous LLVM struct types, passed by pointer (like structs).
        llvm::SmallVector<mlir::Type> fields;
        for (auto e : tv.tuple_elems()) {
            auto ft = logos_to_mlir(e);
            if (!ft) return nullptr;
            fields.push_back(ft);
        }
        return ptr_type();
    }
    case LogosType::Kind::TaggedPtr:
        // &tagged<TS> Trait is a thin pointer (*const u8) — same layout as any ptr.
        return ptr_type();
    case LogosType::Kind::TraitObject:
        // &dyn Trait is a fat pointer {data_ptr, vtable_ptr}, passed by pointer.
        return ptr_type();
    case LogosType::Kind::TypeVar:
        // TypeVar should have been eliminated by mono_pass.
        std::fprintf(stderr, "mlir_gen: unresolved TypeVar '%s' — mono_pass required\n",
                     std::string(tv.type_var_name()).c_str());
        return nullptr;
    case LogosType::Kind::ConstVar:
        // ConstVar (e.g. N in [T; N]) should have been resolved by mono_pass.
        std::fprintf(stderr, "mlir_gen: unresolved ConstVar '%s' — mono_pass required\n",
                     std::string(tv.type_var_name()).c_str());
        return nullptr;
    case LogosType::Kind::AssocType: {
        // AssocType (T::Item) should have been resolved by mono_pass.
        std::string base_s = tv.assoc_base() ? type_str(tv.assoc_base()) : "<null>";
        std::fprintf(stderr,
                     "mlir_gen: unresolved AssocType '%s::%s::%s' — mono_pass required\n",
                     base_s.c_str(), std::string(tv.trait_name()).c_str(), std::string(tv.assoc_type_name()).c_str());
        return nullptr;
    }
    case LogosType::Kind::Error:       return nullptr;
    case LogosType::Kind::ImplTrait:   return nullptr;
    case LogosType::Kind::Generic:     return nullptr;  // value-side marker only
    case LogosType::Kind::HStaticLit:  return nullptr;  // type-level only, no MLIR mapping
    case LogosType::Kind::CfgSlotType: return nullptr;  // type-level only, no MLIR mapping
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Struct registration (Pass 0)
// ---------------------------------------------------------------------------

bool MLIRGenImpl::register_struct(const LStructDef& sd) {
    std::string key = qualify_pkg(sd.pkg, sd.name);
    if (struct_types_.count(key)) return true;
    auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), key);
    StructInfo info;
    info.name      = key;
    info.llvm_type = struct_type;

    std::vector<mlir::Type> field_types;
    for (auto& f : sd.fields) {
        mlir::Type ft;
        std::string fsname;
        // Datatype fields are embedded by value (not by pointer).
        // Regular Struct fields with a registered llvm_type are also inline.
        TypeRef fv{f.type};
        // AnyVal is lowered as a scalar i32 everywhere — including as a
        // struct field. Otherwise the inline-embed branch below would
        // store a wrapped !llvm.struct<"AnyVal", (i32)>, and field-loads
        // would yield the struct value, mismatching arg-passing ABI.
        if ((fv.kind() == LogosType::Kind::ZonedStruct ||
             fv.kind() == LogosType::Kind::Struct) &&
            type_str(fv) == "AnyVal") {
            ft = logos_to_mlir(f.type);
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, false});
            field_types.push_back(ft);
            continue;
        }
        if (fv.kind() == LogosType::Kind::ZonedStruct ||
            fv.kind() == LogosType::Kind::Struct) {
            auto cname = mlir_struct_key(f.type);
            auto sit = struct_types_.find(cname);
            if (sit == struct_types_.end()) {
                // Not yet registered — try to register it now (resolve dependency order).
                auto def_it = all_struct_defs_.find(cname);
                if (def_it == all_struct_defs_.end())
                    def_it = all_struct_defs_.find(concrete_struct_name(f.type));
                if (def_it != all_struct_defs_.end())
                    register_struct(*def_it->second);
                sit = struct_types_.find(cname);
            }
            if (sit != struct_types_.end()) {
                // Inline embed: use the sub-struct's LLVM aggregate type directly.
                ft = sit->second.llvm_type;
                fsname = cname;
            } else {
                // Still not found (forward reference or unknown type) — use pointer.
                ft = ptr_type();
                fsname = cname;
            }
        } else if ((fv.kind() == LogosType::Kind::Ptr ||
                    fv.kind() == LogosType::Kind::Ref ||
                    fv.kind() == LogosType::Kind::MutRef) &&
                   fv.pointee() &&
                   (fv.pointee().kind() == LogosType::Kind::Struct ||
                    fv.pointee().kind() == LogosType::Kind::ZonedStruct)) {
            // *Struct / &Struct / &mut Struct field — pointer to struct.
            // Set fsname so gen_recv_struct can chain field access through
            // it; mark is_pointer so the auto-Drop pass skips it.
            ft = ptr_type();
            fsname = mlir_struct_key(fv.pointee());
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname, /*is_pointer=*/true});
            field_types.push_back(ft);
            continue;
        } else {
            ft = logos_to_mlir(f.type);
            if (!ft) {
                std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n", sd.name.c_str());
                return false;
            }
        }
        info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname, false});
        field_types.push_back(ft);
    }
    if (mlir::failed(struct_type.setBody(field_types, false))) {
        std::fprintf(stderr, "mlir_gen: failed to set struct body for '%s'\n", key.c_str());
        return false;
    }
    struct_types_[key] = info;
    // Back-compat alias under the bare name for paths that look up via
    // concrete_struct_name (which doesn't carry pkg). First-registered wins.
    if (!sd.pkg.empty() && !struct_types_.count(sd.name))
        struct_types_[sd.name] = std::move(info);
    return true;
}

// ---------------------------------------------------------------------------
// Tagged enum registration
// Layout: { i32 disc, [max_payload_bytes x i8] }
// ---------------------------------------------------------------------------

// Compute ABI byte size from LogosType — avoids MLIR opaque struct problem.
// Used to size enum payload slots correctly before MLIR struct bodies are set.
uint64_t MLIRGenImpl::logos_abi_byte_size(TypeRef t,
                                           std::unordered_set<std::string>& seen) {
    if (!t) return 8;
    TypeRef tv{t};
    switch (tv.kind()) {
    case LogosType::Kind::Void:    return 0;
    case LogosType::Kind::Bool:    return 1;
    case LogosType::Kind::U8:
    case LogosType::Kind::I8:      return 1;
    case LogosType::Kind::I16:
    case LogosType::Kind::U16:     return 2;
    case LogosType::Kind::I24:
    case LogosType::Kind::U24:     return 3;
    case LogosType::Kind::I32:
    case LogosType::Kind::U32:
    case LogosType::Kind::F32:
    case LogosType::Kind::IntLit:  return 4;
    case LogosType::Kind::I56:
    case LogosType::Kind::U56:     return 7;
    case LogosType::Kind::I64:
    case LogosType::Kind::U64:
    case LogosType::Kind::F64:
    case LogosType::Kind::FloatLit:
    case LogosType::Kind::Ptr:
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
    case LogosType::Kind::FnPtr:
    case LogosType::Kind::TaggedPtr:    return 8;
    case LogosType::Kind::I128:
    case LogosType::Kind::U128:         return 16;
    case LogosType::Kind::Array:
        if (!tv.elem()) return 0;
        return tv.arr_size() * logos_abi_byte_size(tv.elem(), seen);
    // Fat pointers — two pointers wide. The Slice case used to be lumped
    // in with Ptr/Ref above and reported 8 bytes, which silently truncated
    // slices stored in enum variant payloads (e.g. Option<&[u8]>'s Some
    // arm only kept the .ptr field; .len was lost on extraction).
    case LogosType::Kind::Slice:
    case LogosType::Kind::Closure:
    case LogosType::Kind::TraitObject:  return 16;
    case LogosType::Kind::Tuple: {
        uint64_t offset = 0, max_align = 1;
        for (auto e : tv.tuple_elems()) {
            uint64_t esz = logos_abi_byte_size(e, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) offset = (offset + align - 1) & ~(align - 1);
            offset += esz;
            if (align > max_align) max_align = align;
        }
        return (offset + max_align - 1) & ~(max_align - 1);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        auto cname = concrete_struct_name(t);
        if (seen.count(cname)) return 8;  // cycle guard
        auto it = all_struct_defs_.find(cname);
        if (it == all_struct_defs_.end()) return 8;  // unknown — assume ptr size
        seen.insert(cname);
        const LStructDef* sd = it->second;
        uint64_t offset = 0, max_align = 1;
        for (auto& f : sd->fields) {
            uint64_t esz = logos_abi_byte_size(f.type, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) offset = (offset + align - 1) & ~(align - 1);
            offset += esz;
            if (align > max_align) max_align = align;
        }
        seen.erase(cname);
        return (offset + max_align - 1) & ~(max_align - 1);
    }
    case LogosType::Kind::Enum: {
        auto it = tagged_enums_.find(std::string(tv.enum_name()));
        if (it != tagged_enums_.end())
            return 4 + it->second.payload_bytes;  // disc + payload
        return 8;
    }
    default: return 8;
    }
}

void MLIRGenImpl::register_tagged_enum(const LEnumDef& ed) {
    if (tagged_enums_.count(ed.name)) return;
    TaggedEnumInfo info;
    info.name = ed.name;
    uint64_t max_bytes = 0;
    for (auto& v : ed.variants) {
        TaggedEnumInfo::VariantPayload vp;
        vp.disc = v.disc;
        uint64_t variant_bytes = 0;
        for (auto pt : v.payload_types) {
            if (TypeRef(pt).kind() == LogosType::Kind::Void) continue;  // () unit — no field
            auto ft = logos_to_mlir(pt);
            if (!ft) ft = builder_.getI32Type();
            vp.field_types.push_back(ft);
            vp.logos_types.push_back(pt);
            std::unordered_set<std::string> seen;
            variant_bytes += logos_abi_byte_size(pt, seen);
        }
        if (variant_bytes > max_bytes) max_bytes = variant_bytes;
        info.variants.push_back(std::move(vp));
    }
    info.payload_bytes = max_bytes;
    auto i32 = builder_.getI32Type();
    auto payload = mlir::LLVM::LLVMArrayType::get(
        builder_.getIntegerType(8), max_bytes > 0 ? max_bytes : 1);
    auto enum_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), "enum." + ed.name);
    (void)enum_type.setBody({i32, payload}, false);
    info.llvm_type = enum_type;
    tagged_enums_[ed.name] = std::move(info);
}


// ---------------------------------------------------------------------------
// resolve_tagged_enum, tuple_llvm_type, slice_llvm_type, closure_llvm_type
// ---------------------------------------------------------------------------

const TaggedEnumInfo* MLIRGenImpl::resolve_tagged_enum(const std::string& name,
                                                        TypeRef type) {
    auto tit = tagged_enums_.find(name);
    if (tit != tagged_enums_.end()) return &tit->second;
    // For generic enums: compute concrete name from type_args.
    // Must match the mangling used by mono's record_needed_enum:
    // struct/datatype args use concrete_struct_name(), others use type_str().
    if (type && TypeRef(type).kind() == LogosType::Kind::Enum && !TypeRef(type).type_args().empty()) {
        std::string cname = std::string(TypeRef(type).enum_name());
        for (auto a : TypeRef(type).type_args()) { cname += "__"; cname += Mono::mangle_type(a); }
        tit = tagged_enums_.find(cname);
        if (tit != tagged_enums_.end()) return &tit->second;
    }
    return nullptr;
}

mlir::Type MLIRGenImpl::tuple_llvm_type(TypeRef t) {
    if (!t || TypeRef(t).kind() != LogosType::Kind::Tuple) return nullptr;
    llvm::SmallVector<mlir::Type> fields;
    for (auto e : TypeRef(t).tuple_elems()) {
        auto ft = logos_to_mlir(e);
        if (!ft) return nullptr;
        fields.push_back(ft);
    }
    return mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), fields);
}

mlir::Type MLIRGenImpl::slice_llvm_type() {
    return mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), builder_.getI64Type()});
}

mlir::Type MLIRGenImpl::closure_llvm_type() {
    return mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});
}

} // namespace logos::compiler
