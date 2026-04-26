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
    auto sr = stmt_ref_of(stmt);
    if (!sr) {
        std::fprintf(stderr, "mlir_gen: gen_stmt called without LIR mirror\n");
        return;
    }
    using C = lir_schema::stmt::Code;
    switch (sr.kind()) {
    case C::Let:             gen_stmt_kind(lir_view::SLetView{sr}); return;
    case C::Assign:          gen_stmt_kind(lir_view::SAssignView{sr}); return;
    case C::Return:          gen_stmt_kind(lir_view::SReturnView{sr}); return;
    case C::If:              gen_stmt_kind(lir_view::SIfView{sr}); return;
    case C::While:           gen_stmt_kind(lir_view::SWhileView{sr}); return;
    case C::For:             gen_stmt_kind(lir_view::SForView{sr}); return;
    case C::Loop:            gen_stmt_kind(lir_view::SLoopView{sr}); return;
    case C::Break:           gen_stmt_kind(lir_view::SBreakView{sr}); return;
    case C::Continue:        gen_stmt_kind(lir_view::SContinueView{sr}); return;
    case C::Block:           gen_stmt_kind(lir_view::SBlockView{sr}); return;
    case C::FieldWrite:      gen_stmt_kind(lir_view::SFieldWriteView{sr}); return;
    case C::IndexWrite:      gen_stmt_kind(lir_view::SIndexWriteView{sr}); return;
    case C::FieldIndexWrite: gen_stmt_kind(lir_view::SFieldIndexWriteView{sr}); return;
    case C::ExprStmt:        gen_stmt_kind(lir_view::SExprStmtView{sr}); return;
    case C::Match:           gen_stmt_kind(lir_view::SMatchView{sr}); return;
    case C::Delete:          gen_stmt_kind(lir_view::SDeleteView{sr}); return;
    case C::ForEach:         gen_stmt_kind(lir_view::SForEachView{sr}); return;
    case C::DerefWrite:      gen_stmt_kind(lir_view::SDerefWriteView{sr}); return;
    case C::Drop:            gen_stmt_kind(lir_view::SDropView{sr}); return;
    case C::DerefFieldWrite: gen_stmt_kind(lir_view::SDerefFieldWriteView{sr}); return;
    case C::TupleWrite:      gen_stmt_kind(lir_view::STupleWriteView{sr}); return;
    case C::LetElse:         gen_stmt_kind(lir_view::SLetElseView{sr}); return;
    case C::ChainFieldWrite: gen_stmt_kind(lir_view::SChainFieldWriteView{sr}); return;
    }
}

void MLIRGenImpl::gen_stmt_kind(lir_view::SLetView v)        { gen_let(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SAssignView v)     { gen_assign(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SReturnView v)     { gen_return(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SIfView v)         { gen_if(std::get<SIf>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SWhileView v)      { gen_while(std::get<SWhile>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SForView v)        { gen_for(std::get<SFor>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SLoopView v)       { gen_loop(std::get<SLoop>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SBreakView v)      { gen_break(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SContinueView v) {
    if (loop_stack_.empty()) return;
    auto label = v.label();
    if (label.empty()) { gen_continue(); return; }
    for (int i = (int)loop_stack_.size() - 1; i >= 0; --i) {
        if (loop_stack_[i].label == label) {
            builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_[i].cont);
            return;
        }
    }
    gen_continue(); // fallback
}
void MLIRGenImpl::gen_stmt_kind(lir_view::SFieldWriteView v)      { gen_field_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::STupleWriteView v)      { gen_tuple_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SDerefFieldWriteView v) { gen_deref_field_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SChainFieldWriteView v) { gen_chain_field_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SIndexWriteView v)      { gen_index_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SFieldIndexWriteView v) { gen_field_index_write(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SExprStmtView v)   { if (auto* le = lexpr_of(v.expr())) gen_expr(*le); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SMatchView v)      { gen_match(std::get<SMatch>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SDeleteView v)     { gen_delete(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SForEachView v)    { gen_for_each(std::get<SForEach>(lstmt_of(v.self)->kind)); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SBlockView v)      { if (auto* lb = lblock_of(v.body())) gen_block(*lb); }

void MLIRGenImpl::gen_stmt_kind(lir_view::SDropView v) {
    std::string var_name(v.var_name());
    auto it = scope_.find(var_name);
    if (it == scope_.end()) return;
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // 1. Call user's explicit drop function (if any)
    std::string drop_fn(v.drop_fn());
    if (!drop_fn.empty()) {
        auto fn = mod.lookupSymbol<mlir::func::FuncOp>(drop_fn);
        if (fn)
            builder_.create<mlir::func::CallOp>(loc_, fn, mlir::ValueRange{it->second});
    }

    // 2. Auto-drop droppable fields (reverse field order)
    if (TypeRef st = v.type(pool_impl()); v.drop_fields() && st && st.kind() == LogosType::Kind::Struct) {
        auto sit = struct_types_.find(std::string(st.struct_name()));
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

void MLIRGenImpl::gen_stmt_kind(lir_view::SDerefWriteView v) {
    auto* ptr_le = lexpr_of(v.ptr());
    auto* val_le = lexpr_of(v.value());
    if (!ptr_le || !val_le) return;
    auto ptr = gen_expr(*ptr_le);
    auto val = gen_expr(*val_le);
    if (!ptr || !val) return;
    // Determine element type from pointer's pointee type (handles both *T and &mut T)
    mlir::Type elem_type = nullptr;
    TypeRef pt(ptr_le->type);
    if (pt && pt.pointee() &&
        (pt.kind() == LogosType::Kind::Ptr ||
         pt.kind() == LogosType::Kind::MutRef))
        elem_type = logos_to_mlir(pt.pointee());
    if (!elem_type) elem_type = builder_.getI32Type();
    TypeRef pointee_t = (pt && pt.pointee()) ? pt.pointee() : nullptr;
    if (pointee_t && (TypeRef(pointee_t).kind() == LogosType::Kind::Struct ||
                      TypeRef(pointee_t).kind() == LogosType::Kind::ZonedStruct) &&
        val.getType() == ptr_type()) {
        auto cname = concrete_struct_name(pointee_t);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) {
            auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
            auto bytes = (int64_t)dl.getTypeSize(sit->second.llvm_type);
            auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                loc_, builder_.getI64Type(),
                builder_.getI64IntegerAttr(bytes));
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, sz, /*isVolatile=*/false);
            return;
        }
    }
    val = coerce_int(val, elem_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, ptr);
}

// ---------------------------------------------------------------------------
// gen_let
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_let(lir_view::SLetView v) {
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;
    struct LetCtx {
        std::string  name;
        TypeRef      type;
        bool         is_mut;
        const LExpr* value;
    };
    LetCtx s{std::string(v.name()), v.type(pool_impl()), v.is_mut(), val_le};
    if (s.type && type_str(s.type) == "AnyVal") {
        auto val = gen_expr(*s.value);
        if (!val) return;
        val = coerce_numeric(val, builder_.getI32Type());
        auto alloca = create_entry_alloca(builder_.getI32Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_elem_types_[s.name] = builder_.getI32Type();
        return;
    }
    auto val_code = expr_ref_of(*s.value).kind();
    // ── Struct literal ────────────────────────────────────────
    if (val_code == lir_schema::expr::Code::StructLit) {
        lir_view::EStructLitView lit{expr_ref_of(*s.value)};
        auto alloca = gen_struct_lit(lit);
        if (!alloca) return;
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_struct_[s.name] = s.type ? concrete_struct_name(s.type) : std::string(lit.name());
        return;
    }

    // ── Array literal ─────────────────────────────────────────
    if (val_code == lir_schema::expr::Code::ArrLit) {
        lir_view::EArrLitView lit{expr_ref_of(*s.value)};
        auto elem_type = logos_to_mlir(TypeRef(s.type).elem());
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
    if (val_code == lir_schema::expr::Code::TupleLit) {
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
    if (s.type && TypeRef(s.type).kind() == LogosType::Kind::Tuple) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        auto stype = tuple_llvm_type(s.type);
        if (stype && val.getType() != ptr_type()) {
            // By-value struct (e.g. from function call) — store into alloca.
            auto alloca = create_entry_alloca(stype);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            val = alloca;
        }
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);
        return;
    }

    // ── Closure value ─────────────────────────────────────────
    if (s.type && TypeRef(s.type).kind() == LogosType::Kind::Closure) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);  // return ptr directly
        return;
    }

    // ── FnPtr value (fn(T) -> R) ──────────────────────────────
    if (s.type && TypeRef(s.type).kind() == LogosType::Kind::FnPtr) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        // Store as a let-bound scalar (alloca holding a ptr).
        auto alloca = create_entry_alloca(ptr_type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
        scope_[s.name]          = alloca;
        let_vars_.insert(s.name);
        var_elem_types_[s.name] = ptr_type();
        return;
    }

    // ── Slice / str value ────────────────────────────────────
    if (s.type && TypeRef(s.type).kind() == LogosType::Kind::Slice) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_tuple_.insert(s.name);
        return;
    }

    // ── Tagged enum value ────────────────────────────────────
    if (TypeRef st(s.type); st && st.kind() == LogosType::Kind::Enum) {
        auto* te = resolve_tagged_enum(std::string(st.enum_name()), s.type);
        if (te) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            // If gen_expr returned a non-pointer value, create the tagged enum alloca.
            if (val.getType() != ptr_type()) {
                auto alloca = create_entry_alloca(te->llvm_type);
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
                auto ptr_slot = create_entry_alloca(ptr_type());
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
    if (s.type && (TypeRef(s.type).kind() == LogosType::Kind::Struct ||
                    TypeRef(s.type).kind() == LogosType::Kind::ZonedStruct)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        if (val.getType() != ptr_type()) {
            // By-value struct from function return — spill to alloca.
            auto sname = concrete_struct_name(s.type);
            auto sit = struct_types_.find(sname);
            if (sit != struct_types_.end()) {
                auto alloca = create_entry_alloca(sit->second.llvm_type);
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
    if (TypeRef st(s.type);
        st && (st.kind() == LogosType::Kind::Ref ||
               st.kind() == LogosType::Kind::MutRef) &&
        st.pointee() && (st.pointee().kind() == LogosType::Kind::Struct ||
                         st.pointee().kind() == LogosType::Kind::ZonedStruct)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        scope_[s.name] = val;
        let_vars_.insert(s.name);
        var_struct_[s.name] = concrete_struct_name(st.pointee());
        return;
    }

    // ── &dyn Trait / Box<dyn Trait> coercion ─────────────────
    if (TypeRef st(s.type); st && st.kind() == LogosType::Kind::TraitObject) {
        auto data_ptr = gen_expr(*s.value);
        if (!data_ptr) return;
        mlir::Value alloca;
        if (TypeRef vt(s.value->type); vt && vt.kind() == LogosType::Kind::TraitObject) {
            // RHS is already a fat pointer (e.g., returned from a Box<dyn T> function).
            // Use it directly — no need to rebuild the fat struct.
            alloca = data_ptr;
        } else {
            // Concrete type → build fat pointer from scratch.
            // For &dyn T from `new Foo {}`, value type is *mut Foo — strip the pointer.
            // Box<T> is { *mut T } so the value *is* the data ptr; unwrap to T for vtable.
            TypeRef src_logos_type = s.value->type;
            if (src_logos_type && TypeRef(src_logos_type).kind() == LogosType::Kind::Ptr &&
                TypeRef(src_logos_type).pointee())
                src_logos_type = TypeRef(src_logos_type).pointee();
            if (src_logos_type &&
                TypeRef(src_logos_type).kind() == LogosType::Kind::Struct &&
                TypeRef(src_logos_type).struct_name() == "Box" &&
                TypeRef(src_logos_type).type_args().size() == 1)
                src_logos_type = TypeRef(src_logos_type).type_args()[0];
            std::string src_type = type_str(src_logos_type);
            alloca = coerce_to_dyn(data_ptr, std::string(st.trait_name()), src_type);
        }
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_dyn_trait_[s.name] = std::string(st.trait_name());
        return;
    }

    // ── Pointer to struct/datatype ────────────────────────────
    if (TypeRef st(s.type);
        st && st.kind() == LogosType::Kind::Ptr && st.pointee() &&
        (st.pointee().kind() == LogosType::Kind::Struct ||
         st.pointee().kind() == LogosType::Kind::ZonedStruct)) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        if (s.is_mut) {
            auto slot = create_entry_alloca(ptr_type());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, slot);
            scope_[s.name] = slot;
            var_elem_types_[s.name] = ptr_type();
            auto cname = concrete_struct_name(st.pointee());
            auto sit2 = struct_types_.find(cname);
            if (sit2 != struct_types_.end())
                var_local_ptrs_[s.name] = sit2->second.llvm_type;
        } else {
            scope_[s.name] = val;
        }
        let_vars_.insert(s.name);
        var_struct_[s.name] = concrete_struct_name(st.pointee());
        return;
    }

    // ── Scalar ───────────────────────────────────────────────
    // Pre-allocate the slot BEFORE generating the RHS expression.
    // This ensures the AllocaOp is in the current block (entry-reachable)
    // even when the RHS is an if-expression that creates new blocks.
    auto var_type = logos_to_mlir(s.type);
    mlir::Value alloca;
    if (var_type) {
        alloca = create_entry_alloca(var_type);
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
    if (TypeRef st(s.type); st && st.kind() == LogosType::Kind::Array && st.elem()) {
        auto elem_mlir = logos_to_mlir(st.elem());
        if (!elem_mlir) elem_mlir = builder_.getI32Type();
        var_elem_types_[s.name] = elem_mlir;
        var_subscript_[s.name]  = elem_mlir;
    } else {
        var_elem_types_[s.name] = var_type;
    }
    // Track local pointer variables so indexing can load the ptr before GEP.
    if (TypeRef st(s.type); st && st.kind() == LogosType::Kind::Ptr && st.pointee()) {
        TypeRef pointee = st.pointee();
        if (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
            TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct) {
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

void MLIRGenImpl::gen_assign(lir_view::SAssignView v) {
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;
    auto val = gen_expr(*val_le);
    if (!val) return;
    std::string name(v.name());
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: assign to undefined '%s'\n", name.c_str());
        return;
    }
    // Mutable tagged enum: val is a new struct ptr; store to pointer slot.
    if (var_tagged_enum_ptr_.count(name)) {
        // If val is an aggregate (returned by value), spill to alloca first.
        val = spill_to_alloca(val);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
        return;
    }
    auto et = var_elem_types_.find(name);
    if (et != var_elem_types_.end())
        val = coerce_int(val, et->second);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
}

// ---------------------------------------------------------------------------
// gen_return
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_return(lir_view::SReturnView v) {
    auto val_er = v.value();
    const LExpr* val_le = val_er ? lexpr_of(val_er) : nullptr;
    if (val_le) {
        const LExpr& s_value = *val_le;
        // Box<dyn Trait> / &dyn Trait return: coerce concrete type to heap fat pointer.
        if (cur_fn_ret_logos_type_ &&
            TypeRef(cur_fn_ret_logos_type_).kind() == LogosType::Kind::TraitObject &&
            s_value.type &&
            TypeRef(s_value.type).kind() != LogosType::Kind::TraitObject) {
            auto val = gen_expr(s_value);
            if (!val) return;
            TypeRef src_lt = s_value.type;
            if (TypeRef(src_lt).kind() == LogosType::Kind::Ptr && TypeRef(src_lt).pointee())
                src_lt = TypeRef(src_lt).pointee();
            auto vtable = build_inline_vtable(
                TypeRef(cur_fn_ret_logos_type_).trait_name(), type_str(src_lt));
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

        auto val = gen_expr(s_value);
        if (!val) return;
        if (cur_fn_ret_logos_type_ &&
            TypeRef(cur_fn_ret_logos_type_).kind() == LogosType::Kind::Slice &&
            val.getType() == ptr_type()) {
            auto size16 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 16LL, 64);
            auto heap = call_malloc(size16);
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, heap, val, size16, false);
            val = heap;
        }
        if (cur_ret_type_ && cur_ret_type_ == ptr_type() && val.getType() != ptr_type()) {
            if (s_value.type && TypeRef(s_value.type).kind() == LogosType::Kind::Enum) {
                // The value is a discriminant — need to figure out the enum struct type.
                // Look through all registered tagged enums to find a matching one.
                // For now: create a generic {i32, [4 x i8]} wrapper.
                auto i32t = builder_.getI32Type();
                auto pad = mlir::LLVM::LLVMArrayType::get(builder_.getIntegerType(8), 4);
                auto wrap = mlir::LLVM::LLVMStructType::getLiteral(
                    builder_.getContext(), {i32t, pad});
                auto alloca = create_entry_alloca(wrap);
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
                auto alloca = create_entry_alloca(cur_ret_type_);
                auto disc_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), cur_ret_type_, alloca,
                    llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(0)});
                builder_.create<mlir::LLVM::StoreOp>(
                    loc_, coerce_int(val, builder_.getI32Type()), disc_ptr);
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, alloca);
            }
        }
        else if (cur_ret_type_)
            val = coerce_numeric(val, cur_ret_type_, s_value.type);
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

    auto i_alloca = create_entry_alloca(loop_type);
    bool lo_unsigned = s.lo->type &&
        (TypeRef(s.lo->type).kind() == LogosType::Kind::U8  ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U16 ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U32 ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U24 ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U56 ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U64 ||
         TypeRef(s.lo->type).kind() == LogosType::Kind::U128);
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
        (TypeRef(s.hi->type).kind() == LogosType::Kind::U8  ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U16 ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U32 ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U24 ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U56 ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U64 ||
         TypeRef(s.hi->type).kind() == LogosType::Kind::U128);
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
            break_slot  = create_entry_alloca(slot_ty);
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
    // If exit_block has no predecessors the loop never breaks — it's unreachable.
    // Emit llvm.unreachable so gen_block sees this block as terminated and stops
    // emitting dead code after the loop (avoids invalid func.return in dead blocks).
    if (exit_block->hasNoPredecessors())
        builder_.create<mlir::LLVM::UnreachableOp>(loc_);
}

// ---------------------------------------------------------------------------
// gen_break / gen_continue
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_break(lir_view::SBreakView v) {
    if (loop_stack_.empty()) return;
    std::string label(v.label());
    LoopBlocks* target = nullptr;
    if (label.empty()) {
        target = &loop_stack_.back();
    } else {
        for (int i = (int)loop_stack_.size() - 1; i >= 0; --i) {
            if (loop_stack_[i].label == label) { target = &loop_stack_[i]; break; }
        }
        if (!target) { target = &loop_stack_.back(); }
    }
    auto val_er = v.value();
    if (val_er && target->break_slot) {
        if (auto* val_le = lexpr_of(val_er)) {
            mlir::Value val = gen_expr(*val_le);
            if (val)
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, target->break_slot);
        }
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
        auto i_alloca = create_entry_alloca(builder_.getI64Type());
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

        auto elem_alloca = create_entry_alloca(elem_mlir);
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
    auto i_alloca = create_entry_alloca(builder_.getI32Type());
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
        TypeRef(s.elem_type).kind() == LogosType::Kind::Struct;

    if (is_struct_elem) {
        // Struct elements are stored as pointers in the array ([N x ptr]).
        // Load the pointer — it IS the struct pointer, matching the struct convention
        // (scope_ holds a direct struct pointer for struct variables).
        auto struct_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), elem_ptr);
        scope_[s.var] = struct_ptr;
        var_struct_[s.var] = concrete_struct_name(s.elem_type);
    } else {
        // Scalar: alloca + store so the body can read (and mutate) via scope_.
        auto elem_alloca = create_entry_alloca(elem_mlir);
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

void MLIRGenImpl::gen_field_write(lir_view::SFieldWriteView v) {
    std::string receiver(v.receiver());
    std::string field(v.field());
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;

    mlir::Value ptr;
    std::string type_name;

    // Check if receiver is a direct struct/class var.
    auto sit = var_struct_.find(receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(receiver) : var_class_.end();
    if (sit != var_struct_.end()) {
        ptr = get_struct_ptr(receiver);
        type_name = sit->second;
    } else if (cit != var_class_.end()) {
        ptr = get_struct_ptr(receiver);
        type_name = cit->second;
    } else {
        // May be a pointer-to-struct variable (e.g. *mut Point or &mut Point).
        // var_local_ptrs_ stores the pointee MLIR type for raw-pointer locals.
        // Match it against known struct LLVM types to recover the struct name.
        auto sc = scope_.find(receiver);
        if (sc != scope_.end()) {
            auto lpit = var_local_ptrs_.find(receiver);
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
                        if (f.name == field) { type_name = sn; break; }
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
                         receiver.c_str());
            return;
        }
    }
    auto& info = struct_types_[type_name];
    auto gep = gep_field(ptr, info, field);
    if (!gep) return;
    auto val = gen_expr(*val_le);
    if (!val) return;
    for (auto& f : info.fields) {
        if (f.name == field) {
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

void MLIRGenImpl::gen_deref_field_write(lir_view::SDerefFieldWriteView v) {
    std::string receiver(v.receiver());
    std::string type_name(v.type_name());
    std::string field(v.field());
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;

    auto it = scope_.find(receiver);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: deref-field-write: undefined '%s'\n", receiver.c_str());
        return;
    }
    // Mutable class pointer vars store an alloca(ptr); load to get the actual object ptr.
    // Immutable class pointer vars store the raw ptr directly.
    mlir::Value ptr;
    if (var_elem_types_.count(receiver)) {
        ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
    } else {
        ptr = it->second;
    }
    auto sit = struct_types_.find(type_name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: deref-field-write: unknown type '%s'\n", type_name.c_str());
        return;
    }
    auto& info = sit->second;
    auto gep = gep_field(ptr, info, field);
    if (!gep) return;
    auto val = gen_expr(*val_le);
    if (!val) return;
    for (auto& f : info.fields) {
        if (f.name == field) {
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

void MLIRGenImpl::gen_chain_field_write(lir_view::SChainFieldWriteView v) {
    std::string receiver(v.receiver());
    std::string mid_field(v.mid_field());
    std::string field(v.field());
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;

    // a.mid_field.field = value
    // Step 1: resolve outer struct ptr (same logic as gen_field_write).
    mlir::Value outer_ptr;
    std::string outer_type_name;

    auto sit = var_struct_.find(receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(receiver) : var_class_.end();
    if (sit != var_struct_.end()) {
        outer_ptr = get_struct_ptr(receiver);
        outer_type_name = sit->second;
    } else if (cit != var_class_.end()) {
        outer_ptr = get_struct_ptr(receiver);
        outer_type_name = cit->second;
    } else {
        auto sc = scope_.find(receiver);
        if (sc != scope_.end()) {
            auto lpit = var_local_ptrs_.find(receiver);
            if (lpit != var_local_ptrs_.end()) {
                for (auto& [sn, si] : struct_types_) {
                    if (si.llvm_type == lpit->second) { outer_type_name = sn; break; }
                }
            }
            if (!outer_type_name.empty()) {
                outer_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), sc->second);
            }
        }
        if (!outer_ptr || outer_type_name.empty()) {
            std::fprintf(stderr, "mlir_gen: chain-field-write: '%s' is not a struct\n",
                         receiver.c_str());
            return;
        }
    }

    // Step 2: GEP into mid_field — gives *mut MidType.
    auto oit = struct_types_.find(outer_type_name);
    if (oit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: chain-field-write: unknown outer type '%s'\n",
                     outer_type_name.c_str());
        return;
    }
    auto mid_gep = gep_field(outer_ptr, oit->second, mid_field);
    if (!mid_gep) return;

    // Determine mid struct type name.
    // First try: look up the LIR field type from all_struct_defs_ and call
    // concrete_struct_name on its pointee.  This handles the common case where
    // the mid field is a *mut S pointer (opaque ptr in LLVM — LLVM type alone
    // can't tell us which struct it points to).
    std::string mid_type_name;
    auto odi = all_struct_defs_.find(outer_type_name);
    if (odi != all_struct_defs_.end()) {
        for (auto& f : odi->second->fields) {
            if (f.name == mid_field && f.type) {
                TypeRef ft = f.type;
                // field is *mut S → descend into pointee
                if (TypeRef(ft).kind() == LogosType::Kind::Ptr && TypeRef(ft).pointee())
                    ft = TypeRef(ft).pointee();
                mid_type_name = concrete_struct_name(ft);
                break;
            }
        }
    }
    // Fallback: match by LLVM aggregate type (covers non-pointer embedded structs).
    if (mid_type_name.empty()) {
        mlir::Type mid_llvm_type;
        for (auto& f : oit->second.fields) {
            if (f.name == mid_field) { mid_llvm_type = f.type; break; }
        }
        if (mid_llvm_type) {
            for (auto& [sn, si] : struct_types_) {
                if (si.llvm_type == mid_llvm_type) { mid_type_name = sn; break; }
            }
        }
    }
    if (mid_type_name.empty()) {
        std::fprintf(stderr, "mlir_gen: chain-field-write: cannot resolve struct type for '%s.%s'\n",
                     outer_type_name.c_str(), mid_field.c_str());
        return;
    }

    // Step 3: GEP into final field using mid_gep as base pointer.
    // If the mid field is a pointer type (e.g. inner: *mut Inner<T>), mid_gep
    // is a pointer TO that pointer slot.  Load once to obtain *mut Inner<T>
    // before performing the final GEP.
    mlir::Value mid_base = mid_gep;
    if (odi != all_struct_defs_.end()) {
        for (auto& f : odi->second->fields) {
            if (f.name == mid_field && f.type &&
                TypeRef(f.type).kind() == LogosType::Kind::Ptr) {
                mid_base = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), mid_gep);
                break;
            }
        }
    }
    auto mit = struct_types_.find(mid_type_name);
    if (mit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: chain-field-write: unknown mid type '%s'\n",
                     mid_type_name.c_str());
        return;
    }
    auto field_gep = gep_field(mid_base, mit->second, field);
    if (!field_gep) return;

    // Step 4: generate value and store.
    auto val = gen_expr(*val_le);
    if (!val) return;
    for (auto& f : mit->second.fields) {
        if (f.name == field) {
            if (mlir::isa<mlir::LLVM::LLVMStructType>(f.type) && val.getType() == ptr_type())
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, val);
            else
                val = coerce_int(val, f.type);
            break;
        }
    }
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, field_gep);
}

void MLIRGenImpl::gen_tuple_write(lir_view::STupleWriteView v) {
    // var.N = value;  — tuple field write via GEP + store
    std::string receiver(v.receiver());
    auto it = scope_.find(receiver);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: tuple write: undefined '%s'\n", receiver.c_str());
        return;
    }
    // Get the LLVM struct type for the tuple from the LIR receiver type.
    auto stype = tuple_llvm_type(v.recv_type(pool_impl()));
    if (!stype) {
        std::fprintf(stderr, "mlir_gen: tuple write: cannot derive tuple type for '%s'\n",
                     receiver.c_str());
        return;
    }
    auto* val_le = lexpr_of(v.value());
    if (!val_le) return;
    mlir::Value base_ptr = it->second;
    auto val = gen_expr(*val_le);
    if (!val) return;
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(v.index())};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, base_ptr, idx);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

// ---------------------------------------------------------------------------
// gen_index_write / gen_field_index_write
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_index_write(lir_view::SIndexWriteView v) {
    std::string arr(v.arr());
    auto it = scope_.find(arr);
    if (it == scope_.end()) {
        std::fprintf(stderr, "mlir_gen: index write: undefined '%s'\n", arr.c_str());
        return;
    }
    // Local pointer variables: scope_ holds an alloca(ptr); load the actual ptr first.
    mlir::Value base_ptr;
    mlir::Type  elem_type;
    auto lpit = var_local_ptrs_.find(arr);
    if (lpit != var_local_ptrs_.end()) {
        base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
        elem_type = lpit->second;
    } else {
        base_ptr  = it->second;
        elem_type = subscript_elem_type(arr);
    }

    auto* idx_le = lexpr_of(v.index());
    auto* val_le = lexpr_of(v.value());
    if (!idx_le || !val_le) return;
    auto idx = gen_expr(*idx_le);
    auto val = gen_expr(*val_le);
    if (!idx || !val) return;
    val = coerce_int(val, elem_type);

    // Zero-extend unsigned index types so u8(200) doesn't become i8(-56) in GEP.
    bool idx_unsigned = idx_le->type &&
        (TypeRef(idx_le->type).kind() == LogosType::Kind::U8  ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U16 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U32 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U24 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U56 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U64 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U128);
    if (idx_unsigned && idx.getType() != builder_.getI64Type())
        idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
    llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, base_ptr, indices);

    // Struct/datatype element assignment is a byte-level copy: gen_expr of a
    // struct-typed r-value returns a pointer to the struct bytes, not the
    // struct by value. Emit llvm.memcpy of sizeof(struct) bytes. Mirrors the
    // path in gen_stmt_kind(SDerefWrite).
    TypeRef val_t = val_le ? val_le->type : nullptr;
    if (val_t && (TypeRef(val_t).kind() == LogosType::Kind::Struct ||
                  TypeRef(val_t).kind() == LogosType::Kind::ZonedStruct) &&
        val.getType() == ptr_type()) {
        auto cname = concrete_struct_name(val_t);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) {
            auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
            auto bytes = (int64_t)dl.getTypeSize(sit->second.llvm_type);
            auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                loc_, builder_.getI64Type(),
                builder_.getI64IntegerAttr(bytes));
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, sz, /*isVolatile=*/false);
            return;
        }
    }

    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

void MLIRGenImpl::gen_field_index_write(lir_view::SFieldIndexWriteView v) {
    std::string receiver(v.receiver());
    std::string field(v.field());
    auto* idx_le = lexpr_of(v.index());
    auto* val_le = lexpr_of(v.value());
    if (!idx_le || !val_le) return;

    // Get pointer to the struct/class.
    auto struct_ptr = get_struct_ptr(receiver);
    if (!struct_ptr) return;

    // Get struct type info to find the field.
    auto sit = var_struct_.find(receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(receiver) : var_class_.end();
    if (sit == var_struct_.end() && cit == var_class_.end()) {
        std::fprintf(stderr, "mlir_gen: field index write: '%s' not struct/class\n",
                     receiver.c_str());
        return;
    }
    const std::string& type_name = (sit != var_struct_.end()) ? sit->second : cit->second;
    auto& info = struct_types_[type_name];

    // GEP to the field.
    auto field_gep = gep_field(struct_ptr, info, field);
    if (!field_gep) return;

    // Determine element type and base pointer.
    mlir::Type field_mlir_type = builder_.getI32Type();  // dummy default
    bool       is_array_field  = false;
    for (auto& f : info.fields) {
        if (f.name == field) {
            field_mlir_type = f.type;
            is_array_field  = mlir::isa<mlir::LLVM::LLVMArrayType>(f.type);
            break;
        }
    }

    mlir::Type val_type = val_le->type ? logos_to_mlir(val_le->type) : builder_.getI32Type();
    if (!val_type) val_type = builder_.getI32Type();

    auto idx = gen_expr(*idx_le);
    auto val = gen_expr(*val_le);
    if (!idx || !val) return;
    val = coerce_int(val, val_type);

    // Zero-extend unsigned index types; coerce_int sign-extends, which is wrong for u8/u16/u32/u64.
    bool idx_unsigned = idx_le->type &&
        (TypeRef(idx_le->type).kind() == LogosType::Kind::U8  ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U16 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U32 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U24 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U56 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U64 ||
         TypeRef(idx_le->type).kind() == LogosType::Kind::U128);
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
        // For struct-typed values, val_type is ptr (structs are passed by ref);
        // GEP stride must use the concrete struct LLVM type, not ptr_type (8B).
        mlir::Type gep_elem = val_type;
        TypeRef vt = val_le ? val_le->type : nullptr;
        if (vt && (TypeRef(vt).kind() == LogosType::Kind::Struct ||
                   TypeRef(vt).kind() == LogosType::Kind::ZonedStruct)) {
            auto cname = concrete_struct_name(vt);
            auto sit2 = struct_types_.find(cname);
            if (sit2 != struct_types_.end()) gep_elem = sit2->second.llvm_type;
        }
        llvm::SmallVector<mlir::LLVM::GEPArg> ptr_idx{extend_idx(builder_.getIntegerType(32))};
        base_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), gep_elem, field_ptr, ptr_idx);
    }

    // Struct/datatype element assignment is a byte-level copy: gen_expr of a
    // struct-typed r-value returns a pointer to the struct bytes. Emit
    // llvm.memcpy instead of StoreOp (mirrors gen_index_write / SDerefWrite).
    TypeRef val_t = val_le ? val_le->type : nullptr;
    if (val_t && (TypeRef(val_t).kind() == LogosType::Kind::Struct ||
                  TypeRef(val_t).kind() == LogosType::Kind::ZonedStruct) &&
        val.getType() == ptr_type()) {
        auto cname = concrete_struct_name(val_t);
        auto sit2 = struct_types_.find(cname);
        if (sit2 != struct_types_.end()) {
            auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
            auto bytes = (int64_t)dl.getTypeSize(sit2->second.llvm_type);
            auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                loc_, builder_.getI64Type(),
                builder_.getI64IntegerAttr(bytes));
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, base_ptr, val, sz, /*isVolatile=*/false);
            return;
        }
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
    if (TypeRef sct(s.scrut->type); sct && sct.kind() == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(std::string(sct.enum_name()), s.scrut->type);
        if (te_info) {
            // If scrut is an aggregate (returned by value from a function),
            // spill it to an alloca so GEP works below.
            if (scrut.getType() != ptr_type()) {
                auto alloca = create_entry_alloca(te_info->llvm_type);
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
        if (auto* pt = std::get_if<lir::PatTuple>(&p)) {
            if (pt->subs.empty()) return true;  // legacy all-wild tuple
            for (auto& s : pt->subs) if (!is_irrefutable(s)) return false;
            return true;
        }
        if (auto* ps = std::get_if<lir::PatStruct>(&p)) {
            for (auto& pfb : ps->fields)
                if (!pfb.sub.empty() && !is_irrefutable(pfb.sub[0])) return false;
            return true;
        }
        if (auto* psl = std::get_if<lir::PatSlice>(&p)) {
            for (auto& sp : psl->prefix) if (!is_irrefutable(sp)) return false;
            for (auto& sp : psl->rest)   if (!is_irrefutable(sp)) return false;
            for (auto& sp : psl->suffix) if (!is_irrefutable(sp)) return false;
            return true;
        }
        if (std::holds_alternative<lir::PatRefBind>(p))  return true;
        if (auto* pa = std::get_if<lir::PatAt>(&p))
            return pa->sub.empty() || is_irrefutable(pa->sub[0]);
        if (auto* prp = std::get_if<lir::PatRefPat>(&p))
            return prp->inner.empty() || is_irrefutable(prp->inner[0]);
        // NC5: PatOr is irrefutable only if all alternatives are irrefutable.
        if (auto* por = std::get_if<lir::PatOr>(&p)) {
            if (por->alts.empty()) return true;
            for (auto& alt : por->alts)
                if (!is_irrefutable(alt)) return false;
            return true;
        }
        return false;
    };
    if (s.scrut->type && TypeRef(s.scrut->type).kind() == LogosType::Kind::Tuple) {
        // Tuple patterns are always irrefutable.
        for (auto& arm : s.arms) {
            if (arm.guard) continue;
            if (is_irrefutable(arm.pat)) { exhaustive_discrete = true; break; }
        }
    } else if (s.scrut->type && TypeRef(s.scrut->type).kind() == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (auto& arm : s.arms) {
            if (arm.guard) continue;
            if (is_irrefutable(arm.pat)) { has_wild = true; break; }
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
    } else if (s.scrut->type && TypeRef(s.scrut->type).kind() == LogosType::Kind::Enum) {
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](const lir::Pattern& p) {
            if (auto* pv  = std::get_if<lir::PatVariant>(&p))     covered.insert(pv->disc);
            else if (auto* pvd = std::get_if<lir::PatVariantData>(&p)) covered.insert(pvd->disc);
        };
        for (auto& arm : s.arms) {
            if (arm.guard) continue;
            if (is_irrefutable(arm.pat)) { has_wild = true; break; }
            if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
                for (auto& alt : por->alts) cover_enum(alt);
            } else {
                cover_enum(arm.pat);
            }
        }
        if (has_wild) {
            exhaustive_discrete = true;
        } else {
            std::string en(TypeRef(s.scrut->type).enum_name());
            auto eit = enum_types_.find(en);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(en, s.scrut->type)) {
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
    std::function<void(const LMatchArm&)> extract_payload = [&](const LMatchArm& arm) {
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
                auto alloca = create_entry_alloca(elem_mlir);
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
                        // For inline structs, fp already points to the struct bytes —
                        // use it directly (no load), matching the memcpy write side.
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
                            // See mlir_gen_expr.cpp (EMatch) — bind `fp`
                            // directly as the struct pointer so SDrop of
                            // this binding calls T__drop on the inline
                            // struct bytes instead of on a ptr-holding slot.
                            scope_[pvd->bindings[bi]] = fp;
                            let_vars_.insert(pvd->bindings[bi]);
                            var_struct_[pvd->bindings[bi]] = concrete_struct_name(lt);
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
                        }
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
                // Helper: GEP + load a field value and bind it to `bind_name`.
                auto bind_struct_field = [&](const std::string& bind_name) {
                    auto fp = gep_field(sptr, sinfo, pfb.field_name);
                    if (!fp) return;
                    mlir::Type fmlir;
                    for (auto& sf : sinfo.fields)
                        if (sf.name == pfb.field_name) { fmlir = sf.type; break; }
                    if (!fmlir) return;
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, fmlir, fp);
                    auto alloca = create_entry_alloca(fmlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                    scope_[bind_name] = alloca;
                    let_vars_.insert(bind_name);
                    var_elem_types_[bind_name] = fmlir;
                };
                if (pfb.sub.empty()) {
                    // Shorthand: Point { x } → bind field_name.
                    bind_struct_field(pfb.field_name);
                } else if (auto* pw = std::get_if<lir::PatWild>(&pfb.sub[0])) {
                    // C1: Explicit rename: Point { x: a } → bind pw->name to x's value.
                    if (pw->name != "_") bind_struct_field(pw->name);
                } else if (auto* prb = std::get_if<lir::PatRefBind>(&pfb.sub[0])) {
                    // NC3: ref binding to struct field: Point { x: ref px } → px = &field.
                    if (!prb->name.empty() && prb->name != "_") {
                        auto fp = gep_field(sptr, sinfo, pfb.field_name);
                        if (fp) {
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                            scope_[prb->name] = alloca;
                            let_vars_.insert(prb->name);
                            var_elem_types_[prb->name] = ptr_type();
                        }
                    }
                }
            }
            return;
        }
        // ── PatSlice: GEP-extract indexed elements ────────────────────────
        if (auto* psl = std::get_if<PatSlice>(&arm.pat)) {
            auto atype = s.scrut->type;
            if (atype && TypeRef(atype).kind() == LogosType::Kind::Array && TypeRef(atype).elem()) {
                auto elem_mlir = logos_to_mlir(TypeRef(atype).elem());
                auto arr_mlir  = logos_to_mlir(atype);
                mlir::Value aptr = scrut_ptr ? scrut_ptr : gen_expr(*s.scrut);
                if (aptr && elem_mlir && arr_mlir) {
                    auto bind_elem = [&](const lir::Pattern& p, int32_t idx) {
                        // GEP element pointer for this index.
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), arr_mlir, aptr, gi);
                        if (auto* pw = std::get_if<lir::PatWild>(&p)) {
                            if (pw->name == "_") return;
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto alloca = create_entry_alloca(elem_mlir);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                            scope_[pw->name] = alloca;
                            let_vars_.insert(pw->name);
                            var_elem_types_[pw->name] = elem_mlir;
                        } else if (auto* prb = std::get_if<lir::PatRefBind>(&p)) {
                            // C4: ref x in slice pattern — bind name to pointer-to-element.
                            if (prb->name == "_") return;
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, ep, alloca);
                            scope_[prb->name] = alloca;
                            let_vars_.insert(prb->name);
                            var_elem_types_[prb->name] = ptr_type();
                        }
                    };
                    for (size_t i = 0; i < psl->prefix.size(); ++i)
                        bind_elem(psl->prefix[i], (int32_t)i);
                    size_t total = (size_t)TypeRef(atype).arr_size();
                    for (size_t i = 0; i < psl->suffix.size(); ++i)
                        bind_elem(psl->suffix[i], (int32_t)(total - psl->suffix.size() + i));
                }
            }
            return;
        }
        // ── PatAt: bind outer name then recurse into sub-pattern ─────────
        if (auto* pa = std::get_if<PatAt>(&arm.pat)) {
            mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
            if (!pa->name.empty() && pa->name != "_") {
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[pa->name] = alloca;
                let_vars_.insert(pa->name);
                var_elem_types_[pa->name] = sv.getType();
            }
            // C5: recurse into sub-pattern to bind nested fields (e.g. n @ Point { x, y }).
            if (!pa->sub.empty()) {
                lir::LMatchArm sub_arm;
                sub_arm.pat = pa->sub[0];
                extract_payload(sub_arm);
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
                    auto tmp = create_entry_alloca(scrut.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    sv_ptr = tmp;
                }
                // n: &T → alloca(ptr) holding the address of the scrutinee.
                auto alloca = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv_ptr, alloca);
                scope_[prb->name] = alloca;
                let_vars_.insert(prb->name);
                var_elem_types_[prb->name] = ptr_type();
            }
            return;
        }
        // ── PatRefPat: &pat or &mut pat — recurse into inner pattern ─────
        // NC1: extract_payload was missing a PatRefPat handler.
        if (auto* prp = std::get_if<PatRefPat>(&arm.pat)) {
            if (!prp->inner.empty()) {
                lir::LMatchArm sub_arm;
                sub_arm.pat = prp->inner[0];
                extract_payload(sub_arm);
            }
            return;
        }
        // ── PatOr: extract bindings from first alternative ────────────────
        // NC2: extract_payload was missing a PatOr handler.
        // All alts must bind the same names (sema ensures this); use first alt.
        if (auto* por = std::get_if<PatOr>(&arm.pat)) {
            if (!por->alts.empty()) {
                lir::LMatchArm sub_arm;
                sub_arm.pat = por->alts[0];
                extract_payload(sub_arm);
            }
            return;
        }
        // ── PatWild (named wildcard) ───────────────────────────────────────
        if (auto* pw = std::get_if<PatWild>(&arm.pat)) {
            if (pw->name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
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
            // C2: use unsigned predicates for unsigned scrutinee types.
            bool range_unsigned = s.scrut->type &&
                (TypeRef(s.scrut->type).kind() == LogosType::Kind::U8  ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U16 ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U32 ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U24 ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U56 ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U64 ||
                 TypeRef(s.scrut->type).kind() == LogosType::Kind::U128);
            auto pred_ge = range_unsigned ? mlir::arith::CmpIPredicate::uge
                                          : mlir::arith::CmpIPredicate::sge;
            auto pred_le = range_unsigned ? mlir::arith::CmpIPredicate::ule
                                          : mlir::arith::CmpIPredicate::sle;
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto lo_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->lo, 64), scrut_type);
                auto hi_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->hi, 64), scrut_type);
                auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, pred_ge, scrut, lo_val);
                auto le = builder_.create<mlir::arith::CmpIOp>(loc_, pred_le, scrut, hi_val);
                auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (auto* por = std::get_if<lir::PatOr>(&arm.pat)) {
            // OR pattern: chain of comparisons — any match goes to arm_entry.
            // Build right-to-left so each test falls through to the next.
            // NC4: get_disc only handles scalar patterns; PatRange and structural
            // patterns inside PatOr are not representable as a single discriminant.
            // Callers must not pass PatOr with non-scalar alternatives here.
            auto get_disc = [](const lir::Pattern& p) -> int64_t {
                if (auto* pv  = std::get_if<lir::PatVariant>(&p))     return pv->disc;
                if (auto* pvd = std::get_if<lir::PatVariantData>(&p)) return pvd->disc;
                if (auto* pi  = std::get_if<lir::PatInt>(&p))         return pi->value;
                if (auto* pb  = std::get_if<lir::PatBool>(&p))        return pb->value ? 1 : 0;
                // PatRange and structural patterns inside PatOr are unsupported here;
                // sema should reject them. Return INT64_MIN as sentinel.
                return std::numeric_limits<int64_t>::min();
            };
            mlir::Block* cur_else = else_block;
            for (int64_t ai = static_cast<int64_t>(por->alts.size()) - 1; ai >= 0; --ai) {
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                int64_t disc = get_disc(por->alts[static_cast<size_t>(ai)]);
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                if (disc == std::numeric_limits<int64_t>::min()) {
                    // Unrepresentable alt (e.g. PatRange, structural): skip to next test.
                    // Sema should have rejected this, but fall-through safely instead of
                    // emitting a bogus cmp-eq-INT64_MIN that can spuriously match.
                    builder_.create<mlir::cf::BranchOp>(loc_, cur_else);
                } else {
                    auto disc_val = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64), scrut_type);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                    builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, cur_else);
                }
                cur_else = test_block;
            }
            else_block = cur_else;
        } else if (auto* pa = std::get_if<lir::PatAt>(&arm.pat)) {
            // PatAt with refutable sub-pattern: dispatch on sub-pattern.
            if (!pa->sub.empty()) {
                const lir::Pattern& sub = pa->sub[0];
                if (auto* pr = std::get_if<lir::PatRange>(&sub)) {
                    // C2: same unsigned predicate fix for PatAt + PatRange.
                    bool pat_at_unsigned = s.scrut->type &&
                        (TypeRef(s.scrut->type).kind() == LogosType::Kind::U8  ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U16 ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U32 ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U24 ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U56 ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U64 ||
                         TypeRef(s.scrut->type).kind() == LogosType::Kind::U128);
                    auto at_pred_ge = pat_at_unsigned ? mlir::arith::CmpIPredicate::uge
                                                      : mlir::arith::CmpIPredicate::sge;
                    auto at_pred_le = pat_at_unsigned ? mlir::arith::CmpIPredicate::ule
                                                      : mlir::arith::CmpIPredicate::sle;
                    auto* test_block = new mlir::Block();
                    region->push_back(test_block);
                    {
                        mlir::OpBuilder::InsertionGuard ig(builder_);
                        builder_.setInsertionPointToStart(test_block);
                        auto lo_val = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->lo, 64), scrut_type);
                        auto hi_val = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, pr->hi, 64), scrut_type);
                        auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_ge, scrut, lo_val);
                        auto le = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_le, scrut, hi_val);
                        auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                        builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
                    }
                    else_block = test_block;
                } else {
                    // C3: Scalar sub-pattern: int, bool, variant (disc=0 was wrong for variants).
                    int64_t disc = 0;
                    if (auto* pi  = std::get_if<lir::PatInt>(&sub))         disc = pi->value;
                    else if (auto* pb  = std::get_if<lir::PatBool>(&sub))   disc = pb->value ? 1 : 0;
                    else if (auto* pv  = std::get_if<lir::PatVariant>(&sub)) disc = pv->disc;
                    else if (auto* pvd = std::get_if<lir::PatVariantData>(&sub)) disc = pvd->disc;
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
        } else if (auto* pt = std::get_if<lir::PatTuple>(&arm.pat); pt && !pt->subs.empty()) {
            // Refutable tuple: GEP each refutable element and AND-chain equality tests.
            auto ttype = tuple_llvm_type(s.scrut->type);
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*s.scrut);
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                for (size_t i = 0; i < pt->subs.size() && ttype; ++i) {
                    const lir::Pattern& sub = pt->subs[i];
                    if (std::holds_alternative<lir::PatWild>(sub)) continue;
                    int64_t sub_val = 0;
                    if (auto* pi = std::get_if<lir::PatInt>(&sub))       sub_val = pi->value;
                    else if (auto* pb = std::get_if<lir::PatBool>(&sub)) sub_val = pb->value ? 1 : 0;
                    else continue;
                    auto elem_mlir = i < pt->binding_types.size()
                                     ? logos_to_mlir(pt->binding_types[i]) : mlir::Type();
                    if (!elem_mlir) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(i)};
                    auto fp = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), ttype, tptr, fi);
                    auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
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
        } else {
            bool have_disc = false;
            int64_t disc = 0;
            if (auto* pv = std::get_if<PatVariant>(&arm.pat))      { disc = pv->disc;  have_disc = true; }
            else if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) { disc = pvd->disc; have_disc = true; }
            else if (auto* pi = std::get_if<PatInt>(&arm.pat))     { disc = pi->value; have_disc = true; }
            else if (auto* pb = std::get_if<PatBool>(&arm.pat))    { disc = pb->value ? 1 : 0; have_disc = true; }

            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                if (!have_disc) {
                    // Unhandled refutable pattern kind (e.g. PatRefPat with refutable
                    // inner). Sema should have rejected or lowered this elsewhere; fall
                    // through safely rather than comparing scrut to an arbitrary 0.
                    builder_.create<mlir::cf::BranchOp>(loc_, else_block);
                } else {
                    auto disc_val = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64),
                        scrut_type);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                    builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
                }
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

void MLIRGenImpl::gen_delete(lir_view::SDeleteView v) {
    auto er = v.expr();
    auto* le = er ? lexpr_of(er) : nullptr;
    if (!le) return;
    auto ptr = gen_expr(*le);
    if (!ptr) return;
    // Call Drop before free (if the class/struct has a drop function)
    if (TypeRef et(le->type);
        et && et.kind() == LogosType::Kind::Ptr && et.pointee()) {
        auto tname = et.pointee().struct_name();
        if (!tname.empty()) {
            auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
            auto drop_fn = mod.lookupSymbol<mlir::func::FuncOp>(std::string(tname) + "__drop");
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

void MLIRGenImpl::gen_stmt_kind(lir_view::SLetElseView v) {
    auto& s = std::get<lir::SLetElse>(lstmt_of(v.self)->kind);
    auto* region = builder_.getBlock()->getParent();

    // ── Evaluate scrutinee ────────────────────────────────────────────────
    auto scrut_val = gen_expr(*s.scrut);
    if (!scrut_val) return;

    // ── Handle PatWild: always matches, just bind name ────────────────────
    if (auto* pw = std::get_if<PatWild>(&s.pat)) {
        if (pw->name != "_") {
            auto alloca = create_entry_alloca(scrut_val.getType());
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

    if (TypeRef sct(s.scrut->type); sct && sct.kind() == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(std::string(sct.enum_name()), s.scrut->type);
        if (te_info) {
            // Spill to alloca if it's a value (not already a pointer)
            if (scrut_val.getType() != ptr_type()) {
                auto alloca = create_entry_alloca(te_info->llvm_type);
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
                    auto alloca = create_entry_alloca(elem_mlir);
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
                        auto alloca = create_entry_alloca(vp->field_types[bi]);
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
