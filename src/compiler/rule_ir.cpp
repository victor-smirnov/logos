// Logos project — https://github.com/victor-smirnov/logos
//
// See include/logos/compiler/rule_ir.hpp.

#include <logos/compiler/rule_ir.hpp>

#include <unordered_map>
#include <utility>

namespace logos::compiler::rule_ir {

namespace {

struct Entry {
    logos::writ::Writ   doc;    // owns the arena the root lives in
    logos::writ::AnyVal root;
};

// One doc per macro site. Keyed exactly like the macro-arg blob table
// (`prog.macro_arg_blobs`), so a thunk that knows its site_id can find both.
std::unordered_map<uint64_t, Entry>& table() {
    static std::unordered_map<uint64_t, Entry> t;
    return t;
}

}  // namespace

void put(uint64_t site_id, logos::writ::Writ doc, logos::writ::AnyVal root) {
    // `root` is a Ref AnyVal: at rest it is a SELF-RELATIVE delta from the slot
    // it sits in. Moving it into the map re-anchors it (AnyVal's copy ctor
    // resolves then re-encodes), which is why the doc must be moved in FIRST —
    // resolving `root` needs the arena to still be where it was.
    auto& t = table();
    t.erase(site_id);
    t.emplace(site_id, Entry{std::move(doc), root});
}

const uint8_t* root_ptr(uint64_t site_id) {
    auto& t = table();
    auto it = t.find(site_id);
    if (it == t.end()) return nullptr;
    logos::writ::AnyVal r = it->second.root;
    if (!r.is_ref()) return nullptr;
    return r.resolve();
}

void clear() { table().clear(); }

}  // namespace logos::compiler::rule_ir
