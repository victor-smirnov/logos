name: refpeel
file: src/compiler/sema_expr.cpp
---
    // T2-26 prereq: auto-deref a `&T`/`&mut T` operand whose pointee is a
---
    // PROBE 2026-09-15a-refeq (refpeel / refagg / refeq) — see src/compiler/PROBES.md.
    const bool refeq_cmp_op = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    auto refeq_is_ref = [](TypeRef t) {
        return t && (t.kind() == LogosType::Kind::Ref || t.kind() == LogosType::Kind::MutRef);
    };
    auto refeq_kind = [&](TypeRef t) -> const char* {
        if (!t) return "null";
        using K = LogosType::Kind;
        switch (t.kind()) {
        case K::Struct: return "Struct";
        case K::Enum: return "Enum";
        case K::Tuple: return "Tuple";
        case K::TypeVar: return "TypeVar";
        case K::Array: return "Array";
        case K::Slice: return "Slice";
        case K::Ref: case K::MutRef: return "Ref";
        case K::Ptr: return "Ptr";
        default: break;
        }
        if (is_integer_kind(t.kind()) || t.kind() == K::F32 || t.kind() == K::F64 || t.kind() == K::Bool ||
            t.kind() == K::Char)
            return "Prim";
        return "Other";
    };
    if (refeq_cmp_op && refeq_is_ref(lt) && refeq_is_ref(rt)) {
        int depth = 1;
        for (TypeRef a = TypeRef(lt).pointee(), b = TypeRef(rt).pointee(); refeq_is_ref(a) && refeq_is_ref(b);
             a = a.pointee(), b = b.pointee())
            ++depth;
        logos::probe::census(std::string("refeq.sema.refpair.depth") + (depth > 1 ? "2+." : "1.") +
                             refeq_kind(TypeRef(lt).pointee()) + "." + refeq_kind(TypeRef(rt).pointee()));
        if (depth > 1 && (logos::probe::on("refpeel") || logos::probe::on("refeq"))) {
            while (refeq_is_ref(TypeRef(lt).pointee()) && refeq_is_ref(TypeRef(rt).pointee())) {
                TypeRef pl = TypeRef(lt).pointee(), pr = TypeRef(rt).pointee();
                lhs = builder().deref(std::move(lhs), pl);
                lt = pl;
                rhs = builder().deref(std::move(rhs), pr);
                rt = pr;
            }
        }
    }
    // PROBE 2026-09-15a-refeq (refagg / refeq): a reference pair to a value compared in place (array, str, all-primitive
    // tuple) is dereferenced to that value compare.
    if (refeq_cmp_op && refeq_is_ref(lt) && refeq_is_ref(rt)) {
        TypeRef pl = TypeRef(lt).pointee(), pr = TypeRef(rt).pointee();
        auto placeval = [&](TypeRef p) {
            if (!p) return false;
            using K = LogosType::Kind;
            if (p.kind() == K::Array) return true;
            if (p.kind() == K::Slice && p.elem() && p.elem().kind() == K::U8) return true;
            if (p.kind() == K::Tuple) {
                auto es = p.tuple_elems();
                if (es.empty()) return false;
                for (auto e : es)
                    if (!e || !(is_integer_kind(e.kind()) || e.kind() == K::F32 || e.kind() == K::F64 ||
                                e.kind() == K::Bool || e.kind() == K::Char))
                        return false;
                return true;
            }
            return false;
        };
        if (placeval(pl) && placeval(pr) && pl.kind() == pr.kind() &&
            (logos::probe::on("refagg") || logos::probe::on("refeq"))) {
            lhs = builder().deref(std::move(lhs), pl);
            lt = pl;
            rhs = builder().deref(std::move(rhs), pr);
            rt = pr;
        }
    }

    // T2-26 prereq: auto-deref a `&T`/`&mut T` operand whose pointee is a
===
name: refstruct
file: src/compiler/sema_expr.cpp
---
    // Operator overloading: if LHS is a struct, desugar to trait method call.
    if (TypeRef(lt).kind() == LogosType::Kind::Struct) {
---
    // Operator overloading: if LHS is a struct, desugar to trait method call.
    // PROBE 2026-09-15a-refeq (refstruct / refeq): a comparison of two references to structs compares THROUGH them.
    const bool refeq_spair_shape = refeq_cmp_op && refeq_is_ref(lt) && refeq_is_ref(rt) && TypeRef(lt).pointee() &&
                                   TypeRef(rt).pointee() && TypeRef(lt).pointee().kind() == LogosType::Kind::Struct &&
                                   TypeRef(rt).pointee().kind() == LogosType::Kind::Struct;
    const bool refeq_spair = refeq_spair_shape && (logos::probe::on("refstruct") || logos::probe::on("refeq"));
    TypeRef lt_sv = refeq_spair ? TypeRef(lt).pointee() : TypeRef(lt);
    TypeRef rt_sv = refeq_spair ? TypeRef(rt).pointee() : TypeRef(rt);
    if (TypeRef(lt_sv).kind() == LogosType::Kind::Struct) {
===
name: refstruct_lookup
file: src/compiler/sema_expr.cpp
---
            auto type_name = concrete_struct_name(lt);
            auto mangled = type_name + "__" + method_name;
            auto fit = find_func_by_base_and_signature(mangled, {lt, rt}, false);
---
            auto type_name = concrete_struct_name(lt_sv);
            auto mangled = type_name + "__" + method_name;
            auto fit = refeq_spair
                ? find_func_by_base_and_signature(mangled, {make_ref(false, lt_sv), make_ref(false, rt_sv)}, false)
                : find_func_by_base_and_signature(mangled, {lt, rt}, false);
===
name: refstruct_noimpl
file: src/compiler/sema_expr.cpp
---
            // No impl found — fall through to normal type checking
---
            // No impl found — fall through to normal type checking
            if (refeq_spair) logos::probe::census("refeq.sema.spair.noimpl");
===
name: refagg_enum
file: src/compiler/sema_expr.cpp
---
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::Enum &&
        TypeRef(rt).kind() == LogosType::Kind::Enum &&
        TypeRef(lt).enum_name() == TypeRef(rt).enum_name()) {
---
    // PROBE 2026-09-15a-refeq (refagg / refeq): a reference pair to one enum passes the references to its eq impl; with no
    // impl (a C-like enum) the discriminants are compared through them.
    if (refeq_spair_shape) logos::probe::census(refeq_spair ? "refeq.sema.spair.routed" : "refeq.sema.spair.unarmed");
    const bool refeq_epair_shape = (op == "==" || op == "!=") && refeq_is_ref(lt) && refeq_is_ref(rt) &&
                                   TypeRef(lt).pointee() && TypeRef(rt).pointee() &&
                                   TypeRef(lt).pointee().kind() == LogosType::Kind::Enum &&
                                   TypeRef(rt).pointee().kind() == LogosType::Kind::Enum &&
                                   TypeRef(lt).pointee().enum_name() == TypeRef(rt).pointee().enum_name();
    if (refeq_epair_shape) logos::probe::census("refeq.sema.epair.arrive");
    if (refeq_epair_shape && (logos::probe::on("refagg") || logos::probe::on("refeq"))) {
        TypeRef pl = TypeRef(lt).pointee(), pr = TypeRef(rt).pointee();
        std::string ebare = std::string(pl.enum_name()) + "__" + ((op == "==") ? "eq" : "ne");
        const SemaFuncInfo* echosen = nullptr;
        for (auto* c : find_func_candidates(ebare))
            if (c && c->param_types.size() == 2) { echosen = c; break; }
        if (echosen) {
            std::vector<lir::LExprPtr> eargs;
            eargs.push_back(std::move(lhs));
            eargs.push_back(std::move(rhs));
            std::string esym = echosen->symbol_name.empty() ? ebare : echosen->symbol_name;
            if (!echosen->type_params.empty()) {
                std::vector<TypeRef> m_type_args;
                for (auto ta : pl.type_args()) m_type_args.push_back(ta);
                return finish_generic_call(esym, *echosen, std::move(m_type_args), std::move(eargs));
            }
            return builder().call(esym, {}, std::move(eargs), bool_t());
        }
        logos::probe::census("refeq.sema.epair.noimpl");
        lhs = builder().deref(std::move(lhs), pl);
        lt = pl;
        rhs = builder().deref(std::move(rhs), pr);
        rt = pr;
    }
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::Enum &&
        TypeRef(rt).kind() == LogosType::Kind::Enum &&
        TypeRef(lt).enum_name() == TypeRef(rt).enum_name()) {
        logos::probe::census("refeq.sema.enum.byvalue");
===
name: refagg_tuple
file: src/compiler/sema_expr.cpp
---
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::Tuple &&
        TypeRef(rt).kind() == LogosType::Kind::Tuple) {
        auto elems = TypeRef(lt).tuple_elems();
---
    // PROBE 2026-09-15a-refeq (refagg / refeq): a reference pair to a tuple passes the references to the tuple eq impl.
    const bool refeq_tpair_shape = (op == "==" || op == "!=") && refeq_is_ref(lt) && refeq_is_ref(rt) &&
                                   TypeRef(lt).pointee() && TypeRef(rt).pointee() &&
                                   TypeRef(lt).pointee().kind() == LogosType::Kind::Tuple &&
                                   TypeRef(rt).pointee().kind() == LogosType::Kind::Tuple;
    if (refeq_tpair_shape) logos::probe::census("refeq.sema.tpair.arrive");
    const bool refeq_tpair = refeq_tpair_shape && (logos::probe::on("refagg") || logos::probe::on("refeq"));
    TypeRef refeq_tlt = lt, refeq_trt = rt;
    if (refeq_tpair) {
        lt = TypeRef(lt).pointee();
        rt = TypeRef(rt).pointee();
    }
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::Tuple &&
        TypeRef(rt).kind() == LogosType::Kind::Tuple) {
        auto elems = TypeRef(lt).tuple_elems();
===
name: refagg_tuple_args
file: src/compiler/sema_expr.cpp
---
            auto lref_e = autoref_operand(std::move(lhs), false, lty);
            auto rref_e = autoref_operand(std::move(rhs), false, rty);
---
            auto lref_e = refeq_tpair ? std::move(lhs) : autoref_operand(std::move(lhs), false, lty);
            auto rref_e = refeq_tpair ? std::move(rhs) : autoref_operand(std::move(rhs), false, rty);
===
name: refagg_tuple_restore
file: src/compiler/sema_expr.cpp
---
        // No tuple Eq impl found — fall through to the primitive-only
---
        if (refeq_tpair) {  // PROBE 2026-09-15a-refeq: no impl — the operands are still the references
            logos::probe::census("refeq.sema.tpair.noimpl");
            lt = refeq_tlt;
            rt = refeq_trt;
        }
        // No tuple Eq impl found — fall through to the primitive-only
===
name: refagg_typevar
file: src/compiler/sema_expr.cpp
---
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::TypeVar) {
        std::string tv_name(TypeRef(lt).type_var_name());
---
    // PROBE 2026-09-15a-refeq (refagg / refeq): a reference pair to a type variable calls `eq` on the references.
    const bool refeq_vpair_shape = (op == "==" || op == "!=") && refeq_is_ref(lt) && refeq_is_ref(rt) &&
                                   TypeRef(lt).pointee() && TypeRef(rt).pointee() &&
                                   TypeRef(lt).pointee().kind() == LogosType::Kind::TypeVar &&
                                   TypeRef(rt).pointee().kind() == LogosType::Kind::TypeVar;
    if (refeq_vpair_shape) logos::probe::census("refeq.sema.vpair.arrive");
    const bool refeq_vpair = refeq_vpair_shape && (logos::probe::on("refagg") || logos::probe::on("refeq"));
    TypeRef refeq_vlt = lt;
    if (refeq_vpair) lt = TypeRef(lt).pointee();
    if ((op == "==" || op == "!=") &&
        TypeRef(lt).kind() == LogosType::Kind::TypeVar) {
        std::string tv_name(TypeRef(lt).type_var_name());
===
name: refagg_typevar_args
file: src/compiler/sema_expr.cpp
---
            auto lref = take_operand_ref(map_of(node.get(la::LHS.code)), std::move(lhs), lt);
            auto rref = take_operand_ref(map_of(node.get(la::RHS.code)), std::move(rhs), rt);
            lir::EMethodCall mc;
---
            auto lref = refeq_vpair ? std::move(lhs)
                                    : take_operand_ref(map_of(node.get(la::LHS.code)), std::move(lhs), lt);
            auto rref = refeq_vpair ? std::move(rhs)
                                    : take_operand_ref(map_of(node.get(la::RHS.code)), std::move(rhs), rt);
            lir::EMethodCall mc;
===
name: refagg_typevar_restore
file: src/compiler/sema_expr.cpp
---
        // No eq-providing bound — fall through to the generic operator check.
    }
---
        // No eq-providing bound — fall through to the generic operator check.
    }
    if (refeq_vpair) {  // PROBE 2026-09-15a-refeq
        logos::probe::census("refeq.sema.vpair.noeq");
        lt = refeq_vlt;
    }
===
name: refeq_mono_census
file: src/compiler/mono_clone.cpp
---
            auto lt = new_lhs ? new_lhs.type(out_.type_pool.impl()) : TypeRef{};
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
---
            auto lt = new_lhs ? new_lhs.type(out_.type_pool.impl()) : TypeRef{};
            // PROBE 2026-09-15a-refeq (census only): a comparison of a reference pair after substitution.
            if (lt && new_rhs && (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")) {
                auto rt2 = new_rhs.type(out_.type_pool.impl());
                auto isr = [](TypeRef t) {
                    return t && (t.kind() == LogosType::Kind::Ref || t.kind() == LogosType::Kind::MutRef);
                };
                if (isr(lt) && isr(rt2)) {
                    TypeRef p = TypeRef(lt).pointee();
                    using K = LogosType::Kind;
                    logos::probe::census(!p                         ? "refeq.mono.refpair.null"
                                         : p.kind() == K::Struct    ? "refeq.mono.refpair.Struct"
                                         : isr(p)                   ? "refeq.mono.refpair.Ref"
                                         : p.kind() == K::Enum      ? "refeq.mono.refpair.Enum"
                                         : p.kind() == K::Tuple     ? "refeq.mono.refpair.Tuple"
                                         : p.kind() == K::Char      ? "refeq.mono.refpair.Char"
                                                                    : "refeq.mono.refpair.Other");
                }
            }
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
===
name: refeq_mono_include
file: src/compiler/mono_clone.cpp
---
#include "mono_impl.hpp"
---
#include "mono_impl.hpp"
#include <logos/compiler/probe.hpp>  // PROBE 2026-09-15a-refeq
===
name: refeq_mlirgen_census
file: src/compiler/mlir_gen_expr.cpp
---
        TypeRef lhs_pe = is_ref_to_prim(lhs_ty);
        TypeRef rhs_pe = is_ref_to_prim(rhs_ty);
---
        TypeRef lhs_pe = is_ref_to_prim(lhs_ty);
        TypeRef rhs_pe = is_ref_to_prim(rhs_ty);
        {   // PROBE 2026-09-15a-refeq (census only)
            auto isr = [](TypeRef t) {
                return t && (t.kind() == LogosType::Kind::Ref || t.kind() == LogosType::Kind::MutRef);
            };
            logos::probe::census(lhs_pe && rhs_pe                    ? "refeq.mlirgen.refprim.load"
                                 : isr(lhs_ty) && isr(rhs_ty)        ? "refeq.mlirgen.refpair.ptrcmp"
                                                                     : "refeq.mlirgen.ptr.other");
        }
===
