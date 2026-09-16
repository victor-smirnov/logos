// TWIN of bc_0915a_refeq_hb_e18_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
struct P { x: D, n: i64 }
fn run() -> i32 {
    let p = P { x: D { v: 8 }, n: 1 };
    let q = P { x: D { v: 8 }, n: 2 };
    if &p.x == &q.x { return 0; }
    1
}
fn main() { std::process::exit(run()); }
