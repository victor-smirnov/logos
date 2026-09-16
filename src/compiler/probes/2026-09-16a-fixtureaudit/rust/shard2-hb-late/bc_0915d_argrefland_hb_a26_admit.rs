// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a26_admit.logos
struct D { v: i64 }
fn inner<T>(a: &T) -> *const T { a as *const T }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let slot: *const &D = inner::<&D>(&rd);
    let back: &D = unsafe { *slot };
    if back.v != 6 { return 27; }
    0
}
fn main() { std::process::exit(run()); }
