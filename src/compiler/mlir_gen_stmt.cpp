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
void MLIRGenImpl::gen_stmt_kind(const SBreak&)         { gen_break(); }
void MLIRGenImpl::gen_stmt_kind(const SContinue&)      { gen_continue(); }
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
                // GEP to field, load the struct pointer, then pass to drop
                auto field_gep = gep_field(it->second, info, f.name);
                if (!field_gep) continue;
                // Struct fields are stored as pointers to allocas
                auto field_val = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, ptr_type(), field_gep);
                builder_.create<mlir::func::CallOp>(loc_, field_fn, mlir::ValueRange{field_val});
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
        var_struct_[s.name] = lit.name;
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
    if (s.type && s.type->kind == LogosType::Kind::Struct) {
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
        s.type->pointee && s.type->pointee->kind == LogosType::Kind::Struct) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_struct_[s.name] = concrete_struct_name(s.type->pointee);
        return;
    }

    // ── Class pointer (from 'new') ────────────────────────────
    if (s.type && s.type->kind == LogosType::Kind::Ptr &&
        s.type->pointee && s.type->pointee->kind == LogosType::Kind::Class) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        if (s.is_mut) {
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), ptr_type(), i64_one());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            scope_[s.name]          = alloca;
            let_vars_.insert(s.name);
            var_elem_types_[s.name] = ptr_type();
        } else {
            scope_[s.name]  = val;
            let_vars_.insert(s.name);
            var_class_[s.name] = concrete_class_name(s.type->pointee);
        }
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
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
    scope_[s.name]          = alloca;
    let_vars_.insert(s.name);
    var_elem_types_[s.name] = var_type;
    // Track local pointer variables so indexing can load the ptr before GEP.
    if (s.type && s.type->kind == LogosType::Kind::Ptr && s.type->pointee) {
        auto pt = logos_to_mlir(s.type->pointee);
        if (pt) var_local_ptrs_[s.name] = pt;
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
            val = coerce_int(val, cur_ret_type_);
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
    loop_stack_.push_back({cond_block, exit_block});
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
         s.lo->type->kind == LogosType::Kind::U64);
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
         s.hi->type->kind == LogosType::Kind::U64);
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
    loop_stack_.push_back({incr_block, exit_block});
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

    builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

    builder_.setInsertionPointToStart(loop_block);
    loop_stack_.push_back({loop_block, exit_block});
    gen_block(*s.body);
    loop_stack_.pop_back();
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

    builder_.setInsertionPointToStart(exit_block);
}

// ---------------------------------------------------------------------------
// gen_break / gen_continue
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_break() {
    if (loop_stack_.empty()) return;
    builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().exit);
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

        loop_stack_.push_back({incr_block, exit_block});
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
        (s.elem_type->kind == LogosType::Kind::Struct ||
         s.elem_type->kind == LogosType::Kind::Class);

    if (is_struct_elem) {
        // Struct elements are stored as pointers in the array ([N x ptr]).
        // Load the pointer — it IS the struct pointer, matching the struct convention
        // (scope_ holds a direct struct pointer for struct variables).
        auto struct_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), elem_ptr);
        scope_[s.var] = struct_ptr;
        if (s.elem_type->kind == LogosType::Kind::Struct)
            var_struct_[s.var] = concrete_struct_name(s.elem_type);
        else
            var_class_[s.var] = s.elem_type->struct_name;
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

    loop_stack_.push_back({incr_block, exit_block});
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
    auto ptr = get_struct_ptr(s.receiver);
    if (!ptr) return;
    // Look up struct info: check var_struct_ first, then var_class_.
    auto sit = var_struct_.find(s.receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(s.receiver) : var_class_.end();
    if (sit == var_struct_.end() && cit == var_class_.end()) {
        std::fprintf(stderr, "mlir_gen: field write: '%s' is not a struct/class\n",
                     s.receiver.c_str());
        return;
    }
    const std::string& type_name = (sit != var_struct_.end()) ? sit->second : cit->second;
    auto& info = struct_types_[type_name];
    auto gep = gep_field(ptr, info, s.field);
    if (!gep) return;
    auto val = gen_expr(*s.value);
    if (!val) return;
    for (auto& f : info.fields)
        if (f.name == s.field) { val = coerce_int(val, f.type); break; }
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
    for (auto& f : info.fields)
        if (f.name == s.field) { val = coerce_int(val, f.type); break; }
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

    mlir::Value base_ptr;
    if (is_array_field) {
        // field_gep points to the array — index directly into it.
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx;
        arr_idx.push_back(mlir::LLVM::GEPArg(int32_t(0)));
        auto idx_i32 = coerce_int(idx, builder_.getIntegerType(32));
        arr_idx.push_back(mlir::LLVM::GEPArg(idx_i32));
        base_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), field_mlir_type, field_gep, arr_idx);
    } else {
        // Pointer field: load the stored pointer, then GEP to element.
        auto field_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_gep);
        auto idx_i32 = coerce_int(idx, builder_.getIntegerType(32));
        llvm::SmallVector<mlir::LLVM::GEPArg> ptr_idx{idx_i32};
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

    // Determine if any arm is a wildcard.
    bool has_wild = false;
    for (auto& arm : s.arms)
        if (std::holds_alternative<PatWild>(arm.pat)) { has_wild = true; break; }

    int last_tested = (int)s.arms.size() - 1;
    mlir::Block* else_block = merge_block;

    if (!has_wild && !s.arms.empty()) {
        // Pre-generate the last arm's body as the initial else target.
        auto& last_arm = s.arms.back();
        auto* last_body = new mlir::Block();
        region->push_back(last_body);
        {
            mlir::OpBuilder::InsertionGuard guard(builder_);
            builder_.setInsertionPointToStart(last_body);
            // Extract payload bindings for tagged enum patterns (same as loop body).
            if (auto* pvd = std::get_if<PatVariantData>(&last_arm.pat)) {
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
            }
            gen_block(*last_arm.body);
            if (!is_terminated(builder_.getBlock()))
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        }
        else_block = last_body;
        last_tested = (int)s.arms.size() - 2;
    }

    // Helper: extract PatVariantData payload bindings into scope (call inside a block).
    auto extract_payload = [&](const LMatchArm& arm) {
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
        } else if (auto* pw = std::get_if<PatWild>(&arm.pat)) {
            // Named wildcard: bind the scrutinee value to pw->name.
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

    // Build if-else chain from second-to-last arm down to first.
    for (int i = last_tested; i >= 0; --i) {
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

} // namespace logos::compiler
