// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c04_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let e = D { v: 7 };
    let (ra, rb): (&D, &D) = (&d, &e);
    if inner(&ra) != &d as *const D { return 4; }
    if inner(&rb) != &e as *const D { return 40; }
    0
}
fn main() { std::process::exit(run()); }
