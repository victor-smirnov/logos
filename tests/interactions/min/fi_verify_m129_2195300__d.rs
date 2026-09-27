struct D { x: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.x); } }
struct W { inner: Box<D> }
fn main() { let w = W { inner: Box::new(D { x: 4 }) }; let v = *w.inner; println!("{}", v.x); }
