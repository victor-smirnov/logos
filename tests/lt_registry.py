"""lt_registry — THE TEST REGISTRY: every test `lt` knows about, as data.

This module replaces the test REGISTRATION that used to live in CMake
(tests/logos/CMakeLists.txt, cmake/LogosTestTiers.cmake, the root CMakeLists.txt
and src/writ, src/jit, src/compiler, tools/peg_gen_cpp). CMake still BUILDS
everything; it no longer has to describe the tests. `scripts/lt_discover.py`
walks the tree with the rules below and produces one record per test.

Two kinds of entry:

  SINGLETONS   hand-registered tests (gates, lints, exercisers): a literal
               command line with placeholders, grouped by the build directory
               ctest ran them from (their WORKING_DIRECTORY).
  FAMILIES     functions that derive tests from the source tree LIVE — a new
               `tests/logos/pass/foo.logos` + `foo.expected` is a test the next
               time discovery runs, with nothing edited here.

Placeholders in SINGLETONS (expanded with str.format by lt_discover):
  {src}  source root            {build} build root
  {tsrc} {src}/tests/logos      {tbin}  {build}/tests/logos
  {logosc} {build}/bin/logosc   {libdir} LOGOS_BUILD_LIB_DIR = {build}/lib/logos
  {bash} {python3}  absolute paths of the interpreters (ctest resolves a bare
                    command name through PATH; so does discovery)
  {cmake} {ctest} {filecheck}  configure-time tool paths (read from the build's
                    CMakeCache.txt when present, else searched like CMake does)

THE TIER RULE (formerly cmake/LogosTestTiers.cmake, `_logos_tier_audit_dir`): every test
that is not `corpus` carries EXACTLY ONE of TIER_VALUES. That used to be a
configure-time FATAL_ERROR; `lt_discover.check_tiers` enforces it now.
"""
import os
import re

TIER_VALUES = ("tier_commit", "tier_full", "tier_explicit")

# Every gate in tests/logos that runs a logosc-produced program gets this.
LIB = ["LOGOS_LIB_DIR={libdir}"]


def T(name, command, labels, timeout=None, env=(), processors=1, skip_rc=None,
      fixtures_required=(), fixtures_setup=(), disabled=0):
    """One test. `labels` is CMake's `"a;b;c"` spelling; `timeout=None` means
    no TIMEOUT property (lt's default applies)."""
    return dict(name=name, command=list(command), labels=labels.split(";"),
                timeout=timeout, env=list(env), processors=processors,
                skip_rc=skip_rc, fixtures_required=list(fixtures_required),
                fixtures_setup=list(fixtures_setup), disabled=disabled)


# ═════════════════════════════════════════════════════════════════════════════
# SINGLETONS — keyed by WORKING_DIRECTORY relative to the build root ('.' is the
# root). The comment above each entry is the first line of the comment block
# that precedes its add_test() in the CMakeLists it came from; the argument for
# each gate is in the gate script itself.
# ═════════════════════════════════════════════════════════════════════════════
SINGLETONS = {
    '.': [  # root CMakeLists.txt
        # Coercion arc S2: the type-mismatch verdict is emitted by exactly one
        T('lint_mismatch_monopoly',
          ['{bash}', '{src}/scripts/lint-mismatch-monopoly.sh'],
          'lint;tier_full'),
        # Class-B arc: a borrow is deposited by exactly one function
        T('lint_record_borrow_monopoly',
          ['{bash}', '{src}/scripts/lint-record-borrow-monopoly.sh'],
          'lint;tier_full'),
    ],
    'src/writ': [  # src/writ/CMakeLists.txt
        T('writ_exerciser_smoke',
          ['{build}/src/writ/writ_exerciser_smoke'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_containers',
          ['{build}/src/writ/writ_exerciser_containers'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_views',
          ['{build}/src/writ/writ_exerciser_views'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_clone',
          ['{build}/src/writ/writ_exerciser_clone'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_document',
          ['{build}/src/writ/writ_exerciser_document'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_codec',
          ['{build}/src/writ/writ_exerciser_codec'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_text',
          ['{build}/src/writ/writ_exerciser_text'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_multi_arena',
          ['{build}/src/writ/writ_exerciser_multi_arena'],
          'writ;cpp;tier_full'),
        T('writ_exerciser_producer',
          ['{build}/src/writ/writ_exerciser_producer'],
          'writ;cpp;tier_full'),
    ],
    'src/jit': [  # src/jit/CMakeLists.txt
        T('jit_smoke',
          ['{build}/src/jit/exerciser_jit_smoke'],
          'jit;cpp;tier_full'),
        T('jit_host_call',
          ['{build}/src/jit/exerciser_jit_host_call'],
          'jit;cpp;tier_full'),
    ],
    'src/compiler': [  # src/compiler/CMakeLists.txt
        T('trait_engine_test',
          ['{build}/src/compiler/trait_engine_test'],
          'trait_engine;cpp;tier_full'),
        T('dl_test',
          ['{build}/src/compiler/dl_test'],
          'dl;cpp;tier_commit'),
        # Souffle is the oracle; without it the test reports SKIPPED (rc 77), never
        T('dl_souffle_oracle',
          ['{bash}', '{src}/tests/dl/oracle.sh', '{build}/src/compiler/logos-dl', '{src}/tests/dl/cases', '{src}/src/compiler/dl/rules'],
          'dl;cpp;tier_commit', skip_rc=77),
    ],
    'tools/peg_gen_cpp': [  # tools/peg_gen_cpp/CMakeLists.txt
        T('peg_ast_header_fresh',
          ['{cmake}', '-DPEG={build}/tools/peg_gen_cpp/peg_gen_cpp', '-DGRAMMAR={src}/tools/peg_gen_cpp/grammars/logos.peg', '-DHEADER={src}/include/logos/compiler/ast.hpp', '-DWORK={build}/tools/peg_gen_cpp/ast_fresh_check', '-P', '{src}/tools/peg_gen_cpp/check_ast_header.cmake'],
          'lint;tier_full'),
        T('peg_schema_mirror_el',
          ['{build}/tools/peg_gen_cpp/peg_gen_cpp', '{src}/stdlib/mem/wql/grammars/el.peg', '--check-schema', '{src}/stdlib/mem/wql/ir.logos'],
          'lint;tier_full'),
        T('peg_schema_mirror_wql',
          ['{build}/tools/peg_gen_cpp/peg_gen_cpp', '{src}/stdlib/mem/wql/grammars/wql.peg', '--check-schema', '{src}/stdlib/mem/wql/plan.logos', '--check-schema', '{src}/stdlib/mem/wql/ir.logos'],
          'lint;tier_full'),
    ],
    'tests/logos': [  # tests/logos/CMakeLists.txt
        # THE REGISTRATION CENSUS
        T('logos_00_corpus_registration',
          ['{tsrc}/corpus_registration_gate.sh', '{src}', '{tsrc}/unregistered.ledger'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=120),
        # BORROW-FLOW MASKS (D1 round 11 / X3)
        T('logos_00_bc_flow_mask',
          ['{tsrc}/bc_flow_mask_gate.sh', '{logosc}', '{tsrc}/bc_flow_mask'],
          'logos;fail;suite_diagnostics;tier_commit', timeout=300, env=LIB),
        # THE RECORDED LESSONS ABOUT LYING GATES, AS RULES
        T('logos_00_gate_lint',
          ['{python3}', '{tsrc}/gate_lint.py', '--selftest', '--build-dir={build}', '{src}'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300),
        # THE PROBE LOG'S LINKS MUST STILL RESOLVE
        T('logos_00_probe_log_lint',
          ['{python3}', '{src}/scripts/probe-log-lint.py', '{src}'],
          'logos;pass;suite_semantic_core;tier_commit;lint', timeout=120),
        # THE SEPARATOR-CLASS CLAIMS AN EXIT CODE CANNOT SEE
        T('logos_00_sep_symbol_shape',
          ['{tsrc}/sep_symbol_shape_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=120, env=LIB),
        # A BUILTIN NAME IS NOT AN IDENTITY: THE HALF AN EXIT CODE CANNOT SEE
        T('logos_00_intrinsic_bare_name_binding',
          ['{tsrc}/intrinsic_bare_name_binding_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=120, env=LIB),
        # R2's VERDICT MUST REACH THE EXIT CODE, AND NO fail TEST CAN SEE THAT
        T('logos_00_mlir_gen_exit_code',
          ['{tsrc}/mlir_gen_exit_code_gate.sh', '{logosc}', '{tsrc}/fail', '{src}/src/compiler'],
          'logos;fail;suite_semantic_core;tier_commit', timeout=120, env=LIB),
        # THE OPEN mlir-gen SELF-DIAGNOSES — A LEDGER THAT CAN ONLY SHRINK
        T('logos_00_mlir_gen_bug_ledger',
          ['{tsrc}/mlir_gen_bug_ledger_gate.sh', '{logosc}', '{tsrc}/mlir_gen_bug.ledger', '{src}'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # THE BORROW-CHECK HOLES THE rustc IMPORT FOUND ARE A LEDGER
        T('logos_00_bc_admits_ledger',
          ['{tsrc}/bc_admits_ledger_gate.sh', '{logosc}', '{tsrc}/bc_admits.ledger', '{src}', '{tsrc}/bc_admits_blocked.ledger'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # THE LEDGERS ARE GENERATED FROM THE GITHUB ISSUES (2026-09-17) AND MUST NOT BE
        T('logos_00_ledger_sync',
          ['{tsrc}/ledger_sync_gate.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # THE SOUNDNESS QUEUE — OPEN DEFECTS THAT COMPILE CLEAN, HELD LIKE THE BC LEDGER
        T('logos_00_soundness_queue',
          ['{tsrc}/soundness_queue_gate.sh', '--roster', '{logosc}', '{tsrc}/soundness_queue.ledger', '{src}'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=120, env=LIB),
        # `type_apply` — THE ONLY name→Type BRIDGE, AND IT ABORTS
        T('logos_00_type_apply_trap_ledger',
          ['{tsrc}/type_apply_trap_gate.sh', '{logosc}', '{tsrc}/type_apply_trap.ledger', '{tsrc}/type_apply_trap', '{libdir}'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # THE SEPARATOR CLASS CANNOT REGROW SILENTLY
        T('logos_00_separator_split_lint',
          ['{tsrc}/separator_split_lint.sh', '{src}/src/compiler', '{tsrc}/separator_split.ledger'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # A LOOKUP KEY IS NOT AN IDENTITY
        T('logos_00_key_identity_lint',
          ['{tsrc}/key_identity_lint.sh', '{src}', '{tsrc}/key_identity.ledger'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # AST KEY CENSUS (ADR 0030 H0)
        T('logos_00_ast_key_census',
          ['{python3}', '{tsrc}/ast_key_census.py', '{src}/tools/peg_gen_cpp/grammars/logos.peg'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # ONE SCHEDULER: A REGISTERED TEST MAY NOT FAN OUT ITS OWN
        T('logos_00_one_scheduler_lint',
          ['{tsrc}/one_scheduler_lint.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # A STDLIB DEFAULT METHOD BODY MAY NOT BE LOWERED INTO A USER IMPL
        T('logos_00_trait_homonym_no_injected_symbol',
          ['{tsrc}/trait_homonym_symbol_gate.sh', '{logosc}', '{libdir}'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # THE ROOT COORDINATE HAS ONE DEFINITION, AND ONE MEMBERSHIP
        T('logos_00_root_coord_lint',
          ['{tsrc}/root_coord_lint.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # A CENSUS NOBODY GATES IS A DOCUMENT ABOUT THE PAST
        T('logos_00_census_pin',
          ['{tsrc}/census_pin_gate.sh', '{src}', '{src}/docs/deem-interpreter-deletion-census.md', '{src}/scripts/lt_discover.py', '{build}'],
          'logos;lint;suite_lint;tier_commit', timeout=300),
        # THE FOUR WIDEST-TRIGGER POPULATION PINS, ON THE PER-COMMIT PATH
        T('logos_00_population_pin_lint',
          ['{python3}', '{tsrc}/population_pin_lint.py', '--selftest', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # ADR 0025 S4 — THE SHARED ACCUMULATOR FRAGMENTS HAVE ONE DEFINITION SITE.
        T('logos_00_agg_frag_single_site',
          ['{tsrc}/agg_frag_single_site_gate.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # The LANGUAGE SPEC's Evidence lines, checked for their nouns. `docs/spec/*.md`
        T('logos_00_spec_path_lint',
          ['{tsrc}/spec_path_lint.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=300),
        # #112: THE `*((&x) as *const T)` DUPLICATE-OWNER CLASS
        T('logos_00_stdlib_raw_dup_lint',
          ['{tsrc}/stdlib_raw_dup_lint.sh', '{src}', '{tsrc}/stdlib_raw_dup.ledger'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # #327: `WAny::Ref(p)` takes any pointer — a construction outside anyval.logos
        T('logos_00_wany_ref_ctor_lint',
          ['{tsrc}/wany_ref_ctor_lint.sh', '{src}'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # THE SHARED-&T UB RULING CANNOT DRIFT BACK INTO PROSE
        T('logos_00_shared_ref_ub_lint',
          ['{tsrc}/shared_ref_ub_lint.sh', '{src}', '{tsrc}/shared_ref_ub.ledger', '{tsrc}/shared_ref_ub_claims.ledger'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # THE FREEZE LATTICE'S AXIS POPULATION IS DERIVED FROM THE PREDICATE
        T('logos_00_freeze_arm_coverage',
          ['{python3}', '{tsrc}/freeze_arm_coverage.py', '{src}', '--ledger', '{tsrc}/freeze_arms.ledger', '--check', '{tsrc}/ir/param_attrs_freeze_lattice.check'],
          'logos;lint;suite_lint;tier_commit', timeout=60),
        # THE OPEN LAYOUT DECLINES — A LEDGER THAT CAN ONLY SHRINK (see LAYOUT_DECLINE_LEDGER)
        T('logos_00_layout_decline_ledger',
          ['{tsrc}/layout_decline_ledger_gate.sh', '{logosc}', '{tsrc}/layout_decline.ledger', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # ABI CLOSURE
        T('logos_00_abi_reachability',
          ['{tsrc}/abi_closure_gate.sh', '{logosc}', '{libdir}', '{src}/abi/logos.abi-closure-exempt'],
          'logos;lint;suite_lint;tier_commit', timeout=120),
        # Lint: inline `LogosTypeBuilder` Struct/Enum/ZonedStruct sites outside
        T('logos_08_lint_inline_typebuilder',
          ['{src}/tools/check_typebuilder_lint.sh', '{src}'],
          'logos;lint;suite_lint;tier_full', timeout=10),
        # Import-set gate: a synth module's USES must be a SET. Three independent
        T('logos_09_no_dup_use_in_synth',
          ['{tsrc}/no_dup_use_gate.sh', '{logosc}', '{tsrc}/pass/wql_first_fold_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # Plan SIZE gate (ADR 0024 S4): a source's size is a fact the plan ASKS FOR, and
        T('logos_09_plan_size_asked',
          ['{tsrc}/plan_size_gate.sh', '{logosc}', '{tsrc}/pass/deem_source_size.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # CONTAINER ACCESS-PATH gate (ADR 0024 S6): WHICH rows the container was asked
        T('logos_09_ctr_access_path',
          ['{tsrc}/ctr_access_path_gate.sh', '{logosc}', '{tsrc}/pass/deem_source_size.logos', '{tsrc}/pass/deem_hashmap_source.logos', '{tsrc}/pass/container_item_e2e.logos', '{tsrc}/pass/deem_cross_domain_join.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=180, env=LIB),
        # PLAN DECISIONS' GROUNDS AS RULES OVER FACTS (why.logos): join strategy and
        # access path — the rule that fired and the negative explanation, asserted on
        # LOGOS_TRACE_PLAN=facts.
        T('logos_09_plan_ground_facts',
          ['{tsrc}/plan_ground_facts_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_full', timeout=120, env=LIB),
        # ADR 0025 S2 — MATERIALIZATION AS A NAMED PLAN NODE (§4).
        T('logos_09_plan_nodes',
          ['{tsrc}/plan_nodes_gate.sh', '{logosc}', '{tsrc}/pass/deem_join_step_reread.logos', '{tsrc}/pass/deem_batch_scan_drain.logos', '{tsrc}/pass/deem_hashmap_source.logos', '{tsrc}/pass/deem_cross_domain_join.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # ADR 0025 S2 — THE REFUSAL CENSUS, RE-DERIVED BY MACHINE (§4's gate, both halves).
        T('logos_09_plan_ground_census',
          ['{tsrc}/plan_ground_census_gate.sh', '{logosc}', '{tsrc}/pass', '{src}/stdlib/mem/wql/why.logos', '{src}/stdlib/mem/wql/access_plan.logos', '{tbin}/facts'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB, fixtures_required=['logos_facts_glob']),
        # ADR 0025 R-H — CRITERION 2 GETS A GATE (2026-08-16).
        T('logos_09_pull_shape',
          ['{tsrc}/pull_shape_gate.sh', '{logosc}', '{tsrc}/pass', '{tbin}/facts'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB, fixtures_required=['logos_facts_glob']),
        # ADR 0025 §12 — THE DIRECT DOOR CENSUS OVER THE WHOLE PASS CORPUS (2026-08-19).
        T('logos_09_direct_door_census',
          ['{tsrc}/direct_door_census_gate.sh', '{logosc}', '{tsrc}/pass', '{tbin}/facts'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB, fixtures_required=['logos_facts_all']),
        # ADR 0025 S3a — THE IMPORT PAIR THE `Buffer` LANDING NEEDS.
        T('logos_09_drain_import_pair',
          ['{tsrc}/drain_import_pair_gate.sh', '{logosc}', '{tsrc}/pass', '{src}/stdlib/mem/wql/wql.logos', '{src}/stdlib/mem/wql/rexpr_walk.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # ADR 0025 S3f — THE READ-ONCE DECISION, PINNED IN BOTH DIRECTIONS.
        T('logos_09_drain_read_once_pair',
          ['{tsrc}/drain_read_once_pair_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # ADR 0025 §2/S6 GATE — THE BATCH LAYOUT, BOTH DIRECTIONS.
        T('logos_09_rowmajor_batch_layout',
          ['{tsrc}/rowmajor_batch_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # ADR 0025 CRITERION 3'S INSTRUMENT — THE NON-BLIND ONE (#47 entry ticket).
        T('logos_09_rc_seam_hot_path',
          ['{tsrc}/rc_seam_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_full', timeout=600, env=LIB),
        # ADR 0025 S3 GATE — THE ORDER ELISION, ADMITTED AND REFUSED.
        T('logos_09_order_elision_pair',
          ['{tsrc}/order_elision_pair_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # ADR 0025 §8 GATE — THE GROUP FRAME IS NAMED, GROUNDED AND PRICED.
        T('logos_09_group_frame_naming',
          ['{tsrc}/group_frame_naming_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # ADR 0025 S3-desc GATE — THE BACKWARD WALK (Victor's traversal axis, §3).
        T('logos_09_order_desc_pair',
          ['{tsrc}/order_desc_pair_gate.sh', '{logosc}', '{tsrc}/pass'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=300, env=LIB),
        # ADR 0025 S1 GATE 1 — SLICE-SOURCE CODEGEN, BYTE-COMPARABLE.
        T('logos_09_slice_scan_codegen',
          ['{tsrc}/slice_scan_codegen_gate.sh', '{logosc}', '{tsrc}/pass/wql_slice_scan_shape.logos', '{tsrc}/slice_scan_shape.golden'],
          'logos;pass;suite_semantic_core;tier_full', timeout=120, env=LIB),
        # ADR 0025 S1 GATE 2 — THE MEMORIA SCAN'S ASYMPTOTICS (§5), MEASURED.
        T('logos_09_ctr_leaf_descent',
          ['{tsrc}/ctr_leaf_descent_gate.sh', '{logosc}', '{tsrc}/pass/ctr_leaf_descent_count.logos', '{libdir}', '{tsrc}/callgrind_calls.py', 'bt_seek_at', 'bt_cur_next'],
          'logos;pass;suite_semantic_core;tier_full', timeout=600, env=LIB),
        # #121-A — THE DROP-FLAG KEYSPACE, MEASURED BY AN INSTRUMENT THE PROGRAM DOES
        T('logos_09_cond_move_field_valgrind',
          ['{tsrc}/cond_move_field_valgrind_gate.sh', '{logosc}', '{tsrc}/pass/cond_move_field_overlap.logos', '{libdir}'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # 2026-09-06k — A BLOCK IN EXPRESSION POSITION RUNS ITS OWN SCOPE-EXIT DROPS.
        T('logos_09_blockexpr_scope_drop_valgrind',
          ['{src}/tests/lattice/valgrind/leak_gate.sh', '{logosc}', '{tsrc}/pass/blockexpr_scope_drop_valgrind.logos', '{libdir}', '40'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # THE CONTROL: the arm that MOVES its buffer out of the block. An over-reaching
        T('logos_09_blockexpr_scope_drop_format_ctl_valgrind',
          ['{src}/tests/lattice/valgrind/leak_gate.sh', '{logosc}', '{tsrc}/pass/blockexpr_scope_drop_format_ctl.logos', '{libdir}', '40'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # #123 — THE SAME GATE SCRIPT over the `#[no_auto_drop]` storage-site sweep.
        T('logos_09_no_auto_drop_valgrind',
          ['{tsrc}/cond_move_field_valgrind_gate.sh', '{logosc}', '{tsrc}/pass/no_auto_drop_container.logos', '{libdir}'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # The ADMIT half under the same instrument. The suppression fixture and its
        T('logos_09_no_auto_drop_ctl_valgrind',
          ['{tsrc}/cond_move_field_valgrind_gate.sh', '{logosc}', '{tsrc}/pass/no_auto_drop_container_ctl.logos', '{libdir}'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # The SAME gate over the POSITIONAL family (ADR 0025 S1b). Not a formality: S1b
        T('logos_09_ctr_vec_leaf_descent',
          ['{tsrc}/ctr_leaf_descent_gate.sh', '{logosc}', '{tsrc}/pass/ctr_vec_leaf_descent_count.logos', '{libdir}', '{tsrc}/callgrind_calls.py', 'btvec_seek', 'btvec_cur_next'],
          'logos;pass;suite_semantic_core;tier_full', timeout=900, env=LIB),
        # JOIN-ORDER gate (ADR 0024 S4): the emitted fn carries BOTH join orders and one
        T('logos_09_join_order_either_side',
          ['{tsrc}/join_order_gate.sh', '{logosc}', '{tsrc}/pass/wql_join_order_dyn_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # PREPARED-PLAN gate (ADR 0024 S4i): the plan is a runtime VALUE — a type, a
        T('logos_09_prepared_plan_surface',
          ['{tsrc}/prepared_plan_gate.sh', '{logosc}', '{tsrc}/pass/wql_prepared_plan_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # DEFERRED-PLAN gate (ADR 0024 S4j): the plan's SECOND decision point — inside
        T('logos_09_deferred_plan_second_point',
          ['{tsrc}/deferred_plan_gate.sh', '{logosc}', '{tsrc}/pass/wql_deferred_plan_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # INCREMENTAL-ELIGIBILITY gate (P1): the set of queries that gain an incremental
        T('logos_09_incr_eligibility_population',
          ['{tsrc}/incr_eligibility_gate.sh', '{logosc}', '{tsrc}/pass/wql_incr_eligibility_matrix.logos'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=60, env=LIB),
        # INCREMENTAL RETRACTION gate — the same shape, one axis in. Eligibility asks
        T('logos_09_incr_retraction_population',
          ['{tsrc}/incr_retraction_gate.sh', '{logosc}', '{tsrc}/pass/wql_incr_retract_matrix.logos'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=60, env=LIB),
        # SCC DRIVER COUNT gate (P3c): one cross-epoch semi-naive driver per admitted
        T('logos_09_incr_scc_driver_count',
          ['{tsrc}/incr_scc_driver_gate.sh', '{logosc}', '{tsrc}/pass/wql_incr_rel_mutrec_epochs.logos', '{tsrc}/pass/wql_incr_rel_rec_epochs.logos'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=60, env=LIB),
        # MUTREC REFUSAL gate: the handle is EMITTED and the RETRACTION, specifically, is
        T('logos_09_incr_mutrec_refusal',
          ['{tsrc}/incr_mutrec_refusal_gate.sh', '{logosc}', '{tsrc}/pass/wql_incr_rel_mutrec_epochs.logos', '{tsrc}/pass/wql_incr_rel_rec_epochs.logos'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=60, env=LIB),
        # MULTI-ORDER JOIN gate (ADR 0024 S4k): the join-order axis is a decision over a
        T('logos_09_join_order_beyond_pair',
          ['{tsrc}/join_order_multi_gate.sh', '{logosc}', '{tsrc}/pass/wql_join_order_multi_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # KEY-ORDER LICENCE gate (ADR 0024 S4k, C1): carrying more than one nest is licensed
        T('logos_09_join_order_key_fidelity',
          ['{tsrc}/join_order_key_fidelity_gate.sh', '{logosc}', '{tsrc}/pass/wql_join_order_key_fidelity_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # WIDE-KEY INDEX TIER (ADR 0024 S6 — the value domain, the decision side).
        T('logos_09_wide_key_index_tier',
          ['{tsrc}/wide_key_index_tier_gate.sh', '{logosc}', '{tsrc}/pass/wql_join_wide_key_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=120, env=LIB),
        # SHADOWED-COLUMN gate (ADR 0024 S3/S6 + S4k C1): a column's type is a fact about a
        T('logos_09_wql_shadowed_column',
          ['{tsrc}/wql_shadowed_column_gate.sh', '{logosc}', '{tsrc}/pass/wql_shadowed_column_e2e.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # COLUMN-DECLARATION LAYER gate (the value-domain arc, D2). `run_test.sh`
        T('logos_09_wql_column_decl_layer',
          ['{tsrc}/wql_column_decl_layer_gate.sh', '{logosc}', '{tsrc}/fail'],
          'logos;pass;suite_semantic_core;tier_full', timeout=120, env=LIB),
        # Renderer CONTENT gate: the gendir gate above only proves the dump reparses,
        T('logos_09_render_type_fidelity',
          ['{tsrc}/render_type_fidelity.sh', '{logosc}', '{tsrc}/pass/quote_item_dyn_typearg.logos'],
          'logos;pass;suite_semantic_core;tier_full', timeout=60, env=LIB),
        # WHY-SIZE gate (ADR 0024 S4q): the rendered justification reaches the artifact only
        T('logos_09_why_size_reachability',
          ['{tsrc}/why_size_gate.sh', '{logosc}'],
          'logos;pass;suite_semantic_core;tier_full', timeout=180, env=LIB),
        # Diagnostics: --diag-format=json + structured exit codes (B0.2).
        T('logos_09_diag_json_format',
          ['{src}/tests/diag/json_format.sh', '{logosc}'],
          'logos;diag;suite_diag;tier_full', timeout=120),
        # ── lforge (`if (TARGET lforge)`: tools/lforge defines the target whenever
        # logosc exists, and this whole file returns early without logosc) ──
        # lforge end-to-end smoke: build, run, clean a hello-world fixture (B0.5).
        T('logos_10_lforge_smoke',
          ['{src}/tests/lforge/smoke.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B1.1: multi-target builds (lib + bin with deps), topo-sort, --release.
        T('logos_11_lforge_multitarget',
          ['{src}/tests/lforge/multitarget.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B1.5: lforge test discovers tests/*.logos, builds + runs each.
        T('logos_12_lforge_test_cmd',
          ['{src}/tests/lforge/test_cmd.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=90),
        # B1.2: mtime-based incremental rebuild.
        T('logos_13_lforge_incremental',
          ['{src}/tests/lforge/incremental.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B1.6: lforge install <prefix> places artifacts under <prefix>/{bin,lib}.
        T('logos_14_lforge_install',
          ['{src}/tests/lforge/install.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # `lforge build --dump-metacall` writes metafn-emitted ASTs as Logos source.
        T('logos_14_lforge_dump_metacall',
          ['{src}/tests/lforge/dump_metacall.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=90),
        # B1.7: logosc --only-file <path> per-file emit.
        T('logos_15_emit_file',
          ['{src}/tests/lforge/emit_file.sh', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # Archive integrity: a package must never vanish from a module archive
        T('logos_15_archive_integrity',
          ['{src}/tests/lforge/archive_integrity.sh', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=300),
        # ADR 0014 slice-1: logosc --emit-docs writes a .docwr doc-facts container.
        T('logos_15_emit_docs',
          ['{src}/tests/lforge/emit_docs.sh', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # ADR 0014 slice-2: tools/docgen resolves a .docwr into docs.json (impl edges →
        T('logos_15_doc_json',
          ['{src}/tests/lforge/doc_json.sh', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=90),
        # B1.3: lforge fans out per-file lib compilation in parallel + per-file
        T('logos_16_lforge_parallel',
          ['{src}/tests/lforge/parallel.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=90),
        # ADR 0014 slice-3: `lforge doc` builds libs, --emit-docs, resolves docs.json.
        T('logos_16_lforge_doc',
          ['{src}/tests/lforge/doc_cmd.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=120),
        # B1.8: lib targets accept C and asm sources alongside Logos files.
        T('logos_17_lforge_c_asm',
          ['{src}/tests/lforge/c_asm.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=90),
        # B1.9: dogfood — build stdlib_rt + stdlib_fibers via lforge and verify
        T('logos_18_lforge_dogfood_rt',
          ['{src}/tests/lforge/dogfood_rt.sh', '{build}/bin/lforge', '{logosc}', '{libdir}', '{src}/stdlib/rt'],
          'logos;lforge;suite_lforge;tier_full', timeout=120),
        # B2: project-level external deps via local paths.
        T('logos_19_lforge_external_dep',
          ['{src}/tests/lforge/external_dep.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B2.5: project-level external deps via git URLs (uses a local file://
        T('logos_20_lforge_git_dep',
          ['{src}/tests/lforge/git_dep.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B3a: transitive external deps -- a closure of local-path projects
        T('logos_21_lforge_transitive_dep',
          ['{src}/tests/lforge/transitive_dep.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B3a: lockfile records git pins; second build uses them without
        T('logos_22_lforge_lockfile',
          ['{src}/tests/lforge/lockfile.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B3b: MVS conflict detection + `lforge update` re-resolves.
        T('logos_23_lforge_mvs_update',
          ['{src}/tests/lforge/mvs_update.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B4: content-addressed build cache shared across consumers.
        T('logos_24_lforge_build_cache',
          ['{src}/tests/lforge/build_cache.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # B5: replace: directive + requires_logos: ABI floor.
        T('logos_25_lforge_replace_and_floor',
          ['{src}/tests/lforge/replace_and_floor.sh', '{build}/bin/lforge', '{logosc}', '{libdir}'],
          'logos;lforge;suite_lforge;tier_full', timeout=60),
        # PLAN INDEPENDENCE, CORPUS-WIDE (ADR 0024 S4r)
        T('logos_09_plan_independence',
          ['{tsrc}/plan_independence_gate.sh', '{logosc}', '{tsrc}/pass', '{tsrc}/run_test.sh', '12'],
          'logos;pass;suite_semantic_core;tier_full', timeout=900, env=['LOGOS_LIB_DIR={libdir}', 'LOGOS_TEST_LIB_DIR={tbin}'], processors=12),
        # Layout-engine agreement (see layout_engine_agreement_gate.sh for the full
        T('logos_09_layout_engine_agreement',
          ['{tsrc}/layout_engine_agreement_gate.sh', '{logosc}', '{libdir}'],
          'logos;pass;suite_semantic_core;tier_full', timeout=300, env=LIB),
        # The compile-UNIT partition and the ORDER it derives (unit_graph.cpp). Before
        T('logos_09_unit_graph',
          ['{tsrc}/unit_graph_gate.sh', '{logosc}', '{libdir}', '{src}', '{tbin}/unit_graph_gate'],
          'logos;pass;suite_semantic_core;tier_full', timeout=900, env=LIB),
        # THE DYNAMIC/INCREMENTAL VALUE DOMAIN (`RtVal`), AND THE LEDGER OF ITS `_`
        T('logos_09_rtval_domain',
          ['{tsrc}/rtval_domain_gate.sh', '{src}/stdlib/mem/deem', '{src}/abi/logos.abi', '{tsrc}/rtval_fallback.ledger'],
          'logos;pass;suite_semantic_core;tier_commit', timeout=60),
        # SHARDED EMISSION (LOGOS_EMIT_SHARDS / LOGOS_EMIT_WORKERS). MEASURED 2026-08-04:
        T('logos_09_emit_shards',
          ['{tsrc}/emit_shards_gate.sh', '{logosc}', '{libdir}', '{src}', '{tbin}/emit_shards_gate'],
          'logos;pass;suite_semantic_core;tier_full', timeout=600, env=LIB),
    ],
}


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 1 — THE CORPUS (tests/logos/CMakeLists.txt: the pass/fail loops over
# `LOGOS_SUITE_ORDER` and `logos_add_grouped_test`).
#
# One test per `*.expected` with a sibling `.logos`, from
#   tests/logos/pass/*.expected   + tests/imported/pass/**/*.expected  (MODE pass)
#   tests/logos/fail/*.expected   + tests/imported/fail/**/*.expected  (MODE fail)
# named  logos_<NN>_<suite>_<mode>_<BASE>  where BASE is CMake's NAME_WE (the
# file name up to its FIRST dot).
# ═════════════════════════════════════════════════════════════════════════════

# `LOGOS_SUITE_ORDER`, with the two-digit prefix `logos_add_grouped_test` gives
# each (anything else is "06").
SUITE_PREFIX = {
    "smoke": "01",
    "semantic_core": "02",
    "ownership": "03",
    "advanced_features": "04",
    "integration": "05",
    "diagnostics": "06",
}

# `LOGOS_SMOKE_TESTS`
SMOKE_TESTS = {
    # pass: core language features — one representative per category
    "arith_precedence", "fn_default_ret", "if_expr", "while_test", "for_sum",
    "loop_continue", "g6_struct_impl", "enum_match", "generic_identity",
    "g1_trait_impl", "closure_with_capture",
    # fail: one representative per error class
    "stray_token", "type_mismatch", "wrong_arg_count", "undef_var",
    "assign_immutable",
    # fail: parse errors (one per scenario, not per token type)
    "invalid_char_after_package",
}

# `LOGOS_INTEGRATION_TESTS`
INTEGRATION_TESTS = set("""
    derive_debug_e2e derive_debug_enum derive_debug_generic item_list_splice
    multi_derive_e2e quote_item_smoke quote_item_antiquot quote_item_nested_antiquot
    metacall_expr_blob quote_expr_smoke quote_expr_antiquot quote_expr_repeat_andand
    quote_expr_repeat_comma quote_expr_subblob quote_expr_call_arg_shapes
    quote_expr_stmt_fold quote_item_exprblob_cursor any_downcast slice6_field_accessors
    meta_pack_basic meta_pack_search meta_kind_preds meta_field_iter meta_fold_i64
    meta_field_byname meta_type_methods meta_kind_name quote_ty_smoke meta_args_count
    meta_align_of meta_tuple_intrinsics meta_has_trait meta_typelist meta_reify_type
    meta_apply meta_quote_ty_antiquot meta_quote_ty_tup_arr meta_variant_intrinsics
    meta_generic_handle meta_type_align meta_has_trait_of meta_quote_ty_pack
    meta_template_handle meta_typelist_a_side meta_fold_to_type meta_template_of_smoke
    meta_template_of_typed meta_transform meta_filter meta_const_pack
    meta_sizeof_pack_arg pub_reexport bst fibonacci quicksort linked_list stdlib_demo
    vec_usage format_basic format_types datatype_zone_basic datatype_zone_ref
    datatype_zone_ergonomic datatype_impl datatype_own datatype_relptr writ_string
    writ_typetag writ_objectmap writ_document writ_stringify writ_ex_arena
    writ_ex_containers writ_parse writ_round_trip writ_stringbuf_grow
    writ_anyval_methods writ_ctr_traversal writ_builder writ_registry_lookup
    writ_traits_full writ_traits_bugfix writ_scalar writ_buffer_i64 writ_buffer_string
    writ_string_view_interop writ_zone_seal writ_memholder_rc writ_check
    writ_import_export writ_hbs writ_lit_parse assoc_const_writ_static writ_sdn_lit
    writ_typed_arr_lit writ_typed_map_lit writ_capture_scalar writ_capture_str
    writ_capture_f64 writ_param_roundtrip writ_as_array writ_as_map
    writ_typed_array_variants writ_as_array_variants writ_as_map_variants writ_ctr_view
    writ_typed_array_all_elems writ_typed_map_all_keys writ_typed_container_clone
    writ_typed_container_equal
    writ_view_from_static writ_typed_push writ_list_comp_basic writ_list_comp_edge
    writ_map_comp_basic writ_map_comp_multi writ_comp_coerce writ_match_scalar
    writ_match_or writ_match_str writ_match_map writ_match_arr writ_match_nested
    writ_match_bind writ_match_rest writ_match_typed writ_match_or_struct
    array_i32_generic_api array_generic_push_grow writ_typed_array_grow
    fiber_basic fiber_yield fiber_future
    fiber_thread_basic fiber_thread_future
    fiber_chan_basic fiber_latch_basic fls_tls_basic
    auto_trait_send_scalar auto_trait_send_struct auto_trait_unsafe_impl
    auto_trait_generic_propagation
    blanket_impl_unbounded blanket_impl_assoc_eq instantiate_decl
    vec_full hashset_basic vecdeque_basic btreemap_basic
    atomic_basic mutex_basic
    box_basic box_drop_raii rc_basic rc_drop_inner arc_basic bytes_basic
    box_dyn_trait_basic
    io_uring_basic
    fs_basic env_basic io_print_basic panic_basic pipe_basic io_read_write_trait
    io_writev_basic
    path_basic process_basic net_basic
    math_basic random_basic thread_basic bufio_basic bufio_generic_reader
    signal_basic os_basic
    hex_basic base64_basic url_basic url_parse_basic http_types_basic http_parse_basic
    http_parse_corpus http_golden_suite http_parse_stream http_serialize_stream
    http_service_basic http_serve_bounded http_keepalive http_workers_basic
    http_chunked_body_helper http_chunked http_chunked_reader http_framing_readers
    http_roundtrip json_basic log_basic args_basic crypto_basic testing_basic
    csv_basic datetime_basic regex_basic
""".split())

# `logos_determine_suite`: the name-prefix rules, tried after the two lists.
OWNERSHIP_RE = re.compile(
    r"^(borrow_|clone_|copy_|drop_|lifetime_|unsafe_|cond_ref_|deref_|move_|mut_|"
    r"shared_|dangling_ref|assign_ref_dangling|double_mut_borrow|use_after_move|"
    r"use_while_mut_borrowed|const_ptr_write)")
ADVANCED_RE = re.compile(
    r"^(assoc_|generic_|trait_|dyn_|variadic_|closure_|impl_|default_|g1_trait_impl|"
    r"g1_standalone_impl|g1_extern_fn|g2_impl_trait_return|impl_trait_|from_|into_|"
    r"option_chain|spec_dispatch|extern_)")


def determine_suite(mode, base):
    """`logos_determine_suite(MODE BASE OUT_VAR)`."""
    if base in SMOKE_TESTS:
        return "smoke"
    if base in INTEGRATION_TESTS:
        return "integration"
    if OWNERSHIP_RE.search(base):
        return "ownership"
    if ADVANCED_RE.search(base):
        return "advanced_features"
    return "diagnostics" if mode == "fail" else "semantic_core"


# The fixture ARCHIVES the corpus links (built by add_custom_command in
# tests/logos/CMakeLists.txt into CMAKE_CURRENT_BINARY_DIR), by CMake variable.
ARCHIVES = {
    "PUB_LIB_BIN": "libpub_lib.a",
    "UB_BOUNDARY_BIN": "libub_boundary.a",
    "WQL_MAP_LIB_BIN": "libwql_map_lib.a",
    "MEMORIA_CTR_BIN": "libmemoria-ctr.a",
    "MEMORIA_STORE_BIN": "libmemoria-store.a",
    "MEMORIA_TESTKIT_BIN": "libmemoria-testkit.a",
    "COEX_BIN": "libcoex.a",
    "COEX2A_BIN": "libcoex2a.a",
    "COEX2B_BIN": "libcoex2b.a",
    "LAZY_PKG_BIN": "liblazy_pkg.a",
    "SD_DST_MOD_BIN": "libsd_dst_mod.a",
    "CTR_MOD_BIN": "libctr_mod.a",
    "PMAP_MOD_BIN": "libpmap_mod.a",
    "LAZY_LOWER_BIN": "liblazy_lower.a",
    "LAZY_UPPER_BIN": "liblazy_upper.a",
    "THREE_LAYER_LOW_BIN": "liblow.a",
    "THREE_LAYER_MID_BIN": "libmid.a",
    "THREE_LAYER_HI_BIN": "libhi.a",
    "TRAIT_IDENT_CHAIN_LHOM_BIN": "liblhom.a",
    "TRAIT_IDENT_CHAIN_HMID_BIN": "libhmid.a",
    "TRAIT_BLANKET_CHAIN_BMID_BIN": "libbmid.a",
    "TRAIT_BLANKET_CHAIN_BPROBE_BIN": "libbprobe.a",
    "BCXA_BIN": "libbcxa.a",
}

# `logos_pass_extra_args`: the memoria corpus links its three archives, matched
# by PREFIX (and returns before the _O2 / --test rules below).
MEMORIA_PREFIX = "memoria_"
MEMORIA_ARCHIVES = ["MEMORIA_CTR_BIN", "MEMORIA_STORE_BIN", "MEMORIA_TESTKIT_BIN"]

# `logos_pass_extra_args`: the if/elseif chain over the LOCAL_*_USERS lists, in
# chain order — the FIRST list naming BASE decides, and each archive becomes
# `-l <archive>`. (LOCAL_STDLIB_USERS and LOCAL_WRIT_USERS are defined there
# too but no branch reads them, so they add nothing and are not carried.)
PASS_ARCHIVE_USERS = [
    # LOCAL_METAPROG_PUBLIB_USERS
    ({"metacall_item_use_inherit"}, ["PUB_LIB_BIN"]),
    # LOCAL_PUBLIB_USERS
    ({"pub_module_internal_use", "pub_module_type_internal", "pub_cross_package",
      "pkg_multifile", "pub_reexport", "pub_enum_trait_cross_pkg",
      "pub_static_cross_module", "pub_dyn_cross_module", "wql_wref_field_pkg"},
     ["PUB_LIB_BIN"]),
    # LOCAL_UB_BOUNDARY_USERS — the Freeze-predicate guard (archive built at -O2)
    ({"interior_mut_freeze_canary"}, ["UB_BOUNDARY_BIN"]),
    # LOCAL_WQLMAP_USERS — ADR 0016 cross-module mapping fusion
    ({"wql_mapping_cross_module_e2e"}, ["WQL_MAP_LIB_BIN"]),
    # LOCAL_COEX_USERS — B-mv-01 / G156-1 same-name coexistence
    ({"cross_pkg_coexistence", "cross_pkg_type_coexistence",
      "cross_pkg_type_id_distinct", "cross_pkg_const_scoped", "coex_dyn_bare_key",
      "coex_enum_bare_key", "coex_tagged_enum_bare_key"}, ["COEX_BIN"]),
    # LOCAL_COEX2_USERS — same-pkg-same-name TYPE coexistence across two modules
    ({"coex_from_a", "coex_from_b"}, ["COEX2A_BIN", "COEX2B_BIN"]),
    # LOCAL_LAZY_PKG_USERS — Phase 6 lazy-mode archive
    ({"lazy_pkg_basic"}, ["LAZY_PKG_BIN"]),
    # LOCAL_SD_DST_MOD_USERS — generic self-describing DST from an archive
    ({"sd_dst_module_methods"}, ["SD_DST_MOD_BIN"]),
    # LOCAL_CTR_MOD_USERS — ADR 0020 container from a binary module
    ({"container_item_from_module"}, ["CTR_MOD_BIN"]),
    # LOCAL_PMAP_MOD_USERS — ADR 0021 Phase 4a generic container alias
    ({"metaclass_pmap_from_module"}, ["PMAP_MOD_BIN"]),
    # LOCAL_LAZY_CHAIN_USERS — lazy→lazy chain
    ({"lazy_pkg_chain"}, ["LAZY_LOWER_BIN", "LAZY_UPPER_BIN"]),
    # LOCAL_THREE_LAYER_USERS — manifest depends chain
    ({"three_layer_chain"},
     ["THREE_LAYER_HI_BIN", "THREE_LAYER_MID_BIN", "THREE_LAYER_LOW_BIN"]),
    # LOCAL_TRAIT_IDENT_CHAIN_USERS — homonym trait across package archives
    ({"trait_ident_pkg_chain", "trait_ident_bare_alias_bound",
      "dyn_vtable_homonym_target"},
     ["TRAIT_IDENT_CHAIN_HMID_BIN", "TRAIT_IDENT_CHAIN_LHOM_BIN"]),
    # LOCAL_TRAIT_BLANKET_CHAIN_USERS — the blanket-alias twin
    ({"trait_blanket_bare_alias_bound"}, ["TRAIT_BLANKET_CHAIN_BMID_BIN"]),
    # LOCAL_TRAIT_BPROBE_USERS — the mirror archive
    ({"trait_blanket_homonym_bound_admits"}, ["TRAIT_BLANKET_CHAIN_BPROBE_BIN"]),
    # LOCAL_BCXA_USERS — D1 round 5 / H6 door-F callee in an archive
    ({"bc_d1r5_h6_cross_archive_admits"}, ["BCXA_BIN"]),
]

# `logos_pass_extra_args`, after the chain: name SUFFIX/PREFIX rules that append.
#   `_O2` suffix: the optimisation level IS the test (#343).
#   `test_harness_` prefix: logosc's --test mode.
PASS_APPEND_RULES = [
    (re.compile(r"_O2$"), ["-O2"]),
    (re.compile(r"^test_harness_"), ["--test"]),
]

# `logos_fail_extra_args`, same shape. (FAIL_METAPROG_USERS maps to no flags.)
FAIL_ARCHIVE_USERS = [
    # FAIL_PUBLIB_USERS
    ({"pub_module_cross_module", "pub_module_type_cross_module", "pub_call_private",
      "pub_private_field_access", "enum_private_cross_pkg", "trait_private_cross_pkg"},
     ["PUB_LIB_BIN"]),
    # FAIL_WQLMAP_USERS
    ({"wql_mapping_cross_module_priv_fail", "wql_mapping_cross_module_modvis_fail"},
     ["WQL_MAP_LIB_BIN"]),
    # FAIL_COEX_USERS
    ({"cross_pkg_ambiguous_call", "cross_pkg_const_ambiguous"}, ["COEX_BIN"]),
    # FAIL_TRAIT_BPROBE_USERS — refusal half of the homonym-blanket pair
    ({"trait_blanket_homonym_bound_refused"}, ["TRAIT_BLANKET_CHAIN_BPROBE_BIN"]),
    # FAIL_TRAIT_IDENT_CHAIN_USERS — a bound crossing an archive keeps its identity
    ({"trait_ident_cross_archive_bound_refused"}, ["TRAIT_IDENT_CHAIN_LHOM_BIN"]),
    # FAIL_TRAIT_IDENT_MIRROR_USERS — the mirror direction, through two archives
    ({"trait_ident_chain_mirror_bound_refused"},
     ["TRAIT_IDENT_CHAIN_HMID_BIN", "TRAIT_IDENT_CHAIN_LHOM_BIN"]),
]

# `LAYOUT_DECLINE_LEDGER`: pass tests compiled WITHOUT LOGOS_VERIFY_LAYOUT=1
# (held both ways by logos_00_layout_decline_ledger; the grounds are written at
# the ledger in tests/logos/CMakeLists.txt and tests/logos/layout_decline.ledger).
LAYOUT_DECLINE_LEDGER = {
    "zone_zvec_two_zones",
    "memoria_dyn_boundary",
    "memoria_example_map",
    "memoria_example_multimap",
}

# `_LOGOS_DISABLED_PORTS`: tests/imported ports registered DISABLED (rustc
# accepts them, logosc refuses; see the note at the list in the CMakeLists for
# each one's diagnostic). Disabled, not dropped, so they stay countable.
DISABLED_PORTS = {
    "dropck-shadow-rebind",
    "ref-mut-binding-stable-across-guard-m2",
    "poll-problem-case-3",
    "if-generic",
}

# The `bc` label (`ctest -L bc`): imported ports by upstream SUITE DIRECTORY,
# native fixtures by filename PREFIX (the measured set — see the CMakeLists).
BC_LABEL_RES = [
    re.compile(r"/tests/imported/(pass|fail|admit)/(borrowck|nll|moves|move|regions|"
               r"lifetimes|dropck|drop|closures|variance)/"),
    re.compile(r"/tests/logos/(pass|fail)/(bc_|zone_mut|place_write|branch_merge|reborrow|nll)"),
]

# A family-forging pass test (imports the metaclass factory) gets the heavy
# timeout. `file(READ)` + MATCHES in `logos_add_grouped_test`.
HEAVY_SOURCE_RE = re.compile(rb"logos\.lcm\.canon\.(metaclass|container_item)")
PASS_TIMEOUT, PASS_HEAVY_TIMEOUT, FAIL_TIMEOUT = 120, 180, 60

# The facts side product (task #85): `tests/logos/pass` fixtures write facts and
# are FIXTURES_SETUP for the census gates; `wql_`/`deem_` ones also set up the
# narrower glob fixture.
FACTS_GLOB_RE = re.compile(r"^(wql|deem)_")


def facts_fixtures(base):
    if FACTS_GLOB_RE.search(base):
        return ["logos_facts_all", "logos_facts_glob"]
    return ["logos_facts_all"]


def name_we(path):
    """CMake `get_filename_component(... NAME_WE)`: the name up to its FIRST dot."""
    return os.path.basename(path).split(".", 1)[0]


def pass_extra_args(ctx, base):
    """`logos_pass_extra_args(BASE OUT_VAR)`."""
    arch = lambda keys: [a for k in keys for a in ("-l", ctx.v["tbin"] + "/" + ARCHIVES[k])]
    if base.startswith(MEMORIA_PREFIX):
        return arch(MEMORIA_ARCHIVES)
    extra = []
    for names, keys in PASS_ARCHIVE_USERS:
        if base in names:
            extra = arch(keys)
            break
    for rx, flags in PASS_APPEND_RULES:
        if rx.search(base):
            extra += flags
    return extra


def fail_extra_args(ctx, base):
    """`logos_fail_extra_args(BASE OUT_VAR)`."""
    for names, keys in FAIL_ARCHIVE_USERS:
        if base in names:
            return [a for k in keys for a in ("-l", ctx.v["tbin"] + "/" + ARCHIVES[k])]
    return []


def grouped_test(ctx, mode, base, exp_file, logos_file, suite):
    """`logos_add_grouped_test(MODE BASE EXP_FILE LOGOS_FILE SUITE)`."""
    v = ctx.v
    if mode == "pass":
        extra = pass_extra_args(ctx, base)
        timeout = PASS_TIMEOUT
        if HEAVY_SOURCE_RE.search(ctx.read_bytes(logos_file)):
            timeout = PASS_HEAVY_TIMEOUT
    else:
        extra = fail_extra_args(ctx, base)
        timeout = FAIL_TIMEOUT
    # `corpus` is what exempts these from the tier rule.
    labels = ["logos", "corpus", mode, "suite_" + suite]
    if "/tests/imported/" in logos_file:
        labels.append("imported")
    if any(rx.search(logos_file) for rx in BC_LABEL_RES):
        labels.append("bc")
    env = ["LOGOS_LIB_DIR=" + v["libdir"]]
    if mode == "pass" and base not in LAYOUT_DECLINE_LEDGER:
        env.append("LOGOS_VERIFY_LAYOUT=1")
    # handed to EVERY test; logosc asks whether the program is listed.
    env.append("LOGOS_MLIRGEN_BUG_LEDGER=" + v["tsrc"] + "/mlir_gen_bug.ledger")
    fixtures = []
    if mode == "pass" and "/tests/logos/pass/" in logos_file:
        env.append("LOGOS_FACTS_DIR=" + v["tbin"] + "/facts/" + base)
        fixtures = facts_fixtures(base)
    t = dict(
        name="logos_%s_%s_%s_%s" % (SUITE_PREFIX.get(suite, "06"), suite, mode, base),
        command=[v["tsrc"] + "/run_test.sh", mode, v["logosc"], logos_file, exp_file] + extra,
        labels=labels, timeout=timeout, env=env, fixtures_setup=fixtures)
    # The memoria fixtures read byte fixtures through repo-root-relative paths.
    if base.startswith(MEMORIA_PREFIX):
        t["workdir"] = v["src"]
    if "/tests/imported/" in logos_file and base in DISABLED_PORTS:
        t["disabled"] = 1
    return t


def corpus_tests(ctx):
    pass_exp = sorted(ctx.glob("tests/logos/pass/*.expected")
                      + ctx.glob_recurse("tests/imported/pass", ".expected"))
    fail_exp = sorted(ctx.glob("tests/logos/fail/*.expected")
                      + ctx.glob_recurse("tests/imported/fail", ".expected"))
    for mode, exps in (("pass", pass_exp), ("fail", fail_exp)):
        for exp in exps:
            logos = exp[: -len(".expected")] + ".logos"
            if not os.path.exists(logos):
                ctx.warn("logos test: no .logos for %s, skipping" % exp)
                continue
            base = name_we(exp)
            yield grouped_test(ctx, mode, base, exp, logos, determine_suite(mode, base))


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 2 — FACTS OF THE PASS FIXTURES NO SUITE REGISTERS (task #85): every
# `tests/logos/pass/*.logos` with NO `.expected` still emits its facts, for
# logos_09_direct_door_census. Nothing is compared.
# ═════════════════════════════════════════════════════════════════════════════
def facts_orphan_tests(ctx):
    v = ctx.v
    for fl in ctx.glob("tests/logos/pass/*.logos"):
        if os.path.exists(fl[: -len(".logos")] + ".expected"):
            continue
        b = name_we(fl)
        facts = v["tbin"] + "/facts/" + b
        yield dict(
            name="logos_09_facts_" + b,
            command=[v["tsrc"] + "/facts_emit.sh", v["logosc"], fl, facts, facts + "/obj.o"]
                    + pass_extra_args(ctx, b),
            timeout=180, labels=["logos", "pass", "suite_semantic_core", "tier_commit"],
            env=["LOGOS_LIB_DIR=" + v["libdir"]], fixtures_setup=facts_fixtures(b))


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 3 — SOUNDNESS QUEUE, ONE TEST PER ROW PROGRAM (`_squeue_srcs`): every
# tests/soundness/open/*.logos. The roster test is a singleton above.
# ═════════════════════════════════════════════════════════════════════════════
def squeue_tests(ctx):
    v = ctx.v
    for sq in ctx.glob("tests/soundness/open/*.logos"):
        b = name_we(sq)
        yield dict(
            name="logos_00_squeue_" + b,
            command=[v["tsrc"] + "/soundness_queue_gate.sh", "--one", b, v["logosc"],
                     v["tsrc"] + "/soundness_queue.ledger", v["src"]],
            timeout=120, labels=["logos", "pass", "suite_semantic_core", "tier_commit"],
            env=["LOGOS_LIB_DIR=" + v["libdir"]])


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 4 — ONE TEST PER BC ADMIT PROGRAM (`_bc_admit_srcs`):
# tests/imported/admit/<dir>/*.logos -> logos_00_bc_admit_<dir>_<BASE>.
# ═════════════════════════════════════════════════════════════════════════════
def bc_admit_tests(ctx):
    v = ctx.v
    for ba in ctx.glob("tests/imported/admit/*/*.logos"):
        bb = name_we(ba)
        bd = os.path.basename(os.path.dirname(ba))
        yield dict(
            name="logos_00_bc_admit_%s_%s" % (bd, bb),
            command=[v["tsrc"] + "/bc_admit_one.sh", v["logosc"], ba, bd],
            timeout=120, labels=["logos", "pass", "suite_semantic_core", "tier_commit"],
            env=["LOGOS_LIB_DIR=" + v["libdir"]])


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 5 — IR SNAPSHOTS: each tests/logos/ir/*.check with a same-named .logos,
# run through FileCheck. ABSENCE IS RED: with no FileCheck the suite does not
# shrink, it registers logos_07_ir_snapshot_UNAVAILABLE (a failing test) unless
# LOGOS_ALLOW_NO_FILECHECK is ON.
# ═════════════════════════════════════════════════════════════════════════════
IR_LABELS = ["logos", "ir_snapshot", "suite_ir", "tier_commit"]


def ir_snapshot_tests(ctx):
    v = ctx.v
    checks = ctx.glob("tests/logos/ir/*.check")
    if v["filecheck"]:
        for chk in checks:
            b = name_we(chk)
            logos = v["tsrc"] + "/ir/" + b + ".logos"
            if not os.path.exists(logos):
                ctx.warn("ir snapshot: no .logos for %s, skipping" % chk)
                continue
            yield dict(
                name="logos_07_ir_snapshot_" + b,
                command=[v["tsrc"] + "/ir/run_ir_snapshot.sh", v["logosc"], v["filecheck"],
                         logos, chk],
                timeout=120, labels=IR_LABELS, env=["LOGOS_LIB_DIR=" + v["libdir"]])
    elif not ctx.option("LOGOS_ALLOW_NO_FILECHECK"):
        yield dict(
            name="logos_07_ir_snapshot_UNAVAILABLE",
            command=[v["cmake"], "-E", "env", "LOGOS_IR_TESTS_MISSING=%d" % len(checks),
                     v["cmake"], "-E", "false"],
            timeout=120, labels=IR_LABELS)


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 6 — SPEC CONFORMANCE (tools/spec-extract output): tests/spec/{pass,fail}.
# Registered outside logos_add_grouped_test, so the R2 ledger is handed here too
# (pass half only, as in CMake).
# ═════════════════════════════════════════════════════════════════════════════
def spec_tests(ctx):
    v = ctx.v
    for mode, timeout in (("pass", 120), ("fail", 60)):
        for exp in ctx.glob("tests/spec/%s/*.expected" % mode):
            logos = exp[: -len(".expected")] + ".logos"
            if not os.path.exists(logos):
                ctx.warn("spec test: no .logos for %s, skipping" % exp)
                continue
            env = ["LOGOS_LIB_DIR=" + v["libdir"]]
            if mode == "pass":
                env.append("LOGOS_MLIRGEN_BUG_LEDGER=" + v["tsrc"] + "/mlir_gen_bug.ledger")
            yield dict(
                name="logos_25_spec_%s_%s" % (mode, name_we(exp)),
                command=[v["tsrc"] + "/run_test.sh", mode, v["logosc"], logos, exp],
                timeout=timeout, labels=["logos", "corpus", mode, "suite_spec"], env=env)


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 7 — LISTED-CASE GATES over named pass fixtures.
# ═════════════════════════════════════════════════════════════════════════════
# `GENDIR_CASE`: the --gen-dir round trip, one representative per corpus axis
# plus the emitter-output cases that found renderer holes.
GENDIR_CASES = [
    "quote_item_vis_antiquote", "quote_item_opt_use", "quote_item_parse_as",
    "quote_item_wstatic_typearg", "quote_item_wstatic_const", "quote_item_ctrfamily_impl",
    "quote_item_dyn_typearg", "wql_tuple_first_e2e", "query_mapping_runtime_e2e",
    "quote_item_exprblob_cursor", "quote_item_qual_call_render",
]
# `FLATBODY_CASE`: a Deem-emitted fn body is FLAT (ADR 0024 S5), one per emitter family.
FLATBODY_CASES = [
    "wql_first_fold_e2e", "wql_join_e2e", "wql_aggregate_e2e",
    "wql_datalog_stratified_e2e", "deem_ctr_family_streams",
]
LISTED_GATES = [
    # (name prefix, gate script under tests/logos, cases)
    ("logos_09_gendir_", "run_gendir_test.sh", GENDIR_CASES),
    ("logos_09_flat_emitted_body_", "flat_body_gate.sh", FLATBODY_CASES),
]


def listed_gate_tests(ctx):
    v = ctx.v
    for prefix, script, cases in LISTED_GATES:
        for case in cases:
            yield dict(
                name=prefix + case,
                command=[v["tsrc"] + "/" + script, v["logosc"],
                         v["tsrc"] + "/pass/" + case + ".logos"],
                timeout=60, labels=["logos", "pass", "suite_semantic_core", "tier_full"],
                env=["LOGOS_LIB_DIR=" + v["libdir"]])


# ═════════════════════════════════════════════════════════════════════════════
# FAMILY 8 — THE ENUMERATOR (tests/exhaustive), only when its harness exists.
# The smoke tier is `tier_explicit`: test-levels.sh runs it from its own block.
# ═════════════════════════════════════════════════════════════════════════════
EXHAUSTIVE_TIERS = [("smoke", 600, "tier_explicit"), ("full", 1800, "tier_full")]


def exhaustive_tests(ctx):
    v = ctx.v
    if not os.path.exists(v["src"] + "/tests/exhaustive/harness.py"):
        return
    for tier, timeout, tier_label in EXHAUSTIVE_TIERS:
        yield dict(
            name="logos_26_exhaustive_" + tier,
            command=[v["src"] + "/tests/exhaustive/run_tier.sh", tier, v["logosc"], v["libdir"]],
            timeout=timeout, processors=12,
            labels=["logos", "exhaustive", "suite_exhaustive", tier_label])


# Every family, with the build directory its tests run from. All of them come
# from tests/logos/CMakeLists.txt, so their default workdir is {build}/tests/logos.
FAMILIES = [
    ("tests/logos", corpus_tests),
    ("tests/logos", facts_orphan_tests),
    ("tests/logos", squeue_tests),
    ("tests/logos", bc_admit_tests),
    ("tests/logos", ir_snapshot_tests),
    ("tests/logos", spec_tests),
    ("tests/logos", listed_gate_tests),
    ("tests/logos", exhaustive_tests),
]
