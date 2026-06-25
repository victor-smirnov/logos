// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_debug.cpp — DWARF debug-info (-g) emission for MLIRGen.
//
// Strategy: attach MLIR LLVM-dialect debug-info attributes (DICompileUnit /
// DISubprogram / DIFile) and per-statement FileLineColLoc locations fused with
// the current function's DISubprogram. translateModuleToLLVMIR then lowers these
// to LLVM debug metadata, and the LLVM backend emits standard DWARF — so stock
// gdb/lldb get source-level line tables (Stage 1), and later locals/types.
//
// Locations are debug-fused ONLY while emitting a TOP-LEVEL user function body
// (begin_fn_debug … end_fn_debug). Nested compiler-generated functions
// (closures, drop glue, ctors) suspend the scope via DebugScopeSuspend so a
// single DISubprogram never attaches to two LLVM functions (which the LLVM
// verifier rejects and which produces nonsensical DWARF).

#include "mlir_gen_impl.hpp"

#include <mlir/IR/Location.h>

#include <llvm/Support/Path.h>

namespace logos::compiler {

using namespace lir;

namespace {
// DWARF source language for the compile unit. Logos is Rust-shaped but we keep
// language-neutral C semantics here; gdb display is driven by our own
// pretty-printers (Stage 4/5), not by language-specific DWARF heuristics.
constexpr unsigned kDwarfLangC99 = 0x000c;  // DW_LANG_C99
} // namespace

mlir::LLVM::DIFileAttr MLIRGenImpl::di_file_for(std::string_view path) {
    std::string key(path);
    if (auto it = di_files_.find(key); it != di_files_.end()) return it->second;
    llvm::StringRef p(key);
    llvm::StringRef name = llvm::sys::path::filename(p);
    llvm::StringRef dir  = llvm::sys::path::parent_path(p);
    if (name.empty()) name = "<logos>";
    auto f = mlir::LLVM::DIFileAttr::get(builder_.getContext(), name, dir);
    di_files_.emplace(std::move(key), f);
    return f;
}

mlir::LLVM::DICompileUnitAttr
MLIRGenImpl::ensure_di_cu(mlir::LLVM::DIFileAttr file) {
    if (di_cu_) return di_cu_;
    auto* ctx = builder_.getContext();
    auto id = mlir::DistinctAttr::create(mlir::UnitAttr::get(ctx));
    di_cu_ = mlir::LLVM::DICompileUnitAttr::get(
        ctx, id, kDwarfLangC99, file,
        mlir::StringAttr::get(ctx, "logosc"),
        /*isOptimized=*/false,
        mlir::LLVM::DIEmissionKind::Full,
        mlir::LLVM::DINameTableKind::Default);
    return di_cu_;
}

void MLIRGenImpl::begin_fn_debug(mlir::func::FuncOp func, lir_view::FunctionView fn) {
    di_subprogram_ = {};
    di_file_       = {};
    di_scope_line_ = 0;
    loc_ = builder_.getUnknownLoc();
    if (!debug_info_) return;

    std::string src(fn.source_file());
    if (src.empty()) src = main_source_;     // single-file pipeline: per-fn path empty
    if (src.empty()) src = "<logos>";
    auto* ctx = builder_.getContext();
    di_file_ = di_file_for(src);
    auto cu  = ensure_di_cu(di_file_);

    // Function decl/scope line: the LIR has no decl line on the function, so use
    // the first body statement's source line (the closest available anchor).
    uint32_t scope_line = 0;
    fn.body().each_stmt([&](lir_view::StmtRef s) {
        if (scope_line == 0 && s.line() != 0) scope_line = s.line();
    });
    if (scope_line == 0) scope_line = 1;
    di_scope_line_ = scope_line;

    // Readable name for DWARF `name` (gdb display + `break <fn>`). method_base
    // is the unmangled source name (e.g. "add"); fall back to the raw symbol.
    //
    // We deliberately DO NOT set DW_AT_linkage_name: Logos mangled symbols carry
    // `$` (module suffixes, type-arg packs) which gdb (C language) cannot
    // demangle, so it would register the function under the mangled name and
    // break `break add`. The mangled symbol still lives in the ELF symtab (so
    // `break <mangled>` works) and gdb correlates the subprogram by address.
    std::string bare(fn.method_base());
    if (bare.empty()) bare = std::string(fn.name());
    auto flags = mlir::LLVM::DISubprogramFlags::Definition;
    if (bare == "main" || fn.name() == "main")
        flags = flags | mlir::LLVM::DISubprogramFlags::MainSubprogram;

    // Stage 1: empty subroutine type (line tables only). Stage 2 fills real
    // parameter/return DI types so `ptype`/`whatis` work.
    auto sub_type = mlir::LLVM::DISubroutineTypeAttr::get(
        ctx, /*callingConvention=*/0, llvm::ArrayRef<mlir::LLVM::DITypeAttr>{});

    auto sp = mlir::LLVM::DISubprogramAttr::get(
        ctx,
        /*recId=*/mlir::DistinctAttr{},
        /*isRecSelf=*/false,
        /*id=*/mlir::DistinctAttr::create(mlir::UnitAttr::get(ctx)),
        /*compileUnit=*/cu,
        /*scope=*/di_file_,
        /*name=*/mlir::StringAttr::get(ctx, bare),
        /*linkageName=*/mlir::StringAttr{},   // see note above (gdb `$` demangle)
        /*file=*/di_file_,
        /*line=*/scope_line,
        /*scopeLine=*/scope_line,
        /*subprogramFlags=*/flags,
        /*type=*/sub_type,
        /*retainedNodes=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
        /*annotations=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{});
    di_subprogram_ = sp;

    // Attach the subprogram to the function via a fused location so the LLVM
    // translation sets llvmFunc->setSubprogram(). The underlying point loc gives
    // the function's source line.
    auto fl = mlir::FileLineColLoc::get(ctx, di_file_.getName(), scope_line, 0);
    func->setLoc(mlir::FusedLoc::get(ctx, {mlir::Location(fl)}, sp));

    // Body ops emitted before the first statement (param binding, prologue) get
    // the function-scope location so any call there carries a valid !dbg.
    loc_ = dbg_loc(scope_line);
}

void MLIRGenImpl::end_fn_debug() {
    di_subprogram_ = {};
    di_file_       = {};
    di_scope_line_ = 0;
    loc_ = builder_.getUnknownLoc();
}

mlir::Location MLIRGenImpl::dbg_loc(uint32_t line) {
    if (!debug_info_ || !di_subprogram_) return builder_.getUnknownLoc();
    if (line == 0) line = di_scope_line_ ? di_scope_line_ : 1;
    auto* ctx = builder_.getContext();
    auto fl = mlir::FileLineColLoc::get(ctx, di_file_.getName(), line, 0);
    return mlir::FusedLoc::get(ctx, {mlir::Location(fl)}, di_subprogram_);
}

} // namespace logos::compiler
