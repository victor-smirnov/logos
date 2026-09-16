// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b17_admit.logos
struct D { v: i64 }
fn pick<'a, 'b>(a: &'b &'a D) -> &'b D { *a }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let back: &D = pick(&rd);
    if back.v != 6 { return 17; }
    0
}
fn main() { std::process::exit(run()); }
