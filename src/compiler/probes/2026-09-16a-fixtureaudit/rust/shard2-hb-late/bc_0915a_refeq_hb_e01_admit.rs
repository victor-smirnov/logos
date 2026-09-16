// TWIN of tests/logos/pass/bc_0915a_refeq_hb_e01_admit.logos
// TWIN: Logos `impl Eq for D { fn eq }` -> Rust `impl PartialEq for D` (Logos's Eq carries `eq`).
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a: i64 = 4; let b: i64 = 4;
    let mut ra: &i64 = &a; let mut rb: &i64 = &b;
    let x: &mut &i64 = &mut ra; let y: &mut &i64 = &mut rb;
    if x == y { return 0; }
    1
}
fn main() { std::process::exit(run()); }
