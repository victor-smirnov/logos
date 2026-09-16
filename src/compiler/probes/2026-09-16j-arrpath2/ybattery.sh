#!/usr/bin/env bash
# Round 2026-09-16j-arrpath2 — MY OWN counter-examples, in shapes the 16i pricing did NOT use.
# usage: ybattery.sh <outdir> <logosc> <libdir>
set -uo pipefail
OUT="$1"; LOGOSC="$2"; LIBDIR="$3"
RUSTC=/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc
mkdir -p "$OUT/logos" "$OUT/rust" "$OUT/work"
export LOGOS_VERIFY_LAYOUT=1
LINK=()
for a in "$LIBDIR"/liblstdlib*.a; do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/liblogos-*.a;  do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK+=("$a") ;; esac; done

PRE_L='extern fn printf(fmt: *const u8, ...) -> i32;
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
struct W { a: D, b: D }
struct V { a: D, n: i64 }
struct P { t: (D, D) }'
POST_L='fn main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let k: i64 = g(p);
    let got: i64 = unsafe { n };
    unsafe { printf("k=%ld n=%ld\n".as_ptr(), k, got); }
    return 0i32;
}'
PRE_R='struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] struct W { a: D, b: D }
#[allow(dead_code)] struct V { a: D, n: i64 }
#[allow(dead_code)] struct P { t: (D, D) }'
POST_R='fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }'

emit() { local id="$1"
  printf 'package hb_%s;\n%s\n%s\n%s\n' "$id" "$PRE_L" "$2" "$POST_L" > "$OUT/logos/$id.logos"
  printf '%s\n%s\n%s\n' "$PRE_R" "$3" "$POST_R" > "$OUT/rust/$id.rs"; }

# y01 — LET, NOTHING BOUND. Separates "the mark is wrong" from "the temp never drops at all".
emit y01 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }];
    let [_, _] = arr;
    return 9i64;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    let [_, _] = arr;
    return 9;
}'

# y02 — SUFFIX rest + nested sub. The suffix loop is a SEPARATE code path from the prefix loop.
emit y02 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } },
                       W { a: D { v: 3i64, c: p }, b: D { v: 4i64, c: p } }];
    match arr {
        [.., W { a: x, b: _ }] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } },
                       W { a: D { v: 3, c: p }, b: D { v: 4, c: p } }];
    match arr { [.., W { a: x, b: _ }] => { return x.v; } }
}'

# y03 — PREFIX nested sub + trailing rest.
emit y03 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } },
                       W { a: D { v: 3i64, c: p }, b: D { v: 4i64, c: p } }];
    match arr {
        [W { a: x, b: _ }, ..] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } },
                       W { a: D { v: 3, c: p }, b: D { v: 4, c: p } }];
    match arr { [W { a: x, b: _ }, ..] => { return x.v; } }
}'

# y04 — TUPLE sub inside an array element (path arr.0.0), not a struct sub.
emit y04 \
'fn g(p: *mut i64) -> i64 {
    let arr: [(D, D); 1] = [(D { v: 1i64, c: p }, D { v: 2i64, c: p })];
    match arr {
        [(x, _)] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [(D, D); 1] = [(D { v: 1, c: p }, D { v: 2, c: p })];
    match arr { [(x, _)] => { return x.v; } }
}'

# y05 — ARRAY INSIDE ARRAY: a nested Slice sub, path arr.0.0. A1 must recurse or it double-frees.
emit y05 \
'fn g(p: *mut i64) -> i64 {
    let arr: [[D; 2]; 1] = [[D { v: 1i64, c: p }, D { v: 2i64, c: p }]];
    match arr {
        [[x, _]] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [[D; 2]; 1] = [[D { v: 1, c: p }, D { v: 2, c: p }]];
    match arr { [[x, _]] => { return x.v; } }
}'

# y06 — REF BINDING MODE. `match &arr` moves NOTHING; A1 must emit no path at all.
emit y06 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }];
    let r: i64 = match &arr {
        [W { a: x, b: _ }] => x.v,
    };
    return r;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    let r: i64 = match &arr { [W { a: x, b: _ }] => x.v, };
    return r;
}'

# y07 — NON-DROP SIBLING: the struct sub has a plain i64 field beside the D.
emit y07 \
'fn g(p: *mut i64) -> i64 {
    let arr: [V; 1] = [V { a: D { v: 1i64, c: p }, n: 5i64 }];
    match arr {
        [V { a: x, n: _ }] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [V; 1] = [V { a: D { v: 1, c: p }, n: 5 }];
    match arr { [V { a: x, n: _ }] => { return x.v; } }
}'

# y08 — TWO ARMS, one nested one plain. The mark must be PER ARM, not unioned across arms.
emit y08 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }];
    match arr {
        [W { a: x, b: _ }] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    match arr { [W { a: x, b: _ }] => { return x.v; } }
}'

# y09 — LET, ALL WILD over 3. Every element must still be destroyed.
emit y09 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 3] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }, D { v: 3i64, c: p }];
    let [_, _, _] = arr;
    return 9i64;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 3] = [D { v: 1, c: p }, D { v: 2, c: p }, D { v: 3, c: p }];
    let [_, _, _] = arr;
    return 9;
}'

# y10 — STRUCT FIELD holding a TUPLE, matched inside an array element: path arr.0.t.0.
emit y10 \
'fn g(p: *mut i64) -> i64 {
    let arr: [P; 1] = [P { t: (D { v: 1i64, c: p }, D { v: 2i64, c: p }) }];
    match arr {
        [P { t: (x, _) }] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [P; 1] = [P { t: (D { v: 1, c: p }, D { v: 2, c: p }) }];
    match arr { [P { t: (x, _) }] => { return x.v; } }
}'

printf 'id\tlogos_cc\tlogos_rc\tlogos_out\trustc_cc\trust_rc\trust_out\n' > "$OUT/YBATTERY.tsv"
for f in "$OUT"/logos/*.logos; do
  id=$(basename "$f" .logos); w="$OUT/work/$id"; mkdir -p "$w"
  cc=0; "$LOGOSC" "$f" -o "$w/t.o" >"$w/cc.out" 2>"$w/cc.err" || cc=$?
  grep -q -E "error( \[|:)" "$w/cc.err" && cc="DIAG"
  lrc="-"; lout=""
  if [ "$cc" = "0" ]; then
    if cc "$w/t.o" -Wl,--start-group "${LINK[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$w/t" 2>"$w/ld.err"; then
      lrc=0; timeout 60 "$w/t" >"$w/stdout" 2>/dev/null || lrc=$?
      lout=$(tr -d '\n' < "$w/stdout")
    else lrc="LINKFAIL"; fi
  fi
  rcc=0; "$RUSTC" --edition 2024 -O --crate-name "$id" "$OUT/rust/$id.rs" --out-dir "$w" >"$w/rs.out" 2>"$w/rs.err" || rcc=$?
  rrc="-"; rout=""
  if [ "$rcc" = "0" ]; then rrc=0; timeout 60 "$w/$id" >"$w/rs.stdout" 2>/dev/null || rrc=$?; rout=$(tr -d '\n' < "$w/rs.stdout"); fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$cc" "$lrc" "$lout" "$rcc" "$rrc" "$rout" >> "$OUT/YBATTERY.tsv"
done
touch "$OUT/YB_DONE"
