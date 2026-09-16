// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h12_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let flag: bool = true;
    let pr: &&D = if flag { &rd } else { &rd };
    let x: &D = *pr;
    if x.v != 6 { return 12; }
    0
}
fn main() { std::process::exit(run()); }
