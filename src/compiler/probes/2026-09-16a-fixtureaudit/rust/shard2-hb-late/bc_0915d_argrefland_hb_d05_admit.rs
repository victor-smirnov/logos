// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_d05_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn both(a: [&&D; 2]) -> *const D { let x: &D = *a[1]; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    if both([&rd, &rd]) != rd as *const D { return 5; }
    0
}
fn main() { std::process::exit(run()); }
