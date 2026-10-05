#pragma once
// ADR 0030 S9 (C-OBL): the one answer to "does `Self: Trait<A…>` hold, and by
// which impl". Sema and mono each build an ImplTable from the impls they see
// and ask `select`; what only a phase knows (its type parameters' bounds, its
// substitution, auto-trait structure) comes in through Env. Pool-free: the
// solver never allocates a type, it unifies patterns and asks Env::subst.

#include "logos/compiler/sema.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::compiler::obl {

using Subst = std::unordered_map<std::string, TypeRef>;

// `param: trait<args>` — a bound of an impl's own generic parameter.
struct Bound {
    std::string param;
    std::string trait;               // identity `pkg::Trait`
    std::vector<TypeRef> args;       // may mention the impl's generics
};

struct ImplFact {
    std::string trait;               // identity `pkg::Trait`
    std::vector<TypeRef> trait_args; // patterns over `generics`
    TypeRef self;                    // pattern over `generics`; a bare generic for a blanket impl
    std::vector<std::string> generics;
    std::string pack;                // the variadic generic (`impl<A...> Tr for (A...)`), or empty
    std::vector<Bound> bounds;
    // The impl's associated types (`type Item = T;`), over its generics.
    std::vector<std::pair<std::string, TypeRef>> assoc_types;
    bool negative = false;
    // A fact that answers only when no other impl does (Logos's `impl … for
    // str` facts: `str` IS `[u8]`, and a pattern over the same Self wins).
    bool fallback = false;
    uint32_t source = 0;             // the phase's own index of the impl
};

// The identities of the lang items the builtin rules answer for (empty = the
// trait is not declared in this program).
struct LangIds {
    std::string copy, clone, sized, fn, fn_mut, fn_once;
    std::string eq, partial_eq, ord, partial_ord;
    // From the phase's active lang-item table (LProgram::lang_items).
    static LangIds active();
};

enum class Kind : uint8_t {
    None,       // does not hold
    Impl,       // by `impl` (+ substitution of its generics)
    Builtin,    // by a compiler rule (Copy of a reference, Fn of a closure, an auto trait…)
    Param,      // by a bound of the enclosing generic scope
    Deferred,   // Self is not yet known well enough (an open inference variable)
    Ambiguous,  // more than one impl answers
};

struct Selection {
    Kind kind = Kind::None;
    const ImplFact* impl = nullptr;
    Subst subst;
    bool holds() const { return kind == Kind::Impl || kind == Kind::Builtin || kind == Kind::Param ||
                                kind == Kind::Deferred || kind == Kind::Ambiguous; }
};

// The call shape a `F: Fn(A…) -> R` bound declares.
struct FnSig {
    std::vector<TypeRef> params;
    TypeRef ret = nullptr;
};

struct Env {
    LangIds lang;
    // `tv: trait<args>` for a type variable of the enclosing generic scope.
    std::function<bool(TypeRef tv, std::string_view trait, const std::vector<TypeRef>& args)> param_holds;
    // An auto trait's structural answer; nullopt when `trait` is not an auto trait.
    std::function<std::optional<bool>(TypeRef self, std::string_view trait)> auto_trait;
    // Apply a substitution of an impl's generics (the phase's pool allocates).
    std::function<TypeRef(TypeRef t, const Subst& s)> subst;
    // Does this type still carry an open inference variable (`?iN`, an
    // unsolved integer literal)? Then the answer waits.
    std::function<bool(TypeRef t)> is_open;
    // A trait object implements its own trait and that trait's supertraits.
    std::function<bool(TypeRef object, std::string_view trait)> object_implements;
    // Two types compared as call shapes (regions are not part of the question);
    // and whether a type still mentions a type variable (then it is undecided).
    std::function<bool(TypeRef a, TypeRef b)> same_shape;
    std::function<bool(TypeRef t)> mentions_tv;
    // A closure's Fn-family level (0 Fn, 1 FnMut, 2 FnOnce); unset = from its type.
    std::function<int(TypeRef closure)> closure_level;
    // `[E]` for a `&[E]` (Slice): a `&T` pattern takes a slice with T = `[E]`,
    // as Rust's `impl<T: ?Sized> Tr for &T` does (the phase's pool allocates).
    std::function<TypeRef(TypeRef slice)> unsized_of;
};

class ImplTable {
public:
    void add(ImplFact f);
    const std::vector<uint32_t>& of(std::string_view trait) const;
    const ImplFact& at(uint32_t i) const { return facts_[i]; }
    size_t size() const { return facts_.size(); }

private:
    std::vector<ImplFact> facts_;
    std::unordered_map<std::string, std::vector<uint32_t>> by_trait_;
};

// Unify a concrete (or partly generic) type with an impl pattern, binding the
// pattern's generics in `s`. Nominal types match by name AND package.
bool unify(TypeRef concrete, TypeRef pattern, const std::vector<std::string>& generics, Subst& s,
           std::string_view pack = {});

Selection select(const ImplTable& table, const Env& env, std::string_view trait, TypeRef self,
                 const std::vector<TypeRef>& args, const FnSig* sig = nullptr);

// Every impl that answers `self: trait<args>` (its pattern unifies, its own
// bounds hold), each with its substitution: the caller picks among overloads.
// Fallback facts answer only when nothing else does.
std::vector<Selection> candidates(const ImplTable& table, const Env& env, std::string_view trait,
                                  TypeRef self, const std::vector<TypeRef>& args);

// `<self as trait<args>>::name`: the selected impl's item at its substitution;
// nullopt when no impl answers, or two answer differently.
std::optional<TypeRef> project(const ImplTable& table, const Env& env, std::string_view trait, TypeRef self,
                               const std::vector<TypeRef>& args, std::string_view name);

}  // namespace logos::compiler::obl
