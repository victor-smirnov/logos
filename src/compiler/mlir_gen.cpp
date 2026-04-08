// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos L-IR to MLIR.
//
// Input: lir::LProgram (typed IR produced by sema_lower()).
// Output: mlir::ModuleOp.
//
// All type information is pre-computed in L-IR — no Hermes lookups here.

#include "mlir_gen.hpp"

#include <logos/compiler/lir.hpp>

#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cstdio>
#include <variant>

namespace logos::compiler {
namespace {

using namespace lir;

// ---------------------------------------------------------------------------
// Struct type registry (MLIR-level)
// ---------------------------------------------------------------------------

struct FieldInfo {
    std::string name;
    mlir::Type  type;
    uint32_t    index;
    std::string struct_name;   // non-empty if field is *struct
};

struct StructInfo {
    std::string                  name;
    mlir::LLVM::LLVMStructType   llvm_type;
    std::vector<FieldInfo>       fields;
};

// Tagged enum registry: { i32 discriminant, [payload_bytes x i8] }
struct TaggedEnumInfo {
    std::string                         name;
    mlir::LLVM::LLVMStructType          llvm_type;
    uint64_t                            payload_bytes = 0;
    // Per-variant payload LLVM types (for bitcasting the payload area)
    struct VariantPayload {
        int32_t disc;
        std::vector<mlir::Type> field_types;  // empty = no payload
    };
    std::vector<VariantPayload> variants;
};

// ---------------------------------------------------------------------------
// MLIRGenImpl
// ---------------------------------------------------------------------------

class MLIRGenImpl {
public:
    explicit MLIRGenImpl(mlir::MLIRContext& ctx)
        : builder_(&ctx)
        , loc_(builder_.getUnknownLoc())
    {}

    mlir::OwningOpRef<mlir::ModuleOp> generate(const LProgram& prog) {
        auto mod = mlir::ModuleOp::create(loc_);

        // Pass 0: register struct LLVM types.
        for (auto& sd : prog.structs)
            if (!register_struct(sd)) return nullptr;

        // Pass 0a: register class LLVM types (with prepended vtable pointer).
        for (auto& cd : prog.classes)
            if (!register_class(mod, cd)) return nullptr;

        // Pass 0.5: register enum types and module constants.
        for (auto& ed : prog.enums) {
            enum_types_[ed.name] = &ed;
            if (ed.has_payload()) register_tagged_enum(ed);
        }

        for (auto& ta : prog.type_aliases)
            type_aliases_[ta.name] = logos_to_mlir(ta.type);

        for (auto& c : prog.consts)
            module_consts_[c.name] = &c;

        // Declare malloc and free for 'new' and 'delete'.
        ensure_malloc_free(mod);

        // Pass 1: forward-declare all functions (structs, classes, free fns).
        for (auto& sd : prog.structs)
            for (auto& m : sd.methods)
                forward_declare(mod, m);

        for (auto& cd : prog.classes)
            for (auto& m : cd.methods)
                forward_declare(mod, m);

        for (auto& fn : prog.functions)
            forward_declare(mod, fn);

        // Pass 1a: emit vtable globals for concrete classes.
        for (auto& cd : prog.classes)
            if (!cd.is_abstract)
                emit_vtable(mod, cd);

        // Pass 1b: emit vtable globals for trait impls (&dyn Trait support).
        emit_trait_vtables(mod, prog);

        // Pass 2: fill function bodies (structs, classes, free fns).
        for (auto& sd : prog.structs) {
            for (auto& m : sd.methods) {
                auto func = mod.lookupSymbol<mlir::func::FuncOp>(m.name);
                if (!gen_function_body(func, m)) return nullptr;
            }
        }
        for (auto& cd : prog.classes) {
            for (auto& m : cd.methods) {
                auto func = mod.lookupSymbol<mlir::func::FuncOp>(m.name);
                if (!gen_function_body(func, m)) return nullptr;
            }
        }
        for (auto& fn : prog.functions) {
            if (fn.is_extern) continue;
            auto func = mod.lookupSymbol<mlir::func::FuncOp>(fn.name);
            if (!gen_function_body(func, fn)) return nullptr;
        }

        if (mlir::failed(mlir::verify(mod))) {
            std::fprintf(stderr, "mlir_gen: module verification failed\n");
            mod.dump();
            return nullptr;
        }
        return mod;
    }

private:
    mlir::OpBuilder builder_;
    mlir::Location  loc_;

    std::unordered_map<std::string, StructInfo>        struct_types_;
    std::unordered_map<std::string, const LEnumDef*>   enum_types_;
    std::unordered_map<std::string, TaggedEnumInfo>    tagged_enums_;
    std::unordered_map<std::string, mlir::Type>        type_aliases_;
    std::unordered_map<std::string, const LConst*>     module_consts_;
    std::unordered_set<std::string>                    vararg_fns_;  // names of vararg extern fns

    // Per-function: variables holding &dyn Trait values (name → trait name).
    std::unordered_map<std::string, std::string>  var_dyn_trait_;
    // Function name → Logos-level parameter types (for dyn coercion at call sites).
    std::unordered_map<std::string, std::vector<const LogosType*>> fn_param_types_;

    // Per-function: tracks class name for variables/params holding class pointers.
    std::unordered_map<std::string, std::string>       var_class_;

    // Per-function state.
    std::unordered_map<std::string, mlir::Value>  scope_;
    std::unordered_set<std::string>               let_vars_;
    std::unordered_map<std::string, mlir::Type>   var_elem_types_;
    std::unordered_map<std::string, std::string>  var_struct_;
    std::unordered_map<std::string, mlir::Type>   var_subscript_;
    std::unordered_set<std::string>              var_tuple_;
    std::unordered_set<std::string>              var_tagged_enum_;
    // Mutable tagged-enum variables use a pointer slot (alloca-of-ptr) for rebinding.
    // scope_[name] = ptr_slot alloca; reading loads the ptr; assigning stores new ptr.
    std::unordered_set<std::string>              var_tagged_enum_ptr_;
    // Local let-bound pointer variables (*mut T / *const T): maps name → pointee MLIR type.
    // Needed because scope_[name] is an alloca(ptr), so indexing requires a load first.
    std::unordered_map<std::string, mlir::Type>   var_local_ptrs_;
    mlir::Type                                    cur_ret_type_;
    const LogosType*                              cur_fn_ret_logos_type_ = nullptr;
    bool                                          in_llvm_func_ = false;

    struct LoopBlocks { mlir::Block* cont; mlir::Block* exit; };
    std::vector<LoopBlocks> loop_stack_;

    int str_counter_ = 0;

    // ── MLIR helpers ─────────────────────────────────────────────

    static bool is_terminated(mlir::Block* block) noexcept {
        if (!block || block->empty()) return false;
        return block->back().hasTrait<mlir::OpTrait::IsTerminator>();
    }

    mlir::Value i32_zero() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    }
    mlir::Value i64_one() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    }
    mlir::LLVM::LLVMPointerType ptr_type() {
        return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
    }

    // Spill an aggregate value (struct/enum returned by value) to an alloca.
    // Used when passing such a value to a function that expects a pointer.
    mlir::Value spill_to_alloca(mlir::Value v) {
        auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(v.getType());
        if (!st) return v;
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), st, i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, v, alloca);
        return alloca;
    }

    mlir::Value coerce_int(mlir::Value v, mlir::Type to) {
        if (!v || !to || v.getType() == to) return v;
        auto fi = mlir::dyn_cast<mlir::IntegerType>(v.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!fi || !ti) return v;
        if (ti.getWidth() > fi.getWidth()) {
            // i1 (bool) must be zero-extended; other integers sign-extended.
            if (fi.getWidth() == 1)
                return builder_.create<mlir::arith::ExtUIOp>(loc_, to, v);
            return builder_.create<mlir::arith::ExtSIOp>(loc_, to, v);
        }
        if (ti.getWidth() < fi.getWidth())
            return builder_.create<mlir::arith::TruncIOp>(loc_, to, v);
        return v;
    }

    // ── Type conversion: LogosType → mlir::Type ──────────────────

    mlir::Type logos_to_mlir(const LogosType* t) {
        if (!t) return nullptr;
        switch (t->kind) {
        case LogosType::Kind::Void:   return nullptr;
        case LogosType::Kind::I32:    return builder_.getI32Type();
        case LogosType::Kind::I64:    return builder_.getI64Type();
        case LogosType::Kind::F64:    return builder_.getF64Type();
        case LogosType::Kind::Bool:   return builder_.getI1Type();
        case LogosType::Kind::U8:     return builder_.getIntegerType(8);
        case LogosType::Kind::I8:     return builder_.getIntegerType(8);
        case LogosType::Kind::U32:    return builder_.getIntegerType(32);
        case LogosType::Kind::U64:    return builder_.getIntegerType(64);
        case LogosType::Kind::IntLit: return builder_.getI32Type();
        case LogosType::Kind::Enum: {
            // Tagged enums are passed by pointer; C-style enums are i32.
            if (resolve_tagged_enum(t->enum_name, t)) return ptr_type();
            return builder_.getI32Type();
        }
        case LogosType::Kind::Ptr:    return ptr_type();
        case LogosType::Kind::Array: {
            auto elem = logos_to_mlir(t->elem);
            if (!elem) return nullptr;
            return mlir::LLVM::LLVMArrayType::get(elem, t->arr_size);
        }
        case LogosType::Kind::Struct: {
            // Check type alias first.
            auto cname = concrete_struct_name(t);
            auto ait = type_aliases_.find(cname);
            if (ait != type_aliases_.end()) return ait->second;
            // Structs are always passed by pointer; no need to wait for registration.
            return ptr_type();
        }
        case LogosType::Kind::Class:
            // Classes are always passed by pointer (heap allocated via 'new').
            return ptr_type();
        case LogosType::Kind::Closure:
            // Closures are {fn_ptr, env_ptr}, passed by pointer.
            return ptr_type();
        case LogosType::Kind::Slice:
            // Slices are fat pointers {ptr, i64}, passed by pointer (like structs/tuples).
            return ptr_type();
        case LogosType::Kind::Tuple: {
            // Tuples are anonymous LLVM struct types, passed by pointer (like structs).
            llvm::SmallVector<mlir::Type> fields;
            for (auto* e : t->tuple_elems) {
                auto ft = logos_to_mlir(e);
                if (!ft) return nullptr;
                fields.push_back(ft);
            }
            return ptr_type();
        }
        case LogosType::Kind::TraitObject:
            // &dyn Trait is a fat pointer {data_ptr, vtable_ptr}, passed by pointer.
            return ptr_type();
        case LogosType::Kind::TypeVar:
            // TypeVar should have been eliminated by mono_pass.
            // Treat as error type to produce a clear diagnostic.
            std::fprintf(stderr, "mlir_gen: unresolved TypeVar '%s' — mono_pass required\n",
                         t->type_var_name.c_str());
            return nullptr;
        case LogosType::Kind::AssocType:
            // AssocType (T::Item) should have been resolved by mono_pass.
            std::fprintf(stderr, "mlir_gen: unresolved AssocType '%s::%s' — mono_pass required\n",
                         t->type_var_name.c_str(), t->assoc_type_name.c_str());
            return nullptr;
        case LogosType::Kind::Error:     return nullptr;
        case LogosType::Kind::ImplTrait: return nullptr;
        }
        return nullptr;
    }

    // ── Struct registration (Pass 0) ─────────────────────────────

    bool register_struct(const LStructDef& sd) {
        if (struct_types_.count(sd.name)) return true;
        auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
            builder_.getContext(), sd.name);
        StructInfo info;
        info.name      = sd.name;
        info.llvm_type = struct_type;

        std::vector<mlir::Type> field_types;
        for (auto& f : sd.fields) {
            auto ft = logos_to_mlir(f.type);
            if (!ft) {
                std::fprintf(stderr, "mlir_gen: unknown field type in '%s'\n", sd.name.c_str());
                return false;
            }
            std::string fsname;
            if (f.type->kind == LogosType::Kind::Struct) fsname = concrete_struct_name(f.type);
            info.fields.push_back({f.name, ft, uint32_t(info.fields.size()), fsname});
            field_types.push_back(ft);
        }
        if (mlir::failed(struct_type.setBody(field_types, false))) {
            std::fprintf(stderr, "mlir_gen: failed to set struct body for '%s'\n", sd.name.c_str());
            return false;
        }
        struct_types_[sd.name] = std::move(info);
        return true;
    }

    // ── Tagged enum registration ────────────────────────────────
    // Layout: { i32 disc, [max_payload_bytes x i8] }
    void register_tagged_enum(const LEnumDef& ed) {
        if (tagged_enums_.count(ed.name)) return;
        TaggedEnumInfo info;
        info.name = ed.name;
        uint64_t max_bytes = 0;
        for (auto& v : ed.variants) {
            TaggedEnumInfo::VariantPayload vp;
            vp.disc = v.disc;
            uint64_t variant_bytes = 0;
            for (auto* pt : v.payload_types) {
                auto ft = logos_to_mlir(pt);
                if (!ft) ft = builder_.getI32Type();
                vp.field_types.push_back(ft);
                // Estimate size: i32=4, i64=8, ptr=8, bool=1, etc.
                if (ft.isInteger(1)) variant_bytes += 1;
                else if (ft.isInteger(8)) variant_bytes += 1;
                else if (ft.isInteger(32)) variant_bytes += 4;
                else if (ft.isInteger(64)) variant_bytes += 8;
                else if (ft == ptr_type()) variant_bytes += 8;
                else variant_bytes += 8; // default
            }
            if (variant_bytes > max_bytes) max_bytes = variant_bytes;
            info.variants.push_back(std::move(vp));
        }
        info.payload_bytes = max_bytes;
        auto i32 = builder_.getI32Type();
        auto payload = mlir::LLVM::LLVMArrayType::get(
            builder_.getIntegerType(8), max_bytes > 0 ? max_bytes : 1);
        auto enum_type = mlir::LLVM::LLVMStructType::getIdentified(
            builder_.getContext(), "enum." + ed.name);
        (void)enum_type.setBody({i32, payload}, false);
        info.llvm_type = enum_type;
        tagged_enums_[ed.name] = std::move(info);
    }

    // ── Class registration (Pass 0a) ─────────────────────────────
    // Registers a class as an LLVM struct type with:
    //   field 0..N: user fields (all_fields = parent fields + own fields)
    // NOTE: Vtable pointer omitted for Batch H — all calls are direct.
    // Stores in struct_types_ so existing gep/field helpers work.

    bool register_class(mlir::ModuleOp /*mod*/, const LClassDef& cd) {
        if (struct_types_.count(cd.name)) return true;

        auto struct_type = mlir::LLVM::LLVMStructType::getIdentified(
            builder_.getContext(), cd.name);
        StructInfo info;
        info.name      = cd.name;
        info.llvm_type = struct_type;

        std::vector<mlir::Type> field_types;

        // Parent fields first (from parent's StructInfo)
        if (!cd.parent_name.empty()) {
            auto pit = struct_types_.find(cd.parent_name);
            if (pit != struct_types_.end()) {
                for (auto& pf : pit->second.fields) {
                    uint32_t idx = uint32_t(info.fields.size());
                    info.fields.push_back({pf.name, pf.type, idx, pf.struct_name});
                    field_types.push_back(pf.type);
                }
            }
        }

        // Own fields
        for (auto& f : cd.own_fields) {
            auto ft = logos_to_mlir(f.type);
            if (!ft) {
                std::fprintf(stderr, "mlir_gen: unknown field type in class '%s'\n",
                             cd.name.c_str());
                return false;
            }
            std::string fsname;
            if (f.type && f.type->kind == LogosType::Kind::Struct)
                fsname = concrete_struct_name(f.type);
            else if (f.type && f.type->kind == LogosType::Kind::Class)
                fsname = f.type->struct_name;
            uint32_t idx = uint32_t(info.fields.size());
            info.fields.push_back({f.name, ft, idx, fsname});
            field_types.push_back(ft);
        }

        if (!field_types.empty()) {
            if (mlir::failed(struct_type.setBody(field_types, false))) {
                std::fprintf(stderr, "mlir_gen: failed to set body for class '%s'\n",
                             cd.name.c_str());
                return false;
            }
        }
        struct_types_[cd.name] = std::move(info);
        return true;
    }

    // ── Vtable emission (Pass 1a) ─────────────────────────────────
    // Batch H: direct calls only — vtable dispatch deferred to Batch I.
    void emit_vtable(mlir::ModuleOp /*mod*/, const LClassDef& /*cd*/) {}

    // ── Trait vtable info (Pass 1b) ─────────────────────────────
    // Index impl method names per (Trait, Type) for inline vtable construction.
    // No globals emitted — vtables are built on the stack at coercion sites.
    void emit_trait_vtables(mlir::ModuleOp /*mod*/, const LProgram& prog) {
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
    // "Trait::Type" → mangled method names in vtable slot order
    std::unordered_map<std::string, std::vector<std::string>> dyn_vtable_methods_;

    // Build a vtable [N x ptr] heap-allocated for a concrete type implementing a trait.
    // Vtable is heap-allocated so it outlives any creating function's stack frame —
    // this is required for Box<dyn Trait> returned from functions.
    mlir::Value build_inline_vtable(const std::string& trait_name,
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

    // ── malloc / free helpers ─────────────────────────────────────

    void ensure_malloc_free(mlir::ModuleOp mod) {
        if (!mod.lookupSymbol("malloc")) {
            auto fn_type = builder_.getFunctionType(
                {builder_.getI64Type()}, {ptr_type()});
            auto fn = mlir::func::FuncOp::create(loc_, "malloc", fn_type);
            fn.setPrivate();
            mod.push_back(fn);
        }
        if (!mod.lookupSymbol("free")) {
            auto fn_type = builder_.getFunctionType({ptr_type()}, {});
            auto fn = mlir::func::FuncOp::create(loc_, "free", fn_type);
            fn.setPrivate();
            mod.push_back(fn);
        }
    }

    mlir::Value call_malloc(mlir::Value size) {
        auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto malloc_fn = mod.lookupSymbol<mlir::func::FuncOp>("malloc");
        if (!malloc_fn) return nullptr;
        auto call = builder_.create<mlir::func::CallOp>(
            loc_, malloc_fn, mlir::ValueRange{size});
        return call.getResult(0);
    }

    void call_free(mlir::Value ptr) {
        auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto free_fn = mod.lookupSymbol<mlir::func::FuncOp>("free");
        if (!free_fn) return;
        builder_.create<mlir::func::CallOp>(loc_, free_fn, mlir::ValueRange{ptr});
    }

    // Compute sizeof an LLVM struct type via GEP null trick.
    mlir::Value sizeof_struct(mlir::LLVM::LLVMStructType struct_type) {
        mlir::Value zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        mlir::Value null   = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero64);
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(1)};
        mlir::Value gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), struct_type, null, idx);
        return builder_.create<mlir::LLVM::PtrToIntOp>(
            loc_, builder_.getI64Type(), gep);
    }

    // ── Function type from LFunction ─────────────────────────────

    mlir::FunctionType make_fn_type(const LFunction& fn) {
        llvm::SmallVector<mlir::Type> param_types;
        for (auto& p : fn.params) {
            // Arrays (like structs) are passed by pointer.
            if (p.type && p.type->kind == LogosType::Kind::Array)
                param_types.push_back(ptr_type());
            else {
                auto t = logos_to_mlir(p.type);
                if (t) param_types.push_back(t);
            }
        }
        llvm::SmallVector<mlir::Type> ret_types;
        if (fn.ret_type) {
            // Tuples and structs are returned by value (as LLVM struct), not by pointer.
            // Returning a pointer to a local alloca would be a dangling pointer after return.
            if (fn.ret_type->kind == LogosType::Kind::Tuple) {
                auto rt = tuple_llvm_type(fn.ret_type);
                if (rt) ret_types.push_back(rt);
            } else if (fn.ret_type->kind == LogosType::Kind::Struct) {
                auto cname = concrete_struct_name(fn.ret_type);
                auto sit = struct_types_.find(cname);
                if (sit != struct_types_.end())
                    ret_types.push_back(sit->second.llvm_type);
                else
                    ret_types.push_back(ptr_type()); // fallback (struct not yet registered)
            } else if (fn.ret_type->kind == LogosType::Kind::Enum) {
                // Tagged enums must also be returned by value (aggregate), not by pointer.
                auto* te = resolve_tagged_enum(fn.ret_type->enum_name, fn.ret_type);
                if (te)
                    ret_types.push_back(te->llvm_type);
                else {
                    // C-style (non-payload) enum — return i32.
                    ret_types.push_back(builder_.getI32Type());
                }
            } else {
                auto rt = logos_to_mlir(fn.ret_type);
                if (rt) ret_types.push_back(rt);
            }
        }
        return builder_.getFunctionType(param_types, ret_types);
    }

    void forward_declare(mlir::ModuleOp mod, const LFunction& fn) {
        if (fn.is_vararg) {
            // Vararg extern fns use llvm.func (func dialect doesn't support varargs)
            if (mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(fn.name)) return;
            llvm::SmallVector<mlir::Type> param_types;
            for (auto& p : fn.params) {
                auto t = logos_to_mlir(p.type);
                if (t) param_types.push_back(t);
            }
            mlir::Type ret = fn.ret_type ? logos_to_mlir(fn.ret_type) : nullptr;
            if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
            auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types,
                                                                    /*isVarArg=*/true);
            auto llvm_fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, fn.name, llvm_fn_type);
            llvm_fn.setLinkage(mlir::LLVM::Linkage::External);
            mod.push_back(llvm_fn);
            vararg_fns_.insert(fn.name);
            return;
        }
        if (mod.lookupSymbol<mlir::func::FuncOp>(fn.name)) return;
        auto f = mlir::func::FuncOp::create(loc_, fn.name, make_fn_type(fn));
        if (fn.is_extern) f.setPrivate();
        mod.push_back(f);
        // Record Logos-level param types for dyn coercion at call sites.
        std::vector<const LogosType*> ptypes;
        for (auto& p : fn.params) ptypes.push_back(p.type);
        fn_param_types_[fn.name] = std::move(ptypes);
    }

    // ── Function body ─────────────────────────────────────────────

    bool gen_function_body(mlir::func::FuncOp func, const LFunction& fn) {
        auto* entry = func.addEntryBlock();
        builder_.setInsertionPointToStart(entry);

        scope_.clear();
        let_vars_.clear();
        var_elem_types_.clear();
        var_struct_.clear();
        var_class_.clear();
        var_subscript_.clear();
        var_tuple_.clear();
        var_tagged_enum_.clear();
        var_tagged_enum_ptr_.clear();
        var_local_ptrs_.clear();
        var_dyn_trait_.clear();
        loop_stack_.clear();

        // Bind parameters.
        for (size_t i = 0; i < fn.params.size(); ++i) {
            auto& p = fn.params[i];
            scope_[p.name] = entry->getArgument(i);

            // Track subscript element type for pointer parameters.
            if (p.type && p.type->kind == LogosType::Kind::Ptr && p.type->pointee) {
                auto et = logos_to_mlir(p.type->pointee);
                if (et) var_subscript_[p.name] = et;
            }

            // Track struct / class type for parameters (including 'self').
            if (p.type) {
                std::string sname;
                if (p.type->kind == LogosType::Kind::Struct)
                    sname = concrete_struct_name(p.type);
                else if (p.type->kind == LogosType::Kind::Ptr && p.type->pointee &&
                         p.type->pointee->kind == LogosType::Kind::Struct)
                    sname = concrete_struct_name(p.type->pointee);
                if (!sname.empty()) { var_struct_[p.name] = std::move(sname); continue; }

                std::string cname;
                if (p.type->kind == LogosType::Kind::Class)
                    cname = concrete_class_name(p.type);
                else if (p.type->kind == LogosType::Kind::Ptr && p.type->pointee &&
                         p.type->pointee->kind == LogosType::Kind::Class)
                    cname = concrete_class_name(p.type->pointee);
                if (!cname.empty()) var_class_[p.name] = std::move(cname);
            }
        }

        auto ret_types = func.getFunctionType().getResults();
        cur_ret_type_ = ret_types.empty() ? mlir::Type{} : ret_types[0];
        cur_fn_ret_logos_type_ = fn.ret_type;

        gen_block(fn.body);

        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::func::ReturnOp>(loc_);

        return true;
    }

    // ── Block ─────────────────────────────────────────────────────

    void gen_block(const LBlock& block) {
        for (auto& s : block.stmts) {
            if (is_terminated(builder_.getBlock())) break;
            gen_stmt(s);
        }
    }

    // ── Statements ────────────────────────────────────────────────

    void gen_stmt(const LStmt& stmt) {
        std::visit([&](auto& s) { gen_stmt_kind(s); }, stmt.kind);
    }

    void gen_stmt_kind(const SLet& s)        { gen_let(s); }
    void gen_stmt_kind(const SAssign& s)      { gen_assign(s); }
    void gen_stmt_kind(const SReturn& s)      { gen_return(s); }
    void gen_stmt_kind(const SIf& s)          { gen_if(s); }
    void gen_stmt_kind(const SWhile& s)       { gen_while(s); }
    void gen_stmt_kind(const SFor& s)         { gen_for(s); }
    void gen_stmt_kind(const SLoop& s)        { gen_loop(s); }
    void gen_stmt_kind(const SBreak&)         { gen_break(); }
    void gen_stmt_kind(const SContinue&)      { gen_continue(); }
    void gen_stmt_kind(const SFieldWrite& s)       { gen_field_write(s); }
    void gen_stmt_kind(const SDerefFieldWrite& s)  { gen_deref_field_write(s); }
    void gen_stmt_kind(const SIndexWrite& s)       { gen_index_write(s); }
    void gen_stmt_kind(const SFieldIndexWrite& s)  { gen_field_index_write(s); }
    void gen_stmt_kind(const SExprStmt& s)    { gen_expr(*s.expr); }
    void gen_stmt_kind(const SMatch& s)       { gen_match(s); }
    void gen_stmt_kind(const SDelete& s)      { gen_delete(s); }
    void gen_stmt_kind(const SForEach& s)     { gen_for_each(s); }
    void gen_stmt_kind(const SBlock& s)       { gen_block(*s.body); }
    void gen_stmt_kind(const SDrop& s) {
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
    void gen_stmt_kind(const SDerefWrite& s) {
        auto ptr = gen_expr(*s.ptr);
        auto val = gen_expr(*s.value);
        if (!ptr || !val) return;
        // Determine element type from pointer's pointee type
        mlir::Type elem_type = nullptr;
        if (s.ptr->type && s.ptr->type->kind == LogosType::Kind::Ptr && s.ptr->type->pointee)
            elem_type = logos_to_mlir(s.ptr->type->pointee);
        if (!elem_type) elem_type = builder_.getI32Type();
        val = coerce_int(val, elem_type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, ptr);
    }

    void gen_let(const SLet& s) {
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

        // ── Class pointer (from 'new') ────────────────────────────
        // 'new ClassName { ... }' returns *mut ClassName.  Store the
        // heap pointer directly — no alloca wrapper needed (immutable).
        // Mutable class pointer (let mut p: *mut C): needs alloca so the
        // pointer itself can be reassigned.  Field access via (*p).f uses
        // gen_recv_struct's general EDeref path, so var_class_ is not set.
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

    void gen_assign(const SAssign& s) {
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

    void gen_return(const SReturn& s) {
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

    void gen_if(const SIf& s) {
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

    void gen_while(const SWhile& s) {
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

    void gen_for(const SFor& s) {
        auto lo = gen_expr(*s.lo);
        auto hi = gen_expr(*s.hi);
        if (!lo || !hi) return;

        auto i_alloca = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type(), builder_.getI32Type(), i64_one());
        builder_.create<mlir::LLVM::StoreOp>(loc_, lo, i_alloca);
        scope_[s.var] = i_alloca;
        let_vars_.insert(s.var);
        var_elem_types_[s.var] = builder_.getI32Type();

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
        auto i_val = builder_.create<mlir::LLVM::LoadOp>(
                         loc_, builder_.getI32Type(), i_alloca);
        auto hi_i32 = coerce_int(hi, builder_.getI32Type());
        mlir::Value cond;
        if (s.inclusive)
            cond = builder_.create<mlir::arith::CmpIOp>(
                       loc_, mlir::arith::CmpIPredicate::sle, i_val, hi_i32);
        else
            cond = builder_.create<mlir::arith::CmpIOp>(
                       loc_, mlir::arith::CmpIPredicate::slt, i_val, hi_i32);
        builder_.create<mlir::cf::CondBranchOp>(loc_, cond, body_block, exit_block);

        builder_.setInsertionPointToStart(body_block);
        // continue → incr_block (so that i is incremented before re-checking)
        loop_stack_.push_back({incr_block, exit_block});
        gen_block(*s.body);
        loop_stack_.pop_back();
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::cf::BranchOp>(loc_, incr_block);

        // Increment block: i += 1, branch back to condition.
        builder_.setInsertionPointToStart(incr_block);
        {
            auto i_cur = builder_.create<mlir::LLVM::LoadOp>(
                             loc_, builder_.getI32Type(), i_alloca);
            auto one = builder_.create<mlir::arith::ConstantOp>(
                           loc_, builder_.getI32Type(), builder_.getI32IntegerAttr(1));
            auto i_next = builder_.create<mlir::arith::AddIOp>(loc_, i_cur, one);
            builder_.create<mlir::LLVM::StoreOp>(loc_, i_next, i_alloca);
            builder_.create<mlir::cf::BranchOp>(loc_, cond_block);
        }

        builder_.setInsertionPointToStart(exit_block);
        scope_.erase(s.var);
        let_vars_.erase(s.var);
        var_elem_types_.erase(s.var);
    }

    void gen_loop(const SLoop& s) {
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

    void gen_break() {
        if (loop_stack_.empty()) return;
        builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().exit);
    }

    // ── for item in array ─────────────────────────────────────────
    // Allocate the array on the stack, fill with the iter value, then
    // loop over indices 0..arr_size using the existing gen_for pattern.
    void gen_for_each(const SForEach& s) {
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
            auto i_cur32 = builder_.create<mlir::arith::TruncIOp>(
                loc_, builder_.getI32Type(), i_cur);
            llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx;
            arr_idx.push_back(mlir::LLVM::GEPArg(i_cur32));
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
            {
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
        builder_.setInsertionPointToStart(incr_block);
        {
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

    void gen_continue() {
        if (loop_stack_.empty()) return;
        builder_.create<mlir::cf::BranchOp>(loc_, loop_stack_.back().cont);
    }

    void gen_field_write(const SFieldWrite& s) {
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

    void gen_deref_field_write(const SDerefFieldWrite& s) {
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

    void gen_index_write(const SIndexWrite& s) {
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

    void gen_field_index_write(const SFieldIndexWrite& s) {
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
        // For pointer fields (*mut T): load the pointer, then GEP to element.
        // For array fields ([T; N]):   field_gep IS the array base; GEP to element.
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
            // GEP with {0, idx} to reach element idx.
            llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx;
            arr_idx.push_back(mlir::LLVM::GEPArg(int32_t(0)));
            auto idx_i32 = coerce_int(idx, builder_.getIntegerType(32));
            arr_idx.push_back(mlir::LLVM::GEPArg(idx_i32));
            base_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), field_mlir_type, field_gep, arr_idx);
        } else {
            // Pointer field: load the stored pointer, then GEP to element.
            auto field_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_gep);
            llvm::SmallVector<mlir::LLVM::GEPArg> ptr_idx{idx};
            base_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), val_type, field_ptr, ptr_idx);
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, base_ptr);
    }

    void gen_match(const SMatch& s) {
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
        auto scrut_i32 = coerce_int(scrut, builder_.getI32Type());

        // Determine if any arm is a wildcard.
        bool has_wild = false;
        for (auto& arm : s.arms)
            if (std::holds_alternative<PatWild>(arm.pat)) { has_wild = true; break; }

        // When there is no wildcard arm, treat the LAST arm (in source order,
        // i.e., index arms.size()-1) as a catch-all to handle exhaustive matches
        // (e.g. all enum variants covered). We pre-generate its body block and
        // use it as the initial else_block so no edge goes to merge_block.
        int last_tested = (int)s.arms.size() - 1; // last arm index that gets a test block
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
            last_tested = (int)s.arms.size() - 2; // skip last arm in the main loop
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

            // arm_entry: block the discriminant test (or wildcard path) jumps to.
            // With a guard: arm_entry = guard_block (bindings extracted there).
            // Without a guard: arm_entry = body_block (bindings extracted there).
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
                int32_t disc = 0;
                if (auto* pv = std::get_if<PatVariant>(&arm.pat)) disc = pv->disc;
                else if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) disc = pvd->disc;
                else if (auto* pi = std::get_if<PatInt>(&arm.pat))  disc = pi->value;
                else if (auto* pb = std::get_if<PatBool>(&arm.pat)) disc = pb->value ? 1 : 0;

                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                {
                    mlir::OpBuilder::InsertionGuard ig(builder_);
                    builder_.setInsertionPointToStart(test_block);
                    auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut_i32, disc_val);
                    builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
                }
                else_block = test_block;
            }
        }

        builder_.create<mlir::cf::BranchOp>(loc_, else_block);
        region->push_back(merge_block);
        // If all arms terminated (return/break/continue), merge_block has no
        // predecessors and is unreachable. Erase it so the builder stays at
        // the last block (which is already terminated), preventing a spurious
        // bare func.return() from being inserted.
        if (merge_block->hasNoPredecessors()) {
            merge_block->erase();
            return;
        }
        builder_.setInsertionPointToStart(merge_block);
    }

    // ── Expressions ───────────────────────────────────────────────

    mlir::Value gen_expr(const LExpr& e) {
        return std::visit([&](auto& k) { return gen_expr_kind(k, e.type); }, e.kind);
    }

    mlir::Value gen_expr_kind(const ELitInt& e, const LogosType*) {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, e.value, 32);
    }
    mlir::Value gen_expr_kind(const ELitBool& e, const LogosType*) {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, e.value ? 1 : 0, 1);
    }
    mlir::Value gen_expr_kind(const ELitStr& e, const LogosType*) {
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

    mlir::Value gen_expr_kind(const EVarRef& e, const LogosType*) {
        // Module constant: re-evaluate inline.
        auto cit = module_consts_.find(e.name);
        if (cit != module_consts_.end())
            return gen_expr(*cit->second->value);

        auto it = scope_.find(e.name);
        if (it == scope_.end()) {
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

    // Resolve a tagged enum name from the expression type (handles generic enums).
    const TaggedEnumInfo* resolve_tagged_enum(const std::string& name, const LogosType* type) {
        auto tit = tagged_enums_.find(name);
        if (tit != tagged_enums_.end()) return &tit->second;
        // For generic enums: compute concrete name from type_args
        if (type && type->kind == LogosType::Kind::Enum && !type->type_args.empty()) {
            std::string cname = type->enum_name;
            for (auto* a : type->type_args) { cname += "__"; cname += type_str(a); }
            tit = tagged_enums_.find(cname);
            if (tit != tagged_enums_.end()) return &tit->second;
        }
        return nullptr;
    }

    mlir::Value gen_expr_kind(const EEnumLit& e, const LogosType* type) {
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

    mlir::Value gen_expr_kind(const EEnumLitData& e, const LogosType* type) {
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
                // Store each field via GEP into the payload struct
                // Note: pay_ptr points to the [N x i8] payload area; using the same pattern
                // as extraction (match arms), where this works correctly.
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

    mlir::Value gen_expr_kind(const EBinOp& e, const LogosType*) {
        auto lhs = gen_expr(*e.lhs);
        auto rhs = gen_expr(*e.rhs);
        if (!lhs || !rhs) return nullptr;
        // Widen narrower integer operand.
        if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
            if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
                if (li.getWidth() < ri.getWidth())
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
                else if (ri.getWidth() < li.getWidth())
                    rhs = builder_.create<mlir::arith::ExtSIOp>(loc_, lhs.getType(), rhs);
            }
        }
        auto& op = e.op;
        if (op == "+")  return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);
        if (op == "%")  return builder_.create<mlir::arith::RemSIOp>(loc_, lhs, rhs);
        if (op == "&&") return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
        if (op == "||") return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
        if (op == "&")  return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
        if (op == "|")  return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
        if (op == "^")  return builder_.create<mlir::arith::XOrIOp>(loc_, lhs, rhs);
        if (op == "<<") return builder_.create<mlir::arith::ShLIOp>(loc_, lhs, rhs);
        if (op == ">>") return builder_.create<mlir::arith::ShRSIOp>(loc_, lhs, rhs);
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
        if (op == "<")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::slt, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sgt, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sle, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::sge, lhs, rhs);
        std::fprintf(stderr, "mlir_gen: unknown op '%s'\n", op.c_str());
        return nullptr;
    }

    mlir::Value gen_expr_kind(const EUnary& e, const LogosType*) {
        auto val = gen_expr(*e.operand);
        if (!val) return nullptr;
        if (e.op == "-") {
            auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            return builder_.create<mlir::arith::SubIOp>(loc_, zero, val);
        }
        if (e.op == "!") {
            auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, one);
        }
        std::fprintf(stderr, "mlir_gen: unknown unary op '%s'\n", e.op.c_str());
        return nullptr;
    }

    mlir::Value gen_expr_kind(const EAddrOf& e, const LogosType*) {
        // Address-of: return the alloca pointer directly.
        auto it = scope_.find(e.var_name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: & undefined '%s'\n", e.var_name.c_str());
            return nullptr;
        }
        return it->second;
    }

    mlir::Value gen_expr_kind(const EDeref& e, const LogosType* type) {
        auto ptr = gen_expr(*e.operand);
        if (!ptr) return nullptr;
        // Class objects are always pointer-represented in MLIR/LLVM.
        // Dereferencing *mut ClassName just yields the same pointer — no load needed.
        if (type && type->kind == LogosType::Kind::Class)
            return ptr;
        auto pointee = logos_to_mlir(type);
        if (!pointee) pointee = builder_.getI32Type();
        return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
    }

    mlir::Value gen_expr_kind(const ECall& e, const LogosType* ret_logos_type) {
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
                if (i < fixed_inputs.size()) v = coerce_int(v, fixed_inputs[i]);
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
                    v = coerce_int(v, param_types[i]);
            }
            args.push_back(v);
        }
        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    // Build a &dyn Trait fat pointer from a concrete data_ptr.
    // Returns a pointer to a stack-allocated {data_ptr, vtable_ptr}.
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, const std::string& trait_name,
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

    // Indirect call through &dyn Trait vtable.
    mlir::Value gen_dyn_dispatch(const EMethodCall& e) {
        // The receiver is a &dyn Trait — a pointer to {data_ptr, vtable_ptr}.
        // We need to: load data_ptr, load vtable_ptr, GEP to slot, load fn_ptr, call.

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

        // Use i32 return type as default.
        // TODO: look up actual return type from trait method signature.
        auto ret_type = builder_.getI32Type();
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

    mlir::Value gen_expr_kind(const EMethodCall& e, const LogosType*) {
        // &dyn Trait dispatch: load vtable, GEP slot, indirect call
        if (e.receiver->type &&
            e.receiver->type->kind == LogosType::Kind::TraitObject &&
            e.vtable_index >= 0) {
            return gen_dyn_dispatch(e);
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
                    v = coerce_int(v, param_types[pi]);
            }
            args.push_back(v);
        }
        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    mlir::Value gen_expr_kind(const EFieldRead& e, const LogosType* type) {
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

    mlir::Value gen_expr_kind(const EIndexRead& e, const LogosType* type) {
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
        } else if (auto* fr = std::get_if<EFieldRead>(&e.receiver->kind)) {
            // Field index read: field may be an array or a pointer.
            // - Array field: GEP to the field → use as base (no load needed)
            // - Pointer field: GEP to the field → LOAD → use as base
            auto [struct_ptr, sname] = gen_recv_struct(*fr->receiver);
            if (struct_ptr && !sname.empty()) {
                auto& info = struct_types_[sname];
                auto field_ptr = gep_field(struct_ptr, info, fr->field);
                if (field_ptr) {
                    elem_type = logos_to_mlir(type);
                    if (!elem_type) elem_type = builder_.getI32Type();
                    // Determine if field is a pointer type (load needed) or array type.
                    // e.receiver is the field-read expression whose type is the field type.
                    bool field_is_ptr = e.receiver->type &&
                                        e.receiver->type->kind == LogosType::Kind::Ptr;
                    if (field_is_ptr) {
                        // Load the pointer stored in the field, then index into it.
                        arr_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_ptr);
                    } else {
                        // Array field: GEP to the field, use directly as base.
                        arr_ptr = field_ptr;
                    }
                }
            }
            if (!arr_ptr) {
                // Fallback: evaluate as expression (may fail for array-valued fields).
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
        llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, arr_ptr, indices);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, gep);
    }

    mlir::Value gen_expr_kind(const EStructLit& e, const LogosType*) {
        return gen_struct_lit(e);
    }

    mlir::Value gen_expr_kind(const EArrLit& e, const LogosType* type) {
        mlir::Type elem_type = builder_.getI32Type();
        if (type && type->elem) {
            auto et = logos_to_mlir(type->elem);
            if (et) elem_type = et;
        }
        return gen_arr_lit(e, elem_type);
    }

    mlir::Value gen_expr_kind(const ECast& e, const LogosType* type) {
        auto val    = gen_expr(*e.operand);
        if (!val) return nullptr;
        auto target = logos_to_mlir(type);
        if (!target || val.getType() == target) return val;

        auto fi = mlir::dyn_cast<mlir::IntegerType>(val.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(target);
        if (fi && ti) {
            if (ti.getWidth() > fi.getWidth()) {
                // bool (i1) → int: use zero-extend to preserve 0/1 semantics.
                // Other ints: sign-extend.
                if (fi.getWidth() == 1)
                    return builder_.create<mlir::arith::ExtUIOp>(loc_, target, val);
                return builder_.create<mlir::arith::ExtSIOp>(loc_, target, val);
            }
            if (ti.getWidth() < fi.getWidth())
                return builder_.create<mlir::arith::TruncIOp>(loc_, target, val);
            return val;
        }
        if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) &&
            mlir::dyn_cast<mlir::FloatType>(target))
            return builder_.create<mlir::arith::SIToFPOp>(loc_, target, val);
        if (mlir::dyn_cast<mlir::FloatType>(val.getType()) &&
            mlir::dyn_cast<mlir::IntegerType>(target))
            return builder_.create<mlir::arith::FPToSIOp>(loc_, target, val);

        // int → ptr
        if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) && target == ptr_type()) {
            auto v64 = coerce_int(val, builder_.getI64Type());
            return builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), v64);
        }
        // ptr → int
        if (val.getType() == ptr_type() && mlir::dyn_cast<mlir::IntegerType>(target))
            return builder_.create<mlir::LLVM::PtrToIntOp>(loc_, target, val);

        std::fprintf(stderr, "mlir_gen: unsupported cast\n");
        return nullptr;
    }

    // ── Class new / delete ────────────────────────────────────────

    mlir::Value gen_expr_kind(const ENew& e, const LogosType*) {
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

    void gen_delete(const SDelete& s) {
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

    // ── if-as-expression ─────────────────────────────────────────
    // Generates a conditional branch with a merge block that takes a
    // block argument (the result value).
    mlir::Value gen_expr_kind(const EIfExpr& e, const LogosType* type) {
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
        then_val = coerce_int(then_val, result_type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, then_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        builder_.setInsertionPointToStart(else_block);
        auto else_val = gen_expr(*e.else_val);
        if (!else_val) else_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        else_val = coerce_int(else_val, result_type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, else_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
    }

    // ── Match expression (value-producing match) ─────────────────
    mlir::Value gen_expr_kind(const EMatchExpr& e, const LogosType* type) {
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
        auto scrut_i32 = coerce_int(scrut, builder_.getI32Type());

        // Extract payload bindings for a PatVariantData arm into scope.
        // Returns the set of binding names added (for cleanup).
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

        // Determine if any arm is a wildcard.
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
                    val = coerce_int(val, result_type);
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

            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                auto added = extract_arm_payload(arm);
                auto val = gen_expr(*arm.value);
                for (auto& n : added) { scope_.erase(n); let_vars_.erase(n); var_elem_types_.erase(n); }
                if (val) {
                    val = coerce_int(val, result_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                }
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }

            bool is_wild = std::holds_alternative<PatWild>(arm.pat);
            if (is_wild) {
                else_block = arm_entry;
            } else {
                int32_t disc = 0;
                if (auto* pv = std::get_if<PatVariant>(&arm.pat)) disc = pv->disc;
                else if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) disc = pvd->disc;
                else if (auto* pi = std::get_if<PatInt>(&arm.pat))  disc = pi->value;
                else if (auto* pb = std::get_if<PatBool>(&arm.pat)) disc = pb->value ? 1 : 0;

                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                {
                    mlir::OpBuilder::InsertionGuard ig(builder_);
                    builder_.setInsertionPointToStart(test_block);
                    auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut_i32, disc_val);
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

    // ── Tuple helpers ────────────────────────────────────────────

    // Build the anonymous LLVM struct type for a tuple.
    mlir::Type tuple_llvm_type(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Tuple) return nullptr;
        llvm::SmallVector<mlir::Type> fields;
        for (auto* e : t->tuple_elems) {
            auto ft = logos_to_mlir(e);
            if (!ft) return nullptr;
            fields.push_back(ft);
        }
        return mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), fields);
    }

    mlir::Value gen_expr_kind(const ETupleLit& e, const LogosType* type) {
        auto stype = tuple_llvm_type(type);
        if (!stype) return nullptr;
        // Allocate tuple on stack, store each element via GEP.
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(loc_, ptr_type(), stype, i64_one());
        for (uint32_t i = 0; i < e.elems.size(); ++i) {
            auto val = gen_expr(*e.elems[i]);
            if (!val) return nullptr;
            if (type->tuple_elems[i]) {
                auto et = logos_to_mlir(type->tuple_elems[i]);
                if (et) val = coerce_int(val, et);
            }
            llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
            auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, idx);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
        return alloca;
    }

    mlir::Value gen_expr_kind(const ETupleIndex& e, const LogosType* type) {
        auto recv = gen_expr(*e.receiver);
        if (!recv) return nullptr;
        auto stype = tuple_llvm_type(e.receiver->type);
        if (!stype) return nullptr;
        auto elem_mlir = logos_to_mlir(type);
        if (!elem_mlir) return nullptr;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(e.index)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, recv, idx);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, gep);
    }

    // ── Closure helpers ────────────────────────────────────────────

    // Closure value layout: { fn_ptr: ptr, env_ptr: ptr }
    mlir::Type closure_llvm_type() {
        return mlir::LLVM::LLVMStructType::getLiteral(
            builder_.getContext(), {ptr_type(), ptr_type()});
    }

    mlir::Value gen_expr_kind(const EClosureBox& box, const LogosType* type) {
        if (!box.inner) return nullptr;
        return gen_closure(*box.inner, type);
    }

    mlir::Value gen_closure(const EClosure& e, const LogosType*) {
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt = builder_.saveInsertionPoint();

        // Build capture struct type
        llvm::SmallVector<mlir::Type> cap_fields;
        for (auto* ct : e.capture_types) {
            auto ft = logos_to_mlir(ct);
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
        auto saved_scope = scope_;
        auto saved_lets  = let_vars_;
        auto saved_elems = var_elem_types_;
        auto saved_ret   = cur_ret_type_;
        scope_.clear(); let_vars_.clear(); var_elem_types_.clear();

        bool ret_is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(llvm_ret);
        cur_ret_type_ = ret_is_void ? mlir::Type{} : llvm_ret;

        // Unpack captures from env pointer (arg 0)
        auto env_ptr = entry->getArgument(0);
        for (size_t i = 0; i < e.captures.size(); ++i) {
            llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
            auto fp = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), cap_struct, env_ptr, idx);
            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, cap_fields[i], fp);
            // Store in alloca for let-variable semantics
            auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), cap_fields[i], i64_one());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            scope_[e.captures[i]] = alloca;
            let_vars_.insert(e.captures[i]);
            var_elem_types_[e.captures[i]] = cap_fields[i];
        }

        // Bind params (starting from arg 1)
        for (size_t i = 0; i < e.params.size(); ++i) {
            scope_[e.params[i].name] = entry->getArgument(i + 1);
        }

        // Generate body (inside llvm.func — use llvm.return)
        in_llvm_func_ = true;
        gen_block(e.body);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
        in_llvm_func_ = false;

        // Restore state
        scope_ = saved_scope;
        let_vars_ = saved_lets;
        var_elem_types_ = saved_elems;
        cur_ret_type_ = saved_ret;
        builder_.restoreInsertionPoint(save_pt);

        // At the creation site: alloca capture struct, store captures
        auto env_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), cap_struct, i64_one());
        for (size_t i = 0; i < e.captures.size(); ++i) {
            auto it = scope_.find(e.captures[i]);
            if (it == scope_.end()) continue;
            mlir::Value cap_val;
            auto eit = var_elem_types_.find(e.captures[i]);
            if (let_vars_.count(e.captures[i]) && eit != var_elem_types_.end())
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

    mlir::Value gen_expr_kind(const EClosureCall& e, const LogosType* type) {
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

    // ── Slice helpers ─────────────────────────────────────────────

    // Slice LLVM type: { ptr, i64 } (data pointer + length)
    mlir::Type slice_llvm_type() {
        return mlir::LLVM::LLVMStructType::getLiteral(
            builder_.getContext(), {ptr_type(), builder_.getI64Type()});
    }

    mlir::Value gen_expr_kind(const ESliceLit& e, const LogosType*) {
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

    mlir::Value gen_expr_kind(const ESliceIndex& e, const LogosType* type) {
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
        // GEP into data array by index
        auto idx32 = coerce_int(index, builder_.getI32Type());
        llvm::SmallVector<mlir::LLVM::GEPArg> di{idx32};
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, data_ptr, di);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, elem_ptr);
    }

    mlir::Value gen_expr_kind(const ESliceLen& e, const LogosType*) {
        auto slice = gen_expr(*e.slice);
        if (!slice) return nullptr;
        auto stype = slice_llvm_type();
        // Load len from field 1
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, li);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
    }

    // ── format() built-in ─────────────────────────────────────────

    static int format_type_tag(const LogosType* t) noexcept {
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

    mlir::Value gen_expr_kind(const EFormatCall& e, const LogosType*) {
        auto fmt_val = gen_expr(*e.fmt);
        if (!fmt_val) return nullptr;

        int n = (int)e.args.size();
        auto i32_type = builder_.getI32Type();
        auto i64_type = builder_.getI64Type();

        // Allocate [n x i32] tags and [n x i64] data arrays on stack.
        // Use at least 1 for zero-arg case to avoid zero-size alloca.
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

    mlir::Value gen_expr_kind(const EPackExpand&, const LogosType*) {
        // EPackExpand should be eliminated by mono before reaching mlir_gen.
        std::fprintf(stderr, "mlir_gen: unexpected EPackExpand (should be expanded by mono)\n");
        return nullptr;
    }

    // sizeof::<T>() — GEP null trick: ptrtoint(GEP(null, 1)) = sizeof(T) in bytes.
    mlir::Value gen_expr_kind(const ESizeOf& e, const LogosType*) {
        auto elem_mlir = logos_to_mlir(e.elem_type);
        if (!elem_mlir) {
            // Fallback: return 8 (pointer size) for unknown types.
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
        }
        // null pointer as i64 0 → ptr
        mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        mlir::Value null_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero);
        // GEP(null, 1) — advance by one element
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(1)};
        auto size_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_mlir, null_ptr, idx);
        // ptrtoint → i64
        return builder_.create<mlir::LLVM::PtrToIntOp>(
            loc_, builder_.getI64Type(), size_ptr);
    }

    // ── Try expression: expr? ─────────────────────────────────────
    // inner : *Result<T,E>  →  ok_block: yields T  /  err_block: early return Err(E)
    mlir::Value gen_expr_kind(const ETry& e, const LogosType* type) {
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

            // Build the early-return value: alloc a Result struct, fill Err(e), return
            // Use the inner Result's te info (same layout since T/E match the fn return).
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

    // ── Struct helpers ────────────────────────────────────────────

    mlir::Value get_struct_ptr(const std::string& name) {
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
            return nullptr;
        }
        return it->second;
    }

    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name) {
        for (auto& f : info.fields) {
            if (f.name == field_name) {
                llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(f.index)};
                return builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), info.llvm_type, base, idx);
            }
        }
        std::fprintf(stderr, "mlir_gen: struct '%s' has no field '%s'\n",
                     info.name.c_str(), field_name.c_str());
        return nullptr;
    }

    // Resolve receiver expr → (object_ptr, type_name).
    // Works for both structs (var_struct_) and classes (var_class_).
    std::pair<mlir::Value, std::string> gen_recv_struct(const LExpr& recv) {
        if (auto* vr = std::get_if<EVarRef>(&recv.kind)) {
            auto& name = vr->name;
            // Check class first
            auto cit = var_class_.find(name);
            if (cit != var_class_.end())
                return {get_struct_ptr(name), cit->second};
            // Then struct
            auto sit = var_struct_.find(name);
            if (sit != var_struct_.end())
                return {get_struct_ptr(name), sit->second};
            std::fprintf(stderr, "mlir_gen: '%s' is not a struct/class var\n", name.c_str());
            return {nullptr, {}};
        }
        if (auto* fr = std::get_if<EFieldRead>(&recv.kind)) {
            auto [base_ptr, base_sname] = gen_recv_struct(*fr->receiver);
            if (!base_ptr || base_sname.empty()) return {nullptr, {}};
            auto it = struct_types_.find(base_sname);
            if (it == struct_types_.end()) return {nullptr, {}};
            auto& info = it->second;
            auto gep = gep_field(base_ptr, info, fr->field);
            if (!gep) return {nullptr, {}};
            for (auto& f : info.fields) {
                if (f.name == fr->field) {
                    if (!f.struct_name.empty()) {
                        auto obj_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                           loc_, ptr_type(), gep);
                        return {obj_ptr, f.struct_name};
                    }
                    std::fprintf(stderr, "mlir_gen: field '%s' is not a struct/class type\n",
                                 fr->field.c_str());
                    return {nullptr, {}};
                }
            }
            return {nullptr, {}};
        }
        // General case: evaluate expression, derive type name from LExpr.type
        auto ptr = gen_expr(recv);
        if (!ptr) return {nullptr, {}};
        // If the result is an aggregate struct (by-value return), spill to alloca.
        if (mlir::isa<mlir::LLVM::LLVMStructType>(ptr.getType()))
            ptr = spill_to_alloca(ptr);
        if (recv.type) {
            const LogosType* t = recv.type;
            if (t->kind == LogosType::Kind::Ptr && t->pointee) t = t->pointee;
            if (t->kind == LogosType::Kind::Struct)
                return {ptr, concrete_struct_name(t)};
            if (t->kind == LogosType::Kind::Class)
                return {ptr, concrete_class_name(t)};
        }
        std::fprintf(stderr, "mlir_gen: unsupported receiver kind for struct/class access\n");
        return {nullptr, {}};
    }

    mlir::Value gen_struct_lit(const EStructLit& e) {
        auto sit = struct_types_.find(e.name);
        if (sit == struct_types_.end()) {
            std::fprintf(stderr, "mlir_gen: unknown struct '%s'\n", e.name.c_str());
            return nullptr;
        }
        auto& info  = sit->second;
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                          loc_, ptr_type(), info.llvm_type, i64_one());
        for (auto& [fname, fval] : e.fields) {
            // Find field metadata.
            const FieldInfo* fi = nullptr;
            for (auto& f : info.fields) if (f.name == fname) { fi = &f; break; }

            auto gep = gep_field(alloca, info, fname);
            if (!gep) return nullptr;

            // Special case: if the field is an inline array (LLVMArrayType) and
            // the initialiser is an EArrLit, copy elements one-by-one into the
            // struct field instead of storing a pointer returned by gen_arr_lit.
            auto arr_llvm = fi ? mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(fi->type)
                               : mlir::LLVM::LLVMArrayType{};
            if (arr_llvm && std::holds_alternative<EArrLit>(fval->kind)) {
                auto& arr_lit = std::get<EArrLit>(fval->kind);
                auto elem_type = arr_llvm.getElementType();
                for (uint64_t i = 0; i < arr_lit.elems.size(); ++i) {
                    auto val = gen_expr(*arr_lit.elems[i]);
                    if (!val) return nullptr;
                    val = coerce_int(val, elem_type);
                    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
                    auto elem_gep = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), arr_llvm, gep, idx);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, elem_gep);
                }
                continue;
            }

            auto val = gen_expr(*fval);
            if (!val) return nullptr;
            // Coerce scalar literals to the field's declared type (e.g. IntLit→i64).
            if (fi && fi->type && !mlir::isa<mlir::LLVM::LLVMArrayType>(fi->type))
                val = coerce_int(val, fi->type);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
        return alloca;
    }

    // ── Array helpers ─────────────────────────────────────────────

    mlir::Value get_subscript_ptr(const std::string& name) {
        auto it = scope_.find(name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
            return nullptr;
        }
        return it->second;
    }

    mlir::Type subscript_elem_type(const std::string& name) {
        auto it = var_elem_types_.find(name);
        if (it != var_elem_types_.end()) return it->second;
        auto sit = var_subscript_.find(name);
        if (sit != var_subscript_.end()) return sit->second;
        return builder_.getI32Type();
    }

    mlir::Value gen_arr_lit(const EArrLit& e, mlir::Type elem_type) {
        uint64_t n = e.elems.size();
        auto arr_type = mlir::LLVM::LLVMArrayType::get(elem_type, n);
        auto alloca   = builder_.create<mlir::LLVM::AllocaOp>(
                            loc_, ptr_type(), arr_type, i64_one());
        for (uint64_t i = 0; i < n; ++i) {
            auto val = gen_expr(*e.elems[i]);
            if (!val) return nullptr;
            val = coerce_int(val, elem_type);
            llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(i)};
            auto gep = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), elem_type, alloca, idx);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
        return alloca;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const lir::LProgram& prog) noexcept
{
    MLIRGenImpl gen(ctx);
    return gen.generate(prog);
}

} // namespace logos::compiler
