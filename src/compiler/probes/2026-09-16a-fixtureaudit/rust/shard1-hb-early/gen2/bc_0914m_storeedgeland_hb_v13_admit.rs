// TWIN of tests/logos/pass/bc_0914m_storeedgeland_hb_v13_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14m-storeedge-land, program v13 — caught: legal, refused on base (`y = 20` after a read, the Vec dead), compiles and runs under the landing
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let mut x: i64 = 3i64;
    let mut y: i64 = 4i64;
    let mut buffer: Vec<&i64> = Vec::new();
    buffer.push(&x);
    buffer.push(&y);
    let s: i64 = *buffer[0u64] + *buffer[1u64];
    x = 10i64;
    y = 20i64;
    return (s + x + y) as i32 - 37i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

