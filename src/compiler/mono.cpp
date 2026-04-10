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

namespace logos::compiler {

lir::LProgram Mono::run(lir::LProgram&& in, int /*max_depth*/) {
    in_ = std::move(in);

    out_.consts       = std::move(in_.consts);
    out_.type_aliases = std::move(in_.type_aliases);
    out_.traits       = std::move(in_.traits);
    out_.impls        = std::move(in_.impls);
    // Move type_pool — will be extended with new types during mono
    out_.type_pool    = std::move(in_.type_pool);

    // Index associated type impls for subst_type resolution
    for (auto& impl : out_.impls) {
        for (auto& [aname, atype] : impl.assoc_types) {
            std::string key = impl.trait_name + "::" + impl.target_type + "::" + aname;
            assoc_impls_[key] = atype;
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

    // Index generic class templates; pass-through concrete classes immediately.
    for (auto& cd : in_.classes) {
        if (!cd.type_params.empty())
            class_templates_[cd.name] = &cd;  // stable: in_.classes not moved
    }
    for (auto& cd : in_.classes) {
        if (cd.type_params.empty())
            out_.classes.push_back(clone_class_def(cd, {}, {}, cd.name));
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
        if (!fn.type_params.empty())
            templates_[fn.name] = &fn;
    }

    // Index fn specialisations.
    for (auto& spec : in_.specializations)
        specs_[spec.name].push_back(&spec);

    // Index struct specialisations.
    for (auto& ss : in_.struct_specializations)
        struct_specs_[ss.name].push_back(&ss);

    // Process non-generic free functions (also scans class method bodies for
    // concrete class methods needed via scan_fn on each non-generic class).
    for (auto& fn : in_.functions) {
        if (!fn.type_params.empty()) continue;
        auto cloned = clone_fn(fn, {});
        scan_fn(cloned);
        out_.functions.push_back(std::move(cloned));
    }

    // Process function work-list.
    while (!worklist_.empty()) {
        auto item = std::move(worklist_.back());
        worklist_.pop_back();
        // Set current depth for enqueue_if_needed during scan_fn
        depth_ = item.depth;
        auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
        scan_fn(inst);
        out_.functions.push_back(std::move(inst));
    }
    depth_ = 0;

    // Instantiate all generic structs referenced by the output.
    instantiate_struct_templates();

    // Instantiate all generic enums referenced by the output.
    instantiate_enum_templates();

    // Instantiate all generic classes referenced by the output.
    instantiate_class_templates();

    out_.diags = std::move(in_.diags);
    return std::move(out_);
}

// ── Public entry point ─────────────────────────────────────────────────────

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth) noexcept(false) {
    Mono m(max_instantiation_depth);
    return m.run(std::move(prog), max_instantiation_depth);
}

} // namespace logos::compiler
