// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h07_admit.logos
fn inner(a: &&(i64, i64)) -> i64 { let x: &(i64, i64) = *a; x.1 }
fn run() -> i32 {
    let arr: [(i64, i64); 2] = [(1, 2), (3, 4)];
    let s: &[(i64, i64)] = &arr;
    let mut sum: i64 = 0;
    for x in s { sum = sum + inner(&x); }
    if sum != 6 { return 7; }
    0
}
fn main() { std::process::exit(run()); }
