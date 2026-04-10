// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_expr.cpp — Expression code generation.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

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
    // Strip surrounding quotes.
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    // Process escape sequences.
    std::string text;
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
        // Check if name is a free function being used as a value (function pointer).
        // Create a non-capturing closure: {fn_ptr, null_env}.
        if (type && type->kind == LogosType::Kind::Closure) {
            auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
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
    if (var_struct_.count(e.name) || var_subscript_.count(e.name) ||
        var_class_.count(e.name) || var_tuple_.count(e.name) ||
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
    auto rhs = gen_expr(*e.rhs);
    if (!lhs || !rhs) return nullptr;
    // Widen narrower integer operand, using zero-extend for unsigned types.
    if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
        if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
            if (li.getWidth() < ri.getWidth()) {
                bool lhs_unsigned = e.lhs->type &&
                    (e.lhs->type->kind == LogosType::Kind::U8  ||
                     e.lhs->type->kind == LogosType::Kind::U16 ||
                     e.lhs->type->kind == LogosType::Kind::U32 ||
                     e.lhs->type->kind == LogosType::Kind::U64);
                if (lhs_unsigned)
                    lhs = builder_.create<mlir::arith::ExtUIOp>(loc_, rhs.getType(), lhs);
                else
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
            } else if (ri.getWidth() < li.getWidth()) {
                bool rhs_unsigned = e.rhs->type &&
                    (e.rhs->type->kind == LogosType::Kind::U8  ||
                     e.rhs->type->kind == LogosType::Kind::U16 ||
                     e.rhs->type->kind == LogosType::Kind::U32 ||
                     e.rhs->type->kind == LogosType::Kind::U64);
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
             e.rhs->type->kind == LogosType::Kind::U64);
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
             e.lhs->type->kind == LogosType::Kind::U64);
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
             e.lhs->type->kind == LogosType::Kind::U64);
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
             e.lhs->type->kind == LogosType::Kind::U64));
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
             e.lhs->type->kind == LogosType::Kind::U64);
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

mlir::Value MLIRGenImpl::gen_expr_kind(const EDeref& e, const LogosType* type) {
    auto ptr = gen_expr(*e.operand);
    if (!ptr) return nullptr;
    // Structs and classes are always pointer-represented in MLIR/LLVM.
    // Dereferencing *Struct or &mut Struct just yields the same pointer — no load needed.
    if (type && (type->kind == LogosType::Kind::Class ||
                 type->kind == LogosType::Kind::Struct))
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

    auto callee_fn  = parent_mod.lookupSymbol<mlir::func::FuncOp>(e.callee);
    if (!callee_fn) {
        std::fprintf(stderr, "mlir_gen: undefined function '%s'\n", e.callee.c_str());
        return nullptr;
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
    // &dyn Trait dispatch: load vtable, GEP slot, indirect call
    if (e.receiver->type &&
        e.receiver->type->kind == LogosType::Kind::TraitObject &&
        e.vtable_index >= 0) {
        return gen_dyn_dispatch(e, ret_logos_type);
    }
    auto [ptr, tname] = gen_recv_struct(*e.receiver);
    if (!ptr || tname.empty()) return nullptr;
    // Direct call: mangled name = TypeName__method
    // If resolved_type is set (inherited method), use the defining class name.
    const std::string& defining = e.resolved_type.empty() ? tname : e.resolved_type;
    auto mangled    = defining + "__" + e.method;
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto callee_fn  = parent_mod.lookupSymbol<mlir::func::FuncOp>(mangled);
    if (!callee_fn) {
        std::fprintf(stderr, "mlir_gen: method '%s' not found\n", mangled.c_str());
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
                     ir->index->type->kind == LogosType::Kind::U64);
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
         e.index->type->kind == LogosType::Kind::U32 ||
         e.index->type->kind == LogosType::Kind::U64);
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
                  e.operand->type->kind == LogosType::Kind::U64));
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
        bool src_unsigned = e.operand->type &&
            (e.operand->type->kind == LogosType::Kind::U8  ||
             e.operand->type->kind == LogosType::Kind::U16 ||
             e.operand->type->kind == LogosType::Kind::U32 ||
             e.operand->type->kind == LogosType::Kind::U64);
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
             type->kind == LogosType::Kind::U64);
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
             e.operand->type->kind == LogosType::Kind::U64);
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

    bool has_wild = false;
    for (auto& arm : e.arms)
        if (std::holds_alternative<PatWild>(arm.pat)) { has_wild = true; break; }

    int last_tested = (int)e.arms.size() - 1;
    mlir::Block* else_block = merge_block;

    if (!has_wild && !e.arms.empty()) {
        auto& last_arm = e.arms.back();
        auto* last_body = new mlir::Block();
        region->push_back(last_body);
        {
            mlir::OpBuilder::InsertionGuard guard(builder_);
            builder_.setInsertionPointToStart(last_body);
            auto added = extract_arm_payload(last_arm);
            auto val = gen_expr(*last_arm.value);
            for (auto& n : added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
            if (val) {
                val = coerce_numeric(val, result_type);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
            }
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        }
        else_block = last_body;
        last_tested = (int)e.arms.size() - 2;
    }

    for (int i = last_tested; i >= 0; --i) {
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
         e.index->type->kind == LogosType::Kind::U32 ||
         e.index->type->kind == LogosType::Kind::U64);
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

        // Evaluate arg and widen to i64
        auto arg_val = gen_expr(*e.args[i]);
        if (!arg_val) return nullptr;
        mlir::Value as_i64;
        if (arg_val.getType() == ptr_type())
            as_i64 = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_type, arg_val);
        else
            as_i64 = coerce_int(arg_val, i64_type);

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
    auto elem_mlir = logos_to_mlir(e.elem_type);
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

} // namespace logos::compiler
