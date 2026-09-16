// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_d03_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    match &rd {
        r => { if inner(r) != rd as *const D { return 3; } }
    }
    0
}
fn main() { std::process::exit(run()); }
