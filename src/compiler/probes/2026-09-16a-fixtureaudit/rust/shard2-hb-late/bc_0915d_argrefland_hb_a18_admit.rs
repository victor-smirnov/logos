// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a18_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let f = |a: &&D| -> i64 { let x: &D = *a; x.v };
    if f(&rd) != 6 { return 18; }
    0
}
fn main() { std::process::exit(run()); }
