// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_clone.cpp — Expression/statement substitution and function/type cloning.

#include "mono_impl.hpp"
#include "logos/compiler/sha256.hpp"
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_builder.hpp>
#include <functional>

namespace logos::compiler {

// Type-subst function used by the pattern walker.
using TypeSubstFn = std::function<TypeRef(TypeRef)>;
namespace {
class PatSubstWalker {
public:
    PatSubstWalker(TypeSubstFn st, const TypePoolImpl* pool) noexcept
        : st_(std::move(st)), pool_(pool) {}
    lir::Pattern walk(lir_view::PatRef pref) const;
private:
    TypeSubstFn         st_;
    const TypePoolImpl* pool_;
};
} // anonymous

lir::Pattern Mono::subst_pattern(const lir::Pattern& pat, const SubstMap& s) {
    auto pref = pat_ref_of(pat);
    if (!pref) return pat;  // mirror miss — pass through (defensive)
    return subst_pattern(pref, s);
}

lir::Pattern Mono::subst_pattern(lir_view::PatRef pref, const SubstMap& s) {
    PatSubstWalker w([&](TypeRef t) { return subst_type(t, s); },
                     out_.type_pool.impl());
    return w.walk(pref);
}

lir::LExprPtr Mono::subst_expr(const lir::LExpr& e, const SubstMap& s,
                          const PackMap& /*unused*/) {
    // packs are stored in cur_packs_ (set by clone_fn)
    auto* result = lir::alloc_expr(out_);
    result->type = subst_type(e.type, s);

    // Stage 3g.4b: every LExpr reaching mono is mirrored — sema's LirBuilder
    // emits per-node, and lir_mirror_emit_into runs once at end of sema. The
    // view switch handles all 41 expr Codes; no std::visit fallback remains.
    auto eref = expr_ref_of(e);
    if (!eref) {
        std::fprintf(stderr,
            "mono.subst_expr: input LExpr lacks mirror_offset_ "
            "(variant index=%zu)\n", e.kind.index());
        std::abort();
    }
    {
        auto subst_child_expr = [&](lir_view::ExprRef er) -> lir::LExprPtr {
            auto* le = lexpr_of(er);
            return le ? subst_expr(*le, s) : nullptr;
        };
        auto subst_child_block = [&](lir_view::BlockRef br) -> lir::LBlock {
            auto* lb = lblock_of(br);
            return lb ? subst_block(*lb, s) : lir::LBlock{};
        };
        using C = lir_schema::expr::Code;
        switch (eref.kind()) {
        // Stage 2: variant-free leaf-kind cases. Mirror emitted directly,
        // result->kind stays at default (unread by view-based readers).
        case C::LitInt:
            result->mirror_offset_ = lir_mirror_emit_lit_int(
                out_, result->type, lir_view::ELitIntView{eref}.value());
            break;
        case C::LitFloat:
            result->mirror_offset_ = lir_mirror_emit_lit_float(
                out_, result->type, lir_view::ELitFloatView{eref}.value());
            break;
        case C::LitBool:
            result->mirror_offset_ = lir_mirror_emit_lit_bool(
                out_, result->type, lir_view::ELitBoolView{eref}.value());
            break;
        case C::LitStr: {
            // Copy to std::string: lir_mirror_emit_* may grow the arena,
            // which invalidates string_view pointers into it.
            std::string v(lir_view::ELitStrView{eref}.value());
            result->mirror_offset_ = lir_mirror_emit_lit_str(out_, result->type, v);
            break;
        }
        case C::VarRef: {
            std::string n(lir_view::EVarRefView{eref}.name());
            result->mirror_offset_ = lir_mirror_emit_var_ref(out_, result->type, n);
            break;
        }
        case C::AddrOf: {
            std::string n(lir_view::EAddrOfView{eref}.var_name());
            result->mirror_offset_ = lir_mirror_emit_addr_of(out_, result->type, n);
            break;
        }
        case C::PackExpand: {
            std::string n(lir_view::EPackExpandView{eref}.var_name());
            result->mirror_offset_ = lir_mirror_emit_pack_expand(out_, result->type, n);
            break;
        }
        case C::SizeOf: {
            auto t = lir_view::ESizeOfView{eref}.elem_type(out_.type_pool.impl());
            result->mirror_offset_ = lir_mirror_emit_size_of(
                out_, result->type, subst_type(t, s));
            break;
        }
        case C::Deref: {
            auto op = subst_child_expr(lir_view::EDerefView{eref}.operand());
            result->mirror_offset_ = lir_mirror_emit_deref(
                out_, result->type, op);
            break;
        }
        case C::FieldRead: {
            lir_view::EFieldReadView v{eref};
            std::string field(v.field());
            auto rcv = subst_child_expr(v.receiver());
            result->mirror_offset_ = lir_mirror_emit_field_read(
                out_, result->type, rcv, field);
            break;
        }
        case C::TupleIndex: {
            lir_view::ETupleIndexView v{eref};
            uint32_t idx = v.index();
            auto rcv = subst_child_expr(v.receiver());
            result->mirror_offset_ = lir_mirror_emit_tuple_index(
                out_, result->type, rcv, idx);
            break;
        }
        case C::IndexRead: {
            lir_view::EIndexReadView v{eref};
            auto rcv = subst_child_expr(v.receiver());
            auto idx = subst_child_expr(v.index());
            result->mirror_offset_ = lir_mirror_emit_index_read(
                out_, result->type, rcv, idx);
            break;
        }
        case C::Cast: {
            lir_view::ECastView v{eref};
            std::string hbf(v.hermes_build_fn());
            auto op = subst_child_expr(v.operand());
            result->mirror_offset_ = lir_mirror_emit_cast(
                out_, result->type, op, hbf);
            break;
        }
        case C::Try: {
            lir_view::ETryView v{eref};
            int32_t ok_disc  = v.ok_disc();
            int32_t err_disc = v.err_disc();
            auto inner = subst_child_expr(v.inner());
            result->mirror_offset_ = lir_mirror_emit_try(
                out_, result->type, inner, ok_disc, err_disc);
            break;
        }
        case C::SliceLit: {
            lir_view::ESliceLitView v{eref};
            auto base = subst_child_expr(v.base());
            auto len  = subst_child_expr(v.len());
            result->mirror_offset_ = lir_mirror_emit_slice_lit(
                out_, result->type, base, len);
            break;
        }
        case C::SliceIndex: {
            lir_view::ESliceIndexView v{eref};
            auto slice = subst_child_expr(v.slice());
            auto idx   = subst_child_expr(v.index());
            result->mirror_offset_ = lir_mirror_emit_slice_index(
                out_, result->type, slice, idx);
            break;
        }
        case C::SliceLen: {
            auto sl = subst_child_expr(lir_view::ESliceLenView{eref}.slice());
            result->mirror_offset_ = lir_mirror_emit_slice_len(out_, result->type, sl);
            break;
        }
        case C::SlicePtr: {
            auto sl = subst_child_expr(lir_view::ESlicePtrView{eref}.slice());
            result->mirror_offset_ = lir_mirror_emit_slice_ptr(out_, result->type, sl);
            break;
        }
        case C::IfExpr: {
            lir_view::EIfExprView v{eref};
            auto cond = subst_child_expr(v.cond());
            auto thn  = subst_child_expr(v.then_val());
            auto els  = subst_child_expr(v.else_val());
            result->mirror_offset_ = lir_mirror_emit_if_expr(
                out_, result->type, cond, thn, els);
            break;
        }
        case C::TupleLit: {
            std::vector<lir::LExprPtr> elems;
            lir_view::ETupleLitView{eref}.each_elem(
                [&](lir_view::ExprRef er) { elems.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_tuple_lit(
                out_, result->type, elems);
            break;
        }
        case C::ArrLit: {
            std::vector<lir::LExprPtr> elems;
            lir_view::EArrLitView{eref}.each_elem(
                [&](lir_view::ExprRef er) { elems.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_arr_lit(
                out_, result->type, elems);
            break;
        }
        case C::ClosureCall: {
            lir_view::EClosureCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir::LExprPtr> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_closure_call(
                out_, result->type, callee, args);
            break;
        }
        case C::FnPtrCall: {
            lir_view::EFnPtrCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir::LExprPtr> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_fn_ptr_call(
                out_, result->type, callee, args);
            break;
        }
        case C::FormatCall: {
            lir_view::EFormatCallView v{eref};
            lir::EFormatCall nf;
            nf.fmt       = subst_child_expr(v.fmt());
            nf.arg_types = v.arg_types(out_.type_pool.impl());
            v.each_arg([&](lir_view::ExprRef er) { nf.args.push_back(subst_child_expr(er)); });
            result->kind = std::move(nf);
            break;
        }
        case C::PtrArith: {
            lir_view::EPtrArithView v{eref};
            uint8_t op = v.op_code();
            auto ptr = subst_child_expr(v.ptr());
            auto off = subst_child_expr(v.offset());
            result->mirror_offset_ = lir_mirror_emit_ptr_arith(
                out_, result->type, op, ptr, off);
            break;
        }
        case C::PtrDiff: {
            lir_view::EPtrDiffView v{eref};
            bool by_byte = v.by_byte();
            auto lhs = subst_child_expr(v.lhs());
            auto rhs = subst_child_expr(v.rhs());
            result->mirror_offset_ = lir_mirror_emit_ptr_diff(
                out_, result->type, by_byte, lhs, rhs);
            break;
        }
        case C::BlockExpr: {
            lir_view::EBlockExprView v{eref};
            lir::LBlock* nb_block = nullptr;
            lir::LExprPtr nb_result = nullptr;
            if (auto br = v.block(); br) {
                nb_block = lir::alloc_block(out_, subst_child_block(br));
                lir_mirror_emit_block_node(out_, *nb_block);
            }
            if (auto rr = v.result(); rr)
                nb_result = subst_child_expr(rr);
            result->mirror_offset_ = lir_mirror_emit_block_expr(
                out_, result->type, nb_block, nb_result);
            break;
        }
        case C::ReflectOf: {
            auto resolved = subst_type(
                lir_view::EReflectOfView{eref}.type(out_.type_pool.impl()), s);
            result->mirror_offset_ = lir_mirror_emit_reflect_of(
                out_, result->type, resolved);
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct &&
                TypeRef(resolved).type_args().empty()) {
                std::string pkg{TypeRef(resolved).pkg_name()};
                std::string fqn = pkg.empty() ? std::string(TypeRef(resolved).struct_name())
                                              : pkg + "::" + std::string(TypeRef(resolved).struct_name());
                out_.reflect_requests.insert(fqn);
            }
            break;
        }
        case C::Unary: {
            lir_view::EUnaryView v{eref};
            std::string op{v.op()};
            auto new_op = subst_child_expr(v.operand());
            auto vt = new_op ? new_op->type : TypeRef{};
            if (vt && TypeRef(vt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "-") method_name = "neg";
                else if (op == "!") method_name = "not_";
                if (!method_name.empty()) {
                    std::string callee = concrete_struct_name(vt) + "__" + method_name;
                    std::vector<lir::LExprPtr> args; args.push_back(std::move(new_op));
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, callee, {}, args);
                    break;
                }
            }
            result->mirror_offset_ = lir_mirror_emit_unary(
                out_, result->type, op, new_op);
            break;
        }
        case C::BinOp: {
            lir_view::EBinOpView v{eref};
            std::string op{v.op()};
            auto new_lhs = subst_child_expr(v.lhs());
            auto new_rhs = subst_child_expr(v.rhs());
            auto lt = new_lhs ? new_lhs->type : TypeRef{};
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "+")  method_name = "add";
                else if (op == "-")  method_name = "sub";
                else if (op == "*")  method_name = "mul";
                else if (op == "/")  method_name = "div";
                else if (op == "%")  method_name = "rem";
                else if (op == "==") method_name = "eq";
                else if (op == "!=") method_name = "ne";
                else if (op == "<")  method_name = "lt";
                else if (op == "<=") method_name = "le";
                else if (op == ">")  method_name = "gt";
                else if (op == ">=") method_name = "ge";
                if (!method_name.empty()) {
                    std::string callee = concrete_struct_name(lt) + "__" + method_name;
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(new_lhs));
                    args.push_back(std::move(new_rhs));
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, callee, {}, args);
                    break;
                }
            }
            result->mirror_offset_ = lir_mirror_emit_bin_op(
                out_, result->type, op, new_lhs, new_rhs);
            break;
        }
        case C::AddrOfTemp: {
            lir_view::EAddrOfTempView v{eref};
            bool is_mut = v.is_mut();
            auto inner = subst_child_expr(v.inner());
            result->mirror_offset_ = lir_mirror_emit_addr_of_temp(
                out_, result->type, inner, is_mut);
            break;
        }
        case C::EnumLit: {
            lir_view::EEnumLitView v{eref};
            std::string enum_name(v.enum_name());
            std::string variant(v.variant());
            int64_t disc = v.disc();
            TypeRef rt(result->type);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(result->type);
            }
            result->mirror_offset_ = lir_mirror_emit_enum_lit(
                out_, result->type, enum_name, variant, disc);
            break;
        }
        case C::EnumLitData: {
            lir_view::EEnumLitDataView v{eref};
            std::string variant(v.variant());
            int64_t disc = v.disc();
            std::string enum_name;
            TypeRef rt(result->type);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(result->type);
            } else {
                enum_name = std::string(v.enum_name());
            }
            std::vector<lir::LExprPtr> payload;
            v.each_payload([&](lir_view::ExprRef er) {
                payload.push_back(subst_child_expr(er));
            });
            result->mirror_offset_ = lir_mirror_emit_enum_lit_data(
                out_, result->type, enum_name, variant, disc, payload);
            break;
        }
        case C::StructLit: {
            lir_view::EStructLitView v{eref};
            std::string name;
            TypeRef rt2(result->type);
            if (rt2 && (rt2.kind() == LogosType::Kind::Struct ||
                        rt2.kind() == LogosType::Kind::ZonedStruct) &&
                !rt2.type_args().empty())
                name = concrete_struct_name(result->type);
            else
                name = std::string(v.name());
            std::vector<std::pair<std::string, lir::LExprPtr>> fields;
            v.each_field([&](std::string_view fname, lir_view::ExprRef er) {
                fields.push_back({std::string(fname), subst_child_expr(er)});
            });
            record_needed_struct(result->type);
            result->mirror_offset_ = lir_mirror_emit_struct_lit(
                out_, result->type, name, fields);
            break;
        }
        case C::New: {
            lir_view::ENewView v{eref};
            lir::ENew nn;
            nn.class_name = std::string(v.class_name());
            v.each_field([&](std::string_view fname, lir_view::ExprRef er) {
                nn.fields.push_back({std::string(fname), subst_child_expr(er)});
            });
            result->kind = std::move(nn);
            break;
        }
        case C::MatchExpr: {
            lir_view::EMatchExprView v{eref};
            lir::EMatchExpr nm;
            nm.scrut = subst_child_expr(v.scrut());
            PatSubstWalker pw([&](TypeRef t) { return subst_type(t, s); },
                              out_.type_pool.impl());
            v.each_arm([&](lir_view::EMatchArmRef arm) {
                lir::EMatchArm na;
                if (auto pr = arm.pat(); pr) na.pat = pw.walk(pr);
                else                         na.pat = lir::PatWild{};
                if (auto gr = arm.guard(); gr)
                    na.guard = subst_child_expr(gr);
                na.value = subst_child_expr(arm.value());
                nm.arms.push_back(std::move(na));
            });
            result->kind = std::move(nm);
            break;
        }
        case C::TypeCodeOf: {
            auto resolved = subst_type(
                lir_view::ETypeCodeOfView{eref}.elem_type(out_.type_pool.impl()), s);
            bool has_tv = false;
            std::function<void(TypeRef)> walk = [&](TypeRef t) {
                if (!t || has_tv) return;
                if (TypeRef(t).kind() == LogosType::Kind::TypeVar) { has_tv = true; return; }
                for (auto a : TypeRef(t).type_args()) walk(a);
                if (TypeRef(t).pointee()) walk(TypeRef(t).pointee());
                if (TypeRef(t).elem())    walk(TypeRef(t).elem());
            };
            walk(resolved);
            if (has_tv || !resolved) {
                result->mirror_offset_ = lir_mirror_emit_type_code_of(
                    out_, result->type, resolved);
            } else {
                uint64_t code = 0;
                if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                    TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                    std::string mangled = TypeRef(resolved).type_args().empty()
                        ? std::string(TypeRef(resolved).struct_name())
                        : concrete_struct_name(resolved);
                    for (auto& sd : out_.structs)
                        if (sd.name == mangled && sd.type_code != 0)
                            { code = sd.type_code; break; }
                    if (code == 0)
                        for (auto& ia : out_.inst_annotations)
                            if (ia.mangled_name == mangled && ia.type_code != 0)
                                { code = ia.type_code; break; }
                }
                if (code == 0) {
                    auto hash = type_hash_23(type_str(resolved));
                    uint64_t raw = type_hash_56bit(hash);
                    code = (raw < 128) ? (raw + 128) : raw;
                }
                result->mirror_offset_ = lir_mirror_emit_lit_int(
                    out_, result->type, (int64_t)code);
            }
            break;
        }
        case C::HermesLit: {
            lir_view::EHermesLitView v{eref};
            namespace hvc = lir_schema::hermes_val;
            std::function<lir::HermesValPtr(lir_view::HermesValRef)> clone_hv =
                [&](lir_view::HermesValRef vref) -> lir::HermesValPtr {
                auto out = lir::alloc_hermes_val(out_);
                if (!vref) {
                    std::fprintf(stderr,
                        "mono.clone_hv: null HermesValRef\n");
                    std::abort();
                }
                switch (vref.kind()) {
                case hvc::Code::Null:
                    out->mirror_offset_ = lir_mirror_emit_hv_null(out_);
                    break;
                case hvc::Code::Bool:
                    out->mirror_offset_ = lir_mirror_emit_hv_bool(
                        out_, lir_view::HVBoolView{vref}.value());
                    break;
                case hvc::Code::Int:
                    out->mirror_offset_ = lir_mirror_emit_hv_int(
                        out_, lir_view::HVIntView{vref}.value());
                    break;
                case hvc::Code::Float:
                    out->mirror_offset_ = lir_mirror_emit_hv_float(
                        out_, lir_view::HVFloatView{vref}.value());
                    break;
                case hvc::Code::Str: {
                    std::string s(lir_view::HVStrView{vref}.value());
                    out->mirror_offset_ = lir_mirror_emit_hv_str(out_, s);
                    break;
                }
                case hvc::Code::Capture: {
                    lir_view::HVCaptureView cv{vref};
                    out->mirror_offset_ = lir_mirror_emit_hv_capture(
                        out_, cv.param_index(), cv.value_index());
                    break;
                }
                case hvc::Code::Map: {
                    lir_view::HVMapView mv{vref};
                    std::string key_type(mv.key_type());
                    bool int_keys = mv.int_keyed();
                    std::vector<lir::HVMapEntry> entries;
                    entries.reserve(mv.size());
                    for (uint64_t i = 0; i < mv.size(); ++i) {
                        lir::HVMapEntry ent;
                        if (int_keys) ent.key = mv.int_key(i);
                        else          ent.key = std::string(mv.str_key(i));
                        ent.val = clone_hv(mv.value(i));
                        entries.push_back(std::move(ent));
                    }
                    out->mirror_offset_ = lir_mirror_emit_hv_map(
                        out_, entries, key_type);
                    break;
                }
                case hvc::Code::Array: {
                    lir_view::HVArrayView av{vref};
                    std::string elem_type(av.elem_type());
                    std::vector<lir::HermesValPtr> elements;
                    elements.reserve(av.size());
                    for (uint64_t i = 0; i < av.size(); ++i)
                        elements.push_back(clone_hv(av.elem(i)));
                    out->mirror_offset_ = lir_mirror_emit_hv_array(
                        out_, elements, elem_type);
                    break;
                }
                }
                return out;
            };
            lir::HermesValPtr root = nullptr;
            if (auto root_ref = v.root()) root = clone_hv(root_ref);
            bool has_captures = v.has_captures();
            uint32_t capture_param_count = v.capture_param_count();
            std::vector<lir::LExprPtr> capture_exprs;
            std::vector<TypeRef> capture_types;
            v.each_capture_expr([&](lir_view::ExprRef er) {
                capture_exprs.push_back(subst_child_expr(er));
            });
            v.each_capture_type(out_.type_pool.impl(),
                [&](TypeRef ct) { capture_types.push_back(subst_type(ct, s)); });
            result->mirror_offset_ = lir_mirror_emit_hermes_lit(
                out_, result->type, root, has_captures,
                capture_exprs, capture_types, capture_param_count);
            break;
        }
        case C::Call: {
            lir_view::ECallView v{eref};
            lir::ECall nc;
            nc.callee = std::string(v.callee());
            for (auto ta : v.type_args(out_.type_pool.impl()))
                nc.type_args.push_back(subst_type(ta, s));
            v.each_arg([&](lir_view::ExprRef ar) {
                if (ar && ar.kind() == lir_schema::expr::Code::PackExpand) {
                    std::string pe_var_name(lir_view::EPackExpandView{ar}.var_name());
                    std::string pack_name;
                    TypeRef at = ar.type(out_.type_pool.impl());
                    if (at && at.kind() == LogosType::Kind::TypeVar)
                        pack_name = std::string(at.type_var_name());
                    auto pit = cur_packs_.find(pack_name);
                    if (pit != cur_packs_.end()) {
                        for (size_t pi = 0; pi < pit->second.size(); ++pi) {
                            nc.args.push_back(LirBuilder(out_).var_ref(
                                make_pack_arg_name(pe_var_name, pi),
                                pit->second[pi]));
                        }
                        if (templates_.count(nc.callee)) {
                            for (auto pt : pit->second)
                                nc.type_args.push_back(pt);
                        }
                    }
                } else {
                    nc.args.push_back(subst_child_expr(ar));
                }
            });
            // Generic static-trait-dispatch: rewrite "DT__method" prefix when
            // DT is bound by the substitution map.
            {
                auto sep = nc.callee.find("__");
                if (sep != std::string::npos) {
                    std::string prefix = nc.callee.substr(0, sep);
                    auto it = s.find(prefix);
                    if (it != s.end() && it->second) {
                        std::string cname;
                        TypeRef t = it->second;
                        if (TypeRef(t).kind() == LogosType::Kind::Struct)
                            cname = concrete_struct_name(t);
                        else
                            cname = type_str(t);
                        if (cname == "&[u8]") cname = "str";
                        if (!cname.empty())
                            nc.callee = cname + nc.callee.substr(sep);
                    }
                }
            }
            // Rewrite callee if it's a generic call already instantiated.
            if (!nc.type_args.empty()) {
                bool rewritten_as_struct_method = false;
                auto sep = nc.callee.find("__");
                if (sep != std::string::npos) {
                    std::string struct_part = nc.callee.substr(0, sep);
                    std::string method_part = nc.callee.substr(sep);
                    auto sit = struct_templates_.find(struct_part);
                    if (sit != struct_templates_.end()) {
                        bool all_concrete = true;
                        for (auto ta : nc.type_args)
                            if (ta && TypeRef(ta).kind() == LogosType::Kind::TypeVar)
                                { all_concrete = false; break; }
                        if (all_concrete) {
                            size_t n_impl_tp = sit->second->type_params.size();
                            size_t n_args    = std::min(n_impl_tp, nc.type_args.size());
                            std::vector<TypeRef> args(
                                nc.type_args.begin(), nc.type_args.begin() + n_args);
                            std::string cname = concrete_struct_name_raw(struct_part, args);
                            nc.callee = cname + method_part;
                            nc.type_args.clear();
                            rewritten_as_struct_method = true;
                        }
                    }
                }
                if (!rewritten_as_struct_method)
                    nc.callee = mangle(nc.callee, nc.type_args);
            }
            result->mirror_offset_ = lir_mirror_emit_call(
                out_, result->type, nc.callee, nc.type_args, nc.args);
            break;
        }
        case C::MethodCall: {
            lir_view::EMethodCallView v{eref};
            auto recv_ref = v.receiver();
            auto orig_recv_type = recv_ref.type(out_.type_pool.impl());
            auto new_recv = subst_child_expr(recv_ref);
            std::string method{v.method()};
            std::string resolved_symbol{v.resolved_symbol()};
            std::string resolved_type{v.resolved_type()};
            std::string tag_system{v.tag_system()};
            std::string tag_trait{v.tag_trait()};
            int32_t vtable_index = v.vtable_index();
            // Unwrap pointer/reference for TypeVar check.
            auto orig_inner = orig_recv_type;
            if (orig_inner && (TypeRef(orig_inner).kind() == LogosType::Kind::Ptr ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::Ref ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::MutRef) &&
                TypeRef(orig_inner).pointee())
                orig_inner = TypeRef(orig_inner).pointee();
            if (orig_inner && TypeRef(orig_inner).kind() == LogosType::Kind::TypeVar &&
                new_recv && new_recv->type) {
                std::string cname;
                auto rt = new_recv->type;
                if (TypeRef(rt).kind() == LogosType::Kind::Struct ||
                    TypeRef(rt).kind() == LogosType::Kind::ZonedStruct)
                    cname = concrete_struct_name(rt);
                else if ((TypeRef(rt).kind() == LogosType::Kind::Ptr ||
                          TypeRef(rt).kind() == LogosType::Kind::Ref ||
                          TypeRef(rt).kind() == LogosType::Kind::MutRef) && TypeRef(rt).pointee()) {
                    if (TypeRef(rt).pointee().kind() == LogosType::Kind::Struct ||
                        TypeRef(rt).pointee().kind() == LogosType::Kind::ZonedStruct)
                        cname = concrete_struct_name(TypeRef(rt).pointee());
                    else {
                        std::string ptr_cname = type_str(rt);
                        std::string ptr_fn = ptr_cname + "__" + method;
                        bool ptr_exists = templates_.count(ptr_fn) || specs_.count(ptr_fn);
                        if (!ptr_exists)
                            for (auto& f : in_.functions)
                                if (f->name == ptr_fn) { ptr_exists = true; break; }
                        if (!ptr_exists)
                            for (auto& f : out_.functions)
                                if (f->name == ptr_fn) { ptr_exists = true; break; }
                        cname = ptr_exists ? ptr_cname : type_str(TypeRef(rt).pointee());
                    }
                }
                if (cname.empty()) cname = type_str(rt);
                if (cname == "&[u8]") cname = "str";
                if (!cname.empty()) {
                    lir::ECall nc;
                    std::string base_fn = cname + "__" + method;
                    std::string tmpl_key = base_fn;
                    if (!templates_.count(tmpl_key) && !specs_.count(tmpl_key)) {
                        std::string p = base_fn + "__g__";
                        for (auto& [kn, _] : templates_)
                            if (kn.rfind(p, 0) == 0) { tmpl_key = kn; break; }
                        if (tmpl_key == base_fn)
                            for (auto& [kn, _] : specs_)
                                if (kn.rfind(p, 0) == 0) { tmpl_key = kn; break; }
                    }
                    nc.callee = tmpl_key;
                    nc.args.push_back(std::move(new_recv));
                    v.each_arg([&](lir_view::ExprRef ar) {
                        nc.args.push_back(subst_child_expr(ar));
                    });
                    for (auto ta : v.type_args(out_.type_pool.impl()))
                        nc.type_args.push_back(subst_type(ta, s));
                    if (!nc.type_args.empty())
                        nc.callee = mangle(tmpl_key, nc.type_args);
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, nc.callee, nc.type_args, nc.args);
                    break;
                }
                // Fallback: keep as method call
                std::vector<lir::LExprPtr> mc_args;
                v.each_arg([&](lir_view::ExprRef ar) {
                    mc_args.push_back(subst_child_expr(ar));
                });
                result->mirror_offset_ = lir_mirror_emit_method_call(
                    out_, result->type, new_recv, method, resolved_symbol,
                    {}, mc_args, vtable_index, resolved_type,
                    tag_system, tag_trait);
                break;
            }
            // Non-trait-method-on-TypeVar path.
            lir::EMethodCall nm;
            nm.receiver = std::move(new_recv);
            nm.method = method;
            nm.resolved_symbol = resolved_symbol;
            for (auto ta : v.type_args(out_.type_pool.impl()))
                nm.type_args.push_back(subst_type(ta, s));
            nm.vtable_index = vtable_index;
            nm.tag_system = tag_system;
            nm.tag_trait  = tag_trait;
            // SPECIALIZATION LOOKUP (Bug 12)
            bool rewritten = false;
            if (nm.receiver && nm.receiver->type) {
                TypeRef rt = nm.receiver->type;
                while (rt && (TypeRef(rt).kind() == LogosType::Kind::Ptr ||
                              TypeRef(rt).kind() == LogosType::Kind::Ref ||
                              TypeRef(rt).kind() == LogosType::Kind::MutRef) && TypeRef(rt).pointee()) {
                    rt = TypeRef(rt).pointee();
                }
                if (rt &&
                    (TypeRef(rt).kind() == LogosType::Kind::Struct ||
                     TypeRef(rt).kind() == LogosType::Kind::ZonedStruct ||
                     TypeRef(rt).kind() == LogosType::Kind::Enum)) {
                    std::vector<TypeRef> combined_args = TypeRef(rt).type_args();
                    for (auto mta : nm.type_args) combined_args.push_back(mta);
                    std::string base_struct;
                    if (!resolved_type.empty()) {
                        base_struct = resolved_type;
                    } else if (TypeRef(rt).kind() == LogosType::Kind::Enum) {
                        base_struct = TypeRef(rt).enum_name();
                    } else {
                        base_struct = TypeRef(rt).struct_name();
                    }
                    std::string base_name = base_struct + "__" + method;
                    auto pick_mono_template_key = [&]() -> std::string {
                        if (templates_.count(base_name) || specs_.count(base_name))
                            return base_name;
                        std::string p = base_name + "__";
                        for (auto& [kname, _] : templates_)
                            if (kname.rfind(p, 0) == 0) return kname;
                        for (auto& [kname, _] : specs_)
                            if (kname.rfind(p, 0) == 0) return kname;
                        return {};
                    };
                    std::string mono_base = pick_mono_template_key();
                    if (auto* spec = find_best_spec(mono_base.empty() ? base_name : mono_base,
                                                    combined_args)) {
                        std::vector<lir::LExprPtr> args;
                        args.push_back(std::move(nm.receiver));
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        result->mirror_offset_ = lir_mirror_emit_call(
                            out_, result->type, spec->name, {}, args);
                        rewritten = true;
                    } else if (!combined_args.empty() && !mono_base.empty()) {
                        std::string callee = mangle(mono_base, combined_args);
                        std::vector<lir::LExprPtr> args;
                        args.push_back(std::move(nm.receiver));
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        result->mirror_offset_ = lir_mirror_emit_call(
                            out_, result->type, callee, combined_args, args);
                        rewritten = true;
                    }
                }
            }
            if (!rewritten) {
                v.each_arg([&](lir_view::ExprRef ar) {
                    nm.args.push_back(subst_child_expr(ar));
                });
                result->mirror_offset_ = lir_mirror_emit_method_call(
                    out_, result->type, nm.receiver, nm.method, nm.resolved_symbol,
                    nm.type_args, nm.args, nm.vtable_index, resolved_type,
                    nm.tag_system, nm.tag_trait);
            }
            break;
        }
        case C::ClosureBox: {
            lir_view::EClosureBoxView v{eref};
            auto br = v.body();
            if (!br) {
                result->kind = lir::EClosureBox{nullptr};
                break;
            }
            auto nc = lir::alloc_closure(out_);
            nc->closure_id = std::string(v.closure_id());
            v.each_param(out_.type_pool.impl(),
                [&](std::string_view nm, TypeRef pt) {
                    nc->params.push_back({std::string(nm), subst_type(pt, s)});
                });
            nc->ret_type  = subst_type(v.ret_type(out_.type_pool.impl()), s);
            nc->body      = subst_child_block(br);
            lir_mirror_emit_block_node(out_, nc->body);
            nc->is_move   = v.is_move();
            nc->as_fn_ptr = v.as_fn_ptr();
            v.each_capture_name([&](std::string_view cn) {
                nc->captures.push_back(std::string(cn));
            });
            v.each_capture(out_.type_pool.impl(),
                [&](std::string_view, TypeRef ct) {
                    nc->capture_types.push_back(subst_type(ct, s));
                });
            result->kind = lir::EClosureBox{std::move(nc)};
            break;
        }
        default:
            std::fprintf(stderr,
                "mono.subst_expr: unhandled expr Code=%d\n",
                int(eref.kind()));
            std::abort();
        }
    }

    lir_mirror_emit_expr_node(out_, *result);
    return result;
}


// View-based pattern subst: walks input via PatRef (mirror-tracked), builds
// new lir::Pattern variants from view fields. M1-M5 fixes preserved.
lir::Pattern PatSubstWalker::walk(lir_view::PatRef pref) const {
    namespace pc = lir_schema::pat;
    if (!pref) return lir::PatWild{};  // defensive: no mirror entry
    switch (pref.kind()) {
    case pc::Code::Variant: {
        lir_view::PatVariantView v{pref};
        return lir::PatVariant{std::string(v.enum_name()),
                               std::string(v.variant()), v.disc()};
    }
    case pc::Code::Int:
        return lir::PatInt{lir_view::PatIntView{pref}.value()};
    case pc::Code::Bool:
        return lir::PatBool{lir_view::PatBoolView{pref}.value()};
    case pc::Code::Wild:
        return lir::PatWild{std::string(lir_view::PatWildView{pref}.name())};
    case pc::Code::Range: {
        lir_view::PatRangeView v{pref};
        return lir::PatRange{v.lo(), v.hi()};
    }
    case pc::Code::VariantData: {
        lir_view::PatVariantDataView v{pref};
        lir::PatVariantData n;
        n.enum_name = std::string(v.enum_name());
        n.variant   = std::string(v.variant());
        n.disc      = v.disc();
        v.each_binding([&](std::string_view s) { n.bindings.emplace_back(s); });
        v.each_binding_type(pool_, [&](TypeRef t) { n.binding_types.push_back(st_(t)); });
        return n;
    }
    case pc::Code::Or: {
        lir::PatOr n;
        lir_view::PatOrView{pref}.each_alt(
            [&](lir_view::PatRef alt) { n.alts.push_back(walk(alt)); });
        return n;
    }
    case pc::Code::Tuple: {
        lir_view::PatTupleView v{pref};
        lir::PatTuple n;
        v.each_binding([&](std::string_view s) { n.bindings.emplace_back(s); });
        v.each_binding_type(pool_, [&](TypeRef t) { n.binding_types.push_back(st_(t)); });
        v.each_sub([&](lir_view::PatRef sp) { n.subs.push_back(walk(sp)); });
        return n;
    }
    case pc::Code::Struct: {
        lir_view::PatStructView v{pref};
        lir::PatStruct n;
        n.struct_name = std::string(v.struct_name());
        n.has_rest    = v.has_rest();
        v.each_field([&](lir_view::PatFieldBindingView fbv) {
            lir::PatFieldBinding pfb;
            pfb.field_name = std::string(fbv.field_name());
            if (auto sub = fbv.sub()) pfb.sub.push_back(walk(sub));
            n.fields.push_back(std::move(pfb));
        });
        return n;
    }
    case pc::Code::Slice: {
        lir_view::PatSliceView v{pref};
        lir::PatSlice n;
        v.each_prefix([&](lir_view::PatRef p) { n.prefix.push_back(walk(p)); });
        if (auto r = v.rest()) n.rest.push_back(walk(r));
        v.each_suffix([&](lir_view::PatRef p) { n.suffix.push_back(walk(p)); });
        return n;
    }
    case pc::Code::At: {
        lir_view::PatAtView v{pref};
        lir::PatAt n;
        n.name = std::string(v.name());
        n.type = st_(v.type(pool_));
        if (auto sub = v.sub()) n.sub.push_back(walk(sub));
        return n;
    }
    case pc::Code::RefBind: {
        lir_view::PatRefBindView v{pref};
        lir::PatRefBind n;
        n.name      = std::string(v.name());
        n.is_mut    = v.is_mut();
        n.bind_type = st_(v.bind_type(pool_));
        return n;
    }
    case pc::Code::RefPat: {
        lir_view::PatRefPatView v{pref};
        lir::PatRefPat n;
        n.is_mut = v.is_mut();
        if (auto inner = v.inner()) n.inner.push_back(walk(inner));
        return n;
    }
    }
    return lir::PatWild{};
}

lir::LStmt Mono::subst_stmt(const lir::LStmt& st, const SubstMap& s) {
    // Returns LStmt with mirror_offset_=0 by design. Caller (instantiate_fn /
    // clone_struct_def) bulk-emits via lir_mirror_emit_function only after the
    // owning LFunction reaches a heap-stable address (unique_ptr) and its
    // body's stmt-vector buffers stop reallocating. See the call sites in
    // the worklist drain and per-method emit loop for the full rationale.
    lir::LStmt ns;
    ns.line = st.line;

    auto sref = stmt_ref_of(st);
    if (!sref) return ns;  // mirror miss — defensive

    auto subst_child_expr = [&](lir_view::ExprRef er) -> lir::LExprPtr {
        auto* le = lexpr_of(er);
        return le ? subst_expr(*le, s) : nullptr;
    };
    auto subst_child_block = [&](lir_view::BlockRef br) -> lir::LBlock {
        auto* lb = lblock_of(br);
        return lb ? subst_block(*lb, s) : lir::LBlock{};
    };

    using SCode = lir_schema::stmt::Code;
    const TypePoolImpl* pool = out_.type_pool.impl();

    switch (sref.kind()) {
    case SCode::Let: {
        lir_view::SLetView v{sref};
        std::string name(v.name());
        TypeRef ty = subst_type(v.type(pool), s);
        bool is_mut = v.is_mut();
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_let(
            out_, ns.line, name, ty, value, is_mut);
        break;
    }
    case SCode::Assign: {
        lir_view::SAssignView v{sref};
        std::string name(v.name());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_assign(
            out_, ns.line, name, value);
        break;
    }
    case SCode::Return: {
        auto val = lir_view::SReturnView{sref}.value();
        auto value = val ? subst_child_expr(val) : nullptr;
        ns.mirror_offset_ = lir_mirror_emit_return(
            out_, ns.line, value);
        break;
    }
    case SCode::If: {
        lir_view::SIfView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto* then_blk = lir::alloc_block(out_, subst_child_block(v.then_block()));
        lir_mirror_emit_block_node(out_, *then_blk);
        lir::LBlock* else_blk = nullptr;
        if (auto eb = v.else_block()) {
            else_blk = lir::alloc_block(out_, subst_child_block(eb));
            lir_mirror_emit_block_node(out_, *else_blk);
        }
        ns.mirror_offset_ = lir_mirror_emit_if_stmt(
            out_, ns.line, cond, then_blk, else_blk);
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_while(
            out_, ns.line, cond, body, label);
        break;
    }
    case SCode::For: {
        lir_view::SForView v{sref};
        std::string var(v.var());
        auto lo = subst_child_expr(v.lo());
        auto hi = subst_child_expr(v.hi());
        bool inclusive = v.inclusive();
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_for(
            out_, ns.line, var, lo, hi, inclusive, body, label);
        break;
    }
    case SCode::Loop: {
        lir_view::SLoopView v{sref};
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        TypeRef result_type = v.result_type(pool);
        std::string break_slot(v.break_slot());
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_loop(
            out_, ns.line, body, label, break_slot, result_type);
        break;
    }
    case SCode::Block: {
        lir_view::SBlockView v{sref};
        auto* blk = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *blk);
        ns.mirror_offset_ = lir_mirror_emit_block_stmt(
            out_, ns.line, blk);
        break;
    }
    case SCode::Break: {
        lir_view::SBreakView v{sref};
        lir::LExprPtr value = nullptr;
        if (auto val = v.value()) value = subst_child_expr(val);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_break(
            out_, ns.line, value, label);
        break;
    }
    case SCode::Continue: {
        std::string label(lir_view::SContinueView{sref}.label());
        ns.mirror_offset_ = lir_mirror_emit_continue(
            out_, ns.line, label);
        break;
    }
    case SCode::FieldWrite: {
        lir_view::SFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_field_write(
            out_, ns.line, receiver, field, value);
        break;
    }
    case SCode::ChainFieldWrite: {
        lir_view::SChainFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string mid_field(v.mid_field());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_chain_field_write(
            out_, ns.line, receiver, mid_field, field, value);
        break;
    }
    case SCode::DerefFieldWrite: {
        lir_view::SDerefFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string type_name(v.type_name());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_deref_field_write(
            out_, ns.line, receiver, type_name, field, value);
        break;
    }
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{sref};
        std::string arr(v.arr());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_index_write(
            out_, ns.line, arr, idx, value);
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_field_index_write(
            out_, ns.line, receiver, field, idx, value);
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{sref};
        auto ptr = subst_child_expr(v.ptr());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_deref_write(
            out_, ns.line, ptr, value);
        break;
    }
    case SCode::TupleWrite: {
        lir_view::STupleWriteView v{sref};
        std::string receiver(v.receiver());
        uint32_t index = v.index();
        auto value = subst_child_expr(v.value());
        TypeRef recv_type = v.recv_type(pool);
        ns.mirror_offset_ = lir_mirror_emit_tuple_write(
            out_, ns.line, receiver, index, value, recv_type);
        break;
    }
    case SCode::ExprStmt: {
        auto expr = subst_child_expr(lir_view::SExprStmtView{sref}.expr());
        ns.mirror_offset_ = lir_mirror_emit_expr_stmt(
            out_, ns.line, expr);
        break;
    }
    case SCode::Delete: {
        auto expr = subst_child_expr(lir_view::SDeleteView{sref}.expr());
        ns.mirror_offset_ = lir_mirror_emit_delete(
            out_, ns.line, expr);
        break;
    }
    case SCode::Drop: {
        lir_view::SDropView v{sref};
        std::string var_name(v.var_name());
        std::string drop_fn(v.drop_fn());
        TypeRef ty = subst_type(v.type(pool), s);
        bool drop_fields = v.drop_fields();
        ns.mirror_offset_ = lir_mirror_emit_drop(
            out_, ns.line, var_name, drop_fn, ty, drop_fields);
        break;
    }
    case SCode::Match: {
        lir_view::SMatchView v{sref};
        auto scrut = subst_child_expr(v.scrut());
        std::vector<lir::LMatchArm> arms;
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            lir::LMatchArm na;
            if (auto pref = arm.pat()) na.pat = subst_pattern(pref, s);
            na.body = lir::alloc_block(out_, subst_child_block(arm.body()));
            lir_mirror_emit_block_node(out_, *na.body);
            if (auto g = arm.guard()) na.guard = subst_child_expr(g);
            arms.push_back(std::move(na));
        });
        ns.mirror_offset_ = lir_mirror_emit_match_stmt(
            out_, ns.line, scrut, arms);
        break;
    }
    case SCode::ForEach: {
        lir_view::SForEachView v{sref};
        std::string var(v.var());
        auto iter = subst_child_expr(v.iter());
        TypeRef elem_type = subst_type(v.elem_type(pool), s);
        int64_t arr_size = v.arr_size();
        bool is_slice = v.is_slice();
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        ns.mirror_offset_ = lir_mirror_emit_for_each(
            out_, ns.line, var, iter, elem_type, arr_size, is_slice, body);
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{sref};
        lir::Pattern pat;
        if (auto pref = v.pat()) pat = subst_pattern(pref, s);
        auto scrut = subst_child_expr(v.scrut());
        auto* else_block = lir::alloc_block(out_, subst_child_block(v.else_block()));
        lir_mirror_emit_block_node(out_, *else_block);
        ns.mirror_offset_ = lir_mirror_emit_let_else(
            out_, ns.line, pat, scrut, else_block);
        break;
    }
    default: break;
    }

    lir_mirror_emit_stmt_node(out_, ns);
    return ns;
}


// ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

lir::LFunction Mono::clone_fn(const lir::LFunction& fn, const SubstMap& s,
                         const PackMap& packs) {
    cur_packs_ = packs;  // make available to subst_expr
    lir::LFunction nf;
    nf.name               = fn.name;
    nf.is_extern          = fn.is_extern;
    nf.is_vararg          = fn.is_vararg;
    // Never propagate from_binary_module to cloned functions: clone_fn is
    // called by mono to create instantiations, which are new functions not
    // present in the binary archive. The archive contains only the pre-compiled
    // non-generic originals (identified via LProgram::binary_symbols in mlir_gen).
    nf.ret_type  = subst_type(fn.ret_type, s);
    for (auto& p : fn.params) {
        if (p.is_variadic) {
            // Expand variadic param into N concrete params.
            // Find the pack type for this param's TypeVar name.
            std::string pack_name;
            TypeRef pt(p.type);
            if (pt && pt.kind() == LogosType::Kind::TypeVar)
                pack_name = std::string(pt.type_var_name());
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
    nd.is_zoned = tmpl.is_zoned;
    nd.meta_val    = tmpl.meta_val;
    // type_params cleared: result is monomorphic
    for (auto& f : tmpl.fields) {
        if (f.is_variadic) {
            std::string pack_name;
            TypeRef ft(f.type);
            if (ft && ft.kind() == LogosType::Kind::TypeVar)
                pack_name = std::string(ft.type_var_name());
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
    for (auto& m_up : tmpl.methods) {
        auto& m = *m_up;
        // Bound gate: if this method came from `impl<T: Bound> Trait for S<T>`,
        // skip cloning when the concrete type substituted for T doesn't satisfy
        // Bound.  Without this gate, methods get cloned with bodies that call
        // functions (e.g. `T::clone_to`) that don't exist for unsupported Ts,
        // producing dangling references at mlir-gen time.
        bool bound_ok = true;
        for (auto& itp : m.impl_type_params) {
            if (itp.bounds.empty()) continue;
            auto sit = s.find(itp.name);
            if (sit == s.end()) continue;  // TypeVar not substituted — keep.
            TypeRef concrete = sit->second;
            if (!concrete) continue;
            std::string cname;
            if (TypeRef(concrete).kind() == LogosType::Kind::Struct ||
                TypeRef(concrete).kind() == LogosType::Kind::ZonedStruct)
                cname = concrete_struct_name(concrete);
            else if (TypeRef(concrete).kind() == LogosType::Kind::Enum)
                cname = TypeRef(concrete).enum_name();
            else
                cname = type_str(concrete);
            // Strip instantiation suffix — impls are keyed on base name.
            if (auto p = cname.find("$G"); p != std::string::npos)
                cname = cname.substr(0, p);
            // Recursive satisfaction: C impls T if a concrete impl exists,
            // or if any blanket `impl<U: B> T for U` exists and C impls B.
            // Cycle-guarded via `seen`.
            std::function<bool(const std::string&, const std::string&,
                               StrSet&)> has_impl;
            has_impl = [&](const std::string& trait, const std::string& cn,
                           StrSet& seen) -> bool {
                std::string k = trait + "::" + cn;
                if (!seen.insert(k).second) return false;
                if (concrete_impls_.count(k)) return true;
                for (auto& bi : blanket_impls_) {
                    if (bi.trait_name != trait) continue;
                    if (bi.bound_trait.empty()) return true;
                    if (has_impl(bi.bound_trait, cn, seen)) return true;
                }
                return false;
            };
            for (auto& tb : itp.bounds) {
                StrSet seen;
                if (!has_impl(tb.trait_name, cname, seen)) { bound_ok = false; break; }
            }
            if (!bound_ok) break;
        }
        if (!bound_ok) continue;
        auto nm = clone_fn(m, s, packs);
        // Rename method: "OldBase__methodName" → "new_name__methodName".
        // If the template method name carries a generic suffix
        // ("OldBase__method__g__T"), strip it for the instantiated struct
        // method. The concrete struct name already captures the instantiation.
        auto sep = m.name.find("__");
        if (sep != std::string::npos)
            nm.name = new_name + "__" + m.name.substr(sep + 2, m.name.find("__", sep + 2) == std::string::npos
                ? std::string::npos
                : m.name.find("__", sep + 2) - (sep + 2));
        // Specialization: if the user wrote `impl Foo<Concrete> { fn m ... }`
        // separately from `impl<T> Foo<T> { fn m ... }`, the concrete method
        // lives in in_.functions under the mangled name.  Skip cloning the
        // blanket version for this concrete — the free-fn path will emit it
        // with the correct body.
        bool overridden = false;
        for (auto& fn : in_.functions) {
            if (!fn->type_params.empty()) continue;
            if (fn->name == nm.name) { overridden = true; break; }
        }
        if (overridden) continue;
        // Substitute struct type in params/ret as needed (already done by clone_fn).
        nd.methods.push_back(std::make_unique<lir::LFunction>(std::move(nm)));
        lir_mirror_emit_function(out_, *out_.mirror_table, *nd.methods.back());
    }
    return nd;
}


// Return the best-matching struct specialisation for (base_name, type_args).
const lir::LStructDef* Mono::find_best_struct_spec(
    const std::string& base_name,
    const std::vector<TypeRef>& type_args) {
    auto sit = struct_specs_.find(base_name);
    if (sit == struct_specs_.end()) return nullptr;

    const lir::LStructDef* best       = nullptr;
    std::vector<int>       best_vec;
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
        auto svec = specificity_vec(spec->spec_patterns);
        if (!best || svec > best_vec) {
            best_vec  = svec;
            best      = spec;
            ambiguous = false;
        } else if (svec == best_vec) {
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
    auto& arena = out_.type_pool.arena_or_init();
    for (auto& fn : out_.functions) {
        collect_type_for_structs(fn->ret_type);
        for (auto& p : fn->params) collect_type_for_structs(p.type);
        if (fn->body.mirror_offset_ != hermes::arena_offset_t{})
            collect_struct_needs_from_block(
                lir_view::BlockRef(&arena, fn->body.mirror_offset_));
    }
    // Also walk already-instantiated structs (field types may reference more).
    for (auto& sd : out_.structs)
        for (auto& f : sd.fields) collect_type_for_structs(f.type);
}


void Mono::collect_struct_needs_from_block(lir_view::BlockRef b) {
    if (!b) return;
    b.each_stmt([&](lir_view::StmtRef s) { collect_struct_needs_from_stmt(s); });
}


void Mono::collect_struct_needs_from_stmt(lir_view::StmtRef s) {
    if (!s) return;
    using SCode = lir_schema::stmt::Code;
    const TypePoolImpl* pool = out_.type_pool.impl();
    switch (s.kind()) {
    case SCode::Let: {
        lir_view::SLetView v{s};
        collect_type_for_structs(v.type(pool));
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::Assign:
        collect_struct_needs_from_expr(lir_view::SAssignView{s}.value());
        break;
    case SCode::Return:
        if (auto v = lir_view::SReturnView{s}.value()) collect_struct_needs_from_expr(v);
        break;
    case SCode::If: {
        lir_view::SIfView v{s};
        collect_struct_needs_from_expr(v.cond());
        collect_struct_needs_from_block(v.then_block());
        collect_struct_needs_from_block(v.else_block());
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{s};
        collect_struct_needs_from_expr(v.cond());
        collect_struct_needs_from_block(v.body());
        break;
    }
    case SCode::For:
        collect_struct_needs_from_block(lir_view::SForView{s}.body());
        break;
    case SCode::Loop:
        collect_struct_needs_from_block(lir_view::SLoopView{s}.body());
        break;
    case SCode::Block:
        collect_struct_needs_from_block(lir_view::SBlockView{s}.body());
        break;
    case SCode::FieldWrite:
        collect_struct_needs_from_expr(lir_view::SFieldWriteView{s}.value());
        break;
    case SCode::ChainFieldWrite:
        collect_struct_needs_from_expr(lir_view::SChainFieldWriteView{s}.value());
        break;
    case SCode::DerefFieldWrite:
        collect_struct_needs_from_expr(lir_view::SDerefFieldWriteView{s}.value());
        break;
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{s};
        collect_struct_needs_from_expr(v.index());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{s};
        collect_struct_needs_from_expr(v.index());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{s};
        collect_struct_needs_from_expr(v.ptr());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::TupleWrite:
        collect_struct_needs_from_expr(lir_view::STupleWriteView{s}.value());
        break;
    case SCode::ExprStmt:
        collect_struct_needs_from_expr(lir_view::SExprStmtView{s}.expr());
        break;
    case SCode::Delete:
        collect_struct_needs_from_expr(lir_view::SDeleteView{s}.expr());
        break;
    case SCode::Drop:
        break;
    case SCode::Match: {
        lir_view::SMatchView v{s};
        collect_struct_needs_from_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) collect_struct_needs_from_expr(g);
            collect_struct_needs_from_block(arm.body());
        });
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{s};
        collect_struct_needs_from_expr(v.scrut());
        collect_struct_needs_from_block(v.else_block());
        break;
    }
    default: break;
    }
}


void Mono::collect_struct_needs_from_expr(lir_view::ExprRef e) {
    if (!e) return;
    const TypePoolImpl* pool = out_.type_pool.impl();
    collect_type_for_structs(e.type(pool));
    using ECode = lir_schema::expr::Code;
    switch (e.kind()) {
    case ECode::Call:
        lir_view::ECallView{e}.each_arg(
            [&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    case ECode::MethodCall: {
        lir_view::EMethodCallView v{e};
        collect_struct_needs_from_expr(v.receiver());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::BinOp: {
        lir_view::EBinOpView v{e};
        collect_struct_needs_from_expr(v.lhs());
        collect_struct_needs_from_expr(v.rhs());
        break;
    }
    case ECode::Unary:
        collect_struct_needs_from_expr(lir_view::EUnaryView{e}.operand());
        break;
    case ECode::Deref:
        collect_struct_needs_from_expr(lir_view::EDerefView{e}.operand());
        break;
    case ECode::FieldRead:
        collect_struct_needs_from_expr(lir_view::EFieldReadView{e}.receiver());
        break;
    case ECode::IndexRead: {
        lir_view::EIndexReadView v{e};
        collect_struct_needs_from_expr(v.receiver());
        collect_struct_needs_from_expr(v.index());
        break;
    }
    case ECode::StructLit:
        lir_view::EStructLitView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { collect_struct_needs_from_expr(fv); });
        break;
    case ECode::ArrLit:
        lir_view::EArrLitView{e}.each_elem(
            [&](lir_view::ExprRef el) { collect_struct_needs_from_expr(el); });
        break;
    case ECode::Cast:
        collect_struct_needs_from_expr(lir_view::ECastView{e}.operand());
        break;
    case ECode::New:
        lir_view::ENewView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { collect_struct_needs_from_expr(fv); });
        break;
    case ECode::FormatCall: {
        lir_view::EFormatCallView v{e};
        collect_struct_needs_from_expr(v.fmt());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::PackExpand:
        break;
    case ECode::MatchExpr: {
        lir_view::EMatchExprView v{e};
        collect_struct_needs_from_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) collect_struct_needs_from_expr(g);
            collect_struct_needs_from_expr(arm.value());
        });
        break;
    }
    case ECode::BlockExpr: {
        lir_view::EBlockExprView v{e};
        if (auto blk = v.block()) collect_struct_needs_from_block(blk);
        if (auto r = v.result()) collect_struct_needs_from_expr(r);
        break;
    }
    default: break;
    }
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

            TypeRef struct_t = info.first;
            depth_ = info.second;

            const std::string base{TypeRef(struct_t).struct_name()};
            SubstMap subst;

            const lir::LStructDef* tmpl = nullptr;
            PackMap packs;
            if (auto* spec = find_best_struct_spec(base, TypeRef(struct_t).type_args())) {
                for (size_t i = 0; i < spec->spec_patterns.size() &&
                                   i < TypeRef(struct_t).type_args().size(); ++i)
                    match_type(TypeRef(struct_t).type_args()[i], spec->spec_patterns[i], subst);
                tmpl = spec;
            } else {
                auto it = struct_templates_.find(base);
                if (it == struct_templates_.end()) continue;
                tmpl = it->second;
                for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                    if (tmpl->type_params[i].is_variadic) {
                        std::vector<TypeRef> pack;
                        while (j < TypeRef(struct_t).type_args().size()) pack.push_back(TypeRef(struct_t).type_args()[j++]);
                        packs[tmpl->type_params[i].name] = std::move(pack);
                    } else if (j < TypeRef(struct_t).type_args().size()) {
                        subst[tmpl->type_params[i].name] = TypeRef(struct_t).type_args()[j++];
                    }
                }
            }

            auto inst = clone_struct_def(*tmpl, subst, packs, cname);
            // Mirror emit happens here — *after* clone, *before* scan_fn — because
            // only at this point are stmt/expr addresses stable. Two conditions:
            //   (a) all recursive subst_block push_backs are done, so the
            //       LBlock::stmts vector buffers won't reallocate again and
            //       LStmt addresses inside them are fixed;
            //   (b) each LFunction sits behind a unique_ptr in inst.methods,
            //       so the LFunction itself (and therefore its body LBlock) is
            //       heap-stable even when `inst` is later moved into out_.structs.
            // subst_stmt deliberately returns LStmt with mirror_offset_=0 — emitting
            // mid-clone would point the mirror at a transient vector slot.
            for (auto& m : inst.methods)
                lir_mirror_emit_function(out_, *out_.mirror_table, *m);
            for (auto& m : inst.methods) scan_fn(*m);
            // Apply explicit instantiation annotation if present (sets type_code
            // on a specific generic instantiation, e.g. `#[type_code=100] eidos Array<AnyVal>;`).
            for (auto& ia : out_.inst_annotations) {
                if (ia.mangled_name == cname && ia.type_code != 0) {
                    inst.type_code = ia.type_code;
                    break;
                }
            }
            // Collect field types of new struct for further instantiation.
            for (auto& f : inst.fields) collect_type_for_structs(f.type);
            out_.structs.push_back(std::move(inst));
        }
        depth_ = 0;

        // Drain any fn-worklist items added by struct-method scans above.
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            depth_ = item.depth;
            auto fn_inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
            // Same two-step stabilization as the struct-method case above:
            // (a) instantiate_fn's recursive subst_block push_backs have ended
            //     so stmt-vector buffers are fixed; (b) make_unique parks the
            //     LFunction at a heap-stable address before emit walks it.
            out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn_inst)));
            auto& fn_ref = *out_.functions.back();
            lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
            scan_fn(fn_ref);
        }
        depth_ = 0;
    }
}


// ── Class monomorphization ────────────────────────────────────

// Clone a class def with substitution; rename to new_name.
// Mirrors clone_struct_def but preserves vtable_order, parent_name, etc.

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
            auto pt = v.payload_types[0];
            if (TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto pit = packs.find(TypeRef(pt).type_var_name());
                if (pit != packs.end()) {
                    for (auto pt_in_pack : pit->second)
                        nv.payload_types.push_back(subst_type(pt_in_pack, s));
                } else {
                    nv.payload_types.push_back(subst_type(pt, s));
                }
            } else {
                nv.payload_types.push_back(subst_type(pt, s));
            }
        } else {
            for (auto pt : v.payload_types)
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
        std::vector<std::pair<std::string, std::pair<std::vector<TypeRef>, int>>> todo;
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
                    std::vector<TypeRef> pack;
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
            for (auto& fn_up : in_.functions) {
                auto& fn = *fn_up;
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
                         std::vector<TypeRef> pack;
                         while (j < args.size()) pack.push_back(args[j++]);
                         fn_packs[fn.type_params[i].name] = std::move(pack);
                    } else if (j < args.size()) {
                        fn_subst[fn.type_params[i].name] = args[j++];
                    }
                }
                auto nm = clone_fn(fn, fn_subst, fn_packs);
                nm.name = inst_name;
                done_.insert(inst_name);
                out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(nm)));
                lir_mirror_emit_function(out_, *out_.mirror_table, *out_.functions.back());
            }
            out_.enums.push_back(std::move(inst));
        }
    }
}



} // namespace logos::compiler
