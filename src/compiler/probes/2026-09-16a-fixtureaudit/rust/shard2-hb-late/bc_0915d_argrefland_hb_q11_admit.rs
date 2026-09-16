// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q11_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let p = P { x: 5 };
    let r: &P = &p;
    let rr: &&P = &r;
    let ppp: &&&P = &rr;
    match ppp { &&&P { x } => { if x != 5 { return 11; } } }
    0
}
fn main() { std::process::exit(run()); }
