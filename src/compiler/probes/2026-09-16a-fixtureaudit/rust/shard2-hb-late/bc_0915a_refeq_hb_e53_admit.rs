// TWIN of bc_0915a_refeq_hb_e53_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a = D { v: 5 }; let b = D { v: 5 };
    let ra: &D = &a;
    if ra == &b { return 0; }
    1
}
fn main() { std::process::exit(run()); }
