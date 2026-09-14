name: mobl
file: include/logos/compiler/outlives.hpp
---
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
    bool permissive_empty = true)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
---
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
    bool permissive_empty = true)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
    // PROBES 2026-09-14c-meetobl batch 2 (mobl and every mobl* name): a token on the SUB side outlives S iff EVERY
    // candidate does (moblsome: iff ANY does — the control twin); on the SUP side, iff L outlives ANY candidate.
    // Asked at EVERY comparator, equality included: batch 1 measured the equality-inert read as a broken hop
    // (subtype()'s head equality answered for "" before the Co arm was reached).
    if (lt_is_meet(L) || lt_is_meet(S)) {
        logos::probe::census("mobl.outl.arrive");
        auto itL = meet_members().find(L);
        if (itL != meet_members().end()) {
            const bool some = logos::probe::on("moblsome");
            bool acc = !some;
            for (auto& m : itL->second) {
                const bool r = outlives(m, shorter, adj, permissive_empty);
                if (some) { if (r) { acc = true; break; } }
                else if (!r) { acc = false; break; }
            }
            if (!acc) logos::probe::census("mobl.outl.refuse.sub");
            return acc;
        }
        auto itS = meet_members().find(S);
        if (itS != meet_members().end()) {
            for (auto& m : itS->second)
                if (outlives(longer, m, adj, permissive_empty)) return true;
            logos::probe::census("mobl.outl.refuse.sup");
            return false;
        }
    }
===
name: moblng
file: src/compiler/sema_expr.cpp
---
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
---
        // PROBES 2026-09-14c-meetobl batch 2: moblng / mobl / moblsome / moblany — this non-generic literal's own walk is
        // first-wins; a binder offered two regions takes the meet token structlit_lt_subst_ minted for it instead.
        if (logos::probe::on("mobl") || logos::probe::on("moblng") || logos::probe::on("moblsome") ||
            logos::probe::on("moblany")) {
            auto bl_ng = structlit_lt_subst_(sinfo.lifetime_params, sinfo.fields, fields,
                                             sinfo.package.empty() ? std::string(sname)
                                                                   : sinfo.package + "." + std::string(sname));
            for (auto& [k, v] : bl_ng)
                if (lt_is_meet(v)) { logos::probe::census("mobl.ng.token"); flt[k] = v; }
        }
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
===
name: moblenum
file: src/compiler/sema_expr.cpp
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
        // PROBES 2026-09-14c-meetobl batch 2: moblenum — a COVARIANT binder offered two regions keeps both (today: the
        // first). moblany drops the covariance guard (rule 9's second name, the abuse direction).
        if (logos::probe::on("mobl") || logos::probe::on("moblenum") ||
            logos::probe::on("moblsome") || logos::probe::on("moblany")) {
            for (size_t bi = 0; bi < einfo.lifetime_params.size(); ++bi) {
                auto cit = lt_cands.find(einfo.lifetime_params[bi]);
                if (cit == lt_cands.end()) continue;
                std::unordered_set<std::string> dd(cit->second.begin(), cit->second.end());
                if (dd.size() < 2) continue;
                if (!binder_is_covariant_(eit->first, bi) && !logos::probe::on("moblany")) continue;
                logos::probe::census("mobl.mint.enumd");
                lt_subst[einfo.lifetime_params[bi]] = mint_meet_token(cit->second);
            }
        }
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
===
name: moblcall
file: src/compiler/sema_impl.hpp
---
                ls[lp] = "";     // the meet: unnamed, exactly as the literal's
---
                // PROBES 2026-09-14c-meetobl batch 2: moblcall — the call's meet keeps its candidates.
                if (logos::probe::on("mobl") || logos::probe::on("moblcall") ||
                    logos::probe::on("moblsome") || logos::probe::on("moblany")) {
                    logos::probe::census("mobl.mint.call");
                    ls[lp] = mint_meet_token(it->second);
                    continue;
                }
                ls[lp] = "";     // the meet: unnamed, exactly as the literal's
===
name: moblgen
file: src/compiler/sema_impl.hpp
---
                logos::probe::census("meet.structlit.applied");
                out[lp] = "";
                continue;
---
                logos::probe::census("meet.structlit.applied");
                // PROBES 2026-09-14c-meetobl batch 2: moblgen — the struct literal's meet keeps its candidates (the
                // GENERIC literal's type and every field check read it; moblng also hands it to the non-generic type).
                if (logos::probe::on("mobl") || logos::probe::on("moblgen") || logos::probe::on("moblng") ||
                    logos::probe::on("moblsome") || logos::probe::on("moblany")) {
                    logos::probe::census("mobl.mint.structlit");
                    out[lp] = mint_meet_token(it->second);
                    continue;
                }
                out[lp] = "";
                continue;
===
name: moblany
file: src/compiler/sema_expr.cpp
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
        // PROBES 2026-09-14c-meetobl batch 2: the static-call twin of moblenum (same names, same guard).
        if (logos::probe::on("mobl") || logos::probe::on("moblenum") ||
            logos::probe::on("moblsome") || logos::probe::on("moblany")) {
            for (size_t bi = 0; bi < einfo.lifetime_params.size(); ++bi) {
                auto cit = lt_cands.find(einfo.lifetime_params[bi]);
                if (cit == lt_cands.end()) continue;
                std::unordered_set<std::string> dd(cit->second.begin(), cit->second.end());
                if (dd.size() < 2) continue;
                if (!binder_is_covariant_(eit->first, bi) && !logos::probe::on("moblany")) continue;
                logos::probe::census("mobl.mint.enums");
                lt_subst[einfo.lifetime_params[bi]] = mint_meet_token(cit->second);
            }
        }
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
===
name: moblsome
file: include/logos/compiler/outlives.hpp
---
inline bool lt_is_minted(std::string_view lt) {
    return lt.size() > 1 && lt[0] == '\'' && lt[1] == '%';
}
---
inline bool lt_is_minted(std::string_view lt) {
    return lt.size() > 1 && lt[0] == '\'' && lt[1] == '%';
}

// PROBES 2026-09-14c-meetobl batch 2: a region binder offered TWO OR MORE candidate regions is instantiated at a MEET
// TOKEN that remembers them. `'%^` keeps lt_is_minted true, so every consumer that reads a minted slot as elided keeps
// its answer; only the comparator (outlives) asks the candidates. Empty unless a minting site is armed.
inline std::unordered_map<std::string, std::vector<std::string>>& meet_members() {
    static std::unordered_map<std::string, std::vector<std::string>> m;
    return m;
}
inline bool lt_is_meet(std::string_view lt) {
    return lt.size() > 2 && lt[0] == '\'' && lt[1] == '%' && lt[2] == '^';
}
inline std::string mint_meet_token(const std::vector<std::string>& cands) {
    static unsigned n = 0;
    std::vector<std::string> ms;
    for (auto& c : cands) {
        if (c.empty()) continue;
        bool dup = false;
        for (auto& x : ms) if (x == c) { dup = true; break; }
        if (!dup) ms.push_back(c);
    }
    std::string t = "'%^" + std::to_string(++n);
    meet_members()[t] = std::move(ms);
    return t;
}
