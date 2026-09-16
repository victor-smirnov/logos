// TWIN of bc_0915a_refeq_hb_e51_admit.logos ; TWIN: Eq -> PartialEq; vec_new -> Vec::new
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut v: Vec<D> = Vec::new();
    v.push(D { v: 4 });
    v.push(D { v: 4 });
    let r0: &D = &v[0]; let r1: &D = &v[1];
    if r0 == r1 { return 0; }
    1
}
fn main() { std::process::exit(run()); }
