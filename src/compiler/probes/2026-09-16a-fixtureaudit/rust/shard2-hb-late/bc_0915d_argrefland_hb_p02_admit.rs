// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p02_admit.logos
struct P { x: i32 }
fn deref2(p: &&P) -> i32 { match p { &&P { x } => x } }
fn run() -> i32 {
    let p = P { x: 5 };
    if deref2(&&p) != 5 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
