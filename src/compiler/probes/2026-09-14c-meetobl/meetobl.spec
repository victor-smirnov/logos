name: meetobl
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
// PROBES 2026-09-14c-meetobl: meetobl / meetoblsl / meetoblcall / meetoblenumd / meetoblenums / meetoblsome / meetoblinv.
// A region binder offered TWO OR MORE candidate regions (struct literal meet, call meet, enum literal first-wins) is
// instantiated at a MEET TOKEN that remembers its candidates. The registry is empty unless a minting site is armed.
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
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
    bool permissive_empty = true)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
    // meetobl: a token on the SUB side outlives S iff EVERY candidate does (meetoblsome: iff ANY does — the control
    // twin, which asks nothing a kept candidate does not already discharge); on the SUP side, iff L outlives ANY.
    if (lt_is_meet(L) || lt_is_meet(S)) {
        logos::probe::census("meetobl.outl.arrive");
        auto itL = meet_members().find(L);
        if (itL != meet_members().end()) {
            const bool some = logos::probe::on("meetoblsome");
            bool acc = !some;
            for (auto& m : itL->second) {
                const bool r = outlives(m, shorter, adj, permissive_empty);
                if (some) { if (r) { acc = true; break; } }
                else if (!r) { acc = false; break; }
            }
            if (!acc) logos::probe::census("meetobl.outl.refuse.sub");
            return acc;
        }
        auto itS = meet_members().find(S);
        if (itS != meet_members().end()) {
            for (auto& m : itS->second)
                if (outlives(longer, m, adj, permissive_empty)) return true;
            logos::probe::census("meetobl.outl.refuse.sup");
            return false;
        }
    }
===
name: meetoblsl
file: src/compiler/sema_impl.hpp
---
                logos::probe::census("meet.structlit.applied");
                out[lp] = "";
                continue;
---
                logos::probe::census("meet.structlit.applied");
                // PROBES 2026-09-14c-meetobl: meetoblsl — the struct literal's meet keeps its candidates.
                if (logos::probe::on("meetobl") || logos::probe::on("meetoblsl") ||
                    logos::probe::on("meetoblsome") || logos::probe::on("meetoblinv")) {
                    logos::probe::census("meetobl.mint.structlit");
                    out[lp] = mint_meet_token(it->second);
                    continue;
                }
                out[lp] = "";
                continue;
===
name: meetoblcall
file: src/compiler/sema_impl.hpp
---
                ls[lp] = "";     // the meet: unnamed, exactly as the literal's
---
                // PROBES 2026-09-14c-meetobl: meetoblcall — the call's meet keeps its candidates.
                if (logos::probe::on("meetobl") || logos::probe::on("meetoblcall") ||
                    logos::probe::on("meetoblsome") || logos::probe::on("meetoblinv")) {
                    logos::probe::census("meetobl.mint.call");
                    ls[lp] = mint_meet_token(it->second);
                    continue;
                }
                ls[lp] = "";     // the meet: unnamed, exactly as the literal's
===
name: meetoblenumd
file: src/compiler/sema_expr.cpp
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
        // PROBES 2026-09-14c-meetobl: meetoblenumd — a COVARIANT binder offered two regions keeps both (today: the first).
        if (logos::probe::on("meetobl") || logos::probe::on("meetoblenumd") ||
            logos::probe::on("meetoblsome") || logos::probe::on("meetoblinv")) {
            for (size_t bi = 0; bi < einfo.lifetime_params.size(); ++bi) {
                auto cit = lt_cands.find(einfo.lifetime_params[bi]);
                if (cit == lt_cands.end()) continue;
                std::unordered_set<std::string> dd(cit->second.begin(), cit->second.end());
                if (dd.size() < 2 || !binder_is_covariant_(eit->first, bi)) continue;
                logos::probe::census("meetobl.mint.enumd");
                lt_subst[einfo.lifetime_params[bi]] = mint_meet_token(cit->second);
            }
        }
    }

    // Build the enum type (may be generic, e.g. Option<i32>)
===
name: meetoblenums
file: src/compiler/sema_expr.cpp
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
---
        census_meet_("enumlit", einfo.lifetime_params, lt_cands, eit->first);
        // PROBES 2026-09-14c-meetobl: meetoblenums — the static-call twin of meetoblenumd.
        if (logos::probe::on("meetobl") || logos::probe::on("meetoblenums") ||
            logos::probe::on("meetoblsome") || logos::probe::on("meetoblinv")) {
            for (size_t bi = 0; bi < einfo.lifetime_params.size(); ++bi) {
                auto cit = lt_cands.find(einfo.lifetime_params[bi]);
                if (cit == lt_cands.end()) continue;
                std::unordered_set<std::string> dd(cit->second.begin(), cit->second.end());
                if (dd.size() < 2 || !binder_is_covariant_(eit->first, bi)) continue;
                logos::probe::census("meetobl.mint.enums");
                lt_subst[einfo.lifetime_params[bi]] = mint_meet_token(cit->second);
            }
        }
    }

    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
===
name: meetoblinv
file: include/logos/compiler/subtype.hpp
---
    auto lt_eq = [&](std::string_view x, std::string_view y) {
---
    auto lt_eq = [&](std::string_view x, std::string_view y) {
        // PROBES 2026-09-14c-meetobl: a meet token is ASKED only at an outlives site; at an equality it reads as the ""
        // it replaced — except under meetoblinv, the inner predicate's second name, which asks it at equalities too.
        if (lt_is_meet(x) && !logos::probe::on("meetoblinv")) { logos::probe::census("meetobl.eq.inert"); x = {}; }
        if (lt_is_meet(y) && !logos::probe::on("meetoblinv")) { logos::probe::census("meetobl.eq.inert"); y = {}; }
===
name: meetoblsome
file: include/logos/compiler/subtype.hpp
---
        case Variance::Inv: {
            (void)logos::probe::on("ltinvarm_site");
---
        case Variance::Inv: {
            (void)logos::probe::on("ltinvarm_site");
            // PROBES 2026-09-14c-meetobl: the Inv twin of lt_eq's inert read (meetoblinv asks the token here too).
            if (lt_is_meet(sub_lt) && !logos::probe::on("meetoblinv")) { logos::probe::census("meetobl.inv.inert"); sub_lt = {}; }
            if (lt_is_meet(sup_lt) && !logos::probe::on("meetoblinv")) { logos::probe::census("meetobl.inv.inert"); sup_lt = {}; }
