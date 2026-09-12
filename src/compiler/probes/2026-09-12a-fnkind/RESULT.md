# 2026-09-12a — THE Fn-FAMILY KIND IS MINTED CORRECTLY AND READ IN EXACTLY TWO
# PLACES; `bck.NEW-CMUT` IS A MISSING ARRIVAL, NOT A MISSING RULE

build: `0e2b64ecc2cb7064 43` (read with `scripts/build_hash.py`), `build/bin/logosc`
dated Sep 12 07:46. No compiler source was edited in this round; every number below
is measured on that unmodified binary.

## Census (queue gate first)

* `soundness_queue_gate.sh` (with `LOGOS_LIB_DIR=$PWD/build/lib/logos`) — **rc 0**,
  85 rows, three planted canaries read correctly.
* `# TOTAL`: soundness_queue 85 · bc_admits **84** · bc_admits_blocked 8.
  ⚠ The prompt says the ledger is at 85; it is at **84** — `b6e916800` closed
  `nllmoves.R11-ASSIGN` and edited the count down. A handed-down number is a
  hypothesis (rule 17).
* `probe-log-lint.py`: 275 records, every site symbol resolves.
* `tools/dlog/selftest.sh`: **rc 0** — the known-answer control on `28fc7c75`
  still reads 19 walkers / 24 findings / try_path 1-5 / domain 42-5, and the duty
  rule still discriminates across `756aed65` (1 -> 0). Run before any rule below.

## The three rows' recorded controls, re-verified on today's binary

All three still compile silently (rc 0, object written): `borrowck-unboxed-closures`,
`borrow-immutable-upvar-mutation-impl-trait`, `borrowed-referent-issue-38899`.
Upstream evidence re-read from the checkout, not cited from a record:
E0594 "cannot assign to `x`, as it is a captured variable in a `Fn` closure" and
E0596 "cannot borrow `f` as mutable, as it is not declared as mutable"
(`/home/logos/cxx/rust/tests/ui/borrowck/*.stderr`).

## dlog: which sites decide an Fn-family question, and on which fact

New rule `tools/dlog/fnkind_readers.dl` over sema_expr.cpp, sema_collect.cpp,
sema_stmt.cpp, sema.cpp (4 TUs). Answers archived beside this file.

* `closure_kind_` (signature-keyed, open #90) — read in **2** contexts only:
  `check_type_bounds` (sema_collect.cpp:1559-1560) and `lower_closure_expr`
  (the mint, sema_expr.cpp:18418-18420).
* `closure_kind_by_id_` (per-literal) — read in **2**: `callable_is_fn_once`
  and the mint.
* `check_type_bounds` is CALLED from **12** sites: `type_bounds_satisfied_quiet`,
  `lower_call`, `resolve_type_generic_inst` x3, `resolve_type_assoc_ref`,
  `finish_generic_call`, `lower_generic_ref`, `lower_method_call`,
  `lower_struct_lit`, `lower_enum_lit_data`, `lower_enum_lit_data_from_static`.
  **None of the twelve is a return-type position or a `dyn` coercion.** That is
  the absence, and an absence has no spelling — a grep cannot find it.
* `is_fn_family` is read in **12** contexts, of which dlog derives **9** as
  reaching no kind fact at all.

⚠ **THE MANDATED PER-SITE CROSS-CHECK, AND THE TWO NUMBERS SIDE BY SIDE.**
dlog: 9 blind contexts. Per-site read: **1 of the 9 is legitimately blind** —
`check_bounds` (sema_impl.hpp:3480) is the post-collect well-formedness sweep and
`continue`s on `is_fn_family` by design. The other eight are hint/inference
contexts that ask only whether a bound NAMES an Fn trait. So dlog's verdict
enumerates the class correctly and its "blind" LABEL over-counts by one, exactly
the `ctx_of` coarsening this tool has been wrong about before. The number that
funds anything is the per-site one.

## The hand battery — VARIED BY SHAPE, NOT BY COUNT (rule 5)

18 compile probes + 6 linked-and-run programs, archived under `hand/`.

| # | position the closure meets the Fn trait | body | verdict today | Rust |
|---|---|---|---|---|
| h1 | generic `F: Fn` param | `x += 1` | **REFUSED**, E0525 wording | refuse |
| h2 | generic `F: Fn` param | `x = x + 1` | **REFUSED** | refuse |
| h13 | generic `F: Fn` param, non-`move` | `x += 1` | **REFUSED** | refuse |
| h3 | `-> impl Fn()` return (**the row**) | `x += 1` | admits | refuse E0594 |
| h4 | `-> Box<dyn Fn()>` return | `x += 1` | admits | refuse |
| h7 | `let f: Box<dyn Fn()>` annotation | `x += 1` | admits | refuse |
| h8 | `&dyn Fn()` parameter | `x += 1` | admits | refuse |
| h5 | `-> impl Fn()` return | read-only | admits | admit ✓ |
| h6 | `-> impl FnMut()` return | `x += 1` | admits | admit ✓ |
| h9 | `Box<dyn Fn>` STRUCT FIELD | `x += 1` | refused, wrong reason ("not callable") | refuse |

⚠ h9 is NOT new: it is the standing queue row
`boxed_closure_struct_field_not_callable`. Checked before claiming it.

**THE SEPARATION, AND IT SPLITS THE ROW'S OWN ROOT IN TWO.** The `dyn` spellings
DO get the ordinary structural check — `-> Box<dyn Fn(i64)->i64>` returning a
0-arg closure is refused "return type mismatch" (h17), and `Box::new(7i64)` is
refused (h18). The `impl Trait` return position is checked by **nothing at all**:
`-> impl Fn()->i64 { return 7i64; }` compiles (h15), so does a wrong-arity closure
(h16), and so does `-> impl Speak { return A{..} }` where `A` does not implement
`Speak` (h19). So the row's two ports were admitted by DIFFERENT roots — the
ledger note "the boxing was neither needed nor the cause" is right about the
boxing and incomplete about the cause:
* **R-a** `impl Trait` in return position is not a bound-check position at all;
* **R-b** the Fn-family KIND is asked only at a generic type-param bound, so the
  `dyn`/`Box<dyn>`/`&dyn` spellings carry the signature but never the kind.

⚠ **R-a IS NOT A TYPE CONFUSION** — rule 1, a zero is not an answer until the
site is proven live, and here the reverse. h15/h16/h19 are refused at the CALLER
(`call to undefined function 'f'`, `expected 0 args, got 1`, `'A' has no method
'speak'`), because `impl Trait` is resolved to the concrete type of the returned
expression. The declared trait is IGNORED, not mis-trusted: these are wrong
DIAGNOSTICS at a distance, not runtime holes.

## Runtime column — a destructor count, not an exit code

| program | oracle | result |
|---|---|---|
| r3 = the row, called twice | exit code | rc 12 (=1*10+2): correct FnMut behaviour, no runtime defect |
| r23 `move` closure consuming a capture with `impl Drop`, called twice | drop count via `*mut i64` | **refused** "an `FnOnce` is consumed by the call" |
| r24 same closure passed as `F: Fn` | drop count | **refused** "its body moves out (consumes) a captured variable, so it is `FnOnce`" |

⚠ **A CONTROL THAT SAVED A WRONG FINDING.** With the SAME capture struct but NO
`impl Drop` (h11/h12) both programs are ADMITTED, and I was one step from filing
"kind 2 is not minted for a `let`-move of a whole capture". It is minted; the
capture is **auto-Copy** and there is no move to see. That is blessed divergence
**A16** (`docs/DIVERGENCES.md`, canonised by Victor 2026-08-24), not a checker
hole. The separating variable is `impl Drop`, and it must be in every program
that claims a move-tracking defect.

## Pricing — by direct arrival census, and why no ceiling probe was built

`fires:` not measured by an installed probe this round. The arrival population is
bounded by direct listing instead, which no probe could exceed:

**R-a arrival set — every `-> impl Fn` in the tree** (excluding `probes/`, notes
and generators): the ledger row itself; `tests/soundness/open/
impl_fn_return_stack_env_dangles` (3 sites); the pass fixture
`tests/logos/pass/fatret_closure_impl_fn_pair_survives`; 7 forms in
`tests/lattice/closure/gen.py`. **0 in the stdlib.**

| column | R-a (kind check at the `impl Trait` return) |
|---|---|
| CEILING on `bc_admits` | **1** — `borrow-immutable-upvar-mutation-impl-trait`, and it is the only non-pass arrival |
| cost, `pass` | **0 by reading** — every arrival above returns a READ-ONLY closure (kind 0) |
| cost, `cfail` | 0 — no arrival |
| cost, `stdlib` | 0 — no `-> impl Fn` in the stdlib |
| cost, runtime | 0 — the three runnable arrivals (fatret pass fixture, both queue parts) are kind 0 |

⚠ **RULE 4 GOVERNS THIS TABLE.** A ceiling of 1 off an eleven-program arrival set
is neither a refutation nor an argument, and **rule 5: cost 0 here is a reading,
not a measurement.** It licenses building the probe; it does not license a landing.

**R-b arrival set — `dyn Fn`(strict) in the acceptance corpus**: 30 `.logos`
programs, 14 of them PASS fixtures (`bc_objlt_*`, `deref_call_boxed_closure*`,
`boxed-*-closure-*-b167`, `coercion-as-explicit-cast`, …). Ceiling on
`bc_admits`: **0** — the other two `dyn Fn` admit rows (`borrowed-data-escapes-
closure-148392`, `issue-95079-missing-move-in-nested-closure`) are escape /
lifetime roots, measured by reading their headers, not kind roots.

## What deserves funding

1. **R-b, and NOT for the ledger.** h4/h7/h8 admit an illegal program at three
   `dyn` spellings with a 30-program arrival set, and its ledger ceiling is 0.
   That is soundness-queue material (`admits`), not `bc_admits` material. It is
   the larger defect and the ledger cannot see it.
2. **R-a is worth exactly one row.** The arm exists, prints upstream's sentence,
   and the whole repair is routing the declared RPIT bound into it. Ceiling 1,
   cost 0 by reading, no stdlib exposure. Small, but honest, and the round that
   takes it should build the probe rather than trust this table.
3. **NOT funded: `bck.A-FNMUT`.** It needs an arm that exists NOWHERE — a
   `mut`-binding requirement at a call of an FnMut-bounded callee. `refuse_not_
   mut_binding` is reached only from `take_borrow_whole_` and `check_recv_
   conflict`; a closure call takes no borrow of its callee at all. New mechanism,
   not a missing arrival.
4. **Reported, not filed (I price, I do not edit the corpus):** `-> impl Speak`
   returning a type that does not implement `Speak` compiles, and fails later at
   the call with `'A' has no method 'speak'` (h19/r19). That is a `diag`-shaped
   defect with no queue row, at the same position as R-a.
