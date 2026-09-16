// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h04_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let d = D { v: 6 };
    let mut it: Option<&D> = Some(&d);
    let mut sum: i64 = 0;
    while let Some(rd) = it {
        sum = sum + inner(&rd);
        it = None;
    }
    if sum != 6 { return 4; }
    0
}
fn main() { std::process::exit(run()); }
