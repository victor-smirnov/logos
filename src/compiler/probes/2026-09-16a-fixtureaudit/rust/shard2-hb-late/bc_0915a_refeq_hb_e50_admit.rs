// TWIN of bc_0915a_refeq_hb_e50_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
struct P { x: D, y: D }
fn run() -> i32 {
    let p = P { x: D { v: 3 }, y: D { v: 3 } };
    match &p {
        P { x, y } => {
            if x == y { return 0; }
            return 1;
        }
    }
}
fn main() { std::process::exit(run()); }
