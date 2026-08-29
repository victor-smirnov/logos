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
