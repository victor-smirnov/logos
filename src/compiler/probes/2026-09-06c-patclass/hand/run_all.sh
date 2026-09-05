#!/usr/bin/env bash
# run_all.sh <logosc> <dir>  -> one line per program, sorted
L=$1; D=$2; R=/home/logos/devel/logos/src/compiler/probes/2026-09-06-patmut/run1.sh
ls $D/*.logos | xargs -P 24 -I{} bash $R $L {} | sort
