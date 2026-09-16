// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_p03_admit.logos
struct P { x: i32 }
fn deref2b(p: &&P) -> i32 { let x: &P = *p; x.x }
fn run() -> i32 {
    let p = P { x: 5 };
    let r = &p;
    if deref2b(&r) != 5 { return 3; }
    0
}
fn main() { std::process::exit(run()); }
