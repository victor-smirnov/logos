name: refmono
file: src/compiler/mono_clone.cpp
---
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "+")  method_name = "add";
---
            // PROBE 2026-09-15a-refeq (refmono / refeqx): after substitution, a comparison of a reference pair to one
            // struct calls the pointee's comparison method with the references themselves (the by-value arm below passes
            // the values; a generic `impl<T: Eq> Eq for W<T>` is only reachable here, sema's lookup misses it).
            if (lt && new_rhs && (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") &&
                (lt.kind() == LogosType::Kind::Ref || lt.kind() == LogosType::Kind::MutRef) && lt.pointee() &&
                lt.pointee().kind() == LogosType::Kind::Struct) {
                auto rt3 = new_rhs.type(out_.type_pool.impl());
                if (rt3 && (rt3.kind() == LogosType::Kind::Ref || rt3.kind() == LogosType::Kind::MutRef) && rt3.pointee() &&
                    rt3.pointee().kind() == LogosType::Kind::Struct &&
                    (logos::probe::on("refmono") || logos::probe::on("refeqx"))) {
                    std::string mname = op == "==" ? "eq" : op == "!=" ? "ne" : op == "<" ? "lt" : op == "<=" ? "le"
                                      : op == ">" ? "gt" : "ge";
                    TypeRef pt = lt.pointee();
                    std::string bare = concrete_struct_name(pt) + "__" + mname;
                    std::string pkg{pt.pkg_name()};
                    std::string callee = pkg.empty() ? bare : pkg + "." + bare;
                    std::vector<lir_view::ExprRef> args;
                    args.push_back(std::move(new_lhs));
                    args.push_back(std::move(new_rhs));
                    logos::probe::census("refeq.mono.refpair.Struct.routed");
                    mp_ = lir_mirror_emit_call(out_, rt_, callee, {}, args);
                    break;
                }
            }
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "+")  method_name = "add";
===
