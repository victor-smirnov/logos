// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_stmt.cpp — Statement code generation.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_block(const LBlock& block) {
    for (auto& s : block.stmts) {
        if (is_terminated(builder_.getBlock())) break;
        gen_stmt(s);
    }
}

// ---------------------------------------------------------------------------
// Statement dispatch
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_stmt(const LStmt& stmt) {
    std::visit([&](auto& s) { gen_stmt_kind(s); }, stmt.kind);
}

void MLIRGenImpl::gen_stmt_kind(const SLet& s)        { gen_let(s); }
void MLIRGenImpl::gen_stmt_kind(const SAssign& s)      { gen_assign(s); }
void MLIRGenImpl::gen_stmt_kind(const SReturn& s)      { gen_return(s); }
void MLIRGenImpl::gen_stmt_kind(const SIf& s)          { gen_if(s); }
void MLIRGenImpl::gen_stmt_kind(const SWhile& s)       { gen_while(s); }
void MLIRGenImpl::gen_stmt_kind(const SFor& s)         { gen_for(s); }
void MLIRGenImpl::gen_stmt_kind(const SLoop& s)        { gen_loop(s); }
void MLIRGenImpl::gen_stmt_kind(const SBreak& s)       { gen_break(s); }
void MLIRGenImpl::gen_stmt_kind(const SContinue& s) {
    if (loop_stack_.empty()) return;
    if (s.label.empty()) { gen_continue(); return; }
    for (int i = (int)loop_stack_.size() - 1; i >= 0; --i) {
        if (loop_stack_[i].label == s.label) {
            builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_[i].cont);
            return;
        }
    }
    gen_continue(); // fallback
}
void MLIRGenImpl::gen_stmt_kind(const SFieldWrite& s)       { gen_field_write(s); }
void MLIRGenImpl::gen_stmt_kind(const STupleWrite& s)       { gen_tuple_write(s); }
void MLIRGenImpl::gen_stmt_kind(const SDerefFieldWrite& s)  { gen_deref_field_write(s); }
void MLIRGenImpl::gen_stmt_kind(const SIndexWrite& s)       { gen_index_write(s); }
void MLIRGenImpl::gen_stmt_kind(const SFieldIndexWrite& s)  { gen_field_index_write(s); }
void MLIRGenImpl::gen_stmt_kind(const SExprStmt& s)    { gen_expr(*s.expr); }
void MLIRGenImpl::gen_stmt_kind(const SMatch& s)       { gen_match(s); }
void MLIRGenImpl::gen_stmt_kind(const SDelete& s)      { gen_delete(s); }
void MLIRGenImpl::gen_stmt_kind(const SForEach& s)     { gen_for_each(s); }
void MLIRGenImpl::gen_stmt_kind(const SBlock& s)       { gen_block(*s.body); }

void MLIRGenImpl::gen_stmt_kind(const SDrop& s) {
    auto it = scope_.find(s.var_name);
    if (it == scope_.end()) return;
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // 1. Call user's explicit drop function (if any)
    if (!s.drop_fn.empty()) {
        auto fn = mod.lookupSymbol<mlir::func::FuncOp>(s.drop_fn);
        if (fn)
            builder_.create<mlir::func::CallOp>(loc_, fn, mlir::ValueRange{it->second});
    }

    // 2. Auto-drop droppable fields (reverse field order)
    if (s.drop_fields && s.type && s.type->kind == LogosType::Kind::Struct) {
        auto sit = struct_types_.find(s.type->struct_name);
        if (sit != struct_types_.end()) {
            auto& info = sit->second;
            for (int fi = (int)info.fields.size() - 1; fi >= 0; --fi) {
                auto& f = info.fields[fi];
                std::string field_drop = f.struct_name.empty()
                    ? std::string{} : f.struct_name + "__drop";
                if (field_drop.empty()) continue;
                auto field_fn = mod.lookupSymbol<mlir::func::FuncOp>(field_drop);
                if (!field_fn) continue;
                // GEP to field; pass pointer to the field data to drop.
                // Inline-embedded fields (LLVMStructType): GEP IS the pointer.
                // Pointer fields: load the pointer from the GEP first.
                auto field_gep = gep_field(it->second, info, f.name);
                if (!field_gep) continue;
                mlir::Value field_ptr;
                if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type)) {
                    field_ptr = field_gep;
                } else {
                    field_ptr = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, ptr_type(), field_gep);
                }
                builder_.create<mlir::func::CallOp>(loc_, field_fn, mlir::ValueRange{field_ptr});
            }
        }
    }
}

void MLIRGenImpl::gen_stmt_kind(const SDerefWrite& s) {
    auto ptr = gen_expr(*s.ptr);
    auto val = gen_expr(*s.value);
    if (!ptr || !val) return;
    // Determine element type from pointer's pointee type (handles both *T and &mut T)
    mlir::Type elem_type = nullptr;
    if (s.ptr->type && s.ptr->type->pointee &&
        (s.ptr->type->kind == LogosType::Kind::Ptr ||
         s.ptr->type->kind == LogosType::Kind::MutRef))
        elem_type = logos_to_mlir(s.ptr->type->pointee);
    if (!elem_type) elem_type = builder_.getI32Type();
    val = coerce_int(val, elem_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, ptr);
}

// ---------------------------------------------------------------------------
// gen_let
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_let(const SLet& s) {
    // ── Struct literal ────────────────────────────────────────
    if (std::holds_alternative<EStructLit>(s.value->kind)) {
        auto& lit = std::get<EStructLit>(s.value->kind);
        auto alloca = gen_struct_lit(lit);
        if (!alloca) return;
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_struct_[s.name] = s.type ? concrete_struct_name(s.type) : lit.name;
        return;
    }

    // ── Array literal ─────────────────────────────────────────
    if (std::holds_alternative<EArrLit>(s.value->kind)) {
        auto& lit = std::get<EArrLit>(s.value->kind);
        auto elem_type = logos_to_mlir(s.type->elem ? s.type->elem : nullptr);
        if (!elem_type) elem_type = builder_.getI32Type();
        auto alloca = gen_arr_lit(lit, elem_type);
        if (!alloca) return;
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_elem_types_[s.name] = elem_type;
        var_subscript_[s.name]  = elem_type;
        return;
    }

    // ── Tuple literal ────────────────────────────────────────
    if (std::holds_alternative<ETupleLit>(s.value->kind)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);
        return;
    }

    // ── Tuple value (from call or variable) ──────────────────
    // If the value is already a pointer (tuple literal, variable), use directly.
    // If the value is a struct by-value (from function return), store into alloca.
    if (s.type && s.type->kind == LogosType::Kind::Tuple) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        auto stype = tuple_llvm_type(s.type);
        if (stype && val.getType() != ptr_type()) {
            // By-value struct (e.g. from function call) — store into alloca.
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), stype, i64_one());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            val = alloca;
        }
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);
        return;
    }

    // ── Closure value ─────────────────────────────────────────
    if (s.type && s.type->kind == LogosType::Kind::Closure) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);  // return ptr directly
        return;
    }

    // ── FnPtr value (fn(T) -> R) ──────────────────────────────
    if (s.type && s.type->kind == LogosType::Kind::FnPtr) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        // Store as a let-bound scalar (alloca holding a ptr).
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), ptr_type(), i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
        scope_[s.name]          = alloca;
        let_vars_.insert(s.name);
        var_elem_types_[s.name] = ptr_type();
        return;
    }

    // ── Slice value ──────────────────────────────────────────
    if (s.type && s.type->kind == LogosType::Kind::Slice) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        // String literal → &str coercion: val is a ptr (from ELitStr global),
        // we need to wrap it in a fat pointer {ptr, len}.
        if (s.value->type && s.value->type->kind == LogosType::Kind::Ptr &&
            val.getType() == ptr_type() &&
            std::holds_alternative<ELitStr>(s.value->kind)) {
            auto& lit = std::get<ELitStr>(s.value->kind);
            // Compute string length (strip quotes, process escapes)
            std::string raw = lit.value;
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                raw = raw.substr(1, raw.size() - 2);
            // Count actual bytes (escape sequences are single bytes)
            size_t len = 0;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) ++i;
                ++len;
            }
            auto stype = slice_llvm_type();
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), stype, i64_one());
            llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
            auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, pp);
            llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
            auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
            auto len_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)len, 64);
            builder_.create<mlir::LLVM::StoreOp>(loc_, len_val, lp);
            val = alloca;
        }
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);
        return;
    }

    // ── Tagged enum value ────────────────────────────────────
    if (s.type && s.type->kind == LogosType::Kind::Enum) {
        auto* te = resolve_tagged_enum(s.type->enum_name, s.type);
        if (te) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            // If gen_expr returned a non-pointer value, create the tagged enum alloca.
            if (val.getType() != ptr_type()) {
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), te->llvm_type, i64_one());
                if (val.getType() == te->llvm_type) {
                    // Aggregate returned by value from a function — store whole struct.
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                } else {
                    // Plain i32 discriminant (e.g. Option::None with no type_args).
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), te->llvm_type, alloca, di);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
                }
                val = alloca;
            }
            if (s.is_mut) {
                // Mutable: wrap in pointer slot so assignments can rebind.
                auto ptr_slot = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), ptr_type(), i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, ptr_slot);
                scope_[s.name] = ptr_slot;
                var_tagged_enum_ptr_.insert(s.name);
            } else {
                scope_[s.name] = val;
            }
            let_vars_.insert(s.name);
            var_tagged_enum_.insert(s.name);
            return;
        }
    }

    // ── Struct value (from call or variable) ─────────────────
    // Structs are always held as pointers (alloca).
    // If the value is a by-value aggregate (e.g. returned from a function),
    // store it into a fresh alloca so the rest of the pipeline sees a pointer.
    if (s.type && (s.type->kind == LogosType::Kind::Struct ||
                    s.type->kind == LogosType::Kind::Datatype)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        if (val.getType() != ptr_type()) {
            // By-value struct from function return — spill to alloca.
            auto sname = concrete_struct_name(s.type);
            auto sit = struct_types_.find(sname);
            if (sit != struct_types_.end()) {
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), sit->second.llvm_type, i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                val = alloca;
            }
        }
        scope_[s.name]    = val;
        let_vars_.insert(s.name);
        var_struct_[s.name] = concrete_struct_name(s.type);
        return;
    }

    // ── Reference to struct (&Struct or &mut Struct) ─────────
    // The value is a pointer to the struct; register in var_struct_ for field access.
    if (s.type && (s.type->kind == LogosType::Kind::Ref ||
                   s.type->kind == LogosType::Kind::MutRef) &&
        s.type->pointee && (s.type->pointee->kind == LogosType::Kind::Struct ||
                            s.type->pointee->kind == LogosType::Kind::Datatype)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_struct_[s.name] = concrete_struct_name(s.type->pointee);
        return;
    }

    // ── &dyn Trait / Box<dyn Trait> coercion ─────────────────
    if (s.type && s.type->kind == LogosType::Kind::TraitObject) {
        auto data_ptr = gen_expr(*s.value);
        if (!data_ptr) return;
        mlir::Value alloca;
        if (s.value->type && s.value->type->kind == LogosType::Kind::TraitObject) {
            // RHS is already a fat pointer (e.g., returned from a Box<dyn T> function).
            // Use it directly — no need to rebuild the fat struct.
            alloca = data_ptr;
        } else {
            // Concrete type → build fat pointer from scratch.
            // For &dyn T from `new Foo {}`, value type is *mut Foo — strip the pointer.
            const LogosType* src_logos_type = s.value->type;
            if (src_logos_type && src_logos_type->kind == LogosType::Kind::Ptr &&
                src_logos_type->pointee)
                src_logos_type = src_logos_type->pointee;
            std::string src_type = type_str(src_logos_type);
            alloca = coerce_to_dyn(data_ptr, s.type->trait_name, src_type);
        }
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_dyn_trait_[s.name] = s.type->trait_name;
        return;
    }

    // ── Scalar ───────────────────────────────────────────────
    // Pre-allocate the slot BEFORE generating the RHS expression.
    // This ensures the AllocaOp is in the current block (entry-reachable)
    // even when the RHS is an if-expression that creates new blocks.
    auto var_type = logos_to_mlir(s.type);
    mlir::Value alloca;
    if (var_type) {
        alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), var_type, i64_one());
    }

    auto val = gen_expr(*s.value);
    if (!val) return;

    if (!var_type) {
        scope_[s.name] = val;
        return;
    }

    val = coerce_int(val, var_type);
    val = coerce_float(val, var_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
    scope_[s.name] = alloca;
    let_vars_.insert(s.name);
    // For array-typed variables (assigned from expressions, not array literals),
    // subscript_elem_type must return the element type (i32), NOT the array type
    // (!llvm.array<N x i32>). Setting var_elem_types_ to the array type causes
    // nested indexing like `row[j]` to generate GEPs with the wrong elem_type.
    if (s.type && s.type->kind == LogosType::Kind::Array && s.type->elem) {
        auto elem_mlir = logos_to_mlir(s.type->elem);
        if (!elem_mlir) elem_mlir = builder_.getI32Type();
        var_elem_types_[s.name] = elem_mlir;
        var_subscript_[s.name]  = elem_mlir;
    } else {
        var_elem_types_[s.name] = var_type;
    }
    // Track local pointer variables so indexing can load the ptr before GEP.
    if (s.type && s.type->kind == LogosType::Kind::Ptr && s.type->pointee) {
        const LogosType* pointee = s.type->pointee;
        if (pointee->kind == LogosType::Kind::Struct ||
            pointee->kind == LogosType::Kind::Datatype) {
            // logos_to_mlir(Struct/Datatype) == ptr_type(), which can't be matched
            // to a struct LLVM type in gen_field_write. Store the actual aggregate type
            // so the fallback path resolves the correct struct name.
            auto cname = concrete_struct_name(pointee);
            auto sit2 = struct_types_.find(cname);
            if (sit2 != struct_types_.end())
                var_local_ptrs_[s.name] = sit2->second.llvm_type;
        } else {
            auto pt = logos_to_mlir(pointee);
            if (pt) var_local_ptrs_[s.name] = pt;
        }
    }
}

// ---------------------------------------------------------------------------
// gen_assign
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_assign(const SAssign& s) {
    auto val = gen_expr(*s.value);
    if (!val) return;
    auto it = scope_.find(s.name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: assign to undefined '%s'\n", s.name.c_str());
        return;
    }
    // Mutable tagged enum: val is a new struct ptr; store to pointer slot.
    if (var_tagged_enum_ptr_.count(s.name)) {
        // If val is an aggregate (returned by value), spill to alloca first.
        val = spill_to_alloca(val);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
        return;
    }
    auto et = var_elem_types_.find(s.name);
    if (et != var_elem_types_.end())
        val = coerce_int(val, et->second);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
}

// ---------------------------------------------------------------------------
// gen_return
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_return(const SReturn& s) {
    if (s.value) {
        // Box<dyn Trait> / &dyn Trait return: coerce concrete type to heap fat pointer.
        if (cur_fn_ret_logos_type_ &&
            cur_fn_ret_logos_type_->kind == LogosType::Kind::TraitObject &&
            s.value->type &&
            s.value->type->kind != LogosType::Kind::TraitObject) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            const LogosType* src_lt = s.value->type;
            if (src_lt->kind == LogosType::Kind::Ptr && src_lt->pointee)
                src_lt = src_lt->pointee;
            auto vtable = build_inline_vtable(
                cur_fn_ret_logos_type_->trait_name, type_str(src_lt));
            // Heap-allocate the fat struct so it survives past this function's frame.
            auto size16 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 16LL, 64);
            auto fat_ptr = call_malloc(size16);
            auto dyn_struct = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), {ptr_type(), ptr_type()});
            llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
            builder_.create<mlir::LLVM::StoreOp>(loc_, val,
                builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), dyn_struct, fat_ptr, idx0));
            if (vtable) {
                llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
                builder_.create<mlir::LLVM::StoreOp>(loc_, vtable,
                    builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), dyn_struct, fat_ptr, idx1));
            }
            if (in_llvm_func_)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{fat_ptr});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fat_ptr});
            return;
        }

        auto val = gen_expr(*s.value);
        if (!val) return;
        // Tagged enum None returning i32 but function expects ptr:
        // wrap in alloca like gen_let does.
        if (cur_ret_type_ && cur_ret_type_ == ptr_type() && val.getType() != ptr_type()) {
            // Find the tagged enum info from the return value's LIR type
            if (s.value->type && s.value->type->kind == LogosType::Kind::Enum) {
                // The value is a discriminant — need to figure out the enum struct type.
                // Look through all registered tagged enums to find a matching one.
                // For now: create a generic {i32, [4 x i8]} wrapper.
                auto i32t = builder_.getI32Type();
                auto pad = mlir::LLVM::LLVMArrayType::get(builder_.getIntegerType(8), 4);
                auto wrap = mlir::LLVM::LLVMStructType::getLiteral(
                    builder_.getContext(), {i32t, pad});
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), wrap, i64_one());
                llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                auto dp = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), wrap, alloca, di);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
                val = alloca;
            }
        } else if (cur_ret_type_ && mlir::isa<mlir::LLVM::LLVMStructType>(cur_ret_type_)) {
            if (val.getType() == ptr_type()) {
                // val is a pointer (to struct/enum alloca) — load the aggregate.
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, val);
            } else {
                // val is a scalar (i32 discriminant) — wrap in a struct alloca and load.
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), cur_ret_type_, i64_one());
                auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), cur_ret_type_, alloca,
                    llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(0)});
                builder_.create<mlir::LLVM::StoreOp>(
                    loc_, coerce_int(val, builder_.getI32Type()), disc_ptr);
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, alloca);
            }
        }
        else if (cur_ret_type_)
            val = coerce_numeric(val, cur_ret_type_);
        if (in_llvm_func_)
            builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{val});
        else
            builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{val});
    } else {
        if (in_llvm_func_)
            builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
        else
            builder_.create<mlir::func::ReturnOp>(loc_);
    }
}

// ---------------------------------------------------------------------------
// gen_if
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_if(const SIf& s) {
    auto cond = gen_expr(*s.cond);
    if (!cond) return;

    auto* region      = builder_.getBlock()->getParent();
    auto* then_block  = new mlir::Block();
    auto* else_block  = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(then_block);
    region->push_back(else_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

    builder_.setInsertionPointToStart(then_block);
    gen_block(*s.then_);
    bool then_falls = !is_terminated(builder_.getBlock());
    if (then_falls) builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(else_block);
    if (s.else_) gen_block(**s.else_);
    bool else_falls = !is_terminated(builder_.getBlock());
    if (else_falls) builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    if (!then_falls && !else_falls) {
        merge_block->erase();
        return;
    }
    builder_.setInsertionPointToStart(merge_block);
}

// ---------------------------------------------------------------------------
// gen_while
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_while(const SWhile& s) {
    auto* region     = builder_.getBlock()->getParent();
    auto* cond_block = new mlir::Block();
    auto* body_block = new mlir::Block();
    auto* exit_block = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(exit_block);

    builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
    builder_.setInsertionPointToStart(cond_block);
    auto cond = gen_expr(*s.cond);
    if (!cond) return;
    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

    builder_.setInsertionPointToStart(body_block);
    loop_stack_.push_back({cond_block, exit_block, {}, s.label});
    gen_block(*s.body);
    loop_stack_.pop_back();
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

    builder_.setInsertionPointToStart(exit_block);
}

// ---------------------------------------------------------------------------
// gen_for
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_for(const SFor& s) {
    auto lo = gen_expr(*s.lo);
    auto hi = gen_expr(*s.hi);
    if (!lo || !hi) return;

    // Use the wider of lo/hi types so i64 bounds aren't truncated to i32.
    mlir::Type loop_type = builder_.getI32Type();
    if (auto hi_int = mlir::dyn_cast<mlir::IntegerType>(hi.getType()))
        if (hi_int.getWidth() > 32) loop_type = hi.getType();
    if (auto lo_int = mlir::dyn_cast<mlir::IntegerType>(lo.getType()))
        if (lo_int.getWidth() > mlir::cast<mlir::IntegerType>(loop_type).getWidth())
            loop_type = lo.getType();

    auto i_alloca = builder_.create<mlir::LLVM::AllocaOp>(
                        loc_, ptr_type(), loop_type, i64_one());
    bool lo_unsigned = s.lo->type &&
        (s.lo->type->kind == LogosType::Kind::U8  ||
         s.lo->type->kind == LogosType::Kind::U16 ||
         s.lo->type->kind == LogosType::Kind::U32 ||
         s.lo->type->kind == LogosType::Kind::U56 ||
         s.lo->type->kind == LogosType::Kind::U64 ||
         s.lo->type->kind == LogosType::Kind::U128);
    mlir::Value lo_coerced;
    if (lo_unsigned && lo.getType() != loop_type)
        lo_coerced = builder_.create<mlir::arith::ExtUIOp>(loc_, loop_type, lo);
    else
        lo_coerced = coerce_int(lo, loop_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, lo_coerced, i_alloca);
    scope_[s.var] = i_alloca;
    let_vars_.insert(s.var);
    var_elem_types_[s.var] = loop_type;

    auto* region     = builder_.getBlock()->getParent();
    auto* cond_block = new mlir::Block();
    auto* body_block = new mlir::Block();
    auto* incr_block = new mlir::Block();   // increment i, then back to cond
    auto* exit_block = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(incr_block);
    region->push_back(exit_block);

    builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

    builder_.setInsertionPointToStart(cond_block);
    auto i_val  = builder_.create<mlir::LLVM::LoadOp>(loc_, loop_type, i_alloca);
    bool hi_unsigned = s.hi->type &&
        (s.hi->type->kind == LogosType::Kind::U8  ||
         s.hi->type->kind == LogosType::Kind::U16 ||
         s.hi->type->kind == LogosType::Kind::U32 ||
         s.hi->type->kind == LogosType::Kind::U56 ||
         s.hi->type->kind == LogosType::Kind::U64 ||
         s.hi->type->kind == LogosType::Kind::U128);
    mlir::Value hi_val;
    if (hi_unsigned && hi.getType() != loop_type)
        hi_val = builder_.create<mlir::arith::ExtUIOp>(loc_, loop_type, hi);
    else
        hi_val = coerce_int(hi, loop_type);
    mlir::Value cond;
    if (s.inclusive)
        cond = builder_.create<mlir::arith::CmpIOp>(loc_,
            hi_unsigned ? mlir::arith::CmpIPredicate::ule
                        : mlir::arith::CmpIPredicate::sle,
            i_val, hi_val);
    else
        cond = builder_.create<mlir::arith::CmpIOp>(loc_,
            hi_unsigned ? mlir::arith::CmpIPredicate::ult
                        : mlir::arith::CmpIPredicate::slt,
            i_val, hi_val);
    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

    builder_.setInsertionPointToStart(body_block);
    // continue → incr_block (so that i is incremented before re-checking)
    loop_stack_.push_back({incr_block, exit_block, {}, s.label});
    gen_block(*s.body);
    loop_stack_.pop_back();
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, incr_block);

    // Increment block: i += 1, branch back to condition.
    // If no predecessor (body always terminates, no continue), mark unreachable.
    builder_.setInsertionPointToStart(incr_block);
    if (incr_block->hasNoPredecessors()) {
        builder_.create<mlir::LLVM::UnreachableOp>(loc_);
    } else {
        auto i_cur  = builder_.create<mlir::LLVM::LoadOp>(loc_, loop_type, i_alloca);
        auto one    = builder_.create<mlir::arith::ConstantIntOp>(
                          loc_, 1, mlir::cast<mlir::IntegerType>(loop_type).getWidth());
        auto i_next = builder_.create<mlir::arith::AddIOp>(loc_, i_cur, one);
        builder_.create<mlir::LLVM::StoreOp>(loc_, i_next, i_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
    }

    builder_.setInsertionPointToStart(exit_block);
    scope_.erase(s.var);
    let_vars_.erase(s.var);
    var_elem_types_.erase(s.var);
}

// ---------------------------------------------------------------------------
// gen_loop
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_loop(const SLoop& s) {
    auto* region     = builder_.getBlock()->getParent();
    auto* loop_block = new mlir::Block();
    auto* exit_block = new mlir::Block();
    region->push_back(loop_block);
    region->push_back(exit_block);

    // Allocate break-value slot before the loop block, if needed.
    mlir::Value break_slot;
    if (!s.break_slot.empty() && s.result_type) {
        mlir::Type slot_ty = logos_to_mlir(s.result_type);
        if (slot_ty) {
            auto ptr_ty = mlir::LLVM::LLVMPointerType::get(builder_.getContext());
            break_slot  = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_ty, slot_ty, builder_.create<mlir::LLVM::ConstantOp>(
                    loc_, builder_.getI64Type(), builder_.getI64IntegerAttr(1)));
            scope_[s.break_slot]          = break_slot;
            let_vars_.insert(s.break_slot);
            var_elem_types_[s.break_slot] = slot_ty;
        }
    }

    builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

    builder_.setInsertionPointToStart(loop_block);
    loop_stack_.push_back({loop_block, exit_block, break_slot, s.label});
    gen_block(*s.body);
    loop_stack_.pop_back();
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

    builder_.setInsertionPointToStart(exit_block);
}

// ---------------------------------------------------------------------------
// gen_break / gen_continue
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_break(const SBreak& s) {
    if (loop_stack_.empty()) return;
    // Find target loop: if label specified, search from innermost outward.
    LoopBlocks* target = nullptr;
    if (s.label.empty()) {
        target = &loop_stack_.back();
    } else {
        for (int i = (int)loop_stack_.size() - 1; i >= 0; --i) {
            if (loop_stack_[i].label == s.label) { target = &loop_stack_[i]; break; }
        }
        if (!target) { target = &loop_stack_.back(); }
    }
    // Store break value into the slot if present.
    if (s.value && target->break_slot) {
        mlir::Value val = gen_expr(*s.value);
        if (val)
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, target->break_slot);
    }
    builder_.create<mlir::cf::BranchOp>(loc_, target->exit);
}

void MLIRGenImpl::gen_continue() {
    if (loop_stack_.empty()) return;
    builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().cont);
}

// ---------------------------------------------------------------------------
// gen_for_each
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_for_each(const SForEach& s) {
    // Evaluate the iter (array/slice) expression.
    mlir::Type elem_mlir = logos_to_mlir(s.elem_type);
    if (!elem_mlir) return;

    auto arr_alloca = gen_expr(*s.iter);
    if (!arr_alloca) return;

    // ── Slice path: &[T] — load data_ptr and len from fat pointer ──
    if (s.is_slice) {
        auto stype = slice_llvm_type();
        // Load data_ptr from field 0
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, arr_alloca, pi);
        auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        // Load len from field 1
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, arr_alloca, li);
        auto len_val = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);

        // i64 index
        auto i_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), builder_.getI64Type(), i64_one());
        auto zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0LL, 64);
        builder_.create<mlir::LLVM::StoreOp>(loc_, zero64, i_alloca);

        auto* region     = builder_.getBlock()->getParent();
        auto* cond_block = new mlir::Block();
        auto* body_block = new mlir::Block();
        auto* incr_block = new mlir::Block();
        auto* exit_block = new mlir::Block();
        region->push_back(cond_block);
        region->push_back(body_block);
        region->push_back(incr_block);
        region->push_back(exit_block);

        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

        builder_.setInsertionPointToStart(cond_block);
        auto i_val = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), i_alloca);
        auto cond  = builder_.create<mlir::arith::CmpIOp>(
            loc_, mlir::arith::CmpIPredicate::slt, i_val, len_val);
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

        builder_.setInsertionPointToStart(body_block);
        mlir::Value i_cur = builder_.create<mlir::LLVM::LoadOp>(
            loc_, builder_.getI64Type(), i_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx;
        arr_idx.push_back(mlir::LLVM::GEPArg(i_cur));
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_mlir, data_ptr, arr_idx);

        auto elem_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), elem_mlir, i64_one());
        auto elem_val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, elem_ptr);
        builder_.create<mlir::LLVM::StoreOp>(loc_, elem_val, elem_alloca);
        scope_[s.var]          = elem_alloca;
        var_elem_types_[s.var] = elem_mlir;
        let_vars_.insert(s.var);

        loop_stack_.push_back({incr_block, exit_block, {}, {}});
        gen_block(*s.body);
        loop_stack_.pop_back();

        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, incr_block);

        builder_.setInsertionPointToStart(incr_block);
        if (incr_block->hasNoPredecessors()) {
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        } else {
            auto i_inc = builder_.create<mlir::LLVM::LoadOp>(
                loc_, builder_.getI64Type(), i_alloca);
            auto one64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1LL, 64);
            auto i_next = builder_.create<mlir::arith::AddIOp>(loc_, i_inc, one64);
            builder_.create<mlir::LLVM::StoreOp>(loc_, i_next, i_alloca);
            builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
        }

        builder_.setInsertionPointToStart(exit_block);
        scope_.erase(s.var);
        let_vars_.erase(s.var);
        var_elem_types_.erase(s.var);
        return;
    }

    // ── Array path (static size) ──────────────────────────────────
    // Alloca for the index
    auto i_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), builder_.getI32Type(), i64_one());
    auto zero32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    builder_.create<mlir::LLVM::StoreOp>(loc_, zero32, i_alloca);

    auto hi_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, s.arr_size, 32);

    auto* region     = builder_.getBlock()->getParent();
    auto* cond_block = new mlir::Block();
    auto* body_block = new mlir::Block();
    auto* exit_block = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(exit_block);

    builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

    builder_.setInsertionPointToStart(cond_block);
    auto i_val = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
    auto cond  = builder_.create<mlir::arith::CmpIOp>(
        loc_, mlir::arith::CmpIPredicate::slt, i_val, hi_val);
    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

    builder_.setInsertionPointToStart(body_block);
    // Load arr[i]: GEP to element, then load.
    mlir::Value i_cur = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
    llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx{i_cur};
    auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_mlir, arr_alloca, arr_idx);

    bool is_struct_elem = s.elem_type &&
        s.elem_type->kind == LogosType::Kind::Struct;

    if (is_struct_elem) {
        // Struct elements are stored as pointers in the array ([N x ptr]).
        // Load the pointer — it IS the struct pointer, matching the struct convention
        // (scope_ holds a direct struct pointer for struct variables).
        auto struct_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), elem_ptr);
        scope_[s.var] = struct_ptr;
        var_struct_[s.var] = concrete_struct_name(s.elem_type);
    } else {
        // Scalar: alloca + store so the body can read (and mutate) via scope_.
        auto elem_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), elem_mlir, i64_one());
        auto elem_val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, elem_ptr);
        builder_.create<mlir::LLVM::StoreOp>(loc_, elem_val, elem_alloca);
        scope_[s.var]          = elem_alloca;
        var_elem_types_[s.var] = elem_mlir;
    }
    let_vars_.insert(s.var);

    // Create a separate increment block so that `continue` increments i first.
    auto* incr_block = new mlir::Block();
    region->push_back(incr_block);

    loop_stack_.push_back({incr_block, exit_block, {}, {}});
    gen_block(*s.body);
    loop_stack_.pop_back();

    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, incr_block);

    // Increment block: i += 1, reload element, branch back to condition.
    // If no predecessor (body always terminates, no continue), mark unreachable.
    builder_.setInsertionPointToStart(incr_block);
    if (incr_block->hasNoPredecessors()) {
        builder_.create<mlir::LLVM::UnreachableOp>(loc_);
    } else {
        auto i_inc = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
        auto one32 = builder_.create<mlir::arith::ConstantOp>(
            loc_, builder_.getI32Type(), builder_.getI32IntegerAttr(1));
        auto i_next = builder_.create<mlir::arith::AddIOp>(loc_, i_inc, one32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, i_next, i_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
    }

    builder_.setInsertionPointToStart(exit_block);
    scope_.erase(s.var);
    let_vars_.erase(s.var);
    var_elem_types_.erase(s.var);
    var_struct_.erase(s.var);
    var_class_.erase(s.var);
}

// ---------------------------------------------------------------------------
// gen_field_write / gen_deref_field_write / gen_tuple_write
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_field_write(const SFieldWrite& s) {
    mlir::Value ptr;
    std::string type_name;

    // Check if receiver is a direct struct/class var.
    auto sit = var_struct_.find(s.receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(s.receiver) : var_class_.end();
    if (sit != var_struct_.end()) {
        ptr = get_struct_ptr(s.receiver);
        type_name = sit->second;
    } else if (cit != var_class_.end()) {
        ptr = get_struct_ptr(s.receiver);
        type_name = cit->second;
    } else {
        // May be a pointer-to-struct variable (e.g. *mut Point or &mut Point).
        // var_local_ptrs_ stores the pointee MLIR type for raw-pointer locals.
        // Match it against known struct LLVM types to recover the struct name.
        auto sc = scope_.find(s.receiver);
        if (sc != scope_.end()) {
            auto lpit = var_local_ptrs_.find(s.receiver);
            if (lpit != var_local_ptrs_.end()) {
                // lpit->second is the MLIR type of the pointee (e.g. !llvm.struct<"Point",...>).
                for (auto& [sn, si] : struct_types_) {
                    if (si.llvm_type == lpit->second) {
                        type_name = sn;
                        break;
                    }
                }
            }
            if (type_name.empty()) {
                // Fallback: match by field name across all known structs.
                for (auto& [sn, si] : struct_types_) {
                    for (auto& f : si.fields)
                        if (f.name == s.field) { type_name = sn; break; }
                    if (!type_name.empty()) break;
                }
            }
            if (!type_name.empty()) {
                // The alloca holds the pointer value; load it to get the struct ptr.
                ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), sc->second);
            }
        }
        if (!ptr || type_name.empty()) {
            std::fprintf(stderr, "mlir_gen: field write: '%s' is not a struct/class\n",
                         s.receiver.c_str());
            return;
        }
    }
    auto& info = struct_types_[type_name];
    auto gep = gep_field(ptr, info, s.field);
    if (!gep) return;
    auto val = gen_expr(*s.value);
    if (!val) return;
    for (auto& f : info.fields) {
        if (f.name == s.field) {
            if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type) &&
                val.getType() == ptr_type()) {
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, val);
            } else {
                val = coerce_int(val, f.type);
            }
            break;
        }
    }
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

void MLIRGenImpl::gen_deref_field_write(const SDerefFieldWrite& s) {
    auto it = scope_.find(s.receiver);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: deref-field-write: undefined '%s'\n", s.receiver.c_str());
        return;
    }
    // Mutable class pointer vars store an alloca(ptr); load to get the actual object ptr.
    // Immutable class pointer vars store the raw ptr directly.
    mlir::Value ptr;
    if (var_elem_types_.count(s.receiver)) {
        ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
    } else {
        ptr = it->second;
    }
    auto sit = struct_types_.find(s.type_name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: deref-field-write: unknown type '%s'\n", s.type_name.c_str());
        return;
    }
    auto& info = sit->second;
    auto gep = gep_field(ptr, info, s.field);
    if (!gep) return;
    auto val = gen_expr(*s.value);
    if (!val) return;
    for (auto& f : info.fields) {
        if (f.name == s.field) {
            // If field is an inline struct and val is a pointer (from struct literal/alloca),
            // load the aggregate value before storing.
            if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type) &&
                val.getType() == ptr_type()) {
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, val);
            } else {
                val = coerce_int(val, f.type);
            }
            break;
        }
    }
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

void MLIRGenImpl::gen_tuple_write(const STupleWrite& s) {
    // var.N = value;  — tuple field write via GEP + store
    auto it = scope_.find(s.receiver);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: tuple write: undefined '%s'\n", s.receiver.c_str());
        return;
    }
    // Get the LLVM struct type for the tuple from the LIR receiver type.
    auto stype = tuple_llvm_type(s.recv_type);
    if (!stype) {
        std::fprintf(stderr, "mlir_gen: tuple write: cannot derive tuple type for '%s'\n",
                     s.receiver.c_str());
        return;
    }
    mlir::Value base_ptr = it->second;
    auto val = gen_expr(*s.value);
    if (!val) return;
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(s.index)};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, base_ptr, idx);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

// ---------------------------------------------------------------------------
// gen_index_write / gen_field_index_write
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_index_write(const SIndexWrite& s) {
    auto it = scope_.find(s.arr);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: index write: undefined '%s'\n", s.arr.c_str());
        return;
    }
    // Local pointer variables: scope_ holds an alloca(ptr); load the actual ptr first.
    mlir::Value base_ptr;
    mlir::Type  elem_type;
    auto lpit = var_local_ptrs_.find(s.arr);
    if (lpit != var_local_ptrs_.end()) {
        base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
        elem_type = lpit->second;
    } else {
        base_ptr  = it->second;
        elem_type = subscript_elem_type(s.arr);
    }

    auto idx = gen_expr(*s.index);
    auto val = gen_expr(*s.value);
    if (!idx || !val) return;
    val = coerce_int(val, elem_type);

    // Zero-extend unsigned index types so u8(200) doesn't become i8(-56) in GEP.
    bool idx_unsigned = s.index->type &&
        (s.index->type->kind == LogosType::Kind::U8  ||
         s.index->type->kind == LogosType::Kind::U16 ||
         s.index->type->kind == LogosType::Kind::U32 ||
         s.index->type->kind == LogosType::Kind::U56 ||
         s.index->type->kind == LogosType::Kind::U64 ||
         s.index->type->kind == LogosType::Kind::U128);
    if (idx_unsigned && idx.getType() != builder_.getI64Type())
        idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
    llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, base_ptr, indices);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

void MLIRGenImpl::gen_field_index_write(const SFieldIndexWrite& s) {
    // Get pointer to the struct/class.
    auto struct_ptr = get_struct_ptr(s.receiver);
    if (!struct_ptr) return;

    // Get struct type info to find the field.
    auto sit = var_struct_.find(s.receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(s.receiver) : var_class_.end();
    if (sit == var_struct_.end() && cit == var_class_.end()) {
        std::fprintf(stderr, "mlir_gen: field index write: '%s' not struct/class\n",
                     s.receiver.c_str());
        return;
    }
    const std::string& type_name = (sit != var_struct_.end()) ? sit->second : cit->second;
    auto& info = struct_types_[type_name];

    // GEP to the field.
    auto field_gep = gep_field(struct_ptr, info, s.field);
    if (!field_gep) return;

    // Determine element type and base pointer.
    mlir::Type field_mlir_type = builder_.getI32Type();  // dummy default
    bool       is_array_field  = false;
    for (auto& f : info.fields) {
        if (f.name == s.field) {
            field_mlir_type = f.type;
            is_array_field  = mlir::isa<mlir::LLVM::LLVMArrayType>(f.type);
            break;
        }
    }

    mlir::Type val_type = s.value->type ? logos_to_mlir(s.value->type) : builder_.getI32Type();
    if (!val_type) val_type = builder_.getI32Type();

    auto idx = gen_expr(*s.index);
    auto val = gen_expr(*s.value);
    if (!idx || !val) return;
    val = coerce_int(val, val_type);

    // Zero-extend unsigned index types; coerce_int sign-extends, which is wrong for u8/u16/u32/u64.
    bool idx_unsigned = s.index->type &&
        (s.index->type->kind == LogosType::Kind::U8  ||
         s.index->type->kind == LogosType::Kind::U16 ||
         s.index->type->kind == LogosType::Kind::U32 ||
         s.index->type->kind == LogosType::Kind::U56 ||
         s.index->type->kind == LogosType::Kind::U64 ||
         s.index->type->kind == LogosType::Kind::U128);
    auto extend_idx = [&](mlir::Type to) -> mlir::Value {
        if (idx.getType() == to) return idx;
        auto fi = mlir::dyn_cast<mlir::IntegerType>(idx.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!fi || !ti) return coerce_int(idx, to);
        if (ti.getWidth() > fi.getWidth())
            return idx_unsigned
                ? builder_.create<mlir::arith::ExtUIOp>(loc_, to, idx).getResult()
                : builder_.create<mlir::arith::ExtSIOp>(loc_, to, idx).getResult();
        return builder_.create<mlir::arith::TruncIOp>(loc_, to, idx);
    };

    mlir::Value base_ptr;
    if (is_array_field) {
        // field_gep points to the array — index directly into it.
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx;
        arr_idx.push_back(mlir::LLVM::GEPArg(int32_t(0)));
        arr_idx.push_back(mlir::LLVM::GEPArg(extend_idx(builder_.getIntegerType(32))));
        base_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), field_mlir_type, field_gep, arr_idx);
    } else {
        // Pointer field: load the stored pointer, then GEP to element.
        auto field_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_gep);
        llvm::SmallVector<mlir::LLVM::GEPArg> ptr_idx{extend_idx(builder_.getIntegerType(32))};
        base_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), val_type, field_ptr, ptr_idx);
    }
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, base_ptr);
}

// ---------------------------------------------------------------------------
// gen_match
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_match(const SMatch& s) {
    auto* region      = builder_.getBlock()->getParent();
    auto* merge_block = new mlir::Block();

    auto scrut = gen_expr(*s.scrut);
    if (!scrut) {
        region->push_back(merge_block);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return;
    }

    // Detect tagged enum: scrut is a pointer, load discriminant.
    mlir::Value scrut_ptr = nullptr;  // non-null for tagged enums
    const TaggedEnumInfo* te_info = nullptr;
    if (s.scrut->type && s.scrut->type->kind == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(s.scrut->type->enum_name, s.scrut->type);
        if (te_info) {
            // If scrut is an aggregate (returned by value from a function),
            // spill it to an alloca so GEP works below.
            if (scrut.getType() != ptr_type()) {
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), te_info->llvm_type, i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, alloca);
                scrut = alloca;
            }
            scrut_ptr = scrut;  // pointer to enum struct
            llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
            auto dp = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), te_info->llvm_type, scrut_ptr, di);
            scrut = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
        }
    }
    // Keep scrut at its natural type; coerce disc constants to match it.
    mlir::Type scrut_type = scrut.getType();

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    // Helper: is this pattern irrefutable (always matches)?
    // PatAt is irrefutable only if its sub-pattern is (e.g. n @ _ is irrefutable,
    // n @ 42 is refutable).
    std::function<bool(const lir::Pattern&)> is_irrefutable;
    is_irrefutable = [&](const lir::Pattern& p) -> bool {
        if (std::holds_alternative<lir::PatWild>(p))     return true;
        if (std::holds_alternative<lir::PatTuple>(p))    return true;
        if (std::holds_alternative<lir::PatStruct>(p))   return true;
        if (std::holds_alternative<lir::PatSlice>(p))    return true;
        if (std::holds_alternative<lir::PatRefBind>(p))  return true;
        if (auto* pa = std::get_if<lir::PatAt>(&p))
            return pa->sub.empty() || is_irrefutable(pa->sub[0]);
        if (auto* prp = std::get_if<lir::PatRefPat>(&p))
            return prp->inner.empty() || is_irrefutable(prp->inner[0]);
        return false;
    };
    if (s.scrut->type && s.scrut->type->kind == LogosType::Kind::Tuple) {
        // Tuple patterns are always irrefutable.
        for (auto& arm : s.arms) {
            if (arm.guard) continue;
            if (is_irrefutable(arm.pat)) { exhaustive_discrete = true; break; }
        }
    } else if (s.scrut->type && s.scrut->type->kind == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (auto& arm : s.arms) {
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
    } else if (s.scrut->type && s.scrut->type->kind == LogosType::Kind::Enum) {
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](const lir::Pattern& p) {
            if (auto* pv  = std::get_if<lir::PatVariant>(&p))     covered.insert(pv->disc);
            else if (auto* pvd = std::get_if<lir::PatVariantData>(&p)) covered.insert(pvd->disc);
        };
        for (auto& arm : s.arms) {
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
            auto eit = enum_types_.find(s.scrut->type->enum_name);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(s.scrut->type->enum_name, s.scrut->type)) {
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

    // Helper: extract payload bindings into scope for the current arm.
    auto extract_payload = [&](const LMatchArm& arm) {
        // ── PatTuple ───────────────────────────────────────────────────────
        if (auto* pt = std::get_if<PatTuple>(&arm.pat)) {
            auto ttype = tuple_llvm_type(s.scrut->type);
            if (!ttype) return;
            mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*s.scrut);
            if (!tptr) return;
            for (size_t bi = 0; bi < pt->bindings.size() && bi < pt->binding_types.size(); ++bi) {
                if (pt->bindings[bi] == "_") continue;
                auto elem_mlir = logos_to_mlir(pt->binding_types[bi]);
                if (!elem_mlir) continue;
                llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, tptr, fi);
                auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), elem_mlir, i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                scope_[pt->bindings[bi]] = alloca;
                let_vars_.insert(pt->bindings[bi]);
                var_elem_types_[pt->bindings[bi]] = elem_mlir;
            }
            return;
        }
        // ── PatVariantData ────────────────────────────────────────────────
        if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) {
            if (te_info && scrut_ptr) {
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& v : te_info->variants)
                    if (v.disc == pvd->disc) { vp = &v; break; }
                if (vp && !pvd->bindings.empty()) {
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
                    }
                }
            }
            return;
        }
        // ── PatStruct: GEP-extract each named field ───────────────────────
        if (auto* ps = std::get_if<PatStruct>(&arm.pat)) {
            // Use struct_types_ for the concrete LLVM struct type (logos_to_mlir returns ptr).
            auto sit = struct_types_.find(ps->struct_name);
            if (sit == struct_types_.end()) return;
            const StructInfo& sinfo = sit->second;
            mlir::Value sptr = scrut_ptr ? scrut_ptr : gen_expr(*s.scrut);
            if (!sptr) return;
            for (auto& pfb : ps->fields) {
                if (pfb.sub.empty()) {
                    // Shorthand binding: find field by name and bind.
                    auto fp = gep_field(sptr, sinfo, pfb.field_name);
                    if (!fp) continue;
                    // Determine field MLIR type from StructInfo.
                    mlir::Type fmlir;
                    for (auto& sf : sinfo.fields) {
                        if (sf.name == pfb.field_name) { fmlir = sf.type; break; }
                    }
                    if (!fmlir) continue;
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, fmlir, fp);
                    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                        loc_, ptr_type(), fmlir, i64_one());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                    scope_[pfb.field_name] = alloca;
                    let_vars_.insert(pfb.field_name);
                    var_elem_types_[pfb.field_name] = fmlir;
                }
                // Nested sub-patterns in struct fields are irrefutable (handled by
                // bind_pattern during sema); complex nested patterns are deferred.
            }
            return;
        }
        // ── PatSlice: GEP-extract indexed elements ────────────────────────
        if (auto* psl = std::get_if<PatSlice>(&arm.pat)) {
            auto* atype = s.scrut->type;
            if (atype && atype->kind == LogosType::Kind::Array && atype->elem) {
                auto elem_mlir = logos_to_mlir(atype->elem);
                auto arr_mlir  = logos_to_mlir(atype);
                mlir::Value aptr = scrut_ptr ? scrut_ptr : gen_expr(*s.scrut);
                if (aptr && elem_mlir && arr_mlir) {
                    auto bind_elem = [&](const lir::Pattern& p, int32_t idx) {
                        if (auto* pw = std::get_if<lir::PatWild>(&p)) {
                            if (pw->name == "_") return;
                            llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                            auto ep = builder_.create<mlir::LLVM::GEPOp>(
                                loc_, ptr_type(), arr_mlir, aptr, gi);
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                                loc_, ptr_type(), elem_mlir, i64_one());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                            scope_[pw->name] = alloca;
                            let_vars_.insert(pw->name);
                            var_elem_types_[pw->name] = elem_mlir;
                        }
                    };
                    for (size_t i = 0; i < psl->prefix.size(); ++i)
                        bind_elem(psl->prefix[i], (int32_t)i);
                    size_t total = (size_t)atype->arr_size;
                    for (size_t i = 0; i < psl->suffix.size(); ++i)
                        bind_elem(psl->suffix[i], (int32_t)(total - psl->suffix.size() + i));
                }
            }
            return;
        }
        // ── PatAt: bind outer name then handle sub-pattern ────────────────
        if (auto* pa = std::get_if<PatAt>(&arm.pat)) {
            mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
            if (!pa->name.empty() && pa->name != "_") {
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), sv.getType(), i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[pa->name] = alloca;
                let_vars_.insert(pa->name);
                var_elem_types_[pa->name] = sv.getType();
            }
            return;
        }
        // ── PatRefBind: bind name as a reference (pointer to scrutinee) ──
        if (auto* prb = std::get_if<PatRefBind>(&arm.pat)) {
            if (!prb->name.empty() && prb->name != "_") {
                // We need a pointer to the scrutinee. If scrut_ptr is available
                // (enum scrutinee on stack), use it directly. Otherwise spill the
                // value to a fresh alloca to obtain an address.
                mlir::Value sv_ptr;
                if (scrut_ptr) {
                    sv_ptr = scrut_ptr;
                } else {
                    // Spill: alloca for the scrutinee value.
                    auto tmp = builder_.create<mlir::LLVM::AllocaOp>(
                        loc_, ptr_type(), scrut.getType(), i64_one());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    sv_ptr = tmp;
                }
                // n: &T → alloca(ptr) holding the address of the scrutinee.
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), ptr_type(), i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv_ptr, alloca);
                scope_[prb->name] = alloca;
                let_vars_.insert(prb->name);
                var_elem_types_[prb->name] = ptr_type();
            }
            return;
        }
        // ── PatWild (named wildcard) ───────────────────────────────────────
        if (auto* pw = std::get_if<PatWild>(&arm.pat)) {
            if (pw->name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), sv.getType(), i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[pw->name] = alloca;
                let_vars_.insert(pw->name);
                var_elem_types_[pw->name] = sv.getType();
            }
        }
    };

    // Build if-else chain from last arm down to first.
    for (int i = (int)s.arms.size() - 1; i >= 0; --i) {
        auto& arm = s.arms[i];
        auto* body_block = new mlir::Block();
        region->push_back(body_block);

        mlir::Block* arm_entry = body_block;

        if (arm.guard) {
            // guard_block: extract bindings, evaluate guard, branch accordingly.
            auto* guard_block = new mlir::Block();
            region->push_back(guard_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(guard_block);
                extract_payload(arm);
                auto gval = gen_expr(**arm.guard);
                gval = coerce_int(gval, builder_.getI1Type());
                builder_.create<mlir::cf::CondBranchOp>(loc_, gval, body_block, else_block);
            }
            arm_entry = guard_block;
            // body_block: bindings already in scope from guard_block.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                gen_block(*arm.body);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        } else {
            // No guard: extract bindings and run body in body_block.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                extract_payload(arm);
                gen_block(*arm.body);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        }

        bool is_wild = is_irrefutable(arm.pat);
        if (is_wild) {
            else_block = arm_entry;
        } else if (auto* pr = std::get_if<lir::PatRange>(&arm.pat)) {
            // Range pattern: lo <= scrut && scrut <= hi
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto lo_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->lo, 64), scrut_type);
                auto hi_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->hi, 64), scrut_type);
                auto ge = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::sge, scrut, lo_val);
                auto le = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::sle, scrut, hi_val);
                auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
            // OR pattern: chain of comparisons — any match goes to arm_entry.
            // Build right-to-left so each test falls through to the next.
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
        } else if (auto* pa = std::get_if<lir::PatAt>(&arm.pat)) {
            // PatAt with refutable sub-pattern: dispatch on sub-pattern.
            if (!pa->sub.empty()) {
                const lir::Pattern& sub = pa->sub[0];
                if (auto* pr = std::get_if<lir::PatRange>(&sub)) {
                    auto* test_block = new mlir::Block();
                    region->push_back(test_block);
                    {
                        mlir::OpBuilder::InsertionGuard ig(builder_);
                        builder_.setInsertionPointToStart(test_block);
                        auto lo_val = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->lo, 64), scrut_type);
                        auto hi_val = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->hi, 64), scrut_type);
                        auto ge = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::sge, scrut, lo_val);
                        auto le = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::sle, scrut, hi_val);
                        auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                        builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
                    }
                    else_block = test_block;
                } else {
                    // Scalar sub-pattern: int, bool, variant.
                    int64_t disc = 0;
                    if (auto* pi = std::get_if<lir::PatInt>(&sub))  disc = pi->value;
                    else if (auto* pb = std::get_if<lir::PatBool>(&sub)) disc = pb->value ? 1 : 0;
                    auto* test_block = new mlir::Block();
                    region->push_back(test_block);
                    {
                        mlir::OpBuilder::InsertionGuard ig(builder_);
                        builder_.setInsertionPointToStart(test_block);
                        auto disc_val = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64), scrut_type);
                        auto eq = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                        builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
                    }
                    else_block = test_block;
                }
            } else {
                // Irrefutable PatAt (no sub) — arm always runs.
                else_block = arm_entry;
            }
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
    if (merge_block->hasNoPredecessors()) {
        merge_block->erase();
        return;
    }
    builder_.setInsertionPointToStart(merge_block);
}

// ---------------------------------------------------------------------------
// gen_delete
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_delete(const SDelete& s) {
    auto ptr = gen_expr(*s.expr);
    if (!ptr) return;
    // Call Drop before free (if the class/struct has a drop function)
    if (s.expr->type && s.expr->type->kind == LogosType::Kind::Ptr &&
        s.expr->type->pointee) {
        auto& tname = s.expr->type->pointee->struct_name;
        if (!tname.empty()) {
            auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
            auto drop_fn = mod.lookupSymbol<mlir::func::FuncOp>(tname + "__drop");
            if (drop_fn)
                builder_.create<mlir::func::CallOp>(loc_, drop_fn, mlir::ValueRange{ptr});
        }
    }
    call_free(ptr);
}

// ---------------------------------------------------------------------------
// let-else
// ---------------------------------------------------------------------------
//
// Codegen for:   let Pat = expr else { block (must diverge) };
//
// Structure:
//   %scrut_ptr = alloca (enum type)
//   store %scrut_val, %scrut_ptr
//   %disc = load discriminant from %scrut_ptr
//   %cond = icmp eq %disc, expected_disc
//   condbr %cond, bb_match, bb_else
// bb_else:
//   <else block — must terminate with ret/unreachable>
// bb_match:
//   <extract bindings into scope_>
//   <fall through to continuation>
//
// For PatWild (named wildcard): just bind the scrutinee value directly.
// For PatVariant (unit variant): test discriminant, no bindings.
// For PatVariantData: test discriminant + extract payload bindings.

void MLIRGenImpl::gen_stmt_kind(const lir::SLetElse& s) {
    auto* region = builder_.getBlock()->getParent();

    // ── Evaluate scrutinee ────────────────────────────────────────────────
    auto scrut_val = gen_expr(*s.scrut);
    if (!scrut_val) return;

    // ── Handle PatWild: always matches, just bind name ────────────────────
    if (auto* pw = std::get_if<PatWild>(&s.pat)) {
        if (pw->name != "_") {
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), scrut_val.getType(), i64_one());
            builder_.create<mlir::LLVM::StoreOp>(loc_, scrut_val, alloca);
            scope_[pw->name]          = alloca;
            let_vars_.insert(pw->name);
            var_elem_types_[pw->name] = scrut_val.getType();
        }
        // Else block is unreachable because pattern always matches.
        // Still need to lower the else block in a dead block so stmts compile.
        auto* dead = new mlir::Block();
        region->push_back(dead);
        {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(dead);
            gen_block(*s.else_block);
            if (!is_terminated(builder_.getBlock()))
                builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        }
        return;
    }

    // ── Enum patterns: need discriminant test ─────────────────────────────
    const TaggedEnumInfo* te_info = nullptr;
    mlir::Value scrut_ptr;
    mlir::Value disc_val;
    int32_t expected_disc = 0;

    if (s.scrut->type && s.scrut->type->kind == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(s.scrut->type->enum_name, s.scrut->type);
        if (te_info) {
            // Spill to alloca if it's a value (not already a pointer)
            if (scrut_val.getType() != ptr_type()) {
                auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                    loc_, ptr_type(), te_info->llvm_type, i64_one());
                builder_.create<mlir::LLVM::StoreOp>(loc_, scrut_val, alloca);
                scrut_ptr = alloca;
            } else {
                scrut_ptr = scrut_val;
            }
            // Load discriminant (field 0)
            llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
            auto dp = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), te_info->llvm_type, scrut_ptr, di);
            disc_val = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
        }
    }

    // Determine expected discriminant
    if (auto* pv = std::get_if<PatVariant>(&s.pat)) {
        expected_disc = pv->disc;
    } else if (auto* pvd = std::get_if<PatVariantData>(&s.pat)) {
        expected_disc = pvd->disc;
    }

    // ── Build blocks ──────────────────────────────────────────────────────
    auto* else_block  = new mlir::Block();
    auto* match_block = new mlir::Block();
    auto* cont_block  = new mlir::Block();
    region->push_back(else_block);
    region->push_back(match_block);
    region->push_back(cont_block);

    if (disc_val) {
        // Conditional branch on discriminant match
        auto expected = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, expected_disc, 32);
        auto cond = builder_.create<mlir::arith::CmpIOp>(
            loc_, mlir::arith::CmpIPredicate::eq, disc_val, expected);
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, match_block, else_block);
    } else {
        // Non-enum scrutinee — always matches (fall into match_block)
        builder_.create<mlir::cf::BranchOp>(loc_, match_block);
    }

    // ── else_block: diverging else body ──────────────────────────────────
    {
        mlir::OpBuilder::InsertionGuard ig(builder_);
        builder_.setInsertionPointToStart(else_block);
        gen_block(*s.else_block);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
    }

    // ── match_block: extract bindings, jump to continuation ──────────────
    {
        mlir::OpBuilder::InsertionGuard ig(builder_);
        builder_.setInsertionPointToStart(match_block);

        if (auto* pt = std::get_if<PatTuple>(&s.pat)) {
            // Tuple pattern in let-else: always irrefutable, extract fields.
            auto ttype = tuple_llvm_type(s.scrut->type);
            if (ttype) {
                for (size_t bi = 0; bi < pt->bindings.size() && bi < pt->binding_types.size(); ++bi) {
                    if (pt->bindings[bi] == "_") continue;
                    auto elem_mlir = logos_to_mlir(pt->binding_types[bi]);
                    if (!elem_mlir) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, scrut_val, fi);
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
                    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), elem_mlir, i64_one());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                    scope_[pt->bindings[bi]]          = alloca;
                    let_vars_.insert(pt->bindings[bi]);
                    var_elem_types_[pt->bindings[bi]] = elem_mlir;
                }
            }
        } else if (auto* pvd = std::get_if<PatVariantData>(&s.pat)) {
            if (te_info && scrut_ptr) {
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& v : te_info->variants)
                    if (v.disc == pvd->disc) { vp = &v; break; }
                if (vp && !pvd->bindings.empty()) {
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
                        scope_[pvd->bindings[bi]]          = alloca;
                        let_vars_.insert(pvd->bindings[bi]);
                        var_elem_types_[pvd->bindings[bi]] = vp->field_types[bi];
                    }
                }
            }
        }
        // PatVariant (no payload) — discriminant test was enough, no bindings

        builder_.create<mlir::cf::BranchOp>(loc_, cont_block);
    }

    // Continue in cont_block (bindings from match_block are now in scope_)
    builder_.setInsertionPointToStart(cont_block);
}

} // namespace logos::compiler
