# PREDICTION — declared BEFORE the edit, on build hash 8c2294bf39c2e079 43

## The defect, named at its minting site (not in the verifier)

`sema_enum_key` (src/compiler/sema.cpp:5267) composes the layout-ledger key for a
`Kind::Enum` by calling `concrete_struct_name_raw` — the STRUCT composer, which
spells a generic instance `Base$G<n>$arg` and folds `type_module_suffix`.
The ENUM composer is `Mono::enum_instance_name` (src/compiler/mono_impl.hpp:942),
which spells it `Base__arg` and folds no suffix.

`mono_abi_layout` ALREADY uses the enum composer, and its own comment
(mono_clone.cpp:526-532) says why in this exact vocabulary: "Keyed by the MONO
INSTANCE spelling (`Base__arg__arg`) — the one `record_needed_enum` mints and the
one mlir-gen registers the instance under — so a generic enum's row really lands
in the cross-engine comparison instead of in `n_unmatched`."

mlir-gen's `truth` is keyed off `enum_types_[ed.name()]` (mlir_gen.cpp:152), i.e.
the same mono-instance spelling. So TWO of three engines agree and sema is the
outlier. This is not a verifier normalisation and not a string rewrite: it is one
engine calling the wrong canonical composer for the kind it is naming.

## CLASS, by property (not by spelling)

Property: "a site that composes a ledger/registry key for a type of
`Kind::Enum` that has type arguments". Enumerated over all three engines:
  * mono_abi_layout  (mono_clone.cpp:533)  -> enum_instance_name   CORRECT
  * mlir-gen truth   (mlir_gen.cpp:152 via record_needed_enum)     CORRECT
  * sema_abi_layout  (sema.cpp:5267)       -> concrete_struct_name_raw  DEFECT
The class has exactly three members and exactly one is wrong. Stated so that
"one member" is a result of the enumeration, not an assumption.

## PREDICTED NUMBERS (falsifiable)

P1. `option_box_recursive_struct_field_list`: unmatched 5 -> 2.
    Closes T2 (sema tagged Option$G1$Location), T3 (sema niche
    Option$G1$Box$G1$Node), T4 (sema tagged Result$G2$void$Error).
    Does NOT close T1 (mono c-like logos.lang.cmp.Ordering) or
    T5 (sema product Ident$M2b09c0fe11e753e9) — different roots.
P2. The program-independent floor of 2 (custom_dst_smartptr_owning_drop,
    drop_glue_three_levels, zone_mut_fat_ref, coerce_4, and all six
    counter-examples) STAYS AT 2. The floor is not a generic-enum root.
P3. `sema_abi_layout` x tagged goes from 0 to NON-ZERO on every program that
    instantiates a generic payload enum (all 11 measured programs are at
    tagged=0 today).
P4. disagreements STAYS 0 on all 11. A new disagreement here would mean sema
    and another engine really differ in BYTES and the spelling was hiding it —
    that is a finding, not a regression, and would be reported not suppressed.
P5. No emitted symbol changes. `sema_enum_key` has exactly ONE caller
    (sema.cpp:5380, `lay::record`) and `lay::record` feeds only the verifier
    ledger. Control: the object-code oracles (run_oracle/fail_text/stdlib-cost)
    must be unchanged.

## THE TWO ROOTS THIS DOES *NOT* CLOSE — declined here, by name

R2 `$M<16hex>` on sema's STRUCT keys (T5, Ident$M2b09c0fe11e753e9): sema folds
   type_module_suffix into concrete_struct_name, mlir-gen's struct_types_
   registration path does not always. Different composer, different kind.
R3 `logos.lang.cmp.Ordering` (T1): `enum_types_` is keyed by the BARE enum name,
   so two different enums named `Ordering` in two packages cannot both be
   registered. MUST NOT be fixed by any by-name normalisation: both are 4/4, so
   a string fix reads GREEN while comparing two different types.

## COUNTER-EXAMPLE RISK THE FIX MUST SURVIVE (rule 5)

`Mono::enum_instance_name` uses `mangle_type`, whose Array arm is
`"arr" + arr_size() + "_"`. `mangle_type_for_name` (the struct composer sema uses
today) was FIXED for exactly this: a symbolic length collapsed to `arr0_`, the
G156-1 collision. So the fix moves sema onto a mangler that is KNOWN to collapse
`[T; N]` and `[T; M]`. ce2_array_lengths carries two array-length instances of one
generic enum to test whether that collapse produces a FALSE MATCH (two distinct
types on one key). A false match would be a WORSE outcome than the unmatched it
replaces, and is the number that would condemn this fix.

# ═══ RESULT, measured on build/bin/logosc with both arms in ONE binary ═══

A/B by `LOGOS_SEMA_ENUM_KEY_STRUCT_COMPOSER` (the old arm), unmatched keys
dumped by a measurement-only `LOGOS_DUMP_UNMATCHED` print in the
`!has_truth && !cross` arm. BOTH were removed before the landing build.

| program | unmatched OLD -> NEW | sema tagged | sema niche |
|---|---|---|---|
| option_box_recursive_struct_field_list | 5 -> 2 | 0 -> 2 | 1 -> 2 |
| custom_dst_smartptr_owning_drop | 2 -> 1 | 0 -> 1 | 0 |
| drop_glue_three_levels | 2 -> 1 | 0 -> 1 | 0 |
| zone_mut_fat_ref | 2 -> 1 | 0 -> 1 | 0 |
| coerce_4 | 2 -> 1 | 0 -> 1 | 0 |
| ce1..ce8 (8 counter-examples) | 2 -> 1 each | 0 -> 1 each | 0 |

CLOSED SET, diffed BOTH ways, 13 programs:
  closed: Option$G1$Location (sema/tagged), Option$G1$Box$G1$Node (sema/niche),
          Result$G2$void$Error (sema/tagged)
  OPENED: EMPTY on all 13. No key was pushed INTO unmatched by the change.
  disagreements: 0 -> 0 on all 13, and `declined` 0 -> 0.

## PREDICTIONS, scored

P1 5 -> 2  CONFIRMED, and the two survivors are exactly T1 and T5 as named.
P2 FALSIFIED, in the good direction. I predicted the floor of 2 would STAY 2.
   It went to 1. The prediction was written before the keys were dumped; the
   dump showed HALF THE FLOOR WAS ROOT 1 — `Option$G1$Location` is in every
   program because the prelude sizes it. I had asserted the floor "is not a
   generic-enum root" and it is, half of it. Recorded as a wrong prediction.
P3 CONFIRMED. sema x tagged was 0 on all 13 programs and is now non-zero on
   all 13.
P4 CONFIRMED. 0 disagreements. THIS IS THE ANSWER TO THE PROMPT'S THREE-WAY
   QUESTION for these keys: they were case 2 (A MISSING CROSS-CHECK), not
   case 3 (a real disagreement in hiding). Had the bytes differed, this change
   would have turned an invisible unmatched into a red build, which is the
   outcome the instrument exists to produce.
P5 CONFIRMED. No emitted-code change (see oracles in the round record).

## WHAT THE COUNTER-EXAMPLES DID **NOT** PROVE (rule 1, rule 5)

ce2 and ce7 exist to test the one way this fix could be WORSE than the defect:
`Mono::enum_instance_name` mangles an array as `"arr" + arr_size() + "_"`,
and `mangle_type_for_name` — the composer sema used before — was FIXED for
exactly that (the G156-1 collapse, where a SYMBOLIC length became `arr0_` and
`[T; N]` and `[T; M]` produced one symbol). If that collapse were reachable
here, two distinct types would file under ONE key and the verifier would read
GREEN on a comparison between two different types — worse than the unmatched
it replaces.

MEASURED: it is NOT reachable through this path in this corpus, and that is a
ZERO I am not entitled to read as safety. sema's enum ledger records only
64-99 answers per program and the ONLY generic-enum key it ever filed in all
13 programs is `Option__Location`. `Option<[i64; 4]>` and `Option<[i64; 8]>`
(ce7), instantiated explicitly with their `sizeof` taken, NEVER REACH
`sema_abi_layout`'s enum arm at all. So the array-collapse risk is
UNEXERCISED, not refuted. It is unchanged by this round in either direction —
mono and mlir-gen already used this mangler — but it is not closed and it is
recorded here as the live hazard nearest this change.
