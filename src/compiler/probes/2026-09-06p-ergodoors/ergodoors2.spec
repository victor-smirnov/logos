name: ergorefw
file: src/compiler/sema_stmt.cpp
---
        if (explicit_ref) {
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            bind_ref_modes[k] = is_mut ? 2u : 1u;
---
        if (explicit_ref) {
            // ergoref's SECOND name (rule 9): `binding_is_ref` is true both for a
            // WRITTEN `ref` and for the compiler's own refutable-sub synthesis
            // (the `synth_wants_ref` push). `binding_from_wild` is the fact that
            // separates them, and the landed `mut` half already asks it.
            if (default_ref && logos::probe::on("ergorefw") &&
                k < binding_from_wild.size() && binding_from_wild[k])
                error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", bindings[k]));
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            bind_ref_modes[k] = is_mut ? 2u : 1u;
===
name: ergonestst
file: src/compiler/sema_stmt.cpp
---
                            auto sub = build_pattern(sub_node, ftype);
---
                            TypeRef _sft = ftype;
                            if (dbm_ref && ftype && logos::probe::on("ergonestst")) {
                                auto _sn = sub_node;   // the grammar may wrap in a single-alt PAT_OR
                                if (code_of(_sn) == la::PAT_OR && _sn.has_key(la::ITEMS)) {
                                    auto _a = arr_of(_sn.get(la::ITEMS.code));
                                    if (_a.size() == 1) _sn = map_of(_a.get(0));
                                }
                                if (code_of(_sn) == la::PAT_VARIANT_DATA) _sft = make_ref(dbm_mut, ftype);
                            }
                            auto sub = build_pattern(sub_node, _sft);
===
name: ergonestsl
file: src/compiler/sema_stmt.cpp
---
                        lir::Pattern sub;   // default binding mode, SLICE door
                        if (!mint_dbm_ref(dbm_named_bind(enode), elem_type, sub))
                            sub = build_pattern(enode, elem_type);
---
                        TypeRef _elt = elem_type;
                        if (dbm_ref && elem_type && logos::probe::on("ergonestsl")) {
                            auto _en = enode;   // the grammar may wrap in a single-alt PAT_OR
                            if (code_of(_en) == la::PAT_OR && _en.has_key(la::ITEMS)) {
                                auto _a = arr_of(_en.get(la::ITEMS.code));
                                if (_a.size() == 1) _en = map_of(_a.get(0));
                            }
                            if (code_of(_en) == la::PAT_VARIANT_DATA) _elt = make_ref(dbm_mut, elem_type);
                        }
                        lir::Pattern sub;   // default binding mode, SLICE door
                        if (!mint_dbm_ref(dbm_named_bind(enode), elem_type, sub))
                            sub = build_pattern(enode, _elt);
