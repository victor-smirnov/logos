name: shframe
file: src/compiler/sema_impl.hpp
---
        if (!scope_.empty()) {
            auto sname = std::string(name);
            if (!scope_.back().vars.count(sname))
                scope_.back().var_order.push_back(sname);
            // Phase-1: fresh dense slot per binding (shadowing → new slot),
            // unless a pattern pre-reserved one at build time.
            uint32_t slot = (reuse_slot == 0xFFFFFFFFu) ? next_slot_++ : reuse_slot;
            scope_.back().vars[sname] = {t, is_mut, false, slot};
        }
    }
---
        if (!scope_.empty()) {
            auto sname = std::string(name);
            // Phase-1: fresh dense slot per binding (shadowing → new slot),
            // unless a pattern pre-reserved one at build time.
            uint32_t slot = (reuse_slot == 0xFFFFFFFFu) ? next_slot_++ : reuse_slot;
            shadow_prepare(sname, slot);  // PROBE 2026-09-14p-shadowslot
            if (!scope_.back().vars.count(sname))
                scope_.back().var_order.push_back(sname);
            scope_.back().vars[sname] = {t, is_mut, false, slot};
        }
    }

    // PROBE 2026-09-14p-shadowslot (shslot / shframe / shmoved) — see src/compiler/PROBES.md.
    void shadow_prepare(const std::string& sname, uint32_t slot) {
        auto& fr = scope_.back();
        auto old = fr.vars.find(sname);
        bool same = old != fr.vars.end() && old->second.slot != slot;
        bool outer = old == fr.vars.end() && lookup_var_info(sname) != nullptr;
        auto is_path_of = [&](const std::string& m) {
            return m == sname || (m.size() > sname.size() && m[sname.size()] == '.' &&
                                  m.compare(0, sname.size(), sname) == 0);
        };
        std::vector<std::string> mv;
        if (same || outer)
            for (auto& m : moved_vars_) if (is_path_of(m)) mv.push_back(m);
        if (same)  logos::probe::census(mv.empty() ? "shadow.same_frame.live" : "shadow.same_frame.moved");
        if (outer) logos::probe::census(mv.empty() ? "shadow.outer_frame.live" : "shadow.outer_frame.moved");
        bool arm_frame = logos::probe::on("shslot") || logos::probe::on("shframe");
        bool arm_moved = arm_frame || logos::probe::on("shmoved");
        if (same && arm_frame) {
            std::string hid = sname + std::string(1, '\x1f') + std::to_string(old->second.slot);
            VarInfo keep = old->second;
            for (auto& e : fr.var_order) if (e == sname) { e = hid; break; }
            fr.vars.erase(sname);
            fr.vars[hid] = keep;
            auto rekey = [&](const std::string& m) { return hid + m.substr(sname.size()); };
            for (auto& m : mv) { moved_vars_.erase(m); moved_vars_.insert(rekey(m)); }
            std::vector<std::pair<std::string, std::string>> fl;
            for (auto& [k, v] : fr.cond_move_flags) if (is_path_of(k)) fl.emplace_back(k, v);
            for (auto& [k, v] : fl) { fr.cond_move_flags.erase(k); fr.cond_move_flags[rekey(k)] = v; }
            std::vector<std::string> sm;
            for (auto& m : fr.cond_move_static_moves) if (is_path_of(m)) sm.push_back(m);
            for (auto& m : sm) { fr.cond_move_static_moves.erase(m); fr.cond_move_static_moves.insert(rekey(m)); }
            return;
        }
        if (same && logos::probe::on("shmoved")) { for (auto& m : mv) moved_vars_.erase(m); return; }
        if (outer && arm_moved)
            for (auto& m : mv) { moved_vars_.erase(m); fr.shadow_saved_moves.push_back(m); }
    }
    static std::string shadow_user_name(const std::string& n) {
        auto p = n.find('\x1f');
        return p == std::string::npos ? n : n.substr(0, p);
    }
    static uint32_t shadow_drop_slot(uint32_t s) {
        return (logos::probe::on("shslot") || logos::probe::on("shslotmg")) ? s : 0xFFFFFFFFu;
    }
===
name: shframe
file: src/compiler/sema_impl.hpp
---
            for (auto& name : scope_.back().var_order)
                moved_vars_.erase(name);
            scope_.pop_back();
---
            for (auto& name : scope_.back().var_order)
                moved_vars_.erase(name);
            auto shadow_saved = std::move(scope_.back().shadow_saved_moves);  // PROBE 2026-09-14p-shadowslot
            scope_.pop_back();
            for (auto& m : shadow_saved) moved_vars_.insert(m);
===
name: shframe
file: src/compiler/sema_impl.hpp
---
        std::set<std::string> cond_move_static_moves;
    };
---
        std::set<std::string> cond_move_static_moves;
        std::vector<std::string> shadow_saved_moves;  // PROBE 2026-09-14p-shadowslot
    };
===
name: shslotmg
file: src/compiler/sema.cpp
---
                *cur_prog_, node_line_, name, "__box_dyn__drop", info.type, false, {}));
---
                *cur_prog_, node_line_, shadow_user_name(name), "__box_dyn__drop", info.type, false, {},
                shadow_drop_slot(info.slot)));  // PROBE 2026-09-14p-shadowslot
===
name: shslotmg
file: src/compiler/sema.cpp
---
        lir_mirror_emit_drop(*cur_prog_, node_line_, name, dfn, info.type, df, moved_fields));
---
        lir_mirror_emit_drop(*cur_prog_, node_line_, shadow_user_name(name), dfn, info.type, df, moved_fields,
                             shadow_drop_slot(info.slot)));  // PROBE 2026-09-14p-shadowslot
===
name: shslotmg
file: include/logos/compiler/lir_mirror.hpp
---
std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields = {});
---
std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields = {}, uint32_t slot = 0xFFFFFFFFu);
===
name: shslotmg
file: src/compiler/lir_mirror.cpp
---
const uint8_t* lir_mirror_emit_drop(lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_drop_direct(line, var_name, drop_fn, ty, drop_fields, moved_fields);
---
const uint8_t* lir_mirror_emit_drop(lir::LProgram& prog, uint32_t line, std::string_view var_name, std::string_view drop_fn, TypeRef ty, bool drop_fields, const std::vector<std::string>& moved_fields, uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_drop_direct(line, var_name, drop_fn, ty, drop_fields, moved_fields, slot);
===
name: shslotmg
file: src/compiler/lir_mirror.cpp
---
                                             bool drop_fields,
                                             const std::vector<std::string>& moved_fields) {
        auto var_av = put_string(var_name);
---
                                             bool drop_fields,
                                             const std::vector<std::string>& moved_fields,
                                             uint32_t slot = 0xFFFFFFFFu) {
        auto var_av = put_string(var_name);
===
name: shslotmg
file: src/compiler/lir_mirror.cpp
---
        if (!moved_fields.empty())
            put(map_off, sk::MOVED_FIELDS, moved_av);
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_field_write_direct(uint32_t line,
---
        if (!moved_fields.empty())
            put(map_off, sk::MOVED_FIELDS, moved_av);
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));  // PROBE 2026-09-14p-shadowslot
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_field_write_direct(uint32_t line,
===
name: shslotmg
file: include/logos/compiler/lir_view.hpp
---
    uint64_t         moved_fields_count() const noexcept { return detail::stmt_array_size(self, sk::MOVED_FIELDS.code); }
    template <class F> void each_moved_field(F&& f) const noexcept {
        detail::for_each_stmt_string(self, sk::MOVED_FIELDS.code, std::forward<F>(f));
    }
};
---
    uint64_t         moved_fields_count() const noexcept { return detail::stmt_array_size(self, sk::MOVED_FIELDS.code); }
    template <class F> void each_moved_field(F&& f) const noexcept {
        detail::for_each_stmt_string(self, sk::MOVED_FIELDS.code, std::forward<F>(f));
    }
    uint32_t var_slot() const noexcept {  // PROBE 2026-09-14p-shadowslot
        auto v = detail::read_i64_opt(self, sk::VAR_SLOT.code);
        return v ? static_cast<uint32_t>(*v) : 0xFFFFFFFFu;
    }
};
===
name: shslotmg
file: src/compiler/mono_clone.cpp
---
            out_, ns.line, var_name, drop_fn, ty, drop_fields, moved_fields);
---
            out_, ns.line, var_name, drop_fn, ty, drop_fields, moved_fields, v.var_slot());  // PROBE 2026-09-14p-shadowslot
===
name: shslotmg
file: src/compiler/mlir_gen_impl.hpp
---
    // Per-function state.
    std::unordered_map<std::string, mlir::Value>  scope_;
---
    // Per-function state.
    std::unordered_map<std::string, mlir::Value>  scope_;
    // PROBE 2026-09-14p-shadowslot: the slot identity of a binding, registered where a name is bound.
    std::unordered_map<uint32_t, std::pair<std::string, mlir::Value>> shadow_slot_val_;
    llvm::DenseMap<mlir::Value, uint32_t> shadow_slot_of_val_;
    void shadow_register_slot(uint32_t s, const std::string& n) {
        if (s == 0xFFFFFFFFu) return;
        auto it = scope_.find(n);
        if (it == scope_.end() || !it->second) return;
        shadow_slot_val_[s] = {n, it->second};
        shadow_slot_of_val_[it->second] = s;
    }
    mlir::Value shadow_resolve_drop(uint32_t s, const std::string& n, mlir::Value cur);
===
name: shslotmg
file: src/compiler/mlir_gen_stmt.cpp
---
void MLIRGenImpl::gen_stmt_kind(lir_view::SDropView v) {
    std::string var_name(v.var_name());
    auto it = scope_.find(var_name);
    if (it == scope_.end()) return;
---
// PROBE 2026-09-14p-shadowslot: a drop whose NAME now denotes a different registered binding is redirected
// to the value registered for its SLOT — only when that slot was registered under the same name, in this function.
mlir::Value MLIRGenImpl::shadow_resolve_drop(uint32_t s, const std::string& n, mlir::Value cur) {
    if (s == 0xFFFFFFFFu || !cur) return cur;
    auto cs = shadow_slot_of_val_.find(cur);
    if (cs == shadow_slot_of_val_.end() || cs->second == s) return cur;
    auto sv = shadow_slot_val_.find(s);
    if (sv == shadow_slot_val_.end() || sv->second.first != n || !sv->second.second) return cur;
    auto fn_of = [](mlir::Region* r) -> mlir::Operation* {
        if (!r) return nullptr;
        auto f = r->getParentOfType<mlir::FunctionOpInterface>();
        return f ? f.getOperation() : nullptr;
    };
    mlir::Operation* here = builder_.getBlock() ? fn_of(builder_.getBlock()->getParent()) : nullptr;
    if (!here || fn_of(sv->second.second.getParentRegion()) != here) return cur;
    logos::probe::census("sdrop.name_rebound");
    if (!(logos::probe::on("shslot") || logos::probe::on("shslotmg"))) return cur;
    return sv->second.second;
}

void MLIRGenImpl::gen_stmt_kind(lir_view::SDropView v) {
    std::string var_name(v.var_name());
    auto it = scope_.find(var_name);
    if (it == scope_.end()) return;
    // PROBE 2026-09-14p-shadowslot: rebind for the duration of this drop, restore on every exit.
    struct ShadowRestore {
        std::unordered_map<std::string, mlir::Value>& m; std::string n; mlir::Value v;
        ~ShadowRestore() { m[n] = v; }
    } shadow_restore{scope_, var_name, it->second};
    it->second = shadow_resolve_drop(v.var_slot(), var_name, it->second);
===
name: shslotmg
file: src/compiler/mlir_gen_stmt.cpp
---
void MLIRGenImpl::gen_let(lir_view::SLetView v) {
    gen_let_inner(v);
---
void MLIRGenImpl::gen_let(lir_view::SLetView v) {
    gen_let_inner(v);
    shadow_register_slot(v.var_slot(), std::string(v.name()));  // PROBE 2026-09-14p-shadowslot
===
name: shslotmg
file: src/compiler/mlir_gen_fn.cpp
---
        scope_[pname] = entry->getArgument(i);
---
        scope_[pname] = entry->getArgument(i);
        shadow_register_slot(p.slot(), pname);  // PROBE 2026-09-14p-shadowslot
===
