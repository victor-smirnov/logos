// Logos project — https://github.com/victor-smirnov/logos
//
// mangled_name.hpp — the single home for every mangled-name SPLIT that
// survives the separator-class fix.
//
// R1 (composition). A name is composed from parts. The parts must be CARRIED
// to every consumer. A consumer that needs a part and does not have it must
// either (a) get it threaded, (b) recompose the whole name from parts it has
// and compare for equality, or (c) match against a REGISTRY of known parts
// (longest match). It may NEVER cut a string at a separator that is legal
// inside the parts — `__` is legal inside a Logos identifier, so
// `name.find("__")` is a guess, not a boundary.
//
// Every function here takes the carried parts and returns std::optional.
// There is deliberately NO guessing overload: `nullopt` is a FACT the caller
// must report, never a reason to fall back to a raw split.

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace logos::compiler::mname {

// The overload-disambiguation boundary: the first `__f__` or `__g__` at or
// after `from`. `from` must be an offset the caller established by ANCHORING
// on carried parts — never 0, never `find("__")`.
// Returns npos when neither marker occurs at/after `from`.
inline size_t sig_boundary(std::string_view name, size_t from) noexcept {
    if (from > name.size()) return std::string_view::npos;
    auto pf = name.find("__f__", from);
    auto pg = name.find("__g__", from);
    if (pf == std::string_view::npos) return pg;
    if (pg == std::string_view::npos) return pf;
    return std::min(pf, pg);
}

struct OwnerMethod {
    std::string_view pkg;     // "" when the name is unqualified
    std::string_view owner;   // the carried / registry-matched owner
    std::string_view method;  // the short method name
    std::string_view tail;    // "" or "__f__…" / "__g__…"
};

// The `[<pkg>.]` head of a mangled owner-method name. A package path may
// contain dots; a struct/enum NAME may not (the `.` is the package separator
// in the Logos path model), so the LAST dot is always the composed boundary.
// Returns {pkg, rest}.
inline std::pair<std::string_view, std::string_view>
split_pkg(std::string_view name) noexcept {
    auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return {std::string_view{}, name};
    return {name.substr(0, dot), name.substr(dot + 1)};
}

namespace detail {
// Decompose `body` (already pkg-stripped) assuming `owner` is its owner.
// Requires the literal composition  <owner> "__" <method> [ "__f__"… | "__g__"… ]
// with a NON-EMPTY method. Returns nullopt when `body` was not composed so.
inline std::optional<OwnerMethod>
decompose_body(std::string_view pkg, std::string_view body,
               std::string_view owner) noexcept {
    if (owner.empty()) return std::nullopt;
    if (body.size() <= owner.size() + 2) return std::nullopt;
    if (body.compare(0, owner.size(), owner) != 0) return std::nullopt;
    if (body.compare(owner.size(), 2, "__") != 0) return std::nullopt;
    size_t mstart = owner.size() + 2;
    auto   sb     = sig_boundary(body, mstart);
    if (sb == mstart) return std::nullopt;   // empty method name
    std::string_view method = (sb == std::string_view::npos)
                                  ? body.substr(mstart)
                                  : body.substr(mstart, sb - mstart);
    std::string_view tail   = (sb == std::string_view::npos)
                                  ? std::string_view{}
                                  : body.substr(sb);
    if (method.empty()) return std::nullopt;
    return OwnerMethod{pkg, body.substr(0, owner.size()), method, tail};
}
}  // namespace detail

// EXACT: the caller already KNOWS the owner. Verifies the composed anchor
//   [<pkg>.]<owner> "__" <method> ("__f__"|"__g__")<sig>
// and returns the pieces. `owner` may itself be given pkg-qualified; the
// package head of `name` is stripped first either way.
// nullopt = the name was NOT composed that way; the caller must treat that as
// a fact, not as a reason to fall back to a split.
inline std::optional<OwnerMethod>
split_known_owner(std::string_view name, std::string_view owner) noexcept {
    auto [pkg, body]   = split_pkg(name);
    auto [_, bare_own] = split_pkg(owner);
    if (auto r = detail::decompose_body(pkg, body, bare_own)) return r;
    // The owner may be spelled qualified while the name is not (or vice
    // versa); retry against the FULL name with the FULL owner.
    if (bare_own.size() != owner.size())
        if (auto r = detail::decompose_body({}, name, owner)) return r;
    return std::nullopt;
}

// EXACT: the caller knows owner AND method — the strongest form, and the one
// to prefer. This is RECOMPOSE-AND-COMPARE, not a split: it asserts that
// `name` literally begins with `[pkg.]<owner>"__"<method>` and that whatever
// follows is a signature tail ("" or "__f__…"/"__g__…"). No boundary is
// guessed, so a method name containing `__` (`fn a__f__b`) is handled by
// construction.
// Returns the signature tail; nullopt = not composed that way.
inline std::optional<std::string_view>
sig_of(std::string_view name, std::string_view owner,
       std::string_view method) noexcept {
    if (owner.empty() || method.empty()) return std::nullopt;
    auto try_body = [&](std::string_view body,
                        std::string_view own) -> std::optional<std::string_view> {
        size_t need = own.size() + 2 + method.size();
        if (body.size() < need) return std::nullopt;
        if (body.compare(0, own.size(), own) != 0) return std::nullopt;
        if (body.compare(own.size(), 2, "__") != 0) return std::nullopt;
        if (body.compare(own.size() + 2, method.size(), method) != 0)
            return std::nullopt;
        std::string_view rest = body.substr(need);
        if (rest.empty()) return rest;
        if (rest.starts_with("__f__") || rest.starts_with("__g__")) return rest;
        return std::nullopt;
    };
    auto [pkg, body]   = split_pkg(name);
    auto [_, bare_own] = split_pkg(owner);
    if (auto r = try_body(body, bare_own)) return r;
    if (bare_own.size() != owner.size())
        if (auto r = try_body(name, owner)) return r;
    return std::nullopt;
}

// REGISTRY: no owner part in hand. `is_known(sv)` answers "is this a declared
// owner". Candidate prefixes are tried LONGEST-FIRST and the remainder must
// decompose into a non-empty method; the longest accepted candidate wins.
//
// Why longest-first matters: `foo` and `foo_` may both be declared, and
// `foo___i64__f__…` starts with both. Only `foo_` reproduces the whole name
// from the composition the producer actually performed.
inline std::optional<OwnerMethod>
split_by_registry(std::string_view name,
                  const std::function<bool(std::string_view)>& is_known) noexcept {
    auto [pkg, body] = split_pkg(name);
    // Every `__` in `body` is a CANDIDATE boundary; walk them longest-first.
    std::vector<size_t> bounds;
    for (size_t p = body.find("__"); p != std::string_view::npos;
         p = body.find("__", p + 1))
        if (p > 0) bounds.push_back(p);
    for (auto it = bounds.rbegin(); it != bounds.rend(); ++it) {
        std::string_view cand = body.substr(0, *it);
        if (!is_known(cand)) {
            // Also probe the pkg-qualified spelling — registries commonly key
            // both, and the caller's predicate decides which it accepts.
            if (pkg.empty()) continue;
            std::string q;
            q.reserve(pkg.size() + 1 + cand.size());
            q.append(pkg).append(".").append(cand);
            if (!is_known(q)) continue;
        }
        if (auto r = detail::decompose_body(pkg, body, cand)) return r;
    }
    return std::nullopt;
}

// CARRIED METHOD, UNKNOWN OWNER. The inverse of `split_known_owner`: the
// caller holds the method's source name (dk::METHOD_BASE) but the owner
// spelled in the symbol is not one it can name (a blanket-impl host
// `$blanket$…`, a re-hosted spec fn). The owner is then whatever precedes an
// occurrence of `"__"<method>` that leaves a legal signature tail — and, as
// everywhere else in this file, the LONGEST such owner wins, because a shorter
// one would leave `__`-bearing text unaccounted for.
// This is recompose-and-compare against a carried part, not a boundary guess.
inline std::optional<OwnerMethod>
split_by_method(std::string_view name, std::string_view method) noexcept {
    if (method.empty()) return std::nullopt;
    auto [pkg, body] = split_pkg(name);
    std::string needle;
    needle.reserve(method.size() + 2);
    needle.append("__").append(method);
    // Walk occurrences right-to-left: the last one leaves the longest owner.
    for (size_t p = body.rfind(needle); p != std::string_view::npos;
         p = (p == 0 ? std::string_view::npos : body.rfind(needle, p - 1))) {
        if (p == 0) break;                       // empty owner
        std::string_view tail = body.substr(p + needle.size());
        if (!tail.empty() && !tail.starts_with("__f__") && !tail.starts_with("__g__")) {
            if (p == 0) break;
            continue;
        }
        return OwnerMethod{pkg, body.substr(0, p), body.substr(p + 2, method.size()),
                           tail};
    }
    return std::nullopt;
}

// Same shape for the package boundary of a link symbol: the longest dotted
// prefix of `sym` that `is_known_pkg` accepts. nullopt = no known package is
// a prefix.
inline std::optional<std::string_view>
package_prefix(std::string_view sym,
               const std::function<bool(std::string_view)>& is_known_pkg) noexcept {
    for (size_t dot = sym.rfind('.'); dot != std::string_view::npos;
         dot = (dot == 0 ? std::string_view::npos : sym.rfind('.', dot - 1))) {
        std::string_view cand = sym.substr(0, dot);
        if (!cand.empty() && is_known_pkg(cand)) return cand;
        if (dot == 0) break;
    }
    return std::nullopt;
}

}  // namespace logos::compiler::mname
