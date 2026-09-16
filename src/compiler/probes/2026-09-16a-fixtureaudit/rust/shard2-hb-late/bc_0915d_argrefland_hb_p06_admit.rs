// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p06_admit.logos
struct P { x: i32 }
fn deref2(p: &&P) -> i32 { if let &&P { x } = p { return x; } 0 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r = &p;
    let pp: &&P = &r;
    if deref2(pp) != 5 { return 6; }
    0
}
fn main() { std::process::exit(run()); }
