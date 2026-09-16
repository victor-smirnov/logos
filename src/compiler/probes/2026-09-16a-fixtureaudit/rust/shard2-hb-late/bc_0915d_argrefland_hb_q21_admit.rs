// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q21_admit.logos
fn run() -> i32 {
    let v: i64 = 7;
    let y: i64 = match &v { &n => n + 1 };
    if y != 8 { return 21; }
    let rv: &i64 = &v;
    let z: i64 = match &rv { &&n => n + 2 };
    if z != 9 { return 22; }
    0
}
fn main() { std::process::exit(run()); }
