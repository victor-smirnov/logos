name: refptrco
file: include/logos/compiler/subtype.hpp
---
    if (!(LogosType::is_fn_value_kind(sub.kind()) &&
          LogosType::is_fn_value_kind(sup.kind())) &&
        sub.kind() != sup.kind())
        return true;
---
    // PROBES 2026-09-14a-ptrcoerce: refptrco / ptrcoerce / crosskindx / pcunion — a `&T`/`&mut T` -> `*const U`/`*mut U`
    // coercion leaves through the kind-mismatch exit below, so its pointee variance is never asked; the Ptr arm's own
    // rule (*const Co, *mut Inv) is applied to it instead.
    if ((sub.kind() == K::Ref || sub.kind() == K::MutRef) && sup.kind() == K::Ptr &&
        sub.pointee() && sup.pointee()) {
        logos::probe::census("ptrcoerce.refptr.arrive");
        if (logos::probe::on("refptrco") || logos::probe::on("ptrcoerce") ||
            logos::probe::on("crosskindx") || logos::probe::on("pcunion")) {
            if (sup.mut_ptr())
                return detail::types_equal_with_lifetimes(sub.pointee(), sup.pointee(), &adj, permissive_empty);
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
    }
    // crosskindx / pcunion only — the strict extension to the other region-bearing cross-kind pairs types_compatible
    // accepts: `&mut T -> &U` (lifetime Co, pointee Co) and `&Vec<T>`/`&mut Vec<T>` -> `&[U]` (element Co).
    if (logos::probe::on("crosskindx") || logos::probe::on("pcunion")) {
        if (sub.kind() == K::MutRef && sup.kind() == K::Ref && sub.pointee() && sup.pointee()) {
            logos::probe::census("ptrcoerce.mutref_ref");
            if (!detail::lifetime_at(Variance::Co, sub.lifetime(), sup.lifetime(), adj, permissive_empty))
                return false;
            return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
        }
        // (no struct-name test: types_compatible admits Ref/MutRef(Struct) -> Slice for the stdlib Vec only, and
        // subtype() is asked after it)
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
name: mutconstco
file: include/logos/compiler/subtype.hpp
---
            if (sub.mut_ptr() != sup.mut_ptr()) return true;  // shape diff
---
            if (sub.mut_ptr() != sup.mut_ptr()) {
                // PROBES 2026-09-14a-ptrcoerce: mutconstco / ptrcoerce / crosskindx / pcunion — `*mut T -> *const U`
                // leaves here as a "shape diff"; dropping write permission keeps the pointee Co.
                if (sub.mut_ptr() && (logos::probe::on("mutconstco") || logos::probe::on("ptrcoerce") ||
                                      logos::probe::on("crosskindx") || logos::probe::on("pcunion"))) {
                    logos::probe::census("ptrcoerce.mutconst");
                    return subtype(sub.pointee(), sup.pointee(), adj, vars, depth + 1, permissive_empty);
                }
                return true;  // shape diff
            }
===
name: ptrcoerce
file: include/logos/compiler/subtype.hpp
---
// Concretely: returns false only when sub/sup share a kind that has a
// variance rule and the lifetime-aware structure disagrees.
---
// Concretely: returns false only when sub/sup share a kind that has a
// variance rule and the lifetime-aware structure disagrees.
// (PROBES 2026-09-14a-ptrcoerce: `ptrcoerce` = refptrco ∪ mutconstco, `crosskindx` = ptrcoerce ∪ the two strict
// extensions, `pcunion` = crosskindx ∪ aorecvsk — names armed at the sites below.)
===
name: aorecvsk
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
                // PROBES 2026-09-14a-ptrcoerce batch 2: aorecvsk / aorecvty / pcunion — a receiver sema built as an
                // explicit `&mut v` (Code::AddrOf, MutRef type: try_index_mut_assign's `v[i] = x` on a Vec) reaches the
                // check above with sk == 2, but extract_borrow_place does not root an AddrOf, so check_recv_conflict
                // returns at `bp.root.empty()` and nothing is asked. Root the place at the AddrOf's variable.
                if (recv.kind() == Code::AddrOf && is_mut_ref(recv.type(pool)) &&
                    extract_borrow_place(recv, pool).root.empty()) {
                    logos::probe::census(sk == 2 ? "aorecv.sk2" : "aorecv.skother");
                    if ((sk == 2 && (logos::probe::on("aorecvsk") || logos::probe::on("pcunion"))) ||
                        logos::probe::on("aorecvty")) {
                        BorrowPlace ap_{};
                        ap_.root = std::string(EAddrOfView{recv}.var_name());
                        ap_.root_type = TypeRef(recv.type(pool)).pointee();
                        check_recv_conflict(ap_, /*is_mut=*/true, line);
                    }
                }
            }
===
name: aorecvty
file: src/compiler/borrow_check.cpp
---
    // Receiver self-kind of a method call: 0 = none/by-value, 1 = `&self`, 2 =
---
    // (PROBES 2026-09-14a-ptrcoerce batch 2: `aorecvty` arms the MethodCall-arm AddrOf receiver check at every self kind.)
    // Receiver self-kind of a method call: 0 = none/by-value, 1 = `&self`, 2 =
===
name: crosskindx
file: include/logos/compiler/subtype.hpp
---
// Semantics: returns true when `sub` is variance-compatible with `sup` as far
---
// (PROBES 2026-09-14a-ptrcoerce: crosskindx arms the strict-extension block above the kind-mismatch exit.)
// Semantics: returns true when `sub` is variance-compatible with `sup` as far
===
name: pcunion
file: include/logos/compiler/subtype.hpp
---
// Definition.
---
// Definition. (PROBES 2026-09-14a-ptrcoerce: pcunion = crosskindx ∪ aorecvsk, for ONE runtime column.)
