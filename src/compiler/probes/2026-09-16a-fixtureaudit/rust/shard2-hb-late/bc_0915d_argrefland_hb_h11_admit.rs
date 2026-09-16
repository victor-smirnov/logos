// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h11_admit.logos
struct D { v: i64 }
struct H<'a> { r: &'a &'a D }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let h = H { r: &rd };
    let x: &D = *h.r;
    if x.v != 6 { return 11; }
    0
}
fn main() { std::process::exit(run()); }
