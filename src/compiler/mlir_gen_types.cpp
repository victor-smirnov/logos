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
    // RefRepr (Phase 1): reference-like kinds get their VALUE (SSA) type from the
    // repr registry — uniformly a thin pointer today (the fat {data,meta} pair
    // lives in storage; the value is a pointer to it). Behavior-identical to the
    // per-kind `ptr_type()` cases below.
    if (auto rk = ref_repr_of(tv); rk != RefReprKind::NotARef)
        return cache_ret(repr_value_type(rk));
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
        // Slice (incl. str) / Closure element — inline 16-byte fat pair, matching
        // layout_of (a collapsed 8-byte ptr would mismatch sizeof → memcpy of a
        // `[str; N]` would overflow). Mirrors the struct/enum element inlining.
        if (elem_tv && elem_tv.kind() == LogosType::Kind::Tuple)
            if (auto tt = tuple_llvm_type(elem_tv))
                return cache_ret(mlir::LLVM::LLVMArrayType::get(tt, tv.arr_size()));
        if (elem_tv && elem_tv.kind() == LogosType::Kind::Slice)
            return cache_ret(mlir::LLVM::LLVMArrayType::get(slice_llvm_type(), tv.arr_size()));
        if (elem_tv && elem_tv.kind() == LogosType::Kind::Closure)
            return cache_ret(mlir::LLVM::LLVMArrayType::get(closure_llvm_type(), tv.arr_size()));
        // Bare `&dyn`/`*dyn`/`dyn` (TraitObject) ARRAY elements: inline 16-byte
        // {data,vtable} fat pairs (uniform fat model — matches layout_of=16 and
        // the slice/closure element inlining above). A collapsed 8-byte ptr would
        // mismatch sizeof, so vec_from_arr's memcpy would overflow / alias.
        if (elem_tv && elem_tv.kind() == LogosType::Kind::TraitObject)
            return cache_ret(mlir::LLVM::LLVMArrayType::get(dyn_llvm_type(), tv.arr_size()));
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
    case LogosType::Kind::FnItem:
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
            ft = repr_storage_type(ref_repr_of(fv));  // = dyn_llvm_type() (RefRepr Phase 1)
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
        } else if (fv.kind() == LogosType::Kind::Tuple) {
            // Tuple field — embed the anonymous tuple aggregate INLINE by value
            // (Rust layout), mirroring the nested-struct inline-embed above. A
            // tuple value elsewhere is a pointer to this storage, so a field read
            // returns the embedded slot address (like a nested struct).
            ft = tuple_llvm_type(fv);
            if (!ft) ft = ptr_type();
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::Slice) {
            // Slice field — fixed-size 16-byte fat pointer {data,len} (like Rust
            // `&[T]`). Stored INLINE by value, mirroring the TraitObject branch
            // above (and a slice value elsewhere is a pointer to this storage).
            ft = repr_storage_type(ref_repr_of(fv));  // = slice_llvm_type() (RefRepr Phase 1)
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::DstRef) {
            // Custom-DST fat-pointer field — 16-byte {data, len-or-vtable} pair,
            // stored INLINE by value exactly like a Slice/TraitObject field (a
            // DstRef value elsewhere is a pointer to this 16-byte storage).
            // Inline is REQUIRED for an owning `Rc<dyn>` = {inner: fat} — an
            // 8-byte ptr-to-fat would dangle when the Rc moves.
            ft = repr_storage_type(ref_repr_of(fv));  // = slice_llvm_type() (RefRepr Phase 1)
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), {}, {}, false});
            field_types.push_back(ft);
            continue;
        } else if (fv.kind() == LogosType::Kind::Closure) {
            // Closure field — fixed-size 16-byte {fn,env} fat pair. Stored
            // INLINE by value (like a slice); a closure value elsewhere is a
            // pointer to this storage.
            ft = repr_storage_type(ref_repr_of(fv));  // = closure_llvm_type() (RefRepr Phase 1)
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
        } else if (fv.kind() == LogosType::Kind::Void) {
            // §1 Wave 9 — unit type `()` as a struct field. Zero-sized; same
            // layout as a Never-field (`[i8; 0]`) so the struct's other
            // fields keep their offsets. `logos_to_mlir(())` is nullptr in
            // value/result contexts; treat field-position separately.
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
    // §6.1: union layout per Rust `items.union.common-storage` —
    // size = max(field sizes), align = max(field aligns). LLVM body
    // = `{ <max-aligned field type>, [pad x i8] }` so the struct's
    // own alignment = max-align and total raw size = max-size. All
    // fields share GEP index 0 (they overlap at offset 0).
    // Mismatching field types at access time bitcast via the load's
    // declared type.
    if (sd.is_union && !field_types.empty()) {
        size_t max_al_idx = 0;
        uint64_t max_sz = 0, max_al = 1;
        std::vector<uint64_t> sizes(sd.fields.size(), 0);
        std::vector<uint64_t> aligns(sd.fields.size(), 1);
        for (size_t i = 0; i < sd.fields.size(); ++i) {
            std::unordered_set<std::string> seen;
            auto fl = layout_of(sd.fields[i].type, seen);
            sizes[i] = fl.size; aligns[i] = fl.align;
            if (fl.size > max_sz) max_sz = fl.size;
            if (fl.align > max_al) { max_al = fl.align; max_al_idx = i; }
        }
        std::vector<mlir::Type> body{field_types[max_al_idx]};
        uint64_t pad = (max_sz > sizes[max_al_idx])
                           ? (max_sz - sizes[max_al_idx]) : 0;
        if (pad > 0) {
            body.push_back(mlir::LLVM::LLVMArrayType::get(
                builder_.getI8Type(), pad));
        }
        if (mlir::failed(struct_type.setBody(body, false))) {
            std::fprintf(stderr, "mlir_gen: failed to set union body for '%s'\n", key.c_str());
            return false;
        }
        for (auto& finfo : info.fields) finfo.index = 0;
    } else if (mlir::failed(struct_type.setBody(field_types, false))) {
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
    (void)seen;
    if (!m) return {8, 8};
    // All aggregate members are now stored INLINE by value (Rust layout):
    // struct/enum/slice/closure/dyn/array AND tuples. A nested tuple field
    // occupies its full by-value footprint, not a collapsed 8-byte ptr.
    return layout_of(m, seen);
}

MLIRGenImpl::Layout MLIRGenImpl::layout_of(TypeRef t,
                                           std::unordered_set<std::string>& seen) {
    using K = LogosType::Kind;
    if (!t) return {8, 8};
    TypeRef tv{t};
    if (is_anyval(tv)) return {4, 4};  // AnyVal is lowered as i32 everywhere
    // RefRepr (Phase 1): reference-like kinds get their {size,align} from the
    // repr registry. Behavior-identical to the per-kind cases below (thin {8,8},
    // fat {16,8}); the duplicate cases stay as a cross-check until all storage
    // sites are migrated.
    if (auto rk = ref_repr_of(tv); rk != RefReprKind::NotARef)
        return repr_storage_layout(rk);
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
            // logos-core 1.5: `#[repr(transparent)]` — single-field wrapper
            // inherits its field's layout exactly (no aggregate padding).
            // `NonZeroI64`-style wrappers at FFI boundaries depend on this.
            // The single-field invariant is enforced by sema_collect at
            // collect time (errors on multi-field `#[repr(transparent)]`),
            // so we can trust fields.size() == 1 here.
            if (it->second->repr_transparent && it->second->fields.size() == 1) {
                r = aggregate_member_layout(it->second->fields[0].type, seen);
            } else if (it->second->is_union) {
                // §6.1: union layout per Rust spec
                // `items.union.common-storage` — size = max(field
                // sizes), align = max(field aligns), rounded up.
                uint64_t max_sz = 0, max_al = 1;
                for (auto& f : it->second->fields) {
                    auto fl = aggregate_member_layout(f.type, seen);
                    if (fl.size > max_sz) max_sz = fl.size;
                    if (fl.align > max_al) max_al = fl.align;
                }
                r = { (max_sz + max_al - 1) & ~(max_al - 1), max_al };
            } else {
                LayoutAgg agg;
                for (auto& f : it->second->fields) agg.push(aggregate_member_layout(f.type, seen));
                r = agg.finish();
            }
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
            // Phase 3.5: a niche-packed enum is just its payload (no disc word).
            if (te->niche.packed)
                return { te->payload_bytes, te->payload_align };
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

// ── RefRepr registry (Phase 0 scaffold — dead code, not yet routed) ──────────
// Each method reproduces the CURRENT per-kind behavior, consolidated in one
// place. Phase 1+ migrate the scattered codegen sites to dispatch through these.
// See docs/internals/ref-repr-design.md.

MLIRGenImpl::RefReprKind MLIRGenImpl::ref_repr_of(TypeRef t) {
    if (!t) return RefReprKind::NotARef;
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        // A raw/safe pointer is thin even when its pointee is unsized at the
        // type level (e.g. `*const dyn` collapses to a TraitObject elsewhere);
        // classification is by the OUTER kind, matching today's field layout
        // (a Ptr-to-TraitObject field is an 8B thin slot; a bare TraitObject
        // field is the 16B inline fat pair).
        // `&mut T` to a #[zone_mut] type is a FAT ref {data, zone=*mut Allocator}
        // carrying its Hermes zone (the allocator rides the &mut → grow methods
        // reach it from &mut self). Shared `&T` / `*T` stay thin (read never grows).
        case K::MutRef: {
            TypeRef p = TypeRef(t).pointee();
            if (p && (p.kind() == K::Struct || p.kind() == K::ZonedStruct)) {
                auto it = all_struct_defs_.find(concrete_struct_name(p));
                if (it == all_struct_defs_.end())
                    it = all_struct_defs_.find(std::string(p.struct_name()));
                if (it != all_struct_defs_.end() && it->second && it->second->zone_mut)
                    return RefReprKind::FatZoneMut;
            }
            return RefReprKind::ThinPtr;
        }
        case K::Ptr: case K::Ref:
        case K::FnPtr: case K::FnItem:           return RefReprKind::ThinPtr;
        case K::Slice:                           return RefReprKind::FatSlice;
        case K::TraitObject:                     return RefReprKind::FatDyn;
        case K::Closure:                         return RefReprKind::FatClosure;
        // A #[self_describing] DstRef is physically THIN (8B ptr straight to the
        // header; tail length in-band via dst_len) — not a 16B {data,len} pair.
        // This is what lets a `&Foo` to it be RETURNED safely (no stack-local
        // metadata pair to dangle).
        case K::DstRef:
            return dstref_pointee_self_describing(t) ? RefReprKind::ThinPtr
                                                     : RefReprKind::FatCustomDst;
        // `#[rel_ptr]` struct → self-relative pointer (8B i64 offset storage,
        // absolute thin ptr compute). Classify by the struct def's flag.
        case K::Struct: case K::ZonedStruct: {
            auto it = all_struct_defs_.find(concrete_struct_name(t));
            if (it == all_struct_defs_.end())
                it = all_struct_defs_.find(std::string(TypeRef(t).struct_name()));
            if (it != all_struct_defs_.end() && it->second && it->second->rel_ptr)
                return RefReprKind::RelOffset;
            return RefReprKind::NotARef;
        }
        // UnsizedSlice (`[T]`) / UnsizedDyn (`dyn`) are unsized POINTEES, not
        // references — they have no by-value footprint ({0,1}); not RefReprs.
        default:                                 return RefReprKind::NotARef;
    }
}

MLIRGenImpl::RefReprKind MLIRGenImpl::field_repr(const std::string& owner_key, TypeRef field_type) {
    auto r = ref_repr_of(field_type);
    // A thin pointer field of a #[zoned2] struct is stored self-relative.
    if (r == RefReprKind::ThinPtr) {
        auto it = all_struct_defs_.find(owner_key);
        if (it != all_struct_defs_.end() && it->second && it->second->zoned2)
            return RefReprKind::RelOffset;
    }
    return r;
}

mlir::Type MLIRGenImpl::repr_value_type(RefReprKind k) {
    // Current model: every reference value is a thin pointer (logos_to_mlir
    // returns ptr_type() for all reference kinds). The fat {data,meta} pair
    // lives in storage; the value is a pointer to it.
    if (k == RefReprKind::NotARef) return nullptr;
    return ptr_type();
}

mlir::Type MLIRGenImpl::repr_storage_type(RefReprKind k) {
    // The in-field/in-element slot type (mirrors register_struct's field branch).
    switch (k) {
        case RefReprKind::ThinPtr:      return ptr_type();
        case RefReprKind::FatSlice:     return slice_llvm_type();
        case RefReprKind::FatDyn:       return dyn_llvm_type();
        case RefReprKind::FatClosure:   return closure_llvm_type();
        case RefReprKind::FatCustomDst: return slice_llvm_type();
        case RefReprKind::FatZoneMut:   return slice_llvm_type();  // {data, zone} 16B
        case RefReprKind::RelOffset:    return builder_.getI64Type();  // 8B offset
        case RefReprKind::NotARef:      return nullptr;
    }
    return nullptr;
}

MLIRGenImpl::Layout MLIRGenImpl::repr_storage_layout(RefReprKind k) {
    // Mirrors layout_of: thin pointer {8,8}; fat pair {16,8}.
    switch (k) {
        case RefReprKind::ThinPtr:      return {8, 8};
        case RefReprKind::FatSlice:
        case RefReprKind::FatDyn:
        case RefReprKind::FatClosure:
        case RefReprKind::FatCustomDst:
        case RefReprKind::FatZoneMut:   return {16, 8};
        case RefReprKind::RelOffset:    return {8, 8};   // i64 offset
        case RefReprKind::NotARef:      return {0, 1};
    }
    return {0, 1};
}

mlir::Type MLIRGenImpl::repr_return_type(RefReprKind k) {
    // The by-VALUE return ABI. dyn/slice are materialized as their 16B storage
    // pair in the caller's frame (return-by-value leak fix); closure/custom-DST
    // are returned as the 8B value pointer (their fat storage is not return-
    // materialized — matches the pre-RefRepr behavior where these fell through
    // to logos_to_mlir = ptr). Thin → 8B value.
    switch (k) {
        case RefReprKind::FatDyn:
        case RefReprKind::FatSlice:
        case RefReprKind::FatZoneMut:   return repr_storage_type(k);  // 16B by value
        case RefReprKind::FatClosure:
        case RefReprKind::FatCustomDst:
        case RefReprKind::ThinPtr:
        case RefReprKind::RelOffset:    return repr_value_type(k);    // 8B ptr value
        case RefReprKind::NotARef:      return nullptr;
    }
    return nullptr;
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
            } else if (auto rk = ref_repr_of(lt); rk != RefReprKind::NotARef &&
                                                  rk != RefReprKind::ThinPtr) {
                // A fat reference payload (`&dyn`/`dyn`/`Box<dyn>`, slice, closure,
                // custom-DST ref) is stored INLINE as its 16-byte fat pair
                // (uniform fat model), not a collapsed 8-byte ptr, so it lives in
                // the enum value (no heap handle, no leak). RefRepr (Phase 1): the
                // payload slot IS the reference's storage type. Thin refs (ptr/
                // ref/fn) keep the by-value ptr from `t` above — excluded here.
                t = repr_storage_type(rk);
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
    // Phase 3.5 — null-pointer niche eligibility (Option<&T>-shape): exactly two
    // variants, one fieldless (the niche/`none` variant) and one with a single
    // non-null pointer field (`&T`/`&mut T`). The discriminant is then encoded
    // as null vs non-null in that pointer — no separate disc word, so the enum
    // is pointer-sized (sizeof(Option<&T>) == 8). Only `&`/`&mut` are
    // guaranteed-non-null today; Box/Rc/NonZero niches can follow.
    if (info.variants.size() == 2) {
        using K = LogosType::Kind;
        const TaggedEnumInfo::VariantPayload* none_v = nullptr;
        const TaggedEnumInfo::VariantPayload* some_v = nullptr;
        for (auto& vp : info.variants) {
            if (vp.field_types.empty()) none_v = &vp;
            else if (vp.logos_types.size() == 1) some_v = &vp;
        }
        // (1) Null-pointer niche — Option<&T>-shape (one fieldless + one &T/&mut T).
        if (none_v && some_v) {
            auto k = TypeRef(some_v->logos_types[0]).kind();
            if (k == K::Ref || k == K::MutRef) {
                info.niche.kind      = TaggedEnumInfo::Niche::NullPtr;
                info.niche.packed    = true;
                info.niche.none_disc = none_v->disc;
                info.niche.some_disc = some_v->disc;
            }
        }
        // (2) Low-bit niche — two single-field data arms, one a pointer to an
        // align≥2 pointee (low bit always 0), one a ≤63-bit integer (stored
        // low-bit-1 via a compiler tag). Packs into ONE word; disc = the low bit.
        auto int_arm_bits = [](TypeRef t, uint32_t& bits, bool& sgn) -> bool {
            switch (TypeRef(t).kind()) {
                case K::Bool: bits=1;  sgn=false; return true;
                case K::I8:   bits=8;  sgn=true;  return true;
                case K::U8:   bits=8;  sgn=false; return true;
                case K::I16:  bits=16; sgn=true;  return true;
                case K::U16:  bits=16; sgn=false; return true;
                case K::I24:  bits=24; sgn=true;  return true;
                case K::U24:  bits=24; sgn=false; return true;
                case K::I32:  bits=32; sgn=true;  return true;
                case K::U32:  bits=32; sgn=false; return true;
                case K::I56:  bits=56; sgn=true;  return true;
                default: return false;  // I64/U64 (64 bits) don't fit the niche
            }
        };
        if (!info.niche.packed) {
            const TaggedEnumInfo::VariantPayload* ptr_arm = nullptr;
            const TaggedEnumInfo::VariantPayload* val_arm = nullptr;
            uint32_t vbits = 0; bool vsigned = false; bool ok = true;
            for (auto& vp : info.variants) {
                if (vp.logos_types.size() != 1) { ok = false; break; }
                TypeRef ft = vp.logos_types[0];
                auto k = TypeRef(ft).kind();
                // Only &T/&mut T guarantee the pointer is aligned (low bit 0); a raw
                // `*T` could hold a misaligned address. (Zoned pointers — also
                // low-bit-0 — join here in F3.)
                bool is_ptr = (k == K::Ref || k == K::MutRef) &&
                              TypeRef(ft).pointee() &&
                              layout_of(TypeRef(ft).pointee()).align >= 2;
                uint32_t b = 0; bool s = false;
                bool is_val = int_arm_bits(ft, b, s) && b <= 63;
                if (is_ptr && !ptr_arm)      ptr_arm = &vp;
                else if (is_val && !val_arm) { val_arm = &vp; vbits = b; vsigned = s; }
                else { ok = false; break; }
            }
            if (ok && ptr_arm && val_arm) {
                info.niche.kind       = TaggedEnumInfo::Niche::LowBit;
                info.niche.packed     = true;
                info.niche.ptr_disc   = ptr_arm->disc;
                info.niche.val_disc   = val_arm->disc;
                info.niche.val_bits   = vbits;
                info.niche.val_signed = vsigned;
            }
        }
    }
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
        } else if (e && TypeRef(e).kind() == LogosType::Kind::TraitObject) {
            // Bare `&dyn` element — inline 16-byte {data,vtable} value fat-pair
            // (matches the struct-field convention + layout_of=16). `*mut dyn`
            // = Ptr<TraitObject> stays a thin 8-byte handle (logos_to_mlir).
            ft = dyn_llvm_type();
        } else if (e && TypeRef(e).kind() == LogosType::Kind::Tuple) {
            // Nested tuple element — embed its aggregate INLINE (Rust by-value
            // layout), like a nested struct element; logos_to_mlir would collapse
            // it to an 8-byte ptr and under-size the slot.
            ft = tuple_llvm_type(e);
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
