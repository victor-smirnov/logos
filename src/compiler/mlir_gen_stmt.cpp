// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_stmt.cpp — Statement code generation.

#include "mlir_gen_impl.hpp"
#include <set>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// Enum payload binding (shared by stmt + expr match, for tuple-element enums)
// ---------------------------------------------------------------------------

void MLIRGenImpl::bind_enum_payload(mlir::Value enum_ptr,
                                    const TaggedEnumInfo* te,
                                    lir_view::PatVariantDataView pvd,
                                    std::vector<std::string>& added,
                                    const std::unordered_map<std::string, mlir::Value>* shared) {
    if (!enum_ptr || !te) return;
    std::vector<std::string> bindings;
    pvd.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
    if (bindings.empty()) return;
    std::vector<TypeRef> pvd_binding_types;
    pvd.each_binding_type(pool_impl(),
        [&](TypeRef t){ pvd_binding_types.push_back(t); });
    int64_t pvd_disc = pvd.disc();
    const TaggedEnumInfo::VariantPayload* vp = nullptr;
    for (auto& v : te->variants)
        if (v.disc == pvd_disc) { vp = &v; break; }
    if (!vp) return;

    // GEP {0,1} to the payload area, then GEP each field within the payload
    // struct (mirrors the top-level VariantData extraction in extract_payload).
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
    auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), te->llvm_type, enum_ptr, pi);
    auto pay_struct = variant_payload_struct(*vp);

    for (size_t bi = 0; bi < bindings.size() && bi < vp->field_types.size(); ++bi) {
        if (bindings[bi] == "_") continue;
        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), pay_struct, pay_ptr, fi);
        TypeRef lt = bi < vp->logos_types.size() ? vp->logos_types[bi] : nullptr;

        // `ref v` / `ref mut v` — sema wraps pvd_binding_types[bi] in
        // Ref/MutRef while the payload type stays bare. Bind the GEP address.
        // logos-core 4.3 (finish): the binding type may be wrapped MULTIPLE
        // times (`&&T` / deeper) when match ergonomics flow through a
        // `&&Option<T>`-style scrutinee. Count the layers via `ref_bind_depth`
        // so the codegen materializes N-1 intermediate stack temps before
        // the final binding slot (a depth-N reference is the address of a
        // slot holding the depth-(N-1) value).
        int ref_bind_depth = 0;
        bool is_ref_bind = false;
        if (bi < pvd_binding_types.size() && pvd_binding_types[bi]) {
            TypeRef walk(pvd_binding_types[bi]);
            while (walk &&
                   (walk.kind() == LogosType::Kind::Ref ||
                    walk.kind() == LogosType::Kind::MutRef) &&
                   walk.pointee()) {
                ++ref_bind_depth;
                walk = walk.pointee();
            }
            auto pt = lt ? TypeRef(lt) : TypeRef{};
            bool payload_is_ref = pt &&
                (pt.kind() == LogosType::Kind::Ref ||
                 pt.kind() == LogosType::Kind::MutRef);
            if (ref_bind_depth > 0 && !payload_is_ref) is_ref_bind = true;
        }
        if (is_ref_bind) {
            // G151-1: `ref l` of a STRUCT-typed payload field binds `l : &Struct`.
            // `fp` already IS the pointer to the inline payload struct, so bind
            // it exactly like a `&Struct` let — scope_[l] = fp + var_struct_[l]
            // — so `l.field` GEPs through it. (Wrapping fp in an extra
            // ptr-of-ptr alloca with no struct-shape tracking made `l.field`
            // read garbage — the silent miscompile.) Scalar `ref l` keeps the
            // alloca-wrap below so `*l` derefs through one level.
            //
            // For depth > 1, materialize (ref_bind_depth - 1) intermediate
            // stack temps. Each temp holds the previous-layer reference value;
            // the next temp's value is the address of the previous temp. The
            // resulting binding-slot's content is a depth-N ref, peeled by
            // (N) loads at use sites.
            TypeRef pointee = lt ? TypeRef(lt) : TypeRef{};
            bool ref_to_struct = pointee &&
                (pointee.kind() == LogosType::Kind::Struct ||
                 pointee.kind() == LogosType::Kind::ZonedStruct);
            if (ref_to_struct && ref_bind_depth == 1) {
                // Depth-1 ref-to-struct: bind fp directly + carry struct shape.
                scope_[bindings[bi]] = fp;
                let_vars_.insert(bindings[bi]);
                var_struct_[bindings[bi]] = mlir_struct_key(lt);
                added.push_back(bindings[bi]);
                continue;
            }
            // General path: depth-1 = `alloca holds fp`. Depth-N = chain
            // (N-1) intermediates + final bind slot. After this loop,
            // `chain_val` is the depth-N reference value; we store it into
            // `bind_slot` so reading the binding loads bind_slot → depth-N.
            mlir::Value chain_val = fp;
            for (int li = 1; li < ref_bind_depth; ++li) {
                auto intermediate = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, chain_val, intermediate);
                chain_val = intermediate;
            }
            auto bind_slot = create_entry_alloca(ptr_type());
            builder_.create<mlir::LLVM::StoreOp>(loc_, chain_val, bind_slot);
            scope_[bindings[bi]] = bind_slot;
            let_vars_.insert(bindings[bi]);
            var_elem_types_[bindings[bi]] = ptr_type();
            added.push_back(bindings[bi]);
            continue;
        }
        bool is_inline_struct = lt &&
            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct);
        if (is_inline_struct) {
            // fp already points at the inline struct bytes — bind directly.
            scope_[bindings[bi]] = fp;
            let_vars_.insert(bindings[bi]);
            var_struct_[bindings[bi]] = mlir_struct_key(lt);
            added.push_back(bindings[bi]);
            continue;
        }
        // Enum value-repr: a nested TAGGED enum payload field is INLINE — `fp`
        // is its storage address (one level). Bind it directly as a tagged-enum
        // var. A C-like enum (no TaggedEnumInfo) is an i32 — scalar-load below.
        if (lt && TypeRef(lt).kind() == LogosType::Kind::Enum &&
            resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt)) {
            scope_[bindings[bi]] = fp;
            let_vars_.insert(bindings[bi]);
            var_tagged_enum_.insert(bindings[bi]);
            var_struct_.erase(bindings[bi]);
            var_tuple_.erase(bindings[bi]);
            var_elem_types_.erase(bindings[bi]);
            added.push_back(bindings[bi]);
            continue;
        }
        // Trait-object payload (e.g. `Option<&dyn T>`'s Some arm): bind the
        // 8-byte handle directly (mirrors extract_payload / gen_let).
        bool is_ref_to_trait = lt &&
            (TypeRef(lt).kind() == LogosType::Kind::Ref ||
             TypeRef(lt).kind() == LogosType::Kind::MutRef ||
             TypeRef(lt).kind() == LogosType::Kind::Ptr) &&
            TypeRef(lt).pointee() &&
            TypeRef(TypeRef(lt).pointee()).kind() == LogosType::Kind::TraitObject;
        bool is_bare_trait = lt &&
            TypeRef(lt).kind() == LogosType::Kind::TraitObject;
        auto bound_val = builder_.create<mlir::LLVM::LoadOp>(
            loc_, vp->field_types[bi], fp);
        if (is_bare_trait || is_ref_to_trait) {
            TypeRef trait_t = is_bare_trait ? lt : TypeRef(lt).pointee();
            scope_[bindings[bi]] = bound_val;
            let_vars_.insert(bindings[bi]);
            var_dyn_trait_[bindings[bi]] = std::string(TypeRef(trait_t).trait_name());
            added.push_back(bindings[bi]);
            continue;
        }
        mlir::Value alloca;
        if (shared) { auto it = shared->find(bindings[bi]); if (it != shared->end()) alloca = it->second; }
        if (!alloca) alloca = create_entry_alloca(vp->field_types[bi]);
        builder_.create<mlir::LLVM::StoreOp>(loc_, bound_val, alloca);
        scope_[bindings[bi]] = alloca;
        let_vars_.insert(bindings[bi]);
        var_elem_types_[bindings[bi]] = vp->field_types[bi];
        // Evict stale shape-tracking (the name may be re-bound from an outer
        // different-shape value).
        var_struct_.erase(bindings[bi]);
        var_subscript_.erase(bindings[bi]);
        var_tuple_.erase(bindings[bi]);
        var_tagged_enum_.erase(bindings[bi]);
        var_tagged_enum_ptr_.erase(bindings[bi]);
        var_dyn_trait_.erase(bindings[bi]);
        var_local_ptrs_.erase(bindings[bi]);
        added.push_back(bindings[bi]);
    }
}

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

// B8 drop elaboration pre-scan: a `let mut x: T;` (declared uninit) needs a
// runtime drop flag iff it has an assignment nested DEEPER than its declaration
// (inside a conditional / loop body) — then its init state isn't statically
// known. Vars assigned only at their declaration depth (straight-line) are
// flag-free (static drop placement). `depth` counts conditional/loop nesting;
// a plain `{ }` block does not increase it (it executes unconditionally).
void MLIRGenImpl::prescan_uninit_flags(lir_view::BlockRef block, int depth,
                                       std::unordered_map<std::string, int>& decl_depth) {
    if (!block) return;
    using C = lir_schema::stmt::Code;
    block.each_stmt([&](lir_view::StmtRef s) {
        switch (s.kind()) {
        case C::Let: {
            lir_view::SLetView v{s};
            if (!v.value())                       // declared WITHOUT initializer
                decl_depth[std::string(v.name())] = depth;
            break;
        }
        case C::Assign: {
            lir_view::SAssignView v{s};
            auto it = decl_depth.find(std::string(v.name()));
            if (it != decl_depth.end() && depth > it->second)
                uninit_flag_needed_.insert(std::string(v.name()));
            break;
        }
        case C::If:
            prescan_uninit_flags(lir_view::SIfView{s}.then_block(), depth + 1, decl_depth);
            prescan_uninit_flags(lir_view::SIfView{s}.else_block(), depth + 1, decl_depth);
            break;
        case C::While:   prescan_uninit_flags(lir_view::SWhileView{s}.body(),   depth + 1, decl_depth); break;
        case C::Loop:    prescan_uninit_flags(lir_view::SLoopView{s}.body(),    depth + 1, decl_depth); break;
        case C::For:     prescan_uninit_flags(lir_view::SForView{s}.body(),     depth + 1, decl_depth); break;
        case C::ForEach: prescan_uninit_flags(lir_view::SForEachView{s}.body(), depth + 1, decl_depth); break;
        case C::Match:
            lir_view::SMatchView{s}.each_arm([&](lir_view::EMatchArmRef arm){
                prescan_uninit_flags(arm.body(), depth + 1, decl_depth);
            });
            break;
        case C::Block:   prescan_uninit_flags(lir_view::SBlockView{s}.body(),   depth,     decl_depth); break;
        case C::LetElse: prescan_uninit_flags(lir_view::SLetElseView{s}.else_block(), depth + 1, decl_depth); break;
        default: break;
        }
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
void MLIRGenImpl::gen_stmt_kind(lir_view::SForEachView v)    { gen_for_each(v); }
void MLIRGenImpl::gen_stmt_kind(lir_view::SBlockView v)      {
    // A destructure-`let` (`let Pair{a,b} = e` / `let (a,b) = e`) lowers to a
    // TRANSPARENT block: `let __dst = e; let a = __dst.0; …`. Its field bindings
    // must LEAK into the enclosing scope — INCLUDING shadowing an existing
    // same-named binding (`let b = 2; let Bar{b,..} = …`). A real `{ }` block
    // restores shadows on exit (B-st-01); doing that here reverted the shadowing
    // field binding to the OUTER alloca → the destructured value was lost. The
    // synthetic `__dst`/`__destruct` temp as the FIRST statement marks the
    // destructure block; for it, skip the scope-restore (the temp leaking is
    // harmless — it's a unique compiler name never referenced after).
    {
        bool first = true, is_destructure = false;
        v.body().each_stmt([&](lir_view::StmtRef s) {
            if (!first) return;
            first = false;
            if (s.kind() == lir_schema::stmt::Code::Let) {
                std::string n(lir_view::SLetView{s}.name());
                if (n.rfind("__dst", 0) == 0 || n.rfind("__destruct", 0) == 0)
                    is_destructure = true;
            }
        });
        if (is_destructure) { gen_block(v.body()); return; }
    }
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

bool MLIRGenImpl::value_needs_drop(TypeRef ty) {
    using K = LogosType::Kind;
    if (!ty) return false;
    auto k = TypeRef(ty).kind();
    if (k == K::Ref || k == K::MutRef || k == K::Ptr) return false;
    // An owning Box<dyn Trait> (value fat-pair, heap-owned data) is droppable;
    // a borrowed &dyn is not. Distinguished by the type's owning bit.
    if (k == K::TraitObject) return TypeRef(ty).owning_trait_object();
    // An owning `Box<[T]>` slice owns its heap buffer (free it; drop elements);
    // a borrowed `&[T]` is not droppable. Distinguished by the slice owning bit.
    if (k == K::Slice) return TypeRef(ty).owning_slice();
    // An owning `Box<Foo>` custom-DST owns its heap block; a borrowed `&Foo` not.
    if (k == K::DstRef) return TypeRef(ty).owning_dst();
    if (k == K::Struct || k == K::ZonedStruct) {
        std::string name = concrete_struct_name(ty);
        if (!resolve_method_symbol(name, "drop").empty()) return true;
        if (auto sd = all_struct_defs_.find(name); sd != all_struct_defs_.end())
            for (auto& f : sd->second->fields)
                if (value_needs_drop(f.type)) return true;
        return false;
    }
    if (k == K::Tuple) {
        for (auto e : TypeRef(ty).tuple_elems()) if (value_needs_drop(e)) return true;
        return false;
    }
    if (k == K::Enum) {
        std::string ename(TypeRef(ty).enum_name());
        if (!resolve_method_symbol(ename, "drop").empty()) return true;
        if (auto* te = resolve_tagged_enum(ename, ty))
            for (auto& vp : te->variants)
                for (auto ft : vp.logos_types) if (value_needs_drop(ft)) return true;
        return false;
    }
    if (k == K::Array) return value_needs_drop(TypeRef(ty).elem());
    // NOTE: Closure is deliberately NOT reported as needs-drop here. A closure
    // value held in a struct field / iterator adapter (MapIter etc.) is stored
    // BY POINTER (one indirection), so a recursive struct-field drop would
    // misinterpret the 8-byte pointer slot as a {fn,env} pair and crash. The
    // closure drop is driven NARROWLY: only the owning `Box<Closure>` path
    // (Box<T>::drop's `let _inner: T = p[0]`, T=Closure, where _inner's value
    // is a direct pointer to the {fn,env} pair) invokes gen_drop_value(Closure)
    // — see mono_clone's __typevar_pending__drop Closure branch + SDrop.
    return false;
}

void MLIRGenImpl::gen_drop_dyn_in_place(mlir::Value fat_ptr) {
    if (!fat_ptr) return;
    auto dyn_struct = dyn_llvm_type();
    if (fat_ptr.getType() == dyn_struct) fat_ptr = spill_to_alloca(fat_ptr);
    if (fat_ptr.getType() != ptr_type()) return;
    auto void_t = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_t   = mlir::LLVM::LLVMFunctionType::get(void_t, {ptr_type()});
    // data = pair->field0, vtable = pair->field1
    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
    auto dpp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, di);
    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dpp);
    // Null-guard (a moved-from / zeroed handle).
    auto null = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
    auto nn = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, data, null);
    auto* region = builder_.getBlock()->getParent();
    auto* then_blk = new mlir::Block();
    auto* cont_blk = new mlir::Block();
    region->push_back(then_blk);
    region->push_back(cont_blk);
    builder_.create<mlir::cf::CondBranchOp>(loc_, nn, then_blk, cont_blk);
    builder_.setInsertionPointToStart(then_blk);
    llvm::SmallVector<mlir::LLVM::GEPArg> vi{int32_t(0), int32_t(1)};
    auto vpp    = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, vi);
    auto vtable = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vpp);
    auto slot0  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vtable);
    llvm::SmallVector<mlir::Value> ops{slot0, data};
    builder_.create<mlir::LLVM::CallOp>(loc_, fn_t, mlir::FlatSymbolRefAttr{},
                                        mlir::ValueRange(ops));
    builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
    builder_.setInsertionPointToStart(cont_blk);
}

void MLIRGenImpl::gen_drop_owning_dyn_handle(mlir::Value fat_ptr,
                                             TypeRef::OwningKind kind) {
    if (!fat_ptr) return;
    // Owning smart-pointer dyn = VALUE fat-pair {data,vtable} stored INLINE;
    // `fat_ptr` points at that 16-byte pair. vtable[0]=drop_in_place(T),
    // vtable[1]=size_of_T, vtable[2]=align_of_T (ptr-encoded). Release is
    // kind-specific:
    //   Box → drop_in_place(data) + free(data).
    //   Rc/Arc → recover RcInner = data − round_up(4, align); decrement strong
    //     (Arc: atomic); at the last reference → drop_in_place(data) + free(RcInner).
    auto dyn_struct = dyn_llvm_type();
    // The binding may be a POINTER to the inline fat pair or the fat-pair VALUE
    // in SSA (`let b = make()` returning {data,vtable} by value) — spill so the
    // GEP-based field reads work uniformly.
    if (fat_ptr.getType() == dyn_struct)
        fat_ptr = spill_to_alloca(fat_ptr);
    if (fat_ptr.getType() != ptr_type()) return;

    auto i32_t  = builder_.getI32Type();
    auto i64_t  = builder_.getI64Type();
    auto void_t = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_t   = mlir::LLVM::LLVMFunctionType::get(void_t, {ptr_type()});

    // data = pair->field0, vtable = pair->field1
    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
    auto dpp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, di);
    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dpp);

    // Null-guard data (a moved-from handle is null).
    auto null = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
    auto nn = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, data, null);
    auto* region = builder_.getBlock()->getParent();
    auto* then_blk = new mlir::Block();
    auto* cont_blk = new mlir::Block();
    region->push_back(then_blk);
    region->push_back(cont_blk);
    builder_.create<mlir::cf::CondBranchOp>(loc_, nn, then_blk, cont_blk);
    builder_.setInsertionPointToStart(then_blk);

    llvm::SmallVector<mlir::LLVM::GEPArg> vi{int32_t(0), int32_t(1)};
    auto vpp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, vi);
    auto vtable = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vpp);

    // drop_in_place(data) — the concrete destructor (vtable slot 0).
    auto run_drop_in_place = [&]() {
        auto slot0 = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vtable);
        llvm::SmallVector<mlir::Value> ops{slot0, data};
        builder_.create<mlir::LLVM::CallOp>(loc_, fn_t, mlir::FlatSymbolRefAttr{},
                                            mlir::ValueRange(ops));
    };

    if (kind == TypeRef::OwningKind::Box) {
        run_drop_in_place();
        call_free(data);                  // single heap block = the boxed concrete
        builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
        builder_.setInsertionPointToStart(cont_blk);
        return;
    }

    // Rc/Arc: RcInner = { i32/AtomicI32 strong, i32/AtomicI32 weak, T val }. val
    // sits at offsetof = round_up(8, align(T)) (two counters); recover the block
    // start from data. align = vtable[2] (ptr-encoded usize) → ptrtoint.
    // NOTE: this Rc<dyn>/Arc<dyn> path frees on strong==0 (no weak bookkeeping);
    // Weak<dyn Trait> is a bounded follow-up — Weak is fully supported for
    // concrete Rc<T>/Arc<T>.
    llvm::SmallVector<mlir::LLVM::GEPArg> ai{int32_t(2)};
    auto app   = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ptr_type(), vtable, ai);
    auto alignp = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), app);
    auto align  = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_t, alignp);
    // off = (8 + align - 1) & ~(align - 1) = (7 + align) & ~(align - 1)
    auto c3   = builder_.create<mlir::arith::ConstantIntOp>(loc_, 7, 64);
    auto c1   = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    auto cN1  = builder_.create<mlir::arith::ConstantIntOp>(loc_, -1, 64);
    auto a3   = builder_.create<mlir::arith::AddIOp>(loc_, align, c3);
    auto am1  = builder_.create<mlir::arith::AddIOp>(loc_, align, cN1);
    auto mask = builder_.create<mlir::arith::XOrIOp>(loc_, am1, cN1);  // ~(align-1)
    auto off  = builder_.create<mlir::arith::AndIOp>(loc_, a3, mask);
    mlir::Value data_i = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_t, data);
    mlir::Value inner_i = builder_.create<mlir::arith::SubIOp>(loc_, data_i, off);
    mlir::Value inner = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), inner_i);

    // Decrement strong (i32 at inner, offset 0). is_last = reached zero.
    mlir::Value is_last;
    if (kind == TypeRef::OwningKind::Arc) {
        auto one32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 32);
        auto prev = builder_.create<mlir::LLVM::AtomicRMWOp>(
            loc_, mlir::LLVM::AtomicBinOp::sub, inner, one32,
            mlir::LLVM::AtomicOrdering::seq_cst);
        auto one32b = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 32);
        is_last = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::eq, prev, one32b);  // old == 1 → now 0
    } else {
        auto cur = builder_.create<mlir::LLVM::LoadOp>(loc_, i32_t, inner);
        auto one32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 32);
        auto dec = builder_.create<mlir::arith::SubIOp>(loc_, cur, one32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, dec, inner);
        auto zero32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        is_last = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::eq, dec, zero32);
    }

    auto* last_blk = new mlir::Block();
    region->push_back(last_blk);
    builder_.create<mlir::cf::CondBranchOp>(loc_, is_last, last_blk, cont_blk);
    builder_.setInsertionPointToStart(last_blk);
    run_drop_in_place();   // drop T
    call_free(inner);      // free the whole RcInner block
    builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
    builder_.setInsertionPointToStart(cont_blk);
}

void MLIRGenImpl::gen_drop_owning_dst(mlir::Value dst_ptr, TypeRef ty) {
    using K = LogosType::Kind;
    if (!dst_ptr) return;
    auto stype = slice_llvm_type();   // DstRef value = {data, len} (slice repr)
    // A DstRef local is an alloca-binding (logos_to_mlir(DstRef)=ptr): `dst_ptr`
    // is an alloca holding the DstRef VALUE, which is a pointer to the 16-byte
    // {data,len} storage. Load once to reach that storage. (A by-VALUE fat pair
    // in SSA is spilled instead.)
    if (dst_ptr.getType() == stype) {
        dst_ptr = spill_to_alloca(dst_ptr);
    } else if (dst_ptr.getType() == ptr_type()) {
        dst_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dst_ptr);
    }
    if (dst_ptr.getType() != ptr_type()) return;
    auto i64_t = builder_.getI64Type();
    llvm::SmallVector<mlir::LLVM::GEPArg> i0{int32_t(0), int32_t(0)};
    llvm::SmallVector<mlir::LLVM::GEPArg> i1{int32_t(0), int32_t(1)};
    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(),
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, dst_ptr, i0));
    auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, i64_t,
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, dst_ptr, i1));
    auto null = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
    auto nn = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, data, null);
    auto* region   = builder_.getBlock()->getParent();
    auto* then_blk = new mlir::Block();   // data != null: drop members
    auto* free_blk = new mlir::Block();   // free the heap block
    auto* cont_blk = new mlir::Block();
    region->push_back(then_blk);
    region->push_back(free_blk);
    region->push_back(cont_blk);
    builder_.create<mlir::cf::CondBranchOp>(loc_, nn, then_blk, cont_blk);
    builder_.setInsertionPointToStart(then_blk);

    // `data` points at the struct base (prefix fields at their layout offsets,
    // the tail slice at the last field). Drop droppable prefix fields + tail
    // elements; control then reaches free_blk which releases the whole block.
    std::string name(TypeRef(ty).struct_name());
    auto sdit = all_struct_defs_.find(name);
    auto sit  = struct_types_.find(name);
    if (sdit != all_struct_defs_.end() && sit != struct_types_.end() &&
        !sdit->second->fields.empty()) {
        auto& def  = *sdit->second;
        auto& info = sit->second;
        size_t last = def.fields.size() - 1;
        for (size_t i = 0; i < last; ++i) {
            TypeRef ft(def.fields[i].type);
            if (!ft) continue;
            auto fk = TypeRef(ft).kind();
            if (fk == K::Ref || fk == K::MutRef || fk == K::Ptr) continue;
            if (!value_needs_drop(ft)) continue;
            if (auto fp = gep_field(data, info, std::string(def.fields[i].name)))
                gen_drop_value(fp, ft);
        }
        // Tail: last field is the unsized slice; drop its elements (runtime len).
        TypeRef tailt(def.fields[last].type);
        TypeRef et = tailt ? TypeRef(tailt).elem() : TypeRef{};
        if (et && value_needs_drop(et)) {
            if (auto base = gep_field(data, info, std::string(def.fields[last].name))) {
                uint64_t stride = layout_of(et).size; if (!stride) stride = 1;
                auto strideC = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)stride, 64);
                auto* cond_blk = new mlir::Block();
                auto* body_blk = new mlir::Block();
                cond_blk->addArgument(i64_t, loc_);
                region->push_back(cond_blk);
                region->push_back(body_blk);
                auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
                builder_.create<mlir::cf::BranchOp>(loc_, cond_blk, mlir::ValueRange{zero});
                builder_.setInsertionPointToStart(cond_blk);
                auto iv = cond_blk->getArgument(0);
                auto ltc = builder_.create<mlir::LLVM::ICmpOp>(
                    loc_, mlir::LLVM::ICmpPredicate::slt, iv, len);
                builder_.create<mlir::cf::CondBranchOp>(
                    loc_, ltc, body_blk, mlir::ValueRange{}, free_blk, mlir::ValueRange{});
                builder_.setInsertionPointToStart(body_blk);
                auto off = builder_.create<mlir::arith::MulIOp>(loc_, iv, strideC);
                llvm::SmallVector<mlir::LLVM::GEPArg> ei{off.getResult()};
                auto ep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), builder_.getI8Type(), base, ei);
                gen_drop_value(ep, et);
                auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
                auto inc = builder_.create<mlir::arith::AddIOp>(loc_, iv, one);
                builder_.create<mlir::cf::BranchOp>(loc_, cond_blk, mlir::ValueRange{inc.getResult()});
            }
        }
    }
    // Whatever member-drop path we took, fall through to free (unless the tail
    // loop already wired its exit straight to free_blk).
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::cf::BranchOp>(loc_, free_blk);
    builder_.setInsertionPointToStart(free_blk);
    call_free(data);
    builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
    builder_.setInsertionPointToStart(cont_blk);
}

void MLIRGenImpl::gen_drop_owning_slice(mlir::Value slice_ptr, TypeRef ty) {
    if (!slice_ptr) return;
    auto stype = slice_llvm_type();   // { ptr, i64 }
    if (slice_ptr.getType() == stype) slice_ptr = spill_to_alloca(slice_ptr);
    if (slice_ptr.getType() != ptr_type()) return;
    auto i64_t = builder_.getI64Type();
    // data = field0, len = field1.
    llvm::SmallVector<mlir::LLVM::GEPArg> i0{int32_t(0), int32_t(0)};
    llvm::SmallVector<mlir::LLVM::GEPArg> i1{int32_t(0), int32_t(1)};
    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(),
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice_ptr, i0));
    auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, i64_t,
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice_ptr, i1));
    // Null-guard (a moved-from slice is null).
    auto null = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
    auto nn = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, data, null);
    auto* region = builder_.getBlock()->getParent();
    auto* then_blk = new mlir::Block();
    auto* cont_blk = new mlir::Block();
    region->push_back(then_blk);
    region->push_back(cont_blk);
    builder_.create<mlir::cf::CondBranchOp>(loc_, nn, then_blk, cont_blk);
    builder_.setInsertionPointToStart(then_blk);

    TypeRef et = TypeRef(ty).elem();
    if (et && value_needs_drop(et)) {
        // Runtime loop: for i in [0,len) drop element at data + i*stride.
        uint64_t stride = layout_of(et).size; if (!stride) stride = 1;
        auto strideC = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)stride, 64);
        auto* cond_blk = new mlir::Block();
        auto* body_blk = new mlir::Block();
        auto* exit_blk = new mlir::Block();
        cond_blk->addArgument(i64_t, loc_);
        region->push_back(cond_blk);
        region->push_back(body_blk);
        region->push_back(exit_blk);
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        builder_.create<mlir::cf::BranchOp>(loc_, cond_blk, mlir::ValueRange{zero});
        builder_.setInsertionPointToStart(cond_blk);
        auto iv = cond_blk->getArgument(0);
        auto lt = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::slt, iv, len);
        builder_.create<mlir::cf::CondBranchOp>(
            loc_, lt, body_blk, mlir::ValueRange{}, exit_blk, mlir::ValueRange{});
        builder_.setInsertionPointToStart(body_blk);
        auto off = builder_.create<mlir::arith::MulIOp>(loc_, iv, strideC);
        llvm::SmallVector<mlir::LLVM::GEPArg> ei{off.getResult()};
        auto ep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), builder_.getI8Type(), data, ei);
        gen_drop_value(ep, et);
        auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
        auto inc = builder_.create<mlir::arith::AddIOp>(loc_, iv, one);
        builder_.create<mlir::cf::BranchOp>(loc_, cond_blk, mlir::ValueRange{inc.getResult()});
        builder_.setInsertionPointToStart(exit_blk);
        call_free(data);
        builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
    } else {
        call_free(data);
        builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
    }
    builder_.setInsertionPointToStart(cont_blk);
}

mlir::Value MLIRGenImpl::gen_clone_owning_dyn(const LExpr* recv_le, TypeRef recv_t) {
    auto recv = gen_expr(*recv_le);
    if (!recv) return nullptr;
    auto dyn_struct = dyn_llvm_type();
    // Peel a &(Rc<dyn>) receiver; recover the OwningKind (Rc vs Arc).
    TypeRef ot = recv_t;
    if (ot && (ot.kind() == LogosType::Kind::Ref || ot.kind() == LogosType::Kind::MutRef)
        && ot.pointee())
        ot = ot.pointee();
    auto kind = TypeRef(ot).trait_owning_kind();

    // fat_ptr → pointer to the {data,vtable} pair (spill an SSA value).
    mlir::Value fat_ptr = recv;
    if (recv.getType() == dyn_struct) fat_ptr = spill_to_alloca(recv);
    if (fat_ptr.getType() != ptr_type()) return recv;  // unexpected shape — pass through

    auto i32_t = builder_.getI32Type();
    auto i64_t = builder_.getI64Type();
    // data = field0, vtable = field1; align = vtable[2] (ptr-encoded usize).
    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(),
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, di));
    llvm::SmallVector<mlir::LLVM::GEPArg> vi{int32_t(0), int32_t(1)};
    auto vtable = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(),
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, fat_ptr, vi));
    llvm::SmallVector<mlir::LLVM::GEPArg> ai{int32_t(2)};
    auto alignp = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(),
        builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ptr_type(), vtable, ai));
    mlir::Value align = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_t, alignp);
    // inner = data − round_up(4, align) = data − ((3 + align) & ~(align - 1)).
    auto c3  = builder_.create<mlir::arith::ConstantIntOp>(loc_, 3, 64);
    auto cN1 = builder_.create<mlir::arith::ConstantIntOp>(loc_, -1, 64);
    mlir::Value a3   = builder_.create<mlir::arith::AddIOp>(loc_, align, c3);
    mlir::Value am1  = builder_.create<mlir::arith::AddIOp>(loc_, align, cN1);
    mlir::Value mask = builder_.create<mlir::arith::XOrIOp>(loc_, am1, cN1);
    mlir::Value off  = builder_.create<mlir::arith::AndIOp>(loc_, a3, mask);
    mlir::Value data_i  = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_t, data);
    mlir::Value inner_i = builder_.create<mlir::arith::SubIOp>(loc_, data_i, off);
    mlir::Value inner   = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), inner_i);

    // Bump strong (i32 at inner, offset 0). Arc → atomic.
    auto one32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 32);
    if (kind == TypeRef::OwningKind::Arc) {
        builder_.create<mlir::LLVM::AtomicRMWOp>(
            loc_, mlir::LLVM::AtomicBinOp::add, inner, one32,
            mlir::LLVM::AtomicOrdering::seq_cst);
    } else {
        auto cur = builder_.create<mlir::LLVM::LoadOp>(loc_, i32_t, inner);
        mlir::Value inc = builder_.create<mlir::arith::AddIOp>(loc_, cur, one32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, inc, inner);
    }
    // The clone IS a copy of the same fat pair (shared data + vtable).
    return builder_.create<mlir::LLVM::LoadOp>(loc_, dyn_struct, fat_ptr);
}

void MLIRGenImpl::gen_drop_value(mlir::Value value_ptr, TypeRef ty, bool top_level) {
    using K = LogosType::Kind;
    if (!value_ptr || !ty) return;
    auto k = TypeRef(ty).kind();
    if (k == K::Ref || k == K::MutRef || k == K::Ptr) return;
    // Owning Box<dyn Trait> (value fat-pair): value_ptr points at the inline
    // {data,vtable} pair. Run vtable[0] drop_in_place(data) + free(data). This
    // is what makes the Box<dyn> drop UNIFORM across every storage site —
    // struct field, return temp, Vec element, tuple/array — via the normal
    // aggregate field-recursion, not just a tagged top-level local.
    if (k == K::TraitObject) {
        if (TypeRef(ty).owning_trait_object())
            gen_drop_owning_dyn_handle(value_ptr, TypeRef(ty).trait_owning_kind());
        return;
    }
    // Owning `Box<[T]>` fat slice: drop elements (if droppable) + free buffer.
    if (k == K::Slice) {
        if (TypeRef(ty).owning_slice())
            gen_drop_owning_slice(value_ptr, ty);
        return;
    }
    // Owning `Box<Foo>` custom-DST: drop prefix fields + tail elements + free.
    if (k == K::DstRef) {
        if (TypeRef(ty).owning_dst())
            gen_drop_owning_dst(value_ptr, ty);
        return;
    }
    // Closure drop is driven explicitly (Box<Closure>), not via value_needs_drop
    // (which reports false for Closure to avoid struct-field over-recursion).
    if (k != K::Closure && !value_needs_drop(ty)) return;
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    // Child VALUE ptr from an aggregate ptr + slot index: inline children
    // (struct/tuple/array) → the GEP; heap children (enum heap ptr) → load it.
    // Enum value-repr: a nested enum child is INLINE — its storage is the GEP
    // itself (no heap-ptr load), exactly like a struct/tuple/array child.
    auto child_value_ptr = [&](mlir::Value agg, mlir::Type agg_ty, int idx, K /*ck*/) -> mlir::Value {
        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(idx)};
        return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), agg_ty, agg, gi);
    };
    if (k == K::Struct || k == K::ZonedStruct) {
        std::string name = concrete_struct_name(ty);
        // A user `impl Drop` OWNS the value: calling its drop runs the destructor
        // and (for a by-value `self` drop) consumes the fields, which drop at the
        // drop body's scope end. So call it and STOP — recursing the fields here
        // too would double-drop them (drop_glue_three_levels). Only a DROP-LESS
        // struct recurses its fields. Mirrors the enum branch.
        if (auto ds = resolve_method_symbol(name, "drop"); !ds.empty())
            if (auto fn = mod.lookupSymbol<mlir::func::FuncOp>(ds)) {
                builder_.create<mlir::func::CallOp>(loc_, fn, mlir::ValueRange{value_ptr});
                // Owner (top_level) also drops the fields after the user drop
                // (mirrors SDrop). Nested: stop (by-value self consumes them).
                if (!top_level) return;
            }
        auto sdit = all_struct_defs_.find(name);
        auto sit  = struct_types_.find(mlir_struct_key(ty));
        if (sit == struct_types_.end()) sit = struct_types_.find(name);
        if (sdit != all_struct_defs_.end() && sit != struct_types_.end()) {
            auto& info = sit->second;
            auto& def  = *sdit->second;
            for (int i = (int)def.fields.size() - 1; i >= 0; --i) {
                TypeRef ft(def.fields[i].type);
                auto fk = ft ? TypeRef(ft).kind() : K::Error;
                if (!ft || fk == K::Ref || fk == K::MutRef || fk == K::Ptr) continue;
                if (!value_needs_drop(ft)) continue;
                auto fp = gep_field(value_ptr, info, std::string(def.fields[i].name));
                if (!fp) continue;
                // Enum value-repr: a nested enum field is inline — drop on the GEP.
                gen_drop_value(fp, ft);
            }
        }
        return;
    }
    if (k == K::Tuple) {
        auto ttype = tuple_llvm_type(ty);
        auto elems = TypeRef(ty).tuple_elems();
        if (ttype)
            for (int i = (int)elems.size() - 1; i >= 0; --i) {
                TypeRef et(elems[i]);
                auto ek = et ? TypeRef(et).kind() : K::Error;
                if (!et || ek == K::Ref || ek == K::MutRef || ek == K::Ptr) continue;
                if (!value_needs_drop(et)) continue;
                gen_drop_value(child_value_ptr(value_ptr, ttype, i, ek), et);
            }
        return;
    }
    if (k == K::Enum) {
        std::string ename(TypeRef(ty).enum_name());
        // A REAL user enum Drop (by-value self) consumes the payload itself —
        // call it and stop. resolve_method_symbol can return a non-existent
        // symbol for an enum with NO user Drop (false positive), so require the
        // symbol to actually EXIST before treating it as a user drop; otherwise
        // fall through to the variant-switched payload recursion (G158-4 fix).
        if (auto ds = resolve_method_symbol(ename, "drop"); !ds.empty())
            if (auto fn = mod.lookupSymbol<mlir::func::FuncOp>(ds)) {
                builder_.create<mlir::func::CallOp>(loc_, fn, mlir::ValueRange{value_ptr});
                if (!top_level) return;
            }
        auto* te = resolve_tagged_enum(ename, ty);
        if (!te) return;
        std::vector<const TaggedEnumInfo::VariantPayload*> dvs;
        for (auto& vp : te->variants) {
            for (auto ft : vp.logos_types)
                if (ft && value_needs_drop(ft)) { dvs.push_back(&vp); break; }
        }
        if (dvs.empty()) return;
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), te->llvm_type, value_ptr, di);
        auto disc = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), te->llvm_type, value_ptr, pi);
        auto* region = builder_.getBlock()->getParent();
        for (auto* vp : dvs) {
            auto pay = variant_payload_struct(*vp);
            auto dc = builder_.create<mlir::arith::ConstantIntOp>(loc_, vp->disc, 32);
            auto eq = builder_.create<mlir::arith::CmpIOp>(
                loc_, mlir::arith::CmpIPredicate::eq, disc, dc);
            auto* then_blk = new mlir::Block();
            auto* cont_blk = new mlir::Block();
            region->push_back(then_blk);
            region->push_back(cont_blk);
            builder_.create<mlir::cf::CondBranchOp>(loc_, eq, then_blk, cont_blk);
            builder_.setInsertionPointToStart(then_blk);
            for (size_t fi = 0; fi < vp->logos_types.size(); ++fi) {
                TypeRef ft(vp->logos_types[fi]);
                auto fk = ft ? TypeRef(ft).kind() : K::Error;
                if (!ft || fk == K::Ref || fk == K::MutRef || fk == K::Ptr) continue;
                if (!value_needs_drop(ft)) continue;
                gen_drop_value(child_value_ptr(pay_ptr, pay, (int)fi, fk), ft);
            }
            builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
            builder_.setInsertionPointToStart(cont_blk);
        }
        return;
    }
    if (k == K::Array) {
        TypeRef et = TypeRef(ty).elem();
        auto ek = et ? TypeRef(et).kind() : K::Error;
        if (!et || ek == K::Ref || ek == K::MutRef || ek == K::Ptr) return;
        if (!value_needs_drop(et)) return;
        auto atype = logos_to_mlir(ty);
        uint64_t n = TypeRef(ty).arr_size();
        for (uint64_t i = 0; i < n; ++i)
            gen_drop_value(child_value_ptr(value_ptr, atype, (int)i, ek), et);
        return;
    }
    if (k == K::Closure) {
        // value_ptr → 16-byte {fn, env}. Drop = run the env's drop glue.
        //   env = closure[1]; if (env != null) { g = env[0];
        //                                         if (g != null) g(env); }
        auto ctype = closure_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
        auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, value_ptr, ei);
        auto env = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), ep);
        auto null = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
        auto env_nn = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::ne, env, null);
        auto* region = builder_.getBlock()->getParent();
        auto* e_then = new mlir::Block();
        auto* e_cont = new mlir::Block();
        region->push_back(e_then);
        region->push_back(e_cont);
        builder_.create<mlir::cf::CondBranchOp>(loc_, env_nn, e_then, e_cont);
        builder_.setInsertionPointToStart(e_then);
        // glue = env[0]
        auto glue = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), env);
        auto g_nn = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::ne, glue, null);
        auto* g_then = new mlir::Block();
        auto* g_cont = new mlir::Block();
        region->push_back(g_then);
        region->push_back(g_cont);
        builder_.create<mlir::cf::CondBranchOp>(loc_, g_nn, g_then, g_cont);
        builder_.setInsertionPointToStart(g_then);
        auto void_t = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        auto fn_t = mlir::LLVM::LLVMFunctionType::get(void_t, {ptr_type()}, false);
        llvm::SmallVector<mlir::Value> ops{glue, env};
        builder_.create<mlir::LLVM::CallOp>(loc_, fn_t, mlir::FlatSymbolRefAttr{},
                                            mlir::ValueRange(ops));
        builder_.create<mlir::cf::BranchOp>(loc_, g_cont);
        builder_.setInsertionPointToStart(g_cont);
        builder_.create<mlir::cf::BranchOp>(loc_, e_cont);
        builder_.setInsertionPointToStart(e_cont);
        return;
    }
}

void MLIRGenImpl::gen_stmt_kind(lir_view::SDropView v) {
    std::string var_name(v.var_name());
    auto it = scope_.find(var_name);
    if (it == scope_.end()) return;
    // The full drop body, captured so a B8 drop-flag var can run it conditionally.
    auto emit_body = [&]() {
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // 0. Owning smart-pointer dyn (sema sentinel `__box_dyn__drop`): the
    //    binding stores a VALUE fat pair {data,vtable}. Release is kind-specific
    //    (Box→free data; Rc/Arc→dec strong + free RcInner at the last ref);
    //    the kind is read from the binding's TraitObject type.
    if (std::string(v.drop_fn()) == "__box_dyn__drop") {
        gen_drop_owning_dyn_handle(it->second, v.type(pool_impl()).trait_owning_kind());
        return;
    }
    // Move-out drop of an unsized `dyn` tail (`let _v: T = self.inner.val`,
    // T = dyn): the binding holds a `&dyn` handle (ptr to the {data,vtable}
    // pair). Run the concrete Drop via vtable[0](data) only — NO free (the
    // enclosing block is freed separately by drop_rc's `free(self.inner)`).
    if (std::string(v.drop_fn()) == "__dyn_drop_in_place__") {
        mlir::Value fp = it->second;
        if (let_vars_.count(var_name))
            fp = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
        gen_drop_dyn_in_place(fp);
        return;
    }

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

    // 2. Recursively drop the var's owned sub-values — struct fields, tuple
    //    elements, enum variant payload, AND array elements — generalized via
    //    gen_drop_value, which handles arbitrary nesting (G158-4). Step 1 above
    //    already ran the var's OWN user drop (drop_fn), so here we recurse its
    //    children. Fields/elements moved out (recorded in moved_fields) are
    //    skipped so a value isn't released twice.
    if (TypeRef st = v.type(pool_impl()); v.drop_fields() && st) {
        using K = LogosType::Kind;
        std::set<std::string> moved;
        v.each_moved_field([&](std::string_view fn){ moved.emplace(fn); });
        auto k = st.kind();
        if (k == K::Struct || k == K::ZonedStruct) {
            std::string name = concrete_struct_name(st);
            auto sdit = all_struct_defs_.find(name);
            if (sdit == all_struct_defs_.end())
                sdit = all_struct_defs_.find(std::string(st.struct_name()));
            auto sit = struct_types_.find(mlir_struct_key(st));
            if (sit == struct_types_.end()) sit = struct_types_.find(name);
            if (sit == struct_types_.end()) sit = struct_types_.find(std::string(st.struct_name()));
            if (sdit != all_struct_defs_.end() && sit != struct_types_.end()) {
                auto& info = sit->second;
                auto& def  = *sdit->second;
                for (int i = (int)def.fields.size() - 1; i >= 0; --i) {
                    if (moved.count(std::string(def.fields[i].name))) continue;
                    TypeRef ft(def.fields[i].type);
                    auto fk = ft ? TypeRef(ft).kind() : K::Error;
                    if (!ft || fk == K::Ref || fk == K::MutRef || fk == K::Ptr) continue;
                    if (!value_needs_drop(ft)) continue;
                    auto fp = gep_field(it->second, info, std::string(def.fields[i].name));
                    if (!fp) continue;
                    // Enum value-repr: a nested enum field is inline — drop on the GEP.
                    gen_drop_value(fp, ft);
                }
            }
        } else if (k == K::Tuple) {
            auto ttype = tuple_llvm_type(st);
            auto elems = st.tuple_elems();
            if (ttype)
                for (int i = (int)elems.size() - 1; i >= 0; --i) {
                    if (moved.count(std::to_string(i))) continue;
                    TypeRef et(elems[i]);
                    auto ek = et ? TypeRef(et).kind() : K::Error;
                    if (!et || ek == K::Ref || ek == K::MutRef || ek == K::Ptr) continue;
                    if (!value_needs_drop(et)) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(i)};
                    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, it->second, gi);
                    // Enum value-repr: a nested enum element is inline — drop on the GEP.
                    gen_drop_value(gep, et);
                }
        } else if (k == K::Enum && drop_fn.empty()) {
            // Enum value-repr: the slot IS the inline {disc,payload} storage
            // (one level, like a Struct). gen_drop_value does the variant-switch
            // + payload recursion directly on it — no ptr load.
            gen_drop_value(it->second, st);
        } else if (k == K::Array) {
            // Inline array: it->second points at the `[T; N]` storage.
            gen_drop_value(it->second, st);
        } else if (k == K::Closure) {
            // Owned closure value: it->second points at the {fn, env} pair.
            // gen_drop_value runs the env's drop glue (null-guarded no-op for
            // non-owning closures).
            gen_drop_value(it->second, st);
        } else if (k == K::Slice && st.owning_slice()) {
            // Owning `Box<[T]>` fat slice: it->second points at {data,len}.
            // Drop elements (if droppable) + free the heap buffer.
            gen_drop_owning_slice(it->second, st);
        } else if (k == K::DstRef && st.owning_dst()) {
            // Owning `Box<Foo>` custom-DST: drop prefix fields + tail + free.
            gen_drop_owning_dst(it->second, st);
        }
    }
    };  // end emit_body

    // B8 dynamic drop flag: a declared-uninit var only runs its destructor if
    // it currently holds a live value (flag==1) — an early `return` before the
    // first assignment, or the !c path of a conditional init, leaves it 0 → the
    // drop is a no-op (never runs the destructor on garbage).
    auto fit = uninit_drop_flag_.find(var_name);
    if (fit != uninit_drop_flag_.end()) {
        auto i8t  = builder_.getI8Type();
        auto flag = builder_.create<mlir::LLVM::LoadOp>(loc_, i8t, fit->second);
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 8);
        auto live = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::ne, flag, zero);
        auto* region   = builder_.getBlock()->getParent();
        auto* then_blk = new mlir::Block();
        auto* cont_blk = new mlir::Block();
        region->push_back(then_blk);
        region->push_back(cont_blk);
        builder_.create<mlir::cf::CondBranchOp>(loc_, live, then_blk, cont_blk);
        builder_.setInsertionPointToStart(then_blk);
        emit_body();
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
        builder_.setInsertionPointToStart(cont_blk);
        return;
    }
    // B8 static-uninit var: drop only if it currently holds a live value at this
    // codegen point (statically tracked); an early return before the first
    // assignment, or a never-assigned var, drops nothing.
    if (uninit_static_.count(var_name)) {
        if (uninit_assigned_.count(var_name)) emit_body();
        return;
    }
    emit_body();
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
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, size_const(pointee_t), /*isVolatile=*/false);
            return;
        }
    }
    // Tuple pointee (`*&s.t = (..)` / `*p = tuple`): the tuple is stored INLINE
    // by value; logos_to_mlir(Tuple) collapses to an 8-byte ptr so the aggregate
    // branch below misses it — memcpy the full tuple footprint here.
    if (pointee_t && TypeRef(pointee_t).kind() == LogosType::Kind::Tuple &&
        val.getType() == ptr_type()) {
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, size_const(pointee_t), /*isVolatile=*/false);
        return;
    }
    // General aggregate store-by-value: any LLVM aggregate (tuple, embedded
    // datatype, fixed-array-as-struct) whose rhs is a `ptr` must be copied by
    // VALUE, not by storing the 8-byte pointer into the wider slot (the latter
    // silently corrupts). Mirrors gen_chain_field_write's final-field handling
    // — the prerequisite for routing chain/aggregate writes through this path.
    if (val.getType() == ptr_type() &&
        (mlir::isa<mlir::LLVM::LLVMStructType>(elem_type) ||
         mlir::isa<mlir::LLVM::LLVMArrayType>(elem_type))) {
        auto sz = (val_le && val_le->type) ? size_const(val_le->type)
                                           : size_const(pointee_t);
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, sz, /*isVolatile=*/false);
        return;
    }
    // Closure value store-by-value: a closure passes by pointer at the ABI but
    // its backing storage is a 16-byte fat handle ({fnptr, env}). A deref-write
    // of a closure (e.g. the place-path `p[0] = cl` in Box::box_new once
    // index_write is retired) must memcpy 16 bytes, else only the fnptr half is
    // copied and the box holds a dangling stack pointer → call SIGSEGV.
    // Closure AND Slice: a slice is now stored INLINE as a 16-byte {ptr,len}
    // fat pair everywhere (fields/locals/elements), so a deref-write to a
    // slice place (`self.rest = <slice>`, lowered via the place path to
    // `*&self.rest = v`) must memcpy all 16 bytes — a plain 8-byte store of
    // the data ptr would leave `len` stale (the split-iterator bug).
    // TraitObject memcpy-16 ONLY when the DESTINATION place is itself a bare
    // fat dyn (`*mut dyn`/`&dyn` field or deref — pointee kind TraitObject), as
    // in persistent's `(*slot).p = handle`. A Box<dyn> / raw escape handle is
    // still an 8-byte slot (not yet migrated) whose place pointee is NOT a bare
    // TraitObject — memcpy-16 there reads OOB past the 8-byte source and
    // corrupts neighbours (the b167/b168 dyn regressions).
    bool dst_is_fat_dyn = pointee_t &&
        TypeRef(pointee_t).kind() == LogosType::Kind::TraitObject;
    if (val.getType() == ptr_type() && val_le->type &&
        (TypeRef(val_le->type).kind() == LogosType::Kind::Closure ||
         TypeRef(val_le->type).kind() == LogosType::Kind::Slice ||
         (TypeRef(val_le->type).kind() == LogosType::Kind::TraitObject &&
          dst_is_fat_dyn))) {
        auto sz = builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(), builder_.getI64IntegerAttr(16));
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, sz, /*isVolatile=*/false);
        return;
    }
    // Enum value-repr: a `*p = enum_val` write where p is `&mut Enum` /
    // `*mut Enum` copies the enum's inline {disc,payload} footprint INTO the
    // destination storage (one level, like the struct memcpy above) — NOT a
    // heap-ptr store. This is how `Option::take`/`replace`'s `*self = None`
    // mutates the caller's binding through the inline storage. (C-like enum is
    // an i32 disc value — falls to the scalar store below.)
    {
        TypeRef pe = (pt && pt.pointee()) ? pt.pointee() : TypeRef(nullptr);
        TypeRef vlt(val_le->type);
        if (pe && TypeRef(pe).kind() == LogosType::Kind::Enum &&
            vlt && TypeRef(vlt).kind() == LogosType::Kind::Enum &&
            val.getType() == ptr_type()) {
            if (resolve_tagged_enum(std::string(TypeRef(vlt).enum_name()), vlt)) {
                builder_.create<mlir::LLVM::MemcpyOp>(loc_, ptr, val, size_const(vlt), false);
                return;
            }
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
    if (!val_le) {
        // B3-bg-01 / B3-bg-02: declare-without-init (`let v: T;`).
        // Allocate the slot from the annotated type; leave it
        // uninitialised. Later SAssign writes through scope_[name].
        std::string nm(v.name());
        TypeRef ty = v.type(pool_impl());
        auto mt = ty ? logos_to_mlir(ty) : mlir::Type();
        if (!mt) return;
        auto alloca = create_entry_alloca(mt);
        scope_[nm] = alloca;
        let_vars_.insert(nm);
        var_elem_types_[nm] = mt;
        if (ty && TypeRef(ty).kind() == LogosType::Kind::Struct)
            var_struct_[nm] = mlir_struct_key(ty);
        // B8 drop elaboration: a fresh declaration resets any stale assigned /
        // flag state from an earlier same-named binding (sequential blocks).
        uninit_assigned_.erase(nm);
        if (uninit_flag_needed_.count(nm)) {
            // Init state not statically known (conditional/loop assignment): a
            // runtime drop flag (init 0) decides drop-before-replace + scope-exit
            // drop. The flag-init store sits HERE (re-runs each loop iteration if
            // the decl is in a loop body → correct per-iteration reset).
            auto flag = create_entry_alloca(builder_.getI8Type());
            builder_.create<mlir::LLVM::StoreOp>(
                loc_, builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 8), flag);
            uninit_drop_flag_[nm] = flag;
        } else {
            // Every assignment statically dominates: track init via uninit_assigned_
            // during codegen, place drops statically (no flag, no branch).
            uninit_drop_flag_.erase(nm);
            uninit_static_.insert(nm);
        }
        return;
    }
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
        auto alloca = gen_arr_lit(lit, elem_type,
                                  s.type ? TypeRef(s.type).elem() : TypeRef(nullptr));
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
    // logos-core 1.4: FnItem (the per-instantiation ZST produced at bare-fn
    // refs) lowers identically to FnPtr at codegen — same ptr representation.
    if (s.type && LogosType::is_fn_value_kind(TypeRef(s.type).kind())) {
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
        // Slice-return-by-value: a 16-byte {ptr,len} struct value (e.g. a call
        // result that wasn't routed through the dispatcher spill) must be spilled
        // to a stack slot so the binding holds a pointer-to-{ptr,len} like every
        // other slice place.
        if (mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()))
            val = spill_to_alloca(val);
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
            // Enum value-repr: `scope_` holds the inline storage ptr directly
            // (one level, like a Struct). The RHS `val` may ALIAS another place's
            // storage (e.g. `let old = p[0]` in mem::replace, `let b = a`, a
            // payload-extract GEP). Binding the alias directly is wrong: a later
            // write to the source (`p[0] = src`) clobbers this binding. Copy the
            // enum's inline {disc,payload} footprint into a FRESH slot so the
            // binding is independent — mirrors the struct `let` memcpy. Move-only
            // soundness (no double-free) is handled by sema's moved_vars_ /
            // moved_fields marking the source's drop skipped. (The body size is
            // finalized by the fixpoint, so getTypeSize is correct here.)
            if (val.getType() == ptr_type()) {
                auto fresh = create_entry_alloca(te->llvm_type);
                builder_.create<mlir::LLVM::MemcpyOp>(loc_, fresh, val, size_const(s.type), /*isVolatile=*/false);
                val = fresh;
            }
            scope_[s.name] = val;
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
            auto fresh = create_entry_alloca(sit->second.llvm_type);
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, fresh, val, size_const(s.type), /*isVolatile=*/false);
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
    if (_peeled_st &&
        (_peeled_st.kind() == LogosType::Kind::Ptr ||
         _peeled_st.kind() == LogosType::Kind::Ref ||
         _peeled_st.kind() == LogosType::Kind::MutRef) &&
        TypeRef(_peeled_st.pointee()).kind() == LogosType::Kind::TraitObject) {
        _peeled_st = _peeled_st.pointee();
    }
    if (TypeRef st(_peeled_st); st && st.kind() == LogosType::Kind::TraitObject) {
        // Is the declared type a RAW pointer to dyn (`*const/*mut dyn Trait` =
        // Ptr<TraitObject>)? Only these are subject to a later `*z` deref, and
        // only these need the handle-vs-ptr-to-handle provenance bookkeeping.
        bool is_raw_ptr_dyn =
            TypeRef(s.type) && TypeRef(s.type).kind() == LogosType::Kind::Ptr &&
            TypeRef(s.type).pointee() &&
            TypeRef(TypeRef(s.type).pointee()).kind() == LogosType::Kind::TraitObject;
        auto data_ptr = gen_expr(*s.value);
        if (!data_ptr) return;
        mlir::Value alloca;
        TypeRef src_vt(s.value->type);
        // Source may also be `*mut dyn Trait` / `&dyn Trait` (e.g. a value
        // returned by `Vec::borrow(i) -> &T` where T is a trait object) — peel
        // for the "already fat" shortcut so we store the existing handle/ref
        // directly instead of mis-rebuilding a fat slot from it (G168-A g6/g2:
        // the missing Ref/MutRef peel here made `let rd: &Box<dyn> = v.borrow(0)`
        // store a bogus {ptr-to-handle, garbage-vtable} slot → dispatch crash).
        if (src_vt && (src_vt.kind() == LogosType::Kind::Ptr ||
                       src_vt.kind() == LogosType::Kind::Ref ||
                       src_vt.kind() == LogosType::Kind::MutRef) &&
            src_vt.pointee() &&
            TypeRef(src_vt.pointee()).kind() == LogosType::Kind::TraitObject) {
            // `&dyn`/`&Box<dyn>` (Box<dyn> collapses to TraitObject) from
            // `Vec::borrow(i)` — the value already IS a pointer to a 16-byte fat
            // slot; peel to TraitObject so the "already fat" shortcut stores it
            // directly (NOT rebuild a fat pair from the slot-pointer).
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
            if (src_logos_type &&
                (TypeRef(src_logos_type).kind() == LogosType::Kind::Ptr ||
                 TypeRef(src_logos_type).kind() == LogosType::Kind::Ref ||
                 TypeRef(src_logos_type).kind() == LogosType::Kind::MutRef) &&
                TypeRef(src_logos_type).pointee())
                src_logos_type = TypeRef(src_logos_type).pointee();
            // An OWNING `Box<Concrete>` source (implicit `let g: Box<dyn> =
            // box_new(...)` coercion) yields an OWNING `Box<dyn>` — its handle
            // must be heap-allocated so the scope-end drop_in_place + free
            // sequence frees a real heap block (a stack fat pair would invalid-
            // free). A `&Concrete` borrow source stays a stack fat pair.
            bool src_is_owning_box =
                is_stdlib_box(src_logos_type) &&
                TypeRef(src_logos_type).type_args().size() == 1;
            if (src_is_owning_box)
                src_logos_type = TypeRef(src_logos_type).type_args()[0];
            // Use the mono-mangled concrete name (`Foo$G1$i64`) — the vtable
            // registry keys generic-impl entries on this form. `type_str`
            // yields the angle-bracket form (`Foo<i64>`) which never matches
            // (→ null vtable → SIGSEGV on dispatch; G158-10).
            std::string src_type =
                (src_logos_type &&
                 (TypeRef(src_logos_type).kind() == LogosType::Kind::Struct ||
                  TypeRef(src_logos_type).kind() == LogosType::Kind::ZonedStruct))
                    ? concrete_struct_name(src_logos_type)
                    : type_str(src_logos_type);
            // `&dyn`/`&mut dyn` local → stack fat pair (no leak). Owning
            // `Box<dyn>` local → ALSO an inline value fat pair (droppable; its
            // drop frees the boxed data). Only raw `*const/*mut dyn` keeps an
            // 8-byte heap handle (escapes via the raw pointer; persistent/
            // smart-ptr convention).
            alloca = coerce_to_dyn(data_ptr, std::string(st.trait_name()), src_type,
                                   src_logos_type);
        }
        scope_[s.name] = alloca;
        let_vars_.insert(s.name);
        var_dyn_trait_[s.name] = std::string(st.trait_name());
        // Provenance bookkeeping for raw `*const/*mut dyn` (Ptr<TraitObject>):
        // a `*const dyn` returned by a CONTAINER ACCESSOR (`HashMap::get →
        // *const Box<dyn>`) is a pointer-INTO-storage, so a later `*p` must LOAD
        // the stored handle. A coerced `*const dyn` handle / param / field is the
        // raw fat-ptr itself (`*z` no-op, the EDeref default). Both share the
        // type Ptr<TraitObject>; only provenance distinguishes them. Mark the
        // accessor-return case (source is a method call returning `*const dyn`,
        // or a chained copy of such a var) so EDeref takes the LOAD branch.
        if (is_raw_ptr_dyn && s.value) {
            auto sv_ref = expr_ref_of(*s.value);
            bool src_is_accessor_ret = false;
            if (sv_ref) {
                if (sv_ref.kind() == lir_schema::expr::Code::MethodCall) {
                    src_is_accessor_ret = true;
                } else if (sv_ref.kind() == lir_schema::expr::Code::VarRef) {
                    lir_view::EVarRefView vr(sv_ref);
                    src_is_accessor_ret =
                        dyn_ptr_to_handle_vars_.count(std::string(vr.name())) > 0;
                }
            }
            if (src_is_accessor_ret)
                dyn_ptr_to_handle_vars_.insert(s.name);
        }
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

    // Array let-rebind (`let b: [T; N] = a;`): gen_expr(`a`) returns a
    // pointer to the source array's alloca (var_subscript_ path in
    // EVarRef). A plain StoreOp would overwrite only the first 8 bytes
    // of the destination (the pointer value), leaving the rest of the
    // array slot uninitialised. Mirror the struct-rebind memcpy path in
    // gen_assign (line ~610) — emit llvm.memcpy of the whole array.
    if (TypeRef st(s.type); st && st.kind() == LogosType::Kind::Array &&
        val.getType() == ptr_type()) {
        auto arr_t = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(var_type);
        if (arr_t) {
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, alloca, val, size_const(s.type), /*isVolatile=*/false);
            scope_[s.name] = alloca;
            let_vars_.insert(s.name);
            // Same elem-type bookkeeping as the StoreOp path below.
            var_elem_types_[s.name] = arr_t.getElementType();
            var_subscript_[s.name]  = arr_t.getElementType();
            return;
        }
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
    // B8 drop-before-replace: sema's definite-assignment analysis flagged this
    // reassignment as overwriting a live droppable value. `val` (the new value)
    // is already computed above (RHS evaluated — so `x = f(x)` read the old x
    // safely); drop the OLD value now, before the store below overwrites it.
    // gen_drop_value runs the full destructor (user Drop impl + owned children).
    auto flag_it = uninit_drop_flag_.find(name);
    if (flag_it != uninit_drop_flag_.end()) {
        // B8 dynamic drop flag: drop the OLD value only if the slot currently
        // holds a live one (flag==1) — `x = b` after a conditional `if c {x=a;}`
        // drops `a` iff c ran. RHS already evaluated above (`x=f(x)` safe). Then
        // mark the slot live; the store below writes the new value.
        if (val_le && val_le->type) {
            auto i8t  = builder_.getI8Type();
            auto flag = builder_.create<mlir::LLVM::LoadOp>(loc_, i8t, flag_it->second);
            auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 8);
            auto live = builder_.create<mlir::LLVM::ICmpOp>(
                loc_, mlir::LLVM::ICmpPredicate::ne, flag, zero);
            auto* region   = builder_.getBlock()->getParent();
            auto* then_blk = new mlir::Block();
            auto* cont_blk = new mlir::Block();
            region->push_back(then_blk);
            region->push_back(cont_blk);
            builder_.create<mlir::cf::CondBranchOp>(loc_, live, then_blk, cont_blk);
            builder_.setInsertionPointToStart(then_blk);
            gen_drop_value(it->second, TypeRef(val_le->type));
            builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
            builder_.setInsertionPointToStart(cont_blk);
        }
        builder_.create<mlir::LLVM::StoreOp>(
            loc_, builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 8), flag_it->second);
    } else if (uninit_static_.count(name)) {
        // B8 static-uninit var: its init state is statically tracked. The FIRST
        // (dominating) assignment overwrites garbage → no drop; later ones drop
        // the live value unconditionally.
        if (uninit_assigned_.count(name)) {
            if (val_le && val_le->type) gen_drop_value(it->second, TypeRef(val_le->type));
        } else {
            uninit_assigned_.insert(name);
        }
    } else if (v.drop_old() && val_le && val_le->type) {
        gen_drop_value(it->second, TypeRef(val_le->type));
    }
    // Enum value-repr: the slot IS the inline {disc,payload} storage (one
    // level, like a Struct). Memcpy the new enum value's footprint into it
    // (NOT a ptr store, which would overwrite only the disc word). `val` is a
    // ptr to the source enum storage (or an aggregate value to spill first).
    if (var_tagged_enum_.count(name) || var_tagged_enum_ptr_.count(name)) {
        TypeRef et2 = val_le ? val_le->type : nullptr;
        const TaggedEnumInfo* te = et2 && TypeRef(et2).kind() == LogosType::Kind::Enum
            ? resolve_tagged_enum(std::string(TypeRef(et2).enum_name()), et2) : nullptr;
        val = spill_to_alloca(val);
        if (te && te->llvm_type && val.getType() == ptr_type()) {
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, it->second, val, size_const(et2), /*isVolatile=*/false);
        } else if (te && te->llvm_type) {
            // A tagged enum reassigned a bare i32 disc (e.g. `o = None` with no
            // type_args inferred): write only the disc word of the storage.
            llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
            auto dp = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), te->llvm_type, it->second, di);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
        } else {
            // C-like enum (i32 disc): the slot IS the i32.
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
        }
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
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, it->second, val, size_const(val_t), /*isVolatile=*/false);
            return;
        }
    }
    // Fat-pointer-valued rebind (Slice / Closure / TraitObject): backing
    // storage is 16 bytes ({data, len|vtable}). A plain StoreOp would
    // overwrite only the first 8 bytes with the source data-ptr, leaving
    // len/vtable stale — exactly the bug behind `let mut z: &[T] = ...;
    // z = x;` where z[i] reads garbage. Mirrors the same memcpy path
    // already in gen_index_write (line ~1591).
    if (val_t && val.getType() == ptr_type() &&
        (TypeRef(val_t).kind() == LogosType::Kind::Slice ||
         TypeRef(val_t).kind() == LogosType::Kind::Closure ||
         TypeRef(val_t).kind() == LogosType::Kind::TraitObject)) {
        auto sz = builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(),
            builder_.getI64IntegerAttr(16));
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, it->second, val, sz, /*isVolatile=*/false);
        return;
    }
    // Whole-array rebind (`t = [a, b]` / `t = other_arr`): arrays are
    // pointer-represented, so `val` is a pointer to the source array storage
    // and `it->second` is the destination array alloca. A plain StoreOp would
    // write only the source POINTER into the first 8 bytes of the slot (leaving
    // the elements stale → `t[i]` reads the old value). Memcpy the whole array,
    // like the struct/fat-pointer rebinds above.
    if (val_t && TypeRef(val_t).kind() == LogosType::Kind::Array &&
        val.getType() == ptr_type()) {
        auto arr_ll = logos_to_mlir(val_t);
        if (arr_ll) {
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, it->second, val, size_const(val_t), /*isVolatile=*/false);
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
    // A function whose return type is the never type `!` has a void (0-result)
    // MLIR signature (logos_to_mlir(Never)=nullptr). A `return <e>` in such a
    // fn — e.g. a monomorphized `Option<!>::unwrap` whose payload type is `!`,
    // or `fn f() -> ! { return diverging() }` — must emit an OPERAND-LESS
    // return (returning a value would mismatch the 0-result signature).
    // Evaluate <e> for side effects (it may itself be a diverging call that
    // terminates the block); only emit the void return if the block survives.
    if (cur_fn_ret_logos_type_ &&
        TypeRef(cur_fn_ret_logos_type_).kind() == LogosType::Kind::Never) {
        if (val_le) gen_expr(*val_le);
        if (!is_terminated(builder_.getBlock())) {
            if (in_llvm_func_)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{});
        }
        return;
    }
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
            // Strip the indirection: source may be `&T`/`&mut T`/`*const T`/
            // `*mut T` over a concrete struct. vtable is keyed on the bare
            // struct name. Without unwrapping Ref/MutRef the lookup keys on
            // "&T" and returns null → null vtable → segfault on dispatch.
            if ((TypeRef(src_lt).kind() == LogosType::Kind::Ptr ||
                 TypeRef(src_lt).kind() == LogosType::Kind::Ref ||
                 TypeRef(src_lt).kind() == LogosType::Kind::MutRef) &&
                TypeRef(src_lt).pointee())
                src_lt = TypeRef(src_lt).pointee();
            // Value-fat-pair model: build the {data,vtable} pair on the stack
            // (coerce_to_dyn → alloca, no malloc) and RETURN IT BY VALUE — the
            // function's MLIR return type is the 16-byte struct (llvm_fn_ret_type).
            // The caller copies the value into its own storage, so no heap
            // surviving-slot is needed.
            auto fat_ptr = coerce_to_dyn(val,
                std::string(TypeRef(cur_fn_ret_logos_type_).trait_name()),
                type_str(src_lt));
            if (!fat_ptr) return;
            auto dyn_struct = dyn_llvm_type();
            auto fat_val = builder_.create<mlir::LLVM::LoadOp>(loc_, dyn_struct, fat_ptr);
            if (in_llvm_func_)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{fat_val});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fat_val});
            return;
        }
        // Returning a value that is ALREADY a `&dyn`/`dyn` (TraitObject) — e.g.
        // `return g;` where g: &dyn T. The fn return type is the 16-byte fat
        // pair (by value); the value is a pointer to its storage → load it.
        if (cur_fn_ret_logos_type_ &&
            TypeRef(cur_fn_ret_logos_type_).kind() == LogosType::Kind::TraitObject &&
            s_value.type &&
            TypeRef(s_value.type).kind() == LogosType::Kind::TraitObject) {
            auto val = gen_expr(s_value);
            if (!val) return;
            auto dyn_struct = dyn_llvm_type();
            if (val.getType() == ptr_type())
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, dyn_struct, val);
            if (in_llvm_func_)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{val});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{val});
            return;
        }

        auto val = gen_expr(s_value);
        if (!val) return;
        // Slice/str fat-pair return BY VALUE (mirror the TraitObject path above):
        // the fn MLIR return type is the 16-byte {ptr,len} (llvm_fn_ret_type).
        // A slice value is normally a pointer-to-stack-storage (ESliceLit alloca,
        // a spilled call result, a `&[T]` field/var) → LOAD the 16-byte value and
        // return it. No malloc, no surviving heap slot (was the A3/A4 leak). If
        // it's already a loaded 16-byte struct value, return as-is.
        if (cur_fn_ret_logos_type_ &&
            TypeRef(cur_fn_ret_logos_type_).kind() == LogosType::Kind::Slice) {
            auto stype = slice_llvm_type();
            if (val.getType() == ptr_type())
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, stype, val);
            if (in_llvm_func_)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{val});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{val});
            return;
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
    // G160-10: a diverging condition (`if (return x) {}`) already emitted a
    // terminator — nothing in the if reachable, don't append after it.
    if (is_terminated(builder_.getBlock())) return;

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
    // GEP stride type: for a STRUCT element, logos_to_mlir collapses to
    // ptr_type (8 bytes), but the buffer holds the struct INLINE — striding by
    // 8 reads the wrong element for any multi-word struct. Use the struct's
    // full LLVM type so the per-index GEP strides by sizeof(struct).
    // GEP stride = the inline slot footprint. logos_to_mlir collapses several
    // element kinds to an 8-byte ptr; widen the ones the buffer stores INLINE:
    //   • Struct/ZonedStruct  → full LLVM struct (sizeof(struct)).
    //   • TraitObject/Closure/Slice → 16-byte fat pair (uniform fat model; also
    //     covers Box<dyn> = owning TraitObject).
    // Tuple/Enum/scalar elements keep the logos_to_mlir representation (tuples
    // are stored BY POINTER in slice/Vec buffers — an 8-byte slot — so widening
    // them mis-strides the iteration; the for-(a,b)-in-Vec<tuple> tests).
    mlir::Type gep_elem_mlir = elem_mlir;
    if (s.elem_type) {
        TypeRef et(s.elem_type);
        auto k = et.kind();
        if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct) {
            auto sit = struct_types_.find(mlir_struct_key(et));
            if (sit != struct_types_.end()) gep_elem_mlir = sit->second.llvm_type;
        } else if (k == LogosType::Kind::TraitObject ||
                   k == LogosType::Kind::Closure ||
                   k == LogosType::Kind::Slice ||
                   k == LogosType::Kind::Tuple) {
            // Tuples are now stored INLINE by value in slice/array buffers — use
            // the full footprint stride (place_slot_type → tuple_llvm_type), not
            // logos_to_mlir's collapsed 8-byte ptr.
            gep_elem_mlir = place_slot_type(et);
        }
    }
    if (!gep_elem_mlir) gep_elem_mlir = elem_mlir;

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
            loc_, ptr_type(), gep_elem_mlir, data_ptr, arr_idx);

        // Rust parity: slice iteration yields `&T` — bind the element ADDRESS
        // (a reference into the original buffer), not a copied value. scope_
        // holds the pointer and the var is recorded in ref_param_names_ so
        // `*x` / passing `x` to a `&T` param deref correctly (mirrors a
        // `&T`-typed parameter binding).
        // A TUPLE element is now stored INLINE by value in the buffer (Rust
        // layout), so `elem_ptr` already IS the tuple value (a pointer to the
        // inline tuple storage) — bind it directly, like a struct element. (Was
        // a load of an 8-byte stored tuple-ptr under the old by-pointer model.)
        mlir::Value bound = elem_ptr;
        scope_[s.var]          = bound;
        var_elem_types_[s.var] = elem_mlir;
        var_subscript_[s.var]  = elem_mlir;
        ref_param_names_.insert(s.var);

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
        var_subscript_.erase(s.var);
        ref_param_names_.erase(s.var);
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
    } else if (s.elem_type && TypeRef(s.elem_type).kind() == LogosType::Kind::Tuple) {
        // Tuple elements are stored INLINE (`[N x <tuple>]`); GEP via [0,i] with
        // the tuple aggregate stride — the resulting pointer IS the tuple value
        // (ptr-to-storage), like a struct element. No load.
        auto slot_type = place_slot_type(s.elem_type);
        auto arr_type  = mlir::LLVM::LLVMArrayType::get(slot_type, s.arr_size);
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx{int32_t(0), i_cur};
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_type, arr_alloca, arr_idx);
        scope_[s.var]          = elem_ptr;
        var_tuple_.insert(s.var);
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

    // Check if receiver is a direct struct var.
    auto sit = var_struct_.find(receiver);
    if (sit != var_struct_.end()) {
        ptr = get_struct_ptr(receiver);
        type_name = sit->second;
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
            std::fprintf(stderr, "mlir_gen: field write: '%s' is not a struct\n",
                         receiver.c_str());
            return;
        }
    }
    auto& info = struct_types_[type_name];
    auto gep = gep_field(ptr, info, field);
    if (!gep) return;
    auto val = gen_expr(*val_le);
    if (!val) return;
    // Heap-promote Enum values before storing into a struct field that uses
    // Logos's heap-ptr Enum convention (field type is ptr_type). gen_expr
    // for a let-bound Enum returns its alloca pointer — that alloca lives
    // on the *current* fn's stack, so if the containing struct outlives the
    // fn the field would dangle. Likewise fn-call returns spill the
    // aggregate to a fresh stack alloca. Copy contents into a heap region
    // so the field stays valid past return.
    {
        TypeRef val_lt(val_le->type);
        if (val_lt && val_lt.kind() == LogosType::Kind::Enum &&
            val.getType() == ptr_type()) {
            auto* te = resolve_tagged_enum(std::string(val_lt.enum_name()), val_lt);
            if (te) {
                bool field_is_enum_slot = false;
                for (auto& f : info.fields)
                    if (f.name == field && f.type == ptr_type()) {
                        field_is_enum_slot = true;
                        break;
                    }
                if (field_is_enum_slot) {
                    auto size = size_const(val_lt);
                    auto heap = call_malloc(size);
                    if (heap) {
                        builder_.create<mlir::LLVM::MemcpyOp>(loc_, heap, val, size, false);
                        val = heap;
                    }
                }
            }
        }
    }
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
    if (sit != var_struct_.end()) {
        cur_ptr = get_struct_ptr(receiver);
        cur_type_name = sit->second;
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
    auto slit = var_slice_.find(arr);
    if (slit != var_slice_.end()) {
        // G162-2: slice (`&mut [T]`) param/var — scope_ holds a pointer to the
        // fat `{ptr, len}` descriptor. GEP field 0 + load the data pointer,
        // then stride by the element type (mirror of ESliceIndexView read).
        auto stype = slice_llvm_type();  // { ptr, i64 }
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, it->second, pi);
        base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        elem_type = slit->second;
    } else if (lpit != var_local_ptrs_.end()) {
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
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, size_const(val_t), /*isVolatile=*/false);
            return;
        }
    }
    // Tuple element — stored INLINE by value; memcpy the full footprint.
    if (val_t && TypeRef(val_t).kind() == LogosType::Kind::Tuple &&
        val.getType() == ptr_type()) {
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, size_const(val_t), /*isVolatile=*/false);
        return;
    }
    // C5-cl-04 follow-up: fat-pointer-valued rvalues (Closure / Slice /
    // TraitObject) pass by pointer at the ABI but their backing storage is
    // 16 bytes ({ptr, ptr}). `p[0] = val` over such a type must memcpy 16
    // bytes, not store the 8-byte ptr itself — otherwise Box<dyn FnMut(…)>'s
    // `box_new(cl)` only copies half the fat pointer.
    if (val_t && val.getType() == ptr_type() &&
        (TypeRef(val_t).kind() == LogosType::Kind::Closure ||
         TypeRef(val_t).kind() == LogosType::Kind::Slice ||
         TypeRef(val_t).kind() == LogosType::Kind::TraitObject)) {
        auto sz = builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(),
            builder_.getI64IntegerAttr(16));
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, sz, /*isVolatile=*/false);
        return;
    }

    builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
}

void MLIRGenImpl::gen_field_index_write(lir_view::SFieldIndexWriteView v) {
    std::string receiver(v.receiver());
    std::string field(v.field());
    auto* idx_le = lexpr_of(v.index());
    auto* val_le = lexpr_of(v.value());
    if (!idx_le || !val_le) return;

    // Get pointer to the struct.
    auto struct_ptr = get_struct_ptr(receiver);
    if (!struct_ptr) return;

    // Get struct type info to find the field.
    auto sit = var_struct_.find(receiver);
    if (sit == var_struct_.end()) {
        std::fprintf(stderr, "mlir_gen: field index write: '%s' not struct\n",
                     receiver.c_str());
        return;
    }
    const std::string& type_name = sit->second;
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
        // Stride MUST be the inline slot footprint: a concrete struct's LLVM
        // type, a 16-byte fat pair for dyn/closure/slice — NOT logos_to_mlir's
        // collapsed 8-byte ptr (which would overlap adjacent buffer elements).
        TypeRef vt = val_le ? val_le->type : nullptr;
        mlir::Type gep_elem = vt ? place_slot_type(vt) : val_type;
        if (!gep_elem) gep_elem = val_type;
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
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, base_ptr, val, size_const(val_t), /*isVolatile=*/false);
            return;
        }
    }
    // Tuple element assignment — stored INLINE by value; memcpy the full tuple
    // footprint (`Vec<(i64,i64)>::push`), not an 8-byte ptr store.
    if (val_t && TypeRef(val_t).kind() == LogosType::Kind::Tuple &&
        val.getType() == ptr_type()) {
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, base_ptr, val, size_const(val_t), /*isVolatile=*/false);
        return;
    }
    // Fat-pointer element (dyn/closure/slice): 16-byte {ptr,ptr}/{ptr,len} pair
    // stored INLINE — memcpy all 16 bytes (an 8-byte store leaves the second
    // half stale → bad dispatch / stale len). Mirrors gen_index_write.
    if (val_t && val.getType() == ptr_type() &&
        (TypeRef(val_t).kind() == LogosType::Kind::Closure ||
         TypeRef(val_t).kind() == LogosType::Kind::Slice ||
         TypeRef(val_t).kind() == LogosType::Kind::TraitObject)) {
        auto sz = builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(), builder_.getI64IntegerAttr(16));
        builder_.create<mlir::LLVM::MemcpyOp>(loc_, base_ptr, val, sz, /*isVolatile=*/false);
        return;
    }

    builder_.create<mlir::LLVM::StoreOp>(loc_, val, base_ptr);
}

// ---------------------------------------------------------------------------
// Recursive pattern matcher (nested tuple / variant / or as a sub-pattern).
// `slot_ptr` is a pointer to the value's storage. For an enum value the slot
// holds the heap pointer to the enum struct (the two-level convention), so the
// enum cases load it first.
// ---------------------------------------------------------------------------

void MLIRGenImpl::collect_pat_bindings(
    lir_view::PatRef pat, TypeRef ty,
    std::vector<std::pair<std::string, TypeRef>>& out) {
    namespace pc = lir_schema::pat;
    if (!pat) return;
    switch (pat.kind()) {
    case pc::Code::Wild: {
        auto n = lir_view::PatWildView{pat}.name();
        if (!n.empty() && n != "_") out.emplace_back(std::string(n), ty);
        break;
    }
    case pc::Code::Tuple: {
        auto elems = ty ? TypeRef(ty).tuple_elems() : std::vector<TypeRef>{};
        size_t i = 0;
        lir_view::PatTupleView{pat}.each_sub([&](lir_view::PatRef sp){
            TypeRef et = i < elems.size() ? elems[i] : TypeRef{};
            collect_pat_bindings(sp, et, out);
            ++i;
        });
        break;
    }
    case pc::Code::VariantData: {
        lir_view::PatVariantDataView pvd{pat};
        auto* te = resolve_tagged_enum(std::string(pvd.enum_name()), ty);
        std::vector<std::string> names;
        pvd.each_binding([&](std::string_view n){ names.emplace_back(n); });
        std::vector<TypeRef> btypes;
        pvd.each_binding_type(pool_impl(), [&](TypeRef t){ btypes.push_back(t); });
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] != "_")
                out.emplace_back(names[i], i < btypes.size() ? btypes[i] : TypeRef{});
        (void)te;
        break;
    }
    case pc::Code::Or: {
        lir_view::PatRef first;
        lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a){ if (!first) first = a; });
        if (first) collect_pat_bindings(first, ty, out);
        break;
    }
    default: break;
    }
}

mlir::Value MLIRGenImpl::pat_test(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty) {
    namespace pc = lir_schema::pat;
    auto true_c = [&]{ return builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1).getResult(); };
    if (!pat || !slot_ptr) return true_c();
    auto elem_mlir = ty ? logos_to_mlir(ty) : mlir::Type();
    bool unsign = ty && (TypeRef(ty).kind() == LogosType::Kind::U8 ||
        TypeRef(ty).kind() == LogosType::Kind::U16 || TypeRef(ty).kind() == LogosType::Kind::U32 ||
        TypeRef(ty).kind() == LogosType::Kind::U64 || TypeRef(ty).kind() == LogosType::Kind::Usize ||
        TypeRef(ty).kind() == LogosType::Kind::Char || TypeRef(ty).kind() == LogosType::Kind::Bool);
    switch (pat.kind()) {
    case pc::Code::Wild:
    case pc::Code::RefBind:
        return true_c();
    case pc::Code::Int: case pc::Code::Bool: {
        if (!elem_mlir) return true_c();
        int64_t cval = pat.kind() == pc::Code::Bool
            ? (lir_view::PatBoolView{pat}.value() ? 1 : 0)
            : lir_view::PatIntView{pat}.value();
        auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, slot_ptr);
        auto cv = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(loc_, cval, 64), elem_mlir);
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
    }
    case pc::Code::Range: {
        if (!elem_mlir) return true_c();
        lir_view::PatRangeView pr{pat};
        auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, slot_ptr);
        auto lo = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.lo(), 64), elem_mlir);
        auto hi = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.hi(), 64), elem_mlir);
        auto ge = builder_.create<mlir::arith::CmpIOp>(loc_,
            unsign ? mlir::arith::CmpIPredicate::uge : mlir::arith::CmpIPredicate::sge, ev, lo);
        auto le = builder_.create<mlir::arith::CmpIOp>(loc_,
            unsign ? mlir::arith::CmpIPredicate::ule : mlir::arith::CmpIPredicate::sle, ev, hi);
        return builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
    }
    case pc::Code::Tuple: {
        auto ttype = ty ? tuple_llvm_type(ty) : mlir::Type();
        if (!ttype) return true_c();
        auto elems = TypeRef(ty).tuple_elems();
        // A tuple value IS a pointer to its inline storage (Rust by-value layout):
        // `slot_ptr` already addresses the tuple struct, so GEP into it directly
        // (no load). (Was a by-pointer load: slot holds a ptr to the tuple.)
        auto tptr = slot_ptr;
        mlir::Value cond = true_c();
        size_t i = 0;
        lir_view::PatTupleView{pat}.each_sub([&](lir_view::PatRef sp){
            size_t idx = i++;
            if (!sp || idx >= elems.size()) return;
            llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(idx)};
            auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, tptr, gi);
            auto sc = pat_test(sp, fp, elems[idx]);
            cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, sc);
        });
        return cond;
    }
    case pc::Code::Variant:
    case pc::Code::VariantData: {
        std::string ename = pat.kind() == pc::Code::Variant
            ? std::string(lir_view::PatVariantView{pat}.enum_name())
            : std::string(lir_view::PatVariantDataView{pat}.enum_name());
        int64_t disc = pat.kind() == pc::Code::Variant
            ? lir_view::PatVariantView{pat}.disc()
            : lir_view::PatVariantDataView{pat}.disc();
        // logos-core 4.3 (finish): peel ALL `&`/`&mut` chain layers to reach
        // the enum-storage pointer — pre-fix one layer was peeled which
        // worked for `&Enum` but silently broke for `&&Enum`/deeper (the
        // disc compare then ran on a ptr-to-ptr instead of the inline
        // enum's i32 disc field).
        int enum_ref_depth = 0;
        TypeRef enum_ty = ty;
        while (enum_ty &&
               (TypeRef(enum_ty).kind() == LogosType::Kind::Ref ||
                TypeRef(enum_ty).kind() == LogosType::Kind::MutRef) &&
               TypeRef(enum_ty).pointee()) {
            ++enum_ref_depth;
            enum_ty = TypeRef(enum_ty).pointee();
        }
        bool via_ref_enum = enum_ref_depth > 0 && enum_ty &&
            TypeRef(enum_ty).kind() == LogosType::Kind::Enum;
        if (!via_ref_enum) enum_ref_depth = 0;
        // G160-8: resolve the tagged-enum spec off the ENUM type, not the
        // `&Enum` ref wrapper — passing the ref makes resolve_tagged_enum miss
        // the concrete spec (returns null) → the C-like fallback below
        // under-derefs the two-level `&Enum` element → SIGSEGV on a tuple of
        // `&Option<T>`.
        auto* te = resolve_tagged_enum(ename, via_ref_enum ? enum_ty : ty);
        if (!te) {
            // C-like enum (all-nullary, no TaggedEnumInfo): the value IS the
            // i32 discriminant — there is no heap struct. Peel arbitrary-depth
            // refs by chaining LoadOps before loading the disc.
            mlir::Value base = slot_ptr;
            for (int li = 0; li < enum_ref_depth; ++li)
                base = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), base);
            auto dv = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), base);
            auto dc = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
            return builder_.create<mlir::arith::CmpIOp>(
                loc_, mlir::arith::CmpIPredicate::eq, dv, dc);
        }
        // Enum value-repr: an enum element/field is INLINE — `slot_ptr` IS
        // the enum-struct storage (one level, like a Struct). A `&Enum`
        // element holds a pointer to that storage; load once per ref layer
        // to reach the storage.
        mlir::Value enum_ptr = slot_ptr;
        for (int li = 0; li < enum_ref_depth; ++li)
            enum_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), enum_ptr);
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), te->llvm_type, enum_ptr, di);
        auto dv = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
        auto dc = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
        // Payload sub-patterns are stored as bindings (names) only; refutable
        // inners (Some(1)) are handled by the sema guard channel, so the disc
        // test alone is the constraint here.
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq, dv, dc);
    }
    case pc::Code::Or: {
        mlir::Value cond = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
        lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef alt){
            auto sc = pat_test(alt, slot_ptr, ty);
            cond = builder_.create<mlir::arith::OrIOp>(loc_, cond, sc);
        });
        return cond;
    }
    case pc::Code::Struct: {
        // G148-1: struct pattern with refutable field sub-patterns
        // (`Wrap { x: Inner::A(v), y }`). The slot holds a pointer to the
        // struct value; GEP each named field and AND-chain the refutable
        // sub-pattern tests. Irrefutable subs (bind / shorthand / wild)
        // contribute nothing to the condition.
        lir_view::PatStructView ps{pat};
        std::string sname(ps.struct_name());
        auto sit = struct_types_.find(sname);
        if (sit == struct_types_.end()) return true_c();
        const StructInfo& sinfo = sit->second;
        // Unified convention with Tuple: `slot_ptr` IS the struct data address
        // (no extra alloca-of-pointer indirection). Nested sub-pattern callers
        // pass `gep_field(parent, fname)` — that's already the inline child's
        // address. Loading-as-pointer there was reading the first 8 bytes of
        // the child as if they were a heap pointer (silent miscompile that
        // segfaults on dereference).
        auto sptr = slot_ptr;
        const LStructDef* sd = nullptr;
        if (auto di = all_struct_defs_.find(sname); di != all_struct_defs_.end())
            sd = di->second;
        mlir::Value cond = true_c();
        ps.each_field([&](lir_view::PatFieldBindingView pfb){
            auto sub = pfb.sub();
            if (!sub || sub.kind() == pc::Code::Wild ||
                sub.kind() == pc::Code::RefBind)
                return;
            std::string fname(pfb.field_name());
            auto fp = gep_field(sptr, sinfo, fname);
            if (!fp) return;
            TypeRef fty;
            if (sd) for (auto& lf : sd->fields)
                if (lf.name == fname) { fty = lf.type; break; }
            auto sc = pat_test(sub, fp, fty);
            cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, sc);
        });
        return cond;
    }
    case pc::Code::RefPat: {
        // `&P` (or `&mut P`) matched against a `&T` slot: the slot holds the
        // reference (a ptr to the T value). Load it and recurse into P against
        // the pointee type. Uniform across C-like enum (pointee is an i32),
        // tagged enum (pointee slot holds the heap ptr) and scalars — the
        // dereffed ref value is exactly the address pat_test expects (G155-5a).
        lir_view::PatRefPatView rp{pat};
        auto inner = rp.inner();
        if (!inner) return true_c();
        TypeRef pointee = (ty && (TypeRef(ty).kind() == LogosType::Kind::Ref ||
                                  TypeRef(ty).kind() == LogosType::Kind::MutRef))
                          ? TypeRef(ty).pointee() : ty;
        auto ref_val = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot_ptr);
        return pat_test(inner, ref_val, pointee);
    }
    case pc::Code::At: {
        // `name @ subpat` — the at-binding is irrefutable; test the sub.
        auto sub = lir_view::PatAtView{pat}.sub();
        return sub ? pat_test(sub, slot_ptr, ty) : true_c();
    }
    case pc::Code::Slice: {
        // Slice/array pattern test. `slot_ptr` is the slice VALUE (ptr to
        // {data,len}) for a dynamic `&[T]`, or the array base for a `[T;N]`
        // (both via GEP from the enclosing place — no extra load). Mirrors the
        // gen_match per-arm Slice test, recursing pat_test for sub-elements.
        if (!ty) return true_c();
        lir_view::PatSliceView sv{pat};
        TypeRef aty = ty;
        if ((TypeRef(aty).kind() == LogosType::Kind::Ref ||
             TypeRef(aty).kind() == LogosType::Kind::MutRef) && TypeRef(aty).pointee())
            aty = TypeRef(aty).pointee();
        auto ak = TypeRef(aty).kind();
        if (ak == LogosType::Kind::Array && TypeRef(aty).elem()) {
            auto arr_mlir = logos_to_mlir(aty);
            TypeRef elem_t = TypeRef(aty).elem();
            if (!arr_mlir) return true_c();
            size_t total = (size_t)TypeRef(aty).arr_size();
            size_t suf_n = sv.suffix_count();
            mlir::Value cond = true_c();
            auto at_idx = [&](lir_view::PatRef sp, int32_t idx) {
                if (!sp || sp.kind() == pc::Code::Wild) return;
                llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), arr_mlir, slot_ptr, gi);
                cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, pat_test(sp, ep, elem_t));
            };
            int32_t idx = 0;
            sv.each_prefix([&](lir_view::PatRef sp){ at_idx(sp, idx++); });
            int32_t sidx = (int32_t)(total - suf_n);
            sv.each_suffix([&](lir_view::PatRef sp){ at_idx(sp, sidx++); });
            return cond;  // array length is fixed — no length gate
        }
        if (ak == LogosType::Kind::Slice && TypeRef(aty).elem()) {
            auto sdtype = slice_llvm_type();
            TypeRef elem_t = TypeRef(aty).elem();
            size_t pre_n = sv.prefix_count(), suf_n = sv.suffix_count();
            bool has_rest = (bool)sv.rest();
            llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
            auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, slot_ptr, li);
            auto slen = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
            auto n = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)(pre_n + suf_n), 64);
            auto pred = has_rest ? mlir::arith::CmpIPredicate::sge : mlir::arith::CmpIPredicate::eq;
            mlir::Value cond = builder_.create<mlir::arith::CmpIOp>(loc_, pred, slen, n);
            auto elem_mlir2 = logos_to_mlir(elem_t);
            if (elem_mlir2 && pre_n > 0) {
                llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, slot_ptr, di);
                auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                int32_t pi = 0;
                sv.each_prefix([&](lir_view::PatRef sp){
                    int32_t idx = pi++;
                    if (!sp || sp.kind() == pc::Code::Wild) return;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(idx)};
                    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir2, data, gi);
                    cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, pat_test(sp, ep, elem_t));
                });
            }
            return cond;
        }
        return true_c();
    }
    default:
        return true_c();
    }
}

void MLIRGenImpl::pat_bind(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty,
                           const std::unordered_map<std::string, mlir::Value>* shared) {
    namespace pc = lir_schema::pat;
    if (!pat || !slot_ptr) return;
    switch (pat.kind()) {
    case pc::Code::Wild: {
        auto n = lir_view::PatWildView{pat}.name();
        if (n.empty() || n == "_") return;
        std::string name(n);
        auto elem_mlir = ty ? logos_to_mlir(ty) : ptr_type();
        if (!elem_mlir) elem_mlir = ptr_type();
        // Aggregate (struct/tuple/enum lowers to a struct/ptr): bind the slot
        // pointer directly. Scalars: load + store into a fresh/shared alloca.
        bool is_struct = ty && (TypeRef(ty).kind() == LogosType::Kind::Struct ||
            TypeRef(ty).kind() == LogosType::Kind::ZonedStruct);
        bool aggregate = is_struct || (ty && TypeRef(ty).kind() == LogosType::Kind::Tuple);
        if (aggregate) {
            scope_[name] = slot_ptr;
            let_vars_.insert(name);
            // Track struct shape so `name.field` GEPs through the bound place
            // (matches extract_payload / gen_match's struct-element bind); a
            // bare scope_ entry without var_struct_ makes field access read
            // garbage (the G151-1-class silent miscompile).
            if (is_struct) var_struct_[name] = mlir_struct_key(ty);
            return;
        }
        // Slice/Closure are inline 16-byte fat pairs; their value convention is
        // a POINTER to the storage, i.e. `slot_ptr` itself. Bind it like a
        // pointer-valued scalar (alloca holding slot_ptr, var_elem_types_=ptr)
        // so `var_ref(name)` loads it back uniformly — same indirection a
        // pre-inline 8-byte-ptr element had. A bare `scope_[name]=slot_ptr`
        // (no var_elem_types_) is read inconsistently by some var_ref sites
        // (the str-literal-tuple guard regression).
        bool slice_closure = ty &&
            (TypeRef(ty).kind() == LogosType::Kind::Slice ||
             TypeRef(ty).kind() == LogosType::Kind::Closure ||
             TypeRef(ty).kind() == LogosType::Kind::TraitObject);
        if (slice_closure) {
            mlir::Value target;
            if (shared) { auto it = shared->find(name); if (it != shared->end()) target = it->second; }
            if (!target) target = create_entry_alloca(ptr_type());
            builder_.create<mlir::LLVM::StoreOp>(loc_, slot_ptr, target);
            scope_[name] = target;
            let_vars_.insert(name);
            var_elem_types_[name] = ptr_type();
            return;
        }
        auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, slot_ptr);
        mlir::Value target;
        if (shared) { auto it = shared->find(name); if (it != shared->end()) target = it->second; }
        if (!target) target = create_entry_alloca(elem_mlir);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, target);
        scope_[name] = target;
        let_vars_.insert(name);
        var_elem_types_[name] = elem_mlir;
        break;
    }
    case pc::Code::Tuple: {
        auto ttype = ty ? tuple_llvm_type(ty) : mlir::Type();
        if (!ttype) return;
        // Deref a `&(T,U)` / `&mut (T,U)` scrutinee for the element types
        // (tuple_llvm_type already deref'd for the LLVM layout); without this
        // tuple_elems() is empty on a Ref ty and no element binds.
        TypeRef tt = ty;
        if (tt && (TypeRef(tt).kind() == LogosType::Kind::Ref ||
                   TypeRef(tt).kind() == LogosType::Kind::MutRef) &&
            TypeRef(tt).pointee() &&
            TypeRef(TypeRef(tt).pointee()).kind() == LogosType::Kind::Tuple)
            tt = TypeRef(tt).pointee();
        auto elems = TypeRef(tt).tuple_elems();
        // A tuple value IS a pointer to its inline storage (Rust by-value layout):
        // `slot_ptr` already addresses the tuple struct — GEP directly (no load).
        auto tptr = slot_ptr;
        size_t i = 0;
        lir_view::PatTupleView{pat}.each_sub([&](lir_view::PatRef sp){
            size_t idx = i++;
            if (!sp || idx >= elems.size()) return;
            llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(idx)};
            auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, tptr, gi);
            pat_bind(sp, fp, elems[idx], shared);
        });
        break;
    }
    case pc::Code::VariantData: {
        lir_view::PatVariantDataView pvd{pat};
        // G160-8: a `&Enum` slot resolves its tagged-enum spec off the ENUM
        // pointee, not the `&Enum` ref wrapper (else resolve_tagged_enum
        // returns null and the payload bind silently no-ops / mis-derefs).
        // logos-core 4.3 (finish): peel arbitrary-depth `&`/`&mut` chains so
        // `let Some(z) = rr` where `rr: &&Option<T>` binds `z: &&T`.
        int enum_ref_depth = 0;
        TypeRef enum_ty = ty;
        while (enum_ty &&
               (TypeRef(enum_ty).kind() == LogosType::Kind::Ref ||
                TypeRef(enum_ty).kind() == LogosType::Kind::MutRef) &&
               TypeRef(enum_ty).pointee()) {
            ++enum_ref_depth;
            enum_ty = TypeRef(enum_ty).pointee();
        }
        bool via_ref_enum = enum_ref_depth > 0 && enum_ty &&
            TypeRef(enum_ty).kind() == LogosType::Kind::Enum;
        if (!via_ref_enum) enum_ref_depth = 0;
        auto* te = resolve_tagged_enum(std::string(pvd.enum_name()),
                                       via_ref_enum ? enum_ty : ty);
        if (!te) return;
        // Enum value-repr: `slot_ptr` IS the inline enum storage (one level). A
        // `&Enum` element holds a pointer to it — load `enum_ref_depth` times
        // to reach the storage.
        mlir::Value enum_ptr = slot_ptr;
        for (int li = 0; li < enum_ref_depth; ++li)
            enum_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), enum_ptr);
        // Delegate to the canonical full-fidelity payload binder (handles
        // ref-bind, inline struct/tuple/enum payloads, trait-object handles,
        // and peer-set eviction — the old inline loop here only did scalar
        // load+alloca). `shared` threads or-pattern alloca reuse.
        std::vector<std::string> added;
        bind_enum_payload(enum_ptr, te, pvd, added, shared);
        break;
    }
    case pc::Code::Or: {
        // Pre-create shared allocas for the bindings (all alts bind the same
        // names+types), dispatch per-alt, and bind each alt into the shared
        // slots so the join sees one storage per name.
        std::vector<std::pair<std::string, TypeRef>> binds;
        lir_view::PatRef first;
        lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a){ if (!first) first = a; });
        if (first) collect_pat_bindings(first, ty, binds);
        std::unordered_map<std::string, mlir::Value> shared_map;
        for (auto& [nm, bty] : binds) {
            auto em = bty ? logos_to_mlir(bty) : ptr_type();
            if (!em) em = ptr_type();
            auto a = create_entry_alloca(em);
            shared_map[nm] = a;
            scope_[nm] = a; let_vars_.insert(nm); var_elem_types_[nm] = em;
        }
        std::vector<lir_view::PatRef> alts;
        lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a){ alts.push_back(a); });
        auto* region = builder_.getBlock()->getParent();
        auto* done = new mlir::Block();
        region->push_back(done);
        for (size_t ai = 0; ai < alts.size(); ++ai) {
            auto* bind_blk = new mlir::Block();
            auto* next_blk = (ai + 1 < alts.size()) ? new mlir::Block() : done;
            region->push_back(bind_blk);
            if (next_blk != done) region->push_back(next_blk);
            auto cond = pat_test(alts[ai], slot_ptr, ty);
            builder_.create<mlir::cf::CondBranchOp>(loc_, cond, bind_blk, next_blk);
            builder_.setInsertionPointToStart(bind_blk);
            pat_bind(alts[ai], slot_ptr, ty, &shared_map);
            builder_.create<mlir::cf::BranchOp>(loc_, done);
            builder_.setInsertionPointToStart(next_blk);
        }
        // current block is `done` (last next_blk == done).
        break;
    }
    case pc::Code::Struct: {
        // G148-1: bind a struct pattern's fields. slot holds a pointer to the
        // struct; GEP each named field and recurse. Shorthand `{x}` binds the
        // field by its own name; `{x: a}` is a Wild-rename handled by recursion;
        // refutable subs (`{x: Inner::A(v)}`) bind their inner names.
        lir_view::PatStructView ps{pat};
        std::string sname(ps.struct_name());
        auto sit = struct_types_.find(sname);
        if (sit == struct_types_.end()) return;
        const StructInfo& sinfo = sit->second;
        // Unified convention: `slot_ptr` is the struct data address — see
        // pat_test's Struct case for the rationale (nested sub-pattern
        // miscompile / segfault if we Load through inline-child storage).
        auto sptr = slot_ptr;
        const LStructDef* sd = nullptr;
        if (auto di = all_struct_defs_.find(sname); di != all_struct_defs_.end())
            sd = di->second;
        ps.each_field([&](lir_view::PatFieldBindingView pfb){
            std::string fname(pfb.field_name());
            auto fp = gep_field(sptr, sinfo, fname);
            if (!fp) return;
            TypeRef fty;
            if (sd) for (auto& lf : sd->fields)
                if (lf.name == fname) { fty = lf.type; break; }
            auto sub = pfb.sub();
            if (!sub) {
                // Shorthand `{x}` → bind field value to `x`. Mirror the Wild
                // binding logic: aggregates bind the slot ptr, scalars load+store.
                auto fmlir = fty ? logos_to_mlir(fty) : ptr_type();
                if (!fmlir) fmlir = ptr_type();
                bool aggregate = fty && (TypeRef(fty).kind() == LogosType::Kind::Struct ||
                    TypeRef(fty).kind() == LogosType::Kind::ZonedStruct ||
                    TypeRef(fty).kind() == LogosType::Kind::Tuple);
                if (aggregate) {
                    scope_[fname] = fp; let_vars_.insert(fname);
                } else {
                    auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, fmlir, fp);
                    mlir::Value target;
                    if (shared) { auto it = shared->find(fname); if (it != shared->end()) target = it->second; }
                    if (!target) target = create_entry_alloca(fmlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, target);
                    scope_[fname] = target; let_vars_.insert(fname);
                    var_elem_types_[fname] = fmlir;
                }
                return;
            }
            pat_bind(sub, fp, fty, shared);
        });
        break;
    }
    case pc::Code::RefBind: {
        // `ref x` (and default-binding-mode refs that route through a RefBind
        // sub): bind `x : &T` to the slot ADDRESS — a borrow, no load/copy, so
        // it doesn't move/double-free an owned field. pat_bind previously had NO
        // RefBind case → the name was never bound → garbage reads (silent
        // miscompile for `match &p { Pair { a: ref a } }`). Mirrors the
        // enum-variant is_ref_bind path (G151-1): ref-to-struct binds the ptr +
        // tracks struct shape so `x.field` GEPs through it; a scalar ref
        // alloca-wraps so `*x` derefs one level.
        auto n = lir_view::PatRefBindView{pat}.name();
        if (n.empty() || n == "_") return;
        std::string name(n);
        bool ref_to_struct = ty &&
            (TypeRef(ty).kind() == LogosType::Kind::Struct ||
             TypeRef(ty).kind() == LogosType::Kind::ZonedStruct);
        if (ref_to_struct) {
            scope_[name] = slot_ptr;
            let_vars_.insert(name);
            var_struct_[name] = mlir_struct_key(ty);
        } else {
            auto alloca = create_entry_alloca(ptr_type());
            builder_.create<mlir::LLVM::StoreOp>(loc_, slot_ptr, alloca);
            scope_[name] = alloca;
            let_vars_.insert(name);
            var_elem_types_[name] = ptr_type();
        }
        break;
    }
    default: break;
    }
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
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return;
    }
    // G160-10: a diverging scrutinee (`match return x { … }`) already emitted a
    // terminator — the arms are dead, stop.
    if (is_terminated(builder_.getBlock())) return;

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
                // Enum value-repr: an enum value IS a pointer to its inline
                // {disc,payload} storage (one level, like a Struct). `&Enum` is
                // therefore the SAME one-level pointer — no extra deref. A
                // by-value aggregate (returned by value from a fn) is spilled.
                if (via_ref) {
                    // scrut already IS the enum-storage pointer.
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
            } else if (via_ref) {
                // G165-1: a FIELDLESS / C-like enum has no TaggedEnumInfo — its
                // by-value form is a plain i32 discriminant (not a heap ptr), so
                // `&Enum` is a one-level ptr-to-i32. Load the disc through the ref
                // so the scalar arm tests below compare i32==disc instead of
                // comparing the raw `&Enum` pointer (which crashed mlir-gen:
                // `arith.cmpi operand must be integer, got !llvm.ptr`).
                scrut = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, builder_.getI32Type(), scrut);
            }
        }
    }
    // Default binding modes: `match &(T,U) { (a,b) }`. A `&tuple` is a one-level
    // pointer to the tuple-struct (like `&struct`, NOT the two-level `&Enum`),
    // so use it directly as the tuple base ptr (feeds the tuple extract's
    // `scrut_ptr ? scrut_ptr : …`). tuple_llvm_type derefs the ref for layout.
    if (!scrut_ptr) {
        TypeRef st(scrut_le->type);
        if (st && (st.kind() == LogosType::Kind::Ref ||
                   st.kind() == LogosType::Kind::MutRef ||
                   st.kind() == LogosType::Kind::Ptr) &&
            st.pointee() &&
            TypeRef(st.pointee()).kind() == LogosType::Kind::Tuple)
            scrut_ptr = scrut;
    }
    // Keep scrut at its natural type; coerce disc constants to match it.
    mlir::Type scrut_type = scrut.getType();

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    // Pattern irrefutability — single foundation in `lir_view`. See
    // `is_irrefutable_pattern` for the full case list (Wild/RefBind/RefPat/
    // At/Tuple/Struct/Slice/Or). Was a 50-line local lambda that drifted
    // from a narrower copy in mlir_gen_expr.cpp; foundation closes the
    // drift (logos-core 4.1).
    auto is_irrefutable = [](lir_view::PatRef p) -> bool {
        return lir_view::is_irrefutable_pattern(p);
    };
    bool scrut_is_tuple = scrut_le->type &&
        (TypeRef(scrut_le->type).kind() == LogosType::Kind::Tuple ||
         ((TypeRef(scrut_le->type).kind() == LogosType::Kind::Ref ||
           TypeRef(scrut_le->type).kind() == LogosType::Kind::MutRef ||
           TypeRef(scrut_le->type).kind() == LogosType::Kind::Ptr) &&
          TypeRef(scrut_le->type).pointee() &&
          TypeRef(TypeRef(scrut_le->type).pointee()).kind() == LogosType::Kind::Tuple));
    if (scrut_is_tuple) {
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
            // [UNIFY C-tuple] Route the whole tuple destructure through the
            // single pat_bind foundation (was a duplicate per-binding loop +
            // a second nested-variant/tuple pass). pat_bind's Tuple case
            // recurses per element (Wild→struct/scalar bind, VariantData→
            // bind_enum_payload, nested Tuple/Or). It loads the tuple ptr from
            // its slot, so hand it a slot (alloca) holding the tuple base ptr.
            mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
            if (!tptr) return;
            // A tuple value IS a pointer to its inline storage — pass it directly
            // (pat_bind's Tuple case GEPs into it, no load).
            pat_bind(p, tptr, scrut_le->type);
            return;
        }
        // ── PatVariantData ────────────────────────────────────────────────
        case pc::Code::VariantData: {
            if (te_info && scrut_ptr) {
                lir_view::PatVariantDataView pvd{p};
                int32_t pvd_disc = static_cast<int32_t>(pvd.disc());
                std::vector<std::string> bindings;
                pvd.each_binding([&](std::string_view n){ bindings.emplace_back(n); });
                std::vector<TypeRef> pvd_binding_types;
                pvd.each_binding_type(pool_impl(),
                    [&](TypeRef t){ pvd_binding_types.push_back(t); });
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& vinfo : te_info->variants)
                    if (vinfo.disc == pvd_disc) { vp = &vinfo; break; }
                if (vp && !bindings.empty()) {
                    auto pay_struct = variant_payload_struct(*vp);
                    for (size_t bi = 0; bi < bindings.size() &&
                                         bi < vp->field_types.size(); ++bi) {
                        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                        auto fp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), pay_struct, pay_ptr, fi);
                        // SL-sl-03 follow-up: `ref v` / `ref mut v`
                        // binding — sema wraps with Ref/MutRef. Bind to
                        // the GEP address directly.
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
                            // G151-1: `ref l` of a STRUCT payload binds `l : &Struct`
                            // — fp IS the struct pointer, so bind it like a
                            // `&Struct` (scope_=fp + var_struct_) so `l.field`
                            // GEPs correctly. The old ptr-of-ptr alloca with no
                            // struct-shape tracking made `l.field` read garbage.
                            // Scalar `ref l` keeps the alloca-wrap for `*l`.
                            TypeRef rlt = bi < vp->logos_types.size()
                                ? TypeRef(vp->logos_types[bi]) : TypeRef{};
                            if (rlt && (rlt.kind() == LogosType::Kind::Struct ||
                                        rlt.kind() == LogosType::Kind::ZonedStruct)) {
                                scope_[bindings[bi]] = fp;
                                let_vars_.insert(bindings[bi]);
                                var_struct_[bindings[bi]] = mlir_struct_key(rlt);
                                continue;
                            }
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                            scope_[bindings[bi]] = alloca;
                            let_vars_.insert(bindings[bi]);
                            var_elem_types_[bindings[bi]] = ptr_type();
                            continue;
                        }
                        // For inline structs, fp already points to the struct bytes —
                        // use it directly (no load), matching the memcpy write side.
                        TypeRef lt = bi < vp->logos_types.size()
                                              ? vp->logos_types[bi] : nullptr;
                        // Enum value-repr: a nested TAGGED enum payload field is
                        // INLINE — `fp` is its storage (one level). Bind as a
                        // tagged-enum var (no load), matching the memcpy write. A
                        // C-like enum (no TaggedEnumInfo) is an i32 — scalar below.
                        if (lt && TypeRef(lt).kind() == LogosType::Kind::Enum &&
                            resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt)) {
                            scope_[bindings[bi]] = fp;
                            let_vars_.insert(bindings[bi]);
                            var_tagged_enum_.insert(bindings[bi]);
                            var_struct_.erase(bindings[bi]);
                            var_tuple_.erase(bindings[bi]);
                            var_elem_types_.erase(bindings[bi]);
                            var_tagged_enum_ptr_.erase(bindings[bi]);
                            continue;
                        }
                        bool is_inline_struct = lt &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                             TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                             TypeRef(lt).kind() == LogosType::Kind::Slice ||
                             TypeRef(lt).kind() == LogosType::Kind::Closure);
                        if (is_inline_struct &&
                            (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                             TypeRef(lt).kind() == LogosType::Kind::ZonedStruct)) {
                            // Bind `fp` (the inline struct payload address). A
                            // move-type payload bound BY VALUE must be COPIED to
                            // a fresh slot: it semantically MOVES out, so a later
                            // mutation of the scrutinee place within the arm
                            // (`a.1 = …` in issue-19367) must not clobber the
                            // binding. The source's drop is suppressed by
                            // mark_match_scrutinee_moved, so copy + skip = correct
                            // (no double-free). A Copy struct could alias safely,
                            // but copying is always sound.
                            mlir::Value bind_ptr = fp;
                            if (lt && value_needs_drop(lt)) {
                                auto sit = struct_types_.find(mlir_struct_key(lt));
                                if (sit != struct_types_.end() && sit->second.llvm_type) {
                                    auto fresh = create_entry_alloca(sit->second.llvm_type);
                                    builder_.create<mlir::LLVM::MemcpyOp>(
                                        loc_, fresh, fp, size_const(lt), /*isVolatile=*/false);
                                    bind_ptr = fresh;
                                }
                            }
                            scope_[bindings[bi]] = bind_ptr;
                            let_vars_.insert(bindings[bi]);
                            var_struct_[bindings[bi]] = mlir_struct_key(lt);
                        } else {
                            // A bare `&dyn`/`dyn`/`Box<dyn>` (TraitObject) payload is
                            // stored INLINE as a 16-byte fat pair; its slot ADDRESS
                            // (fp) IS the fat value — bind it directly, don't load
                            // the 16-byte aggregate. (A `*const dyn` = Ptr<TraitObject>
                            // payload is a thin 8-byte ptr → still loaded below.)
                            bool bind_inline_dyn = lt &&
                                TypeRef(lt).kind() == LogosType::Kind::TraitObject;
                            mlir::Value bound_val;
                            if (is_inline_struct || bind_inline_dyn) {
                                bound_val = fp;
                            } else {
                                bound_val = builder_.create<mlir::LLVM::LoadOp>(
                                    loc_, vp->field_types[bi], fp);
                            }
                            // TraitObject payload (e.g. `Option<&dyn T>`'s
                            // Some arm). Mirror gen_let's convention at
                            // line ~494: bind the 8-byte handle DIRECTLY
                            // as scope_[name] (no wrapping alloca) so
                            // dyn-dispatch's GEP through `recv_alloca`
                            // walks the heap-allocated {data,vtable} slot
                            // the handle points to, rather than the
                            // alloca's own bytes (which would treat the
                            // alloca as the fat-pair — only 8 bytes are
                            // initialised, the vtable load lands in
                            // adjacent stack garbage and segfaults at
                            // dispatch). Without this branch, gen_let's
                            // direct-handle convention and match-extract's
                            // alloca-of-handle convention diverged at
                            // every `Option<&dyn T>` use.
                            TypeRef payload_lt = lt;
                            bool is_ref_to_trait = payload_lt &&
                                (TypeRef(payload_lt).kind() == LogosType::Kind::Ref ||
                                 TypeRef(payload_lt).kind() == LogosType::Kind::MutRef ||
                                 TypeRef(payload_lt).kind() == LogosType::Kind::Ptr) &&
                                TypeRef(payload_lt).pointee() &&
                                TypeRef(TypeRef(payload_lt).pointee()).kind()
                                    == LogosType::Kind::TraitObject;
                            bool is_bare_trait = payload_lt &&
                                TypeRef(payload_lt).kind() == LogosType::Kind::TraitObject;
                            if (is_bare_trait || is_ref_to_trait) {
                                TypeRef trait_t = is_bare_trait
                                    ? payload_lt
                                    : TypeRef(payload_lt).pointee();
                                scope_[bindings[bi]] = bound_val;
                                let_vars_.insert(bindings[bi]);
                                var_dyn_trait_[bindings[bi]] =
                                    std::string(TypeRef(trait_t).trait_name());
                                continue;
                            }
                            auto alloca = create_entry_alloca(vp->field_types[bi]);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, bound_val, alloca);
                            scope_[bindings[bi]] = alloca;
                            let_vars_.insert(bindings[bi]);
                            var_elem_types_[bindings[bi]] = vp->field_types[bi];
                            // Clear OTHER shape-tracking sets — the name may
                            // already be bound in the outer scope to a
                            // different-shape value (e.g. outer
                            // `let b: ControlFlow<...>` then inner pattern
                            // `Break(b) => …` binds b: i32). Without
                            // clearing, gen_expr(VarRef(b)) consults the
                            // stale var_tagged_enum_ flag and returns the
                            // alloca-ptr unloaded — yielding `(ptr, i32)`
                            // type mismatch at the first use of b in the
                            // arm body. scope_ already overwrote correctly;
                            // peer-tracking sets need the same eviction.
                            var_struct_.erase(bindings[bi]);
                            var_subscript_.erase(bindings[bi]);
                            var_tuple_.erase(bindings[bi]);
                            var_tagged_enum_.erase(bindings[bi]);
                            var_tagged_enum_ptr_.erase(bindings[bi]);
                            var_dyn_trait_.erase(bindings[bi]);
                            var_local_ptrs_.erase(bindings[bi]);
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
            // P4-pm-08: scrut may arrive as a by-value struct (e.g. when
            // it came directly from a fn return); GEP needs a pointer, so
            // spill to an alloca first.
            if (sptr.getType() != ptr_type()) {
                auto a = create_entry_alloca(sptr.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sptr, a);
                sptr = a;
            }
            const LStructDef* sd = nullptr;
            if (auto di = all_struct_defs_.find(sname); di != all_struct_defs_.end())
                sd = di->second;
            ps.each_field([&](lir_view::PatFieldBindingView pfb) {
                std::string field_name(pfb.field_name());
                auto bind_struct_field = [&](const std::string& bind_name) {
                    auto fp = gep_field(sptr, sinfo, field_name);
                    if (!fp) return;
                    // A struct-typed field is stored INLINE; bind its GEP ADDRESS
                    // (a place) + track shape — NOT a load/copy. The copy didn't
                    // persist mutation through a `&mut` binding (the change hit a
                    // local alloca, not the scrutinee) and only "worked" for
                    // shared reads because the &-typed binding's Drop is skipped.
                    // Mirrors the tuple-element / pat_bind aggregate bind.
                    TypeRef fty;
                    if (sd) for (auto& lf : sd->fields)
                        if (lf.name == field_name) { fty = lf.type; break; }
                    if (fty && (TypeRef(fty).kind() == LogosType::Kind::Struct ||
                                TypeRef(fty).kind() == LogosType::Kind::ZonedStruct)) {
                        scope_[bind_name] = fp;
                        let_vars_.insert(bind_name);
                        var_struct_[bind_name] = mlir_struct_key(fty);
                        return;
                    }
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
                            // A struct-typed field: bind `fp` directly + track
                            // struct shape so `px.field` GEPs through it. The
                            // alloca-wrap (ptr-of-ptr, no shape) made `px.field`
                            // read GARBAGE — the silent miscompile, sibling of
                            // the enum-payload G151-1 fix. Scalar fields keep
                            // the alloca-wrap so `*px` derefs one level.
                            TypeRef fty;
                            if (sd) for (auto& lf : sd->fields)
                                if (lf.name == field_name) { fty = lf.type; break; }
                            bool ref_to_struct = fty &&
                                (TypeRef(fty).kind() == LogosType::Kind::Struct ||
                                 TypeRef(fty).kind() == LogosType::Kind::ZonedStruct);
                            if (ref_to_struct) {
                                scope_[prbn] = fp;
                                let_vars_.insert(prbn);
                                var_struct_[prbn] = mlir_struct_key(fty);
                            } else {
                                auto alloca = create_entry_alloca(ptr_type());
                                builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                                scope_[prbn] = alloca;
                                let_vars_.insert(prbn);
                                var_elem_types_[prbn] = ptr_type();
                            }
                        }
                    }
                } else {
                    // G148-1: refutable field sub-pattern (variant / tuple /
                    // or / nested struct) — bind its inner names via the
                    // recursive matcher. fp is a pointer to the field slot.
                    auto fp = gep_field(sptr, sinfo, field_name);
                    if (fp) {
                        TypeRef fty;
                        if (sd) for (auto& lf : sd->fields)
                            if (lf.name == field_name) { fty = lf.type; break; }
                        pat_bind(sub, fp, fty);
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
            } else if (atype && TypeRef(atype).kind() == LogosType::Kind::Slice &&
                       TypeRef(atype).elem()) {
                // G149-4: dynamic `&[T]` slice. scrut is a fat pointer
                // {data, i64 len}. Bind prefix elements by-value through the
                // data pointer; bind a named rest (`xs @ ..`) to a freshly
                // built sub-slice {data + prefix*sizeof(T), len - prefix}.
                auto elem_mlir = logos_to_mlir(TypeRef(atype).elem());
                auto sdtype = slice_llvm_type();
                mlir::Value sptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                if (sptr && elem_mlir) {
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sptr, di);
                    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
                    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sptr, li);
                    auto slen = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
                    lir_view::PatSliceView psl{p};
                    auto bind_at = [&](lir_view::PatRef sp, mlir::Value idx) {
                        if (!sp) return;
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, data, gi);
                        if (sp.kind() == pc::Code::Wild) {
                            std::string pwn(lir_view::PatWildView{sp}.name());
                            if (pwn == "_" || pwn.empty()) return;
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto a = create_entry_alloca(elem_mlir);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, a);
                            scope_[pwn] = a; let_vars_.insert(pwn);
                            var_elem_types_[pwn] = elem_mlir;
                        } else if (sp.kind() == pc::Code::RefBind) {
                            std::string prbn(lir_view::PatRefBindView{sp}.name());
                            if (prbn == "_" || prbn.empty()) return;
                            auto a = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, ep, a);
                            scope_[prbn] = a; let_vars_.insert(prbn);
                            var_elem_types_[prbn] = ptr_type();
                        }
                    };
                    auto i64c = [&](int64_t k){
                        return builder_.create<mlir::arith::ConstantIntOp>(loc_, k, 64).getResult();
                    };
                    int32_t pre_n = 0;
                    psl.each_prefix([&](lir_view::PatRef sp){ bind_at(sp, i64c(pre_n++)); });
                    // G167-6a: suffix elements bind from the tail at `len - suf_n + i`.
                    size_t suf_n = psl.suffix_count();
                    {
                        size_t i = 0;
                        psl.each_suffix([&](lir_view::PatRef sp){
                            mlir::Value idx = builder_.create<mlir::arith::SubIOp>(
                                loc_, slen, i64c((int64_t)(suf_n - i)));
                            bind_at(sp, idx);
                            ++i;
                        });
                    }
                    // G167-6b: named rest → sub-slice {data + pre_n, len - pre_n - suf_n}
                    // bound as a first-class `&[T]` PLACE (var_slice_, NOT a raw
                    // struct value) so `rest.len()` / re-matching `rest` work —
                    // previously typed as the {ptr,i64} struct, so a `.len()` GEP
                    // hit the struct value and crashed MLIR-gen.
                    if (auto rest = psl.rest()) {
                        std::string rn(lir_view::PatWildView{rest}.name());
                        if (!rn.empty() && rn != "_") {
                            auto rdata = builder_.create<mlir::LLVM::GEPOp>(
                                loc_, ptr_type(), elem_mlir, data,
                                llvm::SmallVector<mlir::LLVM::GEPArg>{i64c((int64_t)pre_n)});
                            auto rlen = builder_.create<mlir::arith::SubIOp>(
                                loc_, slen, i64c((int64_t)(pre_n + (int)suf_n)));
                            auto sub = create_entry_alloca(sdtype);
                            auto sdp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sub,
                                llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(0)});
                            builder_.create<mlir::LLVM::StoreOp>(loc_, rdata, sdp);
                            auto slp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sub,
                                llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(1)});
                            builder_.create<mlir::LLVM::StoreOp>(loc_, rlen, slp);
                            scope_[rn] = sub;
                            var_slice_[rn] = elem_mlir;
                        }
                    }
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
                // P4-pm-17: if scrut is itself a pointer (e.g. `&T` from
                // an outer PatRefPat over a borrowed scrutinee), the
                // binding `a` aliases the pointer value directly — no
                // extra spill-then-store-addr indirection. Otherwise we
                // need to spill the scrut value to get an address.
                mlir::Value bind_val;
                if (scrut_ptr) {
                    bind_val = scrut_ptr;
                } else if (scrut.getType() == ptr_type()) {
                    bind_val = scrut;
                } else {
                    auto tmp = create_entry_alloca(scrut.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    bind_val = tmp;
                }
                auto alloca = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, bind_val, alloca);
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
                TypeRef st = scrut_le ? TypeRef(scrut_le->type) : TypeRef{};
                if (st && (st.kind() == LogosType::Kind::Struct ||
                           st.kind() == LogosType::Kind::ZonedStruct)) {
                    // Whole-value struct binding (`match v { x => … }` for an
                    // owned struct): `sv` is already the POINTER to the
                    // scrutinee's struct (structs are by-pointer). Alias that
                    // storage directly + record the struct key, so `x` sees the
                    // real fields and its drop glue frees the real buffer once.
                    // The store-into-a-fresh-alloca path below would make `x`
                    // hold a pointer-to-struct typed as the struct, so drop
                    // would read the (stack) pointer value as the first field
                    // and free a bogus address (SIGSEGV). The scrutinee var is
                    // marked moved in sema (lower_match), so it isn't dropped
                    // a second time.
                    scope_[pwn] = sv;
                    var_struct_[pwn] = mlir_struct_key(st);
                    let_vars_.insert(pwn);
                } else {
                    auto alloca = create_entry_alloca(sv.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                    scope_[pwn] = alloca;
                    let_vars_.insert(pwn);
                    var_elem_types_[pwn] = sv.getType();
                }
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
        // G155-5(a): explicit `&E::Foo{..}` / `&E::Some(x)` ref-pattern over a
        // `&Enum` scrutinee we already auto-deref'd to a TAGGED enum (te_info
        // set ⇒ `scrut` is the disc, `scrut_ptr` the enum struct). Peel the
        // redundant leading `&` so the inner variant/struct pattern flows
        // through the normal payload-extracting paths. The C-like no-payload
        // `&E::A` case (te_info null, `scrut` still a ptr-to-i32) keeps the
        // dedicated RefPat handler below.
        if (te_info && arm_kind == pc::Code::RefPat) {
            if (auto inner = lir_view::PatRefPatView{arm_pat}.inner();
                inner && (inner.kind() == pc::Code::VariantData ||
                          inner.kind() == pc::Code::Variant ||
                          inner.kind() == pc::Code::Struct)) {
                arm_pat = inner;
                arm_kind = arm_pat.kind();
            }
        }
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
                auto alt = alts[static_cast<size_t>(ai)];
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                // G144-2: a wildcard / binding alt (`0 | _`) is irrefutable —
                // match unconditionally rather than emit a bogus
                // `scrut == get_scalar_disc(Wild)` test (wrong match + a
                // dead-block arith.constant that fails LLVM translation).
                if (alt.kind() == pc::Code::Wild || alt.kind() == pc::Code::RefBind) {
                    builder_.create<mlir::cf::BranchOp>(loc_, arm_entry);
                    cur_else = test_block;
                    continue;
                }
                int64_t disc = get_scalar_disc(alt);
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
                if (sub.kind() == pc::Code::Or) {
                    // `n @ (1 | 2 | 3)` / `n @ (lo..=hi | …)` — bind the whole
                    // value to `n` (handled by the PatAt binding path) and gate
                    // the arm on the OR of each alternative's test. Mirrors the
                    // tuple-element or-pattern OR-chain; supports int/bool/range
                    // alternatives (the scalar pattern kinds an at-binding admits).
                    auto* test_block = new mlir::Block();
                    region->push_back(test_block);
                    {
                        mlir::OpBuilder::InsertionGuard ig(builder_);
                        builder_.setInsertionPointToStart(test_block);
                        auto at_pred_ge = scrut_unsigned() ? mlir::arith::CmpIPredicate::uge
                                                          : mlir::arith::CmpIPredicate::sge;
                        auto at_pred_le = scrut_unsigned() ? mlir::arith::CmpIPredicate::ule
                                                          : mlir::arith::CmpIPredicate::sle;
                        mlir::Value alt_or =
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
                        lir_view::PatOrView{sub}.each_alt([&](lir_view::PatRef alt) {
                            if (alt.kind() == pc::Code::Range) {
                                lir_view::PatRangeView pr{alt};
                                auto lo_val = coerce_int(
                                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.lo(), 64), scrut_type);
                                auto hi_val = coerce_int(
                                    builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.hi(), 64), scrut_type);
                                auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_ge, scrut, lo_val);
                                auto le = builder_.create<mlir::arith::CmpIOp>(loc_, at_pred_le, scrut, hi_val);
                                auto both = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
                                alt_or = builder_.create<mlir::arith::OrIOp>(loc_, alt_or, both);
                                return;
                            }
                            int64_t av = 0;
                            if (alt.kind() == pc::Code::Int)       av = lir_view::PatIntView{alt}.value();
                            else if (alt.kind() == pc::Code::Bool) av = lir_view::PatBoolView{alt}.value() ? 1 : 0;
                            else return;  // skip unsupported alt kind
                            auto cv = coerce_int(
                                builder_.create<mlir::arith::ConstantIntOp>(loc_, av, 64), scrut_type);
                            auto eq = builder_.create<mlir::arith::CmpIOp>(
                                loc_, mlir::arith::CmpIPredicate::eq, scrut, cv);
                            alt_or = builder_.create<mlir::arith::OrIOp>(loc_, alt_or, eq);
                        });
                        builder_.create<mlir::cf::CondBranchOp>(loc_, alt_or, arm_entry, else_block);
                    }
                    else_block = test_block;
                } else if (sub.kind() == pc::Code::Range) {
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
            // [UNIFY D-tuple] Route the whole refutable-tuple structural test
            // through the single pat_test foundation. A tuple value IS a pointer
            // to its inline storage (Rust by-value layout), so hand pat_test the
            // tuple base pointer DIRECTLY — its Tuple case GEPs into it (no load).
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value tptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                mlir::Value cond = pat_test(arm_pat, tptr, scrut_le->type);
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_kind == pc::Code::Slice &&
                   scrut_le->type &&
                   (TypeRef(scrut_le->type).kind() == LogosType::Kind::Array ||
                    // G160-4: a `&[u8; N]` / `&mut [u8; N]` scrutinee (byte-string
                    // pattern over a ref-to-array) — peel the ref below.
                    ((TypeRef(scrut_le->type).kind() == LogosType::Kind::Ref ||
                      TypeRef(scrut_le->type).kind() == LogosType::Kind::MutRef) &&
                     TypeRef(scrut_le->type).pointee() &&
                     TypeRef(TypeRef(scrut_le->type).pointee()).kind() == LogosType::Kind::Array))) {
            // P4-pm-04: refutable slice pattern on fixed-size array.
            // GEP each scalar sub-element and AND-chain equality tests.
            // PatWild sub-patterns contribute no constraint. Suffix
            // indices are computed from arr_size - suffix_count.
            // (Dynamic slice scrutinees deferred — would need length
            // check and runtime-known arr_size.)
            lir_view::PatSliceView sv{arm_pat};
            // G160-4: peel a `&[u8; N]` ref — the ref value IS the array base
            // pointer (one level, like `&struct`), so `aptr` below works for
            // both the by-value array and the ref forms.
            TypeRef atyp = scrut_le->type;
            if ((TypeRef(atyp).kind() == LogosType::Kind::Ref ||
                 TypeRef(atyp).kind() == LogosType::Kind::MutRef) &&
                TypeRef(atyp).pointee())
                atyp = TypeRef(atyp).pointee();
            auto elem_mlir = logos_to_mlir(TypeRef(atyp).elem());
            auto arr_mlir  = logos_to_mlir(atyp);
            size_t total   = (size_t)TypeRef(atyp).arr_size();
            size_t suf_n   = sv.suffix_count();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value aptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                auto chk_at = [&](lir_view::PatRef sp, int32_t idx) {
                    if (!sp || sp.kind() == pc::Code::Wild) return;
                    int64_t sub_val = 0;
                    if      (sp.kind() == pc::Code::Int)  sub_val = lir_view::PatIntView{sp}.value();
                    else if (sp.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sp}.value() ? 1 : 0;
                    else return;
                    if (!elem_mlir || !arr_mlir) return;
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
        } else if (arm_kind == pc::Code::Slice &&
                   scrut_le->type &&
                   TypeRef(scrut_le->type).kind() == LogosType::Kind::Slice &&
                   TypeRef(scrut_le->type).elem()) {
            // G149-4: top-level dynamic-slice (`&[T]`) match arm. The scrut is
            // a fat pointer `{data, i64 len}`; gate on the runtime length
            // (== prefix when no rest, >= prefix+suffix with a `..`) and
            // AND-chain any literal prefix-element checks. Element/rest
            // bindings are emitted by extract_payload's dynamic-slice case.
            // (Previously this fell through to the default scalar-disc path,
            // which cmpi'd the slice pointer → silent wrong dispatch.)
            lir_view::PatSliceView sv{arm_pat};
            TypeRef stype_l = scrut_le->type;
            auto elem_mlir = logos_to_mlir(TypeRef(stype_l).elem());
            auto sdtype = slice_llvm_type();
            size_t pre_n = sv.prefix_count();
            size_t suf_n = sv.suffix_count();
            bool has_rest = (bool)sv.rest();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value sptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                // len = slot.1
                llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
                auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sptr, li);
                auto slen = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
                auto n = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, (int64_t)(pre_n + suf_n), 64);
                auto pred = has_rest ? mlir::arith::CmpIPredicate::sge
                                     : mlir::arith::CmpIPredicate::eq;
                mlir::Value cond = builder_.create<mlir::arith::CmpIOp>(loc_, pred, slen, n);
                // Literal prefix-element checks through the data pointer.
                if (elem_mlir && pre_n > 0) {
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sdtype, sptr, di);
                    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                    int32_t pi = 0;
                    sv.each_prefix([&](lir_view::PatRef sp){
                        int32_t idx = pi++;
                        if (!sp || sp.kind() == pc::Code::Wild) return;
                        int64_t sub_val = 0;
                        if      (sp.kind() == pc::Code::Int)  sub_val = lir_view::PatIntView{sp}.value();
                        else if (sp.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sp}.value() ? 1 : 0;
                        else return;
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(idx)};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, data, gi);
                        auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                        auto cv = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, sub_val, 64), elem_mlir);
                        auto eq = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                        cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                    });
                }
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_kind == pc::Code::RefPat &&
                   scrut_le->type &&
                   (TypeRef(scrut_le->type).kind() == LogosType::Kind::Ref ||
                    TypeRef(scrut_le->type).kind() == LogosType::Kind::MutRef) &&
                   TypeRef(scrut_le->type).pointee()) {
            // P4-pm-18: `match &T { &<scalar> => … }`. PatRefPat with
            // a scalar inner: deref the scrut (load) and cmp against
            // the inner disc. Without this the default arm-dispatch
            // would `cmpi` the raw ptr against an i32 — verifier crash.
            auto inner = lir_view::PatRefPatView{arm_pat}.inner();
            int64_t disc = inner ? get_scalar_disc(inner) : std::numeric_limits<int64_t>::min();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                if (disc == std::numeric_limits<int64_t>::min()) {
                    builder_.create<mlir::cf::BranchOp>(loc_, else_block);
                } else {
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
            }
            else_block = test_block;
        } else if (arm_kind == pc::Code::Struct) {
            // G148-1: struct arm with refutable field sub-patterns
            // (`Wrap { x: Inner::A(v), y } => …`). is_irrefutable already
            // routed fully-irrefutable struct patterns to is_wild; reaching
            // here means at least one field sub is refutable. Hand pat_test
            // the struct data ptr directly (same convention as Tuple); the
            // old alloca-of-ptr wrapper was the asymmetry that miscompiled
            // nested struct sub-patterns.
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value sptr = scrut_ptr ? scrut_ptr : gen_expr(*scrut_le);
                if (sptr && sptr.getType() != ptr_type()) {
                    auto a = create_entry_alloca(sptr.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, sptr, a);
                    sptr = a;
                }
                auto cond = pat_test(arm_pat, sptr, scrut_le->type);
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

    // G144-3a: or-pattern in let-else (`let A(x) | B(x) = v else …`). Collect
    // each alt's discriminant for an OR'd tag test, and extract bindings using
    // the FIRST alt's payload layout — all alts bind the same names+types at the
    // same payload offset (sema enforces it), mirroring the match or-pattern
    // path. Rebind pat_ref/pat_kind to the first alt for the extraction below.
    std::vector<int32_t> or_discs;
    if (pat_kind == pc::Code::Or) {
        lir_view::PatRef first_alt;
        lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a) {
            if (!first_alt) first_alt = a;
            if (a.kind() == pc::Code::Variant)
                or_discs.push_back((int32_t)lir_view::PatVariantView{a}.disc());
            else if (a.kind() == pc::Code::VariantData)
                or_discs.push_back((int32_t)lir_view::PatVariantDataView{a}.disc());
        });
        if (first_alt) { pat_ref = first_alt; pat_kind = first_alt.kind(); }
    }

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

    // Auto-deref `&Enum` / `&mut Enum` / `*Enum` (match ergonomics + nested
    // by-ref synths) so `let Some(v) = &opt else …` works like the match path.
    // Enum value-repr: a `&Enum` is a one-level pointer to the inline storage
    // (like `&Struct`), so no extra deref is needed.
    TypeRef sct(scrut_le->type);
    TypeRef enum_ct = sct;
    bool sle_via_ref = false;
    if (sct && (sct.kind() == LogosType::Kind::Ref ||
                sct.kind() == LogosType::Kind::MutRef ||
                sct.kind() == LogosType::Kind::Ptr) &&
        sct.pointee() && TypeRef(sct.pointee()).kind() == LogosType::Kind::Enum) {
        enum_ct = sct.pointee();
        sle_via_ref = true;
    }
    if (enum_ct && enum_ct.kind() == LogosType::Kind::Enum) {
        te_info = resolve_tagged_enum(std::string(enum_ct.enum_name()), enum_ct);
        if (te_info) {
            if (sle_via_ref) {
                // Enum value-repr: scrut_val (the `&Enum`) IS the inline storage
                // address (one level) — use it directly, no extra load.
                scrut_ptr = scrut_val;
            } else if (scrut_val.getType() != ptr_type()) {
                // Spill a by-value enum to an alloca.
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
        // A C-like (all-nullary) enum has no TaggedEnumInfo and is passed as a
        // bare i32 — the value IS the discriminant. Without this, disc_val
        // stayed null and the let-else matched UNCONDITIONALLY (the else block
        // became dead — a silent wrong result, even for a single pattern).
        // Mirrors the match path, which uses the i32 scrutinee directly.
        if (!disc_val && scrut_val &&
            mlir::isa<mlir::IntegerType>(scrut_val.getType()))
            disc_val = coerce_int(scrut_val, builder_.getI32Type());
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
        // Conditional branch on discriminant match. For an or-pattern, the
        // condition is the OR of each alt's discriminant test.
        mlir::Value cond;
        if (!or_discs.empty()) {
            for (int32_t d : or_discs) {
                auto dc = builder_.create<mlir::arith::ConstantIntOp>(loc_, d, 32);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, disc_val, dc);
                cond = cond ? builder_.create<mlir::arith::OrIOp>(loc_, cond, eq).getResult()
                            : eq.getResult();
            }
        } else {
            auto expected = builder_.create<mlir::arith::ConstantIntOp>(
                loc_, expected_disc, 32);
            cond = builder_.create<mlir::arith::CmpIOp>(
                loc_, mlir::arith::CmpIPredicate::eq, disc_val, expected);
        }
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, match_block, else_block);
    } else {
        // Non-enum scrutinee. A LITERAL / range pattern is REFUTABLE — test the
        // value and branch to else on mismatch (G154-5: previously this always
        // fell into match_block, so the else was dead and a non-matching literal
        // like `let 4 = x else {…}` was silently accepted). Irrefutable
        // non-enum patterns (tuple / plain binding) keep the unconditional jump.
        mlir::Value lit_cond;
        if (mlir::isa<mlir::IntegerType>(scrut_val.getType())) {
            auto styp = scrut_val.getType();
            if (pat_kind == pc::Code::Int) {
                auto cv = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, lir_view::PatIntView{pat_ref}.value(), 64), styp);
                lit_cond = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut_val, cv);
            } else if (pat_kind == pc::Code::Bool) {
                auto cv = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, lir_view::PatBoolView{pat_ref}.value() ? 1 : 0, 64), styp);
                lit_cond = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut_val, cv);
            } else if (pat_kind == pc::Code::Range) {
                lir_view::PatRangeView pr{pat_ref};
                auto lo = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.lo(), 64), styp);
                auto hi = coerce_int(builder_.create<mlir::arith::ConstantIntOp>(loc_, pr.hi(), 64), styp);
                auto ge = builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sge, scrut_val, lo);
                auto le = builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sle, scrut_val, hi);
                lit_cond = builder_.create<mlir::arith::AndIOp>(loc_, ge, le);
            }
        }
        if (lit_cond)
            builder_.create<mlir::cf::CondBranchOp>(loc_, lit_cond, match_block, else_block);
        else
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
                // SL-sl-03 follow-up: per-binding override types from
                // `ref v` / `ref mut v` patterns (sema wraps Ref/MutRef).
                // Bind directly to the GEP address so the binding actually
                // references the original payload slot.
                std::vector<TypeRef> pvd_binding_types;
                pvd.each_binding_type(pool_impl(),
                    [&](TypeRef t){ pvd_binding_types.push_back(t); });
                int32_t pvd_disc = static_cast<int32_t>(pvd.disc());
                llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                const TaggedEnumInfo::VariantPayload* vp = nullptr;
                for (auto& vinfo : te_info->variants)
                    if (vinfo.disc == pvd_disc) { vp = &vinfo; break; }
                if (vp && !bindings.empty()) {
                    auto pay_struct = variant_payload_struct(*vp);
                    for (size_t bi = 0; bi < bindings.size() &&
                                         bi < vp->field_types.size(); ++bi) {
                        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(bi)};
                        auto fp = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), pay_struct, pay_ptr, fi);
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
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                            scope_[bindings[bi]]          = alloca;
                            let_vars_.insert(bindings[bi]);
                            var_elem_types_[bindings[bi]] = ptr_type();
                            continue;
                        }
                        // Inline aggregate payload (struct / zoned-struct):
                        // the value is stored inline in the payload, so `fp`
                        // already points at its bytes — bind it directly as the
                        // struct pointer (mirrors extract_payload). Loading
                        // vp->field_types[bi] (a collapsed `ptr`) would read the
                        // struct's first 8 bytes as if they were the value
                        // (e.g. `Option<String>` via let-else → corrupt String).
                        TypeRef lt = bi < vp->logos_types.size()
                                          ? vp->logos_types[bi] : nullptr;
                        if (lt && (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                                   TypeRef(lt).kind() == LogosType::Kind::ZonedStruct)) {
                            scope_[bindings[bi]]      = fp;
                            let_vars_.insert(bindings[bi]);
                            var_struct_[bindings[bi]] = mlir_struct_key(lt);
                            continue;
                        }
                        // Enum value-repr: a nested TAGGED enum payload is INLINE
                        // — `fp` is its storage (one level). Bind as a tagged-enum
                        // var (no load). Crucial for the K4 nested-variant synth
                        // `let Some(inner_enum) = synth` over Some(Some(v)).
                        if (lt && TypeRef(lt).kind() == LogosType::Kind::Enum &&
                            resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt)) {
                            scope_[bindings[bi]] = fp;
                            let_vars_.insert(bindings[bi]);
                            var_tagged_enum_.insert(bindings[bi]);
                            var_struct_.erase(bindings[bi]);
                            var_tuple_.erase(bindings[bi]);
                            var_elem_types_.erase(bindings[bi]);
                            continue;
                        }
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

        // G161-3: refutable-inner guards (`__refut_N == value` for
        // `let Some(1) = … else`). The payload bindings (incl. the synth
        // `__refut_N`) are now in scope_; evaluate each guard and branch to the
        // else block if any fails — otherwise the inner literal/sub-pattern test
        // would be silently dropped (only the variant disc was checked).
        mlir::Value guard_cond;
        v.each_guard([&](lir_view::ExprRef g){
            auto* gle = lexpr_of(g);
            if (!gle) return;
            auto gv = gen_expr(*gle);
            if (!gv) return;
            if (gv.getType() != builder_.getI1Type())
                gv = coerce_int(gv, builder_.getI1Type());
            guard_cond = guard_cond
                ? builder_.create<mlir::arith::AndIOp>(loc_, guard_cond, gv).getResult()
                : gv;
        });
        if (guard_cond)
            builder_.create<mlir::cf::CondBranchOp>(loc_, guard_cond, cont_block, else_block);
        else
            builder_.create<mlir::cf::BranchOp>(loc_, cont_block);
    }

    // Continue in cont_block (bindings from match_block are now in scope_)
    builder_.setInsertionPointToStart(cont_block);
}

} // namespace logos::compiler
