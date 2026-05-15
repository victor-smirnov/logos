// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_expr.cpp — Expression code generation.

#include "mlir_gen_impl.hpp"

#include <logos/compiler/sha256.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/typed_array.hpp>

#include <cstring>

namespace logos::compiler {

using namespace lir;

namespace {
// Iterate top-level func.func ops directly (mod.getOps<>) instead of
// recursively walking ALL nested ops — orders of magnitude faster for
// modules with thousands of functions.
template<class Pred>
static mlir::func::FuncOp find_fn_matching(mlir::ModuleOp mod, Pred&& pred) {
    for (auto fn : mod.getOps<mlir::func::FuncOp>())
        if (pred(fn)) return fn;
    return {};
}

static mlir::func::FuncOp find_func_op(mlir::ModuleOp mod, std::string_view name) {
    if (auto fn = mod.lookupSymbol<mlir::func::FuncOp>(name))
        return fn;
    auto found = find_fn_matching(mod,
        [&](mlir::func::FuncOp fn) { return fn.getName().str() == name; });
    if (found) return found;
    // Hardcoded stdlib intrinsic lookups (e.g. `hermes_build_from_template`)
    // must also resolve the post-unify pkg-qualified + sig-suffixed form
    // (`std.hermes.ctr$<bare>__f__<sig>`). Walk fns and canonicalise.
    auto canonical = [](std::string_view nm) -> std::string_view {
        // Strip free-fn `pkg$` prefix.
        if (auto d = nm.find('$'); d != std::string_view::npos) {
            bool gen = (d + 2 < nm.size() && nm[d + 1] == 'G' &&
                        nm[d + 2] >= '0' && nm[d + 2] <= '9');
            if (!gen) nm = nm.substr(d + 1);
        }
        // Strip method `pkg.` prefix (last dot before bare name).
        if (auto d = nm.rfind('.'); d != std::string_view::npos)
            nm = nm.substr(d + 1);
        // Strip sig suffix.
        if (auto p = nm.find("__f__"); p != std::string_view::npos)
            nm = nm.substr(0, p);
        else if (auto p = nm.find("__g__"); p != std::string_view::npos)
            nm = nm.substr(0, p);
        return nm;
    };
    return find_fn_matching(mod, [&](mlir::func::FuncOp fn) {
        return canonical(fn.getName()) == name;
    });
}
}  // namespace

// ---------------------------------------------------------------------------
// gen_expr — main dispatcher
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr(const LExpr& e) {
    return gen_expr(expr_ref_of(e));
}

mlir::Value MLIRGenImpl::gen_expr(lir_view::ExprRef er) {
    if (!er) {
        std::fprintf(stderr, "mlir_gen: gen_expr called without LIR mirror\n");
        return nullptr;
    }
    TypeRef ty = er.type(pool_impl());
    using C = lir_schema::expr::Code;
    switch (er.kind()) {
    case C::LitInt:     return gen_expr_kind(lir_view::ELitIntView{er},     ty);
    case C::LitFloat:   return gen_expr_kind(lir_view::ELitFloatView{er},   ty);
    case C::LitBool:    return gen_expr_kind(lir_view::ELitBoolView{er},    ty);
    case C::LitStr:     return gen_expr_kind(lir_view::ELitStrView{er},     ty);
    case C::VarRef:     return gen_expr_kind(lir_view::EVarRefView{er},     ty);
    case C::EnumLit:    return gen_expr_kind(lir_view::EEnumLitView{er},    ty);
    case C::EnumLitData:return gen_expr_kind(lir_view::EEnumLitDataView{er},ty);
    case C::Call:       return gen_expr_kind(lir_view::ECallView{er},       ty);
    case C::MethodCall: return gen_expr_kind(lir_view::EMethodCallView{er}, ty);
    case C::BinOp:      return gen_expr_kind(lir_view::EBinOpView{er},      ty);
    case C::Unary:      return gen_expr_kind(lir_view::EUnaryView{er},      ty);
    case C::AddrOf:     return gen_expr_kind(lir_view::EAddrOfView{er},     ty);
    case C::AddrOfTemp: return gen_expr_kind(lir_view::EAddrOfTempView{er}, ty);
    case C::Deref:      return gen_expr_kind(lir_view::EDerefView{er},      ty);
    case C::FieldRead:  return gen_expr_kind(lir_view::EFieldReadView{er},  ty);
    case C::IndexRead:  return gen_expr_kind(lir_view::EIndexReadView{er},  ty);
    case C::StructLit:  return gen_expr_kind(lir_view::EStructLitView{er},  ty);
    case C::ArrLit:     return gen_expr_kind(lir_view::EArrLitView{er},     ty);
    case C::Cast:       return gen_expr_kind(lir_view::ECastView{er},       ty);
    case C::New:        return gen_expr_kind(lir_view::ENewView{er},        ty);
    case C::IfExpr:     return gen_expr_kind(lir_view::EIfExprView{er},     ty);
    case C::TupleLit:   return gen_expr_kind(lir_view::ETupleLitView{er},   ty);
    case C::TupleIndex: return gen_expr_kind(lir_view::ETupleIndexView{er}, ty);
    case C::SliceLit:   return gen_expr_kind(lir_view::ESliceLitView{er},   ty);
    case C::SliceIndex: return gen_expr_kind(lir_view::ESliceIndexView{er}, ty);
    case C::SliceLen:   return gen_expr_kind(lir_view::ESliceLenView{er},   ty);
    case C::SlicePtr:   return gen_expr_kind(lir_view::ESlicePtrView{er},   ty);
    case C::ClosureBox: return gen_expr_kind(lir_view::EClosureBoxView{er}, ty);
    case C::ClosureCall:return gen_expr_kind(lir_view::EClosureCallView{er},ty);
    case C::FnPtrCall:  return gen_expr_kind(lir_view::EFnPtrCallView{er},  ty);
    case C::FormatCall: return gen_expr_kind(lir_view::EFormatCallView{er}, ty);
    case C::PackExpand: return gen_expr_kind(lir_view::EPackExpandView{er}, ty);
    case C::Try:        return gen_expr_kind(lir_view::ETryView{er},        ty);
    case C::MatchExpr:  return gen_expr_kind(lir_view::EMatchExprView{er},  ty);
    case C::SizeOf:     return gen_expr_kind(lir_view::ESizeOfView{er},     ty);
    case C::AlignOf:    return gen_expr_kind(lir_view::EAlignOfView{er},    ty);
    case C::GenericRef: {
        // Slice 2 invariant: mono_clone subst_expr rewrites every GenericRef
        // node into a VarRef of FnPtr type before mlir-gen runs. Reaching
        // here means a generic body had an unresolved GenericRef — diagnose
        // loudly rather than emit garbage.
        std::fprintf(stderr, "mlir_gen: unresolved GenericRef '%.*s' — mono failed to substitute its type-args\n",
                     (int)lir_view::EGenericRefView{er}.name().size(),
                     lir_view::EGenericRefView{er}.name().data());
        std::abort();
    }
    case C::TypeCodeOf: return gen_expr_kind(lir_view::ETypeCodeOfView{er}, ty);
    case C::BlockExpr:  return gen_expr_kind(lir_view::EBlockExprView{er},  ty);
    case C::HermesLit:  return gen_expr_kind(lir_view::EHermesLitView{er},  ty);
    case C::PtrArith:   return gen_expr_kind(lir_view::EPtrArithView{er},   ty);
    case C::PtrDiff:    return gen_expr_kind(lir_view::EPtrDiffView{er},    ty);
    case C::ReflectOf:  return gen_expr_kind(lir_view::EReflectOfView{er},  ty);
    }
    std::fprintf(stderr, "mlir_gen: unhandled expr code %d\n", int(er.kind()));
    return nullptr;
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitIntView v, TypeRef type) {
    int64_t value = v.value();
    int width = 32;
    if (type) {
        switch (TypeRef(type).kind()) {
        case LogosType::Kind::I64:
        case LogosType::Kind::U64: width = 64; break;
        case LogosType::Kind::I8:
        case LogosType::Kind::U8:  width = 8;  break;
        case LogosType::Kind::I16:
        case LogosType::Kind::U16: width = 16; break;
        case LogosType::Kind::I24:
        case LogosType::Kind::U24: width = 24; break;
        case LogosType::Kind::I56:
        case LogosType::Kind::U56: width = 56; break;
        case LogosType::Kind::I128:
        case LogosType::Kind::U128: width = 128; break;
        case LogosType::Kind::Usize:
        case LogosType::Kind::Isize:
            // K10-co-05: pointer-sized integers — match the target ABI
            // bit-width so an `Nusize` suffixed literal lowers as i64
            // (on a 64-bit target) instead of falling to the default
            // i32 path. Critical for `&3usize` temp-materialisation:
            // alloca x i64 with c3_i32 stored leaves high bits
            // uninitialised; downstream `load i64` returns garbage.
            width = ::logos::compiler::g_target_pointer_bits;
            break;
        case LogosType::Kind::Bool: width = 1; break;
        case LogosType::Kind::IntLit:
            // Untyped literal: use i64 if value doesn't fit in i32.
            if (value > INT32_MAX || value < INT32_MIN) width = 64;
            break;
        default: break;
        }
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, value, width);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitFloatView v, TypeRef type) {
    bool is_f32 = type && TypeRef(type).kind() == LogosType::Kind::F32;
    if (is_f32) {
        auto f32 = builder_.getF32Type();
        return builder_.create<mlir::arith::ConstantFloatOp>(
            loc_, f32, llvm::APFloat(float(v.value())));
    }
    auto f64 = builder_.getF64Type();
    return builder_.create<mlir::arith::ConstantFloatOp>(
        loc_, f64, llvm::APFloat(v.value()));
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitBoolView v, TypeRef) {
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, v.value() ? 1 : 0, 1);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitStrView v, TypeRef) {
    std::string raw{v.value()};
    bool is_raw = raw.size() >= 3 && raw[0] == 'r' &&
                  (raw[1] == '"' || raw[1] == '#');
    if (is_raw) {
        // Count '#' delimiters: r"...", r#"..."#, r##"..."##, etc.
        size_t hashes = 0;
        size_t p = 1;
        while (p < raw.size() && raw[p] == '#') { ++hashes; ++p; }
        // Strip r + hashes + opening " ... closing " + hashes
        raw = raw.substr(p + 1, raw.size() - p - 1 - hashes - 1);
    } else {
        // Regular string "..." — strip surrounding quotes.
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
            raw = raw.substr(1, raw.size() - 2);
    }
    // Process escape sequences (skipped for raw strings).
    std::string text;
    if (is_raw) {
        text = raw;
    } else {
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                switch (raw[i + 1]) {
                    case 'n':  text.push_back('\n'); ++i; break;
                    case 't':  text.push_back('\t'); ++i; break;
                    case 'r':  text.push_back('\r'); ++i; break;
                    case '\\': text.push_back('\\'); ++i; break;
                    case '0':  text.push_back('\0'); ++i; break;
                    case '"':  text.push_back('"');  ++i; break;
                    default:   text.push_back(raw[i]); break;
                }
            } else {
                text.push_back(raw[i]);
            }
        }
    }
    // LLVM requires string globals to include a null terminator in the array type.
    // The fat pointer's `len` field holds the content length (without the null byte).
    auto global_name = ".str." + std::to_string(str_counter_++);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    std::string text_with_null = text + '\0';
    auto i8       = builder_.getIntegerType(8);
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text_with_null.size());
    auto str_attr = builder_.getStringAttr(llvm::StringRef(text_with_null.data(), text_with_null.size()));
    builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, true, mlir::LLVM::Linkage::Internal, global_name, str_attr);

    builder_.restoreInsertionPoint(save_pt);
    auto raw_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);

    // Build fat pointer {ptr, len} on the stack and return pointer to it.
    auto stype  = slice_llvm_type();
    auto alloca = create_entry_alloca(stype);
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, raw_ptr, pp);
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
    auto len_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)text.size(), 64);
    builder_.create<mlir::LLVM::StoreOp>(loc_, len_val, lp);
    return alloca;
}

// ---------------------------------------------------------------------------
// Variable reference
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EVarRefView v, TypeRef type) {
    std::string name{v.name()};
    // Module constant: re-evaluate inline.
    auto cit = module_consts_.find(name);
    if (cit != module_consts_.end())
        return gen_expr(*cit->second->value);

    auto it = scope_.find(name);
    if (it == scope_.end()) {
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        // Check if name is a free function being used as a bare fn-ptr.
        if (type && TypeRef(type).kind() == LogosType::Kind::FnPtr) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(name);
            if (fn_sym) {
                // Return just the function address as a raw ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), name);
                return builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
            }
        }
        // Check if name is a free function being used as a value (closure fat pointer).
        // Create a non-capturing closure: {fn_ptr, null_env}.
        if (type && TypeRef(type).kind() == LogosType::Kind::Closure) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(name);
            if (fn_sym) {
                // Build closure fat pointer: { fn_ptr, env_ptr=null }
                auto closure_struct_t = mlir::LLVM::LLVMStructType::getLiteral(
                    builder_.getContext(), {ptr_type(), ptr_type()});
                auto alloca = create_entry_alloca(closure_struct_t);
                // Store the function address as fn_ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), name);
                auto fn_addr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
                llvm::SmallVector<mlir::LLVM::GEPArg> fp_idx{int32_t(0), int32_t(0)};
                auto fp_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), closure_struct_t, alloca, fp_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp_ptr);
                // Store null as env_ptr.
                auto null_ptr = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
                llvm::SmallVector<mlir::LLVM::GEPArg> ep_idx{int32_t(0), int32_t(1)};
                auto ep_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), closure_struct_t, alloca, ep_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, null_ptr, ep_ptr);
                return alloca;
            }
        }
        // Suppress noise from stale VarRefs left over by mono specialization
        // of `Option<void>` / `Result<void, …>` etc. — pattern variable
        // bindings for void payloads (e.g. `Option::Some(v) => return v`)
        // become unreachable but mono still clones them. Print only under
        // an opt-in env var so debugging stays available.
        if (std::getenv("LOGOS_MLIRGEN_DEBUG_UNDEF"))
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    // Mutable tagged enum: load struct ptr from pointer slot.
    if (var_tagged_enum_ptr_.count(name))
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
    // Struct/class/array/tuple/tagged-enum/dyn-trait variables: return pointer directly.
    if (var_struct_.count(name) || var_class_.count(name))
        return get_struct_ptr(name);
    if (var_subscript_.count(name) || var_tuple_.count(name) ||
        var_tagged_enum_.count(name) || var_dyn_trait_.count(name))
        return it->second;
    // Let-bound scalar: load from alloca.
    if (let_vars_.count(name)) {
        auto et = var_elem_types_.find(name);
        if (et == var_elem_types_.end()) return nullptr;
        return builder_.create<mlir::LLVM::LoadOp>(loc_, et->second, it->second);
    }
    // Parameter SSA value.
    return it->second;
}

// ---------------------------------------------------------------------------
// Enum literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EEnumLitView v, TypeRef type) {
    std::string enum_name(v.enum_name());
    int64_t     disc = v.disc();
    // Tagged enum without payload (e.g. Option::None, HttpError::Io):
    // heap-allocate so the pointer can safely escape — including being stored
    // into another enum's payload slot as a pointer (EEnumLitData path).
    auto* te = resolve_tagged_enum(enum_name, type);
    if (te) {
        mlir::Value size = sizeof_struct(te->llvm_type);
        auto heap = call_malloc(size);
        if (!heap) return nullptr;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(0)};
        auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, heap, idx);
        auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, disc_val, disc_ptr);
        return heap;
    }
    // C-style enum: just the discriminant, sized per backing type.
    return builder_.create<mlir::arith::ConstantIntOp>(
        loc_, disc, enum_disc_bits(enum_name));
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EEnumLitDataView v, TypeRef type) {
    std::string enum_name(v.enum_name());
    int64_t disc = v.disc();
    std::vector<lir_view::ExprRef> payload;
    v.each_payload([&](lir_view::ExprRef pr){ payload.push_back(pr); });

    auto* te = resolve_tagged_enum(enum_name, type);
    if (!te) {
        std::fprintf(stderr, "mlir_gen: unknown tagged enum '%s'\n", enum_name.c_str());
        return nullptr;
    }
    auto& info = *te;
    // Allocate the enum struct on the heap (malloc) so it survives function returns
    mlir::Value size = sizeof_struct(info.llvm_type);
    auto alloca = call_malloc(size);
    if (!alloca) return nullptr;
    // Store discriminant at field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> disc_idx{int32_t(0), int32_t(0)};
    auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), info.llvm_type, alloca, disc_idx);
    auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
    builder_.create<mlir::LLVM::StoreOp>(loc_, disc_val, disc_ptr);
    // Store payload into field 1 (the [N x i8] area), bitcasted
    if (!payload.empty()) {
        // GEP to the payload area (field index 1)
        llvm::SmallVector<mlir::LLVM::GEPArg> pay_idx{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), info.llvm_type, alloca, pay_idx);
        // Find the variant's field types
        const TaggedEnumInfo::VariantPayload* vp = nullptr;
        for (auto& vi : info.variants)
            if (vi.disc == disc) { vp = &vi; break; }
        if (vp) {
            // Build a struct type for this variant's payload
            llvm::SmallVector<mlir::Type> ft;
            for (auto& t : vp->field_types) ft.push_back(t);
            auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), ft);
            for (size_t i = 0; i < payload.size() && i < vp->field_types.size(); ++i) {
                auto* pl_le = lexpr_of(payload[i]);
                if (!pl_le) return nullptr;
                auto val = gen_expr(*pl_le);
                if (!val) return nullptr;
                llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(i)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), pay_struct, pay_ptr, fi);
                // For inline structs, val is a *Struct pointer; copy bytes into payload.
                TypeRef lt = i < vp->logos_types.size() ? vp->logos_types[i] : nullptr;
                bool is_inline = lt && (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                                        TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                                        TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                                        TypeRef(lt).kind() == LogosType::Kind::Slice ||
                                        TypeRef(lt).kind() == LogosType::Kind::Closure);
                if (is_inline) {
                    std::unordered_set<std::string> seen;
                    uint64_t sz = logos_abi_byte_size(lt, seen);
                    auto sz_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)sz, 64);
                    // val may be a struct value (e.g. result of a function
                    // call returning a struct) rather than a pointer. memcpy
                    // requires a pointer source — spill aggregate values.
                    if (mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()))
                        val = spill_to_alloca(val);
                    builder_.create<mlir::LLVM::MemcpyOp>(loc_, fp, val, sz_val, false);
                } else {
                    builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_int(val, vp->field_types[i]), fp);
                }
            }
        }
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Binary / Unary operators
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EBinOpView v, TypeRef) {
    auto* lhs_l = lexpr_of(v.lhs());
    auto* rhs_l = lexpr_of(v.rhs());
    if (!lhs_l || !rhs_l) return nullptr;
    std::string op{v.op()};
    auto lhs = gen_expr(*lhs_l);
    if (!lhs) return nullptr;

    // Short-circuit operators: evaluate RHS only when LHS doesn't determine the result.
    // && : if LHS is false, result is false (skip RHS)
    // || : if LHS is true,  result is true  (skip RHS)
    if (op == "&&" || op == "||") {
        auto i1 = builder_.getI1Type();
        auto result_alloca = create_entry_alloca(i1);

        auto* region      = builder_.getBlock()->getParent();
        auto* rhs_block   = new mlir::Block();
        auto* sc_block    = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(rhs_block);
        region->push_back(sc_block);
        region->push_back(merge_block);

        // && : evaluate RHS when LHS=true; short-circuit to false when LHS=false
        // || : evaluate RHS when LHS=false; short-circuit to true  when LHS=true
        if (op == "&&")
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, rhs_block, sc_block);
        else
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, sc_block, rhs_block);

        // Short-circuit block: store the known result without evaluating RHS.
        builder_.setInsertionPointToStart(sc_block);
        auto sc_val = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, (op == "||") ? 1 : 0, 1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, sc_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // RHS block: evaluate RHS, store its value.
        builder_.setInsertionPointToStart(rhs_block);
        auto rhs_val = gen_expr(*rhs_l);
        if (!rhs_val)
            rhs_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, rhs_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, i1, result_alloca);
    }

    auto rhs = gen_expr(*rhs_l);
    if (!rhs) return nullptr;
    // Widen narrower integer operand, using zero-extend for unsigned types.
    if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
        if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
            if (li.getWidth() < ri.getWidth()) {
                bool lhs_unsigned = lhs_l->type &&
                    (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8   ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U16  ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U32  ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U24  ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U56  ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U64  ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::U128 ||
                     TypeRef(lhs_l->type).kind() == LogosType::Kind::Bool);
                if (lhs_unsigned)
                    lhs = builder_.create<mlir::arith::ExtUIOp>(loc_, rhs.getType(), lhs);
                else
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
            } else if (ri.getWidth() < li.getWidth()) {
                bool rhs_unsigned = rhs_l->type &&
                    (TypeRef(rhs_l->type).kind() == LogosType::Kind::U8   ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U16  ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U32  ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U24  ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U56  ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U64  ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::U128 ||
                     TypeRef(rhs_l->type).kind() == LogosType::Kind::Bool);
                if (rhs_unsigned)
                    rhs = builder_.create<mlir::arith::ExtUIOp>(loc_, lhs.getType(), rhs);
                else
                    rhs = builder_.create<mlir::arith::ExtSIOp>(loc_, lhs.getType(), rhs);
            }
        }
    }
    // Unify operand types for mixed arithmetic:
    // float+int → convert int to float; float+float of different widths → widen narrower.
    if (mlir::isa<mlir::FloatType>(lhs.getType()) &&
        mlir::isa<mlir::IntegerType>(rhs.getType())) {
        bool rhs_unsigned = rhs_l->type &&
            (TypeRef(rhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(rhs_l->type).kind() == LogosType::Kind::U128);
        if (rhs_unsigned)
            rhs = builder_.create<mlir::arith::UIToFPOp>(loc_, lhs.getType(), rhs);
        else
            rhs = builder_.create<mlir::arith::SIToFPOp>(loc_, lhs.getType(), rhs);
    }
    if (mlir::isa<mlir::IntegerType>(lhs.getType()) &&
        mlir::isa<mlir::FloatType>(rhs.getType())) {
        bool lhs_unsigned = lhs_l->type &&
            (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128);
        if (lhs_unsigned)
            lhs = builder_.create<mlir::arith::UIToFPOp>(loc_, rhs.getType(), lhs);
        else
            lhs = builder_.create<mlir::arith::SIToFPOp>(loc_, rhs.getType(), lhs);
    }
    // float+float of different widths: convert the FloatLit operand to match the typed one.
    // If both are typed floats of different widths, widen the narrower.
    if (lhs.getType() != rhs.getType()) {
        auto lft = mlir::dyn_cast<mlir::FloatType>(lhs.getType());
        auto rft = mlir::dyn_cast<mlir::FloatType>(rhs.getType());
        if (lft && rft) {
            bool lhs_is_lit = lhs_l->type && TypeRef(lhs_l->type).kind() == LogosType::Kind::FloatLit;
            bool rhs_is_lit = rhs_l->type && TypeRef(rhs_l->type).kind() == LogosType::Kind::FloatLit;
            if (rhs_is_lit && !lhs_is_lit) {
                // rhs is FloatLit, lhs is typed: coerce rhs to lhs type
                rhs = coerce_float(rhs, lhs.getType());
            } else if (lhs_is_lit && !rhs_is_lit) {
                // lhs is FloatLit, rhs is typed: coerce lhs to rhs type
                lhs = coerce_float(lhs, rhs.getType());
            } else {
                // Both typed floats: widen the narrower
                if (lft.getWidth() < rft.getWidth())
                    lhs = builder_.create<mlir::arith::ExtFOp>(loc_, rhs.getType(), lhs);
                else
                    rhs = builder_.create<mlir::arith::ExtFOp>(loc_, lhs.getType(), rhs);
            }
        }
    }
    bool is_float = mlir::isa<mlir::FloatType>(lhs.getType());
    if (is_float) {
        if (op == "+")  return builder_.create<mlir::arith::AddFOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubFOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulFOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivFOp>(loc_, lhs, rhs);
        if (op == "%")  return builder_.create<mlir::arith::RemFOp>(loc_, lhs, rhs);
        if (op == "==") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OEQ, lhs, rhs);
        if (op == "!=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::ONE, lhs, rhs);
        if (op == "<")  return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OLT, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OGT, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OLE, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OGE, lhs, rhs);
    }
    // B-ex-01: runtime overflow trap on integer +/-/*. Replace silent-wrap
    // arith ops with LLVM with-overflow intrinsics + cond_br + llvm.intr.trap
    // (SIGILL on overflow). Intentional wrapping — hashing, modular arith,
    // parsers — must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul`
    // intrinsic family which emits the silent arith op directly.
    if (op == "+" || op == "-" || op == "*") {
        bool is_unsigned = lhs_l->type &&
            (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::Usize);
        auto int_ty = mlir::dyn_cast<mlir::IntegerType>(lhs.getType());
        if (int_ty) {
            mlir::Type i1 = builder_.getI1Type();
            mlir::Type result_struct = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), {int_ty, i1});
            mlir::Operation* intr = nullptr;
            if (op == "+") intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::UAddWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SAddWithOverflowOp>(loc_, result_struct, lhs, rhs);
            else if (op == "-") intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::USubWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SSubWithOverflowOp>(loc_, result_struct, lhs, rhs);
            else intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::UMulWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SMulWithOverflowOp>(loc_, result_struct, lhs, rhs);
            mlir::Value result_v = builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, intr->getResult(0), llvm::ArrayRef<int64_t>{0});
            mlir::Value ovf_v = builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, intr->getResult(0), llvm::ArrayRef<int64_t>{1});
            auto* parent_region = builder_.getInsertionBlock()->getParent();
            auto* trap_block = new mlir::Block();
            auto* cont_block = new mlir::Block();
            parent_region->getBlocks().push_back(trap_block);
            parent_region->getBlocks().push_back(cont_block);
            builder_.create<mlir::cf::CondBranchOp>(loc_, ovf_v, trap_block, cont_block);
            builder_.setInsertionPointToStart(trap_block);
            builder_.create<mlir::LLVM::Trap>(loc_);
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
            builder_.setInsertionPointToStart(cont_block);
            return result_v;
        }
        if (op == "+") return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-") return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
    }
    {
        bool is_unsigned = lhs_l->type &&
            (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128);
        if (op == "/") {
            if (is_unsigned) return builder_.create<mlir::arith::DivUIOp>(loc_, lhs, rhs);
            return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);
        }
        if (op == "%") {
            if (is_unsigned) return builder_.create<mlir::arith::RemUIOp>(loc_, lhs, rhs);
            return builder_.create<mlir::arith::RemSIOp>(loc_, lhs, rhs);
        }
    }
    if (op == "&&") return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
    if (op == "||") return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
    if (op == "&")  return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
    if (op == "|")  return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
    if (op == "^")  return builder_.create<mlir::arith::XOrIOp>(loc_, lhs, rhs);
    if (op == "<<") return builder_.create<mlir::arith::ShLIOp>(loc_, lhs, rhs);
    if (op == ">>") {
        auto it = mlir::dyn_cast<mlir::IntegerType>(lhs.getType());
        bool is_unsigned = it && (lhs_l->type &&
            (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128));
        if (is_unsigned)
            return builder_.create<mlir::arith::ShRUIOp>(loc_, lhs, rhs);
        return builder_.create<mlir::arith::ShRSIOp>(loc_, lhs, rhs);
    }
    // CP-cm-08: tuple == / != — emit per-field load + cmp, AND together.
    // Without this, Kind::Tuple lowers to ptr_type (the by-pointer ABI for
    // anonymous LLVM struct values), and the generic is_ptr_cmp branch
    // below compares the pointers to the tuple slots — false for any two
    // tuple values held in distinct memory even when contents match.
    // Limitation: primitive-only fields. Tuples with str / nested-tuple /
    // struct fields fall through to the historic pointer-cmp behaviour;
    // follow-up will widen.
    if ((op == "==" || op == "!=") &&
        lhs_l->type && rhs_l->type &&
        TypeRef(lhs_l->type).kind() == LogosType::Kind::Tuple &&
        TypeRef(rhs_l->type).kind() == LogosType::Kind::Tuple) {
        auto le = TypeRef(lhs_l->type).tuple_elems();
        auto re = TypeRef(rhs_l->type).tuple_elems();
        if (le.size() == re.size() && !le.empty()) {
            // Check every field is primitive (handle nested/str later).
            auto is_prim = [](TypeRef t) {
                if (!t) return false;
                using K = LogosType::Kind;
                switch (t.kind()) {
                case K::I8:  case K::I16: case K::I24: case K::I32:
                case K::I56: case K::I64: case K::I128:
                case K::U8:  case K::U16: case K::U24: case K::U32:
                case K::U56: case K::U64: case K::U128:
                case K::F32: case K::F64:
                case K::Bool: case K::Char:
                case K::Usize: case K::Isize:
                case K::IntLit: case K::FloatLit:
                    return true;
                default: return false;
                }
            };
            bool all_prim = true;
            for (auto e : le) if (!is_prim(e)) { all_prim = false; break; }
            for (auto e : re) if (!is_prim(e)) { all_prim = false; break; }
            if (all_prim) {
                mlir::Type struct_ty = tuple_llvm_type(lhs_l->type);
                if (struct_ty) {
                    mlir::Value acc;
                    size_t idx = 0;
                    for (auto e : le) {
                        auto elem_t = logos_to_mlir(e);
                        if (!elem_t) { acc = nullptr; break; }
                        auto l_ptr = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), struct_ty, lhs,
                            llvm::ArrayRef<mlir::LLVM::GEPArg>{
                                0, (int32_t)idx});
                        auto r_ptr = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), struct_ty, rhs,
                            llvm::ArrayRef<mlir::LLVM::GEPArg>{
                                0, (int32_t)idx});
                        auto l_val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, elem_t, l_ptr);
                        auto r_val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, elem_t, r_ptr);
                        mlir::Value cmp;
                        if (mlir::isa<mlir::FloatType>(elem_t)) {
                            cmp = builder_.create<mlir::arith::CmpFOp>(
                                loc_, mlir::arith::CmpFPredicate::OEQ,
                                l_val, r_val);
                        } else {
                            cmp = builder_.create<mlir::arith::CmpIOp>(
                                loc_, mlir::arith::CmpIPredicate::eq,
                                l_val, r_val);
                        }
                        if (!acc) acc = cmp;
                        else acc = builder_.create<mlir::arith::AndIOp>(
                            loc_, acc, cmp);
                        ++idx;
                    }
                    if (acc) {
                        if (op == "==") return acc;
                        // != → XOR with true (i1 not).
                        auto true_c = builder_.create<mlir::arith::ConstantIntOp>(
                            loc_, 1LL, 1);
                        return builder_.create<mlir::arith::XOrIOp>(
                            loc_, acc, true_c);
                    }
                }
            }
        }
    }

    // For pointer comparisons, use llvm.icmp instead of arith.cmpi
    bool is_ptr_cmp = mlir::isa<mlir::LLVM::LLVMPointerType>(lhs.getType());
    // Rust-style auto-deref at `==` / `!=` for &T / &mut T when both
    // operands point at a primitive scalar. Closes the "ref-int" gap:
    // previously `&i32 == &i32` did pointer-equality (two refs to
    // distinct stack slots holding 1 returned false). Now matches the
    // PartialEq-for-&T blanket: dereference both sides first.
    if (is_ptr_cmp && (op == "==" || op == "!=")) {
        auto is_ref_to_prim = [](TypeRef t) -> TypeRef {
            if (!t) return TypeRef{};
            auto k = t.kind();
            if (k != LogosType::Kind::Ref && k != LogosType::Kind::MutRef)
                return TypeRef{};
            TypeRef pe = t.pointee();
            if (!pe) return TypeRef{};
            using K = LogosType::Kind;
            switch (pe.kind()) {
            case K::I8:  case K::I16: case K::I24: case K::I32: case K::I56:
            case K::I64: case K::I128:
            case K::U8:  case K::U16: case K::U24: case K::U32: case K::U56:
            case K::U64: case K::U128:
            case K::F32: case K::F64:
            case K::Bool:
            case K::IntLit: case K::FloatLit:
                return pe;
            default: return TypeRef{};
            }
        };
        TypeRef lhs_pe = is_ref_to_prim(lhs_l->type);
        TypeRef rhs_pe = is_ref_to_prim(rhs_l->type);
        if (lhs_pe && rhs_pe) {
            auto elem_t = logos_to_mlir(lhs_pe);
            if (elem_t) {
                lhs = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, lhs);
                rhs = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, rhs);
                is_ptr_cmp = false;
                // Override lhs_l->type / rhs_l->type pointee handling for
                // downstream signedness checks: not needed here since
                // CmpIPredicate::eq/ne are sign-agnostic.
            }
        }
    }
    if (op == "==") {
        if (is_ptr_cmp)
            return builder_.create<mlir::LLVM::ICmpOp>(
                loc_, mlir::LLVM::ICmpPredicate::eq, lhs, rhs);
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq,  lhs, rhs);
    }
    if (op == "!=") {
        if (is_ptr_cmp)
            return builder_.create<mlir::LLVM::ICmpOp>(
                loc_, mlir::LLVM::ICmpPredicate::ne, lhs, rhs);
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::ne,  lhs, rhs);
    }
    {
        bool is_unsigned_cmp = lhs_l->type &&
            (TypeRef(lhs_l->type).kind() == LogosType::Kind::U8  ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U16 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U32 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U24 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U56 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U64 ||
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128 ||
             // Bool lowers to LLVM i1. Signed i1 has `true`=−1, `false`=0,
             // which inverts `<` / `>` / `<=` / `>=` relative to Rust's
             // canonical `false < true` semantics. Treat as unsigned so
             // i1 `true`(1) > i1 `false`(0) as in Rust.
             TypeRef(lhs_l->type).kind() == LogosType::Kind::Bool);
        if (op == "<")  return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ult : mlir::arith::CmpIPredicate::slt, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ugt : mlir::arith::CmpIPredicate::sgt, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ule : mlir::arith::CmpIPredicate::sle, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::uge : mlir::arith::CmpIPredicate::sge, lhs, rhs);
    }
    std::fprintf(stderr, "mlir_gen: unknown op '%s'\n", op.c_str());
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EUnaryView v, TypeRef) {
    auto* operand = lexpr_of(v.operand());
    if (!operand) return nullptr;
    auto val = gen_expr(*operand);
    if (!val) return nullptr;
    auto op = v.op();
    if (op == "-") {
        if (mlir::isa<mlir::FloatType>(val.getType()))
            return builder_.create<mlir::arith::NegFOp>(loc_, val);
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, 0, mlir::cast<mlir::IntegerType>(val.getType()).getWidth());
        return builder_.create<mlir::arith::SubIOp>(loc_, zero, val);
    }
    if (op == "!") {
        auto itype = mlir::dyn_cast<mlir::IntegerType>(val.getType());
        if (!itype) {
            std::fprintf(stderr, "mlir_gen: unary '!' on non-integer type\n");
            return nullptr;
        }
        unsigned width = itype.getWidth();
        if (width == 1) {
            // bool: logical NOT via XOR with 1
            auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, one);
        } else {
            // integer: bitwise NOT via XOR with all-ones (-1)
            auto allones = builder_.create<mlir::arith::ConstantIntOp>(loc_, -1, width);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, allones);
        }
    }
    std::fprintf(stderr, "mlir_gen: unknown unary op '%.*s'\n",
                 int(op.size()), op.data());
    return nullptr;
}

// ---------------------------------------------------------------------------
// AddrOf / Deref
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAddrOfView v, TypeRef) {
    // Address-of: return the alloca pointer directly.
    std::string var_name{v.var_name()};
    auto it = scope_.find(var_name);
    if (it == scope_.end()) {
        // B98.2: module-level const — materialize a temporary stack slot
        // and store the const value, then return the slot address.
        auto cit = module_consts_.find(var_name);
        if (cit != module_consts_.end()) {
            auto val = gen_expr(*cit->second->value);
            if (!val) {
                std::fprintf(stderr, "mlir_gen: & const '%s' eval failed\n", var_name.c_str());
                return nullptr;
            }
            auto alloca = create_entry_alloca(val.getType());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            return alloca;
        }
        std::fprintf(stderr, "mlir_gen: & undefined '%s'\n", var_name.c_str());
        return nullptr;
    }
    // Fn parameters are bound as SSA values (not allocas).  Taking `&x` on a
    // scalar-typed param requires materializing an on-stack copy so callers
    // can receive a real pointer.
    if (it->second && it->second.getType() != ptr_type()) {
        auto alloca = create_entry_alloca(it->second.getType());
        builder_.create<mlir::LLVM::StoreOp>(loc_, it->second, alloca);
        return alloca;
    }
    // Ref/MutRef-typed param: `&p` means address of the param's own
    // storage slot, not the value it points at. Spill the SSA arg to
    // an entry alloca and return the alloca (closes B3-bg-03 / Sprint 6).
    // Replace the scope entry with the alloca so subsequent reads
    // and other `&p` calls in the same function body see the same
    // slot (and so the spill happens exactly once).
    if (it->second && ref_param_names_.count(var_name)) {
        auto alloca = create_entry_alloca(it->second.getType());
        builder_.create<mlir::LLVM::StoreOp>(loc_, it->second, alloca);
        it->second = alloca;
        ref_param_names_.erase(var_name);
        return alloca;
    }
    // L5: tagged-enum non-mut let stores the enum ptr directly in scope_
    // (no surrounding alloca slot). `&o` for `o: Option<i64>` would then
    // hand back the enum-struct ptr instead of a real ptr-to-ptr — match
    // call sites expecting `&Enum` to be ptr-to-ptr (via_ref load in
    // gen_match) then dereference garbage and SIGSEGV. Spill once on
    // first `&o` so the slot exists; replace the scope binding with the
    // slot (subsequent reads load through it). Mutable enum lets already
    // have a slot via var_tagged_enum_ptr_.
    if (it->second && var_tagged_enum_.count(var_name) &&
        !var_tagged_enum_ptr_.count(var_name)) {
        auto alloca = create_entry_alloca(ptr_type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, it->second, alloca);
        // Replace scope entry with the slot so future reads load
        // through it (and future `&` calls return this same slot).
        it->second = alloca;
        // Mark as "slot-backed" so subsequent reads know to load.
        var_tagged_enum_ptr_.insert(var_name);
        return alloca;
    }
    return it->second;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAddrOfTempView v, TypeRef) {
    namespace ec = lir_schema::expr;
    auto inner_ref = v.inner();
    auto* inner_le = lexpr_of(inner_ref);
    if (!inner_le) return nullptr;
    TypeRef inner_t = inner_ref.type(pool_impl());

    // Special case: &mut <field_read> on an inline struct field must return a
    // GEP into the original struct, NOT a copy.  gen_expr(EFieldRead) always
    // loads, which would give us a by-value copy — useless for mutation.
    if (inner_ref.kind() == ec::Code::FieldRead) {
        lir_view::EFieldReadView frv{inner_ref};
        auto* fr_recv_le = lexpr_of(frv.receiver());
        if (fr_recv_le) {
            auto [ptr, sname] = gen_recv_struct(*fr_recv_le);
            if (ptr && !sname.empty()) {
                auto sit = struct_types_.find(sname);
                if (sit != struct_types_.end()) {
                    auto& info = sit->second;
                    auto gep = gep_field(ptr, info, std::string(frv.field()));
                    if (gep) return gep;
                }
            }
        }
        // Fall through.
    }
    // `&mut arr[i]` on a struct-element-typed array/pointer: take GEP address
    // directly instead of loading the struct by value and then needing to re-spill.
    if (inner_ref.kind() == ec::Code::IndexRead && inner_t) {
        lir_view::EIndexReadView irv{inner_ref};
        auto ir_recv  = irv.receiver();
        auto ir_index = irv.index();
        TypeRef ir_recv_t = ir_recv.type(pool_impl());
        mlir::Value base_ptr;
        mlir::Type  elem_type;
        if (ir_recv.kind() == ec::Code::VarRef) {
            std::string vn(lir_view::EVarRefView{ir_recv}.name());
            auto lpit = var_local_ptrs_.find(vn);
            if (lpit != var_local_ptrs_.end()) {
                auto slot = get_subscript_ptr(vn);
                base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot);
                elem_type = lpit->second;
            } else if (ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Ptr && ir_recv_t.pointee()) {
                auto cname = concrete_struct_name(ir_recv_t.pointee());
                auto sit   = struct_types_.find(cname);
                if (sit != struct_types_.end()) {
                    auto sc = scope_.find(vn);
                    if (sc != scope_.end()) {
                        base_ptr  = sc->second;
                        elem_type = sit->second.llvm_type;
                    }
                }
            } else if (ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Array) {
                auto sp = get_subscript_ptr(vn);
                if (sp) {
                    base_ptr  = sp;
                    elem_type = logos_to_mlir(inner_t);
                }
            }
        } else if (ir_recv.kind() == ec::Code::FieldRead) {
            lir_view::EFieldReadView frv{ir_recv};
            auto* fr_recv_le = lexpr_of(frv.receiver());
            if (fr_recv_le) {
                auto [struct_ptr, sname] = gen_recv_struct(*fr_recv_le);
                if (struct_ptr && !sname.empty()) {
                    auto& info = struct_types_[sname];
                    auto field_ptr = gep_field(struct_ptr, info, std::string(frv.field()));
                    if (field_ptr) {
                        bool field_is_ptr = ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Ptr;
                        if (field_is_ptr) {
                            base_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                loc_, ptr_type(), field_ptr);
                            TypeRef rpt = ir_recv_t.pointee();
                            if (rpt &&
                                (rpt.kind() == LogosType::Kind::Struct ||
                                 rpt.kind() == LogosType::Kind::ZonedStruct)) {
                                auto cname = concrete_struct_name(rpt);
                                auto sit2  = struct_types_.find(cname);
                                if (sit2 != struct_types_.end())
                                    elem_type = sit2->second.llvm_type;
                            }
                            if (!elem_type)
                                elem_type = logos_to_mlir(inner_t);
                        } else {
                            base_ptr  = field_ptr;
                            elem_type = logos_to_mlir(inner_t);
                        }
                    }
                }
            }
        }
        if (base_ptr && elem_type) {
            auto* ir_index_le = lexpr_of(ir_index);
            if (!ir_index_le) return nullptr;
            auto idx = gen_expr(*ir_index_le);
            if (!idx) return nullptr;
            TypeRef ir_idx_t = ir_index.type(pool_impl());
            bool idx_unsigned = ir_idx_t &&
                (ir_idx_t.kind() == LogosType::Kind::U8  ||
                 ir_idx_t.kind() == LogosType::Kind::U16 ||
                 ir_idx_t.kind() == LogosType::Kind::U32 ||
                 ir_idx_t.kind() == LogosType::Kind::U24 ||
                 ir_idx_t.kind() == LogosType::Kind::U56 ||
                 ir_idx_t.kind() == LogosType::Kind::U64 ||
                 ir_idx_t.kind() == LogosType::Kind::U128);
            if (idx_unsigned && idx.getType() != builder_.getI64Type())
                idx = builder_.create<mlir::arith::ExtUIOp>(
                    loc_, builder_.getI64Type(), idx);
            llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
            return builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), elem_type, base_ptr, indices);
        }
    }
    // SL-sl-03: `o.take()` → autoref'd `(&mut o).take()` — for a tagged-enum
    // local we must hand back the real slot, not a spilled copy of the
    // pointer, so the callee's `*self = …` rebind reaches the caller's
    // binding. Restricted to vars that genuinely live in a slot
    // (var_tagged_enum_ptr_); the broader VarRef carve-out broke
    // ref-to-struct lets whose scope_ entry holds the ref value directly.
    if (inner_ref.kind() == ec::Code::VarRef &&
        inner_t && inner_t.kind() == LogosType::Kind::Enum) {
        std::string vn{lir_view::EVarRefView{inner_ref}.name()};
        auto sc = scope_.find(vn);
        if (sc != scope_.end() && sc->second &&
            sc->second.getType() == ptr_type() &&
            var_tagged_enum_.count(vn)) {
            if (!var_tagged_enum_ptr_.count(vn)) {
                auto alloca = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sc->second, alloca);
                sc->second = alloca;
                var_tagged_enum_ptr_.insert(vn);
            }
            return sc->second;
        }
    }
    auto val = gen_expr(*inner_le);
    if (!val) return nullptr;
    if (inner_t && (inner_t.kind() == LogosType::Kind::Tuple ||
                    inner_t.kind() == LogosType::Kind::Struct ||
                    inner_t.kind() == LogosType::Kind::ZonedStruct ||
                    inner_t.kind() == LogosType::Kind::Array ||
                    // CP-cm-08b: Slice<T> values are already ptr-to-slice-desc
                    // (Logos ABI). Spilling them to an 8-byte alloca and
                    // passing the alloca address makes callees that expect
                    // "ptr to {ptr,i64}" read 16 bytes from an 8-byte slot.
                    // Treat as already-spilled — return the ptr value.
                    inner_t.kind() == LogosType::Kind::Slice ||
                    inner_t.kind() == LogosType::Kind::TraitObject))
        return val;
    auto llvm_type = logos_to_mlir(inner_t);
    if (!llvm_type) llvm_type = builder_.getI32Type();
    auto alloca = create_entry_alloca(llvm_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EDerefView v, TypeRef type) {
    auto* operand = lexpr_of(v.operand());
    if (!operand) return nullptr;
    auto ptr = gen_expr(*operand);
    if (!ptr) return nullptr;
    // Structs/datatypes are always pointer-represented in MLIR/LLVM; the
    // logical *-deref just yields the same pointer.  Subsequent field
    // access or the return-by-value wrap handles the byte-level copy.
    // (Previously only Struct was covered here — Datatype fell through to
    // the load branch, producing a bogus double-load through pass-by-ptr
    // parameters: `*const V3` was treated as `ptr-to-ptr-to-V3`.)
    if (type && (TypeRef(type).kind() == LogosType::Kind::Struct ||
                 TypeRef(type).kind() == LogosType::Kind::ZonedStruct ||
                 // Trait objects are fat-pointer-represented; the dyn-pair
                 // {data,vtable} lives in memory and dispatch reads fields
                 // through a pointer to it, so `*p` for `*const dyn T`
                 // should yield the same pointer (no double-load).
                 TypeRef(type).kind() == LogosType::Kind::TraitObject ||
                 // C6-cc-08 follow-up: `*p` for `p: *const [T; N]` — the array
                 // value is too large to "load by value"; we keep it pointer-
                 // represented so subsequent `(*p)[i]` indexing GEPs into it.
                 TypeRef(type).kind() == LogosType::Kind::Array))
        return ptr;
    auto pointee = logos_to_mlir(type);
    if (!pointee) pointee = builder_.getI32Type();
    return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
}

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type) {
    namespace ec = lir_schema::expr;
    std::string callee(v.callee());
    std::vector<lir_view::ExprRef> arg_refs;
    v.each_arg([&](lir_view::ExprRef ar){ arg_refs.push_back(ar); });
    std::vector<const LExpr*> arg_les;
    arg_les.reserve(arg_refs.size());
    for (auto& ar : arg_refs) {
        auto* le = lexpr_of(ar);
        if (!le) return nullptr;
        arg_les.push_back(le);
    }
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // ── Compiler intrinsics recognised by name ────────────────────────────────
    // wrapping_add / wrapping_sub / wrapping_mul — silent two's-complement
    // arithmetic that explicitly opts out of the runtime overflow trap on
    // `+`/`-`/`*`. Same signature as the built-in op; emits the silent
    // arith.* directly (B-ex-01).
    // After pkg-mangling and monomorphization the callee may take the
    // form `pkg$<name>__g__<sig>` or `<name>__g__<sig>` (or just bare
    // `<name>` pre-mono). Strip pkg prefix before matching.
    auto is_wrapping_intr = [&](std::string_view name) {
        auto dollar = callee.rfind('$');
        std::string_view suffix = (dollar == std::string::npos)
            ? std::string_view{callee}
            : std::string_view{callee}.substr(dollar + 1);
        return suffix == name ||
               (suffix.size() > name.size() + 4 &&
                suffix.compare(0, name.size(), name) == 0 &&
                suffix.compare(name.size(), 4, "__g_") == 0);
    };
    if (is_wrapping_intr("wrapping_add") || is_wrapping_intr("wrapping_sub") || is_wrapping_intr("wrapping_mul")) {
        std::string_view base_op =
            is_wrapping_intr("wrapping_add") ? "wrapping_add" :
            is_wrapping_intr("wrapping_sub") ? "wrapping_sub" : "wrapping_mul";
        if (arg_les.size() == 2) {
            auto a = gen_expr(*arg_les[0]); if (!a) return nullptr;
            auto b = gen_expr(*arg_les[1]); if (!b) return nullptr;
            // Coerce types so the arith op sees matching integer widths.
            if (a.getType() != b.getType()) {
                if (auto ai = mlir::dyn_cast<mlir::IntegerType>(a.getType())) {
                    if (auto bi = mlir::dyn_cast<mlir::IntegerType>(b.getType())) {
                        if (ai.getWidth() < bi.getWidth())
                            a = builder_.create<mlir::arith::ExtUIOp>(loc_, b.getType(), a);
                        else if (bi.getWidth() < ai.getWidth())
                            b = builder_.create<mlir::arith::ExtUIOp>(loc_, a.getType(), b);
                    }
                }
            }
            if (base_op == "wrapping_add")
                return builder_.create<mlir::arith::AddIOp>(loc_, a, b);
            if (base_op == "wrapping_sub")
                return builder_.create<mlir::arith::SubIOp>(loc_, a, b);
            return builder_.create<mlir::arith::MulIOp>(loc_, a, b);
        }
    }

    // After pkg-mangling, intrinsic names ship as
    // `std.lang.text$str_from_raw__f__pcst_u8__i64` etc. Strip pkg
    // prefix and the `__f__<sig>` / `__g__<sig>` suffix to recover
    // the bare name for matching against the known-intrinsic set.
    auto bare_intrinsic = [&]() -> std::string {
        auto dollar = callee.rfind('$');
        std::string_view body = (dollar == std::string::npos)
            ? std::string_view{callee}
            : std::string_view{callee}.substr(dollar + 1);
        if (auto p = body.find("__f__"); p != std::string::npos)
            return std::string(body.substr(0, p));
        if (auto p = body.find("__g__"); p != std::string::npos)
            return std::string(body.substr(0, p));
        return std::string(body);
    }();

    // str_from_raw(ptr: *const u8, len: i64) -> str
    // Constructs a str fat-pointer {ptr, len} on the stack, mirroring ELitStr.
    if (bare_intrinsic == "str__str_from_raw" || bare_intrinsic == "str_from_raw") {
        if (arg_les.size() == 2) {
            auto ptr_v = gen_expr(*arg_les[0]); if (!ptr_v) return nullptr;
            auto len_v = gen_expr(*arg_les[1]); if (!len_v) return nullptr;
            auto stype  = slice_llvm_type();
            auto alloca = create_entry_alloca(stype);
            llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
            auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
            builder_.create<mlir::LLVM::StoreOp>(loc_, ptr_v, pp);
            llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
            auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
            auto len_i64 = coerce_numeric(len_v, builder_.getIntegerType(64));
            builder_.create<mlir::LLVM::StoreOp>(loc_, len_i64, lp);
            return alloca;
        }
    }

    // Bitwise intrinsics on u64 — emit LLVM dialect ops.
    // popcount/ctlz/cttz return i64 from i64 input; truncate to i32 for u32 return.
    if (bare_intrinsic == "popcount_u64"        || bare_intrinsic == "leading_zeros_u64"  ||
        bare_intrinsic == "trailing_zeros_u64"  || bare_intrinsic == "bswap_u64"          ||
        bare_intrinsic == "bitreverse_u64") {
        if (arg_les.size() == 1) {
            auto v = gen_expr(*arg_les[0]); if (!v) return nullptr;
            auto i64_ty = builder_.getIntegerType(64);
            auto i32_ty = builder_.getIntegerType(32);
            v = coerce_int(v, i64_ty);
            mlir::Value res;
            if (bare_intrinsic == "popcount_u64")
                res = builder_.create<mlir::LLVM::CtPopOp>(loc_, i64_ty, v);
            else if (bare_intrinsic == "leading_zeros_u64")
                res = builder_.create<mlir::LLVM::CountLeadingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (bare_intrinsic == "trailing_zeros_u64")
                res = builder_.create<mlir::LLVM::CountTrailingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (bare_intrinsic == "bswap_u64")
                res = builder_.create<mlir::LLVM::ByteSwapOp>(loc_, i64_ty, v);
            else // bitreverse_u64
                res = builder_.create<mlir::LLVM::BitReverseOp>(loc_, i64_ty, v);
            // popcount/ctlz/cttz: Logos return type is u32; truncate.
            if (bare_intrinsic == "popcount_u64"       ||
                bare_intrinsic == "leading_zeros_u64"  ||
                bare_intrinsic == "trailing_zeros_u64")
                res = coerce_int(res, i32_ty);
            return res;
        }
    }

    // Check if this is a vararg extern fn (declared as llvm.func)
    if (vararg_fns_.count(callee)) {
        auto callee_fn = parent_mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(callee);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: undefined vararg function '%s'\n", callee.c_str());
            return nullptr;
        }
        llvm::SmallVector<mlir::Value> args;
        auto fn_type   = callee_fn.getFunctionType();
        auto fixed_inputs = fn_type.getParams();
        for (size_t i = 0; i < arg_les.size(); ++i) {
            auto v = gen_expr(*arg_les[i]);
            if (!v) return nullptr;
            if (i < fixed_inputs.size()) v = coerce_numeric(v, fixed_inputs[i]);
            args.push_back(v);
        }
        mlir::Type ret_type = fn_type.getReturnType();
        bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret_type);
        auto call = builder_.create<mlir::LLVM::CallOp>(
            loc_, fn_type, callee_fn.getName(), mlir::ValueRange(args));
        if (is_void) return nullptr;
        mlir::Value res = call.getResult();
        return res ? res : nullptr;
    }

    auto callee_fn  = find_func_op(parent_mod, callee);
    if (!callee_fn) {
        auto gpos = callee.find("__g__");
        if (gpos != std::string::npos)
            callee_fn = find_func_op(parent_mod, callee.substr(0, gpos));
        // Pkg-mangled generic call (`pkg$base__g__sig`) whose terminator
        // overload is registered under the bare name `base` (the first-
        // registered overload of a same-name pair stays bare when the
        // second one is generic — sema_collect's demote-rename only
        // fires for two non-generic overloads). Strip pkg + g-suffix
        // and retry.
        if (!callee_fn && gpos != std::string::npos) {
            auto dollar = callee.rfind('$', gpos);
            if (dollar != std::string::npos)
                callee_fn = find_func_op(parent_mod,
                    callee.substr(dollar + 1, gpos - dollar - 1));
        }
        // Variadic-shrinking fallback: a recursive call inside a
        // variadic-pack-expand template carries the literal generic
        // mangling `…__g__sig__H__T` that no concrete clone matches.
        // The actual terminator is a sibling **non-generic** overload
        // of the same base whose own symbol is mangled with `__f__sig`.
        // Strip `__g__sig` and search for any `<base>__f__…` defined.
        if (!callee_fn && gpos != std::string::npos) {
            std::string base_with_pkg = callee.substr(0, gpos);
            std::string fn_prefix_pkg = base_with_pkg + "__f__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    return fn.getName().str().rfind(fn_prefix_pkg, 0) == 0;
                });
            if (!callee_fn) {
                auto dollar = base_with_pkg.rfind('$');
                if (dollar != std::string::npos) {
                    std::string fn_prefix_bare =
                        base_with_pkg.substr(dollar + 1) + "__f__";
                    callee_fn = find_fn_matching(parent_mod,
                        [&](mlir::func::FuncOp fn) {
                            return fn.getName().str().rfind(fn_prefix_bare, 0) == 0;
                        });
                }
            }
        }
        if (!callee_fn) {
            std::string generic_prefix = callee + "__g__";
            std::string fn_prefix      = callee + "__f__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    auto n = fn.getName().str();
                    return n.rfind(generic_prefix, 0) == 0 ||
                           n.rfind(fn_prefix, 0) == 0;
                });
        }
        if (!callee_fn) {
            std::string contains_f = "." + callee + "__f__";
            std::string contains_g = "." + callee + "__g__";
            std::string ends_dot = "." + callee;
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    auto n = fn.getName().str();
                    bool ends = n.size() >= ends_dot.size() &&
                                n.compare(n.size() - ends_dot.size(),
                                          ends_dot.size(), ends_dot) == 0;
                    return ends ||
                           n.find(contains_f) != std::string::npos ||
                           n.find(contains_g) != std::string::npos;
                });
        }
        if (!callee_fn) {
            std::string callee_prefix = callee + "__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    return fn.getName().str().rfind(callee_prefix, 0) == 0;
                });
        }
    }
    if (!callee_fn) {
        llvm::SmallVector<mlir::Value> args;
        for (size_t i = 0; i < arg_les.size(); ++i) {
            auto v = gen_expr(*arg_les[i]);
            if (!v) return nullptr;
            args.push_back(v);
        }
        llvm::SmallVector<mlir::Type> result_types;
        if (ret_logos_type) {
            auto ret_mlir = logos_to_mlir(ret_logos_type);
            if (ret_mlir)
                result_types.push_back(ret_mlir);
        }
        auto call = builder_.create<mlir::func::CallOp>(
            loc_, callee, result_types, mlir::ValueRange(args));
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    auto param_types = callee_fn.getFunctionType().getInputs();
    // Look up Logos-level param types for dyn coercion
    auto fpit = fn_param_types_.find(callee);
    for (size_t i = 0; i < arg_les.size(); ++i) {
        mlir::Value v;
        // When the callee expects a pointer and the arg is an EFieldRead of an
        // inline-embedded struct, pass the field's GEP directly instead of
        // load+spill. This ensures mutations (e.g. &mut self.inner) write back
        // to the original struct, not a disconnected alloca copy.
        if (i < param_types.size() && param_types[i] == ptr_type() &&
            arg_refs[i].kind() == ec::Code::FieldRead) {
            lir_view::EFieldReadView frv{arg_refs[i]};
            auto* fr_recv_le = lexpr_of(frv.receiver());
            std::string fr_field(frv.field());
            if (fr_recv_le) {
                auto [base_ptr, base_sname] = gen_recv_struct(*fr_recv_le);
                if (base_ptr && !base_sname.empty()) {
                    auto bit = struct_types_.find(base_sname);
                    if (bit != struct_types_.end()) {
                        auto gep = gep_field(base_ptr, bit->second, fr_field);
                        if (gep) {
                            for (auto& f : bit->second.fields) {
                                if (f.name == fr_field &&
                                    mlir::isa<mlir::LLVM::LLVMStructType>(f.type)) {
                                    v = gep;
                                    goto arg_push;
                                }
                            }
                        }
                    }
                }
            }
        }
        v = gen_expr(*arg_les[i]);
        if (!v) return nullptr;
    arg_push:
        // Coerce concrete struct/class → &dyn Trait if param expects it.
        // Box<T> is laid out as { *mut T } so the box value *is* the data pointer;
        // use T as the vtable key so the impl on T (not Box<T>) is looked up.
        if (fpit != fn_param_types_.end() && i < fpit->second.size()) {
            auto param_lt = fpit->second[i];
            auto arg_lt = arg_refs[i].type(pool_impl());
            if (param_lt && TypeRef(param_lt).kind() == LogosType::Kind::TraitObject &&
                arg_lt && TypeRef(arg_lt).kind() != LogosType::Kind::TraitObject) {
                TypeRef vt_type = arg_lt;
                // C6-cc-09: `&T` / `&mut T` over a struct → &dyn Trait. The
                // ref value is already a data pointer at the LLVM level; unwrap
                // the pointee to look up the impl on T (not &T).
                if (TypeRef(vt_type).kind() == LogosType::Kind::Ref ||
                    TypeRef(vt_type).kind() == LogosType::Kind::MutRef)
                    vt_type = TypeRef(vt_type).pointee();
                if (TypeRef(vt_type).kind() == LogosType::Kind::Struct &&
                    TypeRef(vt_type).struct_name() == "Box" &&
                    TypeRef(vt_type).type_args().size() == 1)
                    vt_type = TypeRef(vt_type).type_args()[0];
                // If the source is a struct value (not a pointer) — applies to
                // bare-struct → &dyn (the original C6-cc-09 surface) — spill
                // so coerce_to_dyn has something to store as data ptr. Existing
                // Box-source path: the Box value is already a 1-field struct
                // whose payload is the data pointer, so the underlying LLVM
                // value flows verbatim; only spill when the source isn't a
                // ref/Box (i.e. genuine struct value).
                if (v.getType() != ptr_type() &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::Ref &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::MutRef &&
                    !(TypeRef(arg_lt).kind() == LogosType::Kind::Struct &&
                      TypeRef(arg_lt).struct_name() == "Box"))
                    v = spill_to_alloca(v);
                v = coerce_to_dyn(v, std::string(TypeRef(param_lt).trait_name()), type_str(vt_type));
            }
        }
        if (i < param_types.size()) {
            // Aggregate returned by value but param expects pointer — spill to alloca.
            if (v.getType() != param_types[i] &&
                param_types[i] == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
                v = spill_to_alloca(v);
            else if (v.getType() != ptr_type())
                v = coerce_numeric(v, param_types[i], arg_refs[i].type(pool_impl()));
        }
        args.push_back(v);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type) {
    std::string method(v.method());
    std::string tag_system(v.tag_system());
    std::string resolved_type(v.resolved_type());
    std::string resolved_symbol(v.resolved_symbol());
    int32_t     vtable_index = v.vtable_index();
    auto recv_ref = v.receiver();
    auto* recv_le = lexpr_of(recv_ref);
    if (!recv_le) return nullptr;
    TypeRef recv_t = recv_ref.type(pool_impl());

    std::vector<lir_view::ExprRef>    arg_refs;
    v.each_arg([&](lir_view::ExprRef ar){ arg_refs.push_back(ar); });
    std::vector<const LExpr*> arg_les;
    arg_les.reserve(arg_refs.size());
    for (auto& ar : arg_refs) {
        auto* le = lexpr_of(ar);
        if (!le) return nullptr;
        arg_les.push_back(le);
    }

    if (method == "as_offset" && recv_t) {
        bool is_anyval =
            type_str(recv_t) == "AnyVal" ||
            ((recv_t.kind() == LogosType::Kind::Ptr ||
              recv_t.kind() == LogosType::Kind::Ref ||
              recv_t.kind() == LogosType::Kind::MutRef) &&
             recv_t.pointee() && type_str(recv_t.pointee()) == "AnyVal");
        if (is_anyval) {
            auto recv = gen_expr(*recv_le);
            if (!recv) return nullptr;
            if (recv.getType() == ptr_type())
                return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
            return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    if (!tag_system.empty())
        return gen_tagged_dispatch(v, ret_logos_type);
    if (recv_t && recv_t.kind() == LogosType::Kind::TraitObject && vtable_index >= 0)
        return gen_dyn_dispatch(v, ret_logos_type);
    auto [ptr, tname] = gen_recv_struct(*recv_le);
    if (!ptr || tname.empty()) return nullptr;
    if (strip_struct_pkg(tname) == "AnyVal" && ptr.getType() != ptr_type()) {
        auto slot = create_entry_alloca(builder_.getI32Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_numeric(ptr, builder_.getI32Type()), slot);
        ptr = slot;
    }
    // Method symbols are pkg-qualified at sema (`pkg.Concrete__method__f__sig`).
    // Build qualified callee from receiver tname's pkg prefix; fall back to
    // bare and to a global suffix scan when needed.
    std::string defining = resolved_type.empty()
                           ? std::string(strip_struct_pkg(tname))
                           : resolved_type;
    std::string tname_pkg;
    // Pkg may have inner dots; split at LAST dot.
    if (auto p = tname.rfind('.'); p != std::string::npos)
        tname_pkg = tname.substr(0, p);
    std::string bare_mangled = defining + "__" + method;
    auto mangled = tname_pkg.empty()
                   ? bare_mangled
                   : tname_pkg + "." + bare_mangled;

    auto callee_name = resolved_symbol.empty() ? mangled : resolved_symbol;
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto callee_fn   = find_func_op(parent_mod, callee_name);
    auto walk_prefix = [&](const std::string& cn) -> mlir::func::FuncOp {
        std::string generic_prefix = cn + "__g__";
        std::string fn_prefix = cn + "__f__";
        return find_fn_matching(parent_mod,
            [&](mlir::func::FuncOp fn) {
                auto n = fn.getName().str();
                return n.rfind(generic_prefix, 0) == 0 ||
                       n.rfind(fn_prefix, 0) == 0;
            });
    };
    if (!callee_fn) callee_fn = walk_prefix(callee_name);
    if (!callee_fn && !resolved_symbol.empty()) {
        callee_name = mangled;
        callee_fn = find_func_op(parent_mod, callee_name);
        if (!callee_fn) callee_fn = walk_prefix(callee_name);
    }
    if (!callee_fn && mangled != bare_mangled) {
        callee_name = bare_mangled;
        callee_fn = find_func_op(parent_mod, callee_name);
        if (!callee_fn) callee_fn = walk_prefix(callee_name);
    }
    if (!callee_fn) {
        std::string suffix1 = "." + bare_mangled;
        std::string contains_f = "." + bare_mangled + "__f__";
        std::string contains_g = "." + bare_mangled + "__g__";
        callee_fn = find_fn_matching(parent_mod,
            [&](mlir::func::FuncOp fn) {
                auto n = fn.getName().str();
                bool ends = n.size() >= suffix1.size() &&
                            n.compare(n.size() - suffix1.size(),
                                      suffix1.size(), suffix1) == 0;
                return ends ||
                       n.find(contains_f) != std::string::npos ||
                       n.find(contains_g) != std::string::npos;
            });
        if (callee_fn) callee_name = callee_fn.getName().str();
    }
    if (!callee_fn) {
        std::fprintf(stderr, "mlir_gen: method '%s' not found\n", callee_name.c_str());
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    args.push_back(ptr);
    auto param_types = callee_fn.getFunctionType().getInputs();
    for (size_t i = 0; i < arg_les.size(); ++i) {
        auto val = gen_expr(*arg_les[i]);
        if (!val) return nullptr;
        size_t pi = i + 1;
        if (pi < param_types.size()) {
            if (val.getType() != param_types[pi] &&
                param_types[pi] == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()))
                val = spill_to_alloca(val);
            else
                val = coerce_numeric(val, param_types[pi], arg_refs[i].type(pool_impl()));
        }
        args.push_back(val);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Field / index reads
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFieldReadView v, TypeRef type) {
    auto* recv_l = lexpr_of(v.receiver());
    if (!recv_l) return nullptr;
    std::string field{v.field()};
    if (TypeRef rt(recv_l->type); field == "raw" && rt) {
        bool is_anyval = type_str(recv_l->type) == "AnyVal";
        bool is_anyval_ptr = (rt.kind() == LogosType::Kind::Ptr ||
                              rt.kind() == LogosType::Kind::Ref ||
                              rt.kind() == LogosType::Kind::MutRef) &&
                             rt.pointee() &&
                             type_str(rt.pointee()) == "AnyVal";
        if (is_anyval || is_anyval_ptr) {
            auto recv = gen_expr(*recv_l);
            if (!recv) return nullptr;
            if (recv.getType() == ptr_type())
                return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
            return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    auto [ptr, sname] = gen_recv_struct(*recv_l);
    if (!ptr || sname.empty()) return nullptr;
    auto& info = struct_types_[sname];
    auto gep   = gep_field(ptr, info, field);
    if (!gep) return nullptr;
    for (auto& f : info.fields)
        if (f.name == field)
            return builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, gep);
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EIndexReadView v, TypeRef type) {
    namespace ec = lir_schema::expr;
    auto recv_ref = v.receiver();
    auto idx_ref  = v.index();
    auto* recv_le = lexpr_of(recv_ref);
    auto* idx_le  = lexpr_of(idx_ref);
    if (!recv_le || !idx_le) return nullptr;
    TypeRef recv_t = recv_ref.type(pool_impl());
    TypeRef idx_t  = idx_ref.type(pool_impl());

    mlir::Value arr_ptr;
    mlir::Type  elem_type;

    switch (recv_ref.kind()) {
    case ec::Code::VarRef: {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        // Module constant carrying an array literal — re-materialise the
        // value as a fresh on-stack alloca, walk into it like a normal
        // local. The const's TypeRef is what drives elem_type.
        if (auto cit = module_consts_.find(name); cit != module_consts_.end()) {
            arr_ptr = gen_expr(*cit->second->value);
            if (!arr_ptr) return nullptr;
            TypeRef ct = cit->second->type;
            if (ct && TypeRef(ct).elem()) {
                elem_type = logos_to_mlir(TypeRef(ct).elem());
            }
            break;
        }
        auto lpit = var_local_ptrs_.find(name);
        if (lpit != var_local_ptrs_.end()) {
            auto alloca = get_subscript_ptr(name);
            arr_ptr   = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), alloca);
            elem_type = lpit->second;
        } else if (recv_t && recv_t.kind() == LogosType::Kind::Ptr &&
                   recv_t.pointee() &&
                   (recv_t.pointee().kind() == LogosType::Kind::Struct ||
                    recv_t.pointee().kind() == LogosType::Kind::ZonedStruct)) {
            auto cname = concrete_struct_name(recv_t.pointee());
            auto sit   = struct_types_.find(cname);
            if (sit != struct_types_.end()) {
                auto sc = scope_.find(name);
                if (sc != scope_.end()) {
                    arr_ptr   = sc->second;
                    elem_type = sit->second.llvm_type;
                }
            }
            if (!arr_ptr) {
                arr_ptr   = get_subscript_ptr(name);
                elem_type = subscript_elem_type(name);
            }
        } else {
            arr_ptr   = get_subscript_ptr(name);
            elem_type = subscript_elem_type(name);
        }
        break;
    }
    case ec::Code::IndexRead: {
        // Nested index: matrix[i][j] — get a pointer to matrix[i] without loading it.
        lir_view::EIndexReadView irv{recv_ref};
        auto ir_recv  = irv.receiver();
        auto ir_index = irv.index();
        auto* ir_recv_le  = lexpr_of(ir_recv);
        auto* ir_index_le = lexpr_of(ir_index);
        mlir::Value inner_ptr;
        mlir::Type  inner_elem_type;
        if (ir_recv.kind() == ec::Code::VarRef) {
            std::string vn(lir_view::EVarRefView{ir_recv}.name());
            inner_ptr       = get_subscript_ptr(vn);
            inner_elem_type = subscript_elem_type(vn);
        } else if (ir_recv_le) {
            inner_ptr       = gen_expr(*ir_recv_le);
            inner_elem_type = inner_ptr ? logos_to_mlir(ir_recv.type(pool_impl())) : nullptr;
        }
        if (inner_ptr && inner_elem_type && ir_index_le) {
            auto i_idx = gen_expr(*ir_index_le);
            if (i_idx) {
                TypeRef ir_idx_t = ir_index.type(pool_impl());
                bool i_unsigned = ir_idx_t &&
                    (ir_idx_t.kind() == LogosType::Kind::U8  ||
                     ir_idx_t.kind() == LogosType::Kind::U16 ||
                     ir_idx_t.kind() == LogosType::Kind::U32 ||
                     ir_idx_t.kind() == LogosType::Kind::U24 ||
                     ir_idx_t.kind() == LogosType::Kind::U56 ||
                     ir_idx_t.kind() == LogosType::Kind::U64 ||
                     ir_idx_t.kind() == LogosType::Kind::U128);
                if (i_unsigned && i_idx.getType() != builder_.getI64Type())
                    i_idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), i_idx);
                llvm::SmallVector<mlir::LLVM::GEPArg> inner_indices{i_idx};
                arr_ptr   = builder_.create<mlir::LLVM::GEPOp>(
                                loc_, ptr_type(), inner_elem_type, inner_ptr, inner_indices);
                elem_type = logos_to_mlir(type);
                if (!elem_type) elem_type = builder_.getI32Type();
            }
        }
        break;
    }
    case ec::Code::FieldRead: {
        // Field index read: field may be an array or a pointer.
        lir_view::EFieldReadView frv{recv_ref};
        auto fr_recv = frv.receiver();
        auto* fr_recv_le = lexpr_of(fr_recv);
        std::string field(frv.field());
        if (fr_recv_le) {
            auto [struct_ptr, sname] = gen_recv_struct(*fr_recv_le);
            if (struct_ptr && !sname.empty()) {
                auto& info = struct_types_[sname];
                auto field_ptr = gep_field(struct_ptr, info, field);
                if (field_ptr) {
                    elem_type = logos_to_mlir(type);
                    if (!elem_type) elem_type = builder_.getI32Type();
                    bool field_is_ptr = recv_t && recv_t.kind() == LogosType::Kind::Ptr;
                    if (field_is_ptr) {
                        arr_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_ptr);
                        TypeRef rpt = recv_t.pointee();
                        if (rpt &&
                            (rpt.kind() == LogosType::Kind::Struct ||
                             rpt.kind() == LogosType::Kind::ZonedStruct)) {
                            auto cname = concrete_struct_name(rpt);
                            auto sit   = struct_types_.find(cname);
                            if (sit != struct_types_.end())
                                elem_type = sit->second.llvm_type;
                        }
                    } else {
                        arr_ptr = field_ptr;
                    }
                }
            }
        }
        if (!arr_ptr) {
            arr_ptr   = gen_expr(*recv_le);
            elem_type = logos_to_mlir(type);
            if (!elem_type) elem_type = builder_.getI32Type();
        }
        break;
    }
    default:
        arr_ptr   = gen_expr(*recv_le);
        elem_type = logos_to_mlir(type);
        if (!elem_type) elem_type = builder_.getI32Type();
        break;
    }

    auto idx = gen_expr(*idx_le);
    if (!idx || !arr_ptr) return nullptr;
    bool idx_unsigned = idx_t &&
        (idx_t.kind() == LogosType::Kind::U8  ||
         idx_t.kind() == LogosType::Kind::U16 ||
         idx_t.kind() == LogosType::Kind::U32 ||
         idx_t.kind() == LogosType::Kind::U24 ||
         idx_t.kind() == LogosType::Kind::U56 ||
         idx_t.kind() == LogosType::Kind::U64 ||
         idx_t.kind() == LogosType::Kind::U128);
    if (idx_unsigned && idx.getType() != builder_.getI64Type())
        idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
    llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, arr_ptr, indices);
    // Inline struct slot: array element IS the struct (sizeof(Struct) per
    // slot). Downstream code consumes structs by pointer, so return the
    // slot address rather than loading the aggregate.
    if (elem_type && mlir::isa<mlir::LLVM::LLVMStructType>(elem_type))
        return gep;
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, gep);
}

// ---------------------------------------------------------------------------
// Struct / array / tuple literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EStructLitView v, TypeRef) {
    return gen_struct_lit(v);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EArrLitView v, TypeRef type) {
    mlir::Type elem_type = builder_.getI32Type();
    if (type) {
        // Derive slot type from the whole-array conversion so struct elements
        // get inline LLVM struct slots (sizeof(Struct) per slot).
        if (auto arr_t = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(
                logos_to_mlir(type))) {
            elem_type = arr_t.getElementType();
        } else if (TypeRef(type).elem()) {
            auto et = logos_to_mlir(TypeRef(type).elem());
            if (et) elem_type = et;
        }
    }
    return gen_arr_lit(v, elem_type);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETupleLitView v, TypeRef type) {
    auto stype = tuple_llvm_type(type);
    if (!stype) return nullptr;
    // Allocate tuple on stack, store each element via GEP.
    auto alloca = create_entry_alloca(stype);
    uint32_t i = 0;
    bool ok = true;
    v.each_elem([&](lir_view::ExprRef er) {
        if (!ok) return;
        auto* el = lexpr_of(er);
        if (!el) { ok = false; return; }
        auto val = gen_expr(*el);
        if (!val) { ok = false; return; }
        if (TypeRef(type).tuple_elems()[i]) {
            auto et = logos_to_mlir(TypeRef(type).tuple_elems()[i]);
            if (et) val = coerce_numeric(val, et);
        }
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        ++i;
    });
    if (!ok) return nullptr;
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETupleIndexView v, TypeRef type) {
    auto* recv_l = lexpr_of(v.receiver());
    if (!recv_l) return nullptr;
    auto recv = gen_expr(*recv_l);
    if (!recv) return nullptr;
    // Auto-deref: if receiver is &(tuple) or &mut(tuple), use pointee for GEP type.
    // recv is already a pointer to the tuple (passed as ptr in calling convention).
    TypeRef recv_type = recv_l->type;
    if (recv_type && TypeRef(recv_type).pointee() &&
        TypeRef(recv_type).pointee().kind() == LogosType::Kind::Tuple &&
        (TypeRef(recv_type).kind() == LogosType::Kind::Ref ||
         TypeRef(recv_type).kind() == LogosType::Kind::MutRef ||
         TypeRef(recv_type).kind() == LogosType::Kind::Ptr))
        recv_type = TypeRef(recv_type).pointee();
    auto stype = tuple_llvm_type(recv_type);
    if (!stype) return nullptr;
    auto elem_mlir = logos_to_mlir(type);
    if (!elem_mlir) return nullptr;
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(v.index())};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, recv, idx);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, gep);
}

// ---------------------------------------------------------------------------
// Cast
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ECastView v, TypeRef type) {
    auto* op_le = lexpr_of(v.operand());
    if (!op_le) return nullptr;
    std::string hermes_build_fn(v.hermes_build_fn());
    // ── Hermes typed container cast: &[T] as <I32>[] → Hermes. ──────────
    if (!hermes_build_fn.empty()) {
        auto val = gen_expr(*op_le);
        if (!val) return nullptr;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto build_fn = find_func_op(parent_mod, hermes_build_fn);
        if (!build_fn) {
            std::fprintf(stderr, "mlir_gen: '%s' not found — add 'use std.hermes.ctr;'\n",
                         hermes_build_fn.c_str());
            return nullptr;
        }
        // fix3: dispatch by function name prefix, not arg count — getNumArguments() is fragile
        // (any future 3-arg array builder would silently take the wrong path).
        if (hermes_build_fn.rfind("hermes_build_map_", 0) == 0) {
            // Map source: alloca ptr to MapSliceI32 { &[i32], &[AnyVal] }.
            // LLVM layout: { ptr (→keys_slice {ptr,i64}), ptr (→vals_slice {ptr,i64}) }
            auto mtype = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), {ptr_type(), ptr_type()});
            auto stype = slice_llvm_type();  // { ptr, i64 }
            // Load keys_slice alloca ptr from field 0.
            llvm::SmallVector<mlir::LLVM::GEPArg> k0i{int32_t(0), int32_t(0)};
            auto kpp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), mtype, val, k0i);
            auto keys_slice = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), kpp);
            // Extract data ptr (field 0 of keys_slice).
            llvm::SmallVector<mlir::LLVM::GEPArg> kdi{int32_t(0), int32_t(0)};
            auto kdp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, keys_slice, kdi);
            auto keys_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), kdp);
            // Extract len (field 1 of keys_slice).
            llvm::SmallVector<mlir::LLVM::GEPArg> kli{int32_t(0), int32_t(1)};
            auto klp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, keys_slice, kli);
            auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getIntegerType(64), klp);
            // Load vals_slice alloca ptr from field 1.
            llvm::SmallVector<mlir::LLVM::GEPArg> v0i{int32_t(0), int32_t(1)};
            auto vpp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), mtype, val, v0i);
            auto vals_slice = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vpp);
            // Extract data ptr (field 0 of vals_slice).
            llvm::SmallVector<mlir::LLVM::GEPArg> vdi{int32_t(0), int32_t(0)};
            auto vdp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, vals_slice, vdi);
            auto vals_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vdp);
            auto call = builder_.create<mlir::func::CallOp>(
                loc_, build_fn, mlir::ValueRange{keys_ptr, vals_ptr, len});
            return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
        }
        // Array source: alloca ptr to { ptr, i64 } (slice representation).
        // Extract data_ptr (field 0) and len (field 1).
        auto stype = slice_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, pi);
        auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, li);
        auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getIntegerType(64), lp);
        auto call = builder_.create<mlir::func::CallOp>(
            loc_, build_fn, mlir::ValueRange{data_ptr, len});
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    auto val    = gen_expr(*op_le);
    if (!val) return nullptr;

    // str (Slice<u8> = fat pointer {ptr, i64}) as *const u8 → extract field 0.
    // Must be checked BEFORE the val.getType() == target early-return because
    // both the alloca ptr (fat struct) and *const u8 are !llvm.ptr in LLVM 17.
    if (TypeRef ot(op_le->type);
        ot && ot.kind() == LogosType::Kind::Slice &&
        ot.elem() && ot.elem().kind() == LogosType::Kind::U8 &&
        type && TypeRef(type).kind() == LogosType::Kind::Ptr &&
        TypeRef(type).pointee() && TypeRef(type).pointee().kind() == LogosType::Kind::U8) {
        auto stype = slice_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, pi);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
    }

    auto target = logos_to_mlir(type);
    if (!target) return val;

    // *concrete → *mut/*const dyn Trait — synthesise a fat pointer
    // (data + vtable). Both sides are MLIR ptr_type so the early
    // identity-check below would otherwise short-circuit; check here
    // first via the logos types. coerce_to_dyn allocates a 16-byte slot,
    // populates {data_ptr, vtable_ptr}, returns the slot pointer (the
    // 8-byte handle that subsequent code dereferences as ptr-to-fat).
    if (val.getType() == ptr_type() && target == ptr_type() &&
        type && TypeRef(type).kind() == LogosType::Kind::Ptr &&
        TypeRef(TypeRef(type).pointee()).kind() == LogosType::Kind::TraitObject &&
        op_le->type &&
        (TypeRef(op_le->type).kind() == LogosType::Kind::Ptr ||
         TypeRef(op_le->type).kind() == LogosType::Kind::Ref ||
         TypeRef(op_le->type).kind() == LogosType::Kind::MutRef)) {
        auto pointee = TypeRef(op_le->type).pointee();
        std::string src_struct;
        if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
                        TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct))
            src_struct = concrete_struct_name(pointee);
        std::string trait = std::string(TypeRef(TypeRef(type).pointee()).trait_name());
        if (auto alloca = coerce_to_dyn(val, trait, src_struct)) return alloca;
    }
    // `&T` / `&mut T` (over a struct) `as &dyn Trait` / `&mut dyn Trait` — the
    // target type is bare TraitObject (no Ptr wrap, since `&dyn` is one node
    // at sema). Same fat-ptr synthesis as the Ptr case above.
    if (val.getType() == ptr_type() && target == ptr_type() &&
        type && TypeRef(type).kind() == LogosType::Kind::TraitObject &&
        op_le->type &&
        (TypeRef(op_le->type).kind() == LogosType::Kind::Ref ||
         TypeRef(op_le->type).kind() == LogosType::Kind::MutRef)) {
        auto pointee = TypeRef(op_le->type).pointee();
        std::string src_struct;
        if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
                        TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct))
            src_struct = concrete_struct_name(pointee);
        std::string trait = std::string(TypeRef(type).trait_name());
        if (auto alloca = coerce_to_dyn(val, trait, src_struct)) return alloca;
    }

    // C6-cc-08 follow-up: fat-pointer → thin-pointer cast.
    //
    //   * `*mut dyn Trait` / `*const dyn Trait` (Ptr<TraitObject>) → `*mut ()`
    //     / `*const ()` (Ptr<Void>): extract data field of the dyn fat pair,
    //     vtable discarded. Restricted to Void target so `*mut dyn T as *mut
    //     Node` raw-reinterpret casts (used in persistent) keep their no-op
    //     semantics.
    //
    //   * `*const [T]` / `*mut [T]` (Slice) → any thin pointer (Ptr<X>) or
    //     Ptr<Array>: extract data field of the slice {ptr,len} pair. Slice's
    //     grammar is an intentional fat-ptr declaration (no raw-reinterpret
    //     overload exists), so widening the source target permits both `as
    //     *const [T; N]` and `as *const ()` from a raw slice.
    //
    // Both MLIR types are `ptr` so the identity check below would short-
    // circuit and emit a no-op cast yielding the fat-pair address instead
    // of the contained data ptr — must run before it.
    bool fat_to_thin = false;
    auto fk = TypeRef(op_le->type).kind();
    auto tk = TypeRef(type).kind();
    if (val.getType() == ptr_type() && target == ptr_type() && op_le->type && type) {
        bool src_is_dyn_ptr =
            fk == LogosType::Kind::Ptr &&
            TypeRef(op_le->type).pointee() &&
            (TypeRef(op_le->type).pointee().kind() == LogosType::Kind::TraitObject ||
             TypeRef(op_le->type).pointee().kind() == LogosType::Kind::Closure);
        bool src_is_slice = fk == LogosType::Kind::Slice;
        bool dst_is_void_ptr =
            tk == LogosType::Kind::Ptr &&
            TypeRef(type).pointee() &&
            TypeRef(type).pointee().kind() == LogosType::Kind::Void;
        bool dst_is_thin_ptr =
            tk == LogosType::Kind::Ptr &&
            TypeRef(type).pointee() &&
            TypeRef(type).pointee().kind() != LogosType::Kind::TraitObject &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Closure &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Slice;
        if (src_is_dyn_ptr && dst_is_void_ptr) fat_to_thin = true;
        if (src_is_slice  && dst_is_thin_ptr) fat_to_thin = true;
    }
    if (fat_to_thin) {
        auto fat_t = mlir::LLVM::LLVMStructType::getLiteral(
            builder_.getContext(), {ptr_type(), ptr_type()});
        llvm::SmallVector<mlir::LLVM::GEPArg> data_idx{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), fat_t, val, data_idx);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
    }
    // C6-cc-08 follow-up: thin → fat-slice cast. `*const [T; N]` / `*mut
    // [T; N]` (Ptr<Array>) cast to `*const [T]` / `*mut [T]` (Slice)
    // synthesises a `{ptr, len=N}` fat pair on the stack. Without this the
    // cast was a no-op (both ptr at MLIR), so a subsequent fat→thin
    // extraction read the array contents as if they were the data field of
    // a phantom slice pair.
    if (val.getType() == ptr_type() && target == ptr_type() && op_le->type && type &&
        fk == LogosType::Kind::Ptr &&
        TypeRef(op_le->type).pointee() &&
        TypeRef(op_le->type).pointee().kind() == LogosType::Kind::Array &&
        tk == LogosType::Kind::Slice) {
        auto slice_t = slice_llvm_type();
        auto alloca = create_entry_alloca(slice_t);
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), slice_t, alloca, di);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), slice_t, alloca, li);
        auto len = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, int64_t(TypeRef(op_le->type).pointee().arr_size()), 64);
        builder_.create<mlir::LLVM::StoreOp>(loc_, len, lp);
        return alloca;
    }
    if (val.getType() == target) return val;

    auto fi = mlir::dyn_cast<mlir::IntegerType>(val.getType());
    auto ti = mlir::dyn_cast<mlir::IntegerType>(target);
    if (fi && ti) {
        if (ti.getWidth() > fi.getWidth()) {
            bool src_unsigned = fi.getWidth() == 1 ||
                (op_le->type &&
                 (TypeRef(op_le->type).kind() == LogosType::Kind::U8  ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U16 ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U32 ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U24 ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U56 ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U64 ||
                  TypeRef(op_le->type).kind() == LogosType::Kind::U128));
            if (src_unsigned)
                return builder_.create<mlir::arith::ExtUIOp>(loc_, target, val);
            return builder_.create<mlir::arith::ExtSIOp>(loc_, target, val);
        }
        if (ti.getWidth() < fi.getWidth())
            return builder_.create<mlir::arith::TruncIOp>(loc_, target, val);
        return val;
    }
    if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) &&
        mlir::dyn_cast<mlir::FloatType>(target)) {
        // Bool (i1) must be zero-extended before conversion: sitofp(i1(1)) = -1.0 (wrong),
        // uitofp(i1(1)) = 1.0 (correct).  Treat i1 the same as unsigned integers.
        bool src_unsigned = (val.getType() == builder_.getI1Type()) ||
            (op_le->type &&
             (TypeRef(op_le->type).kind() == LogosType::Kind::U8  ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U16 ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U32 ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U24 ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U56 ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U64 ||
              TypeRef(op_le->type).kind() == LogosType::Kind::U128));
        if (src_unsigned)
            return builder_.create<mlir::arith::UIToFPOp>(loc_, target, val);
        return builder_.create<mlir::arith::SIToFPOp>(loc_, target, val);
    }
    // float → float (truncate or extend)
    if (mlir::dyn_cast<mlir::FloatType>(val.getType()) &&
        mlir::dyn_cast<mlir::FloatType>(target)) {
        auto fv = mlir::cast<mlir::FloatType>(val.getType());
        auto ft = mlir::cast<mlir::FloatType>(target);
        if (ft.getWidth() < fv.getWidth())
            return builder_.create<mlir::arith::TruncFOp>(loc_, target, val);
        return builder_.create<mlir::arith::ExtFOp>(loc_, target, val);
    }
    if (mlir::dyn_cast<mlir::FloatType>(val.getType()) &&
        mlir::dyn_cast<mlir::IntegerType>(target)) {
        bool dst_unsigned = type &&
            (TypeRef(type).kind() == LogosType::Kind::U8  ||
             TypeRef(type).kind() == LogosType::Kind::U16 ||
             TypeRef(type).kind() == LogosType::Kind::U32 ||
             TypeRef(type).kind() == LogosType::Kind::U24 ||
             TypeRef(type).kind() == LogosType::Kind::U56 ||
             TypeRef(type).kind() == LogosType::Kind::U64 ||
             TypeRef(type).kind() == LogosType::Kind::U128);
        if (dst_unsigned)
            return builder_.create<mlir::arith::FPToUIOp>(loc_, target, val);
        return builder_.create<mlir::arith::FPToSIOp>(loc_, target, val);
    }

    // int → ptr
    if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) && target == ptr_type()) {
        mlir::Value v64;
        bool src_unsigned = op_le->type &&
            (TypeRef(op_le->type).kind() == LogosType::Kind::U8  ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U16 ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U32 ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U24 ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U56 ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U64 ||
             TypeRef(op_le->type).kind() == LogosType::Kind::U128);
        if (src_unsigned && val.getType() != builder_.getI64Type())
            v64 = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), val);
        else
            v64 = coerce_int(val, builder_.getI64Type());
        return builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), v64);
    }
    // ptr → int
    if (val.getType() == ptr_type() && mlir::dyn_cast<mlir::IntegerType>(target))
        return builder_.create<mlir::LLVM::PtrToIntOp>(loc_, target, val);

    std::fprintf(stderr, "mlir_gen: unsupported cast\n");
    return nullptr;
}

// ---------------------------------------------------------------------------
// Class new
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ENewView v, TypeRef) {
    std::string class_name(v.class_name());
    auto sit = struct_types_.find(class_name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: unknown class '%s'\n", class_name.c_str());
        return nullptr;
    }
    auto& info = sit->second;

    // Allocate heap memory: malloc(sizeof(ClassType))
    mlir::Value size;
    if (info.fields.empty()) {
        // Zero-field class — allocate 1 byte
        size = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    } else {
        size = sizeof_struct(info.llvm_type);
    }
    auto raw = call_malloc(size);
    if (!raw) return nullptr;

    // Initialize user fields
    bool ok = true;
    v.each_field([&](std::string_view fname, lir_view::ExprRef vr) {
        if (!ok) return;
        auto* fv_le = lexpr_of(vr);
        if (!fv_le) { ok = false; return; }
        auto val = gen_expr(*fv_le);
        if (!val) { ok = false; return; }
        auto gep = gep_field(raw, info, std::string(fname));
        if (!gep) { ok = false; return; }
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    });
    if (!ok) return nullptr;

    return raw;  // *mut ClassName
}

// ---------------------------------------------------------------------------
// If-expression / match-expression
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EIfExprView v, TypeRef type) {
    auto* cond_l  = lexpr_of(v.cond());
    auto* then_l  = lexpr_of(v.then_val());
    auto* else_l  = lexpr_of(v.else_val());
    if (!cond_l || !then_l || !else_l) return nullptr;
    auto cond = gen_expr(*cond_l);
    if (!cond) return nullptr;

    // Void-typed if (both branches evaluate to `()`): still emit the
    // branches — they may have side effects (panic call, write, etc.).
    // Without this the cond is computed but no `br` follows, silently
    // dropping both branch bodies. Was the root cause behind
    // assert_eq!(2, 3) not panicking despite the if-then containing
    // __fmt_panic. logos_to_mlir(Void) returns nullptr, which the
    // original `if (!result_type) return nullptr;` short-circuited on.
    mlir::Type result_type = logos_to_mlir(type);
    bool void_if = (type && TypeRef(type).kind() == LogosType::Kind::Void);
    if (!result_type && !void_if) return nullptr;

    // Allocate result slot in the current (entry-reachable) block.
    mlir::Value result_alloca;
    if (result_type) result_alloca = create_entry_alloca(result_type);

    auto* region      = builder_.getBlock()->getParent();
    auto* then_block  = new mlir::Block();
    auto* else_block  = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(then_block);
    region->push_back(else_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

    builder_.setInsertionPointToStart(then_block);
    auto then_val = gen_expr(*then_l);
    // P3-pg-04: branch may diverge (e.g. `break` as expression) and
    // already cf.br'd the block. Skip the store+merge cf.br in that
    // case — the block is terminated and merge_block's predecessors
    // simply omit this edge.
    if (!is_terminated(builder_.getBlock())) {
        if (result_type) {
            if (!then_val) then_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            then_val = coerce_numeric(then_val, result_type);
            // Branches may return a struct by-value (function call) while the merge
            // slot expects a pointer (struct values are normally pointer-aliased).
            // Spill aggregate values so both branches store a pointer.
            if (result_type == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(then_val.getType()))
                then_val = spill_to_alloca(then_val);
            builder_.create<mlir::LLVM::StoreOp>(loc_, then_val, result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    builder_.setInsertionPointToStart(else_block);
    auto else_val = gen_expr(*else_l);
    if (!is_terminated(builder_.getBlock())) {
        if (result_type) {
            if (!else_val) else_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            else_val = coerce_numeric(else_val, result_type);
            if (result_type == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(else_val.getType()))
                else_val = spill_to_alloca(else_val);
            builder_.create<mlir::LLVM::StoreOp>(loc_, else_val, result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    builder_.setInsertionPointToStart(merge_block);
    if (!result_type) {
        // Void if: synthetic unit value so callers don't deref nullptr.
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    }
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMatchExprView v, TypeRef type) {
    namespace pc = lir_schema::pat;
    auto* scrut_le = lexpr_of(v.scrut());
    if (!scrut_le) return nullptr;
    std::vector<lir_view::EMatchArmRef> arm_refs;
    v.each_arm([&](lir_view::EMatchArmRef a){ arm_refs.push_back(a); });
    mlir::Type result_type = logos_to_mlir(type);
    if (!result_type) return nullptr;

    // Allocate result slot before the match (entry-block reachable).
    auto result_alloca = create_entry_alloca(result_type);

    auto* region      = builder_.getBlock()->getParent();
    auto* merge_block = new mlir::Block();

    auto scrut = gen_expr(*scrut_le);
    if (!scrut) {
        region->push_back(merge_block);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
    }

    // Detect tagged enum: load discriminant.
    mlir::Value scrut_ptr = nullptr;
    const TaggedEnumInfo* te_info = nullptr;
    if (TypeRef st(scrut_le->type); st) {
        // Auto-deref `&Enum` / `&mut Enum` / `*Enum` so `match &enum_val { ... }`
        // works the same as `match enum_val { ... }`.
        TypeRef enum_t = st;
        bool via_ref = false;
        if ((st.kind() == LogosType::Kind::Ref ||
             st.kind() == LogosType::Kind::MutRef ||
             st.kind() == LogosType::Kind::Ptr) && st.pointee()) {
            TypeRef inner(st.pointee());
            if (inner.kind() == LogosType::Kind::Enum) {
                enum_t = inner;
                via_ref = true;
            }
        }
        if (enum_t.kind() == LogosType::Kind::Enum) {
            te_info = resolve_tagged_enum(std::string(enum_t.enum_name()), enum_t);
            if (te_info) {
                // Logos enum values are heap pointers (EEnumLitData returns
                // a malloc'd ptr). `&Enum` is therefore a pointer-to-pointer;
                // load the inner ptr first to get the enum-struct address.
                if (via_ref) {
                    scrut = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, ptr_type(), scrut);
                } else if (scrut.getType() != ptr_type()) {
                    auto tmp = create_entry_alloca(te_info->llvm_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    scrut = tmp;
                }
                scrut_ptr = scrut;
                llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                auto dp = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, di);
                scrut = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
            }
        }
    }
    mlir::Type scrut_type = scrut.getType();

    // Extract payload bindings for a PatVariantData arm into scope.
    std::function<std::vector<std::string>(lir_view::PatRef)> extract_arm_payload =
        [&](lir_view::PatRef pat_ref) -> std::vector<std::string> {
        std::vector<std::string> added;
        if (pat_ref.kind() == pc::Code::VariantData) {
            lir_view::PatVariantDataView pvd{pat_ref};
            std::vector<std::string> bindings;
            pvd.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
            // SL-sl-03 follow-up: per-binding override types from
            // `Option::Some(ref v)` / `ref mut v` patterns — sema wraps
            // the corresponding `binding_types` slot with Ref / MutRef.
            // When set, codegen returns the payload field's GEP address
            // and types the binding as a ref so subsequent `*v` reads
            // (or `*v = x` writes for &mut) hit the original payload
            // slot rather than a value copy.
            std::vector<TypeRef> pvd_binding_types;
            pvd.each_binding_type(pool_impl(),
                [&](TypeRef t){ pvd_binding_types.push_back(t); });
            int64_t pvd_disc = pvd.disc();
            if (te_info && scrut_ptr) {
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& v : te_info->variants)
                    if (v.disc == pvd_disc) { vp = &v; break; }
                if (vp) {
                    llvm::SmallVector<mlir::Type> ft;
                    for (auto& t : vp->field_types) ft.push_back(t);
                    auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                        builder_.getContext(), ft);
                    for (size_t bi = 0; bi < bindings.size() &&
                                         bi < vp->field_types.size(); ++bi) {
                        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                        auto fp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), pay_struct, pay_ptr, fi);
                        TypeRef lt = bi < vp->logos_types.size()
                                              ? vp->logos_types[bi] : nullptr;
                        bool is_inline_struct = lt &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                             TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                             TypeRef(lt).kind() == LogosType::Kind::Slice ||
                             TypeRef(lt).kind() == LogosType::Kind::Closure);
                        // `ref v` / `ref mut v` binding — sema wraps
                        // pvd_binding_types[bi] in Ref/MutRef while the
                        // underlying variant payload type stays bare.
                        // (For Opt<&T>'s Some(plain r), pvd is Ref but
                        // so is the payload — not a ref-pattern.)
                        bool is_ref_bind = false;
                        if (bi < pvd_binding_types.size() && pvd_binding_types[bi]) {
                            auto ot = TypeRef(pvd_binding_types[bi]);
                            auto pt = bi < vp->logos_types.size()
                                ? TypeRef(vp->logos_types[bi]) : TypeRef{};
                            bool pvd_is_ref = ot.kind() == LogosType::Kind::Ref ||
                                              ot.kind() == LogosType::Kind::MutRef;
                            bool payload_is_ref = pt &&
                                (pt.kind() == LogosType::Kind::Ref ||
                                 pt.kind() == LogosType::Kind::MutRef);
                            if (pvd_is_ref && !payload_is_ref) is_ref_bind = true;
                        }
                        if (is_ref_bind) {
                            // Bind as an alloca holding the GEP address,
                            // typed as ptr_type so let-var lookup loads
                            // the address for use. `*ref_binding` then
                            // dereferences through the original slot.
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                            scope_[bindings[bi]] = alloca;
                            let_vars_.insert(bindings[bi]);
                            var_elem_types_[bindings[bi]] = ptr_type();
                            added.push_back(bindings[bi]);
                            continue;
                        }
                        if (is_inline_struct &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct)) {
                            scope_[bindings[bi]] = fp;
                            let_vars_.insert(bindings[bi]);
                            var_struct_[bindings[bi]] = mlir_struct_key(lt);
                            added.push_back(bindings[bi]);
                        } else {
                            mlir::Value bound_val;
                            if (is_inline_struct) {
                                bound_val = fp;
                            } else {
                                bound_val = builder_.create<mlir::LLVM::LoadOp>(
                                    loc_, vp->field_types[bi], fp);
                            }
                            auto alloca = create_entry_alloca(vp->field_types[bi]);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, bound_val, alloca);
                            scope_[bindings[bi]] = alloca;
                            let_vars_.insert(bindings[bi]);
                            var_elem_types_[bindings[bi]] = vp->field_types[bi];
                            added.push_back(bindings[bi]);
                        }
                    }
                }
            }
        } else if (pat_ref.kind() == pc::Code::Wild) {
            std::string name(lir_view::PatWildView{pat_ref}.name());
            if (!name.empty() && name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[name] = alloca;
                let_vars_.insert(name);
                var_elem_types_[name] = sv.getType();
                added.push_back(name);
            }
        } else if (pat_ref.kind() == pc::Code::Or) {
            // Or-pattern bindings: sema's NG4 check guarantees every alternative
            // binds the same name set. For variant alts that share payload
            // shape (e.g. L(x: i32) | R(x: i32)), GEP'ing as the first alt's
            // variant lands at the same offset/type — which is what we want.
            // (Mixed-shape alts are rejected at sema.)
            lir_view::PatRef first;
            lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){
                if (!first) first = a;
            });
            if (first) {
                auto inner = extract_arm_payload(first);
                added.insert(added.end(), inner.begin(), inner.end());
            }
        }
        return added;
    };

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    if (scrut_le->type && TypeRef(scrut_le->type).kind() == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (size_t ai = 0; ai < arm_refs.size(); ++ai) {
            if (arm_refs[ai].guard()) continue;
            auto pat_ref = arm_refs[ai].pat();
            if (pat_ref.kind() == pc::Code::Wild) { has_wild = true; break; }
            auto check_bool = [&](lir_view::PatRef p) {
                if (p.kind() == pc::Code::Bool) {
                    if (lir_view::PatBoolView{p}.value()) has_true = true; else has_false = true;
                }
            };
            if (pat_ref.kind() == pc::Code::Or) {
                lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){ check_bool(a); });
            } else {
                check_bool(pat_ref);
            }
        }
        exhaustive_discrete = has_wild || (has_true && has_false);
    } else if (scrut_le->type && TypeRef(scrut_le->type).kind() == LogosType::Kind::Enum) {
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](lir_view::PatRef p) {
            if (p.kind() == pc::Code::Variant)          covered.insert(static_cast<int32_t>(lir_view::PatVariantView{p}.disc()));
            else if (p.kind() == pc::Code::VariantData) covered.insert(static_cast<int32_t>(lir_view::PatVariantDataView{p}.disc()));
        };
        for (size_t ai = 0; ai < arm_refs.size(); ++ai) {
            if (arm_refs[ai].guard()) continue;
            auto pat_ref = arm_refs[ai].pat();
            if (pat_ref.kind() == pc::Code::Wild) { has_wild = true; break; }
            if (pat_ref.kind() == pc::Code::Or) {
                lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){ cover_enum(a); });
            } else {
                cover_enum(pat_ref);
            }
        }
        if (has_wild) {
            exhaustive_discrete = true;
        } else {
            std::string en(TypeRef(scrut_le->type).enum_name());
            auto eit = enum_types_.find(en);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(en, scrut_le->type)) {
                exhaustive_discrete = std::all_of(
                    te->variants.begin(), te->variants.end(),
                    [&](const TaggedEnumInfo::VariantPayload& v) { return covered.count(v.disc) > 0; });
            }
        }
    }
    if (exhaustive_discrete) {
        auto* default_block = new mlir::Block();
        region->push_back(default_block);
        {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(default_block);
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        }
        else_block = default_block;
    }
    for (int i = (int)arm_refs.size() - 1; i >= 0; --i) {
        auto arm_guard_ref = arm_refs[i].guard();
        auto arm_value_ref = arm_refs[i].value();
        auto arm_pat_ref = arm_refs[i].pat();
        auto* body_block = new mlir::Block();
        region->push_back(body_block);

        mlir::Block* arm_entry = body_block;

        if (arm_guard_ref) {
            // guard_block: extract bindings, evaluate guard, branch to body_block or else_block.
            auto* guard_block = new mlir::Block();
            region->push_back(guard_block);
            std::vector<std::string> guard_added;
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(guard_block);
                guard_added = extract_arm_payload(arm_pat_ref);
                auto* guard_le = lexpr_of(arm_guard_ref);
                auto gval = guard_le ? gen_expr(*guard_le) : nullptr;
                gval = coerce_int(gval, builder_.getI1Type());
                builder_.create<mlir::cf::CondBranchOp>(loc_, gval, body_block, else_block);
            }
            arm_entry = guard_block;
            // body_block: bindings already in scope from guard_block; generate arm value.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                auto* value_le = lexpr_of(arm_value_ref);
                auto val = value_le ? gen_expr(*value_le) : nullptr;
                for (auto& n : guard_added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
                if (!is_terminated(builder_.getBlock())) {
                    if (val) {
                        val = coerce_numeric(val, result_type);
                        builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                    }
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
                }
            }
        } else {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(body_block);
            auto added = extract_arm_payload(arm_pat_ref);
            auto* value_le = lexpr_of(arm_value_ref);
            auto val = value_le ? gen_expr(*value_le) : nullptr;
            for (auto& n : added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
            if (!is_terminated(builder_.getBlock())) {
                if (val) {
                    val = coerce_numeric(val, result_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                }
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        }

        bool is_wild = arm_pat_ref.kind() == pc::Code::Wild;
        auto get_disc = [](lir_view::PatRef p) -> int64_t {
            switch (p.kind()) {
            case pc::Code::Variant:     return lir_view::PatVariantView{p}.disc();
            case pc::Code::VariantData: return lir_view::PatVariantDataView{p}.disc();
            case pc::Code::Int:         return lir_view::PatIntView{p}.value();
            case pc::Code::Bool:        return lir_view::PatBoolView{p}.value() ? 1 : 0;
            default: return 0;
            }
        };
        // Local scrut_unsigned helper — match-as-expression had no
        // Range-arm handling, so it never needed this. Mirror the
        // match-stmt version (mlir_gen_stmt.cpp).
        auto scrut_unsigned = [&]() -> bool {
            if (!scrut_le->type) return false;
            switch (TypeRef(scrut_le->type).kind()) {
                case LogosType::Kind::U8:  case LogosType::Kind::U16:
                case LogosType::Kind::U24: case LogosType::Kind::U32:
                case LogosType::Kind::U56: case LogosType::Kind::U64:
                case LogosType::Kind::U128: case LogosType::Kind::Usize:
                case LogosType::Kind::Char: return true;
                default: return false;
            }
        };
        if (is_wild) {
            else_block = arm_entry;
        } else if (arm_pat_ref.kind() == pc::Code::Tuple &&
                   lir_view::PatTupleView{arm_pat_ref}.sub_count() > 0) {
            // Refutable tuple arm in match-as-expression. Mirrors the
            // match-stmt path: GEP each refutable sub-pat element,
            // AND-chain equality tests. Scrut may be a value (not a
            // pointer); spill to alloca for GEP.
            lir_view::PatTupleView tv{arm_pat_ref};
            std::vector<lir_view::PatRef> subs;
            tv.each_sub([&](lir_view::PatRef sp){ subs.push_back(sp); });
            std::vector<TypeRef> btypes;
            tv.each_binding_type(pool_impl(), [&](TypeRef t){ btypes.push_back(t); });
            auto ttype = scrut_le->type ? tuple_llvm_type(scrut_le->type) : mlir::Type();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                // Spill scrut to alloca if it's a non-pointer value.
                mlir::Value tptr;
                if (scrut.getType() == ptr_type()) {
                    tptr = scrut;
                } else if (ttype) {
                    auto a = create_entry_alloca(ttype);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, a);
                    tptr = a;
                }
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                for (size_t si = 0; si < subs.size() && ttype && tptr; ++si) {
                    auto sub = subs[si];
                    if (!sub || sub.kind() == pc::Code::Wild) continue;
                    auto elem_mlir = si < btypes.size()
                                     ? logos_to_mlir(btypes[si]) : mlir::Type();
                    if (!elem_mlir) continue;
                    bool elem_unsigned = si < btypes.size() && btypes[si] &&
                        (TypeRef(btypes[si]).kind() == LogosType::Kind::U8  ||
                         TypeRef(btypes[si]).kind() == LogosType::Kind::U16 ||
                         TypeRef(btypes[si]).kind() == LogosType::Kind::U32 ||
                         TypeRef(btypes[si]).kind() == LogosType::Kind::U64 ||
                         TypeRef(btypes[si]).kind() == LogosType::Kind::Usize ||
                         TypeRef(btypes[si]).kind() == LogosType::Kind::Char);
                    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(si)};
                    auto fp = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), ttype, tptr, fi);
                    auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
                    if (sub.kind() == pc::Code::Range) {
                        lir_view::PatRangeView pr{sub};
                        auto pred_ge = elem_unsigned ? mlir::arith::CmpIPredicate::uge
                                                     : mlir::arith::CmpIPredicate::sge;
                        auto pred_le = elem_unsigned ? mlir::arith::CmpIPredicate::ule
                                                     : mlir::arith::CmpIPredicate::sle;
                        auto lo_v = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.lo(), 64), elem_mlir);
                        auto hi_v = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.hi(), 64), elem_mlir);
                        auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, pred_ge, ev, lo_v);
                        auto le = builder_.create<mlir::arith::CmpIOp>(loc_, pred_le, ev, hi_v);
                        auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                        cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, both);
                        continue;
                    }
                    if (sub.kind() == pc::Code::VariantData) {
                        // P4-pm-24 (expr form): same disc-check shape as
                        // the stmt path. fp is the tuple-slot ptr (holds
                        // an enum ptr); load through to the enum struct
                        // and compare its disc.
                        lir_view::PatVariantDataView vv{sub};
                        auto* te_t = resolve_tagged_enum(
                            std::string(vv.enum_name()),
                            si < btypes.size() ? btypes[si] : TypeRef());
                        if (!te_t) continue;
                        auto enum_ptr = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, ptr_type(), fp);
                        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                        auto dp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), te_t->llvm_type, enum_ptr, di);
                        auto dv = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, builder_.getI32Type(), dp);
                        auto dc = builder_.create<mlir::arith::ConstantIntOp>(
                            loc_, vv.disc(), 32);
                        auto eq = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::eq, dv, dc);
                        cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                        continue;
                    }
                    if (sub.kind() == pc::Code::Or) {
                        // P4-pm-03 (expr form): or-pattern at tuple element.
                        mlir::Value alt_or =
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
                        bool any_alt = false;
                        lir_view::PatOrView{sub}.each_alt([&](lir_view::PatRef alt) {
                            int64_t av = 0;
                            if (alt.kind() == pc::Code::Int) {
                                av = lir_view::PatIntView{alt}.value();
                            } else if (alt.kind() == pc::Code::Bool) {
                                av = lir_view::PatBoolView{alt}.value() ? 1 : 0;
                            } else {
                                return;
                            }
                            auto cv = coerce_int(
                                builder_.create<mlir::arith::ConstantIntOp>(loc_, av, 64),
                                elem_mlir);
                            auto eq = builder_.create<mlir::arith::CmpIOp>(
                                loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                            alt_or = builder_.create<mlir::arith::OrIOp>(loc_, alt_or, eq);
                            any_alt = true;
                        });
                        if (any_alt) cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, alt_or);
                        continue;
                    }
                    int64_t sub_val = 0;
                    if (sub.kind() == pc::Code::Int)       sub_val = lir_view::PatIntView{sub}.value();
                    else if (sub.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sub}.value() ? 1 : 0;
                    else continue;
                    auto cv = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, sub_val, 64),
                        elem_mlir);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                    cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                }
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Range) {
            // Range arm in match-as-expression. Same shape as the
            // match-stmt path (`lo <= scrut && scrut <= hi`) — was
            // missing here, so range arms silently fell through to
            // the wildcard via the `get_disc` default of 0.
            lir_view::PatRangeView pr{arm_pat_ref};
            auto pred_ge = scrut_unsigned() ? mlir::arith::CmpIPredicate::uge
                                            : mlir::arith::CmpIPredicate::sge;
            auto pred_le = scrut_unsigned() ? mlir::arith::CmpIPredicate::ule
                                            : mlir::arith::CmpIPredicate::sle;
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto lo_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.lo(), 64), scrut_type);
                auto hi_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.hi(), 64), scrut_type);
                auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, pred_ge, scrut, lo_val);
                auto le = builder_.create<mlir::arith::CmpIOp>(loc_, pred_le, scrut, hi_val);
                auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Or) {
            std::vector<lir_view::PatRef> alts;
            lir_view::PatOrView{arm_pat_ref}.each_alt([&](lir_view::PatRef a){ alts.push_back(a); });
            mlir::Block* cur_else = else_block;
            for (int64_t ai = static_cast<int64_t>(alts.size()) - 1; ai >= 0; --ai) {
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                int64_t disc = get_disc(alts[static_cast<size_t>(ai)]);
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64), scrut_type);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, cur_else);
                cur_else = test_block;
            }
            else_block = cur_else;
        } else if (arm_pat_ref.kind() == pc::Code::Slice &&
                   scrut_le->type &&
                   TypeRef(scrut_le->type).kind() == LogosType::Kind::Array) {
            // P4-pm-04 (match-as-expression): mirror of mlir_gen_stmt
            // slice-pattern handling. Spill scrut to alloca if it isn't
            // already a pointer; GEP each refutable sub-element and
            // AND-chain equality tests. Trailing `..` absorbs the rest.
            lir_view::PatSliceView sv{arm_pat_ref};
            TypeRef atyp = scrut_le->type;
            auto elem_mlir = logos_to_mlir(TypeRef(atyp).elem());
            auto arr_mlir  = logos_to_mlir(atyp);
            size_t total   = (size_t)TypeRef(atyp).arr_size();
            size_t suf_n   = sv.suffix_count();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value aptr;
                if (scrut.getType() == ptr_type()) {
                    aptr = scrut;
                } else if (arr_mlir) {
                    auto a = create_entry_alloca(arr_mlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, a);
                    aptr = a;
                }
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                auto chk_at = [&](lir_view::PatRef sp, int32_t idx) {
                    if (!sp || sp.kind() == pc::Code::Wild) return;
                    int64_t sub_val = 0;
                    if      (sp.kind() == pc::Code::Int)  sub_val = lir_view::PatIntView{sp}.value();
                    else if (sp.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sp}.value() ? 1 : 0;
                    else return;
                    if (!elem_mlir || !arr_mlir || !aptr) return;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                    auto ep = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), arr_mlir, aptr, gi);
                    auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                    auto cv = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, sub_val, 64),
                        elem_mlir);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                    cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                };
                int32_t idx = 0;
                sv.each_prefix([&](lir_view::PatRef sp){ chk_at(sp, idx++); });
                int32_t sidx = (int32_t)(total - suf_n);
                sv.each_suffix([&](lir_view::PatRef sp){ chk_at(sp, sidx++); });
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::RefPat &&
                   scrut_le->type &&
                   (TypeRef(scrut_le->type).kind() == LogosType::Kind::Ref ||
                    TypeRef(scrut_le->type).kind() == LogosType::Kind::MutRef) &&
                   TypeRef(scrut_le->type).pointee()) {
            // P4-pm-18 (match-as-expr): `match &T { &<scalar> => … }`.
            // Deref scrut and cmp against the inner disc.
            auto inner = lir_view::PatRefPatView{arm_pat_ref}.inner();
            int64_t disc = inner ? get_disc(inner) : 0;
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto pointee_t = TypeRef(scrut_le->type).pointee();
                auto elem_mlir = logos_to_mlir(pointee_t);
                if (!elem_mlir) elem_mlir = builder_.getI32Type();
                auto loaded = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, elem_mlir, scrut);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64),
                    elem_mlir);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, loaded, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
            }
            else_block = test_block;
        } else {
            int64_t disc = get_disc(arm_pat_ref);

            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64),
                    scrut_type);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
            }
            else_block = test_block;
        }
    }

    builder_.create<mlir::cf::BranchOp>(loc_, else_block);
    region->push_back(merge_block);
    builder_.setInsertionPointToStart(merge_block);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

// ---------------------------------------------------------------------------
// Closure call
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EClosureBoxView v, TypeRef type) {
    return gen_closure(v, type);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EClosureCallView v, TypeRef type) {
    auto* callee_le = lexpr_of(v.callee());
    if (!callee_le) return nullptr;
    auto closure = gen_expr(*callee_le);
    if (!closure) return nullptr;

    auto ctype = closure_llvm_type();
    // Load fn_ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure, fi);
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), fp);
    // Load env_ptr from field 1
    llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure, ei);
    auto env_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), ep);

    // Build args: env_ptr first, then user args
    llvm::SmallVector<mlir::Value> args;
    args.push_back(env_ptr);

    // Build LLVM function type for indirect call
    llvm::SmallVector<mlir::Type> param_types;
    param_types.push_back(ptr_type());  // env
    bool arg_failed = false;
    v.each_arg([&](lir_view::ExprRef ar) {
        if (arg_failed) return;
        auto* a_le = lexpr_of(ar);
        if (!a_le) { arg_failed = true; return; }
        auto val = gen_expr(*a_le);
        if (!val) { arg_failed = true; return; }
        args.push_back(val);
        param_types.push_back(val.getType());
    });
    if (arg_failed) return nullptr;

    // See EFnPtrCall for the struct-return ABI rationale.
    mlir::Type ret = fn_call_ret_llvm_type(type);
    if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret);
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types, false);

    // Indirect call via function pointer
    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, llvm_fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    if (is_void) return nullptr;
    auto result = call.getResult();
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ret))
        return spill_to_alloca(result);
    return result;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFnPtrCallView v, TypeRef type) {
    // Bare function pointer call: fn_ptr(arg1, arg2, ...) — no env_ptr.
    auto* callee_le = lexpr_of(v.callee());
    if (!callee_le) return nullptr;
    auto fn_ptr = gen_expr(*callee_le);
    if (!fn_ptr) return nullptr;

    // fn_ptr is stored as a scalar (not in an alloca) when it's a let var;
    // but scope_ stores allocas for let-bound scalars, so load it first.
    // Actually FnPtr variables are stored as scalars (like integers) — load from alloca.
    // (fn_ptr here is the raw pointer value, already loaded by gen_expr_kind(EVarRef))

    llvm::SmallVector<mlir::Value> args;
    llvm::SmallVector<mlir::Type> param_types;
    bool arg_failed = false;
    v.each_arg([&](lir_view::ExprRef ar) {
        if (arg_failed) return;
        auto* a_le = lexpr_of(ar);
        if (!a_le) { arg_failed = true; return; }
        auto val = gen_expr(*a_le);
        if (!val) { arg_failed = true; return; }
        args.push_back(val);
        param_types.push_back(val.getType());
    });
    if (arg_failed) return nullptr;

    // Return type must match the callee's ABI — tuples/structs/enums are
    // returned by aggregate value (the callee uses sret promotion by the LLVM
    // backend). Using logos_to_mlir(struct) would yield `ptr`, producing a
    // call type that disagrees with the callee and breaks argument passing
    // (rdi becomes the first real arg instead of the hidden sret slot).
    mlir::Type ret = fn_call_ret_llvm_type(type);
    if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret);
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types, false);

    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, llvm_fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    if (is_void) return nullptr;
    auto result = call.getResult();
    // If the return is an aggregate (struct/tuple/enum), spill to alloca so
    // the rest of codegen — which expects struct values as ptr — can work.
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ret))
        return spill_to_alloca(result);
    return result;
}

// ---------------------------------------------------------------------------
// Slice helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceLitView v, TypeRef) {
    auto* base_l = lexpr_of(v.base());
    auto* len_l  = lexpr_of(v.len());
    if (!base_l || !len_l) return nullptr;
    auto base = gen_expr(*base_l);
    auto len  = gen_expr(*len_l);
    if (!base || !len) return nullptr;
    auto stype = slice_llvm_type();
    auto alloca = create_entry_alloca(stype);
    // Store ptr at field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, base, pp);
    // Store len at field 1
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
    auto len64 = coerce_int(len, builder_.getI64Type());
    builder_.create<mlir::LLVM::StoreOp>(loc_, len64, lp);
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceIndexView v, TypeRef type) {
    auto* slice_l = lexpr_of(v.slice());
    auto* index_l = lexpr_of(v.index());
    if (!slice_l || !index_l) return nullptr;
    auto slice = gen_expr(*slice_l);
    auto index = gen_expr(*index_l);
    if (!slice || !index) return nullptr;
    auto elem_type = logos_to_mlir(type);
    if (!elem_type) elem_type = builder_.getI32Type();
    auto stype = slice_llvm_type();
    // Load ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, pi);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
    // GEP into data array by index.
    bool idx_unsigned = index_l->type &&
        (TypeRef(index_l->type).kind() == LogosType::Kind::U8  ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U16 ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U32 ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U24 ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U56 ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U64 ||
         TypeRef(index_l->type).kind() == LogosType::Kind::U128);
    mlir::Value gep_idx;
    if (idx_unsigned && index.getType() != builder_.getI64Type())
        gep_idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), index);
    else
        gep_idx = index;
    // P4-pm-15: struct/ZonedStruct elements lay out inline in arrays
    // (stride = sizeof(struct)) but logos_to_mlir resolves them to
    // ptr_type. Using ptr_type as GEP stride strides by 8B and then
    // load-as-ptr re-dereferences into garbage. Detect struct elements
    // and switch to the actual aggregate LLVM type for stride, and
    // return the element ptr (the caller will memcpy by-value for
    // aggregate values).
    if (TypeRef rt(type); rt && (rt.kind() == LogosType::Kind::Struct ||
                                  rt.kind() == LogosType::Kind::ZonedStruct)) {
        auto cname = concrete_struct_name(rt);
        auto sit   = struct_types_.find(cname);
        if (sit != struct_types_.end()) {
            auto agg_t = sit->second.llvm_type;
            llvm::SmallVector<mlir::LLVM::GEPArg> di{gep_idx};
            auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), agg_t, data_ptr, di);
            return elem_ptr;
        }
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> di{gep_idx};
    auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, data_ptr, di);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, elem_ptr);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceLenView v, TypeRef) {
    auto* sl = lexpr_of(v.slice());
    if (!sl) return nullptr;
    auto slice = gen_expr(*sl);
    if (!slice) return nullptr;
    auto stype = slice_llvm_type();
    // Load len from field 1
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, li);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESlicePtrView v, TypeRef) {
    auto* sl = lexpr_of(v.slice());
    if (!sl) return nullptr;
    auto slice = gen_expr(*sl);
    if (!slice) return nullptr;
    auto stype = slice_llvm_type();
    // Load ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, pi);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
}

// ---------------------------------------------------------------------------
// format() built-in
// ---------------------------------------------------------------------------

int MLIRGenImpl::format_type_tag(TypeRef t) noexcept {
    if (!t) return 0;
    switch (TypeRef(t).kind()) {
        case LogosType::Kind::I32:    return 0;
        case LogosType::Kind::I64:    return 1;
        case LogosType::Kind::Ptr:    return 2;
        case LogosType::Kind::Slice:  return 2;
        case LogosType::Kind::Bool:   return 3;
        case LogosType::Kind::U8:     return 4;
        case LogosType::Kind::U32:    return 5;
        case LogosType::Kind::U64:    return 6;
        case LogosType::Kind::I8:     return 7;
        case LogosType::Kind::I16:    return 0;  // dispatches as i32
        case LogosType::Kind::U16:    return 5;  // dispatches as u32
        case LogosType::Kind::I24:    return 1;  // dispatches as i64
        case LogosType::Kind::I56:    return 1;  // dispatches as i64
        case LogosType::Kind::U24:    return 6;  // dispatches as u64
        case LogosType::Kind::U56:    return 6;  // dispatches as u64
        case LogosType::Kind::I128:   return 1;  // dispatches as i64
        case LogosType::Kind::U128:   return 6;  // dispatches as u64
        case LogosType::Kind::IntLit: return 0;
        default:                      return 0;
    }
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFormatCallView v, TypeRef) {
    auto* fmt_le = lexpr_of(v.fmt());
    if (!fmt_le) return nullptr;
    auto fmt_val = gen_expr(*fmt_le);
    if (!fmt_val) return nullptr;

    auto arg_types = v.arg_types(pool_impl());
    std::vector<lir_view::ExprRef> arg_refs;
    v.each_arg([&](lir_view::ExprRef r) { arg_refs.push_back(r); });
    int n = (int)arg_refs.size();

    auto i32_type = builder_.getI32Type();
    auto i64_type = builder_.getI64Type();

    // Allocate [n x i32] tags and [n x i64] data arrays on stack.
    int64_t n_cnt = n > 0 ? n : 1;
    auto tags_alloca = create_entry_alloca(i32_type, n_cnt);
    auto data_alloca = create_entry_alloca(i64_type, n_cnt);

    for (int i = 0; i < n; ++i) {
        int tag = format_type_tag(arg_types[i]);

        // Store tag at tags[i]
        llvm::SmallVector<mlir::LLVM::GEPArg> ti{int32_t(i)};
        auto tgep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i32_type, tags_alloca, ti);
        auto tag_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, tag, 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, tag_val, tgep);

        // Evaluate arg and widen to i64.
        // Unsigned types narrower than 64 bits must be zero-extended, not sign-extended.
        auto* a_le = lexpr_of(arg_refs[i]);
        if (!a_le) return nullptr;
        auto arg_val = gen_expr(*a_le);
        if (!arg_val) return nullptr;
        mlir::Value as_i64;
        if (arg_val.getType() == ptr_type()) {
            as_i64 = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_type, arg_val);
        } else {
            TypeRef arg_lt = static_cast<size_t>(i) < arg_types.size() ? arg_types[i] : TypeRef{};
            bool arg_unsigned = arg_lt &&
                (arg_lt.kind() == LogosType::Kind::U8   ||
                 arg_lt.kind() == LogosType::Kind::U16  ||
                 arg_lt.kind() == LogosType::Kind::U32  ||
                 arg_lt.kind() == LogosType::Kind::U24  ||
                 arg_lt.kind() == LogosType::Kind::U56  ||
                 arg_lt.kind() == LogosType::Kind::U64  ||
                 arg_lt.kind() == LogosType::Kind::U128);
            auto ai = mlir::dyn_cast<mlir::IntegerType>(arg_val.getType());
            if (arg_unsigned && ai && ai.getWidth() < 64)
                as_i64 = builder_.create<mlir::arith::ExtUIOp>(loc_, i64_type, arg_val);
            else
                as_i64 = coerce_int(arg_val, i64_type);
        }

        // Store data at data[i]
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(i)};
        auto dgep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i64_type, data_alloca, di);
        builder_.create<mlir::LLVM::StoreOp>(loc_, as_i64, dgep);
    }

    // Call __format_impl(fmt, tags_ptr, data_ptr, nargs)
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto impl_fn = mod.lookupSymbol<mlir::func::FuncOp>("__format_impl");
    if (!impl_fn) {
        std::fprintf(stderr,
            "mlir_gen: format() requires 'use std.lang.text;' to be imported\n");
        return nullptr;
    }
    auto n_i32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, n, 32);
    llvm::SmallVector<mlir::Value> call_args{fmt_val, tags_alloca, data_alloca, n_i32};
    auto call = builder_.create<mlir::func::CallOp>(loc_, impl_fn, call_args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Misc expression kinds
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPackExpandView, TypeRef) {
    std::fprintf(stderr, "mlir_gen: unexpected EPackExpand (should be expanded by mono)\n");
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESizeOfView v, TypeRef) {
    TypeRef elem_type = v.elem_type(pool_impl());
    // For Struct/Datatype: logos_to_mlir returns ptr_type() (always passed by pointer),
    // but sizeof needs the actual aggregate type, not the pointer.
    mlir::Type elem_mlir = nullptr;
    if (elem_type && (elem_type.kind() == LogosType::Kind::Struct ||
                      elem_type.kind() == LogosType::Kind::ZonedStruct)) {
        auto cname = concrete_struct_name(elem_type);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end())
            elem_mlir = sit->second.llvm_type;
    }
    // C5-cl-04 follow-up: fat-pointer kinds (Closure / Slice / TraitObject) also
    // pass by ptr at the ABI layer, but their underlying storage is 16 bytes
    // (`{ptr, ptr}`). Without this, `sizeof::<Closure>()` returns 8 from the
    // gep-null trick on a single ptr, which truncates Box<Closure>'s heap
    // allocation and breaks Box<dyn FnMut(…)> dispatch.
    if (!elem_mlir && elem_type &&
        (elem_type.kind() == LogosType::Kind::Closure ||
         elem_type.kind() == LogosType::Kind::Slice ||
         elem_type.kind() == LogosType::Kind::TraitObject)) {
        elem_mlir = mlir::LLVM::LLVMStructType::getLiteral(
            builder_.getContext(), {ptr_type(), ptr_type()});
    }
    if (!elem_mlir) elem_mlir = logos_to_mlir(elem_type);
    if (!elem_mlir) {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
    }
    mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    mlir::Value null_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero);
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(1)};
    auto size_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_mlir, null_ptr, idx);
    return builder_.create<mlir::LLVM::PtrToIntOp>(
        loc_, builder_.getI64Type(), size_ptr);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAlignOfView v, TypeRef) {
    TypeRef elem_type = v.elem_type(pool_impl());
    int64_t align = 8;
    if (elem_type) {
        using K = LogosType::Kind;
        switch (elem_type.kind()) {
        case K::Bool: case K::I8:  case K::U8:                       align = 1; break;
        case K::I16: case K::U16:                                    align = 2; break;
        case K::I24: case K::U24:                                    align = 1; break;
        case K::I32: case K::U32: case K::F32:                       align = 4; break;
        case K::I56: case K::U56:                                    align = 1; break;
        case K::I64: case K::U64: case K::F64:
        case K::Ptr: case K::Ref: case K::MutRef:                    align = 8; break;
        case K::I128: case K::U128:                                  align = 16; break;
        default: {
            mlir::Type elem_mlir = nullptr;
            if (elem_type.kind() == K::Struct || elem_type.kind() == K::ZonedStruct) {
                auto cname = concrete_struct_name(elem_type);
                auto sit = struct_types_.find(cname);
                if (sit != struct_types_.end()) elem_mlir = sit->second.llvm_type;
            }
            if (!elem_mlir) elem_mlir = logos_to_mlir(elem_type);
            if (elem_mlir) {
                auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
                auto a = (int64_t)dl.getTypeABIAlignment(elem_mlir);
                if (a > 0) align = a;
            }
            break;
        }
        }
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, align, 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPtrArithView v, TypeRef) {
    auto ptr_ref    = v.ptr();
    auto offset_ref = v.offset();
    auto op         = EPtrArith::Op(v.op_code());
    auto* ptr_le    = lexpr_of(ptr_ref);
    auto* off_le    = lexpr_of(offset_ref);
    if (!ptr_le || !off_le) return nullptr;
    auto p = gen_expr(*ptr_le);
    auto n = gen_expr(*off_le);
    if (!p || !n) return nullptr;
    // Widen/narrow offset to i64 just in case.
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(n.getType()))
        if (it.getWidth() != 64)
            n = coerce_int(n, builder_.getI64Type(), off_le->type);
    // Negate for Sub variants.
    if (op == EPtrArith::ByteSub || op == EPtrArith::Sub) {
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        n = builder_.create<mlir::arith::SubIOp>(loc_, zero, n);
    }
    mlir::Type elem_ty = builder_.getI8Type();  // default: byte indexing
    if (op == EPtrArith::Add || op == EPtrArith::Sub) {
        // Element indexing uses the pointee type from the receiver.
        TypeRef pt = ptr_le->type;
        if (pt && pt.pointee()) {
            // Struct/Datatype want their aggregate LLVM type, not ptr.
            if (pt.pointee().kind() == LogosType::Kind::Struct ||
                pt.pointee().kind() == LogosType::Kind::ZonedStruct) {
                auto cname = concrete_struct_name(pt.pointee());
                auto sit = struct_types_.find(cname);
                if (sit != struct_types_.end())
                    elem_ty = sit->second.llvm_type;
                else
                    elem_ty = logos_to_mlir(pt.pointee());
            } else {
                elem_ty = logos_to_mlir(pt.pointee());
            }
        }
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{n};
    return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_ty, p, idx);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPtrDiffView v, TypeRef) {
    auto* lhs_le = lexpr_of(v.lhs());
    auto* rhs_le = lexpr_of(v.rhs());
    if (!lhs_le || !rhs_le) return nullptr;
    auto a = gen_expr(*lhs_le);
    auto b = gen_expr(*rhs_le);
    if (!a || !b) return nullptr;
    auto i64ty = builder_.getI64Type();
    auto ai = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, a);
    auto bi = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, b);
    mlir::Value diff = builder_.create<mlir::arith::SubIOp>(loc_, ai, bi);
    if (v.by_byte()) return diff;
    // Element distance: diff / sizeof(pointee).
    TypeRef pt = lhs_le->type;
    if (!pt || !pt.pointee()) return diff;
    mlir::Type elem_mlir = nullptr;
    if (pt.pointee().kind() == LogosType::Kind::Struct ||
        pt.pointee().kind() == LogosType::Kind::ZonedStruct) {
        auto cname = concrete_struct_name(pt.pointee());
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) elem_mlir = sit->second.llvm_type;
    }
    if (!elem_mlir) elem_mlir = logos_to_mlir(pt.pointee());
    if (!elem_mlir) return diff;
    // sizeof trick.
    mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    mlir::Value null_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero);
    llvm::SmallVector<mlir::LLVM::GEPArg> one{int32_t(1)};
    auto size_ptr = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, null_ptr, one);
    auto sz = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, size_ptr);
    return builder_.create<mlir::arith::DivSIOp>(loc_, diff, sz);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETypeCodeOfView, TypeRef) {
    // Should have been folded to ELitInt by mono.  Emit 0 as a defensive
    // fallback (not expected to be reached for well-formed programs).
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EBlockExprView v, TypeRef) {
    if (auto br = v.block(); br) gen_block(br);
    if (is_terminated(builder_.getBlock())) return nullptr;
    if (auto rr = v.result(); rr) {
        if (auto* r = lexpr_of(rr)) return gen_expr(*r);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Try expression: expr?
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETryView v, TypeRef type) {
    auto* inner = lexpr_of(v.inner());
    if (!inner) return nullptr;
    auto inner_ptr = gen_expr(*inner);
    if (!inner_ptr) return nullptr;
    // Aggregate returned by value — spill to alloca so GEP works below.
    inner_ptr = spill_to_alloca(inner_ptr);

    auto* te = resolve_tagged_enum(std::string(TypeRef(inner->type).enum_name()), inner->type);
    if (!te) {
        std::fprintf(stderr, "mlir_gen: ETry: cannot resolve Result enum\n");
        return nullptr;
    }

    // Load discriminant at offset (0,0)
    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
    auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), te->llvm_type, inner_ptr, di);
    auto disc     = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), disc_ptr);
    auto ok_cst   = builder_.create<mlir::arith::ConstantIntOp>(loc_, v.ok_disc(), 32);
    auto is_ok    = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, disc, ok_cst);

    auto ok_mlir = logos_to_mlir(type);
    if (!ok_mlir) return nullptr;
    auto result_alloca = create_entry_alloca(ok_mlir);

    auto* region      = builder_.getBlock()->getParent();
    auto* ok_block    = new mlir::Block();
    auto* err_block   = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(ok_block);
    region->push_back(err_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, is_ok, ok_block, err_block);

    // ── ok_block: extract T payload → store to result_alloca ──────────
    builder_.setInsertionPointToStart(ok_block);
    {
        const TaggedEnumInfo::VariantPayload* ok_vp = nullptr;
        int32_t ok_d = v.ok_disc();
        for (auto& vp : te->variants) if (vp.disc == ok_d) { ok_vp = &vp; break; }

        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, inner_ptr, pi);
        if (ok_vp && !ok_vp->field_types.empty()) {
            auto ps  = mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), ok_vp->field_types);
            llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
            auto fp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ps, pay_ptr, fi);
            TypeRef lt = ok_vp->logos_types.empty() ? nullptr : ok_vp->logos_types[0];
            bool is_inline = lt && (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                                    TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                                    TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                                    TypeRef(lt).kind() == LogosType::Kind::Slice ||
                                    TypeRef(lt).kind() == LogosType::Kind::Closure);
            mlir::Value val;
            if (is_inline)
                val = fp;
            else
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, ok_vp->field_types[0], fp);
            builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_int(val, ok_mlir), result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    // ── err_block: extract E payload, build Err return, early func.return ──
    builder_.setInsertionPointToStart(err_block);
    {
        const TaggedEnumInfo::VariantPayload* err_vp = nullptr;
        int32_t err_d = v.err_disc();
        for (auto& vp : te->variants) if (vp.disc == err_d) { err_vp = &vp; break; }

        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, inner_ptr, pi);

        auto ret_alloca = create_entry_alloca(te->llvm_type);
        // Store err discriminant
        llvm::SmallVector<mlir::LLVM::GEPArg> di2{int32_t(0), int32_t(0)};
        auto rdp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, ret_alloca, di2);
        auto edc = builder_.create<mlir::arith::ConstantIntOp>(loc_, v.err_disc(), 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, edc, rdp);
        // Copy E payload if it exists
        if (err_vp && !err_vp->field_types.empty()) {
            auto src_ps = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), err_vp->field_types);
            llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
            auto src_fp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), src_ps, pay_ptr, fi);
            auto err_val = builder_.create<mlir::LLVM::LoadOp>(
                loc_, err_vp->field_types[0], src_fp);
            llvm::SmallVector<mlir::LLVM::GEPArg> rpi{int32_t(0), int32_t(1)};
            auto rpp = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), te->llvm_type, ret_alloca, rpi);
            auto dst_ps = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), err_vp->field_types);
            auto dst_fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dst_ps, rpp, fi);
            builder_.create<mlir::LLVM::StoreOp>(loc_, err_val, dst_fp);
        }
        // Return: enums are returned as *ptr; struct-return is also handled
        if (cur_ret_type_ == ptr_type()) {
            builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{ret_alloca});
        } else if (cur_ret_type_ && mlir::isa<mlir::LLVM::LLVMStructType>(cur_ret_type_)) {
            auto ret_val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, ret_alloca);
            builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{ret_val});
        } else {
            builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{});
        }
    }

    // ── merge_block: yield Ok value ────────────────────────────────────
    builder_.setInsertionPointToStart(merge_block);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, ok_mlir, result_alloca);
}

// ---------------------------------------------------------------------------
// Hermes SDN literal — zone blob builder (C++ Hermes API + clone())
// ---------------------------------------------------------------------------
//
// Strategy: construct the literal's HermesVal tree into a live mutable
// Hermes document via the public C++ Hermes API (ObjectArray / ObjectMap /
// TypedArray<T> / TypedMap<K,V> / ArenaString / anyval_put), then clone()
// it into a packed arena. Extract the packed bytes as the emit blob.
// PARAM slot offsets come from clone()'s out_params — single source of
// truth for both wire format and PARAM bookkeeping.

namespace {

using logos::hermes::AnyVal;
using logos::hermes::Arena;
using logos::hermes::ArenaMode;
using logos::hermes::ArenaString;
using logos::hermes::HermesAccess;
using logos::hermes::MapI32AnyVal;
using logos::hermes::MapU32AnyVal;
using logos::hermes::MapI64AnyVal;
using logos::hermes::MapU64AnyVal;
using logos::hermes::ObjectArray;
using logos::hermes::ObjectMap;
using logos::hermes::TypedArray;
using logos::hermes::arena_offset_t;
using logos::hermes::anyval_put;
using logos::hermes::make_doc;

struct HermesZoneBuild {
    std::vector<uint8_t>                        blob;
    std::vector<std::pair<uint32_t, uint32_t>>  param_slots;  // (blob_off, value_idx)
};

// Build a HermesVal into the live `doc`, returning the raw AnyVal u32.
// For PARAM (HVCapture), returns the inline PARAM raw; the caller writes it
// into the slot, and clone() will pick it up via its out_params bookkeeping.
static uint32_t build_hermes_val(lir_view::HermesValRef v,
                                 logos::hermes::Hermes& doc);

static uint32_t ptr_anyval_raw(const void* obj, logos::hermes::Hermes& doc) {
    const uint8_t* base = HermesAccess::base(doc);
    uint32_t off = static_cast<uint32_t>(
        static_cast<const uint8_t*>(obj) - base);
    return AnyVal::from_offset(arena_offset_t(off)).raw();
}

static uint32_t build_object_array(lir_view::HVArrayView arr,
                                   logos::hermes::Hermes& doc) {
    uint64_t n = arr.size();
    auto* a = ObjectArray::create(HermesAccess::arena(doc),
                                  n ? n : uint64_t{4}).get();
    uint32_t a_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - HermesAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t elem_raw = build_hermes_val(arr.elem(i), doc);
        auto* cur = reinterpret_cast<ObjectArray*>(
            HermesAccess::base(doc) + a_off);
        cur->push_back(AnyVal::from_raw(elem_raw),
                       HermesAccess::arena(doc)).get();
    }
    return AnyVal::from_offset(arena_offset_t(a_off)).raw();
}

template <typename T>
static uint32_t build_typed_array_scalar(lir_view::HVArrayView arr,
                                         logos::hermes::Hermes& doc) {
    uint64_t n = arr.size();
    auto* a = TypedArray<T>::create(HermesAccess::arena(doc),
                                    n ? n : uint64_t{4}).get();
    uint32_t a_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - HermesAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        T val = 0;
        auto er = arr.elem(i);
        if (er && er.kind() == lir_schema::hermes_val::Code::Int) {
            val = static_cast<T>(lir_view::HVIntView{er}.value());
        }
        auto* cur = reinterpret_cast<TypedArray<T>*>(
            HermesAccess::base(doc) + a_off);
        cur->push_back(val, HermesAccess::arena(doc)).get();
    }
    return AnyVal::from_offset(arena_offset_t(a_off)).raw();
}

static uint32_t build_array(lir_view::HVArrayView arr,
                            logos::hermes::Hermes& doc) {
    auto et = arr.elem_type();
    if (et == "I8")  return build_typed_array_scalar<int8_t>(arr, doc);
    if (et == "U8")  return build_typed_array_scalar<uint8_t>(arr, doc);
    if (et == "I16") return build_typed_array_scalar<int16_t>(arr, doc);
    if (et == "U16") return build_typed_array_scalar<uint16_t>(arr, doc);
    if (et == "I32") return build_typed_array_scalar<int32_t>(arr, doc);
    if (et == "U32") return build_typed_array_scalar<uint32_t>(arr, doc);
    if (et == "I64") return build_typed_array_scalar<int64_t>(arr, doc);
    if (et == "U64") return build_typed_array_scalar<uint64_t>(arr, doc);
    if (et == "F32") return build_typed_array_scalar<float>(arr, doc);
    if (et == "F64") return build_typed_array_scalar<double>(arr, doc);
    return build_object_array(arr, doc);
}

static uint32_t build_object_map(lir_view::HVMapView map,
                                 logos::hermes::Hermes& doc) {
    uint64_t n = map.size();
    uint32_t cap = 8;
    while (cap < n * 2 || cap < 8) cap <<= 1;
    auto* m = ObjectMap::create(HermesAccess::arena(doc), cap).get();
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        std::string key_str = map.int_keyed()
            ? std::to_string(map.int_key(i))
            : std::string(map.str_key(i));
        uint32_t val_raw = build_hermes_val(map.value(i), doc);
        auto* cur = reinterpret_cast<ObjectMap*>(
            HermesAccess::base(doc) + m_off);
        cur->put(key_str, AnyVal::from_raw(val_raw),
                 HermesAccess::arena(doc)).get();
    }
    return AnyVal::from_offset(arena_offset_t(m_off)).raw();
}

template <typename Map, typename K>
static uint32_t build_typed_map_anyval(lir_view::HVMapView map,
                                       logos::hermes::Hermes& doc) {
    uint64_t n = map.size();
    uint32_t cap = n == 0 ? 1 : static_cast<uint32_t>(n);
    auto* m = Map::create(HermesAccess::arena(doc), cap).get();
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        K key = static_cast<K>(map.int_key(i));
        uint32_t val_raw = build_hermes_val(map.value(i), doc);
        auto* cur = reinterpret_cast<Map*>(
            HermesAccess::base(doc) + m_off);
        cur->put(key, AnyVal::from_raw(val_raw), HermesAccess::base(doc));
    }
    return AnyVal::from_offset(arena_offset_t(m_off)).raw();
}

static uint32_t build_map(lir_view::HVMapView map,
                          logos::hermes::Hermes& doc) {
    auto kt = map.key_type();
    if (kt == "I32") return build_typed_map_anyval<MapI32AnyVal, int32_t>(map, doc);
    if (kt == "U32") return build_typed_map_anyval<MapU32AnyVal, uint32_t>(map, doc);
    if (kt == "I64") return build_typed_map_anyval<MapI64AnyVal, int64_t>(map, doc);
    if (kt == "U64") return build_typed_map_anyval<MapU64AnyVal, uint64_t>(map, doc);
    return build_object_map(map, doc);
}

static uint32_t build_hermes_val(lir_view::HermesValRef v,
                                 logos::hermes::Hermes& doc) {
    if (!v) return 0;
    using HC = lir_schema::hermes_val::Code;
    switch (v.kind()) {
    case HC::Null:
        return 0;
    case HC::Bool:
        // Boolean: type_hash=37 (see any_val.hpp).
        return AnyVal::from_value<uint8_t>(
            lir_view::HVBoolView{v}.value() ? 1 : 0, 37).raw();
    case HC::Int: {
        int64_t iv = lir_view::HVIntView{v}.value();
        if (iv >= -8388608LL && iv <= 8388607LL) {
            return AnyVal::from_value<int32_t>(
                static_cast<int32_t>(iv)).raw();
        }
        return anyval_put<int64_t>(HermesAccess::arena(doc), iv).get().raw();
    }
    case HC::Float:
        return anyval_put<double>(
            HermesAccess::arena(doc),
            lir_view::HVFloatView{v}.value()).get().raw();
    case HC::Str: {
        auto sv = lir_view::HVStrView{v}.value();
        auto* s = ArenaString::create(
            HermesAccess::arena(doc), std::string(sv)).get();
        return ptr_anyval_raw(s, doc);
    }
    case HC::Array:
        return build_array(lir_view::HVArrayView{v}, doc);
    case HC::Map:
        return build_map(lir_view::HVMapView{v}, doc);
    case HC::Capture:
        // Inline PARAM (tc=127): raw = (value_index << 8) | 0xFF.
        return (lir_view::HVCaptureView{v}.value_index() << 8u) | 0xFFu;
    case HC::Type: {
        // Component-metaprog slice 1C: emit a TinyObjectMap whose
        // schema_type_code = type_hash::Type=107 carrying:
        //   key 0: kind (u32, inline AnyVal)
        //   key 1: uid  (u64, ptr-mode AnyVal)
        //   key 2: name (ArenaString ptr-mode AnyVal)
        lir_view::HVTypeView tv{v};
        auto& arena = HermesAccess::arena(doc);
        auto* m = logos::hermes::TinyObjectMap::create(arena, /*cap=*/4).get();
        m->set_schema_type_code(logos::hermes::type_hash::Type);
        uint32_t m_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));

        AnyVal kind_av = AnyVal::from_value<uint32_t>(tv.kind());
        AnyVal uid_av  = anyval_put<uint64_t>(arena, tv.uid()).get();
        auto*  s       = ArenaString::create(arena, std::string(tv.name())).get();
        AnyVal name_av = AnyVal::from_offset(arena_offset_t(static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(s) - HermesAccess::base(doc))));

        auto* cur = reinterpret_cast<logos::hermes::TinyObjectMap*>(
            HermesAccess::base(doc) + m_off);
        cur->put(0, kind_av, arena).get();
        cur = reinterpret_cast<logos::hermes::TinyObjectMap*>(
            HermesAccess::base(doc) + m_off);
        cur->put(1, uid_av, arena).get();
        cur = reinterpret_cast<logos::hermes::TinyObjectMap*>(
            HermesAccess::base(doc) + m_off);
        cur->put(2, name_av, arena).get();

        return AnyVal::from_offset(arena_offset_t(m_off)).raw();
    }
    }
    return 0;
}

// Build the full zone blob for an EHermesLit node.
// Steps:
//   1. Make a fresh doc (DocumentHeader at offset 0).
//   2. Build the root value tree.
//   3. Write root AnyVal.raw into DocumentHeader (works for inline + ptr
//      alike — AnyVal bit0 disambiguates on read; see Task 1 in clone.cpp).
//   4. clone() → packed arena + PARAM slot list.
//   5. Extract bytes from packed head() chunk.
static HermesZoneBuild build_hermes_zone(lir_view::EHermesLitView e) {
    auto doc = make_doc().get();
    uint32_t root_raw = build_hermes_val(e.root(), doc);
    HermesAccess::set_root_offset(doc, arena_offset_t(root_raw));

    std::vector<logos::hermes::ParamSlot> params;
    auto packed = logos::hermes::clone(doc, &params).get();

    auto& packed_arena = HermesAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();

    HermesZoneBuild out;
    out.blob.assign(data, data + used);
    out.param_slots.reserve(params.size());
    for (auto& p : params)
        out.param_slots.emplace_back(p.offset, p.value_index);
    return out;
}
}  // namespace (zone builder helpers)

// Coerce a Logos runtime value to AnyVal.raw (u32) for hermes capture substitution.
// Handles scalars that fit in 24 bits (embed_i24/embed_bool/etc.) and AnyVal passthrough.
// String/large-integer coercion is implemented in C5.
mlir::Value MLIRGenImpl::coerce_to_anyval_raw(mlir::Value v, TypeRef t) {
    if (!v || !t) return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    auto i32_mlir = builder_.getIntegerType(32);
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: {
            // C4 bug fix: AnyVal::embed_bool uses type_hash=37, tag_byte=0x4B (not 0x4D=38).
            // build_hermes_val uses 0x4Bu; coerce must match.
            // raw = (bool_val << 8) | 0x4B
            mlir::Value b = coerce_numeric(v, i32_mlir);
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, b,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0x4B, 32));
        }
        case K::I8:  case K::I16: case K::I32:
        case K::U8:  case K::U16: case K::U32:
        case K::I24: case K::U24: {
            // AnyVal::embed_i24: raw = ((v & 0xFFFFFF) << 8) | 0x2F (type_hash=23=0x17)
            mlir::Value iv = coerce_numeric(v, i32_mlir);
            mlir::Value masked = builder_.create<mlir::arith::AndIOp>(loc_, iv,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0xFFFFFF, 32));
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, masked,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0x2F, 32));
        }
        case K::I64: case K::U64: {
            // Truncate to low 24 bits and embed as i24. Values outside ±8M need C5.
            mlir::Value iv = coerce_numeric(v, i32_mlir);
            mlir::Value masked = builder_.create<mlir::arith::AndIOp>(loc_, iv,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0xFFFFFF, 32));
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, masked,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0x2F, 32));
        }
        case K::F32: case K::F64:
            // C4 bug fix: F32/F64 need zone-alloc RelPtr encoding (C5).
            // is_capturable no longer allows these; return null AnyVal as fallback.
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        case K::Ptr: case K::Ref: case K::MutRef:
            // C4 bug fix: pointer/reference captures need varchar/C5 zone alloc.
            // is_capturable no longer allows these; return null AnyVal as fallback.
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        case K::Struct:
            if (TypeRef(t).struct_name() == "AnyVal") {
                // C4 bug fix: use mlir::ArrayRef (not llvm::ArrayRef) for ExtractValueOp
                // to match the MLIR dialect API which takes mlir::ArrayRef<int64_t>.
                return builder_.create<mlir::LLVM::ExtractValueOp>(
                    loc_, v, mlir::ArrayRef<int64_t>{0});
            }
            break;
        default:
            break;
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EReflectOfView v, TypeRef) {
    auto i8 = builder_.getIntegerType(8);

    // Compute symbol name deterministically from fqn (same formula as reflection_emit).
    std::string fqn;
    if (TypeRef et = v.type(pool_impl())) {
        auto pkg = et.pkg_name();
        auto sn  = et.struct_name();
        fqn = pkg.empty() ? std::string(sn) : std::string(pkg) + "::" + std::string(sn);
    }
    auto hash = logos::compiler::type_hash_23(fqn);
    static const char hexc[] = "0123456789abcdef";
    std::string sym_name = "__logos_reflect__";
    for (auto b : hash) { sym_name += hexc[b >> 4]; sym_name += hexc[b & 0xF]; }

    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    // Forward-declare the global as external if not already in the module.
    // reflection_emit emitted the real WeakODR global earlier in the same module.
    if (!parent_mod.lookupSymbol(sym_name)) {
        auto save_pt = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());
        auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, 1);
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::External,
            sym_name, mlir::Attribute{});
        builder_.restoreInsertionPoint(save_pt);
    }

    // ptr = address_of(global) + 8  (past size prefix, pointing to Hermes payload)
    auto global_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), sym_name);
    mlir::Value offset8 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
    auto blob_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), i8, global_ptr, mlir::ValueRange{offset8});

    // Return HermesStatic { ptr: blob_ptr } as an alloca.
    auto sit = struct_types_.find("HermesStatic");
    if (sit == struct_types_.end()) return blob_ptr;
    auto alloca = create_entry_alloca(sit->second.llvm_type);
    auto gep = gep_field(alloca, sit->second, "ptr");
    if (!gep) return blob_ptr;
    builder_.create<mlir::LLVM::StoreOp>(loc_, blob_ptr, gep);
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EHermesLitView v, TypeRef ret_type) {
    // Metacall splice path: pre-serialised blob bypasses build_hermes_zone.
    // Same rodata layout as the captures-free @-literal: [u64 size][bytes],
    // ptr returned by the wrapper points after the size prefix.
    // Helper: emit "AddressOf(global) + 8 → HermesStatic alloca" given a global
    // symbol name. Used both on cache hit and after a fresh global is created.
    auto materialize_static = [&](const std::string& global_name) -> mlir::Value {
        auto i8 = builder_.getIntegerType(8);
        auto global_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
        mlir::Value offset8 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
        auto blob_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i8, global_ptr, mlir::ValueRange{offset8});
        auto sit = struct_types_.find("HermesStatic");
        if (sit == struct_types_.end()) return blob_ptr;
        auto alloca = create_entry_alloca(sit->second.llvm_type);
        auto gep = gep_field(alloca, sit->second, "ptr");
        if (!gep) return blob_ptr;
        builder_.create<mlir::LLVM::StoreOp>(loc_, blob_ptr, gep);
        return alloca;
    };

    if (auto sb = v.static_blob(); !sb.empty()) {
        auto i8 = builder_.getIntegerType(8);
        auto size_le = static_cast<uint64_t>(sb.size());
        std::string prefixed(8, '\0');
        for (int k = 0; k < 8; ++k)
            prefixed[k] = static_cast<char>((size_le >> (k * 8)) & 0xFF);
        prefixed.append(sb.begin(), sb.end());

        // Content-keyed cache: identical bytes share one rodata global.
        if (auto cit = hermes_lit_global_cache_.find(prefixed); cit != hermes_lit_global_cache_.end()) {
            return materialize_static(cit->second);
        }

        auto lit_idx     = hermes_lit_counter_++;
        auto global_name = "__hermes_blob_" + std::to_string(lit_idx);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt     = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto arr_type  = mlir::LLVM::LLVMArrayType::get(i8, prefixed.size());
        auto blob_attr = builder_.getStringAttr(
            llvm::StringRef(prefixed.data(), prefixed.size()));
        auto blob_global = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            global_name, blob_attr);
        blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);
        builder_.restoreInsertionPoint(save_pt);

        hermes_lit_global_cache_[prefixed] = global_name;
        return materialize_static(global_name);
    }

    auto [blob, param_slots] = build_hermes_zone(v);
    bool has_captures = v.has_captures();
    std::vector<TypeRef> capture_types;
    v.each_capture_type(pool_impl(), [&](TypeRef t){ capture_types.push_back(t); });
    std::vector<const LExpr*> capture_exprs;
    v.each_capture_expr([&](lir_view::ExprRef er){
        capture_exprs.push_back(lexpr_of(er));
    });

    auto i8 = builder_.getIntegerType(8);

    // C8e: static @-literals (no captures) get an 8-byte little-endian size
    // prefix in rodata so that HermesStatic::size() can read *(ptr - 8). The
    // resulting bytes are content-keyed in hermes_lit_global_cache_ so
    // multiple references to the same const-value (e.g. an associated
    // constant accessed at multiple call sites) share one rodata global and
    // therefore one address.
    if (!has_captures) {
        auto size_le = static_cast<uint64_t>(blob.size());
        std::string prefixed(8, '\0');
        for (int k = 0; k < 8; ++k)
            prefixed[k] = static_cast<char>((size_le >> (k * 8)) & 0xFF);
        prefixed.append(blob.begin(), blob.end());

        if (auto cit = hermes_lit_global_cache_.find(prefixed); cit != hermes_lit_global_cache_.end()) {
            return materialize_static(cit->second);
        }

        auto lit_idx     = hermes_lit_counter_++;
        auto global_name = "__hermes_lit_" + std::to_string(lit_idx);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt     = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto arr_type  = mlir::LLVM::LLVMArrayType::get(i8, prefixed.size());
        auto blob_attr = builder_.getStringAttr(
            llvm::StringRef(prefixed.data(), prefixed.size()));
        auto blob_global = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            global_name, blob_attr);
        blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);
        builder_.restoreInsertionPoint(save_pt);

        hermes_lit_global_cache_[prefixed] = global_name;
        return materialize_static(global_name);
    }

    // Capture path: distinct lit_idx + slots table; runtime-evaluated captures
    // mean we don't dedupe these globals.
    auto lit_idx    = hermes_lit_counter_++;
    auto global_name = "__hermes_lit_" + std::to_string(lit_idx);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    // Capture path: emit plain blob (no size prefix).
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, blob.size());
    auto blob_attr = builder_.getStringAttr(
        llvm::StringRef(reinterpret_cast<const char*>(blob.data()), blob.size()));
    auto cap_blob_global = builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
        global_name, blob_attr);
    cap_blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);

    // ── Capture path ─────────────────────────────────────────────────────────
    // Emit slots table: array of u32 pairs [blob_off, value_idx, ...].
    auto slots_name = "__hermes_slots_" + std::to_string(lit_idx);
    size_t n_slots  = param_slots.size();
    size_t n_values = capture_exprs.size();

    {
        auto u32_type  = builder_.getIntegerType(32);
        auto slots_arr = mlir::LLVM::LLVMArrayType::get(u32_type, n_slots * 2);
        llvm::SmallVector<uint32_t> slot_vals;
        for (auto& [off, vidx] : param_slots) { slot_vals.push_back(off); slot_vals.push_back(vidx); }
        auto slots_attr = mlir::DenseIntElementsAttr::get(
            mlir::RankedTensorType::get({static_cast<int64_t>(n_slots * 2)}, u32_type),
            llvm::SmallVector<uint32_t>(slot_vals));
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, slots_arr, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            slots_name, slots_attr);
    }

    builder_.restoreInsertionPoint(save_pt);

    // Check if any capture requires zone allocation (f64, string, *const u8).
    // Zone-alloc captures need the Hermes to exist before coercion, so we
    // use the hermes_template_ctr_new + hermes_ctr_alloc_* + hermes_template_patch path.
    auto is_zone_alloc_cap = [](TypeRef t) -> bool {
        if (!t) return false;
        using K = LogosType::Kind;
        K tk = TypeRef(t).kind();
        if (tk == K::F64 || tk == K::F32 || tk == K::FloatLit) return true;
        if (tk == K::Ptr) return true;  // *const u8 → C-string varchar
        if (tk == K::Slice && TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8) return true; // str → varchar
        if (tk == K::Struct && TypeRef(t).struct_name() == "StringView") return true;
        return false;
    };
    bool any_zone_alloc = false;
    for (auto ct : capture_types) {
        if (is_zone_alloc_cap(ct)) { any_zone_alloc = true; break; }
    }

    // Shared: build slots_ptr, tmpl_ptr, tmpl_size_val, n_slots_v, n_values_v.
    mlir::Value tmpl_ptr_v = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
    mlir::Value tmpl_size_v = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(blob.size()), 64);
    mlir::Value slots_ptr_v = n_slots > 0
        ? builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), slots_name).getResult()
        : [&]() -> mlir::Value {
            mlir::Value z = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
            return builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), z);
          }();
    mlir::Value n_slots_v  = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n_slots), 64);
    mlir::Value n_values_v = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n_values), 64);

    // Allocate resolved[] on stack: n_values × u32.
    mlir::Value resolved_ptr = nullptr;
    auto u32_mlir = builder_.getIntegerType(32);
    if (n_values > 0) {
        auto arr_t = mlir::LLVM::LLVMArrayType::get(u32_mlir, n_values);
        resolved_ptr = create_entry_alloca(arr_t);
    } else {
        mlir::Value zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        resolved_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero64);
    }

    // ── Zone-alloc path (C5): one or more captures need varchar/f64 in the zone. ─
    if (any_zone_alloc) {
        auto new_fn    = find_func_op(parent_mod, "hermes_template_ctr_new");
        auto patch_fn  = find_func_op(parent_mod, "hermes_template_patch");
        auto alloc_f64_fn = find_func_op(parent_mod, "hermes_ctr_alloc_f64");
        auto alloc_str_fn = find_func_op(parent_mod, "hermes_ctr_alloc_str");
        auto alloc_cstr_fn = find_func_op(parent_mod, "hermes_ctr_alloc_cstr");
        // C5-fix4: check all alloc helpers upfront — missing functions cause silent null AnyVal.
        if (!new_fn || !patch_fn || !alloc_f64_fn || !alloc_str_fn || !alloc_cstr_fn) {
            std::fprintf(stderr, "mlir_gen: hermes zone-alloc helpers not found — "
                         "add 'use std.hermes.ctr;' to your file\n");
            return nullptr;
        }

        // Count zone-alloc captures for capacity estimate (4096 per string, 16 per f64/f32).
        // C5-fix3: only count zone-alloc captures (skip scalar/AnyVal captures).
        // C5-fix2: include K::FloatLit in the f64 branch (16 bytes), not the string branch.
        int64_t extra_cap_bytes = 0;
        for (auto ct : capture_types) {
            using K = LogosType::Kind;
            if (!ct || !is_zone_alloc_cap(ct)) continue;
            K ctk = TypeRef(ct).kind();
            if (ctk == K::F64 || ctk == K::F32 || ctk == K::FloatLit)
                extra_cap_bytes += 16;
            else
                extra_cap_bytes += 4096;  // string: generous estimate
        }
        mlir::Value extra_cap_v = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, extra_cap_bytes, 64);

        // Create the Hermes with template pre-loaded.
        auto new_call = builder_.create<mlir::func::CallOp>(
            loc_, new_fn,
            mlir::ValueRange{tmpl_ptr_v, tmpl_size_v, extra_cap_v});
        if (new_call.getNumResults() == 0) return nullptr;
        mlir::Value ctr_val  = new_call.getResult(0);
        mlir::Type  ctr_type = new_fn.getFunctionType().getResult(0);

        // Alloca Hermes so we can take its address for alloc helpers.
        mlir::Value ctr_alloca = create_entry_alloca(ctr_type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, ctr_val, ctr_alloca);

        // For each unique capture: gen_expr, coerce, store in resolved[i].
        for (size_t i = 0; i < n_values; ++i) {
            mlir::Value cap_val = gen_expr(*capture_exprs[i]);
            if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

            TypeRef ct = capture_types[i];
            mlir::Value raw_u32 = nullptr;

            if (is_zone_alloc_cap(ct)) {
                using K = LogosType::Kind;
                K ctk = TypeRef(ct).kind();
                if ((ctk == K::F64 || ctk == K::F32 ||
                     ctk == K::FloatLit) && alloc_f64_fn) {
                    // Widen f32 → f64 if needed. FloatLit defaults to f64.
                    mlir::Value f64_val = cap_val;
                    if (ctk == K::F32) {
                        auto f64_type = builder_.getF64Type();
                        f64_val = builder_.create<mlir::arith::ExtFOp>(loc_, f64_type, cap_val);
                    }
                    // If FloatLit/F64 but value is f32-typed MLIR, widen.
                    if (f64_val && mlir::isa<mlir::Float32Type>(f64_val.getType())) {
                        auto f64_type = builder_.getF64Type();
                        f64_val = builder_.create<mlir::arith::ExtFOp>(loc_, f64_type, f64_val);
                    }
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_f64_fn, mlir::ValueRange{ctr_alloca, f64_val});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Ptr && alloc_cstr_fn) {
                    // *const u8 — treat as null-terminated C-string.
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_cstr_fn, mlir::ValueRange{ctr_alloca, cap_val});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Slice && TypeRef(ct).elem() && TypeRef(ct).elem().kind() == K::U8
                           && alloc_str_fn) {
                    // str (&[u8]) fat pointer — load ptr+len fields from the alloca.
                    auto stype = slice_llvm_type();
                    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
                    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, cap_val, pi);
                    mlir::Value sv_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
                    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
                    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, cap_val, li);
                    mlir::Value sv_len = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, builder_.getI64Type(), lp);
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_str_fn, mlir::ValueRange{ctr_alloca, sv_ptr, sv_len});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Struct && TypeRef(ct).struct_name() == "StringView"
                           && alloc_str_fn) {
                    // StringView: extract ptr (field 0) and len (field 1).
                    mlir::Value sv_ptr = builder_.create<mlir::LLVM::ExtractValueOp>(
                        loc_, cap_val, mlir::ArrayRef<int64_t>{0});
                    mlir::Value sv_len = builder_.create<mlir::LLVM::ExtractValueOp>(
                        loc_, cap_val, mlir::ArrayRef<int64_t>{1});
                    // len is u64; hermes_ctr_alloc_str takes i64 — reinterpret as i64.
                    auto i64_type = builder_.getIntegerType(64);
                    if (sv_len.getType() != i64_type)
                        sv_len = builder_.create<mlir::arith::BitcastOp>(loc_, i64_type, sv_len);
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_str_fn, mlir::ValueRange{ctr_alloca, sv_ptr, sv_len});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                }
            } else {
                raw_u32 = coerce_to_anyval_raw(cap_val, ct);
            }

            if (!raw_u32) raw_u32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

            // Store to resolved[i].
            llvm::SmallVector<mlir::Value> gep_idx{
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64),
                builder_.create<mlir::arith::ConstantIntOp>(loc_, static_cast<int64_t>(i), 64)};
            auto arr_t = mlir::LLVM::LLVMArrayType::get(u32_mlir, n_values);
            auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), arr_t, resolved_ptr, gep_idx);
            builder_.create<mlir::LLVM::StoreOp>(loc_, raw_u32, slot_ptr);
        }

        // Patch PARAM slots in the cloned zone.
        builder_.create<mlir::func::CallOp>(
            loc_, patch_fn,
            mlir::ValueRange{ctr_alloca, slots_ptr_v, n_slots_v, resolved_ptr, n_values_v});

        // Return the Hermes by value (load from alloca).
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ctr_type, ctr_alloca);
    }

    // ── Scalar-only path (C4): all captures are inline AnyVal (no zone alloc). ──
    for (size_t i = 0; i < n_values; ++i) {
        mlir::Value cap_val = gen_expr(*capture_exprs[i]);
        if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

        mlir::Value raw_u32 = coerce_to_anyval_raw(cap_val, capture_types[i]);
        if (!raw_u32) raw_u32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

        llvm::SmallVector<mlir::Value> gep_idx{
            builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64),
            builder_.create<mlir::arith::ConstantIntOp>(loc_, static_cast<int64_t>(i), 64)};
        auto arr_t = mlir::LLVM::LLVMArrayType::get(u32_mlir, n_values);
        auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_t, resolved_ptr, gep_idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, raw_u32, slot_ptr);
    }

    auto build_fn = find_func_op(parent_mod, "hermes_build_from_template");
    if (!build_fn) {
        std::fprintf(stderr, "mlir_gen: hermes_build_from_template not found — "
                     "add 'use std.hermes.ctr;' to your file\n");
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> build_args{
        tmpl_ptr_v, tmpl_size_v, slots_ptr_v, n_slots_v, resolved_ptr, n_values_v};
    auto build_call = builder_.create<mlir::func::CallOp>(loc_, build_fn, mlir::ValueRange(build_args));
    if (build_call.getNumResults() == 0) return nullptr;
    return build_call.getResult(0);
}

} // namespace logos::compiler
