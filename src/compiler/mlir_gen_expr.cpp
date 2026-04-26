// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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
#include <logos/hermes/typed_array.hpp>

#include <cstring>

namespace logos::compiler {

using namespace lir;

namespace {
static mlir::func::FuncOp find_func_op(mlir::ModuleOp mod, std::string_view name) {
    if (auto fn = mod.lookupSymbol<mlir::func::FuncOp>(name))
        return fn;

    mlir::func::FuncOp found;
    mod.walk([&](mlir::func::FuncOp fn) {
        if (fn.getName().str() == name) {
            found = fn;
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return found;
}
}  // namespace

// ---------------------------------------------------------------------------
// gen_expr — main dispatcher
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr(const LExpr& e) {
    auto er = expr_ref_of(e);
    if (!er) {
        std::fprintf(stderr, "mlir_gen: gen_expr called without LIR mirror\n");
        return nullptr;
    }
    using C = lir_schema::expr::Code;
    switch (er.kind()) {
    case C::LitInt:     return gen_expr_kind(lir_view::ELitIntView{er},     e.type);
    case C::LitFloat:   return gen_expr_kind(lir_view::ELitFloatView{er},   e.type);
    case C::LitBool:    return gen_expr_kind(lir_view::ELitBoolView{er},    e.type);
    case C::LitStr:     return gen_expr_kind(lir_view::ELitStrView{er},     e.type);
    case C::VarRef:     return gen_expr_kind(lir_view::EVarRefView{er},     e.type);
    case C::EnumLit:    return gen_expr_kind(lir_view::EEnumLitView{er},    e.type);
    case C::EnumLitData:return gen_expr_kind(lir_view::EEnumLitDataView{er},e.type);
    case C::Call:       return gen_expr_kind(lir_view::ECallView{er},       e.type);
    case C::MethodCall: return gen_expr_kind(lir_view::EMethodCallView{er}, e.type);
    case C::BinOp:      return gen_expr_kind(lir_view::EBinOpView{er},      e.type);
    case C::Unary:      return gen_expr_kind(lir_view::EUnaryView{er},      e.type);
    case C::AddrOf:     return gen_expr_kind(lir_view::EAddrOfView{er},     e.type);
    case C::AddrOfTemp: return gen_expr_kind(lir_view::EAddrOfTempView{er}, e.type);
    case C::Deref:      return gen_expr_kind(lir_view::EDerefView{er},      e.type);
    case C::FieldRead:  return gen_expr_kind(lir_view::EFieldReadView{er},  e.type);
    case C::IndexRead:  return gen_expr_kind(lir_view::EIndexReadView{er},  e.type);
    case C::StructLit:  return gen_expr_kind(lir_view::EStructLitView{er},  e.type);
    case C::ArrLit:     return gen_expr_kind(lir_view::EArrLitView{er},     e.type);
    case C::Cast:       return gen_expr_kind(lir_view::ECastView{er},       e.type);
    case C::New:        return gen_expr_kind(lir_view::ENewView{er},        e.type);
    case C::IfExpr:     return gen_expr_kind(lir_view::EIfExprView{er},     e.type);
    case C::TupleLit:   return gen_expr_kind(lir_view::ETupleLitView{er},   e.type);
    case C::TupleIndex: return gen_expr_kind(lir_view::ETupleIndexView{er}, e.type);
    case C::SliceLit:   return gen_expr_kind(lir_view::ESliceLitView{er},   e.type);
    case C::SliceIndex: return gen_expr_kind(lir_view::ESliceIndexView{er}, e.type);
    case C::SliceLen:   return gen_expr_kind(lir_view::ESliceLenView{er},   e.type);
    case C::SlicePtr:   return gen_expr_kind(lir_view::ESlicePtrView{er},   e.type);
    case C::ClosureBox: return gen_expr_kind(lir_view::EClosureBoxView{er}, e.type);
    case C::ClosureCall:return gen_expr_kind(lir_view::EClosureCallView{er},e.type);
    case C::FnPtrCall:  return gen_expr_kind(lir_view::EFnPtrCallView{er},  e.type);
    case C::FormatCall: return gen_expr_kind(lir_view::EFormatCallView{er}, e.type);
    case C::PackExpand: return gen_expr_kind(lir_view::EPackExpandView{er}, e.type);
    case C::Try:        return gen_expr_kind(lir_view::ETryView{er},        e.type);
    case C::MatchExpr:  return gen_expr_kind(lir_view::EMatchExprView{er},  e.type);
    case C::SizeOf:     return gen_expr_kind(lir_view::ESizeOfView{er},     e.type);
    case C::TypeCodeOf: return gen_expr_kind(lir_view::ETypeCodeOfView{er}, e.type);
    case C::BlockExpr:  return gen_expr_kind(lir_view::EBlockExprView{er},  e.type);
    case C::HermesLit:  return gen_expr_kind(lir_view::EHermesLitView{er},  e.type);
    case C::PtrArith:   return gen_expr_kind(lir_view::EPtrArithView{er},   e.type);
    case C::PtrDiff:    return gen_expr_kind(lir_view::EPtrDiffView{er},    e.type);
    case C::ReflectOf:  return gen_expr_kind(lir_view::EReflectOfView{er},  e.type);
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
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EEnumLitData>(_le->kind);
    auto* te = resolve_tagged_enum(e.enum_name, type);
    if (!te) {
        std::fprintf(stderr, "mlir_gen: unknown tagged enum '%s'\n", e.enum_name.c_str());
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
    auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, e.disc, 32);
    builder_.create<mlir::LLVM::StoreOp>(loc_, disc_val, disc_ptr);
    // Store payload into field 1 (the [N x i8] area), bitcasted
    if (!e.payload.empty()) {
        // GEP to the payload area (field index 1)
        llvm::SmallVector<mlir::LLVM::GEPArg> pay_idx{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), info.llvm_type, alloca, pay_idx);
        // Find the variant's field types
        const TaggedEnumInfo::VariantPayload* vp = nullptr;
        for (auto& v : info.variants)
            if (v.disc == e.disc) { vp = &v; break; }
        if (vp) {
            // Build a struct type for this variant's payload
            llvm::SmallVector<mlir::Type> ft;
            for (auto& t : vp->field_types) ft.push_back(t);
            auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), ft);
            for (size_t i = 0; i < e.payload.size() && i < vp->field_types.size(); ++i) {
                auto val = gen_expr(*e.payload[i]);
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
    if (op == "+")  return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
    if (op == "-")  return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
    if (op == "*")  return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
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
    // For pointer comparisons, use llvm.icmp instead of arith.cmpi
    bool is_ptr_cmp = mlir::isa<mlir::LLVM::LLVMPointerType>(lhs.getType());
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
             TypeRef(lhs_l->type).kind() == LogosType::Kind::U128);
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
    return it->second;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAddrOfTempView v, TypeRef) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EAddrOfTemp>(_le->kind);
    // Materialize a temporary rvalue to an anonymous stack slot and return its address.
    // Aggregates (tuple, struct, array) are already pointer-represented by the codegen
    // (their gen_expr returns an alloca directly) — no extra wrapping needed.
    //
    // Special case: &mut <field_read> on an inline struct field must return a
    // GEP into the original struct, NOT a copy.  gen_expr(EFieldRead) always
    // loads, which would give us a by-value copy — useless for mutation.
    if (auto* fr = std::get_if<EFieldRead>(&e.inner->kind)) {
        auto [ptr, sname] = gen_recv_struct(*fr->receiver);
        if (ptr && !sname.empty()) {
            auto sit = struct_types_.find(sname);
            if (sit != struct_types_.end()) {
                auto& info = sit->second;
                auto gep = gep_field(ptr, info, fr->field);
                if (gep) return gep;  // field address into original struct
            }
        }
        // Fall through to the general path for non-struct fields.
    }
    // `&mut arr[i]` on a struct-element-typed array/pointer: take GEP address
    // directly instead of loading the struct by value and then needing to
    // re-spill.  Without this, EIndexRead's trailing LoadOp hands back a
    // struct value that subsequent struct-access ops mis-interpret as a ptr.
    if (auto* ir = std::get_if<EIndexRead>(&e.inner->kind)) {
        auto t = e.inner->type;
        if (t) {
            mlir::Value base_ptr;
            mlir::Type  elem_type;
            if (auto* vr = std::get_if<EVarRef>(&ir->receiver->kind)) {
                auto lpit = var_local_ptrs_.find(vr->name);
                if (lpit != var_local_ptrs_.end()) {
                    auto slot = get_subscript_ptr(vr->name);
                    base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot);
                    elem_type = lpit->second;
                } else if (TypeRef rt(ir->receiver->type);
                           rt && rt.kind() == LogosType::Kind::Ptr && rt.pointee()) {
                    auto cname = concrete_struct_name(rt.pointee());
                    auto sit   = struct_types_.find(cname);
                    if (sit != struct_types_.end()) {
                        auto sc = scope_.find(vr->name);
                        if (sc != scope_.end()) {
                            base_ptr  = sc->second;
                            elem_type = sit->second.llvm_type;
                        }
                    }
                } else if (TypeRef rt(ir->receiver->type);
                           rt && rt.kind() == LogosType::Kind::Array) {
                    // Stack-allocated array: get_subscript_ptr returns the alloca
                    // directly (no load needed since the alloca IS the array base).
                    auto sp = get_subscript_ptr(vr->name);
                    if (sp) {
                        base_ptr  = sp;
                        elem_type = logos_to_mlir(t);
                    }
                }
            } else if (auto* fr = std::get_if<EFieldRead>(&ir->receiver->kind)) {
                auto [struct_ptr, sname] = gen_recv_struct(*fr->receiver);
                if (struct_ptr && !sname.empty()) {
                    auto& info = struct_types_[sname];
                    auto field_ptr = gep_field(struct_ptr, info, fr->field);
                    if (field_ptr) {
                        bool field_is_ptr = ir->receiver->type &&
                                            TypeRef(ir->receiver->type).kind() == LogosType::Kind::Ptr;
                        if (field_is_ptr) {
                            base_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                loc_, ptr_type(), field_ptr);
                            TypeRef rpt = TypeRef(ir->receiver->type).pointee();
                            if (rpt &&
                                (rpt.kind() == LogosType::Kind::Struct ||
                                 rpt.kind() == LogosType::Kind::ZonedStruct)) {
                                auto cname = concrete_struct_name(rpt);
                                auto sit2  = struct_types_.find(cname);
                                if (sit2 != struct_types_.end())
                                    elem_type = sit2->second.llvm_type;
                            }
                            if (!elem_type)
                                elem_type = logos_to_mlir(t);
                        } else {
                            base_ptr  = field_ptr;
                            elem_type = logos_to_mlir(t);
                        }
                    }
                }
            }
            if (base_ptr && elem_type) {
                auto idx = gen_expr(*ir->index);
                if (!idx) return nullptr;
                bool idx_unsigned = ir->index->type &&
                    (TypeRef(ir->index->type).kind() == LogosType::Kind::U8  ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U16 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U32 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U24 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U56 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U64 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U128);
                if (idx_unsigned && idx.getType() != builder_.getI64Type())
                    idx = builder_.create<mlir::arith::ExtUIOp>(
                        loc_, builder_.getI64Type(), idx);
                llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
                return builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), elem_type, base_ptr, indices);
            }
        }
    }
    auto val = gen_expr(*e.inner);
    if (!val) return nullptr;
    auto t = e.inner->type;
    if (t && (TypeRef(t).kind() == LogosType::Kind::Tuple ||
              TypeRef(t).kind() == LogosType::Kind::Struct ||
              TypeRef(t).kind() == LogosType::Kind::ZonedStruct ||
              TypeRef(t).kind() == LogosType::Kind::Array))
        return val;  // already a pointer to the value on the stack
    // Scalar: spill to a fresh stack slot.
    auto llvm_type = logos_to_mlir(t);
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
                 TypeRef(type).kind() == LogosType::Kind::ZonedStruct))
        return ptr;
    auto pointee = logos_to_mlir(type);
    if (!pointee) pointee = builder_.getI32Type();
    return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
}

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<ECall>(_le->kind);
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // ── Compiler intrinsics recognised by name ────────────────────────────────
    // str_from_raw(ptr: *const u8, len: i64) -> str
    // Constructs a str fat-pointer {ptr, len} on the stack, mirroring ELitStr.
    if (e.callee == "str__str_from_raw" || e.callee == "str_from_raw") {
        if (e.args.size() == 2) {
            auto ptr_v = gen_expr(*e.args[0]); if (!ptr_v) return nullptr;
            auto len_v = gen_expr(*e.args[1]); if (!len_v) return nullptr;
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
    if (e.callee == "popcount_u64"        || e.callee == "leading_zeros_u64"  ||
        e.callee == "trailing_zeros_u64"  || e.callee == "bswap_u64"          ||
        e.callee == "bitreverse_u64") {
        if (e.args.size() == 1) {
            auto v = gen_expr(*e.args[0]); if (!v) return nullptr;
            auto i64_ty = builder_.getIntegerType(64);
            auto i32_ty = builder_.getIntegerType(32);
            v = coerce_int(v, i64_ty);
            mlir::Value res;
            if (e.callee == "popcount_u64")
                res = builder_.create<mlir::LLVM::CtPopOp>(loc_, i64_ty, v);
            else if (e.callee == "leading_zeros_u64")
                res = builder_.create<mlir::LLVM::CountLeadingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (e.callee == "trailing_zeros_u64")
                res = builder_.create<mlir::LLVM::CountTrailingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (e.callee == "bswap_u64")
                res = builder_.create<mlir::LLVM::ByteSwapOp>(loc_, i64_ty, v);
            else // bitreverse_u64
                res = builder_.create<mlir::LLVM::BitReverseOp>(loc_, i64_ty, v);
            // popcount/ctlz/cttz: Logos return type is u32; truncate.
            if (e.callee == "popcount_u64"       ||
                e.callee == "leading_zeros_u64"  ||
                e.callee == "trailing_zeros_u64")
                res = coerce_int(res, i32_ty);
            return res;
        }
    }

    // Check if this is a vararg extern fn (declared as llvm.func)
    if (vararg_fns_.count(e.callee)) {
        auto callee_fn = parent_mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(e.callee);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: undefined vararg function '%s'\n", e.callee.c_str());
            return nullptr;
        }
        llvm::SmallVector<mlir::Value> args;
        auto fn_type   = callee_fn.getFunctionType();
        auto fixed_inputs = fn_type.getParams();
        for (size_t i = 0; i < e.args.size(); ++i) {
            auto v = gen_expr(*e.args[i]);
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

    auto callee_fn  = find_func_op(parent_mod, e.callee);
    if (!callee_fn) {
        auto gpos = e.callee.find("__g__");
        if (gpos != std::string::npos)
            callee_fn = find_func_op(parent_mod, e.callee.substr(0, gpos));
        if (!callee_fn) {
            // Generic instantiations may be emitted without their trailing
            // `__g__...` suffix in the call site.  Fall back to the concrete
            // generic symbol with the same base prefix.
            std::string generic_prefix = e.callee + "__g__";
            parent_mod.walk([&](mlir::func::FuncOp fn) {
                auto fn_name = fn.getName().str();
                if (fn_name.rfind(generic_prefix, 0) == 0) {
                    callee_fn = fn;
                    return mlir::WalkResult::interrupt();
                }
                return mlir::WalkResult::advance();
            });
        }
    }
    if (!callee_fn) {
        llvm::SmallVector<mlir::Value> args;
        for (size_t i = 0; i < e.args.size(); ++i) {
            auto v = gen_expr(*e.args[i]);
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
            loc_, e.callee, result_types, mlir::ValueRange(args));
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    auto param_types = callee_fn.getFunctionType().getInputs();
    // Look up Logos-level param types for dyn coercion
    auto fpit = fn_param_types_.find(e.callee);
    for (size_t i = 0; i < e.args.size(); ++i) {
        mlir::Value v;
        // When the callee expects a pointer and the arg is an EFieldRead of an
        // inline-embedded struct, pass the field's GEP directly instead of
        // load+spill. This ensures mutations (e.g. &mut self.inner) write back
        // to the original struct, not a disconnected alloca copy.
        if (i < param_types.size() && param_types[i] == ptr_type()) {
            if (auto* fr = std::get_if<EFieldRead>(&e.args[i]->kind)) {
                auto [base_ptr, base_sname] = gen_recv_struct(*fr->receiver);
                if (base_ptr && !base_sname.empty()) {
                    auto bit = struct_types_.find(base_sname);
                    if (bit != struct_types_.end()) {
                        auto gep = gep_field(base_ptr, bit->second, fr->field);
                        if (gep) {
                            // Only use GEP directly when the field is an inline
                            // struct — primitives/pointers don't need this.
                            for (auto& f : bit->second.fields) {
                                if (f.name == fr->field &&
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
        v = gen_expr(*e.args[i]);
        if (!v) return nullptr;
    arg_push:
        // Coerce concrete struct/class → &dyn Trait if param expects it.
        // Box<T> is laid out as { *mut T } so the box value *is* the data pointer;
        // use T as the vtable key so the impl on T (not Box<T>) is looked up.
        if (fpit != fn_param_types_.end() && i < fpit->second.size()) {
            auto param_lt = fpit->second[i];
            auto arg_lt = e.args[i]->type;
            if (param_lt && TypeRef(param_lt).kind() == LogosType::Kind::TraitObject &&
                arg_lt && TypeRef(arg_lt).kind() != LogosType::Kind::TraitObject) {
                TypeRef vt_type = arg_lt;
                if (TypeRef(vt_type).kind() == LogosType::Kind::Struct &&
                    TypeRef(vt_type).struct_name() == "Box" &&
                    TypeRef(vt_type).type_args().size() == 1)
                    vt_type = TypeRef(vt_type).type_args()[0];
                v = coerce_to_dyn(v, TypeRef(param_lt).trait_name(), type_str(vt_type));
            }
        }
        if (i < param_types.size()) {
            // Aggregate returned by value but param expects pointer — spill to alloca.
            if (v.getType() != param_types[i] &&
                param_types[i] == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
                v = spill_to_alloca(v);
            else if (v.getType() != ptr_type())
                v = coerce_numeric(v, param_types[i], e.args[i]->type);
        }
        args.push_back(v);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EMethodCall>(_le->kind);
    if (e.method == "as_offset" && e.receiver && e.receiver->type) {
        const auto rt = e.receiver->type;
        bool is_anyval =
            type_str(rt) == "AnyVal" ||
            ((TypeRef(rt).kind() == LogosType::Kind::Ptr ||
              TypeRef(rt).kind() == LogosType::Kind::Ref ||
              TypeRef(rt).kind() == LogosType::Kind::MutRef) &&
             TypeRef(rt).pointee() && type_str(TypeRef(rt).pointee()) == "AnyVal");
        if (is_anyval) {
        auto recv = gen_expr(*e.receiver);
        if (!recv) return nullptr;
        if (recv.getType() == ptr_type())
            return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
        return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    // &tagged<TS> Trait dispatch: read type_code, GEP tier-1 table, indirect call.
    if (!e.tag_system.empty()) {
        return gen_tagged_dispatch(e, ret_logos_type);
    }
    // &dyn Trait dispatch: load vtable, GEP slot, indirect call
    if (e.receiver->type &&
        TypeRef(e.receiver->type).kind() == LogosType::Kind::TraitObject &&
        e.vtable_index >= 0) {
        return gen_dyn_dispatch(e, ret_logos_type);
    }
    auto [ptr, tname] = gen_recv_struct(*e.receiver);
    if (!ptr || tname.empty()) return nullptr;
    if (tname == "AnyVal" && ptr.getType() != ptr_type()) {
        auto slot = create_entry_alloca(builder_.getI32Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_numeric(ptr, builder_.getI32Type()), slot);
        ptr = slot;
    }
    // Direct call:
    // 1) prefer sema-resolved concrete symbol (overload-safe),
    // 2) fallback to legacy TypeName__method lookup.
    // If resolved_type is set (inherited method), use the defining class name.
    const std::string& defining = e.resolved_type.empty() ? tname : e.resolved_type;
    auto mangled    = defining + "__" + e.method;
    auto callee_name = e.resolved_symbol.empty() ? mangled : e.resolved_symbol;
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto callee_fn  = find_func_op(parent_mod, callee_name);
    if (!callee_fn) {
        // Generic struct methods may retain a trailing generic suffix in the
        // instantiated symbol name, e.g. `Box$G1$i32__unwrap__g__Box$G1$T`.
        // Fall back to the first function whose name starts with the concrete
        // direct-call prefix.
        std::string generic_prefix = callee_name + "__g__";
        parent_mod.walk([&](mlir::func::FuncOp fn) {
            auto fn_name = fn.getName().str();
            if (fn_name.rfind(generic_prefix, 0) == 0) {
                callee_fn = fn;
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
    }
    // If sema provided a resolved symbol and it wasn't found (e.g. mono renamed),
    // try legacy receiver-based lookup as a final compatibility fallback.
    if (!callee_fn && !e.resolved_symbol.empty()) {
        callee_name = mangled;
        callee_fn = find_func_op(parent_mod, callee_name);
        if (!callee_fn) {
            std::string generic_prefix = callee_name + "__g__";
            parent_mod.walk([&](mlir::func::FuncOp fn) {
                auto fn_name = fn.getName().str();
                if (fn_name.rfind(generic_prefix, 0) == 0) {
                    callee_fn = fn;
                    return mlir::WalkResult::interrupt();
                }
                return mlir::WalkResult::advance();
            });
        }
    }
    if (!callee_fn) {
        std::fprintf(stderr, "mlir_gen: method '%s' not found\n", callee_name.c_str());
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    args.push_back(ptr);
    auto param_types = callee_fn.getFunctionType().getInputs();
    for (size_t i = 0; i < e.args.size(); ++i) {
        auto v = gen_expr(*e.args[i]);
        if (!v) return nullptr;
        size_t pi = i + 1;
        if (pi < param_types.size()) {
            if (v.getType() != param_types[pi] &&
                param_types[pi] == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
                v = spill_to_alloca(v);
            else
                v = coerce_numeric(v, param_types[pi], e.args[i]->type);
        }
        args.push_back(v);
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
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EIndexRead>(_le->kind);
    // Receiver: try to get the alloca pointer directly for VAR_REF.
    mlir::Value arr_ptr;
    mlir::Type  elem_type;

    if (auto* vr = std::get_if<EVarRef>(&e.receiver->kind)) {
        // Local pointer variable: scope_ holds alloca(ptr), load actual ptr first.
        auto lpit = var_local_ptrs_.find(vr->name);
        if (lpit != var_local_ptrs_.end()) {
            auto alloca = get_subscript_ptr(vr->name);
            arr_ptr   = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), alloca);
            elem_type = lpit->second;
        } else if (TypeRef rt(e.receiver->type);
                   rt && rt.kind() == LogosType::Kind::Ptr &&
                   rt.pointee() &&
                   (rt.pointee().kind() == LogosType::Kind::Struct ||
                    rt.pointee().kind() == LogosType::Kind::ZonedStruct)) {
            auto cname = concrete_struct_name(rt.pointee());
            auto sit   = struct_types_.find(cname);
            if (sit != struct_types_.end()) {
                auto sc = scope_.find(vr->name);
                if (sc != scope_.end()) {
                    arr_ptr   = sc->second;
                    elem_type = sit->second.llvm_type;
                }
            }
            if (!arr_ptr) {
                arr_ptr   = get_subscript_ptr(vr->name);
                elem_type = subscript_elem_type(vr->name);
            }
        } else {
            arr_ptr   = get_subscript_ptr(vr->name);
            elem_type = subscript_elem_type(vr->name);
        }
    } else if (auto* ir = std::get_if<EIndexRead>(&e.receiver->kind)) {
        // Nested index: matrix[i][j] — get a pointer to matrix[i] without loading it.
        mlir::Value inner_ptr;
        mlir::Type  inner_elem_type;
        if (auto* vr2 = std::get_if<EVarRef>(&ir->receiver->kind)) {
            inner_ptr       = get_subscript_ptr(vr2->name);
            inner_elem_type = subscript_elem_type(vr2->name);
        } else {
            inner_ptr       = gen_expr(*ir->receiver);
            inner_elem_type = inner_ptr ? logos_to_mlir(ir->receiver->type) : nullptr;
        }
        if (inner_ptr && inner_elem_type) {
            auto i_idx = gen_expr(*ir->index);
            if (i_idx) {
                bool i_unsigned = ir->index->type &&
                    (TypeRef(ir->index->type).kind() == LogosType::Kind::U8  ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U16 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U32 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U24 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U56 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U64 ||
                     TypeRef(ir->index->type).kind() == LogosType::Kind::U128);
                if (i_unsigned && i_idx.getType() != builder_.getI64Type())
                    i_idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), i_idx);
                llvm::SmallVector<mlir::LLVM::GEPArg> inner_indices{i_idx};
                arr_ptr   = builder_.create<mlir::LLVM::GEPOp>(
                                loc_, ptr_type(), inner_elem_type, inner_ptr, inner_indices);
                elem_type = logos_to_mlir(type);
                if (!elem_type) elem_type = builder_.getI32Type();
            }
        }
    } else if (auto* fr = std::get_if<EFieldRead>(&e.receiver->kind)) {
        // Field index read: field may be an array or a pointer.
        auto [struct_ptr, sname] = gen_recv_struct(*fr->receiver);
        if (struct_ptr && !sname.empty()) {
            auto& info = struct_types_[sname];
            auto field_ptr = gep_field(struct_ptr, info, fr->field);
            if (field_ptr) {
                elem_type = logos_to_mlir(type);
                if (!elem_type) elem_type = builder_.getI32Type();
                bool field_is_ptr = e.receiver->type &&
                                    TypeRef(e.receiver->type).kind() == LogosType::Kind::Ptr;
                if (field_is_ptr) {
                    arr_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_ptr);
                    // Use struct LLVM type for stride when pointee is a struct/datatype.
                    TypeRef rpt = TypeRef(e.receiver->type).pointee();
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
        if (!arr_ptr) {
            arr_ptr   = gen_expr(*e.receiver);
            elem_type = logos_to_mlir(type);
            if (!elem_type) elem_type = builder_.getI32Type();
        }
    } else {
        arr_ptr   = gen_expr(*e.receiver);
        elem_type = logos_to_mlir(type);
        if (!elem_type) elem_type = builder_.getI32Type();
    }

    auto idx = gen_expr(*e.index);
    if (!idx || !arr_ptr) return nullptr;
    // Zero-extend unsigned index types so u8(200) doesn't become i8(-56) in GEP.
    bool idx_unsigned = e.index->type &&
        (TypeRef(e.index->type).kind() == LogosType::Kind::U8  ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U16 ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U32 ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U24 ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U56 ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U64 ||
         TypeRef(e.index->type).kind() == LogosType::Kind::U128);
    if (idx_unsigned && idx.getType() != builder_.getI64Type())
        idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
    llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, arr_ptr, indices);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, gep);
}

// ---------------------------------------------------------------------------
// Struct / array / tuple literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EStructLitView v, TypeRef) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EStructLit>(_le->kind);
    return gen_struct_lit(e);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EArrLitView v, TypeRef type) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EArrLit>(_le->kind);
    mlir::Type elem_type = builder_.getI32Type();
    if (type && TypeRef(type).elem()) {
        auto et = logos_to_mlir(TypeRef(type).elem());
        if (et) elem_type = et;
    }
    return gen_arr_lit(e, elem_type);
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
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<ECast>(_le->kind);
    // ── Hermes typed container cast: &[T] as <I32>[] → Hermes. ──────────
    if (!e.hermes_build_fn.empty()) {
        auto val = gen_expr(*e.operand);
        if (!val) return nullptr;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto build_fn = find_func_op(parent_mod, e.hermes_build_fn);
        if (!build_fn) {
            std::fprintf(stderr, "mlir_gen: '%s' not found — add 'use std.hermes.ctr;'\n",
                         e.hermes_build_fn.c_str());
            return nullptr;
        }
        // fix3: dispatch by function name prefix, not arg count — getNumArguments() is fragile
        // (any future 3-arg array builder would silently take the wrong path).
        if (e.hermes_build_fn.rfind("hermes_build_map_", 0) == 0) {
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

    auto val    = gen_expr(*e.operand);
    if (!val) return nullptr;

    // str (Slice<u8> = fat pointer {ptr, i64}) as *const u8 → extract field 0.
    // Must be checked BEFORE the val.getType() == target early-return because
    // both the alloca ptr (fat struct) and *const u8 are !llvm.ptr in LLVM 17.
    if (TypeRef ot(e.operand->type);
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
    if (!target || val.getType() == target) return val;

    auto fi = mlir::dyn_cast<mlir::IntegerType>(val.getType());
    auto ti = mlir::dyn_cast<mlir::IntegerType>(target);
    if (fi && ti) {
        if (ti.getWidth() > fi.getWidth()) {
            bool src_unsigned = fi.getWidth() == 1 ||
                (e.operand->type &&
                 (TypeRef(e.operand->type).kind() == LogosType::Kind::U8  ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U16 ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U32 ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U24 ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U56 ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U64 ||
                  TypeRef(e.operand->type).kind() == LogosType::Kind::U128));
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
            (e.operand->type &&
             (TypeRef(e.operand->type).kind() == LogosType::Kind::U8  ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U16 ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U32 ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U24 ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U56 ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U64 ||
              TypeRef(e.operand->type).kind() == LogosType::Kind::U128));
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
        bool src_unsigned = e.operand->type &&
            (TypeRef(e.operand->type).kind() == LogosType::Kind::U8  ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U16 ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U32 ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U24 ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U56 ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U64 ||
             TypeRef(e.operand->type).kind() == LogosType::Kind::U128);
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

    mlir::Type result_type = logos_to_mlir(type);
    if (!result_type) return nullptr;

    // Allocate result slot in the current (entry-reachable) block.
    auto result_alloca = create_entry_alloca(result_type);

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
    if (!then_val) then_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    then_val = coerce_numeric(then_val, result_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, then_val, result_alloca);
    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(else_block);
    auto else_val = gen_expr(*else_l);
    if (!else_val) else_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    else_val = coerce_numeric(else_val, result_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, else_val, result_alloca);
    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(merge_block);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMatchExprView v, TypeRef type) {
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EMatchExpr>(_le->kind);
    mlir::Type result_type = logos_to_mlir(type);
    if (!result_type) return nullptr;

    // Allocate result slot before the match (entry-block reachable).
    auto result_alloca = create_entry_alloca(result_type);

    auto* region      = builder_.getBlock()->getParent();
    auto* merge_block = new mlir::Block();

    auto scrut = gen_expr(*e.scrut);
    if (!scrut) {
        region->push_back(merge_block);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
    }

    // Detect tagged enum: load discriminant.
    mlir::Value scrut_ptr = nullptr;
    const TaggedEnumInfo* te_info = nullptr;
    if (TypeRef st(e.scrut->type); st && st.kind() == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(std::string(st.enum_name()), e.scrut->type);
        if (te_info) {
            // GEP requires a pointer operand.  If the scrutinee is a by-value
            // struct (e.g. a direct function call result), spill it to an alloca.
            if (scrut.getType() != ptr_type()) {
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
    mlir::Type scrut_type = scrut.getType();

    // Extract payload bindings for a PatVariantData arm into scope.
    auto extract_arm_payload = [&](const EMatchArm& arm) -> std::vector<std::string> {
        std::vector<std::string> added;
        if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) {
            if (te_info && scrut_ptr) {
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& v : te_info->variants)
                    if (v.disc == pvd->disc) { vp = &v; break; }
                if (vp) {
                    llvm::SmallVector<mlir::Type> ft;
                    for (auto& t : vp->field_types) ft.push_back(t);
                    auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                        builder_.getContext(), ft);
                    for (size_t bi = 0; bi < pvd->bindings.size() &&
                                         bi < vp->field_types.size(); ++bi) {
                        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                        auto fp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), pay_struct, pay_ptr, fi);
                        // For inline structs (logos_to_mlir returns ptr but struct is stored
                        // inline in the payload), fp already points to the struct bytes —
                        // use fp directly as the variable (no extra load).
                        TypeRef lt = bi < vp->logos_types.size()
                                              ? vp->logos_types[bi] : nullptr;
                        bool is_inline_struct = lt &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                             TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                             TypeRef(lt).kind() == LogosType::Kind::Slice ||
                             TypeRef(lt).kind() == LogosType::Kind::Closure);
                        if (is_inline_struct &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct)) {
                            // Bind the payload bytes directly: scope_[name]=fp
                            // points at the inline struct, matching the normal
                            // struct-var convention (scope_ holds a pointer
                            // *to* the struct bytes, not a pointer-to-pointer).
                            // Without this, SDrop would call T__drop(alloca)
                            // where alloca is an 8-byte slot holding fp, so
                            // T__drop would read garbage stack as field[1..]
                            // and free an out-of-range pointer.
                            scope_[pvd->bindings[bi]] = fp;
                            let_vars_.insert(pvd->bindings[bi]);
                            var_struct_[pvd->bindings[bi]] = concrete_struct_name(lt);
                            added.push_back(pvd->bindings[bi]);
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
                            scope_[pvd->bindings[bi]] = alloca;
                            let_vars_.insert(pvd->bindings[bi]);
                            var_elem_types_[pvd->bindings[bi]] = vp->field_types[bi];
                            added.push_back(pvd->bindings[bi]);
                        }
                    }
                }
            }
        } else if (auto* pw = std::get_if<PatWild>(&arm.pat)) {
            if (pw->name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[pw->name] = alloca;
                let_vars_.insert(pw->name);
                var_elem_types_[pw->name] = sv.getType();
                added.push_back(pw->name);
            }
        }
        return added;
    };

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    if (e.scrut->type && TypeRef(e.scrut->type).kind() == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (auto& arm : e.arms) {
            if (arm.guard) continue;
            if (std::holds_alternative<PatWild>(arm.pat)) { has_wild = true; break; }
            auto check_bool = [&](const lir::Pattern& p) {
                if (auto* pb = std::get_if<lir::PatBool>(&p)) {
                    if (pb->value) has_true = true; else has_false = true;
                }
            };
            if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
                for (auto& alt : por->alts) check_bool(alt);
            } else {
                check_bool(arm.pat);
            }
        }
        exhaustive_discrete = has_wild || (has_true && has_false);
    } else if (e.scrut->type && TypeRef(e.scrut->type).kind() == LogosType::Kind::Enum) {
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](const lir::Pattern& p) {
            if (auto* pv  = std::get_if<lir::PatVariant>(&p))     covered.insert(pv->disc);
            else if (auto* pvd = std::get_if<lir::PatVariantData>(&p)) covered.insert(pvd->disc);
        };
        for (auto& arm : e.arms) {
            if (arm.guard) continue;
            if (std::holds_alternative<PatWild>(arm.pat)) { has_wild = true; break; }
            if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
                for (auto& alt : por->alts) cover_enum(alt);
            } else {
                cover_enum(arm.pat);
            }
        }
        if (has_wild) {
            exhaustive_discrete = true;
        } else {
            std::string en(TypeRef(e.scrut->type).enum_name());
            auto eit = enum_types_.find(en);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(en, e.scrut->type)) {
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
    for (int i = (int)e.arms.size() - 1; i >= 0; --i) {
        auto& arm = e.arms[i];
        auto* body_block = new mlir::Block();
        region->push_back(body_block);

        mlir::Block* arm_entry = body_block;

        if (arm.guard) {
            // guard_block: extract bindings, evaluate guard, branch to body_block or else_block.
            auto* guard_block = new mlir::Block();
            region->push_back(guard_block);
            std::vector<std::string> guard_added;
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(guard_block);
                guard_added = extract_arm_payload(arm);
                auto gval = gen_expr(**arm.guard);
                gval = coerce_int(gval, builder_.getI1Type());
                builder_.create<mlir::cf::CondBranchOp>(loc_, gval, body_block, else_block);
            }
            arm_entry = guard_block;
            // body_block: bindings already in scope from guard_block; generate arm value.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                auto val = gen_expr(*arm.value);
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
            auto added = extract_arm_payload(arm);
            auto val = gen_expr(*arm.value);
            for (auto& n : added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
            if (!is_terminated(builder_.getBlock())) {
                if (val) {
                    val = coerce_numeric(val, result_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                }
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        }

        bool is_wild = std::holds_alternative<PatWild>(arm.pat);
        if (is_wild) {
            else_block = arm_entry;
        } else if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
            auto get_disc = [](const lir::Pattern& p) -> int64_t {
                if (auto* pv  = std::get_if<lir::PatVariant>(&p))     return pv->disc;
                if (auto* pvd = std::get_if<lir::PatVariantData>(&p)) return pvd->disc;
                if (auto* pi  = std::get_if<lir::PatInt>(&p))         return pi->value;
                if (auto* pb  = std::get_if<lir::PatBool>(&p))        return pb->value ? 1 : 0;
                return 0;
            };
            mlir::Block* cur_else = else_block;
            for (int64_t ai = static_cast<int64_t>(por->alts.size()) - 1; ai >= 0; --ai) {
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                int64_t disc = get_disc(por->alts[static_cast<size_t>(ai)]);
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
        } else {
            int64_t disc = 0;
            if (auto* pv = std::get_if<PatVariant>(&arm.pat)) disc = pv->disc;
            else if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) disc = pvd->disc;
            else if (auto* pi = std::get_if<PatInt>(&arm.pat))  disc = pi->value;
            else if (auto* pb = std::get_if<PatBool>(&arm.pat)) disc = pb->value ? 1 : 0;

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
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& box = std::get<EClosureBox>(_le->kind);
    if (!box.inner) return nullptr;
    return gen_closure(*box.inner, type);
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
    if (auto br = v.block(); br) {
        if (auto* blk = lblock_of(br)) gen_block(*blk);
    }
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
static uint32_t build_hermes_val(const lir::HermesVal& v,
                                 logos::hermes::Hermes& doc);

static uint32_t ptr_anyval_raw(const void* obj, logos::hermes::Hermes& doc) {
    const uint8_t* base = HermesAccess::base(doc);
    uint32_t off = static_cast<uint32_t>(
        static_cast<const uint8_t*>(obj) - base);
    return AnyVal::from_offset(arena_offset_t(off)).raw();
}

static uint32_t build_object_array(const lir::HVArray& arr,
                                   logos::hermes::Hermes& doc) {
    uint64_t n = arr.elements.size();
    auto* a = ObjectArray::create(HermesAccess::arena(doc),
                                  n ? n : uint64_t{4}).get();
    uint32_t a_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - HermesAccess::base(doc));
    for (auto& ep : arr.elements) {
        uint32_t elem_raw = build_hermes_val(*ep, doc);
        auto* cur = reinterpret_cast<ObjectArray*>(
            HermesAccess::base(doc) + a_off);
        cur->push_back(AnyVal::from_raw(elem_raw),
                       HermesAccess::arena(doc)).get();
    }
    // Re-fetch pointer at end (arena may have grown).
    return AnyVal::from_offset(arena_offset_t(a_off)).raw();
}

template <typename T>
static uint32_t build_typed_array_scalar(const lir::HVArray& arr,
                                         logos::hermes::Hermes& doc) {
    uint64_t n = arr.elements.size();
    auto* a = TypedArray<T>::create(HermesAccess::arena(doc),
                                    n ? n : uint64_t{4}).get();
    uint32_t a_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - HermesAccess::base(doc));
    for (auto& ep : arr.elements) {
        T val = 0;
        if (ep) {
            std::visit([&](const auto& k) {
                if constexpr (std::is_same_v<std::decay_t<decltype(k)>,
                                             lir::HVInt>) {
                    val = static_cast<T>(k.value);
                }
            }, ep->kind);
        }
        auto* cur = reinterpret_cast<TypedArray<T>*>(
            HermesAccess::base(doc) + a_off);
        cur->push_back(val, HermesAccess::arena(doc)).get();
    }
    return AnyVal::from_offset(arena_offset_t(a_off)).raw();
}

static uint32_t build_array(const lir::HVArray& arr,
                            logos::hermes::Hermes& doc) {
    if (arr.elem_type == "I8")  return build_typed_array_scalar<int8_t>(arr, doc);
    if (arr.elem_type == "U8")  return build_typed_array_scalar<uint8_t>(arr, doc);
    if (arr.elem_type == "I16") return build_typed_array_scalar<int16_t>(arr, doc);
    if (arr.elem_type == "U16") return build_typed_array_scalar<uint16_t>(arr, doc);
    if (arr.elem_type == "I32") return build_typed_array_scalar<int32_t>(arr, doc);
    if (arr.elem_type == "U32") return build_typed_array_scalar<uint32_t>(arr, doc);
    if (arr.elem_type == "I64") return build_typed_array_scalar<int64_t>(arr, doc);
    if (arr.elem_type == "U64") return build_typed_array_scalar<uint64_t>(arr, doc);
    if (arr.elem_type == "F32") return build_typed_array_scalar<float>(arr, doc);
    if (arr.elem_type == "F64") return build_typed_array_scalar<double>(arr, doc);
    return build_object_array(arr, doc);
}

static uint32_t build_object_map(const lir::HVMap& map,
                                 logos::hermes::Hermes& doc) {
    // Pre-size so the load factor doesn't force a rehash mid-build.
    uint32_t count = static_cast<uint32_t>(map.entries.size());
    uint32_t cap = 8;
    while (cap < count * 2 || cap < 8) cap <<= 1;
    auto* m = ObjectMap::create(HermesAccess::arena(doc), cap).get();
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
    for (auto& e : map.entries) {
        std::string key_str;
        if (std::holds_alternative<std::string>(e.key))
            key_str = std::get<std::string>(e.key);
        else
            key_str = std::to_string(std::get<int64_t>(e.key));
        uint32_t val_raw = build_hermes_val(*e.val, doc);
        auto* cur = reinterpret_cast<ObjectMap*>(
            HermesAccess::base(doc) + m_off);
        cur->put(key_str, AnyVal::from_raw(val_raw),
                 HermesAccess::arena(doc)).get();
    }
    return AnyVal::from_offset(arena_offset_t(m_off)).raw();
}

template <typename Map, typename K>
static uint32_t build_typed_map_anyval(const lir::HVMap& map,
                                       logos::hermes::Hermes& doc) {
    // TypedMap::put silently drops on overflow — pre-size to entry count
    // (minimum 1, since create(arena, 0) skips buffer allocation).
    uint32_t count = static_cast<uint32_t>(map.entries.size());
    uint32_t cap = count == 0 ? 1 : count;
    auto* m = Map::create(HermesAccess::arena(doc), cap).get();
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
    for (auto& e : map.entries) {
        K key = 0;
        if (auto* iv = std::get_if<int64_t>(&e.key))
            key = static_cast<K>(*iv);
        uint32_t val_raw = build_hermes_val(*e.val, doc);
        auto* cur = reinterpret_cast<Map*>(
            HermesAccess::base(doc) + m_off);
        cur->put(key, AnyVal::from_raw(val_raw), HermesAccess::base(doc));
    }
    return AnyVal::from_offset(arena_offset_t(m_off)).raw();
}

static uint32_t build_map(const lir::HVMap& map,
                          logos::hermes::Hermes& doc) {
    if (map.key_type == "I32") return build_typed_map_anyval<MapI32AnyVal, int32_t>(map, doc);
    if (map.key_type == "U32") return build_typed_map_anyval<MapU32AnyVal, uint32_t>(map, doc);
    if (map.key_type == "I64") return build_typed_map_anyval<MapI64AnyVal, int64_t>(map, doc);
    if (map.key_type == "U64") return build_typed_map_anyval<MapU64AnyVal, uint64_t>(map, doc);
    return build_object_map(map, doc);
}

static uint32_t build_hermes_val(const lir::HermesVal& v,
                                 logos::hermes::Hermes& doc) {
    return std::visit([&](auto& k) -> uint32_t {
        using T = std::decay_t<decltype(k)>;
        if constexpr (std::is_same_v<T, lir::HVNull>) {
            return 0;
        } else if constexpr (std::is_same_v<T, lir::HVBool>) {
            // Boolean: type_hash=37 (see any_val.hpp).
            return AnyVal::from_value<uint8_t>(k.value ? 1 : 0, 37).raw();
        } else if constexpr (std::is_same_v<T, lir::HVInt>) {
            // Prefer inline i24 when it fits; otherwise allocate i64 in arena.
            if (k.value >= -8388608LL && k.value <= 8388607LL) {
                return AnyVal::from_value<int32_t>(
                    static_cast<int32_t>(k.value)).raw();
            }
            AnyVal av = anyval_put<int64_t>(HermesAccess::arena(doc),
                                            k.value).get();
            return av.raw();
        } else if constexpr (std::is_same_v<T, lir::HVFloat>) {
            AnyVal av = anyval_put<double>(HermesAccess::arena(doc),
                                           k.value).get();
            return av.raw();
        } else if constexpr (std::is_same_v<T, lir::HVStr>) {
            auto* s = ArenaString::create(HermesAccess::arena(doc),
                                          k.value).get();
            return ptr_anyval_raw(s, doc);
        } else if constexpr (std::is_same_v<T, lir::HVArray>) {
            return build_array(k, doc);
        } else if constexpr (std::is_same_v<T, lir::HVMap>) {
            return build_map(k, doc);
        } else if constexpr (std::is_same_v<T, lir::HVCapture>) {
            // Inline PARAM (tc=127): raw = (value_index << 8) | 0xFF.
            // clone() records the dst-arena slot via out_params when the
            // container writes this raw into its slot.
            return (k.value_index << 8u) | 0xFFu;
        } else {
            return 0;
        }
    }, v.kind);
}

// Build the full zone blob for an EHermesLit node.
// Steps:
//   1. Make a fresh doc (DocumentHeader at offset 0).
//   2. Build the root value tree.
//   3. Write root AnyVal.raw into DocumentHeader (works for inline + ptr
//      alike — AnyVal bit0 disambiguates on read; see Task 1 in clone.cpp).
//   4. clone() → packed arena + PARAM slot list.
//   5. Extract bytes from packed head() chunk.
static HermesZoneBuild build_hermes_zone(const lir::EHermesLit& e) {
    auto doc = make_doc().get();
    uint32_t root_raw = build_hermes_val(*e.root, doc);
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
    auto* _le = lexpr_of(v.self); if (!_le) return nullptr;
    auto& e = std::get<EHermesLit>(_le->kind);
    auto [blob, param_slots] = build_hermes_zone(e);

    auto lit_idx    = hermes_lit_counter_++;
    auto global_name = "__hermes_lit_" + std::to_string(lit_idx);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    // C8e: static @-literals get an 8-byte little-endian size prefix in rodata
    // so that HermesStatic::size() can read *(ptr - 8) without a separate symbol.
    // Capture path still uses the raw blob (no prefix) because the blob is
    // memcpy'd into a live Hermes at runtime.
    auto i8 = builder_.getIntegerType(8);
    if (!e.has_captures) {
        // Emit [u64 size_le][blob bytes...] as one rodata global.
        auto size_le = static_cast<uint64_t>(blob.size());
        std::vector<uint8_t> prefixed(8);
        for (int k = 0; k < 8; ++k)
            prefixed[k] = static_cast<uint8_t>((size_le >> (k * 8)) & 0xFF);
        prefixed.insert(prefixed.end(), blob.begin(), blob.end());

        auto arr_type  = mlir::LLVM::LLVMArrayType::get(i8, prefixed.size());
        auto blob_attr = builder_.getStringAttr(
            llvm::StringRef(reinterpret_cast<const char*>(prefixed.data()), prefixed.size()));
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            global_name, blob_attr);

        builder_.restoreInsertionPoint(save_pt);

        // ptr = address_of(global) + 8  (points past the size prefix, to the blob).
        auto global_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
        mlir::Value offset8 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
        auto blob_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i8, global_ptr, mlir::ValueRange{offset8});

        // Return HermesStatic { ptr: blob_ptr } as an alloca (DataPlain struct).
        auto sit = struct_types_.find("HermesStatic");
        if (sit == struct_types_.end()) {
            // Fallback: HermesStatic not yet registered (shouldn't happen in normal builds).
            return blob_ptr;
        }
        auto alloca = create_entry_alloca(sit->second.llvm_type);
        auto gep = gep_field(alloca, sit->second, "ptr");
        if (!gep) return blob_ptr;
        builder_.create<mlir::LLVM::StoreOp>(loc_, blob_ptr, gep);
        return alloca;
    }

    // Capture path: emit plain blob (no size prefix).
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, blob.size());
    auto blob_attr = builder_.getStringAttr(
        llvm::StringRef(reinterpret_cast<const char*>(blob.data()), blob.size()));
    builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
        global_name, blob_attr);

    // ── Capture path ─────────────────────────────────────────────────────────
    // Emit slots table: array of u32 pairs [blob_off, value_idx, ...].
    auto slots_name = "__hermes_slots_" + std::to_string(lit_idx);
    size_t n_slots  = param_slots.size();
    size_t n_values = e.capture_exprs.size();

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
    for (auto ct : e.capture_types) {
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
        for (auto ct : e.capture_types) {
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
            mlir::Value cap_val = gen_expr(*e.capture_exprs[i]);
            if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

            TypeRef ct = e.capture_types[i];
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
        mlir::Value cap_val = gen_expr(*e.capture_exprs[i]);
        if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

        mlir::Value raw_u32 = coerce_to_anyval_raw(cap_val, e.capture_types[i]);
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
