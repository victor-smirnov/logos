// T2-24 (B) — const-arg specialization analysis.
//
// `compute_const_want(base)` returns the parameter indices of `base` whose
// value, when a compile-time literal at a call site, is worth baking into a
// specialized clone: the param forwards (directly or transitively) to an
// intrinsic position that mlir-gen const-evaluates (today: atomic Ordering,
// via read_ordering_at). The result is conservative — missing a forwarding
// shape just means no specialization (sound: the unspecialized fn keeps its
// seq_cst fallback).

#include "mono_impl.hpp"
#include <logos/compiler/lir_view.hpp>
#include <set>
#include <unordered_map>

namespace logos::compiler {

using ECode = lir_schema::expr::Code;
using SCode = lir_schema::stmt::Code;

// Seed registry: intrinsic name → arg positions const-read by mlir-gen
// (mirrors mlir_gen_expr.cpp read_ordering_at(N) per atomic `*_ord` op).
std::vector<size_t> Mono::const_intrinsic_positions(const std::string& name) {
    static const std::unordered_map<std::string, std::vector<size_t>> reg = {
        {"logos_atomic_load32_ord",      {1}}, {"logos_atomic_load64_ord",      {1}},
        {"logos_atomic_store32_ord",     {2}}, {"logos_atomic_store64_ord",     {2}},
        {"logos_atomic_fetch_add32_ord", {2}}, {"logos_atomic_fetch_add64_ord", {2}},
        {"logos_atomic_swap32_ord",      {2}}, {"logos_atomic_swap64_ord",      {2}},
        {"logos_atomic_fetch_or32_ord",  {2}}, {"logos_atomic_fetch_or64_ord",  {2}},
        {"logos_atomic_fetch_and32_ord", {2}}, {"logos_atomic_fetch_and64_ord", {2}},
        {"logos_atomic_fetch_xor32_ord", {2}}, {"logos_atomic_fetch_xor64_ord", {2}},
        {"logos_atomic_cas32_ord",       {3, 4}}, {"logos_atomic_cas64_ord",    {3, 4}},
    };
    auto it = reg.find(name);
    return it == reg.end() ? std::vector<size_t>{} : it->second;
}

const lir::LFunction* Mono::find_fn_def_by_base(const std::string& base) {
    for (auto& f : in_.functions)
        if (f && f->name == base) return f.get();
    for (auto& sd : in_.structs)
        for (auto& m : sd.methods)
            if (m && m->name == base) return m.get();
    return nullptr;
}

namespace {
// Peel value-transparent wrappers so a forwarded `#param` is seen as a bare
// VarRef even through `as`-casts / parens / unary / &/*. (Conservative: any
// shape not peeled here simply isn't treated as a bare forward.)
lir_view::ExprRef peel(lir_view::ExprRef e) {
    for (;;) {
        if (!e) return e;
        switch (e.kind()) {
            case ECode::Cast:   e = lir_view::ECastView{e}.operand();  break;
            case ECode::Unary:  e = lir_view::EUnaryView{e}.operand(); break;
            case ECode::Deref:  e = lir_view::EDerefView{e}.operand(); break;
            default: return e;
        }
    }
}
}  // namespace

const std::vector<size_t>& Mono::compute_const_want(const std::string& base) {
    if (auto it = const_want_cache_.find(base); it != const_want_cache_.end())
        return it->second;
    static const std::vector<size_t> kEmpty;
    // Recursion guard: while computing `base`, treat it as no-const-want.
    if (!const_want_inflight_.insert(base).second) return kEmpty;

    std::vector<size_t> result;
    if (auto reg = const_intrinsic_positions(base); !reg.empty()) {
        result = std::move(reg);
    } else if (const lir::LFunction* fn = find_fn_def_by_base(base);
               fn && fn->body.mirror_ptr_ != nullptr) {
        std::unordered_map<std::string, size_t> pidx;
        for (size_t i = 0; i < fn->params.size(); ++i) pidx[fn->params[i].name] = i;
        std::set<size_t> want;
        auto& arena = out_.type_pool.arena_or_init();

        // Visit every Call expr reachable through the common forwarding shapes.
        std::function<void(lir_view::ExprRef)> visit_expr =
            [&](lir_view::ExprRef e) {
                if (!e) return;
                switch (e.kind()) {
                    case ECode::Cast:  visit_expr(lir_view::ECastView{e}.operand());  return;
                    case ECode::Unary: visit_expr(lir_view::EUnaryView{e}.operand()); return;
                    case ECode::Deref: visit_expr(lir_view::EDerefView{e}.operand()); return;
                    case ECode::Call: break;
                    default: return;
                }
                lir_view::ECallView cv{e};
                std::string callee(cv.callee());
                std::vector<lir_view::ExprRef> args;
                cv.each_arg([&](lir_view::ExprRef a) { args.push_back(a); });
                const auto& cw = compute_const_want(callee);  // recurse (guarded)
                for (size_t p : cw) {
                    if (p >= args.size()) continue;
                    auto a = peel(args[p]);
                    if (!a || a.kind() != ECode::VarRef) continue;
                    std::string vn(lir_view::EVarRefView{a}.name());
                    if (auto pit = pidx.find(vn); pit != pidx.end())
                        want.insert(pit->second);
                }
                // Nested calls inside args.
                for (auto a : args) visit_expr(a);
            };

        std::function<void(lir_view::BlockRef)> visit_block;
        std::function<void(lir_view::StmtRef)> visit_stmt =
            [&](lir_view::StmtRef s) {
                if (!s) return;
                switch (s.kind()) {
                    case SCode::Let:    visit_expr(lir_view::SLetView{s}.value()); break;
                    case SCode::Assign: visit_expr(lir_view::SAssignView{s}.value()); break;
                    case SCode::Return: visit_expr(lir_view::SReturnView{s}.value()); break;
                    case SCode::ExprStmt: visit_expr(lir_view::SExprStmtView{s}.expr()); break;
                    case SCode::Block:  visit_block(lir_view::SBlockView{s}.body()); break;
                    case SCode::If: {
                        lir_view::SIfView v{s};
                        visit_expr(v.cond());
                        visit_block(v.then_block());
                        visit_block(v.else_block());
                        break;
                    }
                    case SCode::While: {
                        lir_view::SWhileView v{s};
                        visit_expr(v.cond()); visit_block(v.body()); break;
                    }
                    case SCode::Loop: visit_block(lir_view::SLoopView{s}.body()); break;
                    default: break;
                }
            };
        visit_block = [&](lir_view::BlockRef b) {
            if (!b) return;
            b.each_stmt([&](lir_view::StmtRef s) { visit_stmt(s); });
        };

        visit_block(lir_view::BlockRef(&arena, fn->body.mirror_ptr_));
        result.assign(want.begin(), want.end());
    }

    const_want_inflight_.erase(base);
    return const_want_cache_.emplace(base, std::move(result)).first->second;
}

// Core: given a finalized callee + its (cloned) args, return the callee to
// actually emit — either `callee` unchanged, or a per-value spec name (with
// the spec enqueued for cloning). Works for free calls AND method→call
// rewrites: in the latter the receiver is args[0] and self is params[0], so a
// const-want param index maps to the same args index with no offset.
std::string Mono::const_specialize_callee(
        const std::string& callee, const std::vector<lir::LExprPtr>& args) {
    // A registry intrinsic is the const-want SEED, never a spec target: its
    // const arg is already a literal here, and renaming it would hide it from
    // mlir-gen's name-keyed atomic lowering.
    if (!const_intrinsic_positions(callee).empty()) return callee;
    const auto& cw = compute_const_want(callee);
    if (cw.empty()) return callee;
    const lir::LFunction* fn = find_fn_def_by_base(callee);
    if (!fn || fn->body.mirror_ptr_ == nullptr)
        return callee;  // extern / bodyless / unknown — can't clone

    std::vector<std::pair<std::string, ConstArgVal>> binds;
    std::string suffix = "__cv";
    auto& arena = out_.type_pool.arena_or_init();
    for (size_t p : cw) {
        if (p >= args.size() || p >= fn->params.size() || !args[p]) continue;
        lir_view::ExprRef aref(&arena, args[p].addr());
        ConstArgVal cv;
        if (!try_read_const_arg(aref, cv)) continue;  // runtime arg → no spec
        binds.emplace_back(fn->params[p].name, cv);
        suffix += "_" + std::to_string(p) + "_"
                + (cv.is_enum ? cv.enum_name : std::string("i"))
                + std::to_string(cv.ival);
    }
    if (binds.empty()) return callee;  // every const-want arg was runtime

    std::string spec = callee + suffix;
    if (!done_.count(spec)) {
        done_.insert(spec);
        WorkItem wi;
        wi.mangled    = spec;
        wi.tmpl       = fn;
        wi.depth      = depth_ + 1;
        wi.const_args = std::move(binds);
        worklist_.push_back(std::move(wi));
    }
    return spec;
}

void Mono::maybe_const_specialize(lir::ECall& nc) {
    nc.callee = const_specialize_callee(nc.callee, nc.args);
}

bool Mono::try_read_const_arg(lir_view::ExprRef arg, ConstArgVal& out) {
    arg = peel(arg);
    if (!arg) return false;
    if (arg.kind() == ECode::EnumLit) {
        lir_view::EEnumLitView v{arg};
        out.is_enum   = true;
        out.enum_name = std::string(v.enum_name());
        out.variant   = std::string(v.variant());
        out.ival      = v.disc();
        out.type      = arg.type(out_.type_pool.impl());
        return true;
    }
    if (arg.kind() == ECode::LitInt) {
        out.is_enum = false;
        out.ival    = lir_view::ELitIntView{arg}.value();
        out.type    = arg.type(out_.type_pool.impl());
        return true;
    }
    return false;
}

}  // namespace logos::compiler
