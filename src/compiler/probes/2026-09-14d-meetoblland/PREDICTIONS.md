# 2026-09-14d-meetoblland — PREDICTIONS BY NAME, written before the first build (base 47bea7f0aeb42d36 43)

Written after the source edits and before any build of them; the edits are uncommitted at this commit.

## The change (moblany as the compiler's behaviour, plus a printer)
A region binder offered two or more distinct regions is instantiated at a MEET TOKEN (`'%^N`) that keeps them:
structlit_lt_subst_'s meet, build_call_lt_subst_'s meet, lower_struct_lit's non-generic walk, both enum literal
sites (no covariance guard). outlives(): token on the sub side iff EVERY member, on the sup side iff ANY member.
check_variance names the members and the failing one. Build-time probes for this round only:
`mobleq` (token-aware rigid arm in lt_eq and Inv arm in lifetime_at), `moblencand` (enum walk's aggregate-arg
arm records candidates), `moblfat` (enum walk gains the fat-pointer region arm).

## bc_admits.ledger: 67 -> 65
    regions-creating-enums3               (lifereg.NEW-N1)  refused at the return, sentence names 'a and 'b
    regions-glb-free-free--glb-free-free  (lifereg.NEW-R19) refused at the return, sentence names 'a and the elided lifetime of parameter 's'
No other bc_admits row moves (diffed both ways against the ledger listing). bc_admits_blocked: 0 move.

## soundness_queue.ledger: 0 rows move (149).

## Harness columns, default (no probe): -L bc 0 failed · run_oracle 6752 common 0 changed (cast-region-to-uint subtracted) ·
fail_text_oracle 1583 common 0 changed except the two moved rows are not in its population · stdlib 4 of 4.

## Hand battery landbat (34 programs), default build vs base copy
    REFUSED (admitted on base): X106 X112 X114 X115 X124
    STILL ADMITTED by default: X118 (closes under moblencand) · X119 X120 (close under moblfat)
    STILL REFUSED: X102 X121 (refused on base)
    LEGAL, compile + same exit: L101 L102 L103 L105-L114 L118 L119 L121 L123-L128
    LEGAL MOVES refused -> compiles: L104 (arg 2 of `total<T>` compares two meet tokens, permissive: equal) — exit 6
    STILL REFUSED (pre-existing): L122 (where-clause literal, base refuses 'c: 'a); its sentence must not print `'%^`
Named risk: L102 under the default (Q<'a> over an invariant enum: token at an Inv lifetime_at is a string compare) —
predicted REFUSED by default, compiles under mobleq. Every stderr is scanned for `'%`.
