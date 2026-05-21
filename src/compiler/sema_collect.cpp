// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include "ctfe.hpp"

#include <cstdio>
#include <format>
#include <functional>

#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/type_tag.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// MC2.5: ODR-style structural equality between two AST sub-trees rooted at
// AnyVal pointers. Used to dedup item-level definitions emitted from multiple
// metacalls (e.g. two metafns each emitting `struct Synth { x: i32 }` should
// not collide). Walks TinyObjectMap by bitmap key, ObjectArray by index, and
// HermesString by view; conservative on unknown Data-tag objects.
namespace {
bool ast_anyval_equal(AnyVal a, AnyVal b,
                      const uint8_t* base_a, const uint8_t* base_b);

bool ast_tom_equal(const hermes::TinyObjectMap* a,
                   const hermes::TinyObjectMap* b,
                   const uint8_t* base_a, const uint8_t* base_b) {
    // SRC_LINE is purely diagnostic; ignore it in ODR equality so items
    // emitted from quote_item! at different source lines still dedup.
    constexpr uint64_t skip_mask = 1ULL << la::SRC_LINE.code;
    if ((a->bitmap() & ~skip_mask) != (b->bitmap() & ~skip_mask)) return false;
    auto* ma = const_cast<hermes::TinyObjectMap*>(a);
    auto* mb = const_cast<hermes::TinyObjectMap*>(b);
    auto* ba = const_cast<uint8_t*>(base_a);
    auto* bb = const_cast<uint8_t*>(base_b);
    for (uint8_t k = 0; k < hermes::TinyObjectMap::MAX_KEYS; ++k) {
        if (!a->has_key(k) || k == la::SRC_LINE.code) continue;
        if (!ast_anyval_equal(ma->get(k, ba), mb->get(k, bb), base_a, base_b))
            return false;
    }
    return true;
}

bool ast_array_equal(const hermes::ObjectArray* a,
                     const hermes::ObjectArray* b,
                     const uint8_t* base_a, const uint8_t* base_b) {
    if (a->size() != b->size()) return false;
    auto* aa = const_cast<hermes::ObjectArray*>(a);
    auto* ab = const_cast<hermes::ObjectArray*>(b);
    auto* ba = const_cast<uint8_t*>(base_a);
    auto* bb = const_cast<uint8_t*>(base_b);
    for (uint64_t i = 0; i < a->size(); ++i) {
        if (!ast_anyval_equal(aa->get(i, ba), ab->get(i, bb), base_a, base_b))
            return false;
    }
    return true;
}

bool ast_anyval_equal(AnyVal a, AnyVal b,
                      const uint8_t* base_a, const uint8_t* base_b) {
    if (a.is_null() && b.is_null()) return true;
    if (a.is_null() || b.is_null()) return false;
    if (a.is_value() != b.is_value()) return false;
    if (a.is_value()) return a.raw() == b.raw();
    const uint8_t* pa = base_a + a.to_offset().value();
    const uint8_t* pb = base_b + b.to_offset().value();
    auto ta = hermes::TypeTag::read_before(pa);
    auto tb = hermes::TypeTag::read_before(pb);
    if (ta.type_code() != tb.type_code()) return false;
    if (ta.type_code() == hermes::type_hash::TinyObjectMap) {
        return ast_tom_equal(reinterpret_cast<const hermes::TinyObjectMap*>(pa),
                             reinterpret_cast<const hermes::TinyObjectMap*>(pb),
                             base_a, base_b);
    }
    if (ta.type_code() == hermes::type_hash::Array) {
        return ast_array_equal(reinterpret_cast<const hermes::ObjectArray*>(pa),
                               reinterpret_cast<const hermes::ObjectArray*>(pb),
                               base_a, base_b);
    }
    if (ta.type_code() == hermes::type_hash::HermesString) {
        return reinterpret_cast<const hermes::ArenaString*>(pa)->view()
            == reinterpret_cast<const hermes::ArenaString*>(pb)->view();
    }
    // Unknown data tag — be conservative and treat as not equal.
    return false;
}
} // namespace

// Symbol-collection phase: populate SemaChecker symbol tables.

void SemaChecker::collect(const std::vector<hermes::Hermes>& asts) {
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
    struct FirstSeen { hermes::MemHolder* holder; uint32_t off; };
    std::unordered_map<std::string, FirstSeen> first_struct;
    std::unordered_map<std::string, FirstSeen> first_datatype;
    std::unordered_map<std::string, FirstSeen> first_enum;
    auto item_off = [](TinyMapView t) -> uint32_t {
        return static_cast<uint32_t>(t.offset().value());
    };
    auto items_equal = [](FirstSeen a, hermes::MemHolder* hb, uint32_t off_b) {
        const uint8_t* ba = a.holder->base();
        const uint8_t* bb = hb->base();
        auto* tom_a = reinterpret_cast<const hermes::TinyObjectMap*>(ba + a.off);
        auto* tom_b = reinterpret_cast<const hermes::TinyObjectMap*>(bb + off_b);
        return ast_tom_equal(tom_a, tom_b, ba, bb);
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
            if ((ic == la::STRUCT || ic == la::DATATYPE || ic == la::ENUM)
                    && item.has_key(la::NAME.code))
                pass0_decl_names.insert(std::string(str_of(item.get(la::NAME.code))));
        }
    }
    pass0_decl_names_ = &pass0_decl_names;

    // First pass: register names (so forward references work).
    for (size_t pass0_ai = 0; pass0_ai < asts.size(); ++pass0_ai) {
        // M6.1: delta mode — skip asts already processed in a prior
        // sema_lower call within this same compile session.
        if (pass0_ai < delta_start_idx_) continue;
        auto& ast = asts[pass0_ai];
        // M5 step 3b+5: skip ONLY cached BINARY holders. User asts
        // re-walk so the strict-mode final sema still validates them
        // (e.g. catches "unknown type 'HermesStatic'" when the user
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
            bool is_zoned_struct = false;
            if (ic == la::STRUCT) {
                for (auto& ann : pass0_pending) {
                    if (str_of(ann.get(la::NAME.code)) == "zoned") { is_zoned_struct = true; break; }
                }
            }
            pass0_pending.clear();
            if (ic == la::STRUCT && !is_zoned_struct) {
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
                }
            } else if ((ic == la::STRUCT && is_zoned_struct) || ic == la::DATATYPE) {
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
                collected_holders_.count(asts[ai].holder())) continue;
        }
        cur_ast_idx_ = ai;
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        cur_from_binary_ = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
        cur_from_lazy_   = (is_lazy_     && ai < is_lazy_->size())     ? (*is_lazy_)[ai]     : false;
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
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
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
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
                          std::string_view item_name) {
    if (is_pub || def_package.empty() || cur_package_.empty()) return;
    if (def_package != cur_package_)
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

    for (size_t i = 0; i < args.size(); ++i) {
        if (i >= type_params.size() && !has_variadic) break;

        const auto& tp = (has_variadic && i >= non_variadic_count)
                         ? type_params.back()
                         : type_params[i];

        auto concrete = args[i];
        if (!concrete) continue;
        TypeRef cv{concrete};
        if (cv.kind() == LogosType::Kind::Error) continue;
        if (cv.kind() == LogosType::Kind::TypeVar) continue; // defer until mono
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
            // Auto trait: synthesize satisfaction from field types.
            auto trit = traits_.find(bound.trait_name);
            if (trit != traits_.end() && trit->second.is_auto) {
                StrSet visited;
                last_offender_ = {};
                if (is_auto_trait_satisfied(concrete, bound.trait_name, visited)) continue;
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
                if (found) {
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
                if (impls_.count(key3)) continue;
            }
            // SL-sl-08 follow-up: tuple-impl bound satisfaction. Tuples
            // are registered under `$tuple$N` (generic, mirrors the
            // `$tuple$N$<t1>$<t2>…` concrete form). Recognise both.
            // The element-level bounds (A: X for elem 0, B: X for elem 1)
            // are checked recursively below by the same machinery as
            // generic-struct impls — at monomorphisation time.
            // Variadic form `impl<A...> Trait for (A...)` registers under
            // `$tuple$variadic` — accept any tuple arity.
            if (cv.kind() == LogosType::Kind::Tuple) {
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
                                       cv.kind() == LogosType::Kind::FnPtr))
                continue;
            error(std::format("'{}': type '{}' does not implement trait '{}' required by parameter '{}'",
                  target_name, concrete_str, bound.trait_name, tp.name));
        }
    }
}

void SemaChecker::collect_module(TinyMapView mod, int phase) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));

    // Track pending #[...] annotations so the collect phase can populate
    // explicit_type_codes_ early (before lower_module). Without this, an
    // `impl Trait for Foo` in one package couldn't resolve Foo's type_code
    // when Foo is declared in another package with #[type_code=N] — leading
    // to a hash-fallback dispatch slot that never matches the runtime tag.
    std::vector<TinyMapView> pending_annots;

    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t c = code_of(item);
        if (c == la::ANNOTATION) {
            if (phase == 1) pending_annots.push_back(item);
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
        // Phase 2-2: conditional compilation. `#[cfg(...)]` on any item
        // is evaluated here; if false, drop the item entirely (don't
        // collect, don't lower) along with any other pending annotations
        // it would have inherited.
        bool dropped_by_cfg = false;
        for (auto& ann : pending_annots) {
            if (str_of(ann.get(la::NAME.code)) != "cfg") continue;
            if (!evaluate_cfg_annotation(ann)) { dropped_by_cfg = true; break; }
        }
        if (dropped_by_cfg) {
            pending_annots.clear();
            continue;
        }
        // Phase 2-3: cfg_attr. Evaluate the predicate (ARGS[0]) and act:
        //   - false → silently drop the cfg_attr entry (no wrapped attr
        //             activated).
        //   - true  → wrapped attr (ARGS[1..]) should be applied. Full
        //             attribute synthesis into pending_annots is complex
        //             (requires building synthetic Hermes nodes); for
        //             now we emit a one-time diagnostic asking the user
        //             to inline the activated attribute directly. Rust
        //             core code that uses #[cfg_attr] for repr/derive
        //             can port-time-replace with direct attribute.
        {
            auto it = pending_annots.begin();
            while (it != pending_annots.end()) {
                if (str_of(it->get(la::NAME.code)) != "cfg_attr") { ++it; continue; }
                // Predicate is the first ARG. ARGS structure: { ITEMS: [...] }.
                bool active = false;
                if (it->has_key(la::ARGS)) {
                    auto args_av = it->get(la::ARGS.code);
                    if (!args_av.is_null()) {
                        auto args_list = map_of(args_av);
                        if (args_list.has_key(la::ITEMS)) {
                            auto items_arr = arr_of(args_list.get(la::ITEMS.code));
                            if (items_arr.size() >= 1) {
                                auto pred = map_of(items_arr.get(0));
                                int32_t code = code_of(pred);
                                if (code == la::ANNOT_KV &&
                                    pred.has_key(la::NAME) && pred.has_key(la::VALUE)) {
                                    std::string key(str_of(pred.get(la::NAME.code)));
                                    auto val_node = map_of(pred.get(la::VALUE.code));
                                    std::string val;
                                    if (code_of(val_node) == la::LIT_STR && val_node.has_key(la::VALUE)) {
                                        val = std::string(str_of(val_node.get(la::VALUE.code)));
                                        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                                            val = val.substr(1, val.size() - 2);
                                    }
                                    active = match_cfg_predicate_kv(key, val);
                                } else if (pred.has_key(la::NAME)) {
                                    active = match_cfg_predicate_flag(
                                        str_of(pred.get(la::NAME.code)));
                                }
                            }
                        }
                    }
                }
                if (active) {
                    // Wrapped attr activation not yet implemented — diagnose.
                    node_line_ = get_line(*it);
                    warn("#[cfg_attr(...)]: predicate matched but wrapped "
                         "attribute activation is not yet implemented in "
                         "this compiler. Inline the wrapped attribute "
                         "directly as a port-time workaround.");
                }
                it = pending_annots.erase(it);
            }
        }
        if (phase == 1) {
            if      (c == la::STRUCT) {
                bool is_zoned = false;
                for (auto& ann : pending_annots)
                    if (str_of(ann.get(la::NAME.code)) == "zoned") { is_zoned = true; break; }
                if (!item.has_key(la::NAME.code)) { /* struct_inst — skip collect */ }
                else {
                    auto sname = std::string(str_of(item.get(la::NAME.code)));
                    bool htp = item_has_type_params(item);
                    // STRUCT-syntax items: `#[zoned]` is the syntactic switch
                    // that promotes to datatype, so the syntactic target is
                    // always Struct here.
                    check_annotations(AttrTarget::Struct, sname, htp, pending_annots);
                    if (is_specialization_struct(item)) collect_struct_spec(item);
                    else if (is_zoned) {
                        bool pending_is_annot_type = false;
                        for (auto& ann : pending_annots) {
                            if (str_of(ann.get(la::NAME.code)) == "annotation") { pending_is_annot_type = true; break; }
                        }
                        collect_datatype(item, pending_is_annot_type);
                    } else {
                        collect_struct(item);
                        // `#[no_auto_drop]`: opt the struct out of compiler
                        // auto-Drop (no user-drop call, no field-drop). The
                        // lang-item shape behind ManuallyDrop<T> — the wrapper
                        // must NOT run the inner T's destructor at scope exit.
                        for (auto& ann : pending_annots)
                            if (str_of(ann.get(la::NAME.code)) == "no_auto_drop") {
                                auto skey = sema_key(cur_package_, sname);
                                auto sit = structs_.find(skey);
                                if (sit == structs_.end()) sit = structs_.find(sname);
                                if (sit != structs_.end()) sit->second.no_auto_drop = true;
                                break;
                            }
                    }
                }
            } else if (c == la::DATATYPE) {
                // Skip explicit instantiation declarations (no FIELDS key, no NAME key).
                // These only bind annotations to existing generic instantiations.
                if (item.has_key(la::NAME.code)) {
                    bool is_spec = is_specialization_struct(item);
                    if (is_spec) {
                        collect_struct_spec(item);
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
                // is_hermes for the Hermes-datatype-family identification
                // that reflect::<T>() and reflection emission consult.
                pending_trait_is_hermes_ = false;
                for (auto& ann : pending_annots) {
                    if (str_of(ann.get(la::NAME.code)) == "type_code") {
                        pending_trait_is_hermes_ = true;
                        break;
                    }
                }
                collect_trait(item);
            }
            else if (c == la::IMPL_BLOCK)                 collect_impl(item);
        } else {
            if      (c == la::TYPE_ALIAS)                 collect_type_alias(item);
            else if (c == la::CONST_DEF)                  collect_const(item);
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
                        // MP-mc-01: `Variant = metacall { <expr> }`. Block
                        // tail expression evaluated via ctfe; integer
                        // result becomes the discriminant.
                        hermes::TinyMapView tail{};
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
                            auto r = ctfe::eval_expr(tail, holder_);
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
                            TinyMapView tm(av.to_offset(), holder_);
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
                            TinyMapView tm(av.to_offset(), holder_);
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
    entry.type = resolve_type(map_of(node.get(la::TYPE.code)));
    pop_type_params(entry.type_params);
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
        t = resolve_type(map_of(node.get(la::TYPE.code)));
    } else if (node.has_key(la::VALUE)) {
        // Evaluate type of the value expression lazily.
        // We just store an i32 as placeholder for now.
        t = i32_t();
    }
    if (t) {
        // Const-def uniqueness (closes B-ca-04)
        if (module_consts_.count(name) || generic_consts_.count(name)) {
            error(std::format("duplicate const '{}'", name));
        }
        module_consts_[name] = t;
        // M5 step 5c: track user-origin keys for snapshot filtering.
        if (!cur_from_binary_) user_module_const_keys_.insert(std::string(name));
        if (node.has_key(la::VALUE)) {
            auto val_av = node.get(la::VALUE.code);
            if (val_av.is_pointer())
                module_const_values_[name] = map_of(val_av);
        }
    }

    // Sprint 1.2: detect self-referential const initializer (closes
    // B-ca-01 P0 SEGFAULT — sema would substitute X for X recursively).
    // Conservative shallow check: catches the direct cycle `const X = X`
    // and the common arithmetic shape `const X = X + N`.  Deeper structural
    // walks (through hermes literals, blocks, calls) require a robust
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
            // const-evaluable children, CAST of const-evaluable, hermes-lit
            // (already handled below as HermesStatic).
            std::function<bool(TinyMapView)> is_const_evaluable;
            is_const_evaluable = [&](TinyMapView v) -> bool {
                int32_t vc = code_of(v);
                if (vc == la::LIT_INT  || vc == la::LIT_BOOL ||
                    vc == la::LIT_STR  || vc == la::LIT_FLOAT ||
                    vc == la::LIT_CHAR || vc == la::LIT_HSTATIC ||
                    vc == la::LIT_BYTES)
                    return true;
                if (vc == la::HERMES_MAP.code  || vc == la::HERMES_ARRAY.code ||
                    vc == la::HERMES_STR.code  || vc == la::HERMES_INT.code  ||
                    vc == la::HERMES_NEG_INT.code || vc == la::HERMES_FLOAT.code ||
                    vc == la::HERMES_BOOL.code || vc == la::HERMES_NULL.code)
                    return true;  // HermesStatic literal — handled separately
                if (vc == la::METACALL) return true;
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

    // `pub const X: HermesStatic = @{...};` semantically replaces the legacy
    // `pub type X = @{...};` form. The literal is a HermesStatic value, not
    // a type — but at type-arg positions it functions as a const-generic
    // value with byte-hash identity. Register X as a type alias to that
    // HStaticLit so call-site lookups resolve uniformly with legacy aliases
    // until that path is fully migrated. Generic constants
    // (`pub const X<T1, T2>: HermesStatic = @{… <type:T1> …}`) are recorded
    // separately in generic_consts_ and instantiated per use-site.
    if (node.has_key(la::VALUE) && t &&
        TypeRef(t).kind() == LogosType::Kind::Struct &&
        is_hermes_static(t)) {
        auto val_av = node.get(la::VALUE.code);
        if (val_av.is_pointer()) {
            auto val_node = map_of(val_av);
            auto vc = code_of(val_node);
            // hermes_lit produces LIT_HSTATIC at expression position when
            // the literal flows through a type-arg slot; here the value-AST
            // IS the hermes literal (HERMES_MAP / HERMES_ARRAY / scalar).
            // resolve_type's hstatic-lit handling expects a LIT_HSTATIC
            // wrapper. The legacy path went through hstatic_lit_type which
            // emitted LIT_HSTATIC; const_def's value is the bare hermes_lit
            // node, so we synthesise a LIT_HSTATIC view by resolving via
            // the LIT_HSTATIC/HERMES_* code path directly.
            //
            // Easiest: detect bare hermes_lit AST codes and route them
            // through the existing LIT_HSTATIC handler in resolve_type by
            // synthesising the same shape.
            (void)vc;
            // Attempt resolve: if VALUE node has LIT_HSTATIC code already,
            // resolve_type accepts it. Otherwise, the value is a primary
            // hermes_lit AST and we need to detect that here.
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
                // HStaticLit. Wrap value in a LIT_HSTATIC-shaped node by
                // calling the existing resolver path.
                TypeRef hs = resolve_type(val_node);
                if (hs && TypeRef(hs).kind() == LogosType::Kind::HStaticLit) {
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
    info.doc = take_pending_doc();
    info.is_hermes = pending_trait_is_hermes_;
    pending_trait_is_hermes_ = false;
    // Read auto marker
    if (node.has_key(la::IS_AUTO)) {
        AnyVal av = node.get(la::IS_AUTO);
        info.is_auto = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // Validate: auto traits must have an empty body
    if (info.is_auto && node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() > 0) {
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
                pop_type_params(at.type_params);
                info.assoc_types.push_back(std::move(at));
                continue;
            }
            if (code_of(m) == la::ASSOC_CONST_DEF) {
                SemaAssocConstInfo ac;
                ac.name = std::string(str_of(m.get(la::NAME.code)));
                if (m.has_key(la::TYPE))
                    ac.type = resolve_type(map_of(m.get(la::TYPE.code)));
                ac.doc = std::move(trait_method_sweep_doc);
                trait_method_sweep_doc.clear();
                info.assoc_consts.push_back(std::move(ac));
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
                            if (p.has_key(la::TYPE)) {
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
            mi.ret_type = m.has_key(la::RET_TYPE)
                ? resolve_type(map_of(m.get(la::RET_TYPE.code))) : void_t();
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
            if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
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
        if (self_type)
            current_type_params_["Self"] = self_type;
    }
    // Verify trait exists (only for trait impls)
    // Copy and Drop are built-in marker traits — not always visible through
    // the dependency-graph (pub trait + use isn't enough when the target
    // type's own package re-imports a different non-pub Drop, e.g. std.string
    // and hermes.zone both used to declare local `trait Drop`). Treating Drop
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
                // Bug 3 fix: detect duplicate assoc type impl for the same trait+type+name.
                std::string key = trait_name + "::" + key_target + "::" + aname;
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
                assoc_type_impls_[key] = std::move(ate);
                if (!cur_from_binary_) user_assoc_type_impl_keys_.insert(key);
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
                auto cands = find_func_candidates(
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
                        if (ssi_def) {
                            if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_struct(target, std::move(tv_args), {}, spkg_def);
                            } else {
                                self_type = make_struct_type(target, spkg_def);
                            }
                        } else if (dsi_def) {
                            if (!impl_tps.empty()) {
                                std::vector<TypeRef> tv_args;
                                for (auto& tp : impl_tps)
                                    tv_args.push_back(make_typevar(tp.name));
                                self_type = make_generic_datatype(target, std::move(tv_args), {}, dpkg_def);
                            } else {
                                self_type = make_datatype_type(target, dpkg_def);
                            }
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
                std::string key = trait_name + "::" + target + "::" + at.name;
                if (!assoc_type_impls_.count(key))
                    error(std::format("impl {} for {}: missing associated type '{}'",
                          trait_name, target, at.name));
            }
            // Check associated constant completeness
            for (auto& ac : tit->second.assoc_consts) {
                std::string key = trait_name + "::" + target + "::" + ac.name;
                if (!assoc_const_impls_.count(key))
                    error(std::format("impl {} for {}: missing associated constant '{}'",
                          trait_name, target, ac.name));
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
        copy_types_.insert(target);
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
                          impl_lt_outlives, impl_doc};
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
        if (!cur_from_binary_) user_impl_keys_.insert(key);
        // `str` is a built-in that resolves to Slice<u8>; type_str() produces
        // "&[u8]" for Slice<u8>, so trait-bound checks look for "Trait::&[u8]".
        // Register an alias entry so satisfaction checks find the impl.
        if (target == "str") {
            SemaImplInfo alias{trait_name, "&[u8]", impl_is_unsafe, impl_is_negative,
                               target_resolved, impl_tps,
                               trait_type_args, trait_lt_args, impl_lt_params,
                               impl_lt_outlives, impl_doc};
            impls_[trait_name + "::&[u8]"] = alias;
        }
    }
}

// Collect a struct specialization into struct_specs_sema_.
// Only full specializations (all patterns concrete) are registered;

void SemaChecker::collect_struct_spec(TinyMapView node) {
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
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        spec_patterns.push_back(resolve_type(tpnode));
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        auto known = try_resolve_as_known_type(name);
                        if (known) {
                            spec_patterns.push_back(known);
                        } else {
                            // Partial spec — skip for sema registration.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            pattern_tvars.push_back({std::string(name), {}});
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
    info.base_name = sname;
    info.spec_patterns = spec_patterns;
    std::string spec_field_sweep_doc;
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            if (try_append_doc(spec_field_sweep_doc, fnode)) continue;
            if (code_of(fnode) != la::FIELD_DEF) continue;
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
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
    info.is_annotation_type = is_annotation_type;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
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
                auto is_datatype_safe = [](TypeRef t, auto& self) -> bool {
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
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
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
            auto ftype = resolve_type(ftype_node);
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
    pop_type_params(structs_[skey].type_params);
}

TypeRef SemaChecker::try_resolve_as_known_type(std::string_view name) {
    // Type-params bound in current scope (generic-const instantiation, generic
    // fn body, generic struct method) win over global lookups. Ensures that
    // `<type:K>` inside `pub const PMap<K, V>: HermesStatic = @{...}` resolves
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
    } else if (tc == la::ARR_TYPE) {
        if (node.has_key(la::TYPE))
            extract_typevars_from_type_node(
                map_of(node.get(la::TYPE.code)), out_tvars);
    } else if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        if (!is_known_type_name(name) &&
            !current_type_params_.count(std::string(name))) {
            current_type_params_[std::string(name)] = make_typevar(name);
            out_tvars.push_back({std::string(name), {}});
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
        if (c == la::PTR_TYPE || c == la::ARR_TYPE)
            return true;  // structured pattern → specialisation
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
    return is_specialization_fn(node);  // identical check
}

lir::LStructDef SemaChecker::lower_spec_struct(TinyMapView node) {
    auto sname = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("struct {} (specialization)", sname);

    lir::LStructDef sd;
    sd.name = sname;
    sd.pkg  = cur_package_;
    sd.is_specialization = true;
    sd.from_binary_module = cur_from_binary_;

    // Parse spec type-param list: populate spec_patterns and TypeVar scope.
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
                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        sd.spec_patterns.push_back(resolve_type(tpnode));
                    } else if (tc == la::TYPE_PARAM) {
                        auto name = str_of(tpnode.get(la::NAME.code));
                        if (tpnode.has_key(la::ITEMS)) {
                            // TypeVar with bounds — stays TypeVar in pattern.
                            current_type_params_[std::string(name)] = make_typevar(name);
                            TypeParam tp; tp.name = std::string(name);
                            pattern_tvars.push_back(std::move(tp));
                            sd.spec_patterns.push_back(make_typevar(name));
                        } else {
                            auto known = try_resolve_as_known_type(name);
                            if (known) {
                                sd.spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] = make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}});
                                sd.spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }

    // Lower fields (TypeVars from patterns now in scope).
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            sd.fields.push_back({std::string(fname), ftype});
        }
    }

    // Lower methods.
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
            if (code_of(method) == la::FN)
                sd.methods.push_back(std::make_unique<lir::LFunction>(lower_fn(method, sname)));
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

lir::LFunction SemaChecker::lower_spec_fn(TinyMapView node) {
    auto raw_name = str_of(node.get(la::NAME.code));
    ctx_ = std::format("fn {} (specialization)", raw_name);
    node_line_ = get_line(node);

    lir::LFunction fn;
    fn.name = std::string(raw_name);
    fn.doc  = take_pending_doc();
    fn.is_specialization = true;
    fn.from_binary_module = cur_from_binary_;
    fn.from_lazy_module   = cur_from_lazy_;

    // Parse spec type-param list: populate fn.spec_patterns and scope TypeVars.
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

                    if (tc == la::LIFETIME_PARAM) continue;  // skip — deferred to borrow checker
                    if (tc == la::PTR_TYPE || tc == la::ARR_TYPE) {
                        // Structured pattern: extract TypeVars then resolve.
                        extract_typevars_from_type_node(tpnode, pattern_tvars);
                        fn.spec_patterns.push_back(resolve_type(tpnode));

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
                                        {std::string(str_of(bn.get(la::NAME.code))), {}, {}});
                            }
                            pattern_tvars.push_back(std::move(tp));
                            fn.spec_patterns.push_back(make_typevar(name));
                        } else {
                            // Plain IDENT: known type → concrete; else → TypeVar.
                            auto known = try_resolve_as_known_type(name);
                            if (known) {
                                fn.spec_patterns.push_back(known);
                            } else {
                                current_type_params_[std::string(name)] =
                                    make_typevar(name);
                                pattern_tvars.push_back({std::string(name), {}});
                                fn.spec_patterns.push_back(make_typevar(name));
                            }
                        }
                    }
                }
            }
        }
    }

    // Resolve params and return type (TypeVars from patterns are now in scope).
    fn.ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
        : void_t();
    ret_type_ = fn.ret_type;

    scope_.clear();
    push_scope();

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
                    fn.params.push_back({std::string(pname), ptype});
                }
            }
        }
    }

    if (!fn.is_extern && node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        // B-fn-06: TAIL_EXPR acts as implicit return inside fn-body lowering
        // and the reachability check.
        bool saved_tail = tail_as_return_;
        tail_as_return_ = true;
        fn.body = lower_block(body_node);
        if (fn.ret_type && TypeRef(fn.ret_type).kind() != LogosType::Kind::Void &&
            TypeRef(fn.ret_type).kind() != LogosType::Kind::Error &&
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
    read_param_types();
    info.ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
        : void_t();
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
        const std::string qual_base = std::string(struct_ctx) + "__" +
                                       info.trait_name + "__" + std::string(raw_name);
        bool generic = !info.type_params.empty();
        auto& ov  = generic ? generic_overloads_ : func_overloads_;
        auto& tbl = generic ? generic_funcs_     : funcs_;
        const std::string plain_sig =
            function_signature_key(plain_base, info.param_types, info.is_vararg);

        bool already = trait_method_registry_.count(plain_base) > 0;
        std::string clash_sym;
        if (!already) {
            if (auto oit = ov.find(plain_base); oit != ov.end()) {
                for (auto& sym : oit->second) {
                    auto fit = tbl.find(sym);
                    if (fit == tbl.end()) continue;
                    std::string esig = function_signature_key(
                        plain_base, fit->second.param_types, fit->second.is_vararg);
                    if (esig == plain_sig && !fit->second.trait_name.empty() &&
                        fit->second.trait_name != info.trait_name) {
                        clash_sym = sym; break;
                    }
                }
            }
        }
        if (already || !clash_sym.empty()) {
            // First collision: re-key the pre-existing plain entry to its
            // own trait-qualified base, and pull it out of the plain index.
            if (!clash_sym.empty()) {
                if (auto fit = tbl.find(clash_sym); fit != tbl.end()) {
                    SemaFuncInfo ex = std::move(fit->second);
                    tbl.erase(fit);
                    std::string ex_trait = ex.trait_name;
                    std::string ex_qual = std::string(struct_ctx) + "__" +
                        ex_trait + "__" + std::string(raw_name);
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
