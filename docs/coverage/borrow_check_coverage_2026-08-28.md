# borrow_check.cpp — region execution map

| | |
|---|---|
| taken | 2026-08-28 |
| subject | `/home/logos/devel/logos/.claude/worktrees/wf_7d324b0a-3fe-1/src/compiler/borrow_check.cpp` |
| binary | logosc-cov — src/compiler/borrow_check.cpp instrumented, every other TU as built (RelWithDebInfo, -O2) |
| population | ctest corpus (8056 run_test.sh fixtures: tests/logos + tests/imported, pass and fail) + the four stdlib layers (lang, mem, lcm, std) |
| compiler runs | 8060 |
| reproduce | `scripts/coverage-map.sh` |

## ⚠ A ZERO IN COVERAGE IS NOT A DEFECT

A region with count 0 is exactly one fact: **nothing in the population
above executed it**. That is three different situations and they take
three different responses:

1. **dead code** — no input can reach it. Delete it.
2. **an unexercised case** — reachable, but nothing we test reaches it.
   That is a CORPUS gap, and the response is a fixture.
3. **structurally unreachable** — a guard whose condition cannot hold
   given its callers' invariants. The response is to say so in the
   code, or to remove the guard.

This map cannot tell them apart and does not try. Reading a zero as a
defect would repeat the error this arc is about — the census's 28 915
"arrivals" were real and meant something other than what was read off
them. Equally: a zero is not permission to delete.

**And coverage is not a probe.** Coverage says which code executed and
how often. A probe says what CHANGES if the code behaves differently,
which coverage cannot answer at all. This map says where a probe would
have a population behind it; it never says what the probe would find.

## Totals

| | count |
|---|---:|
| code regions in the TU | 5726 |
| never executed (count 0) | 575 (10.0%) |
| near-dead (1..9) | 258 |
| functions with regions here | 199 |
| functions never entered | 4 |

Every region is in `borrow_check_regions_2026-08-28.csv`, one row each, sorted by line and column.

⚠ Counts are summed ACROSS INSTANTIATIONS: a template or a lambda has
one entry per instantiation in the raw profile, and the question here is
about the SOURCE. So the function count above is smaller than the one
`llvm-cov report` prints, which counts instantiations. Two regions can
share a line and differ only in column — a sub-expression of an `&&`
chain is its own region — so line:col is the identity, not line.

## A. Functions never entered (4)

No region in these executed. A probe placed in any of them reports
ceiling 0 for the reason that has nothing to do with the hypothesis.

| first line | regions | function |
|---:|---:|---|
| 589 | 1 | `logos::compiler::VarStore::clear` |
| 1539 | 1 | `logos::compiler::RefGraph::erase` |
| 4726 | 1 | `logos::compiler::BorrowChecker::expr_ref` |
| 4730 | 1 | `logos::compiler::BorrowChecker::pat_ref` |

## B. Never-executed regions inside functions that DO run (571)

The interesting half: the function is live, this branch of it is not.
Grouped by enclosing function, ranked by how much of the function is
cold. Full list in the CSV; here, every function with at least one.

### `logos::compiler::BorrowChecker::visit_stmt` — 56 of 422 regions cold (hottest 43504906)

| line:col | what it is | source |
|---:|---|---|
| 10550:18 | if guard | `if (!sr) return;` |
| 10554:64 | if guard | `if (uint64_t pt = stmt_point(sr); pt > max_line_seen_) max_line_seen_ = pt;` |
| 10565:13 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10565:14 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10565:47 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10566:13 | statement | `fprintf(stderr, "[bc-placewrite-door] fn=%s ln=%u kind=%d\n",` |
| 10566:21 | statement | `fprintf(stderr, "[bc-placewrite-door] fn=%s ln=%u kind=%d\n",` |
| 10689:29 | statement | `fprintf(stderr, "[#86trace-let] fn=%s line=%u var=%s "` |
| 10689:37 | statement | `fprintf(stderr, "[#86trace-let] fn=%s line=%u var=%s "` |
| 10795:70 | statement | `note_holder_escape_prov(name, val ? val.type(pool) : TypeRef(nullptr),` |
| 10797:60 | statement | `note_reborrow(name, val ? val.type(pool) : TypeRef(nullptr), val);  // H1` |
| 10854:13 | switch arm | `case Code::FieldWrite: {` |
| 10865:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10865:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10865:41 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10865:60 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10866:63 | if guard | `if (auto it = var_find(NO_SLOT, recv_nm); it != nullptr)` |
| 10869:25 | statement | `erase_reinit(it->moved_fields, field_nm);` |
| 10890:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10890:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10890:41 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10891:21 | statement | `note_reborrow_place(recv_nm + "." + field_nm, v.value());` |
| 10904:13 | switch arm | `case Code::IndexWrite: {` |
| 10907:54 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10907:69 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10908:25 | if guard | `if (it->shared_borrows > 0)` |
| 10909:25 | diagnostic emission | `report(ln, std::format(` |
| 10912:25 | if guard | `if (it->mut_borrowed)` |
| 10913:25 | diagnostic emission | `report(ln, std::format(` |
| 10929:13 | switch arm | `case Code::FieldIndexWrite: {` |
| 10932:54 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10932:69 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10933:25 | if guard | `if (it->shared_borrows > 0)` |
| 10934:25 | diagnostic emission | `report(ln, std::format(` |
| 10937:25 | if guard | `if (it->mut_borrowed)` |
| 10938:25 | diagnostic emission | `report(ln, std::format(` |
| 10952:13 | switch arm | `case Code::ChainFieldWrite: {` |
| 10959:59 | call / statement | `v.each_extra([&](std::string_view ex) {` |
| 10960:29 | if guard | `if (!ex.empty()) cf_path += "." + std::string(ex);` |
| 10960:42 | if guard | `if (!ex.empty()) cf_path += "." + std::string(ex);` |
| 10962:25 | if guard | `if (!v.field().empty())` |
| 10963:25 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10963:37 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10963:55 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10963:60 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10972:13 | switch arm | `case Code::DerefFieldWrite: {` |
| 11144:50 | if guard | `if (eroot86.empty()) eroot86 = root;` |
| 11190:73 | statement | `TypeRef pt = atv.inner() ? atv.inner().type(pool) : TypeRef(nullptr);` |
| 11241:31 | statement | `? std::string(EVarRefView{mrecv}.name())` |
| 11245:29 | statement | `cr = ref_place_root(cr);` |
| 11304:68 | statement | `v.ptr() ? v.ptr().type(pool) : TypeRef{},` |
| 11312:13 | switch arm | `case Code::TupleWrite: {` |
| 11352:25 | statement | `roots.push_back(std::move(st));` |
| 11359:24 | else branch | `} else {` |
| 11582:88 | statement | `acc_rbl[k] = ref_borrow_line_.count(k) ? ref_borrow_line_[k] : ln;` |
| 11679:13 | switch arm | `default:` |

### `logos::compiler::BorrowChecker::take_ref_borrows` — 49 of 361 regions cold (hottest 7085699)

| line:col | what it is | source |
|---:|---|---|
| 7898:33 | if guard | `if (!a) return;` |
| 7902:40 | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 8286:30 | if guard | `if (blind && !tied_recv && !holder.empty() &&` |
| 8286:44 | if guard | `if (blind && !tied_recv && !holder.empty() &&` |
| 8287:21 | statement | `fn_index_.by_name.find(std::string(v.callee())) ==` |
| 8288:50 | call / statement | `fn_index_.by_name.end()) {` |
| 8290:46 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8290:52 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8290:63 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8291:25 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8291:25 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8291:31 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8291:62 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8294:39 | statement | `bool rawptr = bp.root_type &&` |
| 8295:29 | statement | `bp.root_type.kind() == LogosType::Kind::Ptr;` |
| 8296:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8296:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8296:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8296:49 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8297:29 | call / statement | `var_has(bp.root_slot, bp.root))` |
| 8298:29 | statement | `record_borrow(bp, /*is_mut=*/false, line, holder,` |
| 8314:29 | if guard | `if (!a) return;` |
| 8363:66 | if guard | `if (logos::probe::on("genautorefx")) {` |
| 8364:47 | statement | `bool rawptr = pbp.root_type &&` |
| 8365:37 | statement | `pbp.root_type.kind() == LogosType::Kind::Ptr;` |
| 8366:37 | if guard | `if (!rawptr)` |
| 8367:37 | statement | `record_borrow(pbp,` |
| 8427:61 | if guard | `if (logos::probe::on("mcallrefrecv") && recv &&` |
| 8428:25 | call / statement | `is_ref_kind(recv.type(pool)))` |
| 8429:25 | statement | `take_ref_borrows(recv, line, holder, record_only);` |
| 8482:29 | statement | `rn = rn.substr(d + 1);` |
| 8484:29 | statement | `rn = rn.substr(0, g);` |
| 8515:24 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8515:28 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8515:28 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8515:36 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8515:66 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8521:29 | if guard | `if (!a) return;` |
| 8591:25 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8591:29 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8591:43 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8702:29 | statement | `consume(std::string(cap), line);` |
| 8723:53 | if guard | `if (logos::probe::on("capmut")) is_mut = true;` |
| 8725:25 | statement | `std::fprintf(stderr,` |
| 8725:38 | statement | `std::fprintf(stderr,` |
| 8748:58 | if guard | `if (logos::probe::on("capshared") && shared_whole)` |
| 8749:25 | statement | `shared_whole = false;   // fall to record_borrow` |
| 8765:64 | statement | `: TypeRef(nullptr);` |
| 8780:45 | statement | `? std::string() : holder);` |

### `borrow_check.cpp:logos::compiler::extract_borrow_place` — 36 of 122 regions cold (hottest 24492131)

| line:col | what it is | source |
|---:|---|---|
| 1028:49 | if guard | `if (logos::probe::on("sharedsticky") && bp.through_ref_type &&` |
| 1029:13 | statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) return;` |
| 1029:65 | statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) return;` |
| 1086:63 | call / statement | `sl.type(pool).kind() == LogosType::Kind::Ptr) {` |
| 1093:21 | if guard | `if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }` |
| 1093:52 | if guard | `if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }` |
| 1148:51 | if guard | `if (logos::probe::on("ptrderef")) {` |
| 1156:20 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1156:21 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1156:21 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1157:21 | statement | `cur.kind() == Code::Call \|\|` |
| 1158:21 | call / statement | `cur.kind() == Code::AddrOfTemp)) {` |
| 1158:54 | call / statement | `cur.kind() == Code::AddrOfTemp)) {` |
| 1165:17 | if guard | `if (cur.kind() == Code::AddrOfTemp)` |
| 1166:17 | statement | `nxt = EAddrOfTempView{cur}.inner();` |
| 1167:18 | else-if guard | `else if (cur.kind() == Code::MethodCall)` |
| 1167:22 | else-if guard | `else if (cur.kind() == Code::MethodCall)` |
| 1168:17 | statement | `nxt = EMethodCallView{cur}.receiver();` |
| 1170:17 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1170:56 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1170:62 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1170:68 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1171:20 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1171:20 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1171:27 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1171:28 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1172:28 | call / statement | `nxt.kind() == Code::SliceLit)) {` |
| 1172:59 | call / statement | `nxt.kind() == Code::SliceLit)) {` |
| 1173:23 | statement | `nxt = nxt.kind() == Code::AddrOfTemp` |
| 1174:27 | statement | `? EAddrOfTempView{nxt}.inner()` |
| 1175:27 | statement | `: ESliceLitView{nxt}.base();` |
| 1177:17 | if guard | `if (!nxt) break;` |
| 1177:23 | if guard | `if (!nxt) break;` |
| 1178:13 | statement | `path_parts.clear();` |
| 1194:45 | if guard | `if (logos::probe::on("refwhole") && bp.through_ref) path_parts.clear();` |
| 1194:61 | if guard | `if (logos::probe::on("refwhole") && bp.through_ref) path_parts.clear();` |

### `logos::compiler::borrow_check` — 35 of 46 regions cold (hottest 38403212)

| line:col | what it is | source |
|---:|---|---|
| 12784:13 | statement | `fprintf(stderr, "[flow-iters] fns=%zu rounds=%u max_body_passes=%u\n",` |
| 12784:21 | statement | `fprintf(stderr, "[flow-iters] fns=%zu rounds=%u max_body_passes=%u\n",` |
| 12786:63 | if guard | `if (const char* df = std::getenv("LOGOS_DUMP_FLOWS")) {` |
| 12788:41 | loop body | `for (auto& [nm, s] : flows) {` |
| 12789:21 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12789:21 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12789:36 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12789:72 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12790:17 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12790:21 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12790:35 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12790:45 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12791:17 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12791:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12791:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12791:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12791:36 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12791:52 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12793:43 | early return | `return true; }()) continue;` |
| 12794:17 | statement | `fprintf(stderr, "[flow] %s: result<-%#llx", nm.c_str(),` |
| 12794:25 | statement | `fprintf(stderr, "[flow] %s: result<-%#llx", nm.c_str(),` |
| 12796:38 | loop body | `for (uint32_t j = 0; j < s.nparams; ++j)` |
| 12796:53 | loop body | `for (uint32_t j = 0; j < s.nparams; ++j)` |
| 12797:21 | if guard | `if (s.to_outparam[j])` |
| 12797:25 | if guard | `if (s.to_outparam[j])` |
| 12798:25 | statement | `fprintf(stderr, " out%u<-%#llx", j,` |
| 12798:33 | statement | `fprintf(stderr, " out%u<-%#llx", j,` |
| 12800:25 | statement | `fprintf(stderr, "%s  (rounds=%u)\n",` |
| 12801:25 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12801:41 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12801:52 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12893:9 | statement | `fprintf(stderr, "[bc-thruref] fired=%llu\n",` |
| 12893:17 | statement | `fprintf(stderr, "[bc-thruref] fired=%llu\n",` |
| 12898:9 | statement | `fprintf(stderr,` |
| 12898:17 | statement | `fprintf(stderr,` |

### `logos::compiler::BorrowChecker::place_write_loans` — 29 of 71 regions cold (hottest 18992)

| line:col | what it is | source |
|---:|---|---|
| 10461:35 | if guard | `if (root.empty() \|\| !val) return;` |
| 10475:39 | statement | `size_t nb = scopes_.empty() ? 0 : scopes_.back().borrows.size();` |
| 10476:39 | statement | `size_t nf = scopes_.empty() ? 0 : scopes_.back().field_borrows.size();` |
| 10491:17 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10491:68 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10491:83 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10492:25 | if guard | `if (br.is_mut) it->mut_borrowed = false;` |
| 10492:36 | if guard | `if (br.is_mut) it->mut_borrowed = false;` |
| 10493:26 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10493:30 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10493:54 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10497:62 | loop body | `for (size_t i = fr.field_borrows.size(); i > nf; --i) {` |
| 10497:67 | loop body | `for (size_t i = fr.field_borrows.size(); i > nf; --i) {` |
| 10499:21 | if guard | `if (fb.target != root) continue;` |
| 10499:40 | if guard | `if (fb.target != root) continue;` |
| 10500:17 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10500:68 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10500:83 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10503:25 | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 10503:36 | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 10504:26 | else-if guard | `else if (auto sb = it->shared_field_borrows.find(fb.path);` |
| 10505:30 | call / statement | `sb != it->shared_field_borrows.end()) {` |
| 10505:68 | call / statement | `sb != it->shared_field_borrows.end()) {` |
| 10506:29 | if guard | `if (sb->second <= 0)` |
| 10507:29 | statement | `(void)logos::probe::on("szw_pwl_pre0");` |
| 10508:29 | if guard | `if (--sb->second <= 0)` |
| 10509:29 | statement | `it->shared_field_borrows.erase(sb);` |
| 10510:30 | else branch | `else (void)logos::probe::on("szw_pwl_keep");` |
| 10521:30 | if guard | `if (src == root) continue;` |

### `logos::compiler::BorrowChecker::release_dead_borrows` — 22 of 82 regions cold (hottest 87052260)

| line:col | what it is | source |
|---:|---|---|
| 9348:30 | if guard | `if (scopes_.empty()) return;` |
| 9349:51 | if guard | `if (std::getenv("LOGOS_DUMP_BC_RELEASE")) {` |
| 9350:26 | statement | `std::fprintf(stderr, "[bc-release] cur_line=%llu frames=%zu\n",` |
| 9352:33 | loop body | `for (size_t fi = 0; fi < scopes_.size(); ++fi)` |
| 9352:54 | loop body | `for (size_t fi = 0; fi < scopes_.size(); ++fi)` |
| 9353:17 | loop body | `for (auto& b : scopes_[fi].borrows)` |
| 9354:21 | statement | `std::fprintf(stderr,` |
| 9354:34 | statement | `std::fprintf(stderr,` |
| 9357:25 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9357:52 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9357:63 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9403:37 | if guard | `if (it->holder.empty()) { ++it; continue; }` |
| 9415:61 | if guard | `if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++it; continue; }` |
| 9421:53 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9421:54 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9421:65 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9437:39 | if guard | `if (fit2->holder.empty()) { ++fit2; continue; }` |
| 9439:61 | if guard | `if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++fit2; continue; }` |
| 9458:53 | statement | `holder_drops_after_last_use(*fit2)) { ++fit2; continue; }` |
| 9459:53 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9459:54 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9459:65 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |

### `borrow_check.cpp:logos::compiler::build_type_sets` — 21 of 294 regions cold (hottest 425756949)

| line:col | what it is | source |
|---:|---|---|
| 180:13 | statement | `ts.borrow_carrying.insert(std::string(n.substr(dot + 1)));` |
| 186:17 | statement | `ts.borrow_carrying.insert(std::string(base.substr(d2 + 1)));` |
| 204:17 | if guard | `if (!t) return {};` |
| 218:17 | if guard | `if (!t) return false;` |
| 233:22 | if guard | `if (!ft) continue;` |
| 237:64 | if guard | `if (auto d = n.rfind('.'); d != std::string::npos) n = n.substr(d + 1);` |
| 246:13 | statement | `n = n.substr(dot + 1);` |
| 263:66 | if guard | `if (!sd.borrow_carrying() && holds_residency_holder(sd)) reg_exempt_name(std::string(sd.name()));` |
| 298:17 | statement | `ts.loan_carrying.insert(std::string(n.substr(dot + 1)));` |
| 303:21 | if guard | `if (!t) return false;` |
| 306:68 | if guard | `if (!an.empty() && ts.loan_carrying.count(an) > 0) return true;` |
| 327:30 | if guard | `if (hit) return;` |
| 329:81 | statement | `[&](TypeRef pt) { if (type_is_lc(pt)) hit = true; });` |
| 331:26 | if guard | `if (hit) { reg_lc_name(ed_name); lc_changed = true; }` |
| 344:21 | if guard | `if (!t) return false;` |
| 348:68 | if guard | `if (!an.empty() && ts.holds_mut_ref.count(an) > 0) return true;` |
| 356:17 | statement | `ts.holds_mut_ref.insert(std::string(n.substr(dot + 1)));` |
| 397:21 | if guard | `if (!t) return false;` |
| 416:17 | statement | `ts.holds_any_ref.insert(std::string(n.substr(dot + 1)));` |
| 443:13 | statement | `fprintf(stderr, "[holds_any_ref] %zu names (holds_mut_ref: %zu)\n",` |
| 443:21 | statement | `fprintf(stderr, "[holds_any_ref] %zu names (holds_mut_ref: %zu)\n",` |

### `logos::compiler::BorrowChecker::pop_scope` — 17 of 90 regions cold (hottest 74125553)

| line:col | what it is | source |
|---:|---|---|
| 2444:30 | if guard | `if (scopes_.empty()) return;` |
| 2560:32 | if guard | `if (it == nullptr) continue;` |
| 2571:25 | statement | `(void)logos::probe::on("szw_pop_pre0");` |
| 2635:29 | if guard | `if (dorder) {` |
| 2636:65 | statement | `auto didx = [&](std::string_view n) -> long {` |
| 2637:44 | loop body | `for (size_t i = 0; i < frame.declared.size(); ++i)` |
| 2637:71 | loop body | `for (size_t i = 0; i < frame.declared.size(); ++i)` |
| 2638:29 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2638:33 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2638:57 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2639:25 | early return | `return -1;` |
| 2642:25 | if guard | `if (bi >= 0)` |
| 2643:25 | loop body | `for (auto& src : sources)` |
| 2644:29 | if guard | `if (didx(src.name) > bi) {` |
| 2644:33 | if guard | `if (didx(src.name) > bi) {` |
| 2644:54 | if guard | `if (didx(src.name) > bi) {` |
| 2658:25 | diagnostic emission | `report(ref_borrow_line_[place], std::format(` |

### `logos::compiler::BorrowChecker::loop_exit_snapshot` — 16 of 46 regions cold (hottest 26280)

| line:col | what it is | source |
|---:|---|---|
| 2264:50 | if guard | `if (scopes_.size() <= outer_scope_count) return snap;` |
| 2269:43 | if guard | `if (!pending_esc_holder_.empty()) outer.insert(pending_esc_holder_);` |
| 2304:36 | if guard | `if (it == nullptr) continue;` |
| 2309:26 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2309:30 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2309:56 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2314:34 | if guard | `if (escapes(fb)) continue;` |
| 2316:36 | if guard | `if (it == nullptr) continue;` |
| 2318:22 | else branch | `else {` |
| 2323:25 | if guard | `if (sit != it->shared_field_borrows.end()) {` |
| 2323:64 | if guard | `if (sit != it->shared_field_borrows.end()) {` |
| 2324:29 | if guard | `if (sit->second <= 0)` |
| 2325:29 | statement | `(void)logos::probe::on("szw_snap_pre0");` |
| 2326:29 | if guard | `if (--sit->second <= 0)` |
| 2327:29 | statement | `it->shared_field_borrows.erase(sit);` |
| 2328:30 | else branch | `else (void)logos::probe::on("szw_snap_keep");` |

### `logos::compiler::BorrowChecker::collect_ref_sources_paths` — 12 of 233 regions cold (hottest 16397967)

| line:col | what it is | source |
|---:|---|---|
| 3122:21 | if guard | `if (!a) return false;` |
| 3150:13 | switch arm | `case EC::ArrLit:` |
| 3151:81 | call / statement | `lir_view::EArrLitView{a}.each_elem([&](lir_view::ExprRef inner) {` |
| 3152:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3152:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3152:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3152:34 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3152:43 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3152:72 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3218:49 | statement | `fv, fname.empty() ? path : sub(std::string(fname)), out);` |
| 3461:13 | switch arm | `case EC::Try:` |
| 3488:55 | statement | `TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::record_borrow` — 12 of 40 regions cold (hottest 2213272)

| line:col | what it is | source |
|---:|---|---|
| 3971:50 | if guard | `if (logos::probe::on("selftest_refuse")) {` |
| 3976:30 | if guard | `if (bp.root.empty()) return;` |
| 3987:46 | if guard | `if (logos::probe::on("movedborrow")) {` |
| 3988:62 | if guard | `if (auto* mst = var_find(bp.root_slot, bp.root); mst != nullptr) {` |
| 3988:78 | if guard | `if (auto* mst = var_find(bp.root_slot, bp.root); mst != nullptr) {` |
| 3989:21 | if guard | `if (mst->moved) {` |
| 3989:33 | if guard | `if (mst->moved) {` |
| 3995:17 | if guard | `if (!bp.path.empty())` |
| 3995:21 | if guard | `if (!bp.path.empty())` |
| 3996:21 | if guard | `if (auto* hit = find_moved_overlap(mst->moved_fields,` |
| 3996:31 | if guard | `if (auto* hit = find_moved_overlap(mst->moved_fields,` |
| 3997:66 | call / statement | `bp.path)) {` |

### `logos::compiler::BorrowChecker::visit` — 12 of 273 regions cold (hottest 54027290)

| line:col | what it is | source |
|---:|---|---|
| 11850:13 | if guard | `if (!e) return;` |
| 11903:48 | if guard | `if (!it->is_mut_binding && !param_names_.count(vname))` |
| 11904:25 | diagnostic emission | `report(line, std::format(` |
| 12473:50 | statement | `!p0.owning_dst()) \|\| (gcf && p0_ref)) {` |
| 12476:32 | if guard | `if (gcf && a0 && a0.kind() == Code::AddrOfTemp)` |
| 12476:38 | if guard | `if (gcf && a0 && a0.kind() == Code::AddrOfTemp)` |
| 12477:25 | statement | `a0 = EAddrOfTempView{a0}.inner();` |
| 12481:55 | statement | `(gcf && p0.kind() == LogosType::Kind::MutRef),` |
| 12553:47 | statement | `TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 12659:20 | else branch | `} else {` |
| 12667:9 | switch arm | `case Code::Try:` |
| 12692:9 | switch arm | `case Code::FormatCall: {` |

### `logos::compiler::BorrowChecker::type_is_residency_backed` — 11 of 35 regions cold (hottest 12)

| line:col | what it is | source |
|---:|---|---|
| 5633:17 | if guard | `if (!t) return false;` |
| 5636:41 | if guard | `if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 5637:50 | else-if guard | `else if (k == LogosType::Kind::Struct \|\| k == LogosType::Kind::ZonedStruct)` |
| 5640:49 | if guard | `if (ts_.residency_exempt.count(nm)) return true;` |
| 5643:17 | statement | `bare = bare.substr(d + 1);` |
| 5645:17 | statement | `bare = bare.substr(0, g);` |
| 5646:64 | if guard | `if (ts_.residency_exempt.count(std::string(bare))) return true;` |
| 5649:25 | if guard | `if (depth <= 0) return false;` |
| 5651:13 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |
| 5651:17 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |
| 5651:66 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |

### `logos::compiler::BorrowChecker::prov_of` — 11 of 279 regions cold (hottest 5723922)

| line:col | what it is | source |
|---:|---|---|
| 6478:21 | early return | `return {{}, /*is_local=*/false, /*is_temp=*/true};` |
| 6596:13 | switch arm | `case Code::SlicePtr:` |
| 6647:33 | if guard | `if (!a) return;` |
| 6658:71 | if guard | `if (auto it = prov_.find(cap); it != prov_.end()) {` |
| 6774:33 | if guard | `if (!a) return;` |
| 6788:21 | statement | `rp.is_temp = true;` |
| 6831:33 | statement | `fprintf(stderr, "[#86trace-carry] fn=%s loc=%d tmp=%d\n",` |
| 6831:41 | statement | `fprintf(stderr, "[#86trace-carry] fn=%s loc=%d tmp=%d\n",` |
| 6954:58 | statement | `TypeRef at0 = a ? a.type(pool) : TypeRef(nullptr);` |
| 7106:29 | if guard | `if (!a) return;` |
| 7110:44 | statement | `(elided_to >= 0 && (size_t)elided_to == fb_here))` |

### `logos::compiler::BorrowChecker::check_return_value` — 11 of 168 regions cold (hottest 20517297)

| line:col | what it is | source |
|---:|---|---|
| 7470:25 | if guard | `if (!ret_type_) return;` |
| 7489:48 | if guard | `if (std::getenv("LOGOS_DUMP_RETGATE")) {` |
| 7493:50 | statement | `std::string j; for (auto& s : srcs0) { j += s; j += ","; }` |
| 7494:21 | statement | `fprintf(stderr,` |
| 7502:13 | statement | `fprintf(stderr, "[#86trace-gate] fn=%s line=%u\n", fn_name_.c_str(), line);` |
| 7502:21 | statement | `fprintf(stderr, "[#86trace-gate] fn=%s line=%u\n", fn_name_.c_str(), line);` |
| 7534:55 | if guard | `if (src.empty() && !srcs.empty()) src = srcs.front();` |
| 7698:51 | if guard | `if (it == param_lifetimes_.end()) continue;` |
| 7714:33 | statement | `: outlives(src_lt, ret_lt, outlives_adj_,` |
| 7738:44 | statement | `? false` |
| 7761:53 | if guard | `if (!prov.params.count(sole_param)) {` |

### `logos::compiler::BorrowChecker::scan_uses_stmt` — 11 of 53 regions cold (hottest 86040927)

| line:col | what it is | source |
|---:|---|---|
| 9137:18 | if guard | `if (!sr) return;` |
| 9165:13 | switch arm | `case Code::FieldWrite: {` |
| 9168:21 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9168:21 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9168:59 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9169:21 | statement | `prescan_reborrow_place(std::string(v.receiver()) + "." +` |
| 9174:13 | switch arm | `case Code::IndexWrite: {` |
| 9181:13 | switch arm | `case Code::FieldIndexWrite: {` |
| 9188:13 | switch arm | `case Code::ChainFieldWrite: {` |
| 9194:13 | switch arm | `case Code::DerefFieldWrite: {` |
| 9206:13 | switch arm | `case Code::TupleWrite: {` |

### `logos::compiler::BorrowChecker::release_borrows_held_by` — 10 of 48 regions cold (hottest 44753)

| line:col | what it is | source |
|---:|---|---|
| 9291:13 | early return | `return true;` |
| 9301:17 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9301:38 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9301:42 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9301:60 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9338:21 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9338:48 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9338:79 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9339:77 | if guard | `if (auto sit = var_find(fit->target_slot, fit->target); sit != nullptr)` |
| 9340:25 | statement | `sit->mut_field_borrows.erase(fit->path);` |

### `logos::compiler::BorrowChecker::retain_operand_loans` — 9 of 29 regions cold (hottest 4190)

| line:col | what it is | source |
|---:|---|---|
| 5936:35 | if guard | `if (!e \|\| holder.empty()) return;` |
| 5939:22 | if guard | `if (!op) return;` |
| 5941:22 | if guard | `if (!ot) return;` |
| 5954:13 | switch arm | `case Code::StructLit:  EStructLitView{e}.each_field_value(one); break;` |
| 5955:13 | switch arm | `case Code::TupleLit:   ETupleLitView{e}.each_elem(one);         break;` |
| 5956:13 | switch arm | `case Code::ArrLit:     EArrLitView{e}.each_elem(one);           break;` |
| 5957:13 | switch arm | `case Code::EnumLitData:EEnumLitDataView{e}.each_payload(one);   break;` |
| 5958:13 | switch arm | `case Code::Cast:       one(ECastView{e}.operand());             break;` |
| 5959:13 | switch arm | `default: break;` |

### `logos::compiler::BorrowChecker::carried_prov_of_recv` — 9 of 34 regions cold (hottest 5111)

| line:col | what it is | source |
|---:|---|---|
| 6427:17 | if guard | `if (!r) return {};` |
| 6428:39 | if guard | `if (r.kind() == Code::AddrOf) {` |
| 6430:17 | if guard | `if (param_names_.count(nm)) return {{nm}, false};` |
| 6430:41 | if guard | `if (param_names_.count(nm)) return {{nm}, false};` |
| 6431:13 | statement | `auto it = prov_.find(nm);` |
| 6432:20 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6432:40 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6432:53 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6448:54 | statement | `: RefProv{{}, /*is_local=*/true};` |

### `borrow_check.cpp:logos::compiler::borrow_check` — 8 of 37 regions cold (hottest 73693199)

| line:col | what it is | source |
|---:|---|---|
| 12791:55 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12792:54 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12792:58 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12792:61 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12793:25 | early return | `return true; }()) continue;` |
| 12839:13 | statement | `ri.dump(std::string(bare_fn_name(fn.name())));` |
| 12850:17 | statement | `std::swap(first, second);` |
| 12854:52 | if guard | `if (target_label.starts_with("<temp")) target_label = "temporary";` |

### `logos::compiler::BorrowChecker::take_borrow_whole_` — 6 of 60 regions cold (hottest 1630365)

| line:col | what it is | source |
|---:|---|---|
| 3891:29 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3891:33 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3891:33 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3891:56 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3891:68 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3894:28 | else branch | `} else {` |

### `void logos::compiler::BorrowChecker::each_pat_binding` — 6 of 30 regions cold (hottest 7301087)

| line:col | what it is | source |
|---:|---|---|
| 4804:18 | if guard | `if (!pr) return;` |
| 4839:13 | switch arm | `case PC::Or:` |
| 4840:55 | statement | `PatOrView{pr}.each_alt([&](PatRef alt){ each_pat_binding(alt, f); });` |
| 4849:13 | switch arm | `case PC::At: {` |
| 4852:26 | if guard | `if (auto sub = v.sub()) each_pat_binding(sub, f);` |
| 4852:41 | if guard | `if (auto sub = v.sub()) each_pat_binding(sub, f);` |

### `logos::compiler::BorrowChecker::residency_exemption_holds` — 6 of 28 regions cold (hottest 20678053)

| line:col | what it is | source |
|---:|---|---|
| 5664:17 | if guard | `if (!e) return true;` |
| 5668:73 | if guard | `if (is_return_temp_name(n) \|\| is_materialized_temp_name(n)) continue;` |
| 5671:21 | statement | `fprintf(stderr, "[#86trace-exempt-denied] fn=%s src=%s\n",` |
| 5671:29 | statement | `fprintf(stderr, "[#86trace-exempt-denied] fn=%s src=%s\n",` |
| 5732:21 | statement | `fprintf(stderr, "[#86trace-exempt-multishare] fn=%s n=%d\n",` |
| 5732:29 | statement | `fprintf(stderr, "[#86trace-exempt-multishare] fn=%s n=%d\n",` |

### `logos::compiler::BorrowChecker::drop_can_observe_borrow` — 5 of 38 regions cold (hottest 108218)

| line:col | what it is | source |
|---:|---|---|
| 2776:22 | if guard | `if (!sd) return false;` |
| 2779:26 | if guard | `if (!ft) continue;` |
| 2783:61 | if guard | `if (drop_can_observe_borrow(ft, depth + 1)) return true;` |
| 2790:46 | if guard | `if (pit != ts_.spec_by_name.end() && reaches_ref(pit->second)) return true;` |
| 2790:72 | if guard | `if (pit != ts_.spec_by_name.end() && reaches_ref(pit->second)) return true;` |

### `logos::compiler::BorrowChecker::collect_borrow_locals` — 5 of 16 regions cold (hottest 58)

| line:col | what it is | source |
|---:|---|---|
| 2833:17 | if guard | `if (!e) return;` |
| 2844:13 | switch arm | `case EC::AddrOfTemp:` |
| 2851:13 | switch arm | `case EC::TupleLit:` |
| 2853:47 | statement | `[&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });` |
| 2855:13 | switch arm | `case EC::Cast:` |

### `logos::compiler::BorrowChecker::propagate_pat_reborrows` — 5 of 57 regions cold (hottest 21179072)

| line:col | what it is | source |
|---:|---|---|
| 5156:28 | if guard | `if (!pr \|\| !scrut) return;` |
| 5187:34 | if guard | `if (src.empty()) return;` |
| 5196:55 | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 5206:37 | if guard | `if (n == place) return;` |
| 5228:28 | if guard | `if (s.empty()) continue;` |

### `logos::compiler::BorrowChecker::retains_loan_carrying_operand` — 5 of 26 regions cold (hottest 3340866)

| line:col | what it is | source |
|---:|---|---|
| 5888:17 | if guard | `if (!e) return false;` |
| 5905:13 | switch arm | `case Code::StructLit:` |
| 5907:13 | switch arm | `case Code::TupleLit:  ETupleLitView{e}.each_elem(by_value_bc); break;` |
| 5908:13 | switch arm | `case Code::ArrLit:    EArrLitView{e}.each_elem(by_value_bc);  break;` |
| 5909:13 | switch arm | `case Code::EnumLitData:` |

### `logos::compiler::BorrowChecker::type_hides_borrow_` — 5 of 65 regions cold (hottest 57462959)

| line:col | what it is | source |
|---:|---|---|
| 7214:17 | if guard | `if (!t) return false;` |
| 7217:21 | if guard | `if (!a) return false;` |
| 7248:26 | if guard | `if (!sd) return false;` |
| 7258:50 | if guard | `if (pit != ts_.spec_by_name.end() && walk(pit->second)) return true;` |
| 7258:69 | if guard | `if (pit != ts_.spec_by_name.end() && walk(pit->second)) return true;` |

### `borrow_check.cpp:logos::compiler::has_droppable_fields` — 4 of 24 regions cold (hottest 7837238)

| line:col | what it is | source |
|---:|---|---|
| 462:52 | if guard | `if (!t \|\| t.kind() != LogosType::Kind::Struct) return false;` |
| 472:18 | if guard | `if (!sd) return false;` |
| 480:41 | if guard | `if (pit != ts.spec_by_name.end() && def_has_drop(pit->second)) return true;` |
| 480:68 | if guard | `if (pit != ts.spec_by_name.end() && def_has_drop(pit->second)) return true;` |

### `borrow_check.cpp:logos::compiler::ref_source_places` — 4 of 76 regions cold (hottest 28310599)

| line:col | what it is | source |
|---:|---|---|
| 1723:9 | switch arm | `case Code::Try:` |
| 1767:31 | if guard | `if (is_rawptr(r)) return;` |
| 1793:31 | if guard | `if (is_rawptr(s)) return;` |
| 1805:20 | if guard | `if (p.empty()) return;` |

### `logos::compiler::BorrowChecker::take_field_borrow_path_` — 4 of 56 regions cold (hottest 582893)

| line:col | what it is | source |
|---:|---|---|
| 3727:47 | if guard | `if (is_mut && it->shared_borrows > 0) {` |
| 3742:43 | if guard | `if (is_mut && root_is_shared_ref) {` |
| 3796:25 | if guard | `if (c == 0) (void)logos::probe::on("szr_take_zero");` |
| 3797:25 | if guard | `if (c <  0) (void)logos::probe::on("szr_take_neg");` |

### `logos::compiler::BorrowChecker::deref_type_of_` — 4 of 14 regions cold (hottest 2952)

| line:col | what it is | source |
|---:|---|---|
| 4600:40 | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 4601:18 | if guard | `if (!ot) return TypeRef(nullptr);` |
| 4604:13 | call / statement | `ot.kind() == LogosType::Kind::Ptr)` |
| 4606:9 | early return | `return TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` — 4 of 31 regions cold (hottest 10596830)

| line:col | what it is | source |
|---:|---|---|
| 4692:13 | statement | `cur = lir_view::ECastView{cur}.operand();` |
| 4693:19 | if guard | `if (!cur) return;` |
| 4702:17 | statement | `cur = lir_view::ECastView{cur}.operand();` |
| 4707:25 | if guard | `if (nm.empty()) return;` |

### `logos::compiler::BorrowChecker::check_place_mut_use` — 4 of 29 regions cold (hottest 7519)

| line:col | what it is | source |
|---:|---|---|
| 5529:30 | if guard | `if (bp.root.empty()) return;` |
| 5531:29 | if guard | `if (sit == nullptr) return;` |
| 5537:32 | if guard | `if (sit->mut_borrowed) return;` |
| 5538:38 | if guard | `if (sit->shared_borrows > 0) {` |

### `logos::compiler::BorrowChecker::note_holder_escape_prov` — 4 of 47 regions cold (hottest 679485)

| line:col | what it is | source |
|---:|---|---|
| 5795:35 | if guard | `if (name.empty() \|\| !val) return;` |
| 5804:56 | if guard | `if (residency_exemption_holds(holder_ty, val)) return;` |
| 5816:13 | statement | `fprintf(stderr, "[#86trace-%s] fn=%s line=%u var=%s loc=%d tmp=%d\n",` |
| 5816:21 | statement | `fprintf(stderr, "[#86trace-%s] fn=%s line=%u var=%s loc=%d tmp=%d\n",` |

### `logos::compiler::BorrowChecker::prov_of_retained` — 4 of 63 regions cold (hottest 3877805)

| line:col | what it is | source |
|---:|---|---|
| 7368:17 | if guard | `if (!e) return merged;` |
| 7371:21 | if guard | `if (!a) return;` |
| 7413:29 | statement | `one(a);` |
| 7460:25 | statement | `merged.is_local = true;   // capture of a plain local` |

### `logos::compiler::BorrowChecker::release_place_retarget` — 4 of 32 regions cold (hottest 219)

| line:col | what it is | source |
|---:|---|---|
| 10438:48 | if guard | `if (logos::probe::on("retarget_keep")) return;` |
| 10442:38 | if guard | `if (it == targets.end()) return false;` |
| 10449:66 | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 10450:43 | if guard | `if (!take_one(br.target)) continue;` |

### `logos::compiler::BorrowChecker::propagate_pat_borrows` — 3 of 29 regions cold (hottest 21178992)

| line:col | what it is | source |
|---:|---|---|
| 5100:28 | if guard | `if (!pr \|\| !scrut) return;` |
| 5121:55 | if guard | `if (place.size() < base_place.size()) return;` |
| 5126:35 | statement | `: base.path + place.substr(base_place.size());` |

### `logos::compiler::BorrowChecker::type_is_share_handle` — 3 of 20 regions cold (hottest 649)

| line:col | what it is | source |
|---:|---|---|
| 5747:17 | if guard | `if (!t) return false;` |
| 5756:13 | statement | `bare = bare.substr(d + 1);` |
| 5758:13 | statement | `bare = bare.substr(0, g);` |

### `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` — 3 of 82 regions cold (hottest 22895)

| line:col | what it is | source |
|---:|---|---|
| 6061:40 | if guard | `if (recv_is_rawptr(r)) return {};` |
| 6066:40 | if guard | `if (recv_is_rawptr(r)) return {};` |
| 6083:39 | if guard | `if (k == Code::SlicePtr)  { cur = ESlicePtrView{cur}.slice(); continue; }` |

### `logos::compiler::BorrowChecker::scan_uses_expr` — 3 of 52 regions cold (hottest 115305148)

| line:col | what it is | source |
|---:|---|---|
| 9012:13 | switch arm | `case Code::Try:` |
| 9094:13 | switch arm | `case Code::FormatCall: {` |
| 9097:42 | statement | `v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });` |

### `logos::compiler::BorrowChecker::ref_sources_of[abi:cxx11]` — 3 of 42 regions cold (hottest 262108)

| line:col | what it is | source |
|---:|---|---|
| 9965:21 | if guard | `if (!a) return;` |
| 10014:13 | switch arm | `case Code::Try:` |
| 10016:21 | statement | `add(std::move(p));` |

### `borrow_check.cpp:logos::compiler::BorrowChecker::visit` — 3 of 69 regions cold (hottest 5287818)

| line:col | what it is | source |
|---:|---|---|
| 12102:53 | statement | `TypeRef rt = r ? r.type(pool) : TypeRef(nullptr);` |
| 12262:33 | if guard | `if (!a) return;` |
| 12411:37 | if guard | `if (!a) return;` |

### `borrow_check.cpp:logos::compiler::is_cond_move_field_drop_place` — 2 of 8 regions cold (hottest 231)

| line:col | what it is | source |
|---:|---|---|
| 752:13 | switch arm | `default:` |
| 756:5 | early return | `return false;` |

### `borrow_check.cpp:logos::compiler::merge_loans` — 2 of 21 regions cold (hottest 3363607)

| line:col | what it is | source |
|---:|---|---|
| 1243:55 | if guard | `if (st.mut_reservations > b.mut_reservations) b.mut_reservations = st.mut_reservations;` |
| 1261:26 | if guard | `if (n > cur) cur = n;` |

### `void logos::compiler::RefGraph::each_root_place` — 2 of 24 regions cold (hottest 39070037)

| line:col | what it is | source |
|---:|---|---|
| 1593:28 | if guard | `if (start.empty()) return;` |
| 1599:71 | if guard | `if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;` |

### `logos::compiler::BorrowChecker::loop_target` — 2 of 13 regions cold (hottest 5706)

| line:col | what it is | source |
|---:|---|---|
| 2226:34 | if guard | `if (loop_stack_.empty()) return nullptr;` |
| 2230:9 | early return | `return &loop_stack_.back();` |

### `auto logos::compiler::BorrowChecker::loop_exit_snapshot` — 2 of 13 regions cold (hottest 194)

| line:col | what it is | source |
|---:|---|---|
| 2277:37 | if guard | `if (rec.holder.empty()) return false;   // lexical: dies at pop` |
| 2296:44 | if guard | `if (lec \|\| outer.count(h)) return true;` |

### `logos::compiler::BorrowChecker::walk_closure_body` — 2 of 17 regions cold (hottest 674)

| line:col | what it is | source |
|---:|---|---|
| 2410:19 | if guard | `if (!cbb) return;` |
| 2422:29 | if guard | `if (pn.empty()) return;` |

### `logos::compiler::BorrowChecker::struct_is_dropck_relevant` — 2 of 22 regions cold (hottest 4484240)

| line:col | what it is | source |
|---:|---|---|
| 2747:46 | if guard | `if (pit != ts_.spec_by_name.end() && has_lt(pit->second)) return true;` |
| 2747:67 | if guard | `if (pit != ts_.spec_by_name.end() && has_lt(pit->second)) return true;` |

### `logos::compiler::BorrowChecker::apply_call_outparam_rules` — 2 of 37 regions cold (hottest 7118242)

| line:col | what it is | source |
|---:|---|---|
| 4100:21 | if guard | `if (!a) continue;` |
| 4126:25 | if guard | `if (!a) continue;` |

### `logos::compiler::BorrowChecker::apply_flow_outparams` — 2 of 66 regions cold (hottest 6961816)

| line:col | what it is | source |
|---:|---|---|
| 4182:68 | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 4204:27 | if guard | `if (!src) continue;` |

### `logos::compiler::BorrowChecker::erase_reinit` — 2 of 12 regions cold (hottest 12402)

| line:col | what it is | source |
|---:|---|---|
| 4463:18 | statement | `it->first.compare(0, path.size(), path) == 0 &&` |
| 4464:18 | statement | `it->first[path.size()] == '.');` |

### `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` — 2 of 23 regions cold (hottest 18)

| line:col | what it is | source |
|---:|---|---|
| 4631:40 | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 4636:9 | early return | `return "cannot move out of a dereference (E0507)";` |

### `logos::compiler::BorrowChecker::declare_pat_bindings` — 2 of 16 regions cold (hottest 21194302)

| line:col | what it is | source |
|---:|---|---|
| 4740:18 | if guard | `if (!pr) return;` |
| 4762:49 | call / statement | `void declare_pat_bindings(const Pattern& p) {` |

### `void logos::compiler::BorrowChecker::each_pat_binding_place` — 2 of 39 regions cold (hottest 42285618)

| line:col | what it is | source |
|---:|---|---|
| 4980:34 | if guard | `if (!pr \|\| base.empty()) return;` |
| 5027:42 | statement | `? base : sub(std::string(fb.field_name()));` |

### `logos::compiler::BorrowChecker::method_self_kind` — 2 of 38 regions cold (hottest 16065482)

| line:col | what it is | source |
|---:|---|---|
| 5353:45 | statement | `auto kind0 = f_params.empty() ? LogosType::Kind::Void` |
| 5364:31 | if guard | `if (f_params.empty()) return 0;` |

### `logos::compiler::BorrowChecker::is_reborrow_store_value` — 2 of 16 regions cold (hottest 5527726)

| line:col | what it is | source |
|---:|---|---|
| 10077:44 | if guard | `if (k != LogosType::Kind::Array && k != LogosType::Kind::Slice) return false;` |
| 10077:73 | if guard | `if (k != LogosType::Kind::Array && k != LogosType::Kind::Slice) return false;` |

### `logos::compiler::BorrowChecker::note_reborrow_place` — 2 of 14 regions cold (hottest 1043371)

| line:col | what it is | source |
|---:|---|---|
| 10215:28 | if guard | `if (place.empty()) return;` |
| 10217:45 | statement | `TypeRef vt = val ? val.type(pool) : TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::resolve_ref_places` — 2 of 21 regions cold (hottest 5327293)

| line:col | what it is | source |
|---:|---|---|
| 10291:27 | if guard | `if (base.empty()) return;` |
| 10321:31 | if guard | `if (next.empty()) break;` |

### `auto logos::compiler::BorrowChecker::place_write_loans` — 2 of 15 regions cold (hottest 34)

| line:col | what it is | source |
|---:|---|---|
| 10534:40 | if guard | `if (rec.holder == src) return;` |
| 10536:46 | statement | `!= rec.co_holders.end()) return;` |

### `borrow_check.cpp:logos::compiler::is_move_type` — 1 of 34 regions cold (hottest 16499743)

| line:col | what it is | source |
|---:|---|---|
| 525:38 | if guard | `if (ts.drop_types.count(en)) return true;       // has a Drop impl` |

### `borrow_check.cpp:logos::compiler::is_temporary_value_expr` — 1 of 20 regions cold (hottest 10622044)

| line:col | what it is | source |
|---:|---|---|
| 781:13 | if guard | `if (!e) return false;` |

### `borrow_check.cpp:logos::compiler::bc_is_borrow_carrying_type` — 1 of 37 regions cold (hottest 270211767)

| line:col | what it is | source |
|---:|---|---|
| 1276:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` — 1 of 32 regions cold (hottest 299203029)

| line:col | what it is | source |
|---:|---|---|
| 1360:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_holds_mut_ref_type` — 1 of 35 regions cold (hottest 192593262)

| line:col | what it is | source |
|---:|---|---|
| 1382:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_holds_any_ref_type` — 1 of 35 regions cold (hottest 201304332)

| line:col | what it is | source |
|---:|---|---|
| 1423:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::build_fn_index` — 1 of 12 regions cold (hottest 73693199)

| line:col | what it is | source |
|---:|---|---|
| 1462:17 | if guard | `if (!f) return;` |

### `void logos::compiler::RefGraph::each_under` — 1 of 9 regions cold (hottest 22515)

| line:col | what it is | source |
|---:|---|---|
| 1545:27 | if guard | `if (root.empty()) return;` |

### `void logos::compiler::RefGraph::each_root` — 1 of 15 regions cold (hottest 7672898)

| line:col | what it is | source |
|---:|---|---|
| 1566:28 | if guard | `if (start.empty()) return;` |

### `borrow_check.cpp:void logos::compiler::each_ref_store` — 1 of 21 regions cold (hottest 32296196)

| line:col | what it is | source |
|---:|---|---|
| 1836:44 | if guard | `if (dest.empty() \|\| !val \|\| depth > 8) return;` |

### `logos::compiler::BorrowChecker::var_find` — 1 of 2 regions cold (hottest 82612027)

| line:col | what it is | source |
|---:|---|---|
| 1919:74 | statement | `const VarState* var_find(uint32_t slot, std::string_view name) const {` |

### `logos::compiler::BorrowChecker::stmt_point` — 1 of 9 regions cold (hottest 173050739)

| line:col | what it is | source |
|---:|---|---|
| 2184:18 | if guard | `if (!sr) return 0;` |

### `auto logos::compiler::BorrowChecker::pop_scope` — 1 of 20 regions cold (hottest 126300107)

| line:col | what it is | source |
|---:|---|---|
| 2522:57 | if guard | `if (logos::probe::on("rehome_all")) return true;` |

### `bool logos::compiler::BorrowChecker::holder_drops_after_last_use` — 1 of 8 regions cold (hottest 85857)

| line:col | what it is | source |
|---:|---|---|
| 2799:60 | if guard | `if (drop_can_observe_borrow(holder_ty_of(co))) return true;` |

### `logos::compiler::BorrowChecker::ref_sources_under` — 1 of 11 regions cold (hottest 12482565)

| line:col | what it is | source |
|---:|---|---|
| 2886:27 | if guard | `if (root.empty()) return out;` |

### `logos::compiler::BorrowChecker::add_ref_sources` — 1 of 6 regions cold (hottest 2739)

| line:col | what it is | source |
|---:|---|---|
| 2933:35 | if guard | `if (name.empty() \|\| !val) return;` |

### `logos::compiler::BorrowChecker::flow_operand_root[abi:cxx11]` — 1 of 10 regions cold (hottest 214608)

| line:col | what it is | source |
|---:|---|---|
| 4078:17 | if guard | `if (!a) return {};` |

### `logos::compiler::BorrowChecker::holders_last_use` — 1 of 7 regions cold (hottest 103562)

| line:col | what it is | source |
|---:|---|---|
| 4368:60 | statement | `co[i], i < co_slots.size() ? co_slots[i] : NO_SLOT));` |

### `logos::compiler::BorrowChecker::is_loan_holder` — 1 of 14 regions cold (hottest 4820)

| line:col | what it is | source |
|---:|---|---|
| 4405:49 | statement | `!= fb.co_holders.end()) return true;` |

### `logos::compiler::BorrowChecker::consume` — 1 of 23 regions cold (hottest 366134)

| line:col | what it is | source |
|---:|---|---|
| 4485:17 | diagnostic emission | `report(line, std::format("use of moved value '{}'", name));` |

### `logos::compiler::BorrowChecker::deref_move_exempt` — 1 of 22 regions cold (hottest 9995)

| line:col | what it is | source |
|---:|---|---|
| 4540:18 | if guard | `if (!op) return true;` |

### `logos::compiler::BorrowChecker::check_live` — 1 of 13 regions cold (hottest 30052679)

| line:col | what it is | source |
|---:|---|---|
| 4656:17 | diagnostic emission | `report(line, std::format("use of moved value '{}'", name));` |

### `auto void logos::compiler::BorrowChecker::each_pat_binding` — 1 of 13 regions cold (hottest 8630002)

| line:col | what it is | source |
|---:|---|---|
| 4812:68 | statement | `f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr));` |

### `logos::compiler::BorrowChecker::propagate_pat_prov` — 1 of 18 regions cold (hottest 21179072)

| line:col | what it is | source |
|---:|---|---|
| 4906:28 | if guard | `if (!pr \|\| !scrut) return;` |

### `auto void logos::compiler::BorrowChecker::each_pat_binding_place` — 1 of 16 regions cold (hottest 56529478)

| line:col | what it is | source |
|---:|---|---|
| 4990:68 | statement | `f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr),` |

### `logos::compiler::BorrowChecker::is_self_borrowing` — 1 of 26 regions cold (hottest 148417)

| line:col | what it is | source |
|---:|---|---|
| 5299:17 | if guard | `if (!f) return false;` |

### `logos::compiler::BorrowChecker::check_recv_conflict` — 1 of 33 regions cold (hottest 111282)

| line:col | what it is | source |
|---:|---|---|
| 5415:13 | diagnostic emission | `report(line, std::format(` |

### `logos::compiler::BorrowChecker::type_is_residency_exempt` — 1 of 13 regions cold (hottest 20678053)

| line:col | what it is | source |
|---:|---|---|
| 5583:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::type_retains_values` — 1 of 21 regions cold (hottest 3627399)

| line:col | what it is | source |
|---:|---|---|
| 5873:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::type_may_carry_borrow` — 1 of 36 regions cold (hottest 69075430)

| line:col | what it is | source |
|---:|---|---|
| 5979:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::bc_hop_roots` — 1 of 158 regions cold (hottest 7705290)

| line:col | what it is | source |
|---:|---|---|
| 6355:40 | if guard | `if (recv_is_rawptr(r)) return;` |

### `logos::compiler::BorrowChecker::retains_borrowing_operand` — 1 of 34 regions cold (hottest 286533)

| line:col | what it is | source |
|---:|---|---|
| 7330:17 | if guard | `if (!e) return false;` |

### `logos::compiler::BorrowChecker::prescan_reborrow_place` — 1 of 9 regions cold (hottest 2051650)

| line:col | what it is | source |
|---:|---|---|
| 8881:36 | if guard | `if (place.empty() \|\| !val) return;` |

### `logos::compiler::BorrowChecker::note_use_slot` — 1 of 13 regions cold (hottest 63423939)

| line:col | what it is | source |
|---:|---|---|
| 8974:27 | if guard | `if (name.empty()) return;` |

### `logos::compiler::BorrowChecker::visit_block` — 1 of 18 regions cold (hottest 33929729)

| line:col | what it is | source |
|---:|---|---|
| 9570:56 | diagnostic emission | `report(cursor ? (uint32_t)cursor : 0, std::format(` |

### `logos::compiler::BorrowChecker::visit_loop_body` — 1 of 15 regions cold (hottest 107982)

| line:col | what it is | source |
|---:|---|---|
| 9604:44 | loop body | `for (auto& r : var_loan_roots) inherit_loans(r, loop_vars.front(), 0);` |

### `logos::compiler::BorrowChecker::place_write_root[abi:cxx11]` — 1 of 41 regions cold (hottest 2785486)

| line:col | what it is | source |
|---:|---|---|
| 9796:26 | if guard | `if (!op) break;` |

### `logos::compiler::BorrowChecker::closure_caps_of[abi:cxx11]` — 1 of 15 regions cold (hottest 63319)

| line:col | what it is | source |
|---:|---|---|
| 10381:22 | if guard | `if (!callee) return nullptr;` |

### `logos::compiler::BorrowChecker::call_callee` — 1 of 7 regions cold (hottest 63319)

| line:col | what it is | source |
|---:|---|---|
| 10393:9 | early return | `return {};` |

### `logos::compiler::BorrowChecker::flow_of_call` — 1 of 4 regions cold (hottest 4639448)

| line:col | what it is | source |
|---:|---|---|
| 11701:74 | early return | `return flows_ ? resolve_call_flow(*flows_, symbol, &fn_index_) : nullptr;` |

### `logos::compiler::BorrowChecker::flow_of_method` — 1 of 4 regions cold (hottest 607272)

| line:col | what it is | source |
|---:|---|---|
| 11706:25 | statement | `: nullptr;` |

## C. Near-dead regions — count 1..9 (258)

**This is the class that wasted three probe slots.** A site here is
live, so a probe on it is not "never fired" — it fires, twice, and
reports a ceiling of 0 that reads exactly like a refuted hypothesis.
Before spending a slot here, ask whether a population of this size
could show the effect at all.

| count | line:col | function | what it is | source |
|---:|---:|---|---|---|
| 1 | 1600:36 | `void logos::compiler::RefGraph::each_root_place` | if guard | `if (seen.size() > 512) break;          // bound, as elsewhere here` |
| 1 | 2574:26 | `logos::compiler::BorrowChecker::pop_scope` | else branch | `else (void)logos::probe::on("szw_pop_keep");` |
| 1 | 3701:31 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live_conflict")) continue;` |
| 1 | 3701:78 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live_conflict")) continue;` |
| 1 | 3925:49 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!it->mut_field_borrows.empty()) {` |
| 1 | 4444:54 | `logos::compiler::BorrowChecker::path_overlaps` | early return | `return b.compare(0, a.size(), a) == 0 && b[a.size()] == '.';` |
| 1 | 4462:17 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4462:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4462:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4465:46 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 1 | 4700:17 | `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` | statement | `cur = lir_view::EAddrOfTempView{cur}.inner();` |
| 1 | 5443:13 | `logos::compiler::BorrowChecker::check_recv_conflict` | diagnostic emission | `report(line, std::format(` |
| 1 | 6112:45 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (ts_.frame_consts.count(rn)) return rn;` |
| 1 | 6655:28 | `logos::compiler::BorrowChecker::prov_of` | if guard | `if (!caps) return {};` |
| 1 | 7409:25 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (fs) {` |
| 1 | 7411:48 | `logos::compiler::BorrowChecker::prov_of_retained` | call / statement | `fv.each_arg([&](ExprRef a) {` |
| 1 | 7412:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7412:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7412:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7412:34 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7412:53 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7663:17 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!var_has(NO_SLOT, src))` |
| 1 | 7664:17 | `logos::compiler::BorrowChecker::check_return_value` | diagnostic emission | `report(line, std::format(` |
| 1 | 10453:26 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 10453:30 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 10453:55 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 11956:55 | `logos::compiler::BorrowChecker::visit` | call / statement | `sit->moved_fields, path)) {` |
| 1 | 12013:62 | `logos::compiler::BorrowChecker::visit` | if guard | `if (is_mut && sit->mut_reservations > 0) {` |
| 1 | 12645:41 | `borrow_check.cpp:logos::compiler::BorrowChecker::visit` | if guard | `if (st.moved && saved_s.has_id(slot, name))` |
| 1 | 12646:29 | `borrow_check.cpp:logos::compiler::BorrowChecker::visit` | statement | `merged_s->at_id(slot, name) = st;` |
| 2 | 2313:50 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | loop body | `for (auto& fb : frame.field_borrows) {` |
| 2 | 2314:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (escapes(fb)) continue;` |
| 2 | 2315:17 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | statement | `auto* it = snap.find(fb.target_slot, fb.target);` |
| 2 | 2316:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (it == nullptr) continue;` |
| 2 | 2317:17 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 2317:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 2317:32 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 4497:13 | `logos::compiler::BorrowChecker::consume` | early return | `return false;` |
| 2 | 5218:29 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | statement | `adds.emplace_back(n + kv.first.substr(place.size()),` |
| 2 | 5220:42 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& a : adds) {` |
| 2 | 5223:29 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | if guard | `if (std::find(v.begin(), v.end(), s) == v.end())` |
| 2 | 5223:33 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | if guard | `if (std::find(v.begin(), v.end(), s) == v.end())` |
| 2 | 5224:33 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | statement | `v.push_back(s);` |
| 2 | 5520:19 | `logos::compiler::BorrowChecker::check_place_mut_use` | statement | `? std::string("cannot assign to a place behind a `&` reference")` |
| 2 | 5647:33 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (bare == "Rc" \|\| bare == "Arc") return true;` |
| 2 | 5649:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (depth <= 0) return false;` |
| 2 | 5649:13 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (depth <= 0) return false;` |
| 2 | 5650:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | loop body | `for (auto a : t.type_args())` |
| 2 | 5652:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | early return | `return false;` |
| 2 | 5669:48 | `logos::compiler::BorrowChecker::residency_exemption_holds` | if guard | `if (!local_is_residency_backed(n)) {` |
| 2 | 5670:21 | `logos::compiler::BorrowChecker::residency_exemption_holds` | if guard | `if (std::getenv("LOGOS_86_TRACE"))` |
| 2 | 7352:13 | `logos::compiler::BorrowChecker::retains_borrowing_operand` | switch arm | `case Code::ArrLit:      EArrLitView{e}.each_elem(one); break;` |
| 2 | 7619:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `default: break;` |
| 2 | 7900:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | early return | `return;` |
| 2 | 7902:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 2 | 7902:30 | `logos::compiler::BorrowChecker::take_ref_borrows` | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 2 | 8083:59 | `logos::compiler::BorrowChecker::take_ref_borrows` | call / statement | `sit->moved_fields, path)) {` |
| 2 | 10129:27 | `logos::compiler::BorrowChecker::note_reborrow` | if guard | `if (name.empty()) return;` |
| 2 | 10371:27 | `logos::compiler::BorrowChecker::note_closure_caps` | if guard | `if (name.empty()) return;` |
| 2 | 10385:44 | `logos::compiler::BorrowChecker::closure_caps_of[abi:cxx11]` | if guard | `if (callee.kind() != Code::VarRef) return nullptr;` |
| 2 | 10532:21 | `auto logos::compiler::BorrowChecker::place_write_loans` | call / statement | `std::find(rec.co_holders.begin(), rec.co_holders.end(), root)` |
| 2 | 10533:50 | `auto logos::compiler::BorrowChecker::place_write_loans` | statement | `== rec.co_holders.end()) return;` |
| 2 | 10543:54 | `logos::compiler::BorrowChecker::place_write_loans` | loop body | `for (auto& fb : frame.field_borrows) reroot(fb);` |
| 3 | 2858:13 | `logos::compiler::BorrowChecker::collect_borrow_locals` | switch arm | `default:` |
| 3 | 3147:72 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3 | 3585:43 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 3 | 3657:24 | `logos::compiler::BorrowChecker::path_prefix_or_eq` | if guard | `if (a.empty()) return true;  // whole-value covers everything` |
| 3 | 3718:28 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (it == nullptr) return;` |
| 3 | 4591:35 | `logos::compiler::BorrowChecker::deref_move_exempt` | if guard | `if (in_destructure_temp_) return true;` |
| 3 | 4634:9 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4634:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4634:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4634:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4635:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | early return | `return "cannot move out of a value behind a mutable reference (E0507)";` |
| 3 | 5521:19 | `logos::compiler::BorrowChecker::check_place_mut_use` | statement | `: std::format("cannot assign to '{}': '{}' is behind a "` |
| 3 | 6085:39 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (k == Code::AddrOfTemp){ cur = EAddrOfTempView{cur}.inner(); continue; }` |
| 3 | 6144:17 | `logos::compiler::BorrowChecker::bc_hop_roots` | if guard | `if (!e) return;` |
| 3 | 7570:25 | `logos::compiler::BorrowChecker::check_return_value` | loop body | `for (auto& n : it->second)` |
| 3 | 7571:29 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7571:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7571:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7571:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7571:47 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7572:33 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!is_materialized_temp_name(n)) { src = n; break; }` |
| 3 | 7572:64 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!is_materialized_temp_name(n)) { src = n; break; }` |
| 3 | 7616:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::ArrLit:   case Code::LitInt:` |
| 3 | 7616:50 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::ArrLit:   case Code::LitInt:` |
| 3 | 7617:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::LitFloat: case Code::LitBool:` |
| 3 | 7617:50 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::LitFloat: case Code::LitBool:` |
| 3 | 7622:35 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 3 | 7748:64 | `logos::compiler::BorrowChecker::check_return_value` | statement | `ret_lt, src, src_lt.empty() ? "(elided)" : src_lt));` |
| 3 | 12030:55 | `logos::compiler::BorrowChecker::visit` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 3 | 12030:63 | `logos::compiler::BorrowChecker::visit` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 4 | 2704:17 | `logos::compiler::BorrowChecker::declared_pos` | loop exit | `continue;   // same word, different binding` |
| 4 | 2720:27 | `logos::compiler::BorrowChecker::note_binding_slot` | if guard | `if (name.empty()) return;` |
| 4 | 2898:28 | `logos::compiler::BorrowChecker::erase_ref_sources_under` | if guard | `if (place.empty()) return;` |
| 4 | 3134:13 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | switch arm | `case EC::TupleLit:` |
| 4 | 3136:34 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3136:43 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3136:72 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3702:37 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (path.empty() \|\| paths_overlap(path, p)) {` |
| 4 | 3854:52 | `logos::compiler::BorrowChecker::take_borrow_whole_` | statement | `!it->shared_field_borrows.empty()) {` |
| 4 | 4445:50 | `logos::compiler::BorrowChecker::path_overlaps` | early return | `return a.compare(0, b.size(), b) == 0 && a[b.size()] == '.';` |
| 4 | 4633:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | early return | `return "cannot move out of a value behind a shared reference (E0507)";` |
| 4 | 4713:9 | `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` | diagnostic emission | `report(line, std::format(` |
| 4 | 6497:21 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {{}, /*is_local=*/true};` |
| 4 | 7011:25 | `logos::compiler::BorrowChecker::prov_of` | statement | `ap.is_temp = true;` |
| 4 | 7458:25 | `logos::compiler::BorrowChecker::prov_of_retained` | statement | `merged = merge_prov(merged, it->second);` |
| 4 | 7459:26 | `logos::compiler::BorrowChecker::prov_of_retained` | else-if guard | `else if (force_local)` |
| 4 | 7459:30 | `logos::compiler::BorrowChecker::prov_of_retained` | else-if guard | `else if (force_local)` |
| 4 | 7567:47 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (is_return_temp_name(src)) {` |
| 4 | 7569:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (it != ret_temp_roots_.end())` |
| 4 | 9337:36 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (fit->is_mut && same(fit->holder, fit->holder_slot) &&` |
| 4 | 11239:70 | `logos::compiler::BorrowChecker::visit_stmt` | call / statement | `type_may_carry_borrow(v.value().type(pool))) {` |
| 4 | 11240:43 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `std::string cr0 = mrecv.kind() == EC::VarRef` |
| 4 | 11242:31 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `: flow_operand_root(mrecv);` |
| 4 | 11244:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11244:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11244:44 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11246:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11246:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11246:44 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11247:29 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `note_holder_escape_prov(cr, holder_ty_of(cr),` |
| 4 | 11362:45 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `v.each_guard([&](ExprRef g) { visit(g, /*consuming=*/true, ln); });` |
| 4 | 11949:21 | `logos::compiler::BorrowChecker::visit` | diagnostic emission | `report(line, std::format(` |
| 4 | 12140:43 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 5 | 3142:76 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 5 | 3586:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | call / statement | `arg_retained_by_callee(fs, i)))` |
| 5 | 3721:31 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (it->mut_borrowed) {` |
| 5 | 5517:65 | `logos::compiler::BorrowChecker::check_place_mut_use` | call / statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) {` |
| 5 | 5519:26 | `logos::compiler::BorrowChecker::check_place_mut_use` | diagnostic emission | `report(line, place.empty()` |
| 5 | 6531:21 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {{}, /*is_local=*/false, /*is_temp=*/true};` |
| 5 | 7614:42 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 5 | 7614:48 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 5 | 7675:35 | `logos::compiler::BorrowChecker::check_return_value` | statement | `src.empty() ? "?" : src));` |
| 5 | 9298:63 | `logos::compiler::BorrowChecker::release_borrows_held_by` | call / statement | `auto named_elsewhere = [&](const std::string& target) {` |
| 5 | 9299:58 | `logos::compiler::BorrowChecker::release_borrows_held_by` | loop body | `for (auto& [pl, srcs] : ref_borrow_sources_) {` |
| 5 | 9300:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (place_under(pl, holder_name)) continue;` |
| 5 | 9300:51 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (place_under(pl, holder_name)) continue;` |
| 5 | 9303:13 | `logos::compiler::BorrowChecker::release_borrows_held_by` | early return | `return false;` |
| 5 | 9328:47 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `it->co_holders.empty() && !named_elsewhere(it->target) &&` |
| 5 | 9329:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `!logos::probe::on("holderkill_keep")) {` |
| 5 | 9329:59 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `!logos::probe::on("holderkill_keep")) {` |
| 5 | 9330:75 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (auto sit = var_find(it->target_slot, it->target); sit != nullptr)` |
| 5 | 9331:25 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `sit->mut_borrowed = false;` |
| 5 | 9883:62 | `logos::compiler::BorrowChecker::ref_source_admissible` | if guard | `if (root.empty() \|\| is_materialized_temp_name(root)) return false;` |
| 5 | 10664:27 | `logos::compiler::BorrowChecker::visit_stmt` | call / statement | `t.kind() == LogosType::Kind::ZonedStruct))` |
| 5 | 11036:33 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln, std::format(` |
| 5 | 11948:24 | `logos::compiler::BorrowChecker::visit` | statement | `&& !param_names_.count(root))` |
| 6 | 746:13 | `borrow_check.cpp:logos::compiler::is_cond_move_field_drop_place` | switch arm | `case EK::TupleIndex:` |
| 6 | 1371:57 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | if guard | `if (bc_loan_carrying_type(ts_, TypeRef(e))) return true;` |
| 6 | 3750:65 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | statement | `!it->is_mut_binding && !param_names_.count(target)) {` |
| 6 | 3885:45 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (it->shared_borrows > 0) {` |
| 6 | 3887:25 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!scopes_.empty()) {` |
| 6 | 3887:43 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!scopes_.empty()) {` |
| 6 | 3892:29 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (in_top < it->shared_borrows)` |
| 6 | 3893:29 | `logos::compiler::BorrowChecker::take_borrow_whole_` | statement | `outer_shared = true;` |
| 6 | 3897:25 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (outer_shared) {` |
| 6 | 3897:39 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (outer_shared) {` |
| 6 | 4420:35 | `auto logos::compiler::BorrowChecker::inherit_loans` | if guard | `if (rec.holder == to) return;` |
| 6 | 4465:28 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 6 | 6486:32 | `logos::compiler::BorrowChecker::prov_of` | statement | `? RefProv{{name}, false}` |
| 6 | 6498:17 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {};` |
| 6 | 7879:25 | `logos::compiler::BorrowChecker::take_ref_borrows` | loop body | `for (auto& c : *caps)` |
| 6 | 7880:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7880:33 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7880:33 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7880:56 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7880:80 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 9289:35 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (h != holder_name) return false;` |
| 6 | 10452:36 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.is_mut) sit->mut_borrowed = false;` |
| 6 | 11584:52 | `logos::compiler::BorrowChecker::visit_stmt` | loop body | `for (auto& [k, d] : dangling_) acc_dang.emplace(k, d);` |
| 7 | 3867:43 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (it->mut_reservations > 0) {` |
| 7 | 4182:17 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4182:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4182:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4182:46 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4183:17 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (std::find(chased.begin(), chased.end(), r) == chased.end())` |
| 7 | 4183:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (std::find(chased.begin(), chased.end(), r) == chased.end())` |
| 7 | 4184:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | statement | `chased.push_back(r);` |
| 7 | 4333:36 | `logos::compiler::BorrowChecker::apply_flow_outparams` | loop body | `for (auto& h : chased) inherit_loans(dst, h, line);` |
| 7 | 4442:21 | `logos::compiler::BorrowChecker::path_overlaps` | if guard | `if (a == b) return true;` |
| 7 | 4460:60 | `logos::compiler::BorrowChecker::erase_reinit` | loop body | `for (auto it = moved.begin(); it != moved.end(); ) {` |
| 7 | 4461:28 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `bool covered = it->first == path \|\|` |
| 7 | 4465:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 7 | 4472:40 | `logos::compiler::BorrowChecker::consume` | if guard | `if (!it->moved_fields.empty()) {` |
| 7 | 4631:9 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4631:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4631:24 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4632:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 4632:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 4632:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 7899:35 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (fs && ix < fs->nparams && !(fs->to_result & (1ull << ix)))` |
| 7 | 7899:55 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (fs && ix < fs->nparams && !(fs->to_result & (1ull << ix)))` |
| 7 | 10440:51 | `logos::compiler::BorrowChecker::release_place_retarget` | call / statement | `auto take_one = [&](const std::string& t) {` |
| 7 | 10442:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (it == targets.end()) return false;` |
| 7 | 10443:13 | `logos::compiler::BorrowChecker::release_place_retarget` | statement | `targets.erase(it);` |
| 7 | 10447:58 | `logos::compiler::BorrowChecker::release_place_retarget` | loop body | `for (size_t i = frame.borrows.size(); i > 0; --i) {` |
| 7 | 10447:63 | `logos::compiler::BorrowChecker::release_place_retarget` | loop body | `for (size_t i = frame.borrows.size(); i > 0; --i) {` |
| 7 | 10449:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10449:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10449:42 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10450:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (!take_one(br.target)) continue;` |
| 7 | 10450:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (!take_one(br.target)) continue;` |
| 7 | 10451:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10451:69 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10451:85 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10452:25 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.is_mut) sit->mut_borrowed = false;` |
| 7 | 11032:33 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln, std::format(` |
| 7 | 12029:39 | `logos::compiler::BorrowChecker::visit` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live_addrof")) continue;` |
| 7 | 12029:84 | `logos::compiler::BorrowChecker::visit` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live_addrof")) continue;` |
| 8 | 2609:51 | `logos::compiler::BorrowChecker::pop_scope` | if guard | `if (bpos >= 0 && spos < bpos) continue; // binding drops first` |
| 8 | 3135:83 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | call / statement | `lir_view::ETupleLitView{a}.each_elem([&](lir_view::ExprRef inner) {` |
| 8 | 3136:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3136:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3136:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3583:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `is_borrow_carrying_type(a.type(pool)) \|\|` |
| 8 | 3584:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `forms_borrow_at_call(a) \|\|` |
| 8 | 3585:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 8 | 3585:36 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 8 | 4577:13 | `logos::compiler::BorrowChecker::deref_move_exempt` | early return | `return true;` |
| 8 | 4846:46 | `void logos::compiler::BorrowChecker::each_pat_binding` | statement | `v.each_suffix([&](PatRef sub){ each_pat_binding(sub, f); });` |
| 8 | 5196:35 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 8 | 5196:39 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 8 | 5752:46 | `logos::compiler::BorrowChecker::type_is_share_handle` | else-if guard | `else if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 8 | 6088:23 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (terminal) *terminal = cur;` |
| 8 | 6091:13 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | statement | `*temp_root = true;` |
| 8 | 7455:80 | `logos::compiler::BorrowChecker::prov_of_retained` | call / statement | `EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {` |
| 8 | 7457:50 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (auto it = prov_.find(n); it != prov_.end())` |
| 8 | 7534:40 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (src.empty() && !srcs.empty()) src = srcs.front();` |
| 8 | 7608:46 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (src.empty() && !is_temp) {` |
| 8 | 7613:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7613:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7613:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7614:25 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 8 | 7622:21 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 8 | 7622:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 8 | 8955:37 | `logos::compiler::BorrowChecker::prescan_fnptr` | else-if guard | `else if (it->second != sym) fnptr_multi_.insert(name);    // two callees` |
| 8 | 9290:13 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9290:17 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9290:17 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9290:37 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9290:52 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9328:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `it->co_holders.empty() && !named_elsewhere(it->target) &&` |
| 8 | 10656:25 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln,` |
| 8 | 11563:52 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!bg \|\| !bg->moved) guard_acc.at_id(slot, name) = st;` |
| 8 | 12140:61 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 9 | 3798:51 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 9 | 4198:45 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (reborrow_mut_.count(s)) subs.push_back(s);` |
| 9 | 4200:34 | `logos::compiler::BorrowChecker::apply_flow_outparams` | loop body | `for (auto& s : subs) reborrow_of_.each_root_place(s, chase);` |
| 9 | 6121:17 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | early return | `return rn;` |
| 9 | 6641:33 | `logos::compiler::BorrowChecker::prov_of` | statement | `merged = merge_prov(merged, prov_of(a));` |
| 9 | 9977:72 | `logos::compiler::BorrowChecker::ref_sources_of[abi:cxx11]` | statement | `reborrow_of_.each_under(p, [&](const std::string& sub) { add(sub); });` |
| 9 | 10699:43 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!sources.empty()) {` |
| 9 | 11349:25 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `bc_hop_roots(sc, roots);` |
| 9 | 12128:81 | `logos::compiler::BorrowChecker::visit` | if guard | `if (auto* hit = find_moved_overlap(it->moved_fields, path)) {` |
| 9 | 12140:29 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 9 | 12140:29 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |

## D. The hottest 30 regions, for contrast

| count | line:col | function | source |
|---:|---:|---|---|
| 425756949 | 203:54 | `borrow_check.cpp:logos::compiler::build_type_sets` | `auto type_bc_name = [](TypeRef t) -> std::string {` |
| 425756949 | 204:13 | `borrow_check.cpp:logos::compiler::build_type_sets` | `if (!t) return {};` |
| 425756949 | 205:9 | `borrow_check.cpp:logos::compiler::build_type_sets` | `auto k = t.kind();` |
| 425756949 | 206:13 | `borrow_check.cpp:logos::compiler::build_type_sets` | `if (k == LogosType::Kind::Enum) return std::string(t.enum_name());` |
| 412217593 | 207:9 | `borrow_check.cpp:logos::compiler::build_type_sets` | `if (k == LogosType::Kind::Struct \|\| k == LogosType::Kind::ZonedStruct)` |
| 412217593 | 207:13 | `borrow_check.cpp:logos::compiler::build_type_sets` | `if (k == LogosType::Kind::Struct \|\| k == LogosType::Kind::ZonedStruct)` |
| 412217593 | 207:13 | `borrow_check.cpp:logos::compiler::build_type_sets` | `if (k == LogosType::Kind::Struct \|\| k == LogosType::Kind::ZonedStruct)` |
| 392525587 | 814:36 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `static bool is_ref_kind(TypeRef t) {` |
| 392525587 | 829:12 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378609 | 829:17 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378609 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378609 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378609 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378609 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 365072703 | 830:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `t.kind() == LogosType::Kind::MutRef \|\|` |
| 346781301 | 831:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `t.kind() == LogosType::Kind::Slice \|\|` |
| 325631884 | 832:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) \|\|` |
| 325631884 | 832:19 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) \|\|` |
| 325627023 | 833:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::TraitObject &&` |
| 325627023 | 833:19 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::TraitObject &&` |
| 299203029 | 1359:67 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `static bool bc_loan_carrying_type(const TypeSets& ts_, TypeRef t) {` |
| 299203029 | 1360:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!t) return false;` |
| 299203029 | 1361:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `auto k = t.kind();` |
| 299203029 | 1363:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 299203029 | 1366:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!nm.empty() && ts_.loan_carrying.count(nm) > 0) return true;` |
| 299203029 | 1366:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!nm.empty() && ts_.loan_carrying.count(nm) > 0) return true;` |
| 299133200 | 1367:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `for (auto a : t.type_args())` |
| 299102349 | 1369:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Tuple) {` |
| 299102349 | 1369:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Tuple) {` |
| 296788163 | 1374:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Array \|\| k == LogosType::Kind::Slice)` |

## E. Where the probes are aimed

Every `logos::probe::on(...)` site in the TU with the execution count
of its ENCLOSING region — the number of times the probe's condition
was evaluated in this population. The probe's own body is 0 by
construction here: no probe was armed for the mapping run.

| arrivals | line | probe | enclosing function |
|---:|---:|---|---|
| 0 | 1093 | `rootkeep` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 0 | 2325 | `szw_snap_pre0` | `logos::compiler::BorrowChecker::loop_exit_snapshot` |
| 0 | 2328 | `szw_snap_keep` | `logos::compiler::BorrowChecker::loop_exit_snapshot` |
| 0 | 10507 | `szw_pwl_pre0` | `logos::compiler::BorrowChecker::place_write_loans` |
| 0 | 10510 | `szw_pwl_keep` | `logos::compiler::BorrowChecker::place_write_loans` |
| 10 | 7454 | `capescape` | `logos::compiler::BorrowChecker::prov_of_retained` |
| 13 | 3700 | `sharedzero_site_conflict` | `logos::compiler::BorrowChecker::field_borrow_conflicts` |
| 13 | 3701 | `sharedzero_live_conflict` | `logos::compiler::BorrowChecker::field_borrow_conflicts` |
| 17 | 7737 | `lifereg_aggtrust` | `logos::compiler::BorrowChecker::check_return_value` |
| 18 | 12028 | `sharedzero_site_addrof` | `logos::compiler::BorrowChecker::visit` |
| 18 | 12029 | `sharedzero_live_addrof` | `logos::compiler::BorrowChecker::visit` |
| 35 | 8701 | `capmove` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 82 | 8779 | `capscope` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 181 | 3797 | `szr_take_neg` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 194 | 2276 | `loopexit_coholder` | `auto logos::compiler::BorrowChecker::loop_exit_snapshot` |
| 194 | 10438 | `retarget_keep` | `logos::compiler::BorrowChecker::release_place_retarget` |
| 259 | 8723 | `capmut` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 259 | 8748 | `capshared` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 325 | 3791 | `sharedzero_site_take` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 325 | 3792 | `sharedzero_live_take` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 325 | 3796 | `szr_take_zero` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 785 | 1255 | `szw_merge_iter` | `borrow_check.cpp:logos::compiler::merge_loans` |
| 785 | 1256 | `szw_merge_srczero` | `borrow_check.cpp:logos::compiler::merge_loans` |
| 785 | 1260 | `szw_merge_fresh` | `borrow_check.cpp:logos::compiler::merge_loans` |
| 785 | 1262 | `szw_merge_leftzero` | `borrow_check.cpp:logos::compiler::merge_loans` |
| 1138 | 9329 | `holderkill_keep` | `logos::compiler::BorrowChecker::release_borrows_held_by` |
| 1655 | 9439 | `nll_lu_zero` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 1655 | 9457 | `fldnlldrop` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 1655 | 9459 | `nll_lu_strict` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 2347 | 8502 | `rcexempt` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 2397 | 8343 | `genautoref` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 2397 | 8363 | `genautorefx` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 6908 | 8427 | `mcallrefrecv` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 19193 | 11026 | `dwnoidx` | `logos::compiler::BorrowChecker::visit_stmt` |
| 21299 | 1077 | `rootkeep` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 25627 | 8285 | `genarg0blind` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 84202 | 9415 | `nll_lu_zero` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 84202 | 9421 | `nll_lu_strict` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 176974 | 2634 | `droporder` | `logos::compiler::BorrowChecker::pop_scope` |
| 575650 | 2571 | `szw_pop_pre0` | `logos::compiler::BorrowChecker::pop_scope` |
| 575650 | 2574 | `szw_pop_keep` | `logos::compiler::BorrowChecker::pop_scope` |
| 582848 | 3815 | `sharedzero_prod` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 582885 | 3786 | `sharedzero_reach` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 1362221 | 1148 | `ptrderef` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 1362221 | 1155 | `callroot` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 1447676 | 1028 | `sharedsticky` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 2165443 | 2522 | `rehome_all` | `auto logos::compiler::BorrowChecker::pop_scope` |
| 2213272 | 3970 | `selftest_inert` | `logos::compiler::BorrowChecker::record_borrow` |
| 2213272 | 3971 | `selftest_refuse` | `logos::compiler::BorrowChecker::record_borrow` |
| 2213272 | 3987 | `movedborrow` | `logos::compiler::BorrowChecker::record_borrow` |
| 3432831 | 12469 | `genrecvconflict` | `logos::compiler::BorrowChecker::visit` |
| 22842047 | 1194 | `refwhole` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |

