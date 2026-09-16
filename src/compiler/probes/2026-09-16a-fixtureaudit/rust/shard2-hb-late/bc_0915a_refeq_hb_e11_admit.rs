// TWIN of bc_0915a_refeq_hb_e11_admit.logos ; TWIN: Eq -> PartialEq (bound and impl)
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn same<T: PartialEq>(a: &T, b: &T) -> bool { a == b }
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    if same::<D>(&a, &b) { return 0; }
    1
}
fn main() { std::process::exit(run()); }
