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
        if ((sub.kind() == K::Ref || sub.kind() == K::MutRef) && sup.kind() == K::Slice && sub.pointee() &&
            sub.pointee().kind() == K::Struct && sub.pointee().struct_name() == "Vec" &&
            !sub.pointee().type_args().empty() && sup.elem()) {
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
// extensions, `pcunion` = crosskindx ∪ idxstoremut — names armed at the sites below.)
===
name: idxstoremut
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
                // PROBES 2026-09-14a-ptrcoerce: idxstoremut / amutrecv / pcunion — a receiver sema built as an explicit
                // `&mut v` (AddrOf of MutRef type: try_index_mut_assign's `v[i] = x` on a Vec) is a mutable use of `v`
                // whether or not method_self_kind resolves the callee; visit()'s AddrOf arm asks no conflict at all.
                if (sk == 0 && recv.kind() == Code::AddrOf && is_mut_ref(recv.type(pool))) {
                    bool im_ = v.method() == "index_mut";
                    logos::probe::census(im_ ? "amutrecv.index_mut" : "amutrecv.other");
                    if ((im_ && (logos::probe::on("idxstoremut") || logos::probe::on("pcunion"))) ||
                        logos::probe::on("amutrecv")) {
                        BorrowPlace ap_{};
                        ap_.root = std::string(EAddrOfView{recv}.var_name());
                        ap_.root_type = TypeRef(recv.type(pool)).pointee();
                        check_recv_conflict(ap_, /*is_mut=*/true, line);
                    }
                }
            }
===
name: amutrecv
file: src/compiler/borrow_check.cpp
---
    // Receiver self-kind of a method call: 0 = none/by-value, 1 = `&self`, 2 =
---
    // (PROBES 2026-09-14a-ptrcoerce: `amutrecv` arms the MethodCall-arm AddrOf receiver check for every method.)
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
// Definition. (PROBES 2026-09-14a-ptrcoerce: pcunion = crosskindx ∪ idxstoremut, for ONE runtime column.)
