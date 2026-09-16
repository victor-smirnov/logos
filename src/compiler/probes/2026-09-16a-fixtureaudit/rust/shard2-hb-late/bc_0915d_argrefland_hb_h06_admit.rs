// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h06_admit.logos
fn inner(a: &&(i64, i64)) -> i64 { let x: &(i64, i64) = *a; x.0 + x.1 }
fn run() -> i32 {
    let t: (i64, i64) = (3, 4);
    let f = |rt: &(i64, i64)| -> i64 { inner(&rt) };
    if f(&t) != 7 { return 6; }
    0
}
fn main() { std::process::exit(run()); }
