// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a03_admit.logos
struct D { v: i64 }
struct H { k: i64 }
impl H { fn take(&self, a: &&D) -> i64 { let x: &D = *a; x.v + self.k } }
fn run() -> i32 {
    let d = D { v: 6 };
    let h = H { k: 1 };
    let rd: &D = &d;
    if h.take(&rd) != 7 { return 3; }
    0
}
fn main() { std::process::exit(run()); }
