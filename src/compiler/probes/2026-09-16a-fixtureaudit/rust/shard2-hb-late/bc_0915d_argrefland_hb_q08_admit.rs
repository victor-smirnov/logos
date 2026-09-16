// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q08_admit.logos
struct P { x: i32 }
struct Q { k: i32, p: P }
fn run() -> i32 {
    let q = Q { k: 2, p: P { x: 5 } };
    let r: &Q = &q;
    let pp: &&Q = &r;
    match pp { &&Q { k, p: P { x } } => { if k * 10 + x != 25 { return 8; } } }
    0
}
fn main() { std::process::exit(run()); }
