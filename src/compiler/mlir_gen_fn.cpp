// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_fn.cpp — malloc/free helpers, function type, forward declaration, function body.

#include "mlir_gen_impl.hpp"

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// malloc / free helpers
// ---------------------------------------------------------------------------

void MLIRGenImpl::ensure_malloc_free(mlir::ModuleOp mod) {
    if (declared_fn_names_.insert("malloc").second) {
        auto fn_type = builder_.getFunctionType(
            {builder_.getI64Type()}, {ptr_type()});
        auto fn = mlir::func::FuncOp::create(loc_, "malloc", fn_type);
        fn.setPrivate();
        mod.push_back(fn);
        mark_funcs_dirty();
    }
    if (declared_fn_names_.insert("free").second) {
        auto fn_type = builder_.getFunctionType({ptr_type()}, {});
        auto fn = mlir::func::FuncOp::create(loc_, "free", fn_type);
        fn.setPrivate();
        mod.push_back(fn);
        mark_funcs_dirty();
    }
}

mlir::Value MLIRGenImpl::call_malloc(mlir::Value size) {
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto malloc_fn = mod.lookupSymbol<mlir::func::FuncOp>("malloc");
    if (!malloc_fn) return nullptr;
    auto call = builder_.create<mlir::func::CallOp>(
        loc_, malloc_fn, mlir::ValueRange{size});
    return call.getResult(0);
}

void MLIRGenImpl::call_free(mlir::Value ptr) {
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto free_fn = mod.lookupSymbol<mlir::func::FuncOp>("free");
    if (!free_fn) return;
    builder_.create<mlir::func::CallOp>(loc_, free_fn, mlir::ValueRange{ptr});
}

// Compute sizeof an LLVM struct type via GEP null trick.
mlir::Value MLIRGenImpl::sizeof_struct(mlir::LLVM::LLVMStructType struct_type) {
    mlir::Value zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    mlir::Value null   = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero64);
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(1)};
    mlir::Value gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), struct_type, null, idx);
    return builder_.create<mlir::LLVM::PtrToIntOp>(
        loc_, builder_.getI64Type(), gep);
}

// ---------------------------------------------------------------------------
// Function type from LFunction
// ---------------------------------------------------------------------------

mlir::Type MLIRGenImpl::fn_call_ret_llvm_type(TypeRef ret_type) {
    if (!ret_type) return nullptr;
    TypeRef rv{ret_type};
    if (type_str(ret_type) == "AnyVal") return builder_.getI32Type();
    if (rv.kind() == LogosType::Kind::Tuple) {
        return tuple_llvm_type(ret_type);
    }
    if (rv.kind() == LogosType::Kind::Struct ||
        rv.kind() == LogosType::Kind::ZonedStruct) {
        auto cname = mlir_struct_key(ret_type);
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) return sit->second.llvm_type;
        return ptr_type();
    }
    if (rv.kind() == LogosType::Kind::Enum) {
        auto* te = resolve_tagged_enum(std::string(rv.enum_name()), ret_type);
        if (te) return te->llvm_type;
        return builder_.getI32Type();
    }
    // RefRepr (Phase 2): a reference's by-value return ABI comes from the
    // descriptor — dyn/slice return their 16B fat pair by value (A3/A4 leak
    // fix), closure/custom-DST/thin return their 8B value pointer. NotARef →
    // fall through to logos_to_mlir for non-reference returns.
    if (auto rk = ref_repr_of(rv); rk != RefReprKind::NotARef)
        return repr_return_type(rk);
    return logos_to_mlir(ret_type);
}

mlir::FunctionType MLIRGenImpl::make_fn_type(lir_view::FunctionView fn) {
    const auto* mft_pool = pool_impl();
    TypeRef fn_ret = fn.ret_type(mft_pool);
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& p : fn.params()) {
        TypeRef pt = p.type(mft_pool);
        if (is_anyval(pt)) {
            param_types.push_back(builder_.getI32Type());
            continue;
        }
        // Arrays (like structs) are passed by pointer.
        if (pt && pt.kind() == LogosType::Kind::Array)
            param_types.push_back(ptr_type());
        else {
            auto t = logos_to_mlir(pt);
            if (t) param_types.push_back(t);
        }
    }
    llvm::SmallVector<mlir::Type> ret_types;
    if (fn_ret) {
        TypeRef rv{fn_ret};
        if (is_anyval(rv)) {
            ret_types.push_back(builder_.getI32Type());
        } else
        // Tuples and structs are returned by value (as LLVM struct), not by pointer.
        // Returning a pointer to a local alloca would be a dangling pointer after return.
        if (rv.kind() == LogosType::Kind::Tuple) {
            auto rt = tuple_llvm_type(fn_ret);
            if (rt) ret_types.push_back(rt);
        } else if (rv.kind() == LogosType::Kind::Struct ||
                   rv.kind() == LogosType::Kind::ZonedStruct) {
            auto cname = mlir_struct_key(fn_ret);
            auto sit = struct_types_.find(cname);
            if (sit != struct_types_.end())
                ret_types.push_back(sit->second.llvm_type);
            else
                ret_types.push_back(ptr_type()); // fallback (struct not yet registered)
        } else if (rv.kind() == LogosType::Kind::Enum) {
            // Tagged enums must also be returned by value (aggregate), not by pointer.
            auto* te = resolve_tagged_enum(std::string(rv.enum_name()), fn_ret);
            if (te)
                ret_types.push_back(te->llvm_type);
            else {
                // C-style (non-payload) enum — return i32.
                ret_types.push_back(builder_.getI32Type());
            }
        } else if (auto rk = ref_repr_of(rv); rk != RefReprKind::NotARef) {
            // RefRepr (Phase 2): the reference's by-value return ABI from the
            // descriptor (dyn/slice → 16B fat by value; closure/custom-DST/thin
            // → 8B value ptr) — mirrors fn_call_ret_llvm_type.
            ret_types.push_back(repr_return_type(rk));
        } else {
            auto rt = logos_to_mlir(fn_ret);
            if (rt) ret_types.push_back(rt);
        }
    }
    return builder_.getFunctionType(param_types, ret_types);
}

// ---------------------------------------------------------------------------
// Type-derived LLVM parameter attributes (rustc-parity, slice 1)
//
// Emits, on a THIN ptr param only:
//   &T / &mut T   → noundef, align(N), dereferenceable(size>0)
//   &mut T        → + noalias                     (borrow-checker exclusivity)
//   &T, T Freeze  → + noalias, readonly    (rule type.shared-ref.write-through-derived-ub)
//
// ⚠ THE `@claim … @endclaim` BLOCKS BELOW ARE MECHANIZED.
// A ruling that lives only in prose gets re-written, and this comment is the
// proof: the freeze bullet once read "DEFERRED … unsound until we can prove T
// has no interior mutability (UnsafeCell AND atomics, which are NOT
// UnsafeCell-wrapped in our stdlib)" and every clause of it was false by the
// next commit, while every gate stayed green for months.
// tests/logos/shared_ref_ub_lint.sh hashes each delimited block against
// tests/logos/shared_ref_ub_claims.ledger. PROSE OUTSIDE A BLOCK IS FREE TO
// IMPROVE — that is the whole reason the blocks are narrow rather than the file
// being hashed whole; hashing everything makes each honest edit a re-bless, and
// a gate that fires on honest edits gets deleted. Inside a block, a reworded
// claim is a deliberate re-bless and a REVERSED one is caught by the same
// mechanism, because no reader can tell those two apart.
//
// Soundness gates (see project_rustc_parity_type_attrs / the audit roadmap):
//   • Kind ∈ {Ref, MutRef} AND ref_repr_of == ThinPtr. This self-excludes raw
//     *T / fn-ptr (wrong Kind) and every FAT repr (FatSlice/FatDyn/FatClosure/
//     FatZoneMut) whose ptr arg points at a 16B {data,meta} pair, not the
//     pointee — so dereferenceable/align/noalias would be wrong there.
//   • extern fns skipped: C-ABI boundary, we don't own the callee's aliasing.
//   • noalias on &mut is sound because the borrow checker guarantees a safe
//     &mut param is exclusive for the call (verified across two-&mut, split
//     borrows, two-phase, closures). raw-ptr-aliased &mut is caller UB, same
//     as rustc.
//   @claim guard.freeze-gated
//   • noalias+readonly on a shared &T is gated on `type_is_freeze(pointee)`
//     (mlir_gen_types.cpp): no UnsafeCell reachable through the pointee's own
//     inline bytes — struct fields, enum payloads, tuple/array elements, and
//     NOT across an indirection (so Rc/Arc/&Cell stay Freeze, matching rustc:
//     the count they bump lives behind a pointer they LOAD, which carries its
//     own provenance and is not derived from this argument). The predicate is
//     conservative in the safe direction — unknown/unresolvable shape → NOT
//     Freeze — so a case it misses costs an optimization, never soundness.
//     @endclaim
//     Its arms are pinned one-for-one by tests/logos/ir/param_attrs_freeze_lattice
//     and the coverage is held by tests/logos/freeze_arm_coverage.py, which
//     derives the arm population from this predicate rather than from a list.
//
//     @claim history.deferred-bullet-was-false
//     ⚠ THIS BULLET USED TO READ "DEFERRED (slice C, blocked on a freeze
//     predicate) … unsound until we can prove T has no interior mutability
//     (UnsafeCell AND atomics, which are NOT UnsafeCell-wrapped in our
//     stdlib)". Every clause of that was false by the NEXT commit: e23c099a
//     landed the predicate right after 31b57f6a wrote the bullet and did not
//     come back for it, and all 12 Atomic* types are `{ val: UnsafeCell<…> }`
//     today (stdlib/lang/atomic/atomic.logos), as are Mutex/RwLock data
//     (lcm/sync/sync.logos:49,124) and the Writ arena+root
//     (lang/writ/container.logos:40,45). A comment describing a world that
//     ended one commit later is not a caveat, it is a wrong answer that
//     reads as a considered one — which is why the note stays here instead
//     of being quietly deleted.
//     @endclaim
//     The 12/Mutex/RwLock/Writ population in that paragraph is no longer a hand
//     count either: it is 19 roots derived over the whole of stdlib by
//     tests/logos/shared_ref_ub_lint.sh and checked against the emitted IR by
//     tests/logos/ir/param_attrs_freeze_interior.
//
//   @claim rule.unsafecell-is-not-a-writer
//   • WHAT LICENSES THE ATTRIBUTE — SETTLED, and it is a LANGUAGE rule, not a
//     codegen one. `readonly` says the callee performs no write through this
//     argument or anything derived from it. UnsafeCell is not a privileged
//     writer in Logos; it is only a MARKER that turns the predicate off. The
//     `&x as *const T as *mut T` cast is permitted on EVERY type in an unsafe
//     block, by explicit design (stdlib/lang/cell/cell.logos:15-16), and
//     `NonNull::from_ref(&T) -> NonNull<T>` (stdlib/lang/ptr/ptr.logos:241)
//     ships that laundering as a stdlib API. So the attribute cannot rest on
//     "nobody can make such a pointer" — it rests on a rule about the PROGRAM:
//     @endclaim
//
//     @claim rule.derived-write-ub
//       RULE type.shared-ref.write-through-derived-ub (tools/spec-extract/
//       rules/codegen/mlir_gen_fn/logos.json). Given `r: &T` with T Freeze, the
//       referent is IMMUTABLE for r's lifetime, and a write through any pointer
//       whose provenance chain is rooted at `r` is UNDEFINED.
//       EXCEPTION: a pointer VALUE loaded OUT of the referent carries its own
//       provenance and is NOT derived from `r`; writing through it is defined.
//     @endclaim
//
//     @claim rule.loaded-pointer-exception
//     THE EXCEPTION IS NOT A TECHNICALITY — IT IS WHY THE RULE IS LIVABLE.
//     `Rc<T> { inner: *mut RcInner<T> }` is Freeze exactly because the Freeze
//     recursion stops at the indirection, so `&Rc<T>` really does carry
//     noalias+readonly; and `Rc::clone` bumps a refcount reached by LOADING
//     `self.inner`, never by writing into the referent's own bytes. Drop the
//     exception and this rule condemns Rc::clone, Arc::clone, Weak::upgrade and
//     Rc/Arc::downgrade — all correct today. The mechanical guard on the
//     exception is tests/logos/ir/param_attrs_freeze_lattice's `axis_ptr_field`
//     line: it goes red the moment the recursion stops stopping at a pointer.
//     @endclaim
//
//     @claim rule.rustc-parity-not-divergence
//     THIS IS RUSTC PARITY, NOT A DIVERGENCE — rustc emits the identical
//     noalias+readonly on &T and rests on exactly this rule. It therefore
//     belongs in the SPEC, and NOT in DIVERGENCES.md.
//     @endclaim
//
//     MEASURED 2026-08-04, so the exposure is known rather than guessed:
//         @claim exposure.confined-to-unsafe
//       – THE EXPOSURE IS CONFINED TO `unsafe`. The cast itself is safe (it
//         only makes a pointer), but the write is not: dropping the unsafe
//         block off a cast-then-write repro fails the compile with "write
//         through raw pointer field requires unsafe context" + "dereference of
//         raw pointer requires unsafe context" (logosc rc=1). So no SAFE Logos
//         program can reach the rule, and it binds exactly where an obligation
//         already sits (spec `stmt.deref-write.raw-ptr-unsafe`).
//         @endclaim
//       – ⚠ AN EARLIER NOTE HERE CLAIMED the synthetic cast-then-write "returns
//         the right value at -O0 and the stale pre-call one at -O1/-O2/-O3".
//         RE-MEASURED and it does NOT reproduce in that shape: a single-file
//         `fn bump(c: &Counter)` doing read → cast-write → read exits 0 at ALL
//         FOUR levels, even though the emitted IR shows `bump` carrying
//         `ptr noalias noundef readonly`. The reason is the same one that made
//         the old behavioural canary useless: `bump` INLINES into `main`, and
//         inlining drops the parameter attributes before any pass can act on
//         them. A parameter attribute only has teeth while the call is still a
//         call. Cross an archive boundary and it bites immediately — see
//         tests/logos/ub_boundary/ (built at -O2) and the control revert
//         recorded in tests/logos/pass/interior_mut_freeze_canary.logos.
//         Recorded rather than quietly corrected, because "measured" was doing
//         the work of "true" in a claim nothing re-ran.
//         @claim exposure.no-stdlib-instance
//       – No stdlib instance of the prohibited shape. Every `as *const X as
//         *mut X` site was read: each is either behind a `&mut self` (fabric
//         push/set/insert/erase), a by-value `self` (Vec::into_iter,
//         PrimVec::drop), a non-Freeze pointee (every writ site — Writ holds
//         UnsafeCell), a READ through a `*mut`-receiver method (http types /
//         serialize / parse, hashmap_at, deem/incr, node_arc_data_ptr), or the
//         LOADED-POINTER EXCEPTION (rc.logos, arc.logos). That audit is a hand
//         read of ONE syntactic form; it is evidence, not a proof of absence,
//         which is why tests/logos/shared_ref_ub_lint.sh now holds the census
//         against a ledger instead of leaving it to the next hand read.
//         @endclaim
//
//     @claim status.write-detection-unenforced
//     STILL UNENFORCED, stated so the rule is not mistaken for a check: nothing
//     DETECTS a user program that takes the cast and writes. A real detector
//     must model the PROVENANCE CHAIN rather than the syntax, because the
//     exception above is the common and correct stdlib shape and a syntactic
//     matcher would fire on every Rc/Arc/Weak method in the tree.
//     @endclaim
//
//   • The arg-index walk MIRRORS make_fn_type's slot-push order exactly: anyval
//     → one i32 slot; Array → one ptr slot; a param whose logos_to_mlir is null
//     (Void/Never) pushes NO slot and must not advance the index.
// ---------------------------------------------------------------------------
void MLIRGenImpl::apply_param_attrs(mlir::func::FuncOp f, lir_view::FunctionView fn) {
    if (fn.is_extern()) return;            // FFI boundary — don't assert Logos aliasing
    using K = LogosType::Kind;
    const auto* pool = pool_impl();
    auto unit = mlir::UnitAttr::get(builder_.getContext());
    unsigned arg = 0;
    for (auto& p : fn.params()) {
        TypeRef pt = p.type(pool);
        if (is_anyval(pt))                         { ++arg; continue; }  // i32 slot
        if (pt && pt.kind() == K::Array)           { ++arg; continue; }  // ptr-to-array slot
        if (!logos_to_mlir(pt)) continue;          // zero-width param: NO slot pushed
        if (pt && (pt.kind() == K::Ref || pt.kind() == K::MutRef) &&
            ref_repr_of(pt) == RefReprKind::ThinPtr) {
            // ⚠ `llvm.align` / `llvm.dereferenceable` ARE A LAYOUT CLAIM, so they
            // may only be written when the pointee HAS a layout. A forward
            // declaration is emitted for every function in the program, including
            // ones whose signature still carries a TypeVar (an HRTB bound's
            // `&T`) or an Error — types no instance of which exists, which is
            // exactly what `type_has_unresolved_residue` names. Asking `layout_of`
            // for one is a question with no answer: it DECLINED and returned
            // `{8,8}`, and that guess was then written into the object file as
            // `align 8, dereferenceable 8` for a pointee whose real alignment is
            // whatever the instantiation says. MEASURED 2026-08-01 over the 5536
            // registered pass programs: 14 of them reached here, 10 with a bare
            // TypeVar (`tests/imported/pass/closures/hrtb-*`) and the rest with a
            // generic struct still carrying one.
            //
            // `noalias` / `readonly` / `noundef` are aliasing facts, not layout
            // facts, and stay — they are true of the reference whatever T is.
            bool sizeable = pt.pointee() && !type_has_unresolved_residue(pt.pointee());
            Layout lo = sizeable ? layout_of(pt.pointee()) : Layout{0, 0};
            f.setArgAttr(arg, "llvm.noundef", unit);
            if (sizeable)
                f.setArgAttr(arg, "llvm.align",
                             builder_.getI64IntegerAttr((int64_t)lo.align));
            if (lo.size > 0)
                f.setArgAttr(arg, "llvm.dereferenceable",
                             builder_.getI64IntegerAttr((int64_t)lo.size));
            if (pt.kind() == K::MutRef) {
                // &mut T is exclusive (borrow checker) → noalias.
                f.setArgAttr(arg, "llvm.noalias", unit);
            } else {
                // Shared &T: noalias + readonly ONLY when T is Freeze. The
                // UnsafeCell discipline is what makes a non-Freeze pointee
                // (Cell/atomic/Mutex/…) keep NEITHER, so mutation through a
                // coexisting interior-mut path is not mis-assumed away.
                //
                // WHAT MAKES THESE TWO LINES SOUND is not the predicate alone —
                // the const→mut cast is legal on a Freeze T too — but the
                // language rule that the header states in full:
                //   type.shared-ref.write-through-derived-ub
                // a write through a pointer DERIVED FROM a shared &T with a
                // Freeze pointee is UNDEFINED, while a write through a pointer
                // LOADED FROM the referent is not derived from it and stays
                // defined (that clause is what keeps Rc/Arc sound). Rustc
                // parity; spec rule, not a divergence.
                //
                // PINNED IN BOTH DIRECTIONS by tests/logos/ir/
                // param_attrs_freeze (definitions), _declare (the far larger
                // imported-declaration surface) and _lattice (the predicate's
                // five axes, incl. the indirection stop the exception needs).
                // The behavioural guard tests/logos/pass/
                // interior_mut_freeze_canary only bites because its mutators
                // live ACROSS AN ARCHIVE BOUNDARY (tests/logos/ub_boundary,
                // built at -O2): in one TU they inline and the attribute is
                // gone before any pass sees it. MEASURED — as a single file it
                // stayed green at -O0..-O3 with this gate forced open.
                if (type_is_freeze(pt.pointee())) {
                    f.setArgAttr(arg, "llvm.noalias", unit);
                    f.setArgAttr(arg, "llvm.readonly", unit);
                }
            }
        }
        ++arg;
    }
    // Return value: noundef on a SCALAR or POINTER result only. Logos has no
    // undef value form (MaybeUninit is zeroed; codegen emits no poison), so a
    // scalar/ptr return is always well-defined. Aggregate returns
    // (struct/tuple/enum-with-payload, lowered to an LLVM struct) are OMITTED:
    // their padding / inactive-variant bytes are genuinely undef. Mirrors rustc.
    auto rets = f.getFunctionType().getResults();
    if (rets.size() == 1 &&
        mlir::isa<mlir::IntegerType, mlir::FloatType,
                  mlir::LLVM::LLVMPointerType>(rets[0]))
        f.setResultAttr(0, "llvm.noundef", unit);
}

// ---------------------------------------------------------------------------
// Forward declare
// ---------------------------------------------------------------------------

void MLIRGenImpl::forward_declare(mlir::ModuleOp mod, lir_view::FunctionView fn,
                                    bool is_binary_skip) {
    const auto* fd_pool = pool_impl();
    std::string fn_name(fn.name());
    TypeRef fn_ret = fn.ret_type(fd_pool);
    // Dup-check via declared_fn_names_ instead of mod.lookupSymbol:
    // MLIR's symbol table cache is invalidated by every push_back, so each
    // lookupSymbol re-walks the module — O(n) per call, O(n²) across the
    // 3500+ forward_declare iterations.
    // Module system: emit under the qualified LINK symbol (methods gain the
    // module prefix here; free fns/extern unchanged). Symbol-identity uses below
    // key off `link`; only the diagnostic keeps the raw fn.name.
    const std::string link = link_name(fn);
    if (!declared_fn_names_.insert(link).second) return;
    if (fn.is_vararg()) {
        // Vararg extern fns use llvm.func (func dialect doesn't support varargs)
        llvm::SmallVector<mlir::Type> param_types;
        for (auto& p : fn.params()) {
            auto t = logos_to_mlir(p.type(fd_pool));
            if (t) param_types.push_back(t);
        }
        mlir::Type ret = fn_ret ? logos_to_mlir(fn_ret) : nullptr;
        if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types,
                                                                /*isVarArg=*/true);
        auto llvm_fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, link, llvm_fn_type);
        llvm_fn.setLinkage(mlir::LLVM::Linkage::External);
        mod.push_back(llvm_fn);
        vararg_fns_.insert(link);
        return;
    }
    auto f = mlir::func::FuncOp::create(loc_, link, make_fn_type(fn));
    // Binary-skip and extern fns are declaration-only — set private at
    // creation time to avoid the separate setPrivate-by-name pass.
    if (fn.is_extern() || is_binary_skip) f.setPrivate();
    apply_param_attrs(f, fn);
    mod.push_back(f);
    mark_funcs_dirty();   // invalidate the canonical-resolution index
    // Record Logos-level param types for dyn coercion at call sites.
    std::vector<TypeRef> ptypes;
    std::vector<bool> powning;
    for (auto& p : fn.params()) { ptypes.push_back(p.type(fd_pool)); powning.push_back(p.owning_box_dyn()); }
    // Module system: key the Logos-level param maps by BOTH the qualified link
    // name (the FuncOp's actual name) AND the bare fn.name. Bodies reference
    // BARE callees during emission (the canonical() bridge only fixes call names
    // post-hoc), so call-arg coercion (fn_param_types_.find(callee)) must hit on
    // the bare key too — else args are passed un-coerced (e.g. a by-value enum
    // word not spilled to its ptr param → operand type mismatch).
    if (link != fn_name) {
        fn_param_types_[fn_name] = ptypes;
        fn_param_owning_box_dyn_[fn_name] = powning;
    }
    fn_param_types_[link] = std::move(ptypes);
    fn_param_owning_box_dyn_[link] = std::move(powning);
}

// ---------------------------------------------------------------------------
// Function body
// ---------------------------------------------------------------------------

bool MLIRGenImpl::gen_function_body(mlir::func::FuncOp func, lir_view::FunctionView fn) {
    const auto* gfb_pool = pool_impl();
    auto fn_params = fn.params();
    auto fn_body = fn.body();
    TypeRef fn_ret = fn.ret_type(gfb_pool);
    // Guard: two distinct LFunctions producing the same mangled name would
    // otherwise both call addEntryBlock on the same FuncOp, resulting in a
    // single function with two unrelated bodies stitched together. Bug
    // surfaces later as a bare MLIR verifier "func.return op expects parent
    // op func.func" error with no source location. Most common cause:
    // pkg-mangling skips a non-current package context for a free fn, so
    // a private fn in pkg A collides with a pub fn of the same base name
    // in pkg B (which A imports).
    if (!func.empty()) {
        std::fprintf(stderr,
            "mlir-gen: duplicate function body for symbol '%s'; two "
            "functions resolved to the same mangled name — likely a "
            "private fn in one package shadowed by a pub fn of the same "
            "base name in an imported package. Rename one to disambiguate.\n",
            std::string(fn.name()).c_str());
        return false;
    }
    auto* entry = func.addEntryBlock();
    builder_.setInsertionPointToStart(entry);
    cur_entry_block_ = entry;

    // -g: open this function's DWARF scope (DISubprogram + fused debug loc).
    // No-op unless debug_info_. Prologue/param-binding ops below inherit the
    // function-scope location set here.
    begin_fn_debug(func, fn);

    scope_.clear();
    let_vars_.clear();
    uninit_drop_flag_.clear();
    uninit_flag_needed_.clear();
    uninit_static_.clear();
    uninit_assigned_.clear();
    var_elem_types_.clear();
    var_struct_.clear();
    var_subscript_.clear();
    var_slice_.clear();
    var_tuple_.clear();
    var_tagged_enum_.clear();
    var_tagged_enum_ptr_.clear();
    var_local_ptrs_.clear();
    var_dyn_trait_.clear();
    var_raw_dyn_.clear();
    dyn_ptr_to_handle_vars_.clear();
    ref_param_names_.clear();
    ptr_family_param_.clear();
    loop_stack_.clear();

    // Bind parameters.
    for (size_t i = 0; i < fn_params.size(); ++i) {
        auto& p = fn_params[i];
        std::string pname(p.name());
        TypeRef ptype = p.type(gfb_pool);
        scope_[pname] = entry->getArgument(i);
        // Pointer-family params (`*mut`/`*const`/`&`/`&mut`): their SSA arg IS a
        // pointer VALUE, so `&p` is the address of the param's own slot — record
        // them so EAddrOf spills (scalars are caught there by an SSA-type check;
        // aggregate by-value params arrive AS a pointer = the object address and
        // are NOT recorded, so `&p` returns that address unchanged). Classified
        // by logos kind only (no MLIR-arg query — safe for zero-size/`!` params
        // that are elided from the signature). Ref/MutRef additionally rebind
        // for `&&mut T` write-through.
        if (ptype) {
            auto pk = ptype.kind();
            if (pk == LogosType::Kind::Ptr || pk == LogosType::Kind::Ref ||
                pk == LogosType::Kind::MutRef)
                ptr_family_param_.insert(pname);
            if (pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef)
                ref_param_names_.insert(pname);
        }

        // Track subscript element type for pointer / reference parameters.
        auto is_ptr_kind = [](LogosType::Kind k) {
            return k == LogosType::Kind::Ptr ||
                   k == LogosType::Kind::Ref ||
                   k == LogosType::Kind::MutRef;
        };
        if (ptype) {
            TypeRef pv{ptype};
            if (is_ptr_kind(pv.kind()) && pv.pointee()) {
                // For ptr-to-struct, the subscript stride must be
                // sizeof(struct), not sizeof(ptr) — `logos_to_mlir(Struct)`
                // collapses to ptr_type, so look up the struct's full LLVM
                // type directly. Params don't go through a local alloca
                // slot, so we register only var_subscript_ (gen_index_*
                // reads it directly off the SSA arg) — not var_local_ptrs_,
                // which would trigger a spurious LoadOp.
                TypeRef pe = pv.pointee();
                // G162-2: a `&/&mut/*[T; N]` param indexes by the ELEMENT type
                // (the pointee is the whole array — `logos_to_mlir(array)` is
                // the `[N x T]` aggregate, which would stride the GEP by
                // sizeof(array) → OOB write/read). Peel to the element.
                if (pe.kind() == LogosType::Kind::Array && pe.elem())
                    pe = pe.elem();
                mlir::Type et;
                if (pe.kind() == LogosType::Kind::Struct ||
                    pe.kind() == LogosType::Kind::ZonedStruct) {
                    auto cname = mlir_struct_key(pe);
                    auto sit = struct_types_.find(cname);
                    if (sit != struct_types_.end()) et = sit->second.llvm_type;
                }
                if (!et) et = logos_to_mlir(pe);
                if (et) var_subscript_[pname] = et;
            } else if (pv.kind() == LogosType::Kind::Slice && pv.elem()) {
                // G162-2: a `&[T]` / `&mut [T]` slice param arrives as a
                // pointer to the fat `{ptr, len}` descriptor. Indexed
                // read/write must deref field 0 to the data pointer first
                // (gen_index_write / EIndexRead consult var_slice_), then
                // stride by the element type. Struct elements lay out inline,
                // so use the struct's full LLVM type for the stride.
                TypeRef se = pv.elem();
                mlir::Type et;
                if (se.kind() == LogosType::Kind::Struct ||
                    se.kind() == LogosType::Kind::ZonedStruct) {
                    auto cname = mlir_struct_key(se);
                    auto sit = struct_types_.find(cname);
                    if (sit != struct_types_.end()) et = sit->second.llvm_type;
                }
                if (!et) et = logos_to_mlir(se);
                if (et) var_slice_[pname] = et;
            } else if (pv.kind() == LogosType::Kind::Array && pv.elem()) {
                // Array params arrive as `ptr` (per make_fn_type). Without an
                // explicit subscript entry, gen_index_read's
                // subscript_elem_type(name) falls back to i32 — which on an
                // i64 array reads with stride-4 instead of stride-8 and
                // yields the alternating-value/zero pattern that masked the
                // assertion bug. Register the element's MLIR type so the
                // GEP stride matches the array layout.
                TypeRef ae = pv.elem();
                mlir::Type et;
                // G161-1: a `[Struct; N]` array stores INLINE structs, not
                // pointers — `logos_to_mlir(Struct)` is `ptr`, which would
                // stride the GEP by 8 and read each element as a pointer
                // (then deref garbage → SIGSEGV). Use the struct's LLVM type
                // so the stride is sizeof(Struct) and `a[i]` is the inline
                // element address (mirrors the slice-param branch above).
                if (ae.kind() == LogosType::Kind::Struct ||
                    ae.kind() == LogosType::Kind::ZonedStruct) {
                    auto cname = mlir_struct_key(ae);
                    auto sit = struct_types_.find(cname);
                    if (sit != struct_types_.end()) et = sit->second.llvm_type;
                }
                if (!et) et = logos_to_mlir(ae);
                if (et) var_subscript_[pname] = et;
            }
        }

        // Track trait-object (`dyn Trait` / `&dyn Trait`) parameters. Direct
        // dispatch works off the param type alone, but a closure capturing
        // such a param needs `var_dyn_trait_` set so it takes the dyn capture
        // branch (storing the {data,vtable} handle directly) instead of the
        // scalar branch (which allocas the handle and then mis-GEPs it as the
        // fat pair → SIGSEGV). Var-ref returns it->second either way, so this
        // doesn't change the direct path.
        if (ptype) {
            TypeRef pv{ptype};
            TypeRef trait_t;
            if (pv.kind() == LogosType::Kind::TraitObject)
                trait_t = pv;
            else if ((pv.kind() == LogosType::Kind::Ref ||
                      pv.kind() == LogosType::Kind::MutRef ||
                      pv.kind() == LogosType::Kind::Ptr) && pv.pointee() &&
                     TypeRef(pv.pointee()).kind() == LogosType::Kind::TraitObject)
                trait_t = pv.pointee();
            if (trait_t) {
                var_dyn_trait_[pname] = std::string(TypeRef(trait_t).trait_name());
                // A `*const/*mut dyn Trait` PARAM holds the raw trait-object fat
                // pointer (the handle) by value — the Rust raw-fat-ptr, not a
                // pointer-to-handle — so `*p` is the no-op default in EDeref
                // (raw-ptr-dyn-trait). No ptr-to-handle marking needed.
                continue;
            }
        }

        // Track struct type for parameters (including 'self').
        if (ptype) {
            TypeRef pv{ptype};
            std::string sname;
            if (pv.kind() == LogosType::Kind::Struct ||
                pv.kind() == LogosType::Kind::ZonedStruct)
                sname = mlir_struct_key(ptype);
            else if (is_ptr_kind(pv.kind()) && pv.pointee() &&
                     (pv.pointee().kind() == LogosType::Kind::Struct ||
                      pv.pointee().kind() == LogosType::Kind::ZonedStruct))
                sname = mlir_struct_key(pv.pointee());
            if (!sname.empty()) { var_struct_[pname] = std::move(sname); continue; }

            // G157-1: a by-value TAGGED-enum param (e.g. `x: Option<i64>`)
            // arrives as the heap ptr (one level). Register it like a local
            // enum `let` so `&x` spills it to a slot (EAddrOf's var_tagged_enum_
            // path) — yielding a real ptr-to-enum-ptr that the `==`→`eq` method
            // (which takes `&Enum`, two-level) can deref. Without this, `&x`
            // returned the bare heap ptr and `eq` loaded the i32 disc as a
            // pointer → SIGSEGV. C-like (no-payload) enum params are i32, not
            // ptr — their `&` is handled by EAddrOf's scalar-spill branch, so
            // gate on a resolvable TaggedEnumInfo.
            if (pv.kind() == LogosType::Kind::Enum &&
                resolve_tagged_enum(std::string(pv.enum_name()), pv)) {
                var_tagged_enum_.insert(pname);
                continue;
            }
        }
    }

    // -g: emit DWARF parameter debug info (info args / print <param>). Uses the
    // calling-convention arg SSA directly (entry args), not scope_ (which later
    // branches may remap).
    if (debug_info_ && di_subprogram_) {
        for (size_t i = 0; i < fn_params.size(); ++i) {
            if (i >= entry->getNumArguments()) break;
            emit_param_dbg_declare(std::string(fn_params[i].name()),
                                   fn_params[i].type(gfb_pool),
                                   entry->getArgument(i), (unsigned)i + 1,
                                   di_scope_line_);
        }
    }

    auto ret_types = func.getFunctionType().getResults();
    cur_ret_type_ = ret_types.empty() ? mlir::Type{} : ret_types[0];
    cur_fn_ret_logos_type_ = fn_ret;
    // A body whose own SIGNATURE still carries a TypeVar / Error / AssocType
    // is TEMPLATE RESIDUE — no instance of it exists, so nothing in it can be
    // "dropped" in the sense the R2 silent-drop guards mean. The residue is
    // mono's arc (`$tuple$variadic__fmt__g__ref_tup$1$A…` in logos.mem.fmt is
    // the live example), so the guards below are scoped OFF for it rather than
    // softened. This is a STRUCTURAL fact read off the types, not a name test.
    // …and an UNINHABITED signature (`!` anywhere in it) is dead by
    // construction for the same reason — `Option<!>::replace` in logos.lang.
    auto sig_dead = [&](TypeRef t) {
        return type_has_unresolved_residue(t) || type_mentions_never(t);
    };
    cur_fn_has_residue_ = sig_dead(fn_ret);
    if (!cur_fn_has_residue_)
        for (auto& p : fn.params())
            if (sig_dead(p.type(gfb_pool))) { cur_fn_has_residue_ = true; break; }
    cur_fn_name_ = link_name(fn);
    cur_fn_pkg_  = std::string(fn.package());   // G156-1: pkg-scoped const resolution

    // B8 drop elaboration: decide which declared-uninit vars need a runtime drop
    // flag (any conditional/loop assignment) BEFORE codegen — a flagged var must
    // maintain its flag from its very first assignment, which is lowered before
    // we'd otherwise discover a later conditional assignment.
    { std::unordered_map<std::string, int> decl_depth;
      prescan_uninit_flags(fn_body, 0, decl_depth); }

    gen_block(fn_body);

    if (!is_terminated(builder_.getBlock())) {
        if (ret_types.empty()) {
            builder_.create<mlir::func::ReturnOp>(loc_);
        } else {
            // Non-void fn whose body fell through. Sema's reachability
            // pass should have rejected genuinely missing returns; the
            // fall-through here means sema accepted the path (e.g.
            // exhaustive tuple/struct match without an explicit `_`
            // arm) but mlir-gen's dispatch lowering doesn't know that
            // and left the merge block live. Emit unreachable so the
            // function verifies; dead code, never executed.
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        }
    }

    end_fn_debug();  // -g: close this function's DWARF scope.
    return true;
}

} // namespace logos::compiler
