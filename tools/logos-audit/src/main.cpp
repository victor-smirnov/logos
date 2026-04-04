// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// logos-audit — enforces the Logos C++ subset rules:
//
//   [noexcept]      Every function returning logos::expected<> must be noexcept.
//   [no-exceptions] No try/catch blocks in production code.
//   [no-exceptions] No throw expressions in production code.
//   [no-get]        No logos::expected::get() calls in library code.
//                   (get() calls std::terminate(); use LOGOS_TRY instead.)
//
// "Production code" = src/ and include/ of the project, excluding
// exerciser*, walkthrough*, and test files.
//
// Usage:
//   logos-audit -p <build-dir> <files...>
//   logos-audit -p build src/hermes/template.cpp
//   run-clang-tidy -clang-tidy-binary logos-audit   # over whole project

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Custom matcher: true if the FunctionDecl has a noexcept exception spec.
AST_MATCHER(clang::FunctionDecl, isNoexceptFn) {
    if (const auto* FPT = Node.getType()->getAs<clang::FunctionProtoType>())
        return FPT->isNothrow();
    return false;
}

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

// ---------------------------------------------------------------------------
// File classification helpers
// ---------------------------------------------------------------------------

static bool isLogosSrc(StringRef path) {
    // Must be within the project's source or include tree.
    return path.contains("/logos/src/") || path.contains("/logos/include/");
}

static bool isExerciserOrTest(StringRef path) {
    return path.contains("exerciser") ||
           path.contains("walkthrough") ||
           path.contains("/test/")    ||
           path.ends_with("_test.cpp");
}

static bool isProductionCode(const SourceManager& SM, SourceLocation loc) {
    if (loc.isInvalid()) return false;
    StringRef path = SM.getFilename(SM.getExpansionLoc(loc));
    return isLogosSrc(path) && !isExerciserOrTest(path);
}

// ---------------------------------------------------------------------------
// Diagnostic emission
// ---------------------------------------------------------------------------

static int g_errors = 0;

// Deduplicate: clang processes headers multiple times (once per TU that
// includes them).  Track emitted (file:line:col:rule) tuples.
static llvm::DenseSet<unsigned> g_seen;

static void emit(const SourceManager& SM, SourceLocation loc,
                 StringRef rule, StringRef msg) {
    FullSourceLoc full(SM.getExpansionLoc(loc), SM);
    // Build a cheap hash: line * 1000003 ^ col * 31 ^ rule hash.
    unsigned key = full.getExpansionLineNumber() * 1000003u
                 ^ full.getExpansionColumnNumber() * 31u
                 ^ llvm::hash_value(rule)
                 ^ llvm::hash_value(SM.getFilename(full));
    if (!g_seen.insert(key).second) return;

    errs() << SM.getFilename(full)
           << ':' << full.getExpansionLineNumber()
           << ':' << full.getExpansionColumnNumber()
           << ": error [logos-" << rule << "]: " << msg << '\n';
    ++g_errors;
}

// ---------------------------------------------------------------------------
// Check 1 — expected<> without noexcept
//
// Every function or method whose return type is logos::expected<T> (or any
// specialisation of std::expected / logos::expected) must be noexcept.
// ---------------------------------------------------------------------------

class ExpectedNoexceptCheck : public MatchFinder::MatchCallback {
public:
    static void addTo(MatchFinder& f) {
        // Match function definitions returning any specialisation of "expected"
        // that are NOT marked noexcept.
        f.addMatcher(
            functionDecl(
                returns(qualType(hasDeclaration(
                    classTemplateSpecializationDecl(hasName("expected"))))),
                unless(isNoexceptFn()),
                isDefinition()
            ).bind("fn"),
            new ExpectedNoexceptCheck);
    }

    void run(const MatchFinder::MatchResult& R) override {
        auto* fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
        if (!isProductionCode(*R.SourceManager, fn->getLocation())) return;
        emit(*R.SourceManager, fn->getLocation(), "noexcept",
             "function returning expected<> must be noexcept");
    }
};

// ---------------------------------------------------------------------------
// Check 2 — try/catch in production code
// ---------------------------------------------------------------------------

class NoTryCatchCheck : public MatchFinder::MatchCallback {
public:
    static void addTo(MatchFinder& f) {
        f.addMatcher(cxxTryStmt().bind("try"), new NoTryCatchCheck);
    }

    void run(const MatchFinder::MatchResult& R) override {
        auto* stmt = R.Nodes.getNodeAs<CXXTryStmt>("try");
        if (!isProductionCode(*R.SourceManager, stmt->getBeginLoc())) return;
        emit(*R.SourceManager, stmt->getBeginLoc(), "no-exceptions",
             "try/catch not allowed in production code (logos uses -fno-exceptions)");
    }
};

// ---------------------------------------------------------------------------
// Check 3 — throw in production code
// ---------------------------------------------------------------------------

class NoThrowCheck : public MatchFinder::MatchCallback {
public:
    static void addTo(MatchFinder& f) {
        f.addMatcher(cxxThrowExpr().bind("throw"), new NoThrowCheck);
    }

    void run(const MatchFinder::MatchResult& R) override {
        auto* expr = R.Nodes.getNodeAs<CXXThrowExpr>("throw");
        if (!isProductionCode(*R.SourceManager, expr->getBeginLoc())) return;
        emit(*R.SourceManager, expr->getBeginLoc(), "no-exceptions",
             "throw not allowed in production code (logos uses -fno-exceptions)");
    }
};

// ---------------------------------------------------------------------------
// Check 4 — logos::expected::get() in library code
//
// get() now calls std::terminate() — safe as a forced-unwrap in exercisers,
// but must not appear in library code.  Use LOGOS_TRY / LOGOS_TRY_VOID.
// ---------------------------------------------------------------------------

class NoExpectedGetCheck : public MatchFinder::MatchCallback {
public:
    static void addTo(MatchFinder& f) {
        f.addMatcher(
            cxxMemberCallExpr(
                callee(cxxMethodDecl(
                    hasName("get"),
                    ofClass(classTemplateSpecializationDecl(hasName("expected")))
                ))
            ).bind("call"),
            new NoExpectedGetCheck);
    }

    void run(const MatchFinder::MatchResult& R) override {
        auto* call = R.Nodes.getNodeAs<CXXMemberCallExpr>("call");
        if (!isProductionCode(*R.SourceManager, call->getBeginLoc())) return;
        emit(*R.SourceManager, call->getBeginLoc(), "no-get",
             "expected::get() terminates on error — use LOGOS_TRY / LOGOS_TRY_VOID");
    }
};

// ---------------------------------------------------------------------------
// Check 5 — non-noexcept function that silently swallows errors
//
// A function returning plain void (not expected<void>) that is also not
// noexcept has no way to report failures to its caller.  Flag it so that
// the author makes a conscious choice: either add noexcept (if truly
// infallible) or change the return type to expected<void> noexcept.
//
// Heuristic: flag void functions that are NOT noexcept and are NOT:
//   - constructors / destructors (often legitimately void+throwing)
//   - override / virtual (signature is fixed by the base)
//   - main()
// ---------------------------------------------------------------------------

class VoidNotNoexceptCheck : public MatchFinder::MatchCallback {
public:
    static void addTo(MatchFinder& f) {
        f.addMatcher(
            functionDecl(
                returns(voidType()),
                unless(isNoexceptFn()),
                unless(cxxConstructorDecl()),
                unless(cxxDestructorDecl()),
                unless(cxxMethodDecl(isOverride())),
                unless(cxxMethodDecl(isVirtual())),
                unless(hasName("main")),
                isDefinition()
            ).bind("fn"),
            new VoidNotNoexceptCheck);
    }

    void run(const MatchFinder::MatchResult& R) override {
        auto* fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
        if (!isProductionCode(*R.SourceManager, fn->getLocation())) return;
        emit(*R.SourceManager, fn->getLocation(), "noexcept",
             "void function is not noexcept — add noexcept or return expected<void>");
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static cl::OptionCategory AuditCategory("logos-audit options");

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "Examples:\n"
    "  logos-audit -p build src/hermes/template.cpp\n"
    "  logos-audit -p build include/logos/hrpc/schema.hpp\n"
);

int main(int argc, const char** argv) {
    auto parser = CommonOptionsParser::create(argc, argv, AuditCategory);
    if (!parser) {
        errs() << toString(parser.takeError());
        return 1;
    }

    ClangTool tool(parser->getCompilations(), parser->getSourcePathList());

    // The tool is built against a specific Clang installation.  Inject its
    // resource directory so system headers (stddef.h etc.) are found correctly.
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-resource-dir=" LOGOS_CLANG_RESOURCE_DIR,
                                  ArgumentInsertPosition::BEGIN));

    MatchFinder finder;
    ExpectedNoexceptCheck::addTo(finder);
    NoTryCatchCheck::addTo(finder);
    NoThrowCheck::addTo(finder);
    NoExpectedGetCheck::addTo(finder);
    VoidNotNoexceptCheck::addTo(finder);

    int rc = tool.run(newFrontendActionFactory(&finder).get());

    if (g_errors > 0) {
        errs() << '\n' << g_errors << " logos-audit violation(s) found.\n";
        return 1;
    }
    return rc;
}
