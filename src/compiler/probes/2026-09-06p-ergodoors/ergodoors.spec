name: ergoref
file: src/compiler/sema_stmt.cpp
---
        if (explicit_ref) {
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            bind_ref_modes[k] = is_mut ? 2u : 1u;
---
        if (explicit_ref) {
            if (default_ref && logos::probe::on("ergoref"))
                error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", bindings[k]));
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            bind_ref_modes[k] = is_mut ? 2u : 1u;
===
name: ergoreftup
file: src/compiler/sema_stmt.cpp
---
            if (!flag(la::IS_REF) || !en.has_key(la::NAME)) return false;
            auto nm = std::string(str_of(en.get(la::NAME.code)));
            if (nm.empty() || nm == "_") return false;
            bool im = flag(la::IS_MUT);
---
            if (!flag(la::IS_REF) || !en.has_key(la::NAME)) return false;
            auto nm = std::string(str_of(en.get(la::NAME.code)));
            if (nm.empty() || nm == "_") return false;
            if (dbm_ref && logos::probe::on("ergoreftup"))
                error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", nm));
            bool im = flag(la::IS_MUT);
===
name: ergorefst
file: src/compiler/sema_stmt.cpp
---
                        if (fld_is_ref && !fnode.has_key(la::VALUE) &&
                            fname != "_") {
                            TypeRef bt = make_ref(fld_is_mut,
---
                        if (fld_is_ref && !fnode.has_key(la::VALUE) &&
                            fname != "_") {
                            if (dbm_ref && logos::probe::on("ergorefst"))
                                error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", fname));
                            TypeRef bt = make_ref(fld_is_mut,
===
name: ergorefsl
file: src/compiler/sema_stmt.cpp
---
                        lir::Pattern sub;   // default binding mode, SLICE door
                        if (!mint_dbm_ref(dbm_named_bind(enode), elem_type, sub))
                            sub = build_pattern(enode, elem_type);
---
                        if (dbm_ref && logos::probe::on("ergorefsl")) {
                            auto _en = enode;   // the grammar wraps an element in a single-alt PAT_OR
                            if (code_of(_en) == la::PAT_OR && _en.has_key(la::ITEMS)) {
                                auto _al = arr_of(_en.get(la::ITEMS.code));
                                if (_al.size() == 1) _en = map_of(_al.get(0));
                            }
                            if (_en.has_key(la::IS_REF) && _en.get(la::IS_REF.code).is_value() &&
                                _en.get(la::IS_REF.code).as_value<uint8_t>() != 0 && _en.has_key(la::NAME))
                                error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", str_of(_en.get(la::NAME.code))));
                        }
                        lir::Pattern sub;   // default binding mode, SLICE door
                        if (!mint_dbm_ref(dbm_named_bind(enode), elem_type, sub))
                            sub = build_pattern(enode, elem_type);
===
name: ergonest
file: src/compiler/sema_stmt.cpp
---
                        } else if (isc == la::PAT_VARIANT_DATA.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, elem_ty));
                            single = true;
---
                        } else if (isc == la::PAT_VARIANT_DATA.code) {
                            // The default binding mode is a fact of the WALK, not of the
                            // element's type: the tuple door knows it and does not carry it.
                            // ergonest    = CRUDE, carry it always (full RFC 2005 nesting).
                            // ergonestchk = NARROW, carry it only where the sub-pattern
                            //               WRITES a `mut` modifier (the 2024 check alone).
                            TypeRef _nt = elem_ty;
                            if (dbm_ref && elem_ty) {
                                bool _want = logos::probe::on("ergonest");
                                if (!_want && logos::probe::on("ergonestchk") && inner.has_key(la::ARGS)) {
                                    auto _av = inner.get(la::ARGS.code);
                                    if (!_av.is_null() && _av.is_pointer()) {
                                        auto _args = arr_of(_av);
                                        for (uint64_t _q = 0; _q < _args.size() && !_want; ++_q) {
                                            auto _pn = map_of(_args.get(_q));
                                            if (code_of(_pn) == la::PAT_OR && _pn.has_key(la::ITEMS)) {
                                                auto _pa = arr_of(_pn.get(la::ITEMS.code));
                                                if (_pa.size() == 1) _pn = map_of(_pa.get(0));
                                            }
                                            auto _fl = [&](const la::Key& kk) {
                                                return _pn.has_key(kk) && _pn.get(kk.code).is_value() &&
                                                       _pn.get(kk.code).as_value<uint8_t>() != 0;
                                            };
                                            if (_fl(la::IS_MUT) && !_fl(la::IS_REF)) _want = true;
                                        }
                                    }
                                }
                                if (_want) _nt = make_ref(dbm_mut, elem_ty);
                            }
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, _nt));
                            single = true;
===
name: ergonestchk
file: src/compiler/sema_stmt.cpp
---
    const bool dbm_mut = dbm_ref &&
        TypeRef(scrut_orig).kind() == LogosType::Kind::MutRef;
---
    const bool dbm_mut = dbm_ref &&
        TypeRef(scrut_orig).kind() == LogosType::Kind::MutRef;  // probes ergonest / ergonestchk read this at the TUPLE door
