// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c14_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let outer = || -> bool {
        let f = || -> bool { inner(&rd) == want };
        f()
    };
    if !outer() { return 14; }
    0
}
fn main() { std::process::exit(run()); }
