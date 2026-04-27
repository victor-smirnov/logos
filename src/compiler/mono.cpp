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
            info.target_typevar = impl.target_type;
            info.assoc_types    = impl.assoc_types;
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
    for (auto& sd : in_.structs) {
        if (!sd.type_params.empty())
            struct_templates_[sd.name] = &sd;  // stable: in_.structs not moved
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
        for (auto& impl : out_.impls) {
            if (impl.is_blanket) continue;
            if (impl.trait_name != bi.bound_trait) continue;
            const std::string& concrete = impl.target_type;
            // For each method of the blanket, clone template with T→concrete
            // and emit under `concrete__method`.
            for (auto& tfn_up : in_.functions) {
                auto& tfn = *tfn_up;
                if (tfn.name.rfind(tmpl_prefix, 0) != 0) continue;
                std::string method = tfn.name.substr(tmpl_prefix.size());
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
                        concrete_t = out_.type_pool.alloc(std::move(st));
                        break;
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
    }

    // Process function work-list.
    while (!worklist_.empty()) {
        auto item = std::move(worklist_.back());
        worklist_.pop_back();
        // Set current depth for enqueue_if_needed during scan_fn
        depth_ = item.depth;
        auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
        out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(inst)));
        auto& fn_ref = *out_.functions.back();
        lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
        scan_fn(fn_ref);
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
                    for (auto& sm : sd.methods) if (sm->name == sym) { has = true; break; }
                    if (!has) {
                        for (auto& f : out_.functions) if (f->name == sym) { has = true; break; }
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
                }
            }
        }
    }

    out_.diags          = std::move(in_.diags);
    out_.binary_symbols = std::move(in_.binary_symbols);

    // Final fixup: emit mirror entries for items not produced by per-fn
    // clone+push_back paths above (consts, impl methods, struct method
    // bodies of non-instantiated structs, etc.). Already-emitted nodes
    // are deduplicated by the table caches.
    lir_mirror_emit_into(out_, *out_.mirror_table);

    return std::move(out_);
}

// ── Public entry point ─────────────────────────────────────────────────────

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth) noexcept(false) {
    Mono m(max_instantiation_depth);
    return m.run(std::move(prog), max_instantiation_depth);
}

} // namespace logos::compiler
