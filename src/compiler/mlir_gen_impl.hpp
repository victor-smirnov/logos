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
    std::string struct_name;   // non-empty if field is struct, *struct, &struct, &mut struct.
    std::string trait_name;    // non-empty if field is &dyn Trait / *const dyn Trait / *mut dyn Trait;
                               // signals struct-lit init to fat-pointer-coerce (B-dyn-field).
    bool        is_pointer = false;  // true for *T / &T / &mut T fields. The struct_name
                                     // is still populated (so chain-field access via the
                                     // pointer can resolve), but auto-Drop must skip these
                                     // — they don't own the pointee.
};

struct StructInfo {
    std::string                  name;
    mlir::LLVM::LLVMStructType   llvm_type;
    std::vector<FieldInfo>       fields;
};

// Tagged enum registry: { i32 discriminant, <payload blob of payload_align> }.
// The payload blob carries the widest variant's alignment so an i64 / ptr /
// align-8 struct payload lands on an aligned offset (LLVM places the blob after
// the disc with the needed padding); the enum's own alignment = max(4, align).
struct TaggedEnumInfo {
    std::string                         name;
    mlir::LLVM::LLVMStructType          llvm_type;
    uint64_t                            payload_bytes = 0;
    uint64_t                            payload_align = 1;  // max align over variants
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
        if (!prog_ || e.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(prog_->type_pool.arena(), e.mirror_offset_);
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
        if (!prog_ || s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(prog_->type_pool.arena(), s.mirror_offset_);
    }
    lir_view::PatRef pat_ref_of(const Pattern& p) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->pat.find(&p);
        if (it == mirror_->pat.end()) return {};
        return lir_view::PatRef(prog_->type_pool.arena(), it->second);
    }
    lir_view::BlockRef block_ref_of(const LBlock& b) const noexcept {
        if (!prog_ || b.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::BlockRef(prog_->type_pool.arena(), b.mirror_offset_);
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
    // Resolve `<struct>__<method>` to the actual mangled fn symbol in
    // prog_->structs (sema may append `__f__sig` / `__g__sig` under
    // overload mangling). Returns the bare convention name as fallback
    // when no match is found.
    std::string resolve_method_symbol(std::string_view struct_name,
                                      std::string_view method_name) const noexcept {
        auto bare_struct = strip_struct_pkg(struct_name);
        std::string base; base.reserve(bare_struct.size() + 2 + method_name.size());
        base.append(bare_struct); base.append("__"); base.append(method_name);
        if (!prog_) return base;
        // After unification, method names may be `[pkg.]Base__method[__f__sig]`.
        // Match either bare base or a pkg-qualified form ending with `.base`.
        auto matches = [&](std::string_view nm) -> bool {
            if (nm == base) return true;
            // Check `Base__method__[fg]__sig` exact prefix
            bool starts = nm.size() > base.size() &&
                          nm.compare(0, base.size(), base) == 0 &&
                          (nm.compare(base.size(), 5, "__f__") == 0 ||
                           nm.compare(base.size(), 5, "__g__") == 0);
            if (starts) return true;
            // Check `pkg.Base__method[__[fg]__sig]` — pkg may have inner dots,
            // so split at the LAST dot (boundary between pkg and bare name).
            auto dot = nm.rfind('.');
            if (dot != std::string_view::npos) {
                std::string_view rest = nm.substr(dot + 1);
                if (rest == base) return true;
                if (rest.size() > base.size() &&
                    rest.compare(0, base.size(), base) == 0 &&
                    (rest.compare(base.size(), 5, "__f__") == 0 ||
                     rest.compare(base.size(), 5, "__g__") == 0))
                    return true;
            }
            return false;
        };
        for (auto& sd : prog_->structs) {
            if (sd.name != bare_struct) continue;
            for (auto& mp : sd.methods) {
                if (!mp) continue;
                if (matches(mp->name)) return mp->name;
            }
        }
        for (auto& fn : prog_->functions) {
            if (!fn) continue;
            if (matches(fn->name)) return fn->name;
        }
        return base;
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
    // logos_to_mlir cache keyed by TypeRef offset. Same TypeRef
    // value appears in many fn signatures (e.g. `&self` across 50+
    // methods on the same struct); without the cache, make_fn_type
    // re-computes the MLIR Type for each occurrence. Offsets are
    // stable for the lifetime of a single mlir_gen invocation (the
    // LProgram's type_pool arena isn't mutated by mlir_gen).
    std::unordered_map<hermes::arena_offset_t, mlir::Type> logos_to_mlir_cache_;

    // Names already forward-declared in the current generate() pass.
    // Replaces `mod.lookupSymbol(name)` as a duplicate-declaration guard:
    // MLIR's SymbolTable cache is invalidated by every push_back so each
    // lookupSymbol walks the module afresh — O(n) per call, O(n²) total
    // across 3500+ symbols.
    std::unordered_set<std::string>                        declared_fn_names_;
    std::unordered_set<std::string>                    vararg_fns_;  // names of vararg extern fns

    // Per-function: variables holding &dyn Trait values (name → trait name).
    std::unordered_map<std::string, std::string>  var_dyn_trait_;
    // Function name → Logos-level parameter types (for dyn coercion at call sites).
    std::unordered_map<std::string, std::vector<TypeRef>> fn_param_types_;
    // Function name → per-param owning-Box<dyn> flag: the param collapsed to a
    // bare TraitObject but the callee owns+frees the heap handle, so the call
    // site must coerce the arg to a HEAP fat handle (heap=true).
    std::unordered_map<std::string, std::vector<bool>> fn_param_owning_box_dyn_;

    // Per-function state.
    std::unordered_map<std::string, mlir::Value>  scope_;
    std::unordered_set<std::string>               let_vars_;
    // Per-function: let-vars bound directly from a container accessor returning
    // `*const/*mut dyn` (e.g. `let p = map.get(&k);` → `*const Box<dyn>`). Such a
    // var holds a pointer-INTO-storage, so `*p` must LOAD the stored handle —
    // unlike a coerced `*const dyn` handle / param / field, where `*p` is a no-op.
    // The two are type-indistinguishable (both `Ptr<TraitObject>`); we track the
    // accessor-return provenance here. See gen_expr_kind(EDerefView).
    std::unordered_set<std::string>               dyn_ptr_to_handle_vars_;
    std::unordered_map<std::string, mlir::Type>   var_elem_types_;
    std::unordered_map<std::string, std::string>  var_struct_;
    std::unordered_map<std::string, mlir::Type>   var_subscript_;
    // Slice-typed variables/params (`&[T]` / `&mut [T]` — Kind::Slice). scope_
    // holds a pointer to the fat `{ptr, len}` descriptor, so indexed read/write
    // must GEP field 0 + load the data pointer before striding by element.
    // Maps name → element MLIR type (the GEP stride for the data array).
    std::unordered_map<std::string, mlir::Type>   var_slice_;
    std::unordered_set<std::string>              var_tuple_;
    std::unordered_set<std::string>              var_tagged_enum_;
    // Mutable tagged-enum variables use a pointer slot (alloca-of-ptr) for rebinding.
    // scope_[name] = ptr_slot alloca; reading loads the ptr; assigning stores new ptr.
    std::unordered_set<std::string>              var_tagged_enum_ptr_;
    // Local let-bound pointer variables (*mut T / *const T): maps name → pointee MLIR type.
    // Needed because scope_[name] is an alloca(ptr), so indexing requires a load first.
    std::unordered_map<std::string, mlir::Type>   var_local_ptrs_;
    // Names of fn parameters whose type is Ref/MutRef. `&p` for such a
    // param means "address of param storage" — we must spill the SSA
    // arg into an entry alloca and return the alloca. Without the
    // spill, `&p` returns p itself (the inner pointer), breaking
    // `&&mut T` chains (was B3-bg-03 / Sprint 6).
    std::unordered_set<std::string>               ref_param_names_;
    mlir::Type                                    cur_ret_type_;
    TypeRef                              cur_fn_ret_logos_type_ = nullptr;
    std::string                                   cur_fn_name_;
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

    // Memoize EHermesLit codegen by content. Identical blob bytes resolve to
    // the same rodata global so multiple accesses to the same const-value
    // expression (e.g. an associated constant) preserve pointer identity at
    // -O0, where LLVM's ConstantMerge is not running. Keyed by the final
    // rodata bytes (size prefix included for the static path), value is the
    // emitted global symbol name.
    std::unordered_map<std::string, std::string> hermes_lit_global_cache_;

    // "Trait::Type" → mangled method names in vtable slot order
    std::unordered_map<std::string, std::vector<std::string>> dyn_vtable_methods_;
    // "Trait::Type" → symbol name of the STATIC vtable global (emitted once,
    // `[N x ptr]` of method addresses). A `&dyn` coercion takes its address
    // instead of malloc'ing+filling a fresh vtable per coercion (the recurring
    // per-coercion vtable leak; Rust vtables are static).
    std::unordered_map<std::string, std::string> dyn_vtable_globals_;
    // (vtable global sym → ordered method symbols). build_inline_vtable emits a
    // zero-init `constant [N x ptr]` placeholder global; the real address-of-
    // method initializer is materialised AFTER func→llvm lowering (in
    // lower_and_emit_object, where the methods are `llvm.func` so addressof is
    // valid) → a true `.data.rel.ro`/`.rodata` static vtable. Carried to the
    // pipeline via the `logos.vtable_specs` module attribute set in generate().
    std::vector<std::pair<std::string, std::vector<std::string>>> dyn_vtable_specs_;
    // Trait name → its method names in vtable slot order, and whether the
    // trait has a blanket impl (`impl<T> Trait for T`). Used by
    // build_inline_vtable to synthesize a `<Concrete>__<method>` vtable on the
    // fly when a concrete type reaches `&dyn Trait` only through a blanket
    // (whose impl block registered the typevar target, not each concrete).
    std::unordered_map<std::string, std::vector<std::string>> trait_method_names_;
    std::unordered_set<std::string> blanket_traits_;

    // drop_in_place glue: every vtable's slot 0 is a `__drop_in_place__<type>`
    // function that runs the concrete type's FULL drop (Rust-faithful). Maps a
    // vtable type-name key → the emitted glue symbol (dedup; emitted once per
    // concrete type). Empty body for a non-droppable type — harmless no-op.
    std::unordered_map<std::string, std::string> dyn_drop_glue_;
    // Emit (once) the drop_in_place glue fn for concrete type `ty` keyed on
    // `type_name`; returns its symbol (always non-empty so it can fill slot 0).
    std::string emit_drop_in_place_glue(std::string_view type_name, TypeRef ty);

    // Closure env drop glue: per closure-id, a `__closure_drop__<id>(env_ptr)`
    // fn that drops each owned droppable capture (env field i+1) then, if the
    // env is heap-allocated (escaping closure), frees the env. Stored at env
    // field 0 and invoked when an OWNED closure value is dropped. Deduped per
    // closure-id.
    std::unordered_map<std::string, std::string> closure_drop_glue_;
    std::string emit_closure_drop_glue(
        const std::string& closure_id,
        mlir::Type cap_struct,
        const std::vector<std::string>& captures,
        const std::vector<TypeRef>& capture_types,
        const std::vector<bool>& capture_drops,
        bool heap_env);

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

    // Spill an aggregate value (struct/enum/array returned by value) to an
    // alloca. Used when passing such a value to a function that expects a
    // pointer.
    // A Call/MethodCall returning a Slice/str now yields the 16-byte {ptr,len}
    // fat pair BY VALUE (slice-return-by-value ABI, A3/A4 leak fix). Every
    // downstream slice consumer (s[i], .len, field stores, arg passing) expects
    // a pointer-to-{ptr,len}. Spill the value back to a stack slot and hand back
    // the slot address so the by-value→by-pointer transition is transparent.
    mlir::Value spill_slice_call_result(mlir::Value v, TypeRef ty) {
        if (!v || !ty || ty.kind() != LogosType::Kind::Slice) return v;
        if (mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
            return spill_to_alloca(v);
        return v;
    }

    mlir::Value spill_to_alloca(mlir::Value v) {
        auto t = v.getType();
        if (!mlir::isa<mlir::LLVM::LLVMStructType>(t) &&
            !mlir::isa<mlir::LLVM::LLVMArrayType>(t))
            return v;
        auto alloca = create_entry_alloca(t);
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

    // LLVM type used in fn-return position. Differs from logos_to_mlir
    // only for aggregate-by-value returns (Struct/ZonedStruct/Enum):
    // logos_to_mlir returns ptr_type for these (the "passed by ptr"
    // shorthand used at param/field/scope positions), but the actual
    // fn-def returns the literal LLVM struct value. Indirect calls
    // and closure-fn synthesis must use this struct type for the
    // return slot, otherwise the call gets typed `() -> ptr` while
    // the callee writes the full aggregate — silent corruption that
    // segfaults the next match on the result. See
    // [[baghunt-dyn-in-enum-payload]] for the originating fix.
    mlir::Type llvm_fn_ret_type(TypeRef ret_t) {
        if (!ret_t) return mlir::Type{};
        TypeRef rt{ret_t};
        if (rt.kind() == LogosType::Kind::Struct ||
            rt.kind() == LogosType::Kind::ZonedStruct) {
            auto sit = struct_types_.find(mlir_struct_key(rt));
            if (sit == struct_types_.end())
                sit = struct_types_.find(std::string(rt.struct_name()));
            if (sit != struct_types_.end()) return sit->second.llvm_type;
        }
        if (rt.kind() == LogosType::Kind::Enum) {
            if (auto* te = resolve_tagged_enum(std::string(rt.enum_name()), rt))
                return te->llvm_type;
        }
        // Trait-object value-fat-pair: return the 16-byte {data,vtable} pair BY
        // VALUE (mirrors how we'd return a slice's {ptr,len}). Without this a
        // `-> &dyn T` returned a single ptr and the callee had to malloc a
        // surviving fat slot (a leak). Covers bare `dyn`, `&dyn`/`&mut dyn`,
        // `*const dyn`/`*mut dyn`.
        // Only a BARE `dyn`/`&dyn`/`&mut dyn` (sema flattens these to a single
        // TraitObject node) returns by-value as the 16-byte fat pair. A
        // `Ref/MutRef<TraitObject>` (i.e. `&T` where T is itself `&dyn`, as in
        // `Vec<&dyn>::index -> &T`) is a genuine POINTER into storage — keep it
        // thin. Raw `*const/*mut dyn` likewise stays a thin handle.
        if (rt.kind() == LogosType::Kind::TraitObject)
            return dyn_llvm_type();
        // Slice/str fat-pair: return the 16-byte {ptr,len} BY VALUE (mirrors the
        // TraitObject fat-pair above). logos_to_mlir(Slice)=ptr, which forced
        // gen_return to malloc(16)+memcpy a surviving heap slot (a leak, A3/A4).
        // `str` IS Slice<u8> so it gets the same treatment. The caller spills the
        // returned value back to a stack slot (slices are consumed by-pointer).
        if (rt.kind() == LogosType::Kind::Slice)
            return slice_llvm_type();
        return logos_to_mlir(ret_t);
    }

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
    // MLIR struct keys carry the package prefix so same-named structs in
    // different packages don't alias at the LLVM struct-type level. The bare
    // `concrete_struct_name(t)` is reused for method-symbol mangling (which
    // is package-agnostic — see mono.cpp's "<Struct>__<method>" pattern) and
    // as a back-compat alias key in struct_types_.
    static std::string qualify_pkg(std::string_view pkg, std::string_view name) {
        if (pkg.empty()) return std::string(name);
        std::string r;
        r.reserve(pkg.size() + 1 + name.size());
        r.append(pkg); r.push_back('.'); r.append(name);
        return r;
    }
    static std::string_view strip_struct_pkg(std::string_view qualified) {
        // Inverse of qualify_pkg: split at the last '.'. Struct base names
        // never contain '.' in their mangled form, so this is unambiguous.
        auto p = qualified.rfind('.');
        if (p == std::string_view::npos) return qualified;
        return qualified.substr(p + 1);
    }
    std::string mlir_struct_key(TypeRef t) {
        if (!t) return {};
        auto base = concrete_struct_name(t);
        return qualify_pkg(t.pkg_name(), base);
    }
    bool register_struct(const LStructDef& sd);
    void register_tagged_enum(const LEnumDef& ed);
    uint64_t variant_payload_bytes(const LVariant& v);

    // ── Unified in-memory layout ─────────────────────────────────────────────
    // THE single source of truth for the {size, alignment} of any Logos type,
    // matching the non-packed LLVM aggregate layout codegen emits. sizeof /
    // alignof / enum payload bytes / variant footprint / DST field offsets /
    // inline-copy strides all DERIVE from this — add a type kind to the one
    // switch and every size/align query follows.
public:
    struct Layout { uint64_t size = 0; uint64_t align = 1; };
private:
    Layout layout_of(TypeRef t, std::unordered_set<std::string>& seen);
    Layout layout_of(TypeRef t) { std::unordered_set<std::string> s; return layout_of(t, s); }
    // {size,align} of a type as an AGGREGATE MEMBER (struct field / tuple
    // element / enum payload field). Mirrors register_struct / tuple_llvm_type /
    // variant_payload_struct: Slice/Closure/Tuple members are stored as an
    // 8-byte POINTER (logos_to_mlir → ptr), NOT their by-value footprint;
    // struct/enum/array/bare-dyn members are inline (layout_of); AnyVal is i32.
    Layout aggregate_member_layout(TypeRef m, std::unordered_set<std::string>& seen);
    // {size, align} of one variant's payload (struct/tuple of its fields) —
    // both the enum's payload_bytes and payload_align derive from this.
    Layout variant_payload_layout(const LVariant& v);

    // Byte size (= layout_of(t).size). Thin wrapper kept for existing callers.
    uint64_t logos_abi_byte_size(TypeRef t,
                                  std::unordered_set<std::string>& seen) {
        return layout_of(t, seen).size;
    }

    // i64 byte-size CONSTANT of a Logos type, from the unified layout. Use this
    // for value-copy memcpy sizes instead of `mlir::DataLayout::getTypeSize` —
    // at mlir-gen time the module has no target datalayout, so MLIR's DataLayout
    // PACKS aggregates (drops inter-field padding) and under-copies (e.g. an
    // {i32, i64}/enum payload-at-offset-8 copied as 12 bytes loses 4 bytes).
    mlir::Value size_const(TypeRef t) {
        return builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(),
            builder_.getI64IntegerAttr((int64_t)layout_of(t).size));
    }

    // Resolve a tagged enum name from the expression type (handles generic enums).
    const TaggedEnumInfo* resolve_tagged_enum(const std::string& name, TypeRef type);

    // Build the LLVM struct type for a variant's payload, using the INLINE
    // aggregate type (not the collapsed `ptr`) for struct/tuple payload fields.
    // The constructor memcpy's aggregate payload fields inline (their full ABI
    // byte size), so the payload struct type used for field GEPs must reserve
    // the same inline footprint — otherwise a field after an aggregate (e.g.
    // the `z` in `C(T2, i64)`) lands at the wrong offset and aliases the
    // aggregate's bytes. Pass `vp.field_types` collapsed for scalar fields.
    mlir::LLVM::LLVMStructType variant_payload_struct(
        const TaggedEnumInfo::VariantPayload& vp);

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

    // Trait-object fat pair: { data_ptr, vtable_ptr }, 16-byte value-repr.
    mlir::Type dyn_llvm_type();

    // ── Vtable / dyn ─────────────────────────────────────────────
    void emit_trait_vtables(mlir::ModuleOp mod, const LProgram& prog);
    void emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog);
    mlir::Value build_inline_vtable(std::string_view trait_name,
                                     std::string_view type_name,
                                     TypeRef concrete_ty = {});
    // Build a fat {data,vtable} pair. `heap=false` (default) → stack alloca:
    // used for borrow `&dyn`/`&mut dyn` (value-fat-pair model; no leak). The
    // CONSUMER copies the 16 bytes when it escapes (struct field / array /
    // Vec<&dyn> / by-value return). `heap=true` → malloc(16): used for OWNING
    // `Box<dyn>` and raw `*const/*mut dyn` handles, whose single-word handle is
    // stored/escapes (Vec<Box<dyn>>, persistent NodeARC.p) and survives via the
    // heap slot (Box<dyn>'s drop frees it).
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                               std::string_view src_type_name, bool heap = false,
                               TypeRef concrete_ty = {});
    // G168-A: unsize-coerce a concrete `Box<Concrete>` / `&Concrete` / struct
    // value into a fat `{data,vtable}` handle when the destination SLOT is a
    // trait object (`dyn`/`Box<dyn>`/`&dyn`) but the VALUE is still concrete —
    // e.g. an enum-variant payload typed `Box<dyn>` constructed from a
    // `Box<Concrete>`. No-op when not applicable or already a trait object.
    mlir::Value coerce_value_to_dyn_if_needed(mlir::Value val, TypeRef slot_lt,
                                              TypeRef val_lt, bool force_heap = false);
    mlir::Value gen_dyn_dispatch(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    mlir::Value gen_tagged_dispatch(lir_view::EMethodCallView v, TypeRef ret_logos_type);

    // ── malloc / free helpers ─────────────────────────────────────
    void ensure_malloc_free(mlir::ModuleOp mod);
    mlir::Value call_malloc(mlir::Value size);
    void call_free(mlir::Value ptr);
    mlir::Value sizeof_struct(mlir::LLVM::LLVMStructType struct_type);

    // Cheap allocation-free replacement for `type_str(t) == "AnyVal"`.
    // The old form materialised an std::string for every check; this fires
    // in hot paths (make_fn_type, logos_to_mlir) where stdlib's 3500+
    // forward_declare calls each do several checks per fn. AnyVal is
    // declared `#[zoned] struct` so its kind is ZonedStruct, not Struct —
    // the type_str-based check matched both because type_str renders the
    // struct_name for both kinds.
    static bool is_anyval(TypeRef t) noexcept {
        if (!t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        return t.struct_name() == "AnyVal";
    }
    // FQN-checked stdlib `logos.mem.boxed.Box<T>` (not a user struct named Box).
    // See the sema-side twin for rationale. pkg tolerated empty for internal
    // paths (mlir-gen sometimes strips struct pkg); a user Box keeps its own.
    static bool is_stdlib_box(TypeRef t) noexcept {
        return is_stdlib_smart_ptr(t, "Box", "logos.mem.boxed");
    }
    // NOTE: `Box<dyn Trait>` does NOT appear as a Box<TraitObject> struct in
    // mlir-gen — sema collapses it to an OWNING bare `TraitObject` (16-byte
    // {data,vtable} fat pair, IDENTICAL repr to `&dyn`; differs only by ownership
    // → drop calls vtable[0] then deallocs `data`). So all dyn representation /
    // dispatch / stride code keys uniformly on Kind::TraitObject; no Box special-
    // casing is needed here.
    // FQN-checked stdlib smart-pointer struct (name + package; pkg tolerated
    // empty for internal paths where it was stripped).
    static bool is_stdlib_smart_ptr(TypeRef t, std::string_view name,
                                    std::string_view pkg) noexcept {
        if (!t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        if (t.struct_name() != name) return false;
        auto p = t.pkg_name();
        return p.empty() || p == pkg;
    }
    // Smart-pointer kind of a CONCRETE Rc<T>/Arc<T>/Box<T> struct value (the
    // SOURCE of a `as Rc<dyn>` unsize cast), or Borrow if not a smart pointer.
    static TypeRef::OwningKind stdlib_smart_ptr_kind(TypeRef t) noexcept {
        if (is_stdlib_smart_ptr(t, "Box", "logos.mem.boxed")) return TypeRef::OwningKind::Box;
        if (is_stdlib_smart_ptr(t, "Rc",  "logos.mem.rc"))    return TypeRef::OwningKind::Rc;
        if (is_stdlib_smart_ptr(t, "Arc", "logos.mem.sync"))  return TypeRef::OwningKind::Arc;
        return TypeRef::OwningKind::Borrow;
    }

    // ── Function type from LFunction ─────────────────────────────
    mlir::FunctionType make_fn_type(const LFunction& fn);
    // When `is_binary_skip` is true, the FuncOp is created private so the
    // module ends up with a declaration-only entry (no body, matching the
    // archive-resident implementation).
    void forward_declare(mlir::ModuleOp mod, const LFunction& fn,
                          bool is_binary_skip = false);
    bool gen_function_body(mlir::func::FuncOp func, const LFunction& fn);

    // ── Block ─────────────────────────────────────────────────────
    // Stage 3g.3: BlockRef / StmtRef in signatures so the dispatcher no
    // longer round-trips offset → C++ block ptr → offset via lblock_of.
    void gen_block(lir_view::BlockRef block);

    // ── Statements ────────────────────────────────────────────────
    void gen_stmt(lir_view::StmtRef stmt);

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
    void gen_stmt_kind(lir_view::SForEachView v);
    void gen_stmt_kind(lir_view::SBlockView v);
    void gen_stmt_kind(lir_view::SDropView v);
    // G158-4: recursively drop a value of type `ty` located at `value_ptr`
    // (struct → user drop + fields; tuple → elements; enum → variant-switched
    // payload; array → each element; ref/ptr/scalar → nothing). Handles
    // arbitrary nesting (array-of-struct, struct-with-array-field, …).
    // `top_level=true` mirrors SDrop's owner semantics: after a user `impl
    // Drop` runs, the value's FIELDS/payload are ALSO dropped (the owner drops
    // both). `top_level=false` (nested) calls the user drop and stops (the
    // by-value `self` consumes its own fields at the drop body's scope end).
    void gen_drop_value(mlir::Value value_ptr, TypeRef ty, bool top_level = false);
    // Drop an OWNING `Box<dyn Trait>` whose binding storage `handle` IS the
    // 8-byte heap handle to a 16-byte {data,vtable} fat pair. Sequence (null-
    // guarded): load data(field0)+vtable(field1); call vtable[0](data)
    // (drop_in_place runs the concrete's destructor + its owned fields);
    // free(data) (the boxed concrete); free(handle) (the fat slot).
    void gen_drop_owning_dyn_handle(mlir::Value handle, TypeRef::OwningKind kind);
    // Rc<dyn>/Arc<dyn>.clone(): bump strong (at data − round_up(4, vtable.align);
    // atomic for Arc) and return a copy of the {data,vtable} fat pair.
    mlir::Value gen_clone_owning_dyn(const LExpr* recv_le, TypeRef recv_t);
    // Codegen-side "does a value of this type own anything droppable" — mirrors
    // sema's has_droppable_fields; gates gen_drop_value recursion to avoid empty
    // GEP/loop emission for non-droppable members.
    bool value_needs_drop(TypeRef ty);
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
    void gen_match(lir_view::SMatchView v);

    // ── Expressions ───────────────────────────────────────────────
    mlir::Value gen_expr(const LExpr& e);
    mlir::Value gen_expr(lir_view::ExprRef er);

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
    // True when `*operand` over a `*const/*mut dyn` is a genuine pointer-INTO-
    // storage (a container accessor return, e.g. `HashMap::get → *const Box<dyn>`)
    // and must LOAD the stored handle — as opposed to the default, where the
    // value already IS the dyn handle (the raw fat pointer) and `*p` is a no-op.
    bool deref_operand_is_ptr_to_dyn_handle(const LExpr& operand);
    mlir::Value gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EFieldReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EIndexReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EStructLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EArrLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ECastView v, TypeRef type);
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
    mlir::Value gen_expr_kind(lir_view::EAlignOfView v, TypeRef);
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

    // Bind the payload of a tagged-enum VariantData pattern given a pointer to
    // the enum struct (`enum_ptr`) and its TaggedEnumInfo. Handles scalar,
    // inline-struct, ref-bind, and trait-object payload bindings. Used by the
    // tuple-element recursion in match payload extraction (a tuple whose
    // element is an enum, e.g. `(E::A(x), F::B(y))`); the bound names are
    // appended to `added`. Shared by the statement and expression match paths.
    void bind_enum_payload(mlir::Value enum_ptr,
                           const TaggedEnumInfo* te,
                           lir_view::PatVariantDataView pvd,
                           std::vector<std::string>& added,
                           const std::unordered_map<std::string, mlir::Value>* shared = nullptr);

    // Recursive pattern matcher for arbitrarily nested patterns (a tuple
    // element that is itself a tuple / variant / or-pattern). `slot_ptr` points
    // to the value's storage (for an enum value the slot holds the heap ptr;
    // pat_test/pat_bind load it). pat_test returns a pure i1 (no control flow —
    // an or-pattern is the OR of its alts' tests). pat_bind binds names into
    // scope_; for an or-pattern it dispatches per-alt and binds into the
    // pre-created `shared` allocas (name→alloca) so the join sees one slot.
    mlir::Value pat_test(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty);
    void        pat_bind(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty,
                         const std::unordered_map<std::string, mlir::Value>* shared = nullptr);
    // Collect (name,type) pairs a pattern binds (first-alt for or). Used to
    // pre-create shared allocas for an or-pattern's bindings.
    void collect_pat_bindings(lir_view::PatRef pat, TypeRef ty,
                              std::vector<std::pair<std::string, TypeRef>>& out);

    // ── Array helpers ─────────────────────────────────────────────
    mlir::Value get_subscript_ptr(const std::string& name);
    mlir::Type subscript_elem_type(const std::string& name);
    // G163-2: recursively compute the ADDRESS of an lvalue place expression
    // (VarRef / IndexRead / FieldRead / TupleIndex / Deref chain) — the real
    // storage address, not a value copy. Returns null for shapes it can't
    // address (callers must treat null as "not a place"). Used by the general
    // place-write (`a[i][j] = v`, `(*p).0 = v`, deep mixes) and `&mut <place>`.
    mlir::Value gen_lvalue_addr(lir_view::ExprRef e);
    // MLIR slot type for one element/field of a place (struct/tuple inline
    // aggregate type, else logos_to_mlir) — the GEP stride into an aggregate.
    mlir::Type place_slot_type(TypeRef t);
    mlir::Value gen_arr_lit(lir_view::EArrLitView v, mlir::Type elem_type,
                            TypeRef logos_elem = TypeRef(nullptr));

    // ── format() built-in ─────────────────────────────────────────
    static int format_type_tag(TypeRef t) noexcept;
};

} // namespace logos::compiler
