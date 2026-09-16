// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h10_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn pick(k: i64) -> i64 {
    let d1 = D { v: 6 };
    let d2 = D { v: 8 };
    let rd: &D = match k { 0 => &d1, _ => &d2 };
    inner(&rd)
}
fn run() -> i32 {
    if pick(0) != 6 { return 10; }
    if pick(1) != 8 { return 11; }
    0
}
fn main() { std::process::exit(run()); }
