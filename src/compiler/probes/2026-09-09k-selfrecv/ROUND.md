
# ═══ ROUND 2026-09-09k-selfrecv — THE `Drop::drop` RECEIVER IS ONE INSTANCE OF
# A GENERAL DEFECT: THE IMPL-VS-TRAIT **RECEIVER** CONFORMANCE CHECK IS DEAD BY
# TAUTOLOGY, ITS DIAGNOSTIC IS IN THE TREE UNREACHED, AND IT IS **TWO DOORS IN
# SERIES** WHOSE SECOND DOOR HAS A LANDED SIBLING ONE LINE AWAY ═════════════

Base build `a5f458f318529f21 43`, HEAD `33a1af40b`, tree clean at STEP 1.
Queue gate rc **0**, 77 rows (tier1 19 / tier2 9 / tier3 42 / tier4 7),
`# TOTAL` 77. `bc_admits` 90, `bc_admits_blocked` 8.
probe-log-lint: 264 records, every site symbol resolves.
Targets written before the compiler was touched:
`src/compiler/probes/2026-09-09k-selfrecv/TARGETS.md`.

## 0. FOUR CORRECTIONS TO THE PROMPT AND TO PROBES.md, EACH RE-VERIFIED

 1. **HEAD had moved.** The prompt's snapshot is `48cc71ae7`; HEAD at STEP 1 was
    `33a1af40b` — `c8af5a935`, `1276df5d9`, `33a1af40b` landed after it, all
    pattern-binding/borrow-check work, none of it drop work.
 2. **`drop_glue_three_levels` NO LONGER BLOCKS.** The prompt says it "pins `b3`
    while its own comment asks for `b3 a7`". Re-run on today's binary: it prints
    `b3 a7 `, rc 42, and its `.expected` says `b3 a7`. Re-pinned 2026-09-08.
    The other two blockers DO reproduce, by valgrind on today's binary:
    `custom_dst_smartptr_owning_drop` rc 42 GREEN over `Invalid read of size 8
    at __drop_in_place__A`; `bc_objlt_str_literal` rc 0 GREEN over
    `Invalid write of size 8` in `bc_objlt_str_literal$f__f__void`.
    **Two of three, not three of three.**
 3. **PROBES.md 2026-09-09sig says "`stdlib/lang/drop` DECLARES
    `fn drop(self: Self)`, so the by-value form is the trait's signature".
    THAT IS NOW FALSE.** `stdlib/lang/drop/drop.logos:11` reads
    `fn drop(self: &mut Self);`. The whole by-value corpus is therefore not
    "conformant to its own trait" — it is 251 impls that do not match a
    declaration the stdlib already spells Rust-canonically.
 4. **`probe-batch.sh` DOES NOT REVERT ITS EDITS ON SUCCESS** — only its INT/
    TERM/HUP trap does. Its own barrier text warns only about interruption. The
    tree was left dirty after the batch and a re-apply on top of it DOUBLE-
    applied the spec (caught by `grep -c`, reverted, re-applied once).

## 1. THE CENSUS THAT SETTLES PIECE 2 — 251/123 CONFIRMED, AND IT SPLITS 38/213

`fn drop(self: <non-reference>)`, counted with an anchored regex (an unanchored
`[^&)]` reads 1016 because `*` backtracks onto the space):

    251 sites in 123 files, **0 in stdlib/**            ← the prompt's number, exact
      67  tests/imported/pass/drop  (26 files)
      ... 77 files tests/logos/pass · 11 tests/logos/fail · 30 tests/imported/pass

**THE SPLIT NOBODY HAD MADE.** Of the 123 files, **90 declare their own
`trait Drop { fn drop(self: Self); }`** — those impls conform to their own
declaration and are a USER trait that happens to be named Drop. Only **33 files
(38 sites)** bind the STDLIB `Drop`, and those are the Rust-illegal ones.
**All 26 files under `tests/imported/pass/drop` are in the 90.** So the
prompt's warning about re-porting 67 sites is real but the arm never touches
them — and the honest reading is that those ports' local by-value `trait Drop`
IS the port artefact, since rustc's `Drop` takes `&mut self`. That is a corpus
decision with an owner; not edited here.

A THIRD population nobody counted: **8 shared-ref `fn drop(self: &T)` sites.**

## 2. UPSTREAM EVIDENCE, NOT A READING

`/home/logos/cxx/rust/tests/ui/compare-method/bad-self-type.rs` +`.stderr`
carries BOTH directions in one file:
  * trait declares a non-by-value receiver, impl writes `self` →
    `error[E0053]: method 'poll' has an incompatible type for trait ...
     expected 'Pin<&mut MyFuture>', found 'MyFuture'` +
    `help: change the self-receiver type to match the trait`
  * trait declares `fn foo(self)`, impl writes `fn foo(self: Box<Self>)` →
    E0053 as well.
The test is NOT ported into this tree. Neither divergence registry covers the
receiver: `docs/DIVERGENCES.md` has no row (all 17 A-rows read), and no
`docs/spec/*.md` clause names impl-vs-trait receiver conformance. So the
standing rule applies and the answer is Rust's — which is also the owner's
2026-09-09 decision.

## 3. THE ROOT — A TAUTOLOGY, AT ONE LAMBDA

`src/compiler/sema_collect.cpp::_self_shape_artefact`, last line:

    return !types_equal(sub, impl_t);

The receiver check downstream is
`if (!_t0_collapsed && !_alpha_ok(_t0, c->param_types[0], 0, false))`.
`_t0_collapsed` is true **exactly when** the substituted trait slot and the
impl's written slot differ — i.e. exactly when the check would have something to
say. The check can therefore only ever fire on types that are already equal.
**The receiver conformance check is dead by construction for every trait method
whose declared receiver mentions `Self`**, and the sentence it would print —
`"the receiver is declared '{}' and the impl declares '{}'"`,
sema_collect.cpp:4686 — is in the tree, unreachable, today.

LIVE-SITE PROOF, three ways: 1,632,950 arrivals over the run-oracle population;
h11 (a non-receiver param mismatch) IS refused; h12 (a return mismatch) IS
refused with the sibling sentence that landed in 2026-09-09sigland.

## 4. PRICED — THREE ARMS, TWO BUILDS

probe          fires      ceiling  cost  cfail  runtime  stdlib  verdict
sigselfnone    1626006    98*      1468  1471   —        ⛔      STOP
sigselfdepth   1632950    0        8**   0      **29**   ok      the half that is safe
sigselfty      SEE §6     …        …     …      …        …       the series completed

`*` not a ceiling: the stdlib did not compile, so every `bc_admit` fixture
failed to compile and 98 rows counted as "closed" (rule 11). `sigselfnone`
reproduces, verbatim, the four sites that condemned `sigrecvty` in
2026-09-09sig — `impl Pattern for str` (`find_in`, `match_len`),
`impl ToString for str`, `impl WritField for str`, all
*"the receiver is declared `[u8]` and the impl declares `&[u8]`"*. An
independent confirmation of that record, taken by a differently-written probe.

`**` **AND THIS IS THE MEASUREMENT WORTH KEEPING.** `ceiling-probe.sh` printed
COST = 8. `run_oracle.py` over 6610 pass fixtures compiled, linked and RUN, both
directions from ONE configure (armed build `ebe93d7ff05f3832 43`), says **30
rows moved, 29 after subtracting `cast-region-to-uint` by name**. The cost
column undercounts by 3.6×, and the 21 it cannot see are simply outside
`-L bc -L pass` plus the three directories. Diffed BOTH ways against a
hand-compile of all 33 candidate files: 30 refused, of which 29 are registered
pass fixtures and the 30th is a queue-row program (below). Empty in both
directions.

## 5. RULE 2 — HALF A MECHANISM IS NOT ONE. THE ARM IS TWO DOORS IN SERIES.

`sigselfdepth` keys the exemption on the SHAPE OF THE DIFFERENCE (the
indirection prefix of the substituted slot vs the written one) — which is
exactly the repair 2026-09-09sig named, and it survives the stdlib because
`&[u8]` and `[u8]` are both `Kind::Slice` and so carry the SAME prefix. But it
closes only the by-VALUE half:

    h2  trait `&mut Self`, impl `&S`      depth ADMITS   ty REFUSES
    h9  trait `&mut Self`, impl `&S`      depth ADMITS   ty REFUSES

because after the exemption is out of the way `_alpha_ok` still compares
LIFETIME STRINGS ONLY, and `&S` and `&mut S` both collect the empty list. That
is the same defect 2026-09-09sigland fixed for the RETURN slot by adding
`|| !types_equal(tra, c->ret_type)`. **The receiver twin of that one line has
never been written.** `sigselfty` = `sigselfdepth` + that line.

## 6. THE HAND TABLE — THIRTEEN PROGRAMS, VARIED BY SHAPE (RULE 5), ALL
##    MULTI-LINE, ALL READ ON THE BASE BINARY FIRST

    prog  what it varies                                base depth  ty
    h1    by-value `self: S` vs stdlib `&mut Self`        0    1     1
    h2    SHARED-ref `self: &S`                           0    0     1
    h3    CONTROL `&mut self` shorthand                   0    0     0
    h4    CONTROL explicit `self: &mut S`                 0    0     0
    h5    by-value on a GENERIC struct                    0    1     1
    h6    by-value + body moves a field out (E0509)       1    1     1
    h7    RULE-9 TWIN: a NON-Drop trait, `&mut Self`      0    1     1
    h8    the 90-file shape: LOCAL by-value trait Drop    0    0     0
    h9    non-Drop, trait `&mut Self`, impl `&S`          0    0     1
    h10   REVERSE: trait by value, impl `&mut S`          0    1     1
    h11   LIVE-SITE: non-receiver param mismatch          1    1     1
    h12   LIVE-SITE: return mismatch                      1    1     1
    h13   by-value Drop reached through a generic bound   0    1     1

**h7 IS THE ROUND'S WIDENING.** A trait called `Tick` declaring
`fn tick(self: &mut Self)`, implemented `fn tick(self: S)`, compiles and RUNS
today. **The defect is NOT about `Drop`.** PIECE 2 as the prompt states it is
one instance of a general signature-conformance hole, and the receiver arm buys
the whole class, not 251 fixtures' worth of `drop`.

h6 is a text-only cost an rc column cannot see: it was already refused (E0509)
and now refuses on the receiver first — a DIFFERENT sentence for the same rc.

## 7. ⚠ A QUEUE ROW IS IN THE REFUSAL SET, AND IT WOULD READ AS CLOSED

`tests/soundness/open/unsized_local_binds_place_dropped_after_free.logos`
(tier 2, `admits`) carries an incidental `fn drop(self: A)`. Armed, it is
REFUSED — for the receiver, not for the unsized-local defect the row records.
The queue gate checks that an `admits` row still admits, so the row would go
RED and read as closed by a landing that never touched its defect.
**The receiver landing must rewrite that program's receiver to `&mut A` in the
same commit**, or it silently corrupts a row. Same shape, one tier up:
`custom_dst_smartptr_owning_drop` is in the 29 and is already a corpus decision
with an owner (pinned green over a use-after-free).

## 8. PIECE 1 — CARRIED, NOT RE-PRICED, AND ITS BLOCKER IS GONE

`struct_fields_dropped_reverse_order` / `tuple_elems_dropped_reverse_order`
were priced to completion earlier the same day (`dropdecl_*`, build
`a440722a531c9492 43`): four reverse walks, two roots, each root two sites in
series, nine hand programs, twelve runtime triples, the spec cost with rule ids.
Both rows still reproduce (queue gate rc 0). That record's verdict was
"DO NOT FUND WITHOUT THE OWNER"; the owner decided on 2026-09-09 and the
decision is Rust-canonical, so **the blocker is discharged and the arm is
fundable as recorded.** Re-measuring it identically would be an operation that
cannot change the hypothesis. Two things this round adds to its price:
  * the spec clause `expr.drop.struct-user-drop-then-fields` justifies the
    current behaviour with *"the by-value self of the user drop already
    consumed the fields"* — a REASON that PIECE 2 deletes. The two pieces are
    independent landings but they share one stale clause.
  * both clauses cite `mlir_gen_stmt.cpp#L880-L938` / `#L985-L995`; the four
    loops are at 1080 / 1101 / 1395 / 1414. The citations are stale line
    numbers in a file the citing document does not own.

## 9. THE SECOND DOOR COSTS NOTHING — DIFFED BOTH WAYS, EMPTY BOTH WAYS

`sigselfty` priced on its own build `e0f73c54f3a0dfdc 43`, its unarmed control
taken on the SAME build (a fail-text baseline self-invalidates across a
rebuild, so both halves come from one configure):

    fires 3128198 · CEILING 0 · COST 8 · COST-fail **0 of 1478**
    (rc 0, .expected-match 0, text-only 0) · stdlib all four layers compile
    run_oracle 6610 fixtures: **30 rows moved, 29 after `cast-region-to-uint`**

**The same 30 names, character for character, as `sigselfdepth`.** Diffed in
both directions: empty. So completing the series — adding the receiver's
`|| !types_equal(...)`, the twin of the return-slot line that landed in
2026-09-09sigland — closes the `&Self`/`&mut Self` half (h2, h9) for **zero
additional corpus cost**.

WHY THAT ZERO IS REAL AND NOT A BROKEN HOP (rule 1, rule 11). The site is
proven live twice over: 3.13 M arrivals, and h2/h9 do refuse with the right
sentence. The corpus zero is a population fact, counted: the tree holds exactly
**8** shared-ref `fn drop(self: &…)` lines — three of them are local
`trait Drop { fn drop(self: &Self); }` DECLARATIONS (`tests/spec/pass/item_5`,
`tests/logos/pass/stmt_expr_temp_drop`) whose impls therefore conform, and the
other three (`bc_dropident_diffsig_{nested_field,enum,moved}`) return `i32`/`i64`
and are INHERENT methods named `drop`, not trait impls. **Nothing in the tree
implements a `&mut Self` trait method with a `&Self` receiver.** The shape is
absent, not exempted.

## 10. WHAT DESERVES FUNDING

 1. **`sigselfty` — the receiver conformance check, both doors, as one
    landing.** stdlib clean, cfail 0 of 1478, runtime 29 of 6610, and the
    diagnostic already exists and is already right. It is the receiver twin of
    a line that landed hours earlier for the return slot. It buys a CLASS, not
    a corpus conversion: h7 shows an ordinary trait's `&mut self` method
    implemented by value compiles and runs today.
 2. **The 29 are a corpus conversion, not a cost to argue about** — 33 files,
    of which 30 need only the receiver spelling changed; none of them is an
    imported port, because all 26 `tests/imported/pass/drop` files declare
    their own by-value `trait Drop`. Corpus-and-spec FIRST, its own commit, on
    the unmodified compiler, exactly as the prompt orders — the arm is what
    proves the conversion complete, not what makes it green.
 3. ⚠ **BLOCKING, BEFORE ANY OF IT**: rewrite the receiver in
    `tests/soundness/open/unsized_local_binds_place_dropped_after_free.logos`.
    Its `fn drop(self: A)` is incidental and the landing would red its row for
    the wrong reason.
 4. **The 213 by-value impls under a LOCAL `trait Drop` are a corpus decision
    with an owner.** They are legal against their own declaration; whether a
    user trait may be named `Drop` at all is the question, and 26 imported
    ports depend on the answer. Reported, not edited.
 5. **`tests/ui/compare-method/bad-self-type.rs` is not ported** and is the
    upstream fixture for exactly this rule, both directions.
 6. **PIECE 1 (field drop order) is fundable as already priced** — its only
    recorded blocker was the owner's decision, which now exists.
 7. ⛔ **`sigselfnone` — DO NOT LAND**, for the reason `sigrecvty` was stopped:
    it breaks the stdlib at the four `str` DST sites.
