#!/usr/bin/env bash
# Hand battery, round 2026-09-16i-arrpath. THE PLAINEST SPELLINGS FIRST, both doors.
# Each program prints "k=<returned> n=<destructor trace>"; the trace is n = n*10 + v,
# so a leak reads SHORT and a double destructor reads LONG. rc alone cannot tell them apart.
# usage: battery.sh <outdir> <logosc> <libdir>
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
struct S { arr: [D; 2], tag: i64 }'
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
#[allow(dead_code)] struct S { arr: [D; 2], tag: i64 }'
POST_R='fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }'

emit() { # emit <id> <logos-body-g> <rust-body-g>
  local id="$1"
  printf 'package hb_%s;\n%s\n%s\n%s\n' "$id" "$PRE_L" "$2" "$POST_L" > "$OUT/logos/$id.logos"
  printf '%s\n%s\n%s\n' "$PRE_R" "$3" "$POST_R" > "$OUT/rust/$id.rs"
}

# ── LET DOOR ────────────────────────────────────────────────────────────────
emit x01 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }];
    let [_, y] = arr;
    return y.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    let [_, y] = arr;
    return y.v;
}'

emit x02 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }];
    let [a, b] = arr;
    return a.v + b.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    let [a, b] = arr;
    return a.v + b.v;
}'

emit x03 \
'fn g(p: *mut i64) -> i64 {
    let s: S = S { arr: [D { v: 1i64, c: p }, D { v: 2i64, c: p }], tag: 7i64 };
    let [_, y] = s.arr;
    return y.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let s: S = S { arr: [D { v: 1, c: p }, D { v: 2, c: p }], tag: 7 };
    let [_, y] = s.arr;
    return y.v;
}'

emit x04 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }];
    let [W { a: x, b: _ }] = arr;
    return x.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    let [W { a: x, b: _ }] = arr;
    return x.v;
}'

emit x05 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 3] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }, D { v: 3i64, c: p }];
    let [_, y, _] = arr;
    return y.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 3] = [D { v: 1, c: p }, D { v: 2, c: p }, D { v: 3, c: p }];
    let [_, y, _] = arr;
    return y.v;
}'

# ── MATCH DOOR, NESTED SUB-PATTERNS ─────────────────────────────────────────
emit m01 \
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

emit m02 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }];
    match arr {
        [W { a: x, b: z }] => { return x.v + z.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    match arr { [W { a: x, b: z }] => { return x.v + z.v; } }
}'

emit m03 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } },
                       W { a: D { v: 3i64, c: p }, b: D { v: 4i64, c: p } }];
    match arr {
        [_, W { a: x, b: _ }] => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 2] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } },
                       W { a: D { v: 3, c: p }, b: D { v: 4, c: p } }];
    match arr { [_, W { a: x, b: _ }] => { return x.v; } }
}'

emit m04 \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }];
    match arr {
        [w] => { return w.a.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    match arr { [w] => { return w.a.v; } }
}'

emit m05 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }];
    match arr {
        [_, y] => { return y.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    match arr { [_, y] => { return y.v; } }
}'

# TUPLE element with a nested struct sub — the tuple loop had skip paths all along.
emit t01 \
'fn g(p: *mut i64) -> i64 {
    let tp: (W, i64) = (W { a: D { v: 1i64, c: p }, b: D { v: 2i64, c: p } }, 9i64);
    match tp {
        (W { a: x, b: _ }, _) => { return x.v; }
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let tp: (W, i64) = (W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }, 9);
    match tp { (W { a: x, b: _ }, _) => { return x.v; } }
}'

# ── RUN BOTH ────────────────────────────────────────────────────────────────
printf 'id\tlogos_cc\tlogos_rc\tlogos_out\trustc_cc\trust_rc\trust_out\n' > "$OUT/BATTERY.tsv"
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
  rcc=0; "$RUSTC" --edition 2024 -O "$OUT/rust/$id.rs" --out-dir "$w" >"$w/rs.out" 2>"$w/rs.err" || rcc=$?
  rrc="-"; rout=""
  if [ "$rcc" = "0" ]; then rrc=0; timeout 60 "$w/$id" >"$w/rs.stdout" 2>/dev/null || rrc=$?; rout=$(tr -d '\n' < "$w/rs.stdout"); fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$cc" "$lrc" "$lout" "$rcc" "$rrc" "$rout" >> "$OUT/BATTERY.tsv"
done
touch "$OUT/BATTERY_DONE"
