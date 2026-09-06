# Round 2026-09-06 — landing `dropmemo`. PREDICTION, written BEFORE the edit.

build read back before the edit: 6dd76d6d10202933 · queue 39 · bc_admits 98 ·
bc_admits_blocked 25 · probe-log-lint 221 · queue gate rc 0.

## THE CLAIM

`MLIRGenImpl::resolve_method_symbol` is a pure function of
(struct_name, method_name, pkg) over the immutable `const LProgram* prog_`,
returning a (symbol, pkg_owns_struct) PAIR. Memoising that pair on that key is
semantics-preserving by construction. It is not a new behaviour; it is the same
answer, computed once.

## THE NUMBER, AND THE LIST OF ROWS

  rows closed:            **0**   — the soundness queue stays at 39.
  rows opened by the fix: **0**.
  bc_admits delta:        **0**   (98), bc_admits_blocked **0** (25).
  `-L bc` delta:          **0** of 2670.
  stdlib-cost, four layers: **0**.
  fail_text_oracle:       **0** damaged.
  run_oracle:             **0** damaged (`cast-region-to-uint` subtracted by name).

  ⚠ THIS ROUND CLOSES NO LEDGER ROW BY DESIGN. Its deliverable is a RED GATE
  turned green: `logos_02_semantic_core_pass_wql_domain_static_extremes` crossed
  its 120 s property and, being a `FIXTURES_SETUP` producer, took three
  `logos_09_*` census gates with it. A ledger row closed under a red producer is
  closed against an unmeasured tree.

  Predicted wall on the red fixture: **< 10 s** (pricing round measured 6.97 s
  against 30.72 s HEAD, interleaved, 4 runs each). Predicted `--stats`
  `codegen+write`: **< 4 000 ms** against 26 236 ms measured today on HEAD.

## THE CLASS, ENUMERATED BY THE PROPERTY

Property: *a pure query over the immutable `prog_` implemented as a FULL LINEAR
SCAN of `prog_->structs` and/or `prog_->functions`, invoked from a PER-NODE code
path in mlir-gen* — i.e. O(program) work inside an O(program) loop.

Enumerated over the whole compiler, 86 loops over `prog[_].structs` /
`prog[_].functions`. All but three are ONE-SHOT top-level passes (`generate`'s
pass 0..N, `emit_module`, `sema`'s collect phases, `borrow_check`'s registration)
— O(program) once, not per node. The three reachable per node:

  1. `MLIRGenImpl::resolve_method_symbol`   mlir_gen_impl.hpp 516/525/541/545
  2. `MLIRGenImpl::pkg_owns_symbol_owner`   mlir_gen_impl.hpp 1235
  3. `MLIRGenImpl::gen_tagged_dispatch`     mlir_gen_dyn.cpp   1361/1374

  (2) is ALREADY a member fixed the same structural way — it builds
  `pkg_struct_names_` once behind `pkg_struct_names_built_` and scans an index
  after. (3) is reached only when `parent_mod.lookupSymbol` MISSES on a tagged
  dispatch node. So the prediction is: the class has three members, two of them
  already closed, and (1) is the open one.

  ⚠ A GREP CERTIFIES WHAT IT CANNOT SEE. The property is settled by
  MEASUREMENT, not by the grep: a census counts arrivals and scan ITERATIONS at
  all three sites on the red fixture in the same build as the fix, and `--stats`
  says what fraction of the phase is left after the memo. Predicted iteration
  counts on `wql_domain_static_extremes`, HEAD: site 1 ~172.5 M (the pricing
  round measured `rms.scan.fn` 172 568 880), site 2 O(structs) ONCE, site 3 zero
  (no tagged dispatch in that program).

## COUNTER-EXAMPLES — five shapes, written before the edit, with HEAD verdicts

Under `ce/`. Every one is multi-line, and none is drawn from the syntax the
pricing round used (`-L bc` and the queue). Each must read IDENTICALLY after the
memo; a difference in ANY column condemns it.

| program | what it attacks | HEAD verdict |
|---|---|---|
| `ce1_homonym_two_pkgs_both_drop` | two live `String`s (user + `logos.mem.string`), both with drop glue, one nested as a FIELD | cc 0, **rc 139 SIGSEGV** |
| `ce2_inherent_drop_and_trait_drop` | the `qbase` path — an inherent `drop` and a `Drop` impl on ONE type, both asked under one memo key | cc 0, rc 4, `sum=16 trait=0 inherent=4` |
| `ce3_authoritative_negative_twice` | the `pkg_owns_struct` OUT-PARAM on a memo HIT — a destructor COUNT | cc 0, rc 14, `drops=14` (correct) |
| `ce4_generic_two_insts_drop` | one generic at two element types, each with its own destructor — a destructor COUNT | cc 0, rc 11, `n=2 a=1 b=1` (correct) |
| `ce5_no_drop_at_all` | the NEGATIVE answer memoised — a type with no `drop` of any kind, queried many times | cc 0, rc 30, `s=30` (correct) |

⚠ ce1 and ce2 are WRONG ON HEAD, before any edit of mine. They are INHERITED
(rule 14) and are recorded here as baselines, not as this round's damage.

## THE NEW DEFECT ce1 FOUND — a PAIR, one token apart

  n2_vec_holder    `struct String {a:i64}` + `struct Holder {s:String,t:String}`
                   + `Vec<Holder>`                         → cc 0, **rc 139**
  n3_ctl_no_homonym  the same program with `String` → `ZqStr`  → cc 0, rc 3

The existing fixture `mlirgen_odr_drop_glue_homonym` covers `Vec<String>`
DIRECTLY. The FIELD-nesting channel — a homonym reached through another
struct's field drop glue — is open. Queue row candidate, tier 1, `run 139`.
