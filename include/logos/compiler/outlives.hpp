#pragma once
// B65: outlives reasoning over named lifetime regions.
//
// `outlives(long, short, graph)` answers: does region `long` live at least
// as long as region `short`? Returns true for:
//   - reflexive:  long == short
//   - 'static:    'static outlives every region
//   - elision:    empty short is treated as the most permissive — every region
//                 outlives the unconstrained one
//   - direct:     (long, short) appears in the explicit outlives graph
//   - transitive: there exists c such that long: c and c: short (BFS)
//
// The graph is a sequence of (longer, shorter) pairs as parsed from
// `where 'long: 'short` / `'long: 'short + 'mid` / etc.

#include <logos/compiler/probe.hpp>

#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace logos::compiler {

// Both `'a` and the bare `a` forms are accepted; LIFETIME terminals in the
// grammar include the leading apostrophe, while elsewhere we sometimes have
// the bare name. Normalise to with-apostrophe for graph keys.
inline std::string outlives_norm(std::string_view s) {
    if (s.empty()) return {};
    if (s.front() == '\'') return std::string(s);
    return std::string("'") + std::string(s);
}

// THE BINDERS OF THE SCOPE BEING CHECKED. `outlives()` is handed two STRINGS
// and cannot say which BINDER each one denotes; two names that collide by
// spelling are not two names of one scope. This set is that missing fact — the
// lifetime parameters declared by the generic scope whose body is under check
// (the fn's own, plus its `impl` block's) — refilled at each fn in
// sema_decl.cpp beside `current_outlives_`. Empty outside a fn body, which is
// exactly when the rule below must not fire.
//
// ⚠ IT IS NOT SOUND BY CONSTRUCTION AND IT DOES NOT CLAIM TO BE: it works by
// name collision, so a callee's `'a` that happens to be spelled like the
// caller's is caught and the same program with the callee's binder renamed is
// missed. The measured pair is in src/compiler/PROBES.md (u7 / u8) and is
// pinned as a fixture pair. What removes the caveat is SUBSTITUTION, which is
// its own round.
inline std::unordered_set<std::string>& current_lt_binders() {
    static std::unordered_set<std::string> s;
    return s;
}

// The last pair of DISTINCT minted regions a comparator refused to equate, and
// the parameter each was minted for. Written at the refusal in subtype.hpp,
// read by check_variance so the message can name the two elided slots instead
// of printing one spelling twice. See src/compiler/PROBES.md 2026-09-06a.
inline std::pair<std::string, std::string>& last_rigid_mismatch() {
    static std::pair<std::string, std::string> p;
    return p;
}
inline std::unordered_map<std::string, std::string>& minted_lt_origin() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}

// The minted regions belonging to a CLOSURE's OWN parameters, as opposed to an
// enclosing fn signature's. The distinction is not cosmetic and it is not a
// label: measured 2026-09-03b, a rule keyed on "minted" alone un-refuses three
// pinned fn-side `fail` fixtures (nll-anon-to-static and two siblings), while
// the same rule keyed on THIS set costs nothing. Cleared with minted_lt_origin.
inline std::unordered_set<std::string>& closure_minted_lts() {
    static std::unordered_set<std::string> s;
    return s;
}

// A MINTED REGION — the name an elided slot did not have. `""` means both
// "elided here" and "the same region as that other elided slot"; a minted name
// means exactly the first. The prefix is unspellable in the grammar, so a
// minted name can never collide with a user's binder, and every consumer that
// treats an elided slot as ABSENT (diagnostics, the undeclared-lifetime walk,
// borrow_check's elision contract) asks `lt_is_minted` and keeps its old
// answer. Only the comparators (subtype/outlives) see the name.
inline bool lt_is_minted(std::string_view lt) {
    return lt.size() > 1 && lt[0] == '\'' && lt[1] == '%';
}

// A MEET TOKEN — a region binder offered TWO OR MORE distinct regions is
// instantiated at the region every one of them outlives, and the token keeps
// them. `'%^N` is lt_is_minted, so every consumer that reads a minted slot as
// elided keeps its answer; the comparator (`outlives`) asks the members.
// The registry is append-only and process-wide: the counter never reissues a
// name. PROBES.md 2026-09-14d-meetoblland.
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
// The last member a meet failed on: {token, member, the other region}. Read by
// check_variance so the refusal names the region, not a hidden token.
inline std::tuple<std::string, std::string, std::string>& last_meet_refusal() {
    static std::tuple<std::string, std::string, std::string> r;
    return r;
}

// A FN VALUE'S OWN BINDER, carried by its type: a fn-pointer type's `for<'r>`, or a fn item's lifetime parameter in a
// fn pointer minted from the item. `'%hN` is lt_is_minted; the registry keeps the written name. PROBES.md 2026-09-14k.
inline std::unordered_map<std::string, std::string>& fnptr_binder_names() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}
inline bool lt_is_fnptr_binder(std::string_view lt) {
    return lt.size() > 2 && lt[0] == '\'' && lt[1] == '%' && lt[2] == 'h';
}
inline std::string mint_fnptr_binder(std::string_view written) {
    static unsigned n = 0;
    std::string t = "'%h" + std::to_string(++n);
    fnptr_binder_names()[t] = outlives_norm(written);
    return t;
}
// The spelling a diagnostic shows: a binder token prints as the name the user wrote.
inline std::string lt_written(std::string_view lt) {
    if (lt_is_fnptr_binder(lt)) {
        auto it = fnptr_binder_names().find(std::string(lt));
        if (it != fnptr_binder_names().end()) return it->second;
    }
    if (lt == "static") return "'static";   // the promoted/literal region's internal spelling
    return std::string(lt);
}

// An impl header's `'_`, named per impl: sema_impl.hpp::name_impl_anon_lts_.
inline bool lt_is_impl_anon(std::string_view lt) {
    return lt.starts_with("'__anon");
}

inline bool outlives_is_static(std::string_view lt) {
    return lt == "'static" || lt == "static";
}

// Where an EMPTY sub region against a 'static sup slot is NOT a body borrow's
// missing name, the 'static-slot arm in `lt_eq` yields (PROBES.md 2026-09-02s):
//   · the two RETURN sites — borrow_check's 'static-return rule owns them and
//     names the variable;
//   · a fn-pointer type's OUTPUT position — its elided region is a BINDER
//     (`fn(&T) -> &T` is `for<'a>`), instantiable at 'static.
inline bool& lt_static_yield() { static bool b = false; return b; }

// Build a forward adjacency map (longer → set of shorters it outlives directly).
// Caller passes parsed pairs; this just indexes them. Symmetry: an entry
// `(a, b)` means a: b (a lives at least as long as b).
inline std::unordered_map<std::string, std::unordered_set<std::string>>
outlives_adj(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::unordered_map<std::string, std::unordered_set<std::string>> adj;
    for (auto& [longer, shorter] : pairs) {
        if (longer.empty() || shorter.empty()) continue;
        adj[outlives_norm(longer)].insert(outlives_norm(shorter));
    }
    return adj;
}

// Decide whether `longer` outlives `shorter` given the graph.
// Reflexive, static-is-top, with BFS for transitive paths. When
// `permissive_empty` is true, an empty (elided) lifetime on the sub side
// is treated as compatible with any named region — appropriate at
// variance/subtype coercion sites where call-site region inference will
// pick a unification. The borrow-check return path passes `false` to
// keep elided sources from silently satisfying a named return.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
    bool permissive_empty = true)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
    // A MEET TOKEN (see lt_is_meet): on the SUB side it outlives S iff EVERY
    // member does; on the SUP side L outlives it iff L outlives ANY member.
    // Asked here, ahead of the elision arms, so every comparator reaches it.
    if (lt_is_meet(L) || lt_is_meet(S)) {
        auto itL = meet_members().find(L);
        if (itL != meet_members().end()) {
            for (auto& m : itL->second)
                if (!outlives(m, shorter, adj, permissive_empty)) {
                    last_meet_refusal() = {L, m, S};
                    return false;
                }
            return true;
        }
        auto itS = meet_members().find(S);
        if (itS != meet_members().end()) {
            for (auto& m : itS->second)
                if (outlives(longer, m, adj, permissive_empty)) return true;
            last_meet_refusal() = {S, std::string{}, L};
            return false;
        }
    }
    // PROBES ltelidesup / ltelidesub / ltelideboth — AN ELIDED REGION HAS NO
    // NAME, so `outlives()` treats it as compatible with everything. That, not
    // region inference, is what admits the lifereg.A elision rows: naming the
    // slots by hand makes three of them REFUSE today, unarmed.
    if (S.empty()) {
        if (logos::probe::on("ltelidesup") ||
            logos::probe::on("ltelideboth") ||
            logos::probe::on("ltmintfresh")) return false;
        return true;                        // unconstrained short side
    }
    // LANDED 2026-09-02s: an EMPTY region is not 'static — a body borrow has
    // no name, and the one region whose meaning is fixed is not a wildcard.
    if (L.empty() && outlives_is_static(S)) {
        logos::probe::census("stland.outl");
        return false;
    }
    if (permissive_empty && L.empty()) {
        if (logos::probe::on("ltelidesub") ||
            logos::probe::on("ltelideboth") ||
            logos::probe::on("ltmintfresh")) return false;
        return true;                        // see comment above
    }
    if (outlives_is_static(L)) return true; // 'static outlives all
    if (L == S) return true;                // reflexive

    std::unordered_set<std::string> seen;
    std::queue<std::string> q;
    q.push(L);
    seen.insert(L);
    while (!q.empty()) {
        auto cur = std::move(q.front());
        q.pop();
        auto it = adj.find(cur);
        if (it == adj.end()) continue;
        for (auto& nb : it->second) {
            if (nb == S) return true;
            if (seen.insert(nb).second) q.push(nb);
        }
    }
    // Permissive default for two named generic lifetimes (neither static)
    // when NEITHER appears anywhere in the explicit outlives graph: assume
    // the caller's region inference will pick a unification. Gated by
    // `permissive_empty` — strict-mode callers (e.g. borrow_check return
    // path) opt out and get conservative-reject.
    if (outlives_is_static(S)) return false;
    if (!permissive_empty) return false;
    auto mentioned = [&](const std::string& lt) {
        if (adj.count(lt)) return true;
        for (auto& [k, v] : adj) {
            (void)k;
            if (v.count(lt)) return true;
        }
        return false;
    };
    // PROBE lifereg_unmentioned: the single permissive default UPSTREAM of
    // lifereg_callargstrict and lifereg_structlitstrict. MENTIONED-NESS, not
    // the constraint, is what flips the answer — adding `where 'a: 'b` turns
    // an admission into a refusal. Rust's rule is the opposite.
    // Numbers live in src/compiler/PROBES.md (re-measured 2026-08-31 against
    // the 310-row ledger; the 2026-08-27 pair that used to sit here was a
    // 423-row measurement and was never recorded outside this comment).
    if (logos::probe::on("lifereg_unmentioned")) return false;
    // LANDED 2026-08-31. MENTIONED-NESS is not a relation: two named generic
    // regions that appear in no `where` clause are UNRELATED, and Rust refuses
    // the coercion between them. The permissive default below admitted them.
    //
    // It is narrowed to the case where BOTH names are binders of the scope
    // actually being checked — see `current_lt_binders()`. The wider rule (the
    // `lifereg_unmentioned` probe directly above) closes two more rows and
    // refuses FIVE legal programs to do it, every one of them comparing a name
    // from ONE binder against a name from ANOTHER: a callee's or a struct's own
    // lifetime parameter that reached here UNSUBSTITUTED. Numbers, the two rows
    // given up and the counter-examples: src/compiler/PROBES.md 2026-08-31h.
    // PROBE ltbindersoff — the CONTROL REVERT of this predicate alone.
    if (current_lt_binders().count(L) && current_lt_binders().count(S)) {
        if (!logos::probe::on("ltbindersoff")) return false;
    }
    if (mentioned(L) || mentioned(S)) return false;
    return true;
}

// Convenience: build + query in one shot.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::vector<std::pair<std::string, std::string>>& pairs,
    bool permissive_empty = true)
{
    return outlives(longer, shorter, outlives_adj(pairs), permissive_empty);
}

} // namespace logos::compiler
