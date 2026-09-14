name: a2elem
file: src/compiler/borrow_check.cpp
---
                for (auto& p : ref_sources_of(src))
                    reborrow_of_.add(dst, p);
---
                // PROBES 2026-09-14l-storeedge (a2skipall / a2elem / a2elemshr): no A2 alias edge when the
                // out-param STORES the operand as a container element (operand type == an element type arg).
                bool a2skip_ = logos::probe::on("a2skipall");
                if (!a2skip_ && (logos::probe::on("a2elem") || logos::probe::on("a2elemshr"))) {
                    TypeRef ct_ = ops[j] ? ops[j].type(pool) : TypeRef(nullptr);
                    for (int pk = 0; pk < 4 && ct_ && is_ref_kind(ct_) &&
                             ct_.kind() != LogosType::Kind::TraitObject; ++pk)
                        ct_ = ct_.elem();
                    if (!ct_) ct_ = holder_ty_of(dst);
                    for (int pk = 0; pk < 4 && ct_ && is_ref_kind(ct_) &&
                             ct_.kind() != LogosType::Kind::TraitObject; ++pk)
                        ct_ = ct_.elem();
                    bool elem_ = false;
                    if (ct_ && st)
                        for (auto el : ct_.type_args())
                            if (el == st) { elem_ = true; break; }
                    if (elem_ && (logos::probe::on("a2elem") ||
                                  st.kind() == LogosType::Kind::Ref))
                        a2skip_ = true;
                }
                if (!a2skip_)
                for (auto& p : ref_sources_of(src))
                    reborrow_of_.add(dst, p);
