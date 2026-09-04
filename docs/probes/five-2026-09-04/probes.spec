name: bxfldmv
file: src/compiler/sema_impl.hpp
---
                        if (is_deref_or_index_call(op)) {
                            if (!is_box_deref_call_(op)) return true;
                            x = call_recv_(op);
                            break;
                        }
---
                        if (is_deref_or_index_call(op)) {
                            if (!is_box_deref_call_(op)) return true;
                            // PROBE bxfldmv (2026-09-04five, root 1): the Box hop is
                            // TRANSPARENT here and `bx.field` of a move-typed field
                            // then DOUBLE-FREES at run time (no DerefMove lowering for
                            // a FIELD). The crude arm turns the miscompile into a
                            // refusal by treating the Box hop as unowned.
                            if (logos::probe::on("bxfldmv")) return true;
                            x = call_recv_(op);
                            break;
                        }
===
name: dstrbind
file: src/compiler/sema_stmt.cpp
---
            if (has_sub && code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME)) {
                bind_name = std::string(str_of(sub.get(la::NAME.code)));
                has_sub = false;  // simple alias — treat as name bind
            }
---
            if (has_sub && code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME)) {
                bind_name = std::string(str_of(sub.get(la::NAME.code)));
                has_sub = false;  // simple alias — treat as name bind
            }
            // PROBE dstrbind (2026-09-04five, root 2): this loop emits a plain
            // by-value field READ for every field spelling, so `ref v` copies and
            // `_` binds — both double-free. The crude arm emits NOTHING for either
            // spelling (correct for `_`, a refusal-by-omission for `ref`).
            if (logos::probe::on("dstrbind")) {
                auto probe_isref_ = [&](TinyMapView n) {
                    return n.has_key(la::IS_REF) &&
                           n.get(la::IS_REF.code).is_value() &&
                           n.get(la::IS_REF.code).as_value<uint8_t>() != 0;
                };
                bool probe_skip_ = probe_isref_(fnode) || bind_name == "_";
                if (fnode.has_key(la::VALUE)) {
                    auto probe_sv_ = map_of(fnode.get(la::VALUE.code));
                    if (code_of(probe_sv_) == la::PAT_WILD && probe_isref_(probe_sv_))
                        probe_skip_ = true;
                }
                if (probe_skip_) continue;
            }
===
name: dropident
file: src/compiler/sema.cpp
---
    std::string mangled = type_name + "__drop";
    // B-mv-02: a candidate Drop impl must belong to the SAME package as `t`.
---
    std::string mangled = type_name + "__drop";
    // PROBE dropident (2026-09-04five, root 3): the destructor is keyed on the
    // MANGLED NAME, so an inherent `drop` or a USER trait's `drop` runs at scope
    // exit. `SemaFuncInfo::trait_name` is the fact, and explicit_destructor_call
    // already reads it off this same candidate list. PROBES.md 2026-09-02u §3.
    // The arm is the EXISTING reader, called: no second copy of the identity
    // test, and no new bare-name intercept for key_identity_lint to census.
    if (logos::probe::on("dropident") &&
        !const_cast<SemaChecker*>(this)->explicit_destructor_call(t))
        return {};
    // B-mv-02: a candidate Drop impl must belong to the SAME package as `t`.
===
name: clowndyn
file: src/compiler/mlir_gen_dyn.cpp
---
            if (capture_is_dyn[i]) continue;  // dyn handle is a borrow
---
            // PROBE clowndyn (2026-09-04five, root 4): an OWNING `Box<dyn Tr>` IS
            // owned storage, and leaving it a borrow makes an escaping `move`
            // closure read its defining fn's dead slot. The owning bit is on the
            // capture's own TypeRef (trait_owning_kind), read at
            // gen_drop_owning_dyn_handle already.
            if (capture_is_dyn[i] &&
                !(logos::probe::on("clowndyn") &&
                  TypeRef(capture_types[i]).owning_trait_object()))
                continue;  // dyn handle is a borrow
===
name: derefclos
file: src/compiler/mlir_gen_expr.cpp
---
    if (type && ref_repr_of(type) == RefReprKind::FatSlice)
        return ptr;
    auto pointee = logos_to_mlir(type);
---
    if (type && ref_repr_of(type) == RefReprKind::FatSlice)
        return ptr;
    // PROBE derefclos (2026-09-04five, root 5): the FatClosure kind is on the LOAD
    // path by the comment above, and `Box::new(closure)` stores the {fn,env} pair
    // INLINE (box_new memcpys 16 B), so `Box__deref` already yields the pair's
    // address and the load is one indirection too many — SIGSEGV.
    if (type && ref_repr_of(type) == RefReprKind::FatClosure &&
        logos::probe::on("derefclos"))
        return ptr;
    auto pointee = logos_to_mlir(type);
===
