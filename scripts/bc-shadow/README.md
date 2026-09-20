# Shadow measurement of the new borrow checker (ADR 0028)

    git ls-files 'tests/*.logos' 'examples/*.logos' | sed "s|^|$PWD/|" > /tmp/all.txt
    scripts/bc-shadow/rerun.sh /tmp/all.txt /tmp/shadow.log        # ~28 min, 11k inputs, -P 12
    python3 scripts/bc-shadow/summ.py /tmp/shadow.log 20           # census + classes
    python3 scripts/bc-shadow/top.py  /tmp/shadow.log new_only "use of moved" 30

`old_only`: the old checker refuses, the new one does not (a miss of the new one,
or an over-refusal of the old one). `new_only`: the reverse. `skipped`: a function
the BIR lowering does not handle yet (`bc-skip` lines name the reason).

Working loop. A full run gives the disagreeing inputs:

    grep -A1 '^bc	' /tmp/shadow.log | grep input | sed 's/  input: //' | sort -u > /tmp/disagree.txt

Re-running only those takes under a minute. It shows what ALREADY disagreed and
cannot show a regression in what agreed, so re-derive it from a full run every
few changes. Two fixed samples are partial guards: ~1500 inputs from
bc/borrowck/nll/moves, and ~1500 that declare generics (for the `bcg` census).

Debug aids: `LOGOS_BIR_DUMP=<substring of a function name>` prints its BIR and the
derivation of each error; `LOGOS_BIR_LABELS=1` prints every call's result/source
region labels and the field projections that gave up; `LOGOS_BIR_GAPNAME=1` names
the receiver in a bare-receiver skip. rustc is the oracle for a disputed program:
transcribe it and ask.
