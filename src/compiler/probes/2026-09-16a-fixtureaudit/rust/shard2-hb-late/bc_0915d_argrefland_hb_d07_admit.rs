// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_d07_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let e = D { v: 7 };
    let rd: &D = &d;
    let re: &D = &e;
    let c: bool = true;
    if inner(if c { &rd } else { &re }) != rd as *const D { return 7; }
    0
}
fn main() { std::process::exit(run()); }
