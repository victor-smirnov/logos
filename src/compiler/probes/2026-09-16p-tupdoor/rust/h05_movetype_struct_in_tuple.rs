use std::sync::atomic::{AtomicI64, Ordering};
static N: AtomicI64 = AtomicI64::new(0);
struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { N.fetch_add(self.v, Ordering::SeqCst); } }
struct H { d: D }
fn main() {
    let got;
    {
        let h = H { d: D { v: 7 } };
        let sv: (H, i64) = (h, 1);
        match sv {
            (H { d }, k) => { got = d.v + k; }
        }
    }
    let n2 = N.load(Ordering::SeqCst);
    println!("got={} n={}", got, n2);
    if got != 8 { std::process::exit(1); }
    if n2 != 7 { std::process::exit(2); }
    std::process::exit(0);
}
