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
        // Enum value-repr: an array of TAGGED enums embeds each element inline
        // ({disc,payload}), so the element stride is the full enum footprint —
        // NOT a collapsed ptr (which would corrupt `arr[i]` indexing).
        if (elem_tv && elem_tv.kind() == LogosType::Kind::Enum) {
            if (auto* te = resolve_tagged_enum(std::string(elem_tv.enum_name()), elem_tv);
                te && te->llvm_type)
                return cache_ret(mlir::LLVM::LLVMArrayType::get(
                    te->llvm_type, tv.arr_size()));
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
        } else if (fv.kind() == LogosType::Kind::Enum &&
                   resolve_tagged_enum(std::string(fv.enum_name()), fv)) {
            // Inline-embed a TAGGED enum-typed field (enum value-repr): use the
            // registered `enum.NAME` aggregate so the field occupies its full
            // {disc,payload} footprint, mirroring the nested-struct inline-embed
            // branch above. A C-like enum (no TaggedEnumInfo) is an i32 disc and
            // falls through to the generic logos_to_mlir branch below.
            ft = resolve_tagged_enum(std::string(fv.enum_name()), fv)->llvm_type;
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
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
            // TraitObject node (no Ref wrapper). Value-fat-pair model: stored
            // INLINE as a 16-byte {data,vtable} pair (mirrors a slice field).
            ft = dyn_llvm_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {},
                                   std::string(fv.trait_name()),
                                   /*is_pointer=*/false});
            field_types.push_back(ft);
            continue;
        } else if ((fv.kind() == LogosType::Kind::Ptr ||
                    fv.kind() == LogosType::Kind::Ref ||
                    fv.kind() == LogosType::Kind::MutRef) &&
                   fv.pointee() &&
                   fv.pointee().kind() == LogosType::Kind::TraitObject) {
            // `&(&dyn)` / `*const dyn` / `*mut dyn` field (Ref/MutRef/Ptr over a
            // TraitObject) — these are genuine 8-byte THIN handles (a `&dyn`
            // FLATTENS to a bare TraitObject and hits the inline-16 branch
            // above; only an explicit pointer-to-trait-object lands here). The
            // persistent/Zone NodeARC.p path also relies on the thin word.
            ft = ptr_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {},
                                   std::string(fv.pointee().trait_name()),
                                   /*is_pointer=*/true});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::Slice) {
            // Slice field — fixed-size 16-byte fat pointer {data,len} (like Rust
            // `&[T]`). Stored INLINE by value, mirroring the TraitObject branch
            // above (and a slice value elsewhere is a pointer to this storage).
            ft = slice_llvm_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::Closure) {
            // Closure field — fixed-size 16-byte {fn,env} fat pair. Stored
            // INLINE by value (like a slice); a closure value elsewhere is a
            // pointer to this storage.
            ft = closure_llvm_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
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
// Aggregate (struct / tuple / enum) layout accumulator: appends fields in
// declaration order, inserting natural alignment padding, then rounds the
// total to the aggregate's own alignment. Matches LLVM's non-packed C layout.
namespace {
struct LayoutAgg {
    uint64_t offset = 0, align = 1;
    void push(MLIRGenImpl::Layout f) {
        if (f.align > 1) offset = (offset + f.align - 1) & ~(f.align - 1);
        offset += f.size;
        if (f.align > align) align = f.align;
    }
    MLIRGenImpl::Layout finish() const {
        return { (offset + align - 1) & ~(align - 1), align };
    }
};
}  // namespace

MLIRGenImpl::Layout MLIRGenImpl::aggregate_member_layout(
        TypeRef m, std::unordered_set<std::string>& seen) {
    using K = LogosType::Kind;
    if (!m) return {8, 8};
    switch (TypeRef(m).kind()) {
    // Stored as an 8-byte pointer inside an aggregate (logos_to_mlir → ptr),
    // unlike their 16-byte/inline by-value footprint.
    case K::Tuple: return {8, 8};
    // Slice and Closure fields are now stored INLINE as a 16-byte fat pair
    // ({ptr,len} / {fn,env}), so they count their by-value footprint like
    // dyn/struct.
    default: return layout_of(m, seen);  // inline (slice/closure/struct/enum/array/dyn/scalar)
    }
}

MLIRGenImpl::Layout MLIRGenImpl::layout_of(TypeRef t,
                                           std::unordered_set<std::string>& seen) {
    using K = LogosType::Kind;
    if (!t) return {8, 8};
    TypeRef tv{t};
    if (is_anyval(tv)) return {4, 4};  // AnyVal is lowered as i32 everywhere
    switch (tv.kind()) {
    case K::Void: case K::Never:                return {0, 1};  // zero-size
    case K::Bool: case K::I8: case K::U8:       return {1, 1};
    case K::I16:  case K::U16:                  return {2, 2};
    case K::I24:  case K::U24:                  return {3, 1};  // odd width, align 1
    case K::I32:  case K::U32: case K::F32: case K::IntLit:
    case K::Char:                               return {4, 4};
    case K::I56:  case K::U56:                  return {7, 1};
    case K::I64:  case K::U64: case K::F64: case K::FloatLit:
    case K::Ptr:  case K::Ref: case K::MutRef:
    case K::FnPtr: case K::TaggedPtr:
    case K::Usize: case K::Isize:               return {8, 8};
    case K::I128: case K::U128:                 return {16, 16};
    // Fat pointers — two pointers wide (ptr-aligned).
    case K::Slice: case K::Closure: case K::TraitObject: case K::DstRef:
        return {16, 8};
    // Unsized — no by-value footprint (sema/borrow-check reject by-value use).
    case K::UnsizedSlice: case K::UnsizedDyn:   return {0, 1};
    case K::Array: {
        if (!tv.elem()) return {0, 1};
        auto e = aggregate_member_layout(tv.elem(), seen);  // element repr in the array
        return { tv.arr_size() * e.size, e.align };
    }
    case K::Tuple: {
        LayoutAgg agg;
        for (auto e : tv.tuple_elems()) agg.push(aggregate_member_layout(e, seen));
        return agg.finish();
    }
    case K::Struct: case K::ZonedStruct: {
        auto cname = concrete_struct_name(t);
        if (!seen.insert(cname).second) return {8, 8};  // cycle guard
        Layout r{8, 8};
        if (auto it = all_struct_defs_.find(cname); it != all_struct_defs_.end()) {
            LayoutAgg agg;
            for (auto& f : it->second->fields) agg.push(aggregate_member_layout(f.type, seen));
            r = agg.finish();
        }
        seen.erase(cname);
        return r;
    }
    case K::Enum: {
        // Tagged enum value-repr = `{ i32 disc, <aligned payload blob> }` —
        // payload at offset round(4, payload_align). Resolve the CONCRETE
        // instantiation so nested generics (Option<Option<i64>>) size their
        // full inline footprint.
        if (auto* te = resolve_tagged_enum(std::string(tv.enum_name()), t)) {
            LayoutAgg agg;
            agg.push({4, 4});                                   // discriminant
            agg.push({te->payload_bytes, te->payload_align});   // aligned payload
            return agg.finish();
        }
        // C-like enum — backing-type-sized discriminant.
        uint64_t b = (enum_disc_bits(std::string(tv.enum_name())) + 7) / 8;
        return { b, b ? b : 1 };
    }
    default: return {8, 8};
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
            } else if (k == LogosType::Kind::Enum) {
                // Inline nested enum: embed its full {disc,payload} footprint
                // (enum value-repr) so a nested enum payload field occupies its
                // real ABI size, not a collapsed ptr.
                if (auto* nte = resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt))
                    if (nte->llvm_type) t = nte->llvm_type;
            }
        }
        ft.push_back(t);
    }
    return mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), ft);
}

// Aligned byte size of one variant's payload, laid out as a struct (matches
// variant_payload_struct's LLVM aggregate layout — inter-field alignment
// padding INCLUDED). A naive sum of field sizes under-counts a multi-field
// variant like `Cons { head: i32, tail: *const List }` (4+8=12 vs the real
// {i32,ptr} struct of 16) — which silently overlapped adjacent enum allocas.
MLIRGenImpl::Layout MLIRGenImpl::variant_payload_layout(const lir::LVariant& v) {
    // The payload is laid out exactly like a struct/tuple of its fields —
    // derive {size, align} from the unified layout accumulator.
    // Enum payloads store members BY VALUE (a slice/closure payload is the full
    // inline fat pair, e.g. Option<&[u8]> = {i32, [16 x i8]}), UNLIKE struct/
    // tuple fields which store slice/closure/tuple as an 8-byte ptr. So use the
    // by-value layout_of here, not aggregate_member_layout.
    LayoutAgg agg;
    std::unordered_set<std::string> seen;
    for (auto pt : v.payload_types) {
        if (TypeRef(pt).kind() == LogosType::Kind::Void) continue;
        agg.push(layout_of(pt, seen));
    }
    return agg.finish();
}

uint64_t MLIRGenImpl::variant_payload_bytes(const lir::LVariant& v) {
    return variant_payload_layout(v).size;
}

void MLIRGenImpl::register_tagged_enum(const LEnumDef& ed) {
    // Skip if fully populated already (variants filled in). Stub entries
    // (pre-registered by mlir_gen.cpp's two-pass loop) have empty variants
    // and need their bodies filled here.
    auto eit = tagged_enums_.find(ed.name);
    if (eit != tagged_enums_.end() && !eit->second.variants.empty()) return;
    TaggedEnumInfo info;
    info.name = ed.name;
    uint64_t max_bytes = 0, max_align = 1;
    for (auto& v : ed.variants) {
        TaggedEnumInfo::VariantPayload vp;
        vp.disc = v.disc;
        for (auto pt : v.payload_types) {
            if (TypeRef(pt).kind() == LogosType::Kind::Void) continue;  // () unit — no field
            auto ft = logos_to_mlir(pt);
            if (!ft) ft = builder_.getI32Type();
            vp.field_types.push_back(ft);
            vp.logos_types.push_back(pt);
        }
        auto pl = variant_payload_layout(v);
        if (pl.size  > max_bytes) max_bytes = pl.size;
        if (pl.align > max_align) max_align = pl.align;
        info.variants.push_back(std::move(vp));
    }
    info.payload_bytes = max_bytes;
    info.payload_align = max_align;
    auto enum_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), "enum." + ed.name);
    // NOTE: the body (payload byte-array size) is NOT set here — a nested enum
    // payload may still be a 0-byte stub at this point, so max_bytes can be
    // under-sized. The body is set ONCE, after the fixpoint in mlir_gen.cpp
    // recomputes every enum's final payload_bytes (finalize_enum_bodies).
    // An identified LLVM struct's body is set-once, so setting it prematurely
    // here would lock in the wrong size.
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
    // Deref a `&(T,U)` / `&mut (T,U)` / `*(T,U)` to the inner tuple so a tuple
    // pattern over a ref scrutinee resolves the layout (default binding modes).
    if (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
              TypeRef(t).kind() == LogosType::Kind::MutRef ||
              TypeRef(t).kind() == LogosType::Kind::Ptr) &&
        TypeRef(t).pointee() &&
        TypeRef(TypeRef(t).pointee()).kind() == LogosType::Kind::Tuple)
        t = TypeRef(t).pointee();
    if (!t || TypeRef(t).kind() != LogosType::Kind::Tuple) return nullptr;
    llvm::SmallVector<mlir::Type> fields;
    for (auto e : TypeRef(t).tuple_elems()) {
        // A struct-typed element is stored INLINE (the tuple literal stores the
        // whole struct value into the element slot — see gen codegen), exactly
        // like a struct-typed FIELD of a struct. `logos_to_mlir` lowers a Struct
        // to `ptr` (8B), which here would UNDER-size the slot vs the 24B inline
        // store → stack overflow + corrupted neighbour elements (a String
        // element clobbered the next i64). Embed the registered inline struct
        // type instead, mirroring the struct-field layout.
        mlir::Type ft;
        if (e && (TypeRef(e).kind() == LogosType::Kind::Struct ||
                  TypeRef(e).kind() == LogosType::Kind::ZonedStruct)) {
            auto cn = concrete_struct_name(e);
            if (auto sit = struct_types_.find(cn); sit != struct_types_.end())
                ft = sit->second.llvm_type;
        } else if (e && TypeRef(e).kind() == LogosType::Kind::Enum) {
            // Inline-embed an enum-typed tuple element (enum value-repr), like
            // a struct element — its full {disc,payload} footprint, not a ptr.
            if (auto* te = resolve_tagged_enum(std::string(TypeRef(e).enum_name()), e))
                if (te->llvm_type) ft = te->llvm_type;
        } else if (e && TypeRef(e).kind() == LogosType::Kind::Slice) {
            // Slice element (incl. `str` = Slice<u8>) — inline 16-byte {ptr,len}
            // fat pair (Rust `&[T]`), matching layout_of / the slice-field
            // convention, not an 8-byte ptr (which mismatched layout_of=16 and
            // corrupted the trailing elements).
            ft = slice_llvm_type();
        } else if (e && TypeRef(e).kind() == LogosType::Kind::Closure) {
            ft = closure_llvm_type();  // inline 16-byte {fn,env}
        }
        if (!ft) ft = logos_to_mlir(e);
        if (!ft) return nullptr;
        fields.push_back(ft);
    }
    return mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), fields);
}

mlir::Type MLIRGenImpl::slice_llvm_type() {
    return mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), builder_.getI64Type()});
}

mlir::Type MLIRGenImpl::dyn_llvm_type() {
    // A trait object fat pair: { data_ptr, vtable_ptr }, 16 bytes, value-repr
    // (mirrors slice_llvm_type's {ptr,len}). Stored inline in fields/elements;
    // a `&dyn` value is a pointer to this 16-byte storage (like a slice value).
    return mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});
}

mlir::Type MLIRGenImpl::closure_llvm_type() {
    return mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});
}

} // namespace logos::compiler
