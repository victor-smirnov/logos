// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q12_admit.logos
fn run() -> i32 {
    let t: (i32, i32) = (3, 4);
    let r: &(i32, i32) = &t;
    let pp: &&(i32, i32) = &r;
    match pp { &q => { if q.1 != 4 { return 12; } } }
    0
}
fn main() { std::process::exit(run()); }
