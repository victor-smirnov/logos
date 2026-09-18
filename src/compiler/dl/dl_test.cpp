// Unit tests for the Datalog engine (ADR 0028 S1). Result correctness on real
// programs is the Souffle oracle's job (tests/dl/oracle.sh); these cover what
// the oracle cannot see: diagnostics, refusals, provenance, the include path.
#include "dl.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace logos::dl;

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #cond);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static std::string parse_error(const char* src, const IncludeResolver& inc = {}) {
    Symbols syms;
    auto p = Program::parse(src, "t.dl", syms, inc);
    return p ? std::string() : p.error().str();
}

static bool has(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

static void test_closure_and_provenance() {
    const char* src = R"(
.decl edge(a: symbol, b: symbol)
.decl path(a: symbol, b: symbol)
.output path
edge("a", "b"). edge("b", "c"). edge("c", "a").
path(X, Y) :- edge(X, Y).
path(X, Z) :- path(X, Y), edge(Y, Z).
)";
    Symbols syms;
    auto p = Program::parse(src, "closure.dl", syms);
    CHECK(p.has_value());
    if (!p) return;
    Database db(*p, syms);
    db.run();
    uint32_t path = *p->relation("path");
    CHECK(db.relation(path).size() == 9);   // 3 nodes on one cycle: all pairs

    Value a = *syms.find("a"), c = *syms.find("c");
    Value row[] = {a, c};
    auto d = db.explain(path, row);
    CHECK(d.has_value());
    if (!d) return;
    // path(a,c) :- path(a,b), edge(b,c); path(a,b) :- edge(a,b).
    CHECK(d->rule == 1);
    CHECK(d->premises.size() == 2);
    CHECK(d->premises[0].rule == 0);
    CHECK(d->premises[0].premises.size() == 1);
    CHECK(d->premises[0].premises[0].rule == -1);
    CHECK(d->premises[1].rule == -1);
    std::string r = db.render(*d);
    CHECK(has(r, "path(\"a\", \"c\")  [closure.dl:7]"));
    CHECK(has(r, "edge(\"b\", \"c\")  [fact]"));
}

static void test_negation_and_comparisons() {
    const char* src = R"(
.decl node(x: number)
.decl edge(a: number, b: number)
.decl reach(x: number)
.decl unreached(x: number)
.decl other(a: number, b: number)
.decl same(a: number, b: number)
.decl flag()
node(1). node(2). node(3). node(4).
edge(1, 2). edge(2, 3).
reach(1).
reach(Y) :- reach(X), edge(X, Y).
unreached(X) :- node(X), !reach(X).
other(X, Y) :- node(X), node(Y), X != Y, !edge(X, _).
same(X, Y) :- node(X), Y = X.
flag() :- unreached(4).
)";
    Symbols syms;
    auto p = Program::parse(src, "neg.dl", syms);
    CHECK(p.has_value());
    if (!p) return;
    Database db(*p, syms);
    db.run();
    CHECK(db.relation(*p->relation("unreached")).size() == 1);   // 4
    // other: X with no out-edge (3, 4), Y any other node: 3*2 = 6
    CHECK(db.relation(*p->relation("other")).size() == 6);
    CHECK(db.relation(*p->relation("same")).size() == 4);
    CHECK(db.relation(*p->relation("flag")).size() == 1);
}

static void test_errors() {
    std::string e;

    e = parse_error(".decl a(x: number)\na(X) :- b(X).\n");
    CHECK(has(e, "t.dl:2:") && has(e, "undeclared relation 'b'"));

    e = parse_error(".decl a(x: number)\n.decl b(x: number)\nb(1).\na(X) :- b(X), !a(X).\n");
    CHECK(has(e, "t.dl:4:") && has(e, "not stratifiable"));

    e = parse_error(".decl a(x: number)\n.decl b(x: number)\nb(1).\na(X) :- b(Y), !b(X).\n");
    CHECK(has(e, "variable X in a negated atom is not bound"));

    e = parse_error(".decl a(x: number)\n.decl b(x: number)\na(X) :- b(Y).\n");
    CHECK(has(e, "head variable X is not bound"));

    e = parse_error(".decl a(x: number)\n.decl s(x: symbol)\na(X) :- s(X).\n");
    CHECK(has(e, "variable X used as both symbol and number"));

    e = parse_error(".decl a(x: number)\na(\"str\").\n");
    CHECK(has(e, "string \"str\" in a number position"));

    e = parse_error(".decl a(x: number)\na(1, 2).\n");
    CHECK(has(e, "has 1 columns, used with 2"));

    e = parse_error(".decl a(x: number)\n.input a(IO=file)\n");
    CHECK(has(e, "IO parameters"));

    e = parse_error(".decl a(x: number)\na(X) :- a(X); a(X).\n");
    CHECK(has(e, "t.dl:2:"));

    e = parse_error("#include \"missing.dl\"\n");
    CHECK(has(e, "cannot resolve #include \"missing.dl\""));
}

static void test_include_and_types() {
    IncludeResolver inc = [](std::string_view name) -> std::optional<std::string> {
        if (name == "types.dl") return ".type Loan <: number\n.type Point <: symbol\n";
        if (name == "bad.dl") return ".type Loan <: number\n\nfoo bar\n";
        return std::nullopt;
    };
    Symbols syms;
    auto p = Program::parse("#include \"types.dl\"\n.decl issued(l: Loan, p: Point)\nissued(1, \"p0\").\n",
                            "main.dl", syms, inc);
    CHECK(p.has_value());
    if (p) CHECK(p->relations()[0].types[0] == ColType::Number &&
                 p->relations()[0].types[1] == ColType::Symbol);
    // An error inside an included file points at that file.
    std::string e = parse_error("#include \"bad.dl\"\n", inc);
    CHECK(has(e, "bad.dl:3:"));
}

// Driving a negation-free program incrementally must reach the same fixpoint
// as one run over all the rows.
static void test_incremental() {
    const char* src = R"(
.decl edge(a: number, b: number)
.input edge
.decl path(a: number, b: number)
path(X, Y) :- edge(X, Y).
path(X, Z) :- path(X, Y), edge(Y, Z).
)";
    const Value edges[][2] = {{1, 2}, {2, 3}, {5, 6}, {3, 4}, {4, 1}, {6, 7}, {7, 5}, {4, 5}};
    Symbols s1, s2;
    auto p1 = Program::parse(src, "inc.dl", s1);
    auto p2 = Program::parse(src, "inc.dl", s2);
    CHECK(p1 && p2 && !p1->has_negation());
    if (!p1 || !p2) return;
    uint32_t edge = *p1->relation("edge"), path = *p1->relation("path");
    Database once(*p1, s1), inc(*p2, s2);
    for (auto& e : edges) once.insert(edge, e);
    once.run();
    for (auto& e : edges) { inc.insert(edge, e); inc.run(); }
    CHECK(once.relation(path).size() == inc.relation(path).size());
    for (size_t i = 0; i < once.relation(path).size(); ++i)
        CHECK(inc.relation(path).contains(once.relation(path).row(i)));
    // 7 nodes, all mutually reachable after edge (4,5) joins the two cycles
    // one way: 4 nodes {1..4} reach all 7, 3 nodes {5,6,7} reach only {5,6,7}.
    CHECK(once.relation(path).size() == 4 * 7 + 3 * 3);
}

int main() {
    test_incremental();
    test_closure_and_provenance();
    test_negation_and_comparisons();
    test_errors();
    test_include_and_types();
    if (failures) {
        std::fprintf(stderr, "dl_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("dl_test: all checks passed");
    return 0;
}
