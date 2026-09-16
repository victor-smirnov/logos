// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h18_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let arr: [D; 2] = [D { v: 6 }, D { v: 7 }];
    let s: &[D] = &arr;
    let mut sum: i64 = 0;
    for x in s {
        let f = || -> i64 { inner(&x) };
        sum = sum + f();
    }
    if sum != 13 { return 18; }
    0
}
fn main() { std::process::exit(run()); }
