// Logos project — https://github.com/victor-smirnov/logos
//
// CTFE evaluator — see ctfe.hpp.

#include "ctfe.hpp"
#include "sema_impl.hpp"   // parse_int_literal, valid_*_format, *_suffix_kind

#include <cmath>
#include <cstring>

namespace logos::compiler::ctfe {

namespace {

using namespace logos::hermes;
using K = LogosType::Kind;
using namespace logos::compiler::sema_detail;  // la::*

inline int32_t code_of(TinyMapView node) noexcept {
    if (node.is_null()) return -1;
    AnyVal av = node.get(la::CODE.code);
    return av.is_null() ? -1 : av.as_value<int32_t>();
}

inline std::string_view str_of(AnyVal av, MemHolder* h) noexcept {
    if (av.is_null()) return {};
    return StringView(av, h).view();
}

inline TinyMapView map_of(AnyVal av, MemHolder* h) noexcept {
    if (av.is_null()) return TinyMapView{};
    return TinyMapView(av, h);
}

inline bool is_signed_int(K k) noexcept {
    return k == K::I8 || k == K::I16 || k == K::I24 || k == K::I32 ||
           k == K::I56 || k == K::I64 || k == K::I128 || k == K::IntLit;
}
inline bool is_unsigned_int(K k) noexcept {
    return k == K::U8 || k == K::U16 || k == K::U24 || k == K::U32 ||
           k == K::U56 || k == K::U64 || k == K::U128;
}
inline bool is_int(K k) noexcept { return is_signed_int(k) || is_unsigned_int(k); }
inline bool is_float(K k) noexcept {
    return k == K::F32 || k == K::F64 || k == K::FloatLit;
}

CtfeError err(std::string s) { return CtfeError{std::move(s)}; }

// Promote two integer kinds to a common kind. IntLit yields to the other.
K promote_int(K a, K b) noexcept {
    if (a == b) return a;
    if (a == K::IntLit) return b;
    if (b == K::IntLit) return a;
    // Prefer signed-of-larger over unsigned for mixed arithmetic; fallback I64.
    return K::I64;
}
K promote_float(K a, K b) noexcept {
    if (a == K::F64 || b == K::F64) return K::F64;
    if (a == K::F32 || b == K::F32) return K::F32;
    return K::FloatLit;
}

logos::expected<CtfeValue, CtfeError>
do_eval(TinyMapView node, MemHolder* h,
        ConstResolver* resolver = nullptr) noexcept;

logos::expected<CtfeValue, CtfeError>
eval_lit_int(TinyMapView node, MemHolder* h) noexcept {
    auto sv = str_of(node.get(la::VALUE.code), h);
    if (!valid_int_literal_format(sv))
        return std::unexpected(err("ctfe: malformed integer literal"));
    int64_t v = parse_int_literal(sv);
    K suf = int_suffix_kind(sv);
    K kind = (suf != K::Error) ? suf : K::IntLit;
    CtfeValue out;
    out.kind = kind;
    out.i = v;
    out.u = static_cast<uint64_t>(v);
    return out;
}

logos::expected<CtfeValue, CtfeError>
eval_lit_float(TinyMapView node, MemHolder* h) noexcept {
    auto sv = str_of(node.get(la::VALUE.code), h);
    if (!valid_float_literal_format(sv))
        return std::unexpected(err("ctfe: malformed float literal"));
    std::string s(sv);
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    K suf = float_suffix_kind(sv);
    if (suf != K::Error) s.resize(s.size() - 3);
    double v = std::stod(s);
    CtfeValue out;
    out.kind = (suf != K::Error) ? suf : K::FloatLit;
    out.f = v;
    return out;
}

logos::expected<CtfeValue, CtfeError>
eval_lit_bool(TinyMapView node) noexcept {
    AnyVal av = node.get(la::VALUE.code);
    bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
    CtfeValue out;
    out.kind = K::Bool;
    out.b = v;
    return out;
}

logos::expected<CtfeValue, CtfeError>
eval_lit_str(TinyMapView node, MemHolder* h) noexcept {
    auto sv = str_of(node.get(la::VALUE.code), h);
    CtfeValue out;
    // Use Slice as a stand-in for str (Slice<u8>); compiler validates against
    // its actual `&str` / `Slice<u8>` typeref separately.
    out.kind = K::Slice;
    out.s = std::string(sv);
    return out;
}

logos::expected<CtfeValue, CtfeError>
eval_unary(TinyMapView node, MemHolder* h, ConstResolver* resolver) noexcept {
    auto op = str_of(node.get(la::OP.code), h);
    auto inner_node = map_of(node.get(la::VALUE.code), h);
    auto v = do_eval(inner_node, h, resolver);
    if (!v) return v;
    CtfeValue r = *v;
    if (op == "-") {
        if (is_int(r.kind)) {
            r.i = -r.i;
            r.u = static_cast<uint64_t>(r.i);
            return r;
        }
        if (is_float(r.kind)) { r.f = -r.f; return r; }
        return std::unexpected(err("ctfe: unary '-' requires numeric operand"));
    }
    if (op == "!") {
        if (r.kind == K::Bool) { r.b = !r.b; return r; }
        return std::unexpected(err("ctfe: unary '!' requires bool operand"));
    }
    return std::unexpected(err(std::string("ctfe: unsupported unary op '") + std::string(op) + "'"));
}

logos::expected<CtfeValue, CtfeError>
eval_binop(TinyMapView node, MemHolder* h, ConstResolver* resolver) noexcept {
    auto op = str_of(node.get(la::OP.code), h);
    auto lhs_n = map_of(node.get(la::LHS.code), h);
    auto rhs_n = map_of(node.get(la::RHS.code), h);
    auto lv_e = do_eval(lhs_n, h, resolver);
    if (!lv_e) return lv_e;
    auto rv_e = do_eval(rhs_n, h, resolver);
    if (!rv_e) return rv_e;
    CtfeValue l = *lv_e, r = *rv_e;

    auto bool_res = [](bool v) {
        CtfeValue o; o.kind = K::Bool; o.b = v; return o;
    };

    // Bool/logical ops
    if (op == "&&" || op == "||") {
        if (l.kind != K::Bool || r.kind != K::Bool)
            return std::unexpected(err("ctfe: logical op requires bool operands"));
        return bool_res(op == "&&" ? (l.b && r.b) : (l.b || r.b));
    }

    // Numeric arithmetic / comparisons.
    bool both_float = is_float(l.kind) || is_float(r.kind);
    bool both_int   = is_int(l.kind) && is_int(r.kind);

    if (both_float && (is_int(l.kind) || is_int(r.kind) || is_float(l.kind) || is_float(r.kind))) {
        // Coerce ints into doubles for mixed; otherwise use the float values.
        double a = is_float(l.kind) ? l.f : (double)l.i;
        double b = is_float(r.kind) ? r.f : (double)r.i;
        K rk = promote_float(is_float(l.kind) ? l.kind : K::FloatLit,
                             is_float(r.kind) ? r.kind : K::FloatLit);
        CtfeValue o;
        if (op == "+") { o.kind = rk; o.f = a + b; return o; }
        if (op == "-") { o.kind = rk; o.f = a - b; return o; }
        if (op == "*") { o.kind = rk; o.f = a * b; return o; }
        if (op == "/") {
            if (b == 0.0) return std::unexpected(err("ctfe: float division by zero"));
            o.kind = rk; o.f = a / b; return o;
        }
        if (op == "==") return bool_res(a == b);
        if (op == "!=") return bool_res(a != b);
        if (op == "<")  return bool_res(a <  b);
        if (op == "<=") return bool_res(a <= b);
        if (op == ">")  return bool_res(a >  b);
        if (op == ">=") return bool_res(a >= b);
        return std::unexpected(err(std::string("ctfe: unsupported float binop '") + std::string(op) + "'"));
    }

    if (both_int) {
        K rk = promote_int(l.kind, r.kind);
        bool sgn = is_signed_int(rk);
        int64_t  ai = l.i, bi = r.i;
        uint64_t au = l.u, bu = r.u;
        CtfeValue o; o.kind = rk;
        if (op == "+") { if (sgn) { o.i = ai + bi; o.u = (uint64_t)o.i; } else { o.u = au + bu; o.i = (int64_t)o.u; } return o; }
        if (op == "-") { if (sgn) { o.i = ai - bi; o.u = (uint64_t)o.i; } else { o.u = au - bu; o.i = (int64_t)o.u; } return o; }
        if (op == "*") { if (sgn) { o.i = ai * bi; o.u = (uint64_t)o.i; } else { o.u = au * bu; o.i = (int64_t)o.u; } return o; }
        if (op == "/") {
            if (sgn) {
                if (bi == 0) return std::unexpected(err("ctfe: integer division by zero"));
                o.i = ai / bi; o.u = (uint64_t)o.i;
            } else {
                if (bu == 0) return std::unexpected(err("ctfe: integer division by zero"));
                o.u = au / bu; o.i = (int64_t)o.u;
            }
            return o;
        }
        if (op == "%") {
            if (sgn) {
                if (bi == 0) return std::unexpected(err("ctfe: integer modulo by zero"));
                o.i = ai % bi; o.u = (uint64_t)o.i;
            } else {
                if (bu == 0) return std::unexpected(err("ctfe: integer modulo by zero"));
                o.u = au % bu; o.i = (int64_t)o.u;
            }
            return o;
        }
        if (op == "<<") { o.u = au << (bu & 63); o.i = (int64_t)o.u; return o; }
        if (op == ">>") {
            if (sgn) { o.i = ai >> (bi & 63); o.u = (uint64_t)o.i; }
            else     { o.u = au >> (bu & 63); o.i = (int64_t)o.u; }
            return o;
        }
        if (op == "&")  { o.u = au & bu; o.i = (int64_t)o.u; return o; }
        if (op == "|")  { o.u = au | bu; o.i = (int64_t)o.u; return o; }
        if (op == "^")  { o.u = au ^ bu; o.i = (int64_t)o.u; return o; }
        if (op == "==") return bool_res(sgn ? (ai == bi) : (au == bu));
        if (op == "!=") return bool_res(sgn ? (ai != bi) : (au != bu));
        if (op == "<")  return bool_res(sgn ? (ai <  bi) : (au <  bu));
        if (op == "<=") return bool_res(sgn ? (ai <= bi) : (au <= bu));
        if (op == ">")  return bool_res(sgn ? (ai >  bi) : (au >  bu));
        if (op == ">=") return bool_res(sgn ? (ai >= bi) : (au >= bu));
        return std::unexpected(err(std::string("ctfe: unsupported integer binop '") + std::string(op) + "'"));
    }

    // Bool equality
    if (l.kind == K::Bool && r.kind == K::Bool) {
        if (op == "==") return bool_res(l.b == r.b);
        if (op == "!=") return bool_res(l.b != r.b);
    }

    return std::unexpected(err(std::string("ctfe: unsupported binop '") + std::string(op) + "' on these operand types"));
}

logos::expected<CtfeValue, CtfeError>
do_eval(TinyMapView node, MemHolder* h, ConstResolver* resolver) noexcept {
    int32_t c = code_of(node);
    if (c == la::LIT_INT)    return eval_lit_int(node, h);
    if (c == la::LIT_FLOAT)  return eval_lit_float(node, h);
    if (c == la::LIT_BOOL)   return eval_lit_bool(node);
    if (c == la::LIT_STR)    return eval_lit_str(node, h);
    if (c == la::PAREN_EXPR) return do_eval(map_of(node.get(la::VALUE.code), h), h, resolver);
    if (c == la::UNARY)      return eval_unary(node, h, resolver);
    if (c == la::BINOP)      return eval_binop(node, h, resolver);
    // §6.9: path-to-const resolution. A bare IDENT in const-eval
    // position (`metacall { THRESHOLD + 1 }`) lands as VAR_REF; the
    // resolver hands us the const's RHS expression node + its
    // owning holder, and we recurse. Cross-package consts work as
    // long as the resolver knows about them.
    if (c == la::VAR_REF && resolver) {
        AnyVal name_av = node.get(la::NAME.code);
        std::string_view name = str_of(name_av, h);
        MemHolder* tgt_holder = h;
        auto tgt_node = resolver->lookup_const(name, &tgt_holder);
        if (!tgt_node.is_null())
            return do_eval(tgt_node, tgt_holder, resolver);
    }
    return std::unexpected(err("ctfe: expression is not a compile-time constant"));
}

} // namespace

logos::expected<CtfeValue, CtfeError>
eval_expr(hermes::TinyMapView node, hermes::MemHolder* holder,
          ConstResolver* resolver) noexcept {
    return do_eval(node, holder, resolver);
}

} // namespace logos::compiler::ctfe
