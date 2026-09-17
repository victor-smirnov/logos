struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { } }
struct H { d: D }
fn main() {
    let t;
    let h = H { d: D { v: 7 } };
    let sv: (H, i64) = (h, 1);
    let r: &(H, i64) = &sv;
    match r {
        &(H { d }, k) => { t = d.v + k; }
    }
    if t != 8 { std::process::exit(1); }
    std::process::exit(0);
}
