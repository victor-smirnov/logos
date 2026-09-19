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
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace logos::compiler {

struct DefId {
    uint32_t v = 0;                       // 0 = no entity
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
    DefTable() { entries_.emplace_back(); }   // id 0 = none

    // The id of (kind's namespace, package, name), minted on first sight.
    // A second declaration of the same path is the caller's duplicate check.
    DefId intern(DefKind kind, std::string_view package, std::string_view name) {
        std::string key = path_key(def_ns(kind), package, name);
        if (auto it = by_path_.find(key); it != by_path_.end()) return it->second;
        DefId id{static_cast<uint32_t>(entries_.size())};
        entries_.push_back({kind, std::string(package), std::string(name)});
        by_path_.emplace(std::move(key), id);
        return id;
    }
    DefId find(DefNs ns, std::string_view package, std::string_view name) const {
        auto it = by_path_.find(path_key(ns, package, name));
        return it == by_path_.end() ? DefId{} : it->second;
    }
    const DefEntry& operator[](DefId id) const { return entries_.at(id.v); }
    size_t size() const noexcept { return entries_.size() - 1; }

    // `package::name`, or `name` for the root: the spelling a diagnostic and
    // the L-IR's identity fields use.
    std::string path(DefId id) const {
        const auto& e = (*this)[id];
        return e.package.empty() ? e.name : e.package + "::" + e.name;
    }

private:
    static std::string path_key(DefNs ns, std::string_view package, std::string_view name) {
        std::string k;
        k.reserve(package.size() + name.size() + 3);
        k.push_back(ns == DefNs::Type ? 'T' : 'V');
        k.append(package);
        k.push_back('\x1f');   // cannot occur in a package or a name
        k.append(name);
        return k;
    }
    std::vector<DefEntry> entries_;
    StrMap<DefId> by_path_;
};

}  // namespace logos::compiler

template <> struct std::hash<logos::compiler::DefId> {
    size_t operator()(logos::compiler::DefId d) const noexcept { return std::hash<uint32_t>{}(d.v); }
};
