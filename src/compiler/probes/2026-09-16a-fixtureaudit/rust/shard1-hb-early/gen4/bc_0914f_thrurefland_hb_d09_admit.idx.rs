// TWIN: index exprs cast to usize (Logos indexes by i64/u64, Rust by usize)
// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_d09_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: u64 index literal -> usize (Rust indices are usize)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program d09 — caught: verdict moved base -> landed — PROBES.md "ref-typed LOCAL root (`&r.a`, `r = &s2`), let / match-default / written walk CLOSED (queue row; d01 d06 d09 d12 d24 d29 d30 d31, t1-t3)"...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/d09_loop_cursor_array_legal.logos
struct P { a: i64, b: i64 }
fn __logos_main() -> i32 {
    let arr: [P; 3] = [P { a: 1i64, b: 0i64 }, P { a: 2i64, b: 0i64 }, P { a: 3i64, b: 0i64 }];
    let mut r: &P = &arr[0usize];
    let mut sum: i64 = 0i64;
    let mut i: u64 = 1u64;
    while i < 3u64 {
        let a: &i64 = &r.a;
        r = &arr[i as usize];
        sum = sum + *a;
        i = i + 1u64;
    }
    return (sum + r.a) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

