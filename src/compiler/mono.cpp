// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Monomorphization pass: LProgram → LProgram.
//
// Generic functions (those with non-empty type_params) are replaced by their
// concrete instantiations discovered via ECall::type_args.  Non-generic
// functions pass through unchanged.  The output program has no TypeVar types.
//
// Algorithm:
//   1. Copy all non-generic structs, enums, consts, type aliases verbatim.
//   2. Walk every non-generic function body looking for GENERIC_CALL nodes.
//   3. For each GENERIC_CALL (callee, type_args), if the instantiation
//      (callee + mangled type suffix) is not yet generated, push it onto the
//      work-list.
//   4. Process the work-list: clone the generic function template, substitute
//      TypeVars, recursively scan the cloned body for more GENERIC_CALLs.
//   5. A depth counter guards against infinite recursion; if exceeded, emit
//      a diagnostic and skip.

#include "mono_impl.hpp"

#include <logos/compiler/lir_mirror.hpp>

namespace logos::compiler {

lir::LProgram Mono::run(lir::LProgram&& in, int /*max_depth*/) {
    in_ = std::move(in);

    // Stage 3g.1: in_.mirror_table is already comprehensive — sema's end-of-
    // run pass emitted every stmt/block/pattern, and LirBuilder mirrored each
    // LExpr at construction. No top-up needed here.

    // Output mirror — populated incrementally as functions are cloned
    // (so scan_fn can dispatch via lir_view). Empty at start of mono.
    out_.mirror_table = std::make_unique<LirMirrorTable>();

    // Slice 1b: LExprPtr is now a raw pointer into LProgram::expr_pool_.
    // Moving consts/functions/etc. from in_ to out_ leaves their LExpr*'s
    // pointing into in_.expr_pool_ — so we must transfer the pool too. New
    // mono-cloned exprs are appended onto this same pool by alloc_expr(out_).
    out_.expr_pool_          = std::move(in_.expr_pool_);
    out_.hstatic_registry_   = std::move(in_.hstatic_registry_);
    // Slice 1c: same hazard for LBlock / HermesVal / EClosure pools.
    out_.block_pool_         = std::move(in_.block_pool_);
    out_.hermes_val_pool_    = std::move(in_.hermes_val_pool_);
    out_.closure_pool_       = std::move(in_.closure_pool_);
    out_.consts              = std::move(in_.consts);
    out_.type_aliases        = std::move(in_.type_aliases);
    out_.traits              = std::move(in_.traits);
    out_.impls               = std::move(in_.impls);
    out_.dispatch_entries    = std::move(in_.dispatch_entries);
    out_.inst_annotations    = std::move(in_.inst_annotations);
    out_.reflection_globals  = std::move(in_.reflection_globals);
    out_.reflect_requests    = std::move(in_.reflect_requests);
    // Move type_pool — will be extended with new types during mono
    out_.type_pool           = std::move(in_.type_pool);

    // Index associated type impls for subst_type resolution.
    // Also split blanket impls into a separate table for fallback lookup.
    for (auto& impl : out_.impls) {
        if (impl.is_blanket) {
            BlanketImplInfo info;
            info.trait_name     = impl.trait_name;
            info.bound_trait    = impl.bound_trait;
            info.extra_bounds   = impl.extra_bounds;
            info.target_typevar = impl.target_type;
            info.assoc_types    = impl.assoc_types;
            info.primary_assoc_eqs = impl.primary_assoc_eqs;
            info.extra_assoc_eqs = impl.extra_assoc_eqs;
            blanket_impls_.push_back(std::move(info));
        } else {
            for (auto& [aname, atype] : impl.assoc_types) {
                std::string key = impl.trait_name + "::" + impl.target_type + "::" + aname;
                assoc_impls_[key] = atype;
            }
            if (!impl.trait_name.empty())
                concrete_impls_.insert(impl.trait_name + "::" + impl.target_type);
        }
    }

    // Index generic struct templates; pass-through plain structs immediately.
    // Pkg-qualified entries enable cross-pkg same-named struct disambig;
    // bare alias kept as last-wins for back-compat with callers operating
    // by base name only.
    for (auto& sd : in_.structs) {
        if (!sd.type_params.empty()) {
            if (!sd.pkg.empty())
                struct_templates_[sd.pkg + "." + sd.name] = &sd;
            struct_templates_[sd.name] = &sd;  // stable: in_.structs not moved
        }
        // L1: build (base_struct, short_method_name) → template index for lazy
        // method instantiation. After unification, method fn-names are
        // pkg-qualified (`pkg.Base__method__f__sig`); extract short name
        // by finding `Base__` and reading until next `__`.
        std::string prefix = sd.name + "__";
        for (auto& m : sd.methods) {
            std::string short_name;
            auto p = m->name.find(prefix);
            if (p != std::string::npos) {
                size_t start = p + prefix.size();
                auto end = m->name.find("__", start);
                short_name = (end == std::string::npos)
                             ? m->name.substr(start)
                             : m->name.substr(start, end - start);
            } else {
                short_name = m->name;
            }
            if (!sd.pkg.empty())
                struct_method_templates_[sd.pkg + "." + sd.name][short_name] = m.get();
            struct_method_templates_[sd.name][short_name] = m.get();
        }
    }
    // Move non-generic structs to output.
    for (auto& sd : in_.structs) {
        if (sd.type_params.empty())
            out_.structs.push_back(clone_struct_def(sd, {}, {}, sd.name));
    }

    // Index generic enum templates; pass-through plain enums.
    for (auto& ed : in_.enums) {
        if (!ed.type_params.empty()) {
            enum_templates_[ed.name] = &ed;
        } else {
            out_.enums.push_back(ed);
        }
    }

    // Index generic fn templates.
    for (auto& fn : in_.functions) {
        if (!fn->type_params.empty())
            templates_[fn->name] = fn.get();
    }

    // Eagerly instantiate blanket-impl methods for every concrete type that
    // satisfies the blanket's bound.  Each clone produces a function named
    // `Concrete__method` alongside the original `$blanket$...__method`
    // template, so normal call resolution (`I64__storage_new`) works.
    for (auto& bi : blanket_impls_) {
        std::string tmpl_prefix =
            "$blanket$" + bi.trait_name + "$" + bi.bound_trait
            + "$" + bi.target_typevar + "__";
        // Build the candidate concrete-type list. Bounded blanket
        // (`impl<T: Bound> Trait for T`) instantiates only for types that
        // satisfy `Bound`, found via the existing `Bound::Concrete` impls.
        // Unbounded blanket (`impl<T> Trait for T {}`) has no such filter —
        // instantiate for every non-generic struct/datatype/enum in the
        // program. Primitives/refs are not included; if needed, a future
        // pass should walk LIR for receiver types and lazily instantiate.
        std::vector<std::string> candidates;
        if (bi.bound_trait.empty()) {
            for (auto& sd : out_.structs)
                if (sd.type_params.empty()) candidates.push_back(sd.name);
            for (auto& ed : out_.enums)
                if (ed.type_params.empty()) candidates.push_back(ed.name);
        } else {
            // Direct concrete impls of bound_trait.
            for (auto& impl : out_.impls) {
                if (impl.is_blanket) continue;
                if (impl.trait_name != bi.bound_trait) continue;
                candidates.push_back(impl.target_type);
            }
            // Chain-satisfiers: types whose bound_trait impl is itself
            // derived from a blanket (e.g. Foo blanket on Container, where
            // K's Container is via the Primitive→Container blanket). Walk
            // every concrete type and ask the recursive resolver.
            StrSet already(candidates.begin(), candidates.end());
            auto consider = [&](const std::string& cn) {
                if (already.count(cn)) return;
                StrSet seen;
                if (mono_has_impl_recursive(bi.bound_trait, cn, seen)) {
                    candidates.push_back(cn);
                    already.insert(cn);
                }
            };
            for (auto& sd : out_.structs)
                if (sd.type_params.empty()) consider(sd.name);
            for (auto& ed : out_.enums)
                if (ed.type_params.empty()) consider(ed.name);
        }
        // ADR 0008: helper to check Trait<Assoc = Type> equality clauses.
        // Falls back to blanket-derived assoc-types when `concrete` does
        // not have a direct impl of `trait` but satisfies the bounds of
        // a blanket of `trait` (which carries `type Assoc = X` definitions
        // in blanket_impls_[…].assoc_types).
        auto assoc_eqs_ok = [&](const std::string& trait,
                                const std::string& concrete,
                                const std::vector<std::pair<std::string, TypeRef>>& eqs) {
            for (auto& [aname, expected] : eqs) {
                if (!expected) continue;
                std::string key = trait + "::" + concrete + "::" + aname;
                auto it = assoc_impls_.find(key);
                TypeRef found = nullptr;
                if (it != assoc_impls_.end()) {
                    found = it->second;
                } else {
                    // Blanket fallback: walk blankets of `trait`, find one
                    // whose bounds `concrete` satisfies, look up `aname` in
                    // its assoc_types.
                    for (auto& bj : blanket_impls_) {
                        if (bj.trait_name != trait) continue;
                        StrSet seen_pri;
                        bool ok = bj.bound_trait.empty()
                            || mono_has_impl_recursive(bj.bound_trait, concrete, seen_pri);
                        if (ok) {
                            for (auto& eb : bj.extra_bounds) {
                                StrSet seen_eb;
                                if (!mono_has_impl_recursive(eb, concrete, seen_eb)) {
                                    ok = false; break;
                                }
                            }
                        }
                        if (!ok) continue;
                        auto bait = bj.assoc_types.find(aname);
                        if (bait == bj.assoc_types.end()) continue;
                        // Substitute target_typevar → concrete struct type and
                        // recursively resolve so blanket bodies that reference
                        // the typevar (e.g. `type P = DT::Prim`) reduce to the
                        // concrete type before equality compare.
                        TypeRef concrete_t = nullptr;
                        for (auto& sd : out_.structs)
                            if (sd.name == concrete) {
                                LogosTypeBuilder st;
                                st.kind = sd.is_zoned ? LogosType::Kind::ZonedStruct
                                                       : LogosType::Kind::Struct;
                                st.struct_name = concrete;
                                st.pkg_name    = sd.pkg;
                                concrete_t = out_.type_pool.alloc(std::move(st));
                                break;
                            }
                        if (!concrete_t) {
                            for (auto& ed : out_.enums)
                                if (ed.name == concrete) {
                                    LogosTypeBuilder et;
                                    et.kind = LogosType::Kind::Enum;
                                    et.enum_name = concrete;
                                    et.pkg_name  = ed.pkg;
                                    concrete_t = out_.type_pool.alloc(std::move(et));
                                    break;
                                }
                        }
                        if (concrete_t) {
                            SubstMap bsubst;
                            bsubst[bj.target_typevar] = concrete_t;
                            found = subst_type(bait->second, bsubst);
                        } else {
                            found = bait->second;
                        }
                        break;
                    }
                }
                if (!found) return false;
                if (!types_equal(found, expected)) return false;
            }
            return true;
        };
        for (auto& concrete_ref : candidates) {
            const std::string& concrete = concrete_ref;
            // Multi-bound blanket: `impl<T: A + B + …> Trait for T` — only
            // instantiate for `concrete` types that satisfy *every* extra
            // bound, not just the primary one. concrete_impls_ was indexed
            // earlier from non-blanket impls (TraitName::TargetType keys).
            bool all_extra_satisfied = true;
            for (auto& eb : bi.extra_bounds) {
                StrSet seen;
                if (!mono_has_impl_recursive(eb, concrete, seen)) {
                    all_extra_satisfied = false;
                    break;
                }
            }
            if (!all_extra_satisfied) continue;
            // ADR 0008: assoc-type equality clauses must hold on every bound.
            if (!assoc_eqs_ok(bi.bound_trait, concrete, bi.primary_assoc_eqs)) continue;
            bool extra_eqs_ok = true;
            for (auto& [trait, eqs] : bi.extra_assoc_eqs)
                if (!assoc_eqs_ok(trait, concrete, eqs)) { extra_eqs_ok = false; break; }
            if (!extra_eqs_ok) continue;
            // For each method of the blanket, clone template with T→concrete
            // and emit under `concrete__method`.
            for (auto& tfn_up : in_.functions) {
                auto& tfn = *tfn_up;
                // Strip pkg prefix (`pkg.`) before matching the
                // synthetic `$blanket$...` template prefix.
                std::string tn = tfn.name;
                if (auto dot = tn.find('.'); dot != std::string::npos)
                    tn = tn.substr(dot + 1);
                if (tn.rfind(tmpl_prefix, 0) != 0) continue;
                // Method may carry `__f__sig` / `__g__sig`. Preserve sig in
                // dest_name so subsequent enqueues that mangle with type_args
                // produce matching final names.
                std::string method = tn.substr(tmpl_prefix.size());
                std::string dest = concrete + "__" + method;
                if (done_.count(dest)) continue;
                SubstMap subst;
                // Build concrete type for substitution.
                // Target may be a struct or datatype — use the right Kind.
                TypeRef concrete_t = nullptr;
                for (auto& sd : out_.structs)
                    if (sd.name == concrete) {
                        LogosTypeBuilder st;
                        st.kind = sd.is_zoned ? LogosType::Kind::ZonedStruct
                                                 : LogosType::Kind::Struct;
                        st.struct_name = concrete;
                        st.pkg_name    = sd.pkg;
                        concrete_t = out_.type_pool.alloc(std::move(st));
                        break;
                    }
                if (!concrete_t) {
                    for (auto& ed : out_.enums)
                        if (ed.name == concrete) {
                            LogosTypeBuilder et;
                            et.kind = LogosType::Kind::Enum;
                            et.enum_name = concrete;
                            et.pkg_name  = ed.pkg;
                            concrete_t = out_.type_pool.alloc(std::move(et));
                            break;
                        }
                }
                if (!concrete_t) {
                    // Scalar candidate (`impl Trait for u64` etc.) — build
                    // the appropriate scalar LogosType directly.
                    LogosType::Kind sk = LogosType::Kind::Error;
                    if      (concrete == "u8")   sk = LogosType::Kind::U8;
                    else if (concrete == "u16")  sk = LogosType::Kind::U16;
                    else if (concrete == "u32")  sk = LogosType::Kind::U32;
                    else if (concrete == "u64")  sk = LogosType::Kind::U64;
                    else if (concrete == "i8")   sk = LogosType::Kind::I8;
                    else if (concrete == "i16")  sk = LogosType::Kind::I16;
                    else if (concrete == "i32")  sk = LogosType::Kind::I32;
                    else if (concrete == "i64")  sk = LogosType::Kind::I64;
                    else if (concrete == "f32")  sk = LogosType::Kind::F32;
                    else if (concrete == "f64")  sk = LogosType::Kind::F64;
                    else if (concrete == "bool") sk = LogosType::Kind::Bool;
                    if (sk != LogosType::Kind::Error) {
                        LogosTypeBuilder pt; pt.kind = sk;
                        concrete_t = out_.type_pool.alloc(std::move(pt));
                    }
                }
                if (!concrete_t) continue;
                subst[bi.target_typevar] = concrete_t;
                auto cloned = clone_fn(tfn, subst);
                cloned.name = dest;
                cloned.type_params.clear();
                out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(cloned)));
                auto& fn_ref = *out_.functions.back();
                lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
                scan_fn(fn_ref);
                done_.insert(dest);
            }
        }
    }

    // Index fn specialisations.
    for (auto& spec : in_.specializations)
        specs_[spec->name].push_back(spec.get());

    // Index struct specialisations.
    for (auto& ss : in_.struct_specializations)
        struct_specs_[ss.name].push_back(&ss);

    // Process non-generic free functions.
    for (auto& fn_up : in_.functions) {
        auto& fn = *fn_up;
        if (!fn.type_params.empty()) continue;
        auto cloned = clone_fn(fn, {});
        out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(cloned)));
        auto& fn_ref = *out_.functions.back();
        lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
        scan_fn(fn_ref);
        ++stats_.fn_clones;
    }

    // Process function work-list.
    note_fn_worklist_size(worklist_.size());
    while (!worklist_.empty()) {
        auto item = std::move(worklist_.back());
        worklist_.pop_back();
        // Set current depth for enqueue_if_needed during scan_fn
        depth_ = item.depth;
        note_depth(depth_);
        auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
        out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(inst)));
        auto& fn_ref = *out_.functions.back();
        lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
        scan_fn(fn_ref);
        ++stats_.fn_instances;
        note_fn_worklist_size(worklist_.size());
    }
    depth_ = 0;

    // Demand instantiation of generic structs declared via #[type_code=N] eidos Foo<T>;
    // even when no Logos code directly references Foo<T> as a variable type.
    // This ensures that blanket trait-impl methods (e.g. `impl<T> HermesStringify for
    // Array<T>`) get cloned into those structs so tag-dispatch entries can be emitted
    // for blob-literal types like @<I32>[...] (which are produced at C++ level without
    // ever instantiating the Logos struct in user code).
    for (auto& ia : out_.inst_annotations) {
        if (ia.struct_type && !ia.mangled_name.empty() &&
                ia.mangled_name.find("$G") != std::string::npos) {
            record_needed_struct(ia.struct_type);
        }
    }

    // Instantiate all generic structs referenced by the output.
    instantiate_struct_templates();

    // Instantiate all generic enums referenced by the output.
    instantiate_enum_templates();

    // L1.3: `instantiate Foo<T>;` and `pub instantiate Foo<T>;` carry
    // is_root_pin=true. In lazy mode, that means "treat every inherent + trait
    // method of this instance as a worklist root" — the C++
    // `template class Foo<int>;` analog. In eager mode it's a no-op (every
    // method was already cloned).
    if (lazy_methods_) {
        for (auto& ia : out_.inst_annotations) {
            if (!ia.is_root_pin) continue;
            if (!ia.struct_type) continue;
            auto kind = TypeRef(ia.struct_type).kind();
            if (kind != LogosType::Kind::Struct &&
                kind != LogosType::Kind::ZonedStruct)
                continue;
            std::string base{TypeRef(ia.struct_type).struct_name()};
            std::string ia_pkg{TypeRef(ia.struct_type).pkg_name()};
            // Pkg-aware: prefer pkg-qualified method-template lookup. If the
            // struct exists in this pkg (template registered) but has no
            // methods, accept that — don't fall back to bare which would
            // pull in another pkg's same-named struct's methods.
            bool pkg_struct_exists = !ia_pkg.empty() &&
                struct_templates_.find(ia_pkg + "." + base) != struct_templates_.end();
            auto sit = ia_pkg.empty() ? struct_method_templates_.end()
                                      : struct_method_templates_.find(ia_pkg + "." + base);
            if (sit == struct_method_templates_.end() && !pkg_struct_exists)
                sit = struct_method_templates_.find(base);
            if (sit == struct_method_templates_.end()) continue;
            std::string concrete = concrete_struct_name(ia.struct_type);
            // Strip overload-disambiguation suffix `__g__<sig>` so the dest
            // name matches what user call sites produce (and what eager-mode
            // clone_struct_def emits). enqueue_method_inst's match loop picks
            // up overloads via prefix-matching on the stripped name.
            StrSet seen_short;
            for (auto& [mname, _] : sit->second) {
                std::string short_name = mname;
                if (auto p = short_name.find("__g__"); p != std::string::npos)
                    short_name.resize(p);
                if (!seen_short.insert(short_name).second) continue;
                pinned_method_roots_.insert(concrete + "__" + short_name);
                enqueue_method_inst(ia.struct_type, short_name);
            }
        }
    }

    // L1.1: lazy-method drain fixpoint. No-op in eager mode (default) since
    // method_worklist_ stays empty. When LOGOS_LAZY_METHODS=1, scan_fn enqueues
    // method instances on the receiver's concrete struct; draining clones each
    // method, scans its body, which may enqueue more functions or methods. A
    // method body may also reference new generic struct types — re-run
    // instantiate_struct_templates / _enums to drain those.
    while (!method_worklist_.empty() || !worklist_.empty() ||
           !needed_struct_insts_.empty()) {
        drain_method_worklist();
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            depth_ = item.depth;
            auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
            out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(inst)));
            auto& fn_ref = *out_.functions.back();
            lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
            scan_fn(fn_ref);
        }
        if (!needed_struct_insts_.empty()) instantiate_struct_templates();
        instantiate_enum_templates();
        // Resolve deferred method enqueues whose concrete struct now exists.
        if (!deferred_method_enqueues_.empty()) {
            std::vector<std::pair<std::string, std::string>> still;
            for (auto& [cname, mname] : deferred_method_enqueues_) {
                auto cit = concrete_struct_types_.find(cname);
                if (cit != concrete_struct_types_.end())
                    enqueue_method_inst(cit->second, mname);
                else
                    still.emplace_back(std::move(cname), std::move(mname));
            }
            deferred_method_enqueues_ = std::move(still);
        }
        depth_ = 0;
    }

    // L1.2: in lazy mode, pin every trait method on every concrete generic
    // struct before emitting dispatch entries. Without this, the entry-emit
    // loop below would skip methods that haven't been demanded by a direct
    // call site, leaving dispatch tables pointing at unresolved symbols.
    // No-op in eager mode (clone_struct_def already cloned every method).
    if (lazy_methods_) {
        for (auto& sd : out_.structs) {
            std::string base = sd.name;
            auto p = base.find("$G");
            if (p == std::string::npos) continue;  // not a generic instance
            base = base.substr(0, p);
            auto cit = concrete_struct_types_.find(sd.name);
            if (cit == concrete_struct_types_.end()) continue;
            for (auto& impl : out_.impls) {
                if (impl.is_blanket) continue;
                if (impl.trait_name.empty()) continue;
                if (impl.target_type != base) continue;
                for (auto& td : out_.traits) {
                    if (td.name != impl.trait_name) continue;
                    for (auto& tm : td.methods) {
                        std::string sym = sd.name + "__" + tm.name;
                        pinned_method_roots_.insert(sym);
                        enqueue_method_inst(cit->second, tm.name);
                    }
                }
            }
        }
        // Re-drain — methods may transitively pull in more functions/methods.
        while (!method_worklist_.empty() || !worklist_.empty() ||
               !needed_struct_insts_.empty()) {
            drain_method_worklist();
            while (!worklist_.empty()) {
                auto item = std::move(worklist_.back());
                worklist_.pop_back();
                depth_ = item.depth;
                auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
                out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(inst)));
                auto& fn_ref = *out_.functions.back();
                lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
                scan_fn(fn_ref);
            }
            if (!needed_struct_insts_.empty()) instantiate_struct_templates();
            instantiate_enum_templates();
            depth_ = 0;
        }
    }

    // Emit dispatch entries for generic-trait-impls over generic structs
    // (`impl<T> Trait for GenericStruct<T>`).  Sema only emits dispatch
    // entries for concrete impls; for generic ones, we iterate the
    // monomorphized struct set and cross-reference against impls whose
    // target is the template's base name.
    for (auto& sd : out_.structs) {
        if (sd.type_code == 0) continue;
        // Extract the base name: "Array$G1$AnyVal" → "Array".
        std::string base = sd.name;
        if (auto p = base.find("$G"); p != std::string::npos)
            base = base.substr(0, p);
        for (auto& impl : out_.impls) {
            if (impl.is_blanket) continue;  // those use a different path
            if (impl.trait_name.empty()) continue;
            if (impl.target_type != base) continue;
            // Find tag_system for this trait.
            std::string tag_system;
            for (auto& td : out_.traits)
                if (td.name == impl.trait_name) { tag_system = td.tag_dispatch_system; break; }
            if (tag_system.empty()) continue;
            // Emit entries for each method of the trait that's present as
            // a cloned method on the concrete struct.
            for (auto& td : out_.traits) {
                if (td.name != impl.trait_name) continue;
                for (auto& tm : td.methods) {
                    std::string sym = sd.name + "__" + tm.name;
                    // Only emit if the method actually exists (cloned by mono).
                    bool has = false;
                    for (auto& sm : sd.methods) if (bare_fn_name(sm->name) == sym) { has = true; break; }
                    if (!has) {
                        for (auto& f : out_.functions) if (bare_fn_name(f->name) == sym) { has = true; break; }
                    }
                    if (!has) continue;
                    // Dedup: skip if an equivalent entry already exists (sema
                    // may have emitted one for a concrete specialization).
                    bool dup = false;
                    for (auto& e : out_.dispatch_entries)
                        if (e.tag_system == tag_system && e.trait_name == impl.trait_name &&
                            e.method_name == tm.name && e.type_code == sd.type_code)
                            { dup = true; break; }
                    if (dup) continue;
                    lir::LDispatchEntry de;
                    de.tag_system     = tag_system;
                    de.trait_name     = impl.trait_name;
                    de.method_name    = tm.name;
                    de.fn_symbol      = sym;
                    de.impl_type_name = sd.name;
                    de.type_code      = sd.type_code;
                    out_.dispatch_entries.push_back(std::move(de));
                    ++stats_.dispatch_entries;
                }
            }
        }
    }

    out_.diags          = std::move(in_.diags);
    out_.binary_symbols = std::move(in_.binary_symbols);

    // B.6 Stage 1b: full bulk emit pass replaced with cache-only walker.
    // Functions, struct methods, and enum-impl methods are emit'd eagerly at
    // their push_back sites. The remaining items moved wholesale from in_ →
    // out_ (impl methods, const value exprs) carry mirror_offset_ from sema
    // but need their fresh out_.mirror_table cache populated for downstream
    // offset → ptr reverse lookups (mlir_gen, borrow_check).
    lir_mirror_populate_moved(out_, *out_.mirror_table);

    if (stats_enabled_) {
        std::fprintf(stderr,
            "[mono-stats] fn_clones=%llu fn_inst=%llu method_inst=%llu "
            "struct_inst=%llu enum_inst=%llu dispatch=%llu "
            "peak_fn_wl=%zu peak_method_wl=%zu peak_depth=%d\n",
            (unsigned long long)stats_.fn_clones,
            (unsigned long long)stats_.fn_instances,
            (unsigned long long)stats_.method_instances,
            (unsigned long long)stats_.struct_instances,
            (unsigned long long)stats_.enum_instances,
            (unsigned long long)stats_.dispatch_entries,
            stats_.peak_fn_worklist,
            stats_.peak_method_worklist,
            stats_.peak_depth);
    }

    return std::move(out_);
}

// ── Public entry point ─────────────────────────────────────────────────────

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth) noexcept(false) {
    Mono m(max_instantiation_depth);
    return m.run(std::move(prog), max_instantiation_depth);
}

} // namespace logos::compiler
