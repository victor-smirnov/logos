# 2026-09-13f-declarrivalland — PREDICTIONS, written before any landing edit

Base: HEAD 926ff2b24, build_hash 4cf0bf5e5e07b0cf 43 (read), bc_admits # TOTAL 72, soundness_queue # TOTAL 122, queue gate rc 0.
Diff budget declared before implementing: <= 180 lines of compiled source (src/compiler, include).

## The change (three arrivals of three existing checks, landed as mechanisms, not as the probes' code)
W  read_type_params reaches fold_where_bounds on EVERY arrival (no early return before the fold); the fold's add-fallback for an
   undeclared NAME subject becomes the existing "unknown type 'X'" error; collect_impl's header where loop asks the same question
   when the header was not folded by read_type_params (trait impls, param-less inherent impls).
W+ (measured separately under probe name `whtraitdecl` before it is kept or dropped) collect_trait asks it of a trait METHOD
   DECLARATION's where clause (wh5, I_w11, X_w06).
C  one helper counts a turbofish's LIFETIME args against the declaration at the enum tuple-ctor, struct literal and unit-variant
   arrivals (zero-declared included: the declaration is complete at lowering) — the type-position sentence.
K  one helper compares an impl item type's REGIONS with the trait's: trait binders renamed positionally to the impl's trait-reference
   lifetime args through subst_type_sema's existing lifetime map, subtype() under the impl's outlives, plus the Slice region subtype()
   never compares; used at the associated-const compare and the method-return compare (k3sig's only-header-binders guard kept).
   type_str prints a borrowed slice's region in source form, so the sentence names both regions.

## Predicted closed set — bc_admits, 3 rows, 72 -> 69
  outlives-with-missing                     "unknown type 'T'"
  constructor-lifetime-early-binding-error  "'E': expected 2 lifetime arg(s), got 1" / "got 3"
  trait-associated-constant                 FailStruct only, naming &'c and &'b
soundness_queue: 0 rows closed (122 before any new row).

## Predicted hand-battery movement (land0913f battery, 60 programs, keyed by file name vs base)
  newly REFUSED, all illegal: X_w01 X_w02 X_w03 X_w04 X_w07 X_w09 X_w10 X_c02 X_c03 X_c05 X_k02 X_k04 X_k05
  + under whtraitdecl only: X_w06
  NOT moved: X_c04 (code 131 door), X_c06 (syntax), X_k03 (already refused for another reason)
  LEGAL moved: 0
Pricing battery (122): the 26 names of rtunion; + wh5, I_w11 under whtraitdecl. Legal moved: 0.
