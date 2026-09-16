// TWIN of bc_0915a_refeq_hb_e29_admit.logos ; TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
impl D { fn same(&self, o: &D) -> bool { self == o } }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    if a.same(&b) { return 0; }
    1
}
fn main() { std::process::exit(run()); }
