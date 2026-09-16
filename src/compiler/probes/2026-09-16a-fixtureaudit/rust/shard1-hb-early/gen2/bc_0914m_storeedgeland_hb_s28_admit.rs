// TWIN of tests/logos/pass/bc_0914m_storeedgeland_hb_s28_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14m-storeedge-land, program s28 — caught: legal, refused on base (two block-scoped Vec holders of `&x` re-homed onto `x`, then `x = 4`), compiles and runs under the landing
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let mut x: i64 = 5i64;
    let mut total: i64 = 0i64;
    {
        let mut v: Vec<&i64> = Vec::new();
        let mut w: Vec<&i64> = Vec::new();
        v.push(&x);
        w.push(&x);
        v.push(&x);
        total = v.len() as i64 + w.len() as i64;
    }
    x = 4i64;
    return (total as i32) + (x as i32) - 7i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

