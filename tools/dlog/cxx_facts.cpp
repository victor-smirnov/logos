// cxx_facts — a GENERAL relational encoding of a C++ translation unit.
//
// ── WHY THIS REPLACES QUESTION-SHAPED EXTRACTION ────────────────────────────
// lir_facts.cpp emits the facts one particular question needs, so every new
// question is a C++ change and a rebuild. That is the small cost. The large one
// is measured: ALL THREE of its extraction bugs were bugs of OMISSION — the
// domain keyed on a grep of five names, callers left unfiltered, dispatch
// attributed for `case` but not `if`, so `try_path` contributed zero edges. In
// each case the fact was not wrong, it was ABSENT, because nobody asked for it.
//
// A complete, question-independent schema converts "I did not ask for that"
// into "my query is wrong". A wrong query is visible; an absent fact is not.
//
// ⚠ COMPLETENESS IS AFFORDABLE — the earlier measurement said otherwise and was
// misread. `-ast-dump=json` on one TU is 2.8 GB, but that is JSON verbosity, not
// information: the same tree as relations with integer-ish ids and no repeated
// key strings is two orders smaller. What must be cut is SYSTEM HEADERS, not
// detail.
//
// ── IDENTITY, and it decides whether 40 TUs can be merged ───────────────────
// DECLARATIONS are identified by the canonical declaration's SOURCE LOCATION —
// stable across translation units, so a function declared in a header is ONE
// entity no matter how many TUs see it. That is what makes `ref` and `call`
// joinable across a sweep.
// NODES (statements, expressions) are identified by a per-TU counter with the
// TU's tag, because they are TU-local by construction and pointers are not
// stable between runs.
//
// ⚠ AND IDENTITY IS THE POINT, not a detail. Keying on NAMES — which is what a
// grep can do and all lir_facts could do — is already half broken here:
// overloads, template instantiations, lambdas, and five separate enums named
// `Code` in lir_schema.hpp. `ref(Use, Decl)` cannot confuse two of them,
// because they are different nodes rather than equal strings.
//
//   node(Id, Kind, Parent, Index)   the tree, in order
//   loc(Id, File, Line, Col)
//   decl(DeclId, Kind, QualifiedName)
//   decl_name(DeclId, BareName)     the last component, for joins
//   decl_node(Id, DeclId)           this NODE is a declaration of that ENTITY
//   ref(UseId, DeclId)              a name use resolved to what it names
//   call(CallId, CalleeDeclId)      a call resolved to its callee
//   enum_member(EnumDeclId, ConstDeclId, Name)
//   type_of(Id, TypeId)             an expression's canonical type
//   type(TypeId, Class, Name)       Pointer / LValueReference / Record / Enum / …
//   type_pointee(TypeId, TypeId)    what a pointer or reference points at
//   type_decl(TypeId, DeclId)       a record/enum type's declaration
//   cast_kind(Id, Kind)             LValueToRValue, IntegralCast, NoOp, …
//   cfg_block(FnDeclId, B) / cfg_entry / cfg_exit / cfg_edge(FnDeclId, B, B2)
//   cfg_stmt(FnDeclId, B, NodeId)
//
// ⚠ THE CFG IS KEYED ON THE SAME NODE IDS AS THE AST, which lir_facts' was not:
// it emitted cfg_call(fnName, block, calleeName) and nothing could be joined to
// it. Here `cfg_stmt` names the very node `call` and `type_of` already talk
// about, so "which calls happen in this block" is a JOIN rather than a second
// extraction — and per-block anything comes free, not just calls.
//
// ⚠ TYPE IDENTITY IS THE CANONICAL TYPE'S SPELLING. Types have no source
// location to key on, and a pointer is not stable across TUs. The canonical
// spelling is stable, joinable, and is what a query wants to match anyway.
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include <fstream>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>

using namespace clang;

static llvm::cl::OptionCategory Cat("cxx-facts");
static llvm::cl::opt<std::string> OutDir("out", llvm::cl::desc("directory for .facts"),
                                         llvm::cl::Required, llvm::cl::cat(Cat));

namespace {

struct Out {
    std::ofstream node, loc, decl, decl_node, ref, call, enum_member, decl_name;
    std::ofstream type_of, type, type_pointee, type_decl, cast_kind;
    std::ofstream cfg_block, cfg_entry, cfg_exit, cfg_edge, cfg_stmt;
    long nodes = 0, decls = 0, refs = 0, calls = 0, types = 0, edges = 0;
    void flush() {
        node.flush(); loc.flush(); decl.flush(); decl_node.flush();
        ref.flush(); call.flush(); enum_member.flush(); decl_name.flush();
        type_of.flush(); type.flush(); type_pointee.flush(); type_decl.flush();
        cast_kind.flush(); cfg_block.flush(); cfg_entry.flush(); cfg_exit.flush();
        cfg_edge.flush(); cfg_stmt.flush();
    }
    void open(const std::string &d) {
        auto p = [&](const char *n) { return d + "/" + n + ".facts"; };
        node.open(p("node"));   loc.open(p("loc"));   decl.open(p("decl"));
        decl_node.open(p("decl_node")); ref.open(p("ref"));
        call.open(p("call"));   enum_member.open(p("enum_member"));
        decl_name.open(p("decl_name"));
        type_of.open(p("type_of")); type.open(p("type"));
        type_pointee.open(p("type_pointee")); type_decl.open(p("type_decl"));
        cast_kind.open(p("cast_kind"));
        cfg_block.open(p("cfg_block")); cfg_entry.open(p("cfg_entry"));
        cfg_exit.open(p("cfg_exit"));   cfg_edge.open(p("cfg_edge"));
        cfg_stmt.open(p("cfg_stmt"));
    }
};
Out g_out;
std::string g_tu;

// ⚠ A TAB OR A NEWLINE IN A NAME WOULD SILENTLY SHIFT EVERY COLUMN AFTER IT,
// and Souffle would load the misaligned row without complaint. Names reaching
// here can contain anything a template argument can contain.
std::string safe(std::string s) {
    for (char &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

class V : public RecursiveASTVisitor<V> {
public:
    explicit V(ASTContext &C) : ctx_(C), sm_(C.getSourceManager()) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return false; }

    bool TraverseDecl(Decl *D) {
        if (!D) return true;
        // ⚠ SKIPPING MUST NOT SKIP THE TRAVERSAL. The first cut returned early
        // for anything `skip()` rejected, and TranslationUnitDecl has an INVALID
        // location — so the guard killed the walk at the root and the tool wrote
        // seven empty files and exited 0. Emitting is one decision; descending
        // is another.
        if (skip(D->getLocation())) return RecursiveASTVisitor::TraverseDecl(D);
        std::string id = fresh(D);
        emit(id, D->getDeclKindName(), D->getLocation());
        if (const auto *ND = dyn_cast<NamedDecl>(D)) {
            // decl_id() emits the decl and its bare name on first sight; here
            // we only need the NODE-to-ENTITY link. ⚠ The bare name is emitted
            // from C++ rather than derived in Datalog: Souffle has no split and
            // peeling `::` there needs an ungrounded variable. The qualified
            // name is kept too — it is strictly more informative.
            std::string did = decl_id(ND);
            if (!did.empty()) g_out.decl_node << id << '\t' << did << '\n';
        }
        if (const auto *FD = dyn_cast<FunctionDecl>(D))
            if (FD->doesThisDeclarationHaveABody() && !decl_id(FD).empty())
                bodies_.emplace_back(FD, decl_id(FD));
        if (const auto *ED = dyn_cast<EnumDecl>(D))
            for (const auto *E : ED->enumerators())
                g_out.enum_member << decl_id(ED) << '\t' << decl_id(E) << '\t'
                                  << safe(E->getNameAsString()) << '\n';
        Frame f(*this, id);
        return RecursiveASTVisitor::TraverseDecl(D);
    }

    bool TraverseStmt(Stmt *S) {
        if (!S) return true;
        if (skip(S->getBeginLoc())) return RecursiveASTVisitor::TraverseStmt(S);
        std::string id = fresh(S);
        emit(id, S->getStmtClassName(), S->getBeginLoc());
        if (const auto *E = dyn_cast<Expr>(S)) {
            std::string tid = type_id(E->getType());
            if (!tid.empty()) g_out.type_of << id << '\t' << tid << '\n';
        }
        if (const auto *CE = dyn_cast<CastExpr>(S))
            g_out.cast_kind << id << '\t' << CE->getCastKindName() << '\n';
        if (const auto *DR = dyn_cast<DeclRefExpr>(S)) {
            std::string did = decl_id(DR->getDecl());
            if (!did.empty()) ++g_out.refs, g_out.ref << id << '\t' << did << '\n';
        }
        if (const auto *CE = dyn_cast<CallExpr>(S))
            if (const FunctionDecl *F = CE->getDirectCallee()) {
                std::string did = decl_id(F);
                if (!did.empty()) ++g_out.calls, g_out.call << id << '\t' << did << '\n';
            }
        Frame f(*this, id);
        return RecursiveASTVisitor::TraverseStmt(S);
    }

private:
    struct Frame {
        Frame(V &v, const std::string &id) : v_(v), p_(v.parent_), i_(v.index_) {
            v_.parent_ = id; v_.index_ = 0;
        }
        ~Frame() { v_.parent_ = p_; v_.index_ = i_; }
        V &v_; std::string p_; int i_;
    };

    // ⚠ SYSTEM HEADERS ARE THE WHOLE COST. This TU instantiates a great deal of
    // libstdc++, and a CFG/AST is built for every body the parser sees — the
    // same reason the first CFG run reported __stable_sort as a finding.
    bool skip(SourceLocation L) const {
        if (L.isInvalid()) return true;
        return sm_.isInSystemHeader(L) || sm_.isInExternCSystemHeader(L);
    }

    std::string fresh(const void *p) {
        std::string id = g_tu + "#" + std::to_string(next_++);
        node_of_[p] = id;          // the CFG pass needs to name the SAME nodes
        return id;
    }

    // Location of the CANONICAL declaration: the same entity gets the same id in
    // every TU that sees it, which is what makes a 40-TU sweep joinable.
    std::string decl_id(const Decl *D) {
        if (!D) return {};
        const Decl *C = D->getCanonicalDecl();
        auto P = sm_.getPresumedLoc(C->getLocation());
        if (!P.isValid()) return {};
        std::string id = llvm::sys::path::filename(P.getFilename()).str() + ":" +
                         std::to_string(P.getLine()) + ":" + std::to_string(P.getColumn());
        // ⚠ A REFERENCED DECLARATION GETS A ROW EVEN IF IT LIVES IN A SYSTEM
        // HEADER. The walk skips those bodies — that is the whole cost control —
        // but skipping the body must not make the ENTITY nameless: `out.push_back(c)`
        // resolves to a decl in <vector>, so `decl_name` had no row, and 268
        // arm_call rows silently lost their callee. Completeness of the schema
        // must not depend on where a thing is declared; filtering by that is the
        // QUERY's job, which is exactly what `structural` already does.
        // ⚠ AN EMPTY NAME IS NOT A NAME. Unnamed decls (anonymous structs,
        // parameters without an identifier, some template machinery) produced
        // 30 arm_call rows whose callee column was blank — rows that join with
        // nothing and read as data.
        if (const auto *ND = dyn_cast<NamedDecl>(C))
            if (!ND->getNameAsString().empty() && seen_decl_.insert(id).second) {
                ++g_out.decls;
                g_out.decl << id << '\t' << C->getDeclKindName() << '\t'
                           << safe(ND->getQualifiedNameAsString()) << '\n';
                g_out.decl_name << id << '\t' << safe(ND->getNameAsString()) << '\n';
            }
        return id;
    }

    // Canonical spelling as identity: no source location exists for a type, a
    // pointer is not stable across TUs, and the spelling is what a query
    // matches on anyway.
    std::string type_id(QualType Q) {
        if (Q.isNull()) return {};
        QualType C = Q.getCanonicalType();
        std::string n = safe(C.getAsString());
        if (n.empty()) return {};
        if (seen_type_.insert(n).second) {
            ++g_out.types;
            g_out.type << n << '\t' << C->getTypeClassName() << '\t' << n << '\n';
            if (const auto *PT = C->getAs<PointerType>())
                g_out.type_pointee << n << '\t' << type_id(PT->getPointeeType()) << '\n';
            else if (const auto *RT = C->getAs<ReferenceType>())
                g_out.type_pointee << n << '\t' << type_id(RT->getPointeeType()) << '\n';
            if (const auto *TD = C->getAsTagDecl()) {
                std::string did = decl_id(TD);
                if (!did.empty()) g_out.type_decl << n << '\t' << did << '\n';
            }
        }
        return n;
    }

    void emit(const std::string &id, const char *kind, SourceLocation L) {
        ++g_out.nodes;
        g_out.node << id << '\t' << kind << '\t' << (parent_.empty() ? "-" : parent_)
                   << '\t' << index_++ << '\n';
        auto P = sm_.getPresumedLoc(L);
        if (P.isValid())
            g_out.loc << id << '\t' << llvm::sys::path::filename(P.getFilename()).str()
                      << '\t' << P.getLine() << '\t' << P.getColumn() << '\n';
    }

public:
    // ⚠ THE CFG IS BUILT AFTER THE WALK, NOT DURING IT. A CFG names the
    // statements of a body, and their node ids do not exist until the body has
    // been traversed — emitting it from TraverseDecl would key the graph on
    // nodes that had not been numbered yet.
    void emit_cfgs() {
        for (auto &[FD, did] : bodies_) {
            CFG::BuildOptions opts;
            auto cfg = CFG::buildCFG(nullptr, FD->getBody(), &ctx_, opts);
            if (!cfg) continue;   // clang declines some bodies; a missing CFG is not a fact
            auto id = [&](const CFGBlock *B) { return std::to_string(B->getBlockID()); };
            g_out.cfg_entry << did << '\t' << id(&cfg->getEntry()) << '\n';
            g_out.cfg_exit  << did << '\t' << id(&cfg->getExit())  << '\n';
            for (const CFGBlock *B : *cfg) {
                if (!B) continue;
                g_out.cfg_block << did << '\t' << id(B) << '\n';
                for (const CFGBlock::AdjacentBlock &s : B->succs())
                    if (s.isReachable() && s.getReachableBlock()) {
                        ++g_out.edges;
                        g_out.cfg_edge << did << '\t' << id(B) << '\t'
                                       << id(s.getReachableBlock()) << '\n';
                    }
                for (const CFGElement &E : *B)
                    if (auto SE = E.getAs<CFGStmt>())
                        if (auto it = node_of_.find(SE->getStmt()); it != node_of_.end())
                            g_out.cfg_stmt << did << '\t' << id(B) << '\t'
                                           << it->second << '\n';
            }
        }
    }

private:
    ASTContext &ctx_;
    const SourceManager &sm_;
    std::unordered_map<const void *, std::string> node_of_;
    std::vector<std::pair<const FunctionDecl *, std::string>> bodies_;
    std::string parent_;
    int index_ = 0;
    long next_ = 0;
    std::set<std::string> seen_decl_;
    std::set<std::string> seen_type_;
};

class Action : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef F) override {
        g_tu = llvm::sys::path::stem(F).str();
        struct C : ASTConsumer {
            void HandleTranslationUnit(ASTContext &X) override {
                V v(X);
                v.TraverseDecl(X.getTranslationUnitDecl());
                v.emit_cfgs();
            }
        };
        return std::make_unique<C>();
    }
};
}  // namespace

int main(int argc, const char **argv) {
    auto Opt = tooling::CommonOptionsParser::create(argc, argv, Cat);
    if (!Opt) { llvm::errs() << toString(Opt.takeError()); return 2; }
    g_out.open(OutDir);
    tooling::ClangTool T(Opt->getCompilations(), Opt->getSourcePathList());
    int rc = T.run(tooling::newFrontendActionFactory<Action>().get());
    g_out.flush();
    // ⚠ AN EMPTY RESULT IS A REFUSAL, NOT A CLEAN RUN. The first version wrote
    // seven empty files and exited 0, and every downstream rule would have
    // reported a green tree over nothing. Silence is not an answer.
    // ⚠ THE FLOOR CATCHES "THE WALK DID NOT HAPPEN", WHICH MEANS ZERO — NOT
    // "SMALL". A threshold of 1000 rejected module_manifest.cpp, a genuinely
    // small TU at 693 nodes, on every run: the guard put here to stop a silent
    // lie (seven empty files, exit 0) started telling a different one, dropping
    // part of the subject while the caller reported a clean answer. Whether a
    // TOTAL is implausible is the aggregate's question, and ask.sh asks it.
    if (g_out.nodes == 0) {
        llvm::errs() << "cxx_facts: no nodes emitted — the walk did not happen\n";
        return 3;
    }
    llvm::outs() << "cxx_facts: " << g_out.nodes << " nodes, " << g_out.decls
                 << " decls, " << g_out.refs << " refs, " << g_out.calls << " calls, " << g_out.types
                 << " types, " << g_out.edges << " cfg edges\n";
    return rc;
}
