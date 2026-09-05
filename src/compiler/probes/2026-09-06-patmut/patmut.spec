name: patmutbit
file: src/compiler/sema_stmt.cpp
---
static bool pat_byval_mut(TinyMapView n) {
---
// PROBE patmutbit / patmutwhole (PROBES.md 2026-09-06): carry the by-value
// pattern `mut` into the LIR for match/if-let/while-let/for-each bindings.
static bool patmut_carry() {
    return logos::probe::on("patmutbit") || logos::probe::on("patmutwhole");
}
static bool pat_byval_mut(TinyMapView n) {
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
binding_is_mut.push_back(is_ref && is_mut);
---
binding_is_mut.push_back(is_mut);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
            bind_ref_modes[k] = default_mut ? 4u : 3u;
        }
    }
---
            bind_ref_modes[k] = default_mut ? 4u : 3u;
        } else if (patmut_carry() && k < binding_is_mut.size() && binding_is_mut[k]) {
            bind_ref_modes[k] = 0x10u;  // by value, written `mut` (PROBE patmutbit)
        }
    }
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
                define(std::string(names[i]), types[i], false,
                       i < _vd_slots.size() ? _vd_slots[i] : 0xFFFFFFFFu);
---
        auto _vd_muts = v.bind_byval_muts();  // PROBE patmutbit: the carried by-value `mut`
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
                define(std::string(names[i]), types[i],
                       i < _vd_muts.size() && _vd_muts[i] != 0u,
                       i < _vd_slots.size() ? _vd_slots[i] : 0xFFFFFFFFu);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
    p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, wname);
---
    p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, wname, 0xFFFFFFFFu,
        patmut_carry() && wname != "_" && pat_byval_mut(pnode));  // PROBE patmutbit
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
    std::string var_name;
    if (node.has_key(la::PAT)) {
---
    std::string var_name;
    bool for_var_mut = false;  // PROBE patmutbit: `for mut x in …`
    if (node.has_key(la::PAT)) {
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
            var_name = std::string(str_of(p.get(la::NAME.code)));  // bare binding
---
            var_name = std::string(str_of(p.get(la::NAME.code)));  // bare binding
            for_var_mut = patmut_carry() && pat_byval_mut(p);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
        define(var_name, elem_type, false);
---
        define(var_name, elem_type, for_var_mut);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
define(var_name, make_ref(for_mut_ref, elem_type), for_mut_ref);
---
define(var_name, make_ref(for_mut_ref, elem_type), for_mut_ref || for_var_mut);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
define(var_name, make_ref(false, elem_t), false);  // yields &T
---
define(var_name, make_ref(false, elem_t), for_var_mut);  // yields &T
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
define(std::string(var_name), elem_type, false);
---
define(std::string(var_name), elem_type, for_var_mut);
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
            some_pat.bindings, some_pat.binding_types);
---
            some_pat.bindings, some_pat.binding_types, {},
            std::vector<uint32_t>{for_var_mut ? 0x10u : 0u});
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
        sfe.arr_size  = arr_size;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
---
        sfe.arr_size  = arr_size;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
        sfe.var_mut   = for_var_mut;
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
        sfe.arr_size  = 0;
        sfe.is_slice  = true;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
---
        sfe.arr_size  = 0;
        sfe.is_slice  = true;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
        sfe.var_mut   = for_var_mut;
===
name: patmutbit
file: src/compiler/sema_stmt.cpp
---
            sfe.arr_size  = 0;
            sfe.is_slice  = true;
            sfe.body      = lir_mirror_block(*cur_prog_, body);
            sfe.slot      = _fe_slot;
---
            sfe.arr_size  = 0;
            sfe.is_slice  = true;
            sfe.body      = lir_mirror_block(*cur_prog_, body);
            sfe.slot      = _fe_slot;
            sfe.var_mut   = for_var_mut;
===
name: patmutbit
file: include/logos/compiler/lir.hpp
---
    bool             is_slice = false;  // true → iter is &[T] (dynamic length from fat pointer)
---
    bool             is_slice = false;  // true → iter is &[T] (dynamic length from fat pointer)
    bool             var_mut  = false;  // `for mut x in …` (sk::IS_MUT, sparse)
===
name: patmutbit
file: src/compiler/sema_impl.hpp
---
k.arr_size, k.is_slice, k.body, k.slot);
---
k.arr_size, k.is_slice, k.body, k.slot, k.var_mut);
===
name: patmutbit
file: include/logos/compiler/lir_mirror.hpp
---
lir_view::BlockRef body, uint32_t slot = 0xFFFFFFFFu);
---
lir_view::BlockRef body, uint32_t slot = 0xFFFFFFFFu, bool var_mut = false);
===
name: patmutbit
file: include/logos/compiler/lir_mirror.hpp
---
const uint8_t* lir_mirror_emit_pat_wild         (lir::LProgram& prog, std::string_view name, uint32_t slot = 0xFFFFFFFFu);
---
const uint8_t* lir_mirror_emit_pat_wild         (lir::LProgram& prog, std::string_view name, uint32_t slot = 0xFFFFFFFFu, bool is_mut = false);
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
    const uint8_t* emit_pat_wild_direct(std::string_view name,
                                                uint32_t slot = 0xFFFFFFFFu) {
        auto name_av = name.empty() ? writ::AnyVal{} : put_string(name);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Wild));
        put(map_off, pk::NAME, name_av);
---
    const uint8_t* emit_pat_wild_direct(std::string_view name,
                                                uint32_t slot = 0xFFFFFFFFu,
                                                bool is_mut = false) {
        auto name_av = name.empty() ? writ::AnyVal{} : put_string(name);
        auto map_off = make_map(writ::schema::lir_pat(lir_schema::pat::Code::Wild));
        put(map_off, pk::NAME, name_av);
        if (is_mut) put(map_off, pk::IS_MUT, put_bool(true));  // sparse: by-value `mut x`
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
    return em.emit_pat_wild_direct(name, slot);
---
    return em.emit_pat_wild_direct(name, slot, is_mut);
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
const uint8_t* lir_mirror_emit_pat_wild(lir::LProgram& prog, std::string_view name, uint32_t slot) {
---
const uint8_t* lir_mirror_emit_pat_wild(lir::LProgram& prog, std::string_view name, uint32_t slot, bool is_mut) {
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
                                                 uint32_t slot = 0xFFFFFFFFu) {
        auto var_av  = put_string(var);
---
                                                 uint32_t slot = 0xFFFFFFFFu,
                                                 bool var_mut = false) {
        auto var_av  = put_string(var);
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_write_direct(
---
        if (slot != 0xFFFFFFFFu) put(map_off, sk::VAR_SLOT, put_i64((int64_t)slot));
        if (var_mut) put(map_off, sk::IS_MUT, put_bool(true));  // sparse: `for mut x`
        put_line(map_off, line);
        return map_off;
    }
    const uint8_t* emit_deref_write_direct(
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
const uint8_t* lir_mirror_emit_for_each(lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef iter, TypeRef elem_type, int64_t arr_size, bool is_slice, lir_view::BlockRef body, uint32_t slot) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_for_each_direct(line, var, iter, elem_type, arr_size, is_slice, body, slot);
---
const uint8_t* lir_mirror_emit_for_each(lir::LProgram& prog, uint32_t line, std::string_view var, lir_view::ExprRef iter, TypeRef elem_type, int64_t arr_size, bool is_slice, lir_view::BlockRef body, uint32_t slot, bool var_mut) {
    auto& ctr = prog.type_pool.ctr_or_init();
    LirMirrorEmitter em(ctr, *prog.mirror_table, prog.type_pool);
    return em.emit_for_each_direct(line, var, iter, elem_type, arr_size, is_slice, body, slot, var_mut);
===
name: patmutbit
file: include/logos/compiler/lir_view.hpp
---
    std::vector<uint32_t> bind_ref_modes() const noexcept {
        std::vector<uint32_t> out;
        auto av = self.mirror()->get(pk::BINDING_REF_MODES.code);
---
    std::vector<uint32_t> bind_ref_modes() const noexcept {
        auto out = bind_ref_modes_raw();
        for (auto& m : out) m &= 0xFu;
        return out;
    }
    // Bit 0x10 of a mode: the BY-VALUE binding was written `mut` (PROBES.md 2026-09-06).
    std::vector<uint32_t> bind_byval_muts() const noexcept {
        auto out = bind_ref_modes_raw();
        for (auto& m : out) m = (m >> 4) & 1u;
        return out;
    }
    std::vector<uint32_t> bind_ref_modes_raw() const noexcept {
        std::vector<uint32_t> out;
        auto av = self.mirror()->get(pk::BINDING_REF_MODES.code);
===
name: patmutbit
file: include/logos/compiler/lir_view.hpp
---
struct PatWildView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
---
struct PatWildView {
    PatRef self;
    std::string_view name() const noexcept { return detail::read_string(self, pk::NAME.code); }
    bool is_mut() const noexcept { return detail::read_bool(self, pk::IS_MUT.code); }  // by-value `mut x` (sparse)
===
name: patmutbit
file: include/logos/compiler/lir_view.hpp
---
    bool             is_slice() const noexcept { return detail::read_bool(self, sk::IS_SLICE.code); }
---
    bool             is_slice() const noexcept { return detail::read_bool(self, sk::IS_SLICE.code); }
    bool             var_mut() const noexcept { return detail::read_bool(self, sk::IS_MUT.code); }  // `for mut x` (sparse)
===
name: patmutbit
file: src/compiler/mono_clone.cpp
---
p.mirror_ptr_ = lir_mirror_emit_pat_wild(*prog_, name, wv.bind_slot());  // Phase-1
---
p.mirror_ptr_ = lir_mirror_emit_pat_wild(*prog_, name, wv.bind_slot(), wv.is_mut());  // Phase-1
===
name: patmutbit
file: src/compiler/mono_clone.cpp
---
            v.bind_ref_modes());   // and the binding modes
---
            v.bind_ref_modes_raw());   // and the binding modes, `mut` bit included
===
name: patmutbit
file: src/compiler/mono_clone.cpp
---
out_, ns.line, var, iter, elem_type, arr_size, is_slice, body, v.var_slot());  // Phase-1
---
out_, ns.line, var, iter, elem_type, arr_size, is_slice, body, v.var_slot(), v.var_mut());  // Phase-1
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
                auto slots = v.bind_slots();  // Phase-1
                size_t i = 0;
                v.each_binding([&](std::string_view b) {
                    declare_var(std::string(b), i < slots.size() ? slots[i] : NO_SLOT);
                    ++i;
                });
---
                auto slots = v.bind_slots();  // Phase-1
                auto muts  = v.bind_byval_muts();  // PROBE patmutbit: the carried `mut`
                size_t i = 0;
                v.each_binding([&](std::string_view b) {
                    const uint32_t sl_ = i < slots.size() ? slots[i] : NO_SLOT;
                    declare_var(std::string(b), sl_);
                    if ((i < muts.size() && muts[i] != 0u) || logos::probe::on("patmutany"))
                        var_at(sl_, b).is_mut_binding = true;
                    ++i;
                });
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
                if (!n.empty() && n != "_") declare_var(n, wv.bind_slot());  // Phase-1
---
                if (!n.empty() && n != "_") {
                    declare_var(n, wv.bind_slot());  // Phase-1
                    if (wv.is_mut() || logos::probe::on("patmutany"))  // PROBE patmutbit
                        var_at(wv.bind_slot(), n).is_mut_binding = true;
                }
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
                         std::string_view break_slot = {}) {
        auto seed_loop_var_loans = [&]() {
---
                         std::string_view break_slot = {},
                         bool loop_var_mut = false) {  // PROBE patmutbit: `for mut x`
        auto seed_loop_var_loans = [&]() {
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
        suppress_reports_ = true;
        push_scope();
        declaring_pattern_ = true;
        for (auto& v : loop_vars) declare_var(v);
---
        suppress_reports_ = true;
        push_scope();
        declaring_pattern_ = true;
        for (auto& v : loop_vars) {
            declare_var(v);
            if (loop_var_mut || logos::probe::on("patmutany")) var_at(NO_SLOT, v).is_mut_binding = true;
        }
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
        cur_diverged_ = false;
        push_scope();
        declaring_pattern_ = true;
        for (auto& v : loop_vars) declare_var(v);
---
        cur_diverged_ = false;
        push_scope();
        declaring_pattern_ = true;
        for (auto& v : loop_vars) {
            declare_var(v);
            if (loop_var_mut || logos::probe::on("patmutany")) var_at(NO_SLOT, v).is_mut_binding = true;
        }
===
name: patmutbit
file: src/compiler/borrow_check.cpp
---
visit_loop_body(b, {std::string(v.var())});
---
visit_loop_body(b, {std::string(v.var())}, {}, {}, {}, v.var_mut());
===
name: patmutwhole
file: src/compiler/borrow_check.cpp
---
pat_root_ = pst_->pat_bound;
---
pat_root_ = pst_->pat_bound && !logos::probe::on("patmutwhole");  // PROBE: the mask retires with the bit
===
name: patmutany
file: src/compiler/borrow_check.cpp
---
if ((tst_ && tst_->pat_bound) ||
---
if ((tst_ && tst_->pat_bound && !logos::probe::on("patmutwhole")) ||

===
name: patmutwhole
file: src/compiler/sema_stmt.cpp
---
            define(bind, ftype);
            auto sref = builder().var_ref(nsub.synth_name, synth_t);
            auto fr = builder().field_read(std::move(sref), fname, ftype);
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = false;
---
            // PROBE patmutbit: the nested-sub prologue is the 7th `let` site
            const bool bmut_ = patmut_carry() &&
                (pat_byval_mut(fnode) ||
                 (fnode.has_key(la::VALUE) && pat_byval_mut(map_of(fnode.get(la::VALUE.code)))));
            define(bind, ftype, bmut_);
            auto sref = builder().var_ref(nsub.synth_name, synth_t);
            auto fr = builder().field_read(std::move(sref), fname, ftype);
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
===
name: patmutwhole
file: src/compiler/sema_stmt.cpp
---
                        define(nm, et);
                        // Binding moves the element OUT of stmp — mark stmp.<i>
                        // moved so stmp's scope-exit Drop skips it (else double).
                        if (is_move_type(et)) mark_moved_expr(expr_ref_of(elem_expr));
                        lir::SLet el; el.name = nm; el.type = et;
                        el.is_mut = false; el.value = std::move(elem_expr);
---
                        const bool emut_ = patmut_carry() && pat_byval_mut(en);  // PROBE patmutbit
                        define(nm, et, emut_);
                        // Binding moves the element OUT of stmp — mark stmp.<i>
                        // moved so stmp's scope-exit Drop skips it (else double).
                        if (is_move_type(et)) mark_moved_expr(expr_ref_of(elem_expr));
                        lir::SLet el; el.name = nm; el.type = et;
                        el.is_mut = emut_; el.value = std::move(elem_expr);
===
name: patmutwhole
file: src/compiler/sema_stmt.cpp
---
            define(nm, et);
            lir::SLet s; s.name = nm; s.type = et; s.is_mut = false;
            s.value = std::move(elem_expr);
---
            const bool fmut_ = patmut_carry() && pat_byval_mut(en);  // PROBE patmutbit: `for (mut a, b)`
            define(nm, et, fmut_);
            lir::SLet s; s.name = nm; s.type = et; s.is_mut = fmut_;
            s.value = std::move(elem_expr);
===
name: patmutbit
file: src/compiler/lir_mirror.cpp
---
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::ForEach));
---
        auto map_off = make_map(writ::schema::lir_stmt(lir_schema::stmt::Code::ForEach), 9);  // + sk::IS_MUT
