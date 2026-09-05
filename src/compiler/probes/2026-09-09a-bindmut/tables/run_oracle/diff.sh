#!/usr/bin/env bash
# diff.sh <arm> — rows whose (ccrc, runrc, sha) moved against base.tsv; cast-region-to-uint subtracted by name
cd "$(dirname "$0")"
join -t $'\t' <(sort base.tsv) <(sort "$1.tsv") | awk -F'\t' '$2!=$5 || $3!=$6 || $4!=$7' | grep -v cast-region-to-uint
echo "[$1] moved: $(join -t $'\t' <(sort base.tsv) <(sort "$1.tsv") | awk -F'\t' '$2!=$5 || $3!=$6 || $4!=$7' | grep -vc cast-region-to-uint) of $(wc -l < base.tsv) (base rows) / $(wc -l < "$1.tsv") (armed rows)"
