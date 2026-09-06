name: patuple
file: src/compiler/sema_stmt.cpp
---
    bool field_old_live = false;   // → emit drop_old at the place store
    if (pc == la::FIELD_READ) {
        std::vector<std::string> segs;
        auto cur = place_node;
        while (!cur.is_null() && code_of(cur) == la::FIELD_READ &&
               cur.has_key(la::FIELD) && cur.has_key(la::RECEIVER)) {
            segs.emplace_back(str_of(cur.get(la::FIELD.code)));
            cur = map_of(cur.get(la::RECEIVER.code));
        }
        if (!cur.is_null() && code_of(cur) == la::VAR_REF) {
            std::string root(str_of(cur.get(la::NAME.code)));
            std::string path(root);
            for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
                path.push_back('.');
                path += *it;
            }
            // Old value is live (droppable + present) iff we have EXCLUSIVE
            // write access to its place: an OWNED value local, OR a `&mut`
            // referent. Writing `(*self).f = new` through a unique borrow
            // overwrites a LIVE field — the owner drops the NEW value at its
            // scope end, never the old one, so the old must be dropped HERE
            // (without this, `self.f = x` leaked f's old value). A `&mut`
            // referent is always fully initialised and cannot have a moved-out
            // field (you cannot move out of a borrow), so its field is live.
            // A shared `&` is not assignable; a raw `*mut`/`*const` stays
            // MANUAL (no implicit drop — writing into uninit memory is the
            // whole point of a raw pointer). Also require the path/ancestors
            // not moved out (a moved-out value is already gone). A moved
            // DESCENDANT (`path.x`) still leaves siblings live — but the
            // whole-field memcpy store overwrites them, so the broad overlap
            // check stays conservative-correct: skip drop_old when any overlap
            // exists, lift-then-rely on the move bookkeeping.
            TypeRef root_ty = lookup(root);
            bool root_owned = root_ty &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ref &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ptr;
            bool any_overlap = false;
            std::string pre = path + ".";
            std::string anc = root;  // ancestors: root, root.a, … up to path
            // Build ancestor-prefix set and check moved membership/overlap.
            for (auto& mv : moved_vars_) {
                bool eq_or_under = mv == path ||
                    (mv.size() > pre.size() && mv.compare(0, pre.size(), pre) == 0);
                bool ancestor = path.size() > mv.size() + 1 &&
                    path.compare(0, mv.size(), mv) == 0 && path[mv.size()] == '.';
                if (eq_or_under || ancestor) { any_overlap = true; break; }
            }
            field_old_live = root_owned && !decl_uninit_vars_.count(root) &&
                             !any_overlap;
---
    bool field_old_live = false;   // → emit drop_old at the place store
    // PROBE 2026-09-06m placedrop — three arms + an unconditional site census.
    const bool pa_tup_  = logos::probe::on("patuple");
    const bool pa_idx_  = false;
    const bool pa_root_ = false;
    const bool pa_any_  = pa_tup_ || pa_idx_ || pa_root_;
    logos::probe::census(pc == la::FIELD_READ  ? "pasgn.kind.field"
                       : pc == la::TUPLE_INDEX ? "pasgn.kind.tuple"
                       : pc == la::INDEX_READ  ? "pasgn.kind.index"
                                               : "pasgn.kind.deref");
    if (pc == la::FIELD_READ || (pa_tup_ && pc == la::TUPLE_INDEX) ||
        (pa_idx_ && pc == la::INDEX_READ)) {
        std::vector<std::string> segs;
        auto cur = place_node;
        bool idx_seen_ = false;
        for (;;) {
            if (cur.is_null()) break;
            const int32_t cc_ = code_of(cur);
            if ((cc_ == la::FIELD_READ || (pa_tup_ && cc_ == la::TUPLE_INDEX)) &&
                cur.has_key(la::FIELD) && cur.has_key(la::RECEIVER)) {
                // move_path_of (sema_impl.hpp) spells a tuple element `t.0` and
                // already walks MIXED field/tuple chains, so a TUPLE_INDEX
                // segment must normalise to the same decimal text or the
                // overlap check below compares against a path nothing records.
                std::string seg_(str_of(cur.get(la::FIELD.code)));
                if (cc_ == la::TUPLE_INDEX)
                    seg_ = std::to_string((uint64_t)parse_int_literal(
                               str_of(cur.get(la::FIELD.code))));
                segs.emplace_back(std::move(seg_));
                auto nx_ = map_of(cur.get(la::RECEIVER.code));
                cur = pa_any_ ? unwrap_paren_node(nx_) : nx_;
                continue;
            }
            if (pa_idx_ && cc_ == la::INDEX_READ && cur.has_key(la::RECEIVER)) {
                // A subscript has no static path. Fall back to the CONTAINER
                // and let the overlap check answer for the whole root — the
                // move side already REFUSES moving a Drop-bearing element out
                // of a fixed-size array by index, so the moved-element case
                // this coarsening cannot see is unreachable, not unhandled.
                idx_seen_ = true; segs.clear();
                cur = unwrap_paren_node(map_of(cur.get(la::RECEIVER.code)));
                continue;
            }
            break;
        }
        (void)idx_seen_;
        if (pa_any_) cur = unwrap_paren_node(cur);
        if (pa_root_ && !cur.is_null() && code_of(cur) == la::DEREF &&
            cur.has_key(la::VALUE)) {
            // ROOT AXIS: `(*q).d = new`, q: `&mut W`. The rule is already
            // WRITTEN twenty lines below — a `&mut` referent is always fully
            // initialised and cannot have a moved-out field — and the SUGARED
            // spelling `q.d = new` already gets it, because its place node is
            // FIELD_READ over a VAR_REF. Only the desugared spelling is out.
            TypeRef dt_ = resolve_place_type(map_of(cur.get(la::VALUE.code)));
            if (dt_ && TypeRef(dt_).kind() == LogosType::Kind::MutRef) {
                field_old_live = true;
                logos::probe::census("pasgn.live.root_deref");
            }
        }
        logos::probe::census(cur.is_null()                  ? "pasgn.root.null"
                           : code_of(cur) == la::VAR_REF    ? "pasgn.root.var"
                           : code_of(cur) == la::DEREF      ? "pasgn.root.deref"
                                                            : "pasgn.root.other");
        if (!cur.is_null() && code_of(cur) == la::VAR_REF) {
            std::string root(str_of(cur.get(la::NAME.code)));
            std::string path(root);
            for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
                path.push_back('.');
                path += *it;
            }
            // Old value is live (droppable + present) iff we have EXCLUSIVE
            // write access to its place: an OWNED value local, OR a `&mut`
            // referent. Writing `(*self).f = new` through a unique borrow
            // overwrites a LIVE field — the owner drops the NEW value at its
            // scope end, never the old one, so the old must be dropped HERE
            // (without this, `self.f = x` leaked f's old value). A `&mut`
            // referent is always fully initialised and cannot have a moved-out
            // field (you cannot move out of a borrow), so its field is live.
            // A shared `&` is not assignable; a raw `*mut`/`*const` stays
            // MANUAL (no implicit drop — writing into uninit memory is the
            // whole point of a raw pointer). Also require the path/ancestors
            // not moved out (a moved-out value is already gone). A moved
            // DESCENDANT (`path.x`) still leaves siblings live — but the
            // whole-field memcpy store overwrites them, so the broad overlap
            // check stays conservative-correct: skip drop_old when any overlap
            // exists, lift-then-rely on the move bookkeeping.
            TypeRef root_ty = lookup(root);
            bool root_owned = root_ty &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ref &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ptr;
            bool any_overlap = false;
            std::string pre = path + ".";
            std::string anc = root;  // ancestors: root, root.a, … up to path
            // Build ancestor-prefix set and check moved membership/overlap.
            for (auto& mv : moved_vars_) {
                bool eq_or_under = mv == path ||
                    (mv.size() > pre.size() && mv.compare(0, pre.size(), pre) == 0);
                bool ancestor = path.size() > mv.size() + 1 &&
                    path.compare(0, mv.size(), mv) == 0 && path[mv.size()] == '.';
                if (eq_or_under || ancestor) { any_overlap = true; break; }
            }
            field_old_live = root_owned && !decl_uninit_vars_.count(root) &&
                             !any_overlap;
            logos::probe::census(field_old_live ? "pasgn.live.var.yes"
                                                : "pasgn.live.var.no");
            logos::probe::census(std::string("pasgn.live.var.") +
                (pc == la::FIELD_READ ? "field" : pc == la::TUPLE_INDEX ? "tuple" : "index") +
                (field_old_live ? ".yes" : ".no"));
===
name: paindex
file: src/compiler/sema_stmt.cpp
---
    const bool pa_idx_  = false;
---
    const bool pa_idx_  = logos::probe::on("paindex");
===
name: paroot
file: src/compiler/sema_stmt.cpp
---
    const bool pa_root_ = false;
---
    const bool pa_root_ = logos::probe::on("paroot");
===
name: paall
file: src/compiler/sema_stmt.cpp
---
    const bool pa_tup_  = logos::probe::on("patuple");
    const bool pa_idx_  = logos::probe::on("paindex");
    const bool pa_root_ = logos::probe::on("paroot");
---
    const bool pa_tup_  = logos::probe::on("patuple") || logos::probe::on("paall");
    const bool pa_idx_  = logos::probe::on("paindex") || logos::probe::on("paall");
    const bool pa_root_ = logos::probe::on("paroot")  || logos::probe::on("paall");
===
