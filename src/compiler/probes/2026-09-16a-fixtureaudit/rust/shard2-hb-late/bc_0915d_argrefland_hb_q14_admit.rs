// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q14_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r: &P = &p;
    let pp: &&P = &r;
    let v: i32 = match pp { P { x } => *x };
    if v != 5 { return 14; }
    0
}
fn main() { std::process::exit(run()); }
