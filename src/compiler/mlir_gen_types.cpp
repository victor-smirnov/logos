// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_types.cpp — Type conversion, struct/enum/class registration.

#include "mlir_gen_impl.hpp"
#include <set>
#include <map>
#include <cstring>
#include "mono_impl.hpp"
#include "compile_pipeline.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/ADT/DenseMap.h>

#include <algorithm>

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
        return cache_ret(enum_disc_mlir(std::string(tv.enum_name()), tv));
    }
    case LogosType::Kind::Ptr:    return cache_ret(ptr_type());
    case LogosType::Kind::Ref:    return cache_ret(ptr_type());
    case LogosType::Kind::MutRef: return cache_ret(ptr_type());
    case LogosType::Kind::Array: {
        // An array whose length is still a NAME has not been bound by
        // mono_subst. Lowering it would emit `[0 x T]` — a zero-sized field
        // that every index reads past, compiled without a word of complaint.
        // Emission is the one place where this is unambiguously wrong (all
        // substitution is behind us), so it is fatal HERE rather than silent
        // everywhere.
        // A NAME or a deferred const-length EXPRESSION (both ride arr_size_var)
        // that survives to emission was never bound — it would lower to a
        // zero-length array. All substitution is behind us, so this is fatal
        // HERE rather than a silent [0 x T].
        if (!tv.arr_size_var().empty() && tv.arr_size() == 0) {
            llvm::report_fatal_error(llvm::StringRef(std::string(
                "array length '" + std::string(tv.arr_size_var()) +
                "' was never bound to a value; it would lower to a zero-length "
                "array. Expected a module-level const or a bound const-generic "
                "parameter.")));
        }
        TypeRef elem_tv = tv.elem();
        if (elem_tv && (elem_tv.kind() == LogosType::Kind::Struct ||
                        elem_tv.kind() == LogosType::Kind::ZonedStruct) &&
            !is_anyval(elem_tv)) {
            // PKG-QUALIFIED lookup (find_struct_it) so `[Item; N]` of a user
            // struct doesn't alias an imported same-named struct's layout.
            auto cname = concrete_struct_name(elem_tv);
            auto sit   = find_struct_it(elem_tv);
            if (sit == struct_types_.end()) {
                auto def_it = all_struct_defs_.find(cname);
                if (def_it != all_struct_defs_.end()) {
                    register_struct(def_it->second);
                    sit = find_struct_it(elem_tv);
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
    case LogosType::Kind::InferredType:
        std::fprintf(stderr, "mlir_gen: unresolved InferredType '_' — sema/mono_pass required\n");
        return nullptr;
    case LogosType::Kind::Error:       return nullptr;
    case LogosType::Kind::ImplTrait:   return nullptr;
    case LogosType::Kind::Generic:     return nullptr;
    case LogosType::Kind::WStaticLit:  return nullptr;
    case LogosType::Kind::CfgSlotType: return nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Struct registration (Pass 0)
// ---------------------------------------------------------------------------

bool MLIRGenImpl::register_struct(lir_view::StructView sd) {
    const TypePoolImpl* rs_pool = pool_impl();
    std::string sd_name(sd.name()), sd_pkg(sd.pkg());
    std::string key = qualify_pkg(sd_pkg, sd_name);
    if (struct_types_.count(key)) return true;
    auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), key);
    StructInfo info;
    info.name      = key;
    info.llvm_type = struct_type;

    std::vector<mlir::Type> field_types;
    for (auto fld : sd.fields()) {
        struct { std::string name; TypeRef type; } f{ std::string(fld.name()), fld.type(rs_pool) };
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
                    register_struct(def_it->second);
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
                std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n", sd_name.c_str());
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
    if (sd.is_union() && !field_types.empty()) {
        auto sd_fields = sd.fields();
        size_t max_al_idx = 0;
        uint64_t max_sz = 0, max_al = 1;
        std::vector<uint64_t> sizes(sd_fields.size(), 0);
        std::vector<uint64_t> aligns(sd_fields.size(), 1);
        for (size_t i = 0; i < sd_fields.size(); ++i) {
            std::unordered_set<std::string> seen;
            auto fl = layout_of(sd_fields[i].type(rs_pool), seen);
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
    if (!sd_pkg.empty() && !struct_types_.count(sd_name))
        struct_types_[sd_name] = info;
    // Coexistence + G156-1: mlir_struct_key = qualify_pkg(pkg, concrete_struct_name),
    // and concrete_struct_name carries the folded "$M<...>" suffix ("$M<module_id>"
    // for a module type, or the ambiguous-name package fingerprint). Register
    // matching aliases — the pkg-qualified form (the actual lookup key) and the
    // bare folded form (concrete_struct_name-only paths).
    std::string msuffix = type_module_suffix(sd_name, sd_pkg);
    if (!msuffix.empty()) {
        std::string qbare = sd_name + msuffix;
        std::string qkey  = qualify_pkg(sd_pkg, qbare);
        if (!struct_types_.count(qkey))  struct_types_[qkey]  = info;
        if (!struct_types_.count(qbare)) struct_types_[qbare] = info;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tagged enum registration
// Layout: { i32 disc, [max_payload_bytes x i8] }
// ---------------------------------------------------------------------------

// Compute ABI byte size from LogosType — avoids MLIR opaque struct problem.
// Used to size enum payload slots correctly before MLIR struct bodies are set.
// The aggregate accumulator, the union rule and the enum rule are ONE copy, in
// layout_law.hpp, asked here and by sema and by mono. This engine used to own
// the only correct version of all three; the other two were missing branches.
namespace lay = logos::compiler::layout;
using LayoutAgg = lay::Agg;

MLIRGenImpl::Layout MLIRGenImpl::aggregate_member_layout(
        TypeRef m, std::unordered_set<std::string>& seen) {
    (void)seen;
    if (!m) return {8, 8};
    // All aggregate members are now stored INLINE by value (Rust layout):
    // struct/enum/slice/closure/dyn/array AND tuples. A nested tuple field
    // occupies its full by-value footprint, not a collapsed 8-byte ptr.
    return layout_of(m, seen);
}

// The layout of a struct FROM ITS DEFINITION. Split out of `layout_of`'s
// Struct case so `verify_layout_engines` can ask the TypeRef engine the same
// question it asks the MLIR-type engine and the backend, keyed by the struct
// def rather than by a TypeRef it would have to synthesise. Same code, one
// caller more — not a copy.
MLIRGenImpl::Layout MLIRGenImpl::struct_def_layout(
        lir_view::StructView sv, std::unordered_set<std::string>& seen) {
    const TypePoolImpl* lo_pool = pool_impl();
    auto sv_fields = sv.fields();
    // THE LAW DECIDES WHICH SHAPE THIS IS. `#[repr(transparent)]`'s
    // single-field invariant is enforced by sema_collect at collect time, and
    // `agg_shape` re-states it as a condition rather than as an assumption.
    std::vector<lay::L> ml;
    ml.reserve(sv_fields.size());
    for (auto f : sv_fields) ml.push_back(aggregate_member_layout(f.type(lo_pool), seen));
    auto ans = lay::aggregate_layout(
        lay::agg_shape(sv.repr_transparent(), sv.is_union(), sv_fields.size()), ml);
    if (layout_ledger_open_)
        lay::record("layout_of", lay::type_key(sv.pkg(), sv.name()), ans);
    return { ans.layout.size, ans.layout.align };
}

// Is this struct UNSIZED — does it carry an `[T]` / `dyn` tail, at any depth?
// `MemNode`'s last field is a `PkdAlloc`, whose last field is a `[u8]`, so the
// property has to be transitive: MemNode has no static size either. TRANSITIVE
// and CONSERVATIVE — an unresolvable field type answers "not unsized", which
// keeps the type IN the comparison rather than silently out of it.
bool MLIRGenImpl::struct_is_unsized(lir_view::StructView sv,
                                    std::unordered_set<std::string>& seen) {
    using K = LogosType::Kind;
    if (!seen.insert(qualify_pkg(sv.pkg(), sv.name())).second) return false;
    const TypePoolImpl* p = pool_impl();
    for (auto f : sv.fields()) {
        TypeRef ft = f.type(p);
        auto k = TypeRef(ft).kind();
        if (k == K::UnsizedSlice || k == K::UnsizedDyn) return true;
        if (k == K::Struct || k == K::ZonedStruct) {
            auto it = find_struct_def_it(ft);
            if (it != all_struct_defs_.end() && struct_is_unsized(it->second, seen))
                return true;
        }
    }
    return false;
}

// The layout of an ENUM from its declaration. Split out of `layout_of`'s Enum
// case for the same reason `struct_def_layout` was split out of the Struct
// case: `verify_layout_engines` asks this engine, by registry name, at the
// moment every registry is complete.
MLIRGenImpl::Layout MLIRGenImpl::enum_def_layout(const std::string& ename,
                                                 TypeRef t) {
    // ONE call, four facts — the same call sema and mono make.
    // `register_tagged_enum` is gated on `ed.has_payload()`, so `te == nullptr`
    // IS "C-like" and needs no second test. The backing type comes from the
    // DECLARATION here, through `backing_layout`, instead of the old round trip
    // (backing TypeRef → mlir::Type → getWidth → int_layout): three engines
    // reaching one number by three routes is how they drift, even when all
    // three routes currently agree.
    auto* te = resolve_tagged_enum(ename, t);
    lay::L payload = te ? lay::L{ te->payload_bytes, te->payload_align }
                        : lay::L{ 0, 1 };
    std::string ekey = te ? te->name : ename;
    std::string_view epkg;
    std::optional<lay::L> backing;
    // The DECLARATION, resolved the one way — instance name included, so a
    // generic C-like enum does not silently become the default i32 disc.
    if (auto* ev = find_enum_decl(ekey, t)) {
        epkg = ev->pkg();
        ekey = ev->name();
        if (auto bt = ev->backing_type(pool_impl()))
            backing = lay::backing_layout(TypeRef(bt).kind());
    }
    auto ans = lay::enum_layout(te != nullptr, te && te->niche.packed,
                                payload, backing);
    if (layout_ledger_open_)
        lay::record("layout_of", lay::type_key(epkg, ekey), ans);
    return { ans.layout.size, ans.layout.align };
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
    // Leaf kinds: the ONE table, at the enum (LogosType::scalar_layout).
    if (auto sl = LogosType::scalar_layout(tv.kind()); sl.align != 0)
        return {sl.size, sl.align};
    switch (tv.kind()) {
    // Unsized `[T]` tail: no bytes of its own, aligned as its element.
    case K::UnsizedSlice:
        return { 0, tv.elem() ? aggregate_member_layout(tv.elem(), seen).align : 1 };
    case K::Array: {
        if (!tv.elem()) return {0, 1};
        // Same law as the mlir_type path: an unbound length has no layout.
        // Returning 0 here silently sized every enclosing aggregate wrong,
        // and did so BEHIND the mlir_type guard rather than through it.
        if (!tv.arr_size_var().empty()) {
            llvm::report_fatal_error(llvm::StringRef(std::string(
                "array length '" + std::string(tv.arr_size_var()) +
                "' was never bound to a value; it has no layout.")));
        }
        auto e = aggregate_member_layout(tv.elem(), seen);  // element repr in the array
        return { tv.arr_size() * e.size, e.align };
    }
    case K::Tuple: {
        LayoutAgg agg;
        for (auto e : tv.tuple_elems()) agg.push(aggregate_member_layout(e, seen));
        return agg.finish();
    }
    case K::Struct: case K::ZonedStruct: {
        // PKG-QUALIFIED key for the cycle guard AND the def lookup: a bare
        // `concrete_struct_name` would (a) collide two same-named structs in the
        // cycle-guard set and (b) mis-resolve all_struct_defs_ to the wrong def
        // → wrong size (find_struct_def_it).
        auto cname = mlir_struct_key(t);
        if (!seen.insert(cname).second) return {8, 8};  // cycle guard
        Layout r{8, 8};
        if (auto it = find_struct_def_it(t); it != all_struct_defs_.end())
            r = struct_def_layout(it->second, seen);
        seen.erase(cname);
        return r;
    }
    case K::Enum:
        return enum_def_layout(std::string(tv.enum_name()), t);
    default: return {8, 8};
    }
}

// ── Freeze predicate (interior-mutability detection, rustc-parity Slice C) ───
// Returns true iff `t` has NO UnsafeCell reachable through its own inline bytes
// (struct fields / enum payloads / tuple + array elements), WITHOUT crossing a
// pointer/reference. Mirrors layout_of's recursion. CONSERVATIVE: any unknown
// or unresolvable shape returns FALSE so a missing case can only COST an
// optimization, never cause an unsound readonly/noalias on a shared &T.
bool MLIRGenImpl::type_is_freeze(TypeRef t,
                                 std::unordered_set<std::string>& seen) {
    using K = LogosType::Kind;
    if (!t) return false;                       // unknown → conservative
    TypeRef tv{t};
    if (is_anyval(tv)) return true;             // i32 tag word, no interior
    switch (tv.kind()) {
    // Scalars / zero-size — no interior mutability.
    case K::Void: case K::Never:
    case K::Bool: case K::Char:
    case K::I8:  case K::U8:  case K::I16: case K::U16:
    case K::I24: case K::U24: case K::I32: case K::U32:
    case K::I56: case K::U56: case K::I64: case K::U64:
    case K::I128: case K::U128:
    case K::F32: case K::F64: case K::IntLit: case K::FloatLit:
    case K::Usize: case K::Isize:
        return true;
    // Pointers/references STOP the recursion: interior mutability behind an
    // indirection does NOT infect the container (Arc/Rc/&Cell stay Freeze).
    case K::Ptr: case K::Ref: case K::MutRef: case K::FnPtr:
    case K::Slice: case K::Closure: case K::TraitObject: case K::DstRef:
    case K::TaggedPtr:
    case K::UnsizedSlice: case K::UnsizedDyn:
        return true;
    case K::Array:
        return !tv.elem() ? true : type_is_freeze(tv.elem(), seen);
    case K::Tuple: {
        for (auto e : tv.tuple_elems())
            if (!type_is_freeze(e, seen)) return false;
        return true;
    }
    case K::Struct: case K::ZonedStruct: {
        // The interior-mutability lang-item: recognised by qualified name to
        // avoid colliding with a user `UnsafeCell` (mirrors sema_auto_trait).
        if (tv.struct_name() == "UnsafeCell" &&
            tv.pkg_name() == "logos.lang.cell")
            return false;
        // PKG-QUALIFIED key/def (find_struct_def_it): a bare name would collide
        // two same-named structs → wrong freeze answer (noalias soundness).
        auto cname = mlir_struct_key(t);
        if (!seen.insert(cname).second) return true;   // cycle → don't re-block
        bool frozen = true;
        auto it = find_struct_def_it(t);
        if (it == all_struct_defs_.end()) {
            frozen = false;                            // unresolved → conservative
        } else {
            const TypePoolImpl* lo_pool = pool_impl();
            for (auto f : it->second.fields()) {
                if (!type_is_freeze(f.type(lo_pool), seen)) { frozen = false; break; }
            }
        }
        seen.erase(cname);
        return frozen;
    }
    case K::Enum: {
        if (auto* te = resolve_tagged_enum(std::string(tv.enum_name()), t)) {
            for (auto& v : te->variants)
                for (auto& ft : v.logos_types)
                    if (!type_is_freeze(ft, seen)) return false;
            return true;
        }
        return true;   // C-like enum (no payload) — just a discriminant scalar
    }
    default: return false;                       // unknown kind → conservative
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
        // carrying its Writ zone (the allocator rides the &mut → grow methods
        // reach it from &mut self). Shared `&T` / `*T` stay thin (read never grows).
        case K::MutRef: {
            TypeRef p = TypeRef(t).pointee();
            if (p && (p.kind() == K::Struct || p.kind() == K::ZonedStruct)) {
                auto it = find_struct_def_it(p);  // pkg-qualified-first
                if (it != all_struct_defs_.end() && it->second.valid() && it->second.zone_mut())
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
            auto it = find_struct_def_it(t);  // pkg-qualified-first
            if (it != all_struct_defs_.end() && it->second.valid() && it->second.rel_ptr())
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
        if (it != all_struct_defs_.end() && it->second.valid() && it->second.zoned2())
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
MLIRGenImpl::Layout MLIRGenImpl::variant_payload_layout(lir_view::EnumVariantView v) {
    // The payload is laid out exactly like a struct/tuple of its fields —
    // derive {size, align} from the unified layout accumulator.
    // Enum payloads store members BY VALUE (a slice/closure payload is the full
    // inline fat pair, e.g. Option<&[u8]> = {i32, [16 x i8]}), UNLIKE struct/
    // tuple fields which store slice/closure/tuple as an 8-byte ptr. So use the
    // by-value layout_of here, not aggregate_member_layout.
    LayoutAgg agg;
    std::unordered_set<std::string> seen;
    v.each_payload_type(pool_impl(), [&](TypeRef pt) {
        if (TypeRef(pt).kind() == LogosType::Kind::Void) return;
        agg.push(layout_of(pt, seen));
    });
    return agg.finish();
}

uint64_t MLIRGenImpl::variant_payload_bytes(lir_view::EnumVariantView v) {
    return variant_payload_layout(v).size;
}

// The engine's half of the niche rule: resolve the two registry-dependent
// facts (`#[non_null]` 8-byte wrapper; a reference's pointee alignment) and
// hand the rest to `layout::arm_desc_of_kind`.
lay::ArmDesc MLIRGenImpl::niche_arm_desc(TypeRef t) {
    using K = LogosType::Kind;
    auto k = TypeRef(t).kind();
    uint64_t pointee_align = 0;
    if ((k == K::Ref || k == K::MutRef) && TypeRef(t).pointee())
        pointee_align = layout_of(TypeRef(t).pointee()).align;
    bool nonnull_wrapper = false;
    if (k == K::Struct || k == K::ZonedStruct) {
        auto it = find_struct_def_it(t);   // pkg-qualified-first
        if (it != all_struct_defs_.end() && it->second.valid() && it->second.non_null()) {
            std::unordered_set<std::string> seen;
            nonnull_wrapper = logos_abi_byte_size(t, seen) == 8;
        }
    }
    return lay::arm_desc_of_kind(k, pointee_align, nonnull_wrapper);
}

void MLIRGenImpl::register_tagged_enum(lir_view::EnumView ed) {
    // Skip if fully populated already (variants filled in). Stub entries
    // (pre-registered by mlir_gen.cpp's two-pass loop) have empty variants
    // and need their bodies filled here.
    std::string ed_name(ed.name());
    auto eit = tagged_enums_.find(ed_name);
    if (eit != tagged_enums_.end() && !eit->second.variants.empty()) return;
    TaggedEnumInfo info;
    info.zoned = ed.zoned2();   // F3: niche enum's Ref arm self-relative at-rest
    info.name = ed_name;
    uint64_t max_bytes = 0, max_align = 1;
    ed.each_variant([&](lir_view::EnumVariantView v) {
        TaggedEnumInfo::VariantPayload vp;
        vp.disc = v.disc();
        v.each_payload_type(pool_impl(), [&](TypeRef pt) {
            if (TypeRef(pt).kind() == LogosType::Kind::Void) return;  // () unit — no field
            auto ft = logos_to_mlir(pt);
            if (!ft) ft = builder_.getI32Type();
            vp.field_types.push_back(ft);
            vp.logos_types.push_back(pt);
        });
        auto pl = variant_payload_layout(v);
        if (pl.size  > max_bytes) max_bytes = pl.size;
        if (pl.align > max_align) max_align = pl.align;
        info.variants.push_back(std::move(vp));
    });
    info.payload_bytes = max_bytes;
    info.payload_align = max_align;
    // Niche eligibility — whether the enum packs its discriminant into the
    // payload's spare values instead of carrying a disc word. That is a
    // LAYOUT question (`sizeof(Option<&T>) == 8`, not 16), so the rule is in
    // layout_law.hpp with the accumulation rules and sema asks the same one.
    // The engine supplies only the per-arm description; `classify_niche`
    // decides.
    {
        std::vector<lay::VariantDesc> vds;
        vds.reserve(info.variants.size());
        for (auto& vp : info.variants) {
            lay::VariantDesc vd;
            vd.disc = vp.disc;
            vd.n_payload = static_cast<unsigned>(vp.logos_types.size());
            if (vd.n_payload == 1) vd.arm = niche_arm_desc(vp.logos_types[0]);
            vds.push_back(vd);
        }
        auto n = lay::classify_niche(info.zoned, vds);
        info.niche.packed = n.packed;
        switch (n.kind) {
        case lay::NicheKind::None:
            info.niche.kind = TaggedEnumInfo::Niche::NoNiche;
            break;
        case lay::NicheKind::NullPtr:
            info.niche.kind      = TaggedEnumInfo::Niche::NullPtr;
            info.niche.none_disc = n.none_disc;
            info.niche.some_disc = n.some_disc;
            break;
        case lay::NicheKind::LowBit:
            info.niche.kind       = TaggedEnumInfo::Niche::LowBit;
            info.niche.ptr_disc   = n.ptr_disc;
            info.niche.val_disc   = n.val_disc;
            info.niche.val_bits   = n.val_bits;
            info.niche.val_signed = n.val_signed;
            info.niche.val_raw    = n.val_raw;
            break;
        }
    }
    auto enum_type = mlir::LLVM::LLVMStructType::getIdentified(
        builder_.getContext(), "enum." + ed_name);
    // NOTE: the body (payload byte-array size) is NOT set here — a nested enum
    // payload may still be a 0-byte stub at this point, so max_bytes can be
    // under-sized. The body is set ONCE, after the fixpoint in mlir_gen.cpp
    // recomputes every enum's final payload_bytes (finalize_enum_bodies).
    // An identified LLVM struct's body is set-once, so setting it prematurely
    // here would lock in the wrong size.
    info.llvm_type = enum_type;
    tagged_enums_[ed_name] = std::move(info);
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

const lir_view::EnumView* MLIRGenImpl::find_enum_decl(std::string_view name,
                                                      TypeRef type) {
    if (auto it = enum_types_.find(std::string(name)); it != enum_types_.end())
        return &it->second;
    if (type && TypeRef(type).kind() == LogosType::Kind::Enum &&
        !TypeRef(type).type_args().empty()) {
        std::string cname(name);
        for (auto a : TypeRef(type).type_args()) {
            cname += "__";
            cname += Mono::mangle_type(a);
        }
        if (auto it = enum_types_.find(cname); it != enum_types_.end())
            return &it->second;
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

// ---------------------------------------------------------------------------
// THE ENGINE-AGREEMENT GATE
// ---------------------------------------------------------------------------
//
// Three engines answer "how many bytes, at what alignment, with which field at
// which offset":
//
//   A  `layout_of` / `struct_def_layout` — over TypeRef. Drives `size_of`, the
//      alloca sizes, every container's element stride, sema and mono.
//   B  `mlir_abi_size` / `mlir_abi_align` / `mlir_field_offset` — over the
//      EMITTED LLVM-dialect type. Drives every value-copy memcpy byte count and
//      the DWARF member offsets.
//   C  `llvm::DataLayout` on the same type, built from the TARGET's own layout
//      string. This is what the object file is actually laid out with: it is
//      what the emitted `getelementptr` resolves to, so it is what every READ
//      the compiler emits uses.
//
// A ≠ C or B ≠ C is a miscompile with no diagnostic: the value is written at
// one stride and read at another. Two rounds of exactly that shipped — first
// `{i32,i64}` sized 12 against ISel's 16, then `{i56,i8,i64}` sized 16 against
// LLVM's 24 with `id` at offset 16. Both were found by running programs and
// noticing wrong numbers.
//
// So the disagreement is made to be a COMPILE ERROR naming the type and both
// answers, and the check runs on EVERY compile, not in a fixture. C is computed
// by mirroring the MLIR type into a real `llvm::Type` and asking LLVM — it
// shares no line of code with A or B, which is what makes it an oracle rather
// than a restatement.
namespace {

// mlir::Type → llvm::Type. Structural mirror only; reads nothing from A or B.
llvm::Type* mirror_to_llvm(mlir::Type t, llvm::LLVMContext& c,
                           llvm::DenseMap<mlir::Type, llvm::Type*>& memo) {
    if (auto it = memo.find(t); it != memo.end()) return it->second;
    if (auto i = mlir::dyn_cast<mlir::IntegerType>(t))
        return llvm::IntegerType::get(c, i.getWidth());
    if (mlir::isa<mlir::Float32Type>(t))  return llvm::Type::getFloatTy(c);
    if (mlir::isa<mlir::Float64Type>(t))  return llvm::Type::getDoubleTy(c);
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t))
        return llvm::PointerType::get(c, 0);
    if (auto a = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(t)) {
        auto* e = mirror_to_llvm(a.getElementType(), c, memo);
        return e ? llvm::ArrayType::get(e, a.getNumElements()) : nullptr;
    }
    if (auto s = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t)) {
        if (s.isIdentified() && !s.isInitialized()) return nullptr;  // opaque
        auto* st = llvm::StructType::create(c);
        memo[t] = st;                       // break recursion before descending
        llvm::SmallVector<llvm::Type*, 8> body;
        for (auto e : s.getBody()) {
            auto* m = mirror_to_llvm(e, c, memo);
            if (!m) { memo.erase(t); return nullptr; }
            body.push_back(m);
        }
        st->setBody(body, s.isPacked());
        return st;
    }
    return nullptr;   // vectors, tokens, … — not something we size
}

}  // namespace

void MLIRGenImpl::verify_layout_engines() {
    // No target → no C. Report nothing rather than compare against a guess;
    // the count the gate asserts then drops to 0 and the gate goes red, which
    // is the correct outcome for "could not look".
    std::string dl_str = target_data_layout_string(target_cpu_);
    if (dl_str.empty()) return;
    // From here on `layout_of` writes what it answers to the cross-engine
    // ledger — see `layout_ledger_open_`. Every registry is complete, so an
    // answer given now is the answer this engine stands behind.
    layout_ledger_open_ = true;
    struct CloseLedger {
        bool& f;
        ~CloseLedger() { f = false; }
    } close_ledger{ layout_ledger_open_ };
    llvm::DataLayout DL(dl_str);
    llvm::LLVMContext ctx;
    llvm::DenseMap<mlir::Type, llvm::Type*> memo;

    uint64_t n_types = 0, n_fields = 0, n_defs = 0;
    // (bucket, row). The bucket is the SHAPE the row is about, and it exists
    // because the row list is BOUNDED when printed: an engine off by one byte
    // produces thousands of rows, and showing the first N of a name-sorted list
    // showed N rows of whichever shape sorts first — every other shape was
    // invisible in the diagnostic even though it was counted. The report now
    // shows a few of EACH, which is what makes "named by engine and by shape"
    // true of the output and not only of the census.
    std::vector<std::pair<std::string, std::string>> bad;
    std::string bucket = "emitted";   // A/B vs C, over the emitted MLIR type
    // What `llvm::DataLayout` says, keyed the way every engine names a type —
    // the oracle the early engines' recorded answers are checked against.
    std::unordered_map<std::string, Layout> truth;

    auto note = [&](std::string_view what, std::string_view name,
                    std::string_view lhs_engine, uint64_t lhs,
                    std::string_view rhs_engine, uint64_t rhs) {
        bad.push_back({ bucket,
                      std::string(name) + ": " + std::string(what) + " — " +
                      std::string(lhs_engine) + " says " + std::to_string(lhs) +
                      ", " + std::string(rhs_engine) + " says " + std::to_string(rhs) });
    };

    // Deterministic order: the diagnostic must not depend on hash iteration.
    std::vector<const StructInfo*> infos;
    infos.reserve(struct_types_.size());
    for (auto& kv : struct_types_) infos.push_back(&kv.second);
    std::sort(infos.begin(), infos.end(),
              [](const StructInfo* a, const StructInfo* b) { return a->name < b->name; });

    // ⚠ THE CANARY for the two engines that are asked RIGHT HERE (see
    // layout_law.hpp). `sema_abi_layout`/`mono_abi_layout` are moved on the way
    // into the ledger; `layout_of` and `mlir_abi_size` never enter it — they are
    // read below — so their canary is injected at the read, one line above the
    // comparison that judges them, and travels through the same `note()`, the
    // same `bad`, the same census field and the same fatal error. Only the FIRST
    // compared type is moved: `infos` is sorted, every type that reaches the
    // read is compared, so one is deterministic and enough.
    const char* canary = layout::canary_engine();
    const bool canary_b = canary && std::strcmp(canary, "mlir_abi_size") == 0;
    const bool canary_a = canary && std::strcmp(canary, "layout_of") == 0;

    for (const StructInfo* info : infos) {
        auto st = info->llvm_type;
        if (!st || (st.isIdentified() && !st.isInitialized())) continue;
        auto* lt = mirror_to_llvm(st, ctx, memo);
        if (!lt || !lt->isStructTy()) continue;
        auto* slt = llvm::cast<llvm::StructType>(lt);
        const llvm::StructLayout* SL = DL.getStructLayout(slt);
        ++n_types;

        // ── B vs C: the memcpy byte count against the GEP stride ─────────────
        uint64_t b_size  = mlir_abi_size(st);
        uint64_t b_align = mlir_abi_align(st);
        if (canary_b && n_types == 1) b_size += 1;
        uint64_t c_size  = DL.getTypeAllocSize(slt).getFixedValue();
        uint64_t c_align = DL.getABITypeAlign(slt).value();
        if (b_size != c_size)
            note("size", info->name, "mlir_abi_size", b_size, "llvm::DataLayout", c_size);
        if (b_align != c_align)
            note("align", info->name, "mlir_abi_align", b_align, "llvm::DataLayout", c_align);
        for (unsigned i = 0, n = slt->getNumElements(); i < n; ++i) {
            ++n_fields;
            uint64_t b_off = mlir_field_offset(st, i);
            uint64_t c_off = SL->getElementOffset(i);
            if (b_off != c_off)
                note("offset of field " + std::to_string(i), info->name,
                     "mlir_field_offset", b_off, "llvm::StructLayout", c_off);
        }

        // ── A vs C: `size_of` / the container stride against the same ────────
        // TWO shapes are excluded, and each is excluded because A and C are
        // answering DIFFERENT QUESTIONS about it, not because they disagree:
        //
        //   * a UNION — its LLVM type is the field list in sequence (the
        //     variants are reached by casting the storage), so C is a sum where
        //     A is a max, by design;
        //   * a CUSTOM DST (`Segment { …, data: [u8] }`, `RcInner<T: ?Sized>`,
        //     `WString { bytes: [u8] }`) — A is the size of the SIZED PREFIX,
        //     which is the only static size such a type has, while its LLVM
        //     stand-in carries a placeholder slot for the tail. Nothing is ever
        //     allocated at C's number, and the FIELD OFFSETS, which is what a
        //     prefix access actually uses, are still compared above.
        //
        // Every other shape — plain, `#[repr(transparent)]`, nested, arrays of
        // them, tuple-carrying — is compared. Removing either exclusion must
        // make exactly these types reappear and nothing else.
        auto dit = all_struct_defs_.find(info->name);
        if (dit == all_struct_defs_.end()) continue;
        // ASK THIS ENGINE FIRST, EXCLUDE SECOND. The two exclusions below are
        // about what `llvm::DataLayout` can be compared to — they are NOT
        // reasons to leave a union or a custom DST out of the LEDGER, which is
        // where the cross-ENGINE comparison happens and which is the only
        // authority those two shapes have. Asking before the `continue` is what
        // keeps the `union` cell of the matrix non-empty.
        std::unordered_set<std::string> seen;
        Layout a = struct_def_layout(dit->second, seen);
        if (dit->second.is_union()) continue;
        {
            std::unordered_set<std::string> dseen;
            if (struct_is_unsized(dit->second, dseen)) continue;
        }
        ++n_defs;
        if (canary_a && n_defs == 1) a.size += 1;
        if (a.size != c_size)
            note("size", info->name, "layout_of", a.size, "llvm::DataLayout", c_size);
        if (a.align != c_align)
            note("align", info->name, "layout_of", a.align, "llvm::DataLayout", c_align);
        truth[info->name] = Layout{ c_size, c_align };
    }

    // ── ENUMS: THE KIND THE STRUCT LOOP CANNOT SEE ──────────────────────────
    // `truth` was built from `struct_types_` alone, so an enum's layout entered
    // the comparison only when some struct happened to have a field of it. That
    // is why mono answering {4,4} for `enum B : u64` survived: no row of this
    // table was ever about an enum, and the enum's own bytes are what a DstRef
    // prefix offset is measured from.
    //
    // A payload enum's emitted type is a struct; a C-LIKE enum's is an INTEGER,
    // and `llvm::DataLayout` sizes both. Same oracle, same `truth` map, same
    // comparison below.
    uint64_t n_enum_types = 0;
    {
        std::vector<std::string> enames;
        enames.reserve(enum_types_.size());
        for (auto& kv : enum_types_) enames.push_back(kv.first);
        std::sort(enames.begin(), enames.end());
        for (auto& en : enames) {
            auto evit = enum_types_.find(en);
            std::string key = layout::type_key(evit->second.pkg(), en);
            mlir::Type mt;
            if (auto tit = tagged_enums_.find(en); tit != tagged_enums_.end()) {
                auto st = tit->second.llvm_type;
                if (!st || (st.isIdentified() && !st.isInitialized())) continue;
                mt = st;
            } else {
                // C-like — the value IS the discriminant.
                mt = enum_disc_mlir(en);
            }
            if (!mt) continue;
            auto* lt = mirror_to_llvm(mt, ctx, memo);
            if (!lt) continue;
            truth[key] = Layout{ DL.getTypeAllocSize(lt).getFixedValue(),
                                 DL.getABITypeAlign(lt).value() };
            // Ask the TypeRef engine about the same enum, by registry name, so
            // its answer lands in the ledger alongside sema's and mono's and
            // the enum cells of the matrix have a `layout_of` row at all.
            (void)enum_def_layout(en, TypeRef(nullptr));
            ++n_enum_types;
        }
    }

    // ── D, E vs C: the engines that ran BEFORE this one ─────────────────────
    // Sema and mono answer the same question earlier, over their own type
    // registries, and their answers are byte OFFSETS — where a custom DST's
    // `[T]` tail starts, where `offset_of!` points. They are not reachable from
    // here, so they RECORD what they answered (layout_law.cpp's ledger) and the
    // record is checked against `llvm::DataLayout` — the layout the object file
    // is emitted with — right here, keyed by `concrete_struct_name`, the same
    // function both sides use to name a type.
    //
    // A ledger entry whose key this compile never registered is COUNTED, not
    // dropped: `n_unmatched` is reported, so "sema was checked" is a number the
    // gate can put a floor under rather than an assumption.
    //
    // ⚠ AND THE COMPARISON IS A MATRIX, NOT A TOTAL — ENGINE × SHAPE.
    //
    // A per-engine total says an engine was checked. It does not say WHICH
    // BRANCHES of it were checked, and a missing branch is exactly a branch
    // nothing exercised. So every ledger entry carries the `Shape` the LAW
    // applied (not the one the engine believed), and the cells are counted
    // separately: `mono_abi_layout` × `c-like` at ZERO is the report that would
    // have named this arc's bug before a program was miscompiled.
    //
    // TWO authorities, because one of them cannot see every shape:
    //   * `llvm::DataLayout` — for every key it has an opinion about. It has
    //     none about a UNION (its LLVM type is the field list in sequence, so C
    //     is a sum where the law is a max) or a custom DST (its stand-in
    //     carries a placeholder tail), which is precisely why those cells need
    //     the second authority and not an exclusion;
    //   * EVERY OTHER ENGINE that answered the same key. All of them now call
    //     ONE law, so a difference is a difference in what an engine SUPPLIED —
    //     which member list, which shape, which backing type. Including the
    //     shape itself: two engines applying two different branches to one type
    //     is reported as a shape disagreement even when the bytes coincide.
    uint64_t n_unmatched = 0;
    std::map<std::string, uint64_t> per_engine;   // engine → types CHECKED
    std::map<std::string, std::array<uint64_t, layout::kShapeCount>> cells;
    {
        // key → engine → first answer (dedup on (engine, key), as before).
        std::map<std::string, std::map<std::string, layout::LedgerEntry>> by_key;
        for (auto& e : layout::ledger())
            by_key[e.key].emplace(e.engine, e);
        for (auto& [key, ans] : by_key) {
            auto tit = truth.find(key);
            const bool has_truth = tit != truth.end();
            const bool cross     = ans.size() >= 2;
            if (!has_truth && !cross) { n_unmatched += ans.size(); continue; }
            bucket = std::string(layout::shape_name(ans.begin()->second.shape));
            for (auto& [eng, e] : ans) {
                ++per_engine[eng];
                ++cells[eng][static_cast<size_t>(e.shape)];
                if (!has_truth) continue;
                if (e.answer.size != tit->second.size)
                    note("size", key, eng, e.answer.size,
                         "llvm::DataLayout", tit->second.size);
                if (e.answer.align != tit->second.align)
                    note("align", key, eng, e.answer.align,
                         "llvm::DataLayout", tit->second.align);
            }
            if (!cross) continue;
            auto ref = ans.begin();
            for (auto it = std::next(ref); it != ans.end(); ++it) {
                if (it->second.answer.size != ref->second.answer.size)
                    note("size", key, ref->first, ref->second.answer.size,
                         it->first, it->second.answer.size);
                if (it->second.answer.align != ref->second.answer.align)
                    note("align", key, ref->first, ref->second.answer.align,
                         it->first, it->second.answer.align);
                if (it->second.shape != ref->second.shape)
                    bad.push_back({ bucket,
                        key + ": SHAPE — " + ref->first + " applied '" +
                        std::string(layout::shape_name(ref->second.shape)) +
                        "', " + it->first + " applied '" +
                        std::string(layout::shape_name(it->second.shape)) + "'" });
            }
        }
    }

    if (std::getenv("LOGOS_VERIFY_LAYOUT")) {
        // PER ENGINE, not a total: a total lets one engine go silent while the
        // other carries the number. Every engine that answers must appear here
        // with its own count, and the gate floors each one separately.
        std::string per;
        for (auto& [eng, n] : per_engine)
            per += ", " + eng + " " + std::to_string(n);
        std::fprintf(stderr,
                     "layout-verify: %llu struct types, %llu fields, %llu defs, "
                     "%llu enum types%s, %llu unmatched, %llu disagreements\n",
                     (unsigned long long)n_types, (unsigned long long)n_fields,
                     (unsigned long long)n_defs,
                     (unsigned long long)n_enum_types, per.c_str(),
                     (unsigned long long)n_unmatched,
                     (unsigned long long)bad.size());
        // THE MATRIX, one row per engine. Every cell is a COUNT OF CHECKED
        // ANSWERS of that shape — not a claim that the shape exists. The gate
        // floors each cell, so a branch that stops being reached goes red where
        // it is, named by engine and by shape, instead of two months later as a
        // wrong program.
        for (auto& [eng, row] : cells) {
            std::string line = "layout-matrix: " + eng;
            for (size_t i = 0; i < layout::kShapeCount; ++i) {
                line += " ";
                line += layout::shape_name(static_cast<layout::Shape>(i));
                line += "=" + std::to_string(row[i]);
            }
            std::fprintf(stderr, "%s\n", line.c_str());
        }
        // ── THE SAME CENSUS AS A STRUCTURED VERDICT ─────────────────────────
        // The two blocks above are for a human reading a failing build. THE
        // GATE READS ONLY THIS ONE, with a strict parser (tests/logos/
        // verdict.py), because a gate that scrapes prose has a failure mode
        // that looks exactly like a pass: `sed -E 's/…([0-9]+) defs…/\1/'` on
        // a line whose wording moved prints the WHOLE LINE, and
        // `[ "<a sentence>" -lt 3676 ]` exits 2, which `if` reads as FALSE.
        // MEASURED 2026-08-01: rewriting only `, N defs,` to `, defs=N,` on
        // this stream left the gate at EXIT 0 with its OK line printed.
        //
        // Here a renamed field is a MISSING field and the parser says so and
        // exits 3. Every name below is an identifier and every value an
        // integer, so this is emitted without a JSON library and cannot be
        // malformed by its own content: engine and shape names come from
        // `layout::shape_name` and from the engines' own registered names,
        // which are C identifiers, plus the '-' in "c-like".
        auto jnum = [](const char* k, uint64_t v) {
            return "\"" + std::string(k) + "\":" + std::to_string(v);
        };
        std::string j = "{";
        j += jnum("struct_types", n_types) + ",";
        j += jnum("fields", n_fields) + ",";
        j += jnum("defs", n_defs) + ",";
        j += jnum("enum_types", n_enum_types) + ",";
        j += jnum("unmatched", n_unmatched) + ",";
        j += jnum("disagreements", bad.size()) + ",";
        j += "\"engines\":{";
        bool first = true;
        for (auto& [eng, n] : per_engine) {
            if (!first) j += ",";
            first = false;
            j += jnum(eng.c_str(), n);
        }
        j += "},\"matrix\":{";
        first = true;
        for (auto& [eng, row] : cells) {
            if (!first) j += ",";
            first = false;
            j += "\"" + eng + "\":{";
            for (size_t i = 0; i < layout::kShapeCount; ++i) {
                if (i) j += ",";
                j += jnum(std::string(layout::shape_name(
                              static_cast<layout::Shape>(i))).c_str(), row[i]);
            }
            j += "}";
        }
        j += "}}";
        std::fprintf(stderr, "layout-verify-json: %s\n", j.c_str());
    }

    if (!bad.empty()) {
        std::string msg = "the compiler's layout engines disagree — a value would "
                          "be written at one stride and read at another:\n";
        // Bounded PER SHAPE: a whole engine off by one byte (the canary does
        // exactly that) would otherwise print thousands of rows and bury the
        // count — and a flat first-N of a name-sorted list buries every shape
        // but one, which made the report say less than the census did. The
        // COUNT is on the census line above and is what the gate reads.
        constexpr size_t kMaxPerShape = 6;
        std::map<std::string, size_t> shown;
        size_t total_shown = 0;
        for (auto& [b, row] : bad) {
            if (shown[b]++ >= kMaxPerShape) continue;
            msg += "  [" + b + "] " + row + "\n";
            ++total_shown;
        }
        if (bad.size() > total_shown)
            msg += "  … and " + std::to_string(bad.size() - total_shown) +
                   " more (at most " + std::to_string(kMaxPerShape) +
                   " shown per shape)\n";
        llvm::report_fatal_error(llvm::StringRef(msg));
    }
}

} // namespace logos::compiler
