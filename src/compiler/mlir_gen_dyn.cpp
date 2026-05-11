// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_dyn.cpp — Vtable emission, &dyn Trait coercion, dyn dispatch, closures.

#include "mlir_gen_impl.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// emit_tier2_lookup — binary-search lookup for tier-2 type codes.
//
// Emits a func.func @<lookup_name>(type_code: i64) -> ptr that performs
// binary search over the sorted @<codes_name> array and returns the
// corresponding entry from @<fns_name>, or null if not found.
// ---------------------------------------------------------------------------

static void emit_tier2_lookup(
    mlir::OpBuilder& builder, mlir::Location loc,
    mlir::ModuleOp mod,
    const std::string& lookup_name,
    const std::string& codes_name,
    const std::string& fns_name,
    mlir::LLVM::LLVMArrayType codes_arr_type,
    mlir::LLVM::LLVMArrayType fns_arr_type,
    int64_t n)
{
    auto* ctx  = builder.getContext();
    auto  ptr_t = mlir::LLVM::LLVMPointerType::get(ctx);
    auto  i64_t = builder.getI64Type();

    // func @lookup(type_code: i64) -> ptr
    auto fn_type = mlir::FunctionType::get(ctx, {i64_t}, {ptr_t});
    builder.setInsertionPointToEnd(mod.getBody());
    auto fn = builder.create<mlir::func::FuncOp>(loc, lookup_name, fn_type);
    fn->setAttr("sym_visibility", mlir::StringAttr::get(ctx, "private"));
    auto* entry_block = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry_block);

    mlir::Value type_code = entry_block->getArgument(0);

    // Allocas for loop variables: lo, hi, mid.
    mlir::Value alloca_one = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
    auto lo_alloca  = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);
    auto hi_alloca  = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);
    auto mid_alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);

    auto zero64   = builder.create<mlir::arith::ConstantIntOp>(loc, 0LL, 64);
    auto nminus1  = builder.create<mlir::arith::ConstantIntOp>(loc, n - 1, 64);
    builder.create<mlir::LLVM::StoreOp>(loc, zero64,  lo_alloca);
    builder.create<mlir::LLVM::StoreOp>(loc, nminus1, hi_alloca);

    // Addresses of the sorted codes and fns globals.
    auto codes_addr = builder.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, codes_name);
    auto fns_addr   = builder.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, fns_name);

    // Create CFG blocks.
    auto* region       = builder.getBlock()->getParent();
    auto* cond_block   = new mlir::Block();
    auto* body_block   = new mlir::Block();
    auto* neq_block    = new mlir::Block();
    auto* lo_upd_block = new mlir::Block();
    auto* hi_upd_block = new mlir::Block();
    auto* found_block  = new mlir::Block();
    auto* exit_block   = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(neq_block);
    region->push_back(lo_upd_block);
    region->push_back(hi_upd_block);
    region->push_back(found_block);
    region->push_back(exit_block);

    // entry → cond
    builder.create<mlir::cf::BranchOp>(loc, cond_block);

    // cond_block: loop while lo <= hi (signed; indices are non-negative).
    builder.setInsertionPointToStart(cond_block);
    {
        auto lo  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, lo_alloca);
        auto hi  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, hi_alloca);
        auto cmp = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::sle, lo, hi);
        builder.create<mlir::cf::CondBranchOp>(loc, cmp, body_block, exit_block);
    }

    // body_block: compute mid, load codes[mid], compare.
    builder.setInsertionPointToStart(body_block);
    {
        auto lo2  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, lo_alloca);
        auto hi2  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, hi_alloca);
        auto sum  = builder.create<mlir::arith::AddIOp>(loc, lo2, hi2);
        auto two  = builder.create<mlir::arith::ConstantIntOp>(loc, 2LL, 64);
        mlir::Value mid = builder.create<mlir::arith::DivSIOp>(loc, sum, two);
        builder.create<mlir::LLVM::StoreOp>(loc, mid, mid_alloca);

        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid);  // dynamic index (mlir::Value)
        auto code_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, codes_arr_type, codes_addr, gep);
        auto code_mid = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, code_slot);

        auto eq = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::eq, code_mid, type_code);
        builder.create<mlir::cf::CondBranchOp>(loc, eq, found_block, neq_block);
    }

    // neq_block: decide whether to go lo or hi.
    // Re-read codes[mid] to get code_mid (mid was stored to alloca by body_block).
    builder.setInsertionPointToStart(neq_block);
    {
        mlir::Value mid3 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid3);
        auto code_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, codes_arr_type, codes_addr, gep);
        auto code_mid2 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, code_slot);
        // Use unsigned comparison: type_codes are logically u64.
        auto lt = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::ult, code_mid2, type_code);
        builder.create<mlir::cf::CondBranchOp>(loc, lt, lo_upd_block, hi_upd_block);
    }

    // lo_upd_block: lo = mid + 1
    builder.setInsertionPointToStart(lo_upd_block);
    {
        auto mid4   = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        auto one    = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
        auto new_lo = builder.create<mlir::arith::AddIOp>(loc, mid4, one);
        builder.create<mlir::LLVM::StoreOp>(loc, new_lo, lo_alloca);
        builder.create<mlir::cf::BranchOp>(loc, cond_block);
    }

    // hi_upd_block: hi = mid - 1
    builder.setInsertionPointToStart(hi_upd_block);
    {
        auto mid5   = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        auto one    = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
        auto new_hi = builder.create<mlir::arith::SubIOp>(loc, mid5, one);
        builder.create<mlir::LLVM::StoreOp>(loc, new_hi, hi_alloca);
        builder.create<mlir::cf::BranchOp>(loc, cond_block);
    }

    // found_block: return fns[mid].
    builder.setInsertionPointToStart(found_block);
    {
        mlir::Value mid6 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid6);
        auto fn_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, fns_arr_type, fns_addr, gep);
        auto fn_ptr = builder.create<mlir::LLVM::LoadOp>(loc, ptr_t, fn_slot);
        builder.create<mlir::func::ReturnOp>(loc, mlir::ValueRange{fn_ptr});
    }

    // exit_block: return null (type_code not found — programmer error).
    builder.setInsertionPointToStart(exit_block);
    {
        auto null_ptr = builder.create<mlir::LLVM::ZeroOp>(loc, ptr_t);
        builder.create<mlir::func::ReturnOp>(loc, mlir::ValueRange{null_ptr});
    }

    builder.setInsertionPointToEnd(mod.getBody());
}

// ---------------------------------------------------------------------------
// Trait vtable info (Pass 1b)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tag-dispatch tables (Pass 1c)
// ---------------------------------------------------------------------------
// Tier-1 (type_code 1-222): [223 x ptr] global array, O(1) direct index.
// Tier-2 (type_code 223+): sorted [N x i64] codes + [N x ptr] fns arrays;
//   a per-table binary-search lookup function (@__logos_tier2_*_lookup) is
//   emitted.  Both tiers are filled at program startup by the generated
//   @__logos_tag_dispatch_init function (injected at the top of main() in
//   mlir_gen.cpp Pass 3).
//
// Collision detection (Pass A):
//   For each (tag_system, trait, type_code) registration the compiler emits:
//     @__logos_tagdispatch_<ts>_<trait>_<XXXXXXXXXXXXXXXX> = global i8 1
//   with External linkage.  If two object files register the same tuple the
//   linker sees a multiply-defined symbol and aborts with an error.
//   The sentinel value (1) is arbitrary — only the symbol identity matters.

// Build the collision-detection symbol name.
// Format: __logos_tagdispatch_<ts>_<trait>_<type_code_hex16>
// '::' and other non-identifier chars in names are replaced with '_'.
static std::string collision_sym_name(const std::string& ts,
                                       const std::string& trait,
                                       uint64_t type_code)
{
    // Sanitize: replace chars invalid in LLVM symbol names.
    auto sanitize = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) r.push_back((c == ':' || c == '<' || c == '>') ? '_' : c);
        return r;
    };
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)type_code);
    // Use "__" (double underscore) as field separator so names that contain a
    // single underscore (e.g. "My_System" or "My_Trait") do not produce the
    // same symbol as a different (ts, trait) combination.
    return "__logos_tagdispatch__" + sanitize(ts) + "__" + sanitize(trait) + "__" + hex;
}

void MLIRGenImpl::emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog) {
    if (prog.dispatch_entries.empty()) return;

    // Compute which tag systems are fully covered by the binary archive.
    // For those systems we emit only external declarations (so gen_tagged_dispatch
    // can reference the tables) but no initialiser/body — the archive provides them.
    std::set<std::string> binary_tag_systems;
    if (!prog.binary_symbols.empty()) {
        std::map<std::string, bool> sys_all_binary;
        for (auto& de : prog.dispatch_entries) {
            if (de.tag_system.empty() || de.type_code == 0) continue;
            auto it = sys_all_binary.find(de.tag_system);
            if (it == sys_all_binary.end())
                sys_all_binary[de.tag_system] = true;
            if (!prog.binary_symbols.count(de.fn_symbol))
                sys_all_binary[de.tag_system] = false;
        }
        for (auto& [sys, all_bin] : sys_all_binary)
            if (all_bin) binary_tag_systems.insert(sys);
    }

    auto ptr_t = ptr_type();
    constexpr int kTier1Size = 256;
    auto arr_type_t1 = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);

    // For binary tag systems emit External declarations so gen_tagged_dispatch
    // can emit AddressOf / call — the linker resolves them to the archive.
    if (!binary_tag_systems.empty()) {
        auto i64_t = builder_.getI64Type();
        std::set<std::string> decl_emitted;
        for (auto& de : prog.dispatch_entries) {
            if (!binary_tag_systems.count(de.tag_system)) continue;
            if (de.type_code == 0) continue;
            auto base = de.tag_system + "__" + de.trait_name + "__" + de.method_name;
            if (de.type_code < static_cast<uint64_t>(kTier1Size)) {
                auto tname = "__logos_tag_dispatch__" + base;
                if (decl_emitted.insert(tname).second && !mod.lookupSymbol(tname)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    builder_.create<mlir::LLVM::GlobalOp>(
                        loc_, arr_type_t1, false, mlir::LLVM::Linkage::External,
                        tname, mlir::Attribute{}, 0);
                }
            } else {
                auto t2key = "__logos_tier2__" + base;
                // forward-declare _codes, _fns as external (size unknown here, use i8 array of 0)
                for (auto& suffix : {std::string("_codes"), std::string("_fns")}) {
                    auto gname = t2key + suffix;
                    if (decl_emitted.insert(gname).second && !mod.lookupSymbol(gname)) {
                        builder_.setInsertionPointToEnd(mod.getBody());
                        auto arr0 = mlir::LLVM::LLVMArrayType::get(i64_t, 0);
                        builder_.create<mlir::LLVM::GlobalOp>(
                            loc_, arr0, false, mlir::LLVM::Linkage::External,
                            gname, mlir::Attribute{}, 0);
                    }
                }
                // forward-declare _lookup function
                auto lkp_name = t2key + "_lookup";
                if (decl_emitted.insert(lkp_name).second && !mod.lookupSymbol(lkp_name)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    auto fn_type = builder_.getFunctionType(
                        {builder_.getI64Type()}, {ptr_t});
                    auto decl = builder_.create<mlir::func::FuncOp>(loc_, lkp_name, fn_type);
                    decl.setPrivate();
                }
            }
        }
    }

    // If every tag system is binary, declarations are enough — no definitions.
    if (!binary_tag_systems.empty()) {
        bool all_binary = true;
        for (auto& de : prog.dispatch_entries)
            if (!de.tag_system.empty() && !binary_tag_systems.count(de.tag_system))
                { all_binary = false; break; }
        if (all_binary) return;
    }
    auto i64_t = builder_.getI64Type();

    struct Entry { uint64_t type_code; std::string fn_symbol; };
    std::map<std::string, std::vector<Entry>> tier1_tables;
    std::map<std::string, std::vector<Entry>> tier2_tables;
    // Bug fix: store valid tier-2 entries for Pass D to avoid size mismatch.
    std::map<std::string, std::vector<Entry>> tier2_valid_entries;

    for (auto& de : prog.dispatch_entries) {
        // Skip dispatch entries from fully-binary tag systems.
        if (binary_tag_systems.count(de.tag_system)) continue;
        // type_code == 0 is unset (no impl registered yet); skip.
        // Codes 1-127 are valid for inline AnyVal slots; 128-255 for tier-1 zone types.
        if (de.type_code == 0) continue;
        // Use "__" as separator to avoid ambiguity when tag_system or trait_name
        // contains a single underscore (e.g. "My_System__Trait__method" is
        // unambiguous; "My_System_Trait_method" is not).
        auto base = de.tag_system + "__" + de.trait_name + "__" + de.method_name;
        if (de.type_code < static_cast<uint64_t>(kTier1Size)) {
            tier1_tables["__logos_tag_dispatch__" + base].push_back({de.type_code, de.fn_symbol});
        } else {
            tier2_tables["__logos_tier2__" + base].push_back({de.type_code, de.fn_symbol});
        }
    }
    if (tier1_tables.empty() && tier2_tables.empty()) return;

    // ── Pass A: emit link-time collision detection sentinels ──────────────
    // One i8 global per unique (tag_system, trait_name, type_code) triple.
    // Deduplication: use a set to avoid emitting twice when a trait has
    // multiple methods (each method produces one LDispatchEntry for the same
    // type_code, but we only need one sentinel per type registration).
    {
        auto i8_t = builder_.getIntegerType(8);
        auto one_attr = mlir::IntegerAttr::get(i8_t, 1);
        std::set<std::string> emitted;
        for (auto& de : prog.dispatch_entries) {
            if (de.type_code == 0) continue;
            if (binary_tag_systems.count(de.tag_system)) continue;
            auto sym = collision_sym_name(de.tag_system, de.trait_name, de.type_code);
            if (!emitted.insert(sym).second) continue;    // already emitted this triple
            // Type-check the existing symbol: only skip if it is already the
            // expected i8 sentinel.  Any other symbol with the same name
            // (e.g. a user function) is a naming conflict — warn and skip.
            if (auto existing = mod.lookupSymbol(sym)) {
                if (!llvm::isa<mlir::LLVM::GlobalOp>(existing))
                    std::fprintf(stderr,
                        "warning: collision-sentinel name '%s' shadowed by "
                        "a non-global symbol; dispatch collision detection "
                        "for type_code 0x%llx may be suppressed\n",
                        sym.c_str(), (unsigned long long)de.type_code);
                continue;
            }
            builder_.setInsertionPointToEnd(mod.getBody());
            builder_.create<mlir::LLVM::GlobalOp>(
                loc_, i8_t, /*isConstant=*/true,
                mlir::LLVM::Linkage::External, sym, one_attr);
        }
    }

    // ── Pass B: emit zero-initialized [223 x ptr] globals for tier-1 ─────
    // Use Weak linkage when the table already exists in a binary archive so the
    // linker deduplicates rather than flagging a duplicate-definition error.
    for (auto& [table_name, _entries] : tier1_tables) {
        if (mod.lookupSymbol(table_name)) continue;
        bool in_binary = prog.binary_symbols.count(table_name) > 0;
        auto linkage = in_binary ? mlir::LLVM::Linkage::Weak
                                 : mlir::LLVM::Linkage::External;
        builder_.setInsertionPointToEnd(mod.getBody());
        auto glob = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type_t1, false, linkage, table_name, mlir::Attribute{}, 0);
        auto& ir = glob.getInitializerRegion();
        builder_.setInsertionPointToStart(builder_.createBlock(&ir));
        builder_.create<mlir::LLVM::ReturnOp>(
            loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, arr_type_t1));
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // ── Pass C: emit tier-2 data globals + binary-search lookup functions ─
    for (auto& [t2key, entries] : tier2_tables) {
        // Bug fix: filter entries to only include those with valid callees.
        // This avoids creating sparse arrays with zeros that break binary search.
        std::vector<Entry> valid_entries;
        for (auto& e : entries) {
            if (mod.lookupSymbol<mlir::func::FuncOp>(e.fn_symbol)) {
                valid_entries.push_back(e);
            }
        }
        if (valid_entries.empty()) continue;  // skip tier-2 table if no valid entries
        std::sort(valid_entries.begin(), valid_entries.end(),
                  [](const Entry& a, const Entry& b){ return a.type_code < b.type_code; });

        // Bug fix: store valid_entries for Pass D to use.
        tier2_valid_entries[t2key] = valid_entries;
        int64_t n = static_cast<int64_t>(valid_entries.size());

        bool t2_in_binary = prog.binary_symbols.count(t2key + "_codes") > 0;
        auto t2_linkage = t2_in_binary ? mlir::LLVM::Linkage::Weak
                                       : mlir::LLVM::Linkage::External;

        auto codes_arr_type = mlir::LLVM::LLVMArrayType::get(i64_t, n);
        auto codes_name = t2key + "_codes";
        if (!mod.lookupSymbol(codes_name)) {
            builder_.setInsertionPointToEnd(mod.getBody());
            auto g = builder_.create<mlir::LLVM::GlobalOp>(
                loc_, codes_arr_type, false,
                t2_linkage, codes_name, mlir::Attribute{}, 0);
            auto& ir = g.getInitializerRegion();
            builder_.setInsertionPointToStart(builder_.createBlock(&ir));
            builder_.create<mlir::LLVM::ReturnOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, codes_arr_type));
            builder_.setInsertionPointToEnd(mod.getBody());
        }

        auto fns_arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, n);
        auto fns_name = t2key + "_fns";
        if (!mod.lookupSymbol(fns_name)) {
            builder_.setInsertionPointToEnd(mod.getBody());
            auto g = builder_.create<mlir::LLVM::GlobalOp>(
                loc_, fns_arr_type, false,
                t2_linkage, fns_name, mlir::Attribute{}, 0);
            auto& ir = g.getInitializerRegion();
            builder_.setInsertionPointToStart(builder_.createBlock(&ir));
            builder_.create<mlir::LLVM::ReturnOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, fns_arr_type));
            builder_.setInsertionPointToEnd(mod.getBody());
        }

        auto lookup_name = t2key + "_lookup";
        if (!mod.lookupSymbol<mlir::func::FuncOp>(lookup_name)) {
            emit_tier2_lookup(builder_, loc_, mod,
                              lookup_name, codes_name, fns_name,
                              codes_arr_type, fns_arr_type, n);
        }
    }

    // ── Pass D: emit per-tag-system __logos_tag_dispatch_init__<TagSystem> ──
    // One init function per non-binary tag system avoids symbol clashes when
    // the stdlib archive already provides its own init function.
    // Collect tag systems that have tables to initialize.
    std::set<std::string> active_systems;
    for (auto& [tname, _] : tier1_tables) {
        // table_name is "__logos_tag_dispatch__<sys>__<trait>__<method>"
        // extract sys: strip prefix, then take up to second "__"
        std::string_view sv(tname);
        sv.remove_prefix(sizeof("__logos_tag_dispatch__") - 1);
        auto p = sv.find("__");
        if (p != std::string_view::npos)
            active_systems.insert(std::string(sv.substr(0, p)));
    }
    for (auto& [tname, _] : tier2_valid_entries) {
        std::string_view sv(tname);
        sv.remove_prefix(sizeof("__logos_tier2__") - 1);
        auto p = sv.find("__");
        if (p != std::string_view::npos)
            active_systems.insert(std::string(sv.substr(0, p)));
    }

    for (const auto& sys : active_systems) {
        auto ctor_name = "__logos_tag_dispatch_init__" + sys;
        if (mod.lookupSymbol<mlir::func::FuncOp>(ctor_name)) continue;
        auto ctor_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
        builder_.setInsertionPointToEnd(mod.getBody());
        auto ctor_fn = builder_.create<mlir::func::FuncOp>(loc_, ctor_name, ctor_type);
        ctor_fn->setAttr("sym_visibility",
                         mlir::StringAttr::get(builder_.getContext(), "private"));
        auto* ctor_block = ctor_fn.addEntryBlock();
        builder_.setInsertionPointToStart(ctor_block);

        // Fill tier-1 tables for this tag system.
        for (auto& [table_name, entries] : tier1_tables) {
            std::string_view sv(table_name);
            sv.remove_prefix(sizeof("__logos_tag_dispatch__") - 1);
            auto p = sv.find("__");
            if (p == std::string_view::npos || sv.substr(0, p) != sys) continue;
            auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, table_name);
            for (auto& e : entries) {
                auto callee = mod.lookupSymbol<mlir::func::FuncOp>(e.fn_symbol);
                if (!callee) continue;
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, callee.getFunctionType(), e.fn_symbol);
                auto fn_ptr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_t, mlir::ValueRange{fn_ref}).getResult(0);
                mlir::Value idx = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(e.type_code), 64);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(idx);
                auto slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, arr_type_t1, table_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_ptr, slot);
            }
        }

        // Fill tier-2 for this tag system.
        for (auto& [t2key, entries] : tier2_valid_entries) {
            std::string_view sv(t2key);
            sv.remove_prefix(sizeof("__logos_tier2__") - 1);
            auto p = sv.find("__");
            if (p == std::string_view::npos || sv.substr(0, p) != sys) continue;
            int64_t n = static_cast<int64_t>(entries.size());
            auto codes_arr_type = mlir::LLVM::LLVMArrayType::get(i64_t, n);
            auto fns_arr_type   = mlir::LLVM::LLVMArrayType::get(ptr_t, n);
            auto codes_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, t2key + "_codes");
            auto fns_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, t2key + "_fns");
            for (int64_t i = 0; i < n; ++i) {
                auto callee = mod.lookupSymbol<mlir::func::FuncOp>(entries[i].fn_symbol);
                if (!callee) continue;
                mlir::Value idx = builder_.create<mlir::arith::ConstantIntOp>(loc_, i, 64);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(idx);
                mlir::Value code_val = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(entries[i].type_code), 64);
                auto code_slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, codes_arr_type, codes_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, code_val, code_slot);
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, callee.getFunctionType(), entries[i].fn_symbol);
                auto fn_ptr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_t, mlir::ValueRange{fn_ref}).getResult(0);
                auto fn_slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, fns_arr_type, fns_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_ptr, fn_slot);
            }
        }

        builder_.create<mlir::func::ReturnOp>(loc_);
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // Note: the constructor is a func.func, not a llvm.func. We cannot use
    // llvm.mlir.global_ctors here because its verifier requires llvm.func
    // symbols (which only exist after ConvertFuncToLLVM runs, after codegen).
    // Instead, mlir_gen.cpp injects a call to @__logos_tag_dispatch_init at
    // the start of main() after all function bodies are generated (Pass 3).

    // ── Pass E: emit public lookup wrappers for registry access ──────────
    // For each (TagSystem, trait, method) triple, generate:
    //   fn __logos_dispatch_lookup__<TS>__<trait>__<method>(code: u64) -> *const u8
    //
    // The wrapper checks tier-1 (if present) and falls back to tier-2 (if
    // present). Returns null when neither table has an entry. stdlib can
    // call this to expose `hermes_fn_<trait>_<method>(code)` as a registry API
    // for reflection / deferred invocation.
    {
        std::set<std::string> all_bases;
        for (auto& [tkey, _] : tier1_tables) {
            // tkey = "__logos_tag_dispatch__<base>"
            all_bases.insert(tkey.substr(std::string("__logos_tag_dispatch__").size()));
        }
        for (auto& [tkey, _] : tier2_valid_entries) {
            // tkey = "__logos_tier2__<base>"
            all_bases.insert(tkey.substr(std::string("__logos_tier2__").size()));
        }

        auto lookup_ret_type = ptr_t;
        auto lookup_fn_type = mlir::FunctionType::get(
            builder_.getContext(), {i64_t}, {lookup_ret_type});

        for (auto& base : all_bases) {
            auto lookup_sym = "__logos_dispatch_lookup__" + base;
            bool has_t1 = tier1_tables.count("__logos_tag_dispatch__" + base) > 0;
            bool has_t2 = tier2_valid_entries.count("__logos_tier2__" + base) > 0;
            if (!has_t1 && !has_t2) continue;

            // If stdlib declared this symbol `extern fn` (empty-body FuncOp),
            // reuse the existing declaration so the define below satisfies the
            // linker reference. Otherwise create a fresh FuncOp.
            mlir::func::FuncOp fn;
            if (auto existing = mod.lookupSymbol<mlir::func::FuncOp>(lookup_sym)) {
                if (!existing.getBody().empty()) continue;  // already defined
                fn = existing;
            } else {
                builder_.setInsertionPointToEnd(mod.getBody());
                fn = builder_.create<mlir::func::FuncOp>(
                    loc_, lookup_sym, lookup_fn_type);
            }
            // Public (default visibility) — stdlib and user code can call it.
            auto* entry = fn.addEntryBlock();
            builder_.setInsertionPointToStart(entry);
            auto code = entry->getArgument(0);

            auto null_ptr = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t);

            auto gen_tier1_load = [&](mlir::Value code_val) -> mlir::Value {
                auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                    loc_, ptr_t, "__logos_tag_dispatch__" + base);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(code_val);
                auto slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, arr_type_t1, table_addr, gi);
                return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot);
            };
            auto gen_tier2_call = [&](mlir::Value code_val) -> mlir::Value {
                auto t2lkp = mod.lookupSymbol<mlir::func::FuncOp>(
                    "__logos_tier2__" + base + "_lookup");
                if (!t2lkp) return null_ptr;
                return builder_.create<mlir::func::CallOp>(
                    loc_, t2lkp, mlir::ValueRange{code_val}).getResult(0);
            };

            if (has_t1 && !has_t2) {
                // Tier-1 only: direct load. Out-of-range code would read past
                // the table; branch to guard it.
                auto bound = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(kTier1Size), 64);
                auto in_range = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::ult, code, bound);
                auto* hit = new mlir::Block();
                auto* miss = new mlir::Block();
                fn.getBody().push_back(hit);
                fn.getBody().push_back(miss);
                builder_.create<mlir::cf::CondBranchOp>(loc_, in_range, hit, miss);

                builder_.setInsertionPointToStart(hit);
                auto fn_ptr = gen_tier1_load(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr});

                builder_.setInsertionPointToStart(miss);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{null_ptr});
            } else if (!has_t1 && has_t2) {
                auto fn_ptr = gen_tier2_call(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr});
            } else {
                // Both tiers: branch on code < kTier1Size.
                auto bound = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(kTier1Size), 64);
                auto in_t1 = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::ult, code, bound);
                auto* t1b = new mlir::Block();
                auto* t2b = new mlir::Block();
                fn.getBody().push_back(t1b);
                fn.getBody().push_back(t2b);
                builder_.create<mlir::cf::CondBranchOp>(loc_, in_t1, t1b, t2b);

                builder_.setInsertionPointToStart(t1b);
                auto fn_ptr1 = gen_tier1_load(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr1});

                builder_.setInsertionPointToStart(t2b);
                auto fn_ptr2 = gen_tier2_call(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr2});
            }
            builder_.setInsertionPointToEnd(mod.getBody());
        }
    }
}


void MLIRGenImpl::emit_trait_vtables(mlir::ModuleOp /*mod*/, const LProgram& prog) {
    // Helper: find every concrete-mangled clone of `target_base` in
    // prog.functions. Each clone's struct mangling looks like
    // `[pkg.]target_base$G<N>$<arg1>[$<arg2>…]__method__[fg]__sig`.
    // We collect the set of unique struct manglings (`target_base$G…`).
    auto collect_concrete_targets = [&](std::string_view target_base) {
        std::set<std::string> targets;
        std::string prefix = std::string(target_base) + "$G";
        auto scan = [&](std::string_view name) {
            if (auto dot = name.rfind('.'); dot != std::string_view::npos)
                name = name.substr(dot + 1);
            if (name.compare(0, prefix.size(), prefix) != 0) return;
            auto p = name.find("__", prefix.size());
            if (p == std::string_view::npos) return;
            targets.emplace(name.substr(0, p));
        };
        for (auto& fp : prog.functions) if (fp) scan(fp->name);
        // Methods on cloned struct specs land under struct.methods in some
        // mono paths.
        for (auto& sd : prog.structs) {
            for (auto& mp : sd.methods) if (mp) scan(mp->name);
        }
        return targets;
    };

    for (auto& td : prog.traits) {
        for (auto& ib : prog.impls) {
            if (ib.trait_name != td.name) continue;

            // Resolve method-symbol given a TARGET (bare or concrete).
            //
            // Match strategy:
            //   1. fn.method_base == trait method name (exact, no string
            //      manipulation of the mangled symbol)
            //   2. fn.name belongs to `target` — strip pkg prefix, then
            //      check the symbol starts with `target__` (bare) or
            //      `concrete__` (post-mono concrete clone).
            //
            // Replaces the old try_match heuristic that walked mangling
            // suffixes with strncmp; method_base is set by sema's
            // lower_fn (raw_name from AST) and propagated by
            // mono_clone's clone_fn.
            auto resolve_methods = [&](std::string_view target) -> std::vector<std::string> {
                auto belongs_to_target = [&](std::string_view nm) -> bool {
                    if (auto dot = nm.rfind('.'); dot != std::string_view::npos)
                        nm = nm.substr(dot + 1);
                    if (nm.size() < target.size() + 2) return false;
                    if (nm.compare(0, target.size(), target) != 0) return false;
                    return nm[target.size()] == '_' && nm[target.size() + 1] == '_';
                };
                std::vector<std::string> methods;
                for (auto& m : td.methods) {
                    std::string sym;
                    auto try_match = [&](const lir::LFunction& fn) -> bool {
                        return fn.method_base == m.name && belongs_to_target(fn.name);
                    };
                    for (auto& fp : ib.methods) {
                        if (!fp) continue;
                        if (try_match(*fp)) { sym = fp->name; break; }
                    }
                    if (sym.empty()) {
                        for (auto& fp : prog.functions) {
                            if (!fp) continue;
                            if (try_match(*fp)) { sym = fp->name; break; }
                        }
                    }
                    if (sym.empty()) {
                        for (auto& sd : prog.structs) {
                            if (sd.name != target) continue;
                            for (auto& mp : sd.methods) {
                                if (!mp) continue;
                                if (try_match(*mp)) { sym = mp->name; break; }
                            }
                            if (!sym.empty()) break;
                        }
                    }
                    if (sym.empty()) sym = std::string(target) + "__" + m.name;
                    methods.push_back(std::move(sym));
                }
                return methods;
            };

            // Bare-target entry — used by non-generic impls and as a default
            // fallback. For non-generic structs, this is also the lookup key.
            dyn_vtable_methods_[td.name + "::" + ib.target_type] =
                resolve_methods(ib.target_type);

            // Concrete-target entries — for generic impls, register one
            // vtable per concrete struct instantiation found in mono's
            // output (prog.functions / struct.methods). Lookup at
            // `coerce_to_dyn` keys on the concrete-mangled struct name
            // (`Foo$G1$arg`), so this is what makes generic-impl method
            // dispatch resolve to the right monomorphised symbols.
            for (auto& concrete : collect_concrete_targets(ib.target_type)) {
                dyn_vtable_methods_[td.name + "::" + concrete] =
                    resolve_methods(concrete);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Build inline vtable [N x ptr] heap-allocated for a concrete type.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::build_inline_vtable(std::string_view trait_name,
                                               std::string_view type_name) {
    std::string key;
    key.reserve(trait_name.size() + 2 + type_name.size());
    key.append(trait_name); key.append("::"); key.append(type_name);
    auto vit = dyn_vtable_methods_.find(key);
    if (vit == dyn_vtable_methods_.end()) return nullptr;
    auto& methods = vit->second;
    size_t n = methods.size();
    // Heap-allocate: n pointers × 8 bytes each.
    auto size_val = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n * 8), 64);
    auto vtable = call_malloc(size_val);
    if (!vtable) return nullptr;
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    for (size_t i = 0; i < n; ++i) {
        auto callee = parent_mod.lookupSymbol<mlir::func::FuncOp>(methods[i]);
        if (!callee) {
            std::fprintf(stderr, "mlir_gen: vtable: method '%s' not found\n",
                         methods[i].c_str());
            continue;
        }
        auto func_type = callee.getFunctionType();
        auto fn_ref = builder_.create<mlir::func::ConstantOp>(
            loc_, func_type, methods[i]);
        auto fn_addr = builder_.create<mlir::UnrealizedConversionCastOp>(
            loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
        // GEP: vtable is ptr to array of ptrs; element i at offset i*sizeof(ptr).
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(i)};
        auto slot = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), ptr_type(), vtable, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, slot);
    }
    return vtable;
}

// ---------------------------------------------------------------------------
// Build a &dyn Trait fat pointer from a concrete data_ptr.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                                        std::string_view src_type_name) {
    auto dyn_struct = mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});
    // Heap-allocate the {data, vtable} fat-storage so the resulting
    // handle can outlive the cast site's stack frame. Necessary for
    // smart-pointer use cases (NodeARC) where the cast happens in one
    // fn and the handle is returned / stored elsewhere. Stack alloc
    // would dangle on return. Cost: 16-byte malloc per `as *mut dyn`
    // expression; the storage stays alive as long as any handle
    // references it (caller's responsibility — Box<dyn>'s drop frees,
    // raw `*mut dyn` doesn't).
    auto size16 = builder_.create<mlir::arith::ConstantIntOp>(loc_, int64_t(16), 64);
    auto alloca = call_malloc(size16);
    if (!alloca) return nullptr;
    // Store data pointer at field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, alloca, idx0);
    builder_.create<mlir::LLVM::StoreOp>(loc_, data_ptr, dp);
    // Store vtable pointer at field 1
    auto vtable = build_inline_vtable(trait_name, src_type_name);
    if (vtable) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
        auto vp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, alloca, idx1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, vtable, vp);
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Indirect call through &tagged<TS> Trait dispatch.
//
// Dispatch sequence:
//   1. Evaluate receiver — *const u8 pointing to the tagged object.
//   2. Call read_type_code(obj_ptr) → i64 type_code.
//   3a. If type_code < 223 (tier-1): GEP into @__logos_tag_dispatch_*[type_code].
//   3b. If type_code >= 223 (tier-2): call @__logos_tier2_*_lookup(type_code).
//   4. Load/receive fn_ptr.
//   5. Indirect call through fn_ptr with (obj_ptr, user_args…).
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_tagged_dispatch(lir_view::EMethodCallView v,
                                              TypeRef ret_logos_type) {
    constexpr int kTier1Size = 256;
    auto ptr_t = ptr_type();

    auto recv_ref = v.receiver();
    auto* recv_le = lexpr_of(recv_ref);
    if (!recv_le) return nullptr;

    // 1. Evaluate the receiver.
    mlir::Value obj_ptr = nullptr;
    if (recv_ref && recv_ref.kind() == lir_schema::expr::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        auto it = scope_.find(name);
        if (it != scope_.end()) {
            if (let_vars_.count(name))
                obj_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, it->second);
            else
                obj_ptr = it->second;
        }
    }
    if (!obj_ptr) obj_ptr = gen_expr(*recv_le);
    if (!obj_ptr) return nullptr;

    // 2. <TagSystem>::read_tag(nullptr_self, obj_ptr) → i64.
    // Each TagSystem knows how to extract the type_code from memory for its
    // encoding (legacy 2-byte tag vs. vlen datatag vs. TOM inline header).
    // TagSystems are unit structs with no state, so self can be a null pointer.
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto rtc_base = std::string(v.tag_system()) + "__read_tag";
    std::string rtc_sym = rtc_base;
    auto rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
    if (!rtc_fn && prog_) {
        // Method names may be bare (`Base__read_tag[__f__sig]`) or pkg-qualified
        // (`pkg.Base__read_tag[__f__sig]`). Concrete trait impls land in
        // prog_->functions (not sd.methods), so scan both.
        std::string rtc_f = rtc_base + "__f__";
        std::string rtc_g = rtc_base + "__g__";
        auto try_match = [&](const std::string& nm) {
            return nm == rtc_base ||
                   nm.find(rtc_f) != std::string::npos ||
                   nm.find(rtc_g) != std::string::npos;
        };
        for (auto& sd : prog_->structs) {
            if (sd.name != v.tag_system()) continue;
            for (auto& mp : sd.methods) {
                if (!mp) continue;
                if (try_match(mp->name)) {
                    rtc_sym = mp->name;
                    rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
                    if (rtc_fn) break;
                }
            }
            if (rtc_fn) break;
        }
        if (!rtc_fn) {
            for (auto& fn : prog_->functions) {
                if (!fn) continue;
                if (try_match(fn->name)) {
                    rtc_sym = fn->name;
                    rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
                    if (rtc_fn) break;
                }
            }
        }
    }
    if (!rtc_fn) {
        std::fprintf(stderr, "gen_tagged_dispatch: '%s' not found\n", rtc_sym.c_str());
        return nullptr;
    }
    auto null_self = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t);
    mlir::Value type_code_val = builder_.create<mlir::func::CallOp>(
        loc_, rtc_fn, mlir::ValueRange{null_self, obj_ptr}).getResult(0);

    // Identify available tables.
    // Must use the same "__" separator as emit_tag_dispatch_tables.
    auto base_key    = std::string(v.tag_system()) + "__" + std::string(v.tag_trait()) + "__" + std::string(v.method());
    auto table_sym   = "__logos_tag_dispatch__" + base_key;
    auto t2_lkp_sym  = "__logos_tier2__" + base_key + "_lookup";
    bool has_tier1   = (parent_mod.lookupSymbol<mlir::LLVM::GlobalOp>(table_sym) != nullptr);
    bool has_tier2   = (parent_mod.lookupSymbol<mlir::func::FuncOp>(t2_lkp_sym)  != nullptr);

    if (!has_tier1 && !has_tier2) {
        std::fprintf(stderr, "gen_tagged_dispatch: no dispatch table for '%s'\n",
                     table_sym.c_str());
        return nullptr;
    }

    // 3. Resolve fn_ptr (tier-1 or tier-2 path).
    mlir::Value fn_ptr;

    if (!has_tier2) {
        // ── Tier-1 only: direct array lookup ──────────────────────────────
        auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);
        auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_t, table_sym);
        llvm::SmallVector<mlir::LLVM::GEPArg> gi;
        gi.push_back(int32_t(0));
        gi.push_back(type_code_val);
        auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_t, arr_type, table_addr, gi);
        fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot_ptr);
    } else {
        // ── Conditional: tier-1 if type_code < 223, else tier-2 lookup ───
        auto fn_ptr_alloca = create_entry_alloca(ptr_t);

        auto boundary = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, static_cast<int64_t>(kTier1Size), 64);
        auto is_tier1 = builder_.create<mlir::arith::CmpIOp>(
            loc_, mlir::arith::CmpIPredicate::ult, type_code_val, boundary);

        auto* region      = builder_.getBlock()->getParent();
        auto* tier1_block = new mlir::Block();
        auto* tier2_block = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(tier1_block);
        region->push_back(tier2_block);
        region->push_back(merge_block);

        builder_.create<mlir::cf::CondBranchOp>(loc_, is_tier1, tier1_block, tier2_block);

        // tier1_block
        builder_.setInsertionPointToStart(tier1_block);
        if (has_tier1) {
            auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);
            auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, table_sym);
            llvm::SmallVector<mlir::LLVM::GEPArg> gi;
            gi.push_back(int32_t(0));
            gi.push_back(type_code_val);
            auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_t, arr_type, table_addr, gi);
            auto t1_fn = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot_ptr);
            builder_.create<mlir::LLVM::StoreOp>(loc_, t1_fn, fn_ptr_alloca);
        } else {
            builder_.create<mlir::LLVM::StoreOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t), fn_ptr_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // tier2_block
        builder_.setInsertionPointToStart(tier2_block);
        {
            auto lkp_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(t2_lkp_sym);
            if (!lkp_fn) {
                // Lookup function missing — store null and fall through.
                builder_.create<mlir::LLVM::StoreOp>(
                    loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t), fn_ptr_alloca);
            } else {
                auto t2_fn = builder_.create<mlir::func::CallOp>(
                    loc_, lkp_fn, mlir::ValueRange{type_code_val}).getResult(0);
                builder_.create<mlir::LLVM::StoreOp>(loc_, t2_fn, fn_ptr_alloca);
            }
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // merge_block
        builder_.setInsertionPointToStart(merge_block);
        fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, fn_ptr_alloca);
    }

    // 4. Build call args: obj_ptr (self as *const u8) + user args.
    llvm::SmallVector<mlir::Value> args;
    args.push_back(obj_ptr);
    llvm::SmallVector<mlir::Type> param_types;
    param_types.push_back(ptr_t);  // self: *const u8

    v.each_arg([&](lir_view::ExprRef ar){
        auto* le = lexpr_of(ar);
        if (!le) { return; }
        auto val = gen_expr(*le);
        if (!val) return;
        args.push_back(val);
        param_types.push_back(val.getType());
    });

    // 6. Build LLVM function type and call indirectly.
    mlir::Type ret_type;
    if (ret_logos_type && TypeRef(ret_logos_type).kind() != LogosType::Kind::Void)
        ret_type = logos_to_mlir(ret_logos_type);
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Indirect call through &dyn Trait vtable.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_dyn_dispatch(lir_view::EMethodCallView v,
                                           TypeRef ret_logos_type) {
    // The receiver is a &dyn Trait — a pointer to {data_ptr, vtable_ptr}.

    auto recv_ref = v.receiver();
    auto* recv_le = lexpr_of(recv_ref);
    if (!recv_le) return nullptr;

    // Check if receiver is a variable we know is dyn
    mlir::Value recv_alloca = nullptr;
    if (recv_ref && recv_ref.kind() == lir_schema::expr::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        auto it = scope_.find(name);
        if (it != scope_.end()) recv_alloca = it->second;
    }
    if (!recv_alloca) {
        recv_alloca = gen_expr(*recv_le);
    }
    if (!recv_alloca) return nullptr;

    auto dyn_struct = mlir::LLVM::LLVMStructType::getLiteral(
        builder_.getContext(), {ptr_type(), ptr_type()});

    // Load data_ptr (field 0)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx0);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);

    // Load vtable_ptr (field 1)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
    auto vp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx1);
    auto vtable_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vp);

    // GEP into vtable array to get fn_ptr at vtable_index
    llvm::SmallVector<mlir::LLVM::GEPArg> slot_idx{int32_t(v.vtable_index())};
    auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), ptr_type(), vtable_ptr, slot_idx);
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot_ptr);

    // Build args: data_ptr (self) + user args
    llvm::SmallVector<mlir::Value> args;
    args.push_back(data_ptr);
    v.each_arg([&](lir_view::ExprRef ar){
        auto* le = lexpr_of(ar);
        if (!le) return;
        auto val = gen_expr(*le);
        if (!val) return;
        args.push_back(val);
    });

    // Build LLVM function type for the indirect call.
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& a : args) param_types.push_back(a.getType());

    mlir::Type ret_type;
    if (ret_logos_type && TypeRef(ret_logos_type).kind() != LogosType::Kind::Void) {
        // Struct returns: the actual fn signature returns the LLVM struct
        // value (see Logos's struct-return convention — register_struct
        // emits LLVMStructType for each concrete instantiation). The
        // logos_to_mlir(Struct) shorthand returns ptr_type for "passed by
        // ptr" use cases (params, fields), but for return-position we need
        // the struct itself so the indirect call type matches the fn def.
        if (TypeRef(ret_logos_type).kind() == LogosType::Kind::Struct ||
            TypeRef(ret_logos_type).kind() == LogosType::Kind::ZonedStruct) {
            auto sit = struct_types_.find(mlir_struct_key(ret_logos_type));
            if (sit == struct_types_.end())
                sit = struct_types_.find(std::string(TypeRef(ret_logos_type).struct_name()));
            if (sit != struct_types_.end()) ret_type = sit->second.llvm_type;
        }
        if (!ret_type) ret_type = logos_to_mlir(ret_logos_type);
    }
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    // Indirect call via function pointer (same pattern as closure calls)
    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Closure generation
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_closure(lir_view::EClosureBoxView v, TypeRef) {
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt = builder_.saveInsertionPoint();

    // Materialise params, captures, ret_type / closure_id once from the view.
    std::vector<std::pair<std::string, TypeRef>> params;
    v.each_param(pool_impl(), [&](std::string_view name, TypeRef t) {
        params.emplace_back(std::string(name), t);
    });
    std::vector<std::string> captures;
    std::vector<TypeRef> capture_types;
    v.each_capture(pool_impl(), [&](std::string_view name, TypeRef t) {
        captures.emplace_back(name);
        capture_types.push_back(t);
    });
    TypeRef ret_t        = v.ret_type(pool_impl());
    std::string closure_id(v.closure_id());
    bool        as_fn_ptr_flag = v.as_fn_ptr();

    auto body_blk = v.body();

    // Non-capturing closure coerced to fn ptr: emit as plain function (no env_ptr).
    if (as_fn_ptr_flag) {
        // Build function type without env_ptr: (params...) -> ret
        llvm::SmallVector<mlir::Type> fn_params;
        for (auto& [_, pt_type] : params) {
            auto pt = logos_to_mlir(pt_type);
            if (pt) fn_params.push_back(pt);
        }
        mlir::Type llvm_ret = ret_t
            ? logos_to_mlir(ret_t)
            : mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        if (!llvm_ret) llvm_ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(llvm_ret, fn_params, false);
        builder_.setInsertionPointToEnd(parent_mod.getBody());
        auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, closure_id, llvm_fn_type);
        fn.setLinkage(mlir::LLVM::Linkage::Private);
        auto* entry = fn.addEntryBlock(builder_);
        builder_.setInsertionPointToStart(entry);
        // Save/restore state (same as regular closure)
        auto saved_scope      = scope_;
        auto saved_lets       = let_vars_;
        auto saved_elems      = var_elem_types_;
        auto saved_ret        = cur_ret_type_;
        auto saved_struct     = var_struct_;
        auto saved_class      = var_class_;
        auto saved_subscript  = var_subscript_;
        auto saved_tuple      = var_tuple_;
        auto saved_te         = var_tagged_enum_;
        auto saved_te_ptr     = var_tagged_enum_ptr_;
        auto saved_local_ptrs = var_local_ptrs_;
        auto saved_dyn_trait  = var_dyn_trait_;
        auto saved_loop_stack = loop_stack_;
        auto saved_entry_block = cur_entry_block_;
        cur_entry_block_ = entry;
        scope_.clear(); let_vars_.clear(); var_elem_types_.clear();
        var_struct_.clear(); var_class_.clear(); var_subscript_.clear();
        var_tuple_.clear(); var_tagged_enum_.clear(); var_tagged_enum_ptr_.clear();
        var_local_ptrs_.clear(); var_dyn_trait_.clear(); loop_stack_.clear();
        cur_ret_type_ = ret_t ? logos_to_mlir(ret_t) : mlir::Type{};
        // Bind params starting from arg 0 (no env_ptr)
        for (size_t i = 0; i < params.size(); ++i)
            scope_[params[i].first] = entry->getArgument(i);
        bool saved_in_llvm = in_llvm_func_;
        in_llvm_func_ = true;
        if (body_blk) gen_block(body_blk);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
        in_llvm_func_ = saved_in_llvm;
        scope_              = saved_scope;
        let_vars_           = saved_lets;
        var_elem_types_     = saved_elems;
        cur_ret_type_       = saved_ret;
        var_struct_         = saved_struct;
        var_class_          = saved_class;
        var_subscript_      = saved_subscript;
        var_tuple_          = saved_tuple;
        var_tagged_enum_    = saved_te;
        var_tagged_enum_ptr_ = saved_te_ptr;
        var_local_ptrs_     = saved_local_ptrs;
        var_dyn_trait_      = saved_dyn_trait;
        loop_stack_         = saved_loop_stack;
        cur_entry_block_    = saved_entry_block;
        builder_.restoreInsertionPoint(save_pt);
        // Return the function address (this IS the fn ptr value)
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), closure_id);
    }

    std::vector<bool> capture_is_struct(captures.size(), false);
    std::vector<bool> capture_is_class(captures.size(), false);
    std::vector<bool> capture_is_array(captures.size(), false);
    std::vector<bool> capture_is_tuple(captures.size(), false);
    std::vector<bool> capture_is_enum(captures.size(), false);
    std::vector<bool> capture_is_dyn(captures.size(), false);
    std::vector<bool> capture_is_pointer_repr(captures.size(), false);
    for (size_t i = 0; i < captures.size(); ++i) {
        const auto& name = captures[i];
        capture_is_struct[i] = var_struct_.count(name);
        capture_is_class[i]  = var_class_.count(name);
        capture_is_array[i]  = var_subscript_.count(name);
        capture_is_tuple[i]  = var_tuple_.count(name);
        capture_is_enum[i]   = var_tagged_enum_.count(name);
        capture_is_dyn[i]    = var_dyn_trait_.count(name);
        capture_is_pointer_repr[i] =
            capture_is_struct[i] || capture_is_class[i] || capture_is_array[i] ||
            capture_is_tuple[i] || capture_is_enum[i] || capture_is_dyn[i];
    }

    // Build capture struct type.
    llvm::SmallVector<mlir::Type> cap_fields;
    for (size_t i = 0; i < capture_types.size(); ++i) {
        auto ct = capture_types[i];
        mlir::Type ft;
        if (capture_is_pointer_repr[i])
            ft = ptr_type();
        else
            ft = logos_to_mlir(ct);
        if (!ft) ft = builder_.getI32Type();
        cap_fields.push_back(ft);
    }
    auto cap_struct = cap_fields.empty()
        ? mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), {builder_.getI8Type()})
        : mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), cap_fields);

    // Build function type: (env_ptr, params...) -> ret
    llvm::SmallVector<mlir::Type> fn_params;
    fn_params.push_back(ptr_type());  // env pointer
    for (auto& [_, pt_type] : params) {
        auto pt = logos_to_mlir(pt_type);
        if (pt) fn_params.push_back(pt);
    }
    llvm::SmallVector<mlir::Type> fn_rets;
    if (ret_t) {
        auto rt = logos_to_mlir(ret_t);
        if (rt) fn_rets.push_back(rt);
    }
    // Create the closure function as llvm.func (so llvm.mlir.addressof works)
    builder_.setInsertionPointToEnd(parent_mod.getBody());
    mlir::Type llvm_ret = fn_rets.empty()
        ? mlir::LLVM::LLVMVoidType::get(builder_.getContext()) : fn_rets[0];
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(llvm_ret, fn_params, false);
    auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, closure_id, llvm_fn_type);
    fn.setLinkage(mlir::LLVM::Linkage::Private);
    auto* entry = fn.addEntryBlock(builder_);
    builder_.setInsertionPointToStart(entry);

    // Save/restore mlir_gen state
    auto saved_scope       = scope_;
    auto saved_lets        = let_vars_;
    auto saved_elems       = var_elem_types_;
    auto saved_ret         = cur_ret_type_;
    auto saved_struct      = var_struct_;
    auto saved_class       = var_class_;
    auto saved_subscript   = var_subscript_;
    auto saved_tuple       = var_tuple_;
    auto saved_te          = var_tagged_enum_;
    auto saved_te_ptr      = var_tagged_enum_ptr_;
    auto saved_local_ptrs  = var_local_ptrs_;
    auto saved_dyn_trait   = var_dyn_trait_;
    auto saved_loop_stack  = loop_stack_;
    auto saved_entry_block = cur_entry_block_;
    cur_entry_block_ = entry;
    scope_.clear(); let_vars_.clear(); var_elem_types_.clear();
    var_struct_.clear(); var_class_.clear(); var_subscript_.clear();
    var_tuple_.clear(); var_tagged_enum_.clear(); var_tagged_enum_ptr_.clear();
    var_local_ptrs_.clear(); var_dyn_trait_.clear(); loop_stack_.clear();

    bool ret_is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(llvm_ret);
    cur_ret_type_ = ret_is_void ? mlir::Type{} : llvm_ret;

    // Unpack captures from env pointer (arg 0)
    auto env_ptr = entry->getArgument(0);
    for (size_t i = 0; i < captures.size(); ++i) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_ptr, idx);
        auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, cap_fields[i], fp);

        TypeRef ct = capture_types[i];
        bool is_struct_cap = capture_is_struct[i];
        bool is_class_cap  = capture_is_class[i];
        bool is_array_cap  = capture_is_array[i];
        bool is_tuple_cap  = capture_is_tuple[i];
        bool is_enum_cap   = capture_is_enum[i];
        bool is_dyn_cap    = capture_is_dyn[i];
        if (is_struct_cap || is_class_cap || is_array_cap ||
            is_tuple_cap || is_enum_cap || is_dyn_cap) {
            scope_[captures[i]] = val;
            if (is_struct_cap)
                var_struct_[captures[i]] = std::string(TypeRef(ct).struct_name());
            else if (is_class_cap)
                var_class_[captures[i]] = std::string(TypeRef(ct).struct_name());
            else if (is_array_cap)
                var_subscript_[captures[i]] = logos_to_mlir(ct ? TypeRef(ct).elem() : TypeRef());
            else if (is_tuple_cap)
                var_tuple_.insert(captures[i]);
            else if (is_enum_cap)
                var_tagged_enum_.insert(captures[i]);
            else if (is_dyn_cap && ct)
                var_dyn_trait_[captures[i]] = std::string(TypeRef(ct).trait_name());
        } else {
            auto alloca = create_entry_alloca(cap_fields[i]);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            scope_[captures[i]] = alloca;
            let_vars_.insert(captures[i]);
            var_elem_types_[captures[i]] = cap_fields[i];
        }
    }

    // Bind params (starting from arg 1)
    for (size_t i = 0; i < params.size(); ++i) {
        scope_[params[i].first] = entry->getArgument(i + 1);
    }

    // Generate body (inside llvm.func — use llvm.return)
    bool saved_in_llvm = in_llvm_func_;
    in_llvm_func_ = true;
    if (body_blk) gen_block(body_blk);
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
    in_llvm_func_ = saved_in_llvm;

    // Restore state
    scope_              = saved_scope;
    let_vars_           = saved_lets;
    var_elem_types_     = saved_elems;
    cur_ret_type_       = saved_ret;
    var_struct_         = saved_struct;
    var_class_          = saved_class;
    var_subscript_      = saved_subscript;
    var_tuple_          = saved_tuple;
    var_tagged_enum_    = saved_te;
    var_tagged_enum_ptr_ = saved_te_ptr;
    var_local_ptrs_     = saved_local_ptrs;
    var_dyn_trait_      = saved_dyn_trait;
    loop_stack_         = saved_loop_stack;
    cur_entry_block_    = saved_entry_block;
    builder_.restoreInsertionPoint(save_pt);

    // At the creation site: alloca capture struct, store captures
    auto env_alloca = create_entry_alloca(cap_struct);
    for (size_t i = 0; i < captures.size(); ++i) {
        auto it = scope_.find(captures[i]);
        if (it == scope_.end()) continue;
        mlir::Value cap_val;
        bool pointer_repr = capture_is_pointer_repr[i];
        auto eit = var_elem_types_.find(captures[i]);
        if (pointer_repr)
            cap_val = it->second;
        else if (let_vars_.count(captures[i]) && eit != var_elem_types_.end())
            cap_val = builder_.create<mlir::LLVM::LoadOp>(loc_, eit->second, it->second);
        else
            cap_val = it->second;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, cap_val, fp);
    }

    // Build closure value: { fn_ptr, env_ptr }
    auto ctype = closure_llvm_type();
    auto closure_alloca = create_entry_alloca(ctype);
    auto fn_addr = builder_.create<mlir::LLVM::AddressOfOp>(
        loc_, ptr_type(), closure_id);
    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, fi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp);
    llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, ei);
    builder_.create<mlir::LLVM::StoreOp>(loc_, env_alloca, ep);

    return closure_alloca;
}

} // namespace logos::compiler
