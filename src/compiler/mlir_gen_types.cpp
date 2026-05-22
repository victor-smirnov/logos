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
    // Cache check first — see logos_to_mlir_cache_ comment in
    // mlir_gen_impl.hpp. Struct/Array/Enum cases lazily register types
    // as a side effect; subsequent cache hits skip re-registration but
    // the registration itself is idempotent, so this is safe. Hit rate
    // is very high in forward_declare (same TypeRef appears across many
    // fn signatures: `&self`, common return types, etc).
    auto off = tv.offset();
    if (auto it = logos_to_mlir_cache_.find(off);
        it != logos_to_mlir_cache_.end())
        return it->second;
    auto cache_ret = [&](mlir::Type t) -> mlir::Type {
        if (t) logos_to_mlir_cache_[off] = t;
        return t;
    };
    if (is_anyval(tv)) return cache_ret(builder_.getI32Type());
    switch (tv.kind()) {
    case LogosType::Kind::Void:   return nullptr;
    // The never type yields no value — a diverging expression emits its own
    // terminator, so a Never-typed slot is never materialised (treat as void).
    case LogosType::Kind::Never:  return nullptr;
    case LogosType::Kind::I32:    return cache_ret(builder_.getI32Type());
    case LogosType::Kind::I64:    return cache_ret(builder_.getI64Type());
    case LogosType::Kind::F64:    return cache_ret(builder_.getF64Type());
    case LogosType::Kind::F32:    return cache_ret(builder_.getF32Type());
    case LogosType::Kind::Bool:   return cache_ret(builder_.getI1Type());
    case LogosType::Kind::U8:     return cache_ret(builder_.getIntegerType(8));
    case LogosType::Kind::I8:     return cache_ret(builder_.getIntegerType(8));
    case LogosType::Kind::I16:    return cache_ret(builder_.getIntegerType(16));
    case LogosType::Kind::U16:    return cache_ret(builder_.getIntegerType(16));
    case LogosType::Kind::I24:    return cache_ret(builder_.getIntegerType(24));
    case LogosType::Kind::U24:    return cache_ret(builder_.getIntegerType(24));
    case LogosType::Kind::I56:    return cache_ret(builder_.getIntegerType(56));
    case LogosType::Kind::U56:    return cache_ret(builder_.getIntegerType(56));
    case LogosType::Kind::U32:    return cache_ret(builder_.getIntegerType(32));
    case LogosType::Kind::U64:    return cache_ret(builder_.getIntegerType(64));
    case LogosType::Kind::I128:   return cache_ret(builder_.getIntegerType(128));
    case LogosType::Kind::U128:   return cache_ret(builder_.getIntegerType(128));
    case LogosType::Kind::Usize:  return cache_ret(builder_.getIntegerType(::logos::compiler::g_target_pointer_bits));
    case LogosType::Kind::Isize:  return cache_ret(builder_.getIntegerType(::logos::compiler::g_target_pointer_bits));
    case LogosType::Kind::Char:   return cache_ret(builder_.getI32Type());
    case LogosType::Kind::IntLit:   return cache_ret(builder_.getI32Type());
    case LogosType::Kind::FloatLit: return cache_ret(builder_.getF64Type());
    case LogosType::Kind::Enum: {
        if (resolve_tagged_enum(std::string(tv.enum_name()), tv))
            return cache_ret(ptr_type());
        return cache_ret(enum_disc_mlir(std::string(tv.enum_name())));
    }
    case LogosType::Kind::Ptr:    return cache_ret(ptr_type());
    case LogosType::Kind::Ref:    return cache_ret(ptr_type());
    case LogosType::Kind::MutRef: return cache_ret(ptr_type());
    case LogosType::Kind::Array: {
        TypeRef elem_tv = tv.elem();
        if (elem_tv && (elem_tv.kind() == LogosType::Kind::Struct ||
                        elem_tv.kind() == LogosType::Kind::ZonedStruct) &&
            !is_anyval(elem_tv)) {
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
                return cache_ret(mlir::LLVM::LLVMArrayType::get(
                    sit->second.llvm_type, tv.arr_size()));
        }
        auto elem = logos_to_mlir(elem_tv);
        if (!elem) return nullptr;
        return cache_ret(mlir::LLVM::LLVMArrayType::get(elem, tv.arr_size()));
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        auto cname = concrete_struct_name(tv);
        auto ait = type_aliases_.find(cname);
        if (ait != type_aliases_.end()) return cache_ret(ait->second);
        return cache_ret(ptr_type());
    }
    case LogosType::Kind::Closure:      return cache_ret(ptr_type());
    case LogosType::Kind::FnPtr:        return cache_ret(ptr_type());
    case LogosType::Kind::Slice:        return cache_ret(ptr_type());
    case LogosType::Kind::UnsizedSlice: return cache_ret(ptr_type());
    case LogosType::Kind::UnsizedDyn:   return cache_ret(ptr_type());
    case LogosType::Kind::DstRef:       return cache_ret(ptr_type());
    case LogosType::Kind::Tuple: {
        // Tuples are anonymous LLVM struct types, passed by pointer.
        // We discard the literal struct type here (return ptr_type) but
        // tuple_llvm_type() builds it on demand for return-by-value.
        llvm::SmallVector<mlir::Type> fields;
        for (auto e : tv.tuple_elems()) {
            auto ft = logos_to_mlir(e);
            if (!ft) return nullptr;
            fields.push_back(ft);
        }
        return cache_ret(ptr_type());
    }
    case LogosType::Kind::TaggedPtr:    return cache_ret(ptr_type());
    case LogosType::Kind::TraitObject:  return cache_ret(ptr_type());
    case LogosType::Kind::TypeVar:
        std::fprintf(stderr, "mlir_gen: unresolved TypeVar '%s' — mono_pass required\n",
                     std::string(tv.type_var_name()).c_str());
        return nullptr;
    case LogosType::Kind::ConstVar:
        std::fprintf(stderr, "mlir_gen: unresolved ConstVar '%s' — mono_pass required\n",
                     std::string(tv.type_var_name()).c_str());
        return nullptr;
    case LogosType::Kind::AssocType: {
        std::string base_s = tv.assoc_base() ? type_str(tv.assoc_base()) : "<null>";
        std::fprintf(stderr,
                     "mlir_gen: unresolved AssocType '%s::%s::%s' — mono_pass required\n",
                     base_s.c_str(), std::string(tv.trait_name()).c_str(), std::string(tv.assoc_type_name()).c_str());
        return nullptr;
    }
    case LogosType::Kind::Error:       return nullptr;
    case LogosType::Kind::ImplTrait:   return nullptr;
    case LogosType::Kind::Generic:     return nullptr;
    case LogosType::Kind::HStaticLit:  return nullptr;
    case LogosType::Kind::CfgSlotType: return nullptr;
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
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
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
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname, {}, /*is_pointer=*/true});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::TraitObject) {
            // Bare `&dyn Trait` field — sema may flatten `&dyn Trait` to a single
            // TraitObject node (no Ref wrapper). Storage is an 8-byte handle.
            ft = ptr_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {},
                                   std::string(fv.trait_name()),
                                   /*is_pointer=*/true});
            field_types.push_back(ft);
            continue;
        } else if ((fv.kind() == LogosType::Kind::Ptr ||
                    fv.kind() == LogosType::Kind::Ref ||
                    fv.kind() == LogosType::Kind::MutRef) &&
                   fv.pointee() &&
                   fv.pointee().kind() == LogosType::Kind::TraitObject) {
            // &dyn Trait / *const dyn Trait / *mut dyn Trait field — handle is
            // an 8-byte ptr to a heap-allocated {data,vtable} fat slot. Record
            // trait_name so struct-lit init can fat-pointer-coerce a concrete
            // `&T` value before storing into the field.
            ft = ptr_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {},
                                   std::string(fv.pointee().trait_name()),
                                   /*is_pointer=*/true});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::Never) {
            // A `!`-typed field (e.g. the Err payload of an infallible
            // `Result<T, !>`, or a never type-arg flowing into stdlib iterator
            // machinery) is uninhabited — no value ever exists, so the field is
            // never read. Give it a genuinely zero-size representation
            // (`array<0 x i8>`) so the layout is valid; logos_to_mlir(Never)
            // stays nullptr for value/result contexts (if/match diverging
            // branches rely on that).
            ft = mlir::LLVM::LLVMArrayType::get(builder_.getI8Type(), 0);
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
        } else {
            ft = logos_to_mlir(f.type);
            if (!ft) {
                std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n", sd.name.c_str());
                return false;
            }
        }
        info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname, {}, false});
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
    case LogosType::Kind::Never:   return 0;  // uninhabited — zero-size
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
    case LogosType::Kind::TraitObject:
    case LogosType::Kind::DstRef:       return 16;  // Phase 1B-14: same fat-pair size as Slice
    case LogosType::Kind::UnsizedSlice:
    case LogosType::Kind::UnsizedDyn:
        // Phase 1B: unsized — has no by-value ABI size. Report 0 so any
        // accidental layout query produces an obvious zero-size payload
        // (rather than corrupting an aggregate). Borrow check + sema
        // reject unsized values from positions where size matters.
        return 0;
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

mlir::LLVM::LLVMStructType MLIRGenImpl::variant_payload_struct(
        const TaggedEnumInfo::VariantPayload& vp) {
    llvm::SmallVector<mlir::Type> ft;
    for (size_t i = 0; i < vp.field_types.size(); ++i) {
        TypeRef lt = i < vp.logos_types.size() ? vp.logos_types[i] : TypeRef{};
        mlir::Type t = vp.field_types[i];
        if (lt) {
            auto k = TypeRef(lt).kind();
            if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct) {
                // Inline struct: use the identified struct type so the field
                // occupies its full ABI footprint, not a collapsed ptr.
                auto sit = struct_types_.find(mlir_struct_key(lt));
                if (sit == struct_types_.end())
                    sit = struct_types_.find(std::string(TypeRef(lt).struct_name()));
                if (sit != struct_types_.end() && sit->second.llvm_type)
                    t = sit->second.llvm_type;
            } else if (k == LogosType::Kind::Tuple) {
                if (auto tt = tuple_llvm_type(lt)) t = tt;
            }
        }
        ft.push_back(t);
    }
    return mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), ft);
}

void MLIRGenImpl::register_tagged_enum(const LEnumDef& ed) {
    // Skip if fully populated already (variants filled in). Stub entries
    // (pre-registered by mlir_gen.cpp's two-pass loop) have empty variants
    // and need their bodies filled here.
    auto eit = tagged_enums_.find(ed.name);
    if (eit != tagged_enums_.end() && !eit->second.variants.empty()) return;
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
