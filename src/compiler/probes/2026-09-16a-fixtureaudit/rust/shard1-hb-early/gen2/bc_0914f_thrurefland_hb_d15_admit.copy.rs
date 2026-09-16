// TWIN of tests/logos/pass/bc_0914f_thrurefland_hb_d15_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: A16 VARIANT: #[derive(Clone, Copy)] added to 1 non-Drop struct(s)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14f-thrurefland, program d15 — caught: verdict moved base -> landed, predicted wrong — PROBES.md "I read d15 / o11 / o12 / v1-v7 as ILLEGAL programs opened by D2. They are not illegal in Logos ... auto-Copy under blessed divergence A1...
// legality: legal in Logos under blessed divergence A16 (auto-Copy struct), by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/land14f_d2.978t/d15_reassign_then_move_old_owner_illegal.logos
#[derive(Clone, Copy)]
struct P { a: i64, b: i64 }
fn eat(p: P) -> i64 { return p.a; }
fn __logos_main() -> i32 {
    let s1: P = P { a: 1i64, b: 2i64 };
    let s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &P = &s1;
    let a: &i64 = &r.a;
    r = &s2;
    let m: i64 = eat(s1);
    return (*a + m + r.a) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

