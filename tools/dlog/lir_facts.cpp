// lir_facts — emit Datalog facts about how the compiler DISPATCHES over the
// LIR schema enums, read from the AST rather than from the text.
//
// ── WHY THIS REPLACED A grep/awk EXTRACTOR ──────────────────────────────────
// The shell version worked and was wrong in three measurable ways:
//
//  1. `handles` meant "the body MENTIONS the code". `case SliceIndex: break;`
//     counted as handled. The tool that finds MISSING arms could not see an
//     EMPTY one. Here a case label carries its body: case_live vs case_empty.
//
//  2. The domain was unqualified. lir_schema.hpp declares FIVE enums named
//     `Code` — expr(42), stmt(22), writ_val(9), pat(13), decl(14) — and
//     `grep -oE "::(TupleIndex|...)"` cannot say whose. Measured 2026-08-26:
//     no projection name currently appears in another Code enum, so the grep
//     was LUCKY, not correct; the day someone adds `pat::Code::Deref` it starts
//     lying silently. Here the enum is named by its FULLY QUALIFIED name.
//
//  3. Bodies were cut by brace balance from a grepped definition line. And
//     `try_path` is not a function: it is a std::function assigned a lambda on
//     the next line. The awk worked by accident. Here the context is the AST's.
//
// ── WHAT IT EMITS, AND WHAT IT REFUSES TO DECIDE ────────────────────────────
// Facts are MECHANICAL and GENERAL: every context that tests any code of the
// named enum. Which contexts are "place walkers" and which codes are
// "projections" are CLAIMS, and they stay in Datalog where they can be checked.
// The extractor knowing about walkers is what let the old one certify a
// function complete against a domain that shared its author's blind spot.
//
//   enum_code(Kind)                 — every enumerator, in declaration order
//   tests(Context, Kind, Position)  — Position: case_live | case_empty
//                                                | cond | mention
//
// Build with tools/dlog/make.sh; the whole chain is bite-proved by selftest.sh
// against revision 28fc7c75, where six defects are already known.
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace clang;

static llvm::cl::OptionCategory Cat("lir-facts");
static llvm::cl::opt<std::string> EnumName(
    "enum", llvm::cl::desc("fully qualified enum, e.g. ::logos::compiler::lir_schema::expr::Code"),
    llvm::cl::Required, llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> OutDir("out", llvm::cl::desc("directory for .facts"),
                                         llvm::cl::Required, llvm::cl::cat(Cat));

namespace {

// Collected across translation units; written once at exit so a second TU
// cannot truncate the first one's facts.
std::set<std::string> g_codes_seen;
std::vector<std::string> g_codes_ordered;
std::set<std::string> g_tests;  // "ctx\tkind\tpos"

class V : public RecursiveASTVisitor<V> {
public:
    explicit V(ASTContext &C) : ctx_(C) {}

    // ── the domain, from the enum itself ────────────────────────────────────
    bool VisitEnumDecl(EnumDecl *E) {
        if (E->getQualifiedNameAsString() != strip(EnumName)) return true;
        for (auto *C : E->enumerators())
            if (g_codes_seen.insert(C->getNameAsString()).second)
                g_codes_ordered.push_back(C->getNameAsString());
        return true;
    }

    // ── contexts: named functions, and lambdas named for their variable ─────
    bool TraverseFunctionDecl(FunctionDecl *F) {
        Scope s(*this, F->getNameAsString());
        return RecursiveASTVisitor::TraverseFunctionDecl(F);
    }
    bool TraverseCXXMethodDecl(CXXMethodDecl *M) {
        // A lambda's operator() carries no useful name of its own; the name was
        // pushed by TraverseLambdaExpr, so do not shadow it with "operator()".
        if (M->getParent() && M->getParent()->isLambda())
            return RecursiveASTVisitor::TraverseCXXMethodDecl(M);
        Scope s(*this, M->getNameAsString());
        return RecursiveASTVisitor::TraverseCXXMethodDecl(M);
    }
    bool TraverseLambdaExpr(LambdaExpr *L) {
        Scope s(*this, lambda_name(L));
        return RecursiveASTVisitor::TraverseLambdaExpr(L);
    }

    // ── the three positions ─────────────────────────────────────────────────
    bool VisitCaseStmt(CaseStmt *S) {
        if (auto k = kind_of(S->getLHS())) {
            // ⚠ THE DISTINCTION THE grep COULD NOT MAKE. A case label with no
            // body handles nothing, and reporting it as handled is how a missing
            // arm hides in plain sight.
            //
            // ⚠ BUT FALLING THROUGH IS NOT NOTHING, and the first cut got this
            // backwards. `case EnumLit: case EnumLitData: <body>` shares one arm
            // between two codes; treating the first as empty produced three
            // confident FALSE findings in retype_aggregate_lit_to on the very
            // first run. Follow the chain to the body that actually runs.
            const Stmt *B = S->getSubStmt();
            while (const auto *N = dyn_cast_or_null<SwitchCase>(B)) B = N->getSubStmt();
            bool live = B && !isa<NullStmt>(B) &&
                        !(isa<CompoundStmt>(B) && cast<CompoundStmt>(B)->body_empty());
            record(*k, live ? "case_live" : "case_empty");
            seen_.insert(S->getLHS()->IgnoreParenImpCasts());
        }
        return true;
    }
    bool VisitBinaryOperator(BinaryOperator *B) {
        if (B->getOpcode() != BO_EQ && B->getOpcode() != BO_NE) return true;
        for (const Expr *E : {B->getLHS(), B->getRHS()})
            if (auto k = kind_of(E)) { record(*k, "cond"); seen_.insert(E->IgnoreParenImpCasts()); }
        return true;
    }
    bool VisitDeclRefExpr(DeclRefExpr *D) {
        if (seen_.count(D)) return true;  // already classified as case/cond
        if (auto k = kind_of(D)) record(*k, "mention");
        return true;
    }

private:
    static std::string strip(std::string s) {  // "::a::b" -> "a::b"
        return s.rfind("::", 0) == 0 ? s.substr(2) : s;
    }

    std::optional<std::string> kind_of(const Expr *E) {
        if (!E) return std::nullopt;
        const auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
        if (!D) return std::nullopt;
        const auto *EC = dyn_cast<EnumConstantDecl>(D->getDecl());
        if (!EC) return std::nullopt;
        const auto *ED = dyn_cast<EnumDecl>(EC->getDeclContext());
        if (!ED || ED->getQualifiedNameAsString() != strip(EnumName)) return std::nullopt;
        return EC->getNameAsString();
    }

    std::string lambda_name(LambdaExpr *L) {
        // `std::function<...> f; f = [&](...){...}` — the lambda's own name is
        // nothing; the name a reader means is the variable it lands in.
        // ⚠ THE ASSIGNMENT IS NOT THE IMMEDIATE PARENT. `f = [&]{...}` on a
        // std::function wraps the lambda in a CXXConstructExpr and a
        // MaterializeTemporaryExpr first, so a one-level parent lookup found
        // nothing and try_path — the walker this whole tool was built to check —
        // emitted ZERO facts on the first run. Climb, with a bound.
        const Stmt *N = L;
        for (int depth = 0; N && depth < 8; ++depth) {
            const Stmt *up = nullptr;
            for (const auto &P : ctx_.getParents(*N)) {
                if (const auto *A = P.get<BinaryOperator>())
                    if (A->isAssignmentOp())
                        if (const auto *R = dyn_cast<DeclRefExpr>(A->getLHS()->IgnoreParenImpCasts()))
                            return R->getDecl()->getNameAsString();
                if (const auto *O = P.get<CXXOperatorCallExpr>())
                    if (O->getOperator() == OO_Equal && O->getNumArgs() == 2)
                        if (const auto *R = dyn_cast<DeclRefExpr>(O->getArg(0)->IgnoreParenImpCasts()))
                            return R->getDecl()->getNameAsString();
                if (const auto *VD = P.get<VarDecl>()) return VD->getNameAsString();
                if (!up) up = P.get<Stmt>();
            }
            N = up;
        }
        return "";  // an anonymous lambda is not a context anyone can claim
    }

    // An RAII scope, not a wrapper around the traversal: RecursiveASTVisitor has
    // no single "traverse this node" entry point, and inventing one is what the
    // first cut tried. The visitor keeps its own recursion; only cur_ is saved.
    struct Scope {
        Scope(V &v, std::string n) : v_(v), saved_(v.cur_) {
            if (n.empty()) return;  // an anonymous lambda is a context nobody can claim
            v_.cur_ = n;
        }
        ~Scope() { v_.cur_ = saved_; }
        V &v_;
        std::string saved_;
    };


    void record(const std::string &kind, const char *pos) {
        if (cur_.empty()) return;
        g_tests.insert(cur_ + "\t" + kind + "\t" + pos);
    }

    ASTContext &ctx_;
    std::string cur_;
    std::set<const Expr *> seen_;
};

class Action : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
        struct C : ASTConsumer {
            void HandleTranslationUnit(ASTContext &X) override {
                V(X).TraverseDecl(X.getTranslationUnitDecl());
            }
        };
        return std::make_unique<C>();
    }
};

void write(const std::string &name, const std::vector<std::string> &rows) {
    std::ofstream o(OutDir + "/" + name);
    for (const auto &r : rows) o << r << "\n";
}
}  // namespace

int main(int argc, const char **argv) {
    auto Opt = tooling::CommonOptionsParser::create(argc, argv, Cat);
    if (!Opt) { llvm::errs() << toString(Opt.takeError()); return 2; }
    tooling::ClangTool T(Opt->getCompilations(), Opt->getSourcePathList());
    if (T.run(tooling::newFrontendActionFactory<Action>().get()) != 0)
        llvm::errs() << "lir_facts: the parse reported errors; facts may be partial\n";

    // ⚠ AN EMPTY DOMAIN IS A REFUSAL, NOT A GREEN RESULT. If the enum was not
    // found — renamed, moved, misspelled on the command line — every downstream
    // rule has nothing to demand and reports a clean tree. Silence is not an
    // answer; make it an exit code instead.
    if (g_codes_ordered.size() < 20) {
        llvm::errs() << "lir_facts: found " << g_codes_ordered.size() << " enumerators of "
                     << EnumName << " — refusing to write a domain that small\n";
        return 3;
    }
    write("expr_code.facts", g_codes_ordered);
    write("tests.facts", {g_tests.begin(), g_tests.end()});
    llvm::outs() << "lir_facts: " << g_codes_ordered.size() << " codes, " << g_tests.size()
                 << " tests\n";
    return 0;
}
