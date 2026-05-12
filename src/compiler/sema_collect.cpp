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

    // First pass: register names (so forward references work).
    for (auto& ast : asts) {
        holder_ = ast.holder();
        auto root = ast.root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        cur_imports_ = build_import_scope(root);
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
            }
        }
    }

    // Intermediate pass: type aliases and consts (Phase 2). Wait, we execute this FIRST so aliases are known for fn signatures.
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        cur_ast_idx_ = ai;
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        cur_from_binary_ = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        cur_imports_ = build_import_scope(root);
        collect_module(root, 2);
    }
    // Second pass: fill in fields, variants, function signatures (Phase 1).
    for (size_t ai = 0; ai < asts.size(); ++ai) {
        cur_ast_idx_ = ai;
        holder_ = asts[ai].holder();
        file_ = (filenames_ && ai < filenames_->size()) ? (*filenames_)[ai] : std::string{};
        cur_from_binary_ = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
        auto root = asts[ai].root_object().as_tiny_map();
        cur_package_ = read_package_name(root);
        cur_imports_ = build_import_scope(root);
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
            bool is_bin = (from_binary_ && ai < from_binary_->size()) ? (*from_binary_)[ai] : false;
            if (is_bin) continue;
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
            bool is_bin = (from_binary_ && ai < from_binary_->size())
                          ? (*from_binary_)[ai] : false;
            if (is_bin) continue;
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
            if (impls_.count(key1) || (!key2.empty() && impls_.count(key2))) continue;
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
                    } else                              collect_struct(item);
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
                for (auto& ann : pending_annots) {
                    auto nm = str_of(ann.get(la::NAME.code));
                    if (nm == "no_mangle")    pending_no_mangle_ = true;
                    if (nm == "fn_macro")     pending_fn_macro_  = true;
                    if (nm == "token_macro")  pending_token_macro_ = true;
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
    }
}

void SemaChecker::collect_enum(TinyMapView node) {
    auto ename = std::string(str_of(node.get(la::NAME.code)));
    ctx_ = std::format("enum {}", ename);
    // B-it-06 — empty enum bodies are intentionally legal as uninhabited /
    // marker types (used by stdlib meta_variant_intrinsics, generic-anchor
    // patterns, etc.). No diagnostic.
    SemaEnumInfo info;
    info.type_params = read_type_params(node);
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
                for (uint64_t i = 0; i < variants.size(); ++i) {
                    auto v = map_of(variants.get(i));
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
                            info.variants.push_back({vname, vval, {}, {}, false, false});
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
                                             is_var, is_struct_shape});
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
    // Bug 1 fix: detect duplicate type alias names.
    if (type_aliases_.count(name))
        error(std::format("duplicate type alias '{}'", name));
    TypeAliasEntry entry;
    entry.lifetime_params = read_lifetime_params(node);
    entry.type_params = read_type_params(node);
    push_type_params(entry.type_params);
    entry.type = resolve_type(map_of(node.get(la::TYPE.code)));
    pop_type_params(entry.type_params);
    type_aliases_[name] = std::move(entry);
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
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
            if (code_of(m) == la::ASSOC_TYPE_DEF) {
                SemaAssocTypeInfo at;
                at.name = std::string(str_of(m.get(la::NAME.code)));
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
                info.assoc_consts.push_back(std::move(ac));
                continue;
            }
            if (code_of(m) != la::FN) continue;
            SemaTraitMethodInfo mi;
            mi.name = std::string(str_of(m.get(la::NAME.code)));
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
    // Trait-def uniqueness (closes B-it-05). Skip empty names — trait_inst
    // forward-instantiation declarations reach this code path with no NAME
    // field; those aren't duplicates of each other.
    if (!tname.empty() && traits_.count(tname)) {
        auto& existing = traits_[tname];
        // B-mv-02: cross-pkg same-name traits hit this path because traits_
        // is keyed by bare name (legacy).  Make the diagnostic explicit.
        std::string existing_pkg = existing.package;
        if (existing_pkg != cur_package_ &&
            !existing_pkg.empty() && !cur_package_.empty()) {
            error(std::format(
                "trait '{}' defined in both packages '{}' and '{}' — "
                "cross-package same-name traits clobber each other in "
                "the legacy trait registry (B-mv-02); rename one of them",
                tname, existing_pkg, cur_package_));
        } else {
            error(std::format("duplicate trait '{}'", tname));
        }
    }
    info.package = cur_package_;  // record so future cross-pkg dup diag can show pkg
    traits_[tname] = std::move(info);
}

void SemaChecker::collect_impl(TinyMapView node) {
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
    if (node.has_key(la::IMPL_TYPE_PARAMS)) {
        impl_tps = read_type_params_from(node, la::IMPL_TYPE_PARAMS.code);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;
    } else if (trait_name.empty() && node.has_key(la::TYPE_PARAMS)) {
        impl_tps = read_type_params(node);
        push_type_params(impl_tps);
        impl_type_params_ = impl_tps;  // so collect_fn includes them in fn.type_params
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
    // Resolve trait type args (e.g. impl Into<i32> for Celsius → T=i32)
    // and push them into current_type_params_ so method sigs resolve correctly.
    std::vector<TypeRef> trait_type_args;
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
                    if (code_of(item) == la::LIFETIME_PARAM) continue;
                    trait_type_args.push_back(resolve_type(item));
                }
            }
        }
        auto tit = traits_.find(trait_name);
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
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto m = map_of(items.get(i));
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
                collect_fn(m, reg_target);
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
                auto tit_gat = traits_.find(trait_name);
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
                assoc_type_impls_[key] = { atype, impl_tps, gat_tps };
            } else if (code_of(m) == la::ASSOC_CONST_IMPL) {
                auto cname = std::string(str_of(m.get(la::NAME.code)));
                if (trait_name.empty()) {
                    // Standalone impls cannot declare associated constants (no trait to fulfill).
                    error(std::format("impl {}: associated constants can only appear in trait impls",
                                      target));
                } else {
                    TypeRef ctype = nullptr;
                    if (m.has_key(la::TYPE))
                        ctype = resolve_type(map_of(m.get(la::TYPE.code)));
                    // Type check: impl's type must match the trait's declared type.
                    auto tit2 = traits_.find(trait_name);
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
                    assoc_const_impls_[key] = { ctype, m.get(la::VALUE) };  // Bug 1 fix: no .code
                }
            }
        }
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
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& m : tit->second.methods) {
                auto mangled = check_target + "__" + m.name;
                auto cands = find_func_candidates(mangled);
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
                const SemaFuncInfo* matching = nullptr;
                for (auto* c : cands) {
                    if (c->param_types.size() != m.param_types.size()) continue;
                    bool sig_match = true;
                    for (size_t k = 1; k < m.param_types.size(); ++k) {
                        auto tp = m.param_types[k];
                        auto cp = c->param_types[k];
                        if (!tp || !cp) { sig_match = false; break; }
                        // If the trait param is generic (TypeVar or AssocType),
                        // it matches any concrete impl type.
                        if (is_generic_param(tp)) continue;
                        if (is_generic_param(cp)) continue;
                        if (!types_equal(tp, cp)) { sig_match = false; break; }
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
                    if (self_type)
                        current_type_params_["Self"] = self_type;
                    // Switch holder to the zone that owns the default AST node —
                    // it may live in a different module's zone (cross-module trait).
                    auto* saved_holder = holder_;
                    if (m.default_holder) holder_ = m.default_holder;
                    collect_fn(map_of(m.default_ast), target);
                    holder_ = saved_holder;
                    // Default trait-method: inherits trait accessibility.
                    // Mark ALL newly-registered overloads as pub (not just first).
                    auto dmangled = target + "__" + m.name;
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
        auto tit = traits_.find(trait_name);
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
        auto tit = traits_.find(trait_name);
        if (tit != traits_.end()) {
            for (auto& tp : tit->second.type_params)
                current_type_params_.erase(tp.name);
        }
    }
    // Clean up Self (set at top for all impl blocks)
    current_type_params_.erase("Self");
    // Clean up impl's own type params (pushed at top for standalone generic impl)
    if (!impl_tps.empty()) { pop_type_params(impl_tps); impl_type_params_.clear(); }
    // Register the impl mapping (only for trait impls)
    if (!trait_name.empty()) {
        SemaImplInfo info{trait_name, target, impl_is_unsafe, impl_is_negative,
                          target_resolved, impl_tps};
        impls_[trait_name + "::" + target] = info;
        // `str` is a built-in that resolves to Slice<u8>; type_str() produces
        // "&[u8]" for Slice<u8>, so trait-bound checks look for "Trait::&[u8]".
        // Register an alias entry so satisfaction checks find the impl.
        if (target == "str") {
            SemaImplInfo alias{trait_name, "&[u8]", impl_is_unsafe, impl_is_negative,
                               target_resolved, impl_tps};
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
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
            auto fname = str_of(fnode.get(la::NAME.code));
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            info.fields.push_back({fname, ftype, fpub});
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
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
    info.package = cur_package_;
    info.is_annotation_type = is_annotation_type;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    push_type_params(info.type_params);
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
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
            info.fields.push_back({fname, ftype, fpub, false});
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
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
    info.package = cur_package_;
    if (node.has_key(la::IS_PUB)) {
        AnyVal av = node.get(la::IS_PUB.code);
        info.is_pub = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    }
    push_type_params(info.type_params);
    // B-ts-01: detect tuple-struct shape — first FIELD_DEF has no NAME slot.
    if (node.has_key(la::FIELDS)) {
        auto fs0 = arr_of(node.get(la::FIELDS.code));
        if (fs0.size() > 0 && !map_of(fs0.get(0)).has_key(la::NAME))
            info.is_tuple_struct = true;
    }
    if (node.has_key(la::FIELDS)) {
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fnode = map_of(fields.get(i));
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
                fname = intern_synth_field_name(i);
            auto ftype = resolve_type(map_of(fnode.get(la::TYPE.code)));
            bool fpub = fnode.has_key(la::IS_PUB) &&
                        fnode.get(la::IS_PUB.code).is_value() &&
                        fnode.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
            bool fvar = false;
            if (fnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = fnode.get(la::IS_VARIADIC.code);
                fvar = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            info.fields.push_back({fname, ftype, fpub, fvar});
        }
    }
    auto skey = sema_key(cur_package_, sname);
    structs_[skey] = std::move(info);
    // Methods must be collected with the struct's type params in scope.
    if (node.has_key(la::ITEMS)) {
        auto methods = arr_of(node.get(la::ITEMS.code));
        for (uint64_t m = 0; m < methods.size(); ++m) {
            auto method = map_of(methods.get(m));
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
    fn.is_specialization = true;

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

void SemaChecker::collect_fn(TinyMapView node, std::string_view struct_ctx) {
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
    info.type_params = read_type_params(node);
    info.lifetime_params = read_lifetime_params(node);
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

    // For every registered impl "Trait::Type", walk Trait's supertrait chain and
    // verify that a corresponding impl "SuperTrait::Type" also exists.
    for (auto& [key, impl] : impls_) {
        const std::string& tname  = impl.trait_name;
        const std::string& target = impl.target_type;
        auto tit = traits_.find(tname);
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
            error(std::format("impl {} for {}: missing impl {} for {} (required by supertrait)",
                              tname, target, super.trait_name, target));
        }
    }
}

} // namespace logos::compiler
