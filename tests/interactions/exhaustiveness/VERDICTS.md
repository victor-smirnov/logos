# Exhaustiveness / refutability oracle battery — verdicts

For ADR 0030 step S3 (one usefulness matrix). Each probe `NAME` has `NAME.logos` and its rustc twin `NAME.rs` sharing one body (`fn run() -> i32`); `main` returns / `exit`s it. Non-exhaustive probes feed the scrutinee the MISSING value, so a permissive acceptance shows what the fall-through does at run time.

- rustc 1.98.1 (48a229cea 2026-09-01), `--edition 2024`; verdict = first error code, or `ok run=<exit>`; rustc lint warnings recorded (`warn:`).
- logosc `0.49.0-preview+main-gfd1e8a37-dirty.a057b02d851c` (binary built from fd1e8a37, not the checkout HEAD 980ac6f65); verdict = first error line, or `compiled run=<exit>` via the soundness-queue link recipe (139 = SIGSEGV).
- Measured 2026-09-27.

| NAME | shape | rustc | logosc | verdict |
|---|---|---|---|---|
| bool_exh | match bool {true,false} | ok run=20 | compiled run=20 | AGREE |
| bool_nonexh | match bool {true} (false missing, value=false) | refused E0004 | refused: bool_nonexh.logos:3:53: error [fn run]: match on bool is not exhaustive — missing false | AGREE |
| enum_plain_exh | enum {R,G,B} all arms | ok run=3 | compiled run=3 | AGREE |
| enum_plain_nonexh | enum {R,G,B} missing B (value=B) | refused E0004 | refused: enum_plain_nonexh.logos:5:50: error [fn run]: match is not exhaustive — missing variant(s): B | AGREE |
| enum_plain_stmt_nonexh | enum stmt-form match missing B (value=B), fallthrough return 99 | refused E0004 | refused: enum_plain_stmt_nonexh.logos:5:90: error [fn run]: match is not exhaustive — missing variant(s): B | AGREE |
| enum_payload_exh | Shape{Circle(r),Rect(w,h),Empty} | ok run=12 | compiled run=12 | AGREE |
| enum_payload_nonexh | Shape missing Rect (value=Rect) | refused E0004 | refused: enum_payload_nonexh.logos:5:65: error [fn run]: match is not exhaustive — missing variant(s): Rect | AGREE |
| enum_payload_literal_nonexh | Circle(0) only, Circle(_) missing (value=Circle(5)) | refused E0004 | refused: enum_payload_literal_nonexh.logos:5:61: error [fn run]: match is not exhaustive — missing variant(s): Circle | AGREE |
| enum_payload_literal_exh | Circle(0), Circle(_), Rect(..), Empty | ok run=7 | compiled run=7 | AGREE |
| opt_opt_exh | Option<Option<i64>> {Some(Some),Some(None),None} | ok run=42 | compiled run=42 | AGREE |
| opt_opt_nonexh | Option<Option<i64>> missing Some(None) (value=Some(None)) | refused E0004 | refused: opt_opt_nonexh.logos:3:89: error [fn run]: match is not exhaustive — missing variant(s): Some | AGREE |
| res_opt_exh | Result<Option<i64>,i64> {Ok(Some),Ok(None),Err} | ok run=42 | compiled run=42 | AGREE |
| res_opt_nonexh | Result<Option<i64>,i64> missing Ok(None) (value=Ok(None)) | refused E0004 | refused: res_opt_nonexh.logos:3:92: error [fn run]: match is not exhaustive — missing variant(s): Ok | AGREE |
| nested_struct_in_enum_nonexh | enum E{A(S{b:bool})} only A(S{b:true}) (value=false) | refused E0004 | refused: nested_struct_in_enum_nonexh.logos:5:71: error [fn run]: match is not exhaustive — missing variant(s): A | AGREE |
| tuple_bool_exh | (bool,bool) {(t,t),(t,f),(f,_)} | ok run=3 | compiled run=3 | AGREE |
| tuple_bool_nonexh | (bool,bool) {(t,t),(f,f)} (value=(f,t)) | refused E0004 | refused: tuple_bool_nonexh.logos:3:55: error [fn run]: match is not exhaustive (E0004): the arms do not cover every value of `(bool, bool)` | AGREE |
| tuple_enum_bool_exh | (Color,bool) all cells | ok run=3 | compiled run=3 | AGREE |
| tuple_enum_bool_nonexh | (Color,bool) missing (G,false) (value=(G,false)) | refused E0004 | refused: tuple_enum_bool_nonexh.logos:5:59: error [fn run]: match is not exhaustive (E0004): the arms do not cover every value of `(Color, bool)` | AGREE |
| tuple_i64_nonexh | (i64,i64) {(0,_),(_,0)} (value=(1,1)) | refused E0004 | compiled run=252 | DISAGREE — permissive |
| struct_fields_exh | S{c:Color,b:bool} covered by 3 arms | ok run=3 | refused: error [struct_fields_exh.logos]: syntax error near 'fn' at line 6 col 1 | DISAGREE — over-refusal |
| struct_fields_nonexh | S{c,b} missing S{c:G\|B,b:false} (value=G,false) | refused E0004 | refused: struct_fields_nonexh.logos:6:69: error [fn run]: match is not exhaustive (E0004): the arms do not cover every value of `S` | AGREE |
| tuple_struct_exh | P(bool,bool) {P(t,_),P(f,t),P(f,f)} | ok run=3 | compiled run=3 | AGREE |
| tuple_struct_nonexh | P(bool,bool) missing P(f,f) (value=P(f,f)) | refused E0004 | refused: tuple_struct_nonexh.logos:4:57: error [fn run]: match is not exhaustive (E0004): the arms do not cover every value of `P` | AGREE |
| ref_enum_exh | match &Color default binding mode, all arms | ok run=2 | compiled run=2 | AGREE |
| ref_enum_nonexh | match &Color missing B (value=B) | refused E0004 | compiled run=252 | DISAGREE — permissive |
| ref_opt_exh | match &Option<i64> {Some(x)=>*x, None} | ok run=7 | compiled run=7 | AGREE |
| ref_opt_nonexh | match &Option<i64> {Some(x)} (value=None) | refused E0004 | compiled run=240 | DISAGREE — permissive |
| refref_exh | match &&Color all arms | ok run=3 | compiled run=3 | AGREE |
| refref_nonexh | match &&Color missing B (value=B) | refused E0004 | compiled run=255 | DISAGREE — permissive |
| explicit_ref_pat_exh | match &Option<i64> {&Some(x), &None} | ok run=7 | compiled run=7 | AGREE |
| explicit_ref_pat_nonexh | match &Option<i64> {&Some(x)} (value=None) | refused E0004 | compiled run=240 | DISAGREE — permissive |
| u8_full_range_exh | u8 {0..=127, 128..=255} | ok run=2 | compiled run=2 | AGREE |
| u8_excl_range_exh | u8 {0..128, 128..=255} (exclusive range) | ok run=2 | compiled run=2 | AGREE |
| u8_gap_nonexh | u8 {0..=100, 102..=255} gap at 101 (value=101) | refused E0004 | compiled run=255 | DISAGREE — permissive |
| u8_top_nonexh | u8 {0..=254} (255 missing, value=255) | refused E0004 | compiled run=252 | DISAGREE — permissive |
| i8_full_exh | i8 {MIN..=-1, 0..=MAX} | ok run=1 | refused: error [i8_full_exh.logos]: syntax error near 'fn' at line 3 col 1 | DISAGREE — over-refusal |
| i8_or_full_exh | i8 {MIN..=-1 \| 0..=MAX} one or-arm | ok run=3 | refused: error [i8_or_full_exh.logos]: syntax error near 'fn' at line 3 col 1 | DISAGREE — over-refusal |
| i8_gap_nonexh | i8 {MIN..=-2, 0..=MAX} (-1 missing, value=-1) | refused E0004 | refused: error [i8_gap_nonexh.logos]: syntax error near 'fn' at line 3 col 1 | AGREE (wrong reason: parse error) |
| i64_no_wild_nonexh | i64 {0, 1..=100} no wildcard (value=500) | refused E0004 | compiled run=254 | DISAGREE — permissive |
| i64_full_range_exh | i64 {MIN..=0, 1..=MAX} | ok run=2 | refused: error [i64_full_range_exh.logos]: syntax error near 'fn' at line 3 col 1 | DISAGREE — over-refusal |
| i64_binding_catchall_exh | i64 {0, k} binding catch-all | ok run=10 | compiled run=10 | AGREE |
| char_range_nonexh | char {'a'..='z','A'..='Z'} (value='0') | refused E0004 | compiled run=254 | DISAGREE — permissive |
| char_full_exh | char {'\0'..='\u{D7FF}', '\u{E000}'..='\u{10FFFF}'} | ok run=1 | compiled run=1 | AGREE |
| bool_int_tuple_exh | (bool,i64) {(t,_),(f,0),(f,_)} | ok run=3 | compiled run=3 | AGREE |
| bool_int_tuple_nonexh | (bool,i64) {(t,_),(f,0)} (value=(f,3)) | refused E0004 | compiled run=255 | DISAGREE — permissive |
| str_nonexh | &str {"a","b"} no wildcard (value="c") | refused E0004 | compiled run=0 | DISAGREE — permissive |
| str_exh | &str {"a","b",_} | ok run=3 | compiled run=3 | AGREE |
| string_as_str_nonexh | String.as_str() {"a"} no wildcard (value="z") | refused E0004 | compiled run=0 | DISAGREE — permissive |
| string_as_str_exh | String.as_str() {"a", other} | ok run=5 | compiled run=5 | AGREE |
| slice_exh | &[i64] {[],[x],[x,y,..]} | ok run=13 | compiled run=13 | AGREE |
| slice_nonexh | &[i64] {[],[x]} longer missing (value len 3) | refused E0004 | compiled run=139 | DISAGREE — permissive |
| slice_head_exh | &[i64] {[], [first, ..]} | ok run=4 | compiled run=4 | AGREE |
| slice_tail_exh | &[i64] {[.., last], []} | ok run=3 | compiled run=3 | AGREE |
| slice_head_nonexh | &[i64] {[first, ..]} empty missing (value []) | refused E0004 | compiled run=139 | DISAGREE — permissive |
| slice_rest_exh | &[i64] {[a,b,rest@..],[a],[]} | ok run=5 | compiled run=5 | AGREE |
| slice_rest_nonexh | &[i64] {[a,b,rest@..],[]} len-1 missing (value len 1) | refused E0004 | compiled run=139 | DISAGREE — permissive |
| slice_literal_nonexh | &[i64] {[1,..],[]} non-1 head missing (value [5]) | refused E0004 | compiled run=139 | DISAGREE — permissive |
| slice_prefix_suffix_exh | &[i64] {[x,..,y],[x],[]} | ok run=43 | compiled run=43 | AGREE |
| array2_bool_exh | [bool;2] {[t,_],[f,t],[f,f]} | ok run=3 | compiled run=3 | AGREE |
| array2_bool_nonexh | [bool;2] missing [f,t] (value=[f,t]) | refused E0004 | compiled run=255 | DISAGREE — permissive |
| array2_i64_nonexh | [i64;2] {[0,_],[_,0]} (value [1,1]) | refused E0004 | compiled run=253 | DISAGREE — permissive |
| array_rest_exh | [i64;3] {[x, ..]} irrefutable with rest | ok run=6 | compiled run=6 | AGREE |
| or_pattern_exh | Color {R\|G, B} | ok run=1 | compiled run=1 | AGREE |
| or_pattern_nonexh | Color {R\|G} (value=B) | refused E0004 | refused: or_pattern_nonexh.logos:5:50: error [fn run]: match is not exhaustive — missing variant(s): B | AGREE |
| or_nested_payload_exh | Option<Color> {Some(R\|G),Some(B),None} | ok run=2 | compiled run=2 | AGREE |
| or_nested_payload_nonexh | Option<Color> {Some(R\|G),None} (value=Some(B)) | refused E0004 | refused: or_nested_payload_nonexh.logos:5:79: error [fn run]: match is not exhaustive — missing variant(s): Some | AGREE |
| or_tuple_nested_exh | (bool,bool) {(true\|false, true), (_, false)} | ok run=2 | compiled run=2 | AGREE |
| at_binding_exh | Option<i64> {x @ Some(_), None} | ok run=4 | compiled run=4 | AGREE |
| at_binding_range_nonexh | u8 {n @ 0..=9, 10..=254} (255 missing, value=255) | refused E0004 | compiled run=144 | DISAGREE — permissive |
| at_binding_range_exh | u8 {n @ 0..=9, m @ 10..=255} | ok run=51 | compiled run=51 | AGREE |
| guard_nonexh | Option<i64> {Some(x) if x>0, None} (value=Some(-1)) | refused E0004 | refused: guard_nonexh.logos:3:74: error [fn run]: match is not exhaustive — missing variant(s): Some | AGREE |
| guard_later_unguarded_exh | Option<i64> {Some(x) if x>0, Some(_), None} | ok run=4 | compiled run=4 | AGREE |
| guard_bool_nonexh | bool {true if cond, false} (value=true, cond false) | refused E0004 | refused: guard_bool_nonexh.logos:3:71: error [fn run]: match on bool is not exhaustive — missing true | AGREE |
| guard_all_arms_nonexh | bool {true if t, false if t} every arm guarded | refused E0004 | refused: guard_all_arms_nonexh.logos:3:72: error [fn run]: match on bool is not exhaustive — missing true | AGREE |
| guard_wild_nonexh | i64 {_ if x>0} guarded wildcard only (value=-3) | refused E0004 | compiled run=254 | DISAGREE — permissive |
| let_refutable_opt | let Some(x) = opt; (E0005) | refused E0005 | refused: let_refutable_opt.logos:3:54: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | AGREE |
| let_refutable_enum | let Color::R = c; (E0005) | refused E0005 | refused: let_refutable_enum.logos:5:37: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | AGREE |
| let_refutable_literal | let 0i64 = x; (E0005) | refused E0005 | refused: let_refutable_literal.logos:3:38: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | AGREE |
| let_refutable_slice | let [a, b] = slice; (E0005) | refused E0005 | refused: let_refutable_slice.logos:3:77: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | AGREE |
| let_refutable_tuple_inner | let (0i64, y) = t; (E0005) | refused E0005 | refused: let_refutable_tuple_inner.logos:3:41: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | AGREE |
| let_irrefutable_struct | let S{a, b} = s; | ok run=34 | compiled run=34 | AGREE |
| let_irrefutable_tuple | let (a, (b, c)) = t; | ok run=123 | compiled run=123 | AGREE |
| let_irrefutable_single_variant | enum W{V(i64)}; let W::V(x) = w; | ok run=9 | refused: let_irrefutable_single_variant.logos:4:39: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | DISAGREE — over-refusal |
| let_irrefutable_u8_range | let 0..=255u8 = x; (full-domain range, irrefutable) | ok run=3 | refused: let_irrefutable_u8_range.logos:3:36: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | DISAGREE — over-refusal |
| let_irrefutable_array | let [a, b, c] = [i64;3]; | ok run=6 | compiled run=6 | AGREE |
| let_else_ok | let Some(x) = o else { return 1 }; (value=None) | ok run=11 | compiled run=11 | AGREE |
| let_else_ok_taken | let Some(x) = o else {..}; (value=Some(7)) | ok run=7 | compiled run=7 | AGREE |
| let_else_irrefutable | let (a,b) = t else {..}; irrefutable (rustc warns) | ok run=5 (warn: unreachable `else` clause) | compiled run=5 | AGREE |
| let_else_nondiverging | let Some(x) = o else { 5i64 }; else must diverge (E0308) | refused E0308 | refused: let_else_nondiverging.logos:3:85: error [fn run]: return type mismatch — expected i32, got i64 | AGREE |
| unreachable_after_wild | Color {_, R} arm after wildcard (warning) | ok run=1 (warn: unreachable pattern) | refused: unreachable_after_wild.logos:5:50: error [fn run]: unreachable match arm: a previous '_' arm matches all values | DISAGREE — over-refusal |
| unreachable_dup_arm | Color {R, R, G, B} duplicate arm (warning) | ok run=1 (warn: unreachable pattern) | compiled run=1 | AGREE |
| unreachable_range_overlap | u8 {0..=10, 5..=8, _} subsumed range (warning) | ok run=1 (warn: unreachable pattern) | compiled run=1 | AGREE |
| unreachable_or_alt | Option<bool> {Some(true), Some(true\|false), None} partly-unreachable alt (warning) | ok run=2 (warn: unreachable pattern) | compiled run=2 | AGREE |
| empty_enum_match | fn f(e: Empty) { match e {} } | ok run=4 | compiled run=4 | AGREE |
| empty_enum_ref_match | fn f(e: &Empty) { match e {} } (rustc E0004) | refused E0004 | compiled run=4 | DISAGREE — permissive |
| result_empty_err_omitted | Result<i64,Empty> {Ok(v)} Err omitted (min_exhaustive_patterns) | ok run=6 | compiled run=6 | AGREE |
| let_ok_empty_err | let Ok(v) = r; r: Result<i64,Empty> (irrefutable) | ok run=8 | refused: let_ok_empty_err.logos:5:65: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match` | DISAGREE — over-refusal |
| empty_match_on_i64 | match x {} over i64 (E0004) | refused E0004 | compiled run=139 | DISAGREE — permissive |
| i8_full_lit_exh | i8 {-128..=-1, 0..=127} literal bounds | ok run=1 | compiled run=1 | AGREE |
| i8_or_full_lit_exh | i8 {-128..=-1 \| 0..=127} one or-arm, literal bounds | ok run=3 | compiled run=3 | AGREE |
| i8_gap_lit_nonexh | i8 {-128..=-2, 0..=127} (-1 missing, value=-1) | refused E0004 | compiled run=252 | DISAGREE — permissive |
| i64_full_lit_exh | i64 {-9223372036854775808..=0, 1..=9223372036854775807} literal bounds | ok run=2 | compiled run=2 | AGREE |
| struct_fields_split_exh | S{c:Color,b:bool} 4 arms, no or-pattern in field | ok run=4 | compiled run=4 | AGREE |
| or_in_struct_field_parse | struct pattern with or-pattern in a field S{c: R\|G, ..} | ok run=1 | refused: error [or_in_struct_field_parse.logos]: syntax error near 'fn' at line 6 col 1 | DISAGREE — over-refusal |
| u8_wild_after_full_range | u8 {0..=255, _} wildcard unreachable after full range (warning) | ok run=1 (warn: unreachable pattern) | compiled run=1 | AGREE |
| i64_literal_nonexh_stmt | i64 stmt-form {0,1} no wildcard, fallthrough return 99 (value=5) | refused E0004 | compiled run=99 | DISAGREE — permissive |

## Summary

- Probes: 106. rustc accepts 58, refuses 48.
- AGREE: 73 (of which 1 only because logosc fails to PARSE the probe). DISAGREE: 33 — permissive 24, over-refusal 9, wrong-runtime 0.

### Permissive (logosc accepts what rustc refuses)

- `tuple_i64_nonexh` — (i64,i64) {(0,_),(_,0)} (value=(1,1)): rustc `refused E0004`, logosc `compiled run=252`
- `ref_enum_nonexh` — match &Color missing B (value=B): rustc `refused E0004`, logosc `compiled run=252`
- `ref_opt_nonexh` — match &Option<i64> {Some(x)} (value=None): rustc `refused E0004`, logosc `compiled run=240`
- `refref_nonexh` — match &&Color missing B (value=B): rustc `refused E0004`, logosc `compiled run=255`
- `explicit_ref_pat_nonexh` — match &Option<i64> {&Some(x)} (value=None): rustc `refused E0004`, logosc `compiled run=240`
- `u8_gap_nonexh` — u8 {0..=100, 102..=255} gap at 101 (value=101): rustc `refused E0004`, logosc `compiled run=255`
- `u8_top_nonexh` — u8 {0..=254} (255 missing, value=255): rustc `refused E0004`, logosc `compiled run=252`
- `i64_no_wild_nonexh` — i64 {0, 1..=100} no wildcard (value=500): rustc `refused E0004`, logosc `compiled run=254`
- `char_range_nonexh` — char {'a'..='z','A'..='Z'} (value='0'): rustc `refused E0004`, logosc `compiled run=254`
- `bool_int_tuple_nonexh` — (bool,i64) {(t,_),(f,0)} (value=(f,3)): rustc `refused E0004`, logosc `compiled run=255`
- `str_nonexh` — &str {"a","b"} no wildcard (value="c"): rustc `refused E0004`, logosc `compiled run=0`
- `string_as_str_nonexh` — String.as_str() {"a"} no wildcard (value="z"): rustc `refused E0004`, logosc `compiled run=0`
- `slice_nonexh` — &[i64] {[],[x]} longer missing (value len 3): rustc `refused E0004`, logosc `compiled run=139`
- `slice_head_nonexh` — &[i64] {[first, ..]} empty missing (value []): rustc `refused E0004`, logosc `compiled run=139`
- `slice_rest_nonexh` — &[i64] {[a,b,rest@..],[]} len-1 missing (value len 1): rustc `refused E0004`, logosc `compiled run=139`
- `slice_literal_nonexh` — &[i64] {[1,..],[]} non-1 head missing (value [5]): rustc `refused E0004`, logosc `compiled run=139`
- `array2_bool_nonexh` — [bool;2] missing [f,t] (value=[f,t]): rustc `refused E0004`, logosc `compiled run=255`
- `array2_i64_nonexh` — [i64;2] {[0,_],[_,0]} (value [1,1]): rustc `refused E0004`, logosc `compiled run=253`
- `at_binding_range_nonexh` — u8 {n @ 0..=9, 10..=254} (255 missing, value=255): rustc `refused E0004`, logosc `compiled run=144`
- `guard_wild_nonexh` — i64 {_ if x>0} guarded wildcard only (value=-3): rustc `refused E0004`, logosc `compiled run=254`
- `empty_enum_ref_match` — fn f(e: &Empty) { match e {} } (rustc E0004): rustc `refused E0004`, logosc `compiled run=4`
- `empty_match_on_i64` — match x {} over i64 (E0004): rustc `refused E0004`, logosc `compiled run=139`
- `i8_gap_lit_nonexh` — i8 {-128..=-2, 0..=127} (-1 missing, value=-1): rustc `refused E0004`, logosc `compiled run=252`
- `i64_literal_nonexh_stmt` — i64 stmt-form {0,1} no wildcard, fallthrough return 99 (value=5): rustc `refused E0004`, logosc `compiled run=99`

### Over-refusal (logosc refuses what rustc accepts)

- `struct_fields_exh` — S{c:Color,b:bool} covered by 3 arms: rustc `ok run=3`, logosc `refused: error [struct_fields_exh.logos]: syntax error near 'fn' at line 6 col 1`
- `i8_full_exh` — i8 {MIN..=-1, 0..=MAX}: rustc `ok run=1`, logosc `refused: error [i8_full_exh.logos]: syntax error near 'fn' at line 3 col 1`
- `i8_or_full_exh` — i8 {MIN..=-1 \| 0..=MAX} one or-arm: rustc `ok run=3`, logosc `refused: error [i8_or_full_exh.logos]: syntax error near 'fn' at line 3 col 1`
- `i64_full_range_exh` — i64 {MIN..=0, 1..=MAX}: rustc `ok run=2`, logosc `refused: error [i64_full_range_exh.logos]: syntax error near 'fn' at line 3 col 1`
- `let_irrefutable_single_variant` — enum W{V(i64)}; let W::V(x) = w;: rustc `ok run=9`, logosc `refused: let_irrefutable_single_variant.logos:4:39: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match``
- `let_irrefutable_u8_range` — let 0..=255u8 = x; (full-domain range, irrefutable): rustc `ok run=3`, logosc `refused: let_irrefutable_u8_range.logos:3:36: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match``
- `unreachable_after_wild` — Color {_, R} arm after wildcard (warning): rustc `ok run=1 (warn: unreachable pattern)`, logosc `refused: unreachable_after_wild.logos:5:50: error [fn run]: unreachable match arm: a previous '_' arm matches all values`
- `let_ok_empty_err` — let Ok(v) = r; r: Result<i64,Empty> (irrefutable): rustc `ok run=8`, logosc `refused: let_ok_empty_err.logos:5:65: error [fn run]: refutable pattern in local binding: use `let … else { … }` or `match``
- `or_in_struct_field_parse` — struct pattern with or-pattern in a field S{c: R\|G, ..}: rustc `ok run=1`, logosc `refused: error [or_in_struct_field_parse.logos]: syntax error near 'fn' at line 6 col 1`

### Wrong-runtime (both accept, different exit code)


### Agree for the wrong reason (logosc parse error, not an exhaustiveness verdict)

- `i8_gap_nonexh` — i8 {MIN..=-2, 0..=MAX} (-1 missing, value=-1): rustc `refused E0004`, logosc `refused: error [i8_gap_nonexh.logos]: syntax error near 'fn' at line 3 col 1`

### Classes behind the DISAGREE list

Permissive (24) — the non-exhaustive match compiles and the missing value FALLS THROUGH: the match-expression yields an undefined value (exit codes 240–255, differing between two runs of the same battery: `refref_nonexh` 252 then 255), a slice match SEGFAULTs (139), a statement match runs the code after it (`i64_literal_nonexh_stmt` returns 99).
- P1 reference scrutinee — no check at all under `&` / `&&`, default binding mode or explicit `&` pattern: `ref_enum_nonexh`, `refref_nonexh`, `ref_opt_nonexh`, `explicit_ref_pat_nonexh` (the same shapes by value ARE refused).
- P2 integer / char columns — no range-coverage check (gaps, missing top value, no wildcard on i64): `u8_gap_nonexh`, `u8_top_nonexh`, `i8_gap_lit_nonexh`, `i64_no_wild_nonexh`, `i64_literal_nonexh_stmt`, `char_range_nonexh`, `at_binding_range_nonexh`; inside a tuple/array: `tuple_i64_nonexh`, `bool_int_tuple_nonexh`, `array2_i64_nonexh`.
- P3 string literals without wildcard: `str_nonexh`, `string_as_str_nonexh` (run=0).
- P4 slice patterns — no length/prefix coverage, and the fall-through crashes: `slice_nonexh`, `slice_head_nonexh`, `slice_rest_nonexh`, `slice_literal_nonexh` (all 139).
- P5 fixed arrays even over bool: `array2_bool_nonexh` (the same cells as a tuple `(bool,bool)` ARE refused).
- P6 guarded wildcard counted as covering: `guard_wild_nonexh` (guarded enum/bool arms are handled right).
- P7 empty match on an inhabited type: `empty_match_on_i64` (139), `empty_enum_ref_match` (`&Empty` is not empty for rustc).

Over-refusal (9).
- O1 parse, not exhaustiveness — associated-const path as a range bound (`i8::MIN..=`, `..=i8::MAX`, `i64::MIN`): `i8_full_exh`, `i8_or_full_exh`, `i64_full_range_exh` (and `i8_gap_nonexh` agrees only by this parse error); or-pattern inside a struct-pattern field: `struct_fields_exh`, `or_in_struct_field_parse`. Their literal-bound / split-arm twins (`i8_full_lit_exh`, `i8_or_full_lit_exh`, `i64_full_lit_exh`, `struct_fields_split_exh`) compile and agree.
- O2 refutability of `let` is decided by pattern KIND, not by coverage of the type: single-variant enum `let W::V(x) = w` (`let_irrefutable_single_variant`), full-domain range `let 0..=255u8 = x` (`let_irrefutable_u8_range`), `let Ok(v) = r` with an uninhabited `Err` (`let_ok_empty_err`; note the `match` form `result_empty_err_omitted` IS accepted).
- O3 unreachable arm is a hard error for a `_`-then-arm shape (`unreachable_after_wild`) where rustc only warns.

Wrong-runtime: none — every shape both compilers accept returns the same exit code.

Diagnostics, not verdicts: rustc warns `unreachable pattern` on `unreachable_dup_arm`, `unreachable_range_overlap`, `unreachable_or_alt`, `u8_wild_after_full_range`, and `unreachable else clause` on `let_else_irrefutable`; logosc emits no warning on any of them (it compiles them silently).
