// TWIN of bc_0915a_refeq_hb_e49_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let a = D { v: 2 }; let b = D { v: 2 };
    let ra: &D = &a; let rb: &D = &b;
    let k: i64 = 1;
    match k {
        1 if ra == rb => { return 0; }
        _ => { return 1; }
    }
}
fn main() { std::process::exit(run()); }
