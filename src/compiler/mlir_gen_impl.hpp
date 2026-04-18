// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_impl.hpp — MLIRGenImpl class definition shared across all mlir_gen_*.cpp TUs.
//
// Each mlir_gen_*.cpp includes this header and defines a subset of MLIRGenImpl methods.
// The class itself is declared here but NOT defined (no method bodies here).

#pragma once

#include "mlir_gen.hpp"

#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>

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

    mlir::OwningOpRef<mlir::ModuleOp> generate(const LProgram& prog);

private:
    mlir::OpBuilder builder_;
    mlir::Location  loc_;

    std::unordered_map<std::string, StructInfo>        struct_types_;
    std::unordered_map<std::string, const LStructDef*> all_struct_defs_; // name→def for recursive registration
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

    struct LoopBlocks {
        mlir::Block*  cont;
        mlir::Block*  exit;
        mlir::Value   break_slot;  // alloca for break-value; null if loop is void
        std::string   label;       // loop label (e.g. "'outer"), empty = unlabeled
    };
    std::vector<LoopBlocks> loop_stack_;

    int str_counter_ = 0;
    int hermes_lit_counter_ = 0;

    // "Trait::Type" → mangled method names in vtable slot order
    std::unordered_map<std::string, std::vector<std::string>> dyn_vtable_methods_;

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

    mlir::Value coerce_float(mlir::Value v, mlir::Type to) {
        if (!v || !to || v.getType() == to) return v;
        auto fv = mlir::dyn_cast<mlir::FloatType>(v.getType());
        auto ft = mlir::dyn_cast<mlir::FloatType>(to);
        if (!fv || !ft) return v;
        if (ft.getWidth() < fv.getWidth())
            return builder_.create<mlir::arith::TruncFOp>(loc_, to, v);
        return builder_.create<mlir::arith::ExtFOp>(loc_, to, v);
    }

    // Coerce any numeric value: int→int, float→float, int→float.
    // Does NOT handle float→int (that requires an explicit cast).
    // src_lt: Logos source type — required for correct signed/unsigned int→float conversion.
    mlir::Value coerce_numeric(mlir::Value v, mlir::Type to,
                               const LogosType* src_lt = nullptr) {
        if (!v || !to || v.getType() == to) return v;
        // int → int
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::IntegerType>(to))
            return coerce_int(v, to);
        // float → float (truncate or extend)
        if (mlir::isa<mlir::FloatType>(v.getType()) && mlir::isa<mlir::FloatType>(to))
            return coerce_float(v, to);
        // int → float: use unsigned op for unsigned Logos types
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::FloatType>(to)) {
            bool src_unsigned = src_lt &&
                (src_lt->kind == LogosType::Kind::U8   ||
                 src_lt->kind == LogosType::Kind::U16  ||
                 src_lt->kind == LogosType::Kind::U32  ||
                 src_lt->kind == LogosType::Kind::U56  ||
                 src_lt->kind == LogosType::Kind::U64  ||
                 src_lt->kind == LogosType::Kind::U128);
            if (src_unsigned)
                return builder_.create<mlir::arith::UIToFPOp>(loc_, to, v);
            return builder_.create<mlir::arith::SIToFPOp>(loc_, to, v);
        }
        return v;
    }

    // ── Type conversion ──────────────────────────────────────────
    mlir::Type logos_to_mlir(const LogosType* t);

    // ── Struct / enum / class registration ──────────────────────
    bool register_struct(const LStructDef& sd);
    void register_tagged_enum(const LEnumDef& ed);

    // Resolve a tagged enum name from the expression type (handles generic enums).
    const TaggedEnumInfo* resolve_tagged_enum(const std::string& name, const LogosType* type);

    // Build the anonymous LLVM struct type for a tuple.
    mlir::Type tuple_llvm_type(const LogosType* t);

    // Slice LLVM type: { ptr, i64 }
    mlir::Type slice_llvm_type();

    // Closure LLVM type: { fn_ptr, env_ptr }
    mlir::Type closure_llvm_type();

    // ── Vtable / dyn ─────────────────────────────────────────────
    void emit_trait_vtables(mlir::ModuleOp mod, const LProgram& prog);
    void emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog);
    mlir::Value build_inline_vtable(const std::string& trait_name,
                                     const std::string& type_name);
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, const std::string& trait_name,
                               const std::string& src_type_name);
    mlir::Value gen_dyn_dispatch(const EMethodCall& e, const LogosType* ret_logos_type);
    mlir::Value gen_tagged_dispatch(const EMethodCall& e, const LogosType* ret_logos_type);

    // ── malloc / free helpers ─────────────────────────────────────
    void ensure_malloc_free(mlir::ModuleOp mod);
    mlir::Value call_malloc(mlir::Value size);
    void call_free(mlir::Value ptr);
    mlir::Value sizeof_struct(mlir::LLVM::LLVMStructType struct_type);

    // ── Function type from LFunction ─────────────────────────────
    mlir::FunctionType make_fn_type(const LFunction& fn);
    void forward_declare(mlir::ModuleOp mod, const LFunction& fn);
    bool gen_function_body(mlir::func::FuncOp func, const LFunction& fn);

    // ── Block ─────────────────────────────────────────────────────
    void gen_block(const LBlock& block);

    // ── Statements ────────────────────────────────────────────────
    void gen_stmt(const LStmt& stmt);

    void gen_stmt_kind(const SLet& s);
    void gen_stmt_kind(const SAssign& s);
    void gen_stmt_kind(const SReturn& s);
    void gen_stmt_kind(const SIf& s);
    void gen_stmt_kind(const SWhile& s);
    void gen_stmt_kind(const SFor& s);
    void gen_stmt_kind(const SLoop& s);
    void gen_stmt_kind(const SBreak&);
    void gen_stmt_kind(const SContinue&);
    void gen_stmt_kind(const SFieldWrite& s);
    void gen_stmt_kind(const STupleWrite& s);
    void gen_stmt_kind(const SDerefFieldWrite& s);
    void gen_stmt_kind(const SIndexWrite& s);
    void gen_stmt_kind(const SFieldIndexWrite& s);
    void gen_stmt_kind(const SExprStmt& s);
    void gen_stmt_kind(const SMatch& s);
    void gen_stmt_kind(const SDelete& s);
    void gen_stmt_kind(const SForEach& s);
    void gen_stmt_kind(const SBlock& s);
    void gen_stmt_kind(const SDrop& s);
    void gen_stmt_kind(const SDerefWrite& s);
    void gen_stmt_kind(const SLetElse& s);
    void gen_stmt_kind(const SChainFieldWrite& s);

    void gen_let(const SLet& s);
    void gen_assign(const SAssign& s);
    void gen_return(const SReturn& s);
    void gen_if(const SIf& s);
    void gen_while(const SWhile& s);
    void gen_for(const SFor& s);
    void gen_loop(const SLoop& s);
    void gen_break(const SBreak& s);
    void gen_continue();
    void gen_for_each(const SForEach& s);
    void gen_field_write(const SFieldWrite& s);
    void gen_deref_field_write(const SDerefFieldWrite& s);
    void gen_chain_field_write(const SChainFieldWrite& s);
    void gen_tuple_write(const STupleWrite& s);
    void gen_index_write(const SIndexWrite& s);
    void gen_field_index_write(const SFieldIndexWrite& s);
    void gen_match(const SMatch& s);
    void gen_delete(const SDelete& s);

    // ── Expressions ───────────────────────────────────────────────
    mlir::Value gen_expr(const LExpr& e);

    mlir::Value gen_expr_kind(const ELitInt& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ELitFloat& e, const LogosType*);
    mlir::Value gen_expr_kind(const ELitBool& e, const LogosType*);
    mlir::Value gen_expr_kind(const ELitStr& e, const LogosType*);
    mlir::Value gen_expr_kind(const EVarRef& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EEnumLit& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EEnumLitData& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EBinOp& e, const LogosType*);
    mlir::Value gen_expr_kind(const EUnary& e, const LogosType*);
    mlir::Value gen_expr_kind(const EAddrOf& e, const LogosType*);
    mlir::Value gen_expr_kind(const EAddrOfTemp& e, const LogosType*);
    mlir::Value gen_expr_kind(const EDeref& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ECall& e, const LogosType* ret_logos_type);
    mlir::Value gen_expr_kind(const EMethodCall& e, const LogosType* ret_logos_type);
    mlir::Value gen_expr_kind(const EFieldRead& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EIndexRead& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EStructLit& e, const LogosType*);
    mlir::Value gen_expr_kind(const EArrLit& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ECast& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ENew& e, const LogosType*);
    mlir::Value gen_expr_kind(const EIfExpr& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EMatchExpr& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ETupleLit& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ETupleIndex& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EClosureBox& box, const LogosType* type);
    mlir::Value gen_closure(const EClosure& e, const LogosType*);
    mlir::Value gen_expr_kind(const EClosureCall& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EFnPtrCall& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ESliceLit& e, const LogosType*);
    mlir::Value gen_expr_kind(const ESliceIndex& e, const LogosType* type);
    mlir::Value gen_expr_kind(const ESliceLen& e, const LogosType*);
    mlir::Value gen_expr_kind(const EFormatCall& e, const LogosType*);
    mlir::Value gen_expr_kind(const EPackExpand&, const LogosType*);
    mlir::Value gen_expr_kind(const ESizeOf& e, const LogosType*);
    mlir::Value gen_expr_kind(const ETypeCodeOf& e, const LogosType*);
    mlir::Value gen_expr_kind(const EBlockExpr& e, const LogosType*);
    mlir::Value gen_expr_kind(const ETry& e, const LogosType* type);
    mlir::Value gen_expr_kind(const EHermesLit& e, const LogosType*);

    // ── Struct helpers ────────────────────────────────────────────
    mlir::Value get_struct_ptr(const std::string& name);
    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name);
    std::pair<mlir::Value, std::string> gen_recv_struct(const LExpr& recv);
    mlir::Value gen_struct_lit(const EStructLit& e);

    // ── Array helpers ─────────────────────────────────────────────
    mlir::Value get_subscript_ptr(const std::string& name);
    mlir::Type subscript_elem_type(const std::string& name);
    mlir::Value gen_arr_lit(const EArrLit& e, mlir::Type elem_type);

    // ── format() built-in ─────────────────────────────────────────
    static int format_type_tag(const LogosType* t) noexcept;
};

} // namespace logos::compiler
