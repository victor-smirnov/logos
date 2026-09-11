# RESULT — 2026-09-11d, `lifereg.B`

builds, all READ:
  base      411ed23b6b9fea05 43   (session opening, unmodified tree)
  armed-1   6820bf7c18965cb7 43   `liferegbparam` + census        -> ceiling/cost run
  armed-2   4930d1ed7bc8cdb6 43   + `liferegbboth` (index descent)
  armed-3   e830237ffdcef89b 43   + per-DOOR census               -> run_oracle

## 1. THE ARRIVAL CENSUS DECIDED THE BLOCK BEFORE ANY ARM DID — FOR THE FOURTH ROUND RUNNING
`liferegb.arrive` (a PARAM-rooted value reaching `note_holder_escape_prov`):
    mut-slice-struct-lifetime-transmute--c17  (field door)   1
    mut-slice-struct-lifetime-transmute--t17  (slice door)   0
    X4 field, no reborrow   1   ·  X5 index, no reborrow  0  ·  X3 deref  0
Over ALL 86 ledger programs, accumulated: `liferegb.arrive` = **1**, and it is
c17. `liferegb.recvdoor` = 1, and that one is `.noborrow`.

## 2. THE GROUPING IN THE LEDGER IS REFUTED — TWO ROWS, TWO MECHANISMS
The file records (2026-09-08) "the two rows are one mechanism at two
projections". ONE candidate change does NOT move both:
  `liferegbparam`  closes c17, leaves t17 — ceiling 1 of a predicted 2.
  `liferegbboth`   (params + the `lifereg_indexstore` array descent) newly
                   refuses X5 (`out[0]=y` on an ARRAY local) and STILL leaves
                   t17 admitted with ZERO arrivals at every one of the five
                   deposit doors.
t17 writes through a `&mut [&i64]` SLICE local. It is not a field write, not an
array-index write of the root, and — measured, `liferegb.recvdoor` never fires
on it — not the `SDerefWrite(MethodCall(index_mut …))` receiver-store door the
source comment at :13488 says such a write takes. **The fact is not merely
uncarried at t17's door; the door is not reached at all.**

## 3. THE ARM, AND WHY IT IS NOT FUNDABLE AS SPELLED
`note_holder_escape_prov` records the ESCAPE fact only and never `params` — its
own comment says the skip is deliberate. The probe ORs `vp.params` into
`prov_[name]`. The refusing arm already exists (`check_return_value` case 2,
:9396) and fires today on the root-local spelling.
COST 0 IN EVERY CORPUS COLUMN AND THREE LEGAL PROGRAMS REFUSED BY HAND:
  L1  sibling FIELD still carries 'a  (`p.b = y; return p.a;`)     REFUSED
  L5b OVERWRITE (`u.h = y; u.h = x; return u.h;`), legal Rust      REFUSED
  L7  sibling ELEMENT (under `liferegbboth`)                       REFUSED
The deposit is ROOT-keyed and ADDITIVE. The same two properties that make the
helper correct for the escape bits make it wrong for `params`: an escape bit is
monotone in the holder, a REGION obligation is per-PLACE and is killed by a
later write to the same place. The fundable shape is a PER-FIELD-PATH record —
which this file already built once, for dropck (`dropck_field_srcs_[root][path]`,
:13322), with the pair that forced it pinned in
`fail/bc_dropck_field_two_paths_fail` and its `_swapped_` twin.

## 4. A PRIOR NEGATIVE RESULT AT A DIFFERENT SITE, AND WHY IT READ ZERO
`borrow_check.cpp:13225` records: 2026-08-28, 187 fires, CEILING 0, COST 0,
"Predicted c17 (and predicted --t17 would NOT close). Neither closed." That
round probed the §B6 `ref_sources` walk. **A ROOT NAME IS NOT A SITE**: the
same root at the `prov_` deposit closes c17 at ceiling 1. The 2026-08-28
prediction about t17 is confirmed here with the mechanism it lacked.

## 5. DLOG — NEW RULE, DECLARED, WITH ITS KNOWN-ANSWER CONTROL
`tools/dlog/selftest.sh` RUN FIRST, rc 0: 19 walkers / 24 findings / try_path
1-5 / domain 42-5; duty discriminates across 756aed65 (1 -> 0).
New question `tools/dlog/provdep.dl` (claims `provdep_accessor.claim` /
`provdep_container.claim` are INPUTS, not in the rule):
  holder_call    5 call sites, 3 contexts — visit_stmt 12836/13389/13505,
                 apply_flow_outparams 4945, visit 15523.
  container_ref  53 references in 14 contexts.
KNOWN-ANSWER CONTROL, stated before the run and reproduced: the helper appears
in `container_ref` (it deposits) and NOT as a consumer of itself;
`check_return_value` appears in NEITHER, because it reads `prov_` only through
`prov_of` — and `prov_of` is in `container_ref`. It is.
CROSS-CHECK PER SITE, both numbers side by side as the standing warning
requires: `grep -n "note_holder_escape_prov("` = 5 call sites + 1 definition;
dlog = 5 calls. AGREE.
⚠ WHAT DLOG ADDS, AND IT CHANGES THE VERDICT: `container_ref` = 53 sites in 14
contexts means `prov_` is written from many places, so an edit at this ONE
helper is an INSTANCE fix, not the class fix the same shape bought yesterday at
`closure_caps_of` (where the absence relation was empty). **That absence is the
thing a grep cannot find, and here it is NOT absent.**
⚠ A STALE COMMENT CAUGHT BY THE TOOL: `note_holder_escape_prov`'s own header
says "ONE helper, FOUR call sites". There are FIVE.
