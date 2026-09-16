// TWIN of bc_0915a_refeq_hb_e05_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut a = D { v: 4 }; let mut b = D { v: 4 };
    let ra: &mut D = &mut a; let rb: &mut D = &mut b;
    if ra == rb { return 0; }
    1
}
fn main() { std::process::exit(run()); }
