// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_d02_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let p: *const &D = &rd as *const &D;
    let back: &D = unsafe { *p };
    if back as *const D != rd as *const D { return 2; }
    0
}
fn main() { std::process::exit(run()); }
