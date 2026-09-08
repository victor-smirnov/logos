# ROUND 2026-09-08-fatrepr — MEASUREMENTS (filled as they land)

Base build `2c5a33ef99e94fc9 43`, HEAD `8edce4409`. Probe build: one build, four
edits, L1 rc 0 with nothing armed (probe-batch proved the batch inert).

## 0. STEP-1 CENSUS
    soundness queue gate rc 0 — 80 rows (t1=27 t2=7 t3=42 t4=4), '# TOTAL' 80
    bc_admits 91 · bc_admits_blocked 15 · probe-log-lint 248 records
    all four target rows re-verified on today's binary: all still reproduce
      zonemut…layout_abort            rc 134, "[emitted] …H: size — layout_of says 16, llvm::DataLayout says 8"
      fatslice…invalid_mlir           rc 1,   "'llvm.getelementptr' op operand #0 … got '!llvm.struct<(ptr, i64)>'"
      enum_variant_ctor…coercion      rc 1,   "E::S arg 0: expected &[i64], got &[i64; 3]"
      method_with_unsized_wrapper…    rc 1,   "method call: 'Holder' has no method 'eat'"

## 1. A PROMPT CORRECTION, VERIFIED AGAINST THE PROMPT IN FRONT OF ME
The STEP-1 gate command DOES carry `LOGOS_LIB_DIR` in the text I was given, and
the gate answered rc 0 with it. Nothing to re-report. (Rule: a complaint about
an instruction is a claim with a timestamp.)

## 2. THE ROW HEADERS THAT ARE WRONG, MEASURED TODAY
Both were re-measured on today's binary, and both are wrong in a way that
changes what the fix has to do.

  * `method_with_unsized_wrapper_param_not_found` says "an inherent METHOD whose
    by-value parameter is `Rc<dyn Sp>` IS NOT FOUND AT ALL". Measured, 15 hand
    programs, base build:
      - the method DECLARED and never called compiles (rc 0) — it is registered;
      - the SAME call with the argument already typed `Rc<dyn Sp>` (`let rd:
        Rc<dyn Sp> = rc;`) compiles and RUNS rc 0 — the method is found;
      - the TRAIT-method spelling fails identically, so "inherent" is not the
        property;
      - `&Rc<dyn Sp>` fails too, so "by-value" is not the property;
      - `Box<A>` -> `Box<dyn Sp>` and `&A` -> `&dyn Sp` at the SAME method-arg
        position compile and run.
    THE PROPERTY IS: an argument whose type must UNSIZE to reach the parameter
    is rejected by the method-candidate SELECTOR, before any coercion runs, and
    the rejection is reported as "has no method". The site is
    `arg_compatible_for_dispatch` (sema_impl.hpp:8099), whose own comment states
    the rule it then only implements for ONE coercion: "The selector must accept
    what the coercion pipeline can produce, or the candidate is rejected before
    the coercion ever runs and the call reports 'no method' instead of coercing."
    It carries `&[E;N]` -> `&[E]` and nothing else.
  * `zonemut_fat_ref_struct_field_layout_abort` says the abort is one site
    ("layout_of counts the field fat and llvm::DataLayout builds it thin"). It
    is THREE engines: with the LLVM struct type builder repaired (probe
    `fatfield`), `LOGOS_VERIFY_LAYOUT=1` still aborts on
        [product] H: size — mono_abi_layout says 8, llvm::DataLayout says 16
        [product] H: size — sema_abi_layout says 8, llvm::DataLayout says 16
        [product] H: size — layout_of says 16, mono_abi_layout says 8
        [product] H: size — layout_of says 16, sema_abi_layout says 8
    (5 disagreements unarmed -> 4 armed; the two `[emitted]` ones are the ones
    the arm removes). `layout_of` is the only engine of four that counts the
    field fat. Doors in SERIES (rule 2): repairing one gives a compiler that
    writes an object file and a stricter check that still says the layout is
    inconsistent.

## 3. THE PROBE TABLE (one build, four edits, five arms)

    probe          site                                    fires  ceiling cost cfail stdlib
    fatfield       mlir_gen_types.cpp register_struct        3173      0     0     0   ok
    fatbind        mlir_gen_stmt.cpp  bind_struct_field        27      0     0     0   ok
    enumunsize     sema_expr.cpp      mask_for(Operand)       210      0     0     0   ok
    dispunsize     sema_impl.hpp      arg_compatible_…        1898      0     0     0   ok
    dispunsizeagg  same site, aggregate_unsize_pending twin    —       —     —     —    —

`fires` is the ARRIVAL count at each site over the ceiling population, so every
site is proven LIVE (rule 1). `cost 0 / cfail 0 / stdlib ok` is the FULL cost
line: 0 legal programs refused over pass(ledger+legal), 0 of 1457 `-L bc -L fail`
fixtures changed in rc, `.expected`-match or text, and all four stdlib layers
compile under each arm.

⚠ CEILING 0 FOR ALL FIVE, AND IT IS NOT A REFUTATION: these four defects have no
representative in the ledger population at all — the ledger prices BORROW-CHECK
rows, and the soundness queue's programs are not in it. The number that matters
here is the hand matrix below, and the cost columns are what they are for: the
BLAST RADIUS.

## 4. THE HAND MATRIX — 29 programs x 5 arms + the four row programs

`rc=0` means compiled, linked, ran and answered correctly; `REF` = refused;
`CCFAIL` = the compiler aborted. Base column = the same binary with nothing armed.

    program   base  fatfield  fatbind  enumunsize  dispunsize  dispunsizeagg
    B1  slice field, projected through a by-value binder
              REF    REF       rc=0     REF         REF         REF
    B3  `str` field, same door, `.len()`
              REF    REF       rc=0     REF         REF         REF
    B7  the binder ESCAPES through a fn taking the struct by value
              REF    REF       rc=0     REF         REF         REF
    B4  `&dyn Tr` FIELD, method called through the binder  ← LEGAL, rc 0 at base
              rc=0   rc=0      rc=139   rc=0        rc=0        rc=0
    B2 B5 B6 B8 (unprojected binder, thin field, `ref` binder, `let` door)
              rc=0   rc=0      rc=0     rc=0        rc=0        rc=0
    E1 E2 E3  enum ctor unsize: tuple variant, struct variant, arg index 1
              REF    REF       REF      rc=0        REF         REF
    E6  ILLEGAL `&[u8;2]` into `&[i64]` — must STAY refused
              REF    REF       REF      REF         REF         REF
    E4 E5 E7  compound-assign RHS, const init, the `let`-routed twin
              rc=0   rc=0      rc=0     rc=0        rc=0        rc=0
    F1 F5 F6 F7 F8  fat `&mut` in a field: named, tuple-struct, write-through,
                    method receiver, nested — all built with `zone_mut_ref`
              CCFAIL rc=0      CCFAIL   CCFAIL      CCFAIL      CCFAIL
    F2 F3 F4  shared `&` field, thin `&mut` field, enum payload
              rc=0   rc=0      rc=0     rc=0        rc=0        rc=0
    M1 M2  `Rc<A>` at an `Rc<dyn Sp>` param: inherent method, trait method
              REF    REF       REF      REF         rc=0        REF
    M4  ILLEGAL `W<bool>` at a `W<i64>` param — must STAY refused
              REF    REF       REF      REF         REF         REF
    M3 M5 M6  overload on one wrapper base, free fn, already-typed arg
              rc=0   rc=0      rc=0     rc=0        rc=0        rc=0
    ROW zonemut…layout_abort
              CCFAIL rc=0      CCFAIL   CCFAIL      CCFAIL      CCFAIL
    ROW fatslice…invalid_mlir
              REF    REF       rc=0     REF         REF         REF
    ROW enum_variant_ctor…
              REF    REF       REF      rc=0        REF         REF
    ROW method_with_unsized_wrapper…
              REF    REF       REF      REF         rc=0        REF

## 5. WHAT THE MATRIX SAYS

  (a) FOUR ROOTS, NOT ONE, AND THE SETS DO NOT OVERLAP AT ALL. Each arm closes
      EXACTLY its own row and moves no other program in the matrix. Additivity
      was CHECKED, not assumed (rule 13): no candidate change moves two rows.
  (b) `dispunsizeagg` — the twin arm at the SAME site asking the question
      through `aggregate_unsize_pending` — closes NOTHING. The two names were
      needed (rule 9): the fact `Rc<A>` can reach `Rc<dyn Sp>` is not visible
      through that predicate, so a fix must add a wrapper-unsize test of its own.
  (c) THE ONLY DAMAGE ANY ARM DID IS FATBIND's, AND NO COST COLUMN SAW IT: B4 —
      a `&dyn Tr` FIELD whose method is called through a by-value struct-pattern
      binder — runs rc 0 at base and SEGFAULTS (rc 139) under the arm, while
      cost/cfail/stdlib all read 0/0/ok. The corpus contains no such program.
      A real fix must be narrower than the crude arm: FatSlice (`&[T]`/`str`)
      only, or a FatDyn case that also fixes the method-receiver convention.
  (d) fatfield is the arm with the widest positive evidence and NO negative:
      five hand programs across four field spellings compile, link and RUN with
      both halves of the pair correct — including `zone_of` through a nested
      field and a `self: &mut Box2` method receiver read out of a field.

## 6. THE RUNTIME COLUMN — `scripts/run_oracle.py`, 6555 pass fixtures COMPILED, LINKED, RUN

Baseline taken from the same binary with nothing armed; each arm diffed against
it on the triple (logosc rc, program rc, stdout sha); `cast-region-to-uint`
subtracted by name (it prints a stack address).

    fatfield     0 of 6555 changed
    fatbind      0 of 6555 changed      ← and it segfaults hand B4
    enumunsize   0 of 6555 changed
    dispunsize   0 of 6555 changed

So all four arms read ZERO in all five columns — ledger ceiling, pass cost,
`-L bc -L fail` text, stdlib, and the runtime triple — and one of them creates a
silent miscompile in a legal program. B4 under `fatbind`: logosc rc 0, link rc 0,
run SEGV 139, re-verified by hand outside the runner.

## 7. WHAT DESERVES FUNDING, IN ORDER

  1. `enum_variant_ctor_arg_no_unsize_coercion` — CHEAPEST AND CLEANEST. One
     mask, one position: the enum-variant ctor argument is an ARGUMENT and must
     not be priced as `CoercePos::Operand`. The crude arm (widen the whole
     Operand position) already closes the row and all three ctor spellings, keeps
     the illegal `&[u8;2]` refused, and moves nothing else in any column. The
     careful fix is narrower still — a `CoercePos::EnumCtorArg` with the CallArg
     mask — so its blast radius is strictly smaller than what was measured here.
  2. `method_with_unsized_wrapper_param_not_found` — SAME SHAPE, ONE LEVEL
     EARLIER, and it fixes a wrong DIAGNOSTIC for a whole class as a side effect.
     The site states its own rule in a comment; the fix is the second clause of
     that rule. ⚠ the crude arm accepts any same-base struct pair, which is too
     much — the fix must test the WRAPPER UNSIZE, and the `aggregate_unsize_pending`
     twin is measured NOT to answer it, so the predicate has to be written.
  3. `zonemut_fat_ref_struct_field_layout_abort` — the prompt's recommendation,
     and the measurement moves it DOWN, not out: the arm-that-exists is real and
     the corpus radius is nil (no struct in tests/ or stdlib/ holds a `&mut`
     to a `#[zone_mut]` type; 3173 arrivals, 0 of them fat), but the row needs
     THREE engines carrying the same fact, not one. Fund it as a three-site
     change with `LOGOS_VERIFY_LAYOUT=1` as the acceptance oracle, never the
     default cross-check alone.
  4. `fatslice_field_match_binder_invalid_mlir` — LAST. The one-line convention
     swap closes the row and breaks a legal neighbour at RUN TIME with every
     cost column at zero. It needs the FatDyn method-receiver convention worked
     out first, and that is a design question, not a repair.
