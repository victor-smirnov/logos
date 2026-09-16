// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_d06_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
static G: D = D { v: 6 };
fn run() -> i32 {
    let rd: &D = &G;
    if inner(&rd) != rd as *const D { return 6; }
    0
}
fn main() { std::process::exit(run()); }
