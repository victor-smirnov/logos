// Logos project — region inference (B70 scaffolding).
//
// Per-fn pass: build CFG over statement points, assign fresh
// RegionIds to every borrow site (`&x`, `&mut x`, `&temp`) found in
// the body, and seed the constraint set with a Contains-at-origin
// per borrow. B71 grows the solver; B72 wires the conflict checker.

#include <logos/compiler/region_infer.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/sema.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace logos::compiler {

namespace {

bool is_mut_ref_type(TypeRef t) {
    return t && t.kind() == LogosType::Kind::MutRef;
}

} // namespace

void RegionInferer::analyze(const lir::LFunction& fn, const lir::LProgram& prog) {
    cfg_.blocks.clear();
    borrows_.clear();
    constraints_.clear();
    live_in_.clear();
    live_out_.clear();
    use_.clear();
    def_.clear();
    named_regions_.clear();
    outlives_pairs_.clear();
    next_region_id_ = 1;
    prog_for_liveness_ = &prog;

    // logos-core 2.1: allocate a fresh RegionId per declared lifetime
    // parameter (`'a`, `'b`, …) and seed Outlives constraints from
    // `fn.lifetime_outlives`. These named regions become the canonical
    // link between borrow_check's syntactic outlives graph (B66, string-
    // keyed) and region_infer's semantic constraint graph. Downstream
    // passes (HRTB instantiation, dropck, default trait-object lifetime)
    // consume named_region(name) → RegionId.
    for (auto& lp : fn.lifetime_params)
        if (!lp.empty()) named_regions_[lp] = fresh_region();
    outlives_pairs_ = fn.lifetime_outlives;
    for (auto& [longer, shorter] : fn.lifetime_outlives) {
        // Skip outlives clauses that mention an unknown lifetime — that
        // is a sema-level error already reported (region_infer is best-
        // effort here, not the diagnostic site).
        auto li = named_regions_.find(longer);
        auto si = named_regions_.find(shorter);
        if (li == named_regions_.end() || si == named_regions_.end()) continue;
        RegionConstraint c;
        c.kind    = RegionConstraint::Kind::Outlives;
        c.longer  = li->second;
        c.shorter = si->second;
        c.point   = StmtPoint{0, 0};   // outlives is point-independent
        constraints_.push_back(c);
    }

    cfg_.blocks.emplace_back();
    walk_block(fn.body, /*blk_id=*/0, prog);
    // After CFG construction, also walk every block one more time to
    // populate use_/def_ per StmtPoint. This is independent of the
    // borrow-walker (which targets AddrOf nodes only).
    for (uint32_t b = 0; b < cfg_.blocks.size(); ++b) {
        // We don't preserve the per-block LStmt array; instead the
        // walk_stmt path already had `use_def_for_stmt` invoked when
        // the statement was visited. Liveness fixpoint runs over the
        // gathered maps.
    }
    region_points_.clear();
    compute_liveness();
    // B71.2: emit region-growth constraints from holder liveness.
    // A borrow's region must contain every CFG point where the
    // holder (let/assign LHS that binds the `&x`) is live.
    for (auto& b : borrows_) {
        if (b.holder.empty()) continue;
        for (uint32_t bi = 0; bi < cfg_.blocks.size(); ++bi) {
            for (uint32_t si = 0; si < cfg_.blocks[bi].n_stmts; ++si) {
                StmtPoint p{bi, si};
                auto it = live_in_.find(p);
                if (it == live_in_.end()) continue;
                if (!it->second.count(b.holder)) continue;
                RegionConstraint c;
                c.kind    = RegionConstraint::Kind::Contains;
                c.longer  = b.region;
                c.shorter = NO_REGION;
                c.point   = p;
                constraints_.push_back(c);
            }
        }
    }
    solve();
}

bool RegionInferer::outlives_named(const std::string& longer,
                                    const std::string& shorter) const noexcept {
    // Reflexive.
    if (longer == shorter) return true;
    // `'static` is the longest lifetime — outlives everything; nothing
    // concrete outlives it *unless* the user explicitly declares so
    // (a `'a: 'static` clause asserts 'a IS as long as 'static, which
    // Rust allows). So the static-at-top fast-path only fires for
    // `longer == 'static`; the `shorter == 'static` case falls through
    // to BFS so a declared `longer: 'static` edge wins.
    auto norm = [](const std::string& s) -> std::string {
        // Strip a leading `'` so the graph keys agree with the
        // outlives_adj convention shared with `outlives.hpp`.
        if (!s.empty() && s.front() == '\'') return s.substr(1);
        return s;
    };
    std::string L = norm(longer);
    std::string S = norm(shorter);
    if (L == "static") return true;
    if (S.empty()) return true;  // unconstrained shorter — vacuously
    if (L.empty()) return false; // strict mode (matches outlives() with permissive_empty=false)
    // BFS over the original `(longer, shorter)` pairs — directly
    // equivalent to `outlives_adj` in `outlives.hpp`. We can walk
    // through `'static` (which is never a named-region) because it
    // appears as a string key/value in the pairs.
    std::unordered_set<std::string> seen;
    std::vector<std::string> queue;
    queue.push_back(L);
    seen.insert(L);
    while (!queue.empty()) {
        std::string cur = std::move(queue.back());
        queue.pop_back();
        if (cur == S) return true;
        for (auto& [pa, pb] : outlives_pairs_) {
            std::string na = norm(pa);
            std::string nb = norm(pb);
            if (na != cur) continue;
            if (nb == S) return true;
            if (seen.insert(nb).second) queue.push_back(std::move(nb));
        }
    }
    // Conservative reject: matches `outlives(..., permissive_empty=false)`
    // at the borrow_check return-value path.
    return false;
}

void RegionInferer::solve() {
    // Fixed-point: each region = set of CFG points.
    //   Contains(r, P)       → r.points.insert(P)
    //   Outlives(longer, shorter)
    //     → longer.points |= shorter.points
    //   (i.e. longer must contain everything shorter contains)
    // Iterate until stable. Bound iterations to (regions * constraints)
    // for safety on pathological inputs.
    bool changed = true;
    int rounds = 0;
    int cap = static_cast<int>(constraints_.size() * region_count() + 16);
    while (changed && rounds++ < cap) {
        changed = false;
        for (auto& c : constraints_) {
            if (!c.longer.valid()) continue;
            auto& dst = region_points_[c.longer.value];
            if (c.kind == RegionConstraint::Kind::Contains) {
                if (dst.insert(c.point).second) changed = true;
            } else if (c.kind == RegionConstraint::Kind::Outlives) {
                if (!c.shorter.valid()) continue;
                auto& src = region_points_[c.shorter.value];
                for (auto& p : src)
                    if (dst.insert(p).second) changed = true;
            }
        }
    }
}

namespace {
// Block-builder helpers. Each CFG block grows by appending statements.
// Branch statements emit nested sub-blocks and connect them via
// successors. Successor list of a block is "where control may go after
// the LAST statement of this block" — empty for terminators (return,
// break, continue) that jump elsewhere.
} // namespace

uint32_t RegionInferer_alloc_block(CFG& cfg) {
    cfg.blocks.emplace_back();
    return static_cast<uint32_t>(cfg.blocks.size() - 1);
}

void RegionInferer::walk_block(const lir::LBlock& blk, uint32_t blk_id,
                                const lir::LProgram& prog) {
    using namespace lir_view;
    using SCode = lir_schema::stmt::Code;

    uint32_t cur = blk_id;
    for (uint32_t i = 0; i < blk.stmts.size(); ++i) {
        auto& s = blk.stmts[i];
        // Statement index within the current CFG block.
        uint32_t local_idx = cfg_.blocks[cur].n_stmts;
        cfg_.blocks[cur].n_stmts = local_idx + 1;

        // Default: walk for borrow sites at this point.
        walk_stmt(s, cur, local_idx, prog);

        // Detect branching / loop statements that need sub-blocks.
        if (s.mirror_offset_ == hermes::arena_offset_t{}) continue;
        StmtRef sr(prog.type_pool.arena(), s.mirror_offset_);
        if (!sr) continue;

        auto block_ptr_of = [&](BlockRef br) -> const lir::LBlock* {
            if (!br) return nullptr;
            auto& m = prog.mirror_table->block_by_offset;
            auto it = m.find(br.offset().value());
            return it == m.end() ? nullptr : it->second;
        };

        switch (sr.kind()) {
            case SCode::If: {
                SIfView v{sr};
                // Allocate then-block + else-block + after-block.
                uint32_t then_id  = RegionInferer_alloc_block(cfg_);
                uint32_t else_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {then_id, else_id};
                if (auto b = block_ptr_of(v.then_block())) walk_block(*b, then_id, prog);
                if (auto b = block_ptr_of(v.else_block())) walk_block(*b, else_id, prog);
                cfg_.blocks[then_id].successors.push_back(after_id);
                cfg_.blocks[else_id].successors.push_back(after_id);
                cur = after_id;
                break;
            }
            case SCode::While: {
                SWhileView v{sr};
                uint32_t body_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {body_id, after_id};
                if (auto b = block_ptr_of(v.body())) walk_block(*b, body_id, prog);
                cfg_.blocks[body_id].successors.push_back(cur);  // back-edge
                cur = after_id;
                break;
            }
            case SCode::For: {
                SForView v{sr};
                uint32_t body_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {body_id, after_id};
                if (auto b = block_ptr_of(v.body())) walk_block(*b, body_id, prog);
                cfg_.blocks[body_id].successors.push_back(cur);  // back-edge
                cur = after_id;
                break;
            }
            case SCode::ForEach: {
                SForEachView v{sr};
                uint32_t body_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {body_id, after_id};
                if (auto b = block_ptr_of(v.body())) walk_block(*b, body_id, prog);
                cfg_.blocks[body_id].successors.push_back(cur);
                cur = after_id;
                break;
            }
            case SCode::Loop: {
                SLoopView v{sr};
                uint32_t body_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {body_id};
                if (auto b = block_ptr_of(v.body())) walk_block(*b, body_id, prog);
                cfg_.blocks[body_id].successors.push_back(body_id);  // back to body
                // `break` inside body wires to after via separate edge;
                // tracked when liveness lands in B71.1. Empty successors
                // on body for now.
                cur = after_id;
                break;
            }
            case SCode::Block: {
                SBlockView v{sr};
                uint32_t inner_id = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {inner_id};
                if (auto b = block_ptr_of(v.body())) walk_block(*b, inner_id, prog);
                cfg_.blocks[inner_id].successors.push_back(after_id);
                cur = after_id;
                break;
            }
            case SCode::Match: {
                SMatchView v{sr};
                std::vector<uint32_t> arm_ids;
                v.each_arm([&](EMatchArmRef arm) {
                    uint32_t arm_id = RegionInferer_alloc_block(cfg_);
                    arm_ids.push_back(arm_id);
                    if (auto b = block_ptr_of(arm.body())) walk_block(*b, arm_id, prog);
                });
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                for (auto id : arm_ids) {
                    cfg_.blocks[cur].successors.push_back(id);
                    cfg_.blocks[id].successors.push_back(after_id);
                }
                cur = after_id;
                break;
            }
            case SCode::LetElse: {
                SLetElseView v{sr};
                uint32_t else_id  = RegionInferer_alloc_block(cfg_);
                uint32_t after_id = RegionInferer_alloc_block(cfg_);
                cfg_.blocks[cur].successors = {else_id, after_id};
                if (auto b = block_ptr_of(v.else_block())) walk_block(*b, else_id, prog);
                // else block diverges; no edge to after.
                cur = after_id;
                break;
            }
            case SCode::Return:
            case SCode::Break:
            case SCode::Continue:
                // Terminators — successors stay empty.
                break;
            default:
                break;
        }
    }
}

void RegionInferer::walk_stmt(const lir::LStmt& s,
                               uint32_t blk_id, uint32_t idx,
                               const lir::LProgram& prog) {
    using namespace lir_view;
    using ECode = lir_schema::expr::Code;
    using SCode = lir_schema::stmt::Code;
    if (s.mirror_offset_ == hermes::arena_offset_t{}) return;
    StmtRef sr(prog.type_pool.arena(), s.mirror_offset_);
    if (!sr) return;
    StmtPoint origin{blk_id, idx};
    const auto* pool = prog.type_pool.impl();
    // B73: remember the source line for this CFG point.
    if (s.line) point_line_.emplace(origin, s.line);

    // B71.1: collect use/def for this point. Runs alongside the
    // borrow walker so we don't traverse the AST twice.
    LiveSet u, d;
    use_def_for_stmt(s, blk_id, idx, prog, u, d);
    use_.emplace(origin, std::move(u));
    def_.emplace(origin, std::move(d));

    // Recursive expression walker — finds borrow sites at any depth.
    // B82: depth of nested fn-call arg evaluation. Borrows taken with
    // depth > 0 are flagged as two-phase reservations.
    int in_call_args_depth = 0;
    std::function<void(ExprRef, const std::string&)> walk_expr;
    walk_expr = [&](ExprRef e, const std::string& holder) {
        if (!e) return;
        switch (e.kind()) {
            case ECode::AddrOf: {
                EAddrOfView v{e};
                BorrowSite bs;
                bs.region = fresh_region();
                bs.origin = origin;
                bs.holder = holder;
                bs.target = std::string(v.var_name());
                bs.is_mut = is_mut_ref_type(e.type(pool));
                bs.is_tpb_reservation = bs.is_mut && in_call_args_depth > 0;
                bs.origin_line = s.line;
                borrows_.push_back(std::move(bs));
                RegionConstraint c;
                c.kind    = RegionConstraint::Kind::Contains;
                c.longer  = borrows_.back().region;
                c.shorter = NO_REGION;
                c.point   = origin;
                constraints_.push_back(c);
                return;
            }
            case ECode::AddrOfTemp: {
                EAddrOfTempView v{e};
                BorrowSite bs;
                bs.region = fresh_region();
                bs.origin = origin;
                bs.holder = holder;
                // Each AddrOfTemp produces its OWN fresh temporary —
                // distinct from every other temp, so tag the target
                // uniquely by region id. Conflict logic compares
                // target strings and would otherwise collapse all
                // temp-borrows into a single phantom variable.
                bs.target = "<temp#" + std::to_string(bs.region.value) + ">";
                bs.is_mut = v.is_mut();
                bs.is_tpb_reservation = bs.is_mut && in_call_args_depth > 0;
                bs.origin_line = s.line;
                borrows_.push_back(std::move(bs));
                RegionConstraint c;
                c.kind    = RegionConstraint::Kind::Contains;
                c.longer  = borrows_.back().region;
                c.shorter = NO_REGION;
                c.point   = origin;
                constraints_.push_back(c);
                walk_expr(v.inner(), "");
                return;
            }
            case ECode::Deref:
                walk_expr(EDerefView{e}.operand(), "");
                return;
            case ECode::FieldRead:
                walk_expr(EFieldReadView{e}.receiver(), "");
                return;
            case ECode::TupleIndex:
                walk_expr(ETupleIndexView{e}.receiver(), "");
                return;
            case ECode::Cast:
                walk_expr(ECastView{e}.operand(), "");
                return;
            case ECode::Unary:
                walk_expr(EUnaryView{e}.operand(), "");
                return;
            case ECode::BinOp: {
                EBinOpView v{e};
                walk_expr(v.lhs(), "");
                walk_expr(v.rhs(), "");
                return;
            }
            case ECode::IndexRead: {
                EIndexReadView v{e};
                walk_expr(v.receiver(), "");
                walk_expr(v.index(),    "");
                return;
            }
            case ECode::IfExpr: {
                EIfExprView v{e};
                walk_expr(v.cond(),     "");
                walk_expr(v.then_val(), holder);
                walk_expr(v.else_val(), holder);
                return;
            }
            case ECode::Call:
                in_call_args_depth++;
                ECallView{e}.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                in_call_args_depth--;
                return;
            case ECode::MethodCall: {
                EMethodCallView v{e};
                walk_expr(v.receiver(), "");
                in_call_args_depth++;
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                in_call_args_depth--;
                return;
            }
            case ECode::ClosureCall: {
                EClosureCallView v{e};
                walk_expr(v.callee(), "");
                in_call_args_depth++;
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                in_call_args_depth--;
                return;
            }
            case ECode::FnPtrCall: {
                EFnPtrCallView v{e};
                walk_expr(v.callee(), "");
                in_call_args_depth++;
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                in_call_args_depth--;
                return;
            }
            case ECode::FormatCall: {
                EFormatCallView v{e};
                walk_expr(v.fmt(), "");
                in_call_args_depth++;
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                in_call_args_depth--;
                return;
            }
            case ECode::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv){
                    walk_expr(fv, "");
                });
                return;
            case ECode::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el){ walk_expr(el, ""); });
                return;
            case ECode::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el){ walk_expr(el, ""); });
                return;
            case ECode::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){
                    walk_expr(pl, "");
                });
                return;
            case ECode::SliceLit: {
                ESliceLitView v{e};
                walk_expr(v.base(), "");
                walk_expr(v.len(),  "");
                return;
            }
            case ECode::SliceIndex: {
                ESliceIndexView v{e};
                walk_expr(v.slice(), "");
                walk_expr(v.index(), "");
                return;
            }
            case ECode::Try:
                walk_expr(ETryView{e}.inner(), "");
                return;
            default:
                return;
        }
    };

    switch (sr.kind()) {
        case SCode::Let: {
            SLetView v{sr};
            std::string name(v.name());
            walk_expr(v.value(), name);
            break;
        }
        case SCode::Assign: {
            SAssignView v{sr};
            walk_expr(v.value(), std::string(v.name()));
            break;
        }
        case SCode::Return: {
            SReturnView v{sr};
            walk_expr(v.value(), "");
            break;
        }
        case SCode::ExprStmt:
            walk_expr(SExprStmtView{sr}.expr(), "");
            break;
        case SCode::FieldWrite:
            walk_expr(SFieldWriteView{sr}.value(), "");
            break;
        case SCode::IndexWrite: {
            SIndexWriteView v{sr};
            walk_expr(v.index(), "");
            walk_expr(v.value(), "");
            break;
        }
        case SCode::FieldIndexWrite: {
            SFieldIndexWriteView v{sr};
            walk_expr(v.index(), "");
            walk_expr(v.value(), "");
            break;
        }
        case SCode::ChainFieldWrite:
            walk_expr(SChainFieldWriteView{sr}.value(), "");
            break;
        case SCode::DerefFieldWrite:
            walk_expr(SDerefFieldWriteView{sr}.value(), "");
            break;
        case SCode::DerefWrite: {
            SDerefWriteView v{sr};
            walk_expr(v.ptr(),   "");
            walk_expr(v.value(), "");
            break;
        }
        case SCode::TupleWrite:
            walk_expr(STupleWriteView{sr}.value(), "");
            break;
        case SCode::If:
            walk_expr(SIfView{sr}.cond(), "");
            break;
        case SCode::While:
            walk_expr(SWhileView{sr}.cond(), "");
            break;
        case SCode::For: {
            SForView v{sr};
            walk_expr(v.lo(), "");
            walk_expr(v.hi(), "");
            break;
        }
        case SCode::ForEach:
            walk_expr(SForEachView{sr}.iter(), "");
            break;
        case SCode::Match:
            walk_expr(SMatchView{sr}.scrut(), "");
            break;
        case SCode::LetElse:
            walk_expr(SLetElseView{sr}.scrut(), "");
            break;
        case SCode::Break:
            walk_expr(SBreakView{sr}.value(), "");
            break;
        default:
            break;
    }
}

void RegionInferer::use_def_for_stmt(const lir::LStmt& s,
                                       uint32_t blk_id, uint32_t idx,
                                       const lir::LProgram& prog,
                                       LiveSet& use, LiveSet& def) const {
    using namespace lir_view;
    using ECode = lir_schema::expr::Code;
    using SCode = lir_schema::stmt::Code;
    (void)blk_id; (void)idx;
    if (s.mirror_offset_ == hermes::arena_offset_t{}) return;
    StmtRef sr(prog.type_pool.arena(), s.mirror_offset_);
    if (!sr) return;

    // Recursive expression walker that records every VarRef / AddrOf
    // target name as a USE. Mutations are recorded as DEF below at
    // the statement level (Let/Assign LHS).
    std::function<void(ExprRef)> walk_use;
    walk_use = [&](ExprRef e) {
        if (!e) return;
        switch (e.kind()) {
            case ECode::VarRef:
                use.insert(std::string(EVarRefView{e}.name()));
                return;
            case ECode::AddrOf:
                use.insert(std::string(EAddrOfView{e}.var_name()));
                return;
            case ECode::AddrOfTemp:
                walk_use(EAddrOfTempView{e}.inner());
                return;
            case ECode::Deref:    walk_use(EDerefView{e}.operand());   return;
            case ECode::FieldRead: walk_use(EFieldReadView{e}.receiver()); return;
            case ECode::TupleIndex: walk_use(ETupleIndexView{e}.receiver()); return;
            case ECode::Cast:     walk_use(ECastView{e}.operand());   return;
            case ECode::Unary:    walk_use(EUnaryView{e}.operand());  return;
            case ECode::BinOp: {
                EBinOpView v{e};
                walk_use(v.lhs()); walk_use(v.rhs());
                return;
            }
            case ECode::IndexRead: {
                EIndexReadView v{e};
                walk_use(v.receiver()); walk_use(v.index());
                return;
            }
            case ECode::IfExpr: {
                EIfExprView v{e};
                walk_use(v.cond()); walk_use(v.then_val()); walk_use(v.else_val());
                return;
            }
            case ECode::Call:
                ECallView{e}.each_arg([&](ExprRef a){ walk_use(a); });
                return;
            case ECode::MethodCall: {
                EMethodCallView v{e};
                walk_use(v.receiver());
                v.each_arg([&](ExprRef a){ walk_use(a); });
                return;
            }
            case ECode::ClosureCall: {
                EClosureCallView v{e};
                walk_use(v.callee());
                v.each_arg([&](ExprRef a){ walk_use(a); });
                return;
            }
            case ECode::FnPtrCall: {
                EFnPtrCallView v{e};
                walk_use(v.callee());
                v.each_arg([&](ExprRef a){ walk_use(a); });
                return;
            }
            case ECode::FormatCall: {
                EFormatCallView v{e};
                walk_use(v.fmt());
                v.each_arg([&](ExprRef a){ walk_use(a); });
                return;
            }
            case ECode::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv){ walk_use(fv); });
                return;
            case ECode::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el){ walk_use(el); });
                return;
            case ECode::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el){ walk_use(el); });
                return;
            case ECode::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){ walk_use(pl); });
                return;
            case ECode::SliceLit: {
                ESliceLitView v{e};
                walk_use(v.base()); walk_use(v.len());
                return;
            }
            case ECode::SliceIndex: {
                ESliceIndexView v{e};
                walk_use(v.slice()); walk_use(v.index());
                return;
            }
            case ECode::Try:
                walk_use(ETryView{e}.inner());
                return;
            default:
                return;
        }
    };

    switch (sr.kind()) {
        case SCode::Let: {
            SLetView v{sr};
            walk_use(v.value());
            def.insert(std::string(v.name()));
            break;
        }
        case SCode::Assign: {
            SAssignView v{sr};
            walk_use(v.value());
            def.insert(std::string(v.name()));
            break;
        }
        case SCode::Return:
            walk_use(SReturnView{sr}.value());
            break;
        case SCode::ExprStmt:
            walk_use(SExprStmtView{sr}.expr());
            break;
        case SCode::FieldWrite: {
            SFieldWriteView v{sr};
            use.insert(std::string(v.receiver()));
            walk_use(v.value());
            break;
        }
        case SCode::IndexWrite: {
            SIndexWriteView v{sr};
            use.insert(std::string(v.arr()));
            walk_use(v.index());
            walk_use(v.value());
            break;
        }
        case SCode::FieldIndexWrite: {
            SFieldIndexWriteView v{sr};
            use.insert(std::string(v.receiver()));
            walk_use(v.index());
            walk_use(v.value());
            break;
        }
        case SCode::ChainFieldWrite: {
            SChainFieldWriteView v{sr};
            use.insert(std::string(v.receiver()));
            walk_use(v.value());
            break;
        }
        case SCode::DerefFieldWrite: {
            SDerefFieldWriteView v{sr};
            use.insert(std::string(v.receiver()));
            walk_use(v.value());
            break;
        }
        case SCode::DerefWrite: {
            SDerefWriteView v{sr};
            walk_use(v.ptr());
            walk_use(v.value());
            break;
        }
        case SCode::TupleWrite: {
            STupleWriteView v{sr};
            use.insert(std::string(v.receiver()));
            walk_use(v.value());
            break;
        }
        case SCode::If:
            walk_use(SIfView{sr}.cond());
            break;
        case SCode::While:
            walk_use(SWhileView{sr}.cond());
            break;
        case SCode::For: {
            SForView v{sr};
            walk_use(v.lo()); walk_use(v.hi());
            def.insert(std::string(v.var()));
            break;
        }
        case SCode::ForEach: {
            SForEachView v{sr};
            walk_use(v.iter());
            def.insert(std::string(v.var()));
            break;
        }
        case SCode::Match:
            walk_use(SMatchView{sr}.scrut());
            break;
        case SCode::LetElse:
            walk_use(SLetElseView{sr}.scrut());
            break;
        case SCode::Break:
            walk_use(SBreakView{sr}.value());
            break;
        default:
            break;
    }
}

void RegionInferer::compute_liveness() {
    // Backward dataflow:
    //   live_out(P) = ∪ live_in(succ(P))
    //   live_in (P) = use(P) ∪ (live_out(P) \ def(P))
    //
    // Iterate over every (block, idx) point until a fixed point.
    // Successor of a point within a block is the next statement; the
    // successor of the last statement is the union of live_in of the
    // first statements of the block's CFG successor blocks. For a
    // block with zero stmts (e.g. a synthesized after-block), its
    // "live-in" is the union of its successors' live-ins.
    auto first_point_live_in = [&](uint32_t b) -> LiveSet {
        const auto& B = cfg_.blocks[b];
        if (B.n_stmts == 0) {
            // No stmts — propagate from successors. We avoid recursion by
            // returning empty here; the fixpoint pass will populate.
            LiveSet acc;
            for (auto s : B.successors) {
                auto it = live_in_.find(StmtPoint{s, 0});
                if (it != live_in_.end()) acc.insert(it->second.begin(), it->second.end());
            }
            return acc;
        }
        StmtPoint p0{b, 0};
        auto it = live_in_.find(p0);
        return it == live_in_.end() ? LiveSet{} : it->second;
    };

    bool changed = true;
    int rounds = 0;
    while (changed && rounds++ < 64) {
        changed = false;
        // Visit blocks in reverse order, statements backward within block.
        for (uint32_t bi = static_cast<uint32_t>(cfg_.blocks.size()); bi-- > 0; ) {
            const auto& B = cfg_.blocks[bi];
            // First compute live-out of the last statement.
            for (int32_t si = static_cast<int32_t>(B.n_stmts) - 1; si >= 0; --si) {
                StmtPoint p{bi, static_cast<uint32_t>(si)};
                LiveSet out;
                if (si + 1 < static_cast<int32_t>(B.n_stmts)) {
                    // Successor is next stmt in same block.
                    auto it = live_in_.find(StmtPoint{bi, static_cast<uint32_t>(si + 1)});
                    if (it != live_in_.end()) out = it->second;
                } else {
                    // Last stmt: live_out is union of successors' live_in.
                    for (auto s : B.successors) {
                        auto ls = first_point_live_in(s);
                        for (auto& v : ls) out.insert(v);
                    }
                }
                // in = use ∪ (out \ def)
                auto& u = use_[p];
                auto& d = def_[p];
                LiveSet in = u;
                for (auto& v : out)
                    if (!d.count(v)) in.insert(v);

                auto& cur_out = live_out_[p];
                auto& cur_in  = live_in_[p];
                if (cur_out != out) { cur_out = std::move(out); changed = true; }
                if (cur_in  != in)  { cur_in  = std::move(in);  changed = true; }
            }
        }
    }
}

std::vector<RegionInferer::Conflict>
RegionInferer::find_conflicts() const {
    std::vector<Conflict> out;
    for (size_t i = 0; i < borrows_.size(); ++i) {
        for (size_t j = i + 1; j < borrows_.size(); ++j) {
            const auto& a = borrows_[i];
            const auto& b = borrows_[j];
            // Same target var (transient `<temp>` matches transient).
            if (a.target != b.target) continue;
            // At least one must be mut for a conflict.
            if (!a.is_mut && !b.is_mut) continue;
            // B82: a mut-reservation taken as a call argument is compatible
            // with concurrent shared reads of the same target (TPB).
            // A reservation still conflicts with another mut or reservation.
            if ((a.is_tpb_reservation && !b.is_mut) ||
                (b.is_tpb_reservation && !a.is_mut))
                continue;
            auto ait = region_points_.find(a.region.value);
            auto bit = region_points_.find(b.region.value);
            if (ait == region_points_.end() || bit == region_points_.end()) continue;
            const PointSet& ap = ait->second;
            const PointSet& bp = bit->second;
            // Find the first overlapping point (stable: iterate smaller).
            const PointSet& small = (ap.size() < bp.size()) ? ap : bp;
            const PointSet& big   = (ap.size() < bp.size()) ? bp : ap;
            for (auto& p : small) {
                if (big.count(p)) {
                    out.push_back(Conflict{&a, &b, p});
                    break;
                }
            }
        }
    }
    return out;
}

void RegionInferer::dump(const std::string& fn_name) const {
    if (!std::getenv("LOGOS_DUMP_REGIONS")) return;
    std::fprintf(stderr, "[regions] fn '%s' — %u regions, %zu borrows, "
                         "%zu constraints, %zu CFG blocks\n",
                 fn_name.c_str(), region_count(),
                 borrows_.size(), constraints_.size(),
                 cfg_.blocks.size());
    for (size_t bi = 0; bi < cfg_.blocks.size(); ++bi) {
        auto& B = cfg_.blocks[bi];
        std::string succ;
        for (auto s : B.successors) succ += std::to_string(s) + " ";
        std::fprintf(stderr,
            "  block %zu: n_stmts=%u successors=[%s]\n",
            bi, B.n_stmts, succ.c_str());
    }
    for (auto& b : borrows_) {
        std::fprintf(stderr,
            "  borrow #%u from '%s' to '%s' is_mut=%d at (%u, %u)\n",
            b.region.value, b.target.c_str(), b.holder.c_str(),
            b.is_mut, b.origin.block, b.origin.idx);
    }
    for (auto& c : constraints_) {
        const char* kn = (c.kind == RegionConstraint::Kind::Outlives)
            ? "outlives" : "contains";
        std::fprintf(stderr,
            "  constraint %s longer=%u shorter=%u point=(%u, %u)\n",
            kn, c.longer.value, c.shorter.value, c.point.block, c.point.idx);
    }
    // Solved region point sets.
    for (auto& [rid, pts] : region_points_) {
        std::string ps;
        for (auto& p : pts)
            ps += "(" + std::to_string(p.block) + "," + std::to_string(p.idx) + ") ";
        std::fprintf(stderr, "  region %u live at {%s}\n", rid, ps.c_str());
    }
    // Per-statement live-in (live-out is implied by the live-in of
    // the next stmt or successor block).
    for (size_t bi = 0; bi < cfg_.blocks.size(); ++bi) {
        for (uint32_t si = 0; si < cfg_.blocks[bi].n_stmts; ++si) {
            StmtPoint p{static_cast<uint32_t>(bi), si};
            auto it = live_in_.find(p);
            if (it == live_in_.end() || it->second.empty()) continue;
            std::string vars;
            for (auto& v : it->second) vars += v + " ";
            std::fprintf(stderr, "  live_in (%zu, %u) = {%s}\n", bi, si, vars.c_str());
        }
    }
}

} // namespace logos::compiler
