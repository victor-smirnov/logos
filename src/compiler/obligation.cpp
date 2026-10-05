// ADR 0030 S9 (C-OBL) — see obligation.hpp.
#include "obligation.hpp"

#include <algorithm>

namespace logos::compiler::obl {

using K = LogosType::Kind;

LangIds LangIds::active() {
    auto id = [](std::string_view lang) { return std::string(lang_item_identity(lang)); };
    return {id("copy"), id("clone"), id("sized"), id("fn"), id("fn_mut"), id("fn_once"),
            id("eq"), id("partial_eq"), id("ord"), id("partial_ord")};
}

void ImplTable::add(ImplFact f) {
    const uint32_t i = static_cast<uint32_t>(facts_.size());
    by_trait_[f.trait].push_back(i);
    facts_.push_back(std::move(f));
}

const std::vector<uint32_t>& ImplTable::of(std::string_view trait) const {
    static const std::vector<uint32_t> none;
    auto it = by_trait_.find(std::string(trait));
    return it == by_trait_.end() ? none : it->second;
}

namespace {

bool is_generic(const std::vector<std::string>& g, std::string_view n) {
    return std::find(g.begin(), g.end(), n) != g.end();
}

// A nominal's declared name: mono's instances carry their instance spelling
// (`RangeOfIncl$G1$i64`, a `$M<hash>` package fingerprint) in the name.
std::string_view declared_name(std::string_view n) {
    if (auto p = n.find("$G"); p != std::string_view::npos) n = n.substr(0, p);
    if (auto p = n.find("$M"); p != std::string_view::npos) n = n.substr(0, p);
    return n;
}

bool same_nominal(std::string_view pa, std::string_view pb, std::string_view na, std::string_view nb) {
    return declared_name(na) == declared_name(nb) && (pa.empty() || pb.empty() || pa == pb);
}

}  // namespace

// One type: a primitive scalar IS its kind, whichever pool (or none) carries it.
static bool same_type(TypeRef a, TypeRef b) {
    if (!a || !b) return !a && !b;
    if (is_primitive_scalar_kind(a.kind()) || is_primitive_scalar_kind(b.kind())) return a.kind() == b.kind();
    return types_equal(a, b);
}

bool unify(TypeRef c, TypeRef p, const std::vector<std::string>& generics, Subst& s, std::string_view pack) {
    if (!c || !p) return false;
    // `(A...)`: the one-element tuple of the pack takes the whole tuple.
    if (!pack.empty() && p.kind() == K::Tuple && c.kind() == K::Tuple) {
        auto pe = p.tuple_elems();
        if (pe.size() == 1 && pe[0].kind() == K::TypeVar && pe[0].type_var_name() == pack) {
            s[std::string(pack)] = c;
            return true;
        }
    }
    if ((p.kind() == K::TypeVar || p.kind() == K::ConstVar) && is_generic(generics, p.type_var_name())) {
        std::string n(p.type_var_name());
        if (auto it = s.find(n); it != s.end()) return same_type(c, it->second) || unify(c, it->second, {}, s);
        s.emplace(std::move(n), c);
        return true;
    }
    if (p.kind() != c.kind()) return false;   // `[T]` and `&[T]` are two types, as in Rust
    switch (p.kind()) {
    case K::Ptr:
        return p.mut_ptr() == c.mut_ptr() && unify(c.pointee(), p.pointee(), generics, s, pack);
    case K::Ref:
    case K::MutRef:
        return unify(c.pointee(), p.pointee(), generics, s, pack);
    case K::Slice:
    case K::UnsizedSlice:
        return unify(c.elem(), p.elem(), generics, s, pack);
    case K::Array:
        // `[T; N]` with N a const generic: any length.
        if (!p.arr_size_var().empty()) return unify(c.elem(), p.elem(), generics, s, pack);
        return p.arr_size() == c.arr_size() && unify(c.elem(), p.elem(), generics, s, pack);
    case K::Struct:
    case K::ZonedStruct:
    case K::Enum: {
        const bool st = p.kind() != K::Enum;
        if (!same_nominal(p.pkg_name(), c.pkg_name(), st ? p.struct_name() : p.enum_name(),
                          st ? c.struct_name() : c.enum_name()))
            return false;
        auto pa = p.type_args(), ca = c.type_args();
        if (pa.size() != ca.size()) return false;
        for (size_t i = 0; i < pa.size(); ++i)
            if (!unify(ca[i], pa[i], generics, s, pack)) return false;
        return true;
    }
    case K::Tuple: {
        auto pe = p.tuple_elems(), ce = c.tuple_elems();
        if (pe.size() != ce.size()) return false;
        for (size_t i = 0; i < pe.size(); ++i)
            if (!unify(ce[i], pe[i], generics, s, pack)) return false;
        return true;
    }
    case K::FnPtr: {
        auto pp = p.closure_params(), cp = c.closure_params();
        if (pp.size() != cp.size()) return false;
        for (size_t i = 0; i < pp.size(); ++i)
            if (!unify(cp[i], pp[i], generics, s, pack)) return false;
        if (!p.closure_ret() || !c.closure_ret()) return !p.closure_ret() && !c.closure_ret();
        return unify(c.closure_ret(), p.closure_ret(), generics, s, pack);
    }
    case K::IntLit:
        return p.const_val() && c.const_val() ? *p.const_val() == *c.const_val() : types_equal(c, p);
    default:
        if (is_primitive_scalar_kind(p.kind())) return true;
        return types_equal(c, p);
    }
}

namespace {

struct Solver {
    const ImplTable& table;
    const Env& env;
    const FnSig* sig = nullptr;
    int depth = 0;

    // The callable's parameters and result against the bound's `Fn(A…) -> R`.
    bool sig_matches(TypeRef callable) const {
        if (!sig || !env.same_shape) return true;
        auto got = callable.closure_params();
        auto tv = [&](TypeRef t) { return !t || (env.mentions_tv && env.mentions_tv(t)); };
        for (auto w : sig->params) if (tv(w)) return true;
        if (sig->ret && tv(sig->ret)) return true;
        for (auto g : got) if (tv(g)) return true;
        if (callable.closure_ret() && tv(callable.closure_ret())) return true;
        if (got.size() != sig->params.size()) return false;
        for (size_t i = 0; i < got.size(); ++i)
            if (!env.same_shape(got[i], sig->params[i])) return false;
        if (sig->ret && callable.closure_ret() && !env.same_shape(callable.closure_ret(), sig->ret)) return false;
        return true;
    }

    bool scalar(TypeRef t) const { return is_primitive_scalar_kind(t.kind()); }

    bool all_hold(std::string_view trait, const std::vector<TypeRef>& ts) {
        for (auto t : ts)
            if (!select(trait, t, {}).holds()) return false;
        return true;
    }

    // Compiler rules (Rust's builtin impls); nullopt = no rule for this pair.
    std::optional<bool> builtin(std::string_view trait, TypeRef self, const std::vector<TypeRef>& args) {
        const LangIds& l = env.lang;
        const K k = self.kind();
        if (!l.sized.empty() && trait == l.sized)
            return !(k == K::UnsizedSlice || k == K::UnsizedDyn);
        // Copy is a marker: a builtin rule needs no method. Clone carries `clone`,
        // so it holds only by an impl whose method can be called.
        const bool copy = !l.copy.empty() && trait == l.copy;
        if (copy) {
            if (scalar(self) || k == K::Never || k == K::Ref || k == K::Ptr || k == K::FnPtr || k == K::FnItem)
                return true;
            if (k == K::Slice) return !self.owning_slice();
            if (k == K::TraitObject) return !self.owning_trait_object();
            if (k == K::Tuple) return all_hold(trait, self.tuple_elems());
            if (k == K::Array) return all_hold(trait, {self.elem()});
            if (k == K::MutRef) return false;
            return std::nullopt;
        }
        const bool fn = !l.fn.empty() && trait == l.fn;
        const bool fn_mut = !l.fn_mut.empty() && trait == l.fn_mut;
        const bool fn_once = !l.fn_once.empty() && trait == l.fn_once;
        if (fn || fn_mut || fn_once) {
            if (k == K::FnPtr || k == K::FnItem) return sig_matches(self);
            if (k == K::Closure) {
                if (!sig_matches(self)) return false;
                // The family the closure's body admits: Fn ⊂ FnMut ⊂ FnOnce.
                using F = TypeRef::FnFamily;
                const int need = fn ? 0 : fn_mut ? 1 : 2;
                if (env.closure_level) return env.closure_level(self) <= need;
                const F fam = self.closure_fn_family();
                return fam == F::Unstated || static_cast<int>(fam) - 1 <= need;
            }
            // `&F: Fn*` when `F: Fn`; `&mut F: FnMut / FnOnce` when `F: FnMut`.
            if (k == K::Ref && self.pointee()) return select(l.fn, self.pointee(), args).holds();
            if (k == K::MutRef && self.pointee() && !fn)
                return select(l.fn_mut, self.pointee(), args).holds();
            return std::nullopt;
        }
        if (env.auto_trait)
            if (auto a = env.auto_trait(self, trait)) return *a;
        return std::nullopt;
    }

    // Self against an impl's pattern; a `&[E]` against `&T` binds T = `[E]`.
    bool unify_self(TypeRef self, const ImplFact& f, Subst& s) const {
        if (unify(self, f.self, f.generics, s, f.pack)) return true;
        if (self.kind() == K::Slice && f.self.kind() == K::Ref && env.unsized_of) {
            s.clear();
            if (TypeRef us = env.unsized_of(self)) return unify(us, f.self.pointee(), f.generics, s, f.pack);
        }
        return false;
    }

    Selection by_impls(std::string_view trait, TypeRef self, const std::vector<TypeRef>& args,
                       std::vector<Selection>* all = nullptr) {
        Selection out;
        const ImplFact* neg = nullptr;
        for (uint32_t i : table.of(trait)) {
            const ImplFact& f = table.at(i);
            Subst s;
            if (!unify_self(self, f, s)) continue;
            // An impl that writes no trait arguments takes the trait's defaults
            // (`impl AddAssign for u8` is `AddAssign<u8>`): any asked ones answer.
            bool ok = f.trait_args.size() == args.size() || args.empty() || f.trait_args.empty();
            // An argument that still mentions a type variable (a generic body's
            // `Item: IntoPair<A, B>` with the method's own A, B) is not decided
            // here: the instantiation fixes it.
            for (size_t a = 0; ok && a < args.size() && a < f.trait_args.size(); ++a)
                ok = !args[a] || (env.mentions_tv && env.mentions_tv(args[a])) ||
                     unify(args[a], f.trait_args[a], f.generics, s, f.pack);
            if (!ok) continue;
            if (f.negative) { neg = &f; continue; }
            bool nested = true;
            for (auto& b : f.bounds) {
                auto it = s.find(b.param);
                if (it == s.end()) continue;   // a generic Self does not fix: the call site does
                std::vector<TypeRef> bargs;
                for (auto a : b.args) bargs.push_back(env.subst ? env.subst(a, s) : a);
                if (b.param == f.pack && it->second.kind() == K::Tuple) {
                    for (auto e : it->second.tuple_elems())
                        if (!select(b.trait, e, bargs).holds()) { nested = false; break; }
                    if (!nested) break;
                    continue;
                }
                if (!select(b.trait, it->second, bargs).holds()) { nested = false; break; }
            }
            if (!nested) continue;
            if (all) all->push_back({Kind::Impl, &f, s});
            if (out.kind == Kind::Impl) { out.kind = Kind::Ambiguous; continue; }
            if (out.kind == Kind::Ambiguous) continue;
            out.kind = Kind::Impl;
            out.impl = &f;
            out.subst = std::move(s);
        }
        if (neg) { if (all) all->clear(); return {}; }
        return out;
    }

    Selection select(std::string_view trait, TypeRef self, const std::vector<TypeRef>& args) {
        if (!self) return {};
        if (depth > 48) return {};   // an obligation that does not terminate does not hold
        struct Depth { int& d; Depth(int& x) : d(x) { ++d; } ~Depth() { --d; } } guard{depth};
        if (env.is_open && env.is_open(self)) return {Kind::Deferred, nullptr, {}};
        if (self.kind() == K::TypeVar) {
            if (env.param_holds && env.param_holds(self, trait, args)) return {Kind::Param, nullptr, {}};
            if (auto b = builtin(trait, self, args); b && *b) return {Kind::Builtin, nullptr, {}};
            return by_impls(trait, self, args);   // a blanket impl over a bare parameter
        }
        if (auto b = builtin(trait, self, args)) {
            if (*b) return {Kind::Builtin, nullptr, {}};
            // A builtin "no" still lets an explicit impl answer (`impl Copy for MyStruct`).
        }
        if ((self.kind() == K::TraitObject || self.kind() == K::UnsizedDyn) && env.object_implements &&
            env.object_implements(self, trait))
            return {Kind::Builtin, nullptr, {}};
        Selection s = by_impls(trait, self, args);
        if (s.holds()) return s;
        // SL-sl-02: Logos's `Eq` / `Ord` are not declared `: PartialEq` / `: PartialOrd`;
        // the total impl answers the partial bound, which carries the same method.
        const LangIds& l = env.lang;
        if (!l.partial_eq.empty() && trait == l.partial_eq && !l.eq.empty()) return select(l.eq, self, args);
        if (!l.partial_ord.empty() && trait == l.partial_ord && !l.ord.empty()) return select(l.ord, self, args);
        return s;
    }
};

}  // namespace

Selection select(const ImplTable& table, const Env& env, std::string_view trait, TypeRef self,
                 const std::vector<TypeRef>& args, const FnSig* sig) {
    Solver s{table, env, sig};
    return s.select(trait, self, args);
}

std::vector<Selection> candidates(const ImplTable& table, const Env& env, std::string_view trait,
                                  TypeRef self, const std::vector<TypeRef>& args) {
    std::vector<Selection> all;
    if (!self) return all;
    Solver s{table, env, nullptr};
    (void)s.by_impls(trait, self, args, &all);
    return all;
}

}  // namespace logos::compiler::obl
