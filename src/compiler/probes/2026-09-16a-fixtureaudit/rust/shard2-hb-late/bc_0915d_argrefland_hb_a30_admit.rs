// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a30_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let f = || -> i64 { inner(&rd) };
    if f() != 6 { return 31; }
    0
}
fn main() { std::process::exit(run()); }
