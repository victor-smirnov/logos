// Logos project — https://github.com/victor-smirnov/logos
//
// mono_scan.cpp — Function scanning and generic call enqueueing.
//
// Phase 3d: walks the L-IR Hermes mirror via lir_view types instead of the
// std::variant tree. Mirror entries for the function being scanned must be
// emitted via lir_mirror_emit_function before scan_fn runs — call-site
// ordering in mono.cpp / mono_clone.cpp guarantees this.

#include "mono_impl.hpp"

#include <cstdio>
#include <cstdlib>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/lir_mirror.hpp>

namespace logos::compiler {

namespace {

using ECode = lir_schema::expr::Code;
using SCode = lir_schema::stmt::Code;

} // namespace

void Mono::scan_fn(lir_view::FunctionView fn) {
    auto b = fn.body();
    if (!b) return;
    out_.type_pool.arena_or_init();   // ensure the out type-pool is live before scan
    scan_block(b);
}

void Mono::scan_block(lir_view::BlockRef b) {
    if (!b) return;
    b.each_stmt([&](lir_view::StmtRef s) { scan_stmt(s); });
}

void Mono::scan_stmt(lir_view::StmtRef s) {
    if (!s) return;
    switch (s.kind()) {
    case SCode::Let:
        scan_expr(lir_view::SLetView{s}.value());
        break;
    case SCode::Assign:
        scan_expr(lir_view::SAssignView{s}.value());
        break;
    case SCode::Return: {
        if (auto v = lir_view::SReturnView{s}.value()) scan_expr(v);
        break;
    }
    case SCode::If: {
        lir_view::SIfView v{s};
        scan_expr(v.cond());
        scan_block(v.then_block());
        scan_block(v.else_block());
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{s};
        scan_expr(v.cond());
        scan_block(v.body());
        break;
    }
    case SCode::For: {
        lir_view::SForView v{s};
        scan_expr(v.lo());
        scan_expr(v.hi());
        scan_block(v.body());
        break;
    }
    case SCode::Loop:
        scan_block(lir_view::SLoopView{s}.body());
        break;
    case SCode::Block:
        scan_block(lir_view::SBlockView{s}.body());
        break;
    case SCode::FieldWrite:
        scan_expr(lir_view::SFieldWriteView{s}.value());
        break;
    case SCode::DerefFieldWrite:
        scan_expr(lir_view::SDerefFieldWriteView{s}.value());
        break;
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{s};
        scan_expr(v.index());
        scan_expr(v.value());
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{s};
        scan_expr(v.index());
        scan_expr(v.value());
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{s};
        scan_expr(v.ptr());
        scan_expr(v.value());
        break;
    }
    case SCode::TupleWrite:
        scan_expr(lir_view::STupleWriteView{s}.value());
        break;
    case SCode::ChainFieldWrite:
        scan_expr(lir_view::SChainFieldWriteView{s}.value());
        break;
    case SCode::ExprStmt:
        scan_expr(lir_view::SExprStmtView{s}.expr());
        break;
    case SCode::Match: {
        lir_view::SMatchView v{s};
        scan_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) scan_expr(g);
            scan_block(arm.body());
        });
        break;
    }
    case SCode::ForEach: {
        lir_view::SForEachView v{s};
        scan_expr(v.iter());
        scan_block(v.body());
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{s};
        scan_expr(v.scrut());
        scan_block(v.else_block());
        break;
    }
    case SCode::Break: {
        if (auto v = lir_view::SBreakView{s}.value()) scan_expr(v);
        break;
    }
    case SCode::Continue:
    case SCode::Drop:
        // No sub-expressions to scan.
        break;
    }
}

void Mono::scan_expr(lir_view::ExprRef e) {
    if (!e) return;
    switch (e.kind()) {
    case ECode::Call: {
        lir_view::ECallView v{e};
        if (v.has_type_args()) {
            // Post-substitution generic call: callee is already mangled.
            enqueue_if_needed(std::string(v.callee()), v.type_args(out_.type_pool.impl()));
        } else if (!entry_points_.empty()) {
            // Prune mode: a direct (non-generic) call to a free fn makes that
            // fn reachable. No-op for callees that aren't non-generic free fns
            // (externs, methods, generic instances) — those flow through the
            // method/generic worklists or a linked archive.
            enqueue_free_fn(std::string(v.callee()));
        }
        if (lazy_methods_) {
            // L1.5: sema may lower `recv.method()` directly to ECall
            // `[pkg.]<Concrete>__<method>[__f__|__g__sig]`. Recover the
            // (concrete struct, method short-name) pair and enqueue.
            std::string callee{v.callee()};
            if (auto sep = callee.find("__"); sep != std::string::npos) {
                std::string concrete = callee.substr(0, sep);
                std::string method_part = callee.substr(sep + 2);
                // Strip `__f__sig` / `__g__sig` to get the user-facing short name.
                if (auto sig = method_part.find("__f__"); sig != std::string::npos)
                    method_part.resize(sig);
                else if (auto sig = method_part.find("__g__"); sig != std::string::npos)
                    method_part.resize(sig);
                auto cit = concrete_struct_types_.find(concrete);
                if (cit != concrete_struct_types_.end())
                    enqueue_method_inst(cit->second, method_part);
                else if (concrete.find("$G") != std::string::npos)
                    deferred_method_enqueues_.emplace_back(std::move(concrete),
                                                           std::move(method_part));
            }
        }
        v.each_arg([&](lir_view::ExprRef a) { scan_expr(a); });
        break;
    }
    case ECode::BinOp: {
        lir_view::EBinOpView v{e};
        scan_expr(v.lhs());
        scan_expr(v.rhs());
        break;
    }
    case ECode::Unary:
        scan_expr(lir_view::EUnaryView{e}.operand());
        break;
    case ECode::Deref:
        scan_expr(lir_view::EDerefView{e}.operand());
        break;
    case ECode::FieldRead:
        scan_expr(lir_view::EFieldReadView{e}.receiver());
        break;
    case ECode::IndexRead: {
        lir_view::EIndexReadView v{e};
        scan_expr(v.receiver());
        scan_expr(v.index());
        break;
    }
    case ECode::MethodCall: {
        lir_view::EMethodCallView v{e};
        scan_expr(v.receiver());
        v.each_arg([&](lir_view::ExprRef a) { scan_expr(a); });
        if (lazy_methods_) {
            // L1.1: hook for lazy method instantiation. Default off; only fires
            // when LOGOS_LAZY_METHODS=1. Resolve the receiver type to a concrete
            // generic struct and enqueue this method's instance for codegen.
            auto rt = v.receiver().type(out_.type_pool.impl());
            while (rt && (TypeRef(rt).kind() == LogosType::Kind::Ptr ||
                          TypeRef(rt).kind() == LogosType::Kind::Ref ||
                          TypeRef(rt).kind() == LogosType::Kind::MutRef) &&
                   TypeRef(rt).pointee())
                rt = TypeRef(rt).pointee();
            enqueue_method_inst(rt, std::string(v.method()));
        }
        break;
    }
    case ECode::StructLit:
        lir_view::EStructLitView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { scan_expr(fv); });
        break;
    case ECode::ArrLit:
        lir_view::EArrLitView{e}.each_elem(
            [&](lir_view::ExprRef el) { scan_expr(el); });
        break;
    case ECode::Cast: {
        lir_view::ECastView cv{e};
        scan_expr(cv.operand());
        // Prune mode: a Hermes container cast (`&[T] as <I32>[]`, comprehension
        // `@{...}` with captures) lowers to a call to a named builder fn
        // (hermes_build_map_*/hermes_build_arr_*) — see mlir_gen's ECast path.
        // It's not a Call node, so make it reachable explicitly or the JIT
        // calls a forward-decl-only symbol → NULL.
        if (!entry_points_.empty()) {
            auto bf = cv.hermes_build_fn();
            if (!bf.empty()) enqueue_free_fn(std::string(bf));
        }
        // `&prim as &dyn Trait` / `box prim as Box<dyn Trait>`: record the
        // PRIMITIVE→trait coercion so the post-drain pass can instantiate the
        // blanket impl for that primitive (the eager blanket pass skips
        // primitives; eagerly cloning ALL of them breaks integer-bodied blankets
        // on f32/f64 — so we target only actually-coerced primitives).
        TypeRef tgt = e.type(out_.type_pool.impl());
        if (tgt && TypeRef(tgt).kind() == LogosType::Kind::Ptr && TypeRef(tgt).pointee())
            tgt = TypeRef(tgt).pointee();
        if (tgt && TypeRef(tgt).kind() == LogosType::Kind::TraitObject) {
            // Mirror the ECast dyn-coercion's Self-type derivation EXACTLY so the
            // blanket instance name matches the vtable key:
            //   • `&X as &dyn` / `*X as *dyn` → Self = the pointee X (NO Box
            //     unwrap — `&Box<i64> as &dyn` keys on `Box$G1$i64`).
            //   • `box X as Box<dyn>` (source is a Box<…> VALUE) → Self = the
            //     boxed type (unwrap ONE Box — `box i64 → Box<i64>` keys on `i64`).
            TypeRef src = cv.operand().type(out_.type_pool.impl());
            if (src && (TypeRef(src).kind() == LogosType::Kind::Ptr ||
                        TypeRef(src).kind() == LogosType::Kind::Ref ||
                        TypeRef(src).kind() == LogosType::Kind::MutRef) &&
                TypeRef(src).pointee()) {
                src = TypeRef(src).pointee();   // ref/ptr source: pointee IS Self
            } else if (is_stdlib_box(src) &&
                       TypeRef(src).type_args().size() == 1) {
                src = TypeRef(src).type_args()[0];  // box-value source: unwrap once
            }
            // Record the concrete coercion target keyed by its name. Primitives
            // (i64/bool/…) and GENERIC STRUCT INSTANTIATIONS (Box$G1$i64) are the
            // cases the eager blanket pass misses; plain non-generic structs are
            // also recorded but the supplementary pass dedups them via done_.
            if (src) {
                auto k = TypeRef(src).kind();
                std::string nm;
                if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
                    nm = concrete_struct_name(src);
                else if (k != LogosType::Kind::TraitObject &&
                         k != LogosType::Kind::TypeVar && k != LogosType::Kind::Error)
                    nm = type_str(src);   // primitives, etc.
                if (!nm.empty())
                    dyn_coerced_targets_[std::string(TypeRef(tgt).trait_name())]
                        .emplace(std::move(nm), src);
            }
        }
        break;
    }
    case ECode::IfExpr: {
        lir_view::EIfExprView v{e};
        scan_expr(v.cond());
        scan_expr(v.then_val());
        scan_expr(v.else_val());
        break;
    }
    case ECode::TupleLit:
        lir_view::ETupleLitView{e}.each_elem(
            [&](lir_view::ExprRef el) { scan_expr(el); });
        break;
    case ECode::TupleIndex:
        scan_expr(lir_view::ETupleIndexView{e}.receiver());
        break;
    case ECode::ClosureBox: {
        // Walk the captured closure body's statements.
        scan_block(lir_view::EClosureBoxView{e}.body());
        break;
    }
    case ECode::HermesLit:
        // Captured runtime sub-expressions of a `@{...}` literal (e.g. the
        // values folded into the blob) may themselves contain calls; scan them
        // so prune mode keeps their callees. (The builder fn for the capture
        // path is enqueued at the wrapping ECast — see above.)
        lir_view::EHermesLitView{e}.each_capture_expr(
            [&](lir_view::ExprRef c) { scan_expr(c); });
        break;
    case ECode::ClosureCall: {
        lir_view::EClosureCallView v{e};
        scan_expr(v.callee());
        v.each_arg([&](lir_view::ExprRef a) { scan_expr(a); });
        break;
    }
    case ECode::FnPtrCall: {
        lir_view::EFnPtrCallView v{e};
        scan_expr(v.callee());
        v.each_arg([&](lir_view::ExprRef a) { scan_expr(a); });
        break;
    }
    case ECode::SliceLit: {
        lir_view::ESliceLitView v{e};
        scan_expr(v.base());
        scan_expr(v.len());
        break;
    }
    case ECode::SliceIndex: {
        lir_view::ESliceIndexView v{e};
        scan_expr(v.slice());
        scan_expr(v.index());
        break;
    }
    case ECode::SliceLen:
        scan_expr(lir_view::ESliceLenView{e}.slice());
        break;
    case ECode::SlicePtr:
        scan_expr(lir_view::ESlicePtrView{e}.slice());
        break;
    case ECode::EnumLitData:
        lir_view::EEnumLitDataView{e}.each_payload(
            [&](lir_view::ExprRef p) { scan_expr(p); });
        break;
    case ECode::FormatCall: {
        lir_view::EFormatCallView v{e};
        scan_expr(v.fmt());
        v.each_arg([&](lir_view::ExprRef a) { scan_expr(a); });
        break;
    }
    case ECode::Try:
        scan_expr(lir_view::ETryView{e}.inner());
        break;
    case ECode::MatchExpr: {
        lir_view::EMatchExprView v{e};
        scan_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) scan_expr(g);
            if (auto val = arm.value()) scan_expr(val);
        });
        break;
    }
    case ECode::BlockExpr: {
        lir_view::EBlockExprView v{e};
        scan_block(v.block());
        if (auto r = v.result()) scan_expr(r);
        break;
    }
    case ECode::AddrOfTemp:
        // CP-cm-17 leg (b) — sema's autoref-on-rvalue wraps the inner
        // expression in AddrOfTemp when an outer `&self` method is
        // chained off an rvalue Call (`make_opt::<i32>(7).is_some()`).
        // Without recursing into the operand, mono never sees the
        // inner generic Call's callee/type_args and the specialisation
        // is never enqueued → mlir-gen forward-decl reference dangles.
        scan_expr(lir_view::EAddrOfTempView{e}.inner());
        break;
    // Leaf / no-recurse variants.
    case ECode::LitInt:
    case ECode::LitFloat:
    case ECode::LitBool:
    case ECode::LitStr:
    case ECode::VarRef:
    case ECode::EnumLit:
    case ECode::AddrOf:
    case ECode::PackExpand:
    case ECode::SizeOf:
    case ECode::AlignOf:
    case ECode::GenericRef:  // rewritten to VarRef during subst_expr; never reaches here
    case ECode::TypeCodeOf:
    case ECode::PtrArith:
    case ECode::PtrDiff:
    case ECode::ReflectOf:
        break;
    }
}


// ── Pattern matching (static, inline in mono_impl.hpp) ───────────

// Return the most specific specialisation that matches type_args, or nullptr.
lir_view::FunctionView Mono::find_best_spec(
    const std::string& base_name,
    const std::vector<TypeRef>& type_args) {
    auto sit = specs_.find(base_name);
    if (sit == specs_.end()) {
        // Strip mangling: pkg`$` prefix and the `__f__`/`__g__` suffix.
        // lower_spec_fn registers specs under the bare raw name, while
        // generic templates carry pkg + `__g__sig`. The fallback unifies
        // the two namespaces for spec lookup.
        std::string raw = base_name;
        if (auto p = raw.find("__g__"); p != std::string::npos)
            raw.resize(p);
        else if (auto p = raw.find("__f__"); p != std::string::npos)
            raw.resize(p);
        if (auto d = raw.rfind('$'); d != std::string::npos)
            raw = raw.substr(d + 1);
        sit = specs_.find(raw);
    }
    if (sit == specs_.end()) return {};

    auto* fbs_pool = out_.type_pool.impl();
    lir_view::FunctionView best;
    std::vector<int>      best_vec;
    bool                  ambiguous = false;

    for (auto spec : sit->second) {
        auto sp = spec.spec_patterns(fbs_pool);
        if (sp.size() != type_args.size()) continue;
        SubstMap dummy;
        bool ok = true;
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type(type_args[i], sp[i], dummy)) {
                ok = false; break;
            }
        }
        if (!ok) continue;
        auto svec = specificity_vec(sp);
        if (!best || svec > best_vec) {
            best_vec  = svec;
            best      = spec;
            ambiguous = false;
        } else if (svec == best_vec) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("ambiguous specializations for function '{}'", base_name),
            "", 0});
    }
    return best;
}


// ── Enqueue an instantiation if needed ───────────────────────

void Mono::enqueue_if_needed(const std::string& mangled_callee,
                       const std::vector<TypeRef>& type_args) {
    if (done_.count(mangled_callee)) return;

    // Find the base name by checking templates_ and specs_.
    std::string orig_name;
    for (auto& [tname, _] : templates_)
        if (mangle(tname, type_args) == mangled_callee) { orig_name = tname; break; }
    if (orig_name.empty())
        for (auto& [sname, _] : specs_)
            if (mangle(sname, type_args) == mangled_callee) { orig_name = sname; break; }
    // Struct-method templates with method-level tparams: sema/finish_generic_call
    // emits the call with a callee name in template form `Struct__method[__g__sig]`
    // and type_args that include BOTH the struct-level tparams AND the method-
    // level tparams. enqueue_method_inst only handles struct-tparams; route
    // through a dedicated method-level enqueue here so the right specialisation
    // gets cloned.
    if (orig_name.empty()) {
        // Split mangled_callee at the first `__` to find Struct + method-tail.
        auto sep = mangled_callee.find("__");
        if (sep != std::string::npos) {
            std::string struct_part = mangled_callee.substr(0, sep);
            std::string method_tail = mangled_callee.substr(sep + 2);
            // Strip `__g__sig` / `__f__sig` suffix and the trailing
            // mangled type-args (`__i32__...`) to reach the method short name.
            // We don't know the suffix length exactly here, but the
            // struct_method_templates lookup is by short name only.
            std::string method_name = method_tail;
            if (auto p = method_name.find("__g__"); p != std::string::npos)
                method_name.resize(p);
            else if (auto p = method_name.find("__f__"); p != std::string::npos)
                method_name.resize(p);
            else if (auto p = method_name.find("__"); p != std::string::npos)
                method_name.resize(p);
            // Pkg-qualified struct (e.g. `std.lang.iter.SliceIter`): the call
            // emit may carry only the bare struct name. struct_method_templates_
            // keys both pkg-qualified and bare.
            auto* smt_inner = find_struct_method_templates_unguarded(struct_part);
            auto* mscan_pool = out_.type_pool.impl();
            lir_view::FunctionView tmpl;
            if (smt_inner) {
                auto mit = smt_inner->find(method_name);
                if (mit == smt_inner->end()) {
                    // Try with `__g__sig` variants.
                    for (auto& [k, _] : *smt_inner) {
                        if (k == method_name ||
                            (k.size() > method_name.size() + 5 &&
                             k.compare(0, method_name.size(), method_name) == 0 &&
                             k.compare(method_name.size(), 5, "__g__") == 0)) {
                            mit = smt_inner->find(k);
                            break;
                        }
                    }
                }
                if (mit != smt_inner->end())
                    tmpl = mit->second;
            }
            if (!tmpl) {
                // CP-cm-14 extension: primitive-receiver impl methods
                // (e.g. `impl Sum<i32> for i32`) live in templates_ as
                // free fns, not in struct_method_templates_. Walk
                // templates_ for `[pkg.]<base>__<method>__g__*`.
                std::string base_p = struct_part + "__" + method_name + "__g__";
                std::string base_p_bare = method_name + "__g__";
                std::string dot_path = "." + struct_part + "__" + method_name + "__g__";
                std::string dot_bare = "." + base_p_bare;
                for (auto& [kn, fp] : templates_) {
                    if (kn.rfind(base_p, 0) == 0 ||
                        kn.find(dot_path) != std::string::npos) {
                        tmpl = fp;
                        break;
                    }
                }
                if (!tmpl) {
                    // Strip pkg-prefix from struct_part and retry the bare form.
                    auto dot = struct_part.rfind('.');
                    if (dot != std::string::npos) {
                        std::string base_only = struct_part.substr(dot + 1);
                        std::string bare_p = base_only + "__" + method_name + "__g__";
                        std::string bare_dot = "." + bare_p;
                        for (auto& [kn, fp] : templates_) {
                            if (kn.rfind(bare_p, 0) == 0 ||
                                kn.find(bare_dot) != std::string::npos) {
                                tmpl = fp;
                                break;
                            }
                        }
                    }
                }
            }
            if (tmpl) {
                    {
                    // Find the struct template to split type_args into struct-
                    // vs method-tparam slices. Primitive receivers won't have
                    // a struct_templates_ entry; treat them as zero-tparam.
                    auto stt_ptr = find_struct_template_unguarded(struct_part);
                    {
                        std::vector<std::string> sd_tpars;
                        if (stt_ptr.valid())
                            for (auto tp : stt_ptr.type_params())
                                sd_tpars.push_back(std::string(tp.name()));
                        size_t n_struct = sd_tpars.size();
                        // Cross-package impl method on a generic receiver: the
                        // impl's type-params (e.g. T in `impl<T> Pin<&T>`) are
                        // NOT carried in the method's own FunctionDraft.type_params
                        // (they would have come from the struct-template path,
                        // which a foreign-package impl bypasses). The body is in
                        // terms of those impl params, and the call's type_args
                        // are exactly them in pattern order. Recover the names
                        // from the impl-target pattern and bind by name —
                        // otherwise type_args get mis-bound to the struct's own
                        // tparams (wrong name AND wrong value: `P=i64` instead
                        // of `T=i64`) and the body's T stays unresolved, lowering
                        // to an `llvm.unreachable` stub. See
                        // docs/track3-gaps/cross-package-impl-method-mono.md.
                        std::vector<std::string> impl_tvs;
                        TypeRef tmpl_itp = tmpl.impl_target_pattern(mscan_pool);
                        if (tmpl.type_params_empty() && tmpl_itp)
                            collect_pattern_typevars(
                                TypeRef(tmpl_itp), impl_tvs);
                        if (!impl_tvs.empty() && impl_tvs.size() == type_args.size()) {
                            SubstMap subst;
                            for (size_t i = 0; i < impl_tvs.size(); ++i)
                                subst[impl_tvs[i]] = type_args[i];
                            done_.insert(mangled_callee);
                            worklist_.push_back({mangled_callee, tmpl,
                                                 std::move(subst), {},
                                                 depth_ + 1, {}});
                            return;
                        }
                        if (type_args.size() >= n_struct) {
                            // Build SubstMap: struct tparams from prefix +
                            // method tparams from suffix.
                            SubstMap subst;
                            for (size_t i = 0; i < n_struct && i < type_args.size(); ++i)
                                subst[sd_tpars[i]] = type_args[i];
                            size_t i_meth = 0;
                            for (auto& mtp : tmpl.type_params()) {
                                std::string mtp_name(mtp.name());
                                bool is_struct = false;
                                for (auto& stp : sd_tpars)
                                    if (stp == mtp_name) { is_struct = true; break; }
                                if (is_struct) continue;
                                if (n_struct + i_meth < type_args.size())
                                    subst[mtp_name] = type_args[n_struct + i_meth];
                                ++i_meth;
                            }
                            done_.insert(mangled_callee);
                            worklist_.push_back({mangled_callee, tmpl,
                                                 std::move(subst), {},
                                                 depth_ + 1, {}});
                            return;
                        }
                    }
                }
            }
        }
    }
    if (orig_name.empty()) return;  // not a generic/spec call we know about

    if (depth_ >= max_depth_) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("instantiation depth limit ({}) exceeded for '{}'",
                        max_depth_, mangled_callee), {}, 0});
        return;
    }

    done_.insert(mangled_callee);

    // Prefer the most-specific matching specialisation over the generic template.
    auto* mscan_pool2 = out_.type_pool.impl();
    if (auto spec = find_best_spec(orig_name, type_args)) {
        auto sp = spec.spec_patterns(mscan_pool2);
        SubstMap subst;
        for (size_t i = 0; i < sp.size(); ++i)
            match_type(type_args[i], sp[i], subst);
        worklist_.push_back({mangled_callee, spec, std::move(subst), {}, depth_ + 1, {}});
        return;
    }

    // Generic template fallback.
    auto tit = templates_.find(orig_name);
    if (tit == templates_.end()) return;
    lir_view::FunctionView tmpl = tit->second;

    SubstMap subst;
    PackMap  packs;
    auto tmpl_tparams = tmpl.type_params();
    bool has_variadic = !tmpl_tparams.empty() && tmpl_tparams.back().is_variadic();
    size_t non_variadic_count = tmpl_tparams.size() - (has_variadic ? 1 : 0);
    // CP-cm-16 follow-up: partial-spec impl-target unification (parallel to
    // finish_generic_call in sema_expr.cpp). When the template is a method
    // of `impl<T,E> Trait for Foo<Vec<T>, E>`, the receiver-positional
    // type_args [Vec<i32>, i32] do NOT line up positionally with the impl-
    // level tparams [T, E] — unify the pattern against type_args' head to
    // bind T=i32, then layer any method-level type_args positionally.
    size_t impl_level_n = 0;
    bool used_impl_pattern = false;
    TypeRef tmpl_itp2 = tmpl.impl_target_pattern(mscan_pool2);
    if (tmpl_itp2) {
        auto pat = TypeRef(tmpl_itp2);
        auto pa = pat.type_args();
        if (!pa.empty() && pa.size() <= type_args.size()) {
            SubstMap impl_bind;
            bool ok = true;
            for (size_t i = 0; i < pa.size(); ++i)
                if (!unify_impl_target(type_args[i], pa[i], impl_bind))
                    { ok = false; break; }
            if (ok) {
                for (size_t i = 0; i < pa.size() && i < tmpl_tparams.size(); ++i) {
                    auto it = impl_bind.find(std::string(tmpl_tparams[i].name()));
                    if (it == impl_bind.end()) { ok = false; break; }
                }
                if (ok) {
                    for (auto& [k, v] : impl_bind) subst[k] = v;
                    impl_level_n = pa.size();
                    used_impl_pattern = true;
                }
            }
        }
    }
    for (size_t i = used_impl_pattern ? impl_level_n : 0;
         i < non_variadic_count && i < type_args.size(); ++i)
        subst[std::string(tmpl_tparams[i].name())] = type_args[i];
    if (has_variadic) {
        auto vtp = tmpl_tparams.back();
        std::string vtp_name(vtp.name());
        std::vector<TypeRef> pack_types;
        for (size_t i = non_variadic_count; i < type_args.size(); ++i) {
            // Const-pack: wrap each scalar as a ConstVar carrying the param's
            // numeric type in `pointee` so PACK_EXPAND in mono_clone has the
            // info to emit a typed `lit_int(N, i64)` expression.
            if (vtp.is_const() && type_args[i] && type_args[i].const_val()) {
                LogosTypeBuilder cv;
                cv.kind = LogosType::Kind::ConstVar;
                cv.type_var_name = vtp_name;
                cv.pointee = vtp.const_type(mscan_pool2);
                cv.const_val = *type_args[i].const_val();
                pack_types.push_back(out_.type_pool.alloc(std::move(cv)));
            } else {
                pack_types.push_back(type_args[i]);
            }
        }
        packs[vtp_name] = std::move(pack_types);
    }
    worklist_.push_back({mangled_callee, tmpl, std::move(subst), std::move(packs), depth_ + 1, {}});
}

// ── L1: lazy method instantiation (infrastructure) ────────────────────────
//
// Default scheme is still eager: clone_struct_def clones every method during
// struct instantiation, so this path is dead until lazy_methods_ is flipped
// (L1.6). It exists now so call-site rewrites (L1.1), trait-dispatch pinning
// (L1.2), and is_root_pin (L1.3) hooks have a stable target to enqueue into.

void Mono::enqueue_method_inst(TypeRef concrete_struct_t,
                               const std::string& method_name) {
    if (!concrete_struct_t) return;
    auto kind = TypeRef(concrete_struct_t).kind();
    if (kind != LogosType::Kind::Struct && kind != LogosType::Kind::ZonedStruct)
        return;
    std::string concrete = concrete_struct_name(concrete_struct_t);
    std::string base{TypeRef(concrete_struct_t).struct_name()};
    if (auto p = base.find("$G"); p != std::string::npos)
        base = base.substr(0, p);
    // Prefer pkg-qualified lookup so cross-pkg same-named structs use
    // the correct template. If the struct exists in this pkg but has no
    // methods, don't fall back to bare (would leak other pkg's methods).
    std::string pkg{TypeRef(concrete_struct_t).pkg_name()};
    auto* sit_inner = find_struct_method_templates_guarded(pkg, base);
    if (!sit_inner) return;

    // Method names may carry overload-disambiguation suffix `__g__<sig>`.
    // Match every entry whose short-name equals `method_name` exactly, or
    // begins with `method_name + "__g__"`. Each match enqueues separately
    // (overloads keep their distinct signatures).
    auto* lmi_pool = out_.type_pool.impl();
    std::vector<std::pair<std::string, lir_view::FunctionView>> matches;
    for (auto& [sn, fp] : *sit_inner) {
        if (sn == method_name ||
            (sn.size() > method_name.size() + 5 &&
             sn.compare(0, method_name.size(), method_name) == 0 &&
             sn.compare(method_name.size(), 5, "__g__") == 0))
            matches.emplace_back(sn, fp);
    }
    if (matches.empty()) return;

    auto stt_ptr = find_struct_template_pkg_first(pkg, base);
    if (!stt_ptr.valid()) return;
    auto tpars = stt_ptr.type_params();
    auto type_args = TypeRef(concrete_struct_t).type_args();

    for (auto& [sn, fp] : matches) {
        // Skip methods with method-level type-params (e.g. `fn map<U>` on
        // `impl<I, T> Iterator<T> for FilterIter<I, T>`). The root-pin /
        // dispatch path only binds the struct-level tpars; method-level
        // tparams need a real call-site turbofish to bind them. Enqueuing
        // here would leave the cloned body with literal TypeVars (e.g.
        // `MapIter<FilterIter, i32, U>`) which mlir_gen can't lower.
        // Real call sites enqueue via subst_expr's MethodCall path with
        // a SubstMap that includes the method-tparam binding.
        bool has_method_tparam = false;
        for (auto& tp : fp.type_params()) {
            std::string tp_name(tp.name());
            bool is_struct_tparam = false;
            for (auto& stp : tpars)
                if (stp.name() == tp_name) { is_struct_tparam = true; break; }
            if (!is_struct_tparam) { has_method_tparam = true; break; }
        }
        if (has_method_tparam) continue;

        // Dedup key uses the short user-facing name so multiple overloads
        // sharing it dedupe to one slot (matches eager rename semantics).
        std::string key = concrete + "__" + method_name + "::" + sn;
        if (!done_methods_.insert(key).second) continue;

        SubstMap subst;
        PackMap  packs;
        for (size_t i = 0, j = 0; i < tpars.size(); ++i) {
            if (tpars[i].is_variadic()) {
                std::vector<TypeRef> pack;
                while (j < type_args.size()) pack.push_back(type_args[j++]);
                packs[std::string(tpars[i].name())] = std::move(pack);
            } else if (j < type_args.size()) {
                subst[std::string(tpars[i].name())] = type_args[j++];
            }
        }

        // Structured impl self-type (`impl<T> Pin<&T>` on `struct Pin<P>`):
        // the positional subst above binds only the struct's own param names
        // (P), leaving impl-level params (T) unbound — and it can't tell that
        // this overload doesn't even belong to the instantiation (a Pin<&T>
        // method on a Pin<Box<…>> spec). Unify the impl pattern's args
        // against the concrete struct args: mismatch → skip the overload,
        // success → merge the impl-level bindings.
        TypeRef fp_itp = fp.impl_target_pattern(lmi_pool);
        if (fp_itp) {
            auto pa = TypeRef(fp_itp).type_args();
            if (!pa.empty() && pa.size() == tpars.size() &&
                pa.size() <= type_args.size()) {
                bool mismatch = false;
                for (size_t i = 0; i < pa.size(); ++i) {
                    if (!type_args[i] || contains_typevar(type_args[i]))
                        continue;  // defer to a later, fully concrete pass
                    if (contains_assoc_type(pa[i]))
                        continue;  // projection — not structurally decidable
                    SubstMap b;
                    if (!unify_impl_target(type_args[i], pa[i], b)) {
                        mismatch = true;
                        break;
                    }
                    for (auto& [bk, bv] : b)
                        if (!subst.count(bk)) subst[bk] = bv;
                }
                if (mismatch) continue;
            }
        }

        method_worklist_.push_back({concrete, pkg, base, method_name, fp,
                                    std::move(subst), std::move(packs), depth_ + 1});
    }
}

void Mono::drain_method_worklist() {
    while (!method_worklist_.empty()) {
        auto item = std::move(method_worklist_.back());
        method_worklist_.pop_back();

        lir_view::FunctionView tmpl = item.tmpl;

        // Find the target struct in out_.structs (it must already exist —
        // struct shells are emitted before any method enqueue can fire).
        lir_view::StructView* target = nullptr;
        // Pkg-aware disambig when two same-named clones coexist.
        for (auto& sd : out_.structs)
            if (sd.name() == item.concrete_struct &&
                (item.struct_pkg.empty() || sd.pkg() == item.struct_pkg)) {
                target = &sd; break;
            }
        if (!target)
            for (auto& sd : out_.structs)
                if (sd.name() == item.concrete_struct) { target = &sd; break; }
        if (!target) continue;

        // Build pkg-qualified dest_name preserving the template's sig suffix
        // (`__f__sig` / `__g__sig`). With unification, the method template's
        // name is `[pkg.]Base__method__[fg]__sig`; the cloned method should
        // be `[pkg.]Concrete__method__[fg]__sig`.
        std::string sig;
        {
            std::string tn = std::string(tmpl.name());
            // Pkg may have inner dots; split at LAST dot.
            if (auto dot = tn.rfind('.'); dot != std::string::npos)
                tn = tn.substr(dot + 1);
            auto sep1 = tn.find("__");
            if (sep1 != std::string::npos) {
                // Method names may themselves start with `__` (e.g.
                // `__drop_in_place`). Walk to the actual signature boundary —
                // `__f__` (free fn / by-value self) or `__g__` (generic /
                // ref self) — instead of grabbing the next `__`, which would
                // otherwise cut into the method name.
                auto pf = tn.find("__f__", sep1 + 2);
                auto pg = tn.find("__g__", sep1 + 2);
                size_t sep2 = std::string::npos;
                if (pf != std::string::npos && pg != std::string::npos)
                    sep2 = std::min(pf, pg);
                else if (pf != std::string::npos) sep2 = pf;
                else if (pg != std::string::npos) sep2 = pg;
                else                              sep2 = tn.find("__", sep1 + 2);
                if (sep2 != std::string::npos) sig = tn.substr(sep2);
            }
        }
        std::string bare_dest = item.concrete_struct + "__" + item.method_name + sig;
        std::string dest_name = item.struct_pkg.empty()
                                ? bare_dest
                                : item.struct_pkg + "." + bare_dest;
        bool exists = false;
        target->each_method([&](lir_view::FunctionView m) {
            if (m.name() == dest_name) exists = true;
        });
        if (exists) continue;
        // Specialization: a non-generic `impl Foo<Concrete>` lowers to a
        // free-fn under this exact mangled name. Don't clone the blanket
        // body — the passthrough free-fn path emits the correct one.
        for (auto& fn : in_.functions) {
            if (!fn.type_params_empty()) continue;
            if (fn.name() == dest_name) { exists = true; break; }
        }
        if (exists) continue;

        if (!method_bound_ok(tmpl, item.subst)) continue;

        depth_ = item.depth;
        // Binary-symbol fast path: pre-baked struct-method instance in the
        // archive. Push a signature-only stub so mlir_gen can forward-declare
        // it; skip the deep body clone + scan_fn.
        if (!in_.binary_symbols.empty() &&
            in_.binary_symbols.has(dest_name)) {
            auto stub = clone_fn_signature(tmpl, item.subst, item.packs);
            stub.str_always(lir_schema::decl_keys::NAME, dest_name);
            lir_mirror_struct_append_method(out_, *target, stub.view<lir_view::FunctionView>());
            ++stats_.method_instances;
            note_method_worklist_size(method_worklist_.size());
            continue;
        }
        auto cloned = clone_fn(tmpl, item.subst, item.packs);
        cloned.str_always(lir_schema::decl_keys::NAME, dest_name);
        auto cloned_v = cloned.view<lir_view::FunctionView>();
        lir_mirror_struct_append_method(out_, *target, cloned_v);
        scan_fn(cloned_v);
        ++stats_.method_instances;
        note_method_worklist_size(method_worklist_.size());
    }
    depth_ = 0;
}

} // namespace logos::compiler
