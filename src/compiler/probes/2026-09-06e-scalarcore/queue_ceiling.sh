#!/usr/bin/env bash
# queue_ceiling.sh <probe-name> — the SOUNDNESS QUEUE ceiling of one arm.
# The gate itself is the oracle: it re-runs every row's program and REDS on any
# row that stopped exhibiting its recorded behaviour. Armed, that red list IS
# the set of rows the arm closes, BY NAME (a ceiling bounds the count, not the
# set — read the names).
cd /home/logos/devel/logos || exit 2
N="${1:?probe name}"
export LOGOS_LIB_DIR=$PWD/build/lib/logos
LOGOS_PROBE="$N" bash tests/logos/soundness_queue_gate.sh build/bin/logosc \
    tests/logos/soundness_queue.ledger . 2>&1
echo "GATE_RC=$?"
