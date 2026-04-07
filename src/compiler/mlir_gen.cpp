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
    mlir::Type                                    cur_ret_type_;

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

    mlir::Value coerce_int(mlir::Value v, mlir::Type to) {
        if (!v || !to || v.getType() == to) return v;
        auto fi = mlir::dyn_cast<mlir::IntegerType>(v.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!fi || !ti) return v;
        if (ti.getWidth() > fi.getWidth())
            return builder_.create<mlir::arith::ExtSIOp>(loc_, to, v);
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
        case LogosType::Kind::TypeVar:
            // TypeVar should have been eliminated by mono_pass.
            // Treat as error type to produce a clear diagnostic.
            std::fprintf(stderr, "mlir_gen: unresolved TypeVar '%s' — mono_pass required\n",
                         t->type_var_name.c_str());
            return nullptr;
        case LogosType::Kind::Error:  return nullptr;
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
            // Tuples are returned by value (as LLVM struct), not by pointer.
            if (fn.ret_type->kind == LogosType::Kind::Tuple) {
                auto rt = tuple_llvm_type(fn.ret_type);
                if (rt) ret_types.push_back(rt);
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
    void gen_stmt_kind(const SFieldWrite& s)  { gen_field_write(s); }
    void gen_stmt_kind(const SIndexWrite& s)  { gen_index_write(s); }
    void gen_stmt_kind(const SExprStmt& s)    { gen_expr(*s.expr); }
    void gen_stmt_kind(const SMatch& s)       { gen_match(s); }
    void gen_stmt_kind(const SDelete& s)      { gen_delete(s); }
    void gen_stmt_kind(const SForEach& s)     { gen_for_each(s); }

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

        // ── Slice value ──────────────────────────────────────────
        if (s.type && s.type->kind == LogosType::Kind::Slice) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            scope_[s.name] = val;
            let_vars_.insert(s.name);
            var_tuple_.insert(s.name);  // reuse tuple tracking (return ptr directly)
            return;
        }

        // ── Tagged enum value ────────────────────────────────────
        if (s.type && s.type->kind == LogosType::Kind::Enum) {
            auto* te = resolve_tagged_enum(s.type->enum_name, s.type);
            if (te) {
                auto val = gen_expr(*s.value);
                if (!val) return;
                // If gen_expr returned a plain i32 (e.g. Option::None with no type_args
                // on the expression), create the tagged enum alloca manually.
                if (val.getType() != ptr_type()) {
                    auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
                        loc_, ptr_type(), te->llvm_type, i64_one());
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), te->llvm_type, alloca, di);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
                    val = alloca;
                }
                scope_[s.name] = val;
                let_vars_.insert(s.name);
                var_tagged_enum_.insert(s.name);
                return;
            }
        }

        // ── Struct value (from call or variable) ─────────────────
        // Structs are represented as pointers — store the pointer directly,
        // no wrapper alloca needed.
        if (s.type && s.type->kind == LogosType::Kind::Struct) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            scope_[s.name]    = val;
            let_vars_.insert(s.name);
            var_struct_[s.name] = concrete_struct_name(s.type);
            return;
        }

        // ── Class pointer (from 'new') ────────────────────────────
        // 'new ClassName { ... }' returns *mut ClassName.  Store the
        // heap pointer directly — no alloca wrapper needed.
        if (s.type && s.type->kind == LogosType::Kind::Ptr &&
            s.type->pointee && s.type->pointee->kind == LogosType::Kind::Class) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            scope_[s.name]  = val;
            let_vars_.insert(s.name);
            var_class_[s.name] = concrete_class_name(s.type->pointee);
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
    }

    void gen_assign(const SAssign& s) {
        auto val = gen_expr(*s.value);
        if (!val) return;
        auto it = scope_.find(s.name);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: assign to undefined '%s'\n", s.name.c_str());
            return;
        }
        auto et = var_elem_types_.find(s.name);
        if (et != var_elem_types_.end())
            val = coerce_int(val, et->second);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, it->second);
    }

    void gen_return(const SReturn& s) {
        if (s.value) {
            auto val = gen_expr(*s.value);
            if (!val) return;
            // Tuple return: val is a pointer — load the struct value.
            if (cur_ret_type_ && mlir::isa<mlir::LLVM::LLVMStructType>(cur_ret_type_))
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, val);
            else if (cur_ret_type_)
                val = coerce_int(val, cur_ret_type_);
            builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{val});
        } else {
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
        auto* exit_block = new mlir::Block();
        region->push_back(cond_block);
        region->push_back(body_block);
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
        loop_stack_.push_back({cond_block, exit_block});
        gen_block(*s.body);
        loop_stack_.pop_back();
        if (!is_terminated(builder_.getBlock())) {
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
        // Evaluate the iter (array) expression — yields a pointer to the array data.
        mlir::Type elem_mlir = logos_to_mlir(s.elem_type);
        if (!elem_mlir) return;

        // The source expression evaluates to a pointer (alloca or parameter).
        auto arr_alloca = gen_expr(*s.iter);
        if (!arr_alloca) return;

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
        // Alloca for the loop variable (element copy)
        auto elem_alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), elem_mlir, i64_one());
        // Load arr[i] → elem_alloca
        // Use elem_type (flat pointer) GEP — same as gen_index_read
        mlir::Value i_cur = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), i_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> arr_idx{i_cur};
        auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_mlir, arr_alloca, arr_idx);
        auto elem_val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, elem_ptr);
        builder_.create<mlir::LLVM::StoreOp>(loc_, elem_val, elem_alloca);

        scope_[s.var]         = elem_alloca;
        let_vars_.insert(s.var);
        var_elem_types_[s.var] = elem_mlir;

        loop_stack_.push_back({cond_block, exit_block});
        gen_block(*s.body);
        loop_stack_.pop_back();

        if (!is_terminated(builder_.getBlock())) {
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

    void gen_index_write(const SIndexWrite& s) {
        auto it = scope_.find(s.arr);
        if (it == scope_.end()) {
            std::fprintf(stderr, "mlir_gen: index write: undefined '%s'\n", s.arr.c_str());
            return;
        }
        auto et = var_elem_types_.find(s.arr);
        mlir::Type elem_type = (et != var_elem_types_.end())
                               ? et->second : builder_.getI32Type();

        auto idx = gen_expr(*s.index);
        auto val = gen_expr(*s.value);
        if (!idx || !val) return;
        val = coerce_int(val, elem_type);

        llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), elem_type, it->second, indices);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
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
                gen_block(*last_arm.body);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
            else_block = last_body;
            last_tested = (int)s.arms.size() - 2; // skip last arm in the main loop
        }

        // Build if-else chain from second-to-last arm down to first.
        for (int i = last_tested; i >= 0; --i) {
            auto& arm = s.arms[i];
            auto* body_block = new mlir::Block();
            region->push_back(body_block);
            {
                mlir::OpBuilder::InsertionGuard guard(builder_);
                builder_.setInsertionPointToStart(body_block);
                // Extract payload bindings for tagged enum patterns
                if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) {
                    if (te_info && scrut_ptr) {
                        // GEP to payload area
                        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(1)};
                        auto pay_ptr = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), te_info->llvm_type, scrut_ptr, pi);
                        // Find variant payload types
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
                                // Bind as let variable
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
                gen_block(*arm.body);
                if (!is_terminated(builder_.getBlock()))
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }

            bool is_wild = std::holds_alternative<PatWild>(arm.pat);
            if (is_wild) {
                else_block = body_block;
            } else {
                int32_t disc = 0;
                if (auto* pv = std::get_if<PatVariant>(&arm.pat)) disc = pv->disc;
                else if (auto* pvd = std::get_if<PatVariantData>(&arm.pat)) disc = pvd->disc;
                else if (auto* pi = std::get_if<PatInt>(&arm.pat))  disc = pi->value;
                else if (auto* pb = std::get_if<PatBool>(&arm.pat)) disc = pb->value ? 1 : 0;

                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                {
                    mlir::OpBuilder::InsertionGuard guard(builder_);
                    builder_.setInsertionPointToStart(test_block);
                    auto disc_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut_i32, disc_val);
                    builder_.create<mlir::cf::CondBranchOp>(loc_, eq, body_block, else_block);
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
        // Struct/class/array/tuple/tagged-enum variables: return pointer directly.
        if (var_struct_.count(e.name) || var_subscript_.count(e.name) ||
            var_class_.count(e.name) || var_tuple_.count(e.name) ||
            var_tagged_enum_.count(e.name))
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
        // Alloca the enum struct
        auto alloca = builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), info.llvm_type, i64_one());
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
        if (op == "==") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq,  lhs, rhs);
        if (op == "!=") return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::ne,  lhs, rhs);
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
        for (size_t i = 0; i < e.args.size(); ++i) {
            auto v = gen_expr(*e.args[i]);
            if (!v) return nullptr;
            if (i < param_types.size()) v = coerce_int(v, param_types[i]);
            args.push_back(v);
        }
        auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    mlir::Value gen_expr_kind(const EMethodCall& e, const LogosType*) {
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
            if (pi < param_types.size()) v = coerce_int(v, param_types[pi]);
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
            arr_ptr   = get_subscript_ptr(vr->name);
            elem_type = subscript_elem_type(vr->name);
        } else if (auto* fr = std::get_if<EFieldRead>(&e.receiver->kind)) {
            // When the receiver is a field access (e.g. c.arr[0]) and the field
            // is an array type, we must GEP to the field and use that pointer as
            // the array base — NOT load the field value (which would yield an
            // LLVM array value, not a pointer).
            auto [struct_ptr, sname] = gen_recv_struct(*fr->receiver);
            if (struct_ptr && !sname.empty()) {
                auto& info = struct_types_[sname];
                auto field_ptr = gep_field(struct_ptr, info, fr->field);
                if (field_ptr) {
                    arr_ptr = field_ptr;
                    // Determine element type from LExpr type (element of the array).
                    elem_type = logos_to_mlir(type);
                    if (!elem_type) elem_type = builder_.getI32Type();
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
            if (ti.getWidth() > fi.getWidth())
                return builder_.create<mlir::arith::ExtSIOp>(loc_, target, val);
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
