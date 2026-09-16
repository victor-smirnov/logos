// TWIN of tests/logos/pass/bc_0915c_argref_hb_b02_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let mut d = D { v: 6 };
    let rd: &mut D = &mut d;
    let p: &&mut D = &rd;
    if p.v != 6 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
