# borrow_check.cpp — region execution map

| | |
|---|---|
| taken | 2026-08-27 |
| subject | `/home/logos/devel/logos/src/compiler/borrow_check.cpp` |
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
| code regions in the TU | 5693 |
| never executed (count 0) | 563 (9.9%) |
| near-dead (1..9) | 257 |
| functions with regions here | 199 |
| functions never entered | 4 |

Every region is in `borrow_check_regions_2026-08-27.csv`, one row each, sorted by line and column.

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
| 1505 | 1 | `logos::compiler::RefGraph::erase` |
| 4616 | 1 | `logos::compiler::BorrowChecker::expr_ref` |
| 4620 | 1 | `logos::compiler::BorrowChecker::pat_ref` |

## B. Never-executed regions inside functions that DO run (559)

The interesting half: the function is live, this branch of it is not.
Grouped by enclosing function, ranked by how much of the function is
cold. Full list in the CSV; here, every function with at least one.

### `logos::compiler::BorrowChecker::visit_stmt` — 56 of 420 regions cold (hottest 43504858)

| line:col | what it is | source |
|---:|---|---|
| 10351:18 | if guard | `if (!sr) return;` |
| 10355:64 | if guard | `if (uint64_t pt = stmt_point(sr); pt > max_line_seen_) max_line_seen_ = pt;` |
| 10366:13 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10366:14 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10366:47 | statement | `(sr.kind() == Code::FieldWrite \|\| sr.kind() == Code::TupleWrite))` |
| 10367:13 | statement | `fprintf(stderr, "[bc-placewrite-door] fn=%s ln=%u kind=%d\n",` |
| 10367:21 | statement | `fprintf(stderr, "[bc-placewrite-door] fn=%s ln=%u kind=%d\n",` |
| 10490:29 | statement | `fprintf(stderr, "[#86trace-let] fn=%s line=%u var=%s "` |
| 10490:37 | statement | `fprintf(stderr, "[#86trace-let] fn=%s line=%u var=%s "` |
| 10596:70 | statement | `note_holder_escape_prov(name, val ? val.type(pool) : TypeRef(nullptr),` |
| 10598:60 | statement | `note_reborrow(name, val ? val.type(pool) : TypeRef(nullptr), val);  // H1` |
| 10655:13 | switch arm | `case Code::FieldWrite: {` |
| 10666:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10666:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10666:41 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10666:60 | if guard | `if (!recv_nm.empty() && !field_nm.empty()) {` |
| 10667:63 | if guard | `if (auto it = var_find(NO_SLOT, recv_nm); it != nullptr)` |
| 10670:25 | statement | `erase_reinit(it->moved_fields, field_nm);` |
| 10691:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10691:21 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10691:41 | if guard | `if (!recv_nm.empty() && !field_nm.empty())` |
| 10692:21 | statement | `note_reborrow_place(recv_nm + "." + field_nm, v.value());` |
| 10705:13 | switch arm | `case Code::IndexWrite: {` |
| 10708:54 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10708:69 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10709:25 | if guard | `if (it->shared_borrows > 0)` |
| 10710:25 | diagnostic emission | `report(ln, std::format(` |
| 10713:25 | if guard | `if (it->mut_borrowed)` |
| 10714:25 | diagnostic emission | `report(ln, std::format(` |
| 10730:13 | switch arm | `case Code::FieldIndexWrite: {` |
| 10733:54 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10733:69 | if guard | `if (auto it = var_find(NO_SLOT, nm); it != nullptr) {` |
| 10734:25 | if guard | `if (it->shared_borrows > 0)` |
| 10735:25 | diagnostic emission | `report(ln, std::format(` |
| 10738:25 | if guard | `if (it->mut_borrowed)` |
| 10739:25 | diagnostic emission | `report(ln, std::format(` |
| 10753:13 | switch arm | `case Code::ChainFieldWrite: {` |
| 10760:59 | call / statement | `v.each_extra([&](std::string_view ex) {` |
| 10761:29 | if guard | `if (!ex.empty()) cf_path += "." + std::string(ex);` |
| 10761:42 | if guard | `if (!ex.empty()) cf_path += "." + std::string(ex);` |
| 10763:25 | if guard | `if (!v.field().empty())` |
| 10764:25 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10764:37 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10764:55 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10764:60 | statement | `cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());` |
| 10773:13 | switch arm | `case Code::DerefFieldWrite: {` |
| 10926:50 | if guard | `if (eroot86.empty()) eroot86 = root;` |
| 10972:73 | statement | `TypeRef pt = atv.inner() ? atv.inner().type(pool) : TypeRef(nullptr);` |
| 11023:31 | statement | `? std::string(EVarRefView{mrecv}.name())` |
| 11027:29 | statement | `cr = ref_place_root(cr);` |
| 11086:68 | statement | `v.ptr() ? v.ptr().type(pool) : TypeRef{},` |
| 11094:13 | switch arm | `case Code::TupleWrite: {` |
| 11134:25 | statement | `roots.push_back(std::move(st));` |
| 11141:24 | else branch | `} else {` |
| 11364:88 | statement | `acc_rbl[k] = ref_borrow_line_.count(k) ? ref_borrow_line_[k] : ln;` |
| 11461:13 | switch arm | `default:` |

### `logos::compiler::BorrowChecker::take_ref_borrows` — 46 of 353 regions cold (hottest 7085691)

| line:col | what it is | source |
|---:|---|---|
| 7788:33 | if guard | `if (!a) return;` |
| 7792:40 | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 8176:30 | if guard | `if (blind && !tied_recv && !holder.empty() &&` |
| 8176:44 | if guard | `if (blind && !tied_recv && !holder.empty() &&` |
| 8177:21 | statement | `fn_index_.by_name.find(std::string(v.callee())) ==` |
| 8178:50 | call / statement | `fn_index_.by_name.end()) {` |
| 8180:46 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8180:52 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8180:63 | statement | `v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });` |
| 8181:25 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8181:25 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8181:31 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8181:62 | if guard | `if (b0 && b0.kind() == Code::AddrOfTemp) {` |
| 8184:39 | statement | `bool rawptr = bp.root_type &&` |
| 8185:29 | statement | `bp.root_type.kind() == LogosType::Kind::Ptr;` |
| 8186:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8186:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8186:29 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8186:49 | if guard | `if (!bp.root.empty() && !rawptr &&` |
| 8187:29 | call / statement | `var_has(bp.root_slot, bp.root))` |
| 8188:29 | statement | `record_borrow(bp, /*is_mut=*/false, line, holder,` |
| 8204:29 | if guard | `if (!a) return;` |
| 8249:66 | if guard | `if (logos::probe::on("genautorefx")) {` |
| 8250:47 | statement | `bool rawptr = pbp.root_type &&` |
| 8251:37 | statement | `pbp.root_type.kind() == LogosType::Kind::Ptr;` |
| 8252:37 | if guard | `if (!rawptr)` |
| 8253:37 | statement | `record_borrow(pbp,` |
| 8344:29 | statement | `rn = rn.substr(d + 1);` |
| 8346:29 | statement | `rn = rn.substr(0, g);` |
| 8359:24 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8359:28 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8359:28 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8359:36 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8359:66 | else-if guard | `} else if (recv && is_ref_kind(recv.type(pool))) {` |
| 8365:29 | if guard | `if (!a) return;` |
| 8435:25 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8435:29 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8435:43 | if guard | `if (!record_only) visit(g, /*consuming=*/true, line);  // #70` |
| 8546:29 | statement | `consume(std::string(cap), line);` |
| 8567:53 | if guard | `if (logos::probe::on("capmut")) is_mut = true;` |
| 8569:25 | statement | `std::fprintf(stderr,` |
| 8569:38 | statement | `std::fprintf(stderr,` |
| 8592:58 | if guard | `if (logos::probe::on("capshared") && shared_whole)` |
| 8593:25 | statement | `shared_whole = false;   // fall to record_borrow` |
| 8609:64 | statement | `: TypeRef(nullptr);` |
| 8624:45 | statement | `? std::string() : holder);` |

### `borrow_check.cpp:logos::compiler::extract_borrow_place` — 36 of 122 regions cold (hottest 24492115)

| line:col | what it is | source |
|---:|---|---|
| 1028:49 | if guard | `if (logos::probe::on("sharedsticky") && bp.through_ref_type &&` |
| 1029:13 | statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) return;` |
| 1029:65 | statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) return;` |
| 1079:63 | call / statement | `sl.type(pool).kind() == LogosType::Kind::Ptr) {` |
| 1080:21 | if guard | `if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }` |
| 1080:52 | if guard | `if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }` |
| 1129:51 | if guard | `if (logos::probe::on("ptrderef")) {` |
| 1137:20 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1137:21 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1137:21 | statement | `(cur.kind() == Code::MethodCall \|\|` |
| 1138:21 | statement | `cur.kind() == Code::Call \|\|` |
| 1139:21 | call / statement | `cur.kind() == Code::AddrOfTemp)) {` |
| 1139:54 | call / statement | `cur.kind() == Code::AddrOfTemp)) {` |
| 1146:17 | if guard | `if (cur.kind() == Code::AddrOfTemp)` |
| 1147:17 | statement | `nxt = EAddrOfTempView{cur}.inner();` |
| 1148:18 | else-if guard | `else if (cur.kind() == Code::MethodCall)` |
| 1148:22 | else-if guard | `else if (cur.kind() == Code::MethodCall)` |
| 1149:17 | statement | `nxt = EMethodCallView{cur}.receiver();` |
| 1151:17 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1151:56 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1151:62 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1151:68 | statement | `ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });` |
| 1152:20 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1152:20 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1152:27 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1152:28 | loop body | `while (nxt && (nxt.kind() == Code::AddrOfTemp \|\|` |
| 1153:28 | call / statement | `nxt.kind() == Code::SliceLit)) {` |
| 1153:59 | call / statement | `nxt.kind() == Code::SliceLit)) {` |
| 1154:23 | statement | `nxt = nxt.kind() == Code::AddrOfTemp` |
| 1155:27 | statement | `? EAddrOfTempView{nxt}.inner()` |
| 1156:27 | statement | `: ESliceLitView{nxt}.base();` |
| 1158:17 | if guard | `if (!nxt) break;` |
| 1158:23 | if guard | `if (!nxt) break;` |
| 1159:13 | statement | `path_parts.clear();` |
| 1175:45 | if guard | `if (logos::probe::on("refwhole") && bp.through_ref) path_parts.clear();` |
| 1175:61 | if guard | `if (logos::probe::on("refwhole") && bp.through_ref) path_parts.clear();` |

### `logos::compiler::borrow_check` — 35 of 46 regions cold (hottest 38403212)

| line:col | what it is | source |
|---:|---|---|
| 12564:13 | statement | `fprintf(stderr, "[flow-iters] fns=%zu rounds=%u max_body_passes=%u\n",` |
| 12564:21 | statement | `fprintf(stderr, "[flow-iters] fns=%zu rounds=%u max_body_passes=%u\n",` |
| 12566:63 | if guard | `if (const char* df = std::getenv("LOGOS_DUMP_FLOWS")) {` |
| 12568:41 | loop body | `for (auto& [nm, s] : flows) {` |
| 12569:21 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12569:21 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12569:36 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12569:72 | if guard | `if (filt != "1" && nm.find(filt) == std::string::npos) continue;` |
| 12570:17 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12570:21 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12570:35 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12570:45 | if guard | `if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }` |
| 12571:17 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12571:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12571:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12571:21 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12571:36 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12571:52 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12573:43 | early return | `return true; }()) continue;` |
| 12574:17 | statement | `fprintf(stderr, "[flow] %s: result<-%#llx", nm.c_str(),` |
| 12574:25 | statement | `fprintf(stderr, "[flow] %s: result<-%#llx", nm.c_str(),` |
| 12576:38 | loop body | `for (uint32_t j = 0; j < s.nparams; ++j)` |
| 12576:53 | loop body | `for (uint32_t j = 0; j < s.nparams; ++j)` |
| 12577:21 | if guard | `if (s.to_outparam[j])` |
| 12577:25 | if guard | `if (s.to_outparam[j])` |
| 12578:25 | statement | `fprintf(stderr, " out%u<-%#llx", j,` |
| 12578:33 | statement | `fprintf(stderr, " out%u<-%#llx", j,` |
| 12580:25 | statement | `fprintf(stderr, "%s  (rounds=%u)\n",` |
| 12581:25 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12581:41 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12581:52 | statement | `s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());` |
| 12673:9 | statement | `fprintf(stderr, "[bc-thruref] fired=%llu\n",` |
| 12673:17 | statement | `fprintf(stderr, "[bc-thruref] fired=%llu\n",` |
| 12678:9 | statement | `fprintf(stderr,` |
| 12678:17 | statement | `fprintf(stderr,` |

### `logos::compiler::BorrowChecker::place_write_loans` — 26 of 68 regions cold (hottest 18992)

| line:col | what it is | source |
|---:|---|---|
| 10269:35 | if guard | `if (root.empty() \|\| !val) return;` |
| 10283:39 | statement | `size_t nb = scopes_.empty() ? 0 : scopes_.back().borrows.size();` |
| 10284:39 | statement | `size_t nf = scopes_.empty() ? 0 : scopes_.back().field_borrows.size();` |
| 10299:17 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10299:68 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10299:83 | if guard | `if (auto it = var_find(br.target_slot, br.target); it != nullptr) {` |
| 10300:25 | if guard | `if (br.is_mut) it->mut_borrowed = false;` |
| 10300:36 | if guard | `if (br.is_mut) it->mut_borrowed = false;` |
| 10301:26 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10301:30 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10301:54 | else-if guard | `else if (it->shared_borrows > 0) --it->shared_borrows;` |
| 10305:62 | loop body | `for (size_t i = fr.field_borrows.size(); i > nf; --i) {` |
| 10305:67 | loop body | `for (size_t i = fr.field_borrows.size(); i > nf; --i) {` |
| 10307:21 | if guard | `if (fb.target != root) continue;` |
| 10307:40 | if guard | `if (fb.target != root) continue;` |
| 10308:17 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10308:68 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10308:83 | if guard | `if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {` |
| 10309:25 | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 10309:36 | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 10310:26 | else-if guard | `else if (auto sb = it->shared_field_borrows.find(fb.path);` |
| 10311:30 | call / statement | `sb != it->shared_field_borrows.end() && --sb->second <= 0)` |
| 10311:30 | call / statement | `sb != it->shared_field_borrows.end() && --sb->second <= 0)` |
| 10311:70 | call / statement | `sb != it->shared_field_borrows.end() && --sb->second <= 0)` |
| 10312:25 | statement | `it->shared_field_borrows.erase(sb);` |
| 10322:30 | if guard | `if (src == root) continue;` |

### `logos::compiler::BorrowChecker::release_dead_borrows` — 22 of 80 regions cold (hottest 87052132)

| line:col | what it is | source |
|---:|---|---|
| 9186:30 | if guard | `if (scopes_.empty()) return;` |
| 9187:51 | if guard | `if (std::getenv("LOGOS_DUMP_BC_RELEASE")) {` |
| 9188:26 | statement | `std::fprintf(stderr, "[bc-release] cur_line=%llu frames=%zu\n",` |
| 9190:33 | loop body | `for (size_t fi = 0; fi < scopes_.size(); ++fi)` |
| 9190:54 | loop body | `for (size_t fi = 0; fi < scopes_.size(); ++fi)` |
| 9191:17 | loop body | `for (auto& b : scopes_[fi].borrows)` |
| 9192:21 | statement | `std::fprintf(stderr,` |
| 9192:34 | statement | `std::fprintf(stderr,` |
| 9195:25 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9195:52 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9195:63 | statement | `fi + 1 == scopes_.size() ? "(back)" : "",` |
| 9241:37 | if guard | `if (it->holder.empty()) { ++it; continue; }` |
| 9247:61 | if guard | `if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++it; continue; }` |
| 9253:53 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9253:54 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9253:65 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9269:39 | if guard | `if (fit2->holder.empty()) { ++fit2; continue; }` |
| 9271:61 | if guard | `if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++fit2; continue; }` |
| 9272:53 | if guard | `if (holder_drops_after_last_use(*fit2)) { ++fit2; continue; }` |
| 9273:53 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9273:54 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |
| 9273:65 | if guard | `if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)` |

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

### `logos::compiler::BorrowChecker::pop_scope` — 16 of 87 regions cold (hottest 74125465)

| line:col | what it is | source |
|---:|---|---|
| 2381:30 | if guard | `if (scopes_.empty()) return;` |
| 2488:32 | if guard | `if (it == nullptr) continue;` |
| 2553:29 | if guard | `if (dorder) {` |
| 2554:65 | statement | `auto didx = [&](std::string_view n) -> long {` |
| 2555:44 | loop body | `for (size_t i = 0; i < frame.declared.size(); ++i)` |
| 2555:71 | loop body | `for (size_t i = 0; i < frame.declared.size(); ++i)` |
| 2556:29 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2556:33 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2556:57 | if guard | `if (frame.declared[i] == n) return (long)i;` |
| 2557:25 | early return | `return -1;` |
| 2560:25 | if guard | `if (bi >= 0)` |
| 2561:25 | loop body | `for (auto& src : sources)` |
| 2562:29 | if guard | `if (didx(src.name) > bi) {` |
| 2562:33 | if guard | `if (didx(src.name) > bi) {` |
| 2562:54 | if guard | `if (didx(src.name) > bi) {` |
| 2576:25 | diagnostic emission | `report(ref_borrow_line_[place], std::format(` |

### `logos::compiler::BorrowChecker::loop_exit_snapshot` — 13 of 43 regions cold (hottest 26280)

| line:col | what it is | source |
|---:|---|---|
| 2230:50 | if guard | `if (scopes_.size() <= outer_scope_count) return snap;` |
| 2235:43 | if guard | `if (!pending_esc_holder_.empty()) outer.insert(pending_esc_holder_);` |
| 2248:36 | if guard | `if (it == nullptr) continue;` |
| 2253:26 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2253:30 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2253:56 | else-if guard | `else if (it->mut_reservations > 0) it->mut_reservations--;` |
| 2258:34 | if guard | `if (escapes(fb)) continue;` |
| 2260:36 | if guard | `if (it == nullptr) continue;` |
| 2262:22 | else branch | `else {` |
| 2264:25 | if guard | `if (sit != it->shared_field_borrows.end() &&` |
| 2264:25 | if guard | `if (sit != it->shared_field_borrows.end() &&` |
| 2265:25 | statement | `--sit->second <= 0)` |
| 2266:25 | statement | `it->shared_field_borrows.erase(sit);` |

### `logos::compiler::BorrowChecker::collect_ref_sources_paths` — 12 of 233 regions cold (hottest 16397959)

| line:col | what it is | source |
|---:|---|---|
| 3040:21 | if guard | `if (!a) return false;` |
| 3068:13 | switch arm | `case EC::ArrLit:` |
| 3069:81 | call / statement | `lir_view::EArrLitView{a}.each_elem([&](lir_view::ExprRef inner) {` |
| 3070:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3070:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3070:25 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3070:34 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3070:43 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3070:72 | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3136:49 | statement | `fv, fname.empty() ? path : sub(std::string(fname)), out);` |
| 3379:13 | switch arm | `case EC::Try:` |
| 3406:55 | statement | `TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::record_borrow` — 12 of 40 regions cold (hottest 2213272)

| line:col | what it is | source |
|---:|---|---|
| 3861:50 | if guard | `if (logos::probe::on("selftest_refuse")) {` |
| 3866:30 | if guard | `if (bp.root.empty()) return;` |
| 3877:46 | if guard | `if (logos::probe::on("movedborrow")) {` |
| 3878:62 | if guard | `if (auto* mst = var_find(bp.root_slot, bp.root); mst != nullptr) {` |
| 3878:78 | if guard | `if (auto* mst = var_find(bp.root_slot, bp.root); mst != nullptr) {` |
| 3879:21 | if guard | `if (mst->moved) {` |
| 3879:33 | if guard | `if (mst->moved) {` |
| 3885:17 | if guard | `if (!bp.path.empty())` |
| 3885:21 | if guard | `if (!bp.path.empty())` |
| 3886:21 | if guard | `if (auto* hit = find_moved_overlap(mst->moved_fields,` |
| 3886:31 | if guard | `if (auto* hit = find_moved_overlap(mst->moved_fields,` |
| 3887:66 | call / statement | `bp.path)) {` |

### `logos::compiler::BorrowChecker::visit` — 12 of 273 regions cold (hottest 54027114)

| line:col | what it is | source |
|---:|---|---|
| 11632:13 | if guard | `if (!e) return;` |
| 11685:48 | if guard | `if (!it->is_mut_binding && !param_names_.count(vname))` |
| 11686:25 | diagnostic emission | `report(line, std::format(` |
| 12253:50 | statement | `!p0.owning_dst()) \|\| (gcf && p0_ref)) {` |
| 12256:32 | if guard | `if (gcf && a0 && a0.kind() == Code::AddrOfTemp)` |
| 12256:38 | if guard | `if (gcf && a0 && a0.kind() == Code::AddrOfTemp)` |
| 12257:25 | statement | `a0 = EAddrOfTempView{a0}.inner();` |
| 12261:55 | statement | `(gcf && p0.kind() == LogosType::Kind::MutRef),` |
| 12333:47 | statement | `TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 12439:20 | else branch | `} else {` |
| 12447:9 | switch arm | `case Code::Try:` |
| 12472:9 | switch arm | `case Code::FormatCall: {` |

### `logos::compiler::BorrowChecker::type_is_residency_backed` — 11 of 35 regions cold (hottest 12)

| line:col | what it is | source |
|---:|---|---|
| 5523:17 | if guard | `if (!t) return false;` |
| 5526:41 | if guard | `if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 5527:50 | else-if guard | `else if (k == LogosType::Kind::Struct \|\| k == LogosType::Kind::ZonedStruct)` |
| 5530:49 | if guard | `if (ts_.residency_exempt.count(nm)) return true;` |
| 5533:17 | statement | `bare = bare.substr(d + 1);` |
| 5535:17 | statement | `bare = bare.substr(0, g);` |
| 5536:64 | if guard | `if (ts_.residency_exempt.count(std::string(bare))) return true;` |
| 5539:25 | if guard | `if (depth <= 0) return false;` |
| 5541:13 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |
| 5541:17 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |
| 5541:66 | if guard | `if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;` |

### `logos::compiler::BorrowChecker::prov_of` — 11 of 279 regions cold (hottest 5723914)

| line:col | what it is | source |
|---:|---|---|
| 6368:21 | early return | `return {{}, /*is_local=*/false, /*is_temp=*/true};` |
| 6486:13 | switch arm | `case Code::SlicePtr:` |
| 6537:33 | if guard | `if (!a) return;` |
| 6548:71 | if guard | `if (auto it = prov_.find(cap); it != prov_.end()) {` |
| 6664:33 | if guard | `if (!a) return;` |
| 6678:21 | statement | `rp.is_temp = true;` |
| 6721:33 | statement | `fprintf(stderr, "[#86trace-carry] fn=%s loc=%d tmp=%d\n",` |
| 6721:41 | statement | `fprintf(stderr, "[#86trace-carry] fn=%s loc=%d tmp=%d\n",` |
| 6844:58 | statement | `TypeRef at0 = a ? a.type(pool) : TypeRef(nullptr);` |
| 6996:29 | if guard | `if (!a) return;` |
| 7000:44 | statement | `(elided_to >= 0 && (size_t)elided_to == fb_here))` |

### `logos::compiler::BorrowChecker::check_return_value` — 11 of 168 regions cold (hottest 20517297)

| line:col | what it is | source |
|---:|---|---|
| 7360:25 | if guard | `if (!ret_type_) return;` |
| 7379:48 | if guard | `if (std::getenv("LOGOS_DUMP_RETGATE")) {` |
| 7383:50 | statement | `std::string j; for (auto& s : srcs0) { j += s; j += ","; }` |
| 7384:21 | statement | `fprintf(stderr,` |
| 7392:13 | statement | `fprintf(stderr, "[#86trace-gate] fn=%s line=%u\n", fn_name_.c_str(), line);` |
| 7392:21 | statement | `fprintf(stderr, "[#86trace-gate] fn=%s line=%u\n", fn_name_.c_str(), line);` |
| 7424:55 | if guard | `if (src.empty() && !srcs.empty()) src = srcs.front();` |
| 7588:51 | if guard | `if (it == param_lifetimes_.end()) continue;` |
| 7604:33 | statement | `: outlives(src_lt, ret_lt, outlives_adj_,` |
| 7628:44 | statement | `? false` |
| 7651:53 | if guard | `if (!prov.params.count(sole_param)) {` |

### `logos::compiler::BorrowChecker::scan_uses_stmt` — 11 of 53 regions cold (hottest 86040831)

| line:col | what it is | source |
|---:|---|---|
| 8981:18 | if guard | `if (!sr) return;` |
| 9009:13 | switch arm | `case Code::FieldWrite: {` |
| 9012:21 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9012:21 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9012:59 | if guard | `if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())` |
| 9013:21 | statement | `prescan_reborrow_place(std::string(v.receiver()) + "." +` |
| 9018:13 | switch arm | `case Code::IndexWrite: {` |
| 9025:13 | switch arm | `case Code::FieldIndexWrite: {` |
| 9032:13 | switch arm | `case Code::ChainFieldWrite: {` |
| 9038:13 | switch arm | `case Code::DerefFieldWrite: {` |
| 9050:13 | switch arm | `case Code::TupleWrite: {` |

### `logos::compiler::BorrowChecker::release_borrows_held_by` — 10 of 48 regions cold (hottest 44753)

| line:col | what it is | source |
|---:|---|---|
| 9135:13 | early return | `return true;` |
| 9145:17 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9145:38 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9145:42 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9145:60 | loop body | `for (auto& s : srcs) if (s.name == target) return true;` |
| 9176:21 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9176:48 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9176:79 | call / statement | `fit->co_holders.empty() && !named_elsewhere(fit->target)) {` |
| 9177:77 | if guard | `if (auto sit = var_find(fit->target_slot, fit->target); sit != nullptr)` |
| 9178:25 | statement | `sit->mut_field_borrows.erase(fit->path);` |

### `logos::compiler::BorrowChecker::retain_operand_loans` — 9 of 29 regions cold (hottest 4190)

| line:col | what it is | source |
|---:|---|---|
| 5826:35 | if guard | `if (!e \|\| holder.empty()) return;` |
| 5829:22 | if guard | `if (!op) return;` |
| 5831:22 | if guard | `if (!ot) return;` |
| 5844:13 | switch arm | `case Code::StructLit:  EStructLitView{e}.each_field_value(one); break;` |
| 5845:13 | switch arm | `case Code::TupleLit:   ETupleLitView{e}.each_elem(one);         break;` |
| 5846:13 | switch arm | `case Code::ArrLit:     EArrLitView{e}.each_elem(one);           break;` |
| 5847:13 | switch arm | `case Code::EnumLitData:EEnumLitDataView{e}.each_payload(one);   break;` |
| 5848:13 | switch arm | `case Code::Cast:       one(ECastView{e}.operand());             break;` |
| 5849:13 | switch arm | `default: break;` |

### `logos::compiler::BorrowChecker::carried_prov_of_recv` — 9 of 34 regions cold (hottest 5111)

| line:col | what it is | source |
|---:|---|---|
| 6317:17 | if guard | `if (!r) return {};` |
| 6318:39 | if guard | `if (r.kind() == Code::AddrOf) {` |
| 6320:17 | if guard | `if (param_names_.count(nm)) return {{nm}, false};` |
| 6320:41 | if guard | `if (param_names_.count(nm)) return {{nm}, false};` |
| 6321:13 | statement | `auto it = prov_.find(nm);` |
| 6322:20 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6322:40 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6322:53 | early return | `return it != prov_.end() ? it->second : RefProv{};` |
| 6338:54 | statement | `: RefProv{{}, /*is_local=*/true};` |

### `borrow_check.cpp:logos::compiler::borrow_check` — 8 of 37 regions cold (hottest 73693199)

| line:col | what it is | source |
|---:|---|---|
| 12571:55 | if guard | `if (filt == "1" && !s.to_result && [&]{` |
| 12572:54 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12572:58 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12572:61 | loop body | `for (auto m : s.to_outparam) if (m) return false;` |
| 12573:25 | early return | `return true; }()) continue;` |
| 12619:13 | statement | `ri.dump(std::string(bare_fn_name(fn.name())));` |
| 12630:17 | statement | `std::swap(first, second);` |
| 12634:52 | if guard | `if (target_label.starts_with("<temp")) target_label = "temporary";` |

### `logos::compiler::BorrowChecker::take_borrow_whole_` — 6 of 60 regions cold (hottest 1630365)

| line:col | what it is | source |
|---:|---|---|
| 3781:29 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3781:33 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3781:33 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3781:56 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3781:68 | if guard | `if (br.target == target && !br.is_mut) ++in_top;` |
| 3784:28 | else branch | `} else {` |

### `void logos::compiler::BorrowChecker::each_pat_binding` — 6 of 30 regions cold (hottest 7301087)

| line:col | what it is | source |
|---:|---|---|
| 4694:18 | if guard | `if (!pr) return;` |
| 4729:13 | switch arm | `case PC::Or:` |
| 4730:55 | statement | `PatOrView{pr}.each_alt([&](PatRef alt){ each_pat_binding(alt, f); });` |
| 4739:13 | switch arm | `case PC::At: {` |
| 4742:26 | if guard | `if (auto sub = v.sub()) each_pat_binding(sub, f);` |
| 4742:41 | if guard | `if (auto sub = v.sub()) each_pat_binding(sub, f);` |

### `logos::compiler::BorrowChecker::residency_exemption_holds` — 6 of 28 regions cold (hottest 20678053)

| line:col | what it is | source |
|---:|---|---|
| 5554:17 | if guard | `if (!e) return true;` |
| 5558:73 | if guard | `if (is_return_temp_name(n) \|\| is_materialized_temp_name(n)) continue;` |
| 5561:21 | statement | `fprintf(stderr, "[#86trace-exempt-denied] fn=%s src=%s\n",` |
| 5561:29 | statement | `fprintf(stderr, "[#86trace-exempt-denied] fn=%s src=%s\n",` |
| 5622:21 | statement | `fprintf(stderr, "[#86trace-exempt-multishare] fn=%s n=%d\n",` |
| 5622:29 | statement | `fprintf(stderr, "[#86trace-exempt-multishare] fn=%s n=%d\n",` |

### `logos::compiler::BorrowChecker::drop_can_observe_borrow` — 5 of 38 regions cold (hottest 108218)

| line:col | what it is | source |
|---:|---|---|
| 2694:22 | if guard | `if (!sd) return false;` |
| 2697:26 | if guard | `if (!ft) continue;` |
| 2701:61 | if guard | `if (drop_can_observe_borrow(ft, depth + 1)) return true;` |
| 2708:46 | if guard | `if (pit != ts_.spec_by_name.end() && reaches_ref(pit->second)) return true;` |
| 2708:72 | if guard | `if (pit != ts_.spec_by_name.end() && reaches_ref(pit->second)) return true;` |

### `logos::compiler::BorrowChecker::collect_borrow_locals` — 5 of 16 regions cold (hottest 58)

| line:col | what it is | source |
|---:|---|---|
| 2751:17 | if guard | `if (!e) return;` |
| 2762:13 | switch arm | `case EC::AddrOfTemp:` |
| 2769:13 | switch arm | `case EC::TupleLit:` |
| 2771:47 | statement | `[&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });` |
| 2773:13 | switch arm | `case EC::Cast:` |

### `logos::compiler::BorrowChecker::propagate_pat_reborrows` — 5 of 57 regions cold (hottest 21179072)

| line:col | what it is | source |
|---:|---|---|
| 5046:28 | if guard | `if (!pr \|\| !scrut) return;` |
| 5077:34 | if guard | `if (src.empty()) return;` |
| 5086:55 | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 5096:37 | if guard | `if (n == place) return;` |
| 5118:28 | if guard | `if (s.empty()) continue;` |

### `logos::compiler::BorrowChecker::retains_loan_carrying_operand` — 5 of 26 regions cold (hottest 3340866)

| line:col | what it is | source |
|---:|---|---|
| 5778:17 | if guard | `if (!e) return false;` |
| 5795:13 | switch arm | `case Code::StructLit:` |
| 5797:13 | switch arm | `case Code::TupleLit:  ETupleLitView{e}.each_elem(by_value_bc); break;` |
| 5798:13 | switch arm | `case Code::ArrLit:    EArrLitView{e}.each_elem(by_value_bc);  break;` |
| 5799:13 | switch arm | `case Code::EnumLitData:` |

### `logos::compiler::BorrowChecker::type_hides_borrow_` — 5 of 65 regions cold (hottest 57462959)

| line:col | what it is | source |
|---:|---|---|
| 7104:17 | if guard | `if (!t) return false;` |
| 7107:21 | if guard | `if (!a) return false;` |
| 7138:26 | if guard | `if (!sd) return false;` |
| 7148:50 | if guard | `if (pit != ts_.spec_by_name.end() && walk(pit->second)) return true;` |
| 7148:69 | if guard | `if (pit != ts_.spec_by_name.end() && walk(pit->second)) return true;` |

### `borrow_check.cpp:logos::compiler::has_droppable_fields` — 4 of 24 regions cold (hottest 7837238)

| line:col | what it is | source |
|---:|---|---|
| 462:52 | if guard | `if (!t \|\| t.kind() != LogosType::Kind::Struct) return false;` |
| 472:18 | if guard | `if (!sd) return false;` |
| 480:41 | if guard | `if (pit != ts.spec_by_name.end() && def_has_drop(pit->second)) return true;` |
| 480:68 | if guard | `if (pit != ts.spec_by_name.end() && def_has_drop(pit->second)) return true;` |

### `borrow_check.cpp:logos::compiler::ref_source_places` — 4 of 76 regions cold (hottest 28310575)

| line:col | what it is | source |
|---:|---|---|
| 1689:9 | switch arm | `case Code::Try:` |
| 1733:31 | if guard | `if (is_rawptr(r)) return;` |
| 1759:31 | if guard | `if (is_rawptr(s)) return;` |
| 1771:20 | if guard | `if (p.empty()) return;` |

### `logos::compiler::BorrowChecker::deref_type_of_` — 4 of 14 regions cold (hottest 2952)

| line:col | what it is | source |
|---:|---|---|
| 4490:40 | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 4491:18 | if guard | `if (!ot) return TypeRef(nullptr);` |
| 4494:13 | call / statement | `ot.kind() == LogosType::Kind::Ptr)` |
| 4496:9 | early return | `return TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` — 4 of 31 regions cold (hottest 10596830)

| line:col | what it is | source |
|---:|---|---|
| 4582:13 | statement | `cur = lir_view::ECastView{cur}.operand();` |
| 4583:19 | if guard | `if (!cur) return;` |
| 4592:17 | statement | `cur = lir_view::ECastView{cur}.operand();` |
| 4597:25 | if guard | `if (nm.empty()) return;` |

### `logos::compiler::BorrowChecker::check_place_mut_use` — 4 of 29 regions cold (hottest 7519)

| line:col | what it is | source |
|---:|---|---|
| 5419:30 | if guard | `if (bp.root.empty()) return;` |
| 5421:29 | if guard | `if (sit == nullptr) return;` |
| 5427:32 | if guard | `if (sit->mut_borrowed) return;` |
| 5428:38 | if guard | `if (sit->shared_borrows > 0) {` |

### `logos::compiler::BorrowChecker::note_holder_escape_prov` — 4 of 47 regions cold (hottest 679485)

| line:col | what it is | source |
|---:|---|---|
| 5685:35 | if guard | `if (name.empty() \|\| !val) return;` |
| 5694:56 | if guard | `if (residency_exemption_holds(holder_ty, val)) return;` |
| 5706:13 | statement | `fprintf(stderr, "[#86trace-%s] fn=%s line=%u var=%s loc=%d tmp=%d\n",` |
| 5706:21 | statement | `fprintf(stderr, "[#86trace-%s] fn=%s line=%u var=%s loc=%d tmp=%d\n",` |

### `logos::compiler::BorrowChecker::prov_of_retained` — 4 of 63 regions cold (hottest 3877805)

| line:col | what it is | source |
|---:|---|---|
| 7258:17 | if guard | `if (!e) return merged;` |
| 7261:21 | if guard | `if (!a) return;` |
| 7303:29 | statement | `one(a);` |
| 7350:25 | statement | `merged.is_local = true;   // capture of a plain local` |

### `logos::compiler::BorrowChecker::release_place_retarget` — 4 of 32 regions cold (hottest 219)

| line:col | what it is | source |
|---:|---|---|
| 10246:48 | if guard | `if (logos::probe::on("retarget_keep")) return;` |
| 10250:38 | if guard | `if (it == targets.end()) return false;` |
| 10257:66 | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 10258:43 | if guard | `if (!take_one(br.target)) continue;` |

### `logos::compiler::BorrowChecker::propagate_pat_borrows` — 3 of 29 regions cold (hottest 21178992)

| line:col | what it is | source |
|---:|---|---|
| 4990:28 | if guard | `if (!pr \|\| !scrut) return;` |
| 5011:55 | if guard | `if (place.size() < base_place.size()) return;` |
| 5016:35 | statement | `: base.path + place.substr(base_place.size());` |

### `logos::compiler::BorrowChecker::type_is_share_handle` — 3 of 20 regions cold (hottest 649)

| line:col | what it is | source |
|---:|---|---|
| 5637:17 | if guard | `if (!t) return false;` |
| 5646:13 | statement | `bare = bare.substr(d + 1);` |
| 5648:13 | statement | `bare = bare.substr(0, g);` |

### `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` — 3 of 82 regions cold (hottest 22895)

| line:col | what it is | source |
|---:|---|---|
| 5951:40 | if guard | `if (recv_is_rawptr(r)) return {};` |
| 5956:40 | if guard | `if (recv_is_rawptr(r)) return {};` |
| 5973:39 | if guard | `if (k == Code::SlicePtr)  { cur = ESlicePtrView{cur}.slice(); continue; }` |

### `logos::compiler::BorrowChecker::scan_uses_expr` — 3 of 52 regions cold (hottest 115304796)

| line:col | what it is | source |
|---:|---|---|
| 8856:13 | switch arm | `case Code::Try:` |
| 8938:13 | switch arm | `case Code::FormatCall: {` |
| 8941:42 | statement | `v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });` |

### `logos::compiler::BorrowChecker::ref_sources_of[abi:cxx11]` — 3 of 42 regions cold (hottest 262100)

| line:col | what it is | source |
|---:|---|---|
| 9779:21 | if guard | `if (!a) return;` |
| 9828:13 | switch arm | `case Code::Try:` |
| 9830:21 | statement | `add(std::move(p));` |

### `borrow_check.cpp:logos::compiler::BorrowChecker::visit` — 3 of 69 regions cold (hottest 5287746)

| line:col | what it is | source |
|---:|---|---|
| 11882:53 | statement | `TypeRef rt = r ? r.type(pool) : TypeRef(nullptr);` |
| 12042:33 | if guard | `if (!a) return;` |
| 12191:37 | if guard | `if (!a) return;` |

### `borrow_check.cpp:logos::compiler::is_cond_move_field_drop_place` — 2 of 8 regions cold (hottest 231)

| line:col | what it is | source |
|---:|---|---|
| 752:13 | switch arm | `default:` |
| 756:5 | early return | `return false;` |

### `borrow_check.cpp:logos::compiler::merge_loans` — 2 of 15 regions cold (hottest 3363607)

| line:col | what it is | source |
|---:|---|---|
| 1224:55 | if guard | `if (st.mut_reservations > b.mut_reservations) b.mut_reservations = st.mut_reservations;` |
| 1228:26 | if guard | `if (n > cur) cur = n;` |

### `void logos::compiler::RefGraph::each_root_place` — 2 of 24 regions cold (hottest 39070037)

| line:col | what it is | source |
|---:|---|---|
| 1559:28 | if guard | `if (start.empty()) return;` |
| 1565:71 | if guard | `if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;` |

### `logos::compiler::BorrowChecker::loop_target` — 2 of 13 regions cold (hottest 5706)

| line:col | what it is | source |
|---:|---|---|
| 2192:34 | if guard | `if (loop_stack_.empty()) return nullptr;` |
| 2196:9 | early return | `return &loop_stack_.back();` |

### `auto logos::compiler::BorrowChecker::loop_exit_snapshot` — 2 of 11 regions cold (hottest 194)

| line:col | what it is | source |
|---:|---|---|
| 2237:37 | if guard | `if (rec.holder.empty()) return false;   // lexical: dies at pop` |
| 2240:37 | if guard | `if (outer.count(h)) return true;` |

### `logos::compiler::BorrowChecker::walk_closure_body` — 2 of 17 regions cold (hottest 674)

| line:col | what it is | source |
|---:|---|---|
| 2347:19 | if guard | `if (!cbb) return;` |
| 2359:29 | if guard | `if (pn.empty()) return;` |

### `logos::compiler::BorrowChecker::struct_is_dropck_relevant` — 2 of 22 regions cold (hottest 4484232)

| line:col | what it is | source |
|---:|---|---|
| 2665:46 | if guard | `if (pit != ts_.spec_by_name.end() && has_lt(pit->second)) return true;` |
| 2665:67 | if guard | `if (pit != ts_.spec_by_name.end() && has_lt(pit->second)) return true;` |

### `logos::compiler::BorrowChecker::take_field_borrow_path_` — 2 of 52 regions cold (hottest 582893)

| line:col | what it is | source |
|---:|---|---|
| 3639:47 | if guard | `if (is_mut && it->shared_borrows > 0) {` |
| 3654:43 | if guard | `if (is_mut && root_is_shared_ref) {` |

### `logos::compiler::BorrowChecker::apply_call_outparam_rules` — 2 of 37 regions cold (hottest 7118170)

| line:col | what it is | source |
|---:|---|---|
| 3990:21 | if guard | `if (!a) continue;` |
| 4016:25 | if guard | `if (!a) continue;` |

### `logos::compiler::BorrowChecker::apply_flow_outparams` — 2 of 66 regions cold (hottest 6961776)

| line:col | what it is | source |
|---:|---|---|
| 4072:68 | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 4094:27 | if guard | `if (!src) continue;` |

### `logos::compiler::BorrowChecker::erase_reinit` — 2 of 12 regions cold (hottest 12402)

| line:col | what it is | source |
|---:|---|---|
| 4353:18 | statement | `it->first.compare(0, path.size(), path) == 0 &&` |
| 4354:18 | statement | `it->first[path.size()] == '.');` |

### `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` — 2 of 23 regions cold (hottest 18)

| line:col | what it is | source |
|---:|---|---|
| 4521:40 | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 4526:9 | early return | `return "cannot move out of a dereference (E0507)";` |

### `logos::compiler::BorrowChecker::declare_pat_bindings` — 2 of 16 regions cold (hottest 21194302)

| line:col | what it is | source |
|---:|---|---|
| 4630:18 | if guard | `if (!pr) return;` |
| 4652:49 | call / statement | `void declare_pat_bindings(const Pattern& p) {` |

### `void logos::compiler::BorrowChecker::each_pat_binding_place` — 2 of 39 regions cold (hottest 42285618)

| line:col | what it is | source |
|---:|---|---|
| 4870:34 | if guard | `if (!pr \|\| base.empty()) return;` |
| 4917:42 | statement | `? base : sub(std::string(fb.field_name()));` |

### `logos::compiler::BorrowChecker::method_self_kind` — 2 of 38 regions cold (hottest 16065572)

| line:col | what it is | source |
|---:|---|---|
| 5243:45 | statement | `auto kind0 = f_params.empty() ? LogosType::Kind::Void` |
| 5254:31 | if guard | `if (f_params.empty()) return 0;` |

### `logos::compiler::BorrowChecker::is_reborrow_store_value` — 2 of 16 regions cold (hottest 5527718)

| line:col | what it is | source |
|---:|---|---|
| 9891:44 | if guard | `if (k != LogosType::Kind::Array && k != LogosType::Kind::Slice) return false;` |
| 9891:73 | if guard | `if (k != LogosType::Kind::Array && k != LogosType::Kind::Slice) return false;` |

### `logos::compiler::BorrowChecker::note_reborrow_place` — 2 of 14 regions cold (hottest 1043371)

| line:col | what it is | source |
|---:|---|---|
| 10029:28 | if guard | `if (place.empty()) return;` |
| 10031:45 | statement | `TypeRef vt = val ? val.type(pool) : TypeRef(nullptr);` |

### `logos::compiler::BorrowChecker::resolve_ref_places` — 2 of 21 regions cold (hottest 5327293)

| line:col | what it is | source |
|---:|---|---|
| 10105:27 | if guard | `if (base.empty()) return;` |
| 10135:31 | if guard | `if (next.empty()) break;` |

### `auto logos::compiler::BorrowChecker::place_write_loans` — 2 of 15 regions cold (hottest 34)

| line:col | what it is | source |
|---:|---|---|
| 10335:40 | if guard | `if (rec.holder == src) return;` |
| 10337:46 | statement | `!= rec.co_holders.end()) return;` |

### `borrow_check.cpp:logos::compiler::is_move_type` — 1 of 34 regions cold (hottest 16499743)

| line:col | what it is | source |
|---:|---|---|
| 525:38 | if guard | `if (ts.drop_types.count(en)) return true;       // has a Drop impl` |

### `borrow_check.cpp:logos::compiler::is_temporary_value_expr` — 1 of 20 regions cold (hottest 10622044)

| line:col | what it is | source |
|---:|---|---|
| 781:13 | if guard | `if (!e) return false;` |

### `borrow_check.cpp:logos::compiler::bc_is_borrow_carrying_type` — 1 of 37 regions cold (hottest 270211695)

| line:col | what it is | source |
|---:|---|---|
| 1242:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` — 1 of 32 regions cold (hottest 299203029)

| line:col | what it is | source |
|---:|---|---|
| 1326:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_holds_mut_ref_type` — 1 of 35 regions cold (hottest 192593174)

| line:col | what it is | source |
|---:|---|---|
| 1348:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::bc_holds_any_ref_type` — 1 of 35 regions cold (hottest 201304300)

| line:col | what it is | source |
|---:|---|---|
| 1389:13 | if guard | `if (!t) return false;` |

### `borrow_check.cpp:logos::compiler::build_fn_index` — 1 of 12 regions cold (hottest 73693199)

| line:col | what it is | source |
|---:|---|---|
| 1428:17 | if guard | `if (!f) return;` |

### `void logos::compiler::RefGraph::each_under` — 1 of 9 regions cold (hottest 22515)

| line:col | what it is | source |
|---:|---|---|
| 1511:27 | if guard | `if (root.empty()) return;` |

### `void logos::compiler::RefGraph::each_root` — 1 of 15 regions cold (hottest 7672898)

| line:col | what it is | source |
|---:|---|---|
| 1532:28 | if guard | `if (start.empty()) return;` |

### `borrow_check.cpp:void logos::compiler::each_ref_store` — 1 of 21 regions cold (hottest 32296140)

| line:col | what it is | source |
|---:|---|---|
| 1802:44 | if guard | `if (dest.empty() \|\| !val \|\| depth > 8) return;` |

### `logos::compiler::BorrowChecker::var_find` — 1 of 2 regions cold (hottest 82611923)

| line:col | what it is | source |
|---:|---|---|
| 1885:74 | statement | `const VarState* var_find(uint32_t slot, std::string_view name) const {` |

### `logos::compiler::BorrowChecker::stmt_point` — 1 of 9 regions cold (hottest 173050547)

| line:col | what it is | source |
|---:|---|---|
| 2150:18 | if guard | `if (!sr) return 0;` |

### `auto logos::compiler::BorrowChecker::pop_scope` — 1 of 20 regions cold (hottest 126299947)

| line:col | what it is | source |
|---:|---|---|
| 2450:57 | if guard | `if (logos::probe::on("rehome_all")) return true;` |

### `bool logos::compiler::BorrowChecker::holder_drops_after_last_use` — 1 of 8 regions cold (hottest 85857)

| line:col | what it is | source |
|---:|---|---|
| 2717:60 | if guard | `if (drop_can_observe_borrow(holder_ty_of(co))) return true;` |

### `logos::compiler::BorrowChecker::ref_sources_under` — 1 of 11 regions cold (hottest 12482565)

| line:col | what it is | source |
|---:|---|---|
| 2804:27 | if guard | `if (root.empty()) return out;` |

### `logos::compiler::BorrowChecker::add_ref_sources` — 1 of 6 regions cold (hottest 2739)

| line:col | what it is | source |
|---:|---|---|
| 2851:35 | if guard | `if (name.empty() \|\| !val) return;` |

### `logos::compiler::BorrowChecker::flow_operand_root[abi:cxx11]` — 1 of 10 regions cold (hottest 214608)

| line:col | what it is | source |
|---:|---|---|
| 3968:17 | if guard | `if (!a) return {};` |

### `logos::compiler::BorrowChecker::holders_last_use` — 1 of 7 regions cold (hottest 103562)

| line:col | what it is | source |
|---:|---|---|
| 4258:60 | statement | `co[i], i < co_slots.size() ? co_slots[i] : NO_SLOT));` |

### `logos::compiler::BorrowChecker::is_loan_holder` — 1 of 14 regions cold (hottest 4820)

| line:col | what it is | source |
|---:|---|---|
| 4295:49 | statement | `!= fb.co_holders.end()) return true;` |

### `logos::compiler::BorrowChecker::consume` — 1 of 23 regions cold (hottest 366134)

| line:col | what it is | source |
|---:|---|---|
| 4375:17 | diagnostic emission | `report(line, std::format("use of moved value '{}'", name));` |

### `logos::compiler::BorrowChecker::deref_move_exempt` — 1 of 22 regions cold (hottest 9995)

| line:col | what it is | source |
|---:|---|---|
| 4430:18 | if guard | `if (!op) return true;` |

### `logos::compiler::BorrowChecker::check_live` — 1 of 13 regions cold (hottest 30052639)

| line:col | what it is | source |
|---:|---|---|
| 4546:17 | diagnostic emission | `report(line, std::format("use of moved value '{}'", name));` |

### `auto void logos::compiler::BorrowChecker::each_pat_binding` — 1 of 13 regions cold (hottest 8630002)

| line:col | what it is | source |
|---:|---|---|
| 4702:68 | statement | `f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr));` |

### `logos::compiler::BorrowChecker::propagate_pat_prov` — 1 of 18 regions cold (hottest 21179072)

| line:col | what it is | source |
|---:|---|---|
| 4796:28 | if guard | `if (!pr \|\| !scrut) return;` |

### `auto void logos::compiler::BorrowChecker::each_pat_binding_place` — 1 of 16 regions cold (hottest 56529478)

| line:col | what it is | source |
|---:|---|---|
| 4880:68 | statement | `f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr),` |

### `logos::compiler::BorrowChecker::is_self_borrowing` — 1 of 26 regions cold (hottest 148417)

| line:col | what it is | source |
|---:|---|---|
| 5189:17 | if guard | `if (!f) return false;` |

### `logos::compiler::BorrowChecker::check_recv_conflict` — 1 of 33 regions cold (hottest 111266)

| line:col | what it is | source |
|---:|---|---|
| 5305:13 | diagnostic emission | `report(line, std::format(` |

### `logos::compiler::BorrowChecker::type_is_residency_exempt` — 1 of 13 regions cold (hottest 20678053)

| line:col | what it is | source |
|---:|---|---|
| 5473:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::type_retains_values` — 1 of 21 regions cold (hottest 3627399)

| line:col | what it is | source |
|---:|---|---|
| 5763:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::type_may_carry_borrow` — 1 of 36 regions cold (hottest 69075422)

| line:col | what it is | source |
|---:|---|---|
| 5869:17 | if guard | `if (!t) return false;` |

### `logos::compiler::BorrowChecker::bc_hop_roots` — 1 of 158 regions cold (hottest 7705282)

| line:col | what it is | source |
|---:|---|---|
| 6245:40 | if guard | `if (recv_is_rawptr(r)) return;` |

### `logos::compiler::BorrowChecker::retains_borrowing_operand` — 1 of 34 regions cold (hottest 286533)

| line:col | what it is | source |
|---:|---|---|
| 7220:17 | if guard | `if (!e) return false;` |

### `logos::compiler::BorrowChecker::prescan_reborrow_place` — 1 of 9 regions cold (hottest 2051650)

| line:col | what it is | source |
|---:|---|---|
| 8725:36 | if guard | `if (place.empty() \|\| !val) return;` |

### `logos::compiler::BorrowChecker::note_use_slot` — 1 of 13 regions cold (hottest 63423859)

| line:col | what it is | source |
|---:|---|---|
| 8818:27 | if guard | `if (name.empty()) return;` |

### `logos::compiler::BorrowChecker::visit_block` — 1 of 18 regions cold (hottest 33929705)

| line:col | what it is | source |
|---:|---|---|
| 9384:56 | diagnostic emission | `report(cursor ? (uint32_t)cursor : 0, std::format(` |

### `logos::compiler::BorrowChecker::visit_loop_body` — 1 of 15 regions cold (hottest 107982)

| line:col | what it is | source |
|---:|---|---|
| 9418:44 | loop body | `for (auto& r : var_loan_roots) inherit_loans(r, loop_vars.front(), 0);` |

### `logos::compiler::BorrowChecker::place_write_root[abi:cxx11]` — 1 of 41 regions cold (hottest 2785486)

| line:col | what it is | source |
|---:|---|---|
| 9610:26 | if guard | `if (!op) break;` |

### `logos::compiler::BorrowChecker::closure_caps_of[abi:cxx11]` — 1 of 15 regions cold (hottest 63319)

| line:col | what it is | source |
|---:|---|---|
| 10195:22 | if guard | `if (!callee) return nullptr;` |

### `logos::compiler::BorrowChecker::call_callee` — 1 of 7 regions cold (hottest 63319)

| line:col | what it is | source |
|---:|---|---|
| 10207:9 | early return | `return {};` |

### `logos::compiler::BorrowChecker::flow_of_call` — 1 of 4 regions cold (hottest 4639424)

| line:col | what it is | source |
|---:|---|---|
| 11483:74 | early return | `return flows_ ? resolve_call_flow(*flows_, symbol, &fn_index_) : nullptr;` |

### `logos::compiler::BorrowChecker::flow_of_method` — 1 of 4 regions cold (hottest 607256)

| line:col | what it is | source |
|---:|---|---|
| 11488:25 | statement | `: nullptr;` |

## C. Near-dead regions — count 1..9 (257)

**This is the class that wasted three probe slots.** A site here is
live, so a probe on it is not "never fired" — it fires, twice, and
reports a ceiling of 0 that reads exactly like a refuted hypothesis.
Before spending a slot here, ask whether a population of this size
could show the effect at all.

| count | line:col | function | what it is | source |
|---:|---:|---|---|---|
| 1 | 1566:36 | `void logos::compiler::RefGraph::each_root_place` | if guard | `if (seen.size() > 512) break;          // bound, as elsewhere here` |
| 1 | 3613:31 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live")) continue;` |
| 1 | 3613:69 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live")) continue;` |
| 1 | 3815:49 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!it->mut_field_borrows.empty()) {` |
| 1 | 4334:54 | `logos::compiler::BorrowChecker::path_overlaps` | early return | `return b.compare(0, a.size(), a) == 0 && b[a.size()] == '.';` |
| 1 | 4352:17 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4352:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4352:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `(it->first.size() > path.size() &&` |
| 1 | 4355:46 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 1 | 4590:17 | `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` | statement | `cur = lir_view::EAddrOfTempView{cur}.inner();` |
| 1 | 5333:13 | `logos::compiler::BorrowChecker::check_recv_conflict` | diagnostic emission | `report(line, std::format(` |
| 1 | 6002:45 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (ts_.frame_consts.count(rn)) return rn;` |
| 1 | 6545:28 | `logos::compiler::BorrowChecker::prov_of` | if guard | `if (!caps) return {};` |
| 1 | 7299:25 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (fs) {` |
| 1 | 7301:48 | `logos::compiler::BorrowChecker::prov_of_retained` | call / statement | `fv.each_arg([&](ExprRef a) {` |
| 1 | 7302:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7302:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7302:29 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7302:34 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7302:53 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (a && i < fs->nparams && (fs->to_result & (1ull << i)))` |
| 1 | 7553:17 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!var_has(NO_SLOT, src))` |
| 1 | 7554:17 | `logos::compiler::BorrowChecker::check_return_value` | diagnostic emission | `report(line, std::format(` |
| 1 | 10261:26 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 10261:30 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 10261:55 | `logos::compiler::BorrowChecker::release_place_retarget` | else-if guard | `else if (sit->shared_borrows > 0) --sit->shared_borrows;` |
| 1 | 11738:55 | `logos::compiler::BorrowChecker::visit` | call / statement | `sit->moved_fields, path)) {` |
| 1 | 11795:62 | `logos::compiler::BorrowChecker::visit` | if guard | `if (is_mut && sit->mut_reservations > 0) {` |
| 1 | 12425:41 | `borrow_check.cpp:logos::compiler::BorrowChecker::visit` | if guard | `if (st.moved && saved_s.has_id(slot, name))` |
| 1 | 12426:29 | `borrow_check.cpp:logos::compiler::BorrowChecker::visit` | statement | `merged_s->at_id(slot, name) = st;` |
| 2 | 2257:50 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | loop body | `for (auto& fb : frame.field_borrows) {` |
| 2 | 2258:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (escapes(fb)) continue;` |
| 2 | 2259:17 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | statement | `auto* it = snap.find(fb.target_slot, fb.target);` |
| 2 | 2260:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (it == nullptr) continue;` |
| 2 | 2261:17 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 2261:21 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 2261:32 | `logos::compiler::BorrowChecker::loop_exit_snapshot` | if guard | `if (fb.is_mut) it->mut_field_borrows.erase(fb.path);` |
| 2 | 4387:13 | `logos::compiler::BorrowChecker::consume` | early return | `return false;` |
| 2 | 5108:29 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | statement | `adds.emplace_back(n + kv.first.substr(place.size()),` |
| 2 | 5110:42 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& a : adds) {` |
| 2 | 5113:29 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | if guard | `if (std::find(v.begin(), v.end(), s) == v.end())` |
| 2 | 5113:33 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | if guard | `if (std::find(v.begin(), v.end(), s) == v.end())` |
| 2 | 5114:33 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | statement | `v.push_back(s);` |
| 2 | 5410:19 | `logos::compiler::BorrowChecker::check_place_mut_use` | statement | `? std::string("cannot assign to a place behind a `&` reference")` |
| 2 | 5537:33 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (bare == "Rc" \|\| bare == "Arc") return true;` |
| 2 | 5539:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (depth <= 0) return false;` |
| 2 | 5539:13 | `logos::compiler::BorrowChecker::type_is_residency_backed` | if guard | `if (depth <= 0) return false;` |
| 2 | 5540:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | loop body | `for (auto a : t.type_args())` |
| 2 | 5542:9 | `logos::compiler::BorrowChecker::type_is_residency_backed` | early return | `return false;` |
| 2 | 5559:48 | `logos::compiler::BorrowChecker::residency_exemption_holds` | if guard | `if (!local_is_residency_backed(n)) {` |
| 2 | 5560:21 | `logos::compiler::BorrowChecker::residency_exemption_holds` | if guard | `if (std::getenv("LOGOS_86_TRACE"))` |
| 2 | 7242:13 | `logos::compiler::BorrowChecker::retains_borrowing_operand` | switch arm | `case Code::ArrLit:      EArrLitView{e}.each_elem(one); break;` |
| 2 | 7509:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `default: break;` |
| 2 | 7790:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | early return | `return;` |
| 2 | 7792:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 2 | 7792:30 | `logos::compiler::BorrowChecker::take_ref_borrows` | statement | `(res_bc && is_borrow_carrying_type(a.type(pool))))` |
| 2 | 7973:59 | `logos::compiler::BorrowChecker::take_ref_borrows` | call / statement | `sit->moved_fields, path)) {` |
| 2 | 9943:27 | `logos::compiler::BorrowChecker::note_reborrow` | if guard | `if (name.empty()) return;` |
| 2 | 10185:27 | `logos::compiler::BorrowChecker::note_closure_caps` | if guard | `if (name.empty()) return;` |
| 2 | 10199:44 | `logos::compiler::BorrowChecker::closure_caps_of[abi:cxx11]` | if guard | `if (callee.kind() != Code::VarRef) return nullptr;` |
| 2 | 10333:21 | `auto logos::compiler::BorrowChecker::place_write_loans` | call / statement | `std::find(rec.co_holders.begin(), rec.co_holders.end(), root)` |
| 2 | 10334:50 | `auto logos::compiler::BorrowChecker::place_write_loans` | statement | `== rec.co_holders.end()) return;` |
| 2 | 10344:54 | `logos::compiler::BorrowChecker::place_write_loans` | loop body | `for (auto& fb : frame.field_borrows) reroot(fb);` |
| 3 | 2776:13 | `logos::compiler::BorrowChecker::collect_borrow_locals` | switch arm | `default:` |
| 3 | 3065:72 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 3 | 3503:43 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 3 | 3575:24 | `logos::compiler::BorrowChecker::path_prefix_or_eq` | if guard | `if (a.empty()) return true;  // whole-value covers everything` |
| 3 | 3630:28 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (it == nullptr) return;` |
| 3 | 4481:35 | `logos::compiler::BorrowChecker::deref_move_exempt` | if guard | `if (in_destructure_temp_) return true;` |
| 3 | 4524:9 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4524:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4524:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4524:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::MutRef)` |
| 3 | 4525:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | early return | `return "cannot move out of a value behind a mutable reference (E0507)";` |
| 3 | 5411:19 | `logos::compiler::BorrowChecker::check_place_mut_use` | statement | `: std::format("cannot assign to '{}': '{}' is behind a "` |
| 3 | 5975:39 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (k == Code::AddrOfTemp){ cur = EAddrOfTempView{cur}.inner(); continue; }` |
| 3 | 6034:17 | `logos::compiler::BorrowChecker::bc_hop_roots` | if guard | `if (!e) return;` |
| 3 | 7460:25 | `logos::compiler::BorrowChecker::check_return_value` | loop body | `for (auto& n : it->second)` |
| 3 | 7461:29 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7461:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7461:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7461:33 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7461:47 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (!n.empty() && !is_return_temp_name(n) &&` |
| 3 | 7462:33 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!is_materialized_temp_name(n)) { src = n; break; }` |
| 3 | 7462:64 | `logos::compiler::BorrowChecker::check_return_value` | statement | `!is_materialized_temp_name(n)) { src = n; break; }` |
| 3 | 7506:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::ArrLit:   case Code::LitInt:` |
| 3 | 7506:50 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::ArrLit:   case Code::LitInt:` |
| 3 | 7507:29 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::LitFloat: case Code::LitBool:` |
| 3 | 7507:50 | `logos::compiler::BorrowChecker::check_return_value` | switch arm | `case Code::LitFloat: case Code::LitBool:` |
| 3 | 7512:35 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 3 | 7638:64 | `logos::compiler::BorrowChecker::check_return_value` | statement | `ret_lt, src, src_lt.empty() ? "(elided)" : src_lt));` |
| 3 | 11810:55 | `logos::compiler::BorrowChecker::visit` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 3 | 11810:63 | `logos::compiler::BorrowChecker::visit` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 4 | 2622:17 | `logos::compiler::BorrowChecker::declared_pos` | loop exit | `continue;   // same word, different binding` |
| 4 | 2638:27 | `logos::compiler::BorrowChecker::note_binding_slot` | if guard | `if (name.empty()) return;` |
| 4 | 2816:28 | `logos::compiler::BorrowChecker::erase_ref_sources_under` | if guard | `if (place.empty()) return;` |
| 4 | 3052:13 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | switch arm | `case EC::TupleLit:` |
| 4 | 3054:34 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3054:43 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3054:72 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 4 | 3614:37 | `logos::compiler::BorrowChecker::field_borrow_conflicts` | if guard | `if (path.empty() \|\| paths_overlap(path, p)) {` |
| 4 | 3744:52 | `logos::compiler::BorrowChecker::take_borrow_whole_` | statement | `!it->shared_field_borrows.empty()) {` |
| 4 | 4335:50 | `logos::compiler::BorrowChecker::path_overlaps` | early return | `return a.compare(0, b.size(), b) == 0 && a[b.size()] == '.';` |
| 4 | 4523:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | early return | `return "cannot move out of a value behind a shared reference (E0507)";` |
| 4 | 4603:9 | `logos::compiler::BorrowChecker::check_whole_read_vs_field_loans` | diagnostic emission | `report(line, std::format(` |
| 4 | 6387:21 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {{}, /*is_local=*/true};` |
| 4 | 6901:25 | `logos::compiler::BorrowChecker::prov_of` | statement | `ap.is_temp = true;` |
| 4 | 7348:25 | `logos::compiler::BorrowChecker::prov_of_retained` | statement | `merged = merge_prov(merged, it->second);` |
| 4 | 7349:26 | `logos::compiler::BorrowChecker::prov_of_retained` | else-if guard | `else if (force_local)` |
| 4 | 7349:30 | `logos::compiler::BorrowChecker::prov_of_retained` | else-if guard | `else if (force_local)` |
| 4 | 7457:47 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (is_return_temp_name(src)) {` |
| 4 | 7459:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (it != ret_temp_roots_.end())` |
| 4 | 9175:36 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (fit->is_mut && same(fit->holder, fit->holder_slot) &&` |
| 4 | 11021:70 | `logos::compiler::BorrowChecker::visit_stmt` | call / statement | `type_may_carry_borrow(v.value().type(pool))) {` |
| 4 | 11022:43 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `std::string cr0 = mrecv.kind() == EC::VarRef` |
| 4 | 11024:31 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `: flow_operand_root(mrecv);` |
| 4 | 11026:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11026:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11026:44 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && !var_has(NO_SLOT, cr))` |
| 4 | 11028:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11028:29 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11028:44 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!cr.empty() && var_has(NO_SLOT, cr))` |
| 4 | 11029:29 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `note_holder_escape_prov(cr, holder_ty_of(cr),` |
| 4 | 11144:45 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `v.each_guard([&](ExprRef g) { visit(g, /*consuming=*/true, ln); });` |
| 4 | 11731:21 | `logos::compiler::BorrowChecker::visit` | diagnostic emission | `report(line, std::format(` |
| 4 | 11920:43 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 5 | 3060:76 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 5 | 3504:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | call / statement | `arg_retained_by_callee(fs, i)))` |
| 5 | 3633:31 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (it->mut_borrowed) {` |
| 5 | 5407:65 | `logos::compiler::BorrowChecker::check_place_mut_use` | call / statement | `bp.through_ref_type.kind() == LogosType::Kind::Ref) {` |
| 5 | 5409:26 | `logos::compiler::BorrowChecker::check_place_mut_use` | diagnostic emission | `report(line, place.empty()` |
| 5 | 6421:21 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {{}, /*is_local=*/false, /*is_temp=*/true};` |
| 5 | 7504:42 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 5 | 7504:48 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 5 | 7565:35 | `logos::compiler::BorrowChecker::check_return_value` | statement | `src.empty() ? "?" : src));` |
| 5 | 9142:63 | `logos::compiler::BorrowChecker::release_borrows_held_by` | call / statement | `auto named_elsewhere = [&](const std::string& target) {` |
| 5 | 9143:58 | `logos::compiler::BorrowChecker::release_borrows_held_by` | loop body | `for (auto& [pl, srcs] : ref_borrow_sources_) {` |
| 5 | 9144:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (place_under(pl, holder_name)) continue;` |
| 5 | 9144:51 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (place_under(pl, holder_name)) continue;` |
| 5 | 9147:13 | `logos::compiler::BorrowChecker::release_borrows_held_by` | early return | `return false;` |
| 5 | 9166:47 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `it->co_holders.empty() && !named_elsewhere(it->target) &&` |
| 5 | 9167:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `!logos::probe::on("holderkill_keep")) {` |
| 5 | 9167:59 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `!logos::probe::on("holderkill_keep")) {` |
| 5 | 9168:75 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (auto sit = var_find(it->target_slot, it->target); sit != nullptr)` |
| 5 | 9169:25 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `sit->mut_borrowed = false;` |
| 5 | 9697:62 | `logos::compiler::BorrowChecker::ref_source_admissible` | if guard | `if (root.empty() \|\| is_materialized_temp_name(root)) return false;` |
| 5 | 10465:27 | `logos::compiler::BorrowChecker::visit_stmt` | call / statement | `t.kind() == LogosType::Kind::ZonedStruct))` |
| 5 | 10818:33 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln, std::format(` |
| 5 | 11730:24 | `logos::compiler::BorrowChecker::visit` | statement | `&& !param_names_.count(root))` |
| 6 | 746:13 | `borrow_check.cpp:logos::compiler::is_cond_move_field_drop_place` | switch arm | `case EK::TupleIndex:` |
| 6 | 1337:57 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | if guard | `if (bc_loan_carrying_type(ts_, TypeRef(e))) return true;` |
| 6 | 3662:65 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | statement | `!it->is_mut_binding && !param_names_.count(target)) {` |
| 6 | 3775:45 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (it->shared_borrows > 0) {` |
| 6 | 3777:25 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!scopes_.empty()) {` |
| 6 | 3777:43 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (!scopes_.empty()) {` |
| 6 | 3782:29 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (in_top < it->shared_borrows)` |
| 6 | 3783:29 | `logos::compiler::BorrowChecker::take_borrow_whole_` | statement | `outer_shared = true;` |
| 6 | 3787:25 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (outer_shared) {` |
| 6 | 3787:39 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (outer_shared) {` |
| 6 | 4310:35 | `auto logos::compiler::BorrowChecker::inherit_loans` | if guard | `if (rec.holder == to) return;` |
| 6 | 4355:28 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 6 | 6376:32 | `logos::compiler::BorrowChecker::prov_of` | statement | `? RefProv{{name}, false}` |
| 6 | 6388:17 | `logos::compiler::BorrowChecker::prov_of` | early return | `return {};` |
| 6 | 7769:25 | `logos::compiler::BorrowChecker::take_ref_borrows` | loop body | `for (auto& c : *caps)` |
| 6 | 7770:29 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7770:33 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7770:33 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7770:56 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 7770:80 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (var_has(NO_SLOT, c) && !param_names_.count(c)) {` |
| 6 | 9133:35 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (h != holder_name) return false;` |
| 6 | 10260:36 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.is_mut) sit->mut_borrowed = false;` |
| 6 | 11366:52 | `logos::compiler::BorrowChecker::visit_stmt` | loop body | `for (auto& [k, d] : dangling_) acc_dang.emplace(k, d);` |
| 7 | 3757:43 | `logos::compiler::BorrowChecker::take_borrow_whole_` | if guard | `if (it->mut_reservations > 0) {` |
| 7 | 4072:17 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4072:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4072:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4072:46 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (param_names_.count(r) \|\| !var_has(NO_SLOT, r)) return;` |
| 7 | 4073:17 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (std::find(chased.begin(), chased.end(), r) == chased.end())` |
| 7 | 4073:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (std::find(chased.begin(), chased.end(), r) == chased.end())` |
| 7 | 4074:21 | `logos::compiler::BorrowChecker::apply_flow_outparams` | statement | `chased.push_back(r);` |
| 7 | 4223:36 | `logos::compiler::BorrowChecker::apply_flow_outparams` | loop body | `for (auto& h : chased) inherit_loans(dst, h, line);` |
| 7 | 4332:21 | `logos::compiler::BorrowChecker::path_overlaps` | if guard | `if (a == b) return true;` |
| 7 | 4350:60 | `logos::compiler::BorrowChecker::erase_reinit` | loop body | `for (auto it = moved.begin(); it != moved.end(); ) {` |
| 7 | 4351:28 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `bool covered = it->first == path \|\|` |
| 7 | 4355:18 | `logos::compiler::BorrowChecker::erase_reinit` | statement | `it = covered ? moved.erase(it) : ++it;` |
| 7 | 4362:40 | `logos::compiler::BorrowChecker::consume` | if guard | `if (!it->moved_fields.empty()) {` |
| 7 | 4521:9 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4521:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4521:24 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | statement | `auto ot = op ? op.type(pool) : TypeRef(nullptr);` |
| 7 | 4522:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 4522:13 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 4522:19 | `logos::compiler::BorrowChecker::deref_move_message[abi:cxx11]` | if guard | `if (ot && ot.kind() == LogosType::Kind::Ref)` |
| 7 | 7789:35 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (fs && ix < fs->nparams && !(fs->to_result & (1ull << ix)))` |
| 7 | 7789:55 | `logos::compiler::BorrowChecker::take_ref_borrows` | if guard | `if (fs && ix < fs->nparams && !(fs->to_result & (1ull << ix)))` |
| 7 | 10248:51 | `logos::compiler::BorrowChecker::release_place_retarget` | call / statement | `auto take_one = [&](const std::string& t) {` |
| 7 | 10250:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (it == targets.end()) return false;` |
| 7 | 10251:13 | `logos::compiler::BorrowChecker::release_place_retarget` | statement | `targets.erase(it);` |
| 7 | 10255:58 | `logos::compiler::BorrowChecker::release_place_retarget` | loop body | `for (size_t i = frame.borrows.size(); i > 0; --i) {` |
| 7 | 10255:63 | `logos::compiler::BorrowChecker::release_place_retarget` | loop body | `for (size_t i = frame.borrows.size(); i > 0; --i) {` |
| 7 | 10257:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10257:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10257:42 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.holder != root \|\| !br.co_holders.empty()) continue;` |
| 7 | 10258:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (!take_one(br.target)) continue;` |
| 7 | 10258:21 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (!take_one(br.target)) continue;` |
| 7 | 10259:17 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10259:69 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10259:85 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {` |
| 7 | 10260:25 | `logos::compiler::BorrowChecker::release_place_retarget` | if guard | `if (br.is_mut) sit->mut_borrowed = false;` |
| 7 | 10814:33 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln, std::format(` |
| 7 | 11809:39 | `logos::compiler::BorrowChecker::visit` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live")) continue;` |
| 7 | 11809:77 | `logos::compiler::BorrowChecker::visit` | if guard | `if (c <= 0 && !logos::probe::on("sharedzero_live")) continue;` |
| 8 | 2527:51 | `logos::compiler::BorrowChecker::pop_scope` | if guard | `if (bpos >= 0 && spos < bpos) continue; // binding drops first` |
| 8 | 3053:83 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | call / statement | `lir_view::ETupleLitView{a}.each_elem([&](lir_view::ExprRef inner) {` |
| 8 | 3054:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3054:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3054:25 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | if guard | `if (!tied && inner && forms_borrow_at_call(inner)) tied = true;` |
| 8 | 3501:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `is_borrow_carrying_type(a.type(pool)) \|\|` |
| 8 | 3502:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `forms_borrow_at_call(a) \|\|` |
| 8 | 3503:35 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 8 | 3503:36 | `logos::compiler::BorrowChecker::collect_ref_sources_paths` | statement | `(!fs && is_ref_kind(a.type(pool))) \|\|` |
| 8 | 4467:13 | `logos::compiler::BorrowChecker::deref_move_exempt` | early return | `return true;` |
| 8 | 4736:46 | `void logos::compiler::BorrowChecker::each_pat_binding` | statement | `v.each_suffix([&](PatRef sub){ each_pat_binding(sub, f); });` |
| 8 | 5086:35 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 8 | 5086:39 | `logos::compiler::BorrowChecker::propagate_pat_reborrows` | loop body | `for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;` |
| 8 | 5642:46 | `logos::compiler::BorrowChecker::type_is_share_handle` | else-if guard | `else if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 8 | 5978:23 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | if guard | `if (terminal) *terminal = cur;` |
| 8 | 5981:13 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | statement | `*temp_root = true;` |
| 8 | 7345:80 | `logos::compiler::BorrowChecker::prov_of_retained` | call / statement | `EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {` |
| 8 | 7347:50 | `logos::compiler::BorrowChecker::prov_of_retained` | if guard | `if (auto it = prov_.find(n); it != prov_.end())` |
| 8 | 7424:40 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (src.empty() && !srcs.empty()) src = srcs.front();` |
| 8 | 7498:46 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (src.empty() && !is_temp) {` |
| 8 | 7503:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7503:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7503:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&` |
| 8 | 7504:25 | `logos::compiler::BorrowChecker::check_return_value` | call / statement | `temp_root_msg && term) {` |
| 8 | 7512:21 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 8 | 7512:25 | `logos::compiler::BorrowChecker::check_return_value` | if guard | `if (lit_term) is_temp = true;` |
| 8 | 8799:37 | `logos::compiler::BorrowChecker::prescan_fnptr` | else-if guard | `else if (it->second != sym) fnptr_multi_.insert(name);    // two callees` |
| 8 | 9134:13 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9134:17 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9134:17 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9134:37 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9134:52 | `logos::compiler::BorrowChecker::release_borrows_held_by` | if guard | `if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;` |
| 8 | 9166:21 | `logos::compiler::BorrowChecker::release_borrows_held_by` | statement | `it->co_holders.empty() && !named_elsewhere(it->target) &&` |
| 8 | 10457:25 | `logos::compiler::BorrowChecker::visit_stmt` | diagnostic emission | `report(ln,` |
| 8 | 11345:52 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!bg \|\| !bg->moved) guard_acc.at_id(slot, name) = st;` |
| 8 | 11920:61 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 9 | 3688:51 | `logos::compiler::BorrowChecker::take_field_borrow_path_` | if guard | `if (paths_overlap(path, p) && is_mut) {` |
| 9 | 4088:45 | `logos::compiler::BorrowChecker::apply_flow_outparams` | if guard | `if (reborrow_mut_.count(s)) subs.push_back(s);` |
| 9 | 4090:34 | `logos::compiler::BorrowChecker::apply_flow_outparams` | loop body | `for (auto& s : subs) reborrow_of_.each_root_place(s, chase);` |
| 9 | 6011:17 | `logos::compiler::BorrowChecker::value_local_root[abi:cxx11]` | early return | `return rn;` |
| 9 | 6531:33 | `logos::compiler::BorrowChecker::prov_of` | statement | `merged = merge_prov(merged, prov_of(a));` |
| 9 | 9791:72 | `logos::compiler::BorrowChecker::ref_sources_of[abi:cxx11]` | statement | `reborrow_of_.each_under(p, [&](const std::string& sub) { add(sub); });` |
| 9 | 10500:43 | `logos::compiler::BorrowChecker::visit_stmt` | if guard | `if (!sources.empty()) {` |
| 9 | 11131:25 | `logos::compiler::BorrowChecker::visit_stmt` | statement | `bc_hop_roots(sc, roots);` |
| 9 | 11908:81 | `logos::compiler::BorrowChecker::visit` | if guard | `if (auto* hit = find_moved_overlap(it->moved_fields, path)) {` |
| 9 | 11920:29 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |
| 9 | 11920:29 | `logos::compiler::BorrowChecker::visit` | if guard | `if (into_moved \|\| !in_addr_source_) {` |

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
| 392525339 | 814:36 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `static bool is_ref_kind(TypeRef t) {` |
| 392525339 | 829:12 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378361 | 829:17 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378361 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378361 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378361 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 391378361 | 829:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `return t && (t.kind() == LogosType::Kind::Ref \|\|` |
| 365072479 | 830:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `t.kind() == LogosType::Kind::MutRef \|\|` |
| 346781077 | 831:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `t.kind() == LogosType::Kind::Slice \|\|` |
| 325631740 | 832:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) \|\|` |
| 325631740 | 832:19 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) \|\|` |
| 325626879 | 833:18 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::TraitObject &&` |
| 325626879 | 833:19 | `borrow_check.cpp:logos::compiler::is_ref_kind` | `(t.kind() == LogosType::Kind::TraitObject &&` |
| 299203029 | 1325:67 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `static bool bc_loan_carrying_type(const TypeSets& ts_, TypeRef t) {` |
| 299203029 | 1326:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!t) return false;` |
| 299203029 | 1327:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `auto k = t.kind();` |
| 299203029 | 1329:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());` |
| 299203029 | 1332:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!nm.empty() && ts_.loan_carrying.count(nm) > 0) return true;` |
| 299203029 | 1332:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (!nm.empty() && ts_.loan_carrying.count(nm) > 0) return true;` |
| 299133200 | 1333:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `for (auto a : t.type_args())` |
| 299102349 | 1335:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Tuple) {` |
| 299102349 | 1335:9 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Tuple) {` |
| 296788163 | 1340:5 | `borrow_check.cpp:logos::compiler::bc_loan_carrying_type` | `if (k == LogosType::Kind::Array \|\| k == LogosType::Kind::Slice)` |

## E. Where the probes are aimed

Every `logos::probe::on(...)` site in the TU with the execution count
of its ENCLOSING region. The probe's own body is 0 by construction
here: no probe was armed for the mapping run.

⚠ CORRECTION 2026-08-27: this column was originally labelled "the
number of times the probe's condition was evaluated". IT IS NOT — it is
the ENCLOSING region, and where the probe sits behind short-circuited
conjuncts or after an early `return` the two differ, in BOTH
directions. Five rows were re-read against the probe's OWN condition
region and are corrected in place below (the enclosing figure is kept
in parentheses):
`retarget_keep` 194 → 30 · `holderkill_keep` 1138 → 5 ·
`rehome_all` 2165443 → 573451 · `ptrderef` 1362221 → 46887 ·
`callroot` 1362221 → 22933239 (§E UNDERSTATED this one by 17x: it
printed the preceding Deref arm). Every other row is the enclosing
region and has not been re-read per site.

| arrivals | line | probe | enclosing function |
|---:|---:|---|---|
| 0 | 1080 | `rootkeep` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 10 | 7344 | `capescape` | `logos::compiler::BorrowChecker::prov_of_retained` |
| 13 | 3612 | `sharedzero_site` | `logos::compiler::BorrowChecker::field_borrow_conflicts` |
| 13 | 3613 | `sharedzero_live` | `logos::compiler::BorrowChecker::field_borrow_conflicts` |
| 17 | 7627 | `lifereg_aggtrust` | `logos::compiler::BorrowChecker::check_return_value` |
| 18 | 11808 | `sharedzero_site` | `logos::compiler::BorrowChecker::visit` |
| 18 | 11809 | `sharedzero_live` | `logos::compiler::BorrowChecker::visit` |
| 35 | 8545 | `capmove` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 82 | 8623 | `capscope` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 30 (encl. 194) | 10246 | `retarget_keep` | `logos::compiler::BorrowChecker::release_place_retarget` |
| 259 | 8567 | `capmut` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 259 | 8592 | `capshared` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 325 | 3686 | `sharedzero_site` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 325 | 3687 | `sharedzero_live` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 5 (encl. 1138) | 9167 | `holderkill_keep` | `logos::compiler::BorrowChecker::release_borrows_held_by` |
| 1655 | 9271 | `nll_lu_zero` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 1655 | 9273 | `nll_lu_strict` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 2397 | 8229 | `genautoref` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 2397 | 8249 | `genautorefx` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 21299 | 1070 | `rootkeep` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 25627 | 8175 | `genarg0blind` | `logos::compiler::BorrowChecker::take_ref_borrows` |
| 84202 | 9247 | `nll_lu_zero` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 84202 | 9253 | `nll_lu_strict` | `logos::compiler::BorrowChecker::release_dead_borrows` |
| 176974 | 2552 | `droporder` | `logos::compiler::BorrowChecker::pop_scope` |
| 582848 | 3705 | `sharedzero_prod` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 582885 | 3684 | `sharedzero_reach` | `logos::compiler::BorrowChecker::take_field_borrow_path_` |
| 46887 (encl. 1362221) | 1129 | `ptrderef` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 22933239 (encl. 1362221, the preceding Deref arm) | 1136 | `callroot` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 1447676 | 1028 | `sharedsticky` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |
| 573451 (encl. 2165443) | 2450 | `rehome_all` | `auto logos::compiler::BorrowChecker::pop_scope` |
| 2213272 | 3860 | `selftest_inert` | `logos::compiler::BorrowChecker::record_borrow` |
| 2213272 | 3861 | `selftest_refuse` | `logos::compiler::BorrowChecker::record_borrow` |
| 2213272 | 3877 | `movedborrow` | `logos::compiler::BorrowChecker::record_borrow` |
| 3432807 | 12249 | `genrecvconflict` | `logos::compiler::BorrowChecker::visit` |
| 22842031 | 1175 | `refwhole` | `borrow_check.cpp:logos::compiler::extract_borrow_place` |

