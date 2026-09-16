// TWIN of bc_0915a_refeq_hb_e28_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    let f = |x: &D, y: &D| -> bool { x == y };
    if f(&a, &b) { return 0; }
    1
}
fn main() { std::process::exit(run()); }
