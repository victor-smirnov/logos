// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h02_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
struct W { k: i64, inner: D }
fn run() -> i32 {
    let w = W { k: 1, inner: D { v: 6 } };
    match w {
        W { k: _, inner: ref q } => {
            if inner(&q) != 6 { return 2; }
        }
    }
    0
}
fn main() { std::process::exit(run()); }
