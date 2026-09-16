// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p01_admit.logos
struct P { x: i32 }
fn deref2(p: &&P) -> i32 { match p { &&P { x } => x } }
fn run() -> i32 {
    let p = P { x: 5 };
    let r = &p;
    let pp: &&P = &r;
    if deref2(pp) != 5 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
