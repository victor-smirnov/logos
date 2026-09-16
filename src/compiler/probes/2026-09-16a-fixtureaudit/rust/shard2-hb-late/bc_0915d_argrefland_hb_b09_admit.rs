// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b09_admit.logos
struct D { v: i64 }
fn same_addr<T>(a: &&T, want: *const T) -> bool { let x: &T = *a; x as *const T == want }
fn g<T>(x: &T) -> bool { let r: &T = x; same_addr::<T>(&r, x as *const T) }
fn run() -> i32 {
    let d = D { v: 6 };
    if !g::<D>(&d) { return 9; }
    0
}
fn main() { std::process::exit(run()); }
