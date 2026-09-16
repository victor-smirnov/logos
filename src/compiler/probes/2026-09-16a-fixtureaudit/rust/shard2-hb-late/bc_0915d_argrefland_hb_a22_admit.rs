// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a22_admit.logos
struct D { v: i64, w: i64 }
fn inner(a: &&D) -> i64 { a.w }
fn run() -> i32 {
    let d = D { v: 6, w: 7 };
    let rd: &D = &d;
    if inner(&rd) != 7 { return 23; }
    0
}
fn main() { std::process::exit(run()); }
