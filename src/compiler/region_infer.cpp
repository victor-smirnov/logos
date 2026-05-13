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
    next_region_id_ = 1;

    cfg_.blocks.emplace_back();
    walk_block(fn.body, /*blk_id=*/0, prog);
}

void RegionInferer::walk_block(const lir::LBlock& blk, uint32_t blk_id,
                                const lir::LProgram& prog) {
    auto& B = cfg_.blocks[blk_id];
    B.n_stmts = static_cast<uint32_t>(blk.stmts.size());
    for (uint32_t i = 0; i < blk.stmts.size(); ++i)
        walk_stmt(blk.stmts[i], blk_id, i, prog);
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

    // Recursive expression walker — finds borrow sites at any depth.
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
                bs.target = "<temp>";
                bs.is_mut = v.is_mut();
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
                ECallView{e}.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                return;
            case ECode::MethodCall: {
                EMethodCallView v{e};
                walk_expr(v.receiver(), "");
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                return;
            }
            case ECode::ClosureCall: {
                EClosureCallView v{e};
                walk_expr(v.callee(), "");
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                return;
            }
            case ECode::FnPtrCall: {
                EFnPtrCallView v{e};
                walk_expr(v.callee(), "");
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                return;
            }
            case ECode::FormatCall: {
                EFormatCallView v{e};
                walk_expr(v.fmt(), "");
                v.each_arg([&](ExprRef a){ walk_expr(a, ""); });
                return;
            }
            case ECode::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv){
                    walk_expr(fv, "");
                });
                return;
            case ECode::New:
                ENewView{e}.each_field_value([&](ExprRef fv){
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
        case SCode::Delete:
            walk_expr(SDeleteView{sr}.expr(), "");
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

void RegionInferer::dump(const std::string& fn_name) const {
    if (!std::getenv("LOGOS_DUMP_REGIONS")) return;
    std::fprintf(stderr, "[regions] fn '%s' — %u regions, %zu borrows, "
                         "%zu constraints, %zu CFG blocks\n",
                 fn_name.c_str(), region_count(),
                 borrows_.size(), constraints_.size(),
                 cfg_.blocks.size());
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
}

} // namespace logos::compiler
