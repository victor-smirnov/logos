// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_expr.cpp — Expression code generation.

#include "mlir_gen_impl.hpp"

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
    return std::visit([&](auto& k) { return gen_expr_kind(k, e.type); }, e.kind);
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const ELitInt& e, const LogosType* type) {
    int width = 32;
    if (type) {
        switch (type->kind) {
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
            if (e.value > INT32_MAX || e.value < INT32_MIN) width = 64;
            break;
        default: break;
        }
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, e.value, width);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ELitFloat& e, const LogosType* type) {
    bool is_f32 = type && type->kind == LogosType::Kind::F32;
    if (is_f32) {
        auto f32 = builder_.getF32Type();
        return builder_.create<mlir::arith::ConstantFloatOp>(
            loc_, f32, llvm::APFloat(float(e.value)));
    }
    auto f64 = builder_.getF64Type();
    return builder_.create<mlir::arith::ConstantFloatOp>(
        loc_, f64, llvm::APFloat(e.value));
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ELitBool& e, const LogosType*) {
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, e.value ? 1 : 0, 1);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ELitStr& e, const LogosType*) {
    std::string raw = e.value;
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
    text.push_back('\0');

    auto global_name = ".str." + std::to_string(str_counter_++);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    auto i8       = builder_.getIntegerType(8);
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text.size());
    auto str_attr = builder_.getStringAttr(llvm::StringRef(text.data(), text.size()));
    builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, true, mlir::LLVM::Linkage::Internal, global_name, str_attr);

    builder_.restoreInsertionPoint(save_pt);
    return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
}

// ---------------------------------------------------------------------------
// Variable reference
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EVarRef& e, const LogosType* type) {
    // Module constant: re-evaluate inline.
    auto cit = module_consts_.find(e.name);
    if (cit != module_consts_.end())
        return gen_expr(*cit->second->value);

    auto it = scope_.find(e.name);
    if (it == scope_.end()) {
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        // Check if name is a free function being used as a bare fn-ptr.
        if (type && type->kind == LogosType::Kind::FnPtr) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(e.name);
            if (fn_sym) {
                // Return just the function address as a raw ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), e.name);
                return builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
            }
        }
        // Check if name is a free function being used as a value (closure fat pointer).
        // Create a non-capturing closure: {fn_ptr, null_env}.
        if (type && type->kind == LogosType::Kind::Closure) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(e.name);
            if (fn_sym) {
                // Build closure fat pointer: { fn_ptr, env_ptr=null }
                auto closure_struct_t = mlir::LLVM::LLVMStructType::getLiteral(
                    builder_.getContext(), {ptr_type(), ptr_type()});
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), closure_struct_t, i64_one());
                // Store the function address as fn_ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), e.name);
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
        std::fprintf(stderr, "mlir_gen: undefined '%s'\n", e.name.c_str());
        return nullptr;
    }
    // Mutable tagged enum: load struct ptr from pointer slot.
    if (var_tagged_enum_ptr_.count(e.name))
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
    // Struct/class/array/tuple/tagged-enum/dyn-trait variables: return pointer directly.
    if (var_struct_.count(e.name) || var_class_.count(e.name))
        return get_struct_ptr(e.name);
    if (var_subscript_.count(e.name) || var_tuple_.count(e.name) ||
        var_tagged_enum_.count(e.name) || var_dyn_trait_.count(e.name))
        return it->second;
    // Let-bound scalar: load from alloca.
    if (let_vars_.count(e.name)) {
        auto et = var_elem_types_.find(e.name);
        if (et == var_elem_types_.end()) return nullptr;
        return builder_.create<mlir::LLVM::LoadOp>(loc_, et->second, it->second);
    }
    // Parameter SSA value.
    return it->second;
}

// ---------------------------------------------------------------------------
// Enum literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EEnumLit& e, const LogosType* type) {
    // Tagged enum without payload (e.g. Option::None): alloca + store disc
    auto* te = resolve_tagged_enum(e.enum_name, type);
    if (te) {
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), te->llvm_type, i64_one());
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(0)};
        auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, alloca, idx);
        auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, e.disc, 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, disc_val, disc_ptr);
        return alloca;
    }
    // C-style enum: just the discriminant
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, e.disc, 32);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EEnumLitData& e, const LogosType* type) {
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
                val = coerce_int(val, vp->field_types[i]);
                llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(i)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), pay_struct, pay_ptr, fi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, fp);
            }
        }
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Binary / Unary operators
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EBinOp& e, const LogosType*) {
    auto lhs = gen_expr(*e.lhs);
    if (!lhs) return nullptr;

    // Short-circuit operators: evaluate RHS only when LHS doesn't determine the result.
    // && : if LHS is false, result is false (skip RHS)
    // || : if LHS is true,  result is true  (skip RHS)
    if (e.op == "&&" || e.op == "||") {
        auto i1 = builder_.getI1Type();
        auto result_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), i1, i64_one());

        auto* region      = builder_.getBlock()->getParent();
        auto* rhs_block   = new mlir::Block();
        auto* sc_block    = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(rhs_block);
        region->push_back(sc_block);
        region->push_back(merge_block);

        // && : evaluate RHS when LHS=true; short-circuit to false when LHS=false
        // || : evaluate RHS when LHS=false; short-circuit to true  when LHS=true
        if (e.op == "&&")
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, rhs_block, sc_block);
        else
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, sc_block, rhs_block);

        // Short-circuit block: store the known result without evaluating RHS.
        builder_.setInsertionPointToStart(sc_block);
        auto sc_val = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, (e.op == "||") ? 1 : 0, 1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, sc_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // RHS block: evaluate RHS, store its value.
        builder_.setInsertionPointToStart(rhs_block);
        auto rhs_val = gen_expr(*e.rhs);
        if (!rhs_val)
            rhs_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, rhs_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, i1, result_alloca);
    }

    auto rhs = gen_expr(*e.rhs);
    if (!rhs) return nullptr;
    // Widen narrower integer operand, using zero-extend for unsigned types.
    if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
        if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
            if (li.getWidth() < ri.getWidth()) {
                bool lhs_unsigned = e.lhs->type &&
                    (e.lhs->type->kind == LogosType::Kind::U8   ||
                     e.lhs->type->kind == LogosType::Kind::U16  ||
                     e.lhs->type->kind == LogosType::Kind::U32  ||
                     e.lhs->type->kind == LogosType::Kind::U24  ||
                     e.lhs->type->kind == LogosType::Kind::U56  ||
                     e.lhs->type->kind == LogosType::Kind::U64  ||
                     e.lhs->type->kind == LogosType::Kind::U128 ||
                     e.lhs->type->kind == LogosType::Kind::Bool);
                if (lhs_unsigned)
                    lhs = builder_.create<mlir::arith::ExtUIOp>(loc_, rhs.getType(), lhs);
                else
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
            } else if (ri.getWidth() < li.getWidth()) {
                bool rhs_unsigned = e.rhs->type &&
                    (e.rhs->type->kind == LogosType::Kind::U8   ||
                     e.rhs->type->kind == LogosType::Kind::U16  ||
                     e.rhs->type->kind == LogosType::Kind::U32  ||
                     e.rhs->type->kind == LogosType::Kind::U24  ||
                     e.rhs->type->kind == LogosType::Kind::U56  ||
                     e.rhs->type->kind == LogosType::Kind::U64  ||
                     e.rhs->type->kind == LogosType::Kind::U128 ||
                     e.rhs->type->kind == LogosType::Kind::Bool);
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
        bool rhs_unsigned = e.rhs->type &&
            (e.rhs->type->kind == LogosType::Kind::U8  ||
             e.rhs->type->kind == LogosType::Kind::U16 ||
             e.rhs->type->kind == LogosType::Kind::U32 ||
             e.rhs->type->kind == LogosType::Kind::U24 ||
             e.rhs->type->kind == LogosType::Kind::U56 ||
             e.rhs->type->kind == LogosType::Kind::U64 ||
             e.rhs->type->kind == LogosType::Kind::U128);
        if (rhs_unsigned)
            rhs = builder_.create<mlir::arith::UIToFPOp>(loc_, lhs.getType(), rhs);
        else
            rhs = builder_.create<mlir::arith::SIToFPOp>(loc_, lhs.getType(), rhs);
    }
    if (mlir::isa<mlir::IntegerType>(lhs.getType()) &&
        mlir::isa<mlir::FloatType>(rhs.getType())) {
        bool lhs_unsigned = e.lhs->type &&
            (e.lhs->type->kind == LogosType::Kind::U8  ||
             e.lhs->type->kind == LogosType::Kind::U16 ||
             e.lhs->type->kind == LogosType::Kind::U32 ||
             e.lhs->type->kind == LogosType::Kind::U24 ||
             e.lhs->type->kind == LogosType::Kind::U56 ||
             e.lhs->type->kind == LogosType::Kind::U64 ||
             e.lhs->type->kind == LogosType::Kind::U128);
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
            bool lhs_is_lit = e.lhs->type && e.lhs->type->kind == LogosType::Kind::FloatLit;
            bool rhs_is_lit = e.rhs->type && e.rhs->type->kind == LogosType::Kind::FloatLit;
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
    auto& op = e.op;
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
        bool is_unsigned = e.lhs->type &&
            (e.lhs->type->kind == LogosType::Kind::U8  ||
             e.lhs->type->kind == LogosType::Kind::U16 ||
             e.lhs->type->kind == LogosType::Kind::U32 ||
             e.lhs->type->kind == LogosType::Kind::U24 ||
             e.lhs->type->kind == LogosType::Kind::U56 ||
             e.lhs->type->kind == LogosType::Kind::U64 ||
             e.lhs->type->kind == LogosType::Kind::U128);
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
        bool is_unsigned = it && (e.lhs->type &&
            (e.lhs->type->kind == LogosType::Kind::U8  ||
             e.lhs->type->kind == LogosType::Kind::U16 ||
             e.lhs->type->kind == LogosType::Kind::U32 ||
             e.lhs->type->kind == LogosType::Kind::U24 ||
             e.lhs->type->kind == LogosType::Kind::U56 ||
             e.lhs->type->kind == LogosType::Kind::U64 ||
             e.lhs->type->kind == LogosType::Kind::U128));
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
        bool is_unsigned_cmp = e.lhs->type &&
            (e.lhs->type->kind == LogosType::Kind::U8  ||
             e.lhs->type->kind == LogosType::Kind::U16 ||
             e.lhs->type->kind == LogosType::Kind::U32 ||
             e.lhs->type->kind == LogosType::Kind::U24 ||
             e.lhs->type->kind == LogosType::Kind::U56 ||
             e.lhs->type->kind == LogosType::Kind::U64 ||
             e.lhs->type->kind == LogosType::Kind::U128);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EUnary& e, const LogosType*) {
    auto val = gen_expr(*e.operand);
    if (!val) return nullptr;
    if (e.op == "-") {
        if (mlir::isa<mlir::FloatType>(val.getType()))
            return builder_.create<mlir::arith::NegFOp>(loc_, val);
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, 0, mlir::cast<mlir::IntegerType>(val.getType()).getWidth());
        return builder_.create<mlir::arith::SubIOp>(loc_, zero, val);
    }
    if (e.op == "!") {
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
    std::fprintf(stderr, "mlir_gen: unknown unary op '%s'\n", e.op.c_str());
    return nullptr;
}

// ---------------------------------------------------------------------------
// AddrOf / Deref
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EAddrOf& e, const LogosType*) {
    // Address-of: return the alloca pointer directly.
    auto it = scope_.find(e.var_name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: & undefined '%s'\n", e.var_name.c_str());
        return nullptr;
    }
    return it->second;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EAddrOfTemp& e, const LogosType*) {
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
                if (gep) {
                    for (auto& f : info.fields) {
                        if (f.name == fr->field) {
                            if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type))
                                return gep;  // inline struct: GEP is the address
                            break;
                        }
                    }
                }
            }
        }
        // Fall through to the general path for non-struct fields.
    }
    auto val = gen_expr(*e.inner);
    if (!val) return nullptr;
    auto* t = e.inner->type;
    if (t && (t->kind == LogosType::Kind::Tuple ||
              t->kind == LogosType::Kind::Struct ||
              t->kind == LogosType::Kind::Datatype ||
              t->kind == LogosType::Kind::Array))
        return val;  // already a pointer to the value on the stack
    // Scalar: spill to a fresh stack slot.
    auto llvm_type = logos_to_mlir(t);
    if (!llvm_type) llvm_type = builder_.getI32Type();
    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), llvm_type, i64_one());
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EDeref& e, const LogosType* type) {
    auto ptr = gen_expr(*e.operand);
    if (!ptr) return nullptr;
    // Structs/datatypes are always pointer-represented in MLIR/LLVM; the
    // logical *-deref just yields the same pointer.  Subsequent field
    // access or the return-by-value wrap handles the byte-level copy.
    // (Previously only Struct was covered here — Datatype fell through to
    // the load branch, producing a bogus double-load through pass-by-ptr
    // parameters: `*const V3` was treated as `ptr-to-ptr-to-V3`.)
    if (type && (type->kind == LogosType::Kind::Struct ||
                 type->kind == LogosType::Kind::Datatype))
        return ptr;
    auto pointee = logos_to_mlir(type);
    if (!pointee) pointee = builder_.getI32Type();
    return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
}

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const ECall& e, const LogosType* ret_logos_type) {
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

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
        auto v = gen_expr(*e.args[i]);
        if (!v) return nullptr;
        // Coerce concrete struct/class → &dyn Trait if param expects it
        if (fpit != fn_param_types_.end() && i < fpit->second.size()) {
            auto* param_lt = fpit->second[i];
            auto* arg_lt = e.args[i]->type;
            if (param_lt && param_lt->kind == LogosType::Kind::TraitObject &&
                arg_lt && arg_lt->kind != LogosType::Kind::TraitObject) {
                v = coerce_to_dyn(v, param_lt->trait_name, type_str(arg_lt));
            }
        }
        if (i < param_types.size()) {
            // Aggregate returned by value but param expects pointer — spill to alloca.
            if (v.getType() != param_types[i] &&
                param_types[i] == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
                v = spill_to_alloca(v);
            else
                v = coerce_numeric(v, param_types[i]);
        }
        args.push_back(v);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EMethodCall& e, const LogosType* ret_logos_type) {
    if (e.method == "as_offset" && e.receiver && e.receiver->type) {
        const auto* rt = e.receiver->type;
        bool is_anyval =
            type_str(rt) == "AnyVal" ||
            ((rt->kind == LogosType::Kind::Ptr ||
              rt->kind == LogosType::Kind::Ref ||
              rt->kind == LogosType::Kind::MutRef) &&
             rt->pointee && type_str(rt->pointee) == "AnyVal");
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
        e.receiver->type->kind == LogosType::Kind::TraitObject &&
        e.vtable_index >= 0) {
        return gen_dyn_dispatch(e, ret_logos_type);
    }
    auto [ptr, tname] = gen_recv_struct(*e.receiver);
    if (!ptr || tname.empty()) return nullptr;
    if (tname == "AnyVal" && ptr.getType() != ptr_type()) {
        auto slot = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), builder_.getI32Type(), i64_one());
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
                v = coerce_numeric(v, param_types[pi]);
        }
        args.push_back(v);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Field / index reads
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EFieldRead& e, const LogosType* type) {
    if (e.field == "raw" && e.receiver->type) {
        bool is_anyval = type_str(e.receiver->type) == "AnyVal";
        bool is_anyval_ptr = (e.receiver->type->kind == LogosType::Kind::Ptr ||
                              e.receiver->type->kind == LogosType::Kind::Ref ||
                              e.receiver->type->kind == LogosType::Kind::MutRef) &&
                             e.receiver->type->pointee &&
                             type_str(e.receiver->type->pointee) == "AnyVal";
        if (is_anyval || is_anyval_ptr) {
            auto recv = gen_expr(*e.receiver);
            if (!recv) return nullptr;
            if (recv.getType() == ptr_type())
                return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
            return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    auto [ptr, sname] = gen_recv_struct(*e.receiver);
    if (!ptr || sname.empty()) return nullptr;
    auto& info = struct_types_[sname];
    auto gep   = gep_field(ptr, info, e.field);
    if (!gep) return nullptr;
    for (auto& f : info.fields)
        if (f.name == e.field)
            return builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, gep);
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EIndexRead& e, const LogosType* type) {
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
                    (ir->index->type->kind == LogosType::Kind::U8  ||
                     ir->index->type->kind == LogosType::Kind::U16 ||
                     ir->index->type->kind == LogosType::Kind::U32 ||
                     ir->index->type->kind == LogosType::Kind::U24 ||
                     ir->index->type->kind == LogosType::Kind::U56 ||
                     ir->index->type->kind == LogosType::Kind::U64 ||
                     ir->index->type->kind == LogosType::Kind::U128);
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
                                    e.receiver->type->kind == LogosType::Kind::Ptr;
                if (field_is_ptr) {
                    arr_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_ptr);
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
        (e.index->type->kind == LogosType::Kind::U8  ||
         e.index->type->kind == LogosType::Kind::U16 ||
         e.index->type->kind == LogosType::Kind::U32 ||
         e.index->type->kind == LogosType::Kind::U24 ||
         e.index->type->kind == LogosType::Kind::U56 ||
         e.index->type->kind == LogosType::Kind::U64 ||
         e.index->type->kind == LogosType::Kind::U128);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EStructLit& e, const LogosType*) {
    return gen_struct_lit(e);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EArrLit& e, const LogosType* type) {
    mlir::Type elem_type = builder_.getI32Type();
    if (type && type->elem) {
        auto et = logos_to_mlir(type->elem);
        if (et) elem_type = et;
    }
    return gen_arr_lit(e, elem_type);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ETupleLit& e, const LogosType* type) {
    auto stype = tuple_llvm_type(type);
    if (!stype) return nullptr;
    // Allocate tuple on stack, store each element via GEP.
    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), stype, i64_one());
    for (uint32_t i = 0; i < e.elems.size(); ++i) {
        auto val = gen_expr(*e.elems[i]);
        if (!val) return nullptr;
        if (type->tuple_elems[i]) {
            auto et = logos_to_mlir(type->tuple_elems[i]);
            if (et) val = coerce_numeric(val, et);
        }
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    }
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ETupleIndex& e, const LogosType* type) {
    auto recv = gen_expr(*e.receiver);
    if (!recv) return nullptr;
    // Auto-deref: if receiver is &(tuple) or &mut(tuple), use pointee for GEP type.
    // recv is already a pointer to the tuple (passed as ptr in calling convention).
    const LogosType* recv_type = e.receiver->type;
    if (recv_type && recv_type->pointee &&
        recv_type->pointee->kind == LogosType::Kind::Tuple &&
        (recv_type->kind == LogosType::Kind::Ref ||
         recv_type->kind == LogosType::Kind::MutRef ||
         recv_type->kind == LogosType::Kind::Ptr))
        recv_type = recv_type->pointee;
    auto stype = tuple_llvm_type(recv_type);
    if (!stype) return nullptr;
    auto elem_mlir = logos_to_mlir(type);
    if (!elem_mlir) return nullptr;
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(e.index)};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, recv, idx);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, gep);
}

// ---------------------------------------------------------------------------
// Cast
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const ECast& e, const LogosType* type) {
    // ── Hermes typed container cast: &[T] as <I32>[] → HermesCtr. ──────────
    if (!e.hermes_build_fn.empty()) {
        auto val = gen_expr(*e.operand);
        if (!val) return nullptr;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto build_fn = find_func_op(parent_mod, e.hermes_build_fn);
        if (!build_fn) {
            std::fprintf(stderr, "mlir_gen: '%s' not found — add 'use hermes.ctr;'\n",
                         e.hermes_build_fn.c_str());
            return nullptr;
        }
        if (build_fn.getNumArguments() == 3) {
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
    auto target = logos_to_mlir(type);
    if (!target || val.getType() == target) return val;

    auto fi = mlir::dyn_cast<mlir::IntegerType>(val.getType());
    auto ti = mlir::dyn_cast<mlir::IntegerType>(target);
    if (fi && ti) {
        if (ti.getWidth() > fi.getWidth()) {
            bool src_unsigned = fi.getWidth() == 1 ||
                (e.operand->type &&
                 (e.operand->type->kind == LogosType::Kind::U8  ||
                  e.operand->type->kind == LogosType::Kind::U16 ||
                  e.operand->type->kind == LogosType::Kind::U32 ||
                  e.operand->type->kind == LogosType::Kind::U24 ||
                  e.operand->type->kind == LogosType::Kind::U56 ||
                  e.operand->type->kind == LogosType::Kind::U64 ||
                  e.operand->type->kind == LogosType::Kind::U128));
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
             (e.operand->type->kind == LogosType::Kind::U8  ||
              e.operand->type->kind == LogosType::Kind::U16 ||
              e.operand->type->kind == LogosType::Kind::U32 ||
              e.operand->type->kind == LogosType::Kind::U24 ||
              e.operand->type->kind == LogosType::Kind::U56 ||
              e.operand->type->kind == LogosType::Kind::U64 ||
              e.operand->type->kind == LogosType::Kind::U128));
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
            (type->kind == LogosType::Kind::U8  ||
             type->kind == LogosType::Kind::U16 ||
             type->kind == LogosType::Kind::U32 ||
             type->kind == LogosType::Kind::U24 ||
             type->kind == LogosType::Kind::U56 ||
             type->kind == LogosType::Kind::U64 ||
             type->kind == LogosType::Kind::U128);
        if (dst_unsigned)
            return builder_.create<mlir::arith::FPToUIOp>(loc_, target, val);
        return builder_.create<mlir::arith::FPToSIOp>(loc_, target, val);
    }

    // int → ptr
    if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) && target == ptr_type()) {
        mlir::Value v64;
        bool src_unsigned = e.operand->type &&
            (e.operand->type->kind == LogosType::Kind::U8  ||
             e.operand->type->kind == LogosType::Kind::U16 ||
             e.operand->type->kind == LogosType::Kind::U32 ||
             e.operand->type->kind == LogosType::Kind::U24 ||
             e.operand->type->kind == LogosType::Kind::U56 ||
             e.operand->type->kind == LogosType::Kind::U64 ||
             e.operand->type->kind == LogosType::Kind::U128);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const ENew& e, const LogosType*) {
    auto sit = struct_types_.find(e.class_name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: unknown class '%s'\n", e.class_name.c_str());
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
    for (auto& [fname, fval] : e.fields) {
        auto val = gen_expr(*fval);
        if (!val) return nullptr;
        auto gep = gep_field(raw, info, fname);
        if (!gep) return nullptr;
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    }

    return raw;  // *mut ClassName
}

// ---------------------------------------------------------------------------
// If-expression / match-expression
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const EIfExpr& e, const LogosType* type) {
    auto cond = gen_expr(*e.cond);
    if (!cond) return nullptr;

    mlir::Type result_type = logos_to_mlir(type);
    if (!result_type) return nullptr;

    // Allocate result slot in the current (entry-reachable) block.
    auto result_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), result_type, i64_one());

    auto* region      = builder_.getBlock()->getParent();
    auto* then_block  = new mlir::Block();
    auto* else_block  = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(then_block);
    region->push_back(else_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

    builder_.setInsertionPointToStart(then_block);
    auto then_val = gen_expr(*e.then_val);
    if (!then_val) then_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    then_val = coerce_numeric(then_val, result_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, then_val, result_alloca);
    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(else_block);
    auto else_val = gen_expr(*e.else_val);
    if (!else_val) else_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    else_val = coerce_numeric(else_val, result_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, else_val, result_alloca);
    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(merge_block);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EMatchExpr& e, const LogosType* type) {
    mlir::Type result_type = logos_to_mlir(type);
    if (!result_type) return nullptr;

    // Allocate result slot before the match (entry-block reachable).
    auto result_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), result_type, i64_one());

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
    if (e.scrut->type && e.scrut->type->kind == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(e.scrut->type->enum_name, e.scrut->type);
        if (te_info) {
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
                        auto val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, vp->field_types[bi], fp);
                        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type(), vp->field_types[bi], i64_one());
                        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                        scope_[pvd->bindings[bi]] = alloca;
                        let_vars_.insert(pvd->bindings[bi]);
                        var_elem_types_[pvd->bindings[bi]] = vp->field_types[bi];
                        added.push_back(pvd->bindings[bi]);
                    }
                }
            }
        } else if (auto* pw = std::get_if<PatWild>(&arm.pat)) {
            if (pw->name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), sv.getType(), i64_one());
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
    if (e.scrut->type && e.scrut->type->kind == LogosType::Kind::Bool) {
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
    } else if (e.scrut->type && e.scrut->type->kind == LogosType::Kind::Enum) {
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
            auto eit = enum_types_.find(e.scrut->type->enum_name);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(e.scrut->type->enum_name, e.scrut->type)) {
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
                if (val) {
                    val = coerce_numeric(val, result_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                }
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        } else {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(body_block);
            auto added = extract_arm_payload(arm);
            auto val = gen_expr(*arm.value);
            for (auto& n : added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
            if (val) {
                val = coerce_numeric(val, result_type);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
            }
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EClosureBox& box, const LogosType* type) {
    if (!box.inner) return nullptr;
    return gen_closure(*box.inner, type);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EClosureCall& e, const LogosType* type) {
    auto closure = gen_expr(*e.callee);
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
    for (auto& a : e.args) {
        auto val = gen_expr(*a);
        if (!val) return nullptr;
        args.push_back(val);
        param_types.push_back(val.getType());
    }

    mlir::Type ret = type ? logos_to_mlir(type) : nullptr;
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
    return call.getResult();
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EFnPtrCall& e, const LogosType* type) {
    // Bare function pointer call: fn_ptr(arg1, arg2, ...) — no env_ptr.
    auto fn_ptr = gen_expr(*e.callee);
    if (!fn_ptr) return nullptr;

    // fn_ptr is stored as a scalar (not in an alloca) when it's a let var;
    // but scope_ stores allocas for let-bound scalars, so load it first.
    // Actually FnPtr variables are stored as scalars (like integers) — load from alloca.
    // (fn_ptr here is the raw pointer value, already loaded by gen_expr_kind(EVarRef))

    llvm::SmallVector<mlir::Value> args;
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& a : e.args) {
        auto val = gen_expr(*a);
        if (!val) return nullptr;
        args.push_back(val);
        param_types.push_back(val.getType());
    }

    mlir::Type ret = type ? logos_to_mlir(type) : nullptr;
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
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Slice helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const ESliceLit& e, const LogosType*) {
    auto base = gen_expr(*e.base);
    auto len  = gen_expr(*e.len);
    if (!base || !len) return nullptr;
    auto stype = slice_llvm_type();
    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), stype, i64_one());
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

mlir::Value MLIRGenImpl::gen_expr_kind(const ESliceIndex& e, const LogosType* type) {
    auto slice = gen_expr(*e.slice);
    auto index = gen_expr(*e.index);
    if (!slice || !index) return nullptr;
    auto elem_type = logos_to_mlir(type);
    if (!elem_type) elem_type = builder_.getI32Type();
    auto stype = slice_llvm_type();
    // Load ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, pi);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
    // GEP into data array by index.
    bool idx_unsigned = e.index->type &&
        (e.index->type->kind == LogosType::Kind::U8  ||
         e.index->type->kind == LogosType::Kind::U16 ||
         e.index->type->kind == LogosType::Kind::U32 ||
         e.index->type->kind == LogosType::Kind::U24 ||
         e.index->type->kind == LogosType::Kind::U56 ||
         e.index->type->kind == LogosType::Kind::U64 ||
         e.index->type->kind == LogosType::Kind::U128);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const ESliceLen& e, const LogosType*) {
    auto slice = gen_expr(*e.slice);
    if (!slice) return nullptr;
    auto stype = slice_llvm_type();
    // Load len from field 1
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, li);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
}

// ---------------------------------------------------------------------------
// format() built-in
// ---------------------------------------------------------------------------

int MLIRGenImpl::format_type_tag(const LogosType* t) noexcept {
    if (!t) return 0;
    switch (t->kind) {
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EFormatCall& e, const LogosType*) {
    auto fmt_val = gen_expr(*e.fmt);
    if (!fmt_val) return nullptr;

    int n = (int)e.args.size();
    auto i32_type = builder_.getI32Type();
    auto i64_type = builder_.getI64Type();

    // Allocate [n x i32] tags and [n x i64] data arrays on stack.
    mlir::Value n_alloc = builder_.create<mlir::arith::ConstantIntOp>(loc_, n > 0 ? n : 1, 64);
    auto tags_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), i32_type, n_alloc);
    auto data_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), i64_type, n_alloc);

    for (int i = 0; i < n; ++i) {
        int tag = format_type_tag(e.arg_types[i]);

        // Store tag at tags[i]
        llvm::SmallVector<mlir::LLVM::GEPArg> ti{int32_t(i)};
        auto tgep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i32_type, tags_alloca, ti);
        auto tag_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, tag, 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, tag_val, tgep);

        // Evaluate arg and widen to i64.
        // Unsigned types narrower than 64 bits must be zero-extended, not sign-extended.
        auto arg_val = gen_expr(*e.args[i]);
        if (!arg_val) return nullptr;
        mlir::Value as_i64;
        if (arg_val.getType() == ptr_type()) {
            as_i64 = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_type, arg_val);
        } else {
            auto* arg_lt = static_cast<size_t>(i) < e.arg_types.size() ? e.arg_types[i] : nullptr;
            bool arg_unsigned = arg_lt &&
                (arg_lt->kind == LogosType::Kind::U8   ||
                 arg_lt->kind == LogosType::Kind::U16  ||
                 arg_lt->kind == LogosType::Kind::U32  ||
                 arg_lt->kind == LogosType::Kind::U24  ||
                 arg_lt->kind == LogosType::Kind::U56  ||
                 arg_lt->kind == LogosType::Kind::U64  ||
                 arg_lt->kind == LogosType::Kind::U128);
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
            "mlir_gen: format() requires 'use std.string;' to be imported\n");
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EPackExpand&, const LogosType*) {
    std::fprintf(stderr, "mlir_gen: unexpected EPackExpand (should be expanded by mono)\n");
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(const ESizeOf& e, const LogosType*) {
    // For Struct/Datatype: logos_to_mlir returns ptr_type() (always passed by pointer),
    // but sizeof needs the actual aggregate type, not the pointer.
    mlir::Type elem_mlir = nullptr;
    if (e.elem_type && (e.elem_type->kind == LogosType::Kind::Struct ||
                        e.elem_type->kind == LogosType::Kind::Datatype)) {
        auto cname = concrete_struct_name(e.elem_type);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end())
            elem_mlir = sit->second.llvm_type;
    }
    if (!elem_mlir) elem_mlir = logos_to_mlir(e.elem_type);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const ETypeCodeOf& e, const LogosType*) {
    // Should have been folded to ELitInt by mono.  Emit 0 as a defensive
    // fallback (not expected to be reached for well-formed programs).
    (void)e;
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(const EBlockExpr& e, const LogosType*) {
    if (e.block) gen_block(*e.block);
    if (is_terminated(builder_.getBlock())) return nullptr;
    if (e.result) return gen_expr(*e.result);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Try expression: expr?
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(const ETry& e, const LogosType* type) {
    auto inner_ptr = gen_expr(*e.inner);
    if (!inner_ptr) return nullptr;
    // Aggregate returned by value — spill to alloca so GEP works below.
    inner_ptr = spill_to_alloca(inner_ptr);

    auto* te = resolve_tagged_enum(e.inner->type->enum_name, e.inner->type);
    if (!te) {
        std::fprintf(stderr, "mlir_gen: ETry: cannot resolve Result enum\n");
        return nullptr;
    }

    // Load discriminant at offset (0,0)
    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
    auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), te->llvm_type, inner_ptr, di);
    auto disc     = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), disc_ptr);
    auto ok_cst   = builder_.create<mlir::arith::ConstantIntOp>(loc_, e.ok_disc, 32);
    auto is_ok    = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, disc, ok_cst);

    auto ok_mlir = logos_to_mlir(type);
    if (!ok_mlir) return nullptr;
    auto result_alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), ok_mlir, i64_one());

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
        for (auto& v : te->variants) if (v.disc == e.ok_disc) { ok_vp = &v; break; }

        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, inner_ptr, pi);
        if (ok_vp && !ok_vp->field_types.empty()) {
            auto ps  = mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), ok_vp->field_types);
            llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
            auto fp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ps, pay_ptr, fi);
            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, ok_vp->field_types[0], fp);
            builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_int(val, ok_mlir), result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    // ── err_block: extract E payload, build Err return, early func.return ──
    builder_.setInsertionPointToStart(err_block);
    {
        const TaggedEnumInfo::VariantPayload* err_vp = nullptr;
        for (auto& v : te->variants) if (v.disc == e.err_disc) { err_vp = &v; break; }

        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, inner_ptr, pi);

        auto ret_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), te->llvm_type, i64_one());
        // Store err discriminant
        llvm::SmallVector<mlir::LLVM::GEPArg> di2{int32_t(0), int32_t(0)};
        auto rdp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), te->llvm_type, ret_alloca, di2);
        auto edc = builder_.create<mlir::arith::ConstantIntOp>(loc_, e.err_disc, 32);
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
// Hermes SDN literal — Phase 2: compile-time zone blob builder
// ---------------------------------------------------------------------------

namespace {

// Compile-time Hermes zone builder.
// Produces a flat byte buffer that is a valid sealed Hermes zone with one
// document: a 4-byte AnyVal root at offset 0, followed by all objects.
//
// Memory layout rules (from stdlib/hermes/):
//   - All allocations are 8-byte aligned.
//   - Tagged objects (tc 1-222): 1-byte type_code at obj[-1] (immediately before
//     body). Alignment: pre = offset + 1, aligned = ceil(pre / 8) * 8. Tag byte
//     written at buf[aligned-1]; body starts at buf[aligned].
//   - AnyVal (32-bit, little-endian):
//       null     = 0
//       bool     = (v << 8) | 0x4B       (type_hash 37, tag byte bits[7:1])
//       i24 int  = ((v & 0xFFFFFF) << 8) | 0x2F  (type_hash 23, fits -8388608..8388607)
//       zone ptr = zone_offset            (bit0 = 0; 8-byte aligned objects)
//   - DocumentHeader: 4-byte AnyVal root at zone offset 0 (allocated as 8 bytes).
//   - HermesString (tc=28): vlen-encoded length prefix + UTF-8 bytes, no NUL.
//     vlen: 1 byte if len < 249, else 1 + N bytes (249+N marker, then N LE bytes).
//   - ObjectArray (tc=100): u64 size, u64 capacity, u32 data_off (+ 4B pad = 24B).
//     Data buffer: capacity × 4-byte AnyVal values.
//   - ObjectMap (tc=101): u32 entries_off, u32 capacity, u32 count, u32 reserved (16B).
//     Entries buffer: capacity × 8 bytes (key_off: u32, val: u32 AnyVal).
//     Hash: FNV-1a over raw key bytes, linear probing, cap = power-of-2 >= 2*count.
//   - I64 (tc=26): 8 bytes i64 body (large integers only; small ones use inline i24).
//   - F64 (tc=31): 8 bytes f64 body.

struct ZoneBuilder {
    std::vector<uint8_t> buf;
    // (blob_offset, value_idx): where each PARAM AnyVal slot lives in buf.
    // Populated when build_hermes_val encounters an HVCapture node.
    std::vector<std::pair<uint32_t, uint32_t>> param_slots;

    void push8(uint8_t v)  { buf.push_back(v); }

    void push32le(uint32_t v) {
        buf.push_back(v & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 24) & 0xFF);
    }

    void push64le(uint64_t v) {
        push32le(static_cast<uint32_t>(v));
        push32le(static_cast<uint32_t>(v >> 32));
    }

    void align8() {
        while (buf.size() % 8 != 0) buf.push_back(0);
    }

    // Allocate `sz` bytes aligned to 8, returning the offset of the first byte.
    uint32_t alloc_raw(size_t sz) {
        align8();
        uint32_t off = static_cast<uint32_t>(buf.size());
        buf.resize(buf.size() + sz, 0);
        return off;
    }

    // Allocate a tagged object with type_code `tc`.
    // Tag byte is written at buf[aligned-1] (i.e. obj[-1] per datatag.logos).
    // Returns the offset of the first byte of the object body.
    uint32_t alloc_tagged(uint8_t tc, size_t body_sz) {
        // pre = current end + 1 tag byte; aligned = next 8-byte boundary.
        size_t pre     = buf.size() + 1;
        size_t aligned = (pre + 7) / 8 * 8;
        buf.resize(aligned, 0);      // zero-pad up to alignment boundary
        buf[aligned - 1] = tc;       // tag at obj[-1]
        uint32_t obj_off = static_cast<uint32_t>(aligned);
        buf.resize(buf.size() + body_sz, 0);
        return obj_off;
    }

    void write32le(uint32_t off, uint32_t v) {
        buf[off]     = v & 0xFF;
        buf[off + 1] = (v >> 8) & 0xFF;
        buf[off + 2] = (v >> 16) & 0xFF;
        buf[off + 3] = (v >> 24) & 0xFF;
    }

    void write64le(uint32_t off, uint64_t v) {
        write32le(off,     static_cast<uint32_t>(v));
        write32le(off + 4, static_cast<uint32_t>(v >> 32));
    }

    uint32_t read32le(uint32_t off) const {
        return static_cast<uint32_t>(buf[off])
             | (static_cast<uint32_t>(buf[off + 1]) << 8)
             | (static_cast<uint32_t>(buf[off + 2]) << 16)
             | (static_cast<uint32_t>(buf[off + 3]) << 24);
    }
};

static uint64_t fnv1a_str(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ULL;
    return h;
}

// Forward declaration.
static uint32_t build_hermes_val(ZoneBuilder& zb, const lir::HermesVal& v);

// vlen_encode_size: bytes needed to encode `value` in Hermes vlen format.
// values < 249: 1 byte; values >= 249: 1 + N bytes (N = ceil(log256(value))).
static size_t vlen_encode_size(uint64_t value) {
    if (value < 249) return 1;
    size_t n = 0;
    uint64_t v = value;
    while (v > 0) { v >>= 8; ++n; }
    if (n > 7) n = 7;
    return 1 + n;
}

// Build a HermesString object. Returns zone offset of the object body.
// Layout: vlen-encoded length prefix + raw UTF-8 bytes (no NUL).
static uint32_t build_string(ZoneBuilder& zb, const std::string& s) {
    uint64_t slen    = s.size();
    size_t   vlen_sz = vlen_encode_size(slen);
    size_t   body_sz = vlen_sz + slen;
    uint32_t obj_off = zb.alloc_tagged(28, body_sz);

    // Write vlen prefix.
    if (slen < 249) {
        zb.buf[obj_off] = static_cast<uint8_t>(slen);
    } else {
        size_t n = vlen_sz - 1;
        zb.buf[obj_off] = static_cast<uint8_t>(248 + n);
        uint64_t v = slen;
        for (size_t i = 0; i < n; ++i) {
            zb.buf[obj_off + 1 + i] = static_cast<uint8_t>(v & 0xFF);
            v >>= 8;
        }
    }
    // Write UTF-8 bytes.
    for (size_t i = 0; i < s.size(); ++i)
        zb.buf[obj_off + vlen_sz + i] = static_cast<uint8_t>(s[i]);
    return obj_off;
}

static inline void record_param_if(ZoneBuilder& zb, uint32_t av_off, uint32_t av_raw) {
    if ((av_raw & 0xFFu) == 0xFFu)
        zb.param_slots.push_back({av_off, (av_raw >> 8u) & 0xFFFFFFu});
}


// Build an ObjectArray (tc=100). Returns zone offset of the object body.
static uint32_t build_anyval_array(ZoneBuilder& zb, const lir::HVArray& arr) {
    uint64_t count = arr.elements.size();

    // Build all element AnyVals first (may allocate), then write array header.
    std::vector<uint32_t> elem_anyvals;
    elem_anyvals.reserve(count);
    for (auto& ep : arr.elements)
        elem_anyvals.push_back(build_hermes_val(zb, *ep));

    // Data buffer: capacity × 4 bytes (AnyVal). Empty array → cap=0, no data bytes.
    uint64_t cap = count;
    uint32_t data_raw_off = zb.alloc_raw(static_cast<size_t>(cap) * 4);
    for (uint64_t i = 0; i < count; i++) {
        uint32_t av_off = data_raw_off + static_cast<uint32_t>(i) * 4;
        zb.write32le(av_off, elem_anyvals[i]);
        record_param_if(zb, av_off, elem_anyvals[i]);
    }

    // ObjectArray header: u64 size, u64 capacity, u32 data_off, u32 padding.
    uint32_t hdr_off = zb.alloc_tagged(100, 24);
    zb.write64le(hdr_off,      count);
    zb.write64le(hdr_off + 8,  cap);
    zb.write32le(hdr_off + 16, data_raw_off);
    // bytes 20-23: padding, already 0
    return hdr_off;
}

// Build an ArrayI32 (tc=104): dense i32 elements.
// Layout: { u64 size, u64 capacity, u32 data_off, u32 pad } + capacity×4 i32 bytes.
static uint32_t build_typed_array_i32(ZoneBuilder& zb, const lir::HVArray& arr) {
    uint64_t count = arr.elements.size();
    uint32_t data_raw_off = zb.alloc_raw(static_cast<size_t>(count) * 4);
    for (uint64_t i = 0; i < count; i++) {
        int32_t v = 0;
        if (arr.elements[i]) {
            std::visit([&](const auto& k) {
                if constexpr (std::is_same_v<std::decay_t<decltype(k)>, lir::HVInt>)
                    v = static_cast<int32_t>(k.value);
            }, arr.elements[i]->kind);
        }
        zb.write32le(data_raw_off + static_cast<uint32_t>(i) * 4,
                     static_cast<uint32_t>(v));
    }
    uint32_t hdr_off = zb.alloc_tagged(104, 24);
    zb.write64le(hdr_off,      count);
    zb.write64le(hdr_off + 8,  count);
    zb.write32le(hdr_off + 16, data_raw_off);
    return hdr_off;
}

// Build an ArrayU64 (tc=108): dense u64 elements.
// Layout: { u64 size, u64 capacity, u32 data_off, u32 pad } + capacity×8 u64 bytes.
static uint32_t build_typed_array_u64(ZoneBuilder& zb, const lir::HVArray& arr) {
    uint64_t count = arr.elements.size();
    uint32_t data_raw_off = zb.alloc_raw(static_cast<size_t>(count) * 8);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t v = 0;
        if (arr.elements[i]) {
            std::visit([&](const auto& k) {
                if constexpr (std::is_same_v<std::decay_t<decltype(k)>, lir::HVInt>)
                    v = static_cast<uint64_t>(k.value);
            }, arr.elements[i]->kind);
        }
        zb.write64le(data_raw_off + static_cast<uint32_t>(i) * 8, v);
    }
    uint32_t hdr_off = zb.alloc_tagged(108, 24);
    zb.write64le(hdr_off,      count);
    zb.write64le(hdr_off + 8,  count);
    zb.write32le(hdr_off + 16, data_raw_off);
    return hdr_off;
}

// Dispatch: typed array → dense builder; untyped → ObjectArray.
static uint32_t build_array(ZoneBuilder& zb, const lir::HVArray& arr) {
    if (arr.elem_type == "I32") return build_typed_array_i32(zb, arr);
    if (arr.elem_type == "U64") return build_typed_array_u64(zb, arr);
    return build_anyval_array(zb, arr);
}

// Build a MapI32AnyVal (tc=105): dense parallel keys[]/vals[] arrays. Returns zone offset.
static uint32_t build_map_i32_anyval(ZoneBuilder& zb, const lir::HVMap& map) {
    uint32_t count = static_cast<uint32_t>(map.entries.size());

    // First pass: build all values (may cause zone growth — track by offset only).
    std::vector<uint32_t> val_avs;
    val_avs.reserve(count);
    for (auto& e : map.entries)
        val_avs.push_back(build_hermes_val(zb, *e.val));

    // Alloc keys/vals buffers only when non-empty (alloc_raw(0) is a no-op and
    // two consecutive calls return the same offset, corrupting the header).
    uint32_t keys_off = count > 0 ? zb.alloc_raw(static_cast<size_t>(count) * 4) : 0;
    uint32_t vals_off = count > 0 ? zb.alloc_raw(static_cast<size_t>(count) * 4) : 0;

    // Write keys.
    for (uint32_t i = 0; i < count; i++) {
        int32_t k = 0;
        if (auto* iv = std::get_if<int64_t>(&map.entries[i].key))
            k = static_cast<int32_t>(*iv);
        zb.write32le(keys_off + i * 4, static_cast<uint32_t>(k));
    }

    // MapI32AnyVal header (tc=105): size(u32), capacity(u32), keys(u32), vals(u32) = 16 bytes.
    uint32_t hdr_off = zb.alloc_tagged(105, 16);
    zb.write32le(hdr_off,      count);
    zb.write32le(hdr_off + 4,  count);
    zb.write32le(hdr_off + 8,  keys_off);
    zb.write32le(hdr_off + 12, vals_off);

    // Write values.
    // C4 bug fix: call record_param_if so PARAM captures in MapI32AnyVal values
    // are tracked in param_slots (same as ObjectMap and ObjectArray do).
    for (uint32_t i = 0; i < count; i++) {
        zb.write32le(vals_off + i * 4, val_avs[i]);
        record_param_if(zb, vals_off + i * 4, val_avs[i]);
    }

    return hdr_off;
}

static uint32_t build_map(ZoneBuilder& zb, const lir::HVMap& map) {
    if (map.key_type == "I32") return build_map_i32_anyval(zb, map);
    uint32_t count = static_cast<uint32_t>(map.entries.size());

    // Collect all key strings and their HermesString offsets; build values.
    // Entry = (key_str_obj_off, val_anyval).
    struct RawEntry { uint32_t key_off; uint32_t val; };
    std::vector<RawEntry> raw;
    raw.reserve(count);

    for (auto& e : map.entries) {
        std::string key_str;
        if (std::holds_alternative<std::string>(e.key))
            key_str = std::get<std::string>(e.key);
        else
            key_str = std::to_string(std::get<int64_t>(e.key));

        uint32_t key_off = build_string(zb, key_str);
        uint32_t val_av  = build_hermes_val(zb, *e.val);
        raw.push_back({key_off, val_av});
    }

    // Hash table: capacity = smallest power of 2 >= 2*count (minimum 8).
    uint32_t cap = 8;
    while (cap < count * 2) cap *= 2;

    // Entries buffer: cap × 8 bytes (key_off: u32, val: u32).
    // Empty slot sentinel: key_off = 0 (zone offset 0 = DocumentHeader = never a string).
    uint32_t entries_raw_off = zb.alloc_raw(static_cast<size_t>(cap) * 8);

    // Insert entries using FNV-1a over key string + linear probing.
    for (size_t i = 0; i < map.entries.size(); i++) {
        std::string key_str;
        if (std::holds_alternative<std::string>(map.entries[i].key))
            key_str = std::get<std::string>(map.entries[i].key);
        else
            key_str = std::to_string(std::get<int64_t>(map.entries[i].key));

        uint64_t h = fnv1a_str(key_str);
        uint32_t slot = static_cast<uint32_t>(h & (cap - 1));
        // Linear probe: find empty slot (key_off == 0). Cap >= 2*count ensures termination.
        for (uint32_t probed = 0; probed < cap; ++probed) {
            uint32_t ep = entries_raw_off + slot * 8;
            if (zb.read32le(ep) == 0) break;
            slot = (slot + 1) & (cap - 1);
        }
        uint32_t ep = entries_raw_off + slot * 8;
        zb.write32le(ep,     raw[i].key_off);
        zb.write32le(ep + 4, raw[i].val);
        record_param_if(zb, ep + 4, raw[i].val);
    }

    // ObjectMap header: u32 entries_off, u32 capacity, u32 count, u32 reserved.
    uint32_t hdr_off = zb.alloc_tagged(101, 16);
    zb.write32le(hdr_off,      entries_raw_off);
    zb.write32le(hdr_off + 4,  cap);
    zb.write32le(hdr_off + 8,  count);
    // bytes 12-15: reserved = 0
    return hdr_off;
}

// Build a HermesVal node, return its AnyVal encoding (32-bit).
static uint32_t build_hermes_val(ZoneBuilder& zb, const lir::HermesVal& v) {
    return std::visit([&](auto& k) -> uint32_t {
        using T = std::decay_t<decltype(k)>;
        if constexpr (std::is_same_v<T, lir::HVNull>) {
            return 0;  // AnyVal null
        } else if constexpr (std::is_same_v<T, lir::HVBool>) {
            // type_code 37 → bits[7:1] = 37 → raw bits = 0x4A; bit0=1 → 0x4B
            uint32_t payload = k.value ? 1u : 0u;
            return (payload << 8) | 0x4Bu;
        } else if constexpr (std::is_same_v<T, lir::HVInt>) {
            // Small integers (fitting in 24 signed bits) use inline AnyVal embed_i24
            // (type_hash 23, tag byte 0x2F). Matches parser wire format and allows
            // anyval.as_i64() to work correctly.
            if (k.value >= -8388608LL && k.value <= 8388607LL) {
                uint32_t raw = (static_cast<uint32_t>(static_cast<int32_t>(k.value))
                                & 0xFFFFFFu) << 8 | 0x2Fu;
                return raw;
            }
            // Larger integers: I64 zone object (type_code 26), 8-byte body.
            uint32_t obj_off = zb.alloc_tagged(26, 8);
            zb.write64le(obj_off, static_cast<uint64_t>(k.value));
            return obj_off;
        } else if constexpr (std::is_same_v<T, lir::HVFloat>) {
            // F64/Double zone object: type_code 31 (not 27 which is U64).
            uint32_t obj_off = zb.alloc_tagged(31, 8);
            uint64_t bits;
            static_assert(sizeof(double) == 8);
            std::memcpy(&bits, &k.value, 8);
            zb.write64le(obj_off, bits);
            return obj_off;
        } else if constexpr (std::is_same_v<T, lir::HVStr>) {
            uint32_t obj_off = build_string(zb, k.value);
            return obj_off;
        } else if constexpr (std::is_same_v<T, lir::HVArray>) {
            uint32_t obj_off = build_array(zb, k);
            return obj_off;
        } else if constexpr (std::is_same_v<T, lir::HVMap>) {
            uint32_t obj_off = build_map(zb, k);
            return obj_off;
        } else if constexpr (std::is_same_v<T, lir::HVCapture>) {
            // PARAM placeholder AnyVal: type_hash=127 (0x7F), bit0=1.
            // raw = (param_index << 8) | 0xFF
            // Offset is recorded at caller when the AnyVal is written to buf.
            return (k.param_index << 8u) | 0xFFu;
        } else {
            return 0;
        }
    }, v.kind);
}


struct HermesZoneBuild {
    std::vector<uint8_t>                        blob;
    std::vector<std::pair<uint32_t, uint32_t>>  param_slots;  // (blob_off, value_idx)
};

// Build the full zone blob for an EHermesLit node.
// blob[0..7]: DocumentHeader (root AnyVal at offset 0, 4 bytes + 4 pad).
// param_slots: (blob_offset, value_idx) for each PARAM AnyVal in the blob.
static HermesZoneBuild build_hermes_zone(const lir::EHermesLit& e) {
    ZoneBuilder zb;

    // Reserve 8 bytes at offset 0 for DocumentHeader (AnyVal root, 4 bytes + 4 pad).
    zb.alloc_raw(8);

    // Build the root value tree.
    uint32_t root_av = build_hermes_val(zb, *e.root);

    // Write root AnyVal at offset 0 (DocumentHeader).
    zb.write32le(0, root_av);
    record_param_if(zb, 0, root_av);

    return {std::move(zb.buf), std::move(zb.param_slots)};
}

}  // namespace (zone builder helpers)

// Coerce a Logos runtime value to AnyVal.raw (u32) for hermes capture substitution.
// Handles scalars that fit in 24 bits (embed_i24/embed_bool/etc.) and AnyVal passthrough.
// String/large-integer coercion is implemented in C5.
mlir::Value MLIRGenImpl::coerce_to_anyval_raw(mlir::Value v, const LogosType* t) {
    if (!v || !t) return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    auto i32_mlir = builder_.getIntegerType(32);
    using K = LogosType::Kind;
    switch (t->kind) {
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
            if (t->struct_name == "AnyVal") {
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EHermesLit& e, const LogosType* ret_type) {
    auto [blob, param_slots] = build_hermes_zone(e);

    auto lit_idx    = hermes_lit_counter_++;
    auto global_name = "__hermes_lit_" + std::to_string(lit_idx);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    // Emit template blob as a rodata global.
    auto i8       = builder_.getIntegerType(8);
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, blob.size());
    auto blob_attr = builder_.getStringAttr(
        llvm::StringRef(reinterpret_cast<const char*>(blob.data()), blob.size()));
    builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
        global_name, blob_attr);

    if (!e.has_captures) {
        builder_.restoreInsertionPoint(save_pt);
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
    }

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
    // Zone-alloc captures need the HermesCtr to exist before coercion, so we
    // use the hermes_template_ctr_new + hermes_ctr_alloc_* + hermes_template_patch path.
    auto is_zone_alloc_cap = [](const LogosType* t) -> bool {
        if (!t) return false;
        using K = LogosType::Kind;
        if (t->kind == K::F64 || t->kind == K::F32 || t->kind == K::FloatLit) return true;
        if (t->kind == K::Ptr) return true;  // *const u8 → C-string varchar
        if (t->kind == K::Struct && t->struct_name == "StringView") return true;
        return false;
    };
    bool any_zone_alloc = false;
    for (auto* ct : e.capture_types) {
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
        resolved_ptr = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), arr_t, i64_one());
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
                         "add 'use hermes.ctr;' to your file\n");
            return nullptr;
        }

        // Count zone-alloc captures for capacity estimate (4096 per string, 16 per f64/f32).
        // C5-fix3: only count zone-alloc captures (skip scalar/AnyVal captures).
        // C5-fix2: include K::FloatLit in the f64 branch (16 bytes), not the string branch.
        int64_t extra_cap_bytes = 0;
        for (auto* ct : e.capture_types) {
            using K = LogosType::Kind;
            if (!ct || !is_zone_alloc_cap(ct)) continue;
            if (ct->kind == K::F64 || ct->kind == K::F32 || ct->kind == K::FloatLit)
                extra_cap_bytes += 16;
            else
                extra_cap_bytes += 4096;  // string: generous estimate
        }
        mlir::Value extra_cap_v = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, extra_cap_bytes, 64);

        // Create the HermesCtr with template pre-loaded.
        auto new_call = builder_.create<mlir::func::CallOp>(
            loc_, new_fn,
            mlir::ValueRange{tmpl_ptr_v, tmpl_size_v, extra_cap_v});
        if (new_call.getNumResults() == 0) return nullptr;
        mlir::Value ctr_val  = new_call.getResult(0);
        mlir::Type  ctr_type = new_fn.getFunctionType().getResult(0);

        // Alloca HermesCtr so we can take its address for alloc helpers.
        mlir::Value ctr_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), ctr_type, i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, ctr_val, ctr_alloca);

        // For each unique capture: gen_expr, coerce, store in resolved[i].
        for (size_t i = 0; i < n_values; ++i) {
            mlir::Value cap_val = gen_expr(*e.capture_exprs[i]);
            if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

            const LogosType* ct = e.capture_types[i];
            mlir::Value raw_u32 = nullptr;

            if (is_zone_alloc_cap(ct)) {
                using K = LogosType::Kind;
                if ((ct->kind == K::F64 || ct->kind == K::F32 ||
                     ct->kind == K::FloatLit) && alloc_f64_fn) {
                    // Widen f32 → f64 if needed. FloatLit defaults to f64.
                    mlir::Value f64_val = cap_val;
                    if (ct->kind == K::F32) {
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
                } else if (ct->kind == K::Ptr && alloc_cstr_fn) {
                    // *const u8 — treat as null-terminated C-string.
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_cstr_fn, mlir::ValueRange{ctr_alloca, cap_val});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ct->kind == K::Struct && ct->struct_name == "StringView"
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

        // Return the HermesCtr by value (load from alloca).
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
                     "add 'use hermes.ctr;' to your file\n");
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> build_args{
        tmpl_ptr_v, tmpl_size_v, slots_ptr_v, n_slots_v, resolved_ptr, n_values_v};
    auto build_call = builder_.create<mlir::func::CallOp>(loc_, build_fn, mlir::ValueRange(build_args));
    if (build_call.getNumResults() == 0) return nullptr;
    return build_call.getResult(0);
}

} // namespace logos::compiler
