// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c17_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
impl D {
    fn me(&self) -> bool { let s: &D = self; inner(&s) == self as *const D }
}
fn run() -> i32 {
    let d = D { v: 6 };
    if !d.me() { return 17; }
    0
}
fn main() { std::process::exit(run()); }
