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
#include "module_loader.hpp"

#include <logos/compiler/lir_mirror.hpp>

namespace logos::compiler {

void Mono::enqueue_blanket_concrete(const BlanketImplInfo& bi,
                                    const std::string& tmpl_prefix,
                                    const std::string& concrete, TypeRef candidate_t) {
    if (!candidate_t) return;
    for (auto& tfn : in_.functions) {
        // Strip pkg prefix (`pkg.`) before matching the synthetic
        // `$blanket$...` template prefix.
        std::string tn = std::string(tfn.name());
        if (auto dot = tn.rfind('.'); dot != std::string::npos)
            tn = tn.substr(dot + 1);
        if (tn.rfind(tmpl_prefix, 0) != 0) continue;
        // Method may carry `__f__sig` / `__g__sig`. Preserve sig in dest so
        // subsequent enqueues that mangle with type_args produce matching names.
        std::string method = tn.substr(tmpl_prefix.size());
        std::string concrete_pkg;
        for (auto& sd : out_.structs)
            if (sd.name() == concrete) { concrete_pkg = std::string(sd.pkg()); break; }
        if (concrete_pkg.empty())
            for (auto& ed : out_.enums)
                if (ed.name() == concrete) { concrete_pkg = std::string(ed.pkg()); break; }
        // Coexistence: module-qualify the type part (matches concrete_struct_name
        // used at the call site) so two modules' blanket-method instances (e.g.
        // `Widget__type_id` from `impl<T> Any for T`) don't collide at link.
        std::string concrete_q = concrete + type_module_suffix(concrete_pkg);
        std::string bare_dest = concrete_q + "__" + method;
        std::string dest = concrete_pkg.empty() ? bare_dest
                                                : concrete_pkg + "." + bare_dest;
        if (done_.count(dest)) continue;
        SubstMap subst;
        subst[bi.target_typevar] = candidate_t;
        // Skip blanket methods carrying type-params the target binding can't
        // supply (e.g. the OUTPUT `D` in `impl<S, D: From<S>> Into<D> for S`) —
        // those instantiate at the real call site with full type_args.
        bool all_tp_bound = true;
        for (auto& tp : tfn.type_params()) {
            if (tp.is_variadic()) continue;
            if (!subst.count(std::string(tp.name()))) { all_tp_bound = false; break; }
        }
        if (!all_tp_bound) continue;
        WorkItem wi;
        wi.mangled = dest;
        wi.tmpl    = tfn;
        wi.subst   = std::move(subst);
        wi.depth   = 0;
        worklist_.push_back(std::move(wi));
        done_.insert(dest);
    }
}

// Prune-mode (entry_points_) helpers. enqueue_free_fn records a reachable
// non-generic free fn once; drain_free_fn_queue clones each queued fn (same
// body-clone / binary-symbol-stub logic as the eager root loop), and scan_fn
// — run on each cloned body — calls enqueue_free_fn again for its direct
// callees, so the queue grows to the full reachable free-fn closure.
void Mono::enqueue_free_fn(const std::string& name) {
    // Only names that are actually non-generic free fns of this program are
    // reachability roots. Extern intrinsics, methods (Struct__method) and
    // generic instances aren't in the index — those flow through the
    // method/generic worklists or resolve from a linked archive.
    if (!free_fn_index_.count(name)) return;
    if (!free_fn_queued_.insert(name).second) return;
    free_fn_queue_.push_back(name);
}

void Mono::drain_free_fn_queue() {
    while (!free_fn_queue_.empty()) {
        std::string name = std::move(free_fn_queue_.back());
        free_fn_queue_.pop_back();
        auto it = free_fn_index_.find(name);
        if (it == free_fn_index_.end()) continue;
        lir_view::FunctionView fn = it->second;
        if (has_prev_out_ && done_.count(std::string(fn.name()))) continue;
        // Binary-symbol fast path (mirrors the eager loop): the body lives in
        // a linked archive, so emit a signature-only stub and skip the scan —
        // the archive is self-contained for this fn's transitive closure.
        if (!in_.binary_symbols.empty() && in_.binary_symbols.count(std::string(fn.name()))) {
            auto stub = clone_fn_signature(fn, {}, {});
            out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
            if (has_prev_out_) done_.insert(std::string(fn.name()));
            ++stats_.fn_clones;
            continue;
        }
        auto cloned = clone_fn(fn, {});
        out_.functions.push_back(lir_mirror_emit_fn_view(out_, cloned));
        scan_fn(cloned);
        if (has_prev_out_) done_.insert(std::string(fn.name()));
        ++stats_.fn_clones;
    }
}

lir::LProgram Mono::run(lir::LProgram&& in, int /*max_depth*/) {
    in_ = std::move(in);

    // Coexistence: module-qualify type-keyed names consistently with sema/mlir.
    TypeModuleScope _type_module_scope(&in_.pkg_module_ids);

    // Stage 3g.1: in_.mirror_table is already comprehensive — sema's end-of-
    // run pass emitted every stmt/block/pattern, and LirBuilder mirrored each
    // LExpr at construction. No top-up needed here.

    // M3 step 3: confirm receipt of the stdlib exports catalog. Trace-only
    // for now; future steps consume `stdlib_exports_` to short-circuit the
    // in_-walks that build templates_/struct_templates_/etc.
    if (stdlib_exports_ && std::getenv("LOGOS_TRACE_PHASES")) {
        std::fprintf(stderr,
            "[trace] mono received stdlib_exports: %zu struct, %zu enum, %zu fn templates, %zu blanket, %zu concrete impls\n",
            stdlib_exports_->struct_templates.size(),
            stdlib_exports_->enum_templates.size(),
            stdlib_exports_->fn_templates.size(),
            stdlib_exports_->blanket_impls.size(),
            stdlib_exports_->concrete_impls.size());
    }

    if (has_prev_out_) {
        // M6.2 incremental: seed out_ with prev iter's mono output so
        // its cloned generic instances + passthrough non-generics are
        // preserved. prev_out_.mirror_table / type_pool / pools are
        // moved over too (same shared_ptrs as in_'s thanks to the
        // SemaCache, so refcount semantics are preserved when in_'s
        // pools also move in below).
        out_ = std::move(prev_out_);
    } else {
        // Output mirror — populated incrementally as functions are cloned
        // (so scan_fn can dispatch via lir_view). Empty at start of mono.
        out_.mirror_table = std::make_unique<LirMirrorTable>();
    }

    // M6.2: when has_prev_out_, in_'s pools/type_pool are the SAME shared_ptr
    // values as prev_out_'s (via SemaCache), so this move is effectively a
    // no-op for the pointed-to data — refcounts stay the same.
    // Module system: carry the package→module map forward so metaprog delta
    // iters (which feed out_ back in as the next in_) keep qualifying symbols.
    out_.pkg_module_ids      = in_.pkg_module_ids;
    out_.hstatic_registry_   = std::move(in_.hstatic_registry_);
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
    out_.type_pool           = std::move(in_.type_pool);

    if (has_prev_out_) {
        // Seed done_ tracking sets from prev_out's items so the passthrough
        // and clone passes below skip work for items already cloned in the
        // previous iter. Done-tracking is name-keyed (mangled name for fns,
        // pkg::name for structs/enums). For struct methods we also seed
        // done_methods_ from each struct's .methods vector.
        for (auto& fp : out_.functions)
            if (fp) done_.insert(std::string(fp.name()));
        for (auto& fp : out_.specializations)
            if (fp) done_.insert(std::string(fp.name()));
        for (auto& sd : out_.structs) {
            std::string sd_name(sd.name()), sd_pkg(sd.pkg());
            auto qkey = sd_pkg.empty() ? sd_name : (sd_pkg + "." + sd_name);
            struct_done_.insert(qkey);
            struct_done_.insert(sd_name);
            sd.each_method([&](lir_view::FunctionView m) {
                done_methods_.insert(sd_name + "__" + std::string(m.name()));
                done_.insert(std::string(m.name()));
            });
        }
        for (auto& sd : out_.struct_specializations) {
            std::string sd_name(sd.name()), sd_pkg(sd.pkg());
            auto qkey = sd_pkg.empty() ? sd_name : (sd_pkg + "." + sd_name);
            struct_done_.insert(qkey);
            struct_done_.insert(sd_name);
            sd.each_method([&](lir_view::FunctionView m) {
                done_methods_.insert(sd_name + "__" + std::string(m.name()));
                done_.insert(std::string(m.name()));
            });
        }
        for (auto& ed : out_.enums) {
            std::string ed_name(ed.name()), ed_pkg(ed.pkg());
            auto qkey = ed_pkg.empty() ? ed_name : (ed_pkg + "." + ed_name);
            enum_done_.insert(qkey);
            enum_done_.insert(ed_name);
        }
    }

    // Index associated type impls for subst_type resolution.
    // Also split blanket impls into a separate table for fallback lookup.
    const TypePoolImpl* impl_pool = out_.type_pool.impl();
    for (auto& impl : out_.impls) {
        if (impl.is_blanket()) {
            BlanketImplInfo info;
            info.trait_name     = std::string(impl.trait_name());
            info.bound_trait    = std::string(impl.bound_trait());
            for (auto sv : impl.extra_bounds()) info.extra_bounds.emplace_back(sv);
            info.target_typevar = std::string(impl.target_type());
            impl.each_assoc_type([&](lir_view::AssocEntryView ae) {
                info.assoc_types[std::string(ae.name())] = ae.type(impl_pool);
            });
            impl.each_primary_assoc_eq([&](lir_view::AssocEntryView ae) {
                info.primary_assoc_eqs.emplace_back(std::string(ae.name()), ae.type(impl_pool));
            });
            impl.each_extra_assoc_eq([&](lir_view::ExtraEqView ee) {
                std::vector<std::pair<std::string, TypeRef>> eqs;
                ee.each_eq([&](lir_view::AssocEntryView ae) {
                    eqs.emplace_back(std::string(ae.name()), ae.type(impl_pool));
                });
                info.extra_assoc_eqs.emplace_back(std::string(ee.trait()), std::move(eqs));
            });
            blanket_impls_.push_back(std::move(info));
        } else {
            // G156-1: index assoc types by the trait's concrete type-args
            // (suffixed) so a suffixed AssocType node `<P as Trait<i64>>::A`
            // (two `Trait<T>` impls for one type) resolves to the right impl;
            // keep the plain key (first-wins) for bare / non-generic
            // projections. Suffix format is byte-identical to sema's
            // SemaChecker::trait_targ_suffix.
            std::string impl_trait(impl.trait_name());
            std::string impl_target(impl.target_type());
            auto trait_args = impl.trait_type_args(impl_pool);
            std::string targ_sfx;
            if (!trait_args.empty()) {
                targ_sfx = "$G" + std::to_string(trait_args.size());
                for (auto a : trait_args) {
                    targ_sfx += "$";
                    std::string ts = a ? type_str(a) : std::string("?");
                    for (char& c : ts)
                        if (!(std::isalnum((unsigned char)c) || c == '_')) c = '_';
                    targ_sfx += ts;
                }
            }
            impl.each_assoc_type([&](lir_view::AssocEntryView ae) {
                std::string aname(ae.name());
                TypeRef atype = ae.type(impl_pool);
                assoc_impls_[impl_trait + targ_sfx + "::" + impl_target + "::" + aname] = atype;
                if (!targ_sfx.empty())
                    assoc_impls_.emplace(impl_trait + "::" + impl_target + "::" + aname, atype);
            });
            if (!impl_trait.empty())
                concrete_impls_.insert(impl_trait + "::" + impl_target);
        }
    }

    // Index generic struct templates; pass-through plain structs immediately.
    // Pkg-qualified entries enable cross-pkg same-named struct disambig;
    // bare alias kept as last-wins for back-compat with callers operating
    // by base name only.
    for (auto& sd : in_.structs) {
        std::string sd_name(sd.name()), sd_pkg(sd.pkg());
        // Index EVERY struct (generic + non-generic) for the DstRef
        // canonicalisation's is_dst lookup (struct_templates_ below is
        // generics-only). Bare last-wins + pkg-qualified, mirroring below.
        if (!sd_pkg.empty()) all_structs_[sd_pkg + "." + sd_name] = sd;
        all_structs_[sd_name] = sd;
        if (!sd.type_params_empty()) {
            if (!sd_pkg.empty())
                struct_templates_[sd_pkg + "." + sd_name] = sd;
            struct_templates_[sd_name] = sd;  // stable: in_.structs not moved
        }
        // L1: build (base_struct, short_method_name) → template index for lazy
        // method instantiation. After unification, method fn-names are
        // pkg-qualified (`pkg.Base__method__f__sig`); extract short name
        // by finding `Base__` and reading until next `__`.
        std::string prefix = sd_name + "__";
        sd.each_method([&](lir_view::FunctionView m) {
            std::string m_name(m.name());
            std::string short_name;
            auto p = m_name.find(prefix);
            if (p != std::string::npos) {
                size_t start = p + prefix.size();
                // The mangled tail looks like `<short>__[fg]__<sig>` (or just
                // `<short>` for body-less / non-overloaded forms). For methods
                // whose own name starts with `__` (e.g. `__drop_in_place`),
                // a naive `find("__", start)` returns offset `start` itself —
                // truncating short to empty. Walk to the `__f__` / `__g__`
                // overload-disambig boundary instead, falling back to plain
                // `__` only when neither is present.
                auto find_sig_boundary = [&](size_t from) -> size_t {
                    auto pf = m_name.find("__f__", from);
                    auto pg = m_name.find("__g__", from);
                    if (pf == std::string::npos) return pg;
                    if (pg == std::string::npos) return pf;
                    return std::min(pf, pg);
                };
                auto end = find_sig_boundary(start);
                if (end == std::string::npos)
                    end = m_name.find("__", start);
                // Overloaded generic methods (`__g__<sig>` tail): keep the
                // full tail as the key — a short-name key is one slot and
                // silently drops all but the last overload (Pin<&T>::new vs
                // Pin<&mut T>::new vs Pin<Box<T>>::new). Every lookup site
                // already prefix-matches `<short>__g__*`.
                if (end != std::string::npos &&
                    m_name.compare(end, 5, "__g__") == 0)
                    end = std::string::npos;
                short_name = (end == std::string::npos)
                             ? m_name.substr(start)
                             : m_name.substr(start, end - start);
            } else {
                short_name = m_name;
            }
            if (!sd_pkg.empty())
                struct_method_templates_[sd_pkg + "." + sd_name][short_name] = m;
            struct_method_templates_[sd_name][short_name] = m;
        });
    }
    // Move non-generic structs to output.
    // M6.2: when has_prev_out_, skip structs that prev_out already
    // passed through (struct_done_ seeded from prev_out at top of
    // run). In default mode (cache=null), the skip+insert pair is a
    // no-op for the first non-generic in_.structs walk; we keep the
    // insert so subsequent code paths that consult struct_done_
    // observe consistent state.
    for (auto& sd : in_.structs) {
        if (sd.type_params_empty()) {
            std::string sd_name(sd.name()), sd_pkg(sd.pkg());
            if (has_prev_out_) {
                auto qkey = sd_pkg.empty() ? sd_name : (sd_pkg + "." + sd_name);
                if (struct_done_.count(qkey) || struct_done_.count(sd_name)) continue;
            }
            auto nd = clone_struct_def(sd, {}, {}, sd_name);
            out_.structs.push_back(lir_mirror_emit_struct_view(out_, nd));
        }
    }

    // Index generic enum templates; pass-through plain enums.
    for (auto& ed : in_.enums) {
        std::string ed_name(ed.name()), ed_pkg(ed.pkg());
        if (!ed.type_params_empty()) {
            enum_templates_[ed_name] = ed;
        } else {
            if (has_prev_out_) {
                auto qkey = ed_pkg.empty() ? ed_name : (ed_pkg + "." + ed_name);
                if (enum_done_.count(qkey) || enum_done_.count(ed_name)) continue;
            }
            out_.enums.push_back(ed);
        }
    }

    // Index generic fn templates.
    for (auto& fn : in_.functions) {
        if (!fn.type_params_empty())
            templates_[std::string(fn.name())] = fn;
    }

    // Eagerly instantiate blanket-impl methods for every concrete type that
    // satisfies the blanket's bound.  Each clone produces a function named
    // `Concrete__method` alongside the original `$blanket$...__method`
    // template, so normal call resolution (`I64__storage_new`) works.
    //
    // SKIP in reachability/JIT mode (entry_points non-empty): the metacall-JIT
    // compiles a tiny hook module and lists every program symbol as required.
    // The eager over-all-types pass would pollute that set with `<Type>__m`
    // for every program type — incl. ast_only metaprog types (e.g.
    // `Ident__type_id` from the unbounded `impl<T> Any for T` blanket) whose
    // bodies are never codegen'd. In JIT mode blanket methods clone lazily.
    for (auto& bi : blanket_impls_) {
        if (!entry_points_.empty()) break;
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
                if (sd.type_params_empty()) candidates.push_back(std::string(sd.name()));
            for (auto& ed : out_.enums)
                if (ed.type_params_empty()) candidates.push_back(std::string(ed.name()));
        } else {
            // Direct concrete impls of bound_trait.
            for (auto& impl : out_.impls) {
                if (impl.is_blanket()) continue;
                if (impl.trait_name() != bi.bound_trait) continue;
                candidates.push_back(std::string(impl.target_type()));
            }
            // Chain-satisfiers: types whose bound_trait impl is itself
            // derived from a blanket (e.g. Foo blanket on Container, where
            // K's Container is via the Primitive→Container blanket). Walk
            // every concrete type and ask the recursive resolver.
            // Phase 2: deep `mono_concrete_satisfies_bound` check when
            // the candidate has a constructible TypeRef (struct/enum/
            // primitive); falls back to the shallow recursive check
            // only when build_concrete_typeref returns null.
            StrSet already(candidates.begin(), candidates.end());
            auto consider = [&](const std::string& cn) {
                if (already.count(cn)) return;
                StrSet seen;
                auto tref = build_concrete_typeref(cn);
                bool ok = tref
                    ? mono_concrete_satisfies_bound(bi.bound_trait, tref, seen)
                    : mono_has_impl_recursive(bi.bound_trait, cn, seen);
                if (ok) {
                    candidates.push_back(cn);
                    already.insert(cn);
                }
            };
            for (auto& sd : out_.structs)
                if (sd.type_params_empty()) consider(std::string(sd.name()));
            for (auto& ed : out_.enums)
                if (ed.type_params_empty()) consider(std::string(ed.name()));
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
                    // its assoc_types. Phase 2: deep
                    // `mono_concrete_satisfies_bound` when the candidate's
                    // TypeRef is constructible; falls back to shallow.
                    auto concrete_t_assoc = build_concrete_typeref(concrete);
                    for (auto& bj : blanket_impls_) {
                        if (bj.trait_name != trait) continue;
                        StrSet seen_pri;
                        bool ok = bj.bound_trait.empty()
                            || (concrete_t_assoc
                                ? mono_concrete_satisfies_bound(bj.bound_trait,
                                                                concrete_t_assoc,
                                                                seen_pri)
                                : mono_has_impl_recursive(bj.bound_trait,
                                                          concrete, seen_pri));
                        if (ok) {
                            for (auto& eb : bj.extra_bounds) {
                                StrSet seen_eb;
                                bool eb_ok = concrete_t_assoc
                                    ? mono_concrete_satisfies_bound(eb, concrete_t_assoc, seen_eb)
                                    : mono_has_impl_recursive(eb, concrete, seen_eb);
                                if (!eb_ok) { ok = false; break; }
                            }
                        }
                        if (!ok) continue;
                        auto bait = bj.assoc_types.find(aname);
                        if (bait == bj.assoc_types.end()) continue;
                        // Substitute target_typevar → concrete struct type and
                        // recursively resolve so blanket bodies that reference
                        // the typevar (e.g. `type P = DT::Prim`) reduce to the
                        // concrete type before equality compare.
                        TypeRef concrete_t = concrete_t_assoc;
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
            // Phase 2: build the candidate TypeRef once via the
            // shared `build_concrete_typeref` helper and reuse it for
            // bound checks AND for per-method substitution below. The
            // deep `mono_concrete_satisfies_bound` unifies the bound's
            // impl pattern against the candidate's type-args and
            // recurses through the impl's own bounds — see
            // [[baghunt-mono-blanket-bound-recursion]] for the same fix
            // pattern applied in `method_bound_ok`.
            TypeRef candidate_t = build_concrete_typeref(concrete);
            // Multi-bound blanket: `impl<T: A + B + …> Trait for T` — only
            // instantiate for `concrete` types that satisfy *every* extra
            // bound, not just the primary one. Falls back to the shallow
            // recursive check only if `candidate_t` is null (which today
            // happens only for unknown type names).
            bool all_extra_satisfied = true;
            for (auto& eb : bi.extra_bounds) {
                StrSet seen;
                bool ok = candidate_t
                    ? mono_concrete_satisfies_bound(eb, candidate_t, seen)
                    : mono_has_impl_recursive(eb, concrete, seen);
                if (!ok) {
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
            // For each method of the blanket, enqueue an instantiation
            // into the main worklist. Phase 2 step 3: previously this
            // cloned inline (clone_fn + push_back + mirror_emit +
            // scan_fn), running BEFORE the worklist drain and skipping
            // depth/dedup invariants the drain enforces. Now both
            // generic-fn instantiation and blanket-method
            // instantiation share the same drain loop, the same
            // done_ memo (cycle protection), and the same depth_
            // counter; new bodies that the drain discovers are
            // automatically enqueued.
            enqueue_blanket_concrete(bi, tmpl_prefix, concrete, candidate_t);
        }
    }

    // Index fn specialisations.
    for (auto& spec : in_.specializations)
        specs_[std::string(spec.name())].push_back(spec);

    // Index struct specialisations.
    for (auto& ss : in_.struct_specializations)
        struct_specs_[std::string(ss.name())].push_back(ss);

    // Process non-generic free functions. The lazy_methods_ path handles
    // struct methods separately.
    //
    // Default (eager) mode: every non-generic free fn is a clone root.
    //
    // Prune mode (entry_points_ non-empty — the metacall-thunk JIT): only fns
    // transitively reachable from the entry points are cloned. We index every
    // non-generic free fn, seed free_fn_queue_ with the entry points, and
    // drain_free_fn_queue() clones each + runs scan_fn, which enqueues its
    // direct free-fn callees (here), its generic instances (worklist_) and its
    // method calls (method_worklist_). Those drains converge in the fixpoint
    // loop below, so the whole reachable closure — and nothing else — is
    // materialised. This is the reachability-through-mono realisation of the
    // long-standing "metacall JIT shouldn't process the whole stdlib" TODO:
    // the user's test fns + their iterator monomorphizations, never called by
    // a macro thunk, are simply never cloned.
    if (!entry_points_.empty()) {
        for (auto& fn_up : in_.functions) {
            if (!fn_up || !fn_up.type_params_empty()) continue;
            free_fn_index_.emplace(std::string(fn_up.name()), fn_up);
        }
        for (auto& ep : entry_points_) enqueue_free_fn(ep);
        drain_free_fn_queue();
    } else {
        for (auto& fn : in_.functions) {
            if (!fn.type_params_empty()) continue;
            // M6.2: skip when prev_out_ already cloned this fn (done_ seed).
            // Gated on has_prev_out_ so the default-mode walk doesn't acquire
            // the new done_ insert side-effect (which would shift state for
            // unrelated code paths consulting done_ later).
            if (has_prev_out_ && done_.count(std::string(fn.name()))) continue;
            // Binary-symbol fast path: the body lives in liblstdlib.a (or a
            // user -L archive). mlir_gen would skip body emission anyway, so
            // the deep body clone + mirror emit + scan_fn are pure waste here.
            // All transitive generic instantiations referenced by this body
            // are already pre-baked in the same archive (otherwise the archive
            // wouldn't link), so dropping the scan can't leave the worklist
            // missing a required instantiation.
            if (!in_.binary_symbols.empty() && in_.binary_symbols.count(std::string(fn.name()))) {
                auto stub = clone_fn_signature(fn, {}, {});
                out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
                if (has_prev_out_) done_.insert(std::string(fn.name()));
                ++stats_.fn_clones;
                continue;
            }
            auto cloned = clone_fn(fn, {});
            out_.functions.push_back(lir_mirror_emit_fn_view(out_, cloned));
            scan_fn(cloned);
            if (has_prev_out_) done_.insert(std::string(fn.name()));
            ++stats_.fn_clones;
        }
    }

    // Process function work-list.
    note_fn_worklist_size(worklist_.size());
    while (!worklist_.empty()) {
        auto item = std::move(worklist_.back());
        worklist_.pop_back();
        // Set current depth for enqueue_if_needed during scan_fn
        depth_ = item.depth;
        note_depth(depth_);
        // Binary-symbol fast path: the archive already has the body of this
        // generic instance (stdlib emit_module pre-baked it). Emit a
        // signature-only stub so mlir_gen can forward-declare; skip mirror
        // + scan_fn to save the deep body walk.
        if (!in_.binary_symbols.empty() &&
            in_.binary_symbols.count(item.mangled)) {
            auto stub = clone_fn_signature(item.tmpl, item.subst, item.packs);
            stub.name = item.mangled;
            stub.type_params.clear();
            out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
            ++stats_.fn_instances;
            note_fn_worklist_size(worklist_.size());
            continue;
        }
        // T2-24 (B): const-arg spec — bake the literals into this clone by
        // exposing them to subst_expr's VarRef case for the body walk only.
        current_const_args_.clear();
        for (auto& [pn, cv] : item.const_args) current_const_args_[pn] = cv;
        auto inst = instantiate_fn(item.tmpl, item.mangled, item.subst, item.packs);
        current_const_args_.clear();
        out_.functions.push_back(lir_mirror_emit_fn_view(out_, inst));
        scan_fn(inst);
        ++stats_.fn_instances;
        note_fn_worklist_size(worklist_.size());
    }
    depth_ = 0;

    // Supplementary blanket pass: scan_fn (above) recorded every concrete type
    // coerced to a `dyn Trait` that the eager blanket pass misses — PRIMITIVES
    // (`&i64 as &dyn Any`) and GENERIC STRUCT INSTANTIATIONS (`&Box<i64> as &dyn`,
    // created later by instantiate_struct_templates). Instantiate each blanket
    // ONLY for those actually-coerced targets (a sema-validated coercion means the
    // type satisfies any bound; done_ dedups types the eager loop already did).
    // The collected TypeRef drives the substitution directly, so the target need
    // not yet exist in out_.structs. Then drain the new worklist items.
    // (Runs in prune mode too: dyn_coerced_targets_ is populated by scan_fn
    // over the reachable closure, so this instantiates blanket methods only
    // for types actually coerced to `dyn` by reachable code — needed or the
    // vtable slot stays null and dispatch jumps to 0x0.)
    if (!dyn_coerced_targets_.empty()) {
        for (auto& bi : blanket_impls_) {
            auto pit = dyn_coerced_targets_.find(bi.trait_name);
            if (pit == dyn_coerced_targets_.end()) continue;
            std::string tmpl_prefix =
                "$blanket$" + bi.trait_name + "$" + bi.bound_trait
                + "$" + bi.target_typevar + "__";
            for (auto& [nm, tref] : pit->second) {
                // The scan over-collects coerced targets: generic container
                // code (Vec etc.) emits raw-buffer reinterprets like
                // `*const u8 as &dyn Trait` that look like a dyn-coercion but
                // are not — their pointee (u8) does NOT satisfy the blanket's
                // bound. Instantiating the blanket for it clones e.g. `u8__d`
                // whose `self.tag()` resolves to a nonexistent `u8__tag` →
                // MLIR verify failure. Mirror the eager pass's bound filter:
                // instantiate only for targets that actually satisfy the bound.
                if (tref) {
                    if (!bi.bound_trait.empty()) {
                        StrSet seen;
                        if (!mono_concrete_satisfies_bound(bi.bound_trait, tref, seen))
                            continue;
                    }
                    bool extra_ok = true;
                    for (auto& eb : bi.extra_bounds) {
                        StrSet seen;
                        if (!mono_concrete_satisfies_bound(eb, tref, seen)) { extra_ok = false; break; }
                    }
                    if (!extra_ok) continue;
                }
                enqueue_blanket_concrete(bi, tmpl_prefix, nm, tref);
            }
        }
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            depth_ = item.depth;
            if (!in_.binary_symbols.empty() && in_.binary_symbols.count(item.mangled)) {
                auto stub = clone_fn_signature(item.tmpl, item.subst, item.packs);
                stub.name = item.mangled; stub.type_params.clear();
                out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
                continue;
            }
            auto inst = instantiate_fn(item.tmpl, item.mangled, item.subst, item.packs);
            out_.functions.push_back(lir_mirror_emit_fn_view(out_, inst));
            scan_fn(inst);
        }
        depth_ = 0;
    }

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
            auto* sit_inner = find_struct_method_templates_guarded(ia_pkg, base);
            if (!sit_inner) continue;
            std::string concrete = concrete_struct_name(ia.struct_type);
            // Strip overload-disambiguation suffix `__g__<sig>` so the dest
            // name matches what user call sites produce (and what eager-mode
            // clone_struct_def emits). enqueue_method_inst's match loop picks
            // up overloads via prefix-matching on the stripped name.
            StrSet seen_short;
            for (auto& [mname, _] : *sit_inner) {
                std::string short_name = mname;
                if (auto p = short_name.find("__g__"); p != std::string::npos)
                    short_name.resize(p);
                if (!seen_short.insert(short_name).second) continue;
                pinned_method_roots_.insert(concrete + "__" + short_name);
                enqueue_method_inst(ia.struct_type, short_name);
            }
        }
    }

    // Drain deferred method enqueues from the non-generic-fn scan above
    // exactly once now that line-426 instantiate_struct_templates() has
    // populated concrete_struct_types_. Each resolvable entry promotes to
    // method_worklist_; the rest stay in deferred for later drain inside
    // the fixpoint loop below. One-shot to avoid the loop spinning on
    // entries whose concrete struct is never produced.
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

    // L1.1: lazy-method drain fixpoint. No-op in eager mode (default) since
    // method_worklist_ stays empty. When LOGOS_LAZY_METHODS=1, scan_fn enqueues
    // method instances on the receiver's concrete struct; draining clones each
    // method, scans its body, which may enqueue more functions or methods. A
    // method body may also reference new generic struct types — re-run
    // instantiate_struct_templates / _enums to drain those.
    while (!method_worklist_.empty() || !worklist_.empty() ||
           !needed_struct_insts_.empty() || !free_fn_queue_.empty()) {
        // Prune mode: method/generic bodies cloned in this fixpoint may call
        // further non-generic free fns — clone those before the rest so their
        // own scan_fn can feed the same iteration's worklists.
        drain_free_fn_queue();
        drain_method_worklist();
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            depth_ = item.depth;
            if (!in_.binary_symbols.empty() &&
                in_.binary_symbols.count(item.mangled)) {
                auto stub = clone_fn_signature(item.tmpl, item.subst, item.packs);
                stub.name = item.mangled;
                stub.type_params.clear();
                out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
                continue;
            }
            auto inst = instantiate_fn(item.tmpl, item.mangled, item.subst, item.packs);
            out_.functions.push_back(lir_mirror_emit_fn_view(out_, inst));
            scan_fn(inst);
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
            std::string sd_name(sd.name());
            std::string base = sd_name;
            auto p = base.find("$G");
            if (p == std::string::npos) continue;  // not a generic instance
            base = base.substr(0, p);
            auto cit = concrete_struct_types_.find(sd_name);
            if (cit == concrete_struct_types_.end()) continue;
            for (auto& impl : out_.impls) {
                if (impl.is_blanket()) continue;
                std::string impl_trait(impl.trait_name());
                if (impl_trait.empty()) continue;
                if (impl.target_type() != base) continue;
                for (auto& td : out_.traits) {
                    if (td.name() != impl_trait) continue;
                    td.each_method([&](lir_view::TraitMethodSigView tm) {
                        std::string mname(tm.name());
                        std::string sym = sd_name + "__" + mname;
                        pinned_method_roots_.insert(sym);
                        enqueue_method_inst(cit->second, mname);
                    });
                }
                // `Drop` is special: its `drop` is invoked IMPLICITLY by
                // drop-glue (SDrop), which scan_fn does not demand-pin — and
                // the lang-item `Drop` trait lives in a precompiled library, so
                // it is absent from out_.traits and the loop above pins
                // nothing. Without this, a generic struct's `impl Drop` (the
                // common `Box`/`Vec` shape) never instantiates `drop` in lazy
                // mode and the destructor silently never runs. Pin it directly.
                if (impl_trait == "Drop") {
                    pinned_method_roots_.insert(sd_name + "__drop");
                    enqueue_method_inst(cit->second, "drop");
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
                if (!in_.binary_symbols.empty() &&
                    in_.binary_symbols.count(item.mangled)) {
                    auto stub = clone_fn_signature(item.tmpl, item.subst, item.packs);
                    stub.name = item.mangled;
                    stub.type_params.clear();
                    out_.functions.push_back(lir_mirror_emit_fn_view(out_, stub));
                    continue;
                }
                auto inst = instantiate_fn(item.tmpl, item.mangled, item.subst, item.packs);
                out_.functions.push_back(lir_mirror_emit_fn_view(out_, inst));
                scan_fn(inst);
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
        if (sd.type_code() == 0) continue;
        std::string sd_name(sd.name());
        // Extract the base name: "Array$G1$AnyVal" → "Array".
        std::string base = sd_name;
        if (auto p = base.find("$G"); p != std::string::npos)
            base = base.substr(0, p);
        for (auto& impl : out_.impls) {
            if (impl.is_blanket()) continue;  // those use a different path
            std::string impl_trait(impl.trait_name());
            if (impl_trait.empty()) continue;
            if (impl.target_type() != base) continue;
            // Find tag_system for this trait.
            std::string tag_system;
            for (auto& td : out_.traits)
                if (td.name() == impl_trait) { tag_system = std::string(td.tag_dispatch_system()); break; }
            if (tag_system.empty()) continue;
            // Emit entries for each method of the trait that's present as
            // a cloned method on the concrete struct.
            for (auto& td : out_.traits) {
                if (td.name() != impl_trait) continue;
                td.each_method([&](lir_view::TraitMethodSigView tm) {
                    std::string mname(tm.name());
                    std::string sym = sd_name + "__" + mname;
                    // Only emit if the method actually exists (cloned by mono).
                    // Capture the actual (pkg-qualified, sig-suffixed) symbol
                    // so the dispatch table init resolves correctly.
                    std::string actual_sym;
                    sd.each_method([&](lir_view::FunctionView sm) {
                        if (actual_sym.empty() && bare_fn_name(sm.name()) == sym)
                            actual_sym = std::string(sm.name());
                    });
                    if (actual_sym.empty()) {
                        for (auto& f : out_.functions)
                            if (bare_fn_name(f.name()) == sym) { actual_sym = std::string(f.name()); break; }
                    }
                    if (actual_sym.empty()) return;
                    // Dedup: skip if an equivalent entry already exists (sema
                    // may have emitted one for a concrete specialization).
                    bool dup = false;
                    for (auto& e : out_.dispatch_entries)
                        if (e.tag_system == tag_system && e.trait_name == impl_trait &&
                            e.method_name == mname && e.type_code == sd.type_code())
                            { dup = true; break; }
                    if (dup) return;
                    lir::LDispatchEntry de;
                    de.tag_system     = tag_system;
                    de.trait_name     = impl_trait;
                    de.method_name    = mname;
                    de.fn_symbol      = std::move(actual_sym);
                    de.impl_type_name = sd_name;
                    de.type_code      = sd.type_code();
                    out_.dispatch_entries.push_back(std::move(de));
                    ++stats_.dispatch_entries;
                });
            }
        }
    }

    out_.diags          = std::move(in_.diags);
    out_.binary_symbols = std::move(in_.binary_symbols);

    // B.6 Stage 1b: full bulk emit pass replaced with cache-only walker.
    // Functions, struct methods, and enum-impl methods are emit'd eagerly at
    // their push_back sites. The remaining items moved wholesale from in_ →
    // out_ (impl methods, const value exprs) carry mirror_ptr_ from sema
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

lir::LProgram mono_pass(lir::LProgram prog, MonoOpts opts) {
    Mono m(opts.max_instantiation_depth);
    m.set_entry_points(std::move(opts.entry_points));
    // M6.2: seed mono with previous iter's output so already-cloned
    // generic instances + passed-through non-generics are preserved and
    // not re-cloned this call.
    bool has_prev = !opts.prev_out.functions.empty() ||
                    !opts.prev_out.structs.empty() ||
                    !opts.prev_out.enums.empty();
    if (has_prev) m.set_prev_out(std::move(opts.prev_out));
    // M3 step 3: forward the stdlib template catalog (non-owning; caller
    // keeps it alive across this mono_pass call).
    m.set_stdlib_exports(opts.stdlib_exports);
    return m.run(std::move(prog), opts.max_instantiation_depth);
}

} // namespace logos::compiler
