name: pbdbm
file: src/compiler/borrow_check.cpp
---
                if (mode == 0 || mode > 2 ||
                    b.empty() || b == "_" || place.empty()) return;
                if (place.size() < base_place.size()) return;
                if (carried && std::find(carried->begin(), carried->end(),
                                         std::string(b)) == carried->end())
                    return;
                BorrowPlace bp = base;
                if (place.size() > base_place.size())
                    bp.path = base.path.empty()
                                ? place.substr(base_place.size() + 1)
                                : base.path + place.substr(base_place.size());
                record_borrow(bp, /*is_mut=*/mode == 2, ln,
---
                // PROBES 2026-09-14e-thruref D1 (pbdbm / pbdbmasg / asgrefany): a DEFAULT-binding-mode by-ref binding
                // (mode 3 shared, 4 mut) records its loan exactly as a written `ref` / `ref mut` does.
                const bool dbm_ = (mode == 3 || mode == 4) &&
                                  (logos::probe::on("pbdbm") || logos::probe::on("pbdbmasg") ||
                                   logos::probe::on("asgrefany"));
                if (mode == 0 || (mode > 2 && !dbm_) ||
                    b.empty() || b == "_" || place.empty()) return;
                if (place.size() < base_place.size()) return;
                if (carried && std::find(carried->begin(), carried->end(),
                                         std::string(b)) == carried->end())
                    return;
                BorrowPlace bp = base;
                if (place.size() > base_place.size())
                    bp.path = base.path.empty()
                                ? place.substr(base_place.size() + 1)
                                : base.path + place.substr(base_place.size());
                record_borrow(bp, /*is_mut=*/mode == 2 || mode == 4, ln,
===
name: asgref
file: src/compiler/borrow_check.cpp
---
                    if (!said_shared)
                        field_borrow_conflicts(*it, name, /*path=*/"",
                                               /*need_exclusive=*/true, ln,
                                               "assign to");
---
                    // PROBES 2026-09-14e-thruref D2 (asgref / pbdbmasg): a REFERENCE-typed local has no fields of its own,
                    // so a dotted loan under it names the POINTEE, and overwriting the local does not touch that storage;
                    // the re-own below erases the map. asgrefany: the same skip for ANY local (the control twin).
                    const bool thru_asg_ =
                        ((logos::probe::on("asgref") || logos::probe::on("pbdbmasg")) && val &&
                         is_ref_kind(val.type(pool))) ||
                        logos::probe::on("asgrefany");
                    if (!said_shared && !thru_asg_)
                        field_borrow_conflicts(*it, name, /*path=*/"",
                                               /*need_exclusive=*/true, ln,
                                               "assign to");
===
name: pbdbmasg
file: src/compiler/borrow_check.cpp
---
// (mode 3 shared, 4 mut) records its loan exactly as a written `ref` / `ref mut` does.
---
// (mode 3 shared, 4 mut) records its loan exactly as a written `ref` / `ref mut` does. [pbdbmasg = D1 + D2, the whole]
===
name: asgrefany
file: src/compiler/borrow_check.cpp
---
// the re-own below erases the map. asgrefany: the same skip for ANY local (the control twin).
---
// the re-own below erases the map. asgrefany: the same skip for ANY local (the control twin). [asgrefany arms D1 too]
===
name: cmpinv
file: src/compiler/sema_expr.cpp
---
        bool ok = ptr_null_cmp || types_compatible(lt, rt) || types_compatible(rt, lt);
---
        bool ok = ptr_null_cmp || types_compatible(lt, rt) || types_compatible(rt, lt);
        // PROBES 2026-09-14e-thruref R2 (cmpinv / cmpinvtop): `==` / `!=` on pointer-like operands needs a COMMON type.
        // Regions under a covariant or contravariant position always meet; under an INVARIANT one (the pointee of
        // `*mut` / `&mut`) they must be equal. cmpinv walks `*const` / `&` pointees and fn params+ret to find the
        // invariant positions; cmpinvtop asks only the top-level `*mut` / `&mut` pointee (the control twin).
        if (ok && !ptr_null_cmp && (op == "==" || op == "!=") &&
            (logos::probe::on("cmpinv") || logos::probe::on("cmpinvtop"))) {
            using PK_ = LogosType::Kind;
            const bool top_only_ = logos::probe::on("cmpinvtop");
            std::function<bool(TypeRef, TypeRef, int)> meet_ = [&](TypeRef a, TypeRef b, int d) -> bool {
                if (!a || !b || d > 16) return true;
                const bool af_ = LogosType::is_fn_value_kind(a.kind()), bf_ = LogosType::is_fn_value_kind(b.kind());
                if (a.kind() != b.kind() && !(af_ && bf_)) return true;
                if ((a.kind() == PK_::Ptr && a.mut_ptr() && b.mut_ptr()) || a.kind() == PK_::MutRef)
                    return variance_ok(a, b, true) || variance_ok(b, a, true);
                if (top_only_ && d > 0) return true;
                if (a.kind() == PK_::Ptr || a.kind() == PK_::Ref) return meet_(a.pointee(), b.pointee(), d + 1);
                if (af_ && bf_) {
                    auto ap_ = a.closure_params();
                    auto bp_ = b.closure_params();
                    if (ap_.size() != bp_.size()) return true;
                    for (size_t i = 0; i < ap_.size(); ++i)
                        if (!meet_(ap_[i], bp_[i], d + 1)) return false;
                    return meet_(a.closure_ret(), b.closure_ret(), d + 1);
                }
                return true;
            };
            const bool ptr_like_ = TypeRef(lt).kind() == PK_::Ptr || LogosType::is_fn_value_kind(TypeRef(lt).kind());
            if (ptr_like_ && !meet_(lt, rt, 0))
                error(std::format("operator '{}': lifetime may not live long enough — {} and {} have no common type, "
                                  "because a lifetime under an invariant position differs",
                                  op, type_str(lt, true), type_str(rt, true)));
        }
===
name: cmpinvtop
file: src/compiler/sema_expr.cpp
---
// invariant positions; cmpinvtop asks only the top-level `*mut` / `&mut` pointee (the control twin).
---
// invariant positions; cmpinvtop asks only the top-level `*mut` / `&mut` pointee (the control twin). [cmpinvtop]
