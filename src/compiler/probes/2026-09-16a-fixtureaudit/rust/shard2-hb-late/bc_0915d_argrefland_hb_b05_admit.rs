// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b05_admit.logos
// TWIN: Logos `impl Eq` -> Rust `impl PartialEq`
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, o: &Self) -> bool { self.v == o.v } }
fn run() -> i32 {
    let d1 = D { v: 6 }; let d2 = D { v: 6 };
    let r1: &D = &d1; let r2: &D = &d2;
    if !(&r1 == &r2) { return 5; }
    0
}
fn main() { std::process::exit(run()); }
