// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p04_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r: &P = &p;
    let pp: &&P = &r;
    match pp { &&P { x } => { if x != 5 { return 4; } } }
    0
}
fn main() { std::process::exit(run()); }
