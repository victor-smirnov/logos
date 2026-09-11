# vgleak-2026-09-11 — TARGET ROWS AND WHY THIS BLOCK

Subject fixed by the prompt: `unrowed_backlog.ledger` `vg_leak_records`, the 39
leak allocation sites. No compiler source was edited and no probe was installed
this round — the instrument is valgrind and the work is attribution, so
`probe-batch.sh` / `ceiling-probe.sh` / `run_oracle.py` have no column to fill.

## WHY RE-MEASURE BEFORE ANYTHING ELSE

The prompt's own data is dated 2026-09-08, build `8e5f92705b285d29 43`. Today's
build is `19ff93338332ba46 43`. Between them `1979d72f4` landed "drop glue is
run `Drop::drop`, then drop the fields, at every depth" and CLOSED
`replace_site_skips_field_drop_glue` — the row the prompt and the backlog entry
both still call the one proven root inside the largest class. A grouping keyed
on a closed root is not a grouping. So the first move was a whole re-sweep, not
a pricing round.

## TARGET ROWS

There were none to target: `vg_leak_records` is a backlog ENTRY, not a row, and
the queue rows it touches (`operator_autoref_temp_never_dropped`,
`weak_local_never_dropped`) were re-verified rather than priced. The round's
output is one NEW row, `boxdyn_arg_deref_borrow_kills_box_drop`, and an entry
rewritten to what is left.
