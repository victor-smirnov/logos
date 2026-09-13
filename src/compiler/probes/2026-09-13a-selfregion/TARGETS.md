# 2026-09-13a-selfregion — TARGETS, WRITTEN BEFORE THE COMPILER WAS TOUCHED

Base build `a5088a6875e092aa 43` (read with `scripts/build_hash.py`; logosc sha256 57a8f7afa6a7e4d8),
HEAD `1940f93fe`. Queue gate rc 0, `# TOTAL 100` (100 rows by listing); bc_admits `# TOTAL 76`
(76 by listing); blocked 8. probe-log-lint 280 records.

## TARGET ROWS (tests/logos/bc_admits.ledger)

    issue-55394--b   nllmoves.NEW-3   tests/imported/admit/nll/issue-55394--b
    issue-98170      nllmoves.R13     tests/imported/admit/nll/issue-98170

## WHY THIS BLOCK

* Both roots are NEVER SURVEYED: PROBES.md names them only in lists of zero-record roots
  (09-12 §never-surveyed, twice). No round has a control, a census or a probe on either.
* Both are the shape that has paid: an ARM THAT EXISTS (the return-type variance check refuses
  the free-fn and the NAMED-impl-region spellings of the same escape) reached through a fact the
  code does not carry (what `Self` denotes inside an impl whose self type has regions).
* Excluded by name and by note: the lifereg.B / NEW-B2 door plane; the three A16-blocked rows
  (controls re-verified today, still A16's); bck.D/nllmoves.D (09-08 decline in the file);
  bck.NEW-CAPMOVE (five arms installed); bck.A-FNMUT and argresvact (pins).

## ONE-VARIABLE CONTROLS ON THE BASE BINARY (scratchpad ctl/, compile+link+run)

    n3a  impl Foo<'_>   fn newf(bar:&mut Bar) -> Self   { Foo{bar} }      ADMITTED  (= row)
    n3b  impl<'s> Foo<'s>  same body, -> Self                              REFUSED  variance, expected Foo<'s>
    n3c  impl<'s> Foo<'s>  -> Foo<'s>                                      REFUSED
    n3d  free fn newf<'s>(bar:&mut Bar) -> Foo<'s>                         REFUSED
    r13a impl MyStruct<'_>  make<'a>(f:&'a) -> MyStruct<'a> { Self{f} }    ADMITTED  (= row)
    r13b impl<'q> MyStruct<'q>  same                                       ADMITTED  <- NOT the '_
    s3   impl<'q> MyStruct<'q>  make<'a>(f:&'a) -> Self { Self{f} }        REFUSED  "got MyStruct<'a>"
    r13f free fn make<'a,'q>(f:&'a) -> MyStruct<'q> { MyStruct{f} }        REFUSED

So the two rows are NOT hypothesised to be one fact: NEW-3 flips on naming the impl region
(n3a/n3b); R13 does not (r13b), and its literal `Self{..}` is typed from its field, not from the
impl's self type. Grouping to be tested by ONE candidate change moving both, not assumed.
