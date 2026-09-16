// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p09_admit.logos
struct P { x: i32 }
fn deref1(p: &&P) -> i32 { match p { &q => q.x } }
fn run() -> i32 {
    let p = P { x: 5 };
    let r = &p;
    let pp: &&P = &r;
    if deref1(pp) != 5 { return 9; }
    0
}
fn main() { std::process::exit(run()); }
