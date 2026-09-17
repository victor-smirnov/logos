struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
struct H { d: D }
fn main() { let h = H { d: D { v: 5 } }; let r: &H = &h; let k: i32 = 1;
    match (r, k) { (&whole, j) => { println!("got={} {}", whole.d.v, j); } } }
