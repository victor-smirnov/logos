// TWIN of tests/logos/pass/bc_0915c_argref_hb_a09_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let p = &rd;
    if inner(p) != want { return 9; }
    0
}
fn main() { std::process::exit(run()); }
