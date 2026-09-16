// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a15_admit.logos
struct D { v: i64 }
fn inner(a: &&mut D) -> i64 { a.v }
fn run() -> i32 {
    let mut d = D { v: 6 };
    let rd: &mut D = &mut d;
    if inner(&rd) != 6 { return 15; }
    0
}
fn main() { std::process::exit(run()); }
