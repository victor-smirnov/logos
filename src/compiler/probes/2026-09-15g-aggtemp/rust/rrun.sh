#!/usr/bin/env bash
# rrun.sh RS OUTDIR — rustc 1.98.1 --edition 2024 build + run; prints "<name> rustc=<rc> run=<rc> out=<stdout> codes=<E-codes>"
RUSTC=/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc
rs="$1"; o="$2"; n=$(basename "$rs" .rs); d="$o/$n"; mkdir -p "$d"
rc=0; "$RUSTC" --edition 2024 -O -o "$d/bin" "$rs" 2>"$d/err" || rc=$?
codes=$(grep -oE 'error\[E[0-9]+\]' "$d/err" | sort -u | tr '\n' ' ')
if [ $rc -ne 0 ]; then echo "$n rustc=$rc run=- codes=$codes"; exit 0; fi
r=0; timeout 20 "$d/bin" >"$d/out" 2>/dev/null || r=$?
echo "$n rustc=0 run=$r out=$(tr '\n' ' ' < "$d/out")"
