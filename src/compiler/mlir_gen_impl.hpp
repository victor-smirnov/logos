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
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
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
        int64_t disc;
        std::vector<mlir::Type>        field_types;   // empty = no payload
        std::vector<TypeRef>  logos_types;   // parallel: original LogosType per field
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

    // Set in generate(); used by view-ref helpers below to resolve LExpr*/LStmt*/Pattern*
    // back to mirror offsets so callers can read fields through lir_view types.
    const LProgram*       prog_   = nullptr;
    const LirMirrorTable* mirror_ = nullptr;

    lir_view::ExprRef expr_ref_of(const LExpr& e) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->expr.find(&e);
        if (it == mirror_->expr.end()) return {};
        return lir_view::ExprRef(prog_->type_pool.arena(), it->second);
    }
    // Resolve an ExprRef back to its variant LExpr* via the mirror's reverse
    // map. Used inside view-handlers to recurse through gen_expr() on
    // sub-expressions while the rest of the dispatcher still walks variants.
    const LExpr* lexpr_of(lir_view::ExprRef r) const noexcept {
        if (!mirror_) return nullptr;
        auto it = mirror_->expr_by_offset.find(uint32_t(r.offset()));
        if (it == mirror_->expr_by_offset.end()) return nullptr;
        return it->second;
    }
    lir_view::StmtRef stmt_ref_of(const LStmt& s) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->stmt.find(&s);
        if (it == mirror_->stmt.end()) return {};
        return lir_view::StmtRef(prog_->type_pool.arena(), it->second);
    }
    lir_view::PatRef pat_ref_of(const Pattern& p) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->pat.find(&p);
        if (it == mirror_->pat.end()) return {};
        return lir_view::PatRef(prog_->type_pool.arena(), it->second);
    }
    const LBlock* lblock_of(lir_view::BlockRef r) const noexcept {
        if (!mirror_) return nullptr;
        auto it = mirror_->block_by_offset.find(uint32_t(r.offset()));
        if (it == mirror_->block_by_offset.end()) return nullptr;
        return it->second;
    }
    const LStmt* lstmt_of(lir_view::StmtRef r) const noexcept {
        if (!mirror_) return nullptr;
        auto it = mirror_->stmt_by_offset.find(uint32_t(r.offset()));
        if (it == mirror_->stmt_by_offset.end()) return nullptr;
        return it->second;
    }
    const TypePoolImpl* pool_impl() const noexcept {
        return prog_ ? prog_->type_pool.impl() : nullptr;
    }

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
    std::unordered_map<std::string, std::vector<TypeRef>> fn_param_types_;

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
    TypeRef                              cur_fn_ret_logos_type_ = nullptr;
    bool                                          in_llvm_func_ = false;
    // Entry block of the function currently being emitted.  All LLVM::AllocaOp
    // instructions must be inserted here (at the top) so LLVM treats them as
    // *static* allocas — otherwise an alloca inside a loop body grows the
    // stack frame on every iteration and eventually overflows.
    mlir::Block*                                  cur_entry_block_ = nullptr;

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

    // Create an LLVM::AllocaOp in the current function's entry block so it
    // is recognised as a static alloca (reused across loop iterations /
    // function calls, never growing the stack dynamically).  Returns the
    // alloca pointer.  The builder's insertion point is restored before
    // returning so the caller can continue emitting at its original site.
    mlir::Value create_entry_alloca(mlir::Type elem_type, int64_t count = 1) {
        if (!cur_entry_block_) {
            // Fallback for callers outside a tracked function body (should
            // not happen in practice; preserve old behaviour just in case).
            auto cnt = builder_.create<mlir::arith::ConstantIntOp>(loc_, count, 64);
            return builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), elem_type, cnt);
        }
        mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToStart(cur_entry_block_);
        auto cnt = builder_.create<mlir::arith::ConstantIntOp>(loc_, count, 64);
        return builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), elem_type, cnt);
    }

    // Spill an aggregate value (struct/enum returned by value) to an alloca.
    // Used when passing such a value to a function that expects a pointer.
    mlir::Value spill_to_alloca(mlir::Value v) {
        auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(v.getType());
        if (!st) return v;
        auto alloca = create_entry_alloca(st);
        builder_.create<mlir::LLVM::StoreOp>(loc_, v, alloca);
        return alloca;
    }

    mlir::Value coerce_int(mlir::Value v, mlir::Type to,
                           TypeRef src_lt = nullptr) {
        if (!v || !to || v.getType() == to) return v;
        auto fi = mlir::dyn_cast<mlir::IntegerType>(v.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!fi || !ti) return v;
        if (ti.getWidth() > fi.getWidth()) {
            // Pick zero vs sign extend by *source* signedness when known.
            // Bool (i1) is always zero-extended.  Without src_lt, fall back
            // to sign-extend to preserve legacy behavior for signed sources.
            bool src_unsigned = fi.getWidth() == 1;
            if (src_lt) {
                using K = LogosType::Kind;
                auto k = TypeRef(src_lt).kind();
                src_unsigned = src_unsigned ||
                    k == K::U8 || k == K::U16 || k == K::U24 || k == K::U32 ||
                    k == K::U56 || k == K::U64 || k == K::U128 || k == K::Bool;
            }
            if (src_unsigned)
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
                               TypeRef src_lt = nullptr) {
        if (!v || !to || v.getType() == to) return v;
        // int → int
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::IntegerType>(to))
            return coerce_int(v, to, src_lt);
        // float → float (truncate or extend)
        if (mlir::isa<mlir::FloatType>(v.getType()) && mlir::isa<mlir::FloatType>(to))
            return coerce_float(v, to);
        // int → float: use unsigned op for unsigned Logos types
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::FloatType>(to)) {
            auto src_k = src_lt ? TypeRef(src_lt).kind() : LogosType::Kind::Error;
            bool src_unsigned = src_lt &&
                (src_k == LogosType::Kind::U8   ||
                 src_k == LogosType::Kind::U16  ||
                 src_k == LogosType::Kind::U32  ||
                 src_k == LogosType::Kind::U56  ||
                 src_k == LogosType::Kind::U64  ||
                 src_k == LogosType::Kind::U128);
            if (src_unsigned)
                return builder_.create<mlir::arith::UIToFPOp>(loc_, to, v);
            return builder_.create<mlir::arith::SIToFPOp>(loc_, to, v);
        }
        return v;
    }

    // ── Type conversion ──────────────────────────────────────────
    mlir::Type logos_to_mlir(TypeRef tv);

    // MLIR type for a C-style enum's discriminant.  Uses the enum's
    // explicit backing type if declared (`enum Foo : u64 {}`), else i32.
    mlir::Type enum_disc_mlir(const std::string& enum_name) {
        auto it = enum_types_.find(enum_name);
        if (it != enum_types_.end() && it->second->backing_type) {
            auto t = logos_to_mlir(it->second->backing_type);
            if (t) return t;
        }
        return builder_.getI32Type();
    }
    unsigned enum_disc_bits(const std::string& enum_name) {
        auto t = enum_disc_mlir(enum_name);
        if (auto it = mlir::dyn_cast<mlir::IntegerType>(t)) return it.getWidth();
        return 32;
    }

    // ── Struct / enum / class registration ──────────────────────
    bool register_struct(const LStructDef& sd);
    void register_tagged_enum(const LEnumDef& ed);
    uint64_t logos_abi_byte_size(TypeRef t,
                                  std::unordered_set<std::string>& seen);

    // Resolve a tagged enum name from the expression type (handles generic enums).
    const TaggedEnumInfo* resolve_tagged_enum(const std::string& name, TypeRef type);

    // Build the anonymous LLVM struct type for a tuple.
    mlir::Type tuple_llvm_type(TypeRef t);

    // Compute the LLVM return type matching how function definitions return
    // values (struct/tuple/enum aggregates by value, not as ptr). Used to
    // build correct ABI-matching call types for indirect / fn-pointer calls.
    mlir::Type fn_call_ret_llvm_type(TypeRef ret_type);

    // Slice LLVM type: { ptr, i64 }
    mlir::Type slice_llvm_type();

    // Closure LLVM type: { fn_ptr, env_ptr }
    mlir::Type closure_llvm_type();

    // ── Vtable / dyn ─────────────────────────────────────────────
    void emit_trait_vtables(mlir::ModuleOp mod, const LProgram& prog);
    void emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog);
    mlir::Value build_inline_vtable(std::string_view trait_name,
                                     std::string_view type_name);
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                               std::string_view src_type_name);
    mlir::Value gen_dyn_dispatch(const EMethodCall& e, TypeRef ret_logos_type);
    mlir::Value gen_tagged_dispatch(const EMethodCall& e, TypeRef ret_logos_type);

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

    void gen_stmt_kind(lir_view::SLetView v);
    void gen_stmt_kind(lir_view::SAssignView v);
    void gen_stmt_kind(lir_view::SReturnView v);
    void gen_stmt_kind(lir_view::SIfView v);
    void gen_stmt_kind(lir_view::SWhileView v);
    void gen_stmt_kind(lir_view::SForView v);
    void gen_stmt_kind(lir_view::SLoopView v);
    void gen_stmt_kind(lir_view::SBreakView v);
    void gen_stmt_kind(lir_view::SContinueView v);
    void gen_stmt_kind(lir_view::SFieldWriteView v);
    void gen_stmt_kind(lir_view::STupleWriteView v);
    void gen_stmt_kind(lir_view::SDerefFieldWriteView v);
    void gen_stmt_kind(lir_view::SIndexWriteView v);
    void gen_stmt_kind(lir_view::SFieldIndexWriteView v);
    void gen_stmt_kind(lir_view::SExprStmtView v);
    void gen_stmt_kind(lir_view::SMatchView v);
    void gen_stmt_kind(lir_view::SDeleteView v);
    void gen_stmt_kind(lir_view::SForEachView v);
    void gen_stmt_kind(lir_view::SBlockView v);
    void gen_stmt_kind(lir_view::SDropView v);
    void gen_stmt_kind(lir_view::SDerefWriteView v);
    void gen_stmt_kind(lir_view::SLetElseView v);
    void gen_stmt_kind(lir_view::SChainFieldWriteView v);

    void gen_let(lir_view::SLetView v);
    void gen_assign(lir_view::SAssignView v);
    void gen_return(lir_view::SReturnView v);
    void gen_if(lir_view::SIfView v);
    void gen_while(lir_view::SWhileView v);
    void gen_for(lir_view::SForView v);
    void gen_loop(lir_view::SLoopView v);
    void gen_break(lir_view::SBreakView v);
    void gen_continue();
    void gen_for_each(lir_view::SForEachView v);
    void gen_field_write(lir_view::SFieldWriteView v);
    void gen_deref_field_write(lir_view::SDerefFieldWriteView v);
    void gen_chain_field_write(lir_view::SChainFieldWriteView v);
    void gen_tuple_write(lir_view::STupleWriteView v);
    void gen_index_write(lir_view::SIndexWriteView v);
    void gen_field_index_write(lir_view::SFieldIndexWriteView v);
    void gen_match(const SMatch& s);
    void gen_delete(lir_view::SDeleteView v);

    // ── Expressions ───────────────────────────────────────────────
    mlir::Value gen_expr(const LExpr& e);

    mlir::Value gen_expr_kind(lir_view::ELitIntView v,   TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ELitFloatView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ELitBoolView v,  TypeRef);
    mlir::Value gen_expr_kind(lir_view::ELitStrView v,   TypeRef);
    mlir::Value gen_expr_kind(lir_view::EVarRefView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EEnumLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EEnumLitDataView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EBinOpView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EUnaryView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EAddrOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EAddrOfTempView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EDerefView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EFieldReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EIndexReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EStructLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EArrLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ECastView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ENewView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EIfExprView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EMatchExprView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ETupleLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ETupleIndexView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EClosureBoxView v, TypeRef type);
    mlir::Value gen_closure(lir_view::EClosureBoxView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EClosureCallView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EFnPtrCallView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ESliceLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESliceIndexView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ESliceLenView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESlicePtrView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EFormatCallView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPackExpandView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESizeOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ETypeCodeOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EBlockExprView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ETryView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EHermesLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPtrArithView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPtrDiffView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EReflectOfView v, TypeRef);
    // Coerce a Logos runtime value to AnyVal.raw (u32) for hermes capture substitution.
    mlir::Value coerce_to_anyval_raw(mlir::Value v, TypeRef t);

    // ── Struct helpers ────────────────────────────────────────────
    mlir::Value get_struct_ptr(const std::string& name);
    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name);
    std::pair<mlir::Value, std::string> gen_recv_struct(const LExpr& recv);
    mlir::Value gen_struct_lit(lir_view::EStructLitView v);

    // ── Array helpers ─────────────────────────────────────────────
    mlir::Value get_subscript_ptr(const std::string& name);
    mlir::Type subscript_elem_type(const std::string& name);
    mlir::Value gen_arr_lit(lir_view::EArrLitView v, mlir::Type elem_type);

    // ── format() built-in ─────────────────────────────────────────
    static int format_type_tag(TypeRef t) noexcept;
};

} // namespace logos::compiler
