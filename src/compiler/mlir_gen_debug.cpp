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
#include <mlir/Interfaces/DataLayoutInterfaces.h>

#include <llvm/Support/Path.h>

namespace logos::compiler {

using namespace lir;

namespace {
// DWARF source language for the compile unit. Logos is Rust-shaped but we keep
// language-neutral C semantics here; gdb display is driven by our own
// pretty-printers (Stage 4/5), not by language-specific DWARF heuristics.
constexpr unsigned kDwarfLangC99 = 0x000c;  // DW_LANG_C99

// DWARF tags / base-type encodings (subset).
constexpr unsigned DW_TAG_member           = 0x0d;
constexpr unsigned DW_TAG_pointer_type     = 0x0f;
constexpr unsigned DW_TAG_structure_type   = 0x13;
constexpr unsigned DW_TAG_base_type        = 0x24;
constexpr unsigned DW_ATE_boolean          = 0x02;
constexpr unsigned DW_ATE_float            = 0x04;
constexpr unsigned DW_ATE_signed           = 0x05;
constexpr unsigned DW_ATE_unsigned         = 0x07;
constexpr unsigned DW_ATE_UTF              = 0x10;

// The DWARF member walk asks the SAME question as the value-copy memcpy sizes
// ("what stride does the backend step, where does member i start"), so it asks
// the same engine: `mlir_abi_align` / `mlir_abi_size` / `layout_align_up` in
// mlir_gen_impl.hpp. This file used to carry its own copy of that walk — the
// correct one, as it happens, while the memcpy sites used `mlir::DataLayout`
// and got a different answer. A second copy of a right answer is still a second
// engine, and the next edit only has to reach one of them.
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

    // Subroutine type (return + param DI types) so gdb shows the signature and
    // `whatis`/`ptype <fn>` work.
    auto sub_type = di_subroutine_type(fn);

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

// ── DI type builder (Stage 2) ───────────────────────────────────────────────

// Leaf DI type from an MLIR type (used for struct/tuple members). Scalars are
// faithful; LLVM pointers → void*; aggregates → opaque sized composite (no
// members) — keeps the builder recursion-free. Signedness is lost here (an
// MLIR i64 may be Logos i64 or u64) — acceptable for member display.
mlir::LLVM::DITypeAttr MLIRGenImpl::di_leaf_from_mlir(mlir::Type t) {
    auto* ctx = builder_.getContext();
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(t)) {
        unsigned w = it.getWidth();
        unsigned enc = (w == 8) ? DW_ATE_signed : DW_ATE_signed;  // members: signed default
        std::string nm = "i" + std::to_string(w);
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, nm, (uint64_t)w, enc);
    }
    if (mlir::isa<mlir::Float32Type>(t))
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, "f32", 32, DW_ATE_float);
    if (mlir::isa<mlir::Float64Type>(t))
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, "f64", 64, DW_ATE_float);
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t))
        return mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_pointer_type, mlir::StringAttr{}, mlir::LLVM::DITypeAttr{},
            /*sizeInBits=*/64, /*alignInBits=*/64, /*offsetInBits=*/0,
            /*dwarfAddressSpace=*/std::nullopt, /*extraData=*/mlir::LLVM::DINodeAttr{});
    // Aggregate (struct/array/...) → opaque, correctly sized.
    uint64_t bits = mlir_abi_size(t) * 8;
    std::string nm;
    if (auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t); st && st.isIdentified())
        nm = st.getName().str();
    return mlir::LLVM::DICompositeTypeAttr::get(
        ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, nm), di_file_,
        /*line=*/0, /*scope=*/mlir::LLVM::DIScopeAttr{}, /*baseType=*/mlir::LLVM::DITypeAttr{},
        mlir::LLVM::DIFlags::Zero, /*sizeInBits=*/bits, /*alignInBits=*/0,
        /*elements=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
}

// Struct DICompositeType with members + byte offsets. Offsets/sizes come from
// the registered LLVM struct layout; member BASE types come from the Logos field
// types (so a `*mut T` member is a typed pointer-to-T, not void*, and signedness
// is preserved) — matched to the LLVM fields by name. di_struct_inprogress_
// breaks self-referential structs (e.g. `Node { next: *mut Node }`): the
// back-edge returns an opaque sized stub.
mlir::LLVM::DITypeAttr MLIRGenImpl::di_struct_type(TypeRef t) {
    auto* ctx = builder_.getContext();
    std::string key = mlir_struct_key(t);
    std::string disp = type_str(t);
    auto opaque = [&](uint64_t sizeBits) {
        return mlir::LLVM::DICompositeTypeAttr::get(
            ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, disp), di_file_,
            0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
            sizeBits, 0, llvm::ArrayRef<mlir::LLVM::DINodeAttr>{}, mlir::LLVM::DIExpressionAttr{},
            mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
    };
    auto sit = struct_types_.find(key);
    if (sit == struct_types_.end()) {
        std::unordered_set<std::string> seen;
        return opaque(logos_abi_byte_size(t, seen) * 8);
    }
    if (di_struct_inprogress_.count(key))
        return opaque(mlir_abi_size(sit->second.llvm_type) * 8);
    di_struct_inprogress_.insert(key);

    // name → Logos field type (typed pointers / signedness for members).
    std::unordered_map<std::string, TypeRef> logos_fields;
    {
        auto svit = all_struct_defs_.find(key);
        if (svit == all_struct_defs_.end()) svit = all_struct_defs_.find(disp);
        if (svit != all_struct_defs_.end())
            svit->second.each_field([&](lir_view::LFieldView f) {
                logos_fields[std::string(f.name())] = f.type(pool_impl());
            });
    }

    llvm::SmallVector<mlir::LLVM::DINodeAttr> members;
    uint64_t off = 0;
    for (auto& f : sit->second.fields) {
        uint64_t fbits = mlir_abi_size(f.type) * 8;
        uint64_t falign = mlir_abi_align(f.type);                     // bytes (x86-64)
        off = layout_align_up(off, falign * 8);
        mlir::LLVM::DITypeAttr base;
        if (auto lit = logos_fields.find(f.name);
            lit != logos_fields.end() && lit->second)
            base = di_type(lit->second);
        if (!base) base = di_leaf_from_mlir(f.type);
        members.push_back(mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_member, mlir::StringAttr::get(ctx, f.name),
            base, fbits, /*alignInBits=*/(uint32_t)(falign * 8),
            /*offsetInBits=*/off, std::nullopt, mlir::LLVM::DINodeAttr{}));
        off += fbits;
    }
    uint64_t total = mlir_abi_size(sit->second.llvm_type) * 8;
    di_struct_inprogress_.erase(key);
    return mlir::LLVM::DICompositeTypeAttr::get(
        ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, disp), di_file_,
        /*line=*/0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
        /*sizeInBits=*/total, /*alignInBits=*/0, members,
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
}

mlir::LLVM::DITypeAttr MLIRGenImpl::di_type(TypeRef t) {
    if (!t) return {};                  // void / null
    auto* ctx = builder_.getContext();
    auto off = t.offset();
    if (auto it = di_type_cache_.find(off); it != di_type_cache_.end()) return it->second;

    auto basic = [&](const char* nm, uint64_t bits, unsigned enc) {
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, nm, bits, enc);
    };
    auto ptr_to = [&](mlir::LLVM::DITypeAttr base) -> mlir::LLVM::DITypeAttr {
        return mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_pointer_type, mlir::StringAttr{}, base,
            64, 64, 0, std::nullopt, mlir::LLVM::DINodeAttr{});
    };

    mlir::LLVM::DITypeAttr result;
    using K = LogosType::Kind;
    switch (t.kind()) {
        case K::Void: return {};   // void return type — no DI node
        case K::I8:   result = basic("i8",   8,   DW_ATE_signed);   break;
        case K::I16:  result = basic("i16",  16,  DW_ATE_signed);   break;
        case K::I24:  result = basic("i24",  24,  DW_ATE_signed);   break;
        case K::I32:  result = basic("i32",  32,  DW_ATE_signed);   break;
        case K::I56:  result = basic("i56",  56,  DW_ATE_signed);   break;
        case K::I64:  result = basic("i64",  64,  DW_ATE_signed);   break;
        case K::I128: result = basic("i128", 128, DW_ATE_signed);   break;
        case K::Isize:result = basic("isize",64,  DW_ATE_signed);   break;
        case K::U8:   result = basic("u8",   8,   DW_ATE_unsigned); break;
        case K::U16:  result = basic("u16",  16,  DW_ATE_unsigned); break;
        case K::U24:  result = basic("u24",  24,  DW_ATE_unsigned); break;
        case K::U32:  result = basic("u32",  32,  DW_ATE_unsigned); break;
        case K::U56:  result = basic("u56",  56,  DW_ATE_unsigned); break;
        case K::U64:  result = basic("u64",  64,  DW_ATE_unsigned); break;
        case K::U128: result = basic("u128", 128, DW_ATE_unsigned); break;
        case K::Usize:result = basic("usize",64,  DW_ATE_unsigned); break;
        case K::Bool: result = basic("bool", 8,   DW_ATE_boolean);  break;
        case K::Char: result = basic("char", 32,  DW_ATE_UTF);      break;
        case K::F32:  result = basic("f32",  32,  DW_ATE_float);    break;
        case K::F64:  result = basic("f64",  64,  DW_ATE_float);    break;
        case K::IntLit:   result = basic("i64", 64, DW_ATE_signed); break;
        case K::FloatLit: result = basic("f64", 64, DW_ATE_float);  break;
        case K::Ptr: case K::Ref: case K::MutRef: {
            result = ptr_to(t.pointee() ? di_type(t.pointee()) : mlir::LLVM::DITypeAttr{});
            break;
        }
        case K::FnPtr: case K::Closure:
            result = ptr_to(mlir::LLVM::DITypeAttr{});
            break;
        case K::Struct: case K::ZonedStruct:
            result = di_struct_type(t);
            break;
        case K::Slice: {
            // Fat pointer { ptr: *elem, len: i64 } — gives &[T] / str inspectable
            // members and lets the Vec/slice pretty-printer iterate elements.
            auto i64ty = basic("i64", 64, DW_ATE_signed);
            auto elemdi = t.elem() ? di_type(t.elem()) : mlir::LLVM::DITypeAttr{};
            llvm::SmallVector<mlir::LLVM::DINodeAttr> m;
            m.push_back(mlir::LLVM::DIDerivedTypeAttr::get(
                ctx, DW_TAG_member, mlir::StringAttr::get(ctx, "ptr"), ptr_to(elemdi),
                64, 64, /*offset=*/0, std::nullopt, mlir::LLVM::DINodeAttr{}));
            m.push_back(mlir::LLVM::DIDerivedTypeAttr::get(
                ctx, DW_TAG_member, mlir::StringAttr::get(ctx, "len"), i64ty,
                64, 64, /*offset=*/64, std::nullopt, mlir::LLVM::DINodeAttr{}));
            result = mlir::LLVM::DICompositeTypeAttr::get(
                ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, type_str(t)), di_file_,
                0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
                /*sizeInBits=*/128, 0, m, mlir::LLVM::DIExpressionAttr{},
                mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
            break;
        }
        case K::Enum:
            collect_enum_meta(t);   // record variant/layout metadata for the printer
            [[fallthrough]];
        default: {
            // Tuple/Array/Enum/Slice/TraitObject/… → opaque, correctly sized.
            std::unordered_set<std::string> seen;
            uint64_t bytes = logos_abi_byte_size(t, seen);
            result = mlir::LLVM::DICompositeTypeAttr::get(
                ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, type_str(t)), di_file_,
                0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
                /*sizeInBits=*/bytes * 8, 0, llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
                mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
                mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
            break;
        }
    }
    if (result) di_type_cache_[off] = result;
    return result;
}

void MLIRGenImpl::emit_local_dbg_declare(std::string_view name, TypeRef ty,
                                         uint32_t line) {
    if (!debug_info_ || !di_subprogram_ || !ty) return;
    auto it = scope_.find(std::string(name));
    if (it == scope_.end() || !it->second) return;
    mlir::Value slot = it->second;
    // Slot must be an alloca (the variable's own storage). SSA-value bindings
    // (tuples/closures bound by value, ref aliases) are skipped — DbgDeclare on
    // them would mislead gdb. (DbgValue handling is future work.)
    auto alloca = slot.getDefiningOp<mlir::LLVM::AllocaOp>();
    if (!alloca) return;
    // Size guard: the alloca's element must be the same ABI size as the
    // variable's type. Rejects `let r = &s` (slot = s's alloca, sizes differ)
    // while accepting scalars, structs/enums/arrays/tuples, and mut-ref slots
    // (alloca-of-ptr, ptr-sized == &T size). x86-64 abi_size_bytes — mlir's
    // default DataLayout under-reports (no inter-field padding).
    std::unordered_set<std::string> seen;
    uint64_t ty_bytes = logos_abi_byte_size(ty, seen);
    uint64_t slot_bytes = mlir_abi_size(alloca.getElemType());
    if (ty_bytes == 0 || ty_bytes != slot_bytes) return;

    auto dty = di_type(ty);
    if (!dty) return;
    auto* ctx = builder_.getContext();
    auto var = mlir::LLVM::DILocalVariableAttr::get(
        ctx, di_subprogram_, mlir::StringAttr::get(ctx, name), di_file_,
        line ? line : di_scope_line_, /*arg=*/0, /*alignInBits=*/0, dty,
        mlir::LLVM::DIFlags::Zero);
    builder_.create<mlir::LLVM::DbgDeclareOp>(dbg_loc(line), slot, var);
}

void MLIRGenImpl::emit_param_dbg_declare(std::string_view name, TypeRef ty,
                                         mlir::Value arg, unsigned arg_no,
                                         uint32_t line) {
    if (!debug_info_ || !di_subprogram_ || !ty || !arg) return;
    auto dty = di_type(ty);
    if (!dty) return;
    auto* ctx = builder_.getContext();
    auto var = mlir::LLVM::DILocalVariableAttr::get(
        ctx, di_subprogram_, mlir::StringAttr::get(ctx, name), di_file_,
        line ? line : di_scope_line_, /*arg=*/arg_no, /*alignInBits=*/0, dty,
        mlir::LLVM::DIFlags::Zero);
    auto loc = dbg_loc(line);
    // Aggregate (struct/enum/array/tuple) arriving as a pointer = the object's
    // address → DbgDeclare. Scalars / pointers / by-value aggregates → DbgValue.
    using K = LogosType::Kind;
    bool aggregate = false;
    switch (ty.kind()) {
        case K::Struct: case K::ZonedStruct: case K::Enum:
        case K::Array: case K::Tuple: aggregate = true; break;
        default: break;
    }
    if (aggregate && mlir::isa<mlir::LLVM::LLVMPointerType>(arg.getType()))
        builder_.create<mlir::LLVM::DbgDeclareOp>(loc, arg, var);
    else
        builder_.create<mlir::LLVM::DbgValueOp>(loc, arg, var);
}

namespace {
std::string json_esc(std::string_view s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}
} // namespace

void MLIRGenImpl::collect_enum_meta(TypeRef t) {
    if (!t || t.kind() != LogosType::Kind::Enum) return;
    std::string key = type_str(t);
    if (di_enum_meta_.count(key)) return;
    std::string ename(t.enum_name());

    // disc → variant name, from the EnumView (base or instance keyed).
    std::map<int64_t, std::string> names;
    auto find_ev = [&](const std::string& nm) {
        auto it = enum_types_.find(nm);
        if (it != enum_types_.end()) return it;
        auto lt = nm.find('<');
        return lt != std::string::npos ? enum_types_.find(nm.substr(0, lt))
                                       : enum_types_.end();
    };
    auto* te = resolve_tagged_enum(ename, t);
    // Generic enum instances are registered in enum_types_ under the mono-mangled
    // name (e.g. `Option__i64`), not the bare `Option` from type_str — so prefer
    // te->name (the instance key) for the variant-name lookup.
    auto evit = te ? find_ev(te->name) : enum_types_.end();
    if (evit == enum_types_.end()) evit = find_ev(ename);
    if (evit == enum_types_.end()) evit = find_ev(key);
    if (evit != enum_types_.end())
        evit->second.each_variant([&](lir_view::EnumVariantView v) {
            names[v.disc()] = std::string(v.name());
        });

    // disc → concrete payload field types (from the instance's TaggedEnumInfo).
    std::map<int64_t, std::vector<TypeRef>> payloads;
    if (te)
        for (auto& vp : te->variants) payloads[vp.disc] = vp.logos_types;

    auto field_json = [&](int64_t disc) {
        std::string o = "[";
        bool first = true;
        auto it = payloads.find(disc);
        if (it != payloads.end())
            for (auto& lt : it->second) {
                if (!lt) continue;
                if (!first) o += ",";
                first = false;
                o += "\"" + json_esc(type_str(lt)) + "\"";
            }
        o += "]";
        return o;
    };
    auto name_of = [&](int64_t d) {
        auto it = names.find(d);
        return it != names.end() ? it->second : ("v" + std::to_string(d));
    };

    std::string rec;
    if (te && te->niche.kind != TaggedEnumInfo::Niche::NoNiche) {
        if (te->niche.kind == TaggedEnumInfo::Niche::NullPtr) {
            rec = "{\"kind\":\"niche_nullptr\",\"none\":\"" +
                  json_esc(name_of(te->niche.none_disc)) + "\",\"some\":\"" +
                  json_esc(name_of(te->niche.some_disc)) + "\",\"some_f\":" +
                  field_json(te->niche.some_disc) + "}";
        } else {  // LowBit
            rec = "{\"kind\":\"niche_lowbit\",\"ptr_n\":\"" +
                  json_esc(name_of(te->niche.ptr_disc)) + "\",\"val_n\":\"" +
                  json_esc(name_of(te->niche.val_disc)) + "\",\"val_bits\":" +
                  std::to_string(te->niche.val_bits) + ",\"val_signed\":" +
                  (te->niche.val_signed ? "true" : "false") + ",\"val_raw\":" +
                  (te->niche.val_raw ? "true" : "false") + "}";
        }
    } else if (te) {
        uint64_t pa = te->payload_align ? te->payload_align : 1;
        uint64_t payload_off = layout_align_up(4, pa);
        if (payload_off < 4) payload_off = 4;
        rec = "{\"kind\":\"tagged\",\"disc_off\":0,\"disc_size\":4,\"payload_off\":" +
              std::to_string(payload_off) + ",\"variants\":[";
        bool first = true;
        for (auto& [d, n] : names) {
            if (!first) rec += ",";
            first = false;
            rec += "{\"d\":" + std::to_string(d) + ",\"n\":\"" + json_esc(n) +
                   "\",\"f\":" + field_json(d) + "}";
        }
        rec += "]}";
    } else {
        // C-like enum (no payload) — disc value → name.
        rec = "{\"kind\":\"c\",\"disc_off\":0,\"disc_size\":4,\"variants\":[";
        bool first = true;
        for (auto& [d, n] : names) {
            if (!first) rec += ",";
            first = false;
            rec += "{\"d\":" + std::to_string(d) + ",\"n\":\"" + json_esc(n) + "\"}";
        }
        rec += "]}";
    }
    di_enum_meta_[key] = std::move(rec);
}

void MLIRGenImpl::emit_debug_metadata(mlir::ModuleOp mod) {
    if (!debug_info_ || di_enum_meta_.empty()) return;
    if (mod.lookupSymbol("__logos_debug_meta")) return;
    std::string json = "{";
    bool first = true;
    for (auto& [k, rec] : di_enum_meta_) {
        if (!first) json += ",";
        first = false;
        json += "\"" + json_esc(k) + "\":" + rec;
    }
    json += "}";
    json.push_back('\0');  // NUL-terminate so gdb Value.string() stops cleanly.

    auto i8 = builder_.getIntegerType(8);
    mlir::OpBuilder::InsertionGuard guard(builder_);
    builder_.setInsertionPointToEnd(mod.getBody());
    auto arr = mlir::LLVM::LLVMArrayType::get(i8, json.size());
    auto attr = builder_.getStringAttr(llvm::StringRef(json.data(), json.size()));
    // WeakODR + a section the linker keeps even under --gc-sections. The printer
    // reads the symbol via parse_and_eval.
    auto g = builder_.create<mlir::LLVM::GlobalOp>(
        builder_.getUnknownLoc(), arr, /*isConstant=*/true,
        mlir::LLVM::Linkage::WeakODR, "__logos_debug_meta", attr);
    g.setSection(".logos_debug_meta");
}

mlir::LLVM::DISubroutineTypeAttr
MLIRGenImpl::di_subroutine_type(lir_view::FunctionView fn) {
    auto* ctx = builder_.getContext();
    const auto* pool = pool_impl();
    llvm::SmallVector<mlir::LLVM::DITypeAttr> types;
    // Index 0 = return type (null = void).
    TypeRef ret = fn.ret_type(pool);
    types.push_back(ret ? di_type(ret) : mlir::LLVM::DITypeAttr{});
    for (auto& p : fn.params())
        types.push_back(di_type(p.type(pool)));
    return mlir::LLVM::DISubroutineTypeAttr::get(ctx, /*callingConvention=*/0, types);
}

} // namespace logos::compiler
