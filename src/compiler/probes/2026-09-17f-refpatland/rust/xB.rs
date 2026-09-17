struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
fn main() { let o: Option<D> = Some(D { v: 3 }); let r: &Option<D> = &o; let k: i32 = 1;
    match (r, k) { (&Some(d), j) => { println!("got={} {}", d.v, j); } (&None, j) => { println!("none {}", j); } } }
