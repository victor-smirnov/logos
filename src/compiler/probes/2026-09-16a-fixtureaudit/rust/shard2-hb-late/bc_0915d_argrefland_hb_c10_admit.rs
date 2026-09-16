// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c10_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let fp: fn(&&D) -> *const D = inner;
    if fp(&rd) != rd as *const D { return 10; }
    0
}
fn main() { std::process::exit(run()); }
