// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q02_admit.logos
struct P { x: i32, y: i32 }
fn run() -> i32 {
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    let pp: &&P = &r;
    let s: i32 = match pp { &&P { x, y } => x * 10 + y };
    if s != 56 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
