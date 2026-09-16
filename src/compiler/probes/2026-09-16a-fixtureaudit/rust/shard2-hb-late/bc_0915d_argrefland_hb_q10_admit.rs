// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q10_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r: &P = &p;
    let pp: &&P = &r;
    let mut n: i32 = 0;
    while let &&P { x } = pp {
        n = n + x;
        if n > 9 { break; }
    }
    if n != 10 { return 10; }
    0
}
fn main() { std::process::exit(run()); }
