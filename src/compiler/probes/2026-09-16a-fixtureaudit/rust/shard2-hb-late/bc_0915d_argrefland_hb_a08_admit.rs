// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a08_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let e = D { v: 9 };
    let rd: &D = &d;
    let re: &D = &e;
    let want: *const D = rd as *const D;
    let mut p: &&D = &re;
    p = &rd;
    let x: &D = *p;
    if x as *const D != want { return 8; }
    0
}
fn main() { std::process::exit(run()); }
