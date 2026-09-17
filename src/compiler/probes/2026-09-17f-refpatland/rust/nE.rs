struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
struct H { d: D }
fn main() { let h = H { d: D { v: 2 } }; let r: &H = &h; let rr: &&H = &r; let k: i32 = 1;
    match (rr, k) { (&&H { d: ref q }, j) => { println!("got={} {}", q.v, j); } } }
