#pragma once
// ADR 0030 S8 row 3 — the ONE operator table: an operator spelling to its
// lang trait, the trait's method, and (for the arithmetic / bitwise ones) the
// compound-assignment twin. Sema's operator lowering, compound assignment,
// the bounded-type-parameter `Output` question and mono's re-dispatch all read
// it; a copy per site is how mono came to know no bitwise operator.

#include <string_view>

namespace logos::compiler {

struct OpLangItem {
    std::string_view op;
    std::string_view trait;          // the lang trait (`Add`, `Eq`, `Ord`, `Neg`)
    std::string_view method;         // its method (`add`, `eq`, `lt`, `neg`)
    std::string_view assign_trait;   // `AddAssign`, empty for comparisons / unary
    std::string_view assign_method;  // `add_assign`
    std::string_view lang;           // the trait's `#[lang]` name (`add`, `partial_eq`, `ord`, `neg`)
    // A by-value operator trait with an `Output` (arithmetic, bitwise, shift).
    bool has_output() const noexcept { return !assign_trait.empty(); }
};

inline const OpLangItem* binary_op_item(std::string_view op) noexcept {
    static constexpr OpLangItem kBinary[] = {
        {"+",  "Add",    "add",    "AddAssign",    "add_assign", "add"},
        {"-",  "Sub",    "sub",    "SubAssign",    "sub_assign", "sub"},
        {"*",  "Mul",    "mul",    "MulAssign",    "mul_assign", "mul"},
        {"/",  "Div",    "div",    "DivAssign",    "div_assign", "div"},
        {"%",  "Rem",    "rem",    "RemAssign",    "rem_assign", "rem"},
        {"&",  "BitAnd", "bitand", "BitAndAssign", "bitand_assign", "bitand"},
        {"|",  "BitOr",  "bitor",  "BitOrAssign",  "bitor_assign", "bitor"},
        {"^",  "BitXor", "bitxor", "BitXorAssign", "bitxor_assign", "bitxor"},
        {"<<", "Shl",    "shl",    "ShlAssign",    "shl_assign", "shl"},
        {">>", "Shr",    "shr",    "ShrAssign",    "shr_assign", "shr"},
        {"==", "Eq",     "eq",     {}, {}, "partial_eq"},
        {"!=", "Eq",     "ne",     {}, {}, "partial_eq"},
        {"<",  "Ord",    "lt",     {}, {}, "ord"},
        {"<=", "Ord",    "le",     {}, {}, "ord"},
        {">",  "Ord",    "gt",     {}, {}, "ord"},
        {">=", "Ord",    "ge",     {}, {}, "ord"},
    };
    for (auto& it : kBinary) if (it.op == op) return &it;
    return nullptr;
}

inline const OpLangItem* unary_op_item(std::string_view op) noexcept {
    static constexpr OpLangItem kUnary[] = {
        {"-", "Neg", "neg", {}, {}, "neg"},
        {"!", "Not", "not", {}, {}, "not"},
    };
    for (auto& it : kUnary) if (it.op == op) return &it;
    return nullptr;
}

} // namespace logos::compiler
