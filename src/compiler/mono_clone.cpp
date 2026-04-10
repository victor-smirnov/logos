// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_clone.cpp — Expression/statement substitution and function/type cloning.

#include "mono_impl.hpp"

namespace logos::compiler {

lir::LExprPtr Mono::subst_expr(const lir::LExpr& e, const SubstMap& s,
                          const PackMap& /*unused*/) {
    // packs are stored in cur_packs_ (set by clone_fn)
    auto result = std::make_unique<lir::LExpr>();
    result->type = subst_type(e.type, s);

    std::visit([&](const auto& k) {
        using K = std::decay_t<decltype(k)>;

        if constexpr (std::is_same_v<K, lir::ELitInt>   ||
                      std::is_same_v<K, lir::ELitFloat> ||
                      std::is_same_v<K, lir::ELitBool>  ||
                      std::is_same_v<K, lir::ELitStr>   ||
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
            for (auto& a : k.args) {
                // Pack expansion: args... → expand into individual args
                if (auto* pe = std::get_if<lir::EPackExpand>(&a->kind)) {
                    // Find which type pack this var belongs to.
                    // The var's type is a TypeVar whose name is in cur_packs_.
                    std::string pack_name;
                    if (a->type && a->type->kind == LogosType::Kind::TypeVar)
                        pack_name = a->type->type_var_name;
                    auto pit = cur_packs_.find(pack_name);
                    if (pit != cur_packs_.end()) {
                        for (size_t pi = 0; pi < pit->second.size(); ++pi) {
                            auto ref = std::make_unique<lir::LExpr>();
                            ref->type = pit->second[pi];
                            ref->kind = lir::EVarRef{make_pack_arg_name(pe->var_name, pi)};
                            nc.args.push_back(std::move(ref));
                        }
                        // If callee is a template, add pack types as type_args
                        // so mono enqueues the recursive instantiation.
                        if (templates_.count(nc.callee)) {
                            for (auto* pt : pit->second)
                                nc.type_args.push_back(pt);
                        }
                    }
                } else {
                    nc.args.push_back(subst_expr(*a, s));
                }
            }
            // Rewrite callee if it's a generic call that was already instantiated
            if (!nc.type_args.empty()) {
                // Check if callee is a static method on a generic struct template.
                // If so, use the concrete-struct naming scheme (e.g. "Inner$G1$i32__new")
                // instead of the free-fn scheme (e.g. "Inner__new__i32").
                // Detect by: callee = "StructName__method", struct_templates_ has "StructName"
                // and all type_args correspond to the struct's type params (no TypeVar left).
                bool rewritten_as_struct_method = false;
                {
                    auto sep = nc.callee.find("__");
                    if (sep != std::string::npos) {
                        std::string struct_part = nc.callee.substr(0, sep);
                        std::string method_part = nc.callee.substr(sep); // includes "__"
                        auto sit = struct_templates_.find(struct_part);
                        if (sit != struct_templates_.end()) {
                            // Verify none of the type args are still TypeVar
                            bool all_concrete = true;
                            for (auto* ta : nc.type_args)
                                if (ta && ta->kind == LogosType::Kind::TypeVar)
                                    { all_concrete = false; break; }
                            if (all_concrete) {
                                // Build the concrete struct type.
                                LogosType st;
                                st.kind = LogosType::Kind::Struct;
                                st.struct_name = struct_part;
                                size_t n_impl_tp = sit->second->type_params.size();
                                for (size_t i = 0; i < n_impl_tp && i < nc.type_args.size(); ++i)
                                    st.type_args.push_back(nc.type_args[i]);
                                std::string cname = concrete_struct_name(&st);
                                nc.callee = cname + method_part;
                                nc.type_args.clear();  // call is now concrete
                                rewritten_as_struct_method = true;
                            }
                        }
                    }
                }
                if (!rewritten_as_struct_method)
                    nc.callee = mangle(nc.callee, nc.type_args);
            }
            result->kind = std::move(nc);

        } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
            // Check if this is a trait method call on a TypeVar that got resolved.
            // If so, rewrite to a direct ECall.
            auto* orig_recv_type = k.receiver->type;
            auto  new_recv = subst_expr(*k.receiver, s);
            // Unwrap pointer for TypeVar check (handles *mut T → *mut Class)
            auto* orig_inner = orig_recv_type;
            if (orig_inner && orig_inner->kind == LogosType::Kind::Ptr && orig_inner->pointee)
                orig_inner = orig_inner->pointee;
            if (orig_inner && orig_inner->kind == LogosType::Kind::TypeVar &&
                new_recv->type) {
                // Trait method → direct call: TypeName__method(self, args...)
                std::string cname;
                auto* rt = new_recv->type;
                if (rt->kind == LogosType::Kind::Struct)
                    cname = concrete_struct_name(rt);
                else if (rt->kind == LogosType::Kind::Class)
                    cname = concrete_class_name(rt);
                else if (rt->kind == LogosType::Kind::Ptr && rt->pointee) {
                    if (rt->pointee->kind == LogosType::Kind::Class)
                        cname = concrete_class_name(rt->pointee);
                    else if (rt->pointee->kind == LogosType::Kind::Struct)
                        cname = concrete_struct_name(rt->pointee);
                    else
                        cname = type_str(rt);  // full ptr type: *const u8, *mut i32
                }
                // Fallback: primitive types (i32, bool, etc.)
                if (cname.empty())
                    cname = type_str(rt);
                if (!cname.empty()) {
                    lir::ECall nc;
                    nc.callee = cname + "__" + k.method;
                    nc.args.push_back(std::move(new_recv));
                    for (auto& a : k.args) nc.args.push_back(subst_expr(*a, s));
                    result->kind = std::move(nc);
                } else {
                    lir::EMethodCall nm;
                    nm.receiver = std::move(new_recv);
                    nm.method = k.method;
                    nm.vtable_index = k.vtable_index;
                    nm.resolved_type = k.resolved_type;
                    for (auto& a : k.args) nm.args.push_back(subst_expr(*a, s));
                    result->kind = std::move(nm);
                }
            } else {
                lir::EMethodCall nm;
                nm.receiver = std::move(new_recv);
                nm.method = k.method;
                for (auto* ta : k.type_args) nm.type_args.push_back(subst_type(ta, s));
                nm.vtable_index = k.vtable_index;

                // SPECIALIZATION LOOKUP (Bug 12)
                bool rewritten = false;
                if (nm.receiver->type) {
                    const LogosType* rt = nm.receiver->type;
                    while (rt && (rt->kind == LogosType::Kind::Ptr ||
                                  rt->kind == LogosType::Kind::Ref ||
                                  rt->kind == LogosType::Kind::MutRef) && rt->pointee) {
                        rt = rt->pointee;
                    }

                    if (rt && (rt->kind == LogosType::Kind::Struct || rt->kind == LogosType::Kind::Class)) {
                        std::vector<const LogosType*> combined_args = rt->type_args;
                        for (auto* mta : nm.type_args) combined_args.push_back(mta);

                        std::string base_struct = nm.resolved_type.empty()
                            ? rt->struct_name : nm.resolved_type;
                        std::string base_name = base_struct + "__" + nm.method;

                        if (auto* spec = find_best_spec(base_name, combined_args)) {
                            // Specialized method found! Rewrite to ECall.
                            lir::ECall nc;
                            nc.callee = spec->name;
                            nc.args.push_back(std::move(nm.receiver));
                            for (auto& arg : k.args)
                                nc.args.push_back(subst_expr(*arg, s));
                            result->kind = std::move(nc);
                            rewritten = true;
                        } else if (!nm.type_args.empty() &&
                                   (templates_.count(base_name) || specs_.count(base_name))) {
                            // Generic direct method call on a concrete receiver:
                            // rewrite to a monomorphized free-function call so it
                            // participates in the normal generic-instantiation flow.
                            lir::ECall nc;
                            nc.callee = mangle(base_name, combined_args);
                            nc.type_args = combined_args;
                            nc.args.push_back(std::move(nm.receiver));
                            for (auto& arg : k.args)
                                nc.args.push_back(subst_expr(*arg, s));
                            result->kind = std::move(nc);
                            rewritten = true;
                        }
                    }
                }

                if (!rewritten) {
                    // Translate resolved_type (generic class template name) to
                    // concrete name using the current substitution.
                    if (!k.resolved_type.empty() && !s.empty()) {
                        auto tit = class_templates_.find(k.resolved_type);
                        if (tit != class_templates_.end()) {
                            const lir::LClassDef* rt_tmpl = tit->second;
                            std::vector<const LogosType*> concrete_args;
                            bool all_concrete = true;
                            for (auto& tp : rt_tmpl->type_params) {
                                auto sit2 = s.find(tp.name);
                                if (sit2 != s.end()) concrete_args.push_back(sit2->second);
                                else all_concrete = false;
                            }
                            if (all_concrete && !concrete_args.empty()) {
                                LogosType parent_t;
                                parent_t.kind = LogosType::Kind::Class;
                                parent_t.struct_name = k.resolved_type;
                                parent_t.type_args = concrete_args;
                                nm.resolved_type = concrete_class_name(&parent_t);
                            } else {
                                nm.resolved_type = k.resolved_type;
                            }
                        } else {
                            nm.resolved_type = k.resolved_type;
                        }
                    } else {
                        nm.resolved_type = k.resolved_type;
                    }
                    for (auto& a : k.args) nm.args.push_back(subst_expr(*a, s));
                    result->kind = std::move(nm);
                }
            } // end else (non-trait method call)

        } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
            auto new_lhs = subst_expr(*k.lhs, s);
            auto new_rhs = subst_expr(*k.rhs, s);
            // After substitution, if LHS is a struct/class, rewrite binop
            // to operator trait method call (e.g. v + v → Vec2__add(v, v)).
            auto* lt = new_lhs->type;
            if (lt && (lt->kind == LogosType::Kind::Struct ||
                       lt->kind == LogosType::Kind::Class)) {
                std::string method_name;
                if      (k.op == "+")  method_name = "add";
                else if (k.op == "-")  method_name = "sub";
                else if (k.op == "*")  method_name = "mul";
                else if (k.op == "/")  method_name = "div";
                else if (k.op == "%")  method_name = "rem";
                else if (k.op == "==") method_name = "eq";
                else if (k.op == "!=") method_name = "ne";
                else if (k.op == "<")  method_name = "lt";
                else if (k.op == "<=") method_name = "le";
                else if (k.op == ">")  method_name = "gt";
                else if (k.op == ">=") method_name = "ge";
                if (!method_name.empty()) {
                    std::string cname = (lt->kind == LogosType::Kind::Struct)
                        ? concrete_struct_name(lt) : concrete_class_name(lt);
                    lir::ECall nc;
                    nc.callee = cname + "__" + method_name;
                    nc.args.push_back(std::move(new_lhs));
                    nc.args.push_back(std::move(new_rhs));
                    result->kind = std::move(nc);
                } else {
                    result->kind = lir::EBinOp{k.op,
                        std::move(new_lhs), std::move(new_rhs)};
                }
            } else {
                result->kind = lir::EBinOp{k.op,
                    std::move(new_lhs), std::move(new_rhs)};
            }

        } else if constexpr (std::is_same_v<K, lir::EUnary>) {
            auto new_op = subst_expr(*k.operand, s);
            auto* vt = new_op->type;
            if (vt && (vt->kind == LogosType::Kind::Struct ||
                       vt->kind == LogosType::Kind::Class)) {
                std::string method_name;
                if      (k.op == "-") method_name = "neg";
                else if (k.op == "!") method_name = "not_";
                if (!method_name.empty()) {
                    std::string cname = (vt->kind == LogosType::Kind::Struct)
                        ? concrete_struct_name(vt) : concrete_class_name(vt);
                    lir::ECall nc;
                    nc.callee = cname + "__" + method_name;
                    nc.args.push_back(std::move(new_op));
                    result->kind = std::move(nc);
                } else {
                    result->kind = lir::EUnary{k.op, std::move(new_op)};
                }
            } else {
                result->kind = lir::EUnary{k.op, std::move(new_op)};
            }

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

        } else if constexpr (std::is_same_v<K, lir::EClosureBox>) {
            if (k.inner) {
                auto nc = std::make_unique<lir::EClosure>();
                nc->closure_id = k.inner->closure_id;
                for (auto& p : k.inner->params)
                    nc->params.push_back({p.name, subst_type(p.type, s)});
                nc->ret_type = subst_type(k.inner->ret_type, s);
                nc->body = subst_block(k.inner->body, s);
                nc->is_move = k.inner->is_move;
                nc->captures = k.inner->captures;
                for (auto* ct : k.inner->capture_types)
                    nc->capture_types.push_back(subst_type(ct, s));
                result->kind = lir::EClosureBox{std::move(nc)};
            } else {
                result->kind = lir::EClosureBox{nullptr};
            }

        } else if constexpr (std::is_same_v<K, lir::EClosureCall>) {
            lir::EClosureCall nc;
            nc.callee = subst_expr(*k.callee, s);
            for (auto& a : k.args)
                nc.args.push_back(subst_expr(*a, s));
            result->kind = std::move(nc);

        } else if constexpr (std::is_same_v<K, lir::EFnPtrCall>) {
            lir::EFnPtrCall nc;
            nc.callee = subst_expr(*k.callee, s);
            for (auto& a : k.args)
                nc.args.push_back(subst_expr(*a, s));
            result->kind = std::move(nc);

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
        } else if constexpr (std::is_same_v<K, lir::EFormatCall>) {
            lir::EFormatCall nf;
            nf.fmt = subst_expr(*k.fmt, s);
            nf.arg_types = k.arg_types;
            for (auto& a : k.args)
                nf.args.push_back(subst_expr(*a, s));
            result->kind = std::move(nf);
        } else if constexpr (std::is_same_v<K, lir::EPackExpand>) {
            // Pack expansion is handled specially in the ECall case.
            // If we reach here, it's a standalone expansion — just clone it.
            result->kind = lir::EPackExpand{k.var_name};
        } else if constexpr (std::is_same_v<K, lir::ETry>) {
            lir::ETry nt;
            nt.inner    = subst_expr(*k.inner, s);
            nt.ok_disc  = k.ok_disc;
            nt.err_disc = k.err_disc;
            result->kind = std::move(nt);
        } else if constexpr (std::is_same_v<K, lir::EMatchExpr>) {
            lir::EMatchExpr nm;
            nm.scrut = subst_expr(*k.scrut, s);
            for (auto& arm : k.arms) {
                lir::EMatchArm na;
                na.pat = arm.pat;
                if (arm.guard) na.guard = subst_expr(**arm.guard, s);
                na.value = subst_expr(*arm.value, s);
                nm.arms.push_back(std::move(na));
            }
            result->kind = std::move(nm);
        } else if constexpr (std::is_same_v<K, lir::ESizeOf>) {
            result->kind = lir::ESizeOf{subst_type(k.elem_type, s)};
        } else if constexpr (std::is_same_v<K, lir::EBlockExpr>) {
            lir::EBlockExpr nb;
            if (k.block) nb.block = std::make_unique<lir::LBlock>(subst_block(*k.block, s));
            if (k.result) nb.result = subst_expr(*k.result, s);
            result->kind = std::move(nb);
        } else if constexpr (std::is_same_v<K, lir::EAddrOfTemp>) {
            result->kind = lir::EAddrOfTemp{subst_expr(*k.inner, s), k.is_mut};
        }
    }, e.kind);

    return result;
}


lir::LStmt Mono::subst_stmt(const lir::LStmt& st, const SubstMap& s) {
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
            lir::SWhile nw;
            nw.cond  = subst_expr(*k.cond, s);
            nw.body  = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
            nw.label = k.label;
            ns.kind  = std::move(nw);

        } else if constexpr (std::is_same_v<K, lir::SFor>) {
            lir::SFor nf;
            nf.var       = k.var;
            nf.lo        = subst_expr(*k.lo, s);
            nf.hi        = subst_expr(*k.hi, s);
            nf.inclusive = k.inclusive;
            nf.body      = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
            nf.label     = k.label;
            ns.kind      = std::move(nf);

        } else if constexpr (std::is_same_v<K, lir::SLoop>) {
            lir::SLoop nl;
            nl.body        = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
            nl.result_type = k.result_type;
            nl.break_slot  = k.break_slot;
            nl.label       = k.label;
            ns.kind        = std::move(nl);

        } else if constexpr (std::is_same_v<K, lir::SBlock>) {
            ns.kind = lir::SBlock{
                std::make_unique<lir::LBlock>(subst_block(*k.body, s))};

        } else if constexpr (std::is_same_v<K, lir::SBreak>) {
            lir::SBreak nb;
            if (k.value) nb.value = subst_expr(*k.value, s);
            nb.label = k.label;
            ns.kind = std::move(nb);

        } else if constexpr (std::is_same_v<K, lir::SContinue>) {
            ns.kind = k;  // SContinue copies the label via default copy

        } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
            ns.kind = lir::SFieldWrite{k.receiver, k.field, subst_expr(*k.value, s)};

        } else if constexpr (std::is_same_v<K, lir::SDerefFieldWrite>) {
            ns.kind = lir::SDerefFieldWrite{k.receiver, k.type_name, k.field, subst_expr(*k.value, s)};

        } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
            ns.kind = lir::SIndexWrite{
                k.arr, subst_expr(*k.index, s), subst_expr(*k.value, s)};

        } else if constexpr (std::is_same_v<K, lir::SFieldIndexWrite>) {
            ns.kind = lir::SFieldIndexWrite{
                k.receiver, k.field, subst_expr(*k.index, s), subst_expr(*k.value, s)};

        } else if constexpr (std::is_same_v<K, lir::SDerefWrite>) {
            ns.kind = lir::SDerefWrite{subst_expr(*k.ptr, s), subst_expr(*k.value, s)};

        } else if constexpr (std::is_same_v<K, lir::STupleWrite>) {
            ns.kind = lir::STupleWrite{k.receiver, k.index, subst_expr(*k.value, s), k.recv_type};

        } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
            ns.kind = lir::SExprStmt{subst_expr(*k.expr, s)};

        } else if constexpr (std::is_same_v<K, lir::SDelete>) {
            ns.kind = lir::SDelete{subst_expr(*k.expr, s)};

        } else if constexpr (std::is_same_v<K, lir::SDrop>) {
            ns.kind = lir::SDrop{k.var_name, k.drop_fn, subst_type(k.type, s), k.drop_fields};

        } else if constexpr (std::is_same_v<K, lir::SMatch>) {
            lir::SMatch nm;
            nm.scrut = subst_expr(*k.scrut, s);
            for (auto& arm : k.arms) {
                lir::LMatchArm na;
                // Substitute types in PatVariantData / PatTuple bindings
                if (auto* pvd = std::get_if<lir::PatVariantData>(&arm.pat)) {
                    lir::PatVariantData npvd = *pvd;
                    for (auto& bt : npvd.binding_types)
                        bt = subst_type(bt, s);
                    na.pat = std::move(npvd);
                } else if (auto* pt = std::get_if<lir::PatTuple>(&arm.pat)) {
                    lir::PatTuple npt = *pt;
                    for (auto& bt : npt.binding_types)
                        bt = subst_type(bt, s);
                    na.pat = std::move(npt);
                } else {
                    na.pat = arm.pat;
                }
                na.body = std::make_unique<lir::LBlock>(subst_block(*arm.body, s));
                if (arm.guard)
                    na.guard = subst_expr(**arm.guard, s);
                nm.arms.push_back(std::move(na));
            }
            ns.kind = std::move(nm);

        } else if constexpr (std::is_same_v<K, lir::SForEach>) {
            lir::SForEach nf;
            nf.var       = k.var;
            nf.iter      = subst_expr(*k.iter, s);
            nf.elem_type = subst_type(k.elem_type, s);
            nf.arr_size  = k.arr_size;
            nf.is_slice  = k.is_slice;
            nf.body      = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
            ns.kind      = std::move(nf);

        } else if constexpr (std::is_same_v<K, lir::SLetElse>) {
            lir::SLetElse sle;
            // Substitute types in pattern bindings
            if (auto* pvd = std::get_if<lir::PatVariantData>(&k.pat)) {
                lir::PatVariantData npvd = *pvd;
                for (auto& bt : npvd.binding_types)
                    bt = subst_type(bt, s);
                sle.pat = std::move(npvd);
            } else if (auto* pt = std::get_if<lir::PatTuple>(&k.pat)) {
                lir::PatTuple npt = *pt;
                for (auto& bt : npt.binding_types)
                    bt = subst_type(bt, s);
                sle.pat = std::move(npt);
            } else {
                sle.pat = k.pat;
            }
            sle.scrut      = subst_expr(*k.scrut, s);
            sle.else_block = std::make_unique<lir::LBlock>(subst_block(*k.else_block, s));
            ns.kind        = std::move(sle);
        }
    }, st.kind);

    return ns;
}


// ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

lir::LFunction Mono::clone_fn(const lir::LFunction& fn, const SubstMap& s,
                         const PackMap& packs) {
    cur_packs_ = packs;  // make available to subst_expr
    lir::LFunction nf;
    nf.name      = fn.name;
    nf.is_extern = fn.is_extern;
    nf.is_vararg = fn.is_vararg;
    nf.ret_type  = subst_type(fn.ret_type, s);
    for (auto& p : fn.params) {
        if (p.is_variadic) {
            // Expand variadic param into N concrete params.
            // Find the pack type for this param's TypeVar name.
            std::string pack_name;
            if (p.type && p.type->kind == LogosType::Kind::TypeVar)
                pack_name = p.type->type_var_name;
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    auto expanded_name = make_pack_arg_name(p.name, i);
                    nf.params.push_back({expanded_name, pit->second[i]});
                }
            }
        } else {
            nf.params.push_back({p.name, subst_type(p.type, s)});
        }
    }
    nf.body = subst_block(fn.body, s, packs);
    // type_params left empty: instantiated functions are monomorphic
    return nf;
}


// ── Struct monomorphization ───────────────────────────────────

// Clone a struct def with substitution; rename to new_name.
// Method names are rewritten from "Base__method" to "new_name__method".
lir::LStructDef Mono::clone_struct_def(const lir::LStructDef& tmpl,
                                  const SubstMap& s,
                                  const PackMap& packs,
                                  const std::string& new_name) {
    lir::LStructDef nd;
    nd.name = new_name;
    // type_params cleared: result is monomorphic
    for (auto& f : tmpl.fields) {
        if (f.is_variadic) {
            std::string pack_name;
            if (f.type && f.type->kind == LogosType::Kind::TypeVar)
                pack_name = f.type->type_var_name;
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    nd.fields.push_back({f.name + "_" + std::to_string(i), pit->second[i]});
                }
            }
        } else {
            nd.fields.push_back({f.name, subst_type(f.type, s)});
        }
    }
    for (auto& m : tmpl.methods) {
        auto nm = clone_fn(m, s, packs);
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
const lir::LStructDef* Mono::find_best_struct_spec(
    const std::string& base_name,
    const std::vector<const LogosType*>& type_args) {
    auto sit = struct_specs_.find(base_name);
    if (sit == struct_specs_.end()) return nullptr;

    const lir::LStructDef* best       = nullptr;
    int                    best_score = -1;
    bool                   ambiguous  = false;

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
        if (score > best_score) {
            best_score = score;
            best = spec;
            ambiguous = false;
        } else if (score == best_score && best_score != -1) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("ambiguous specializations for struct '{}'", base_name),
            "", 0});
    }
    return best;
}


// Walk all output functions collecting generic struct instantiations needed.
void Mono::collect_struct_needs_from_output() {
    for (auto& fn : out_.functions) {
        collect_type_for_structs(fn.ret_type);
        for (auto& p : fn.params) collect_type_for_structs(p.type);
        collect_struct_needs_from_block(fn.body);
    }
    // Also walk already-instantiated structs (field types may reference more).
    for (auto& sd : out_.structs)
        for (auto& f : sd.fields) collect_type_for_structs(f.type);
}


void Mono::collect_struct_needs_from_block(const lir::LBlock& b) {
    for (auto& st : b.stmts) collect_struct_needs_from_stmt(st);
}


void Mono::collect_struct_needs_from_stmt(const lir::LStmt& st) {
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
        } else if constexpr (std::is_same_v<K, lir::SBlock>) {
            collect_struct_needs_from_block(*k.body);
        } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SDerefFieldWrite>) {
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
            collect_struct_needs_from_expr(*k.index);
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SFieldIndexWrite>) {
            collect_struct_needs_from_expr(*k.index);
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SDerefWrite>) {
            collect_struct_needs_from_expr(*k.ptr);
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::STupleWrite>) {
            collect_struct_needs_from_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
            collect_struct_needs_from_expr(*k.expr);
        } else if constexpr (std::is_same_v<K, lir::SDelete>) {
            collect_struct_needs_from_expr(*k.expr);
        } else if constexpr (std::is_same_v<K, lir::SDrop>) {
            // no-op
        } else if constexpr (std::is_same_v<K, lir::SMatch>) {
            collect_struct_needs_from_expr(*k.scrut);
            for (auto& arm : k.arms) collect_struct_needs_from_block(*arm.body);
        }
    }, st.kind);
}


void Mono::collect_struct_needs_from_expr(const lir::LExpr& e) {
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
        } else if constexpr (std::is_same_v<K, lir::EFormatCall>) {
            collect_struct_needs_from_expr(*k.fmt);
            for (auto& a : k.args) collect_struct_needs_from_expr(*a);
        } else if constexpr (std::is_same_v<K, lir::EPackExpand>) {
            // nothing
        } else if constexpr (std::is_same_v<K, lir::EMatchExpr>) {
            collect_struct_needs_from_expr(*k.scrut);
            for (auto& arm : k.arms) {
                if (arm.guard) collect_struct_needs_from_expr(**arm.guard);
                collect_struct_needs_from_expr(*arm.value);
            }
        } else if constexpr (std::is_same_v<K, lir::EBlockExpr>) {
            if (k.block) collect_struct_needs_from_block(*k.block);
            if (k.result) collect_struct_needs_from_expr(*k.result);
        }
    }, e.kind);
}


// Process all pending struct instantiations (may discover more via field types).
void Mono::instantiate_struct_templates() {
    collect_struct_needs_from_output();

    while (!needed_struct_insts_.empty()) {
        // Take a copy of current needs (instantiating may add more).
        auto current = std::move(needed_struct_insts_);
        needed_struct_insts_.clear();

        for (auto& [cname, info] : current) {
            if (struct_done_.count(cname)) continue;
            struct_done_.insert(cname);

            const LogosType* struct_t = info.first;
            depth_ = info.second;

            const std::string& base = struct_t->struct_name;
            SubstMap subst;

            const lir::LStructDef* tmpl = nullptr;
            PackMap packs;
            if (auto* spec = find_best_struct_spec(base, struct_t->type_args)) {
                for (size_t i = 0; i < spec->spec_patterns.size() &&
                                   i < struct_t->type_args.size(); ++i)
                    match_type(struct_t->type_args[i], spec->spec_patterns[i], subst);
                tmpl = spec;
            } else {
                auto it = struct_templates_.find(base);
                if (it == struct_templates_.end()) continue;
                tmpl = it->second;
                for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                    if (tmpl->type_params[i].is_variadic) {
                        std::vector<const LogosType*> pack;
                        while (j < struct_t->type_args.size()) pack.push_back(struct_t->type_args[j++]);
                        packs[tmpl->type_params[i].name] = std::move(pack);
                    } else if (j < struct_t->type_args.size()) {
                        subst[tmpl->type_params[i].name] = struct_t->type_args[j++];
                    }
                }
            }

            auto inst = clone_struct_def(*tmpl, subst, packs, cname);
            // Collect field types of new struct for further instantiation.
            for (auto& f : inst.fields) collect_type_for_structs(f.type);
            out_.structs.push_back(std::move(inst));
        }
        depth_ = 0;
    }
}


// ── Class monomorphization ────────────────────────────────────

// Clone a class def with substitution; rename to new_name.
// Mirrors clone_struct_def but preserves vtable_order, parent_name, etc.
lir::LClassDef Mono::clone_class_def(const lir::LClassDef& tmpl,
                                const SubstMap& s,
                                const PackMap& packs,
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
    for (auto& f : tmpl.own_fields) {
        if (f.is_variadic) {
            std::string pack_name;
            if (f.type && f.type->kind == LogosType::Kind::TypeVar)
                pack_name = f.type->type_var_name;
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    nd.own_fields.push_back({f.name + "_" + std::to_string(i), pit->second[i]});
                }
            }
        } else {
            nd.own_fields.push_back({f.name, subst_type(f.type, s)});
        }
    }
    for (auto& m : tmpl.methods) {
        auto nm = clone_fn(m, s, packs);
        auto sep = m.name.find("__");
        if (sep != std::string::npos)
            nm.name = new_name + m.name.substr(sep);
        nd.methods.push_back(std::move(nm));
    }
    return nd;
}


// Instantiate a single generic class by concrete name + type.
// Ensures the parent class is instantiated first (DFS order = parent before child).
void Mono::instantiate_one_class(const std::string& cname, const LogosType* class_t, int depth) {
    if (class_done_.count(cname)) return;
    class_done_.insert(cname);

    depth_ = depth;

    const std::string& base = class_t->struct_name;
    auto it = class_templates_.find(base);
    if (it == class_templates_.end()) return;
    const lir::LClassDef* tmpl = it->second;

    SubstMap subst;
    PackMap packs;
    for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
        if (tmpl->type_params[i].is_variadic) {
            std::vector<const LogosType*> pack;
            while (j < class_t->type_args.size()) pack.push_back(class_t->type_args[j++]);
            packs[tmpl->type_params[i].name] = std::move(pack);
        } else if (j < class_t->type_args.size()) {
            subst[tmpl->type_params[i].name] = class_t->type_args[j++];
        }
    }

    size_t class_idx = out_.classes.size();
    {
        lir::LClassDef seed;
        seed.name = cname;
        out_.classes.push_back(std::move(seed));
    }

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
            // Inherit depth for parent instantiation (DFS)
            instantiate_one_class(parent_cname, parent_t, depth);
        }
    }

    auto inst = clone_class_def(*tmpl, subst, packs, cname);
    for (auto& f : inst.own_fields) collect_type_for_classes(f.type);
    
    // Fill the seed
    out_.classes[class_idx] = std::move(inst);
}


lir::LEnumDef Mono::clone_enum_def(const lir::LEnumDef& tmpl,
                              const SubstMap& s,
                              const PackMap& packs,
                              const std::string& new_name) {
    lir::LEnumDef nd;
    nd.name = new_name;
    for (auto& v : tmpl.variants) {
        lir::LVariant nv;
        nv.name = v.name;
        nv.disc = v.disc;
        // Variadic expansion for variants like Multi(...T)
        if (v.is_variadic && !v.payload_types.empty()) {
            auto* pt = v.payload_types[0];
            if (pt->kind == LogosType::Kind::TypeVar) {
                auto pit = packs.find(pt->type_var_name);
                if (pit != packs.end()) {
                    for (auto* pt_in_pack : pit->second)
                        nv.payload_types.push_back(subst_type(pt_in_pack, s));
                } else {
                    nv.payload_types.push_back(subst_type(pt, s));
                }
            } else {
                nv.payload_types.push_back(subst_type(pt, s));
            }
        } else {
            for (auto* pt : v.payload_types)
                nv.payload_types.push_back(subst_type(pt, s));
        }
        nd.variants.push_back(std::move(nv));
    }
    return nd;
}


void Mono::instantiate_enum_templates() {
    // Instantiate generic enums that were recorded during function cloning.
    // Simple approach: iterate until no more needed (fixed-point).
    while (true) {
        std::vector<std::pair<std::string, std::pair<std::vector<const LogosType*>, int>>> todo;
        for (auto& [cname, info] : needed_enum_insts_) {
            if (enum_done_.count(cname)) continue;
            todo.push_back({cname, info});
        }
        if (todo.empty()) break;
        for (auto& [cname, info] : todo) {
            enum_done_.insert(cname);
            const auto& args = info.first;
            depth_ = info.second;
            // Find the template
            // Extract base name from cname (before first __)
            std::string base = cname;
            auto pos = base.find("__");
            if (pos != std::string::npos) base = base.substr(0, pos);
            auto tit = enum_templates_.find(base);
            if (tit == enum_templates_.end()) continue;
            auto* tmpl = tit->second;
            // Build substitution map and packs
            SubstMap subst;
            PackMap packs;
            for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                if (tmpl->type_params[i].is_variadic) {
                    std::vector<const LogosType*> pack;
                    while (j < args.size()) pack.push_back(args[j++]);
                    packs[tmpl->type_params[i].name] = std::move(pack);
                } else if (j < args.size()) {
                    subst[tmpl->type_params[i].name] = args[j++];
                }
            }
            // Instantiate: substitute payload types and methods
            auto inst = clone_enum_def(*tmpl, subst, packs, cname);

            // Instantiate any impl<T> methods stored as generic functions in prog.functions.
            // Convention: function name starts with "Base__" and has matching type params.
            std::string prefix = base + "__";
            for (auto& fn : in_.functions) {
                if (fn.type_params.empty()) continue;
                if (fn.name.substr(0, prefix.size()) != prefix) continue;
                // Match type params to subst keys
                bool matches = fn.type_params.size() == tmpl->type_params.size();
                if (!matches) continue;
                std::string inst_name = cname + fn.name.substr(base.size());
                if (done_.count(inst_name)) continue;
                SubstMap fn_subst = subst;
                PackMap fn_packs = packs;
                // Override type params with the enum's type param names if different
                for (size_t i = 0, j = 0; i < fn.type_params.size(); ++i) {
                    if (fn.type_params[i].is_variadic) {
                         std::vector<const LogosType*> pack;
                         while (j < args.size()) pack.push_back(args[j++]);
                         fn_packs[fn.type_params[i].name] = std::move(pack);
                    } else if (j < args.size()) {
                        fn_subst[fn.type_params[i].name] = args[j++];
                    }
                }
                auto nm = clone_fn(fn, fn_subst, fn_packs);
                nm.name = inst_name;
                done_.insert(inst_name);
                out_.functions.push_back(std::move(nm));
            }
            out_.enums.push_back(std::move(inst));
        }
    }
}


void Mono::instantiate_class_templates() {
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

        for (auto& [cname, info] : current)
            instantiate_one_class(cname, info.first, info.second);

        depth_ = 0;
    }
}

} // namespace logos::compiler
