// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c06_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let o: Option<D> = Some(D { v: 6 });
    let Some(rd) = &o else { return 60; };
    if inner(&rd) != rd as *const D { return 6; }
    0
}
fn main() { std::process::exit(run()); }
