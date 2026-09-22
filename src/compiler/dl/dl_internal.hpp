// Rule planning, shared by the parser (safety errors at load time) and the
// evaluator (the join order it runs). One function decides both, so a rule the
// parser accepts is exactly a rule the evaluator can run.
#pragma once

#include "dl.hpp"

#include <string>
#include <vector>

namespace logos::dl::detail {

// How one column of a scanned or negated atom is treated at its step.
struct ColOp {
    enum class Kind : uint8_t {
        Key,      // value known before the step: part of the lookup key
        Bind,     // first occurrence of a variable: assign from the row
        Repeat,   // variable bound earlier in this same atom: compare
        Wild,
    };
    Kind     kind = Kind::Wild;
    uint32_t col  = 0;
    Term     term;
};

struct Step {
    enum class Kind : uint8_t { Scan, Neg, Cmp, Bind };
    Kind     kind = Kind::Scan;
    uint32_t lit  = 0;          // index into Rule::body
    uint32_t mask = 0;          // Scan/Neg: columns in the lookup key
    std::vector<ColOp> cols;    // Scan/Neg
    // Cmp: lit's lhs/rhs, both bound. Bind: `var` <- `from`.
    uint32_t var = 0;
    Term     from;
};

struct Plan {
    std::vector<Step>     steps;
    std::vector<uint32_t> pos_lits;   // body indices of positive atoms, in order
};

// Orders the body: positive atoms as written; each negation / comparison right
// after the first point its variables are bound; `X = t` with X unbound
// becomes a binding. Fails with a message naming the unbound variable.
//
// `first_lit` >= 0 puts that positive atom FIRST and the rest after it, in
// written order. Semi-naive evaluation restricts ONE body atom to the delta
// rows, and a plan that reaches it second scans every earlier atom in full:
// `ocl(O1,L,P), subset(O1,O2,P)` with the delta in `subset` walked all 592,162
// rows of `ocl` per round. One plan per delta position makes the delta atom
// the driver, so the rest join against bound columns.
bool plan_rule(const Rule& r, Plan& out, std::string& err, int32_t first_lit = -1);

} // namespace logos::dl::detail
