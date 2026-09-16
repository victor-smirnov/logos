// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h03_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let (a, b) = (&d, 1i64);
    if inner(&a) + b != 7 { return 3; }
    0
}
fn main() { std::process::exit(run()); }
