#!/bin/bash
f="$1"; n=$(basename "$f" .rs); RC="$2"
RUSTC=/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc
$RUSTC --edition 2024 --crate-type lib --emit=metadata -o "$RC/meta/$n.rmeta" "$f" > "$RC/err/$n.txt" 2>&1
echo "$? $n" >> "$RC/rc.txt"
