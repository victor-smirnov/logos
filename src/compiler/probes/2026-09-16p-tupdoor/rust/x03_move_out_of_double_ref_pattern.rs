struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { } }
struct P { d: D }
fn main() {
    let t;
    let pv = P { d: D { v: 5 } };
    let r: &P = &pv;
    let pp: &&P = &r;
    let k: i64 = 1;
    match (pp, k) {
        (&&P { d }, j) => { t = d.v + j; }
    }
    if t != 6 { std::process::exit(1); }
    std::process::exit(0);
}
