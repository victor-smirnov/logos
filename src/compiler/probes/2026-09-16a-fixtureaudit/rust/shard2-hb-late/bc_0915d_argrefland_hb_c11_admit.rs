// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c11_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn inner_m(a: &&mut D) -> i64 { a.v }
fn run() -> i32 {
    let mut d = D { v: 6 };
    let f = |rd: &mut D| -> i64 { inner_m(&rd) };
    if f(&mut d) != 6 { return 11; }
    0
}
fn main() { std::process::exit(run()); }
