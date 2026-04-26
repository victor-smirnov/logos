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
        std::string raw = base_name;
        if (auto p = raw.find("__g__"); p != std::string::npos)
            raw.resize(p);
        else if (auto p = raw.find("__f__"); p != std::string::npos)
            raw.resize(p);
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
        for (size_t i = non_variadic_count; i < type_args.size(); ++i)
            pack_types.push_back(type_args[i]);
        packs[vtp.name] = std::move(pack_types);
    }
    worklist_.push_back({mangled_callee, tmpl, std::move(subst), std::move(packs), depth_ + 1});
}

} // namespace logos::compiler
