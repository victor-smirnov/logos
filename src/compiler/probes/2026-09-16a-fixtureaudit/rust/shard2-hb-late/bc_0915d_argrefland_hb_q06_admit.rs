// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q06_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r: &P = &p;
    let pp: &&P = &r;
    match pp { &&P { x: ref q } => { if *q != 5 { return 6; } } }
    0
}
fn main() { std::process::exit(run()); }
