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
//   arm_call(Context, Kind, Callee)  — a call made INSIDE the arm for Kind
//   callee_home(Callee, File)        — where the callee is DEFINED
//   cfg_edge(Fn, From, To)           — control-flow edges, per function
//   cfg_entry(Fn, B) / cfg_exit(Fn, B)
//   cfg_call(Fn, B, Callee)          — a call made IN that basic block
//   fn_home(Fn, File)                — where the FUNCTION is defined
//
// ⚠ THE CFG IS HERE BECAUSE arm_divergence FAILED. Asking "which sibling arms
// call P" found nothing sharp: the arms of one switch are not peers, fifteen
// expression kinds have fifteen jobs. The question that actually matches the
// defects fixed this week is "does P DOMINATE every path to the exit" — a call
// that was not made is a PATH that avoids it, and a path is not a syntax tree.
// Reachability-avoiding-P is transitive closure, which is exactly the operation
// I approximate by sampling and Souffle computes by construction.
//
// arm_call is what turns "handles k of 5" into "four arms ask the checker and
// the fifth does not". Seven of eight borrow-check fixes in one week were a
// CALL THAT WAS NOT MADE, not missing machinery — field_borrow_conflicts existed
// and was reached from four mutating sites out of five. That defect has no
// spelling to grep for; it is a hole in a relation, and a hole in a relation is
// derivable. And unlike "which functions are walkers", it needs NO claim: the
// siblings of one switch are their own domain.
//
// Build with tools/dlog/make.sh; the whole chain is bite-proved by selftest.sh
// against revision 28fc7c75, where six defects are already known.
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
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
std::set<std::string> g_tests;      // "ctx\tkind\tpos"
std::set<std::string> g_arm_calls;  // "ctx\tkind\tcallee" — a call INSIDE an arm
std::set<std::string> g_callee_home;  // "callee\tdefining-file"
std::set<std::string> g_cfg_edge;     // "fn\tfrom\tto"
std::set<std::string> g_cfg_entry;    // "fn\tblock"
std::set<std::string> g_cfg_exit;     // "fn\tblock"
std::set<std::string> g_cfg_call;     // "fn\tblock\tcallee"
std::set<std::string> g_fn_home;      // "fn\tdefining-file" — is F ours at all?

// ⚠ callee_home MUST BE FED FROM EVERY CALL, not just the ones inside a
// dispatch arm. The first CFG run filtered nothing: `structural(P)` is derived
// from callee_home, and callees seen only through the CFG had no row, so
// `__platform_wait`, `__stable_sort` and `vprint_nonunicode` came back as
// findings. A filter defined over a table only filters what the table covers.
void note_home(ASTContext &C, const NamedDecl *D, std::set<std::string> &out) {
    if (!D) return;
    auto P = C.getSourceManager().getPresumedLoc(D->getLocation());
    if (!P.isValid()) return;
    out.insert(D->getNameAsString() + "\t" +
               llvm::sys::path::filename(P.getFilename()).str());
}

// Built per function body, outside the visitor: clang's CFG is a separate
// analysis over a Stmt, not something a RecursiveASTVisitor produces.
void emit_cfg(ASTContext &C, const std::string &fn, Stmt *body) {
    if (!body || fn.empty()) return;
    CFG::BuildOptions opts;
    auto cfg = CFG::buildCFG(nullptr, body, &C, opts);
    if (!cfg) return;   // clang declines some bodies; a missing CFG is not a fact
    auto id = [&](const CFGBlock *B) { return std::to_string(B->getBlockID()); };
    g_cfg_entry.insert(fn + "\t" + id(&cfg->getEntry()));
    g_cfg_exit.insert(fn + "\t" + id(&cfg->getExit()));
    for (const CFGBlock *B : *cfg) {
        if (!B) continue;
        for (const CFGBlock::AdjacentBlock &succ : B->succs())
            if (succ.isReachable() && succ.getReachableBlock())
                g_cfg_edge.insert(fn + "\t" + id(B) + "\t" + id(succ.getReachableBlock()));
        for (const CFGElement &E : *B) {
            auto SE = E.getAs<CFGStmt>();
            if (!SE) continue;
            const auto *CE = dyn_cast_or_null<CallExpr>(SE->getStmt());
            if (!CE) continue;
            if (const FunctionDecl *D = CE->getDirectCallee()) {
                g_cfg_call.insert(fn + "\t" + id(B) + "\t" + D->getNameAsString());
                note_home(C, D, g_callee_home);
            }
        }
    }
}

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
        if (F->doesThisDeclarationHaveABody() && !F->getNameAsString().empty()) {
            note_home(ctx_, F, g_fn_home);
            emit_cfg(ctx_, F->getNameAsString(), F->getBody());
        }
        return RecursiveASTVisitor::TraverseFunctionDecl(F);
    }
    bool TraverseCXXMethodDecl(CXXMethodDecl *M) {
        // A lambda's operator() carries no useful name of its own; the name was
        // pushed by TraverseLambdaExpr, so do not shadow it with "operator()".
        if (M->getParent() && M->getParent()->isLambda())
            return RecursiveASTVisitor::TraverseCXXMethodDecl(M);
        Scope s(*this, M->getNameAsString());
        if (M->doesThisDeclarationHaveABody() && !M->getNameAsString().empty()) {
            note_home(ctx_, M, g_fn_home);
            emit_cfg(ctx_, M->getNameAsString(), M->getBody());
        }
        return RecursiveASTVisitor::TraverseCXXMethodDecl(M);
    }
    bool TraverseLambdaExpr(LambdaExpr *L) {
        Scope s(*this, lambda_name(L));
        return RecursiveASTVisitor::TraverseLambdaExpr(L);
    }

    // ── which arm a call sits in ────────────────────────────────────────────
    // ⚠ LEXICAL NESTING DOES NOT ANSWER THIS. In clang's AST an UNBRACED arm's
    // statements are SIBLINGS of the CaseStmt inside the switch's CompoundStmt,
    // not its children — `case A: f(); g(); break;` puts only `f()` under the
    // CaseStmt. So walk the body in ORDER and carry the active labels forward,
    // which also gives fall-through groups (`case A: case B: <body>`) the right
    // answer: both kinds own the body.
    bool TraverseSwitchStmt(SwitchStmt *S) {
        if (!TraverseStmt(S->getCond())) return false;
        auto *B = dyn_cast_or_null<CompoundStmt>(S->getBody());
        if (!B) return RecursiveASTVisitor::TraverseSwitchStmt(S);
        auto saved = arm_;
        for (Stmt *child : B->body()) {
            Stmt *cur = child;
            if (isa<SwitchCase>(cur)) {
                arm_.clear();
                while (auto *SC = dyn_cast_or_null<SwitchCase>(cur)) {
                    if (auto *CS = dyn_cast<CaseStmt>(SC))
                        if (auto k = kind_of(CS->getLHS())) arm_.push_back(*k);
                    VisitSwitchCaseLabel(SC);
                    cur = SC->getSubStmt();
                }
            }
            if (cur && !TraverseStmt(cur)) { arm_ = saved; return false; }
        }
        arm_ = saved;
        return true;
    }

    // ⚠ AN `if` CHAIN IS A DISPATCHER TOO, AND THE FIRST CUT COULD NOT SEE ONE.
    // Only switch arms were attributed, so 25 contexts had call edges and
    // `try_path` — written as `if (e.kind() == EC::VarRef) … if (… FieldRead)` —
    // contributed NOTHING. The detector for enumeration had enumerated the two
    // spellings of dispatch and kept one. The THEN branch of a kind test is an
    // arm of that kind, exactly as a case label's body is.
    bool TraverseIfStmt(IfStmt *S) {
        std::vector<std::string> ks;
        if (const auto *B = dyn_cast_or_null<BinaryOperator>(
                S->getCond() ? S->getCond()->IgnoreParenImpCasts() : nullptr))
            if (B->getOpcode() == BO_EQ)
                for (const Expr *E : {B->getLHS(), B->getRHS()})
                    if (auto k = kind_of(E)) ks.push_back(*k);
        if (S->getInit() && !TraverseStmt(S->getInit())) return false;
        if (S->getCond() && !TraverseStmt(S->getCond())) return false;
        bool ok = true;
        {
            auto saved = arm_;
            if (!ks.empty()) arm_ = ks;
            ok = !S->getThen() || TraverseStmt(S->getThen());
            arm_ = saved;
        }
        // The ELSE branch is NOT an arm of K — it is where K is known false.
        if (ok && S->getElse()) ok = TraverseStmt(S->getElse());
        return ok;
    }

    bool VisitCallExpr(CallExpr *C) {
        const FunctionDecl *D = C->getDirectCallee();
        if (!D || arm_.empty() || cur_.empty()) return true;
        std::string callee = D->getNameAsString();
        for (const auto &k : arm_) g_arm_calls.insert(cur_ + "\t" + k + "\t" + callee);
        // ⚠ WHERE A CALLEE LIVES IS THE ONLY MECHANICAL WAY TO TELL A CHECK
        // FROM AN ACCESSOR. The first run of arm_divergence drowned: its seven
        // sharpest rows were `push_back`, `operand`, `receiver`, `operator=` —
        // structural readers from lir_view.hpp, whose absence from an arm means
        // only that the arm has a different shape. Filtering them by NAME would
        // be the enumeration disease again; filtering by defining file is a
        // property, and it moves with the code.
        auto &SM = ctx_.getSourceManager();
        auto P = SM.getPresumedLoc(D->getLocation());
        if (P.isValid())
            g_callee_home.insert(callee + "\t" +
                                 llvm::sys::path::filename(P.getFilename()).str());
        return true;
    }

    // ── the three positions ─────────────────────────────────────────────────
    void VisitSwitchCaseLabel(SwitchCase *SC) {
        if (auto *S = dyn_cast<CaseStmt>(SC)) VisitCaseStmt(S);
    }
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
    std::vector<std::string> arm_;   // the case kinds whose arm we are inside
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
    write("arm_call.facts", {g_arm_calls.begin(), g_arm_calls.end()});
    write("callee_home.facts", {g_callee_home.begin(), g_callee_home.end()});
    write("cfg_edge.facts",  {g_cfg_edge.begin(),  g_cfg_edge.end()});
    write("cfg_entry.facts", {g_cfg_entry.begin(), g_cfg_entry.end()});
    write("cfg_exit.facts",  {g_cfg_exit.begin(),  g_cfg_exit.end()});
    write("cfg_call.facts",  {g_cfg_call.begin(),  g_cfg_call.end()});
    write("fn_home.facts",   {g_fn_home.begin(),   g_fn_home.end()});
    llvm::outs() << "lir_facts: " << g_codes_ordered.size() << " codes, " << g_tests.size()
                 << " tests, "
                 << g_arm_calls.size() << " arm calls, " << g_cfg_edge.size()
                 << " cfg edges\n";
    return 0;
}
