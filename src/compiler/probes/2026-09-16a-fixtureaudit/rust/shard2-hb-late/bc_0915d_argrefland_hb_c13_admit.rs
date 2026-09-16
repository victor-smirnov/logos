// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c13_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
struct G<T> { v: T }
fn ginner(a: &&G<i64>) -> *const G<i64> { let x: &G<i64> = *a; x as *const G<i64> }
fn run() -> i32 {
    let g: G<i64> = G::<i64> { v: 6 };
    let rg: &G<i64> = &g;
    if ginner(&rg) != &g as *const G<i64> { return 13; }
    0
}
fn main() { std::process::exit(run()); }
