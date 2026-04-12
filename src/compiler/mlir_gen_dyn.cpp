// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_dyn.cpp — Vtable emission, &dyn Trait coercion, dyn dispatch, closures.

#include "mlir_gen_impl.hpp"
#include <map>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// Vtable emission (Pass 1a)
// ---------------------------------------------------------------------------

void MLIRGenImpl::emit_vtable(mlir::ModuleOp /*mod*/, const LClassDef& /*cd*/) {}

// ---------------------------------------------------------------------------
// Trait vtable info (Pass 1b)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tag-dispatch tables (Pass 1c)
// ---------------------------------------------------------------------------
// For each unique (tag_system, trait, method) triplet derived from
// prog.dispatch_entries, emit a [223 x ptr] global array:
//
//   @__logos_tag_dispatch_<TS>_<Trait>_<method> = global [223 x ptr] { ... }
//
// Tier-1 entries (type_code 128-222): the corresponding slot holds the
// address of the impl function.  All other slots are null.
//
// Tier-2 entries (type_code >= 223): only a linker collision-detection
// symbol is emitted for now:
//   @__logos_typeops_<type_code_hex16>_<Trait>_<method> = global ptr @fn_symbol
//
// The table name is mangled once; any TU that sees an impl for the same
// (TS, Trait, method) will extend the SAME table (via linker sections in
// multi-TU builds; currently logosc is single-TU so it owns the whole table).

void MLIRGenImpl::emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog) {
    if (prog.dispatch_entries.empty()) return;

    auto ptr_t = ptr_type();
    constexpr int kTier1Size = 223;
    auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);

    // ── Pass A: collect tier-1 entries grouped by table name ──────────���──
    std::map<std::string, std::vector<std::pair<uint64_t, std::string>>> tables;
    for (auto& de : prog.dispatch_entries) {
        if (de.type_code == 0 || de.type_code >= static_cast<uint64_t>(kTier1Size)) continue;
        auto key = "__logos_tag_dispatch_" + de.tag_system + "_"
                 + de.trait_name + "_" + de.method_name;
        tables[key].emplace_back(de.type_code, de.fn_symbol);
    }
    if (tables.empty()) return;

    // ── Pass B: emit zero-initialized [223 x ptr] globals ────────────────
    // We emit a zero-value init region (ZeroOp) so the global is a *definition*
    // (not a declaration).  Function pointers are filled at runtime by the
    // __logos_tag_dispatch_init constructor (Pass C / mlir_gen.cpp Pass 3).
    for (auto& [table_name, entries] : tables) {
        if (mod.lookupSymbol(table_name)) continue;
        builder_.setInsertionPointToEnd(mod.getBody());
        auto glob = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConst=*/false,
            mlir::LLVM::Linkage::External, table_name,
            mlir::Attribute{}, 0);
        auto& init_region = glob.getInitializerRegion();
        auto* init_block  = builder_.createBlock(&init_region);
        builder_.setInsertionPointToStart(init_block);
        mlir::Value zero = builder_.create<mlir::LLVM::ZeroOp>(loc_, arr_type);
        builder_.create<mlir::LLVM::ReturnOp>(loc_, zero);
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // ── Pass C: emit func.func constructor that stores fn ptrs at startup ─
    // One constructor per module covers all tables.  Naming is deterministic
    // so that a second TU can merge cleanly (same symbol = linker dedup).
    auto ctor_name = std::string("__logos_tag_dispatch_init");
    if (!mod.lookupSymbol<mlir::func::FuncOp>(ctor_name)) {
        auto ctor_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
        builder_.setInsertionPointToEnd(mod.getBody());
        auto ctor_fn = builder_.create<mlir::func::FuncOp>(loc_, ctor_name, ctor_type);
        ctor_fn->setAttr("sym_visibility",
                         mlir::StringAttr::get(builder_.getContext(), "private"));
        auto* ctor_block = ctor_fn.addEntryBlock();
        builder_.setInsertionPointToStart(ctor_block);

        for (auto& [table_name, entries] : tables) {
            // Get pointer to the zero-initialized table global.
            auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, table_name);

            for (auto& [type_code, fn_sym] : entries) {
                auto callee = mod.lookupSymbol<mlir::func::FuncOp>(fn_sym);
                if (!callee) continue;
                // func.constant @fn → value of function type; cast to ptr.
                auto func_type = callee.getFunctionType();
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, func_type, fn_sym);
                auto fn_ptr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_t, mlir::ValueRange{fn_ref}).getResult(0);
                // GEP: [0, type_code] into [223 x ptr].
                mlir::Value idx_val = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(type_code), 64);
                llvm::SmallVector<mlir::LLVM::GEPArg> gep_idx;
                gep_idx.push_back(int32_t(0));
                gep_idx.push_back(idx_val);  // mlir::Value — dynamic GEP index
                auto slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, arr_type, table_addr, gep_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_ptr, slot);
            }
        }
        builder_.create<mlir::func::ReturnOp>(loc_);
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // Note: the constructor is a func.func, not a llvm.func. We cannot use
    // llvm.mlir.global_ctors here because its verifier requires llvm.func
    // symbols (which only exist after ConvertFuncToLLVM runs, after codegen).
    // Instead, mlir_gen.cpp injects a call to @__logos_tag_dispatch_init at
    // the start of main() after all function bodies are generated (Pass 3).
}

void MLIRGenImpl::emit_trait_vtables(mlir::ModuleOp /*mod*/, const LProgram& prog) {
    for (auto& td : prog.traits) {
        for (auto& ib : prog.impls) {
            if (ib.trait_name != td.name) continue;
            auto key = td.name + "::" + ib.target_type;
            std::vector<std::string> methods;
            for (auto& m : td.methods)
                methods.push_back(ib.target_type + "__" + m.name);
            dyn_vtable_methods_[key] = std::move(methods);
        }
    }
}

// ---------------------------------------------------------------------------
// Build inline vtable [N x ptr] heap-allocated for a concrete type.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::build_inline_vtable(const std::string& trait_name,
                                               const std::string& type_name) {
    auto key = trait_name + "::" + type_name;
    auto vit = dyn_vtable_methods_.find(key);
    if (vit == dyn_vtable_methods_.end()) return nullptr;
    auto& methods = vit->second;
    size_t n = methods.size();
    // Heap-allocate: n pointers × 8 bytes each.
    auto size_val = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n * 8), 64);
    auto vtable = call_malloc(size_val);
    if (!vtable) return nullptr;
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    for (size_t i = 0; i < n; ++i) {
        auto callee = parent_mod.lookupSymbol<mlir::func::FuncOp>(methods[i]);
        if (!callee) {
            std::fprintf(stderr, "mlir_gen: vtable: method '%s' not found\n",
                         methods[i].c_str());
            continue;
        }
        auto func_type = callee.getFunctionType();
        auto fn_ref = builder_.create<mlir::func::ConstantOp>(
            loc_, func_type, methods[i]);
        auto fn_addr = builder_.create<mlir::UnrealizedConversionCastOp>(
            loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
        // GEP: vtable is ptr to array of ptrs; element i at offset i*sizeof(ptr).
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(i)};
        auto slot = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), ptr_type(), vtable, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, slot);
    }
    return vtable;
}

// ---------------------------------------------------------------------------
// Build a &dyn Trait fat pointer from a concrete data_ptr.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::coerce_to_dyn(mlir::Value data_ptr, const std::string& trait_name,
                                        const std::string& src_type_name) {
    auto dyn_struct = mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});
    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), dyn_struct, i64_one());
    // Store data pointer at field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, alloca, idx0);
    builder_.create<mlir::LLVM::StoreOp>(loc_, data_ptr, dp);
    // Store vtable pointer at field 1
    auto vtable = build_inline_vtable(trait_name, src_type_name);
    if (vtable) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
        auto vp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, alloca, idx1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, vtable, vp);
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Indirect call through &tagged<TS> Trait tier-1 dispatch table.
//
// Dispatch sequence:
//   1. Evaluate receiver — this is a *const u8 pointing to the tagged object.
//   2. Read type_code via read_type_code(obj_ptr) → i64.
//   3. Bounds-check: type_code must be in [1, 222].  Out-of-range → trap.
//   4. GEP into @__logos_tag_dispatch_<TS>_<Trait>_<method>[type_code].
//   5. Load the stored fn ptr.
//   6. Build the LLVM function type from the actual args + return type.
//   7. Indirect call through the loaded ptr.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_tagged_dispatch(const EMethodCall& e,
                                              const LogosType* ret_logos_type) {
    constexpr int kTier1Size = 223;

    // 1. Evaluate the receiver (already a *const u8 in memory, stored as alloca or direct ptr).
    mlir::Value obj_ptr = nullptr;
    if (auto* vr = std::get_if<lir::EVarRef>(&e.receiver->kind)) {
        // Variable in scope: if it's a let-bound pointer, load from alloca.
        auto it = scope_.find(vr->name);
        if (it != scope_.end()) {
            // If stored in let_vars_ it's an alloca holding a ptr; load it.
            if (let_vars_.count(vr->name))
                obj_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
            else
                obj_ptr = it->second;
        }
    }
    if (!obj_ptr) obj_ptr = gen_expr(*e.receiver);
    if (!obj_ptr) return nullptr;

    // 2. Call read_type_code(obj_ptr) → type_code as i64.
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>("read_type_code");
    if (!rtc_fn) {
        std::fprintf(stderr, "gen_tagged_dispatch: 'read_type_code' not found in module\n");
        return nullptr;
    }
    auto type_code_val = builder_.create<mlir::func::CallOp>(
        loc_, rtc_fn, mlir::ValueRange{obj_ptr}).getResult(0);  // i64

    // 3. GEP into @__logos_tag_dispatch_<TS>_<Trait>_<method>[type_code].
    //    No bounds check — type_code always comes from write_tag with a valid code.
    auto table_sym = "__logos_tag_dispatch_" + e.tag_system + "_"
                   + e.tag_trait + "_" + e.method;
    auto table_global = parent_mod.lookupSymbol<mlir::LLVM::GlobalOp>(table_sym);
    if (!table_global) {
        std::fprintf(stderr, "gen_tagged_dispatch: table '%s' not found\n", table_sym.c_str());
        return nullptr;
    }

    auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_type(), kTier1Size);
    auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
        loc_, ptr_type(), table_sym);

    // GEPArg: int32_t for static (ptr→array), mlir::Value for dynamic (element index).
    llvm::SmallVector<mlir::LLVM::GEPArg> gep_idx;
    gep_idx.push_back(int32_t(0));       // ptr → array base
    gep_idx.push_back(type_code_val);    // dynamic slot index
    auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), arr_type, table_addr, gep_idx);

    // 4. Load fn_ptr from the slot.
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot_ptr);

    // 5. Build call args: obj_ptr (self as *const u8) + user args.
    llvm::SmallVector<mlir::Value> args;
    args.push_back(obj_ptr);
    llvm::SmallVector<mlir::Type> param_types;
    param_types.push_back(ptr_type());  // self: *const u8

    for (auto& a : e.args) {
        auto v = gen_expr(*a);
        if (!v) return nullptr;
        args.push_back(v);
        param_types.push_back(v.getType());
    }

    // 6. Build LLVM function type and call indirectly.
    mlir::Type ret_type;
    if (ret_logos_type && ret_logos_type->kind != LogosType::Kind::Void)
        ret_type = logos_to_mlir(ret_logos_type);
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Indirect call through &dyn Trait vtable.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_dyn_dispatch(const EMethodCall& e,
                                           const LogosType* ret_logos_type) {
    // The receiver is a &dyn Trait — a pointer to {data_ptr, vtable_ptr}.

    // Check if receiver is a variable we know is dyn
    mlir::Value recv_alloca = nullptr;
    if (auto* vr = std::get_if<EVarRef>(&e.receiver->kind)) {
        auto it = scope_.find(vr->name);
        if (it != scope_.end()) recv_alloca = it->second;
    }
    if (!recv_alloca) {
        recv_alloca = gen_expr(*e.receiver);
    }
    if (!recv_alloca) return nullptr;

    auto dyn_struct = mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});

    // Load data_ptr (field 0)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx0);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);

    // Load vtable_ptr (field 1)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
    auto vp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx1);
    auto vtable_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vp);

    // GEP into vtable array to get fn_ptr at vtable_index
    llvm::SmallVector<mlir::LLVM::GEPArg> slot_idx{int32_t(e.vtable_index)};
    auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), ptr_type(), vtable_ptr, slot_idx);
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot_ptr);

    // Build args: data_ptr (self) + user args
    llvm::SmallVector<mlir::Value> args;
    args.push_back(data_ptr);
    for (auto& a : e.args) {
        auto v = gen_expr(*a);
        if (!v) return nullptr;
        args.push_back(v);
    }

    // Build LLVM function type for the indirect call.
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& a : args) param_types.push_back(a.getType());

    mlir::Type ret_type;
    if (ret_logos_type && ret_logos_type->kind != LogosType::Kind::Void) {
        ret_type = logos_to_mlir(ret_logos_type);
    }
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    // Indirect call via function pointer (same pattern as closure calls)
    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Closure generation
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_closure(const EClosure& e, const LogosType*) {
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt = builder_.saveInsertionPoint();

    std::vector<bool> capture_is_struct(e.captures.size(), false);
    std::vector<bool> capture_is_class(e.captures.size(), false);
    std::vector<bool> capture_is_array(e.captures.size(), false);
    std::vector<bool> capture_is_tuple(e.captures.size(), false);
    std::vector<bool> capture_is_enum(e.captures.size(), false);
    std::vector<bool> capture_is_dyn(e.captures.size(), false);
    std::vector<bool> capture_is_pointer_repr(e.captures.size(), false);
    for (size_t i = 0; i < e.captures.size(); ++i) {
        const auto& name = e.captures[i];
        capture_is_struct[i] = var_struct_.count(name);
        capture_is_class[i]  = var_class_.count(name);
        capture_is_array[i]  = var_subscript_.count(name);
        capture_is_tuple[i]  = var_tuple_.count(name);
        capture_is_enum[i]   = var_tagged_enum_.count(name);
        capture_is_dyn[i]    = var_dyn_trait_.count(name);
        capture_is_pointer_repr[i] =
            capture_is_struct[i] || capture_is_class[i] || capture_is_array[i] ||
            capture_is_tuple[i] || capture_is_enum[i] || capture_is_dyn[i];
    }

    // Build capture struct type.
    // Pointer-represented locals (structs, classes, arrays, tuples, tagged enums,
    // closures, dyn trait fat pointers) must stay pointers inside the env.
    llvm::SmallVector<mlir::Type> cap_fields;
    for (size_t i = 0; i < e.capture_types.size(); ++i) {
        auto* ct = e.capture_types[i];
        mlir::Type ft;
        if (capture_is_pointer_repr[i])
            ft = ptr_type();
        else
            ft = logos_to_mlir(ct);
        if (!ft) ft = builder_.getI32Type();
        cap_fields.push_back(ft);
    }
    auto cap_struct = cap_fields.empty()
        ? mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), {builder_.getI8Type()})
        : mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), cap_fields);

    // Build function type: (env_ptr, params...) -> ret
    llvm::SmallVector<mlir::Type> fn_params;
    fn_params.push_back(ptr_type());  // env pointer
    for (auto& p : e.params) {
        auto pt = logos_to_mlir(p.type);
        if (pt) fn_params.push_back(pt);
    }
    llvm::SmallVector<mlir::Type> fn_rets;
    if (e.ret_type) {
        auto rt = logos_to_mlir(e.ret_type);
        if (rt) fn_rets.push_back(rt);
    }
    // Create the closure function as llvm.func (so llvm.mlir.addressof works)
    builder_.setInsertionPointToEnd(parent_mod.getBody());
    mlir::Type llvm_ret = fn_rets.empty()
        ? mlir::LLVM::LLVMVoidType::get(builder_.getContext()) : fn_rets[0];
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(llvm_ret, fn_params, false);
    auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, e.closure_id, llvm_fn_type);
    fn.setLinkage(mlir::LLVM::Linkage::Private);
    auto* entry = fn.addEntryBlock(builder_);
    builder_.setInsertionPointToStart(entry);

    // Save/restore mlir_gen state
    auto saved_scope       = scope_;
    auto saved_lets        = let_vars_;
    auto saved_elems       = var_elem_types_;
    auto saved_ret         = cur_ret_type_;
    auto saved_struct      = var_struct_;
    auto saved_class       = var_class_;
    auto saved_subscript   = var_subscript_;
    auto saved_tuple       = var_tuple_;
    auto saved_te          = var_tagged_enum_;
    auto saved_te_ptr      = var_tagged_enum_ptr_;
    auto saved_local_ptrs  = var_local_ptrs_;
    auto saved_dyn_trait   = var_dyn_trait_;
    auto saved_loop_stack  = loop_stack_;
    scope_.clear(); let_vars_.clear(); var_elem_types_.clear();
    var_struct_.clear(); var_class_.clear(); var_subscript_.clear();
    var_tuple_.clear(); var_tagged_enum_.clear(); var_tagged_enum_ptr_.clear();
    var_local_ptrs_.clear(); var_dyn_trait_.clear(); loop_stack_.clear();

    bool ret_is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(llvm_ret);
    cur_ret_type_ = ret_is_void ? mlir::Type{} : llvm_ret;

    // Unpack captures from env pointer (arg 0)
    auto env_ptr = entry->getArgument(0);
    for (size_t i = 0; i < e.captures.size(); ++i) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_ptr, idx);
        auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, cap_fields[i], fp);

        const LogosType* ct = e.capture_types[i];
        bool is_struct_cap = capture_is_struct[i];
        bool is_class_cap  = capture_is_class[i];
        bool is_array_cap  = capture_is_array[i];
        bool is_tuple_cap  = capture_is_tuple[i];
        bool is_enum_cap   = capture_is_enum[i];
        bool is_dyn_cap    = capture_is_dyn[i];
        if (is_struct_cap || is_class_cap || is_array_cap ||
            is_tuple_cap || is_enum_cap || is_dyn_cap) {
            // val is a pointer-like capture — restore the same variable shape
            // the enclosing scope used so downstream codegen can treat it
            // identically to a normal local variable.
            scope_[e.captures[i]] = val;
            if (is_struct_cap)
                var_struct_[e.captures[i]] = ct->struct_name;
            else if (is_class_cap)
                var_class_[e.captures[i]] = ct->struct_name;
            else if (is_array_cap)
                var_subscript_[e.captures[i]] = logos_to_mlir(ct ? ct->elem : nullptr);
            else if (is_tuple_cap)
                var_tuple_.insert(e.captures[i]);
            else if (is_enum_cap)
                var_tagged_enum_.insert(e.captures[i]);
            else if (is_dyn_cap && ct)
                var_dyn_trait_[e.captures[i]] = ct->trait_name;
        } else {
            // Scalar capture: store in alloca for let-variable semantics.
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), cap_fields[i], i64_one());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            scope_[e.captures[i]] = alloca;
            let_vars_.insert(e.captures[i]);
            var_elem_types_[e.captures[i]] = cap_fields[i];
        }
    }

    // Bind params (starting from arg 1)
    for (size_t i = 0; i < e.params.size(); ++i) {
        scope_[e.params[i].name] = entry->getArgument(i + 1);
    }

    // Generate body (inside llvm.func — use llvm.return)
    bool saved_in_llvm = in_llvm_func_;
    in_llvm_func_ = true;
    gen_block(e.body);
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
    in_llvm_func_ = saved_in_llvm;

    // Restore state
    scope_              = saved_scope;
    let_vars_           = saved_lets;
    var_elem_types_     = saved_elems;
    cur_ret_type_       = saved_ret;
    var_struct_         = saved_struct;
    var_class_          = saved_class;
    var_subscript_      = saved_subscript;
    var_tuple_          = saved_tuple;
    var_tagged_enum_    = saved_te;
    var_tagged_enum_ptr_ = saved_te_ptr;
    var_local_ptrs_     = saved_local_ptrs;
    var_dyn_trait_      = saved_dyn_trait;
    loop_stack_         = saved_loop_stack;
    builder_.restoreInsertionPoint(save_pt);

    // At the creation site: alloca capture struct, store captures
    auto env_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), cap_struct, i64_one());
    for (size_t i = 0; i < e.captures.size(); ++i) {
        auto it = scope_.find(e.captures[i]);
        if (it == scope_.end()) continue;
        mlir::Value cap_val;
        bool pointer_repr = capture_is_pointer_repr[i];
        auto eit = var_elem_types_.find(e.captures[i]);
        if (pointer_repr)
            cap_val = it->second;
        else if (let_vars_.count(e.captures[i]) && eit != var_elem_types_.end())
            cap_val = builder_.create<mlir::LLVM::LoadOp>(loc_, eit->second, it->second);
        else
            cap_val = it->second;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, cap_val, fp);
    }

    // Build closure value: { fn_ptr, env_ptr }
    auto ctype = closure_llvm_type();
    auto closure_alloca = builder_.create<mlir::LLVM::AllocaOp>(
        loc_, ptr_type(), ctype, i64_one());
    // Store fn_ptr
    auto fn_addr = builder_.create<mlir::LLVM::AddressOfOp>(
        loc_, ptr_type(), e.closure_id);
    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, fi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp);
    // Store env_ptr
    llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, ei);
    builder_.create<mlir::LLVM::StoreOp>(loc_, env_alloca, ep);

    return closure_alloca;
}

} // namespace logos::compiler
