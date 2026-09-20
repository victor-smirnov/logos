#pragma once
// DefTable: every declaration's identity (#438).
//
// Name resolution turns a written path into a DefId once; from then on a
// phase names the entity by its DefId and never looks a spelled name up again.
// As in rustc there are two forms:
//   DefPath  (namespace, package, name): stable across compilations; what an
//            archive, the L-IR and a diagnostic carry.
//   DefId    a dense index into this table, local to one compilation. Interning
//            the same path twice gives the same id, so a declaration collected
//            from source and again from an archive (or re-collected after a
//            metaprog round) keeps its id.
// The table only grows: a registry that drops an entry (sema's user-state reset)
// drops its own record, and the path still maps to the same id when it returns.

#include "logos/compiler/str_map.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace logos::compiler {

struct DefId {
    // A STABLE hash of the path, not a counter: rustc's DefPathHash, for the
    // same reason. Sema runs several times over one program (metaprog rounds,
    // the discovery pass, a cache-less re-run), and a counter gave one path two
    // ids across those runs — records written by one run then missed the other
    // run's lookups. A hash is the same in every run and in every process.
    uint64_t v = 0;                       // 0 = no entity
    explicit operator bool() const noexcept { return v != 0; }
    friend bool operator==(DefId a, DefId b) noexcept { return a.v == b.v; }
    friend auto operator<=>(DefId a, DefId b) noexcept { return a.v <=> b.v; }
};

// Rust's namespaces: a trait, a struct and an enum share the TYPE namespace
// (`trait T` and `struct T` in one package collide); fns, consts and statics
// share the VALUE namespace.
enum class DefNs : uint8_t { Type, Value };

enum class DefKind : uint8_t { Trait, Struct, Enum, Datatype, Union, Alias, Fn, Const, Static };

inline DefNs def_ns(DefKind k) noexcept {
    switch (k) {
    case DefKind::Fn: case DefKind::Const: case DefKind::Static: return DefNs::Value;
    default: return DefNs::Type;
    }
}

struct DefEntry {
    DefKind     kind{};
    std::string package;   // empty: a package-less file (the root)
    std::string name;
};

class DefTable {
public:
    // The id of (kind's namespace, package, name). Minting is a hash, so the
    // same path gives the same id in any run; the table records what the id
    // stands for, for diagnostics and for name lookups.
    DefId intern(DefKind kind, std::string_view package, std::string_view name) {
        DefId id = hash_of(def_ns(kind), package, name);
        auto [it, fresh] = entries_.try_emplace(id.v, DefEntry{kind, std::string(package), std::string(name)});
        if (!fresh && (it->second.package != package || it->second.name != name))
            collision(it->second, package, name);
        return id;
    }
    // Empty unless the path has been interned in this compilation — "is there
    // such a declaration", not "what would its id be".
    DefId find(DefNs ns, std::string_view package, std::string_view name) const {
        DefId id = hash_of(ns, package, name);
        return entries_.count(id.v) ? id : DefId{};
    }
    const DefEntry& operator[](DefId id) const {
        static const DefEntry kNone{};
        auto it = entries_.find(id.v);
        return it == entries_.end() ? kNone : it->second;
    }
    size_t size() const noexcept { return entries_.size(); }

    // `package::name`, or `name` for the root: the spelling a diagnostic and
    // the L-IR's identity fields use.
    std::string path(DefId id) const {
        const auto& e = (*this)[id];
        return e.package.empty() ? e.name : e.package + "::" + e.name;
    }

private:
    // FNV-1a over (namespace, package, \x1f, name). 64 bits: a collision would
    // make two declarations one, so it is checked at intern time rather than
    // assumed away.
    static DefId hash_of(DefNs ns, std::string_view package, std::string_view name) noexcept {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
        mix(ns == DefNs::Type ? 'T' : 'V');
        for (char c : package) mix(uint8_t(c));
        mix(0x1f);
        for (char c : name) mix(uint8_t(c));
        return DefId{h | 1ull};   // never 0, which means "no entity"
    }
    [[noreturn]] static void collision(const DefEntry& have,
                                       std::string_view package, std::string_view name) {
        std::fprintf(stderr,
            "logosc INTERNAL: DefId collision — '%s::%s' and '%.*s::%.*s' hash alike.\n"
            "  Two declarations would become one entity. Widen the hash.\n",
            have.package.c_str(), have.name.c_str(),
            (int)package.size(), package.data(), (int)name.size(), name.data());
        std::abort();
    }
    std::unordered_map<uint64_t, DefEntry> entries_;
};

}  // namespace logos::compiler

template <> struct std::hash<logos::compiler::DefId> {
    size_t operator()(logos::compiler::DefId d) const noexcept { return std::hash<uint64_t>{}(d.v); }
};
