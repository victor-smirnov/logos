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
#                                            tests/logos/bc_flow_mask/a1_loopbreak.logos
#                                            (round 12 / A1)
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
# D1 round 12 (A0/A1/A2): +10 corpus tests, +0 tier_commit. A0 the MatchExpr arm
# of ref_source_places (fail x2 — the match witness and the `if` twin that always
# refused — plus pass x1); A1 the `break v` deposit into a loop's break slot in
# BOTH walkers (fail x2 + pass x1, plus a bc_flow_mask ROW, which registers no
# test of its own: the summarizer half moves no verdict, so the existing gate
# grew a fixture — bc_flow_mask/a1_loopbreak.logos — and its floor went 2 -> 4);
# A2 the PROSPECTIVE half of apply_flow_outparams (fail x3 — the witness and BOTH
# ordering discriminators — plus pass x1). 7 fail + 3 pass = the +10 below.
# D1 round 13 (P0/P1/P2/P3): +18 corpus tests, +0 tier_commit. P0 the pattern
# binding as a reborrow node, three spellings (fail x5 — struct destructure,
# enum payload, `?`, plus the two one-variable twins that always refused — and
# pass x3); P1 the tuple/array holders (fail x3 — the tuple witness, the array
# witness, the struct twin — plus pass x2); P2 the out-param deposit chased
# through the reborrow edge (fail x2 — the witness and the direct-param twin —
# plus pass x1); P3 the out-param SEED predicate (fail x1 + pass x1, plus a
# bc_flow_mask ROW, which registers no test of its own — the mask moves no
# verdict on its own, so the existing gate grew a fixture,
# bc_flow_mask/p3_byvalue_outparam.logos, and its floor went 4 -> 6).
# 11 fail + 7 pass = the +18 below.
#
# D1 ROUND 14 (+19: 12 fail + 7 pass) — THE FOURTH CHANNEL. Re-running the
# mechanical coverage table with the pattern propagators added as a FOURTH
# COLUMN showed that `collect_ref_sources_paths` (§B6) had never had round 8's
# one-shape-enumeration treatment: it answers a different question for a
# DIFFERENT VERDICT (`pop_scope`'s E0597, not the `c.bump()` mutation refusal
# every earlier round used as a witness), so eight omissions in it were
# invisible to twelve rounds of witnesses. Q1 MatchExpr, Q3 Deref, Q4 Closure /
# Q5 FnPtr call results, Q6 the Call-arm gate (`Option<&T>`), Q8 the FieldRead
# gate round 13 deliberately deferred with a standing "needs its own probe
# pair" note; plus Q7, two of the four pattern propagators missing at the
# rvalue-match site (which is where `?` actually arrives).
# 12 fail (7 witnesses + 5 twins) + 7 pass admit controls = the +19.
#
# ADR 0025 S1, THE SPELLABILITY LAYER (+4: 2 pass + 2 fail, +0 tier_commit).
# `CtrLeafFamily` (the assoc-type door a hand-written consumer needs for the
# generated `{N}LeafBatch`/`{N}LeafWalk`) plus the traversal trait pair
# `Bidirectional`/`RandomAccess` in logos.lang.stream. PREDICTED 7139/3456/32
# before the reconfigure and measured identical after.
#   pass/ctr_leaf_family_spelling        the projection in a hand-written fn's
#                                        parameter AND return, values asserted
#                                        end-to-end, plus seek_nth/prev on the
#                                        family walk (oracle: the container's
#                                        own per-row cursor)
#   pass/stream_traversal_buffer         the same axis on the degenerate stream
#   fail/ctr_leaf_family_vector_refused  the abuse direction: a vector family
#                                        has no leaf walk. ⚠ RE-AIMED AT S1b as
#                                        fail/ctr_leaf_family_volume_refused —
#                                        the vector arm GAINED a leaf-batch
#                                        producer, so the claim stopped being
#                                        true of it and the abuse direction moved
#                                        to the VOLUME arm rather than being
#                                        weakened away
#   fail/ctr_leaf_family_wrong_family    the alias is LOAD-BEARING — two
#                                        families, distinct hashes in the
#                                        message
# Both pass fixtures are held by a CONTROL REVERT, run one clause at a time with
# the tree restored (md5-verified) and rebuilt green in between: neutering
# `seek_nth` reds them at 40 / 21, neutering `prev`/`retreat` at 50 / 42.
#
# ADR 0025 S1, THE EMITTER COLLAPSE — THE MEMORIA SCAN (+1 pass, +0 tier_commit).
# The generated ordered_map family now DECLARES the leaf-batch producers
# (`rel entry = #browsfn`, and the three narrowing landings likewise), the
# natspec carries a `b` flag beside `i` (`producer_batches_` =
# `sema_has_impl_recursive("BatchStream", …)`, the exact twin of
# `producer_streams_`), and both consumers of a batch producer emit ADR §1's
# one shape — `next_batch()` outside, the old row loop inside.
#   pass/deem_batch_scan_drain  the DRAINED leg, which no fixture in the tree
#                               exercised at all: `order by`, an aggregate, and
#                               a narrowed landing under a sort, each against
#                               the container's own per-row cursor as an
#                               oracle, plus the streamed leg beside them.
# The STREAMED leg was already covered (deem_ctr_family_streams, source_size,
# cross_domain_join, three_domain_join) and its shape change was measured on the
# EMITTED ARTIFACT (`--gen-dir`) rather than inferred from a green.
# PREDICTED 7140/3457/32 before the reconfigure and measured identical after.
#
# ADR 0025 S1, THE TWO SLICE-GATES REGISTERED (+2 pass + 2 gates, +0 tier_commit).
# S1's own acceptance gates existed as SENTENCES in the ADR and as nothing in the
# tree. Both are now artifacts, and both close a hole that is invisible to a green
# corpus BY CONSTRUCTION — the recurring class: a fixture asserts the ANSWER of a
# scan, and every defect these two catch leaves the answer alone.
#   pass/wql_slice_scan_shape       one slice source, one `where`, one projection
#                                   — the reference the byte-comparison needs.
#                                   Also a plain pass test (rows asserted, empty
#                                   and full answers from the same loop).
#   pass/ctr_leaf_descent_count     the MINIMAL two-traversal program: the
#                                   container's own per-row cursor and one
#                                   leaf-batch scan in §1's shape, nothing else,
#                                   so every counted call has one possible origin.
#                                   Its own exit code pins rows/order/sums/batch
#                                   count against the oracle.
#   logos_09_slice_scan_codegen     GATE 1. The emitted `slice_scan_run` byte-for-
#                                   byte against the golden checked in beside the
#                                   gate (`tests/logos/slice_scan_shape.golden`),
#                                   plus: the golden must be big enough to
#                                   be an assertion, must carry the indexed loop
#                                   and the `where`, and must carry NONE of §1's
#                                   batch vocabulary — the honest S1 state written
#                                   as an assertion. S1 recorded this gate as NOT
#                                   RUN (no perturbation existed to compare
#                                   against); it is now the perturbation detector
#                                   itself. CONTROL: changing `>=` to `>` in the
#                                   fixture's `where` reds it with the one-line
#                                   diff; and so does changing the EMITTER — an
#                                   answer-preserving `let __n0: i64 = (rows).len();`
#                                   hoist added to this arm's emitted block in
#                                   rexpr_walk.logos reds it on that one added line
#                                   while the fixture stays green. Restored
#                                   md5-identical, rebuilt, re-run green.
#   logos_09_ctr_leaf_descent       GATE 2. §5's asymptotics measured, not
#                                   commented: callgrind call counts (never a
#                                   duration — shared box), attributed BY CALLER so
#                                   the batch plane's descents and the oracle's are
#                                   counted apart. MEASURED: 1000 rows scanned in 8
#                                   descents over 8 leaves, against 1000 per-row
#                                   container calls on the oracle side; batch pulls
#                                   9 = leaves+1; TOTAL descents in the program 18 =
#                                   2*8+2, fully accounted, which is the clause that
#                                   closes "something else descends". The leaf count
#                                   is INDEPENDENT of the batch plane: a CoW tree has
#                                   no sibling pointer, so the row cursor's own
#                                   boundary crossings (`bt_cur_next -> bt_seek_at`,
#                                   8) are the container's leaf count, and the two
#                                   must agree. No counter was welded into the
#                                   stdlib: an instrument inside `advance()` would be
#                                   a production edit on a hot path measuring itself.
#                                   CONTROL: a second `cp.seek(self.at)` inside the
#                                   family's `advance()` — which changes NO answer,
#                                   so the fixture and the whole ctr corpus stay
#                                   green — reds this gate at 16 descents for 8
#                                   leaves. Restored md5-identical, rebuilt, re-run
#                                   green.
# ⚠ AND THE SWEEP THAT REGISTERING THEM FORCED FOUND A GATE ALREADY RED.
# `logos_09_ctr_access_path` is `tier_full`, so no level below L4 runs it, and the
# S1 emitter collapse had orphaned it three steps earlier: the ordered-map family's
# per-row producers were DELETED, so `__ctr_from_`/`__ctr_rows_` — the names three
# of its clauses are written around — name nothing, and the family declares
# `__ctr_bfrom_`/`__ctr_brows_` instead. The narrowing it guards is intact (the
# trace still reads `m -> __ctr_bfrom_… [a range] on key (an operation EXACT …)`,
# one call, filter retired); the PULL UNIT is what moved. RE-PINNED, not weakened:
# the three ordered-map clauses now name today's producers, and the absence clause
# in particular had gone VACUOUS the moment `__ctr_rows_` stopped existing — a
# permissive defect that can never fire again. The POSITIONAL family still emits
# `__ctr_from_`/`__ctr_rows_` and its clauses are untouched, so the two families
# are now told apart by name where one spelling used to cover both. ⚠ THAT LAST
# SENTENCE EXPIRED AT S1b, THE SAME WEEK: the positional arm got its own
# leaf-batch producers and its per-row ones were deleted too, so the vector
# clauses moved to `__ctr_bfrom_`/`__ctr_brows_` as well — WITH the cut this time,
# in one step, which is what naming the gate in a deletion's ledger is for. All 42
# `logos_09_*` artifact gates re-run after the fix: 42/42.
# Both gates are `tier_full`, following `logos_09_ctr_access_path`: they compile a
# fixture (and one of them links and runs it under valgrind), which is per-commit
# weight the lint tier does not carry. tier_commit therefore does not move.
# PREDICTED 7144/3461/32 before the reconfigure and measured identical after.
#
# ADR 0025 S1b, THE VECTOR ARM'S LEAF-BATCH PRODUCER (+7 files, -1 file, +0
# tier_commit). PREDICTED 7150/3467/32 (+6/+6/0 off 7144/3461/32), stated BEFORE
# the reconfigure and then measured three ways. The count is NET and every file
# in it is named, because a count nobody can decompose is a number and not a
# prediction:
#   pass/ctr_vec_leaf_batches            the §5 oracle on the POSITIONAL family:
#                                        rows/order/sums against the container's
#                                        own per-row cursor, leaves == descents
#                                        (12, pinned exactly), the three narrowed
#                                        landings with an honest size(), the
#                                        traversal axis, the EMPTY container and
#                                        a leaf-boundary-exact landing
#   pass/ctr_vec_leaf_family_spelling    the CtrLeafFamily projection on a `kind
#                                        vector` family — the admitting twin the
#                                        vector arm did not have before
#   pass/ctr_vec_batch_then_mut          admit twin, SPLIT pull door
#   pass/ctr_vec_next_batch_then_mut     admit twin, `next_batch()` Option door
#   fail/ctr_vec_mut_while_batch         refuse half, SPLIT pull door
#   fail/ctr_vec_mut_while_next_batch    refuse half, Option door
#   fail/ctr_leaf_family_volume_refused  the abuse direction, RE-AIMED (below)
#   -fail/ctr_leaf_family_vector_refused REMOVED — and this is the only removal
#                                        in this ledger that is not a weakening,
#                                        so it is priced here rather than
#                                        narrated: the fixture's substantive
#                                        claim was "a vector family cannot be
#                                        leaf-walked", S1b IMPLEMENTED the vector
#                                        arm's producer, and an implemented
#                                        subject is the one thing that retires a
#                                        refusal. The abuse DIRECTION did not
#                                        retire with it — it moved, verbatim, to
#                                        the VOLUME (str-valued ordered_map) arm,
#                                        which still emits no leaf batches, and
#                                        the moved file carries the history in
#                                        its header. Net registry effect of the
#                                        swap: zero.
# The four `ctr_vec_*` refuse/admit fixtures are PAIRS on purpose: the two arms
# of the emitter build their walks independently, so a `#[borrow_carrying]` or a
# `&#nm` witness lost from the vector arm reds nothing the ordered pairs pin.
# The two new branches the positional `advance`/`retreat` carry (the ORDINAL trim
# `rem < avail`, the RETREAT clamp `lstart < base`) were FIRE-COUNTED inside the
# block with a temporary counter field (2 fires and 1 fire respectively, with the
# zero cases named), the instrumentation removed and container_item.logos
# md5-verified back, and each arm then held by a CONTROL REVERT run one at a time
# with a green checkpoint between: no-trim reds pass/ctr_vec_leaf_batches at code
# 12, no-clamp at code 27, and the restored tree is green.
#
# ADR 0025 S1b, THE COLLAPSE + THE DESCENT ORACLE (+2 files, +0 tier_commit).
# PREDICTED 7152/3469/32 (+2/+2/0 off 7150/3467/32), stated BEFORE the
# reconfigure and measured three ways. Decomposed:
#   pass/ctr_vec_leaf_descent_count      the descent-gate SUBJECT for the vector
#                                        arm — one row walk, one batch scan, no
#                                        landings, so every counted call has one
#                                        possible origin
#   logos_09_ctr_vec_leaf_descent        GATE 3, tier_full. The SAME
#                                        `ctr_leaf_descent_gate.sh` over the
#                                        positional family, which descends
#                                        through `btvec_seek` rather than
#                                        `bt_seek_at`. The gate grew two REQUIRED
#                                        arguments for those two primitive names
#                                        rather than a default: an absent edge
#                                        already exits 2, but a default that
#                                        matched the WRONG primitive would exit 0
#                                        having counted some other code path's
#                                        calls — the exemption checked in the
#                                        abuse direction.
#                                        MEASURED: 3000 rows scanned in 12
#                                        descents over 12 leaves, batch pulls 13
#                                        = leaves+1, TOTAL descents 26 = 2*12+2,
#                                        fully accounted; the row oracle's own
#                                        boundary crossings independently say 12.
#                                        CONTROL: a second `cp.seek(self.at)` in
#                                        the vector family's `advance()` — which
#                                        changes NO answer, so the fixture and the
#                                        ctr corpus stay green — reds this gate at
#                                        24 descents for 12 leaves. Restored
#                                        md5-identical, rebuilt, re-run green.
# tier_commit does not move: like `logos_09_ctr_leaf_descent`, this gate compiles,
# links and runs a fixture under valgrind, which is per-commit weight the lint
# tier does not carry.
# ADR 0025 S2, THE NODE LAYER (+1 registered test, +0 fixtures, +0 tier_commit).
# PREDICTED 7153/3470/32 (+1/+1/0 off 7152/3469/32), stated BEFORE the
# reconfigure. Decomposed:
#   logos_09_plan_nodes                  GATE, tier_full. `plan_nodes_gate.sh`
#                                        over FOUR fixtures that already exist —
#                                        no new fixture is registered, and that
#                                        is a measurement rather than a saving:
#                                        the node layer emits no code, so the
#                                        only thing a new fixture could assert is
#                                        the trace, which is what this gate reads
#                                        off the fixtures whose ARTIFACTS supply
#                                        the independent side of the comparison
#                                        (deem_join_step_reread's `__hm1`+`__hm2`
#                                        over one drained `__rel_d`,
#                                        deem_batch_scan_drain's three `__it_m`
#                                        bindings, deem_hashmap_source's zero,
#                                        deem_cross_domain_join's one index over
#                                        a container that is never drained).
# tier_commit does not move: the gate compiles four fixtures with `--gen-dir`,
# which is per-commit weight the lint tier does not carry.
#
# ADR 0025 S2, THE REFUSAL CENSUS RE-DERIVED (+1 registered test, +0 fixtures,
# +0 tier_commit). PREDICTED 7154/3471/32 (+1/+1/0 off 7153/3470/32), stated
# BEFORE the reconfigure. Decomposed:
#   logos_09_plan_ground_census          GATE, tier_full. `plan_ground_census_gate.sh`
#                                        over the WHOLE wql_/deem_ pass corpus
#                                        (175 fixtures, already registered — no
#                                        new fixture, and again a measurement
#                                        rather than a saving: the census's
#                                        subject is the corpus AS IT IS, and a
#                                        fixture written to witness a ground
#                                        would be the gate grading its own
#                                        homework. The 15 grounds the corpus does
#                                        NOT reach are recorded as debt instead).
# tier_commit does not move: the gate compiles 175 fixtures with `--gen-dir`
# (~45 s at -P nproc), which is per-commit weight the lint tier does not carry.
#
# ADR 0025 S2h, THE MATERIALIZATION DEBT LEDGER CLOSED (+1 registered test,
# +1 FIXTURE, +0 tier_commit). PREDICTED 7155/3472/32 (+1/+1/0 off 7154/3471/32)
# from the single added `pass/*.expected`; the gate's red reported exactly
# ALL 7155 / -LE imported 3472 / tier_commit 32 before this block was touched.
# Decomposed:
#   logos_02_semantic_core_pass_deem_mat_ground_witness
#                                        FIXTURE, suite_semantic_core. Three
#                                        `deem` queries over ONE iterator source,
#                                        witnessing `MG_REL_BLOCK`,
#                                        `MG_UNDECIDED` and `MG_UNPROVEN`.
#
# ⚠ THIS REVERSES THE S2 BLOCK ABOVE, WHICH SAID A WITNESS FIXTURE WOULD BE THE
# GATE GRADING ITS OWN HOMEWORK. The reversal is deliberate and the earlier
# sentence was right about the RISK and wrong about the ONLY WAY TO TAKE IT. S2
# recorded the unreached grounds as debt because the census's subject is the
# corpus as it is; S2h had to discharge that debt, because ADR 0025 forbids
# retiring the single-pass proof while any ground it carries is unwitnessed —
# a proof cannot be retired on the strength of sentences nobody has compiled.
# (S2j retired it by INVERSION: `plan_insert_drains` carries all eight grounds
# onto nodes. The debt had to be paid first for exactly that reason.)
# What makes this fixture a measurement and not a self-graded green: each query
# carries a RUNTIME oracle over the emitted artifact (row sequence + the PULL
# COUNT of an iterator that has no length and cannot be read twice), and the
# writing of it FOUND TWO THINGS the census could not have — a node whose ground
# was set and whose justification was the empty string (now `access_plan`'s
# `swhy` fallback plus the census's FACT F, probe-paired: reverting the fallback
# reds FACT F on exactly this fixture), and a drain of a source the query never
# names. And it did NOT discharge the fourth ground: `MG_GPATH` turned out to be
# unreachable and was deleted instead, which is the outcome a fixture-writing
# stage has to be able to reach if it is measuring rather than decorating.
# tier_commit does not move: a `pass` fixture registers into suite_semantic_core.
#
# ADR 0025 S3a/S3b, DRAIN -> BUFFER (+1 registered test, +0 fixture,
# +0 tier_commit). PREDICTED 7156/3473/32 (+1/+1/0 off 7155/3472/32) from the
# single added `add_test`; the gate's red then reported exactly
# ALL 7156 / -LE imported 3473 / tier_commit 32 before this block was touched —
# the prediction was written down first, which is the only thing that makes the
# agreement evidence.
# Decomposed:
#   logos_09_drain_import_pair           GATE, suite_semantic_core + tier_full.
#                                        Pins the TWO-LINE import pair that makes
#                                        the Drain node's `Buffer<R>` landing
#                                        resolve at all.
#
# WHY A WHOLE GATE FOR TWO `use` LINES, AND WHY IT IS NOT A LINT OVER PROSE. The
# two lines look like one duplicated line and they are not: a spliced item's own
# `use P;` makes P's names VISIBLE but does not LOAD P, so the trigger module
# must import it too. Removing either half ALONE, one at a time, full stdlib
# rebuild each, restored byte-identical with a green 6/6 checkpoint between and
# after:
#   quote literal alone   1 pass / 5 FAIL   trigger import alone   0 pass / 6 FAIL
# The refusal is ASYMMETRIC and that asymmetry is the hazard the pin exists for:
# the one fixture that survives with the trigger import deleted
# (`deem_batch_scan_drain`) survives because it loads the package transitively
# through Memoria, so anyone who probes this question with the obvious batch
# fixture measures GREEN and deletes "the redundant one". The gate reads its
# artifact clause on `deem_join_step_streams` instead, precisely so a pass is
# evidence about the import pair and not about Memoria's dependency graph.
#
# ⚠ THE GATE RETIRES WITH ITS SUBJECT. Its FACT 2 requires the witness dump to
# carry a `Buffer<` landing, so if the Drain arm ever stops spelling `Buffer` the
# gate goes RED rather than standing guard over a fact nobody needs — and the
# instruction in the failure text is to retire it, not to weaken the assert.
# Both FACT 2 clauses were proved to bite against states that ACTUALLY EXISTED
# rather than by hypothesis: on the pre-S3a corpus snapshot the witness dump has
# 0 spliced `use logos.mem.stream;` lines, and on the pre-S3b snapshot it has 0
# `Buffer<`.
# tier_commit does not move: the gate compiles 6 fixtures with `--gen-dir` and
# registers into tier_full beside the other three ADR 0025 codegen gates.
# ADR 0025 S3f, THE READ-ONCE PAIR AND THE EMPTY LANDING (+2 registered tests,
# +1 FIXTURE, +0 tier_commit). PREDICTED 7158/3475/32 (+2/+2/0 off 7156/3473/32),
# written down BEFORE the reconfigure; measured after it: ALL 7158 / -LE imported
# 3475 / tier_commit 32. Round total off the pin this round STARTED from:
# +3/+3/0 against 7155/3472/32.
# Decomposed:
#   logos_02_semantic_core_pass_deem_drain_buffer_empty
#                                        FIXTURE, suite_semantic_core. The drain
#                                        landing BUILT AND NEVER PUSHED TO — the
#                                        degenerate case §1 has a rule for and
#                                        the corpus was silent about, because
#                                        every other drain fixture lands rows.
#   logos_09_drain_read_once_pair        GATE, suite_semantic_core + tier_full.
#                                        The read-once decision pinned in BOTH
#                                        directions off ONE fixture.
#
# WHY THE PAIR IS THE UNIT. A refusal pinned alone is green on the PESSIMAL
# compiler (drain everything, correct answers, every refusal test still green);
# an admission pinned alone is green on the compiler that never drains — which
# is the miscompile the withdrawal exists to prevent and it is SILENT (the
# second index build reads an iterator the first one spent, the answer goes
# empty). So both halves are read off `deem_join_step_reread`, whose two queries
# differ in exactly one thing — how many times the query names the source:
#   ADMIT  `q_dup`      one naming  -> `let mut __rel_d: DupIter = dup_rows(d);`
#                                      consumed in place; no landing.
#   REFUSE `q_selfstep` two namings -> `__it_d` + `Buffer<(i64,i64)>` landing,
#                                      filled by `push`, read by `as_slice`.
# and each half must carry the plan ground that explains it (`-> stream` /
# `-> no materialization` vs `-> drain on drained: second use`), so an artifact
# and a trace that disagree cannot both be green.
#
# WHY THE BEHAVIOURAL FIXTURES COULD NOT DO THIS. `deem_join_step_reread` and
# the new `deem_drain_buffer_empty` both count the SOURCE's own `next()` calls —
# a strong oracle for "read once" and blind to WHERE the rows went. An emitter
# that kept a private `Vec`, or that drained the admitted query too, gives
# identical pull counts and identical answers. The landing is an artifact fact
# and is read off the artifact.
#
# AND WHY THE EMPTY FIXTURE NEEDED THE GATE. Its empty run asserts 0 rows —
# which a drain-free emitter also produces. What makes it an assertion is the
# source's call log: `TICKS == 1` (consulted once, answered `None`); 0 would mean
# the query never touched it, 2 would mean the landing was built once per naming
# — the spent-iterator defect, also 0 rows on the answer.
#
# ⚠ AND THE LIMIT OF THAT COUNTER WAS MEASURED, NOT ASSUMED. A first writing of
# the fixture claimed `TICKS == 0` would catch an emitter that elided the drain.
# It would not: compiled and run, the SAME fixture with the source named ONCE —
# the admitted shape, `-> stream` / `-> no materialization`, no landing at all —
# reports the same `TICKS` 1 and 4. Every runtime assertion in the file is green
# under an emitter that stopped draining. That is exactly why the gate half
# exists and why the fixture is registered as one half of a pair; neither is
# sufficient alone.
#
# PROBE PAIR ON THE NEW GATE, one perturbation at a time, fixture restored to a
# byte-identical source (md5 b34aa8a0f611dd2236ce62f501a71bad) with a green
# checkpoint between and after: `q_dup` given a second naming (admit -> refuse)
# reds 7 clauses, `q_selfstep` reduced to one naming (refuse -> admit) reds 4.
# ⚠ RECORDED, NOT HIDDEN: one clause (`.push(`) did NOT fire in the second probe
# — a streamed join pushes into its hash bucket too — so it is kept for the
# "landing built and never filled" case but is not one of the clauses that
# separate refuse from admit.
#
# ⚠ CENSUS-GATE NUMBERS MOVED WITH THE FIXTURE, AND ONLY WITH IT.
# `logos_09_plan_ground_census`: fixtures 176 -> 177, drain+sort 10 -> 11,
# `__it_` 10 -> 11, arrange 596 -> 598, index 596 -> 598 (the S2d EQUALITY
# carried the addition unremarked), hash join 493 -> 495. `__ks`, key vector,
# `__ix<k>` (311), already-a-buffer (204) and read-once (18) did NOT move — this
# fixture has no `order by`, no aggregate, no `rel` block, and its read-once
# proof is WITHDRAWN rather than granted. Corpus snapshot before/after S3f:
# `Only in after: deem_drain_buffer_empty.gen`, all 160 pre-existing dumps
# byte-identical. THE SNAPSHOT BASELINE FOR THE NEXT STAGE IS THEREFORE
# 161 dumps / 6,856,383 bytes (corrected from 6,856,379 by the S2i audit — measured twice, byte-identical runs; the chain closes: 6,801,240 + 46,018 + 9,125 = 6,856,383) (was 160 / 6,847,258 after S3b, and 160 /
# 6,801,240 before S3a — that growth closes exactly: 2089 spliced
# `use logos.mem.stream;` lines x 22 bytes + S3b's 60 == 46,018).
# `logos_09_drain_import_pair`'s compile clause went 6/6 -> 7/7 with it: its
# fixture list is EVERY dump carrying a `Buffer<` landing, re-derived by grep
# rather than kept by hand.
#
# ── THE WHOLE ROUND, STAGE BY STAGE, INCLUDING WHAT DID NOT HAPPEN ──────────
#
#   S3a  LANDED. The import pair (`use logos.mem.stream;` in wql.logos AND
#        literally in both `quote_item!`s of `rexpr_walk::emit_fn_quote_blob`).
#        Abuse-direction probe pair, each half removed ALONE, full stdlib
#        rebuild each, byte-restored with a green 6/6 checkpoint between and
#        after: quote literal alone 1/5, trigger import alone 0/6, both 6/0.
#        Pinned by `logos_09_drain_import_pair`.
#   S3b  LANDED. Drain -> Buffer, ACCUMULATOR spelling (two string literals).
#        Measured against the WRAP spelling on the same tree: WRAP +830 corpus
#        bytes / +29 instrs / frame 0x148->0x198; THIS +60 / +20 / 0x148->0x188.
#        Byte-pin, plan_nodes and ground_census did NOT move, which is what says
#        the slice arm was untouched.
#   S3e  LANDED. Census FACT G — the 311 `__ix<k>` permutation vectors counted,
#        plus the per-fixture direction `perm >= ks`. NOT an equality against a
#        Sort node, and the number that refuses the equality is 85.
#   S3f  LANDED (this block). The read-once pair + the empty landing.
#
#   NOT LANDED, EACH REFUSED ON A MEASUREMENT RATHER THAN DEFERRED:
#   * `__ix` -> Sort node emission. 85 of the 311 bindings, in 39 of the 89
#     fixtures that emit them, are the aggregate emitter's group-row enumeration
#     order with NO `__ks` anywhere — a Sort node owning all 311 would assert a
#     materialization for an ordering nobody requested, 85 times. The other 226
#     are ALREADY node-owned under FACT E through `join_sel::sort_key_vector`;
#     what the task described would have been a RENAME of its verdict word to
#     `sort`, and FACT B (`drain + sort == __it_` per fixture, over the PRELUDE
#     plane) reds 89 fixtures on exactly that rename. Recorded in the census
#     gate's FACT G header.
#   * indices-vs-rows / deleting the `streams` param from the four frags: the
#     same measurement blocks it — the buckets that would hold ROWS are the ones
#     the 85 non-sort bindings feed.
#   * `plan_mark_single_pass` dies: DONE at S2j, and not by deletion. The S2h
#     refusal stands as measured — dropping the proof outright makes every query
#     pessimal (11 artifacts, +2,953 bytes then, +3,061 on today's corpus) and no
#     runtime fixture can see it. So the grounds MOVED instead: the walk now
#     inserts the `Drain`/`Sort` node directly (`plan_insert_drains`) and the
#     `once`/`owhy` side channel is gone. Corpus text-preserving, `diff -rq`
#     empty at 161 dumps / 6,856,383 bytes. What is still owed is the OTHER half:
#     the second read refused by `Rewind` rather than by a planner walk.
#   * S3d, the slice arm dies: NOT STARTED. `logos_09_slice_scan_codegen`'s
#     golden therefore still shows the pre-batch slice loop ON PURPOSE, and it
#     transitioned this round through the BYTE-COMPARABLE arm (empty diff), not
#     the measured-equal one — see that gate's header for the arithmetic that
#     closes it (2089 x 22 + 60 == 46,018 == the whole corpus growth, leaving no
#     unexplained byte).
#
# ADR 0025 S2j, THE INVERSION AND THE TYPING DOOR'S FIRST HALF (+2 registered
# tests, +2 FIXTURES, +0 tier_commit). PREDICTED 7160/3477/32 (+2/+2/0 off
# 7158/3475/32), written down BEFORE the reconfigure; measured after it: ALL 7160
# / -LE imported 3477 / tier_commit 32. The two are a PAIR and are priced as one
# stage: neither half is an oracle alone.
# Decomposed:
#   logos_02_semantic_core_pass_stream_rewind_door_admitted
#                                        FIXTURE, suite_semantic_core. `Buffer`
#                                        through `read_again<S: Rewind>`, and the
#                                        second pass asserted to yield the SAME
#                                        three rows in order — a compile-only
#                                        admission would pass a `Rewind` impl that
#                                        re-lands nothing.
#   logos_06_diagnostics_fail_stream_rewind_door_refused
#                                        FIXTURE, suite_diagnostics. The SAME
#                                        signature, a `BatchStream` with no
#                                        `Rewind` impl (the shape a walk has), and
#                                        the diagnostic pinned verbatim.
# tier_commit does not move: both register through the `.expected` globs beside
# the rest of the corpus. ⚠ AND THE PAIR IS SPLIT ACROSS TWO SUITES by the
# harness's own rule (pass -> semantic_core, fail -> diagnostics), so nothing in
# the registry says these two belong together — which is why each fixture's
# header names the other by path, and why they are priced here as one stage.
#
# ── THE WHOLE ROUND, STAGE BY STAGE, INCLUDING WHAT DID NOT HAPPEN ──────────
#
#   I1  LANDED. THE INVERSION. `plan_mark_single_pass` -> `plan_insert_drains`
#       (`stdlib/mem/wql/plan_walker.logos`): the walk that used to RECORD a
#       per-rel read-once proof now INSERTS the `Drain`/`Sort` node directly,
#       carrying the same eight grounds through the same `js_reads_once`
#       cascade. `once`/`owhy` leave `AccessPlan` entirely
#       (`stdlib/mem/wql/access_plan.logos`), `access_plan_decide_mode` recovers
#       "reads it once" as the ABSENCE of a drain node, and
#       `access_plan_plan_nodes` — the third pass that existed only to turn the
#       boolean into the node — is gone with its two call sites.
#       ⚠ THE ORACLE IS THE CORPUS, NOT THE SUITE. Text-preserving: `diff -rq`
#       EMPTY over 161 dumps / 6,856,383 bytes, the S3f baseline unchanged in
#       both directions. That is what makes this an inversion of MECHANISM and
#       not a change of plan — and it is also why L2 alone could not have told
#       the two apart.
#       ⚠ AND IT IS THE CHANGE S2h REFUSED, TAKEN THE OTHER WAY. S2h measured
#       the BARE deletion (the proof dropped with nothing in its place): 11
#       artifacts move, +2,953 bytes then, +3,061 re-measured on today's corpus,
#       every query pessimal, and NO runtime fixture can see it because a
#       conservative plan returns the same rows. The refusal stands; what the
#       grounds needed was somewhere to go.
#   I2  LANDED, ONE HALF OF TWO. THE TYPING DOOR, as a registered pair:
#       `tests/logos/fail/stream_rewind_door_refused.logos` (a non-`Rewind`
#       stream refused at `read_again<S: Rewind>`) and
#       `tests/logos/pass/stream_rewind_door_admitted.logos` (`Buffer<i64>`
#       admitted through the same signature, and behaviourally re-read).
#       WHAT IS PINNED is that the type-level door EXISTS and SEPARATES. What is
#       NOT: the wql emitter does not route through it yet — a second read is
#       still refused by a PLANNER — and both fixtures' headers,
#       `stdlib/lang/stream/stream.logos`'s `Rewind` doc and
#       `plan_insert_drains`'s own header say so in the same words, so the open
#       half is stated in three places and claimed in none.
#       PROBE PAIR, one perturbation at a time, on copies outside the tree so the
#       registered files were never edited: the refuse half given
#       `impl Rewind for WalkIter` and nothing else compiles (rc 0, diagnostic
#       gone, that test red — the bound is what refuses); the admit half's own
#       `read_again` called with a second non-`Rewind` stream is refused (rc 1,
#       same message — the admission is a check, not a signature that admits
#       everything). Both directions therefore have a witness that the OTHER
#       direction's defect would red.
#
#   NOT LANDED, REFUSED ON A MEASUREMENT RATHER THAN DEFERRED:
#   * S3d, THE SLICE ARM DIES: BUILT, MEASURED, REVERTED — no longer "not
#     started", and the byte-pin's measured-equal arm was REFUSED BY THE
#     MEASUREMENT rather than left unexamined. In order:
#       D0  BASELINE pinned before anything was touched (161 / 6,856,383).
#       D1  FAIL-FAST PROBE, hand-written, no stdlib rebuild: is a one-packet
#           `SliceStream<R>` over a borrowed slice affordable at all?
#       D2  SHAPE SURVEY, over the pinned corpus rather than over the emitter's
#           source: 98 of the 161 dumps carry 379 indexed slice loops, against 5
#           dumps carrying a `next_batch()` loop. The arm is the corpus's
#           majority shape, which is what makes its codegen a price and not a
#           detail.
#       D3  VOCABULARY UNIT minted (`SliceStream<R>`, one packet then `None`).
#       D4  EMITTER COLLAPSE: both sites (the `emit_simple` else-arm and
#           `chain_nest_frag`'s base loop) through one fragment.
#       D5  THE BYTE-PIN, MEASURED-EQUAL ARM — AND THE ARM WAS NOT TAKEN.
#           `slice_scan_run`, same fixture, disassembled both ways:
#             BASE   59 instructions · 242 bytes .text · frame 0x78
#             NEW   128 instructions · 528 bytes .text · frame 0x108
#           +69 instructions, +286 bytes, frame doubled, for a source that is
#           ALREADY materialized. "OR measured equal" is a licence to re-golden
#           when the emitted code is equivalent; the measurement says it is not,
#           so the golden did not move and the round did not spend it.
#       D6  CORPUS ORACLE — the thing L2 could not see, run because D5 is one
#           function and the arm is 379 loops.
#       D7  CLASSIFY BEFORE TOUCHING, minimal repro, one variable at a time:
#           the emitted join-order branch puts the stream in an ARM scope and the
#           sort's key vector at FUNCTION scope, and the packet-borrowed key is
#           then refused — `'__bb0' does not live long enough … borrowed by
#           '__ks'` (E0597) — though the rows live in the caller's slice, which
#           outlives everything. ONE arm compiles (lt2, rc 0); TWO SIBLING ARMS
#           of the same shape are refused (lt4, rc 1); the same two arms with the
#           PRE-COLLAPSE indexed loop compile (lt5, rc 0). Re-measured on today's
#           compiler while writing this block, not quoted from the round's notes.
#       D8  STOP, with the reason: the blocker is the borrow-provenance
#           over-refusal class (the D1-residual task), not the batch design. A
#           collapse landed on top of it would have bought +286 bytes per slice
#           scan AND a scoping rule the emitter has to route around.
#       D9  RESTORE PROVED, not assumed: `SliceStream` is absent from `stdlib/`,
#           the golden is untouched, and the corpus is back at the D0 baseline.
#     The record of (1) and (2) lives in `tests/logos/slice_scan_codegen_gate.sh`'s
#     header, beside the clause it is about.
#
#   ══ 2026-08-14 — S5-D4: D7's BLOCKER IS CLOSED, AT THE CHECKER ═══════════
#   D8 stopped on "the borrow-provenance over-refusal class, not the batch
#   design". That diagnosis is now a fix, and the fix is in the CHECKER, which
#   is where D7 said the defect was:
#
#     THE RECORDED REPRO DOES NOT RUN AS-IS, AND THAT IS THE FIRST FINDING.
#     `sandbox/s3d_slice_arm_repro/{lt2,lt4}.logos` BOTH fail rc 1 with
#     `unknown generic type 'SliceStream'` — D9 reverted the vocabulary unit out
#     of stdlib, so anyone re-measuring from those files reads an rc-1/rc-1 pair
#     and concludes the wrong thing. Re-minted inside the probe file, D7's triple
#     reproduces EXACTLY (q2 rc 0 · q4 rc 1 · q5 rc 0).
#
#     ROOTED TO AN 8-LINE PROGRAM WITH NO STREAM IN IT. Not the packet, not
#     `unwrap`, not `#[borrow_carrying]`, not arm-siblinghood, not a name
#     collision — each refuted by its own single-variable probe (r1-r5, m1-mA in
#     `sandbox/s5_lt/`). The minimum: a `Vec<str>` at outer scope, and TWO push
#     sites reached through a REFERENCE-TYPED LOCAL declared in an inner scope.
#
#     THE ACTUAL MECHANISM, which is NOT what the shape survey suggested. Arm 1
#     records `b` — the reference LOCAL — as a §B6 borrow source of `ks`; `b`
#     dies at the block's `}`; `ks` is then marked dangling; and the FIRST
#     LATER USE of `ks` is refused. One arm passes only because a `return ks`
#     is a MOVE (`consume`) and not a live read; two arms fail because arm 2's
#     `push` IS one. So "one loan site vs two" was the symptom; "is `ks` read
#     again after the block" is the trigger.
#
#     THE FIX, and it is one arm in one switch: `collect_ref_sources_paths`'
#     `AddrOfTemp` case emitted the place's ROOT LOCAL. When the place walk went
#     THROUGH a reference (`b[i]`, `b.f`, `*b`, `b[a..c]` with `b: &T`) the
#     storage borrowed is the POINTEE, whose life is whatever `b` itself
#     borrows — so the arm now emits `ref_sources_under(root)`, which is exactly
#     what the neighbouring `VarRef` arm has always done for the plain copy
#     (`o = r` emits r's sources, never `r`). `BorrowPlace` carries a new
#     `through_ref` bit set by the walker; the LOAN channel's policy (root at
#     the reference variable, for reborrow exclusivity) is untouched, which is
#     why the bit exists instead of a second walker.
#     Fire-counted: `LOGOS_DUMP_BC_THRUREF=1` (`fired=` per compile; 0 on the
#     direct-from-parameter twin, ≥1 on every probe that moved).
#
#     THE PAIR, one variable — the referent's ORIGIN, nothing else
#     (`sandbox/s5d_pair/`):
#       pA  `let b: &[Row] = rows;` (a PARAMETER)  → rc 0   (was rc 1)
#       pB  `let v: [Row;1] = …; let b: &[Row;1] = &v;` (a dying LOCAL)
#                                                  → rc 1, E0597 naming **`v`**
#     The refusal SURVIVES and now names the thing that dies. It is not a
#     weakening in the other direction either: with `b` outliving `v`, the old
#     rule emitted `b`, saw nothing die, and ADMITTED the dangle.
#
#     BLAST: whole stdlib rebuilds; L2 2120/2120 + 36 gates; the tier_full sweep
#     81/81; and the corpus snapshot is BYTE-IDENTICAL (166 dumps / 7,649,819,
#     `diff -rq` empty) — a checker change may not move an artifact, and this
#     one does not.
#
#   ⚠ AND THE SLICE-ARM COLLAPSE IS STILL NOT LANDED — the door is open, the
#   MEASUREMENT still refuses it. D5's numbers (59 → 128 instructions, 242 → 528
#   bytes of `.text`, frame 0x78 → 0x108, for an ALREADY-materialized source) are
#   what the byte-pin's "OR measured equal" arm has to clear, and they are a
#   fact about codegen that a checker fix cannot change. So the blocker count
#   goes from TWO reasons to ONE, and the surviving one is a number rather than
#   a compiler bug. ⚠ A CONFLICT IS RECORDED RATHER THAN RESOLVED: a later note
#   quotes the collapse as "-2 instructions per ROW, favourable", which is
#   consistent with D5's +69 only if one is per-loop setup and the other
#   per-iteration. Both cannot be summarised as one verdict, and "a trade" is
#   not "measured equal" — landing it is a design call with a re-measurement
#   under it, not a licence this round holds.
#
#   ══ 2026-08-14 — S5-D5: THE AGGREGATE PULLS BATCHES (S4's other half) ═════
#   S4 proved the fold single-pass at the OPERATOR and could not claim it,
#   because `emit_aggregate`'s base loop was still `while __i0 < (src).len()`.
#   `plan_walker.logos` recorded the one-sided attempt and its failure verbatim
#   (`deem_batch_scan_drain`: `'…LeafWalk' has no method 'len'`). This is the
#   PAIR, both halves in one step:
#     EMITTER  `emit_aggregate`'s base nest routes through `batch_scan_frag` —
#              §1's ONE shape, the same function `emit_simple`'s streamed arm
#              and `chain_nest_frag`'s join base already call. Third and last
#              site. GATED on three facts the plan CARRIES: `pure_group` (the
#              representative class seeds `__g_row.push(__i0)`, and a global row
#              ORDINAL is unspellable in a batch pull — that class keeps the
#              indexed walk and says so), the producer's batch flag, and the
#              plan's own drain plane.
#     PLANNER  the pure class stops claiming a Drain it no longer performs.
#              ⚠ `ap_effective_batch` and not `prm.rel_batch[ri]`:
#              `plan_apply_access` — which installs the PUSHDOWN-CHOSEN op's
#              batch flag — runs AFTER `plan_insert_drains`, so reading the
#              field there is reading it before it is written, and the proof
#              would have been about a producer the emitter never calls.
#   MEASURED, not asserted. The corpus moved EXACTLY ONE FILE:
#     166 dumps, −23 bytes, `diff -rq` names `deem_batch_scan_drain.gen` alone.
#     `let mut __it_m: …LeafWalk` + `let mut __rel_m: Buffer<(u64,u64)>` collapse
#     to ONE binding — the walk IS the source — and the fold now runs inside the
#     `next_batch()` loop. One full pass over the data and one Buffer removed.
#   GATES MOVED WITH THE STAGE, each delta attributed to that one query:
#     drain+sort 13 → 12 (drain 8 → 7) · `__it_` 13 → 12 (measured IN the
#     fixture, 2 → 1, not inferred from the corpus difference) · read-once
#     18 → 19. Everything else HELD — arrange 598, index 598, hash-join 495,
#     `__ks` 127, `__ix<k>` 319, container 204, elided 8, group frame
#     152/208/13/7 — which is the control that says this is one decision and not
#     a shape change: the FOLD did not move, only what the rows arrive in.
#   `logos_09_plan_nodes` was RE-AUTHORED, not relaxed. Its clause asserted "the
#   aggregate's re-read was NAMED"; there is no re-read to name, and pinning a
#   ground the compiler correctly stopped emitting is pinning a defect. Three
#   assertions replace it: drain == 0 (so a drain RETURNING is red), read-once
#   == 2 (the absence is still a POSITIVE line), and — the independent channel —
#   the emitted `parity_sums_run` must contain `next_batch()` and no `Buffer<`.
#   A one-sided change would leave the plan clause green and that one red, which
#   is exactly the failure S4 measured.
#   ⚠ THE NEW ARM HAS NO CORPUS WITNESS, AND WAS MEASURED OUT OF TREE RATHER
#   THAN LEFT UNEXECUTED. A PURE aggregate over a ROW-at-a-time producer keeps
#   its drain on a new sentence; the corpus's only two rel-registered aggregate
#   sources are `deem_batch_scan_drain`'s batch family (now proved) and
#   `deem_pushdown_all_shapes`' representative-class query, so nothing in it
#   reaches that arm. `sandbox/s5d_aggprobe/pure_rowsrc.logos` does: a pure
#   aggregate over `MapSource::rel entry` states the ROWS-not-batches ground,
#   compiles, and runs its 6-pull / 2-group oracle green under `run_test.sh`.
#   It is NOT added as a fixture — that moves the pinned census — and the
#   trade-off is written here so the next reader finds a decision, not a gap.
#   * THE TYPING DOOR'S SECOND HALF (the emitter's second read refused BY THE
#     TYPE): open, and now open with a fixture pair standing under it, so its
#     landing will be a change of mechanism with both directions already pinned.
#
# ══ ADR 0025 S3 — ORDER AS A FACT, STAGE BY STAGE ══════════════════════════
#
# The census's method is "PREDICT the count, then measure", and a stage that is
# summarised only by its delta is a number with no subject. What follows is what
# each step of S3 DID, in order, and — the half a summary always drops — WHAT
# THE TREE DOES NOT CONTAIN, so the next stage does not go looking for it.
#
#   S3.0  RE-BASELINE the corpus oracle on this tree before touching it. Build
#         confirmed current first: a baseline taken against a stale build is a
#         measurement of the previous stage.
#   S3.1  THE ORDER FACTS TRAVEL FROM THE TYPE. `o`/`r`/`n` natspec flags from
#         `producer_impls_trait_fn` (OrderedBy / Bidirectional / RandomAccess)
#         emitted into the same `!` flag SET as `i`/`b`, so membership answers
#         and no existing reader changes (`src/compiler/sema_expr.cpp`,
#         `src/compiler/sema_collect.cpp`, `src/compiler/sema_impl.hpp`).
#         (b) FIRE-COUNT CONTROL: a permanent `LOGOS_TRACE_NATSPEC` instrument
#         on the only channel by which producer capabilities reach the planner.
#         It is consumed at metaprog time and appears in NO artifact, so
#         "built but never reached" and "reached and ignored" are otherwise the
#         same observation.
#         (c) THE DECLARATION HALF: `order <rel> = <col>;` — no grammar rule
#         needed, it is `rel_bind`'s shape with a data lead, like `size` —
#         collected with the column-name check, emitted as the `^<col>` natspec
#         section, parsed into `MacroParams.rel_ordered` / `rel_bidir` /
#         `rel_randacc` / `rel_ordcol`. BOTH generated family arms declare it
#         (`stdlib/lcm/canon/container_item.logos`). The type says THAT the rows
#         are ordered; only the declaration says by WHICH column, because
#         `OrderedBy<u64>` names a key TYPE and a row can have two `u64`s.
#   S3.2  THE REFUSALS, IN PAIRS, and SPLIT ON PURPOSE. `order` over a producer
#         whose return type does not implement `OrderedBy` is refused at SPEC
#         time (`tests/logos/fail/deem_order_not_ordered.logos`); `order` naming
#         a non-column is refused at COLLECT time
#         (`tests/logos/fail/deem_order_unknown_column.logos`). The split is not
#         cosmetic: collect runs over a partly populated impl graph, and asking
#         the type question there refuses working code. Checked in the ABUSE
#         direction — admitting the first case makes the planner delete a Sort
#         node over unsorted rows, i.e. a WRONG ANSWER, not a slow one.
#   S3.3  THE ELISION. `ap_order_is_noop` (4 required clauses) in
#         `plan_prove_once`'s Simple arm; the decision is recorded ONCE on
#         `AccessPlan.ord_noop` (`stdlib/mem/wql/access_plan.logos`), carried to
#         `MacroParams.rel_ord_noop` by `plan_apply_access` exactly as `rel_node`
#         is (`stdlib/mem/wql/params.logos`), and read by `emit_simple`, which
#         CLEARS `mods.has_sort`. New ground `MG_ORDERED_SOURCE` — the second
#         ground for the ABSENCE of a node, extending `MG_CONTAINER`'s precedent.
#   S3.4  THE ARTIFACT. `emit_simple`'s admitted arm is a bare streamed
#         `next_batch()` loop: no `Buffer<`, no `__ix0`, no `__ks`, no
#         `__rel_m_sl`, no insertion sort. The refuse twin keeps all five. The
#         `limit` harvest falls out with NO new mechanism — it is the streamed
#         arm's existing `__out.len() < (3i64)` break, twice (outer leaf pull +
#         inner row loop), with no landing in front of it.
#   S3.5  THE GATE REGISTERED: `logos_09_order_elision_pair`
#         (`tests/logos/order_elision_pair_gate.sh`), `tier_commit` because the
#         wrong direction of this decision is a wrong ANSWER. 15 clauses, every
#         count PREDICTED. One prediction was wrong on first run (`__rel_m_sl`
#         3 against 4 — the binding counts beside its three uses) and is
#         corrected IN the gate WITH the reason, not silently.
#   S3.6  CENSUS MOVED WITH THE STAGE, delta predicted before measuring.
#   S3.7  GATE-LINT RED, CLASSIFIED NOT SUPPRESSED: [R1-exit-undecidable] on
#         `exit` inside the gate's awk program. Fixed AT THE CLASS by removing
#         awk's `exit` (a `done` flag), so exactly one meaning of the word
#         survives in the file, rather than annotating past the rule.
#   S3.8  FULL GATES + the leaf-count oracles + the corpus.
#
# ── S3-desc, THE TRAVERSAL HARVEST (Victor's §3 axis) ──────────────────────
#
#   T0    RE-BASELINE on this tree, build confirmed current.
#   T1    THE CONTAINER PRIMITIVE: `land_end()` on BOTH generated family walks
#         (ordered map + positional), DELEGATING to
#         `RandomAccess::seek_nth(size())` instead of assigning `at`/`clo`/`chi`
#         — so the clamp, the ordinal origin and the `endr` convention exist
#         once, and "the backward scan lands in ONE seek_nth, never a skip loop"
#         is true by construction rather than by assertion.
#   T2    THE PLAN CARRIES THE DIRECTION: `AccessPlan.ord_rev` +
#         `MacroParams.rel_ord_rev` + `rel_src_ord_rev`, copied by
#         `plan_apply_access` exactly as `ord_noop` is.
#   T3    THE REFUSAL CLAUSES, in the abuse direction: `desc` requires
#         `rel_bidir` AND `rel_randacc` AND `rel_batch`, each by name.
#   T4    THE EMITTER: `batch_scan_rev_frag` (land_end, `prev_batch` outer,
#         decrementing inner loop), selected from the plan's `rel_ord_rev`.
#   T5    NEW GROUND `MG_ORDERED_SOURCE_REV`, kept DISTINCT from
#         `MG_ORDERED_SOURCE` (the 4th absence ground), with its own access line
#         and explain sentence.
#   T6    THE FIXTURES, split at a real seam: the family admit/refuse set
#         (`tests/logos/pass/deem_order_desc_elision.logos`) and a forward-only
#         ordered source for the traversal refusal
#         (`tests/logos/pass/deem_order_desc_forward_only.logos`).
#   T7    THE GATE: `logos_09_order_desc_pair`
#         (`tests/logos/order_desc_pair_gate.sh`), `tier_commit`, 34 clauses,
#         every count PREDICTED from measurement.
#   T8    CONTROL AT THE DECISION SITE: the three `desc` clauses deleted from
#         `ap_order_is_noop`, full stdlib rebuild, wrong answer measured,
#         RESTORED and re-measured green.
#   T9    THE DESCENT ORACLES EXTENDED — stricter, never weakened: 3 new clauses
#         on both container families and clause 6 tightened `2*LEAVES+2` ->
#         `3*LEAVES+3` (`tests/logos/ctr_leaf_descent_gate.sh` over
#         `tests/logos/pass/ctr_leaf_descent_count.logos` and
#         `tests/logos/pass/ctr_vec_leaf_descent_count.logos`).
#   T10   BLAST RADIUS: `tests/logos/pass/deem_batch_scan_drain.logos`'s two
#         `desc` queries would have silently lost the fixture's sorted-drain leg.
#   T11   `logos_09_plan_nodes` RED, classified and re-pinned STRICTER
#         (`tests/logos/plan_nodes_gate.sh`).
#   T12   `logos_09_plan_ground_census`: INHERITED RED measured BEFORE anything
#         was re-pinned (`tests/logos/plan_ground_census_gate.sh`).
#   T13   THE CENSUS FIXED AT THE CLASS: the "no materialization on <ground>"
#         line is PARSED now, not pattern-matched against one hardcoded ground.
#   T14   TWO ERRORS CANCELLING, caught and recorded.
#   T15   CENSUS MOVED WITH THE STAGE; delta PREDICTED before measuring.
#   T16   FULL GATES + the leaf-count oracles + the corpus.
#
# ── THE DESCENT ORACLES' OWN CONTROL PAIRS, EXECUTED ───────────────────────
#
# T9 extended two gates; an extension nobody perturbed is a clause that has
# never been shown to be able to fail. Both directions were RUN, on copies
# outside the tree for the fixture half and on a full stdlib rebuild for the
# primitive half, and the tree was restored and re-measured green between them.
#
#   (1) THE PERTURBATION THAT DOUBLES THE DESCENT. A second complete backward
#       traversal added to each fixture (its own accumulators, asserted equal to
#       the first, so the PROGRAM still exits 0 and the gate is judging counts
#       and not a crash). MEASURED, both red at clause 7, exit 1: ordered
#       `rev_descents` 16 against LEAVES=8 (`rev_pulls` 18, landing 3, total 36
#       against 27); positional `rev_descents` 24 against LEAVES=12
#       (`rev_pulls` 26, landing 3, total 52 against 39).
#   (2) THE WRONG-PRIMITIVE ABUSE DIRECTION. Both families' `land_end` rewritten
#       to find the end by DESCENDING (`c.seek(endr-1)` before assigning the
#       fields) instead of delegating to `seek_nth` — the same landing, one
#       descent nobody asked for — and the whole stdlib rebuilt. MEASURED, both
#       red, exit 1: total descents 28 against 27 (ordered) and 40 against 39
#       (positional).
#       ⚠ AND IT RED AT CLAUSE 6, NOT CLAUSE 9, WHICH THE GATE'S OWN PROSE HAD
#       CLAIMED. The landing count stayed 2 in both runs: clause 9's edge is
#       CALLER-QUALIFIED (walk constructor -> seek), and a descent paid inside
#       `land_end` is a different caller, so it lands in the unqualified total
#       and nowhere else. The clause was NOT weakened — it is the one that
#       catches a third constructed walk — and the two messages were corrected
#       to stop making each other's claim. Restored, rebuilt, both green.
#
# ── WHAT S3 DOES NOT CONTAIN (recorded, never invented) ────────────────────
#
#   * `offset` DOES NOT EXIST. No keyword in `stdlib/mem/wql/grammars/wql.peg`,
#     no field in `stdlib/mem/wql/plan.logos`, no node in
#     `stdlib/mem/wql/lower.logos`, no fixture. §3's "offset/limit push down as
#     ONE seek_nth instead of a skip loop" therefore HAS NO SURFACE TO LAND ON
#     for its offset half: there is no skip loop to replace, because there is no
#     way to ask for one. Recorded in `stdlib/lang/stream/stream.logos`.
#   * SUPERSEDED, CORRECTED: the prior stage recorded that "`seek_nth` therefore
#     has no wql consumer and cannot acquire one". It DID acquire one — through
#     the DIRECTION axis rather than the ORDINAL one: `land_end()` is
#     `seek_nth(size())` and every emitted `desc` scan opens with it, pinned at
#     ZERO descents by the leaf-descent gate's clause 9. What is still missing is
#     only a query surface that asks for a position BY ORDINAL.
#   * NO SOURCE IN THE TREE IS `OrderedBy` + `Bidirectional` BUT NOT
#     `RandomAccess`. The three `desc` clauses cannot be exercised SEPARATELY by
#     any fixture: the ordered producers are (a) generated family walks, which
#     have all three, and (b) hand-written forward-only iterators, which have
#     none. `tests/logos/pass/deem_order_desc_forward_only.logos` fires all three
#     at once. Their separation is proved by CONTROL (T8: deleted together,
#     wrong answer measured, restored) and NOT by three fixtures.
#   * NO ROW-AT-A-TIME BACKWARD SCAN SHAPE EXISTS in the emitter — only the
#     leaf-batch one (`batch_scan_rev_frag`). That is why `rel_batch` is a
#     REQUIRED clause and not an optimisation: without it the planner would clear
#     `has_sort` for a non-batch producer and the FORWARD row loop would answer a
#     `desc` query. Closed at the registration/decision site, because a permissive
#     defect is invisible to a green corpus by construction.
#   * `BTreeMapIter` / `HashMapIter` ARE NOT ON THIS PLANE: they implement
#     `Iterator`, not `BatchStream`, and neither declares `OrderedBy`. The ADR's
#     "BTreeMap-iter sources would declare OrderedBy too" has no subject in the
#     tree today.
#   * COMPILER DEFECT, OUT OF SCOPE, minimal repro recorded: a module holding a
#     `static mut` + a container-family deem + a native-source deem fails at
#     `logosc-metaprog: jit add_module: Duplicate definition of symbol 'test$…'`
#     — two metaprog rounds re-add the module's static to the JIT. It FORCED the
#     T6 fixture split (which was the correct seam regardless). Not fixed here.
#
# ADR 0025 S3 moved these by +4 / +4 / +1, and the delta was PREDICTED before
# the gate was run: three fixtures (`pass/deem_order_elision`, and the two
# refusal halves `fail/deem_order_not_ordered` + `fail/deem_order_unknown_column`)
# and one registered gate (`logos_09_order_elision_pair`), of which only the
# gate carries `tier_commit`. The measured move was exactly that.
# ADR 0025 S3-desc moved them again by +3 / +3 / +1, PREDICTED before measuring:
# two fixtures (`pass/deem_order_desc_elision` — the family admit/refuse set —
# and `pass/deem_order_desc_forward_only`, which is a separate file because a
# module holding a `static mut`, a family deem and a native-source deem hits a
# metaprog/JIT duplicate-symbol defect) plus one registered gate
# (`logos_09_order_desc_pair`), of which only the gate carries `tier_commit`.
# The descent gates and `deem_batch_scan_drain` were EXTENDED and EDITED, not
# added, so they move nothing here — which is the arithmetic that made the
# prediction checkable.
#
# ⚠ AND THE PREDICTION FAILED ON FIRST MEASUREMENT, +1/+1/+1 AGAINST +3/+3/+1 —
# THE GATE EARNING ITS KEEP, EXACTLY AS ITS OWN MESSAGE SAYS. The cause is worth
# recording because it is the 12th gate-lie form (A TEST MISSING) arriving by a
# route with no diagnostic at all: a `pass/*.logos` fixture is registered ONLY
# if a sibling `.expected` file exists, and both new fixtures had none. They
# compiled, they ran green under the gate that reads their artifacts, and they
# were in NO suite — so `test-levels.sh` would have reported a clean tree while
# two of this stage's three subjects never executed. Nothing red would have been
# visible; only the count was. The fix was the two `.expected` files, after
# which the measurement matched the prediction exactly.
#
# ADR 0025 S4 moved them by +2 / +2 / +1: one fixture
# (`pass/wql_group_single_pass_fold_e2e`, WITH its `.expected` — the 12th
# gate-lie form is now a thing this stage checks first rather than discovers)
# and one registered lint (`logos_00_agg_frag_single_site`, `tier_commit`).
#
# ADR 0025 §8 (S4-naming — THE GROUP FRAME ENTERS THE PLAN VOCABULARY) moves
# them by +1 / +1 / +1, PREDICTED before the gate was run and measured exactly:
# ONE registered gate, `logos_09_group_frame_naming` (`tier_commit`), and NO new
# fixture — the stage's subject is `pass/wql_group_single_pass_fold_e2e`, which
# S4 already registered and which already witnesses all four of the new grounds
# (6 group frames, 12 accumulators, one `avg` count column, two representative
# rows). A stage that names a class does not need a new fixture to name it with;
# writing one would have added a second copy of the same six queries and moved
# three numbers here for nothing.
# 2026-08-14 — ADR 0025 S5 (the streaming return surface, §12 `buffered`).
# PREDICTED 7171 / 3488 / 36 (+1 / +1 / 0) BEFORE the reconfigure and measured
# IDENTICAL after. ONE new fixture with its `.expected`
# (`pass/deem_stream_return_surface`), and NO new gate: the stage's claim is a
# PROTOCOL claim (exactly one packet for the `buffered` form; an empty result is
# a packet and not an absence), and a protocol is asserted at `next()` by a
# consumer, not by an artifact grep. tier_commit therefore does not move.
# ⚠ WHAT DID NOT MOVE, AND IS THE POINT OF THE STAGE: the Vec surface. All 505
# emitted `*_run` entries are unchanged and the corpus delta is PURE ADDITION —
# measured as a line multiset over the 165-dump snapshot, ZERO lines of the
# baseline disappeared, 486 `*_stream` entries appeared, +567,879 bytes (+8.0%).
# That is why the codegen byte-pin `logos_09_slice_scan_codegen` needed no
# re-golden: it pins ONE function's text and this change adds siblings.
# S5-PIPELINE (§12's composition oracle). PREDICTED 7172 / 3489 / 36
# (+1 / +1 / 0) BEFORE the reconfigure and measured IDENTICAL after. ONE new
# fixture with its `.expected` (`pass/deem_pipeline_chain`), and NO new gate,
# for the same reason the stage above added none: the claim is a PROTOCOL claim
# counted at `next()` by a consumer — SEAM 1 in packets (exactly 1, the
# `buffered` form) and SEAM 2 in PULLS through the chained source (3 of 12 under
# `limit 3`, 12 of 12 unbounded as the control) — and no artifact grep can see a
# pull count. tier_commit therefore does not move.
# ⚠ WHAT THE FIXTURE DOES NOT ASSERT, both by measurement rather than omission:
# direct-over-direct (the `direct` form is not landed — the ADR's S5 bullet
# carries the three-way probe `s5d/pd1..pd3`, all rc 1 byte-identical), and the
# OUTPUT-seam `Drain` node (deliberately not inserted at S5; with `direct`
# absent the answer is constant across all 486 entries). "The plan says so" is
# therefore asserted at SEAM 2, where the plan has two answers to give and gives
# the positive-absence one (`no materialization … consumes it where it stands`).
# S6-A (ADR 0025 §2/S6 — the row-major batch layout) adds TWO registrations and
# PREDICTED both before the reconfigure: `pass/deem_rowmajor_batch_source` (the
# fixture) and `logos_09_rowmajor_batch_layout` (the artifact gate, tier_full).
# +2 ALL, +2 -LE imported, +0 tier_commit — measured 7174 / 3491 / 36, exactly
# the prediction. tier_commit does not move because reading the WRONG layout
# does not compile (`slice has no method 'a_at'`), so the commit tier already
# reds through the fixture; the gate is for the case that compiles and reads
# wrong.
# S6-B (ADR 0025 §10 — the DBSP seam) adds ONE registration and PREDICTED it
# before the reconfigure: `pass/stream_weighted_epoch_seam` (the boundary
# fixture), and NO gate. +1 ALL, +1 -LE imported, +0 tier_commit — measured
# 7175 / 3492 / 36, exactly the prediction. No gate because the claims are
# VALUE claims a running program answers (an epoch is one batch; `Some(empty)`
# != `None`; a mixed-sign packet folds to the same accumulator as the two
# constant-weight epochs it replaces; a `Buffer` replayed across an epoch
# boundary is z⁻¹) — there is no emitted artifact to grep, because the emitter
# is deliberately UNMOVED: `<q>_apply` still takes `(__src, __wd, &[R])` and
# the consumption seam is declared, not taken (ADR §10). The corpus artifact
# control for that "unmoved" claim is the byte-identical snapshot, not a
# registration.
# THE #47 ENTRY TICKET (ADR 0025 — the criteria and their instruments) adds TWO
# registrations and PREDICTED both before the reconfigure:
# `pass/deem_pipeline_handle_seam` (the pipeline arm whose q1 reads a CONTAINER
# HANDLE — the arm the S5 audit named as missing, without which criterion 3's
# reading is blind by construction) and `logos_09_rc_seam_hot_path` (the
# non-blind reference-count instrument that reads it, tier_full). +2 ALL,
# +2 -LE imported, +0 tier_commit — measured 7177 / 3494 / 36, exactly the
# prediction. tier_commit does not move because the fixture's own runtime oracle
# (one packet at seam 1; 3-of-8 pull-through with its unbounded control) already
# reds at the commit tier, while the ARTIFACT reading needs objdump and two full
# compiles. The criterion-1 instrument
# (`tests/logos/criterion1_materialization_instrument.sh`) adds NO registration
# and that is a decision with a reason, recorded in
# `docs/adr/0025-criteria-and-instruments.md` §1: its values are
# corpus-size-dependent by construction (one added fixture moved the denominator
# +10, another +15), so a ctest gate over them either re-baselines every commit
# or becomes a number to tune — the three properties of it that DON'T move with
# a slice are gated inside the script itself.
# ── R-A: THE C2 NUMBER, MOVED AND MEASURED (2026-08-15) ────────────────────
# The whole-corpus artifact sweep (`criterion1_materialization_instrument.sh`,
# 170 user dumps) run on TWO TREES — R-A, and the CONTROL with
# `rexpr_walk::slice_stream_src` forced to `return false` and the tree rebuilt —
# then counted for the three spellings that R-A is about:
#
#                                   CONTROL      R-A      delta
#   indexed walks (`while (+__i<k> <`)   1166     1017      -149
#   §1 batch pulls (`.next_batch()`)       15      164      +149
#   the wrap (`SliceStream::<`)             0      149      +149
#
#   THE ACCOUNTING CLOSES WITH NO REMAINDER: every wrap emitted removed exactly
#   one indexed walk, corpus-wide. (An earlier reading of this said -147 and it
#   was the READER, not the tree: `while (__i` misses the `limit` arm's
#   `while ((__i < …) && …)` double paren. The regex above catches both.)
#
#   ⚠ SCOPE OF ROW 1, so the two documents cannot be read as disagreeing: 1166
#   and 1017 are the `__i` FAMILY, which is what R-A's plane is made of. The
#   WHOLE indexed-walk population — every minted name, both paren spellings —
#   is 4301 -> 4152 on the same two trees, the same -149. The audit's published
#   "3975" is that population read with a regex that cannot see the double
#   paren (4293 on its own dumps, 318 sites missed); ADR 0025 §2 carries the
#   fixed derivation and the three-column table. Re-run:
#     grep -ho 'while ((\?[A-Za-z_0-9]* < [^;{]*\.len())' /tmp/<sweep>/*.user | wc -l
#
#   AND CRITERION 1 DID NOT MOVE: the instrument's own SUMMARY is IDENTICAL on
#   both trees — `N1/D1=1336/4042=33.05%  T=617  D2=3632 accounted=1919
#   (52.84%)`. That is the control for the claim R-A makes about itself: it
#   changed the SCAN SHAPE and materialized nothing — no new `Vec`, no new
#   `Buffer`, no new worklist class. `SliceStream` borrows.
#
# ⚠ WHAT IS NOT CLOSED, STATED AS A NUMBER RATHER THAN AS "PARTIAL". 1017
#   indexed walks REMAIN, and R-A routed exactly ONE of the S2j audit's 13
#   sites: `emit_simple`, both of its arms (the plain scan and the sort's phase
#   1). The other twelve — `emit_find`, `step_wrap` x2, `chain_nest_frag`,
#   `emit_aggregate`, `emit_incremental` x4, `rel_body_simple_frag`,
#   `chain_body_frag` — still emit the indexed walk for a slice param, and the
#   JOIN sites are where the per-scan-site cursor claim (`__ss<k>`, k numbered)
#   acquires its first real consumer: today every call site passes k = 0 because
#   `emit_simple` scans once. The wrap and its numbering are built for them; the
#   routing is not done, and 1017 is the honest size of what is left.
#
# R-A (ADR 0025 — the slice arm dissolves) adds THREE registrations and PREDICTED
# all three before the reconfigure: `pass/stream_slice_stream_seam` (the
# `SliceStream` contract, by values — one packet, the empty slice as a TICK,
# rewind, `size`, and `Epoch<R> == SliceStream<R> + w` as arithmetic rather than
# as prose), `pass/deem_slice_param_batch_e2e` (a `&[R]` PARAM through the wrap
# end to end: the empty source through three emitted shapes, the sort arm's
# body-side ordinal across a `where`, and a borrowed `str` key), and
# `fail/slice_stream_mutate_under_scan_fail` (the REFUSE half — mutating the
# owner under a live stream; its ADMIT half is the last block of
# `stream_slice_stream_seam`, the same `push` on the same `Vec` after the
# stream's last use, so the pair separates refusal from over-refusal).
# +3 ALL, +3 -LE imported, +0 tier_commit — measured 7180 / 3497 / 36, exactly
# the prediction. tier_commit does not move because R-A adds NO gate script: the
# EMITTED-TEXT claim already has one — `logos_09_slice_scan_codegen`, whose
# golden and whose presence/absence clauses INVERTED with this stage (see that
# script's header for the two numbers the transition was taken on) — and the
# remaining claims are VALUE claims a running program answers.
#
# D4 (the wrong-answer miscompile R-A's verify found, and which PREDATES R-A)
# adds TWO registrations, PREDICTED before the reconfigure and measured exactly:
# `pass/vec_struct_homonym_stride_deem` (the reported symptom — a deem over
# `vec.as_slice()` returning every other row — with its identical-layout,
# non-colliding CONTROL struct in the same program, so the fixture separates the
# defect from the shape) and `pass/vec_struct_homonym_stride_shapes` (the CLASS:
# the nine further shapes that reach the same `&recv.field[idx]` address path —
# set/swap/remove, iteration, `&mut` element write-through, the Vec in a struct
# field, `Vec<Vec<T>>`, a raw `*mut T` field indexed in USER code, `Option`
# payloads, reverse, and growth across reallocs — all measured RED on the
# pre-fix compiler by control revert, all green after). Both carry `.expected`.
# +2 ALL, +2 -LE imported, +0 tier_commit — measured 7182 / 3499 / 36. Neither is
# a gate script: both are VALUE claims a running program answers, and the value
# is what the defect got wrong.
#
# R-D STAGE 1 (ADR 0025, the FOURTH pull site) adds ONE registration, PREDICTED
# 7183 / 3500 / 36 before the reconfigure and measured exactly:
# `pass/deem_batch_build_side_join` — a BatchStream producer the plan STREAMS
# onto a join's BUILD side. That query did not compile on the previous tree
# (`let '__bo1': type mismatch — expected Option, got Option`, from
# `build_phase_frag`'s unconditional `Iterator::next` spelling), so this is a
# fixture whose ADMISSION is the claim; its VALUE half is the answer, which
# crosses a batch boundary INSIDE one hash bucket (key 1 lives in batch 0 and
# batch 1) so a build that stopped at the first batch answers 2 rows and reds
# with exit 2 — measured by control revert on the emitter arm itself, not at the
# call site. +1 ALL, +1 -LE imported, +0 tier_commit; not a gate script.
#
# R-D STAGE 2 (the cursor itself) adds a SECOND registration, PREDICTED
# 7184 / 3501 / 36 and measured exactly: `pass/wql_writ_walk_cursor` — the
# document walk reified from mutual recursion (`wg_walk`/`wg_descend`) to an
# explicit frame stack with an owned seen set and an INVENTED batch boundary
# (`WG_BATCH_CAP`), differentiated ROW FOR ROW ON ALL EIGHT COLUMNS against the
# `Vec` producer it replaces the traversal of. The oracle is the other producer
# and it is independent where it matters: the two share only the single-spelled
# leaf rules (`wg_node_id`/`wg_kind`/`wg_vi`) and the root coordinates, while
# the TRAVERSAL — the thing this stage rewrote — is written twice and compared.
# Control revert on the cursor's map ORDINAL bookkeeping (advance `ord` with
# `pos` instead of only on present slots) reds it at exit 5, the `child` column,
# which is FNV(parent, ordinal) — measured on a full rebuild, restored by md5.
# The fixture also pins `batches >= 2` (the walk is suspended and resumed mid
# document), two live independent cursors over one borrowed document, and
# `Rewind` as a RESTART. +1 ALL, +1 -LE imported, +0 tier_commit.
# ⚠ SCOPE, stated so R-E cannot inherit it inflated: the cursor is LANDED IN
# THE STDLIB, UNCONSUMED AND UNINSTRUMENTED — no `rel` declares over it (Stage
# 3 refused), it emits ZERO gen dumps, and every arc instrument (criterion1,
# plan_ground, rc_seam) reads the artifact/plan channels and is blind to it BY
# CONSTRUCTION. Its only witness is this hand-written differential, which L2's
# sampler does not select (verified passing directly). It is NOT plane coverage.
#
# ⚠ R-D STAGE 3 (declaring `rel edge` over the cursor) IS REFUSED, and its
# ground is MEASURED, not asserted — see the REFUSAL block at the foot of
# `stdlib/mem/wql/writ_graph.logos`. Throwaway build with `rel edge = writ_walk`
# and nothing else changed: all six writ fixtures still COMPILE and answer (the
# S6-A `Buffer` route killed three), but the plan gains +3 materialization nodes
# (same-188-fixture population; first recorded as +5 by a population mix the
# audit caught — 2,701 was the pre-round 186-fixture baseline) while D2 DROPS
# 3,954→3,933 and accounted IMPROVES 83.38%→83.83% (the refusal is on the
# node/byte axes, not one-sided), and the six fixtures gain +17,048 generated
# bytes, because a cursor makes
# `ap_offers` true and drops every writ rel into the drain cascade whose first
# arm is the blanket `if prog.has_rels()`. Criterion (a) is "at or below
# baseline", so the switch was reverted (md5-proven) and the writ fixture bytes
# on this tree are IDENTICAL to the pre-round baseline, all six.

# ── 2026-08-15 — R-E COMPILER, THE typeof-IN-MODULE ROUND-ORDER FIX (+2) ─────
# PREDICTED BEFORE THE RECONFIGURE, measured after, agreed exactly:
# ALL 7184→7186, -LE imported 3501→3503, tier_commit 36→36 (+1 pass, +1 fail,
# both local, neither carries a tier label). Verified individually with
# `ctest -N -R typeof_container` = 2.
#
# THE DEFECT. ADR 0025 §6 recorded `typeof(<container>)` as unresolvable "in any
# hand-written item of the declaring module" and priced the `direct` stream form
# behind it. It is a ROUND-ORDER defect in two sema sites, not a language limit:
#   * src/compiler/sema.cpp, resolve_type's TYPEOF_TYPE container arm — `<X>Cfg` is emitted
#     by the container item's handler in a LATER metaprog round, and round 0
#     hard-errored on the hand-written item that named it. The hatch for type
#     ALIASES (`alias_decl_resolve_`) was the narrow case of a general fact: a
#     user item is re-collected every round, so its signature re-resolves for
#     free. Now, while the container item is still CONTAINER_DEF (`ci->pending`),
#     the missing config is recorded as a DEMAND on `LProgram::pending` — who
#     owes what — and main()'s post-fixpoint sweep refuses if it never arrives.
#     CONTAINER_DEF_DONE with the config still absent keeps the original error.
#   * src/compiler/sema_expr.cpp, expect_type — the error-propagation rule ("uses of an
#     error-typed value are silent") tested only the OUTERMOST kind, so
#     `&mut <error>` / `&mut <error>::LeafWalk` hard-errored at the CALL in a
#     non-terminal round. A signature you can write but cannot call is half a
#     fix; both spellings are now covered (Error under pointee/elem/assoc-base/
#     type-args, GOT side only).
#
# ⚠ THE §6 ROW'S CAUSAL CLAIM WAS ALREADY FALSE AND STAYS FALSE. `direct` never
# needed this fix — the emitter spells the concrete `Hs…LeafWalk` as TEXT from
# `MacroParams` (R-E gate, measured). This is a hand-written-consumer ergonomics
# fix. `MG_OUT_BUFFERED()` and its two siblings in stdlib/mem/wql/rexpr_walk.logos
# still state that ground on 605 landings and must be rewritten by whoever moves
# `direct`, whether or not it lands.
#
# WHAT IS ADMITTED, and it is not everything: a hand-written `let` annotation, a
# fn PARAMETER, and a `pub type` alias — declared AND called
# (pass/typeof_container_hand_written_state, which checks its rows and key sum
# against the container's own per-row cursor, so the green is behavioural).
# A STRUCT FIELD is still REFUSED (fail/typeof_container_field_no_family_fail).
#
# ⚠ THE FIELD POSITION WAS ADMITTED, THEN REVERTED BY AN INDEPENDENT ORACLE.
# A stage flag in src/compiler/mlir_gen_types.cpp's register_struct (skip a not-yet-lowerable
# field in the per-round metaprog JIT gen, keep the final gen strict) DID make
# the used-struct form compile — and every such program then ABORTS under
# LOGOS_VERIFY_LAYOUT with `[declined] <kind 32>` (AssocType): two of the three
# layout engines cannot size the field. Reverted; the verifier was not touched.
# The five files it needed (src/compiler/mlir_gen.hpp, src/compiler/mlir_gen.cpp,
# src/compiler/mlir_gen_impl.hpp, src/compiler/mlir_gen_types.cpp,
# src/compiler/main.cpp) are back at 2cf0eaf1 verbatim.
#
# ⚠ B3, MEASURED AND REFUTED IN THE SAME ROUND: raising the FACTORY DEMAND from
# type-position resolution (`defer_factory_backed(make_metaclass_wrapper(cfg))`)
# to make the never-created-container case resolve made ALL ELEVEN probes worse,
# including the ones that pass today — "the factory did not satisfy the deferred
# trait obligation … the CtrFamily impl is missing after the drain round". The
# refutation is written at the site so the next reader does not re-try it.
#
# BLAST, both legs LABELLED by the tree they were measured on:
#   BASE = 2cf0eaf1, src/compiler clean (git stash), logosc md5 27f1eac1…
#   FIX  = 2cf0eaf1 + src/compiler/sema.cpp + src/compiler/sema_expr.cpp only, full `cmake --build`
#   * whole LOCAL pass corpus compiled both ways: 2087 → 2088 rows, ZERO moved,
#     the one added row is the new fixture (rc=0).
#   * whole LOCAL fail corpus both ways: 698 → 699 rows, ZERO moved, the one
#     added row is the new fixture (rc=1, its own .expected string still hit).
#     Refusal diffs are therefore zero, as predicted.
#   * answer_diff_instrument.sh, 188 rows each tree: ZERO moved.
#   * criterion1_materialization_instrument.sh: N1/D1=2703/5417=49.90%, D2 3954,
#     accounted 3297 (83.38%), worklist 657 — IDENTICAL to HEAD, as predicted
#     for a compiler-only change that touches no emitter.
#   * CONTROL: pass/typeof_container_hand_written_state FAILS on the md5-pinned
#     baseline binary and passes on the fixed one.
#
# FIRE COUNTS, and one of them is a correction. Over the 30 pass fixtures that
# spell `typeof(`: the deferral arm fires 40 times across 9 fixtures; the
# CONTAINER_DEF_DONE refusal arm fires ZERO times. Those 40 are NOT newly-quiet
# errors: they land inside enrich_deem_params' existing diagnostic-rollback
# probe (src/compiler/sema_expr.cpp ~22702), which already created and discarded that exact
# error. What is new there is only that the demand is now RECORDED.
# ⚠ THE ASYMMETRY IS REAL AND UNCLOSED: that probe rolls back DIAGS and not
# PENDINGS, so a demand raised inside a speculative resolve outlives it. It
# refuses rather than admits, and 2088 pass + 699 fail fixtures plus a full
# stdlib+tools build show no program taking that path to a new refusal — but it
# is an asymmetry, not a proof, and the symmetric rollback is the next round's.
# ⚠ NOT PINNED BY ANY FIXTURE, stated rather than papered over: the
# CONTAINER_DEF_DONE arm and main()'s post-fixpoint pending sweep for this
# demand. No source-level program in the tree reaches either — the first is dead
# in this corpus, the second needs a handler that is in scope and produces
# nothing, which cannot be written from source (dropping the `use` line refuses
# earlier, with its own diagnostic).
# ⚠ +1 / +1 / +0 at ADR 0025 R-H (2026-08-16): `logos_09_pull_shape`
# (tests/logos/pull_shape_gate.sh), the criterion-2 pull-shape gate. §7 of
# docs/adr/0025-criteria-and-instruments.md recorded that criterion 2 had NO
# gate of any kind — its numbers lived in prose and were re-typed by each
# stage, and three stages in a row (R-A, R-E, R-F) found a defect in one of
# those hand-run greps. `tier_full`, so tier_commit does not move.
# +71 / +70 / +0 (2026-08-16, the conuco arc): the conuco/memoria package moved
# from ONE aggregate gate to 67 per-test ctest entries plus a build fixture and
# a population witness (net +69 registrations: -1 aggregate, +69 new), and
# tests/logos/pass/dst_self_ptr_receiver.logos — the regression test for the
# `*const Self`-receiver-on-a-DST call that lowered to nothing — adds the last
# one. ALL moves by one more than NOIMPORTED because the pre-existing aggregate
# gate landed inside this same arc. All `tier_full`, so tier_commit does not move.
# +1 / +1 / +0 (2026-08-16): tests/logos/pass/if_expr_branch_local_drop.logos —
# the regression test for a `let` inside an if-as-EXPRESSION branch whose drop
# was emitted after the whole `if`, so the untaken path freed a stale slot.
# +1 / +1 / +0 (2026-08-16): the conuco memoria suite gains `ctr_vec_bool` — the
# positional family's bool element type, whose cursor `value()` and handle
# `get()` emitted `as bool` and could not be built at all. Per-test conuco
# entries are globbed, so the population grows by one with the file.
# -2 / -2 / +0 (2026-08-16): the memoria port's 68 tests moved OUT of
# conuco/memoria and into the corpus as `memoria_*`. The conuco scaffolding they
# needed — a build fixture and a population witness — goes with them (70 entries
# out, 68 in), because a corpus test needs neither: the population witness IS the
# glob the whole corpus already uses, and the fixture is an ordinary archive
# dependency.
# -1 / -1 / +0 (2026-08-16): the Deem×Memoria showcase left the corpus for
# examples/deem_memoria_showcase.logos. It is a DEMO — it prints its answers and
# nothing in the build runs it — so it registers no test. It still verifies the
# rows it prints and says `self-check: FAILED at check <n>` when they disagree.
# +1 / +1 / +0 (2026-08-18): tests/logos/pass/writ_objdata_roundtrip.logos — the
# WritObjectData freeze/thaw round trip (ADR 0027 OPEN-1).
# ⚠ AND THE PIN'S `ALL` WAS ALREADY ONE HIGH. Measured on this tree with the new
# test REMOVED: ALL 7257 / -LE imported 3574, against a pinned 7258 / 3574. So
# the +1 lands on a baseline of 7257 and `ALL` stays 7258 BY COINCIDENCE — the
# new test papered over a stale row rather than moving it. The stale +1 dates to
# 05fe9e45 (the showcase leaving the corpus was priced -1/-1 and only
# NOIMPORTED actually moved). Numbers below are measured, not derived.
# +5 / +5 / +0 (2026-08-18): the `#[zone_mut]` thin-source refusal — the PAIR, and
# the count is the prediction. THREE fail tests (tests/logos/fail/
# zone_mut_thin_source_{ref,place,wmap}.logos: the raw-pointer leg, the safe-code
# place leg, and the partial-spec leg over WMap<WString,WAny>) and TWO pass
# controls (tests/logos/pass/zone_mut_thin_source_admits{,_wmap}.logos: the
# zone_mut_ref constructor, the fat→fat reborrow that NO test in the tree pinned
# before, the shared-`&` arm that must stay thin, and the deliberately-thin
# WMap<Wu6,WAny> / WMap<K,WAny> specs — the abuse direction of the spec
# exemption). A refusal without its admit cannot tell separation from
# over-refusal, so 3+2 is the unit, not 3.
# +8 / +8 / +0 (2026-08-18, second round): the ABUSE pass found the first round's
# refusal bypassed on two axes, and each axis is priced as a refuse+admit PAIR.
# (a) AGGREGATE laundering, 3 fail — a tuple/array coerces ELEMENTWISE inside
# types_compatible, so the element position never reached expect_type and the
# refused `*mut T` → `&mut T` substitution walked straight past it (including the
# ORIGINAL WMap<WString,WAny> defect, tuple-laundered):
#   tests/logos/fail/zone_mut_thin_source_tuple.logos
#   tests/logos/fail/zone_mut_thin_source_array.logos
#   tests/logos/fail/zone_mut_thin_source_tuple_ret.logos
# (b) POST-MONO, 3 fail — inside a generic body the pointee is a TypeVar, so sema
# is structurally unable to decide, and the deciding instantiation is written by
# the USER (`Box<ZS>` reaches it from FULLY SAFE code, no unsafe and no cast).
# The claim that this hole was harmless because "the sweep measured ZERO in-tree
# instantiations" is refuted by construction: an in-tree sweep cannot see a
# user's type argument.
#   tests/logos/fail/zone_mut_thin_source_generic.logos
#   tests/logos/fail/zone_mut_thin_source_generic_place.logos
#   tests/logos/fail/zone_mut_thin_source_box_safe.logos
# The 2 admits are what separate the rule from a blanket refusal — a generic
# instantiated AT the `#[zone_mut]` type still RUNs when it only reborrows an
# already-fat `&mut`, and the identical aggregate literals still RUN when the
# element is a genuine fat ref:
#   tests/logos/pass/zone_mut_thin_source_admits_aggregate.logos
#   tests/logos/pass/zone_mut_thin_source_admits_generic.logos
REGISTRY-ALL         7424
REGISTRY-NOIMPORTED  3741
REGISTRY-TIERCOMMIT  36
# 2026-08-21 (#95 diagnostic RE-SPELLING — the aggregate-slot refusal stopped
# impersonating the type-mismatch verdict. The two new emitters spelled
# `expected {}, got {}`, which scripts/lint-mismatch-monopoly.sh reserves for
# SemaChecker::expect_type — it counted three emitters and redded L4. The lint's
# ground is that a position wanting to reject must route through the JUDGMENT,
# not re-implement its verdict; and this refusal is a DIFFERENT verdict (the
# types are not merely unequal — the slot is not a coercion site and the trait
# is not implemented, so the element's vtable half would be uninitialised).
# Re-spelled as `slot type X needs an unsize from Y, but the type does not
# implement the trait, …`; the lint reads 1 emitter. Three .expected re-aimed
# under the RE-SPELLING rule (grep of tests/ + *.sh + CMakeLists.txt returned
# exactly these three, all fixture text, no gate pattern):
#   tests/logos/fail/aggregate_unsize_enum_literal_no_impl.expected
#   tests/logos/fail/aggregate_unsize_boxdyn_wrong_trait_fail.expected
#   tests/logos/fail/tuple_dyn_element_no_impl.expected
# No fixture added or removed; the registry counts are unchanged by this note.
# 2026-08-21 (#95 M1/M2/M3 — the three LIVE CRASHES the #95 round's OWN VERIFY
# found inside the #95 landing. All three were in find_uncoerced_aggregate_slot
# or its callers, and all three were the PERMISSIVE direction of a question
# whose `true` is a refusal.
#   M1 the DEPTH CAP admitted on exhaustion — `depth > 8 -> return false` said
#      "no uncoerced slot" when it had merely stopped looking; depth >= 9
#      compiled and SIGSEGVed (rc 139) and the cap also defeated the impl check
#      at depth. Replaced by a NODE BUDGET whose exhaustion REFUSES, plus an
#      `expected == actual` short-circuit so ordinary code never reaches it.
#   M2 the OWNING `Box<dyn>` slot was EXEMPTED as "unmeasured, narrow on
#      purpose"; measured it was rc 139 in three shapes. The literal half now
#      COERCES (the annotated spelling already lowered correctly, which is what
#      settled the decision) and the hoisted half REFUSES.
#   M3 the generic-instance arm was guarded Struct-and-Struct, so an ENUM
#      instance was never walked: `Option<&Sq>` against `Option<&dyn Shape>`
#      compiled and returned the WRONG ANSWER, wrong-trait included. The arm now
#      covers Enum, its type-arg recursion passes at_slot, and an enum LITERAL
#      is stamped at a coercion site like a tuple/array one.)
# PREDICTED +13/+13/0 (7411/3728/36 -> 7424/3741/36) before the reconfigure and
# MET exactly. THIRTEEN new files: ten fail, three pass.
#   M1 tests/logos/fail/aggregate_unsize_deep_nesting_fail.logos      (was 139)
#      tests/logos/fail/aggregate_unsize_deep_wrong_trait_fail.logos  (was 139)
#      tests/logos/fail/aggregate_unsize_deep_array_fail.logos        (was 139)
#      tests/logos/fail/aggregate_unsize_budget_exhausted.logos       (fires the
#        exhaustion arm itself: an alias-doubled 2^11-slot type)
#      tests/logos/pass/aggregate_unsize_deep_nesting_admit.logos     (the SAME
#        depth, built at the coercion site, so it coerces)
#   M2 tests/logos/fail/aggregate_unsize_boxdyn_hoisted_fail.logos    (was 139)
#      tests/logos/fail/aggregate_unsize_boxdyn_hoisted_array_fail.logos (139)
#      tests/logos/fail/aggregate_unsize_boxdyn_wrong_trait_fail.logos (the
#        owning copy of the permissive twin: `Sq: !Other` used to compile)
#      tests/logos/pass/aggregate_unsize_boxdyn_literal_admit.logos   (was 139;
#        pins BOTH the call-site literal and the annotated binding)
#   M3 tests/logos/fail/aggregate_unsize_enum_instance_fail.logos     (was a
#        WRONG ANSWER, rc 1)
#      tests/logos/fail/aggregate_unsize_enum_wrong_trait_fail.logos  (compiled)
#      tests/logos/fail/aggregate_unsize_enum_literal_no_impl.logos   (the enum
#        copy of the permissive twin, opened BY the M3 admit and closed with it)
#      tests/logos/pass/aggregate_unsize_enum_literal_admit.logos     (was rc 1)
# 2026-08-21 (#95 — THE ROOT of the #68 class: `types_compatible`'s
# "Struct -> &dyn Trait coercion (impl check deferred to codegen)" arm is a
# BLANKET ACCEPT, and the Tuple / Array / generic-instance arms walk an
# aggregate ELEMENTWISE into it, so a value already typed as the thin aggregate
# is accepted where the fat one is wanted and NO coercion is ever attempted).
# PREDICTED +7/+7/0 (7404/3721/36 -> 7411/3728/36) before the reconfigure and MET
# exactly. SEVEN new files: six fail, one pass.
#   tests/logos/fail/aggregate_unsize_needs_cast_tuple.logos          (was 139)
#   tests/logos/fail/aggregate_unsize_needs_cast_array.logos          (was 112 —
#     a WRONG ANSWER, not a segfault: same root, different crash shape)
#   tests/logos/fail/aggregate_unsize_needs_cast_enum_payload.logos   (was 139)
#   tests/logos/fail/aggregate_unsize_needs_cast_generic_field.logos  (was 139)
#   tests/logos/fail/aggregate_unsize_needs_cast_wrong_trait.logos    — the
#     PERMISSIVE twin's hoisted half; fail/tuple_dyn_element_no_impl closed only
#     the literal half, which reached a stamp attempt. This one needs no impl
#     question at all.
#   tests/logos/fail/aggregate_unsize_generic_field_literal_overrefusal.logos —
#     ⚠ PINS AN OVER-REFUSAL ON PURPOSE. Rust accepts it; Logos does not
#     propagate the expectation through a generic struct literal's field, so no
#     literal is in reach where the mismatch surfaces. It ran rc=139 before, so
#     the refusal is strictly better than what it replaces; the fix is an
#     inference change, filed as TASK #96, and this test GOES RED when it lands.
#   tests/logos/pass/aggregate_unsize_literal_and_cast_admit.logos — the admit
#     half, eight groups: the literal at a coercion site (tuple + array), the
#     explicit `as &dyn` hoisted, the SCALAR unsize in both spellings, a plain
#     struct field from a literal, an enum payload from a LITERAL (rc=139 before
#     #95 — the payload site guarded its `expect_type` with the very
#     `types_compatible` that blanket-accepted the pair, so neither the stamp nor
#     the refusal was reachable), a generic field with an explicit cast, and the
#     already-fat / thin-concrete siblings with their sizeof facts (16 vs 24).
# 2026-08-21 (#68 CLASS — an aggregate LITERAL's slot types have no second
# source, so the `&Concrete` -> `&dyn Trait` unsize had to be recorded on the
# LITERAL at the coercion site), PREDICTED +2/+2/0 (7402/3719/36 -> 7404/3721/36)
# before the reconfigure and MET exactly. TWO new files, not three: the third
# fixture of the round, tests/logos/pass/tuple_dyn_element_implicit.logos, was
# already in the 7402 baseline (the earlier, let-only spelling of the fix added
# it) and is EXTENDED here rather than added — it now carries the eight contexts
# the verify measured, each asserting a runtime value.
#   tests/logos/pass/array_dyn_element_implicit.logos  — the ARRAY half of the
#     class: call argument (was 176), struct field (was 139) and assignment (was
#     139), with `let` / `return` kept as the pair that was ALREADY green and is
#     the reason the earlier round concluded "arrays are safe".
#   tests/logos/fail/tuple_dyn_element_no_impl.logos   — the PERMISSIVE twin
#     this round's own probe found: a `&dyn Other` slot fed a `&Sq` that does not
#     implement Other USED TO COMPILE to an object file, because types_compatible
#     blanket-accepts Struct -> TraitObject and defers the impl check to codegen,
#     which in the aggregate case never runs (nothing attempts the coercion).
# 2026-08-21 (#69 class A — a loop body whose fall-through bottom is a `-> !`
# CALL was treated as reaching the back edge), PREDICTED +3/+3/0
# (7399/3716/36 -> 7402/3719/36) before the reconfigure and MET exactly. One
# admit and TWO refuses, because the repair moves the borrow checker in the
# PERMISSIVE direction and one refuse cannot separate the two back-edge
# channels:
#   tests/logos/pass/bc_loop_bot_divergent_call_admit.logos — the admit. Control
#     revert of src/compiler/borrow_check.cpp: rc 1, "use of moved value 'g'".
#   tests/logos/fail/bc_loop_bot_plain_call_refuse.logos — the arm's CONDITION.
#     Same body, tail call returns `i64` instead of `!`: the bottom does reach
#     the back edge and the refusal stands.
#   tests/logos/fail/bc_loop_bot_divergent_no_reinit_refuse.logos — the OTHER
#     channel. The `-> !` tail is present (so the new arm fires) but the
#     `continue` path drops the re-init, so frame1.continue_states still carries
#     the move and the refusal stands. A repair that suppressed the whole back
#     edge on divergence, not just the fall-through arm, would look green
#     without this one.
# The imported red this closes —
# tests/imported/pass/for-loop-while/loop-no-reinit-needed-post-bot-b145.logos —
# is outside every gate, which is why it survived; the admit fixture is the
# gated copy of its shape.
# 2026-08-21 (#68 — a `&dyn` TUPLE ELEMENT with no explicit `as &dyn` cast:
# SIGSEGV, not a diagnostic), PREDICTED +1/+1/0 (7398/3715/36 -> 7399/3716/36)
# before the reconfigure and MET exactly. ONE fixture, and the reason there is no
# refuse twin is stated rather than skipped: this is a CODEGEN repair, not a
# checker rule — the defective spelling was already ACCEPTED by sema and the
# borrow checker and then miscompiled, so there is no refusal to pair with. The
# pair that does the separating work here is admit×admit across the two
# SPELLINGS, and the second half already existed:
#   tests/logos/pass/tuple_dyn_element_implicit.logos — NEW. `(&a, 7i64)` at type
#     `(&dyn Shape, i64)`, plus dyn in the SECOND slot and a two-dyn literal
#     mixing the cast and no-cast spellings. Bite-proven: on the control build
#     (the one-arm revert of src/compiler/mlir_gen_expr.cpp) it exits 139, and on
#     the fixed build 42.
#   tests/logos/pass/tuple_dyn_element_inline.logos — PRE-EXISTING, unchanged.
#     Pins `(&a as &dyn Shape, 7i64)`, and it is GREEN ON THE CONTROL BUILD too.
#     That is what makes the pair informative: the corpus pinned the cast
#     spelling and left the implicit-coercion spelling unpinned, and the delta
#     between the two fixtures on one control build is exactly the missing arm.
# The arm added to gen_expr_kind(ETupleLitView) in src/compiler/mlir_gen_expr.cpp
# is the one gen_arr_lit already carries per array element in
# src/compiler/mlir_gen.cpp (peel Ref/MutRef/Ptr -> TraitObject,
# concrete_struct_name, coerce_to_dyn, then a 16-byte MemcpyOp into the slot GEP
# — an 8-byte store would leave the vtable half uninitialised).
# 2026-08-21 (#61 D6 — a typeof-container projection in a struct FIELD, an enum
# PAYLOAD and a TUPLE element), PREDICTED +2/+2/0 (7396/3713/36 -> 7398/3715/36)
# before the reconfigure and MET exactly. One admit + one refuse, 1 ctest test
# each, neither labelled `imported`, no gate added:
#   tests/logos/pass/typeof_container_field_admit.logos — the field and the enum
#     payload are constructed, drained and checked against `c.seek`/`c.next` (a
#     per-row cursor that knows nothing about the batch plane), under
#     LOGOS_VERIFY_LAYOUT=1 like every pass test.
#     ⚠ CORRECTED 2026-08-21, and the correction is the point: this entry, and
#     the header of the fail twin below, both claimed a TUPLE ELEMENT was
#     constructed and checked here. THERE IS NO TUPLE IN THAT FIXTURE. The round
#     verify caught it, and re-measuring by hand confirms the cell is NOT fixed:
#     `pub struct S { t: (<typeof(K) as CtrLeafFamily>::LeafWalk, u64) }` with
#     the family demanded and the holder CONSTRUCTED is rc 1 ("unknown tuple
#     field type in 'S'" + "struct literal field 't': expected (…::LeafWalk,
#     u64), got (<error>, u64)"), and the SIZED-only spelling still prints
#     "unknown tuple field type" as a now-non-fatal metaprog-round diagnostic.
#     CONSEQUENCE, stated rather than papered over: the round's second
#     permissive-hole repair — the on-demand register_struct recovery in
#     tuple_llvm_type's Struct arm — has NO passing pin. Only its DECLINE arm is
#     pinned (by the fail twin). By this round's own C4 standard (an arm that
#     fires zero times is not landed) that repair is UNPINNED, not proven. Filed
#     as its own cell; repro /tmp/d6v/probes/p23_tuple_field_notype_literal.logos
#     and the constructed spelling in the same directory.
#   tests/logos/fail/typeof_container_tuple_field_no_family_fail.logos — pins
#     the arm this round ADDED: register_struct's Tuple branch used to answer
#     `ptr_type()` for an element it could not size (a silent 8 bytes against
#     layout_of's 16), and now declines. Without the refuse half that arm fires
#     zero times in the corpus.
# The two existing canaries are RE-GROUNDED, not weakened: pass/typeof_container
# _hand_written_state and fail/typeof_container_field_no_family_fail each
# recorded "the field position aborts under LOGOS_VERIFY_LAYOUT with `[declined]
# <kind 32>`" as the reason the position stayed refused. The measurement was
# real; the conclusion was not. Those declines were recorded in metaprog fixpoint
# ROUND 0, and the ledger deduped (engine,key) FIRST-WINS, so round 0's {8,8}
# guess beat the final round's correct 72 about the SAME struct. Both fixtures
# keep their exit codes; only the recorded ground moved.
# 2026-08-20 (the METHOD-RESOLUTION channel of the same class), PREDICTED
# +4/+4/0 (7392/3709/36 -> 7396/3713/36) before the reconfigure and MET
# exactly: two pass fixtures (mlirgen_odr_drop_glue_homonym and its `_ctl`)
# and two fail fixtures (mlirgen_odr_operator_homonym and its `_ctl`), 1 ctest
# test each, none labelled `imported`, no gate added.
# 2026-08-20 (#59 — the FREE-FN generic-instance channel of the same class),
# +2/+2/0 (7390/3707/36 -> 7392/3709/36), predicted before the reconfigure and
# met exactly: the two missing `_ctl` twins
# (mlirgen_odr_vec_datumcol_ctl, mlirgen_odr_vec_header_ctl), 1 ctest test
# each, neither labelled `imported`. No new NON-control fixture: the three
# vec_* homonym fixtures were RE-PROVOKED in place (2 pushes -> 8, and the
# element widened past the stdlib homonym's size) because at 2 pushes none of
# them could see the defect they name — measured: under the control revert the
# 2-push versions PASSED.
#
# THE FREE-FN CHANNEL. sema's `mangle_type_for_name` folds
# `ambiguous_type_arg_fingerprint` into a struct type ARG; mono's `mangle_type`
# (mono_impl.hpp, the composer behind `Mono::mangle(base, type_args)` — every
# generic FREE-FN instance name) did NOT. One emitted IR carried both
# spellings: `Vec$G1$ExprBlob$M975819681a395537` (method channel, folded) next
# to the bare `<mem-archive-prefix>vec$vec_new__g__void__ExprBlob` —
# and that bare symbol is T-defined in liblogos-mem.a at the STDLIB ExprBlob,
# so the user's `vec_new<test.ExprBlob>` was elided and `init_cap *
# sizeof::<T>()` allocated for an 8-byte element. Measured threshold: 4 pushes
# of a 16-byte user struct fine, 5 => abort (64/16 + 1). Fixed by giving mono's
# Struct arm the same fold under the same predicate.
#
# THE ENUMERATION (measured, by an env-gated trace at every mangler site that
# spells a type into a symbol; counts are calls in ONE compile of the 8-push
# ExprBlob probe, FOLDED/BARE read off the produced text):
#   sema mangle_type_for_name Struct       50540 calls   FOLDED (#58) — with 44
#     BARE, all in one contiguous pre-install window (the ambiguous-set is not
#     yet installed there); only the STDLIB ExprBlob and `Vec$G1$ExprBlob` pass
#     through it, never the user's, and no emitted symbol keeps that spelling.
#   sema mangle_type_for_name Enum          1596 calls   NOT folded (module
#     suffix only)
#   mono mangle_type Struct                21298 calls   FOLDED (#59, this round)
#   mono mangle_type Enum                   7033 calls   NOT folded
#   mono enum_instance_name (composer)     37933 calls   no suffix at all (the
#     divergence already recorded at its own definition site)
#   mono mangle(base, type_args), the FREE-FN instance composer  7321 calls —
#     folds THROUGH the Struct arm above, which is why one change closes it.
# concrete_struct_name / concrete_struct_name_raw / the DstRef arm / sema's
# function_signature_key route their ARGS through sema's Struct arm and fold
# with it — they are not separate decision sites.
#
# THE ENUM CHANNEL STAYS OPEN, deliberately, measured. `Ordering` and
# `ControlFlow` are the two stdlib bare names declared in >=2 packages. One
# compile of a user `enum Ordering`: user-package spellings BARE 218 (sema) +
# 555 (mono); `logos.lang.cmp` FOLDED 280 (+28 bare in the pre-install window);
# `logos.lang.atomic` BARE 162 — two stdlib packages of ONE module already
# disagree, because atomic's package has no owning module_id in the map while
# cmp's does. NO BITE against today's archives: over every `__g__` instance
# symbol in the shipped archives, ZERO have a bare ambiguous ENUM as the type
# argument, so no user enum can bind to a prebuilt definition. Folding it would
# move stdlib symbol TEXT (atomic's `..__f__..__Ordering` params) — the shape
# the previous round refuted for identities — so it needs its own round with
# its own control, not a ride-along. NOT fixed, NOT fixtured (a fixture that
# cannot see its defect is the F7 anti-pattern), NAMED here.
#
# ALSO MEASURED, NOT FIXED: mono's `mangle_type` has no ZonedStruct arm (falls
# to `type_str`) while sema's routes ZonedStruct through the Struct arm.
# 2026-08-20 (#58/#59/#60 — the bare-struct-name IDENTITY class), +16/+16/0
# (7374/3691/36 -> 7390/3707/36), predicted before the reconfigure and met
# exactly: 16 pass fixtures, 1 ctest test each, none labelled `imported`.
#
# THE DEFECT, in two halves.
#
# (1) #58/#59 — THE GENERIC-INSTANCE SYMBOL. A user `Vec<test.ExprBlob>` in a
#     PLAIN compile (one source file straight to `logosc`, no `--emit-module`)
#     mangled to the bare `Vec$G1$ExprBlob`. That is a symbol DEFINED in
#     liblogos-mem.a — the stdlib's prebuilt
#     `Vec<logos.std.compiler.metaprog.ExprBlob>` — so `is_binary_skip` ELIDED
#     the user's instance body (`declare`, not `define`, in the emitted IR) and
#     every call bound to an element stride of 8 instead of 16. Measured on an
#     8-line program with no deem and no slice: `b=103138573533184`. The same
#     program with `Header` SIGSEGV'd (139); with `DatumCol` it printed
#     correctly and then SIGSEGV'd in drop (134). 25 nominal names appear as
#     `Vec$G1$<Name>` DEFINED in the shipped archives and 420 struct names are
#     declared in stdlib, so any user struct taking one of those names was
#     exposed. The G156-1 ambiguous-name fingerprint EXISTED and did not fire:
#     `type_module_suffix` declined for a package with no owning module_id,
#     i.e. the fingerprint was granted to the stdlib side (whose package IS in
#     a module) and withheld from the user side. Applied to exactly the side
#     that does not need it.
#
# (2) #60 — THE BARE LOOKUP SITES. `TypeRef::struct_name()` and
#     `PatStructView::struct_name()` yield a BARE base name: no package, no
#     `$M<fp>` fold, no `$G…` instance suffix. A `struct_types_` /
#     `all_struct_defs_` lookup on one lands in the first-registered-wins BARE
#     alias installed by register_struct / mlir_gen.cpp:127, so a user struct
#     took an IMPORTED homonym's field offsets and footprint.
#
# WHAT LANDED FOR (1), AND THE REFUTED ATTEMPT THAT CAME FIRST.
# The obvious fix — drop the `if (!mid.empty())` guard so `type_module_suffix`
# folds for every ambiguous name — WAS WRITTEN, BUILT, AND REVERTED. It broke
# def==use, because a nominal type's suffix is part of its IDENTITY and identity
# is minted in `collect_impl` (concrete-specialisation impl targets), which runs
# BEFORE `set_ambiguous_type_names` is installed — the set is BUILT from what
# collect registered. Measured, three ways, on the tree that carried it:
#   * `impl Header<i64> { fn weight(..) }` in a package with no module_id ->
#     "'Header$M05bf4536d7351c91$G1$i64' has no method 'weight'" (the impl
#     target was keyed UNFOLDED in collect, the call site FOLDED in lower);
#   * the `Box` / `Rc` / `Arc` lang-item shapes lost their receiver type
#     entirely ("method call: receiver is not a struct (got i64)");
#   * tests/logos/ir/metacall_instantiate.logos (a user `struct Box<T>` next to
#     `logos.mem.boxed.Box`) went red on exactly that message. CONTROL: with
#     only that one sema hunk reverted and a logosc-only rebuild, the 8-line
#     repro compiled at rc 0; with it restored, rc 1.
# What landed instead is `ambiguous_type_arg_fingerprint` (src/compiler/sema.cpp,
# consumed by `mangle_type_for_name`'s Struct/ZonedStruct case): the TYPE-ARG
# half of the fold and only that half. A generic instance NAME is not an
# identity minted in collect — it is a SYMBOL minted in mono/lower, on the
# definition and the use side alike, always after the set is installed. So
# `Vec<test.ExprBlob>` becomes `Vec$G1$ExprBlob$M<fp>` and stops matching the
# archive symbol, while `test.ExprBlob`'s own identity stays `ExprBlob` and
# collect/lower cannot desynchronise. It fires ONLY where type_module_suffix has
# already declined AND the name is ambiguous, so a uniquely-named type and every
# type in a package that HAS a module_id are byte-identical to before.
# `scripts/abi-check.sh` rc 0: sym 12518 / type 369 / vtable 123, ADDED 0,
# VERDICT ABI-PRESERVING, abi_closure_gate OK (3 canaries caught).
#
# WHAT LANDED FOR (2) — measured, not swept. Each converted site was fitted with
# a temporary `odr_fire` print (site, chosen key, bare key, and whether the bare
# slot holds a DIFFERENT llvm_type) and swept over the WHOLE 2229-fixture pass
# corpus ON THE LANDED TREE. The instrument was then removed and the tree
# rebuilt and re-gated. `div=1` = the bare key this site used BEFORE resolves to
# a different LLVM aggregate than the qualified key it uses now, i.e. the
# conversion is load-bearing at that fire.
#
#   SITE                                        fires    div=1   fixture producing div=1
#   A gen_match extract_payload PatStruct           37       3    mlirgen_odr_match_stmt (1), mlirgen_odr_pat_refutable (2)
#   B gen_expr(EMatchExprView)  PatStruct           13       2    mlirgen_odr_match_expr
#   C pat_test                  PatStruct            8       2    mlirgen_odr_pat_refutable
#   D pat_bind                  PatStruct            3       1    mlirgen_odr_pat_nested
#   T tuple_llvm_type element footprint           6211       4    mlirgen_odr_tuple_field
#   F register_variant inline field footprint   107918      85    vec_struct_homonym_stride_shapes (the D4 fixture)
#   E DST drop glue (TypeRef::struct_name)           3       0    reached, never divergent
#   I WritLit result struct from ret_type          771       0    reached, never divergent
#   H dstref_has_slice_tail                         59     n/a    behaviour change, see below
#   G mlir_gen_dyn rel-path field walk               0     n/a    DELETED, see below
#
# A/B/C/D go through one new helper, `pat_struct_ty` (src/compiler/mlir_gen_impl.hpp):
# it narrows the SCRUTINEE TypeRef (peeling ref/ptr) to the struct the pattern
# names, returning null when the scrutinee is not one — enum-variant payloads,
# tuple scrutinees, missing type info — so the caller keeps the bare key as the
# LAST resort. That is sema's recorded find_struct_repr_ order (⚠ QUALIFIED KEY
# FIRST, BARE SLOT LAST): a program declaring its own S still cannot alias a
# foreign S, and no site that resolved before becomes a miss. The lookup ORDER
# is unchanged, so the two imported tests the sema comment names cannot regress
# by reordering.
#
# G WAS DELETED, NOT SHIPPED. The `mlir_gen_dyn.cpp` rel-path field walk is a
# bare site with a TypeRef in hand, cell (c) by inspection. Its conversion was
# WRITTEN, built, and measured to fire ZERO times over the whole pass corpus. An
# arm that never executes is not evidence; `git checkout` restored the file and
# the site stays in the partition as "not reached by this corpus" — neither
# proven wrong nor proven unreachable.
#
# H IS A BEHAVIOUR CHANGE, DECLARED. `dstref_has_slice_tail` looked up ONLY the
# bare `struct_name()`, so for a monomorphized generic DST it MISSED and
# returned false — the same bug its neighbour `dstref_pointee_self_describing`
# already documents and works around. It now probes qualified -> concrete ->
# bare. All 59 corpus fires are `div=2` (the new key HITS where the bare key
# missed), so it genuinely changes what the fat/thin discriminator answers at 59
# sites. It has NO red-first fixture of its own — it is carried on the corpus
# gates, and is named here so the next reader can revert it independently.
#
# A LEDGER ROW WAS DELETED, NOT MOVED: `pat_6` (tests/logos/mlir_gen_bug.ledger).
# Its recorded cause — "a struct pattern over a GENERIC struct (`GPair<u8,u16>`)
# binds fields whose initialisers lower to nothing" — was this very defect: the
# bare `GPair` key missed because the instance is registered as
# `GPair$G2$u8$u16`, so gen_match's PatStruct case returned early and every
# dependent statement vanished. `logos_00_mlir_gen_bug_ledger` reported it
# ("pat_6 no longer self-diagnoses — the defect is FIXED"), the row was removed,
# and tests/spec/pass/pat_6.logos is now held to the R2 rule with the rest
# of the corpus (logos_25_spec_pass_pat_6 green). `logos_00_layout_decline_ledger`
# reported the same for `zone_zvec_two_zones` on the REFUTED variant only; on
# the landed tree it is green with its entry intact, so nothing was removed
# there.
#
# CELLS NOT CONVERTED, and why (the partition is the deliverable):
#   (a) ALREADY QUALIFIED — key is `mlir_struct_key(TypeRef)` or a registration
#       key. mlir_gen_types.cpp register_struct field embed and the `info->name`
#       sweep; mlir_gen_fn.cpp 74/124/557/574/598; mlir_gen_stmt.cpp
#       186/951/1173/1904/2918/3059; mlir_gen_expr.cpp 1263; mlir_gen_dyn.cpp
#       1740/2033; mlir_gen_debug.cpp 218; mlir_gen.cpp gen_struct_lit and the
#       DstRef chain in gen_recv_struct (both qualified-first, bare-last).
#       ⚠ A SECOND HAZARD DIRECTION IS OPEN THERE: mlir_gen_fn.cpp's five sites
#       are qualified with NO bare fallback, so a bare-only-registered struct
#       MISSES and degrades to `ptr_type()` (8 bytes). Unmeasured; not touched.
#   (c) CONCRETE_STRUCT_NAME FAMILY — the ~25
#       `struct_types_.find(concrete_struct_name(t))` sites. `qualify_pkg` and
#       `concrete_struct_name` fold the same suffix, so these are correct
#       whenever the qualified registration exists; the two that were MEASURED
#       divergent (T and F) were converted to find_struct_it, the rest are not
#       reached divergently by any fixture in the corpus.
#   (d) QUALIFIED BY CONSTRUCTION THROUGH A STRING CHANNEL — mlir_gen.cpp
#       961/990, mlir_gen_expr.cpp 2596, mlir_gen_stmt.cpp
#       3277/3292/3362/3374/3414. Keys come from `StructInfo::name` /
#       `StructInfo.fields[].struct_name` / `var_struct_`, all WRITTEN as
#       `mlir_struct_key`. Correct only while every producer stores qualified
#       keys — that wants an assertion at the PRODUCER, not a conversion here.
#   (e) HARDCODED BARE, cannot be qualified from a TypeRef —
#       `struct_types_.find("WritStatic")` in mlir_gen_expr.cpp. The ret_type
#       half of that site WAS converted; the `"WritStatic"` default remains a
#       bare key and a user struct named `WritStatic` still competes for it.
#   (f) MONO SIDE — mono.cpp 1042/1092/1117, mono_scan.cpp 276/295/298,
#       mono_clone.cpp 5621, mono_impl.hpp 621 (`concrete_struct_types_`, whose
#       bare/`strip_mtags` aliases are LAST-wins where mlir-gen's are
#       FIRST-wins). Not fired, not converted. The diagnosis's S3 (poison the
#       bare alias at registration) and S2 (un-blind `ffo_canonical` /
#       `strip_mtags` to `$M`) were NOT done: no measured program needs them
#       once the type-arg fold lands, and S3 without S5 (a loud decline at every
#       miss path) trades a silent mis-bind for a silent 8-byte miss. Named
#       debt, not silently dropped.
#
# EVIDENCE. CONTROL REVERT of the whole src change (git stash, FULL rebuild
# rc 0): all 7 of the then-existing homonym fixtures RED — `mlirgen_odr_match_expr` r=33554432,
# `match_stmt` "a= b=", `pat_nested` r=3, `pat_refutable` red, `tuple_field`
# b=5, `vec_stride` b0=97567985197066, `vec_header` exit 139, `vec_datumcol`
# exit 134 — and all 7 of their `_ctl` oracles GREEN, so every fixture bites and every
# `_ctl` proves its numbers independently of the name. Restored, FULL rebuild
# rc 0, all green. Gates on the landed tree: L1 727/727 + gates tier,
# L2 2293/2293 + gates tier, `ctest -R _bc_` 290 tests green,
# `ctest -L fail` 1417 tests exit 0, `logos_09_direct_door_census` green
# (421 s alone), census_pin / spec_path_lint / gate_lint green,
# `scripts/abi-check.sh` rc 0.
#   tests/logos/pass/mlirgen_odr_vec_stride.logos
#   tests/logos/pass/mlirgen_odr_vec_stride_ctl.logos
#   tests/logos/pass/mlirgen_odr_vec_header.logos
#   tests/logos/pass/mlirgen_odr_vec_datumcol.logos
#   tests/logos/pass/mlirgen_odr_tuple_field.logos
#   tests/logos/pass/mlirgen_odr_tuple_field_ctl.logos
#   tests/logos/pass/mlirgen_odr_match_stmt.logos
#   tests/logos/pass/mlirgen_odr_match_stmt_ctl.logos
#   tests/logos/pass/mlirgen_odr_match_expr.logos
#   tests/logos/pass/mlirgen_odr_match_expr_ctl.logos
#   tests/logos/pass/mlirgen_odr_pat_nested.logos
#   tests/logos/pass/mlirgen_odr_pat_nested_ctl.logos
#   tests/logos/pass/mlirgen_odr_pat_refutable.logos
#   tests/logos/pass/mlirgen_odr_pat_refutable_ctl.logos
#   tests/logos/pass/mlirgen_odr_mangle_channels.logos
#   tests/logos/pass/mlirgen_odr_mangle_channels_ctl.logos
# `mangle_channels` is the guard on the type-arg fold's own risk: it walks every
# channel `mangle_type_for_name` feeds with a homonym struct — by-value free fn,
# a generic fn instantiated AT the homonym and called through a fn-POINTER
# parameter, an inherent impl method, a trait impl method, a `Vec<homonym>`
# instance, and a SECOND homonym in the same program — and asserts VALUES, so a
# def/use split shows up as a link error or a wrong number. PER-HUNK CONTROL
# (only src/compiler/sema.cpp reverted, logosc-only rebuild): `mangle_channels`
# rc 1 (`vec=99423547105285`), `vec_stride` rc 1, `vec_header` exit 139,
# `vec_datumcol` exit 134, while `tuple_field` and `match_stmt` stayed GREEN —
# i.e. the sema hunk owns the four generic-instance fixtures and the four
# PatStruct/tuple fixtures are owned by the mlir_gen site conversions, exactly
# as the div=1 fire table says. Restored, FULL rebuild rc 0, all 16 green.
# +16 pass fixtures, so `logos_09_direct_door_census` moves: corpus 2215 ->
# 2231, glob 191 (unmoved), nonglob 2024 -> 2040, doors 36 = 10 + 26 unmoved.
# Re-derived by direct file listing, not by adding 16 to the previous pin.
#
# (3) THE METHOD-RESOLUTION CHANNEL — A CELL THE PARTITION ABOVE DID NOT HAVE,
#     AND ITS ABSENCE IS PART OF THE FINDING. Cells (a)-(f) enumerate lookups of
#     a struct's LAYOUT (`struct_types_` / `all_struct_defs_`). Nothing in them
#     covers the lookup of a struct's METHOD, which runs through entirely
#     different tables and was package-blind in three more places. Two produced
#     RUNTIME SIGSEGVs on programs of 5 and 8 lines with no unsafe, no deem and
#     no slice.
#
#     THE NAMED SITE WAS NOT ONE OF THEM. `resolve_method_symbol`
#     (src/compiler/mlir_gen_impl.hpp) really was bare and first-registered-wins
#     — `for (auto& sd : prog_->structs) if (sd.name() != bare_struct) continue;`
#     — but instrumented (a temporary print at its head, removed again) it fired
#     ZERO times on either repro, and zero times on a program with a plain
#     `impl Drop`. It is not the channel those bites travel. It was converted
#     anyway (below) because the SDrop repair needs an authoritative answer from
#     it, and that use makes it fire and pins it.
#
#     THE SITES, and what each one does now:
#
#     M1 mono_clone.cpp `__typevar_pending__drop` arm — INVENTS the symbol.
#        `drop_fn = concrete_struct_name(ty) + "__drop"`, unconditional: it never
#        asks whether the substituted T HAS a Drop impl, and the name is BARE.
#        NOT CHANGED — mono has no method table at that point (measured:
#        `out_.structs` reports `methods=[]` for both the user's and the stdlib's
#        struct there), so the check cannot be made where the name is invented.
#        Named debt.
#     M2 mlir_gen_expr.cpp `find_func_op` canonical fallback — BINDS it.
#        `ffo_canonical` strips the package off the callee AND every def, so M1's
#        bare `String__drop` bound `logos.mem.string.String__drop__f__String`,
#        which read a user `struct String { a: i64 }`'s i64 as a heap pointer and
#        called free() on it (gdb: `__GI___libc_free (mem=0x5)`). The same
#        fallback bound mono's OPERATOR callee `test.Ident__eq` — mono_clone's
#        BinOp arm rewrites a struct `==` that reaches it into an invented
#        `<pkg>.<Bare>__eq` — to
#        `logos.std.compiler.metaprog.Ident__eq__f__ref_Ident__ref_Ident`.
#        CONVERTED: a PACKAGE GUARD refuses the cross-package canonical bind, but
#        only on evidence that a homonym exists — the callee names a package,
#        that package declares its own struct that OWNS the symbol, and the def
#        lives elsewhere. Every other cross-package canonical bind (the
#        assoc-const accessors, the sig-stripped stdlib intrinsics, any callee
#        whose package declares no such struct) is byte-identical to before.
#        ⚠ The owner test is ANCHORED ON A CARRIED PART — each candidate struct's
#        own name recomposed with `__` and compared as a prefix — not a
#        `find("__")` cut; the separator-split lint is green with the ledger
#        unchanged.
#     M3 mlir_gen_stmt.cpp SDrop — CONSUMES it. M2's guard cannot help here
#        because M1's invented drop symbol carries NO package at all. The var's
#        TypeRef does, so SDrop now asks `resolve_method_symbol` PACKAGE-SCOPED
#        first; when that package's struct exists and has no drop, and the
#        pkg-blind chokepoint would answer with a def from a DIFFERENT package,
#        no call is emitted. Every other answer falls through to the historic
#        path, so a destructor that used to run for a type that has one still
#        runs.
#     M4 resolve_method_symbol itself — QUALIFIED FIRST, BARE LAST, plus one
#        added rule that is the actual fix: when a struct of exactly that
#        package+name EXISTS, its method table is AUTHORITATIVE and the bare pass
#        is closed off (an `owns` out-param reports that to M3). ⚠ The recorded
#        find_struct_repr_ order is preserved — the bare pass still runs for every
#        call that names no package and for every name whose owning package holds
#        no such struct, so no site that resolved before becomes a miss.
#
#     SIBLINGS FOUND WITH THE SAME SHAPE AND NOT CLOSED (grep `prog_->structs`,
#     `strip_struct_pkg`):
#       * mlir_gen_dyn.cpp:1361 — `if (sd.name() != v.tag_system()) continue;`
#         over prog_->structs, then the same scan over prog_->functions. Bare,
#         first-wins. The LIR's `tag_system` is a bare STRING with no package in
#         hand at the site, so it cannot be qualified the way M4 was. Not fired
#         by any repro; named, not converted.
#       * mono_clone.cpp tuple_all_eq element resolve — walks out_.functions /
#         in_.functions for the first symbol CONTAINING `<T>__eq__f__`. Same
#         first-wins shape one layer up. Not reached by either repro.
#       * sema.cpp `drop_fn_for` — already carries a B-mv-02 package guard, but
#         only on the `fn drop(&self)` REF-form loop; the leading
#         `find_func_by_base_and_signature` and the by-value `types_equal` loop
#         have none, and the guard's own "empty package on either side is a
#         wildcard" hatch is unchecked in the abuse direction. Measured to be
#         asked exactly ONCE in the Vec<String> repro (`Vec__drop`), so it is not
#         on the bite path; named.
#
#     STILL OPEN, MEASURED, NOT CLOSED THIS ROUND — and this is why the operator
#     pair is a `fail` pair and not the `pass` pair that was asked for:
#       * SEMA METHOD RESOLUTION IS PACKAGE-BLIND IN THE OVER-REFUSAL DIRECTION.
#         Give the user's `Ident` its OWN `impl Eq`, and the homonym program
#         stops compiling: "[fn Ident__ne]: method call: 'Ident' has no method
#         'eq'" — the trait's DEFAULT `ne` body resolves `self.eq(..)` against the
#         wrong `Ident`. The collision-free twin compiles and prints the right
#         values. CONTROL: identical with all three of this round's hunks
#         disabled, so it is PRE-EXISTING and independent.
#       * THE COPY VERDICT IS TAKEN FROM THE HOMONYM TOO, and is NOT closed.
#         Measured: `Vec<String>::get` on a user `struct String { a: i64 }` is
#         refused with "element type `String` is not `Copy`" while the
#         collision-free twin IS Copy and compiles. The cause is NOT this
#         round's channel — `copy_types_` is a set of BARE names by construction
#         (sema.cpp, the comment says so), and the fixpoint that fills it skips
#         any struct for which `impls_.count("Drop::" + bare)` holds, where
#         `impls_` is bare-keyed BY A CARRIED DESIGN DECISION (sema_collect.cpp
#         documents ~50 probes that compose a bare stdlib trait name and are
#         correct only because that trait owns the bare slot). Closing it means
#         making the impl TARGET key package-qualified, which is a different and
#         much wider change than the four sites above; doing it inside this round
#         would have been a sweep, not a fix. Named, with the measurement, as the
#         next step of this class.
#       * THE QUOTE CHANNEL HAS THE SAME DEFECT. `println!` expands to
#         `let __buf: String`, so a package that declares its own `String` gets
#         "let '__buf': type mismatch — expected test.String, got
#         logos.mem.string.String" plus three follow-ons. That is why
#         mlirgen_odr_drop_glue_homonym asserts an EXIT CODE and not a printed
#         line.
#
#     THE `__f__`/`__g__` SPELLING INCONSISTENCY IS NOT CLOSED BY ANY OF THIS,
#     and the reason is structural: those 13 bare symbols
#     (the archive's `<mod>.<pkg>$println__f__Vec$G1$ExprBlob` spelling beside
#     `…Vec$G1$ExprBlob$M2b09…__drop__g__Vec$G1$T`) are ARCHIVE-DEFINED — minted
#     when the stdlib was compiled, where `ExprBlob` was unambiguous and the fold
#     did not apply. Nothing decided in a USER compile can rename a symbol that
#     is already in a `.a`. The same fact has an ABI-clean consequence worth one
#     sentence: logos.std.compiler.metaprog has no owning module_id, so in a user
#     compile its `ExprBlob` folds and the object RE-EMITS
#     `Vec$G1$ExprBlob$M2b09…__drop/__index/__borrow/__into_iter` locally instead
#     of binding the archive's bare symbols — two definitions of one logical
#     instance across the archive/object boundary, resolved by
#     `-Wl,--allow-multiple-definition`, identical in content.
#
#     FIXTURES (all four bite-proven by per-channel kill-switches compiled in,
#     measured, then removed and the tree rebuilt green):
#       tests/logos/pass/mlirgen_odr_drop_glue_homonym.logos
#       tests/logos/pass/mlirgen_odr_drop_glue_homonym_ctl.logos
#       tests/logos/fail/mlirgen_odr_operator_homonym.logos
#       tests/logos/fail/mlirgen_odr_operator_homonym_ctl.logos
# 2026-08-20 (later, #86 VERIFY ROUND 2), +8/+8/0 (7366/3683/36 ->
# 7374/3691/36), for the 8 fixtures of MISS-A/B/C/D (4 fail + 4 admit twins):
#   tests/logos/fail/bc_esc_holder_reborrow_field_dangle.logos
#   tests/logos/fail/bc_esc_holder_reborrow_container_dangle.logos
#   tests/logos/fail/bc_esc_holder_index_assign_dangle.logos
#   tests/logos/fail/bc_esc_holder_residency_pershare_dangle.logos
#   tests/logos/pass/bc_esc_holder_reborrow_field_admit.logos
#   tests/logos/pass/bc_esc_holder_reborrow_container_admit.logos
#   tests/logos/pass/bc_esc_holder_index_assign_admit.logos
#   tests/logos/pass/bc_esc_holder_residency_pershare_admit.logos
# ⚠ THE PREDICTION WAS WRONG AND IS RECORDED WRONG. It read "+16/+16" —
# arrived at by reading the PREVIOUS row's "+16/+16 for eight pass fixtures"
# and inferring two registrations per fixture. The previous row added SIXTEEN
# fixtures (eight pass + eight fail) and its own text names only the pass half,
# so the rate is ONE registration per fixture, not two. Measured after the
# reconfigure: 7374/3691/36 — the ARITHMETIC was off, the direction and the
# fixture list were not. The rule this row states for the next reader: derive
# the delta from `ls` on BOTH directories, never from a previous delta.
# MISS-E adds NO fixture: it corrects the diagnostic STRING of two existing
# ones, and its pins are their `.expected` files
# (fail/bc_esc_holder_return_chained_dangle: '__ret_tmp_0' -> 'o';
#  fail/wany_escapes_rc_container: '__ret_tmp_0' -> 'e').
#
# RED LIST PER HOLE, measured between the holes, each on its own full 53-target
# rebuild (`ninja -k 0` from build/, rc stated) plus a compile-only sweep of the
# WHOLE corpus — 2211 `tests/logos/pass/*.logos` + 754 `tests/logos/fail/*.logos`
# = 2965 files, rc recorded per file (the sweep's own instrument artefacts: 50
# pass + 32 fail fixtures answer rc=4, "needs -I", and 3 fail fixtures compile
# clean — all three pre-existing and none of them borrow-check rows):
#   MISS-A   build rc 0 · sweep: 2161 pass rc0 / 0 pass rc1 · 719 fail rc1
#   MISS-B/C build rc 0 · sweep byte-identical to MISS-A's
#   MISS-D   build rc 0 · sweep byte-identical
#   MISS-E   build rc 0 · sweep byte-identical (message-only change)
#
# ⚠ ONE MEASURED REFUSAL, AND IT IS WHY MISS-B/C IS NARROWER THAN ITS OBVIOUS
# SHAPE. The first MISS-B/C attempt taught the SHARED walker `place_write_root`
# the two borrow-forming steps (AddrOf / AddrOfTemp) — the right shape, since
# `flow_operand_root` and `apply_call_outparam_rules` both pre-peel exactly
# those kinds before calling it. The 53-target build then FAILED: 4 stdlib
# E0597 over-refusals in stdlib/mem/wql/lower.logos (`lower_aggr`: 'rg'
# borrowed by 'ra'; `gp_build` x2: 'js0'/'js' borrowed by 'starr';
# `gp_desugar_query`: 'orig2' borrowed by 'sa'), ALL FOUR FALSE — each stores a
# WRef HANDLE into an arena container in the same `h: &Writ`, so nothing dies
# at the loop-body scope the diagnostic names. `logos-mem` failed, so every
# package downstream of it was never measured at all. The widening was
# therefore applied to the ESCAPE record only; the uncovered count it buys is
# on the bounding list (the LOAN channel stays blind to 3115 of 3116 AddrOfTemp
# `&mut self` receivers over the first 300 pass fixtures).
#
# ── THE BOUNDING LIST: HOLDER-ESCAPE SPELLINGS THIS ROUND DID *NOT* CLOSE ────
# Eight consecutive rounds have shipped a fix whose own report missed a hole
# one site over. These were PROBED on the fixed tree, with the rc measured, so
# the next reader inherits a list instead of a surprise. Scratch repros:
# /home/logos/sandbox/miss86/probe/*.logos.
#
# B1  WHOLE-VALUE WRITE THROUGH A REBORROW LOCAL — `*r = W { v: o.as_str() };`
#     with `let r: &mut W = &mut w;`, then `return w.v`.  rc 0, NO DOOR FIRES
#     AT ALL (LOGOS_86_TRACE silent). It is not `Code::Assign` (the place is
#     `*r`, not a name) and the DerefWrite §B6/holder block excludes it by its
#     own guard `atv.inner().kind() != EC::VarRef`. Repro: probe/P1.logos.
#     Closest sibling that DOES refuse: fail/bc_esc_holder_reborrow_field_dangle
#     (`r.v = o.as_str()`), one character of place syntax away.
#
# B2  CONTAINER REACHED THROUGH A REBORROW OF A STRUCT FIELD —
#     `let r: &mut Vec<str> = &mut k.v; r.push(o.as_str()); return k;`  rc 0.
#     The ROOT resolves correctly (`r` -> `k.v` -> `k`, and the return gate
#     DOES open: `[#86trace-gate] fn=fn bad`), and the deposit is then lost on
#     the ELEMENT TYPE: the receiver expression's type is a `MutRef` whose
#     `elem()` measures NULL, and the `holder_ty_of(rn86)` fallback answers
#     `K` — a struct with no type-args — so `el86` is empty and `at == el`
#     never matches. Repro: probe/P7.logos. This is the one residual of the
#     MISS-B fix that is NOT a missing shape but a missing container TYPE at a
#     dotted holder.
#
# B3  TWO SHARES IN DIFFERENT FRAMES — MISS-D's rule counts share handles among
#     the bindings THIS function declares, so `fn bad(other: &mut Rc<Writ>) {
#     let mut h = writ_rc(64); let e = mk(&mut h); return hold_any(other, e); }`
#     has ONE local share and admits. rc 0. Repro: probe/P12.logos. Closing it
#     needs the real `e ∈ h` provenance edge, i.e. task #81 (summaries for
#     prebuilt stdlib methods), not a wider frame count.
#
# B4  ZERO-SHARE EXEMPT FRAMES — `residency_exemption_holds` still returns YES
#     when the escaping expression reaches NO share handle at all (every local
#     is `Held`/`WAny`-typed and no `Rc` is named). That is today's answer and
#     was left unchanged deliberately: tightening it is unmeasured.
#
# B5  THE LOAN CHANNEL'S OWN AddrOfTemp BLINDNESS — the MISS-B/C widening was
#     confined to the ESCAPE record after the shared-walker version produced 4
#     false stdlib E0597s (above). `place_write_root` still answers "" for an
#     AddrOf / AddrOfTemp receiver, so `add_ref_sources` and D1 door 8b remain
#     blind on 3115 of the 3116 AddrOfTemp `&mut self` receivers measured over
#     the first 300 pass fixtures. Closing it requires the arena-handle
#     question first (a `WRef` into `h: &Writ` is not frame storage).
#
# B6  `'?'` IN THE CLOSURE DIAGNOSTIC — MISS-E removed every `__ret_tmp_N` from
#     the corpus (measured: 0 of 2965 fixtures now print one), but FOUR fixtures
#     still print `local variable '?'`:
#       tests/logos/fail/bc_d1r3_f4_closure_local.logos
#       tests/logos/fail/bc_d1r4_n2_bare_closure_plain_ref_held.logos
#       tests/logos/fail/bc_d1r4_n2_bare_closure_return_held.logos
#       tests/logos/fail/bc_d1r4_n3_closure_struct_field_held.logos
#     `'?'` is a placeholder, not a fabricated name, and their `.expected`
#     files stop at "cannot return reference to local variable" — so nothing
#     pins the placeholder either way. Separate item.
#
# B7  NOT PROBED, so NOT CLAIMED EITHER WAY: a store into a `static` / module
#     const; a store into a captured variable from inside a closure body; the
#     `HashMap::insert(k, borrow)` two-arg container shape; array-element
#     assign (probe/P5 could not be measured — `[str; 2]` by-value return hits
#     an UNRELATED pre-existing mlir-gen defect, `llvm.getelementptr` operand
#     type on `!llvm.array<2 x struct<(ptr,i64)>>`); and the nested
#     `v.get_mut(0).v = …` shape (probe/P3: sema declines the deep assignment
#     before the borrow checker ever sees it).
# The earlier round's note: #86 VERIFY — THE THREE HOLES THE #86 LANDING'S
# OWN VERIFY FOUND, ONE LINE OVER. The landing was sound and narrow; it was
# also the SEVENTH consecutive round whose verify found a hole beside the fix,
# so the abuse direction was the primary task here, not the epilogue. Three
# holes, closed ONE AT A TIME, each with its own full 53-target rebuild and its
# own control-revert chain.
#
# MISS 1 — THE MUTATION AFTER THE LET (runtime-confirmed UAF, one line from the
#   landing's own fixture tests/logos/fail/bc_esc_holder_return_field_dangle.logos):
#     let mut w: W = W { v: "" };  w.v = o.as_str();  return w.v;      // rc 0
#   #86 hunk (B) wrote `prov_` ONLY at the `let` INITIALIZER. Every later store
#   into the holder left it empty, so the gate — which DID open
#   (LOGOS_DUMP_RETGATE: mcb=1, and the loan channel even named the source,
#   srcs=[o,]) — had no provenance to refuse on. Whole-value reassign, `Option`
#   reassign, tuple-element assign and the container deposit were all rc 0.
#   THE FIX is ONE helper, `note_holder_escape_prov`, at FOUR doors. It records
#   what hunk (B) records and no more: the ESCAPE FACT only (is_local/is_temp,
#   never `params`), so it cannot start check_return_value's elision arm on a
#   binding that never fed it. ADDITIVE (OR-ed in), because a field write
#   touches ONE field of a holder whose siblings may still carry an earlier
#   borrow; the alternative, clearing on every store, LOSES the sibling borrow,
#   which is the permissive direction and the direction this hole is in.
#   PARAMS ARE SKIPPED DELIBERATELY: writing a local borrow through a `&mut`
#   PARAMETER and returning it is the FRAME escape, task #78, still open.
#   ⚠ THE TWO DEAD DOORS, MEASURED, NOT ASSUMED. The first draft hooked
#   Code::FieldWrite and Code::TupleWrite — the arms whose NAMES match the
#   spelling. A print in both fired ZERO times over all 2211 pass + 1413 fail
#   fixtures AND the whole 53-target build: sema lowers `w.v = …` and `t.0 = …`
#   to SDerefWrite(AddrOfTemp(FieldRead/TupleIndex(VarRef w))), which this
#   file's own §2-Wave-9 comment already said. Both hooks were DELETED and the
#   instrument that proved it is kept as LOGOS_DUMP_BC_PLACEWRITE_DOOR, so the
#   claim can be re-checked instead of believed.
#   `holder_ty_` is new state: the mutation doors know only the receiver's
#   NAME and VarState carries no type. Recorded at the `let`, cleared per
#   FUNCTION beside `prov_`, and NOT part of the branch save/restore (a
#   binding's type does not change across a branch).
#   CONTROL CHAIN (each door disabled alone, logosc-only rebuild, the other
#   left in — the two hit DISJOINT fixtures):
#     DerefWrite door off -> assign_field rc 1, assign_tuple rc 1 (RED),
#                            assign_whole rc 0, assign_option rc 0 (green)
#     Assign door off     -> assign_whole rc 1, assign_option rc 1 (RED),
#                            assign_field rc 0, assign_tuple rc 0 (green)
#     restored            -> all four rc 0 (green checkpoint re-measured)
#   RED LIST ON A FULL 53-TARGET REBUILD: EMPTY (rc 0, 0 FAILED, 0 logos
#   errors). L1 726/726, L2 2256/2256 at that stage.
#
# MISS 2 — THE RESIDENCY EXEMPTION WAS UNCHECKED IN THE ABUSE DIRECTION
#   (runtime-confirmed UAF). `type_is_residency_exempt` is a NAME test over
#   `ts_.residency_exempt`, auto-populated for ANY struct with a field whose
#   package-stripped name is Rc/Arc, and registered under the BARE name too. So
#     pub struct E { pub h: Rc<i64>, pub v: str }
#   switched BOTH #86 hunks off wholesale: returning `E`, or `e.v`, with `v`
#   borrowing a fn-local String was rc 0. The Rc share keeps a DIFFERENT
#   allocation alive and says nothing about `v`. Also reachable by BARE-NAME
#   COLLISION: a user struct named `Held` in any package matched.
#   ⚠ THE LANDING'S CTRL-D PROVED THE EXEMPTION NECESSARY AND NOTHING ELSE.
#   That is the repo rule `feedback_gate_exemption_checked_in_abuse_direction`
#   exactly: an unchecked hatch is worse than no gate, because the green now
#   vouches for it.
#   WHAT SEPARATES THE REAL USER FROM THE ABUSE, MEASURED rather than argued.
#   With the exemption forced off, examples/writ_container_showcase.logos
#   `make_held_doc` reds naming `h` — and `h: Rc<Writ>` IS the share
#   ([retgate] fn=fn make_held_doc line=91 … srcs=[h,]). The abuse reds naming
#   `o: String`, which no share covers. So the exemption's real claim is not
#   "this TYPE is exempt" but "the borrow that escapes is kept alive by the
#   share this value carries".
#   THE FIX, and it is the narrowing the task named "exempt only the
#   residency-carrying part" reduced to a checkable form:
#   `residency_exemption_holds(t, e)` = the old name test AND every local the
#   escaping expression names is itself residency-backed
#   (`type_is_residency_backed`: Rc/Arc, anything already in
#   `residency_exempt`, or one type-arg hop to either). Absence of a recorded
#   type answers NO — the refusing direction, which is the direction this hole
#   is in. THE HATCH KEEPS ITS REAL USER: the admit twin IS make_held_doc.
#   CONTROL CHAIN:
#     CTRL-2  blanket exemption restored -> abuse fixture rc 1 (RED),
#             pass/bc_esc_holder_residency_backed_admit rc 0 (still green)
#     CTRL-2B exemption removed entirely -> the admit fixture AND
#             examples/writ_container_showcase.logos both red with
#             "cannot return reference to local variable 'h'" (the hatch is
#             NECESSARY, re-measured on the new fixture, not inherited)
#     restored -> abuse rc 0, backed rc 0
#   ⚠ NOT TOUCHED, and named rather than implied: the REGISTRATION-level
#   application of the same set (`is_borrow_carrying_type`, the
#   `residency_exempt.count(nm)` early-out) is unchanged. Its blast radius is
#   the whole transitive borrow-carrying fixpoint, not the #86 escape gate, and
#   it belongs to its own round. 1 site left open, named.
#   ONE PRE-EXISTING PASS FIXTURE NOW DENIES THE EXEMPTION and still admits:
#   tests/logos/pass/bc_d1_residency_exempt_return_admits.logos, `fn main`,
#   src=`c` (a local `C`, not residency-backed). It stays green because the
#   escape fact there is empty, not because the exemption saved it. If that
#   ever changes, that fixture is the canary and it is already in the corpus.
#   RED LIST ON A FULL 53-TARGET REBUILD: EMPTY. L1 726/726, L2 2266/2266.
#
# MISS 3 — CONTAINER HOLDERS. `Vec<str>` and `Vec<H>` built from a fn-local
#   borrow and returned admitted at rc 0; the #86 fixture set contained no
#   container holder at all. Same root as MISS 1 (the borrow enters by a
#   MUTATION), and it needs TWO doors, which a control proved are BOTH
#   load-bearing and NEITHER redundant:
#     (i)  apply_flow_outparams — the callee SUMMARY door (`out0<-0x2`), the
#          only one that covers a FREE FUNCTION `stash(&mut v, H { … })`.
#          Hooked BEFORE the existing loan filter and with its own gate: that
#          filter's three predicates are the LOAN channel's, and widening THEM
#          would widen inherit_loans / take_ref_borrows / the A2 alias edges in
#          one move — three rules in one control.
#     (ii) the `&mut self` RECEIVER arm — the only one that covers
#          `v.push(o.as_str())` on a `Vec<str>`, because `Vec<str>::push` comes
#          PREBUILT from the stdlib archive and has NO summary at all (fs=0,
#          measured — that is task #81 showing through). It needs none: it
#          reads the receiver's own ELEMENT TYPES. `stored_ref_elem` is not
#          reused there because it requires `is_ref_kind(at)`, which is exactly
#          what made the `Vec<H>` spelling deposit NOTHING even in the §B6
#          channel (srcs=[] measured, against srcs=[o,] for the `Vec<str>`
#          twin). The element-type match is what discriminates a STORE from a
#          read (`contains(&&T)`: type != element); the ref-ness never was.
#   CONTROL CHAIN (each door disabled alone, the other left in):
#     receiver arm off  -> vec_str rc 0 (RED), vec_struct rc 1, outparam rc 1
#     out-param arm off -> outparam rc 0 (RED), vec_str rc 1, vec_struct rc 1
#   — so each door has a fixture that fires it ALONE, which is why the
#   out-param pair exists separately from the two Vec pairs.
#   THE DIAGNOSTIC HAD TO MOVE WITH IT. The `Vec<H>` spelling first refused
#   with "cannot return reference to local variable '__ret_tmp_0'" — a name in
#   no source file. #77 round 2's repair chases `ref_sources_under(temp)` and
#   found nothing, because the §B6 channel never recorded a source for a
#   by-VALUE element. `note_holder_escape_prov` now deposits the §B6 source
#   too, through `store_ref_sources` (ADDITIVE — `add_ref_sources` would
#   `erase_ref_sources_under` the place first and lose an earlier push's
#   source, the permissive direction). Not a wider claim: it fires only where
#   the escape fact is already local/temp, i.e. the two channels are answering
#   the same question, one of them just where the diagnostic can read it.
#   RED LIST ON A FULL 53-TARGET REBUILD: EMPTY, on both of this hole's two
#   rebuilds (the doors, then the §B6 deposit).
#
# FIRE COUNTS, WITH THE INSTRUMENT STILL IN PLACE WHEN QUOTED
#   (LOGOS_86_TRACE over 2211 pass + 1413 fail fixtures + examples, one output
#   file per fixture so nothing interleaves):
#     gate 1 043 522 · let 606 · outparam 140 (107 files) · assign 47 (41
#     files) · recvstore 43 (25 files) · derefwrite 13 (13 files) · carry 5 ·
#     exempt-denied 2 (2 files)
#   IN-TREE, over a forced full 53-target rebuild (87 logosc processes,
#   LOGOS_DUMP_BC_HOLDERPROV, which now prints per door):
#     fired=36 · assign=18 · outparam=18 · derefwrite=0 · recvstore=0
#   So two of the four doors fire only in the CORPUS, not in the tree — and
#   they are pinned by fixtures that FIRE them (13 and 25 files respectively,
#   including the new refuse halves), which is the standard this repo applies
#   to a zero-firing arm. The place-write dead-door instrument reads 0 in both
#   populations, which is why those two hooks are gone rather than kept.
#
# THE SIXTEEN (8 fail + 8 pass). Every refuse has an admit twin whose ONE
# VARIABLE is where the carried borrow is ROOTED — a fn-LOCAL in the refuse
# half, a PARAMETER in the admit half — except the residency pair, whose one
# variable is whether the escaping borrow is rooted at the RESIDENCY HOLDER
# itself. Logos has no lifetime parameters, so the admit half IS the elision
# model and a red there is the over-refusal, not progress:
#   tests/logos/fail/bc_esc_holder_assign_field_dangle.logos
#   tests/logos/fail/bc_esc_holder_assign_whole_dangle.logos
#   tests/logos/fail/bc_esc_holder_assign_option_dangle.logos
#   tests/logos/fail/bc_esc_holder_assign_tuple_dangle.logos
#   tests/logos/fail/bc_esc_holder_residency_abuse_dangle.logos
#   tests/logos/fail/bc_esc_holder_container_vec_str_dangle.logos
#   tests/logos/fail/bc_esc_holder_container_vec_struct_dangle.logos
#   tests/logos/fail/bc_esc_holder_container_outparam_dangle.logos
#   tests/logos/pass/bc_esc_holder_assign_field_admit.logos
#   tests/logos/pass/bc_esc_holder_assign_whole_admit.logos
#   tests/logos/pass/bc_esc_holder_assign_option_admit.logos
#   tests/logos/pass/bc_esc_holder_assign_tuple_admit.logos
#   tests/logos/pass/bc_esc_holder_residency_backed_admit.logos
#   tests/logos/pass/bc_esc_holder_container_vec_str_admit.logos
#   tests/logos/pass/bc_esc_holder_container_vec_struct_admit.logos
#   tests/logos/pass/bc_esc_holder_container_outparam_admit.logos
# CANARIES RE-MEASURED GREEN, not assumed: examples/writ_container_showcase.logos
# (make_held_doc), tests/logos/pass/bc_argcomp_tvbuild_byvalue_fat_admit.logos,
# all 33 `bc_esc_holder_*` / `bc_fatret_*` / `bc_d1r3_*` tests
# (`ctest -R bc_esc_holder` 33/33, `ctest -R _bc_` 282/282,
# `ctest -L fail` 1413/1413).
# GATES: L1 726/726 + gates tier 36/36, L2 2272/2272 + gates tier 36/36, and
# all three tier_full sweep gates run SERIALLY and green
# (logos_09_plan_ground_census, logos_09_pull_shape,
# logos_09_direct_door_census). Door census re-derived BY DIRECT FILE LISTING:
# 2211 = 191 + 2020, doors unmoved at 36 = 10 + 26.
# UNCOVERED, COUNTED AND NAMED rather than implied:
#   (1) THE FRAME ESCAPE THROUGH A `&mut` PARAMETER IS STILL OPEN — task #78,
#       and this round deliberately did not touch it: `note_holder_escape_prov`
#       skips params by construction, because marking a parameter `is_local`
#       would refuse every later return of that parameter, not just the stored
#       borrow. 1 named door.
#   (2) THE REGISTRATION-LEVEL RESIDENCY EXEMPTION IS UNNARROWED — the
#       `residency_exempt.count(nm)` early-out inside `is_borrow_carrying_type`.
#       MISS 2 narrowed the four ESCAPE-GATE applications only. 1 named site.
#   (3) #81 SHOWED THROUGH AND IS NOT CLOSED: `Vec<str>::push` has no summary
#       because it is prebuilt (fs=0, measured here for the first time). MISS 3
#       routes AROUND it via the receiver arm rather than through it, so a
#       summary-less callee that is NOT a `&mut self` method and NOT a
#       container store still deposits nothing. Not measured how many such
#       shapes exist; stated as unproven rather than as a finding.
#   (4) THE ADDITIVE FIELD-WRITE RULE OVER-REFUSES ONE SHAPE BY DESIGN:
#       `let w = W{v:o.as_str()}; w.v = "static"; return w.v;` is refused
#       although the local borrow was overwritten. Priced deliberately (see
#       MISS 1); 0 occurrences in the corpus or the tree — the full rebuild and
#       L1/L2 are the measurement.
# 2026-08-20, +17/+17/0 (7333/3650/36 -> 7350/3667/36), predicted before the
# reconfigure and met exactly. #86 — A VALUE THAT HOLDS A BORROW ESCAPED BY
# RETURN, UNCHECKED. Runtime-confirmed use-after-free in four lines, no
# generics and no call:
#   `pub struct W { pub v: str }`
#   `pub fn bad() -> str { let o = String::from("hello");`
#   `                     let w: W = W { v: o.as_str() }; return w.v; }`  rc 0
#   THE SHAPE OF THE QUESTION. `check_return_value`'s gate asked "is this a
#   REFERENCE" (`is_ref_kind || is_borrow_carrying_type`, plus a retention arm
#   for the erased kinds `type_hides_borrow` names). It must ask "does this
#   value HOLD a borrow". #71 had already built that predicate —
#   `holds_any_ref`, read by `type_may_carry_borrow` — and it answered 1 for
#   every one of the nine measured spellings the whole time. THE HYPOTHESIS IN
#   THE TICKET WAS CONFIRMED BY MEASUREMENT, not assumed: instrumenting the
#   gate (`LOGOS_DUMP_RETGATE`, kept) split the nine into THREE groups, not
#   two, which is what named the third sub-site.
#   THREE SUB-SITES, THREE HUNKS, EACH SEPARATELY CONTROL-REVERTED:
#     (A) THE GATE. `-> W`, `-> (str,i64)`, `-> Option<str>` are none of the
#         three admitted kinds, so the gate never opened at all. New
#         `holds_gate` term + the F4 `prov_of_retained` fallback extended to
#         it. CONTROL `holds_gate := false`, logosc-only rebuild: struct /
#         tuple / option spellings back to rc 0, the other six keep rc 1.
#     (B) THE LET. `prov_[name]` was recorded only for a ref-kind or
#         `#[borrow_carrying]` binding, so `let w: W = W{v:o.as_str()}` left
#         `prov_` empty and the return gate — which for `-> str` DID open —
#         found nothing to say. A third branch records the ESCAPE FACT ONLY
#         (`is_local`/`is_temp`, never `params`), so the elision arm of
#         `check_return_value` cannot start firing on bindings that never fed
#         it. This arm also owns the value-returning spellings, because sema's
#         `make_return_with_drops` rewrites their returns through a
#         `let __ret_tmp_0` of the value type. CONTROL `:= false`: ALL EIGHT
#         gate-closable spellings back to rc 0.
#     (C) THE BORROW THE RECEIVER *CARRIES*. `mk(o.as_str()).get()` and
#         `let w = W{v:o.as_str()}; return w.get();` still admitted after (A)
#         and (B). NOT a generics defect and not a chained-call defect — the
#         plain bound spelling admits identically (measured). The callee
#         summary `W::get(&self) -> str` is `result<-0 EXACT` and that is
#         CORRECT: it is `stored_shared_extract`, the Rust-parity rule that a
#         `str` copied out of a `&W` has the FIELD's lifetime. So
#         `recv_contributes` is false and prov_of's MethodCall arm merged
#         nothing — nobody asked where the FIELD's borrow came from. New
#         clause merges the receiver's CARRIED escape fact. CONTROL
#         `if (false && ...)`: chained + bound spellings back to rc 0, the
#         other six keep rc 1. The three controls hit DISJOINT fixtures.
#   ⚠ THE CARRY CLAUSE MUST NOT READ `prov_of(receiver)`, and the first draft
#   did. A `&self` call spells its receiver `&w`, and prov_of's AddrOf arm
#   answers `is_local` for ANY local — that is the provenance of a borrow OF
#   `w`, not of the borrow `w` HOLDS, i.e. exactly the F2 over-refusal the
#   `recv_contributes` guard exists to prevent. MEASURED: it refused
#   `fn ok(s: str) -> str { let w = W{v:s}; return w.get(); }` (rc 1, "cannot
#   return reference to local variable 'w'") — THE ELISION CASE, which must
#   compile. `carried_prov_of_recv` reads the provenance recorded FOR THE
#   VALUE instead. pass/bc_esc_holder_return_chained_admit pins that mistake.
#   ⚠ `type_is_residency_exempt` SHIPS WITH THE GATE OR THE HATCH REDS.
#   `type_may_carry_borrow` routes through `loan_carrying_type`, which by
#   design does NOT apply `ts_.residency_exempt` (it feeds the LOAN channel);
#   the #86 gate is an ESCAPE gate, so it must apply it itself. CONTROL
#   (`return false` at the head of the helper, logosc rebuild + the one
#   target): examples/writ_container_showcase.logos `make_held_doc` fails,
#   "cannot return reference to local variable 'h'" — `return hold_any(&mut h,
#   e);` is the hatch's whole PURPOSE (`c4_exempt_return`).
#   RED LIST ON A FULL TREE REBUILD (51 logos targets: stdlib lang/mem/lcm/std,
#   lforge, peg_gen_logos, memoria-ctr/-store, ub_boundary, ctr_mod, examples):
#   EMPTY, on three separate full rebuilds (rc 0, 0 FAILED, 0 logos errors).
#   THE RED LIST THAT MATTERS IS THE REJECTED DESIGN'S. A first version used
#   the flat `collect_ref_sources` as the verdict channel; it stopped ninja at
#   `liblogos-mem.a` (target 2 of 51) and, measured under a soft mask, came out
#   at 106 hits / 41 unique sites, 0 REAL, 41 FALSE — 25 deem-emitted plan fns
#   (`ss_grp(&__pl, a)`: a borrow of a local the result never touches, which
#   the summary-aware `prov_of_retained` gets right and a flat source collector
#   cannot), 7 arena-backed WAny locals rooted at a param, 3 interning through
#   a scratch arena, and the residency hatch. That mask (`LOGOS_86_SOFT`) is
#   NOT in the landed patch: an env var that disables a safety check is the
#   un-abuse-checked hatch this repo has a rule about. `LOGOS_DUMP_RETGATE` and
#   `LOGOS_86_TRACE` are print-only and kept.
#   BOTH WIDE ARMS MEASURED FIRING IN-TREE, not argued (`LOGOS_86_TRACE` over a
#   full 51-target rebuild): `holds_gate` opens 49 387 times, the Let record
#   fires 1 226 times (12 distinct sites, all `loc=1 tmp=0`, e.g.
#   `ByteSplitter__reduce:449 first`, `format_args_str:120 f`,
#   `walk_program_params:1538 pick_ap`), the carry clause fires 2 times
#   (`writ_map_comp_new`, `writ_list_comp_new`). None is dead code, and the
#   first two counts are IDENTICAL before and after (C) — which is the evidence
#   that the third hunk did not perturb the first two.
#   THE SEVENTEEN (8 fail + 9 pass). Every refuse has an admit twin whose ONE
#   VARIABLE is where the carried borrow is ROOTED — a fn-LOCAL in the refuse
#   half, a PARAMETER in the admit half. Logos has no lifetime parameters, so
#   the admit half IS the elision model and a red there is the over-refusal,
#   not progress:
#     tests/logos/fail/bc_esc_holder_return_field_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_struct_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_tuple_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_option_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_chained_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_method_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_dyn_dangle.logos
#     tests/logos/fail/bc_esc_holder_return_generic_dangle.logos
#     tests/logos/pass/bc_esc_holder_return_field_admit.logos
#     tests/logos/pass/bc_esc_holder_return_struct_admit.logos
#     tests/logos/pass/bc_esc_holder_return_tuple_admit.logos
#     tests/logos/pass/bc_esc_holder_return_option_admit.logos
#     tests/logos/pass/bc_esc_holder_return_chained_admit.logos
#     tests/logos/pass/bc_esc_holder_return_method_admit.logos
#     tests/logos/pass/bc_esc_holder_return_dyn_admit.logos
#     tests/logos/pass/bc_esc_holder_return_generic_admit.logos
#     tests/logos/pass/bc_esc_holder_outparam_param_borrow_admit.logos
#   BITE-PROVEN, not asserted: with `src/compiler/borrow_check.cpp` restored to
#   7b72b89c and logosc relinked, ALL EIGHT fail fixtures go rc 1 through
#   `run_test.sh` (i.e. the compiler admits them) and ALL NINE pass fixtures
#   stay rc 0 — so the admit half is a genuine permissive guard and not
#   something this change made green.
#   UNCOVERED, COUNTED AND NAMED rather than implied:
#     (1) THE §B6 LOAN CHANNEL HAS NO CHAINED-CALL ARM. `collect_ref_sources`
#         deposits nothing for `mk(o.as_str()).get()`, so the diagnostic for
#         fail/bc_esc_holder_return_chained_dangle names the compiler temp
#         `__ret_tmp_0` instead of `o`. The #77-round repair that normally
#         recovers the user name asks that same channel. The string is PINNED
#         in the `.expected` so closing it is a visible edit. 1 of 17.
#     (2) #78 DOES NOT CLOSE WITH THIS, measured both ways: a callee-local
#         borrow stored through the caller's `&mut` out-param admits at rc 0
#         before AND after, and so does its DIRECT control (`out: &mut str`,
#         `*out = o.as_str()`). A different channel — the STORE side, not the
#         return gate. pass/bc_esc_holder_outparam_param_borrow_admit is the
#         permissive guard the eventual #78 repair has to keep green.
#     (3) #81 IS NOT CLOSED AND WAS NOT MEASURED HERE. Reasoning from the code
#         only: `prov_of_retained`'s Call arm falls back to `each_arg(one)`
#         when `flow_of_call` misses, which is the pre-F4 CONSERVATIVE rule, so
#         an unavailable foreign summary makes this channel more refusing, not
#         less. Stated as unproven rather than as a finding.
# 2026-08-20, +7/+7/0 (7326/3643/36 -> 7333/3650/36), predicted before the
# reconfigure and met exactly. #83 — A METHOD CALL ON A GENERIC RECEIVER WAS
# SUMMARY-BLIND, AND THE TICKET'S STATED CAUSE WAS REFUTED BY MEASUREMENT.
#   THE ROOT, instrumented. `resolve_method_flow` narrowing to zero candidates
#   for a TypeVar receiver was NOT it: that function is never entered on the
#   repro, because POST-mono the node is not a MethodCall at all. Mono
#   devirtualises a trait call on a TypeVar receiver into a `Code::Call` whose
#   callee it writes as its OWN worklist key (`K__thru`), while the function is
#   emitted as `<pkg>.K__thru__f__ref_K__slice_u8`. Summaries are keyed by the
#   LIR name ⇒ `flow_of_call` missed. A LOOKUP KEY IS NOT AN IDENTITY, again.
#   And PRE-mono, the generic-template pass was handed `flows == {}` outright,
#   so a generic fn with NO call site — checked only in that pass — was blind
#   in its whole body.
#   TWO ARMS, TWO REPAIRS, EACH SEPARATELY PINNED BY A FIXTURE THAT FIRES IT:
#     (1) `resolve_call_flow` gains a mono-key fallback via `FnIndex::by_bare`
#         (`bare_fn_name` of every function name), narrowed to symbols with NO
#         `__f__`/`__g__` signature tail — a fully mangled miss is a genuinely
#         unknown callee (residuals (a)/(e), #81's seam) and stays a miss.
#         Ambiguity ⇒ agree() ⇒ nullptr, never a guess.
#         PINNED BY fail/bc_esc_generic_monokey_dangle — two impls with
#         DIFFERENT summaries, so the pre-mono agree() cannot decide and only
#         the post-mono key resolution can.
#     (2) summaries are computed and consumed in the PRE-mono pass too.
#         PINNED BY fail/bc_esc_generic_uninst_dangle — no call site at all.
#   THE FIX IS PRECISE, NOT CONSERVATIVE, and that is the reason it was worth
#   the extra round. The measure-only round costed the documented (d) route
#   (unresolvable receiver ⇒ retains every ref-kind argument): green on this
#   tree, but it refuses `fn f<T: Tr>(t: &T) -> str { let o = String::from(..);
#   return t.pick(o.as_str()); }` where `pick` returns `self.s` — stricter than
#   BOTH the `&dyn` and the concrete spelling of the same program, which rustc
#   accepts (elision rule 3). Naming the callee instead resolves `result<-0x1`
#   and admits it. pass/bc_esc_generic_recv_admit and
#   pass/bc_esc_generic_monokey_admit are what keep that distinction visible: a
#   corpus that merely stays green cannot tell a precise fix from a blanket one.
#   THE SEVEN (4 fail + 3 pass), refuse/admit in pairs:
#     tests/logos/fail/bc_esc_generic_recv_dangle.logos
#     tests/logos/fail/bc_esc_generic_uninst_dangle.logos
#     tests/logos/fail/bc_esc_generic_monokey_dangle.logos
#     tests/logos/fail/bc_esc_generic_outparam_dangle.logos
#     tests/logos/pass/bc_esc_generic_recv_admit.logos
#     tests/logos/pass/bc_esc_generic_uninst_admit.logos
#     tests/logos/pass/bc_esc_generic_monokey_admit.logos
#   CONTROLS, one variable each, logosc-only rebuild, restored between:
#     (1) `if (true) return nullptr;` at the head of the mono-key fallback ->
#         fail/bc_esc_generic_monokey_dangle rc 0 (defect back); the other six
#         fixtures keep their verdicts.
#     (2) `generic_templates_only ? nullptr : &flows` at the checker ->
#         fail/bc_esc_generic_uninst_dangle rc 0 (defect back); the other six
#         keep their verdicts.
#   That the two controls hit DISJOINT fixtures is what makes the arms
#   separately pinned rather than jointly.
#   RED LIST ON A FULL TREE REBUILD (51 logos targets: stdlib lang/mem/std,
#   lforge, peg_gen_logos, memoria-ctr/-store, ub_boundary, ctr_mod, examples):
#   EMPTY. rc 0, 0 logos errors. Build time 2m56s with the change against 3m09s
#   with the pre-mono summaries reverted on the same tree — the second
#   summarizer run is inside the build's noise.
#   ⚠ A SIDE EFFECT WORTH NAMING, NOT A CLOSURE: the generic-receiver spelling
#   of the OUT-PARAM channel now refuses too (fail/bc_esc_generic_outparam_
#   dangle, E0597), because `apply_flow_outparams` finally resolves the callee.
#   #78 — a CALLEE-local borrow stored through the caller's `&mut` — is
#   untouched and stays open.
# 2026-08-19, +2/+2/0 (7324/3641/36 -> 7326/3643/36), predicted before the
# reconfigure and met exactly. THE #80-VERIFY FINDINGS RE-VERIFIED, and the
# same rule applied to the repair the previous entry made.
#   (A) THE ENTRY BELOW'S OWN FINDING (2) LEFT A ZERO-FIRING ARM. It made
#       `place_slot_type`'s struct-key miss a LOUD DECLINE and justified it
#       with "zero misses measured" — which is exactly the shape finding (1)
#       refused one paragraph earlier: a report no program executes. Both
#       declines are now EXECUTABLE, one fixture per arm:
#         tests/logos/fail/mlirgen_place_slot_decline.logos       (struct miss)
#         tests/logos/fail/mlirgen_place_slot_null_decline.logos  (null type)
#       Fault injection in `declare_local_place`, same shape as
#       `__slotfit_canary`: `__slotmiss_canary*` sets `slot_decline_canary_`
#       so the `struct_types_` lookup takes its miss arm, `__slotnull_canary*`
#       asks with a null TypeRef. ONE PREFIX PER ARM because the fail runner's
#       assertion is `grep -F` over the whole `.expected`, and a two-line
#       pattern there is an OR — a single fixture asserting both texts would
#       stay green after either report was deleted.
#       CONTROLS, logosc-only rebuild each, restored to md5
#       868ffa31b10473d04be34f272d78b41a (mlir_gen_expr.cpp — the file then
#       took COMMENT-ONLY edits naming the two fixtures at their sites, so the
#       committed md5 is cb2ca02d27e239a85355d16277df5f49; the full rebuild and
#       every gate below ran on that one):
#         `if (false) bug(...)` on the NULL arm   -> null fixture rc 1
#                                                   ("logosc: wrote /dev/null")
#         `if (false) bug(...)` on the STRUCT arm -> struct fixture rc 1
#       And the perturbation that proves the struct arm's text without the
#       injection: `if (false && sit != struct_types_.end())` -> compiling
#       pass/bc_fatval_deferred_init_len reports the miss for
#       `bc_fatval_deferred_init_len.S` and `.W`, rc 1.
#   (B) THE NULL ARM WAS NOT A DEAD ARM — it fired once on correct code, and
#       the ask was SPECULATIVE. `gen_lvalue_addr`'s IndexRead case computed
#       the element stride EAGERLY, before dispatching on the receiver kind;
#       `TypeRef(Ptr/Ref).elem()` is null, so the two pointer-receiver arms
#       (which then recompute the stride from the POINTEE) asked
#       `place_slot_type(null)` and discarded the answer. Harmless while that
#       answer was a silent i32 guess; a FALSE REPORT once it became a decline.
#       Measured on pass/bc_fatval_deferred_init_len (one fire, backtrace via
#       LOGOS_MLIRGEN_ABORT_ON_BUG + gdb: place_slot_type <- gen_lvalue_addr).
#       Closed by making the ask LAZY, per receiver arm — the Slice and Array
#       arms ask, the two pointer arms keep the stride they already computed
#       from the pointee. No stride changes.
#       Also loud now: the NULL fallback used to answer i32, a FOUR-byte place,
#       which is smaller than every fat repr and most scalars — wrong in the
#       overwrite direction, the #80 defect's own shape.
#   (C) THE PROSE REPAIR THE ENTRY BELOW CLAIMS WAS INCOMPLETE. It fixed the
#       header map of pass/bc_fatval_deferred_init_len but not the three
#       SECTION headers, which a reader debugging a red reaches first:
#       "1-5" -> "1-6" (the call-arrival cell 6 is in that section),
#       "7-13" -> "7-17" (struct W is 14/15, zone_mut 16/17),
#       "22-28" -> "22-30" (the zone_mut control returns 29/30). The map at the
#       top and the section headers now both match the `return` values.
#   (D) THE SENSOR AND THE &mut [T] CELL RE-VERIFIED FROM SCRATCH, not taken on
#       report. `fail/mlirgen_slot_fits_sensor` rc 0; injection disabled
#       (`if (false && nm.rfind(...))`, logosc-only rebuild) -> rc 1 with
#       "logosc: wrote /dev/null", i.e. green-by-silence, restored to md5
#       bc71ba7e8873708727744ef7e4969243 before the next step. Injection WIDENED
#       to every deferred local -> pass/bc_fatval_deferred_init_len does not
#       compile, 11 sensor reports naming v1 v2 v4 v5 v6 sl cl st v7 and `ms`
#       — `ms` is the deferred `&mut [i64]` the #80 report called unbuildable,
#       so cell 31-36 bites on the class it was written for.
# +2 registry rows = 2 fail fixtures; the pass corpus is unmoved, so
# logos_09_direct_door_census's 2191/191/2000 stand (re-derived by direct file
# listing: `ls tests/logos/pass/*.logos|wc -l` = 2191,
# `ls tests/logos/pass/{wql_*,deem_*}.logos|wc -l` = 191).
# 2026-08-19, +1/+1/0 (7323/3640/36 -> 7324/3641/36), predicted before the
# reconfigure and met exactly. #80 VERIFY — three findings, and the +1 is the
# fixture that makes the first one executable.
#   (1) `check_slot_fits` WAS A SENSOR NOTHING EXECUTED. Measured with a
#       temporary fprintf on its comparison line: the guard is ASKED 45 494
#       times — 17 113 over the fixture corpus (4 473 programs, 399 of them
#       reaching a call site) and 28 381 over a full stdlib+examples build —
#       and `have` equalled `want` EVERY time. Correct codegen cannot reach the
#       report, so its green said nothing. It is now executed by a name-scoped
#       FAULT INJECTION in `declare_local_place`: a local named
#       `__slotfit_canary*` gets the pre-#80 8-byte handle slot, so the
#       assignment copies 16 into 8 and the compile fails with the sensor's own
#       words. Both halves control-reverted, logosc-only rebuild each:
#       injection disabled -> the fail fixture reds (rc 1, "stderr did not
#       contain"); sensor's report disabled (`if (true || have >= bytes)`) ->
#       same rc 1; both restored (md5 mlir_gen_stmt.cpp
#       bc71ba7e8873708727744ef7e4969243, mlir_gen_impl.hpp
#       bd9c4a8bae3ead2a74cf8f990a4ef187) and green again.
#         tests/logos/fail/mlirgen_slot_fits_sensor.logos
#   (2) `place_slot_type`'s Struct arm GUESSED on a `struct_types_` miss — it
#       fell through to `logos_to_mlir(Struct)` = the 8-byte handle `ptr`, i.e.
#       a place slot / element stride of 8 bytes for a struct of any size, and
#       since #80 also a deferred-`let` slot that registers `var_struct_`. The
#       miss is the open bare-name class (#58/#60/#61). Now a LOUD DECLINE
#       through the R2 sink. Measured on the same two sweeps: ZERO misses over
#       the corpus and the full build, so no program in the tree reaches it
#       today — the decline is there so the first one that does is a named
#       compile failure and not silent 8 bytes.
#   (3) REFUTED CLAIM REPAIRED. The #80 report said a `&mut [T]` local is
#       unbuildable and sema refuses every spelling. FALSE: the whole-array
#       borrow `let s: &mut [i64] = &mut a;` builds one, and so does the
#       DEFERRED spelling. Cells 31-36 of the fixture carry it — deferred
#       `&mut [i64]` + three live neighbours + write-through + the initialised
#       control — and they BITE: with the pre-#80 slot decision forced on for
#       every deferred local, `ms` does not compile ("copies 16 bytes of a fat
#       {data,meta} pair into a 8-byte slot"). The fixture's prose numbering was
#       also repaired in the same edit: the header said the overwrite was cell
#       14 and the controls 20-27, while the code returns 19-21 and 22-30.
#         tests/logos/pass/bc_fatval_deferred_init_len.logos  (30 -> 36 cells)
# +1 registry row = 1 fail fixture; the pass corpus is unmoved (an existing
# fixture grew cells), so logos_09_direct_door_census's 2191/191/2000 stand,
# re-derived by direct file listing.
# 2026-08-19, +7/+7/0 (7316/3633/36 -> 7323/3640/36), predicted before the
# reconfigure and met exactly. #77 ROUND 2 — the verify of the just-landed
# #77/#78/#79 returned four findings, each landed on its own with a stated rc
# and a full stdlib rebuild between:
#   (1) the summary SEED was blind to a by-value aggregate holding a SHARED
#       borrow, and the `approx` flag recorded only bits GUESSED IN, so an
#       INCOMPLETE mask printed EXACT and the #77 door trusted it. Seed closed
#       (`bc_holds_any_ref_type` in `can_carry`), flag split into
#       `over_approx` / `under_approx` with the under half CONSUMED (an
#       under-approximate summary is refused by `flow_of_call` /
#       `flow_of_method` / `flow_of_fnptr`, so every consumer takes its
#       summary-less route instead of trusting a mask that narrows). The
#       widening needed its Rust-parity cut in the SAME step —
#       `stored_shared_extract`, "a shared borrow copied out through a `&` has
#       the STORED borrow's lifetime" — measured: without it the full stdlib
#       rebuild came back 6 red, all in wql/plan_walker.logos.
#         tests/logos/fail/bc_esc_summary_seed_field_dangle.logos
#         tests/logos/pass/bc_esc_summary_seed_field_admit.logos
#   (2) `prov_of`'s Code::MethodCall arm had the SAME door defect #77 had just
#       fixed on Code::Call — its fat gate returned {} before the summary
#       consult. Measured twin, one variable: the method spelling admitted at
#       rc 0 while the free-fn spelling of the same body refused at rc 1.
#         tests/logos/fail/bc_esc_method_retain_dangle.logos
#         tests/logos/pass/bc_esc_method_retain_admit.logos
#   (3) #79's residual was understated: `flow_of_fnptr` resolves only a LOCAL
#       whose initializer is a known fn item, so a pointer arriving as a
#       PARAMETER or read from a STRUCT FIELD admitted a real dangle (and so
#       did the `fnptr_multi_` case round 1 named). An unresolvable indirect
#       callee now takes the documented (d) route.
#         tests/logos/fail/bc_esc_fnptr_param_dangle.logos
#         tests/logos/pass/bc_esc_fnptr_param_admit.logos
#   (4) the #77 repair leaked a compiler-internal temp name: a function with a
#       droppable local printed "local variable '__ret_tmp_0'" where the thin
#       twin correctly named `t`. No fixture pinned that path.
#         tests/logos/fail/bc_esc_ret_temp_name.logos
# +7 registry rows = 4 fail + 3 pass; tier_commit unmoved — none of the seven
# is a lint or a census gate.
# 2026-08-19, +1/+1/0 (7315/3632/36 -> 7316/3633/36), predicted before the
# reconfigure and met exactly. #80 — A DEFERRED-INIT LOCAL OF A FAT TYPE GOT AN
# 8-BYTE SLOT. `let v: T;` with no initialiser decided its slot with
# `logos_to_mlir(T)`, which is the by-pointer HANDLE query and answers `ptr` for
# every fat repr, and it registered no shape; the INITIALISED `let` arms each
# allocate the storage type (slice/dyn/closure/tuple/enum) and register the
# shape that makes a read take the slot AS the value. So a deferred fat local
# had an 8-byte slot that the later `v = …` memcpy'd 16 bytes into — a stack
# overwrite of the next local — and every read loaded through it once too many.
# Measured PRE-EXISTING at 6440e565 (2026-08-04): identical values AND
# byte-identical `@main` IR on both binaries; `git blame` puts the branch at
# 813aa91b5 (2026-05-11). NOT caused by the #70-#79 borrow-check work.
#   THE FIX IS ONE DECISION SITE: `MLIRGenImpl::declare_local_place`
#   (src/compiler/mlir_gen_stmt.cpp), called from the `!val_le` branch of
#   `gen_let_inner`. It states each kind's convention by mirroring the
#   initialised arm it has to agree with — Slice/Closure/Tuple → the 16-byte
#   pair + `var_tuple_`; TraitObject (incl. `&dyn`, `Box<dyn>`) → dyn_llvm_type
#   + `var_dyn_trait_`; tagged Enum → the inline body + `var_tagged_enum_`;
#   Struct → place_slot_type + `var_struct_`; Array → the array + var_subscript_;
#   `&Struct`/`&mut Struct` → an 8-byte slot + var_local_ptrs_ (the `let mut r =
#   &s` arm, since a deferred binding is rebindable by construction); scalars
#   unchanged. A raw `*const/*mut dyn` deliberately keeps HANDLE semantics.
#   THE CLASS, measured cell by cell (probe -> before -> after):
#     str 1 -> 5 · &[i64] 2 -> 3 · &dyn SIGSEGV -> 7 · closure SIGSEGV -> 7 ·
#     Box<dyn> SIGSEGV -> 7 · FatZoneMut 10 -> 0 · [i64;3] COMPILE_FAIL -> 6 ·
#     neighbour local clobbered (a=5) -> intact. Shapes: straight-line, block,
#     if/else arms, match arms, `let mut`, value-through-a-call — all 1/0 -> 5.
#     Unmoved and correct before and after: tuple, struct, String, i64, &i64,
#     and every initialised twin.
#   PLUS TWO SITES THE FIX EXPOSES RATHER THAN CAUSES: `gen_assign` had no
#   whole-TUPLE rebind arm (it worked only because the 8-byte slot ALIASED the
#   RHS temporary), and every value-copy rebind arm memcpy'd into the slot with
#   no size check. `check_slot_fits` now compares the destination alloca's
#   allocated type against the copy size and reports a malfunction through the
#   R2 sink — under it, the OLD slot decision is a COMPILE ERROR instead of a
#   silent stack overwrite (measured: with the fix reverted behind an env gate,
#   str / slice / closure / the clobber probe all fail to compile naming the
#   8-byte slot; the whole stdlib builds with the fix and fires it 0 times).
#   ⚠ CORRECTED BY THE #80 VERIFY (entry above): "fires it 0 times" was the
#   whole of the evidence for the sensor, and a report arm no program executes
#   is not evidence at all. It is now asked 45 494 times with 0 fires AND its
#   report is executed by tests/logos/fail/mlirgen_slot_fits_sensor.logos
#   THE FIXTURE: tests/logos/pass/bc_fatval_deferred_init_len.logos — 36
#   assertions (30 at landing + cells 31-36, the deferred `&mut [T]` class the
#   report had called unbuildable), one exit code each, every one bite-proven by
#   perturbing its constant or, for 31-36, by forcing the pre-#80 slot decision.
#   Cells 22-30 are the INITIALISED controls in the same file, so a "fix" that
#   broke both spellings into agreement cannot satisfy it.
#   DEBT PAID: four admit fixtures asserted `len() >= 0` — a tautology forced by
#   this defect — and now assert the real length: bc_fatret_nested_call_admit,
#   bc_fatret_methodarg_admit (5), bc_esc_fnptr_admit (5),
#   bc_esc_outparam_scope_admit (5 and 2). All four bite (perturbed -> rc 1).
#   bc_fatret_struct_field_admit was named in the brief but never had the
#   weakening — it asserts `*held.r == 9` and is untouched.
# 2026-08-19, +6/+6/0 (7309/3626/36 -> 7315/3632/36), predicted before the
# reconfigure and met exactly. #77 / #78 / #79 — THE THREE ESCAPE CHANNELS THAT
# NEVER ASKED THE CALLEE, landed ONE AT A TIME, each with its own full stdlib
# rebuild as the red list and its own control revert. The previous entry's
# closing sentence is the brief for this one: the deposit site consults a
# summary, and NO OTHER escape site did. That framing was TESTED here and it
# holds for two of the three — but #77 turned out not to be summary-blind by
# its RULE at all, only by its DOOR (see below).
#   #77 RETURN ESCAPE — `prov_of`'s `Code::Call` arm opened on
#   `is_borrow_carrying_type(result)`, so a call returning a bare `&i64`
#   returned {} two lines BEFORE the summary consult that was already there and
#   already correct. Widened to `type_may_carry_borrow` (the predicate the
#   §B6 deposit arm moved to at round 14 / Q5 for the same question).
#   RED LIST of the un-narrowed widening, measured on a full stdlib rebuild:
#   3, all in `eval_sexpr` (stdlib/mem/deem/tpl.logos:329/420/424, the three
#   `return RtVal::S(intern(scratch, &t))` spellings), all classified FALSE —
#   `intern` (deem.logos:1062) retains only its ARENA; `Writ::wstring` COPIES
#   the bytes (copy_bytes, stdlib/lang/writ/wstring.logos:58) and declares
#   `&'a WString` tied to `self` alone, so rustc accepts all three. The bit
#   came from `Writ::wstring` being UNAVAILABLE across the package boundary
#   (#81) and `call_result_taint`'s (a)-(d) fallback tying the result to every
#   borrowing operand: `intern: result<-0x3` where the truth is `0x1`.
#   THE NARROWING, and it is a new FACT the summary now carries:
#   `FlowSummary::approx` — true iff a mask bit came from a GUESS (this
#   function's own fallback, or a callee whose summary is itself approx),
#   monotone, in the same fixpoint as the masks (`flow_eq`, +1 per fn in the
#   ICE's derived round bound). The NEW door takes EXACT summaries only; the
#   OLD `#[borrow_carrying]` door is untouched, because that is the behaviour
#   every bc_* pin was measured against. Red list after the narrowing: 0.
#   UNCOVERED, stated with repro paths rather than hidden: (i) a ref result
#   whose callee summary is approx — every cross-package callee — still
#   admits; (ii) `pick(h: H) -> &i64 { return h.r; }` with `H { r: &i64 }`
#   passed BY VALUE summarises `result<-0` (sandbox/escchan/p_struct.logos,
#   rc 0) and so does the array spelling (sandbox/escchan/p_arr.logos) — a
#   SUMMARIZER SEED gap, not a channel gap: `seed()` marks a by-value aggregate param through
#   `bc_holds_mut_ref_type`, which does not see a plain `&`. Widening that seed
#   moves every channel at once and needs its own measured red list.
#   #78 OUT-PARAM SCOPE ESCAPE — `apply_flow_outparams` read `to_outparam[j]`
#   and moved LOANS and alias edges through it, so a later MUTATION was caught;
#   nothing wrote `ref_borrow_sources_`, so the source simply DYING was
#   invisible. `set2(&mut k, owner.as_str())` admitted at rc 0 while the direct
#   `k.f = owner.as_str()` refused with E0597 naming `k`. Deposit added at the
#   same site, keyed to the out-param ROOT (the mask names a PARAMETER, as A2
#   and X1 already had to say). RED LIST on the full stdlib rebuild: 0.
#   #79 FN-POINTER CALL — the §B6 `FnPtrCall` arm applied the summary-LESS
#   filter triple only, so a fat by-value arg deposited nothing:
#   `let f: fn(str)->str = keep1; v = f(owner.as_str())` admitted while the
#   identical direct call refused. `flow_of_fnptr` (G1's resolver: known fn
#   ITEM, never reassigned) is now consulted, i.e. the priced-and-not-taken
#   option ("be conservative for every ref-typed arg through any pointer") is
#   NOT what landed. RED LIST on the full stdlib rebuild: 0. UNCOVERED: a
#   pointer in `fnptr_multi_` (assigned two different fns) resolves to nullptr
#   and its dangle still admits — sandbox/escchan/r79_multi.logos, rc 0; and a
#   ClosureCall's ARGUMENTS are still never consulted (a closure body is never
#   summarised; its CAPTURES are, and were already).
#   THE ARMS EXECUTE, fire-printed over one `stdlib/mem` module build and the
#   prints then REMOVED (this is the check the previous round's two deleted
#   arms failed): #77's new door TAKEN 408 times on exact summaries and SHUT
#   238 times on approx ones; #78's deposit 68 times; #79's summary term admits
#   238 arguments that none of the other three predicates admit. All with 0
#   reds, so each is live and each is priced.
#   THE PAIRS, +3 fail / +3 pass, each pair one variable:
#     fail/bc_esc_return_summary_dangle  · pass/bc_esc_return_summary_admit
#       (which argument `second<'a>(a:&i64, b:&'a i64)` retains — the local or
#        the parameter; `result<-0x2`, so the arm does not tie every ref arg)
#     fail/bc_esc_outparam_scope_dangle  · pass/bc_esc_outparam_scope_admit
#       (the stored borrow's SOURCE — an inner-block local, or the caller's own
#        parameter; the admit half carries the static-literal direction too)
#     fail/bc_esc_fnptr_dangle           · pass/bc_esc_fnptr_admit
#       (what the pointed-to fn DOES — returns its argument, or a literal)
#   CONTROL REVERTS, each perturbing the side that fires, each restored to a
#   green checkpoint before the next: #77 gate reverted to `is_borrow_carrying_
#   type` -> its fail fixture rc 1 (ch2/ch3 unmoved); #77 mask ignored (tie
#   every arg) -> its ADMIT fixture rc 1 AND the stdlib build breaks in 2 places
#   (`parse`, `wbs_read`); #78 deposit `if (false)` -> its fail fixture rc 1
#   (ch1 rc 0); #78 out-param mask ignored -> the stdlib build breaks
#   (`ward_rule_join`, "cannot borrow 'wp' as mutable"); #79 summary term
#   `&& false` -> its fail fixture rc 1 (ch2 rc 0).
# 2026-08-19, +8/+8/0 (7301/3618/36 -> 7309/3626/36), predicted before the
# reconfigure and met exactly. #71/#72: THE RAW-POINTER ROUND TRIP SEVERED
# BORROW PROVENANCE, IN TWO SEPARATE CHANNELS, AND BOTH ARE CLOSED AT THE
# DEPOSIT SITE (collect_ref_sources_paths) — NOT AT THE RETURN SITE. The
# round's own verify measured the difference and it is not a quibble: the
# return-escape channel never consults a flow summary, it is keyed on the
# DECLARED #[borrow_carrying] attribute, so `fn bad() -> &i64 { let t = 9;
# return keepr(&t); }` — a plain &i64 through a one-line identity call —
# still ADMITS (task #77, with the same shape for FnPtrCall/ClosureCall in
# #79 and the out-param scope-escape half in #78). "Closed" here means: the
# deposit site now ties what the callee's summary says it retains, which is
# what #72's cross-fn shape and #71's struct-field shape needed.
#   Layer 1 — src/compiler/borrow_flow_summary.inc. Four repairs to the
#   summarizer's `taint_of` / `call_result_taint`: a `SlicePtr` arm (`s.as_ptr()`
#   on a SLICE is a node, not a MethodCall), an address-arithmetic PEEL entered
#   from a Cast into a carrying type (`(p as i64 + 8) as *const T` decays the
#   static type, so no can_carry-GATED BinOp arm can see it), a `PtrArith` arm
#   (`p.byte_add(n)` is its own node — found while bite-proving the peel), and
#   the fallback ARG gate widened from a three-predicate triple to the same
#   `can_carry` the RESULT gate one line above already used (the asymmetry
#   inside one fallback WAS the defect: bodyless `str_from_raw` contributed
#   nothing, so the whole `string_as_str` family read `result<-0`).
#   TWO ARMS WERE WRITTEN, MEASURED DEAD AND REMOVED rather than shipped: a
#   gated `BinOp` and a gated `Unary` arm executed ZERO times over the probes,
#   the stdlib and examples/deem_memoria_showcase.logos (fire-print) — a BinOp
#   can never be pointer-typed and generic bodies are never summarised.
#   Layer 2 — src/compiler/borrow_check.cpp collect_ref_sources_paths. The
#   per-arg filters never consulted a summary, so a FAT by-value argument
#   (`my_as_str2(owner.as_str())`) tied nothing even when the callee's summary
#   said `result<-0x1`. An argument now ties iff the callee's flow summary says
#   its bit reaches the result, which keeps the tv_build exemption BY
#   CONSTRUCTION (`tvb` summarises `result<-0`; measured) instead of by a
#   fat-versus-plain guess. The MethodCall arm's entry gate widens the same way
#   (`result_borrows_self` asks only about the RECEIVER, so a method retaining
#   an ARGUMENT was invisible).
#   THE SIX NEW TESTS, three refuse/admit PAIRS, one per channel:
#     tests/logos/fail/bc_flowsum_rawtrip_outparam_dangle.logos
#     tests/logos/pass/bc_flowsum_rawtrip_outparam_admit.logos
#     tests/logos/fail/bc_fatret_nested_call_dangle.logos
#     tests/logos/pass/bc_fatret_nested_call_admit.logos
#     tests/logos/fail/bc_fatret_methodarg_dangle.logos
#     tests/logos/pass/bc_fatret_methodarg_admit.logos
#   Each admit twin differs from its dangle twin by ONE VARIABLE — whether the
#   callee RETAINS its argument — not by the argument's type, which is the same
#   fat `str` on both sides. None is tier_commit; none is imported; the names
#   carry the `bc_` prefix and so stay outside the `logos_09` glob populations.
#   Layer 3 — #71, the plain-struct raw-ref FIELD, lands here too, with its own
#   pair (+2 of the 8). `type_may_carry_borrow` is the ENTRY gate of the EC::Call
#   arm and it answered NO for `struct H { r: &i64 }`: not a ref kind, not
#   `#[borrow_carrying]`, not loan-carrying (that set propagates only NAMED
#   carriers), no type_args, not a tuple, not an array — so the arg loop inside
#   the `if` never ran and `pick(H { r: &tmp })` escaped at rc 0 while its
#   generic twin `H<T> { r: T }` refused. New `TypeSets::holds_any_ref`:
#   holds_mut_ref's fixpoint with ONE predicate changed (MutRef -> is_ref_kind)
#   plus tuple/array element seeding, which holds_mut_ref's SET BUILDER lacks.
#     tests/logos/fail/bc_fatret_struct_field_dangle.logos
#     tests/logos/pass/bc_fatret_struct_field_admit.logos
#   THE WIDENING WAS PRICED BEFORE IT LANDED, not after: 446 names enter the set
#   where holds_mut_ref holds 17 (measured over the whole stdlib), and against
#   that the corpus moved ZERO rows — L1 725/725, L2 2213/2213, `ctest -R _bc_`
#   226/226, `ctest -L fail` 1382/1382, and the stdlib itself still builds. The
#   abuse-direction control (`if (!n.empty()) return true;` — every named type
#   carries) reds the STDLIB build, so the set's NARROWNESS is load-bearing and
#   the green is not vacuous.
# 2026-08-19 (post-landing repair, no count change): the #75 fix's own verify
# found its residual REACHABLE, and it is closed here. The point packed the
# ordinal into 20 bits, so past 1,048,575 statements on ONE physical line the
# ordinal SATURATED and every later statement collapsed to one point — the fix
# silently re-opened the exact hole it exists for, in exactly the channel it
# exists for (a single-line emitted module puts its whole statement count on
# line 1). Witness pair, built by the verify and re-run here on the widened
# build: sandbox/verify75/big_sub.logos (1,000,004 statements on one line) and
# sandbox/verify75/big_sat.logos (1,100,004) straddle the cap — the second read
# rc 0 at 20 bits and reads rc 1 now. A line is uint32, so (uint64(line) << 32) | ord is EXACT: the
# point domain is the full cross product and nothing is packed away.
# Also repaired, same round, no count change: pass/bc_line_ordinal_emitted_admit
# now CALLS the emitted fn and asserts its value (its `main` was `return 0i32;`,
# so "compiles, links and runs" asserted nothing past codegen — bite-proven);
# direct_door_census_gate.sh's header numbers corrected to the measured 26
# doors / 11 fixtures / 36 corpus doors, its tautological CLAUSE-3 attribution
# leg DELETED with the reason (same accumulator read twice — CLAUSE 4 and
# CLAUSE 5 are what actually check attribution), its vacuous `overlap` pin
# marked as set-arithmetic rather than left reading like a check, and its
# clause labels reconciled with the code.

# 2026-08-19, +1/+1/0 (7300/3617/36 -> 7301/3618/36), predicted before the
# reconfigure and met exactly. #76: THE DIRECT DOORS OUTSIDE THE QUERY GLOB WERE
# PINNED BY NOTHING. `logos_09_pull_shape` and `logos_09_plan_ground_census`
# both sweep `pass/wql_*` + `pass/deem_*` — 191 of 2180 pass fixtures — and
# between them pin 10 ADR 0025 §12 direct doors. The corpus holds 36. The new
# `logos_09_direct_door_census` (tier_full, +1 test, not tier_commit, not
# imported) sweeps ALL 2180 and pins the rest.
#   THE PARTITION, asserted in the gate: corpus 2180 = glob 191 + nonglob 1989,
#   the glob half read with the LITERAL shell globs the other two gates use and
#   compared as a SET against the prefix rule, plus swept-vs-listed both ways —
#   which is the only half of that clause not forced by arithmetic.
#   THE COUNT WAS RE-DERIVED, NOT INHERITED. The briefed floor was 22 (the
#   S5-direct verify over the 69 non-glob fixtures mentioning `deem`, counted by
#   the `^pub struct …Dx… {` shape). Measured: 26. +2 `memoria_showcase_deem`
#   (4 doors, not 2) and +1 `memoria_ctr_vec_deem`, all three emitted into
#   `logos.gen.*` units that the inherited user-module dump rule DROPS; +1
#   `container_item_from_module`, which does not compile standalone at all
#   (needs `-l libctr_mod.a`, exit 4) and contributed a silent zero. 22+2+1+1=26.
#   THE RULE IS SCOPED BY PROVENANCE, NOT SHAPE, and the corpus forced it:
#   `pass/bc_d8_quote_field_split_admit` emits `#[borrow_carrying] pub struct
#   QuoteDx` with a matching `next_batch` from `gen_quote` — a HAND-WRITTEN
#   mimic (tasks #74/#75), 2 of the 5 door spellings. A shape-only wide count
#   reads it as a half-emitted door. Every `--gen-dir` unit carries
#   `// emitted by: <fn>`; only `deem` units may hold a door, and the mimic is
#   pinned per spelling in a non-deem residual so it cannot absorb a real one.
#   PER-FIXTURE PLAN↔ARTIFACT IDENTITY: `LOGOS_TRACE_PLAN=1` states the decision
#   in words ("`_stream` DOOR is now the §12 DIRECT form"); plan 36 == artifact
#   36, compared PER FIXTURE, because two totals agree while a door moves.
#   BITES (5, each on a sandbox copy of the swept dumps fed back through the
#   script's 4th argument, each restored md5-proven with a green checkpoint):
#     B1 retype one facade in `memoria_ctr_plan_pushdown` -> CLAUSE 3 reads
#        36/36/36/35/36/36 + PIN dx_facade.
#     B2 delete a whole door unit from `container_item_from_module` -> CLAUSE 4
#        (1 pinned, 0 measured) + CLAUSE 5 (plan 1, artifact 0) + 8 pins.
#     B3 re-attribute one door unit's emitter to `gen_quote` -> CLAUSE 4 +
#        CLAUSE 5 + 14 pins, the residual naming the fixture.
#     B4 set one fixture's recorded rc to 4 -> CLAUSE 2 + PIN unswept 0->1.
#     B5 remove one fixture's rc file -> CLAUSE 1 "never probed".
#   NOT A NARROWING OF ANYTHING: no existing pin moved, `logos_09_pull_shape`'s
#   10 is re-measured here by an independent sweep and CONFIRMED — including on
#   the two GLOB fixtures that gate cannot compile (`wql_mapping_cross_module
#   _e2e`, `wql_wref_field_pkg`, 0 doors each), a fact nothing recorded before.
# 2026-08-19, +4/+4/0 (7296/3613/36 -> 7300/3617/36), predicted before the
# reconfigure and met exactly. #75: LOAN LIVENESS WAS KEYED ON THE SOURCE LINE.
# `release_dead_borrows(cur_line)` released a loan whose holder's last use was
# `<= cur_line`, so two statements sharing ONE physical line released the first
# one's loans before the second one's conflicting use was checked — and every
# metaprog emitter that pushes a whole module as a single-line string was
# thereby exempt from ALL exclusivity checking (move checks and the §B6 escape
# channel still fired, which is why the hole looked like coverage). The key is
# now the pair (line, per-line statement ordinal), lexicographic; statements on
# distinct lines keep ordinal 0 and compare exactly as their lines did, so
# multi-line code is bit-identical and only shared-line code changes.
#   fail/bc_line_ordinal_oneline_fail — the whole hazard on one line; refused
#     with the checker's own B-arm spelling (REUSED, not minted). BITE: dropping
#     the later `p.at` removes the conflict and the assertion misses (rc 1).
#   pass/bc_line_ordinal_admit — two legs the ordinal must NOT refuse: the
#     multi-line original (the bit-identical half) and the SAME pattern on one
#     line with the loan dead before the reborrow (single-line code is checked,
#     not blanket-refused). BITES: moving `p.at` after `w.plain()` reds leg 2
#     (one-line) and leg 1 (multi-line) independently.
# The second pair pins the CHANNEL the hole was found in — a metaprog handler
# that push_str's a WHOLE MODULE as ONE line — because no stdlib emitter in the
# tree emits a single-line fn body today, so without it the class is pinned only
# by hand-written one-liners:
#   fail/bc_line_ordinal_emitted_oneline_fail — the emitted chunk carries the
#     same hazard; MEASURED rc 0 on the pre-#75 compiler, rc 1 now. BITE:
#     dropping the later `p.at` INSIDE the emitted string kills the refusal.
#   pass/bc_line_ordinal_emitted_admit — byte-identical emitter, conflicting use
#     removed; the single-line emitted module still compiles and runs. BITE:
#     putting `w.plain()` back into the emitted string reds it.
# CONTROL: forcing the ordinal to 0 at its assignment site (`stmt_point`, not a
# call site) rebuilds a compiler that admits the fail fixtures again and leaves
# both admits green — the pin is held by the ordinal, not by anything else in
# the round.
# 2026-08-19, +2/+2/0 (7294/3611/36 -> 7296/3613/36), predicted before the
# reconfigure and met exactly: the #74 fix round's OWN verify found a second
# site of the class it had just closed, and this pair pins it. A MATCH
# SCRUTINEE is a whole-value read; `take_borrow`'s B83 arm has refused
# whole-value shared borrows against a live MUT FIELD loan since it was
# written, but the scrutinee is visited non-consumingly and never reaches
# take_borrow — the only guard that ran was `check_live`, which reads the
# WHOLE-VARIABLE flag alone. Found by a ONE-TOKEN twin pair (loan on `&mut d`
# refuses / loan on `&mut d.a` ADMITTED, same `match &d`), pre-existing and
# with zero instances corpus-wide — the permissive shape.
#   fail/bc_d8_match_scrutinee_field_loan_fail — refuses with B83's own
#     spelling (REUSED, not minted: same fact, two routes). BITE: moving the
#     loan's last use BEFORE the match kills it under NLL and the assertion
#     misses (rc 1) — so the guard does not outlive the loan.
#   pass/bc_d8_match_scrutinee_disjoint_admit — three legs the guard must NOT
#     refuse: a disjoint FIELD scrutinee, the same `match &d` after the loan is
#     dead, and a CALL scrutinee (the direct emitter's own shape). BITE: leg 1's
#     `&d.b` → `&d` reds the fixture.
# 2026-08-19, +1/+1/0 (7293/3610/36 -> 7294/3611/36), predicted before the
# reconfigure and MEASURED exactly: V2-M1 — the §12 `direct` door ADMITTED a
# query whose emitted door does not compile. `emit_simple`'s `dx_on` cascade
# asked nine questions and not the tenth: is the rendered `select`/`where`
# INFALLIBLE? `emit_sexpr` lowers checked-integer arithmetic through the
# error-returning tower (`el_addu(..)?`), and that `?` is well-typed in the
# buffered `_run` (`Result<Vec<E>, ElError>`) and NOT in the direct door's
# `next_batch` (`Option<&[E]>`), so `select (e.key, e.val + 1u64)` or
# `where e.key + 1u64 > 30u64` over a container walk emitted
# `'?' operator used in function that does not return Result<T, E>` against a fn
# nobody wrote. All ten corpus doors project bare fields, so no gate saw it.
# CLOSED AT THE DECISION SITE, not at the emitter: a new cascade clause
# (`clause_infallible`, rexpr_walk.logos) refuses the door and states why. The
# alternative — threading the error out of `next_batch` — is UNAVAILABLE, and
# that is a fact about `BatchStream<B>::next(&mut self) -> Option<B>`
# (stdlib/lang/stream/stream.logos): the pull protocol has no error channel, so
# a `Result`-returning `next_batch` could only be re-wrapped by DISCARDING the
# error at the trait facade, trading a compile-time red for a short answer.
# One test:
#   pass/deem_direct_fallible_buffered — the one-token pair-mate of
#     `deem_direct_stream_pull` (`+ 1u64` in the projection). It COMPILES (which
#     is the refusal, and the failure mode no runtime oracle reaches), reads
#     `pk == 1` — the BUFFERED door's signature, against its direct sibling's
#     `pk >= 2` over the same container and the same 2000 rows — and its rows,
#     key-sum and value-sum equal the CONTAINER'S OWN CURSOR walked with the
#     same predicate and the `+ 1` applied by hand. Bite-proven: perturbing the
#     new clause to `false && (…)` re-admits the query and the fixture reds at
#     COMPILE time with the exact two errors above, while its bare-field sibling
#     stays rc 0; restored md5-asserted (rexpr_walk.logos
#     44c0ae2f62fdafd04c64696cd2378fad).
# `deem_direct_fallible_buffered` IS a `deem_*` fixture and therefore joins the
# logos_09 glob populations. Both were re-derived and both are green:
#   logos_09_plan_ground_census — EXPECT_FIXTURES 190 -> 191, EXPECT_OUTQ
#     608 -> 609, EXPECT_OUTHEAD["query output"] 480 -> 481, EXPECT_NOMAT
#     readonce 28 -> 29, EXPECT_REFUSED 491 -> 492, EXPECT_DIRECT unmoved at 10.
#     The per-clause refusal census moved OFF the comment and INTO code
#     (`EXPECT_DXWHY`, thirteen first-reason counts with an asserted sum);
#     `slice` 82 -> 55 and `notfwd` 51 -> 50 are pure RE-ATTRIBUTION — the new
#     clause is a SHAPE clause and is asked before the SOURCE clauses, so 28
#     queries that were refused for their source are now refused for their
#     arithmetic. Bite-proven: moving the clause to the END of the cascade reds
#     exactly those three per-clause pins and leaves EXPECT_REFUSED (492) GREEN,
#     which is the hole a total-only pin had.
#   logos_09_pull_shape — dumps 173 -> 174, nb_all 1040 -> 1041, nb_pull
#     1030 -> 1031, both batch-loop counts 1030 -> 1031; nb_forward and all four
#     door counts UNMOVED at 10, which is the fixture's own assertion read off
#     the artifact.
# 2026-08-19, +2/+2/0 (7291/3608/36 -> 7293/3610/36), predicted before the
# reconfigure and MEASURED exactly: V1-M1 — the SHARED half of the receiver
# conflict check. `check_recv_conflict` consulted the field-loan table for
# `is_mut` receivers ONLY, so a `&self` method call on the WHOLE variable while
# one of its FIELDS carried a live mut loan was ADMITTED. The D8 landing above
# is what exposed it: D8 moved the receiver loan out of the whole-variable flag
# (`mut_borrowed`, which check_live refuses for any use) into the field table,
# leaving this arm as the only guard on the path — and it had no shared branch.
# A `self` receiver is the only spelling that reaches it, because `self` is a
# reference parameter and sema never wraps it in an AddrOfTemp; the value-local
# twin goes to take_borrow, whose shared arm (§B83) has had the check all along.
# Two tests, one PAIR:
#   fail/bc_d8_shared_use_while_field_mut_fail — the quote_item! channel; the
#     `&self` method READS `self.w.at`, the field `self.w.next_batch()` holds
#     `&mut` and mutates, so the alias is real and not a shape technicality.
#     Control-reverted: with the new arm disabled the file emits NO diagnostic
#     at all (admitted), rc 1 against its .expected.
#   pass/bc_d8_disjoint_field_use_admit — the same live loan on `self.a.w`, but
#     four following uses at three depths all name DISJOINT places, including a
#     DEEP SIBLING (`self.a.sc`, under the same field `a` the loan is rooted
#     in). Bite-proven: disabling the D8 field split (loan widens to whole
#     `self`) reds it on the disjoint writes and the deep sibling; restored
#     md5-asserted (borrow_check.cpp 7c7ac0b4ae62ebf2926c031d1e5e342a).
# Both are bc_* and so stay outside the logos_09 glob populations — the
# deem_*/wql_* gate counts are unaffected and were not re-derived.
# 2026-08-19, +1/+1/0 (7290/3607/36 -> 7291/3608/36), predicted before the
# reconfigure and MEASURED exactly: ADR 0025 §12 — the `direct` STREAM DOOR
# lands, unblocked by D8 above (the fix that closed #74 is what made this stage
# possible; the two are separate landings and this one adds exactly one test).
#   pass/deem_direct_stream_pull  — the direct door pulls MULTI-PACKET (pk >= 2
#     over 2000 rows in an ordered_map) and its rows and key-sum equal the
#     CONTAINER'S OWN CURSOR walked with the same predicate — a different
#     mechanism, not the emitter compared with itself — and the buffered `_run`
#     Vec surface still agrees. Bite-proven three ways, each restored md5-asserted:
#     `pk < 2u64` -> `pk < 200000u64` gives rc 3; the oracle's `> 30u64` -> `> 33u64`
#     gives rc 4 (the FIRST attempt, `> 31u64`, did NOT bite — the keys are
#     multiples of 3, so 30 and 31 select the same set; recorded because a
#     perturbation that changes no set is not a bite); `rows_o` -> `rows_o + 1u64`
#     on the buffered comparison gives rc 7.
# ⚠ THE FIXTURE IS `deem_*`, SO IT JOINS BOTH logos_09 POPULATIONS BY NAME. It
# moved, and every one is re-derived in the gate beside the pin: plan_ground
# census EXPECT_FIXTURES 189->190, EXPECT_OUTQ 607->608, EXPECT_OUTHEAD["query
# output"] 479->480, EXPECT_NOMAT["readonce"] 27->28; pull_shape `dumps` 172->173
# and the batch plane 1019->1030 (+11 = the fixture's own `_run` pull, plus one
# `(self.w).next_batch()` per direct door).
# THE EMITTER, in one sentence: `rexpr_walk::emit_stream_direct` emits, per
# eligible query, a `#[borrow_carrying] pub struct <Q>Dx<walk-ty>` holding the
# source's batch stream as a FIELD plus an owned scratch `Vec<E>` and a `done`
# flag, an inherent `next_batch(&mut self) -> Option<&[E]>` that pulls ONE source
# packet and projects into the scratch, a `BatchStream` forward, and a
# `<q>_stream(...) -> Result<<Q>Dx…, ElError>` facade; the decision site is
# `emit_simple`'s `dx_on`, and every refusal is authored there and printed in the
# landing's ground. 10 doors corpus-wide, all of them the CONTAINER-WALK class.
# ⚠ TWO THINGS THE CORPUS FOUND THAT THE RECONSTRUCTION SPEC DID NOT PREDICT,
# both fixed here rather than deferred:
#   (a) THE STATE TYPE IS PER (QUERY, SOURCE FAMILY). A `deem` over a container
#       CLASS is instantiated once per family (render_deem_plan_chunks, keyed
#       `package::name@family`) and every instance re-enters the emitter with the
#       SAME fn_name. The query FN survives on overloading; a TYPE cannot, so
#       `pass/memoria_ctr_class_deem` and `pass/memoria_ctr_gen_vector_deem` red
#       with `duplicate struct 'HighValueEntriesDx'` / `'TallDx'`. The name now
#       carries the walk type's identifier bytes, which is the family's own
#       emitted name and therefore unique per instance.
#   (b) THE REFUSAL CLAUSES HAD TO BE REORDERED FOR THE GROUND TO BE TRUE. Asked
#       source-first, every `order by` query — which takes the sort ARM and so
#       never claims a walk — was refused as "the source is not a forward batch
#       producer", which is FALSE of a sorted container walk. Shape clauses first,
#       source clauses last. 62 lines changed their reason; none changed its
#       verdict.
# ANSWER ORACLE (the instrument an emitter change owes): answer_diff_instrument
# on the pre-direct tree (D8 only, full rebuild) vs this one — matched population
# 189, MOVED ANSWER ROWS 0, one row ADDED (`deem_direct_stream_pull exit=0`).
# Artifacts change at 10 sites; no answer does.
# 2026-08-18 (third entry), +2/+2/0 (7288/3605/36 -> 7290/3607/36), predicted
# before the reconfigure and met exactly: task #74 / D8 — the MethodCall receiver
# arm in borrow_check.cpp that fires when sema wrapped a FIELD place in an
# AddrOfTemp (`self.w.next_batch()`, callee `&mut self` + borrow-carrying return)
# deposited the loan with take_borrow on the ROOT and DISCARDED bp.path, so every
# sibling-field use that followed (`self.sc.clear()/push()/as_slice()`) red with
# `cannot use 'self' while it is mutably borrowed`. Its two siblings — the
# bare-place receiver arm and the explicit `&mut place` AddrOf arm — already split
# on the path; this one now does too. The filed premise ("the quote_item! splice
# channel loses the place path") is REFUTED: the defect is channel-independent
# and reproduces in plain multi-line source; the module-source half that looked
# green was emitted as ONE physical line, and loan liveness is keyed on source
# LINE, so it was never borrow-checked at all (that permissive hole is SEPARATE
# and still open — a fix there moves in the REFUSING direction over a population
# no gate has ever checked, and must land with a measured red list).
# The unit is a refuse+admit PAIR — an admit alone cannot tell a field split from
# a dropped loan:
#   pass/bc_d8_quote_field_split_admit         — the sibling-field uses ADMIT,
#     pinned on BOTH channels in one compilation (quote_item! splice + ordinary
#     MULTI-LINE source; a single-line half would be a vacuous oracle here)
#   fail/bc_d8_quote_whole_self_conflict_fail  — through the SAME quote channel,
#     a `&mut self` method on the WHOLE struct while the `self.w` loan is live
#     still REFUSES, and now names the field: `cannot borrow 'self': 'self.w' is
#     already mutably borrowed`
# The diagnostic for the mut-binding shape sharpens with the split ('d' -> 'd.w',
# the spelling take_field_borrow already used); RE-SPELLING swept: the one
# in-tree fixture holding `as mutable: not declared as mut`
# (tests/spec/fail/borrow_diag_1__take-mut-requires-mut-binding.expected) pins a
# whole-variable target and does not move.
# 2026-08-18 (second entry, same task), +3/+3/0 (7285/3602/36 -> 7288/3605/36),
# predicted before the reconfigure and met exactly: the #70 verify's MISS-2 —
# the (a) widening had covered only the nested-Call spelling; a borrow composed
# by an AGGREGATE LITERAL in argument position still escaped. forms_borrow_at_call
# gained literal arms (tuple/struct/enum/array member recursion) and
# type_may_carry_borrow gained structural tuple-elem/array-elem recursion.
#   fail/bc_argcomp_aggr_tuple_dangle   — `pick((&tmp, 1))` refuses E0597
#   fail/bc_argcomp_aggr_enum_dangle    — `pick(MyOpt::Some(&tmp))` refuses E0597
#   pass/bc_argcomp_aggr_lit_admit      — all three spellings at fn scope admit
# The STRUCT spelling (`pick(H { r: &tmp })`) is the remaining residual — a
# plain struct's raw-ref field is invisible to type_may_carry_borrow without a
# holds-any-ref fixpoint set (task #71, repro m16_dangle_structlit_real).
# MISS-1 also paid: the tv_build pin's stand-in returned i64 (vacuous — a
# scalar cannot tie under ANY widening); it now returns a #[borrow_carrying]
# enum, the faithful WAny shape.
# 2026-08-18, +5/+5/0 (7280/3597/36 -> 7285/3602/36), predicted before the
# reconfigure and met exactly: task #70, the borrow/move formed by COMPOSITION
# in ARGUMENT position — the over-refusal direction fully, the permissive
# direction for the nested-Call spelling (see the entry above for the
# aggregate-literal spellings). Five fixtures, none of
# them under the wql_*/deem_* globs, so logos_09_plan_ground_census and
# logos_09_pull_shape keep their populations.
#   (b) OVER-REFUSAL — take_ref_borrows dropped `record_only` on its aggregate
#   recursion, so apply_call_outparam_rules' re-walk visited the composed
#   argument a SECOND time and stdlib `Option<T>::replace` ("return
#   replace_ref(self, Option::Some(value))") reported a double move of a value
#   moved exactly once. Control-reverted: stash -> rc 1 on all three probes,
#   unstash -> rc 0.
#     tests/logos/pass/bc_argcomp_replace_admit.logos       (instantiates AND
#       CALLS .replace on a #[borrow_carrying] T with an owned Vec field; the
#       runtime answer is asserted, so a wrongly-admitted move shows as a wrong
#       exit code)
#     tests/logos/fail/bc_argcomp_use_after_move_fail.logos (abuse direction:
#       `value` used AFTER the composition moved it must still refuse)
#   (a) PERMISSIVE — a nested borrow-forming call in argument position
#   (`pick1(&arr[0..2])`, the arg being the fat-typed slice_get_range Call
#   node) deposited no sources, so the dangle was admitted at rc 0 while the
#   one-level twin bc_d1res_r2_sliceform_dangle refused.
#     tests/logos/fail/bc_argcomp_nested_call_dangle.logos
#     tests/logos/pass/bc_argcomp_nested_call_admit.logos
#     tests/logos/pass/bc_argcomp_tvbuild_byvalue_fat_admit.logos  (the
#       by-value fat COPY exemption the per-arg filters exist for — before this
#       task NOTHING under tests/ pinned it; grep found tv_build in exactly one
#       stdlib file and zero tests, and no bc_* fixture mentioned as_str)
# 2026-08-18, +2/+2/0 (7278/3595/36 -> 7280/3597/36), predicted before the
# reconfigure: the #62 (D7) fix — the metaprog dispatch loop no longer advances
# next_delta_start across an uncached deferral-retry round (whose collected
# logos.gen registrations were discarded with no snapshot), and resolve_type's
# metaprog TYPE_REF swallow records an `emitted-type` pending in item-signature
# position instead of freezing Kind::Error into a struct field ({8,8} layout
# guess, `[declined] <kind 46>`, LOGOS_VERIFY_LAYOUT abort sema 8 vs
# llvm::DataLayout 72). The pair:
#   tests/logos/pass/deem_emitted_struct_field_layout.logos   (a LATE emitted
#     chunk holds the emitted family LeafWalk struct as a FIELD; every pass
#     test compiles under LOGOS_VERIFY_LAYOUT=1, so this was rc 134 pre-fix —
#     control-reverted: stash -> 134 with sema 8 vs 72 on test.LwFieldProbe,
#     unstash -> green, exit 0)
#   tests/logos/fail/emitted_struct_field_unknown_fail.logos  (same position,
#     a NEVER-emitted type: refuses by name, `unknown type
#     'HsNeverEmittedD7Probe'` — the admit's separation from a blanket admit)
# 2026-08-18, +4/+4/0 (7274/3591/36 -> 7278/3595/36), predicted before the
# reglob: the D1-residuals slice §B6 pair (task #51, R1/R2 — the store side
# records sources for slice-forming call args via forms_borrow_at_call, plus
# the SliceLit/SliceIndex read arms in collect_ref_sources_paths):
#   tests/logos/fail/bc_d1res_r1_sliceindex_dangle.logos  (o = sl[0] escaping
#     the slice's array's scope refuses E0597 like the array twin)
#   tests/logos/pass/bc_d1res_r1_sliceindex_admit.logos   (fn-scope twin, exit 7)
#   tests/logos/fail/bc_d1res_r2_sliceform_dangle.logos   (o = &arr[0..2] in an
#     inner scope refuses E0597 — the slice-forming spelling itself)
#   tests/logos/pass/bc_d1res_r2_sliceform_admit.logos    (fn-scope twin, exit 1)
# 2026-08-18, +3/+3/0 (7271/3588/36 -> 7274/3591/36), predicted before the
# reconfigure: the D3 miscompile fix (task #50, thin-ref-to-struct pattern
# bindings + tuple/array-index fat receivers) lands with
#   tests/logos/pass/bc_d3_struct_pat_fat_mut_recv.logos   (the z3 repro, exit 1)
#   tests/logos/pass/bc_d3_thin_ref_binding_class.logos    (10-leg class matrix)
#   tests/logos/pass/zone_mut_tupleidx_fat_recv.logos      (t.0.v through a
#     tuple-held fat &mut — the receiver-path defect the aggregate admit
#     fixture used to carry as a commented NON-assertion; that comment is now a
#     real `if t.0.v != 42i64 { return 9; }` in
#     tests/logos/pass/zone_mut_thin_source_admits_aggregate.logos)

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
