// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_types.cpp — Type conversion, struct/enum/class registration.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// Type conversion: LogosType → mlir::Type
// ---------------------------------------------------------------------------

mlir::Type MLIRGenImpl::logos_to_mlir(const LogosType* t) {
    if (!t) return nullptr;
    if (type_str(t) == "AnyVal") return builder_.getI32Type();
    switch (t->kind) {
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
    case LogosType::Kind::IntLit:   return builder_.getI32Type();
    case LogosType::Kind::FloatLit: return builder_.getF64Type();
    case LogosType::Kind::Enum: {
        // Tagged enums are passed by pointer; C-style enums are i32.
        if (resolve_tagged_enum(t->enum_name, t)) return ptr_type();
        return builder_.getI32Type();
    }
    case LogosType::Kind::Ptr:    return ptr_type();
    case LogosType::Kind::Ref:    return ptr_type();  // &T — same layout as *const T
    case LogosType::Kind::MutRef: return ptr_type();  // &mut T — same layout as *mut T
    case LogosType::Kind::Array: {
        auto elem = logos_to_mlir(t->elem);
        if (!elem) return nullptr;
        return mlir::LLVM::LLVMArrayType::get(elem, t->arr_size);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::Datatype: {
        // Check type alias first.
        auto cname = concrete_struct_name(t);
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
        for (auto* e : t->tuple_elems) {
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
                     t->type_var_name.c_str());
        return nullptr;
    case LogosType::Kind::ConstVar:
        // ConstVar (e.g. N in [T; N]) should have been resolved by mono_pass.
        std::fprintf(stderr, "mlir_gen: unresolved ConstVar '%s' — mono_pass required\n",
                     t->type_var_name.c_str());
        return nullptr;
    case LogosType::Kind::AssocType: {
        // AssocType (T::Item) should have been resolved by mono_pass.
        std::string base_s = t->assoc_base ? type_str(t->assoc_base) : "<null>";
        std::fprintf(stderr,
                     "mlir_gen: unresolved AssocType '%s::%s::%s' — mono_pass required\n",
                     base_s.c_str(), t->trait_name.c_str(), t->assoc_type_name.c_str());
        return nullptr;
    }
    case LogosType::Kind::Error:     return nullptr;
    case LogosType::Kind::ImplTrait: return nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Struct registration (Pass 0)
// ---------------------------------------------------------------------------

bool MLIRGenImpl::register_struct(const LStructDef& sd) {
    if (struct_types_.count(sd.name)) return true;
    auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), sd.name);
    StructInfo info;
    info.name      = sd.name;
    info.llvm_type = struct_type;

    std::vector<mlir::Type> field_types;
    for (auto& f : sd.fields) {
        mlir::Type ft;
        std::string fsname;
        // Datatype fields are embedded by value (not by pointer).
        // Regular Struct fields with a registered llvm_type are also inline.
        if (f.type->kind == LogosType::Kind::Datatype ||
            f.type->kind == LogosType::Kind::Struct) {
            auto cname = concrete_struct_name(f.type);
            auto sit = struct_types_.find(cname);
            if (sit == struct_types_.end()) {
                // Not yet registered — try to register it now (resolve dependency order).
                auto def_it = all_struct_defs_.find(cname);
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
        } else if ((f.type->kind == LogosType::Kind::Ptr ||
                    f.type->kind == LogosType::Kind::Ref ||
                    f.type->kind == LogosType::Kind::MutRef) &&
                   f.type->pointee &&
                   (f.type->pointee->kind == LogosType::Kind::Struct ||
                    f.type->pointee->kind == LogosType::Kind::Datatype)) {
            // *Struct / &Struct / &mut Struct field — pointer to struct.
            // Set fsname so gen_recv_struct can chain field access through it.
            ft = ptr_type();
            fsname = concrete_struct_name(f.type->pointee);
        } else {
            ft = logos_to_mlir(f.type);
            if (!ft) {
                std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n", sd.name.c_str());
                return false;
            }
        }
        info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname});
        field_types.push_back(ft);
    }
    if (mlir::failed(struct_type.setBody(field_types, false))) {
        std::fprintf(stderr, "mlir_gen: failed to set struct body for '%s'\n", sd.name.c_str());
        return false;
    }
    struct_types_[sd.name] = std::move(info);
    return true;
}

// ---------------------------------------------------------------------------
// Tagged enum registration
// Layout: { i32 disc, [max_payload_bytes x i8] }
// ---------------------------------------------------------------------------

void MLIRGenImpl::register_tagged_enum(const LEnumDef& ed) {
    if (tagged_enums_.count(ed.name)) return;
    TaggedEnumInfo info;
    info.name = ed.name;
    uint64_t max_bytes = 0;
    for (auto& v : ed.variants) {
        TaggedEnumInfo::VariantPayload vp;
        vp.disc = v.disc;
        uint64_t variant_bytes = 0;
        for (auto* pt : v.payload_types) {
            if (pt->kind == LogosType::Kind::Void) continue;  // () unit — no field
            auto ft = logos_to_mlir(pt);
            if (!ft) ft = builder_.getI32Type();
            vp.field_types.push_back(ft);
            // Estimate size: i32=4, i64=8, ptr=8, bool=1, etc.
            if (ft.isInteger(1)) variant_bytes += 1;
            else if (ft.isInteger(8)) variant_bytes += 1;
            else if (ft.isInteger(32)) variant_bytes += 4;
            else if (ft.isInteger(64)) variant_bytes += 8;
            else if (ft == ptr_type()) variant_bytes += 8;
            else variant_bytes += 8; // default
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
                                                        const LogosType* type) {
    auto tit = tagged_enums_.find(name);
    if (tit != tagged_enums_.end()) return &tit->second;
    // For generic enums: compute concrete name from type_args
    if (type && type->kind == LogosType::Kind::Enum && !type->type_args.empty()) {
        std::string cname = type->enum_name;
        for (auto* a : type->type_args) { cname += "__"; cname += type_str(a); }
        tit = tagged_enums_.find(cname);
        if (tit != tagged_enums_.end()) return &tit->second;
    }
    return nullptr;
}

mlir::Type MLIRGenImpl::tuple_llvm_type(const LogosType* t) {
    if (!t || t->kind != LogosType::Kind::Tuple) return nullptr;
    llvm::SmallVector<mlir::Type> fields;
    for (auto* e : t->tuple_elems) {
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
