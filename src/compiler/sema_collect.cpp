// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include "ctfe.hpp"

#include <cstdio>
#include <format>
#include <functional>

#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

namespace logos::compiler {

namespace la = ast;
using writ::TinyMapView;
using writ::ArrayView;
using writ::StringView;
using writ::AnyVal;
using writ::MemHolder;

// MC2.5: ODR-style structural equality between two AST sub-trees rooted at
// AnyVal pointers. Used to dedup item-level definitions emitted from multiple
// metacalls (e.g. two metafns each emitting `struct Synth { x: i32 }` should
// not collide). Walks TinyObjectMap by bitmap key, ObjectArray by index, and
// WritString by view; conservative on unknown Data-tag objects.
namespace {
bool ast_anyval_equal(AnyVal a, AnyVal b,
                      writ::MemHolder* ha, writ::MemHolder* hb);

bool ast_tom_equal(writ::TinyMapView a, writ::TinyMapView b,
                   writ::MemHolder* ha, writ::MemHolder* hb) {
    // SRC_LINE is purely diagnostic; ignore it in ODR equality so items
    // emitted from quote_item! at different source lines still dedup.
    constexpr uint64_t skip_mask = 1ULL << la::SRC_LINE.code;
    if ((a.bitmap() & ~skip_mask) != (b.bitmap() & ~skip_mask)) return false;
    for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k) {
        if (!a.has_key(k) || k == la::SRC_LINE.code) continue;
        if (!ast_anyval_equal(a.get(k), b.get(k), ha, hb))
            return false;
    }
    return true;
}

bool ast_array_equal(writ::ArrayView a, writ::ArrayView b,
                     writ::MemHolder* ha, writ::MemHolder* hb) {
    if (a.size() != b.size()) return false;
    for (uint64_t i = 0; i < a.size(); ++i) {
        if (!ast_anyval_equal(a.get(i), b.get(i), ha, hb))
            return false;
    }
    return true;
}

bool ast_anyval_equal(AnyVal a, AnyVal b,
                      writ::MemHolder* ha, writ::MemHolder* hb) {
    if (a.is_null() && b.is_null()) return true;
    if (a.is_null() || b.is_null()) return false;
    if (a.is_value() != b.is_value()) return false;
    if (a.is_value()) return a.raw() == b.raw();
    auto ta = writ::TypeTag::read_before(a.resolve());
    auto tb = writ::TypeTag::read_before(b.resolve());
    if (ta.type_code() != tb.type_code()) return false;
    if (ta.type_code() == writ::type_hash::TinyObjectMap)
        return ast_tom_equal(writ::as_tinymap(a, ha), writ::as_tinymap(b, hb), ha, hb);
    if (ta.type_code() == writ::type_hash::Array)
        return ast_array_equal(writ::as_array(a, ha), writ::as_array(b, hb), ha, hb);
    if (ta.type_code() == writ::type_hash::WritString)
        return writ::StringView(a, ha).view() == writ::StringView(b, hb).view();
    // Unknown data tag — be conservative and treat as not equal.
    return false;
}
} // namespace

// Symbol-collection phase: populate SemaChecker symbol tables.

void SemaChecker::collect(const std::vector<writ::Writ>& asts) {
    // Helper: build ImportScope (wildcard_packages) from a module's USES array.
    auto build_import_scope = [&](TinyMapView root) -> ImportScope {
        ImportScope scope;
        if (!root.has_key(la::USES)) return scope;
        auto uses_av = root.get(la::USES.code);
        if (uses_av.is_null() || !uses_av.is_pointer()) return scope;
        auto uses = arr_of(uses_av);
        for (uint64_t i = 0; i < uses.size(); ++i) {
            auto use_node = map_of(uses.get(i));
            // CP-cm-02: `use pkg.Path.Type.{V1, V2, …};` — register each
            // listed variant under the bare-name alias map. The dotted-path
            // portion still becomes a wildcard import so the enum type
            // itself is in scope (call sites can use both `Type::V1` and
            // bare `V1`). TYPE_NAME is the last segment (the enum); the
            // pkg head + PATH_PARTS up to TYPE_NAME form the wildcard pkg.
            int32_t use_code = la::USE.code;
            if (use_node.has_key(la::CODE)) {
                auto cv = use_node.get(la::CODE.code);
                if (!cv.is_null() && !cv.is_pointer())
                    use_code = cv.as_value<int32_t>();
            }
            std::string dotted;
            if (use_node.has_key(la::NAME)) {
                dotted = std::string(str_of(use_node.get(la::NAME.code)));
            }
            if (use_node.has_key(la::mod::PATH_PARTS)) {
                auto parts = arr_of(use_node.get(la::mod::PATH_PARTS.code));
                for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                    auto part = map_of(parts.get(pi));
                    if (!part.has_key(la::NAME)) continue;
                    if (!dotted.empty()) dotted += '.';
                    dotted += std::string(str_of(part.get(la::NAME.code)));
                }
            }
            // GR-gp-02: `use pkg.{a, b, c};` parses as USE_VARIANTS with
            // a lowercase TYPE_NAME — desugar to wildcard imports
            // `<dotted>.<TYPE_NAME>.<item>`. Capitalised TYPE_NAME is the
            // enum-variant form handled below.
            if (use_code == la::USE_VARIANTS.code && use_node.has_key(la::TYPE_NAME)) {
                std::string tn(str_of(use_node.get(la::TYPE_NAME.code)));
                if (!tn.empty() && tn[0] >= 'a' && tn[0] <= 'z') {
                    std::string prefix = dotted.empty()
                        ? tn : (dotted + "." + tn);
                    if (use_node.has_key(la::VARIANTS)) {
                        auto vlist_av = use_node.get(la::VARIANTS.code);
                        if (!vlist_av.is_null() && vlist_av.is_pointer()) {
                            auto vlist = arr_of(vlist_av);
                            for (uint64_t vi = 0; vi < vlist.size(); ++vi) {
                                auto v = map_of(vlist.get(vi));
                                if (!v.has_key(la::NAME)) continue;
                                auto bare = std::string(str_of(v.get(la::NAME.code)));
                                std::string full = prefix + "." + bare;
                                if (std::find(scope.wildcard_packages.begin(),
                                              scope.wildcard_packages.end(), full)
                                    != scope.wildcard_packages.end())
                                    continue;
                                scope.wildcard_packages.push_back(std::move(full));
                            }
                        }
                    }
                    continue;
                }
            }
            if (use_code == la::USE_VARIANTS.code) {
                if (use_node.has_key(la::VARIANTS)) {
                    auto vlist_av = use_node.get(la::VARIANTS.code);
                    if (!vlist_av.is_null() && vlist_av.is_pointer()) {
                        auto vlist = arr_of(vlist_av);
                        std::string type_q;  // pkg-qualifier captured but not
                        // emitted in the alias today (Logos resolves bare
                        // variant against any enum carrying that variant
                        // via find_enum_by_name during lower).
                        for (uint64_t vi = 0; vi < vlist.size(); ++vi) {
                            auto v = map_of(vlist.get(vi));
                            if (!v.has_key(la::NAME)) continue;
                            auto bare = std::string(str_of(v.get(la::NAME.code)));
                            // Resolve TYPE_NAME for the alias value; the
                            // dotted path captured above is the pkg part
                            // (e.g. "std.lang.ord"), TYPE_NAME is "Ordering".
                            std::string enum_qual;
                            if (use_node.has_key(la::TYPE_NAME)) {
                                enum_qual = std::string(
                                    str_of(use_node.get(la::TYPE_NAME.code)));
                            }
                            scope.variant_aliases[bare] = enum_qual;
                        }
                    }
                }
                // Make the underlying pkg visible too (so `Type::V` still
                // resolves alongside bare `V`).
                if (!dotted.empty())
                    scope.wildcard_packages.push_back(std::move(dotted));
                continue;
            }
            if (dotted.empty()) continue;
            // B-mv-10: warn on `use pkg;` repeated in the same module.
            // Functional behaviour is unchanged (effective_import_pkgs already
            // dedups), but copy-paste mistakes silently slipped through.
            if (std::find(scope.wildcard_packages.begin(),
                          scope.wildcard_packages.end(), dotted)
                != scope.wildcard_packages.end()) {
                warn(std::format("duplicate 'use {};' in module", dotted));
            }
            // B-mv-11: self-import — `use cur_package_;` is a no-op (own
            // package symbols already resolve first).  Warn so users notice
            // the redundancy.
            if (!cur_package_.empty() && dotted == cur_package_) {
                warn(std::format("'use {};': self-import has no effect "
                                 "(own package is always in scope)", dotted));
            }
            scope.wildcard_packages.push_back(dotted);
            // §3: `use pkg from <module>;` — restrict this package's candidates
            // to the named module. The contextual `from` keyword is matched as a
            // bare IDENT in the grammar (so `From::from` stays valid), so validate
            // it here; resolve the module NAME (bare or quoted) to its mangle id.
            if (use_node.has_key(la::mod::FROM_MODULE)) {
                std::string kw;
                if (use_node.has_key(la::mod::FROM_KW))
                    kw = std::string(str_of(use_node.get(la::mod::FROM_KW.code)));
                if (kw != "from") {
                    error(std::format("expected 'from' before the module name in "
                                      "`use {} ...;`, found '{}'", dotted, kw));
                } else {
                    std::string mname;
                    auto fm = map_of(use_node.get(la::mod::FROM_MODULE.code));
                    if (fm.has_key(la::NAME))
                        mname = std::string(str_of(fm.get(la::NAME.code)));
                    // The STRING token keeps its surrounding quotes — strip them.
                    if (mname.size() >= 2 && mname.front() == '"' && mname.back() == '"')
                        mname = mname.substr(1, mname.size() - 2);
                    if (mname.empty()) {
                        error(std::format("`use {} from`: missing module name", dotted));
                    } else if (!module_name_to_id_ || module_name_to_id_->empty()) {
                        // Map not primed (e.g. a metaprog discovery pass before the
                        // loaded-module set is threaded). Skip the restriction
                        // silently here; the final pass carries the real map.
                    } else if (auto it = module_name_to_id_->find(mname);
                               it == module_name_to_id_->end()) {
                        error(std::format("`use {} from {}`: no loaded module is "
                                          "named '{}'", dotted, mname, mname));
                    } else {
                        scope.pkg_from_module_id[dotted] = it->second;
                    }
                }
            }
            // `pub use pkg;` — register as re-export from current package
            bool is_pub = use_node.has_key(la::IS_PUB) &&
                          !use_node.get(la::IS_PUB.code).is_null() &&
                          use_node.get(la::IS_PUB.code).is_value() &&
                          use_node.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            if (is_pub && !cur_package_.empty()) {
                auto& vec = pkg_reexports_[cur_package_];
                if (std::find(vec.begin(), vec.end(), dotted) == vec.end())
                    vec.push_back(dotted);
            }
        }
        return scope;
    };

    // Three-layer split Phase 3.4: append the manifest-declared implicit
    // prelude to the wildcard scope unless this file opts out via
    // `#![no_implicit_prelude]` or is loaded from a binary archive (its
    // producer already applied its own prelude when the archive was built).
    // Self-imports are skipped (own package symbols always resolve first).
    auto maybe_inject_implicit_prelude = [&](TinyMapView root, bool is_bin) {
        if (implicit_prelude_.empty() || is_bin) return;
        if (cur_package_ == implicit_prelude_) return;
        // Scan ITEMS for INNER_ANNOTATION{NAME="no_implicit_prelude"} opt-out.
        if (root.has_key(la::ITEMS)) {
            auto items = arr_of(root.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto it = map_of(items.get(i));
                if (code_of(it) != la::INNER_ANNOTATION.code) continue;
                if (!it.has_key(la::NAME)) continue;
                if (str_of(it.get(la::NAME.code)) == "no_implicit_prelude")
                    return;
            }
        }
        // Dedup against any explicit `use <prelude>;` already in scope.
        if (std::find(cur_imports_.wildcard_packages.begin(),
                      cur_imports_.wildcard_packages.end(),
                      implicit_prelude_)
            == cur_imports_.wildcard_packages.end()) {
            cur_imports_.wildcard_packages.push_back(implicit_prelude_);
        }
    };
    // MC2.5: per-name first-seen item record for ODR dedup. On a name
    // collision we deep-compare the new item's AST sub-tree against the
    // first-seen one; equal → silently skip (dedup), differ → "duplicate".
    struct FirstSeen { writ::MemHolder* holder; uint32_t off; };
    std::unordered_map<std::string, FirstSeen> first_struct;
    std::unordered_map<std::string, FirstSeen> first_datatype;
    std::unordered_map<std::string, FirstSeen> first_enum;
    auto item_off = [](TinyMapView t) -> uint32_t {
        return static_cast<uint32_t>(t.offset().value());
    };
    auto items_equal = [](FirstSeen a, writ::MemHolder* hb, uint32_t off_b) {
        return ast_tom_equal(
            writ::TinyMapView(writ::arena_offset_t(a.off), a.holder),
            writ::TinyMapView(writ::arena_offset_t(off_b), hb),
            a.holder, hb);
    };

    // Pre-scan: collect every declared type name (struct/datatype/enum) across
    // ALL modules so is_specialization_fn can tell a concrete type-arg
    // (e.g. `Map<K, AnyVal>` → partial spec) from a fresh type-param
    // (`Map<K, V>` → generic base) without depending on whether the arg's
    // defining module was processed first. Order-independence: without this,
    // a partial spec whose concrete arg hasn't been registered yet is
    // mis-classified as a second base and trips a spurious "duplicate datatype".
    logos::compiler::StrSet pass0_decl_names;
    for (size_t pre_ai = 0; pre_ai < asts.size(); ++pre_ai) {
        if (pre_ai < delta_start_idx_) continue;       // already registered earlier
        auto& ast = asts[pre_ai];
        bool is_bin = (from_binary_ && pre_ai < from_binary_->size())
                      ? (*from_binary_)[pre_ai] : false;
        if (is_bin && collected_holders_.count(ast.holder())) continue;
        holder_ = ast.holder();                          // tiny-map views resolve against holder_
        auto root = ast.root_object().as_tiny_map();
        if (!root.has_key(la::ITEMS)) continue;
        auto items = arr_of(root.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t ic = code_of(item);
            if ((ic == la::STRUCT || ic == la::DATATYPE || ic == la::ENUM ||
                 ic == la::UNION_DEF || ic == la::SCHEMA_DEF ||
                 ic == la::SCHEMA_ENUM_DEF)
                    && item.has_key(la::NAME.code))
                pass0_decl_names.insert(std::string(str_of(item.get(la::NAME.code))));
        }
    }
    pass0_decl_names_ = &pass0_decl_names;

    // First pass: register names (so forward references work).
    for (size_t pass0_ai = 0; pass0_ai < asts.size(); ++pass0_ai) {
        // Module system: record pkg→module for EVERY ast — including cached
        // binary + delta-skipped ones — so LProgram::pkg_module_ids is COMPLETE
        // downstream. Otherwise mlir-gen's link_name() can't recognise a binary
        // stdlib method's module → it isn't binary-skipped → every stdlib method
        // body is re-emitted in the consumer (O(n²) lookupSymbol blowup). Cheap
        // (one package-name read); done before the skips below.
        if (module_ids_ && pass0_ai < module_ids_->size() &&
            !(*module_ids_)[pass0_ai].empty()) {
            auto pk = read_package_name(asts[pass0_ai].root_object().as_tiny_map());
            if (!pk.empty()) pkg_module_ids_[pk] = (*module_ids_)[pass0_ai];
        }
        // M6.1: delta mode — skip asts already processed in a prior
        // sema_lower call within this same compile session.
        if (pass0_ai < delta_start_idx_) continue;
        auto& ast = asts[pass0_ai];
        // M5 step 3b+5: skip ONLY cached BINARY holders. User asts
        // re-walk so the strict-mode final sema still validates them
        // (e.g. catches "unknown type 'WritStatic'" when the user
        // file doesn't `use` it). Cached binary asts are safe to skip
        // because they're processed identically in every mode.
        bool is_bin = (from_binary_ && pass0_ai < from_binary_->size())
                      ? (*from_binary_)[pass0_ai] : false;
        if (is_bin &&
            collected_holders_.count(ast.holder())) continue;
        holder_ = ast.holder();
        auto root = ast.root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        cur_imports_ = build_import_scope(root);
        maybe_inject_implicit_prelude(root, is_bin);
        // M5 step 5c: record user-pkgs so take_snapshot can filter
        // their entries out before persisting.
        if (!is_bin && !cur_package_.empty())
            user_pkgs_.insert(cur_package_);
        if (!root.has_key(la::ITEMS)) continue;
        auto items = arr_of(root.get(la::ITEMS.code));
        std::vector<TinyMapView> pass0_pending;
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            int32_t ic = code_of(item);
            if (ic == la::ANNOTATION) {
                pass0_pending.push_back(item);
                continue;
            }
            // T0-6: cfg gate runs FIRST — a cfg-false item must not
            // pre-register its name (the cfg(unix)/cfg(windows) same-name
            // struct/enum/trait idiom otherwise died with "duplicate
            // struct" here before the gated collection pass ever ran).
            if (cfg_attrs_drop_item(pass0_pending)) {
                pass0_pending.clear();
                continue;
            }
            // `#[datatype]`/`#[annotation]` promote a STRUCT-syntax item into
            // the datatype pipeline; `#[zoned]` marks self-relative fields and
            // does NOT promote. (Single-point parser: parse_struct_attr_flags.)
            bool is_datatype_struct = false;
            if (ic == la::STRUCT)
                is_datatype_struct = parse_struct_attr_flags(pass0_pending).promotes_to_datatype();
            pass0_pending.clear();
            if (ic == la::STRUCT && !is_datatype_struct) {
                if (!item.has_key(la::NAME.code)) continue;  // struct_inst — skip name registration
                if (is_specialization_struct(item)) continue;  // specs registered later
                auto sname = std::string(str_of(item.get(la::NAME.code)));
                auto key = sema_key(cur_package_, sname);
                if (structs_.count(key)) {
                    auto fit = first_struct.find(key);
                    if (fit != first_struct.end()
                            && items_equal(fit->second, holder_, item_off(item))) {
                        // ODR-equal duplicate emitted from another splice; ignore.
                    } else {
                        error(std::format("duplicate struct '{}'", sname));
                    }
                } else {
                    structs_[key] = {};
                    first_struct[key] = {holder_, item_off(item)};
                    first_struct_decl_[key] = {static_cast<void*>(holder_), item_off(item)};
                }
            } else if (ic == la::UNION_DEF) {
                // §6.1 (Rust `items.union.namespace`): unions share the
                // struct/enum type namespace. Pre-register the name in
                // structs_ (with a placeholder, mirroring STRUCT) so
                // phase-2 `type Alias = U;` resolves U through
                // find_struct_by_name before the union body is collected
                // in phase 1. Without this, `type UA = U;` errored
                // "unknown type 'U'" because pass-0 only walked STRUCT/
                // DATATYPE/ENUM (the asymmetry that surfaced as P31).
                if (!item.has_key(la::NAME.code)) continue;
                auto uname = std::string(str_of(item.get(la::NAME.code)));
                auto key = sema_key(cur_package_, uname);
                if (structs_.count(key)) {
                    auto fit = first_struct.find(key);
                    if (fit != first_struct.end()
                            && items_equal(fit->second, holder_, item_off(item))) {
                        // ODR-equal duplicate.
                    } else {
                        error(std::format("duplicate struct/union '{}'", uname));
                    }
                } else {
                    structs_[key] = {};
                    first_struct[key] = {holder_, item_off(item)};
                }
            } else if (ic == la::SCHEMA_DEF || ic == la::SCHEMA_ENUM_DEF) {
                // ADR 0011: a `schema S {…}` / `schema enum E {…}` registers in the
                // struct namespace (both are Struct-shaped views), mirroring
                // STRUCT/UNION_DEF pre-registration so forward refs resolve before
                // the body is collected in phase 1.
                if (!item.has_key(la::NAME.code)) continue;
                auto sname = std::string(str_of(item.get(la::NAME.code)));
                auto key = sema_key(cur_package_, sname);
                if (structs_.count(key)) {
                    auto fit = first_struct.find(key);
                    if (fit != first_struct.end()
                            && items_equal(fit->second, holder_, item_off(item))) {
                        // ODR-equal duplicate from another splice; ignore.
                    } else {
                        error(std::format("duplicate schema/struct '{}'", sname));
                    }
                } else {
                    structs_[key] = {};
                    first_struct[key] = {holder_, item_off(item)};
                }
            } else if ((ic == la::STRUCT && is_datatype_struct) || ic == la::DATATYPE) {
                // Explicit instantiation declarations have no NAME key — skip name registration.
                if (!item.has_key(la::NAME.code)) continue;
                if (is_specialization_struct(item)) continue;  // partial/full specs registered later
                auto dname = std::string(str_of(item.get(la::NAME.code)));
                auto key = sema_key(cur_package_, dname);
                if (datatypes_.count(key)) {
                    auto fit = first_datatype.find(key);
                    if (fit != first_datatype.end()
                            && items_equal(fit->second, holder_, item_off(item))) {
                        // ODR-equal duplicate.
                    } else {
                        error(std::format("duplicate datatype '{}'", dname));
                    }
                } else {
                    datatypes_[key] = {};
                    first_datatype[key] = {holder_, item_off(item)};
                }
            } else if (ic == la::ENUM) {
                auto ename = std::string(str_of(item.get(la::NAME.code)));
                auto key = sema_key(cur_package_, ename);
                if (enums_.count(key)) {
                    auto fit = first_enum.find(key);
                    if (fit != first_enum.end()
                            && items_equal(fit->second, holder_, item_off(item))) {
                        // ODR-equal duplicate.
                    } else {
                        error(std::format("duplicate enum '{}'", ename));
                    }
                } else {
                    enums_[key] = {};
                    first_enum[key] = {holder_, item_off(item)};
                }
            } else if (ic == la::TRAIT_DEF) {
                // Pre-register trait names so collect_impl in pass2 can
                // resolve `impl Trait for X` regardless of the iteration
                // order between the impl's file and the trait's defining
                // file. Without this, cross-file impl-of-trait fails when
                // the trait's file is iterated AFTER the impl's file —
                // critical for cross-archive cases where binary-loaded
                // trait packages arrive later in `asts`. collect_trait
                // (pass2) populates the body and treats this pre-existing
                // empty entry as a no-op (see traits_.count(tname) guard).
                if (item.has_key(la::NAME.code)) {
                    auto tname = std::string(str_of(item.get(la::NAME.code)));
                    if (!traits_.count(tname)) {
                        SemaTraitInfo placeholder{};
                        placeholder.package = cur_package_;
                        placeholder.predeclared = true;
                        // T1-9: carry IS_PUB on the placeholder too — a
                        // cross-package reference resolving BEFORE the
                        // defining file's collect_trait must not read a
                        // default-false is_pub (spurious "private to
                        // package" on pub stdlib traits).
                        if (item.has_key(la::IS_PUB)) {
                            AnyVal pv = item.get(la::IS_PUB.code);
                            placeholder.is_pub = !pv.is_null() && pv.is_value() &&
                                                 pv.as_value<uint8_t>() != 0;
                        }
                        traits_[tname] = std::move(placeholder);
                    }
                }
            }
        }
    }
    pass0_decl_names_ = nullptr;  // pre-scan set is pass-0 scoped; goes out of scope below

    // Intermediate pass: type aliases and consts (Phase 2). Wait, we execute this FIRST so aliases are known for fn signatures.
    // M5 step 3b: skip ASTs whose collect contributions are already in
    // the symbol tables (installed via SemaCheckerSnapshot at run-time).
    // The snapshot was taken at the end of a previous sema_lower call;
    // those holders are still pinned in `asts` (caller preserves the
    // vector across invocations).
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        // M6.1: delta mode — already processed in a prior call.
        if (ai < delta_start_idx_) continue;
        // M5 step 3b+5: skip ONLY cached binary holders. User asts must
        // re-walk because (a) they may be processed under different
        // SemaOptions (metaprog_mode vs strict) and (b) skipping the
        // final strict pass loses validation errors that should fire.
        {
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin &&
                collected_holders_.count(asts[ai].holder())) {
                // containers_ is per-SemaChecker state the cache does NOT
                // carry — a cache-skipped binary module must still register
                // its container declarations (typeof over a library-declared
                // container resolves through containers_ at entry-body time).
                register_container_decls(asts[ai].root_object().as_tiny_map());
                continue;
            }
        }
        cur_ast_idx_ = ai;
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        cur_from_binary_ = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
        cur_from_lazy_   = (is_lazy_     && ai < is_lazy_->size())     ? (*is_lazy_)[ai]     : false;
        cur_module_id_   = (module_ids_  && ai < module_ids_->size())  ? (*module_ids_)[ai] : std::string{};
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        if (!cur_package_.empty() && !cur_module_id_.empty())
            pkg_module_ids_[cur_package_] = cur_module_id_;
        cur_imports_ = build_import_scope(root);
        maybe_inject_implicit_prelude(root, cur_from_binary_);
        collect_module(root, 2);
    }
    // Second pass: fill in fields, variants, function signatures (Phase 1).
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        // M6.1: delta mode — already processed in a prior call.
        if (ai < delta_start_idx_) continue;
        // M5 step 3b+5: skip ONLY cached binary holders. User asts must
        // re-walk because (a) they may be processed under different
        // SemaOptions (metaprog_mode vs strict) and (b) skipping the
        // final strict pass loses validation errors that should fire.
        {
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin &&
                collected_holders_.count(asts[ai].holder())) continue;
        }
        cur_ast_idx_ = ai;
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        cur_from_binary_ = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
        cur_from_lazy_   = (is_lazy_     && ai < is_lazy_->size())     ? (*is_lazy_)[ai]     : false;
        cur_module_id_   = (module_ids_  && ai < module_ids_->size())  ? (*module_ids_)[ai] : std::string{};
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        if (!cur_package_.empty() && !cur_module_id_.empty())
            pkg_module_ids_[cur_package_] = cur_module_id_;
        cur_imports_ = build_import_scope(root);
        maybe_inject_implicit_prelude(root, cur_from_binary_);
        collect_module(root, 1);
    }
    cur_package_ = {};

    // Sprint 1.2: detect recursive by-value cycles in struct/enum graph
    // (closes B-it-01 P0 SEGFAULT in mlir_gen register_struct, B-it-02 latent).
    check_recursive_value_types();

    // Catalog-sweep: validate trait bounds at definition site (closes
    // B-gn-03 unknown trait, B-gn-04 bound arity).
    check_trait_bounds_well_formed();

    // Catalog-sweep: warn on unused type-params in fn signatures
    // (closes B-gn-07).
    check_unused_generics_in_funcs();

    // Phase 7 slice 12: scan top-level items in user (non-binary) asts
    // for annotations whose name matches a registered metaprog handler
    // trigger; record (ast_idx, offset, trigger) targets for the driver.
    // Done after phase 1 so the handler registry is complete.
    if (!metaprog_handlers_.empty()) {
        std::set<std::string> trigger_names;
        for (const auto& mh : metaprog_handlers_)
            if (mh.trigger != "<missing>") trigger_names.insert(mh.trigger);
        for (size_t ai = 0; ai < asts.size(); ++ai) {
            // M6.1: delta mode — already processed in a prior call.
            if (ai < delta_start_idx_) continue;
            bool is_bin = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
            if (is_bin) continue;
            // M5 step 3b: skip user ASTs already collected in a prior
            // sema_lower call — their metaprog_targets contributions are
            // already in the snapshot we installed at run() top.
            // M5 step 3b+5: skip ONLY cached binary holders. User asts must
        // re-walk because (a) they may be processed under different
        // SemaOptions (metaprog_mode vs strict) and (b) skipping the
        // final strict pass loses validation errors that should fire.
        {
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin &&
                collected_holders_.count(asts[ai].holder())) continue;
        }
            // Bind helpers (arr_of/map_of/code_of/str_of) to this ast's holder.
            holder_ = asts[ai].holder();
            auto root = asts[ai].root_object().as_tiny_map();
            if (!root.has_key(la::ITEMS)) continue;
            auto items = arr_of(root.get(la::ITEMS.code));
            std::vector<std::string> pending;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t ic = code_of(item);
                if (ic == la::ANNOTATION) {
                    auto aname = std::string(str_of(item.get(la::NAME.code)));
                    if (trigger_names.count(aname)) pending.push_back(std::move(aname));
                    continue;
                }
                for (auto& trig : pending)
                    metaprog_targets_.push_back({
                        ai,
                        static_cast<uint32_t>(item.offset().value()),
                        trig
                    });
                pending.clear();
            }
        }
    }

    // B-at-01: cross-module unknown-annotation warning. Now that the full
    // trigger registry AND all annotation-type datatypes are known, walk
    // every top-level user-AST `#[name]` and warn if `name` is neither a
    // builtin attribute, a registered metaprog-handler trigger, nor an
    // `#[annotation]` datatype.
    {
        using namespace sema_detail;
        std::set<std::string> trigger_names;
        for (const auto& mh : metaprog_handlers_)
            if (mh.trigger != "<missing>") trigger_names.insert(mh.trigger);
        std::set<std::string> annotation_type_names;
        for (auto& [k, dt] : datatypes_)
            if (dt.is_annotation_type) {
                // Key is "<pkg>::<name>" or just "<name>"; strip pkg prefix.
                auto colon = k.rfind("::");
                annotation_type_names.insert(
                    colon == std::string::npos ? k : k.substr(colon + 2));
            }
        for (size_t ai = 0; ai < asts.size(); ++ai) {
            // M6.1: delta mode — already processed in a prior call.
            if (ai < delta_start_idx_) continue;
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin) continue;
            // M5 step 3b: skip cached user ASTs (their unknown-attr diags
            // were emitted by the original sema_lower call).
            // M5 step 3b+5: skip ONLY cached binary holders. User asts must
        // re-walk because (a) they may be processed under different
        // SemaOptions (metaprog_mode vs strict) and (b) skipping the
        // final strict pass loses validation errors that should fire.
        {
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin &&
                collected_holders_.count(asts[ai].holder())) continue;
        }
            holder_ = asts[ai].holder();
            auto root = asts[ai].root_object().as_tiny_map();
            if (!root.has_key(la::ITEMS)) continue;
            auto items = arr_of(root.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                if (code_of(item) != la::ANNOTATION) continue;
                auto aname = std::string(str_of(item.get(la::NAME.code)));
                if (aname.empty()) continue;
                if (attr_builtin_targets(aname) != 0) continue;
                if (trigger_names.count(aname))         continue;
                if (annotation_type_names.count(aname)) continue;
                node_line_ = get_line(item);
                ctx_.clear();
                warn(std::format(
                    "unknown attribute '#[{}]' — not a builtin, not a "
                    "registered metaprog-handler trigger, and not an "
                    "`#[annotation]` datatype. Typo, missing import, or "
                    "removed?",
                    aname));
            }
        }
    }

    // Final pass: simplify all collected types (resolve concrete associated types).
    simplify_all_types();

    // Deferred check: verify supertrait impls are satisfied for all trait impls.
    // Deferred because impl Foo for T and impl Super for T may appear in any order.
    check_supertrait_impls();

    // G3-tg-03: structural auto-Copy. After manual `impl Copy` entries are
    // collected, walk structs_ once more and promote any non-Drop struct
    // whose every field is itself Copy. Closes the most surprising Logos-
    // vs-Rust divergence for porting scalar-only structs.
    compute_auto_copy_types();

    // M5 step 3b+5: record only BINARY holders. User holders intentionally
    // skipped — user ASTs must re-walk every call so strict-mode validation
    // fires for them (e.g. fail-tests expecting "unknown type" diags) and
    // mutation-prone tables like module_consts_/funcs_ don't grow stale
    // user entries that would conflict on re-add.
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        // M6.1: don't reset delta_start tracking for already-processed
        // asts (their holders are already in collected_holders_).
        if (ai < delta_start_idx_) continue;
        bool is_bin = (from_binary_ && ai < from_binary_->size())
                      ? (*from_binary_)[ai] : false;
        if (is_bin) {
            collected_holders_.insert(asts[ai].holder());
        } else if (cache_ && cache_->keep_user_state()) {
            // M6.1: in delta mode, also pin user holders so subsequent
            // sema_lower calls skip them in collect (same as binary).
            // user_holders_ marks them as user-origin for reset_user_state.
            collected_holders_.insert(asts[ai].holder());
            user_holders_.insert(asts[ai].holder());
        }
    }
}

void SemaChecker::simplify_all_types() {
    for (auto& [name, info] : structs_) {
        for (auto& f : info.fields) f.type = subst_type_sema(f.type, {});
    }
    for (auto& [name, info] : enums_) {
        for (auto& v : info.variants) {
            for (auto& pt : v.payload_types) pt = subst_type_sema(pt, {});
        }
    }
    auto simplify_fn = [&](SemaFuncInfo& fi) {
        for (auto& pt : fi.param_types) pt = subst_type_sema(pt, {});
        fi.ret_type = subst_type_sema(fi.ret_type, {});
    };
    for (auto& [name, info] : funcs_) simplify_fn(info);
    for (auto& [name, info] : generic_funcs_) simplify_fn(info);
    for (auto& [name, entry] : type_aliases_)
        entry.type = subst_type_sema(entry.type, {});
    for (auto& [name, t] : module_consts_)
        t = subst_type_sema(t, {});
    for (auto& [key, entry] : assoc_const_impls_)
        if (entry.type) entry.type = subst_type_sema(entry.type, {});
    // Bug 1 fix: simplify assoc type impl body types as well.
    // Non-generic body types (e.g. type Inner<T> = i32) get their concrete types
    // resolved; types containing TypeVars are left unchanged for later substitution.
    for (auto& [key, entry] : assoc_type_impls_)
        if (entry.type) entry.type = subst_type_sema(entry.type, {});
}

std::string SemaChecker::read_package_name(TinyMapView mod) {
    if (!mod.has_key(la::NAME)) return {};
    std::string name(str_of(mod.get(la::NAME.code)));
    if (mod.has_key(la::mod::PATH_PARTS)) {
        auto parts = arr_of(mod.get(la::mod::PATH_PARTS.code));
        for (uint64_t i = 0; i < parts.size(); ++i) {
            auto part = map_of(parts.get(i));
            if (!part.has_key(la::NAME)) continue;
            name += ".";
            name += std::string(str_of(part.get(la::NAME.code)));
        }
    }
    return name;
}

void SemaChecker::check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name,
                          bool is_module_only, const std::string& def_module_id) {
    if (def_package.empty() || cur_package_.empty()) return;  // no scope context
    if (def_package == cur_package_) return;                  // own package: always OK
    // §4: cross-package access. `pub(module)` has module-linkage — visible to other
    // packages in its OWNING module, but not to a consumer in a different module.
    // Checked BEFORE is_pub, since a pub(module) item also sets is_pub.
    if (is_module_only) {
        if (def_module_id != cur_module_id_)
            error(std::format("'{}' is module-private (declared `pub(module)` in "
                              "package '{}')", item_name, def_package));
        return;
    }
    if (!is_pub)
        error(std::format("'{}' is private to package '{}'", item_name, def_package));
}

bool SemaChecker::sema_has_impl_recursive(const std::string& trait_name,
                                          const std::string& concrete,
                                          const std::string& concrete_alt,
                                          logos::compiler::StrSet& seen) {
    std::string k = trait_name + "::" + concrete;
    if (!seen.insert(k).second) return false;
    if (impls_.count(k)) return true;
    if (!concrete_alt.empty()) {
        std::string ka = trait_name + "::" + concrete_alt;
        if (impls_.count(ka)) return true;
    }
    // Reference Self: `impl Trait for &T` / `&mut T` registers under collect_impl's
    // `$ref_`/`$mut_ref_` mangling, but `concrete` here is the raw type_str
    // (`&i32` / `&mut Foo`). Try both forms — primitive pointee keeps the full
    // string (`$ref_&i32`), struct pointee uses the bare name (`$ref_Foo`). This
    // is the SHARED satisfaction primitive (default-method where-gate, blanket
    // recursion, etc.), so every site that asks "does `&T` impl Trait" benefits.
    for (auto& pfx : {std::string("&mut "), std::string("&")}) {
        if (concrete.rfind(pfx, 0) != 0) continue;
        std::string mpfx = (pfx == "&mut ") ? "$mut_ref_" : "$ref_";
        if (impls_.count(trait_name + "::" + mpfx + concrete)) return true;
        if (impls_.count(trait_name + "::" + mpfx + concrete.substr(pfx.size())))
            return true;
        break;
    }
    for (auto& bi : blanket_impls_) {
        if (bi.trait_name != trait_name) continue;
        if (bi.bound_trait.empty() && bi.extra_bounds.empty()) return true;
        // Per-attempt copy: a failed candidate must not poison `seen`
        // for the next sibling candidate.
        logos::compiler::StrSet attempt = seen;
        bool ok = bi.bound_trait.empty()
            || sema_has_impl_recursive(bi.bound_trait, concrete, concrete_alt, attempt);
        if (ok) {
            for (auto& eb : bi.extra_bounds)
                if (!sema_has_impl_recursive(eb, concrete, concrete_alt, attempt)) {
                    ok = false; break;
                }
        }
        if (ok) return true;
    }
    return false;
}

void SemaChecker::check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<TypeRef>& args) {
    if (type_params.empty()) return;
    bool has_variadic = type_params.back().is_variadic;
    size_t non_variadic_count = type_params.size() - (has_variadic ? 1 : 0);

    // Map THIS call's type-params to their concrete args. A parametrized bound
    // `I: Iterator<T>` whose type-arg is ANOTHER of the fn's type-params (`T`)
    // must be checked against T's actual value (e.g. turbofish `T=i32`), not
    // the bare TypeVar — else it defers to mono where a layout mismatch
    // silently miscompiles (`iter_take::<SliceIter<i32>, i32>`: SliceIter impls
    // Iterator<&i32>, body wants Iterator<i32> → niche/tagged Option mismatch →
    // runtime infinite loop). Substituting closes the deferral so the mismatch
    // is a compile error here.
    SemaSubst call_subst;
    for (size_t j = 0; j < type_params.size() && j < args.size(); ++j)
        if (args[j]) call_subst[type_params[j].name] = args[j];

    for (size_t i = 0; i < args.size(); ++i) {
        if (i >= type_params.size() && !has_variadic) break;

        const auto& tp = (has_variadic && i >= non_variadic_count)
                         ? type_params.back()
                         : type_params[i];

        auto concrete = args[i];
        if (!concrete) continue;
        TypeRef cv{concrete};
        if (cv.kind() == LogosType::Kind::Error) continue;
        // A type-expression that still MENTIONS a TypeVar anywhere (`&T`,
        // `[T;0]`, `&[T]`, `EnumPair<T>`, `(T,U)`, …) is not decidable here:
        // its trait-satisfaction depends on what the TypeVar becomes after
        // monomorphisation. Defer to mono — where the per-method
        // `where_type_bounds` gate (and impl-type-param bounds) re-check the
        // SUBSTITUTED form. The old test only deferred a *bare* top-level
        // TypeVar, so a default-method body like `fn max() where Item: Ord`
        // synthesised for `impl<T> Iterator<&T> for VecIter<T>` couldn't
        // assume `&T: Ord` and wrongly errored at the `iter_max::<_,&T>`
        // call. Generalises the assume-the-where-clause mechanism from bare
        // `T` to any TypeVar-bearing subject. (Struct/enum kinds also fell
        // through to error when the generic itself had no impl, e.g.
        // `EnumPair<T>: Ord` — same class, same cure.)
        {
            std::function<bool(TypeRef)> mentions_tv = [&](TypeRef t) -> bool {
                if (!t) return false;
                if (t.kind() == LogosType::Kind::TypeVar) return true;
                if (t.pointee() && mentions_tv(t.pointee())) return true;
                if (t.elem()    && mentions_tv(t.elem()))    return true;
                for (auto a : t.type_args())     if (mentions_tv(a)) return true;
                for (auto e : t.tuple_elems())   if (mentions_tv(e)) return true;
                for (auto p : t.closure_params())if (mentions_tv(p)) return true;
                if (t.closure_ret() && mentions_tv(t.closure_ret())) return true;
                return false;
            };
            if (mentions_tv(cv)) continue;   // defer until mono
        }
        if (cv.kind() == LogosType::Kind::AssocType) continue; // deferred (bounds checked via trait decl)
        if (cv.kind() == LogosType::Kind::CfgSlotType) continue; // deferred — concrete type known after CFG substitution

        std::string concrete_str = type_str(concrete);
        std::string unwrapped_name;
        if ((cv.kind() == LogosType::Kind::Ptr || cv.kind() == LogosType::Kind::Ref || cv.kind() == LogosType::Kind::MutRef) && cv.pointee()) {
            TypeRef iv = cv.pointee();
            if (iv.kind() == LogosType::Kind::Struct)
                unwrapped_name = concrete_struct_name(iv);
        } else if (cv.kind() == LogosType::Kind::Struct) {
            unwrapped_name = concrete_struct_name(concrete);
        }

        for (auto& bound : tp.bounds) {
            // M7-mt-03: `Sized` is a compiler-builtin marker. Logos has no
            // unsized types yet, so every concrete type satisfies it; the
            // bound is admitted as a no-op (matches `T: Sized` being
            // implicit in Rust). `?Sized` opt-out isn't expressible yet.
            if (bound.trait_name == "Sized") continue;
            // `Copy` is built-in for the bitwise-copyable handle kinds: a shared
            // reference `&T` (incl. `&dyn Trait`), a raw pointer `*const/*mut T`,
            // a slice `&[T]`, a fn pointer, and a trait-object fat pointer — none
            // need an explicit `impl Copy`. `&mut T` is an exclusive (move-only)
            // borrow and is NOT Copy (falls through → impl lookup → error, as in
            // Rust). Concrete `impl Copy for <T>` types are handled below.
            if (bound.trait_name == "Copy") {
                auto ck = cv.kind();
                if (ck == LogosType::Kind::Ref ||
                    ck == LogosType::Kind::Ptr ||
                    ck == LogosType::Kind::Slice ||
                    LogosType::is_fn_value_kind(ck) ||
                    ck == LogosType::Kind::TraitObject)
                    continue;
            }
            // G158-6: a `where &T: Trait` bound (on_ref_subject) is satisfied by
            // an `impl Trait for &Concrete` (registered under `$ref_<C>` /
            // `$mut_ref_<C>`), NOT `impl Trait for Concrete`. Check that impl
            // key instead of the plain one below.
            if (bound.on_ref_subject) {
                std::string rk = (bound.is_ref_mut ? "$mut_ref_" : "$ref_") + concrete_str;
                if (impls_.count(bound.trait_name + "::" + rk)) continue;
                if (bounds_probe_) { bounds_probe_ok_ = false; continue; }
                error(std::format("'{}': type '{}{}' does not implement trait '{}' "
                                  "required by parameter '&{}'",
                      target_name, bound.is_ref_mut ? "&mut " : "&", concrete_str,
                      bound.trait_name, tp.name));
                continue;
            }
            // Auto trait: synthesize satisfaction from field types.
            auto trit = traits_.find(bound.trait_name);
            if (trit != traits_.end() && trit->second.is_auto) {
                StrSet visited;
                last_offender_ = {};
                if (is_auto_trait_satisfied(concrete, bound.trait_name, visited)) continue;
                if (bounds_probe_) { bounds_probe_ok_ = false; continue; }
                if (!last_offender_.field_name.empty()) {
                    error(std::format("'{}': type '{}' does not satisfy auto trait '{}' "
                                      "(field '{}' of type '{}' is not {})",
                          target_name, concrete_str, bound.trait_name,
                          last_offender_.field_name,
                          last_offender_.field_ty ? type_str(last_offender_.field_ty) : "?",
                          bound.trait_name));
                } else {
                    // Bug 5 fix: say "not inherently Send/Sync" rather than always
                    // blaming raw pointers — Closures, TraitObjects, etc. also reach here.
                    error(std::format("'{}': type '{}' does not satisfy auto trait '{}' "
                                      "(type is not inherently {})",
                          target_name, concrete_str, bound.trait_name, bound.trait_name));
                }
                continue;
            }
            auto key1 = bound.trait_name + "::" + concrete_str;
            auto key2 = unwrapped_name.empty() ? "" : bound.trait_name + "::" + unwrapped_name;
            // Parametrized bound `T: Trait<Args>`: the impls_ registry is keyed
            // `Trait::Self` and SINGLE-valued, so a Self-name hit proves only that
            // SOME `Trait` impl exists — NOT one with the right type-args (a type
            // can impl `From<i32>` AND `From<i16>`, or `Iterator<&T>` where the
            // bound wants `Iterator<i32>`). `type_args_ok` = does ANY impl for
            // this Self (enumerated via the multi-valued impls_all_) match the
            // bound's type-args, after substituting the impl's params from the
            // concrete Self? When false, the name-keyed acceptance paths below
            // (direct, generic-struct, tuple, alias) must NOT accept — only a
            // blanket can. Empty bound.type_args = no constraint (true). Closes
            // the hole where `I: Iterator<i32>` was satisfied by `Iterator<&i32>`.
            // Substitute the call's type-params into the bound's type-args so a
            // bound `Iterator<T>` is checked against T's CONCRETE value (e.g.
            // turbofish `T=i32`) rather than the bare TypeVar. Without this the
            // mtv-defer below would punt to mono and miscompile.
            std::vector<TypeRef> bound_targs;
            bound_targs.reserve(bound.type_args.size());
            for (auto& ta : bound.type_args)
                bound_targs.push_back(ta ? subst_type_sema(TypeRef(ta), call_subst)
                                         : TypeRef(ta));
            bool type_args_ok = bound_targs.empty();
            if (!type_args_ok) {
                std::function<bool(TypeRef)> mtv = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    if (t.kind() == LogosType::Kind::TypeVar) return true;
                    // A still-unbound CONST param (`impl<const N, …> Trait<…, N>
                    // for W<N, …>` matched against `W<2, …>`) is abstract in
                    // exactly the TypeVar sense — undecidable here, validated
                    // at monomorphization. Without this the concrete const arg
                    // compared against the impl's ConstVar and every
                    // const-parameterized generic impl failed its bound.
                    if (t.kind() == LogosType::Kind::ConstVar) return true;
                    if (t.pointee() && mtv(t.pointee())) return true;
                    if (t.elem() && mtv(t.elem()))       return true;
                    for (auto a : t.type_args())   if (mtv(a)) return true;
                    for (auto e : t.tuple_elems()) if (mtv(e)) return true;
                    return false;
                };
                auto matches = [&](const SemaImplInfo& info) -> bool {
                    if (info.trait_type_args.size() < bound_targs.size())
                        return false;
                    SemaSubst sub;
                    if (info.target_typeref)
                        unify_types(info.target_typeref, concrete, sub);
                    for (size_t k = 0; k < bound_targs.size(); ++k) {
                        TypeRef ba = bound_targs[k];
                        TypeRef ia = info.trait_type_args[k];
                        if (ia) ia = subst_type_sema(ia, sub);
                        if (!ba || !ia) continue;
                        // Either side still abstract → undecidable here; defer
                        // (mono re-checks the concrete instantiation).
                        if (mtv(ba) || mtv(ia)) continue;
                        if (!types_equal(ba, ia)) return false;
                    }
                    return true;
                };
                // Enumerate under key1/key2 (concrete + unwrapped names) AND the
                // BARE struct/enum name — a generic impl `impl<T> Trait<…> for
                // Foo<T>` registers under `Trait::Foo` (bare), not the mangled
                // `Trait::Foo$G1$i32`, so the generic-struct acceptance path keys
                // on the bare name; mirror that here.
                std::string key_bare;
                if (cv.kind() == LogosType::Kind::Struct ||
                    cv.kind() == LogosType::Kind::ZonedStruct) {
                    if (!cv.struct_name().empty())
                        key_bare = bound.trait_name + "::" + std::string(cv.struct_name());
                } else if (cv.kind() == LogosType::Kind::Enum) {
                    if (!cv.enum_name().empty())
                        key_bare = bound.trait_name + "::" + std::string(cv.enum_name());
                }
                for (auto* kp : {&key1, &key2, &key_bare}) {
                    if (kp->empty()) continue;
                    auto it = impls_all_.find(*kp);
                    if (it == impls_all_.end()) continue;
                    for (auto& info : it->second)
                        if (matches(info)) { type_args_ok = true; break; }
                    if (type_args_ok) break;
                }
            }
            // B62/B63: region check — bound's universally-quantified lifetimes
            // must satisfy two properties when matched against an impl's
            // trait-arg lifetimes:
            //   (a) Universal-position: each bound lifetime (non-empty, non-
            //       'static) must align with an impl lifetime that is a free
            //       impl-level param, not a concrete region pinned at the impl.
            //   (b) Impl-tie injectivity: if the impl uses the same lifetime
            //       in two trait-arg positions, the bound must use the same
            //       binder in those positions. (Forward direction is allowed
            //       to be non-injective: bound binders may collapse to the
            //       same impl lifetime — impl is strictly more general than
            //       bound. Reverse direction is enforced: impl-tied slots
            //       can't be satisfied by independent bound binders.)
            // Walks Ref/MutRef pointee + Struct/Enum type_args recursively.
            auto region_ok = [&](const SemaImplInfo& info) {
                if (bound.type_args.empty() || info.trait_type_args.empty())
                    return true;
                std::unordered_map<std::string, std::string> impl_to_skolem;
                auto univ_at_impl = [&](const std::string& lt) {
                    for (auto& nm : info.impl_lifetime_params)
                        if (nm == lt) return true;
                    return false;
                };
                auto unify = [&](const std::string& blt, const std::string& ilt) -> bool {
                    // LIFETIME terminal includes the leading apostrophe; check
                    // both forms defensively.
                    if (blt.empty() || blt == "static" || blt == "'static") {
                        // Bound concrete; impl must match exactly OR be a free
                        // impl-level param (which is more general than static).
                        if (ilt == blt) return true;
                        if (univ_at_impl(ilt)) return true;
                        // 'static at bound, impl is anything else concrete: reject.
                        return false;
                    }
                    if (!univ_at_impl(ilt)) return false;
                    auto br2 = impl_to_skolem.emplace(ilt, blt);
                    if (!br2.second && br2.first->second != blt) return false;
                    return true;
                };
                std::function<bool(TypeRef, TypeRef)> walk =
                    [&](TypeRef bt, TypeRef it) -> bool {
                    if (!bt || !it) return true;
                    bool br = bt.kind() == LogosType::Kind::Ref ||
                              bt.kind() == LogosType::Kind::MutRef;
                    bool ir = it.kind() == LogosType::Kind::Ref ||
                              it.kind() == LogosType::Kind::MutRef;
                    if (br && ir) {
                        if (!unify(std::string(bt.lifetime()),
                                   std::string(it.lifetime()))) return false;
                        return walk(bt.pointee(), it.pointee());
                    }
                    if (bt.kind() != it.kind()) return true;
                    if (bt.kind() == LogosType::Kind::Struct ||
                        bt.kind() == LogosType::Kind::ZonedStruct ||
                        bt.kind() == LogosType::Kind::Enum) {
                        auto blts = bt.lifetime_args();
                        auto ilts = it.lifetime_args();
                        size_t nl = std::min(blts.size(), ilts.size());
                        for (size_t i = 0; i < nl; ++i)
                            if (!unify(blts[i], ilts[i])) return false;
                        auto bts = bt.type_args();
                        auto its = it.type_args();
                        size_t nt = std::min(bts.size(), its.size());
                        for (size_t i = 0; i < nt; ++i)
                            if (!walk(bts[i], its[i])) return false;
                    }
                    return true;
                };
                size_t n = std::min(bound.type_args.size(),
                                    info.trait_type_args.size());
                for (size_t i = 0; i < n; ++i) {
                    if (!walk(TypeRef(bound.type_args[i]),
                              TypeRef(info.trait_type_args[i])))
                        return false;
                }
                // B85: HRTB skolemization — if the impl declares a where-clause
                // outlives `'a: 'b` between two impl-side lifetime params that
                // BOTH map to bound binders (skolems), the constraint is
                // unsatisfiable under universal quantification. Reject.
                auto is_binder_lt = [&](const std::string& lt) -> bool {
                    for (auto& b : bound.hrtb_binders)
                        if (b == lt) return true;
                    return false;
                };
                for (auto& [longi, shorti] : info.impl_lifetime_outlives) {
                    auto lit = impl_to_skolem.find(longi);
                    auto sit = impl_to_skolem.find(shorti);
                    if (lit == impl_to_skolem.end() || sit == impl_to_skolem.end()) continue;
                    if (lit->second == sit->second) continue;  // reflexive
                    if (is_binder_lt(lit->second) && is_binder_lt(sit->second))
                        return false;
                }
                return true;
            };
            {
                const SemaImplInfo* found = nullptr;
                auto i1 = impls_.find(key1);
                if (i1 != impls_.end()) found = &i1->second;
                else if (!key2.empty()) {
                    auto i2 = impls_.find(key2);
                    if (i2 != impls_.end()) found = &i2->second;
                }
                // SL-sl-02: `T: PartialEq` / `T: PartialOrd` accepted via
                // existing `Eq` / `Ord` impls. Rust's hierarchy is
                // `Eq: PartialEq` (every Eq is a PartialEq); Logos's `Eq`
                // currently carries the methods Rust puts on PartialEq.
                // Until the full split happens (separate Logos trait
                // hierarchy + per-impl migration), bound-resolution
                // treats Eq-impls as PartialEq satisfiers.
                if (!found) {
                    std::string alias;
                    if (bound.trait_name == "PartialEq")    alias = "Eq";
                    else if (bound.trait_name == "PartialOrd") alias = "Ord";
                    if (!alias.empty()) {
                        auto ak1 = alias + "::" + concrete_str;
                        auto i1a = impls_.find(ak1);
                        if (i1a != impls_.end()) found = &i1a->second;
                        else if (!unwrapped_name.empty()) {
                            auto ak2 = alias + "::" + unwrapped_name;
                            auto i2a = impls_.find(ak2);
                            if (i2a != impls_.end()) found = &i2a->second;
                        }
                    }
                }
                if (found && type_args_ok) {
                    if (region_ok(*found)) continue;
                    std::string binders_str;
                    if (!bound.hrtb_binders.empty()) {
                        binders_str = " for<";
                        for (size_t k = 0; k < bound.hrtb_binders.size(); ++k) {
                            if (k) binders_str += ", ";
                            // LIFETIME terminal already includes the apostrophe.
                            binders_str += bound.hrtb_binders[k];
                        }
                        binders_str += ">";
                    }
                    if (bounds_probe_) { bounds_probe_ok_ = false; continue; }
                    error(std::format(
                        "'{}': type '{}' does not satisfy `{}{}` bound: "
                        "impl's trait-arg lifetimes are incompatible with "
                        "the bound's quantification (either impl pins to a "
                        "concrete region, or independent binders collapse to "
                        "a single impl param)",
                        target_name, concrete_str, bound.trait_name,
                        binders_str));
                    continue;
                }
            }
            // Blanket-impl satisfaction: a `impl<T: X> bound.trait for T`
            // makes every `T: X` automatically implement the bound trait.
            bool via_blanket = false;
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != bound.trait_name) continue;
                auto bound_satisfied = [&](const std::string& bt) {
                    // A blanket's own bound may be an AUTO trait (Fst/Send/…) —
                    // the string-recursive impl lookup can't see structural
                    // satisfaction, so consult the auto engine first.
                    auto tit = traits_.find(bt);
                    if (tit != traits_.end() && tit->second.is_auto) {
                        logos::compiler::StrSet av;
                        return is_auto_trait_satisfied(concrete, bt, av);
                    }
                    logos::compiler::StrSet seen;
                    return sema_has_impl_recursive(bt, concrete_str, unwrapped_name, seen);
                };
                // Unbounded blanket (`impl<T> Trait for T {}`) trivially satisfies
                // every concrete type. Bounded blanket: primary + all extras must hold.
                if (!bi.bound_trait.empty() && !bound_satisfied(bi.bound_trait)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds)
                    if (!bound_satisfied(eb)) { all_extra = false; break; }
                if (!all_extra) continue;
                // ADR 0008: assoc-type equality clauses must hold.
                if (!assoc_eqs_satisfied(bi.bound_trait, concrete_str,
                                          unwrapped_name, bi.primary_assoc_eqs)) continue;
                bool extra_eqs_ok = true;
                for (auto& [trait, eqs] : bi.extra_assoc_eqs)
                    if (!assoc_eqs_satisfied(trait, concrete_str,
                                              unwrapped_name, eqs)) { extra_eqs_ok = false; break; }
                if (extra_eqs_ok) { via_blanket = true; break; }
            }
            if (via_blanket) continue;
            // Generic-struct impl: `impl<T: X> Trait for GenericStruct<T>`
            // registers under the base name ("GenericStruct").  Accept the
            // bound satisfaction if a generic impl for the concrete's base
            // struct exists; the impl's own type-param bounds are validated
            // at monomorphization time by recursive check_type_bounds.
            if ((cv.kind() == LogosType::Kind::Struct ||
                 cv.kind() == LogosType::Kind::ZonedStruct) &&
                !cv.struct_name().empty()) {
                auto key3 = bound.trait_name + "::" + std::string(cv.struct_name());
                if (type_args_ok && impls_.count(key3)) continue;
            }
            // Slice-impl bound satisfaction (the Sized-partition pattern):
            // `impl<E: …> Trait for [E]` registers under `$slice$T` (concrete
            // elem impls under `$slice$<elem>`). A concrete [u8] satisfies
            // the bound through either key; the impl's own element bounds
            // are validated at monomorphization like the generic-struct and
            // tuple paths below.
            if ((cv.kind() == LogosType::Kind::Slice ||
                 cv.kind() == LogosType::Kind::UnsizedSlice) && type_args_ok) {
                TypeRef selem = cv.elem();
                std::string ekey = bound.trait_name + "::$slice$"
                    + (selem ? type_str(selem) : std::string("?"));
                if (impls_.count(ekey)) continue;
                if (impls_.count(bound.trait_name + "::$slice$T")) continue;
            }
            // Slice-impl bound satisfaction (the Sized-partition pattern):
            // `impl<E: …> Trait for [E]` registers under `$slice$T` (concrete
            // elem impls under `$slice$<elem>`). A concrete [u8] satisfies
            // the bound through either key; the impl's own element bounds
            // are validated at monomorphization like the generic-struct and
            // tuple paths below.
            if ((cv.kind() == LogosType::Kind::Slice ||
                 cv.kind() == LogosType::Kind::UnsizedSlice) && type_args_ok) {
                TypeRef selem = cv.elem();
                std::string ekey = bound.trait_name + "::$slice$"
                    + (selem ? type_str(selem) : std::string("?"));
                if (impls_.count(ekey)) continue;
                if (impls_.count(bound.trait_name + "::$slice$T")) continue;
            }
            // SL-sl-08 follow-up: tuple-impl bound satisfaction. Tuples
            // are registered under `$tuple$N` (generic, mirrors the
            // `$tuple$N$<t1>$<t2>…` concrete form). Recognise both.
            // The element-level bounds (A: X for elem 0, B: X for elem 1)
            // are checked recursively below by the same machinery as
            // generic-struct impls — at monomorphisation time.
            // Variadic form `impl<A...> Trait for (A...)` registers under
            // `$tuple$variadic` — accept any tuple arity.
            if (cv.kind() == LogosType::Kind::Tuple && type_args_ok) {
                auto key_variadic = bound.trait_name + "::$tuple$variadic";
                if (impls_.count(key_variadic)) continue;
                size_t arity = cv.tuple_elems().size();
                auto key_arity = bound.trait_name + "::$tuple$"
                                 + std::to_string(arity);
                if (impls_.count(key_arity)) {
                    // Recursive bound check: every element must itself
                    // implement bound.trait (matches the impl's
                    // `<A: trait, B: trait, …>` qualifiers).
                    bool all_elems_ok = true;
                    for (auto e : cv.tuple_elems()) {
                        if (!e) continue;
                        // Skip TypeVar elements — checked at mono time.
                        if (TypeRef(e).kind() == LogosType::Kind::TypeVar)
                            continue;
                        std::vector<TypeRef> rec_args{e};
                        // Re-enter check_type_bounds; on failure it
                        // emits a separate diagnostic, but we want to
                        // suppress that and just record failure here.
                        // Simpler: peek by reusing the same key-lookup
                        // logic against the element type.
                        std::string e_str = type_str(e);
                        std::string ek1 = bound.trait_name + "::" + e_str;
                        if (impls_.count(ek1)) continue;
                        // Tuple element is itself a tuple → arity key.
                        if (TypeRef(e).kind() == LogosType::Kind::Tuple) {
                            size_t a2 = TypeRef(e).tuple_elems().size();
                            if (impls_.count(bound.trait_name + "::$tuple$"
                                             + std::to_string(a2)))
                                continue;
                        }
                        // Auto trait short-circuit.
                        auto tit2 = traits_.find(bound.trait_name);
                        if (tit2 != traits_.end() && tit2->second.is_auto) {
                            StrSet visited;
                            if (is_auto_trait_satisfied(e, bound.trait_name, visited))
                                continue;
                        }
                        all_elems_ok = false;
                        break;
                    }
                    if (all_elems_ok) continue;
                }
            }
            // Sprint 5.7c: Fn-family bound (`F: FnOnce(args) -> R`)
            // satisfied by any closure or fn-pointer type. Arity /
            // arg-type / ret-type compatibility is enforced at the
            // call site (lower_call synthesises a callable type from
            // the bound). When F resolves to FnPtr at mono time, the
            // ClosureCall LIR op rewrites to FnPtrCall in mono_clone.
            if (bound.is_fn_family && (cv.kind() == LogosType::Kind::Closure ||
                                       LogosType::is_fn_value_kind(cv.kind()))) {
                // Fn-family kind check (Rust E0525): a closure that MUTATES a
                // capture doesn't implement `Fn`; one that MOVES OUT a capture
                // implements only `FnOnce`. Fn pointers capture nothing → always
                // Fn-kind. An unrecorded closure is read-only (Fn). Required
                // levels: Fn=0, FnMut=1, FnOnce=2; the closure's inferred kind
                // must not exceed it.
                if (cv.kind() == LogosType::Kind::Closure) {
                    int req = (bound.trait_name == "Fn")    ? 0
                            : (bound.trait_name == "FnMut") ? 1 : 2;
                    auto kit = closure_kind_.find(type_str(cv));
                    int ck = (kit == closure_kind_.end()) ? 0 : kit->second;
                    if (ck > req)
                        error(std::format(
                            "closure does not implement `{}`: its body {} a "
                            "captured variable, so it is `{}`",
                            bound.trait_name,
                            ck == 2 ? "moves out (consumes)" : "mutates",
                            ck == 2 ? "FnOnce" : "FnMut"));
                }
                continue;
            }
            // G158-1: `&F` / `&mut F` satisfies an Fn-family bound when the
            // pointee is itself callable (a closure / fn-ptr, or a TypeVar
            // bounded by Fn that resolves to one at mono). Rust's blanket
            // `impl<F: Fn> Fn for &F`. The call through such a reference
            // autoderef-invokes (see lower_call's fn_bound_via_ref path).
            if (bound.is_fn_family &&
                (cv.kind() == LogosType::Kind::Ref ||
                 cv.kind() == LogosType::Kind::MutRef) &&
                cv.pointee() &&
                (TypeRef(cv.pointee()).kind() == LogosType::Kind::Closure ||
                 LogosType::is_fn_value_kind(TypeRef(cv.pointee()).kind()) ||
                 TypeRef(cv.pointee()).kind() == LogosType::Kind::TypeVar))
                continue;
            // G149-6: `impl<A,B,C> Trait for fn(A,B)->C` registers under
            // `$fnptr$N`; a concrete fn-pointer satisfies the bound by arity.
            if (LogosType::is_fn_value_kind(cv.kind()) &&
                impls_.count(bound.trait_name + "::$fnptr$" +
                             std::to_string(cv.closure_params().size())))
                continue;
            // G158-7: a `dyn Trait` trait object satisfies a `T: Trait` bound —
            // a trait object implements its own trait (Rust's auto rule) + any
            // supertrait. Enables the `?Sized` generic passthrough
            // `tick_generic<C: ?Sized + Counter>(c: &mut C)` invoked with a
            // `&mut dyn Counter`. The downstream method dispatch (`c.tick()` on a
            // `&mut C` that monomorphises to a trait object) is wired in
            // mono_clone (vtable-slot resolution) + mlir-gen (ref-wrapped
            // TraitObject dispatch).
            if ((cv.kind() == LogosType::Kind::TraitObject ||
                 cv.kind() == LogosType::Kind::UnsizedDyn) &&
                !cv.trait_name().empty()) {
                logos::compiler::StrSet seen;
                std::function<bool(const std::string&)> reaches =
                    [&](const std::string& tn) -> bool {
                        if (!seen.insert(tn).second) return false;
                        if (tn == bound.trait_name) return true;
                        auto it = traits_.find(tn);
                        if (it == traits_.end()) return false;
                        for (auto& s : it->second.supertraits)
                            if (reaches(s.trait_name)) return true;
                        return false;
                    };
                if (reaches(std::string(cv.trait_name()))) continue;
            }
            // `impl Trait for &T` / `&mut T` — a reference Self type. collect_impl
            // registers these under `$ref_`/`$mut_ref_` mangled keys (symbol-safe:
            // no `&` for struct pointees), but the primary key1 above used the raw
            // type_str (`&i32`). Recompute the SAME mangling so a `T: Trait` bound
            // with T = `&Concrete` is satisfied. General — covers every
            // `impl Trait for &ConcreteType` (e.g. `Ord for &i32`, the by-ref
            // iterator `.max()`/`.min()` path), mirroring collect_impl's target
            // mangling (struct pointee → `$ref_<Name>`; else → `$ref_<type_str>`).
            if (cv.kind() == LogosType::Kind::Ref ||
                cv.kind() == LogosType::Kind::MutRef) {
                std::string pfx = (cv.kind() == LogosType::Kind::MutRef)
                                      ? "$mut_ref_" : "$ref_";
                TypeRef pt = cv.pointee();
                std::string mangled =
                    (pt && (TypeRef(pt).kind() == LogosType::Kind::Struct ||
                            TypeRef(pt).kind() == LogosType::Kind::ZonedStruct))
                        ? pfx + concrete_struct_name(pt)
                        : pfx + concrete_str;
                if (impls_.count(bound.trait_name + "::" + mangled)) continue;
            }
            // ADR 0021 Phase 4a: a factory-backed metaclass marker's trait
            // impl is GENERATED by the mono→factory drain, which runs after
            // this sema round. Treat the bound as satisfiable-later: record a
            // REQUIRED factory demand (non-probe mode) and defer — the post-
            // drain re-sema re-checks strictly (the impl exists by then; a
            // still-deferring program is escalated by the driver).
            if (factory_backed_marker_hash(concrete)) {
                if (!bounds_probe_) defer_factory_backed(concrete);
                continue;
            }
            if (bounds_probe_) { bounds_probe_ok_ = false; continue; }
            error(std::format("'{}': type '{}' does not implement trait '{}' required by parameter '{}'",
                  target_name, concrete_str, bound.trait_name, tp.name));
        }
    }
}

// ADR 0020/0021: register `container` declarations (DEF and DONE) into
// containers_ from one module root. Split out of collect_module phase 2 so
// CACHE-SKIPPED binary holders still register: containers_ is per-SemaChecker
// state NOT carried by the sema cache, and `typeof(Map::<...>)` (the ADR 0021
// declaration→type bridge) resolves through this registry at entry-body
// lowering — before the lower_module_items pre-scan reaches the binary module.
void SemaChecker::register_container_decls(TinyMapView mod) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto it = map_of(items.get(i));
        int32_t mc = code_of(it);
        if (mc != la::CONTAINER_DEF && mc != la::CONTAINER_DEF_DONE) continue;
        ContainerInfo ci; std::string cerr;
        if (reconstruct_container_def(it, ci, cerr)) {
            ci.pending = (mc == la::CONTAINER_DEF);
            std::string ckey =
                (ci.package.empty() ? "" : ci.package + ".") + ci.name;
            containers_[ckey] = std::move(ci);
        }
    }
}

void SemaChecker::collect_module(TinyMapView mod, int phase) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));

    // ADR 0020 wave-0 (S2): register `container` declarations into containers_
    // in the EARLIEST collect phase, so a generic container used in a fn
    // SIGNATURE (`fn f(m: &mut Map<u64,str>)`, resolved during phase-1 signature
    // collection below) is recognised by the type resolver's generic-container
    // hook. Idempotent with the lower_module_items pre-scan; reconstruct returns
    // an err string WITHOUT emitting, so a malformed decl is diagnosed there,
    // not double-reported here.
    if (phase == 2) register_container_decls(mod);

    // §6.7: flatten `extern "ABI" { extern_fn_def* }` blocks into a
    // linear worklist. Each block's child EXTERN_FN entries inherit
    // the block's ABI string when they don't carry their own. This
    // lets the rest of item collection treat extern fns identically
    // whether they came in flat or grouped.
    std::vector<TinyMapView> flat_items;
    flat_items.reserve(items.size());
    auto validate_abi = [&](std::string_view raw, writ::TinyMapView at_node) {
        // Strip optional enclosing quotes.
        std::string_view s = raw;
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
        if (s != "C" && s != "C-unwind" && s != "system" && s != "Rust") {
            node_line_ = get_line(at_node);
            error(std::format(
                "unsupported ABI string \"{}\" — expected one of "
                "\"C\", \"C-unwind\", \"system\", \"Rust\"", s));
        }
    };
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto it = map_of(items.get(i));
        if (code_of(it) == la::EXTERN_BLOCK) {
            std::string_view block_abi;
            if (it.has_key(la::VALUE)) {
                block_abi = str_of(it.get(la::VALUE.code));
                validate_abi(block_abi, it);
            }
            if (it.has_key(la::ITEMS)) {
                auto block_items = arr_of(it.get(la::ITEMS.code));
                for (uint64_t j = 0; j < block_items.size(); ++j) {
                    auto child = map_of(block_items.get(j));
                    // Per-fn ABI override (parsed onto child.VALUE)
                    // takes precedence over the block's; otherwise
                    // the child inherits via the block's VALUE. Both
                    // forms parse onto the same VALUE slot — child
                    // has its own value if present.
                    if (!child.has_key(la::VALUE) && !block_abi.empty()) {
                        // No mutation needed; we just track the inherited
                        // ABI here for diagnostics. With the value-repr
                        // staying as a verbatim string, sema's later passes
                        // can re-read child.VALUE — and if absent fall
                        // back to the block ABI by looking at the parent
                        // structure (not currently consulted; default
                        // calling convention is fine for slice 1).
                    }
                    flat_items.push_back(child);
                }
            }
            continue;
        }
        // Per-fn `extern "ABI" fn …;` outside a block: validate the ABI
        // string. Same set as the block form.
        if (code_of(it) == la::EXTERN_FN && it.has_key(la::VALUE)) {
            validate_abi(str_of(it.get(la::VALUE.code)), it);
        }
        flat_items.push_back(it);
    }

    // Track pending #[...] annotations so the collect phase can populate
    // explicit_type_codes_ early (before lower_module). Without this, an
    // `impl Trait for Foo` in one package couldn't resolve Foo's type_code
    // when Foo is declared in another package with #[type_code=N] — leading
    // to a hash-fallback dispatch slot that never matches the runtime tag.
    std::vector<TinyMapView> pending_annots;

    for (uint64_t i = 0; i < flat_items.size(); ++i) {
        auto item = flat_items[i];
        int32_t c = code_of(item);
        if (c == la::ANNOTATION) {
            // T0-6: accumulate in BOTH phases — phase 2 collects consts/
            // statics/type-aliases, and the cfg gate below must see their
            // `#[cfg(...)]` attrs (pre-fix the phase-1-only accumulation
            // left phase-2 items ungated → "duplicate const" on the
            // cfg(unix)/cfg(windows) idiom).
            pending_annots.push_back(item);
            continue;
        }
        if (c == la::DOC_LINE_LIT) {
            // Strip `/// ` (or `///`) prefix; one optional space.
            // Both phases accumulate so the buffer is consistent when the
            // next non-doc item arrives in either pass.
            append_doc_line(pending_doc_, str_of(item.get(la::VALUE.code)));
            continue;
        }
        if (c == la::DOC_BLOCK_LIT) {
            // Phase A.4: `/** ... */` outer block doc-comment.
            append_doc_block(pending_doc_,
                             str_of(item.get(la::VALUE.code)),
                             /*prefix_len=*/3);
            continue;
        }
        if (c == la::INNER_DOC_LIT) {
            // Phase A.3: `//!` lines accumulate into per-module inner-doc
            // buffer (separate from pending_doc_; never attaches to any
            // specific item). Joined in lower_module_items.
            std::string_view raw = str_of(item.get(la::VALUE.code));
            std::string_view stripped = raw.size() >= 3 ? raw.substr(3) : std::string_view{};
            if (!stripped.empty() && stripped.front() == ' ')
                stripped.remove_prefix(1);
            if (!module_inner_doc_.empty()) module_inner_doc_.push_back('\n');
            module_inner_doc_.append(stripped);
            continue;
        }
        if (c == la::INNER_DOC_BLOCK_LIT) {
            // Phase A.4: `/*! ... */` inner block doc-comment.
            append_doc_block(module_inner_doc_,
                             str_of(item.get(la::VALUE.code)),
                             /*prefix_len=*/3);
            continue;
        }
        // §6.8 / T0-6: cfg_attr activation + cfg evaluation — the shared
        // gate (cfg_attrs_drop_item, also called by the lowering walk).
        // `#[cfg_attr(unix, cfg(windows))]` activation runs FIRST so the
        // wrapped `cfg(...)` joins the drop list this iteration; if any
        // cfg predicate is false the item is dropped entirely (don't
        // collect, don't lower) with its pending annotations.
        if (cfg_attrs_drop_item(pending_annots)) {
            pending_annots.clear();
            continue;
        }
        if (phase == 1) {
            // §6.1: `union NAME { … }` shares STRUCT's collection
            // shape (named fields, optional type-params). Slice 1
            // routes the union through `collect_struct` so the rest
            // of sema sees it as a known type; layout falls back to
            // struct sum-of-fields (TODO for max-of-fields slice
            // when the SemaStructInfo grows an `is_union` flag) and
            // field-access doesn't yet require `unsafe`. The parse
            // surface is what unblocks ports today; the soundness
            // pieces follow in a dedicated slice.
            if (c == la::UNION_DEF) {
                if (item.has_key(la::NAME.code)) {
                    auto uname = std::string(str_of(item.get(la::NAME.code)));
                    bool htp = item_has_type_params(item);
                    check_annotations(AttrTarget::Struct, uname, htp, pending_annots);
                    // Note: a `struct X { ... }` + `union X { ... }` collision
                    // is caught at pass-0 (UNION_DEF name pre-registration)
                    // with "duplicate struct/union '{}'" — Rust
                    // `items.union.namespace`. No additional check needed here.
                    collect_struct(item);
                    auto [upkg, usi] = find_struct_by_name(uname);
                    if (usi) {
                        usi->is_union = true;
                        // §6.1 (Rust spec `items.union.fieldless`):
                        // zero-field union is rejected by the
                        // compiler. Macros can produce them but the
                        // item-collection stage errors.
                        if (usi->fields.empty()) {
                            node_line_ = get_line(item);
                            error(std::format(
                                "union `{}` has no fields — Rust requires "
                                "at least one (use a zero-sized struct if "
                                "you need an empty type)", uname));
                        }
                        // §6.1 (Rust spec `items.union.field-restrictions`):
                        // union field types are restricted to Copy
                        // types, references (`&T`/`&mut T`),
                        // ManuallyDrop<T>, or aggregates of these.
                        // Slice-1 uses `is_move_type` as the
                        // rejection oracle — anything classed
                        // move-type (Vec / Box / String / owning
                        // trait object) rejects. Refinement to the
                        // full spec set (ManuallyDrop recognition,
                        // tuple/array recursion) is a Wave 9
                        // follow-up.
                        //
                        // Generic-union exception: a bare TypeParam
                        // field can't be classed here — the concrete
                        // type is only known at instantiation. Defer
                        // the move-type check to monomorphization
                        // (the spec gets the check). Rust accepts a
                        // generic union and requires `T: Copy` at
                        // the use site; ours is functionally
                        // equivalent for now (post-mono Copy-check
                        // is a separate slice).
                        for (auto& f : usi->fields) {
                            if (!f.type) continue;
                            if (TypeRef(f.type).kind() ==
                                LogosType::Kind::TypeVar) continue;
                            // Allow another union as a union field — Rust
                            // accepts an inner union if it's Copy (and
                            // bare unions are not auto-Copy in Rust, but
                            // recursive-union nesting is a common C-FFI
                            // pattern and our `is_move_type` over-rejects
                            // any Struct-kind type here).
                            if (TypeRef(f.type).kind() ==
                                LogosType::Kind::Struct) {
                                auto [fpkg, fsi] = find_struct_by_name(
                                    std::string(TypeRef(f.type).struct_name()));
                                (void)fpkg;
                                if (fsi && fsi->is_union) continue;
                            }
                            if (is_move_type(f.type)) {
                                node_line_ = get_line(item);
                                error(std::format(
                                    "union `{}`: field `{}` has type "
                                    "`{}` which is not allowed in a "
                                    "union (union fields must be "
                                    "`Copy` types, references, "
                                    "`ManuallyDrop<T>`, or aggregates "
                                    "thereof)",
                                    uname, f.name, type_str(f.type)));
                            }
                        }
                    }
                }
                pending_annots.clear();
                continue;
            }
            if      (c == la::STRUCT) {
                bool is_zoned = false;
                for (auto& ann : pending_annots)
                    if (str_of(ann.get(la::NAME.code)) == "zoned") { is_zoned = true; break; }
                if (!item.has_key(la::NAME.code)) { /* struct_inst — skip collect */ }
                else {
                    auto sname = std::string(str_of(item.get(la::NAME.code)));
                    bool htp = item_has_type_params(item);
                    // Single-point attribute parse (see parse_struct_attr_flags):
                    // promotion (#[datatype]/#[annotation]) vs structural flags.
                    check_annotations(AttrTarget::Struct, sname, htp, pending_annots);
                    auto aflags = parse_struct_attr_flags(pending_annots);
                    if (is_specialization_struct(item)) collect_struct_spec(item, &aflags);
                    else if (aflags.promotes_to_datatype()) {
                        collect_datatype(item, aflags.annotation);
                    } else {
                        (void)is_zoned;
                        collect_struct(item);
                        // Structural flag attributes — applied through the
                        // single-point parser (see parse_struct_attr_flags for
                        // the per-flag docs).
                        {
                            auto f = parse_struct_attr_flags(pending_annots);
                            auto skey = sema_key(cur_package_, sname);
                            auto sit = structs_.find(skey);
                            if (sit == structs_.end()) sit = structs_.find(sname);
                            if (sit != structs_.end()) {
                                auto& si = sit->second;
                                si.no_auto_drop    |= f.no_auto_drop;
                                si.self_describing |= f.self_describing;
                                si.rel_ptr         |= f.rel_ptr;
                                si.pinned          |= f.pinned;
                                si.zone_mut        |= f.zone_mut;
                                si.zoned2          |= f.zoned;
                                si.borrow_carrying |= f.borrow_carrying;
                                si.non_null        |= f.non_null;
                            }
                        }
                        // `#[repr(...)]` minimal (logos-core 1.5). For struct
                        // items only `transparent` is recognised so far — sets
                        // the struct's `repr_transparent` flag (single-field
                        // wrapper inherits its field's layout exactly). Other
                        // modes (`C` / `packed` / `align(...)`) are parse-then-
                        // reject — no silent acceptance, no quiet drift if a
                        // ported test expects them to do something.
                        for (auto& ann : pending_annots) {
                            if (str_of(ann.get(la::NAME.code)) != "repr") continue;
                            if (!ann.has_key(la::ARGS.code)) {
                                error(std::format("#[repr] on '{}' requires an argument", sname));
                                continue;
                            }
                            auto args_map = map_of(ann.get(la::ARGS.code));
                            if (!args_map.has_key(la::ITEMS.code)) continue;
                            auto args_items = arr_of(args_map.get(la::ITEMS.code));
                            for (uint64_t kk = 0; kk < args_items.size(); ++kk) {
                                auto a = map_of(args_items.get(kk));
                                if (!a.has_key(la::NAME.code)) continue;
                                std::string mode(str_of(a.get(la::NAME.code)));
                                if (mode == "transparent") {
                                    auto skey = sema_key(cur_package_, sname);
                                    auto sit = structs_.find(skey);
                                    if (sit == structs_.end()) sit = structs_.find(sname);
                                    if (sit != structs_.end()) {
                                        if (sit->second.fields.size() != 1)
                                            error(std::format(
                                                "#[repr(transparent)] on '{}' requires exactly one non-zero-sized field, found {}",
                                                sname, sit->second.fields.size()));
                                        else sit->second.repr_transparent = true;
                                    }
                                } else {
                                    error(std::format(
                                        "#[repr({})] on struct '{}' is not yet supported "
                                        "(only `transparent` is recognised in the core layout pass)",
                                        mode, sname));
                                }
                            }
                        }
                    }
                }
            } else if (c == la::DATATYPE) {
                // Skip explicit instantiation declarations (no FIELDS key, no NAME key).
                // These only bind annotations to existing generic instantiations.
                if (item.has_key(la::NAME.code)) {
                    bool is_spec = is_specialization_struct(item);
                    if (is_spec) {
                        auto aflags2 = parse_struct_attr_flags(pending_annots);
                        collect_struct_spec(item, &aflags2);
                        continue;
                    }
                    // Pre-scan pending annotations to pass the #[annotation] flag
                    // into collect_datatype before field-type validation runs.
                    bool pending_is_annot_type = false;
                    for (auto& ann : pending_annots) {
                        auto aname = str_of(ann.get(la::NAME.code));
                        if (aname == "annotation") { pending_is_annot_type = true; break; }
                    }
                    collect_datatype(item, pending_is_annot_type);
                    // Apply any pending #[type_code=N] to explicit_type_codes_
                    // so collect_impl (same pass) can find it.  Also flag
                    // #[annotation] on the SemaStructInfo so downstream passes
                    // can recognise user-annotation types.
                    auto dname = std::string(str_of(item.get(la::NAME.code)));
                    check_annotations(AttrTarget::Datatype, dname,
                                      item_has_type_params(item), pending_annots);
                    // Annotation-name uniqueness for "exclusive" attributes
                    // (closes B-at-03 — multiple #[type_code] silent before).
                    {
                        logos::compiler::StrSet exclusive_seen;
                        for (auto& ann : pending_annots) {
                            std::string aname(str_of(ann.get(la::NAME.code)));
                            // Add other exclusive attrs here as the registry grows.
                            static const StrSet kExclusive = {"type_code", "annotation"};
                            if (kExclusive.count(aname) &&
                                !exclusive_seen.insert(aname).second) {
                                error(std::format("duplicate #[{}] annotation on '{}'",
                                                  aname, dname));
                            }
                        }
                    }
                    for (auto& ann : pending_annots) {
                        auto aname = std::string(str_of(ann.get(la::NAME.code)));
                        if (aname == "type_code" && ann.has_key(la::VALUE)) {
                            uint64_t tc = read_annotation_u64(ann);
                            auto fqn = cur_package_.empty()
                                       ? dname : cur_package_ + "::" + dname;
                            explicit_type_codes_[fqn] = tc;
                        } else if (aname == "annotation") {
                            auto qkey = sema_key(cur_package_, dname);
                            auto it = datatypes_.find(qkey);
                            if (it != datatypes_.end())
                                it->second.is_annotation_type = true;
                        }
                    }
                }
            }
            else if (c == la::ENUM) {
                if (item.has_key(la::NAME.code)) {
                    check_annotations(AttrTarget::Enum,
                                      str_of(item.get(la::NAME.code)),
                                      item_has_type_params(item), pending_annots);
                }
                collect_enum(item);
                // `#[zoned]` on an enum — the niche enum's Ref arm stores
                // SELF-RELATIVE at-rest (RelOffset) and absolute as a value (the
                // storage/compute split; F3, ref-repr-design §8). Mirrors the
                // struct `#[zoned]` at the field-collection path above.
                if (item.has_key(la::NAME.code)) {
                    std::string ename(str_of(item.get(la::NAME.code)));
                    auto f = parse_struct_attr_flags(pending_annots);
                    if (f.zoned || f.borrow_carrying) {
                        auto eit = enums_.find(sema_key(cur_package_, ename));
                        if (eit == enums_.end()) eit = enums_.find(ename);
                        if (eit != enums_.end()) {
                            eit->second.zoned2          |= f.zoned;
                            eit->second.borrow_carrying |= f.borrow_carrying;
                        }
                    }
                }
                // `#[repr(uN)]` on an enum — set discriminant width if the
                // enum didn't already declare one via the Logos-native
                // `enum Foo : u32 { ... }` ascription. Maps directly to
                // `SemaEnumInfo::backing_type`. `#[repr(C)]` and other modes
                // parse-then-reject (no silent acceptance — logos-core 1.5).
                if (item.has_key(la::NAME.code)) {
                    std::string ename(str_of(item.get(la::NAME.code)));
                    for (auto& ann : pending_annots) {
                        if (str_of(ann.get(la::NAME.code)) != "repr") continue;
                        if (!ann.has_key(la::ARGS.code)) {
                            error(std::format("#[repr] on enum '{}' requires an argument", ename));
                            continue;
                        }
                        auto args_map = map_of(ann.get(la::ARGS.code));
                        if (!args_map.has_key(la::ITEMS.code)) continue;
                        auto args_items = arr_of(args_map.get(la::ITEMS.code));
                        for (uint64_t kk = 0; kk < args_items.size(); ++kk) {
                            auto a = map_of(args_items.get(kk));
                            if (!a.has_key(la::NAME.code)) continue;
                            std::string mode(str_of(a.get(la::NAME.code)));
                            // Recognise integer-type names; map to backing_type.
                            LogosType::Kind disc_kind = LogosType::Kind::Error;
                            if      (mode == "u8")    disc_kind = LogosType::Kind::U8;
                            else if (mode == "u16")   disc_kind = LogosType::Kind::U16;
                            else if (mode == "u32")   disc_kind = LogosType::Kind::U32;
                            else if (mode == "u64")   disc_kind = LogosType::Kind::U64;
                            else if (mode == "i8")    disc_kind = LogosType::Kind::I8;
                            else if (mode == "i16")   disc_kind = LogosType::Kind::I16;
                            else if (mode == "i32")   disc_kind = LogosType::Kind::I32;
                            else if (mode == "i64")   disc_kind = LogosType::Kind::I64;
                            else if (mode == "usize") disc_kind = LogosType::Kind::Usize;
                            else if (mode == "isize") disc_kind = LogosType::Kind::Isize;
                            if (disc_kind != LogosType::Kind::Error) {
                                auto eit = enums_.find(ename);
                                if (eit != enums_.end()) {
                                    if (eit->second.backing_type &&
                                        TypeRef(eit->second.backing_type).kind() != disc_kind) {
                                        error(std::format(
                                            "#[repr({})] on enum '{}' conflicts with declared backing type '{}'",
                                            mode, ename, type_str(eit->second.backing_type)));
                                    } else {
                                        eit->second.backing_type = prim(disc_kind);
                                    }
                                }
                            } else {
                                error(std::format(
                                    "#[repr({})] on enum '{}' is not yet supported "
                                    "(integer discriminant widths uN/iN/usize/isize only in core)",
                                    mode, ename));
                            }
                        }
                    }
                }
            }
            else if (c == la::SCHEMA_DEF) {
                // ADR 0011: collect a `schema S {…}` as a Struct-shaped view.
                if (item.has_key(la::NAME.code)) {
                    check_annotations(AttrTarget::Struct,
                                      str_of(item.get(la::NAME.code)),
                                      false, pending_annots);
                }
                collect_schema(item);
            }
            else if (c == la::SCHEMA_ENUM_DEF) {
                // ADR 0011: collect a `schema enum E {…}` as a Struct-shaped union view.
                if (item.has_key(la::NAME.code)) {
                    check_annotations(AttrTarget::Struct,
                                      str_of(item.get(la::NAME.code)),
                                      false, pending_annots);
                }
                collect_schema_enum(item);
            }
            else if (c == la::FN || c == la::EXTERN_FN)   {
                if (item.has_key(la::NAME.code)) {
                    check_annotations(AttrTarget::Fn,
                                      str_of(item.get(la::NAME.code)),
                                      item_has_type_params(item), pending_annots);
                }
                // Detect `#[no_mangle]` so collect_fn keeps the bare base
                // name in symbol_name (program entry / inline-asm callees).
                pending_no_mangle_ = false;
                pending_fn_macro_ = false;
                pending_token_macro_ = false;
                pending_is_test_      = false;
                pending_should_panic_ = false;
                pending_ignore_       = false;
                pending_should_panic_expected_.clear();
                for (auto& ann : pending_annots) {
                    auto nm = str_of(ann.get(la::NAME.code));
                    if (nm == "no_mangle")    pending_no_mangle_ = true;
                    if (nm == "fn_macro")     pending_fn_macro_  = true;
                    if (nm == "token_macro")  pending_token_macro_ = true;
                    if (nm == "test")         pending_is_test_      = true;
                    if (nm == "ignore")       pending_ignore_       = true;
                    if (nm == "should_panic") {
                        pending_should_panic_ = true;
                        // TH-th-02: extract `expected = "..."` from ARGS.
                        // ANNOT_KV with NAME="expected", VALUE=LIT_STR.
                        if (ann.has_key(la::ARGS.code)) {
                            auto args_map = map_of(ann.get(la::ARGS.code));
                            if (args_map.has_key(la::ITEMS.code)) {
                                auto items_arr = arr_of(args_map.get(la::ITEMS.code));
                                for (uint64_t kk = 0; kk < items_arr.size(); ++kk) {
                                    auto a = map_of(items_arr.get(kk));
                                    if (code_of(a) != la::ANNOT_KV) continue;
                                    if (!a.has_key(la::NAME.code) ||
                                        !a.has_key(la::VALUE.code)) continue;
                                    auto kname = str_of(a.get(la::NAME.code));
                                    if (kname != "expected") continue;
                                    auto v = map_of(a.get(la::VALUE.code));
                                    if (code_of(v) != la::LIT_STR) continue;
                                    auto raw = str_of(v.get(la::VALUE.code));
                                    if (raw.size() >= 2 && raw.front() == '"'
                                        && raw.back() == '"') {
                                        pending_should_panic_expected_.assign(
                                            raw.substr(1, raw.size() - 2));
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                // Phase 7 slice 12: record `#[metaprog_handler("trigger")]`
                // hooks. The annotation's first positional arg is the
                // user-facing trigger name; the host driver scans user
                // items for matching `#[trigger]` annotations and
                // invokes the registered hook on each.
                for (auto& ann : pending_annots) {
                    if (str_of(ann.get(la::NAME.code)) != "metaprog_handler")
                        continue;
                    std::string trigger;
                    if (ann.has_key(la::ARGS.code)) {
                        auto args_map = map_of(ann.get(la::ARGS.code));
                        if (args_map.has_key(la::ITEMS.code)) {
                            auto args_items = arr_of(args_map.get(la::ITEMS.code));
                            if (args_items.size() > 0) {
                                auto a0 = map_of(args_items.get(0));
                                if (code_of(a0) == la::ANNOT_POS && a0.has_key(la::VALUE.code)) {
                                    auto vmap = map_of(a0.get(la::VALUE.code));
                                    if (code_of(vmap) == la::LIT_STR) {
                                        auto raw = str_of(vmap.get(la::VALUE.code));
                                        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                                            trigger.assign(raw.substr(1, raw.size() - 2));
                                    }
                                }
                            }
                        }
                    }
                    if (trigger.empty()) {
                        // Surface as a sema diagnostic later via a degenerate
                        // entry — still record so validation can complain.
                        trigger = "<missing>";
                    }
                    metaprog_handlers_.push_back({
                        std::move(trigger),
                        std::string(str_of(item.get(la::NAME.code)))
                    });
                    break;
                }
                collect_fn(item);
            }
            else if (c == la::TRAIT_DEF) {
                if (item.has_key(la::NAME.code)) {
                    check_annotations(AttrTarget::Trait,
                                      str_of(item.get(la::NAME.code)),
                                      item_has_type_params(item), pending_annots);
                }
                // Peek `#[type_code]` annotation so collect_trait can flag
                // is_writ for the Writ-datatype-family identification
                // that reflect::<T>() and reflection emission consult.
                pending_trait_is_writ_ = false;
                for (auto& ann : pending_annots) {
                    if (str_of(ann.get(la::NAME.code)) == "type_code") {
                        pending_trait_is_writ_ = true;
                        break;
                    }
                }
                collect_trait(item);
            }
            else if (c == la::IMPL_BLOCK)                 collect_impl(item);
        } else {
            if      (c == la::TYPE_ALIAS)                 collect_type_alias(item);
            else if (c == la::CONST_DEF)                  collect_const(item);
            else if (c == la::STATIC_DEF) {
                // §6.2: `static [mut]` — collected like a const for type
                // lookup, plus registered in module_statics_ (name → link
                // symbol) so reads/writes lower through the global's address
                // instead of const-inline. The `mut` form additionally goes
                // into module_static_muts_ (reads/writes require `unsafe`).
                // No VALUE ⇒ extern-block decl: links against the BARE name.
                collect_const(item);
                auto sm_name = std::string(str_of(item.get(la::NAME.code)));
                // Coexistence: module-qualify the static's link symbol
                // (`<module_id>.<pkg>$<name>`) like functions so two modules that
                // each declare `pkg::NAME` don't collide at link. cur_module_id_
                // is the symbol's OWNING module (own source or the binary AST it
                // came from), so imported statics resolve to their definer's sym.
                // extern statics (no VALUE) link against the BARE name — never qualify.
                if (item.has_key(la::VALUE)) {
                    std::string base = std::string(cur_package_) + "$" + sm_name;
                    module_statics_[sm_name] = cur_module_id_.empty()
                        ? base : cur_module_id_ + "." + base;
                } else {
                    module_statics_[sm_name] = sm_name;
                }
                // T1-13: extern statics (no VALUE) — every ACCESS requires
                // `unsafe` (Rust items.extern.static), tracked separately
                // from `static mut`.
                if (!item.has_key(la::VALUE))
                    module_extern_statics_.insert(sm_name);
                if (item.has_key(la::IS_MUT))
                    module_static_muts_.insert(sm_name);
            }
        }
        pending_annots.clear();
        // Defensive: items that didn't consume the doc buffer shouldn't
        // leak it into the next item.
        pending_doc_.clear();
    }
}

void SemaChecker::collect_enum(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("enum {}", ename);
    // B-it-06 — empty enum bodies are intentionally legal as uninhabited /
    // marker types (used by stdlib meta_variant_intrinsics, generic-anchor
    // patterns, etc.). No diagnostic.
    SemaEnumInfo info;
    // T1-9: cross-package visibility (lookup_qualified_<true> checks it).
    info.package = cur_package_;
    info.module_id = cur_module_id_;
    if (node.has_key(la::IS_PUB)) {
        AnyVal pv = node.get(la::IS_PUB.code);
        info.is_pub = !pv.is_null() && pv.is_value() && pv.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);  // §4: `pub(module)`
    info.doc = take_pending_doc();
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);  // B65
    push_type_params(info.type_params);
    // Optional backing type: `enum Foo : u64 { ... }`.  Must be an integer kind.
    if (node.has_key(la::TYPE)) {
        TypeRef bt = resolve_type(map_of(node.get(la::TYPE.code)));
        if (bt && TypeRef(bt).kind() != LogosType::Kind::Error) {
            if (!is_integer_kind(TypeRef(bt).kind()))
                error(std::format("enum backing type must be an integer, got '{}'", type_str(bt)));
            else
                info.backing_type = bt;
        }
    }
    int64_t next_val = 0;
    if (node.has_key(la::ITEMS)) {
        auto av = node.get(la::ITEMS.code);
        if (av.is_pointer()) {
            auto list = map_of(av);
            if (list.has_key(la::ITEMS)) {
                auto variants = arr_of(list.get(la::ITEMS.code));
                std::string variant_sweep_doc;
                for (uint64_t i = 0; i < variants.size(); ++i) {
                    auto v = map_of(variants.get(i));
                    if (try_append_doc(variant_sweep_doc, v)) continue;
                    auto vname = str_of(v.get(la::NAME.code));
                    int64_t vval = next_val;
                    if (v.has_key(la::VALUE)) {
                        auto sv = str_of(v.get(la::VALUE.code));
                        vval = parse_int_literal(sv);
                        if (v.has_key(la::LO_NEG)) vval = -vval;
                    } else if (v.has_key(la::BODY)) {
                        auto blk = map_of(v.get(la::BODY.code));
                        // S8-en-02: `Variant = OtherEnum::OtherVariant (as T)?`.
                        // The grammar emits a CODE-less sub-map with NAME and
                        // FIELD slots — distinguishable from a metacall BLOCK
                        // (which carries CODE=BLOCK). Resolve the referent in
                        // already-collected enums and use its discriminant
                        // value verbatim. Optional `as T` cast is dropped —
                        // width is governed by the enclosing enum's
                        // backing_type / repr.
                        if (code_of(blk) != la::BLOCK &&
                            blk.has_key(la::NAME) && blk.has_key(la::FIELD)) {
                            auto ref_enum    = std::string(str_of(blk.get(la::NAME.code)));
                            auto ref_variant = std::string(str_of(blk.get(la::FIELD.code)));
                            auto [rpkg, rinfo] = find_enum_by_name(ref_enum);
                            if (!rinfo) {
                                auto rit = enums_.find(ref_enum);
                                if (rit != enums_.end()) rinfo = &rit->second;
                            }
                            if (!rinfo) {
                                error(std::format("enum disc: unknown enum '{}'", ref_enum));
                            } else {
                                bool got = false;
                                for (auto& rv : rinfo->variants) {
                                    if (rv.name == ref_variant) {
                                        vval = (int64_t)rv.value;
                                        got = true;
                                        break;
                                    }
                                }
                                if (!got)
                                    error(std::format(
                                        "enum disc: variant '{}::{}' not found",
                                        ref_enum, ref_variant));
                            }
                            // payload check skipped — xref node has no payload list
                            info.variants.push_back({vname, vval, {}, {}, false, false,
                                                     std::move(variant_sweep_doc)});
                            variant_sweep_doc.clear();
                            next_val = vval + 1;
                            continue;
                        }
                        // G159-3: a general const-expression discriminant
                        // (`Purple = 1 << 1`) — BODY is a bare expr node, NOT a
                        // metacall BLOCK. Evaluate it directly via the same CTFE
                        // channel metacall discriminants use.
                        if (code_of(blk) != la::BLOCK) {
                            auto r = ctfe_eval_const(blk, holder_);
                            if (!r)
                                error(std::format("enum discriminant expression: {}", r.error().msg));
                            else
                                vval = r.value().i;
                            info.variants.push_back({vname, vval, {}, {}, false, false,
                                                     std::move(variant_sweep_doc)});
                            variant_sweep_doc.clear();
                            next_val = vval + 1;
                            continue;
                        }
                        // MP-mc-01: `Variant = metacall { <expr> }`. Block
                        // tail expression evaluated via ctfe; integer
                        // result becomes the discriminant.
                        writ::TinyMapView tail{};
                        bool have_tail = false;
                        if (blk.has_key(la::ITEMS)) {
                            auto sitems = arr_of(blk.get(la::ITEMS.code));
                            for (uint64_t k = sitems.size(); k-- > 0; ) {
                                auto s = map_of(sitems.get(k));
                                int32_t sc = code_of(s);
                                if ((sc == la::TAIL_EXPR || sc == la::EXPR_STMT) &&
                                    s.has_key(la::VALUE)) {
                                    tail = map_of(s.get(la::VALUE.code));
                                    have_tail = true; break;
                                }
                            }
                        }
                        if (!have_tail) {
                            error("metacall in enum discriminant must contain a single integer expression");
                        } else {
                            auto r = ctfe_eval_const(tail, holder_);
                            if (!r)
                                error(std::format("metacall in enum discriminant: {}", r.error().msg));
                            else
                                vval = r.value().i;
                        }
                    }
                    if (info.backing_type &&
                        !intlit_fits(vval, TypeRef(info.backing_type).kind()))
                        error(std::format(
                            "variant '{}::{}' = {} does not fit in backing type '{}'",
                            ename, std::string(vname), vval, type_str(info.backing_type)));
                    // Read payload types for tagged union variants
                    std::vector<TypeRef> payload;
                    std::vector<std::string> payload_names;
                    bool is_var = false;
                    bool is_struct_shape = false;
                    if (v.has_key(la::IS_VARIADIC)) is_var = v.get(la::IS_VARIADIC.code).as_value<int32_t>() != 0;
                    if (v.has_key(la::variant::IS_STRUCT_SHAPE))
                        is_struct_shape = v.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;

                    if (v.has_key(la::ITEMS)) {
                        // E0121: enum-variant payload types are item
                        // signatures — `_` rejected (covers tuple,
                        // struct-shape and variadic payload forms below).
                        ItemSignatureGuard sig_guard(in_item_signature_);
                        auto av = v.get(la::ITEMS.code);
                        if (is_var) {
                            // Single type_ref map (variadic variant: ITEMS: $4)
                            payload.push_back(resolve_type(map_of(av)));
                        } else if (is_struct_shape) {
                            // P4-pm-01: struct-shape variant — ITEMS is a
                            // sub-record { ITEMS: [FIELD_DEF*] }, each
                            // carrying NAME + TYPE. Walk in declaration
                            // order; that order becomes the canonical
                            // positional layout downstream.
                            TinyMapView tm(av, holder_);
                            ArrayView fitems;
                            if (tm.has_key(la::ITEMS)) {
                                fitems = arr_of(tm.get(la::ITEMS.code));
                            } else {
                                fitems = arr_of(av);
                            }
                            for (uint64_t j = 0; j < fitems.size(); ++j) {
                                auto fnode = map_of(fitems.get(j));
                                std::string fname = std::string(str_of(fnode.get(la::NAME.code)));
                                TypeRef ftype = fnode.has_key(la::TYPE)
                                    ? resolve_type(map_of(fnode.get(la::TYPE.code)))
                                    : nullptr;
                                payload.push_back(ftype);
                                payload_names.push_back(std::move(fname));
                            }
                            check_unique_names(payload_names,
                                               [](auto& n) -> std::string_view { return n; },
                                               "field",
                                               std::format("variant {}::{}", ename,
                                                           std::string(vname)));
                        } else {
                            // Nested record { ITEMS: [...] } (normal variant: ITEMS: $3)
                            // or raw array (old grammar/other paths)
                            TinyMapView tm(av, holder_);
                            ArrayView pitems;
                            if (tm.has_key(la::ITEMS)) {
                                pitems = arr_of(tm.get(la::ITEMS.code));
                            } else {
                                pitems = arr_of(av);
                            }
                            for (uint64_t j = 0; j < pitems.size(); ++j)
                                payload.push_back(resolve_type(map_of(pitems.get(j))));
                        }
                    }
                    info.variants.push_back({vname, vval, std::move(payload),
                                             std::move(payload_names),
                                             is_var, is_struct_shape,
                                             std::move(variant_sweep_doc)});
                    variant_sweep_doc.clear();
                    next_val = vval + 1;
                }
            }
        }
    }
    pop_type_params(info.type_params);
    enums_[sema_key(cur_package_, ename)] = std::move(info);
}

void SemaChecker::collect_type_alias(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    if (!node.has_key(la::TYPE)) return;
    TypeAliasEntry entry;
    entry.package = cur_package_;
    entry.lifetime_params = read_lifetime_params(node);
    entry.type_params = read_type_params(node);
    push_type_params(entry.type_params);
    {
        // E0121: `type T = _;` rejected — pre-fix the InferredType alias
        // poisoned downstream resolution with misleading stdlib errors.
        ItemSignatureGuard sig_guard(in_item_signature_);
        const bool saved_adr = alias_decl_resolve_;
        alias_decl_resolve_ = true;
        entry.type = resolve_type(map_of(node.get(la::TYPE.code)));
        alias_decl_resolve_ = saved_adr;
    }
    pop_type_params(entry.type_params);
    // ADR 0021 Phase 4a: retain a GENERIC alias's RHS AST so use sites can
    // re-resolve it under concrete bindings when it instantiates a generic
    // const (see TypeAliasEntry.rhs_node). Cheap: a view + holder pointer.
    //
    // LAZY ALIASES, same mechanism: an alias whose RHS did not resolve HERE
    // keeps its AST too, and its use sites re-resolve it. A `type L =
    // typeof(Ledger)` names a container whose config const a LATER metaprog
    // round emits, so decl-time resolution cannot succeed — while the very
    // same `typeof` inside a fn body resolves fine, because bodies are lowered
    // after that round. Without this, a factory-generated container has no
    // writable NAME in any position where a type must be spelled (parameter,
    // field, return, type argument), which is what blocked writing a query
    // against one. Diagnostics are not lost, they MOVE to the use site: an RHS
    // that never resolves errors there, where the name is actually wanted.
    if (!entry.type_params.empty() || !entry.type
        || TypeRef(entry.type).kind() == LogosType::Kind::Error) {
        entry.rhs_node = map_of(node.get(la::TYPE.code));
        entry.holder   = holder_;
    }
    // B-mv-02 (type-alias facet, mirrors collect_trait): a user `type
    // MemDestroyer = ...` coexists with a same-name stdlib alias pulled in via
    // the prelude. Incumbent (first / different package) keeps the bare slot;
    // a same-name alias from another package registers under `pkg::Name` only.
    // lookup_type_by_name probes `cur_package_::name` first, so user code
    // resolves to its own alias. Real duplicate = same package + same name.
    auto bit = type_aliases_.find(name);
    const bool bare_taken_by_other =
        bit != type_aliases_.end() && !bit->second.package.empty() &&
        bit->second.package != cur_package_;
    if (bare_taken_by_other) {
        const std::string qkey = sema_key(cur_package_, name);
        if (type_aliases_.count(qkey))
            error(std::format("duplicate type alias '{}'", name));
        type_aliases_[qkey] = std::move(entry);
        if (!cur_from_binary_) user_type_alias_keys_.insert(qkey);
    } else {
        if (bit != type_aliases_.end())
            error(std::format("duplicate type alias '{}'", name));
        type_aliases_[name] = std::move(entry);
        if (!cur_from_binary_) user_type_alias_keys_.insert(name);
    }
}

void SemaChecker::collect_const(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    TypeRef t = nullptr;
    if (node.has_key(la::TYPE)) {
        // E0121: `const C: _ = …` rejected — const items are item
        // signatures, no inference context.
        ItemSignatureGuard sig_guard(in_item_signature_);
        t = resolve_type(map_of(node.get(la::TYPE.code)));
    } else if (node.has_key(la::VALUE)) {
        // Evaluate type of the value expression lazily.
        // We just store an i32 as placeholder for now.
        t = i32_t();
    }
    if (t) {
        // G156-1: package-scoped consts. Key by sema_key(pkg, name); a same-name
        // const in a DIFFERENT package no longer collides — the duplicate error
        // fires only on a genuine same-(pkg,name) redefinition (closes B-ca-04).
        std::string qk = sema_key(cur_package_, name);
        if (module_consts_.count(qk) || generic_consts_.count(name)) {
            error(std::format("duplicate const '{}'", name));
        }
        module_consts_[qk] = t;
        const_index_add(cur_package_, name);   // bare-name uniqueness index
        // M5 step 5c: track user-origin keys for snapshot filtering.
        if (!cur_from_binary_) user_module_const_keys_.insert(qk);
        if (node.has_key(la::VALUE)) {
            auto val_av = node.get(la::VALUE.code);
            if (val_av.is_pointer())
                module_const_values_[qk] = map_of(val_av);
        }
    }

    // Sprint 1.2: detect self-referential const initializer (closes
    // B-ca-01 P0 SEGFAULT — sema would substitute X for X recursively).
    // Conservative shallow check: catches the direct cycle `const X = X`
    // and the common arithmetic shape `const X = X + N`.  Deeper structural
    // walks (through writ literals, blocks, calls) require a robust
    // AST-walker that respects schema; deferred to Phase 5 fact-base.
    if (node.has_key(la::VALUE)) {
        auto val_av = node.get(la::VALUE.code);
        if (val_av.is_pointer()) {
            auto refs_self_shallow = [&](TinyMapView v) -> bool {
                int32_t vc = code_of(v);
                if (vc == la::VAR_REF) {
                    return str_of(v.get(la::NAME.code)) == name;
                }
                if (vc == la::BINOP || vc == la::UNARY) {
                    if (v.has_key(la::LHS)) {
                        auto la_av = v.get(la::LHS.code);
                        if (la_av.is_pointer() &&
                            code_of(map_of(la_av)) == la::VAR_REF &&
                            str_of(map_of(la_av).get(la::NAME.code)) == name)
                            return true;
                    }
                    if (v.has_key(la::RHS)) {
                        auto ra_av = v.get(la::RHS.code);
                        if (ra_av.is_pointer() &&
                            code_of(map_of(ra_av)) == la::VAR_REF &&
                            str_of(map_of(ra_av).get(la::NAME.code)) == name)
                            return true;
                    }
                }
                return false;
            };
            if (refs_self_shallow(map_of(val_av))) {
                error(std::format("const '{}' references itself in initializer",
                                  name));
            }
            // B-ca-03: const initializer must be a literal expression, simple
            // arithmetic over literals, OR an explicit `metacall <fn>()` —
            // otherwise the bare fn-call is silently inlined at every use
            // site (effectively making `pub const X = compute()` a fn alias,
            // not a constant). Reject anything else with a specific message.
            //
            // Allowed shapes: LIT_*, METACALL, BINOP/UNARY/PAREN_EXPR with
            // const-evaluable children, CAST of const-evaluable, writ-lit
            // (already handled below as WritStatic).
            std::function<bool(TinyMapView)> is_const_evaluable;
            is_const_evaluable = [&](TinyMapView v) -> bool {
                int32_t vc = code_of(v);
                if (vc == la::LIT_INT  || vc == la::LIT_BOOL ||
                    vc == la::LIT_STR  || vc == la::LIT_FLOAT ||
                    vc == la::LIT_CHAR || vc == la::LIT_WSTATIC ||
                    vc == la::LIT_BYTES)
                    return true;
                if (vc == la::WRIT_MAP.code  || vc == la::WRIT_ARRAY.code ||
                    vc == la::WRIT_STR.code  || vc == la::WRIT_INT.code  ||
                    vc == la::WRIT_NEG_INT.code || vc == la::WRIT_FLOAT.code ||
                    vc == la::WRIT_BOOL.code || vc == la::WRIT_NULL.code)
                    return true;  // WritStatic literal — handled separately
                if (vc == la::METACALL) return true;
                // offset_of!(Type, field) lowers to a compile-time i64 const
                // (the field's byte offset — no fn call is inlined), so it is
                // a constant just like a LIT_INT.
                if (vc == la::OFFSET_OF) return true;
                if (vc == la::CAST) {
                    if (!v.has_key(la::VALUE)) return false;
                    return is_const_evaluable(map_of(v.get(la::VALUE.code)));
                }
                if (vc == la::PAREN_EXPR) {
                    if (!v.has_key(la::VALUE)) return false;
                    return is_const_evaluable(map_of(v.get(la::VALUE.code)));
                }
                if (vc == la::UNARY) {
                    if (!v.has_key(la::VALUE)) return false;
                    return is_const_evaluable(map_of(v.get(la::VALUE.code)));
                }
                if (vc == la::BINOP) {
                    bool lhs_ok = v.has_key(la::LHS) &&
                        is_const_evaluable(map_of(v.get(la::LHS.code)));
                    bool rhs_ok = v.has_key(la::RHS) &&
                        is_const_evaluable(map_of(v.get(la::RHS.code)));
                    return lhs_ok && rhs_ok;
                }
                // Array / tuple literals: defer to lower_const_def's B-ca-05
                // check, which produces a specific "const arrays/tuples not
                // yet supported" diagnostic. Returning true here doesn't
                // accept them — it just lets the more-specific message win.
                if (vc == la::ARR_LIT || vc == la::TUPLE_LIT) return true;
                // Struct / union literal: accept if every field-init's value
                // is itself const-evaluable. §6.1 lets `const X: U = U { a: 1 };`
                // and `static S: U = U { a: 1 };` round-trip through the
                // normal struct-lit shape (Rust accepts these — see
                // `items.union.init.intro` and `items.static`).
                if (vc == la::STRUCT_LIT) {
                    if (!v.has_key(la::ITEMS)) return true;
                    auto inits = arr_of(v.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < inits.size(); ++i) {
                        auto fi = map_of(inits.get(i));
                        int32_t fic = code_of(fi);
                        if (fic == la::FIELD_INIT && fi.has_key(la::VALUE)) {
                            if (!is_const_evaluable(map_of(fi.get(la::VALUE.code))))
                                return false;
                        }
                        // FIELD_SHORTHAND would expand to a VAR_REF — not
                        // const-evaluable on its own. Don't accept it here.
                        else if (fic == la::FIELD_SHORTHAND) return false;
                    }
                    return true;
                }
                // §6.2 (items.static.init): "Static initializers may
                // refer to and read from other statics." A VAR_REF
                // resolving to an already-collected module const/static
                // is const-evaluable; same for &VAR_REF (AddrOf of a
                // module item — `static R: &i32 = &X;`).
                if (vc == la::VAR_REF && v.has_key(la::NAME)) {
                    auto vn = std::string(str_of(v.get(la::NAME.code)));
                    if (const_pkg_of_.count(vn)) return true;   // G156-1: any-pkg const
                    // A bare fn-name in static-init position is a fn
                    // pointer constant (Rust: `static F: fn() -> i32 =
                    // answer;` is well-formed). Accept it when the
                    // name resolves to at least one known free fn.
                    if (!find_func_candidates(vn).empty()) return true;
                }
                // NOTE — Rust soundness: a bare CALL in const/static
                // init position would silently inline at every read site
                // (B-ca-03's invariant). The escape hatch is
                // `metacall <fn>(...)` (METACALL acceptance above).
                // Stdlib constructors like `atomic_i32_new(0)` aren't
                // usable directly in const/static init — that's a Wave-9
                // gap pending `const fn` or per-callee const-eval-safe
                // marking. Pinned by fail/const_bare_fn_call.
                // `&X` parses as UNARY{op:"&", value:X}; `&mut X` is
                // ADDR_OF_MUT (static-init can't be `&mut` — rejected
                // by type-check anyway). Accept the shared form when
                // the inner is itself a VAR_REF to a known module item
                // or otherwise const-evaluable.
                auto is_addr_of_unary = [&](TinyMapView u) {
                    if (code_of(u) != la::UNARY) return false;
                    if (!u.has_key(la::OP)) return false;
                    auto op = str_of(u.get(la::OP.code));
                    return op == "&";
                };
                if (is_addr_of_unary(v) && v.has_key(la::VALUE)) {
                    auto inner = map_of(v.get(la::VALUE.code));
                    if (code_of(inner) == la::VAR_REF && inner.has_key(la::NAME)) {
                        auto vn = std::string(str_of(inner.get(la::NAME.code)));
                        if (const_pkg_of_.count(vn)) return true;   // G156-1: any-pkg const
                    }
                    return is_const_evaluable(inner);
                }
                return false;
            };
            if (!is_const_evaluable(map_of(val_av))) {
                error(std::format(
                    "const '{}': initializer must be a literal expression, "
                    "simple arithmetic over literals, or an explicit "
                    "`metacall <fn>(...)` (a bare fn call would otherwise "
                    "silently inline at every read site, not produce a "
                    "compile-time constant)", name));
            }
        }
    }

    // `pub const X: WritStatic = @{...};` semantically replaces the legacy
    // `pub type X = @{...};` form. The literal is a WritStatic value, not
    // a type — but at type-arg positions it functions as a const-generic
    // value with byte-hash identity. Register X as a type alias to that
    // WStaticLit so call-site lookups resolve uniformly with legacy aliases
    // until that path is fully migrated. Generic constants
    // (`pub const X<T1, T2>: WritStatic = @{… <type:T1> …}`) are recorded
    // separately in generic_consts_ and instantiated per use-site.
    if (node.has_key(la::VALUE) && t &&
        TypeRef(t).kind() == LogosType::Kind::Struct &&
        is_writ_static(t)) {
        auto val_av = node.get(la::VALUE.code);
        if (val_av.is_pointer()) {
            auto val_node = map_of(val_av);
            auto vc = code_of(val_node);
            // writ_lit produces LIT_WSTATIC at expression position when
            // the literal flows through a type-arg slot; here the value-AST
            // IS the writ literal (WRIT_MAP / WRIT_ARRAY / scalar).
            // resolve_type's wstatic-lit handling expects a LIT_WSTATIC
            // wrapper. The legacy path went through wstatic_lit_type which
            // emitted LIT_WSTATIC; const_def's value is the bare writ_lit
            // node, so we synthesise a LIT_WSTATIC view by resolving via
            // the LIT_WSTATIC/WRIT_* code path directly.
            //
            // Easiest: detect bare writ_lit AST codes and route them
            // through the existing LIT_WSTATIC handler in resolve_type by
            // synthesising the same shape.
            //
            // BTFL 8b: a quote-emitted const whose value came through
            // parse_wstatic (`= #(cfg)`) carries the LIT_WSTATIC WRAPPER at
            // the value slot (the wstatic_lit_type export's shape), where a
            // source-parsed const carries the bare writ_lit. Normalize by
            // unwrapping — downstream (resolve_wstatic_value, the generic
            // per-use instantiation, const lowering) all expect the bare
            // literal node.
            if (vc == la::LIT_WSTATIC && val_node.has_key(la::VALUE)) {
                val_node = map_of(val_node.get(la::VALUE.code));
                vc = code_of(val_node);
            }
            (void)vc;
            // Attempt resolve: if VALUE node has LIT_WSTATIC code already,
            // resolve_type accepts it. Otherwise, the value is a primary
            // writ_lit AST and we need to detect that here.
            //
            // Generic case: defer to per-use-site instantiation.
            bool has_type_params = false;
            if (node.has_key(la::TYPE_PARAMS)) {
                AnyVal tpav = node.get(la::TYPE_PARAMS.code);
                if (!tpav.is_null()) {
                    auto tplist = map_of(tpav);
                    if (tplist.has_key(la::ITEMS)) {
                        auto items = arr_of(tplist.get(la::ITEMS.code));
                        has_type_params = items.size() > 0;
                    }
                }
            }
            if (!has_type_params) {
                // Non-generic: bind X as a type alias to the resolved
                // WStaticLit. Wrap value in a LIT_WSTATIC-shaped node by
                // calling the existing resolver path.
                TypeRef hs = resolve_type(val_node);
                if (hs && TypeRef(hs).kind() == LogosType::Kind::WStaticLit) {
                    TypeAliasEntry ent{};
                    ent.type = hs;
                    type_aliases_[name] = std::move(ent);
                }
            } else {
                // Generic: parse type_params, save the val_node for per-use-
                // site instantiation. resolve_type's GENERIC_INST handler
                // looks up here.
                std::vector<TypeParam> tps;
                AnyVal tpav = node.get(la::TYPE_PARAMS.code);
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto tp_node = map_of(items.get(i));
                        TypeParam tp;
                        tp.name = std::string(str_of(tp_node.get(la::NAME.code)));
                        tps.push_back(std::move(tp));
                    }
                }
                GenericConstEntry ent;
                ent.type_params = std::move(tps);
                ent.value_node = val_node;
                ent.holder = holder_;
                generic_consts_[name] = std::move(ent);
                if (!cur_from_binary_) user_generic_const_keys_.insert(std::string(name));
            }
        }
    }
}

void SemaChecker::collect_trait(TinyMapView node) {
    auto tname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("trait {}", tname);
    // Push "Self" as a type param so trait method signatures can reference it.
    current_type_params_["Self"] = make_typevar("Self");
    current_trait_name_ = tname;
    SemaTraitInfo info;
    info.name = tname;
    // T1-9: cross-package visibility (lookup_qualified_<true> checks it).
    if (node.has_key(la::IS_PUB)) {
        AnyVal pv = node.get(la::IS_PUB.code);
        info.is_pub = !pv.is_null() && pv.is_value() && pv.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);  // §4: `pub(module)`
    info.doc = take_pending_doc();
    info.is_writ = pending_trait_is_writ_;
    pending_trait_is_writ_ = false;
    // Read auto marker
    if (node.has_key(la::IS_AUTO)) {
        AnyVal av = node.get(la::IS_AUTO);
        info.is_auto = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Validate: auto traits must declare no members.
    //
    // ITEMS holds more than just body members: the grammar's `$...` collector
    // also slurps in non-member capture nodes from the rule's leading items —
    // the `pub_vis` visibility node (`pub` / `pub(module)`), and the
    // type-param / supertrait lists for `auto trait T<…>: … {}`. Those are not
    // body items, so a raw `size() > 0` check spuriously rejects every auto
    // trait that carries a visibility modifier, type params, or a supertrait
    // (e.g. `pub auto trait Send {}`). Count only genuine trait members — the
    // same FN / ASSOC_TYPE_DEF / ASSOC_CONST_DEF codes the member-collection
    // loop below recognises.
    if (info.is_auto && node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        bool has_member = false;
        for (uint64_t i = 0; i < items.size() && !has_member; ++i) {
            auto code = code_of(map_of(items.get(i)));
            has_member = (code == la::FN || code == la::ASSOC_TYPE_DEF
                          || code == la::ASSOC_CONST_DEF);
        }
        if (has_member) {
            error(std::format("auto trait '{}' must have an empty body", tname));
            // Bug 1 fix: clean up Self and trait name before early return to
            // avoid polluting scope for subsequent trait collections.
            current_type_params_.erase("Self");
            current_trait_name_.clear();
            return;
        }
    }
    // Read unsafe marker
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE);
        info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Read trait type params (e.g. trait Into<T>)
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);
    // Read supertraits: trait Foo: Bar + Baz<T> { ... } → SUPERS: { ITEMS: [TRAIT_BOUND(...), ...] }
    if (node.has_key(la::SUPERS)) {
        auto supers_node = map_of(node.get(la::SUPERS.code));
        if (supers_node.has_key(la::ITEMS)) {
            auto supers = arr_of(supers_node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < supers.size(); ++i) {
                auto b = map_of(supers.get(i));
                if (code_of(b) != la::TRAIT_BOUND) continue;
                TraitBound tb;
                tb.trait_name = std::string(str_of(b.get(la::NAME.code)));
                read_trait_bound_args(b, tb);
                info.supertraits.push_back(std::move(tb));
            }
        }
    }
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        std::string trait_method_sweep_doc;
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (try_append_doc(trait_method_sweep_doc, m)) continue;
            if (code_of(m) == la::ASSOC_TYPE_DEF) {
                SemaAssocTypeInfo at;
                at.name = std::string(str_of(m.get(la::NAME.code)));
                at.doc = std::move(trait_method_sweep_doc);
                trait_method_sweep_doc.clear();
                // GAT: read the assoc type's own type params (e.g. type Item<T>)
                at.type_params = read_type_params(m);
                push_type_params(at.type_params);
                if (m.has_key(la::ITEMS)) {
                    auto bounds = arr_of(m.get(la::ITEMS.code));
                    for (uint64_t b = 0; b < bounds.size(); ++b) {
                        auto bnode = map_of(bounds.get(b));
                        if (code_of(bnode) == la::TRAIT_BOUND) {
                            TraitBound tb;
                            tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                            at.bounds.push_back(std::move(tb));
                        }
                    }
                }
                // §3 c13 Wave 9 — `type Item = i32;` default RHS. The
                // grammar emits a TYPE slot when the `= type_ref` form
                // matched. An impl that omits this assoc type then
                // falls back to the default (Rust behavior).
                if (m.has_key(la::TYPE)) {
                    // E0121: assoc-type default is an item signature.
                    ItemSignatureGuard sig_guard(in_item_signature_);
                    at.default_type = resolve_type(map_of(m.get(la::TYPE.code)));
                }
                pop_type_params(at.type_params);
                info.assoc_types.push_back(std::move(at));
                continue;
            }
            if (code_of(m) == la::ASSOC_CONST_DEF) {
                SemaAssocConstInfo ac;
                ac.name = std::string(str_of(m.get(la::NAME.code)));
                if (m.has_key(la::TYPE)) {
                    // E0121: assoc-const type is an item signature.
                    ItemSignatureGuard sig_guard(in_item_signature_);
                    ac.type = resolve_type(map_of(m.get(la::TYPE.code)));
                }
                // §6 f1 Wave 9 — `const X: i32 = 42;` default. When the
                // grammar matched the `= expr` form a VALUE slot is set;
                // record so impl that omits this const falls back to it.
                // T2-14: keep the default VALUE ast for projection.
                if (m.has_key(la::VALUE)) {
                    ac.has_default = true;
                    ac.default_value_ast = m.get(la::VALUE.code);
                }
                ac.doc = std::move(trait_method_sweep_doc);
                trait_method_sweep_doc.clear();
                info.assoc_consts.push_back(std::move(ac));
                continue;
            }
            if (code_of(m) == la::REL_SIG) {
                // ADR 0016 §6: `rel edge(parent: i64, …);` — a source-trait
                // vocabulary member. Contextual keyword: the grammar accepts
                // any lead IDENT; only the literal `rel` is legal.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                if (lead != "rel") {
                    error(std::format(
                        "trait '{}': unexpected member '{} {}' — trait bodies "
                        "contain fn / type / const / rel members",
                        tname, lead, std::string(str_of(m.get(la::NAME.code)))));
                    continue;
                }
                TraitRelSig sig;
                sig.rel = std::string(str_of(m.get(la::NAME.code)));
                if (m.has_key(la::PARAMS)) {
                    auto cl = map_of(m.get(la::PARAMS.code));
                    if (!cl.is_null() && cl.has_key(la::ITEMS.code)) {
                        auto carr = arr_of(cl.get(la::ITEMS.code));
                        for (uint64_t j = 0; j < carr.size(); ++j) {
                            auto cp = map_of(carr.get(j));
                            if (cp.is_null() || code_of(cp) != la::PARAM) continue;
                            std::string cn(str_of(cp.get(la::NAME.code)));
                            std::string ct = render_type_src_syntactic_(
                                map_of(cp.get(la::TYPE.code)));
                            if (ct != "i64" && ct != "str" && ct != "bool") {
                                error(std::format(
                                    "trait '{}': rel '{}' column '{}' has type "
                                    "`{}` — rel columns are i64/str/bool (rows "
                                    "are set-deduplicated; the column type must "
                                    "be joinable/Eq)", tname, sig.rel, cn, ct));
                                ct = "i64";   // recover, keep collecting
                            }
                            sig.cols.push_back({cn, ct});
                        }
                    }
                }
                if (sig.cols.empty()) {
                    error(std::format(
                        "trait '{}': rel '{}' needs at least one typed column",
                        tname, sig.rel));
                    continue;
                }
                for (const auto& seen : trait_rels_[tname]) {
                    if (seen.rel == sig.rel) {
                        // User ASTs re-collect every metaprog round while the
                        // registry persists across rounds (snapshot) — an
                        // IDENTICAL re-collection is confirmation. Divergence
                        // (changed columns) is the real duplicate.
                        bool same = seen.cols.size() == sig.cols.size();
                        if (same)
                            for (size_t ci = 0; ci < sig.cols.size(); ++ci)
                                if (seen.cols[ci].name != sig.cols[ci].name
                                    || seen.cols[ci].ty != sig.cols[ci].ty)
                                    { same = false; break; }
                        if (!same)
                            error(std::format("trait '{}': duplicate rel '{}'",
                                              tname, sig.rel));
                        sig.rel.clear();
                        break;
                    }
                }
                if (!sig.rel.empty()) trait_rels_[tname].push_back(std::move(sig));
                trait_method_sweep_doc.clear();
                continue;
            }
            if (code_of(m) != la::FN) continue;
            SemaTraitMethodInfo mi;
            mi.name = std::string(str_of(m.get(la::NAME.code)));
            mi.doc = std::move(trait_method_sweep_doc);
            trait_method_sweep_doc.clear();
            // Method-level type params: `fn hash<H: Hasher>(...)`. Push them on the
            // scope stack so resolve_type() can see H inside param/ret types.
            if (m.has_key(la::TYPE_PARAMS)) {
                mi.type_params = read_type_params_from(m, la::TYPE_PARAMS.code);
                push_type_params(mi.type_params);
            }
            if (m.has_key(la::PARAMS)) {
                auto pav = m.get(la::PARAMS.code);
                if (!pav.is_null() && pav.is_pointer()) {
                    auto plist = map_of(pav);
                    if (plist.has_key(la::ITEMS)) {
                        auto params = arr_of(plist.get(la::ITEMS.code));
                        for (uint64_t j = 0; j < params.size(); ++j) {
                            auto p = map_of(params.get(j));
                            // P2-15: the first param is the `self` receiver if it
                            // lacks an explicit TYPE (`&self`/`&mut self`/`self`)
                            // or is named `self` (`self: &S` — concretely typed).
                            if (j == 0) {
                                bool no_type = !p.has_key(la::TYPE);
                                std::string pn = p.has_key(la::NAME)
                                    ? std::string(str_of(p.get(la::NAME.code))) : "";
                                mi.has_self_receiver = no_type || pn == "self";
                            }
                            if (p.has_key(la::TYPE)) {
                                // E0121: trait-method signatures reject `_`.
                                ItemSignatureGuard sig_guard(in_item_signature_);
                                mi.param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                            } else {
                                // `&self` / `&mut self` — no TYPE token, but receiver is
                                // real; synthesize Self so param_types[0] = self for
                                // later arity checks (see sema_expr expected_explicit).
                                auto self_tv = make_typevar("Self");
                                TypeRef ty = self_tv;
                                bool is_mut = false;
                                if (p.has_key(la::IS_MUT)) {
                                    AnyVal mv = p.get(la::IS_MUT);
                                    is_mut = !mv.is_null() && mv.is_value() && mv.as_value<uint8_t>() != 0;
                                }
                                ty = make_ref(is_mut, self_tv);
                                mi.param_types.push_back(ty);
                            }
                        }
                    }
                }
            }
            {
                ItemSignatureGuard sig_guard(in_item_signature_);
                mi.ret_type = m.has_key(la::RET_TYPE)
                    ? resolve_type(map_of(m.get(la::RET_TYPE.code))) : void_t();
            }
            // P2-15: a `where Self: Sized` method is excluded from the vtable, so
            // it never affects object-safety (the trait-method where-clause is now
            // captured under WHERE — grammar fix). Scan for a `Self: Sized` bound.
            if (m.has_key(la::WHERE)) {
                AnyVal wav = m.get(la::WHERE.code);
                if (!wav.is_null()) {
                    auto wmap = map_of(wav);
                    if (wmap.has_key(la::ITEMS)) {
                        auto witems = arr_of(wmap.get(la::ITEMS.code));
                        for (uint64_t wi = 0; wi < witems.size(); ++wi) {
                            auto witem = map_of(witems.get(wi));
                            if (code_of(witem) != la::TYPE_PARAM) continue;
                            std::string subject(str_of(witem.get(la::NAME.code)));
                            if (!witem.has_key(la::ITEMS)) continue;
                            auto inner = arr_of(witem.get(la::ITEMS.code));
                            for (uint64_t bj = 0; bj < inner.size(); ++bj) {
                                auto bnode = map_of(inner.get(bj));
                                if (code_of(bnode) != la::TRAIT_BOUND) continue;
                                std::string bound_trait(str_of(bnode.get(la::NAME.code)));
                                if (subject == "Self") {
                                    if (bound_trait == "Sized")
                                        mi.requires_sized_self = true;
                                } else {
                                    // §8.5: a where-bound whose subject is a
                                    // TRAIT type-param (Item, K, …) — captured
                                    // for per-impl default synthesis gating.
                                    mi.where_param_bounds.push_back({subject, bound_trait});
                                }
                            }
                        }
                    }
                }
            }
            mi.has_default = m.has_key(la::BODY);
            if (mi.has_default) {
                mi.default_ast    = items.get(i);
                mi.default_holder = holder_;  // remember zone that owns the AST node
            }
            if (m.has_key(la::IS_UNSAFE)) {
                AnyVal av = m.get(la::IS_UNSAFE);
                mi.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            if (!mi.type_params.empty())
                pop_type_params(mi.type_params);
            info.methods.push_back(std::move(mi));
        }
    }
    pop_type_params(info.type_params);
    current_type_params_.erase("Self");
    current_trait_name_.clear();
    info.package = cur_package_;  // record so cross-pkg resolution can pick scope
    // B-mv-02 fix: by default a trait keeps its legacy BARE-name slot (single
    // entry — preserves the per-trait iterations over traits_). When a user
    // trait collides with an already-registered trait of the SAME bare name
    // from a DIFFERENT package (e.g. a user `trait From` vs the prelude's
    // `logos.lang.convert::From`), the two are distinct traits (Rust parity):
    // keep the incumbent in the bare slot and register the newcomer ONLY under
    // its package-qualified key `pkg::Name`. `find_trait_by_name` probes
    // `cur_package_::Name` first, so user code resolves to its own trait while
    // bare/hardcoded lookups (`traits_.find("Iterator")`) and other packages
    // still see the incumbent. Doubling is thus confined to genuinely-colliding
    // names. Real duplicate (B-it-05) = same package + same name.
    auto bit = traits_.find(tname);
    const bool bare_taken_by_other =
        !tname.empty() && bit != traits_.end() && !bit->second.predeclared &&
        !bit->second.package.empty() && bit->second.package != cur_package_;
    if (bare_taken_by_other) {
        const std::string qkey = sema_key(cur_package_, tname);
        if (auto qit = traits_.find(qkey);
            qit != traits_.end() && !qit->second.predeclared)
            error(std::format("duplicate trait '{}'", tname));
        traits_[qkey] = std::move(info);   // qualified-only; bare untouched
        if (!cur_from_binary_) user_trait_keys_.insert(qkey);
    } else {
        if (!tname.empty() && bit != traits_.end() && !bit->second.predeclared)
            error(std::format("duplicate trait '{}'", tname));
        traits_[tname] = std::move(info);  // legacy bare slot (canonical)
        if (!cur_from_binary_) user_trait_keys_.insert(tname);
    }
}

void SemaChecker::collect_impl(TinyMapView node) {
    std::string impl_doc = take_pending_doc();
    std::string trait_name;
    if (node.has_key(la::NAME))
        trait_name = std::string(str_of(node.get(la::NAME.code)));
    // T1-9 (audit-v2): `impl PrivTrait for Mine` from another package errors
    // ("private to package") — collect_impl used to consume the raw name
    // without any visibility gate. The lookup itself stays check-free
    // (introspective callers must not emit privacy diags); the explicit
    // check fires here, at the site that INTRODUCES the foreign trait.
    if (!trait_name.empty()) {
        auto [tpkg, tinfo] = find_trait_by_name(trait_name);
        (void)tpkg;
        if (tinfo) {
            auto mit = pkg_module_ids_.find(tinfo->package);  // §4: trait's module
            check_pub_access(tinfo->is_pub, tinfo->package, trait_name,
                             tinfo->is_module_only,
                             mit != pkg_module_ids_.end() ? mit->second : std::string{});
        }
    }
    bool impl_is_unsafe = false;
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE);
        impl_is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    bool impl_is_negative = false;
    if (node.has_key(la::IS_NEGATIVE)) {
        AnyVal av = node.get(la::IS_NEGATIVE);
        impl_is_negative = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Push impl's own type params: either from IMPL_TYPE_PARAMS (new generic trait impl
    // form: impl<T> Trait for Struct<T>) or from TYPE_PARAMS (standalone: impl<T> Pair<T>).
    std::vector<TypeParam> impl_tps;
    // B62: impl-level lifetime params from `impl<'a, T>` — used to recognize
    // when a trait-arg lifetime is universally quantified at impl site.
    std::vector<std::string> impl_lt_params;
    // B65: outlives bounds from `impl<'a, 'b: 'a> ...`.
    std::vector<std::pair<std::string, std::string>> impl_lt_outlives;
    auto extract_impl_lt = [&](int32_t field_code) {
        if (!node.has_key(field_code)) return;
        AnyVal tpav = node.get(field_code);
        if (tpav.is_null()) return;
        auto tplist = map_of(tpav);
        if (!tplist.has_key(la::ITEMS)) return;
        auto items = arr_of(tplist.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto it = map_of(items.get(i));
            if (code_of(it) != la::LIFETIME_PARAM) continue;
            impl_lt_params.push_back(std::string(str_of(it.get(la::NAME.code))));
        }
    };
    if (node.has_key(la::IMPL_TYPE_PARAMS)) {
        impl_tps = read_type_params_from(node, la::IMPL_TYPE_PARAMS.code);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;
        extract_impl_lt(la::IMPL_TYPE_PARAMS.code);
        impl_lt_outlives = read_lifetime_outlives_from(node, la::IMPL_TYPE_PARAMS.code);
    } else if (trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        impl_tps = read_type_params(node);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;  // so collect_fn includes them in fn.type_params
        extract_impl_lt(la::TYPE_PARAMS.code);
        impl_lt_outlives = read_lifetime_outlives(node);
    }
    // CP-cm-16 follow-up: capture impl-target pattern (set later, after target_resolved
    // is computed). RAII-style restore handled by impl_type_params_.clear() at end of
    // collect_impl — we reset impl_target_typeref_ in the same teardown block.
    impl_target_typeref_ = nullptr;
    // B67: pick up the where-clause's outlives + type-outlives bounds too.
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) impl_lt_outlives.push_back(std::move(p));
        if (node.has_key(la::WHERE)) {
            AnyVal wav = node.get(la::WHERE.code);
            if (!wav.is_null()) {
                auto wmap = map_of(wav);
                if (wmap.has_key(la::ITEMS)) {
                    auto witems = arr_of(wmap.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < witems.size(); ++i) {
                        auto witem = map_of(witems.get(i));
                        if (code_of(witem) != la::TYPE_PARAM) continue;
                        if (!witem.has_key(la::ITEMS)) continue;
                        std::string tname(str_of(witem.get(la::NAME.code)));
                        auto inner = arr_of(witem.get(la::ITEMS.code));
                        TypeParam* target = nullptr;
                        for (auto& tp : impl_tps)
                            if (tp.name == tname) { target = &tp; break; }
                        if (!target) continue;
                        for (uint64_t j = 0; j < inner.size(); ++j) {
                            auto inode = map_of(inner.get(j));
                            if (code_of(inode) != la::LIFETIME_PARAM) continue;
                            target->lifetime_outlives.push_back(
                                std::string(str_of(inode.get(la::NAME.code))));
                        }
                    }
                }
            }
        }
    }
    // TYPE is the target type (simple_type, ptr_type, or GENERIC_INST)
    std::string target;
    TypeRef target_resolved = nullptr;  // concrete resolved type (for Self)
    if (node.has_key(la::TYPE)) {
        auto tnode = map_of(node.get(la::TYPE.code));
        if (code_of(tnode) == la::PTR_TYPE) {
            // *const T or *mut T → resolve full type string
            auto resolved = resolve_type(tnode);
            target = type_str(resolved);
        } else if (code_of(tnode) == la::UNSIZED_SLICE_TYPE) {
            // Phase 1B-10: `impl Trait for [T]` — bare unsized-slice
            // self-type. Resolve under `unsized_ok_=true` so the bare
            // `[T]` produces Kind::UnsizedSlice. Mangle:
            //   - `[T]` with TypeVar element → `$slice$T` (generic blanket).
            //   - `[u8]` with concrete element → `$slice$u8` (concrete).
            // Dispatch (sema_expr) matches by recv's Slice<elem> kind.
            bool was_ok = unsized_ok_;
            unsized_ok_ = true;
            auto resolved = resolve_type(tnode);
            unsized_ok_ = was_ok;
            target_resolved = resolved;
            TypeRef elem = resolved ? TypeRef(resolved).elem() : TypeRef(nullptr);
            if (elem && TypeRef(elem).kind() == LogosType::Kind::TypeVar) {
                target = "$slice$T";
            } else {
                target = "$slice$" + (elem ? type_str(elem) : std::string("?"));
            }
        } else if (code_of(tnode) == la::DYN_TYPE) {
            // Phase 1B-10: `impl Trait for dyn Foo` — bare dyn-trait
            // self-type. Resolve under `unsized_ok_=true` so the bare
            // `dyn Foo` produces Kind::UnsizedDyn (Phase 1B-4). Mangle:
            //   - `$dyn$Foo` for concrete trait.
            bool was_ok = unsized_ok_;
            unsized_ok_ = true;
            auto resolved = resolve_type(tnode);
            unsized_ok_ = was_ok;
            target_resolved = resolved;
            target = "$dyn$" + (resolved ? std::string(TypeRef(resolved).trait_name())
                                         : std::string("?"));
        } else if (code_of(tnode) == la::REF_TYPE ||
                   code_of(tnode) == la::MUT_REF_TYPE) {
            // &T or &mut T → "$ref_Foo" / "$mut_ref_Foo" (base) for generic
            // pointee, "$ref_Foo$G1$i32" for concrete. The "$ref_"/"$mut_ref_"
            // prefix is symbol-safe (no `&` in mangled names) and unambiguous
            // (regular structs can't start with "$"). Mirrors GENERIC_INST.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            std::string prefix = (code_of(tnode) == la::MUT_REF_TYPE) ? "$mut_ref_" : "$ref_";
            TypeRef pointee = resolved ? TypeRef(resolved).pointee() : TypeRef(nullptr);
            // `impl Trait for &[T]` / `&mut [T]`: the reference-to-slice
            // canonicalises to Kind::Slice (a fat pointer — Logos ABI). Register
            // it under the SAME `$slice$<elem>` / `$slice$T` key as a bare
            // `impl Trait for [T]`, so the slice-receiver dispatch
            // (try_method_on_slice) finds it (`self: &[T]` ≡ Slice<T>).
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::Slice) {
                TypeRef selem = TypeRef(resolved).elem();
                target = (selem && TypeRef(selem).kind() == LogosType::Kind::TypeVar)
                         ? std::string("$slice$T")
                         : "$slice$" + (selem ? type_str(selem) : std::string("?"));
                // Treat `impl Trait for &[T]` exactly like `impl Trait for [T]`:
                // bind Self to the UnsizedSlice form so `&Self` canonicalises to
                // Slice and the method body emits under the same `$slice$` symbol
                // the slice-receiver dispatch calls (otherwise Self=Slice diverges
                // and the body is never emitted under the expected name).
                target_resolved = make_unsized_slice_type(selem);
            } else if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
                            TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct)) {
                bool has_tvar = false;
                for (auto a : TypeRef(pointee).type_args())
                    if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { has_tvar = true; break; }
                if (TypeRef(pointee).type_args().empty() || has_tvar) {
                    target = prefix + std::string(TypeRef(pointee).struct_name());
                } else {
                    target = prefix + concrete_struct_name(pointee);
                }
            } else if (pointee && TypeRef(pointee).kind() == LogosType::Kind::TypeVar) {
                // Phase 1B-8: generic ref-blanket `impl<T> Trait for &T` /
                // `impl<T> Trait for &mut T`. Use a fixed sentinel name so
                // dispatch can find the impl by trait-receiver shape
                // regardless of the typevar's source name. Coherence rules
                // (one such impl per trait/ref-shape) keep this unambiguous.
                target = prefix + "$T";
            } else {
                target = prefix + type_str(resolved);
            }
        } else if (code_of(tnode) == la::GENERIC_INST) {
            // Concrete generic (e.g. Pair<i32>) → use mangled name; generic (Pair<T>) → base name.
            target = std::string(str_of(tnode.get(la::NAME.code)));
            if (impl_tps.empty()) {
                // No own type params — may be a concrete specialization like impl Pair<i32>.
                auto resolved = resolve_type(tnode);
                if (resolved && !TypeRef(resolved).type_args().empty()) {
                    bool concrete = true;
                    for (auto a : TypeRef(resolved).type_args())
                        if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { concrete = false; break; }
                    if (concrete) {
                        if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                            TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct)
                            target = concrete_struct_name(resolved);
                        target_resolved = resolved;
                    }
                }
            } else {
                // Generic-target impl like `impl<T: Send> Send for Mutex<T>` — capture
                // the full pattern (Mutex<T> with TypeVar T) for honest bound-check
                // at auto-trait satisfaction time. impl_tps already pushed into scope.
                target_resolved = resolve_type(tnode);
            }
        } else if (code_of(tnode) == la::TUPLE_TYPE) {
            // SL-sl-08: `impl Trait for (A, B, …)` — tuple as impl target.
            // Resolve to a Tuple TypeRef and mangle by arity (generic
            // impl with TypeVar elems → `$tuple$N`) or by element types
            // (concrete impl with monomorphic elems → `$tuple$N$<t1>$<t2>…`).
            // Variadic form `impl<A...> Trait for (A...)` → `$tuple$variadic`.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::Void) {
                target = "void";
            } else {
                auto elems = TypeRef(resolved).tuple_elems();
                bool is_variadic_target = false;
                if (elems.size() == 1) {
                    TypeRef e0(elems[0]);
                    if (e0 && e0.kind() == LogosType::Kind::TypeVar) {
                        std::string tvn(e0.type_var_name());
                        for (auto& itp : impl_tps)
                            if (itp.name == tvn && itp.is_variadic) {
                                is_variadic_target = true;
                                break;
                            }
                    }
                }
                if (is_variadic_target) {
                    target = "$tuple$variadic";
                } else {
                    size_t arity = elems.size();
                    bool any_tvar = false;
                    for (auto e : elems)
                        if (e && TypeRef(e).kind() == LogosType::Kind::TypeVar)
                            { any_tvar = true; break; }
                    target = "$tuple$" + std::to_string(arity);
                    if (!any_tvar) {
                        for (auto e : elems) {
                            target += "$";
                            target += (e ? type_str(e) : std::string("?"));
                        }
                    }
                }
            }
        } else if (code_of(tnode) == la::FN_PTR_TYPE) {
            // G149-6: `impl<A,B,C> Trait for fn(A,B)->C` — fn-pointer is
            // type-erased to a uniform pointer at the Logos ABI, so the impl
            // covers ALL fn-ptrs of a given arity. Key it by arity: `$fnptr$N`.
            auto resolved = resolve_type(tnode);
            target_resolved = resolved;
            target = "$fnptr$" + std::to_string(TypeRef(resolved).closure_params().size());
        } else {
            target = std::string(str_of(tnode.get(la::NAME.code)));
            // Unfold transparent type aliases so `impl Trait for Alias`
            // registers methods under the aliased type's concrete name.
            auto ait = type_aliases_.find(target);
            if (ait != type_aliases_.end() &&
                ait->second.type_params.empty() && ait->second.lifetime_params.empty()) {
                auto aliased = ait->second.type;
                if (aliased && (TypeRef(aliased).kind() == LogosType::Kind::Struct ||
                                TypeRef(aliased).kind() == LogosType::Kind::ZonedStruct)) {
                    if (!TypeRef(aliased).type_args().empty()) {
                        target = concrete_struct_name(aliased);
                        target_resolved = aliased;
                    } else {
                        target = std::string(TypeRef(aliased).struct_name());
                    }
                } else if (aliased && TypeRef(aliased).kind() == LogosType::Kind::Slice) {
                    target = type_str(aliased);
                }
            }
        }
    }
    // Note: impl_tps are left in current_type_params_ until after collect_fn calls below.
    // CP-cm-16 follow-up: publish impl-target pattern so collect_fn can plant
    // it onto each method's SemaFuncInfo. Carries the full pattern with
    // unsubstituted TypeVars (e.g. `Result<Vec<T>, E>`); finish_generic_call
    // unifies against the concrete receiver to recover impl-level bindings.
    impl_target_typeref_ = target_resolved;
    // G149-6: fn-ptr target is type-erased → its methods codegen identically for
    // all A,B,C; collect them NON-generic (don't prepend impl type-params, which
    // would make a never-instantiated template). A,B,C stay pushed for `&Self`.
    if (target.rfind("$fnptr$", 0) == 0)
        impl_type_params_.clear();
    if (trait_name.empty())
        ctx_ = std::format("{}impl {}", impl_is_unsafe ? "unsafe " : "", target);
    else
        ctx_ = std::format("{}impl {} for {}", impl_is_unsafe ? "unsafe " : "", trait_name, target);
    // Set Self → the concrete target type so method signatures resolve *const Self, etc.
    // For generic impl<T> Foo<T>: Self = Foo<T> (TypeVars); for impl Foo<i32>: Self = Foo<i32>.
    {
        TypeRef self_type = nullptr;
        if (target_resolved) {
            // Concrete specialization: use the fully resolved type (preserves type_args).
            self_type = target_resolved;
        } else {
            std::string base_target = target;
            if (auto d = base_target.find('$'); d != std::string::npos)
                base_target = base_target.substr(0, d);
            // Try struct first (target, then base_target)
            auto [spkg_t, ssi_t] = find_struct_by_name(target);
            auto [spkg_b, ssi_b] = find_struct_by_name(base_target);
            SemaStructInfo* ssi_found = ssi_t ? ssi_t : ssi_b;
            std::string sname = ssi_t ? target : (ssi_b ? base_target : "");
            std::string spkg  = ssi_t ? spkg_t : spkg_b;
            if (ssi_found) {
                if (!impl_tps.empty()) {
                    std::vector<TypeRef> tv_args;
                    for (auto& tp : impl_tps)
                        tv_args.push_back(make_typevar(tp.name));
                    // Bug 2: include struct's lifetime params so Self carries lifetime_args.
                    std::vector<std::string> lt_args = ssi_found->lifetime_params;
                    self_type = make_generic_struct(sname, std::move(tv_args), std::move(lt_args), spkg);
                } else {
                    self_type = make_struct_type(sname, spkg);
                }
            } else {
                // Try datatype
                auto [dpkg_t, dsi_t] = find_datatype_by_name(target);
                auto [dpkg_b, dsi_b] = find_datatype_by_name(base_target);
                SemaStructInfo* dsi_found = dsi_t ? dsi_t : dsi_b;
                std::string dname = dsi_t ? target : (dsi_b ? base_target : "");
                std::string dpkg  = dsi_t ? dpkg_t : dpkg_b;
                if (dsi_found) {
                    // Bug 3: datatypes with lifetime params also need lifetime_args in Self.
                    if (!impl_tps.empty()) {
                        std::vector<TypeRef> tv_args;
                        for (auto& tp : impl_tps)
                            tv_args.push_back(make_typevar(tp.name));
                        std::vector<std::string> lt_args = dsi_found->lifetime_params;
                        self_type = make_generic_datatype(dname, std::move(tv_args), std::move(lt_args), dpkg);
                    } else {
                        self_type = make_datatype_type(dname, dpkg);
                    }
                } else {
                    // Blanket impl (impl<T: Bound> Trait for T): target IS a type
                    // parameter in impl_tps.  Self resolves to that TypeVar so
                    // method signatures with `&self`/`*const Self` see it, and
                    // the bound's trait methods become callable on self.
                    for (auto& tp : impl_tps) {
                        if (tp.name == target) {
                            self_type = make_typevar(target);
                            break;
                        }
                    }
                    // Primitive target (e.g. impl Hash for i32): look up by name.
                    if (!self_type) {
                        if (auto prim_t = lookup_type_by_name(target))
                            self_type = prim_t;
                    }
                }
            }
        }
        // Phase 1B-11: when target_resolved is set to an unsized self-type
        // kind (UnsizedSlice / UnsizedDyn from the 1B-10 grammar paths),
        // prefer it over the name-based lookup so method signatures see
        // Self = UnsizedSlice<...> / UnsizedDyn<...>. `&Self` then
        // canonicalises to the existing fat-pointer kind at resolve time
        // (Phase 1B-11 resolve_type canonicalisation).
        if (target_resolved &&
            (TypeRef(target_resolved).kind() == LogosType::Kind::UnsizedSlice ||
             TypeRef(target_resolved).kind() == LogosType::Kind::UnsizedDyn)) {
            self_type = target_resolved;
        }
        // `impl Trait for str` falls through to the primitive lookup which
        // returns Slice<u8> (Logos's fat-ptr alias). For method-body
        // semantics we want Self = UnsizedSlice<u8> so `&Self` canonicalises
        // to Slice<u8> rather than nesting to `&&[u8]`.
        if (target == "str") {
            LogosTypeBuilder us; us.kind = LogosType::Kind::UnsizedSlice;
            us.elem = u8_t();
            self_type = pool_->alloc(std::move(us));
        }
        // G149-6: a fn-ptr target binds Self to the resolved FnPtr so `&Self`
        // resolves (codegens as a plain ptr).
        if (!self_type && target_resolved)
            self_type = target_resolved;
        if (self_type)
            current_type_params_["Self"] = self_type;
    }
    // Verify trait exists (only for trait impls)
    // Copy and Drop are built-in marker traits — not always visible through
    // the dependency-graph (pub trait + use isn't enough when the target
    // type's own package re-imports a different non-pub Drop, e.g. std.string
    // and writ.zone both used to declare local `trait Drop`). Treating Drop
    // as a built-in matches Copy and lets the impl resolve via name alone.
    if (!trait_name.empty() && trait_name != "Copy" && trait_name != "Drop"
        && !traits_.count(trait_name))
        error(std::format("impl: unknown trait '{}'", trait_name));
    // Phase 6: scope the impl's trait name so `Self::Item<X>` inside
    // method bodies / signatures resolves before impls_ is populated.
    current_impl_trait_name_ = trait_name;
    // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
    // and push them into current_type_params_ so method sigs resolve correctly.
    std::vector<TypeRef> trait_type_args;
    // B62: parallel collection of lifetime args at trait position
    // (`impl Trait<'a, T>` → ["a"]). Skipped from type_args resolution but
    // captured for HRTB satisfaction check at bound time.
    std::vector<std::string> trait_lt_args;
    if (!trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    // L1: skip LIFETIME_PARAM entries — they're lifetime
                    // arg position in `impl Foo<'a> for ...`. Logos doesn't
                    // track regions structurally for trait dispatch, and
                    // resolve_type would error on code 131 (LIFETIME_PARAM).
                    if (code_of(item) == la::LIFETIME_PARAM) {
                        trait_lt_args.push_back(
                            std::string(str_of(item.get(la::NAME.code))));
                        continue;
                    }
                    trait_type_args.push_back(resolve_type(item));
                }
            }
        }
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (size_t i = 0; i < tit->second.type_params.size() &&
                                i < trait_type_args.size(); ++i)
                current_type_params_[tit->second.type_params[i].name] = trait_type_args[i];
        }
    }
    // G156-1: expose this impl's concrete trait type-args to collect_fn so the
    // method collision-detection can mangle by them (empty for inherent impls).
    current_impl_trait_args_ = trait_type_args;
    // Detect blanket impl: `impl<T: Bound> Trait for T` — target IS one of
    // this impl's own type parameters.  Methods are collected as generic fns
    // (target becomes the TypeVar name, e.g. "T__method"); later, at call
    // sites on concrete types satisfying Bound, we instantiate the blanket.
    bool is_blanket = false;
    std::string blanket_bound_trait;
    std::vector<std::string> blanket_extra_bounds;
    // ADR 0008: associated-type equality clauses parallel to the bound list.
    std::vector<std::pair<std::string, TypeRef>> blanket_primary_assoc_eqs;
    std::vector<std::pair<std::string,
        std::vector<std::pair<std::string, TypeRef>>>> blanket_extra_assoc_eqs;
    if (!trait_name.empty()) {
        for (auto& tp : impl_tps) {
            if (tp.name == target) {
                is_blanket = true;
                if (!tp.bounds.empty()) {
                    blanket_bound_trait = tp.bounds[0].trait_name;
                    blanket_primary_assoc_eqs = tp.bounds[0].assoc_eqs;
                    for (size_t bi = 1; bi < tp.bounds.size(); ++bi) {
                        blanket_extra_bounds.push_back(tp.bounds[bi].trait_name);
                        blanket_extra_assoc_eqs.emplace_back(
                            tp.bounds[bi].trait_name, tp.bounds[bi].assoc_eqs);
                    }
                }
                break;
            }
        }
    }

    // Snapshot blanket_impls_ size so we can detect after the items loop
    // whether *any* per-method blanket entry got pushed. Blankets that
    // declare only assoc-types (no fn methods) need a marker entry so
    // trait-satisfaction queries can find them — without it,
    // sema_has_impl_recursive would report the blanket trait as
    // unsatisfied for any concrete that depends on it.
    size_t blanket_size_before = blanket_impls_.size();

    // Register impl methods as free functions with mangled names: Target__method
    // Also collect associated type definitions.
    // Skip if already registered (e.g. class methods defined inline).
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: sweep doc-lines into pending_doc_ so the next
        // collect_fn invocation picks them up via take_pending_doc().
        pending_doc_.clear();
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (try_append_doc(pending_doc_, m)) continue;
            if (code_of(m) == la::REL_BIND) {
                // ADR 0016 §6: `rel edge = writ_graph_edges;` — bind one
                // trait rel to its native materializer for this type.
                std::string lead(str_of(m.get(la::REL_KW.code)));
                std::string rn(str_of(m.get(la::NAME.code)));
                if (lead != "rel") {
                    error(std::format(
                        "impl for '{}': unexpected member '{} {}'",
                        target, lead, rn));
                    continue;
                }
                if (trait_name.empty()) {
                    error(std::format(
                        "impl for '{}': `rel {} = …` outside a trait impl — "
                        "rel bindings implement a source trait's vocabulary "
                        "(`impl GraphSource for {} {{ … }}`)",
                        target, rn, target));
                    continue;
                }
                auto trit = trait_rels_.find(trait_name);
                const TraitRelSig* sig = nullptr;
                if (trit != trait_rels_.end())
                    for (const auto& ts : trit->second)
                        if (ts.rel == rn) { sig = &ts; break; }
                if (!sig) {
                    error(std::format(
                        "impl {} for {}: trait '{}' declares no rel '{}'",
                        trait_name, target, trait_name, rn));
                    continue;
                }
                SourceRelBind b;
                b.trait_name = trait_name;
                b.rel    = rn;
                b.mat_fn = std::string(str_of(m.get(la::VALUE.code)));
                b.mat_module = cur_package_;   // refined at spec time if needed
                b.cols   = sig->cols;
                bool dup = false, same = false;
                for (const auto& e : source_impls_[target])
                    if (e.rel == rn) {
                        dup = true;
                        // The convention pre-scan (#[derive_graph_source])
                        // seeds this exact binding a round before the derive's
                        // emitted impl collects — identical re-registration
                        // is confirmation, not conflict.
                        same = (e.trait_name == trait_name && e.mat_fn == b.mat_fn);
                        break;
                    }
                if (dup) {
                    if (!same)
                        error(std::format(
                            "impl {} for {}: duplicate rel binding '{}'",
                            trait_name, target, rn));
                    continue;
                }
                source_impls_[target].push_back(std::move(b));
                continue;
            }
            if (code_of(m) == la::FN || code_of(m) == la::STATIC_FN) {
                auto mname = std::string(str_of(m.get(la::NAME.code)));
                // Blanket impls use a synthetic target name so the method
                // doesn't collide with `T::method` lookups on other generic
                // `T: Trait` type parameters that share the same letter.
                // Blanket key includes the bound trait so distinct blankets
                // on the same trait (e.g. `impl<DT: Primitive> T for DT` and
                // `impl<DT: PodRef> T for DT`) register under separate keys.
                std::string reg_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                auto mangled = reg_target + "__" + mname;
                collect_fn(m, reg_target, trait_name);
                // Trait-impl methods inherit their trait's accessibility:
                // if the trait is reachable, so are its methods.  The
                // grammar disallows `pub fn` inside trait / trait-impl
                // blocks, so force is_pub=true post-collection.  Inherent
                // impls (no trait_name) keep the explicit pub/private split.
                if (!trait_name.empty()) {
                    // Push method-level type params so `fn m<H: Bound>(&self, x: &mut H)`
                    // can resolve H when re-walking params for public-visibility promotion.
                    auto method_tps = read_type_params(m);
                    if (!method_tps.empty()) push_type_params(method_tps);
                    std::vector<TypeRef> method_param_types;
                    if (m.has_key(la::PARAMS)) {
                        auto params_av = m.get(la::PARAMS.code);
                        if (params_av.is_pointer()) {
                            auto params_node = map_of(params_av);
                            if (params_node.has_key(la::ITEMS)) {
                                auto arr = arr_of(params_node.get(la::ITEMS.code));
                                for (uint64_t j = 0; j < arr.size(); ++j) {
                                    auto p = map_of(arr.get(j));
                                    if (code_of(p) != la::PARAM) continue;
                                    if (p.has_key(la::TYPE))
                                        method_param_types.push_back(resolve_type(map_of(p.get(la::TYPE.code))));
                                    else {
                                        auto self_t = current_type_params_.count("Self")
                                            ? current_type_params_.at("Self") : error_t();
                                        bool is_mut = p.has_key(la::IS_MUT) &&
                                                      !p.get(la::IS_MUT.code).is_null() &&
                                                      p.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                                        method_param_types.push_back(make_ref(is_mut, self_t));
                                    }
                                }
                            }
                        }
                    }
                    if (!method_tps.empty()) pop_type_params(method_tps);
                    if (auto it = find_func_by_base_and_signature(mangled, method_param_types, false))
                        const_cast<SemaFuncInfo*>(it)->is_pub = true;
                }
                if (is_blanket) {
                    BlanketImpl bi_rec;
                    bi_rec.trait_name = trait_name;
                    bi_rec.target_typevar = target;
                    bi_rec.bound_trait = blanket_bound_trait;
                    bi_rec.extra_bounds = blanket_extra_bounds;
                    bi_rec.method_name = mname;
                    bi_rec.mangled_name = mangled;
                    bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
                    bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
                    if (!cur_from_binary_) user_blanket_mangled_.insert(bi_rec.mangled_name);
                    blanket_impls_.push_back(std::move(bi_rec));
                }
            } else if (code_of(m) == la::ASSOC_TYPE_IMPL && !trait_name.empty()) {
                auto aname = std::string(str_of(m.get(la::NAME.code)));
                // For blanket impls, key under the synthetic `$blanket$...`
                // name so normal `T::Assoc` lookups on other generics don't
                // shadow; the AssocType resolver falls back to blanket keys
                // when the concrete base satisfies the blanket's bound.
                std::string key_target = is_blanket
                    ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
                    : target;
                // G156-1: key by the trait's concrete type-args so two impls of
                // a generic trait `Trait<T>` for ONE type at distinct T (each
                // declaring the same-named assoc type) coexist. The SUFFIXED key
                // (empty suffix for non-generic traits → identical to the legacy
                // key) is always stored; the PLAIN key is stored first-impl-wins
                // for backward-compat + unambiguous single-impl lookups, and
                // ERASED once a second distinct-args impl appears so a bare
                // ambiguous `X::Assoc` lookup fails loud (Rust requires
                // `<X as Trait<T>>::Assoc`). Resolution prefers the suffixed key
                // when the trait args are known from the impl context
                // (current_impl_trait_args_). A true duplicate (same
                // trait+args+target+name) still collides on the suffixed key.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                std::string key  = trait_name + targ_sfx + "::" + key_target + "::" + aname;
                std::string pkey = trait_name + "::" + key_target + "::" + aname;
                if (assoc_type_impls_.count(key))
                    error(std::format("impl {} for {}: duplicate associated type '{}'",
                                      trait_name, target, aname));
                // GAT: read assoc type's own params (e.g. type Item<T> = ...)
                std::vector<TypeParam> gat_tps = read_type_params(m);
                // Bug 2 fix: GAT param names must not shadow impl type param names.
                for (auto& gtp : gat_tps)
                    for (auto& itp : impl_tps)
                        if (gtp.name == itp.name)
                            error(std::format("impl {} for {}: GAT param '{}' shadows impl type param",
                                              trait_name, target, gtp.name));
                // Bug 4 fix: impl GAT arity must match the trait's declaration.
                auto tit_gat = find_trait_iter_scoped(trait_name);
                if (tit_gat != traits_.end()) {
                    for (auto& at_def : tit_gat->second.assoc_types) {
                        if (at_def.name == aname && at_def.type_params.size() != gat_tps.size()) {
                            error(std::format(
                                "impl {} for {}: associated type '{}' has {} GAT params but trait declares {}",
                                trait_name, target, aname,
                                gat_tps.size(), at_def.type_params.size()));
                            break;
                        }
                    }
                }
                push_type_params(gat_tps);
                auto atype = resolve_type(map_of(m.get(la::TYPE.code)));
                pop_type_params(gat_tps);
                AssocTypeEntry ate{atype, impl_tps, gat_tps, std::move(pending_doc_)};
                pending_doc_.clear();
                assoc_type_impls_[key] = ate;
                if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(key);
                // Plain key: first-impl-wins; erase on a second distinct-args
                // impl so bare ambiguous lookups fail loud (G156-1). When
                // targ_sfx is empty (non-generic trait) key==pkey already —
                // nothing more to do.
                if (!targ_sfx.empty()) {
                    if (!assoc_type_impls_.count(pkey)) {
                        assoc_type_impls_[pkey] = std::move(ate);
                        if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(pkey);
                    } else {
                        // A different-args impl already claimed the plain key →
                        // the bare projection is now ambiguous. Drop it.
                        assoc_type_impls_.erase(pkey);
                        user_assoc_type_impl_keys_.erase(pkey);
                    }
                }
            } else if (code_of(m) == la::ASSOC_CONST_IMPL) {
                auto cname = std::string(str_of(m.get(la::NAME.code)));
                std::string assoc_doc = std::move(pending_doc_);
                pending_doc_.clear();
                if (trait_name.empty()) {
                    // B97: inherent assoc-const on `impl S { const C: T = ...; }`
                    // is allowed; register it under "inherent::<target>::<name>".
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    std::string key = "inherent::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                } else {
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    // Type check: impl's type must match the trait's declared type.
                    auto tit2 = find_trait_iter_scoped(trait_name);
                    if (tit2 != traits_.end() && ctype) {
                        for (auto& ac_def : tit2->second.assoc_consts) {
                            if (ac_def.name == cname && ac_def.type) {
                                if (!types_equal(ac_def.type, ctype))
                                    error(std::format(
                                        "impl {} for {}: associated constant '{}' declared as '{}' but trait requires '{}'",
                                        trait_name, target, cname,
                                        type_str(ctype), type_str(ac_def.type)));
                                break;
                            }
                        }
                    }
                    std::string key = trait_name + "::" + target + "::" + cname;
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE), nullptr, std::move(assoc_doc) };
                    if (!cur_from_binary_) user_assoc_const_impl_keys_.insert(key);
                }
            }
        }
        // Defensive: clear pending_doc_ at end of impl-body iteration so
        // it doesn't leak into the surrounding collect_module loop.
        pending_doc_.clear();
    }
    // If this is a blanket impl with no fn methods (only assoc-types), the
    // items loop didn't push anything to blanket_impls_. Push a marker
    // entry now so trait-satisfaction queries (sema_has_impl_recursive,
    // assoc_eqs_satisfied) can see it. method_name stays empty.
    if (is_blanket && blanket_impls_.size() == blanket_size_before) {
        BlanketImpl bi_rec;
        bi_rec.trait_name = trait_name;
        bi_rec.target_typevar = target;
        bi_rec.bound_trait = blanket_bound_trait;
        bi_rec.extra_bounds = blanket_extra_bounds;
        // method_name / mangled_name intentionally empty — this is a
        // satisfaction-only marker, not a method-dispatch entry.
        bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
        bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
        // M5 step 5c: for satisfaction markers (empty mangled), tag with
        // a synthetic "$marker$<trait>$<bound>$<target>" so the snapshot
        // filter can drop user-origin entries by mangled-name.
        if (!cur_from_binary_) {
            std::string marker = "$marker$" + trait_name + "$" +
                                 blanket_bound_trait + "$" + target;
            user_blanket_mangled_.insert(marker);
            bi_rec.mangled_name = std::move(marker);
        }
        blanket_impls_.push_back(std::move(bi_rec));
    }

    // Check completeness: every required trait method must be in the impl.
    // Default methods are registered as Target__method if not overridden.
    // Blanket impls use a synthetic target in their registrations; apply the
    // same mapping here so the completeness check sees the real methods.
    std::string check_target = is_blanket
        ? ("$blanket$" + trait_name + "$" + blanket_bound_trait + "$" + target)
        : target;
    if (!trait_name.empty()) {
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = check_target + "__" + m.name;
                // Trait-aware mangling: a method that collided with another
                // trait's same-named method was re-keyed under the
                // trait-qualified base; check that first.
                // G156-1: when this impl has concrete trait type-args, the method
                // was re-keyed under the args-aware base `<tgt>__<Trait>$<args>__<m>`
                // (so `impl Trait<u64>` and `impl Trait<u8>` coexist). Look that up
                // first, then the bare trait-qualified base, then the plain name.
                std::string targ_sfx = trait_targ_suffix(trait_type_args);
                auto cands = !targ_sfx.empty()
                    ? find_func_candidates(check_target + "__" + trait_name + targ_sfx + "__" + m.name)
                    : std::vector<const SemaFuncInfo*>{};
                if (cands.empty()) cands = find_func_candidates(
                    check_target + "__" + trait_name + "__" + m.name);
                if (cands.empty()) cands = find_func_candidates(mangled);
                // Find if THIS specific overload was explicitly provided.
                // Match by arity and non-receiver param types.
                // Receiver (param[0]) is &TypeVar(Self) in the trait vs
                // &ConcreteType in the impl — always skip it.
                // A TypeVar in the trait param (possibly inside Ref/Ptr) is a
                // generic param and matches any concrete impl type.
                auto is_generic_param = [](TypeRef t) -> bool {
                    while (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
                                 TypeRef(t).kind() == LogosType::Kind::MutRef ||
                                 TypeRef(t).kind() == LogosType::Kind::Ptr))
                        t = TypeRef(t).pointee();
                    if (!t) return false;
                    // TypeVar = generic type param (T); AssocType = T::Item
                    // Both are polymorphic from the trait's perspective and
                    // match any concrete type in the impl.
                    TypeRef tv{t};
                    return tv.kind() == LogosType::Kind::TypeVar ||
                           tv.kind() == LogosType::Kind::AssocType;
                };
                // Fn-family epic step B: detect a variadic trait type
                // param (`pub trait Fn<A...> { fn call(&self, args: A...)
                // ... }`). If the trait method uses the pack TypeVar at
                // some position, the impl is allowed to expose any number
                // of concrete params from that position onward — pack
                // absorbs them.
                //
                // Step B.4 (Deferred-3): per-element type check past the
                // pack position. The impl block carries `trait_type_args`
                // (e.g. `impl Fn<i32, i32> for Foo` → [i32, i32]); we
                // unify each post-pack impl-method param against the
                // corresponding `trait_type_args` element so an impl that
                // exposes `fn call(&self, x: u8, y: bool)` against
                // `Fn<i32, i32>` gets rejected instead of silently bound.
                std::string variadic_tp_name;
                for (auto& tp : tit->second.type_params) {
                    if (tp.is_variadic) { variadic_tp_name = tp.name; break; }
                }
                // S2b: substitute the impl's CONCRETE trait args into the
                // declared method signature before comparing. `fn f(&self,
                // cnt: &[u64; N])` in `trait T<const N: u32>` must compare as
                // `&[u64; 1]` against an `impl T<K, 1>` method — without the
                // substitution a const-param-sized array in a trait method
                // makes every impl method "missing" (the conformance check is
                // one more site of the array-length disease). Bound TYPE
                // params substitute too — the leftover-generic skips below
                // still cover the unbound ones.
                SemaSubst trait_arg_subst;
                {
                    auto& tps = tit->second.type_params;
                    for (size_t ti = 0; ti < tps.size() && ti < trait_type_args.size(); ++ti) {
                        if (trait_type_args[ti])
                            trait_arg_subst[tps[ti].name] = trait_type_args[ti];
                    }
                }
                const SemaFuncInfo* matching = nullptr;
                for (auto* c : cands) {
                    int variadic_pos = -1;
                    if (!variadic_tp_name.empty()) {
                        for (size_t k = 1; k < m.param_types.size(); ++k) {
                            auto tp = m.param_types[k];
                            if (tp &&
                                TypeRef(tp).kind() == LogosType::Kind::TypeVar &&
                                TypeRef(tp).type_var_name() == variadic_tp_name) {
                                variadic_pos = (int)k;
                                break;
                            }
                        }
                    }
                    bool has_pack = (variadic_pos >= 0);
                    if (has_pack) {
                        if (c->param_types.size() < (size_t)variadic_pos) continue;
                    } else {
                        if (c->param_types.size() != m.param_types.size()) continue;
                    }
                    bool sig_match = true;
                    size_t check_end = has_pack
                        ? (size_t)variadic_pos
                        : m.param_types.size();
                    for (size_t k = 1; k < check_end; ++k) {
                        auto tp = m.param_types[k];
                        auto cp = c->param_types[k];
                        if (!tp || !cp) { sig_match = false; break; }
                        if (!trait_arg_subst.empty())
                            tp = subst_type_sema(tp, trait_arg_subst);
                        // If the trait param is generic (TypeVar or AssocType),
                        // it matches any concrete impl type.
                        if (is_generic_param(tp)) continue;
                        if (is_generic_param(cp)) continue;
                        if (!types_equal(tp, cp)) { sig_match = false; break; }
                    }
                    // Per-element check past the pack position: each
                    // impl-method param at index k (where k >=
                    // variadic_pos) corresponds to trait_type_args[k -
                    // variadic_pos]. Mismatch rejects the candidate.
                    if (has_pack && sig_match) {
                        for (size_t k = (size_t)variadic_pos;
                             k < c->param_types.size(); ++k) {
                            size_t targ_idx = k - (size_t)variadic_pos;
                            if (targ_idx >= trait_type_args.size()) {
                                // Impl exposes more args than the pack
                                // instantiation has — concrete arity
                                // overflow.
                                sig_match = false; break;
                            }
                            auto exp = trait_type_args[targ_idx];
                            auto cp  = c->param_types[k];
                            if (!exp || !cp) continue;
                            if (is_generic_param(exp)) continue;
                            if (is_generic_param(cp)) continue;
                            if (!types_equal(exp, cp)) {
                                sig_match = false; break;
                            }
                        }
                        // Trait pack carries more args than impl exposes —
                        // arity underflow (less common; the
                        // `c->param_types.size() < variadic_pos` guard
                        // catches the leading-args case but not the
                        // pack-itself-arity).
                        if (sig_match &&
                            (c->param_types.size() - (size_t)variadic_pos)
                                != trait_type_args.size())
                            sig_match = false;
                    }
                    if (sig_match) { matching = c; break; }
                }
                if (matching) {
                    if (m.is_unsafe != matching->is_unsafe) {
                        error(std::format("impl {} for {}: method '{}' has mismatched unsafe parity (trait: {}, impl: {})",
                            trait_name, target, m.name,
                            m.is_unsafe ? "unsafe" : "safe",
                            matching->is_unsafe ? "unsafe" : "safe"));
                    }
                } else if (m.has_default) {
                    // This overload not explicitly provided; register the default.
                    // Build Self type; for generic impls include the type params as TypeVars.
                    TypeRef self_type = nullptr;
                    {
                        auto [spkg_def, ssi_def] = find_struct_by_name(target);
                        auto [dpkg_def, dsi_def] = find_datatype_by_name(target);
                        auto _shaped_target = [](TypeRef pat) -> bool {
                            if (!pat) return false;
                            for (auto a : TypeRef(pat).type_args()) {
                                if (!a) continue;
                                auto k = TypeRef(a).kind();
                                if (k != LogosType::Kind::TypeVar &&
                                    k != LogosType::Kind::ConstVar)
                                    return true;
                            }
                            return false;
                        };
                        if (ssi_def) {
                            // Shaped target → Self = the impl's pattern (see
                            // sema_decl synthesis; both sides must agree or
                            // declared vs body types of defaults diverge).
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(target, std::move(tv_args), {}, spkg_def);
                            } else {
                                self_type = make_struct_type(target, spkg_def);
                            }
                        } else if (dsi_def) {
                            if (target_resolved && _shaped_target(target_resolved)) {
                                self_type = target_resolved;
                            } else if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_datatype(target, std::move(tv_args), {}, dpkg_def);
                            } else {
                                self_type = make_datatype_type(target, dpkg_def);
                            }
                        } else if (auto prim_t = lookup_type_by_name(target);
                                   prim_t && [&]{
                                       auto k = TypeRef(prim_t).kind();
                                       using K = LogosType::Kind;
                                       return k==K::I8||k==K::I16||k==K::I24||k==K::I32||
                                              k==K::I56||k==K::I64||k==K::I128||
                                              k==K::U8||k==K::U16||k==K::U24||k==K::U32||
                                              k==K::U56||k==K::U64||k==K::U128||
                                              k==K::Usize||k==K::Isize||
                                              k==K::F32||k==K::F64||k==K::Bool||k==K::Char;
                                   }()) {
                            // G160-3: `impl Trait for <SCALAR primitive>` (i64/
                            // u32/bool/char/…). Bind Self to the primitive so a
                            // default body's `&Self` signature resolves. Without
                            // this, every inherited default beyond the first
                            // (which only worked by inheriting a leaked Self)
                            // failed with "unknown type 'Self'". RESTRICTED to
                            // scalar kinds — `str` (a slice alias) / enum targets
                            // (Option/Result) must NOT be bound here (their
                            // defaults rely on Self staying a TypeVar for generic
                            // eq inference).
                            self_type = prim_t;
                        }
                    }
                    // Blanket impl `impl<T: Bound> Trait for T {}`: the default
                    // method must be synthesized as a generic fn under the
                    // synthetic `$blanket$...` target (so dispatch can find it)
                    // with Self = the blanket TypeVar, and a blanket_impls_
                    // entry pushed so try_blanket_method_dispatch surfaces it on
                    // any concrete receiver satisfying Bound. Without this, the
                    // trait's defaults are invisible on a blanket impl.
                    std::string def_reg_target = is_blanket ? check_target : target;
                    if (is_blanket)
                        current_type_params_["Self"] = make_typevar(target);
                    else if (self_type)
                        current_type_params_["Self"] = self_type;
                    // Switch holder to the zone that owns the default AST node —
                    // it may live in a different module's zone (cross-module trait).
                    auto* saved_holder = holder_;
                    if (m.default_holder) holder_ = m.default_holder;
                    collect_fn(map_of(m.default_ast), def_reg_target, trait_name);
                    holder_ = saved_holder;
                    if (is_blanket) {
                        BlanketImpl bi_rec;
                        bi_rec.trait_name = trait_name;
                        bi_rec.target_typevar = target;
                        bi_rec.bound_trait = blanket_bound_trait;
                        bi_rec.extra_bounds = blanket_extra_bounds;
                        bi_rec.method_name = m.name;
                        bi_rec.mangled_name = def_reg_target + "__" + m.name;
                        bi_rec.primary_assoc_eqs = blanket_primary_assoc_eqs;
                        bi_rec.extra_assoc_eqs = blanket_extra_assoc_eqs;
                        if (!cur_from_binary_)
                            user_blanket_mangled_.insert(bi_rec.mangled_name);
                        blanket_impls_.push_back(std::move(bi_rec));
                    }
                    // Default trait-method: inherits trait accessibility.
                    // Mark ALL newly-registered overloads as pub (not just first).
                    auto dmangled = def_reg_target + "__" + m.name;
                    for (auto* df : find_func_candidates(
                             def_reg_target + "__" + trait_name + "__" + m.name))
                        const_cast<SemaFuncInfo*>(df)->is_pub = true;
                    for (auto* df : find_func_candidates(dmangled))
                        const_cast<SemaFuncInfo*>(df)->is_pub = true;
                    current_type_params_.erase("Self");
                } else {
                    error(std::format("impl {} for {}: missing method '{}'",
                          trait_name, target, m.name));
                }
            }
        }
    }
    // Check associated type completeness (skipped for blanket impls — the
    // blanket's assoc types are per-instantiation and not keyed by a single
    // target; the LIR body catches mistakes at monomorphization time).
    if (!trait_name.empty() && !is_blanket) {
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (auto& at : tit->second.assoc_types) {
                // G156-1: this impl's assoc types are keyed by the trait's
                // type-args (suffixed); the plain key may have been erased by a
                // sibling dual impl. Check the suffixed key for THIS impl.
                std::string key = trait_name + trait_targ_suffix(trait_type_args)
                                + "::" + target + "::" + at.name;
                if (!assoc_type_impls_.count(key)) {
                    // §3 c13 Wave 9 — an impl that omits an assoc type falls
                    // back to the trait's default (Rust:
                    // `trait T { type Item = i32; }`); only error when the
                    // trait declared NO default and the impl didn't provide.
                    if (at.default_type) {
                        AssocTypeEntry ae;
                        ae.type = at.default_type;
                        assoc_type_impls_[key] = std::move(ae);
                    } else
                        error(std::format("impl {} for {}: missing associated type '{}'",
                              trait_name, target, at.name));
                }
            }
            // Check associated constant completeness
            for (auto& ac : tit->second.assoc_consts) {
                std::string key = trait_name + "::" + target + "::" + ac.name;
                if (!assoc_const_impls_.count(key)) {
                    // §6 f1 Wave 9 — a default value in the trait lets the
                    // impl omit the const (Rust:
                    // `trait T { const X: i32 = 42; }`).
                    if (!ac.has_default)
                        error(std::format("impl {} for {}: missing associated constant '{}'",
                              trait_name, target, ac.name));
                    else
                        // T2-14: project the trait DEFAULT into this impl so
                        // `Target::CONST` (and `Trait::CONST` via the unique
                        // impl) resolve to the default value. value_ast lives
                        // in the trait's holder, same as a same-file impl const
                        // — the access-site lowering reads it through the
                        // current holder exactly like an explicit impl const.
                        assoc_const_impls_[key] =
                            { ac.type, ac.default_value_ast, nullptr, ac.doc };
                }
            }
            // Check unsafe parity
            if (tit->second.is_unsafe && !impl_is_unsafe)
                error(std::format("impl {} for {}: implementing unsafe trait requires `unsafe impl`",
                      trait_name, target));
            if (!tit->second.is_unsafe && impl_is_unsafe && !tit->second.is_auto)
                error(std::format("impl {} for {}: `unsafe impl` for a safe trait",
                      trait_name, target));
        }
    }
    // Register Copy types so is_move_type() can respect them.
    if (trait_name == "Copy" && !target.empty()) {
        if (impl_is_unsafe)
            error(std::format("impl Copy for {}: `unsafe impl` for a safe built-in trait Copy",
                              target));
        // Conditional Copy (`impl<P: Copy> Copy for Pin<P>`, Rust-style):
        // record target type-arg positions bound to a Copy-bounded impl
        // param — the instance is Copy iff each such arg is Copy
        // (struct_type_is_copy). A bound-less param (true blanket) or a
        // non-generic target registers unconditionally as before.
        std::vector<size_t> cond_positions;
        if (target_resolved && !impl_tps.empty()) {
            auto targs = TypeRef(target_resolved).type_args();
            for (size_t i = 0; i < targs.size(); ++i) {
                if (!targs[i] ||
                    TypeRef(targs[i]).kind() != LogosType::Kind::TypeVar)
                    continue;
                auto tvn = std::string(TypeRef(targs[i]).type_var_name());
                for (auto& tp : impl_tps) {
                    if (tp.name != tvn) continue;
                    for (auto& b : tp.bounds)
                        if (b.trait_name == "Copy") {
                            cond_positions.push_back(i);
                            break;
                        }
                    break;
                }
            }
        }
        if (cond_positions.empty())
            copy_types_.insert(target);
        else
            conditional_copy_[target] = std::move(cond_positions);
    }
    // Standalone unsafe impl (no trait) makes no semantic sense.
    if (impl_is_unsafe && trait_name.empty())
        error(std::format("unsafe impl {}: standalone impl cannot be unsafe", target));
    // Clean up trait type params from scope
    if (!trait_name.empty() && !trait_type_args.empty()) {
        auto tit = find_trait_iter_scoped(trait_name);
        if (tit != traits_.end()) {
            for (auto& tp : tit->second.type_params)
                current_type_params_.erase(tp.name);
        }
    }
    // Clean up Self (set at top for all impl blocks)
    current_type_params_.erase("Self");
    // Clean up impl's own type params (pushed at top for standalone generic impl)
    if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
    impl_target_typeref_ = nullptr;
    // Register the impl mapping (only for trait impls)
    if (!trait_name.empty()) {
        SemaImplInfo info{trait_name, target, impl_is_unsafe, impl_is_negative,
                          target_resolved, impl_tps,
                          trait_type_args, trait_lt_args, impl_lt_params,
                          impl_lt_outlives, impl_doc, {}};
        // B91: coherence — reject a second impl of the same trait for the
        // same target type. Only fires for NON-GENERIC impls (no impl type
        // params and no impl lifetime params): two `impl<T> ... for Map<K,V>`
        // forms with different specialization patterns are legitimate and
        // disambiguated downstream. Negative impls (`impl !T for X {}`)
        // are coherence markers, not impls — skip.
        // Coherence-only key: must include the trait's type-arguments —
        // otherwise `impl From<i8> for i32` and `impl From<i16> for i32`
        // would collide on `"From::i32"`. The main impls_ map stays keyed
        // by the bare `Trait::Target` (so bound-check / has_impl /
        // find_impl callers without trait_args still find a hit); coherence
        // duplicate detection consults a SECOND set keyed with args spelled
        // out. impls_ ends up pointing at one of the legitimate variants
        // (last wins on insertion order); per-call resolution that needs
        // the args-specific impl walks all_impls_ instead.
        std::string trait_args_key;
        if (!trait_type_args.empty()) {
            trait_args_key = "[";
            for (size_t i = 0; i < trait_type_args.size(); ++i) {
                if (i) trait_args_key += ",";
                trait_args_key += type_str(trait_type_args[i]);
            }
            trait_args_key += "]";
        }
        // B-mv-02: key coherence by the CANONICAL (scope-resolved) trait name
        // so a user `impl Hash for i32` and the stdlib's own `Hash` impl for the
        // same type are NOT seen as conflicting implementations of one trait
        // (they implement distinct same-name traits). Non-colliding traits
        // resolve to their bare name → unchanged. impls_ stays bare-keyed;
        // dispatch composes the bare chosen_trait + target and the target
        // disambiguates the dispatch entry.
        std::string coh_trait = trait_name.empty() ? trait_name
                                                    : canonical_trait_name(trait_name);
        std::string coh_key = coh_trait + trait_args_key + "::" + target;
        std::string key = trait_name + "::" + target;
        info.canonical_trait = coh_trait;  // for global supertrait verification
        bool is_generic_impl = !impl_tps.empty() || !impl_lt_params.empty();
        if (!impl_is_negative && !is_generic_impl && coherence_keys_.count(coh_key)) {
            error(std::format("conflicting implementations of trait '{}' for type '{}'",
                              trait_name, target));
        }
        if (!is_generic_impl) {
            coherence_keys_.insert(coh_key);
            if (!cur_from_binary_) user_coherence_keys_.insert(coh_key);
        }
        impls_[key] = info;
        impls_all_[key].push_back(info);   // ALL impls (impls_ is last-wins)
        if (!cur_from_binary_) user_impl_keys_.insert(key);
        // `str` is a built-in that resolves to Slice<u8>; type_str() produces
        // "&[u8]" for Slice<u8>, so trait-bound checks look for "Trait::&[u8]".
        // Register an alias entry so satisfaction checks find the impl.
        if (target == "str") {
            SemaImplInfo alias{trait_name, "&[u8]", impl_is_unsafe, impl_is_negative,
                               target_resolved, impl_tps,
                               trait_type_args, trait_lt_args, impl_lt_params,
                               impl_lt_outlives, impl_doc, {}};
            impls_[trait_name + "::&[u8]"] = alias;
            impls_all_[trait_name + "::&[u8]"].push_back(std::move(alias));
        }
    }
}

// Collect a struct specialization into struct_specs_sema_.
// Only full specializations (all patterns concrete) are registered;

void SemaChecker::collect_struct_spec(TinyMapView node,
                                      const StructAttrFlags* attr_flags) {
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {} (specialization)", sname);

    // Parse spec patterns to determine the concrete name.
    std::vector<TypeRef> spec_patterns;
    std::vector<TypeParam> pattern_tvars;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE ||
                        tc == la::SLICE_TYPE || tc == la::UNSIZED_SLICE_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        bool was_uok = unsized_ok_;
                        unsized_ok_ = true;   // [E] pattern = ?Sized position
                        spec_patterns.push_back(resolve_type(tpnode));
                        unsized_ok_ = was_uok;
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        auto known = try_resolve_as_known_type(name);
                        if (known) {
                            spec_patterns.push_back(known);
                        } else {
                            // Partial spec — skip for sema registration.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            pattern_tvars.push_back({std::string(name), {}, false, false, nullptr, nullptr, {}});
                            spec_patterns.push_back(make_typevar(name));
                        }
                    }
                }
            }
        }
    }

    // Register both full and partial specs (e.g. Map<Bitmap, V>): partial
    // specs are matched via find_best_sema_struct_spec at lookup time.
    auto inst_type = make_generic_struct(sname, spec_patterns);
    std::string concrete = concrete_struct_name(inst_type);

    SemaStructInfo info;
    info.package = cur_package_;
    info.module_id = cur_module_id_;
    info.base_name = sname;
    info.spec_patterns = spec_patterns;
    // Pattern-var BOUNDS (bound-discriminated specs): `struct S<T: Copy>`
    // matches only args satisfying the bounds — keep them for the
    // find_best_sema_struct_spec gate. Name-only reader: parametrized bound
    // args may not resolve in this context and the gate needs trait names.
    info.type_params = read_spec_pattern_bounds(node);
    // Structural attribute flags apply to specs exactly like to base structs
    // (#[self_describing] on the [E] spec of a packed array, #[zoned], …).
    if (attr_flags) {
        info.no_auto_drop    |= attr_flags->no_auto_drop;
        info.self_describing |= attr_flags->self_describing;
        info.rel_ptr         |= attr_flags->rel_ptr;
        info.pinned          |= attr_flags->pinned;
        info.zone_mut        |= attr_flags->zone_mut;
        info.zoned2          |= attr_flags->zoned;
        info.borrow_carrying |= attr_flags->borrow_carrying;
        info.non_null        |= attr_flags->non_null;
    }
    std::string spec_field_sweep_doc;
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        uint64_t last_fdef = 0;
        for (uint64_t i = 0; i < fields.size(); ++i)
            if (code_of(map_of(fields.get(i))) == la::FIELD_DEF) last_fdef = i;
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (try_append_doc(spec_field_sweep_doc, fnode)) continue;
            if (code_of(fnode) != la::FIELD_DEF) continue;
            auto fname = str_of(fnode.get(la::NAME.code));
            // Phase 1B-13 parity with the base-struct path: the LAST field
            // may be a `[T]` slice tail (custom DST).
            auto ftype_node = map_of(fnode.get(la::TYPE.code));
            bool is_slice_tail = (i == last_fdef) &&
                                 code_of(ftype_node) == la::UNSIZED_SLICE_TYPE;
            bool was_ok2 = unsized_ok_;
            if (is_slice_tail) unsized_ok_ = true;
            auto ftype = resolve_type(ftype_node);
            unsized_ok_ = was_ok2;
            if (is_slice_tail) info.is_dst = true;
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            info.fields.push_back({fname, ftype, fpub, false, std::move(spec_field_sweep_doc)});
            spec_field_sweep_doc.clear();
        }
    }
    struct_specs_sema_[std::move(concrete)] = std::move(info);

    // Clean up pattern TypeVars.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);
}


void SemaChecker::collect_datatype(TinyMapView node, bool is_annotation_type) {
    auto dname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("datatype {}", dname);
    SemaStructInfo info;
    info.doc = take_pending_doc();
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
    info.package = cur_package_;
    info.module_id = cur_module_id_;
    info.is_annotation_type = is_annotation_type;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);  // §4: `pub(module)`
    push_type_params(info.type_params);
    std::string dt_field_sweep_doc;
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (try_append_doc(dt_field_sweep_doc, fnode)) continue;
            if (code_of(fnode) != la::FIELD_DEF) continue;
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            // Rule 9: datatype fields must be POD-compatible (no heap types).
            // Exception: annotation types are compile-time only and may hold str fields.
            if (!is_annotation_type && ftype && TypeRef(ftype).kind() != LogosType::Kind::Error) {
                auto is_datatype_safe = [&](TypeRef t, auto& self) -> bool {
                    if (!t) return false;
                    switch (TypeRef(t).kind()) {
                    case LogosType::Kind::I8:  case LogosType::Kind::U8:
                    case LogosType::Kind::I16: case LogosType::Kind::U16:
                    case LogosType::Kind::I32: case LogosType::Kind::U32:
                    case LogosType::Kind::I24: case LogosType::Kind::U24:
                    case LogosType::Kind::I56: case LogosType::Kind::U56:
                    case LogosType::Kind::I64: case LogosType::Kind::U64:
                    case LogosType::Kind::I128: case LogosType::Kind::U128:
                    case LogosType::Kind::F32: case LogosType::Kind::F64:
                    case LogosType::Kind::Bool:
                    case LogosType::Kind::IntLit: case LogosType::Kind::FloatLit:
                        return true;
                    case LogosType::Kind::Array:
                        return self(TypeRef(t).elem(), self);
                    case LogosType::Kind::ZonedStruct:
                        return true;  // datatypes in datatypes OK
                    case LogosType::Kind::Struct: {
                        // A #[rel_ptr] self-relative pointer (RelAny / RelPtr<T>) is
                        // POD — an 8-byte offset (target − &field) — and a valid
                        // Writ datatype field: it reaches another tagged object
                        // self-relatively, the never-move-arena analog of legacy's
                        // base-relative inner pointer. Other plain structs stay
                        // disallowed (may carry heap/abs pointers).
                        auto [pkg, ssi] = find_struct_by_name(std::string(TypeRef(t).struct_name()));
                        return ssi && ssi->rel_ptr;
                    }
                    case LogosType::Kind::TypeVar:
                        return true;  // resolved later by mono
                    default:
                        return false;
                    }
                };
                if (!is_datatype_safe(ftype, is_datatype_safe)) {
                    error(std::format("datatype '{}' field '{}': type '{}' is not allowed "
                                      "(only primitives, arrays, and other datatypes)",
                                      dname, fname, type_str(ftype)));
                }
            }
            // DataPlain detection:
            // Bug 2 fix: for concrete (non-generic) Datatype fields, check the nested
            //   type's own is_data_plain flag.  If the nested type is DataPlain (no
            //   relative-pointer fields), embedding it by value does NOT make the outer
            //   type a DataNode.  Types not yet in datatypes_ are treated conservatively
            //   as DataNode (forward-reference or cross-package).
            // Bug 3 fix: arrays whose element type is a DataNode also mark the outer
            //   type as DataNode.
            // For generic Datatype fields (type_args non-empty) we stay conservative
            //   and always mark as DataNode (e.g. RelPtr<Node> contains zone offsets).
            auto marks_data_node = [&](TypeRef ft) -> bool {
                if (!ft) return false;
                // Strip Array wrapper (Bug 3 fix)
                while (TypeRef(ft).kind() == LogosType::Kind::Array) ft = TypeRef(ft).elem();
                TypeRef fv{ft};
                if (fv.kind() == LogosType::Kind::ZonedStruct) {
                    if (!fv.type_args().empty()) return true;  // generic → conservative
                    // Try bare name, then current-package qualified, then pkg_name qualified.
                    auto ndit = datatypes_.find(std::string(fv.struct_name()));
                    if (ndit == datatypes_.end() && !cur_package_.empty())
                        ndit = datatypes_.find(cur_package_ + "::" + std::string(fv.struct_name()));
                    if (ndit == datatypes_.end() && !fv.pkg_name().empty())
                        ndit = datatypes_.find(std::string(fv.pkg_name()) + "::" + std::string(fv.struct_name()));
                    if (ndit == datatypes_.end()) return true; // unknown → conservative
                    return !ndit->second.is_data_plain;
                }
                return false;
            };
            if (marks_data_node(ftype))
                info.is_data_plain = false;
            info.fields.push_back({fname, ftype, fpub, false, std::move(dt_field_sweep_doc)});
            dt_field_sweep_doc.clear();
        }
    }
    auto dkey = sema_key(cur_package_, dname);
    datatypes_[dkey] = std::move(info);
    pop_type_params(datatypes_[dkey].type_params);
}

// ADR 0011 — collect a `schema S : code(expr)? { name: type = key, … }` item.
// A schema registers as a Struct (a typed VIEW) whose ONLY real field is the
// synthetic `m: *const WMap<Wu6,WAny>`. The user's declared fields are recorded
// in schema_fields/schema_keys and surfaced later via desugared get/set sugar.
void SemaChecker::collect_schema(TinyMapView node) {
    if (!node.has_key(la::NAME.code)) return;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("schema {}", sname);

    SemaStructInfo info;
    info.is_schema = true;
    info.doc       = take_pending_doc();
    info.package   = cur_package_;
    info.module_id = cur_module_id_;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);

    // ADR 0011 generics — bind the schema's type params (`schema Box<T: WritField>`)
    // so a field type `val: T` resolves to a TypeVar (not "unknown type T"); popped
    // before registration. Mirrors collect_struct.
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);

    // `code(expr)` clause → schema_type_code. The clause keyword is contextual
    // (a bare IDENT validated == "code" here — `code` stays a usable identifier).
    if (node.has_key(la::CODE_EXPR.code)) {
        auto clause = map_of(node.get(la::CODE_EXPR.code));
        if (clause.has_key(la::NAME.code)) {
            auto kw = std::string(str_of(clause.get(la::NAME.code)));
            if (kw != "code")
                error(std::format("schema '{}': unknown clause '{}(...)' (expected 'code')",
                                  sname, kw));
        }
        if (clause.has_key(la::VALUE.code)) {
            auto r = ctfe_eval_const(map_of(clause.get(la::VALUE.code)), holder_);
            if (!r) error(std::format("schema '{}' code(...): {}", sname, r.error().msg));
            else    info.schema_type_code = static_cast<uint64_t>(r.value().i);
        }
    }
    // Real struct fields (layout matches stdlib WSchemaH): m = TOM pointer view,
    // z = arena allocator (for boxing wide values on write; null for read-only
    // views bound from an erased WAny). Together a 16-byte fat view.
    {
        TypeRef wu6  = make_struct_type("Wu6");
        TypeRef wany = make_enum_type("WAny");
        TypeRef wmap = make_generic_struct("WMap", {wu6, wany});
        info.fields.push_back({std::string_view{"m"}, make_ptr(/*mut=*/false, wmap),
                               /*is_pub=*/false, /*is_variadic=*/false, {}});
        info.fields.push_back({std::string_view{"z"}, make_ptr(/*mut=*/true, prim(LogosType::Kind::U8)),
                               /*is_pub=*/false, /*is_variadic=*/false, {}});
    }

    // Declared sugar fields → schema_fields + parallel schema_keys (TOM keys 0..51).
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        int64_t positional = 0;
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (code_of(fnode) != la::SCHEMA_FIELD_DEF) continue;
            std::string_view fname =
                fnode.has_key(la::NAME) ? str_of(fnode.get(la::NAME.code)) : std::string_view{};
            TypeRef ftype;
            {
                ItemSignatureGuard sig_guard(in_item_signature_);
                ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            }
            int64_t key = positional;
            if (fnode.has_key(la::VALUE.code)) {
                auto r = ctfe_eval_const(map_of(fnode.get(la::VALUE.code)), holder_);
                if (!r) error(std::format("schema '{}' field '{}' key: {}",
                                          sname, std::string(fname), r.error().msg));
                else    key = r.value().i;
            }
            if (key < 0 || key > 51)
                error(std::format("schema '{}' field '{}': key {} out of TOM range 0..51",
                                  sname, std::string(fname), key));
            uint8_t k8 = static_cast<uint8_t>(key < 0 ? 0 : (key > 51 ? 51 : key));
            for (uint8_t prev : info.schema_keys)
                if (prev == k8)
                    error(std::format("schema '{}' field '{}': duplicate key {}",
                                      sname, std::string(fname), static_cast<int>(k8)));
            bool fpub = fnode.has_key(la::IS_PUB) && fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            info.schema_fields.push_back({fname, ftype, fpub, /*is_variadic=*/false, {}});
            info.schema_keys.push_back(k8);
            positional = key + 1;
        }
    }

    pop_type_params(info.type_params);
    structs_[sema_key(cur_package_, sname)] = std::move(info);
}

// ADR 0011 — collect a `schema enum E : category(expr)? { V(S), … }` item.
// Registers a Struct-shaped union view (single synthetic `m` field) flagged
// is_schema_enum, with each variant mapping a name → concrete schema view type.
void SemaChecker::collect_schema_enum(TinyMapView node) {
    if (!node.has_key(la::NAME.code)) return;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("schema enum {}", sname);

    SemaStructInfo info;
    info.is_schema      = true;
    info.is_schema_enum = true;
    info.doc       = take_pending_doc();
    info.package   = cur_package_;
    info.module_id = cur_module_id_;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);

    // ADR 0011 generics — bind type params so variant types `A(Wrap<T>)` resolve T.
    info.type_params = read_type_params(node);
    push_type_params(info.type_params);

    // Optional `category(expr)` clause (contextual keyword == "category").
    if (node.has_key(la::CODE_EXPR.code)) {
        auto clause = map_of(node.get(la::CODE_EXPR.code));
        if (clause.has_key(la::NAME.code)) {
            auto kw = std::string(str_of(clause.get(la::NAME.code)));
            if (kw != "category")
                error(std::format("schema enum '{}': unknown clause '{}(...)' (expected 'category')",
                                  sname, kw));
        }
        if (clause.has_key(la::VALUE.code)) {
            auto r = ctfe_eval_const(map_of(clause.get(la::VALUE.code)), holder_);
            if (!r) error(std::format("schema enum '{}' category(...): {}", sname, r.error().msg));
            else    info.schema_type_code = static_cast<uint64_t>(r.value().i);
        }
    }

    // Real fields {m, z} — same layout as a schema view (TOM ptr + allocator).
    info.fields.push_back({std::string_view{"m"},
        make_ptr(false, make_generic_struct("WMap", {make_struct_type("Wu6"),
                                                     make_enum_type("WAny")})),
        false, false, {}});
    info.fields.push_back({std::string_view{"z"}, make_ptr(true, prim(LogosType::Kind::U8)),
        false, false, {}});

    // Variants: each VARIANT_DEF { NAME, TYPE=concrete schema }.
    if (node.has_key(la::FIELDS)) {
        auto vs = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < vs.size(); ++i) {
            auto v = map_of(vs.get(i));
            if (code_of(v) != la::VARIANT_DEF) continue;
            std::string vname = v.has_key(la::NAME) ? std::string(str_of(v.get(la::NAME.code))) : std::string{};
            TypeRef vty;
            {
                ItemSignatureGuard sig_guard(in_item_signature_);
                vty = resolve_type(map_of(v.get(la::TYPE.code)));
            }
            info.schema_variants.emplace_back(std::move(vname), vty);
        }
    }

    pop_type_params(info.type_params);
    structs_[sema_key(cur_package_, sname)] = std::move(info);
}

void SemaChecker::collect_struct(TinyMapView node) {
    // Struct specialisations don't go into structs_ — they're lowered directly.
    if (is_specialization_struct(node)) return;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {}", sname);
    SemaStructInfo info;
    info.doc = take_pending_doc();
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
    info.lifetime_outlives = read_lifetime_outlives(node);
    {
        auto where_outlives = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : where_outlives) info.lifetime_outlives.push_back(std::move(p));
    }
    info.package = cur_package_;
    info.module_id = cur_module_id_;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);  // §4: `pub(module)`
    push_type_params(info.type_params);
    // Generic tuple-struct (`struct W<T>(T);`) grammar uses `$...` to
    // collect tuple_field results into FIELDS; the same `$...` collector
    // also folds in the rule-call result of `type_param_list?` (peg_gen
    // rcap_var_ has no opt-out for named slots). Filter: real fields are
    // FIELD_DEF nodes; anything else (notably the type_param_list map
    // emitting `{ITEMS: ...}`) is dropped here. TODO: peg_gen-side fix
    // would let an action mark $N positions as "exclude from $..." and
    // remove this workaround.
    auto is_field_def = [&](TinyMapView fnode) {
        return code_of(fnode) == la::FIELD_DEF;
    };
    // B-ts-01: detect tuple-struct shape — first FIELD_DEF has no NAME slot.
    if (node.has_key(la::FIELDS)) {
        auto fs0 = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fs0.size(); ++i) {
            auto f0 = map_of(fs0.get(i));
            if (!is_field_def(f0)) continue;
            if (!f0.has_key(la::NAME)) info.is_tuple_struct = true;
            break;
        }
    }
    // Phase A.2: leftover doc-comment after the FIELDS loop carries over
    // into the methods iteration (greedy `field_def_or_doc*` matcher eats
    // a doc that visually preceded the first method).
    std::string field_sweep_doc;
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        // Phase 1B-13: identify the LAST real FIELD_DEF (custom-DST support
        // permits an unsized type only at this position).
        uint64_t last_field_idx = UINT64_MAX;
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (is_field_def(fnode)) last_field_idx = i;
        }
        uint64_t synth_idx = 0;
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (try_append_doc(field_sweep_doc, fnode)) continue;
            if (!is_field_def(fnode)) continue;
            const uint64_t i_for_synth = synth_idx++;
            // B-ts-01: tuple-struct fields have no NAME slot; synthesise
            // "0", "1", … so member access (`foo.0`) and pattern shape
            // (`Foo(a, b)`) reuse the named-field machinery uniformly.
            std::string_view fname;
            if (fnode.has_key(la::NAME)) {
                fname = str_of(fnode.get(la::NAME.code));
            }
            // B-ts-01: tuple-struct fields carry no NAME slot; the
            // synth digit name ("0", "1", …) is interned via the
            // class-level pool below so the string_view stays valid
            // for the SemaFieldInfo's lifetime.
            if (fname.empty())
                fname = intern_synth_field_name(i_for_synth);
            // Phase 1B-13: at the LAST field position, allow `[T]` (custom
            // DST with slice tail). Narrower than generic unsized_ok_:
            // only flips for UNSIZED_SLICE_TYPE node specifically, so
            // existing DYN_TYPE field semantics (legacy TraitObject form)
            // stay unchanged. Bare `dyn Trait` as DST tail would need
            // separate handling.
            auto ftype_node = map_of(fnode.get(la::TYPE.code));
            bool is_slice_tail = (i == last_field_idx) &&
                                  code_of(ftype_node) == la::UNSIZED_SLICE_TYPE;
            bool was_ok = unsized_ok_;
            if (is_slice_tail) unsized_ok_ = true;
            TypeRef ftype;
            {
                // E0121: struct/union field types are item signatures —
                // `_` rejected here (pre-fix it leaked to a late mlir-gen
                // "unknown field type" failure).
                ItemSignatureGuard sig_guard(in_item_signature_);
                ftype = resolve_type(ftype_node);
            }
            unsized_ok_ = was_ok;
            if (is_slice_tail) info.is_dst = true;
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            bool fvar = false;
            if (fnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = fnode.get(la::IS_VARIADIC.code);
                fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            info.fields.push_back({fname, ftype, fpub, fvar, std::move(field_sweep_doc)});
            field_sweep_doc.clear();
        }
    }
    auto skey = sema_key(cur_package_, sname);
    structs_[skey] = std::move(info);
    // Methods declared inline in the struct body (grammar: `method_def_or_doc*`)
    // are collected exactly like impl methods — so they need the same `Self`
    // binding the impl path establishes (collect_impl, above). Without it,
    // `-> Self` return types fail with "unknown type 'Self'" and the
    // `&self`/`self` receiver shorthand fails with "receiver is not a struct".
    bool had_self = current_type_params_.count("Self") > 0;
    TypeRef saved_self = had_self ? current_type_params_["Self"] : nullptr;
    std::vector<TypeParam> saved_impl_tps = impl_type_params_;
    {
        const auto& sinfo = structs_[skey];
        TypeRef self_type;
        if (!sinfo.type_params.empty()) {
            std::vector<TypeRef> tv_args;
            for (auto& tp : sinfo.type_params)
                tv_args.push_back(make_typevar(tp.name));
            std::vector<std::string> lt_args = sinfo.lifetime_params;
            self_type = make_generic_struct(sname, std::move(tv_args),
                                            std::move(lt_args), cur_package_);
            // collect_fn keys genericity on impl_type_params_ (it combines them
            // into the method's type_params) and routes generic methods to
            // generic_funcs_ — so the static-call resolver substitutes the
            // struct's params at `Pair::<i32,i32>::make()`. Mirror collect_impl.
            impl_type_params_ = sinfo.type_params;
        } else {
            self_type = make_struct_type(sname, cur_package_);
        }
        current_type_params_["Self"] = self_type;
    }
    // Methods must be collected with the struct's type params in scope.
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        // Phase A.2: carry any doc that the greedy FIELDS matcher ate from
        // immediately before the first method. collect_fn consumes pending_doc_
        // at its top.
        pending_doc_ = std::move(field_sweep_doc);
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (try_append_doc(pending_doc_, method)) continue;
            int32_t mc = code_of(method);
            if (mc == la::FN || mc == la::STATIC_FN) collect_fn(method, sname);
        }
    }
    if (had_self) current_type_params_["Self"] = saved_self;
    else current_type_params_.erase("Self");
    impl_type_params_ = std::move(saved_impl_tps);
    pop_type_params(structs_[skey].type_params);
}

TypeRef SemaChecker::try_resolve_as_known_type(std::string_view name) {
    // Type-params bound in current scope (generic-const instantiation, generic
    // fn body, generic struct method) win over global lookups. Ensures that
    // `<type:K>` inside `pub const PMap<K, V>: WritStatic = @{...}` resolves
    // to the substituted concrete type at each use site.
    {
        auto it = current_type_params_.find(std::string(name));
        if (it != current_type_params_.end() && it->second) return it->second;
    }
    if (name == "i32")  return prim(LogosType::Kind::I32);
    if (name == "i64")  return prim(LogosType::Kind::I64);
    if (name == "f64")  return prim(LogosType::Kind::F64);
    if (name == "f32")  return prim(LogosType::Kind::F32);
    if (name == "bool") return prim(LogosType::Kind::Bool);
    if (name == "u8")   return prim(LogosType::Kind::U8);
    if (name == "i8")   return prim(LogosType::Kind::I8);
    if (name == "i16")  return prim(LogosType::Kind::I16);
    if (name == "u16")  return prim(LogosType::Kind::U16);
    if (name == "u32")  return prim(LogosType::Kind::U32);
    if (name == "u64")  return prim(LogosType::Kind::U64);
    if (name == "i24")  return prim(LogosType::Kind::I24);
    if (name == "u24")  return prim(LogosType::Kind::U24);
    if (name == "i56")  return prim(LogosType::Kind::I56);
    if (name == "u56")  return prim(LogosType::Kind::U56);
    if (name == "i128") return prim(LogosType::Kind::I128);
    if (name == "u128") return prim(LogosType::Kind::U128);
    if (name == "usize") return prim(LogosType::Kind::Usize);
    if (name == "isize") return prim(LogosType::Kind::Isize);
    if (name == "char")  return prim(LogosType::Kind::Char);
    if (name == "void") return prim(LogosType::Kind::Void);
    auto ait = type_aliases_.find(std::string(name));
    // Non-generic aliases only: generic aliases are resolved at use sites in resolve_type.
    if (ait != type_aliases_.end() && ait->second.type_params.empty())
        return ait->second.type;
    {
        auto [spkg, ssi] = find_struct_by_name(name);
        if (ssi) return make_struct_type(name, spkg);
    }
    {
        auto [dpkg, dsi] = find_datatype_by_name(name);
        if (dsi) return make_datatype_type(name, dpkg);
    }
    {
        auto [epkg, esi] = find_enum_by_name(name);
        if (esi) return make_enum_type(name, epkg);
    }
    return nullptr;
}

bool SemaChecker::is_known_type_name(std::string_view name) const {
    static constexpr const char* prims[] = {
        "i32","i64","f64","f32","bool","u8","i8","u32","u64","void",
        "i16","u16","i56","u56","i128","u128",nullptr
    };
    for (int i = 0; prims[i]; ++i) if (prims[i] == name) return true;
    if (current_type_params_.count(std::string(name))) return true;
    // is_known_type_name is const — do direct map lookups with qualified key first.
    auto ukey = std::string(name);
    auto has_in = [&](const auto& map) -> bool {
        if (map.count(ukey)) return true;
        if (!cur_package_.empty() && map.count(sema_key(cur_package_, ukey))) return true;
        for (auto& pkg : cur_imports_.wildcard_packages)
            if (map.count(sema_key(pkg, ukey))) return true;
        return false;
    };
    if (has_in(structs_) || has_in(datatypes_) || has_in(enums_)) return true;
    // Type aliases: check unqualified, current package, and imports
    if (type_aliases_.count(ukey)) return true;
    if (!cur_package_.empty() && type_aliases_.count(sema_key(cur_package_, ukey))) return true;
    for (auto& pkg : cur_imports_.wildcard_packages)
        if (type_aliases_.count(sema_key(pkg, ukey))) return true;
    return false;
}

// Walk a type AST node, registering any unresolved IDENT as a TypeVar in

void SemaChecker::extract_typevars_from_type_node(TinyMapView node,
                                         std::vector<TypeParam>& out_tvars) {
    int32_t tc = code_of(node);
    if (tc == la::PTR_TYPE || tc == la::REF_TYPE || tc == la::MUT_REF_TYPE) {
        if (node.has_key(la::POINTEE))
            extract_typevars_from_type_node(
                map_of(node.get(la::POINTEE.code)), out_tvars);
    } else if (tc == la::ARR_TYPE || tc == la::SLICE_TYPE ||
               tc == la::UNSIZED_SLICE_TYPE) {
        if (node.has_key(la::TYPE))
            extract_typevars_from_type_node(
                map_of(node.get(la::TYPE.code)), out_tvars);
    } else if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        if (!is_known_type_name(name) &&
            !current_type_params_.count(std::string(name))) {
            current_type_params_[std::string(name)] = make_typevar(name);
            out_tvars.push_back({std::string(name), {}, false, false, nullptr, nullptr, {}});
        }
    }
}

// Return true when ANY element of the type_param_list is a concrete type

bool SemaChecker::is_specialization_fn(TinyMapView node) {
    if (!node.has_key(la::TYPE_PARAMS)) return false;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return false;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return false;
    auto items = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto n = map_of(items.get(i));
        int32_t c = code_of(n);
        if (c == la::PTR_TYPE || c == la::ARR_TYPE ||
            c == la::SLICE_TYPE || c == la::UNSIZED_SLICE_TYPE)
            return true;  // structured pattern → specialisation ([E] included:
                          // the Sized-partition spec — slices select e.g. the
                          // VLE family of a packed array)
        if (c == la::TYPE_PARAM && !n.has_key(la::ITEMS)) {
            auto name = str_of(n.get(la::NAME.code));
            if (try_resolve_as_known_type(name))
                return true;  // concrete type name → specialisation
            // Pass-0: try_resolve may miss a concrete type whose defining
            // module hasn't been registered yet; the pre-scanned name set
            // makes the classification order-independent.
            if (pass0_decl_names_ && pass0_decl_names_->count(std::string(name)))
                return true;
        }
    }
    return false;
}


bool SemaChecker::is_specialization_struct(TinyMapView node) {
    // §8 fix (Iterator.chain/zip false-spec): the pass0_decl_names_ probe
    // (see is_specialization_fn) misclassifies a GENERIC struct decl
    // whose bare type-param NAME happens to match a user struct in
    // another module. Example: `pub struct ChainIter<A, B, T>` in
    // iter.logos collides with a test file's `struct A` / `struct B`,
    // so ChainIter is flagged as a spec and never registered in
    // structs_ — every later `ChainIter<…>` reference then errors
    // "unknown generic type 'ChainIter'".
    //
    // For a STRUCT decl, a real specialisation by construction matches
    // an EXISTING base of the same name. So gate the pass0 probe on
    // "a struct with this name is already registered" — for `Map<Bitmap,V>`
    // the base `Map<K,V>` is registered first (same module, ordered),
    // and the spec probe falls through normally. For `ChainIter<A,B,T>`
    // no prior `ChainIter` exists → returns false → registered as a
    // fresh generic decl.
    if (!node.has_key(la::TYPE_PARAMS)) return false;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return false;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return false;
    auto items = arr_of(tplist.get(la::ITEMS.code));
    // Gate: structs/datatypes/enums with this name already registered?
    bool base_exists = false;
    bool base_is_schema = false;
    if (node.has_key(la::NAME.code)) {
        auto sname = std::string(str_of(node.get(la::NAME.code)));
        auto key_in_pkg = sema_key(cur_package_, sname);
        auto sit2 = structs_.find(key_in_pkg);
        auto dit2 = datatypes_.find(key_in_pkg);
        base_exists = sit2 != structs_.end() || dit2 != datatypes_.end();
        // A generic SCHEMA materializes same-named struct decls via metaprog;
        // those are the schema's backing, not specializations of it.
        if (sit2 != structs_.end() && sit2->second.is_schema) base_is_schema = true;
        if (dit2 != datatypes_.end() && dit2->second.is_schema) base_is_schema = true;
    }
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto n = map_of(items.get(i));
        int32_t c = code_of(n);
        if (c == la::PTR_TYPE || c == la::ARR_TYPE ||
            c == la::SLICE_TYPE || c == la::UNSIZED_SLICE_TYPE)
            return true;  // structured pattern → specialisation ([E] included:
                          // the Sized-partition spec — slices select e.g. the
                          // VLE family of a packed array)
        if (c == la::TYPE_PARAM && !n.has_key(la::ITEMS)) {
            auto name = str_of(n.get(la::NAME.code));
            if (try_resolve_as_known_type(name))
                return true;  // primitive name → specialisation
            if (base_exists && pass0_decl_names_
                && pass0_decl_names_->count(std::string(name)))
                return true;  // user-type name AND base exists → spec
        }
        bool is_first_decl = false;
        if (node.has_key(la::NAME.code)) {
            auto sname2 = std::string(str_of(node.get(la::NAME.code)));
            auto key2 = sema_key(cur_package_, sname2);
            auto fit2 = first_struct_decl_.find(key2);
            uint32_t off2 = static_cast<uint32_t>(node.offset().value());
            if (fit2 != first_struct_decl_.end() &&
                fit2->second.first == static_cast<void*>(holder_) &&
                fit2->second.second == off2)
                is_first_decl = true;
        }
        if (c == la::TYPE_PARAM && n.has_key(la::ITEMS) && base_exists &&
            !base_is_schema && !is_first_decl) {
            // Bound-discriminated spec: a SECOND same-name decl whose param
            // carries real trait bounds (`struct PkdArray<T: Copy + Fst>`
            // after `struct PkdArray<T: ?Sized>`) specializes by the
            // PROPERTIES of T — the C++ `is_fse<T>` predicate as a bound
            // set. `?Trait` relaxed markers don't count (the base decl
            // itself may say `?Sized`).
            auto bitems = arr_of(n.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bitems.size(); ++b) {
                auto bn = map_of(bitems.get(b));
                if (code_of(bn) != la::TRAIT_BOUND) continue;
                AnyVal rv = bn.get(la::RELAXED.code);
                bool relaxed = !rv.is_null() && rv.is_value() &&
                               rv.as_value<uint8_t>() != 0;
                if (!relaxed) return true;
            }
        }
    }
    return false;
}

// Stage E direct-build: like lower_struct_def but for specialization defs.
// Builds the spec's struct mirror STRAIGHT into the program WritCtr via
// DeclBuilder (no Draft) and returns a DeclBuilder the caller finalizes
// (doc/flags/type_code) and pushes into prog.struct_specializations.
DeclBuilder SemaChecker::lower_spec_struct(TinyMapView node) {
    namespace stk = lir_schema::struct_keys;
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {} (specialization)", sname);

    DeclBuilder sd(*cur_prog_, lir_schema::decl::Code::Struct, /*cap=*/40);
    sd.str_always(stk::NAME, sname);
    sd.str(stk::PKG, cur_package_);
    sd.flag(stk::IS_SPECIALIZATION, true);
    if (cur_from_binary_) sd.flag(stk::FROM_BINARY_MODULE, true);

    // Parse spec type-param list: populate spec_patterns and TypeVar scope.
    std::vector<TypeParam> pattern_tvars;
    std::vector<TypeRef>   spec_patterns;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);
                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE ||
                        tc == la::SLICE_TYPE || tc == la::UNSIZED_SLICE_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        bool was_uok = unsized_ok_;
                        unsized_ok_ = true;   // [E] pattern = ?Sized position
                        spec_patterns.push_back(resolve_type(tpnode));
                        unsized_ok_ = was_uok;
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        if (tpnode.has_key(la::ITEMS)) {
                            // TypeVar with bounds — stays TypeVar in pattern.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            TypeParam tp; tp.name = std::string(name);
                            pattern_tvars.push_back(std::move(tp));
                            spec_patterns.push_back(make_typevar(name));
                        } else {
                            auto known = try_resolve_as_known_type(name);
                            if (known) {
                                spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] = make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}, false, false, nullptr, nullptr, {}});
                                spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }
    if (!spec_patterns.empty()) {
        auto pa = sd.array(stk::SPEC_PATTERNS);
        for (auto t : spec_patterns) pa.push_type(t);
    }
    // Bound-discriminated specs: persist the pattern vars' bounds so mono's
    // find_best_struct_spec (and the Stage E impl→spec matcher) can gate
    // selection on bound satisfaction.
    {
        auto tps_full = read_spec_pattern_bounds(node);
        bool any_bounds = false;
        for (auto& tp : tps_full) if (!tp.bounds.empty()) { any_bounds = true; break; }
        if (any_bounds) {
            auto ta2 = sd.array(stk::TYPE_PARAMS);
            for (auto& tp : tps_full) ta2.push_fn_tparam(tp);
        }
    }

    // Lower fields (TypeVars from patterns now in scope). The grammar's
    // `field_def_or_doc*` interleaves `///` DOC nodes with real FIELD_DEFs — skip
    // the docs (accumulating them as F_DOC) and any other stray node, exactly as
    // the sema-info spec path does (collect_struct_spec). Without the guard a
    // field `///` reaches resolve_type as a null TYPE → "unexpected type node
    // code -1" during specialization lowering.
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        if (fields.size() > 0) {
            auto fa = sd.array(stk::FIELDS);
            std::string field_doc;
            uint64_t last_fdef = 0;
            for (uint64_t i = 0; i < fields.size(); ++i)
                if (code_of(map_of(fields.get(i))) == la::FIELD_DEF) last_fdef = i;
            for (uint64_t i = 0; i < fields.size(); ++i) {
                auto fnode = map_of(fields.get(i));
                if (try_append_doc(field_doc, fnode)) continue;
                if (code_of(fnode) != la::FIELD_DEF) continue;
                auto fname = str_of(fnode.get(la::NAME.code));
                // Last field may be a `[T]` slice tail (custom DST) —
                // Phase 1B-13 parity with lower_struct_def.
                auto ftype_node = map_of(fnode.get(la::TYPE.code));
                bool is_slice_tail = (i == last_fdef) &&
                                     code_of(ftype_node) == la::UNSIZED_SLICE_TYPE;
                bool was_ok2 = unsized_ok_;
                if (is_slice_tail) unsized_ok_ = true;
                auto ftype = resolve_type(ftype_node);
                unsized_ok_ = was_ok2;
                lir::LField fld{std::string(fname), ftype, false, std::move(field_doc)};
                fa.push_field(fld);
                field_doc.clear();
            }
        }
    }

    // METHODS array ALWAYS created (even empty) so in-place appends work later.
    auto ma = sd.array(stk::METHODS);
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (code_of(method) == la::FN) {
                auto mfn = lower_fn(method, sname);
                ma.push_ref(mfn.view<lir_view::FunctionView>().self.addr());
            }
        }
    }

    // Clean up pattern TypeVars.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);

    return sd;
}

// ── lower_spec_fn ─────────────────────────────────────────────
// Like lower_fn but for specialisation definitions.
// Populates spec_patterns and routes the result to prog.specialisations.

DeclBuilder SemaChecker::lower_spec_fn(TinyMapView node) {
    namespace dk = lir_schema::decl_keys;
    auto raw_name = str_of(node.get(la::NAME.code));
    ctx_ = std::format("fn {} (specialization)", raw_name);
    node_line_ = get_line(node);

    // Direct-build the Func decl mirror STRAIGHT into the program WritCtr.
    DeclBuilder fn(*cur_prog_, lir_schema::decl::Code::Func, /*cap=*/40);
    fn.str_always(dk::NAME, std::string(raw_name));
    fn.str(dk::DOC, take_pending_doc());
    fn.flag(dk::IS_SPECIALIZATION, true);
    if (cur_from_binary_) fn.flag(dk::FROM_BINARY_MODULE, true);
    if (cur_from_lazy_)   fn.flag(dk::FROM_LAZY_MODULE, true);

    // Parse spec type-param list: populate spec_patterns and scope TypeVars.
    std::vector<TypeParam> pattern_tvars;
    std::vector<TypeRef> spec_patterns;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto tpitems = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < tpitems.size(); ++i) {
                    auto tpnode = map_of(tpitems.get(i));
                    int32_t tc  = code_of(tpnode);

                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE ||
                        tc == la::SLICE_TYPE || tc == la::UNSIZED_SLICE_TYPE) {
                        // Structured pattern: extract TypeVars then resolve.
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        bool was_uok = unsized_ok_;
                        unsized_ok_ = true;   // [E] pattern = ?Sized position
                        spec_patterns.push_back(resolve_type(tpnode));
                        unsized_ok_ = was_uok;

                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        if (tpnode.has_key(la::ITEMS)) {
                            // TypeVar with bounds — still a TypeVar in patterns.
                            current_type_params_[std::string(name)] =
                                make_typevar(name);
                            TypeParam tp; tp.name = std::string(name);
                            // Read bounds for completeness.
                            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
                            for (uint64_t b = 0; b < bounds.size(); ++b) {
                                auto bn = map_of(bounds.get(b));
                                if (code_of(bn) == la::TRAIT_BOUND)
                                    tp.bounds.push_back(
                                        {std::string(str_of(bn.get(la::NAME.code))), {}, {}, {}, {}, {}});
                            }
                            pattern_tvars.push_back(std::move(tp));
                            spec_patterns.push_back(make_typevar(name));
                        } else {
                            // Plain IDENT: known type → concrete; else → TypeVar.
                            auto known = try_resolve_as_known_type(name);
                            if (known) {
                                spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] =
                                    make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}, false, false, nullptr, nullptr, {}});
                                spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }
    if (!spec_patterns.empty()) {
        auto a = fn.array(dk::SPEC_PATTERNS);
        for (auto& t : spec_patterns) a.push_type(t);
    }

    // Resolve params and return type (TypeVars from patterns are now in scope).
    TypeRef ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
        : void_t();
    ret_type_ = ret_type;
    fn.type(dk::RET_TYPE, ret_type);

    scope_.clear();
    push_scope();

    std::vector<lir::LParam> params;
    if (node.has_key(la::PARAMS)) {
        auto params_av = node.get(la::PARAMS.code);
        if (params_av.is_pointer()) {
            auto params_node = map_of(params_av);
            if (params_node.has_key(la::ITEMS)) {
                auto arr = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arr.size(); ++i) {
                    auto p = map_of(arr.get(i));
                    if (code_of(p) != la::PARAM) continue;
                    auto pname = str_of(p.get(la::NAME.code));
                    TypeRef ptype;
                    if (p.has_key(la::IS_REF)) {
                        auto sit = current_type_params_.find("Self");
                        auto self_t = sit != current_type_params_.end() ? sit->second : error_t();
                        bool is_mut = !p.get(la::IS_MUT.code).is_null();
                        ptype = make_ref(is_mut, self_t);
                    } else {
                        ptype = resolve_type(map_of(p.get(la::TYPE.code)));
                    }
                    define(pname, ptype);
                    params.push_back({std::string(pname), ptype});
                }
            }
        }
    }
    if (!params.empty()) {
        auto a = fn.array(dk::PARAMS);
        for (auto& p : params) a.push_param(p);
    }

    if (node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        // B-fn-06: TAIL_EXPR acts as implicit return inside fn-body lowering
        // and the reachability check.
        bool saved_tail = tail_as_return_;
        tail_as_return_ = true;
        fn.block(dk::BODY, lower_block(body_node));
        if (ret_type && TypeRef(ret_type).kind() != LogosType::Kind::Void &&
            TypeRef(ret_type).kind() != LogosType::Kind::Error &&
            !block_always_returns(body_node)) {
            error("not all paths return a value");
        }
        tail_as_return_ = saved_tail;
    }

    pop_scope();

    // Remove pattern TypeVars from scope.
    for (auto& tp : pattern_tvars)
        current_type_params_.erase(tp.name);

    return fn;
}

void SemaChecker::collect_fn(TinyMapView node, std::string_view struct_ctx,
                             std::string_view trait_ctx) {
    auto raw_name = str_of(node.get(la::NAME.code));
    std::string base_name = struct_ctx.empty()
        ? std::string(raw_name)
        : std::string(struct_ctx) + "__" + std::string(raw_name);
    ctx_ = std::format("fn {}", base_name);

    // Specialisations are validated and lowered inline by lower_spec_fn;
    // skip collection-phase registration entirely.
    if (is_specialization_fn(node)) {
        // B-gn-08: warn when a method's bare-IDENT type-param happens to
        // match an impl-block-level type-param. is_specialization_fn returns
        // true here because try_resolve_as_known_type sees the impl's pushed
        // T — but the user almost certainly meant a fresh method-level T,
        // not an "implicit specialisation" on the impl's T. Diagnose at the
        // collect site (impl_type_params_ is still populated; lower_spec_fn
        // runs later in a different scope and can't see them).
        if (!impl_type_params_.empty() && node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        auto tpnode = map_of(items.get(i));
                        if (code_of(tpnode) != la::TYPE_PARAM) continue;
                        if (tpnode.has_key(la::ITEMS)) continue;  // bounded → not a spec leg
                        auto tpname = std::string(str_of(tpnode.get(la::NAME.code)));
                        for (auto& itp : impl_type_params_) {
                            if (itp.name == tpname) {
                                warn(std::format(
                                    "type parameter '{}' on method '{}' "
                                    "shadows the impl-block's '{}'; the method "
                                    "is silently treated as a specialisation "
                                    "on the impl's '{}'. Rename one if that "
                                    "wasn't intended.",
                                    tpname, base_name, itp.name, itp.name));
                                break;
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    SemaFuncInfo info;
    info.trait_name = std::string(trait_ctx);
    // G156-1: carry the impl's concrete trait type-args (when this method is
    // collected within the current trait-impl) so collision detection + mangling
    // can distinguish `impl Trait<u64> for X` from `impl Trait<u8> for X`.
    if (!trait_ctx.empty() && std::string(trait_ctx) == current_impl_trait_name_)
        info.trait_type_args = current_impl_trait_args_;
    // Free fn: pending_doc_ holds module-level doc. Methods (collect_fn called
    // from collect_impl/collect_trait/collect_struct method loops) — caller
    // has already cleared pending_doc_ via take_pending_doc(), so this is a
    // no-op there. Method-level docs are Phase A.2.
    info.doc = take_pending_doc();
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
    // B69: capture fn's declared outlives bounds (header + where) so
    // call-site cross-check can validate caller satisfies callee's
    // where-clauses under arg-type lifetime substitution.
    info.lifetime_outlives = read_lifetime_outlives(node);
    {
        auto w = read_lifetime_outlives_from(node, la::WHERE.code);
        for (auto& p : w) info.lifetime_outlives.push_back(std::move(p));
    }
    info.base_name = base_name;
    info.source_file = file_;
    info.package = cur_package_;
    info.module_id = cur_module_id_;
    // B-gn-05: warn when a type-param shadows a known type/trait — this
    // currently breaks fn-name resolution at the call site, surfacing as
    // a misleading "undefined function" error.  Use the qualified
    // find_*_by_name lookups so cur_package_-qualified entries are seen.
    for (auto& tp : info.type_params) {
        bool shadows = find_struct_by_name(tp.name).second   != nullptr ||
                       find_datatype_by_name(tp.name).second != nullptr ||
                       find_enum_by_name(tp.name).second     != nullptr ||
                       find_trait_by_name(tp.name).second    != nullptr;
        if (shadows) {
            warn(std::format(
                "type parameter '{}' shadows an existing type / trait — "
                "this currently breaks fn-name resolution at use sites; "
                "rename the type parameter", tp.name));
        }
    }

    auto read_param_types = [&]() {
        if (!node.has_key(la::PARAMS)) return;
        auto params_av = node.get(la::PARAMS.code);
        if (!params_av.is_pointer()) return;
        auto params_node = map_of(params_av);
        if (!params_node.has_key(la::ITEMS)) return;
        auto arr = arr_of(params_node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arr.size(); ++i) {
            auto p = map_of(arr.get(i));
            if (code_of(p) != la::PARAM) continue;
            TypeRef pt;
            if (p.has_key(la::IS_REF)) {
                auto sit = current_type_params_.find("Self");
                auto self_t = sit != current_type_params_.end() ? sit->second : error_t();
                bool is_mut = !p.get(la::IS_MUT.code).is_null();
                pt = make_ref(is_mut, self_t);
            } else {
                pt = p.has_key(la::TYPE) ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
            }
            info.param_types.push_back(pt);
        }
    };

    push_type_params(info.type_params);
    // g4/K5: desugar `impl Trait` PARAMS into synthetic generic type-params.
    impl_param_desugar_active_ = true;
    pending_impl_trait_params_.clear();
    {
        // E0121: `_` is rejected in fn-signature type positions.
        ItemSignatureGuard sig_guard(in_item_signature_);
        read_param_types();
    }
    impl_param_desugar_active_ = false;
    for (auto& tp : pending_impl_trait_params_) info.type_params.push_back(tp);
    pending_impl_trait_params_.clear();
    {
        ItemSignatureGuard sig_guard(in_item_signature_);
        info.ret_type = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
    }
    // logos-core 1.1: precompute "body always diverges" for the
    // Rust-2024 `!`-fallback rule at infer_type_args. Cheap AST walk over
    // the body's last-stmt — see body_always_diverges_simple.
    if (node.has_key(la::BODY)) {
        info.body_always_diverges =
            body_always_diverges_simple(map_of(node.get(la::BODY.code)));
    }
    if (node.has_key(la::IS_VARARG)) {
        AnyVal av = node.get(la::IS_VARARG.code);
        info.is_vararg = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    if (code_of(node) == la::EXTERN_FN) {
        info.is_pub = true;
        info.is_unsafe = true;
        info.is_extern = true;
    } else if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_module_only = read_module_vis(node);  // §4: `pub(module)`
    if (node.has_key(la::IS_UNSAFE)) {
        AnyVal av = node.get(la::IS_UNSAFE.code);
        info.is_unsafe = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    info.is_fn_macro = pending_fn_macro_;
    info.is_token_macro = pending_token_macro_;
    pending_fn_macro_ = false;
    pending_token_macro_ = false;
    pop_type_params(info.type_params);
    if (!impl_type_params_.empty()) {
        auto combined = impl_type_params_;
        combined.insert(combined.end(), info.type_params.begin(), info.type_params.end());
        info.type_params = std::move(combined);
        // CP-cm-16 follow-up: stamp impl-target pattern onto methods of
        // generic impl blocks (impl_type_params_ non-empty). Null for
        // non-generic impls / blanket impls / primitive targets / etc.
        info.impl_target_pattern = impl_target_typeref_;
    }

    info.signature_key = function_signature_key(base_name, info.param_types, info.is_vararg);
    info.symbol_name = base_name;

    // Extern declarations are ABI symbols, not overloadable implementation
    // names.  Keep the raw callee name stable so repeated declarations across
    // stdlib modules (e.g. `malloc` / `free`) still link to the same libc
    // symbol instead of being mangled into `malloc__f__i64`.
    if (info.is_extern) {
        auto& overloads = func_overloads_[base_name];
        for (auto& sym : overloads) {
            auto fit = funcs_.find(sym);
            if (fit != funcs_.end() && fit->second.signature_key == info.signature_key)
                return;
        }
        overloads.push_back(base_name);
        funcs_[base_name] = std::move(info);
        return;
    }

    // Trait-aware method mangling: when two distinct traits define a method
    // with the same name + signature on the same type, Logos's flat
    // `<target>__<method>` registry would reject the second as a duplicate.
    // Detect the collision and lazily re-key the colliding methods under the
    // trait-qualified base `<target>__<trait>__<method>` so both coexist.
    // The trait is threaded to the call site (TypeVar-bound dispatch emits a
    // `$traitqual$` sentinel that mono resolves). Non-colliding methods stay
    // on the plain base — byte-identical to before — so the hot path and the
    // ~30 plain lookup sites are unaffected for the common case.
    if (!info.trait_name.empty() && struct_ctx.size() && raw_name.size()) {
        const std::string plain_base = base_name;
        // G156-1: fold the trait's concrete type-args into the qualified base so
        // `impl Trait<u64> for X` and `impl Trait<u8> for X` mangle distinctly
        // (`X__Trait$u64__m` vs `X__Trait$u8__m`). No args → bare `X__Trait__m`.
        auto qual_for = [&](const std::string& tname, const std::vector<TypeRef>& targs) {
            return std::string(struct_ctx) + "__" + tname + trait_targ_suffix(targs)
                   + "__" + std::string(raw_name);
        };
        const std::string qual_base = qual_for(info.trait_name, info.trait_type_args);
        bool generic = !info.type_params.empty();
        auto& ov  = generic ? generic_overloads_ : func_overloads_;
        auto& tbl = generic ? generic_funcs_     : funcs_;
        const std::string plain_sig =
            function_signature_key(plain_base, info.param_types, info.is_vararg);
        auto args_differ = [&](const std::vector<TypeRef>& a,
                               const std::vector<TypeRef>& b) {
            if (a.size() != b.size()) return true;
            for (size_t i = 0; i < a.size(); ++i)
                if (type_str(a[i] ? a[i] : error_t()) != type_str(b[i] ? b[i] : error_t()))
                    return true;
            return false;
        };

        bool already = trait_method_registry_.count(plain_base) > 0;
        std::string clash_sym;
        bool clash_inherent = false;   // G156-5: an INHERENT method of the same
                                       // name+sig already holds the plain base.
        if (!already) {
            if (auto oit = ov.find(plain_base); oit != ov.end()) {
                for (auto& sym : oit->second) {
                    auto fit = tbl.find(sym);
                    if (fit == tbl.end()) continue;
                    std::string esig = function_signature_key(
                        plain_base, fit->second.param_types, fit->second.is_vararg);
                    if (esig != plain_sig) continue;
                    if (fit->second.trait_name.empty()) {
                        clash_inherent = true;   // keep inherent; qualify trait
                    } else if (fit->second.trait_name != info.trait_name ||
                               // G156-1: same trait NAME, distinct trait type-args.
                               args_differ(fit->second.trait_type_args,
                                           info.trait_type_args)) {
                        clash_sym = sym; break;
                    }
                }
            }
        }
        if (clash_inherent && clash_sym.empty()) {
            // G156-5: Rust prefers the inherent method over a same-named trait
            // method. Leave the inherent on the plain base (concrete-receiver
            // dispatch finds it directly) and qualify ONLY this trait method so
            // both coexist. Record the trait in the registry so a `T: Trait`
            // BOUND dispatch resolves to the qualified `<type>__<trait>__<m>`
            // (not the inherent on the plain base). With a single trait the
            // registry size stays 1 → no disambiguation error for concrete.
            {
                auto& traits = trait_method_registry_[plain_base];
                if (std::find(traits.begin(), traits.end(), info.trait_name) == traits.end())
                    traits.push_back(info.trait_name);
            }
            base_name      = qual_base;
            info.base_name = qual_base;
        } else if (already || !clash_sym.empty()) {
            // First collision: re-key the pre-existing plain entry to its
            // own trait-qualified base, and pull it out of the plain index.
            if (!clash_sym.empty()) {
                if (auto fit = tbl.find(clash_sym); fit != tbl.end()) {
                    SemaFuncInfo ex = std::move(fit->second);
                    tbl.erase(fit);
                    std::string ex_trait = ex.trait_name;
                    // Re-key WITH the existing method's own trait type-args, so a
                    // same-trait-different-args clash (G156-1) doesn't re-key both
                    // to the same bare `X__Trait__m` and re-collide.
                    std::string ex_qual = qual_for(ex_trait, ex.trait_type_args);
                    ex.base_name   = ex_qual;
                    ex.symbol_name = function_symbol_name(ex_qual, ex);
                    auto& plist = ov[plain_base];
                    plist.erase(std::remove(plist.begin(), plist.end(), clash_sym),
                                plist.end());
                    ov[ex_qual].push_back(ex.symbol_name);
                    tbl[ex.symbol_name] = std::move(ex);
                    trait_method_registry_[plain_base].push_back(ex_trait);
                }
            }
            // Register a registry marker for this trait (plain base → traits).
            {
                auto& traits = trait_method_registry_[plain_base];
                if (std::find(traits.begin(), traits.end(), info.trait_name) == traits.end())
                    traits.push_back(info.trait_name);
            }
            // Re-target this method onto its qualified base; the normal
            // registration below recomputes symbol_name from it.
            base_name = qual_base;
            info.base_name = qual_base;
        }
    }

    if (!info.type_params.empty()) {
        info.symbol_name = function_symbol_name(base_name, info);
        auto& gen_overloads = generic_overloads_[base_name];
        for (auto& sym : gen_overloads) {
            auto it = generic_funcs_.find(sym);
            if (it == generic_funcs_.end()) continue;
            // Pkg-qualified mangling: cross-pkg same-base+sig produce
            // distinct symbol_names → coexist. Duplicate flagged only on
            // exact symbol_name match (same pkg, base, signature).
            if (it->second.symbol_name == info.symbol_name) {
                if (code_of(node) == la::EXTERN_FN && it->second.is_extern)
                    return;
                error(std::format("duplicate function '{}'", base_name));
                return;
            }
        }
        gen_overloads.push_back(info.symbol_name);
        generic_funcs_[info.symbol_name] = std::move(info);
        return;
    }

    // `main` is the program entry — ld.lld looks for the bare symbol.
    // `__metacall_thunk_*` are JIT-resolved by bare name in main.cpp.
    // `#[no_mangle]` suppresses pkg+sig mangling so a fn called from
    // inline asm / extern "C" callers keeps its bare name.
    bool is_runtime_abi = (base_name == "main") || pending_no_mangle_ ||
                          base_name.rfind("__metacall_thunk_", 0) == 0;
    pending_no_mangle_ = false;
    auto& overloads = func_overloads_[base_name];
    if (!is_runtime_abi) {
        info.symbol_name = function_symbol_name(base_name, info);
    }
    for (auto& sym : overloads) {
        auto fit = funcs_.find(sym);
        if (fit == funcs_.end()) continue;
        if (fit->second.symbol_name == info.symbol_name) {
            if (code_of(node) == la::EXTERN_FN && fit->second.is_extern)
                return;
            error(std::format("duplicate function '{}'", base_name));
            return;
        }
    }

    overloads.push_back(info.symbol_name);
    funcs_[info.symbol_name] = std::move(info);
}


// ---------------------------------------------------------------------------
// check_supertrait_impls — deferred supertrait impl completeness check
//
// For every registered impl "Trait::Type", walk Trait's supertrait chain and
// verify that a corresponding impl "SuperTrait::Type" also exists.
// This is deferred (not inline in collect_impl) so that ordering within a
// file or across files does not matter.
// ---------------------------------------------------------------------------
// ── Supertrait-closure vtable layout (single source of truth) ────────────
// Flatten a trait's transitive supertrait graph into the dyn-Trait vtable slot
// order. Post-order DFS over supertraits (deepest ancestors first, deduped),
// each trait contributing its OWN methods; the root trait's own methods come
// last. A method's position here is its vtable slot index (+3 header at
// codegen). `upcast_supers` collects the transitive supertraits (every visited
// trait except the root) in the same deepest-first order — one stored
// super-vtable-pointer slot is emitted per entry after the methods, and an
// upcast `&dyn Sub → &dyn Super` indexes it by position. Both sema dispatch and
// mlir-gen's vtable builder consume this, so the ordering cannot drift.
void SemaChecker::trait_vtable_layout(
        const std::string& trait,
        std::vector<std::pair<std::string, const SemaTraitMethodInfo*>>& method_order,
        std::vector<std::string>& upcast_supers) {
    logos::compiler::StrSet seen;
    std::function<void(const std::string&)> walk = [&](const std::string& tn) {
        if (!seen.insert(tn).second) return;
        // ADV1-H: resolve scope-aware (cur_package::name first), so a trait OR a
        // SUPERTRAIT whose bare name shadows a prelude/imported one (e.g. user
        // `Sub: Add` both shadowing operators) walks the USER traits' methods,
        // not the incumbents'. No-op for non-colliding names (falls to bare).
        auto it = find_trait_iter_scoped(tn);
        if (it == traits_.end()) return;
        for (auto& s : it->second.supertraits) {
            if (s.trait_name == "Copy") continue;   // marker, no vtable
            walk(s.trait_name);
        }
        if (tn != trait) upcast_supers.push_back(tn);
        for (auto& m : it->second.methods)
            method_order.push_back({tn, &m});
    };
    walk(trait);
}

void SemaChecker::check_supertrait_impls() {
    // Bug 5: Validate that all supertrait names refer to known traits.
    // This pass must iterate over traits_ (not impls_) so that traits defined
    // but never implemented are also checked — check_supertrait_impls via impls_
    // would silently miss them.
    for (auto& [tname, tinfo] : traits_) {
        for (auto& super : tinfo.supertraits) {
            if (super.trait_name == "Copy") continue;
            if (!traits_.count(super.trait_name)) {
                ctx_ = std::format("trait {}", tname);
                error(std::format("trait {}: unknown supertrait '{}'",
                                  tname, super.trait_name));
            }
        }
    }

    // Does `start` reach `goal` through its supertrait chain?
    std::function<bool(const std::string&, const std::string&,
                       logos::compiler::StrSet&)> trait_has_supertrait =
        [&](const std::string& start, const std::string& goal,
            logos::compiler::StrSet& seen) -> bool {
        if (!seen.insert(start).second) return false;
        auto it = traits_.find(start);
        if (it == traits_.end()) return false;
        for (auto& s : it->second.supertraits) {
            if (s.trait_name == goal) return true;
            if (trait_has_supertrait(s.trait_name, goal, seen)) return true;
        }
        return false;
    };

    // For every registered impl "Trait::Type", walk Trait's supertrait chain and
    // verify that a corresponding impl "SuperTrait::Type" also exists.
    for (auto& [key, impl] : impls_) {
        const std::string& tname  = impl.trait_name;
        const std::string& target = impl.target_type;
        // B-mv-02: resolve the impl's OWN trait (captured canonically at collect
        // time), not whatever same-name trait holds the bare slot — otherwise a
        // user `impl Container for Foo` would be checked against a same-named
        // stdlib trait's supertraits (e.g. fabric::Container: Datatype).
        auto tit = traits_.find(impl.canonical_trait.empty() ? tname
                                                             : impl.canonical_trait);
        if (tit == traits_.end()) continue;
        ctx_ = std::format("impl {} for {}", tname, target);  // set once per impl
        for (auto& super : tit->second.supertraits) {
            if (super.trait_name == "Copy") continue;
            // Bug 3: verify supertrait name is a known trait before checking impls.
            if (!traits_.count(super.trait_name)) continue;  // already reported above
            std::string super_key = super.trait_name + "::" + target;
            if (impls_.count(super_key)) continue;
            // Blanket-derived supertrait satisfaction: if a blanket
            // `impl<T: BoundTrait> SuperTrait for T` exists and `target`
            // implements `BoundTrait` (directly or via another blanket),
            // then `target` transitively implements `SuperTrait`. Mirrors
            // the blanket-aware lookup in check_type_bounds (line ~282).
            bool via_blanket = false;
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != super.trait_name) continue;
                logos::compiler::StrSet seen_pri;
                if (!bi.bound_trait.empty() &&
                    !sema_has_impl_recursive(bi.bound_trait, target, "", seen_pri)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds) {
                    logos::compiler::StrSet seen_eb;
                    if (!sema_has_impl_recursive(eb, target, "", seen_eb)) { all_extra = false; break; }
                }
                if (all_extra) { via_blanket = true; break; }
            }
            if (via_blanket) continue;
            // Blanket impl over a bounded type-param: for
            // `impl<T: Super> Child for T {}`, the supertrait requirement
            // `T: Super` is discharged by the impl's own where-clause bound
            // on T. Check whether `target` is one of this impl's type-params
            // and one of its bounds is (or supertrait-transitively reaches)
            // the required supertrait.
            bool via_self_bound = false;
            for (auto& tp : impl.impl_type_params) {
                if (tp.name != target) continue;
                for (auto& b : tp.bounds) {
                    // direct match, or the bound trait's own supertrait chain
                    // includes the requirement.
                    if (b.trait_name == super.trait_name) { via_self_bound = true; break; }
                    logos::compiler::StrSet seen_super;
                    if (trait_has_supertrait(b.trait_name, super.trait_name, seen_super)) {
                        via_self_bound = true; break;
                    }
                }
                if (via_self_bound) break;
            }
            if (via_self_bound) continue;
            error(std::format("impl {} for {}: missing impl {} for {} (required by supertrait)",
                              tname, target, super.trait_name, target));
        }
    }
}

} // namespace logos::compiler
