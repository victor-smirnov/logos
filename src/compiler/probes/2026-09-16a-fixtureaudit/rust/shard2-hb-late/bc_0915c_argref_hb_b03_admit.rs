// TWIN of tests/logos/pass/bc_0915c_argref_hb_b03_admit.logos
struct D { v: i64 }
fn inner(a: &&&D) -> i64 { let x: &&D = *a; let y: &D = *x; y.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let p: &&D = &rd;
    if inner(&p) != 6 { return 3; }
    0
}
fn main() { std::process::exit(run()); }
