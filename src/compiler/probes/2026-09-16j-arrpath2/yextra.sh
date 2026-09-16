#!/usr/bin/env bash
# y11-y13 — the shapes that SEPARATE my T2 form (per-index on the TEMP) from the 16i pricing's
# recommended form (per-index on the SOURCE). The pricing's own x01/x03 cannot tell them apart.
# usage: yextra.sh <outdir> <logosc> <libdir>
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
struct S { arr: [D; 2], tag: i64 }
fn mk(p: *mut i64) -> [D; 2] { return [D { v: 1i64, c: p }, D { v: 2i64, c: p }]; }'
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
#[allow(dead_code)] struct S { arr: [D; 2], tag: i64 }
fn mk(p: *mut i64) -> [D; 2] { [D { v: 1, c: p }, D { v: 2, c: p }] }'
POST_R='fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }'

emit() { local id="$1"
  printf 'package hb_%s;\n%s\n%s\n%s\n' "$id" "$PRE_L" "$2" "$POST_L" > "$OUT/logos/$id.logos"
  printf '%s\n%s\n%s\n' "$PRE_R" "$3" "$POST_R" > "$OUT/rust/$id.rs"; }

# y11 — SOURCE IS A CALL RESULT: a temporary with no owner the source-mark form can name.
emit y11 \
'fn g(p: *mut i64) -> i64 {
    let [_, y] = mk(p);
    return y.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let [_, y] = mk(p);
    return y.v;
}'

# y12 — SOURCE IS A FIELD OF A LOCAL THAT WAS ITSELF MOVED IN.
emit y12 \
'fn g(p: *mut i64) -> i64 {
    let s: S = S { arr: [D { v: 1i64, c: p }, D { v: 2i64, c: p }], tag: 7i64 };
    let s2: S = s;
    let [_, y] = s2.arr;
    return y.v;
}' \
'fn g(p: *mut i64) -> i64 {
    let s: S = S { arr: [D { v: 1, c: p }, D { v: 2, c: p }], tag: 7 };
    let s2: S = s;
    let [_, y] = s2.arr;
    return y.v;
}'

# y13 — LOCAL source, binding dropped INSIDE an inner scope so the two owners cannot cancel.
emit y13 \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1i64, c: p }, D { v: 2i64, c: p }];
    let r: i64 = 0i64;
    {
        let [_, y] = arr;
        unsafe { *p = *p * 10i64 + 9i64; }
        return y.v;
    }
}' \
'fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    #[allow(unused)] let r: i64 = 0;
    {
        let [_, y] = arr;
        unsafe { *p = *p * 10 + 9; }
        return y.v;
    }
}'

printf 'id\tlogos_cc\tlogos_rc\tlogos_out\trustc_cc\trust_rc\trust_out\n' > "$OUT/YEXTRA.tsv"
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
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$cc" "$lrc" "$lout" "$rcc" "$rrc" "$rout" >> "$OUT/YEXTRA.tsv"
done
touch "$OUT/YX_DONE"
