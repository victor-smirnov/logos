name: a2elemorshr
file: src/compiler/borrow_check.cpp
---
                for (auto& p : ref_sources_of(src))
                    reborrow_of_.add(dst, p);
---
                // PROBES 2026-09-14l-storeedge batch 2 (a2shr / a2elemorshr): no A2 alias edge when the operand is a
                // SHARED `&` (a store through which nothing can be written), or (a2elemorshr) also when it is stored
                // as a container element (operand type == an element type arg).
                bool a2skip_ = false;
                const bool a2shr_on_ = logos::probe::on("a2shr");
                const bool a2eos_on_ = logos::probe::on("a2elemorshr");
                if ((a2shr_on_ || a2eos_on_) && st && st.kind() == LogosType::Kind::Ref)
                    a2skip_ = true;
                if (!a2skip_ && a2eos_on_) {
                    TypeRef ct_ = ops[j] ? ops[j].type(pool) : TypeRef(nullptr);
                    for (int pk = 0; pk < 4 && ct_ && is_ref_kind(ct_) &&
                             ct_.kind() != LogosType::Kind::TraitObject; ++pk)
                        ct_ = ct_.elem();
                    if (!ct_) ct_ = holder_ty_of(dst);
                    for (int pk = 0; pk < 4 && ct_ && is_ref_kind(ct_) &&
                             ct_.kind() != LogosType::Kind::TraitObject; ++pk)
                        ct_ = ct_.elem();
                    if (ct_ && st)
                        for (auto el : ct_.type_args())
                            if (el == st) { a2skip_ = true; break; }
                }
                if (!a2skip_)
                for (auto& p : ref_sources_of(src))
                    reborrow_of_.add(dst, p);
