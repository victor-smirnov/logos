# The Deem interpreter deletion (P5) — call-graph blocker, fixture census, loss ledger

STATUS: **THE DELETION HAPPENED. THIS FILE IS NOW A POST-MORTEM, AND IT IS STILL
PINNED.** Everything above §9 is the measurement that preceded the cut, kept as
written except where a sentence became FALSE — those are corrected in place and
marked, because a post-mortem that quietly rewrites its own predictions is worth
less than the predictions were. §3's table is the LOSS LEDGER: all 85 rows stay,
53 of them declared `GONE-FIXTURE` in the pin block and checked, by FACT 8, to
be really gone with their `.expected` and with a reason.

⚠ **THE RULING ON THE PIN, WHICH WAS THIS ROUND'S TO MAKE AND IS WRITTEN DOWN
HERE.** After the deletion three of the seven facts became unsatisfiable in their
old form — FACT 3 (53 rows name files that no longer exist), FACT 5 (the registry
baseline moved by exactly −53) and FACT 6 (its population rule greps for symbols
that exist nowhere). The honest options were to RETIRE `logos_00_census_pin` or
to convert it into a post-mortem whose facts are about the POST-cut world.
Retirement was refused: this pin is the only machine-checked record of what P5
cost, and it caught three drifts nobody else caught. So:

* FACT 3 gained an EXEMPTION (`GONE-FIXTURE`) and FACT 8 was added to check that
  exemption in the direction it can be abused — the path absent, the `.expected`
  absent, a reason given. Net: every one of the 85 rows is still checked, one
  direction or the other, and a row cannot move between them without an edit here.
* FACT 5 was RE-MEASURED, not relaxed: 6906 / 3223 / 30, a delta of exactly
  −53 / −53 / 0 against the pre-cut 6959 / 3276 / 30, predicted before the
  re-configure and measured after.
* FACT 6 was RE-AIMED from backward to forward. It no longer asks "is the census
  population complete?" — a question about a table that is now history — but "is
  the cut still cut?": no `CUT-SYMBOL` is DEFINED under `stdlib/` or named as a
  string LITERAL under `src/`, and every file under `tests/` that mentions one is
  a LIVE row or a declared `NOT-AFFECTED` line. The `src/` half is pointed
  straight at the trap this round had to disarm (§1b).
* NOTHING WAS WEAKENED AND NO CANARY WAS DROPPED. The `CUT-SYMBOL` list was
  CORRECTED first (§9); three canaries whose targets the cut removed were
  RE-POINTED at live ones; two new canaries were added for FACT 8. Nine became
  eleven.

MEASURED at `9ebb6110` (the tree this file was first committed into), RE-MEASURED
WHOLE at `e53962b6`, and RE-MEASURED AGAIN after the deletion.
Everything below is a grep or a read of a named symbol; no line numbers are cited,
because they move. Reproduce any row with `grep -rnE "^(pub )?(fn|struct) <name>\b" stdlib/`.

⚠ **THIS FILE DRIFTED TWICE BEFORE ANYONE NOTICED, AND NOW IT IS PINNED.** §9
declares every claim in here that a machine can decide and
`tests/logos/census_pin_gate.sh` (`logos_00_census_pin`, `tier_commit`) fails when
the tree disagrees with any of them. A number in this file that no longer matches
the repo is a RED TEST, not a sentence somebody has to re-read.
The pin does not judge prose: whether a class letter was the right judgement is
still a human question, and the class column is now HISTORY — it records how each
row was priced BEFORE the cut, which is what makes the loss ledger readable.

Scope statement being tested (P5 as written): delete `stdlib/mem/deem/check.logos`, `stdlib/mem/deem/exec.logos`, `stdlib/mem/deem/query.logos`
(as written it also named `eval.logos`, which the C2 port has since deleted outright — §1c)
(4713 lines as written), keep `incr.logos` and `incr_rec.logos`, triage ~80 fixtures.
⚠ `eval.logos` no longer exists (the template port, `8c5ad0ea`); the three surviving named files are
974+1450+963 = 3387 lines today. §2 carries the re-measured table.

---

## 1. THE BLOCKER — three independent refusals

### 1a. The four doomed files are the SUBSTRATE of `incr.logos` / `incr_rec.logos`

Every symbol below has exactly ONE definition in `stdlib/`:

| symbol | sole definition | in the cut? |
|---|---|---|
| `qplan_new` `check_rexpr` `struct QPlan` | `stdlib/mem/deem/check.logos` (three files in the tree carry that basename, so this census always writes this one with its path) | yes |
| `relctx_new` `exec_root` `rt_key_hash` `es_scan` `struct RelCtx` `struct OutTab` | `stdlib/mem/deem/exec.logos` | yes |
| `struct Query` `struct QRows` | `stdlib/mem/deem/query.logos` | yes |
| `rbinds_new` `eval_sexpr` `struct RBinds` `struct Tpl` `chk_new` `sx_of` `struct Chk` | `stdlib/mem/deem/tpl.logos` | **NO** — ported at `8c5ad0ea` out of `eval.logos`/`stdlib/mem/deem/check.logos`/`exec.logos`, survives the cut (§1c) |
| `ts_scan` `h_step` `struct RowSet` `dyn_graph_edges` `dyn_graph_edge_rows` `struct DynEdge` | `stdlib/mem/deem/graphsrc.logos` | **NO** — ported at `4569535c` out of `exec.logos`, survives the cut (§5 C4). ⚠ `es_scan` came BACK to `exec.logos`: it walks nothing and its only caller is the `QB_EDGE` arm, so here it would have been unreachable and unsensored after the cut (§5 C4 (3)) |

⚠ The last two rows USED to read `eval.logos` + `stdlib/mem/deem/check.logos` + `exec.logos`, i.e. doomed. Two ports
moved those twelve names to files that are not in the cut, so the blocker below now rests on the first
three rows ONLY. `chk_new` / `sx_of` / `Chk` / `ts_scan` / `h_step` are no longer evidence for it.

Real (non-comment) call/type uses in the two files P5 says it keeps — counted by
`grep -cE "\b(qplan_new|chk_new|sx_of|check_rexpr|relctx_new|exec_root|rt_key_hash|h_step|rbinds_new|eval_sexpr)\("`:

* `stdlib/mem/deem/incr.logos` — **44**
* `stdlib/mem/deem/incr_rec.logos` — **25**

Re-counted TWICE, because the doomed set shrank twice:

* after the `tpl.logos` port (`8c5ad0ea`), dropping `chk_new`/`sx_of`/`rbinds_new`/`eval_sexpr`:
  `qplan_new|check_rexpr|relctx_new|exec_root|rt_key_hash|h_step` → **19** and **21**;
* after the `graphsrc.logos` port (`4569535c`), which moved `h_step` to a surviving file:
  `qplan_new|check_rexpr|relctx_new|exec_root|rt_key_hash` → **12** and **18**. ← current

The refusal does not depend on the difference — 12 and 18 are still unsatisfiable after the cut — but
44/25 and 19/21 are no longer measurements OF THE CUT and must not be quoted as ones. ⚠ Each port
shrinks this number; re-measure it rather than copying, and note that `h_step` is the load-bearing
reason `graphsrc.logos` had to survive at all (`incr.logos` and `incr_rec.logos` both call it).

plus the type uses (`&Query`, `QPlan`, `RelCtx`, `QRows` in `IncrRec::snapshot`, `ir_check`,
`dred`, `epoch`; `Chk` now resolves to `tpl.logos` and survives).

⚠ RE-MEASURED at `e53962b6`, four ways, because `grep -c` counts LINES and a call count is about
OCCURRENCES: 44 / 25 raw lines (the command above, verbatim — confirmed unchanged); 43 / 25 lines with
`//` comments stripped (`sed 's://.*::'`); **54 / 29 OCCURRENCES** stripped
(`| grep -oE … | wc -l`). The load-bearing reading is the last one, and it is LARGER than the recorded
figure, so §1a's conclusion holds a fortiori. A re-measure reported elsewhere as "19 / 21" does not
reproduce by any of the four readings and is not carried here.

And the loop closes at the entry point: `Query { … }` is CONSTRUCTED at exactly one site in the tree —
inside `Query::compile` in `stdlib/mem/deem/query.logos`. `impl Query` exists in three files
(`query.logos`, `incr.logos` for `Query::incremental`, `incr_rec.logos` for `Query::incremental_rec`),
so deleting `query.logos` removes the only way to OBTAIN an `IncrJoin` or an `IncrRec` even if they
still compiled.

⇒ There is no 4713-line cut. The cut is the whole dynamic + dynamic-incremental tier, or nothing.

### 1b. `mapping_state.logos` is `impl EngineState for IncrRec`

`stdlib/mem/deem/mapping_state.logos` (92 lines, not on any list) is nothing but
`pub trait EngineState`, `impl EngineState for IncrRec`, and the four materializers
`deem_state_trace` / `deem_state_epochs` / `deem_state_tail` / `deem_state_controls`, each taking
`e: &IncrRec`. It dies with `incr_rec.logos`.

⚠ CORRECTION (verification pass, measured at `8cf79102`): the compiler does NOT merely lose vocabulary.
`SemaChecker::seed_builtin_source_impls` in `src/compiler/sema_expr.cpp` hard-names the fallback seed
`source_impls_["IncrRec"]` → `EngineState` rels `trace`/`epochs`/`tail`/`controls`, materializers
`deem_state_trace`/`_epochs`/`_tail`/`_controls`, module `logos.mem.deem`. The seed is CONTENT-guarded
(`const bool have_incr = source_impls_.count("IncrRec") > 0; … if (have_incr) return;`) — it defers only
while `mapping_state.logos` declares them. Delete that file and the guard flips OFF, so the C++ fallback
RE-REGISTERS four materializers that no longer exist. The seed must be removed in the same change.
(`writ_graph_edges`, seeded the same way for `Writ`, survives.)

This one bites UPWARD into the static tier: `wql_engine_source_e2e` declares `pub deem encounters(e: &IncrRec)`
— a STATIC `deem` item whose source type is the dynamic engine (ADR 0016 M5, case S). The engine-as-a-source
capability is a static-tier feature that cannot outlive `IncrRec`. `stdlib/mem/wql/params.logos` carries the
comment that names it (on `native_use_at`, "Any `IncrRec` engine-source param present?"); the mechanism itself
is generic (natspec `rel_mod`), so the compiler does not break — the VOCABULARY does.

### 1c. the Trama TEMPLATE engine lived in the cut — RESOLVED by the port at `8c5ad0ea`

`pub struct Tpl`, `Tpl::compile`, `Tpl::render` USED to live in `eval.logos` and reach `stdlib/mem/deem/check.logos`
(`check_stmts`, `cbinds_new`, `chk_new`, `simplify_all`, `chk_err`) and `eval.logos`
(`render_stmts`, `rbinds_new`). They now live, whole, in `stdlib/mem/deem/tpl.logos`
(`eval.logos` no longer exists), so this refusal no longer blocks the cut. It is kept because the
REASON stands: this is a separate SHIPPED public capability, not part of the Datalog
arc: five fixtures use `Tpl` and never mention `Query` (`query_rtval_ops_e2e`, `query_tpl_udf_e2e`,
`query_trama_arith_err_e2e`, `query_trama_dynamic_e2e`, `query_trama_typecheck_e2e`).
`stdlib/mem/wql/trama.logos` and `trama_render.logos` both describe themselves as the STATIC sibling of
"the `Tpl::compile` runtime (logos.std.deem)" — co-designed, not superseded. The static tier never
replaced runtime templating and has no plan to.

---

## 2. The cut, if it is taken anyway

| file | lines | why it is in the cut |
|---|---|---|
Line counts RE-MEASURED by `wc -l` on the MERGED tree, after both ports (`8c5ad0ea` template,
`4569535c` graph walker) shrank `stdlib/mem/deem/check.logos` and `exec.logos` and deleted `eval.logos`:

| file | lines | why it is in the cut |
|---|---|---|
| `stdlib/mem/deem/check.logos` | 974 | on the list (was 1672; the EL/template checker left for `tpl.logos`) |
| `stdlib/mem/deem/exec.logos` | 1212 | on the list (1472 → 1450, `eval_sx` left; → 1167, `ts_scan` and its closure left for `graphsrc.logos`; → 1212, `es_scan` came BACK from `graphsrc.logos` beside its only caller — see §5 C4 (3)) |
| `stdlib/mem/deem/query.logos` | 963 | on the list |
| `stdlib/mem/deem/incr.logos` | 1978 | §1a; also holds `pub struct FactStore`, `IncrJoin` |
| `stdlib/mem/deem/incr_rec.logos` | 1466 | §1a; `IncrRec` |
| `stdlib/mem/deem/mapping_state.logos` | 92 | §1b |
| `stdlib/lcm/deem/facthistory.logos` | 514 | `FactHistory::new` composes `FactStore::new`; sole non-test constructor of `FactStore` |
| **total** | **7199** | 974+1212+963+1978+1466+92+514, `wc -l` on the merged tree |

Two files are NOT in this table and NOT in the cut, each ported out of it precisely so it outlives it:
`stdlib/mem/deem/tpl.logos` (1339, the template engine, §1c) and `stdlib/mem/deem/graphsrc.logos`
(389, the graph walker + `DynEdge`/`dyn_graph_edge_rows`, §5 C4). `eval.logos` no longer exists.

⚠ **This total has now been wrong three times** (8763 → 8480/7437 → 7154), each time because a port
moved lines out from under a recorded number. Do not copy it: run `wc -l` over the seven rows.
The three-file "interpreter proper" cut is 974+1167+963 = **3104**.

`stdlib/mem/deem/deem.logos` (1279) SURVIVES as a file — it holds `RtVal`, `rt_kind`, `rt_eq`,
`SchemaCatalog`, `QEnv` — but its `QEnv` half loses every consumer: `QEnv` is named in real code only in
the eight deem files above (`stdlib/mem/wql/rexpr_walk.logos`'s single `QEnv` mention is a comment), and
`bind_source_erased` / `bind_node_erased` / `bind_source_tree` are `QEnv` methods with no other caller.
The residue inside `deem.logos` is NOT measured here.

Outside `tests/` and outside the deem package there are ZERO real uses: every hit in
`stdlib/mem/wql/{catalog_macro,el,lower,mapping_item,plan_walker,rexpr_walk,writ_graph,trama,trama_render}.logos`,
`tools/peg_gen_cpp/CMakeLists.txt` and `tools/peg_gen_cpp/oracle/run_wql.sh` is a COMMENT.
(`resolve_source` in `plan_walker.logos` is a local definition with a different signature, not a
reference to `stdlib/mem/deem/check.logos`'s.) `catalog_macro.logos` really does `use logos.mem.deem`, but only for
`SchemaCatalog::from_static` — which survives.

---

## 3. Fixture census — 86 files

Population, FIRST ATTEMPT (kept because it is the mistake):
`grep -rlE "Query::run|Query::compile|\.incremental\(\)|incremental_rec\(\)|Tpl::compile|Tpl::render" tests/logos/`
= 82 files — 81 `tests/logos/pass/*.logos` + `tests/logos/rtval_domain_gate.sh`. Re-measured at
`e53962b6`: still 82.

⚠ **THAT POPULATION IS INCOMPLETE, and by construction.** It names the interpreter's ENTRY POINTS, not
the SYMBOLS in the §2 cut. The correct rule — now the one §9 pins and executes — is to grep `tests/` for
the cut's own symbols (`CUT-SYMBOL` in §9: 28 names, every one sole-defined in a §2 file). Measured at
`e53962b6` that yields **92 files**, ten more than the entry-point grep.

Three of the ten are real, non-comment uses and are now rows **83–85** below:
`query_incr_factstore_unit` (`FactStore::new`), `query_incr_factstore_epochs` (`FactHistory::new` ×2),
`query_incr_factstore_float_identity_unit` (`FactStore::new` + `FactHistory::new`, and it reaches
`fs_key_enc`). `FactStore` is defined once, in `stdlib/mem/deem/incr.logos`; `FactHistory` once, in
`stdlib/lcm/deem/facthistory.logos` — both in the cut, so all three die. They are class A, they carry
zero `deem` items and zero interpreter entry points, and they are the witnesses §6 L5 was three short of.

The other seven are mentions inside `//` comments or a ledger row, and are declared line by line as
`NOT-AFFECTED` in §9 — measured, not assumed: with comments stripped (`sed 's://.*::'`) not one of them
matches a cut symbol. Two of the seven (`deem_incr_join_e2e`, `wql_incr_rel_dred_mutrec_full`) did not
exist when this census was first written, which is precisely why the population is now derived by a gate
instead of listed by hand.

⇒ **85 affected test files**, and the population is pinned in BOTH directions: a new fixture that touches
a cut symbol must become a row or a declared `NOT-AFFECTED` line, and a censused fixture cannot be
deleted without the pin going red.

Classes:

* **A — dies with its subject.** No static arm; the file's subject IS the interpreter or the
  dynamic-incremental engine. Deleting it is correct, not a weakening.
* **B — edit.** A real static arm in the SAME file (a `deem` item is declared, or emitted-handle
  functions `<q>_apply`/`_retract`/`_epoch`/`_snapshot` are called). Drop the interpreter arm, keep the
  rest — and then check per fixture that what is left still BITES.
* **C — loss.** No static arm, and the subject is not the interpreter but a cell the interpreter is
  merely the vehicle for. §5 says what the static tier would have to gain.
* **D — capability, not fixture** (`wql_engine_source_e2e` only): §1b.
* **G — gate to re-pin** (`rtval_domain_gate.sh`): §7.
* **K** flag — the file carries a `⚠ KNOWN-WRONG` pin: an expectation that IS a recorded open defect,
  beside the value it OUGHT to give. Deleting the file deletes the record of the defect, so every K row
  must be transcribed into the owning arc BEFORE anything is removed.

⚠ **CORRECTION to the earlier triage.** A `grep` for `_apply(` over these fixtures is a FALSE SIGNAL:
eleven files define local helpers named `src_apply`, `nasty_apply`, `edge_apply` and have no static arm
at all. Measured, the static-arm population is 25 files, not the 17 previously listed, and the overlap is
partial — `query_incr_join_e2e`, `query_incr_join_fuzz`, `query_incr_nasty_{join,sssp,tc}`,
`query_incr_prov_e2e`, `query_incr_sssp_fuzz`, `query_incr_tc_fuzz`, `query_incr_tc_retract`,
`wql_incr_rec_agg_retract_lattice`, `wql_incr_rec_dred_error_window` were all misfiled as B and are A.

`interp` = count of `Query::run|Query::compile|.incremental()|incremental_rec()` lines.
`static arm` = the `deem` items declared in-file, or `handle` for emitted-handle calls.

| # | fixture (`tests/logos/pass/…`) | interp | static arm | class | subject / reason |
|---|---|---|---|---|---|
| 1 | `adv_rec_tc` | 2 | `tc`, `mut_odd` | B | TC/mutual-recursion 3-way: static vs `Query::run` vs naive |
| 2 | `deem_dred_od_rw_split` | 1 | `tcx` | B | DRed read/write split, interpreter vs emitted sibling; the CROSS-TIER claim is lost, the static pin survives — and as of 2026-08-09 the static pin has LIVE controls again (two `emit_scc_od_fns` perturbations, both exit 67), replacing three that pointed at the deleted `fn dred`; the compact re-aim is green and that greenness is measured, see the fixture header |
| 3 | `deem_dred_phases23_spec` | 3 | — | A | executable spec HARVESTED from `incr_rec::dred`; it drives the interpreter, so it dies with what it harvested |
| 4 | `deem_incr_diff_harness` | 10 | 8 deems + handle | B K | the differential spine; built around its own demolition |
| 5 | `deem_incr_static_retract_e2e` | 2 | 5 deems + handle | B | static min/max under retraction; interpreter is one oracle arm |
| 6 | `derive_graph_source_root_row` | 0 | `cfg_n`, `w_all`, `d_parent`/`d_key`/`d_idx` | B | virtual-root coordinate triple, 3 producers 1 consumer — **rewritten onto `dyn_graph_edge_rows` + slice deems, 0 interpreter entry points; all THREE producers survive** (C4) |
| 7 | `query_adv_errvalues` | 1 | — | A | ill-typed DYNAMIC queries are values, never crashes |
| 8 | `query_agg_sum_overflow_e2e` | 2 | `s_sum` | B | checked `sum` accumulator on all three engines |
| 9 | `query_compile_robust_e2e` | 3 | — | A | `Query::compile` robustness defects |
| 10 | `query_diff_err_e2e` | 2 | `s_ov`,`s_dz`,`s_ok` | B | static ≡ dynamic on ERROR inputs |
| 11 | `query_diff_fuzz` | 2 | — | A | 10 query shapes built as TEXT vs a FIXTURE-LOCAL naive oracle; the interpreter is the SUBJECT and the METHOD is now ported to row 12 — §5 C5 |
| 12 | `query_diff_static` | 2 | 10 deems | B | TEN shapes: 6 numeric three-way + 4 STRING shapes that are static-vs-naive only, already in post-cut form; the C5 discharge — §5 C5 |
| 13 | `query_diff_str_adv` | 11 | — | A | the STRING-column differential, 9 shapes; its four folds now have a static analogue in row 12 with an oracle that does not call `str_cmp` — §5 C5 |
| 14 | `query_dyn_bool_arith_pinned` | 2 | — | C | `deem.exec.lenient-bool-one` (docs/spec/deem.md) — the rule's only executable witness; **RE-PINNED 2026-08-09** at `tests/logos/pass/wql_domain_bool_one_tpl.logos`, which drives BOTH `B` arms (`rt_i` and `rt_f`, stdlib/mem/deem/deem.logos) through `Tpl::render` over a `WAny` (FK_ANY) column — the spec's "no fixture drives their `B` arms" is now false and the sentence is struck there, see §5 C1 |
| 15 | `query_el_arith_err_e2e` | 2 | — | A | EL arithmetic errors are values on the dynamic path |
| 16 | `query_f64_avg_nan_fuzz` | 3 | 7 deems | B | f64/avg/NaN 3-way, bit-exact |
| 17 | `query_gpath_e2e` | 3 | — | A | gpath sugar ON THE RUNTIME ENGINE; the static gpath has the `wql_*` suite |
| 18 | `query_incr_budget_e2e` | 2 | — | A | S2 budgeted fixpoint (ADR 0015 §3) — see §6 L2 |
| 19 | `query_incr_ctl_journal_e2e` | 1 | — | A | S3 control atoms + journal — §6 L3 |
| 20 | `query_f64_agg_hand_derived` | 4 | 4 deems | B | f64 aggregates, three engines, hand-derived constants |
| 21 | `query_incr_factstore_e2e_join` | 1 | — | A | FactStore → IncrJoin, two-source seam — §6 L5 |
| 22 | `query_incr_factstore_e2e_rec` | 1 | — | A | FactStore → IncrRec change capture — §6 L5 |
| 23 | `query_incr_guard` | 1 | — | A | `Query::incremental` fragment guard |
| 24 | `query_incr_join_e2e` | 2 | — | A | slice-1 DBSP join differential (`src_apply` is local) |
| 25 | `query_incr_join_fuzz` | 3 | — | A | slice-1 property fuzz |
| 26 | `query_incr_journal_replay` | 1 | — | A | journal container + `replay_fresh` — §6 L5 |
| 27 | `query_incr_nasty_join` | 3 | — | A | adversarial join deltas (`nasty_apply` is local) |
| 28 | `query_incr_nasty_sssp` | 1 | — | A | adversarial SSSP deltas |
| 29 | `query_incr_nasty_tc` | 2 | — | A | adversarial TC deltas |
| 30 | `query_incr_prov_e2e` | 1 | — | A | R4 provenance / `IncrRec::explain` — §6 L4 |
| 31 | `query_incr_s4_select_one` | 1 | — | A | S4 fork/merge over a live engine — §6 L6 |
| 32 | `query_incr_sssp_fuzz` | 1 | — | A | slice-7 recursive MIN vs Bellman-Ford |
| 33 | `query_incr_sssp_guard` | 4 | — | A | recursive-aggregate stratification guard |
| 34 | `query_incr_tc_fuzz` | 2 | — | A | slice-5/6 ±edge property fuzz |
| 35 | `query_incr_tc_guard` | 1 | — | A | `incremental_rec` fragment guard (its `rel` is query TEXT, not a `deem` item) |
| 36 | `query_incr_tc_retract` | 1 | — | A | slice-6 mark-based delete-rederive |
| 37 | `query_incr_trace_e2e` | 2 | — | A | S1 trace reification — §6 L1 |
| 38 | `query_interp_smoke` | 4 | — | A | the interpreter's runtime-unique behaviours, by construction |
| 39 | `query_lenient_e2e` | 4 | — | C | LENIENT/erased sources + Null propagation (ADR 0012-queue2 §4a); interp count RE-MEASURED 08-09 = 4 ✔; **C3 RULED WITHDRAWN, see §5** |
| 40 | `query_lenient_null_fuzz_adv` | 3 | — | C | adversarial Null-propagation differential over erased sources; interp count RE-MEASURED 08-09 = 3 ✔; **C3 RULED WITHDRAWN, see §5** |
| 41 | `query_mapping_runtime_e2e` | 2 | `s_engines` | B | dynamic query consuming a STATIC mapping; the static twin survives |
| 42 | `query_metamorphic_adv` | 1 | — | A | 10 metamorphic invariants, engine vs ITSELF — NOT a differential, so nothing is lost: measured GREEN under a total inversion of the engine's ordered compare — §5 C5 |
| 43 | `query_minmax_float_seed_leak` | 1 | `q_min`,`q_max` | B | min/max must return a value FROM THE GROUP |
| 44 | `query_observer_l1` | 2 | — | A | Nous ladder rung 1 — §6 L7 |
| 45 | `query_order_by_float_data_key` | 2 | `q_asc`,`q_desc` | B | float sort-key parity; the differential collapses to one side |
| 46 | `query_parser_robust_advX` | 6 | — | A | adversarial parser probe over `Query::compile` |
| 47 | `query_proto_observer_l0` | 2 | — | A | Nous ladder rung 0 — §6 L7 |
| 48 | `query_rec_agg_batch_e2e` | 3 | `sssp`,`longest` | A | regression for a DYNAMIC executor defect; the static arm is the oracle and duplicates `wql_*` coverage (salvageable) |
| 49 | `query_rtval_ops_e2e` | 0 | — | C | RtVal operator/builtin semantics through `Tpl` — §6 L8 |
| 50 | `query_run_errors_e2e` | 3 | — | A | query errors are values, compile-phase vs run-phase |
| 51 | `query_tail_order_adv` | 2 | 8 deems | B | order/limit/having, POSITIONAL 3-way |
| 52 | `query_tpl_udf_e2e` | 0 | — | C | template/query UDF registry unification — §6 L8 |
| 53 | `query_trama_arith_err_e2e` | 0 | — | C | `Tpl::render` arithmetic errors are values — §6 L8 |
| 54 | `query_trama_dynamic_e2e` | 0 | — | C | dynamic Trama end to end — §6 L8 |
| 55 | `query_trama_typecheck_e2e` | 0 | — | C | the runtime template CHECKER — §6 L8 |
| 56 | `query_tree_source_e2e` | 0 | 12 deems | B | virtual graph sources — **rewritten: every leg is now a `deem` written TWICE, over `&Writ` and over a `dyn_graph_edge_rows` SLICE, so a single-sourced file became a two-producer differential. The two bound anchors (`root`, `start`) became document-independent query anchors (`parent == 0`; seed on `key == "db"`). ONE LOSS, named: leg (f), `Query::incremental_rec` refusing a tree source — its subject IS `bind_source_tree`, so guard and guarded die together** (C4) |
| 57 | `query_tree_source_graph_e2e` | 0 | 14 deems | B | DAG / cycle / leaf-identity / TOM semantics AND the query capabilities over the walk — **rewritten: join, `rel` recursion over a CYCLIC edge set, `order by` and `group by … aggregate` all preserved as slice-sourced deems beside their `&Writ` twins. A `Vec` alone would NOT have replaced them; `DynEdge` + `dyn_graph_edge_rows` is why they survive. 0 interpreter entry points** (C4) |
| 58 | `query_u64_ordw_origin` | 1 | — | C | `ordw` under/over-carry at the aggregate out-name — **BOTH HALVES DISCHARGED**: the under-carry by `tests/logos/pass/wql_domain_static_ordw_origin.logos` (blocks 10s/11s/20s), the over-carry as a REFUSAL WITH ITS GROUND by `tests/logos/fail/wql_cond_branch_types_fail.logos` (`check_cond_branches` names the clause and both columns). ⚠ RE-MEASURED 2026-08-09: the `wql_domain_static_ordw_origin` header still described the over-carry as an ungrounded host-compiler anti-diagnostic and the computed u64 key as impossible — both were true when written and neither is true now; the header is corrected in the same commit |
| 59 | `vfy_nan_key_probe` | 5 | — | C | PROVENANCE of the f64 refusals (which stage refused, with what message) — ⚠ THE ROW WAS UNDER-PRICED. The file also held the ONLY pin on `rt_eq`'s surviving float arm, which its own header named as the thing a future canonicalising 'fix' would break unseen. The provenance half really died with `stdlib/mem/deem/check.logos`; the behaviour half did not, and was re-pinned at row 86 |
| 60 | `wql_agg_avg_bool_value_rule` | 4 | `q_avg` | B | `avg(bool)` VALUE RULE on the one surviving engine (the "three engines" claim died with `Query`). INDEPENDENT ARM LANDED (§8c): `ref_avg_bool` + a second, INTERLEAVED corpus with no hand constants |
| 61 | `wql_domain_carrier_positions` | 25 | — | C | one law: the carrier at EVERY position that computes or compares a column integer. The deleted file lettered FOURTEEN positions; **the enumeration and its verdicts are now written out in §5 C1** rather than left as "the rest". R/B/G by `tests/logos/pass/wql_arith_u64_tower_e2e.logos`; A/C/D/E/F/H/I/M by `tests/logos/pass/wql_domain_static_carrier_positions.logos` (**BUILT 2026-08-09**, the file this row asked for through three rounds); L as a REFUSAL with its ground (`tests/logos/fail/wql_arith_mixed_tower_fail.logos`); J and K were the dynamic tier's OWN positions and go with it |
| 62 | `wql_domain_incr_disagreement` | 9 | — | C K | ⚠ **"THREE DISAGREEMENTS" WAS ALREADY WRONG WHEN THE ROW WAS WRITTEN, RE-MEASURED 2026-08-09 off the pre-deletion file**: blocks 1/1b (`max` over u64) and 2/2b (`sum` over f32) carry CLOSED dates IN THE FILE (2026-08-05, 2026-08-04) and block 4 is a shape floor its own text says is not a type fact, so exactly ONE disagreement was live — 3/3b, an equi-join on an `f64` key. **TRANSCRIBED 2026-08-09** onto `no_join_f64key` (`pass/wql_incr_eligibility_matrix` + `incr_eligibility_gate.sh`), and the transcription CHANGED WHICH PAIR OF TIERS CARRIES IT: the deleted block was DYNAMIC-accepts vs INCREMENTAL-refuses; what is pinned now is STATIC-accepts vs INCREMENTAL-declines. Same fact about the f64 key, different witnesses — see §5 C1 |
| 63 | `wql_domain_runtime_extremes` | 3 | — | C | 18 types round-tripped through the dynamic tier; the static twin `tests/logos/pass/wql_domain_static_extremes.logos` covers all 18 plus i128/u128 — **K DROPPED 2026-08-09**, the file's own header says no block asserts a wrong value any more (the `f32` one closed 2026-08-04) |
| 64 | `wql_domain_runtime_order_a` | 6 | — | C | `order by` — i8/i16/i32/i64/isize/i24; static twin `tests/logos/pass/wql_domain_static_extremes.logos` (its `ord_T` items ask the same permutation of emitted code) — **K DROPPED 2026-08-09**, see below |
| 65 | `wql_domain_runtime_order_b` | 6 | — | C | `order by` — i56/u8/u16/u32/u24/u56; static twin `tests/logos/pass/wql_domain_static_extremes.logos` — **K DROPPED 2026-08-09**, see below |
| 66 | `wql_domain_runtime_order_c` | 6 | — | C | `order by` — u64/usize/f32/f64/bool/str (both defects closed here); static twin `tests/logos/pass/wql_domain_static_extremes.logos` |
| 67 | `wql_domain_u64_order_seams` | 9 | — | C | the three INTERMEDIATE facts of the u64 order fix — **ALL THREE NOW HAVE ARMS**: the SEEDS and the COMPARE at blocks 30s/40s of `tests/logos/pass/wql_domain_static_ordw_origin.logos`, and the PLUMB at `tests/logos/pass/wql_domain_catalog_ord_plumb.logos` (**BUILT 2026-08-09**). ⚠ The plumb was recorded as unaskable and that was WRONG, not merely stale: `SchemaCatalog::field_ord_wrap` lives in `stdlib/mem/deem/deem.logos`, survives P5, is `pub`, and its own docstring says it is public so a fixture can pin exactly this. The reason given — that `el_ty_stored_of` is module-private — is true (still `fn`, `stdlib/mem/wql/el.logos`) and was about the STATIC tier's carrier, which is a different question from the one row 67 asked |
| 68 | `wql_engine_source_e2e` | 1 | 4 deems over `&IncrRec` | D | static `deem` whose SOURCE is the engine — §1b |
| 69 | `wql_graph_float_root_vi` | 0 | `root_vi`,`root_kind`,`root_vi_dyn` | B | float-rooted document: static vs dynamic walker — **rewritten onto `dyn_graph_edge_rows` + a slice deem, so the `parent == 0` filter stays a QUERY and not an `if`; 0 interpreter entry points** (C4) |
| 70 | `wql_graph_null_root_row` | 0 | 8 deems | B | root row + **THE WALKER'S ARMS**: one vocabulary, two walkers — rewritten onto `dyn_graph_edges` and then **WIDENED (C4-finish) with THREE container-rooted documents** (map root over a DAG+cycle graph with a TOM and an array child and two equal leaves under different parents; array root; TOM root). Measured: the two-document form stayed GREEN under both a `ts_row`-salt perturbation and a severed `ts_descend` — it never entered `ts_walk`. Now row-for-row parity with `writ_graph_edges` on all three (C4) |
| 71 | `wql_graph_root_id_cross_document` | 0 | 5 deems | B K | ⚠ tripwire recording an OPEN root-id defect — **rewritten: the cross-document join is now a two-SLICE deem over two `dyn_graph_edge_rows` walks, so the defect keeps BOTH binding times after the cut; it is still a JOIN, not a hand comparison** (C4) |
| 72 | `wql_incr_rec_agg_retract_lattice` | 1 | — | A | REGION 4 harvest — lattice head over a recursive rel under retraction; drives the interpreter |
| 73 | `wql_incr_rec_dred_error_window` | 1 | — | A | REGION 5 harvest — partially-applied-retraction window |
| 74 | `wql_incr_retract_two_ways` | 4 | 4 deems + handle | B | the static retract surface, checked three ways |
| 75 | `wql_incr_static_two_ways` | 2 | `q` + handle | B | the static incremental aggregate handle |
| 76 | `wql_mapping_rules_escape_e2e` | 3 | `s_pg` | B | `<M>__rules()` literal pinned byte for byte |
| 77 | `wql_native_graph_e2e` | 1 | 3 deems | B | native object graph as a `deem!` source |
| 78 | `wql_u64_capability_matrix` | 5 | 10 deems | B K | THREE tiers disagree about what they will answer; becomes a two-tier file |
| 79 | `wql_u64_sum_accumulator` | 10 | — | C | the `sum` accumulator seam over u64, both failure directions — static arm BUILT 2026-08-09 (`tests/logos/pass/wql_domain_static_u64_sum_accumulator.logos`) for the two cells nothing else asked; the rest is covered and the coverage claim is RE-DERIVED below, not inherited |
| 80 | `wql_u64_sum_scalar_arith` | 7 | — | C | the scalar-arith seam one level below, one cell per operator — **static arm BUILT 2026-08-09** (`pass/wql_arith_u64_tower_e2e`), see §5 C1 |
| 81 | `wql_value_domain_measured` | 5 | `sg_sel`,`su_order` | B | the value domain across three engines; becomes two |
| 82 | `tests/logos/rtval_domain_gate.sh` | 1 | — | G | §7 |
| 83 | `query_incr_factstore_unit` | 0 | — | A | slice-8 `FactStore` SET semantics + effective-delta emission, WITHOUT the engine — §6 L5 |
| 84 | `query_incr_factstore_epochs` | 0 | — | A | the `FactHistory` epoch-history layer (ADR 0017 P1) — §6 L5 |
| 85 | `query_incr_factstore_float_identity_unit` | 0 | — | A | `FsKey` content identity under the PostgreSQL float ruling — §6 L5 |
| 86 | `query_tpl_float_eq_identity` | 0 | — | A | `rt_eq`'s FLOAT arm at the only position that still reaches it (`Tpl::render`'s `OP_EQ`/`OP_NE`). Written AFTER the cut, because row 59 was priced for its provenance half and its surviving-behaviour half went unpinned — see row 59 |

Totals: **A 39 · B 26 · C 19 · D 1 · G 1** = 86. K flag on 4 files (4, 62, 71, 78).

⚠ **THE K FLAG ON ROWS 63, 64 AND 65 WAS STALE AND IS DROPPED (2026-08-09).** It was
re-derived rather than inherited. `K` means the row's fixture asserts an answer that is KNOWN to be
wrong, beside a fixed-signature arm that fires when the defect closes. Measured, per file:
`tests/logos/pass/wql_domain_runtime_extremes.logos` opens "NO BLOCK HERE ASSERTS A WRONG VALUE ANY
MORE" (its `f32` block closed 2026-08-04); `tests/logos/pass/wql_domain_runtime_order_c.logos` opens
"NO BLOCK IN THIS FILE ASSERTS A WRONG ORDER ANY MORE" (f32 2026-08-04, u64/usize 2026-08-05) — and
that file is row **66**, which was never K-flagged. In `tests/logos/pass/wql_domain_runtime_order_a.logos`
and `tests/logos/pass/wql_domain_runtime_order_b.logos` the string `KNOWN-WRONG` occurs ONLY in the
family-header prose describing the idiom and in the exit-code legend ("+1, on KNOWN-WRONG columns
only"); NO block body carries one, and neither file can: parts a and b hold the signed widths and the
unsigned widths BELOW 64 bits, where the signed compare and the column's own compare agree by
construction. Both defects this family ever carried lived in part **c**. The old totals line
overstated the arc's open defects by three.

⚠ The totals line previously read "A 31 · B 25", and the class column of the table it summarises has
never said that. Counted off the rows at `e53962b6` — before rows 83–85 were added — the table is
**A 32 · B 24**, so the summary was wrong in BOTH directions and had been since the B→A correction
above was applied to the rows and not to the total. This is the third drift found in this file and the
first one found by a machine: §9 FACT 4 now derives these five numbers from the class column and refuses
to let the summary and the rows tell two stories.
(Rows 83–85 have interp count 0 and no `deem` item: they reach the cut through `FactStore`/`FactHistory`
directly, which is exactly why the entry-point grep could not see them.)

⚠ The class column is a judgement over a measured signature (interp count, declared `deem` items,
emitted-handle calls) plus each file's own header block, which every one of the 84 fixtures carries and
states its subject in. It is NOT a full read of 85 files, and §9 does not check it — the pin decides
existence and arithmetic, never a judgement. The B rows in particular still owe the per-fixture
"does what is left still bite" check that no one has done. Rows 11, 13 and 42 have now had that read
(see §5 C5); all three left class C — 11 and 13 because the METHOD was ported into row 12 and measured
to bite there, 42 because it never had an oracle to lose.

---

## 4. What survives on the static side

Named, so the next attempt does not have to re-find them:
`stdlib/mem/wql/` keeps the whole compile path — `stdlib/mem/wql/el.logos` (`el_ty_stored`, `el_ty_stored_of`,
`el_wrap_ord_key`), `params.logos` (`stamp_rel_incr_shape`, `native_use_at`), `rexpr_walk.logos`
(`emit_scc_od_fns`), `writ_graph.logos` (`writ_graph_edges`), `trama_render.logos`,
`stdlib/mem/wql/codegen.logos` (a second file of that basename lives under `tools/`, so this one is written with its path), `lower.logos`, `plan_walker.logos`, `catalog_macro.logos`.
`stdlib/mem/deem/deem.logos` keeps `RtVal`,
`rt_kind`, `rt_eq`, `SchemaCatalog`.

The static emitted surface is `<q>_apply(&mut h, __src, __w, <params>)` / `<q>_retract` /
`<q>_epoch` / `<q>_snapshot` over caller-supplied typed delta rows. There is no presence set, no content
keying, no arena and no journal on that side — which is why `FactStore` (defined in `incr.logos`) has no
static counterpart to move to.

---

## 5. Class-C requirements — what the static tier would have to GAIN

Stated as symbols, so each is a task and not a wish.

**C1 — the value domain in the dynamic tier** (rows 14, 58, 59, 61, 62, 63, 64, 65, 66, 67, 79, 80).
These pin that a column's own type governs comparison, ordering and accumulation at every position.
The static tier already has the type: `el_ty_stored` / `el_ty_stored_of` (`stdlib/mem/wql/el.logos`) and
the wrap key `el_wrap_ord_key`. What was missing is a STATIC fixture family that asks the same question of
emitted code at the same seams — order key, aggregate accumulator, scalar arithmetic inside an aggregate
argument, and the aggregate OUT-NAME's signedness.

⚠ **PART OF C1 WAS NOT A FIXTURE GAP AT ALL, AND THAT IS THE CORRECTION THIS ENTRY NEEDED.** The
paragraph above assumed every C1 cell was a witness the static tier could already produce and simply had
not been asked for. Rows 61 and 80 — the scalar-arithmetic seam over a `u64` column — were not: the
static tier could not compute them. MEASURED (grep over `stdlib/` and `src/`, both spellings): the EL's
unsigned set was `el_addu` / `el_subu` / `el_mulu` only, written for an aggregate ACCUMULATOR, and
`el_divu` / `el_remu` **did not exist anywhere**. Two of the five operators the EL's grammar has had no
unsigned form, `el_arith_ok` returned `ElTy.fits` and therefore refused every `u64` arithmetic node, and
`infer_emit_ty` laundered every arithmetic node to the class representative `i64`, so a computed `u64`
key was refused twice over. **CLOSED 2026-08-09**: `el_arith_tower` / `el_tower_join` / `el_tower_repr`
(stdlib/mem/wql/el.logos) plus `arith_operand_tower` / `arith_node_tower` (stdlib/mem/wql/codegen.logos) give the EL a second integer
tower; `el_divu` / `el_remu` were written; `el_int_op_fn` takes the tower. Witnesses:

* `tests/logos/pass/wql_arith_u64_tower_e2e.logos` — division, remainder, a computed `order by` key, the u64
  overflow / underflow / div-by-zero arms and a `sum` over an arithmetic argument, all at 2^63 and
  2^64−1 where the signed reading gives a **different** answer, with an i64 control block;
* `tests/logos/fail/wql_arith_wide_int_fail.logos` — rewritten to `u128`, the only remaining towerless case;
* `tests/logos/fail/wql_arith_mixed_tower_fail.logos`, `tests/logos/fail/wql_arith_u64_neg_fail.logos` — the two refusals the
  second tower creates (a node mixing signednesses; unary `-` over an unsigned value);
* `tests/logos/fail/wql_cond_branch_types_fail.logos` — row 58's over-carry half, which reached the HOST
  compiler as `if-expression branches have incompatible types: u64 vs i64` from `sema_expr.cpp`, naming
  neither clause nor column and pointing at a synthesised blob. `check_cond_branches` (stdlib/mem/wql/codegen.logos)
  now refuses first, naming the clause and both columns.

**Row 62 is TRANSCRIBED, not discharged.** Its live `⚠ KNOWN-WRONG` (block 3b: an equi-join on an `f64`
key is accepted by the dynamic and static tiers and refused by the incremental one) had both accepting
halves inside `Query::compile`. The pin now also lives on the pair that survives the cut:
`no_join_f64key` in `tests/logos/pass/wql_incr_eligibility_matrix.logos` (the static batch answers it,
asserted on the group totals) plus the matching `declined` row in `tests/logos/incr_eligibility_gate.sh`,
whose control is `ok_join` — the same shape, i64 key, EMITTED. It fails-if-fixed: admitting an f64 join
key makes the query trace EMITTED and reds both the verdict row and the DERIVED retract count.

**THE STATIC-SIBLING TASK LIST, RE-MEASURED 2026-08-09.** This paragraph used to name six wished-for
files. Three of them should never be built and two of the remaining questions turned out to be answered
elsewhere; what was left is one fixture, and it exists. Each verdict below was measured against the
tree, not carried forward:

- `..._static_order_a` / `_b` / `_c` (rows 63–66) — **STRUCK, the question is already answered.**
  `tests/logos/pass/wql_domain_static_extremes.logos` declares twenty `struct S_T` + `pub deem sel_T` +
  `pub deem ord_T` triples — the eighteen types rows 64/65/66 name, plus `i128`/`u128` — and each `ord_T`
  is `select s.id order by s.x` over the same 1,3,0,2 placement the runtime family uses. Three more files
  would each red for a fact that file reds for first, which is noise, not coverage. The citation now
  lives in rows 63–66 where FACT 1 pins it.
- `..._static_u64_sum_accumulator` (row 79) — **BUILT**, `tests/logos/pass/wql_domain_static_u64_sum_accumulator.logos`,
  and it is deliberately small. The inherited claim "largely covered by `wql_agg_wide_int_arg_e2e` blocks
  A/B/C/E" was re-derived block by block: blocks 1/3/4/5 of `tests/logos/pass/wql_u64_sum_accumulator.logos`
  are indeed covered there, block 7 (min/max over u64) by
  `tests/logos/pass/wql_tier_capability_disagreement.logos` block G, and the widths by
  `tests/logos/pass/wql_agg_arg_seam_e2e.logos` — but only at {10, 20, 40}, a total that FITS `u8` and so
  discriminates nothing. The two cells nothing asked are the new file's subject: `order by sum(u64)` (the
  aggregate out-name of `sum` carrying its argument's unsigned claim, with a `max` control and an i64
  twin over the same 64 bits producing the OTHER permutation) and the narrow widths at a total that
  leaves the column (`sum(u8)` of {200,100,0} = 300, `sum(u32)` of {4e9,4e9,0} = 8000000000).
- ⚠ **AND ROW 79's "BOTH FAILURE DIRECTIONS" DOES NOT MEAN WHAT THE SENTENCE SUGGESTS.** The obvious
  static shape for the under-carry — one epoch of {5}, then `<q>_retract` of {9} over a `u64` `sum` —
  returns `Err` and leaves the snapshot at 5, and it is NOT the borrow: MEASURED 2026-08-09 the arm is
  `ElError::RetractAbsent`, and the i64 TWIN of the same call, where 5 − 9 = −4 needs no borrow at all,
  returns `RetractAbsent` too. Retraction refuses any row the accumulator never folded and a sum over
  folded rows cannot exceed the total, so `el_subu`'s borrow is unreachable through that surface — which
  is exactly what `tests/logos/pass/deem_incr_static_retract_e2e.logos` block E and
  `tests/logos/pass/wql_incr_retract_three_ways.logos` F1/F2 already pin BY ARM NAME, with `Underflow`
  named there as the WRONG answer and the borrow asserted at `el_subu` where it lives. Pinning that `Err`
  as "the under-carry" would have recorded a cheap refusal as an expensive one.
- Rows 14, 59 and 61 were the remaining task list. **ALL THREE ARE NOW BUILT** (59 at row 86; 14 and 61
  on 2026-08-09), and their names have moved into their rows, where FACT 1 can see them. A name in this
  paragraph is a REQUIREMENT, not a description, so nothing is left here.

**ROW 61 — THE FOURTEEN POSITIONS, ENUMERATED AND EACH GIVEN A VERDICT (2026-08-09).** The row named "one
law at EVERY position" and the round before this one could only say "the rest". The deleted file's blocks
ARE the enumeration; it was recovered from `e1dd0ac5^` and every verdict below was MEASURED against the
built tree, not read off the old file:

| | position | verdict |
|---|---|---|
| R | aggregate ARGUMENT (the reference) | `tests/logos/pass/wql_arith_u64_tower_e2e.logos` — `summed` |
| A | the WHERE filter | `tests/logos/pass/wql_domain_static_carrier_positions.logos` block A |
| B | the PROJECTION cell | `tests/logos/pass/wql_arith_u64_tower_e2e.logos` — `divd`/`remd`/`plus1`/`minus1`/`dbl` |
| C | the GROUP KEY, computed | `wql_domain_static_carrier_positions` block C |
| D | HAVING over a computed aggregate | `wql_domain_static_carrier_positions` block D |
| E | a TERNARY in a projection | `wql_domain_static_carrier_positions` block E |
| F | a BOOL cell in a projection | `wql_domain_static_carrier_positions` block F |
| G | the SORT KEY | `tests/logos/pass/wql_arith_u64_tower_e2e.logos` — `by_div` |
| H | the JOIN KEY, computed | `wql_domain_static_carrier_positions` block H |
| I | a UDF ARGUMENT | `wql_domain_static_carrier_positions` block I. ⚠ RE-MEASURED: the first probe used a UDF with a `u64` RETURN and was refused, which would have entered this table as "position I is refused" — the refusal is about the RETURN (pinned separately at `tests/logos/fail/wql_udf_wide_int_ret_fail.logos`) and says nothing about the argument. With an `i64` return the argument position is ADMITTED and computes unsigned |
| J | the TEMPLATE tier | the dynamic tier's own position; gone with it |
| K | the INCREMENTAL tier | the dynamic tier's own position; gone with it |
| L | the claim is DROPPED where operands disagree | REFUSED — `tests/logos/fail/wql_arith_mixed_tower_fail.logos`, which names both columns and both signednesses |
| M | the bit-exact positions over a COMPUTED key | `wql_domain_static_carrier_positions` block M |

⚠ **AND THE FEASIBILITY OF A/C/D/E/F/H/M WAS A PREDICTION UNTIL IT WAS COMPILED.** The reasoning that
`arith_operand_tower` / `arith_node_tower` (`stdlib/mem/wql/codegen.logos`) are per-NODE and
clause-agnostic, so every clause should reach the u64 tower by one route, is correct — but it was
SOURCE-DERIVED, and a clause with its own type path would have turned this slice into a codegen change
rather than a fixture. It was falsified cheaply first, by compiling all nine candidate spellings in one
throwaway file: EIGHT compiled and answer the unsigned values (I only after the probe was re-asked with an
`i64` return, see its row), and the one that does not is a spelling of E recorded as a gap below.

⚠ **A CAPABILITY GAP MEASURED HERE AND DELIBERATELY NOT PINNED.** `select (s.p > 0 ? s.u : 0u64)` is
REFUSED: `wql!: select: the two branches … have different types — s.u is u64 and 0 is i64`. The message is
well-grounded in form (it names the clause and both operands) and its CONTENT is false about the source —
the literal is written `0u64`. The cause is one asymmetry: `arith_operand_tower` gives an integer literal
`EL_TOW_NONE` — NEUTRAL, taking its partner's tower — while `check_cond_branches` asks `infer_emit_ty`,
which has no neutral-literal rule and answers `i64`. So the ternary lacks the join rule the arithmetic
already has. It is NOT pinned in a `fail/` fixture on purpose: pinning that `.expected` would ratify the
sentence "`0` is `i64`" about a source that says `0u64`, which is the same mistake as pinning a host
anti-diagnostic. The spelling the deleted file actually used was the UNMIXED one (`(s.p ? s.u : s.u)`), so
no capability was withdrawn — this is a new gap and it is stated here rather than asserted anywhere.

**C2 — runtime templates** (rows 49, 52, 53, 54, 55). There is no static substitute and none was ever
designed: `stdlib/mem/wql/trama_render.logos` is the metaprog-side sibling and says so, naming the
`Tpl::compile` runtime as the other half. To keep the capability, `Tpl` / `Tpl::compile` / `Tpl::render`
plus their dependencies (`check_stmts`, `cbinds_new`, `simplify_all`, `chk_err`, `render_stmts`,
`rbinds_new`, `eval_sexpr`, `RBinds`, `Chk`) had to be MOVED out of `eval.logos`/`stdlib/mem/deem/check.logos` into a
template-only module before either file is deleted. That is a port, not a triage. **DONE at `8c5ad0ea`**
— `stdlib/mem/deem/tpl.logos`, plus `eval_sx` (which this list missed; it lived in `exec.logos` and is
the sole `RtVal::Error` → `chk_fail_p` boundary) and `check_expr` / `check_root` / `src_elem_ty` /
`bin_op_name` / `chk_new` / `chk_fail` / `chk_bad` / `chk_fail_p` / `sx_of` / `CB_CAP` / `impl CBinds`.
Rows 49, 52, 53, 54, 55 are therefore no longer C-class losses.

**C3 — lenient / erased sources** (rows 39, 40) — **RULED: WITHDRAWN, and the withdrawal is now PINNED.**
`QEnv::bind_source_erased` and `QEnv::bind_node_erased` (in `deem.logos`, which survives) have no caller
after the cut and no static analogue: a `deem` item's source is a typed slice, so there is nothing to be
lenient about.

*What was measured, 2026-08-09* (each claim re-grepped, since this document is pinned by no gate):

- **The capability is LIVE but TEST-ONLY.** `bind_source_erased` / `bind_node_erased` have ZERO callers in
  `stdlib/` and ZERO in `src/`; the only call sites in the tree are five, in exactly the two fixtures this
  row names — `tests/logos/pass/query_lenient_e2e.logos` and
  `tests/logos/pass/query_lenient_null_fuzz_adv.logos`. So "no caller after the cut" is true, but
  "already dead code" would be FALSE: the cut withdraws a tested capability, not an unused one.
- **Both symbols are ABI-exported** (`abi/logos.abi`: `QEnv__bind_node_erased__f__refmut_QEnv__slice_u8__WAny`,
  `QEnv__bind_source_erased__f__refmut_QEnv__slice_u8__WAny`). Removing the methods is an ABI BREAK —
  authorised, but it must be a deliberate regenerate + version bump, not a side effect of deleting a file.
- **Why no static form exists, and it is two missing features, not a slice.** (i) There is no erased COLUMN:
  `SemaChecker::native_source_spec` stamps every rel column type concretely from the `Src` impl's trait
  args, and `sema_collect.cpp`'s rel-column check refuses any column type that does not implement `Hash`
  (`rel item(v: WAny)` → ``a rel column type must implement `Hash` ``, exit 1). With no `dyn` column there
  is nowhere for the CEL Null table (`docs/spec/deem.md`) to attach. (ii) There is no runtime BY-NAME field
  resolution: field access is typed against the concrete column type.

*The ruling.* CEL-style Null propagation over string-keyed rows is **withdrawn from the language** with the
interpreter. It is not deferred and not "unspecified": a `deem` item whose source parameter carries an
erased Writ slot is now REFUSED at the item, with the ground stated —
`SemaChecker::enrich_deem_params` (`src/compiler/sema_expr.cpp`), predicate `names_erased_writ_slot_`.
Doors: `tests/logos/fail/deem_erased_source_fail.logos` (`&[WAny]`, the `bind_source_erased` shape) and
`tests/logos/fail/deem_erased_node_fail.logos` (`&WAny`, the `bind_node_erased` shape).

⚠ *Why those doors are not the worthless kind.* Before the check both programs ALREADY failed, and neither
refusal was evidence about erasure: `&[WAny]` died at `<metaprog-blob-subst>:1` with `field read: receiver
is not a struct (got &WAny)` — unlocated, grounded in a rule about structs — and `&WAny` died with the
stdlib walker's ``source `t` is not a slice``, a statement about SHAPE that covers `t: &i64` equally. Each
`.expected` therefore pins the LINE of the offending item plus the ground clause (compile-time column
typing; the withdrawn binding named). MEASURED both ways: perturbing the predicate in the compiler reddens
both doors, and so does a perturbation that withdraws the whole `deem` ITEM form — the second is the check
that a broader refusal cannot satisfy them.

*What is NOT claimed (as of the ruling).* The `QEnv` methods and their two pass fixtures were still present;
the ruling stated the disposition and pinned the static tier's answer. Deleting them was P5's own step, with
the ABI bump above.

**C3 CLOSED, 2026-08-09 (TASK 24) — the plumbing is removed, and one sentence above was already stale.**
The two pass fixtures went with the cut (`query_lenient_e2e` / `query_lenient_null_fuzz_adv` are §3 rows 39
and 40, both GONE-FIXTURE), so at the time of this step the erased binds had ZERO callers of ANY kind. What
was removed, and what was proved before removing each piece:

- **Three `QEnv` methods** — `bind_node_erased`, `bind_source_erased`, `bind_source_tree`
  (`stdlib/mem/deem/deem.logos`). Zero non-comment references under `stdlib/`, `tests/`, `src/`, `tools/`;
  every surviving mention is a comment, a fixture header note, or the compiler's own refusal MESSAGE.
- **The kind codes they alone wrote** — `QB_ENODE` / `QB_ESRC` / `QB_TSRC`. Proved by ENUMERATION, not by
  sampling: `deem.logos` has exactly ten `self.kinds[i] =` sites, one per `bind_*` method, and after the
  three removals no site writes any of the three values. The numbers are NOT re-packed.
- **The reader arms in `stdlib/mem/deem/tpl.logos`** — the two `env_val` arms, the three `check_root` arms
  (`QB_ENODE -> CT_DYN`, `QB_ESRC -> CT_DYNSRC`, `QB_TSRC -> CT_TREESRC`), and the CASCADE those two tags
  left behind: `CT_DYNSRC`/`CT_TREESRC` had no other producer, so the collection-edge refusal drops those
  two disjuncts (keeping `CT_ARR`/`CT_SARR`, which the catalog produces), `ct_is_obj` drops `CT_DYNSRC`,
  and the `TStmt::For` arm drops it too.
- ⚠ **`CT_DYN` WAS KEPT, and keeping it is the measured part.** It has a producer INDEPENDENT of erased
  binding — the catalog `FK_ANY` field arm of `check_expr` (a `WAny` column on a strict schema) — so the
  `CT_DYN` field read, the `dyn_op` arithmetic, the ternary arm and the `For` arm's `CT_DYN` branch all
  stay reachable. Deleting them would have been the "reader arm something else can still reach" failure.
- ⚠ **The walker was NOT touched, and that it CANNOT have been is measured, not assumed.**
  `stdlib/mem/deem/graphsrc.logos` names `QEnv` only in comments, `use`s no module that carries it, and its
  entry points `dyn_graph_edges` / `dyn_graph_edge_rows` take a raw `WAny` root (`ts_scan` takes the root
  WORD). `bind_source_tree` was one way IN to that walker under the old engine, not the walker.

*ABI, with the differ CALIBRATED first.* Three exported records go —
`QEnv__bind_node_erased__f__refmut_QEnv__slice_u8__WAny` and its `bind_source_erased` and
`bind_source_tree` twins (`QEnv__bind_edge_rows__…` SURVIVES). Calibration, before trusting any verdict:
`QEnv::bind_edge_rows` was removed IN THE SOURCE, the stdlib rebuilt, and the emitted spec compared with
the committed one — `sym` 12061 → 12060, `--abi-diff` printed exactly that one record as `[BREAKING]` and
exited **1**; the plant was then restored and the tree re-measured clean. So the emitter really does drop a
record when a method leaves, and the differ really does read that as breaking. Version 0.38.0 → **0.39.0**.
⚠ `MIN_SYM` in `scripts/abi-check.sh` sat EXACTLY at 12061 — the post-P5 measurement — so a three-record
removal trips the "this is not a blob" floor. It is lowered to the newly measured 12058 with the reason
recorded there, exactly as P5 lowered it from 12368.

*Adjacent deadness NOT caused by this step, recorded so nobody later credits it here.* `QEnv::bind_edge_rows`
still writes `QB_EDGE`, and `QB_EDGE` has no reader at all — its reader was the `QB_EDGE` arm of
`exec_rexpr`, cut at P5 with `exec.logos`. `CT_TREEROW` likewise has no producer since that same cut. Both
were already dead BEFORE this step and are left in place; folding them in would have let a later reader
believe the erased-bind removal killed them.

**C4 — the dynamic graph walker** (rows 56, 57, and the loss half of B rows 6, 69, 70, 71).
**DISCHARGED 2026-08-09 in two rounds — and the requirement as originally written was WRONG, so it is
restated here rather than ticked off.** The graph vocabulary (parent, key, idx, child, kind, tag, vi, vs) is
declared ONE vocabulary across binding times with exactly two implementations — `ts_scan` and
`writ_graph_edges`. Delete one and the parity claim is unfalsifiable.

What the old text asked for — "move `ts_scan` (and `QB_TSRC` / `CT_TREESRC` handling) into a source module
independent of the executor" — was insufficient AND partly already true, both measured:

* `QB_TSRC` (`deem.logos`, `const QB_TSRC`) and `QEnv::bind_source_tree` were ALREADY in the surviving
  file; nothing about them needed moving. `CT_TREESRC` appears only in type-checking (`stdlib/mem/deem/check.logos`,
  `tpl.logos`) and never in the walker.
* Moving the FUNCTION would have saved nothing. `ts_scan`'s only caller is the `RExpr::Scan` /`QB_TSRC`
  arm of `exec_rexpr` (`exec.logos`, cut), and every one of the six fixtures reached it through
  `bind_source_tree` + `Query::compile`/`Query::run` — `Query` is in `query.logos`, also cut. The parity
  claim was held by the INTERPRETER, not by the walker. **Measured** by severing that arm
  (`return rs_new();` in place of the `ts_scan` call) on the ported tree: five of the six fixtures went
  red and only the rewritten one stayed green.

DONE: `ts_scan` and its closure (`ts_edge_row`, `ts_row`, `ts_descend`, `ts_walk`, `ts_is_container`,
`ts_kind`, `TS_NCOLS`, `es_scan` — which came BACK in the second round, see (3) below — plus
`RowSet`/`rs_new`/`RS_VARS` and `h_step`) moved byte-identically
out of `exec.logos` into `stdlib/mem/deem/graphsrc.logos`, which is NOT in the cut, and a public entry
point `dyn_graph_edges(root: WAny, scratch: &Writ) -> Vec<WritEdgeRow>` was added there — the same row
type `writ_graph_edges` returns, so the two producers compare directly with no query engine between them.
Row 70 (`wql_graph_null_root_row`, the fixture that pins all EIGHT columns on two documents) was rewritten
onto it and survives the cut.

**C4 FINISHED 2026-08-09 (second round). Three things landed, and the first is the one that mattered.**

**(1) THE ROUTE WAS ONE ROW WIDE, AND THAT WAS MEASURED, NOT SUSPECTED.** Row 70 — the fixture the first
round rewrote and called the surviving parity claim — binds two documents whose roots are a NEVER-SET
(null) root and `d2.int(7)`. Both are NON-CONTAINERS, so `ts_walk` takes no arm at all: `ts_row` is never
called, `ts_descend` is never called, and the only row on either side is the virtual root edge `ts_scan`
emits directly. Every arm of the walker (`is_map` / `is_array` / `is_tinymap`), the `seen` expansion-once
set and the leaf-id salt were sensed ONLY by rows 56 and 57, which reach the walker through
`Query`/`bind_source_tree` and die in P5. The cut would have left the surviving walker's whole body
unsensored behind a file that calls itself the parity fixture.

Row 70 is now WIDENED with three container-rooted documents — a MAP root over a hand-built object graph
(a shared child reached from two parents, a `back` edge closing a cycle, a TOM child with SPARSE key
codes 0/3, an array child, and two EQUAL scalar leaves under DIFFERENT parents), an ARRAY root, and a TOM
root — each asserted against hand-derived facts AND row-for-row against `writ_graph_edges` on all eight
columns.

**Proved by perturbing the ENGINE where the thing lives** (`stdlib/mem/deem/graphsrc.logos`), predicting
the code, and reverting. Before the widening BOTH of the first two left row 70 green:

| perturbation | row 70 | row 57 | row 56 | rows 6/69/71 |
|---|---|---|---|---|
| `ts_row` passes a constant parent word instead of `parent.raw()` | 112 (parity, `parent`) | 4 | 25 | green |
| the FNV SALT alone: `h_step(hh, parent_word)` → `h_step(hh, 0)` | 115 (parity, `child`) | 7 | 31 | green |
| `ts_descend` severed (`if true { return; }`) | 100 (15 rows → 5) | 2 | 3 | green |

Rows 6/69/71 staying green is not a hole: all three bind NON-container roots, whose single leaf id is the
root fold FNV(0,0) — invariant under a parent word that is already 0. They sense the ROOT ROW, which is a
different claim, and row 70 now senses the walk.

**(2) ALL FIVE REMAINING FIXTURES REWRITTEN, WITH ONE NAMED LOSS.** Rows 6, 69 and 71 drop onto
`dyn_graph_edge_rows` + slice deems (no join loss; row 71's cross-document join stays a JOIN, over two
slices). Rows 56 and 57 were the hard pair — they exercise joins, `rel` RECURSION over a cyclic edge set,
`order by` and `group by … aggregate` OVER the walk, and a `Vec<WritEdgeRow>` does not provide any of
that. It does now: `pub struct DynEdge` + `pub fn dyn_graph_edge_rows` (graphsrc.logos) give the walk
NAMED columns, which is what a `&[T]` deem source resolves against, so every one of those capabilities is
preserved as a static deem over the materialized walk — written BESIDE its `&Writ` twin, which turns two
single-sourced class-C files into two-producer differentials. Two runtime-bound anchors
(`bind_i64("root", …)`, `bind_i64("start", db_h)`) became document-INDEPENDENT query anchors
(`where parent == 0`; seed the recursion on `key == "db"`), which is strictly better than what they
replaced.

⚠ **THE ONE LOSS, named rather than dropped:** row 56 leg (f) asserted that `Query::incremental_rec`
REFUSES a tree source with a named error ("tree source", `incr_rec.logos` / `incr.logos`). That refusal's
SUBJECT is `bind_source_tree` itself, an API of `query.logos`. After P5 there is no virtual-source binding
to refuse, so the guard and the thing it guards are deleted together — the assertion has no caller, not
merely no route. It was NOT carried forward onto a slice source, because a slice IS materialized and
"virtual, no delta capture" would be a false statement about it.

**(3) `es_scan` WAS NOT A PRESERVED CAPABILITY, AND IS NO LONGER FILED AS ONE.** It came across with the
ts_* family because it shares their `RowSet` shape, but it walks nothing — it re-reads rows a caller
already materialized, out of a word bound by `QEnv::bind_edge_rows`. Its sole caller in the repo is the
`QB_EDGE` arm of `exec_rexpr`. Post-cut it would have been an unreachable, unsensored function inside the
SURVIVING file. **Moved back into `exec.logos`, beside its caller, so it dies with the query engine.**
`RowSet`/`rs_new`/`RS_VARS` stay in `graphsrc.logos`: every external consumer of theirs is also in
`exec.logos`, so post-cut they are `ts_scan`'s private accumulator (`dyn_graph_edges` projects out of one)
— internal machinery, not a capability, and correctly unexported. `h_step` is the genuine exception:
`incr.logos` and `incr_rec.logos` both call it and both survive.

**C5 — the fuzz method** (rows 11, 13, 42). ⚠ **REWRITTEN at `e53962b6`, and the old paragraph was wrong
about which side the interpreter is on.** It read: "all three generate query TEXT at runtime; a macro
cannot be generated at runtime, so the static tier can only fuzz DATA over fixed shapes — which is what
`query_diff_static` does, and it is strictly weaker". That sentence treats the interpreter as the
second opinion these fixtures would lose. It is not. Read at the COMPARISON SITE — the only place the
question is decidable — the interpreter is the SUBJECT in all three, and the three are not one class.

**Rows 11 and 13 have a real, INDEPENDENT oracle, and it is not the interpreter.**
`query_diff_fuzz` compares at one site, `rows_eq(&mut want, &mut got, ncol)`: `got` comes from
`Query::compile` + `q.run` (the subject), `want` from nested loops written IN THE FIXTURE
(`rowget`, `contains`, `cmp`, `nrows_of`, `rows_sort`, `row_lt`) that call nothing from `logos.mem.deem`
or `logos.std.wql`. `query_diff_str_adv` has the same shape at eight sites (`strv_ms_eq`, `i64ms_eq`,
`pair_ms_eq`, `strv_seq_eq`) over its own byte-wise `str_cmp`/`str_eq`. Deleting the interpreter removes
the THING UNDER TEST; the oracle is fixture-local source text and survives untouched.

**Row 42 has no independent oracle at all — by its own construction.** `query_metamorphic_adv` compares
`rows_eq(&mut got1, &mut got2, ncol)` where BOTH sides are `Query::compile` + `q.run`, and its header
says so: "Instead of an independent oracle, this probe runs the engine against ITSELF under
semantics-preserving transforms". Under this repo's own rule — an oracle that shares the algorithm is not
an oracle — it is a SELF-CONSISTENCY probe, not a differential, and calling it one in the loss ledger
prices it wrong.

**MEASURED, by control revert.** Two perturbations of the ENGINE, each reverted; predictions stated
before running.
* Control 1 — reverse the total order in `rt_cmp` (`stdlib/mem/deem/exec.logos`), the executor's
  comparison fold. Predicted RED for rows 11 and 13, GREEN for row 42.
  RESULT: `query_diff_str_adv` RED, `query_metamorphic_adv` GREEN — and `query_diff_fuzz` and
  `query_diff_static` GREEN, which was WRONG and is the useful part: a scalar `where a < b` in the
  dynamic tier does not go through `rt_cmp` at all. It is decided in `eval_sexpr`'s ordered-compare arm
  (then in `eval.logos`, since ported to `stdlib/mem/deem/tpl.logos`), a DIFFERENT fold; `rt_cmp` serves `order by` and the min/max
  accumulators, which is why only the fixture with an `order by` template noticed. A control that
  perturbs a fold the subject never reaches is a control that changes nothing.
* Control 2 — reverse the ordered compare where scalar comparison actually lives: the four
  `OP_LT/OP_LE/OP_GT/OP_GE` returns of `eval_sexpr`, one site, uniform over str/int/f64.
  Predicted RED for 11, 13 and 12 (`query_diff_static`, whose dynamic arm is one of three), GREEN for 42.
  RESULT, exactly as predicted: `query_diff_fuzz` RED (exit **47**), `query_diff_static` RED,
  `query_diff_str_adv` RED, `query_metamorphic_adv` **GREEN**.
  Row 42 is blind to a TOTAL INVERSION of every ordered comparison the engine makes, because both of its
  sides make it. That is the measurement; it is not an inference from reading.

⇒ **What C5 actually requires is not a replacement oracle.** For rows 11 and 13 the METHOD survives the
deletion and only the HARNESS dies: an independent naive evaluator, over fuzzed data, versus a subject
that must now be `deem`-emitted code instead of runtime-compiled text. And the shape space is finite and
small — measured, `query_diff_fuzz` has 10 templates (`rng.upto(10i64)`, 9 `if tmpl ==` arms + the
trailing else) over 3 column indices and 6 comparison operators, and `query_diff_str_adv` has 9. Ten
`deem` items written out longhand is not "a compile-time fuzzer"; it is enumeration, and the old
paragraph's premise ("a macro cannot be generated at runtime", therefore the method is unreachable) does
not follow from it. `query_diff_static` is the existing instance of exactly this and is weaker only in
the dimensions nobody has added yet — the string column, and shape count.
For row 42 the requirement is ZERO: seven of its ten invariants (T0–T3, T7–T9) permute or duplicate
DATA and need no query text at all, so they port to fixed shapes as they stand; the other three
(T4/T5/T6 — `&&`, `||`, join commutativity) need two spellings of one query, which over a fixed shape
set is two `deem` items instead of one. Nothing about row 42 needs a runtime compiler. It needs a second
opinion, which it has never had.

Requirement, restated: (a) add the string dimension to `query_diff_static` and raise its shape count to
the ten `query_diff_fuzz` enumerates, keeping the fixture-local naive oracle — that discharges rows 11
and 13 as a HARNESS port; (b) re-file row 42, which is a self-consistency probe whose deletion loses no
oracle, and if the metamorphic method is wanted statically, write it against fixed shapes where it costs
nothing. Neither of these is the "metaprog-time shape generator" the old paragraph asked for.

### 5.C5b — BUILT, and the per-fold control the previous round got wrong

(a) is now built rather than argued. `tests/logos/pass/query_diff_static.logos` carries TEN shapes:
the six numeric ones it already had (`sf`/`sj`/`sa`/`sg`/`sm`/`sd`, exit codes 40–45), plus four STRING
shapes (`ss_lt`/`ss_join`/`ss_grp`/`ss_ord`, exit codes 46–49) that run FIRST, in a loop of their own,
with **no dynamic arm at all** — they are already in post-cut form. The string pool is
`query_diff_str_adv`'s: the empty string, the prefix chain `a`/`aa`/`aaa`, `B` (byte 66) against `a`
(byte 97) so byte order and case order disagree, plus a per-iteration pool SPAN that forces collisions,
duplicate keys and empty sources. The fuzzed string LITERAL survives as a fuzzed dimension without one
`deem` item per pool entry by being a typed `str` PARAM (`ss_lt(a: &[SS], q: str)`), the
`wql_typed_params_e2e` spelling.

**The oracle is `b_cmp`, a byte loop over `str_byte_at` written in the fixture — deliberately NOT
`str_cmp`.** Sema lowers the emitted engine's `str` relationals to `logos.lang.str::str_cmp`, so an
oracle that called it would share the exact fold under test in the very dimension being added. Control 6
below is the measurement of that, not an assurance.

**THE STATIC TIER HAS FOUR COMPARE FOLDS, NOT ONE.** This is why Control 1 of the previous round
("reverse `rt_cmp`") changed nothing for the static side, and why one perturbation cannot control this
fixture. Each string shape is aimed at one fold, and each fold was perturbed on its own, in the ENGINE,
with the exit code PREDICTED BEFORE THE RUN and the edit reverted afterwards. The fixture stops at its
first disagreement and the string loop runs first, so the exit code names the shape — a control that
reddens a fold the shape never reaches would show up as the WRONG code, not as a pass.

(Deliberately NOT a markdown table: §9's reader treats every table row in this file as a census row.)

```control
#  perturbation (in the ENGINE, reverted after)                         predicted   measured
1  emit_binop  (stdlib/mem/wql/codegen.logos): OP_LT <-> OP_GT          46          46
2  emit_binop: OP_GE -> " < " only                                      40          40
3  the JS_HASH build insert in build_phase_frag                         47          47
   (stdlib/mem/wql/rexpr_walk.logos) pushes payload 0i64, not the row
4  the batch group fold's key match in rexpr_walk.logos:                48          48
   `__g_key.get(__s) == __k` -> `!=`
5  sort_perm_frag (rexpr_walk.logos) ascending comparator: `>` -> `<`   49          49
6  str_cmp (stdlib/lang/str/str.logos) totally inverted                 RED, 46|49  46
```

Read the rows against each other. Control 1 reddens the `str` ordered compare and NOT the join, group or
order-by shapes — `emit_binop` is not where an equi-join decides equality, which is the mistake the
earlier `rt_cmp` control made in the dynamic tier. Control 2 reddens a numeric shape while all four
string shapes pass, so the codes discriminate and the string loop is not merely being skipped. Control 3
is the fold `emit_binop` cannot reach: the join's answer comes from a HashMap built by `build_phase_frag`
and probed through `bucket_lookup_frag`. Control 4 is the group key, Control 5 the sort key — three folds
that a single inversion of the scalar comparator leaves untouched.

**Control 6 is the oracle-independence measurement.** `str_cmp` is the function every `str` relational
in the emitted engine is lowered to. If the fixture's naive arm called it — as `query_diff_str_adv`'s
`s_lt` does — both sides of the comparison would move together under a total inversion and the fixture
would stay GREEN, exactly the way row 42 stayed green under Control 2 of the previous round. It went RED
at 46. The oracle did not move with the engine, in the one dimension where sharing was most likely.

⇒ Rows 11 and 13 leave class C for **A**. Their subject is the interpreter (measured at the comparison
site, above); their METHOD — fuzzed data against a fixture-local naive evaluator — now exists on the
static side and is measured to bite there, per fold. Row 12 stays class B and is the file that carries it.

(b) **Row 42 is re-filed C → A.** Both sides of its comparison site
`rows_eq(&mut got1, &mut got2, ncol)` are `Query::compile` + `q.run`, and Control 2 of the previous round
— inverting the four `OP_LT/OP_LE/OP_GT/OP_GE` returns of `eval_sexpr`, i.e. every ordered comparison the
engine makes — left it **GREEN** while rows 11, 12 and 13 all went RED. A fixture that cannot see a total
inversion of the operation it is exercising is not losing an oracle when its subject is deleted, because
it never had one. Class C prices a LOSS; there is none here. ⚠ This letter is a JUDGEMENT and §9's pin
does not check it (§9 says so explicitly); the control revert above is the argument, quoted so a reader
can disagree with it on evidence.

---

## 6. THE LOSS LEDGER — what the tree gives up if P5 proceeds anyway

Victor decides this. Each entry is a shipped capability with no static counterpart today.

* **L1 — trace reification** (ADR 0015 §1, invariant I1). `IncrRec` recording `TraceStep`/`TraceEpoch`
  about its own evaluation. Witness: `query_incr_trace_e2e`.
* **L2 — budgeted / anytime fixpoint** (§3). `set_budget_steps`/`set_budget_ns`, `TruncatedTail`,
  resume-from-watermark. Witness: `query_incr_budget_e2e`.
* **L3 — control atoms + journal + I2 boundary** (§4). Witness: `query_incr_ctl_journal_e2e`.
* **L4 — R4 provenance / `IncrRec::explain`** — witness-chain reconstruction. Witness: `query_incr_prov_e2e`.
* **L5 — `FactStore` / `FactHistory`**: content-keyed presence set, `take_delta`, the append-only journal
  and `replay_fresh`. `stdlib/lcm/deem/facthistory.logos` goes with it. Witnesses:
  `query_incr_factstore_e2e_{join,rec}`, `query_incr_journal_replay`.
* **L6 — S4 assumption branches** (`select_one` fork/merge over a live engine, law I4).
  Witness: `query_incr_s4_select_one`.
* **L7 — the Nous observer ladder rungs 0 and 1.** Both are built on IncrRec's budget cut + trace +
  control atoms; L1's "action" is a control atom whose argument is the derived pending count. Witnesses:
  `query_proto_observer_l0`, `query_observer_l1`. This is the entry that is not a database feature.
* **L8 — the runtime template engine** (`Tpl`), ADR 0011 / ADR 0012-queue2 §§2,3,6,7. §5 C2.
* **L9 — the engine as a `deem!` source** (ADR 0016 M5 case S): the `EngineState` trait,
  `<p>_trace`/`_epochs`/`_tail`/`_controls`. §1b.
* **L10 — runtime query compilation itself**: `Query::compile` over TEXT, re-entrancy over different
  envs, runtime join-tier dispatch, runtime `rel` fixpoint (`query_interp_smoke` enumerates exactly this),
  lenient/erased binding (§5 C3), and the ORACLE role the interpreter plays for the static tier in the
  **24** class-B differentials — after the cut, 24 fixtures lose their second opinion. (25 was the old
  totals line; the class column says 24. §9 FACT 4 now keeps this number and the table together.)
  ⚠ Read §5 C5 before pricing this entry: in the three fixtures that were examined row by row, the
  interpreter is the SUBJECT and not the oracle, and in one of them there is no independent oracle at
  all. "Loses its second opinion" is a claim about a comparison site, and it has to be checked per
  fixture at the comparison site — the B rows still owe exactly that read (§8). Row 12 has now had it
  and answers in the other direction too: its four STRING shapes never had a dynamic arm, and six
  engine-side control reverts (§5.C5b) show the static tier is differentially tested per fold without
  one.
* **L11 — the DRed harvest fixtures** (`deem_dred_phases23_spec`, `wql_incr_rec_agg_retract_lattice`,
  `wql_incr_rec_dred_error_window`). ⚠ These were written to preserve knowledge THROUGH P5 and they do
  not: each drives `Query::compile` + `incremental_rec`, so each dies with its subject. Harvest by fixture
  does not survive the deletion of what it harvests — only harvest by static re-implementation does.

---

## 7. Gates that move, and must be RE-PINNED (never weakened)

* `tests/logos/rtval_domain_gate.sh` (`logos_09_rtval_domain`, `tier_commit`, registered in
  `tests/logos/CMakeLists.txt`) globs `*.logos` in `stdlib/mem/deem` and asserts three hard constants:
  `WANT_VARIANTS="B Error F I Node Null S"`, and two counts that MOVED WITH THE CUT and were
  re-measured rather than relaxed: `WANT_MATCH_SITES` 27 → **16**, `WANT_KIND_CALLS` 27 → **22**.
  The sites did not become unchecked — the files holding them were deleted. `rt_cmp`, one of the
  two i32-code branchers item 3 of that gate names, lived in `exec.logos` and is gone; `rt_eq`
  remains and its defect is unchanged. The gate's header sentence ("`RtVal` is the value of BOTH
  the dynamic query engine and the incremental one") was REWRITTEN in the same commit, because
  `RtVal`'s consumers are now the runtime template engine and the graph walker.
  ⚠ The crude per-LINE count first recorded here (eval 11, exec 2, query 1, deem 4) UNDERCOUNTS: by
  OCCURRENCE it is eval **20**, exec **4**, query **1**, deem **4** — 25 of 29 inside the cut, so the
  conclusion holds a fortiori.
  **MEASURED that the gate bites** (verification pass): one extra `rt_kind(` call site planted in
  `deem.logos` (`fn __probe_kind_site`) turned `logos_09_rtval_domain` red with
  `the rt_kind i32-CODE dispatch surface moved: 28 call site(s)`, ctest exit **8**; reverting restored
  green. The gate reads SOURCE TEXT, so it reds with no rebuild — it will red on the cut and must be
  re-measured and re-stated, with the header sentence ("`RtVal` is the value of BOTH the dynamic query engine and the
  incremental one") rewritten, because it stops being true. `tests/logos/rtval_fallback.ledger` is
  currently empty by design and its `deleted-by` discipline applies here.
* `tests/logos/abi_closure_gate.sh` — built on the derived closure that gives
  `type logos.mem.deem.RtVal` a record; `is_deem_api_type` in `src/compiler/emit_module.cpp` names five
  types by hand — `Query`, `SchemaCatalog`, `QEnv`, `QRows`, `QError` — and TWO of them (`Query`, `QRows`)
  cease to exist. ANSWERED (verification pass, by reading `is_deem_api_type` / `is_deem_internal_type`):
  it is a pure `std::string_view` comparison used as an EXCLUSION predicate — no lookup, so a name that
  no longer resolves is silently DEAD, not an error. The consequence is that the derived closure
  `deem_abi_admitted` starts from a two-thirds-alive seed and SHRINKS, dropping every spec record that
  was admitted only because `Query`/`QRows` named it. That reads as record REMOVAL, so `abi-check.sh`
  will report BREAKING and demand a deliberate bump — the gate bites, but only after the deletion.
* Every deleted fixture has a matching `add_test` entry in `tests/logos/CMakeLists.txt`; the registry
  count (`ctest -N`, 6950 all / 3267 `-LE imported` at `9ebb6110`) must be PREDICTED before the cut and
  compared after — a test that silently stops existing is the failure mode this repo has already met.
  ⚠ RE-MEASURED at `e53962b6`: still **6950 / 3267** (the four C1 fixtures below were added on top of
  that and take it to 6954 / 3271). A round brief circulating "6951 / 3268" does not describe this tree;
  the number was re-derived here by moving the new fixtures aside, re-running `cmake -S . -B build`, and
  counting — not by trusting either the brief or this file.

---

## 7b. Verification pass — what an adversarial re-measure CONFIRMED

Re-measured independently at `8cf79102` (build green, clang-20, `LOGOS_EMIT_SHARDS=8`):

* Every sole-definition row of §1a: confirmed by `grep -rnE '^(pub )?(fn|struct) …' stdlib/`. Call counts
  44 / 25 confirmed. `Query { … }` constructed at exactly one site, in `query.logos`'s `Query::compile`.
* §2 arithmetic AT `8cf79102`: 1672+1472+606+963 = 4713 (P5 as written), +1978+1466+92+514 = **8763**.
  Confirmed AT THAT TREE and SUPERSEDED at `8c5ad0ea`: the template port shrank `stdlib/mem/deem/check.logos` to 974 and
  `exec.logos` to 1450 and deleted `eval.logos` (606 → gone), so the four-file cut is 974+1450+963 =
  **3387** and the whole-tier cut is +1978+1466+92+514 = **7437**. §2 carries the current table.
* The three DRed harvest fixtures: `deem_dred_phases23_spec`, `wql_incr_rec_agg_retract_lattice`,
  `wql_incr_rec_dred_error_window` each declare ZERO `deem` items and each reaches `Query::compile` +
  `q.incremental_rec(&env)`. L11 stands: harvest by fixture does not survive.
* The B→A correction: all eleven have zero `deem` declarations; every `_apply(` hit in them is a local
  `src_apply`/`nasty_apply`/`edge_apply`. Confirmed — the `_apply(` grep was indeed a false signal.
* Row 48 `query_rec_agg_batch_e2e` is the one row filed A while HAVING a static arm (`sssp`, `longest`),
  which by this file's own class definitions is B. The A verdict survives on the merits, not the rule:
  the static half is duplicated by `wql_rel_sssp_e2e` and `wql_rel_widest_path_e2e` (both interp-free,
  both carrying an independent Bellman-Ford / bottleneck oracle) and the cycle case by
  `wql_rel_neg_cycle_abort`.
* Rows 23/33/35 (`query_incr_guard`, `query_incr_sssp_guard`, `query_incr_tc_guard`) guard the DYNAMIC
  entry points and return `Result::Err` values; the static tier keeps its own door for the same rule
  (`tests/logos/fail/wql_rel_agg_group_cycle_fail.logos`). A is correct.
* Registry AT `8cf79102`, historical: `ctest -N` 6950 all / 3267 `-LE imported` / 29 `-L '^tier_commit$'`.
  This is the sentence that went stale; today's numbers are §9's, and they are checked.
  `git diff --stat 9ebb6110 8cf79102 -- stdlib src tests tools abi scripts` is EMPTY, so no emitted
  symbol can have moved; absolute baseline recorded for the next attempt — `wql_rel_sssp_e2e` (rel)
  155 defined symbols, `wql_aggregate_e2e` (non-rel) 283.
* `scripts/abi-check.sh` RC **0**, `VERDICT: ABI-PRESERVING`, sym 12695 / type 365 / vtable 115 /
  schema 2 identical to the `origin/main` base; `abi/logos.abi` correctly NOT regenerated. L1 and L2
  green (691/691, 1890/1890).

## 8. Not measured here

* `scripts/abi-check.sh` was not run: nothing was edited, so its verdict would be about the unmodified
  tree and would say nothing about the deletion.
* The dead residue INSIDE `deem.logos` after the cut (which of its ~40 functions lose every caller).
* Whether each class-B remainder still bites once its interpreter arm is gone — per fixture, by breaking
  it and predicting the exit code. This is the largest piece of work the next attempt owes.
* Build/L4 impact of the cut: no branch of this repo has ever compiled without these files.
* ⚠ THE C2 PORT WAS NOT IN THE TREE THIS SECTION WAS WRITTEN AGAINST. At `e53962b6` `eval.logos` still existed (606 lines,
  `Tpl` inside it) and no template-only module exists beside it; §1c, §2 and §5 C2 describe THAT tree and were
  re-measured against it. If the template port lands, `logos_00_census_pin` goes red on the bare name
  `eval.logos` the moment the file disappears — that red is the pin working, and the repair is to rewrite
  §1c/§2/§5 C2 in the same commit rather than to touch the gate.
* Rows 11, 13 and 42 have now had the per-fixture read §8 asks for, at the comparison site and with two
  control reverts (§5 C5). The other 22 class-B rows and the remaining class-C rows have not.
  ⚠ SUPERSEDED IN PART by §8b: thirteen more class-B rows have now had that read, with four control
  reverts. What remains unread is nine class-B rows and the class-C rows.

---

## 8b. Thirteen class-B rows read at the comparison site — 2026-08-09

Rows 1, 12, 16, 43 and nine others (`tests/logos/pass/adv_rec_tc.logos`,
`tests/logos/pass/deem_dred_od_rw_split.logos`, `tests/logos/pass/deem_incr_diff_harness.logos`,
`tests/logos/pass/deem_incr_static_retract_e2e.logos`, `tests/logos/pass/derive_graph_source_root_row.logos`,
`tests/logos/pass/query_agg_sum_overflow_e2e.logos`, `tests/logos/pass/query_diff_err_e2e.logos`,
`tests/logos/pass/query_diff_static.logos`, `tests/logos/pass/query_f64_avg_nan_fuzz.logos`,
`tests/logos/pass/query_f64_agg_hand_derived.logos`, `tests/logos/pass/query_mapping_runtime_e2e.logos`,
`tests/logos/pass/query_minmax_float_seed_leak.logos`,
`tests/logos/pass/query_order_by_float_data_key.logos`). Verdicts live in each file's header; the
measurements are here.

FOUR CONTROL REVERTS, each at the function where the decision LIVES, never at a call site. Archives
dropped and the stdlib rebuilt for each; every fixture below was run on the same build as its claim.

| control (file · edit) | predicted | measured |
|---|---|---|
| `stdlib/lang/str/str.logos` · `str_cmp` returns the opposite sign, `str_eq` untouched | `query_diff_static` red 46 or 49 | **46** (shape 6, the ordered `str` compare); numeric shapes silent |
| `stdlib/mem/wql/rexpr_walk.logos` · `emit_scc_fn`'s emitted `if #(exitc) { break; }` made unconditional | `adv_rec_tc` red 10 | **10**; also `deem_dred_od_rw_split` **64** and `query_mapping_runtime_e2e` **4** |
| `stdlib/lang/num/float.logos` · `f64_data_key` returns `0` for every input | `query_minmax_float_seed_leak` red | **exit 0 — GREEN, blind**, at `a4028326`; **10** after the repair below |
| `stdlib/lang/num/float.logos` · `f64_data_key` returns `0 - f64_total_key(x)` (injective, order-reversed) | separates the old and new oracle arms of `query_f64_avg_nan_fuzz` | **it does not**: 18 both ways |

WHAT THE MEASUREMENTS SAY, beyond pass/fail.

1. **The str hazard was avoided and the avoidance is now measured.** `query_diff_static`'s oracle is
   `b_cmp`, a byte loop over `str_byte_at`; inverting `str_cmp` moved the ENGINE and not the oracle.
   That is what independence looks like when it is measured.
2. **The float hazard was NOT avoided, in one file, and it was fatal there.**
   `query_minmax_float_seed_leak` wrote its expectations as constants — correctly — and then compared
   them with `f64_data_key(got) != f64_data_key(want)`, the engine's own comparator. A key that
   collapses every float to one value does not make the two sides agree on a wrong answer; it destroys
   the comparison, and the fixture went green over an engine answering `min` over `{ NaN, 1.0 }` with
   NaN. Repaired in-file with two arms that reach no stdlib order at all (`pg_eq`, `same_bits`).
3. **A control that fails to separate is still a measurement.** The order-reversed key was run
   specifically to show that `query_f64_avg_nan_fuzz`'s old min/max oracle shared the engine's ranking.
   It reds both versions at 18, because that file compares BIT PATTERNS and a mis-ordered fold stops
   displacing its seed — so it answers a value in no row, which any row-returning oracle contradicts.
   The sharing was real; the hole was not. The rewrite stands as a dependency removal, not as a repair,
   and its header says exactly that.
4. **The strongest surviving oracle in the thirteen is `adv_rec_tc`**: mutual recursion with an
   INDEPENDENT fixpoint (`naive_odd`) on the other side.
5. **`query_metamorphic_adv`'s failure mode did not recur here.** No fixture in the thirteen compares
   the emitted engine with itself. Two compare BATCH against INCREMENTAL — `deem_incr_diff_harness`
   check 23, `deem_incr_static_retract_e2e` codes 38/39 — and both say in their own headers that those
   are law evaluators over one emitter, not oracles.
6. **`query_mapping_runtime_e2e` lost its subject, not just its arm.** It was a PARITY test between
   `Query::compile_with_mapping` and the static twin; `Query` is gone, so there is ONE consumer and the
   question "do the two agree" is dead. No oracle was manufactured for it.
7. **`deem_dred_od_rw_split` is still a differential** (arm A against `automaton_od`, a closed form
   written in the file) but only on one tier; its checks 22/23/24 are lost, as its header states. Its
   controls belong to the DRed slice and were not re-run here.

⚠ NOT MEASURED, and owed: `logos_09_layout_engine_agreement` (`tier_full`) was not run for this slice.
Nothing here changes a stdlib type or instantiation count — the only stdlib edits were control reverts,
all restored — so it is expected to be unmoved, and that is a prediction, not a measurement.

---

## 8c. Row 60 — the independent arm that was owed, BUILT — 2026-08-10

Scope of this section: **row 60 only** (`tests/logos/pass/wql_agg_avg_bool_value_rule.logos`). The other
five fixtures of that slice (rows 51, 70 and the graph rows) were read and control-reverted in the same
worktree without a source change; their verdicts stay in their own headers.

Row 60 was recorded as *constructible-but-unwritten*: after P5 the file was three hand constants against
one engine. Hand constants are the right way to write an expectation, but three numbers over a
GROUP-SORTED corpus can only see value-rule defects. The arm and a second corpus close that.

- `ref_avg_bool(&Vec<S>, i64) -> f64` — a plain `while` walk; callees are `Vec::get`, `Vec::len` and
  arithmetic. It branches `if r.b { 1.0f64 } else { 0.0f64 }` and never spells `as f64` on a bool, so
  the coercion the file rules on is on the SUBJECT side only.
- `ref_group_keys` — first-occurrence key order, walked in the fixture, so the arm can address a group
  without the engine telling it which column is which.
- `rows_ilv()` — an INTERLEAVED corpus (keys 7,3,7,3,7,3,3,7) with **no hand constants at all**. This is
  the population the three literals structurally cannot cover.

| control (file · edit) | predicted | measured |
|---|---|---|
| `stdlib/mem/wql/rexpr_walk.logos` · the group fold's `inner` quote_block starts the `__g_key` dedup scan at the LAST key instead of `0` (a contiguous-grouping regression) | 41, with 11–18 GREEN | **41**, 11–18 green |
| `tests/logos/pass/wql_agg_avg_bool_value_rule.logos` · invert `if r.b` inside `ref_avg_bool` | 16, with 12–15 GREEN | **16**, 12–15 green |
| `src/compiler/mlir_gen_expr.cpp` · `gen_expr_kind(ECastView…)` int→float arm: drop the `val.getType() == builder_.getI1Type()` disjunct | 13 | **exit 0 — GREEN.** The disjunct is REDUNDANT |
| ~~same site · exclude `LogosType::Kind::Bool` from `is_unsigned_repr_kind`'s disjunct instead~~ | ~~13~~ | ⚠ **DID NOT REPRODUCE — see §8d** |

WHAT THIS SAYS.

1. **The arm is a sensor, not decoration.** The first control is invisible to every hand constant in the
   file — `rows()` is group-sorted, so a contiguous-grouping engine answers it correctly — and the arm
   catches it at a WALKED group count after the whole 8-row fold ran. That is the class of defect the
   post-P5 single-sidedness opened.
2. **The two sides are one-sided, measured both ways.** Perturbing the fixture arm moves only the arm
   (16, literals green); perturbing the engine's value rule moves only the engine (13, caught by the
   literals first, arm never reached). Neither perturbation moved both.
3. **A file-header claim was half wrong and is now corrected in place.** The header said mlir-gen
   zero-extends i1 "on purpose". It does — but the clause that carries it is
   `LogosType::is_unsigned_repr_kind`, which names `Bool` explicitly in
   `include/logos/compiler/sema.hpp`; the i1 TYPE test beside it is dead whenever the operand's Logos
   type is known. Deleting the i1 test alone left the fixture green. ⚠ The rest of this paragraph as
   originally written — "a third control, aimed at the clause that actually decides, reds at 13" —
   **DID NOT REPRODUCE**; see §8d.

⚠ NOT MEASURED, and owed: `logos_09_layout_engine_agreement` (`tier_full`) was not run. This slice adds
no stdlib type and no fixture FILE (the arm lives in an existing file), so stdlib type and instantiation
counts are unchanged — a prediction, not a measurement. **DISCHARGED in §8d: the gate was RUN and
PASSES, so the prediction is now a measurement.**

---

## 8d. Row 60 re-verified adversarially — 2026-08-10

An independent worktree checked out `c0ae091e`, rebuilt, and attacked §8c. Three of its four controls
reproduced; one did not, and one NEW control was needed because §8c's own controls did not fire the
thing §8c claims to have built.

**THE ARM WAS UNDER-CONTROLLED, THOUGH NOT UNFIREABLE.** §8c's control (A) reds at **41**, which is
`sv2.len() != ks2.len()` — an assertion over `ref_group_keys`, NOT over `ref_avg_bool`. Reproduced: 41.
But no control in §8c fires `ref_avg_bool` from the ENGINE side; (B) fires it from the FIXTURE side,
which cannot distinguish a sensor from a tautology. A fourth control was constructed for exactly that
gap — in the `inner` quote_block of `stdlib/mem/wql/rexpr_walk.logos`, bump `__g_cnt` at the row's own
group but redirect the accumulator folds to the NEWEST group. A group-SORTED corpus is answered
perfectly (11–18 green) and the group COUNT is untouched (41 green), so 42 is the only sensor that can
see it. **PREDICTED 42, MEASURED 42.** The arm is a real sensor; §8c was right about that and had not
yet shown it.

**THE `Kind::Bool` CONTROL DID NOT REPRODUCE.** §8c records 13 for "exclude `Kind::Bool` from
`is_unsigned_repr_kind`'s disjunct". Both readings of that edit were built and measured:

| edit | predicted | measured |
|---|---|---|
| drop the i1 test only (`src/compiler/mlir_gen_expr.cpp`) | GREEN per §8c | **GREEN**, and the whole L2 sample green with it |
| exclude `Kind::Bool` at the cast site only, i1 test kept | 13 | **GREEN — refutes §8c** |
| drop `k == Kind::Bool` from `is_unsigned_repr_kind` in `include/logos/compiler/sema.hpp`, i1 kept | 13 | **GREEN — refutes §8c** |
| drop the i1 test AND exclude `Bool` | 13 | **13** |

The two disjuncts are **MUTUALLY REDUNDANT**: each alone is sufficient, so no single-clause edit can red
this fixture. §8c's "only the second is load-bearing" is false, and its recorded 13 is reproducible only
as a control stacked on the UNRESTORED previous one. The fixture header is corrected with all four rows.
Nothing about the fixture's coverage changes — the value rule is still pinned at 13, but against the
removal of BOTH clauses, which is what the header now says.

**Gates re-measured on the pristine tree**: L1 690/690, L2 1892/1892 (+12 684 generated cases, 31
`tier_commit`), `census_pin` + `spec_path_lint` + `gate_lint` green, `scripts/abi-check.sh` rc=0
(ABI-PRESERVING, 3 closure canaries caught), `ctest -N` 6911 / 3228 / 31 — all unchanged, and
`logos_09_layout_engine_agreement` (`tier_full`) **PASSES**.

---

## 9. THE PIN — every claim in this file a machine can decide

`tests/logos/census_pin_gate.sh` (`logos_00_census_pin`, label `tier_commit`) reads THIS section and
this whole file, and fails when the tree disagrees. It exists because the two drifts that actually
happened were both mechanically detectable and neither was detected: a registry baseline ctest no longer
agreed with, and a population rule that missed three `FactStore` fixtures. The precedent is
`tests/logos/shared_ref_ub_lint.sh`, whose PATH rows were caught drifting twice while the sentence beside
them saying the same thing was never caught at all. A path has an exit code; a sentence does not.

Six facts, each re-proved on a planted census in the same run (seven self-canaries, so a dead reader
reports the GATE broken rather than the census clean):

1. every `docs/… tests/… stdlib/… src/… tools/… scripts/… abi/…` token in this file names something
   that exists (braces expanded, globs required to match);
2. every BARE filename (`incr.logos`, `params.logos`, …) resolves to exactly ONE file in the tree —
   zero is drift, two or more is an ambiguous sentence that must be written with its path;
3. every §3 row names a fixture that exists AND has its `.expected` beside it, because registration is
   a GLOB over `tests/logos/pass/*.expected` and a `.logos` without one never runs;
4. §3's header count, the number of table rows and the per-class totals are three statements of one
   number, checked against each other and against the class column actually written in the rows;
5. the registry baseline below is TODAY's `ctest -N`;
6. the population is derived from `CUT-SYMBOL` below and held in BOTH directions against the rows plus
   the `NOT-AFFECTED` declarations.

⚠ **FACT 5 GOES RED WHENEVER ANY TEST IS ADDED ANYWHERE IN THE REPO. THAT IS THE DESIGN.** §7's method
is "predict the registry count before the cut, compare after", and a prediction against a number nobody
re-measured is not a prediction. The repair is three lines here, and the gate's failure message prints
the exact values to paste. If the delta is not the one you expected, the gate has just done its job.

⚠ What the pin does NOT do: it does not read prose, and it does not check a JUDGEMENT. Whether row 42 is
class C or class A is an argument (§5 C5 changed it), and no gate can settle that. The pin only
guarantees that the nouns in the argument still exist.

```pin
# Files this census discusses that no longer exist. FACTS 1 and 2 let the name
# through; FACT 7 then requires that it really is absent AND that the line says
# why. Naming a corpse is allowed; pretending a live file is one is not.
GONE-FILE  stdlib/mem/deem/eval.logos  deleted at 8c5ad0ea (C2): its whole contents moved to stdlib/mem/deem/tpl.logos
GONE-FILE  stdlib/mem/deem/check.logos  deleted at P5: the dynamic query CHECKER; its template half had already left for stdlib/mem/deem/tpl.logos
GONE-FILE  stdlib/mem/deem/exec.logos  deleted at P5: the dynamic EXECUTOR (rt_cmp, exec_root, RelCtx, OutTab, es_scan)
GONE-FILE  stdlib/mem/deem/query.logos  deleted at P5: Query / QRows, the runtime query-compilation entry point
GONE-FILE  stdlib/mem/deem/incr.logos  deleted at P5: the DBSP incremental engine (IncrJoin, FactStore, AggState)
GONE-FILE  stdlib/mem/deem/incr_rec.logos  deleted at P5: the recursive incremental engine (IncrRec, dred)
GONE-FILE  stdlib/mem/deem/mapping_state.logos  deleted at P5: trait EngineState + the four deem_state_* materializers (L9 / ADR 0016 M5 case S)
GONE-FILE  stdlib/lcm/deem/facthistory.logos  deleted at P5: FactHistory, the epoch-history layer over FactStore

# registry — `ctest -N` from the build directory, three ways. RE-MEASURED after
# the cut: 6959 / 3276 / 30 -> 6906 / 3223 / 30, i.e. exactly -53 / -53 / 0, one
# ctest per deleted fixture and tier_commit untouched. PREDICTED before the
# re-configure; the measurement agreed.
# ⚠ THE VALUE BELOW WAS MEASURED ON THE MERGED TREE, not taken from a slice.
# Three slices each wrote their own total against the SAME base, so the merge
# conflicted LOUDLY here — unlike the previous round, where two slices wrote the
# BYTE-IDENTICAL hunk and git merged it silently one short.
# Task 30 added ONE pass fixture (tests/logos/pass/trait_ident_pkg_chain.logos) plus two
# non-registering package archives (trait_ident_chain/{lhom,hmid}). PREDICTED
# 6921 / 3238 / 31 before the re-configure; `ctest -N` three ways agreed exactly,
# and the new name was verified individually with `ctest -N -R trait_ident`.
# Task 33 (B-mv-03, the impl-registry identity key) added ONE fail fixture,
# tests/logos/fail/trait_ident_homonym_bound_refused.logos + .expected.
# On its OWN tree it predicted and measured 6923 / 3240 / 31 — +1 / +1 / 0, a
# fail fixture carries no tier_commit label.
# Task 32 added ONE pass fixture (tests/logos/pass/trait_blanket_bare_alias_bound.logos)
# plus one non-registering package archive (trait_blanket_chain/bmid). On its OWN
# tree it also predicted and measured 6923 / 3240 / 31.
# ⚠ THE TWO SLICES WROTE THE SAME TOTAL AGAINST THE SAME BASE. The merged tree
# carries BOTH fixtures, so the merged total is 6924 / 3241 / 31, NOT 6923 —
# the value below was RE-MEASURED on the merged tree with `ctest -N` three ways
# after a re-configure, and both new names were verified individually BY NAME:
#   ctest -N -R trait_ident_homonym_bound_refused
#   ctest -N -R trait_blanket_bare_alias_bound
# The slices' ordinals (#5400 and #5326) were worktree-local and are NOT recorded
# here: each commit inserts a test the other did not have, so both shift on merge.
# This hunk conflicted LOUDLY only because each slice also wrote its own comment
# block. Had the comments matched, the identical values would have merged
# silently and this ledger would have come up ONE SHORT.
# The merged tree measured 6924 / 3241 / 31 exactly, as written above.
# THEN the merge itself exposed one more defect and closed it, adding a third
# fixture: the B-mv-03 refusal was DECLARATION-ORDER dependent — `fn f<T: Hash>`
# written above `pub trait Hash` reverted silently to the pre-B-mv-03 MLIR
# verifier failure. Fixed at pass-0 trait pre-registration (sema_collect.cpp);
# pinned by tests/logos/fail/trait_ident_homonym_bound_decl_order.{logos,expected}.
# +1 / +1 / 0 again — a fail fixture carries no tier_commit label. RE-MEASURED
# on the tree that carries all three, `ctest -N` three ways, after re-configure,
# and the name verified individually with
#   ctest -N -R trait_ident_homonym_bound_decl_order
# And the merge exposed a THIRD, on the blanket side of the same root, found by
# re-running a claim the task-32 report recorded as "red today, not landable":
# it was no longer red the way the report said, and the reason was worse.
# `blanket_implements` matched a BARE query by raw spelling. A query is bare
# exactly when the bound's trait OWNS the bare slot — for every stdlib trait,
# always — so ANY package's `trait Error` + blanket satisfied a bound over
# `logos.lang.error::Error`. MEASURED, with the control: consumer with the local
# homonym rc 0 (compiled), the same consumer with the homonym deleted rc 1
# (correctly refused). Fixed by matching on identity in both directions, which
# also retires the `q.find("::")` text-guess. TWO fixtures, a refusal and an
# admission, because the refusal's message is byte-identical to what an
# over-refusing compiler would print and cannot stand alone:
#   tests/logos/fail/trait_blanket_homonym_bound_refused.{logos,expected}
#   tests/logos/pass/trait_blanket_homonym_bound_admits.{logos,expected}
# plus the non-registering archive tests/logos/trait_blanket_chain/bprobe.
# +2 / +2 / 0. RE-MEASURED after re-configure, three ways, names verified with
#   ctest -N -R trait_blanket_homonym
# And a FOURTH, chasing the reported cross-archive SIGSEGV: the crash did not
# reproduce; the ADMISSION did, every time, and it is quieter than a crash.
# ROOT: a traits_ REGISTRY KEY IS NOT AN IDENTITY. The registry gives the BARE
# name to whichever homonym is collected first, so the incumbent's "canonical"
# key was the same string every OTHER homonym's impl is filed under as its raw
# bare-text alias — a bound over one trait read the other's impls, in whichever
# direction collection order pointed. Identity is now `pkg::Trait` for every
# trait that resolves (`impl_key_trait`), so it cannot collide with a raw alias.
# ⚠ The narrowing is deliberately NOT applied inside `sema_has_impl_recursive`:
# that primitive is also called by hardcoded COMPILER probes ("a rel column must
# implement Hash"), which name a stdlib trait by bare text — narrowing it made
# trait_ident_bare_alias_bound red. The union stays there; the narrowing lives
# where the caller's intent is known.
# +1: tests/logos/fail/trait_ident_cross_archive_bound_refused.{logos,expected}
# (links the existing lhom archive; no new archive). Proved to BITE by reverting
# the identity composition to the registry key — that fixture, and ONLY it, went
# red; restore rebuilt and re-run green before continuing.
# And a FIFTH: mono's eager blanket pass built its candidate list by comparing
# trait SPELLINGS, so `impl<T: Hash> Marker for T` beside a package-local
# `trait Hash` cloned the blanket for every type implementing the STDLIB Hash
# and emitted calls to methods that do not exist ('i32__tag'). A legitimate
# program failing to compile — reproduced by me on this tree before the fix.
# The identity now reaches mono through LIR (impl_keys::IDENTITY_TRAIT /
# IDENTITY_BOUND_TRAIT / IDENTITY_EXTRA_BOUNDS, fn_tbound_keys::TB_IDENTITY, all
# SPARSE with documented fallbacks, so an older archive degrades to the previous
# behaviour instead of losing impls).
# +1: tests/logos/pass/trait_blanket_homonym_no_overinstantiation.{logos,expected}
# — it pins COMPILABILITY plus the value that only the local trait can produce,
# so a "fix" that dropped the local facts fails it too. Proved to BITE by
# restoring the spelling comparison: that fixture, and ONLY it, went red;
# restore rebuilt and re-run green.
# ⚠ THE TWO MONO BARE ALIASES ARE STILL IN. Narrowing the LAST probe
# (`concrete_has_impl` in method_bound_ok) to the identity BREAKS THE STDLIB
# BUILD — 'Vec$G1$tup$3$slice_u8$i64$slice_u8__fmt' does not reference a valid
# function — because not every table that probe reaches is identity-keyed yet.
# That measurement is recorded at the call site. Retiring the aliases is the
# step AFTER those tables, not before.
# ADR 0025 prerequisite (req. 1 + §7 rung 1) added a PAIR:
# fail/ctr_family_mut_while_cursor (+ .expected) and
# pass/ctr_family_cursor_then_mut (+ .expected). The pair is the point: the
# refusal is a CONJUNCTION (borrow-carrying cursor AND `&mut self` mutators) and
# either half alone stays permissive — invisible to the previously-green corpus,
# in which NO test held a cursor across a mutation. The fail half is also the
# only witness that #[borrow_carrying] written inside the family's quote_item
# survives metaprog emission. PREDICTED 6931 / 3248 / 31 before the
# re-configure; `ctest -N` three ways agreed exactly.
# ADR 0025 S0 (vocabulary + Buffer + leaf batches) added FIVE fixtures — four
# pass, one fail — and each pins a different claim of the slice:
#  · pass/stream_buffer_degenerate — §4's base case: a Vec built OUTSIDE the
#    stream vocabulary, read only through BatchStream/Rewind/SizedStream. One
#    packet then None, rewind re-yields the same VALUES (asserted, not counted),
#    size free and unmoved by consumption, and an EMPTY buffer still yielding one
#    empty batch before None (§1's legal tick). Control: perturbing one expected
#    value reds it at that value's code, so the green is not the harness's.
#  · pass/ctr_family_leaf_batches — §5, the emitted leaf-batch producer against
#    the container's OWN per-row cursor as an independent oracle: same rows, same
#    order, same sums; the four landings (full/at/from/upto) with SizedStream
#    agreeing with the rows each yields; and the batch count pinned EXACTLY at
#    the leaf count (8 for 1000 entries in 4K leaves) — with the per-row walk it
#    would be 1000. Derived from the A3 scratch oracle, whose emitter-side
#    control (in-leaf trim `hi = lb` → `hi = cnt`) reds it at code 12.
#  · pass/stream_caps_trait_query — the S0 GATE ("trait-membership questions
#    answerable from the planner"), asked through the metaprog `has_trait` seam
#    the `join_key_caps` comment names, over `typeof(c.leaf_batches())`: the four
#    §3 capabilities answered YES, Rewind answered NO, and a local `impl Rewind`
#    control so the NO cannot be a blanket false. The answer then SELECTS the
#    code path (free `size()` vs the drain the plan would otherwise insert) and
#    which arm ran is asserted. Its header records the layer S1 must lift the
#    query to (a NAME-keyed form: `join_key_caps_named` holds a `str`, and no
#    query takes one) and the MEASURED hole it must not pin: `has_trait` answers
#    0 for a GENERIC type whose bare name is AMBIGUOUS tree-wide — `Buffer`
#    (logos.mem.stream vs logos.lang.fabric) gets a `$M` module fingerprint in
#    its identity while a generic impl registers its target under the bare
#    spelling. Control in ONE program: VecIter/Iterator and Vec/Index — same
#    tier, same imported-generic shape, unambiguous names — answer 1.
#  · fail/ctr_family_mut_while_batch + pass/ctr_family_batch_then_mut — §7 rung 1
#    for the BATCH, a surface that did not exist when the req.1 cursor pair
#    landed. `insert` while a batch lives is refused; with the scope closed it is
#    admitted and the next scan SEES the insert. Measured that the refusal is the
#    BATCH's: delete the `s.batch(&c)` line and the same program compiles (the
#    stream's borrow ended at its last use); use the stream after the mutation
#    and the diagnostic returns. The `Option<B>` pull's laundering is deliberately
#    NOT pinned — it is a recorded hole, not intended behaviour.
# PREDICTED 6936 / 3253 / 31 (+5 / +5 / 0 — the glob registers by `.expected`,
# and no tier_commit label rides that path) BEFORE the re-configure; `ctest -N`
# three ways agreed exactly, and each of the five names was verified individually
# with `ctest -N -R <name>`.
#
# 2026-08-11 — D1 CLOSED, and the `Option<B>` laundering recorded as a hole two
# entries up IS NOW PINNED, so that sentence no longer describes this tree.
# Borrow provenance used to die at ANY by-value hop; a loan now follows the
# HOLDER graph (borrow_check.cpp inherit_loans / bc_hop_roots), so composition
# into an enum literal, extraction by unwrap / field read / match binding,
# pass-through by a by-value fn, and a store into a container all keep it.
# 13 fixtures, one PAIR per door — 8 `fail/bc_d1_*` refusals and 5
# `pass/bc_d1_*_admits` twins, because the refusal message is byte-identical to
# what an OVER-refusing checker prints and neither half stands alone. One twin,
# pass/bc_d1_residency_exempt_return_admits, exists to red if a future rule keys
# on a type NAME instead of is_borrow_carrying_type() and re-captures the
# residency-holder escape hatch.
# PREDICTED 6949 / 3266 / 31 (+13 / +13 / 0 — same `.expected` glob path, no
# tier_commit label rides it) BEFORE the re-configure; `ctest -N` three ways
# agreed exactly.
#
# 2026-08-11 (same day, SEPARATE step) — THE PROTOCOL DOOR RE-PINNED. D1 above
# closed the checker; this step pins what that buys at ADR 0025's own §1
# signature, +2 fixtures:
#   * fail/ctr_family_mut_while_next_batch — the out-of-tree scratch repro that
#     measured D1, brought IN-TREE verbatim so it stops living in a sandbox that
#     no gate reads. It compiled rc=0 on 262398ac and now refuses with "cannot
#     borrow 'c' as mutable: 'c' has shared borrows" (message taken from the
#     actual run, not predicted).
#     It is a DIFFERENT SURFACE from fail/ctr_family_mut_while_batch, which pins
#     the SPLIT pull (`advance()` + `batch(&c)`) and never wraps the batch in an
#     `Option`: two laundering routes to the same freed leaf, and a checker can
#     lose one without the other, so both stay.
#   * pass/ctr_family_next_batch_then_mut — the admit twin, same shape with the
#     batch's and the stream's scope CLOSED before the mutation. It must COMPILE
#     AND RUN: `batches == 1`, `sum == 33`, and `sum2 == 6` so the scan AFTER the
#     mutation SEES it. Without this half the refusal message above is
#     indistinguishable from an over-refusing checker that carried the loan into
#     the `Option` and never released it — a dead container reads exactly like a
#     closed hole.
# PREDICTED 6951 / 3268 / 31 (+2 / +2 / 0 off the 6949 line above — the same
# `.expected` glob, no tier_commit label on either). ⚠ HONEST SEQUENCE, because
# the ledger's rule is predict-BEFORE-reconfigure and that is not what happened
# here: the two fixtures were already on disk and already registered when this
# step began, so the +2 is ledger ARITHMETIC checked against a build tree that
# had seen them, not a prediction made before their first registration. What was
# actually re-run: a fresh `cmake -S . -B build`, then `ctest -N` three ways
# (6951 / 3268 / 31, agreeing exactly with the arithmetic), then each of the two
# names verified individually with `ctest -N -R <name>` (1 each). A prediction
# that could not have been wrong is weaker evidence than one that could; the
# three-way agreement and the per-name checks are the part that carries weight.
#
# 2026-08-11 (same day, SEPARATE step) — D1 ROUND 2 PINNED. The adversarial pass
# over round 1 found EIGHT more laundering routes and the implement step closed
# them; this step pins each one as a PAIR, +16 fixtures, 8 `fail/bc_d1r2_*` and
# 8 `pass/bc_d1r2_*_admits`:
#   Door A  place_write_field   — a place WRITE recorded no loan (`w.b = c.mk()`;
#                                 one rule covers seven statement spellings).
#   Door B  value_block         — pop_scope killed a loan whose HOLDER is an
#                                 outer binding (block-as-value spelling).
#   Door C  let_else            — `let … else` had NO visit_stmt case at all.
#   Door G  destructuring_let   — the `__destruct_*` spill was unrouted AND its
#                                 tuple type read as non-borrow-carrying.
#   Door D  closure_capture     — a capture borrowed the binding but inherited
#                                 none of the loans the binding held.
#   Door F  call_out_param      — Code::Call had no mirror of MethodCall's
#                                 capture flow (`stash(&mut w, c.mk())`).
#   RESIDUE ref_arg_hop         — a `&B` argument was skipped by the hop walk;
#                                 the METHOD spelling of the same program was
#                                 refused. ADR 0025 §2's second ⚠ recorded this
#                                 as open and deliberately UNPINNED; it is now
#                                 closed, pinned here, and the ADR is superseded
#                                 in place.
#   Door E  dyn_erasure         — `Box<dyn Get>` has no type NAME, so erasure
#                                 dropped what `Box<B>` carried.
# NO DOOR IS SKIPPED, and that is MEASURED rather than assumed: the parent's
# door list would be worth nothing here if some door had already been refusing
# for a round-1 reason, so all 8 leak programs were run against a CONTROL REVERT
# of src/compiler/borrow_check.cpp (round-1 checker, rebuilt) — ALL EIGHT
# COMPILED rc=0 with no diagnostic, and all 16 are green again on the restored
# tree. Every fixture therefore pins a refusal that did not exist before this
# arc, and none is a rename of a round-1 pin.
# Each pass twin asserts VALUES, not just exit 0, because the refusal message is
# byte-identical to what an OVER-refusing checker prints: the leak half alone
# cannot tell "hole closed" from "loan now immortal". Two twins carry an extra
# control in the same file — pass/bc_d1r2_dyn_erasure_admits uses a `Box<dyn
# Get>` over a NON-borrow-carrying value AFTER the mutation (the `Arc<dyn
# Snapshot>` ecosystem is what the wrong easy answer would have refused), and
# pass/bc_d1r2_ref_arg_hop_admits keeps a consuming `&`-arg call with a scalar
# result admitted.
# ⚠ THE DROPPED ASSERTION IS BACK — D1 ROUND 3 / F6. The Door A twin's `if z
# != 9i64 { return 3; }` was withheld here as a measured over-refusal (a use of
# a loan's TARGET counted as a use of its HOLDER). Root cause: ref_borrow_sources_
# was keyed per BINDING with no field path, so `w.b = c.mk()` could not express
# "the source of w.b was replaced" and the stale source survived to be re-rooted
# onto the new loan as a co-holder. The map is now keyed by PLACE and the
# assertion is restored in pass/bc_d1r2_place_write_field_admits, with both
# directions pinned (fail/bc_d1r3_f6_place_write_use_after_mut and
# fail/bc_d1r3_f6_place_write_source_escapes).
# PREDICTED 6981 / 3298 / 31 (+14 / +14 / 0 off the 6967 line), MEASURED
# 6980 / 3297 / 31 (+13 / +13 / 0), and the ONE-ROW GAP IS THE FINDING: the
# fourteenth fixture was fail/bc_d1r3_f4_box_ref_local, and it was WITHDRAWN
# rather than pinned. `Box::new(&c.v)` returned past its local still compiles;
# both gates built to catch it (a Ref/Slice type-arg in the erased list; a
# narrower "the construction retained a direct &local") were measured on the
# full `cmake --build` and each traded that leak for a wave of refusals of
# CORRECT code — `Iterator::reduce` for every SliceIter, then
# stdlib/mem/wql::scan_of on all eleven arms, then three PdtBuf/pkd functions.
# Per the standing rule that over-refusing the ecosystem is worse than a
# documented residual hole, the hole is written into borrow_check.cpp
# ("F4: THE DOCUMENTED RESIDUAL HOLE") and is NOT pinned in either direction —
# pinning the admit would write the leak down as intended. The erased-wrapper
# half IS closed and pinned (fail/bc_d1r3_f4_closure_local).
# D1 round 3 therefore pins six findings as 14 `bc_d1r3_*` fixtures, one ledger
# line each — LAUNDERING findings pinned as refuse+admit PAIRS, fixed
# OVER-REFUSALS pinned INVERTED (the admit is the pin; the neighbouring refusals
# are its keep-red control and are named in its header):
#   F0 fail/bc_d1r3_f0_call_byval_return        by-value bc arg across a free
#                                               Call laundered the return
#   F0 pass/bc_d1r3_f0_param_root_admits        same hop, borrow rooted at a
#                                               &-PARAM — stays admitted, VALUES
#   F1 fail/bc_d1r3_f1_method_arg_return        MethodCall consulted the
#                                               receiver only; the arg escaped
#   F1 pass/bc_d1r3_f1_method_arg_admits        same call, arg rooted at a
#                                               &-param, receiver &-param in
#                                               BOTH halves — VALUES
#   F2 pass/bc_d1r3_f2_recv_local_admits        INVERTED: an unrelated VALUE
#                                               receiver used to poison the
#                                               result. Keep-red: F1/F0 fails
#   F3 fail/bc_d1r3_f3_outparam_stash           B manufactured IN the callee and
#                                               stored through its &mut param
#   F3 pass/bc_d1r3_f3_scalar_admits            direction control: callee
#                                               carries NOTHING (result<-0)
#   F3 pass/bc_d1r3_f3_read_before_mut_admits   same call, read BEFORE the
#                                               mutation — NLL, not a lock
#   F4 fail/bc_d1r3_f4_closure_local            erased `Box<dyn Fn()->i64>`
#                                               capturing a &local
#   F4 pass/bc_d1r3_f4_closure_param_admits     identical return type, capture
#                                               by VALUE — the gate reads the
#                                               retention, not the type name
#   F5 pass/bc_d1r3_f5_shadow_admits            INVERTED: name-keyed loan expiry
#                                               revived a dead holder through a
#                                               SHADOW. Keep-red named below
#   F5 fail/bc_d1r3_f5_shadow_held              its keep-red control: the loan
#                                               really IS held
#   F6 fail/bc_d1r3_f6_place_write_use_after_mut  last use AFTER the mutation
#   F6 fail/bc_d1r3_f6_place_write_source_escapes dangling-source direction the
#                                               PLACE keys had to preserve
# F6's ADMIT half is not a new file: it is the restored third assertion in
# pass/bc_d1r2_place_write_field_admits (unchanged registry count), and that
# file now names the two F6 refusals above as its keep-red control.
# Same `.expected` glob as every bc_d1 pair, all 14 local (none under
# tests/imported, so ALL and -LE imported move together), and no tier_commit
# label rides a corpus fixture.
# SEQUENCE, honestly: 13 of the 14 were on disk and measured at 6980 / 3297 / 31
# in the step above. This step adds exactly ONE fixture pair
# (pass/bc_d1r3_f1_method_arg_admits + its `.expected`) — F1 had a refusal and
# no admit of its own, which left "method call with a by-value bc argument"
# pinned in one direction only. PREDICTED 6981 / 3298 / 31 (+1 / +1 / 0 off the
# 6980 measurement) BEFORE `cmake -S . -B build`; measured after it with
# `ctest -N` three ways, plus `ctest -N -R bc_d1r3` = 14. The counts below are
# the measurement.
#
# D1 ROUND 4 (N0/N1/N2/N3) adds ELEVEN fixtures — 7 refusals + 4 admits, all
# local (none under tests/imported, so ALL and -LE imported move together), no
# tier_commit label on any of them:
#   fail/bc_d1r4_n0_field_recv_summary_held     N0 — the peel; field-projection
#                                               receiver of a generic method
#   fail/bc_d1r4_n1_field_container_push_held   N1 — door 8b's place root
#   fail/bc_d1r4_n1_nested_field_push_held      N1 — two field steps
#   fail/bc_d1r4_n1_field_ref_elem_outlives     N1 — THE WITNESS ONLY N1 CLOSES
#                                               (stored_ref_elem/add_ref_sources
#                                               has no summary counterpart)
#   fail/bc_d1r4_n2_bare_closure_return_held    N2 — bare closure return type
#   fail/bc_d1r4_n2_bare_closure_plain_ref_held N2 — same, no bc struct at all
#   fail/bc_d1r4_n3_closure_struct_field_held   N3 — closure in a struct FIELD
#   pass/bc_d1r4_n0_field_recv_admits           read before the mutation (NLL)
#   pass/bc_d1r4_n1_field_container_admits      read before the mutation (NLL)
#   pass/bc_d1r4_n2_bare_closure_admits         captures an i64 PARAM only
#   pass/bc_d1r4_n3_closure_struct_field_admits struct w/ closure field, no
#                                               borrow captured
# PREDICTED 6992 / 3309 / 31 (+11 / +11 / 0) BEFORE re-running cmake; the gate
# then measured exactly that, which is the prediction earning its keep rather
# than a number copied out of a failure message.
#
# D1 round 5 adds 17 fixtures — 12 fail + 5 pass, none imported:
#   fail/bc_d1r5_h0_alias_out_param             H0, &mut reborrow local
#   fail/bc_d1r5_h0b_field_alias                H0, &mut self field spelling
#   fail/bc_d1r5_h1_reborrow_callsite           H1, call-site place root
#   fail/bc_d1r5_h1b_reborrow_struct            H1, struct spelling
#   fail/bc_d1r5_h2_chain_caller_first          H2, declaration order is inert
#   fail/bc_d1r5_h2_chain_callee_first          H2, the reversed twin
#   fail/bc_d1r5_h3_stmt_order_chain            H3, statement order is inert
#   fail/bc_d1r5_h4_closure_ret                 H4, closure-call result
#   fail/bc_d1r5_h4_closure_out                 H4, closure-call out-param sink
#   fail/bc_d1r5_h5_struct_pat                  H5, struct pattern
#   fail/bc_d1r5_h5_tuple_pat                   H5, tuple pattern
#   fail/bc_d1r5_h5_wild_pat_control            H5, the arm that WAS handled
#   pass/bc_d1r5_h0_alias_admits                read before the mutation (NLL)
#   pass/bc_d1r5_h1_reborrow_admits             read before the mutation (NLL)
#   pass/bc_d1r5_h4_closure_out_admits          read before the mutation (NLL)
#   pass/bc_d1r5_h4_closure_scalar_admits       i64-returning closure carries
#                                               nothing (result-type gate)
#   pass/bc_d1r5_h5_struct_pat_admits           read before the mutation (NLL)
# PREDICTED 7009 / 3326 / 31 (+17 / +17 / 0) BEFORE re-running the gate; it
# then measured exactly that.
#
# D1 round 5, SECOND WAVE — the two measured GAPS of the first wave, closed, plus
# the INERT-PROPERTY pins. 12 fixtures, 8 fail + 4 pass, none imported, none
# carrying a tier label (all are corpus registrations, so TIERCOMMIT is
# UNCHANGED — a +12/+12/+0 prediction, not +12/+12/+12):
#   fail/bc_d1r5_h8_match_tmp_scrutinee    H8, a TEMPORARY match scrutinee held
#                                          no loan; also H5b's uniqueness proof
#   fail/bc_d1r5_h8_match_named_twin       H8, naming the scrutinee is INERT
#   fail/bc_d1r5_h8_matchexpr_tmp          H8, the match-as-VALUE spelling
#   fail/bc_d1r5_h0_noalias_twin           H0, a named reborrow is INERT
#   fail/bc_d1r5_h1_noreborrow_twin        H1, the call-site half of the same
#   fail/bc_d1r5_h3_stmt_order_forward     H3, statement order is INERT
#   fail/bc_d1r5_h7_declared_bc_struct_held  H7, F4's residual: the DECLARED
#                                          borrow-carrying edge refuses
#   fail/bc_d1r5_h6_local_body_twin        H6, the callee body in THIS file
#                                          refuses (the archived twin does not —
#                                          written down, deliberately unpinned)
#   pass/bc_d1r5_h8_match_tmp_admits       read through, THEN mutate (NLL)
#   pass/bc_d1r5_h8_match_scalar_admits    a temporary carrying no loan ties none
#   pass/bc_d1r5_h7_param_root_admits      H7, the PARAM-rooted edge admits
#   pass/bc_d1r5_h6_cross_archive_admits   links libbcxa.a; reads before mutating
# The archive's own source (tests/logos/bc_cross_archive/bcxa/bcxa.logos) is NOT
# a corpus file — corpus_registration_gate.sh walks pass/ fail/ only — so it adds
# no registration and needs no ledger line.
# PREDICTED 7021 / 3338 / 31 (+12 / +12 / 0) BEFORE re-running cmake; the gate
# then measured exactly that, three ways.
#
# D1 round 6 (G0 field-place reborrow, G1 fn-pointer summaries, D2 arm binding
# over a deferred scrutinee) adds NINE corpus fixtures and no gate:
#   pass/ctr_family_match_next_batch          the `match` door on a family stream
#   pass/bc_d1r6_g0_field_admit               G0's admit twin
#   pass/bc_d1r6_g1_fnptr_scalar_admit        G1's admit twin (scalar callee)
#   fail/bc_d1r6_g0_field_reborrow            `h.r` IS `vs`
#   fail/bc_d1r6_g0_reborrow_use_after_mut    the re-homed loan must not shorten
#   fail/bc_d1r6_g1_fnptr_result              result half through a fn pointer
#   fail/bc_d1r6_g1_fnptr_outparam            out-param half
#   fail/bc_d1r6_g1_fnptr_reassigned          two callees ⇒ unresolvable
#   fail/bc_d1r6_g1_fnptr_opaque              the SUMMARY was empty, not the site
# PREDICTED 7030 / 3347 / 31 (+9 / +9 / 0); the gate measured exactly that.
#
# D1 round 6, MATRIX CLOSURE: the D2 rule's over-refusal direction was measured
# with a probe in /tmp, i.e. by an artefact that dies with the session and can
# vouch for nothing afterwards. A refusal rule is invisible to its own refusing
# fixture, so the admit half is the only thing that separates "closed hole" from
# "the arm stopped type-checking". Promoted into the corpus, ONE more fixture:
#   pass/bc_d1r6_d2_generic_param_admit       legitimate `T` binding, VALUES asserted
# PREDICTED 7031 / 3348 / 31 (+1 / +1 / 0) BEFORE re-running cmake; the gate then
# measured exactly that, three ways.
#
# D1 round 7 lands FOUR rules — R7a rule 1 (a call result inherits the loans of
# a named reborrow's REFERENT, not just of the terminal name), R7a rule 2 (a
# SHARED reborrow is a reborrow: `&` joins `&mut` at all three recording doors),
# R7b (a destructuring `let` over an Error-typed rhs DEFERS instead of erroring
# out of the unit), R7b's residue (field privacy is checked at the destructuring
# `let`, the third door after the field read and the struct literal) — and until
# now they were PINNED BY NOTHING: the corpus stopped at bc_d1r6_*, every round-7
# probe lived in /tmp, and each rule's control revert therefore reddened exactly
# zero tests. TEN corpus fixtures, no gate, five refusing and five admitting:
#   fail/bc_d1r7_a1_pull_through_reborrow      rule 1, THE FINDING VERBATIM
#   fail/bc_d1r7_a1_family_next_batch_reborrow rule 1, the ADR 0025 customer:
#                                              `r.next_batch().unwrap()` held
#                                              across `insert` behind a named
#                                              reborrow — emitted types, hashed
#                                              instance, freed leaf
#   fail/bc_d1r7_a2_shared_reborrow            rule 2, `let r: &S = &s;`
#   fail/bc_d1r7_b1_destructure_name_mismatch  R7b, the deferral ESCALATES
#   fail/bc_d1r7_b2_destructure_private_field  the residue, the permissive hole
#   pass/bc_d1r7_a1_no_loan_admit              rule 1 ac1: no loan ⇒ no-op
#   pass/bc_d1r7_a1_scope_release_admit        rule 1 ac2: the loan still dies
#   pass/bc_d1r7_a2_shared_read_then_mut_admit rule 2's over-refusal direction
#   pass/bc_d1r7_b1_destructure_deferred       R7b, the door OPENS (PdtCol over a
#                                              factory-backed chain), values asserted
#   pass/bc_d1r7_b2_destructure_same_package_admit  privacy is per PACKAGE
# THE ATTRIBUTION MATRIX, measured one revert at a time with a restored green
# checkpoint between every pair (four reverts, four restores, eight builds):
#   rule 1 OUT → 3 red (a1_pull, a1_family, a2_shared)      restore → 0 red
#   rule 2 OUT → 1 red (a2_shared)                          restore → 0 red
#   R7b    OUT → 2 red (b1_deferred, b1_name_mismatch)      restore → 0 red
#   residue OUT→ 1 red (b2_private_field)                   restore → 0 red
# Each rule is individually load-bearing: rule 2's revert reds ONLY its own
# fixture, and rule 1's two fixtures stay green under it, so the one fixture the
# two rules SHARE (a2_shared needs the shared reborrow recorded AND the referent
# inherited) does not have to separate them. The R7b branch was additionally
# measured from the inside with a fire-count print — 1 fire in each of the two
# b1 fixtures, 0 in the privacy pair and 0 in pass/ctr_family_cursor_then_mut,
# which is what makes the residue an INDEPENDENT finding and not a side effect
# of the deferral — then removed.
# STATED MATRIX GAP: pass/bc_d1r7_b1_destructure_deferred binds NO names. Every
# field reachable through a deferred family chain is private to its defining
# package, and the emitted types whose fields are `pub` are named by content hash
# (`Hs…Cur`), which no source file can spell. The BINDING half of the deferred
# destructure is therefore unpinnable until a pub-field struct exists on a
# deferred chain — a stdlib change, not a test change. What is pinned today: the
# door opens, the unit survives, the values come out right, and the fail twin
# proves the deferral still escalates.
#   ⚠ SUPERSEDED 2026-08-12 (D1 round 8 / S0). The paragraph above is WRONG on
#   its load-bearing clause and the correction is a compiler fix, not a test
#   one. A content-hashed name DOES resolve in PATTERN position — the binding
#   half's actual diagnostic was «unknown field 'found'», i.e. the TYPE had
#   resolved and the FIELD lookup was what failed. It failed because the
#   pattern path called `field_type_of` with two arguments while the expr path
#   passes the receiver's `pkg_name()` as a third (sema_stmt.cpp vs sema.cpp);
#   without the hint the lookup is import-scope dependent and misses a
#   metaprog-emitted struct from another package. An OVER-REFUSAL in sema, one
#   layer below the deferral. Fixed, and the binding half is now pinned IN
#   pass/bc_d1r7_b1_destructure_deferred (it binds `found` and reads it in both
#   directions). What remains true: the hash name does NOT resolve in TYPE
#   position, so the pattern name is the only spelling — and pinning it pins
#   the family MANGLER, which that fixture's header now says out loud.
# PREDICTED 7041 / 3358 / 31 (+10 / +10 / 0 — ten `.expected` files under
# tests/logos/{pass,fail}, which the corpus glob registers one test each, and no
# new gate, so tier_commit is unmoved) BEFORE re-running cmake; the gate then
# measured exactly that, three ways.
# D1 round 8: PREDICTED 7047 / 3364 / 31 (+6 / +6 / 0 — SIX new `.expected`
# files, and no new gate, so tier_commit is unmoved). The six, named because a
# count nobody can decompose is a number and not a prediction:
#   fail/bc_d1r8_u0_twohop_arg          — U0, the two-hop chain in ARG position
#   fail/bc_d1r8_u1_field_rhs_bind      — U1, a ref bound from a struct FIELD
#   fail/bc_d1r8_u3_pattern_bind        — U3, a destructuring `let`
#   pass/bc_d1r8_u0_twohop_read_before_mut_admit      — U0's admit control
#   pass/bc_d1r8_u1_field_rhs_read_before_mut_admit   — U1+U3's admit control
#   pass/bc_d1r8_m0_closure_captured_ref — the fixed closure captured-`&Struct`
#                                          mlir_gen over-refusal
# The round's S0 half added NO file: the R7b binding pin went INTO the existing
# pass/bc_d1r7_b1_destructure_deferred, whose header claimed it was unpinnable.
# Nor did the round's stated MATRIX GAP, closed after the first measurement:
# pass/bc_d1r8_u1_field_rhs_read_before_mut_admit said "both new recording
# shapes" and carried only U1's, so U3's widening had a red fixture and no
# admit control; U3's shape was added to that same file (its own objects, exit
# codes 3/4) rather than as a seventh fixture. Both are edits, not additions,
# so the +6 stands. The gate then measured exactly that, three ways — and again
# after the gap edit, unchanged.
#
# D1 round 9, FIRST HALF: PREDICTED 7059 / 3376 / 31 (+12 / +12 / 0 off round
# 8's 7047 / 3364 / 31 — TWELVE new `.expected` files, no new gate). ⚠ THIS
# PARAGRAPH IS WRITTEN LATE. The twelve landed and the three numbers below were
# bumped to match, but the ledger sentence that decomposes them was never
# written, so for one step the pin block carried round-9 COUNTS under a round-8
# EXPLANATION — a number nobody can decompose, which is the exact thing the
# round-8 paragraph above says a prediction must not be. Reconstructed and
# named here, one line per file:
#   fail/bc_d1r9_p12_twohop_structlit_addrof — P12, the permissive REGRESSION
#                                              r8 introduced (rc=1 at HEAD)
#   fail/bc_d1r9_n0_nested_structlit         — N0, nested literal records
#                                              nothing (unsound, HEAD too)
#   fail/bc_d1r9_n0_inner_binding_nested     — N0, inner aggregate as its own
#                                              binding, then nested
#   fail/bc_d1r9_n0_aggregate_copy_out       — N0, the aggregate copied back
#                                              OUT of the nested place
#   fail/bc_d1r9_s1_summary_field_store      — S1, the summarizer loses `out1`
#                                              for a `&mut` stored in a FIELD
#   fail/bc_d1r9_f0_retarget_held            — F0's abuse direction: use while
#                                              the field is still live
#   fail/bc_d1r9_f0_retarget_leak            — F0, the refusal that must
#                                              survive the inversion
#   pass/bc_d1r9_p12_read_before_mut_admit   — P12's admit control
#   pass/bc_d1r9_n0_read_before_mut_admit    — N0's admit control
#   pass/bc_d1r9_s1_read_before_mut_admit    — S1's admit control
#   pass/bc_d1r9_f0_retarget_then_use_admit  — F0 INVERTED: the legal
#                                              retarget-then-use now admits
#   pass/bc_d1r9_f0_retarget_overlap_admit   — F0, source/destination overlap
#
# D1 round 9, MATRIX-GAP CLOSURE: PREDICTED 7061 / 3378 / 31 (+2 / +2 / 0 off
# 7059 / 3376 / 31), stated BEFORE the reconfigure and then measured three
# ways. TWO gaps were found in the twelve above; ONE of them costs files.
#
#   GAP A, closed and it costs the two: the P12 fix widened FOUR aggregate
#   arms (EnumLitData, StructLit, TupleLit, ArrLit) from
#   `is_borrow_carrying_type` to `type_may_carry_borrow`, and only StructLit
#   had a fixture — shape-general fix, one shape exercised. The enum arm is
#   reachable and now pinned in both directions:
#     fail/bc_d1r9_p12_enum_payload_addrof         — the same two-hop alias
#                                                    into an ENUM payload `&`
#     pass/bc_d1r9_p12_enum_read_before_mut_admit  — its admit control
#   MEASURED BY CONTROL REVERT, not inferred: with the four gate lines put
#   back (/tmp/bcr9/ctl/REV_P12.cpp swapped in, full rebuild, md5-asserted both
#   ways, restored to a green checkpoint) the refusing half COMPILES — rc=0,
#   the unsound admit — and the admit half is rc=0 under BOTH, which is what
#   makes the pair a separation test and not two copies of one file.
#
#   GAP A's REMAINDER, STATED because it has no witness to close it with:
#   TupleLit and ArrLit stay unexercised, and cannot be exercised. The whole
#   D1 mechanism is keyed on the CONTAINER carrying `#[borrow_carrying]`, and
#   a tuple or array literal cannot carry the attribute. Measured, not
#   assumed: `let t: (&i64, i64) = (&rc2.v, 7i64);` and `let a: [&i64; 1] =
#   [&rc2.v];` both admit — and so does the bare `let p: &i64 = &rc2.v;` with
#   no aggregate at all, which locates that admission in the arc's standing
#   SCOPE (D1 tracks provenance through `#[borrow_carrying]` values) rather
#   than in anything the widening opened. Two of the four arms are therefore
#   generality carried for free, priced here so the next reader does not
#   re-measure it.
#
#   GAP B, closed WITHOUT a file — the same gap round 8 recorded above,
#   repeated: N0 widened the place recorder in THREE spellings, each got a
#   refusal fixture, and only the nested-literal one had an admit control, so
#   two of three widenings were pinned in the refusing direction only. The
#   other two shapes went INTO pass/bc_d1r9_n0_read_before_mut_admit with
#   their own objects and DISTINCT contributions (3 + 5 + 7, exit 3 -> 15) so
#   a shape that stops compiling, or one whose read picks up the
#   post-mutation value, moves the exit code instead of hiding in a sum. An
#   edit, not an addition, so the +2 stands.
#
# D1 round 10: PREDICTED 7074 / 3391 / 31 (+13 / +13 / 0 off round 9's final
# 7061 / 3378 / 31 — THIRTEEN new `.expected` files, no new gate), stated
# before the reconfigure and then measured three ways (`ctest -N`, `-LE
# imported`, `-L '^tier_commit$'`) = 7074 / 3391 / 31, agreeing exactly. The
# decomposition, one line per file, written WITH the numbers this time (round
# 9's paragraph above is the reason that sentence is now part of the step):
#   fail/bc_d1r10_j0_while_call_summary    — J0, a summary-raised loan dropped
#                                            at the `while` join
#   fail/bc_d1r10_j0_for_call_summary      — J0, the `for` spelling (a second
#                                            statement kind into the same join)
#   fail/bc_d1r10_j0_if_empty_else         — J0, the ONE-ARMED `if`; the
#                                            both-arms shape hid it
#   fail/bc_d1r10_j0_match_second_arm      — J0 at the match STATEMENT join,
#                                            which never merged loans at all
#   fail/bc_d1r10_j0_matchexpr_second_arm  — J0 at the match EXPRESSION join
#   fail/bc_d1r10_j0_ifexpr_then_arm       — J0 at the if EXPRESSION join
#   fail/bc_d1r10_e0_root_rebind_strands_alias — E0, a root rebind's
#                                            `erase_under` stranding a live
#                                            alias recorded one hop deep
#   fail/bc_d1r10_sp0_aggregate_composed_in — SP0, a recorded aggregate
#                                            composed INTO another aggregate
#   fail/bc_d1r10_sp1_aggregate_copied_out — SP1, the same U2 gate, copied OUT
#   fail/bc_d1r10_sp2_field_retarget       — SP2, the callee-side field
#                                            retarget (a DerefWrite door with
#                                            no alias edge)
#   pass/bc_d1r10_j0_region_scoped_admit   — J0's admit control: the
#                                            region-local loan the deleted
#                                            restriction was written to protect
#   pass/bc_d1r10_e0_rebind_alias_dead_admit — E0's admit control: the freeze
#                                            must not make an alias immortal
#   pass/bc_d1r10_sp_scalar_payload_admit  — SP0-SP2's admit control, all three
#                                            shapes over a borrow-free payload
# The six J0 files are SIX SITES, not one finding spelled six ways: each was
# control-reverted at its own join and each refuses only with its own merge
# restored. The three SP shapes share ONE admit control, on the round-8/9
# precedent — distinct objects and distinct contributions inside one file, so a
# shape that stops compiling moves the exit code instead of hiding in a sum.
#
# D1 round 10, MATRIX GAP — STATED, NOT CLOSED, because closing it is a design
# decision and not an omission. SP0/SP1's fix is the U2 gate widened to "the
# stored value IS, or transitively CONTAINS, a `&mut`", fed by the ONE store
# enumeration; that enumeration recurses into a STRUCT literal only. Measured,
# not inferred, with a two-program separation over the same skeleton:
#   • struct spelling — `let inn = Inner{r:v}; let h = Hold{i:inn}; let mut y =
#     h.i; y.r.push(c.mk());` — rc=1, and LOGOS_DUMP_FLOWS prints
#     `result<-0 out1<-0x1 (rounds=3)` for the callee;
#   • ENUM spelling — the same body with `let e = E::A(inn);` and the payload
#     taken back out through a match arm — rc=0 (the caller's `c.bump()`
#     compiles), and the callee gets NO summary line at all, which is SP0's own
#     signature ("the summary was lost WHOLE").
# ROOT, located: an enum payload has no field NAME, so the place-keyed
# enumeration in `src/compiler/borrow_check.cpp` has nothing to name it with,
# and a positional component (`e.0`) would have to be agreed by BOTH graph
# instances — the checker retracts by place, the summarizer charges to root —
# so it is a round-11 step with its own control revert, not a line to append
# here. It is a PRE-EXISTING permissive hole, not a round-10 regression: round
# 10 only ADDED alias edges, and the struct twin admitted at HEAD too (that is
# exactly what SP0/SP1 are). Recorded so the next round starts from a witness
# instead of re-deriving one.
#
# D1 round 11: PREDICTED 7088 / 3405 / 32 (+14 / +14 / +1 off round 10's final
# 7074 / 3391 / 31 — FOURTEEN new `.expected` files and ONE new gate), stated
# before the reconfigure and then measured three ways (`ctest -N`, `-LE
# imported`, `-L '^tier_commit$'`) = 7088 / 3405 / 32, agreeing exactly. The
# decomposition, one line per file:
#   fail/bc_d1r11_x0_nested_if_break       — X0, the loop-EXIT collector; the
#                                            exit sits in a nested `if`, so the
#                                            if-join is skipped too
#   fail/bc_d1r11_x0_nested_if_break_nocallee — X0's no-callee twin: the loan is
#                                            raised inline, so the witness pins
#                                            the JOIN and not the summarizer
#   fail/bc_d1r11_x0_nested_if_continue    — X0, the `continue` spelling
#                                            (redundant across the two joins —
#                                            the only one of the four that is)
#   fail/bc_d1r11_x0_backedge_continue     — X0 at the BACK EDGE: the use sits
#                                            INSIDE the loop, above the raise
#   pass/bc_d1r11_x0_backedge_local_admit  — X0's admit control, back-edge arm
#                                            (a loop-LOCAL holder crosses
#                                            nothing)
#   pass/bc_d1r11_x0_break_noloan_admit    — X0's admit control, `break` with no
#                                            loan on the path
#   pass/bc_d1r11_x0_break_region_local_admit — X0's admit control, a
#                                            region-scoped loan leaving via
#                                            `break`
#   fail/bc_d1r11_x1_prospective_alias     — X1, a CALL RESULT is a prospective
#                                            reborrow (`ref_source_places` had
#                                            no Call arm)
#   fail/bc_d1r11_x1_result_subplace       — X1's second half: a mask bit names
#                                            a PARAMETER, so every place UNDER
#                                            the seeded one is seeded with it
#   pass/bc_d1r11_x1_result_dead_admit     — X1's admit control: the seeded edge
#                                            is a REBORROW record, not a loan
#   pass/bc_d1r11_x1_nosummary_admit       — X1's FALLBACK route (a `dyn`
#                                            receiver has no summary, the arm
#                                            contributes nothing). A ROUTE
#                                            witness, not a verdict
#                                            discriminator, and labelled as one
#                                            in the file: no revert of the arm
#                                            can red it, so the route is proved
#                                            by a fire counter INSIDE the
#                                            null-summary branch (this file: 1
#                                            entry, 1 no-summary exit, 0 seeds;
#                                            the three refusal witnesses above:
#                                            2 entries, 1 summary exit, 1 seed
#                                            each). Counter removed, sources
#                                            md5-identical after restore.
#   fail/bc_d1r11_x2_byvalue_result        — X2, a BY-VALUE aggregate param lost
#                                            the result mask on field
#                                            projection (`can_carry` said no)
#   pass/bc_d1r11_x2_byvalue_scalar_admit  — X2's admit control: the widened
#                                            `can_carry` must still say NO to a
#                                            scalar aggregate
#   fail/bc_d1r10_sp0_aggregate_composed_in — NOT a new file: RE-AUTHORED. See
#                                            below; it is why the count is +14
#                                            and not +15.
#   (gate) logos_00_bc_flow_mask           — X3's assertion, and the +1 on
#                                            TIERCOMMIT. X3 (name-keyed charging
#                                            manufacturing param-to-param edges
#                                            on disjoint field stores) moves NO
#                                            verdict anywhere in the stdlib, so
#                                            no `.expected` can pin it in either
#                                            direction; the gate reads the masks
#                                            themselves (LOGOS_DUMP_FLOWS) and
#                                            asserts them positively AND
#                                            negatively, with a vacuity floor
#                                            (MIN_FLOWS) so an empty dump
#                                            cannot pass. Its two inputs:
#                                            tests/logos/bc_flow_mask/x3_wire3.logos
#                                            tests/logos/bc_flow_mask/x3_sp0_reauth.logos
#
# D1 round 11, THE NON-WITNESS PAID OFF. fail/bc_d1r10_sp0_aggregate_composed_in
# was registered by round 10 as SP0's refusal witness and was not one: its
# tested line (`h.i = inn`) was REDUNDANT, because `Mid { i: Inner { r: v } }`
# already aliased `v` through the literal. Measured at HEAD, one-at-a-time, with
# a green checkpoint on both sides: with the SP0 arm of the U2 gate reverted
# (`bc_holds_mut_ref_type` dropped from the store door in
# borrow_flow_summary.inc, logosc rebuilt, md5-asserted on restore) the OLD file
# stayed rc=1 — the rule it names could be deleted without moving it. Re-authored
# per the recipe: `h` is now built over a DIFFERENT vec, so the aggregate STORE
# is the sole alias path, and the second vec is dead at the offending line so
# only the first can produce the diagnostic. The revert now reds it — 3 reds
# under the control (this file, sp1, and the X3 mask gate whose twin fixture is
# the same shape) and 159/159 green with the arm restored.
REGISTRY-ALL         7088
REGISTRY-NOIMPORTED  3405
REGISTRY-TIERCOMMIT  32

# §3 table arithmetic. UNCHANGED BY THE CUT, and deliberately so: the class
# column records how each row was PRICED before the deletion, so the loss ledger
# keeps its shape. What changed is which rows are LIVE — see GONE-FIXTURE below.
CENSUS-ROWS          86
CLASS-A              39
CLASS-B              26
CLASS-C              19
CLASS-D              1
CLASS-G              1

# The rows P5 killed, one line each, with a cause of death. FACT 3 skips them and
# FACT 8 then requires the `.logos` AND its `.expected` to be absent and the line
# to say why. This is the loss ledger, machine-checked rather than narrated: a
# restored fixture reds here the same day, and so does a `.logos` deleted while
# its `.expected` is left behind to register a test with no program.
GONE-FIXTURE  tests/logos/pass/deem_dred_phases23_spec.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_adv_errvalues.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_compile_robust_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_diff_fuzz.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_diff_str_adv.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_el_arith_err_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_gpath_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_budget_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_ctl_journal_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_factstore_e2e_join.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_factstore_e2e_rec.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_guard.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_join_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_join_fuzz.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_journal_replay.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_nasty_join.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_nasty_sssp.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_nasty_tc.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_prov_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_s4_select_one.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_sssp_fuzz.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_sssp_guard.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_tc_fuzz.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_tc_guard.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_tc_retract.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_trace_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_interp_smoke.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_metamorphic_adv.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_observer_l1.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_parser_robust_advX.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_proto_observer_l0.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_rec_agg_batch_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_run_errors_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_incr_rec_agg_retract_lattice.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_incr_rec_dred_error_window.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_factstore_unit.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_factstore_epochs.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_incr_factstore_float_identity_unit.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_dyn_bool_arith_pinned.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_lenient_e2e.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_lenient_null_fuzz_adv.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/query_u64_ordw_origin.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/vfy_nan_key_probe.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_carrier_positions.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_incr_disagreement.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_runtime_extremes.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_runtime_order_a.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_runtime_order_b.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_runtime_order_c.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_domain_u64_order_seams.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_u64_sum_accumulator.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_u64_sum_scalar_arith.logos  died with its subject at P5 (see its §3 row)
GONE-FIXTURE  tests/logos/pass/wql_engine_source_e2e.logos  died with its subject at P5 (see its §3 row)
RENAMED-FIXTURE  tests/logos/pass/wql_agg_avg_bool_three_engines.logos  tests/logos/pass/wql_agg_avg_bool_value_rule.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/query_incr_f64_agg_three_engines.logos  tests/logos/pass/query_f64_agg_hand_derived.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/query_order_by_float_static_vs_dynamic.logos  tests/logos/pass/query_order_by_float_data_key.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/wql_incr_static_three_ways.logos  tests/logos/pass/wql_incr_static_two_ways.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/wql_incr_retract_three_ways.logos  tests/logos/pass/wql_incr_retract_two_ways.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/wql_tier_capability_disagreement.logos  tests/logos/pass/wql_u64_capability_matrix.logos  P5: the old name stated a COUNT of engines the cut falsified
RENAMED-FIXTURE  tests/logos/pass/wql_value_domain_tiers_measured.logos  tests/logos/pass/wql_value_domain_measured.logos  P5: the old name stated a COUNT of engines the cut falsified

# The population rule, executable, in the form FACT 6 now uses. ⚠ THIS LIST WAS
# CORRECTED BEFORE THE FACT WAS RE-AIMED, because its own comment used to assert
# "every name here is sole-defined in a §2 file" and that was FALSE for nine of
# the 28: `Tpl`, `Chk`, `chk_new`, `sx_of`, `rbinds_new`, `eval_sexpr` and
# `RBinds` live in the SURVIVING stdlib/mem/deem/tpl.logos, and `h_step`,
# `ts_scan` in the SURVIVING stdlib/mem/deem/graphsrc.logos. The list still
# produced the right answer, by luck — the extra names co-occurred with real ones
# — which is the failure mode FACT 6 exists to prevent, applied to FACT 6 itself.
# The nine are dropped and `es_scan` (sole-defined in exec.logos, moved back there
# at 4569535c precisely so it would die with the engine) is added.
CUT-SYMBOL  qplan_new
CUT-SYMBOL  check_rexpr
CUT-SYMBOL  QPlan
CUT-SYMBOL  relctx_new
CUT-SYMBOL  exec_root
CUT-SYMBOL  rt_key_hash
CUT-SYMBOL  RelCtx
CUT-SYMBOL  OutTab
CUT-SYMBOL  es_scan
CUT-SYMBOL  Query
CUT-SYMBOL  QRows
CUT-SYMBOL  FactStore
CUT-SYMBOL  FactHistory
CUT-SYMBOL  IncrRec
CUT-SYMBOL  IncrJoin
CUT-SYMBOL  EngineState
CUT-SYMBOL  deem_state_trace
CUT-SYMBOL  deem_state_epochs
CUT-SYMBOL  deem_state_tail
CUT-SYMBOL  deem_state_controls

# Files the population rule finds whose every match is a COMMENT or a ledger row.
# Measured: with `sed 's://.*::'` applied, none of these matches a CUT-SYMBOL.
# ⚠ RE-MEASURED AFTER THE CUT AND UNCHANGED — same eleven files. That is not a
# coincidence worth glossing: the twenty surviving fixtures that mention a cut
# symbol do so in their own P5 notes and are LIVE ROWS, so they are accounted for
# by the row column and not by an exemption.
NOT-AFFECTED  tests/logos/census_pin_gate.sh                            comment-only
NOT-AFFECTED  tests/logos/incr_eligibility_gate.sh                     comment-only
NOT-AFFECTED  tests/logos/pass/wql_domain_static_ordw_origin.logos     comment-only
NOT-AFFECTED  tests/logos/pass/wql_incr_eligibility_matrix.logos       comment-only
NOT-AFFECTED  tests/logos/fail/wql_domain_layer_map_param_u8_fail.logos comment-only
NOT-AFFECTED  tests/logos/pass/deem_incr_join_e2e.logos                 comment-only
NOT-AFFECTED  tests/logos/pass/wql_alias_element_e2e.logos              comment-only
NOT-AFFECTED  tests/logos/pass/wql_incr_rel_dred_mutrec_full.logos      comment-only
NOT-AFFECTED  tests/logos/pass/wql_source_trait_e2e.logos               comment-only
NOT-AFFECTED  tests/logos/shared_ref_ub.ledger                          ledger-row
NOT-AFFECTED  tests/logos/wql_shadowed_column_gate.sh                   comment-only
```
