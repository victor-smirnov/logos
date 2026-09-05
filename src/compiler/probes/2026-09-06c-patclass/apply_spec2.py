#!/usr/bin/env python3
# spec2 — applied ON TOP of patclass.spec (the batch tool refuses a dirty tree):
#   ifletown2  = ifletown + the while-let binding frame IS the loop boundary (lower_for_each's frame B)
#              + substructmv
#   substructmv = the nested-STRUCT sub-pattern prologue marks the synth temp's field moved (the tuple branch's line)
import io, sys
def rep(path, old, new, count):
    s = io.open(path).read()
    n = s.count(old)
    assert n == count, (path, old[:60], n, count)
    io.open(path, "w").write(s.replace(old, new))
P = "src/compiler/sema_stmt.cpp"
rep(P, 'logos::probe::on("ifletown") || logos::probe::on("ifletmv")',
       'logos::probe::on("ifletown") || logos::probe::on("ifletown2") || logos::probe::on("ifletmv")', 2)
rep(P, 'logos::probe::on("ifletown") || logos::probe::on("ifletdr")',
       'logos::probe::on("ifletown") || logos::probe::on("ifletown2") || logos::probe::on("ifletdr")', 2)
rep(P, """        // Then arm: pattern → loop body
        push_scope();
        bind_pattern(pat, scrut_type);
""", """        // Then arm: pattern → loop body
        push_scope();
        if (logos::probe::on("ifletown2")) scope_.back().loop_boundary = true;  // PROBE ifletown2: lower_for_each's frame B
        bind_pattern(pat, scrut_type);
""", 1)
rep(P, """        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
""", """        if (!logos::probe::on("ifletown2")) pending_loop_body_scope_ = true;  // G167-4: tag the body frame (PROBE ifletown2: the binding frame is the boundary)
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
""", 1)
rep(P, """            auto fr = builder().field_read(std::move(sref), fname, ftype);
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
""", """            auto fr = builder().field_read(std::move(sref), fname, ftype);
            if ((logos::probe::on("substructmv") || logos::probe::on("ifletown2")) && is_move_type(ftype))
                mark_moved_expr(expr_ref_of(fr));  // PROBE substructmv: the binding moves the field OUT of the synth (the tuple branch's line)
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
""", 1)
print("spec2 applied")
