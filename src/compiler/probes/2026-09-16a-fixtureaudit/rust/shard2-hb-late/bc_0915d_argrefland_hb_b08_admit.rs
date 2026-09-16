// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b08_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let f = |rd: &D| -> i64 { inner(&rd) };
    if f(&d) != 6 { return 8; }
    0
}
fn main() { std::process::exit(run()); }
