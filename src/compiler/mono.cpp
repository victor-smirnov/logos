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

#include <logos/compiler/mono.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

// ── Substitution map ──────────────────────────────────────────────────────

using SubstMap = std::unordered_map<std::string, const LogosType*>;

// ── Monomorphizer ─────────────────────────────────────────────────────────

namespace {

class Mono {
public:
    explicit Mono(int max_depth) : max_depth_(max_depth) {}

    lir::LProgram run(lir::LProgram&& in, int /*max_depth*/) {
        in_ = std::move(in);

        out_.consts       = std::move(in_.consts);
        out_.type_aliases = std::move(in_.type_aliases);
        out_.traits       = std::move(in_.traits);
        out_.impls        = std::move(in_.impls);
        // Move type_pool — will be extended with new types during mono
        out_.type_pool    = std::move(in_.type_pool);

        // Index generic struct templates; pass-through plain structs immediately.
        for (auto& sd : in_.structs) {
            if (!sd.type_params.empty())
                struct_templates_[sd.name] = &sd;  // stable: in_.structs not moved
        }
        // Move non-generic structs to output.
        for (auto& sd : in_.structs) {
            if (sd.type_params.empty())
                out_.structs.push_back(clone_struct_def(sd, {}, sd.name));
        }

        // Index generic class templates; pass-through concrete classes immediately.
        for (auto& cd : in_.classes) {
            if (!cd.type_params.empty())
                class_templates_[cd.name] = &cd;  // stable: in_.classes not moved
        }
        for (auto& cd : in_.classes) {
            if (cd.type_params.empty())
                out_.classes.push_back(clone_class_def(cd, {}, cd.name));
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
            auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst);
            scan_fn(inst);
            out_.functions.push_back(std::move(inst));
        }

        // Instantiate all generic structs referenced by the output.
        instantiate_struct_templates();

        // Instantiate all generic enums referenced by the output.
        instantiate_enum_templates();

        // Instantiate all generic classes referenced by the output.
        instantiate_class_templates();

        out_.diags = std::move(in_.diags);
        return std::move(out_);
    }

private:
    lir::LProgram  in_;
    lir::LProgram  out_;
    int            max_depth_;
    int            depth_ = 0;

    // name → pointer into in_.functions (generic templates only)
    std::unordered_map<std::string, const lir::LFunction*> templates_;

    // name → list of fn specialisations (pointers into in_.specializations)
    std::unordered_map<std::string, std::vector<const lir::LFunction*>> specs_;

    // name → pointer into in_.structs (generic struct templates)
    std::unordered_map<std::string, const lir::LStructDef*> struct_templates_;

    // name → list of struct specialisations (pointers into in_.struct_specializations)
    std::unordered_map<std::string, std::vector<const lir::LStructDef*>> struct_specs_;

    // concrete_name → LogosType* of needed generic struct instantiations
    std::unordered_map<std::string, const LogosType*> needed_struct_insts_;

    // Already-instantiated struct names (prevent duplicates)
    std::unordered_set<std::string> struct_done_;

    // name → pointer into in_.enums (generic enum templates)
    std::unordered_map<std::string, const lir::LEnumDef*> enum_templates_;

    // concrete_name → type_args for needed generic enum instantiations
    std::unordered_map<std::string, std::vector<const LogosType*>> needed_enum_insts_;
    std::unordered_set<std::string> enum_done_;

    // name → pointer into in_.classes (generic class templates)
    std::unordered_map<std::string, const lir::LClassDef*> class_templates_;

    // concrete_name → LogosType* of needed generic class instantiations
    std::unordered_map<std::string, const LogosType*> needed_class_insts_;

    // Already-instantiated class names (prevent duplicates)
    std::unordered_set<std::string> class_done_;

    // Already-instantiated mangled names (prevent duplicate generation)
    std::unordered_set<std::string> done_;

    struct WorkItem {
        std::string                mangled;
        const lir::LFunction*      tmpl;
        SubstMap                   subst;
    };
    std::vector<WorkItem> worklist_;

    // ── Type substitution ─────────────────────────────────────────

    const LogosType* subst_type(const LogosType* t, const SubstMap& s) noexcept {
        if (!t) return t;
        switch (t->kind) {
        case LogosType::Kind::TypeVar: {
            auto it = s.find(t->type_var_name);
            return (it != s.end()) ? it->second : t;
        }
        case LogosType::Kind::Ptr: {
            auto* inner = subst_type(t->pointee, s);
            if (inner == t->pointee) return t;
            LogosType nt = *t; nt.pointee = inner;
            return out_.type_pool.alloc(nt);
        }
        case LogosType::Kind::Array: {
            auto* elem = subst_type(t->elem, s);
            if (elem == t->elem) return t;
            LogosType nt = *t; nt.elem = elem;
            return out_.type_pool.alloc(nt);
        }
        case LogosType::Kind::Struct: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) return t;
            LogosType nt = *t;
            nt.type_args = std::move(new_args);
            // Track this instantiation for struct monomorphization.
            const LogosType* result = out_.type_pool.alloc(nt);
            record_needed_struct(result);
            return result;
        }
        case LogosType::Kind::Class: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) return t;
            LogosType nt = *t;
            nt.type_args = std::move(new_args);
            const LogosType* result = out_.type_pool.alloc(nt);
            record_needed_class(result);
            return result;
        }
        case LogosType::Kind::Enum: {
            if (t->type_args.empty()) return t;
            std::vector<const LogosType*> new_args;
            bool changed = false;
            for (auto* a : t->type_args) {
                auto* na = subst_type(a, s);
                changed |= (na != a);
                new_args.push_back(na);
            }
            if (!changed) {
                // Still record the need even if types didn't change
                // (e.g., non-generic function using Option<i32>).
                record_needed_enum(t);
                return t;
            }
            LogosType nt; nt.kind = LogosType::Kind::Enum;
            nt.enum_name = t->enum_name;
            nt.type_args = std::move(new_args);
            const LogosType* result = out_.type_pool.alloc(std::move(nt));
            record_needed_enum(result);
            return result;
        }
        case LogosType::Kind::Slice: {
            auto* elem = subst_type(t->elem, s);
            if (elem == t->elem) return t;
            LogosType nt; nt.kind = LogosType::Kind::Slice;
            nt.elem = elem;
            return out_.type_pool.alloc(std::move(nt));
        }
        case LogosType::Kind::Tuple: {
            std::vector<const LogosType*> new_elems;
            bool changed = false;
            for (auto* e : t->tuple_elems) {
                auto* ne = subst_type(e, s);
                changed |= (ne != e);
                new_elems.push_back(ne);
            }
            if (!changed) return t;
            LogosType nt; nt.kind = LogosType::Kind::Tuple;
            nt.tuple_elems = std::move(new_elems);
            return out_.type_pool.alloc(std::move(nt));
        }
        default:
            return t;
        }
    }

    // Register a generic struct instantiation as needed (all args must be concrete).
    void record_needed_struct(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Struct || t->type_args.empty()) return;
        // Check all args are concrete (no TypeVars).
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        auto cname = concrete_struct_name(t);
        if (!struct_done_.count(cname))
            needed_struct_insts_[cname] = t;
    }

    void record_needed_class(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Class || t->type_args.empty()) return;
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        auto cname = concrete_class_name(t);
        if (!class_done_.count(cname))
            needed_class_insts_[cname] = t;
    }

    void record_needed_enum(const LogosType* t) {
        if (!t || t->kind != LogosType::Kind::Enum || t->type_args.empty()) return;
        for (auto* a : t->type_args)
            if (a->kind == LogosType::Kind::TypeVar) return;
        // Build concrete enum name: Option__i32
        std::string cname = t->enum_name;
        for (auto* a : t->type_args) { cname += "__"; cname += mangle_type(a); }
        if (!enum_done_.count(cname))
            needed_enum_insts_[cname] = t->type_args;
    }

    // ── Mangling ──────────────────────────────────────────────────

    // type_str() may contain spaces (" *const i32") and brackets ("[i32; 4]"),
    // which are invalid in LLVM symbol names.  mangle_type() produces valid
    // C-identifier-style names: pcst_i32, pmut_i32, arr4_i32, etc.
    static std::string mangle_type(const LogosType* t) {
        if (!t) return "null";
        switch (t->kind) {
        case LogosType::Kind::Ptr:
            return (t->mut_ptr ? "pmut_" : "pcst_") + mangle_type(t->pointee);
        case LogosType::Kind::Array:
            return "arr" + std::to_string(t->arr_size) + "_" + mangle_type(t->elem);
        case LogosType::Kind::Struct:
            return concrete_struct_name(t);  // handles generic inst mangling
        default:
            return type_str(t);  // primitives / TypeVar / Enum — already valid identifiers
        }
    }

    static std::string mangle(const std::string& name,
                               const std::vector<const LogosType*>& type_args) {
        // e.g. identity__i32, describe__pcst_i32, swap__i32__bool
        std::string result = name;
        for (auto* t : type_args) {
            result += "__";
            result += mangle_type(t);
        }
        return result;
    }

    // ── Expression cloning / substitution ────────────────────────

    lir::LExprPtr subst_expr(const lir::LExpr& e, const SubstMap& s) {
        auto result = std::make_unique<lir::LExpr>();
        result->type = subst_type(e.type, s);

        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;

            if constexpr (std::is_same_v<K, lir::ELitInt> ||
                          std::is_same_v<K, lir::ELitBool> ||
                          std::is_same_v<K, lir::ELitStr>  ||
                          std::is_same_v<K, lir::EAddrOf>) {
                result->kind = k;

            } else if constexpr (std::is_same_v<K, lir::EEnumLit>) {
                lir::EEnumLit ne = k;
                if (result->type && result->type->kind == LogosType::Kind::Enum &&
                    !result->type->type_args.empty()) {
                    std::string cname = result->type->enum_name;
                    for (auto* a : result->type->type_args) { cname += "__"; cname += mangle_type(a); }
                    ne.enum_name = cname;
                    record_needed_enum(result->type);
                }
                result->kind = std::move(ne);

            } else if constexpr (std::is_same_v<K, lir::EVarRef>) {
                result->kind = k;

            } else if constexpr (std::is_same_v<K, lir::ECall>) {
                lir::ECall nc;
                nc.callee = k.callee;
                for (auto* ta : k.type_args)
                    nc.type_args.push_back(subst_type(ta, s));
                for (auto& a : k.args)
                    nc.args.push_back(subst_expr(*a, s));
                // Rewrite callee if it's a generic call that was already instantiated
                if (!nc.type_args.empty())
                    nc.callee = mangle(nc.callee, nc.type_args);
                result->kind = std::move(nc);

            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                // Check if this is a trait method call on a TypeVar that got resolved.
                // If so, rewrite to a direct ECall.
                auto* orig_recv_type = k.receiver->type;
                auto  new_recv = subst_expr(*k.receiver, s);
                if (orig_recv_type && orig_recv_type->kind == LogosType::Kind::TypeVar &&
                    new_recv->type && new_recv->type->kind == LogosType::Kind::Struct) {
                    // Trait method → direct call: StructName__method(self, args...)
                    auto cname = concrete_struct_name(new_recv->type);
                    lir::ECall nc;
                    nc.callee = cname + "__" + k.method;
                    nc.args.push_back(std::move(new_recv));
                    for (auto& a : k.args)
                        nc.args.push_back(subst_expr(*a, s));
                    result->kind = std::move(nc);
                } else {
                lir::EMethodCall nm;
                nm.receiver      = std::move(new_recv);
                nm.method        = k.method;
                nm.vtable_index  = k.vtable_index;
                // Translate resolved_type (generic class template name) to
                // concrete name using the current substitution.
                // e.g. resolved_type="Container", s={T:i32} → "Container__i32"
                if (!k.resolved_type.empty() && !s.empty()) {
                    auto tit = class_templates_.find(k.resolved_type);
                    if (tit != class_templates_.end()) {
                        const lir::LClassDef* rt_tmpl = tit->second;
                        // Build concrete type args by substituting the template's
                        // type params according to the current subst map.
                        // We use the receiver type to get the type args for this class.
                        // The receiver type after subst is nm.receiver->type.
                        // But we also need to know which type args rt_tmpl was instantiated with.
                        // Look at rt_tmpl's type_params and find matching entries in s.
                        std::vector<const LogosType*> concrete_args;
                        bool all_concrete = true;
                        for (auto& tp : rt_tmpl->type_params) {
                            auto sit2 = s.find(tp.name);
                            if (sit2 != s.end())
                                concrete_args.push_back(sit2->second);
                            else
                                all_concrete = false;
                        }
                        if (all_concrete && !concrete_args.empty()) {
                            LogosType parent_t;
                            parent_t.kind = LogosType::Kind::Class;
                            parent_t.struct_name = k.resolved_type;
                            parent_t.type_args = concrete_args;
                            nm.resolved_type = concrete_class_name(&parent_t);
                        } else if (rt_tmpl->type_params.empty()) {
                            nm.resolved_type = k.resolved_type;  // non-generic parent
                        } else {
                            nm.resolved_type = k.resolved_type;  // fallback
                        }
                    } else {
                        nm.resolved_type = k.resolved_type;
                    }
                } else {
                    nm.resolved_type = k.resolved_type;
                }
                for (auto& a : k.args)
                    nm.args.push_back(subst_expr(*a, s));
                result->kind = std::move(nm);
                } // end else (non-trait method call)

            } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
                result->kind = lir::EBinOp{k.op,
                    subst_expr(*k.lhs, s), subst_expr(*k.rhs, s)};

            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                result->kind = lir::EUnary{k.op, subst_expr(*k.operand, s)};

            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                result->kind = lir::EDeref{subst_expr(*k.operand, s)};

            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                result->kind = lir::EFieldRead{subst_expr(*k.receiver, s), k.field};

            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                result->kind = lir::EIndexRead{
                    subst_expr(*k.receiver, s), subst_expr(*k.index, s)};

            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                lir::EStructLit ns;
                // Update name to the concrete mangled struct name if generic.
                if (result->type && result->type->kind == LogosType::Kind::Struct &&
                    !result->type->type_args.empty())
                    ns.name = concrete_struct_name(result->type);
                else
                    ns.name = k.name;
                for (auto& [fn, fv] : k.fields)
                    ns.fields.push_back({fn, subst_expr(*fv, s)});
                // Record the instantiation as needed.
                record_needed_struct(result->type);
                result->kind = std::move(ns);

            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                lir::EArrLit na;
                for (auto& elem : k.elems)
                    na.elems.push_back(subst_expr(*elem, s));
                result->kind = std::move(na);

            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                result->kind = lir::ECast{subst_expr(*k.operand, s)};

            } else if constexpr (std::is_same_v<K, lir::ENew>) {
                lir::ENew nn;
                // If the result type is a generic class inst, use the concrete name.
                if (result->type && result->type->kind == LogosType::Kind::Ptr &&
                    result->type->pointee &&
                    result->type->pointee->kind == LogosType::Kind::Class &&
                    !result->type->pointee->type_args.empty()) {
                    nn.class_name = concrete_class_name(result->type->pointee);
                    record_needed_class(result->type->pointee);
                } else {
                    nn.class_name = k.class_name;
                }
                for (auto& [fn, fv] : k.fields)
                    nn.fields.push_back({fn, subst_expr(*fv, s)});
                result->kind = std::move(nn);

            } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
                lir::EIfExpr ni;
                ni.cond      = subst_expr(*k.cond, s);
                ni.then_val  = subst_expr(*k.then_val, s);
                ni.else_val  = subst_expr(*k.else_val, s);
                result->kind = std::move(ni);

            } else if constexpr (std::is_same_v<K, lir::ETupleLit>) {
                lir::ETupleLit nt;
                for (auto& elem : k.elems)
                    nt.elems.push_back(subst_expr(*elem, s));
                result->kind = std::move(nt);

            } else if constexpr (std::is_same_v<K, lir::ETupleIndex>) {
                result->kind = lir::ETupleIndex{subst_expr(*k.receiver, s), k.index};

            } else if constexpr (std::is_same_v<K, lir::ESliceLit>) {
                result->kind = lir::ESliceLit{subst_expr(*k.base, s), subst_expr(*k.len, s)};

            } else if constexpr (std::is_same_v<K, lir::ESliceIndex>) {
                result->kind = lir::ESliceIndex{subst_expr(*k.slice, s), subst_expr(*k.index, s)};

            } else if constexpr (std::is_same_v<K, lir::ESliceLen>) {
                result->kind = lir::ESliceLen{subst_expr(*k.slice, s)};

            } else if constexpr (std::is_same_v<K, lir::EEnumLitData>) {
                lir::EEnumLitData ne;
                // Use concrete enum name if type has type_args
                if (result->type && result->type->kind == LogosType::Kind::Enum &&
                    !result->type->type_args.empty()) {
                    std::string cname = result->type->enum_name;
                    for (auto* a : result->type->type_args) { cname += "__"; cname += mangle_type(a); }
                    ne.enum_name = cname;
                    record_needed_enum(result->type);
                } else {
                    ne.enum_name = k.enum_name;
                }
                ne.variant   = k.variant;
                ne.disc      = k.disc;
                for (auto& a : k.payload)
                    ne.payload.push_back(subst_expr(*a, s));
                result->kind = std::move(ne);
            }
        }, e.kind);

        return result;
    }

    lir::LBlock subst_block(const lir::LBlock& b, const SubstMap& s) {
        lir::LBlock nb;
        for (auto& st : b.stmts)
            nb.stmts.push_back(subst_stmt(st, s));
        return nb;
    }

    lir::LStmt subst_stmt(const lir::LStmt& st, const SubstMap& s) {
        lir::LStmt ns;
        ns.line = st.line;

        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;

            if constexpr (std::is_same_v<K, lir::SLet>) {
                lir::SLet nl;
                nl.name   = k.name;
                nl.type   = subst_type(k.type, s);
                nl.is_mut = k.is_mut;
                nl.value  = subst_expr(*k.value, s);
                ns.kind   = std::move(nl);

            } else if constexpr (std::is_same_v<K, lir::SAssign>) {
                ns.kind = lir::SAssign{k.name, subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SReturn>) {
                ns.kind = lir::SReturn{k.value ? subst_expr(*k.value, s) : nullptr};

            } else if constexpr (std::is_same_v<K, lir::SIf>) {
                lir::SIf ni;
                ni.cond  = subst_expr(*k.cond, s);
                ni.then_ = std::make_unique<lir::LBlock>(subst_block(*k.then_, s));
                if (k.else_)
                    ni.else_ = std::make_unique<lir::LBlock>(subst_block(**k.else_, s));
                ns.kind = std::move(ni);

            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                ns.kind = lir::SWhile{
                    subst_expr(*k.cond, s),
                    std::make_unique<lir::LBlock>(subst_block(*k.body, s))};

            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                lir::SFor nf;
                nf.var       = k.var;
                nf.lo        = subst_expr(*k.lo, s);
                nf.hi        = subst_expr(*k.hi, s);
                nf.inclusive = k.inclusive;
                nf.body      = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
                ns.kind      = std::move(nf);

            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                ns.kind = lir::SLoop{
                    std::make_unique<lir::LBlock>(subst_block(*k.body, s))};

            } else if constexpr (std::is_same_v<K, lir::SBreak> ||
                                 std::is_same_v<K, lir::SContinue>) {
                ns.kind = k;

            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                ns.kind = lir::SFieldWrite{k.receiver, k.field, subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                ns.kind = lir::SIndexWrite{
                    k.arr, subst_expr(*k.index, s), subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                ns.kind = lir::SExprStmt{subst_expr(*k.expr, s)};

            } else if constexpr (std::is_same_v<K, lir::SDelete>) {
                ns.kind = lir::SDelete{subst_expr(*k.expr, s)};

            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                lir::SMatch nm;
                nm.scrut = subst_expr(*k.scrut, s);
                for (auto& arm : k.arms) {
                    lir::LMatchArm na;
                    // Substitute types in PatVariantData bindings
                    if (auto* pvd = std::get_if<lir::PatVariantData>(&arm.pat)) {
                        lir::PatVariantData npvd = *pvd;
                        for (auto& bt : npvd.binding_types)
                            bt = subst_type(bt, s);
                        na.pat = std::move(npvd);
                    } else {
                        na.pat = arm.pat;
                    }
                    na.body = std::make_unique<lir::LBlock>(subst_block(*arm.body, s));
                    nm.arms.push_back(std::move(na));
                }
                ns.kind = std::move(nm);

            } else if constexpr (std::is_same_v<K, lir::SForEach>) {
                lir::SForEach nf;
                nf.var       = k.var;
                nf.iter      = subst_expr(*k.iter, s);
                nf.elem_type = subst_type(k.elem_type, s);
                nf.arr_size  = k.arr_size;
                nf.body      = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
                ns.kind      = std::move(nf);
            }
        }, st.kind);

        return ns;
    }

    // ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

    lir::LFunction clone_fn(const lir::LFunction& fn, const SubstMap& s) {
        lir::LFunction nf;
        nf.name      = fn.name;
        nf.is_extern = fn.is_extern;
        nf.is_vararg = fn.is_vararg;
        nf.ret_type  = subst_type(fn.ret_type, s);
        for (auto& p : fn.params)
            nf.params.push_back({p.name, subst_type(p.type, s)});
        nf.body = subst_block(fn.body, s);
        // type_params left empty: instantiated functions are monomorphic
        return nf;
    }

    // ── Scan a function for generic calls and enqueue them ────────

    void scan_fn(const lir::LFunction& fn) {
        scan_block(fn.body);
    }

    void scan_block(const lir::LBlock& b) {
        for (auto& st : b.stmts) scan_stmt(st);
    }

    void scan_stmt(const lir::LStmt& st) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::SLet>)
                scan_expr(*k.value);
            else if constexpr (std::is_same_v<K, lir::SAssign>)
                scan_expr(*k.value);
            else if constexpr (std::is_same_v<K, lir::SReturn>)
                { if (k.value) scan_expr(*k.value); }
            else if constexpr (std::is_same_v<K, lir::SIf>) {
                scan_expr(*k.cond);
                scan_block(*k.then_);
                if (k.else_) scan_block(**k.else_);
            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                scan_expr(*k.cond); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                scan_expr(*k.lo); scan_expr(*k.hi); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                scan_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                scan_expr(*k.index); scan_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                scan_expr(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SDelete>) {
                scan_expr(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                scan_expr(*k.scrut);
                for (auto& arm : k.arms) scan_block(*arm.body);
            } else if constexpr (std::is_same_v<K, lir::SForEach>) {
                scan_expr(*k.iter); scan_block(*k.body);
            }
        }, st.kind);
    }

    void scan_expr(const lir::LExpr& e) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::ECall>) {
                if (!k.type_args.empty()) {
                    // This is a post-substitution generic call: callee is already mangled.
                    // The name was rewritten by subst_expr. Try to find the original template.
                    // We identify originals by checking templates_ using the un-mangled name.
                    // In practice after subst_expr, callee == mangled name.
                    enqueue_if_needed(k.callee, k.type_args);
                }
                for (auto& a : k.args) scan_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
                scan_expr(*k.lhs); scan_expr(*k.rhs);
            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                scan_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                scan_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                scan_expr(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                scan_expr(*k.receiver); scan_expr(*k.index);
            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                scan_expr(*k.receiver);
                for (auto& a : k.args) scan_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                for (auto& [fn, fv] : k.fields) scan_expr(*fv);
            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                for (auto& elem : k.elems) scan_expr(*elem);
            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                scan_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::ENew>) {
                for (auto& [fn, fv] : k.fields) scan_expr(*fv);
            } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
                scan_expr(*k.cond); scan_expr(*k.then_val); scan_expr(*k.else_val);
            } else if constexpr (std::is_same_v<K, lir::ETupleLit>) {
                for (auto& elem : k.elems) scan_expr(*elem);
            } else if constexpr (std::is_same_v<K, lir::ETupleIndex>) {
                scan_expr(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::ESliceLit>) {
                scan_expr(*k.base); scan_expr(*k.len);
            } else if constexpr (std::is_same_v<K, lir::ESliceIndex>) {
                scan_expr(*k.slice); scan_expr(*k.index);
            } else if constexpr (std::is_same_v<K, lir::ESliceLen>) {
                scan_expr(*k.slice);
            } else if constexpr (std::is_same_v<K, lir::EEnumLitData>) {
                for (auto& a : k.payload) scan_expr(*a);
            }
        }, e.kind);
    }

    // ── Pattern matching (for specialisation selection) ───────────

    // Match a concrete type against a specialisation pattern (may contain TypeVars).
    // On success, fills `bindings` with TypeVar → concrete_type mappings.
    static bool match_type(const LogosType* concrete, const LogosType* pattern,
                           SubstMap& bindings) noexcept {
        if (!concrete || !pattern) return false;
        if (pattern->kind == LogosType::Kind::TypeVar) {
            auto it = bindings.find(pattern->type_var_name);
            if (it != bindings.end())
                return types_equal(*concrete, *it->second);  // must match prior binding
            bindings[pattern->type_var_name] = concrete;
            return true;
        }
        if (pattern->kind != concrete->kind) return false;
        switch (pattern->kind) {
        case LogosType::Kind::Ptr:
            return pattern->mut_ptr == concrete->mut_ptr &&
                   match_type(concrete->pointee, pattern->pointee, bindings);
        case LogosType::Kind::Array:
            return pattern->arr_size == concrete->arr_size &&
                   match_type(concrete->elem, pattern->elem, bindings);
        case LogosType::Kind::Struct:
            return pattern->struct_name == concrete->struct_name;
        default:
            return types_equal(*concrete, *pattern);
        }
    }

    // Higher score = more concrete = higher priority.
    static int type_specificity(const LogosType* t) noexcept {
        if (!t || t->kind == LogosType::Kind::TypeVar) return 0;
        if (t->kind == LogosType::Kind::Ptr)   return 1 + type_specificity(t->pointee);
        if (t->kind == LogosType::Kind::Array)  return 1 + type_specificity(t->elem);
        return 100;  // fully-concrete scalar / struct / enum
    }

    static int specificity_score(const std::vector<const LogosType*>& patterns) noexcept {
        int s = 0;
        for (auto* p : patterns) s += type_specificity(p);
        return s;
    }

    // Return the most specific specialisation that matches type_args, or nullptr.
    const lir::LFunction* find_best_spec(
        const std::string& base_name,
        const std::vector<const LogosType*>& type_args) {
        auto sit = specs_.find(base_name);
        if (sit == specs_.end()) return nullptr;

        const lir::LFunction* best       = nullptr;
        int                   best_score = -1;

        for (auto* spec : sit->second) {
            if (spec->spec_patterns.size() != type_args.size()) continue;
            SubstMap dummy;
            bool ok = true;
            for (size_t i = 0; i < type_args.size(); ++i) {
                if (!match_type(type_args[i], spec->spec_patterns[i], dummy)) {
                    ok = false; break;
                }
            }
            if (!ok) continue;
            int score = specificity_score(spec->spec_patterns);
            if (score > best_score) { best_score = score; best = spec; }
        }
        return best;
    }

    // ── Enqueue an instantiation if needed ───────────────────────

    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<const LogosType*>& type_args) {
        if (done_.count(mangled_callee)) return;

        // Find the base name by checking templates_ and specs_.
        std::string orig_name;
        for (auto& [tname, _] : templates_)
            if (mangle(tname, type_args) == mangled_callee) { orig_name = tname; break; }
        if (orig_name.empty())
            for (auto& [sname, _] : specs_)
                if (mangle(sname, type_args) == mangled_callee) { orig_name = sname; break; }
        if (orig_name.empty()) return;  // not a generic/spec call we know about

        if (depth_ >= max_depth_) {
            in_.diags.diags.push_back({Diag::Level::Error, "mono",
                std::format("instantiation depth limit ({}) exceeded for '{}'",
                            max_depth_, mangled_callee), {}, 0});
            return;
        }

        done_.insert(mangled_callee);

        // Prefer the most-specific matching specialisation over the generic template.
        if (auto* spec = find_best_spec(orig_name, type_args)) {
            SubstMap subst;
            for (size_t i = 0; i < spec->spec_patterns.size(); ++i)
                match_type(type_args[i], spec->spec_patterns[i], subst);
            worklist_.push_back({mangled_callee, spec, std::move(subst)});
            return;
        }

        // Generic template fallback.
        auto tit = templates_.find(orig_name);
        if (tit == templates_.end()) return;
        const lir::LFunction* tmpl = tit->second;

        SubstMap subst;
        size_t n = std::min(tmpl->type_params.size(), type_args.size());
        for (size_t i = 0; i < n; ++i)
            subst[tmpl->type_params[i].name] = type_args[i];
        worklist_.push_back({mangled_callee, tmpl, std::move(subst)});
    }

    // ── Instantiate a function template with a concrete SubstMap ─

    lir::LFunction instantiate_fn(const lir::LFunction& tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst);
        fn.name = mangled_name;
        --depth_;
        return fn;
    }

    // ── Struct monomorphization ───────────────────────────────────

    // Clone a struct def with substitution; rename to new_name.
    // Method names are rewritten from "Base__method" to "new_name__method".
    lir::LStructDef clone_struct_def(const lir::LStructDef& tmpl,
                                      const SubstMap& s,
                                      const std::string& new_name) {
        lir::LStructDef nd;
        nd.name = new_name;
        // type_params cleared: result is monomorphic
        for (auto& f : tmpl.fields)
            nd.fields.push_back({f.name, subst_type(f.type, s)});
        for (auto& m : tmpl.methods) {
            auto nm = clone_fn(m, s);
            // Rename method: "OldBase__methodName" → "new_name__methodName"
            // Method names are stored as "StructName__methodName".
            auto sep = m.name.find("__");
            if (sep != std::string::npos)
                nm.name = new_name + m.name.substr(sep);
            // Substitute struct type in params/ret as needed (already done by clone_fn).
            nd.methods.push_back(std::move(nm));
        }
        return nd;
    }

    // Return the best-matching struct specialisation for (base_name, type_args).
    const lir::LStructDef* find_best_struct_spec(
        const std::string& base_name,
        const std::vector<const LogosType*>& type_args) {
        auto sit = struct_specs_.find(base_name);
        if (sit == struct_specs_.end()) return nullptr;

        const lir::LStructDef* best       = nullptr;
        int                    best_score = -1;

        for (auto* spec : sit->second) {
            if (spec->spec_patterns.size() != type_args.size()) continue;
            SubstMap dummy;
            bool ok = true;
            for (size_t i = 0; i < type_args.size(); ++i) {
                if (!match_type(type_args[i], spec->spec_patterns[i], dummy)) {
                    ok = false; break;
                }
            }
            if (!ok) continue;
            int score = specificity_score(spec->spec_patterns);
            if (score > best_score) { best_score = score; best = spec; }
        }
        return best;
    }

    // Walk a type and record any concrete generic struct instantiations needed.
    void collect_type_for_structs(const LogosType* t) {
        if (!t) return;
        switch (t->kind) {
        case LogosType::Kind::Ptr:   collect_type_for_structs(t->pointee); break;
        case LogosType::Kind::Array: collect_type_for_structs(t->elem);    break;
        case LogosType::Kind::Struct:
            record_needed_struct(t);
            for (auto* a : t->type_args) collect_type_for_structs(a);
            break;
        default: break;
        }
    }

    // Walk all output functions collecting generic struct instantiations needed.
    void collect_struct_needs_from_output() {
        for (auto& fn : out_.functions) {
            collect_type_for_structs(fn.ret_type);
            for (auto& p : fn.params) collect_type_for_structs(p.type);
            collect_struct_needs_from_block(fn.body);
        }
        // Also walk already-instantiated structs (field types may reference more).
        for (auto& sd : out_.structs)
            for (auto& f : sd.fields) collect_type_for_structs(f.type);
    }

    void collect_struct_needs_from_block(const lir::LBlock& b) {
        for (auto& st : b.stmts) collect_struct_needs_from_stmt(st);
    }

    void collect_struct_needs_from_stmt(const lir::LStmt& st) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::SLet>)
                { collect_type_for_structs(k.type); collect_struct_needs_from_expr(*k.value); }
            else if constexpr (std::is_same_v<K, lir::SAssign>)
                collect_struct_needs_from_expr(*k.value);
            else if constexpr (std::is_same_v<K, lir::SReturn>)
                { if (k.value) collect_struct_needs_from_expr(*k.value); }
            else if constexpr (std::is_same_v<K, lir::SIf>) {
                collect_struct_needs_from_expr(*k.cond);
                collect_struct_needs_from_block(*k.then_);
                if (k.else_) collect_struct_needs_from_block(**k.else_);
            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                collect_struct_needs_from_expr(*k.cond);
                collect_struct_needs_from_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                collect_struct_needs_from_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                collect_struct_needs_from_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                collect_struct_needs_from_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                collect_struct_needs_from_expr(*k.index);
                collect_struct_needs_from_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                collect_struct_needs_from_expr(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SDelete>) {
                collect_struct_needs_from_expr(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                collect_struct_needs_from_expr(*k.scrut);
                for (auto& arm : k.arms) collect_struct_needs_from_block(*arm.body);
            }
        }, st.kind);
    }

    void collect_struct_needs_from_expr(const lir::LExpr& e) {
        collect_type_for_structs(e.type);
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::ECall>) {
                for (auto& a : k.args) collect_struct_needs_from_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                collect_struct_needs_from_expr(*k.receiver);
                for (auto& a : k.args) collect_struct_needs_from_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
                collect_struct_needs_from_expr(*k.lhs);
                collect_struct_needs_from_expr(*k.rhs);
            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                collect_struct_needs_from_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                collect_struct_needs_from_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                collect_struct_needs_from_expr(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                collect_struct_needs_from_expr(*k.receiver);
                collect_struct_needs_from_expr(*k.index);
            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                for (auto& [fn, fv] : k.fields) collect_struct_needs_from_expr(*fv);
            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                for (auto& elem : k.elems) collect_struct_needs_from_expr(*elem);
            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                collect_struct_needs_from_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::ENew>) {
                for (auto& [fn, fv] : k.fields) collect_struct_needs_from_expr(*fv);
            }
        }, e.kind);
    }

    // Process all pending struct instantiations (may discover more via field types).
    void instantiate_struct_templates() {
        collect_struct_needs_from_output();

        while (!needed_struct_insts_.empty()) {
            // Take a copy of current needs (instantiating may add more).
            auto current = std::move(needed_struct_insts_);
            needed_struct_insts_.clear();

            for (auto& [cname, struct_t] : current) {
                if (struct_done_.count(cname)) continue;
                struct_done_.insert(cname);

                const std::string& base = struct_t->struct_name;
                SubstMap subst;

                const lir::LStructDef* tmpl = nullptr;
                if (auto* spec = find_best_struct_spec(base, struct_t->type_args)) {
                    for (size_t i = 0; i < spec->spec_patterns.size() &&
                                       i < struct_t->type_args.size(); ++i)
                        match_type(struct_t->type_args[i], spec->spec_patterns[i], subst);
                    tmpl = spec;
                } else {
                    auto it = struct_templates_.find(base);
                    if (it == struct_templates_.end()) continue;
                    tmpl = it->second;
                    for (size_t i = 0; i < tmpl->type_params.size() &&
                                       i < struct_t->type_args.size(); ++i)
                        subst[tmpl->type_params[i].name] = struct_t->type_args[i];
                }

                auto inst = clone_struct_def(*tmpl, subst, cname);
                // Collect field types of new struct for further instantiation.
                for (auto& f : inst.fields) collect_type_for_structs(f.type);
                out_.structs.push_back(std::move(inst));
            }
        }
    }

    // ── Class monomorphization ────────────────────────────────────

    // Clone a class def with substitution; rename to new_name.
    // Mirrors clone_struct_def but preserves vtable_order, parent_name, etc.
    lir::LClassDef clone_class_def(const lir::LClassDef& tmpl,
                                    const SubstMap& s,
                                    const std::string& new_name) {
        lir::LClassDef nd;
        nd.name         = new_name;
        nd.is_abstract  = tmpl.is_abstract;
        // Compute concrete parent name by applying subst to parent_type_args
        if (!tmpl.parent_name.empty() && !tmpl.parent_type_args.empty()) {
            std::vector<const LogosType*> concrete_parent_args;
            for (auto* arg : tmpl.parent_type_args)
                concrete_parent_args.push_back(subst_type(arg, s));
            LogosType parent_cls;
            parent_cls.kind = LogosType::Kind::Class;
            parent_cls.struct_name = tmpl.parent_name;
            parent_cls.type_args = concrete_parent_args;
            nd.parent_name = concrete_class_name(&parent_cls);
            // Trigger instantiation of parent
            const LogosType* parent_t = out_.type_pool.alloc(parent_cls);
            record_needed_class(parent_t);
        } else {
            nd.parent_name = tmpl.parent_name;
        }
        // Rewrite vtable entries: "OldBase__method" → "new_name__method"
        for (auto& entry : tmpl.vtable_order) {
            auto sep = entry.find("__");
            if (sep != std::string::npos && entry.substr(0, sep) == tmpl.name)
                nd.vtable_order.push_back(new_name + entry.substr(sep));
            else
                nd.vtable_order.push_back(entry);
        }
        // type_params cleared: result is monomorphic.
        for (auto& f : tmpl.own_fields)
            nd.own_fields.push_back({f.name, subst_type(f.type, s)});
        for (auto& m : tmpl.methods) {
            auto nm = clone_fn(m, s);
            auto sep = m.name.find("__");
            if (sep != std::string::npos)
                nm.name = new_name + m.name.substr(sep);
            nd.methods.push_back(std::move(nm));
        }
        return nd;
    }

    void collect_type_for_classes(const LogosType* t) {
        if (!t) return;
        switch (t->kind) {
        case LogosType::Kind::Ptr:   collect_type_for_classes(t->pointee); break;
        case LogosType::Kind::Array: collect_type_for_classes(t->elem);    break;
        case LogosType::Kind::Class:
            record_needed_class(t);
            for (auto* a : t->type_args) collect_type_for_classes(a);
            break;
        default: break;
        }
    }

    // Instantiate a single generic class by concrete name + type.
    // Ensures the parent class is instantiated first (DFS order = parent before child).
    void instantiate_one_class(const std::string& cname, const LogosType* class_t) {
        if (class_done_.count(cname)) return;
        class_done_.insert(cname);

        const std::string& base = class_t->struct_name;
        auto it = class_templates_.find(base);
        if (it == class_templates_.end()) return;
        const lir::LClassDef* tmpl = it->second;

        SubstMap subst;
        for (size_t i = 0; i < tmpl->type_params.size() &&
                           i < class_t->type_args.size(); ++i)
            subst[tmpl->type_params[i].name] = class_t->type_args[i];

        // Compute concrete parent name and instantiate parent FIRST (DFS).
        if (!tmpl->parent_name.empty() && !tmpl->parent_type_args.empty()) {
            std::vector<const LogosType*> concrete_parent_args;
            for (auto* arg : tmpl->parent_type_args)
                concrete_parent_args.push_back(subst_type(arg, subst));
            LogosType parent_cls;
            parent_cls.kind = LogosType::Kind::Class;
            parent_cls.struct_name = tmpl->parent_name;
            parent_cls.type_args = concrete_parent_args;
            std::string parent_cname = concrete_class_name(&parent_cls);
            if (!class_done_.count(parent_cname)) {
                const LogosType* parent_t = out_.type_pool.alloc(parent_cls);
                instantiate_one_class(parent_cname, parent_t);
            }
        }

        auto inst = clone_class_def(*tmpl, subst, cname);
        for (auto& f : inst.own_fields) collect_type_for_classes(f.type);
        out_.classes.push_back(std::move(inst));
    }

    void instantiate_enum_templates() {
        // Instantiate generic enums that were recorded during function cloning.
        // Simple approach: iterate until no more needed (fixed-point).
        while (true) {
            std::vector<std::pair<std::string, std::vector<const LogosType*>>> todo;
            for (auto& [cname, args] : needed_enum_insts_) {
                if (enum_done_.count(cname)) continue;
                todo.push_back({cname, args});
            }
            if (todo.empty()) break;
            for (auto& [cname, args] : todo) {
                enum_done_.insert(cname);
                // Find the template
                // Extract base name from cname (before first __)
                std::string base = cname;
                auto pos = base.find("__");
                if (pos != std::string::npos) base = base.substr(0, pos);
                auto tit = enum_templates_.find(base);
                if (tit == enum_templates_.end()) continue;
                auto* tmpl = tit->second;
                // Build substitution map
                SubstMap subst;
                for (size_t i = 0; i < tmpl->type_params.size() && i < args.size(); ++i)
                    subst[tmpl->type_params[i].name] = args[i];
                // Instantiate: substitute payload types
                lir::LEnumDef inst;
                inst.name = cname;
                for (auto& v : tmpl->variants) {
                    lir::LVariant nv;
                    nv.name = v.name;
                    nv.disc = v.disc;
                    for (auto* pt : v.payload_types)
                        nv.payload_types.push_back(subst_type(pt, subst));
                    inst.variants.push_back(std::move(nv));
                }
                out_.enums.push_back(std::move(inst));
            }
        }
    }

    void instantiate_class_templates() {
        // Seed: collect class types referenced in output functions and classes.
        for (auto& fn : out_.functions) {
            collect_type_for_classes(fn.ret_type);
            for (auto& p : fn.params) collect_type_for_classes(p.type);
        }
        for (auto& cd : out_.classes)
            for (auto& f : cd.own_fields) collect_type_for_classes(f.type);

        while (!needed_class_insts_.empty()) {
            auto current = std::move(needed_class_insts_);
            needed_class_insts_.clear();

            for (auto& [cname, class_t] : current)
                instantiate_one_class(cname, class_t);
        }
    }
};

} // anonymous namespace

// ── Public entry point ─────────────────────────────────────────────────────

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth) noexcept(false) {
    Mono m(max_instantiation_depth);
    return m.run(std::move(prog), max_instantiation_depth);
}

} // namespace logos::compiler
