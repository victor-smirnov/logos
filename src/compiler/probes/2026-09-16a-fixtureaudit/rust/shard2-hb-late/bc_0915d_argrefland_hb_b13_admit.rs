// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b13_admit.logos
struct D { v: i64 }
trait Get { fn get(&self) -> i64; }
impl Get for D { fn get(&self) -> i64 { self.v } }
fn via<T: Get>(a: &&T) -> i64 { a.get() }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    if via::<D>(&rd) != 6 { return 13; }
    0
}
fn main() { std::process::exit(run()); }
