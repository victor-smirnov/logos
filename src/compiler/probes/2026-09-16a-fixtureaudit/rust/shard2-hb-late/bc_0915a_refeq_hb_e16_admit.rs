// TWIN of bc_0915a_refeq_hb_e16_admit.logos ; TWIN: Eq -> PartialEq
struct H { s: String, k: i64 }
impl PartialEq for H { fn eq(&self, other: &H) -> bool { self.s == other.s && self.k == other.k } }
fn run() -> i32 {
    let a = H { s: String::from("heap-one"), k: 1 };
    let b = H { s: String::from("heap-one"), k: 1 };
    let ra: &H = &a; let rb: &H = &b;
    if ra == rb { return 0; }
    1
}
fn main() { std::process::exit(run()); }
