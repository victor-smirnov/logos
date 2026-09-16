// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b11_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let e = D { v: 9 };
    let rd: &D = &d;
    let s1: i64 = inner(&rd);
    let rd: &D = &e;
    let s2: i64 = inner(&rd);
    if s1 != 6 || s2 != 9 { return 11; }
    0
}
fn main() { std::process::exit(run()); }
