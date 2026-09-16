// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c02_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let rd2: &D = rd;
    if inner(&rd2) != rd as *const D { return 2; }
    0
}
fn main() { std::process::exit(run()); }
