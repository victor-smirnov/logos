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

    // F3 (ref-repr-design §8): `#[zoned2]` on the enum. The Ref (low-bit-0)
    // arm of this niche enum is stored SELF-RELATIVE at-rest and absolute as a
    // value — the storage/compute split, bridged by zoned_enum_materialize /
    // zoned_enum_lower (the generalized ha_materialize/ha_lower). Only
    // meaningful together with a LowBit niche.
    bool zoned = false;

    // Phase 3.5 niche optimization. When `packed`, the enum has NO separate
    // discriminant: it is laid out as just its payload (`llvm_type` is the
    // niche field), and the discriminant is encoded in an invalid bit-pattern
    // of that field. MVP = the null-pointer niche for an `Option`-shape enum
    // (2 variants: one fieldless = `none_disc`, one single non-null pointer
    // field = `some_disc`); the niche value is null (0) at offset 0, so
    // sizeof(Option<&T>) == sizeof(&T) == 8, matching Rust.
    struct Niche {
        // NullPtr: Option<&T>-shape — null encodes `none_disc`, the non-null ptr
        //          encodes `some_disc`. Stored as just the pointer (8B).
        // LowBit:  HAny-shape — two data arms disambiguated by the payload word's
        //          LOW BIT. The `ptr_disc` arm holds a pointer to an align≥2 pointee
        //          (so its low bit is always 0) stored RAW; the `val_disc` arm holds
        //          a ≤63-bit integer stored as `(value << 1) | 1` (low bit 1). Read:
        //          low bit 0 → ptr arm (word as ptr), low bit 1 → val arm (word >> 1).
        enum Kind { NoNiche, NullPtr, LowBit };
        Kind    kind      = NoNiche;
        bool    packed    = false;       // kind != NoNiche (enum is just its payload word)
        int64_t none_disc = 0;           // NullPtr: the null variant
        int64_t some_disc = 0;           // NullPtr: the pointer variant
        int64_t ptr_disc  = 0;           // LowBit: the low-bit-0 pointer arm
        int64_t val_disc  = 0;           // LowBit: the low-bit-1 value arm
        uint32_t val_bits = 0;           // LowBit: value arm's bit width (for read sign/zero-extend)
        bool     val_signed = false;     // LowBit: value arm signedness
        // LowBit RAW mode (`#[zoned2]` + a 64-bit val arm, e.g. HAny's Pod(u64)):
        // the val arm word is stored/read VERBATIM — no `(v<<1)|1` shift — because
        // the producer already bakes the low-bit-1 tag into it. Pod = the raw word,
        // Ref = low-bit-0 pointer. The disc is still the low bit.
        bool     val_raw  = false;
    };
    Niche niche;
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

    // Resolve an ExprRef back to its variant LExpr* via the mirror's reverse
    // map. Used inside view-handlers to recurse through gen_expr() on
    // sub-expressions while the rest of the dispatcher still walks variants.
    lir_view::StmtRef stmt_ref_of(const LStmt& s) const noexcept {
        if (!prog_ || s.mirror_ptr_ == nullptr) return {};
        return lir_view::StmtRef(prog_->type_pool.arena(), s.mirror_ptr_);
    }
    lir_view::PatRef pat_ref_of(const Pattern& p) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->pat.find(&p);
        if (it == mirror_->pat.end()) return {};
        return lir_view::PatRef(prog_->type_pool.arena(), it->second);
    }
    lir_view::BlockRef block_ref_of(const LBlock& b) const noexcept {
        if (!prog_ || b.mirror_ptr_ == nullptr) return {};
        return lir_view::BlockRef(prog_->type_pool.arena(), b.mirror_ptr_);
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
    // B8 dynamic drop flags: a `let mut x: T;` declared WITHOUT an initializer
    // gets a hidden i8 flag (0 = slot empty, 1 = holds a live value), like
    // Rust's drop flags. Each assignment drops the OLD value only if the flag
    // is set, then sets it; scope-exit/return drops only if set. This gives
    // exact drop semantics for conditionally-initialized vars (`let mut x; if c
    // { x = a; } x = b;` drops `a` iff c was true) that no static analysis can
    // resolve. name → flag alloca.
    std::unordered_map<std::string, mlir::Value>  uninit_drop_flag_;
    // B8 drop elaboration (Rust-style): a declared-uninit var needs a RUNTIME
    // drop flag ONLY if its init state is not statically determinable — i.e. it
    // has an assignment nested inside a conditional/loop (deeper than its decl).
    // Determined by a pre-scan of the fn body (prescan_uninit_flags). Vars whose
    // every assignment statically dominates (straight-line) are flag-FREE: drops
    // are placed statically via the `assigned` set tracked during codegen
    // (uninit_static_ = needs static tracking, uninit_assigned_ = currently holds
    // a live value at this codegen point). This elides the flag + branch for the
    // common straight-line case, matching Rust's MIR drop elaboration.
    std::unordered_set<std::string>               uninit_flag_needed_;
    std::unordered_set<std::string>               uninit_static_;
    std::unordered_set<std::string>               uninit_assigned_;
    void prescan_uninit_flags(lir_view::BlockRef block, int depth,
                              std::unordered_map<std::string, int>& decl_depth);
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
    // Pointer-family params (`*mut`/`*const`/`&`/`&mut`) bound as SSA args. Their
    // arg IS a pointer value, so `&p` is the address of the param's own slot →
    // EAddrOf must spill. (Scalars are caught in EAddrOf by an SSA-type check;
    // aggregate by-value params arrive AS a pointer = the object address and are
    // NOT here, so `&p` returns that address unchanged.) Together these unify the
    // EAddrOf `&p` rule into one condition "the SSA arg holds a value".
    std::unordered_set<std::string>               ptr_family_param_;
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
    // Trait name → ordered transitive supertraits (LTraitDef.upcast_supertraits,
    // single-sourced by sema). Drives the stored super-vtable-pointer slots that
    // each `dyn Trait` vtable carries after its method slots, and the upcast
    // `&dyn Sub → &dyn Super` index. Empty for a trait with no supertraits (so
    // its vtable layout is unchanged).
    std::unordered_map<std::string, std::vector<std::string>> trait_upcast_supers_;

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
        // RFC-2229 phase-2: per-capture narrow FIELD type (null = whole-root).
        // Drop-glue drops the FIELD value when set (only the narrow piece the
        // env actually owns), not the root.
        const std::vector<TypeRef>& capture_field_types,
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
        // A by-value 16B fat return (a Slice, or a fat `&mut` zone reference) comes
        // back as an LLVM struct value; spill it to an alloca so the consumer sees
        // the usual ptr-to-{data,meta} pair (repr_data/repr_meta gep). Without this
        // a returned fat ref is a struct value and field access geps it directly.
        if (!v || !ty) return v;
        bool fat_returnable = (ty.kind() == LogosType::Kind::Slice) ||
                              (ref_repr_of(ty) == RefReprKind::FatZoneMut);
        // A tagged/niche enum is RETURNED by value (an aggregate; mlir_gen_fn.cpp),
        // but its value-repr is by-POINTER (logos_to_mlir(Enum) == ptr). Spill the
        // aggregate result to a slot so consumers (method `self`, match scrutinee,
        // field/disc access) see a pointer — otherwise a by-value enum call result
        // is used as if it were a pointer (e.g. `f().method()` → `*(self …)`),
        // emitting `llvm.load(aggregate)`. (This is the niche-enum-byvalue bug.)
        bool enum_returnable = ty.kind() == LogosType::Kind::Enum &&
                               resolve_tagged_enum(std::string(ty.enum_name()), ty) != nullptr;
        if (!fat_returnable && !enum_returnable) return v;
        if (mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()) ||
            mlir::isa<mlir::LLVM::LLVMArrayType>(v.getType()))
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
    // Module system (symbol-mangle rewrite, emission boundary): qualified LINK
    // symbol of a def (methods gain `<module>..`; free fns unchanged).
    std::string link_name(const LFunction& fn) const {
        if (!prog_) return fn.name;
        return sym::link_name(fn, prog_->pkg_module_ids);
    }
    // Qualify a (bare-module) method-CALL callee STRING to its exact link symbol
    // (full signature preserved) — used by the canonical() bridge before its
    // signature-stripping (ambiguous) fallback. Method shape: part before the
    // first `__` carries a `.`; free fns use `$` / already `..` → unchanged.
    // THE callee-resolution chokepoint (defined in mlir_gen_expr.cpp). Resolves
    // a callee symbol to its FuncOp across the bare↔module-qualified and
    // sig-stripped forms the LIR/mono/bridge produce.
    mlir::func::FuncOp find_func_op(mlir::ModuleOp mod,
                                    std::string_view name) const;
    // Memoised SUCCESSFUL resolutions (callee string → FuncOp). FuncOp defs are
    // stable once created (the bridge renames call sites, never defs), so caching
    // hits is sound. Amortises the per-call bare-miss→link_name_str→qualified-hit
    // work for popular callees (push/get/deref) on codegen-heavy bodies. Misses
    // are NOT cached (a name may resolve once a later forward-decl is emitted).
    mutable std::unordered_map<std::string, mlir::func::FuncOp> find_func_op_cache_;

    // find_func_op's canonical-match index (see find_func_op). Maps each def's
    // canonical key → its FuncOp; ambiguous keys (shared by >1 def) live in the
    // set and resolve to nothing. Rebuilt lazily when the module's FuncOp count
    // changes (stable during body-gen, so built once). Replaces a per-call O(n)
    // canonicalising walk.
    mutable std::unordered_map<std::string, mlir::func::FuncOp> ffo_canon_index_;
    mutable std::unordered_set<std::string> ffo_canon_ambig_;
    mutable long ffo_canon_built_n_ = -1;
    void ensure_ffo_canon_index(mlir::ModuleOp mod) const;

    std::string link_name_str(const std::string& callee) const {
        if (!prog_ || callee.find("..") != std::string::npos) return callee;
        auto us = callee.find("__");
        if (us == std::string::npos) return callee;
        auto dot = callee.rfind('.', us);
        if (dot == std::string::npos) return callee;
        std::string pkg = callee.substr(0, dot);
        auto it = prog_->pkg_module_ids.find(pkg);
        if (it == prog_->pkg_module_ids.end() || it->second.empty()) return callee;
        return it->second + ".." + callee;
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

    // ── RefRepr — reference-representation registry (Phase 0 scaffold) ────────
    // Consolidates the ~50 per-kind switches that hardcode how a reference-like
    // type is laid out (storage) and manipulated (compute). Phase 0 reproduces
    // CURRENT behavior and is NOT YET ROUTED into the codegen sites (dead code);
    // later phases migrate the sites to dispatch through these descriptors.
    // See docs/internals/ref-repr-design.md.
    enum class RefReprKind {
        NotARef,        // not a reference-like type
        ThinPtr,        // *T / &T / &mut T / fn-ptr — 8B thin pointer
        FatSlice,       // &[T] (Slice) — {data,len} 16B
        FatDyn,         // &dyn / TraitObject — {data,vtable} 16B
        FatClosure,     // closure — {fn,env} 16B
        FatCustomDst,   // &CustomDst (DstRef) — {data,meta} 16B
        FatZoneMut,     // &mut T for a #[zone_mut] type — {data, zone=*mut Allocator}
                        // 16B, returned by value (like FatSlice). The zone (allocator)
                        // rides the &mut so grow methods reach it from &mut self.
        RelOffset,      // self-relative pointer — storage = i64 byte offset from
                        // the slot's own address; compute = absolute thin ptr.
                        // materialize = slot + load_i64(slot); lower = store(slot,
                        // target − slot). The hermes2 / #[rel_ptr] zoned pointer.
    };
    // Classify a reference-like TypeRef into its repr kind (NotARef otherwise).
    RefReprKind ref_repr_of(TypeRef t);
    // The EFFECTIVE repr of a struct field: like ref_repr_of(field_type), but a
    // thin pointer field inside a #[zoned2] struct stores SELF-RELATIVE (RelOffset)
    // — the untagged zoned-reference case (ref-repr-design §6). `owner_key` is the
    // field's containing-struct key (all_struct_defs_ key).
    RefReprKind field_repr(const std::string& owner_key, TypeRef field_type);

    // Recover the tail length of a #[self_describing] DST from its THIN header
    // pointer by calling the struct's `dst_len(*const Self)` with `thin_ptr`.
    // The in-band metadata of a thin self_describing DstRef (whose physical value
    // IS the header pointer); the thin counterpart of repr_meta. Returns an i64.
    // `dstref_t` is the DstRef type (carries the struct name). ref-repr §6.
    mlir::Value emit_dst_len(mlir::Value thin_ptr, TypeRef dstref_t);

    // `ref v` / `ref mut v` pattern-binding classifier — single source for the
    // (formerly three) enum-payload binding loops. Given the SEMA-assigned
    // binding type (payload wrapped in N Ref layers) and the payload's own
    // type, returns true when the binding should bind the slot ADDRESS (a
    // borrow), and sets `added_depth` = indirection layers ADDED by `ref` on
    // top of the payload's own THIN-ref layers. Fat-ref payloads
    // (`&dyn`/`&[T]`/`&str`/closure) return false so their dedicated inline-
    // fat handlers own the 16-byte layout. See mlir_gen_stmt.cpp.
    bool ref_bind_kind(TypeRef binding_type, TypeRef payload_type,
                       int& added_depth);
    // Compute representation (the SSA value type). Today uniformly a thin pointer
    // (the fat pair lives in storage; the value is a pointer to it).
    mlir::Type  repr_value_type(RefReprKind k);
    // Storage representation (the in-field / in-element slot LLVM type).
    mlir::Type  repr_storage_type(RefReprKind k);
    // Storage {size, align}.
    Layout      repr_storage_layout(RefReprKind k);
    // By-VALUE return ABI of a reference. A separate axis from storage_type:
    // dyn/slice fat pairs are returned by value as their 16B storage (the
    // A3/A4 slice/dyn-return-by-value leak fix — a ptr-to-local would dangle),
    // whereas closures and custom-DST refs are returned as their 8B by-pointer
    // value (storage owned by the callee escape path / caller slot, not
    // materialized in the return). Thin refs return their 8B value. NotARef →
    // nullptr (caller falls through to logos_to_mlir for non-reference returns).
    mlir::Type  repr_return_type(RefReprKind k);
    // Extract the data / metadata half of a reference value (compute form).
    // Thin: data = the value itself, no metadata (meta returns null). Fat: load
    // field 0 / field 1 of the {data, meta} pair the value points at (mirrors
    // slice_ptr / slice_len).
    mlir::Value repr_data(RefReprKind k, mlir::Value v);
    mlir::Value repr_meta(RefReprKind k, mlir::Value v);
    // Construct a reference value from its data + metadata halves (from_raw_parts).
    // Thin: the value IS the data pointer. Fat: spill a {data, meta} pair to an
    // alloca and return its address (meta stored as vtable-ptr for FatDyn, else
    // an i64 length). Mirrors slice_lit.
    mlir::Value repr_construct(RefReprKind k, mlir::Value data, mlir::Value meta);

    // STORAGE <-> COMPUTE conversion — the heart of the storage/compute split,
    // made first-class so a future repr (a self-relative `zoned T`, whose
    // conversion is offset±anchor, not identity) plugs in as just a new pair of
    // these. Today every repr's conversion is trivial:
    //   materialize(slot)  — storage slot  -> compute value. Thin: load the 8B
    //     ptr. Fat (always-16B pair): the value IS the storage address (the
    //     by-pointer fat value convention) -> return slot. NOTE: FatDyn shares
    //     the 16B form but some sites carry a dyn value as a by-value aggregate
    //     instead of a slot address; those sites keep their own handling and do
    //     NOT route here (the dyn value-convention is context-dependent).
    //   lower(val, slot)   — compute value -> storage slot. Thin: store the 8B
    //     ptr. Fat: memcpy the 16B pair (val is a ptr to the source pair).
    mlir::Value repr_materialize(RefReprKind k, mlir::Value slot);
    void        repr_lower(RefReprKind k, mlir::Value val, mlir::Value slot);

    // F3 (§8): storage↔compute bridge for a `#[zoned2]` niche enum (the
    // compiler-owned ha_materialize/ha_lower). `slot` is the at-rest word
    // (Ref arm self-relative, anchor = slot); the value is a by-pointer enum
    // (ptr to a fresh alloca holding the word with the Ref arm absolute).
    mlir::Value zoned_enum_materialize(mlir::Value slot);
    void        zoned_enum_lower(mlir::Value val, mlir::Value slot);
    // True iff `t` is a tagged enum carrying the `#[zoned2]` (zoned) marker —
    // i.e. its Ref arm is stored self-relative at-rest. Returns the TaggedEnumInfo
    // (or nullptr) so callers can reuse it.
    const TaggedEnumInfo* zoned_niche_enum_info(TypeRef t);

    // True iff `t` is a DstRef whose pointee struct has a literal `[T]` slice
    // tail — i.e. a GENUINELY 16-byte {data,len} fat ref (the len is carried
    // inline). A `dyn`-tail DstRef (`&RcInner<dyn>`) is physically thin (8-byte;
    // the vtable lives in the heap object) and returns false. Discriminates the
    // 16B-fat custom-DST ref from the thin dyn-tail/escape handle at the
    // store/copy sites. Looks the pointee struct up in all_struct_defs_ and
    // checks the last field's kind (Slice / UnsizedSlice).
    bool dstref_has_slice_tail(TypeRef t);

    // True iff `t` is a DstRef to a #[self_describing] DST — PHYSICALLY THIN (8B,
    // pointer straight to the header; tail length recovered in-band via dst_len).
    // The discriminator routing data/len-extraction + repr/store/access onto the
    // thin path. See docs/internals/self-describing-dst-thin-ref.md.
    bool dstref_pointee_self_describing(TypeRef t);

    // Enum representation access — the SINGLE chokepoint for the tagged-enum
    // memory layout, so niche-packing (Phase 3.5) becomes a localized change
    // rather than an edit across every construct/match/drop site. Today every
    // tagged enum is `{ i32 disc @field0, payload @field1 }` and these just GEP
    // those fields; a niche-packed enum will instead encode/decode the
    // discriminant in an invalid bit-pattern of the payload here.
    //   enum_payload_ptr — address of the payload area for `enum_addr`.
    //   enum_store_disc  — write the discriminant for variant `disc`.
    //   enum_load_disc   — read the discriminant as an i32 value.
    mlir::Value enum_payload_ptr(mlir::Value enum_addr, const TaggedEnumInfo& info);
    void        enum_store_disc(mlir::Value enum_addr, const TaggedEnumInfo& info,
                                int64_t disc);
    // Store a RUNTIME discriminant value (not a compile-time constant) — used by
    // the "untyped None reassign" paths where the disc is an i32 SSA value. For
    // a niche-packed enum this has no statically-known variant, so it is only
    // valid on a non-niche `{disc,payload}` enum (asserted there).
    void        enum_store_disc_value(mlir::Value enum_addr, const TaggedEnumInfo& info,
                                      mlir::Value disc_val);
    mlir::Value enum_load_disc(mlir::Value enum_addr, const TaggedEnumInfo& info);

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
    // §6.2 statics (S25): emit one llvm.mlir.global per `static [mut]` item
    // and a @__logos_static_init that runs each initializer into its global
    // address at program startup (called from main's prologue, Pass 3).
    void emit_static_globals(mlir::ModuleOp mod, const LProgram& prog);
    bool has_static_init_ = false;  // set by emit_static_globals if any non-
                                    // extern static needs runtime init
    mlir::Value build_inline_vtable(std::string_view trait_name,
                                     std::string_view type_name,
                                     TypeRef concrete_ty = {});
    // Ensure the `[N x ptr]` vtable global for (trait, type) exists (placeholder
    // + recorded spec) and return its symbol; "" if no methods are registered.
    // build_inline_vtable = ensure_vtable_global + AddressOf. Recurses to build
    // each supertrait's vtable global for the stored super-vtable-pointer slots.
    std::string ensure_vtable_global(std::string_view trait_name,
                                     std::string_view type_name,
                                     TypeRef concrete_ty);
    // Build a fat {data,vtable} pair. `heap=false` (default) → stack alloca:
    // used for borrow `&dyn`/`&mut dyn` (value-fat-pair model; no leak). The
    // CONSUMER copies the 16 bytes when it escapes (struct field / array /
    // Vec<&dyn> / by-value return). `heap=true` → malloc(16): used for OWNING
    // `Box<dyn>` and raw `*const/*mut dyn` handles, whose single-word handle is
    // stored/escapes (Vec<Box<dyn>>, persistent NodeARC.p) and survives via the
    // heap slot (Box<dyn>'s drop frees it).
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                               std::string_view src_type_name,
                               TypeRef concrete_ty = {});
    // G168-A: unsize-coerce a concrete `Box<Concrete>` / `&Concrete` / struct
    // value into a fat `{data,vtable}` handle when the destination SLOT is a
    // trait object (`dyn`/`Box<dyn>`/`&dyn`) but the VALUE is still concrete —
    // e.g. an enum-variant payload typed `Box<dyn>` constructed from a
    // `Box<Concrete>`. No-op when not applicable or already a trait object.
    mlir::Value coerce_value_to_dyn_if_needed(mlir::Value val, TypeRef slot_lt,
                                              TypeRef val_lt);
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
    // skip_paths (T1-10/B78): dotted paths RELATIVE to this value whose
    // sub-values were moved out — an exact segment match skips that
    // child entirely; a deeper path recurses with the stripped remainder
    // so only the moved leaf is suppressed and its siblings still drop.
    void gen_drop_value(mlir::Value value_ptr, TypeRef ty, bool top_level = false,
                        const std::set<std::string>* skip_paths = nullptr);
    // Drop an OWNING `Box<dyn Trait>` whose binding storage `handle` IS the
    // 8-byte heap handle to a 16-byte {data,vtable} fat pair. Sequence (null-
    // guarded): load data(field0)+vtable(field1); call vtable[0](data)
    // (drop_in_place runs the concrete's destructor + its owned fields);
    // free(data) (the boxed concrete); free(handle) (the fat slot).
    void gen_drop_owning_dyn_handle(mlir::Value handle, TypeRef::OwningKind kind);
    // Drop an owning `Box<[T]>` fat slice: drop each element (runtime loop, if T
    // is droppable) then free the heap buffer. `slice_ptr` points at {data,len}.
    void gen_drop_owning_slice(mlir::Value slice_ptr, TypeRef ty);
    // Drop an owning `Box<Foo>` custom-DST: drop droppable prefix fields + tail
    // elements (runtime loop over the fat-pointer length) then free the block.
    // `dst_ptr` points at the {data,len} fat pair (DstRef value = slice repr).
    void gen_drop_owning_dst(mlir::Value dst_ptr, TypeRef ty);
    // Drop the concrete payload behind a `&dyn` fat pair IN PLACE — run
    // vtable[0](data) (the concrete Drop) only, with NO free and NO refcount
    // change. This is the move-out-drop of an unsized `dyn` tail (`let _v: T =
    // self.inner.val` with T = dyn): same "run Drop, don't free the block"
    // semantics as the sized case (the block is freed separately by the caller).
    // `fat_ptr` points at the 16-byte {data,vtable} pair.
    void gen_drop_dyn_in_place(mlir::Value fat_ptr);
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
    bool deref_operand_is_ptr_to_dyn_handle(lir_view::ExprRef operand);
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
    // hermes2: coerce a scalar capture to an 8-byte value-form HAny word.
    mlir::Value coerce_to_hany_raw(mlir::Value v, TypeRef t);

    // ── Struct helpers ────────────────────────────────────────────
    mlir::Value get_struct_ptr(const std::string& name);
    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name);
    std::pair<mlir::Value, std::string> gen_recv_struct(lir_view::ExprRef recv);
    std::pair<mlir::Value, std::string> gen_recv_struct_inner(lir_view::ExprRef recv);
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
