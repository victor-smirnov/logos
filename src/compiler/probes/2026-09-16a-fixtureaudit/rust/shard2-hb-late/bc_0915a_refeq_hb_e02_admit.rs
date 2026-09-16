// TWIN of bc_0915a_refeq_hb_e02_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    let ra: &D = &a; let rb: &D = &b;
    if ra != rb { return 1; }
    0
}
fn main() { std::process::exit(run()); }
