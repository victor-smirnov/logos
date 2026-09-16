// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c05_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
struct W<'a> { r: &'a D }
fn run() -> i32 {
    let d = D { v: 6 };
    let w = W { r: &d };
    let W { r } = w;
    if inner(&r) != &d as *const D { return 5; }
    0
}
fn main() { std::process::exit(run()); }
