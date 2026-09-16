// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a04_admit.logos
struct D { v: i64 }
struct W<'a> { r: &'a &'a D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let w = W { r: &rd };
    let x: &D = *w.r;
    if x as *const D != want { return 4; }
    0
}
fn main() { std::process::exit(run()); }
