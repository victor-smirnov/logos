// Logos project — https://github.com/victor-smirnov/logos
//
// Borrow checker — affine ownership + reference exclusivity + dangling detection.
//
// Phase 1 — Linear ownership (use-after-move):
//   Move types  — structs with Drop impl and no Copy impl.
//   Copy types  — everything else (primitives, raw pointers, enums, &T, &mut T).
//   A Move variable is consumed on first EVarRef in value position; re-use is an error.
//
// Phase 2 — Borrow exclusivity:
//   &T   (Ref)    — shared borrow: multiple allowed, blocks moves and &mut.
//   &mut T (MutRef) — exclusive borrow: one at a time, blocks moves and all other borrows.
//   Borrows are scoped lexically; they end when the scope containing the let binding ends.
//   Call-site borrows (&x in function args) are transient (released after the call).
//
// Phase 3 — Dangling reference detection:
//   A function returning &T / &mut T must not return a reference to a local variable.
//   Parameters are safe to borrow from; locals are not.
//
// Branch merging: moves (from Phase 1) are propagated conservatively (union).
//   Borrows are scope-local and released by pop_scope, so they don't survive merges.
//
// Loops: outer variables moved inside the body are dead after the loop.

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/outlives.hpp>
#include <logos/compiler/region_infer.hpp>
#include <logos/compiler/move_classify.hpp>

#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace logos::compiler {

using namespace lir;

// ── Copy/Move classification ────────────────────────────────────────────────

struct TypeSets {
    std::unordered_set<std::string> drop_types;
    std::unordered_set<std::string> copy_types;
};

static TypeSets build_type_sets(const lir::LProgram& prog) {
    TypeSets ts;
    auto register_drop_symbol = [&](std::string_view sym) {
        // After unification, method names are pkg-qualified
        // (`pkg.Buf__drop__f__sig`). Strip pkg prefix before extracting
        // the bare type name.
        // Pkg may contain inner dots; split at LAST dot for bare name.
        if (auto dot = sym.rfind('.'); dot != std::string_view::npos)
            sym = sym.substr(dot + 1);
        if (auto p = sym.find("__drop"); p != std::string_view::npos)
            ts.drop_types.insert(std::string(sym.substr(0, p)));
    };
    auto scan_fns = [&](const std::vector<LFunctionPtr>& fns) {
        for (auto& fn : fns)
            register_drop_symbol(fn->name);
    };
    scan_fns(prog.functions);
    scan_fns(prog.specializations);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            register_drop_symbol(m->name);
    for (auto& impl : prog.impls)
        if (impl.trait_name == "Copy")
            ts.copy_types.insert(impl.target_type);
    return ts;
}

static bool has_droppable_fields(TypeRef, const lir::LProgram&, const TypeSets&);

static bool needs_drop(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    return ts.drop_types.count(std::string(t.struct_name())) || has_droppable_fields(t, prog, ts);
}

static bool has_droppable_fields(TypeRef t, const lir::LProgram& prog,
                                  const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    auto check = [&](const std::vector<LStructDef>& defs) -> bool {
        for (auto& sd : defs) {
            if (sd.name != t.struct_name()) continue;
            for (auto& f : sd.fields)
                if (needs_drop(f.type, prog, ts)) return true;
            return false;
        }
        return false;
    };
    return check(prog.structs) || check(prog.struct_specializations);
}

// A value is a "move type" for borrow-check purposes (consuming it invalidates
// the source, and it can't be moved while borrowed) when it owns droppable
// resources and isn't Copy. P2-12: this was Struct-only, so a tuple / array /
// enum carrying a move element (e.g. `(String, i64)`) was treated as Copy →
// move-while-borrowed and consume tracking silently skipped (a dangling-ref
// soundness gap). Now recurses structurally to match the value's real ownership.
static bool is_move_type(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    // Shared aggregate-recursion skeleton (moveclass::is_move_type); the
    // callbacks reproduce borrow_check's exact (post-mono) semantics. Tuple /
    // Array recursion is single-sourced in the skeleton.
    auto leaf = [&](TypeRef x) -> std::optional<bool> {
        // Rust: `&mut T` is NOT Copy — passing/binding it MOVES the unique
        // mut-reference. `&T` is Copy. Reborrow-shaped uses (`&mut *r`) are
        // wrapped at sema (try_implicit_reborrow_mut) and surface here as
        // AddrOfTemp(Deref(r)), which the AddrOfTemp handler routes to a
        // borrow on r — they don't pass through the move path.
        if (x && x.kind() == LogosType::Kind::MutRef) return true;
        return std::nullopt;
    };
    auto struct_is_move = [&](TypeRef x) {
        return needs_drop(x, prog, ts) &&
               !ts.copy_types.count(std::string(TypeRef(x).struct_name()));
    };
    auto enum_is_move = [&](TypeRef x) -> bool {
        std::string en(TypeRef(x).enum_name());
        if (ts.copy_types.count(en)) return false;     // explicitly Copy enum
        if (ts.drop_types.count(en)) return true;       // has a Drop impl
        for (auto& ed : prog.enums) {                   // any move-typed payload
            if (ed.name != en) continue;
            for (auto& v : ed.variants)
                for (auto& pt : v.payload_types)
                    if (is_move_type(pt, prog, ts)) return true;
            return false;
        }
        return false;
    };
    return moveclass::is_move_type(t, leaf, struct_is_move, enum_is_move);
}

// ── Variable state ───────────────────────────────────────────────────────────

struct VarState {
    // Phase 1 — ownership
    bool     moved          = false;
    uint32_t moved_line     = 0;
    // Phase 2 — borrow tracking
    int      shared_borrows = 0;     // # active &T borrows on this var
    bool     mut_borrowed   = false; // has an active (activated) &mut borrow
    // B82: two-phase borrows — a `&mut x` taken as a fn-call argument is
    // *reserved* during the rest of the arg evaluation, then *activated*
    // at call entry. Reservations behave like the absence of an exclusive
    // borrow w.r.t. concurrent shared reads, but block other mut borrows.
    int      mut_reservations = 0;
    // Phase 3 — binding mutability (`let mut x` vs `let x`).
    // Required for rejecting `&mut x` and `x = ...` against immutable bindings.
    bool     is_mut_binding = false;
    // Partial moves: name → line where field was moved out. Reading the
    // same field again, or the whole value, after a field move is rejected.
    std::unordered_map<std::string, uint32_t> moved_fields;
    // B83: field-path borrow tracking. Keys are dotted paths into the
    // value (e.g. "a", "i.x"). Disjoint field paths can be borrowed
    // simultaneously even mutably. Two borrows conflict iff one path is
    // a prefix of the other (including equal). The whole-value borrows
    // (mut_borrowed, shared_borrows) act as borrows on path "" — they
    // conflict with every field path.
    std::unordered_map<std::string, int>  shared_field_borrows;  // path → count
    std::unordered_set<std::string>       mut_field_borrows;     // path
};

using StateMap = std::unordered_map<std::string, VarState>;

// Phase 4 — lifetime provenance.
// For each reference-typed variable, tracks which function parameters it
// ultimately dereferences into (params are guaranteed to outlive the call).
// is_local = true  → at least one path originates from a local variable
//                    (returning such a ref is a dangling reference).
// params.empty() && !is_local → provenance unknown / comes from a global or
//                                a function return value; assume safe to return.
struct RefProv {
    std::unordered_set<std::string> params;    // param names this ref may alias
    bool                            is_local = false;
};

using ProvMap = std::unordered_map<std::string, RefProv>;

static RefProv merge_prov(const RefProv& a, const RefProv& b) {
    RefProv r;
    for (auto& s : a.params) r.params.insert(s);
    for (auto& s : b.params) r.params.insert(s);
    r.is_local = a.is_local || b.is_local;
    return r;
}

static void merge_provs(ProvMap& base, const ProvMap& other) {
    for (auto& [name, p] : other)
        base[name] = merge_prov(base[name], p);
}

static bool is_ref_kind(TypeRef t) {
    // logos-core 2.1 (default trait-object lifetime rule): `&dyn Trait`
    // resolves to Kind::TraitObject (a fat pair {data, vtable}, not
    // Ref<TraitObject>). The DATA half is a borrowed pointer, so it
    // has the same dangling-on-return risk as `&T`. Treat it as a
    // ref-kind here so check_return_value catches `fn bad() -> &dyn T
    // { return &local; }` — the elided lifetime defaults to the
    // outer-scope (caller) but the local doesn't outlive the fn.
    // Borrowing-form only — owning Box<dyn T> doesn't qualify.
    return t && (t.kind() == LogosType::Kind::Ref ||
                 t.kind() == LogosType::Kind::MutRef ||
                 (t.kind() == LogosType::Kind::TraitObject &&
                  !t.owning_trait_object()));
}

static bool is_mut_ref(TypeRef t) {
    return t && t.kind() == LogosType::Kind::MutRef;
}

struct BorrowRecord {
    std::string target;   // the var being borrowed FROM
    bool        is_mut;
    // Phase 9 (NLL): the var being borrowed INTO (the let/assign LHS that
    // holds the resulting reference). Empty when the borrow is transient
    // (call-site `&x` in an arg, not bound). After each stmt, if
    // `holder`'s last_use_line < current stmt line, the borrow is
    // released — making the borrow non-lexical.
    std::string holder;
};

// B83: a tracked field borrow recorded in the current scope. On pop,
// the borrow is released from the target var's field map.
struct FieldBorrow {
    std::string target;     // root var
    std::string path;       // dotted field path ("a.b.c"); empty for whole-value
    bool        is_mut;
};

struct ScopeFrame {
    std::vector<std::string>  declared;  // vars declared in this scope
    std::vector<BorrowRecord> borrows;   // borrows held in this scope
    std::vector<FieldBorrow>  field_borrows;  // B83: tracked field-path borrows
};

// Single foundation for "what is being borrowed when we see an `AddrOfTemp`?".
// Walks `inner` outer→inner accumulating field-path components. Fields ABOVE
// the first IndexRead/SliceIndex describe layout WITHIN the indexed element
// (not the path TO the indexed container), so they're discarded when an
// index is crossed; fields BELOW the index are the path to the container.
// Whole-element granularity (Rust-conservative: we don't prove disjointness
// on the index value).
//
// Examples (with the outermost expression first):
//   `&p.f.g`        → root=p,   path="f.g",  index_in_chain=false
//   `&p.arr[i]`     → root=p,   path="arr",  index_in_chain=true
//   `&p.arr[i].x`   → root=p,   path="arr",  index_in_chain=true  (.x is within-elem)
//   `&self.data[i]` → root=self,path="data", index_in_chain=true  (self.idx disjoint)
//   `&x`            → root=x,   path="",     index_in_chain=false
//   `&*r`           → root="",  path="",     index_in_chain=false (deref outside scope —
//                                            reborrow shape is recognised separately)
//
// Both the RECORD pass (`take_ref_borrows::AddrOfTemp`) and the CHECK pass
// (`visit::AddrOfTemp`) use this so the structural rule cannot drift between
// them. Each call site applies its own POLICY (take vs check) to the result.
struct BorrowPlace {
    std::string root;             // empty if walker did not reach a VarRef
    std::string path;             // dotted, outermost-inside-root first
    bool        index_in_chain = false;
    TypeRef     root_type = nullptr;   // for raw-ptr / &mut root classification
};

static BorrowPlace extract_borrow_place(lir_view::ExprRef inner,
                                         const TypePoolImpl* pool) {
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    BorrowPlace bp;
    auto cur = inner;
    std::vector<std::string> path_parts;
    while (cur) {
        if (cur.kind() == Code::FieldRead) {
            EFieldReadView fv{cur};
            path_parts.push_back(std::string(fv.field()));
            cur = fv.receiver();
        } else if (cur.kind() == Code::IndexRead) {
            path_parts.clear();
            bp.index_in_chain = true;
            cur = EIndexReadView{cur}.receiver();
        } else if (cur.kind() == Code::SliceIndex) {
            path_parts.clear();
            bp.index_in_chain = true;
            cur = ESliceIndexView{cur}.slice();
        } else {
            break;
        }
    }
    if (cur && cur.kind() == Code::VarRef) {
        bp.root = std::string(EVarRefView{cur}.name());
        bp.root_type = cur.type(pool);
        for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
            if (!bp.path.empty()) bp.path.push_back('.');
            bp.path.append(*it);
        }
    }
    return bp;
}

// Merge Phase-1 move state from 'other' into 'base' (union of moved sets).
// Borrows are scope-local and do not survive merges.
static void merge_moves(StateMap& base, const StateMap& other) {
    for (auto& [name, st] : other)
        if (st.moved) base[name] = st;
}

// ── BorrowChecker ───────────────────────────────────────────────────────────

class BorrowChecker {
    SemaResult&          diags_;
    std::string          fn_name_;
    const lir::LProgram& prog_;
    const TypeSets&      ts_;

    StateMap                 states_;
    std::vector<ScopeFrame>  scopes_;
    // Phase 4: provenance tracking for reference-typed variables.
    ProvMap                              prov_;
    std::unordered_set<std::string>      param_names_;
    // B82: depth of nested call-arg evaluation. While >0, new &mut borrows
    // are taken as reservations (don't conflict with shared reads of the
    // same target during the remaining arg evaluation).
    int                                  in_call_args_ = 0;
    // param name → lifetime annotation of that param's type (e.g. "'a", "")
    std::unordered_map<std::string, std::string> param_lifetimes_;
    // B86: per-param inner-struct lifetime_args. Populated for ref-typed
    // params whose pointee is a Struct/Enum carrying explicit lt_args.
    // Used by check_return_value to accept `self: &Self` -> &'a T when 'a
    // is one of Self's struct lt_args (Self<'a> shape).
    std::unordered_map<std::string, std::vector<std::string>> param_inner_lifetimes_;
    // B87 dropck: for each binding whose type is a Drop-having struct with
    // lifetime params, record which local vars it borrows from at
    // construction. On scope-pop, if any source local was declared in the
    // exiting scope while the binding still lives, reject — the binding's
    // Drop will run after the source dies.
    std::unordered_map<std::string, std::vector<std::string>>
        dropck_borrow_sources_;
    // B87: line at which each dropck-relevant binding was last bound, for
    // diagnostic reporting.
    std::unordered_map<std::string, uint32_t> dropck_binding_line_;
    // Declared lifetime parameters of the current function (e.g. ["'a", "'b"]).
    std::vector<std::string>             fn_lifetime_params_;
    // B66: outlives graph from fn.lifetime_outlives — used to accept the
    // return-lifetime check when an explicit `where 'src: 'ret` (or
    // transitive) covers the case.
    std::unordered_map<std::string, std::unordered_set<std::string>> outlives_adj_;
    // Phase 3/4: return type of current function.
    TypeRef         ret_type_ = nullptr;
    // CFG divergence flag. Set true when a stmt diverges (Return/Break/
    // Continue). If/Match merges check it on each branch and skip merging
    // moves from a diverged arm — otherwise an early-return inside one arm
    // would pollute the join with moves that the OTHER arm never sees.
    bool cur_diverged_ = false;
    // logos-core 2.1 (consumer): optional region_infer for declared-
    // lifetime outlives queries. When non-null, the named-region BFS
    // replaces the string-graph BFS at return-value lifetime checks
    // (both share `fn.lifetime_outlives` as the source). Null in
    // exclusivity-only / generic-template mode (region_infer is
    // imprecise on TypeVars).
    const RegionInferer* ri_ = nullptr;
    // Phase 9 (NLL): max line at which each local variable is read.
    // Populated by scan_uses_block over the entire fn body before checking.
    // A borrow with non-empty holder is released once cur_line >= last_use_line_[holder].
    std::unordered_map<std::string, uint32_t> last_use_line_;

    void report(uint32_t line, std::string msg) {
        // P2-10 exclusivity-only mode: drop move/use-after-move diagnostics
        // (imprecise for generic templates); keep borrow-conflict ones.
        if (exclusivity_only_ && msg.find("moved") != std::string::npos) return;
        Diag d;
        d.level   = Diag::Level::Error;
        d.context = fn_name_;
        d.message = std::move(msg);
        d.line    = line;
        diags_.diags.push_back(std::move(d));
    }

    // ── Scope management ───────────────────────────────────────────────────

    void push_scope() { scopes_.push_back({}); }

    void pop_scope() {
        if (scopes_.empty()) return;
        auto& frame = scopes_.back();
        // Release borrows held by this scope.
        for (auto& br : frame.borrows) {
            auto it = states_.find(br.target);
            if (it != states_.end()) {
                if (br.is_mut) {
                    // B82: release either an activated mut borrow or an
                    // outstanding reservation taken via in_call_args_.
                    if (it->second.mut_borrowed)
                        it->second.mut_borrowed = false;
                    else if (it->second.mut_reservations > 0)
                        it->second.mut_reservations--;
                }
                else if (it->second.shared_borrows > 0)
                    --it->second.shared_borrows;
            }
        }
        // B83: release field-path borrows.
        for (auto& fb : frame.field_borrows) {
            auto it = states_.find(fb.target);
            if (it == states_.end()) continue;
            if (fb.is_mut) it->second.mut_field_borrows.erase(fb.path);
            else {
                auto sit = it->second.shared_field_borrows.find(fb.path);
                if (sit != it->second.shared_field_borrows.end() && --sit->second <= 0)
                    it->second.shared_field_borrows.erase(sit);
            }
        }
        // B87 dropck: before erasing this scope's declared locals, check
        // whether any outer-scope dropck-relevant binding holds a borrow
        // of one of them. If so, the binding's Drop runs after the local
        // dies — reject.
        if (!frame.declared.empty()) {
            std::unordered_set<std::string> dying;
            for (auto& n : frame.declared) dying.insert(n);
            for (auto& [binding, sources] : dropck_borrow_sources_) {
                // Skip if the binding itself is being declared/erased here —
                // its own death coincides with the source, no issue.
                if (dying.count(binding)) continue;
                for (auto& src : sources) {
                    if (!dying.count(src)) continue;
                    uint32_t ln = dropck_binding_line_[binding];
                    report(ln, std::format(
                        "binding '{}' has a `Drop` impl and borrows local '{}', "
                        "but '{}' goes out of scope before '{}' is dropped",
                        binding, src, src, binding));
                    break;
                }
            }
        }
        // Remove variables declared in this scope.
        for (auto& name : frame.declared) {
            states_.erase(name);
            prov_.erase(name);
            dropck_borrow_sources_.erase(name);
            dropck_binding_line_.erase(name);
        }
        scopes_.pop_back();
    }

    void declare_var(const std::string& name) {
        states_[name] = VarState{};
        if (!scopes_.empty()) scopes_.back().declared.push_back(name);
    }

    // ── B87 dropck helpers ───────────────────────────────────────────────
    //
    // A struct is "dropck-relevant" iff it has a Drop impl AND its declared
    // template had a lifetime parameter (preserved through mono in B87).
    // For such bindings, borrows fed into the construction must outlive
    // the binding's drop point (textual scope-end of the binding).
    bool struct_is_dropck_relevant(TypeRef t) const {
        if (!t || t.kind() != LogosType::Kind::Struct) return false;
        if (!needs_drop(t, prog_, ts_)) return false;
        std::string sname(t.struct_name());
        // Check the post-mono struct (preserves lifetime_params via the
        // B87 clone_struct_def change).
        auto check = [&](const std::vector<lir::LStructDef>& defs) -> bool {
            for (auto& sd : defs)
                if (sd.name == sname && !sd.lifetime_params.empty())
                    return true;
            return false;
        };
        if (check(prog_.structs)) return true;
        if (check(prog_.struct_specializations)) return true;
        // Also honor explicit lifetime_args on the TypeRef (paranoia).
        return !t.lifetime_args().empty();
    }
    // §6.1: `items.union.ref.borrow` — a borrow of one union field
    // implicitly borrows ALL fields (they share common storage). At
    // borrow-recording time we coerce union field-path borrows into
    // whole-root borrows so any other field-path of the same union
    // overlaps. Nested-union (`s.u.a` where `s.u` is a union) is a
    // narrower follow-up.
    bool is_union_root(TypeRef t) const {
        if (!t || t.kind() != LogosType::Kind::Struct) return false;
        std::string sname(t.struct_name());
        auto check = [&](const std::vector<lir::LStructDef>& defs) {
            for (auto& sd : defs)
                if (sd.name == sname && sd.is_union) return true;
            return false;
        };
        return check(prog_.structs) || check(prog_.struct_specializations);
    }

    // Walk a struct-lit (or nested aggregate) expression, collecting names
    // of LOCAL variables that are borrowed via AddrOf. Filters out params.
    void collect_borrow_locals(lir_view::ExprRef e,
                               std::vector<std::string>& out) const {
        if (!e) return;
        using EC = lir_schema::expr::Code;
        switch (e.kind()) {
            case EC::AddrOf: {
                std::string n(lir_view::EAddrOfView{e}.var_name());
                if (states_.count(n) && !param_names_.count(n))
                    out.push_back(std::move(n));
                return;
            }
            case EC::AddrOfTemp:
                collect_borrow_locals(lir_view::EAddrOfTempView{e}.inner(), out);
                return;
            case EC::StructLit:
                lir_view::EStructLitView{e}.each_field_value(
                    [&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });
                return;
            case EC::TupleLit:
                lir_view::ETupleLitView{e}.each_elem(
                    [&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });
                return;
            case EC::Cast:
                collect_borrow_locals(lir_view::ECastView{e}.operand(), out);
                return;
            default:
                return;
        }
    }

    // ── Field-path borrow operations (B83) ───────────────────────────────
    //
    // A path P is a prefix of path Q iff P == Q OR Q starts with P + ".".
    // Two borrows (a, b) conflict iff one path is a prefix of the other AND
    // at least one is mutable.
    static bool path_prefix_or_eq(const std::string& a, const std::string& b) {
        if (a.empty()) return true;  // whole-value covers everything
        if (b.size() < a.size()) return false;
        if (b.compare(0, a.size(), a) != 0) return false;
        return b.size() == a.size() || b[a.size()] == '.';
    }
    static bool paths_overlap(const std::string& a, const std::string& b) {
        return path_prefix_or_eq(a, b) || path_prefix_or_eq(b, a);
    }
    static std::string fmt_path(const std::string& target,
                                const std::string& path) {
        if (path.empty()) return target;
        return target + "." + path;
    }
    void take_field_borrow(const std::string& target, std::string path,
                           bool is_mut, uint32_t line) {
        auto it = states_.find(target);
        if (it == states_.end()) return;
        std::string self_disp = fmt_path(target, path);
        // Whole-value borrows still block everything.
        if (it->second.mut_borrowed) {
            report(line, std::format(
                "cannot borrow '{}': '{}' is already mutably borrowed",
                self_disp, target));
            return;
        }
        if (is_mut && it->second.shared_borrows > 0) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' has shared borrows",
                self_disp, target));
            return;
        }
        // Mut binding check.
        if (is_mut && !it->second.is_mut_binding && !param_names_.count(target)) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' not declared as mut",
                self_disp, target));
            return;
        }
        // Check against tracked field borrows.
        for (auto& [p, c] : it->second.shared_field_borrows) {
            if (c <= 0) continue;
            if (paths_overlap(path, p) && is_mut) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: '{}' is already borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        for (auto& p : it->second.mut_field_borrows) {
            if (paths_overlap(path, p)) {
                report(line, std::format(
                    "cannot borrow '{}': '{}' is already mutably borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        // Record.
        if (is_mut) it->second.mut_field_borrows.insert(path);
        else        it->second.shared_field_borrows[path]++;
        if (!scopes_.empty())
            scopes_.back().field_borrows.push_back({target, std::move(path), is_mut});
    }

    // ── Borrow operations ─────────────────────────────────────────────────

    // Take a borrow of 'target'. Registers it in the current scope for cleanup.
    void take_borrow(const std::string& target, bool is_mut, uint32_t line,
                     const std::string& holder = "") {
        auto it = states_.find(target);
        if (it == states_.end()) return;  // unknown / extern
        if (it->second.moved) {
            report(line, std::format(
                "cannot borrow moved value '{}'", target));
            return;
        }
        if (is_mut) {
            // Reject &mut on a binding declared without `mut`.
            // Function params don't currently carry a mut bit in LParam,
            // so they're declared with is_mut_binding=false; we whitelist
            // them by checking known_params_ to avoid spurious diagnostics.
            if (!it->second.is_mut_binding && !param_names_.count(target)) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: not declared as mut", target));
                return;
            }
            // B83: any tracked field-path borrow blocks a whole-value mut.
            if (!it->second.mut_field_borrows.empty() ||
                !it->second.shared_field_borrows.empty()) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: field of '{}' is already borrowed",
                    target, target));
                return;
            }
            if (it->second.mut_borrowed) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: already mutably borrowed", target));
                return;
            }
            // B82: another mut reservation in flight is still a conflict —
            // Rust rejects f(&mut x, &mut x) too.
            if (it->second.mut_reservations > 0) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: already mutably borrowed", target));
                return;
            }
            // B82: inside fn-call arg evaluation, take the mut borrow as a
            // *reservation*. Shared reads of the same target in subsequent
            // args remain legal; the reservation is activated at call entry
            // (logically — we just leave it as reservation since the scope
            // pops after the call returns).
            if (in_call_args_ > 0) {
                // B82+: TPB reservation is compatible with shared borrows
                // taken *during* the same arg evaluation but NOT with
                // shared borrows pre-existing from outer scope. Detect
                // the latter: the current scope frame is the call-args
                // frame (pushed by visit_args); if shared_borrows > 0
                // and any of them was registered in an OUTER scope (not
                // current frame), reject.
                if (it->second.shared_borrows > 0) {
                    bool outer_shared = false;
                    if (!scopes_.empty()) {
                        // Count shared borrows recorded in the top frame.
                        int in_top = 0;
                        for (auto& br : scopes_.back().borrows)
                            if (br.target == target && !br.is_mut) ++in_top;
                        if (in_top < it->second.shared_borrows)
                            outer_shared = true;
                    } else {
                        outer_shared = true;
                    }
                    if (outer_shared) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: {} shared borrow(s) active",
                            target, it->second.shared_borrows));
                        return;
                    }
                }
                it->second.mut_reservations++;
                if (!scopes_.empty())
                    scopes_.back().borrows.push_back({target, is_mut, holder});
                return;
            }
            if (it->second.shared_borrows > 0) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: {} shared borrow(s) active",
                    target, it->second.shared_borrows));
                return;
            }
            it->second.mut_borrowed = true;
        } else {
            if (it->second.mut_borrowed) {
                report(line, std::format(
                    "cannot borrow '{}' as shared: already mutably borrowed", target));
                return;
            }
            // B83: a mut field borrow blocks whole-value shared borrows.
            if (!it->second.mut_field_borrows.empty()) {
                report(line, std::format(
                    "cannot borrow '{}' as shared: field of '{}' is mutably borrowed",
                    target, target));
                return;
            }
            ++it->second.shared_borrows;
        }
        if (!scopes_.empty())
            scopes_.back().borrows.push_back({target, is_mut, holder});
    }

    // ── Ownership operations ───────────────────────────────────────────────

    bool consume(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return true;
        if (!it->second.moved_fields.empty()) {
            auto& [fld, ln] = *it->second.moved_fields.begin();
            report(line, std::format(
                "use of partially moved value '{}' (field '{}' moved on line {})",
                name, fld, ln));
            return false;
        }
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
            return false;
        }
        if (it->second.mut_borrowed || it->second.shared_borrows > 0 ||
            it->second.mut_reservations > 0) {
            report(line, std::format("cannot move '{}' while it is borrowed", name));
            return false;
        }
        it->second = VarState{};
        it->second.moved = true;
        it->second.moved_line = line;
        return true;
    }

    void check_live(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return;
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
        }
        if (it->second.mut_borrowed) {
            report(line, std::format(
                "cannot use '{}' while it is mutably borrowed", name));
        }
    }

    // ── Phase 4: provenance of a reference expression ─────────────────────
    //
    // Returns the set of function parameters the expression borrows from.
    // is_local = true  → at least one source is a local variable (dangling if returned).
    // params.empty() && !is_local → unknown/global — assumed safe (e.g. static data,
    //   or result of a function call where we don't track cross-call lifetimes).

    lir_view::ExprRef expr_ref(const LExprPtr& e) const {
        if (!e || e->mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(prog_.type_pool.arena(), e->mirror_offset_);
    }

    lir_view::StmtRef stmt_ref(const LStmt& s) const {
        if (s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(prog_.type_pool.arena(), s.mirror_offset_);
    }

    const LBlock* block_ptr(lir_view::BlockRef br) const {
        if (!br) return nullptr;
        auto& m = prog_.mirror_table->block_by_offset;
        auto it = m.find(br.offset().value());
        return it == m.end() ? nullptr : it->second;
    }

    lir_view::PatRef pat_ref(const Pattern& p) const {
        auto& tbl = *prog_.mirror_table;
        auto it = tbl.pat.find(&p);
        if (it == tbl.pat.end()) return {};
        return lir_view::PatRef(prog_.type_pool.arena(), it->second);
    }

    // Match arm pattern bindings: PatVariantData injects each binding name into
    // scope; PatWild may also bind (when name is non-empty and not "_").
    void declare_pat_bindings(lir_view::PatRef pr) {
        if (!pr) return;
        using Code = lir_schema::pat::Code;
        switch (pr.kind()) {
            case Code::VariantData: {
                lir_view::PatVariantDataView{pr}.each_binding([&](std::string_view b) {
                    declare_var(std::string(b));
                });
                break;
            }
            case Code::Wild: {
                std::string n(lir_view::PatWildView{pr}.name());
                if (!n.empty() && n != "_") declare_var(n);
                break;
            }
            default: break;
        }
    }
    void declare_pat_bindings(const Pattern& p) {
        declare_pat_bindings(pat_ref(p));
    }

    RefProv prov_of(lir_view::ExprRef e) const {
        if (!e) return {};
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (e.kind()) {
            case Code::VarRef: {
                EVarRefView v{e};
                std::string name(v.name());
                if (param_names_.count(name) && is_ref_kind(e.type(pool)))
                    return {{name}, false};
                auto it = prov_.find(name);
                if (it != prov_.end()) return it->second;
                return {};
            }
            case Code::AddrOf: {
                EAddrOfView v{e};
                std::string name(v.var_name());
                if (param_names_.count(name)) return {{name}, false};
                if (states_.count(name))      return {{},     true};
                return {};
            }
            case Code::AddrOfTemp: {
                // B74 gap: `&literal` / `&<temp_expr>` whose inner expr
                // is rooted in a temporary (literal, fresh struct lit,
                // call result, etc.) yields a dangling reference when
                // returned. If the inner traces back to a parameter or
                // a long-lived var via field/deref chains, prov_of of
                // the inner already returns that — propagate it.
                EAddrOfTempView v{e};
                auto inner_prov = prov_of(v.inner());
                if (!inner_prov.params.empty() || inner_prov.is_local)
                    return inner_prov;
                // Otherwise: rooted in a true temporary → dangling.
                using EK = lir_schema::expr::Code;
                auto ik = v.inner() ? v.inner().kind() : EK::LitInt;
                if (ik == EK::LitInt || ik == EK::LitFloat ||
                    ik == EK::LitBool || ik == EK::LitStr ||
                    ik == EK::StructLit || ik == EK::TupleLit ||
                    ik == EK::ArrLit || ik == EK::Call ||
                    ik == EK::MethodCall || ik == EK::ClosureCall ||
                    ik == EK::EnumLit || ik == EK::EnumLitData)
                    return {{}, true};  // literal / fresh / call result → dangling
                return {};  // unknown — conservative-accept
            }
            case Code::FieldRead:
                return prov_of(EFieldReadView{e}.receiver());
            case Code::Deref:
                return prov_of(EDerefView{e}.operand());
            case Code::TupleIndex:
                return prov_of(ETupleIndexView{e}.receiver());
            case Code::Cast:
                return prov_of(ECastView{e}.operand());
            case Code::IndexRead:
                return prov_of(EIndexReadView{e}.receiver());
            case Code::IfExpr: {
                EIfExprView v{e};
                return merge_prov(prov_of(v.then_val()), prov_of(v.else_val()));
            }
            case Code::BlockExpr:
                return prov_of(EBlockExprView{e}.result());
            case Code::MatchExpr: {
                RefProv merged = {};
                EMatchExprView{e}.each_arm([&](EMatchArmRef arm) {
                    merged = merge_prov(merged, prov_of(arm.value()));
                });
                return merged;
            }
            default:
                // ECall / EMethodCall / EStructLit / literals — value is caller-owned,
                // not a borrowed reference; leave provenance empty (= unknown/safe).
                return {};
        }
    }

    RefProv prov_of(const LExprPtr& e) const {
        if (!e) return {};
        return prov_of(expr_ref(e));
    }

    // ── Phase 3 + 4: dangling / lifetime check on return ──────────────────

    void check_return_value(lir_view::ExprRef er, uint32_t line) {
        if (!ret_type_ || !is_ref_kind(ret_type_)) return;

        RefProv prov = prov_of(er);

        // 1. Definitely local → always dangling.
        if (prov.is_local) {
            std::string src;
            bool is_temp = false;
            if (er) {
                using Code = lir_schema::expr::Code;
                if (er.kind() == Code::AddrOf)
                    src = std::string(lir_view::EAddrOfView{er}.var_name());
                else if (er.kind() == Code::VarRef)
                    src = std::string(lir_view::EVarRefView{er}.name());
                else if (er.kind() == Code::AddrOfTemp)
                    is_temp = true;
            }
            if (is_temp)
                report(line,
                    "cannot return reference to temporary value: dangling reference");
            else
                report(line, std::format(
                    "cannot return reference to local variable '{}': dangling reference",
                    src.empty() ? "?" : src));
            return;
        }

        // 2. Explicit lifetime on return type — check sources match.
        const std::string ret_lt(TypeRef(ret_type_).lifetime());
        if (!ret_lt.empty() && ret_lt != "'_") {
            // L4: When provenance traces to a param that's NOT a ref
            // (e.g. a struct param `x: Foo<'a, 'b>` whose field is
            // returned: `x.y` with `y: &'b u8`), `prov.params` is
            // empty because the param-source is the struct itself,
            // not a ref. The type checker has already verified the
            // returned expression's type matches `&'b u8` against
            // the declared return — trust it. We only error here on
            // detectable mismatches: prov.params non-empty AND each
            // traced param's declared lifetime ≠ ret_lt.
            if (prov.params.empty()) return;
            for (auto& src : prov.params) {
                auto it = param_lifetimes_.find(src);
                // L4: param is NOT a ref (e.g. struct holding refs that
                // we returned a field of) — not in param_lifetimes_ at
                // all. Trust the type checker; it has already verified
                // the FieldRead's lifetime matches the return type.
                if (it == param_lifetimes_.end()) continue;
                const std::string& src_lt = it->second;
                // B66: equality OR src lt outlives ret lt (an explicit
                // `where 'src: 'ret` covers the case, as does 'static src
                // for any named ret).
                if (src_lt == ret_lt) continue;
                // Strict mode (no permissive-empty) — an elided source
                // lifetime cannot silently match a declared return lifetime.
                // logos-core 2.1 (consumer): prefer region_infer's
                // named-region BFS over the local string-graph BFS when
                // available — both consume `fn.lifetime_outlives` as the
                // source, so the two paths agree on every input, but the
                // region_infer view is the canonical one downstream
                // (HRTB, dropck). Falls back to the string graph when
                // ri_ is null (exclusivity-only mode).
                bool ok = ri_ ? ri_->outlives_named(src_lt, ret_lt)
                              : outlives(src_lt, ret_lt, outlives_adj_,
                                         /*permissive_empty=*/false);
                if (ok) continue;
                // B86: if outer ref lt is elided AND the param points to an
                // aggregate, defer to type-checker. The lt structure of
                // fields is verified there; we'd otherwise reject valid
                // `self: &Self` -> &'a T where Self<'a> shapes (Self type
                // resolution doesn't carry impl-level lt_args forward yet).
                if (src_lt.empty()) {
                    auto inner = param_inner_lifetimes_.find(src);
                    if (inner != param_inner_lifetimes_.end()) {
                        bool found = inner->second.empty();  // aggregate w/o lt_args: trust
                        for (auto& ilt : inner->second)
                            if (ilt == ret_lt) { found = true; break; }
                        if (found) continue;
                    }
                }
                report(line, std::format(
                    "lifetime mismatch: return type has lifetime {} "
                    "but '{}' has lifetime {}",
                    ret_lt, src, src_lt.empty() ? "(elided)" : src_lt));
            }
            return;
        }

        // 3. Elided / '_ return lifetime — apply Rust elision rules.
        //    Rule: if exactly one ref-typed parameter (with any lifetime) exists,
        //    the return must derive from that parameter.
        //    With multiple ref params: ambiguous — only check non-local provenance.
        if (!prov.params.empty()) {
            // Has param source(s). Apply elision rule if applicable.
            if (param_lifetimes_.size() == 1) {
                const std::string& sole_param = param_lifetimes_.begin()->first;
                if (!prov.params.count(sole_param)) {
                    report(line, std::format(
                        "lifetime elision: return reference must derive from '{}' "
                        "(the only reference parameter)", sole_param));
                }
            }
            // else: multiple ref params — can't disambiguate, allow any param source.
            return;
        }
        // prov.params empty && !is_local: call result, global, or untracked.
        // If we have ref params but return doesn't trace to any: suspicious,
        // but only report when we can clearly see an EVarRef or EAddrOf with no match.
        if (!param_lifetimes_.empty()) {
            // There are ref params but we can't trace the return to any of them.
            // This can happen for complex expressions — be conservative, don't error.
        }
        // Empty params and no local: unknown provenance (e.g. function return) — safe.
    }

    // ── Expression visitor ─────────────────────────────────────────────────

    void visit(lir_view::ExprRef e, bool consuming, uint32_t line);
    void visit(const LExprPtr& e, bool consuming, uint32_t line) {
        if (!e) return;
        visit(expr_ref(e), consuming, line);
    }

    // Take scoped borrows for all EAddrOf nodes reachable through a ref
    // expression.  Handles the case where the ref is formed conditionally:
    //   let r = if c { &mut x } else { &mut y };   ← both x and y must be
    //   let r = match tag { A => &x, _ => &y };      borrowed for the scope.
    // For non-borrow sub-expressions (condition of if, scrutinee of match,
    // function calls, etc.) we fall through to a regular visit().
    void take_ref_borrows(lir_view::ExprRef e, uint32_t line,
                           const std::string& holder = "") {
        if (!e) return;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (e.kind()) {
            case Code::AddrOf: {
                EAddrOfView v{e};
                take_borrow(std::string(v.var_name()), is_mut_ref(e.type(pool)),
                             line, holder);
                break;
            }
            // B81/B83: `&o.field.chain` lowers to AddrOfTemp(FieldRead*).
            // Walk down the FieldRead chain to extract the root var and
            // dotted path; check moved_fields and take a path-aware borrow.
            case Code::AddrOfTemp: {
                EAddrOfTempView v{e};
                auto inner = v.inner();
                // Reborrow shape `AddrOfTemp(Deref(VarRef r))` where r is
                // ref-typed — register a borrow on r (NOT on what r points
                // to). NLL releases on the holder's last use, restoring r's
                // usability — this is what makes implicit-reborrow at call
                // args work: r is "frozen" only for the call's scope.
                if (ExprRef inner_var; lir_view::is_reborrow_shape(e, &inner_var)
                    && is_ref_kind(inner_var.type(pool))) {
                    std::string rname(EVarRefView{inner_var}.name());
                    if (auto sit = states_.find(rname); sit != states_.end()) {
                        // Route through take_borrow so two-phase reservation
                        // (B82) and prefix-aware diagnostics kick in. Bypass
                        // the is_mut_binding check (reborrow draws from r's
                        // borrow capacity, not its binding mutness; the
                        // `&mut`-ness comes from r's type, which sema has
                        // already verified) via a temporary param_names_
                        // insertion.
                        bool fake_param = !sit->second.is_mut_binding &&
                                          !param_names_.count(rname);
                        if (fake_param) param_names_.insert(rname);
                        take_borrow(rname, v.is_mut(), line, holder);
                        if (fake_param) param_names_.erase(rname);
                        break;
                    }
                }
                // Structural decomposition of the borrowed PLACE — single
                // foundation shared with the check pass (`visit::AddrOfTemp`).
                // `index_in_chain` flips on if any `[i]` was crossed; the path
                // is the field-chain to the indexed container (whole-element
                // borrow rule). See `extract_borrow_place`.
                BorrowPlace bp = extract_borrow_place(inner, pool);
                std::string root = bp.root;
                std::string path = bp.path;
                bool index_in_chain = bp.index_in_chain;
                if (!root.empty()) {
                    auto sit = states_.find(root);
                    if (sit != states_.end() && !path.empty()) {
                        // moved_fields key uses outermost field name only
                        // (matches B78 partial-move tracking granularity).
                        auto first_dot = path.find('.');
                        std::string outer = (first_dot == std::string::npos)
                                          ? path : path.substr(0, first_dot);
                        if (auto mit = sit->second.moved_fields.find(outer);
                            mit != sit->second.moved_fields.end()) {
                            report(line, std::format(
                                "use of moved field '{}.{}' (moved on line {})",
                                root, outer, mit->second));
                            break;
                        }
                    }
                    // §6.1 `items.union.ref.borrow`: a field-borrow on
                    // a union root is morally a whole-value borrow —
                    // all sibling fields alias.
                    bool root_is_union = is_union_root(bp.root_type);
                    if (index_in_chain) {
                        // Visit inner FIRST (sub-checks on the index expr etc.)
                        // BEFORE registering the borrow, else the recursive
                        // VarRef visit hits check_live on the root we just
                        // borrowed → spurious self-conflict.
                        if (inner) visit(inner, /*consuming=*/false, line);
                        if (!path.empty() && !root_is_union)
                            take_field_borrow(root, std::move(path), v.is_mut(), line);
                        else
                            take_borrow(root, v.is_mut(), line, holder);
                        break;
                    }
                    if (!path.empty()) {
                        bool is_mut = v.is_mut();
                        // §6.1 P40: when union-root redirects a field path
                        // borrow into a WHOLE-VALUE take_borrow, visiting
                        // `inner` AFTER the take would trigger check_live
                        // on the root VarRef and see the freshly-set
                        // mut_borrowed=true as a spurious self-conflict
                        // ("cannot use 'u' while it is mutably borrowed").
                        // The non-union path takes a field-path borrow
                        // (which doesn't set the root's mut_borrowed) so
                        // it's order-insensitive. Mirror the index_in_chain
                        // shape: visit inner FIRST for both branches.
                        if (inner) visit(inner, /*consuming=*/false, line);
                        if (root_is_union)
                            take_borrow(root, is_mut, line, holder);
                        else
                            take_field_borrow(root, std::move(path), is_mut, line);
                        break;
                    }
                }
                visit(e, /*consuming=*/true, line);
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                visit(v.cond(), /*consuming=*/true, line);
                take_ref_borrows(v.then_val(), line, holder);
                take_ref_borrows(v.else_val(), line, holder);
                break;
            }
            // Cross-fn provenance (conservative): when a function-call result is
            // bound to a ref (`let r = f(&a, &b)`), the returned ref MAY alias
            // any of the ref-typed input arguments — without per-fn signature
            // analysis, register a borrow on each ref-arg held by `holder` (the
            // let var). NLL releases at the holder's last use; `a = …` /
            // `&mut a` while r is live is now rejected. Sound (matches Rust's
            // elision conservative upper bound); may overshoot when the callee
            // tighter-binds the return to a specific input — refining that
            // needs per-fn signature provenance (future work).
            case Code::Call: {
                ECallView v{e};
                v.each_arg([&](ExprRef a) {
                    if (a && is_ref_kind(a.type(pool)))
                        take_ref_borrows(a, line, holder);
                });
                break;
            }
            case Code::MethodCall: {
                EMethodCallView v{e};
                if (auto recv = v.receiver(); recv && is_ref_kind(recv.type(pool)))
                    take_ref_borrows(recv, line, holder);
                v.each_arg([&](ExprRef a) {
                    if (a && is_ref_kind(a.type(pool)))
                        take_ref_borrows(a, line, holder);
                });
                break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                visit(v.scrut(), /*consuming=*/false, line);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
                    take_ref_borrows(arm.value(), line, holder);
                });
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                if (auto br = v.block()) {
                    auto it = prog_.mirror_table->block_by_offset.find(
                        br.offset().value());
                    if (it != prog_.mirror_table->block_by_offset.end())
                        visit_block(*it->second);
                }
                take_ref_borrows(v.result(), line, holder);
                break;
            }
            // P2-13: a non-`move` closure that MUTATES a capture holds a `&mut`
            // borrow of it for the closure's lifetime — registering that borrow
            // with the closure's holder ties release to its last use (NLL), so
            // the exclusivity machinery rejects e.g. reading/`&mut`-ing `x` while
            // a `x`-mutating closure is still live. A SHARED (read) capture is
            // left as a liveness check only: Logos captures a whole variable
            // (not a precise field path), so a whole-var shared borrow would
            // wrongly block RFC-2229 disjoint sibling mutation (`|| p.x` next to
            // `&mut p.y`). A `move` closure takes ownership — no borrow.
            case Code::ClosureBox: {
                // RFC-2229 phase-1: register a FIELD-PATH borrow per capture
                // (precise `p.x` instead of whole-var `p`). Disjoint sibling
                // access is sound — `&mut p.y` next to `||p.x` is allowed;
                // conflicting `&mut p.x` is rejected. Shared captures register
                // a shared field-path borrow (NLL-released at the holder's last
                // use); a `move` closure takes ownership — no borrow.
                EClosureBoxView cb{e};
                uint64_t i = 0;
                cb.each_capture_name([&](std::string_view cap) {
                    if (cb.is_move()) { ++i; return; }
                    std::string root(cap);
                    std::string_view fpath = cb.capture_path(i);
                    std::string rel;
                    if (fpath.size() > root.size() + 1 &&
                        fpath.compare(0, root.size(), root) == 0 &&
                        fpath[root.size()] == '.')
                        rel = std::string(fpath.substr(root.size() + 1));
                    bool is_mut = cb.capture_is_mut(i);
                    if (rel.empty()) {
                        // Whole-root capture: use the original whole-value
                        // borrow so plain reads of the root still see the
                        // borrow (mut_field_borrows on path="" is not checked
                        // on a bare variable read).
                        if (is_mut)
                            take_borrow(root, /*is_mut=*/true, line, holder);
                        else
                            check_live(root, line);
                    } else {
                        take_field_borrow(root, rel, is_mut, line);
                        check_live(root, line);
                    }
                    ++i;
                });
                break;
            }
            default:
                // EVarRef (ref param forwarded), ECall, EMethodCall, etc.
                visit(e, /*consuming=*/true, line);
                break;
        }
    }
    void take_ref_borrows(const LExprPtr& e, uint32_t line,
                           const std::string& holder = "") {
        if (!e) return;
        take_ref_borrows(expr_ref(e), line, holder);
    }

    // ── Statement visitor ─────────────────────────────────────────────────

    // Phase 9 (NLL): pre-pass over fn body computing the max line at which
    // each named local is read. The borrow checker uses this to release
    // borrows whose holder's last use has passed — making borrows non-lexical.
    void note_use(std::string name, uint32_t line) {
        if (name.empty()) return;
        auto& slot = last_use_line_[name];
        if (line > slot) slot = line;
    }

    void scan_uses_expr(lir_view::ExprRef e, uint32_t line) {
        if (!e) return;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        switch (e.kind()) {
            case Code::VarRef:
                note_use(std::string(EVarRefView{e}.name()), line);
                break;
            case Code::AddrOf:
                note_use(std::string(EAddrOfView{e}.var_name()), line);
                break;
            case Code::AddrOfTemp:
                scan_uses_expr(EAddrOfTempView{e}.inner(), line);
                break;
            case Code::Unary:
                scan_uses_expr(EUnaryView{e}.operand(), line);
                break;
            case Code::Deref:
                scan_uses_expr(EDerefView{e}.operand(), line);
                break;
            case Code::Cast:
                scan_uses_expr(ECastView{e}.operand(), line);
                break;
            case Code::Try:
                scan_uses_expr(ETryView{e}.inner(), line);
                break;
            case Code::FieldRead:
                scan_uses_expr(EFieldReadView{e}.receiver(), line);
                break;
            case Code::TupleIndex:
                scan_uses_expr(ETupleIndexView{e}.receiver(), line);
                break;
            case Code::SliceLen:
                scan_uses_expr(ESliceLenView{e}.slice(), line);
                break;
            case Code::SlicePtr:
                scan_uses_expr(ESlicePtrView{e}.slice(), line);
                break;
            case Code::BinOp: {
                EBinOpView v{e};
                scan_uses_expr(v.lhs(), line);
                scan_uses_expr(v.rhs(), line);
                break;
            }
            case Code::IndexRead: {
                EIndexReadView v{e};
                scan_uses_expr(v.receiver(), line);
                scan_uses_expr(v.index(), line);
                break;
            }
            case Code::SliceLit: {
                ESliceLitView v{e};
                scan_uses_expr(v.base(), line);
                scan_uses_expr(v.len(),  line);
                break;
            }
            case Code::SliceIndex: {
                ESliceIndexView v{e};
                scan_uses_expr(v.slice(), line);
                scan_uses_expr(v.index(), line);
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                scan_uses_expr(v.cond(),     line);
                scan_uses_expr(v.then_val(), line);
                scan_uses_expr(v.else_val(), line);
                break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                scan_uses_expr(v.scrut(), line);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) scan_uses_expr(g, line);
                    scan_uses_expr(arm.value(), line);
                });
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                if (auto br = v.block()) {
                    auto it = prog_.mirror_table->block_by_offset.find(br.offset().value());
                    if (it != prog_.mirror_table->block_by_offset.end())
                        scan_uses_block(*it->second);
                }
                if (auto r = v.result()) scan_uses_expr(r, line);
                break;
            }
            case Code::Call:
                ECallView{e}.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                scan_uses_expr(v.receiver(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::ClosureCall: {
                EClosureCallView v{e};
                scan_uses_expr(v.callee(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::FnPtrCall: {
                EFnPtrCallView v{e};
                scan_uses_expr(v.callee(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::FormatCall: {
                EFormatCallView v{e};
                scan_uses_expr(v.fmt(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv){ scan_uses_expr(fv, line); });
                break;
            case Code::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el){ scan_uses_expr(el, line); });
                break;
            case Code::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el){ scan_uses_expr(el, line); });
                break;
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){ scan_uses_expr(pl, line); });
                break;
            case Code::ClosureBox:
                EClosureBoxView{e}.each_capture_name([&](std::string_view cap){
                    note_use(std::string(cap), line);
                });
                break;
            case Code::PtrArith: {
                EPtrArithView v{e};
                scan_uses_expr(v.ptr(),    line);
                scan_uses_expr(v.offset(), line);
                break;
            }
            case Code::PtrDiff: {
                EPtrDiffView v{e};
                scan_uses_expr(v.lhs(), line);
                scan_uses_expr(v.rhs(), line);
                break;
            }
            default:
                break;
        }
    }

    void scan_uses_stmt(const LStmt& s) {
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        auto sr = stmt_ref(s);
        if (!sr) return;
        uint32_t ln = s.line;
        switch (sr.kind()) {
            case Code::Let:
                scan_uses_expr(SLetView{sr}.value(), ln);
                break;
            case Code::Assign:
                scan_uses_expr(SAssignView{sr}.value(), ln);
                break;
            case Code::Return:
                scan_uses_expr(SReturnView{sr}.value(), ln);
                break;
            case Code::ExprStmt:
                scan_uses_expr(SExprStmtView{sr}.expr(), ln);
                break;
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                note_use(std::string(v.arr()), ln);
                scan_uses_expr(v.index(), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.index(), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                scan_uses_expr(v.ptr(),   ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::If: {
                SIfView v{sr};
                scan_uses_expr(v.cond(), ln);
                if (auto b = block_ptr(v.then_block())) scan_uses_block(*b);
                if (auto b = block_ptr(v.else_block())) scan_uses_block(*b);
                break;
            }
            case Code::While: {
                SWhileView v{sr};
                scan_uses_expr(v.cond(), ln);
                if (auto b = block_ptr(v.body())) scan_uses_block(*b);
                break;
            }
            case Code::For: {
                SForView v{sr};
                scan_uses_expr(v.lo(), ln);
                scan_uses_expr(v.hi(), ln);
                if (auto b = block_ptr(v.body())) scan_uses_block(*b);
                break;
            }
            case Code::Loop:
                if (auto b = block_ptr(SLoopView{sr}.body())) scan_uses_block(*b);
                break;
            case Code::Block:
                if (auto b = block_ptr(SBlockView{sr}.body())) scan_uses_block(*b);
                break;
            case Code::ForEach: {
                SForEachView v{sr};
                scan_uses_expr(v.iter(), ln);
                if (auto b = block_ptr(v.body())) scan_uses_block(*b);
                break;
            }
            case Code::Match: {
                SMatchView v{sr};
                scan_uses_expr(v.scrut(), ln);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) scan_uses_expr(g, ln);
                    if (auto b = block_ptr(arm.body())) scan_uses_block(*b);
                });
                break;
            }
            case Code::LetElse: {
                SLetElseView v{sr};
                scan_uses_expr(v.scrut(), ln);
                if (auto b = block_ptr(v.else_block())) scan_uses_block(*b);
                break;
            }
            case Code::Break:
                scan_uses_expr(SBreakView{sr}.value(), ln);
                break;
            default:
                break;
        }
    }

    void scan_uses_block(const LBlock& blk) {
        for (auto& s : blk.stmts) scan_uses_stmt(s);
    }

    // Phase 9 (NLL): release borrows whose holder is no longer live.
    // Called after each statement in a block: if holder's max-use line is at or
    // before the current statement, the borrow has expired textually.
    void release_dead_borrows(uint32_t cur_line) {
        if (scopes_.empty()) return;
        auto& frame = scopes_.back();
        auto it = frame.borrows.begin();
        while (it != frame.borrows.end()) {
            if (it->holder.empty()) { ++it; continue; }
            uint32_t lu = 0;
            auto luit = last_use_line_.find(it->holder);
            if (luit != last_use_line_.end()) lu = luit->second;
            if (lu <= cur_line) {
                auto sit = states_.find(it->target);
                if (sit != states_.end()) {
                    if (it->is_mut) sit->second.mut_borrowed = false;
                    else if (sit->second.shared_borrows > 0)
                        --sit->second.shared_borrows;
                }
                it = frame.borrows.erase(it);
            } else {
                ++it;
            }
        }
    }

    void visit_block(const LBlock& blk) {
        push_scope();
        for (auto& s : blk.stmts) {
            visit_stmt(s);
            release_dead_borrows(s.line);
        }
        pop_scope();
    }

    // Analyse a loop body: outer variables moved/borrowed inside are propagated.
    // loop_vars are local to the loop iteration.
    void visit_loop_body(const LBlock& body,
                         const std::vector<std::string>& loop_vars = {}) {
        auto pre_s = states_;
        auto pre_p = prov_;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        for (auto& s : body.stmts) visit_stmt(s);
        pop_scope();
        // Borrows released by pop_scope; propagate only moves of outer vars.
        // For provenance, merge conservatively (loop may run 0 or more times).
        auto post_s = states_;
        auto post_p = prov_;
        states_ = pre_s;
        prov_   = pre_p;
        for (auto& [name, st] : post_s)
            if (st.moved && pre_s.count(name))
                states_[name] = st;
        merge_provs(prov_, post_p);
    }

    void visit_stmt(const LStmt& stmt) {
        uint32_t ln = stmt.line;
        auto sr = stmt_ref(stmt);
        if (!sr) return;
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (sr.kind()) {
            // ── Let binding ──────────────────────────────────────────────
            case Code::Let: {
                SLetView v{sr};
                auto val = v.value();
                auto t   = v.type(pool);
                std::string name(v.name());
                // P2-13: a closure binding routes through take_ref_borrows too,
                // so its by-ref captures register as borrows held by `name` (the
                // closure var) — released at the closure's last use (NLL).
                bool is_closure_t = t && t.kind() == LogosType::Kind::Closure;
                if (val && (is_ref_kind(t) || is_closure_t)) {
                    take_ref_borrows(val, ln, name);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
                }
                declare_var(name);
                if (auto it = states_.find(name); it != states_.end())
                    it->second.is_mut_binding = v.is_mut();
                if (is_ref_kind(t))
                    prov_[name] = prov_of(val);
                else if (t && !t.lifetime_args().empty() &&
                         (t.kind() == LogosType::Kind::Struct ||
                          t.kind() == LogosType::Kind::ZonedStruct))
                    prov_[name] = prov_of(val);  // struct<'z> borrows through lifetime
                // B87 dropck: record local borrow sources for Drop-lt bindings.
                if (val && struct_is_dropck_relevant(t)) {
                    std::vector<std::string> sources;
                    collect_borrow_locals(val, sources);
                    if (!sources.empty()) {
                        dropck_borrow_sources_[name] = std::move(sources);
                        dropck_binding_line_[name] = ln;
                    }
                }
                break;
            }

            // ── Assignment ───────────────────────────────────────────────
            case Code::Assign: {
                SAssignView v{sr};
                auto val = v.value();
                std::string name(v.name());
                // B87 (dropck-light): an assignment that fills a previously-
                // declared Drop-having lifetime-parameterised struct with a
                // freshly-borrowed local is unsafe — the binding survives
                // beyond this scope and its Drop will reference the borrow
                // after the source dies. Detect the pattern syntactically.
                // B74 gap: writing to a variable while it has any active
                // borrow violates exclusivity. mut_borrowed already errored
                // via check_live elsewhere; the missing case was shared
                // borrows. Catch them here.
                if (auto it = states_.find(name); it != states_.end()) {
                    if (it->second.shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}' because it is borrowed", name));
                    if (it->second.mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}' while it is mutably borrowed", name));
                }
                bool is_ref_assign = val &&
                    (prov_.count(name) || is_ref_kind(val.type(pool)));
                if (is_ref_assign) {
                    take_ref_borrows(val, ln, name);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
                }
                if (states_.count(name))
                    states_[name] = VarState{};  // re-own
                if (is_ref_assign)
                    prov_[name] = prov_of(val);
                // B87 dropck: record on (re-)assign too.
                if (val) {
                    auto vt = val.type(pool);
                    if (struct_is_dropck_relevant(vt)) {
                        std::vector<std::string> sources;
                        collect_borrow_locals(val, sources);
                        if (!sources.empty()) {
                            dropck_borrow_sources_[name] = std::move(sources);
                            dropck_binding_line_[name] = ln;
                        }
                    }
                }
                break;
            }

            // ── Return ───────────────────────────────────────────────────
            case Code::Return: {
                if (auto val = SReturnView{sr}.value()) {
                    check_return_value(val, ln);
                    visit(val, /*consuming=*/true, ln);
                }
                cur_diverged_ = true;
                break;
            }

            // ── Expression statement ─────────────────────────────────────
            case Code::ExprStmt:
                visit(SExprStmtView{sr}.expr(), /*consuming=*/true, ln);
                break;

            // ── Field write: recv.field = value ──────────────────────────
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                // §2 Wave 9 — assigning to a moved-out field REINITIALIZES it.
                // The previous "partially moved" state on the receiver is
                // cleared for THIS specific field before check_live runs so
                // `let _ = s.v; s.v = …;` re-binds the field without rejecting
                // the receiver as partially-moved. Rust's NLL move analysis
                // does the same — drop-flag tracking is replaced on the new
                // bits.
                std::string recv_nm(v.receiver());
                std::string field_nm(v.field());
                if (!recv_nm.empty() && !field_nm.empty()) {
                    if (auto it = states_.find(recv_nm); it != states_.end())
                        it->second.moved_fields.erase(field_nm);
                }
                check_live(recv_nm, ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Index write: arr[i] = value ──────────────────────────────
            // Element borrow gap: `arr[i] = v` writes through the container,
            // so it must respect existing borrows on the container — mirror
            // the SAssign exclusivity check (any active shared or mut borrow
            // of `arr` blocks the write). This matches Rust: an `&arr[i]`
            // shared borrow blocks any element write; `&mut arr[i]` blocks it.
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                std::string nm(v.arr());
                if (auto it = states_.find(nm); it != states_.end()) {
                    if (it->second.shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}[..]' because '{}' is borrowed",
                            nm, nm));
                    if (it->second.mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}[..]' while '{}' is mutably borrowed",
                            nm, nm));
                }
                check_live(nm, ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Field-index write: recv.field[i] = value ─────────────────
            // Same exclusivity check as IndexWrite — writing through the
            // receiver respects borrows on the receiver.
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                std::string nm(v.receiver());
                if (auto it = states_.find(nm); it != states_.end()) {
                    if (it->second.shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}.{}[..]' because '{}' is borrowed",
                            nm, std::string(v.field()), nm));
                    if (it->second.mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}.{}[..]' while '{}' is mutably borrowed",
                            nm, std::string(v.field()), nm));
                }
                check_live(nm, ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Chain field write: recv.mid.field = value ────────────────
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Deref-field write: (*recv).field = value ─────────────────
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Deref write: *ptr = value ─────────────────────────────────
            // `arr[i] = v` and other place writes lower to SDerefWrite of
            // AddrOfTemp(<place>). When the place is `<root>[i]…` / `<root>.f[i]…`,
            // the write conflicts with any active borrow on the root (Rust:
            // `&arr[0]; arr[1] = …` rejected — shared borrow blocks any element
            // write; `&mut arr[0]; arr[1] = …` also rejected). Walk the AddrOfTemp
            // chain through FieldRead/IndexRead/SliceIndex to extract the root,
            // then check the root's borrow state directly (no lasting borrow:
            // the write is transient at this statement).
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                auto ptr = v.ptr();
                using EC = lir_schema::expr::Code;
                if (ptr && ptr.kind() == EC::AddrOfTemp) {
                    EAddrOfTempView atv{ptr};
                    auto cur = atv.inner();
                    bool saw_index = false;
                    while (cur) {
                        if (cur.kind() == EC::FieldRead)      cur = EFieldReadView{cur}.receiver();
                        else if (cur.kind() == EC::IndexRead) { saw_index = true; cur = EIndexReadView{cur}.receiver(); }
                        else if (cur.kind() == EC::SliceIndex){ saw_index = true; cur = ESliceIndexView{cur}.slice(); }
                        else if (cur.kind() == EC::TupleIndex){ cur = ETupleIndexView{cur}.receiver(); }
                        else break;
                    }
                    if (saw_index && cur && cur.kind() == EC::VarRef) {
                        std::string root(EVarRefView{cur}.name());
                        if (auto it = states_.find(root); it != states_.end()) {
                            if (it->second.shared_borrows > 0)
                                report(ln, std::format(
                                    "cannot assign through '{}[..]' because '{}' is borrowed",
                                    root, root));
                            if (it->second.mut_borrowed)
                                report(ln, std::format(
                                    "cannot assign through '{}[..]' while '{}' is mutably borrowed",
                                    root, root));
                        }
                    }
                    // §2 Wave 9 — reinit of a moved-out field. The simplest
                    // place shape `s.f = …` lowers to
                    // `SDerefWrite(AddrOfTemp(FieldRead(VarRef s, f)), val)`.
                    // Before visiting the LHS (which walks the FieldRead and
                    // would trip the moved-field check), clear
                    // moved_fields[f] on the root — the assignment
                    // reinitialises the field. Rust's NLL drop-flag analysis
                    // does the same: the new bits replace the old.
                    auto inner = atv.inner();
                    if (inner && inner.kind() == EC::FieldRead) {
                        EFieldReadView fv{inner};
                        auto recv = fv.receiver();
                        if (recv && recv.kind() == EC::VarRef) {
                            std::string root(EVarRefView{recv}.name());
                            std::string field(fv.field());
                            if (auto it = states_.find(root); it != states_.end())
                                it->second.moved_fields.erase(field);
                        }
                    }
                }
                visit(v.ptr(),   /*consuming=*/false, ln);
                visit(v.value(), /*consuming=*/true,  ln);
                break;
            }

            // ── Tuple field write: var.N = value ──────────────────────────
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── SDrop — compiler-generated, no-op in borrow checker ───────
            case Code::Drop:
                break;

            // ── If / else ────────────────────────────────────────────────
            case Code::If: {
                SIfView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                bool saved_div = cur_diverged_;
                cur_diverged_ = false;
                if (auto then_b = block_ptr(v.then_block())) visit_block(*then_b);
                auto then_s = states_;
                auto then_p = prov_;
                bool then_div = cur_diverged_;
                states_ = saved_s;
                prov_   = saved_p;
                cur_diverged_ = false;
                if (auto else_b = block_ptr(v.else_block())) visit_block(*else_b);
                bool else_div = cur_diverged_;
                // A diverged branch (return/break/continue at the join point)
                // contributes nothing to the post-if move state — only the
                // surviving branch propagates. If BOTH diverged, the whole if
                // diverges. If ELSE diverged, the post-if state IS then's.
                if (then_div && else_div) {
                    cur_diverged_ = true;
                } else if (then_div) {
                    cur_diverged_ = saved_div;  // states_ = else's
                } else if (else_div) {
                    states_ = then_s;
                    prov_   = then_p;
                    cur_diverged_ = saved_div;
                } else {
                    merge_moves(states_, then_s);
                    merge_provs(prov_,   then_p);
                    cur_diverged_ = saved_div;
                }
                break;
            }

            // ── While loop ───────────────────────────────────────────────
            case Code::While: {
                SWhileView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                if (auto b = block_ptr(v.body())) visit_loop_body(*b);
                break;
            }

            // ── For range loop ───────────────────────────────────────────
            case Code::For: {
                SForView v{sr};
                visit(v.lo(), /*consuming=*/true, ln);
                visit(v.hi(), /*consuming=*/true, ln);
                if (auto b = block_ptr(v.body()))
                    visit_loop_body(*b, {std::string(v.var())});
                break;
            }

            // ── Infinite loop ─────────────────────────────────────────────
            case Code::Loop:
                if (auto b = block_ptr(SLoopView{sr}.body())) visit_loop_body(*b);
                break;

            // ── Scoping block ─────────────────────────────────────────────
            case Code::Block:
                if (auto b = block_ptr(SBlockView{sr}.body())) visit_block(*b);
                break;

            // ── For-each loop ─────────────────────────────────────────────
            case Code::ForEach: {
                SForEachView v{sr};
                visit(v.iter(), /*consuming=*/false, ln);
                if (auto b = block_ptr(v.body()))
                    visit_loop_body(*b, {std::string(v.var())});
                break;
            }

            // ── Match statement ───────────────────────────────────────────
            case Code::Match: {
                SMatchView v{sr};
                visit(v.scrut(), /*consuming=*/false, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                std::optional<StateMap> merged_s;
                std::optional<ProvMap>  merged_p;
                v.each_arm([&](EMatchArmRef arm) {
                    states_ = saved_s;
                    prov_   = saved_p;
                    push_scope();
                    declare_pat_bindings(arm.pat());
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, ln);
                    if (auto body = block_ptr(arm.body())) visit_block(*body);
                    pop_scope();
                    if (!merged_s) {
                        merged_s = states_;
                        merged_p = prov_;
                    } else {
                        for (auto& [name, st] : states_)
                            if (st.moved && saved_s.count(name))
                                (*merged_s)[name] = st;
                        merge_provs(*merged_p, prov_);
                    }
                });
                if (merged_s) {
                    for (auto& [name, st] : *merged_s)
                        if (saved_s.count(name)) states_[name] = st;
                    merge_provs(prov_, *merged_p);
                }
                break;
            }

            // SBreak, SContinue, LetElse — no variable effects in this pass,
            // but Break/Continue still diverge the current stmt-flow.
            case Code::Break:
            case Code::Continue:
                cur_diverged_ = true;
                break;
            default:
                break;
        }
    }

public:
    BorrowChecker(SemaResult& diags, std::string fn_name,
                  const lir::LProgram& prog, const TypeSets& ts,
                  bool exclusivity_only = false,
                  const RegionInferer* ri = nullptr)
        : diags_(diags), fn_name_(std::move(fn_name)), prog_(prog), ts_(ts),
          ri_(ri), exclusivity_only_(exclusivity_only) {}
    // P2-10: when checking GENERIC templates pre-mono, move/use-after-move
    // tracking is imprecise (TypeVar values + generic method-call move
    // semantics → false positives like a spurious "use of moved 'out'"). In
    // that mode we report only borrow-exclusivity conflicts (which are sound
    // without concrete types) and suppress move-related diagnostics — the
    // concrete moves are fully checked on the monomorphized specializations.
    bool exclusivity_only_ = false;

    void check(const LFunction& fn) {
        states_.clear();
        scopes_.clear();
        prov_.clear();
        param_names_.clear();
        param_lifetimes_.clear();
        last_use_line_.clear();
        fn_lifetime_params_ = fn.lifetime_params;
        outlives_adj_ = outlives_adj(fn.lifetime_outlives);
        ret_type_ = fn.ret_type;

        scan_uses_block(fn.body);

        push_scope();  // function scope
        for (auto& p : fn.params) {
            declare_var(p.name);
            param_names_.insert(p.name);
            if (is_ref_kind(p.type)) {
                param_lifetimes_[p.name] = std::string(TypeRef(p.type).lifetime());
                // B86: capture inner-struct lifetime_args.
                auto pointee = TypeRef(p.type).pointee();
                if (pointee && (pointee.kind() == LogosType::Kind::Struct ||
                                pointee.kind() == LogosType::Kind::ZonedStruct ||
                                pointee.kind() == LogosType::Kind::Enum)) {
                    // B86: capture the pointee's explicit lt_args if any.
                    // Even when empty (e.g. `&Self` where Self isn't yet
                    // resolved with impl-level lt_args), record an empty
                    // vector — its presence signals "param points to an
                    // aggregate; trust type-checker for inner lifetime
                    // structure" in check_return_value.
                    std::vector<std::string> lts;
                    for (auto& lt : pointee.lifetime_args()) lts.push_back(lt);
                    param_inner_lifetimes_[p.name] = std::move(lts);
                }
            }
        }

        visit_block(fn.body);
        pop_scope();
    }
};

// Expression visitor — out-of-line.

void BorrowChecker::visit(lir_view::ExprRef e, bool consuming, uint32_t line) {
    if (!e) return;
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    const auto* pool = prog_.type_pool.impl();

    // Helper: visit a sequence of call arguments via per-view each_arg.
    // EAddrOf args (including those nested in if/match) create call-site
    // borrows released when the scope pops after the call.
    auto visit_args = [&](auto&& view) {
        push_scope();  // call-site borrow scope
        in_call_args_++;
        view.each_arg([&](ExprRef a) {
            if (a && is_ref_kind(a.type(pool))) take_ref_borrows(a, line);
            else                                visit(a, /*consuming=*/true, line);
        });
        in_call_args_--;
        pop_scope();
    };

    switch (e.kind()) {
        // ── Variable reference ─────────────────────────────────────────
        case Code::VarRef: {
            EVarRefView v{e};
            std::string name(v.name());
            if (consuming && is_move_type(e.type(pool), prog_, ts_))
                consume(name, line);
            else
                check_live(name, line);
            break;
        }

        // ── Address-of: &x or &mut x ──────────────────────────────────
        // When EAddrOf appears directly in visit (not as SLet/SAssign RHS),
        // this is a transient borrow — caller handles scope. We verify the
        // source is alive AND, for `&mut x`, that the binding was declared mut.
        case Code::AddrOf: {
            std::string vname(EAddrOfView{e}.var_name());
            check_live(vname, line);
            if (is_mut_ref(e.type(pool))) {
                if (auto it = states_.find(vname); it != states_.end()) {
                    if (!it->second.is_mut_binding && !param_names_.count(vname))
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: not declared as mut",
                            vname));
                }
            }
            break;
        }
        // B81 + B93.2: method-call receivers (and other implicit
        // borrow-takes via AddrOfTemp) need the same path-aware conflict
        // checks as explicit `&mut x` / `&mut x.f`. Decompose via the same
        // foundation as the RECORD pass so the structural rule cannot drift
        // — pre-foundation this site only walked FieldRead chains and
        // silently mis-handled `&mut self.data[i]` (no Index branch). The
        // POLICY here is check-only: AddrOfTemp in visit() is a transient
        // auto-borrow (method-call receiver, arg materialization, …) — NLL
        // releases at the enclosing scope-pop. Recording for explicit
        // let-bindings `let r = &mut p.f` happens via take_ref_borrows.
        case Code::AddrOfTemp: {
            EAddrOfTempView v{e};
            auto inner = v.inner();
            bool is_mut = is_mut_ref(e.type(pool));
            BorrowPlace bp = extract_borrow_place(inner, pool);
            std::string root = bp.root;
            std::string path = bp.path;
            // B93.2: a raw-pointer or `&mut T` root means the borrow goes
            // THROUGH the reference — skip mut-binding + field-borrow
            // tracking (writing through the ref needs no `mut r` binding,
            // Rust parity).
            bool root_is_raw_ptr =
                bp.root_type &&
                (bp.root_type.kind() == LogosType::Kind::Ptr ||
                 bp.root_type.kind() == LogosType::Kind::MutRef);
            if (!root.empty() && !root_is_raw_ptr && states_.count(root)) {
                auto sit = states_.find(root);
                // Mut-binding check (root-level).
                if (is_mut && !sit->second.is_mut_binding
                    && !param_names_.count(root))
                    report(line, std::format(
                        "cannot borrow '{}' as mutable: not declared as mut",
                        root));
                // moved_fields check for FieldRead chains.
                if (!path.empty()) {
                    auto first_dot = path.find('.');
                    std::string outer = (first_dot == std::string::npos)
                                        ? path : path.substr(0, first_dot);
                    if (auto mit = sit->second.moved_fields.find(outer);
                        mit != sit->second.moved_fields.end()) {
                        report(line, std::format(
                            "use of moved field '{}.{}' (moved on line {})",
                            root, outer, mit->second));
                        break;
                    }
                }
                // B94: AddrOfTemp in visit() is a transient auto-borrow
                // (method-call receiver, arg materialization, etc.) — NLL
                // releases it at call-end via scope-pop. Only CHECK against
                // existing borrows; don't record (which would conflict with
                // TPB-style nested method calls and field-receiver chains
                // like `c.v.set(c.v.get() + 1)`).
                // Recording for explicit let-bindings `let r = &mut p.f`
                // happens via take_ref_borrows from the Let handler.
                {
                    std::string self_disp = fmt_path(root, path);
                    if (sit->second.mut_borrowed) {
                        report(line, std::format(
                            "cannot borrow '{}': '{}' is already mutably borrowed",
                            self_disp, root));
                        break;
                    }
                    if (is_mut && sit->second.shared_borrows > 0) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: '{}' has shared borrows",
                            self_disp, root));
                        break;
                    }
                    for (auto& [p, c] : sit->second.shared_field_borrows) {
                        if (c <= 0) continue;
                        if (paths_overlap(path, p) && is_mut) {
                            report(line, std::format(
                                "cannot borrow '{}' as mutable: '{}' is already borrowed",
                                self_disp, fmt_path(root, p)));
                            goto addrof_temp_done;
                        }
                    }
                    for (auto& p : sit->second.mut_field_borrows) {
                        if (paths_overlap(path, p)) {
                            report(line, std::format(
                                "cannot borrow '{}': '{}' is already mutably borrowed",
                                self_disp, fmt_path(root, p)));
                            goto addrof_temp_done;
                        }
                    }
                    addrof_temp_done:;
                }
            }
            if (inner) visit(inner, /*consuming=*/false, line);
            break;
        }

        // ── Dereference: *ptr ──────────────────────────────────────────
        case Code::Deref:
            visit(EDerefView{e}.operand(), /*consuming=*/false, line);
            break;

        // ── Field read: recv.field ─────────────────────────────────────
        case Code::FieldRead: {
            EFieldReadView v{e};
            auto recv = v.receiver();
            std::string field(v.field());
            // Partial-move tracking: when `o.f` is used in a consuming
            // position (e.g. moved into a fn arg or let RHS) and `f` is a
            // move-type field, mark `f` as moved on `o`. Subsequent reads
            // of `o.f` or whole-value `o` then error.
            std::string recv_name;
            if (recv && recv.kind() == Code::VarRef)
                recv_name = std::string(EVarRefView{recv}.name());
            if (!recv_name.empty()) {
                if (auto it = states_.find(recv_name); it != states_.end()) {
                    if (auto fit = it->second.moved_fields.find(field);
                        fit != it->second.moved_fields.end()) {
                        report(line, std::format(
                            "use of moved field '{}.{}' (moved on line {})",
                            recv_name, field, fit->second));
                        break;
                    }
                    if (consuming && is_move_type(e.type(pool), prog_, ts_)) {
                        it->second.moved_fields[field] = line;
                        break;
                    }
                }
            }
            visit(recv, /*consuming=*/false, line);
            break;
        }

        // ── Index read: arr[i] ─────────────────────────────────────────
        case Code::IndexRead: {
            EIndexReadView v{e};
            visit(v.receiver(), /*consuming=*/false, line);
            visit(v.index(),    /*consuming=*/true,  line);
            break;
        }

        // ── Tuple index: t.N ──────────────────────────────────────────
        case Code::TupleIndex:
            visit(ETupleIndexView{e}.receiver(), /*consuming=*/false, line);
            break;

        // ── Method call: recv.method(args) ────────────────────────────
        // Receiver is typically &mut self — already wrapped in EAddrOf.
        // B93.2: scope the receiver borrow to the call — otherwise its
        // implicit &mut self leaks past the call site and conflicts with
        // any subsequent method-call (consecutive `b.foo(); b.bar();`).
        case Code::MethodCall: {
            EMethodCallView v{e};
            push_scope();
            visit(v.receiver(), /*consuming=*/false, line);
            visit_args(v);
            pop_scope();
            break;
        }

        // ── Free function call: f(args) ───────────────────────────────
        case Code::Call:
            visit_args(ECallView{e});
            break;

        // ── Closure call ───────────────────────────────────────────────
        case Code::ClosureCall: {
            EClosureCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Fn-pointer call ────────────────────────────────────────────
        case Code::FnPtrCall: {
            EFnPtrCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Binary / Unary ─────────────────────────────────────────────
        case Code::BinOp: {
            EBinOpView v{e};
            visit(v.lhs(), /*consuming=*/true, line);
            visit(v.rhs(), /*consuming=*/true, line);
            break;
        }
        case Code::Unary:
            visit(EUnaryView{e}.operand(), /*consuming=*/true, line);
            break;

        // ── Cast ───────────────────────────────────────────────────────
        case Code::Cast:
            visit(ECastView{e}.operand(), consuming, line);
            break;

        // ── Struct literal ─────────────────────────────────────────────
        case Code::StructLit:
            EStructLitView{e}.each_field_value([&](ExprRef fv) {
                visit(fv, /*consuming=*/true, line);
            });
            break;

        // ── Array literal ──────────────────────────────────────────────
        case Code::ArrLit:
            EArrLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Tuple literal ──────────────────────────────────────────────
        case Code::TupleLit:
            ETupleLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Enum literal with payload ──────────────────────────────────
        case Code::EnumLitData:
            EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                visit(pl, /*consuming=*/true, line);
            });
            break;

        // ── If expression ──────────────────────────────────────────────
        case Code::IfExpr: {
            EIfExprView v{e};
            visit(v.cond(), /*consuming=*/true, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            visit(v.then_val(), consuming, line);
            auto then_s = states_;
            auto then_p = prov_;
            states_ = saved_s;
            prov_   = saved_p;
            visit(v.else_val(), consuming, line);
            merge_moves(states_, then_s);
            merge_provs(prov_,   then_p);
            break;
        }

        // ── Match expression ───────────────────────────────────────────
        case Code::MatchExpr: {
            EMatchExprView v{e};
            visit(v.scrut(), /*consuming=*/false, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            std::optional<StateMap> merged_s;
            std::optional<ProvMap>  merged_p;
            v.each_arm([&](EMatchArmRef arm) {
                states_ = saved_s;
                prov_   = saved_p;
                push_scope();
                declare_pat_bindings(arm.pat());
                if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
                visit(arm.value(), consuming, line);
                pop_scope();
                if (!merged_s) {
                    merged_s = states_;
                    merged_p = prov_;
                } else {
                    for (auto& [name, st] : states_)
                        if (st.moved && saved_s.count(name))
                            (*merged_s)[name] = st;
                    merge_provs(*merged_p, prov_);
                }
            });
            if (merged_s) {
                for (auto& [name, st] : *merged_s)
                    if (saved_s.count(name)) states_[name] = st;
                merge_provs(prov_, *merged_p);
            }
            break;
        }

        // ── Try expression: expr? ──────────────────────────────────────
        case Code::Try:
            visit(ETryView{e}.inner(), consuming, line);
            break;

        // ── Slice ──────────────────────────────────────────────────────
        case Code::SliceLit: {
            ESliceLitView v{e};
            visit(v.base(), /*consuming=*/false, line);
            visit(v.len(),  /*consuming=*/true,  line);
            break;
        }
        case Code::SliceIndex: {
            ESliceIndexView v{e};
            visit(v.slice(), /*consuming=*/false, line);
            visit(v.index(), /*consuming=*/true,  line);
            break;
        }
        case Code::SliceLen:
            visit(ESliceLenView{e}.slice(), /*consuming=*/false, line);
            break;
        case Code::SlicePtr:
            visit(ESlicePtrView{e}.slice(), /*consuming=*/false, line);
            break;

        // ── Format call ────────────────────────────────────────────────
        case Code::FormatCall: {
            EFormatCallView v{e};
            visit(v.fmt(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Closure box ────────────────────────────────────────────────
        case Code::ClosureBox:
            EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {
                check_live(std::string(cap), line);
            });
            break;

        // ── Block expression ───────────────────────────────────────────
        case Code::BlockExpr: {
            EBlockExprView v{e};
            if (auto br = v.block()) {
                auto it = prog_.mirror_table->block_by_offset.find(
                    br.offset().value());
                if (it != prog_.mirror_table->block_by_offset.end())
                    visit_block(*it->second);
            }
            if (auto r = v.result()) visit(r, consuming, line);
            break;
        }

        // ── Literals / compile-time nodes — no ownership effects ───────
        default:
            break;
    }
}

// ── Pass entry point ────────────────────────────────────────────────────────

lir::LProgram borrow_check(lir::LProgram prog, bool generic_templates_only) {
    const TypeSets ts = build_type_sets(prog);

    auto check = [&](const LFunction& fn) {
        if (fn.is_extern)             return;
        bool is_generic = !fn.type_params.empty();
        // P2-10: a dedicated PRE-mono pass (generic_templates_only) checks generic
        // fn bodies directly — so a generic that is never instantiated (no
        // specialization) is still borrow-checked (Rust parity). It runs in
        // exclusivity-only mode (move tracking is imprecise on TypeVars) and skips
        // region inference (also imprecise on generics). The normal POST-mono pass
        // checks concrete fns + specializations and ignores any leftover generics.
        if (generic_templates_only != is_generic) return;
        // logos-core 2.1 (consumer): run region_infer FIRST so its
        // named-lifetime regions and solved Outlives graph are available
        // to borrow_check. Both passes now consult the SAME source for
        // declared `'a: 'b` outlives, removing the parallel-paths drift
        // risk that the audit's #1 cross-category finding called out
        // (region_infer scaffolding-only). Generic templates skip
        // region inference (imprecise on TypeVars).
        RegionInferer ri;
        if (!generic_templates_only)
            ri.analyze(fn, prog);
        BorrowChecker(prog.diags, "fn " + std::string(bare_fn_name(fn.name)),
                      prog, ts, /*exclusivity_only=*/generic_templates_only,
                      generic_templates_only ? nullptr : &ri).check(fn);
        if (generic_templates_only) return;
        if (std::getenv("LOGOS_DUMP_REGIONS"))
            ri.dump(std::string(bare_fn_name(fn.name)));
        // B72/B73: region-based conflict diagnostics. Phrased in
        // Rust-style so they read naturally alongside B61's lexical
        // messages. The "later" borrow (by source line) is the
        // offending one; the "earlier" is referenced as the still-
        // live one.
        auto conflicts = ri.find_conflicts();
        for (auto& c : conflicts) {
            const BorrowSite* first  = c.a;
            const BorrowSite* second = c.b;
            if (second->origin_line < first->origin_line)
                std::swap(first, second);
            const char* kind_first  = first->is_mut  ? "mutable" : "shared";
            const char* kind_second = second->is_mut ? "mutable" : "shared";
            std::string target_label = second->target;
            if (target_label.starts_with("<temp")) target_label = "temporary";
            Diag d;
            d.level   = Diag::Level::Error;
            d.context = "fn " + std::string(bare_fn_name(fn.name));
            d.message = std::format(
                "cannot borrow '{}' as {}: {} borrow still in scope here "
                "(first borrowed at line {}, conflicting use at line {})",
                target_label, kind_second, kind_first,
                first->origin_line, second->origin_line);
            d.line = second->origin_line;
            prog.diags.diags.push_back(std::move(d));
        }
    };

    for (auto& fn : prog.functions)       check(*fn);
    for (auto& fn : prog.specializations) check(*fn);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)        check(*m);

    // P2-10: a generic template and each of its monomorphizations are checked
    // separately and report the SAME borrow error (same context/line/message —
    // `bare_fn_name` strips the mono suffix). De-duplicate identical diagnostics
    // so the user sees one error, not one per instantiation.
    {
        auto& ds = prog.diags.diags;
        std::vector<Diag> uniq;
        uniq.reserve(ds.size());
        std::unordered_set<std::string> seen;
        for (auto& d : ds) {
            std::string key = std::to_string((int)d.level) + "\x1f" +
                              std::to_string(d.line) + "\x1f" + d.context +
                              "\x1f" + d.message;
            if (seen.insert(key).second) uniq.push_back(std::move(d));
        }
        ds = std::move(uniq);
    }

    return prog;
}

} // namespace logos::compiler
