struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { } }
struct H { d: D }
fn main() {
    let mut t;
    let h = H { d: D { v: 7 } };
    let sv: (H, i64) = (h, 1);
    match sv {
        (H { d }, k) => { t = d.v + k; }
    }
    t = t + sv.0.d.v;
    if t != 15 { std::process::exit(1); }
    std::process::exit(0);
}
