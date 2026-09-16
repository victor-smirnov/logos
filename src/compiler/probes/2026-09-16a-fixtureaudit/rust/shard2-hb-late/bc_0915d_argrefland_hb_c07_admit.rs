// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c07_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn get<'a>(x: &'a D) -> &'a D { x }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = get(&d);
    if inner(&rd) != &d as *const D { return 7; }
    0
}
fn main() { std::process::exit(run()); }
