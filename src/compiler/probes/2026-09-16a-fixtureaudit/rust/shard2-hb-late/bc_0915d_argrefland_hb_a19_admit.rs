// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a19_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let arr: [D; 2] = [D { v: 6 }, D { v: 7 }];
    let s: &[D] = &arr;
    let mut sum: i64 = 0;
    for x in s { sum = sum + inner(&x); }
    if sum != 13 { return 19; }
    0
}
fn main() { std::process::exit(run()); }
