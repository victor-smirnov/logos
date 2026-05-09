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

void MLIRGenImpl::gen_block(lir_view::BlockRef block) {
    if (!block) return;
    block.each_stmt([&](lir_view::StmtRef s) {
        if (is_terminated(builder_.getBlock())) return;
        gen_stmt(s);
    });
}

// ---------------------------------------------------------------------------
// Statement dispatch
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_stmt(lir_view::StmtRef sr) {
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
void MLIRGenImpl::gen_stmt_kind(lir_view::SIfView v)         { gen_if(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SWhileView v)      { gen_while(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SForView v)        { gen_for(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SLoopView v)       { gen_loop(v); }
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
void MLIRGenImpl::gen_stmt_kind(lir_view::SMatchView v)      { gen_match(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SDeleteView v)     { gen_delete(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SForEachView v)    { gen_for_each(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SBlockView v)      {
    // Sprint 3.1: restore shadowed SSA-name mappings on inner-block exit
    // (closes B-st-01 — return-after-shadow read inner alloca instead of
    // outer).  We snapshot only the *previous values* of pre-existing
    // names, then after the block, write those values back.  New names
    // introduced inside (e.g. let-destruct's `a`/`b`) survive — sema
    // legitimately uses inner SBlocks to scope tuple-destruct temporaries.
    auto saved_scope            = scope_;
    auto saved_local_ptrs       = var_local_ptrs_;
    auto saved_tuple            = var_tuple_;
    auto saved_tagged_enum      = var_tagged_enum_;
    auto saved_tagged_enum_ptr  = var_tagged_enum_ptr_;
    auto saved_dyn_trait        = var_dyn_trait_;
    auto saved_struct           = var_struct_;
    gen_block(v.body());
    for (auto& [k, val] : saved_scope)            scope_[k]            = val;
    for (auto& [k, val] : saved_local_ptrs)       var_local_ptrs_[k]   = val;
    for (auto& k : saved_tuple)                   var_tuple_.insert(k);
    for (auto& k : saved_tagged_enum)             var_tagged_enum_.insert(k);
    for (auto& k : saved_tagged_enum_ptr)         var_tagged_enum_ptr_.insert(k);
    for (auto& [k, val] : saved_dyn_trait)        var_dyn_trait_[k]    = val;
    for (auto& [k, val] : saved_struct)           var_struct_[k]       = val;
}

void MLIRGenImpl::gen_stmt_kind(lir_view::SDropView v) {
    std::string var_name(v.var_name());
    auto it = scope_.find(var_name);
    if (it == scope_.end()) return;
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // 1. Call user's explicit drop function (if any).
    //    mono_clone re-mangles drop_fn to the bare `<concrete>__drop` form;
    //    after unconditional pkg-mangling the actual symbol is
    //    `pkg.<concrete>__drop__[fg]__sig`. Bridge via resolve_method_symbol.
    std::string drop_fn(v.drop_fn());
    if (!drop_fn.empty()) {
        auto fn = mod.lookupSymbol<mlir::func::FuncOp>(drop_fn);
        if (!fn) {
            std::string_view dfn = drop_fn;
            // Strip a `pkg.` prefix if present, then `__drop[__...]`.
            if (auto dot = dfn.rfind('.'); dot != std::string_view::npos)
                dfn = dfn.substr(dot + 1);
            if (auto p = dfn.find("__drop"); p != std::string_view::npos) {
                auto resolved = resolve_method_symbol(dfn.substr(0, p), "drop");
                if (!resolved.empty())
                    fn = mod.lookupSymbol<mlir::func::FuncOp>(resolved);
            }
        }
        if (fn)
            builder_.create<mlir::func::CallOp>(loc_, fn, mlir::ValueRange{it->second});
    }

    // 2. Auto-drop droppable fields (reverse field order)
    if (TypeRef st = v.type(pool_impl()); v.drop_fields() && st && st.kind() == LogosType::Kind::Struct) {
        // Try pkg-qualified key first; fall back to bare for back-compat.
        auto sit = struct_types_.find(mlir_struct_key(st));
        if (sit == struct_types_.end())
            sit = struct_types_.find(std::string(st.struct_name()));
        if (sit != struct_types_.end()) {
            auto& info = sit->second;
            for (int fi = (int)info.fields.size() - 1; fi >= 0; --fi) {
                auto& f = info.fields[fi];
                std::string field_drop = f.struct_name.empty()
                    ? std::string{} : resolve_method_symbol(f.struct_name, "drop");
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
        var_struct_[s.name] = s.type ? mlir_struct_key(s.type) : std::string(lit.name());
        return;
    }

    // ── Array literal ─────────────────────────────────────────
    if (val_code == lir_schema::expr::Code::ArrLit) {
        lir_view::EArrLitView lit{expr_ref_of(*s.value)};
        // Use the array's slot type (from logos_to_mlir on the whole array)
        // so struct elements get inline LLVM struct slots, not pointers.
        mlir::Type elem_type;
        if (auto arr_t = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(
                logos_to_mlir(s.type)))
            elem_type = arr_t.getElementType();
        else
            elem_type = logos_to_mlir(TypeRef(s.type).elem());
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
        auto sname = mlir_struct_key(s.type);
        auto sit = struct_types_.find(sname);
        if (val.getType() != ptr_type()) {
            // By-value struct from function return — spill to alloca.
            if (sit != struct_types_.end()) {
                auto alloca = create_entry_alloca(sit->second.llvm_type);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                val = alloca;
            }
        } else if (sit != struct_types_.end()) {
            // val is an existing struct pointer (e.g. `let copy = orig;`).
            // Aliasing here is wrong for `impl Copy` types (mutation through
            // copy would leak into orig). For move-only types the borrow
            // checker forbids touching orig, so memcpy is at worst a small
            // redundancy. Always allocate fresh + memcpy → independent slot.
            auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
            auto bytes = (int64_t)dl.getTypeSize(sit->second.llvm_type);
            auto fresh = create_entry_alloca(sit->second.llvm_type);
            auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                loc_, builder_.getI64Type(),
                builder_.getI64IntegerAttr(bytes));
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, fresh, val, sz, /*isVolatile=*/false);
            val = fresh;
        }
        scope_[s.name]    = val;
        let_vars_.insert(s.name);
        var_struct_[s.name] = sname;
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
        var_struct_[s.name] = mlir_struct_key(st.pointee());
        return;
    }

    // ── &dyn Trait / *mut dyn Trait / Box<dyn Trait> coercion ─────────────────
    // *mut/*const dyn Trait shares its codegen layout with &dyn Trait —
    // an 8-byte handle pointing at a 16-byte {data, vtable} slot. Peel the
    // Ptr to expose the inner TraitObject and route to the same path.
    TypeRef _peeled_st(s.type);
    if (_peeled_st && _peeled_st.kind() == LogosType::Kind::Ptr &&
        TypeRef(_peeled_st.pointee()).kind() == LogosType::Kind::TraitObject) {
        _peeled_st = _peeled_st.pointee();
    }
    if (TypeRef st(_peeled_st); st && st.kind() == LogosType::Kind::TraitObject) {
        auto data_ptr = gen_expr(*s.value);
        if (!data_ptr) return;
        mlir::Value alloca;
        TypeRef src_vt(s.value->type);
        // Source may also be `*mut dyn Trait` — peel for the "already fat"
        // shortcut.
        if (src_vt && src_vt.kind() == LogosType::Kind::Ptr &&
            TypeRef(src_vt.pointee()).kind() == LogosType::Kind::TraitObject) {
            src_vt = src_vt.pointee();
        }
        if (src_vt && src_vt.kind() == LogosType::Kind::TraitObject) {
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
        var_struct_[s.name] = mlir_struct_key(st.pointee());
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
        // Use the inline slot type (struct elements lay out as inline
        // aggregates, not pointers) — derive via the whole-array lowering.
        mlir::Type elem_mlir;
        if (auto arr_t = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(var_type))
            elem_mlir = arr_t.getElementType();
        else
            elem_mlir = logos_to_mlir(st.elem());
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
    // Whole-struct rebind (`acc = src`): the slot is the struct alloca itself,
    // and `val` from gen_expr is a pointer to the source struct. A plain
    // StoreOp would overwrite the first 8 bytes of the slot with the pointer
    // value, leaving the rest uninitialised. Memcpy the struct payload like
    // the array-element-write / deref-write paths below.
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
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, it->second, val, sz, /*isVolatile=*/false);
            return;
        }
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
                std::string(TypeRef(cur_fn_ret_logos_type_).trait_name()), type_str(src_lt));
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
        } else if (cur_ret_type_ && mlir::isa<mlir::LLVM::LLVMArrayType>(cur_ret_type_)) {
            if (val.getType() == ptr_type()) {
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, val);
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

void MLIRGenImpl::gen_if(lir_view::SIfView v) {
    auto* cond_le = lexpr_of(v.cond());
    auto* then_lb = lblock_of(v.then_block());
    if (!cond_le || !then_lb) return;
    auto* else_lb = lblock_of(v.else_block());  // may be null
    auto cond = gen_expr(*cond_le);
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
    gen_block(v.then_block());
    bool then_falls = !is_terminated(builder_.getBlock());
    if (then_falls) builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

    builder_.setInsertionPointToStart(else_block);
    if (else_lb) gen_block(v.else_block());
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

void MLIRGenImpl::gen_while(lir_view::SWhileView v) {
    auto* cond_le = lexpr_of(v.cond());
    auto* body_lb = lblock_of(v.body());
    if (!cond_le || !body_lb) return;
    std::string label(v.label());
    auto* region     = builder_.getBlock()->getParent();
    auto* cond_block = new mlir::Block();
    auto* body_block = new mlir::Block();
    auto* exit_block = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(exit_block);

    builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
    builder_.setInsertionPointToStart(cond_block);
    auto cond = gen_expr(*cond_le);
    if (!cond) return;
    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

    builder_.setInsertionPointToStart(body_block);
    loop_stack_.push_back({cond_block, exit_block, {}, label});
    gen_block(v.body());
    loop_stack_.pop_back();
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, cond_block);

    builder_.setInsertionPointToStart(exit_block);
}

// ---------------------------------------------------------------------------
// gen_for
// ---------------------------------------------------------------------------

void MLIRGenImpl::gen_for(lir_view::SForView v) {
    auto* lo_le   = lexpr_of(v.lo());
    auto* hi_le   = lexpr_of(v.hi());
    auto* body_lb = lblock_of(v.body());
    if (!lo_le || !hi_le || !body_lb) return;
    struct ForCtx {
        std::string var;
        const LExpr* lo;
        const LExpr* hi;
        bool inclusive;
        lir_view::BlockRef body;
        std::string label;
    };
    ForCtx s{std::string(v.var()), lo_le, hi_le, v.inclusive(), v.body(),
             std::string(v.label())};
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
    gen_block(s.body);
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

void MLIRGenImpl::gen_loop(lir_view::SLoopView v) {
    auto* body_lb = lblock_of(v.body());
    if (!body_lb) return;
    std::string break_slot_name(v.break_slot());
    std::string label(v.label());
    TypeRef result_type = v.result_type(pool_impl());

    auto* region     = builder_.getBlock()->getParent();
    auto* loop_block = new mlir::Block();
    auto* exit_block = new mlir::Block();
    region->push_back(loop_block);
    region->push_back(exit_block);

    // Allocate break-value slot before the loop block, if needed.
    mlir::Value break_slot;
    if (!break_slot_name.empty() && result_type) {
        mlir::Type slot_ty = logos_to_mlir(result_type);
        if (slot_ty) {
            break_slot  = create_entry_alloca(slot_ty);
            scope_[break_slot_name]          = break_slot;
            let_vars_.insert(break_slot_name);
            var_elem_types_[break_slot_name] = slot_ty;
        }
    }

    builder_.create<mlir::cf::BranchOp>(loc_, loop_block);

    builder_.setInsertionPointToStart(loop_block);
    loop_stack_.push_back({loop_block, exit_block, break_slot, label});
    gen_block(v.body());
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

void MLIRGenImpl::gen_for_each(lir_view::SForEachView v) {
    auto* iter_le = lexpr_of(v.iter());
    auto* body_lb = lblock_of(v.body());
    if (!iter_le || !body_lb) return;
    struct ForEachCtx {
        std::string  var;
        const LExpr* iter;
        TypeRef      elem_type;
        int64_t      arr_size;
        bool         is_slice;
        lir_view::BlockRef body;
    };
    ForEachCtx s{std::string(v.var()), iter_le, v.elem_type(pool_impl()),
                 v.arr_size(), v.is_slice(), v.body()};
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
        gen_block(s.body);
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

    bool is_struct_elem = s.elem_type &&
        TypeRef(s.elem_type).kind() == LogosType::Kind::Struct;

    if (is_struct_elem) {
        // Struct elements are now stored inline as `[N x %struct_type]`.
        // GEP via [0, i] using the array slot type so stride = sizeof(struct);
        // the resulting pointer IS the struct pointer.
        auto cname = mlir_struct_key(s.elem_type);
        auto sit = struct_types_.find(cname);
        if (sit == struct_types_.end()) return;
        auto slot_type = sit->second.llvm_type;
        auto arr_type  = mlir::LLVM::LLVMArrayType::get(slot_type, s.arr_size);
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx{int32_t(0), i_cur};
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_type, arr_alloca, arr_idx);
        scope_[s.var] = elem_ptr;
        var_struct_[s.var] = cname;
    } else {
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx{i_cur};
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_mlir, arr_alloca, arr_idx);
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
    gen_block(s.body);
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
        // Fallback: type_name from the LIR may carry an unsubstituted
        // typevar (e.g. "Inner$G1$T") when sema lowered the stmt inside a
        // generic body and mono didn't rewrite the precomputed string.
        // Resolve the struct from the receiver's tracked local pointer
        // type instead — by mlir-gen time, mono has produced the concrete
        // struct, and var_local_ptrs_ holds the receiver's LLVM type.
        std::string resolved_name;
        auto lpit = var_local_ptrs_.find(receiver);
        if (lpit != var_local_ptrs_.end()) {
            for (auto& [sn, si] : struct_types_) {
                if (si.llvm_type == lpit->second) { resolved_name = sn; break; }
            }
        }
        if (resolved_name.empty()) {
            auto vsi = var_struct_.find(receiver);
            if (vsi != var_struct_.end()) resolved_name = vsi->second;
        }
        if (resolved_name.empty()) {
            auto vci = var_class_.find(receiver);
            if (vci != var_class_.end()) resolved_name = vci->second;
        }
        if (resolved_name.empty()) {
            std::fprintf(stderr, "mlir_gen: deref-field-write: unknown type '%s'\n", type_name.c_str());
            return;
        }
        sit = struct_types_.find(resolved_name);
        if (sit == struct_types_.end()) {
            std::fprintf(stderr, "mlir_gen: deref-field-write: unknown type '%s' (recv resolved to '%s' which has no entry)\n",
                         type_name.c_str(), resolved_name.c_str());
            return;
        }
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

    // Build the full path: [mid_field, extras..., field] (≥2 segments).
    std::vector<std::string> path;
    path.reserve(2 + v.extra_count());
    path.push_back(mid_field);
    v.each_extra([&](std::string_view s) { path.emplace_back(s); });
    path.push_back(field);

    // Step 1: resolve outer struct ptr (same logic as gen_field_write).
    mlir::Value cur_ptr;
    std::string cur_type_name;

    auto sit = var_struct_.find(receiver);
    auto cit = sit == var_struct_.end() ? var_class_.find(receiver) : var_class_.end();
    if (sit != var_struct_.end()) {
        cur_ptr = get_struct_ptr(receiver);
        cur_type_name = sit->second;
    } else if (cit != var_class_.end()) {
        cur_ptr = get_struct_ptr(receiver);
        cur_type_name = cit->second;
    } else {
        auto sc = scope_.find(receiver);
        if (sc != scope_.end()) {
            auto lpit = var_local_ptrs_.find(receiver);
            if (lpit != var_local_ptrs_.end()) {
                for (auto& [sn, si] : struct_types_) {
                    if (si.llvm_type == lpit->second) { cur_type_name = sn; break; }
                }
            }
            if (!cur_type_name.empty()) {
                cur_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), sc->second);
            }
        }
        if (!cur_ptr || cur_type_name.empty()) {
            std::fprintf(stderr, "mlir_gen: chain-field-write: '%s' is not a struct\n",
                         receiver.c_str());
            return;
        }
    }

    // Walk every segment except the final one, GEPing + auto-deref'ing.
    // After the loop, cur_ptr points to the struct that contains the final
    // field, and cur_type_name names that struct.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        auto cti = struct_types_.find(cur_type_name);
        if (cti == struct_types_.end()) {
            std::fprintf(stderr, "mlir_gen: chain-field-write: unknown type '%s'\n",
                         cur_type_name.c_str());
            return;
        }
        auto seg_gep = gep_field(cur_ptr, cti->second, path[i]);
        if (!seg_gep) return;

        // Resolve the next-step struct type name from LIR field type.
        std::string next_type_name;
        bool seg_is_ptr = false;
        auto cdi = all_struct_defs_.find(cur_type_name);
        if (cdi != all_struct_defs_.end()) {
            for (auto& f : cdi->second->fields) {
                if (f.name == path[i] && f.type) {
                    TypeRef ft = f.type;
                    if (TypeRef(ft).kind() == LogosType::Kind::Ptr && TypeRef(ft).pointee()) {
                        ft = TypeRef(ft).pointee();
                        seg_is_ptr = true;
                    }
                    next_type_name = concrete_struct_name(ft);
                    break;
                }
            }
        }
        // Fallback: match by LLVM aggregate type (non-pointer embedded structs).
        if (next_type_name.empty()) {
            mlir::Type seg_llvm_type;
            for (auto& f : cti->second.fields) {
                if (f.name == path[i]) { seg_llvm_type = f.type; break; }
            }
            if (seg_llvm_type) {
                for (auto& [sn, si] : struct_types_) {
                    if (si.llvm_type == seg_llvm_type) { next_type_name = sn; break; }
                }
            }
        }
        if (next_type_name.empty()) {
            std::fprintf(stderr, "mlir_gen: chain-field-write: cannot resolve struct type for '%s.%s'\n",
                         cur_type_name.c_str(), path[i].c_str());
            return;
        }
        // If the segment field is a pointer type, seg_gep is a slot holding
        // the pointer — load once to descend.
        cur_ptr = seg_is_ptr
            ? (mlir::Value)builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), seg_gep)
            : seg_gep;
        cur_type_name = next_type_name;
    }

    // Final GEP into the destination field.
    auto fti = struct_types_.find(cur_type_name);
    if (fti == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: chain-field-write: unknown final type '%s'\n",
                     cur_type_name.c_str());
        return;
    }
    auto field_gep = gep_field(cur_ptr, fti->second, field);
    if (!field_gep) return;

    auto val = gen_expr(*val_le);
    if (!val) return;
    for (auto& f : fti->second.fields) {
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

void MLIRGenImpl::gen_match(lir_view::SMatchView v) {
    // Pat/arm walking still goes through the C++ variant; scrut is routed
    // through the view. Full PatRef migration is a separate slice.
    auto* scrut_le = lexpr_of(v.scrut());
    if (!scrut_le) return;
    namespace pc = lir_schema::pat;
    std::vector<lir_view::EMatchArmRef> arm_refs;
    v.each_arm([&](lir_view::EMatchArmRef a){ arm_refs.push_back(a); });
    auto* region      = builder_.getBlock()->getParent();
    auto* merge_block = new mlir::Block();

    auto scrut = gen_expr(*scrut_le);
    if (!scrut) {
        region->push_back(merge_block);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return;
    }

    // Detect tagged enum: scrut is a pointer, load discriminant.
    mlir::Value scrut_ptr = nullptr;  // non-null for tagged enums
    const TaggedEnumInfo* te_info = nullptr;
    if (TypeRef sct(scrut_le->type); sct) {
        // Auto-deref `&Enum` / `&mut Enum` / `*Enum` so `match &enum_val {...}`
        // works the same as `match enum_val {...}`.
        TypeRef enum_t = sct;
        bool via_ref = false;
        if ((sct.kind() == LogosType::Kind::Ref ||
             sct.kind() == LogosType::Kind::MutRef ||
             sct.kind() == LogosType::Kind::Ptr) && sct.pointee()) {
            TypeRef inner(sct.pointee());
            if (inner.kind() == LogosType::Kind::Enum) {
                enum_t = inner;
                via_ref = true;
            }
        }
        if (enum_t.kind() == LogosType::Kind::Enum) {
            te_info = resolve_tagged_enum(std::string(enum_t.enum_name()), enum_t);
            if (te_info) {
                // Logos enum values are themselves heap pointers (EEnumLitData
                // mallocs and returns the ptr). So `&Enum` is a pointer-to-
                // pointer: scrut = ptr-to-slot-holding-ptr-to-enum-struct.
                // Load the inner ptr to get the actual enum-struct address.
                if (via_ref) {
                    scrut = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, ptr_type(), scrut);
                } else if (scrut.getType() != ptr_type()) {
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
    }
    // Keep scrut at its natural type; coerce disc constants to match it.
    mlir::Type scrut_type = scrut.getType();

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    // Helper: is this pattern irrefutable (always matches)?
    // PatAt is irrefutable only if its sub-pattern is (e.g. n @ _ is irrefutable,
    // n @ 42 is refutable).
    std::function<bool(lir_view::PatRef)> is_irrefutable;
    is_irrefutable = [&](lir_view::PatRef p) -> bool {
        if (!p) return false;
        switch (p.kind()) {
            case pc::Code::Wild:    return true;
            case pc::Code::RefBind: return true;
            case pc::Code::Tuple: {
                lir_view::PatTupleView tv{p};
                if (tv.sub_count() == 0) return true;  // legacy all-wild tuple
                bool all = true;
                tv.each_sub([&](lir_view::PatRef sp){ if (all && !is_irrefutable(sp)) all = false; });
                return all;
            }
            case pc::Code::Struct: {
                bool all = true;
                lir_view::PatStructView{p}.each_field([&](lir_view::PatFieldBindingView fb){
                    auto sub = fb.sub();
                    if (all && sub && !is_irrefutable(sub)) all = false;
                });
                return all;
            }
            case pc::Code::Slice: {
                bool all = true;
                lir_view::PatSliceView sv{p};
                sv.each_prefix([&](lir_view::PatRef sp){ if (all && !is_irrefutable(sp)) all = false; });
                sv.each_rest  ([&](lir_view::PatRef sp){ if (all && !is_irrefutable(sp)) all = false; });
                sv.each_suffix([&](lir_view::PatRef sp){ if (all && !is_irrefutable(sp)) all = false; });
                return all;
            }
            case pc::Code::At: {
                auto sub = lir_view::PatAtView{p}.sub();
                return !sub || is_irrefutable(sub);
            }
            case pc::Code::RefPat: {
                auto inner = lir_view::PatRefPatView{p}.inner();
                return !inner || is_irrefutable(inner);
            }
            // NC5: PatOr is irrefutable only if all alternatives are irrefutable.
            case pc::Code::Or: {
                bool any_alts = false, all = true;
                lir_view::PatOrView{p}.each_alt([&](lir_view::PatRef alt){
                    any_alts = true;
                    if (all && !is_irrefutable(alt)) all = false;
                });
                return !any_alts || all;
            }
            default: return false;
        }
    };
    if (scrut_le->type && TypeRef(scrut_le->type).kind() == LogosType::Kind::Tuple) {
        // Tuple patterns are always irrefutable.
        for (auto& a : arm_refs) {
            if (a.guard()) continue;
            if (is_irrefutable(a.pat())) { exhaustive_discrete = true; break; }
        }
    } else if (scrut_le->type && TypeRef(scrut_le->type).kind() == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (auto& a : arm_refs) {
            if (a.guard()) continue;
            auto p = a.pat();
            if (is_irrefutable(p)) { has_wild = true; break; }
            auto check_bool = [&](lir_view::PatRef pp) {
                if (pp && pp.kind() == pc::Code::Bool) {
                    if (lir_view::PatBoolView{pp}.value()) has_true = true;
                    else                                   has_false = true;
                }
            };
            if (p && p.kind() == pc::Code::Or) {
                lir_view::PatOrView{p}.each_alt(check_bool);
            } else {
                check_bool(p);
            }
        }
        exhaustive_discrete = has_wild || (has_true && has_false);
    } else if (TypeRef sct(scrut_le->type); sct &&
               (sct.kind() == LogosType::Kind::Enum ||
                ((sct.kind() == LogosType::Kind::Ref ||
                  sct.kind() == LogosType::Kind::MutRef ||
                  sct.kind() == LogosType::Kind::Ptr) &&
                 sct.pointee() &&
                 TypeRef(sct.pointee()).kind() == LogosType::Kind::Enum))) {
        // Resolve the underlying enum type (auto-deref &Enum / *Enum).
        TypeRef enum_lt = sct.kind() == LogosType::Kind::Enum ? sct : TypeRef(sct.pointee());
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](lir_view::PatRef pp) {
            if (!pp) return;
            if (pp.kind() == pc::Code::Variant)
                covered.insert(static_cast<int32_t>(lir_view::PatVariantView{pp}.disc()));
            else if (pp.kind() == pc::Code::VariantData)
                covered.insert(static_cast<int32_t>(lir_view::PatVariantDataView{pp}.disc()));
        };
        for (auto& a : arm_refs) {
            if (a.guard()) continue;
            auto p = a.pat();
            if (is_irrefutable(p)) { has_wild = true; break; }
            if (p && p.kind() == pc::Code::Or) {
                lir_view::PatOrView{p}.each_alt(cover_enum);
            } else {
                cover_enum(p);
            }
        }
        if (has_wild) {
            exhaustive_discrete = true;
        } else {
            std::string en(enum_lt.enum_name());
            auto eit = enum_types_.find(en);
            if (eit != enum_types_.end() && eit->second) {
                exhaustive_discrete = std::all_of(
                    eit->second->variants.begin(), eit->second->variants.end(),
                    [&](const lir::LVariant& v) { return covered.count(v.disc) > 0; });
            } else if (auto* te = resolve_tagged_enum(en, enum_lt)) {
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
    std::function<void(lir_view::PatRef)> extract_payload = [&](lir_view::PatRef p) {
        if (!p) return;
        switch (p.kind()) {
        // ── PatTuple ───────────────────────────────────────────────────────
        case pc::Code::Tuple: {
            auto ttype = tuple_llvm_type(scrut_le->type);
            if (!ttype) return;
            mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
            if (!tptr) return;
            lir_view::PatTupleView tv{p};
            std::vector<std::string> bindings;
            tv.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
            std::vector<TypeRef> btypes;
            tv.each_binding_type(pool_impl(), [&](TypeRef t){ btypes.push_back(t); });
            for (size_t bi = 0; bi < bindings.size() && bi < btypes.size(); ++bi) {
                if (bindings[bi] == "_") continue;
                auto elem_mlir = logos_to_mlir(btypes[bi]);
                if (!elem_mlir) continue;
                llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, tptr, fi);
                auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
                auto alloca = create_entry_alloca(elem_mlir);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                scope_[bindings[bi]] = alloca;
                let_vars_.insert(bindings[bi]);
                var_elem_types_[bindings[bi]] = elem_mlir;
            }
            return;
        }
        // ── PatVariantData ────────────────────────────────────────────────
        case pc::Code::VariantData: {
            if (te_info && scrut_ptr) {
                lir_view::PatVariantDataView pvd{p};
                int32_t pvd_disc = static_cast<int32_t>(pvd.disc());
                std::vector<std::string> bindings;
                pvd.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& vinfo : te_info->variants)
                    if (vinfo.disc == pvd_disc) { vp = &vinfo; break; }
                if (vp && !bindings.empty()) {
                    llvm::SmallVector<mlir::Type> ft;
                    for (auto& t : vp->field_types) ft.push_back(t);
                    auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                        builder_.getContext(), ft);
                    for (size_t bi = 0; bi < bindings.size() &&
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
                            scope_[bindings[bi]] = fp;
                            let_vars_.insert(bindings[bi]);
                            var_struct_[bindings[bi]] = mlir_struct_key(lt);
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
                        }
                    }
                }
            }
            return;
        }
        // ── PatStruct: GEP-extract each named field ───────────────────────
        case pc::Code::Struct: {
            lir_view::PatStructView ps{p};
            std::string sname(ps.struct_name());
            auto sit = struct_types_.find(sname);
            if (sit == struct_types_.end()) return;
            const StructInfo& sinfo = sit->second;
            mlir::Value sptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
            if (!sptr) return;
            ps.each_field([&](lir_view::PatFieldBindingView pfb) {
                std::string field_name(pfb.field_name());
                auto bind_struct_field = [&](const std::string& bind_name) {
                    auto fp = gep_field(sptr, sinfo, field_name);
                    if (!fp) return;
                    mlir::Type fmlir;
                    for (auto& sf : sinfo.fields)
                        if (sf.name == field_name) { fmlir = sf.type; break; }
                    if (!fmlir) return;
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, fmlir, fp);
                    auto alloca = create_entry_alloca(fmlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                    scope_[bind_name] = alloca;
                    let_vars_.insert(bind_name);
                    var_elem_types_[bind_name] = fmlir;
                };
                auto sub = pfb.sub();
                if (!sub) {
                    // Shorthand: Point { x } → bind field_name.
                    bind_struct_field(field_name);
                } else if (sub.kind() == pc::Code::Wild) {
                    // C1: Explicit rename: Point { x: a } → bind pw->name to x's value.
                    std::string pwn(lir_view::PatWildView{sub}.name());
                    if (!pwn.empty() && pwn != "_") bind_struct_field(pwn);
                } else if (sub.kind() == pc::Code::RefBind) {
                    // NC3: ref binding to struct field: Point { x: ref px } → px = &field.
                    std::string prbn(lir_view::PatRefBindView{sub}.name());
                    if (!prbn.empty() && prbn != "_") {
                        auto fp = gep_field(sptr, sinfo, field_name);
                        if (fp) {
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                            scope_[prbn] = alloca;
                            let_vars_.insert(prbn);
                            var_elem_types_[prbn] = ptr_type();
                        }
                    }
                }
            });
            return;
        }
        // ── PatSlice: GEP-extract indexed elements ────────────────────────
        case pc::Code::Slice: {
            auto atype = scrut_le->type;
            if (atype && TypeRef(atype).kind() == LogosType::Kind::Array && TypeRef(atype).elem()) {
                auto elem_mlir = logos_to_mlir(TypeRef(atype).elem());
                auto arr_mlir  = logos_to_mlir(atype);
                mlir::Value aptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                if (aptr && elem_mlir && arr_mlir) {
                    auto bind_elem = [&](lir_view::PatRef sp, int32_t idx) {
                        if (!sp) return;
                        // GEP element pointer for this index.
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), arr_mlir, aptr, gi);
                        if (sp.kind() == pc::Code::Wild) {
                            std::string pwn(lir_view::PatWildView{sp}.name());
                            if (pwn == "_" || pwn.empty()) return;
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto alloca = create_entry_alloca(elem_mlir);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                            scope_[pwn] = alloca;
                            let_vars_.insert(pwn);
                            var_elem_types_[pwn] = elem_mlir;
                        } else if (sp.kind() == pc::Code::RefBind) {
                            // C4: ref x in slice pattern — bind name to pointer-to-element.
                            std::string prbn(lir_view::PatRefBindView{sp}.name());
                            if (prbn == "_" || prbn.empty()) return;
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, ep, alloca);
                            scope_[prbn] = alloca;
                            let_vars_.insert(prbn);
                            var_elem_types_[prbn] = ptr_type();
                        }
                    };
                    lir_view::PatSliceView psl{p};
                    int32_t idx = 0;
                    psl.each_prefix([&](lir_view::PatRef sp){ bind_elem(sp, idx++); });
                    size_t total  = (size_t)TypeRef(atype).arr_size();
                    size_t suf_n  = psl.suffix_count();
                    int32_t sidx  = (int32_t)(total - suf_n);
                    psl.each_suffix([&](lir_view::PatRef sp){ bind_elem(sp, sidx++); });
                }
            }
            return;
        }
        // ── PatAt: bind outer name then recurse into sub-pattern ─────────
        case pc::Code::At: {
            lir_view::PatAtView pa{p};
            mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
            std::string aname(pa.name());
            if (!aname.empty() && aname != "_") {
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[aname] = alloca;
                let_vars_.insert(aname);
                var_elem_types_[aname] = sv.getType();
            }
            // C5: recurse into sub-pattern to bind nested fields.
            if (auto sub = pa.sub()) extract_payload(sub);
            return;
        }
        // ── PatRefBind: bind name as a reference (pointer to scrutinee) ──
        case pc::Code::RefBind: {
            std::string prbn(lir_view::PatRefBindView{p}.name());
            if (!prbn.empty() && prbn != "_") {
                // We need a pointer to the scrutinee. If scrut_ptr is available
                // (enum scrutinee on stack), use it directly. Otherwise spill the
                // value to a fresh alloca to obtain an address.
                mlir::Value sv_ptr;
                if (scrut_ptr) {
                    sv_ptr = scrut_ptr;
                } else {
                    auto tmp = create_entry_alloca(scrut.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    sv_ptr = tmp;
                }
                auto alloca = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv_ptr, alloca);
                scope_[prbn] = alloca;
                let_vars_.insert(prbn);
                var_elem_types_[prbn] = ptr_type();
            }
            return;
        }
        // ── PatRefPat: &pat or &mut pat — recurse into inner pattern ─────
        case pc::Code::RefPat: {
            if (auto inner = lir_view::PatRefPatView{p}.inner())
                extract_payload(inner);
            return;
        }
        // ── PatOr: extract bindings from first alternative ────────────────
        case pc::Code::Or: {
            lir_view::PatRef first;
            lir_view::PatOrView{p}.each_alt([&](lir_view::PatRef alt){
                if (!first) first = alt;
            });
            if (first) extract_payload(first);
            return;
        }
        // ── PatWild (named wildcard) ───────────────────────────────────────
        case pc::Code::Wild: {
            std::string pwn(lir_view::PatWildView{p}.name());
            if (!pwn.empty() && pwn != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                scope_[pwn] = alloca;
                let_vars_.insert(pwn);
                var_elem_types_[pwn] = sv.getType();
            }
            return;
        }
        default: return;
        }
    };

    // Helper: scalar discriminant value for a leaf pattern (PatVariant /
    // PatVariantData / PatInt / PatBool). Returns INT64_MIN if not scalar.
    auto get_scalar_disc = [](lir_view::PatRef pp) -> int64_t {
        if (!pp) return std::numeric_limits<int64_t>::min();
        switch (pp.kind()) {
            case pc::Code::Variant:     return lir_view::PatVariantView{pp}.disc();
            case pc::Code::VariantData: return lir_view::PatVariantDataView{pp}.disc();
            case pc::Code::Int:         return lir_view::PatIntView{pp}.value();
            case pc::Code::Bool:        return lir_view::PatBoolView{pp}.value() ? 1 : 0;
            default:                    return std::numeric_limits<int64_t>::min();
        }
    };
    auto scrut_unsigned = [&]() -> bool {
        if (!scrut_le->type) return false;
        switch (TypeRef(scrut_le->type).kind()) {
            case LogosType::Kind::U8:  case LogosType::Kind::U16: case LogosType::Kind::U24:
            case LogosType::Kind::U32: case LogosType::Kind::U56: case LogosType::Kind::U64:
            case LogosType::Kind::U128: return true;
            default: return false;
        }
    };

    // Build if-else chain from last arm down to first.
    for (int i = (int)arm_refs.size() - 1; i >= 0; --i) {
        auto arm_pat   = arm_refs[i].pat();
        auto arm_kind  = arm_pat ? arm_pat.kind() : pc::Code(-1);
        auto arm_guard_ref = arm_refs[i].guard();
        auto arm_body_ref  = arm_refs[i].body();
        auto* body_block   = new mlir::Block();
        region->push_back(body_block);

        mlir::Block* arm_entry = body_block;

        if (arm_guard_ref) {
            // guard_block: extract bindings, evaluate guard, branch accordingly.
            auto* guard_block = new mlir::Block();
            region->push_back(guard_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(guard_block);
                extract_payload(arm_pat);
                auto* guard_le = lexpr_of(arm_guard_ref);
                auto gval = guard_le ? gen_expr(*guard_le) : nullptr;
                gval = coerce_int(gval, builder_.getI1Type());
                builder_.create<mlir::cf::CondBranchOp>(loc_, gval, body_block, else_block);
            }
            arm_entry = guard_block;
            // body_block: bindings already in scope from guard_block.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                if (arm_body_ref) gen_block(arm_body_ref);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        } else {
            // No guard: extract bindings and run body in body_block.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                extract_payload(arm_pat);
                if (arm_body_ref) gen_block(arm_body_ref);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        }

        bool is_wild = is_irrefutable(arm_pat);
        if (is_wild) {
            else_block = arm_entry;
        } else if (arm_kind == pc::Code::Range) {
            // Range pattern: lo <= scrut && scrut <= hi
            // C2: use unsigned predicates for unsigned scrutinee types.
            lir_view::PatRangeView pr{arm_pat};
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
        } else if (arm_kind == pc::Code::Or) {
            // OR pattern: chain of comparisons — any match goes to arm_entry.
            // Build right-to-left so each test falls through to the next.
            // NC4: get_scalar_disc only handles scalar patterns; PatRange and
            // structural patterns inside PatOr are not representable as a single
            // discriminant. Callers must not pass PatOr with non-scalar alts.
            std::vector<lir_view::PatRef> alts;
            lir_view::PatOrView{arm_pat}.each_alt([&](lir_view::PatRef a){ alts.push_back(a); });
            mlir::Block* cur_else = else_block;
            for (int64_t ai = static_cast<int64_t>(alts.size()) - 1; ai >= 0; --ai) {
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                int64_t disc = get_scalar_disc(alts[static_cast<size_t>(ai)]);
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
        } else if (arm_kind == pc::Code::At) {
            // PatAt with refutable sub-pattern: dispatch on sub-pattern.
            auto sub = lir_view::PatAtView{arm_pat}.sub();
            if (sub) {
                if (sub.kind() == pc::Code::Range) {
                    // C2: same unsigned predicate fix for PatAt + PatRange.
                    lir_view::PatRangeView pr{sub};
                    auto at_pred_ge = scrut_unsigned() ? mlir::arith::CmpIPredicate::uge
                                                      : mlir::arith::CmpIPredicate::sge;
                    auto at_pred_le = scrut_unsigned() ? mlir::arith::CmpIPredicate::ule
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
                        auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_ge, scrut, lo_val);
                        auto le = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_le, scrut, hi_val);
                        auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                        builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
                    }
                    else_block = test_block;
                } else {
                    // C3: Scalar sub-pattern: int, bool, variant (disc=0 was wrong for variants).
                    int64_t disc = get_scalar_disc(sub);
                    if (disc == std::numeric_limits<int64_t>::min()) disc = 0;
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
        } else if (arm_kind == pc::Code::Tuple
                   && lir_view::PatTupleView{arm_pat}.sub_count() > 0) {
            // Refutable tuple: GEP each refutable element and AND-chain equality tests.
            lir_view::PatTupleView tv{arm_pat};
            std::vector<lir_view::PatRef> subs;
            tv.each_sub([&](lir_view::PatRef sp){ subs.push_back(sp); });
            std::vector<TypeRef> btypes;
            tv.each_binding_type(pool_impl(), [&](TypeRef t){ btypes.push_back(t); });
            auto ttype = tuple_llvm_type(scrut_le->type);
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                for (size_t si = 0; si < subs.size() && ttype; ++si) {
                    auto sub = subs[si];
                    if (!sub || sub.kind() == pc::Code::Wild) continue;
                    int64_t sub_val = 0;
                    if (sub.kind() == pc::Code::Int)       sub_val = lir_view::PatIntView{sub}.value();
                    else if (sub.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sub}.value() ? 1 : 0;
                    else continue;
                    auto elem_mlir = si < btypes.size()
                                     ? logos_to_mlir(btypes[si]) : mlir::Type();
                    if (!elem_mlir) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(si)};
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
            int64_t disc = get_scalar_disc(arm_pat);
            bool have_disc = (disc != std::numeric_limits<int64_t>::min());

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
            auto drop_fn = mod.lookupSymbol<mlir::func::FuncOp>(resolve_method_symbol(tname, "drop"));
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
    namespace pc = lir_schema::pat;
    auto* scrut_le = lexpr_of(v.scrut());
    auto* else_lb  = lblock_of(v.else_block());
    if (!scrut_le || !else_lb) return;
    auto pat_ref = v.pat();
    auto pat_kind = pat_ref ? pat_ref.kind() : pc::Code(-1);
    auto* region = builder_.getBlock()->getParent();

    // ── Evaluate scrutinee ────────────────────────────────────────────────
    auto scrut_val = gen_expr(*scrut_le);
    if (!scrut_val) return;

    // ── Handle PatWild: always matches, just bind name ────────────────────
    if (pat_kind == pc::Code::Wild) {
        std::string name(lir_view::PatWildView{pat_ref}.name());
        if (!name.empty() && name != "_") {
            auto alloca = create_entry_alloca(scrut_val.getType());
            builder_.create<mlir::LLVM::StoreOp>(loc_, scrut_val, alloca);
            scope_[name]          = alloca;
            let_vars_.insert(name);
            var_elem_types_[name] = scrut_val.getType();
        }
        // Else block is unreachable because pattern always matches.
        // Still need to lower the else block in a dead block so stmts compile.
        auto* dead = new mlir::Block();
        region->push_back(dead);
        {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(dead);
            gen_block(v.else_block());
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

    if (TypeRef sct(scrut_le->type); sct && sct.kind() == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(std::string(sct.enum_name()), scrut_le->type);
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
    if (pat_kind == pc::Code::Variant) {
        expected_disc = static_cast<int32_t>(lir_view::PatVariantView{pat_ref}.disc());
    } else if (pat_kind == pc::Code::VariantData) {
        expected_disc = static_cast<int32_t>(lir_view::PatVariantDataView{pat_ref}.disc());
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
        gen_block(v.else_block());
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
    }

    // ── match_block: extract bindings, jump to continuation ──────────────
    {
        mlir::OpBuilder::InsertionGuard ig(builder_);
        builder_.setInsertionPointToStart(match_block);

        if (pat_kind == pc::Code::Tuple) {
            // Tuple pattern in let-else: always irrefutable, extract fields.
            auto ttype = tuple_llvm_type(scrut_le->type);
            if (ttype) {
                lir_view::PatTupleView tv{pat_ref};
                std::vector<std::string> bindings;
                tv.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
                std::vector<TypeRef> btypes;
                tv.each_binding_type(pool_impl(), [&](TypeRef t){ btypes.push_back(t); });
                for (size_t bi = 0; bi < bindings.size() && bi < btypes.size(); ++bi) {
                    if (bindings[bi] == "_") continue;
                    auto elem_mlir = logos_to_mlir(btypes[bi]);
                    if (!elem_mlir) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, scrut_val, fi);
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, fp);
                    auto alloca = create_entry_alloca(elem_mlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                    scope_[bindings[bi]]          = alloca;
                    let_vars_.insert(bindings[bi]);
                    var_elem_types_[bindings[bi]] = elem_mlir;
                }
            }
        } else if (pat_kind == pc::Code::VariantData) {
            if (te_info && scrut_ptr) {
                lir_view::PatVariantDataView pvd{pat_ref};
                std::vector<std::string> bindings;
                pvd.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
                int32_t pvd_disc = static_cast<int32_t>(pvd.disc());
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& vinfo : te_info->variants)
                    if (vinfo.disc == pvd_disc) { vp = &vinfo; break; }
                if (vp && !bindings.empty()) {
                    llvm::SmallVector<mlir::Type> ft;
                    for (auto& t : vp->field_types) ft.push_back(t);
                    auto pay_struct = mlir::LLVM::LLVMStructType::getLiteral(
                        builder_.getContext(), ft);
                    for (size_t bi = 0; bi < bindings.size() &&
                                         bi < vp->field_types.size(); ++bi) {
                        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                        auto fp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), pay_struct, pay_ptr, fi);
                        auto val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, vp->field_types[bi], fp);
                        auto alloca = create_entry_alloca(vp->field_types[bi]);
                        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                        scope_[bindings[bi]]          = alloca;
                        let_vars_.insert(bindings[bi]);
                        var_elem_types_[bindings[bi]] = vp->field_types[bi];
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
