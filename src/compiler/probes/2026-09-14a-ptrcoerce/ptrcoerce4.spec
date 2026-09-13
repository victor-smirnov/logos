name: refptrcod
file: include/logos/compiler/subtype.hpp
---
    if (!(LogosType::is_fn_value_kind(sub.kind()) &&
          LogosType::is_fn_value_kind(sup.kind())) &&
        sub.kind() != sup.kind())
        return true;
---
    // PROBES 2026-09-14a-ptrcoerce batch 4: refptrcod / ptrcoerced / crosskindxd / pcuniond — a `&T`/`&mut T` ->
    // `*const U`/`*mut U` coercion leaves through the kind-mismatch exit below with no pointee variance. `refptrco`
    // (batch 2's spelling, hand-armable only) compares the pointees as written and REFUSED three legal pass fixtures in
    // the runtime column: types_compatible's array DECAY (`&mut [T; N]` -> `*mut T`, sema.cpp) reaches here too. The `d`
    // spelling compares the ELEMENT for a decay, and asks `*mut` invariance as subtype both ways so it stays exactly as
    // structurally lenient as types_compatible.
    if ((sub.kind() == K::Ref || sub.kind() == K::MutRef) && sup.kind() == K::Ptr &&
        sub.pointee() && sup.pointee()) {
        logos::probe::census("ptrcoerce.refptr.arrive");
        if (logos::probe::on("refptrco")) {
            if (sup.mut_ptr())
                return detail::types_equal_with_lifetimes(sub.pointee(), sup.pointee(), &adj, permissive_empty);
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
        if (logos::probe::on("refptrcod") || logos::probe::on("ptrcoerced") ||
            logos::probe::on("crosskindxd") || logos::probe::on("pcuniond")) {
            TypeRef sp_ = sub.pointee();
            if (sp_.kind() == K::Array && sup.pointee().kind() != K::Array && sp_.elem()) {
                logos::probe::census("ptrcoerce.refptr.decay");
                sp_ = sp_.elem();
            }
            if (!subtype(sp_, sup.pointee(), adj, vars, depth + 1, permissive_empty)) return false;
            if (sup.mut_ptr())
                return subtype(sup.pointee(), sp_, adj, vars, depth + 1, permissive_empty);
            return true;
        }
    }
    // crosskindxd / pcuniond only — the strict extension to the other region-bearing cross-kind pairs
    // types_compatible accepts: `&mut T -> &U` (lifetime Co, pointee Co) and `&Vec<T>`/`&mut Vec<T>` -> `&[U]` (element
    // Co). No struct-name test: types_compatible admits Ref/MutRef(Struct) -> Slice for the stdlib Vec only.
    if (logos::probe::on("crosskindxd") || logos::probe::on("pcuniond")) {
        if (sub.kind() == K::MutRef && sup.kind() == K::Ref && sub.pointee() && sup.pointee()) {
            logos::probe::census("ptrcoerce.mutref_ref");
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj, permissive_empty))
                return false;
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
        if ((sub.kind() == K::Ref || sub.kind() == K::MutRef) && sup.kind() == K::Slice && sub.pointee() &&
            sub.pointee().kind() == K::Struct && sub.pointee().type_args().size() == 1 && sup.elem()) {
            logos::probe::census("ptrcoerce.vec_slice");
            return subtype(sub.pointee().type_args()[0], sup.elem(), adj, vars, depth + 1, permissive_empty);
        }
    }
    if (!(LogosType::is_fn_value_kind(sub.kind()) &&
          LogosType::is_fn_value_kind(sup.kind())) &&
        sub.kind() != sup.kind())
        return true;
===
name: ptrcoerced
file: include/logos/compiler/subtype.hpp
---
            if (sub.mut_ptr() != sup.mut_ptr()) return true;  // shape diff
---
            if (sub.mut_ptr() != sup.mut_ptr()) {
                // PROBES 2026-09-14a-ptrcoerce batch 4: mutconstco (hand) / ptrcoerced / crosskindxd / pcuniond —
                // `*mut T -> *const U` leaves here as a "shape diff"; dropping write permission keeps the pointee Co.
                if (sub.mut_ptr() && (logos::probe::on("mutconstco") || logos::probe::on("ptrcoerced") ||
                                      logos::probe::on("crosskindxd") || logos::probe::on("pcuniond"))) {
                    logos::probe::census("ptrcoerce.mutconst");
                    return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
                }
                return true;  // shape diff
            }
===
name: pcuniond
file: src/compiler/borrow_check.cpp
---
                int sk = method_self_kind(v);
                if (sk >= 1)
                    check_recv_conflict(extract_borrow_place(recv, pool),
                                        /*is_mut=*/sk == 2, line);
            }
---
                int sk = method_self_kind(v);
                if (sk >= 1)
                    check_recv_conflict(extract_borrow_place(recv, pool),
                                        /*is_mut=*/sk == 2, line);
                // PROBES 2026-09-14a-ptrcoerce batch 4: aorecvsk (hand) / pcuniond — a receiver sema built as an explicit
                // `&mut v` (Code::AddrOf, MutRef type: try_index_mut_assign's `v[i] = x` on a Vec) reaches the check above
                // with sk == 2, but extract_borrow_place does not root an AddrOf and check_recv_conflict returns at
                // `bp.root.empty()`. Root the place at the AddrOf's variable.
                if (sk == 2 && recv.kind() == Code::AddrOf && is_mut_ref(recv.type(pool)) &&
                    extract_borrow_place(recv, pool).root.empty() &&
                    (logos::probe::on("aorecvsk") || logos::probe::on("pcuniond"))) {
                    BorrowPlace ap_{};
                    ap_.root = std::string(EAddrOfView{recv}.var_name());
                    ap_.root_type = TypeRef(recv.type(pool)).pointee();
                    check_recv_conflict(ap_, /*is_mut=*/true, line);
                }
            }
===
name: crosskindxd
file: include/logos/compiler/subtype.hpp
---
// Concretely: returns false only when sub/sup share a kind that has a
// variance rule and the lifetime-aware structure disagrees.
---
// Concretely: returns false only when sub/sup share a kind that has a
// variance rule and the lifetime-aware structure disagrees.
// (PROBES 2026-09-14a-ptrcoerce batch 4: ptrcoerced = refptrcod ∪ mutconst, crosskindxd = ptrcoerced ∪ the two strict
// extensions, pcuniond = crosskindxd ∪ aorecvsk.)
