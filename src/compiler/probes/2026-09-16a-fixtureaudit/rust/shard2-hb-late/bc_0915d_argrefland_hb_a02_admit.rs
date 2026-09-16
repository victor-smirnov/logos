// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a02_admit.logos
struct D { v: i64 }
fn inner<T>(a: &&T) -> *const T { let x: &T = *a; x as *const T }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    if inner::<D>(&rd) != want { return 2; }
    0
}
fn main() { std::process::exit(run()); }
