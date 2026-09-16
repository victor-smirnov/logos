// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c12_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn apply<F: Fn(&D) -> bool>(f: F, d: &D) -> bool { f(d) }
fn run() -> i32 {
    let d = D { v: 6 };
    let want: *const D = &d as *const D;
    if !apply(|rd: &D| -> bool { inner(&rd) == want }, &d) { return 12; }
    0
}
fn main() { std::process::exit(run()); }
