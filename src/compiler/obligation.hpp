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
    // `param: trait<Name = T>`: the projection must equal T (over the generics).
    std::vector<std::pair<std::string, TypeRef>> assoc_eqs;
};

struct AssocItem {
    std::string name;
    TypeRef type;
    std::vector<std::string> params;   // a generic associated type's own type parameters
};

struct ImplFact {
    std::string trait;               // identity `pkg::Trait`
    std::vector<TypeRef> trait_args; // patterns over `generics`
    TypeRef self;                    // pattern over `generics`; a bare generic for a blanket impl
    std::vector<std::string> generics;
    std::string pack;                // the variadic generic (`impl<A...> Tr for (A...)`), or empty
    std::vector<Bound> bounds;
    // The impl's associated types (`type Item = T;`, `type F<U> = Vec<U>;`), over
    // its generics and the item's own parameters.
    std::vector<AssocItem> assoc_types;
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
    // A trait's direct supertraits by identity: an item a supertrait declares
    // projects through it (`<I as DoubleEndedIterator>::Item` is Iterator's).
    std::function<std::vector<std::string>(std::string_view trait)> supertraits;
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

// Coherence (rustc E0119): do two impl heads unify? `vars` are both impls'
// generics, renamed apart by the caller; `s` collects the unifier (a variable may
// be bound to another: resolve through it). No occurs check: a cyclic answer only
// errs toward "overlap".
bool heads_unify(TypeRef a, TypeRef b, const std::vector<std::string>& vars, Subst& s);
// A specialization (a partial struct spec, a function spec): its patterns over
// its generics, and its generics' bounds — gates only when every position is a
// generic (a bound-discriminated spec, `struct S<T: Copy + Fst>`).
struct SpecCand {
    std::vector<TypeRef> patterns;
    std::vector<std::string> generics;
    std::vector<Bound> bounds;
};
struct SpecPick {
    int index = -1;          // the most specific candidate that applies, or -1
    bool ambiguous = false;  // two apply and neither is more specific
};
// The one specialization selection (ADR 0030 S9 row 6): each pattern unifies with
// its argument; a bound-discriminated candidate's bounds hold at the argument (an
// argument still mentioning a type variable: `open_args_hold` decides for a bare
// one — sema asks its scope's bounds, mono defers to the concrete re-scan — and a
// compound one defers). Most specific wins, position by position (a concrete
// head over `[T]` over a variable; each gating bound adds one); a tie is ambiguous.
SpecPick pick_specialization(const ImplTable& table, const Env& env, const std::vector<SpecCand>& cands,
                             const std::vector<TypeRef>& args, bool open_args_hold);
int pattern_specificity(TypeRef pattern);

// Does `t` mention one of `generics`?
bool mentions_generic(TypeRef t, const std::vector<std::string>& generics);

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
                               const std::vector<TypeRef>& args, std::string_view name,
                               const std::vector<TypeRef>& item_args = {});

}  // namespace logos::compiler::obl
