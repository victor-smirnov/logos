# `tmcbsite` — the rule-5 counter-example corpus for `type_may_carry_borrow`

Not a test tier: nothing here is registered with ctest and nothing gates.
These are the 32 hand-written programs that discharge RULE 5 ("COST 0 IS NOT A
SAFETY CLAIM") for the erased-payload widening of
`borrow_check.cpp::type_may_carry_borrow`, one per CONSUMING SITE. The full
record — the 28-call site census, per-site arrivals/flips, the two legal
programs the widening refuses and the site each is attributed to, and the
four-site subset that closes the same three ledger rows at zero cost — is
`src/compiler/PROBES.md` § `tmcbsite`.

    bash docs/probes/tmcbsite/run.sh docs/probes/tmcbsite/adv7894.logos

compiles a program unarmed and then under `LOGOS_PROBE=tmcbdyn`, printing rc for
both and, from the ARMED run, `site:arrivals/flips` per consuming call site.
The site column needs `LOGOS_TMCB_FLIP`; the per-site attribution needs
`LOGOS_PROBE_SITE=<line>[,…]`. Both are read by `type_may_carry_borrow` and are
inert when unset. ⚠ THE LINE NUMBERS ARE KEYED TO THE COMMIT THAT WROTE THE
RECORD; re-derive them with `bash tools/dlog/ask.sh tmcb_sites.dl
src/compiler/borrow_check.cpp` rather than trusting the ones written down.

## OUTCOME — LANDED 2026-08-29, NARROWER THAN THE THING MEASURED
Rule 5 is **NOT** met for `type_may_carry_borrow` as a whole: this corpus found
the widening refusing two LEGAL programs, at sites the 1385-program cost
population had priced COST 0 —

    adv7894.logos    collect_ref_sources_paths' Call ENTRY  (census line 3951)
    ce7827c.logos    prov_of's #86 sub-site C               (census line 7841)

with `ce7827ctl.logos` the one-variable control for the second (same program,
`Cell<i64>` instead of `Cell<Box<dyn Give>>`: site reached, no flip, admitted).

So the knowledge landed at the FOUR arms that DO discharge rule 5, through a
separately-named entry `type_may_carry_borrow_erased` — §B6's MethodCall
by-flow gate, `prov_of`'s Call door, `check_return_value`'s holds_gate and
visit_stmt's #86 Let sub-site 2. Those close the same three ledger rows
(340 -> 337) and admit every program in this directory, both refusals included.
`run.sh` still compares unarmed against `LOGOS_PROBE=tmcbdyn`; since the
landing, "unarmed" IS the four-arm tree and the armed half measures only the
increment from the other 24 sites — which is where the two refusals above live.
