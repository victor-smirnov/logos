// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos L-IR to MLIR.
//
// Input: lir::LProgram (typed IR produced by sema_lower()).
// Output: mlir::ModuleOp.
//
// All type information is pre-computed in L-IR — no Hermes lookups here.
//
// This file: generate(), struct/array helpers, public mlir_gen() function.
// Method definitions split across mlir_gen_types.cpp, mlir_gen_fn.cpp,
// mlir_gen_stmt.cpp, mlir_gen_expr.cpp, mlir_gen_dyn.cpp.

#include "mlir_gen_impl.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

using namespace lir;

namespace {

// Per-phase timer for `LOGOS_MLIR_PHASE_TIMING=1`. Cheap (steady_clock::now)
// when disabled-once-at-init.
struct MlirPhaseTimer {
    bool enabled;
    std::chrono::steady_clock::time_point t0;
    MlirPhaseTimer() {
        const char* e = std::getenv("LOGOS_MLIR_PHASE_TIMING");
        enabled = e && e[0] && e[0] != '0';
    }
    void reset() { t0 = std::chrono::steady_clock::now(); }
    void tick(const char* phase) {
        if (!enabled) { reset(); return; }
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::fprintf(stderr, "[mlir-phase] %-32s %6ld us\n", phase, (long)ms);
        t0 = t1;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// MLIRGenImpl::generate — top-level lowering pipeline
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> MLIRGenImpl::generate(const LProgram& prog) {
    MlirPhaseTimer pt;
    pt.reset();

    auto mod = mlir::ModuleOp::create(loc_);

    prog_   = &prog;
    mirror_ = prog.mirror_table.get();

    // Coexistence: module-qualify type-keyed symbol names (drop glue, vtables,
    // mono insts) consistently with sema/mono so emitted defs match their uses.
    TypeModuleScope _type_module_scope(&prog.pkg_module_ids);

    // Pass 0: build struct lookup table so register_tagged_enum can compute
    // payload sizes from LogosType field trees (logos_abi_byte_size).
    for (auto& sd : prog.structs) {
        std::string key = qualify_pkg(sd.pkg, sd.name);
        all_struct_defs_[key] = &sd;
        // Bare alias for legacy paths that haven't been threaded with pkg.
        // First-registered wins on collision (matches the legacy global-
        // namespace behavior; cross-pkg same-name structs surface via the
        // qualified entry).
        if (!sd.pkg.empty() && !all_struct_defs_.count(sd.name))
            all_struct_defs_[sd.name] = &sd;
        // Coexistence: concrete_struct_name lookups (field access, DST resolve)
        // now carry the "$M<module_id>" suffix for non-stdlib module types, so
        // register a matching alias. Generic-instance sd.name already carries it
        // (built via concrete_struct_name in mono) → suffix is empty, no dup.
        std::string msuffix = type_module_suffix(sd.pkg);
        if (!msuffix.empty()) {
            std::string qname = sd.name + msuffix;
            if (!all_struct_defs_.count(qname)) all_struct_defs_[qname] = &sd;
        }
    }

    // Pass 0.5: register enum types (needs all_struct_defs_ populated above).
    // Two passes for tagged enums: pre-register all NAMES first (stub
    // TaggedEnumInfo entries) so logos_to_mlir on a nested enum payload
    // (e.g. Option<Option<T>>::Some carrying Option<T>) resolves to
    // ptr_type rather than the discriminant scalar. Then fill in real
    // layouts in the second pass.
    for (auto& ed : prog.enums) {
        enum_types_[ed.name] = &ed;
        if (ed.has_payload() && !tagged_enums_.count(ed.name)) {
            TaggedEnumInfo stub;
            stub.name = ed.name;
            tagged_enums_[ed.name] = std::move(stub);
        }
    }
    for (auto& ed : prog.enums) {
        if (ed.has_payload()) register_tagged_enum(ed);
    }
    // Enum value-repr: a nested enum payload's inline footprint depends on the
    // nested enum's own payload_bytes, but registration order is not guaranteed
    // inner-first — an outer enum registered before its nested enum sized that
    // payload from a 0-byte stub (under-sized → nested-enum memcpy/layout
    // corruption). Recompute every tagged enum's payload_bytes to a FIXPOINT
    // (sizes only ever grow) and re-set the `enum.NAME` body so all inline
    // footprints are correct regardless of order.
    {
        // Phase 1: recompute payload_bytes to a fixpoint (sizes only grow).
        bool changed = true;
        for (int pass = 0; changed && pass < 64; ++pass) {
            changed = false;
            for (auto& ed : prog.enums) {
                if (!ed.has_payload()) continue;
                auto it = tagged_enums_.find(ed.name);
                if (it == tagged_enums_.end()) continue;
                uint64_t max_bytes = 0, max_align = 1;
                for (auto& v : ed.variants) {
                    auto pl = variant_payload_layout(v);
                    if (pl.size  > max_bytes) max_bytes = pl.size;
                    if (pl.align > max_align) max_align = pl.align;
                }
                if (max_bytes > it->second.payload_bytes ||
                    max_align > it->second.payload_align) {
                    it->second.payload_bytes = std::max(max_bytes, it->second.payload_bytes);
                    it->second.payload_align = std::max(max_align, it->second.payload_align);
                    changed = true;
                }
            }
        }
        // Phase 2: set every tagged enum's identified-struct body ONCE, now
        // that all payload_bytes/_align are final (an identified LLVM struct
        // body is set-once — this is why register_tagged_enum defers it). The
        // payload is an aligned blob `[count x i<align*8>]` (count chosen so
        // count*align ≥ payload_bytes), so LLVM places it after the i32 disc
        // with the necessary padding and gives the enum align = max(4, align) —
        // an align-8 payload (i64/ptr/struct) is no longer mis-positioned at
        // offset 4. Matches layout_of(enum).
        auto i32 = builder_.getI32Type();
        for (auto& [name, info] : tagged_enums_) {
            if (!info.llvm_type) continue;
            uint64_t pb = info.payload_bytes ? info.payload_bytes : 1;
            uint64_t pa = info.payload_align ? info.payload_align : 1;
            uint64_t count = (pb + pa - 1) / pa;  // round up to whole elements
            auto elem = builder_.getIntegerType((unsigned)(pa * 8));
            auto payload = mlir::LLVM::LLVMArrayType::get(elem, count);
            // Phase 3.5: a niche-packed enum has NO disc word — its body is just
            // the payload (the niche-bearing pointer), so it is pointer-sized.
            if (info.niche.packed)
                (void)info.llvm_type.setBody({payload}, false);
            else
                (void)info.llvm_type.setBody({i32, payload}, false);
        }
    }
    pt.tick("pass0 register types");

    // Register struct LLVM types (all_struct_defs_ already built above).
    for (auto& sd : prog.structs)
        if (!register_struct(sd)) return nullptr;
    pt.tick("pass0 register_struct");

    for (auto& ta : prog.type_aliases)
        type_aliases_[ta.name] = logos_to_mlir(ta.type);

    for (auto& c : prog.consts)
        module_consts_[c.name] = &c;

    // Declare malloc and free for 'new' and 'delete'.
    ensure_malloc_free(mod);
    pt.tick("pass0 aliases+consts+ensure");

    // Pass 1: forward-declare all functions (structs, free fns).
    // Skip a fn's body emission iff the linker can find it in a binary
    // archive (libstdlib.a et al.) — emitting again would cause multiple-
    // definition link errors.
    //
    // Two distinct cases trigger a skip:
    //   1. fn.from_binary_module — sema lowered this from a `.hermes0`
    //      member; the matching `.o` member is right next to it.
    //   2. fn.name is in binary_symbols AND the name carries mono mangling
    //      markers (`$` for type-arg packs, `__` for type-method joins) —
    //      this is a mono-instantiated entry whose template's pre-baked
    //      instance is already in the archive (e.g. user's `Vec_u64__push`
    //      colliding with stdlib's pre-baked one).
    //
    // We deliberately do NOT skip on bare-name match — a user-source fn
    // sharing a bare name with a stdlib symbol (`fn alloc(...)`) must be
    // emitted; the user's `.o` definition wins at link time and the
    // archive's same-named member is left out (lazy archive linking).
    // Body-skip: a fn whose name is in `prog.binary_symbols`. The
    // population side (main.cpp) restricts that set to user-explicit
    // -L / -l archives so the system stdlib does NOT silently shadow a
    // user-source bare-name fn (`fn alloc`) in projects that don't opt
    // in to stdlib's pre-baked bodies. Generic instantiations from
    // binary-module templates are NOT in the archive and must be
    // compiled, so we use binary_symbols rather than the
    // from_binary_module flag (mono erases the flag during clone).
    auto is_binary_skip = [&prog, this](const lir::LFunction& fn) -> bool {
        if (fn.is_extern || prog.binary_symbols.empty()) return false;
        // Module system: archive nm carries QUALIFIED link symbols → match on link_name.
        return prog.binary_symbols.count(link_name(fn)) > 0;
    };

    // Phase 6 (multi-arena IR) item-level lazy reach analysis.
    //
    // For lazy modules (LFunction.from_lazy_module=true), sema lowered the
    // body but we only want to emit it if some non-lazy caller reaches it.
    // Roots: every non-lazy fn + every extern (its body might live in a
    // foreign .a). BFS through direct calls (ECall.callee /
    // EMethodCall.resolved_symbol) and trait/dyn dispatch tables.
    //
    // Coverage: direct calls + method calls. Fn-pointer / closure values
    // pointing at lazy fns are conservatively over-pruned (a real workload
    // that hits this can extend the walker; today's stdlib fixtures don't).
    std::unordered_set<std::string> lazy_emit;
    bool any_lazy = false;
    for (auto& fn : prog.functions) if (fn && fn->from_lazy_module) { any_lazy = true; break; }
    if (!any_lazy)
        for (auto& sd : prog.structs)
            for (auto& m : sd.methods) if (m && m->from_lazy_module) { any_lazy = true; break; }

    if (any_lazy) {
        // Index all fn definitions for cheap callee→fn lookup.
        std::unordered_map<std::string, const lir::LFunction*> by_name;
        for (auto& fn : prog.functions) if (fn) by_name[fn->name] = fn.get();
        for (auto& sd : prog.structs)
            for (auto& m : sd.methods) if (m) by_name[m->name] = m.get();

        std::deque<std::string> worklist;
        auto seed_root = [&](const lir::LFunction& fn) {
            if (fn.from_lazy_module) return;     // not a root
            if (lazy_emit.insert(fn.name).second) worklist.push_back(fn.name);
        };
        for (auto& fn : prog.functions) if (fn) seed_root(*fn);
        for (auto& sd : prog.structs)
            for (auto& m : sd.methods) if (m) seed_root(*m);

        // prog is const; use the const arena() accessor (returns nullptr if
        // the pool was never initialised — in that case there are no bodies
        // to walk, so we early-out below).
        const hermes::Arena* walk_arena_p = prog.type_pool.arena();
        std::function<void(lir_view::BlockRef)> walk_block;
        std::function<void(lir_view::StmtRef)>  walk_stmt;
        std::function<void(lir_view::ExprRef)>  walk_expr;

        auto note_callee = [&](std::string_view name) {
            if (name.empty()) return;
            std::string s(name);
            if (by_name.find(s) == by_name.end()) return;
            if (lazy_emit.insert(s).second) worklist.push_back(std::move(s));
        };

        walk_expr = [&](lir_view::ExprRef e) {
            if (!e) return;
            using C = lir_schema::expr::Code;
            switch (e.kind()) {
            case C::Call: {
                lir_view::ECallView v{e};
                note_callee(v.callee());
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a); });
                break;
            }
            case C::MethodCall: {
                lir_view::EMethodCallView v{e};
                note_callee(v.resolved_symbol());
                walk_expr(v.receiver());
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a); });
                break;
            }
            case C::ClosureCall: {
                lir_view::EClosureCallView v{e};
                walk_expr(v.callee());
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a); });
                break;
            }
            case C::FnPtrCall: {
                lir_view::EFnPtrCallView v{e};
                walk_expr(v.callee());
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a); });
                break;
            }
            case C::AddrOf: {
                // `&fn_name` as a fn-pointer reference — keeps fn reachable.
                note_callee(lir_view::EAddrOfView{e}.var_name());
                break;
            }
            case C::GenericRef: {
                // Generic-fn turbofish at value position — name is the fn.
                lir_view::EGenericRefView v{e};
                note_callee(v.name());
                break;
            }
            case C::VarRef:
                note_callee(lir_view::EVarRefView{e}.name());
                break;
            default: {
                // Generic child walk for all other variants: read every
                // sub-expr key that may carry an expression payload. The
                // dedicated cases above cover call shapes; here we just
                // recurse so nested calls inside arithmetic / struct lits /
                // match arms etc. get visited.
                auto recurse_arr = [&](uint8_t key) {
                    auto av = e.mirror()->get(key);
                    if (av.is_null()) return;
                    auto* arr = av.as_ptr<const hermes::ObjectArray>();
                    for (uint64_t i = 0; i < arr->size(); ++i) {
                        auto el = arr->get(i);
                        if (!el.is_null())
                            walk_expr(lir_view::detail::make_sub_ref<lir_view::ExprRef>(e, el));
                    }
                };
                auto recurse_sub = [&](uint8_t key) {
                    walk_expr(e.sub_expr(key));
                };
                namespace ek = lir_schema::expr_keys;
                recurse_sub(ek::LHS.code);
                recurse_sub(ek::RHS.code);
                recurse_sub(ek::OPERAND.code);
                recurse_sub(ek::RECEIVER.code);
                recurse_sub(ek::SCRUT.code);
                recurse_sub(ek::INDEX.code);
                recurse_sub(ek::CALLEE.code);
                recurse_sub(ek::FMT.code);
                recurse_arr(ek::ARGS.code);
                recurse_arr(ek::ELEMS.code);
                recurse_arr(ek::FIELD_VALUES.code);
                recurse_arr(ek::PAYLOAD.code);
                // Match arms: each arm has (pat, guard, value).
                {
                    auto av = e.mirror()->get(ek::ARMS.code);
                    if (!av.is_null()) {
                        auto* arr = av.as_ptr<const hermes::ObjectArray>();
                        for (uint64_t i = 0; i < arr->size(); ++i) {
                            auto el = arr->get(i);
                            if (el.is_null()) continue;
                            lir_view::EMatchArmRef arm =
                                lir_view::detail::make_sub_ref<lir_view::EMatchArmRef>(e, el);
                            walk_expr(arm.guard());
                            walk_expr(arm.value());
                            walk_block(arm.body());
                        }
                    }
                }
                break;
            }
            }
        };

        walk_stmt = [&](lir_view::StmtRef s) {
            if (!s) return;
            using SC = lir_schema::stmt::Code;
            switch (s.kind()) {
            case SC::Let:     walk_expr(lir_view::SLetView{s}.value()); break;
            case SC::Assign:  walk_expr(lir_view::SAssignView{s}.value()); break;
            case SC::Return:  walk_expr(lir_view::SReturnView{s}.value()); break;
            case SC::ExprStmt: walk_expr(lir_view::SExprStmtView{s}.expr()); break;
            case SC::If: {
                lir_view::SIfView v{s};
                walk_expr(v.cond()); walk_block(v.then_block()); walk_block(v.else_block());
                break;
            }
            case SC::While: {
                lir_view::SWhileView v{s};
                walk_expr(v.cond()); walk_block(v.body());
                break;
            }
            case SC::For: {
                lir_view::SForView v{s};
                walk_expr(v.lo()); walk_expr(v.hi()); walk_block(v.body());
                break;
            }
            case SC::Loop:   walk_block(lir_view::SLoopView{s}.body()); break;
            case SC::Block:  walk_block(lir_view::SBlockView{s}.body()); break;
            case SC::Match: {
                lir_view::SMatchView v{s};
                walk_expr(v.scrut());
                v.each_arm([&](lir_view::EMatchArmRef arm) {
                    walk_expr(arm.guard());
                    walk_expr(arm.value());
                    walk_block(arm.body());
                });
                break;
            }
            case SC::FieldWrite:      walk_expr(lir_view::SFieldWriteView{s}.value()); break;
            case SC::DerefFieldWrite: walk_expr(lir_view::SDerefFieldWriteView{s}.value()); break;
            case SC::IndexWrite: {
                lir_view::SIndexWriteView v{s};
                walk_expr(v.index()); walk_expr(v.value());
                break;
            }
            default: break;
            }
        };

        walk_block = [&](lir_view::BlockRef b) {
            if (!b) return;
            b.each_stmt([&](lir_view::StmtRef s) { walk_stmt(s); });
        };

        while (walk_arena_p && !worklist.empty()) {
            auto name = std::move(worklist.front());
            worklist.pop_front();
            auto it = by_name.find(name);
            if (it == by_name.end()) continue;
            auto& fn = *it->second;
            if (fn.body.mirror_ptr_ == nullptr) continue;
            walk_block(lir_view::BlockRef(walk_arena_p, fn.body.mirror_ptr_));
        }

        if (std::getenv("LOGOS_TRACE_PHASES")) {
            size_t lazy_total = 0, lazy_kept = 0;
            for (auto& fn : prog.functions)
                if (fn && fn->from_lazy_module) {
                    ++lazy_total;
                    if (lazy_emit.count(fn->name)) ++lazy_kept;
                }
            for (auto& sd : prog.structs)
                for (auto& m : sd.methods)
                    if (m && m->from_lazy_module) {
                        ++lazy_total;
                        if (lazy_emit.count(m->name)) ++lazy_kept;
                    }
            std::fprintf(stderr,
                "mlir_gen: lazy reach = %zu/%zu kept (%zu pruned)\n",
                lazy_kept, lazy_total, lazy_total - lazy_kept);
        }
    }

    auto is_lazy_skip = [&](const lir::LFunction& fn) -> bool {
        if (!any_lazy) return false;
        if (!fn.from_lazy_module) return false;
        return lazy_emit.find(fn.name) == lazy_emit.end();
    };

    if (std::getenv("LOGOS_TRACE_PHASES")) {
        size_t total = 0, skipped = 0;
        for (auto& sd : prog.structs) for (auto& m : sd.methods) { ++total; if (is_binary_skip(*m)) ++skipped; }
        for (auto& fn : prog.functions) { ++total; if (is_binary_skip(*fn)) ++skipped; }
        std::fprintf(stderr, "mlir_gen: %zu functions total, %zu binary-skip\n", total, skipped);
    }

    // Always forward-declare every function so call sites can resolve the
    // signature.  Binary-skip only suppresses body emission — the linker
    // provides the implementation from the archive. Pass is_binary_skip
    // through so forward_declare sets the FuncOp private at creation time
    // — saves the second by-name lookup pass over 3500+ symbols.
    //
    // Phase 6: lazy fns unreached by any non-lazy caller are skipped
    // entirely (no forward-decl, no body) — nothing references them, so
    // there's nothing to link against.
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods) {
            if (is_lazy_skip(*m)) continue;
            forward_declare(mod, *m, is_binary_skip(*m));
        }

    for (auto& fn : prog.functions) {
        if (is_lazy_skip(*fn)) continue;
        forward_declare(mod, *fn, is_binary_skip(*fn));
    }
    pt.tick("pass1 forward_declare");

    // Pass 1b: emit vtable globals for trait impls (&dyn Trait support).
    emit_trait_vtables(mod, prog);
    pt.tick("pass1b vtables");

    // Pass 1c: emit tag-based dispatch tables (one [223 x ptr] per TagSystem×Trait×method).
    emit_tag_dispatch_tables(mod, prog);
    pt.tick("pass1c tag_dispatch");

    // Pass 1d: emit TypeInfo rodata globals for reflect::<T>() and annotated types.
    {
        auto i8 = builder_.getIntegerType(8);
        builder_.setInsertionPointToEnd(mod.getBody());
        for (auto& rg : prog.reflection_globals) {
            // Avoid duplicate emission (e.g. stdlib pre-compiled + current TU).
            if (mod.lookupSymbol(rg.symbol)) continue;
            auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, rg.blob.size());
            auto blob_attr = builder_.getStringAttr(
                llvm::StringRef(reinterpret_cast<const char*>(rg.blob.data()), rg.blob.size()));
            // WeakODR: multiple TUs can emit the same symbol; linker keeps one.
            builder_.create<mlir::LLVM::GlobalOp>(
                loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::WeakODR,
                rg.symbol, blob_attr);
        }
    }
    pt.tick("pass1d reflection_globals");

    // Pass 1e: §6.2 statics (S25) — emit one llvm.mlir.global per `static`
    // item + the @__logos_static_init runtime initializer.
    emit_static_globals(mod, prog);
    pt.tick("pass1e static_globals");

    // Pass 2: fill function bodies (structs, free fns).
    size_t bodies_emitted = 0;
    size_t method_bodies = 0;
    size_t fn_bodies = 0;
    for (auto& sd : prog.structs) {
        for (auto& m : sd.methods) {
            if (is_binary_skip(*m) || is_lazy_skip(*m)) continue;
            auto func = mod.lookupSymbol<mlir::func::FuncOp>(link_name(*m));
            if (!gen_function_body(func, *m)) return nullptr;
            ++method_bodies; ++bodies_emitted;
        }
    }
    for (auto& fn : prog.functions) {
        if (fn->is_extern || is_binary_skip(*fn) || is_lazy_skip(*fn)) continue;
        auto func = mod.lookupSymbol<mlir::func::FuncOp>(link_name(*fn));
        if (!gen_function_body(func, *fn)) return nullptr;
        ++fn_bodies; ++bodies_emitted;
    }
    pt.tick("pass2 body emit");
    if (pt.enabled) {
        std::fprintf(stderr, "[mlir-phase]   bodies_emitted=%zu (methods=%zu fns=%zu)\n",
                     bodies_emitted, method_bodies, fn_bodies);
    }

    // Pass 3: inject dispatch table init calls at the start of main().
    // Each tag system has __logos_tag_dispatch_init__<TagSystem>.  Binary tag
    // systems have this function in the archive; non-binary systems have it in
    // the MLIR module (emitted by emit_tag_dispatch_tables).  We forward-declare
    // and call all of them so the linker resolves the binary ones from the .a.
    if (!prog.dispatch_entries.empty()) {
        auto main_fn = mod.lookupSymbol<mlir::func::FuncOp>("main");
        if (main_fn && !main_fn.empty()) {
            mlir::OpBuilder::InsertionGuard guard(builder_);
            // Collect unique tag systems in stable order.
            std::set<std::string> seen_systems;
            std::vector<std::string> init_fns;
            for (auto& de : prog.dispatch_entries) {
                if (de.tag_system.empty()) continue;
                if (!seen_systems.insert(de.tag_system).second) continue;
                init_fns.push_back("__logos_tag_dispatch_init__" + de.tag_system);
            }
            // Forward-declare any init fn not yet in the module (binary archive provides it).
            auto void_fn_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
            for (auto& fn_name : init_fns) {
                if (!mod.lookupSymbol<mlir::func::FuncOp>(fn_name)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    auto decl = builder_.create<mlir::func::FuncOp>(loc_, fn_name, void_fn_type);
                    decl.setPrivate();
                }
            }
            // Inject calls at the start of main.
            builder_.setInsertionPointToStart(&main_fn.front());
            for (auto& fn_name : init_fns) {
                auto init_fn = mod.lookupSymbol<mlir::func::FuncOp>(fn_name);
                builder_.create<mlir::func::CallOp>(loc_, init_fn, mlir::ValueRange{});
            }
        }
    }
    pt.tick("pass3 dispatch init");

    // Pass 3b: §6.2 statics (S25) — call @__logos_static_init at main's
    // prologue (before the dispatch-init calls so statics are live for any
    // ctor-ish code). Injected at the very front of main's entry block.
    if (has_static_init_) {
        auto main_fn = mod.lookupSymbol<mlir::func::FuncOp>("main");
        if (main_fn && !main_fn.empty()) {
            mlir::OpBuilder::InsertionGuard guard(builder_);
            auto init_fn = mod.lookupSymbol<mlir::func::FuncOp>("__logos_static_init");
            if (init_fn) {
                builder_.setInsertionPointToStart(&main_fn.front());
                builder_.create<mlir::func::CallOp>(loc_, init_fn, mlir::ValueRange{});
            }
        }
    }
    pt.tick("pass3b static init");

    // §P4 (module system): the canonical() bare↔pkg rename bridge is DELETED.
    // It used to rewrite callees/symbol-refs produced in older bare forms (mono
    // call rewrites, T→Concrete, blanket-impl emit) to the unified pkg-qualified
    // symbols POST-HOC. Now the find_func_op chokepoint binds every call/ref to
    // the exact module-qualified FuncOp AT EMISSION, so the bridge was fully
    // inert (verified: 0 rewrites across 450 pass+imported tests) and only cost 4
    // whole-module walks. If a regression ever reintroduces a bare-callee leak,
    // route that emission site through find_func_op rather than reviving this.

    // Carry the static-vtable specs to the pipeline tail (lower_and_emit_object):
    // after func→llvm lowering it materialises each placeholder `[N x ptr]`
    // vtable global's initializer with `addressof` of the method symbols (valid
    // only once they are `llvm.func`) → a true .rodata/.data.rel.ro static
    // vtable. Encoded as `logos.vtable_specs` = [[sym, m0, m1, …], …] (an empty
    // method string = object-unsafe sentinel → null slot).
    if (!dyn_vtable_specs_.empty()) {
        llvm::SmallVector<mlir::Attribute> specs;
        for (auto& [vsym, vmethods] : dyn_vtable_specs_) {
            llvm::SmallVector<mlir::Attribute> one;
            one.push_back(builder_.getStringAttr(vsym));
            for (auto& m : vmethods) one.push_back(builder_.getStringAttr(m));
            specs.push_back(builder_.getArrayAttr(one));
        }
        mod->setAttr("logos.vtable_specs", builder_.getArrayAttr(specs));
    }

    if (mlir::failed(mlir::verify(mod))) {
        std::fprintf(stderr, "mlir_gen: module verification failed\n");
        mod.dump();
        return nullptr;
    }
    pt.tick("verify");
    return mod;
}

// ---------------------------------------------------------------------------
// Struct helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::get_struct_ptr(const std::string& name) {
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        // Same suppression as gen_expr_kind's EVarRefView path — stale
        // VarRefs from mono void-payload specs.
        if (std::getenv("LOGOS_MLIRGEN_DEBUG_UNDEF"))
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    // Mutable raw-pointer locals are stored as alloca(ptr) slots.
    // For struct receivers we need the pointee pointer value, not the slot address.
    if (var_local_ptrs_.count(name) && let_vars_.count(name))
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), it->second);
    return it->second;
}

mlir::Value MLIRGenImpl::gep_field(mlir::Value base, const StructInfo& info,
                                    const std::string& field_name) {
    for (auto& f : info.fields) {
        if (f.name == field_name) {
            llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(f.index)};
            return builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), info.llvm_type, base, idx);
        }
    }
    std::fprintf(stderr, "mlir_gen: struct '%s' has no field '%s' (in fn %s)\n",
                 info.name.c_str(), field_name.c_str(), cur_fn_name_.c_str());
    return nullptr;
}

// Resolve receiver expr → (object_ptr, type_name).
// A FatZoneMut receiver (`&mut T` of a #[zone_mut] type) is a {data, zone} fat
// pair; the struct's address is the DATA half. The inner resolver returns the
// pointer-to-pair (like any ref value); this wrapper peels it to the data ptr so
// field/method access lands on the object, not the fat-pair storage. Identity for
// every other (thin) receiver.
std::pair<mlir::Value, std::string> MLIRGenImpl::gen_recv_struct(const LExpr& recv) {
    auto res = gen_recv_struct_inner(recv);
    if (res.first && recv.type &&
        ref_repr_of(TypeRef(recv.type)) == RefReprKind::FatZoneMut)
        res.first = repr_data(RefReprKind::FatZoneMut, res.first);
    return res;
}

std::pair<mlir::Value, std::string> MLIRGenImpl::gen_recv_struct_inner(const LExpr& recv) {
    namespace ec = lir_schema::expr;
    auto recv_ref = expr_ref_of(recv);
    auto recv_kind = recv_ref ? recv_ref.kind() : ec::Code(0);
    if (recv_kind == ec::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        auto sit = var_struct_.find(name);
        if (sit != var_struct_.end())
            return {get_struct_ptr(name), sit->second};
        // Local pointer-to-struct/datatype slots already record the concrete
        // LLVM aggregate type.  Use that as a fallback even if recv.type was
        // not preserved through lowering.
        auto lpit = var_local_ptrs_.find(name);
        if (lpit != var_local_ptrs_.end()) {
            if (auto sc = scope_.find(name); sc != scope_.end()) {
                if (recv.type && TypeRef(recv.type).kind() == LogosType::Kind::Ptr &&
                    TypeRef(recv.type).pointee() &&
                    (TypeRef(recv.type).pointee().kind() == LogosType::Kind::Struct ||
                     TypeRef(recv.type).pointee().kind() == LogosType::Kind::ZonedStruct)) {
                    return {sc->second, mlir_struct_key(TypeRef(recv.type).pointee())};
                }
                // If the receiver type is unavailable, still treat it as a
                // struct/datatype pointer using the recorded aggregate type.
                for (auto& [sname, info] : struct_types_) {
                    if (info.llvm_type == lpit->second)
                        return {sc->second, sname};
                }
            }
        }
        // Last resort: if the receiver is a method parameter/let binding that
        // already lives in scope as a pointer value, trust the caller-side
        // lowering and return it directly.  This covers `self: *const T` and
        // `self: &T` receivers even when the LIR type annotation got dropped.
        if (auto sc = scope_.find(name); sc != scope_.end()) {
            if (recv.type) {
                TypeRef tv{recv.type};
                if ((tv.kind() == LogosType::Kind::Ptr ||
                     tv.kind() == LogosType::Kind::Ref ||
                     tv.kind() == LogosType::Kind::MutRef) && tv.pointee() &&
                    (tv.pointee().kind() == LogosType::Kind::Struct ||
                     tv.pointee().kind() == LogosType::Kind::ZonedStruct)) {
                    return {sc->second, mlir_struct_key(tv.pointee())};
                }
            }
            // If this binding is a pointer slot (alloca(ptr)), load the value
            // directly and let later field/method code decide how to use it.
            if (var_local_ptrs_.count(name)) {
                auto ptr_val = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, ptr_type(), sc->second);
                auto it2 = std::find_if(struct_types_.begin(), struct_types_.end(),
                    [&](const auto& kv) { return kv.second.llvm_type == var_local_ptrs_[name]; });
                if (it2 != struct_types_.end())
                    return {ptr_val, it2->first};
                return {ptr_val, {}};
            }
        }
        // Check if this is a pointer-to-struct variable (e.g. *mut Point).
        // The logical type is Ptr/Ref/MutRef with pointee=Struct.
        if (recv.type) {
            TypeRef tv{recv.type};
            if (type_str(recv.type) == "AnyVal") {
                auto sc = scope_.find(name);
                if (sc != scope_.end())
                    return {sc->second, "AnyVal"};
            }
            if ((tv.kind() == LogosType::Kind::Ptr ||
                 tv.kind() == LogosType::Kind::Ref ||
                 tv.kind() == LogosType::Kind::MutRef) && tv.pointee()) {
                TypeRef inner = tv.pointee();
                if (type_str(inner) == "AnyVal") {
                    auto sc = scope_.find(name);
                    if (sc != scope_.end())
                        return {sc->second, "AnyVal"};
                }
                if (TypeRef(inner).kind() == LogosType::Kind::Struct ||
                    TypeRef(inner).kind() == LogosType::Kind::ZonedStruct) {
                    auto sc = scope_.find(name);
                    if (sc != scope_.end()) {
                        // Local let-bound pointer variables are stored in an alloca(slot).
                        // Parameters / SSA values already are the pointer value.
                        auto lpit = var_local_ptrs_.find(name);
                        if (lpit != var_local_ptrs_.end()) {
                            auto ptr_val = builder_.create<mlir::LLVM::LoadOp>(
                                loc_, ptr_type(), sc->second);
                            return {ptr_val, mlir_struct_key(inner)};
                        }
                        return {sc->second, mlir_struct_key(inner)};
                    }
                }
            }
        }
        // Fall through to the general receiver path instead of hard-failing.
        // Some pointer-like receivers are lowered as plain values in scope_
        // or through temp SSA values, and gen_expr(recv) can still recover them.
    }
    if (recv_kind == ec::Code::FieldRead) {
        lir_view::EFieldReadView fr{recv_ref};
        auto rec_le = lexpr_of(fr.receiver());
        if (!rec_le) return {nullptr, {}};
        std::string field(fr.field());
        auto [base_ptr, base_sname] = gen_recv_struct(*rec_le);
        if (!base_ptr || base_sname.empty()) return {nullptr, {}};
        auto it = struct_types_.find(base_sname);
        if (it == struct_types_.end()) return {nullptr, {}};
        auto& info = it->second;
        auto gep = gep_field(base_ptr, info, field);
        if (!gep) return {nullptr, {}};
        for (auto& f : info.fields) {
            if (f.name == field) {
                if (!f.struct_name.empty()) {
                    // Inline-embedded struct (LLVMStructType) OR a scalar-
                    // represented named type (e.g. AnyVal lowered as i32, RelPtr
                    // as a bare offset): the field lives IN-PLACE, so the GEP is
                    // already its address — return it directly so a chain access
                    // GEPs into the same slot (and &mut self methods mutate the
                    // original, not a copy). Only a genuine POINTER field is a
                    // separate object that must be loaded to descend.
                    if (!mlir::isa<mlir::LLVM::LLVMPointerType>(f.type))
                        return {gep, f.struct_name};
                    // Pointer field: load the pointer.
                    auto obj_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                       loc_, ptr_type(), gep);
                    return {obj_ptr, f.struct_name};
                }
                // struct_name not recorded in the LLVM struct registry: a
                // scalar-represented named type (e.g. AnyVal lowered as i32,
                // RelPtr as a bare offset) tags its field with an EMPTY
                // struct_name. Resolve the field's logical type from the
                // authoritative LIR struct def (same source gen_chain_field_write
                // uses), so a chain access can descend into it. The field lives
                // in-place (scalar) — the GEP is already its address.
                if (auto cdi = all_struct_defs_.find(base_sname);
                    cdi != all_struct_defs_.end()) {
                    for (auto& lf : cdi->second->fields) {
                        if (lf.name == field && lf.type) {
                            auto cn = concrete_struct_name(lf.type);
                            if (!cn.empty() && struct_types_.count(cn)) {
                                if (!mlir::isa<mlir::LLVM::LLVMPointerType>(f.type))
                                    return {gep, cn};
                                auto obj_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                                   loc_, ptr_type(), gep);
                                return {obj_ptr, cn};
                            }
                        }
                    }
                }
                std::fprintf(stderr, "mlir_gen: field '%s' is not a struct type\n",
                             field.c_str());
                return {nullptr, {}};
            }
        }
        return {nullptr, {}};
    }
    // Cluster A (K3/N2): a struct-typed element of an array — `r.cells[i]`
    // (array field) or `a[i]` of a struct array, possibly nested
    // (`g.rows[i].cells[j]`). Route through gen_lvalue_addr, which computes
    // the real element ADDRESS with the correct per-element struct stride
    // (place_slot_type). The general gen_expr(recv) path below returned a
    // wrong pointer here → field READ SIGSEGV (K3) and method-self pointing
    // at garbage (N2). The element's struct name comes from recv.type.
    if (recv_kind == ec::Code::IndexRead || recv_kind == ec::Code::TupleIndex) {
        // Peel a `&`/`&mut`/`*` wrapper: a `&self` method call auto-refs its
        // receiver, so `hs[i]` arrives typed `&Big`. The element address that
        // gen_lvalue_addr computes IS that reference, so treat it as a struct
        // receiver regardless of an outer ref annotation.
        TypeRef st = recv.type;
        if (st && (TypeRef(st).kind() == LogosType::Kind::Ref ||
                   TypeRef(st).kind() == LogosType::Kind::MutRef ||
                   TypeRef(st).kind() == LogosType::Kind::Ptr) &&
            TypeRef(st).pointee())
            st = TypeRef(st).pointee();
        if (st && (TypeRef(st).kind() == LogosType::Kind::Struct ||
                   TypeRef(st).kind() == LogosType::Kind::ZonedStruct)) {
            if (auto addr = gen_lvalue_addr(recv_ref))
                return {addr, mlir_struct_key(st)};
        }
    }
    // General case: evaluate expression, derive type name from LExpr.type
    auto ptr = gen_expr(recv);
    if (!ptr) return {nullptr, {}};
    // If the result is an aggregate struct (by-value return), spill to alloca.
    // AnyVal is a scalar-like 4-byte slot, not a by-value aggregate receiver.
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ptr.getType()) &&
        (!recv.type || type_str(recv.type) != "AnyVal"))
        ptr = spill_to_alloca(ptr);
    if (recv.type) {
        TypeRef t = recv.type;
        // Strip one level of pointer/reference to get the struct type
        TypeRef tv{t};
        if ((tv.kind() == LogosType::Kind::Ptr ||
             tv.kind() == LogosType::Kind::Ref ||
             tv.kind() == LogosType::Kind::MutRef) && tv.pointee()) t = tv.pointee();
        tv = TypeRef{t};
        if (tv.kind() == LogosType::Kind::Struct ||
            tv.kind() == LogosType::Kind::ZonedStruct)
            return {ptr, mlir_struct_key(t)};
    }
    std::fprintf(stderr, "mlir_gen: unsupported receiver kind for struct access\n");
    return {nullptr, {}};
}

mlir::Value MLIRGenImpl::gen_struct_lit(lir_view::EStructLitView v) {
    namespace ec = lir_schema::expr;
    std::string name(v.name());
    if (name == "AnyVal") {
        // AnyVal is lowered as a scalar i32 everywhere in MLIR.
        // Hermes source still spells it as a struct literal (`AnyVal { raw: ... }`),
        // so treat that syntax as a constructor for the raw slot value.
        std::vector<std::pair<std::string, lir_view::ExprRef>> fields;
        v.each_field([&](std::string_view fn, lir_view::ExprRef val){
            fields.emplace_back(std::string(fn), val);
        });
        if (fields.size() != 1 || fields.front().first != "raw") {
            std::fprintf(stderr, "mlir_gen: AnyVal literal expects a single 'raw' field\n");
            return nullptr;
        }
        auto* le = lexpr_of(fields.front().second); if (!le) return nullptr;
        auto raw = gen_expr(*le);
        if (!raw) return nullptr;
        return coerce_numeric(raw, builder_.getI32Type());
    }
    // Try the pkg-qualified key first (via the lit's TypeRef), fall back to bare.
    std::string lookup_key;
    if (TypeRef lt = v.self.type(pool_impl());
        lt && (lt.kind() == LogosType::Kind::Struct ||
               lt.kind() == LogosType::Kind::ZonedStruct))
        lookup_key = mlir_struct_key(lt);
    auto sit = (!lookup_key.empty() && struct_types_.count(lookup_key))
        ? struct_types_.find(lookup_key)
        : struct_types_.find(name);
    if (sit == struct_types_.end()) {
        std::fprintf(stderr, "mlir_gen: unknown struct '%s' (qualified '%s')\n",
                     name.c_str(), lookup_key.c_str());
        return nullptr;
    }
    auto& info  = sit->second;
    auto alloca = create_entry_alloca(info.llvm_type);
    bool ok = true;
    v.each_field([&](std::string_view fn_sv, lir_view::ExprRef fval){
        if (!ok) return;
        std::string fname(fn_sv);
        // Find field metadata.
        const FieldInfo* fi = nullptr;
        for (auto& f : info.fields) if (f.name == fname) { fi = &f; break; }

        auto gep = gep_field(alloca, info, fname);
        if (!gep) { ok = false; return; }

        // Special case: if the field is an inline array (LLVMArrayType) and
        // the initialiser is an EArrLit, copy elements one-by-one into the
        // struct field instead of storing a pointer returned by gen_arr_lit.
        auto arr_llvm = fi ? mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(fi->type)
                           : mlir::LLVM::LLVMArrayType{};
        if (arr_llvm && fval.kind() == ec::Code::ArrLit) {
            lir_view::EArrLitView arr_view{fval};
            auto elem_type = arr_llvm.getElementType();
            // Cluster A (K3/N2): an AGGREGATE element (inline struct or nested
            // array) is pointer-represented by gen_expr; storing that 8-byte
            // pointer into the (wider) inline slot left only the pointer bits
            // in the array and 16 bytes of garbage, so reads through the real
            // element stride saw pointer bits as the field value (SIGSEGV /
            // miscompile). MEMCPY the element VALUE into the slot — mirrors
            // gen_arr_lit's struct-element path.
            bool elem_is_agg = elem_type &&
                (mlir::isa<mlir::LLVM::LLVMStructType>(elem_type) ||
                 mlir::isa<mlir::LLVM::LLVMArrayType>(elem_type));
            uint64_t n = arr_view.count();
            for (uint64_t i = 0; i < n; ++i) {
                auto er = arr_view.elem(i);
                auto* le = lexpr_of(er); if (!le) { ok = false; return; }
                auto val = gen_expr(*le);
                if (!val) { ok = false; return; }
                llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
                auto elem_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), arr_llvm, gep, idx);
                if (elem_is_agg && val.getType() == ptr_type()) {
                    auto dl = mlir::DataLayout::closest(
                        builder_.getInsertionBlock()->getParentOp());
                    auto bytes = (int64_t)dl.getTypeSize(elem_type);
                    auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                        loc_, builder_.getI64Type(),
                        builder_.getI64IntegerAttr(bytes));
                    builder_.create<mlir::LLVM::MemcpyOp>(
                        loc_, elem_gep, val, sz, /*isVolatile=*/false);
                } else {
                    val = coerce_numeric(val, elem_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, elem_gep);
                }
            }
            return;
        }

        auto* fle = lexpr_of(fval); if (!fle) { ok = false; return; }
        // An uninhabited (`!`-typed) field — e.g. a PhantomData<!> marker in an
        // iterator struct monomorphised for the never type — has no runtime
        // value. gen_expr of a never-typed initialiser yields a malformed Value
        // whose getType() SIGSEGVs below; the field slot is zero-size anyway, so
        // skip it. (Surfaced by hoisting a generic match-temp scrutinee into an
        // Option<!> instantiation.)
        if (fle->type && TypeRef(fle->type).kind() == LogosType::Kind::Never) return;
        auto val = gen_expr(*fle);
        if (!val) { ok = false; return; }
        // &dyn Trait field — value may be a concrete `&S` / `&mut S`; build the
        // fat-pointer slot (data+vtable) before storing the handle into the field.
        if (fi && !fi->trait_name.empty() && val.getType() == ptr_type()) {
            TypeRef vt(fle->type);
            if (vt && vt.kind() != LogosType::Kind::TraitObject) {
                TypeRef pointee = vt;
                if (pointee && (pointee.kind() == LogosType::Kind::Ref ||
                                pointee.kind() == LogosType::Kind::MutRef ||
                                pointee.kind() == LogosType::Kind::Ptr) &&
                    pointee.pointee())
                    pointee = pointee.pointee();
                std::string src_struct;
                if (pointee && (pointee.kind() == LogosType::Kind::Struct ||
                                pointee.kind() == LogosType::Kind::ZonedStruct))
                    src_struct = concrete_struct_name(pointee);
                if (!src_struct.empty()) {
                    if (auto fat = coerce_to_dyn(val, fi->trait_name, src_struct))
                        val = fat;
                }
            }
        }
        // Coerce scalar literals to the field's declared type (e.g. IntLit→i64, FloatLit→f32).
        if (fi && fi->type && !mlir::isa<mlir::LLVM::LLVMArrayType>(fi->type)) {
            if (mlir::isa<mlir::LLVM::LLVMStructType>(fi->type) &&
                val.getType() == ptr_type()) {
                // Inline struct field: load the aggregate value from the alloca pointer.
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, fi->type, val);
            } else {
                val = coerce_numeric(val, fi->type);
            }
        }
        // A field value that coerced to null is an uninhabited / zero-size
        // field (e.g. a PhantomData<!> marker in an iterator struct
        // monomorphised for the never type, or any `!`-typed field): there is
        // no value to materialise, so skip the store. Without this the next
        // `val.getType()` deref SIGSEGVs (surfaced by hoisting a generic
        // match-temp scrutinee into an Option<!> instantiation).
        if (!val) return;
        // Inline array field initialized from a non-ArrLit expression
        // (e.g. local var of type `[T; N]`): val is a *pointer* to the
        // source array, not the array value. Memcpy the data into the
        // field slot instead of storing the pointer bits.
        if (fi && mlir::isa<mlir::LLVM::LLVMArrayType>(fi->type) &&
            val.getType() == ptr_type()) {
            auto arr_t = mlir::cast<mlir::LLVM::LLVMArrayType>(fi->type);
            auto dl    = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
            uint64_t sz = dl.getTypeSize(arr_t);
            auto sz_val = builder_.create<mlir::arith::ConstantIntOp>(
                loc_, (int64_t)sz, 64);
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, sz_val, false);
            return;
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
    });
    return ok ? alloca : nullptr;
}

// ---------------------------------------------------------------------------
// Array helpers
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::get_subscript_ptr(const std::string& name) {
    auto it = scope_.find(name);
    if (it == scope_.end()) {
        if (std::getenv("LOGOS_MLIRGEN_DEBUG_UNDEF"))
            std::fprintf(stderr, "mlir_gen: undefined '%s'\n", name.c_str());
        return nullptr;
    }
    return it->second;
}

mlir::Type MLIRGenImpl::subscript_elem_type(const std::string& name) {
    auto it = var_elem_types_.find(name);
    if (it != var_elem_types_.end()) return it->second;
    auto sit = var_subscript_.find(name);
    if (sit != var_subscript_.end()) return sit->second;
    return builder_.getI32Type();
}

mlir::Value MLIRGenImpl::gen_arr_lit(lir_view::EArrLitView v, mlir::Type elem_type,
                                     TypeRef logos_elem) {
    uint64_t n = v.count();
    auto arr_type = mlir::LLVM::LLVMArrayType::get(elem_type, n);
    auto alloca   = create_entry_alloca(arr_type);
    bool elem_is_array  = elem_type && mlir::isa<mlir::LLVM::LLVMArrayType>(elem_type);
    bool elem_is_struct = elem_type && mlir::isa<mlir::LLVM::LLVMStructType>(elem_type);
    // N4: `[&dyn Trait; N]` — the array element is a trait object (pointer to a
    // fat {data, vtable} slot). The element source is a concrete `&Concrete`,
    // so each element needs the same unsize coercion gen_struct_lit applies to
    // a `&dyn` field — without it the raw thin `&Concrete` is stored and a
    // later `arr[i].method()` reads a garbage vtable → SIGSEGV.
    TypeRef dyn_trait_elem = logos_elem;
    if (dyn_trait_elem && (TypeRef(dyn_trait_elem).kind() == LogosType::Kind::Ref ||
                           TypeRef(dyn_trait_elem).kind() == LogosType::Kind::MutRef ||
                           TypeRef(dyn_trait_elem).kind() == LogosType::Kind::Ptr) &&
        TypeRef(dyn_trait_elem).pointee())
        dyn_trait_elem = TypeRef(dyn_trait_elem).pointee();
    bool elem_is_dyn = dyn_trait_elem &&
        TypeRef(dyn_trait_elem).kind() == LogosType::Kind::TraitObject;
    for (uint64_t i = 0; i < n; ++i) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_type, alloca, idx);
        auto er = v.elem(i);
        auto* le = lexpr_of(er); if (!le) return nullptr;
        if (elem_is_dyn) {
            auto val = gen_expr(*le);
            if (!val) return nullptr;
            // Peel the source `&Concrete` to its struct, build the fat pointer.
            TypeRef vt(le->type);
            if (vt && vt.kind() != LogosType::Kind::TraitObject) {
                TypeRef pointee = vt;
                if (pointee && (pointee.kind() == LogosType::Kind::Ref ||
                                pointee.kind() == LogosType::Kind::MutRef ||
                                pointee.kind() == LogosType::Kind::Ptr) &&
                    pointee.pointee())
                    pointee = pointee.pointee();
                std::string src_struct;
                if (pointee && (pointee.kind() == LogosType::Kind::Struct ||
                                pointee.kind() == LogosType::Kind::ZonedStruct))
                    src_struct = concrete_struct_name(pointee);
                if (!src_struct.empty() && val.getType() == ptr_type()) {
                    if (auto fat = coerce_to_dyn(
                            val, std::string(TypeRef(dyn_trait_elem).trait_name()),
                            src_struct))
                        val = fat;
                }
            }
            // `&dyn` array elements are inline 16-byte {data,vtable} fat pairs
            // (uniform fat model — matches logos_to_mlir([&dyn;N]) = [N x {ptr,
            // ptr}] and layout_of=16). `val` is a pointer to the fat pair, so
            // memcpy the 16 bytes INTO the slot (an 8-byte store would leave the
            // vtable half uninitialised → garbage dispatch).
            auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                loc_, builder_.getI64Type(), builder_.getI64IntegerAttr(16));
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, gep, val, sz, /*isVolatile=*/false);
            continue;
        }
        if (elem_is_struct) {
            // Element is an inline LLVM struct slot. gen_expr may return
            // either a pointer to the source struct (alloca/struct_lit) or
            // the struct value itself (function return). Either way, write
            // sizeof(struct) bytes into the slot so the *value* lives in
            // the array — this is what makes returning [Struct;N] safe.
            auto src = gen_expr(*le);
            if (!src) return nullptr;
            if (src.getType() == ptr_type()) {
                auto dl = mlir::DataLayout::closest(builder_.getInsertionBlock()->getParentOp());
                auto bytes = (int64_t)dl.getTypeSize(elem_type);
                auto sz = builder_.create<mlir::LLVM::ConstantOp>(
                    loc_, builder_.getI64Type(),
                    builder_.getI64IntegerAttr(bytes));
                builder_.create<mlir::LLVM::MemcpyOp>(
                    loc_, gep, src, sz, /*isVolatile=*/false);
            } else {
                builder_.create<mlir::LLVM::StoreOp>(loc_, src, gep);
            }
            continue;
        }
        if (elem_is_array) {
            auto inner_ptr = gen_expr(*le);
            if (!inner_ptr) return nullptr;
            auto inner_arr_type = mlir::cast<mlir::LLVM::LLVMArrayType>(elem_type);
            auto inner_elem_type = inner_arr_type.getElementType();
            for (uint64_t j = 0; j < inner_arr_type.getNumElements(); ++j) {
                llvm::SmallVector<mlir::LLVM::GEPArg> inner_idx{int32_t(j)};
                auto src_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), inner_elem_type, inner_ptr, inner_idx);
                auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, inner_elem_type, src_gep);
                auto dst_gep = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), inner_elem_type, gep, inner_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, val, dst_gep);
            }
        } else {
            auto val = gen_expr(*le);
            if (!val) return nullptr;
            val = coerce_numeric(val, elem_type);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        }
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const LProgram& prog) noexcept
{
    auto t0 = std::chrono::steady_clock::now();
    MLIRGenImpl gen(ctx);
    auto t_ctor = std::chrono::steady_clock::now();
    auto mod = gen.generate(prog);
    auto t_gen = std::chrono::steady_clock::now();
    if (const char* e = std::getenv("LOGOS_MLIR_PHASE_TIMING"); e && e[0] && e[0] != '0') {
        auto ctor_us = std::chrono::duration_cast<std::chrono::microseconds>(t_ctor - t0).count();
        auto gen_us  = std::chrono::duration_cast<std::chrono::microseconds>(t_gen - t_ctor).count();
        std::fprintf(stderr, "[mlir-outer] ctor=%ldus generate=%ldus\n",
                     (long)ctor_us, (long)gen_us);
    }
    return mod;
}

} // namespace logos::compiler
