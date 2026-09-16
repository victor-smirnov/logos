// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q15_admit.logos
struct T(i32, i32);
fn run() -> i32 {
    let t = T(3, 4);
    let r: &T = &t;
    let pp: &&T = &r;
    match pp { &&T(a, b) => { if a * 10 + b != 34 { return 15; } } }
    0
}
fn main() { std::process::exit(run()); }
