// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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

void Mono::scan_fn(const lir::LFunction& fn) {
    if (fn.body.mirror_offset_ == hermes::arena_offset_t{}) return;
    auto& arena = out_.type_pool.arena_or_init();
    scan_block(lir_view::BlockRef(&arena, fn.body.mirror_offset_));
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
    case SCode::Delete:
        scan_expr(lir_view::SDeleteView{s}.expr());
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
    case ECode::Cast:
        scan_expr(lir_view::ECastView{e}.operand());
        break;
    case ECode::New:
        lir_view::ENewView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { scan_expr(fv); });
        break;
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
    // Leaf / no-recurse variants.
    case ECode::LitInt:
    case ECode::LitFloat:
    case ECode::LitBool:
    case ECode::LitStr:
    case ECode::VarRef:
    case ECode::EnumLit:
    case ECode::AddrOf:
    case ECode::AddrOfTemp:
    case ECode::PackExpand:
    case ECode::SizeOf:
    case ECode::AlignOf:
    case ECode::GenericRef:  // rewritten to VarRef during subst_expr; never reaches here
    case ECode::TypeCodeOf:
    case ECode::HermesLit:
    case ECode::PtrArith:
    case ECode::PtrDiff:
    case ECode::ReflectOf:
        break;
    }
}


// ── Pattern matching (static, inline in mono_impl.hpp) ───────────

// Return the most specific specialisation that matches type_args, or nullptr.
const lir::LFunction* Mono::find_best_spec(
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
    if (sit == specs_.end()) return nullptr;

    const lir::LFunction* best      = nullptr;
    std::vector<int>      best_vec;
    bool                  ambiguous = false;

    for (auto* spec : sit->second) {
        if (spec->spec_patterns.size() != type_args.size()) continue;
        SubstMap dummy;
        bool ok = true;
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type(type_args[i], spec->spec_patterns[i], dummy)) {
                ok = false; break;
            }
        }
        if (!ok) continue;
        auto svec = specificity_vec(spec->spec_patterns);
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
    if (orig_name.empty()) return;  // not a generic/spec call we know about

    if (depth_ >= max_depth_) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("instantiation depth limit ({}) exceeded for '{}'",
                        max_depth_, mangled_callee), {}, 0});
        return;
    }

    done_.insert(mangled_callee);

    // Prefer the most-specific matching specialisation over the generic template.
    if (auto* spec = find_best_spec(orig_name, type_args)) {
        SubstMap subst;
        for (size_t i = 0; i < spec->spec_patterns.size(); ++i)
            match_type(type_args[i], spec->spec_patterns[i], subst);
        worklist_.push_back({mangled_callee, spec, std::move(subst), {}, depth_ + 1});
        return;
    }

    // Generic template fallback.
    auto tit = templates_.find(orig_name);
    if (tit == templates_.end()) return;
    const lir::LFunction* tmpl = tit->second;

    SubstMap subst;
    PackMap  packs;
    bool has_variadic = !tmpl->type_params.empty() && tmpl->type_params.back().is_variadic;
    size_t non_variadic_count = tmpl->type_params.size() - (has_variadic ? 1 : 0);
    for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
        subst[tmpl->type_params[i].name] = type_args[i];
    if (has_variadic) {
        auto& vtp = tmpl->type_params.back();
        std::vector<TypeRef> pack_types;
        for (size_t i = non_variadic_count; i < type_args.size(); ++i) {
            // Const-pack: wrap each scalar as a ConstVar carrying the param's
            // numeric type in `pointee` so PACK_EXPAND in mono_clone has the
            // info to emit a typed `lit_int(N, i64)` expression.
            if (vtp.is_const && type_args[i] && type_args[i].const_val()) {
                LogosTypeBuilder cv;
                cv.kind = LogosType::Kind::ConstVar;
                cv.type_var_name = vtp.name;
                cv.pointee = vtp.const_type;
                cv.const_val = *type_args[i].const_val();
                pack_types.push_back(out_.type_pool.alloc(std::move(cv)));
            } else {
                pack_types.push_back(type_args[i]);
            }
        }
        packs[vtp.name] = std::move(pack_types);
    }
    worklist_.push_back({mangled_callee, tmpl, std::move(subst), std::move(packs), depth_ + 1});
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
    bool pkg_struct_exists = !pkg.empty() &&
        struct_templates_.find(pkg + "." + base) != struct_templates_.end();
    auto sit = pkg.empty() ? struct_method_templates_.end()
                            : struct_method_templates_.find(pkg + "." + base);
    if (sit == struct_method_templates_.end() && !pkg_struct_exists)
        sit = struct_method_templates_.find(base);
    if (sit == struct_method_templates_.end()) return;

    // Method names may carry overload-disambiguation suffix `__g__<sig>`.
    // Match every entry whose short-name equals `method_name` exactly, or
    // begins with `method_name + "__g__"`. Each match enqueues separately
    // (overloads keep their distinct signatures).
    std::vector<std::pair<std::string, const lir::LFunction*>> matches;
    for (auto& [sn, fp] : sit->second) {
        if (sn == method_name ||
            (sn.size() > method_name.size() + 5 &&
             sn.compare(0, method_name.size(), method_name) == 0 &&
             sn.compare(method_name.size(), 5, "__g__") == 0))
            matches.emplace_back(sn, fp);
    }
    if (matches.empty()) return;

    auto stt = pkg.empty() ? struct_templates_.end()
                            : struct_templates_.find(pkg + "." + base);
    if (stt == struct_templates_.end() && !pkg_struct_exists)
        stt = struct_templates_.find(base);
    if (stt == struct_templates_.end()) return;
    const auto& tpars = stt->second->type_params;
    auto type_args = TypeRef(concrete_struct_t).type_args();

    for (auto& [sn, fp] : matches) {
        // Dedup key uses the short user-facing name so multiple overloads
        // sharing it dedupe to one slot (matches eager rename semantics).
        std::string key = concrete + "__" + method_name + "::" + sn;
        if (!done_methods_.insert(key).second) continue;

        SubstMap subst;
        PackMap  packs;
        for (size_t i = 0, j = 0; i < tpars.size(); ++i) {
            if (tpars[i].is_variadic) {
                std::vector<TypeRef> pack;
                while (j < type_args.size()) pack.push_back(type_args[j++]);
                packs[tpars[i].name] = std::move(pack);
            } else if (j < type_args.size()) {
                subst[tpars[i].name] = type_args[j++];
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

        const lir::LFunction* tmpl = item.tmpl;

        // Find the target struct in out_.structs (it must already exist —
        // struct shells are emitted before any method enqueue can fire).
        lir::LStructDef* target = nullptr;
        // Pkg-aware disambig when two same-named clones coexist.
        for (auto& sd : out_.structs)
            if (sd.name == item.concrete_struct &&
                (item.struct_pkg.empty() || sd.pkg == item.struct_pkg)) {
                target = &sd; break;
            }
        if (!target)
            for (auto& sd : out_.structs)
                if (sd.name == item.concrete_struct) { target = &sd; break; }
        if (!target) continue;

        // Build pkg-qualified dest_name preserving the template's sig suffix
        // (`__f__sig` / `__g__sig`). With unification, the method template's
        // name is `[pkg.]Base__method__[fg]__sig`; the cloned method should
        // be `[pkg.]Concrete__method__[fg]__sig`.
        std::string sig;
        {
            std::string tn = tmpl->name;
            if (auto dot = tn.find('.'); dot != std::string::npos)
                tn = tn.substr(dot + 1);
            auto sep1 = tn.find("__");
            if (sep1 != std::string::npos) {
                auto sep2 = tn.find("__", sep1 + 2);
                if (sep2 != std::string::npos) sig = tn.substr(sep2);
            }
        }
        std::string bare_dest = item.concrete_struct + "__" + item.method_name + sig;
        std::string dest_name = item.struct_pkg.empty()
                                ? bare_dest
                                : item.struct_pkg + "." + bare_dest;
        bool exists = false;
        for (auto& m : target->methods)
            if (m->name == dest_name) { exists = true; break; }
        if (exists) continue;
        // Specialization: a non-generic `impl Foo<Concrete>` lowers to a
        // free-fn under this exact mangled name. Don't clone the blanket
        // body — the passthrough free-fn path emits the correct one.
        for (auto& fn : in_.functions) {
            if (!fn->type_params.empty()) continue;
            if (fn->name == dest_name) { exists = true; break; }
        }
        if (exists) continue;

        if (!method_bound_ok(*tmpl, item.subst)) continue;

        depth_ = item.depth;
        auto cloned = clone_fn(*tmpl, item.subst, item.packs);
        cloned.name = dest_name;
        cloned.type_params.clear();

        target->methods.push_back(std::make_unique<lir::LFunction>(std::move(cloned)));
        auto& fn_ref = *target->methods.back();
        lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
        scan_fn(fn_ref);
        ++stats_.method_instances;
        note_method_worklist_size(method_worklist_.size());
    }
    depth_ = 0;
}

} // namespace logos::compiler
