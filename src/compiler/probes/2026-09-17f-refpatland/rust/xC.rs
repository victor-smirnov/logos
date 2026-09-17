struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
fn main() { let t: (D, i64) = (D { v: 4 }, 9); let r: &(D, i64) = &t; let k: i32 = 1;
    match (r, k) { (&(d, w), j) => { println!("got={} {} {}", d.v, w, j); } } }
